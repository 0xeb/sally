// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "IFileSystem.h"
#include "IPathService.h"
#include <string>

// RAII wrapper for ToLongPath result
class LongPath
{
public:
    explicit LongPath(const wchar_t* path) : m_valid(false)
    {
        if (gPathService == nullptr)
            gPathService = GetWin32PathService();
        if (gPathService == nullptr)
        {
            SetLastError(ERROR_INVALID_FUNCTION);
            return;
        }

        PathResult res = gPathService->ToLongPath(path, m_path);
        if (!res.success)
        {
            SetLastError(res.errorCode);
            return;
        }
        m_valid = true;
    }

    const wchar_t* Get() const { return m_path.c_str(); }
    bool IsValid() const { return m_valid; }

private:
    std::wstring m_path;
    bool m_valid;
    LongPath(const LongPath&);
    LongPath& operator=(const LongPath&);
};

// Win32 implementation of IFileSystem with long path support
class Win32FileSystem : public IFileSystem
{
public:
    static DWORD LastErrorOr(DWORD fallback)
    {
        DWORD err = GetLastError();
        return err != ERROR_SUCCESS ? err : fallback;
    }

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
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));

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

    DWORD GetFileAttributes(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(LastErrorOr(ERROR_INVALID_PARAMETER));
            return INVALID_FILE_ATTRIBUTES;
        }
        return ::GetFileAttributesW(lp.Get());
    }

    FileResult SetFileAttributes(const wchar_t* path, DWORD attributes) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::SetFileAttributesW(lp.Get(), attributes))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult DeleteFile(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::DeleteFileW(lp.Get()))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult MoveFile(const wchar_t* source, const wchar_t* target) override
    {
        LongPath lpSrc(source);
        LongPath lpDst(target);
        if (!lpSrc.IsValid() || !lpDst.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::MoveFileW(lpSrc.Get(), lpDst.Get()))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult CopyFile(const wchar_t* source, const wchar_t* target, bool failIfExists) override
    {
        LongPath lpSrc(source);
        LongPath lpDst(target);
        if (!lpSrc.IsValid() || !lpDst.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::CopyFileW(lpSrc.Get(), lpDst.Get(), failIfExists ? TRUE : FALSE))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult CreateDirectory(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
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
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::RemoveDirectoryW(lp.Get()))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    HANDLE CreateFile(const wchar_t* path,
                      DWORD desiredAccess,
                      DWORD shareMode,
                      LPSECURITY_ATTRIBUTES securityAttributes,
                      DWORD creationDisposition,
                      DWORD flagsAndAttributes,
                      HANDLE templateFile) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(LastErrorOr(ERROR_INVALID_PARAMETER));
            return INVALID_HANDLE_VALUE;
        }
        return ::CreateFileW(lp.Get(), desiredAccess, shareMode, securityAttributes,
                             creationDisposition, flagsAndAttributes, templateFile);
    }

    HANDLE FindFirstFile(const wchar_t* path, WIN32_FIND_DATAW* findData) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(LastErrorOr(ERROR_INVALID_PARAMETER));
            return INVALID_HANDLE_VALUE;
        }
        return ::FindFirstFileW(lp.Get(), findData);
    }

    BOOL FindNextFile(HANDLE findHandle, WIN32_FIND_DATAW* findData) override
    {
        return ::FindNextFileW(findHandle, findData);
    }

    HANDLE OpenFileForRead(const wchar_t* path, DWORD shareMode) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(LastErrorOr(ERROR_INVALID_PARAMETER));
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
            SetLastError(LastErrorOr(ERROR_INVALID_PARAMETER));
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
