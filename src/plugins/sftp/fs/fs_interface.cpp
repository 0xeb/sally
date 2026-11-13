// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <algorithm>
#include <memory>

#include "fs_object.h"
#include "operations_registry.h"
#include "site_config.h"
#include "../ui/site_list_dialog.h"

CPluginFSInterfaceAbstract* WINAPI
CPluginInterfaceForFS::OpenFS(const wchar_t* fsName, int fsNameIndex)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForFS::OpenFS(%ls, %d)", fsName, fsNameIndex);
    try
    {
        // One object per connection. The connection itself is made by its first ChangePath.
        openFS_.reserve(openFS_.size() + 1); // so push_back below cannot throw after the object exists
        std::unique_ptr<CPluginFSInterface> fs(new CPluginFSInterface);
        openFS_.push_back(fs.get());
        return fs.release();
    }
    catch (...)
    {
        TRACE_E("OpenFS: out of memory.");
        return nullptr;
    }
}

void WINAPI
CPluginInterfaceForFS::CloseFS(CPluginFSInterfaceAbstract* fs)
{
    // No window may be opened here (Salamander rule). The destructor joins the session thread.
    CALL_STACK_MESSAGE1("CPluginInterfaceForFS::CloseFS()");
    if (fs == nullptr)
        return;
    CPluginFSInterface* object = static_cast<CPluginFSInterface*>(fs);
    openFS_.erase(std::remove(openFS_.begin(), openFS_.end(), object), openFS_.end());
    // A wait of this object may be running (Salamander closed it from a message handled inside): then the object is
    // deleted when that call has unwound.
    object->CloseOrDefer();
}

void CPluginInterfaceForFS::CloseAllSessions()
{
    // Salamander closes every FS before it unloads a plugin; this is the safety net so that no session thread can
    // outlive the DLL. The objects themselves stay (Salamander still owns them).
    for (CPluginFSInterface* fs : openFS_)
    {
        fs->CloseSession(); // cancels and joins the session thread, also of a busy object (its wait then ends)
        fs->MarkClosedIfBusy();
    }
    if (!openFS_.empty())
        TRACE_E("Plugin released while " << openFS_.size() << " FS object(s) were still open.");
    openFS_.clear();
}

void ConnectFromSiteList(HWND parent, int panel)
{
    CSiteListDialog dialog(parent, SiteConfig); // kept across the loop: the selection and the quick connect text survive a failure
    while (true)
    {
        std::wstring path;
        if (!dialog.Run(path))
            return; // Close
        // let the main window repaint what the dialog covered before the panel starts waiting for the server
        UpdateWindow(SalamanderGeneral->GetMainWindowHWND());

        int failReason = CHPPFR_SUCCESS;
        if (SalamanderGeneral->ChangePanelPathToPluginFS(panel, AssignedFSName.c_str(), path.c_str(), &failReason))
            return;
        // FALSE with CHPPFR_SHORTERPATH also means success (the opened path differs from the requested one)
        if (failReason == CHPPFR_INVALIDPATH ||   // cannot connect or list: show the site list again
            failReason == CHPPFR_CANNOTCLOSEPATH) // the current path cannot be closed (the user cancelled the question)
            continue;
        return;
    }
}

void WINAPI
CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(int panel)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(%d)", panel);
    try
    {
        ConnectFromSiteList(SalamanderGeneral->GetMsgBoxParent(), panel);
    }
    catch (...)
    {
        TRACE_E("ExecuteChangeDriveMenuItem: unexpected exception.");
    }
}

// Menu commands, also the postCmd values of ExecuteChangeDrivePostCommand
static const int CmdShowInPanel = 1; // detached FS
static const int CmdDisconnect = 2;  // FS in a panel (detached ones are closed at once)

