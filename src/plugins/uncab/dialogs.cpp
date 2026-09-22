// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "array2.h"

#include "fdi.h"
#include "uncab.h"
#include "dialogs.h"

#include "uncab.rh"
#include "uncab.rh2"
#include "lang\lang.rh"

WNDPROC OrigTextControlProc;

static BOOL GetFileSystemPathFromIDListOwned(PCIDLIST_ABSOLUTE idList,
                                             std::wstring& path)
{
    PWSTR value = nullptr;
    const HRESULT result = SHGetNameFromIDList(idList, SIGDN_FILESYSPATH, &value);
    if (FAILED(result) || value == nullptr)
        return FALSE;
    path.assign(value);
    CoTaskMemFree(value);
    return TRUE;
}

LRESULT CALLBACK TextControlProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("TextControlProc(, 0x%X, 0x%IX, 0x%IX)", uMsg, wParam,
                        lParam);
    switch (uMsg)
    {
    case WM_PAINT:
    {
        RECT r;
        PAINTSTRUCT ps;
        GetClientRect(hWnd, &r);
        BeginPaint(hWnd, &ps);
        HBRUSH DialogBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        if (DialogBrush)
        {
            FillRect(ps.hdc, &r, DialogBrush);
            DeleteObject(DialogBrush);
        }
        HFONT hCurrentFont = (HFONT)SendMessageW(hWnd, WM_GETFONT, 0, 0);
        HFONT hOldFont = (HFONT)SelectObject(ps.hdc, hCurrentFont);
        SetTextColor(ps.hdc, GetSysColor(COLOR_BTNTEXT));
        int prevBkMode = SetBkMode(ps.hdc, TRANSPARENT);
        const std::wstring txt = SPLGetWindowTextOwned(hWnd);
        DrawTextW(ps.hdc, txt.c_str(), static_cast<int>(txt.size()), &r,
                  DT_SINGLELINE | /*DT_VCENTER*/ DT_BOTTOM | DT_NOPREFIX | DT_PATH_ELLIPSIS);
        SetBkMode(ps.hdc, prevBkMode);
        SelectObject(ps.hdc, hOldFont);
        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return CallWindowProcW(OrigTextControlProc, hWnd, uMsg, wParam, lParam);
}

// ****************************************************************************
//
// CDlgRoot
//

void CDlgRoot::CenterDlgToParent()
{
    CALL_STACK_MESSAGE1("CDlgRoot::CenterDlgToParent()");
    HWND hParent = GetParent(Dlg);
    if (hParent != NULL)
        SalamanderGeneral->MultiMonCenterWindow(Dlg, hParent, TRUE);
}

void CDlgRoot::SubClassStatic(DWORD wID, BOOL subclass)
{
    CALL_STACK_MESSAGE3("CDlgRoot::SubClassStatic(0x%X, %d)", wID, subclass);
    if (subclass)
        OrigTextControlProc = (WNDPROC)SetWindowLongPtrW(GetDlgItem(Dlg, wID), GWLP_WNDPROC, (LONG_PTR)TextControlProc);
    else
        SetWindowLongPtrW(GetDlgItem(Dlg, wID), GWLP_WNDPROC, (LONG_PTR)OrigTextControlProc);
}

// ****************************************************************************
//
// CNextVolumeDialog
//

CNextVolumeDialog::CNextVolumeDialog(HWND parent, const std::wstring& volumeName,
                                     std::wstring* volumePath,
                                     const std::wstring& initialVolumePath,
                                     const std::wstring& diskName,
                                     int cabNumber) : CDlgRoot(parent)
{
    VolumePath = volumePath;
    VolumeNameW = volumeName;
    InitialVolumePathW = initialVolumePath;
    DiskNameW = diskName;
    CabNumber = cabNumber;
    CurrentPath = VolumeNameW;
    SPLCutDirectoryOwned(SalamanderGeneral, CurrentPath);
}

INT_PTR WINAPI NextVolumeDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("NextVolumeDlgProc(, 0x%X, 0x%IX, 0x%IX)", uMsg, wParam,
                        lParam);
    static CNextVolumeDialog* dlg = NULL;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        // SalamanderGUI->ArrangeHorizontalLines(hDlg); // it should be called, but we ignore it here, there are no horizontal lines
        dlg = (CNextVolumeDialog*)lParam;
        dlg->Dlg = hDlg;
        return dlg->DialogProc(uMsg, wParam, lParam);

    default:
        if (dlg)
            return dlg->DialogProc(uMsg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR
CNextVolumeDialog::Proceed()
{
    CALL_STACK_MESSAGE1("CNextVolumeDialog::Proceed()");
    return DialogBoxParamW(HLanguage, MAKEINTRESOURCEW(IDD_CHANGEDISK),
                           Parent, NextVolumeDlgProc, (LPARAM)this);
}

INT_PTR
CNextVolumeDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CNextVolumeDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
        return OnInit(wParam, lParam);
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            return OnOK(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);

        case IDCANCEL:
            EndDialog(Dlg, IDCANCEL);
            return FALSE;

        case IDC_BROWSE:
            return OnBrowse(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);
        }
        break;
    }
    return FALSE;
}

