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

// Extracts the root path from a full path (pure string operation).
// For UNC paths: "\\server\share\dir" => "\\server\share\"
// For local paths: "C:\Users\test" => "C:\"
//
// Parameters:
//   path - Full path (wide string)
//
// Returns:
//   Root path with trailing backslash
//
std::wstring GetRootPathW(const wchar_t* path);

// Checks if a path is a UNC root path (\\server\share with no subdirectories).
//
// Parameters:
//   path - Path to check (wide string)
//
// Returns:
//   TRUE if path is UNC root, FALSE otherwise
//
BOOL IsUNCRootPathW(const wchar_t* path);

// Checks if a path is a UNC path (starts with \\).
//
// Parameters:
//   path - Path to check (wide string)
//
// Returns:
//   TRUE if path starts with \\, FALSE otherwise
//
BOOL IsUNCPathW(const wchar_t* path);

// Checks if a path has a trailing backslash.
//
// Parameters:
//   path - Path to check (wide string)
//
// Returns:
//   TRUE if path ends with \, FALSE otherwise
//
BOOL HasTrailingBackslashW(const wchar_t* path);

// Removes trailing backslash from path if present.
// Modifies the string in place.
//
// Parameters:
//   path - Path to modify
//
void RemoveTrailingBackslashW(std::wstring& path);

// Adds trailing backslash to path if not present.
//
// Parameters:
//   path - Path to modify
//
void AddTrailingBackslashW(std::wstring& path);

// Removes the extension from a path/filename.
// Example: "test.txt" => "test", "archive.tar.gz" => "archive.tar"
//
// Parameters:
//   path - Path or filename to modify
//
void RemoveExtensionW(std::wstring& path);

// Sets or replaces the extension of a path/filename.
// Example: SetExtensionW("test.txt", L".doc") => "test.doc"
//          SetExtensionW("test", L".doc") => "test.doc"
// Note: extension should include the dot.
//
// Parameters:
//   path - Path or filename to modify
//   extension - New extension (including dot)
//
void SetExtensionW(std::wstring& path, const wchar_t* extension);

// Extracts the filename without extension from a path.
// Example: "C:\\Users\\test.txt" => "test"
//
// Parameters:
//   path - Full path (wide string)
//
// Returns:
//   Filename without extension
//
std::wstring GetFileNameWithoutExtensionW(const wchar_t* path);

// Gets the parent directory of a path (goes up one level).
// Example: "C:\\Users\\Test" => "C:\\Users"
//          "C:\\Users\\" => "C:\\"
//          "C:\\" => "" (empty, can't go higher)
//
// Parameters:
//   path - Path (wide string)
//
// Returns:
//   Parent path, or empty if at root
//
std::wstring GetParentPathW(const wchar_t* path);

// Compares two paths for equality (case-insensitive, ignores trailing backslash).
// Example: IsTheSamePathW("C:\\Users", "c:\\users\\") => TRUE
//
// Parameters:
//   path1 - First path
//   path2 - Second path
//
// Returns:
//   TRUE if paths are equivalent, FALSE otherwise
//
BOOL IsTheSamePathW(const wchar_t* path1, const wchar_t* path2);

// Checks if path starts with prefix (case-insensitive).
// Example: PathStartsWithW("C:\\Users\\Test", "C:\\Users") => TRUE
//
// Parameters:
//   path - Full path to check
//   prefix - Prefix to look for
//
// Returns:
//   TRUE if path starts with prefix, FALSE otherwise
//
BOOL PathStartsWithW(const wchar_t* path, const wchar_t* prefix);
