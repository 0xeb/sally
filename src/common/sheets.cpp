// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <windows.h>
#include <crtdbg.h>
#include <tchar.h>
#include <ostream>
#include <uxtheme.h>
#include <algorithm>

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "trace.h"
#include "handles.h"
#include "array.h"
#include "winlib.h"
#include "multimon.h"
#include "sheets.h"
#include "configuration/ConfigurationLayout.h"
#include "DialogFont.h"
#ifndef __TRACESERVER
#include "darkmode.h"
#endif

extern CWinLibHelp* WinLibHelp;

static void FillRectWithColor(HDC hDC, const RECT* rect, COLORREF color)
{
    HBRUSH hBrush = CreateSolidBrush(color);
    if (hBrush != NULL)
    {
        FillRect(hDC, rect, hBrush);
        DeleteObject(hBrush);
    }
}

//
// ****************************************************************************
// CElasticLayout
//

CElasticLayout::CElasticLayout(HWND hWindow)
    : ResizeCtrls(2, 2), MoveCtrls(20, 20)
{
    HWindow = hWindow;
    SplitY = 0;
}

void CElasticLayout::AddResizeCtrl(int resID)
{
    HWND hChild = GetDlgItem(HWindow, resID);
    if (hChild != NULL)
    {
        RECT r;
        GetWindowRect(hChild, &r);

        // if the bottom edge of the control is larger than SplitY, move the SplitY border
        POINT p = {r.right, r.bottom};
        ScreenToClient(HWindow, &p);
        if (p.y > SplitY)
            SplitY = p.y;

        CElasticLayoutCtrl ctrl;
        ctrl.HCtrl = hChild;
        ctrl.Pos.x = r.right - r.left;
        ctrl.Pos.y = 0;
        ResizeCtrls.Add(ctrl);
    }
    else
    {
        TRACE_E("CElasticLayout::AddResizeCtrl() Unknown control: resID=" << resID);
    }
}

void CElasticLayout::Scale(int numeratorX, int denominatorX, int numeratorY, int denominatorY)
{
    SplitY = MulDiv(SplitY, numeratorY, denominatorY);
    for (int i = 0; i < ResizeCtrls.Count; ++i)
    {
        ResizeCtrls[i].Pos.x = MulDiv(ResizeCtrls[i].Pos.x, numeratorX, denominatorX);
        ResizeCtrls[i].Pos.y = MulDiv(ResizeCtrls[i].Pos.y, numeratorY, denominatorY);
    }
}

BOOL CALLBACK
CElasticLayout::FindMoveControls(HWND hChild, LPARAM lParam)
{
    CElasticLayout* el = (CElasticLayout*)lParam;

    // if the element is below SplitY, add it to the list of elements to be moved
    RECT r;
    GetWindowRect(hChild, &r);
    POINT p = {r.left, r.top};
    ScreenToClient(el->HWindow, &p);
    if (p.y >= el->SplitY)
    {
        CElasticLayoutCtrl mc;
        mc.HCtrl = hChild;
        mc.Pos = p;
        el->MoveCtrls.Add(mc);
    }

    return TRUE;
}

void CElasticLayout::FindMoveCtrls()
{
    EnumChildWindows(HWindow, FindMoveControls, (LPARAM)this);

    // find the envelope of all 'move' elements
    RECT rEnvelope = {0};
    for (int i = 0; i < MoveCtrls.Count; i++)
    {
        HWND hCtrl = MoveCtrls[i].HCtrl;

        RECT r;
        GetWindowRect(hCtrl, &r);
        if (r.left < rEnvelope.left)
            rEnvelope.left = r.left;
        if (r.top < rEnvelope.top)
            rEnvelope.top = r.top;
        if (r.right > rEnvelope.right)
            rEnvelope.right = r.right;
        if (r.bottom > rEnvelope.bottom)
            rEnvelope.bottom = r.bottom;
    }
    POINT p = {rEnvelope.right, rEnvelope.bottom};
    ScreenToClient(HWindow, &p);
    int envelopeBottom = p.y;
    // coordinates 'MoveCtrlsY' will be relative to the bottom edge of the envelope
    for (int i = 0; i < MoveCtrls.Count; i++)
        MoveCtrls[i].Pos.y = envelopeBottom - MoveCtrls[i].Pos.y;

    // for ResizeCtrls elements store their distance from the bottom edge to the bottom edge of the envelope
    for (int i = 0; i < ResizeCtrls.Count; i++)
    {
        if (ResizeCtrls[i].Pos.y == 0)
        {
            RECT r;
            GetWindowRect(ResizeCtrls[i].HCtrl, &r);
            ResizeCtrls[i].Pos.y = max(0, rEnvelope.bottom - r.bottom);
        }
    }
}

void CElasticLayout::LayoutCtrls()
{
    if (ResizeCtrls.Count == 0)
    {
        TRACE_E("No controls to layout!");
        return;
    }
    RECT cR;
    GetClientRect(HWindow, &cR);

    FindMoveCtrls();

    HDWP hdwp = HANDLES(BeginDeferWindowPos(ResizeCtrls.Count + MoveCtrls.Count));
    if (hdwp != NULL)
    {
        for (int i = 0; i < ResizeCtrls.Count; i++)
        {
            HWND hCtrl = ResizeCtrls[i].HCtrl;
            RECT r;
            GetWindowRect(hCtrl, &r);
            POINT p = {r.left, r.top};
            ScreenToClient(HWindow, &p);
            HANDLES(DeferWindowPos(hdwp, hCtrl, NULL,
                                   0, 0,
                                   ResizeCtrls[i].Pos.x, cR.bottom - p.y - ResizeCtrls[i].Pos.y,
                                   SWP_NOMOVE | SWP_NOZORDER));
        }
        for (int i = 0; i < MoveCtrls.Count; i++)
        {
            HWND hCtrl = MoveCtrls[i].HCtrl;
            HANDLES(DeferWindowPos(hdwp, hCtrl, NULL,
                                   MoveCtrls[i].Pos.x, cR.bottom - MoveCtrls[i].Pos.y,
                                   0, 0,
                                   SWP_NOSIZE | SWP_NOZORDER));
        }
        HANDLES(EndDeferWindowPos(hdwp));
    }
    MoveCtrls.DestroyMembers();
}

//
// ****************************************************************************
// CPropSheetPage
//

CPropSheetPage::CPropSheetPage(const wchar_t* title, HINSTANCE modul, int resID,
                               DWORD flags, HICON icon, CObjectOrigin origin)
    : CDialog(modul, resID, NULL, origin)
{
    Init(title, modul, resID, icon, flags, origin);
}

CPropSheetPage::CPropSheetPage(const wchar_t* title, HINSTANCE modul, int resID, UINT helpID,
                               DWORD flags, HICON icon, CObjectOrigin origin)
    : CDialog(modul, resID, helpID, NULL, origin)
{
    Init(title, modul, resID, icon, flags, origin);
}

void CPropSheetPage::Init(const wchar_t* title, HINSTANCE modul, int resID,
                          HICON icon, DWORD flags, CObjectOrigin origin)
{
    Title = NULL;
    if (title != NULL)
    {
        int len = (int)wcslen(title);
        Title = new wchar_t[len + 1];
        if (Title != NULL)
            wcscpy_s(Title, len + 1, title);
        else
            TRACE_ET(L"Low memory!");
    }
    Flags = flags;
    Icon = icon;

    ParentDialog = NULL; // set from CPropertyDialog::Execute()
    ParentPage = NULL;
    HTreeItem = NULL;
    Expanded = NULL;
    ElasticLayout = NULL;
    DarkModeGroupBoxThemeEnabled = FALSE;
}

CPropSheetPage::~CPropSheetPage()
{
    if (Title != NULL)
        delete[] Title;
    if (ElasticLayout != NULL)
        delete ElasticLayout;
}

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
    PROPSHEETPAGE psp;
    psp.dwSize = sizeof(PROPSHEETPAGE);
    psp.dwFlags = Flags;
    psp.hInstance = Modul;
    psp.pszTemplate = MAKEINTRESOURCE(ResID);
    psp.hIcon = Icon;
    psp.pszTitle = Title;
    psp.pfnDlgProc = CPropSheetPage::CPropSheetPageProc;
    psp.lParam = (LPARAM)this;
    psp.pfnCallback = NULL;
    psp.pcRefParent = NULL;
    return CreatePropertySheetPage(&psp);
}

