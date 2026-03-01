// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//
// fsutil standalone implementation for tests (no precomp.h dependency)
//

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <wctype.h>

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

std::wstring GetFileNameW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return std::wstring();

    const wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash == NULL)
        return std::wstring(path); // No backslash, return entire path

    return std::wstring(lastSlash + 1);
}

std::wstring GetDirectoryW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return std::wstring();

    const wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash == NULL)
        return std::wstring(); // No backslash, no directory part

    return std::wstring(path, lastSlash - path);
}

std::wstring GetExtensionW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return std::wstring();

    // Find the filename part first (after last backslash)
    const wchar_t* namePart = wcsrchr(path, L'\\');
    if (namePart == NULL)
        namePart = path;
    else
        namePart++;

    // Find the last dot in the filename
    const wchar_t* lastDot = wcsrchr(namePart, L'.');
    if (lastDot == NULL)
        return std::wstring(); // No dot, no extension

    // Return extension without the dot
    return std::wstring(lastDot + 1);
}

std::wstring GetShortPathW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return std::wstring();

    // First call to get required buffer size
    DWORD needed = GetShortPathNameW(path, NULL, 0);
    if (needed == 0)
        return std::wstring(); // Failed (file may not exist)

    std::wstring result(needed - 1, L'\0');
    DWORD written = GetShortPathNameW(path, result.data(), needed);
    if (written == 0 || written >= needed)
        return std::wstring(); // Failed

    return result;
}

std::wstring ExpandEnvironmentW(const wchar_t* input)
{
    if (input == NULL || input[0] == L'\0')
        return std::wstring();

    // First call to get required buffer size
    DWORD needed = ExpandEnvironmentStringsW(input, NULL, 0);
    if (needed == 0)
        return std::wstring(input); // Failed, return original

    std::wstring result(needed - 1, L'\0');
    DWORD written = ExpandEnvironmentStringsW(input, result.data(), needed);
    if (written == 0 || written > needed)
        return std::wstring(input); // Failed, return original

    return result;
}

void RemoveDoubleBackslashesW(std::wstring& path)
{
    if (path.empty())
        return;

    size_t writePos = 0;
    size_t readPos = 0;

    // Preserve UNC prefix (\\) or long path prefix (\\?\)
    if (path.length() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        writePos = 2;
        readPos = 2;

        // Check for \\?\ prefix
        if (path.length() >= 4 && path[2] == L'?' && path[3] == L'\\')
        {
            writePos = 4;
            readPos = 4;
        }
    }

    while (readPos < path.length())
    {
        path[writePos++] = path[readPos++];

        // Skip consecutive backslashes (keep only one)
        if (path[writePos - 1] == L'\\')
        {
            while (readPos < path.length() && path[readPos] == L'\\')
                readPos++;
        }
    }

    path.resize(writePos);
}

std::wstring GetRootPathW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return std::wstring();

    // UNC path: \\server\share
    if (path[0] == L'\\' && path[1] == L'\\')
    {
        const wchar_t* s = path + 2;
        // Skip server name
        while (*s != L'\0' && *s != L'\\')
            s++;
        if (*s != L'\0')
            s++; // skip backslash
        // Skip share name
        while (*s != L'\0' && *s != L'\\')
            s++;
        // Build root with trailing backslash
        std::wstring root(path, s - path);
        root += L'\\';
        return root;
    }
    else
    {
        // Local path: C:\...
        std::wstring root;
        root += path[0];
        root += L':';
        root += L'\\';
        return root;
    }
}

BOOL IsUNCRootPathW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return FALSE;

    // Must start with backslash-backslash
    if (path[0] != L'\\' || path[1] != L'\\')
        return FALSE;

    const wchar_t* s = path + 2;
    // Skip server name
    while (*s != L'\0' && *s != L'\\')
        s++;
    if (*s == L'\0')
        return TRUE; // \\server (no share yet)
    s++; // skip backslash
    // Skip share name
    while (*s != L'\0' && *s != L'\\')
        s++;
    // If we're at end or just trailing backslash, it's a root
    if (*s == L'\0')
        return TRUE;
    if (*s == L'\\' && *(s + 1) == L'\0')
        return TRUE;
    return FALSE;
}

BOOL IsUNCPathW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return FALSE;
    return (path[0] == L'\\' && path[1] == L'\\');
}

BOOL IsReservedNulBasenameW(const wchar_t* pathOrName)
{
    if (pathOrName == NULL || pathOrName[0] == L'\0')
        return FALSE;

    const wchar_t* baseName = pathOrName;
    const wchar_t* lastSlash = wcsrchr(pathOrName, L'\\');
    const wchar_t* lastAltSlash = wcsrchr(pathOrName, L'/');
    if (lastSlash != NULL && lastSlash + 1 > baseName)
        baseName = lastSlash + 1;
    if (lastAltSlash != NULL && lastAltSlash + 1 > baseName)
        baseName = lastAltSlash + 1;

    return _wcsicmp(baseName, L"nul") == 0;
}

