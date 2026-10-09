// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Reads and extracts a ZIP archive that comes from elsewhere, so it trusts nothing in it.
// Classic single-disk archives with stored or deflated entries only. Opening an archive
// checks every entry before anything is extracted:
// - names are relative paths that stay inside the destination: no drive, root, "..",
//   alternate data stream, device name or trailing dot or space, and no two names that
//   Windows would see as the same file;
// - no encryption, no ZIP64, no symbolic links, no overlapping entries;
// - entry count and sizes stay within the given limits.
// Extraction checks each entry's size and CRC-32 as it goes.

#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Sally::Zip
{

enum class ZipError
{
    None,
    Io,
    NotZip,
    Unsupported,    // ZIP64, multi-disk, a compression method other than stored or deflate
    Encrypted,
    Corrupt,        // inconsistent headers, overlapping entries, bad deflate data
    UnsafeName,
    DuplicateName,
    Link,
    TooManyEntries,
    TooLarge,
    CrcMismatch,
    SizeMismatch,
};

struct ZipLimits
{
    uint32_t MaxEntries = 20000;
    uint64_t MaxEntrySize = 512ull * 1024 * 1024;
    uint64_t MaxTotalSize = 2048ull * 1024 * 1024;
};

struct ZipEntry
{
    std::wstring Name; // relative, '\' separated, no trailing separator
    bool IsDirectory = false;
    uint16_t Method = 0;
    uint16_t Flags = 0;
    uint32_t Crc32 = 0;
    uint64_t CompressedSize = 0;
    uint64_t Size = 0;
    uint64_t LocalHeaderOffset = 0;
    uint64_t DataOffset = 0;
};

// Checks one raw archive name (either separator) and returns it '\' separated.
// A name ending in a separator is a folder. On refusal, reason says why, in English.
bool NormalizeZipEntryName(const std::wstring& raw, std::wstring& normalized, bool& isDirectory,
                           std::wstring& reason);

class ZipReader
{
public:
    ZipReader() = default;
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ~ZipReader();

    bool Open(const std::wstring& archivePath, const ZipLimits& limits = ZipLimits());
    void Close();

    const std::vector<ZipEntry>& Entries() const { return m_entries; }
    const ZipEntry* Find(const std::wstring& name) const; // case-insensitive

    bool ExtractToMemory(const ZipEntry& entry, std::string& data);
    // Creates the file; an existing file is an error.
    bool ExtractToFile(const ZipEntry& entry, const std::wstring& destinationPath);
    // Extracts every entry under destinationRoot, which must exist.
    bool ExtractAll(const std::wstring& destinationRoot);

    ZipError Error() const { return m_error; }
    const std::wstring& ErrorText() const { return m_errorText; }
    DWORD ErrorCode() const { return m_errorCode; }

private:
    class Sink;
    bool Fail(ZipError error, const std::wstring& text, DWORD code = 0);
    bool ReadAt(uint64_t offset, void* buffer, size_t size);
    bool ReadEntryData(const ZipEntry& entry, Sink& sink);

    HANDLE m_file = INVALID_HANDLE_VALUE;
    uint64_t m_fileSize = 0;
    std::vector<ZipEntry> m_entries;
    ZipError m_error = ZipError::None;
    std::wstring m_errorText;
    DWORD m_errorCode = 0;
};

} // namespace Sally::Zip