BOOL CPropSheetPage::ElasticVerticalLayout(int count, ...)
{
    if (ElasticLayout != NULL)
    {
        TRACE_E("ElasticLayout already set!");
        return FALSE;
    }
    ElasticLayout = new CElasticLayout(HWindow);
    va_list list;
    va_start(list, count);
    for (int arg = 0; arg < count; arg++)
        ElasticLayout->AddResizeCtrl(va_arg(list, int));
    va_end(list);
    return TRUE;
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
        if (ElasticLayout != NULL)
            ElasticLayout->LayoutCtrls();
#ifndef __TRACESERVER
        if (DarkModeGroupBoxThemeEnabled)
            DarkMode_ApplyGroupBoxThemeRecursive(HWindow);
#endif
        return TRUE; // I want focus from DefDlgProc
    }

    case WM_SIZE:
    {
        if (ElasticLayout != NULL)
            ElasticLayout->LayoutCtrls();
        break;
    }

    case WM_HELP:
    {
        if (WinLibHelp != NULL && HelpID != -1)
        {
            WinLibHelp->OnHelp(HWindow, HelpID, (HELPINFO*)lParam,
                               (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                               (GetKeyState(VK_SHIFT) & 0x8000) != 0);
            return TRUE;
        }
        break; // F1 let through to parent
    }

    case WM_CONTEXTMENU:
    {
        if (WinLibHelp != NULL)
            WinLibHelp->OnContextMenu((HWND)wParam, LOWORD(lParam), HIWORD(lParam));
        return TRUE;
    }

#ifndef __TRACESERVER
    case WM_ERASEBKGND:
    {
        DarkModeColors colors;
        if (DarkMode_GetColors(&colors))
        {
            RECT r;
            GetClientRect(HWindow, &r);
            FillRectWithColor((HDC)wParam, &r, colors.DialogBackground);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HBRUSH hBrush = DarkMode_GetDialogCtlColorBrush(uMsg, (HDC)wParam, (HWND)lParam);
        if (hBrush != NULL)
            return (INT_PTR)hBrush;
        break;
    }
#endif

    case WM_NOTIFY:
    {
        if (((NMHDR*)lParam)->code == PSN_KILLACTIVE) // page deactivation
        {
            if (ValidateData())
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE);
            else // do not allow page deactivation
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
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
        { // Apply Now or OK button pressed
            if (TransferData(ttDataFromWindow))
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, PSNRET_NOERROR);
            else
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, PSNRET_INVALID_NOCHANGEPAGE);
            return TRUE;
        }

        if (((NMHDR*)lParam)->code == PSN_WIZFINISH)
        { // Finish button pressed
            // PSN_KILLACTIVE did not arrive - perform validation
            if (!ValidateData())
            {
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }

            // iterate through all pages for transfer
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

#ifndef __TRACESERVER
    case WM_SETTINGCHANGE:
    {
        if (DarkMode_OnSettingChange(lParam))
        {
            DarkMode_ApplyTitleBar(HWindow);
            DarkMode_ApplyListTreeThemeRecursive(HWindow);
            if (DarkModeGroupBoxThemeEnabled)
                DarkMode_ApplyGroupBoxThemeRecursive(HWindow);
            InvalidateRect(HWindow, NULL, TRUE);
        }
        break;
    }

    case WM_THEMECHANGED:
    {
        DarkMode_ApplyTitleBar(HWindow);
        DarkMode_ApplyListTreeThemeRecursive(HWindow);
        if (DarkModeGroupBoxThemeEnabled)
            DarkMode_ApplyGroupBoxThemeRecursive(HWindow);
        InvalidateRect(HWindow, NULL, TRUE);
        break;
    }
#endif
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
    case WM_INITDIALOG: // first message - attach object to dialog
    {
        dlg = (CPropSheetPage*)((PROPSHEETPAGE*)lParam)->lParam;
        if (dlg == NULL)
        {
            TRACE_ET(L"Unable to create dialog.");
            return TRUE;
        }
        else
        {
            dlg->HWindow = hwndDlg;
            dlg->Parent = ::GetParent(hwndDlg);
            //--- insert window according to hwndDlg into the list of windows
            if (!WindowsManager.AddWindow(hwndDlg, dlg)) // error
            {
                TRACE_ET(L"Unable to create dialog.");
                return TRUE;
            }
            dlg->NotifDlgJustCreated(); // introduced as a place to modify dialog layout
        }
        break;
    }

    case WM_DESTROY: // last message - detach object from dialog
    {
        dlg = (CPropSheetPage*)WindowsManager.GetWindowPtr(hwndDlg);
        INT_PTR ret = FALSE; // in case it is not processed
        if (dlg != NULL && dlg->Is(otDialog))
        {
            // Petr: I moved down below wnd->WindowProc() so that during WM_DESTROY
            //       messages still arrive (Lukas needed it)
            // WindowsManager.DetachWindow(hwndDlg);

            ret = dlg->DialogProc(uMsg, wParam, lParam);

            WindowsManager.DetachWindow(hwndDlg);
            if (dlg->IsAllocated())
                delete dlg;
            else
                dlg->HWindow = NULL; // information about detachment
        }
        return ret;
    }

    default:
    {
        dlg = (CPropSheetPage*)WindowsManager.GetWindowPtr(hwndDlg);
#ifdef __DEBUG_WINLIB
        if (dlg != NULL && !dlg->Is(otPropSheetPage))
        {
            TRACE_CT(L"This should never happen.");
            dlg = NULL;
        }
#endif
    }
    }
    //--- call DialogProc(...) method of the corresponding dialog object
    INT_PTR dlgRes;
    if (dlg != NULL)
        dlgRes = dlg->DialogProc(uMsg, wParam, lParam);
    else
        dlgRes = FALSE; // error or message did not arrive between WM_INITDIALOG and WM_DESTROY

#ifndef __TRACESERVER
    if (dlg != NULL && uMsg == WM_INITDIALOG)
        DarkMode_ApplyListTreeThemeRecursive(hwndDlg);
#endif

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
        PROPSHEETHEADER psh;
        psh.dwSize = sizeof(PROPSHEETHEADER);
        psh.dwFlags = Flags;
        psh.hwndParent = Parent;
        psh.hInstance = Modul;
        psh.hIcon = Icon;
        psh.pszCaption = Caption;
        psh.nPages = Count;
        if (StartPage < 0 || StartPage >= Count)
            StartPage = 0;
        psh.nStartPage = StartPage;
        HPROPSHEETPAGE* pages = new HPROPSHEETPAGE[Count];
        if (pages == NULL)
        {
            TRACE_ET(L"Low memory!");
            return -1;
        }
        psh.phpage = pages;
        for (int i = 0; i < Count; i++)
        {
            psh.phpage[i] = At(i)->CreatePropSheetPage();
            At(i)->ParentDialog = this;
        }
        psh.pfnCallback = Callback;
        INT_PTR ret = PropertySheet(&psh);
        delete pages;
        return ret;
    }
    else
    {
        TRACE_ET(L"Incorrect call to CPropertyDialog::Execute.");
        return -1;
    }
}

int CPropertyDialog::GetCurSel()
{
    HWND tabCtrl = PropSheet_GetTabControl(HWindow);
    return TabCtrl_GetCurSel(tabCtrl);
}

//
// ****************************************************************************
// CTreePropDialog
//

#define _TPD_IDC_TREE 1
#define _TPD_IDC_HELP 9
#define _TPD_IDC_GRIP 10
#define _TPD_IDC_SEP 11
#define _TPD_IDC_CAPTION 3
#define _TPD_IDC_RECT 4
#define _TPD_IDC_OK 5
#define _TPD_IDC_APPLY 6
// dimensions in dlg units
#define _TPD_LEFTMARGIN 4  // indentation of TreeView and Caption from left edge
#define _TPD_TOPMARGIN 4   // indentation of TreeView and Caption from top edge
#define _TPD_TREE_W 100    // width of TreeView
#define _TPD_CAPTION_H 16  // height of caption
#define _TPD_BUTTON_W 50   // width of buttons
#define _TPD_BUTTON_H 14   // height of buttons
#define _TPD_BUTTON_MARG 4 // spacing between buttons

CTPHCaptionWindow::CTPHCaptionWindow(HWND hDlg, int ctrlID)
    : CWindow(hDlg, ctrlID, ooAllocated)
{
    Allocated = 0;
    Text = NULL;
}

CTPHCaptionWindow::~CTPHCaptionWindow()
{
    if (Text != NULL)
        free(Text);
}

void CTPHCaptionWindow::SetText(const WCHAR* text)
{
    int l = (int)wcslen(text);
    if (Allocated < l + 2)
    {
        if (Text != NULL)
            free(Text);
        Text = (WCHAR*)malloc((l + 2) * sizeof(WCHAR));
        if (Text == NULL)
        {
            TRACE_ET(L"Low memory!");
            Allocated = 0;
            return;
        }
        Allocated = l + 2;
    }
    wcscpy_s(Text, Allocated, text);
    InvalidateRect(HWindow, NULL, TRUE);
    UpdateWindow(HWindow);
}

LRESULT
CTPHCaptionWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(HWindow, &ps);

        RECT r;
        GetClientRect(HWindow, &r);

        int devCaps = GetDeviceCaps(hdc, NUMCOLORS);
        COLORREF darkDialogBackground = 0;
        COLORREF darkDialogText = 0;
        BOOL useDark = FALSE;
#ifndef __TRACESERVER
        DarkModeColors colors;
        useDark = DarkMode_GetColors(&colors);
        darkDialogBackground = colors.DialogBackground;
        darkDialogText = colors.DialogText;
