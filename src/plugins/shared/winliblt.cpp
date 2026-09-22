// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#include "precomp.h"
//#include <windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif // _MSC_VER
#include <limits.h>
#include <new>
#include <stdio.h>
//#include <commctrl.h>  // need HIMAGELIST
#include <ostream>
#ifdef __BORLANDC__
#include <stdlib.h>
#endif // __BORLANDC__

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#include "spl_base.h"
#include "dbg.h"
#include "plugindarkmode.h"
#include "plugin_narrow_compat.h"

#ifdef ENABLE_PROPERTYDIALOG
#include "arraylt.h"
#endif // ENABLE_PROPERTYDIALOG

#include "winliblt.h"

#ifdef _MSC_VER
#ifndef itoa
#define itoa _itoa
#endif // itoa
#endif // _MSC_VER

namespace
{
std::wstring WindowClassName;
std::wstring WindowClassName2;
std::wstring WinLibStrings[WLS_COUNT] = {
    L"Invalid number!",
    L"Error",
    L"This text cannot be stored: it is too long, or it contains characters the "
    L"system code page cannot represent."};
}

const wchar_t* CWINDOW_CLASSNAME = L"";
const wchar_t* CWINDOW_CLASSNAME2 = L""; // does not have CS_VREDRAW | CS_HREDRAW

ATOM AtomObject = 0; // window "property" with a pointer to the object (used in WindowsManager)
CWindowsManager WindowsManager;

FWinLibLTHelpCallback WinLibLTHelpCallback = NULL; // callback for connecting to HTML help

//
// ****************************************************************************

void SetWinLibStrings(LPCWSTR invalidNumber, LPCWSTR error, LPCWSTR textNotStorable)
{
    try
    {
        std::wstring invalidNumberText = invalidNumber != NULL ? invalidNumber : L"";
        std::wstring errorText = error != NULL ? error : L"";
        WinLibStrings[WLS_INVALID_NUMBER].swap(invalidNumberText);
        WinLibStrings[WLS_ERROR].swap(errorText);
        // NULL means "keep the built-in English text" - a plugin that has not yet
        // added the string to its resources still gets a message rather than a
        // blank box.
        if (textNotStorable != NULL)
        {
            std::wstring notStorableText = textNotStorable;
            WinLibStrings[WLS_TEXT_NOT_STORABLE].swap(notStorableText);
        }
    }
    catch (const std::bad_alloc&)
    {
        TRACE_E("Unable to allocate WinLib diagnostic strings.");
    }
}

void SetupWinLibHelp(FWinLibLTHelpCallback helpCallback)
{
    WinLibLTHelpCallback = helpCallback;
}

BOOL InitializeWinLib(LPCWSTR pluginName, HINSTANCE dllInstance)
{
    PluginDarkMode_Initialize();

    try
    {
        std::wstring className = pluginName != NULL ? pluginName : L"";
        std::wstring className2 = className;
        className += L" - WinLib Universal Window";
        className2 += L" - WinLib Universal Window2";
        WindowClassName.swap(className);
        WindowClassName2.swap(className2);
        CWINDOW_CLASSNAME = WindowClassName.c_str();
        CWINDOW_CLASSNAME2 = WindowClassName2.c_str();
    }
    catch (const std::bad_alloc&)
    {
        TRACE_E("Unable to allocate WinLib window class names.");
        return FALSE;
    }

    AtomObject = GlobalAddAtomW(L"object handle"); // all plugins will use the same atom, no collision
    if (AtomObject == 0)
    {
        TRACE_E("GlobalAddAtom has failed");
        return FALSE;
    }

    INITCOMMONCONTROLSEX initCtrls;
    initCtrls.dwSize = sizeof(INITCOMMONCONTROLSEX);
    initCtrls.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES |
                      ICC_TAB_CLASSES | ICC_COOL_CLASSES;
    if (!InitCommonControlsEx(&initCtrls))
    {
        TRACE_E("InitCommonControlsEx failed");
        return FALSE;
    }

    if (!CWindow::RegisterUniversalClass(dllInstance))
    {
        DWORD err = GetLastError();
        TRACE_C("Registration of the universal window has failed. Error=" << err);
        return FALSE;
    }
    return TRUE;
}

