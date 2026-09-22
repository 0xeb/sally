// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "common/find/FindResultPersistence.h"
#include "common/IFileSystem.h"
#include "common/Win32TextCodec.h"

#include <algorithm>
#include <cwchar>
#include <limits>

namespace sally::find
{
namespace
{
constexpr unsigned char UTF8_BOM[] = {0xef, 0xbb, 0xbf};

bool IsSlash(wchar_t ch)
{
    return ch == L'\\' || ch == L'/';
}

void SetError(std::wstring* error, const wchar_t* text)
{
    if (error != nullptr)
        *error = text != nullptr ? text : L"";
}

std::wstring FormatLastError(DWORD err)
{
    wchar_t* message = nullptr;
    DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, err, 0, reinterpret_cast<LPWSTR>(&message), 0, nullptr);
    std::wstring ret = len != 0 && message != nullptr ? message : L"Windows error";
    if (message != nullptr)
        LocalFree(message);
    while (!ret.empty() && (ret.back() == L'\r' || ret.back() == L'\n' || ret.back() == L'.'))
        ret.pop_back();
    return ret;
}

void SetLastErrorText(std::wstring* error, const wchar_t* prefix, DWORD err)
{
    if (error == nullptr)
        return;
    *error = prefix;
    *error += L" ";
    *error += FormatLastError(err);
    *error += L".";
}

unsigned __int64 FileTimeToUInt64(const FILETIME& ft)
{
    ULARGE_INTEGER value = {};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

FILETIME UInt64ToFileTime(unsigned __int64 value)
{
    ULARGE_INTEGER q = {};
    q.QuadPart = value;
    FILETIME ft = {};
    ft.dwLowDateTime = q.LowPart;
    ft.dwHighDateTime = q.HighPart;
    return ft;
}

bool ParseUnsigned64(const std::wstring& text, unsigned __int64* value)
{
    if (value == nullptr || text.empty())
        return false;
    wchar_t* end = nullptr;
    errno = 0;
    unsigned long long parsed = wcstoull(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || end == nullptr || *end != L'\0')
        return false;
    *value = static_cast<unsigned __int64>(parsed);
    return true;
}

std::wstring LowerExtension(const std::wstring& fileName)
{
    size_t slash = fileName.find_last_of(L"\\/");
    size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return std::wstring();

    std::wstring ext = fileName.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t ch) { return (wchar_t)towlower(ch); });
    return ext;
}

bool HasExtension(const std::wstring& fileName)
{
    return !LowerExtension(fileName).empty();
}

std::wstring CsvEscape(const std::wstring& value)
{
    bool needsQuotes = value.find_first_of(L",\"\r\n") != std::wstring::npos;
    if (!needsQuotes)
        return value;

    std::wstring out;
    out.reserve(value.length() + 2);
    out.push_back(L'"');
    for (wchar_t ch : value)
    {
        if (ch == L'"')
            out.push_back(L'"');
        out.push_back(ch);
    }
    out.push_back(L'"');
    return out;
}

std::wstring UInt64ToString(unsigned __int64 value)
{
    wchar_t buffer[32];
    swprintf_s(buffer, L"%llu", static_cast<unsigned long long>(value));
    return buffer;
}

void AppendCsvField(std::wstring& out, const std::wstring& value, bool first)
{
    if (!first)
        out.push_back(L',');
    out += CsvEscape(value);
}

std::wstring BuildCsvRow(const FindResultRecord& record)
{
    std::wstring out;
    AppendCsvField(out, record.IsDir ? L"dir" : L"file", true);
    AppendCsvField(out, record.FullPath(), false);
    AppendCsvField(out, record.Path, false);
    AppendCsvField(out, record.Name, false);
    AppendCsvField(out, UInt64ToString(record.Size), false);
    AppendCsvField(out, UInt64ToString(FileTimeToUInt64(record.LastWrite)), false);
    AppendCsvField(out, UInt64ToString(record.Attr), false);
    return out;
}

bool PushCsvRow(std::vector<std::vector<std::wstring>>& rows, std::vector<std::wstring>& row,
                std::wstring& field)
{
    row.push_back(field);
    field.clear();
    if (row.size() == 1 && row[0].empty())
    {
        row.clear();
        return true;
    }
    rows.push_back(row);
    row.clear();
    return true;
}

