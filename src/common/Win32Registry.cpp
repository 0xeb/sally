// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "IRegistry.h"

class Win32Registry : public IRegistry
{
public:
    RegistryResult OpenKeyRead(HKEY root, const wchar_t* subKey, HKEY& outKey) override
    {
        LONG res = RegOpenKeyExW(root, subKey, 0, KEY_READ, &outKey);
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult OpenKeyReadWrite(HKEY root, const wchar_t* subKey, HKEY& outKey) override
    {
        LONG res = RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WRITE, &outKey);
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    RegistryResult CreateKey(HKEY root, const wchar_t* subKey, HKEY& outKey) override
    {
        DWORD disposition;
        LONG res = RegCreateKeyExW(root, subKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                   KEY_READ | KEY_WRITE, nullptr, &outKey, &disposition);
        return res == ERROR_SUCCESS ? RegistryResult::Ok() : RegistryResult::Error(res);
    }

    void CloseKey(HKEY key) override
    {
        if (key)
            RegCloseKey(key);
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
        DWORD type = 0;
        DWORD size = 0;

        // First call to get size
        LONG res = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size);
        if (res != ERROR_SUCCESS)
            return RegistryResult::Error(res);

        if (type != REG_SZ && type != REG_EXPAND_SZ)
            return RegistryResult::Error(ERROR_INVALID_DATATYPE);

        // Allocate and read
        value.resize(size / sizeof(wchar_t));
        res = RegQueryValueExW(key, valueName, nullptr, &type,
                               reinterpret_cast<BYTE*>(&value[0]), &size);
        if (res != ERROR_SUCCESS)
            return RegistryResult::Error(res);

        // Remove null terminator if present
        if (!value.empty() && value.back() == L'\0')
            value.pop_back();

        return RegistryResult::Ok();
    }

    RegistryResult GetDWord(HKEY key, const wchar_t* valueName, DWORD& value) override
    {
        DWORD type = 0;
        DWORD size = sizeof(DWORD);
        LONG res = RegQueryValueExW(key, valueName, nullptr, &type,
                                    reinterpret_cast<BYTE*>(&value), &size);
        if (res != ERROR_SUCCESS)
            return RegistryResult::Error(res);
        if (type != REG_DWORD)
            return RegistryResult::Error(ERROR_INVALID_DATATYPE);
        return RegistryResult::Ok();
    }

    RegistryResult GetQWord(HKEY key, const wchar_t* valueName, uint64_t& value) override
    {
        DWORD type = 0;
        DWORD size = sizeof(uint64_t);
        LONG res = RegQueryValueExW(key, valueName, nullptr, &type,
                                    reinterpret_cast<BYTE*>(&value), &size);
        if (res != ERROR_SUCCESS)
            return RegistryResult::Error(res);
        if (type != REG_QWORD)
            return RegistryResult::Error(ERROR_INVALID_DATATYPE);
        return RegistryResult::Ok();
    }

    RegistryResult GetBinary(HKEY key, const wchar_t* valueName, std::vector<uint8_t>& value) override
    {
        DWORD type = 0;
        DWORD size = 0;

        LONG res = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &size);
        if (res != ERROR_SUCCESS)
            return RegistryResult::Error(res);

        value.resize(size);
        res = RegQueryValueExW(key, valueName, nullptr, &type, value.data(), &size);
        if (res != ERROR_SUCCESS)
            return RegistryResult::Error(res);

        return RegistryResult::Ok();
    }

    RegistryResult GetValue(HKEY key, const wchar_t* valueName,
                            RegValueType& type, std::vector<uint8_t>& data) override
    {
        DWORD dwType = 0;
        DWORD size = 0;

        LONG res = RegQueryValueExW(key, valueName, nullptr, &dwType, nullptr, &size);
        if (res != ERROR_SUCCESS)
            return RegistryResult::Error(res);

        type = static_cast<RegValueType>(dwType);
        data.resize(size);
        res = RegQueryValueExW(key, valueName, nullptr, &dwType, data.data(), &size);
        if (res != ERROR_SUCCESS)
            return RegistryResult::Error(res);

        return RegistryResult::Ok();
    }

    RegistryResult SetString(HKEY key, const wchar_t* valueName, const wchar_t* value) override
    {
        DWORD size = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
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
        LONG res = RegSetValueExW(key, valueName, 0, REG_BINARY,
                                  static_cast<const BYTE*>(data), static_cast<DWORD>(size));
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
        wchar_t name[256];
        DWORD index = 0;

        while (true)
        {
            DWORD nameSize = 256;
            LONG res = RegEnumKeyExW(key, index, name, &nameSize, nullptr, nullptr, nullptr, nullptr);
            if (res == ERROR_NO_MORE_ITEMS)
                break;
            if (res != ERROR_SUCCESS)
                return RegistryResult::Error(res);
            subKeys.push_back(name);
            index++;
        }
        return RegistryResult::Ok();
    }

    RegistryResult EnumValues(HKEY key, std::vector<std::wstring>& valueNames) override
    {
        valueNames.clear();
        wchar_t name[256];
        DWORD index = 0;

        while (true)
        {
            DWORD nameSize = 256;
            LONG res = RegEnumValueW(key, index, name, &nameSize, nullptr, nullptr, nullptr, nullptr);
            if (res == ERROR_NO_MORE_ITEMS)
                break;
            if (res != ERROR_SUCCESS)
                return RegistryResult::Error(res);
            valueNames.push_back(name);
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
