// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "regparse.h"

#define SkipWS(s) \
    while ((*s == ' ') || (*s == 9)) \
    s++

BOOL StrEndsWith(const wchar_t* txt, const wchar_t* pattern, size_t patternLen);

static wchar_t* SeparateStr(wchar_t* s)
{
    while (*s)
    {
        if (*s == '\\')
        {
            switch (s[1])
            {
            case '\"':
                break;
            case '\\':
                break;
            case 't':
                s[1] = '\t';
                break;
            case 'r':
                s[1] = '\r';
                break;
            case 'n':
                s[1] = '\n';
                break;
            default:
                return NULL;
            }
            memmove(s, s + 1, wcslen(s) * sizeof(s[0]));
        }
        else if (*s == '\"')
        {
            break;
        }
        s++;
    }
    if (!*s)
    {
        return NULL;
    }
    *s++ = 0;
    return s;
}

eRPE_ERROR ParseRegistryFileW(wchar_t* buf, CSalamanderRegistryExAbstractW* pRegistry, BOOL doNotDeleteHiddenKeysAndValues)
{
    if (buf == NULL || pRegistry == NULL)
        return RPE_INVALID_FORMAT;

    wchar_t* line;
    wchar_t* tokenContext = NULL;
    HKEY hKey = NULL;
    struct KeyGuard
    {
        CSalamanderRegistryExAbstractW* Registry;
        HKEY& Key;
        ~KeyGuard()
        {
            if (Key != NULL)
                Registry->CloseKey(Key);
        }
    } keyGuard = {pRegistry, hKey};
    eRPE_ERROR ret = RPE_OK;
    BOOL bReg5File;

    line = wcstok_s(buf, L"\r\n", &tokenContext);
    if (!line)
    {
        return RPE_INVALID_FORMAT;
    }
    if (*line == 0xFEFF && !wcscmp(line + 1, L"Windows Registry Editor Version 5.00"))
    {
        bReg5File = TRUE;
    }
    else if (!wcscmp(line, L"REGEDIT4"))
    {
        bReg5File = FALSE;
    }
    else
    {
        return RPE_NOT_REG_FILE;
    }
    while (NULL != (line = wcstok_s(NULL, L"\r\n", &tokenContext)))
    {
        if (!*line)
            continue; // empty line
        if (*line == '[')
        { // First char on the line -> MBCS-safe
            wchar_t* keyName;
            HKEY hParentKey;
            BOOL bDelete = FALSE;

            if (hKey)
            {
                pRegistry->CloseKey(hKey);
                hKey = NULL;
            }
            line++;
            if (line[0] == '-')
            {
                bDelete = TRUE;
                line++;
            }
            if (!_wcsnicmp(line, L"HKEY_CURRENT_USER", SizeOf(L"HKEY_CURRENT_USER") - 1))
            {
                keyName = line + SizeOf(L"HKEY_CURRENT_USER") - 1;
                hParentKey = HKEY_CURRENT_USER;
            }
            else if (!_wcsnicmp(line, L"HKEY_CLASSES_ROOT", SizeOf(L"HKEY_CLASSES_ROOT") - 1))
            {
                keyName = line + SizeOf(L"HKEY_CLASSES_ROOT") - 1;
                hParentKey = HKEY_CLASSES_ROOT;
            }
            else if (!_wcsnicmp(line, L"HKEY_LOCAL_MACHINE", SizeOf(L"HKEY_LOCAL_MACHINE") - 1))
            {
                keyName = line + SizeOf(L"HKEY_LOCAL_MACHINE") - 1;
                hParentKey = HKEY_LOCAL_MACHINE;
            }
            else if (!_wcsnicmp(line, L"HKEY_USERS", SizeOf(L"HKEY_USERS") - 1))
            {
                keyName = line + SizeOf(L"HKEY_USERS") - 1;
                hParentKey = HKEY_USERS;
            }
            else if (!_wcsnicmp(line, L"HKEY_CURRENT_CONFIG", SizeOf(L"HKEY_CURRENT_CONFIG") - 1))
            {
                keyName = line + SizeOf(L"HKEY_CURRENT_CONFIG") - 1;
                hParentKey = HKEY_CURRENT_CONFIG;
            }
            else if (!_wcsnicmp(line, L"HKEY_DYN_DATA", SizeOf(L"HKEY_DYN_DATA") - 1))
            {
                keyName = line + SizeOf(L"HKEY_DYN_DATA") - 1;
                hParentKey = HKEY_DYN_DATA;
            }
            else if (!_wcsnicmp(line, L"HKEY_PERFORMANCE_DATA", SizeOf(L"HKEY_PERFORMANCE_DATA") - 1))
            {
                keyName = line + SizeOf(L"HKEY_PERFORMANCE_DATA") - 1;
                hParentKey = HKEY_PERFORMANCE_DATA;
            }
            else
            {
                return RPE_ROOT_INVALID_KEY;
            }

            if (*keyName != ']')
            {
                if (*keyName == '\\')
                {
                    keyName++;
                }
                else
                {
                    return RPE_ROOT_INVALID_KEY;
                }
            }
            wchar_t* s = keyName;
            int nBrackets = 1;

            while (*s && nBrackets)
            {
                if (*s == '[')
                    nBrackets++;
                else if (*s == ']')
                    nBrackets--;
                s++;
            }
            if (nBrackets)
            {
                return RPE_INVALID_KEY;
            }
            s[-1] = 0;
            if (keyName[0] != 0) // root key cannot be created nor deleted this way (e.g. line with [HKEY_CURRENT_USER] or [-HKEY_CURRENT_USER])
            {
                if (!bDelete)
                {
                    if (!pRegistry->CreateKey(hParentKey, keyName, hKey))
                    {
                        return RPE_KEY_CREATE;
                    }
                }
                else
                {
                    if (!doNotDeleteHiddenKeysAndValues ||
                        !StrEndsWith(keyName, L".hidden", SizeOf(L".hidden") - 1))
                    {
                        // Delete entire subtree
                        if (pRegistry->OpenKey(hParentKey, keyName, hKey))
                        {
                            BOOL keyIsNotEmpty = FALSE;
                            pRegistry->ClearKeyEx(hKey, doNotDeleteHiddenKeysAndValues, &keyIsNotEmpty); // Delete the subtree
                            pRegistry->CloseKey(hKey);
                            if (!keyIsNotEmpty)
                                pRegistry->DeleteKey(hParentKey, keyName); // Delete the key itself
                        }
                        hKey = NULL;
                    }
                }
            }
            continue;
        }
        if ((*line == '\"') || (*line == '@'))
        { // First char on the line -> MBCS-safe
            wchar_t* s;

            if (*line != '@')
            {
                s = SeparateStr(++line);

                if (s == NULL)
                {
                    ret = RPE_VALUE_MISSING_QUOTE;
                    break;
                }
            }
            else
            {
                s = line + 1;
                *line = 0;
            }
            SkipWS(s);
            if (*s++ != '=')
            { // MBCS-safe
                ret = RPE_VALUE_MISSING_ASSIG;
                break;
            }
            SkipWS(s);
            if (!_wcsnicmp(s, L"dword:", SizeOf(L"dword:") - 1))
            {
                DWORD val;
                s += SizeOf(L"dword:") - 1;
                if (1 == swscanf_s(s, L"%x", &val))
                {
                    if (pRegistry->SetValue(hKey, line, REG_DWORD, &val, sizeof(val)))
                    {
                        continue;
                    }
                    ret = RPE_VALUE_SET;
                    break;
                }
                ret = RPE_VALUE_DWORD;
                break;
            }
            else if (!_wcsnicmp(s, L"hex", SizeOf(L"hex") - 1))
            {
                LPBYTE val, val2;
                int valType;

                s += SizeOf(L"hex") - 1;
                if (*s == ':')
                {
                    s++;
                    valType = REG_BINARY;
                }
                else
                {
                    // hex(b):01,02,03,04,05,06,07,08   <-- QWORD value
                    if (*s != '(')
                    {
                        ret = RPE_VALUE_INVALID_TYPE;
                        break;
                    }
                    wchar_t* s2 = ++s;
                    while (((*s2 >= '0') && (*s2 <= '9')) || ((*s2 >= 'a') && (*s2 <= 'f')) || ((*s2 >= 'A') && (*s2 <= 'F')))
                        s2++;
                    if ((*s2 != ')') || (s2[1] != ':'))
                    {
                        ret = RPE_VALUE_INVALID_TYPE;
                        break;
                    }
                    *s2 = 0;
                    if (swscanf_s(s, L"%x", &valType) != 1)
                    {
                        ret = RPE_VALUE_INVALID_TYPE;
                        break;
                    }
                    s = s2 + 2;
                }
                val = val2 = (LPBYTE)s;
                while (*s)
                {
                    if (*s == '\\')
                    { // MBCS-safe
                        s = wcstok_s(NULL, L"\r\n", &tokenContext);
                        if (!s)
                        {
                            ret = RPE_VALUE_HEX;
                            break;
                        }
                        SkipWS(s);
                    }
                    if ((*s >= '0') && (*s <= '9'))
                    {
                        *val2 = (BYTE)((*s++ - '0') << 4);
                    }
                    else if ((*s >= 'a') && (*s <= 'f'))
                    {
                        *val2 = (BYTE)((*s++ - 'a' + 10) << 4);
                    }
                    else if ((*s >= 'A') && (*s <= 'F'))
                    {
                        *val2 = (BYTE)((*s++ - 'A' + 10) << 4);
                    }
                    else
                    {
                        ret = RPE_VALUE_HEX;
                        break;
                    }
                    if ((*s >= '0') && (*s <= '9'))
                    {
                        *val2 |= (*s++ - '0');
                    }
                    else if ((*s >= 'a') && (*s <= 'f'))
                    {
                        *val2 |= (*s++ - 'a' + 10);
                    }
                    else if ((*s >= 'A') && (*s <= 'F'))
                    {
                        *val2 |= (*s++ - 'A' + 10);
                    }
                    else
                    {
                        ret = RPE_VALUE_HEX;
                        break;
                    }
                    val2++;
                    if (*s == ',')
                        s++; // MBCS-safe
                }
                if (*s)
                {
                    break; // ret is non-OK
                }
                LPBYTE valTmp = NULL;
                if (!bReg5File && ((valType == REG_MULTI_SZ) || (valType == REG_EXPAND_SZ)))
                {
                    size_t srcLen = val2 - val;
                    LPBYTE srcVal = val;
                    size_t len = srcLen * sizeof(WCHAR);

                    if (len > (size_t)((LPBYTE)line - (LPBYTE)buf))
                    {
                        valTmp = (LPBYTE)malloc(len);
                        if (!valTmp)
                        {
                            ret = RPE_OUT_OF_MEMORY;
                            break;
                        }
                        val = valTmp;
                    }
                    else
                    {
                        val = (LPBYTE)buf;
                    }
                    // Cope with W2K/XP/Vista/W7 bug: regedit exports just low bytes of such items i.s.o. conversion to CP_ACP
                    // We insert here zeros as high bytes
                    // But imports (at least on XP64) via CP_ACP :-(
                    size_t i;
                    for (i = 0; i < srcLen; i++)
                    {
                        val[2 * i] = srcVal[i];
                        val[2 * i + 1] = 0;
                    }
                    val2 = val + len;
                }
                if (pRegistry->SetValue(hKey, line, valType, val, (DWORD)(val2 - val)))
                {
                    if (valTmp)
                        free(valTmp);
                    continue;
                }
                if (valTmp)
                    free(valTmp);
                ret = RPE_VALUE_SET;
                break;
            }
            else if (*s == '\"')
            { // MBCS-safe
                wchar_t* val = ++s;
                s = SeparateStr(s);
                if (s)
                {
                    if (pRegistry->SetValue(hKey, line, REG_SZ, val, sizeof(wchar_t) * (DWORD)(wcslen(val) + 1)))
                    {
                        continue;
                    }
                    ret = RPE_VALUE_SET;
                    break;
                }
                ret = RPE_VALUE_STRING;
                break;
            }
            else if (*s == '-')
            {
                if (!doNotDeleteHiddenKeysAndValues ||
                    !StrEndsWith(line, L".hidden", SizeOf(L".hidden") - 1))
                {
                    // DeleteValue returns FALSE when the value did not exist
                    pRegistry->DeleteValue(hKey, line);
                }
            }
            else
            {
                ret = RPE_VALUE_INVALID_TYPE;
                break;
            }
        }
    }
    if (hKey)
    {
        pRegistry->CloseKey(hKey);
        hKey = NULL;
    }
    return ret;
}

