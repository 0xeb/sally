// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "unicode/helpers.h" // WideToAnsi

#include "undelete.rh"
#include "undelete.rh2"
#include "lang\lang.rh"

#include "miscstr.h"
#include "os.h"
#include "volume.h"
#include "snapshot.h"
#include "dialogs.h"
#include "undelete.h"

#include "library\volenum.h"

#pragma comment(lib, "uxtheme.lib")

namespace
{
std::wstring GetListViewItemTextOwned(HWND list, int item, int subItem)
{
    size_t capacity = 64;
    const size_t limit = static_cast<size_t>((std::numeric_limits<int>::max)());
    for (;;)
    {
        std::vector<wchar_t> buffer(capacity, L'\0');
        LVITEMW entry = {};
        entry.iSubItem = subItem;
        entry.pszText = buffer.data();
        entry.cchTextMax = static_cast<int>(buffer.size());
        const LRESULT copied = SendMessageW(
            list, LVM_GETITEMTEXTW, item, reinterpret_cast<LPARAM>(&entry));
        if (copied <= 0)
            return std::wstring();
        if (copied < static_cast<LRESULT>(buffer.size() - 1))
            return std::wstring(buffer.data(), static_cast<size_t>(copied));
        if (capacity > limit / 2)
            return std::wstring();
        capacity *= 2;
    }
}
}

// ****************************************************************************
//
//  CSnapshotProgressDlg
//

CSnapshotProgressDlg::CSnapshotProgressDlg(HWND parent, CObjectOrigin origin)
    : CDialog(HLanguage, IDD_SNAPSHOTPROGRESSDLG, parent, origin)
{
    ProgressBar = NULL;
    WantCancel = FALSE;
}

void CSnapshotProgressDlg::SetProgressText(int resID)
{
    CALL_STACK_MESSAGE2("CSnapshotProgressDlg::SetProgressText(%d)", resID);
    SetDlgItemTextW(HWindow, IDC_LABEL_FILENAME, String<wchar_t>::LangStr(resID).c_str());
}

void CSnapshotProgressDlg::SetProgressText(int resID, int number)
{
    CALL_STACK_MESSAGE3("CSnapshotProgressDlg::SetProgressText(%d, %d)", resID, number);
    CQuadWord qwnumber = CQuadWord(number, 0);
    const std::wstring plural = SPLExpandPluralStringOwned(
        SalamanderGeneral, String<wchar_t>::LangStr(resID).c_str(), 1, &qwnumber);
    const std::wstring text = SPLFormatStringOwned(plural.c_str(), number);
    SetDlgItemTextW(HWindow, IDC_LABEL_FILENAME, text.c_str());
}

void CSnapshotProgressDlg::SetProgress(DWORD progress)
{
    CALL_STACK_MESSAGE2("CSnapshotProgressDlg::SetProgress(0x%X)", progress);
    ProgressBar->SetProgress(progress, NULL);
}

BOOL CSnapshotProgressDlg::GetWantCancel()
{
    CALL_STACK_MESSAGE_NONE
    //CALL_STACK_MESSAGE1("CSnapshotProgressDlg::GetWantCancel()");
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) // we want responsive GUI
    {
        if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return WantCancel;
}

INT_PTR CSnapshotProgressDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CSnapshotProgressDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        ProgressBar = SalamanderGUI->AttachProgressBar(HWindow, IDC_PROGRESSBAR);
        if (ProgressBar == NULL)
        {
            DestroyWindow(HWindow);
            return FALSE;
        }
        if (Parent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        break; // we want focus from DefDlgProc
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDCANCEL)
        {
            WantCancel = TRUE;
            return TRUE;
        }
        break;
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
//  CCopyProgressDlg
//

CCopyProgressDlg::CCopyProgressDlg(HWND parent, CObjectOrigin origin)
    : CDialog(HLanguage, IDD_COPYPROGRESSDLG, parent, origin)
{
    CALL_STACK_MESSAGE1("CCopyProgressDlg::CCopyProgressDlg(, )");
    ProgressBar1 = ProgressBar2 = NULL;
    WantCancel = FALSE;
    FileProgress = TotalProgress = 0;
    Changed[0] = Changed[1] = Changed[2] = Changed[3] = 0;
    LastTick = 0;
}

