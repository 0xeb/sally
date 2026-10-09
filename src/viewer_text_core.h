// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The internal viewer's decoded-text layout, apart from the window that shows it.
//
// Decoded text is UTF-8 or UTF-16 shown as Unicode characters (BOM-marked files and files the
// encoding probe recognises). Everything that turns its bytes into displayed rows lives here:
// reading the file in chunks, decoding scalars, finding line ends, wrapping rows, expanding tabs,
// and walking back from a position to the rows before it (viewer_text_seek.cpp). The viewer
// window supplies a configuration and a reader and asks; nothing here touches a window.
//
// Every operation costs in proportion to the lines it looks at, never to the size of the file:
// opening, scrolling to the end and paging read only the screen they produce.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/unicode/ViewerBomText.h"

namespace Sally::Viewer
{

using Sally::Unicode::BomEncoding;
using Sally::Unicode::DecodedRun;

struct EolPolicy
{
    bool Crlf = true; // CR followed by LF ends a line
    bool Cr = true;   // CR alone ends a line
    bool Lf = true;   // LF alone ends a line
    bool Nul = true;  // NUL ends a line
};

struct TextLayoutConfig
{
    BomEncoding Encoding = BomEncoding::Utf8;
    std::int64_t TextStart = 0; // first byte after the BOM
    std::int64_t FileSize = 0;
    EolPolicy Eol;
    int TabSize = 8;
    int WrapColumns = 0;            // 0 = rows are not wrapped
    std::int64_t LongLineBytes = 0; // 0 = no limit; else a longer line is reported as LongLine
};

// How much work the operations did. Callers that care (a progress estimate, a budget check)
// pass one in; the counts only ever grow.
struct TextWork
{
    std::uint64_t ChunkReads = 0;
    std::uint64_t BytesRead = 0;
    std::uint64_t ReaderOpens = 0;
    std::uint64_t ScalarsDecoded = 0;
    std::uint64_t BytesScannedBack = 0;
};

enum class TextStatus
{
    Ok,
    IoError,  // the file could not be read, or changed size
    LongLine, // a line ran past TextLayoutConfig::LongLineBytes
};

// Random access to the viewed bytes. Open is called before a burst of reads and Close after it;
// a reader may keep nothing open in between.
class IRandomAccessReader
{
public:
    virtual ~IRandomAccessReader() = default;
    virtual bool Open() = 0;
    virtual void Close() = 0;
    // Reads up to 'size' bytes at 'offset'; 'read' is less than 'size' only at the end of data.
    virtual bool ReadAt(std::int64_t offset, void* buffer, std::size_t size, std::size_t& read) = 0;
};

// Bytes held in memory.
class MemoryReader : public IRandomAccessReader
{
public:
    MemoryReader(const std::uint8_t* data, std::size_t size) : m_data(data), m_size(size) {}
    bool Open() override { return true; }
    void Close() override {}
    bool ReadAt(std::int64_t offset, void* buffer, std::size_t size, std::size_t& read) override;

private:
    const std::uint8_t* m_data;
    std::size_t m_size;
};

// A small cache of fixed-size, aligned chunks in front of a reader. The reader is opened on the
// first read and closed again when no Scope is active, so a burst of reads inside one Scope costs
// one open.
class ChunkedByteSource
{
public:
    explicit ChunkedByteSource(IRandomAccessReader& reader, std::size_t chunkSize = 64 * 1024);
    ChunkedByteSource(const ChunkedByteSource&) = delete;
    ChunkedByteSource& operator=(const ChunkedByteSource&) = delete;
    ~ChunkedByteSource();

    // Forgets every cached chunk; 'size' is the size the data must have.
    void Reset(std::int64_t size);
    std::int64_t Size() const { return m_size; }
    void SetWork(TextWork* work) { m_work = work; }

    // The cached bytes from 'offset' to the end of its chunk. False at or past the end of the
    // data, or on a read error (Failed() tells which).
    bool View(std::int64_t offset, const std::uint8_t*& data, std::size_t& available);
    // The cached bytes of the chunk holding 'end - 1', from the chunk start up to 'end'.
    bool ViewBefore(std::int64_t end, const std::uint8_t*& data, std::size_t& available);
    // Copies up to 'size' bytes starting at 'offset' across chunks; returns how many.
    std::size_t Copy(std::int64_t offset, std::uint8_t* out, std::size_t size);
    bool Failed() const { return m_failed; }

    // Keeps the reader open for its lifetime; scopes nest.
    class Scope
    {
    public:
        explicit Scope(ChunkedByteSource& source);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        ChunkedByteSource& m_source;
    };

private:
    struct Chunk
    {
        std::int64_t Start = -1;
        std::vector<std::uint8_t> Bytes;
        std::uint64_t LastUse = 0;
    };
    Chunk* Load(std::int64_t offset);
    bool EnsureOpen();
    void CloseIfIdle();

