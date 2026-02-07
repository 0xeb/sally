// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Wide Path Support Implementation - Standalone version for tests
// (No precomp.h dependency)
//
//****************************************************************************

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "../common/widepath.h"

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
static BOOL PathHasLongPrefix(const char* path)
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
    BOOL needsPrefix = (ansiLen >= SAL_LONG_PATH_THRESHOLD) && !PathHasLongPrefix(ansiPath);
    BOOL isUNC = IsUNCPath(ansiPath);

    // Calculate total buffer size needed
    // \\?\        = 4 chars
    // \\?\UNC\    = 8 chars (but we remove the leading \\ from UNC path, so net +6)
    size_t prefixLen = 0;
    if (needsPrefix)
    {
        prefixLen = isUNC ? 6 : 4; // UNC: \\?\UNC\ minus \\, Local: \\?\ (prefix)
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
    : m_widePath(NULL), m_hasPrefix(FALSE)
{
    if (ansiPath != NULL)
    {
        size_t len = strlen(ansiPath);
        m_hasPrefix = (len >= SAL_LONG_PATH_THRESHOLD) && !PathHasLongPrefix(ansiPath);
        m_widePath = SalAllocWidePath(ansiPath);
    }
}

SalWidePath::~SalWidePath()
{
    SalFreeWidePath(m_widePath);
}

//
// CPathBuffer class implementation is now inline in widepath.h
//

//
// Convenience wrappers
//

HANDLE SalLPCreateFile(
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

DWORD SalLPGetFileAttributes(const char* fileName)
{
    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return INVALID_FILE_ATTRIBUTES;
    }

    return GetFileAttributesW(widePath.Get());
}

BOOL SalLPSetFileAttributes(const char* fileName, DWORD dwFileAttributes)
{
    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return SetFileAttributesW(widePath.Get(), dwFileAttributes);
}

BOOL SalLPDeleteFile(const char* fileName)
{
    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return DeleteFileW(widePath.Get());
}

BOOL SalLPRemoveDirectory(const char* dirName)
{
    SalWidePath widePath(dirName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return RemoveDirectoryW(widePath.Get());
}

BOOL SalLPCreateDirectory(const char* pathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    SalWidePath widePath(pathName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return CreateDirectoryW(widePath.Get(), lpSecurityAttributes);
}

BOOL SalLPMoveFile(const char* existingFileName, const char* newFileName)
{
    SalWidePath wideExisting(existingFileName);
    SalWidePath wideNew(newFileName);

    if (!wideExisting.IsValid() || !wideNew.IsValid())
    {
        return FALSE;
    }

    return MoveFileW(wideExisting.Get(), wideNew.Get());
}

BOOL SalLPCopyFile(const char* existingFileName, const char* newFileName, BOOL failIfExists)
{
    SalWidePath wideExisting(existingFileName);
    SalWidePath wideNew(newFileName);

    if (!wideExisting.IsValid() || !wideNew.IsValid())
    {
        return FALSE;
    }

    return CopyFileW(wideExisting.Get(), wideNew.Get(), failIfExists);
}

HANDLE SalLPFindFirstFile(const char* fileName, WIN32_FIND_DATAW* findData)
{
    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return INVALID_HANDLE_VALUE;
    }

    return FindFirstFileW(widePath.Get(), findData);
}

HANDLE SalLPFindFirstFileA(const char* fileName, WIN32_FIND_DATAA* findData)
{
    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return INVALID_HANDLE_VALUE;
    }

    WIN32_FIND_DATAW findDataW;
    HANDLE h = FindFirstFileW(widePath.Get(), &findDataW);
    if (h != INVALID_HANDLE_VALUE && findData != NULL)
    {
        // Convert wide find data to ANSI
        findData->dwFileAttributes = findDataW.dwFileAttributes;
        findData->ftCreationTime = findDataW.ftCreationTime;
        findData->ftLastAccessTime = findDataW.ftLastAccessTime;
        findData->ftLastWriteTime = findDataW.ftLastWriteTime;
        findData->nFileSizeHigh = findDataW.nFileSizeHigh;
        findData->nFileSizeLow = findDataW.nFileSizeLow;
        findData->dwReserved0 = findDataW.dwReserved0;
        findData->dwReserved1 = findDataW.dwReserved1;
        WideCharToMultiByte(CP_ACP, 0, findDataW.cFileName, -1,
                            findData->cFileName, MAX_PATH, NULL, NULL);
        WideCharToMultiByte(CP_ACP, 0, findDataW.cAlternateFileName, -1,
                            findData->cAlternateFileName, 14, NULL, NULL);
    }
    return h;
}
