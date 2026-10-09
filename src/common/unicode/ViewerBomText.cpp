// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ViewerBomText.h"

#include <algorithm>
#include <cwctype>
#include <limits>

namespace Sally::Unicode
{
namespace
{

constexpr std::uint32_t Replacement = 0xFFFD;

bool IsHighSurrogate(std::uint16_t value)
{
    return value >= 0xD800 && value <= 0xDBFF;
}

bool IsLowSurrogate(std::uint16_t value)
{
    return value >= 0xDC00 && value <= 0xDFFF;
}

std::uint32_t DecodeSurrogatePair(std::uint16_t high, std::uint16_t low)
{
    return 0x10000 + (((std::uint32_t)high - 0xD800) << 10) + ((std::uint32_t)low - 0xDC00);
}

std::uint16_t ReadUtf16(const std::uint8_t* data, BomEncoding encoding)
{
    if (encoding == BomEncoding::Utf16Le)
        return (std::uint16_t)data[0] | ((std::uint16_t)data[1] << 8);
    return ((std::uint16_t)data[0] << 8) | (std::uint16_t)data[1];
}

bool IsContinuation(std::uint8_t value)
{
    return (value & 0xC0) == 0x80;
}

std::uint32_t FoldScalar(std::uint32_t scalar, bool caseSensitive)
{
    if (caseSensitive)
        return scalar;
    if (scalar <= std::numeric_limits<wchar_t>::max())
        return (std::uint32_t)towlower((wchar_t)scalar);
    return scalar;
}

bool IsWordScalar(std::uint32_t scalar)
{
    if (scalar == L'_')
        return true;
    if (scalar <= std::numeric_limits<wchar_t>::max())
        return iswalnum((wchar_t)scalar) != 0;
    return true;
}

std::vector<std::uint32_t> PatternScalars(const std::wstring& pattern)
{
    std::vector<std::uint32_t> scalars;
    for (std::size_t i = 0; i < pattern.size();)
    {
        std::uint32_t scalar = (std::uint32_t)pattern[i++];
        if (sizeof(wchar_t) == 2 && IsHighSurrogate((std::uint16_t)scalar) && i < pattern.size())
        {
            std::uint16_t low = (std::uint16_t)pattern[i];
            if (IsLowSurrogate(low))
            {
                scalar = DecodeSurrogatePair((std::uint16_t)scalar, low);
                i++;
            }
        }
        scalars.push_back(scalar);
    }
    return scalars;
}

bool MatchAt(const DecodedRun& run, const std::vector<std::uint32_t>& pattern, bool caseSensitive,
             bool wholeWords, std::size_t cell)
{
    if (cell + pattern.size() > run.CellCount())
        return false;
    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        if (FoldScalar(run.Scalars[cell + i], caseSensitive) != FoldScalar(pattern[i], caseSensitive))
            return false;
    }
    if (wholeWords)
    {
        if (cell > 0 && IsWordScalar(run.Scalars[cell - 1]))
            return false;
        if (cell + pattern.size() < run.CellCount() && IsWordScalar(run.Scalars[cell + pattern.size()]))
            return false;
    }
    return true;
}

} // namespace

void DecodedRun::Clear()
{
    Text.clear();
    CellTextIndex.clear();
    RawStart.clear();
    RawEnd.clear();
    Scalars.clear();
    RawBytesConsumed = 0;
}

std::size_t DecodedRun::TextIndexForCellEnd(std::size_t cell) const
{
    if (cell >= CellTextIndex.size())
        return Text.size();
    return CellTextIndex[cell];
}

void DecodedRun::AppendCell(std::uint32_t scalar, std::int64_t rawStart, std::int64_t rawEnd)
{
    CellTextIndex.push_back(Text.size());
    RawStart.push_back(rawStart);
    RawEnd.push_back(rawEnd);
    Scalars.push_back(scalar);

    if (scalar > 0x10FFFF)
        scalar = Replacement;

    if (sizeof(wchar_t) == 2 && scalar > 0xFFFF)
    {
        scalar -= 0x10000;
        Text.push_back((wchar_t)(0xD800 + (scalar >> 10)));
        Text.push_back((wchar_t)(0xDC00 + (scalar & 0x3FF)));
    }
    else
    {
        Text.push_back((wchar_t)scalar);
    }
}

