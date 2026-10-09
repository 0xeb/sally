// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "crash_issue.h"
#include "salmon_unicode.h"

//*****************************************************************************
//
// MultiMonCenterWindow
//

HWND GetTopVisibleParent(HWND hParent)
{
    // look for a parent that is no longer a child window (it is a POPUP/OVERLAPPED window)
    HWND hIterator = hParent;
    while ((GetWindowLongPtr(hIterator, GWL_STYLE) & WS_CHILD) &&
           (hIterator = ::GetParent(hIterator)) != NULL &&
           IsWindowVisible(hIterator))
        hParent = hIterator;
    return hParent;
}

void MultiMonGetClipRectByRect(const RECT* rect, RECT* workClipRect, RECT* monitorClipRect)
{
    HMONITOR hMonitor = MonitorFromRect(rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(hMonitor, &mi);
    *workClipRect = mi.rcWork;
    if (monitorClipRect != NULL)
        *monitorClipRect = mi.rcMonitor;
}

void MultiMonGetClipRectByWindow(HWND hByWnd, RECT* workClipRect, RECT* monitorClipRect)
{
    HMONITOR hMonitor; // we will place the window on this monitor
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);

    if (hByWnd != NULL && IsWindowVisible(hByWnd) && !IsIconic(hByWnd)) // note this condition is also in MultiMonCenterWindow
    {
        hMonitor = MonitorFromWindow(hByWnd, MONITOR_DEFAULTTONEAREST);
        // retrieve the desktop working area
        GetMonitorInfo(hMonitor, &mi);
    }
    else
    {
        // if we find a foreground window belonging to our application,
        // center the window on the same desktop
        HWND hForegroundWnd = GetForegroundWindow();
        DWORD processID;
        GetWindowThreadProcessId(hForegroundWnd, &processID);
        if (hForegroundWnd != NULL && processID == GetCurrentProcessId())
        {
            hMonitor = MonitorFromWindow(hForegroundWnd, MONITOR_DEFAULTTONEAREST);
        }
        else
        {
            // otherwise center the window on the primary desktop
            POINT pt;
            pt.x = 0; // primary monitor
            pt.y = 0;
            hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
        }

        // retrieve the desktop working area
        GetMonitorInfo(hMonitor, &mi);
    }
    *workClipRect = mi.rcWork;
    if (monitorClipRect != NULL)
        *monitorClipRect = mi.rcMonitor;
}

