// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#pragma once

#include <string>
#include <cstdint>
#include <windows.h>

// File attributes for IFileSystem operations
struct FileInfo
{
    std::wstring name;
    uint64_t size;
    FILETIME creationTime;
    FILETIME lastWriteTime;
    DWORD attributes;
    bool isDirectory;
};

// Result of file operations
struct FileResult
{
    bool success;
    DWORD errorCode;  // Win32 error code on failure

    static FileResult Ok() { return {true, 0}; }
    static FileResult Error(DWORD err) { return {false, err}; }
};

// Abstract interface for file system operations
// Enables mocking for tests and potential future OS abstraction
class IFileSystem
{
public:
    virtual ~IFileSystem() {}

    // File existence and info
    virtual bool FileExists(const wchar_t* path) = 0;
    virtual bool DirectoryExists(const wchar_t* path) = 0;
    virtual FileResult GetFileInfo(const wchar_t* path, FileInfo& info) = 0;

    // File attributes
    virtual DWORD GetFileAttributes(const wchar_t* path) = 0;  // Returns INVALID_FILE_ATTRIBUTES on error
    virtual FileResult SetFileAttributes(const wchar_t* path, DWORD attributes) = 0;

    // File operations
    virtual FileResult DeleteFile(const wchar_t* path) = 0;
    virtual FileResult MoveFile(const wchar_t* source, const wchar_t* target) = 0;
    virtual FileResult CopyFile(const wchar_t* source, const wchar_t* target, bool failIfExists) = 0;

    // Directory operations
    virtual FileResult CreateDirectory(const wchar_t* path) = 0;
    virtual FileResult RemoveDirectory(const wchar_t* path) = 0;

    // File handle operations (for copy loops, etc.)
    virtual HANDLE OpenFileForRead(const wchar_t* path, DWORD shareMode = FILE_SHARE_READ) = 0;
    virtual HANDLE CreateFileForWrite(const wchar_t* path, bool failIfExists) = 0;
    virtual void CloseHandle(HANDLE h) = 0;
};

// Global file system instance - default is Win32 implementation
extern IFileSystem* gFileSystem;

// Returns the default Win32 implementation
IFileSystem* GetWin32FileSystem();

// Helper to convert ANSI path to wide string
inline std::wstring AnsiPathToWide(const char* path)
{
    if (!path) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
    if (len == 0) return L"";
    std::wstring widePath;
    widePath.resize(len);
    MultiByteToWideChar(CP_ACP, 0, path, -1, &widePath[0], len);
    widePath.resize(len - 1);  // Remove null terminator from string length
    return widePath;
}

// ANSI helpers - convert ANSI paths to wide and call wide versions
// Use when migrating ANSI code to IFileSystem

inline FileResult DeleteFileA(IFileSystem* fs, const char* path)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path) return FileResult::Error(GetLastError());
    return fs->DeleteFile(widePath.c_str());
}

inline FileResult MoveFileA(IFileSystem* fs, const char* source, const char* target)
{
    std::wstring wideSource = AnsiPathToWide(source);
    std::wstring wideTarget = AnsiPathToWide(target);
    if ((wideSource.empty() && source && *source) || (wideTarget.empty() && target && *target))
        return FileResult::Error(GetLastError());
    return fs->MoveFile(wideSource.c_str(), wideTarget.c_str());
}

// Wide-path-aware MoveFile: uses wideSource/wideTarget when non-empty, otherwise falls back to ANSI conversion
inline FileResult MoveFileAW(IFileSystem* fs, const char* source, const char* target,
                             const std::wstring& wideSource, const std::wstring& wideTarget)
{
    std::wstring srcFallback, tgtFallback;
    const std::wstring& src = !wideSource.empty() ? wideSource : (srcFallback = AnsiPathToWide(source));
    const std::wstring& tgt = !wideTarget.empty() ? wideTarget : (tgtFallback = AnsiPathToWide(target));
    if ((src.empty() && source && *source) || (tgt.empty() && target && *target))
        return FileResult::Error(GetLastError());
    return fs->MoveFile(src.c_str(), tgt.c_str());
}

inline FileResult CopyFileA(IFileSystem* fs, const char* source, const char* target, bool failIfExists)
{
    std::wstring wideSource = AnsiPathToWide(source);
    std::wstring wideTarget = AnsiPathToWide(target);
    if ((wideSource.empty() && source && *source) || (wideTarget.empty() && target && *target))
        return FileResult::Error(GetLastError());
    return fs->CopyFile(wideSource.c_str(), wideTarget.c_str(), failIfExists);
}

inline DWORD GetFileAttributesA(IFileSystem* fs, const char* path)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_FILE_ATTRIBUTES;
    }
    return fs->GetFileAttributes(widePath.c_str());
}

inline FileResult SetFileAttributesA(IFileSystem* fs, const char* path, DWORD attributes)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path)
        return FileResult::Error(GetLastError());
    return fs->SetFileAttributes(widePath.c_str(), attributes);
}

inline FileResult GetFileInfoA(IFileSystem* fs, const char* path, FileInfo& info)
{
    std::wstring widePath = AnsiPathToWide(path);
    if (widePath.empty() && path && *path)
        return FileResult::Error(GetLastError());
    return fs->GetFileInfo(widePath.c_str(), info);
}