eRPE_ERROR ConvertRegistryFileToUtf16(wchar_t** pBuf, DWORD size, DWORD& utf16ByteSize)
{
    utf16ByteSize = 0;
    if (pBuf == NULL || *pBuf == NULL)
        return RPE_INVALID_FORMAT;

    wchar_t* buf = *pBuf;

    if (size >= sizeof("REGEDIT4") - 1 &&
        !memcmp((const char*)buf, "REGEDIT4", sizeof("REGEDIT4") - 1))
    {
        if (size > INT_MAX)
            return RPE_INVALID_FORMAT;
        const int sizeW = MultiByteToWideChar(CP_ACP, 0, (char*)buf, static_cast<int>(size), NULL, 0);
        if (sizeW <= 0)
            return RPE_INVALID_MBCS;

        wchar_t* bufW = (wchar_t*)malloc((static_cast<size_t>(sizeW) + 1) * sizeof(wchar_t));
        if (!bufW)
            return RPE_OUT_OF_MEMORY;
        if (MultiByteToWideChar(CP_ACP, 0, (char*)buf, static_cast<int>(size), bufW, sizeW) != sizeW)
        {
            free(bufW);
            return RPE_INVALID_MBCS;
        }
        free(buf);
        buf = bufW;
        size = static_cast<DWORD>(sizeW * sizeof(wchar_t));
    }
    else if (size % sizeof(wchar_t) != 0)
        return RPE_INVALID_FORMAT;

    buf[size / sizeof(wchar_t)] = 0; // force NUL termination
    *pBuf = buf;
    utf16ByteSize = size;
    return RPE_OK;
}
