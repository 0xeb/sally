// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Wide Path Support for Long Paths (>MAX_PATH)
//
//****************************************************************************

#pragma once

#include <windows.h>

// Convenience wrappers keep path ownership UTF-16 and dynamically sized.
// Long-path decoration belongs to the filesystem adapter implementation.

HANDLE SalLPCreateFile(
    const wchar_t* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile);

// All SalLP path inputs are UTF-16. Long-path preparation remains adapter-owned.
DWORD SalLPGetFileAttributes(const wchar_t* fileName);

BOOL SalLPSetFileAttributes(const wchar_t* fileName, DWORD dwFileAttributes);
BOOL SalLPDeleteFile(const wchar_t* fileName);
BOOL SalLPRemoveDirectory(const wchar_t* dirName);
BOOL SalLPCreateDirectory(const wchar_t* pathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
BOOL SalLPMoveFile(const wchar_t* existingFileName, const wchar_t* newFileName);
BOOL SalLPCopyFile(const wchar_t* existingFileName, const wchar_t* newFileName, BOOL failIfExists);

HANDLE SalLPFindFirstFile(const wchar_t* fileName, WIN32_FIND_DATAW* findData);
BOOL SalLPFindNextFile(HANDLE hFindFile, WIN32_FIND_DATAW* findData);

// Close a handle returned by SalLPFindFirstFile* through the same active
// filesystem adapter. Retains BOOL only for this transitional wrapper surface.
BOOL SalLPFindClose(HANDLE hFindFile);

#ifdef HANDLES_ENABLE

#define SalCreateFileH(fileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile) \
    SalLPCreateFileTracked(fileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile, __FILE__, __LINE__)

HANDLE SalLPCreateFileTracked(
    const wchar_t* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile,
    const char* srcFile,
    int srcLine);

#define SalFindFirstFileHW(fileName, findData) SalLPFindFirstFileTrackedW(fileName, findData, __FILE__, __LINE__)

HANDLE SalLPFindFirstFileTrackedW(
    const wchar_t* fileName,
    WIN32_FIND_DATAW* findData,
    const char* srcFile,
    int srcLine);

#else // !HANDLES_ENABLE

#define SalCreateFileH SalLPCreateFile
#define SalFindFirstFileHW SalLPFindFirstFile

#endif // HANDLES_ENABLE
