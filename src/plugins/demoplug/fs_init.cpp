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

// FS name assigned by Salamander after the plugin loads
std::wstring AssignedFSName;
extern int AssignedFSNameLen = 0;

// image list for simple FS icons
HIMAGELIST DFSImageList = NULL;

// global variables used to cache pointers to Salamander-wide variables
// shared between the archiver and the FS
const CFileData** TransferFileData = NULL;
int* TransferIsDir = NULL;
wchar_t* TransferBuffer = NULL;
int* TransferLen = NULL;
DWORD* TransferRowData = NULL;
CPluginDataInterfaceAbstract** TransferPluginDataIface = NULL;
DWORD* TransferActCustomData = NULL;

// helper variable for tests
CPluginFSInterfaceAbstract* LastDetachedFS = NULL;

// structure used to hand over data from the "Connect" dialog to a newly created FS
CConnectData ConnectData;

// ****************************************************************************
// FILE SYSTEM SECTION
// ****************************************************************************

BOOL InitFS()
{
    DFSImageList = ImageList_Create(16, 16, ILC_MASK | SalamanderGeneral->GetImageListColorFlags(), 2, 0);
    if (DFSImageList == NULL)
    {
        TRACE_E("Unable to create image list.");
        return FALSE;
    }
    ImageList_SetImageCount(DFSImageList, 2); // initialize
    ImageList_SetBkColor(DFSImageList, SalamanderGeneral->GetCurrentColor(SALCOL_ITEM_BK_NORMAL));

    // icons differ across Windows versions, ideally we would load them dynamically (e.g. the
    // directory icon from the system directory); here we simply use a single icon variant
    // (which does not match Windows 2000 for example)
    ImageList_ReplaceIcon(DFSImageList, 0, HANDLES(LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_DIR))));
    ImageList_ReplaceIcon(DFSImageList, 1, HANDLES(LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_FILE))));

    return TRUE;
}

void ReleaseFS()
{
    ImageList_Destroy(DFSImageList);
}

//
// ****************************************************************************
// CPluginInterfaceForFS
//

CPluginFSInterfaceAbstract* WINAPI
CPluginInterfaceForFS::OpenFS(const wchar_t* fsName, int fsNameIndex)
{

    // this is where a dedicated FS object should be created for each fsNameIndex...

    // 'fsName' shows how the user typed the FS name ("Ftp", "ftp", "FTP",
    // etc. - it is still the same FS name)

    ActiveFSCount++;
    return new CPluginFSInterface;
}

void WINAPI
CPluginInterfaceForFS::CloseFS(CPluginFSInterfaceAbstract* fs)
{
    CPluginFSInterface* dfsFS = (CPluginFSInterface*)fs; // to ensure the correct destructor is invoked

    if (dfsFS == LastDetachedFS)
        LastDetachedFS = NULL;
    ActiveFSCount--;
    if (dfsFS != NULL)
        delete dfsFS;
}

std::wstring ConnectPath;
wchar_t** History = NULL;
int HistoryCount = 0;

INT_PTR CALLBACK ConnectDlgProc(HWND HWindow, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("ConnectDlgProc(, 0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SalamanderGUI->ArrangeHorizontalLines(HWindow);

        // horizontally and vertically center the dialog relative to the parent
        HWND hParent = GetParent(HWindow);
        if (hParent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(HWindow, hParent, TRUE);

        // populate the dialog with data
        SalamanderGeneral->LoadComboFromStdHistoryValues(GetDlgItem(HWindow, IDC_PATH),
                                                         History, HistoryCount);
        SetDlgItemTextW(HWindow, IDC_PATH, ConnectPath.c_str());

        SalamanderGeneral->InstallWordBreakProc(GetDlgItem(HWindow, IDC_PATH)); // install WordBreakProc into the combo box

        return TRUE; // focus handled by the standard dialog proc
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            // read data from the dialog
            ConnectPath = SPLGetDlgItemTextOwned(HWindow, IDC_PATH);
            SalamanderGeneral->AddValueToStdHistoryValues(History, HistoryCount, ConnectPath.c_str(), FALSE);
        }
        case IDCANCEL:
        {
            EndDialog(HWindow, wParam);
            return TRUE;
        }
        }
        break;
    }
    }
    return FALSE; // not processed
}

