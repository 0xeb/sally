// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

class CPluginFSInterface;

// File-system part of the plugin interface: creates and closes the FS objects (one per connection) and handles the
// Change Drive menu items and the Disconnect command.
class CPluginInterfaceForFS : public CPluginInterfaceForFSAbstract
{
public:
    virtual CPluginFSInterfaceAbstract* WINAPI OpenFS(const wchar_t* fsName, int fsNameIndex);
    virtual void WINAPI CloseFS(CPluginFSInterfaceAbstract* fs);

    virtual void WINAPI ExecuteChangeDriveMenuItem(int panel);
    virtual BOOL WINAPI ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                       CPluginFSInterfaceAbstract* pluginFS,
                                                       const wchar_t* pluginFSName, int pluginFSNameIndex,
                                                       BOOL isDetachedFS, BOOL& refreshMenu,
                                                       BOOL& closeMenu, int& postCmd, void*& postCmdParam);
    virtual void WINAPI ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam);

    virtual void WINAPI ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                    const wchar_t* pluginFSName, int pluginFSNameIndex,
                                    CFileData& file, int isDir);
    virtual BOOL WINAPI DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                     CPluginFSInterfaceAbstract* pluginFS,
                                     const wchar_t* pluginFSName, int pluginFSNameIndex);

    // the user part of a path is passed through unchanged
    virtual BOOL WINAPI ConvertPathToInternal(const wchar_t* fsName, int fsNameIndex,
                                              CSalamanderStringBuffer* fsUserPart);
    virtual BOOL WINAPI ConvertPathToExternal(const wchar_t* fsName, int fsNameIndex,
                                              CSalamanderStringBuffer* fsUserPart);

    virtual void WINAPI EnsureShareExistsOnServer(int panel, const wchar_t* server, const wchar_t* share) {}

    // Plugin unload: closes (cancels and joins) the sessions of all FS objects that Salamander has not closed.
    // Busy objects (a wait is on the stack) are marked closed so that the running call deletes them when it unwinds.
    void CloseAllSessions();

private:
    std::vector<CPluginFSInterface*> openFS_; // main thread only
};

// Shows the site list and opens the chosen site in 'panel' (Change Drive item and plugin menu "Connect..."). Repeats the
// dialog while the connect fails, so the user keeps what was typed; ends on success or Close.
void ConnectFromSiteList(HWND parent, int panel);

// The plugin menu: Connect..., Disconnect, Operations..., Session log..., Configuration...
class CPluginInterfaceForMenuExt : public CPluginInterfaceForMenuExtAbstract
{
public:
    virtual DWORD WINAPI GetMenuItemState(int id, DWORD eventMask);
    virtual BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent, int id,
                                        DWORD eventMask);
    virtual BOOL WINAPI HelpForMenuItem(HWND parent, int id);
    virtual void WINAPI BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander) {}
};

// IDs of the plugin menu items
const int SftpCmdConnect = 1;
const int SftpCmdDisconnect = 2;
const int SftpCmdOperations = 3;
const int SftpCmdConfiguration = 4;
const int SftpCmdSessionLog = 5;
// Helper command, not in the menu: posted by the idle timer of a detached FS (see CPluginFSInterface::CloseIdleDetached)
const int SftpCmdCloseIdleDetached = 6;

// The Configuration dialog (defined in sftp.cpp, also reached from the plugin manager)
void ShowPluginConfiguration(HWND parent);

// The panel whose FS the Disconnect command closes: set by the context menu (which posts the command), -1 = the active
// panel (the plugin menu). The command resets it.
extern int PendingDisconnectPanel;
extern CPluginFSInterfaceAbstract* PendingDisconnectFs;
