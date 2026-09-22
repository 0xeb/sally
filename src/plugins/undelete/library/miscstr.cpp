// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "..\undelete.rh2"

#include "miscstr.h"
#include "os.h"
#include "volume.h"
#include "snapshot.h"
#include "volenum.h"

#include "../dialogs.h"
#include "../undelete.h"

extern HINSTANCE DLLInstance;

template <>
std::wstring String<wchar_t>::LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

// ****************************************************************************
//
//  Error message functions
//

extern HWND HProgressDlg;

HWND GetParentHWND()
{
    HWND hParent = SalamanderGeneral->GetMsgBoxParent();
    if (HProgressDlg != NULL)
        hParent = HProgressDlg;
    return hParent;
}

// ****************************************************************************
//
// String<wchar_t> specializations used by the native-wide recovery engine.

template <>
void String<wchar_t>::VSPrintF(wchar_t* buffer, const wchar_t* pattern, va_list& marker)
{
    _vswprintf(buffer, pattern, marker);
}

template <>
int String<wchar_t>::StrCmp(const wchar_t* string1, const wchar_t* string2)
{
    return wcscmp(string1, string2);
}

template <>
int String<wchar_t>::StrICmp(const wchar_t* string1, const wchar_t* string2)
{
    return _wcsicmp(string1, string2);
}

template <>
size_t String<wchar_t>::StrLen(const wchar_t* text)
{
    return wcslen(text);
}

template <>
wchar_t String<wchar_t>::ToUpper(wchar_t c)
{
    return towupper(c);
}

template <>
wchar_t* String<wchar_t>::StrCpy(wchar_t* text1, const wchar_t* text2)
{
    return wcscpy(text1, text2);
}

template <>
wchar_t* String<wchar_t>::StrCat(wchar_t* text1, const wchar_t* text2)
{
    return wcscat(text1, text2);
}

template <>
errno_t String<wchar_t>::StrCat_s(wchar_t* strDestination, size_t numberOfElements, const wchar_t* strSource)
{
    return wcscat_s(strDestination, numberOfElements, strSource);
}

template <>
wchar_t* String<wchar_t>::AddNumberSuffix(wchar_t* filename, int n)
{
    wchar_t* ext = wcsrchr(filename, L'.'); // ".cvspass" is extension in Windows
    if (ext != NULL && (ext - filename) != ((int)wcslen(filename) - 4))
        ext = NULL;

    std::wstring temp;
    if (ext == NULL)
    {
        temp = filename;
        temp += L" (" + std::to_wstring(n) + L")";
    }
    else
    {
        temp.assign(filename, ext);
        temp += L" (" + std::to_wstring(n) + L")";
        temp += ext;
    }

    return NewStr(temp.c_str());
}

template <>
wchar_t* String<wchar_t>::CopyFromASCII(wchar_t* dest, const char* src, unsigned long srclen, unsigned long destlen)
{
    if (destlen)
        dest[0] = 0;
    if (srclen)
    {
        if (destlen > srclen)
        {
            // The only caller of NewFromASCII/CopyFromASCII in
            // the FAT/NTFS engine is the 8.3 short-name path (fat.h's ConvertFATName
            // output) - genuinely OEM-codepage bytes on disk (matches how
            // Explorer/cmd.exe interpret FAT 8.3 names), not the system ANSI
            // codepage. CP_OEMCP here is the named on-disk short-name boundary.
            // srclen counts BYTES; on a multi-byte OEM code page the converted name is
            // shorter, so the terminator has to go where MultiByteToWideChar actually
            // stopped. Terminating at srclen leaves uninitialized characters from
            // NewFromASCII's raw allocation inside the name.
            const int converted = MultiByteToWideChar(CP_OEMCP, 0, src, srclen, dest, destlen);
            if (converted == 0)
            {
                DWORD err = GetLastError();
                TRACE_E("CopyFromASCII error: MultiByteToWideChar failed: " << err);
            }
            dest[converted > 0 ? (unsigned long)converted : 0] = 0;
        }
        else
            TRACE_E("CopyFromASCII error: small dest buffer");
    }
    return dest;
}

