// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//
// fsutil standalone implementation for tests (no precomp.h dependency)
//

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <string>

// Define constants if not already defined
#ifndef SAL_LONG_PATH_THRESHOLD
#define SAL_LONG_PATH_THRESHOLD 240
#endif

#ifndef SAL_MAX_LONG_PATH
#define SAL_MAX_LONG_PATH 32767
#endif

#include "../common/fsutil.h"

SalFileInfo GetFileInfoW(const wchar_t* fullPath)
{
    SalFileInfo info = {};
    info.IsValid = FALSE;
    info.LastError = ERROR_SUCCESS;

    if (fullPath == NULL || fullPath[0] == L'\0')
    {
        info.LastError = ERROR_INVALID_PARAMETER;
        return info;
    }

    // Check if we need to add long path prefix
    size_t pathLen = wcslen(fullPath);
    const wchar_t* pathToUse = fullPath;
    wchar_t* prefixedPath = NULL;

    if (pathLen >= SAL_LONG_PATH_THRESHOLD &&
        !(fullPath[0] == L'\\' && fullPath[1] == L'\\' && fullPath[2] == L'?' && fullPath[3] == L'\\'))
    {
        // Need to add prefix
        BOOL isUNC = (fullPath[0] == L'\\' && fullPath[1] == L'\\');
        size_t prefixLen = isUNC ? 8 : 4; // Prefix length
        size_t skipLen = isUNC ? 2 : 0;   // Skip leading backslashes for UNC

        prefixedPath = (wchar_t*)malloc((prefixLen + pathLen - skipLen + 1) * sizeof(wchar_t));
        if (prefixedPath == NULL)
        {
            info.LastError = ERROR_NOT_ENOUGH_MEMORY;
            return info;
        }

        if (isUNC)
        {
            wcscpy(prefixedPath, L"\\\\?\\UNC\\");
            wcscpy(prefixedPath + 8, fullPath + 2);
        }
        else
        {
            wcscpy(prefixedPath, L"\\\\?\\");
            wcscpy(prefixedPath + 4, fullPath);
        }
        pathToUse = prefixedPath;
    }

    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(pathToUse, &findData);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        info.IsValid = TRUE;
        info.Attributes = findData.dwFileAttributes;
        info.CreationTime = findData.ftCreationTime;
        info.LastAccessTime = findData.ftLastAccessTime;
        info.LastWriteTime = findData.ftLastWriteTime;
        info.FileSize = ((ULONGLONG)findData.nFileSizeHigh << 32) | findData.nFileSizeLow;
        info.FileName = findData.cFileName;
        if (findData.cAlternateFileName[0] != L'\0')
            info.AlternateName = findData.cAlternateFileName;
        FindClose(hFind);
    }
    else
    {
        info.LastError = GetLastError();
    }

    if (prefixedPath != NULL)
        free(prefixedPath);

    return info;
}

std::wstring BuildPathW(const wchar_t* directory, const wchar_t* fileName)
{
    if (directory == NULL)
        return fileName ? std::wstring(fileName) : std::wstring();
    if (fileName == NULL)
        return std::wstring(directory);

    std::wstring result(directory);

    // Add backslash if needed
    if (!result.empty() && result.back() != L'\\')
        result += L'\\';

    result += fileName;
    return result;
}

std::wstring BuildPathW(const char* directory, const char* fileName)
{
    std::wstring dirW, nameW;

    // Convert directory to wide
    if (directory != NULL && directory[0] != '\0')
    {
        int dirLen = MultiByteToWideChar(CP_ACP, 0, directory, -1, NULL, 0);
        if (dirLen > 0)
        {
            dirW.resize(dirLen - 1);
            MultiByteToWideChar(CP_ACP, 0, directory, -1, dirW.data(), dirLen);
        }
    }

    // Convert filename to wide
    if (fileName != NULL && fileName[0] != '\0')
    {
        int nameLen = MultiByteToWideChar(CP_ACP, 0, fileName, -1, NULL, 0);
        if (nameLen > 0)
        {
            nameW.resize(nameLen - 1);
            MultiByteToWideChar(CP_ACP, 0, fileName, -1, nameW.data(), nameLen);
        }
    }

    return BuildPathW(dirW.c_str(), nameW.c_str());
}

BOOL PathExistsW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return FALSE;

    SalFileInfo info = GetFileInfoW(path);
    return info.IsValid;
}

BOOL IsDirectoryW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return FALSE;

    SalFileInfo info = GetFileInfoW(path);
    return info.IsValid && (info.Attributes & FILE_ATTRIBUTE_DIRECTORY);
}
