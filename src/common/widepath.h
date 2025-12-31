// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Wide Path Support for Long Paths (>MAX_PATH)
//
// These utilities convert ANSI paths to wide strings and add the \\?\ prefix
// when needed to support paths longer than MAX_PATH (260 characters).
//
// NOTE: This is Phase 1 - long path support only. It does NOT fix Unicode
// filenames that are outside the current Windows codepage. That requires
// a larger architectural change (Phase 2).
//
//****************************************************************************

#pragma once

#include <windows.h>

// Threshold for adding \\?\ prefix (leave some margin below MAX_PATH)
#define SAL_LONG_PATH_THRESHOLD 240

// Maximum path length with \\?\ prefix
#define SAL_MAX_LONG_PATH 32767

//
// SalWidePath
//
// RAII wrapper for wide path conversion. Converts ANSI path to wide string
// and adds \\?\ prefix if path exceeds SAL_LONG_PATH_THRESHOLD.
//
// Usage:
//   SalWidePath widePath(ansiPath);
//   if (widePath.IsValid())
//       CreateFileW(widePath.Get(), ...);
//
// The prefix is added as follows:
//   - Local paths: C:\foo\bar  ->  \\?\C:\foo\bar
//   - UNC paths:   \\server\share  ->  \\?\UNC\server\share
//
class SalWidePath
{
public:
    // Constructs wide path from ANSI path
    // If path is NULL, IsValid() returns FALSE
    explicit SalWidePath(const char* ansiPath);

    // Destructor frees allocated memory
    ~SalWidePath();

    // Returns TRUE if conversion succeeded
    BOOL IsValid() const { return m_widePath != NULL; }

    // Returns the wide path string (or NULL if invalid)
    const wchar_t* Get() const { return m_widePath; }

    // Implicit conversion for convenience
    operator const wchar_t*() const { return m_widePath; }

    // Returns TRUE if \\?\ prefix was added
    BOOL HasLongPathPrefix() const { return m_hasPrefix; }

private:
    wchar_t* m_widePath;
    BOOL m_hasPrefix;

    // Disable copy
    SalWidePath(const SalWidePath&);
    SalWidePath& operator=(const SalWidePath&);
};

//
// Helper functions for manual memory management (if RAII not suitable)
//

// Converts ANSI path to wide path with optional \\?\ prefix
// Returns allocated wchar_t* that must be freed with SalFreeWidePath()
// Returns NULL on failure (sets LastError)
wchar_t* SalAllocWidePath(const char* ansiPath);

// Frees memory allocated by SalAllocWidePath
void SalFreeWidePath(wchar_t* widePath);

//
// Convenience wrappers for common file operations
// These handle the conversion internally
//

// CreateFile wrapper that supports long paths
HANDLE SalCreateFile(
    const char* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile);

// GetFileAttributes wrapper that supports long paths
DWORD SalGetFileAttributes(const char* fileName);

// SetFileAttributes wrapper that supports long paths
BOOL SalSetFileAttributes(const char* fileName, DWORD dwFileAttributes);

// DeleteFile wrapper that supports long paths
BOOL SalDeleteFile(const char* fileName);

// RemoveDirectory wrapper that supports long paths
BOOL SalRemoveDirectory(const char* dirName);

// CreateDirectory wrapper that supports long paths
BOOL SalCreateDirectory(const char* pathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);

// MoveFile wrapper that supports long paths
BOOL SalMoveFile(const char* existingFileName, const char* newFileName);

// CopyFile wrapper that supports long paths
BOOL SalCopyFile(const char* existingFileName, const char* newFileName, BOOL failIfExists);

// FindFirstFile wrapper that supports long paths
// Note: Returns wide find data; caller must convert if needed
HANDLE SalFindFirstFile(const char* fileName, WIN32_FIND_DATAW* findData);

// FindNextFile (standard API, no wrapper needed since handle is already wide)
// Use standard FindNextFileW with handle from SalFindFirstFile

//
// Handle-tracking variants for integration with Salamander's HANDLES system
// These should be used when HANDLES_ENABLE is defined (debug builds)
//
// Usage:
//   HANDLE h = SalCreateFileH(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
//   // Handle is automatically tracked via HANDLES_ADD_EX
//

#ifdef HANDLES_ENABLE

// CreateFile with handle tracking - use instead of HANDLES_Q(CreateFile(...))
#define SalCreateFileH(fileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile) \
    SalCreateFileTracked(fileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile, __FILE__, __LINE__)

HANDLE SalCreateFileTracked(
    const char* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile,
    const char* srcFile,
    int srcLine);

#else // !HANDLES_ENABLE

// In release builds, just use the regular wrapper
#define SalCreateFileH SalCreateFile

#endif // HANDLES_ENABLE