bool ParseCsvRows(const std::wstring& text, std::vector<std::vector<std::wstring>>& rows,
                  std::wstring* error)
{
    std::vector<std::wstring> row;
    std::wstring field;
    bool inQuotes = false;
    bool quotedFieldClosed = false;

    for (size_t i = 0; i < text.length(); i++)
    {
        wchar_t ch = text[i];

        if (inQuotes)
        {
            if (ch == L'"')
            {
                if (i + 1 < text.length() && text[i + 1] == L'"')
                {
                    field.push_back(L'"');
                    i++;
                }
                else
                {
                    inQuotes = false;
                    quotedFieldClosed = true;
                }
            }
            else
                field.push_back(ch);
            continue;
        }

        if (quotedFieldClosed)
        {
            if (ch == L',')
            {
                row.push_back(field);
                field.clear();
                quotedFieldClosed = false;
                continue;
            }
            if (ch == L'\r' || ch == L'\n')
            {
                PushCsvRow(rows, row, field);
                quotedFieldClosed = false;
                if (ch == L'\r' && i + 1 < text.length() && text[i + 1] == L'\n')
                    i++;
                continue;
            }
            SetError(error, L"CSV contains text after a closing quote.");
            return false;
        }

        if (ch == L'"')
        {
            if (!field.empty())
            {
                SetError(error, L"CSV quote appears inside an unquoted field.");
                return false;
            }
            inQuotes = true;
            continue;
        }

        if (ch == L',')
        {
            row.push_back(field);
            field.clear();
            continue;
        }

        if (ch == L'\r' || ch == L'\n')
        {
            PushCsvRow(rows, row, field);
            if (ch == L'\r' && i + 1 < text.length() && text[i + 1] == L'\n')
                i++;
            continue;
        }

        field.push_back(ch);
    }

    if (inQuotes)
    {
        SetError(error, L"CSV contains an unterminated quoted field.");
        return false;
    }

    if (!field.empty() || !row.empty() || quotedFieldClosed)
        PushCsvRow(rows, row, field);
    return true;
}

bool HeaderMatches(const std::vector<std::wstring>& row)
{
    return row.size() == 7 &&
           row[0] == L"Type" &&
           row[1] == L"FullPath" &&
           row[2] == L"Path" &&
           row[3] == L"Name" &&
           row[4] == L"Size" &&
           row[5] == L"LastWriteFileTime" &&
           row[6] == L"Attributes";
}

size_t RootLength(const std::wstring& path)
{
    if (path.length() >= 3 && path[1] == L':' && IsSlash(path[2]))
        return 3;
    if (path.length() >= 2 && IsSlash(path[0]) && IsSlash(path[1]))
    {
        size_t serverEnd = path.find_first_of(L"\\/", 2);
        if (serverEnd == std::wstring::npos)
            return path.length();
        size_t shareEnd = path.find_first_of(L"\\/", serverEnd + 1);
        if (shareEnd == std::wstring::npos)
            return path.length();
        return shareEnd + 1;
    }
    if (!path.empty() && IsSlash(path[0]))
        return 1;
    return 0;
}

bool SplitFullPath(const std::wstring& fullPath, std::wstring& path, std::wstring& name)
{
    std::wstring normalized = fullPath;
    size_t rootLen = RootLength(normalized);
    while (normalized.length() > rootLen && IsSlash(normalized.back()))
        normalized.pop_back();

    size_t pos = normalized.find_last_of(L"\\/");
    if (pos == std::wstring::npos || pos + 1 >= normalized.length())
        return false;

    size_t pathLen = pos;
    if (pos + 1 == RootLength(normalized))
        pathLen = pos + 1;
    path = normalized.substr(0, pathLen);
    name = normalized.substr(pos + 1);
    return !path.empty() && !name.empty();
}

bool IsDotName(const std::wstring& text)
{
    return text == L"." || text == L"..";
}

// Loaded records feed directly into actionable list rows (focus/open/delete),
// so their shape must be structurally safe before anything else looks at them:
// Name a single real component, Path drive-absolute or UNC with no relative
// escapes, no embedded NULs anywhere (C-string consumers would silently
// truncate, diverging display from action target). Characters that cannot
// exist in Win32 names are left to the load-time re-stat — the serializer
// round-trips arbitrary content and the parser stays symmetric with it.
bool ValidateRecordShape(const FindResultRecord& record)
{
    const std::wstring& name = record.Name;
    if (name.empty() || IsDotName(name) ||
        name.find_first_of(L"\\/:") != std::wstring::npos ||
        name.find(L'\0') != std::wstring::npos)
        return false;

    const std::wstring& path = record.Path;
    if (path.find(L'\0') != std::wstring::npos)
        return false;
    size_t rootLen = RootLength(path);
    if (rootLen < 3) // rejects empty, relative, and drive-relative "\foo" forms
        return false;
    size_t pos = rootLen;
    while (pos <= path.length())
    {
        size_t end = path.find_first_of(L"\\/", pos);
        if (end == std::wstring::npos)
            end = path.length();
        std::wstring component = path.substr(pos, end - pos);
        if ((component.empty() && end != path.length()) || IsDotName(component))
            return false;
        pos = end + 1;
    }
    return true;
}