void ReleaseWinLib(HINSTANCE dllInstance)
{
    if (WindowsManager.WindowsCount != 0)
    {
        // problem - after unloading the plugin the app may crash because the window procedure is called
        //          in an unloaded DLL (if the windows were killed as part of killed threads, it is OK)
        TRACE_E("Unable to release WinLibLT - some window or dialog (count = " << WindowsManager.WindowsCount << ") is still attached to WinLibLT!");
        // return;  // if it was a window in a killed thread, WinLibLT can be released, otherwise unregister returns an error
    }

    // unregister classes so they can be registered again on the next plugin load
    if (CWINDOW_CLASSNAME2[0] != 0 && CWINDOW_CLASSNAME[0] != 0)
    {
        if (!UnregisterClassW(CWINDOW_CLASSNAME2, dllInstance))
            TRACE_E("UnregisterClass(CWINDOW_CLASSNAME2) failed!");
        if (!UnregisterClassW(CWINDOW_CLASSNAME, dllInstance))
            TRACE_E("UnregisterClass(CWINDOW_CLASSNAME) failed!");
    }

    if (AtomObject != 0)
        GlobalDeleteAtom(AtomObject);
}

//
// ****************************************************************************
// CWindow
//
// lpvParam - if CreateWindow calls CWindow::CWindowProc
//            (it is in the window class), it must contain the address of the created window object

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
                       LPVOID lpvParam)        // pointer to the created window object
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
        if (WindowsManager.GetWindowPtr(hWnd) == NULL) // if it is not yet in WindowsManager
            AttachToWindow(hWnd);                      // then add it -> subclassing
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
                     LPVOID lpvParam)        // pointer to the created window object
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
        TRACE_E("Bad window handle. hWnd = " << hWnd);
        DefWndProc = DefWindowProcW;
        return;
    }
    if (!WindowsManager.AddWindow(hWnd, this))
    {
        TRACE_E("Error during attaching object to window.");
        DefWndProc = DefWindowProcW;
        return;
    }
    HWindow = hWnd;
    SetWindowLongPtr(HWindow, GWLP_WNDPROC, (LONG_PTR)CWindowProc);

    if (DefWndProc == CWindow::CWindowProc) // to by byla rekurze
    {
        TRACE_C("This should never happen.");
        DefWndProc = DefWindowProcW;
    }
}