void CCopyProgressDlg::SetSourceFileName(const wchar_t* fileName)
{
    CALL_STACK_MESSAGE2("CCopyProgressDlg::SetSourceFileName(%ls)", fileName);
    SrcName = fileName != NULL ? fileName : L"";
    Changed[0] = TRUE;
    UpdateControls();
}

void CCopyProgressDlg::SetDestFileName(const wchar_t* fileName)
{
    CALL_STACK_MESSAGE2("CCopyProgressDlg::SetDestFileName(%ls)", fileName);
    DestName = fileName != NULL ? fileName : L"";
    Changed[1] = TRUE;
    UpdateControls();
}

void CCopyProgressDlg::SetFileProgress(DWORD progress)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CCopyProgressDlg::SetFileProgress(0x%X)", progress);
    FileProgress = progress;
    Changed[2] = TRUE;
    UpdateControls();
}

void CCopyProgressDlg::SetTotalProgress(DWORD progress)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CCopyProgressDlg::SetTotalProgress(0x%X)", progress);
    TotalProgress = progress;
    Changed[3] = TRUE;
    UpdateControls();
}

void CCopyProgressDlg::UpdateControls(BOOL now)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CCopyProgressDlg::UpdateControls(%d)", now);
    DWORD tick = GetTickCount();
    if (now || tick - LastTick >= 50)
    {
        if (Changed[0])
        {
            Label1->SetText(SrcName.c_str());
            Changed[0] = FALSE;
        }
        if (Changed[1])
        {
            Label2->SetText(DestName.c_str());
            Changed[1] = FALSE;
        }
        if (Changed[2])
        {
            ProgressBar1->SetProgress(FileProgress, NULL);
            Changed[2] = FALSE;
        }
        if (Changed[3])
        {
            ProgressBar2->SetProgress(TotalProgress, NULL);
            Changed[3] = FALSE;
        }
        LastTick = tick;
    }
}

BOOL CCopyProgressDlg::GetWantCancel()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CCopyProgressDlg::GetWantCancel()");

    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) // we want responsive GUI
    {
        if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    UpdateControls();
    return WantCancel;
}

INT_PTR CCopyProgressDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCopyProgressDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        ProgressBar1 = SalamanderGUI->AttachProgressBar(HWindow, IDC_PROGRESS_FILE);
        ProgressBar2 = SalamanderGUI->AttachProgressBar(HWindow, IDC_PROGRESS_TOTAL);
        Label1 = SalamanderGUI->AttachStaticText(HWindow, IDC_LABEL_SOURCE, STF_PATH_ELLIPSIS);
        Label2 = SalamanderGUI->AttachStaticText(HWindow, IDC_LABEL_DEST, STF_PATH_ELLIPSIS);
        if (ProgressBar1 == NULL || ProgressBar2 == NULL || Label1 == NULL || Label2 == NULL)
        {
            DestroyWindow(HWindow);
            return FALSE;
        }
        if (Parent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        break; // request focus from DefDlgProc
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDCANCEL)
        {
            if (!WantCancel)
            {
                UpdateControls(TRUE);
                if (SalamanderGeneral->SalMessageBox(HWindow, String<wchar_t>::LangStr(IDS_WANTCANCEL).c_str(),
                                                     String<wchar_t>::LangStr(IDS_QUESTION).c_str(),
                                                     MB_YESNO | MB_ICONQUESTION) == IDYES)
                {
                    WantCancel = TRUE;
                }
            }
            return TRUE;
        }
        break;
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
//  CConnectDialog
//

CConnectDialog::CConnectDialog(HWND parent, int panel)
    : CDialog(HLanguage, IDD_CONNECT, IDD_CONNECT, parent)
{
    hDrivesImg = NULL;
    Panel = panel;
}

void CConnectDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CConnectDialog::Transfer()");
    ti.CheckBox(IDC_CHECK_SCANVACANT, ConfigScanVacantClusters);
    ti.CheckBox(IDC_CHECK_SHOWEXISTING, ConfigShowExistingFiles);
    ti.CheckBox(IDC_CHECK_SHOWZEROFILES, ConfigShowZeroFiles);
    ti.CheckBox(IDC_CHECK_SHOWEMPTYDIRS, ConfigShowEmptyDirs);
    ti.CheckBox(IDC_CHECK_SHOWMETAFILES, ConfigShowMetafiles);
    ti.CheckBox(IDC_CHECK_ESTIMATEDAMAGE, ConfigEstimateDamage);
}

