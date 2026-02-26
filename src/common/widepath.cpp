// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
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

#include "IFileSystem.h"
#include "widepath.h"

//
// Internal helper: Check if path is UNC (starts with \\)
//
static BOOL IsUNCPathLocal(const char* path)
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
    BOOL isUNC = IsUNCPathLocal(ansiPath);

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
// SalAnsiName class implementation
//

SalAnsiName::SalAnsiName(const wchar_t* wideName)
    : m_ansiName(NULL), m_wideName(NULL), m_ansiLen(0), m_wideLen(0), m_isLossy(FALSE)
{
    if (wideName == NULL)
        return;

    // Get wide name length
    m_wideLen = (int)wcslen(wideName);

    // Allocate and copy wide name
    m_wideName = (wchar_t*)malloc((m_wideLen + 1) * sizeof(wchar_t));
    if (m_wideName == NULL)
        return;
    wcscpy(m_wideName, wideName);

    // Convert wide to ANSI with lossy detection
    // Use WC_NO_BEST_FIT_CHARS to ensure exact conversion detection
    BOOL usedDefaultChar = FALSE;
    int ansiSize = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wideName, -1,
                                       NULL, 0, NULL, &usedDefaultChar);
    if (ansiSize == 0)
    {
        // Conversion failed, try without the flag
        ansiSize = WideCharToMultiByte(CP_ACP, 0, wideName, -1, NULL, 0, NULL, NULL);
        if (ansiSize == 0)
            return;
        m_isLossy = TRUE; // Assume lossy if we couldn't check properly
    }
    else
    {
        m_isLossy = usedDefaultChar;
    }

    // Allocate ANSI buffer
    m_ansiName = (char*)malloc(ansiSize);
    if (m_ansiName == NULL)
        return;

    // Do the actual conversion
    usedDefaultChar = FALSE;
    int converted = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wideName, -1,
                                        m_ansiName, ansiSize, NULL, &usedDefaultChar);
    if (converted == 0)
    {
        // Try without the flag
        converted = WideCharToMultiByte(CP_ACP, 0, wideName, -1, m_ansiName, ansiSize, NULL, NULL);
        if (converted == 0)
        {
            free(m_ansiName);
            m_ansiName = NULL;
            return;
        }
        m_isLossy = TRUE;
    }
    else
    {
        m_isLossy = m_isLossy || usedDefaultChar;
    }

    m_ansiLen = (int)strlen(m_ansiName);
}

SalAnsiName::~SalAnsiName()
{
    if (m_ansiName != NULL)
        free(m_ansiName);
    if (m_wideName != NULL)
        free(m_wideName);
}

char* SalAnsiName::AllocAnsiName() const
{
    if (m_ansiName == NULL)
        return NULL;

    char* copy = (char*)malloc(m_ansiLen + 1);
    if (copy != NULL)
        memcpy(copy, m_ansiName, m_ansiLen + 1);
    return copy;
}

wchar_t* SalAnsiName::AllocWideName() const
{
    if (m_wideName == NULL)
        return NULL;

    wchar_t* copy = (wchar_t*)malloc((m_wideLen + 1) * sizeof(wchar_t));
    if (copy != NULL)
        memcpy(copy, m_wideName, (m_wideLen + 1) * sizeof(wchar_t));
    return copy;
}

//
// CPathBuffer class implementation is now inline in widepath.h
//

//
// CWidePathBuffer class implementation
//

CWidePathBuffer::CWidePathBuffer()
    : m_buffer(m_inline), m_capacity(SAL_WIDE_PATH_BUFFER_INITIAL_CAPACITY)
{
    m_inline[0] = L'\0';
}

CWidePathBuffer::CWidePathBuffer(const wchar_t* initialPath)
    : m_buffer(m_inline), m_capacity(SAL_WIDE_PATH_BUFFER_INITIAL_CAPACITY)
{
    m_inline[0] = L'\0';
    Assign(initialPath);
}

CWidePathBuffer::~CWidePathBuffer()
{
    if (m_buffer != NULL && m_buffer != m_inline)
        free(m_buffer);
    m_buffer = NULL;
    m_capacity = 0;
}