void WINAPI
CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(int panel)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(%d)", panel);
    SalamanderGeneral->GetStdHistoryValues(SALHIST_CHANGEDIR, &History, &HistoryCount);
    while (1)
    {
        if (DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_CONNECT),
                           SalamanderGeneral->GetMsgBoxParent(), ConnectDlgProc, NULL) == IDOK)
        {
            // repaint the main window so the user does not keep staring at stale content after the dialog
            UpdateWindow(SalamanderGeneral->GetMainWindowHWND());

            // change the active panel path to AssignedFSName:ConnectPath
            ConnectData.UseConnectData = TRUE;
            ConnectData.UserPart = ConnectPath;
            int failReason;
            BOOL changeRes = SalamanderGeneral->ChangePanelPathToPluginFS(panel, AssignedFSName.c_str(), L"", &failReason);
            // NOTE: on success it returns failReason==CHPPFR_SHORTERPATH (the user-part of the path is not empty)
            ConnectData.UseConnectData = FALSE;
            if (!changeRes && failReason == CHPPFR_INVALIDPATH)
                continue; // repeat the prompt

            /*
      if (!SalamanderGeneral->ChangePanelPathToDisk(panel, ConnectPath, &failReason))
      {
        if (failReason == CHPPFR_INVALIDPATH) continue;  // repeat the prompt
      }
*/
            /*
      if (!SalamanderGeneral->ChangePanelPathToArchive(panel, ConnectPath, "", &failReason))
      {
        if (failReason == CHPPFR_INVALIDPATH || failReason == CHPPFR_INVALIDARCHIVE) continue;  // repeat the prompt
      }
*/
            /*
      if (LastDetachedFS != NULL &&
          !SalamanderGeneral->ChangePanelPathToDetachedFS(panel, LastDetachedFS, &failReason))
      {
        // repeating the prompt makes no sense
      }
*/
            /*
      std::wstring lastPath;   // owned; no caller buffer, no path ceiling
      if (SPLGetLastWindowsPanelPathOwned(SalamanderGeneral, panel, lastPath))
      {
        if (!SalamanderGeneral->ChangePanelPathToDisk(panel, lastPath.c_str(), &failReason))
        {
          // repeating the prompt makes no sense
        }
      }
*/
            /*
      if (!SalamanderGeneral->ChangePanelPathToRescuePathOrFixedDrive(panel, &failReason))
      {
        // repeating the prompt makes no sense
      }
*/
            //      SalamanderGeneral->RefreshPanelPath(panel);
            //      SalamanderGeneral->PostRefreshPanelPath(panel);
            /*
      if (LastDetachedFS != NULL)
      {
        SalamanderGeneral->CloseDetachedFS(SalamanderGeneral->GetMsgBoxParent(), LastDetachedFS);
      }
*/
        }
        break;
    }
}