void DecodedRun::AppendRun(const DecodedRun& other)
{
    // Bulk-append rather than replaying AppendCell per cell. DecodeTextRange concatenates a
    // freshly decoded chunk into an accumulating run, so the per-cell form made every character
    // pay for a second full materialisation - five push_backs each - purely to copy data that
    // was already laid out correctly.
    //
    // This is exactly equivalent because 'other' was itself built by AppendCell: its Text and
    // CellTextIndex are already what AppendCell would produce, and Text indices only need
    // rebasing onto our current Text length. Note AppendCell stores the ORIGINAL scalar in
    // Scalars while encoding a clamped value into Text, so copying both verbatim preserves that
    // asymmetry - re-deriving Text from Scalars here would not.
    const std::size_t count = other.CellCount();
    if (count == 0)
    {
        RawBytesConsumed += other.RawBytesConsumed;
        return;
    }

    const std::size_t textBase = Text.size();
    CellTextIndex.reserve(CellTextIndex.size() + count);
    RawStart.reserve(RawStart.size() + count);
    RawEnd.reserve(RawEnd.size() + count);
    Scalars.reserve(Scalars.size() + count);
    Text.reserve(textBase + other.Text.size());

    for (std::size_t i = 0; i < count; ++i)
        CellTextIndex.push_back(textBase + other.CellTextIndex[i]);
    RawStart.insert(RawStart.end(), other.RawStart.begin(), other.RawStart.end());
    RawEnd.insert(RawEnd.end(), other.RawEnd.begin(), other.RawEnd.end());
    Scalars.insert(Scalars.end(), other.Scalars.begin(), other.Scalars.end());
    Text.append(other.Text);

    RawBytesConsumed += other.RawBytesConsumed;
}

BomInfo DetectBom(const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr)
        return {};
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        return {BomEncoding::Utf8, 3};
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        return {BomEncoding::Utf16Le, 2};
    if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF)
        return {BomEncoding::Utf16Be, 2};
    return {};
}

bool IsDecodedEncoding(BomEncoding encoding)
{
    return encoding == BomEncoding::Utf8 || encoding == BomEncoding::Utf16Le ||
           encoding == BomEncoding::Utf16Be;
}

std::int64_t AlignToCodeUnit(BomEncoding encoding, std::int64_t offset, std::int64_t textOffset)
{
    if (offset < textOffset)
        return textOffset;
    if (encoding == BomEncoding::Utf16Le || encoding == BomEncoding::Utf16Be)
        return textOffset + ((offset - textOffset) & ~1ll);
    return offset;
}

ScalarStep NextScalar(BomEncoding encoding, const std::uint8_t* data, std::size_t size, bool atEnd)
{
    ScalarStep step;
    if (data == nullptr || size == 0)
    {
        step.NeedMore = !atEnd;
        return step;
    }

    if (encoding == BomEncoding::Utf8)
    {
        const std::uint8_t lead = data[0];
        std::uint32_t scalar = 0;
        std::size_t need = 0;
        std::uint32_t minScalar = 0;
        if (lead < 0x80)
        {
            step.Scalar = lead;
            step.Length = 1;
            return step;
        }
        if (lead >= 0xC2 && lead <= 0xDF)
        {
            scalar = lead & 0x1F;
            need = 2;
            minScalar = 0x80;
        }
        else if (lead >= 0xE0 && lead <= 0xEF)
        {
            scalar = lead & 0x0F;
            need = 3;
            minScalar = 0x800;
        }
        else if (lead >= 0xF0 && lead <= 0xF4)
        {
            scalar = lead & 0x07;
            need = 4;
            minScalar = 0x10000;
        }
        else
        {
            step.Scalar = Replacement;
            step.Length = 1;
            return step;
        }

        // A byte that does not continue the sequence ends it: the lead alone is invalid and
        // the byte is decoded on its own, also when the data stops early.
        const std::size_t present = size < need ? size : need;
        for (std::size_t j = 1; j < present; ++j)
        {
            if (!IsContinuation(data[j]))
            {
                step.Scalar = Replacement;
                step.Length = 1;
                return step;
            }
            scalar = (scalar << 6) | (data[j] & 0x3F);
        }
        if (present < need)
        {
            if (!atEnd)
            {
                step.NeedMore = true;
                return step;
            }
            step.Scalar = Replacement;
            step.Length = (std::uint8_t)present;
            return step;
        }

        step.Length = (std::uint8_t)need;
        step.Scalar = (scalar < minScalar || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF))
                          ? Replacement
                          : scalar;
        return step;
    }

    if (encoding == BomEncoding::Utf16Le || encoding == BomEncoding::Utf16Be)
    {
        if (size < 2)
        {
            if (!atEnd)
            {
                step.NeedMore = true;
                return step;
            }
            step.Scalar = Replacement; // an odd trailing byte
            step.Length = 1;
            return step;
        }
        const std::uint16_t first = ReadUtf16(data, encoding);
        if (IsHighSurrogate(first))
        {
            if (size < 4)
            {
                if (!atEnd)
                {
                    step.NeedMore = true;
                    return step;
                }
                step.Scalar = Replacement;
                step.Length = 2;
                return step;
            }
            const std::uint16_t second = ReadUtf16(data + 2, encoding);
            if (IsLowSurrogate(second))
            {
                step.Scalar = DecodeSurrogatePair(first, second);
                step.Length = 4;
                return step;
            }
            step.Scalar = Replacement;
            step.Length = 2;
            return step;
        }
        step.Scalar = IsLowSurrogate(first) ? Replacement : first;
        step.Length = 2;
        return step;
    }

    step.Scalar = data[0];
    step.Length = 1;
    return step;
}

