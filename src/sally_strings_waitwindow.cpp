// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include "common/IRegistry.h" // wide facades
#include "common/reg_sz_safe_length.h" // SetValueW REG_SZ scan bound
#include "common/HistoryValueIo.h" // history value shapes
#include "common/SubstResolution.h" // wide SUBST resolution
#include "common/fsutil.h" // GetRootPathW / SkipRootW / IsUNCPathW
#include "common/IFileSystem.h"
#include "common/IPathService.h"
#include "common/DiagnosticTextEncoding.h"
#include "common/LegacyConfigTextEncoding.h"

#include <cwctype>
#include <vector>
#include "cfgdlg.h"
#include "mainwnd.h"
#include "dialogs.h"
#include "tasklist.h"
#include "versinfo.h"
#include "logo.h"
#include "reglib\src\regparse.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/unicode/WideVariableExpansion.h"
#include "common/unicode/AnsiToolPathPolicy.h"

static BOOL BuildModuleRelativePathW(HINSTANCE module, const wchar_t* relativePath, std::wstring& path)
{
    std::wstring modulePath;
    if (gPathService == NULL || !gPathService->GetModuleFileName(module, modulePath).success)
        return FALSE;
    const size_t slash = modulePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return FALSE;
    path.assign(modulePath, 0, slash + 1);
    path.append(relativePath);
    return TRUE;
}

static std::wstring GetModuleFileNameForTrace(HMODULE module)
{
    std::wstring path;
    if (gPathService != NULL && gPathService->GetModuleFileName(module, path).success)
        return path;
    return L"(unknown module)";
}

// ****************************************************************************

class C__StrCriticalSection
{
public:
    CRITICAL_SECTION cs;

    // Raw Win32 init/delete on purpose: these globals live in init_seg(lib),
    // and cross-TU order inside the lib segment follows object link order —
    // __MSInit (ms_init.cpp) may not have initialized the HANDLES
    // infrastructure yet, so a HANDLES() call here can enter an unconstructed
    // critical section (crashes when the objects are packaged as a static
    // library, e.g. the private e2e host). Runtime Enter/Leave still go
    // through HANDLES — those records pair independently of the init call.
    C__StrCriticalSection() { ::InitializeCriticalSection(&cs); }
    ~C__StrCriticalSection() { ::DeleteCriticalSection(&cs); }
};

// ensure timely construction of the critical section
#pragma warning(disable : 4073)
#pragma init_seg(lib)
C__StrCriticalSection __StrCriticalSection;

// ****************************************************************************

std::wstring LoadStrOwned(int resID, HINSTANCE hInstance)
{
    if (hInstance == NULL)
        hInstance = HLanguage;
#ifdef _DEBUG
    if (hInstance == NULL)
        TRACE_E("LoadStrOwned: hInstance == NULL");
#endif // _DEBUG

    const wchar_t* resourceText = NULL;
    const int size = LoadStringW(hInstance, resID,
                                 reinterpret_cast<LPWSTR>(&resourceText), 0);
    if (size > 0 && resourceText != NULL)
        return std::wstring(resourceText, static_cast<size_t>(size));

    TRACE_E("Error in LoadStrOwned(" << resID << ").");
    return L"ERROR LOADING STRING";
}

WCHAR* LoadStrW(int resID, HINSTANCE hInstance)
{
    static WCHAR buffer[10000]; // buffer for many strings
    static WCHAR* act = buffer;

    HANDLES(EnterCriticalSection(&__StrCriticalSection.cs));

    if (10000 - (act - buffer) < 200)
        act = buffer;

    if (hInstance == NULL)
        hInstance = HLanguage;
#ifdef _DEBUG
    // better make sure no one calls us before the resource handle is initialized
    if (hInstance == NULL)
        TRACE_E("LoadStrW: hInstance == NULL");
#endif // _DEBUG

RELOAD:
    int size = LoadStringW(hInstance, resID, act, 10000 - (int)(act - buffer));
    // size contains the number of copied characters without the terminator
    //  DWORD error = GetLastError();
    WCHAR* ret;
    if (size != 0 /* || error == NO_ERROR*/) // error is NO_ERROR even if the string does not exist - useless
    {
        if ((10000 - (act - buffer) == size + 1) && (act > buffer))
        {
            // if the string was exactly at the end of the buffer, it may
            // have been truncated -- if we can move the window
            // to the beginning of the buffer, load the string once more
            act = buffer;
            goto RELOAD;
        }
        else
        {
            ret = act;
            act += size + 1;
        }
    }
    else
    {
        TRACE_E("Error in LoadStrW(" << resID << ")." /*"): " << GetErrorTextOwned(error).c_str()*/);
        static wchar_t bufferError[] = L"ERROR LOADING WIDE STRING";
        ret = bufferError;
    }

    HANDLES(LeaveCriticalSection(&__StrCriticalSection.cs));

    return ret;
}

//*****************************************************************************
//
// GetErrorText

std::wstring GetErrorTextOwned(DWORD error)
{
    wchar_t prefix[32];
    swprintf_s(prefix, (static_cast<int>(error) < 0 ? L"(%08X) " : L"(%u) "), error);

    wchar_t* systemText = NULL;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        NULL, error,
                                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<wchar_t*>(&systemText), 0, NULL);
    std::wstring result(prefix);
    if (length != 0 && systemText != NULL && *systemText != 0)
        result.append(systemText, length);
    else if (static_cast<int>(error) < 0)
        result = FormatStrW(L"System error %08X, text description is not available.", error);
    else
        result = FormatStrW(L"System error %u, text description is not available.", error);
    if (systemText != NULL)
        LocalFree(systemText);
    return result;
}

std::wstring GetWindowTextStringW(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    if (length <= 0)
        return {};

    std::wstring text((size_t)length, L'\0');
    const int copied = GetWindowTextW(window, text.data(), length + 1);
    text.resize(copied > 0 ? (size_t)copied : 0);
    return text;
}

// ****************************************************************************

void ClearComboboxListbox(HWND hCombo)
{
    // keep the current text; clear the listbox
    const int length = GetWindowTextLengthW(hCombo);
    std::vector<wchar_t> text((size_t)(length > 0 ? length : 0) + 1, L'\0');
    GetWindowTextW(hCombo, text.data(), (int)text.size());
    SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
    SetWindowTextW(hCombo, text.data());
}

// ****************************************************************************

BOOL SalamanderActive()
{
    HWND foreground = GetForegroundWindow();
    if (foreground == NULL)
        return TRUE; // during the Salamander activation blocked by the wait window, GetForegroundWindow() returns NULL
    DWORD pid;
    GetWindowThreadProcessId(foreground, &pid);
    return pid == GetCurrentProcessId();
    //  return MainWindow != NULL && foreground == MainWindow->HWindow;
}

// ****************************************************************************

BOOL SafeWaitMessageThreadStarted = FALSE;
DWORD SafeWaitMessageThreadID = 0;
// wide, matching CWaitWindow::Text
std::wstring SafeWaitMessageText;
std::wstring SafeWaitMessageCaption;
CRITICAL_SECTION SafeWaitMessageTextSection; // for synchronizing access to SafeWaitMessageText
BOOL SafeWaitMessageCallerSet = FALSE;
unsigned SafeWaitMessageCallerID = 0;
BOOL SafeWaitWindowClosePressed = FALSE;

class C__SafeWaitMessageCallerSetSection
{
public:
    CRITICAL_SECTION cs;

    // Raw init/delete: this global is in init_seg(lib) and may construct before
    // __MSInit has initialized the HANDLES infrastructure (see the
    // C__StrCriticalSection comment above).
    C__SafeWaitMessageCallerSetSection() { ::InitializeCriticalSection(&cs); }
    ~C__SafeWaitMessageCallerSetSection() { ::DeleteCriticalSection(&cs); }
};

C__SafeWaitMessageCallerSetSection SafeWaitMessageCallerSetSection;

void ThreadSafeWaitWindowFBody(BOOL showCloseButton)
{
    CALL_STACK_MESSAGE1("ThreadSafeWaitWindowFBody()");
    SetThreadNameInVCAndTrace(L"SafeWaitWindow");
    TRACE_I("Begin");

    CWaitWindow waitWnd(NULL, 0, showCloseButton, ooStatic);
    MSG msg;
    UINT_PTR timer = 0;
    BOOL run = TRUE;
    HWND hForegroundWnd = NULL;
    while (run && GetMessageW(&msg, NULL, 0, 0))
    {
        switch (msg.message)
        {
        case WM_USER_CREATEWAITWND:
        {
            hForegroundWnd = (HWND)msg.wParam; // if it is not NULL, open the window only if hForegroundWnd is active
            if ((int)msg.lParam > 0)           // if the delay > 0
            {
                if (timer != 0)
                {
                    // kill the timer
                    KillTimer(NULL, timer);
                    // clear the message queue of any WM_TIMER messages
                    MSG msg2;
                    while (PeekMessageW(&msg2, NULL, WM_TIMER, WM_TIMER, PM_REMOVE))
                        ;
                    timer = 0;
                }
                timer = SetTimer(NULL, 0, (UINT)msg.lParam, NULL);
                if (timer != 0)
                    break;
            }
        }
        case WM_TIMER: // when delay == 0, WM_USER_CREATEWAITWND arrives here as well
        {
            if (msg.message == WM_USER_CREATEWAITWND || msg.wParam == timer)
            {
                if (timer != 0)
                {
                    // kill the timer
                    KillTimer(NULL, timer);
                    // clear the message queue of any WM_TIMER messages
                    MSG msg2;
                    while (PeekMessageW(&msg2, NULL, WM_TIMER, WM_TIMER, PM_REMOVE))
                        ;
                    timer = 0;
                }

                if (waitWnd.HWindow == NULL)
                {
                    HANDLES(EnterCriticalSection(&SafeWaitMessageTextSection));
                    if (!SafeWaitMessageText.empty())
                        waitWnd.SetText(SafeWaitMessageText.c_str());
                    waitWnd.SetCaption(SafeWaitMessageCaption.c_str());
                    HANDLES(LeaveCriticalSection(&SafeWaitMessageTextSection));
                    waitWnd.Create(hForegroundWnd);
                }

                BOOL showWindow;
                if (hForegroundWnd != NULL)
                {
                    // show the window only when hForegroundWnd is active
                    showWindow = GetForegroundWindow() == hForegroundWnd;
                }
                else
                {
                    // check whether any window of the process is active; otherwise do not show the window
                    HWND foreground = GetForegroundWindow();
                    DWORD pid;
                    GetWindowThreadProcessId(foreground, &pid);
                    showWindow = pid == GetCurrentProcessId();
                }

                // seemingly redundant call to SetWindowPos twice, but otherwise the window unfortunately stays
                // all the way at the bottom (above the desktop but below all other windows)
                SetWindowPos(waitWnd.HWindow, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
                SetWindowPos(waitWnd.HWindow, HWND_NOTOPMOST, 0, 0, 0, 0,
                             (showWindow ? SWP_SHOWWINDOW : 0) | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
            }
            break;
        }

        case WM_USER_SHOWWAITWND:
        {
            if (waitWnd.HWindow == NULL) // if the window does not exist yet, there is nothing to do
                break;
            BOOL show = (BOOL)msg.wParam;
            if (show)
            {
                // postpone the display for a brief moment; the reason is the case where the display
                // happens because hForegroundWnd is activated after closing a MessageBox;
                // if the user interrupted the operation there, an immediate display would
                // cause a brief flash of the wait window and its immediate destruction;
                // the delay prevents that
                if (timer == 0)
                    timer = SetTimer(NULL, 0, 100, NULL); // show the window again after 100ms
            }
            else
                ShowWindow(waitWnd.HWindow, SW_HIDE);
            break;
        }

        case WM_USER_SETWAITMSG:
        {
            HANDLES(EnterCriticalSection(&SafeWaitMessageTextSection));
            if (!SafeWaitMessageText.empty())
                waitWnd.SetText(SafeWaitMessageText.c_str());
            HANDLES(LeaveCriticalSection(&SafeWaitMessageTextSection));
            break;
        }
            /*
      case WM_USER_ACTIVATEWAITMSG:
      {
        if (waitWnd.HWindow != NULL)   // only if the window is open
        {
          // seemingly redundant call to SetWindowPos twice, but otherwise the window unfortunately stays
          // all the way at the bottom (above the desktop but below all other windows)

          // It is necessary to show the window only in the second operation,
          // because changing the Z-order while the window is shown means the window loses
          // its cached bitmap (it has CS_SAVEBITS set) and after it closes
          // it triggers a repaint of the windows beneath it.

          BOOL visible = IsWindowVisible(waitWnd.HWindow);
          SetWindowPos(waitWnd.HWindow, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
          SetWindowPos(waitWnd.HWindow, HWND_NOTOPMOST, 0, 0, 0, 0,
                       visible ? SWP_SHOWWINDOW : 0 | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
        }
        break;
      }
*/
        case WM_USER_DESTROYWAITWND:
        {
            if (timer != 0)
            {
                // kill the timer
                KillTimer(NULL, timer);
                // clear the message queue of any WM_TIMER messages
                MSG msg2;
                while (PeekMessageW(&msg2, NULL, WM_TIMER, WM_TIMER, PM_REMOVE))
                    ;
                timer = 0;
            }
            if (waitWnd.HWindow != NULL) // only if the window is open
            {
                DestroyWindow(waitWnd.HWindow);
                waitWnd.HWindow = NULL;
            }
            if (msg.wParam)
                run = FALSE; // terminate the thread
            hForegroundWnd = NULL;
            break;
        }

        default:
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            break;
        }
        }
    }

    HANDLES(DeleteCriticalSection(&SafeWaitMessageTextSection));
    SafeWaitMessageThreadStarted = FALSE; // we have finished
    SafeWaitMessageText.clear();
    SafeWaitMessageCaption.clear();
    TRACE_I("End");
}

void ThreadSafeWaitWindowFEH(BOOL showCloseButton)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        ThreadSafeWaitWindowFBody(showCloseButton);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread SafeWaitWindow: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // more forceful exit (this one still invokes something)
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI ThreadSafeWaitWindowF(void* param)
{
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    ThreadSafeWaitWindowFEH((BOOL)(UINT_PTR)param);
    return 0;
}

void CreateSafeWaitWindow(const wchar_t* message, const wchar_t* caption,
                          int delay, BOOL showCloseButton, HWND hForegroundWnd)
{
    HANDLES(EnterCriticalSection(&SafeWaitMessageCallerSetSection.cs));
    if (!SafeWaitMessageCallerSet) // only one message (check for availability)
    {
        SafeWaitMessageCallerSet = TRUE;
        HANDLES(LeaveCriticalSection(&SafeWaitMessageCallerSetSection.cs));
        SafeWaitWindowClosePressed = FALSE;
        SafeWaitMessageCallerID = GetCurrentThreadId();
        if (!SafeWaitMessageThreadStarted) // the thread is not running
        {
            HANDLE thread = HANDLES(CreateThread(NULL, 0, ThreadSafeWaitWindowF,
                                                 (void*)(UINT_PTR)showCloseButton, 0, &SafeWaitMessageThreadID));
            if (thread == NULL)
            {
                TRACE_E("Unable to start ThreadSafeWaitWindow thread.");
                return;
            }
            SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL); // so it actually wins against the main thread
            AddAuxThread(thread);
            HANDLES(InitializeCriticalSection(&SafeWaitMessageTextSection));
            SafeWaitMessageThreadStarted = TRUE;
        }

        HANDLES(EnterCriticalSection(&SafeWaitMessageTextSection));
        SafeWaitMessageText = message ? message : L"";
        SafeWaitMessageCaption = caption ? caption : L"";
        HANDLES(LeaveCriticalSection(&SafeWaitMessageTextSection));

        while (PostThreadMessage(SafeWaitMessageThreadID, WM_USER_CREATEWAITWND, (WPARAM)hForegroundWnd, delay) == 0)
        {
            if (GetLastError() == ERROR_INVALID_THREAD_ID)
                Sleep(100); // not started yet, wait
            else
                break; // different error
        }
    }
    else
    { // can happen - if the internal viewer searches and switches to the main window, it tries to bring up another safe-wait window and the message appears
        HANDLES(LeaveCriticalSection(&SafeWaitMessageCallerSetSection.cs));
        TRACE_I("Incorrect call to CreateSafeWaitWindow() from " << (SafeWaitMessageCallerID == GetCurrentThreadId() ? "owner" : "strange") << " thread");
    }
}

