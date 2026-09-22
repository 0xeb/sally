// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "IEnvironment.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace
{
bool ResizeBuffer(std::wstring& buffer, DWORD capacity)
{
    try
    {
        buffer.resize(static_cast<size_t>(capacity));
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}

bool GrowCapacity(DWORD current, DWORD suggested, DWORD& capacity)
{
    unsigned long long candidate = suggested;
    if (candidate <= current)
        candidate = static_cast<unsigned long long>(current) * 2;
    if (candidate <= current || candidate > (std::numeric_limits<DWORD>::max)())
        return false;
    capacity = static_cast<DWORD>(candidate);
    return true;
}

EnvResult GrowthFailure()
{
    return EnvResult::Error(ERROR_NOT_ENOUGH_MEMORY);
}
} // namespace

class Win32Environment : public IEnvironment
{
public:
    EnvResult GetVariable(const wchar_t* name, std::wstring& value) override
    {
        if (!name)
            return EnvResult::Error(ERROR_INVALID_PARAMETER);

        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return GrowthFailure();

            SetLastError(ERROR_SUCCESS);
            const DWORD written = ::GetEnvironmentVariableW(name, buffer.data(), capacity);
            if (written == 0)
            {
                const DWORD error = GetLastError();
                if (error != ERROR_SUCCESS)
                    return EnvResult::Error(error);
                buffer.clear();
                value.swap(buffer);
                return EnvResult::Ok();
            }
            if (written < capacity)
            {
                buffer.resize(written);
                value.swap(buffer);
                return EnvResult::Ok();
            }

            if (!GrowCapacity(capacity, written, capacity))
                return GrowthFailure();
        }
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
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return GrowthFailure();
            const DWORD len = ::GetTempPathW(capacity, buffer.data());
            if (len == 0)
                return EnvResult::Error(GetLastError());
            if (len < capacity)
            {
                buffer.resize(len);
                path.swap(buffer);
                return EnvResult::Ok();
            }
            if (!GrowCapacity(capacity, len, capacity))
                return GrowthFailure();
        }
    }

    EnvResult GetSystemDirectory(std::wstring& path) override
    {
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return GrowthFailure();
            const UINT len = ::GetSystemDirectoryW(buffer.data(), capacity);
            if (len == 0)
                return EnvResult::Error(GetLastError());
            if (len < capacity)
            {
                buffer.resize(len);
                path.swap(buffer);
                return EnvResult::Ok();
            }
            if (!GrowCapacity(capacity, len, capacity))
                return GrowthFailure();
        }
    }

    EnvResult GetWindowsDirectory(std::wstring& path) override
    {
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return GrowthFailure();
            const UINT len = ::GetWindowsDirectoryW(buffer.data(), capacity);
            if (len == 0)
                return EnvResult::Error(GetLastError());
            if (len < capacity)
            {
                buffer.resize(len);
                path.swap(buffer);
                return EnvResult::Ok();
            }
            if (!GrowCapacity(capacity, len, capacity))
                return GrowthFailure();
        }
    }

    EnvResult GetCurrentDirectory(std::wstring& path) override
    {
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return GrowthFailure();
            const DWORD len = ::GetCurrentDirectoryW(capacity, buffer.data());
            if (len == 0)
                return EnvResult::Error(GetLastError());
            if (len < capacity)
            {
                buffer.resize(len);
                path.swap(buffer);
                return EnvResult::Ok();
            }
            if (!GrowCapacity(capacity, len, capacity))
                return GrowthFailure();
        }
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

        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return GrowthFailure();
            const DWORD written = ::ExpandEnvironmentStringsW(source, buffer.data(), capacity);
            if (written == 0)
                return EnvResult::Error(GetLastError());
            if (written <= capacity)
            {
                buffer.resize(written - 1);
                expanded.swap(buffer);
                return EnvResult::Ok();
            }
            if (!GrowCapacity(capacity, written, capacity))
                return GrowthFailure();
        }
    }

    EnvResult GetComputerName(std::wstring& name) override
    {
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return GrowthFailure();
            DWORD size = capacity;
            if (::GetComputerNameW(buffer.data(), &size))
            {
                buffer.resize(size);
                name.swap(buffer);
                return EnvResult::Ok();
            }
            const DWORD error = GetLastError();
            if (error != ERROR_BUFFER_OVERFLOW)
                return EnvResult::Error(error);
            if (!GrowCapacity(capacity, size, capacity))
                return GrowthFailure();
        }
    }

    EnvResult GetUserName(std::wstring& name) override
    {
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer;
            if (!ResizeBuffer(buffer, capacity))
                return GrowthFailure();
            DWORD size = capacity;
            if (::GetUserNameW(buffer.data(), &size))
            {
                buffer.resize(size > 0 ? size - 1 : 0);
                name.swap(buffer);
                return EnvResult::Ok();
            }
            const DWORD error = GetLastError();
            if (error != ERROR_INSUFFICIENT_BUFFER)
                return EnvResult::Error(error);
            if (!GrowCapacity(capacity, size, capacity))
                return GrowthFailure();
        }
    }
};

// Global instance
static Win32Environment g_win32Environment;
IEnvironment* gEnvironment = &g_win32Environment;

IEnvironment* GetWin32Environment()
{
    return &g_win32Environment;
}
