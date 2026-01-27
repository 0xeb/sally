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
