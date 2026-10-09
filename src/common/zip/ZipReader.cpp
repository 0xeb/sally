// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "zip/ZipReader.h"

#include "ExtendedLengthPath.h"
#include "Win32TextCodec.h"
#include "zip/ZipFormat.h"
#include "zlib/zlib.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Sally::Zip
{

namespace
{

constexpr size_t kChunkSize = 64 * 1024;

bool IsReservedDeviceName(const std::wstring& segment)
{
    std::wstring base = segment.substr(0, segment.find(L'.'));
    while (!base.empty() && base.back() == L' ')
        base.pop_back();
    for (wchar_t& ch : base)
    {
        if (ch >= L'a' && ch <= L'z')
            ch = static_cast<wchar_t>(ch - L'a' + L'A');
    }
    static const wchar_t* const kNames[] = {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"};
    for (const wchar_t* name : kNames)
    {
        if (base == name)
            return true;
    }
    if (base.size() == 4 && (base.compare(0, 3, L"COM") == 0 || base.compare(0, 3, L"LPT") == 0))
    {
        const wchar_t digit = base[3];
        // Windows also reserves COM and LPT with a superscript one, two or three.
        if ((digit >= L'0' && digit <= L'9') || digit == 0x00B9 || digit == 0x00B2 || digit == 0x00B3)
            return true;
    }
    return false;
}

std::wstring UpperKey(const std::wstring& text)
{
    if (text.empty())
        return text;
    std::wstring upper(text.size(), L'\0');
    const int written = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, text.data(),
                                      static_cast<int>(text.size()), &upper[0],
                                      static_cast<int>(upper.size()), NULL, NULL, 0);
    if (written != static_cast<int>(text.size()))
        return text;
    return upper;
}

bool DecodeName(const unsigned char* bytes, size_t size, bool utf8Flag, std::wstring& name)
{
    name.clear();
    if (size == 0)
        return true;
    bool ascii = true;
    for (size_t i = 0; i < size; ++i)
    {
        if (bytes[i] >= 0x80)
            ascii = false;
    }
    if (ascii)
    {
        name.assign(bytes, bytes + size);
        return true;
    }
    // Names without the UTF-8 flag are code page 437 by the format, but many tools write
    // UTF-8 without setting it; valid UTF-8 is taken as UTF-8.
    const char* chars = reinterpret_cast<const char*>(bytes);
    if (Win32DecodeText(CP_UTF8, chars, size, name))
        return true;
    return !utf8Flag && Win32DecodeText(437, chars, size, name);
}

class FileHandle
{
public:
    explicit FileHandle(HANDLE handle) : m_handle(handle) {}
    ~FileHandle()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
    }
    HANDLE Get() const { return m_handle; }
    HANDLE Release()
    {
        HANDLE handle = m_handle;
        m_handle = INVALID_HANDLE_VALUE;
        return handle;
    }

private:
    HANDLE m_handle;
};

struct InflateStream
{
    z_stream Stream = {};
    bool Initialized = false;
    ~InflateStream()
    {
        if (Initialized)
            inflateEnd(&Stream);
    }
};

} // namespace

bool NormalizeZipEntryName(const std::wstring& raw, std::wstring& normalized, bool& isDirectory,
                           std::wstring& reason)
{
    normalized.clear();
    isDirectory = false;
    reason.clear();
    std::wstring name = raw;
    for (wchar_t& ch : name)
    {
        if (ch == L'/')
            ch = L'\\';
    }
    if (!name.empty() && name.back() == L'\\')
    {
        isDirectory = true;
        name.pop_back();
    }
    if (name.empty())
    {
        reason = L"empty name";
        return false;
    }
    if (name.size() > 4096)
    {
        reason = L"name too long";
        return false;
    }
    if (name.front() == L'\\')
    {
        reason = L"absolute path";
        return false;
    }
    for (wchar_t ch : name)
    {
        if (ch < 0x20 || ch == L':' || ch == L'<' || ch == L'>' || ch == L'"' || ch == L'|' ||
            ch == L'?' || ch == L'*')
        {
            reason = ch == L':' ? L"drive or stream name" : L"invalid character";
            return false;
        }
    }
    size_t start = 0;
    while (start <= name.size())
    {
        size_t end = name.find(L'\\', start);
        if (end == std::wstring::npos)
            end = name.size();
        const std::wstring segment = name.substr(start, end - start);
        if (segment.empty())
        {
            reason = L"empty path component";
            return false;
        }
        if (segment == L"." || segment == L"..")
        {
            reason = L"relative path component";
            return false;
        }
        if (segment.back() == L'.' || segment.back() == L' ')
        {
            reason = L"trailing dot or space";
            return false;
        }
        if (segment.size() > 255)
        {
            reason = L"path component too long";
            return false;
        }
        if (IsReservedDeviceName(segment))
        {
            reason = L"device name";
            return false;
        }
        start = end + 1;
    }
    normalized = name;
    return true;
}