void CConnectDialog::AddVolumeDetails(const wchar_t* root, const wchar_t* volumeName, const wchar_t* volumeFS,
                                      const CQuadWord& bytesTotal, const CQuadWord& bytesFree,
                                      const wchar_t* volumeGUIDPath, int serial, BOOL selected)
{
    LVITEMW itemInfo = {};
    itemInfo.iItem = serial;

    itemInfo.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_STATE;
    itemInfo.iSubItem = 0;
    itemInfo.pszText = const_cast<wchar_t*>(root);
    itemInfo.iImage = serial;
    itemInfo.stateMask = LVIS_FOCUSED; // using LVIS_FOCUSED instead of LVIS_SELECTED (we can switch to multi-select ListView)
    if (selected)
        itemInfo.state = LVIS_FOCUSED;
    else
        itemInfo.state = 0;
    itemInfo.iItem = (int)SendMessageW(hList, LVM_INSERTITEMW, 0, (LPARAM)&itemInfo);

    itemInfo.mask = LVIF_TEXT;

    itemInfo.iSubItem = 1;
    itemInfo.pszText = const_cast<wchar_t*>(volumeName);
    SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&itemInfo);

    itemInfo.iSubItem = 2;
    itemInfo.pszText = const_cast<wchar_t*>(volumeFS);
    SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&itemInfo);

    itemInfo.iSubItem = 3;
    static wchar_t emptyBuff[] = L"";
    std::wstring sizeText;
    if (bytesTotal != CQuadWord(-1, -1))
    {
        sizeText = SPLPrintDiskSizeOwned(SalamanderGeneral, bytesTotal, 0);
        itemInfo.pszText = const_cast<wchar_t*>(sizeText.c_str());
    }
    else
        itemInfo.pszText = emptyBuff;
    SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&itemInfo);

    itemInfo.iSubItem = 4;
    if (bytesFree != CQuadWord(-1, -1))
    {
        sizeText = SPLPrintDiskSizeOwned(SalamanderGeneral, bytesFree, 0);
        itemInfo.pszText = const_cast<wchar_t*>(sizeText.c_str());
    }
    else
        itemInfo.pszText = emptyBuff;
    SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&itemInfo);

    itemInfo.iSubItem = 5;
    std::wstring freePercentText;
    if (bytesTotal != CQuadWord(-1, -1))
    {
        double pct = (CQuadWord(1000, 0) * bytesFree / bytesTotal).GetDouble() / 10;
        std::wstring freePercent = SPLFormatStringOwned(L"%5.1f %%", pct);
        SPLPointToLocalDecimalSeparatorOwned(SalamanderGeneral, freePercent);
        freePercentText = freePercent;
        itemInfo.pszText = const_cast<wchar_t*>(freePercentText.c_str());
    }
    else
        itemInfo.pszText = emptyBuff;
    SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&itemInfo);

    itemInfo.iSubItem = 6;
    itemInfo.pszText = const_cast<wchar_t*>(volumeGUIDPath);
    SendMessageW(hList, LVM_SETITEMW, 0, (LPARAM)&itemInfo);
}

