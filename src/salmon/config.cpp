// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <objbase.h>
#include <wtypes.h>
#include <vector>

#include "config.h"
#include "../registry_names.h"

const wchar_t* DB_ROOT_KEY = SAL_REG_KEY_BUG_REPORTER_DB_W;
const wchar_t* CONFIG_EMAIL_REG = L"Email";

CConfiguration Config;

// ****************************************************************************

BOOL CreateKey(HKEY hKey, const wchar_t* name, HKEY& createdKey)
{
    DWORD createType; // info whether the key was created or just opened
    LONG res = HANDLES(RegCreateKeyExW(hKey, name, 0, NULL, REG_OPTION_NON_VOLATILE,
                                       KEY_READ | KEY_WRITE, NULL, &createdKey,
                                       &createType));
    if (res == ERROR_SUCCESS)
        return TRUE;
    else
    {
        return FALSE;
    }
}

// ****************************************************************************

BOOL SetStringValue(HKEY hKey, const wchar_t* name, const std::wstring& value)
{
    if (value.size() >= MAXDWORD / sizeof(wchar_t))
        return FALSE;
    const DWORD dataSize = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    LONG res = RegSetValueExW(hKey, name, 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(value.c_str()), dataSize);
    if (res == ERROR_SUCCESS)
        return TRUE;
    else
    {
        return FALSE;
    }
}

// ****************************************************************************

BOOL OpenKey(HKEY hKey, const wchar_t* name, HKEY& openedKey)
{
    LONG res = HANDLES_Q(RegOpenKeyExW(hKey, name, 0, KEY_READ, &openedKey));
    if (res == ERROR_SUCCESS)
        return TRUE;
    else
    {
        return FALSE;
    }
}

// ****************************************************************************

BOOL GetStringValue(HKEY hKey, const wchar_t* name, std::wstring& value)
{
    DWORD type = 0;
    DWORD bytes = 0;
    LONG res = RegQueryValueExW(hKey, name, 0, &type, NULL, &bytes);
    if (res != ERROR_SUCCESS || type != REG_SZ || bytes % sizeof(wchar_t) != 0)
        return FALSE;
    try
    {
        for (;;)
        {
            std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
            DWORD actualBytes = bytes;
            res = RegQueryValueExW(hKey, name, 0, &type,
                                   reinterpret_cast<BYTE*>(buffer.data()), &actualBytes);
            if (res == ERROR_MORE_DATA)
            {
                if (actualBytes > bytes)
                    bytes = actualBytes;
                else if (bytes <= MAXDWORD / 2)
                    bytes *= 2;
                else
                    return FALSE;
                continue;
            }
            if (res != ERROR_SUCCESS || type != REG_SZ || actualBytes % sizeof(wchar_t) != 0)
                return FALSE;
            const size_t chars = actualBytes / sizeof(wchar_t);
            std::wstring result;
            if (chars > 0 && buffer[chars - 1] == L'\0')
                result.assign(buffer.data(), chars - 1);
            else
                result.assign(buffer.data(), chars);
            value.swap(result);
            return TRUE;
        }
    }
    catch (...)
    {
        return FALSE;
    }
}

//*****************************************************************************
//
// CConfiguration
//

CConfiguration::CConfiguration()
{
    Restart = TRUE;
}

BOOL CConfiguration::Load()
{
    HKEY hKey;
    if (OpenKey(HKEY_CURRENT_USER, DB_ROOT_KEY, hKey))
    {
        GetStringValue(hKey, CONFIG_EMAIL_REG, Email);
        HANDLES(RegCloseKey(hKey));
    }
    return TRUE;
}

BOOL CConfiguration::Save()
{
    HKEY hKey;
    if (CreateKey(HKEY_CURRENT_USER, DB_ROOT_KEY, hKey))
    {
        SetStringValue(hKey, CONFIG_EMAIL_REG, Email);
        HANDLES(RegCloseKey(hKey));
    }

    return TRUE;
}
