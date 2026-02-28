// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "IFileSystem.h"
#include "IPathService.h"
#include "fsutil.h"
#include <string>

namespace
{
bool HasLongPrefix(const wchar_t* path)
{
    return path != NULL && wcsncmp(path, L"\\\\?\\", 4) == 0;
}

bool IsUNCPath(const wchar_t* path)
{
    return path != NULL && path[0] == L'\\' && path[1] == L'\\' && !HasLongPrefix(path);
}

bool IsDriveAbsolutePath(const wchar_t* path)
{
    return path != NULL &&
           ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' &&
           (path[2] == L'\\' || path[2] == L'/');
}

void NormalizePathSeparators(std::wstring& path)
{
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == L'/')
            path[i] = L'\\';
    }
}

const wchar_t* GetBaseName(const wchar_t* path)
{
    if (path == NULL)
        return NULL;

    const wchar_t* lastSlash = wcsrchr(path, L'\\');
    const wchar_t* lastAltSlash = wcsrchr(path, L'/');
    const wchar_t* name = path;
    if (lastSlash != NULL && lastSlash >= name)
        name = lastSlash + 1;
    if (lastAltSlash != NULL && lastAltSlash + 1 > name)
        name = lastAltSlash + 1;
    return name;
}

bool BuildExtendedAbsolutePath(const wchar_t* inputPath, std::wstring& outPath)
{
    if (inputPath == NULL || inputPath[0] == L'\0')
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    if (HasLongPrefix(inputPath))
    {
        outPath.assign(inputPath);
        return true;
    }

    if (gPathService == NULL)
        gPathService = GetWin32PathService();
    if (gPathService == NULL)
    {
        SetLastError(ERROR_INVALID_FUNCTION);
        return false;
    }

    std::wstring fullPath;
    if (IsReservedNulBasenameW(inputPath))
    {
        if (IsDriveAbsolutePath(inputPath) || IsUNCPath(inputPath))
        {
            fullPath.assign(inputPath);
            NormalizePathSeparators(fullPath);
        }
        else
        {
            // GetFullPathNameW("...\\nul") resolves to device path (\\.\nul), so
            // resolve only the parent and then append the reserved file name.
            const wchar_t* baseName = GetBaseName(inputPath);
            size_t parentLen = (baseName != NULL) ? static_cast<size_t>(baseName - inputPath) : 0;
            std::wstring parentPath = parentLen > 0 ? std::wstring(inputPath, parentLen) : std::wstring(L".");

            PathResult parentRes = gPathService->GetFullPathName(parentPath.c_str(), fullPath);
            if (!parentRes.success)
            {
                SetLastError(parentRes.errorCode);
                return false;
            }
            if (fullPath.empty())
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }
            if (fullPath.back() != L'\\' && fullPath.back() != L'/')
                fullPath.push_back(L'\\');
            fullPath.append(baseName != NULL ? baseName : L"nul");
        }
    }
    else
    {
        PathResult fullRes = gPathService->GetFullPathName(inputPath, fullPath);
        if (!fullRes.success)
        {
            SetLastError(fullRes.errorCode);
            return false;
        }

        if (fullPath.empty())
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }
    }

    if (HasLongPrefix(fullPath.c_str()))
    {
        outPath.swap(fullPath);
        return true;
    }

    if (IsUNCPath(fullPath.c_str()))
        outPath = L"\\\\?\\UNC\\" + fullPath.substr(2);
    else
        outPath = L"\\\\?\\" + fullPath;

    if (outPath.size() > SAL_MAX_LONG_PATH - 1)
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return false;
    }
    return true;
}
} // namespace

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
        if (IsReservedNulBasenameW(path))
        {
            std::wstring specialPath;
            if (!BuildExtendedAbsolutePath(path, specialPath))
                return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
            if (::DeleteFileW(specialPath.c_str()))
                return FileResult::Ok();
            return FileResult::Error(GetLastError());
        }

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
