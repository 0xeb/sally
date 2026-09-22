// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "checksum.h"
#include "checksum.rh"
#include "checksum.rh2"
#include "lang\lang.rh"
#include "misc.h"

BOOL Error(HWND hParent, int lastErr, int title, int error)
{
    CALL_STACK_MESSAGE4("Error(, %d, %d, %d)", lastErr, title, error);
    std::wstring message = SPLLoadStrOwned(SalamanderGeneral, HLanguage, error).c_str();
    if (lastErr != ERROR_SUCCESS)
    {
        message += L" ";
        message += SPLGetErrorTextOwned(SalamanderGeneral, lastErr);
    }
    SalamanderGeneral->SalMessageBox(hParent, message.c_str(),
                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, title).c_str(),
                                     MSGBOXEX_OK | MSGBOXEX_ICONEXCLAMATION);

    return FALSE;
}

void GetFirstWord(char* str, int& pos, int& len, char delimitChar)
{
    CALL_STACK_MESSAGE_NONE // frequently called function
        // CALL_STACK_MESSAGE4("GetFirstWord(, %d, %d, %d)", pos, len, delimitChar);
        pos = 0;
    while (str[pos] && ((BYTE)str[pos] <= ' '))
        pos++;
    len = pos;
    while (str[len] && ((BYTE)str[len] > ' ') && str[len] != delimitChar)
        len++;
    len -= pos;
}

void GetLastWord(char* str, int& pos, int& len, char delimitChar)
{
    CALL_STACK_MESSAGE_NONE // frequently called function
        // CALL_STACK_MESSAGE4("GetLastWord(, %d, %d, %d)", pos, len, delimitChar);
        len = (int)strlen(str);
    while (len > 0 && ((BYTE)str[len - 1] <= ' '))
        len--;
    pos = len;
    while (pos > 0 && ((BYTE)str[pos - 1] > ' ') && str[pos - 1] != delimitChar)
        pos--;
    len -= pos;
}

BYTE hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0;
}

BOOL IsHex(const char* str, int len)
{
    CALL_STACK_MESSAGE2("IsHex(, %d)", len);
    for (int i = 0; i < len; i++, str++)
        if (!((*str >= '0' && *str <= '9') || (*str >= 'A' && *str <= 'F') || (*str >= 'a' && *str <= 'f')))
            return FALSE;
    return TRUE;
}
