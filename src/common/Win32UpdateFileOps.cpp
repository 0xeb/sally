// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Win32UpdateFileOps.h"

#include "ExtendedLengthPath.h"

#include <algorithm>

namespace Sally::Update
{

namespace
{

std::wstring Join(const std::wstring& root, const std::wstring& relative)
{
    if (relative.empty())
        return root;
    if (!root.empty() && root.back() == L'\\')
        return root + relative;
    return root + L"\\" + relative;
}

} // namespace

PathKind Win32UpdateFileOps::Query(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(ToExtendedLengthPath(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return PathKind::Missing;
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? PathKind::Folder : PathKind::File;
}

bool Win32UpdateFileOps::List(const std::wstring& root, std::vector<std::wstring>& files,
                              std::vector<std::wstring>& folders, DWORD& error)
{
    files.clear();
    folders.clear();
    error = 0;
    std::vector<std::wstring> pending(1, std::wstring());
    while (!pending.empty())
    {
        const std::wstring relative = pending.back();
        pending.pop_back();
        WIN32_FIND_DATAW find;
        HANDLE handle = FindFirstFileExW(ToExtendedLengthPath(Join(root, relative) + L"\\*").c_str(),
                                         FindExInfoBasic, &find, FindExSearchNameMatch, NULL, 0);
        if (handle == INVALID_HANDLE_VALUE)
        {
            error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND)
                continue;
            return false;
        }
        std::vector<std::wstring> subfolders;
        do
        {
            const std::wstring name = find.cFileName;
            if (name == L"." || name == L"..")
                continue;
            const std::wstring child = relative.empty() ? name : relative + L"\\" + name;
            if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                folders.push_back(child);
                if ((find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                    subfolders.push_back(child);
            }
            else
            {
                files.push_back(child);
            }
        } while (FindNextFileW(handle, &find));
        FindClose(handle);
        // Depth first in name order, so parents always come before their children.
        std::sort(subfolders.rbegin(), subfolders.rend());
        pending.insert(pending.end(), subfolders.begin(), subfolders.end());
    }
    error = 0;
    return true;
}

bool Win32UpdateFileOps::Rename(const std::wstring& from, const std::wstring& to, DWORD& error)
{
    if (MoveFileExW(ToExtendedLengthPath(from).c_str(), ToExtendedLengthPath(to).c_str(), 0))
        return true;
    error = GetLastError();
    return false;
}

bool Win32UpdateFileOps::Copy(const std::wstring& from, const std::wstring& to, DWORD& error)
{
    if (CopyFileExW(ToExtendedLengthPath(from).c_str(), ToExtendedLengthPath(to).c_str(), NULL, NULL, NULL,
                    COPY_FILE_FAIL_IF_EXISTS))
        return true;
    error = GetLastError();
    // A partly written copy must not stay in the install folder.
    if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
        DeleteFileW(ToExtendedLengthPath(to).c_str());
    return false;
}

bool Win32UpdateFileOps::RemoveFile(const std::wstring& path, DWORD& error)
{
    const std::wstring extended = ToExtendedLengthPath(path);
    if (DeleteFileW(extended.c_str()))
        return true;
    error = GetLastError();
    if (error == ERROR_ACCESS_DENIED)
    {
        const DWORD attributes = GetFileAttributesW(extended.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0 &&
            SetFileAttributesW(extended.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY))
        {
            if (DeleteFileW(extended.c_str()))
                return true;
            error = GetLastError();
            SetFileAttributesW(extended.c_str(), attributes);
        }
    }
    return false;
}

bool Win32UpdateFileOps::CreateFolder(const std::wstring& path, DWORD& error)
{
    if (CreateDirectoryW(ToExtendedLengthPath(path).c_str(), NULL))
        return true;
    error = GetLastError();
    return false;
}

bool Win32UpdateFileOps::RemoveFolder(const std::wstring& path, DWORD& error)
{
    if (RemoveDirectoryW(ToExtendedLengthPath(path).c_str()))
        return true;
    error = GetLastError();
    return false;
}

bool Win32UpdateFileOps::AppendLine(const std::wstring& path, const std::string& utf8Line, DWORD& error)
{
    HANDLE file = CreateFileW(ToExtendedLengthPath(path).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    const std::string line = utf8Line + "\r\n";
    DWORD written = 0;
    bool ok = WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, NULL) &&
              written == line.size() && FlushFileBuffers(file);
    if (!ok)
        error = GetLastError();
    CloseHandle(file);
    return ok;
}

bool Win32UpdateFileOps::ReadText(const std::wstring& path, std::string& utf8, DWORD& error)
{
    utf8.clear();
    HANDLE file = CreateFileW(ToExtendedLengthPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart < 16 * 1024 * 1024;
    if (ok)
    {
        utf8.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = utf8.empty() || (ReadFile(file, &utf8[0], static_cast<DWORD>(utf8.size()), &read, NULL) &&
                              read == utf8.size());
    }
    if (!ok)
        error = GetLastError() != 0 ? GetLastError() : ERROR_FILE_TOO_LARGE;
    CloseHandle(file);
    return ok;
}

} // namespace Sally::Update
