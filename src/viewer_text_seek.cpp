// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Walking back from a position to the rows before it, for decoded text.
//
// The rules for which row a position belongs to are the viewer's long-standing ones (its byte
// scanner, CViewerWindow::FindPreviousEOL), so decoded text scrolls exactly like code-page text:
// the line start is found by scanning code units backward from the position, never by reading
// the file from its beginning, and the wrapped rows of that one line are then laid out forward
// with the same column rule the painter uses (TextEngine::LayoutRow).

#include "viewer_text_core.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace Sally::Viewer
{

using Sally::Unicode::ScalarStep;

namespace
{

constexpr std::uint32_t Cr = 0x0D;
constexpr std::uint32_t Lf = 0x0A;
constexpr std::uint32_t Tab = 0x09;
constexpr std::int64_t NoBoundary = (std::numeric_limits<std::int64_t>::max)();

} // namespace

TextStatus TextEngine::FindPreviousLine(const PreviousLineQuery& query, PreviousLineResult& result, int* lines)
{
    result = PreviousLineResult();
    const std::int64_t textStart = m_config.TextStart;
    const std::int64_t size = m_config.FileSize;
    const int unit = UnitSize();
    const EolPolicy& eol = m_config.Eol;
    ChunkedByteSource::Scope scope(m_source);

    std::int64_t seek = Align(std::min(std::max(query.Seek, textStart), std::max(size, textStart)));
    const std::int64_t minSeek = std::max(query.MinSeek, textStart);
    bool takeLineBegin = query.TakeLineBegin;
    bool addLineIfSeekIsWrap = query.AddLineIfSeekIsWrap;

    // A '\n' at 'seek' itself: 'seek' then sits between the CR and the LF of a CR LF pair.
    std::int64_t lf = -2;
    if (seek < size)
    {
        std::uint32_t value = 0;
        if (!ReadUnit(seek, value))
            return TextStatus::IoError;
        if (value == Lf)
            lf = seek;
    }

    // Scan code units backward for the end of the previous line. A line feed, carriage return
    // or NUL code unit is always a scalar of its own (see Sally::Unicode::NextScalar), so no
    // decoding is needed on the way back.
    constexpr std::uint32_t CrLf = Cr + Lf; // a CR LF pair: the line starts after the LF
    constexpr std::uint32_t Nul = 1;
    std::uint32_t kind = 0;
    bool failed = false;
    std::int64_t pos = seek - unit;
    const std::uint8_t* chunk = nullptr;
    std::int64_t chunkStart = 0;
    std::int64_t chunkEnd = 0;
    for (; pos >= textStart; pos -= unit)
    {
        if (pos + 2 * unit < minSeek) // every line start from here on lies before minSeek
        {
            failed = true;
            break;
        }
        if (m_config.LongLineBytes > 0 && seek - pos > m_config.LongLineBytes)
            return TextStatus::LongLine;

        std::uint32_t value = 0;
        if (chunk == nullptr || pos < chunkStart || pos + unit > chunkEnd)
        {
            std::size_t available = 0;
            if (!m_source.ViewBefore(pos + unit, chunk, available))
                return TextStatus::IoError;
            chunkEnd = pos + unit;
            chunkStart = chunkEnd - (std::int64_t)available;
        }
        if (pos >= chunkStart)
        {
            const std::uint8_t* at = chunk + (pos - chunkStart);
            if (unit == 1)
                value = at[0];
            else if (m_config.Encoding == BomEncoding::Utf16Le)
                value = (std::uint32_t)at[0] | ((std::uint32_t)at[1] << 8);
            else
                value = ((std::uint32_t)at[0] << 8) | (std::uint32_t)at[1];
        }
        else
        {
            // A code unit split between two chunks.
            chunk = nullptr;
            if (!ReadUnit(pos, value))
                return TextStatus::IoError;
        }
        if (m_work != nullptr)
            m_work->BytesScannedBack += unit;

        if (value > Cr)
            continue;
        if (value == Lf)
        {
            if (eol.Lf)
            {
                kind = Lf;
                break;
            }
            lf = pos;
        }
        else if (value == Cr)
        {
            if (lf == pos + unit && eol.Crlf)
            {
                kind = CrLf;
                break;
            }
            if (eol.Cr)
            {
                kind = Cr;
                break;
            }
        }
        else if (value == 0 && eol.Nul)
        {
            kind = Nul;
            break;
        }
    }

    std::int64_t lineBegin = textStart;
    std::int64_t previousLineEnd = -1;
    if (!failed)
    {
        if (kind == 0)
        {
            // The text start: the first line has no line before it.
            lineBegin = previousLineEnd = textStart;
            failed = lineBegin < minSeek;
        }
        else if (kind == CrLf)
        {
            lineBegin = lf + unit;
            failed = lineBegin < minSeek;
            previousLineEnd = lineBegin - 2 * unit;
        }
        else
        {
            lineBegin = pos + unit;
            failed = lineBegin < minSeek;
            previousLineEnd = lineBegin - unit;
            if (!failed && kind == Lf && eol.Crlf && lineBegin - 2 * unit >= textStart)
            {
                std::uint32_t before = 0;
                if (!ReadUnit(lineBegin - 2 * unit, before))
                    return TextStatus::IoError;
                if (before == Cr)
                    previousLineEnd -= unit;
            }
        }
    }
    if (failed)
    {
        result.Found = false;
        result.LineBegin = lineBegin;
        result.PreviousLineEnd = -1;
        return TextStatus::Ok;
    }

    if (lineBegin > textStart && query.WantFirstLineEndOff)
        result.FirstLineEndOff = previousLineEnd;

    if (query.AllowWrap && m_config.WrapColumns > 0)
    {
        // The rows of this line up to 'seek': rowStarts[0] is the line start, each later entry the
        // first character of a wrapped row. 'endsFullAtSeek' says the last row fills up exactly
        // at 'seek', which is then a row boundary too.
        const std::int64_t columns = m_config.WrapColumns;
        std::vector<std::int64_t> rowStarts(1, lineBegin);
        std::int64_t column = 0;
        std::int64_t lastEnd = lineBegin;
        for (std::int64_t at = lineBegin; at < seek && at < size;)
        {
            ScalarStep step;
            if (!ReadScalar(at, size, true, step))
                return TextStatus::IoError;
            if (step.NeedMore || step.Length == 0)
                break;
            if (column >= columns)
            {
                rowStarts.push_back(at);
                column = 0;
            }
            column += step.Scalar == Tab ? TabWidth(column, true) : 1;
            at += step.Length;
            lastEnd = at;
        }
        std::size_t lastRow = rowStarts.size() - 1;
        bool endsFullAtSeek = column >= columns && lastEnd == seek && seek > lineBegin;

        auto rowStart = [&](std::size_t row) { return row <= lastRow ? rowStarts[row] : seek; };
        auto rowEnd = [&](std::size_t row) -> std::int64_t
        {
            if (row < lastRow)
                return rowStarts[row + 1];
            if (row == lastRow && endsFullAtSeek)
                return seek;
            return NoBoundary;
        };

        bool wantCharLen = query.WantFirstLineCharLen;
        bool wantEndOff = query.WantFirstLineEndOff;
        std::size_t row = 0;
        while (true)
        {
            const std::int64_t segmentBegin = rowStart(row);
            const std::int64_t segmentEnd = rowEnd(row);
            const bool here = takeLineBegin ? segmentEnd > seek : segmentEnd >= seek;
            if (!here)
            {
                row++;
                continue;
            }
            if (takeLineBegin && addLineIfSeekIsWrap && row > 0 && segmentBegin == seek)
            {
                // 'seek' is a wrap point, counted as the end of the row before it.
                if (lines != nullptr)
                    (*lines)++;
                addLineIfSeekIsWrap = false;
            }
            if (!takeLineBegin && wantCharLen)
            {
                std::int64_t length = 0;
                if (!ColumnsBetween(segmentBegin, seek, true, length))
                    return TextStatus::IoError;
                result.FirstLineCharLen = length;
                wantCharLen = false;
            }
            if (wantEndOff)
            {
                if (row > 0)
                    result.FirstLineEndOff = segmentBegin; // a wrapped row ends where the next begins
                wantEndOff = false;
            }
            if (row > 0)
            {
                if (lines != nullptr && *lines > 0)
                {
                    // More rows are wanted: take them from this line while it is at hand.
                    (*lines)--;
                    takeLineBegin = false;
                    if (row <= lastRow)
                    {
                        seek = segmentBegin;
                        lastRow = row - 1;
                        endsFullAtSeek = true;
                    }
                    row = 0;
                    continue;
                }
                previousLineEnd = segmentBegin;
            }
            lineBegin = segmentBegin;
            break;
        }
    }
    else if (!takeLineBegin && query.WantFirstLineCharLen)
    {
        std::int64_t length = 0;
        if (seek > lineBegin && !ColumnsBetween(lineBegin, seek, false, length))
            return TextStatus::IoError;
        result.FirstLineCharLen = length;
    }

    result.Found = true;
    result.LineBegin = lineBegin;
    result.PreviousLineEnd = previousLineEnd;
    return TextStatus::Ok;
}

TextStatus TextEngine::FindSeekBefore(std::int64_t seek, int lines, std::int64_t& result,
                                      std::int64_t* firstLineEndOff, std::int64_t* firstLineCharLen,
                                      bool addLineIfSeekIsWrap)
{
    if (firstLineEndOff != nullptr)
        *firstLineEndOff = -1;
    if (firstLineCharLen != nullptr)
        *firstLineCharLen = -1;
    const std::int64_t minSeek = m_config.TextStart;
    std::int64_t begin = seek;
    if (seek < minSeek)
        seek = minSeek;
    ChunkedByteSource::Scope scope(m_source);
    // The first position is a character (at a wrap it starts the later row); every later one is
    // the end of a row (at a wrap it ends the earlier row).
    bool first = true;
    while (lines--)
    {
        PreviousLineQuery query;
        query.Seek = seek;
        query.MinSeek = minSeek;
        query.AllowWrap = true;
        query.TakeLineBegin = first;
        query.WantFirstLineEndOff = first && firstLineEndOff != nullptr;
        query.WantFirstLineCharLen = firstLineCharLen != nullptr && *firstLineCharLen == -1;
        query.AddLineIfSeekIsWrap = first && addLineIfSeekIsWrap;
        PreviousLineResult found;
        const TextStatus status = FindPreviousLine(query, found, &lines);
        if (status != TextStatus::Ok)
            return status;
        if (query.WantFirstLineEndOff)
            *firstLineEndOff = found.FirstLineEndOff;
        if (query.WantFirstLineCharLen)
            *firstLineCharLen = found.FirstLineCharLen;
        begin = found.LineBegin;
        if (!found.Found)
            break;
        seek = found.PreviousLineEnd;
        first = false;
    }
    result = begin;
    return TextStatus::Ok;
}

TextStatus TextEngine::MaxSeekY(int fullRows, std::int64_t& result)
{
    return FindSeekBefore(m_config.FileSize, std::max(fullRows, 1), result);
}

TextStatus TextEngine::ZeroLineSize(std::int64_t seekY, std::int64_t& size, std::int64_t* firstLineEndOff,
                                    std::int64_t* firstLineCharLen)
{
    std::int64_t offset = 0;
    const TextStatus status = FindSeekBefore(seekY, 2, offset, firstLineEndOff, firstLineCharLen);
    size = status == TextStatus::Ok ? seekY - offset : 0;
    return status;
}

TextStatus TextEngine::FindBegin(std::int64_t seek, std::int64_t& result)
{
    if (seek < m_config.TextStart)
    {
        result = m_config.TextStart;
        return TextStatus::Ok;
    }
    return FindSeekBefore(seek, 1, result);
}

} // namespace Sally::Viewer
