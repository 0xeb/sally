// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace Sally::Unicode
{

enum class BomEncoding
{
    LegacyBytes,
    Utf8,
    Utf16Le,
    Utf16Be,
};

struct BomInfo
{
    BomEncoding Encoding = BomEncoding::LegacyBytes;
    std::int64_t TextOffset = 0;
};

struct DecodedRun
{
    std::wstring Text;
    std::vector<std::size_t> CellTextIndex;
    std::vector<std::int64_t> RawStart;
    std::vector<std::int64_t> RawEnd;
    std::vector<std::uint32_t> Scalars;
    std::size_t RawBytesConsumed = 0;

    void Clear();
    std::size_t CellCount() const { return RawStart.size(); }
    std::size_t TextIndexForCellEnd(std::size_t cell) const;
    void AppendCell(std::uint32_t scalar, std::int64_t rawStart, std::int64_t rawEnd);
    void AppendRun(const DecodedRun& other);
};

struct LiteralMatch
{
    std::size_t CellIndex = 0;
    std::size_t CellCount = 0;
    std::int64_t RawStart = 0;
    std::int64_t RawEnd = 0;
};

BomInfo DetectBom(const std::uint8_t* data, std::size_t size);
bool IsDecodedEncoding(BomEncoding encoding);
std::int64_t AlignToCodeUnit(BomEncoding encoding, std::int64_t offset, std::int64_t textOffset);

// One decoded scalar at the start of 'data'. 'Length' raw bytes belong to it. NeedMore means
// the bytes end inside a sequence and 'atEnd' is false: the caller must supply more bytes.
// With 'atEnd' an incomplete sequence decodes as U+FFFD covering only the bytes that really
// continue it, so a line feed, carriage return or NUL byte is always a scalar of its own.
struct ScalarStep
{
    std::uint32_t Scalar = 0;
    std::uint8_t Length = 0;
    bool NeedMore = false;
};
ScalarStep NextScalar(BomEncoding encoding, const std::uint8_t* data, std::size_t size, bool atEnd);

DecodedRun DecodeBytes(BomEncoding encoding, const std::uint8_t* data, std::size_t size,
                       std::int64_t rawOffset, bool flush);

std::size_t CountPatternCells(const std::wstring& pattern);
bool FindLiteralForward(const DecodedRun& run, const std::wstring& pattern, bool caseSensitive,
                        bool wholeWords, std::size_t startCell, LiteralMatch& match);
bool FindLiteralBackward(const DecodedRun& run, const std::wstring& pattern, bool caseSensitive,
                         bool wholeWords, std::size_t cellLimit, LiteralMatch& match);

} // namespace Sally::Unicode