BOOL WINAPI
CPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                      CPluginFSInterfaceAbstract* pluginFS,
                                                      const wchar_t* pluginFSName, int pluginFSNameIndex,
                                                      BOOL isDetachedFS, BOOL& refreshMenu,
                                                      BOOL& closeMenu, int& postCmd, void*& postCmdParam)
{
    CALL_STACK_MESSAGE7("CPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(, %d, %d, %d, , %ls, %d, %d, , , ,)",
                        panel, x, y, pluginFSName, pluginFSNameIndex, isDetachedFS);

    // create the menu
    wchar_t** strings;
    static wchar_t buffShowInPanel[] = L"&Show in Panel";
    static wchar_t buffDisconnect[] = L"&Disconnect";
    wchar_t* strings1[] = {buffShowInPanel, buffDisconnect, NULL}; // entries for a detached plugin FS
    static wchar_t buffRefresh[] = L"&Refresh";
    wchar_t* strings2[] = {buffRefresh, buffDisconnect, NULL}; // entries for an active plugin FS
    static wchar_t buffConnect[] = L"Connect To...";
    wchar_t* strings3[] = {buffConnect, NULL}; // entries for the FS
    if (pluginFS != NULL)
    {
        if (isDetachedFS)
            strings = strings1;
        else
            strings = strings2;
    }
    else
        strings = strings3;

    HMENU menu = CreatePopupMenu();
    MENUITEMINFOW mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_TYPE | MIIM_ID;
    mi.fType = MFT_STRING;
    int i;
    for (i = 0; strings[i] != NULL; i++)
    {
        wchar_t* p = strings[i];
        mi.wID = i + 1;
        mi.dwTypeData = p;
        mi.cch = (UINT)wcslen(p);
        InsertMenuItemW(menu, i, TRUE, &mi);
    }
    DWORD cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                 x, y, parent, NULL);
    DestroyMenu(menu);
    if (cmd != 0) // the user selected a command from the menu
    {
        refreshMenu = FALSE;
        closeMenu = FALSE;
        postCmd = 0;
        postCmdParam = NULL;

        if (pluginFS != NULL)
        {
            if (isDetachedFS) // entry for a detached plugin FS
            {
                switch (cmd)
                {
                case 1: // show in panel
                {
                    closeMenu = TRUE;
                    postCmd = 1;
                    postCmdParam = (void*)pluginFS;
                    break;
                }

                case 2: // disconnect
                {
                    // if the FS opens windows in the ReleaseObject method (not the case here),
                    // CloseDetachedFS should be called only after the change-drive menu closes
                    SalamanderGeneral->CloseDetachedFS(parent, pluginFS);
                    refreshMenu = TRUE; // if it closed, refresh the Change Drive menu
                    break;
                }
                }
            }
            else // entry for an active plugin FS
            {
                switch (cmd)
                {
                case 1: // refresh
                {
                    closeMenu = TRUE;
                    postCmd = 2;
                    break;
                }

                case 2: // disconnect
                {
                    closeMenu = TRUE;
                    postCmd = 4;
                    break;
                }
                }
            }
        }
        else // entry for the FS
        {
            switch (cmd)
            {
            case 1: // connect to...
            {
                closeMenu = TRUE;
                postCmd = 3;
                break;
            }
            }
        }
        return TRUE;
    }
    else
        return FALSE; // cancel or menu error
}

void WINAPI
CPluginInterfaceForFS::ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForFS::ExecuteChangeDrivePostCommand(%d, %d,)", panel, postCmd);

    if (postCmd == 1) // entry for a detached FS: show in panel
    {
        SalamanderGeneral->ChangePanelPathToDetachedFS(panel,
                                                       (CPluginFSInterfaceAbstract*)postCmdParam,
                                                       NULL);
    }

    if (postCmd == 2) // entry for an active FS: refresh
    {
        SalamanderGeneral->RefreshPanelPath(panel);
    }

    if (postCmd == 3) // entry for the FS: connect to...
    {
        ExecuteChangeDriveMenuItem(panel);
    }

    if (postCmd == 4) // entry for an active FS: disconnect
    {                 // use PostMenuExtCommand so the disconnect runs later in "sal-idle"
        int leftOrRightPanel = panel == PANEL_SOURCE ? SalamanderGeneral->GetSourcePanel() : panel;
        SalamanderGeneral->PostMenuExtCommand(leftOrRightPanel == PANEL_LEFT ? MENUCMD_DISCONNECT_LEFT : MENUCMD_DISCONNECT_RIGHT,
                                              TRUE); // example of working with the 'panel' panel (if the command comes from the Drive bar)

        //    SalamanderGeneral->PostMenuExtCommand(MENUCMD_DISCONNECT_ACTIVE, TRUE);  // this command cannot be triggered from the Drive bar, so this implementation is sufficient
    }
}

BOOL WINAPI
CPluginInterfaceForFS::DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                    CPluginFSInterfaceAbstract* pluginFS,
                                    const wchar_t* pluginFSName, int pluginFSNameIndex)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForFS::DisconnectFS(, %d, %d, , %ls, %d)",
                        isInPanel, panel, pluginFSName, pluginFSNameIndex);
    ((CPluginFSInterface*)pluginFS)->CalledFromDisconnectDialog = TRUE; // suppress unnecessary prompts (the user requested a disconnect, just perform it)
    BOOL ret = FALSE;
    if (isInPanel)
    {
        SalamanderGeneral->DisconnectFSFromPanel(parent, panel);
        ret = SalamanderGeneral->GetPanelPluginFS(panel) != pluginFS;
    }
    else
    {
        ret = SalamanderGeneral->CloseDetachedFS(parent, pluginFS);
    }
    if (!ret)
        ((CPluginFSInterface*)pluginFS)->CalledFromDisconnectDialog = FALSE; // disable the suppression of unnecessary prompts
    return ret;
}