template <>
wchar_t* String<wchar_t>::NewFromASCII(const char* src)
{
    unsigned long srclen = (unsigned long)strlen(src);
    wchar_t* dest = new wchar_t[srclen + 1];
    if (dest)
        return CopyFromASCII(dest, src, srclen, srclen + 1);
    else
        return NULL;
}

template <>
wchar_t* String<wchar_t>::CopyFromUnicode(wchar_t* dest, const wchar_t* src, unsigned long srclen, unsigned long destlen)
{
    // For CHAR=wchar_t, "Unicode" source and CHAR
    // destination are the same width, so preserve the UTF-16 name exactly.
    if (destlen)
        dest[0] = 0;
    if (srclen)
    {
        if (destlen > srclen)
        {
            memcpy(dest, src, srclen * sizeof(wchar_t));
            dest[srclen] = 0;
        }
        else
            TRACE_E("CopyFromUnicode error: small dest buffer");
    }
    return dest;
}

template <>
wchar_t* String<wchar_t>::NewFromUnicode(const wchar_t* src, unsigned long srclen)
{
    wchar_t* dest = new wchar_t[srclen + 1];
    if (dest)
        return CopyFromUnicode(dest, src, srclen, srclen + 1);
    else
        return NULL;
}

template <>
wchar_t* String<wchar_t>::CopyToUnicode(wchar_t* dest, const wchar_t* src, unsigned long srclen, unsigned long destlen)
{
    // CHAR=wchar_t: src is already wide - direct copy,
    // same reasoning as CopyFromUnicode above.
    if (destlen)
        dest[0] = 0;
    if (srclen)
    {
        if (destlen > srclen)
        {
            memcpy(dest, src, srclen * sizeof(wchar_t));
            dest[srclen] = 0;
        }
        else
            TRACE_E("CopyToUnicode error: small dest buffer");
    }
    return dest;
}

template <>
BOOL String<wchar_t>::SysError(int title, int error, ...)
{
    int lastErr = GetLastError();
    CALL_STACK_MESSAGE3("SysError(%d, %d, ...)", title, error);
    va_list arglist;
    va_start(arglist, error);
    const std::wstring format = LangStr(error);
    std::wstring message = SPLFormatStringOwnedV(format.c_str(), arglist);
    va_end(arglist);
    if (lastErr != ERROR_SUCCESS)
    {
        message += L" ";
        message += SPLGetErrorTextOwned(SalamanderGeneral, lastErr);
    }
    HWND hParent = GetParentHWND();
    const std::wstring caption = LangStr(title);
    SalamanderGeneral->SalMessageBox(hParent, message.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
    return FALSE;
}

template <>
BOOL String<wchar_t>::Error(int title, int error, ...)
{
    CALL_STACK_MESSAGE3("Error(%d, %d, ...)", title, error);
    va_list arglist;
    va_start(arglist, error);
    const std::wstring format = LangStr(error);
    const std::wstring message = SPLFormatStringOwnedV(format.c_str(), arglist);
    va_end(arglist);
    HWND hParent = GetParentHWND();
    const std::wstring caption = LangStr(title);
    SalamanderGeneral->SalMessageBox(hParent, message.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
    return FALSE;
}

template <>
int String<wchar_t>::PartialRestore(int checkBoxText, BOOL* checkBoxValue, int title, int text, ...)
{
    CALL_STACK_MESSAGE3("PartialRestore(%d, %d, ...)", title, text);
    va_list arglist;
    va_start(arglist, text);
    const std::wstring format = LangStr(text);
    const std::wstring message = SPLFormatStringOwnedV(format.c_str(), arglist);
    va_end(arglist);
    HWND hParent = GetParentHWND();
    const std::wstring caption = LangStr(title);
    const std::wstring checkBox = checkBoxText != -1 ? LangStr(checkBoxText) : std::wstring();

    MSGBOXEX_PARAMS mbep;
    memset(&mbep, 0, sizeof(mbep));
    mbep.HParent = hParent;
    mbep.Text = message.c_str();
    mbep.Caption = caption.c_str();
    mbep.Flags = MB_YESNOCANCEL | MSGBOXEX_ICONQUESTION;
    if (checkBoxText != -1)
    {
        mbep.CheckBoxText = checkBox.c_str();
        mbep.CheckBoxValue = checkBoxValue;
    }

    return SalamanderGeneral->SalMessageBoxEx(&mbep);
}
