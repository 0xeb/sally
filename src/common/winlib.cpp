// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <windows.h>
#include <crtdbg.h>
#include <ostream>
#include <stdio.h>
#include <limits.h>
#include <new>
#include <commctrl.h> // need LPCOLORMAP

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "trace.h"
#include "messages.h"
#include "handles.h"

#include "array.h"

#include "winlib.h"
#ifdef INSIDE_SALAMANDER
#include "darkmode.h"
#endif

#ifdef INSIDE_SALAMANDER
#define WinLib_DarkMode_GetDialogCtlColorBrush DarkMode_GetDialogCtlColorBrush
#define WinLib_DarkMode_OnSettingChange DarkMode_OnSettingChange
#define WinLib_DarkMode_ApplyTitleBar DarkMode_ApplyTitleBar
#define WinLib_DarkMode_ApplyListTreeThemeRecursive DarkMode_ApplyListTreeThemeRecursive
#else
static HBRUSH WinLib_DarkMode_GetDialogCtlColorBrush(UINT msg, HDC hdc, HWND hCtrl)
{
    UNREFERENCED_PARAMETER(msg);
    UNREFERENCED_PARAMETER(hdc);
    UNREFERENCED_PARAMETER(hCtrl);
    return NULL;
}

static BOOL WinLib_DarkMode_OnSettingChange(LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    return FALSE;
}

static void WinLib_DarkMode_ApplyTitleBar(HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);
}

static void WinLib_DarkMode_ApplyListTreeThemeRecursive(HWND root)
{
    UNREFERENCED_PARAMETER(root);
}
#endif

// Precaution against runtime check failure in debug version: original macro version casts rgb to WORD,
// so reports data loss (RED component)
#undef GetGValue
#define GetGValue(rgb) ((BYTE)(((rgb) >> 8) & 0xFF))

const wchar_t* CWINDOW_CLASSNAME = L"WinLib Universal Window";
const wchar_t* CWINDOW_CLASSNAME2 = L"WinLib Universal Window2"; // does not have CS_VREDRAW | CS_HREDRAW


CWinLibHelp* WinLibHelp = NULL;
CWindowsManager WindowsManager;
HINSTANCE HInstance = NULL;
BOOL WinLibReleased = FALSE; // TRUE = ReleaseWinLib() has already been called

std::wstring WinLibStrings[WLS_COUNT] = {
    L"Invalid number!",
    L"Error"};

//
// ****************************************************************************

void SetWinLibStrings(const wchar_t* invalidNumber, const wchar_t* error)
{
    try
    {
        std::wstring invalidNumberText = invalidNumber != NULL ? invalidNumber : L"";
        std::wstring errorText = error != NULL ? error : L"";
        WinLibStrings[WLS_INVALID_NUMBER].swap(invalidNumberText);
        WinLibStrings[WLS_ERROR].swap(errorText);
    }
    catch (const std::bad_alloc&)
    {
        TRACE_ET(L"Unable to allocate WinLib diagnostic strings.");
    }
}

BOOL InitializeWinLib()
{
    InitCommonControls();

    if (!CWindow::RegisterUniversalClass())
    {
        TRACE_CT(L"Unable to register universal window class.");
        return FALSE;
    }
    WinLibReleased = FALSE;
    return TRUE;
}

void ReleaseWinLib()
{
    // we must disconnect open windows from WinLib, because WinLib is ending ...
    int c = WindowsManager.GetCount();
    if (c > 0)
        TRACE_ET(L"ReleaseWinLib(): WindowsManager still contains opened windows: " << c);
    WinLibReleased = TRUE;
}

BOOL SetupWinLibHelp(CWinLibHelp* winLibHelp)
{
    WinLibHelp = winLibHelp;
    return TRUE;
}

// ****************************************************************************
// WinLibIsWindowsVersionOrGreater (copy of SalIsWindowsVersionOrGreater)
//
// Based on SDK 8.1 VersionHelpers.h
// Indicates if the current OS version matches, or is greater than, the provided
// version information. This function is useful in confirming a version of Windows
// Server that doesn't share a version number with a client release.
// http://msdn.microsoft.com/en-us/library/windows/desktop/dn424964%28v=vs.85%29.aspx
//

BOOL WinLibIsWindowsVersionOrGreater(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
    OSVERSIONINFOEXW osvi;
    DWORDLONG const dwlConditionMask = VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(0,
                                                                                                   VER_MAJORVERSION, VER_GREATER_EQUAL),
                                                                               VER_MINORVERSION, VER_GREATER_EQUAL),
                                                           VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

    SecureZeroMemory(&osvi, sizeof(osvi)); // replacement for memset (doesn't require RTL)
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    osvi.dwMajorVersion = wMajorVersion;
    osvi.dwMinorVersion = wMinorVersion;
    osvi.wServicePackMajor = wServicePackMajor;
    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask) != FALSE;
}

