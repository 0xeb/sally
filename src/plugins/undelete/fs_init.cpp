// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "undelete.rh"
#include "undelete.rh2"
#include "lang\lang.rh"

#include "library\undelete.h"
#include "miscstr.h"
#include "os.h"
#include "volume.h"
#include "snapshot.h"
#include "dialogs.h"
#include "undelete.h"

// FS-name assigned by Salamanderem after plugin is loaded
std::wstring AssignedFSName;

// image-list for simple FS icons
HIMAGELIST DFSImageList = NULL;

BOOL WantReconnect;

// global variables where we will store pointers to global variables in Salamander
const CFileData** TransferFileData = NULL;
int* TransferIsDir = NULL;
wchar_t* TransferBuffer = NULL;
int* TransferLen = NULL;
DWORD* TransferRowData = NULL;
CPluginDataInterfaceAbstract** TransferPluginDataIface = NULL;
DWORD* TransferActCustomData = NULL;

extern int ConditionFixedWidth;
extern int ConditionWidth;

// ****************************************************************************
//
//   Init & Release
//

BOOL InitFS()
{
    return TRUE;
}

void ReleaseFS()
{
}

// ****************************************************************************
//
//   CPluginInterfaceForFS
//

CPluginFSInterfaceAbstract* WINAPI CPluginInterfaceForFS::OpenFS(const wchar_t* fsName, int fsNameIndex)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForFS::OpenFS(%ls, %d)", fsName, fsNameIndex);
    ActiveFSCount++;
    return new CPluginFSInterface;
}

void WINAPI CPluginInterfaceForFS::CloseFS(CPluginFSInterfaceAbstract* fs)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForFS::CloseFS()");
    CPluginFSInterface* dfsFS = (CPluginFSInterface*)fs; // needed to call correct destructor
    ActiveFSCount--;
    if (!ActiveFSCount)
    {
        std::wstring root = AssignedFSName + L":";
        SPLToLowerCaseOwned(SalamanderGeneral, root);
        SalamanderGeneral->RemoveFilesFromCache(root.c_str());
    }
    if (dfsFS != NULL)
        delete dfsFS;
}

void WINAPI CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(int panel)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(%d)", panel);

    CConnectDialog dlg(SalamanderGeneral->GetMsgBoxParent(), panel);

    // connect dialog
    if (dlg.Execute() != IDOK)
        return;

    WantReconnect = TRUE;
    SalamanderGeneral->ChangePanelPathToPluginFS(panel, AssignedFSName.c_str(), dlg.Volume.c_str());
    WantReconnect = FALSE;
}

void WINAPI
CPluginInterfaceForFS::ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                   const wchar_t* pluginFSName, int pluginFSNameIndex,
                                   CFileData& file, int isDir)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForFS::ExecuteOnFS(%d, , %ls, %d, , %d)",
                        panel, pluginFSName, pluginFSNameIndex, isDir);

    CPluginFSInterface* fs = (CPluginFSInterface*)pluginFS;
    if (isDir) // sub-dir or up-dir
    {
        std::wstring newPath(fs->Path);
        if (isDir == 2) // up-dir
        {
            const size_t separator = newPath.find_last_of(L'\\');
            if (separator != std::wstring::npos)
            {
                const std::wstring cutDir = newPath.substr(separator + 1);
                newPath.erase(separator);
                int topIndex; // next top-index, -1 -> invalid
                if (!fs->TopIndexMem.FindAndPop(newPath.c_str(), topIndex))
                    topIndex = -1;
                // change path in panel
                fs = NULL; // after ChangePanelPathToXXX the pointer could be invalid
                SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath.c_str(), NULL,
                                                             topIndex, cutDir.c_str());
            }
        }
        else // sub-dir
        {
            // store data for TopIndexMem (backupPath + topIndex)
            const std::wstring backupPath(newPath);
            int topIndex = SalamanderGeneral->GetPanelTopIndex(panel);

            SPLSalPathAppendOwned(newPath, file.Name); // set path
            // change path in panel
            fs = NULL; // after ChangePanelPathToXXX the pointer could be invalid
            if (SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath.c_str()))
            {
                fs = (CPluginFSInterface*)SalamanderGeneral->GetPanelPluginFS(panel); // we need to get current object (in case FS changes)
                if (fs != NULL && fs == pluginFS)                                     // if it is original FS
                    fs->TopIndexMem.Push(backupPath.c_str(), topIndex);                // store top-index for return
            }
        }
    }
}

