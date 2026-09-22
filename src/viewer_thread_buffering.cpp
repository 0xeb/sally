// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// WIDE-WINDOW: the CViewerWindow this file writes captions into is created wide,
// so its SetWindowTextW(HWindow, ...) calls reach the screen intact and are NOT
// re-narrowed by USER32. Evidence, all in the non-_UNICODE branch that this build
// takes:
//   viewer.cpp registers CVIEWERWINDOW_CLASSNAMEW through CWindow's wide-only adapter.
//   This unit creates it through the same wide-only adapter.
//   viewer.cpp:555   CViewerWindow ctor passes CWindow(origin, TRUE)
// gtest_window_width_probe already proves the chain itself: a window created with
// CreateWindowExW on a RegisterClassW class reports IsWindowUnicode == TRUE.

#include "precomp.h"
#include "common/text/EncodingDetector.h"
#include "viewer_line_index.h"
#include "viewer_find_seed.h"

#include "viewer.h"
#include "codetbl.h"

#include "cfgdlg.h"
#include "dialogs.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"

#include <new>
#include <stdexcept>

// ****************************************************************************

struct CTVData
{
    int Left, Top, Width, Height;
    CViewerWindow* View;
    const wchar_t* Name; // was a narrow/wide pair
    UINT ShowCmd;
    BOOL Success;
    const wchar_t* Caption;
    BOOL WholeCaption;
};

HANDLE ViewerContinue = NULL;

void ThreadViewerMessageLoopBodyAux()
{
    __try
    {
        OleUninitialize();
        //    CoUninitialize();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        OCUExceptionHasOccured++;
    }
}