//
// ****************************************************************************
// CWindow
//
// lpvParam - in case CWindow::CWindowProc is called during CreateWindow
//            (it is in the window class), must contain the address of the created window object

HWND CWindow::CreateEx(DWORD dwExStyle,        // extended window style
                       LPCWSTR lpszClassName,  // address of registered class name
                       LPCWSTR lpszWindowName, // address of window name
                       DWORD dwStyle,          // window style
                       int x,                  // horizontal position of window
                       int y,                  // vertical position of window
                       int nWidth,             // window width
                       int nHeight,            // window height
                       HWND hwndParent,        // handle of parent or owner window
                       HMENU hmenu,            // handle of menu or child-window identifier
                       HINSTANCE hinst,        // handle of application instance
                       LPVOID lpvParam)        // pointer to created window object
{
    HWND hWnd = CreateWindowExW(dwExStyle,
                               lpszClassName,
                               lpszWindowName,
                               dwStyle,
                               x,
                               y,
                               nWidth,
                               nHeight,
                               hwndParent,
                               hmenu,
                               hinst,
                               lpvParam);
    if (hWnd != 0)
    {
        if (WindowsManager.GetWindowPtr(hWnd) == NULL) // if it's not yet in WindowsManager
            AttachToWindow(hWnd);                      // then we add it -> subclassing
    }
    return hWnd;
}

HWND CWindow::Create(LPCWSTR lpszClassName,  // address of registered class name
                     LPCWSTR lpszWindowName, // address of window name
                     DWORD dwStyle,          // window style
                     int x,                  // horizontal position of window
                     int y,                  // vertical position of window
                     int nWidth,             // window width
                     int nHeight,            // window height
                     HWND hwndParent,        // handle of parent or owner window
                     HMENU hmenu,            // handle of menu or child-window identifier
                     HINSTANCE hinst,        // handle of application instance
                     LPVOID lpvParam)        // pointer to created window object
{
    return CreateEx(0,
                    lpszClassName,
                    lpszWindowName,
                    dwStyle,
                    x,
                    y,
                    nWidth,
                    nHeight,
                    hwndParent,
                    hmenu,
                    hinst,
                    lpvParam);
}

void CWindow::AttachToWindow(HWND hWnd)
{
    DefWndProc = (WNDPROC)GetWindowLongPtr(hWnd, GWLP_WNDPROC);
    if (DefWndProc == NULL)
    {
        TRACE_ET(L"Invalid handle of window. hWnd = " << hWnd);
        DefWndProc = GetDefWindowProc();
        return;
    }
    if (!WindowsManager.AddWindow(hWnd, this))
    {
        TRACE_ET(L"Error in connecting object to window.");
        DefWndProc = GetDefWindowProc();
        return;
    }
    HWindow = hWnd;
    SetWindowLongPtr(HWindow, GWLP_WNDPROC, (LONG_PTR)CWindowProc);

    if (DefWndProc == CWindow::CWindowProc
        ) // to by byla rekurze
    {
        TRACE_CT(L"This should never happen.");
        DefWndProc = GetDefWindowProc();
    }
}

void CWindow::AttachToControl(HWND dlg, int ctrlID)
{
    if (dlg == NULL)
    {
        TRACE_ET(L"Incorrect call to CWindow::AttachToControl.");
        return;
    }
    HWND hwnd = GetDlgItem(dlg, ctrlID);
    if (hwnd == NULL)
        TRACE_ET(L"Control with ctrlID = " << ctrlID << L" is not in dialog.");
    else
        AttachToWindow(hwnd);
}

void CWindow::DetachWindow()
{
    if (HWindow != NULL)
    {
        WindowsManager.DetachWindow(HWindow);
        SetWindowLongPtr(HWindow, GWLP_WNDPROC, (LONG_PTR)DefWndProc);
        HWindow = NULL;
    }
}

