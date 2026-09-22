// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "IRegistry.h"
#include "Win32EarlyStartupRegistry.h"
// handles.h declares CreateMappedBitmap using COLORMAP/LPCOLORMAP, which <windows.h> alone does
// not define - that type lives in commctrl.h. Every other includer of handles.h gets it
// transitively via precomp.h's own #include <commctrl.h>; this file has no precomp.h, so it must
// pull it in directly.
#include <commctrl.h>
#include "handles.h"
#include <shlwapi.h>

#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

namespace
{
RegistryResult ReadRegistryValueDynamic(HKEY key, const wchar_t* valueName,
                                        DWORD& type, std::vector<uint8_t>& data)
{
    try
    {
        DWORD requiredBytes = 0;
        LONG result = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &requiredBytes);
        if (result != ERROR_SUCCESS)
            return RegistryResult::Error(result);

        std::vector<uint8_t> candidate;
        for (;;)
        {
            candidate.resize(requiredBytes);
            DWORD actualBytes = requiredBytes;
            DWORD actualType = type;
            result = RegQueryValueExW(key, valueName, nullptr, &actualType,
                                      candidate.empty() ? nullptr : candidate.data(),
                                      &actualBytes);
            if (result == ERROR_MORE_DATA)
            {
                if (actualBytes > requiredBytes)
                    requiredBytes = actualBytes;
                else
                {
                    if (requiredBytes > (std::numeric_limits<DWORD>::max)() / 2)
                        return RegistryResult::Error(ERROR_ARITHMETIC_OVERFLOW);
                    requiredBytes = requiredBytes == 0 ? sizeof(wchar_t) : requiredBytes * 2;
                }
                continue;
            }
            if (result != ERROR_SUCCESS)
                return RegistryResult::Error(result);

            candidate.resize(actualBytes);
            type = actualType;
            data.swap(candidate);
            return RegistryResult::Ok();
        }
    }
    catch (const std::bad_alloc&)
    {
        return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
    }
    catch (const std::length_error&)
    {
        return RegistryResult::Error(ERROR_ARITHMETIC_OVERFLOW);
    }
}

// RegQueryValueEx does NOT guarantee that a REG_SZ / REG_EXPAND_SZ / REG_MULTI_SZ value comes back
// NUL-terminated - the terminator is only present if whoever wrote the value counted it in cbData,
// and writers that do not are both legal and common (a self-registering shell handler passing
// lstrlenW(name) * sizeof(wchar_t) is the classic one). Every caller here treats the result as a C
// string, so an unterminated value sends them scanning off the end of the buffer; that is how the
// icon-overlay handler list came to build its description out of whatever followed a stack array.
//
// reglib's SalRegQueryValueEx existed for exactly this reason and the wide facade replaced it
// without carrying the guarantee over. This restores that contract in wchar_t units: append the
// terminator(s) when there is room, otherwise report the enlarged requirement with ERROR_MORE_DATA
// rather than hand back a truncated string. A size query is left exactly as the registry reports
// it - see the comment on that arm.
LONG EnsureRegStringTerminator(HKEY key, DWORD type, void* data, DWORD capacity,
                               DWORD& dataSize, LONG result)
{
    if (type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
        return result;

    const DWORD unit = static_cast<DWORD>(sizeof(wchar_t));
    const DWORD terminators = type == REG_MULTI_SZ ? 2u : 1u;

    // HKEY_PERFORMANCE_DATA reports sizes by a different rule and must not be second-guessed.
    if (key == HKEY_PERFORMANCE_DATA)
        return result;

    // A size QUERY reports what the registry holds, exactly - Win32RegistryIntegration's
    // RawValueTransferPreservesTypeSizeAndBytes pins that fidelity, and inflating it would make
    // every honest caller allocate two bytes it does not need and believe the value is longer than
    // it is. The terminator is added on the READ instead: an unterminated value that fills the
    // caller's buffer comes back as ERROR_MORE_DATA with the enlarged requirement below, which is
    // the retry every registry caller already implements. ERROR_MORE_DATA is inflated here because
    // that number IS the allocation the caller is about to make.
    if (result == ERROR_MORE_DATA)
    {
        if (dataSize <= (std::numeric_limits<DWORD>::max)() - terminators * unit)
            dataSize += terminators * unit;
        return result;
    }
    if (result != ERROR_SUCCESS || data == nullptr)
        return result;

    // Work in whole wchar_t units. A string value whose byte count is not a multiple of
    // sizeof(wchar_t) is malformed; round DOWN to the last complete character rather than trust a
    // straddling half of one.
    const BYTE* const bytes = static_cast<const BYTE*>(data);
    const DWORD units = dataSize / unit;
    auto unitIsZero = [bytes, unit](DWORD index) {
        for (DWORD i = 0; i < unit; i++)
            if (bytes[index * unit + i] != 0)
                return false;
        return true;
    };

    DWORD missing = 0;
    if (units == 0 || !unitIsZero(units - 1))
        missing = terminators;
    else if (terminators == 2 && (units < 2 || !unitIsZero(units - 2)))
        missing = 1;

    if (missing == 0)
    {
        dataSize = units * unit;
        return result;
    }
    if (units + missing > capacity / unit)
    {
        dataSize = (units + missing) * unit;
        return ERROR_MORE_DATA;
    }
    memset(static_cast<BYTE*>(data) + units * unit, 0, missing * unit);
    dataSize = (units + missing) * unit;
    return result;
}
}

