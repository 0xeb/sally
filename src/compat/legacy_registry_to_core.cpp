// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_registry_to_core — frozen-v107 registry callback over native wide
// storage. This mixed-generation TU must never include precomp.h.

#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "compat/legacy_convert.h"
#include "compat/legacy_to_core.h"

namespace sally::compat
{
namespace
{

bool IsStringValue(DWORD type)
{
    return type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ;
}

bool WidenName(const char* name, std::wstring& storage,
               const wchar_t*& result)
{
    if (name == nullptr)
    {
        result = nullptr;
        return true;
    }
    if (!WidenPluginText(name, storage))
    {
        result = nullptr;
        return false;
    }
    result = storage.c_str();
    return true;
}

bool ReadStringValue(::CSalamanderRegistryAbstract& registry, HKEY key,
                     const wchar_t* name, DWORD type, std::string& narrow)
{
    DWORD wideBytes = 0;
    if (!registry.GetSize(key, name, type, wideBytes))
        return false;
    if (wideBytes % sizeof(wchar_t) != 0)
    {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }

    const size_t wideChars = wideBytes / sizeof(wchar_t);
    if (wideChars > static_cast<size_t>((std::numeric_limits<int>::max)()))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }

    std::vector<wchar_t> wide;
    try
    {
        wide.resize(wideChars);
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    void* wideBuffer = wide.empty() ? nullptr : wide.data();
    if (!registry.GetValue(key, name, type, wideBuffer, wideBytes))
        return false;

    if (wide.empty())
    {
        narrow.clear();
        return true;
    }

    std::wstring value;
    try
    {
        value.assign(wide.data(), wide.size());
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    NarrowResult result = NarrowExact(value);
    if (!result.ok)
        return false;
    narrow.swap(result.value);
    return true;
}

bool WidenStringValue(const void* data, DWORD dataSize,
                      std::vector<wchar_t>& wide)
{
    size_t narrowBytes = dataSize;
    if (dataSize == DWORD(-1))
    {
        if (data == nullptr)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }
        narrowBytes = std::strlen(static_cast<const char*>(data)) + 1;
    }

    if (narrowBytes == 0)
    {
        wide.clear();
        return true;
    }
    if (data == nullptr ||
        narrowBytes > static_cast<size_t>((std::numeric_limits<int>::max)()))
    {
        SetLastError(data == nullptr ? ERROR_INVALID_PARAMETER
                                     : ERROR_INSUFFICIENT_BUFFER);
        return false;
    }

    std::wstring staged;
    if (!WidenPluginBytes(static_cast<const char*>(data),
                          static_cast<int>(narrowBytes), staged))
        return false;
    try
    {
        wide.assign(staged.begin(), staged.end());
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
}

} // namespace

CLegacySalamanderRegistry::CLegacySalamanderRegistry(
    ::CSalamanderRegistryAbstract& wideRegistry)
    : WideRegistry(wideRegistry)
{
}

BOOL WINAPI CLegacySalamanderRegistry::ClearKey(HKEY key)
{
    return WideRegistry.ClearKey(key);
}

BOOL WINAPI CLegacySalamanderRegistry::CreateKey(HKEY key, const char* name,
                                                  HKEY& createdKey)
{
    std::wstring wideName;
    const wchar_t* liveName = nullptr;
    return WidenName(name, wideName, liveName)
               ? WideRegistry.CreateKey(key, liveName, createdKey)
               : FALSE;
}

BOOL WINAPI CLegacySalamanderRegistry::OpenKey(HKEY key, const char* name,
                                                HKEY& openedKey)
{
    std::wstring wideName;
    const wchar_t* liveName = nullptr;
    return WidenName(name, wideName, liveName)
               ? WideRegistry.OpenKey(key, liveName, openedKey)
               : FALSE;
}

void WINAPI CLegacySalamanderRegistry::CloseKey(HKEY key)
{
    WideRegistry.CloseKey(key);
}

BOOL WINAPI CLegacySalamanderRegistry::DeleteKey(HKEY key, const char* name)
{
    std::wstring wideName;
    const wchar_t* liveName = nullptr;
    return WidenName(name, wideName, liveName)
               ? WideRegistry.DeleteKey(key, liveName)
               : FALSE;
}

BOOL WINAPI CLegacySalamanderRegistry::GetValue(HKEY key, const char* name,
                                                 DWORD type, void* buffer,
                                                 DWORD bufferSize)
{
    std::wstring wideName;
    const wchar_t* liveName = nullptr;
    if (!WidenName(name, wideName, liveName))
        return FALSE;
    if (!IsStringValue(type))
        return WideRegistry.GetValue(key, liveName, type, buffer, bufferSize);

    std::string narrow;
    if (!ReadStringValue(WideRegistry, key, liveName, type, narrow))
        return FALSE;
    if (narrow.size() > bufferSize)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    if (buffer == nullptr && !narrow.empty())
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!narrow.empty())
        std::memcpy(buffer, narrow.data(), narrow.size());
    return TRUE;
}

BOOL WINAPI CLegacySalamanderRegistry::SetValue(HKEY key, const char* name,
                                                 DWORD type, const void* data,
                                                 DWORD dataSize)
{
    std::wstring wideName;
    const wchar_t* liveName = nullptr;
    if (!WidenName(name, wideName, liveName))
        return FALSE;
    if (!IsStringValue(type))
        return WideRegistry.SetValue(key, liveName, type, data, dataSize);

    std::vector<wchar_t> wide;
    if (!WidenStringValue(data, dataSize, wide) ||
        wide.size() > (std::numeric_limits<DWORD>::max)() / sizeof(wchar_t))
    {
        if (wide.size() >
            (std::numeric_limits<DWORD>::max)() / sizeof(wchar_t))
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    const DWORD wideBytes = static_cast<DWORD>(wide.size() * sizeof(wchar_t));
    return WideRegistry.SetValue(key, liveName, type,
                                 wide.empty() ? nullptr : wide.data(), wideBytes);
}

BOOL WINAPI CLegacySalamanderRegistry::DeleteValue(HKEY key, const char* name)
{
    std::wstring wideName;
    const wchar_t* liveName = nullptr;
    return WidenName(name, wideName, liveName)
               ? WideRegistry.DeleteValue(key, liveName)
               : FALSE;
}

BOOL WINAPI CLegacySalamanderRegistry::GetSize(HKEY key, const char* name,
                                                DWORD type, DWORD& bufferSize)
{
    std::wstring wideName;
    const wchar_t* liveName = nullptr;
    if (!WidenName(name, wideName, liveName))
        return FALSE;
    if (!IsStringValue(type))
        return WideRegistry.GetSize(key, liveName, type, bufferSize);

    std::string narrow;
    if (!ReadStringValue(WideRegistry, key, liveName, type, narrow) ||
        narrow.size() > (std::numeric_limits<DWORD>::max)())
    {
        if (narrow.size() > (std::numeric_limits<DWORD>::max)())
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    bufferSize = static_cast<DWORD>(narrow.size());
    return TRUE;
}

} // namespace sally::compat
