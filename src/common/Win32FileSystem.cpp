// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "IFileSystem.h"

// Win32 implementation of IFileSystem
class Win32FileSystem : public IFileSystem
{
public:
    bool FileExists(const wchar_t* path) override
    {
        DWORD attrs = GetFileAttributesW(path);
        return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool DirectoryExists(const wchar_t* path) override
    {
        DWORD attrs = GetFileAttributesW(path);
        return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    FileResult GetFileInfo(const wchar_t* path, FileInfo& info) override
    {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data))
            return FileResult::Error(GetLastError());

        info.name = path;
        info.size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        info.creationTime = data.ftCreationTime;
        info.lastWriteTime = data.ftLastWriteTime;
        info.attributes = data.dwFileAttributes;
        info.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return FileResult::Ok();
    }

    FileResult DeleteFile(const wchar_t* path) override
    {
        if (::DeleteFileW(path))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult MoveFile(const wchar_t* source, const wchar_t* target) override
    {
        if (::MoveFileW(source, target))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult CopyFile(const wchar_t* source, const wchar_t* target, bool failIfExists) override
    {
        if (::CopyFileW(source, target, failIfExists ? TRUE : FALSE))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult CreateDirectory(const wchar_t* path) override
    {
        if (::CreateDirectoryW(path, NULL))
            return FileResult::Ok();
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS)
            return FileResult::Ok();  // Directory already exists is OK
        return FileResult::Error(err);
    }

    FileResult RemoveDirectory(const wchar_t* path) override
    {
        if (::RemoveDirectoryW(path))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    HANDLE OpenFileForRead(const wchar_t* path, DWORD shareMode) override
    {
        return ::CreateFileW(path, GENERIC_READ, shareMode, NULL,
                             OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    }

    HANDLE CreateFileForWrite(const wchar_t* path, bool failIfExists) override
    {
        DWORD disposition = failIfExists ? CREATE_NEW : CREATE_ALWAYS;
        return ::CreateFileW(path, GENERIC_WRITE, 0, NULL,
                             disposition, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    }

    void CloseHandle(HANDLE h) override
    {
        if (h != INVALID_HANDLE_VALUE && h != NULL)
            ::CloseHandle(h);
    }
};

// Singleton instance
static Win32FileSystem g_win32FileSystem;
IFileSystem* gFileSystem = &g_win32FileSystem;

IFileSystem* GetWin32FileSystem()
{
    return &g_win32FileSystem;
}
