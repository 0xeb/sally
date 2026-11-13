// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Panel UI services of the SFTP file system: the hot paths of the directory line (click on a part of the path) and the
// context menu (right click on items, on the path, on the empty panel area).
//
// The pure parts are in ops/ (hot_path, sftp_url) and unit-tested; this file only adapts them to Sally.

#include "precomp.h"

#include <shlobj.h>
#include <shellapi.h>

#include "fs_object.h"

#include "fs_interface.h"
#include "hot_path.h"
#include "listing_format.h"
#include "../ui/session_log_window.h"
#include "sftp_url.h"
#include "wait_for_request.h"

namespace
{
// Menu command ids of our own entries. Sally's commands are added with 'SalamanderCommandBase' added, so that the two
// groups cannot be mixed up (the same trick the FTP plugin uses).
enum
{
    CtxCopyPath = 1,
    CtxCopyUrl,
    CtxProperties,
    CtxAttributes,
    CtxRefresh,
    CtxDisconnect,
    CtxServerInfo,
    CtxShowLog
};
const int SalamanderCommandBase = 5000;

// The menu is Sally's own popup menu (CGUIMenuPopupAbstract): it is drawn by Sally, so it follows dark mode.
void AppendSeparator(CGUIMenuPopupAbstract* menu)
{
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE;
    mii.Type = MENU_TYPE_SEPARATOR;
    menu->InsertItem(-1, TRUE, &mii); // -1 + TRUE = append at the end
}

void AppendCommand(CGUIMenuPopupAbstract* menu, int id, const std::wstring& text)
{
    std::wstring copy = text; // the SDK wants a writable buffer
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID;
    mii.Type = MENU_TYPE_STRING;
    mii.String = copy.data();
    mii.ID = id;
    menu->InsertItem(-1, TRUE, &mii);
}

void AppendText(CGUIMenuPopupAbstract* menu, int id, int stringId)
{
    AppendCommand(menu, id, LangStr(stringId));
}

// Sally's own commands for the items (View, Edit, Copy, Delete, ...), the way the FTP plugin lists them. Which ones fit
// the item depends on the type of the command and on the focused item being a file or a directory.
void AppendSalamanderCommands(CGUIMenuPopupAbstract* menu, int panel)
{
    BOOL focusedIsDir = FALSE;
    SalamanderGeneral->GetPanelFocusedItem(panel, &focusedIsDir);

    int index = 0;
    int command = 0;
    std::wstring name;
    BOOL enabled = FALSE;
    int type = sctyUnknown;
    int lastType = sctyUnknown;
    bool any = false;
    while (SPLEnumSalamanderCommandsOwned(SalamanderGeneral, &index, &command, name, &enabled, &type))
    {
        const bool forItems = type == sctyForFocusedFile || type == sctyForFocusedFileOrDirectory ||
                              type == sctyForSelectedFilesAndDirectories;
        if (!enabled || !forItems || (type == sctyForFocusedFile && focusedIsDir))
            continue;
        if (lastType != sctyUnknown && type != lastType)
            AppendSeparator(menu);
        lastType = type;
        AppendCommand(menu, command + SalamanderCommandBase, name);
        any = true;
    }
    if (any)
        AppendSeparator(menu);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Hot paths (pure string work, see ops/hot_path.h)

BOOL WINAPI
CPluginFSInterface::GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset)
{
    try
    {
        return NextHotPath(text, pathLen, offset) ? TRUE : FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL WINAPI
CPluginFSInterface::CompleteDirectoryLineHotPath(CSalamanderStringBuffer* path)
{
    try
    {
        std::wstring text;
        if (path == nullptr || !sally::plugin_abi::ReadStringBuffer(*path, text))
            return FALSE;
        CompleteHotPath(text);
        return sally::plugin_abi::WriteStringBuffer(*path, text) ? TRUE : FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Context menu

// The URL of a remote path of this connection.
std::wstring CPluginFSInterface::UrlOf(const std::string& rawPath) const
{
    return BuildSftpUrl(site_.user, site_.host, site_.port, DisplayPath(rawPath));
}

// Puts the paths (or URLs) of the selected items into the clipboard, one per line.
void CPluginFSInterface::CopyItemsToClipboard(HWND parent, int panel, int selectedFiles, int selectedDirs, bool asUrl)
{
    std::vector<CSelectedItem> items;
    if (!CollectSelection(panel, selectedFiles, selectedDirs, items))
        return;
    std::wstring text;
    for (const CSelectedItem& item : items)
    {
        const std::string path = JoinRemotePath(curRaw_, item.rawName);
        if (!text.empty())
            text += L"\r\n"; // the Windows line break, so that Notepad shows separate lines
        text += asUrl ? UrlOf(path) : DisplayPath(path);
    }
    SalamanderGeneral->CopyTextToClipboard(text.c_str(), -1, FALSE, parent);
}

void CPluginFSInterface::CopyCurrentToClipboard(HWND parent, bool asUrl)
{
    const std::wstring text = asUrl ? UrlOf(curRaw_) : DisplayPath(curRaw_);
    SalamanderGeneral->CopyTextToClipboard(text.c_str(), -1, FALSE, parent);
}

// Fills the menu for the given type. For items: Sally's commands, then ours; for the path and the panel: ours only.
void CPluginFSInterface::BuildContextMenu(CGUIMenuPopupAbstract* menu, int type, int panel) const
{
    if (type == fscmItemsInPanel)
    {
        AppendSalamanderCommands(menu, panel);
        AppendText(menu, CtxCopyPath, IDS_CTX_COPY_PATH);
        AppendText(menu, CtxCopyUrl, IDS_CTX_COPY_URL);
        AppendSeparator(menu);
        AppendText(menu, CtxProperties, IDS_CTX_PROPERTIES);
        AppendText(menu, CtxAttributes, IDS_CTX_ATTRIBUTES);
        AppendSeparator(menu);
        AppendText(menu, CtxRefresh, IDS_CTX_REFRESH);
        AppendText(menu, CtxServerInfo, IDS_CTX_SERVER_INFO);
        AppendText(menu, CtxShowLog, IDS_CTX_SHOW_LOG);
    }
    else // the current path and the panel get the same menu
    {
        AppendText(menu, CtxServerInfo, IDS_CTX_SERVER_INFO);
        AppendText(menu, CtxShowLog, IDS_CTX_SHOW_LOG);
        AppendSeparator(menu);
        AppendText(menu, CtxCopyPath, IDS_CTX_COPY_CUR_PATH);
        AppendText(menu, CtxCopyUrl, IDS_CTX_COPY_CUR_URL);
        AppendSeparator(menu);
        AppendText(menu, CtxRefresh, IDS_CTX_REFRESH);
        AppendSeparator(menu);
        AppendText(menu, CtxDisconnect, IDS_MENU_DISCONNECT);
    }
}

void CPluginFSInterface::RunContextCommand(const wchar_t* fsName, HWND parent, int command, int type, int panel,
                                           int selectedFiles, int selectedDirs)
{
    if (command >= SalamanderCommandBase)
    {
        SalamanderGeneral->PostSalamanderCommand(command - SalamanderCommandBase);
        return;
    }
    const bool items = type == fscmItemsInPanel;
    switch (command)
    {
    case CtxCopyPath:
    case CtxCopyUrl:
        if (items)
            CopyItemsToClipboard(parent, panel, selectedFiles, selectedDirs, command == CtxCopyUrl);
        else
            CopyCurrentToClipboard(parent, command == CtxCopyUrl);
        break;

    case CtxProperties:
        ShowProperties(fsName, parent, panel, selectedFiles, selectedDirs);
        break;

    case CtxAttributes:
        ChangeAttributes(fsName, parent, panel, selectedFiles, selectedDirs);
        break;

    case CtxRefresh:
        SalamanderGeneral->PostRefreshPanelFS(this, FALSE);
        break;

    case CtxServerInfo:
        ShowInfoDialog(fsName, parent); // the same dialog as Ctrl+F1
        break;

    case CtxShowLog:
        SessionLogWindow().Show(LogId()); // returns at once: the window has its own thread
        break;

    case CtxDisconnect:
        // Closing the FS inside its own method would leave Sally with a dead object: the command runs later, when Sally
        // is idle and no FS method is on the stack.
        PendingDisconnectPanel = panel;
        PendingDisconnectFs = this;
        SalamanderGeneral->PostMenuExtCommand(SftpCmdDisconnect, TRUE);
        break;
    }
}

void WINAPI
CPluginFSInterface::ContextMenu(const wchar_t* fsName, HWND parent, int menuX, int menuY, int type, int panel,
                                int selectedFiles, int selectedDirs)
{
    CALL_STACK_MESSAGE5("CPluginFSInterface::ContextMenu(, , , , %d, %d, %d, %d)", type, panel, selectedFiles,
                        selectedDirs);
    CBusyGuard busy(this); // declared first: destroyed last, and may delete 'this' (see ChangePath)
    CGUIMenuPopupAbstract* menu = nullptr;
    try
    {
        if (closeRequested_ || !connected_ || IsWaiting())
            return;
        menu = SalamanderGUI->CreateMenuPopup();
        if (menu == nullptr)
            return;
        BuildContextMenu(menu, type, panel);
        // MENU_TRACK_RETURNCMD: the call returns the chosen id (0 = nothing) instead of posting a message
        const int command = static_cast<int>(
            menu->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON, menuX, menuY, parent, nullptr));
        SalamanderGUI->DestroyMenuPopup(menu);
        menu = nullptr;
        if (command != 0 && !closeRequested_ && connected_)
            RunContextCommand(fsName, parent, command, type, panel, selectedFiles, selectedDirs);
    }
    catch (...) // no exception may leave an SDK entry point
    {
        TRACE_E("ContextMenu: unexpected exception.");
    }
    if (menu != nullptr)
        SalamanderGUI->DestroyMenuPopup(menu);
}

// ---------------------------------------------------------------------------------------------------------------
// Refresh after a change of this folder

// Tells Sally that the current folder changed: this panel refreshes through AcceptChangeOnPathNotification (which also
// refreshes a second panel or a detached FS showing the same folder) and the F3 copies in the disk cache are dropped.
void CPluginFSInterface::NotifyFolderChanged(bool includingSubdirs)
{
    DropListings(curRaw_, includingSubdirs); // at once: the notification below comes later
    SalamanderGeneral->PostChangeOnPathNotification(FullFsPath(curRaw_).c_str(), includingSubdirs ? TRUE : FALSE);
}
