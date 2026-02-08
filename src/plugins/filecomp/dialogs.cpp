// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

UINT_PTR CALLBACK
ComDlgHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("ComDlgHookProc(, 0x%X, 0x%IX, 0x%IX)", uiMsg, wParam,
                        lParam);
    if (uiMsg == WM_INITDIALOG)
    {
        // SalamanderGUI->ArrangeHorizontalLines(hdlg);  // we do not do this for Windows common dialogs
        CenterWindow(hdlg);
        return 1;
    }
    return 0;
}

// ****************************************************************************
//
// CCompareFilesDialog
//

// history for combo boxes

TCHAR CBHistory[MAX_HISTORY_ENTRIES][MAX_PATH];
int CBHistoryEntries;

void AddToHistory(LPCTSTR path)
{
    CALL_STACK_MESSAGE2(_T("AddToHistory(%s)"), path);
    int toMove = __min(CBHistoryEntries, MAX_HISTORY_ENTRIES - 1);
    int enlarge = 1;
    // check whether the same path is already in the history
    int i;
    for (i = 0; i < CBHistoryEntries; i++)
    {
        if (SG->IsTheSamePath(CBHistory[i], path))
        {
            toMove = i;
            enlarge = 0;
            break;
        }
    }
    // create space for the path we are going to store
    int j;
    for (j = toMove; j > 0; j--)
        _tcscpy(CBHistory[j], CBHistory[j - 1]);
    // And store the path...
    _tcscpy(CBHistory[0], path);
    CBHistoryEntries = __min(CBHistoryEntries + enlarge, MAX_HISTORY_ENTRIES);
}

CCompareFilesDialog::CCompareFilesDialog(HWND parent, LPTSTR path1, LPTSTR path2,
                                         BOOL& succes, CCompareOptions* options,
                                         wchar_t* path1W, wchar_t* path2W, int pathWSize)
    : CCommonDialog(IDD_COMPAREFILES, parent), Succes(succes)
{
    CALL_STACK_MESSAGE_NONE
    Path1 = path1;
    Path2 = path2;
    Path1W = path1W;
    Path2W = path2W;
    PathWSize = pathWSize;
    Succes = succes;
    Options = options;
}

BOOL FileExists(LPCTSTR path)
{
    CALL_STACK_MESSAGE2(_T("FileExists(%s)"), path);
    DWORD attr = SG->SalGetFileAttributes(path);
    int i = GetLastError();
    return ((attr != 0xffffffff) && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) ||
           ((attr == 0xffffffff) && ((i != ERROR_FILE_NOT_FOUND) && (i != ERROR_PATH_NOT_FOUND)));
}

// FileExistsW is now available from shared lcutils.h

void CCompareFilesDialog::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCompareFilesDialog::Validate()");
    const int LONG_PATH_SIZE = 32767;
    TCHAR* buffer = (TCHAR*)malloc(LONG_PATH_SIZE * sizeof(TCHAR));
    wchar_t* wideBuffer = (wchar_t*)malloc(LONG_PATH_SIZE * sizeof(wchar_t));
    if (!buffer || !wideBuffer)
    {
        free(buffer);
        free(wideBuffer);
        return;
    }

    int i;
    for (i = 0; i < 2; i++)
    {
        // Get text from edit control with large buffer
        GetDlgItemText(HWindow, IDE_PATH1 + i, buffer, LONG_PATH_SIZE);
        if (!*buffer)
        {
            SG->SalMessageBox(HWindow, LoadStr(IDS_MISSINGPATH), LoadStr(IDS_ERROR), MB_ICONERROR);
            ti.ErrorOn(IDE_PATH1 + i);
            free(buffer);
            free(wideBuffer);
            return;
        }
        // Convert to wide for file existence check (supports long paths and Unicode)
        MultiByteToWideChar(CP_ACP, 0, buffer, -1, wideBuffer, LONG_PATH_SIZE);
        if (!FileExistsW(wideBuffer))
        {
            TCHAR* buf2 = (TCHAR*)malloc((300 + LONG_PATH_SIZE) * sizeof(TCHAR));
            if (buf2)
            {
                wsprintf(buf2, LoadStr(IDS_FILEDOESNOTEXIST), buffer);
                SG->SalMessageBox(HWindow, buf2, LoadStr(IDS_ERROR), MB_ICONERROR);
                free(buf2);
            }
            ti.ErrorOn(IDE_PATH1 + i);
            free(buffer);
            free(wideBuffer);
            return;
        }
    }
    free(buffer);
    free(wideBuffer);
}

void CCompareFilesDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCompareFilesDialog::Transfer()");

    // Use wide APIs if wide path buffers are provided
    if (Path1W && Path2W && PathWSize > 0)
    {
        HWND hWnd1 = GetDlgItem(HWindow, IDE_PATH1);
        HWND hWnd2 = GetDlgItem(HWindow, IDE_PATH2);

        if (ti.Type == ttDataToWindow)
        {
            // For combo boxes, get the edit control child and set text there
            // This ensures Unicode display even on ANSI dialog controls
            HWND hEdit1 = GetWindow(hWnd1, GW_CHILD);
            HWND hEdit2 = GetWindow(hWnd2, GW_CHILD);
            if (hEdit1)
                SendMessageW(hEdit1, WM_SETTEXT, 0, (LPARAM)Path1W);
            else
                SendMessageW(hWnd1, WM_SETTEXT, 0, (LPARAM)Path1W);
            if (hEdit2)
                SendMessageW(hEdit2, WM_SETTEXT, 0, (LPARAM)Path2W);
            else
                SendMessageW(hWnd2, WM_SETTEXT, 0, (LPARAM)Path2W);
        }
        else // ttDataFromWindow
        {
            // Read text from the edit control child
            HWND hEdit1 = GetWindow(hWnd1, GW_CHILD);
            HWND hEdit2 = GetWindow(hWnd2, GW_CHILD);
            if (hEdit1)
                SendMessageW(hEdit1, WM_GETTEXT, PathWSize, (LPARAM)Path1W);
            else
                SendMessageW(hWnd1, WM_GETTEXT, PathWSize, (LPARAM)Path1W);
            if (hEdit2)
                SendMessageW(hEdit2, WM_GETTEXT, PathWSize, (LPARAM)Path2W);
            else
                SendMessageW(hWnd2, WM_GETTEXT, PathWSize, (LPARAM)Path2W);

            // Also update ANSI paths for history (lossy but OK for history)
            WideCharToMultiByte(CP_ACP, 0, Path1W, -1, Path1, MAX_PATH, NULL, NULL);
            WideCharToMultiByte(CP_ACP, 0, Path2W, -1, Path2, MAX_PATH, NULL, NULL);

            AddToHistory(Path2);
            AddToHistory(Path1);
            Succes = TRUE;
        }
    }
    else
    {
        // Fallback to ANSI if no wide buffers provided
        ti.EditLine(IDE_PATH1, Path1, MAX_PATH);
        ti.EditLine(IDE_PATH2, Path2, MAX_PATH);
        if (ti.Type == ttDataFromWindow)
        {
            AddToHistory(Path2);
            AddToHistory(Path1);
            Succes = TRUE;
        }
    }
}

/*
UINT CALLBACK 
OFNHookProc(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
  CALL_STACK_MESSAGE4("OFNHookProc(, 0x%X, 0x%IX, 0x%IX)", uiMsg, wParam, lParam);
  if (uiMsg == WM_INITDIALOG)
  {
    // SalamanderGUI->ArrangeHorizontalLines(hdlg);  // we do not do this for Windows common dialogs
    HWND hwnd = GetParent(hdlg);
    CenterWindow(hdlg);
    return 1;
  }
  return 0;
}
*/

LRESULT CCompareFilesDialog::DragDropEditProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CCompareFilesDialog* pParent = (CCompareFilesDialog*)WindowsManager.GetWindowPtr(GetParent(hWnd));

    if (!pParent)
        return NULL; // What's wrong?

    if (WM_DROPFILES == uMsg)
    {
        HDROP hDrop = (HDROP)wParam;
        CPathBuffer buffer; // Heap-allocated for long path support
        int nFilesDropped = DragQueryFile(hDrop, 0, buffer, buffer.Size());

        if (nFilesDropped)
        {
            SetWindowText(hWnd, buffer);
        }

        DragFinish(hDrop);
        return 0;
    }

    return CallWindowProc(hWnd == GetDlgItem(pParent->HWindow, IDE_PATH1) ? pParent->OldEditProc1 : pParent->OldEditProc2, hWnd, uMsg, wParam, lParam);
}