LRESULT
CWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_HELP:
    {
        if (WinLibHelp != NULL && HelpID != -1)
        {
            WinLibHelp->OnHelp(HWindow, HelpID, (HELPINFO*)lParam,
                               (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                               (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return TRUE;
        }
        if (GetWindowLongPtr(HWindow, GWL_STYLE) & WS_CHILD)
            break;   // if we don't process F1 and if it's a child window, we let F1 fall through to parent
        return TRUE; // if it's not a child, we end F1 processing
    }
    }
    return CallWindowProcW((WNDPROC)DefWndProc, HWindow, uMsg, wParam, lParam);
}


LRESULT CALLBACK
CWindow::CWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CWindow* wnd;
    switch (uMsg)
    {
    case WM_CREATE: // first message - connecting object to window
    {
        // handle MDI_CHILD_WINDOW
        if (((CREATESTRUCT*)lParam)->dwExStyle & WS_EX_MDICHILD)                                 // CREATESTRUCTA and CREATESTRUCTW do not differ for dwExStyle or lpCreateParams
            wnd = (CWindow*)((MDICREATESTRUCT*)((CREATESTRUCT*)lParam)->lpCreateParams)->lParam; // MDICREATESTRUCTA and MDICREATESTRUCTW do not differ for lParam
        else
            wnd = (CWindow*)((CREATESTRUCT*)lParam)->lpCreateParams;
        if (wnd == NULL)
        {
            TRACE_ET(L"Unable to create window.");
            return FALSE;
        }
        else
        {
            wnd->HWindow = hwnd;
                                                      // insertion of window by hwnd into window list
            if (!WindowsManager.AddWindow(hwnd, wnd)) // error
            {
                TRACE_ET(L"Unable to create window.");
                return FALSE;
            }
            WinLib_DarkMode_ApplyTitleBar(hwnd);
        }
        break;
    }

    case WM_DESTROY: // last message - disconnecting object from window
    {
        wnd = (CWindow*)WindowsManager.GetWindowPtr(hwnd);
        if (wnd != NULL && wnd->Is(otWindow))
        {
            // Petr: moved below wnd->WindowProc() so that messages still arrive during WM_DESTROY
            //       (needed by Lukas)
            // WindowsManager.DetachWindow(hwnd);

            LRESULT res = wnd->WindowProc(uMsg, wParam, lParam);

            // now back to old procedure (because of subclassing)
            WindowsManager.DetachWindow(hwnd);

            // if current WndProc is different from ours, we won't change it,
            // because someone in the subclassing chain already returned the original WndProc
            WNDPROC currentWndProc = (WNDPROC)GetWindowLongPtr(wnd->HWindow, GWLP_WNDPROC);
            if (currentWndProc == CWindow::CWindowProc)
                SetWindowLongPtr(wnd->HWindow, GWLP_WNDPROC, (LONG_PTR)wnd->DefWndProc);

            if (wnd->IsAllocated())
                delete wnd;
            else
                wnd->HWindow = NULL; // no longer connected
            if (res == 0)
                return 0; // application has processed it
            wnd = NULL;
        }
        break;
    }

    default:
    {
        wnd = (CWindow*)WindowsManager.GetWindowPtr(hwnd);
#ifdef __DEBUG_WINLIB
        if (wnd != NULL && !wnd->Is(otWindow))
        {
            TRACE_CT(L"This should never happen.");
            wnd = NULL;
        }
#endif
    }
    }
    // calling WindowProc(...) method of the corresponding window object
    if ((uMsg == WM_SETTINGCHANGE && WinLib_DarkMode_OnSettingChange(lParam)) ||
        uMsg == WM_THEMECHANGED)
    {
        WinLib_DarkMode_ApplyTitleBar(hwnd);
        WinLib_DarkMode_ApplyListTreeThemeRecursive(hwnd);
    }

    LRESULT lResult;
    if (wnd != NULL)
        lResult = wnd->WindowProc(uMsg, wParam, lParam);
    else // error or message came before WM_CREATE
    {
        lResult = DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    return lResult;
}

BOOL CWindow::RegisterUniversalClass()
{
    WNDCLASSW CWindowClass;
    CWindowClass.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    CWindowClass.lpfnWndProc = CWindow::CWindowProc;
    CWindowClass.cbClsExtra = 0;
    CWindowClass.cbWndExtra = 0;
    CWindowClass.hInstance = HInstance;
    CWindowClass.hIcon = HANDLES(LoadIcon(NULL, IDI_APPLICATION));
    CWindowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    CWindowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    CWindowClass.lpszMenuName = NULL;
    CWindowClass.lpszClassName = CWINDOW_CLASSNAME;

    BOOL ret = RegisterClassW(&CWindowClass) != 0;
    if (ret)
    {
        CWindowClass.style = CS_DBLCLKS;
        CWindowClass.lpszClassName = CWINDOW_CLASSNAME2;
        ret = RegisterClassW(&CWindowClass) != 0;
    }


    return ret;
}

BOOL CWindow::RegisterUniversalClass(UINT style, int cbClsExtra, int cbWndExtra,
                                     HICON hIcon, HCURSOR hCursor, HBRUSH hbrBackground,
                                     LPCWSTR lpszMenuName, LPCWSTR lpszClassName,
                                     HICON hIconSm)
{
    WNDCLASSEXW windowClass;
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = style;
    windowClass.lpfnWndProc = CWindow::CWindowProc;
    windowClass.cbClsExtra = cbClsExtra;
    windowClass.cbWndExtra = cbWndExtra;
    windowClass.hInstance = HInstance;
    windowClass.hIcon = hIcon;
    windowClass.hCursor = hCursor;
    windowClass.hbrBackground = hbrBackground;
    windowClass.lpszMenuName = lpszMenuName;
    windowClass.lpszClassName = lpszClassName;
    windowClass.hIconSm = hIconSm;

    return RegisterClassExW(&windowClass) != 0;
}

//
// ****************************************************************************
// CDialog
//

BOOL CDialog::ValidateData()
{
    CTransferInfo ti(HWindow, ttDataFromWindow);
    Validate(ti);
    if (!ti.IsGood())
    {
        ti.EnsureControlIsFocused(ti.FailCtrlID);
        return FALSE;
    }
    else
        return TRUE;
}

BOOL CDialog::TransferData(CTransferType type)
{
    CTransferInfo ti(HWindow, type);
    Transfer(ti);
    if (!ti.IsGood())
    {
        TRACE_ET(L"CDialog::TransferData(): This error should be detected in Validate() and not in Transfer() because already transferred data cannot be changed to their original values! It means that user cannot leave dialog box without changes using Cancel button now!");
        ti.EnsureControlIsFocused(ti.FailCtrlID);
        return FALSE;
    }
    else
        return TRUE;
}

INT_PTR
CDialog::Execute()
{
    Modal = TRUE;
    return DialogBoxParam(Modul, MAKEINTRESOURCE(ResID), Parent,
                          (DLGPROC)CDialog::CDialogProc, (LPARAM)this);
}

HWND CDialog::Create()
{
    Modal = FALSE;
    return CreateDialogParam(Modul, MAKEINTRESOURCE(ResID), Parent,
                             (DLGPROC)CDialog::CDialogProc, (LPARAM)this);
}

INT_PTR
CDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        TransferData(ttDataToWindow);
        return TRUE; // I want focus from DefDlgProc
    }

    case WM_HELP:
    {
        if (WinLibHelp != NULL && HelpID != -1)
        {
            WinLibHelp->OnHelp(HWindow, HelpID, (HELPINFO*)lParam,
                               (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                               (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        }
        return TRUE; // we don't let F1 fall through to parent even if we don't call WinLibHelp->OnHelp()
    }

    case WM_CONTEXTMENU:
    {
        if (WinLibHelp != NULL)
            WinLibHelp->OnContextMenu((HWND)wParam, LOWORD(lParam), HIWORD(lParam));
        return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HBRUSH hBrush = WinLib_DarkMode_GetDialogCtlColorBrush(uMsg, (HDC)wParam, (HWND)lParam);
        if (hBrush != NULL)
            return (INT_PTR)hBrush;
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDHELP:
        {
            if (WinLibHelp != NULL && HelpID != -1)
            {
                HELPINFO hi;
                memset(&hi, 0, sizeof(hi));
                hi.cbSize = sizeof(hi);
                hi.iContextType = HELPINFO_WINDOW;
                hi.dwContextId = ResID; // in WM_HELP also comes ResID and not HelpID, so that it's consistent
                GetCursorPos(&hi.MousePos);
                WinLibHelp->OnHelp(HWindow, HelpID, &hi, FALSE, FALSE);
            }
            else
                TRACE_ET(L"CDialog::DialogProc(): ignoring IDHELP: SetupWinLibHelp() was not called or HelpID is -1!");
            return TRUE;
        }

        case IDOK:
            if (!ValidateData() ||
                !TransferData(ttDataFromWindow))
                return TRUE;
        case IDCANCEL:
        {
            if (Modal)
                EndDialog(HWindow, wParam);
            else
                DestroyWindow(HWindow);
            return TRUE;
        }
        }
        break;
    }

    case WM_SETTINGCHANGE:
    {
        if (WinLib_DarkMode_OnSettingChange(lParam))
        {
            WinLib_DarkMode_ApplyTitleBar(HWindow);
            WinLib_DarkMode_ApplyListTreeThemeRecursive(HWindow);
            InvalidateRect(HWindow, NULL, TRUE);
        }
        break;
    }

    case WM_THEMECHANGED:
    {
        WinLib_DarkMode_ApplyTitleBar(HWindow);
        WinLib_DarkMode_ApplyListTreeThemeRecursive(HWindow);
        InvalidateRect(HWindow, NULL, TRUE);
        break;
    }
    }
    return FALSE;
}