class Win32Registry : public IRegistry
{
public:
    // These must go through HANDLES()/HANDLES_Q(), not raw RegOpenKeyExW/
    // RegCreateKeyExW/RegCloseKey - CloseKey(HKEY) (regwork.cpp's CloseKeyAux, the only close path
    // callers of OpenKeyW/CreateKeyW have - there is no CloseKeyW) closes through the HANDLES-
    // tracked path. A handle opened here untracked and closed there produced a genuine "Handle not
    // found" error box (handles.cpp's CheckClose) on every config load/save, since the tracked
    // table never saw the open. Tracking the open here, not skipping the track on the close side,
    // matches every other registry helper in this codebase (regwork.cpp's OpenKeyAux/CreateKeyAux).
    RegistryResult OpenKeyRead(HKEY root, const wchar_t* subKey, HKEY& outKey) override
    {
        LONG res = HANDLES_Q(RegOpenKeyExW(root, subKey, 0, KEY_READ, &outKey));
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult OpenKeyReadWrite(HKEY root, const wchar_t* subKey, HKEY& outKey) override
    {
        LONG res = HANDLES_Q(RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WRITE, &outKey));
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult CreateKey(HKEY root, const wchar_t* subKey, HKEY& outKey) override
    {
        DWORD disposition;
        LONG res = HANDLES(RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                          KEY_READ | KEY_WRITE, nullptr, &outKey, &disposition));
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    void CloseKey(HKEY key) override
    {
        if (key)
            HANDLES(RegCloseKey(key));
    }

    RegistryResult DeleteKey(HKEY root, const wchar_t* subKey) override
    {
        LONG res = RegDeleteKeyW(root, subKey);
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult DeleteKeyRecursive(HKEY root, const wchar_t* subKey) override
    {
        // Use RegDeleteTreeW if available (Vista+), otherwise manual recursion
        LONG res = RegDeleteTreeW(root, subKey);
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult GetString(HKEY key, const wchar_t* valueName, std::wstring& value) override
    {
        DWORD type = REG_NONE;
        std::vector<uint8_t> bytes;
        RegistryResult result = ReadRegistryValueDynamic(key, valueName, type, bytes);
        if (!result.success)
            return result;
        if (type != REG_SZ && type != REG_EXPAND_SZ)
            return RegistryResult::Error(ERROR_INVALID_DATATYPE);
        if ((bytes.size() % sizeof(wchar_t)) != 0)
            return RegistryResult::Error(ERROR_INVALID_DATA);

        try
        {
            std::wstring candidate(bytes.size() / sizeof(wchar_t), L'\0');
            if (!bytes.empty())
                memcpy(candidate.data(), bytes.data(), bytes.size());
            const size_t terminator = candidate.find(L'\0');
            if (terminator != std::wstring::npos)
                candidate.resize(terminator);
            value.swap(candidate);
            return RegistryResult::Ok();
        }
        catch (const std::bad_alloc&)
        {
            return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return RegistryResult::Error(ERROR_ARITHMETIC_OVERFLOW);
        }
    }

    RegistryResult GetDWord(HKEY key, const wchar_t* valueName, DWORD& value) override
    {
        DWORD candidate = 0;
        DWORD type = REG_NONE;
        DWORD size = sizeof(candidate);
        LONG res = RegQueryValueExW(key, valueName, nullptr, &type,
                                    reinterpret_cast<BYTE*>(&candidate), &size);
        if (res != ERROR_SUCCESS)
            return RegistryResult::Error(res);
        if (type != REG_DWORD || size != sizeof(candidate))
            return RegistryResult::Error(ERROR_INVALID_DATATYPE);
        value = candidate;
        return RegistryResult::Ok();
    }

    RegistryResult GetQWord(HKEY key, const wchar_t* valueName, uint64_t& value) override
    {
        uint64_t candidate = 0;
        DWORD type = REG_NONE;
        DWORD size = sizeof(candidate);
        LONG res = RegQueryValueExW(key, valueName, nullptr, &type,
                                    reinterpret_cast<BYTE*>(&candidate), &size);
        if (res != ERROR_SUCCESS)
            return RegistryResult::Error(res);
        if (type != REG_QWORD || size != sizeof(candidate))
            return RegistryResult::Error(ERROR_INVALID_DATATYPE);
        value = candidate;
        return RegistryResult::Ok();
    }

    RegistryResult GetBinary(HKEY key, const wchar_t* valueName, std::vector<uint8_t>& value) override
    {
        DWORD type = REG_NONE;
        std::vector<uint8_t> candidate;
        RegistryResult result = ReadRegistryValueDynamic(key, valueName, type, candidate);
        if (result.success)
            value.swap(candidate);
        return result;
    }

    RegistryResult GetValue(HKEY key, const wchar_t* valueName,
                            RegValueType& type, std::vector<uint8_t>& data) override
    {
        DWORD nativeType = REG_NONE;
        std::vector<uint8_t> candidate;
        RegistryResult result = ReadRegistryValueDynamic(key, valueName, nativeType, candidate);
        if (result.success)
        {
            type = static_cast<RegValueType>(nativeType);
            data.swap(candidate);
        }
        return result;
    }

    RegistryResult OpenKeyNotify(HKEY root, const wchar_t* subKey, HKEY& outKey) override
    {
        outKey = NULL;
        LONG result = HANDLES_Q(RegOpenKeyExW(root, subKey, 0, KEY_NOTIFY, &outKey));
        return result == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(result);
    }

    RegistryResult NotifyChange(HKEY key, bool watchSubtree, DWORD notifyFilter,
                                HANDLE eventHandle, bool asynchronous) override
    {
        LONG result = ::RegNotifyChangeKeyValue(key, watchSubtree ? TRUE : FALSE, notifyFilter,
                                                eventHandle, asynchronous ? TRUE : FALSE);
        return result == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(result);
    }

    RegistryResult ReadValue(HKEY key, const wchar_t* valueName,
                             RegValueType& type, void* data, DWORD& dataSize) override
    {
        const DWORD capacity = data == nullptr ? 0 : dataSize;
        DWORD nativeType = 0;
        LONG res = RegQueryValueExW(key, valueName, nullptr, &nativeType,
                                    static_cast<BYTE*>(data), &dataSize);
        type = static_cast<RegValueType>(nativeType);
        res = EnsureRegStringTerminator(key, nativeType, data, capacity, dataSize, res);
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult SetString(HKEY key, const wchar_t* valueName, const wchar_t* value) override
    {
        if (value == nullptr)
            return RegistryResult::Error(ERROR_INVALID_PARAMETER);
        const size_t length = wcslen(value);
        if (length > (std::numeric_limits<DWORD>::max)() / sizeof(wchar_t) - 1)
            return RegistryResult::Error(ERROR_ARITHMETIC_OVERFLOW);
        DWORD size = static_cast<DWORD>((length + 1) * sizeof(wchar_t));
        LONG res = RegSetValueExW(key, valueName, 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(value), size);
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult SetDWord(HKEY key, const wchar_t* valueName, DWORD value) override
    {
        LONG res = RegSetValueExW(key, valueName, 0, REG_DWORD,
                                  reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult SetQWord(HKEY key, const wchar_t* valueName, uint64_t value) override
    {
        LONG res = RegSetValueExW(key, valueName, 0, REG_QWORD,
                                  reinterpret_cast<const BYTE*>(&value), sizeof(uint64_t));
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult SetBinary(HKEY key, const wchar_t* valueName,
                             const void* data, size_t size) override
    {
        if (size > (std::numeric_limits<DWORD>::max)())
            return RegistryResult::Error(ERROR_ARITHMETIC_OVERFLOW);
        LONG res = RegSetValueExW(key, valueName, 0, REG_BINARY,
                                  static_cast<const BYTE*>(data), static_cast<DWORD>(size));
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult WriteValue(HKEY key, const wchar_t* valueName,
                              RegValueType type, const void* data, DWORD dataSize) override
    {
        LONG res = RegSetValueExW(key, valueName, 0, static_cast<DWORD>(type),
                                  static_cast<const BYTE*>(data), dataSize);
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult DeleteValue(HKEY key, const wchar_t* valueName) override
    {
        LONG res = RegDeleteValueW(key, valueName);
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult EnumSubKeys(HKEY key, std::vector<std::wstring>& subKeys) override
    {
        subKeys.clear();
        DWORD maxSubKeyNameLen = 0;
        LONG infoRes = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr,
                                        nullptr, &maxSubKeyNameLen, nullptr,
                                        nullptr, nullptr, nullptr, nullptr, nullptr);
        if (infoRes != ERROR_SUCCESS)
            return RegistryResult::Error(infoRes);

        std::vector<wchar_t> name(maxSubKeyNameLen + 1, L'\0');
        DWORD index = 0;

        while (true)
        {
            DWORD nameSize = static_cast<DWORD>(name.size());
            LONG res = RegEnumKeyExW(key, index, name.data(), &nameSize, nullptr, nullptr, nullptr, nullptr);
            if (res == ERROR_NO_MORE_ITEMS)
                break;
            if (res == ERROR_MORE_DATA)
            {
                name.resize(name.size() * 2);
                continue;
            }
            if (res != ERROR_SUCCESS)
                return RegistryResult::Error(res);
            subKeys.emplace_back(name.data(), nameSize);
            index++;
        }
        return RegistryResult::Ok();
    }

    RegistryResult EnumValues(HKEY key, std::vector<std::wstring>& valueNames) override
    {
        valueNames.clear();
        DWORD maxValueNameLen = 0;
        LONG infoRes = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr,
                                        nullptr, nullptr, nullptr, nullptr,
                                        &maxValueNameLen, nullptr, nullptr, nullptr);
        if (infoRes != ERROR_SUCCESS)
            return RegistryResult::Error(infoRes);

        std::vector<wchar_t> name(maxValueNameLen + 1, L'\0');
        DWORD index = 0;

        while (true)
        {
            DWORD nameSize = static_cast<DWORD>(name.size());
            LONG res = RegEnumValueW(key, index, name.data(), &nameSize, nullptr, nullptr, nullptr, nullptr);
            if (res == ERROR_NO_MORE_ITEMS)
                break;
            if (res == ERROR_MORE_DATA)
            {
                name.resize(name.size() * 2);
                continue;
            }
            if (res != ERROR_SUCCESS)
                return RegistryResult::Error(res);
            valueNames.emplace_back(name.data(), nameSize);
            index++;
        }
        return RegistryResult::Ok();
    }

    bool KeyExists(HKEY root, const wchar_t* subKey) override
    {
        HKEY key;
        LONG res = RegOpenKeyExW(root, subKey, 0, KEY_READ, &key);
        if (res == ERROR_SUCCESS)
        {
            RegCloseKey(key);
            return true;
        }
        return false;
    }

    bool ValueExists(HKEY key, const wchar_t* valueName) override
    {
        LONG res = RegQueryValueExW(key, valueName, nullptr, nullptr, nullptr, nullptr);
        return res == ERROR_SUCCESS;
    }
};

// Global instance
static Win32Registry g_win32Registry;
IRegistry* gRegistry = &g_win32Registry;

IRegistry* GetWin32Registry()
{
    return &g_win32Registry;
}

LONG sally::registry::QueryValueForEarlyStartupW(HKEY key, const wchar_t* valueName, DWORD* type,
                                                  BYTE* data, DWORD* dataBytes)
{
    return RegQueryValueExW(key, valueName, nullptr, type, data, dataBytes);
}