BOOL WINAPI
CPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                      CPluginFSInterfaceAbstract* pluginFS,
                                                      const wchar_t* pluginFSName, int pluginFSNameIndex,
                                                      BOOL isDetachedFS, BOOL& refreshMenu,
                                                      BOOL& closeMenu, int& postCmd, void*& postCmdParam)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(, %d, , , , , %d,)", panel,
                        isDetachedFS);
    if (pluginFS == nullptr)
        return FALSE; // the plain "SFTP" item has no menu yet
    try
    {
        const std::wstring showText = LangStr(IDS_MENU_SHOWINPANEL);
        const std::wstring disconnectText = LangStr(IDS_MENU_DISCONNECT);

        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
            return FALSE;
        if (isDetachedFS)
            AppendMenuW(menu, MF_STRING, CmdShowInPanel, showText.c_str());
        AppendMenuW(menu, MF_STRING, CmdDisconnect, disconnectText.c_str());
        const DWORD cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON, x, y, parent, nullptr);
        DestroyMenu(menu);
        if (cmd == 0)
            return FALSE; // cancelled

        refreshMenu = FALSE;
        closeMenu = FALSE;
        postCmd = 0;
        postCmdParam = nullptr;
        if (cmd == CmdShowInPanel)
        {
            closeMenu = TRUE; // do it after the menu is closed
            postCmd = CmdShowInPanel;
            postCmdParam = pluginFS;
        }
        else if (cmd == CmdDisconnect)
        {
            if (isDetachedFS)
            {
                static_cast<CPluginFSInterface*>(pluginFS)->SetDisconnectRequested(true);
                if (SalamanderGeneral->CloseDetachedFS(parent, pluginFS))
                    refreshMenu = TRUE; // the item is gone
                else
                    static_cast<CPluginFSInterface*>(pluginFS)->SetDisconnectRequested(false); // still alive
            }
            else
            {
                closeMenu = TRUE; // an FS in a panel is disconnected after the menu is closed
                postCmd = CmdDisconnect;
                postCmdParam = pluginFS;
            }
        }
        return TRUE;
    }
    catch (...)
    {
        TRACE_E("ChangeDriveMenuItemContextMenu: out of memory.");
        return FALSE;
    }
}

void WINAPI
CPluginInterfaceForFS::ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForFS::ExecuteChangeDrivePostCommand(%d, %d,)", panel, postCmd);
    try
    {
        CPluginFSInterfaceAbstract* fs = static_cast<CPluginFSInterfaceAbstract*>(postCmdParam);
        if (postCmd == CmdShowInPanel)
        {
            SalamanderGeneral->ChangePanelPathToDetachedFS(panel, fs, nullptr);
        }
        else if (postCmd == CmdDisconnect)
        {
            const int sidePanel = panel == PANEL_SOURCE ? SalamanderGeneral->GetSourcePanel() : panel;
            // only if the FS is still the one in that panel (the pointer travelled through the menu)
            if (SalamanderGeneral->GetPanelPluginFS(sidePanel) != fs)
                return;
            CPluginFSInterface* object = static_cast<CPluginFSInterface*>(fs);
            object->SetDisconnectRequested(true); // no "keep the connection?" question after the user said Disconnect
            SalamanderGeneral->DisconnectFSFromPanel(SalamanderGeneral->GetMsgBoxParent(), sidePanel);
            if (SalamanderGeneral->GetPanelPluginFS(sidePanel) == fs) // still there: the disconnect did not happen
                object->SetDisconnectRequested(false);
        }
    }
    catch (...)
    {
        TRACE_E("ExecuteChangeDrivePostCommand: unexpected exception.");
    }
}

// Enter (or double click) on an item of the panel. Salamander wants the path change to happen here, outside the
// FS object's own methods, because it may close the FS.
void WINAPI
CPluginInterfaceForFS::ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                   const wchar_t* pluginFSName, int pluginFSNameIndex,
                                   CFileData& file, int isDir)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForFS::ExecuteOnFS(%d, , , , , %d)", panel, isDir);
    try
    {
        if (isDir == 0)
            return; // opening a file (through a local copy) comes with the file operations

        std::wstring userPart;
        std::wstring focusName;
        if (!static_cast<CPluginFSInterface*>(pluginFS)->BuildNavigation(file, isDir, userPart, focusName))
            return;
        // pluginFS and 'file' may be invalid after the call (the listing is replaced)
        SalamanderGeneral->ChangePanelPathToPluginFS(panel, pluginFSName, userPart.c_str(), nullptr, -1,
                                                     focusName.empty() ? nullptr : focusName.c_str());
    }
    catch (...)
    {
        TRACE_E("ExecuteOnFS: unexpected exception.");
    }
}