INT_PTR CALLBACK
CDialog::CDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CDialog* dlg;
    switch (uMsg)
    {
    case WM_INITDIALOG: // first message - connecting object to dialog
    {
        dlg = (CDialog*)lParam;
        if (dlg == NULL)
        {
            TRACE_ET(L"Unable to create dialog.");
            return TRUE;
        }
        else
        {
            dlg->HWindow = hwndDlg;
            // insertion of window by hwndDlg into window list
            if (!WindowsManager.AddWindow(hwndDlg, dlg)) // error
            {
                TRACE_ET(L"Unable to create dialog.");
                return TRUE;
            }
            WinLib_DarkMode_ApplyTitleBar(hwndDlg);
            dlg->NotifDlgJustCreated(); // introduced as place for dialog layout adjustment
        }
        break;
    }

    case WM_DESTROY: // last message - disconnecting object from dialog
    {
        dlg = (CDialog*)WindowsManager.GetWindowPtr(hwndDlg);
        INT_PTR ret = FALSE; // in case it doesn't process it
        if (dlg != NULL && dlg->Is(otDialog))
        {
            // Petr: moved below dlg->DialogProc() so that messages still arrive during WM_DESTROY
            //       (needed by Lukas)
            // WindowsManager.DetachWindow(hwndDlg);

            ret = dlg->DialogProc(uMsg, wParam, lParam);

            WindowsManager.DetachWindow(hwndDlg);
            if (dlg->IsAllocated())
                delete dlg;
            else
                dlg->HWindow = NULL; // disconnect information
        }
        return ret;
    }

    default:
    {
        dlg = (CDialog*)WindowsManager.GetWindowPtr(hwndDlg);
#ifdef __DEBUG_WINLIB
        if (dlg != NULL && !dlg->Is(otDialog))
        {
            TRACE_CT(L"This should never happen.");
            dlg = NULL;
        }
#endif
    }
    }
    // calling DialogProc(...) method of the corresponding dialog object
    INT_PTR dlgRes;
    if (dlg != NULL)
        dlgRes = dlg->DialogProc(uMsg, wParam, lParam);
    else
        dlgRes = FALSE; // error or message didn't come between WM_INITDIALOG and WM_DESTROY

    if (dlg != NULL && uMsg == WM_INITDIALOG)
        WinLib_DarkMode_ApplyListTreeThemeRecursive(hwndDlg);

    return dlgRes;
}