class ZipReader::Sink
{
public:
    virtual ~Sink() = default;
    virtual bool Write(const unsigned char* data, size_t size) = 0;
};

ZipReader::~ZipReader()
{
    Close();
}

void ZipReader::Close()
{
    if (m_file != INVALID_HANDLE_VALUE)
        CloseHandle(m_file);
    m_file = INVALID_HANDLE_VALUE;
    m_fileSize = 0;
    m_entries.clear();
}

bool ZipReader::Fail(ZipError error, const std::wstring& text, DWORD code)
{
    if (m_error == ZipError::None)
    {
        m_error = error;
        m_errorText = text;
        m_errorCode = code;
    }
    return false;
}

bool ZipReader::ReadAt(uint64_t offset, void* buffer, size_t size)
{
    if (offset > m_fileSize || size > m_fileSize - offset)
        return Fail(ZipError::Corrupt, L"The archive is truncated.");
    OVERLAPPED overlapped = {};
    overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD read = 0;
    if (!ReadFile(m_file, buffer, static_cast<DWORD>(size), &read, &overlapped) || read != size)
        return Fail(ZipError::Io, L"Cannot read the archive.", GetLastError());
    return true;
}

const ZipEntry* ZipReader::Find(const std::wstring& name) const
{
    const std::wstring key = UpperKey(name);
    for (const ZipEntry& entry : m_entries)
    {
        if (UpperKey(entry.Name) == key)
            return &entry;
    }
    return nullptr;
}

