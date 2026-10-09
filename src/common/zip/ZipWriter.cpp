// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "zip/ZipWriter.h"

#include "Win32TextCodec.h"
#include "zip/ZipFormat.h"
#include "zlib/zlib.h"

#include <memory>
#include <vector>

namespace Sally::Zip
{

namespace
{

constexpr DWORD kChunkSize = 64 * 1024;

std::string EntryNameToUtf8(const std::wstring& entryName, bool& nonAscii)
{
    std::wstring name = entryName;
    for (wchar_t& ch : name)
    {
        if (ch == L'\\')
            ch = L'/';
    }
    nonAscii = false;
    for (wchar_t ch : name)
    {
        if (ch >= 0x80)
            nonAscii = true;
    }
    std::string utf8;
    if (!Win32EncodeText(CP_UTF8, name, utf8))
        return std::string();
    return utf8;
}

class SourceFile
{
public:
    explicit SourceFile(const std::wstring& path)
    {
        m_handle = CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    }
    ~SourceFile()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
    }
    HANDLE Handle() const { return m_handle; }

private:
    HANDLE m_handle;
};

struct DeflateStream
{
    z_stream Stream = {};
    bool Initialized = false;
    ~DeflateStream()
    {
        if (Initialized)
            deflateEnd(&Stream);
    }
};

} // namespace

ZipWriter::~ZipWriter()
{
    if (m_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_file);
        if (!m_finished)
            DeleteFileW(m_path.c_str());
    }
}

bool ZipWriter::Fail(const std::wstring& text, DWORD code)
{
    if (m_errorText.empty())
    {
        m_errorText = text;
        m_errorCode = code;
    }
    return false;
}

bool ZipWriter::WriteBytes(const void* data, size_t size)
{
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    while (size > 0)
    {
        const DWORD part = size > kChunkSize ? kChunkSize : static_cast<DWORD>(size);
        DWORD written = 0;
        if (!WriteFile(m_file, bytes, part, &written, NULL) || written != part)
            return Fail(L"Cannot write the archive " + m_path + L".", GetLastError());
        bytes += part;
        size -= part;
        m_offset += part;
    }
    return true;
}

bool ZipWriter::Create(const std::wstring& archivePath)
{
    if (m_file != INVALID_HANDLE_VALUE)
        return Fail(L"The archive is already open.", ERROR_INVALID_STATE);
    m_path = archivePath;
    m_file = CreateFileW(archivePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_file == INVALID_HANDLE_VALUE)
        return Fail(L"Cannot create the archive " + archivePath + L".", GetLastError());
    return true;
}