void DestroySafeWaitWindow(BOOL killThread)
{
    HANDLES(EnterCriticalSection(&SafeWaitMessageCallerSetSection.cs));
    if (killThread ||                                        // kill applies to everyone
        SafeWaitMessageCallerSet &&                          // the window is created
            SafeWaitMessageCallerID == GetCurrentThreadId()) // this is the thread that opened it
    {
        SafeWaitMessageCallerSet = FALSE;
        HANDLES(LeaveCriticalSection(&SafeWaitMessageCallerSetSection.cs));
        SafeWaitMessageCallerID = 0;

        if (SafeWaitMessageThreadStarted) // the thread is running; hide the window and possibly destroy it
        {
            PostThreadMessage(SafeWaitMessageThreadID, WM_USER_DESTROYWAITWND, killThread, 0);
        }
    }
    else
    { // can happen - if the internal viewer searches and switches to the main window, it tries to bring up another safe-wait window and the message appears
        HANDLES(LeaveCriticalSection(&SafeWaitMessageCallerSetSection.cs));
        TRACE_I("Incorrect call to DestroySafeWaitWindow()");
    }
}

BOOL GetSafeWaitWindowClosePressed()
{
    HANDLES(EnterCriticalSection(&SafeWaitMessageCallerSetSection.cs));
    if (SafeWaitMessageCallerSet &&                      // the window is created
        SafeWaitMessageCallerID == GetCurrentThreadId()) // this is the thread that opened it
    {
        HANDLES(LeaveCriticalSection(&SafeWaitMessageCallerSetSection.cs));
        if (SafeWaitMessageThreadStarted) // the thread is running
        {
            return SafeWaitWindowClosePressed;
        }
    }
    else
        HANDLES(LeaveCriticalSection(&SafeWaitMessageCallerSetSection.cs));
    // for example, if the wait window is open for the internal viewer and the panel
    // (Ctrl+Shift+F10) requests another wait window, the new window does not appear;
    // at the same time, we must not return TRUE to the main Salamander thread when Close
    // button is clicked in the viewer's wait window
    return FALSE;
}

BOOL UserWantsToCancelSafeWaitWindow()
{
    return (GetAsyncKeyState(VK_ESCAPE) & 0x8001) && SalamanderActive() || GetSafeWaitWindowClosePressed();
}

void ShowSafeWaitWindow(BOOL show)
{
    HANDLES(EnterCriticalSection(&SafeWaitMessageCallerSetSection.cs));
    if (SafeWaitMessageCallerSet &&                      // the window is created
        SafeWaitMessageCallerID == GetCurrentThreadId()) // this is the thread that opened it
    {
        HANDLES(LeaveCriticalSection(&SafeWaitMessageCallerSetSection.cs));
        if (SafeWaitMessageThreadStarted) // the thread is running; send a command to show or hide
        {
            PostThreadMessage(SafeWaitMessageThreadID, WM_USER_SHOWWAITWND, show, 0);
            // we must reset the pressed button and this is a good opportunity,
            // because the user clearly noticed it was pressed (otherwise they would not call us now)
            SafeWaitWindowClosePressed = FALSE;
        }
    }
    else
        HANDLES(LeaveCriticalSection(&SafeWaitMessageCallerSetSection.cs));
}

void SetSafeWaitWindowText(const wchar_t* message)
{
    HANDLES(EnterCriticalSection(&SafeWaitMessageCallerSetSection.cs));
    if (SafeWaitMessageCallerSet &&                      // the window is created
        SafeWaitMessageCallerID == GetCurrentThreadId()) // this is the thread that opened it
    {
        HANDLES(LeaveCriticalSection(&SafeWaitMessageCallerSetSection.cs));
        if (SafeWaitMessageThreadStarted) // the thread is running; send a command to show or hide
        {
            HANDLES(EnterCriticalSection(&SafeWaitMessageTextSection));
            SafeWaitMessageText = message ? message : L"";
            HANDLES(LeaveCriticalSection(&SafeWaitMessageTextSection));
            PostThreadMessage(SafeWaitMessageThreadID, WM_USER_SETWAITMSG, 0, 0);
        }
    }
    else
        HANDLES(LeaveCriticalSection(&SafeWaitMessageCallerSetSection.cs));
}

// ****************************************************************************

// WIDE PRIMARY. The narrow form above always did
// GetFileAttributesW(AnsiToWide(...)), so the wide one is not a new
// implementation - it is the old one with the conversion REMOVED. A caller that
// already holds a wide path no longer round-trips through CP_ACP to reach it.
BOOL FileExistsW(const wchar_t* fileName)
{
    const DWORD attr = gFileSystem->GetFileAttributes(fileName);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

// 2026-08-25: the narrow DirExists(char*) thin adapter was deleted -
// confirmed-dead (zero callers anywhere in core; gtest_win32_isolation already asserted the
// wide DirExistsW sibling is used at the one call site that could have used either).
//
// WIDE PRIMARY, same reasoning as FileExistsW above.
BOOL DirExistsW(const wchar_t* dirName)
{
    const DWORD attr = gFileSystem->GetFileAttributes(dirName);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

// ****************************************************************************

int WideVarErrorResourceID(sally::unicode::WideVarErrorKind kind)
{
    using sally::unicode::WideVarErrorKind;
    switch (kind)
    {
    case WideVarErrorKind::UnmatchedParenthesis:
        return IDS_EXP_UNMATCHEDPAR;
    case WideVarErrorKind::InvalidVariableWidth:
        return IDS_EXP_INVALIDVARWIDTH;
    case WideVarErrorKind::VariableNotFound:
        return IDS_EXP_VARNOTFOUND;
    case WideVarErrorKind::VariableCallbackFailed:
        return IDS_EXP_INTERNALERR;
    case WideVarErrorKind::UnmatchedBracket:
        return IDS_EXP_UNMATCHEDBRACKET;
    case WideVarErrorKind::EnvironmentNotFound:
        return IDS_EXP_ENVVARNOTFOUND;
    case WideVarErrorKind::EnvironmentTooLarge:
        return IDS_EXP_ENVVARTOOLARGE;
    case WideVarErrorKind::UnexpectedCharacter:
        return IDS_EXP_UNEXPECTEDCHAR;
    case WideVarErrorKind::TrailingDollar:
        return IDS_EXP_TRAILINGDOLLAR;
    case WideVarErrorKind::OutputTooSmall:
        return IDS_EXP_SMALLBUFFER;
    default:
        return 0;
    }
}

std::wstring WideVarErrorText(const sally::unicode::WideVarError& error)
{
    const int resourceID = WideVarErrorResourceID(error.Kind);
    if (resourceID == 0)
        return std::wstring();
    if (error.Kind == sally::unicode::WideVarErrorKind::VariableNotFound ||
        error.Kind == sally::unicode::WideVarErrorKind::EnvironmentNotFound ||
        error.Kind == sally::unicode::WideVarErrorKind::EnvironmentTooLarge)
    {
        return FormatStrW(LoadStrW(resourceID), error.Argument.c_str());
    }
    return LoadStrW(resourceID);
}

void ReportWideVarError(HWND msgParent, const sally::unicode::WideVarError& error)
{
    const int resourceID = WideVarErrorResourceID(error.Kind);
    if (resourceID == 0)
        return;
    if (msgParent == NULL)
        TRACE_IW(WideVarErrorText(error).c_str());
    else
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), WideVarErrorText(error).c_str());
}

const CSalamanderVarStrEntry* FindVarEntryW(
    const CSalamanderVarStrEntry* variables, const wchar_t* name, int nameLength)
{
    if (variables == NULL)
        return NULL;
    for (const CSalamanderVarStrEntry* entry = variables;
         entry->Name != NULL; ++entry)
    {
        if (sally::unicode::SegmentEqualsNoCase(name, nameLength, entry->Name))
            return entry;
    }
    return NULL;
}

BOOL ValidateVarStringW(HWND msgParent, const wchar_t* varText, int& errorPos1,
                        int& errorPos2, const CSalamanderVarStrEntry* variables)
{
    if (varText == NULL || variables == NULL)
        return FALSE;

    const auto resolve = [&](const wchar_t* name, int nameLength, bool,
                             int, std::wstring&, int&) {
        return FindVarEntryW(variables, name, nameLength) != NULL
                   ? sally::unicode::WideVarResolveResult::Found
                   : sally::unicode::WideVarResolveResult::NotFound;
    };
    const auto ignoreEnvironmentError =
        [](const sally::unicode::WideVarError&) { return true; };

    sally::unicode::WideVarError error;
    if (sally::unicode::ExpandWideVarStringCore(
            varText, true, resolve, NULL, NULL, false, NULL, 0,
            (std::numeric_limits<std::size_t>::max)(), &error,
            ignoreEnvironmentError))
        return TRUE;

    ReportWideVarError(msgParent, error);
    errorPos1 = error.Position1;
    errorPos2 = error.Position2;
    return FALSE;
}

BOOL ValidateWideVarStringW(HWND msgParent, const wchar_t* varText, int& errorPos1,
                            int& errorPos2, const sally::unicode::WideVarEntry* variables)
{
    if (varText == NULL || variables == NULL)
        return FALSE;

    const auto resolve = [&](const wchar_t* name, int nameLength, bool,
                             int, std::wstring&, int&) {
        return sally::unicode::FindWideVarEntry(variables, name, nameLength) != NULL
                   ? sally::unicode::WideVarResolveResult::Found
                   : sally::unicode::WideVarResolveResult::NotFound;
    };
    const auto ignoreEnvironmentError =
        [](const sally::unicode::WideVarError&) { return true; };

    sally::unicode::WideVarError error;
    if (sally::unicode::ExpandWideVarStringCore(
            varText, true, resolve, NULL, NULL, false, NULL, 0,
            (std::numeric_limits<std::size_t>::max)(), &error,
            ignoreEnvironmentError))
        return TRUE;

    ReportWideVarError(msgParent, error);
    errorPos1 = error.Position1;
    errorPos2 = error.Position2;
    return FALSE;
}