bool StatFullPath(const std::wstring& fullPath, FindResultRecord& record)
{
    IFileSystem* fs = gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();
    FileInfo info = {};
    if (!fs->GetFileInfo(fullPath.c_str(), info).success)
        return false;

    std::wstring path;
    std::wstring name;
    if (!SplitFullPath(fullPath, path, name))
        return false;

    record.Path = path;
    record.Name = name;
    record.Attr = info.attributes;
    record.IsDir = info.isDirectory;
    record.Size = record.IsDir ? 0 : info.size;
    record.LastWrite = info.lastWriteTime;
    return true;
}

bool WideToUtf8(const std::wstring& text, std::string& bytes, std::wstring* error)
{
    std::string encoded;
    const Win32TextConversionResult result = Win32EncodeText(CP_UTF8, text, encoded);
    if (!result)
    {
        if (result.Error == Win32TextConversionError::InputTooLarge)
            SetError(error, L"Text is too large to encode as UTF-8.");
        else
            SetLastErrorText(error, L"Cannot encode UTF-8:", result.Win32Error);
        return false;
    }
    bytes.swap(encoded);
    return true;
}

bool Utf8ToWide(const unsigned char* bytes, size_t size, std::wstring& text, std::wstring* error)
{
    if (size >= 3 && bytes[0] == UTF8_BOM[0] && bytes[1] == UTF8_BOM[1] && bytes[2] == UTF8_BOM[2])
    {
        bytes += 3;
        size -= 3;
    }

    if (size == 0)
    {
        text.clear();
        return true;
    }
    std::wstring decoded;
    const Win32TextConversionResult result = Win32DecodeText(
        CP_UTF8, reinterpret_cast<const char*>(bytes), size, decoded);
    if (!result)
    {
        if (result.Error == Win32TextConversionError::InputTooLarge)
            SetError(error, L"File is too large to decode as UTF-8.");
        else
            SetLastErrorText(error, L"Cannot decode UTF-8:", result.Win32Error);
        return false;
    }
    text.swap(decoded);
    return true;
}

bool BytesToWideText(const std::vector<unsigned char>& bytes, std::wstring& text, std::wstring* error)
{
    if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
    {
        size_t payload = bytes.size() - 2;
        if ((payload & 1) != 0)
        {
            SetError(error, L"UTF-16LE file has an odd byte count.");
            return false;
        }
        text.resize(payload / sizeof(wchar_t));
        if (payload > 0)
            memcpy(text.data(), bytes.data() + 2, payload);
        return true;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff)
    {
        SetError(error, L"UTF-16BE files are not supported.");
        return false;
    }
    return Utf8ToWide(bytes.data(), bytes.size(), text, error);
}

bool ReadWholeFile(const std::wstring& fileName, std::vector<unsigned char>& bytes, std::wstring* error)
{
    IFileSystem* fs = gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();
    HANDLE file = fs->OpenFileForRead(fileName.c_str(), FILE_SHARE_READ);
    if (file == INVALID_HANDLE_VALUE)
    {
        SetLastErrorText(error, L"Cannot open file:", GetLastError());
        return false;
    }

    uint64_t size = 0;
    FileResult sizeResult = fs->GetHandleFileSize(file, &size);
    if (!sizeResult.success || size > (std::numeric_limits<size_t>::max)())
    {
        DWORD err = sizeResult.success ? ERROR_NOT_ENOUGH_MEMORY : sizeResult.errorCode;
        fs->CloseFileHandle(file);
        SetLastErrorText(error, L"Cannot get file size:", err);
        return false;
    }

    bytes.resize((size_t)size);
    size_t offset = 0;
    while (offset < bytes.size())
    {
        DWORD toRead = (DWORD)(std::min<size_t>)(bytes.size() - offset, 1024 * 1024);
        DWORD read = 0;
        FileResult readResult = fs->ReadFromHandle(file, bytes.data() + offset, toRead, &read);
        if (!readResult.success)
        {
            DWORD err = readResult.errorCode;
            fs->CloseFileHandle(file);
            SetLastErrorText(error, L"Cannot read file:", err);
            return false;
        }
        if (read == 0)
            break;
        offset += read;
    }
    fs->CloseFileHandle(file);
    bytes.resize(offset);
    return true;
}

