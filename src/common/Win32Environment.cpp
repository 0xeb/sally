// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include "IEnvironment.h"
#include <stdlib.h>
#include <lmcons.h>  // For UNLEN

class Win32Environment : public IEnvironment
{
public:
    EnvResult GetVariable(const wchar_t* name, std::wstring& value) override
    {
        if (!name)
            return EnvResult::Error(ERROR_INVALID_PARAMETER);

        // First call to get size
        DWORD size = ::GetEnvironmentVariableW(name, nullptr, 0);
        if (size == 0)
        {
            DWORD err = GetLastError();
            return EnvResult::Error(err);
        }

        // Allocate and read
        value.resize(size);
        DWORD written = ::GetEnvironmentVariableW(name, &value[0], size);
        if (written == 0)
            return EnvResult::Error(GetLastError());

        value.resize(written);  // Remove null terminator from length
        return EnvResult::Ok();
    }

    EnvResult SetVariable(const wchar_t* name, const wchar_t* value) override
    {
        if (!name)
            return EnvResult::Error(ERROR_INVALID_PARAMETER);

        // If value is null, delete the variable
        if (!::SetEnvironmentVariableW(name, value))
            return EnvResult::Error(GetLastError());

        return EnvResult::Ok();
    }

    EnvResult GetTempPath(std::wstring& path) override
    {
        wchar_t buffer[MAX_PATH + 1];
        DWORD len = ::GetTempPathW(MAX_PATH + 1, buffer);
        if (len == 0)
            return EnvResult::Error(GetLastError());

        // GetTempPath can return > MAX_PATH, handle that case
        if (len > MAX_PATH)
        {
            path.resize(len);
            len = ::GetTempPathW(len, &path[0]);
            if (len == 0)
                return EnvResult::Error(GetLastError());
            path.resize(len);
        }
        else
        {
            path = buffer;
        }

        return EnvResult::Ok();
    }

    EnvResult GetSystemDirectory(std::wstring& path) override
    {
        wchar_t buffer[MAX_PATH];
        UINT len = ::GetSystemDirectoryW(buffer, MAX_PATH);
        if (len == 0)
            return EnvResult::Error(GetLastError());

        if (len >= MAX_PATH)
        {
            path.resize(len + 1);
            len = ::GetSystemDirectoryW(&path[0], len + 1);
            if (len == 0)
                return EnvResult::Error(GetLastError());
            path.resize(len);
        }
        else
        {
            path = buffer;
        }

        return EnvResult::Ok();
    }

    EnvResult GetWindowsDirectory(std::wstring& path) override
    {
        wchar_t buffer[MAX_PATH];
        UINT len = ::GetWindowsDirectoryW(buffer, MAX_PATH);
        if (len == 0)
            return EnvResult::Error(GetLastError());

        if (len >= MAX_PATH)
        {
            path.resize(len + 1);
            len = ::GetWindowsDirectoryW(&path[0], len + 1);
            if (len == 0)
                return EnvResult::Error(GetLastError());
            path.resize(len);
        }
        else
        {
            path = buffer;
        }

        return EnvResult::Ok();
    }

    EnvResult GetCurrentDirectory(std::wstring& path) override
    {
        DWORD len = ::GetCurrentDirectoryW(0, nullptr);
        if (len == 0)
            return EnvResult::Error(GetLastError());

        path.resize(len);
        len = ::GetCurrentDirectoryW(len, &path[0]);
        if (len == 0)
            return EnvResult::Error(GetLastError());

        path.resize(len);  // Remove null terminator from length
        return EnvResult::Ok();
    }

    EnvResult SetCurrentDirectory(const wchar_t* path) override
    {
        if (!path)
            return EnvResult::Error(ERROR_INVALID_PARAMETER);

        if (!::SetCurrentDirectoryW(path))
            return EnvResult::Error(GetLastError());

        return EnvResult::Ok();
    }

    EnvResult ExpandEnvironmentStrings(const wchar_t* source, std::wstring& expanded) override
    {
        if (!source)
            return EnvResult::Error(ERROR_INVALID_PARAMETER);

        // First call to get size
        DWORD size = ::ExpandEnvironmentStringsW(source, nullptr, 0);
        if (size == 0)
            return EnvResult::Error(GetLastError());

        // Allocate and expand
        expanded.resize(size);
        DWORD written = ::ExpandEnvironmentStringsW(source, &expanded[0], size);
        if (written == 0)
            return EnvResult::Error(GetLastError());

        expanded.resize(written - 1);  // Remove null terminator from length
        return EnvResult::Ok();
    }

    EnvResult GetComputerName(std::wstring& name) override
    {
        wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = MAX_COMPUTERNAME_LENGTH + 1;

        if (!::GetComputerNameW(buffer, &size))
            return EnvResult::Error(GetLastError());

        name = buffer;
        return EnvResult::Ok();
    }

    EnvResult GetUserName(std::wstring& name) override
    {
        wchar_t buffer[UNLEN + 1];
        DWORD size = UNLEN + 1;

        if (!::GetUserNameW(buffer, &size))
            return EnvResult::Error(GetLastError());

        name = buffer;
        return EnvResult::Ok();
    }
};

// Global instance
static Win32Environment g_win32Environment;
IEnvironment* gEnvironment = &g_win32Environment;

IEnvironment* GetWin32Environment()
{
    return &g_win32Environment;
}