void CConnectDialog::InitDrives()
{
    CALL_STACK_MESSAGE1("CConnectDialog::InitDrives()");

    // set controls status
    EnableWindow(GetDlgItem(HWindow, IDC_EDIT_IMAGE), FALSE);
    EnableWindow(GetDlgItem(HWindow, IDC_BUTTON_BROWSE), FALSE);
    hList = GetDlgItem(HWindow, IDC_LIST_VOLUMES);
    SendMessage(hList, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT);

    // initialize the list view

    // insert columns (column widths will be set at the end using autosize)
    const int columnsCount = 7;
    int headerResId[columnsCount] = {IDS_VOLUME_MOUNT, IDS_VOLUME_NAME, IDS_VOLUME_FORMAT, IDS_VOLUME_SIZE, IDS_VOLUME_FREE, IDS_VOLUME_FREEPROC, IDS_VOLUME_VOLID};
    LVCOLUMNW columnInfo;
    columnInfo.mask = LVCF_TEXT | LVCF_FMT;
    int i;
    for (i = 0; i < columnsCount; i++)
    {
        const std::wstring headerText = String<wchar_t>::LangStr(headerResId[i]);
        columnInfo.pszText = const_cast<wchar_t*>(headerText.c_str());
        if (i >= 3 && i <= 5)
            columnInfo.fmt = LVCFMT_RIGHT;
        else
            columnInfo.fmt = LVCFMT_LEFT;
        SendMessage(hList, LVM_INSERTCOLUMNW, i, (LPARAM)&columnInfo);
    }

    // prepare the image list and fill in the list view
    hDrivesImg = ImageList_Create(16, 16, SalamanderGeneral->GetImageListColorFlags() | ILC_MASK, 0, 1);

    // get info about current panel and focused item
    int sourcePanelType;
    std::wstring sourcePanelPath;
    size_t archiveOrFSOffset = std::wstring::npos;
    BOOL ret = SPLGetPanelPathOwned(SalamanderGeneral, Panel, sourcePanelPath,
                                    &sourcePanelType, &archiveOrFSOffset);
    if (ret)
    {
        switch (sourcePanelType)
        {
        // DOS/WIN or UNC path
        case PATH_TYPE_WINDOWS:
        {
            // pre-fill the disk image path with focused file
            BOOL isDir;
            const CFileData* data = SalamanderGeneral->GetPanelFocusedItem(Panel, &isDir);
            if (data && !isDir)
            {
                SPLSalPathAppendOwned(sourcePanelPath, data->Name);
                SetDlgItemTextW(HWindow, IDC_EDIT_IMAGE, sourcePanelPath.c_str());
            }
            break;
        }

        case PATH_TYPE_FS:
        {
            // remove the "del:" prefix so correct path will be selected when opening this dialog box on Undelete path
            if (archiveOrFSOffset != std::wstring::npos)
                sourcePanelPath.erase(0, archiveOrFSOffset + 1);
            break;
        }

        // can't do much about this
        default:
            break;
        }
    }

    std::wstring sourcePanelGUIDPath;
    SPLGetResolvedPathMountPointAndGUIDOwned(
        SalamanderGeneral, sourcePanelPath.c_str(), NULL, &sourcePanelGUIDPath);

    SendMessage(hList, LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)hDrivesImg);

    VolumeListing<wchar_t> volumeListing;
    DWORD err = GetVolumeListing(volumeListing);
    if (err != 0)
    {
        std::wstring message = String<wchar_t>::LangStr(IDS_VOLENUMERATE);
        if (err != ERROR_SUCCESS)
        {
            message += L" ";
            message += SPLGetErrorTextOwned(SalamanderGeneral, err);
        }
        SalamanderGeneral->SalMessageBox(HWindow, message.c_str(), String<wchar_t>::LangStr(IDS_UNDELETE).c_str(), MSGBOXEX_OK);
    }
    else
    {
        int serial = 0;
        for (i = 0; i < volumeListing.Count; ++i)
        {
            // for simple disks get the icon from system, for mount points or unmounted disks get just plain HDD icon
            // (passing MountPoints instead of volumeName is much faster for some disks)
            HICON icn;
            if (volumeListing[i]->MountPoint.size() == 3)
                icn = OS<wchar_t>::OS_GetDriveIcon(volumeListing[i]->MountPoint.c_str(), volumeListing[i]->Type, TRUE, FALSE);
            else
                icn = OS<wchar_t>::OS_GetDriveIcon(L"", DRIVE_FIXED, TRUE, FALSE);

            ImageList_AddIcon(hDrivesImg, icn);
            DestroyIcon(icn);

            // add volume details to the list view
            BOOL selected = FALSE;
            if (!sourcePanelGUIDPath.empty())
            {
                selected = (sourcePanelGUIDPath == volumeListing[i]->GUIDPath);
            }
            else
            {
                if (!volumeListing[i]->MountPoint.empty() &&
                    SalamanderGeneral->PathIsPrefix(volumeListing[i]->MountPoint.c_str(), sourcePanelPath.c_str()))
                {
                    selected = true;
                }
            }

            if (volumeListing[i]->MountPoint.size() > 3)
                SPLSalPathRemoveBackslashOwned(SalamanderGeneral, volumeListing[i]->MountPoint);

            AddVolumeDetails(volumeListing[i]->MountPoint.c_str(), volumeListing[i]->VolumeName.c_str(), volumeListing[i]->FSName.c_str(),
                             volumeListing[i]->BytesTotal, volumeListing[i]->BytesFree, volumeListing[i]->GUIDPath.c_str(),
                             serial, selected);

            serial++;
        }
    }

    // we have columns header and content, we can (auto)adjust columns width
    for (i = 0; i < columnsCount; i++)
        ListView_SetColumnWidth(hList, i, LVSCW_AUTOSIZE_USEHEADER);

    // select item and ensure it is visible
    int selectIndex = ListView_GetNextItem(hList, -1, LVIS_FOCUSED);
    if (selectIndex == -1)
        selectIndex = 0;
    ListView_SetItemState(hList, selectIndex, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
    ListView_EnsureVisible(hList, selectIndex, FALSE);
}

