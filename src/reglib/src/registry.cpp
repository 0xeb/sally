// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <string>
#include <vector>

#include "regparse.h"

BOOL StrEndsWith(const wchar_t* txt, const wchar_t* pattern, size_t patternLen);

namespace RegLib
{

    class CSystemRegistry : public CSalamanderRegistryExAbstractW
    {
    public:
        virtual BOOL WINAPI ClearKey(HKEY key);
        virtual BOOL WINAPI CreateKey(HKEY key, const wchar_t* name, HKEY& createdKey);
        virtual BOOL WINAPI OpenKey(HKEY key, const wchar_t* name, HKEY& openedKey);
        virtual void WINAPI CloseKey(HKEY key);
        virtual BOOL WINAPI DeleteKey(HKEY key, const wchar_t* name);
        virtual BOOL WINAPI GetValue(HKEY key, const wchar_t* name, DWORD type, LPVOID data, DWORD dataSize);
        virtual BOOL WINAPI SetValue(HKEY key, const wchar_t* name, DWORD type, LPCVOID data, DWORD dataSize);
        virtual BOOL WINAPI DeleteValue(HKEY key, const wchar_t* name);
        virtual BOOL WINAPI GetSize(HKEY key, const wchar_t* name, DWORD type, DWORD& bufferSize);

        virtual BOOL WINAPI EnumKey(HKEY key, DWORD subKeyIndex, std::wstring& name);
        virtual BOOL WINAPI EnumValue(HKEY key, DWORD valIndex, std::wstring& name, LPDWORD valType, LPBYTE data, LPDWORD dataSize);

        virtual void WINAPI RemoveHiddenKeysAndValues() {}
        virtual BOOL WINAPI ClearKeyEx(HKEY key, BOOL doNotDeleteHiddenKeysAndValues, BOOL* keyIsNotEmpty);

        virtual void WINAPI Release();
        virtual BOOL WINAPI Dump(HANDLE /*outputFile*/, const wchar_t* /*clearKeyName*/) { return TRUE; }

    private:
        BOOL ClearKeyAux(HKEY key, BOOL doNotDeleteHiddenKeysAndValues, BOOL* keyIsNotEmpty);
    };

    void CSystemRegistry::Release()
    {
        delete this;
    }

    BOOL CSystemRegistry::ClearKey(HKEY key)
    {
        return ClearKeyAux(key, FALSE, NULL);
    }

    BOOL CSystemRegistry::ClearKeyEx(HKEY key, BOOL doNotDeleteHiddenKeysAndValues, BOOL* keyIsNotEmpty)
    {
        return ClearKeyAux(key, doNotDeleteHiddenKeysAndValues, keyIsNotEmpty);
    }

    BOOL CSystemRegistry::CreateKey(HKEY key, const wchar_t* name, HKEY& createdKey)
    {
        // The class parameter was a `char buff[] = ""` - an empty
        // ANSI string handed to a width-mapped API. NULL says the same thing (no
        // class) and says it in both builds; the buffer only ever existed because
        // the parameter is non-const in the SDK signature.
        return ERROR_SUCCESS == RegCreateKeyExW(key, name, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &createdKey, NULL);
    }

    BOOL CSystemRegistry::OpenKey(HKEY key, const wchar_t* name, HKEY& openedKey)
    {
        return ERROR_SUCCESS == RegOpenKeyExW(key, name, 0, KEY_ALL_ACCESS, &openedKey);
    }

    void CSystemRegistry::CloseKey(HKEY key)
    {
        RegCloseKey(key);
    }

    BOOL CSystemRegistry::DeleteKey(HKEY key, const wchar_t* name)
    {
        return ERROR_SUCCESS == RegDeleteKeyW(key, name);
    }

#ifndef INSIDE_SALAMANDER