void WINAPI
CPluginInterfaceForFS::ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                   const wchar_t* pluginFSName, int pluginFSNameIndex,
                                   CFileData& file, int isDir)
{
#ifndef DEMOPLUG_QUIET
    const std::wstring message = SPLFormatStringOwned(
        L"%s %s in %s panel", isDir ? (isDir == 2 ? L"UpDir" : L"Subdirectory") : L"File",
        file.Name, panel == PANEL_LEFT ? L"left" : L"right");
    SalamanderGeneral->ShowMessageBox(message.c_str(), L"FS Execute", MSGBOX_INFO);
#endif // DEMOPLUG_QUIET

    CPluginFSInterface* fs = (CPluginFSInterface*)pluginFS;
    if (isDir) // subdirectory or up-dir
    {
        std::wstring newPath(fs->Path);
        if (isDir == 2) // parent directory
        {
            while (newPath.size() > 1 && newPath.back() == L'\\')
                newPath.pop_back();
            const size_t separator = newPath.find_last_of(L"\\/");
            const std::wstring cutDir = separator == std::wstring::npos ? newPath : newPath.substr(separator + 1);
            if (SPLCutDirectoryOwned(SalamanderGeneral, newPath)) // shorten the path by the last component
            {
                int topIndex; // next top index, -1 -> invalid
                if (!fs->TopIndexMem.FindAndPop(newPath.c_str(), topIndex))
                    topIndex = -1;
                // change the path in the panel
                fs = NULL; // after ChangePanelPathToXXX the pointer may no longer be valid
                SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath.c_str(), NULL,
                                                             topIndex, cutDir.c_str());
            }
        }
        else // subdirectory
        {
            // backup data for TopIndexMem (backupPath + topIndex)
            const std::wstring backupPath(newPath);
            int topIndex = SalamanderGeneral->GetPanelTopIndex(panel);

            SPLSalPathAppendOwned(newPath, file.Name); // set the path
            // change the path in the panel
            {
                fs = NULL; // after ChangePanelPathToXXX the pointer may no longer be valid
                if (SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath.c_str()))
                {
                    fs = (CPluginFSInterface*)SalamanderGeneral->GetPanelPluginFS(panel); // in case the FS in the panel changes we must fetch the current object
                    if (fs != NULL && fs == pluginFS)                                     // if it is the original FS
                        fs->TopIndexMem.Push(backupPath.c_str(), topIndex);               // remember the top index for returning
                }
            }
        }
    }
    else // file
    {
        SalamanderGeneral->SetUserWorkedOnPanelPath(panel);
        SalamanderGeneral->ExecuteAssociation(SalamanderGeneral->GetMainWindowHWND(),
                                              fs->Path.c_str(),
                                              file.Name);
    }
}

//****************************************************************************
//
// CTopIndexMem
//

static std::wstring NormalizeTopIndexPath(const wchar_t* path)
{
    std::wstring normalized(path != NULL ? path : L"");
    while (normalized.size() > 1 && normalized.back() == L'\\')
        normalized.pop_back();
    return normalized;
}

void CTopIndexMem::Push(const wchar_t* path, int topIndex)
{
    const std::wstring normalized = NormalizeTopIndexPath(path);
    std::wstring parent(normalized);
    const BOOL hasParent = SPLCutDirectoryOwned(SalamanderGeneral, parent);
    const BOOL ok = hasParent && SalamanderGeneral->IsTheSamePath(parent.c_str(), NormalizeTopIndexPath(Path.c_str()).c_str());

    if (ok) // it follows -> remember the next top index
    {
        if (TopIndexesCount == TOP_INDEX_MEM_SIZE) // need to remove the first top index from memory
        {
            int i;
            for (i = 0; i < TOP_INDEX_MEM_SIZE - 1; i++)
                TopIndexes[i] = TopIndexes[i + 1];
            TopIndexesCount--;
        }
        Path = normalized;
        TopIndexes[TopIndexesCount++] = topIndex;
    }
    else // does not follow -> first top index in the sequence
    {
        Path = normalized;
        TopIndexesCount = 1;
        TopIndexes[0] = topIndex;
    }
}