BOOL IsReservedNulBasenameA(const char* pathOrName)
{
    if (pathOrName == NULL || pathOrName[0] == '\0')
        return FALSE;

    const char* baseName = pathOrName;
    const char* lastSlash = strrchr(pathOrName, '\\');
    const char* lastAltSlash = strrchr(pathOrName, '/');
    if (lastSlash != NULL && lastSlash + 1 > baseName)
        baseName = lastSlash + 1;
    if (lastAltSlash != NULL && lastAltSlash + 1 > baseName)
        baseName = lastAltSlash + 1;

    return _stricmp(baseName, "nul") == 0;
}

BOOL ShouldBypassRecycleBinForDeleteW(const wchar_t* pathOrName)
{
    return IsReservedNulBasenameW(pathOrName);
}

BOOL ShouldBypassRecycleBinForDeleteA(const char* pathOrName)
{
    return IsReservedNulBasenameA(pathOrName);
}

int ComputeDeleteRecycleMode(BOOL driveIsFixed,
                             int configuredUseRecycleBin,
                             BOOL invertRecycleBin,
                             BOOL bypassRecycleForEntry)
{
    int recycle = 0;
    if (driveIsFixed)
    {
        if (invertRecycleBin)
            recycle = (configuredUseRecycleBin == 0) ? 1 : 0;
        else
            recycle = configuredUseRecycleBin;
    }

    if (bypassRecycleForEntry)
        recycle = 0;
    return recycle;
}

BOOL HasTrailingBackslashW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return FALSE;
    size_t len = wcslen(path);
    return (path[len - 1] == L'\\');
}

void RemoveTrailingBackslashW(std::wstring& path)
{
    if (!path.empty() && path.back() == L'\\')
        path.pop_back();
}

void AddTrailingBackslashW(std::wstring& path)
{
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
}

void RemoveExtensionW(std::wstring& path)
{
    if (path.empty())
        return;

    // Find the filename part (after last backslash)
    size_t lastSlash = path.rfind(L'\\');
    size_t searchStart = (lastSlash == std::wstring::npos) ? 0 : lastSlash + 1;

    // Find the last dot in the filename part
    size_t lastDot = path.rfind(L'.');
    if (lastDot != std::wstring::npos && lastDot >= searchStart)
        path.resize(lastDot);
}

void SetExtensionW(std::wstring& path, const wchar_t* extension)
{
    if (path.empty())
        return;

    // Remove existing extension first
    RemoveExtensionW(path);

    // Add new extension
    if (extension != NULL && extension[0] != L'\0')
        path += extension;
}

std::wstring GetFileNameWithoutExtensionW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return std::wstring();

    // Get filename first
    std::wstring filename = GetFileNameW(path);

    // Remove extension
    RemoveExtensionW(filename);

    return filename;
}

std::wstring GetParentPathW(const wchar_t* path)
{
    if (path == NULL || path[0] == L'\0')
        return std::wstring();

    std::wstring p(path);

    // Remove trailing backslash if present (unless it's root)
    if (p.length() > 1 && p.back() == L'\\')
    {
        // Check if this is a root path (C:\ or \\server\share\)
        if (p.length() == 3 && p[1] == L':')
            return std::wstring(); // C:\ - can't go higher
        p.pop_back();
    }

    // Handle UNC paths
    if (p.length() >= 2 && p[0] == L'\\' && p[1] == L'\\')
    {
        // Count backslashes to determine if we're at UNC root
        int slashCount = 0;
        for (size_t i = 0; i < p.length(); i++)
            if (p[i] == L'\\') slashCount++;

        // \\server\share has 3 slashes, can't go higher
        if (slashCount <= 3)
            return std::wstring();
    }

    // Find last backslash
    size_t lastSlash = p.rfind(L'\\');
    if (lastSlash == std::wstring::npos)
        return std::wstring(); // No backslash

    // Check if this leaves us at root (C:\ or \\server\share)
    if (lastSlash == 2 && p[1] == L':')
        return p.substr(0, 3); // Return "C:\"

    return p.substr(0, lastSlash);
}

BOOL IsTheSamePathW(const wchar_t* path1, const wchar_t* path2)
{
    if (path1 == NULL || path2 == NULL)
        return (path1 == path2);

    // Skip leading backslash if present
    if (*path1 == L'\\')
        path1++;
    if (*path2 == L'\\')
        path2++;

    // Case-insensitive comparison
    while (*path1 != L'\0' && towlower(*path1) == towlower(*path2))
    {
        path1++;
        path2++;
    }

    // Skip trailing backslash if present
    if (*path1 == L'\\')
        path1++;
    if (*path2 == L'\\')
        path2++;

    return *path1 == L'\0' && *path2 == L'\0';
}

BOOL PathStartsWithW(const wchar_t* path, const wchar_t* prefix)
{
    if (path == NULL || prefix == NULL)
        return FALSE;
    if (*prefix == L'\0')
        return TRUE; // Empty prefix matches everything

    // Case-insensitive comparison
    while (*prefix != L'\0')
    {
        if (towlower(*path) != towlower(*prefix))
            return FALSE;
        path++;
        prefix++;
    }

    // Prefix matched. Check if we're at path boundary
    // (either end of path, or at a backslash)
    if (*path == L'\0' || *path == L'\\')
        return TRUE;

    // Also valid if prefix ended with backslash
    if (*(prefix - 1) == L'\\')
        return TRUE;

    return FALSE;
}