BOOL CWidePathBuffer::EnsureCapacity(int requiredChars)
{
    if (requiredChars <= 0)
        requiredChars = 1;
    if (requiredChars <= m_capacity)
        return TRUE;
    if (requiredChars > SAL_MAX_LONG_PATH)
        return FALSE;

    int newCapacity = m_capacity;
    while (newCapacity < requiredChars)
    {
        if (newCapacity >= SAL_MAX_LONG_PATH / 2)
        {
            newCapacity = SAL_MAX_LONG_PATH;
            break;
        }
        newCapacity *= 2;
    }
    if (newCapacity < requiredChars)
        newCapacity = requiredChars;
    if (newCapacity > SAL_MAX_LONG_PATH)
        newCapacity = SAL_MAX_LONG_PATH;
    if (newCapacity < requiredChars)
        return FALSE;

    wchar_t* newBuffer = (wchar_t*)malloc((size_t)newCapacity * sizeof(wchar_t));
    if (newBuffer == NULL)
        return FALSE;

    wcsncpy(newBuffer, m_buffer, (size_t)newCapacity - 1);
    newBuffer[newCapacity - 1] = L'\0';

    if (m_buffer != m_inline)
        free(m_buffer);
    m_buffer = newBuffer;
    m_capacity = newCapacity;
    return TRUE;
}

void CWidePathBuffer::Clear()
{
    if (m_buffer != NULL)
        m_buffer[0] = L'\0';
}

BOOL CWidePathBuffer::Assign(const wchar_t* text)
{
    if (text == NULL)
    {
        Clear();
        return TRUE;
    }

    size_t len = wcslen(text);
    if (!EnsureCapacity((int)len + 1))
        return FALSE;

    memcpy(m_buffer, text, (len + 1) * sizeof(wchar_t));
    return TRUE;
}

BOOL CWidePathBuffer::Append(const wchar_t* name)
{
    if (m_buffer == NULL || name == NULL)
        return FALSE;

    size_t currentLen = wcslen(m_buffer);
    size_t nameLen = wcslen(name);

    // Add backslash if path doesn't end with one and isn't empty
    BOOL needsBackslash = (currentLen > 0 && m_buffer[currentLen - 1] != L'\\');
    size_t totalLen = currentLen + (needsBackslash ? 1 : 0) + nameLen;

    if (!EnsureCapacity((int)totalLen + 1))
        return FALSE; // Would overflow

    if (needsBackslash)
    {
        m_buffer[currentLen] = L'\\';
        currentLen++;
    }

    wcscpy(m_buffer + currentLen, name);
    return TRUE;
}

BOOL CWidePathBuffer::Append(const char* name)
{
    if (m_buffer == NULL || name == NULL)
        return FALSE;

    // Convert ANSI to wide
    int wideLen = MultiByteToWideChar(CP_ACP, 0, name, -1, NULL, 0);
    if (wideLen <= 0)
        return FALSE;

    // Allocate temp buffer for converted name
    wchar_t* wideName = (wchar_t*)malloc(wideLen * sizeof(wchar_t));
    if (wideName == NULL)
        return FALSE;

    if (MultiByteToWideChar(CP_ACP, 0, name, -1, wideName, wideLen) == 0)
    {
        free(wideName);
        return FALSE;
    }

    // Use the wide version of Append
    BOOL result = Append(wideName);
    free(wideName);
    return result;
}

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
    const char* fileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile)
{
    IFileSystem* fs = GetActiveFileSystem();
    if (fs != NULL)
        return CreateFileA(fs, fileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                           dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);

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
    IFileSystem* fs = GetActiveFileSystem();
    if (fs != NULL)
        return GetFileAttributesA(fs, fileName);

    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return INVALID_FILE_ATTRIBUTES;
    }

    return GetFileAttributesW(widePath.Get());
}

BOOL SalLPSetFileAttributes(const char* fileName, DWORD dwFileAttributes)
{
    IFileSystem* fs = GetActiveFileSystem();
    if (fs != NULL)
        return ResultToBool(SetFileAttributesA(fs, fileName, dwFileAttributes));

    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return SetFileAttributesW(widePath.Get(), dwFileAttributes);
}

BOOL SalLPDeleteFile(const char* fileName)
{
    IFileSystem* fs = GetActiveFileSystem();
    if (fs != NULL)
        return ResultToBool(DeleteFileA(fs, fileName));

    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return DeleteFileW(widePath.Get());
}

