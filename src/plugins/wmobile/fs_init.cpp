// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "wmobile_path_core.h"

// FS-name assigned by Salamander after loading the plugin
std::wstring AssignedFSName;

// global variables used to store pointers to Salamander's global variables
// shared for both the archive and the FS
const CFileData** TransferFileData = NULL;
int* TransferIsDir = NULL;
char* TransferBuffer = NULL;
int* TransferLen = NULL;
DWORD* TransferRowData = NULL;
CPluginDataInterfaceAbstract** TransferPluginDataIface = NULL;
DWORD* TransferActCustomData = NULL;

// ****************************************************************************
// FILE SYSTEM SECTION
// ****************************************************************************

BOOL InitFS()
{
    return TRUE;
}

void ReleaseFS()
{
}

//
// ****************************************************************************
// CPluginInterfaceForFS
//

CPluginFSInterfaceAbstract* WINAPI
CPluginInterfaceForFS::OpenFS(const wchar_t* fsName, int fsNameIndex)
{
    if (!CRAPI::Init())
        return NULL;

    ActiveFSCount++;
    return new CPluginFSInterface;
}

void WINAPI
CPluginInterfaceForFS::CloseFS(CPluginFSInterfaceAbstract* fs)
{
    CPluginFSInterface* dfsFS = (CPluginFSInterface*)fs; // ensure the correct destructor is called

    ActiveFSCount--;

    if (dfsFS != NULL)
        delete dfsFS;

    if (ActiveFSCount == 0)
        CRAPI::UnInit();
}

void WINAPI
CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(int panel)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(%d)", panel);

    //JR Start at the root
    SalamanderGeneral->ChangePanelPathToPluginFS(panel, AssignedFSName.c_str(), L"\\"); //JR x:
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
    // The Windows Mobile plugin has no context Change Drive menu
    return FALSE;
}

void WINAPI
CPluginInterfaceForFS::ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForFS::ExecuteChangeDrivePostCommand(%d, %d,)", panel, postCmd);
}

void WINAPI
CPluginInterfaceForFS::ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                   const wchar_t* pluginFSName, int pluginFSNameIndex,
                                   CFileData& file, int isDir)
{
    CPluginFSInterface* fs = (CPluginFSInterface*)pluginFS;
    if (isDir) // subdirectory or up-dir
    {
        std::wstring newPath = fs->Path;

        if (isDir == 2) // up-dir
        {
            std::wstring cutDir;
            if (SPLCutDirectoryOwned(SalamanderGeneral, newPath, &cutDir)) // shorten the path by the last component
            {
                int topIndex; // next top index, -1 -> invalid
                if (!fs->TopIndexMem.FindAndPop(newPath.c_str(), topIndex))
                    topIndex = -1;
                // change the path in the panel
                SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath.c_str(), NULL,
                                                             topIndex, cutDir.c_str());
            }
        }
        else // subdirectory
        {
            // backup of data for TopIndexMem (backupPath + topIndex)
            const std::wstring backupPath = newPath;
            int topIndex = SalamanderGeneral->GetPanelTopIndex(panel);

            wmobile::AppendDeviceComponent(newPath, file.Name);
            if (SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, newPath.c_str()))
                fs->TopIndexMem.Push(backupPath.c_str(), topIndex); // remember the top index for the return
        }
    }
    else
    {
        std::wstring cmdLine = fs->Path;
        wmobile::AppendDeviceComponent(cmdLine, file.Name);
        wchar_t *command = NULL, *params = NULL;

        int l = (int)cmdLine.size();
        if (l > 4)
        {
            if (SalamanderGeneral->StrICmp(cmdLine.c_str() + l - 4, L".lnk") == 0)
            {
                std::wstring target;
                if (CRAPI::SHGetShortcutTargetWide(cmdLine.c_str(), target))
                {
                    cmdLine = std::move(target);
                    command = cmdLine.data();
                    if (*command == L'"')
                    {
                        command++;
                        wchar_t* end = command + wcslen(command) - 1;
                        if (*end == L'"')
                            *end = 0;
                    }
                    else
                    {
                        params = wcschr(command, L' ');
                        if (params)
                        {
                            *params = 0;
                            params++;
                        }
                    }
                }
            }
            else if (SalamanderGeneral->StrICmp(cmdLine.c_str() + l - 4, L".exe") == 0)
                command = cmdLine.data();

            if (command != 0 && command[0] != 0)
            {
                if (!CRAPI::CreateProcessWide(command, params))
                {
                    DWORD err = CRAPI::GetLastError();
                    SalamanderGeneral->ShowMessageBox(SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), TitleWMobileError, MSGBOX_ERROR);
                }
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

//****************************************************************************
//
// CTopIndexMem
//

// Salamander's case-insensitive comparison, adapted to the core's narrow
// comparator. The wide length is derived from the narrow count exactly as the original code
// derived it, so the folding semantics are unchanged - only the surrounding structure moved.
static int TopIndexComparePrefix(const wchar_t* a, const wchar_t* b, size_t count)
{
    return SalamanderGeneral->StrNICmp(a, b, static_cast<int>(count));
}

CTopIndexMem::CTopIndexMem() : Memory(&TopIndexComparePrefix) {}

void CTopIndexMem::Push(const wchar_t* path, int topIndex)
{
    Memory.Push(path, topIndex);
}

BOOL CTopIndexMem::FindAndPop(const wchar_t* path, int& topIndex)
{
    return Memory.FindAndPop(path, topIndex) ? TRUE : FALSE;
}
