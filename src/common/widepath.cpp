// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Wide Path Support Implementation
//
//****************************************************************************

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "IFileSystem.h"
#include "widepath.h"

//
//

//
// Convenience wrappers
//

static IFileSystem* GetActiveFileSystem()
{
    if (gFileSystem == NULL)
        gFileSystem = GetWin32FileSystem();
    return gFileSystem;
}

static BOOL ResultToBool(const FileResult& result)
{
    if (!result.success)
    {
        SetLastError(result.errorCode);
        return FALSE;
    }
    return TRUE;
}

HANDLE SalLPCreateFile(
    const wchar_t* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    if (fileName == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    return GetActiveFileSystem()->CreateFile(fileName, dwDesiredAccess, dwShareMode,
                                             lpSecurityAttributes, dwCreationDisposition,
                                             dwFlagsAndAttributes, hTemplateFile);
}

DWORD SalLPGetFileAttributes(const wchar_t* fileName)
{
    if (fileName == NULL)
        return INVALID_FILE_ATTRIBUTES;

    return GetActiveFileSystem()->GetFileAttributes(fileName);
}

BOOL SalLPSetFileAttributes(const wchar_t* fileName, DWORD dwFileAttributes)
{
    if (fileName == NULL)
        return FALSE;

    return ResultToBool(GetActiveFileSystem()->SetFileAttributes(fileName, dwFileAttributes));
}

BOOL SalLPDeleteFile(const wchar_t* fileName)
{
    if (fileName == NULL)
        return FALSE;

    return ResultToBool(GetActiveFileSystem()->DeleteFile(fileName));
}

BOOL SalLPRemoveDirectory(const wchar_t* dirName)
{
    if (dirName == NULL)
        return FALSE;

    return ResultToBool(GetActiveFileSystem()->RemoveDirectory(dirName));
}

BOOL SalLPCreateDirectory(const wchar_t* pathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    if (pathName == NULL)
        return FALSE;

    return ResultToBool(GetActiveFileSystem()->CreateDirectoryWithSecurity(
        pathName, lpSecurityAttributes));
}

BOOL SalLPMoveFile(const wchar_t* existingFileName, const wchar_t* newFileName)
{
    if (existingFileName == NULL || newFileName == NULL)
        return FALSE;

    return ResultToBool(GetActiveFileSystem()->MoveFile(existingFileName, newFileName));
}

BOOL SalLPCopyFile(const wchar_t* existingFileName, const wchar_t* newFileName, BOOL failIfExists)
{
    if (existingFileName == NULL || newFileName == NULL)
        return FALSE;

    return ResultToBool(GetActiveFileSystem()->CopyFile(existingFileName, newFileName,
                                                        failIfExists != FALSE));
}

HANDLE SalLPFindFirstFile(const wchar_t* fileName, WIN32_FIND_DATAW* findData)
{
    if (fileName == NULL || findData == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }

    return GetActiveFileSystem()->FindFirstFile(fileName, findData);
}

BOOL SalLPFindNextFile(HANDLE hFindFile, WIN32_FIND_DATAW* findData)
{
    return GetActiveFileSystem()->FindNextFile(hFindFile, findData);
}

BOOL SalLPFindClose(HANDLE hFindFile)
{
    return ResultToBool(GetActiveFileSystem()->CloseFind(hFindFile));
}

//
// Handle-tracking variant (debug builds only)
//

#ifdef HANDLES_ENABLE

#include "handles.h"

HANDLE SalLPCreateFileTracked(
    const wchar_t* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile,
    const char* srcFile /* __FILE__, narrow by construction */,
    int srcLine)
{
    HANDLE h = SalLPCreateFile(fileName, dwDesiredAccess, dwShareMode,
                               lpSecurityAttributes, dwCreationDisposition,
                               dwFlagsAndAttributes, hTemplateFile);

    // Track the handle using Salamander's handle tracking system
    DWORD err = GetLastError();
    GetHandles().SetInfo(srcFile, srcLine, __otQuiet)
        .CheckCreate(h != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, h, err, TRUE);

    return h;
}

HANDLE SalLPFindFirstFileTrackedW(
    const wchar_t* fileName,
    WIN32_FIND_DATAW* findData,
    const char* srcFile /* __FILE__, narrow by construction */,
    int srcLine)
{
    HANDLE h = SalLPFindFirstFile(fileName, findData);

    if (GetActiveFileSystem() == NULL)
    {
        // Track fallback handles. The active Win32 IFileSystem tracks in its FindFirstFile
        // implementation so direct IFileSystem callers and wrapper callers behave alike.
        DWORD err = GetLastError();
        GetHandles().SetInfo(srcFile, srcLine, __otQuiet)
            .CheckCreate(h != INVALID_HANDLE_VALUE, __htFindFile, __hoFindFirstFile, h, err, TRUE);
    }

    return h;
}

#endif // HANDLES_ENABLE
