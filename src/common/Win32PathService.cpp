// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "IPathService.h"
#include "widepath.h"

#include <algorithm>

static bool HasLongPrefix(const wchar_t* path)
{
    if (path == NULL)
        return false;
    size_t len = wcslen(path);
    return len >= 4 && wcsncmp(path, L"\\\\?\\", 4) == 0;
}

static bool IsUNCPath(const wchar_t* path)
{
    return path != NULL &&
           path[0] == L'\\' && path[1] == L'\\' &&
           !HasLongPrefix(path);
}

static wchar_t LastMeaningfulPathChar(const std::wstring& path)
{
    for (size_t i = path.size(); i > 0; --i)
    {
        wchar_t ch = path[i - 1];
        // Skip wildcard and trailing separators when determining Win32 trailing-char quirks.
        if (ch == L'*' || ch == L'?' || ch == L'\\')
            continue;
        return ch;
    }
    return L'\0';
}

static DWORD NextCapacity(DWORD current, DWORD suggested)
{
    DWORD next = suggested;
    if (next <= current)
    {
        if (current >= SAL_MAX_LONG_PATH / 2)
            next = SAL_MAX_LONG_PATH;
        else
            next = current * 2;
    }
    if (next > SAL_MAX_LONG_PATH)
        next = SAL_MAX_LONG_PATH;
    return next;
}

class Win32PathService : public IPathService
{
public:
    PathResult ToLongPath(const wchar_t* path, std::wstring& outPath) override
    {
        if (path == NULL)
            return PathResult::Error(ERROR_INVALID_PARAMETER);

        outPath.assign(path);
        if (outPath.empty())
            return PathResult::Ok();

        if (HasLongPrefix(path))
        {
            if (outPath.size() > SAL_MAX_LONG_PATH - 1)
                return PathResult::Error(ERROR_FILENAME_EXCED_RANGE);
            return PathResult::Ok();
        }

        bool needsPrefix = (outPath.size() >= SAL_LONG_PATH_THRESHOLD);
        if (!needsPrefix)
        {
            wchar_t last = LastMeaningfulPathChar(outPath);
            if (last == L' ' || last == L'.')
                needsPrefix = true;
        }

        if (!needsPrefix)
            return PathResult::Ok();

        if (IsUNCPath(path))
            outPath = L"\\\\?\\UNC\\" + outPath.substr(2);
        else
            outPath = L"\\\\?\\" + outPath;

        if (outPath.size() > SAL_MAX_LONG_PATH - 1)
            return PathResult::Error(ERROR_FILENAME_EXCED_RANGE);
        return PathResult::Ok();
    }

    PathResult GetCurrentDirectory(std::wstring& outPath) override
    {
        DWORD capacity = MAX_PATH;
        for (;;)
        {
            std::wstring buffer;
            buffer.resize(capacity);

            DWORD len = ::GetCurrentDirectoryW(capacity, &buffer[0]);
            if (len == 0)
                return PathResult::Error(GetLastError());

            if (len < capacity)
            {
                buffer.resize(len);
                outPath.swap(buffer);
                return PathResult::Ok();
            }

            DWORD next = NextCapacity(capacity, len + 1);
            if (next <= capacity)
                return PathResult::Error(ERROR_FILENAME_EXCED_RANGE);
            capacity = next;
        }
    }

    PathResult GetModuleFileName(HMODULE module, std::wstring& outPath) override
    {
        DWORD capacity = MAX_PATH;
        for (;;)
        {
            std::wstring buffer;
            buffer.resize(capacity);

            SetLastError(ERROR_SUCCESS);
            DWORD len = ::GetModuleFileNameW(module, &buffer[0], capacity);
            if (len == 0)
                return PathResult::Error(GetLastError());

            DWORD err = GetLastError();
            bool truncated = (len >= capacity) || (len == capacity - 1 && err == ERROR_INSUFFICIENT_BUFFER);
            if (!truncated)
            {
                buffer.resize(len);
                outPath.swap(buffer);
                return PathResult::Ok();
            }

            DWORD next = NextCapacity(capacity, capacity + 1);
            if (next <= capacity)
                return PathResult::Error(ERROR_FILENAME_EXCED_RANGE);
            capacity = next;
        }
    }

    PathResult GetTempPath(std::wstring& outPath) override
    {
        DWORD capacity = MAX_PATH;
        for (;;)
        {
            std::wstring buffer;
            buffer.resize(capacity);

            DWORD len = ::GetTempPathW(capacity, &buffer[0]);
            if (len == 0)
                return PathResult::Error(GetLastError());

            if (len < capacity)
            {
                buffer.resize(len);
                outPath.swap(buffer);
                return PathResult::Ok();
            }

            DWORD next = NextCapacity(capacity, len + 1);
            if (next <= capacity)
                return PathResult::Error(ERROR_FILENAME_EXCED_RANGE);
            capacity = next;
        }
    }

    PathResult GetFullPathName(const wchar_t* inputPath, std::wstring& outPath) override
    {
        if (inputPath == NULL || inputPath[0] == L'\0')
            return PathResult::Error(ERROR_INVALID_PARAMETER);

        DWORD capacity = MAX_PATH;
        for (;;)
        {
            std::wstring buffer;
            buffer.resize(capacity);

            DWORD len = ::GetFullPathNameW(inputPath, capacity, &buffer[0], NULL);
            if (len == 0)
                return PathResult::Error(GetLastError());

            if (len < capacity)
            {
                buffer.resize(len);
                outPath.swap(buffer);
                return PathResult::Ok();
            }

            DWORD next = NextCapacity(capacity, len + 1);
            if (next <= capacity)
                return PathResult::Error(ERROR_FILENAME_EXCED_RANGE);
            capacity = next;
        }
    }
};

static Win32PathService g_win32PathService;
IPathService* gPathService = &g_win32PathService;

IPathService* GetWin32PathService()
{
    return &g_win32PathService;
}