//
// ****************************************************************************
// CWindowsManager
//

CWindowsManager::CWindowsManager() : TDirectArray<CWindowData>(50, 50)
{
    memset(LastHWnd, 0, WNDMGR_CACHE_SIZE * sizeof(HWND));
    memset(LastWnd, 0, WNDMGR_CACHE_SIZE * sizeof(CWindowsObject*));
#ifdef __DEBUG_WINLIB
    search = 0;
    cache = 0;
    maxWndCount = 0;
#endif
}

BOOL CWindowsManager::AddWindow(HWND hWnd, CWindowsObject* wnd)
{
    if (WinLibReleased)
        return FALSE;

    CS.Enter();

    int i;
    if (GetIndex(hWnd, i))
    {
        CS.Leave();

        TRACE_ET(L"Attempt to add window which is already contained in WindowsManager. hwnd = " << hWnd);
        return FALSE;
    }
    else
    {
        CWindowData WindowData;
        WindowData.HWnd = hWnd;
        WindowData.Wnd = wnd;
        Insert(i, WindowData);
        if (!IsGood())
        {
            ResetState();
            TRACE_ET(L"Unable to add window to WindowsManager. hwnd = " << hWnd);
            CS.Leave();
            return FALSE;
        }
        else
        {
            LastHWnd[GetCacheIndex(hWnd)] = hWnd;
            LastWnd[GetCacheIndex(hWnd)] = wnd;
#ifdef __DEBUG_WINLIB
            if (maxWndCount < Count)
                maxWndCount = Count;
#endif
            CS.Leave();
            return TRUE;
        }
    }
}

void CWindowsManager::DetachWindow(HWND hWnd)
{
    if (WinLibReleased)
        return;

    CS.Enter();
    if (LastHWnd[GetCacheIndex(hWnd)] == hWnd) // we must clean the cache
    {
        LastHWnd[GetCacheIndex(hWnd)] = NULL;
        LastWnd[GetCacheIndex(hWnd)] = NULL;
    }

    int i;
    if (GetIndex(hWnd, i))
    {
        Delete(i);
        if (!IsGood())
        {
            ResetState();
            TRACE_ET(L"Unable to detach window from WindowsManager. hwnd = " << hWnd);
            At(i).Wnd = NULL; // at least like this...
        }
    }
    else
        TRACE_ET(L"Attempt to detach window which is not present in WindowsManager. hwnd = " << hWnd);
    CS.Leave();
}

CWindowsObject*
CWindowsManager::GetWindowPtr(HWND hWnd)
{
    if (WinLibReleased)
        return NULL;

    CS.Enter();
    if (LastHWnd[GetCacheIndex(hWnd)] == hWnd)
    {
#ifdef __DEBUG_WINLIB
        cache++;
#endif
        CWindowsObject* ret = LastWnd[GetCacheIndex(hWnd)];
        CS.Leave();
        return ret;
    }
#ifdef __DEBUG_WINLIB
    search++;
#endif
    int i;
    if (GetIndex(hWnd, i)) // found
    {
        LastHWnd[GetCacheIndex(hWnd)] = hWnd;
        LastWnd[GetCacheIndex(hWnd)] = At(i).Wnd;
        CWindowsObject* ret = At(i).Wnd;
        CS.Leave();
        return ret;
    }
    else
    {
        CS.Leave();
        return NULL; // not found
    }
}