BOOL CNextVolumeDialog::OnInit(WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE3("CNextVolumeDialog::OnInit(0x%IX, 0x%IX)", wParam, lParam);
    const std::wstring text = SPLFormatStringOwned(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_NEXTVOLTEXT).c_str(),
        CabNumber);
    SetDlgItemTextW(Dlg, IDC_TEXT, text.c_str());
    SetDlgItemTextW(Dlg, IDC_DISKNAME, DiskNameW.c_str());
    SetDlgItemTextW(Dlg, IDC_CABNAME, VolumeNameW.c_str());
    SetDlgItemTextW(Dlg, IDC_FILENAME, InitialVolumePathW.c_str());

    CenterDlgToParent();
    return TRUE;
}

int CALLBACK DirectoryBrowse(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
    CALL_STACK_MESSAGE4("DirectoryBrowse(, 0x%X, 0x%IX, 0x%IX)", uMsg, lParam,
                        lpData);
    if (uMsg == BFFM_INITIALIZED)
    {
        SetWindowTextW(hwnd, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_BROWSEARCHIVETITLE).c_str());
        std::wstring& path = *(std::wstring*)lpData;
        std::wstring rootPath;
        SPLGetRootPathOwned(SalamanderGeneral, path.c_str(), rootPath);
        if (!rootPath.empty() && rootPath.back() == L'\\')
            rootPath.pop_back();
        if (!path.empty() && path.back() == L'\\')
            path.pop_back();
        if (rootPath.size() == path.size()) // this is the root directory
            SPLSalPathAddBackslashOwned(path);
        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, (LPARAM)path.c_str());
    }
    return 0;
}

BOOL CNextVolumeDialog::OnBrowse(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CNextVolumeDialog::OnBrowse(0x%X, 0x%X, )", wNotifyCode,
                        wID);
    std::wstring path = SPLGetDlgItemTextOwned(Dlg, IDC_FILENAME);

    BROWSEINFOW bi = {};
    bi.hwndOwner = Dlg;
    bi.pidlRoot = NULL;
    bi.pszDisplayName = NULL;
    const std::wstring title = SPLFormatStringOwned(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_BROWSEFOLDERTEXT).c_str(),
        VolumeNameW.c_str());
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS;
    bi.lpfn = DirectoryBrowse;
    bi.lParam = (LPARAM)&path;
    LPITEMIDLIST res = SHBrowseForFolderW(&bi);
    if (res != NULL)
    {
        if (GetFileSystemPathFromIDListOwned(res, path))
            SetDlgItemTextW(Dlg, IDC_FILENAME, path.c_str());
    }
    // release the item ID list
    IMalloc* alloc;
    if (SUCCEEDED(CoGetMalloc(1, &alloc)))
    {
        if (alloc->DidAlloc(res) == 1)
            alloc->Free(res);
        alloc->Release();
    }

    return TRUE;
}

BOOL CNextVolumeDialog::OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CNextVolumeDialog::OnOK(0x%X, 0x%X, )", wNotifyCode, wID);
    std::wstring volumePathW = SPLGetDlgItemTextOwned(Dlg, IDC_FILENAME);
    SPLSalPathAddBackslashOwned(volumePathW);

    std::wstring fullNameW = volumePathW;
    SPLSalPathAppendOwned(fullNameW, VolumeNameW.c_str());

    SalamanderGeneral->SalUpdateDefaultDir(TRUE);
    int err;
    BOOL gotFullName = SPLSalGetFullNameOwned(SalamanderGeneral, fullNameW, &err,
                                               CurrentPath.c_str());
    if (!gotFullName)
    {
        std::wstring errorText;
        SPLGetGFNErrorTextOwned(SalamanderGeneral, err, errorText);
        SalamanderGeneral->SalMessageBox(Dlg, errorText.c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ERROR).c_str(), MB_OK | MB_ICONERROR);
        return TRUE;
    }

    DWORD attr = SalamanderGeneral->SalGetFileAttributes(fullNameW.c_str());
    if (attr == 0xFFFFFFFF || (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        SalamanderGeneral->SalMessageBox(Dlg, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_NOTFOUND).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ERROR).c_str(), MB_OK | MB_ICONERROR);
        return TRUE;
    }

    *VolumePath = volumePathW;

    EndDialog(Dlg, IDOK);
    return TRUE;
}