BOOL WINAPI
CPluginInterfaceForFS::DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                    CPluginFSInterfaceAbstract* pluginFS,
                                    const wchar_t* pluginFSName, int pluginFSNameIndex)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForFS::DisconnectFS(, %d, %d, , %ls, %d)",
                        isInPanel, panel, pluginFSName, pluginFSNameIndex);
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
    return ret;
}

//
// ****************************************************************************
// CPluginFSDataInterface
//

// callback called from Salamander to get texts
// for description see spl_com.h / FColumnGetText
void WINAPI GetSzText()
{
    if (*TransferIsDir)
    {
        //CopyMemory(TransferBuffer, "Dir", 3);
        *TransferLen = 0;
    }
    else
    {
        if (((CPluginFSDataInterface*)(*TransferPluginDataIface))->IsSnapshotValid())
        {
            DIR_ITEM_I<wchar_t>* di = (DIR_ITEM_I<wchar_t>*)(*TransferFileData)->PluginData;
            int resID = IDS_CONDITION_UNKNOWN;
            switch (di->Record->Flags & FR_FLAGS_CONDITION_MASK)
            {
            case FR_FLAGS_CONDITION_GOOD:
                resID = IDS_CONDITION_GOOD;
                break;
            case FR_FLAGS_CONDITION_FAIR:
                resID = IDS_CONDITION_FAIR;
                break;
            case FR_FLAGS_CONDITION_POOR:
                resID = IDS_CONDITION_POOR;
                break;
            case FR_FLAGS_CONDITION_LOST:
                resID = IDS_CONDITION_LOST;
                break;
            }
            if (di->Record->Flags & FR_FLAGS_DELETED)
                *TransferLen = swprintf(TransferBuffer, String<wchar_t>::LangStr(resID).c_str());
            else
                *TransferLen = 0; // don't display "Good" for existing files
        }
        else // snapshot is released, we must not touch its data or we will crash (for example Refresh (Ctrl+R) in "{All Deleted Files}")
            *TransferLen = 0;
    }
}

void WINAPI
CPluginFSDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                  const CFileData* upperDir)
{
    view->GetTransferVariables(TransferFileData, TransferIsDir, TransferBuffer, TransferLen, TransferRowData,
                               TransferPluginDataIface, TransferActCustomData);

    // set columns only in detailed mode
    if (view->GetViewMode() == VIEW_MODE_DETAILED)
    {
        int sizeIndex = view->GetColumnsCount();

        CColumn column;
        lstrcpyW(column.Name, String<wchar_t>::LangStr(IDS_CONDITION).c_str());
        lstrcpyW(column.Description, String<wchar_t>::LangStr(IDS_CONDITIONDESC).c_str());
        column.GetText = GetSzText;
        column.SupportSorting = 0;
        column.LeftAlignment = 1;
        column.ID = COLUMN_ID_CUSTOM;
        column.Width = leftPanel ? LOWORD(ConditionWidth) : HIWORD(ConditionWidth);
        column.FixedWidth = leftPanel ? LOWORD(ConditionFixedWidth) : HIWORD(ConditionFixedWidth);
        view->InsertColumn(sizeIndex, &column);
    }
}

void CPluginFSDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (leftPanel)
        ConditionFixedWidth = MAKELONG(newFixedWidth, HIWORD(ConditionFixedWidth));
    else
        ConditionFixedWidth = MAKELONG(LOWORD(ConditionFixedWidth), newFixedWidth);
    if (newFixedWidth)
        ColumnWidthWasChanged(leftPanel, column, column->Width);
}

void CPluginFSDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    if (leftPanel)
        ConditionWidth = MAKELONG(newWidth, HIWORD(ConditionWidth));
    else
        ConditionWidth = MAKELONG(LOWORD(ConditionWidth), newWidth);
}