#endif
        if (useDark)
            FillRectWithColor(hdc, &r, darkDialogBackground);
        else if (devCaps == -1) // use gradient only with more than 256 colors
        {
            HBRUSH hOldBrush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
#define TPH_STEPS 100
            double stepW = (double)(r.right - r.left + 1) / TPH_STEPS;
            RECT r2 = r;
            r2.right = (long)(r2.left + stepW + 1);
            COLORREF base = GetSysColor(COLOR_BTNFACE);
            for (int i = 0; i <= TPH_STEPS; i++)
            {

                LOGBRUSH lb;
                lb.lbStyle = BS_SOLID;
                lb.lbColor = RGB(max(GetRValue(base) - TPH_STEPS / 2 + i / 2 + 1, 0),
                                 max(GetGValue(base) - TPH_STEPS / 2 + i / 2 + 1, 0),
                                 max(GetBValue(base) - TPH_STEPS / 2 + i / 2 + 1, 0));
                HBRUSH hColorBrush = CreateBrushIndirect(&lb);
                /*
        HBRUSH hColorBrush = CreateSolidBrush(RGB(max(GetRValue(base) - TPH_STEPS / 2 + i / 2 + 1, 0),
        max(GetGValue(base) - TPH_STEPS / 2 + i / 2 + 1, 0),
        max(GetBValue(base) - TPH_STEPS / 2 + i / 2 + 1, 0)));
        */
                FillRect(hdc, &r2, hColorBrush);
                DeleteObject(hColorBrush);
                r2.left = (long)(i * stepW);
                r2.right = (long)(r2.left + stepW + 1);
            }
        }
        else
            FillRect(hdc, &r, (HBRUSH)(COLOR_GRAYTEXT + 1));

        if (Text != NULL)
        {
            int oldBkMode = SetBkMode(hdc, TRANSPARENT);

            HFONT hFont = NULL;
            HFONT hOldFont = NULL;
            LOGFONT srcLF;
            HFONT hSrcFont = reinterpret_cast<HFONT>(SendMessageW(GetParent(HWindow), WM_GETFONT, 0, 0));
            if (hSrcFont == NULL)
                hSrcFont = (HFONT)HANDLES(GetStockObject(DEFAULT_GUI_FONT));
            GetObject(hSrcFont, sizeof(srcLF), &srcLF);
            srcLF.lfHeight = (int)(srcLF.lfHeight * 1.2);
            // srcLF.lfWeight = FW_BOLD; // on Vista bold looks pretty ugly and unreadable
            hFont = CreateFontIndirect(&srcLF);
            hOldFont = (HFONT)SelectObject(hdc, hFont);

            int oldColor;
            if (useDark)
                oldColor = SetTextColor(hdc, darkDialogText);
            else if (devCaps == -1)
                oldColor = SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
            else
                oldColor = SetTextColor(hdc, GetSysColor(COLOR_CAPTIONTEXT));
            r.left += 8;
            DrawTextW(hdc, Text, (int)wcslen(Text), &r, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
            SetTextColor(hdc, oldColor);
            SelectObject(hdc, hOldFont);
            SetBkMode(hdc, oldBkMode);

            if (hFont != NULL)
                DeleteObject(hFont);
        }
        EndPaint(HWindow, &ps);
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

LRESULT
CTPHGripWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_SETCURSOR:
    {
        // we want only north-south cursor
        SetCursor(LoadCursor(NULL, IDC_SIZENS));
        return TRUE;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

CTreePropHolderDlg::CTreePropHolderDlg(HWND hParent, DWORD* windowHeight)
    : CDialog(NULL, 0, hParent, ooStatic)
{
    HTreeView = NULL;
    ChildDialog = NULL;
    CaptionWindow = NULL;
    GripWindow = NULL;
    CurrentPageIndex = -1;
    ExitButton = -1;
    MinWindowSize.cx = 0;
    MinWindowSize.cy = 0;
    WindowHeight = windowHeight;
}

CTreePropHolderDlg::~CTreePropHolderDlg()
{
    for (HFONT font : ReflowFonts)
        HANDLES(DeleteObject(font));
}

INT_PTR
CTreePropHolderDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (TPD != NULL && TPD->HandleDialogMessage(uMsg, wParam, lParam))
        return TRUE;
    // WM_INITDIALOG will be called only when we know the window dimensions
    if (TPD != NULL && uMsg != WM_INITDIALOG)
        TPD->DialogProc(uMsg, wParam, lParam); // forward messages
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        HWND hwnd = GetDlgItem(HWindow, _TPD_IDC_RECT);
        GetWindowRect(hwnd, &ChildDialogRect);
        POINT p;
        p.x = ChildDialogRect.left;
        p.y = ChildDialogRect.top;
        ScreenToClient(HWindow, &p);
        int w = ChildDialogRect.right - ChildDialogRect.left;
        int h = ChildDialogRect.bottom - ChildDialogRect.top;
        ChildDialogRect.left = p.x;
        ChildDialogRect.top = p.y;
        ChildDialogRect.right = p.x + w;
        ChildDialogRect.bottom = p.y + h;
        NaturalPageSize = {w, h};
        MaximumPageSize = NaturalPageSize;
        PageSizes.resize(TPD->Count);
        if (TPD->ScrollablePages())
        {
            PageViewport = hwnd;
            SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) |
                WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
            new CTPHPageViewport(HWindow, _TPD_IDC_RECT, this);
        }
        else
            DestroyWindow(hwnd);
        HTreeView = GetDlgItem(HWindow, _TPD_IDC_TREE);
        BOOL appIsThemed = IsAppThemed();
        if (appIsThemed)
            SetWindowTheme(HTreeView, L"explorer", NULL);
#ifndef __TRACESERVER
        DarkMode_ApplyListTreeThemeRecursive(HWindow);
#endif

        int treeIndent = 0;
        if (appIsThemed)
        {
            RECT rect = {0, 0, 4, 8};
            MapDialogRect(HWindow, &rect); // get baseUnitX and baseUnitY to convert dlg-units to pixels
            treeIndent = MulDiv(9 /* indent in dlg-units */, rect.right /* baseUnitX */, 4);
            TreeView_SetIndent(HTreeView, treeIndent);
        }

        // dlg units -> pixels conversions
        RECT r = {_TPD_BUTTON_W, _TPD_BUTTON_H, _TPD_LEFTMARGIN, _TPD_TOPMARGIN};
        MapDialogRect(HWindow, &r);
        ButtonSize.cx = r.left;
        ButtonSize.cy = r.top;
        MarginSize.cx = r.right;
        MarginSize.cy = r.bottom;
        r = {_TPD_CAPTION_H, _TPD_BUTTON_MARG, 0, 0};
        MapDialogRect(HWindow, &r);
        CaptionHeight = r.left;
        ButtonMargin = r.top;

        CaptionWindow = new CTPHCaptionWindow(HWindow, _TPD_IDC_CAPTION);
        if (CaptionWindow == NULL)
            TRACE_ET(L"Low memory!");
        TreeWidth = BuildAndMeasureTree() + 2 * treeIndent + treeIndent / 2 + GetSystemMetrics(SM_CXVSCROLL);
        if (TPD->StartPage < 0 || TPD->StartPage >= TPD->Count)
            TPD->StartPage = 0;
        TreeView_SelectItem(HTreeView, TPD->At(TPD->StartPage)->HTreeItem);

        GripWindow = new CTPHGripWindow(HWindow, _TPD_IDC_GRIP);

        // default dimensions are minimal - save them so we can check them later
        GetWindowRect(HWindow, &r);
        RECT cR;
        GetClientRect(HWindow, &cR);
        int marginW = (r.right - r.left) - cR.right;
        int marginH = (r.bottom - r.top) - cR.bottom;
        MinWindowSize.cx = TreeWidth + ChildDialogRect.right - ChildDialogRect.left + 3 * MarginSize.cx + marginW;
        MinWindowSize.cy = MarginSize.cy + CaptionHeight + MarginSize.cy +
                           ChildDialogRect.bottom - ChildDialogRect.top +
                           MarginSize.cy + 1 + MarginSize.cy +
                           ButtonSize.cy + MarginSize.cy + marginH;

        // set user window size and perform element layout
        int height = (int)*WindowHeight;
        RECT clipR; // we do not want to be larger than screen height
        MultiMonGetClipRectByWindow(HWindow, &clipR, NULL);
        if (TPD->ScrollablePages())
        {
            MinWindowSize.cx = (std::min)(MinWindowSize.cx, clipR.right - clipR.left);
            MinWindowSize.cy = (std::min)(MinWindowSize.cy, clipR.bottom - clipR.top);
        }
        if (height > clipR.bottom - clipR.top)
            height = clipR.bottom - clipR.top;
        if (height < MinWindowSize.cy)
            height = MinWindowSize.cy;
        SetWindowPos(HWindow, NULL, 0, 0, MinWindowSize.cx, height,
                     SWP_NOZORDER | SWP_NOMOVE);

        LayoutControls();
        TreeView_EnsureVisible(HTreeView, TPD->At(TPD->StartPage)->HTreeItem);

        TPD->DialogProc(uMsg, wParam, lParam); // forward messages

        break;
    }

    case WM_HELP:
    {
        if (WinLibHelp != NULL && ChildDialog != NULL && ChildDialog->HelpID != -1)
        {
            WinLibHelp->OnHelp(HWindow, ChildDialog->HelpID, (HELPINFO*)lParam,
                               (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                               (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        }
        return TRUE; // F1 do not let through to parent even if we do not call WinLibHelp->OnHelp()
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case _TPD_IDC_HELP:
        {
            if (WinLibHelp != NULL && ChildDialog != NULL && ChildDialog->HelpID != -1)
            {
                HELPINFO hi;
                memset(&hi, 0, sizeof(hi));
                hi.cbSize = sizeof(hi);
                hi.iContextType = HELPINFO_WINDOW;
                hi.dwContextId = ChildDialog->HelpID;
                GetCursorPos(&hi.MousePos);
                WinLibHelp->OnHelp(HWindow, ChildDialog->HelpID, &hi, FALSE, FALSE);
            }
            else
                TRACE_ET(L"CTreePropHolderDlg::DialogProc(): ignoring _TPD_IDC_HELP: SetupWinLibHelp() was not called or ChildDialog is NULL or ChildDialog->HelpID is -1!");
            return TRUE;
        }

        case _TPD_IDC_OK:
        case _TPD_IDC_APPLY:
        {
            // Validate every created page before transferring any edited data.
            // A previously visited page can still contain an invalid edit.
            for (int i = 0; i < TPD->Count; i++)
                if (TPD->At(i)->HWindow != NULL && !TPD->At(i)->ValidateData())
                {
                    HWND invalidControl = GetFocus();
                    TreeView_SelectItem(HTreeView, TPD->At(i)->HTreeItem);
                    if (IsChild(TPD->At(i)->HWindow, invalidControl)) SetFocus(invalidControl);
                    return TRUE;
                }
            for (int i = 0; i < TPD->Count; i++)
                if (TPD->At(i)->HWindow != NULL)
                    if (!TPD->At(i)->TransferData(ttDataFromWindow))
                        return TRUE;
            const bool save = LOWORD(wParam) == _TPD_IDC_OK;
            if (!TPD->AfterTransfer(save) || !save)
                return TRUE;
            ExitButton = IDOK;
            PostMessageW(HWindow, WM_NULL, 0, 0); // wake a GetMessage dispatching a sent command
            return TRUE;
        }
        case IDCANCEL:
        {
            ExitButton = LOWORD(wParam);
            PostMessageW(HWindow, WM_NULL, 0, 0);
            return TRUE;
        }
        }

        // forward message so that Enter works on default button
        if (ChildDialog != NULL && HIWORD(wParam) == BN_CLICKED)
            ::SendMessage(ChildDialog->HWindow, uMsg, wParam, lParam);

        break;
    }

    case WM_NOTIFY:
    {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->hwndFrom == HTreeView)
        {
            switch (pnmh->code)
            {
            case TVN_SELCHANGING:
            {
                LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
                CPropSheetPage* page = (CPropSheetPage*)pnmtv->itemOld.lParam;
                if (page != NULL && page->HWindow != NULL)
                {
                    NMHDR nmhdr;
                    nmhdr.hwndFrom = HWindow;
                    nmhdr.idFrom = _TPD_IDC_TREE;
                    nmhdr.code = PSN_KILLACTIVE;
                    SendMessage(page->HWindow, WM_NOTIFY, _TPD_IDC_TREE, (LPARAM)&nmhdr);
                    LONG_PTR res = GetWindowLongPtr(page->HWindow, DWLP_MSGRESULT);
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, res);
                    return TRUE;
                }
                break;
            }

            case TVN_SELCHANGED:
            {
                LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
                CPropSheetPage* page = (CPropSheetPage*)pnmtv->itemNew.lParam;
                if (page != NULL)
                {
                    for (int i = 0; i < TPD->Count; i++)
                        if (TPD->At(i) == page)
                        {
                            SelectPage(i);
                            NMHDR nmhdr;
                            nmhdr.hwndFrom = HWindow;
                            nmhdr.idFrom = _TPD_IDC_TREE;
                            nmhdr.code = PSN_SETACTIVE;
                            SendMessage(page->HWindow, WM_NOTIFY, _TPD_IDC_TREE, (LPARAM)&nmhdr);
                            break;
                        }
                }
                break;
            }

            case TVN_ITEMEXPANDED:
            {
                LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
                CPropSheetPage* page = (CPropSheetPage*)pnmtv->itemNew.lParam;
                if (page != NULL && page->Expanded != NULL)
                    *page->Expanded = (pnmtv->itemNew.state & TVIS_EXPANDED) != 0;
                break;
            }
            }
        }
        break;
    }

    case WM_NCHITTEST:
    {
        // Resize only in vertical direction
        LRESULT ht = DefWindowProcW(HWindow, uMsg, wParam, lParam);
        switch (ht)
        {
        case HTBOTTOMLEFT:
            ht = HTBOTTOM;
            break;
        case HTBOTTOMRIGHT:
            ht = HTBOTTOM;
            break;
        case HTTOPLEFT:
            ht = HTTOP;
            break;
        case HTTOPRIGHT:
            ht = HTTOP;
            break;
        case HTLEFT:
            ht = HTBORDER;
            break;
        case HTRIGHT:
            ht = HTBORDER;
            break;
        }
        SetWindowLongPtr(HWindow, DWLP_MSGRESULT, ht);
        return TRUE;
    }

    case WM_GETMINMAXINFO:
    {
        // Resize only in vertical direction
        LPMINMAXINFO lpmmi = (LPMINMAXINFO)lParam;
        lpmmi->ptMinTrackSize.x = MinWindowSize.cx;
        lpmmi->ptMaxTrackSize.x = MinWindowSize.cx;
        lpmmi->ptMinTrackSize.y = MinWindowSize.cy;

        // Adjust the height: https://blogs.msdn.microsoft.com/oldnewthing/20150504-00/?p=44944
        // FIXME
        //MONITORINFO mi = { sizeof(mi) };
        //GetMonitorInfo(MonitorFromWindow(HWindow,
        //               MONITOR_DEFAULTTOPRIMARY), &mi);
        //lpmmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top -
        //                     lpmmi->ptMaxPosition.y + rc.bottom;
        break;
    }

    case WM_SIZE:
    {
        RECT r;
        GetWindowRect(HWindow, &r);
        *WindowHeight = r.bottom - r.top;
        LayoutControls();
        break;
    }

    case WM_SYSCOLORCHANGE:
    {
#ifndef __TRACESERVER
        DarkMode_ApplyListTreeThemeRecursive(HWindow);
#endif
        break;
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

CTPHPageViewport::CTPHPageViewport(HWND parent, int id, CTreePropHolderDlg* holder)
    : CWindow(parent, id, ooAllocated), Holder(holder)
{
}

LRESULT CTPHPageViewport::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_VSCROLL || message == WM_HSCROLL)
    {
        Holder->ScrollPage(message == WM_VSCROLL ? SB_VERT : SB_HORZ, LOWORD(wParam));
        return 0;
    }
    if (message == WM_MOUSEWHEEL)
    {
        WheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
        while (WheelRemainder >= WHEEL_DELTA || WheelRemainder <= -WHEEL_DELTA)
        {
            const bool up = WheelRemainder > 0;
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines == WHEEL_PAGESCROLL)
                Holder->ScrollPage(SB_VERT, up ? SB_PAGEUP : SB_PAGEDOWN);
            else
                for (UINT i = 0; i < lines; ++i) Holder->ScrollPage(SB_VERT, up ? SB_LINEUP : SB_LINEDOWN);
            WheelRemainder += up ? -WHEEL_DELTA : WHEEL_DELTA;
        }
        return 0;
    }
    return CWindow::WindowProc(message, wParam, lParam);
}