BOOL CTopIndexMem::FindAndPop(const wchar_t* path, int& topIndex)
{
    if (SalamanderGeneral->IsTheSamePath(NormalizeTopIndexPath(path).c_str(), NormalizeTopIndexPath(Path.c_str()).c_str()))
    {
        if (TopIndexesCount > 0)
        {
            SPLCutDirectoryOwned(SalamanderGeneral, Path);
            topIndex = TopIndexes[--TopIndexesCount];
            return TRUE;
        }
        else // we no longer have this value (it was not stored or low memory discarded it)
        {
            Clear();
            return FALSE;
        }
    }
    else // request for another path -> clear the memory, a long jump occurred
    {
        Clear();
        return FALSE;
    }
}

//
// ****************************************************************************
// CPluginFSDataInterface
//

CPluginFSDataInterface::CPluginFSDataInterface(const wchar_t* path)
{
    Path = path != NULL ? path : L"";
    SPLSalPathAddBackslashOwned(Path);
    NameOffset = Path.size();
}

HIMAGELIST WINAPI
CPluginFSDataInterface::GetSimplePluginIcons(int iconSize)
{
    return DFSImageList;
}

HICON WINAPI
CPluginFSDataInterface::GetPluginIcon(const CFileData* file, int iconSize, BOOL& destroyIcon)
{
    std::wstring iconPath(Path);
    iconPath.resize(NameOffset);
    iconPath.append(file->Name);
    HICON icon;
    if (!SalamanderGeneral->GetFileIcon(iconPath.c_str(), &icon, iconSize,
                                        FALSE, FALSE))
        icon = NULL;
    destroyIcon = TRUE;
    return icon; // icon or NULL (failure)
}

// global variables for the next three "get text" functions
CFSData* FSdata;
SYSTEMTIME FSst;
FILETIME FSft;
int FSlen, FSlen2;

// callback invoked by Salamander to obtain text
// see spl_com.h / FColumnGetText for details
void WINAPI GetTypeText()
{
    FSdata = (CFSData*)((*TransferFileData)->PluginData);
    memcpy(TransferBuffer, FSdata->TypeName, (*TransferLen = (int)wcslen(FSdata->TypeName)) * sizeof(wchar_t));
}

void WINAPI GetCreatedText()
{
    FileTimeToLocalFileTime(&((CFSData*)((*TransferFileData)->PluginData))->CreationTime, &FSft);
    FileTimeToSystemTime(&FSft, &FSst);
    FSlen = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &FSst, NULL, TransferBuffer, 50) - 1;
    if (FSlen < 0)
        FSlen = _snwprintf(TransferBuffer, 50, L"%u.%u.%u", FSst.wDay, FSst.wMonth, FSst.wYear);
    *(TransferBuffer + FSlen++) = L' ';
    FSlen2 = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &FSst, NULL, TransferBuffer + FSlen, 50) - 1;
    if (FSlen2 < 0)
        FSlen2 = _snwprintf(TransferBuffer + FSlen, 50, L"%u:%02u:%02u", FSst.wHour, FSst.wMinute, FSst.wSecond);
    *TransferLen = FSlen + FSlen2;
}

void WINAPI GetModifiedText()
{
    FileTimeToLocalFileTime(&(*TransferFileData)->LastWrite, &FSft);
    FileTimeToSystemTime(&FSft, &FSst);
    FSlen = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &FSst, NULL, TransferBuffer, 50) - 1;
    if (FSlen < 0)
        FSlen = _snwprintf(TransferBuffer, 50, L"%u.%u.%u", FSst.wDay, FSst.wMonth, FSst.wYear);
    *(TransferBuffer + FSlen++) = L' ';
    FSlen2 = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &FSst, NULL, TransferBuffer + FSlen, 50) - 1;
    if (FSlen2 < 0)
        FSlen2 = _snwprintf(TransferBuffer + FSlen, 50, L"%u:%02u:%02u", FSst.wHour, FSst.wMinute, FSst.wSecond);
    *TransferLen = FSlen + FSlen2;
}

