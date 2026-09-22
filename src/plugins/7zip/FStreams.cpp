// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "FStreams.h"
#include "7Zip.h"
#include "7zclient.h"
#include "7zthreads.h"
#include "7zip.rh"
#include "7zip.rh2"
#include "lang\lang.rh"

BOOL ShowRetryAbortBox(HWND hParentWnd, int resID, DWORD err, ...)
{
    va_list arglist;
    va_start(arglist, err);
    std::wstring msg = SPLFormatStringOwnedV(SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID).c_str(), arglist);
    va_end(arglist);

    if (msg.compare(0, 3, L"{!}") == 0)
    {
        va_list pluralArgs;
        va_start(pluralArgs, err);
        CQuadWord pluralValue;
        pluralValue.SetUI64(va_arg(pluralArgs, unsigned int));
        va_end(pluralArgs);

        msg = SPLExpandPluralStringOwned(SalamanderGeneral, msg.c_str(), 1,
                                         &pluralValue);
    }
    const std::wstring errorText = SPLGetErrorTextOwned(SalamanderGeneral, err);
    const std::wstring text = SPLFormatStringOwned(L"%ls\n\n%ls", msg.c_str(),
                                                   errorText.c_str());
    /* used by the export_mnu.py script, which generates salmenu.mnu for the Translator
   let the message box buttons handle hotkey collisions by simulating a menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_BTN_RETRY
  {MNTT_IT, IDS_BTN_ABORT
  {MNTT_PE, 0
};
*/
    const std::wstring btnBuffer = SPLFormatStringOwned(
        L"%d\t%s\t%d\t%s", DIALOG_RETRY, LangStr(IDS_BTN_RETRY).c_str(),
        DIALOG_CANCEL, LangStr(IDS_BTN_ABORT).c_str());

    MSGBOXEX_PARAMS mbep;
    const std::wstring caption = LangStr(IDS_PLUGINNAME).c_str();
    ZeroMemory(&mbep, sizeof(mbep));
    mbep.HParent = hParentWnd;
    mbep.Caption = caption.c_str();
    mbep.Text = text.c_str();
    mbep.Flags = MSGBOXEX_RETRYCANCEL | MSGBOXEX_ICONEXCLAMATION;
    mbep.AliasBtnNames = btnBuffer.c_str();
    if (hParentWnd)
    {
        return SendMessage(hParentWnd, WM_7ZIP, WM_7ZIP_SHOWMBOXEX, (LPARAM)&mbep) == DIALOG_RETRY;
    }
    else
    {
        // This can only happen when reading an archive file
        mbep.HParent = SalamanderGeneral->GetMsgBoxParent();
        return SalamanderGeneral->SalMessageBoxEx(&mbep) == DIALOG_RETRY;
    }
}

CRetryableOutFileStream::CRetryableOutFileStream(HWND _hParentWnd) : hParentWnd(_hParentWnd)
{
}

STDMETHODIMP CRetryableOutFileStream::Write(const void* data, UInt32 size, UInt32* processedSize)
{
    UInt32 written;
    HRESULT ret;

    if (processedSize)
        *processedSize = 0;
    while ((S_OK != (ret = COutFileStream::Write(data, size, &written))) /*|| (size != written)*/)
    {
        // NOTE: COutFileStream::Write writes at most kChunkSizeMax bytes (4MB) at once
        data = (char*)data + written;
        size -= written;
        if (!ShowRetryAbortBox(hParentWnd, IDS_CANT_WRITE, ret & 0xFFFF, size))
        {
            ret = E_ABORT;
            break;
        }
        if (processedSize)
            *processedSize += written;
    }
    if (processedSize)
        *processedSize += written;

    return ret;
}

CRetryableInFileStream::CRetryableInFileStream(HWND _hParentWnd) : hParentWnd(_hParentWnd)
{
}

STDMETHODIMP CRetryableInFileStream::Read(void* data, UInt32 size, UInt32* processedSize)
{
    UInt32 read;
    HRESULT ret;

    if (processedSize)
        *processedSize = 0;
    while (S_OK != (ret = CInFileStream::Read(data, size, &read)))
    {
        data = (char*)data + read;
        size -= read;
        if (!ShowRetryAbortBox(hParentWnd, hParentWnd ? IDS_CANT_READ : IDS_CANT_READ_ARCHIVE, ret & 0xFFFF, size))
        {
            ret = E_ABORT;
            break;
        }
        if (processedSize)
            *processedSize += read;
    }
    if (processedSize)
        *processedSize += read;

    return ret;
}