void MultiMonCenterWindowByRect(HWND hWindow, const RECT& clipR, const RECT& byR)
{
    if (hWindow == NULL)
    {
        // working with a NULL hwnd causes unwanted window flicker
        TRACE_E("MultiMonCenterWindowByRect: hWindow == NULL");
        return;
    }

    if (IsZoomed(hWindow))
    {
        // do not move a maximized window
        return;
    }

    RECT wndRect;
    GetWindowRect(hWindow, &wndRect);
    int wndWidth = wndRect.right - wndRect.left;
    int wndHeight = wndRect.bottom - wndRect.top;

    // center it
    wndRect.left = byR.left + (byR.right - byR.left - wndWidth) / 2;
    wndRect.top = byR.top + (byR.bottom - byR.top - wndHeight) / 2;
    wndRect.right = wndRect.left + wndWidth;
    wndRect.bottom = wndRect.top + wndHeight;

    // keep the window within the clipping bounds
    if (wndRect.left < clipR.left) // when the window is wider than clipR, leave its left edge visible
    {
        wndRect.left = clipR.left;
        wndRect.right = wndRect.left + wndWidth;
    }

    if (wndRect.top < clipR.top) // when the window is taller than clipR, leave its top edge visible
    {
        wndRect.top = clipR.top;
        wndRect.bottom = wndRect.top + wndHeight;
    }

    if (wndWidth <= clipR.right - clipR.left)
    {
        // when the window fits inside clipR, prevent it from spilling past the right edge
        if (wndRect.right >= clipR.right)
        {
            wndRect.left = clipR.right - wndWidth;
            wndRect.right = wndRect.left + wndWidth;
        }
    }
    else
    {
        // otherwise anchor the window to the left edge so the visible area is maximized
        if (wndRect.left > clipR.left)
            wndRect.left = clipR.left; // make maximum use of the space
    }

    if (wndHeight <= clipR.bottom - clipR.top)
    {
        // when the window fits inside clipR, keep it from extending past the bottom edge
        if (wndRect.bottom >= clipR.bottom)
        {
            wndRect.top = clipR.bottom - wndHeight;
            wndRect.bottom = wndRect.top + wndHeight;
        }
    }
    else
    {
        // otherwise anchor the window to the top edge so the visible area is maximized
        if (wndRect.top > clipR.top)
            wndRect.top = clipR.top; // make maximum use of the space
    }

    SetWindowPos(hWindow, NULL, wndRect.left, wndRect.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MultiMonCenterWindow(HWND hWindow, HWND hByWnd, BOOL findTopWindow)
{
    if (hWindow == NULL)
    {
        // working with a NULL hwnd causes unwanted window flicker
        TRACE_E("MultiMonCenterWindow: hWindow == NULL");
        return;
    }

    if (IsZoomed(hWindow))
    {
        // do not move a maximized window
        return;
    }

    // we need to find the top-level window
    if (findTopWindow)
    {
        if (hByWnd != NULL)
            hByWnd = GetTopVisibleParent(hByWnd);
        else
            TRACE_E("MultiMonCenterWindow: hByWnd == NULL and findTopWindow is TRUE");
    }

    RECT clipR;
    MultiMonGetClipRectByWindow(hByWnd, &clipR, NULL);
    RECT byR;
    if (hByWnd != NULL && IsWindowVisible(hByWnd) && !IsIconic(hByWnd)) // note this condition is also in MultiMonGetClipRectByWindow
        GetWindowRect(hByWnd, &byR);
    else
        byR = clipR;

    MultiMonCenterWindowByRect(hWindow, clipR, byR);
}

//*****************************************************************************
//
// CMainDialog
//

CMainDialog::CMainDialog(HINSTANCE hInstance, int resID, BOOL minidumpOnOpen)
    : CDialog(hInstance, resID, NULL, ooAllocated)
{
    HBoldFont = NULL;
    Compressing = FALSE;
    Minidumping = FALSE;
    MinidumpOnOpen = minidumpOnOpen;
}

CMainDialog::~CMainDialog()
{
    if (HBoldFont != NULL)
    {
        DeleteObject(HBoldFont);
        HBoldFont = NULL;
    }
}

void CMainDialog::ShowChilds(CDialogTaskEnum task, BOOL show)
{
    int ids[] = {IDD_SALMON_MAIN,
                 IDC_SALMON_INTRO,
                 IDC_SALMON_PRIVACY,
                 IDC_SALMON_VIEW,
                 IDC_SALMON_DESCRIPTION,
                 IDC_SALMON_ACTION_LABEL,
                 IDC_SALMON_ACTION,
                 IDC_SALMON_RESTART,
                 IDOK,
                 IDC_SALMON_FOLDER,
                 IDCANCEL,
                 -1};
    for (int i = 0; ids[i] != -1; i++)
        ShowWindow(GetDlgItem(HWindow, ids[i]), show ? SW_SHOW : SW_HIDE);
    if (!show)
    {
        // set the size of the child window according to the content
        HWND hChild = GetDlgItem(HWindow, IDC_SALMON_PROGRESS);
        std::wstring text;
        int resID = 0;
        switch (task)
        {
        case dteCompress:
            resID = IDS_SALMON_COMPRESSING;
            break;
        case dteMinidump:
            resID = IDS_SALMON_MINIDUMP;
            break;
        }
        if (resID != 0)
        {
            text = LoadStr(resID, HLanguage);
            CurrentProgressText = text;
            SetWindowTextW(hChild, text.c_str());
        }
        HFONT hFont = (HFONT)SendMessage(hChild, WM_GETFONT, 0, 0);
        HDC hDC = HANDLES(GetDC(HWindow));
        HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);
        RECT tR;
        tR.left = 0;
        tR.top = 0;
        tR.right = 1;
        tR.bottom = 1;
        DrawTextW(hDC, text.c_str(), -1, &tR, DT_CALCRECT | DT_CENTER | DT_NOPREFIX);
        SelectObject(hDC, hOldFont);
        HANDLES(ReleaseDC(HWindow, hDC));
        SIZE sz = {tR.right - tR.left, tR.bottom - tR.top};
        sz.cx += 3; // just to be sure
        sz.cy += 1;
        SetWindowPos(hChild, NULL, 0, 0, sz.cx, sz.cy, SWP_NOZORDER | SWP_NOMOVE);
        CenterControl(IDC_SALMON_PROGRESS);
    }
    ShowWindow(GetDlgItem(HWindow, IDC_SALMON_PROGRESS), show ? SW_HIDE : SW_SHOW);
}

void CMainDialog::CenterControl(int resID)
{
    RECT wR;
    GetClientRect(HWindow, &wR);
    RECT cR;
    GetClientRect(GetDlgItem(HWindow, resID), &cR);
    SetWindowPos(GetDlgItem(HWindow, resID), NULL, (wR.right - cR.right) / 2, (wR.bottom - cR.bottom) / 2, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

void CMainDialog::Transfer(CTransferInfo& ti)
{
    ti.EditLineW(IDC_SALMON_ACTION, Config.Description);
}

// The new-issue address for the newest report: a summary of the crash that holds no private data,
// and the user's own description of what they were doing.
static std::wstring ComposeIssueAddress()
{
    std::string report;
    if (!BugReports.empty())
        ReadBugReportText(BugReports[0], report);
    const sally::salmon::CrashSummary summary = sally::salmon::SummarizeBugReport(report);
    std::string lastAction;
    if (!sally::salmon::EncodeUtf8(Config.Description, lastAction))
        lastAction.clear();
    const std::string address = sally::salmon::BuildCrashIssueAddress(summary, lastAction,
                                                                      GetUniqueBugReportCount() - 1);
    return std::wstring(address.begin(), address.end()); // percent-encoded, so ASCII only
}

void CMainDialog::FinishReport(BOOL packed)
{
    ShowChilds(dteDialog, TRUE);
    const BOOL opened = OpenWithShell(HWindow, IssueAddress.c_str());
    if (packed)
    {
        // The archives hold the reports from now on; the reporter does not offer them again.
        CleanBugReportsDirectory(TRUE);
        const std::wstring msg = FormatText(LoadStrW(opened ? IDS_SALMON_GITHUB_OPENED : IDS_SALMON_GITHUB_FAILED, HLanguage).c_str(),
                                            BugReportPath.c_str());
        MessageBoxW(HWindow, msg.c_str(), LoadStrW(IDS_SALMON_TITLE, HLanguage).c_str(),
                    MB_OK | (opened ? MB_ICONINFORMATION : MB_ICONEXCLAMATION) | MB_SETFOREGROUND);
    }
    else
    {
        if (!opened)
        {
            const std::wstring msg = FormatText(LoadStrW(IDS_SALMON_GITHUB_FAILED, HLanguage).c_str(), BugReportPath.c_str());
            MessageBoxW(HWindow, msg.c_str(), LoadStrW(IDS_SALMON_TITLE, HLanguage).c_str(), MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);
        }
        const std::wstring msg = FormatText(LoadStrW(IDS_SALMON_COMPRESSFAILED, HLanguage).c_str(), CompressParams.ErrorMessage.c_str());
        MessageBoxW(HWindow, msg.c_str(), LoadStrW(IDS_SALMON_TITLE, HLanguage).c_str(), MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);
        OpenFolder(NULL, BugReportPath.c_str());
    }
    if (IsDlgButtonChecked(HWindow, IDC_SALMON_RESTART) == BST_CHECKED)
        RestartSalamander(HWindow);
    PostQuitMessage(0);
}

INT_PTR
CMainDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // the monitored process grants us permission to call SetForegroundWindow.
        // The crucial detail is to invoke it only once our message loop is already running;
        // otherwise Windows behaves poorly (until we call SetForegroundWindow from OpenMainDialog).
        // When the monitored application was launched from the Start Menu and crashed, we stayed in the background,
        // the ProtMon window remained inactive, and it did not receive focus until the monitored application
        // closed its window.
        SetForegroundWindow(HWindow);

        MultiMonCenterWindow(HWindow, NULL, FALSE);

        ShowChilds(dteDialog, TRUE);
        CenterControl(IDC_SALMON_PROGRESS);

        LOGFONT lf;
        HFONT hFont = (HFONT)SendMessage(GetDlgItem(HWindow, IDC_SALMON_DESCRIPTION), WM_GETFONT, 0, 0);
        GetObject(hFont, sizeof(lf), &lf);
        lf.lfWeight = FW_BOLD;
        HBoldFont = CreateFontIndirect(&lf);

        SendMessage(GetDlgItem(HWindow, IDC_SALMON_DESCRIPTION), WM_SETFONT, (WPARAM)HBoldFont, TRUE);

        EnableWindow(GetDlgItem(HWindow, IDC_SALMON_RESTART), MinidumpOnOpen);
        if (MinidumpOnOpen)
            CheckDlgButton(HWindow, IDC_SALMON_RESTART, BST_CHECKED);

        SetTimer(HWindow, 666, 250, NULL);

        if (MinidumpOnOpen)
        {
            MinidumpParams = {};
            Minidumping = StartMinidumpThread(&MinidumpParams);
            ShowChilds(dteMinidump, FALSE);
        }

        // focus the field with the crash description
        SetFocus(GetDlgItem(HWindow, IDC_SALMON_ACTION));
        SendMessage(HWindow, DM_SETDEFID, IDC_SALMON_ACTION, 0);

        CDialog::DialogProc(uMsg, wParam, lParam);
        return FALSE;
    }

    case WM_DESTROY:
    {
        KillTimer(HWindow, 666);
        break;
    }

    case WM_CLOSE:
    {
        return 0;
    }

    case WM_TIMER:
    {
        if (!AppIsBusy && wParam == 666) // skip updates while a message box is up; we do not want another one
        {
            if (Compressing || Minidumping)
            {
                static DWORD counter = 0;
                counter++;
                std::wstring text = CurrentProgressText;
                if (text.size() > 3 && text.compare(text.size() - 3, 3, L"...") == 0)
                    text.resize(text.size() - 3 + (counter % 4));
                HWND hChild = GetDlgItem(HWindow, IDC_SALMON_PROGRESS);
                SetWindowTextW(hChild, text.c_str());
            }

            if (Minidumping && !IsMinidumpThreadRunning())
            {
                Minidumping = FALSE;

                // if minidump generation failed, just report the error but continue (some data may have been saved)
                if (!MinidumpParams.Result)
                {
                    const std::wstring msg = FormatText(LoadStrW(IDS_SALMON_BUGREPORT_PROBLEM, HLanguage).c_str(), MinidumpParams.ErrorMessage.c_str());
                    MessageBoxW(HWindow, msg.c_str(), LoadStrW(IDS_SALMON_TITLE, HLanguage).c_str(), MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);
                }

                GetBugReportNames();
                ShowChilds(dteDialog, TRUE);
            }

            if (Compressing && !IsCompressThreadRunning())
            {
                Compressing = FALSE;
                FinishReport(CompressParams.Result);
            }
        }
        return 0;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            if (Compressing || Minidumping || !TransferData(ttDataFromWindow))
                return TRUE;
            SaveDescription();
            IssueAddress = ComposeIssueAddress();

            // pack the reports so they can be shared privately if a maintainer asks for them
            CompressParams = {};
            Compressing = StartCompressThread(&CompressParams);
            if (Compressing)
                ShowChilds(dteCompress, FALSE);
            else
                FinishReport(FALSE);
            return 0;
        }

        case IDCANCEL:
        {
            if (Compressing || Minidumping)
            {
                MessageBoxW(HWindow, LoadStrW(IDS_SALMON_WAIT, HLanguage).c_str(), LoadStrW(IDS_SALMON_TITLE, HLanguage).c_str(), MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
                return 0;
            }
            int ret = MessageBoxW(HWindow, LoadStrW(IDS_SALMON_CONFIRMEXIT, HLanguage).c_str(), LoadStrW(IDS_SALMON_TITLE, HLanguage).c_str(), MB_OKCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND);
            if (ret == IDCANCEL)
                return 0;

            CleanBugReportsDirectory(FALSE);

            if (IsDlgButtonChecked(HWindow, IDC_SALMON_RESTART) == BST_CHECKED)
                RestartSalamander(HWindow);

            PostQuitMessage(0);
            break;
        }

        case IDC_SALMON_VIEW:
        {
            // the text report of the newest crash, or the folder when there is none
            std::wstring report;
            if (!BugReports.empty())
            {
                report = BugReportPath + BugReports[0].Name + L".TXT";
                if (GetFileAttributesW(report.c_str()) == INVALID_FILE_ATTRIBUTES)
                    report.clear();
            }
            if (report.empty() || !OpenWithShell(HWindow, report.c_str()))
                OpenFolder(HWindow, BugReportPath.c_str());
            break;
        }

        case IDC_SALMON_FOLDER:
        {
            OpenFolder(HWindow, BugReportPath.c_str());
            break;
        }
        }
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}