INT_PTR NextVolumeDialog(HWND parent, char* volumeName, std::wstring& volumePath, char* diskName, int cabNumber)
{
    CALL_STACK_MESSAGE1("NextVolumeDialog(, )");
    std::wstring volumeNameW;
    std::wstring diskNameW;
    if (!ProjectCabBytesToWide(volumeName, volumeNameW) ||
        !ProjectCabBytesToWide(diskName, diskNameW))
        return IDCANCEL;
    CNextVolumeDialog dlg(parent, volumeNameW, &volumePath, volumePath, diskNameW,
                          cabNumber);
    HWND mainWnd = SalamanderGeneral->GetWndToFlash(parent);
    INT_PTR ret = dlg.Proceed();
    if (mainWnd != NULL)
        FlashWindow(mainWnd, FALSE);
    return ret;
}

// ****************************************************************************
//
// CContinuedFileDialog
//

INT_PTR WINAPI ContinuedFileDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("ContinuedFileDlgProc(, 0x%X, 0x%IX, 0x%IX)", uMsg, wParam,
                        lParam);
    static CContinuedFileDialog* dlg = NULL;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        // SalamanderGUI->ArrangeHorizontalLines(hDlg); // it should be called, but we ignore it here, there are no horizontal lines
        dlg = (CContinuedFileDialog*)lParam;
        dlg->Dlg = hDlg;
        return dlg->DialogProc(uMsg, wParam, lParam);

    default:
        if (dlg)
            return dlg->DialogProc(uMsg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR
CContinuedFileDialog::Proceed()
{
    CALL_STACK_MESSAGE1("CContinuedFileDialog::Proceed()");
    return DialogBoxParamW(HLanguage, MAKEINTRESOURCEW(IDD_ERROR),
                           Parent, ContinuedFileDlgProc, (LPARAM)this);
}

INT_PTR
CContinuedFileDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CContinuedFileDialog::DialogProc(0x%X, 0x%IX, 0x%IX)",
                        uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
        return OnInit(wParam, lParam);
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDSKIP:
            if (SendDlgItemMessageW(Dlg, IDC_DONTSHOW, BM_GETCHECK, 0, 0) == BST_CHECKED)
                Options |= OP_SKIPCONTINUED;
            EndDialog(Dlg, IDSKIP);
            return FALSE;

        case IDALL:
            if (SendDlgItemMessageW(Dlg, IDC_DONTSHOW, BM_GETCHECK, 0, 0) == BST_CHECKED)
                Options |= OP_SKIPCONTINUED;
            EndDialog(Dlg, IDALL);
            return FALSE;

        case IDCANCEL:
            EndDialog(Dlg, IDCANCEL);
            return FALSE;
        }
        break;

    case WM_DESTROY:
        SubClassStatic(IDS_FILENAME, FALSE);
        return TRUE;
    }
    return FALSE;
}

BOOL CContinuedFileDialog::OnInit(WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE3("CContinuedFileDialog::OnInit(0x%IX, 0x%IX)", wParam, lParam);
    SubClassStatic(IDS_FILENAME, TRUE);
    SendDlgItemMessageW(Dlg, IDS_FILENAME, WM_SETTEXT, 0, (LPARAM)File.c_str());

    CenterDlgToParent();
    return TRUE;
}

/*
BOOL 
CContinuedFileDialog::OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
  if (SendDlgItemMessage(Dlg, IDC_DONTSHOW, BM_GETCHECK, 0, 0) == BST_CHECKED)
    Options |= OP_SKIPCONTINUED;
  
  EndDialog(Dlg, IDOK);
  return TRUE;
}
*/

INT_PTR ContinuedFileDialog(HWND parent, const std::wstring& file)
{
    CALL_STACK_MESSAGE2("ContinuedFileDialog(, %ls)", file.c_str());
    CContinuedFileDialog dlg(parent, file);
    HWND mainWnd = SalamanderGeneral->GetWndToFlash(parent);
    INT_PTR ret = dlg.Proceed();
    if (mainWnd != NULL)
        FlashWindow(mainWnd, FALSE);
    return ret;
}

// ****************************************************************************
//
// CConfigDialog
//

