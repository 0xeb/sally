// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "viewer_text_core.h"

#include <algorithm>
#include <cstring>

#include "common/text/EncodingDetector.h"

namespace Sally::Viewer
{

using Sally::Unicode::ScalarStep;

namespace
{

constexpr std::uint32_t Cr = 0x0D;
constexpr std::uint32_t Lf = 0x0A;
constexpr std::uint32_t Tab = 0x09;
constexpr std::uint32_t Space = 0x20;

} // namespace

// ---------------------------------------------------------------------------------------------
// MemoryReader

bool MemoryReader::ReadAt(std::int64_t offset, void* buffer, std::size_t size, std::size_t& read)
{
    read = 0;
    if (offset < 0)
        return false;
    if ((std::uint64_t)offset >= m_size)
        return true;
    read = std::min(size, m_size - (std::size_t)offset);
    std::memcpy(buffer, m_data + offset, read);
    return true;
}

// ---------------------------------------------------------------------------------------------
// ChunkedByteSource

ChunkedByteSource::ChunkedByteSource(IRandomAccessReader& reader, std::size_t chunkSize)
    : m_reader(reader), m_chunkSize(chunkSize < 16 ? 16 : chunkSize)
{
}

ChunkedByteSource::~ChunkedByteSource()
{
    if (m_open)
        m_reader.Close();
}

void ChunkedByteSource::Reset(std::int64_t size)
{
    for (Chunk& chunk : m_chunks)
    {
        chunk.Start = -1;
        chunk.Bytes.clear();
        chunk.LastUse = 0;
    }
    m_size = size < 0 ? 0 : size;
    m_failed = false;
}

bool ChunkedByteSource::EnsureOpen()
{
    if (m_open)
        return true;
    if (!m_reader.Open())
        return false;
    m_open = true;
    if (m_work != nullptr)
        m_work->ReaderOpens++;
    return true;
}

void ChunkedByteSource::CloseIfIdle()
{
    if (m_scopes == 0 && m_open)
    {
        m_reader.Close();
        m_open = false;
    }
}

ChunkedByteSource::Chunk* ChunkedByteSource::Load(std::int64_t offset)
{
    const std::int64_t start = offset - offset % (std::int64_t)m_chunkSize;
    Chunk* victim = &m_chunks[0];
    for (Chunk& chunk : m_chunks)
    {
        if (chunk.Start == start)
        {
            chunk.LastUse = ++m_clock;
            return &chunk;
        }
        if (chunk.LastUse < victim->LastUse)
            victim = &chunk;
    }

    const std::size_t want = (std::size_t)std::min<std::int64_t>((std::int64_t)m_chunkSize, m_size - start);
    victim->Start = -1;
    victim->Bytes.resize(want);
    std::size_t read = 0;
    const bool ok = EnsureOpen() && m_reader.ReadAt(start, victim->Bytes.data(), want, read);
    CloseIfIdle();
    if (m_work != nullptr)
    {
        m_work->ChunkReads++;
        m_work->BytesRead += read;
    }
    if (!ok || read != want) // a read error, or the file is shorter than it was
    {
        m_failed = true;
        return nullptr;
    }
    victim->Start = start;
    victim->LastUse = ++m_clock;
    return victim;
}

bool ChunkedByteSource::View(std::int64_t offset, const std::uint8_t*& data, std::size_t& available)
{
    data = nullptr;
    available = 0;
    if (offset < 0 || offset >= m_size)
        return false;
    Chunk* chunk = Load(offset);
    if (chunk == nullptr)
        return false;
    const std::size_t inChunk = (std::size_t)(offset - chunk->Start);
    data = chunk->Bytes.data() + inChunk;
    available = chunk->Bytes.size() - inChunk;
    return available > 0;
}

bool ChunkedByteSource::ViewBefore(std::int64_t end, const std::uint8_t*& data, std::size_t& available)
{
    data = nullptr;
    available = 0;
    if (end <= 0 || end > m_size)
        return false;
    Chunk* chunk = Load(end - 1);
    if (chunk == nullptr)
        return false;
    data = chunk->Bytes.data();
    available = (std::size_t)(end - chunk->Start);
    return true;
}

std::size_t ChunkedByteSource::Copy(std::int64_t offset, std::uint8_t* out, std::size_t size)
{
    std::size_t copied = 0;
    while (copied < size)
    {
        const std::uint8_t* data = nullptr;
        std::size_t available = 0;
        if (!View(offset + (std::int64_t)copied, data, available))
            break;
        const std::size_t take = std::min(available, size - copied);
        std::memcpy(out + copied, data, take);
        copied += take;
    }
    return copied;
}

ChunkedByteSource::Scope::Scope(ChunkedByteSource& source) : m_source(source)
{
    m_source.m_scopes++;
}

ChunkedByteSource::Scope::~Scope()
{
    m_source.m_scopes--;
    m_source.CloseIfIdle();
}

// ---------------------------------------------------------------------------------------------
// TextEngine: reading

TextEngine::TextEngine(ChunkedByteSource& source, const TextLayoutConfig& config, TextWork* work)
    : m_source(source), m_config(config), m_work(work)
{
    if (m_config.TabSize < 1)
        m_config.TabSize = 1;
    if (m_config.WrapColumns < 0)
        m_config.WrapColumns = 0;
    if (m_config.TextStart < 0)
        m_config.TextStart = 0;
}

int TextEngine::UnitSize() const
{
    return (m_config.Encoding == BomEncoding::Utf16Le || m_config.Encoding == BomEncoding::Utf16Be) ? 2 : 1;
}

std::int64_t TextEngine::Align(std::int64_t offset) const
{
    return Sally::Unicode::AlignToCodeUnit(m_config.Encoding, offset, m_config.TextStart);
}

bool TextEngine::ReadScalar(std::int64_t pos, std::int64_t limit, bool atLimit, ScalarStep& step)
{
    step = ScalarStep();
    const std::uint8_t* data = nullptr;
    std::size_t available = 0;
    if (!m_source.View(pos, data, available))
        return false;
    std::size_t size = (std::size_t)std::min<std::int64_t>((std::int64_t)available, limit - pos);
    step = Sally::Unicode::NextScalar(m_config.Encoding, data, size, atLimit && pos + (std::int64_t)size >= limit);
    if (step.NeedMore && pos + (std::int64_t)size < limit)
    {
        // The sequence continues into the next chunk.
        std::uint8_t joined[4] = {};
        size = m_source.Copy(pos, joined, (std::size_t)std::min<std::int64_t>(4, limit - pos));
        if (m_source.Failed())
            return false;
        step = Sally::Unicode::NextScalar(m_config.Encoding, joined, size, atLimit && pos + (std::int64_t)size >= limit);
    }
    if (m_work != nullptr && !step.NeedMore)
        m_work->ScalarsDecoded++;
    return true;
}

bool TextEngine::ReadUnit(std::int64_t pos, std::uint32_t& unit)
{
    unit = 0;
    const int unitSize = UnitSize();
    const std::uint8_t* data = nullptr;
    std::size_t available = 0;
    std::uint8_t joined[2] = {};
    if (!m_source.View(pos, data, available))
        return false;
    if ((int)available < unitSize)
    {
        if (m_source.Copy(pos, joined, (std::size_t)unitSize) != (std::size_t)unitSize)
            return false;
        data = joined;
    }
    if (m_config.Encoding == BomEncoding::Utf16Le)
        unit = (std::uint32_t)data[0] | ((std::uint32_t)data[1] << 8);
    else if (m_config.Encoding == BomEncoding::Utf16Be)
        unit = ((std::uint32_t)data[0] << 8) | (std::uint32_t)data[1];
    else
        unit = data[0];
    return true;
}

bool TextEngine::EolAt(std::int64_t pos, const ScalarStep& step, int& eolBytes)
{
    eolBytes = 0;
    const EolPolicy& eol = m_config.Eol;
    if (step.Scalar == Cr)
    {
        if (eol.Crlf)
        {
            const std::int64_t next = pos + step.Length;
            if (next < m_config.FileSize)
            {
                std::uint32_t unit = 0;
                if (!ReadUnit(next, unit))
                    return false;
                if (unit == Lf)
                {
                    eolBytes = step.Length + UnitSize();
                    return true;
                }
            }
        }
        if (eol.Cr)
            eolBytes = step.Length;
    }
    else if (step.Scalar == Lf)
    {
        if (eol.Lf)
            eolBytes = step.Length;
    }
    else if (step.Scalar == 0)
    {
        if (eol.Nul)
            eolBytes = step.Length;
    }
    return true;
}

int TextEngine::TabWidth(std::int64_t column, bool clampToRow) const
{
    std::int64_t width = m_config.TabSize - column % m_config.TabSize;
    if (clampToRow && m_config.WrapColumns > 0)
        width = std::min<std::int64_t>(width, m_config.WrapColumns - column);
    return width < 1 ? 1 : (int)width;
}

bool TextEngine::ColumnsBetween(std::int64_t from, std::int64_t to, bool wrapping, std::int64_t& columns)
{
    columns = 0;
    std::int64_t pos = from;
    while (pos < to && pos < m_config.FileSize)
    {
        ScalarStep step;
        if (!ReadScalar(pos, m_config.FileSize, true, step))
            return false;
        if (step.NeedMore || step.Length == 0)
            break;
        columns += step.Scalar == Tab ? TabWidth(columns, wrapping) : 1;
        pos += step.Length;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// TextEngine: forward

TextStatus TextEngine::LayoutRow(std::int64_t rowBegin, ICellSink* sink, RowLayout& row)
{
    row = RowLayout();
    const std::int64_t size = m_config.FileSize;
    const std::int64_t start = Align(std::max(rowBegin, m_config.TextStart));
    row.LineEnd = row.NextLineBegin = start;
    if (start >= size)
        return TextStatus::Ok;

    ChunkedByteSource::Scope scope(m_source);
    const bool wrapping = m_config.WrapColumns > 0;
    std::int64_t column = 0;
    std::int64_t pos = start;
    while (pos < size)
    {
        ScalarStep step;
        if (!ReadScalar(pos, size, true, step))
            return TextStatus::IoError;
        if (step.NeedMore || step.Length == 0)
            break;

        int eolBytes = 0;
        if (!EolAt(pos, step, eolBytes))
            return TextStatus::IoError;
        if (eolBytes > 0)
        {
            row.LineEnd = pos;
            row.NextLineBegin = pos + eolBytes;
            row.EolBytes = eolBytes;
            row.Eol = true;
            return TextStatus::Ok;
        }
        if (wrapping && column >= m_config.WrapColumns)
        {
            // Another character follows a full row: it starts the next row. A full row followed
            // by an end of line keeps the end of line instead (checked above).
            row.Wrapped = true;
            row.LineEnd = row.NextLineBegin = pos;
            return TextStatus::Ok;
        }
        if (!wrapping && m_config.LongLineBytes > 0 && pos - start >= m_config.LongLineBytes)
        {
            row.TooLong = true;
            row.LineEnd = row.NextLineBegin = pos;
            return TextStatus::LongLine;
        }

        const std::int64_t rawEnd = pos + step.Length;
        if (step.Scalar == Tab)
        {
            const int width = TabWidth(column, wrapping);
            if (sink != nullptr)
                for (int i = 0; i < width; ++i)
                    sink->OnCell(Space, pos, rawEnd);
            column += width;
        }
        else
        {
            if (sink != nullptr)
                sink->OnCell(step.Scalar, pos, rawEnd);
            column++;
        }
        row.Cells = column;
        row.LineEnd = row.NextLineBegin = rawEnd;
        pos = rawEnd;
    }
    row.LineEnd = row.NextLineBegin = std::max(row.LineEnd, pos);
    return TextStatus::Ok;
}

TextStatus TextEngine::FindLineEnd(std::int64_t seek, std::int64_t maxSeek, std::int64_t& lineEnd,
                                   std::int64_t& nextLineBegin, bool& found)
{
    found = false;
    const std::int64_t size = m_config.FileSize;
    std::int64_t pos = Align(std::max(seek, m_config.TextStart));
    maxSeek = std::min(maxSeek, size);
    ChunkedByteSource::Scope scope(m_source);
    while (pos < size && pos <= maxSeek)
    {
        ScalarStep step;
        if (!ReadScalar(pos, size, true, step))
            return TextStatus::IoError;
        if (step.NeedMore || step.Length == 0)
            break;
        int eolBytes = 0;
        if (!EolAt(pos, step, eolBytes))
            return TextStatus::IoError;
        if (eolBytes > 0)
        {
            lineEnd = pos;
            nextLineBegin = pos + eolBytes;
            found = true;
            return TextStatus::Ok;
        }
        pos += step.Length;
    }
    if (maxSeek >= size)
    {
        lineEnd = nextLineBegin = size;
        found = true;
    }
    else
        nextLineBegin = -1;
    return TextStatus::Ok;
}

TextStatus TextEngine::DecodeRange(std::int64_t start, std::int64_t end, bool flush, DecodedRun& run)
{
    run.Clear();
    const std::int64_t first = Align(std::max(start, m_config.TextStart));
    end = std::min(end, m_config.FileSize);
    std::int64_t pos = first;
    ChunkedByteSource::Scope scope(m_source);
    while (pos < end)
    {
        ScalarStep step;
        if (!ReadScalar(pos, end, flush, step))
            return TextStatus::IoError;
        if (step.NeedMore || step.Length == 0)
            break;
        run.AppendCell(step.Scalar, pos, pos + step.Length);
        pos += step.Length;
    }
    run.RawBytesConsumed = pos > first ? (std::size_t)(pos - first) : 0;
    return TextStatus::Ok;
}

// ---------------------------------------------------------------------------------------------

Sally::Unicode::BomInfo ProbeTextEncoding(const std::uint8_t* head, std::size_t size)
{
    sally::text::DetectionOptions options;
    options.scanBudget = size;
    // The head is a window of a larger file and may end in the middle of a character.
    options.windowMayEndMidCharacter = true;
    const sally::text::DetectionResult detected = sally::text::Detect(head, size, options);
    Sally::Unicode::BomInfo info;
    info.Encoding = detected.encoding;
    info.TextOffset = detected.textOffset;
    return info;
}

} // namespace Sally::Viewer