// 2026-08-26: RESTORED - a prior tick's "confirmed dead" claim for this function
// was wrong. tests/sally/varstring_validate/gtest_varstring_validate.cpp directly calls
// ValidateVarString (via consts.h's declaration) as a real, dedicated behavioral test comparing
// narrow-adapter offset projection against ValidateVarStringW - that file's own header comment
// explains the test's exact purpose ("verifies offset projection rather than comparing two
// parser implementations"). The caller-search that declared this dead never covered the tests/
// directory, only src/ (core, plugins, .c files) - a gap distinct from, and found after, the
// earlier .c-file gap. See memory.md's 2026-08-26 entry.
BOOL ExpandVarString(HWND msgParent, const wchar_t* varText,
                     CSalamanderStringBuffer* buffer,
                     const CSalamanderVarStrEntry* variables,
                     void* param, BOOL ignoreEnvVarNotFoundOrTooLong,
                     CSalamanderTextRangeBuffer* varPlacements,
                     BOOL detectMaxVarWidths, int* maxVarWidths,
                     int maxVarWidthsCount)
{
    if (buffer == NULL ||
        !sally::plugin_abi::IsValidStringBuffer(*buffer) ||
        (varPlacements != NULL &&
         !sally::plugin_abi::IsValidTextRangeBuffer(*varPlacements)) ||
        varText == NULL || variables == NULL)
        return FALSE;

    const auto resolve = [&](const wchar_t* name, int nameLength, bool execute,
                             int requestedWidth, std::wstring& value,
                             int& measurementWidth) {
        const CSalamanderVarStrEntry* entry =
            FindVarEntryW(variables, name, nameLength);
        if (entry == NULL)
            return sally::unicode::WideVarResolveResult::NotFound;
        if (!execute)
            return sally::unicode::WideVarResolveResult::Found;
        if (entry->Execute == NULL)
            return sally::unicode::WideVarResolveResult::Failed;

        const wchar_t* rawValue = entry->Execute(msgParent, param);
        if (rawValue == NULL)
            return sally::unicode::WideVarResolveResult::Failed;

        value.assign(rawValue);
        if (value.size() >
            static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            return sally::unicode::WideVarResolveResult::Failed;
        measurementWidth = static_cast<int>(value.size());

        if (requestedWidth < 0)
            return sally::unicode::WideVarResolveResult::Failed;
        if (requestedWidth > 0)
        {
            const std::size_t width = static_cast<std::size_t>(requestedWidth);
            if (value.size() > width)
                value.resize(width);
            else if (value.size() < width)
                value.append(width - value.size(), L' ');
        }
        return sally::unicode::WideVarResolveResult::Found;
    };

    const auto handleEnvironmentError =
        [&](const sally::unicode::WideVarError& environmentError) {
            if (msgParent == NULL)
            {
                ReportWideVarError(NULL, environmentError);
                return true;
            }
            if (ignoreEnvVarNotFoundOrTooLong)
                return true;
            return gPrompter
                       ->ConfirmError(LoadStrW(IDS_ERRORTITLE),
                                      WideVarErrorText(environmentError).c_str())
                       .type != PromptResult::kCancel;
        };

    std::wstring expanded;
    std::vector<sally::unicode::WideTextRange> expandedRanges;
    sally::unicode::WideVarError error;
    if (!sally::unicode::ExpandWideVarStringCore(
            varText, false, resolve, &expanded,
            varPlacements != NULL ? &expandedRanges : NULL,
            detectMaxVarWidths != FALSE, maxVarWidths,
            maxVarWidthsCount, (std::numeric_limits<std::size_t>::max)(), &error,
            handleEnvironmentError))
    {
        if (error.Kind != sally::unicode::WideVarErrorKind::EnvironmentNotFound &&
            error.Kind != sally::unicode::WideVarErrorKind::EnvironmentTooLarge)
            ReportWideVarError(msgParent, error);
        return FALSE;
    }

    if (varPlacements == NULL)
        return sally::plugin_abi::WriteStringBuffer(*buffer, expanded) ? TRUE : FALSE;

    try
    {
        std::vector<CSalamanderTextRange> publishedRanges;
        publishedRanges.reserve(expandedRanges.size());
        for (const sally::unicode::WideTextRange& range : expandedRanges)
        {
            if (range.Offset > (std::numeric_limits<DWORD>::max)() ||
                range.Length > (std::numeric_limits<DWORD>::max)())
            {
                SetLastError(ERROR_FILENAME_EXCED_RANGE);
                return FALSE;
            }
            publishedRanges.push_back(
                {static_cast<DWORD>(range.Offset), static_cast<DWORD>(range.Length)});
        }
        return sally::plugin_abi::WriteTextAndRanges(
                   *buffer, *varPlacements, expanded, publishedRanges)
                   ? TRUE
                   : FALSE;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
}

static BOOL ExpandWideVarStringToOwnerW(HWND msgParent, const wchar_t* varText,
                                        std::wstring& expanded, std::size_t outputCapacity,
                                        const sally::unicode::WideVarEntry* variables,
                                        void* param, BOOL ignoreEnvVarNotFoundOrTooLong,
                                        std::vector<sally::unicode::WideTextRange>* varPlacements,
                                        BOOL detectMaxVarWidths, int* maxVarWidths,
                                        int maxVarWidthsCount)
{
    expanded.clear();
    if (varText == NULL || variables == NULL)
        return FALSE;

    const auto resolve = [&](const wchar_t* name, int nameLength, bool execute,
                             int requestedWidth, std::wstring& value,
                             int& measurementWidth) {
        const sally::unicode::WideVarEntry* entry =
            sally::unicode::FindWideVarEntry(variables, name, nameLength);
        if (entry == NULL)
            return sally::unicode::WideVarResolveResult::NotFound;
        if (!execute)
            return sally::unicode::WideVarResolveResult::Found;
        if (entry->Execute == NULL)
            return sally::unicode::WideVarResolveResult::Failed;

        value = entry->Execute(param);
        if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            return sally::unicode::WideVarResolveResult::Failed;
        measurementWidth = static_cast<int>(value.size());

        if (requestedWidth < 0)
            return sally::unicode::WideVarResolveResult::Failed;
        if (requestedWidth > 0)
        {
            const std::size_t width = static_cast<std::size_t>(requestedWidth);
            if (value.size() > width)
                value.resize(width);
            else if (value.size() < width)
                value.append(width - value.size(), L' ');
        }
        return sally::unicode::WideVarResolveResult::Found;
    };

    const auto handleEnvironmentError =
        [&](const sally::unicode::WideVarError& environmentError) {
            if (msgParent == NULL)
            {
                ReportWideVarError(NULL, environmentError);
                return true;
            }
            if (ignoreEnvVarNotFoundOrTooLong)
                return true;
            return gPrompter
                       ->ConfirmError(LoadStrW(IDS_ERRORTITLE),
                                      WideVarErrorText(environmentError).c_str())
                       .type != PromptResult::kCancel;
        };

    sally::unicode::WideVarError error;
    if (!sally::unicode::ExpandWideVarStringCore(
            varText, false, resolve, &expanded, varPlacements,
            detectMaxVarWidths != FALSE, maxVarWidths,
            maxVarWidthsCount, outputCapacity, &error,
            handleEnvironmentError))
    {
        if (error.Kind != sally::unicode::WideVarErrorKind::EnvironmentNotFound &&
            error.Kind != sally::unicode::WideVarErrorKind::EnvironmentTooLarge)
            ReportWideVarError(msgParent, error);
        return FALSE;
    }

    return TRUE;
}

BOOL ExpandWideVarStringW(HWND msgParent, const wchar_t* varText, wchar_t* buffer,
                          int bufferLen, const sally::unicode::WideVarEntry* variables,
                          void* param, BOOL ignoreEnvVarNotFoundOrTooLong,
                          std::vector<sally::unicode::WideTextRange>* varPlacements,
                          BOOL detectMaxVarWidths, int* maxVarWidths,
                          int maxVarWidthsCount)
{
    if (buffer == NULL || bufferLen <= 0)
        return FALSE;
    std::wstring expanded;
    if (!ExpandWideVarStringToOwnerW(msgParent, varText, expanded,
                                     static_cast<std::size_t>(bufferLen), variables, param,
                                     ignoreEnvVarNotFoundOrTooLong, varPlacements,
                                     detectMaxVarWidths, maxVarWidths,
                                     maxVarWidthsCount))
        return FALSE;
    std::wmemcpy(buffer, expanded.c_str(), expanded.size() + 1);
    return TRUE;
}

BOOL ExpandWideVarStringW(HWND msgParent, const wchar_t* varText, std::wstring& output,
                          const sally::unicode::WideVarEntry* variables, void* param,
                          BOOL ignoreEnvVarNotFoundOrTooLong)
{
    return ExpandWideVarStringToOwnerW(
        msgParent, varText, output, (std::numeric_limits<std::size_t>::max)(),
        variables, param, ignoreEnvVarNotFoundOrTooLong,
        NULL, FALSE, NULL, 0);
}
// ****************************************************************************

// Wide sibling. Sits on MyGetDiskFreeSpaceW, ported earlier in this
// task, so the SUBST + reparse-point walk underneath is already wide.
CQuadWord MyGetDiskFreeSpaceW(const wchar_t* path, CQuadWord* total)
{
    CQuadWord ret = CQuadWord(-1, -1);
    if (total != NULL)
        *total = CQuadWord(-1, -1);
    ULARGE_INTEGER availBytes, totalBytes, freeBytes;
    std::wstring ourPath(path);
    SalPathAddBackslashW(ourPath);
    if (GetDiskFreeSpaceExW(ourPath.c_str(), &availBytes, &totalBytes, &freeBytes))
    {
        ret.Value = (unsigned __int64)availBytes.QuadPart; // availBytes, not freeBytes - see the narrow twin
        if (total != NULL)
            total->Value = (unsigned __int64)totalBytes.QuadPart;
    }
    if (ret == CQuadWord(-1, -1))
    {
        DWORD a, b, c, d;
        if (MyGetDiskFreeSpaceW(path, &a, &b, &c, &d))
        {
            ret = CQuadWord(a, 0) * CQuadWord(b, 0) * CQuadWord(c, 0);
            if (total != NULL)
                *total = CQuadWord(a, 0) * CQuadWord(b, 0) * CQuadWord(d, 0);
        }
        else
            ret = CQuadWord(-1, -1); // error, do not display it
    }
    return ret;
}

// Wide sibling. Only the drive letter is consulted, so this one was
// never lossy — it is widened so that wide callers need no narrowing step to
// reach it, which is the actual defect its narrow form was causing upstream.
UINT GetDriveTypeForDriveLetterPathW(const wchar_t* path)
{
    wchar_t root[4] = L" :\\";
    root[0] = path[0];
    return GetDriveTypeW(root);
}

// 2026-08-25: the narrow IsUNCRootPath(char*) was deleted - confirmed-dead (zero
// callers anywhere; its only apparent uses in main_window_ui_basics.cpp are inside comments).
// IsUNCRootPathW (common/fsutil.cpp) is the real, widely-used implementation.

// Wide-native. The previous implementation narrowed 'resPath' to
// ANSI, called the narrow ResolveSubsts and widened the result back — so a SUBST
// under a Unicode path was destroyed twice over: once at the argument, once
// inside QueryDosDeviceA. Both CP_ACP round trips are gone; the loop is the
// shared one in common/SubstResolution.cpp and the query is the wide API.
// The live SUBST query — the one piece of ResolveSubstsW that touches the OS.
static sally::paths::SubstQueryW LiveSubstQueryW()
{
    return [](wchar_t driveLetter, std::wstring& outTarget) -> bool
    {
        return GetSubstInformationW(static_cast<BYTE>(driveLetter - L'A'), outTarget) != FALSE;
    };
}

BOOL ResolveSubstsW(std::wstring& resPath)
{
    auto result = sally::paths::ResolveSubstChainW(resPath, LiveSubstQueryW());
    if (result == sally::paths::SubstResolveResult::CycleGuard)
        TRACE_E("ResolveSubstsW(): infinite loop found!");
    return result == sally::paths::SubstResolveResult::Resolved;
}

// Wide sibling. Same walk as the narrow form; the differences are
// all consequences of the types:
//
//  - the narrow form calls GetReparsePointDestination with the SAME buffer as
//    source AND destination, which is the "return value aliases an argument"
//    hazard class. Here they are separate strings, which is both safer and
//    clearer about what is being read versus written.
//  - the narrow form uses 'resPath' itself as scratch while computing
//    RootOrCurReparsePoint and relies on overwriting it immediately afterwards.
//    That scratch is a named local here; the behaviour is identical because the
//    narrow code unconditionally overwrites resPath on the next line.
//  - the "too long path" arms disappear: they were fixed-buffer failures.
void ResolveLocalPathWithReparsePointsW(const wchar_t* path, CLocalPathResolutionW& res)
{
    res.ResPath.assign(path);
    ResolveSubstsW(res.ResPath);
    SalPathAddBackslashW(res.ResPath);

    if (res.ResPath.size() <= 3)
        return; // a root path has nothing to walk

    int allowedDepth = 50;
    BOOL firstRepPoint = TRUE;
    std::wstring repPointPath;
    while (GetCurrentLocalReparsePointW(res.ResPath.c_str(), repPointPath))
    {
        if (!res.RootOrCurReparsePointSet)
        {
            // Where the SUBST resolution of the ORIGINAL path's root lands. If
            // the reparse point sits deeper than that, the extra components have
            // to be re-appended after the substituted root.
            std::wstring substRoot = GetRootPath(path);
            ResolveSubstsW(substRoot);
            res.RootOrCurReparsePoint = GetRootPath(path);
            if (substRoot.size() < repPointPath.size())
            {
                if (_wcsnicmp(substRoot.c_str(), repPointPath.c_str(), substRoot.size()) == 0) // always true
                {
                    SalPathAppendW(res.RootOrCurReparsePoint, repPointPath.c_str() + substRoot.size());
                    SalPathAddBackslashW(res.RootOrCurReparsePoint);
                }
                else
                {
                    TRACE_E("ResolveLocalPathWithReparsePointsW(): unexpected prefix of resolved path");
                    res.RootOrCurReparsePoint = repPointPath;
                }
            }
            res.RootOrCurReparsePointSet = TRUE;
        }

        res.ResPath = repPointPath;
        SalPathAddBackslashW(res.ResPath);

        int repPointType = 0;
        std::wstring dst;
        BOOL getRepPointDestRes = GetReparsePointDestinationOwnedW(repPointPath.c_str(), &dst,
                                                                   &repPointType, TRUE);
        if (getRepPointDestRes && (repPointType == 2 /* JUNCTION POINT */ || repPointType == 3 /* SYMBOLIC LINK */))
        {
            if (firstRepPoint)
            {
                res.JunctionOrSymlinkTgt = dst;
                res.LinkType = repPointType;
            }
            ResolveSubstsW(dst);
        }
        firstRepPoint = FALSE;

        UINT drvType = getRepPointDestRes && dst.size() >= 2 && dst[1] == L':'
                           ? GetDriveTypeForDriveLetterPathW(dst.c_str())
                           : DRIVE_UNKNOWN;

        // symlink to a UNC or mapped network path (available since Vista): stop
        // here, because reparse points only make sense to chase on fixed disks —
        // a remote one would return paths meaningful on the OTHER machine.
        if (getRepPointDestRes && (IsUNCPathW(dst.c_str()) || drvType == DRIVE_REMOTE))
        {
            res.NetPath = dst;
            res.ResPath = GetRootPath(dst.c_str());
            break;
        }

        if (!getRepPointDestRes || dst.size() < 2 || dst[1] != L':')
        { // unknown reparse point or volume mount point; do not traverse it, and
          // the path must not be shortened or it may refer to another volume
            res.CutResPathIsPossible = FALSE;
            break;
        }

        if (allowedDepth-- == 0) // looks like an endless loop
        {
            res.ResPath.assign(path); // let the system handle it on its own
            ResolveSubstsW(res.ResPath);
            SalPathAddBackslashW(res.ResPath);
            res.RootOrCurReparsePointSet = FALSE;
            res.JunctionOrSymlinkTgt.clear();
            res.LinkType = 0 /* UNKNOWN */;
            break;
        }

        res.ResPath = dst;
        SalPathAddBackslashW(res.ResPath);
        if (drvType != DRIVE_FIXED)
            break; // reparse points only make sense to look for on fixed disks
    }
}

// Wide sibling — same shape as MyGetDriveTypeW above.
BOOL MyGetDiskFreeSpaceW(const wchar_t* path, LPDWORD lpSectorsPerCluster,
                         LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                         LPDWORD lpTotalNumberOfClusters)
{
    std::wstring resPath(path);
    ResolveSubstsW(resPath);
    std::wstring ourPath = GetRootPath(resPath.c_str());

    if (!IsUNCPathW(ourPath.c_str()) && GetDriveTypeW(ourPath.c_str()) == DRIVE_FIXED) // reparse points only make sense to look for on fixed disks
    {                                                                                  // gradually try shortening the path; on a mounted directory it can return the mounted disk parameters
        CLocalPathResolutionW res;
        ResolveLocalPathWithReparsePointsW(path, res);
        ourPath = res.ResPath;

        while (!GetDiskFreeSpaceW(ourPath.c_str(), lpSectorsPerCluster, lpBytesPerSector,
                                  lpNumberOfFreeClusters, lpTotalNumberOfClusters))
        {
            if (!res.CutResPathIsPossible || !CutDirectoryW(ourPath))
                return FALSE; // we must not cut it or even the root did not succeed
            SalPathAddBackslashW(ourPath);
        }
        return TRUE;
    }
    return GetDiskFreeSpaceW(ourPath.c_str(), lpSectorsPerCluster, lpBytesPerSector,
                             lpNumberOfFreeClusters, lpTotalNumberOfClusters);
}

// Wide sibling. Same shape as the two above, plus the
// rootOrCurReparsePoint / junctionOrSymlinkTgt / linkType outputs the reparse
// walk fills — which is why this one had to wait for the walk to go wide.
// Outputs are std::wstring* rather than caller buffers; pass NULL for what you
// do not want.
static BOOL QueryVolumeInformationOwnedW(const wchar_t* root,
                                         std::wstring* volumeName,
                                         LPDWORD volumeSerialNumber,
                                         LPDWORD maximumComponentLength,
                                         LPDWORD fileSystemFlags,
                                         std::wstring* fileSystemName)
{
    DWORD capacity = 64;
    for (;;)
    {
        try
        {
            std::vector<wchar_t> volumeStorage(
                volumeName != NULL ? capacity : 0, L'\0');
            std::vector<wchar_t> fileSystemStorage(
                fileSystemName != NULL ? capacity : 0, L'\0');
            DWORD stagedSerial = 0;
            DWORD stagedMaximumComponentLength = 0;
            DWORD stagedFlags = 0;
            if (GetVolumeInformationW(
                    root,
                    volumeStorage.empty() ? NULL : volumeStorage.data(),
                    volumeStorage.empty() ? 0 : capacity,
                    volumeSerialNumber != NULL ? &stagedSerial : NULL,
                    maximumComponentLength != NULL
                        ? &stagedMaximumComponentLength
                        : NULL,
                    fileSystemFlags != NULL ? &stagedFlags : NULL,
                    fileSystemStorage.empty() ? NULL
                                              : fileSystemStorage.data(),
                    fileSystemStorage.empty() ? 0 : capacity))
            {
                std::wstring stagedVolume;
                std::wstring stagedFileSystem;
                if (!volumeStorage.empty())
                {
                    const size_t length = wcsnlen_s(volumeStorage.data(), capacity);
                    if (length == capacity)
                    {
                        SetLastError(ERROR_INVALID_DATA);
                        return FALSE;
                    }
                    stagedVolume.assign(volumeStorage.data(), length);
                }
                if (!fileSystemStorage.empty())
                {
                    const size_t length = wcsnlen_s(fileSystemStorage.data(), capacity);
                    if (length == capacity)
                    {
                        SetLastError(ERROR_INVALID_DATA);
                        return FALSE;
                    }
                    stagedFileSystem.assign(fileSystemStorage.data(), length);
                }
                if (volumeName != NULL)
                    volumeName->swap(stagedVolume);
                if (fileSystemName != NULL)
                    fileSystemName->swap(stagedFileSystem);
                if (volumeSerialNumber != NULL)
                    *volumeSerialNumber = stagedSerial;
                if (maximumComponentLength != NULL)
                    *maximumComponentLength = stagedMaximumComponentLength;
                if (fileSystemFlags != NULL)
                    *fileSystemFlags = stagedFlags;
                return TRUE;
            }
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }

        const DWORD error = GetLastError();
        if ((error != ERROR_MORE_DATA && error != ERROR_INSUFFICIENT_BUFFER) ||
            (volumeName == NULL && fileSystemName == NULL) ||
            capacity > (std::numeric_limits<DWORD>::max)() / 2)
            return FALSE;
        capacity *= 2;
    }
}

BOOL MyGetVolumeInformationW(const wchar_t* path, std::wstring* rootOrCurReparsePoint,
                             std::wstring* junctionOrSymlinkTgt, int* linkType,
                             std::wstring* volumeName, LPDWORD lpVolumeSerialNumber,
                             LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
                             std::wstring* fileSystemName)
{
    BOOL ret = TRUE;
    if (volumeName != NULL)
        volumeName->clear();
    if (fileSystemName != NULL)
        fileSystemName->clear();
    if (junctionOrSymlinkTgt != NULL)
        junctionOrSymlinkTgt->clear();
    if (linkType != NULL)
        *linkType = 0;

    std::wstring resPath(path);
    ResolveSubstsW(resPath);
    std::wstring ourPath = GetRootPath(resPath.c_str());

    if (!IsUNCPathW(ourPath.c_str()) && GetDriveTypeW(ourPath.c_str()) == DRIVE_FIXED) // reparse points only make sense to look for on fixed disks
    {                                                                                  // gradually try shortening the path; on a mounted directory it can return the mounted disk parameters
        CLocalPathResolutionW res;
        ResolveLocalPathWithReparsePointsW(path, res);
        ourPath = res.ResPath;
        if (junctionOrSymlinkTgt != NULL)
            *junctionOrSymlinkTgt = res.JunctionOrSymlinkTgt;
        if (linkType != NULL)
            *linkType = res.LinkType;
        if (res.RootOrCurReparsePointSet && rootOrCurReparsePoint != NULL)
            *rootOrCurReparsePoint = res.RootOrCurReparsePoint;

        while (!QueryVolumeInformationOwnedW(
            ourPath.c_str(), volumeName, lpVolumeSerialNumber,
            lpMaximumComponentLength, lpFileSystemFlags, fileSystemName))
        {
            if (!res.CutResPathIsPossible || !CutDirectoryW(ourPath))
            {
                ret = FALSE; // we must not cut it or even the root did not succeed
                break;
            }
            SalPathAddBackslashW(ourPath);
        }

        if (!res.RootOrCurReparsePointSet && rootOrCurReparsePoint != NULL)
        { // ourPath is ResolveSubsts(path) or a shortened version of it
            std::wstring substRoot = GetRootPath(path);
            ResolveSubstsW(substRoot);
            *rootOrCurReparsePoint = GetRootPath(path);
            if (substRoot.size() < ourPath.size()) // the reported path is deeper than the substituted root, so re-append the remainder
            {
                if (_wcsnicmp(substRoot.c_str(), ourPath.c_str(), substRoot.size()) == 0) // always true
                {
                    SalPathAppendW(*rootOrCurReparsePoint, ourPath.c_str() + substRoot.size());
                    SalPathAddBackslashW(*rootOrCurReparsePoint);
                }
                else
                {
                    TRACE_E("MyGetVolumeInformationW(): unexpected prefix of resolved path");
                    *rootOrCurReparsePoint = ourPath;
                }
            }
            // remove the trailing backslash except for "c:\"
            if (rootOrCurReparsePoint->size() > 3 && rootOrCurReparsePoint->back() == L'\\')
                rootOrCurReparsePoint->pop_back();
        }
    }
    else
    {
        ret = QueryVolumeInformationOwnedW(
            ourPath.c_str(), volumeName, lpVolumeSerialNumber,
            lpMaximumComponentLength, lpFileSystemFlags, fileSystemName);
        if (rootOrCurReparsePoint != NULL)
        {
            *rootOrCurReparsePoint = GetRootPath(path);
            if (rootOrCurReparsePoint->size() > 3 && rootOrCurReparsePoint->back() == L'\\')
                rootOrCurReparsePoint->pop_back();
        }
    }
    return ret;
}

// Header used to interpret the opaque Microsoft-tag reparse blob returned by IFileSystem.

struct TMN_REPARSE_DATA_BUFFER
{
    DWORD ReparseTag;
    WORD ReparseDataLength;
    WORD Reserved;
    WORD SubstituteNameOffset;
    WORD SubstituteNameLength;
    WORD PrintNameOffset;
    WORD PrintNameLength;
    WCHAR PathBuffer[1];
};

#define IO_REPARSE_TAG_SYMLINK (0xA000000CL)

// Wide is the real implementation; the ANSI entry point below is a thin wrapper.
//
// The body was already wide internally - it did AnsiToWide() on its own argument before
// every Win32 call - so the only lossy step was the caller's narrow path coming in. That
// mattered: a junction whose name the code page cannot spell arrived as '?', the
// attribute query below failed, and the function reported "not a reparse point". The
// delete confirmation then called a junction a directory (audit A25).
BOOL GetReparsePointDestinationOwnedW(const wchar_t* repPointDir, std::wstring* repPointDst,
                                      int* repPointType, BOOL makeRelPathAbs)
{
    if (repPointType != NULL)
        *repPointType = 0 /* UNKNOWN */;

    // if the path ends with a space/dot we must append '\\', otherwise GetFileAttributes
    // and CreateFile will trim spaces/dots and operate on a different path
    const std::wstring repPointDirCrFile = MakeCopyWithBackslashIfNeededW(repPointDir);

    std::vector<BYTE> reparseData;
    const FileResult reparseResult = gFileSystem->GetReparseData(repPointDirCrFile.c_str(), reparseData);
    if (!reparseResult.success || reparseData.size() < offsetof(TMN_REPARSE_DATA_BUFFER, PathBuffer))
    {
        TRACE_EW(L"GetReparsePointDestinationW(): Unable to get data of reparse point: " << repPointDir);
        return FALSE;
    }
    TMN_REPARSE_DATA_BUFFER* juncData = (TMN_REPARSE_DATA_BUFFER*)reparseData.data();
    if (
        juncData->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT &&
            juncData->ReparseTag != IO_REPARSE_TAG_SYMLINK)
    {
        TRACE_EW(L"GetReparsePointDestinationW(): Unable to get data of reparse point: " << repPointDir);
        return FALSE;
    }

    std::wstring substName;
    std::wstring printName;
    const auto copyReparseName = [&](std::wstring& destination, size_t pathOffset,
                                     WORD nameOffset, WORD nameLength) -> bool
    {
        const size_t begin = pathOffset + nameOffset;
        if ((nameOffset & 1) != 0 || (nameLength & 1) != 0 ||
            begin > reparseData.size() || nameLength > reparseData.size() - begin)
            return false;
        const size_t chars = nameLength / sizeof(WCHAR);
        destination.resize(chars);
        if (chars != 0)
            memcpy(destination.data(), reparseData.data() + begin, chars * sizeof(WCHAR));
        return true;
    };
    int myType = 0;
    if (juncData->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT)
    {
        const size_t pathOffset = offsetof(TMN_REPARSE_DATA_BUFFER, PathBuffer);
        if (!copyReparseName(substName, pathOffset, juncData->SubstituteNameOffset, juncData->SubstituteNameLength) ||
            !copyReparseName(printName, pathOffset, juncData->PrintNameOffset, juncData->PrintNameLength))
            return FALSE;
        myType = substName.size() >= 10 && _wcsnicmp(substName.c_str(), L"\\??\\Volume", 10) == 0 ? 1 /* MOUNT POINT */ : 2 /* JUNCTION POINT */;
    }
    else
    {
        if (juncData->ReparseTag == IO_REPARSE_TAG_SYMLINK)
        {
            const size_t pathOffset = offsetof(TMN_REPARSE_DATA_BUFFER, PathBuffer) + sizeof(ULONG);
            if (!copyReparseName(substName, pathOffset, juncData->SubstituteNameOffset, juncData->SubstituteNameLength) ||
                !copyReparseName(printName, pathOffset, juncData->PrintNameOffset, juncData->PrintNameLength))
                return FALSE;
            myType = 3 /* SYMBOLIC LINK */;
        }
        else
        {
            TRACE_EW(L"GetReparsePointDestinationW(): Unknown type of reparse point: " << repPointDir);
            return FALSE;
        }
    }
    if (repPointType != NULL)
        *repPointType = myType;
    if (repPointDst != NULL)
    {
        std::wstring destination = printName;
        if (destination.empty())
        {
            destination = substName;
            if (destination.size() >= 4 && _wcsnicmp(destination.c_str(), L"\\??\\", 4) == 0)
                destination.erase(0, 4); // skip "\\??\\" in substName
        }
        if (myType == 2 /* JUNCTION POINT */ &&
            (destination.size() < 3 || destination[1] != L':' || destination[2] != L'\\'))
        {
            TRACE_EW(L"GetReparsePointDestinationW(): Unexpected format of junction point (relative path): " << repPointDir);
            return FALSE;
        }
        if (makeRelPathAbs && myType == 3 /* SYMBOLIC LINK */ &&
            !(destination.size() >= 3 && destination[1] == L':' && destination[2] == L'\\' ||
              destination.size() >= 2 && destination[0] == L'\\' && destination[1] == L'\\'))
        { // the symlink is relative; try converting it to an absolute path
            if (repPointDir[0] == 0 || repPointDir[1] != ':')
            {
                TRACE_EW(L"GetReparsePointDestinationW(): Unexpected format of symbolic link name (it is not a local path): " << repPointDir);
                return FALSE;
            }
            // The drive letter no longer needs the "'a-zA-Z' convert 1:1" hack the ANSI
            // version relied on, and the tail no longer needs a conversion at all - it was
            // only ever converting the function's own argument back to the width it had
            // already been widened from.
            if (!destination.empty() && destination[0] == L'\\')
                destination = std::wstring(repPointDir, 2) + destination;
            else
            {
                std::wstring absolutePath = repPointDir;
                SalPathRemoveBackslashW(absolutePath);
                const size_t lastComp = absolutePath.find_last_of(L'\\');
                if (lastComp == std::wstring::npos)
                {
                    TRACE_EW(L"GetReparsePointDestinationW(): Unexpected format of symbolic link name (it does not contain backslash): " << repPointDir);
                    return FALSE;
                }
                absolutePath.resize(lastComp + 1);
                absolutePath += destination;
                destination = std::move(absolutePath);
            }
        }
        if (myType == 3 /* SYMBOLIC LINK */ && destination.size() >= 3 &&
            destination[1] == L':' && destination[2] == L'\\')
        {
            SalRemovePointsFromPath(destination.data() + 3);
            destination.resize(wcslen(destination.c_str()));
        }
        *repPointDst = std::move(destination);
    }
    return TRUE;
}
// Walk components by index in an owned string so no mutable-buffer pointer can outlive
// a reallocation.
BOOL GetCurrentLocalReparsePointW(const wchar_t* path, std::wstring& currentReparsePoint)
{
    BOOL ret = TRUE;

    currentReparsePoint.assign(path);
    SalPathAddBackslashW(currentReparsePoint);

    // Walk reparse points from the start of the path to its end, or to the first
    // symlink leading to a network path.
    size_t lastRepPointEnd = std::wstring::npos;
    const wchar_t* rootEnd = SkipRootW(currentReparsePoint.c_str());
    size_t pos = static_cast<size_t>(rootEnd - currentReparsePoint.c_str()) + 1; // always ends with a backslash
    while (pos <= currentReparsePoint.size())
    {
        size_t sep = currentReparsePoint.find(L'\\', pos);
        if (sep == std::wstring::npos)
            break; // that was the last component of the path, stop here
        pos = sep + 1;

        // Probe the path truncated at this component.
        const std::wstring probe = currentReparsePoint.substr(0, pos);
        std::wstring repPointPath;
        if (GetReparsePointDestinationOwnedW(probe.c_str(), &repPointPath, NULL, TRUE))
        {
            lastRepPointEnd = pos;
            if (IsUNCPathW(repPointPath.c_str()) ||
                repPointPath.size() >= 2 && repPointPath[1] == L':' &&
                    GetDriveTypeForDriveLetterPathW(repPointPath.c_str()) == DRIVE_REMOTE) // symlink to a UNC or mapped network path (available since Vista)
            {
                break;
            }
        }
    }

    if (lastRepPointEnd != std::wstring::npos)
        currentReparsePoint.resize(lastRepPointEnd);
    else
        ret = FALSE; // no reparse point found

    if (!ret)
        currentReparsePoint = GetRootPath(path);
    return ret;
}

// Wide sibling. One of three volume queries that share the same
// shape: resolve SUBSTs, take the root, and on a fixed disk walk the reparse
// points before asking Windows — shortening the path until the API answers.
// All of it now runs on the wide family ported in this task, so the narrow
// round trip is gone rather than relocated.
UINT MyGetDriveTypeW(const wchar_t* path)
{
    std::wstring resPath(path);
    ResolveSubstsW(resPath);
    std::wstring ourPath = GetRootPath(resPath.c_str());

    UINT ret = DRIVE_UNKNOWN;
    if (!IsUNCPathW(ourPath.c_str()))
    {
        UINT drvType = GetDriveTypeW(ourPath.c_str());
        if (drvType == DRIVE_FIXED) // reparse points only make sense to look for on fixed disks
        {
            CLocalPathResolutionW res;
            ResolveLocalPathWithReparsePointsW(path, res);
            ourPath = res.ResPath;

            // NOTE: differs from MyGetVolumeInformation because GetDriveType returns
            // success for any path (not just root + mounted volume)
            while ((ret = GetDriveTypeW(ourPath.c_str())) == DRIVE_UNKNOWN)
            {
                if (!res.CutResPathIsPossible || !CutDirectoryW(ourPath))
                    break; // we must not cut it or even the root did not succeed
                SalPathAddBackslashW(ourPath);
            }
        }
        else
            ret = drvType;
    }
    else
        ret = GetDriveTypeW(ourPath.c_str());
    return ret;
}

//****************************************************************************
//
// GetSubstInformation
//

BOOL MyQueryDosDeviceW(BYTE driveNum, std::wstring& target)
{
    const wchar_t deviceName[3] = {static_cast<wchar_t>(driveNum + L'A'), L':', 0};
    DWORD capacity = 256;
    for (;;)
    {
        std::vector<wchar_t> buffer(capacity, L'\0');
        const DWORD length = QueryDosDeviceW(deviceName, buffer.data(), capacity);
        if (length != 0)
        {
            target.assign(buffer.data(), wcslen(buffer.data()));
            return TRUE;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
            capacity > static_cast<DWORD>((std::numeric_limits<int>::max)() / 2))
            return FALSE;
        capacity *= 2;
    }
}

BOOL GetSubstInformationW(BYTE driveNum, std::wstring& path)
{
    std::wstring target;
    if (!MyQueryDosDeviceW(driveNum, target))
        return FALSE;

    std::wstring resolved;
    if (sally::paths::ParseDosDeviceTargetW(target.c_str(), resolved) ==
        sally::paths::DosDeviceKind::Unresolvable)
        return FALSE;
    path.swap(resolved);
    return TRUE;
}

//****************************************************************************
//
// GetMessagePos
//

void GetMessagePos(POINT& p)
{
    DWORD w = GetMessagePos();
    p.x = ((int)(short)LOWORD(w));
    p.y = ((int)(short)HIWORD(w));
}

// 2026-08-25: the narrow AlterFileName(char*, ...) was deleted - confirmed-dead
// (zero callers; the legacy v107 ABI shim forwards to WideGeneral.AlterFileName, never to this
// free function). AlterFileNameW (common/PathDisplayUtils.cpp, doc-commented in its own header)
// is the sole surviving implementation.

// ****************************************************************************

void RestoreApp(HWND mainWnd, HWND dlgWnd)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    EnableWindow(mainWnd, FALSE); // because of the task list (Alt+TAB)
    if (Configuration.StatusArea)
        ShowWindow(mainWnd, SW_SHOW);
    ShowWindow(mainWnd, SW_RESTORE); // activate minimized wnd
    SetActiveWindow(dlgWnd);
    PostMessage(mainWnd, WM_NCACTIVATE, FALSE, 0);
}

// ****************************************************************************

void MinimizeApp(HWND mainWnd)
{
    ShowWindow(mainWnd, SW_MINIMIZE);
    if (Configuration.StatusArea)
        ShowWindow(mainWnd, SW_HIDE);
    EnableWindow(mainWnd, TRUE); // because of the task list (Alt+TAB)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
}

// ****************************************************************************
BOOL CheckOnlyOneInstance(const sally::cmdline::CommandLineRequest* request)
{
    // :-) a small gift for the transition to the text config :-))))
    // load even if ForceOnlyOneInstance == TRUE
    LoadSaveToRegistryMutex.Enter();
    HKEY salamander;
    if (SALAMANDER_ROOT_REG != NULL &&
        OpenKeyW(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander))
    {
        HKEY actKey;
        if (OpenKeyW(salamander, SALAMANDER_CONFIG_REG, actKey))
        {
            GetValueW(actKey, CONFIG_ONLYONEINSTANCE_REG, REG_DWORD,
                     &Configuration.OnlyOneInstance, sizeof(DWORD));
            CloseKey(actKey);
        }
        CloseKey(salamander);
    }
    LoadSaveToRegistryMutex.Leave();

    if (Configuration.ForceOnlyOneInstance || Configuration.OnlyOneInstance)
    {
        return TaskList.ActivateRunningInstance(request);
    }
    return FALSE;
}
/*
BOOL CheckOnlyOneInstance(const char *leftPath, const char *rightPath, const char *activePath, BYTE activatePanel)
{
  // :-) a small gift for the transition to the text config :-))))
  // load even if ForceOnlyOneInstance == TRUE
  LoadSaveToRegistryMutex.Enter();
  HKEY salamander;
  if (SALAMANDER_ROOT_REG != NULL &&
      OpenKey(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander))
  {
    HKEY actKey;
    if (OpenKey(salamander, SALAMANDER_CONFIG_REG, actKey))
    {
      GetValue(actKey, CONFIG_ONLYONEINSTANCE_REG, REG_DWORD,
               &Configuration.OnlyOneInstance, sizeof(DWORD));
      CloseKey(actKey);
    }
    CloseKey(salamander);
  }
  LoadSaveToRegistryMutex.Leave();

  if (Configuration.ForceOnlyOneInstance || Configuration.OnlyOneInstance)
  {
    HWND wnd;
    int c = 100;   // wait up to five seconds to find the predecessor (it may not have opened the main window yet)
    while (c--)
    {
      wnd = FindWindowW(CMAINWINDOW_CLASSNAME, NULL);
      if (wnd == NULL && !FirstLocalInstance_252b1_or_later) 
        Sleep(50);
      else 
        break; // professional optimization since 2.52 -- why loop 100 times ;-)
    }
    if (wnd != NULL)  // we have a predecessor
    {
      // allow the use of SetForegroundWindow, otherwise Salamander will not be able to bring itself to the front
      DWORD otherSalPID;
      GetWindowThreadProcessId(wnd, &otherSalPID);
      AllowSetForegroundWindow(otherSalPID);

      PostMessage(wnd, WM_USER_SHOWWINDOW, 0, 0);

      // allocate shared space in pagefile.sys
      HANDLE fm = HANDLES(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, // FIXME_X64 are we passing x86/x64 incompatible data?
                                            sizeof(CSetPathsParams), NULL));
      if (fm != NULL)
      {
        int c = -1;
        CSetPathsParams *params = (CSetPathsParams*)HANDLES(MapViewOfFile(fm, FILE_MAP_WRITE, 0, 0, 0)); // FIXME_X64 are we passing x86/x64 incompatible data?
        if (params != NULL)
        {
          ZeroMemory(params, sizeof(CSetPathsParams));
          lstrcpyn(params->LeftPath, leftPath, MAX_PATH);
          lstrcpyn(params->RightPath, rightPath, MAX_PATH - 1); // Salamander older than 2.52 could crash on a path of length MAX_PATH - 1

          // unfortunately when receiving mapped memory we cannot determine its size to the byte (only with page-size granularity)
          // so we use a "trick" -- append a signature after the structure; if it is there, it is very likely our data
          // and the receiver can keep reading
          params->MagicSignature1 = 0x07f2ab13;
          params->MagicSignature2 = 0x471e0901;
          params->StructVersion = 1;
          lstrcpyn(params->ActivePath, activePath, MAX_PATH);
          params->ActivatePanel = activatePanel;

          // let the old process read the memory and change directories
          c = 51;  // give it 5 seconds
          while (--c)
          {
            PostMessage(wnd, WM_USER_SETPATHS, (WPARAM)GetCurrentProcessId(), (LPARAM)fm);
            Sleep(100);
            if (params->Received)
            {
              c = -1;
              break;
            }
          }

          // then we wrap it up
          HANDLES(UnmapViewOfFile(params));
        }
        HANDLES(CloseHandle(fm));
        if (c == 0)
        {
          TRACE_I("Target process is not responding.");
          return SalMessageBoxW(NULL, LoadStrOwned(IDS_SALAMANDBUSY).c_str(),
                                LoadStrOwned(IDS_QUESTION).c_str(),
                                MB_YESNOCANCEL | MB_ICONQUESTION) != IDYES;
        }
      }
      return TRUE;
    }
  }
  return FALSE;
}
*/
// ****************************************************************************

void DrawSplitLine(HWND HWindow, int newDragSplitX, int oldDragSplitX, RECT client)
{
    if (DragFullWindows)
        return;

    RECT wr;
    POINT p;
    p.x = 0;
    p.y = 0;
    ClientToScreen(HWindow, &p);
    GetWindowRect(HWindow, &wr);
    int xOffset = wr.left - p.x;
    int yOffset = wr.top - p.y;

    HDC dc = HANDLES(GetWindowDC(HWindow));
    SetViewportOrgEx(dc, -xOffset, -yOffset, &p);

    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, HDitherBrush);
    HPEN oldPen = (HPEN)SelectObject(dc, HANDLES(GetStockObject(NULL_PEN)));
    int oldROP = SetROP2(dc, R2_XORPEN); // we will AND

    int splitThick = MainWindow->GetSplitBarWidth() + 1;
    client.bottom++;
    int l0 = client.left + newDragSplitX;
    int r0 = client.left + newDragSplitX + splitThick;
    if (newDragSplitX == -1)
        r0 = l0 = -1;
    if (oldDragSplitX != -1) // compute the stripe for invalidation
    {
        int l1 = client.left + oldDragSplitX;
        int r1 = client.left + oldDragSplitX + splitThick;
        if (l1 >= l0 && l1 < r0)
        {
            int tmp = l1;
            l1 = r0 - 1;
            r0 = tmp + 1;
        }
        if (r1 > l0 && r1 <= r0)
        {
            int tmp = r1;
            r1 = l0 + 1;
            l0 = tmp - 1;
        }
        if (l1 <= r1)
        {
            Rectangle(dc, l1, client.top, r1, client.bottom);
        }
    }
    if (l0 != -1 || r0 != -1)
        Rectangle(dc, l0, client.top, r0, client.bottom);

    SetROP2(dc, oldROP);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    HANDLES(ReleaseDC(HWindow, dc));
}