void CWindow::AttachToControl(HWND dlg, int ctrlID)
{
    if (dlg == NULL)
    {
        TRACE_E("Incorrect call to CWindow::AttachToControl.");
        return;
    }
    HWND hwnd = GetDlgItem(dlg, ctrlID);
    if (hwnd == NULL)
        TRACE_E("Control with ctrlID = " << ctrlID << " is not in dialog.");
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
        if (WinLibLTHelpCallback != NULL && HelpID != -1 &&
            (GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0)
        {
            WinLibLTHelpCallback(HWindow, HelpID);
            return TRUE;
        }
        if (GetWindowLong(HWindow, GWL_STYLE) & WS_CHILD)
            break;   // if we do not handle F1 and this is a child window, let F1 fall through to the parent
        return TRUE; // if it is not a child, finish handling F1
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
    case WM_CREATE: // first message - attach the object to the window
    {
        // handle MDI_CHILD_WINDOW
        if (((CREATESTRUCT*)lParam)->dwExStyle & WS_EX_MDICHILD)
            wnd = (CWindow*)((MDICREATESTRUCT*)((CREATESTRUCT*)lParam)->lpCreateParams)->lParam;
        else
            wnd = (CWindow*)((CREATESTRUCT*)lParam)->lpCreateParams;
        if (wnd == NULL)
        {
            TRACE_E("Error during creating of window.");
            return FALSE;
        }
        else
        {
            wnd->HWindow = hwnd;
            //--- add window by hwnd to the window list
            if (!WindowsManager.AddWindow(hwnd, wnd)) // error
            {
                TRACE_E("Error during creating of window.");
                return FALSE;
            }
            PluginDarkMode_ApplyTitleBar(hwnd);
            PluginDarkMode_ApplyListTreeThemeRecursive(hwnd);
        }
        break;
    }

    case WM_DESTROY: // last message - detach the object from the window
    {
        wnd = (CWindow*)WindowsManager.GetWindowPtr(hwnd);
        if (wnd != NULL && wnd->Is(otWindow))
        {
            // Petr: moved this below wnd->WindowProc() so that during WM_DESTROY
            //       messages still arrive (Lukas needed this)
            // WindowsManager.DetachWindow(hwnd);

            LRESULT res = wnd->WindowProc(uMsg, wParam, lParam);

            // now back to the old procedure (due to subclassing)
            WindowsManager.DetachWindow(hwnd);

            // if the current WndProc is different from ours, do not change it,
            // because someone in the subclass chain already restored the original WndProc
            WNDPROC currentWndProc = (WNDPROC)GetWindowLongPtr(wnd->HWindow, GWLP_WNDPROC);
            if (currentWndProc == CWindow::CWindowProc)
                SetWindowLongPtr(wnd->HWindow, GWLP_WNDPROC, (LONG_PTR)wnd->DefWndProc);

            if (wnd->IsAllocated())
                delete wnd;
            else
                wnd->HWindow = NULL; // no longer attached
            if (res == 0)
                return 0; // the application handled it
            wnd = NULL;
        }
        break;
    }

    default:
    {
        wnd = (CWindow*)WindowsManager.GetWindowPtr(hwnd);
#if defined(_DEBUG) || defined(__DEBUG_WINLIB)
        if (wnd != NULL && !wnd->Is(otWindow))
        {
            TRACE_C("This should never happen.");
            wnd = NULL;
        }
#endif
    }
    }

    if ((uMsg == WM_SETTINGCHANGE && PluginDarkMode_OnSettingChange(lParam)) ||
        uMsg == WM_THEMECHANGED)
    {
        PluginDarkMode_ApplyTitleBar(hwnd);
        PluginDarkMode_ApplyListTreeThemeRecursive(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
    }

    //--- call WindowProc(...) of the corresponding window object
    if (wnd != NULL)
        return wnd->WindowProc(uMsg, wParam, lParam);
    else
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    // error or the message arrived before WM_CREATE
}

BOOL CWindow::RegisterUniversalClass(HINSTANCE dllInstance)
{
    WNDCLASSW CWindowClass;
    CWindowClass.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    CWindowClass.lpfnWndProc = CWindow::CWindowProc;
    CWindowClass.cbClsExtra = 0;
    CWindowClass.cbWndExtra = 0;
    CWindowClass.hInstance = dllInstance;
    CWindowClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
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

BOOL CWindow::RegisterUniversalClass(UINT style, int cbClsExtra, int cbWndExtra, HINSTANCE dllInstance,
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
    windowClass.hInstance = dllInstance;
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
        TRACE_E("CDialog::TransferData(): This error should be detected in Validate() and not in Transfer() because already transferred data cannot be changed to their original values! It means that user cannot leave dialog box without changes using Cancel button now!");
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
    return DialogBoxParamW(Modul, MAKEINTRESOURCEW(ResID), Parent,
                          (DLGPROC)CDialog::CDialogProc, (LPARAM)this);
}

HWND CDialog::Create()
{
    Modal = FALSE;
    return CreateDialogParamW(Modul, MAKEINTRESOURCEW(ResID), Parent,
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
        return TRUE; // want focus from DefDlgProc
    }

    case WM_HELP:
    {
        if (WinLibLTHelpCallback != NULL && HelpID != -1 &&
            (GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0)
        {
            WinLibLTHelpCallback(HWindow, HelpID);
        }
        return TRUE; // do not let F1 fall through to the parent even if we do not call WinLibLTHelpCallback()
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HBRUSH hBrush = PluginDarkMode_GetDialogCtlColorBrush(uMsg, (HDC)wParam, (HWND)lParam);
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
            if (WinLibLTHelpCallback != NULL && HelpID != -1 &&
                (GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0)
            {
                WinLibLTHelpCallback(HWindow, HelpID);
            }
            else
                TRACE_E("CDialog::DialogProc(): ignoring IDHELP: SetupWinLibHelp() was not called or HelpID is -1!");
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
        if (PluginDarkMode_OnSettingChange(lParam))
        {
            PluginDarkMode_ApplyTitleBar(HWindow);
            PluginDarkMode_ApplyListTreeThemeRecursive(HWindow);
            InvalidateRect(HWindow, NULL, TRUE);
        }
        break;
    }

    case WM_THEMECHANGED:
    {
        PluginDarkMode_ApplyTitleBar(HWindow);
        PluginDarkMode_ApplyListTreeThemeRecursive(HWindow);
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
    case WM_INITDIALOG: // first message - attach the object to the dialog
    {
        dlg = (CDialog*)lParam;
        if (dlg == NULL)
        {
            TRACE_E("Error during creating of dialog.");
            return TRUE;
        }
        else
        {
            dlg->HWindow = hwndDlg;
            //--- add window by hwndDlg to the window list
            if (!WindowsManager.AddWindow(hwndDlg, dlg)) // error
            {
                TRACE_E("Error during creating of dialog.");
                return TRUE;
            }
            PluginDarkMode_ApplyTitleBar(hwndDlg);
            dlg->NotifDlgJustCreated(); // introduced as a place to adjust dialog layout
        }
        break;
    }

    case WM_DESTROY: // last message - detach the object from the dialog
    {
        dlg = (CDialog*)WindowsManager.GetWindowPtr(hwndDlg);
        INT_PTR ret = FALSE; // in case it does not handle it
        if (dlg != NULL && dlg->Is(otDialog))
        {
            // Petr: moved this below wnd->WindowProc() so that during WM_DESTROY
            //       messages still arrive (Lukas needed this)
            // WindowsManager.DetachWindow(hwndDlg);

            ret = dlg->DialogProc(uMsg, wParam, lParam);

            WindowsManager.DetachWindow(hwndDlg);
            if (dlg->IsAllocated())
                delete dlg;
            else
                dlg->HWindow = NULL; // detached state
        }
        return ret;
    }

    default:
    {
        dlg = (CDialog*)WindowsManager.GetWindowPtr(hwndDlg);
#if defined(_DEBUG) || defined(__DEBUG_WINLIB)
        if (dlg != NULL && !dlg->Is(otDialog))
        {
            TRACE_C("This should never happen.");
            dlg = NULL;
        }
#endif
    }
    }
    //--- call DialogProc(...) of the corresponding dialog object
    INT_PTR dlgRes;
    if (dlg != NULL)
        dlgRes = dlg->DialogProc(uMsg, wParam, lParam);
    else
        dlgRes = FALSE; // error or message did not arrive between WM_INITDIALOG and WM_DESTROY

    if (dlg != NULL && uMsg == WM_INITDIALOG)
        PluginDarkMode_ApplyListTreeThemeRecursive(hwndDlg);

    return dlgRes;
}

//
// ****************************************************************************
// CPropSheetPage
//

#ifdef ENABLE_PROPERTYDIALOG

CPropSheetPage::CPropSheetPage(LPCWSTR title, HINSTANCE modul, int resID,
                               DWORD flags, HICON icon, CObjectOrigin origin)
    : CDialog(modul, resID, NULL, origin)
{
    Init(title, modul, resID, icon, flags, origin);
}

CPropSheetPage::CPropSheetPage(LPCWSTR title, HINSTANCE modul, int resID, int helpID,
                               DWORD flags, HICON icon, CObjectOrigin origin)
    : CDialog(modul, resID, helpID, NULL, origin)
{
    Init(title, modul, resID, icon, flags, origin);
}

void CPropSheetPage::Init(LPCWSTR title, HINSTANCE modul, int resID,
                          HICON icon, DWORD flags, CObjectOrigin origin)
{
    Title.reset();
    try
    {
        if (title != NULL)
            Title.emplace(title);
    }
    catch (const std::bad_alloc&)
    {
        TRACE_E("Low memory!");
    }
    Flags = flags;
    Icon = icon;

    ParentDialog = NULL; // nastavuje se z CPropertyDialog::Execute()
}

CPropSheetPage::~CPropSheetPage() = default;

BOOL CPropSheetPage::ValidateData()
{
    CTransferInfo ti(HWindow, ttDataFromWindow);
    Validate(ti);
    if (!ti.IsGood())
    {
        if (PropSheet_GetCurrentPageHwnd(Parent) != HWindow)
            PropSheet_SetCurSel(Parent, HWindow, 0);

        ti.EnsureControlIsFocused(ti.FailCtrlID);
        return FALSE;
    }
    else
        return TRUE;
}

BOOL CPropSheetPage::TransferData(CTransferType type)
{
    CTransferInfo ti(HWindow, type);
    Transfer(ti);
    if (!ti.IsGood())
    {
        if (ti.Type == ttDataFromWindow &&
            PropSheet_GetCurrentPageHwnd(Parent) != HWindow)
            PropSheet_SetCurSel(Parent, HWindow, 0);

        ti.EnsureControlIsFocused(ti.FailCtrlID);
        return FALSE;
    }
    else
        return TRUE;
}

HPROPSHEETPAGE
CPropSheetPage::CreatePropSheetPage()
{
    PROPSHEETPAGEW psp;
    psp.dwSize = sizeof(PROPSHEETPAGEW);
    psp.dwFlags = Flags;
    psp.hInstance = Modul;
    psp.pszTemplate = MAKEINTRESOURCEW(ResID);
    psp.hIcon = Icon;
    psp.pszTitle = Title.has_value() ? Title->c_str() : NULL;
    psp.pfnDlgProc = CPropSheetPage::CPropSheetPageProc;
    psp.lParam = (LPARAM)this;
    psp.pfnCallback = NULL;
    psp.pcRefParent = NULL;
    return CreatePropertySheetPageW(&psp);
}

INT_PTR
CPropSheetPage::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        ParentDialog->HWindow = Parent;
        TransferData(ttDataToWindow);
        return TRUE; // chci focus od DefDlgProc
    }

    case WM_HELP:
    {
        if (WinLibLTHelpCallback != NULL && HelpID != -1 &&
            (GetKeyState(VK_CONTROL) & 0x8000) == 0 && (GetKeyState(VK_SHIFT) & 0x8000) == 0)
        {
            WinLibLTHelpCallback(HWindow, HelpID);
            return TRUE;
        }
        break; // F1 nechame propadnout do parenta
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HBRUSH hBrush = PluginDarkMode_GetDialogCtlColorBrush(uMsg, (HDC)wParam, (HWND)lParam);
        if (hBrush != NULL)
            return (INT_PTR)hBrush;
        break;
    }

    case WM_NOTIFY:
    {
        if (((NMHDR*)lParam)->code == PSN_KILLACTIVE) // deaktivace stranky
        {
            if (ValidateData())
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE);
            else // nepovolime deaktivaci stranky
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }

        if (((NMHDR*)lParam)->code == PSN_HELP)
        { // stisknuto tlacitko Help
            if (WinLibLTHelpCallback != NULL && HelpID != -1)
                WinLibLTHelpCallback(HWindow, HelpID);
            return TRUE;
        }

        if (((NMHDR*)lParam)->code == PSN_SETACTIVE) // page activation
        {
            if (ParentDialog != NULL && ParentDialog->LastPage != NULL)
            { // remember the last page
                *ParentDialog->LastPage = ParentDialog->GetCurSel();
            }
            break;
        }

        if (((NMHDR*)lParam)->code == PSN_APPLY)
        { // ApplyNow or OK button pressed
            if (TransferData(ttDataFromWindow))
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, PSNRET_NOERROR);
            else
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }

        if (((NMHDR*)lParam)->code == PSN_WIZFINISH)
        { // Finish button pressed
            // PSN_KILLACTIVE not received - perform validation
            if (!ValidateData())
            {
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }

            // loop through all pages for transfer
            for (int i = 0; i < ParentDialog->Count; i++)
            {
                if (ParentDialog->At(i)->HWindow != NULL)
                {
                    if (!ParentDialog->At(i)->TransferData(ttDataFromWindow))
                    {
                        SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
                        return TRUE;
                    }
                }
            }
            SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE);
            return TRUE;
        }
        break;
    }

    case WM_SETTINGCHANGE:
    {
        if (PluginDarkMode_OnSettingChange(lParam))
        {
            PluginDarkMode_ApplyTitleBar(HWindow);
            PluginDarkMode_ApplyListTreeThemeRecursive(HWindow);
            InvalidateRect(HWindow, NULL, TRUE);
        }
        break;
    }

    case WM_THEMECHANGED:
    {
        PluginDarkMode_ApplyTitleBar(HWindow);
        PluginDarkMode_ApplyListTreeThemeRecursive(HWindow);
        InvalidateRect(HWindow, NULL, TRUE);
        break;
    }
    }
    return FALSE;
}