int CWindowsManager::GetCount()
{
    if (WinLibReleased)
        return 0;

    CS.Enter();
    int c = Count;
    CS.Leave();
    return c;
}

//
// ****************************************************************************
// CWindowQueue
//

CWindowQueue::~CWindowQueue()
{
    if (!Empty())
        TRACE_ET(L"Some window is still opened in " << QueueName << L" queue!"); // shouldn't happen...
    // here multi-threading is no longer a threat (software is ending, threads are/were terminated)
    // we deallocate at least some memory
    CWindowQueueItem* last;
    CWindowQueueItem* item = Head;
    while (item != NULL)
    {
        last = item;
        item = item->Next;
        delete last;
    }
    Head = NULL;
    Count = 0;
}

BOOL CWindowQueue::Add(CWindowQueueItem* item)
{
    CS.Enter();
    if (item != NULL)
    {
        item->Next = Head;
        Head = item;
        Count++;
        CS.Leave();
        return TRUE;
    }
    CS.Leave();
    return FALSE;
}

void CWindowQueue::Remove(HWND hWindow)
{
    CS.Enter();
    CWindowQueueItem* last = NULL;
    CWindowQueueItem* item = Head;
    while (item != NULL)
    {
        if (item->HWindow == hWindow) // found, remove
        {
            if (last != NULL)
                last->Next = item->Next;
            else
                Head = item->Next;
            delete item;
            Count--;
            CS.Leave();
            return;
        }
        last = item;
        item = item->Next;
    }
    CS.Leave();
}

BOOL CWindowQueue::Empty()
{
    CS.Enter();
    BOOL e = Head == NULL;
    CS.Leave();
    return e;
}

int CWindowQueue::GetWindowCount()
{
    CS.Enter();
    int c = Count;
    CS.Leave();
    return c;
}

void CWindowQueue::BroadcastMessage(DWORD uMsg, WPARAM wParam, LPARAM lParam)
{
    CS.Enter();
    CWindowQueueItem* item = Head;
    while (item != NULL)
    {
        PostMessage(item->HWindow, uMsg, wParam, lParam);
        item = item->Next;
    }
    CS.Leave();
}

//
// ****************************************************************************
// CTransferInfo
//

BOOL CTransferInfo::GetControl(HWND& ctrlHWnd, int ctrlID, BOOL ignoreIsGood)
{
    if (!ignoreIsGood && !IsGood())
        return FALSE; // next ones don't make sense to process
    ctrlHWnd = GetDlgItem(HDialog, ctrlID);
    if (ctrlHWnd == NULL)
    {
        TRACE_ET(L"Control with ctrlID = " << ctrlID << L" is not in dialog.");
        FailCtrlID = ctrlID;
        return FALSE;
    }
    else
        return TRUE;
}

void CTransferInfo::EnsureControlIsFocused(int ctrlID)
{
    HWND ctrl = GetDlgItem(HDialog, ctrlID);
    if (ctrl != NULL)
    {
        HWND wnd = GetFocus();
        while (wnd != NULL && wnd != ctrl)
            wnd = ::GetParent(wnd);
        if (wnd == NULL) // we focus only if ctrl is not an ancestor of GetFocus
        {                // such as edit-line in combo-box
            SendMessageW(HDialog, WM_NEXTDLGCTL, (WPARAM)ctrl, TRUE);
        }
    }
    else
        TRACE_ET(L"Control with ctrlID = " << ctrlID << L" is not in dialog.");
}

void CTransferInfo::EditLine(int ctrlID, wchar_t* buffer, DWORD bufferSizeInChars, BOOL select)
{
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        switch (Type)
        {
        case ttDataToWindow:
        {
            SendMessageW(HWindow, EM_LIMITTEXT, bufferSizeInChars - 1, 0);
            SendMessageW(HWindow, WM_SETTEXT, 0, (LPARAM)buffer);
            if (select)
                SendMessageW(HWindow, EM_SETSEL, 0, -1);
            break;
        }

        case ttDataFromWindow:
        {
            SendMessageW(HWindow, WM_GETTEXT, bufferSizeInChars, (LPARAM)buffer);
            break;
        }
        }
    }
}

// Unconditional in both builds - see the matching comment on the declaration
// in winlib.h. Explicit SendMessageW calls are correct regardless of _UNICODE state.
void CTransferInfo::EditLineW(int ctrlID, WCHAR* buffer, DWORD bufferSizeInChars, BOOL select)
{
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        switch (Type)
        {
        case ttDataToWindow:
        {
            SendMessageW(HWindow, EM_LIMITTEXT, bufferSizeInChars - 1, 0);
            SendMessageW(HWindow, WM_SETTEXT, 0, (LPARAM)buffer);
            if (select)
                SendMessageW(HWindow, EM_SETSEL, 0, -1);
            break;
        }

        case ttDataFromWindow:
        {
            SendMessageW(HWindow, WM_GETTEXT, bufferSizeInChars, (LPARAM)buffer);
            break;
        }
        }
    }
}

