// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The parts of the ZIP file format (PKWARE APPNOTE) that Sally reads and writes:
// single-disk archives without ZIP64, stored or deflated entries, UTF-8 names.

#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Sally::Zip
{

constexpr uint32_t kLocalHeaderSignature = 0x04034b50;
constexpr uint32_t kCentralHeaderSignature = 0x02014b50;
constexpr uint32_t kEndOfCentralDirectorySignature = 0x06054b50;
constexpr uint32_t kZip64EndLocatorSignature = 0x07064b50;

constexpr size_t kLocalHeaderSize = 30;
constexpr size_t kCentralHeaderSize = 46;
constexpr size_t kEndOfCentralDirectorySize = 22;
constexpr size_t kZip64EndLocatorSize = 20;
constexpr size_t kMaxArchiveCommentSize = 0xFFFF;

constexpr uint16_t kMethodStored = 0;
constexpr uint16_t kMethodDeflated = 8;

constexpr uint16_t kFlagEncrypted = 0x0001;
constexpr uint16_t kFlagDataDescriptor = 0x0008;
constexpr uint16_t kFlagStrongEncryption = 0x0040;
constexpr uint16_t kFlagUtf8Name = 0x0800;

constexpr uint16_t kVersionNeededDeflate = 20;
constexpr uint8_t kHostMsDos = 0;
constexpr uint8_t kHostUnix = 3;

constexpr uint16_t kExtraZip64 = 0x0001;

// Sizes and offsets above this need ZIP64, which Sally neither writes nor reads.
constexpr uint64_t kMaxClassicSize = 0xFFFFFFFEull;

inline uint16_t ReadLe16(const unsigned char* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t ReadLe32(const unsigned char* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline void AppendLe16(std::string& out, uint16_t value)
{
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

inline void AppendLe32(std::string& out, uint32_t value)
{
    AppendLe16(out, static_cast<uint16_t>(value & 0xFFFF));
    AppendLe16(out, static_cast<uint16_t>((value >> 16) & 0xFFFF));
}

inline void StoreLe32(unsigned char* p, uint32_t value)
{
    p[0] = static_cast<unsigned char>(value & 0xFF);
    p[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    p[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    p[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
}

// MS-DOS date and time in local time, as ZIP stores them; 1980-01-01 00:00 when the
// time cannot be represented.
inline void FileTimeToZipDosTime(const FILETIME& utc, uint16_t& dosDate, uint16_t& dosTime)
{
    FILETIME local;
    WORD date = 0;
    WORD time = 0;
    if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToDosDateTime(&local, &date, &time))
    {
        dosDate = date;
        dosTime = time;
        return;
    }
    dosDate = static_cast<uint16_t>((0 << 9) | (1 << 5) | 1);
    dosTime = 0;
}

} // namespace Sally::Zip