INT_PTR CALLBACK
CPropSheetPage::CPropSheetPageProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,
                                   LPARAM lParam)
{
    CPropSheetPage* dlg;
    switch (uMsg)
    {
    case WM_INITDIALOG: // first message - attach the object to the dialog
    {
        dlg = (CPropSheetPage*)((PROPSHEETPAGEW*)lParam)->lParam;
        if (dlg == NULL)
        {
            TRACE_E("Error during creating of dialog.");
            return TRUE;
        }
        else
        {
            dlg->HWindow = hwndDlg;
            dlg->Parent = ::GetParent(hwndDlg);
            //--- add window by hwndDlg to the window list
            if (!WindowsManager.AddWindow(hwndDlg, dlg)) // error
            {
                TRACE_E("Error during creating of dialog.");
                return TRUE;
            }
            PluginDarkMode_ApplyTitleBar(hwndDlg);
            dlg->NotifDlgJustCreated(); // introduced as a place to adjust dialog layout
        }
        break;
    }

    case WM_DESTROY: // last message - detach the object from the dialog
    {
        dlg = (CPropSheetPage*)WindowsManager.GetWindowPtr(hwndDlg);
        INT_PTR ret = FALSE; // in case it does not handle it
        if (dlg != NULL && dlg->Is(otDialog))
        {
            // Petr: moved this below wnd->WindowProc() so that during WM_DESTROY
            //       messages still arrive (Lukas needed this)
            // WindowsManager.DetachWindow(hwndDlg);

            ret = dlg->DialogProc(uMsg, wParam, lParam);

            WindowsManager.DetachWindow(hwndDlg);
            if (dlg->IsAllocated())
                delete dlg;
            else
                dlg->HWindow = NULL; // detached state
        }
        return ret;
    }

    default:
    {
        dlg = (CPropSheetPage*)WindowsManager.GetWindowPtr(hwndDlg);
#if defined(_DEBUG) || defined(__DEBUG_WINLIB)
        if (dlg != NULL && !dlg->Is(otPropSheetPage))
        {
            TRACE_C("This should never happen.");
            dlg = NULL;
        }
#endif
    }
    }
    //--- call DialogProc(...) of the corresponding dialog object
    INT_PTR dlgRes;
    if (dlg != NULL)
        dlgRes = dlg->DialogProc(uMsg, wParam, lParam);
    else
        dlgRes = FALSE; // error or message did not arrive between WM_INITDIALOG and WM_DESTROY

    if (dlg != NULL && uMsg == WM_INITDIALOG)
        PluginDarkMode_ApplyListTreeThemeRecursive(hwndDlg);

    return dlgRes;
}