void WINAPI GetAccessedText()
{
    FileTimeToLocalFileTime(&((CFSData*)((*TransferFileData)->PluginData))->LastAccessTime, &FSft);
    FileTimeToSystemTime(&FSft, &FSst);
    FSlen = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &FSst, NULL, TransferBuffer, 50) - 1;
    if (FSlen < 0)
        FSlen = _snwprintf(TransferBuffer, 50, L"%u.%u.%u", FSst.wDay, FSst.wMonth, FSst.wYear);
    *(TransferBuffer + FSlen++) = L' ';
    FSlen2 = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &FSst, NULL, TransferBuffer + FSlen, 50) - 1;
    if (FSlen2 < 0)
        FSlen2 = _snwprintf(TransferBuffer + FSlen, 50, L"%u:%02u:%02u", FSst.wHour, FSst.wMinute, FSst.wSecond);
    *TransferLen = FSlen + FSlen2;
}

int WINAPI PluginSimpleIconCallback()
{
    return *TransferIsDir ? 0 : 1;
}

void AddTypeColumn(BOOL leftPanel, CSalamanderViewAbstract* view, int& i, const CColumn* origTypeColumn)
{
    CColumn column = *origTypeColumn;
    wcscpy(column.Name, L"DFS Type");
    column.GetText = GetTypeText;
    column.CustomData = 4;
    column.ID = COLUMN_ID_CUSTOM;
    column.Width = leftPanel ? LOWORD(DFSTypeWidth) : HIWORD(DFSTypeWidth);
    column.FixedWidth = leftPanel ? LOWORD(DFSTypeFixedWidth) : HIWORD(DFSTypeFixedWidth);
    view->InsertColumn(++i, &column); // insert our Type column after the original Type column
}

void AddTimeColumns(BOOL leftPanel, CSalamanderViewAbstract* view, int& i)
{
    CColumn column;
    wcscpy(column.Name, L"Created");
    wcscpy(column.Description, L"Creation Time");
    column.GetText = GetCreatedText;
    column.CustomData = 1;
    column.SupportSorting = 1;
    column.LeftAlignment = 1;
    column.ID = COLUMN_ID_CUSTOM;
    column.Width = leftPanel ? LOWORD(CreatedWidth) : HIWORD(CreatedWidth);
    column.FixedWidth = leftPanel ? LOWORD(CreatedFixedWidth) : HIWORD(CreatedFixedWidth);
    view->InsertColumn(i++, &column);

    wcscpy(column.Name, L"Modified");
    wcscpy(column.Description, L"Last Write Time");
    column.GetText = GetModifiedText;
    column.CustomData = 2;
    column.Width = leftPanel ? LOWORD(ModifiedWidth) : HIWORD(ModifiedWidth);
    column.FixedWidth = leftPanel ? LOWORD(ModifiedFixedWidth) : HIWORD(ModifiedFixedWidth);
    view->InsertColumn(i++, &column);

    wcscpy(column.Name, L"Accessed");
    wcscpy(column.Description, L"Last Access Time");
    column.GetText = GetAccessedText;
    column.CustomData = 3;
    column.Width = leftPanel ? LOWORD(AccessedWidth) : HIWORD(AccessedWidth);
    column.FixedWidth = leftPanel ? LOWORD(AccessedFixedWidth) : HIWORD(AccessedFixedWidth);
    view->InsertColumn(i++, &column);
}