// ****************************************************************************

// ****************************************************************************
// Wide facades. See consts.h for why the worker-thread marshaling
// stays: it keeps the UI responsive during a slow-hive read and guards against
// re-entrant deadlock. These widen the NAME, nothing else.
//
// gRegistry is the wide implementation; where the worker thread is usable we go
// through it exactly as the ANSI path does, so the two behave identically apart
// from the name's encoding.

BOOL CreateKeyW(HKEY hKey, const wchar_t* name, HKEY& createdKey)
{
    return gRegistry->CreateKey(hKey, name, createdKey).success;
}

BOOL OpenKeyW(HKEY hKey, const wchar_t* name, HKEY& openedKey)
{
    return gRegistry->OpenKeyReadWrite(hKey, name, openedKey).success;
}

BOOL DeleteKeyW(HKEY hKey, const wchar_t* name)
{
    return gRegistry->DeleteKey(hKey, name).success;
}

BOOL DeleteValueW(HKEY hKey, const wchar_t* name)
{
    return gRegistry->DeleteValue(hKey, name).success;
}

BOOL SetValueW(HKEY hKey, const wchar_t* name, DWORD type, const void* data, DWORD dataSize)
{
    // IRegistry is typed rather than raw on the write side, so dispatch on the
    // caller's REG_* type. REG_SZ takes the wide string as-is: that is the whole
    // point of this facade, and it is what makes the value readable by any other
    // tool (unlike the legacy history shape - see LoadHistoryW's contract note).
    //
    // REG_SZ's length ignores 'dataSize' entirely and derives it from
    // 'data' itself, so this branch has always required 'data' to genuinely be a
    // wchar_t* string. A caller that instead passes a narrow char* (found live in
    // several plugins' registry-persisted fields; see 24-intree-plugins-wide.md's
    // registry-corruption sub-campaign) gets its buffer scanned for a two-byte-
    // aligned zero code unit, which can run past the buffer's real end looking for
    // one - an out-of-bounds read, not just wrong data. ComputeRegSzSafeLength
    // bounds that scan and refuses (rather than reads further out of bounds or
    // writes a bogus-length value) once no terminator turns up within the cap.
    switch (type)
    {
    case REG_SZ:
    {
        const wchar_t* wideData = (const wchar_t*)data;
        size_t len;
        if (!ComputeRegSzSafeLength(wideData, len))
        {
            TRACE_EW(L"SetValueW: REG_SZ value '" << name << L"' has no wide NUL terminator within "
                     L"the safe scan bound - refusing to write (narrow data passed where a wide "
                     L"string was required?)");
            return FALSE;
        }
        return gRegistry->SetString(hKey, name, wideData).success;
    }
    case REG_DWORD:
        return gRegistry->SetDWord(hKey, name, *(const DWORD*)data).success;
    case REG_QWORD:
        return gRegistry->SetQWord(hKey, name, *(const uint64_t*)data).success;
    default:
        // Everything else - REG_BINARY, but also REG_EXPAND_SZ and REG_MULTI_SZ, which v107
        // plugins do write - is bytes. Write them under the TYPE THE CALLER ASKED FOR: SetBinary
        // hard-codes REG_BINARY, and the read side refuses a type mismatch, so stamping the wrong
        // type here makes the value unreadable forever after. The worker path (regwork.cpp) has
        // always preserved the type; this facade must too.
        return gRegistry->WriteValue(hKey, name, (RegValueType)type, data, dataSize).success;
    }
}