    // our variant of the RegQueryValueEx function; unlike the API version it
    // ensures adding a null terminator for REG_SZ, REG_MULTI_SZ, and
    // REG_EXPAND_SZ
    // WARNING: when determining the required buffer size it returns one or two
    //          characters more (two only for REG_MULTI_SZ) in case the string
    //          needs to be terminated with null character(s)
    LONG SalRegQueryValueExW(HKEY hKey, const wchar_t* lpValueName, LPDWORD lpReserved,
                            LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
    {
        DWORD dataBufSize = lpData == NULL ? 0 : *lpcbData;
        DWORD type = REG_NONE;
        LONG ret = RegQueryValueExW(hKey, lpValueName, lpReserved, &type, lpData, lpcbData);
        if (lpType != NULL)
            *lpType = type;
        if (type == REG_SZ || type == REG_MULTI_SZ || type == REG_EXPAND_SZ)
        {
            if (hKey != HKEY_PERFORMANCE_DATA &&
                lpcbData != NULL &&
                (ret == ERROR_MORE_DATA || lpData == NULL && ret == ERROR_SUCCESS))
            {
                (*lpcbData) += (type == REG_MULTI_SZ ? 2 : 1) * (DWORD)sizeof(wchar_t);
                return ret;
            }
            if (ret == ERROR_SUCCESS && lpData != NULL)
            {
                wchar_t* data = (wchar_t*)lpData;
                DWORD units = *lpcbData / (DWORD)sizeof(wchar_t);
                if (units < 1 || data[units - 1] != 0)
                {
                    if (*lpcbData + sizeof(wchar_t) <= dataBufSize)
                    {
                        data[units++] = 0;
                        *lpcbData += (DWORD)sizeof(wchar_t);
                    }
                    else // not enough space for a null terminator in the buffer
                    {
                        (*lpcbData) += (type == REG_MULTI_SZ ? 2 : 1) * (DWORD)sizeof(wchar_t);
                        return ERROR_MORE_DATA;
                    }
                }
                if (type == REG_MULTI_SZ && (units < 2 || data[units - 2] != 0))
                {
                    if (*lpcbData + sizeof(wchar_t) <= dataBufSize)
                    {
                        data[units++] = 0;
                        *lpcbData += (DWORD)sizeof(wchar_t);
                    }
                    else // not enough space for the second null terminator in the buffer
                    {
                        *lpcbData += (DWORD)sizeof(wchar_t);
                        return ERROR_MORE_DATA;
                    }
                }
            }
        }
        return ret;
    }

#endif // INSIDE_SALAMANDER

    BOOL CSystemRegistry::GetValue(HKEY key, const wchar_t* name, DWORD type, LPVOID data, DWORD dataSize)
    {
        DWORD realType;

        return (ERROR_SUCCESS == SalRegQueryValueExW(key, name, NULL, &realType, (BYTE*)data, &dataSize)) && (realType == type);
    }

    BOOL CSystemRegistry::SetValue(HKEY key, const wchar_t* name, DWORD type, LPCVOID data, DWORD dataSize)
    {
        return ERROR_SUCCESS == RegSetValueExW(key, name, NULL, type, (BYTE*)data, dataSize);
    }

    BOOL CSystemRegistry::DeleteValue(HKEY key, const wchar_t* name)
    {
        return ERROR_SUCCESS == RegDeleteValueW(key, name);
    }

    BOOL CSystemRegistry::GetSize(HKEY key, const wchar_t* name, DWORD type, DWORD& dataSize)
    {
        DWORD realType;

        return (ERROR_SUCCESS == SalRegQueryValueExW(key, name, NULL, &realType, NULL, &dataSize)) && (realType == type);
    }

    BOOL CSystemRegistry::EnumKey(HKEY key, DWORD subKeyIndex, std::wstring& name)
    {
        try
        {
            for (;;)
            {
                DWORD maximumLength = 0;
                if (RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, &maximumLength, NULL,
                                     NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS ||
                    maximumLength == MAXDWORD)
                    return FALSE;
                std::vector<wchar_t> buffer(static_cast<size_t>(maximumLength) + 1, L'\0');
                DWORD length = static_cast<DWORD>(buffer.size());
                LONG error = RegEnumKeyExW(key, subKeyIndex, buffer.data(), &length,
                                           NULL, NULL, NULL, NULL);
                if (error == ERROR_MORE_DATA)
                    continue;
                if (error != ERROR_SUCCESS)
                    return FALSE;
                std::wstring result(buffer.data(), length);
                name.swap(result);
                return TRUE;
            }
        }
        catch (...)
        {
            return FALSE;
        }
        return FALSE;
    }

    BOOL CSystemRegistry::EnumValue(HKEY key, DWORD valIndex, std::wstring& name, LPDWORD valType, LPBYTE data, LPDWORD dataSize)
    {
        try
        {
            for (;;)
            {
                DWORD maximumLength = 0;
                if (RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, &maximumLength, NULL, NULL, NULL) != ERROR_SUCCESS ||
                    maximumLength == MAXDWORD)
                    return FALSE;
                std::vector<wchar_t> buffer(static_cast<size_t>(maximumLength) + 1, L'\0');
                DWORD length = static_cast<DWORD>(buffer.size());
                LONG error = RegEnumValueW(key, valIndex, buffer.data(), &length, NULL,
                                           valType, data, dataSize);
                if (error == ERROR_MORE_DATA && data == NULL)
                    continue;
                if (error != ERROR_SUCCESS)
                    return FALSE;
                std::wstring result(buffer.data(), length);
                name.swap(result);
                return TRUE;
            }
        }
        catch (...)
        {
            return FALSE;
        }
        return FALSE;
    }