    IRandomAccessReader& m_reader;
    std::size_t m_chunkSize;
    std::int64_t m_size = 0;
    static const int ChunkCount = 4;
    Chunk m_chunks[ChunkCount];
    std::uint64_t m_clock = 0;
    int m_scopes = 0;
    bool m_open = false;
    bool m_failed = false;
    TextWork* m_work = nullptr;
};

// Receives the cells of a laid-out row: one call per displayed cell, a tab giving several.
class ICellSink
{
public:
    virtual ~ICellSink() = default;
    virtual void OnCell(std::uint32_t scalar, std::int64_t rawStart, std::int64_t rawEnd) = 0;
};

// Appends the cells to a DecodedRun, the form the viewer paints from.
class DecodedRunSink : public ICellSink
{
public:
    explicit DecodedRunSink(DecodedRun& run) : m_run(run) {}
    void OnCell(std::uint32_t scalar, std::int64_t rawStart, std::int64_t rawEnd) override
    {
        m_run.AppendCell(scalar, rawStart, rawEnd);
    }

private:
    DecodedRun& m_run;
};

struct RowLayout
{
    std::int64_t LineEnd = 0;       // just past the row's last displayed byte (the EOL start)
    std::int64_t NextLineBegin = 0; // where the following row starts
    std::int64_t Cells = 0;
    int EolBytes = 0;
    bool Eol = false;     // the row ends at an end of line
    bool Wrapped = false; // the row was wrapped; the line continues on the next row
    bool TooLong = false; // the row stopped at TextLayoutConfig::LongLineBytes
};

// The question FindPreviousLine answers, with the viewer's long-standing meaning of each part.
struct PreviousLineQuery
{
    std::int64_t Seek = 0;    // the reference position
    std::int64_t MinSeek = 0; // a row may not start before this
    bool AllowWrap = true;
    // True: 'Seek' is a character inside a row (at a row boundary it belongs to the later row).
    // False: 'Seek' is the end of a row (at a row boundary it belongs to the earlier row).
    bool TakeLineBegin = true;
    bool WantFirstLineEndOff = false;  // report where the row before the found one ends
    bool WantFirstLineCharLen = false; // report the found row's length up to 'Seek' (only if !TakeLineBegin)
    bool AddLineIfSeekIsWrap = false;  // a 'Seek' exactly at a wrap counts as the end of the row before it
};

struct PreviousLineResult
{
    bool Found = false;
    std::int64_t LineBegin = 0;        // the start of the found row
    std::int64_t PreviousLineEnd = -1; // where the row before it ends (its EOL start, or the wrap point)
    std::int64_t FirstLineEndOff = -1;
    std::int64_t FirstLineCharLen = -1;
};

class TextEngine
{
public:
    TextEngine(ChunkedByteSource& source, const TextLayoutConfig& config, TextWork* work = nullptr);

    const TextLayoutConfig& Config() const { return m_config; }

    // Lays out the row starting at 'rowBegin' (a row start), handing its cells to 'sink' when
    // one is given. LongLine when a row of an unwrapped layout runs past LongLineBytes.
    TextStatus LayoutRow(std::int64_t rowBegin, ICellSink* sink, RowLayout& row);

    // The end of the line containing 'seek', looking no further than 'maxSeek'. 'found' is false
    // when no line end lies within 'maxSeek' before the end of the data.
    TextStatus FindLineEnd(std::int64_t seek, std::int64_t maxSeek, std::int64_t& lineEnd,
                           std::int64_t& nextLineBegin, bool& found);

    // Decodes the scalars wholly inside [start, end). With 'flush' a sequence cut by 'end'
    // decodes as U+FFFD; without it, it is left out.
    TextStatus DecodeRange(std::int64_t start, std::int64_t end, bool flush, DecodedRun& run);

    // viewer_text_seek.cpp: walking back to earlier rows.
    TextStatus FindPreviousLine(const PreviousLineQuery& query, PreviousLineResult& result, int* lines);
    TextStatus FindSeekBefore(std::int64_t seek, int lines, std::int64_t& result,
                              std::int64_t* firstLineEndOff = nullptr, std::int64_t* firstLineCharLen = nullptr,
                              bool addLineIfSeekIsWrap = false);
    // The highest first row that still fills a window of 'fullRows' rows.
    TextStatus MaxSeekY(int fullRows, std::int64_t& result);
    // How far the first row moves when the window scrolls up by one row.
    TextStatus ZeroLineSize(std::int64_t seekY, std::int64_t& size, std::int64_t* firstLineEndOff = nullptr,
                            std::int64_t* firstLineCharLen = nullptr);
    // The start of the row containing 'seek'.
    TextStatus FindBegin(std::int64_t seek, std::int64_t& result);

private:
    // The scalar at 'pos'; 'limit' is where the data ends for this decode ('atLimit' then
    // decides how a sequence cut there decodes). False on a read error.
    bool ReadScalar(std::int64_t pos, std::int64_t limit, bool atLimit, Sally::Unicode::ScalarStep& step);
    // The code unit at 'pos' (a byte for UTF-8, a 16-bit unit for UTF-16).
    bool ReadUnit(std::int64_t pos, std::uint32_t& unit);
    // What ends a line at 'pos' given the scalar there: 0 when nothing does, else the EOL length.
    bool EolAt(std::int64_t pos, const Sally::Unicode::ScalarStep& step, int& eolBytes);
    int UnitSize() const;
    std::int64_t Align(std::int64_t offset) const;
    int TabWidth(std::int64_t column, bool clampToRow) const;
    // Display columns from 'from' (a row start) to 'to', with tabs clamped to the row when wrapping.
    bool ColumnsBetween(std::int64_t from, std::int64_t to, bool wrapping, std::int64_t& columns);

    ChunkedByteSource& m_source;
    TextLayoutConfig m_config;
    TextWork* m_work;
};

// The decoding the viewer uses for a file whose first bytes are 'head': a BOM, else BOM-less
// UTF-16 or UTF-8 recognised from the bytes, else the legacy code page (LegacyBytes).
Sally::Unicode::BomInfo ProbeTextEncoding(const std::uint8_t* head, std::size_t size);

} // namespace Sally::Viewer