void WINAPI
CPluginFSDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                  const CFileData* upperDir)
{
    view->GetTransferVariables(TransferFileData, TransferIsDir, TransferBuffer, TransferLen, TransferRowData,
                               TransferPluginDataIface, TransferActCustomData);

    view->SetPluginSimpleIconCallback(PluginSimpleIconCallback);

    if (view->GetViewMode() == VIEW_MODE_DETAILED) // adjust the columns
    {
        // remove the Date + Time columns and insert Created/Modified/Accessed in their place
        // and find the Type column and replace it with our Type column
        int count = view->GetColumnsCount();
        BOOL timeColumnsDone = FALSE;
        //    BOOL typeColumnDone = FALSE;
        int i;
        for (i = 0; i < count; i++)
        {
            const CColumn* c = view->GetColumn(i);
            if (c->ID == COLUMN_ID_TYPE)
            { // replace the Type column with ours
                //        typeColumnDone = TRUE;
                AddTypeColumn(leftPanel, view, i, c);
                count = view->GetColumnsCount(); // the number of columns has changed
                continue;                        // continue searching at the next 'i'
            }
            if (c->ID == COLUMN_ID_DATE || c->ID == COLUMN_ID_TIME)
            { // remove Date and Time, insert Created/Modified/Accessed
                view->DeleteColumn(i);
                if (!timeColumnsDone)
                {
                    timeColumnsDone = TRUE;
                    AddTimeColumns(leftPanel, view, i); // 'i' increases by the number of added columns
                }
                i--;                             // we removed one, go back to the same 'i'
                count = view->GetColumnsCount(); // the number of columns has changed
                continue;
            }
        }

        /*
    if (!typeColumnDone)  // always display the Type column
    {
      view->InsertStandardColumn(count, COLUMN_ID_TYPE);  // pull in the standard Type column
      const CColumn *c = view->GetColumn(count);
      if (c != NULL && c->ID == COLUMN_ID_TYPE)
      {   // replace the Type column with ours
        AddTypeColumn(leftPanel, view, count, c);
        count = view->GetColumnsCount();
      }
    }
*/
        if (!timeColumnsDone) // always display the time columns
        {
            AddTimeColumns(leftPanel, view, count);
            count = view->GetColumnsCount();
        }
    }
}

void WINAPI
CPluginFSDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (leftPanel)
    {
        switch (column->CustomData)
        {
        case 1:
            CreatedFixedWidth = MAKELONG(newFixedWidth, HIWORD(CreatedFixedWidth));
            break;
        case 2:
            ModifiedFixedWidth = MAKELONG(newFixedWidth, HIWORD(ModifiedFixedWidth));
            break;
        case 3:
            AccessedFixedWidth = MAKELONG(newFixedWidth, HIWORD(AccessedFixedWidth));
            break;
        case 4:
            DFSTypeFixedWidth = MAKELONG(newFixedWidth, HIWORD(DFSTypeFixedWidth));
            break;
        }
    }
    else
    {
        switch (column->CustomData)
        {
        case 1:
            CreatedFixedWidth = MAKELONG(LOWORD(CreatedFixedWidth), newFixedWidth);
            break;
        case 2:
            ModifiedFixedWidth = MAKELONG(LOWORD(ModifiedFixedWidth), newFixedWidth);
            break;
        case 3:
            AccessedFixedWidth = MAKELONG(LOWORD(AccessedFixedWidth), newFixedWidth);
            break;
        case 4:
            DFSTypeFixedWidth = MAKELONG(LOWORD(DFSTypeFixedWidth), newFixedWidth);
            break;
        }
    }
    if (newFixedWidth)
        ColumnWidthWasChanged(leftPanel, column, column->Width);
}

void WINAPI
CPluginFSDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    if (leftPanel)
    {
        switch (column->CustomData)
        {
        case 1:
            CreatedWidth = MAKELONG(newWidth, HIWORD(CreatedWidth));
            break;
        case 2:
            ModifiedWidth = MAKELONG(newWidth, HIWORD(ModifiedWidth));
            break;
        case 3:
            AccessedWidth = MAKELONG(newWidth, HIWORD(AccessedWidth));
            break;
        case 4:
            DFSTypeWidth = MAKELONG(newWidth, HIWORD(DFSTypeWidth));
            break;
        }
    }
    else
    {
        switch (column->CustomData)
        {
        case 1:
            CreatedWidth = MAKELONG(LOWORD(CreatedWidth), newWidth);
            break;
        case 2:
            ModifiedWidth = MAKELONG(LOWORD(ModifiedWidth), newWidth);
            break;
        case 3:
            AccessedWidth = MAKELONG(LOWORD(AccessedWidth), newWidth);
            break;
        case 4:
            DFSTypeWidth = MAKELONG(LOWORD(DFSTypeWidth), newWidth);
            break;
        }
    }
}