//
// ****************************************************************************
// CPropertyDialog
//

INT_PTR
CPropertyDialog::Execute()
{
    if (Count > 0)
    {
        PROPSHEETHEADERW psh;
        psh.dwSize = sizeof(PROPSHEETHEADERW);
        psh.dwFlags = Flags;
        psh.hwndParent = Parent;
        psh.hInstance = Modul;
        psh.hIcon = Icon;
        psh.pszCaption = Caption.c_str();
        psh.nPages = Count;
        if (StartPage < 0 || StartPage >= Count)
            StartPage = 0;
        psh.nStartPage = StartPage;
        HPROPSHEETPAGE* pages = new HPROPSHEETPAGE[Count];
        if (pages == NULL)
        {
            TRACE_E("Low memory!");
            return -1;
        }
        psh.phpage = pages;
        for (int i = 0; i < Count; i++)
        {
            psh.phpage[i] = At(i)->CreatePropSheetPage();
            At(i)->ParentDialog = this;
        }
        psh.pfnCallback = Callback;
        INT_PTR ret = PropertySheetW(&psh);
        delete pages;
        return ret;
    }
    else
    {
        TRACE_E("Incorrect call to CPropertyDialog::Execute.");
        return -1;
    }
}

int CPropertyDialog::GetCurSel()
{
    HWND tabCtrl = PropSheet_GetTabControl(HWindow);
    return TabCtrl_GetCurSel(tabCtrl);
}