BOOL CConnectDialog::OnDialogOK()
{
    CALL_STACK_MESSAGE1("CConnectDialog::OnDialogOK()");

    // check if dealing with volume or image
    if (BST_CHECKED == SendMessage(GetDlgItem(HWindow, IDC_CHECK_IMAGE), BM_GETCHECK, 0, 0))
    {
        // disk image
        Volume = SPLGetDlgItemTextOwned(HWindow, IDC_EDIT_IMAGE);

        // reset the volume if the image file does not exist
        DWORD attr = SalamanderGeneral->SalGetFileAttributes(Volume.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES ||
            attr & FILE_ATTRIBUTE_DIRECTORY)
        {
            SalamanderGeneral->SalMessageBox(HWindow, String<wchar_t>::LangStr(IDS_IMAGENOTFOUND).c_str(),
                                             String<wchar_t>::LangStr(IDS_UNDELETE).c_str(), MSGBOXEX_ICONEXCLAMATION | MSGBOXEX_OK);
            return FALSE;
        }
    }
    else
    {
        // physical disk
        HWND hList2 = GetDlgItem(HWindow, IDC_LIST_VOLUMES);
        const int item = (int)SendMessageW(hList2, LVM_GETNEXTITEM, -1,
                                           LVNI_ALL | LVNI_SELECTED);
        Volume = GetListViewItemTextOwned(hList2, item, 0);
        if (Volume.empty())
            Volume = GetListViewItemTextOwned(hList2, item, 6);

        // Append the current path if the selected volume owns it.
        int sourcePanelType;
        std::wstring sourcePanelPathW;
        BOOL ret = SPLGetPanelPathOwned(SalamanderGeneral, Panel,
                                        sourcePanelPathW, &sourcePanelType);
        if (ret && sourcePanelType == PATH_TYPE_WINDOWS)
        {
            // if mount points are supported, check if we are on the correct volume
            std::wstring vol1, vol2;
            if (SPLGetVolumePathNameOwned(sourcePanelPathW.c_str(), vol1) &&
                SPLGetVolumePathNameOwned(Volume.c_str(), vol2) && vol1 == vol2)
                Volume = sourcePanelPathW;
        }
    }
    return TRUE;
}

void CConnectDialog::OnImageBrowse()
{
    Volume = SPLGetDlgItemTextOwned(HWindow, IDC_EDIT_IMAGE);

    OPENFILENAMEW openInfo;
    memset(&openInfo, 0, sizeof(OPENFILENAMEW));
    openInfo.lStructSize = sizeof(OPENFILENAMEW);
    openInfo.hwndOwner = HWindow;
    openInfo.lpstrFilter = L"Image Files (*.img;*.ima)\0*.IMG;*.IMA\0AllFiles (*.*)\0*.*\0\0\0";
    openInfo.lpstrFile = NULL;
    openInfo.nMaxFile = 0;
    openInfo.Flags = OFN_FILEMUSTEXIST | OFN_READONLY;
    std::vector<std::wstring> selectedFiles;
    if (!Volume.empty())
        selectedFiles.push_back(Volume);
    BOOL ret = SPLSafeGetOpenFileNamesOwned(SalamanderGeneral, &openInfo, selectedFiles) &&
               selectedFiles.size() == 1;
    if (!ret && FNERR_INVALIDFILENAME == CommDlgExtendedError())
    {
        // Windows refuse to open dialog with initial path e.g. C:\. Oh well...
        selectedFiles.clear();
        ret = SPLSafeGetOpenFileNamesOwned(SalamanderGeneral, &openInfo, selectedFiles) &&
              selectedFiles.size() == 1;
    }
    if (ret)
    {
        Volume.swap(selectedFiles[0]);
        SetDlgItemTextW(HWindow, IDC_EDIT_IMAGE, Volume.c_str());
    }
}