bool WriteWholeFile(const std::wstring& fileName, const std::string& bytes, std::wstring* error)
{
    IFileSystem* fs = gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();
    HANDLE file = fs->CreateFileForWrite(fileName.c_str(), false);
    if (file == INVALID_HANDLE_VALUE)
    {
        SetLastErrorText(error, L"Cannot create file:", GetLastError());
        return false;
    }

    size_t offset = 0;
    while (offset < bytes.size())
    {
        DWORD toWrite = (DWORD)(std::min<size_t>)(bytes.size() - offset, 1024 * 1024);
        DWORD written = 0;
        FileResult writeResult = fs->WriteToHandle(file, bytes.data() + offset, toWrite, &written);
        if (!writeResult.success || written != toWrite)
        {
            DWORD err = writeResult.success ? ERROR_WRITE_FAULT : writeResult.errorCode;
            fs->CloseFileHandle(file);
            SetLastErrorText(error, L"Cannot write file:", err);
            return false;
        }
        offset += written;
    }
    fs->CloseFileHandle(file);
    return true;
}

bool ParseCsvRecord(const std::vector<std::wstring>& row, size_t rowNumber,
                    FindResultRecord& record, std::wstring* error)
{
    if (row.size() != 7)
    {
        wchar_t buffer[128];
        swprintf_s(buffer, L"CSV row %u has the wrong number of columns.", (unsigned)rowNumber);
        SetError(error, buffer);
        return false;
    }

    if (row[0] == L"dir")
        record.IsDir = true;
    else if (row[0] == L"file")
        record.IsDir = false;
    else
    {
        wchar_t buffer[128];
        swprintf_s(buffer, L"CSV row %u has an unknown result type.", (unsigned)rowNumber);
        SetError(error, buffer);
        return false;
    }

    record.Path = row[2];
    record.Name = row[3];
    if ((record.Path.empty() || record.Name.empty()) && !SplitFullPath(row[1], record.Path, record.Name))
    {
        wchar_t buffer[128];
        swprintf_s(buffer, L"CSV row %u does not contain a valid full path.", (unsigned)rowNumber);
        SetError(error, buffer);
        return false;
    }
    if (!ValidateRecordShape(record))
    {
        wchar_t buffer[128];
        swprintf_s(buffer, L"CSV row %u contains an unsafe path or name.", (unsigned)rowNumber);
        SetError(error, buffer);
        return false;
    }

    unsigned __int64 parsed = 0;
    if (!ParseUnsigned64(row[4], &parsed))
    {
        wchar_t buffer[128];
        swprintf_s(buffer, L"CSV row %u has an invalid size.", (unsigned)rowNumber);
        SetError(error, buffer);
        return false;
    }
    record.Size = parsed;

    if (!ParseUnsigned64(row[5], &parsed))
    {
        wchar_t buffer[128];
        swprintf_s(buffer, L"CSV row %u has an invalid last-write time.", (unsigned)rowNumber);
        SetError(error, buffer);
        return false;
    }
    record.LastWrite = UInt64ToFileTime(parsed);

    if (!ParseUnsigned64(row[6], &parsed) || parsed > (std::numeric_limits<DWORD>::max)())
    {
        wchar_t buffer[128];
        swprintf_s(buffer, L"CSV row %u has invalid file attributes.", (unsigned)rowNumber);
        SetError(error, buffer);
        return false;
    }
    record.Attr = (DWORD)parsed;
    return true;
}

} // namespace

std::wstring FindResultRecord::FullPath() const
{
    std::wstring fullPath = Path;
    if (!fullPath.empty() && !IsSlash(fullPath.back()))
        fullPath.push_back(L'\\');
    fullPath += Name;
    return fullPath;
}

std::wstring SerializeFindResultsCsv(const std::vector<FindResultRecord>& records)
{
    std::wstring out = L"Type,FullPath,Path,Name,Size,LastWriteFileTime,Attributes\r\n";
    for (const FindResultRecord& record : records)
    {
        out += BuildCsvRow(record);
        out += L"\r\n";
    }
    return out;
}

std::wstring SerializeFindResultsText(const std::vector<FindResultRecord>& records)
{
    std::wstring out;
    for (const FindResultRecord& record : records)
    {
        out += record.FullPath();
        out += L"\r\n";
    }
    return out;
}