#endif // ENABLE_PROPERTYDIALOG

//
// ****************************************************************************
// CWindowsManager
//

BOOL CWindowsManager::AddWindow(HWND hWnd, CWindowsObject* wnd)
{
    if (AtomObject == 0)
    {
        TRACE_E("Uninitialized AtomObject - you should call InitializeWinLib() before first use of WinLib.");
        return FALSE;
    }
    if (!SetPropW(hWnd, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(AtomObject)), (HANDLE)wnd))
    {
        DWORD err = GetLastError();
        TRACE_E("SetProp has failed (err=" << err << ")");
        return FALSE;
    }
    WindowsCount++;
    return TRUE;
}

void CWindowsManager::DetachWindow(HWND hWnd)
{
    if (RemovePropW(hWnd, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(AtomObject))))
    {
        WindowsCount--;
    }
    else
    {
        TRACE_E("RemoveProp has failed. hwnd = " << hWnd);
    }
}

CWindowsObject*
CWindowsManager::GetWindowPtr(HWND hWnd)
{
    return (CWindowsObject*)GetPropW(hWnd, reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(AtomObject)));
}

//
// ****************************************************************************
// CWindowQueue
//

CWindowQueue::~CWindowQueue()
{
    if (!Empty())
        TRACE_E("Some window is still opened in " << QueueName << " queue!"); // should not happen...
    // multithreading is no longer a risk here (plugin is ending, threads are/were terminated)
    // free at least some memory
    CWindowQueueItem* last;
    CWindowQueueItem* item = Head;
    while (item != NULL)
    {
        last = item;
        item = item->Next;
        delete last;
    }
}