DecodedRun DecodeBytes(BomEncoding encoding, const std::uint8_t* data, std::size_t size,
                       std::int64_t rawOffset, bool flush)
{
    DecodedRun run;
    if (data == nullptr || size == 0 || !IsDecodedEncoding(encoding))
        return run;

    // Upper bound on cells is known at entry, so pay one allocation per container instead of
    // ~10 geometric reallocations each. UTF-16 consumes two bytes per code unit; UTF-8 is one
    // byte per cell in the worst case (all ASCII).
    const std::size_t maxCells =
        (encoding == BomEncoding::Utf16Le || encoding == BomEncoding::Utf16Be) ? (size / 2) + 1
                                                                              : size;
    run.CellTextIndex.reserve(maxCells);
    run.RawStart.reserve(maxCells);
    run.RawEnd.reserve(maxCells);
    run.Scalars.reserve(maxCells);
    run.Text.reserve(maxCells);

    std::size_t i = 0;
    while (i < size)
    {
        const ScalarStep step = NextScalar(encoding, data + i, size - i, flush);
        if (step.NeedMore || step.Length == 0)
            break;
        run.AppendCell(step.Scalar, rawOffset + (std::int64_t)i, rawOffset + (std::int64_t)(i + step.Length));
        i += step.Length;
    }
    run.RawBytesConsumed = i;
    return run;
}

std::size_t CountPatternCells(const std::wstring& pattern)
{
    return PatternScalars(pattern).size();
}

bool FindLiteralForward(const DecodedRun& run, const std::wstring& pattern, bool caseSensitive,
                        bool wholeWords, std::size_t startCell, LiteralMatch& match)
{
    std::vector<std::uint32_t> scalars = PatternScalars(pattern);
    if (scalars.empty() || scalars.size() > run.CellCount())
        return false;

    for (std::size_t cell = std::min(startCell, run.CellCount()); cell + scalars.size() <= run.CellCount(); ++cell)
    {
        if (!MatchAt(run, scalars, caseSensitive, wholeWords, cell))
            continue;
        match.CellIndex = cell;
        match.CellCount = scalars.size();
        match.RawStart = run.RawStart[cell];
        match.RawEnd = run.RawEnd[cell + scalars.size() - 1];
        return true;
    }
    return false;
}

bool FindLiteralBackward(const DecodedRun& run, const std::wstring& pattern, bool caseSensitive,
                         bool wholeWords, std::size_t cellLimit, LiteralMatch& match)
{
    std::vector<std::uint32_t> scalars = PatternScalars(pattern);
    if (scalars.empty() || scalars.size() > run.CellCount())
        return false;

    cellLimit = std::min(cellLimit, run.CellCount());
    if (cellLimit < scalars.size())
        return false;

    for (std::size_t cell = cellLimit - scalars.size() + 1; cell-- > 0;)
    {
        if (!MatchAt(run, scalars, caseSensitive, wholeWords, cell))
            continue;
        match.CellIndex = cell;
        match.CellCount = scalars.size();
        match.RawStart = run.RawStart[cell];
        match.RawEnd = run.RawEnd[cell + scalars.size() - 1];
        return true;
    }
    return false;
}

} // namespace Sally::Unicode
