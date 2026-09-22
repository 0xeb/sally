// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "filecomp_path_history.h"

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

std::vector<std::wstring> CBHistory;

void AddToHistory(const wchar_t* path)
{
    CALL_STACK_MESSAGE2("AddToHistory(%ls)", path);
    AddFilecompPathToHistory(
        CBHistory, path, MAX_HISTORY_ENTRIES,
        [](const wchar_t* left, const wchar_t* right) {
            return SG->IsTheSamePath(left, right) != FALSE;
        });
}

CCompareFilesDialog::CCompareFilesDialog(HWND parent, std::wstring& path1,
                                         std::wstring& path2, BOOL& succes,
                                         CCompareOptions* options)
    : CCommonDialog(IDD_COMPAREFILES, parent), Path1(path1), Path2(path2),
      Succes(succes)
{
    CALL_STACK_MESSAGE_NONE
    Options = options;
}

// FileExistsW is now available from shared lcutils.h

void CCompareFilesDialog::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCompareFilesDialog::Validate()");
    int i;
    for (i = 0; i < 2; i++)
    {
        HWND combo = GetDlgItem(HWindow, IDE_PATH1 + i);
        HWND edit = GetWindow(combo, GW_CHILD);
        const std::wstring path = SPLGetWindowTextOwned(edit ? edit : combo);
        if (path.empty())
        {
            SG->SalMessageBox(HWindow, SPLLoadStrOwned(SG, HLanguage, IDS_MISSINGPATH).c_str(),
                              SPLLoadStrOwned(SG, HLanguage, IDS_ERROR).c_str(), MB_ICONERROR);
            ti.ErrorOn(IDE_PATH1 + i);
            return;
        }
        if (!FileExistsW(path.c_str()))
        {
            const std::wstring message = SPLFormatStringOwned(
                SPLLoadStrOwned(SG, HLanguage, IDS_FILEDOESNOTEXIST).c_str(), path.c_str());
            SG->SalMessageBox(HWindow, message.c_str(), SPLLoadStrOwned(SG, HLanguage, IDS_ERROR).c_str(),
                              MB_ICONERROR);
            ti.ErrorOn(IDE_PATH1 + i);
            return;
        }
    }
}

void CCompareFilesDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCompareFilesDialog::Transfer()");

    HWND hWnd1 = GetDlgItem(HWindow, IDE_PATH1);
    HWND hWnd2 = GetDlgItem(HWindow, IDE_PATH2);
    HWND hEdit1 = GetWindow(hWnd1, GW_CHILD);
    HWND hEdit2 = GetWindow(hWnd2, GW_CHILD);

    if (ti.Type == ttDataToWindow)
    {
        SetWindowTextW(hEdit1 ? hEdit1 : hWnd1, Path1.c_str());
        SetWindowTextW(hEdit2 ? hEdit2 : hWnd2, Path2.c_str());
    }
    else
    {
        Path1 = SPLGetWindowTextOwned(hEdit1 ? hEdit1 : hWnd1);
        Path2 = SPLGetWindowTextOwned(hEdit2 ? hEdit2 : hWnd2);
        AddToHistory(Path2.c_str());
        AddToHistory(Path1.c_str());
        Succes = TRUE;
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
        const UINT length = DragQueryFileW(hDrop, 0, NULL, 0);
        if (length != 0)
        {
            std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
            if (DragQueryFileW(hDrop, 0, buffer.data(),
                               static_cast<UINT>(buffer.size())) == length)
                SetWindowTextW(hWnd, buffer.data());
        }

        DragFinish(hDrop);
        return 0;
    }

    return CallWindowProcW(hWnd == GetDlgItem(pParent->HWindow, IDE_PATH1) ? pParent->OldEditProc1 : pParent->OldEditProc2, hWnd, uMsg, wParam, lParam);
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
        if (CBHistory.size() > 1)
        {
            // store the first two paths in the second combo box in reverse order
            for (; i < 2; i++)
            {
                SendMessageW(GetDlgItem(HWindow, IDE_PATH1), CB_ADDSTRING, 0, (LPARAM)CBHistory[i].c_str());
                SendMessageW(GetDlgItem(HWindow, IDE_PATH2), CB_ADDSTRING, 0, (LPARAM)CBHistory[1 - i].c_str());
            }
        }
        for (; i < static_cast<int>(CBHistory.size()); i++)
        {
            SendMessageW(GetDlgItem(HWindow, IDE_PATH1), CB_ADDSTRING, 0, (LPARAM)CBHistory[i].c_str());
            SendMessageW(GetDlgItem(HWindow, IDE_PATH2), CB_ADDSTRING, 0, (LPARAM)CBHistory[i].c_str());
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
            memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = HWindow;
            std::wstring filterW = SPLLoadStrOwned(SG, HLanguage, IDS_ALLFILES).c_str();
            filterW.push_back(L'\0');
            filterW += L"*.*";
            filterW.push_back(L'\0');
            filterW.push_back(L'\0');
            ofn.lpstrFilter = filterW.c_str();
            std::vector<std::wstring> selectedFiles;
            std::wstring initialDirW;
            const std::wstring path = SPLGetDlgItemTextOwned(HWindow, idCB);
            if (path.empty())
            {
                if (!CBHistory.empty())
                {
                    initialDirW = CBHistory[0];
                    SPLCutDirectoryOwned(SG, initialDirW);
                }
                ofn.lpstrInitialDir = initialDirW.c_str();
            }
            else
                selectedFiles.push_back(path);
            std::wstring titleW = SPLLoadStrOwned(SG, HLanguage, idTitle).c_str();
            ofn.lpstrTitle = titleW.c_str();
            //ofn.lpfnHook = OFNHookProc;
            ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR /*| OFN_ENABLEHOOK*/;

            if (SPLSafeGetOpenFileNamesOwned(SG, &ofn, selectedFiles) &&
                selectedFiles.size() == 1)
                SetDlgItemTextW(HWindow, idCB, selectedFiles[0].c_str());

            return 0;
        }
        }
        break;
    }

    case WM_USER_CLEARHISTORY:
    {
        HWND cb = GetDlgItem(HWindow, IDE_PATH1);
        std::wstring text = SPLGetWindowTextOwned(cb);
        SendMessage(cb, CB_RESETCONTENT, 0, 0);
        SendMessageW(cb, WM_SETTEXT, 0, (LPARAM)text.c_str());
        cb = GetDlgItem(HWindow, IDE_PATH2);
        text = SPLGetWindowTextOwned(cb);
        SendMessage(cb, CB_RESETCONTENT, 0, 0);
        SendMessageW(cb, WM_SETTEXT, 0, (LPARAM)text.c_str());
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
