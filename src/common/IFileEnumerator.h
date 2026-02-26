// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <cstdint>
#include <windows.h>

// File entry returned by enumeration
struct FileEnumEntry
{
    std::wstring name;          // File name only (not full path)
    uint64_t size;
    FILETIME creationTime;
    FILETIME lastAccessTime;
    FILETIME lastWriteTime;
    DWORD attributes;

    bool IsDirectory() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
    bool IsHidden() const { return (attributes & FILE_ATTRIBUTE_HIDDEN) != 0; }
    bool IsSystem() const { return (attributes & FILE_ATTRIBUTE_SYSTEM) != 0; }
    bool IsReadOnly() const { return (attributes & FILE_ATTRIBUTE_READONLY) != 0; }
};

// Result of enumeration operations
struct EnumResult
{
    bool success;
    bool noMoreFiles;  // true when enumeration complete (ERROR_NO_MORE_FILES)
    DWORD errorCode;

    static EnumResult Ok() { return {true, false, ERROR_SUCCESS}; }
    static EnumResult Done() { return {true, true, ERROR_NO_MORE_FILES}; }
    static EnumResult Error(DWORD err) { return {false, false, err}; }
};

// Opaque handle for enumeration sessions
typedef void* HENUM;
#define INVALID_HENUM nullptr

// Abstract interface for file/directory enumeration
// Enables mocking for tests and Unicode/long path support
class IFileEnumerator
{
public:
    virtual ~IFileEnumerator() = default;

    // Start enumerating files in a directory
    // path: Directory path (e.g., "C:\\Users" or "C:\\Users\\*")
    // pattern: Optional pattern filter (e.g., "*.txt"). If path already contains pattern, this can be empty.
    // Returns INVALID_HENUM on error (call GetLastError())
    virtual HENUM StartEnum(const wchar_t* path, const wchar_t* pattern = nullptr) = 0;

    // Get next file entry
    // Returns EnumResult::Done() when no more files, EnumResult::Error() on failure
    virtual EnumResult NextFile(HENUM handle, FileEnumEntry& entry) = 0;

    // Close enumeration handle
    virtual void EndEnum(HENUM handle) = 0;

    // Convenience: Check if path contains wildcard pattern
    static bool HasPattern(const wchar_t* path)
    {
        if (!path) return false;
        while (*path)
        {
            if (*path == L'*' || *path == L'?')
                return true;
            path++;
        }
        return false;
    }
};

// Global file enumerator instance - default is Win32 implementation
extern IFileEnumerator* gFileEnumerator;

// Returns the default Win32 implementation
IFileEnumerator* GetWin32FileEnumerator();

// ANSI helper: Convert ANSI path and start enumeration
inline std::wstring AnsiEnumPathToWide(const char* path)
{
    if (!path || !*path) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, path, -1, nullptr, 0);
    if (len == 0) return L"";
    std::wstring wide;
    wide.resize(len);
    MultiByteToWideChar(CP_ACP, 0, path, -1, &wide[0], len);
    wide.resize(len - 1);
    return wide;
}

inline HENUM StartEnumA(IFileEnumerator* enumerator, const char* path, const char* pattern = nullptr)
{
    std::wstring widePath = AnsiEnumPathToWide(path);
    std::wstring widePattern = pattern ? AnsiEnumPathToWide(pattern) : L"";
    return enumerator->StartEnum(widePath.c_str(), widePattern.empty() ? nullptr : widePattern.c_str());
}
