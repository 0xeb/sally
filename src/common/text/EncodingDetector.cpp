// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// EncodingDetector — see EncodingDetector.h. UI-free by construction: this TU
// must keep compiling without precomp.h so tests can build it directly.

#ifdef SALLY_ENCODING_DETECTOR_STANDALONE
#include <windows.h>
#else
#include "precomp.h"
#endif

#include "common/text/EncodingDetector.h"

namespace sally::text
{
namespace
{

// The BOMs Sally recognises, longest first so UTF-32 cannot be mistaken for
// UTF-16LE (a UTF-32LE BOM starts with the UTF-16LE one). UTF-32 is not a
// supported decode target, but recognising the mark keeps us from decoding
// UTF-32 as UTF-16 and producing garbage: it falls through to LegacyBytes.
struct BomPattern
{
    const std::uint8_t bytes[4];
    std::size_t length;
    Encoding encoding;
    bool supported;
};

const BomPattern kBoms[] = {
    {{0xFF, 0xFE, 0x00, 0x00}, 4, Encoding::LegacyBytes, false}, // UTF-32LE
    {{0x00, 0x00, 0xFE, 0xFF}, 4, Encoding::LegacyBytes, false}, // UTF-32BE
    {{0xEF, 0xBB, 0xBF, 0x00}, 3, Encoding::Utf8, true},
    {{0xFF, 0xFE, 0x00, 0x00}, 2, Encoding::Utf16Le, true},
    {{0xFE, 0xFF, 0x00, 0x00}, 2, Encoding::Utf16Be, true},
};

bool StartsWith(const std::uint8_t* data, std::size_t size, const BomPattern& bom)
{
    if (size < bom.length)
        return false;
    for (std::size_t i = 0; i < bom.length; i++)
        if (data[i] != bom.bytes[i])
            return false;
    return true;
}

std::size_t Utf8SequenceLength(std::uint8_t lead)
{
    if (lead < 0x80)
        return 1;
    if ((lead & 0xE0) == 0xC0)
        return 2;
    if ((lead & 0xF0) == 0xE0)
        return 3;
    if ((lead & 0xF8) == 0xF0)
        return 4;
    return 0; // continuation byte or invalid lead
}

} // namespace

bool ValidateUtf8(const std::uint8_t* data, std::size_t size,
                  bool allowTruncatedTail, bool* sawMultiByte)
{
    if (sawMultiByte != nullptr)
        *sawMultiByte = false;
    if (data == nullptr)
        return false;

    std::size_t i = 0;
    while (i < size)
    {
        const std::size_t length = Utf8SequenceLength(data[i]);
        if (length == 0)
            return false; // stray continuation byte or F8..FF

        if (i + length > size)
        {
            // The window ended mid-sequence. When the caller is reading in
            // blocks that is expected and not evidence of invalidity; otherwise
            // a truncated sequence means the content is not valid UTF-8.
            return allowTruncatedTail;
        }

        if (length == 1)
        {
            i++;
            continue;
        }

        // Continuation bytes must all be 10xxxxxx.
        for (std::size_t k = 1; k < length; k++)
            if ((data[i + k] & 0xC0) != 0x80)
                return false;

        // Decode to reject the forms a permissive decoder would accept.
        std::uint32_t scalar = 0;
        switch (length)
        {
        case 2:
            scalar = ((std::uint32_t)(data[i] & 0x1F) << 6) |
                     (std::uint32_t)(data[i + 1] & 0x3F);
            if (scalar < 0x80)
                return false; // overlong
            break;
        case 3:
            scalar = ((std::uint32_t)(data[i] & 0x0F) << 12) |
                     ((std::uint32_t)(data[i + 1] & 0x3F) << 6) |
                     (std::uint32_t)(data[i + 2] & 0x3F);
            if (scalar < 0x800)
                return false; // overlong
            if (scalar >= 0xD800 && scalar <= 0xDFFF)
                return false; // surrogate half encoded as UTF-8 (CESU-8 style)
            break;
        case 4:
            scalar = ((std::uint32_t)(data[i] & 0x07) << 18) |
                     ((std::uint32_t)(data[i + 1] & 0x3F) << 12) |
                     ((std::uint32_t)(data[i + 2] & 0x3F) << 6) |
                     (std::uint32_t)(data[i + 3] & 0x3F);
            if (scalar < 0x10000)
                return false; // overlong
            if (scalar > 0x10FFFF)
                return false; // outside Unicode
            break;
        default:
            return false;
        }

        if (sawMultiByte != nullptr)
            *sawMultiByte = true;
        i += length;
    }
    return true;
}

bool LooksLikeUtf16(const std::uint8_t* data, std::size_t size, bool bigEndian)
{
    if (data == nullptr || size < 2)
        return false;

    bool expectLowSurrogate = false;
    bool sawAnyUnit = false;
    // Whole code units only; a trailing odd byte is a window edge, not evidence.
    for (std::size_t i = 0; i + 1 < size; i += 2)
    {
        const std::uint16_t unit = bigEndian
                                       ? (std::uint16_t)((data[i] << 8) | data[i + 1])
                                       : (std::uint16_t)((data[i + 1] << 8) | data[i]);
        sawAnyUnit = true;

        // Values that do not occur in text: NUL, and the two permanent
        // noncharacters that a mis-guessed byte order produces immediately.
        if (unit == 0x0000 || unit == 0xFFFE || unit == 0xFFFF)
            return false;

        const bool isHigh = unit >= 0xD800 && unit <= 0xDBFF;
        const bool isLow = unit >= 0xDC00 && unit <= 0xDFFF;
        if (expectLowSurrogate)
        {
            if (!isLow)
                return false; // high surrogate not followed by a low one
            expectLowSurrogate = false;
            continue;
        }
        if (isLow)
            return false; // low surrogate without a preceding high one
        if (isHigh)
            expectLowSurrogate = true;
    }

    // A high surrogate at the very end may be completed by the next window.
    return sawAnyUnit;
}

DetectionResult Detect(const std::uint8_t* data, std::size_t size,
                       const DetectionOptions& options)
{
    DetectionResult result;
    if (data == nullptr || size == 0)
        return result;

    // 1. BOM evidence wins outright.
    for (const BomPattern& bom : kBoms)
    {
        if (!StartsWith(data, size, bom))
            continue;
        if (!bom.supported)
        {
            // Recognised but not a decode target (UTF-32): stay on bytes rather
            // than decode it as something it is not.
            result.encoding = Encoding::LegacyBytes;
            result.confidence = Confidence::Fallback;
            result.textOffset = 0;
            return result;
        }
        result.encoding = bom.encoding;
        result.confidence = Confidence::FromBom;
        result.textOffset = (std::int64_t)bom.length;
        return result;
    }

    const std::size_t window = size < options.scanBudget ? size : options.scanBudget;

    // 2. BOM-less UTF-16. Tested before UTF-8 because ASCII-heavy UTF-16LE text
    //    is full of NUL bytes, which UTF-8 validation would reject anyway, while
    //    the reverse (UTF-8 text passing the UTF-16 test) is what the NUL and
    //    noncharacter rejections above prevent.
    if (options.allowUtf16Heuristic && window >= 4)
    {
        const bool le = LooksLikeUtf16(data, window, false);
        const bool be = LooksLikeUtf16(data, window, true);
        if (le != be)
        {
            // Exactly one byte order yields plausible code units.
            result.encoding = le ? Encoding::Utf16Le : Encoding::Utf16Be;
            result.confidence = Confidence::Heuristic;
            result.textOffset = 0;
            return result;
        }
        if (le && be)
        {
            // Both orders produce plausible code units, which is the NORMAL case
            // for text: 'a' is 0x0061 read one way and 0x6100 the other, and
            // neither is forbidden. The decisive evidence is WHERE the zero bytes
            // sit. Every character below U+0100 - i.e. all ASCII, and real text
            // files are full of it - contributes exactly one zero byte, at an ODD
            // offset in little-endian and an EVEN offset in big-endian.
            std::size_t zerosAtEven = 0;
            std::size_t zerosAtOdd = 0;
            for (std::size_t i = 0; i < window; i++)
            {
                if (data[i] != 0x00)
                    continue;
                if ((i & 1) == 0)
                    zerosAtEven++;
                else
                    zerosAtOdd++;
            }
            // Require a clear asymmetry. No zero bytes at all means the content
            // is either not UTF-16 or is entirely non-Latin text whose byte order
            // genuinely cannot be inferred - refuse instead of flipping a coin.
            const std::size_t zeros = zerosAtEven + zerosAtOdd;
            if (zeros > 0)
            {
                const std::size_t high = zerosAtOdd > zerosAtEven ? zerosAtOdd : zerosAtEven;
                const std::size_t low = zerosAtOdd > zerosAtEven ? zerosAtEven : zerosAtOdd;
                if (high >= 2 && high >= low * 4)
                {
                    result.encoding = zerosAtOdd > zerosAtEven ? Encoding::Utf16Le
                                                               : Encoding::Utf16Be;
                    result.confidence = Confidence::Heuristic;
                    result.textOffset = 0;
                    return result;
                }
            }
        }
    }

    // 3. BOM-less UTF-8, and only when a multi-byte sequence actually appeared:
    //    plain ASCII is valid UTF-8 but says nothing, so it stays LegacyBytes
    //    and keeps the legacy byte path for legacy files.
    if (options.allowUtf8Heuristic)
    {
        bool sawMultiByte = false;
        if (ValidateUtf8(data, window, options.windowMayEndMidCharacter, &sawMultiByte) &&
            sawMultiByte)
        {
            result.encoding = Encoding::Utf8;
            result.confidence = Confidence::Heuristic;
            result.textOffset = 0;
            return result;
        }
    }

    // 4. Honest "I do not know".
    return result;
}

} // namespace sally::text
