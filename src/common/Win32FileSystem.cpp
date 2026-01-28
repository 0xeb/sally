// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "IFileSystem.h"

// Threshold for adding long path prefix (leave margin below MAX_PATH)
#define LONG_PATH_THRESHOLD 240

// Helper to add long path prefix for paths that need it
// Returns allocated string that must be freed, or NULL on error
static wchar_t* MakeLongPath(const wchar_t* path)
{
    if (path == nullptr)
        return nullptr;

    size_t len = wcslen(path);

    // Check if already has long path prefix
    if (len >= 4 && wcsncmp(path, L"\\?\\", 4) == 0)
    {
        // Already has prefix - just copy
        wchar_t* result = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
        if (result)
            wcscpy(result, path);
        return result;
    }

    // Check if path needs prefix (long path or has trailing space/dot)
    bool needsPrefix = (len >= LONG_PATH_THRESHOLD);

    // Also add prefix if path ends with space or dot (Windows quirk)
    if (!needsPrefix && len > 0)
    {
        wchar_t lastChar = path[len - 1];
        if (lastChar == L' ' || lastChar == L'.')
            needsPrefix = true;
    }

    if (!needsPrefix)
    {
        // Short path - just copy
        wchar_t* result = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
        if (result)
            wcscpy(result, path);
        return result;
    }

    // Check for UNC path
    bool isUNC = (len >= 2 && path[0] == L'\\' && path[1] == L'\\');

    if (isUNC)
    {
        // UNC path - add long path prefix, skip leading backslashes
        size_t newLen = len + 6;  // "\?\UNC\" minus "\\"
        wchar_t* result = (wchar_t*)malloc((newLen + 1) * sizeof(wchar_t));
        if (result)
        {
            wcscpy(result, L"\\?\UNC\\");
            wcscat(result, path + 2);
        }
        return result;
    }
    else
    {
        // Local path - add \?\ prefix
        size_t newLen = len + 4;
        wchar_t* result = (wchar_t*)malloc((newLen + 1) * sizeof(wchar_t));
        if (result)
        {
            wcscpy(result, L"\\?\\");
            wcscat(result, path);
        }
        return result;
    }
}

// RAII wrapper for MakeLongPath result
class LongPath
{
public:
    explicit LongPath(const wchar_t* path) : m_path(MakeLongPath(path)) {}
    ~LongPath() { free(m_path); }
    const wchar_t* Get() const { return m_path; }
    bool IsValid() const { return m_path != nullptr; }
private:
    wchar_t* m_path;
    LongPath(const LongPath&);
    LongPath& operator=(const LongPath&);
};

// Win32 implementation of IFileSystem with long path support
class Win32FileSystem : public IFileSystem
{
public:
    bool FileExists(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return false;
        DWORD attrs = GetFileAttributesW(lp.Get());
        return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool DirectoryExists(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return false;
        DWORD attrs = GetFileAttributesW(lp.Get());
        return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    FileResult GetFileInfo(const wchar_t* path, FileInfo& info) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);

        WIN32_FILE_ATTRIBUTE_DATA data;
        if (!GetFileAttributesExW(lp.Get(), GetFileExInfoStandard, &data))
            return FileResult::Error(GetLastError());

        info.name = path;  // Store original path, not the prefixed one
        info.size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        info.creationTime = data.ftCreationTime;
        info.lastWriteTime = data.ftLastWriteTime;
        info.attributes = data.dwFileAttributes;
        info.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return FileResult::Ok();
    }

    FileResult DeleteFile(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        if (::DeleteFileW(lp.Get()))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult MoveFile(const wchar_t* source, const wchar_t* target) override
    {
        LongPath lpSrc(source);
        LongPath lpDst(target);
        if (!lpSrc.IsValid() || !lpDst.IsValid())
            return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        if (::MoveFileW(lpSrc.Get(), lpDst.Get()))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult CopyFile(const wchar_t* source, const wchar_t* target, bool failIfExists) override
    {
        LongPath lpSrc(source);
        LongPath lpDst(target);
        if (!lpSrc.IsValid() || !lpDst.IsValid())
            return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        if (::CopyFileW(lpSrc.Get(), lpDst.Get(), failIfExists ? TRUE : FALSE))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult CreateDirectory(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        if (::CreateDirectoryW(lp.Get(), NULL))
            return FileResult::Ok();
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS)
            return FileResult::Ok();  // Directory already exists is OK
        return FileResult::Error(err);
    }

    FileResult RemoveDirectory(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        if (::RemoveDirectoryW(lp.Get()))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    HANDLE OpenFileForRead(const wchar_t* path, DWORD shareMode) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HANDLE_VALUE;
        }
        return ::CreateFileW(lp.Get(), GENERIC_READ, shareMode, NULL,
                             OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    }

    HANDLE CreateFileForWrite(const wchar_t* path, bool failIfExists) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HANDLE_VALUE;
        }
        DWORD disposition = failIfExists ? CREATE_NEW : CREATE_ALWAYS;
        return ::CreateFileW(lp.Get(), GENERIC_WRITE, 0, NULL,
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
