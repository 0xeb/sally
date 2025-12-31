// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Wide Path Support Implementation
//
//****************************************************************************

#include "precomp.h"

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "widepath.h"

//
// Internal helper: Check if path is UNC (starts with \\)
//
static BOOL IsUNCPath(const char* path)
{
    return path != NULL && path[0] == '\\' && path[1] == '\\';
}

//
// Internal helper: Check if path already has long path prefix
//
static BOOL HasLongPathPrefix(const char* path)
{
    return path != NULL &&
           path[0] == '\\' && path[1] == '\\' &&
           path[2] == '?' && path[3] == '\\';
}

//
// SalAllocWidePath
//
wchar_t* SalAllocWidePath(const char* ansiPath)
{
    if (ansiPath == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    // Get length of ANSI path
    size_t ansiLen = strlen(ansiPath);

    // Calculate required buffer size for wide string
    int wideLen = MultiByteToWideChar(CP_ACP, 0, ansiPath, -1, NULL, 0);
    if (wideLen == 0)
    {
        return NULL; // Conversion failed, LastError already set
    }

    // Determine if we need the \\?\ prefix
    BOOL needsPrefix = (ansiLen >= SAL_LONG_PATH_THRESHOLD) && !HasLongPathPrefix(ansiPath);
    BOOL isUNC = IsUNCPath(ansiPath);

    // Calculate total buffer size needed
    // \\?\        = 4 chars
    // \\?\UNC\    = 8 chars (but we remove the leading \\ from UNC path, so net +6)
    size_t prefixLen = 0;
    if (needsPrefix)
    {
        prefixLen = isUNC ? 6 : 4; // UNC: \\?\UNC\ minus \\, Local: \\?\
    }

    size_t totalLen = prefixLen + wideLen;
    if (totalLen > SAL_MAX_LONG_PATH)
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }

    // Allocate buffer
    wchar_t* widePath = (wchar_t*)malloc(totalLen * sizeof(wchar_t));
    if (widePath == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    // Build the wide path
    wchar_t* dest = widePath;

    if (needsPrefix)
    {
        if (isUNC)
        {
            // UNC path: \\server\share -> \\?\UNC\server\share
            wcscpy(dest, L"\\\\?\\UNC\\");
            dest += 8;
            // Skip the leading \\ from the original path
            ansiPath += 2;
        }
        else
        {
            // Local path: C:\foo -> \\?\C:\foo
            wcscpy(dest, L"\\\\?\\");
            dest += 4;
        }
    }

    // Convert the (possibly adjusted) ANSI path to wide
    if (MultiByteToWideChar(CP_ACP, 0, ansiPath, -1, dest, wideLen) == 0)
    {
        DWORD err = GetLastError();
        free(widePath);
        SetLastError(err);
        return NULL;
    }

    return widePath;
}

//
// SalFreeWidePath
//
void SalFreeWidePath(wchar_t* widePath)
{
    if (widePath != NULL)
    {
        free(widePath);
    }
}

//
// SalWidePath class implementation
//

SalWidePath::SalWidePath(const char* ansiPath)
    : m_widePath(NULL)
    , m_hasPrefix(FALSE)
{
    if (ansiPath != NULL)
    {
        size_t len = strlen(ansiPath);
        m_hasPrefix = (len >= SAL_LONG_PATH_THRESHOLD) && !HasLongPathPrefix(ansiPath);
        m_widePath = SalAllocWidePath(ansiPath);
    }
}

SalWidePath::~SalWidePath()
{
    SalFreeWidePath(m_widePath);
}

//
// Convenience wrappers
//

HANDLE SalCreateFile(
    const char* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return INVALID_HANDLE_VALUE;
    }

    return CreateFileW(
        widePath.Get(),
        dwDesiredAccess,
        dwShareMode,
        lpSecurityAttributes,
        dwCreationDisposition,
        dwFlagsAndAttributes,
        hTemplateFile);
}

DWORD SalGetFileAttributes(const char* fileName)
{
    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return INVALID_FILE_ATTRIBUTES;
    }

    return GetFileAttributesW(widePath.Get());
}

BOOL SalSetFileAttributes(const char* fileName, DWORD dwFileAttributes)
{
    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return SetFileAttributesW(widePath.Get(), dwFileAttributes);
}

BOOL SalDeleteFile(const char* fileName)
{
    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return DeleteFileW(widePath.Get());
}

BOOL SalRemoveDirectory(const char* dirName)
{
    SalWidePath widePath(dirName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return RemoveDirectoryW(widePath.Get());
}

BOOL SalCreateDirectory(const char* pathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    SalWidePath widePath(pathName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return CreateDirectoryW(widePath.Get(), lpSecurityAttributes);
}

BOOL SalMoveFile(const char* existingFileName, const char* newFileName)
{
    SalWidePath wideExisting(existingFileName);
    SalWidePath wideNew(newFileName);

    if (!wideExisting.IsValid() || !wideNew.IsValid())
    {
        return FALSE;
    }

    return MoveFileW(wideExisting.Get(), wideNew.Get());
}

BOOL SalCopyFile(const char* existingFileName, const char* newFileName, BOOL failIfExists)
{
    SalWidePath wideExisting(existingFileName);
    SalWidePath wideNew(newFileName);

    if (!wideExisting.IsValid() || !wideNew.IsValid())
    {
        return FALSE;
    }

    return CopyFileW(wideExisting.Get(), wideNew.Get(), failIfExists);
}

HANDLE SalFindFirstFile(const char* fileName, WIN32_FIND_DATAW* findData)
{
    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return INVALID_HANDLE_VALUE;
    }

    return FindFirstFileW(widePath.Get(), findData);
}

//
// Handle-tracking variant (debug builds only)
//

#ifdef HANDLES_ENABLE

#include "handles.h"

HANDLE SalCreateFileTracked(
    const char* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile,
    const char* srcFile,
    int srcLine)
{
    HANDLE h = SalCreateFile(fileName, dwDesiredAccess, dwShareMode,
                             lpSecurityAttributes, dwCreationDisposition,
                             dwFlagsAndAttributes, hTemplateFile);

    // Track the handle using Salamander's handle tracking system
    DWORD err = GetLastError();
    __Handles.SetInfo(srcFile, srcLine, __otQuiet)
        .CheckCreate(h != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, h, err, TRUE);

    return h;
}

#endif // HANDLES_ENABLE