INT_PTR WINAPI ConfigDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("ConfigDlgProc(, 0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    static CConfigDialog* dlg = NULL;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        // SalamanderGUI->ArrangeHorizontalLines(hDlg); // it should be called, but we ignore it here, there are no horizontal lines
        dlg = (CConfigDialog*)lParam;
        dlg->Dlg = hDlg;
        return dlg->DialogProc(uMsg, wParam, lParam);

    default:
        if (dlg)
            return dlg->DialogProc(uMsg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR
CConfigDialog::Proceed()
{
    CALL_STACK_MESSAGE1("CConfigDialog::Proceed()");
    return DialogBoxParamW(HLanguage, MAKEINTRESOURCEW(IDD_CONFIG),
                           Parent, ConfigDlgProc, (LPARAM)this);
}

INT_PTR
CConfigDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConfigDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
        return OnInit(wParam, lParam);
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            return OnOK(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);

        case IDCANCEL:
            EndDialog(Dlg, IDCANCEL);
            return FALSE;
        }
        break;
    }
    return FALSE;
}

BOOL CConfigDialog::OnInit(WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE3("CConfigDialog::OnInit(0x%IX, 0x%IX)", wParam, lParam);
    SendDlgItemMessageW(Dlg, IDC_SKIPCONTINUED, BM_SETCHECK, Options & OP_SKIPCONTINUED ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageW(Dlg, IDC_NOVOLATTENTION, BM_SETCHECK, Options & OP_NO_VOL_ATTENTION ? BST_CHECKED : BST_UNCHECKED, 0);

    CenterDlgToParent();
    return TRUE;
}

BOOL CConfigDialog::OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CConfigDialog::OnOK(0x%X, 0x%X, )", wNotifyCode, wID);
    if (SendDlgItemMessageW(Dlg, IDC_SKIPCONTINUED, BM_GETCHECK, 0, 0) == BST_CHECKED)
        Options |= OP_SKIPCONTINUED;
    else
        Options &= ~OP_SKIPCONTINUED;
    if (SendDlgItemMessageW(Dlg, IDC_NOVOLATTENTION, BM_GETCHECK, 0, 0) == BST_CHECKED)
        Options |= OP_NO_VOL_ATTENTION;
    else
        Options &= ~OP_NO_VOL_ATTENTION;

    EndDialog(Dlg, IDOK);
    return TRUE;
}

INT_PTR ConfigDialog(HWND parent)
{
    CALL_STACK_MESSAGE1("ConfigDialog()");
    CConfigDialog dlg(parent);
    return dlg.Proceed();
}

// ****************************************************************************
//
// CAttentionDialog
//

INT_PTR WINAPI AttentionDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("AttentionDlgProc(, 0x%X, 0x%IX, 0x%IX)", uMsg, wParam,
                        lParam);
    static CAttentionDialog* dlg = NULL;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        // SalamanderGUI->ArrangeHorizontalLines(hDlg); // it should be called, but we ignore it here, there are no horizontal lines
        dlg = (CAttentionDialog*)lParam;
        dlg->Dlg = hDlg;
        return dlg->DialogProc(uMsg, wParam, lParam);

    default:
        if (dlg)
            return dlg->DialogProc(uMsg, wParam, lParam);
    }
    return FALSE;
}

INT_PTR
CAttentionDialog::Proceed()
{
    CALL_STACK_MESSAGE1("CAttentionDialog::Proceed()");
    return DialogBoxParamW(HLanguage, MAKEINTRESOURCEW(IDD_WARNING),
                           Parent, AttentionDlgProc, (LPARAM)this);
}

INT_PTR
CAttentionDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CAttentionDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
        CenterDlgToParent();
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            return OnOK(HIWORD(wParam), LOWORD(wParam), (HWND)lParam);

        case IDCANCEL:
            EndDialog(Dlg, IDCANCEL);
            return FALSE;
        }
        break;
    }
    return FALSE;
}

BOOL CAttentionDialog::OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl)
{
    CALL_STACK_MESSAGE3("CAttentionDialog::OnOK(0x%X, 0x%X, )", wNotifyCode, wID);
    if (SendDlgItemMessageW(Dlg, IDC_DONTSHOW, BM_GETCHECK, 0, 0) == BST_CHECKED)
        Options |= OP_NO_VOL_ATTENTION;

    EndDialog(Dlg, IDOK);
    return TRUE;
}

INT_PTR AttentionDialog(HWND parent)
{
    CALL_STACK_MESSAGE1("AttentionDialog()");
    CAttentionDialog dlg(parent);
    HWND mainWnd = SalamanderGeneral->GetWndToFlash(parent);
    INT_PTR ret = dlg.Proceed();
    if (mainWnd != NULL)
        FlashWindow(mainWnd, FALSE);
    return ret;
}