void CTreePropHolderDlg::LayoutPageViewport()
{
    if (PageViewport == NULL || ChildDialog == NULL || ChildDialog->HWindow == NULL)
        return;
    RECT client;
    // Each scrollbar can make the other necessary; recompute after the client
    // area changes instead of assuming a particular font or monitor DPI.
    for (int pass = 0; pass != 3; ++pass)
    {
        GetClientRect(PageViewport, &client);
        SCROLLINFO horizontal = {sizeof(horizontal), SIF_RANGE | SIF_PAGE};
        horizontal.nMax = (std::max)(0L, NaturalPageSize.cx - 1);
        horizontal.nPage = (std::max)(0L, client.right);
        SetScrollInfo(PageViewport, SB_HORZ, &horizontal, TRUE);
        SCROLLINFO vertical = {sizeof(vertical), SIF_RANGE | SIF_PAGE};
        vertical.nMax = (std::max)(0L, NaturalPageSize.cy - 1);
        vertical.nPage = (std::max)(0L, client.bottom);
        SetScrollInfo(PageViewport, SB_VERT, &vertical, TRUE);
    }
    GetClientRect(PageViewport, &client);
    SetWindowPos(ChildDialog->HWindow, NULL,
                  -GetScrollPos(PageViewport, SB_HORZ), -GetScrollPos(PageViewport, SB_VERT),
                  (std::max)(NaturalPageSize.cx, client.right), (std::max)(NaturalPageSize.cy, client.bottom),
                  SWP_NOZORDER | SWP_NOACTIVATE);
}