BOOL GetValueW(HKEY hKey, const wchar_t* name, DWORD type, void* buffer, DWORD bufferSize)
{
    RegValueType actualType = RegValueType::None;
    std::vector<uint8_t> data;
    if (!gRegistry->GetValue(hKey, name, actualType, data).success)
        return FALSE;
    // The ANSI facade refuses a type mismatch rather than handing back bytes the
    // caller will misread; keep that contract exactly.
    if ((DWORD)actualType != type)
        return FALSE;
    if (data.size() > bufferSize)
        return FALSE;
    if (!data.empty())
        memcpy(buffer, data.data(), data.size());
    return TRUE;
}

BOOL GetStringValueW(HKEY hKey, const wchar_t* name, std::wstring& value)
{
    std::wstring loaded;
    if (!gRegistry->GetString(hKey, name, loaded).success)
        return FALSE;
    value = std::move(loaded);
    return TRUE;
}

BOOL GetSizeW(HKEY hKey, const wchar_t* name, DWORD type, DWORD& bufferSize)
{
    RegValueType actualType = RegValueType::None;
    std::vector<uint8_t> data;
    if (!gRegistry->GetValue(hKey, name, actualType, data).success)
        return FALSE;
    if ((DWORD)actualType != type)
        return FALSE;
    bufferSize = (DWORD)data.size();
    return TRUE;
}