struct CFSInfoLineData
{
    std::wstring Name;
    std::wstring Type;
};

const wchar_t* WINAPI FSInfoLineFile(HWND parent, void* param)
{
    CFSInfoLineData* data = (CFSInfoLineData*)param;
    return data->Name.c_str();
}

const wchar_t* WINAPI FSInfoLineType(HWND parent, void* param)
{
    CFSInfoLineData* data = (CFSInfoLineData*)param;
    return data->Type.c_str();
}

CSalamanderVarStrEntry FSInfoLine[] =
    {
        {L"File", FSInfoLineFile},
        {L"Type", FSInfoLineType},
        {NULL, NULL}};

BOOL WINAPI
CPluginFSDataInterface::GetInfoLineContent(int panel, const CFileData* file, BOOL isDir,
                                           int selectedFiles, int selectedDirs, BOOL displaySize,
                                           const CQuadWord& selectedSize,
                                           CSalamanderStringBuffer* buffer,
                                           CSalamanderTextRangeBuffer* hotTexts)
{
    if (buffer == NULL || hotTexts == NULL)
        return FALSE;
    if (file != NULL)
    {
        CFSInfoLineData data;
        data.Name = file->Name;
        data.Type = ((CFSData*)file->PluginData)->TypeName;
        if (!SalamanderGeneral->ExpandVarString(SalamanderGeneral->GetMsgBoxParent(),
                                                L"$(File): $(Type)", buffer, FSInfoLine,
                                                &data, FALSE, hotTexts))
        {
            const std::vector<CSalamanderTextRange> noRanges;
            return sally::plugin_abi::WriteTextAndRanges(
                       *buffer, *hotTexts, L"Error!", noRanges)
                       ? TRUE
                       : FALSE;
        }
        return TRUE;
    }
    else
    {
        if (selectedFiles == 0 && selectedDirs == 0) // Information Line for an empty panel
        {
            // return FALSE;  // let Salamander print the text
            const std::vector<CSalamanderTextRange> noRanges;
            return sally::plugin_abi::WriteTextAndRanges(
                       *buffer, *hotTexts, L"No items found", noRanges)
                       ? TRUE
                       : FALSE;
        }
        // return FALSE;  // let Salamander print the counts of selected files and directories
        std::wstring text;
        if (displaySize)
        {
            /*
      // double-check the sum
      CQuadWord mySize(0, 0);
      int index = 0;
      const CFileData *file = NULL;
      while ((file = SalamanderGeneral->GetPanelSelectedItem(panel, &index, NULL)) != NULL)
      {
        mySize += file->Size;
      }
      if (mySize != selectedSize) TRACE_E("Unexpected situation in CPluginFSDataInterface::GetInfoLineContent().");
*/

            const std::wstring num = SPLPrintDiskSizeOwned(SalamanderGeneral, selectedSize, 0);
            // for simplicity we do not use "plural" strings (see SalamanderGeneral->ExpandPluralString())
            text = SPLFormatStringOwned(
                L"Selected (<%s>): <%d> files and <%d> directories",
                num.c_str(), selectedFiles, selectedDirs);

            /*    // example of using a standard string
      SalamanderGeneral->ExpandPluralBytesFilesDirs(buffer, 1000, selectedSize, selectedFiles,
                                                    selectedDirs, TRUE);
*/
        }
        else
            text = SPLFormatStringOwned(
                L"Selected: <%d> files and <%d> directories",
                selectedFiles, selectedDirs);
        std::vector<CSalamanderTextRange> ranges;
        if (!SPLLookForSubTextsOwned(SalamanderGeneral, text, ranges))
            return FALSE;
        return sally::plugin_abi::WriteTextAndRanges(
                   *buffer, *hotTexts, text, ranges)
                   ? TRUE
                   : FALSE;
    }
}

//
// ****************************************************************************
// CFSData
//

CFSData::CFSData(const FILETIME& creationTime, const FILETIME& lastAccessTime, const wchar_t* type)
{
    CreationTime = creationTime;
    LastAccessTime = lastAccessTime;
    TypeName = SalamanderGeneral->DupStr(type);
}
