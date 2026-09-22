// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Utf16RegexBridge — see Utf16RegexBridge.h. UI-free by construction (no
// precomp.h, no Win32 dependency at all): this TU hand-rolls UTF-8 encoding
// so it needs nothing but the standard library, keeping it trivially
// includable from both find.cpp and a headless test binary.

#include "common/text/Utf16RegexBridge.h"

namespace sally::text
{
namespace
{

std::uint16_t ReadUnit(const std::uint8_t* p, bool bigEndian)
{
    return bigEndian ? (std::uint16_t)((p[0] << 8) | p[1])
                     : (std::uint16_t)((p[1] << 8) | p[0]);
}

// Appends the UTF-8 encoding of one Unicode scalar value (never a lone
// surrogate — callers combine pairs or substitute U+FFFD before calling this),
// pushing 'rawOffset' once per output byte so the offset table stays in
// lockstep with outUtf8's length.
void AppendUtf8(std::uint32_t codepoint, std::int64_t rawOffset, std::string& outUtf8,
                std::vector<std::int64_t>& outOffsets)
{
    if (codepoint < 0x80)
    {
        outUtf8.push_back((char)codepoint);
        outOffsets.push_back(rawOffset);
    }
    else if (codepoint < 0x800)
    {
        outUtf8.push_back((char)(0xC0 | (codepoint >> 6)));
        outUtf8.push_back((char)(0x80 | (codepoint & 0x3F)));
        outOffsets.push_back(rawOffset);
        outOffsets.push_back(rawOffset);
    }
    else if (codepoint < 0x10000)
    {
        outUtf8.push_back((char)(0xE0 | (codepoint >> 12)));
        outUtf8.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
        outUtf8.push_back((char)(0x80 | (codepoint & 0x3F)));
        outOffsets.push_back(rawOffset);
        outOffsets.push_back(rawOffset);
        outOffsets.push_back(rawOffset);
    }
    else
    {
        outUtf8.push_back((char)(0xF0 | (codepoint >> 18)));
        outUtf8.push_back((char)(0x80 | ((codepoint >> 12) & 0x3F)));
        outUtf8.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
        outUtf8.push_back((char)(0x80 | (codepoint & 0x3F)));
        outOffsets.push_back(rawOffset);
        outOffsets.push_back(rawOffset);
        outOffsets.push_back(rawOffset);
        outOffsets.push_back(rawOffset);
    }
}

} // namespace

std::size_t DecodeUtf16ToUtf8(const std::uint8_t* data, std::size_t size, bool bigEndian,
                              std::string& outUtf8, std::vector<std::int64_t>& outOffsets)
{
    std::size_t i = 0;
    while (i + 1 < size)
    {
        const std::uint16_t unit = ReadUnit(data + i, bigEndian);
        if (unit >= 0xD800 && unit <= 0xDBFF)
        {
            // High surrogate: needs a low surrogate immediately after it.
            if (i + 3 >= size)
                break; // the low half is not in this window yet - stop BEFORE the high surrogate
            const std::uint16_t low = ReadUnit(data + i + 2, bigEndian);
            if (low >= 0xDC00 && low <= 0xDFFF)
            {
                const std::uint32_t codepoint =
                    0x10000 + (((std::uint32_t)(unit - 0xD800)) << 10) + (low - 0xDC00);
                AppendUtf8(codepoint, (std::int64_t)i, outUtf8, outOffsets);
                i += 4;
            }
            else
            {
                // Lone high surrogate followed by something else: malformed, not
                // incomplete (there IS a following unit, it's just the wrong kind).
                AppendUtf8(0xFFFD, (std::int64_t)i, outUtf8, outOffsets);
                i += 2;
            }
        }
        else if (unit >= 0xDC00 && unit <= 0xDFFF)
        {
            // Lone low surrogate: malformed.
            AppendUtf8(0xFFFD, (std::int64_t)i, outUtf8, outOffsets);
            i += 2;
        }
        else
        {
            AppendUtf8(unit, (std::int64_t)i, outUtf8, outOffsets);
            i += 2;
        }
    }
    return i;
}

} // namespace sally::text