BOOL SalLPRemoveDirectory(const char* dirName)
{
    IFileSystem* fs = GetActiveFileSystem();
    if (fs != NULL)
        return ResultToBool(RemoveDirectoryA(fs, dirName));

    SalWidePath widePath(dirName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return RemoveDirectoryW(widePath.Get());
}

BOOL SalLPCreateDirectory(const char* pathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    IFileSystem* fs = GetActiveFileSystem();
    if (fs != NULL && lpSecurityAttributes == NULL)
        return ResultToBool(CreateDirectoryA(fs, pathName));

    SalWidePath widePath(pathName);
    if (!widePath.IsValid())
    {
        return FALSE;
    }

    return CreateDirectoryW(widePath.Get(), lpSecurityAttributes);
}

BOOL SalLPMoveFile(const char* existingFileName, const char* newFileName)
{
    IFileSystem* fs = GetActiveFileSystem();
    if (fs != NULL)
        return ResultToBool(MoveFileA(fs, existingFileName, newFileName));

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
    IFileSystem* fs = GetActiveFileSystem();
    if (fs != NULL)
        return ResultToBool(CopyFileA(fs, existingFileName, newFileName, failIfExists != FALSE));

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
    IFileSystem* fs = GetActiveFileSystem();
    if (fs != NULL)
        return FindFirstFilePathA(fs, fileName, findData);

    SalWidePath widePath(fileName);
    if (!widePath.IsValid())
    {
        return INVALID_HANDLE_VALUE;
    }

    return FindFirstFileW(widePath.Get(), findData);
}

BOOL SalLPFindNextFile(HANDLE hFindFile, WIN32_FIND_DATAW* findData)
{
    IFileSystem* fs = GetActiveFileSystem();
    if (fs != NULL)
        return fs->FindNextFile(hFindFile, findData);

    return FindNextFileW(hFindFile, findData);
}

HANDLE SalLPFindFirstFileA(const char* fileName, WIN32_FIND_DATAA* findData)
{
    WIN32_FIND_DATAW findDataW;
    HANDLE h = SalLPFindFirstFile(fileName, &findDataW);
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

BOOL SalLPFindNextFileA(HANDLE hFindFile, WIN32_FIND_DATAA* findData)
{
    WIN32_FIND_DATAW findDataW;
    BOOL result = SalLPFindNextFile(hFindFile, &findDataW);
    if (result && findData != NULL)
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
    return result;
}

//
// Handle-tracking variant (debug builds only)
//

#ifdef HANDLES_ENABLE

#include "handles.h"

HANDLE SalLPCreateFileTracked(
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
    HANDLE h = SalLPCreateFile(fileName, dwDesiredAccess, dwShareMode,
                               lpSecurityAttributes, dwCreationDisposition,
                               dwFlagsAndAttributes, hTemplateFile);

    // Track the handle using Salamander's handle tracking system
    DWORD err = GetLastError();
    __Handles.SetInfo(srcFile, srcLine, __otQuiet)
        .CheckCreate(h != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, h, err, TRUE);

    return h;
}

HANDLE SalLPFindFirstFileTracked(
    const char* fileName,
    WIN32_FIND_DATAA* findData,
    const char* srcFile,
    int srcLine)
{
    HANDLE h = SalLPFindFirstFileA(fileName, findData);

    // Track the handle using Salamander's handle tracking system
    DWORD err = GetLastError();
    __Handles.SetInfo(srcFile, srcLine, __otQuiet)
        .CheckCreate(h != INVALID_HANDLE_VALUE, __htFindFile, __hoFindFirstFile, h, err, TRUE);

    return h;
}

HANDLE SalLPFindFirstFileTrackedW(
    const char* fileName,
    WIN32_FIND_DATAW* findData,
    const char* srcFile,
    int srcLine)
{
    HANDLE h = SalLPFindFirstFile(fileName, findData);

    // Track the handle using Salamander's handle tracking system
    DWORD err = GetLastError();
    __Handles.SetInfo(srcFile, srcLine, __otQuiet)
        .CheckCreate(h != INVALID_HANDLE_VALUE, __htFindFile, __hoFindFirstFile, h, err, TRUE);

    return h;
}

#endif // HANDLES_ENABLE