void CTreePropHolderDlg::ScrollPage(int bar, int command)
{
    if (PageViewport == NULL)
        return;
    SCROLLINFO info = {sizeof(info), SIF_ALL};
    if (!GetScrollInfo(PageViewport, bar, &info))
        return;
    RECT line = {0, 0, 0, 10};
    MapDialogRect(HWindow, &line);
    const int step = (std::max)(1L, line.bottom);
    int position = info.nPos;
    switch (command)
    {
    case SB_LINEUP: position -= step; break;
    case SB_LINEDOWN: position += step; break;
    case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
    case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
    case SB_TOP: position = info.nMin; break;
    case SB_BOTTOM: position = info.nMax; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: position = info.nTrackPos; break;
    default: return;
    }
    SetScrollPos(PageViewport, bar, position, TRUE);
    LayoutPageViewport();
}

void CTreePropHolderDlg::EnsurePageFocusVisible()
{
    if (PageViewport == NULL || ChildDialog == NULL || ChildDialog->HWindow == NULL)
        return;
    HWND focus = GetFocus();
    if (focus == LastPageFocus)
        return;
    LastPageFocus = focus;
    if (focus == NULL || !IsChild(ChildDialog->HWindow, focus))
        return;
    RECT control, client;
    GetWindowRect(focus, &control);
    MapWindowPoints(NULL, PageViewport, reinterpret_cast<POINT*>(&control), 2);
    GetClientRect(PageViewport, &client);
    const int dx = control.left < 0 ? control.left : control.right > client.right ? control.right - client.right : 0;
    const int dy = control.top < 0 ? control.top : control.bottom > client.bottom ? control.bottom - client.bottom : 0;
    if (dx != 0) SetScrollPos(PageViewport, SB_HORZ, GetScrollPos(PageViewport, SB_HORZ) + dx, TRUE);
    if (dy != 0) SetScrollPos(PageViewport, SB_VERT, GetScrollPos(PageViewport, SB_VERT) + dy, TRUE);
    if (dx != 0 || dy != 0) LayoutPageViewport();
}

namespace
{
    LRESULT CALLBACK LiveDialogFontProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                        UINT_PTR id, DWORD_PTR font)
    {
        if (message == WM_GETFONT)
            return static_cast<LRESULT>(font);
        if (message == WM_SETFONT)
        {
            SetWindowSubclass(window, LiveDialogFontProc, id, static_cast<DWORD_PTR>(wParam));
            if (lParam != 0) InvalidateRect(window, NULL, TRUE);
            return 0; // USER32 continues to own only the original template font.
        }
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(window, LiveDialogFontProc, id);
        return DefSubclassProc(window, message, wParam, lParam);
    }

    struct FontReflowControl
    {
        HWND Window;
        LOGFONTW Font = {};
        RECT Bounds;
        bool DirectPageChild = false;
        std::vector<int> Columns;
    };
} // namespace