void CloseKey(HKEY hKey)
{
    RegistryWorkerThread.CloseKey(hKey);
}

// ****************************************************************************

BOOL GetValue2(HKEY hKey, const wchar_t* name, DWORD type1, DWORD type2, DWORD* returnedType, void* buffer, DWORD bufferSize)
{
    return RegistryWorkerThread.GetValue2(hKey, name, type1, type2, returnedType, buffer, bufferSize);
}

// Configuration operations route through RegistryWorkerThread rather than the
// direct gRegistry/*W facades. Those two
// paths are not interchangeable: RegistryWorkerThread pumps the message loop
// (MsgWaitForMultipleObjects) so a slow/network-backed hive cannot freeze the UI
// during config load/save and preserves the established error reporting.

BOOL CreateKey(HKEY hKey, const wchar_t* name, HKEY& createdKey)
{
    return RegistryWorkerThread.CreateKey(hKey, name, createdKey);
}

BOOL OpenKey(HKEY hKey, const wchar_t* name, HKEY& openedKey)
{
    return RegistryWorkerThread.OpenKey(hKey, name, openedKey);
}

BOOL DeleteKey(HKEY hKey, const wchar_t* name)
{
    return RegistryWorkerThread.DeleteKey(hKey, name);
}

BOOL DeleteValue(HKEY hKey, const wchar_t* name)
{
    return RegistryWorkerThread.DeleteValue(hKey, name);
}

BOOL SetValue(HKEY hKey, const wchar_t* name, DWORD type, const void* data, DWORD dataSize)
{
    return RegistryWorkerThread.SetValue(hKey, name, type, data, dataSize);
}

BOOL GetValue(HKEY hKey, const wchar_t* name, DWORD type, void* buffer, DWORD bufferSize)
{
    return RegistryWorkerThread.GetValue(hKey, name, type, buffer, bufferSize);
}

BOOL GetSize(HKEY hKey, const wchar_t* name, DWORD type, DWORD& bufferSize)
{
    return RegistryWorkerThread.GetSize(hKey, name, type, bufferSize);
}

// ****************************************************************************

BOOL ClearKey(HKEY key)
{
    return RegistryWorkerThread.ClearKey(key);
}

// ****************************************************************************

// The pre-2.53 REG_SZ spelling of a colour - "r,g,b" for LoadRGB, "r,g,b,f" for LoadRGBF - parsed
// out of the value as it comes back from the registry.
//
// THE PAYLOAD IS UTF-16, and always has been. The old build wrote it with the ANSI value API, and
// RegSetValueExA transcodes its buffer CP_ACP -> UTF-16 before storing; reading it back through
// RegQueryValueExA reversed that transcode, which is why a narrow scan was correct THEN. The wide
// facade hands the stored bytes over unconverted, so the same scan finds a NUL one byte into the
// first digit and every legacy colour loads as near-black. Same family as the REG_SZ corruption
// reg_sz_narrow_bridge.h fixes on the plugin side.
//
// 'maxChars' bounds the scan: GetValue2 copies whatever byte count the value carries, and a
// truncated or unterminated one must not send the parser past the buffer.
static void ParseLegacyRegSzColor(const wchar_t* text, size_t maxChars, BYTE* components, int count)
{
    size_t pos = 0;
    for (int i = 0; i < count; i++)
    {
        const size_t start = pos;
        while (pos < maxChars && text[pos] != L'\0' && text[pos] != L',')
            pos++;
        const std::wstring field(text + start, text + pos);
        components[i] = (BYTE)(DWORD)_wtoi(field.c_str());
        if (pos >= maxChars || text[pos] == L'\0')
            break;
        pos++; // step over the separator
    }
}

// The NAME is wide and so is the DATA - see ParseLegacyRegSzColor. LoadRGB still accepts the
// pre-2.53 REG_SZ "r,g,b" spelling as well as the binary REG_DWORD one, and that is an on-disk
// format existing users already have.
BOOL LoadRGB(HKEY hKey, const wchar_t* name, COLORREF& color)
{
    // alignas: the REG_DWORD alternative is read out of this same buffer.
    alignas(DWORD) wchar_t buf[50];
    DWORD returnedType;
    // for backward compatibility (up to reg:\HKEY_CURRENT_USER\Software\Altap\Altap Salamander 2.53 beta 1 (DB 33) inclusive) we can load both
    // the representation as a string and the more efficient binary one
    if (GetValue2(hKey, name, REG_SZ, REG_DWORD, &returnedType, buf, sizeof(buf)))
    {
        if (returnedType == REG_SZ)
        {
            BYTE c[3] = {0, 0, 0};
            ParseLegacyRegSzColor(buf, _countof(buf), c, 3);
            color = RGB(c[0], c[1], c[2]);
        }
        else
        {
            color = (*(DWORD*)buf) & 0x00ffffff;
        }
        return TRUE;
    }
    return FALSE;
}

// ****************************************************************************

BOOL SaveRGB(HKEY hKey, const wchar_t* name, COLORREF color)
{
    //  char buf[50];
    //  sprintf(buf, "%d, %d, %d", GetRValue(color), GetGValue(color), GetBValue(color));
    //  return SetValue(hKey, name, REG_SZ, buf, strlen(buf) + 1);
    DWORD clr = color & 0x00ffffff; // discard the "alpha" channel
    return SetValue(hKey, name, REG_DWORD, &clr, 4);
}

// ****************************************************************************

// ****************************************************************************

BOOL LoadRGBF(HKEY hKey, const wchar_t* name, SALCOLOR& color)
{
    alignas(DWORD) wchar_t buf[50];
    DWORD returnedType;
    // for backward compatibility (up to reg:\HKEY_CURRENT_USER\Software\Altap\Altap Salamander 2.53 beta 1 (DB 33) inclusive) we can load both
    // the representation as a string and the more efficient binary one
    if (GetValue2(hKey, name, REG_SZ, REG_DWORD, &returnedType, buf, sizeof(buf)))
    {
        if (returnedType == REG_SZ)
        {
            BYTE c[4] = {0, 0, 0, 0};
            ParseLegacyRegSzColor(buf, _countof(buf), c, 4);
            color = RGBF(c[0], c[1], c[2], c[3]);
        }
        else
        {
            color = *(DWORD*)buf;
        }
        return TRUE;
    }
    return FALSE;
}

// ****************************************************************************

BOOL SaveRGBF(HKEY hKey, const wchar_t* name, SALCOLOR color)
{
    //  char buf[50];
    //  sprintf(buf, "%d, %d, %d, %d", GetRValue(color), GetGValue(color), GetBValue(color), GetFValue(color));
    //  return SetValue(hKey, name, REG_SZ, buf, strlen(buf) + 1);
    return SetValue(hKey, name, REG_DWORD, &color, 4);
}

// ****************************************************************************