void CTransferInfo::EditLineW(int ctrlID, std::wstring& value, BOOL select)
{
    HWND control;
    if (!GetControl(control, ctrlID))
        return;

    switch (Type)
    {
    case ttDataToWindow:
        SendMessageW(control, EM_LIMITTEXT, 0, 0);
        SendMessageW(control, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(value.c_str()));
        if (select)
            SendMessageW(control, EM_SETSEL, 0, -1);
        break;

    case ttDataFromWindow:
    {
        const int length = GetWindowTextLengthW(control);
        if (length <= 0)
        {
            value.clear();
            break;
        }
        value.resize(length + 1);
        const int copied = GetWindowTextW(control, value.data(), length + 1);
        value.resize(copied > 0 ? copied : 0);
        break;
    }
    }
}

void CTransferInfo::EditLine(int ctrlID, double& value, wchar_t* format, BOOL select)
{
    HWND HWindow;
    wchar_t buff[31];
    if (GetControl(HWindow, ctrlID))
    {
        switch (Type)
        {
        case ttDataToWindow:
        {
            SendMessageW(HWindow, EM_LIMITTEXT, 30, 0);
            _stprintf_s(buff, format, value);
            SendMessageW(HWindow, WM_SETTEXT, 0, (LPARAM)buff);
            if (select)
                SendMessageW(HWindow, EM_SETSEL, 0, -1);
            break;
        }

        case ttDataFromWindow:
        {
            SendMessageW(HWindow, WM_GETTEXT, 31, (LPARAM)buff);
            wchar_t* s = buff;
            BOOL decPoints = FALSE;
            BOOL expPart = FALSE;
            if (*s == L'-' || *s == L'+')
                s++;        // skip sign
            while (*s != 0) // convert comma to dot
            {
                if (!expPart && !decPoints && (*s == L',' || *s == L'.'))
                {
                    decPoints = TRUE;
                    *s = L'.';
                }
                else
                {
                    if (!expPart && (*s == L'e' || *s == L'E' || *s == L'd' || *s == L'D'))
                    {
                        expPart = TRUE;
                        if (*(s + 1) == L'+' || *(s + 1) == L'-')
                            s++; // skip +- after E
                    }
                    else
                    {
                        if (*s < L'0' || *s > L'9')
                        {
                            MessageBoxW(HWindow, WinLibStrings[WLS_INVALID_NUMBER].c_str(), WinLibStrings[WLS_ERROR].c_str(),
                                       MB_OK | MB_ICONEXCLAMATION);
                            ErrorOn(ctrlID);
                            break;
                        }
                    }
                }
                s++;
            }
            if (*s == 0)
            {
                wchar_t* stopString;                  // dummy
                value = wcstod(buff, &stopString); // only if it's a number
            }
            else
                value = 0; // on error we give zero
            break;
        }
        }
    }
}

void CTransferInfo::EditLine(int ctrlID, int& value, BOOL select)
{
    HWND HWindow;
    wchar_t buff[16];
    if (GetControl(HWindow, ctrlID))
    {
        switch (Type)
        {
        case ttDataToWindow:
        {
            SendMessageW(HWindow, EM_LIMITTEXT, 15, 0);
            _itow_s(value, buff, 10);
            SendMessageW(HWindow, WM_SETTEXT, 0, (LPARAM)buff);
            if (select)
                SendMessageW(HWindow, EM_SETSEL, 0, -1);
            break;
        }

        case ttDataFromWindow:
        {
            SendMessageW(HWindow, WM_GETTEXT, 16, (LPARAM)buff);

            wchar_t* s = buff;
            if (*s == L'-' || *s == L'+')
                s++;        // skip sign
            while (*s != 0) // number check
            {
                if (*s < L'0' || *s > L'9')
                {
                    MessageBoxW(HWindow, WinLibStrings[WLS_INVALID_NUMBER].c_str(), WinLibStrings[WLS_ERROR].c_str(),
                               MB_OK | MB_ICONEXCLAMATION);
                    ErrorOn(ctrlID);
                    break;
                }
                s++;
            }

            wchar_t* endptr;
            value = wcstoul(buff, &endptr, 10); // replacement for atoi, which instead of 4000000000 returns 2147483647 (because it is SIGNED INT)
            break;
        }
        }
    }
}