bool CTreePropHolderDlg::ReflowFont(const LOGFONTW& requested)
{
    HFONT font = HANDLES(CreateFontIndirectW(&requested));
    if (font == NULL)
        return false;
    HFONT previousFont = (HFONT)SendMessageW(HWindow, WM_GETFONT, 0, 0);
    LOGFONTW previousDescription = {};
    GetObjectW(previousFont, sizeof(previousDescription), &previousDescription);
    const SIZE oldUnits = Sally::Appearance::DialogFontUnits(HWindow, previousFont);
    const SIZE newUnits = Sally::Appearance::DialogFontUnits(HWindow, font);
    if (oldUnits.cx <= 0 || oldUnits.cy <= 0 || newUnits.cx <= 0 || newUnits.cy <= 0)
    {
        HANDLES(DeleteObject(font));
        return false;
    }
    ReflowFonts.push_back(font);
    std::vector<FontReflowControl> controls;
    EnumChildWindows(HWindow, [](HWND window, LPARAM context) -> BOOL
                     {
        auto& items = *reinterpret_cast<std::vector<FontReflowControl>*>(context);
        FontReflowControl item;
        item.Window = window;
        const HFONT controlFont = (HFONT)SendMessageW(window, WM_GETFONT, 0, 0);
        GetObjectW(controlFont, sizeof(item.Font), &item.Font);
        GetWindowRect(window, &item.Bounds);
        MapWindowPoints(NULL, ::GetParent(window), reinterpret_cast<POINT*>(&item.Bounds), 2);
        wchar_t className[32] = {};
        GetClassNameW(window, className, ARRAYSIZE(className));
        if (wcscmp(className, WC_LISTVIEWW) == 0)
        {
            const int count = Header_GetItemCount(ListView_GetHeader(window));
            for (int i = 0; i < count; ++i)
                item.Columns.push_back(ListView_GetColumnWidth(window, i));
        }
        items.push_back(std::move(item));
        return TRUE; }, reinterpret_cast<LPARAM>(&controls));
    for (auto& control : controls)
        for (int page = 0; page < TPD->Count; ++page)
            if (TPD->At(page)->HWindow != NULL && ::GetParent(control.Window) == TPD->At(page)->HWindow)
                control.DirectPageChild = true;

    // USER32 retains the font/base units from the creation template. Keep an
    // explicit live font for this container and the already-created pages;
    // resources for pages opened later use the newly selected template font.
    if (!SetWindowSubclass(HWindow, LiveDialogFontProc, 1, reinterpret_cast<DWORD_PTR>(font)))
        return false;
    for (int page = 0; page < TPD->Count; ++page)
    {
        HWND window = TPD->At(page)->HWindow;
        if (window != NULL && !SetWindowSubclass(window, LiveDialogFontProc, 1, reinterpret_cast<DWORD_PTR>(font)))
        {
            SetWindowSubclass(HWindow, LiveDialogFontProc, 1, reinterpret_cast<DWORD_PTR>(previousFont));
            for (int undo = 0; undo < page; ++undo)
                if (TPD->At(undo)->HWindow != NULL)
                    SetWindowSubclass(TPD->At(undo)->HWindow, LiveDialogFontProc, 1, reinterpret_cast<DWORD_PTR>(previousFont));
            return false;
        }
    }

    SendMessageW(HWindow, WM_SETREDRAW, FALSE, 0);
    SendMessageW(HWindow, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    for (const auto& control : controls)
    {
        HFONT controlFont = font;
        // Font previews keep their chosen face/style while adopting the dialog's
        // new size. Capture LOGFONTs before WM_SETFONT can retire owner-draw fonts.
        if (control.Font.lfFaceName[0] != 0 &&
            (wcscmp(control.Font.lfFaceName, previousDescription.lfFaceName) != 0 ||
             control.Font.lfWeight != previousDescription.lfWeight || control.Font.lfItalic != previousDescription.lfItalic))
        {
            LOGFONTW preview = control.Font;
            preview.lfHeight = requested.lfHeight;
            preview.lfWidth = 0;
            HFONT created = HANDLES(CreateFontIndirectW(&preview));
            if (created != NULL)
            {
                ReflowFonts.push_back(created);
                controlFont = created;
            }
        }
        SendMessageW(control.Window, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), FALSE);
        if (control.DirectPageChild)
            SetWindowPos(control.Window, NULL,
                         MulDiv(control.Bounds.left, newUnits.cx, oldUnits.cx), MulDiv(control.Bounds.top, newUnits.cy, oldUnits.cy),
                         MulDiv(control.Bounds.right - control.Bounds.left, newUnits.cx, oldUnits.cx),
                         MulDiv(control.Bounds.bottom - control.Bounds.top, newUnits.cy, oldUnits.cy), SWP_NOZORDER | SWP_NOACTIVATE);
        for (std::size_t column = 0; column < control.Columns.size(); ++column)
            ListView_SetColumnWidth(control.Window, static_cast<int>(column), MulDiv(control.Columns[column], newUnits.cx, oldUnits.cx));
    }
    const auto scaleSize = [&](SIZE& size)
    {
        size.cx = MulDiv(size.cx, newUnits.cx, oldUnits.cx);
        size.cy = MulDiv(size.cy, newUnits.cy, oldUnits.cy);
    };
    scaleSize(MaximumPageSize);
    scaleSize(NaturalPageSize);
    scaleSize(ButtonSize);
    scaleSize(MarginSize);
    TreeWidth = MulDiv(TreeWidth, newUnits.cx, oldUnits.cx);
    CaptionHeight = MulDiv(CaptionHeight, newUnits.cy, oldUnits.cy);
    ButtonMargin = MulDiv(ButtonMargin, newUnits.cx, oldUnits.cx);
    for (int page = 0; page < TPD->Count; ++page)
    {
        scaleSize(PageSizes[page]);
        auto* item = TPD->At(page);
        if (item->HWindow != NULL)
        {
            RECT client;
            GetClientRect(item->HWindow, &client);
            if (item->ElasticLayout != NULL)
                item->ElasticLayout->Scale(newUnits.cx, oldUnits.cx, newUnits.cy, oldUnits.cy);
            SetWindowPos(item->HWindow, NULL, 0, 0, MulDiv(client.right, newUnits.cx, oldUnits.cx),
                         MulDiv(client.bottom, newUnits.cy, oldUnits.cy), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    RECT outer, client, workArea;
    GetWindowRect(HWindow, &outer);
    GetClientRect(HWindow, &client);
    MultiMonGetClipRectByWindow(HWindow, &workArea, NULL);
    const int borderX = outer.right - outer.left - client.right;
    const int borderY = outer.bottom - outer.top - client.bottom;
    MinWindowSize.cx = (std::min)(TreeWidth + MaximumPageSize.cx + 3 * MarginSize.cx + borderX, workArea.right - workArea.left);
    MinWindowSize.cy = (std::min)(MaximumPageSize.cy + CaptionHeight + ButtonSize.cy + 6 * MarginSize.cy + borderY, workArea.bottom - workArea.top);
    const int height = (std::max)(MinWindowSize.cy, (std::min<LONG>)(MulDiv(client.bottom, newUnits.cy, oldUnits.cy) + borderY, workArea.bottom - workArea.top));
    SetWindowPos(HWindow, NULL, (std::max)(workArea.left, (std::min)(outer.left, workArea.right - MinWindowSize.cx)),
                 (std::max)(workArea.top, (std::min)(outer.top, workArea.bottom - height)), MinWindowSize.cx, height, SWP_NOZORDER | SWP_NOACTIVATE);
    LastPageFocus = NULL;
    LayoutControls();
    SendMessageW(HWindow, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(HWindow, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
    return true;
}

void CTreePropHolderDlg::LayoutControls()
{
    if (ChildDialog == NULL || ChildDialog->HWindow == NULL)
        return;

    RECT cRect;
    GetClientRect(HWindow, &cRect);

    GripSize.cx = GetSystemMetrics(SM_CXVSCROLL);
    GripSize.cy = GetSystemMetrics(SM_CYHSCROLL);

    const bool apply = TPD->ApplyButtonCaption() != NULL;
    const int buttonCount = apply ? 4 : 3;
    const auto fitted = sally::configuration::FitPropertyLayout(cRect.right, TreeWidth, buttonCount,
        ButtonSize.cx, ButtonSize.cy, ButtonMargin, MarginSize.cx, MarginSize.cy, GripSize.cx,
        Sally::Appearance::DialogFontUnits(HWindow, (HFONT)SendMessageW(HWindow, WM_GETFONT, 0, 0)).cy);
    const bool scrollable = TPD->ScrollablePages();
    const int navigationWidth = scrollable ? fitted.NavigationWidth : TreeWidth;
    const int buttonColumns = scrollable ? fitted.ButtonColumns : buttonCount;
    const int buttonWidth = scrollable ? fitted.ButtonWidth : ButtonSize.cx;
    const int buttonHeight = scrollable ? fitted.ButtonHeight : ButtonSize.cy;
    const int footerHeight = scrollable ? fitted.FooterHeight : 2 * MarginSize.cy + ButtonSize.cy;
    int sepY = cRect.bottom - footerHeight - 1;

    HDWP hdwp = HANDLES(BeginDeferWindowPos(8));
    if (hdwp != NULL)
    {
        // treeview; optional search controls share the navigation column.
        const int navigationHeader = scrollable ?
            sally::configuration::FitNavigationHeader(TPD->NavigationHeaderHeight(HWindow), sepY - 2 * MarginSize.cy) :
            TPD->NavigationHeaderHeight(HWindow);
        hdwp = HANDLES(DeferWindowPos(hdwp, HTreeView, NULL,
                                      MarginSize.cx,
                                      MarginSize.cy + navigationHeader,
                                      navigationWidth,
                                      (std::max)(0L, sepY - 2 * MarginSize.cy - navigationHeader),
                                      SWP_NOZORDER));
        // caption
        int captionX = MarginSize.cx + navigationWidth + MarginSize.cx;
        HWND hCaption = GetDlgItem(HWindow, _TPD_IDC_CAPTION);
        hdwp = HANDLES(DeferWindowPos(hdwp, hCaption, NULL,
                                      captionX,
                                      MarginSize.cy,
                                      cRect.right - MarginSize.cx - captionX,
                                      CaptionHeight,
                                      SWP_NOZORDER));
        // child dialog
        int dlgX = MarginSize.cx + navigationWidth + MarginSize.cx;
        int dlgY = MarginSize.cy + CaptionHeight + MarginSize.cy;
        ChildDialogRect.left = dlgX;
        ChildDialogRect.top = dlgY;
        ChildDialogRect.right = cRect.right - MarginSize.cx;
        ChildDialogRect.bottom = sepY - MarginSize.cy;
        hdwp = HANDLES(DeferWindowPos(hdwp, PageViewport != NULL ? PageViewport : ChildDialog->HWindow, NULL,
                                      ChildDialogRect.left,
                                      ChildDialogRect.top,
                                      ChildDialogRect.right - ChildDialogRect.left,
                                      ChildDialogRect.bottom - ChildDialogRect.top,
                                      SWP_NOZORDER));
        // separator
        HWND hSeparator = GetDlgItem(HWindow, _TPD_IDC_SEP);
        hdwp = HANDLES(DeferWindowPos(hdwp, hSeparator, NULL,
                                      MarginSize.cx,
                                      sepY,
                                      cRect.right - 2 * MarginSize.cx,
                                      1,
                                      SWP_NOZORDER));
        const int buttons[] = {_TPD_IDC_OK, _TPD_IDC_APPLY, IDCANCEL, _TPD_IDC_HELP};
        for (int index = 0; index < buttonCount; ++index)
        {
            const int row = index / buttonColumns;
            const int column = index % buttonColumns;
            const int rowCount = (std::min)(buttonColumns, buttonCount - row * buttonColumns);
            const int buttonsX = cRect.right - MarginSize.cx - GripSize.cx / 2 -
                                 rowCount * buttonWidth - (rowCount - 1) * ButtonMargin;
            const int id = buttons[apply || index == 0 ? index : index + 1];
            hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, id), NULL,
                                          buttonsX + column * (buttonWidth + ButtonMargin),
                                          sepY + 1 + MarginSize.cy + row * (buttonHeight + ButtonMargin),
                                          buttonWidth, buttonHeight, SWP_NOZORDER));
        }
        // Grip (resize)
        HWND hGrip = GetDlgItem(HWindow, _TPD_IDC_GRIP);
        hdwp = HANDLES(DeferWindowPos(hdwp, hGrip, NULL,
                                      cRect.right - GripSize.cx,
                                      cRect.bottom - GripSize.cy,
                                      GripSize.cx,
                                      GripSize.cy,
                                      SWP_NOZORDER));

        HANDLES(EndDeferWindowPos(hdwp));
        LayoutPageViewport();
        const RECT navigation = {MarginSize.cx, MarginSize.cy, MarginSize.cx + navigationWidth, sepY - MarginSize.cy};
        TPD->LayoutNavigation(navigation);
        // hack: there seems to be a bug in treeview/common controls: if a scrollbar appears due to content,
        // the selected item is not redrawn, so it is cropped on the right; it may be related to full row
        // select and still aero look; anyway redrawing under W7 does not flicker, we can probably afford it
        InvalidateRect(HTreeView, NULL, false);
    }
}

int CTreePropHolderDlg::BuildAndMeasureTree()
{
    int width = 0;
    for (int i = 0; i < TPD->Count; i++)
    {
        // Page titles have one UTF-16 owner and are inserted with the explicit wide message.
        TVINSERTSTRUCTW tvis;
        tvis.hParent = NULL;
        if (TPD->At(i)->ParentPage != NULL)
            tvis.hParent = TPD->At(i)->ParentPage->HTreeItem;
        tvis.hInsertAfter = TVI_LAST;
        tvis.item.mask = TVIF_TEXT | TVIF_STATE | TVIF_PARAM;
        tvis.item.pszText = TPD->At(i)->Title;
        tvis.item.cchTextMax = (int)wcslen(TPD->At(i)->Title);
        tvis.item.state = 0;
        // WARNING: expandable items here must be expanded, otherwise TreeView_GetItemRect() will return FALSE
        // and random data in RECT r rectangle
        if (TPD->At(i)->Expanded != NULL)
            tvis.item.state |= TVIS_EXPANDED;
        tvis.item.stateMask = tvis.item.state;
        tvis.item.lParam = (LPARAM)TPD->At(i);
        TPD->At(i)->HTreeItem = (HTREEITEM)SendMessageW(HTreeView, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
        RECT r;
        // Rather take the return value of TreeView_GetItemRect() into account
        if (TreeView_GetItemRect(HTreeView, TPD->At(i)->HTreeItem, &r, TRUE) && r.right - r.left > width)
            width = r.right - r.left;
    }
    // Now we can collapse unexpanded items
    for (int i = 0; i < TPD->Count; i++)
    {
        if (TPD->At(i)->Expanded != NULL && *TPD->At(i)->Expanded == FALSE)
            TreeView_Expand(HTreeView, TPD->At(i)->HTreeItem, TVE_COLLAPSE);
    }
    return width;
}

void CTreePropDialog::SetApplyButtonEnabled(bool enabled)
{
    const HWND apply = GetDlgItem(Dialog.HWindow, _TPD_IDC_APPLY);
    if (!enabled && GetFocus() == apply) SetFocus(GetDlgItem(Dialog.HWindow, _TPD_IDC_OK));
    EnableWindow(apply, enabled);
}

void CTreePropHolderDlg::EnableButtons()
{
}

BOOL CTreePropHolderDlg::SelectPage(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= TPD->Count)
        return FALSE;
    if (pageIndex == CurrentPageIndex)
    {
        ChildDialog = TPD->At(pageIndex);
        return ChildDialog->HWindow != NULL;
    }
    HWND HHideWindow = NULL;
    if (ChildDialog != NULL)
    {
        HHideWindow = ChildDialog->HWindow;
        ChildDialog = NULL;
    }

    if (pageIndex != CurrentPageIndex)
    {
        ChildDialog = TPD->At(pageIndex);
        if (ChildDialog->HWindow == NULL)
        {
            ChildDialog->SetParent(PageViewport != NULL ? PageViewport : HWindow);
            HRSRC resource = FindResourceW(TPD->Modul, MAKEINTRESOURCEW(ChildDialog->ResID), RT_DIALOG);
            HGLOBAL loaded = resource != NULL ? LoadResource(TPD->Modul, resource) : NULL;
            const void* source = loaded != NULL ? LockResource(loaded) : NULL;
            std::vector<BYTE> prepared;
            if (source != NULL && TPD->PrepareDialogTemplate(source, SizeofResource(TPD->Modul, resource), prepared))
                ChildDialog->CreateFromTemplate(reinterpret_cast<const DLGTEMPLATE*>(prepared.data()));
            else
                ChildDialog->Create();
            if (ChildDialog->HWindow == NULL)
                return FALSE;
            RECT natural;
            GetClientRect(ChildDialog->HWindow, &natural);
            if (TPD->ScrollablePages())
            {
                // Most resources share a common empty canvas. Scroll only to
                // real content, rather than into that unused bottom margin.
                int bottom = 0;
                for (HWND child = GetWindow(ChildDialog->HWindow, GW_CHILD); child != NULL; child = GetWindow(child, GW_HWNDNEXT))
                {
                    RECT bounds;
                    GetWindowRect(child, &bounds);
                    MapWindowPoints(NULL, ChildDialog->HWindow, reinterpret_cast<POINT*>(&bounds), 2);
                    bottom = (std::max)(bottom, static_cast<int>(bounds.bottom));
                }
                RECT margin = {0, 0, 0, 4};
                Sally::Appearance::MapCurrentDialogRect(ChildDialog->HWindow, margin);
                if (bottom > 0) natural.bottom = (std::min)(natural.bottom, bottom + margin.bottom);
            }
            PageSizes[pageIndex] = {natural.right, natural.bottom};
            TPD->PageCreated(pageIndex);
        }

        NMHDR nmhdr;
        nmhdr.hwndFrom = HWindow;
        nmhdr.idFrom = 0;
        nmhdr.code = PSN_SETACTIVE;
        SendMessage(ChildDialog->HWindow, WM_NOTIFY, 0, (LPARAM)&nmhdr);

        if (HHideWindow != NULL)
            SetWindowPos(HHideWindow, 0, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_HIDEWINDOW);

        CaptionWindow->SetText(ChildDialog->Title);

        if (PageViewport != NULL)
        {
            NaturalPageSize = PageSizes[pageIndex];
            SetScrollPos(PageViewport, SB_HORZ, 0, TRUE);
            SetScrollPos(PageViewport, SB_VERT, 0, TRUE);
            LayoutPageViewport();
            ShowWindow(ChildDialog->HWindow, SW_SHOW);
        }
        else
            SetWindowPos(ChildDialog->HWindow, HTreeView,
                         ChildDialogRect.left, ChildDialogRect.top,
                         ChildDialogRect.right - ChildDialogRect.left,
                         ChildDialogRect.bottom - ChildDialogRect.top,
                         SWP_SHOWWINDOW);
        CurrentPageIndex = pageIndex;
        EnableButtons();
    }
    return TRUE;
}

void CTreePropHolderDlg::OnCtrlTab(BOOL shift)
{
    int pageIndex = CurrentPageIndex;
    pageIndex += shift ? -1 : 1;
    if (pageIndex < 0)
        pageIndex = TPD->Count - 1;
    if (pageIndex >= TPD->Count)
        pageIndex = 0;
    if (pageIndex != CurrentPageIndex)
    {
        if (TreeView_SelectItem(HTreeView, TPD->At(pageIndex)->HTreeItem))
        {
            HWND hFocus = GetFocus();
            if (hFocus != HTreeView)
            {
                HWND hFirst = GetNextDlgTabItem(ChildDialog->HWindow, NULL, FALSE);
                if (hFirst != NULL)
                    SetFocus(hFirst);
                else
                    SetFocus(HTreeView);
            }
        }
    }
}

int CTreePropHolderDlg::ExecuteIndirect(LPCDLGTEMPLATE hDialogTemplate)
{
    HWND hOldFocus = GetFocus();
    EnableWindow(Parent, FALSE);
    CreateDialogIndirectParam(Modul, hDialogTemplate, Parent,
                              (DLGPROC)CDialog::CDialogProc, (LPARAM)this);
    MSG msg;
    while (ExitButton == -1 && GetMessageW(&msg, NULL, 0, 0))
    {
        if (TPD != NULL && TPD->PreTranslateMessage(msg))
            continue;
        CWindowsObject* wnd = WindowsManager.GetWindowPtr(GetActiveWindow());
        if ((msg.message == WM_KEYDOWN || msg.message == WM_KEYUP) &&
            msg.wParam == VK_TAB && (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            if (msg.message == WM_KEYDOWN)
                OnCtrlTab((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
        }
        else if (wnd == NULL || !wnd->Is(otDialog) || !IsDialogMessage(wnd->HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        EnsurePageFocusVisible();
    }
    EnableWindow(Parent, TRUE);
    DestroyWindow(HWindow);
    SetFocus(hOldFocus);
    return ExitButton;
}

WORD* CTreePropDialog::lpdwAlign(WORD* lpIn)
{
    DWORD_PTR ul;
    ul = (DWORD_PTR)lpIn;
    ul += 3;
    ul >>= 2;
    ul <<= 2;
    return (WORD*)ul;
}

int WinLibCopyText(WCHAR* buf, const wchar_t* text, int bufLen)
{
    lstrcpynW(buf, text, bufLen);
    return (int)wcslen(buf) + 1;
}

// Wide-source overload: the DLGTEMPLATE control-text field is always UTF-16, so
// a caller that already has the text wide (e.g. LoadStrW) can copy it straight in. Guarded out
// under UNICODE builds (e.g. tserver.vcxproj): there wchar_t==WCHAR, so the wchar_t-based overload
// above already takes its "lstrcpynW"/wide branch and this overload would be byte-identical to
// it - a genuine ODR collision, not just redundant.

int CTreePropDialog::AddItemEx(LPWORD& lpw, const wchar_t* className, WORD id, int x, int y, int cx, int cy,
                               UINT style, UINT exStyle, const WCHAR* text)
{
    lpw = lpdwAlign(lpw); // align DLGITEMTEMPLATEEX on DWORD boundary
    *(DWORD*)lpw = 0;     // helpID
    lpw += 2;
    *(DWORD*)lpw = exStyle; // exStyle
    lpw += 2;
    *(DWORD*)lpw = style; // style
    lpw += 2;
    *lpw++ = x;
    *lpw++ = y;
    *lpw++ = cx;
    *lpw++ = cy;
    *(DWORD*)lpw = id; // id
    lpw += 2;
    LPWSTR lpwsz = (LPWSTR)lpw;
    lpw += WinLibCopyText(lpwsz, className, 50);
    if (text == NULL)
        *lpw++ = 0; // no name
    else
    {
        lpwsz = (LPWSTR)lpw;
        lpw += WinLibCopyText(lpwsz, text, 50);
    }
    *lpw++ = 0; // no creation data
    return TRUE;
}

int CTreePropDialog::Execute(const WCHAR* buttonOK,
                             const WCHAR* buttonCancel,
                             const WCHAR* buttonHelp)
{
    if (Count > 0)
    {
        RECT maxPageRect;
        SetRectEmpty(&maxPageRect);

        // determine maximum dimensions
        for (int i = 0; i < Count; i++)
        {
            At(i)->ParentDialog = this;
            HRSRC hrsrc = FindResource(Modul, MAKEINTRESOURCE(At(i)->ResID), RT_DIALOG);
            if (hrsrc == NULL)
            {
                TRACE_ET(L"Unable to find resource for page number: " << i);
                return 0;
            }
            HGLOBAL hglb = LoadResource(Modul, hrsrc);
            WORD* pageTemplate = (WORD*)LockResource(hglb);
            if (pageTemplate == NULL)
            {
                TRACE_ET(L"Unable to find resource for page number: " << i);
                return 0;
            }
            IndexPageResource(i, pageTemplate, SizeofResource(Modul, hrsrc));
            BOOL dlgEx = *pageTemplate /*dlgVer*/ == 1 && *(pageTemplate + 1) /*signature*/ == 0xffff; // DLGEX
            if (!dlgEx)
                TRACE_CT(L"CTreePropDialog::Execute(): DLG is no longer supported! PageResID=" << At(i)->ResID);
            DWORD dlgStyle = 0;
            short dlgCX = 0;
            short dlgCY = 0;
            WCHAR* dlgTitle = NULL;

            dlgStyle = *(DWORD*)(pageTemplate + 6); // style
            dlgCX = *(short*)(pageTemplate + 11);   // cx
            dlgCY = *(short*)(pageTemplate + 12);   // cy
            WORD* t = pageTemplate + 13;            // menu, skip to dialog class, and then to its title
            if (*t == 0)
                t++; // no menu
            else
            {
                if (*t == 0xffff)
                    t += 2; // menu ID
                else
                    t += wcslen((wchar_t*)t) + 1; // menu string
            }
            if (*t == 0)
                t++; // no dialog class
            else
            {
                if (*t == 0xffff)
                    t += 2; // dialog class ID
                else
                    t += wcslen((wchar_t*)t) + 1; // dialog class string
            }
            dlgTitle = (WCHAR*)t;

            if (At(i)->Title == NULL)
            {
                int len = 1;
                if (dlgStyle & WS_CAPTION)
                {
                    len += (int)wcslen(dlgTitle) + 1;
                }
                At(i)->Title = new wchar_t[len];
                if (len > 1)
                {
                    lstrcpynW(At(i)->Title, dlgTitle, len);
                }
                At(i)->Title[len - 1] = 0;

            }

            if (dlgCX > maxPageRect.right)
                maxPageRect.right = dlgCX;
            if (dlgCY > maxPageRect.bottom)
                maxPageRect.bottom = dlgCY;
        }

        // height from bottom edge of dialog to bottom of TreeView and ChildDialog
        int lowMargin = 2 * _TPD_TOPMARGIN + _TPD_BUTTON_H + _TPD_TOPMARGIN + _TPD_TOPMARGIN / 2;
        SIZE dialogSize;
        dialogSize.cx = _TPD_LEFTMARGIN + _TPD_TREE_W + _TPD_LEFTMARGIN + maxPageRect.right +
                        _TPD_LEFTMARGIN;
        dialogSize.cy = _TPD_TOPMARGIN + _TPD_CAPTION_H + _TPD_TOPMARGIN + maxPageRect.bottom +
                        lowMargin;

        // build dialog template: DLG or DLGEX, according to page format, must be the same,
        // otherwise controls are cropped and fonts of pages and rest of tree property dialog differ

        HGLOBAL hgbl;

        LPWORD lpw;
        LPWSTR lpwsz;
        hgbl = GlobalAlloc(GMEM_ZEROINIT, 1024);
        if (!hgbl)
            return -1;
        lpw = (LPWORD)GlobalLock(hgbl); // Define a dialog box.
        *lpw++ = 1;
        *lpw++ = 0xffff;  // DLGEX
        *(DWORD*)lpw = 0; // helpID
        lpw += 2;
        *(DWORD*)lpw = 0; // exStyle
        lpw += 2;
        // style
        *(DWORD*)lpw = WS_VISIBLE | WS_POPUP | WS_BORDER | WS_SYSMENU | WS_CAPTION |
                       DS_SETFONT | DS_MODALFRAME | DS_CENTER | DS_FIXEDSYS | WS_SIZEBOX;
        lpw += 2;
        *lpw++ = ApplyButtonCaption() != NULL ? 9 : 8; // cDlgItems
        *lpw++ = 0; // x
        *lpw++ = 0; // y
        *lpw++ = 0; // cx
        *lpw++ = 0; // cy
        *lpw++ = 0; // no menu
        *lpw++ = 0; // predefined dialog box class (by default)
        lpwsz = (LPWSTR)lpw;
        lpw += WinLibCopyText(lpwsz, CaptionW, 100); // title
        *lpw++ = 8;                                 // font size
        *lpw++ = FW_NORMAL;                         // font weight
        *(BYTE*)lpw = FALSE;                        // is font italic?
        *((BYTE*)lpw + 1) = DEFAULT_CHARSET;        // font charset (any script, as the .rc dialogs)
        lpw++;
        lpwsz = (LPWSTR)lpw; // font typeface
        lpw += WinLibCopyText(lpwsz, L"MS Shell Dlg 2", 50);

        BOOL appIsThemed = IsAppThemed();

        // TreeView
        AddItemEx(lpw, WC_TREEVIEW, _TPD_IDC_TREE,
                  0, 0, 0, 0,
                  WS_BORDER | WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                      TVS_DISABLEDRAGDROP | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS |
                      (appIsThemed ? TVS_FULLROWSELECT : TVS_HASLINES),
                  0, NULL);
        // Caption
        AddItemEx(lpw, L"static", _TPD_IDC_CAPTION,
                  0, 0, 0, 0,
                  WS_CHILD | WS_VISIBLE, 0, NULL);
        // Static, which is replaced by child dialog during init
        AddItemEx(lpw, L"static", _TPD_IDC_RECT,
                  0, 0, maxPageRect.right, maxPageRect.bottom,
                  WS_CHILD, 0, NULL);
        // Separator
        AddItemEx(lpw, L"static", _TPD_IDC_SEP,
                  0, 0, 0, 0,
                  WS_GROUP | WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ, 0, NULL);
        // Bottom row of buttons
        AddItemEx(lpw, L"button", _TPD_IDC_OK,
                  0, 0, 0, 0,
                  WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP, 0, buttonOK);
        if (ApplyButtonCaption() != NULL)
            AddItemEx(lpw, L"button", _TPD_IDC_APPLY, 0, 0, 0, 0,
                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, ApplyButtonCaption());
        AddItemEx(lpw, L"button", IDCANCEL,
                  0, 0, 0, 0,
                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, buttonCancel);
        AddItemEx(lpw, L"button", _TPD_IDC_HELP,
                  0, 0, 0, 0,
                  WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, 0, buttonHelp);
        // Grip (resize)
        AddItemEx(lpw, L"scrollbar", _TPD_IDC_GRIP,
                  0, 0, 0, 0,
                  WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | SBS_SIZEBOX | SBS_SIZEBOXBOTTOMRIGHTALIGN, 0, L"");

        const std::size_t templateSize = reinterpret_cast<const BYTE*>(lpw) - reinterpret_cast<const BYTE*>(hgbl);
        std::vector<BYTE> prepared;
        const bool customized = PrepareDialogTemplate(hgbl, templateSize, prepared);
        GlobalUnlock(hgbl);
        const int result = Dialog.ExecuteIndirect(customized ? reinterpret_cast<const DLGTEMPLATE*>(prepared.data())
                                                            : reinterpret_cast<const DLGTEMPLATE*>(hgbl));
        GlobalFree(hgbl);
        return result;
    }
    else
    {
        TRACE_ET(L"Incorrect call to CPropertyDialog::Execute.");
        return -1;
    }
}

int CTreePropDialog::GetCurSel()
{
    TVITEM item;
    item.hItem = TreeView_GetSelection(Dialog.HTreeView);
    item.mask = TVIF_PARAM;
    TreeView_GetItem(Dialog.HTreeView, &item);
    CPropSheetPage* page = (CPropSheetPage*)item.lParam;
    for (int i = 0; i < Count; i++)
        if (At(i) == page)
            return i;
    return -1;
}

bool CTreePropDialog::NavigateToPage(int page, int control)
{
    if (page < 0 || page >= Count || !TreeView_SelectItem(Dialog.HTreeView, At(page)->HTreeItem))
        return false;
    if (Dialog.CurrentPageIndex != page || At(page)->HWindow == NULL)
        return false;
    HWND target = control != 0 ? GetDlgItem(At(page)->HWindow, control) : NULL;
    if (target == NULL || !IsWindowEnabled(target) || !IsWindowVisible(target))
        target = GetNextDlgTabItem(At(page)->HWindow, NULL, FALSE);
    if (target != NULL)
        SetFocus(target);
    Dialog.EnsurePageFocusVisible();
    return true;
}

int CTreePropDialog::Add(CPropSheetPage* page, CPropSheetPage* parent, BOOL* expanded)
{
    int ret = CPropertyDialog::Add(page);
    if (IsGood())
    {
        page->ParentPage = parent;
        if (expanded != NULL)
            page->Expanded = expanded;
    }
    return ret;
}