static bool ReadConfigurationValueBytes(HKEY key, const wchar_t* name, DWORD type,
                                        std::vector<BYTE>& bytes) noexcept
{
    DWORD byteCount = 0;
    if (!GetSize(key, name, type, byteCount))
        return false;
    try
    {
        std::vector<BYTE> candidate(byteCount);
        if (byteCount != 0 && !GetValue(key, name, type, candidate.data(), byteCount))
            return false;
        bytes.swap(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// Reads a REG_SZ configuration value and hands back its text.
//
// BOTH shapes this file reads are UTF-16 on disk. The honest "<name> W" values are written wide by
// SaveLogFont; the historical unsuffixed ones were written narrow, but through the ANSI value API,
// and RegSetValueExA transcodes CP_ACP -> UTF-16 before storing. So there is nothing left to
// decode on the way back in - a second ACP decode over these bytes stops at the NUL that follows
// the first character's low byte, which is how a whole font spec used to collapse to one letter.
//
// (The one genuinely byte-shaped legacy value is the wide history written by pre-2.53 SaveHistoryW,
// which pushed raw UTF-16 through the same ANSI API. That shape is NOT read here; it has its own
// named importer with its own reasoning - see common/HistoryValueIo.h.)
static bool ReadConfigurationString(HKEY key, const wchar_t* name, std::wstring& text) noexcept
{
    std::vector<BYTE> bytes;
    if (!ReadConfigurationValueBytes(key, name, REG_SZ, bytes) ||
        bytes.size() < sizeof(wchar_t) || bytes.size() % sizeof(wchar_t) != 0)
    {
        return false;
    }
    try
    {
        std::wstring candidate(bytes.size() / sizeof(wchar_t), L'\0');
        memcpy(candidate.data(), bytes.data(), bytes.size());
        const size_t terminator = candidate.find(L'\0');
        if (terminator == std::wstring::npos)
            return false; // unterminated value - refuse rather than invent an end
        candidate.resize(terminator);
        text.swap(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// Current font persistence is UTF-16 under "<legacy name> W". The unsuffixed value remains a
// read-only import of the historical payload; it no longer owns current font text.
BOOL LoadLogFont(HKEY hKey, const wchar_t* name, LOGFONT* logFont)
{
    if (name == NULL || logFont == NULL)
        return FALSE;

    try
    {
        std::wstring wideName(name);
        wideName += L" W";
        std::wstring text;
        if (ReadConfigurationString(hKey, wideName.c_str(), text) &&
            sally::legacy_config::ParseLogFont(text, *logFont))
        {
            return TRUE;
        }

        if (!ReadConfigurationString(hKey, name, text))
            return FALSE;
        return sally::legacy_config::ParseLogFont(text, *logFont) ? TRUE : FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}

// ****************************************************************************

BOOL SaveLogFont(HKEY hKey, const wchar_t* name, LOGFONT* logFont)
{
    if (name == NULL || logFont == NULL)
        return FALSE;
    std::wstring text;
    if (!sally::legacy_config::FormatLogFont(*logFont, text))
        return FALSE;
    try
    {
        std::wstring wideName(name);
        wideName += L" W";
        if (text.size() >= MAXDWORD / sizeof(wchar_t))
            return FALSE;
        const DWORD bytes = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
        return SetValue(hKey, wideName.c_str(), REG_SZ, text.c_str(), bytes);
    }
    catch (...)
    {
        return FALSE;
    }
}

// ****************************************************************************

// ****************************************************************************

// Read-only import of the history keys pre-2.53 wrote with the NARROW SaveHistory (the unsuffixed
// "Named History", "Select History", ... names). Those entries are honest text - see
// ReadConfigurationString for why they arrive as UTF-16 and must not be decoded a second time.
// Current histories live under the "* W" keys and are read by LoadHistory.
BOOL LoadLegacyHistory(HKEY hKey, const wchar_t* name, wchar_t* history[], int maxCount)
{
    if (history == NULL || maxCount < 0)
        return FALSE;

    HKEY historyKey = NULL;
    if (!OpenKey(hKey, name, historyKey))
        return TRUE;

    std::vector<wchar_t*> loaded;
    try
    {
        loaded.assign(static_cast<size_t>(maxCount), nullptr);
    }
    catch (...)
    {
        CloseKey(historyKey);
        return FALSE;
    }
    BOOL ok = TRUE;
    for (int i = 0; i < maxCount; ++i)
    {
        wchar_t valueName[16];
        _itow_s(i + 1, valueName, _countof(valueName), 10);
        std::wstring text;
        if (!ReadConfigurationString(historyKey, valueName, text))
            continue;

        loaded[i] = static_cast<wchar_t*>(malloc((text.size() + 1) * sizeof(wchar_t)));
        if (loaded[i] == NULL)
        {
            TRACE_E(LOW_MEMORY);
            ok = FALSE;
            break;
        }
        memcpy(loaded[i], text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    }
    CloseKey(historyKey);

    if (ok)
    {
        for (int i = 0; i < maxCount; ++i)
        {
            free(history[i]);
            history[i] = loaded[i];
            loaded[i] = NULL;
        }
    }
    for (wchar_t* text : loaded)
        free(text);
    return ok;
}

// ****************************************************************************

// ENCODING CONTRACT — read this before touching either function.
//
// These used to store UTF-16 entries by handing their raw bytes to the ANSI value
// API under REG_SZ, which put the wide bytes on disk REINTERPRETED as ANSI
// characters. That round-trips only on a single-byte code page; on DBCS or a
// UTF-8 active code page it corrupts the entry. The full explanation, and why the
// honest value gets its own name instead of overwriting the legacy one, is in
// common/HistoryValueIo.h — the decision lives with the code that implements it.
//
// Both functions now delegate per entry, so the shape rules exist in exactly one
// place and are unit-tested there (gtest_history_value_io) rather than being
// duplicated between a reader and a writer that must agree.
BOOL LoadHistory(HKEY hKey, const wchar_t* name, wchar_t* history[], int maxCount)
{
    HKEY historyKey;
    int i;
    for (i = 0; i < maxCount; i++)
        if (history[i] != NULL)
        {
            free(history[i]);
            history[i] = NULL;
        }
    if (OpenKey(hKey, name, historyKey))
    {
        for (i = 0; i < maxCount; i++)
        {
            if (!sally::registry::HistoryEntryExists(historyKey, i + 1))
                continue; // absent slot: leave it NULL, as this loop always has

            const std::wstring value = sally::registry::ReadHistoryEntry(historyKey, i + 1);
            history[i] = (wchar_t*)malloc((value.size() + 1) * sizeof(wchar_t));
            if (history[i] == NULL)
            {
                TRACE_E(LOW_MEMORY);
                break;
            }
            memcpy(history[i], value.c_str(), (value.size() + 1) * sizeof(wchar_t));
        }
        CloseKey(historyKey);
    }
    return TRUE;
}

// ****************************************************************************

BOOL SaveHistory(HKEY hKey, const wchar_t* name, wchar_t* history[], int maxCount, BOOL onlyClear)
{
    HKEY historyKey;
    if (CreateKey(hKey, name, historyKey))
    {
        ClearKey(historyKey);

        if (!onlyClear)
        {
            int i;
            for (i = 0; i < maxCount; i++)
            {
                if (history[i] != NULL)
                    sally::registry::WriteHistoryEntry(historyKey, i + 1, history[i]);
                else
                    break;
            }
        }
        CloseKey(historyKey);
    }
    return TRUE;
}

// ****************************************************************************

BOOL LoadViewers(HKEY hKey, const wchar_t* name, CViewerMasks* viewerMasks)
{
    HKEY viewersKey;
    if (OpenKey(hKey, name, viewersKey))
    {
        HKEY subKey;
        wchar_t buf[30];
        wcscpy_s(buf, L"1");
        std::wstring masks;
        std::wstring command;
        std::wstring arguments;
        std::wstring initDir;
        int type;
        int i = 1;
        viewerMasks->DestroyMembers();

        while (OpenKey(viewersKey, buf, subKey))
        {
            if (GetStringValueW(subKey, VIEWERS_MASKS_REG, masks) &&
                wcschr(masks.c_str(), L'|') == NULL &&
                GetValueW(subKey, VIEWERS_TYPE_REG, REG_DWORD, &type, sizeof(DWORD)))
            {
                if (!GetStringValueW(subKey, VIEWERS_COMMAND_REG, command))
                    command.clear();
                if (!GetStringValueW(subKey, VIEWERS_ARGUMENTS_REG, arguments))
                    arguments.clear();
                if (!GetStringValueW(subKey, VIEWERS_INITDIR_REG, initDir))
                    initDir.clear();

                if (Configuration.ConfigVersion < 44) // convert extensions to lowercase
                {
                    const std::wstring masksAux = masks;
                    StrICpyW(masks, masksAux.c_str());
                }
                CViewerMasksItem* item = new CViewerMasksItem(masks.c_str(), command.c_str(), arguments.c_str(),
                                                              initDir.c_str(), type, Configuration.ConfigVersion < 6);
                if (item != NULL && item->IsGood())
                {
                    viewerMasks->Add(item);
                    if (!viewerMasks->IsGood())
                    {
                        delete item;
                        viewerMasks->ResetState();
                        break;
                    }
                }
                else
                {
                    if (item != NULL)
                        delete item;
                    TRACE_E(LOW_MEMORY);
                    break;
                }
            }
            else
                break;
            _itow_s(++i, buf, _countof(buf), 10);
            CloseKey(subKey);
        }
        CloseKey(viewersKey);
    }
    return TRUE;
}

// ****************************************************************************

BOOL SaveViewers(HKEY hKey, const wchar_t* name, CViewerMasks* viewerMasks)
{
    HKEY viewersKey;
    if (CreateKey(hKey, name, viewersKey))
    {
        ClearKey(viewersKey);
        HKEY subKey;
        wchar_t buf[30];
        int i;
        for (i = 0; i < viewerMasks->Count; i++)
        {
            _itow_s(i + 1, buf, _countof(buf), 10);
            if (CreateKey(viewersKey, buf, subKey))
            {
                SetValueW(subKey, VIEWERS_MASKS_REG, REG_SZ, viewerMasks->At(i)->Masks->GetMasksString(), -1);
                if (!viewerMasks->At(i)->Command.empty())
                    SetValueW(subKey, VIEWERS_COMMAND_REG, REG_SZ, viewerMasks->At(i)->Command.c_str(), -1);
                if (!viewerMasks->At(i)->Arguments.empty())
                    SetValueW(subKey, VIEWERS_ARGUMENTS_REG, REG_SZ, viewerMasks->At(i)->Arguments.c_str(), -1);
                if (!viewerMasks->At(i)->InitDir.empty())
                    SetValueW(subKey, VIEWERS_INITDIR_REG, REG_SZ, viewerMasks->At(i)->InitDir.c_str(), -1);
                SetValueW(subKey, VIEWERS_TYPE_REG, REG_DWORD,
                         &viewerMasks->At(i)->ViewerType, sizeof(DWORD));
                CloseKey(subKey);
            }
            else
                break;
        }
        CloseKey(viewersKey);
    }
    return TRUE;
}

// ****************************************************************************

BOOL LoadEditors(HKEY hKey, const wchar_t* name, CEditorMasks* editorMasks)
{
    HKEY editorKey;
    if (OpenKey(hKey, name, editorKey))
    {
        HKEY subKey;
        wchar_t buf[30];
        wcscpy_s(buf, L"1");
        std::wstring masks;
        std::wstring command;
        std::wstring arguments;
        std::wstring initDir;
        int i = 1;
        editorMasks->DestroyMembers();

        while (OpenKey(editorKey, buf, subKey))
        {
            if (GetStringValueW(subKey, EDITORS_MASKS_REG, masks))
            {
                if (!GetStringValueW(subKey, EDITORS_COMMAND_REG, command))
                    command.clear();
                if (!GetStringValueW(subKey, EDITORS_ARGUMENTS_REG, arguments))
                    arguments.clear();
                if (!GetStringValueW(subKey, EDITORS_INITDIR_REG, initDir))
                    initDir.clear();

                if (Configuration.ConfigVersion < 44) // convert extensions to lowercase
                {
                    const std::wstring masksAux = masks;
                    StrICpyW(masks, masksAux.c_str());
                }
                CEditorMasksItem* item = new CEditorMasksItem(masks.c_str(), command.c_str(), arguments.c_str(), initDir.c_str());
                if (item != NULL && item->IsGood())
                {
                    editorMasks->Add(item);
                    if (!editorMasks->IsGood())
                    {
                        delete item;
                        editorMasks->ResetState();
                        break;
                    }
                }
                else
                {
                    if (item != NULL)
                        delete item;
                    TRACE_E(LOW_MEMORY);
                    break;
                }
            }
            else
                break;
            _itow_s(++i, buf, _countof(buf), 10);
            CloseKey(subKey);
        }
        CloseKey(editorKey);
    }
    return TRUE;
}

// ****************************************************************************

BOOL SaveEditors(HKEY hKey, const wchar_t* name, CEditorMasks* editorMasks)
{
    HKEY editorKey;
    if (CreateKey(hKey, name, editorKey))
    {
        ClearKey(editorKey);
        HKEY subKey;
        wchar_t buf[30];
        int i;
        for (i = 0; i < editorMasks->Count; i++)
        {
            _itow_s(i + 1, buf, _countof(buf), 10);
            if (CreateKey(editorKey, buf, subKey))
            {
                SetValueW(subKey, EDITORS_MASKS_REG, REG_SZ, editorMasks->At(i)->Masks->GetMasksString(), -1);
                SetValueW(subKey, EDITORS_COMMAND_REG, REG_SZ, editorMasks->At(i)->Command.c_str(), -1);
                SetValueW(subKey, EDITORS_ARGUMENTS_REG, REG_SZ, editorMasks->At(i)->Arguments.c_str(), -1);
                SetValueW(subKey, EDITORS_INITDIR_REG, REG_SZ, editorMasks->At(i)->InitDir.c_str(), -1);
                CloseKey(subKey);
            }
            else
                break;
        }
        CloseKey(editorKey);
    }
    return TRUE;
}

// ****************************************************************************

static void ShowFileErrorW(HWND hParent, int errTextID, const wchar_t* fileName, DWORD err)
{
    const std::wstring errorText = GetErrorTextOwned(err).c_str();
    std::wstring msg = FormatStrW(LoadStrW(errTextID), fileName, errorText.c_str());
    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
}

// Wide: the export target file path can carry any Unicode component - a non-ANSI
// Windows account name puts one in CreateOurPathInRoamingAPPDATAW's own suggested directory. The
// narrow ExportConfiguration(const char*, ...) this replaced had zero remaining callers once
// CM_EXPORTCONFIG (main_window_commands_help.cpp) was switched to this one - deleted rather than
// kept as an unused wrapper.
//
// The ANSI floor this function was written around is GONE. reglib is separately
// compiled and follows the target's UNICODE define; before P4.2 that define was absent, so
// CopyBranch/Dump were the char* half and the file path had to be resolved to an
// exact-or-8.3-alias-or-refuse ANSI designator (ResolveAnsiToolPath) at their boundary, while the
// registry root went through TryWideToAnsiRoundTripExact. UNICODE is unconditional now, both
// resolve wide-native, and the whole narrowing apparatus was the dead arm of an #ifdef. Nothing
// in this path converts any more - the wchar_t* argument reaches Dump unchanged.
BOOL ExportConfigurationW(HWND hParent, const wchar_t* fileName, BOOL clearKeyBeforeImport)
{
    if (SALAMANDER_ROOT_REG == NULL)
    {
        TRACE_E("ExportConfigurationW(): SALAMANDER_ROOT_REG == NULL");
        return FALSE;
    }

    std::wstring keyName = L"HKEY_CURRENT_USER\\";
    keyName += SALAMANDER_ROOT_REG;
    const wchar_t* dumpFileName = fileName;
    const wchar_t* dumpKeyNameForClear = clearKeyBeforeImport ? keyName.c_str() : NULL;

    BOOL ret = FALSE;
    CSalamanderRegistryExAbstractW* sysReg = REG_SysRegistryFactoryW();
    CSalamanderRegistryExAbstractW* memReg = REG_MemRegistryFactoryW();
    if (sysReg != NULL && memReg != NULL)
    {
        LoadSaveToRegistryMutex.Enter();
        eRPE_ERROR regerr = CopyRegistryBranchW(keyName.c_str(), sysReg, memReg);
        LoadSaveToRegistryMutex.Leave();
        if (RPE_OK == regerr)
        {
            memReg->RemoveHiddenKeysAndValues(); // cut out keys and values that should not be exported
            HANDLE dumpFile = gFileSystem->CreateFile(dumpFileName, GENERIC_WRITE, 0, NULL,
                                                      CREATE_ALWAYS, 0, 0);
            const DWORD createError = GetLastError();
            HANDLES_ADD_EX(__otQuiet, dumpFile != INVALID_HANDLE_VALUE, __htFile,
                           __hoCreateFile, dumpFile, createError, TRUE);
            if (dumpFile == INVALID_HANDLE_VALUE)
                ShowFileErrorW(hParent, IDS_EXPORTCFG_FILEERR, fileName, 0 /* not used */);
            else
            {
                ret = memReg->Dump(dumpFile, dumpKeyNameForClear);
                HANDLES_REMOVE(dumpFile, __htFile, "IFileSystem::CloseHandle");
                gFileSystem->CloseFileHandle(dumpFile);
                if (!ret)
                    ShowFileErrorW(hParent, IDS_EXPORTCFG_FILEERR, fileName, 0 /* not used */);
            }
        }
        else
            ShowFileErrorW(hParent, IDS_EXPORTCFG_REGERR, fileName, 0 /* not used */);
    }
    if (sysReg != NULL)
        sysReg->Release();
    if (memReg != NULL)
        memReg->Release();

    return ret;
}

// ****************************************************************************

BOOL ImportConfigurationW(HWND hParent, const wchar_t* fileName, BOOL ignoreIfNotExists,
                          BOOL autoImportConfig, BOOL* importCfgFromFileWasSkipped)
{
    TRACE_I("ImportConfigurationW(): begin");
    DWORD err = 0;
    HANDLE file = gFileSystem->CreateFile(fileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                                          OPEN_EXISTING, 0, 0);
    const DWORD openError = GetLastError();
    HANDLES_ADD_EX(__otQuiet, file != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, file, openError, TRUE);
    if (file == INVALID_HANDLE_VALUE)
    {
        err = openError;
        if (!ignoreIfNotExists || err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
            ShowFileErrorW(hParent, IDS_IMPORTCFG_OPENERR, fileName, err);
        TRACE_I("ImportConfigurationW(): end");
        return FALSE;
    }

    if (autoImportConfig)
    {
        HANDLES_REMOVE(file, __htFile, "IFileSystem::CloseHandle");
        gFileSystem->CloseFileHandle(file);
        *importCfgFromFileWasSkipped = TRUE;
        TRACE_I("ImportConfigurationW(): end");
        return FALSE;
    }

    IfExistSetSplashScreenText(LoadStrW(IDS_STARTUP_IMPORT_CONFIG));

    BOOL ret = FALSE;
    LPWSTR buf = NULL;
    CQuadWord size;
    uint64_t sizeValue = 0;
    const FileResult sizeResult = gFileSystem->GetHandleFileSize(file, &sizeValue);
    if (sizeResult.success)
    {
        size.Set((DWORD)sizeValue, (DWORD)(sizeValue >> 32));
        if (size <= CQuadWord(10000000, 0)) // above 10MB it is 100% nonsense...
        {
            buf = (LPWSTR)malloc((DWORD)size.Value + sizeof(WCHAR));
            if (buf != NULL) // "always true" (in case of an error we just don’t crash; the user dismissed the out-of-memory message)
            {
                DWORD bytesRead;
                const FileResult readResult = gFileSystem->ReadFromHandle(file, buf, (DWORD)size.Value, &bytesRead);
                if (!readResult.success)
                {
                    ShowFileErrorW(hParent, IDS_IMPORTCFG_OPENERR, fileName, readResult.errorCode);
                    free(buf);
                    buf = NULL;
                }
                else
                {
                    if ((DWORD)size.Value > bytesRead)
                    {
                        size.Set(bytesRead, 0);
                        TRACE_EW(L"ImportConfigurationW(): reading only " << bytesRead << L" bytes from configuration file (" << fileName << L")");
                    }
                }
            }
        }
        else
            ShowFileErrorW(hParent, IDS_IMPORTCFG_TOOBIG, fileName, 0 /* not used */);
    }
    else
        ShowFileErrorW(hParent, IDS_IMPORTCFG_OPENERR, fileName, sizeResult.errorCode);

    HANDLES_REMOVE(file, __htFile, "IFileSystem::CloseHandle");
    gFileSystem->CloseFileHandle(file);

    if (buf != NULL)
    {
        *(WCHAR*)((LPBYTE)buf + (DWORD)size.Value) = 0; // safety net for too short file
        DWORD utf16ByteSize = 0;
        const eRPE_ERROR conversionError = ConvertRegistryFileToUtf16(&buf, (DWORD)size.Value, utf16ByteSize);
        if (conversionError != RPE_OK)
        {
            ShowFileErrorW(hParent,
                           conversionError == RPE_OUT_OF_MEMORY ? IDS_IMPORTCFG_REGERR : IDS_IMPORTCFG_INVALIDFORMAT,
                           fileName, 0 /* not used */);
            free(buf);
            buf = NULL;
        }
        else
            size.Set(utf16ByteSize, 0);
    }

    if (buf != NULL)
    {
        // first try to parse it into memory; if it contains format errors we will not shove it into the registry at all
        CSalamanderRegistryExAbstractW* memReg = REG_MemRegistryFactoryW();
        LPWSTR bufMem = _wcsdup(buf); // parsing changes the buffer, so keep the original for the next pass
        TRACE_I("ImportConfigurationW(): Parse to memory: begin");
        eRPE_ERROR regerr = bufMem != NULL && memReg != NULL ? ParseRegistryFileW(bufMem, memReg, TRUE) : RPE_OUT_OF_MEMORY; // dirty hack: when deleting the configuration key, we do not remove .hidden keys and values (because of the trial version + checkver)
        TRACE_I("ImportConfigurationW(): Parse to memory: end");
        free(bufMem);
        BOOL verIsOK = RPE_OK == regerr; // verify whether the file even contains our configuration version
        if (verIsOK)
        {
            HKEY key;
            if (memReg->OpenKey(HKEY_CURRENT_USER, SalamanderConfigurationRoots[0], key))
                memReg->CloseKey(key);
            else
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_IMPORTCFG_NOTOURVER), fileName);
                if (gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), msg.c_str()).type != PromptResult::kYes)
                {
                    verIsOK = FALSE;
                }
            }
        }
        if (memReg != NULL)
            memReg->Release();
        if (verIsOK && RPE_OK == regerr) // both the config version and the file itself look OK; import it into the registry
        {
            CSalamanderRegistryExAbstractW* sysReg = REG_SysRegistryFactoryW();

            if (sysReg != NULL)
            {
                LoadSaveToRegistryMutex.Enter();
                TRACE_I("ImportConfigurationW(): Parse to registry: begin");
                regerr = ParseRegistryFileW(buf, sysReg, TRUE); // dirty hack: when deleting the configuration key, we do not remove .hidden keys and values (because of the trial version + checkver)
                TRACE_I("ImportConfigurationW(): Parse to registry: end");
                if (RPE_OK == regerr)
                    ret = TRUE; // success
                LoadSaveToRegistryMutex.Leave();

                Configuration.ConfigWasImported = TRUE;
                sysReg->Release();
            }
            else
                regerr = RPE_OUT_OF_MEMORY;
        }
        if (RPE_OK != regerr)
        {
            int errTextID = IDS_IMPORTCFG_REGERR;
            switch (regerr)
            {
            case RPE_NOT_REG_FILE:
                errTextID = IDS_IMPORTCFG_NOTREG;
                break; // not a reg4 or reg5 file

            case RPE_ROOT_INVALID_KEY:
            case RPE_INVALID_KEY:
            case RPE_VALUE_MISSING_QUOTE:
            case RPE_VALUE_MISSING_ASSIG:
            case RPE_VALUE_INVALID_TYPE:
            case RPE_VALUE_DWORD:
            case RPE_VALUE_STRING:
            case RPE_VALUE_HEX:
            case RPE_INVALID_MBCS:
            case RPE_INVALID_FORMAT:
                errTextID = IDS_IMPORTCFG_INVALIDFORMAT;
                break; // format error

                // case RPE_OUT_OF_MEMORY:
                // case RPE_KEY_OPEN:
                // case RPE_KEY_CREATE:
                // case RPE_VALUE_GET_SIZE:
                // case RPE_VALUE_GET:
                // case RPE_VALUE_SET: errTextID = IDS_IMPORTCFG_REGERR; break;   // other error (memory + registry write failure)
            }
            ShowFileErrorW(hParent, errTextID, fileName, 0 /* not used */);
        }

        free(buf);
    }
    TRACE_I("ImportConfigurationW(): end");
    return ret;
}

// 2026-08-26: the narrow ImportConfiguration(char*, ...) thin wrapper was deleted -
// confirmed-dead (zero callers anywhere: CM_IMPORTCONFIG only shows an info message, and the real
// caller, sally_entry_lifecycle.cpp's startup auto-import, already calls ImportConfigurationW
// directly).

//****************************************************************************
//
// CLanguage
//

const char* RT_SLGSIGN = "SLGSIGN";

typedef DWORD(WINAPI* FSalamanderLanguageEntry)();

CLanguage::CLanguage()
{
    FileName = NULL;
    LanguageID = 0;
    AuthorW = NULL;
    Web = NULL;
    CommentW = NULL;
    HelpDir = NULL;
}

void CLanguage::Free()
{
    free(FileName);
    FileName = NULL;
    free(AuthorW);
    AuthorW = NULL;
    free(Web);
    Web = NULL;
    free(CommentW);
    CommentW = NULL;
    free(HelpDir);
    HelpDir = NULL;
}

BOOL CLanguage::Init(const wchar_t* fileName, WORD languageID, const WCHAR* authorW,
                     const wchar_t* web, const WCHAR* commentW, const wchar_t* helpdir)
{
    Free();
    LanguageID = languageID;
    FileName = DupStr(fileName);
    AuthorW = DupStr(authorW);
    Web = DupStr(web);
    CommentW = DupStr(commentW);
    HelpDir = DupStr(helpdir);
    if (FileName == NULL || AuthorW == NULL || Web == NULL || CommentW == NULL || HelpDir == NULL)
    {
        Free();
        return FALSE;
    }
    return TRUE;
}

/*
// a copy of this routine also exists in the Translator program
BOOL LoadSLGData(HINSTANCE hModule, const char *resName, LPVOID buff, int buffSize, BOOL string)
{
  HRSRC hrsrc = FindResource(hModule, resName, RT_SLGSIGN);
  if (hrsrc != NULL)
  {
    int size = SizeofResource(hModule, hrsrc);
    if (size > 0)
    {
      HGLOBAL hglb = LoadResource(hModule, hrsrc);
      if (hglb != NULL)
      {
        LPVOID data = LockResource(hglb);
        if (data != NULL)
        {
          ZeroMemory(buff, buffSize);
          if (string)
          {
            int sz = min(buffSize - 1, size);
            strncpy_s((char*)buff, buffSize, (char*)data, sz);
          }
          else
          {
            int sz = min(buffSize, size);
            memcpy(buff, data, sz);
          }
          return TRUE;
        }
      }
    }
  }
  ZeroMemory(buff, buffSize);
  return FALSE;
}
*/

BOOL IsSLGFileValid(HINSTANCE hModule, HINSTANCE hSLG, WORD& slgLangID, wchar_t* isIncomplete)
{
    // compare the SLG VERSIONINFO version against Salamander's and return TRUE if they match,
    // otherwise FALSE; in case of a match also extract \\VarFileInfo\\Translation and set 'langID'
    CVersionInfo slgVer;
    CVersionInfo moduleVer;

    BYTE *slgBuf, *moduleBuf;
    DWORD slgSize, moduleSize;

    if (!slgVer.ReadResource(hSLG, VS_VERSION_INFO))
    {
        TRACE_EW(L"Unable to load VERSIONINFO resource from SLG module: " << GetModuleFileNameForTrace(hSLG));
        return FALSE;
    }
    if (!moduleVer.ReadResource(hModule, VS_VERSION_INFO))
    {
        TRACE_EW(L"Unable to load VERSIONINFO resource from plugin: " << GetModuleFileNameForTrace(hModule));
        return FALSE;
    }

    // retrieve pointers to the VS_FIXEDFILEINFO structures
    if (!slgVer.QueryValue(L"\\", &slgBuf, &slgSize))
        return FALSE;
    if (!moduleVer.QueryValue(L"\\", &moduleBuf, &moduleSize))
        return FALSE;

    // the SLG version must be identical to our version
    if (((VS_FIXEDFILEINFO*)slgBuf)->dwFileVersionMS != ((VS_FIXEDFILEINFO*)moduleBuf)->dwFileVersionMS ||
        ((VS_FIXEDFILEINFO*)slgBuf)->dwFileVersionLS != ((VS_FIXEDFILEINFO*)moduleBuf)->dwFileVersionLS)
    {
        wchar_t ver1[20];
        swprintf_s(ver1, _countof(ver1), L"%08X%08X", ((VS_FIXEDFILEINFO*)moduleBuf)->dwFileVersionMS,
                   ((VS_FIXEDFILEINFO*)moduleBuf)->dwFileVersionLS);
        wchar_t ver2[20];
        swprintf_s(ver2, _countof(ver2), L"%08X%08X", ((VS_FIXEDFILEINFO*)slgBuf)->dwFileVersionMS,
                   ((VS_FIXEDFILEINFO*)slgBuf)->dwFileVersionLS);
        TRACE_EW(L"Plugin and SLG module are not of the same version (0x" << ver1 << L" != 0x" << ver2 << L"). Plugin: " << GetModuleFileNameForTrace(hModule));
        TRACE_EW(L"... SLG module: " << GetModuleFileNameForTrace(hSLG));
        return FALSE;
    }

    if (isIncomplete != NULL)
    {
        isIncomplete[0] = 0;
        if (!slgVer.QueryString(L"\\StringFileInfo\\040904b0\\SLGIncomplete", isIncomplete, ISSLGINCOMPLETE_SIZE))
        {
            TRACE_EW(L"Missing SLGIncomplete value in VERSIONINFO resource in SLG module: " << GetModuleFileNameForTrace(hSLG));
            return FALSE;
        }
    }

    // extract the language in which the SLG is written
    if (!slgVer.QueryValue(L"\\VarFileInfo\\Translation", &slgBuf, &slgSize))
    {
        TRACE_EW(L"Missing Translation value in VERSIONINFO resource in SLG module: " << GetModuleFileNameForTrace(hSLG));
        return FALSE;
    }

    slgLangID = *((WORD*)slgBuf);

    return TRUE;
}

BOOL CLanguage::Init(const wchar_t* fileName, HINSTANCE modul)
{
    BOOL ret = FALSE;
    if (modul == NULL)
        modul = HInstance;
    std::wstring pathW;
    // sally.h:637 always declared this wide and all three callers already
    // passed wide names; only the definition lagged, so AnsiToWide(fileName) was recovering
    // bytes it had just been handed.
    std::wstring slgNameW = fileName;

    HINSTANCE hLib = NULL;
    if (BuildModuleRelativePathW(modul, (L"lang\\" + slgNameW).c_str(), pathW))
    {
        hLib = HANDLES(LoadLibraryW(pathW.c_str()));
    }
    if (hLib != NULL)
    {
        WORD langID;
        if (IsSLGFileValid(modul, hLib, langID, NULL))
        {
            CVersionInfo ver;
            WCHAR slg_athorW[500] = {0};
            WCHAR slg_web[500] = {0};
            WCHAR slg_commentW[500] = {0};
            WCHAR slg_helpdir[100] = {0};
            WCHAR slg_incomplete[200] = {0};

            BOOL ok = TRUE;
            if (ok)
            {
                ok &= ver.ReadResource(hLib, VS_VERSION_INFO);
                if (!ok)
                    TRACE_EW(L"Missing VERSIONINFO resource in language file " << pathW);
            }
            if (ok)
            {
                ok &= ver.QueryString(L"\\StringFileInfo\\040904b0\\SLGAuthor", slg_athorW, _countof(slg_athorW));
                if (!ok)
                    TRACE_EW(L"Missing SLGAuthor in VERSIONINFO resource in language file " << pathW);
            }
            if (ok)
            {
                ok &= ver.QueryString(L"\\StringFileInfo\\040904b0\\SLGWeb", slg_web, _countof(slg_web));
                if (!ok)
                    TRACE_EW(L"Missing SLGWeb in VERSIONINFO resource in language file " << pathW);
            }
            if (ok)
            {
                ok &= ver.QueryString(L"\\StringFileInfo\\040904b0\\SLGComment", slg_commentW, _countof(slg_commentW));
                if (!ok)
                    TRACE_EW(L"Missing SLGComment in VERSIONINFO resource in language file " << pathW);
            }
            if (ok)
            {
                if (!ver.QueryString(L"\\StringFileInfo\\040904b0\\SLGHelpDir", slg_helpdir, _countof(slg_helpdir)))
                {
                    slg_helpdir[0] = 0; // plugins do not have SLGHelpDir defined (used only in Salamander's .slg)
                    if (modul == HInstance)
                        ok = FALSE; // however this variable cannot be missing in Salamander
                }
                // read only to test that the item exists
                if (!ver.QueryString(L"\\StringFileInfo\\040904b0\\SLGIncomplete", slg_incomplete, _countof(slg_incomplete)))
                {
                    slg_incomplete[0] = 0; // plugins do not have SLGIncomplete defined (used only in Salamander's .slg)
                    if (modul == HInstance)
                        ok = FALSE; // however this variable cannot be missing in Salamander
                }
            }
            if (ok)
                ok &= Init(fileName, langID, slg_athorW, slg_web, slg_commentW, slg_helpdir);
            if (ok)
                ret = TRUE;
        }
        else
            TRACE_EW(L"SLG is not valid (or plugin's VERSIONINFO resource is not set properly): " << pathW);

        HANDLES(FreeLibrary(hLib));
    }
    else
        TRACE_EW(L"Cannot load SLG module " << pathW);
    return ret;
}

BOOL CLanguage::GetLanguageName(wchar_t* buffer, int bufferSize)
{
    if (GetLocaleInfoW(MAKELCID(LanguageID, SORT_DEFAULT), LOCALE_SLANGUAGE, buffer, bufferSize) == 0)
    {
        lstrcpynW(buffer, L"?", bufferSize);
    }
    return TRUE;
}