bool ZipReader::Open(const std::wstring& archivePath, const ZipLimits& limits)
{
    Close();
    m_error = ZipError::None;
    m_errorText.clear();
    m_errorCode = 0;

    m_file = CreateFileW(Sally::ToExtendedLengthPath(archivePath).c_str(), GENERIC_READ, FILE_SHARE_READ,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_file == INVALID_HANDLE_VALUE)
        return Fail(ZipError::Io, L"Cannot open " + archivePath + L".", GetLastError());
    LARGE_INTEGER size;
    if (!GetFileSizeEx(m_file, &size))
        return Fail(ZipError::Io, L"Cannot read " + archivePath + L".", GetLastError());
    m_fileSize = static_cast<uint64_t>(size.QuadPart);
    if (m_fileSize < kEndOfCentralDirectorySize)
        return Fail(ZipError::NotZip, L"The file is not a ZIP archive.");

    // The end record is the last one whose comment reaches exactly to the end of the file.
    const uint64_t tailSize =
        std::min<uint64_t>(m_fileSize, kEndOfCentralDirectorySize + kMaxArchiveCommentSize);
    std::vector<unsigned char> tail(static_cast<size_t>(tailSize));
    if (!ReadAt(m_fileSize - tailSize, tail.data(), tail.size()))
        return false;
    size_t endPos = SIZE_MAX;
    for (size_t pos = tail.size() - kEndOfCentralDirectorySize + 1; pos-- > 0;)
    {
        if (ReadLe32(&tail[pos]) == kEndOfCentralDirectorySignature &&
            pos + kEndOfCentralDirectorySize + ReadLe16(&tail[pos + 20]) == tail.size())
        {
            endPos = pos;
            break;
        }
    }
    if (endPos == SIZE_MAX)
        return Fail(ZipError::NotZip, L"The file is not a ZIP archive.");
    const unsigned char* end = &tail[endPos];
    const uint64_t endOffset = m_fileSize - tailSize + endPos;
    const uint16_t diskNumber = ReadLe16(end + 4);
    const uint16_t centralDisk = ReadLe16(end + 6);
    const uint16_t entriesOnDisk = ReadLe16(end + 8);
    const uint16_t entryCount = ReadLe16(end + 10);
    const uint32_t centralSize = ReadLe32(end + 12);
    const uint32_t centralOffset = ReadLe32(end + 16);
    if (entryCount == 0xFFFF || centralSize == 0xFFFFFFFF || centralOffset == 0xFFFFFFFF)
        return Fail(ZipError::Unsupported, L"ZIP64 archives are not supported.");
    if (endOffset >= kZip64EndLocatorSize)
    {
        unsigned char locator[4];
        if (!ReadAt(endOffset - kZip64EndLocatorSize, locator, sizeof(locator)))
            return false;
        if (ReadLe32(locator) == kZip64EndLocatorSignature)
            return Fail(ZipError::Unsupported, L"ZIP64 archives are not supported.");
    }
    if (diskNumber != 0 || centralDisk != 0 || entriesOnDisk != entryCount)
        return Fail(ZipError::Unsupported, L"Multi-volume archives are not supported.");
    if (entryCount > limits.MaxEntries)
        return Fail(ZipError::TooManyEntries, L"The archive has too many entries.");
    if (static_cast<uint64_t>(centralOffset) + centralSize > endOffset)
        return Fail(ZipError::Corrupt, L"The archive's directory is damaged.");

    std::vector<unsigned char> central(centralSize);
    if (centralSize > 0 && !ReadAt(centralOffset, central.data(), central.size()))
        return false;

    std::unordered_map<std::wstring, bool> seen; // upper-case name -> is a folder
    std::vector<std::wstring> parents;
    uint64_t total = 0;
    size_t pos = 0;
    for (uint32_t index = 0; index < entryCount; ++index)
    {
        if (pos + kCentralHeaderSize > central.size() ||
            ReadLe32(&central[pos]) != kCentralHeaderSignature)
            return Fail(ZipError::Corrupt, L"The archive's directory is damaged.");
        const unsigned char* header = &central[pos];
        const uint16_t madeBy = ReadLe16(header + 4);
        ZipEntry entry;
        entry.Flags = ReadLe16(header + 8);
        entry.Method = ReadLe16(header + 10);
        entry.Crc32 = ReadLe32(header + 16);
        entry.CompressedSize = ReadLe32(header + 20);
        entry.Size = ReadLe32(header + 24);
        const uint16_t nameSize = ReadLe16(header + 28);
        const uint16_t extraSize = ReadLe16(header + 30);
        const uint16_t commentSize = ReadLe16(header + 32);
        const uint16_t startDisk = ReadLe16(header + 34);
        const uint32_t externalAttributes = ReadLe32(header + 38);
        entry.LocalHeaderOffset = ReadLe32(header + 42);
        const size_t recordSize = kCentralHeaderSize + nameSize + extraSize + commentSize;
        if (pos + recordSize > central.size())
            return Fail(ZipError::Corrupt, L"The archive's directory is damaged.");
        const unsigned char* rawName = header + kCentralHeaderSize;

        std::wstring rawText;
        if (!DecodeName(rawName, nameSize, (entry.Flags & kFlagUtf8Name) != 0, rawText))
            return Fail(ZipError::UnsafeName, L"An entry name is not valid text.");
        if (startDisk != 0)
            return Fail(ZipError::Unsupported, L"Multi-volume archives are not supported.");
        if (entry.CompressedSize == 0xFFFFFFFF || entry.Size == 0xFFFFFFFF ||
            entry.LocalHeaderOffset == 0xFFFFFFFF)
            return Fail(ZipError::Unsupported, L"ZIP64 archives are not supported.");
        for (size_t extra = 0; extra + 4 <= extraSize;)
        {
            const unsigned char* field = rawName + nameSize + extra;
            if (ReadLe16(field) == kExtraZip64)
                return Fail(ZipError::Unsupported, L"ZIP64 archives are not supported.");
            extra += 4 + ReadLe16(field + 2);
        }
        if ((entry.Flags & (kFlagEncrypted | kFlagStrongEncryption)) != 0)
            return Fail(ZipError::Encrypted, L"Encrypted archives are not supported: " + rawText);
        if (entry.Method != kMethodStored && entry.Method != kMethodDeflated)
            return Fail(ZipError::Unsupported, L"Unsupported compression method: " + rawText);
        if ((madeBy >> 8) == kHostUnix && ((externalAttributes >> 16) & 0xF000) == 0xA000)
            return Fail(ZipError::Link, L"The archive contains a link: " + rawText);
        if ((externalAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && (madeBy >> 8) != kHostUnix)
            return Fail(ZipError::Link, L"The archive contains a link: " + rawText);

        std::wstring reason;
        if (!NormalizeZipEntryName(rawText, entry.Name, entry.IsDirectory, reason))
            return Fail(ZipError::UnsafeName, L"Unsafe name in the archive (" + reason + L"): " + rawText);
        if (entry.IsDirectory && entry.Size != 0)
            return Fail(ZipError::Corrupt, L"A folder entry has data: " + rawText);
        if (entry.Method == kMethodStored && entry.CompressedSize != entry.Size)
            return Fail(ZipError::Corrupt, L"Stored entry sizes disagree: " + rawText);
        if (entry.Size > limits.MaxEntrySize)
            return Fail(ZipError::TooLarge, L"An entry is too large: " + rawText);
        total += entry.Size;
        if (total > limits.MaxTotalSize)
            return Fail(ZipError::TooLarge, L"The archive expands to too much data.");

        const std::wstring key = UpperKey(entry.Name);
        auto found = seen.find(key);
        if (found != seen.end())
        {
            if (!(found->second && entry.IsDirectory))
                return Fail(ZipError::DuplicateName, L"The archive has the same name twice: " + rawText);
        }
        else
        {
            seen.emplace(key, entry.IsDirectory);
        }
        for (size_t sep = key.find(L'\\'); sep != std::wstring::npos; sep = key.find(L'\\', sep + 1))
            parents.push_back(key.substr(0, sep));

        // The local header must agree with the directory on the name.
        unsigned char local[kLocalHeaderSize];
        if (entry.LocalHeaderOffset + kLocalHeaderSize > centralOffset)
            return Fail(ZipError::Corrupt, L"An entry lies outside the archive's data: " + rawText);
        if (!ReadAt(entry.LocalHeaderOffset, local, sizeof(local)))
            return false;
        if (ReadLe32(local) != kLocalHeaderSignature)
            return Fail(ZipError::Corrupt, L"An entry header is damaged: " + rawText);
        const uint16_t localNameSize = ReadLe16(local + 26);
        const uint16_t localExtraSize = ReadLe16(local + 28);
        if (localNameSize != nameSize || ReadLe16(local + 8) != entry.Method)
            return Fail(ZipError::Corrupt, L"An entry header disagrees with the directory: " + rawText);
        std::vector<unsigned char> localName(localNameSize);
        if (localNameSize > 0 && !ReadAt(entry.LocalHeaderOffset + kLocalHeaderSize, localName.data(), localNameSize))
            return false;
        if (!std::equal(localName.begin(), localName.end(), rawName))
            return Fail(ZipError::Corrupt, L"An entry header disagrees with the directory: " + rawText);
        const uint32_t localCompressedSize = ReadLe32(local + 18);
        const uint32_t localSize = ReadLe32(local + 22);
        if (localCompressedSize == 0xFFFFFFFF || localSize == 0xFFFFFFFF)
            return Fail(ZipError::Unsupported, L"ZIP64 archives are not supported.");
        std::vector<unsigned char> localExtra(localExtraSize);
        if (localExtraSize > 0 &&
            !ReadAt(entry.LocalHeaderOffset + kLocalHeaderSize + localNameSize, localExtra.data(), localExtraSize))
            return false;
        for (size_t extra = 0; extra + 4 <= localExtra.size();)
        {
            if (ReadLe16(&localExtra[extra]) == kExtraZip64)
                return Fail(ZipError::Unsupported, L"ZIP64 archives are not supported.");
            extra += 4 + ReadLe16(&localExtra[extra + 2]);
        }
        // Without a data descriptor the local header repeats the checksum and sizes.
        if ((ReadLe16(local + 6) & kFlagDataDescriptor) == 0 &&
            (ReadLe32(local + 14) != entry.Crc32 || localCompressedSize != entry.CompressedSize ||
             localSize != entry.Size))
            return Fail(ZipError::Corrupt, L"An entry header disagrees with the directory: " + rawText);
        entry.DataOffset = entry.LocalHeaderOffset + kLocalHeaderSize + localNameSize + localExtraSize;
        if (entry.DataOffset + entry.CompressedSize > centralOffset)
            return Fail(ZipError::Corrupt, L"An entry lies outside the archive's data: " + rawText);

        m_entries.push_back(entry);
        pos += recordSize;
    }

    // A name used as a file must not also be a folder of another entry.
    for (const std::wstring& parent : parents)
    {
        auto found = seen.find(parent);
        if (found != seen.end() && !found->second)
            return Fail(ZipError::DuplicateName, L"A name is both a file and a folder: " + parent);
    }

    // Entries must not share bytes (a classic way to build archives that expand enormously).
    std::vector<const ZipEntry*> byOffset;
    for (const ZipEntry& entry : m_entries)
        byOffset.push_back(&entry);
    std::sort(byOffset.begin(), byOffset.end(),
              [](const ZipEntry* a, const ZipEntry* b) { return a->LocalHeaderOffset < b->LocalHeaderOffset; });
    for (size_t i = 1; i < byOffset.size(); ++i)
    {
        if (byOffset[i - 1]->DataOffset + byOffset[i - 1]->CompressedSize > byOffset[i]->LocalHeaderOffset)
            return Fail(ZipError::Corrupt, L"Archive entries overlap.");
    }
    return true;
}

bool ZipReader::ReadEntryData(const ZipEntry& entry, Sink& sink)
{
    if (m_file == INVALID_HANDLE_VALUE)
        return Fail(ZipError::Io, L"The archive is not open.");
    std::vector<unsigned char> input(kChunkSize);
    std::vector<unsigned char> output(kChunkSize);
    uLong crc = crc32(0L, Z_NULL, 0);
    uint64_t produced = 0;
    uint64_t remaining = entry.CompressedSize;
    uint64_t offset = entry.DataOffset;

    if (entry.Method == kMethodStored)
    {
        while (remaining > 0)
        {
            const size_t part = static_cast<size_t>(std::min<uint64_t>(remaining, kChunkSize));
            if (!ReadAt(offset, input.data(), part))
                return false;
            crc = crc32(crc, input.data(), static_cast<uInt>(part));
            if (!sink.Write(input.data(), part))
                return false;
            offset += part;
            remaining -= part;
            produced += part;
        }
    }
    else
    {
        InflateStream inflater;
        if (inflateInit2(&inflater.Stream, -MAX_WBITS) != Z_OK)
            return Fail(ZipError::Io, L"Cannot start decompression.", ERROR_NOT_ENOUGH_MEMORY);
        inflater.Initialized = true;
        int status = Z_OK;
        while (status != Z_STREAM_END)
        {
            if (inflater.Stream.avail_in == 0)
            {
                if (remaining == 0)
                    return Fail(ZipError::Corrupt, L"Damaged data in " + entry.Name + L".");
                const size_t part = static_cast<size_t>(std::min<uint64_t>(remaining, kChunkSize));
                if (!ReadAt(offset, input.data(), part))
                    return false;
                offset += part;
                remaining -= part;
                inflater.Stream.next_in = input.data();
                inflater.Stream.avail_in = static_cast<uInt>(part);
            }
            inflater.Stream.next_out = output.data();
            inflater.Stream.avail_out = static_cast<uInt>(output.size());
            status = inflate(&inflater.Stream, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END)
                return Fail(ZipError::Corrupt, L"Damaged data in " + entry.Name + L".");
            const size_t got = output.size() - inflater.Stream.avail_out;
            produced += got;
            if (produced > entry.Size)
                return Fail(ZipError::SizeMismatch, L"Entry larger than recorded: " + entry.Name + L".");
            crc = crc32(crc, output.data(), static_cast<uInt>(got));
            if (got > 0 && !sink.Write(output.data(), got))
                return false;
        }
        if (remaining != 0 || inflater.Stream.avail_in != 0)
            return Fail(ZipError::Corrupt, L"Damaged data in " + entry.Name + L".");
    }
    if (produced != entry.Size)
        return Fail(ZipError::SizeMismatch, L"Entry size does not match: " + entry.Name + L".");
    if (static_cast<uint32_t>(crc) != entry.Crc32)
        return Fail(ZipError::CrcMismatch, L"Checksum error in " + entry.Name + L".");
    return true;
}

bool ZipReader::ExtractToMemory(const ZipEntry& entry, std::string& data)
{
    class MemorySink : public Sink
    {
    public:
        explicit MemorySink(std::string& out) : m_out(out) {}
        bool Write(const unsigned char* bytes, size_t size) override
        {
            m_out.append(reinterpret_cast<const char*>(bytes), size);
            return true;
        }

    private:
        std::string& m_out;
    };
    data.clear();
    if (entry.IsDirectory)
        return true;
    MemorySink sink(data);
    return ReadEntryData(entry, sink);
}

bool ZipReader::ExtractToFile(const ZipEntry& entry, const std::wstring& destinationPath)
{
    class FileSink : public Sink
    {
    public:
        FileSink(ZipReader& owner, HANDLE file, const std::wstring& path)
            : m_owner(owner), m_file(file), m_path(path) {}
        bool Write(const unsigned char* bytes, size_t size) override
        {
            DWORD written = 0;
            if (!WriteFile(m_file, bytes, static_cast<DWORD>(size), &written, NULL) || written != size)
                return m_owner.Fail(ZipError::Io, L"Cannot write " + m_path + L".", GetLastError());
            return true;
        }

    private:
        ZipReader& m_owner;
        HANDLE m_file;
        const std::wstring& m_path;
    };

    FileHandle file(CreateFileW(Sally::ToExtendedLengthPath(destinationPath).c_str(), GENERIC_WRITE, 0, NULL,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL));
    if (file.Get() == INVALID_HANDLE_VALUE)
        return Fail(ZipError::Io, L"Cannot create " + destinationPath + L".", GetLastError());
    FileSink sink(*this, file.Get(), destinationPath);
    bool ok = ReadEntryData(entry, sink);
    if (ok && !FlushFileBuffers(file.Get()))
        ok = Fail(ZipError::Io, L"Cannot write " + destinationPath + L".", GetLastError());
    CloseHandle(file.Release());
    if (!ok)
        DeleteFileW(Sally::ToExtendedLengthPath(destinationPath).c_str());
    return ok;
}

bool ZipReader::ExtractAll(const std::wstring& destinationRoot)
{
    std::wstring root = destinationRoot;
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
        root.pop_back();
    std::unordered_set<std::wstring> created;
    auto ensureFolder = [&](const std::wstring& relative) -> bool {
        size_t sep = 0;
        while (true)
        {
            sep = relative.find(L'\\', sep);
            const std::wstring part = sep == std::wstring::npos ? relative : relative.substr(0, sep);
            const std::wstring key = UpperKey(part);
            if (created.find(key) == created.end())
            {
                const std::wstring path = Sally::ToExtendedLengthPath(root + L"\\" + part);
                if (!CreateDirectoryW(path.c_str(), NULL))
                {
                    const DWORD error = GetLastError();
                    const DWORD attributes = GetFileAttributesW(path.c_str());
                    if (error != ERROR_ALREADY_EXISTS || attributes == INVALID_FILE_ATTRIBUTES ||
                        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                        return Fail(ZipError::Io, L"Cannot create the folder " + root + L"\\" + part + L".", error);
                }
                created.insert(key);
            }
            if (sep == std::wstring::npos)
                return true;
            ++sep;
        }
    };

    for (const ZipEntry& entry : m_entries)
    {
        if (entry.IsDirectory)
        {
            if (!ensureFolder(entry.Name))
                return false;
            continue;
        }
        const size_t sep = entry.Name.rfind(L'\\');
        if (sep != std::wstring::npos && !ensureFolder(entry.Name.substr(0, sep)))
            return false;
        if (!ExtractToFile(entry, root + L"\\" + entry.Name))
            return false;
    }
    return true;
}

} // namespace Sally::Zip
