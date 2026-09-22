// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Automation Plugin for Open Salamander
	
	Copyright (c) 2009-2023 Milan Kase <manison@manison.cz>
	Copyright (c) 2010-2023 Open Salamander Authors
	
	aututils.cpp
	Miscelaneous utility routines.
*/

#include "precomp.h"
#include "aututils.h"
#include "lang\lang.rh"
#include "knownengines.h"
#include "shim.h"
#include "scriptlist.h"

extern CSalamanderGeneralAbstract* SalamanderGeneral;
extern HINSTANCE g_hLangInst;

static int WINAPI ProtectedMessageBox(
    __in const MSGBOXEX_PARAMS* params)
{
    int harderror = -1;
    int res;

    __try
    {
        res = SalamanderGeneral->SalMessageBoxEx(params);
    }
    __except (GetExceptionCode() == STATUS_STACK_OVERFLOW ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        harderror = _resetstkoflw();
        res = -1;
    }

    return res;
}

static int DisplayError(
    __in EXCEPINFO* ei,
    __in LONG line,
    __in LONG col,
    __in_opt BSTR src,
    __in bool bDebug,
    __in_opt CScriptEngineShim* pShim)
{
    MSGBOXEX_PARAMS msgbox = {
        0,
    };
    int res;

    if (ei->scode == SALAUT_E_ABORT || (pShim != NULL && !pShim->DisplayErrorHook(ei, src, bDebug)))
    {
        // Salamander.AbortScript exception was raised
        return false;
    }

    msgbox.HParent = SalamanderGeneral->GetMsgBoxParent();
    const std::wstring caption =
        SPLLoadStrOwned(SalamanderGeneral, g_hLangInst, IDS_SCRIPTERROR);
    msgbox.Caption = caption.c_str();
    msgbox.Flags = MSGBOXEX_ICONHAND;
    std::wstring text;
    std::wstring aliasButtonNames;

    if (line >= 0)
        text += SPLFormatStringOwned(SPLLoadStrOwned(SalamanderGeneral, g_hLangInst, IDS_LINE).c_str(), line);

    if (col >= 0)
        text += SPLFormatStringOwned(SPLLoadStrOwned(SalamanderGeneral, g_hLangInst, IDS_COLUMN).c_str(), col);

    if (ei->bstrSource)
        text += SPLFormatStringOwned(SPLLoadStrOwned(SalamanderGeneral, g_hLangInst, IDS_SOURCE).c_str(), ei->bstrSource);

    if (ei->bstrDescription)
        text += SPLFormatStringOwned(SPLLoadStrOwned(SalamanderGeneral, g_hLangInst, IDS_ERROR).c_str(), ei->bstrDescription);

    msgbox.Text = text.c_str();

    if (bDebug)
    {
        msgbox.Flags |= MSGBOXEX_OKCANCEL;
        aliasButtonNames = SPLLoadStrOwned(SalamanderGeneral, g_hLangInst, IDS_DEBUGABORT);
    }
    else
    {
        msgbox.Flags |= MSGBOXEX_OK;
        aliasButtonNames = SPLLoadStrOwned(SalamanderGeneral, g_hLangInst, IDS_ABORT);
    }
    msgbox.AliasBtnNames = aliasButtonNames.c_str();

    res = ProtectedMessageBox(&msgbox);

    if (res < 0)
    {
        return res;
    }

    return bDebug && (res == DIALOG_OK);
}

static void AdjustSourcePosition(ULONG& line, LONG& col, CScriptEngineShim* pShim)
{
    // Some engines return zero-based line number, others one-based
    // try to normalize to one-base line numbers since it is more
    // natural for humans and editors start counting from one too.

    if (pShim != NULL)
    {
        LONG l = (LONG)line;
        pShim->AdjustSourcePosition(l, col);
        line = l;
    }
}

int DisplayException(IActiveScriptError* pError, CScriptEngineShim* pShim, bool bDebug)
{
    EXCEPINFO ei;
    EXCEPINFO* pei = &ei;
    DWORD ctx;
    ULONG line;
    LONG col;
    BSTR src;
    int res;

    if (FAILED(pError->GetExceptionInfo(pei)))
    {
        pei = NULL;
    }

    if (FAILED(pError->GetSourcePosition(&ctx, &line, &col)))
    {
        line = col = -1;
    }
    else
    {
        AdjustSourcePosition(line, col, pShim);
    }

    if (FAILED(pError->GetSourceLineText(&src)))
    {
        src = NULL;
    }

    res = DisplayError(pei, line, col, src, bDebug, pShim);

    if (src != NULL)
    {
        SysFreeString(src);
    }

    if (pei != NULL)
    {
        FreeException(*pei);
    }

    return res;
}

void DisplayException(EXCEPINFO& ei, bool bFree)
{
    DisplayError(&ei, -1, -1, NULL, false, NULL);
    if (bFree)
    {
        FreeException(ei);
    }
}

void FreeException(EXCEPINFO& ei)
{
    if (ei.bstrSource)
    {
        SysFreeString(ei.bstrSource);
        ei.bstrSource = NULL;
    }

    if (ei.bstrDescription)
    {
        SysFreeString(ei.bstrDescription);
        ei.bstrDescription = NULL;
    }

    if (ei.bstrHelpFile)
    {
        SysFreeString(ei.bstrHelpFile);
        ei.bstrHelpFile = NULL;
    }
}