INT_PTR CConnectDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConnectDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (Parent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        if (IsAppThemed())
            SetWindowTheme(GetDlgItem(HWindow, IDC_LIST_VOLUMES), L"explorer", NULL);
        InitDrives();
        break;
    }

    case WM_DESTROY:
    {
        // if (hDrivesImg) ImageList_Destroy(hDrivesImg);  // image-list will be released by listview
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            EndDialog(HWindow, FALSE);
            return TRUE;

        case IDOK:
            if (!OnDialogOK())
                return TRUE;
            break;

        case IDC_CHECK_IMAGE:
        {
            BOOL checked = (SendDlgItemMessage(HWindow, IDC_CHECK_IMAGE, BM_GETCHECK, 0, 0) == BST_CHECKED);
            EnableWindow(GetDlgItem(HWindow, IDC_LIST_VOLUMES), !checked);
            EnableWindow(GetDlgItem(HWindow, IDC_EDIT_IMAGE), checked);
            EnableWindow(GetDlgItem(HWindow, IDC_BUTTON_BROWSE), checked);
            return TRUE;
        }

        case IDC_BUTTON_BROWSE:
            OnImageBrowse();
            return TRUE;
        }
        break;
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
//  CFileNameDialog
//

CFileNameDialog::CFileNameDialog(HWND parent, std::wstring& filename)
    : CDialog(HLanguage, IDD_FILENAME, parent), FileName(filename)
{
    AllPressed = FALSE;
}

void CFileNameDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CFileNameDialog::Transfer()");
    ti.EditLine(IDC_EDIT_FILENAME, FileName);
}

INT_PTR CFileNameDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CFileNameDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (Parent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        TransferData(ttDataToWindow);
        SetFocus(GetDlgItem(HWindow, IDC_EDIT_FILENAME));
        SendDlgItemMessage(HWindow, IDC_EDIT_FILENAME, EM_SETSEL, 0, 1);
        AllPressed = FALSE;
        return FALSE;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            EndDialog(HWindow, FALSE);
            return TRUE;

        case IDC_BUTTON_ALL:
            AllPressed = TRUE;
            return CDialog::DialogProc(uMsg, IDOK, 0);
        }
        break;
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
//  CConfigDialog
//

CConfigDialog::CConfigDialog(HWND parent)
    : CDialog(HLanguage, IDD_CONFIG, IDD_CONFIG, parent)
{
}

void CConfigDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CConfigDialog::Transfer()");
    if (ti.Type == ttDataToWindow)
        SetDlgItemTextW(HWindow, IDC_EDIT_TEMPPATH, ConfigTempPath.c_str());
    else if (ti.Type == ttDataFromWindow)
        ConfigTempPath = SPLGetDlgItemTextOwned(HWindow, IDC_EDIT_TEMPPATH);
    ti.CheckBox(IDC_CHECK_ALWAYSREUSE, ConfigAlwaysReuseScanInfo);
    ti.CheckBox(IDC_CHECK_SAMEPARTITION, ConfigDontShowSamePartitionWarning);
    ti.CheckBox(IDC_CHECK_EFS, ConfigDontShowEncryptedWarning);
}

INT_PTR CConfigDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConfigDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (Parent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_BUTTON_BROWSE:
        {
            const std::wstring initial = SPLGetDlgItemTextOwned(HWindow, IDC_EDIT_TEMPPATH);
            std::wstring selected;
            if (SPLGetTargetDirectoryOwned(SalamanderGeneral, HWindow, HWindow,
                                           String<wchar_t>::LangStr(IDS_UNDELETE).c_str(),
                                           String<wchar_t>::LangStr(IDS_CHOOSETEMPDIR).c_str(),
                                           selected, FALSE, initial.c_str()))
            {
                SetDlgItemTextW(HWindow, IDC_EDIT_TEMPPATH, selected.c_str());
            }
            return TRUE;
        }
        }
        break;
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
//  CRestoreDialog
//