    BOOL CSystemRegistry::ClearKeyAux(HKEY key, BOOL doNotDeleteHiddenKeysAndValues, BOOL* keyIsNotEmpty)
    {
        std::wstring name;
        HKEY subKey;
        DWORD index = 0;

        while (EnumKey(key, index, name))
        {
            if (!doNotDeleteHiddenKeysAndValues ||
                !::StrEndsWith(name.c_str(), L".hidden", SizeOf(L".hidden") - 1))
            {
                if (RegOpenKeyExW(key, name.c_str(), 0, KEY_READ | KEY_WRITE, &subKey) == ERROR_SUCCESS)
                {
                    BOOL subkeyIsNotEmpty = FALSE;
                    BOOL ret = ClearKeyAux(subKey, doNotDeleteHiddenKeysAndValues, &subkeyIsNotEmpty);
                    if (subkeyIsNotEmpty && keyIsNotEmpty != NULL)
                        *keyIsNotEmpty = TRUE;

                    RegCloseKey(subKey);
                    if (!ret || !subkeyIsNotEmpty && RegDeleteKeyW(key, name.c_str()) != ERROR_SUCCESS)
                        return FALSE;
                    if (subkeyIsNotEmpty)
                        index++;
                }
                else
                {
                    return FALSE;
                }
            }
            else
            {
                if (keyIsNotEmpty != NULL)
                    *keyIsNotEmpty = TRUE;
                index++;
            }
        }

        index = 0;
        while (EnumValue(key, index, name, NULL, NULL, NULL))
        {
            if (!doNotDeleteHiddenKeysAndValues ||
                !::StrEndsWith(name.c_str(), L".hidden", SizeOf(L".hidden") - 1))
            {
                if (RegDeleteValueW(key, name.c_str()) != ERROR_SUCCESS)
                { // Unable to delete values in specified key (in registry)
                    break;
                }
            }
            else
            {
                if (keyIsNotEmpty != NULL)
                    *keyIsNotEmpty = TRUE;
                index++;
            }
        }

        return TRUE;
    }

} // namespace RegLib

CSalamanderRegistryExAbstractW* REG_SysRegistryFactoryW()
{
    try
    {
        return new RegLib::CSystemRegistry();
    }
    catch (...)
    {
        return NULL;
    }
}