void CTransferInfo::EditLine(int ctrlID, __int64& value, BOOL select, BOOL unsignedNum, BOOL hexMode,
                             BOOL ignoreOverflow, BOOL quiet)
{
    if (!unsignedNum && hexMode)
        TRACE_CT(L"CTransferInfo::EditLine(): unexpected combination of parameters (signed number and hex mode).");
    HWND HWindow;
    wchar_t buff[26];
    if (GetControl(HWindow, ctrlID))
    {
        switch (Type)
        {
        case ttDataToWindow:
        {
            SendMessageW(HWindow, EM_LIMITTEXT, 25, 0);
            if (unsignedNum)
                _ui64tow_s(value, buff, _countof(buff), hexMode ? 16 : 10);
            else
                _i64tow_s(value, buff, _countof(buff), hexMode ? 16 : 10);
            CharUpperW(buff);
            SendMessageW(HWindow, WM_SETTEXT, 0, (LPARAM)buff);
            if (select)
                SendMessageW(HWindow, EM_SETSEL, 0, -1);
            break;
        }

        case ttDataFromWindow:
        {
            SendMessageW(HWindow, WM_GETTEXT, 26, (LPARAM)buff);
            CharUpperW(buff);

            wchar_t* s = buff;
            BOOL minus = !unsignedNum && *s == L'-';
            if (!unsignedNum && *s == L'-' || *s == L'+')
                s++; // skip sign
            unsigned __int64 num = 0;
            BOOL overflow = FALSE;
            while (*s != 0) // number check
            {
                if ((*s < L'0' || *s > L'9') && (!hexMode || *s < L'A' || *s > L'F'))
                {
                    if (!quiet)
                    {
                        MessageBoxW(HWindow, WinLibStrings[WLS_INVALID_NUMBER].c_str(), WinLibStrings[WLS_ERROR].c_str(),
                                   MB_OK | MB_ICONEXCLAMATION);
                    }
                    ErrorOn(ctrlID);
                    break;
                }
                else
                {
                    if (hexMode && num > 0x0fffffffffffffffui64 ||
                        !hexMode && num > 1844674407370955161ui64 || // max is 18446744073709551615ui64
                        !hexMode && num == 1844674407370955161ui64 && *s > L'5')
                    {
                        overflow = TRUE; // unsigned overflow
                    }
                    else
                        num = num * (hexMode ? 16 : 10) + (*s >= L'0' && *s <= L'9' ? *s - L'0' : 10 + *s - L'A');
                }
                s++;
            }
            if (*s != 0)
            {
                value = 0; // on error we give zero
                break;
            }

            // on overflow we give boundary values (inspiration: value = _ttoi64(buff))
            if (unsignedNum)
            {
                if (overflow)
                    value = 0xffffffffffffffffui64 /* _UI64_MAX */;
                else
                    value = num;
            }
            else
            {
                if (minus)
                {
                    if (overflow || num > (unsigned __int64)(-(-9223372036854775807i64 - 1) /* -_I64_MIN */))
                    {
                        value = (-9223372036854775807i64 - 1) /* _I64_MIN */;
                        overflow = TRUE; // signed overflow
                    }
                    else
                        value = -(__int64)num;
                }
                else
                {
                    if (overflow || num > (unsigned __int64)9223372036854775807i64 /* _I64_MAX */)
                    {
                        value = 9223372036854775807i64 /* _I64_MAX */;
                        overflow = TRUE; // signed overflow
                    }
                    else
                        value = num;
                }
            }
            if (overflow)
            {
                if (ignoreOverflow) // we report overflow only via TRACE_E
                {
                    TRACE_ET(L"CTransferInfo::EditLine(" << ctrlID << L"): " << (unsignedNum ? L"unsigned " : L"") << L"int64 overflow has occured!");
                }
                else
                {
                    if (!quiet)
                    {
                        MessageBoxW(HWindow, WinLibStrings[WLS_INVALID_NUMBER].c_str(), WinLibStrings[WLS_ERROR].c_str(),
                                   MB_OK | MB_ICONEXCLAMATION);
                    }
                    ErrorOn(ctrlID);
                }
            }
            break;
        }
        }
    }
}

void CTransferInfo::RadioButton(int ctrlID, int ctrlValue, int& value)
{
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        switch (Type)
        {
        case ttDataToWindow:
        {
            SendMessageW(HWindow, BM_SETCHECK, ctrlValue == value, 0);
            break;
        }

        case ttDataFromWindow:
        {
            if (SendMessageW(HWindow, BM_GETCHECK, 0, 0) == 1)
                value = ctrlValue;
            break;
        }
        }
    }
}

void CTransferInfo::CheckBox(int ctrlID, int& value)
{
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        switch (Type)
        {
        case ttDataToWindow:
        {
            SendMessageW(HWindow, BM_SETCHECK, value, 0);
            break;
        }

        case ttDataFromWindow:
        {
            value = (int)SendMessageW(HWindow, BM_GETCHECK, 0, 0);
            break;
        }
        }
    }
}

void CTransferInfo::TrackBar(int ctrlID, int& value)
{
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        switch (Type)
        {
        case ttDataToWindow:
        {
            SendMessageW(HWindow, TBM_SETPOS, TRUE, value);
            break;
        }
        case ttDataFromWindow:
        {
            value = (int)SendMessageW(HWindow, TBM_GETPOS, 0, 0);
            break;
        }
        }
    }
}