CRestoreDialog::CRestoreDialog(HWND parent)
    : CDialog(HLanguage, IDD_RESTORE, IDD_RESTORE, parent)
{
}

INT_PTR CRestoreDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CRestoreDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    std::wstring path;
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (Parent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, Parent, TRUE);

        std::wstring targetPathW;
        SPLGetPanelPathOwned(SalamanderGeneral, PANEL_TARGET, targetPathW);
        SetDlgItemTextW(HWindow, IDC_EDIT_TARGET, targetPathW.c_str());

        int files, dirs;
        wchar_t text1[200];
        std::wstring text2;
        SalamanderGeneral->GetPanelSelection(PANEL_SOURCE, &files, &dirs);
        SPLGetCommonFSOperSourceDescrOwned(
            SalamanderGeneral, PANEL_SOURCE, files, dirs, NULL, FALSE, FALSE,
            text2);
        GetDlgItemTextW(HWindow, IDC_LABEL_SOURCE, text1, _countof(text1));
        BOOL labelSet = FALSE;
        // if it is "file \"name.txt\"" or "directory \"name\"" we find the name and the remaining text
        // we add the strings to text1 so that CSalamanderGUI::SetSubjectTruncatedText can be used
        if (files + dirs <= 1)
        {
            const size_t beg = text2.find(L'"');
            const size_t end = text2.rfind(L'"');
            if (beg != std::wstring::npos && end != std::wstring::npos && beg < end)
            {
                const std::wstring fileName = text2.substr(beg + 1, end - beg - 1);
                text2.replace(beg + 1, end - beg - 1, L"%s");
                path = SPLFormatStringOwned(text1, text2.c_str());

                BOOL isDir = dirs == 1;
                if (files + dirs == 0)
                    SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, &isDir);
                SalamanderGUI->SetSubjectTruncatedText(GetDlgItem(HWindow, IDC_LABEL_SOURCE), path.c_str(),
                                                       fileName.c_str(), isDir, TRUE);
                labelSet = TRUE;
            }
        }
        if (!labelSet)
        {
            path = SPLFormatStringOwned(text1, text2.c_str());
            SetDlgItemTextW(HWindow, IDC_LABEL_SOURCE, path.c_str());
        }
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_BUTTON_BROWSE:
        {
            const std::wstring initial = SPLGetDlgItemTextOwned(HWindow, IDC_EDIT_TARGET);
            const std::wstring title = SPLGetWindowTextOwned(HWindow);
            std::wstring selected;
            if (SPLGetTargetDirectoryOwned(SalamanderGeneral, HWindow, HWindow,
                                           title.c_str(),
                                           String<wchar_t>::LangStr(IDS_CHOOSETARGET).c_str(),
                                           selected, FALSE, initial.c_str()))
            {
                SetDlgItemTextW(HWindow, IDC_EDIT_TARGET, selected.c_str());
            }
            return TRUE;
        }

        case IDOK:
        {
            TargetPath = SPLGetDlgItemTextOwned(HWindow, IDC_EDIT_TARGET);
            break;
        }
        }
        break;
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
//  CRestoreProgressDlg
//

INT_PTR CRestoreProgressDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CRestoreProgressDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    if (uMsg == WM_INITDIALOG)
    {
        SetDlgItemTextW(HWindow, IDC_LABEL_UNDELETING, String<wchar_t>::LangStr(IDS_RESTORING).c_str());
        SetWindowTextW(HWindow, String<wchar_t>::LangStr(IDS_RESTORE).c_str());
        /*HWND hLabel = GetDlgItem(HWindow, IDC_LABEL_UNDELETING);
    SetWindowLong(hLabel, GWL_STYLE, GetWindowLong(hLabel, GWL_STYLE) | SS_RIGHT);*/
    }
    return CCopyProgressDlg::DialogProc(uMsg, wParam, lParam);
}
