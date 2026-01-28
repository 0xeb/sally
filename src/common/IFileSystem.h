// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
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
