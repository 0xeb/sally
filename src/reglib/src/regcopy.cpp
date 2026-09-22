// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <string>

#include "regparse.h"

static eRPE_ERROR CopyKey(CSalamanderRegistryExAbstractW* pInReg, HKEY hInKey, CSalamanderRegistryExAbstractW* pOutReg, HKEY hOutKey)
{
    eRPE_ERROR ret = RPE_OK;

    std::wstring name;

    DWORD indKey;
    for (indKey = 0;; indKey++)
    {
        HKEY hSubInKey, hSubOutKey;
        if (!pInReg->EnumKey(hInKey, indKey, name))
        {
            break;
        }
        if (!pInReg->OpenKey(hInKey, name.c_str(), hSubInKey))
        {
            return RPE_KEY_OPEN;
        }
        if (!pOutReg->CreateKey(hOutKey, name.c_str(), hSubOutKey))
        {
            pInReg->CloseKey(hSubInKey);
            return RPE_KEY_CREATE;
        }
        ret = CopyKey(pInReg, hSubInKey, pOutReg, hSubOutKey);
        pInReg->CloseKey(hSubInKey);
        pOutReg->CloseKey(hSubOutKey);
        if (RPE_OK != ret)
        {
            return ret;
        }
    }

    DWORD indVal;
    for (indVal = 0;; indVal++)
    {
        DWORD valType, dataSize;
        BYTE data[256];
        LPBYTE dataPtr = data;

        if (!pInReg->EnumValue(hInKey, indVal, name, &valType, NULL, NULL))
        {
            break;
        }
        if (!pInReg->GetSize(hInKey, name.c_str(), valType, dataSize))
        {
            ret = RPE_VALUE_GET_SIZE;
            break;
        }
        if (dataSize > sizeof(data))
        {
            dataPtr = (LPBYTE)malloc(dataSize);
            if (!dataPtr)
            {
                ret = RPE_OUT_OF_MEMORY;
                break;
            }
        }
        if (!pInReg->GetValue(hInKey, name.c_str(), valType, dataPtr, dataSize))
        {
            if (dataPtr != data)
                free(dataPtr);
            ret = RPE_VALUE_GET;
            break;
        }
        if (!pOutReg->SetValue(hOutKey, name.c_str(), valType, dataPtr, dataSize))
        {
            if (dataPtr != data)
                free(dataPtr);
            ret = RPE_VALUE_SET;
            break;
        }
        if (dataPtr != data)
            free(dataPtr);
    }
    return ret;
}

eRPE_ERROR CopyRegistryBranchW(const wchar_t* branch, CSalamanderRegistryExAbstractW* pInReg, CSalamanderRegistryExAbstractW* pOutReg)
{
    HKEY hParentKey, hInKey, hOutKey;
    eRPE_ERROR ret = RPE_OK;

    if (!_wcsnicmp(branch, L"HKEY_CURRENT_USER", SizeOf(L"HKEY_CURRENT_USER") - 1))
    {
        branch += SizeOf(L"HKEY_CURRENT_USER") - 1;
        hParentKey = HKEY_CURRENT_USER;
    }
    else if (!_wcsnicmp(branch, L"HKEY_CLASSES_ROOT", SizeOf(L"HKEY_CLASSES_ROOT") - 1))
    {
        branch += SizeOf(L"HKEY_CLASSES_ROOT") - 1;
        hParentKey = HKEY_CLASSES_ROOT;
    }
    else if (!_wcsnicmp(branch, L"HKEY_LOCAL_MACHINE", SizeOf(L"HKEY_LOCAL_MACHINE") - 1))
    {
        branch += SizeOf(L"HKEY_LOCAL_MACHINE") - 1;
        hParentKey = HKEY_LOCAL_MACHINE;
    }
    else if (!_wcsnicmp(branch, L"HKEY_USERS", SizeOf(L"HKEY_USERS") - 1))
    {
        branch += SizeOf(L"HKEY_USERS") - 1;
        hParentKey = HKEY_USERS;
    }
    else if (!_wcsnicmp(branch, L"HKEY_CURRENT_CONFIG", SizeOf(L"HKEY_CURRENT_CONFIG") - 1))
    {
        branch += SizeOf(L"HKEY_CURRENT_CONFIG") - 1;
        hParentKey = HKEY_CURRENT_CONFIG;
    }
    else if (!_wcsnicmp(branch, L"HKEY_DYN_DATA", SizeOf(L"HKEY_DYN_DATA") - 1))
    {
        branch += SizeOf(L"HKEY_DYN_DATA") - 1;
        hParentKey = HKEY_DYN_DATA;
    }
    else if (!_wcsnicmp(branch, L"HKEY_PERFORMANCE_DATA", SizeOf(L"HKEY_PERFORMANCE_DATA") - 1))
    {
        branch += SizeOf(L"HKEY_PERFORMANCE_DATA") - 1;
        hParentKey = HKEY_PERFORMANCE_DATA;
    }
    else
    {
        return RPE_ROOT_INVALID_KEY;
    }

    if (*branch != 0)
    {
        if (*branch != '\\')
        {
            return RPE_ROOT_INVALID_KEY;
        }
        else
        {
            branch++;
        }
    }

    if (!pInReg->OpenKey(hParentKey, branch, hInKey))
    {
        return RPE_KEY_OPEN;
    }

    if (pOutReg->CreateKey(hParentKey, branch, hOutKey))
    {
        ret = CopyKey(pInReg, hInKey, pOutReg, hOutKey);
        pOutReg->CloseKey(hOutKey);
    }
    else
    {
        ret = RPE_KEY_CREATE;
    }

    pInReg->CloseKey(hInKey);

    return ret;
}
