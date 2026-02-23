// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include "IFileEnumerator.h"
#include "IPathService.h"
#include <string>
#include <stdlib.h>

// Internal enumeration state
struct EnumState
{
    HANDLE hFind;
    WIN32_FIND_DATAW findData;
    bool firstRead;  // First entry already read from FindFirstFileW
};

class Win32FileEnumerator : public IFileEnumerator
{
public:
    HENUM StartEnum(const wchar_t* path, const wchar_t* pattern) override
    {
        if (!path)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_HENUM;
        }

        // Build search path
        std::wstring searchPath = path;

        // If path doesn't end with pattern, add one
        if (!HasPattern(path))
        {
            // Ensure path ends with backslash
            if (!searchPath.empty() && searchPath.back() != L'\\')
                searchPath += L'\\';

            // Add pattern or default wildcard
            if (pattern && *pattern)
                searchPath += pattern;
            else
                searchPath += L'*';
        }

        if (gPathService == nullptr)
            gPathService = GetWin32PathService();
        if (gPathService == nullptr)
        {
            SetLastError(ERROR_INVALID_FUNCTION);
            return INVALID_HENUM;
        }

        std::wstring longPath;
        PathResult pathRes = gPathService->ToLongPath(searchPath.c_str(), longPath);
        if (!pathRes.success)
        {
            SetLastError(pathRes.errorCode);
            return INVALID_HENUM;
        }

        // Allocate state
        EnumState* state = (EnumState*)malloc(sizeof(EnumState));
        if (!state)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HENUM;
        }
        memset(state, 0, sizeof(EnumState));

        state->hFind = FindFirstFileW(longPath.c_str(), &state->findData);

        if (state->hFind == INVALID_HANDLE_VALUE)
        {
            DWORD err = GetLastError();
            free(state);
            SetLastError(err);
            return INVALID_HENUM;
        }

        state->firstRead = true;
        return static_cast<HENUM>(state);
    }

    EnumResult NextFile(HENUM handle, FileEnumEntry& entry) override
    {
        if (!handle)
            return EnumResult::Error(ERROR_INVALID_HANDLE);

        EnumState* state = static_cast<EnumState*>(handle);

        // First call returns data from FindFirstFileW
        if (!state->firstRead)
        {
            if (!FindNextFileW(state->hFind, &state->findData))
            {
                DWORD err = GetLastError();
                if (err == ERROR_NO_MORE_FILES)
                    return EnumResult::Done();
                return EnumResult::Error(err);
            }
        }
        state->firstRead = false;

        // Populate entry
        entry.name = state->findData.cFileName;
        entry.size = ((uint64_t)state->findData.nFileSizeHigh << 32) | state->findData.nFileSizeLow;
        entry.creationTime = state->findData.ftCreationTime;
        entry.lastAccessTime = state->findData.ftLastAccessTime;
        entry.lastWriteTime = state->findData.ftLastWriteTime;
        entry.attributes = state->findData.dwFileAttributes;

        return EnumResult::Ok();
    }

    void EndEnum(HENUM handle) override
    {
        if (!handle)
            return;

        EnumState* state = static_cast<EnumState*>(handle);
        if (state->hFind != INVALID_HANDLE_VALUE)
            FindClose(state->hFind);
        free(state);
    }
};

// Global instance
static Win32FileEnumerator g_win32FileEnumerator;
IFileEnumerator* gFileEnumerator = &g_win32FileEnumerator;

IFileEnumerator* GetWin32FileEnumerator()
{
    return &g_win32FileEnumerator;
}
