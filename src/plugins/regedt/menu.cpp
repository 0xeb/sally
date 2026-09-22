// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

// ****************************************************************************
//
// CPluginInterfaceForMenuExt
//

BOOL CPluginInterfaceForMenuExt::PostFocusCommand(const wchar_t* path,
                                                  const wchar_t* name)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::PostFocusCommand(%ls, %ls)",
                        path, name);
    if (!SG->SalamanderIsNotBusy(NULL))
        return FALSE;

    Path = path != NULL ? path : L"";
    Name = name != NULL ? name : L"";

    SG->PostMenuExtCommand(MID_FOCUS, TRUE);

    // switching to another window happens, so theoretically this Sleep
    // should not cause any harm
    Sleep(500);

    // after 0.5 seconds we are no longer interested in the focus (handles
    // the case when we hit the start of Salamander's BUSY mode)
    Path.clear();
    Name.clear();

    return TRUE;
}

DWORD
CPluginInterfaceForMenuExt::GetMenuItemState(int id, DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::GetMenuItemState(%d, 0x%X)",
                        id, eventMask);
    return 0;
}

BOOL CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                                 int id, DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::ExecuteMenuItem(, , %d, 0x%X)",
                        id, eventMask);
    PARENT(parent);
    BOOL ret = FALSE;
    switch (id)
    {
    case MID_FIND:
    {
        SG->GetConfigParameter(SALCFG_ALWAYSONTOP, &AlwaysOnTop, sizeof(AlwaysOnTop), NULL);

        CPluginFSInterface* fs = (CPluginFSInterface*)SG->GetPanelPluginFS(PANEL_SOURCE);
        std::wstring path = AssignedFSName + L":";
        path += fs != NULL ? fs->GetCurrentPathOwned() : L"\\";
        CFindDialogThread* t = new CFindDialogThread(path.c_str());
        if (t)
        {
            if (!t->Create(ThreadQueue))
                delete t;
        }
        else
            Error(IDS_LOWMEM);
        break;
    }

    case MID_NEWKEY:
    {
        SG->PostSalamanderCommand(SALCMD_CREATEDIRECTORY);
        break;
    }

    case MID_NEWVAL:
    {
        CPluginFSInterface* fs = (CPluginFSInterface*)SG->GetPanelPluginFS(PANEL_SOURCE);
        if (fs)
            fs->EditNewFile();
        break;
    }

    case MID_EXPORT:
    {
        CPluginFSInterface* fs = (CPluginFSInterface*)SG->GetPanelPluginFS(PANEL_SOURCE);
        std::wstring path = AssignedFSName + L":";
        if (fs != NULL)
        {
            path += fs->GetCurrentPathOwned();
            BOOL isDir;
            const CFileData* fd = SG->GetPanelFocusedItem(PANEL_SOURCE, &isDir);
            CPluginData* pd = fd != NULL ? (CPluginData*)fd->PluginData : NULL;
            if (isDir && pd && pd->Name && wcscmp(pd->Name, L"..") != 0)
                SPLSalPathAppendOwned(path, pd->Name);
        }
        else
            path += L"\\";
        ExportKey(path.data());
        break;
    }

    case MID_FOCUS:
    {
        // only if we were lucky enough not to hit the start of Salamander's BUSY mode
        if (!Path.empty())
        {
            SetForegroundWindow(SG->GetMainWindowHWND());
            SG->ChangePanelPathToPluginFS(PANEL_SOURCE, AssignedFSName.c_str(), Path.c_str(), NULL,
                                          -1, Name.c_str());
            Path.clear();
            Name.clear();
        }
        break;
    }
    }
    return ret;
}

BOOL WINAPI
CPluginInterfaceForMenuExt::HelpForMenuItem(HWND parent, int id)
{
    int helpID = 0;
    switch (id)
    {
    case MID_FIND:
        helpID = IDH_SEARCHREG;
        break;
    case MID_NEWKEY:
        helpID = IDH_NEWKEY;
        break;
    case MID_NEWVAL:
        helpID = IDH_NEWVALUE;
        break;
    case MID_EXPORT:
        helpID = IDH_EXPORT;
        break;
    }
    if (helpID != 0)
        SG->OpenHtmlHelp(parent, HHCDisplayContext, helpID, FALSE);
    return helpID != 0;
}