void FormatErrorText(
    HRESULT hrCode,
    wchar_t* pszBuffer,
    UINT cchMax)
{
    UINT facility = HRESULT_FACILITY(hrCode);
    UINT code;
    HRESULT hr;
    wchar_t* end;
    size_t remaining;

    if (facility == FACILITY_WIN32 || HIWORD(hrCode) == 0)
    {
        code = HRESULT_CODE(hrCode);

        hr = StringCchPrintfExW(pszBuffer, cchMax, &end, &remaining, 0,
                                L"(%u) ", code);
    }
    else
    {
        code = hrCode;

        hr = StringCchPrintfExW(pszBuffer, cchMax, &end, &remaining, 0,
                                L"(0x%08X) ", code);
    }

    if (SUCCEEDED(hr))
    {
        DWORD len = FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            code,
            0,
            end,
            (DWORD)remaining,
            NULL);

        if (len > 0)
        {
            // trim CRLF
            end += len - 1;
            while (end > pszBuffer && iswspace(*end))
            {
                *end = L'\0';
                --end;
            }
        }
        else
        {
            LoadStringW(g_hLangInst, IDS_UNKERROR, end, (int)remaining);
        }
    }
}

UINT HashString(__in_z PCWSTR s)
{
    UINT uHash;

    uHash = 0;
    while (*s != L'\0')
    {
        uHash = *s + (uHash << 6) + (uHash << 16) - uHash;
        ++s;
    }

    return uHash;
}

void QuadWordToVariant(const CQuadWord& q, __out VARIANT* var)
{
    if (q.HiDWord == 0 && q.LoDWord <= _I32_MAX)
    {
        // UI4 is not an automation type
        // and VB simply cannot handle it
        V_VT(var) = VT_I4;
        V_I4(var) = q.LoDWord;
    }
    else
    {
        // 64b integer is not OLE Automation type, coerce to double
        V_VT(var) = VT_R8;
        V_R8(var) = (double)q.Value;
    }
}

void QuadWordToVariant(LARGE_INTEGER q, __out VARIANT* var)
{
    if (q.HighPart == 0 && q.LowPart <= _I32_MAX)
    {
        // UI4 is not an automation type
        // and VB simply cannot handle it
        V_VT(var) = VT_I4;
        V_I4(var) = q.LowPart;
    }
    else
    {
        // 64b integer is not OLE Automation type, coerce to double
        V_VT(var) = VT_R8;
        V_R8(var) = (double)q.QuadPart;
    }
}

HRESULT DispPropGet(IDispatch* pdisp, PCWSTR name, VARIANT* result)
{
    HRESULT hr;
    DISPID dispid;
    DISPPARAMS params = {
        0,
    };
    EXCEPINFO ei = {
        0,
    };
    UINT argerr = 0;
    LCID lcid = GetUserDefaultLCID();

    hr = pdisp->GetIDsOfNames(IID_NULL, (LPOLESTR*)&name, 1, lcid, &dispid);
    if (FAILED(hr))
    {
        return hr;
    }

    hr = pdisp->Invoke(dispid, IID_NULL, lcid, DISPATCH_PROPERTYGET,
                       &params, result, &ei, &argerr);

    return hr;
}

HRESULT DispPropGet(IUnknown* punk, PCWSTR name, VARIANT* result)
{
    HRESULT hr;
    IDispatch* pdisp;

    hr = punk->QueryInterface(IID_IDispatch, (void**)&pdisp);
    if (SUCCEEDED(hr))
    {
        hr = DispPropGet(pdisp, name, result);
        pdisp->Release();
    }

    return hr;
}

int ButtonToFriendlyNumber(int button)
{
    switch (button)
    {
    case DIALOG_SKIP:
        return 16;

    case DIALOG_SKIPALL:
        return 17;

    case DIALOG_ALL:
        return 18;
    }

    return button;
}

static void BufferKey(WORD vk, BOOL bExtended = false, BOOL down = true, BOOL up = true)
{
    INPUT input;
    if (down)
    {
        // generate down
        ::ZeroMemory(&input, sizeof(input));
        input.type = INPUT_KEYBOARD;
        if (bExtended)
            input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        input.ki.wVk = vk;
        ::SendInput(1, &input, sizeof(input));
    }
    if (up)
    {
        // generate up
        ::ZeroMemory(&input, sizeof(input));
        input.type = INPUT_KEYBOARD;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        if (bExtended)
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        input.ki.wVk = vk;
        ::SendInput(1, &input, sizeof(input));
    }
}

void ResetKeyboardState()
{
    BOOL controlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    BOOL shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (controlPressed)
        BufferKey(VK_CONTROL, false, false, true);
    if (altPressed)
        BufferKey(VK_MENU, false, false, true);
    if (shiftPressed)
        BufferKey(VK_SHIFT, false, false, true);
}

HRESULT CheckAbort(__in class CScriptInfo* pScriptInfo, __out EXCEPINFO* pExcepInfo)
{
    if (pScriptInfo->IsAbortPending())
    {
        if (pExcepInfo != NULL)
        {
            pScriptInfo->GetShim()->InitAbortException(pExcepInfo);
        }
        return DISP_E_EXCEPTION;
    }

    return S_OK;
}