bool ParseFindResultsCsv(const std::wstring& text, std::vector<FindResultRecord>& records,
                         std::wstring* error)
{
    records.clear();

    std::vector<std::vector<std::wstring>> rows;
    if (!ParseCsvRows(text, rows, error))
        return false;
    if (rows.empty() || !HeaderMatches(rows[0]))
    {
        SetError(error, L"CSV header is not recognized.");
        return false;
    }

    for (size_t i = 1; i < rows.size(); i++)
    {
        FindResultRecord record;
        if (!ParseCsvRecord(rows[i], i + 1, record, error))
            return false;
        records.push_back(record);
    }
    return true;
}

bool ParseFindResultsText(const std::wstring& text, std::vector<FindResultRecord>& records,
                          size_t* skippedRows, std::wstring* error)
{
    records.clear();
    if (skippedRows != nullptr)
        *skippedRows = 0;

    size_t pos = 0;
    while (pos < text.length())
    {
        size_t end = text.find_first_of(L"\r\n", pos);
        std::wstring line = end == std::wstring::npos ? text.substr(pos) : text.substr(pos, end - pos);
        if (end == std::wstring::npos)
            pos = text.length();
        else
        {
            pos = end + 1;
            if (text[end] == L'\r' && pos < text.length() && text[pos] == L'\n')
                pos++;
        }

        if (line.empty())
            continue;

        FindResultRecord record;
        if (StatFullPath(line, record))
            records.push_back(record);
        else if (skippedRows != nullptr)
            (*skippedRows)++;
    }
    SetError(error, L"");
    return true;
}

bool SaveFindResultsFile(const std::wstring& fileName, FindResultsFormat format,
                         const std::vector<FindResultRecord>& records, std::wstring* error)
{
    std::wstring text = format == FindResultsFormat::Csv
                            ? SerializeFindResultsCsv(records)
                            : SerializeFindResultsText(records);
    std::string utf8;
    if (!WideToUtf8(text, utf8, error))
        return false;

    // UTF8_BOM is a 3-byte unsigned char[]. Reinterpreting it as
    // wchar_t* and iterating [begin, begin + sizeof(UTF8_BOM)) read 3 wchar_t
    // elements (6 bytes) - 3 bytes past the actual 3-byte array - producing
    // undefined/garbage BOM bytes on disk (caught by gtest_find_results_persistence
    // once sally_e2e_core stopped being stale; see 00-master.md's History-cluster
    // entry for how that was found).
    std::string bytes(reinterpret_cast<const char*>(UTF8_BOM), sizeof(UTF8_BOM));
    bytes += utf8;
    return WriteWholeFile(fileName, bytes, error);
}

bool LoadFindResultsFile(const std::wstring& fileName, FindResultsFormat format,
                         std::vector<FindResultRecord>& records, size_t* skippedRows,
                         std::wstring* error)
{
    if (skippedRows != nullptr)
        *skippedRows = 0;

    std::vector<unsigned char> bytes;
    if (!ReadWholeFile(fileName, bytes, error))
        return false;

    std::wstring text;
    if (!BytesToWideText(bytes, text, error))
        return false;

    if (format == FindResultsFormat::Csv)
    {
        // Like the text format, re-stat every row: loaded results become
        // actionable list rows, so only rows that exist on disk — with their
        // live metadata — may enter the list; the rest count as skipped.
        std::vector<FindResultRecord> parsed;
        if (!ParseFindResultsCsv(text, parsed, error))
            return false;
        records.clear();
        for (const FindResultRecord& record : parsed)
        {
            FindResultRecord live;
            if (StatFullPath(record.FullPath(), live))
                records.push_back(live);
            else if (skippedRows != nullptr)
                (*skippedRows)++;
        }
        return true;
    }
    return ParseFindResultsText(text, records, skippedRows, error);
}

bool TryFindResultsFormatFromPathOrFilter(const std::wstring& fileName, DWORD filterIndex,
                                          FindResultsFormat* format)
{
    if (format == nullptr)
        return false;

    std::wstring ext = LowerExtension(fileName);
    if (ext == L".csv")
    {
        *format = FindResultsFormat::Csv;
        return true;
    }
    if (ext == L".txt")
    {
        *format = FindResultsFormat::Text;
        return true;
    }
    if (!ext.empty())
        return false;

    if (filterIndex == 1)
    {
        *format = FindResultsFormat::Csv;
        return true;
    }
    if (filterIndex == 2)
    {
        *format = FindResultsFormat::Text;
        return true;
    }
    return false;
}

void AppendFindResultsDefaultExtension(std::wstring& fileName, FindResultsFormat format)
{
    if (HasExtension(fileName))
        return;
    fileName += format == FindResultsFormat::Csv ? L".csv" : L".txt";
}

} // namespace sally::find