bool ZipWriter::AddFile(const std::wstring& sourcePath, const std::wstring& entryName)
{
    if (m_file == INVALID_HANDLE_VALUE || m_finished || !m_errorText.empty())
        return Fail(L"The archive is not open.", ERROR_INVALID_STATE);
    if (m_entryCount >= 0xFFFF)
        return Fail(L"Too many files for one archive.", ERROR_TOO_MANY_NAMES);

    bool nonAscii = false;
    const std::string name = EntryNameToUtf8(entryName, nonAscii);
    if (name.empty() || name.size() > 0xFFFF)
        return Fail(L"Invalid name in the archive: " + entryName + L".", ERROR_INVALID_NAME);

    SourceFile source(sourcePath);
    if (source.Handle() == INVALID_HANDLE_VALUE)
        return Fail(L"Cannot open " + sourcePath + L".", GetLastError());
    LARGE_INTEGER size;
    FILETIME lastWrite;
    if (!GetFileSizeEx(source.Handle(), &size) || !GetFileTime(source.Handle(), NULL, NULL, &lastWrite))
        return Fail(L"Cannot read " + sourcePath + L".", GetLastError());
    if (static_cast<uint64_t>(size.QuadPart) > kMaxClassicSize)
        return Fail(sourcePath + L" is too large for a ZIP archive.", ERROR_FILE_TOO_LARGE);

    const uint64_t headerOffset = m_offset;
    if (headerOffset > kMaxClassicSize)
        return Fail(L"The archive is too large.", ERROR_FILE_TOO_LARGE);

    uint16_t dosDate = 0;
    uint16_t dosTime = 0;
    FileTimeToZipDosTime(lastWrite, dosDate, dosTime);
    const uint16_t flags = nonAscii ? kFlagUtf8Name : 0;

    std::string local;
    AppendLe32(local, kLocalHeaderSignature);
    AppendLe16(local, kVersionNeededDeflate);
    AppendLe16(local, flags);
    AppendLe16(local, kMethodDeflated);
    AppendLe16(local, dosTime);
    AppendLe16(local, dosDate);
    AppendLe32(local, 0); // CRC-32, sizes: patched once the data is written
    AppendLe32(local, 0);
    AppendLe32(local, 0);
    AppendLe16(local, static_cast<uint16_t>(name.size()));
    AppendLe16(local, 0);
    local += name;
    if (!WriteBytes(local.data(), local.size()))
        return false;

    DeflateStream deflater;
    if (deflateInit2(&deflater.Stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        return Fail(L"Cannot start compression.", ERROR_NOT_ENOUGH_MEMORY);
    deflater.Initialized = true;

    std::vector<unsigned char> input(kChunkSize);
    std::vector<unsigned char> output(kChunkSize);
    uLong crc = crc32(0L, Z_NULL, 0);
    uint64_t total = 0;
    uint64_t packed = 0;
    bool atEnd = false;
    while (!atEnd)
    {
        DWORD read = 0;
        if (!ReadFile(source.Handle(), input.data(), kChunkSize, &read, NULL))
            return Fail(L"Cannot read " + sourcePath + L".", GetLastError());
        atEnd = read == 0;
        total += read;
        if (total > static_cast<uint64_t>(size.QuadPart))
            return Fail(sourcePath + L" grew while it was being archived.", ERROR_INVALID_DATA);
        crc = crc32(crc, input.data(), read);
        deflater.Stream.next_in = input.data();
        deflater.Stream.avail_in = read;
        const int flush = atEnd ? Z_FINISH : Z_NO_FLUSH;
        int status = Z_OK;
        do
        {
            deflater.Stream.next_out = output.data();
            deflater.Stream.avail_out = kChunkSize;
            status = deflate(&deflater.Stream, flush);
            if (status == Z_STREAM_ERROR)
                return Fail(L"Compression failed.", ERROR_INVALID_DATA);
            const DWORD produced = kChunkSize - deflater.Stream.avail_out;
            packed += produced;
            if (packed > kMaxClassicSize)
                return Fail(L"The archive is too large.", ERROR_FILE_TOO_LARGE);
            if (produced > 0 && !WriteBytes(output.data(), produced))
                return false;
        } while (deflater.Stream.avail_out == 0 || (atEnd && status != Z_STREAM_END));
    }
    if (total != static_cast<uint64_t>(size.QuadPart))
        return Fail(sourcePath + L" changed while it was being archived.", ERROR_INVALID_DATA);

    unsigned char patch[12];
    StoreLe32(patch, static_cast<uint32_t>(crc));
    StoreLe32(patch + 4, static_cast<uint32_t>(packed));
    StoreLe32(patch + 8, static_cast<uint32_t>(total));
    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(headerOffset + 14);
    DWORD written = 0;
    if (!SetFilePointerEx(m_file, position, NULL, FILE_BEGIN) ||
        !WriteFile(m_file, patch, sizeof(patch), &written, NULL) || written != sizeof(patch))
        return Fail(L"Cannot write the archive " + m_path + L".", GetLastError());
    position.QuadPart = static_cast<LONGLONG>(m_offset);
    if (!SetFilePointerEx(m_file, position, NULL, FILE_BEGIN))
        return Fail(L"Cannot write the archive " + m_path + L".", GetLastError());

    std::string& central = m_centralDirectory;
    AppendLe32(central, kCentralHeaderSignature);
    AppendLe16(central, static_cast<uint16_t>((kHostMsDos << 8) | kVersionNeededDeflate));
    AppendLe16(central, kVersionNeededDeflate);
    AppendLe16(central, flags);
    AppendLe16(central, kMethodDeflated);
    AppendLe16(central, dosTime);
    AppendLe16(central, dosDate);
    AppendLe32(central, static_cast<uint32_t>(crc));
    AppendLe32(central, static_cast<uint32_t>(packed));
    AppendLe32(central, static_cast<uint32_t>(total));
    AppendLe16(central, static_cast<uint16_t>(name.size()));
    AppendLe16(central, 0); // extra field
    AppendLe16(central, 0); // comment
    AppendLe16(central, 0); // disk
    AppendLe16(central, 0); // internal attributes
    AppendLe32(central, FILE_ATTRIBUTE_ARCHIVE);
    AppendLe32(central, static_cast<uint32_t>(headerOffset));
    central += name;
    ++m_entryCount;
    return true;
}

bool ZipWriter::Finish()
{
    if (m_file == INVALID_HANDLE_VALUE || m_finished || !m_errorText.empty())
        return Fail(L"The archive is not open.", ERROR_INVALID_STATE);
    const uint64_t centralOffset = m_offset;
    if (centralOffset + m_centralDirectory.size() > kMaxClassicSize)
        return Fail(L"The archive is too large.", ERROR_FILE_TOO_LARGE);
    if (!WriteBytes(m_centralDirectory.data(), m_centralDirectory.size()))
        return false;

    std::string end;
    AppendLe32(end, kEndOfCentralDirectorySignature);
    AppendLe16(end, 0);
    AppendLe16(end, 0);
    AppendLe16(end, static_cast<uint16_t>(m_entryCount));
    AppendLe16(end, static_cast<uint16_t>(m_entryCount));
    AppendLe32(end, static_cast<uint32_t>(m_centralDirectory.size()));
    AppendLe32(end, static_cast<uint32_t>(centralOffset));
    AppendLe16(end, 0);
    if (!WriteBytes(end.data(), end.size()))
        return false;
    if (!FlushFileBuffers(m_file))
        return Fail(L"Cannot write the archive " + m_path + L".", GetLastError());
    m_finished = true;
    CloseHandle(m_file);
    m_file = INVALID_HANDLE_VALUE;
    return true;
}

} // namespace Sally::Zip