BOOL CWindowQueue::Add(CWindowQueueItem* item)
{
    CS.Enter();
    if (item != NULL)
    {
        item->Next = Head;
        Head = item;
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
        if (item->HWindow == hWindow) // found, remove it
        {
            if (last != NULL)
                last->Next = item->Next;
            else
                Head = item->Next;
            delete item;
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
    BOOL e;
    CS.Enter();
    e = Head == NULL;
    CS.Leave();
    return e;
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

BOOL CWindowQueue::CloseAllWindows(BOOL force, int waitTime, int forceWaitTime)
{
    // send a request to close all windows
    BroadcastMessage(WM_CLOSE, 0, 0);

    // wait until/if they close
    DWORD ti = GetTickCount();
    DWORD w = force ? forceWaitTime : waitTime;
    while ((w == INFINITE || w > 0) && !Empty())
    {
        DWORD t = GetTickCount() - ti;
        if (w == INFINITE || t < w) // should we keep waiting
        {
            if (w == INFINITE || 50 < w - t)
                Sleep(50);
            else
            {
                Sleep(w - t);
                break;
            }
        }
        else
            break;
    }
    return force || Empty();
}

//
// ****************************************************************************
// CTransferInfo
//

BOOL CTransferInfo::GetControl(HWND& ctrlHWnd, int ctrlID, BOOL ignoreIsGood)
{
    if (!ignoreIsGood && !IsGood())
        return FALSE; // no point in processing further
    ctrlHWnd = GetDlgItem(HDialog, ctrlID);
    if (ctrlHWnd == NULL)
    {
        TRACE_E("Control with ctrlID = " << ctrlID << " is not in dialog.");
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
        if (wnd == NULL) // focus only if ctrl is not an ancestor of GetFocus
        {                // e.g. edit line in a combo box
            SendMessageW(HDialog, WM_NEXTDLGCTL, (WPARAM)ctrl, TRUE);
        }
    }
    else
        TRACE_E("Control with ctrlID = " << ctrlID << " is not in dialog.");
}

// A narrow transfer buffer refused the text. The refusal is deliberate - exact ACP
// projection never substitutes characters - but ErrorOn() alone only moves focus
// back to the control, so the OK button appears to do nothing at all. Say why.
void CTransferInfo::ReportTextNotStorable(int ctrlID)
{
    MessageBoxW(HDialog, WinLibStrings[WLS_TEXT_NOT_STORABLE].c_str(),
                WinLibStrings[WLS_ERROR].c_str(), MB_OK | MB_ICONEXCLAMATION);
    ErrorOn(ctrlID);
}

void CTransferInfo::EditLine(int ctrlID, char* buffer, DWORD bufferSize, BOOL select)
{
    HWND HWindow;
    if (buffer == NULL || bufferSize == 0)
    {
        ErrorOn(ctrlID);
        return;
    }
    if (GetControl(HWindow, ctrlID))
    {
        try
        {
            switch (Type)
            {
            case ttDataToWindow:
            {
                std::wstring wide;
                if (!LegacyTextToWide(buffer, wide))
                {
                    ErrorOn(ctrlID);
                    break;
                }
                // The buffer is the caller's fixed char[bufferSize] and stays that
                // size, so the control has to stay inside it. This cap is not one of
                // the self-imposed path ceilings the widening removed - without it
                // the user can type more than the buffer can ever hold and the only
                // symptom is an OK button that stops responding.
                SendMessageW(HWindow, EM_LIMITTEXT, bufferSize - 1, 0);
                SendMessageW(HWindow, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(wide.c_str()));
                if (select)
                    SendMessageW(HWindow, EM_SETSEL, 0, -1);
                break;
            }

            case ttDataFromWindow:
            {
                std::wstring wide;
                if (!ReadWindowTextOwnedW(HWindow, wide))
                    ErrorOn(ctrlID);
                else if (!CopyWideToLegacyTextExact(wide.c_str(), buffer, bufferSize))
                    ReportTextNotStorable(ctrlID);
                break;
            }
            }
        }
        catch (const std::bad_alloc&)
        {
            ErrorOn(ctrlID);
        }
    }
}

void CTransferInfo::EditLine(int ctrlID, std::string& value, BOOL select)
{
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        try
        {
            switch (Type)
            {
            case ttDataToWindow:
            {
                std::wstring wide;
                if (!LegacyTextToWide(value.c_str(), wide))
                {
                    ErrorOn(ctrlID);
                    break;
                }
                SendMessageW(HWindow, EM_LIMITTEXT, 0, 0);
                SendMessageW(HWindow, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(wide.c_str()));
                if (select)
                    SendMessageW(HWindow, EM_SETSEL, 0, -1);
                break;
            }

            case ttDataFromWindow:
            {
                std::string staged;
                if (!ReadWindowLegacyTextExact(HWindow, staged))
                    ReportTextNotStorable(ctrlID); // no length cap here - the owner
                                                   // grows; only the code page refuses
                else
                    value.swap(staged);
                break;
            }
            }
        }
        catch (...)
        {
            ErrorOn(ctrlID);
        }
    }
}

void CTransferInfo::EditLine(int ctrlID, std::wstring& value, BOOL select)
{
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        try
        {
            switch (Type)
            {
            case ttDataToWindow:
                SendMessageW(HWindow, EM_LIMITTEXT, 0, 0);
                SendMessageW(HWindow, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(value.c_str()));
                if (select)
                    SendMessageW(HWindow, EM_SETSEL, 0, -1);
                break;

            case ttDataFromWindow:
            {
                std::wstring staged;
                if (!ReadWindowTextOwnedW(HWindow, staged))
                    ErrorOn(ctrlID);
                else
                    value.swap(staged);
                break;
            }
            }
        }
        catch (...)
        {
            ErrorOn(ctrlID);
        }
    }
}

void CTransferInfo::EditLine(int ctrlID, wchar_t* buffer, DWORD bufferSize, BOOL select)
{
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        switch (Type)
        {
        case ttDataToWindow:
        {
            SendMessageW(HWindow, EM_LIMITTEXT, bufferSize - 1, 0);
            SendMessageW(HWindow, WM_SETTEXT, 0, (LPARAM)buffer);
            if (select)
                SendMessageW(HWindow, EM_SETSEL, 0, -1);
            break;
        }

        case ttDataFromWindow:
        {
            SendMessageW(HWindow, WM_GETTEXT, bufferSize, (LPARAM)buffer);
            break;
        }
        }
    }
}