unsigned ThreadViewerMessageLoopBody(void* parameter)
{
    CALL_STACK_MESSAGE1("ThreadViewerMessageLoopBody(): (text/hex viewer)");
    SetThreadNameInVCAndTrace(L"Viewer");
    TRACE_I("Begin");
    //  TRACE_I("MoresStanislav: ThreadViewerMessageLoopBody 1");
    CTVData* data = (CTVData*)parameter;
    CViewerWindow* view = data->View;
    std::wstring nameW;
    if (data->Name != NULL && data->Name[0] != 0)
        nameW = data->Name;
    std::wstring caption;
    const bool hasCaption = data->Caption != NULL;
    BOOL wholeCaption = FALSE;
    if (hasCaption)
    {
        caption = data->Caption;
        wholeCaption = data->WholeCaption;
    }
    UINT showCmd = data->ShowCmd;

    //  TRACE_I("MoresStanislav: ThreadViewerMessageLoopBody 2");
    //  CALL_STACK_MESSAGE1("MoresStanislav: ThreadViewerMessageLoopBody 2");
    data->Success = /*CoInitialize(NULL) == S_OK &&*/ OleInitialize(NULL) == S_OK;
    //  TRACE_I("MoresStanislav: ThreadViewerMessageLoopBody 3 succes="<<data->Success);
    //  CALL_STACK_MESSAGE1("MoresStanislav: ThreadViewerMessageLoopBody 3");

    // This used to fork on _UNICODE with the branches genuinely backwards (narrow
    // calls under #ifdef _UNICODE, wide under #else) - since _UNICODE is undefined in the real
    // build, the #else (wide) branch is the ONLY one that has ever actually run; the #ifdef
    // _UNICODE branch was dead code, never compiled until the canary. Removing the fork changes
    // no real-build behavior (it already always took this path) and fixes the canary, which
    // would otherwise take the dead, genuinely-broken narrow branch. CVIEWERWINDOW_CLASSNAMEW
    // is itself #ifndef _UNICODE-only - a redundant narrow-build-only duplicate of the already-
    // unconditional, already-wide CVIEWERWINDOW_CLASSNAME (identical string, see viewer.cpp) -
    // so this uses the always-available one instead of adding a third redundant constant.
    if (data->Success &&
        view->CreateEx(Configuration.AlwaysOnTop ? WS_EX_TOPMOST : 0,
                        CVIEWERWINDOW_CLASSNAME,
                        LoadStrW(IDS_VIEWERTITLE),
                        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
                        data->Left,
                        data->Top,
                        data->Width,
                        data->Height,
                        NULL,
                        ViewerMenu,
                        HInstance,
                        view) != NULL)
    {
        //    TRACE_I("MoresStanislav: ThreadViewerMessageLoopBody 4");
        //    CALL_STACK_MESSAGE1("MoresStanislav: ThreadViewerMessageLoopBody 4");
        view->SetObjectOrigin(ooAllocated); // switch from ooStatic because the window was created successfully
        data->Success = TRUE;
        // show the window immediately so it does not annoyingly "pop up" later
        //    TRACE_I("MoresStanislav: ThreadViewerMessageLoopBody 5");
        //    CALL_STACK_MESSAGE1("MoresStanislav: ThreadViewerMessageLoopBody 5");
        ShowWindow(view->HWindow, showCmd);
        //    TRACE_I("MoresStanislav: ThreadViewerMessageLoopBody 6");
        //    CALL_STACK_MESSAGE1("MoresStanislav: ThreadViewerMessageLoopBody 6");
        SetForegroundWindow(view->HWindow); // bug from 1.6 beta 1
                                            //    TRACE_I("MoresStanislav: ThreadViewerMessageLoopBody 7");
                                            //    CALL_STACK_MESSAGE1("MoresStanislav: ThreadViewerMessageLoopBody 7");
        UpdateWindow(view->HWindow);
        //    TRACE_I("MoresStanislav: ThreadViewerMessageLoopBody 8");
        //    CALL_STACK_MESSAGE1("MoresStanislav: ThreadViewerMessageLoopBody 8");
    }
    else
        data->Success = FALSE;

    //  TRACE_I("MoresStanislav: ThreadViewerMessageLoopBody 9");
    //  CALL_STACK_MESSAGE1("MoresStanislav: ThreadViewerMessageLoopBody 9");
    BOOL ok = data->Success;
    data = NULL;              // no longer valid afterwards
    SetEvent(ViewerContinue); // let the main thread continue
                              //  TRACE_I("MoresStanislav: ThreadViewerMessageLoopBody 10");
                              //  CALL_STACK_MESSAGE1("MoresStanislav: ThreadViewerMessageLoopBody 10");

    if (ok) // if the window was created, run the application loop
    {
        CALL_STACK_MESSAGE1("ThreadViewerMessageLoopBody::message_loop");
        // the SalGetFullName/OpenFile fallback is gone with the narrow half
        // it read from; nameW is the only representation CTVData carries now.
        if (SalGetFullNameW(nameW))
            view->OpenFile(nameW.c_str(), hasCaption ? caption.c_str() : NULL, wholeCaption);

        MSG msg;
        HWND viewHWindow = view->HWindow; // because WM_QUIT leaves the window object unallocated
        while (GetMessageW(&msg, NULL, 0, 0))
        {
            if (!TranslateAccelerator(viewHWindow, ViewerTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    ThreadViewerMessageLoopBodyAux();

    TRACE_I("End");
    return ok ? 0 : 1;
}

unsigned ThreadViewerMessageLoopEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return ThreadViewerMessageLoopBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread ViewerMessageLoop: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // more forceful exit (this variant still executes some handlers)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI ThreadViewerMessageLoop(void* param)
{
    CCallStack stack;
    return ThreadViewerMessageLoopEH(param);
}

// was a narrow OpenViewer wrapper around OpenViewerW(nameW, nameA, ...).
// viewer.h had already declared this wide; the narrow definition and the W twin are gone.
BOOL OpenViewer(const wchar_t* name, CViewType mode, int left, int top,
                int width, int height, UINT showCmd, BOOL returnLock, HANDLE* lock,
                BOOL* lockOwner, CSalamanderPluginViewerData* viewerData,
                int enumFileNamesSourceUID, int enumFileNamesLastFileIndex)
{
    CALL_STACK_MESSAGE11("OpenViewer(%ls, %d, %d, %d, %d, %d, %u, %d, , , , %d, %d)",
                         name != NULL ? name : L"", mode, left, top, width, height, showCmd, returnLock,
                         enumFileNamesSourceUID, enumFileNamesLastFileIndex);
    CSalamanderPluginInternalViewerData* intViewerData = NULL;
    if (viewerData != NULL && viewerData->Size == sizeof(CSalamanderPluginInternalViewerData))
    {
        intViewerData = (CSalamanderPluginInternalViewerData*)viewerData;
        mode = (intViewerData->Mode == 0) ? vtText : vtHex;
    }
    CViewerWindow* view = new CViewerWindow(NULL, mode, NULL, FALSE, ooStatic,
                                            enumFileNamesSourceUID, enumFileNamesLastFileIndex);
    if (view != NULL)
    {
        view->InitFindDialog(GlobalFindDialog);
        if (returnLock)
        {
            *lock = view->GetLockObject();
            *lockOwner = TRUE;
        }
    }
    if (view != NULL && view->IsGood() && (!returnLock || *lock != NULL))
    {
        CTVData data;
        data.View = view;
        data.Left = left;
        data.Top = top;
        data.Width = width;
        data.Height = height;
        data.Name = name;
        data.ShowCmd = showCmd;
        data.Caption = intViewerData != NULL ? intViewerData->Caption : NULL;
        data.WholeCaption = intViewerData != NULL ? intViewerData->WholeCaption : FALSE;

        DWORD ThreadID;
        HANDLE loop = HANDLES(CreateThread(NULL, 0, ThreadViewerMessageLoop, &data, 0, &ThreadID));
        if (loop == NULL)
        {
            TRACE_E("Unable to start ViewerMessageLoop thread.");
            goto ERROR_TV_CREATE;
        }
        else
        {
            //      SetThreadPriority(loop, THREAD_PRIORITY_HIGHEST);
        }
        AddAuxThread(loop);                            // register the thread among existing viewers (terminate it on exit)
        WaitForSingleObject(ViewerContinue, INFINITE); // wait until the thread finishes its startup
        if (!data.Success)
            goto ERROR_TV_CREATE;
        return TRUE;
    }
    else
    {
        TRACE_E("Insufficient memory for viewer or unable to get font for viewer.");

    ERROR_TV_CREATE:

        if (view != NULL && returnLock && *lock != NULL)
            view->CloseLockObject();
        if (view != NULL)
            delete view;
        return FALSE;
    }
}

//*****************************************************************************
//
// RegExpErrorText
//
// error messages from regexp.cpp
//

const char* RegExpErrorText(CRegExpErrors err)
{
    if (err < reeNoError || err > reeInternalDisaster)
        return "";

    // The Spencer engine and frozen v107 facade expose byte error pointers. Own
    // those projections here, refresh them when the language module changes, and
    // reject lossy ACP text rather than leaking narrow resource ownership to core.
    static const int resourceIds[] = {
        IDS_REGEXPERROR1, IDS_REGEXPERROR2, IDS_REGEXPERROR3, IDS_REGEXPERROR4,
        IDS_REGEXPERROR5, IDS_REGEXPERROR6, IDS_REGEXPERROR7, IDS_REGEXPERROR8,
        IDS_REGEXPERROR9, IDS_REGEXPERROR10, IDS_REGEXPERROR11, IDS_REGEXPERROR12,
        IDS_REGEXPERROR13};
    static thread_local HINSTANCE loadedLanguage = NULL;
    static thread_local std::string errorTexts[_countof(resourceIds)];
    if (loadedLanguage != HLanguage)
    {
        for (size_t i = 0; i < _countof(resourceIds); ++i)
        {
            if (!Win32EncodeAcpExact(LoadStrOwned(resourceIds[i]), errorTexts[i]))
                errorTexts[i] = "Regular expression error.";
        }
        loadedLanguage = HLanguage;
    }
    return errorTexts[static_cast<size_t>(err)].c_str();
}

//
//*****************************************************************************
// CViewerWindow
//

void CViewerWindow::ConfigHasChanged()
{
    CALL_STACK_MESSAGE1("CViewerWindow::ConfigHasChanged()");
    BOOL fatalErr = FALSE;
    FileChanged(NULL, FALSE, fatalErr, FALSE); // restart viewer
    if (fatalErr)
        FatalFileErrorOccured();
    if (fatalErr || ExitTextMode)
        return;
    InvalidateRect(HWindow, NULL, FALSE);
}

__int64
CViewerWindow::Prepare(HANDLE* hFile, __int64 offset, __int64 bytes, BOOL& fatalErr)
{
    fatalErr = FALSE;
    if (Seek <= offset)
        if (Seek + Loaded >= offset + bytes)
            return bytes; // o.k.
        else
        {
            if (Seek + Loaded == FileSize) // data loaded up to the end of the file
                if (Seek + Loaded > offset)
                    return Seek + Loaded - offset;
                else
                    return 0; // end of file

            if (offset + bytes - (Seek + Loaded) < VIEW_BUFFER_SIZE / 2 &&
                (Loaded <= VIEW_BUFFER_SIZE / 2 ||
                 Seek + VIEW_BUFFER_SIZE / 2 <= offset))
            {
                if (!LoadBehind(hFile))
                    fatalErr = TRUE;
            }
            else
            {
                Seek = offset;
                Loaded = 0;
                if (!LoadBehind(hFile))
                    fatalErr = TRUE;
            }
        }
    else // offset < Seek
    {
        if (Seek - offset < VIEW_BUFFER_SIZE / 2)
        {
            if (!LoadBefore(hFile))
                fatalErr = TRUE;
        }
        else
        {
            Seek = offset;
            Loaded = 0;
            if (!LoadBehind(hFile))
                fatalErr = TRUE;
        }
    }
    if (Seek <= offset)
        if (Seek + Loaded >= offset + bytes)
            return bytes; // o.k.
        else
            return Seek + Loaded > offset ? Seek + Loaded - offset : 0; // shortened
    else
        return 0; // nothing is usable (because the beginning was not loaded)
}

void CViewerWindow::CodeCharacters(unsigned char* start, unsigned char* end)
{
    if (UseCodeTable)
    {
        unsigned char* s = start - 1;
        while (++s < end)
            *s = CodeTable[*s];
    }
}

BOOL CViewerWindow::LoadBefore(HANDLE* hFile)
{
    CALL_STACK_MESSAGE1("CViewerWindow::LoadBefore()");
    if (FileNameW.empty())
        return FALSE;

    HANDLE file;
    if (hFile == NULL || *hFile == NULL)
    {
        file = OpenViewedFile(FILE_FLAG_SEQUENTIAL_SCAN);
        if (hFile != NULL && file != INVALID_HANDLE_VALUE)
            *hFile = file;
    }
    else
        file = *hFile;

    if (file != INVALID_HANDLE_VALUE)
    {
        CQuadWord size;
        DWORD err;
        BOOL haveSize = SalGetFileSize(file, size, err);
        if (!haveSize || size.Value != (unsigned __int64)FileSize) // error or file change
        {
            TRACE_I("The size of the viewed file has changed or some error occured.");
            // PostMessage(HWindow, WM_COMMAND, CM_REREADFILE, 0);  // legacy, unnecessary: it causes a "fatal error" and triggers a repaint
            Seek = Loaded = 0;
            if (hFile == NULL) // if the caller does not close the handle, it is up to us
                HANDLES(CloseHandle(file));
            return FALSE;
        }
        int read;
        if (Seek >= VIEW_BUFFER_SIZE / 2)
            read = VIEW_BUFFER_SIZE / 2;
        else
            read = (int)Seek;
        if (read == 0)
        {
            TRACE_E("Incorrect call to LoadBefore.");
            if (hFile == NULL) // if the caller does not close the handle, it is up to us
                HANDLES(CloseHandle(file));
            return FALSE;
        }
        if (Loaded > 0)
        {
            int space = VIEW_BUFFER_SIZE - read;
            memmove(Buffer + read, Buffer, (int)((space < Loaded) ? (Loaded = space) : Loaded));
            Seek -= read;
        }
        DWORD readed;
        BOOL ret;
        BOOL kill = FALSE; // TRUE means that FileName will be cleared on error
        CQuadWord resSeek;
        resSeek.SetUI64(Seek); // note, the seek for SetFilePointer is a signed value
        resSeek.LoDWord = SetFilePointer(file, resSeek.LoDWord, (PLONG)&resSeek.HiDWord, FILE_BEGIN);
        err = GetLastError();

        if ((resSeek.LoDWord != INVALID_SET_FILE_POINTER || err == NO_ERROR) && // no error
            resSeek.Value == (unsigned __int64)Seek)                            // the current file offset matches
        {
            if (ReadFile(file, Buffer, read, &readed, NULL))
            {
                if (readed != (DWORD)read)
                {
                    InvalidateRect(HWindow, NULL, FALSE);
                    Seek = Loaded = 0; // data in Buffer may be corrupted; invalidate them so nothing uses them while the message box is shown
                    kill = SalMessageBoxViewerPaintBlocked(HWindow, LoadStrW(IDS_VIEWER_UNKNOWNERR),
                                                           LoadStrW(IDS_ERRORREADINGFILE),
                                                           MB_RETRYCANCEL | MB_ICONEXCLAMATION) == IDCANCEL;
                    ret = FALSE;
                    Seek = Loaded = 0; // some data might have been loaded while the message box was shown, so invalidate Buffer again
                }
                else
                {
                    CodeCharacters(Buffer, Buffer + read);
                    Loaded += readed;
                    ret = TRUE;
                }
            }
            else
            {
                DWORD err2 = GetLastError();
                InvalidateRect(HWindow, NULL, FALSE);
                Seek = Loaded = 0; // data in Buffer may be corrupted; invalidate them so nothing uses them while the message box is shown
                kill = SalMessageBoxViewerPaintBlocked(HWindow, GetErrorTextOwned(err2).c_str(), LoadStrW(IDS_ERRORREADINGFILE),
                                                       MB_RETRYCANCEL | MB_ICONEXCLAMATION) == IDCANCEL;
                ret = FALSE;
                Seek = Loaded = 0; // some data might have been loaded while the message box was shown, so invalidate Buffer again
            }
        }
        else
        {
            InvalidateRect(HWindow, NULL, FALSE);
            Seek = Loaded = 0; // data in Buffer may be corrupted; invalidate them so nothing uses them while the message box is shown
            kill = SalMessageBoxViewerPaintBlocked(HWindow, GetErrorTextOwned(err).c_str(), LoadStrW(IDS_ERRORREADINGFILE),
                                                   MB_RETRYCANCEL | MB_ICONEXCLAMATION) == IDCANCEL;
            ret = FALSE;
            Seek = Loaded = 0; // some data might have been loaded while the message box was shown, so invalidate Buffer again
        }
        if (hFile == NULL) // if the caller does not close the handle, it is up to us
            HANDLES(CloseHandle(file));

        if (!ret && kill) // possibly end working with this file
        {
            ClearViewedFile();
            if (!Caption.empty())
            {
                Caption.clear();
                Caption.clear();
            }
            if (Lock != NULL)
            {
                SetEvent(Lock);
                Lock = NULL; // from now on it is up to the disk cache
            }
            SetWindowTextW(HWindow, LoadStrW(IDS_VIEWERTITLE));
            InvalidateRect(HWindow, NULL, FALSE);
        }

        return ret;
    }
    else
    {
        DWORD err = GetLastError();
        Seek = Loaded = 0;
        ClearViewedFile();
        if (!Caption.empty())
        {
            Caption.clear();
            Caption.clear();
        }
        if (Lock != NULL)
        {
            SetEvent(Lock);
            Lock = NULL; // from now on it is up to the disk cache
        }
        SetWindowTextW(HWindow, LoadStrW(IDS_VIEWERTITLE));
        InvalidateRect(HWindow, NULL, FALSE);
        SalMessageBoxViewerPaintBlocked(HWindow, GetErrorTextOwned(err).c_str(), LoadStrW(IDS_ERRORREADINGFILE), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
}

BOOL CViewerWindow::LoadBehind(HANDLE* hFile)
{
    CALL_STACK_MESSAGE1("CViewerWindow::LoadBehind()");
    if (FileNameW.empty())
        return FALSE;

    HANDLE file;
    if (hFile == NULL || *hFile == NULL)
    {
        file = OpenViewedFile(FILE_FLAG_SEQUENTIAL_SCAN);
        if (hFile != NULL && file != INVALID_HANDLE_VALUE)
            *hFile = file;
    }
    else
        file = *hFile;

    if (file != INVALID_HANDLE_VALUE)
    {
        CQuadWord size;
        DWORD err;
        BOOL haveSize = SalGetFileSize(file, size, err);
        if (!haveSize || size.Value != (unsigned __int64)FileSize) // error or file change
        {
            TRACE_I("The size of the viewed file has changed or some error occured.");
            // PostMessage(HWindow, WM_COMMAND, CM_REREADFILE, 0);  // legacy, unnecessary: it causes a "fatal error" and triggers a repaint
            Seek = Loaded = 0;
            if (hFile == NULL) // if the caller does not close the handle, it is up to us
                HANDLES(CloseHandle(file));
            return FALSE;
        }
        int read;
        if (FileSize - (Seek + Loaded) >= VIEW_BUFFER_SIZE / 2)
            read = VIEW_BUFFER_SIZE / 2;
        else
            read = (int)(FileSize - (Seek + Loaded));
        if (read == 0)
        {
            if (hFile == NULL) // if the caller does not close the handle, it is up to us
                HANDLES(CloseHandle(file));
            return FALSE;
        }
        DWORD readed; // first the offset into Buffer, then the number of bytes read
        __int64 seekEnd = Seek + Loaded;
        if (Loaded > 0)
        {
            int space = VIEW_BUFFER_SIZE - read;
            if (space < Loaded)
            {
                memmove(Buffer, Buffer + (Loaded - space), space);
                Loaded = (readed = space);
                Seek = seekEnd - Loaded;
            }
            else
                readed = (int)Loaded;
        }
        else
            readed = 0;
        BOOL ret;
        BOOL kill = FALSE; // TRUE means that FileName will be set to NULL on error

        CQuadWord resSeek;
        resSeek.SetUI64(seekEnd); // note, the seek for SetFilePointer is a signed value
        resSeek.LoDWord = SetFilePointer(file, resSeek.LoDWord, (PLONG)&resSeek.HiDWord, FILE_BEGIN);
        err = GetLastError();
        if ((resSeek.LoDWord != INVALID_SET_FILE_POINTER || err == NO_ERROR) && // no error
            resSeek.Value == (unsigned __int64)seekEnd)                         // the current file offset matches
        {
            if (ReadFile(file, Buffer + readed, read, &readed, NULL))
            {
                if (readed != (DWORD)read)
                {
                    TRACE_I("CViewerWindow::LoadBehind(): ReadFile returned " << (DWORD)readed << " instead of " << (DWORD)read);
                    InvalidateRect(HWindow, NULL, FALSE);
                    Seek = Loaded = 0; // data in Buffer may be corrupted; invalidate them so nothing uses them while the message box is shown
                    kill = SalMessageBoxViewerPaintBlocked(HWindow, LoadStrW(IDS_VIEWER_UNKNOWNERR), LoadStrW(IDS_ERRORREADINGFILE),
                                                           MB_RETRYCANCEL | MB_ICONEXCLAMATION) == IDCANCEL;
                    ret = FALSE;
                    Seek = Loaded = 0; // some data might have been loaded while the message box was shown, so invalidate Buffer again
                }
                else
                {
                    CodeCharacters(Buffer + Loaded, Buffer + Loaded + read);
                    Loaded += readed;
                    ret = TRUE;
                }
            }
            else
            {
                DWORD err2 = GetLastError();
                InvalidateRect(HWindow, NULL, FALSE);
                Seek = Loaded = 0; // data in Buffer may be corrupted; invalidate them so nothing uses them while the message box is shown
                kill = SalMessageBoxViewerPaintBlocked(HWindow, GetErrorTextOwned(err2).c_str(), LoadStrW(IDS_ERRORREADINGFILE),
                                                       MB_RETRYCANCEL | MB_ICONEXCLAMATION) == IDCANCEL;
                ret = FALSE;
                Seek = Loaded = 0; // some data might have been loaded while the message box was shown, so invalidate Buffer again
            }
        }
        else
        {
            DWORD err2 = GetLastError();
            InvalidateRect(HWindow, NULL, FALSE);
            Seek = Loaded = 0; // data in Buffer may be corrupted; invalidate them so nothing uses them while the message box is shown
            kill = SalMessageBoxViewerPaintBlocked(HWindow, GetErrorTextOwned(err2).c_str(), LoadStrW(IDS_ERRORREADINGFILE),
                                                   MB_RETRYCANCEL | MB_ICONEXCLAMATION) == IDCANCEL;
            ret = FALSE;
            Seek = Loaded = 0; // some data might have been loaded while the message box was shown, so invalidate Buffer again
        }
        if (hFile == NULL) // if the caller does not close the handle, it is up to us
            HANDLES(CloseHandle(file));

        if (!ret && kill) // possibly end working with this file
        {
            ClearViewedFile();
            if (!Caption.empty())
            {
                Caption.clear();
                Caption.clear();
            }
            if (Lock != NULL)
            {
                SetEvent(Lock);
                Lock = NULL; // from now on it is up to the disk cache
            }
            SetWindowTextW(HWindow, LoadStrW(IDS_VIEWERTITLE));
            InvalidateRect(HWindow, NULL, FALSE);
        }

        return ret;
    }
    else
    {
        DWORD err = GetLastError();
        Seek = Loaded = 0;
        ClearViewedFile();
        if (!Caption.empty())
        {
            Caption.clear();
            Caption.clear();
        }
        if (Lock != NULL)
        {
            SetEvent(Lock);
            Lock = NULL; // from now on it is up to the disk cache
        }
        SetWindowTextW(HWindow, LoadStrW(IDS_VIEWERTITLE));
        InvalidateRect(HWindow, NULL, FALSE);
        SalMessageBoxViewerPaintBlocked(HWindow, GetErrorTextOwned(err).c_str(), LoadStrW(IDS_ERRORREADINGFILE), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
}

void CViewerWindow::HeightChanged(BOOL& fatalErr)
{
    CALL_STACK_MESSAGE1("CViewerWindow::HeightChanged()");
    fatalErr = FALSE;
    switch (Type)
    {
    case vtHex:
    {
        MaxSeekY = max(0, max(0, FileSize - 1) / 16 + 1 - max(1, Height / CharHeight)) * 16;
        break;
    }

    case vtText:
    {
        int fullLines = max(Height / CharHeight, 1);
        MaxSeekY = FindSeekBefore(FileSize, fullLines, fatalErr);
        if (!fatalErr && fullLines > 1)
        {
            BOOL lastLineFatalErr = FALSE;
            __int64 lastLineSeekY = FindSeekBefore(FileSize, fullLines - 1, lastLineFatalErr);
            if (!lastLineFatalErr && lastLineSeekY > MaxSeekY)
                MaxSeekY = lastLineSeekY;
        }
        break;
    }
    }
}

void CViewerWindow::OpenFile(const wchar_t* file, const wchar_t* caption, BOOL wholeCaption)
{
    CALL_STACK_MESSAGE3("CViewerWindow::OpenFile(%S, %S)", file != NULL ? file : L"", caption != NULL ? caption : L"");

    Caption.clear();
    if (caption != NULL)
    {
        Caption = caption;
        WholeCaption = wholeCaption;
    }
    else
        WholeCaption = FALSE;
    FileNameW = file != NULL ? std::wstring(file) : std::wstring();
    TooBigSelAction = 0;
    CanSwitchToHex = TRUE;
    CanSwitchQuietlyToHex = TRUE;
    OriginX = 0;
    SeekY = 0;
    ExitTextMode = FALSE;
    ForceTextMode = FALSE;
    CodeType = 0;
    UseCodeTable = FALSE;
    TextEncoding = Sally::Unicode::BomEncoding::LegacyBytes;
    TextContentOffset = 0;
    BOOL fatalErr = FALSE;
    FileChanged(NULL, FALSE, fatalErr, TRUE);
    if (fatalErr)
        FatalFileErrorOccured();
    if (fatalErr || ExitTextMode)
    {
        CanSwitchQuietlyToHex = FALSE;
        return;
    }
    if (FileNameW.empty())
        SetWindowTextW(HWindow, LoadStrW(IDS_VIEWERTITLE));
    else
        SetViewerCaption();
    InvalidateRect(HWindow, NULL, FALSE);
    UpdateWindow(HWindow);
    CanSwitchQuietlyToHex = FALSE;
}

HANDLE CViewerWindow::OpenViewedFile(DWORD flags) const
{
    if (FileNameW.empty())
        return INVALID_HANDLE_VALUE;
    return SalCreateFileH(FileNameW.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, flags, NULL);
}

void CViewerWindow::ClearViewedFile()
{
    FileNameW.clear();
}

void CViewerWindow::ReleaseMouseDrag()
{
    if (MouseDrag)
    {
        ReleaseCapture();
        KillTimer(HWindow, IDT_AUTOSCROLL);
        MouseDrag = FALSE;
        EndSelectionPrefX = -1;
    }
}

int CViewerWindow::SalMessageBoxViewerPaintBlocked(HWND hParent, const wchar_t* lpText, const wchar_t* lpCaption, UINT uType)
{
    BOOL oldEnablePaint = EnablePaint;
    // showing a message box triggers Paint = reading the file = more errors,
    // therefore disable Paint = only the viewer background will be cleared (e.g., parts of the file already displayed)
    EnablePaint = FALSE;

    int res;
    // The arguments ARE wide now - these two conversions are gone.
    const std::wstring textW = lpText != NULL ? lpText : L"";
    const std::wstring captionW = lpCaption != NULL ? lpCaption : L"";

    UINT buttons = uType & MB_TYPEMASK;
    if (buttons == MB_RETRYCANCEL)
    {
        res = gPrompter->AskRetryCancel(captionW.c_str(), textW.c_str()).type == PromptResult::kCancel
                  ? IDCANCEL
                  : IDRETRY;
    }
    else if (buttons == MB_OKCANCEL)
    {
        res = gPrompter->ConfirmError(captionW.c_str(), textW.c_str()).type == PromptResult::kCancel
                  ? IDCANCEL
                  : IDOK;
    }
    else if (buttons == MB_YESNOCANCEL)
    {
        auto r = gPrompter->AskYesNoCancel(captionW.c_str(), textW.c_str());
        if (r.type == PromptResult::kYes)
            res = IDYES;
        else if (r.type == PromptResult::kNo)
            res = IDNO;
        else
            res = IDCANCEL;
    }
    else // MB_OK or default
    {
        gPrompter->ShowError(captionW.c_str(), textW.c_str());
        res = IDOK;
    }

    EnablePaint = oldEnablePaint;
    return res;
}

void CViewerWindow::FileChanged(HANDLE file, BOOL testOnlyFileSize, BOOL& fatalErr,
                                BOOL detectFileType, BOOL* calledHeightChanged)
{
    CALL_STACK_MESSAGE3("CViewerWindow::FileChanged(, %d, , %d,)", testOnlyFileSize, detectFileType);
    fatalErr = FALSE;
    if (calledHeightChanged != NULL)
        *calledHeightChanged = FALSE;
    if (FileNameW.empty())
        return;

    // The cached line scan describes the previous contents. The validity key would catch a
    // size change, but a file edited in place keeps its size, so drop the cache whenever the
    // viewer reloads rather than relying on the key alone.
    ResetDecodedLineIndex();

    const wchar_t* sW = wcsrchr(FileNameW.c_str(), L'\\');
    const wchar_t* namePartW = sW != NULL ? sW + 1 : FileNameW.c_str();

    BOOL close;
    if (file == NULL)
    {
        file = OpenViewedFile(FILE_FLAG_SEQUENTIAL_SCAN);
        close = TRUE;
    }
    else
        close = FALSE;

    if (file != INVALID_HANDLE_VALUE)
    {
        __int64 oldFS = FileSize;
        CQuadWord size;
        DWORD err;
        BOOL haveSize = SalGetFileSize(file, size, err);
        FileSize = size.Value;
        if (!haveSize ||                               // error while determining the file size
            size >= CQuadWord(0xFFFFFFFF, 0x7FFFFFFF)) // file too large (> 8 EB)
        {
            Seek = 0;
            Loaded = 0;
            FileSize = 0;
            FindOffset = 0;
            StartSelection = EndSelection = -1;
            ReleaseMouseDrag();
            FirstLineSize = LastLineSize = ViewSize = 0;
            LastFindSeekY = -1;
            ClearViewedFile();
            if (!Caption.empty())
            {
                Caption.clear();
                Caption.clear();
            }
            if (Lock != NULL)
            {
                SetEvent(Lock);
                Lock = NULL; // from now on it is up to the disk cache
            }
            SetWindowTextW(HWindow, LoadStrW(IDS_VIEWERTITLE));
            InvalidateRect(HWindow, NULL, FALSE);
            SalMessageBoxViewerPaintBlocked(HWindow, err == NO_ERROR ? LoadStrW(IDS_UNABLETOVIEWFILENT) : GetErrorTextOwned(err).c_str(),
                                            LoadStrW(IDS_ERRORREADINGFILE), MB_OK | MB_ICONEXCLAMATION);
            fatalErr = TRUE;
        }
        else
        {
            if (!testOnlyFileSize || FileSize != oldFS)
            {
                Seek = 0;
                Loaded = 0;
                FindOffset = 0;
                StartSelection = EndSelection = -1;
                ResetFindOffsetOnNextPaint = TRUE;
                ReleaseMouseDrag();
                FirstLineSize = LastLineSize = ViewSize = 0;
                LastFindSeekY = -1;
                TextEncoding = Sally::Unicode::BomEncoding::LegacyBytes;
                TextContentOffset = 0;

                if (FileSize > 0)
                {
                    // Read a real head window, not just three bytes:
                    // the viewer used to decode ONLY BOM-marked files, so a
                    // BOM-less UTF-8 or UTF-16 file - which is the common case
                    // outside Windows tooling - rendered through the 256-entry
                    // legacy table as mojibake. EncodingDetector answers from the
                    // window, and a real BOM still wins inside it.
                    BYTE head[4096] = {};
                    DWORD read = 0;
                    CQuadWord bomSeek;
                    bomSeek.SetUI64(0);
                    bomSeek.LoDWord = SetFilePointer(file, bomSeek.LoDWord, (PLONG)&bomSeek.HiDWord, FILE_BEGIN);
                    DWORD seekErr = GetLastError();
                    if ((bomSeek.LoDWord != INVALID_SET_FILE_POINTER || seekErr == NO_ERROR) &&
                        ReadFile(file, head, (DWORD)min((__int64)sizeof(head), FileSize), &read, NULL))
                    {
                        sally::text::DetectionOptions detectOptions;
                        detectOptions.scanBudget = read;
                        // The window legitimately ends mid-character for any file
                        // larger than the buffer.
                        detectOptions.windowMayEndMidCharacter = true;
                        const sally::text::DetectionResult detected =
                            sally::text::Detect(head, read, detectOptions);
                        TextEncoding = detected.encoding;
                        TextContentOffset = detected.textOffset;
                    }
                }

                if (detectFileType)
                {
                    int defViewMode = DefViewMode; // (0=Auto-Select)
                    if (defViewMode == 0)
                    {
                        // the exceptions apply only when Auto-Select is active
                        if (Configuration.TextModeMasks.AgreeMasks(namePartW, NULL))
                        {
                            defViewMode = 1;               // Text
                            CanSwitchQuietlyToHex = FALSE; // if we force Text mode, prompt before switching to Hex
                        }
                        else if (Configuration.HexModeMasks.AgreeMasks(namePartW, NULL))
                            defViewMode = 2; // Hex
                    }
                    else
                    {
                        if (defViewMode == 1)
                            CanSwitchQuietlyToHex = FALSE; // if we force Text mode, prompt before switching to Hex
                    }

                    BOOL bomTextMode = HasDecodedTextEncoding() && defViewMode != 2;
                    if (bomTextMode)
                    {
                        Type = vtText;
                        CodeType = 0;
                        UseCodeTable = FALSE;
                        SeekY = TextContentOffset;
                        FindOffset = TextContentOffset;
                    }

                    int len;
                    BOOL fatalErr2 = FALSE;
                    if (!bomTextMode && (CodePageAutoSelect || defViewMode == 0))
                        len = (int)Prepare(NULL, 0, RECOGNIZE_FILE_TYPE_BUFFER_LEN, fatalErr2);
                    else
                        len = 0;
                    if (CodePageAutoSelect && fatalErr2)
                        fatalErr = TRUE;
                    else // when Auto-Select picks the view mode (defViewMode == 0) we ignore a Prepare error here; a bit odd, no idea why ;-) Petr
                    {
                        // with Auto-Select enabled and enough data for the heuristics,
                        // try to find a suitable conversion table
                        if (len > 0 && (defViewMode == 0 || CodePageAutoSelect))
                        {
                            BOOL isText;
                            std::wstring codePage;
                            char recBuf[RECOGNIZE_FILE_TYPE_BUFFER_LEN]; // to be safe, copy the data from Buffer into recBuf
                            int recLen = min(len, RECOGNIZE_FILE_TYPE_BUFFER_LEN);
                            memcpy(recBuf, (char*)Buffer, recLen);
                            BOOL oldEnablePaint = EnablePaint;
                            // displaying a message box triggers Paint = reads the file = produces more errors,
                            // so disable Paint, which only clears the viewer background (e.g., the parts already displayed)
                            EnablePaint = FALSE;
                            RecognizeFileType(HWindow, recBuf, recLen, FALSE, &isText, &codePage);
                            EnablePaint = oldEnablePaint;
                            if (defViewMode == 0)
                            {
                                if (isText)
                                    Type = vtText;
                                else
                                    Type = vtHex;
                            }
                            if (CodePageAutoSelect)
                            {
                                if (isText && defViewMode != 2)
                                {
                                    int c = CodeTables.GetConversionToWinCodePage(codePage.c_str());
                                    if (CodeTables.Valid(c))
                                        SetCodeType(c);
                                    else // conversion "none"
                                    {
                                        CodeType = 0;
                                        UseCodeTable = FALSE;
                                    }
                                }
                            }
                        }
                        if (bomTextMode)
                        {
                            Type = vtText;
                        }
                        else if (defViewMode == 1)
                            Type = vtText;
                        else if (defViewMode == 2)
                            Type = vtHex;
                        // if auto-select is off, fall back to the default conversion
                        if (!bomTextMode && !CodePageAutoSelect)
                        {
                            int defCodeType;
                            if (!CodeTables.GetCodeType(DefaultConvert.c_str(), defCodeType))
                                defCodeType = 0;
                            if (CodeTables.Valid(defCodeType))
                                SetCodeType(defCodeType);
                            else // conversion "none"
                            {
                                CodeType = 0;
                                UseCodeTable = FALSE;
                            }
                        }
                    }
                }

                if (HasDecodedTextMode())
                {
                    CodeType = 0;
                    UseCodeTable = FALSE;
                    SeekY = max(SeekY, TextContentOffset);
                    FindOffset = max(FindOffset, TextContentOffset);
                }

                if (!fatalErr)
                {
                    HeightChanged(fatalErr);
                    if (calledHeightChanged != NULL)
                        *calledHeightChanged = TRUE;
                    if (!fatalErr && !ExitTextMode)
                        FindNewSeekY(SeekY, fatalErr);
                }
            }
        }
        if (close)
            HANDLES(CloseHandle(file));
    }
    else
    {
        DWORD err = GetLastError();
        Seek = 0;
        Loaded = 0;
        FileSize = 0;
        FindOffset = 0;
        StartSelection = EndSelection = -1;
        ReleaseMouseDrag();
        FirstLineSize = LastLineSize = ViewSize = 0;
        LastFindSeekY = -1;
        ClearViewedFile();
        if (!Caption.empty())
        {
            Caption.clear();
            Caption.clear();
        }
        if (Lock != NULL)
        {
            SetEvent(Lock);
            Lock = NULL; // from now on it is up to the disk cache
        }
        SetWindowTextW(HWindow, LoadStrW(IDS_VIEWERTITLE));
        InvalidateRect(HWindow, NULL, FALSE);
        if (IsWindowVisible(HWindow)) // safeguard against a message box when closing the viewer while the viewed file is being overwritten
            SalMessageBoxViewerPaintBlocked(HWindow, GetErrorTextOwned(err).c_str(), LoadStrW(IDS_ERRORREADINGFILE), MB_OK | MB_ICONEXCLAMATION);
        fatalErr = TRUE;
    }
}

void CViewerWindow::FatalFileErrorOccured(DWORD repeatCmd)
{
    // try to set the internal viewer state so no further error occurs before
    // WM_USER_VIEWERREFRESH arrives
    WaitForViewerRefresh = TRUE;
    LastSeekY = SeekY;
    LastOriginX = OriginX;
    RepeatCmdAfterRefresh = repeatCmd;

    Seek = 0;
    Loaded = 0;
    OriginX = 0;
    SeekY = 0;
    MaxSeekY = 0;
    FileSize = 0;
    ViewSize = 0;
    FirstLineSize = 0;
    LastLineSize = 0;
    StartSelection = -1;
    EndSelection = -1;
    ReleaseMouseDrag();
    FindOffset = 0;
    LastFindSeekY = -1;
    LastFindOffset = 0;
    ScrollToSelection = FALSE;
    TextEncoding = Sally::Unicode::BomEncoding::LegacyBytes;
    TextContentOffset = 0;
    LineOffset.DestroyMembers();
    EnableSetScroll = TRUE;
    PostMessage(HWindow, WM_USER_VIEWERREFRESH, 0, 0);
}

BOOL CViewerWindow::FindNextEOL(HANDLE* hFile, __int64 seek, __int64 maxSeek, __int64& lineEnd, __int64& nextLineBegin, BOOL& fatalErr)
{
    CALL_STACK_MESSAGE_NONE
    // CALL_STACK_MESSAGE3("CViewerWindow::FindNextEOL(%g, %g, , ,)", (double)seek, (double)maxSeek);
    unsigned char *s, *end;
    __int64 cr = -2; // offset of the last '\r'
    __int64 len;
    fatalErr = FALSE;
    if (HasDecodedTextMode())
    {
        seek = max(seek, TextContentOffset);
        maxSeek = min(maxSeek, FileSize);
        Sally::Unicode::DecodedRun decoded;
        __int64 decodeEnd = min(FileSize, maxSeek + 8);
        if (!DecodeTextRange(hFile, seek, decodeEnd, decoded, fatalErr, decodeEnd >= FileSize))
            return FALSE;
        if (fatalErr)
            return FALSE;
        for (std::size_t i = 0; i < decoded.CellCount(); ++i)
        {
            if (decoded.RawStart[i] > maxSeek)
                break;
            std::uint32_t scalar = decoded.Scalars[i];
            if (scalar == L'\r')
            {
                if (Configuration.EOL_CRLF)
                {
                    Sally::Unicode::DecodedRun nextScalar;
                    bool haveNext = false;
                    if (i + 1 < decoded.CellCount())
                    {
                        nextScalar.AppendCell(decoded.Scalars[i + 1], decoded.RawStart[i + 1], decoded.RawEnd[i + 1]);
                        haveNext = true;
                    }
                    else if (ReadDecodedScalar(hFile, decoded.RawEnd[i], nextScalar, fatalErr) && !fatalErr &&
                             nextScalar.CellCount() > 0)
                        haveNext = true;
                    if (fatalErr)
                        return FALSE;
                    if (haveNext && nextScalar.Scalars[0] == L'\n')
                    {
                        lineEnd = decoded.RawStart[i];
                        nextLineBegin = nextScalar.RawEnd[0];
                        return TRUE;
                    }
                }
                if (Configuration.EOL_CR)
                {
                    lineEnd = decoded.RawStart[i];
                    nextLineBegin = decoded.RawEnd[i];
                    return TRUE;
                }
            }
            else if (scalar == L'\n')
            {
                if (Configuration.EOL_LF)
                {
                    lineEnd = decoded.RawStart[i];
                    nextLineBegin = decoded.RawEnd[i];
                    return TRUE;
                }
            }
            else if (scalar == 0 && Configuration.EOL_NULL)
            {
                lineEnd = decoded.RawStart[i];
                nextLineBegin = decoded.RawEnd[i];
                return TRUE;
            }
        }
        if (maxSeek >= FileSize)
        {
            lineEnd = FileSize;
            nextLineBegin = FileSize;
            return TRUE;
        }
        nextLineBegin = -1;
        return FALSE;
    }
    if (seek > 0) // not the start of the file
    {
        len = Prepare(hFile, seek - 1, 1, fatalErr);
        if (fatalErr)
            return FALSE;
        if (len == 1 && *(Buffer + (seek - Seek - 1)) == '\r')
            cr = seek - 1;
    }
    lineEnd = seek;
    nextLineBegin = -1;
    while (lineEnd <= maxSeek)
    {
        len = Prepare(hFile, lineEnd, APROX_LINE_LEN, fatalErr);
        if (fatalErr)
            break;
        if (len == 0)
        {
            nextLineBegin = lineEnd; // end of file
            break;
        }
        s = Buffer + (lineEnd - Seek);
        end = s + len;
        while (s < end)
        {
            if (*s <= '\r')
            {
                if (*s == '\r')
                {
                    if (Configuration.EOL_CR)
                        break;
                    cr = (s - Buffer) + Seek;
                }
                else
                {
                    if (*s == '\n')
                    {
                        if (cr + 1 == (s - Buffer) + Seek &&
                            Configuration.EOL_CRLF)
                        {
                            s--; // because of this, the '\r\n' condition below (*s might not be valid)
                            break;
                        }
                        if (Configuration.EOL_LF)
                            break;
                    }
                    else
                    {
                        if (*s == 0 && Configuration.EOL_NULL)
                            break;
                    }
                }
            }
            s++;
        }
        if (s < end) // EOL found
        {
            if (cr == (s - Buffer) + Seek) // '\r\n' already detected
            {
                lineEnd = (s - Buffer) + Seek;
                if (lineEnd > maxSeek)
                    break;
                nextLineBegin = lineEnd + 2;
            }
            else
            {
                lineEnd = (s - Buffer) + Seek;
                if (lineEnd > maxSeek)
                    break;
                nextLineBegin = lineEnd + 1;
                if (*s == '\r' && Configuration.EOL_CRLF) // test for '\r\n'
                {
                    len = Prepare(hFile, lineEnd, 2, fatalErr);
                    if (fatalErr)
                        break;
                    if (len == 2 && *(Buffer + (lineEnd - Seek + 1)) == '\n')
                        nextLineBegin++;
                }
            }
            break; // end of search
        }
        lineEnd += len;
    }
    return !fatalErr && nextLineBegin != -1;
}

BOOL CViewerWindow::FindPreviousEOL(HANDLE* hFile, __int64 seek, __int64 minSeek, __int64& lineBegin,
                                    __int64& previousLineEnd, BOOL allowWrap, BOOL takeLineBegin,
                                    BOOL& fatalErr, int* lines, __int64* firstLineEndOff,
                                    __int64* firstLineCharLen, BOOL addLineIfSeekIsWrap)
{
    SLOW_CALL_STACK_MESSAGE6("CViewerWindow::FindPreviousEOL(%g, %g, , , %d, %d, , , , , %d)",
                             (double)seek, (double)minSeek, allowWrap, takeLineBegin, addLineIfSeekIsWrap);

    if (firstLineEndOff != NULL)
        *firstLineEndOff = -1;
    if (firstLineCharLen != NULL)
        *firstLineCharLen = -1;
    if (HasDecodedTextMode())
    {
        return FindPreviousDecodedEOL(hFile, seek, minSeek, lineBegin, previousLineEnd, allowWrap,
                                      takeLineBegin, fatalErr, lines, firstLineEndOff,
                                      firstLineCharLen, addLineIfSeekIsWrap);
    }
    BOOL collectTabs = allowWrap && WrapText || firstLineCharLen != NULL;
    unsigned char *s, *end;
    fatalErr = FALSE;
    __int64 lf = -2; // offset of the last '\n'
    __int64 len;
    if (seek < FileSize) // not the end of the file
    {
        len = Prepare(NULL, seek, 1, fatalErr);
        if (fatalErr)
            return FALSE;
        if (len == 1 && *(Buffer + (seek - Seek)) == '\n')
            lf = seek;
    }

    TDirectArray<__int64> tabs(1000, 500); // positions of tabs in the line (assumption: there will not be many ...)
    lineBegin = seek;
    previousLineEnd = -1;
    while (lineBegin >= minSeek)
    {
        if (!FindingSoDonotSwitchToHex && CanSwitchToHex && minSeek == 0 && !ForceTextMode &&
            seek - lineBegin > TEXT_MAX_LINE_LEN)
        {
            if (!CanSwitchQuietlyToHex)
                CanSwitchToHex = FALSE;
            if (CanSwitchQuietlyToHex ||
                SalMessageBoxViewerPaintBlocked(HWindow, LoadStrW(IDS_VIEWER_BINFILE), LoadStrW(IDS_VIEWERTITLE),
                                                MB_YESNO | MB_ICONQUESTION) == IDYES)
            {
                CanSwitchQuietlyToHex = FALSE;
                ExitTextMode = TRUE;
                PostMessage(HWindow, WM_COMMAND, CM_TO_HEX, 0);
                return FALSE;
            }
            else
                ForceTextMode = TRUE;
        }

        len = min(APROX_LINE_LEN, lineBegin);
        len = Prepare(NULL, lineBegin - len, len, fatalErr);
        if (fatalErr)
            break;
        if (len == 0)
        {
            previousLineEnd = lineBegin = 0; // start of the file
            break;
        }
        s = Buffer + (lineBegin - Seek - 1);
        end = s - len;
        while (s > end)
        {
            if (*s <= '\r')
            {
                if (*s == '\n')
                {
                    if (Configuration.EOL_LF)
                        break;
                    lf = (s - Buffer) + Seek;
                }
                else
                {
                    if (*s == '\r')
                    {
                        if (lf - 1 == (s - Buffer) + Seek &&
                            Configuration.EOL_CRLF)
                        {
                            s++; // because of this, the '\r\n' condition below (*s might not be valid)
                            break;
                        }
                        if (Configuration.EOL_CR)
                            break;
                    }
                    else
                    {
                        if (*s == 0 && Configuration.EOL_NULL)
                            break;
                        else
                        {
                            if (collectTabs && *s == '\t')
                                tabs.Add((s - Buffer) + Seek);
                        }
                    }
                }
            }
            s--;
        }
        if (s > end) // EOL found
        {
            if (lf == (s - Buffer) + Seek) // '\r\n' already detected
            {
                lineBegin = (s - Buffer) + Seek + 1;
                if (lineBegin < minSeek)
                    break;
                previousLineEnd = lineBegin - 2;
            }
            else
            {
                lineBegin = (s - Buffer) + Seek + 1;
                if (lineBegin < minSeek)
                    break;
                previousLineEnd = lineBegin - 1;
                if (*s == '\n' && Configuration.EOL_CRLF) // test for '\r\n'
                {
                    len = min(lineBegin, 2);
                    len = Prepare(NULL, lineBegin - len, len, fatalErr);
                    if (fatalErr)
                        break;
                    if (len == 2 && *(Buffer + (lineBegin - len - Seek)) == '\r')
                        previousLineEnd--;
                }
            }
            break; // end of search
        }
        lineBegin -= len;
    }

    // do not treat the start of the file as the end of the previous line (which is a bit nonsensical,
    // yet previousLineEnd pretends it is); NOTE: any line wrapping is handled later in the code
    if (lineBegin > 0 && firstLineEndOff != NULL)
        *firstLineEndOff = previousLineEnd;

    if (!fatalErr && allowWrap && WrapText && Width > 0 && Height > 0) // wrap mode
    {
        int columns = (Width - BORDER_WIDTH) / CharWidth; // window width in characters
        __int64 lineLen = seek - lineBegin;               // line length in bytes
        int tabsCount = tabs.Count;
        __int64 originalLineBegin = lineBegin;
        while (1)
        {
            __int64 tabAdd = 0; // how many spaces the tabs add
            while (tabsCount > 0 && tabs[tabsCount - 1] + tabAdd < lineBegin + columns)
            {
                int tab = (int)(Configuration.TabSize - ((tabs[tabsCount - 1] + tabAdd - lineBegin) % Configuration.TabSize));
                if (tabs[tabsCount - 1] + tabAdd + tab > lineBegin + columns)
                {
                    tab = (int)(lineBegin + columns - tabs[tabsCount - 1] - tabAdd);
                }
                tabAdd += tab - 1;
                tabsCount--;
            }
            if ((takeLineBegin && lineBegin + columns - tabAdd > seek) || // treat "seek" as the offset of the character in the line (at line boundaries it acts as the start of the line)
                (!takeLineBegin && lineBegin + columns - tabAdd >= seek)) // treat "seek" as the offset of the end of the line (at line boundaries it acts as the end of the line)
            {
                if (takeLineBegin && addLineIfSeekIsWrap && originalLineBegin < lineBegin && lineBegin == seek)
                {
                    // the start of this wrapped line is also the end of the previous wrapped line, 'addLineIfSeekIsWrap' is
                    // TRUE if 'seek' should be considered the end of the previous one (means skipping one additional "EOL")
                    if (lines != NULL)
                        (*lines)++;
                    addLineIfSeekIsWrap = FALSE; // we can do it only once
                }
                if (!takeLineBegin && firstLineCharLen != NULL) // "seek" is the end of the line, calculate its length
                {
                    *firstLineCharLen = seek - lineBegin + tabAdd;
                    firstLineCharLen = NULL; // we have what we wanted, no further adjustments (that would involve lengths of previous lines)
                }
                if (firstLineEndOff != NULL)
                {
                    if (originalLineBegin < lineBegin) // wrapped line
                        *firstLineEndOff = lineBegin;  // the start of this wrapped line is also the end of the previous wrapped line
                    firstLineEndOff = NULL;            // we have what we wanted, no further adjustments (could potentially concern ends of previous lines up to the count of "lines")
                }

                if (originalLineBegin < lineBegin) // wrapped line
                {
                    if (lines != NULL && *lines > 0)
                    { // if we need to look for the start of more lines, do it while we are here (so the loaded data is used)
                        (*lines)--;
                        takeLineBegin = FALSE; // from now on treat "seek" as the offset of the end of the line
                        seek = lineBegin;
                        lineBegin = originalLineBegin;
                        tabsCount = tabs.Count;
                        continue;
                    }
                    previousLineEnd = lineBegin;
                }
                break;
            }
            lineBegin += columns - tabAdd; // adjust the offset
        }
    }
    else
    {
        if (!fatalErr && !takeLineBegin && firstLineCharLen != NULL)
        {
            __int64 tabAdd = 0; // how many spaces the tabs add
            int tabsCount = tabs.Count;
            while (tabsCount-- > 0)
            {
                int tab = (int)(Configuration.TabSize - ((tabs[tabsCount] + tabAdd - lineBegin) % Configuration.TabSize));
                tabAdd += tab - 1;
            }
            *firstLineCharLen = seek - lineBegin + tabAdd; // line length in bytes + the extra from tabs
        }
    }
    return !fatalErr && previousLineEnd != -1;
}

void CViewerWindow::ResetDecodedLineIndex()
{
    // swap-with-empty, not clear(): clear() keeps capacity, so the peak footprint of a huge
    // file stayed committed for the lifetime of the viewer window even after a reload.
    std::vector<CDecodedLine>().swap(DecodedLineCheckpoints);
    DecodedLineIndexTextStart = -1;
    DecodedLineIndexMaxCells = -1;
    DecodedLineIndexFileSize = -1;
    DecodedLineIndexEncoding = -1;
    DecodedLineIndexEolFlags = -1;
    DecodedLineIndexTabSize = -1;
    DecodedLineIndexWrap = FALSE;
    DecodedLineIndexComplete = FALSE;
}

int CViewerWindow::CurrentDecodedEolFlags() const
{
    return PackDecodedEolFlags(Configuration.EOL_CRLF != 0, Configuration.EOL_CR != 0,
                               Configuration.EOL_LF != 0, Configuration.EOL_NULL != 0);
}

void CViewerWindow::EnsureDecodedIndexValid(__int64 textStart, __int64 maxCells)
{
    const int eolFlags = CurrentDecodedEolFlags();
    if (DecodedLineIndexTextStart != textStart ||
        DecodedLineIndexMaxCells != maxCells ||
        DecodedLineIndexFileSize != FileSize ||
        DecodedLineIndexEncoding != (int)TextEncoding ||
        DecodedLineIndexEolFlags != eolFlags ||
        DecodedLineIndexTabSize != Configuration.TabSize ||
        DecodedLineIndexWrap != WrapText)
    {
        ResetDecodedLineIndex();
        DecodedLineIndexTextStart = textStart;
        DecodedLineIndexMaxCells = maxCells;
        DecodedLineIndexFileSize = FileSize;
        DecodedLineIndexEncoding = (int)TextEncoding;
        DecodedLineIndexEolFlags = eolFlags;
        DecodedLineIndexTabSize = Configuration.TabSize;
        DecodedLineIndexWrap = WrapText;
    }
}

const CViewerWindow::CDecodedLine* CViewerWindow::NearestDecodedCheckpoint(__int64 seek) const
{
    // Checkpoints are appended in increasing order, so a binary search is valid. We want the
    // last one whose FOLLOWING line still starts before seek - resuming there cannot overshoot.
    size_t lo = 0;
    size_t hi = DecodedLineCheckpoints.size();
    const CDecodedLine* best = NULL;
    while (lo < hi)
    {
        const size_t mid = lo + (hi - lo) / 2;
        if (DecodedLineCheckpoints[mid].NextBegin < seek)
        {
            best = &DecodedLineCheckpoints[mid];
            lo = mid + 1;
        }
        else
            hi = mid;
    }
    return best;
}

BOOL CViewerWindow::FindPreviousDecodedEOL(HANDLE* hFile, __int64 seek, __int64 minSeek, __int64& lineBegin,
                                           __int64& previousLineEnd, BOOL allowWrap, BOOL takeLineBegin,
                                           BOOL& fatalErr, int* lines, __int64* firstLineEndOff,
                                           __int64* firstLineCharLen, BOOL addLineIfSeekIsWrap)
{
    UNREFERENCED_PARAMETER(allowWrap);
    UNREFERENCED_PARAMETER(takeLineBegin);
    UNREFERENCED_PARAMETER(lines);
    UNREFERENCED_PARAMETER(addLineIfSeekIsWrap);

    fatalErr = FALSE;
    lineBegin = max(minSeek, TextContentOffset);
    previousLineEnd = lineBegin;
    if (firstLineEndOff != NULL)
        *firstLineEndOff = -1;
    if (firstLineCharLen != NULL)
        *firstLineCharLen = -1;

    minSeek = max(minSeek, TextContentOffset);
    seek = max(seek, minSeek);
    if (seek <= minSeek)
    {
        previousLineEnd = lineBegin = minSeek;
        if (firstLineCharLen != NULL)
            *firstLineCharLen = 0;
        return TRUE;
    }

    // Resume from the nearest checkpoint instead of re-walking from the start of the text.
    //
    // The index is keyed on the TEXT START, not on the caller minSeek. Keying on minSeek made
    // display and search evict each other: a backward regex search moves minSeek every
    // iteration, so the whole index was rebuilt once per line examined - quietly restoring the
    // quadratic cost the memoisation removed. One shared index; results are clamped to minSeek
    // at the end.
    const __int64 textStart = max(TextContentOffset, (__int64)0);
    __int64 maxCells = WrapText ? max(1, (Width - BORDER_WIDTH) / CharWidth) : TEXT_MAX_LINE_LEN + 1;
    EnsureDecodedIndexValid(textStart, maxCells);

    __int64 pos = textStart;
    __int64 lastBegin = minSeek;
    __int64 lastEnd = minSeek;
    __int64 lastPreviousEnd = minSeek;
    __int64 lastLen = 0;
    __int64 lineOrdinal = 0;

    const CDecodedLine* checkpoint = NearestDecodedCheckpoint(seek);
    if (checkpoint != NULL)
    {
        // A checkpoint records the line BEFORE its resume point, which is exactly the loop
        // state: position to continue from, plus the preceding line end and length.
        pos = checkpoint->NextBegin;
        lastBegin = checkpoint->Begin;
        lastEnd = checkpoint->End;
        lastPreviousEnd = checkpoint->End;
        lastLen = checkpoint->CellCount;
    }

    while (pos < seek && pos < FileSize)
    {
        __int64 cellCount = 0;
        __int64 scanEnd = pos;
        __int64 scanNext = pos;
        BOOL eol = FALSE;
        BOOL wrapped = FALSE;
        int eolBytes = 0;
        if (!ScanDecodedTextLine(hFile, pos, maxCells, cellCount, scanEnd, scanNext,
                                 eol, wrapped, eolBytes, fatalErr) ||
            fatalErr)
        {
            // Never leave a half-built index behind: a later call must be able to retry.
            ResetDecodedLineIndex();
            return FALSE;
        }
        if (scanNext <= pos)
            break; // no forward progress - the original scan stopped here too

        CDecodedLine line;
        line.Begin = pos;
        line.End = scanEnd;
        line.NextBegin = scanNext;
        line.CellCount = cellCount;

        if (line.NextBegin >= seek)
        {
            lineBegin = line.Begin;
            previousLineEnd = lastPreviousEnd;
            lastEnd = line.End;
            lastLen = line.CellCount;
            break;
        }

        // Remember every Nth line so a later call resumes near here rather than at the text
        // start. Appended in increasing order, which the binary search depends on.
        if ((lineOrdinal % DECODED_CHECKPOINT_STRIDE) == 0 &&
            (DecodedLineCheckpoints.empty() || DecodedLineCheckpoints.back().Begin < line.Begin))
        {
            DecodedLineCheckpoints.push_back(line);
        }
        lineOrdinal++;

        lastPreviousEnd = line.End;
        lastBegin = line.Begin;
        lastEnd = line.End;
        lastLen = line.CellCount;
        pos = line.NextBegin;
    }

    // The index runs from the text start, so a caller asking about a later window must not be
    // handed a line that begins before what it asked for.
    if (lineBegin < minSeek)
        lineBegin = minSeek;
    if (previousLineEnd < minSeek)
        previousLineEnd = minSeek;

    if (pos >= seek)
    {
        lineBegin = lastBegin;
        previousLineEnd = lastPreviousEnd;
    }

    if (lineBegin > TextContentOffset && firstLineEndOff != NULL)
        *firstLineEndOff = previousLineEnd;
    if (firstLineCharLen != NULL)
        *firstLineCharLen = lastLen;
    if (firstLineEndOff != NULL && *firstLineEndOff == -1)
        *firstLineEndOff = lastEnd;
    return TRUE;
}

__int64
CViewerWindow::FindSeekBefore(__int64 seek, int lines, BOOL& fatalErr, __int64* firstLineEndOff,
                              __int64* firstLineCharLen, BOOL addLineIfSeekIsWrap) // text display
{
    CALL_STACK_MESSAGE3("CViewerWindow::FindSeekBefore(%g, %d, , ,)", (double)seek, lines);
    fatalErr = FALSE;
    if (firstLineEndOff != NULL)
        *firstLineEndOff = -1;
    if (firstLineCharLen != NULL)
        *firstLineCharLen = -1;
    __int64 beg = seek, prevEnd;
    __int64 minSeek = TextStartOffset();
    if (seek < minSeek)
        seek = minSeek;
    BOOL first = TRUE; // the positions at the start and end of the line coincide; at a wrapped line end
                       // the first seek must take the position at the start, the others at the end
    while (lines--)
    {
        FindPreviousEOL(NULL, seek, minSeek, beg, prevEnd, TRUE, first, fatalErr, &lines, first ? firstLineEndOff : NULL,
                        firstLineCharLen != NULL && *firstLineCharLen == -1 ? firstLineCharLen : NULL,
                        first ? addLineIfSeekIsWrap : FALSE);
        if (fatalErr || ExitTextMode)
            return 0;
        seek = prevEnd;
        first = FALSE;
    }
    return beg;
}

__int64
CViewerWindow::ZeroLineSize(BOOL& fatalErr, __int64* firstLineEndOff, __int64* firstLineCharLen)
{
    CALL_STACK_MESSAGE1("CViewerWindow::ZeroLineSize()");
    fatalErr = FALSE;
    if (firstLineEndOff != NULL)
        *firstLineEndOff = -1;
    if (firstLineCharLen != NULL)
        *firstLineCharLen = -1;
    switch (Type)
    {
    case vtHex:
        return 16; // NOTE: 'firstLineEndOff' and 'firstLineCharLen' are not computed for Hex mode because it is not used yet
    case vtText:
    {
        __int64 offset = FindSeekBefore(SeekY, 2, fatalErr, firstLineEndOff, firstLineCharLen);
        if (fatalErr || ExitTextMode)
            return 0;
        return SeekY - offset;
    }
    }
    return 0;
}

__int64
CViewerWindow::FindBegin(__int64 seek, BOOL& fatalErr)
{
    CALL_STACK_MESSAGE2("CViewerWindow::FindBegin(%g,)", (double)seek);
    fatalErr = FALSE;
    switch (Type)
    {
    case vtHex:
        return seek - (seek % 16);
    case vtText:
        if (seek < TextStartOffset())
            return TextStartOffset();
        return FindSeekBefore(seek, 1, fatalErr);
    }
    return 0;
}

void CViewerWindow::ChangeType(CViewType type)
{
    CALL_STACK_MESSAGE2("CViewerWindow::ChangeType(%d)", type);
    __int64 startSel = StartSelection;
    __int64 endSel = EndSelection;
    SetToolTipOffset(-1);
    Type = type;
    __int64 oldSeekY = SeekY;
    BOOL fatalErr;
    FileChanged(NULL, FALSE, fatalErr, FALSE);
    if (!fatalErr && !ExitTextMode)
        FindNewSeekY(oldSeekY, fatalErr);
    if (fatalErr)
        FatalFileErrorOccured();
    if (fatalErr || ExitTextMode)
        return;
    if (startSel >= 0 && startSel < FileSize && // there was a valid selection
        endSel >= 0 && endSel < FileSize)
    {
        StartSelection = startSel; // restore the selection (helpful for orientation when switching modes)
        EndSelection = endSel;
    }
    InvalidateRect(HWindow, NULL, FALSE);
    UpdateWindow(HWindow); // so ViewSize is calculated for the next PageDown
}

BOOL CViewerWindow::GetOffsetOrXAbs(__int64 x, __int64* offset, __int64* offsetX, __int64 lineBegOff,
                                    __int64 lineCharLen, __int64 lineEndOff, BOOL& fatalErr, BOOL* onHexNum,
                                    BOOL getXFromOffset, __int64 findOffset, __int64* foundX)
{
    if (offset != NULL)
        *offset = -1;
    if (offsetX != NULL)
        *offsetX = x;
    fatalErr = FALSE;
    if (onHexNum != NULL)
        *onHexNum = FALSE;
    if (foundX != NULL)
        *foundX = -1;
    switch (Type)
    {
    case vtText:
    {
        if (lineBegOff > lineEndOff ||
            getXFromOffset && (findOffset < lineBegOff || findOffset > lineEndOff))
        {
            TRACE_C("Unexpected in CViewerWindow::GetOffsetOrXAbs().");
        }
        if (HasDecodedTextMode())
        {
            Sally::Unicode::DecodedRun visual;
            if (!DecodeTextRange(NULL, lineBegOff, lineEndOff, visual, fatalErr))
                return FALSE;
            if (fatalErr)
                return FALSE;

            Sally::Unicode::DecodedRun expanded;
            for (std::size_t i = 0; i < visual.CellCount(); ++i)
            {
                if (visual.Scalars[i] == L'\t')
                {
                    int tab = (int)(Configuration.TabSize - (expanded.CellCount() % Configuration.TabSize));
                    if (tab <= 0)
                        tab = 1;
                    while (tab-- > 0)
                        expanded.AppendCell(L' ', visual.RawStart[i], visual.RawEnd[i]);
                }
                else
                    expanded.AppendCell(visual.Scalars[i], visual.RawStart[i], visual.RawEnd[i]);
            }

            lineCharLen = (__int64)expanded.CellCount();
            if (getXFromOffset ? findOffset == lineEndOff : x >= lineCharLen)
            {
                if (foundX != NULL)
                    *foundX = lineCharLen;
                if (offset != NULL)
                    *offset = lineEndOff;
                if (offsetX != NULL)
                    *offsetX = lineCharLen;
                return TRUE;
            }
            if (getXFromOffset ? findOffset == lineBegOff : x <= 0)
            {
                if (foundX != NULL)
                    *foundX = 0;
                if (offset != NULL)
                    *offset = lineBegOff;
                if (offsetX != NULL)
                    *offsetX = 0;
                return TRUE;
            }

            for (std::size_t i = 0; i < expanded.CellCount(); ++i)
            {
                if (getXFromOffset)
                {
                    if (findOffset <= expanded.RawStart[i])
                    {
                        if (foundX != NULL)
                            *foundX = (__int64)i;
                        return TRUE;
                    }
                    if (findOffset <= expanded.RawEnd[i])
                    {
                        if (foundX != NULL)
                            *foundX = (__int64)i + 1;
                        return TRUE;
                    }
                }
                else if (x <= (__int64)i + 1)
                {
                    if (offset != NULL)
                    {
                        if (expanded.RawEnd[i] - expanded.RawStart[i] > 1)
                            *offset = x > (__int64)i ? expanded.RawEnd[i] : expanded.RawStart[i];
                        else
                            *offset = expanded.RawEnd[i];
                    }
                    if (offsetX != NULL)
                        *offsetX = (__int64)i + 1;
                    return TRUE;
                }
            }
            if (foundX != NULL)
                *foundX = lineCharLen;
            if (offset != NULL)
                *offset = lineEndOff;
            if (offsetX != NULL)
                *offsetX = lineCharLen;
            return TRUE;
        }
        if (getXFromOffset ? findOffset == lineEndOff : x >= lineCharLen)
        {
            if (foundX != NULL)
                *foundX = lineCharLen;
            if (offset != NULL)
                *offset = lineEndOff;
            if (offsetX != NULL)
                *offsetX = lineCharLen;
            return TRUE;
        }
        if (getXFromOffset ? findOffset == lineBegOff : x <= 0)
        {
            if (foundX != NULL)
                *foundX = 0;
            if (offset != NULL)
                *offset = lineBegOff;
            if (offsetX != NULL)
                *offsetX = 0;
            return TRUE;
        }
        __int64 off = lineBegOff, lineLen = 0;
        while (1)
        {
            __int64 readLen = APROX_LINE_LEN;
            if (off + readLen > lineEndOff)
                readLen = lineEndOff - off;
            __int64 len = Prepare(NULL, off, readLen, fatalErr);
            if (fatalErr)
                return FALSE;
            if (len > 0)
            {
                char* s = (char*)(Buffer + (off - Seek));
                while (len--)
                {
                    if (*s == '\t')
                    {
                        int tab = (int)(Configuration.TabSize - (lineLen % Configuration.TabSize));
                        if (!getXFromOffset && x >= lineLen && x <= lineLen + tab)
                        {
                            if (offset != NULL)
                                *offset = off + (x > lineLen + tab / 2 ? 1 : 0);
                            if (offsetX != NULL)
                                *offsetX = lineLen + (x > lineLen + tab / 2 ? tab : 0);
                            return TRUE;
                        }
                        lineLen += tab;
                    }
                    else
                        lineLen++;
                    s++;
                    off++;
                    if (getXFromOffset ? findOffset == off : x == lineLen)
                    {
                        if (foundX != NULL)
                            *foundX = lineLen;
                        if (offset != NULL)
                            *offset = off;
                        if (offsetX != NULL)
                            *offsetX = lineLen;
                        return TRUE;
                    }
                }
            }
            else // the last line is empty; nothing was read
            {
                if (offset != NULL)
                    *offset = lineBegOff;
                if (offsetX != NULL)
                    *offsetX = 0;
                if (foundX != NULL)
                    *foundX = 0;
                return TRUE;
            }
        }
        break; // execution never reaches here
    }

    case vtHex:
    {
        if (getXFromOffset)
            TRACE_C("CViewerWindow::GetOffsetOrXAbs(): Unsupported function!"); // we only support text mode
        __int64 foundOff = -1;
        if (x < 62 - 8 + HexOffsetLength)
        {
            if (onHexNum != NULL)
            {
                *onHexNum = (x > 9 - 8 + HexOffsetLength && x < 61 - 8 + HexOffsetLength) &&
                            ((((x - (9 - 8 + HexOffsetLength)) % 13) % 3) >= 1);
            }
            if (x > 10 - 8 + HexOffsetLength)
            {
                x = ((x - (9 - 8 + HexOffsetLength)) - (x - (9 - 8 + HexOffsetLength)) / 13) / 3;
            }
            else
                x = 0;
            foundOff = lineBegOff + x;
            if (foundOff > FileSize)
                foundOff = FileSize;
        }
        else
        {
            if (onHexNum != NULL)
                *onHexNum = TRUE; // even though it is in the text column, it is directly on the character...
            if (x > 62 + 16 - 8 + HexOffsetLength)
                x = 62 + 16 - 8 + HexOffsetLength;
            foundOff = lineBegOff + (x - (62 - 8 + HexOffsetLength));
            if (foundOff > FileSize)
                foundOff = FileSize;
        }
        if (offset != NULL)
            *offset = foundOff;
        return TRUE;
    }
    }
    return FALSE;
}

BOOL CViewerWindow::GetOffset(__int64 x, __int64 y, __int64& offset, BOOL& fatalErr,
                              BOOL leftMost, BOOL* onHexNum)
{
    CALL_STACK_MESSAGE4("CViewerWindow::GetOffset(%d, %d, , , %d)", (int)x, (int)y, leftMost);
    fatalErr = FALSE;
    if (onHexNum != NULL)
        *onHexNum = FALSE;
    if (x >= 0 && y >= 0 && x < Width && y < Height)
    {
        if (!leftMost)
            x = (x - BORDER_WIDTH + CharWidth / 2) / CharWidth;
        else
            x = (x - BORDER_WIDTH) / CharWidth;
        y = y / CharHeight;
        switch (Type)
        {
        case vtText:
        {
            if (3 * y + 2 < LineOffset.Count)
            {
                return GetOffsetOrXAbs(x + OriginX, &offset, NULL, LineOffset[(int)(3 * y)], LineOffset[(int)(3 * y + 2)],
                                       LineOffset[(int)(3 * y + 1)], fatalErr, onHexNum);
            }
            break;
        }

        case vtHex:
            return GetOffsetOrXAbs(x + OriginX, &offset, NULL, SeekY + y * 16, 0, 0, fatalErr, onHexNum);
        }
    }
    return FALSE;
}

void CViewerWindow::SetScrollBar()
{
    CALL_STACK_MESSAGE1("CViewerWindow::SetScrollBar()");
    if (EnableSetScroll)
    { // vertical scrollbar
        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(HWindow, SB_VERT, &si);

        __int64 max = ViewSize + MaxSeekY;
        ScrollScaleY = ((double)max) / 20000.0;
        if (ScrollScaleY < 0.00001)
            ScrollScaleY = 0.00001; // against "divide by zero"
        int page = (int)(ViewSize / ScrollScaleY + 0.5 + 1);
        if (max == 0 || si.nMin != 0 || si.nMax != max / ScrollScaleY + 0.5 + 1 ||
            si.nPage != (DWORD)page ||
            si.nPos != SeekY / ScrollScaleY + 0.5) // if it needs to be set ...
        {
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
            si.nMin = 0;
            if (max != 0 && MaxSeekY != 0)
            {
                si.nMax = (int)(max / ScrollScaleY + 0.5 + 1);
                si.nPage = page;
                si.nPos = (int)(SeekY / ScrollScaleY + 0.5);
            }
            else
            {
                si.nMax = 0;
                si.nPage = 0;
                si.nPos = 0;
            }
            SetScrollInfo(HWindow, SB_VERT, &si, TRUE);
        }

        // horizontal scrollbar
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(HWindow, SB_HORZ, &si);

        max = OriginX + (Width - BORDER_WIDTH) / CharWidth;
        __int64 maxLL = GetMaxVisibleLineLen();
        if (max < maxLL)
            max = maxLL;

        ScrollScaleX = ((double)max) / 20000.0;
        if (ScrollScaleX < 0.00001)
            ScrollScaleX = 0.00001; // against "divide by zero"
        page = (int)(((Width - BORDER_WIDTH) / CharWidth) / ScrollScaleX + 0.5 + 2);
        if (max == 0 || si.nMin != 0 || si.nMax != max / ScrollScaleX + 0.5 + 1 ||
            si.nPage != (DWORD)page ||
            si.nPos != OriginX / ScrollScaleX + 0.5) // if it needs to be set ...
        {
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
            si.nMin = 0;
            if (max != 0)
            {
                si.nMax = (int)(max / ScrollScaleX + 0.5 + 1);
                si.nPage = page;
                si.nPos = (int)(OriginX / ScrollScaleX + 0.5);
            }
            else
            {
                si.nMax = 0;
                si.nPage = 0;
                si.nPos = 0;
            }
            SetScrollInfo(HWindow, SB_HORZ, &si, TRUE);
        }
    }
}

// How much of the selection the Find dialog's auto-fill is allowed to read.
// The rules, and why the seed is bounded while the pattern is not, live in
// viewer_find_seed.h so a headless test can compile them.

// The WIDE selection getter.
//
// The retired fixed-capacity byte getter decoded this selection and then narrowed
// it again, so a CJK word selected in a decoded view could not become the Find
// needle shown on screen. This returns the decoded text as it is.
//
// Only meaningful in a decoded view; a byte-mode selection has no wide form and
// returns FALSE so the caller keeps its byte path.
BOOL CViewerWindow::GetFindTextW(std::wstring& out)
{
    CALL_STACK_MESSAGE1("CViewerWindow::GetFindTextW()");
    out.clear();
    if (StartSelection == EndSelection || !HasDecodedTextMode())
        return FALSE;

    __int64 startSel = min(StartSelection, EndSelection);
    __int64 endSel = max(StartSelection, EndSelection);
    startSel = max(startSel, TextStartOffset());
    endSel = max(endSel, startSel);
    endSel = Sally::Viewer::ClampFindSeedRange(startSel, endSel);

    BOOL fatalErr = FALSE;
    Sally::Unicode::DecodedRun run;
    if (!DecodeTextRange(NULL, startSel, endSel, run, fatalErr) || fatalErr)
    {
        if (fatalErr)
            FatalFileErrorOccured();
        return FALSE;
    }
    out = run.Text;
    Sally::Viewer::ClampFindSeedText(out);
    return !out.empty();
}

BOOL CViewerWindow::GetFindBytes(std::string& bytes)
{
    CALL_STACK_MESSAGE1("CViewerWindow::GetFindBytes()");
    if (StartSelection == EndSelection)
        return FALSE;

    __int64 startSel = min(StartSelection, EndSelection);
    __int64 endSel = max(StartSelection, EndSelection);
    if (startSel < 0 || endSel < startSel ||
        static_cast<unsigned __int64>(endSel - startSel) >
            static_cast<unsigned __int64>(std::string().max_size()))
        return FALSE;
    // Seed only - see viewer_find_seed.h. Without this the hex Find box is filled
    // with three characters per selected byte, for the whole selection.
    endSel = Sally::Viewer::ClampFindSeedRange(startSel, endSel);

    std::string candidate;
    try
    {
        candidate.resize(static_cast<size_t>(endSel - startSel));
    }
    catch (const std::bad_alloc&)
    {
        return FALSE;
    }
    catch (const std::length_error&)
    {
        return FALSE;
    }

    BOOL fatalErr = FALSE;
    __int64 off = startSel;
    while (off < endSel)
    {
        int l = (int)Prepare(NULL, off, min(1000, endSel - off), fatalErr);
        if (fatalErr)
            break;
        if (l == 0)
            return FALSE;
        memcpy(candidate.data() + static_cast<size_t>(off - startSel),
               Buffer + (off - Seek), static_cast<size_t>(l));
        off += l;
    }

    if (fatalErr)
    {
        FatalFileErrorOccured();
        return FALSE;
    }
    else
    {
        bytes.swap(candidate);
        return TRUE;
    }
}

BOOL CViewerWindow::CheckSelectionIsNotTooBig(HWND parent, BOOL* msgBoxDisplayed)
{
    if (msgBoxDisplayed != NULL)
        *msgBoxDisplayed = FALSE;
    __int64 startSel = min(StartSelection, EndSelection);
    if (startSel == -1)
        startSel = 0;
    __int64 endSel = max(StartSelection, EndSelection);
    if (endSel == -1)
        endSel = 0;
    if (endSel - startSel > 100 * 1024 * 1024)
    {
        if (TooBigSelAction != 0 /* ask */)
            return TooBigSelAction == 1 /* YES */;

        if (msgBoxDisplayed != NULL)
            *msgBoxDisplayed = TRUE;
        PromptResult res = gPrompter->AskYesNoCancel(LoadStrW(IDS_VIEWERTITLE), LoadStrW(IDS_VIEWER_BLOCKTOOBIG));
        if (res.type == PromptResult::kYes)
            TooBigSelAction = 2 /* NO */; // is the question skipped? YES = do not copy/do not drag
        if (res.type == PromptResult::kNo)
            TooBigSelAction = 1 /* YES */;
        return res.type == PromptResult::kNo;
    }
    return TRUE; // less than 100MB = YES
}

HGLOBAL
CViewerWindow::GetSelectedText(BOOL& fatalErr)
{
    fatalErr = FALSE;
    CALL_STACK_MESSAGE1("CViewerWindow::GetSelectedText()");
    __int64 startSel = min(StartSelection, EndSelection);
    if (startSel == -1)
        startSel = 0;
    __int64 endSel = max(StartSelection, EndSelection);
    if (endSel == -1)
        endSel = 0;
    if (startSel == endSel)
        startSel = endSel = 0;
    BOOL lowMem = FALSE;
#ifndef _WIN64
    if (endSel - startSel < (unsigned __int64)0xFFFFFFFF) // we can at least try (the 32-bit version really cannot handle more than 4GB)
#endif                                                    // _WIN64
    {
        HGLOBAL h = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (DWORD)(endSel - startSel + 1)));
        if (h != NULL)
        {
            char* s = (char*)HANDLES(GlobalLock(h));
            if (s != NULL)
            {
                __int64 off = startSel, len;
                while (off < endSel)
                {
                    len = Prepare(NULL, off, min(1000, endSel - off), fatalErr);
                    if (fatalErr)
                        break;
                    if (len == 0)
                    {
                        HANDLES(GlobalUnlock(h));
                        NOHANDLES(GlobalFree(h));
                        return NULL;
                    }
                    memcpy(s, Buffer + (off - Seek), (int)len);
                    s += len;
                    off += len;
                }
                *s = 0;
                HANDLES(GlobalUnlock(h));
                if (!fatalErr)
                    return h;
            }
            else
                lowMem = TRUE;
            NOHANDLES(GlobalFree(h));
        }
        else
            lowMem = TRUE;
    }
#ifndef _WIN64
    else
        lowMem = TRUE;
#endif // _WIN64
    if (lowMem)
    {
        SalMessageBoxViewerPaintBlocked(HWindow, GetErrorTextOwned(ERROR_NOT_ENOUGH_MEMORY).c_str(), LoadStrW(IDS_ERRORTITLE),
                                        MB_OK | MB_ICONEXCLAMATION);
    }
    return NULL;
}

HGLOBAL
CViewerWindow::GetSelectedTextW(BOOL& fatalErr, int* textLen)
{
    fatalErr = FALSE;
    if (textLen != NULL)
        *textLen = 0;
    __int64 startSel = min(StartSelection, EndSelection);
    if (startSel == -1)
        startSel = TextStartOffset();
    __int64 endSel = max(StartSelection, EndSelection);
    if (endSel == -1)
        endSel = TextStartOffset();
    startSel = max(startSel, TextStartOffset());
    endSel = max(endSel, startSel);
    endSel = min(endSel, FileSize);

    Sally::Unicode::DecodedRun run;
    if (!DecodeTextRange(NULL, startSel, endSel, run, fatalErr))
        return NULL;
    if (fatalErr)
        return NULL;

    SIZE_T bytes = (run.Text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, bytes));
    if (h == NULL)
    {
        SalMessageBoxViewerPaintBlocked(HWindow, GetErrorTextOwned(ERROR_NOT_ENOUGH_MEMORY).c_str(), LoadStrW(IDS_ERRORTITLE),
                                        MB_OK | MB_ICONEXCLAMATION);
        return NULL;
    }
    wchar_t* s = (wchar_t*)HANDLES(GlobalLock(h));
    if (s == NULL)
    {
        NOHANDLES(GlobalFree(h));
        return NULL;
    }
    if (!run.Text.empty())
        memcpy(s, run.Text.data(), run.Text.size() * sizeof(wchar_t));
    s[run.Text.size()] = 0;
    HANDLES(GlobalUnlock(h));
    if (textLen != NULL)
        *textLen = (int)run.Text.size();
    return h;
}

BOOL CViewerWindow::FindDecodedLiteral(HANDLE* hFile, BOOL forward, WORD flags, BOOL& foundMatch, BOOL& fatalErr)
{
    foundMatch = FALSE;
    fatalErr = FALSE;
    if (!HasDecodedTextMode() || FindDialog.Text.empty())
        return FALSE;

    const std::wstring& pattern = FindDialog.Text;
    std::size_t patternCells = Sally::Unicode::CountPatternCells(pattern);
    if (patternCells == 0)
        return TRUE;

    bool caseSensitive = (flags & sfCaseSensitive) != 0;
    __int64 textStart = TextStartOffset();
    if (forward)
    {
        __int64 off = max(FindOffset, textStart);
        while (off < FileSize)
        {
            __int64 end = min(FileSize, off + FIND_LINE_LEN);
            Sally::Unicode::DecodedRun run;
            if (!DecodeTextRange(hFile, off, end, run, fatalErr, end >= FileSize))
                return FALSE;
            if (fatalErr)
                return FALSE;
            if (run.CellCount() == 0)
                break;

            std::size_t startCell = 0;
            while (startCell < run.CellCount() && run.RawEnd[startCell] <= FindOffset)
                startCell++;

            Sally::Unicode::LiteralMatch match;
            if (Sally::Unicode::FindLiteralForward(run, pattern, caseSensitive, FindDialog.WholeWords != FALSE,
                                                   startCell, match))
            {
                StartSelection = match.RawStart;
                EndSelection = match.RawEnd;
                FindOffset = EndSelection;
                SelectionIsFindResult = TRUE;
                foundMatch = TRUE;
                return TRUE;
            }

            if (run.CellCount() > patternCells)
                off = run.RawStart[run.CellCount() - patternCells + 1];
            else
                off = run.RawEnd[0];
            if (off <= FindOffset)
                off = FindOffset + 1;
            FindOffset = off;
        }
    }
    else
    {
        __int64 off = min(FindOffset, FileSize);
        while (off > textStart)
        {
            __int64 start = max(textStart, off - FIND_LINE_LEN);
            Sally::Unicode::DecodedRun run;
            if (!DecodeTextRange(hFile, start, off, run, fatalErr, off >= FileSize))
                return FALSE;
            if (fatalErr)
                return FALSE;
            if (run.CellCount() == 0)
                break;

            std::size_t limitCell = run.CellCount();
            while (limitCell > 0 && run.RawStart[limitCell - 1] >= FindOffset)
                limitCell--;

            Sally::Unicode::LiteralMatch match;
            if (Sally::Unicode::FindLiteralBackward(run, pattern, caseSensitive, FindDialog.WholeWords != FALSE,
                                                    limitCell, match))
            {
                StartSelection = match.RawStart;
                EndSelection = match.RawEnd;
                FindOffset = StartSelection;
                SelectionIsFindResult = TRUE;
                foundMatch = TRUE;
                return TRUE;
            }

            if (start == textStart)
                break;
            if (run.CellCount() > patternCells)
                off = run.RawEnd[patternCells - 1];
            else
                off = run.RawStart[0];
            if (off >= FindOffset)
                off = FindOffset - 1;
            FindOffset = off;
        }
    }
    return TRUE;
}

void CViewerWindow::SetToolTipOffset(__int64 offset)
{
    if (HToolTip != NULL)
    {
        if (ToolTipOffset != offset)
        {
            SendMessage(HToolTip, TTM_ACTIVATE, FALSE, 0);
        }
        if (offset != -1)
        {
            SendMessage(HToolTip, TTM_ACTIVATE, TRUE, 0);
        }
    }
    ToolTipOffset = offset;
}