BOOL WINAPI
CPluginInterfaceForFS::DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                    CPluginFSInterfaceAbstract* pluginFS,
                                    const wchar_t* pluginFSName, int pluginFSNameIndex)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForFS::DisconnectFS(, %d, %d, , ,)", isInPanel, panel);
    try
    {
        CPluginFSInterface* object = static_cast<CPluginFSInterface*>(pluginFS);
        object->SetDisconnectRequested(true); // the user asked for it: no question
        BOOL done;
        if (isInPanel)
        {
            SalamanderGeneral->DisconnectFSFromPanel(parent, panel);
            done = SalamanderGeneral->GetPanelPluginFS(panel) != pluginFS;
        }
        else
            done = SalamanderGeneral->CloseDetachedFS(parent, pluginFS);
        if (!done)
            object->SetDisconnectRequested(false); // still alive
        return done;
    }
    catch (...)
    {
        TRACE_E("DisconnectFS: unexpected exception.");
        return FALSE;
    }
}

BOOL WINAPI
CPluginInterfaceForFS::ConvertPathToInternal(const wchar_t* fsName, int fsNameIndex,
                                             CSalamanderStringBuffer* fsUserPart)
{
    return fsUserPart != nullptr && sally::plugin_abi::IsValidStringBuffer(*fsUserPart);
}

BOOL WINAPI
CPluginInterfaceForFS::ConvertPathToExternal(const wchar_t* fsName, int fsNameIndex,
                                             CSalamanderStringBuffer* fsUserPart)
{
    return fsUserPart != nullptr && sally::plugin_abi::IsValidStringBuffer(*fsUserPart);
}

//
// ****************************************************************************
// CPluginInterfaceForMenuExt
//

int PendingDisconnectPanel = -1; // main thread only
CPluginFSInterfaceAbstract* PendingDisconnectFs = nullptr; // the FS the context menu was opened in

DWORD WINAPI
CPluginInterfaceForMenuExt::GetMenuItemState(int id, DWORD eventMask)
{
    // the items have their state from the masks given to AddMenuItem; this is only called for items added with
    // callGetState == TRUE (none)
    return MENU_ITEM_STATE_ENABLED;
}

BOOL WINAPI
CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent, int id,
                                            DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::ExecuteMenuItem(, , %d, 0x%X)", id, eventMask);
    try
    {
        switch (id)
        {
        case SftpCmdConnect:
            ConnectFromSiteList(parent, PANEL_SOURCE);
            break;

        case SftpCmdDisconnect:
        {
            // the FS of the panel the context menu was opened in, otherwise of the active panel; only when it is ours
            // (otherwise NULL)
            const int panel = PendingDisconnectPanel >= 0 ? PendingDisconnectPanel : PANEL_SOURCE;
            CPluginFSInterfaceAbstract* const expected = PendingDisconnectFs;
            const bool fromContextMenu = PendingDisconnectPanel >= 0;
            PendingDisconnectPanel = -1; // reset whether the command runs or is ignored
            PendingDisconnectFs = nullptr;
            CPluginFSInterfaceAbstract* fs = SalamanderGeneral->GetPanelPluginFS(panel);
            if (fs == nullptr)
                break;
            if (fromContextMenu && fs != expected)
                break; // the panel shows another FS now than when the menu was used: ignore the command
            CPluginFSInterface* object = static_cast<CPluginFSInterface*>(fs);
            object->SetDisconnectRequested(true); // the user asked for it: no "keep the connection?" question
            SalamanderGeneral->DisconnectFSFromPanel(parent, panel);
            if (SalamanderGeneral->GetPanelPluginFS(panel) == fs) // still there: the disconnect did not happen
                object->SetDisconnectRequested(false);
            break;
        }

        case SftpCmdOperations:
            OperationsRegistry.ShowDialog(parent);
            break;

        case SftpCmdConfiguration:
            ShowPluginConfiguration(parent);
            break;

        default:
            TRACE_E("Unknown menu command, ID=" << id);
        }
    }
    catch (...)
    {
        TRACE_E("ExecuteMenuItem: unexpected exception.");
    }
    return FALSE; // do not deselect
}