void CTransferInfo::EditLine(int ctrlID, double& value, const wchar_t* format, BOOL select)
{
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        try
        {
            switch (Type)
            {
            case ttDataToWindow:
            {
                const int length = format != NULL ? _scwprintf(format, value) : -1;
                if (length < 0)
                {
                    ErrorOn(ctrlID);
                    break;
                }
                std::wstring text(static_cast<size_t>(length) + 1, L'\0');
                if (swprintf_s(text.data(), text.size(), format, value) != length)
                {
                    ErrorOn(ctrlID);
                    break;
                }
                text.resize(static_cast<size_t>(length));
                SendMessageW(HWindow, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
                if (select)
                    SendMessageW(HWindow, EM_SETSEL, 0, -1);
                break;
            }

            case ttDataFromWindow:
            {
                std::wstring text;
                if (!ReadWindowTextOwnedW(HWindow, text))
                {
                    ErrorOn(ctrlID);
                    value = 0;
                    break;
                }
                wchar_t* s = text.data();
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
                        if (!expPart && (*s == L'e' || *s == L'E'))
                        {
                            expPart = TRUE;
                            if (*(s + 1) == L'+' || *(s + 1) == L'-')
                                s++; // skip +/- after E
                        }
                        else if (*s < L'0' || *s > L'9')
                        {
                            MessageBoxW(HWindow, WinLibStrings[WLS_INVALID_NUMBER].c_str(),
                                        WinLibStrings[WLS_ERROR].c_str(), MB_OK | MB_ICONEXCLAMATION);
                            ErrorOn(ctrlID);
                            break;
                        }
                    }
                    s++;
                }
                if (*s == 0)
                    value = wcstod(text.c_str(), NULL); // only if it is a number
                else
                    value = 0; // on error, set to zero
                break;
            }
            }
        }
        catch (const std::bad_alloc&)
        {
            ErrorOn(ctrlID);
            value = 0;
        }
    }
}

void CTransferInfo::EditLine(int ctrlID, int& value, BOOL select)
{
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        try
        {
            switch (Type)
            {
            case ttDataToWindow:
            {
                const std::wstring text = std::to_wstring(value);
                SendMessageW(HWindow, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
                if (select)
                    SendMessageW(HWindow, EM_SETSEL, 0, -1);
                break;
            }

            case ttDataFromWindow:
            {
                std::wstring text;
                if (!ReadWindowTextOwnedW(HWindow, text))
                {
                    ErrorOn(ctrlID);
                    break;
                }

                const wchar_t* s = text.c_str();
                if (*s == L'-' || *s == L'+')
                    s++;        // skip sign
                while (*s != 0) // validate number
                {
                    if (*s < L'0' || *s > L'9')
                    {
                        MessageBoxW(HWindow, WinLibStrings[WLS_INVALID_NUMBER].c_str(),
                                    WinLibStrings[WLS_ERROR].c_str(), MB_OK | MB_ICONEXCLAMATION);
                        ErrorOn(ctrlID);
                        break;
                    }
                    s++;
                }

                wchar_t* endptr;
                value = wcstoul(text.c_str(), &endptr, 10); // replacement for atoi / _ttoi, which return 2147483647 instead of 4000000000 (because it is a signed int)
                break;
            }
            }
        }
        catch (const std::bad_alloc&)
        {
            ErrorOn(ctrlID);
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
