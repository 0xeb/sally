// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <string>

//
// File System Utilities - UI-decoupled helpers for file operations
//
// These functions perform file system queries without any UI dependencies,
// making them suitable for unit testing and headless execution.
//

// File information returned by GetFileInfoW
struct SalFileInfo
{
    DWORD Attributes;
    FILETIME CreationTime;
    FILETIME LastAccessTime;
    FILETIME LastWriteTime;
    ULONGLONG FileSize;
    std::wstring FileName;      // Name only (no path)
    std::wstring AlternateName; // DOS 8.3 name if available
    BOOL IsValid;               // TRUE if info was retrieved successfully
    DWORD LastError;            // GetLastError() if IsValid is FALSE
};

// Retrieves file information for a single file or directory.
// Uses wide APIs for full Unicode and long path support.
//
// Parameters:
//   fullPath - Full path to the file or directory (wide string)
//
// Returns:
//   SalFileInfo with IsValid=TRUE on success, FALSE on failure
//
SalFileInfo GetFileInfoW(const wchar_t* fullPath);

// Builds a full path from directory and filename (wide strings).
// Adds backslash separator if needed.
//
// Parameters:
//   directory - Base directory path
//   fileName  - File or subdirectory name to append
//
// Returns:
//   Combined path (e.g., "C:\Users" + "test.txt" => "C:\Users\test.txt")
//
std::wstring BuildPathW(const wchar_t* directory, const wchar_t* fileName);

// Builds a full path from ANSI directory and filename, returning wide string.
// Converts ANSI to wide, adds backslash separator if needed.
// Adds \\?\ prefix for long paths automatically.
//
// Parameters:
//   directory - Base directory path (ANSI)
//   fileName  - File or subdirectory name to append (ANSI)
//
// Returns:
//   Combined wide path with \\?\ prefix if needed
//
std::wstring BuildPathW(const char* directory, const char* fileName);

// Checks if a path exists (file or directory).
//
// Parameters:
//   path - Path to check (wide string)
//
// Returns:
//   TRUE if path exists, FALSE otherwise
//
BOOL PathExistsW(const wchar_t* path);

// Checks if a path is a directory.
//
// Parameters:
//   path - Path to check (wide string)
//
// Returns:
//   TRUE if path exists and is a directory, FALSE otherwise
//
BOOL IsDirectoryW(const wchar_t* path);

//
// Path parsing helpers - pure string operations, no filesystem access
//

// Extracts the filename (with extension) from a full path.
// Example: "C:\Users\test.txt" => "test.txt"
//
// Parameters:
//   path - Full path (wide string)
//
// Returns:
//   Filename portion after the last backslash, or entire path if no backslash
//
std::wstring GetFileNameW(const wchar_t* path);

// Extracts the directory portion from a full path.
// Example: "C:\Users\test.txt" => "C:\Users"
//
// Parameters:
//   path - Full path (wide string)
//
// Returns:
//   Directory portion before the last backslash, or empty if no backslash
//
std::wstring GetDirectoryW(const wchar_t* path);

// Extracts the file extension (without dot) from a path or filename.
// Example: "test.txt" => "txt", "archive.tar.gz" => "gz"
// Note: ".cvspass" is treated as having extension "cvspass" (Windows behavior)
//
// Parameters:
//   path - Path or filename (wide string)
//
// Returns:
//   Extension without the dot, or empty string if no extension
//
std::wstring GetExtensionW(const wchar_t* path);

// Gets the 8.3 short path name for a file (if available).
// Uses GetShortPathNameW internally.
//
// Parameters:
//   path - Full path to file or directory (wide string)
//
// Returns:
//   Short path name, or empty string if unavailable
//
std::wstring GetShortPathW(const wchar_t* path);

//
// Environment and command expansion helpers
//

// Expands environment variables in a string (e.g., %WINDIR% → C:\Windows).
// Uses ExpandEnvironmentStringsW internally.
//
// Parameters:
//   input - String containing environment variables to expand
//
// Returns:
//   Expanded string, or original if no variables or error
//
std::wstring ExpandEnvironmentW(const wchar_t* input);

// Removes consecutive backslashes from a path (e.g., C:\\\\foo → C:\foo).
// This matches the behavior of RemoveDoubleBackslahesFromPath.
//
// Parameters:
//   path - Path to clean up (modified in place)
//
void RemoveDoubleBackslashesW(std::wstring& path);