INT_PTR
CCompareFilesDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCompareFilesDialog::DialogProc(0x%X, 0x%IX, 0x%IX)",
                        uMsg, wParam, lParam);
    UINT idCB;
    UINT idTitle;

    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        HWND hWnd1 = GetDlgItem(HWindow, IDE_PATH1), hWnd2 = GetDlgItem(HWindow, IDE_PATH2);

        SG->InstallWordBreakProc(hWnd1); // install WordBreakProc into the combo box
        SG->InstallWordBreakProc(hWnd2); // install WordBreakProc into the combo box

        // I believe OldEditProc1 and OldEditProc2 are equal. But I am rather paranoic...
        OldEditProc1 = (WNDPROC)GetWindowLongPtr(hWnd1, GWLP_WNDPROC);
        OldEditProc2 = (WNDPROC)GetWindowLongPtr(hWnd2, GWLP_WNDPROC);
        SetWindowLongPtr(hWnd1, GWLP_WNDPROC, (LONG_PTR)DragDropEditProc);
        SetWindowLongPtr(hWnd2, GWLP_WNDPROC, (LONG_PTR)DragDropEditProc);
        DragAcceptFiles(hWnd1, TRUE);
        DragAcceptFiles(hWnd2, TRUE);

        SetWindowPos(HWindow, AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        // initialize the history
        int i = 0;
        if (CBHistoryEntries > 1)
        {
            // store the first two paths in the second combo box in reverse order
            for (; i < 2; i++)
            {
                SendMessage(GetDlgItem(HWindow, IDE_PATH1), CB_ADDSTRING, 0, (LPARAM)CBHistory[i]);
                SendMessage(GetDlgItem(HWindow, IDE_PATH2), CB_ADDSTRING, 0, (LPARAM)CBHistory[1 - i]);
            }
        }
        for (; i < CBHistoryEntries; i++)
        {
            SendMessage(GetDlgItem(HWindow, IDE_PATH1), CB_ADDSTRING, 0, (LPARAM)CBHistory[i]);
            SendMessage(GetDlgItem(HWindow, IDE_PATH2), CB_ADDSTRING, 0, (LPARAM)CBHistory[i]);
        }

        SendMessage(HWindow, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_FCICO)));

        // Note: Wide path text is set via Transfer() which runs after WM_INITDIALOG

        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDADVANCED:
        {
            BOOL setDefault = FALSE;
            CAdvancedOptionsDialog dlg(HWindow, Options, &setDefault);
            if (dlg.Execute() == IDOK && setDefault)
            {
                if (memcmp(Options, &DefCompareOptions, sizeof(*Options)) != 0)
                {
                    DefCompareOptions = *Options;
                    MainWindowQueue.BroadcastMessage(WM_USER_CFGCHNG, CC_DEFOPTIONS | CC_HAVEHWND, (LPARAM)GetParent(HWindow));
                }
            }
            return 0;
        }

        case IDB_BROWSE1:
        case IDB_BROWSE2:
        {
            if (IDB_BROWSE1 == LOWORD(wParam))
            {
                idCB = IDE_PATH1;
                idTitle = IDS_SELECTFIRST;
            }
            else
            {
                idCB = IDE_PATH2;
                idTitle = IDS_SELECTSECOND;
            }

            OPENFILENAME ofn;
            CPathBuffer path; // Heap-allocated for long path support
            CPathBuffer dir;  // Heap-allocated for long path support
            TCHAR buf[128];

            memset(&ofn, 0, sizeof(OPENFILENAME));
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner = HWindow;
            _stprintf(buf, _T("%s%c*.*%c"), LoadStr(IDS_ALLFILES), 0, 0);
            ofn.lpstrFilter = buf;
            if (!GetDlgItemText(HWindow, idCB, path, path.Size()))
            {
                SendDlgItemMessage(HWindow, idCB, CB_GETLBTEXT, 0, (LPARAM)dir.Get());
                SG->CutDirectory(dir);
                ofn.lpstrInitialDir = dir;
            }
            ofn.lpstrFile = path;
            ofn.nMaxFile = path.Size();
            ofn.lpstrTitle = LoadStr(idTitle);
            //ofn.lpfnHook = OFNHookProc;
            ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR /*| OFN_ENABLEHOOK*/;

            if (SG->SafeGetOpenFileName(&ofn))
                SetDlgItemText(HWindow, idCB, path);

            return 0;
        }
        }
        break;
    }

    case WM_USER_CLEARHISTORY:
    {
        CPathBuffer buffer; // Heap-allocated for long path support
        HWND cb = GetDlgItem(HWindow, IDE_PATH1);
        SendMessage(cb, WM_GETTEXT, buffer.Size(), (LPARAM)buffer.Get());
        SendMessage(cb, CB_RESETCONTENT, 0, 0);
        SendMessage(cb, WM_SETTEXT, 0, (LPARAM)buffer.Get());
        cb = GetDlgItem(HWindow, IDE_PATH2);
        SendMessage(cb, WM_GETTEXT, buffer.Size(), (LPARAM)buffer.Get());
        SendMessage(cb, CB_RESETCONTENT, 0, 0);
        SendMessage(cb, WM_SETTEXT, 0, (LPARAM)buffer.Get());
        break;
    }

    case WM_DESTROY:
        DragAcceptFiles(GetDlgItem(HWindow, IDE_PATH1), FALSE);
        DragAcceptFiles(GetDlgItem(HWindow, IDE_PATH2), FALSE);
        break;
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
// CCommonPropSheetPage
//

void CCommonPropSheetPage::NotifDlgJustCreated()
{
    SalGUI->ArrangeHorizontalLines(HWindow);
}
