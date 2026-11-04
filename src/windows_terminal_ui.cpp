// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "windows_terminal_ui.h"

#include "common/WindowsTerminalService.h"
#include "common/unicode/helpers.h"
#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "filesbox.h"
#include "editwnd.h"
#include "mainwnd.h"
#include "menu.h"
#include "ui/IPrompter.h"

namespace
{
    ShellTarget LoadConfiguredTarget()
    {
        ShellTarget target;
        target.kind = static_cast<ShellTargetKind>(Configuration.CommandShellTargetKind);
        target.profileGuid = Configuration.CommandShellProfileGuid;
        target.profileName = Configuration.CommandShellProfileName;
        return target;
    }

    // WT profile names are user-editable (WSL distro names, custom shells) and
    // routinely contain characters outside CP_ACP, so the sanitized name stays wide end to end.
    // It used to be narrowed for the menu and then repaired with
    // SetItemTextW; AddStringItem takes const wchar_t* now, so both steps are gone.
    std::wstring SanitizeProfileName(const std::wstring& name)
    {
        std::wstring sanitized;
        sanitized.reserve(name.size() + 4);
        for (wchar_t ch : name)
        {
            if (ch < L' ' || ch == 0x7f)
                ch = L' ';
            if (ch == L'&')
                sanitized.push_back(L'&');
            sanitized.push_back(ch);
        }
        return sanitized;
    }

    void AddStringItem(CMenuPopup* popup, DWORD id, const wchar_t* text, DWORD state = 0)
    {
        MENU_ITEM_INFO item{};
        item.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID | MENU_MASK_STATE;
        item.Type = MENU_TYPE_STRING;
        item.String = const_cast<wchar_t*>(text);
        item.ID = id;
        item.State = state;
        popup->InsertItem(0xFFFFFFFF, TRUE, &item);
    }

    void AddSeparator(CMenuPopup* popup)
    {
        MENU_ITEM_INFO item{};
        item.Mask = MENU_MASK_TYPE;
        item.Type = MENU_TYPE_SEPARATOR;
        popup->InsertItem(0xFFFFFFFF, TRUE, &item);
    }

    bool IsWindowsTerminalCommand(DWORD commandId)
    {
        return commandId == CM_WT_DEFAULT_PROFILE || commandId == CM_WT_REFRESH_PROFILES ||
               (commandId >= CM_WT_PROFILE_MIN && commandId <= CM_WT_PROFILE_MAX);
    }

    bool CheckRunPolicy(const ShellTarget& target)
    {
        std::wstring executableName;
        if (target.kind == ShellTargetKind::ComSpec)
        {
            CommandShellRequest request;
            CommandShellPolicyResult policy = gCommandShellService != nullptr
                                                  ? gCommandShellService->GetPolicyInfo(request)
                                                  : CommandShellPolicyResult::Error(ERROR_INVALID_PARAMETER);
            if (policy.success)
                executableName = policy.info.executableNameForPolicy;
        }
        else
        {
            executableName = L"wt.exe";
        }

        return !SystemPolicies.GetNoRun() &&
               (!SystemPolicies.GetMyRunRestricted() ||
                SystemPolicies.GetMyCanRun(executableName.c_str()));
    }

    CommandShellResult LaunchTarget(CMainWindow* mainWindow, CFilesWindow* activePanel,
                                    const ShellTarget& target, bool runAsAdministrator)
    {
        const wchar_t* workingDirectory =
            (activePanel->Is(ptDisk) || activePanel->Is(ptZIPArchive)) ? activePanel->GetPathW() : nullptr;

        POINT position{};
        bool usePosition = MultiMonGetDefaultWindowPos(mainWindow->HWindow, &position) != FALSE;
        if (target.kind == ShellTargetKind::ComSpec)
        {
            CommandShellRequest request;
            request.runAsAdministrator = runAsAdministrator;
            request.owner = mainWindow->HWindow;
            if (workingDirectory != nullptr)
                request.workingDirectory = workingDirectory;
            request.windowTitle = LoadStrW(IDS_COMMANDSHELL);
            request.useShowWindow = true;
            request.showWindow = SW_SHOWNORMAL;
            request.usePosition = usePosition;
            request.x = position.x;
            request.y = position.y;
            return gCommandShellService != nullptr
                       ? gCommandShellService->LaunchShell(request)
                       : CommandShellResult::Error(ERROR_INVALID_PARAMETER);
        }

        WindowsTerminalLaunchRequest request;
        request.runAsAdministrator = runAsAdministrator;
        request.owner = mainWindow->HWindow;
        if (workingDirectory != nullptr)
            request.workingDirectory = workingDirectory;
        request.usePosition = usePosition;
        request.x = position.x;
        request.y = position.y;
        return gWindowsTerminalService != nullptr
                   ? gWindowsTerminalService->Launch(target, request)
                   : CommandShellResult::Error(ERROR_INVALID_PARAMETER);
    }

    void LaunchWithFeedback(CMainWindow* mainWindow, CFilesWindow* activePanel,
                            const ShellTarget& target, bool runAsAdministrator)
    {
        if (!CheckRunPolicy(target))
        {
            gPrompter->ShowErrorWithHelp(LoadStrW(IDS_POLICIESRESTRICTION_TITLE),
                                         LoadStrW(IDS_POLICIESRESTRICTION), IDH_GROUPPOLICY);
            return;
        }

        mainWindow->SetDefaultDirectories();
        CommandShellResult result = LaunchTarget(mainWindow, activePanel, target, runAsAdministrator);
        if (!result.success && runAsAdministrator && result.errorCode == ERROR_CANCELLED)
            return; // Declining UAC is an intentional cancellation, not a launch error.
        if (!result.success)
            gPrompter->ShowError(LoadStrW(IDS_ERROREXECPROMPT), GetErrorTextOwned(result.errorCode).c_str());
        else
            result.CloseProcess();
    }
} // namespace

void UpdateWindowsTerminalCommandsMenu(CMenuPopup* popup)
{
    int oldPosition = popup->FindItemPosition(CM_WT_MENU_ROOT);
    if (oldPosition != -1)
        popup->RemoveItemsRange(oldPosition, oldPosition);

    if (gWindowsTerminalService == nullptr)
        return;
    const WindowsTerminalCatalog& catalog = gWindowsTerminalService->Refresh(false);
    if (!catalog.installed)
        return;

    CMenuPopup* terminalMenu = new CMenuPopup();
    if (terminalMenu == nullptr)
        return;

    AddStringItem(terminalMenu, CM_WT_DEFAULT_PROFILE, LoadStrW(IDS_MENU_CMD_WT_DEFAULT));
    const size_t profileCount = std::min<size_t>(catalog.profiles.size(),
                                                 CM_WT_PROFILE_MAX - CM_WT_PROFILE_MIN + 1);
    for (size_t index = 0; index < profileCount; index++)
    {
        std::wstring sanitizedW = SanitizeProfileName(catalog.profiles[index].name);
        DWORD id = CM_WT_PROFILE_MIN + static_cast<DWORD>(index);
        AddStringItem(terminalMenu, id, sanitizedW.c_str());
    }
    if (!catalog.settingsAvailable)
        AddStringItem(terminalMenu, 0, LoadStrW(IDS_MENU_CMD_WT_UNAVAILABLE), MENU_STATE_GRAYED);
    AddSeparator(terminalMenu);
    AddStringItem(terminalMenu, CM_WT_REFRESH_PROFILES, LoadStrW(IDS_MENU_CMD_WT_REFRESH));

    MENU_ITEM_INFO item{};
    item.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID | MENU_MASK_SUBMENU;
    item.Type = MENU_TYPE_STRING;
    item.String = LoadStrW(IDS_MENU_CMD_WINDOWS_TERMINAL);
    item.ID = CM_WT_MENU_ROOT;
    item.SubMenu = terminalMenu;
    int position = popup->FindItemPosition(CM_DEFAULT_SHELL);
    popup->InsertItem(position != -1 ? position : 0xFFFFFFFF, TRUE, &item);
}

DWORD CaptureCommandShellMenuSelection(DWORD commandId, HWND owner, bool helpMode)
{
    // Menu teardown posts WM_COMMAND. Capture Shift while consuming the actual
    // selection event, before a queued release (or a later press) can change it.
    return commandId == CM_DEFAULT_SHELL && !helpMode && MainWindow != nullptr &&
                   owner == MainWindow->HWindow && (GetKeyState(VK_SHIFT) & 0x8000) != 0
               ? CM_DEFAULT_SHELL_ADMIN : commandId;
}

bool HandleCommandShellMenuCommand(CMainWindow* mainWindow, CFilesWindow* activePanel,
                                   DWORD commandId)
{
    if (commandId != CM_DOSSHELL && commandId != CM_DEFAULT_SHELL && commandId != CM_DEFAULT_SHELL_ADMIN &&
        !(commandId >= CM_SHELL_SHORTCUT1 && commandId <= CM_SHELL_SHORTCUT3) &&
        !IsWindowsTerminalCommand(commandId))
        return false;

    if (mainWindow == nullptr || activePanel == nullptr)
        return true;
    activePanel->UserWorkedOnThisPath = TRUE;

    const WindowsTerminalCatalog& catalog = gWindowsTerminalService != nullptr
                                                ? gWindowsTerminalService->Refresh(
                                                      commandId == CM_WT_REFRESH_PROFILES)
                                                : WindowsTerminalCatalog{};
    if (commandId == CM_WT_REFRESH_PROFILES)
        return true;

    ShellTarget target;
    const bool runAsAdministrator = commandId == CM_DEFAULT_SHELL_ADMIN;
    if (commandId == CM_DEFAULT_SHELL)
    {
        PostMessageW(mainWindow->HWindow, WM_USER_CONFIGURATION, 7, 0);
        return true;
    }
    if (commandId >= CM_SHELL_SHORTCUT1 && commandId <= CM_SHELL_SHORTCUT3)
    {
        const size_t slot = commandId - CM_SHELL_SHORTCUT1;
        if (CommandShellShortcutConflicts(slot))
            return true;
        target = Configuration.CommandShellShortcuts[slot];
        if (target.kind == ShellTargetKind::Unassigned)
            return true;
    }
    else if (commandId == CM_WT_DEFAULT_PROFILE)
    {
        target.kind = ShellTargetKind::WindowsTerminalDefault;
    }
    else if (commandId >= CM_WT_PROFILE_MIN && commandId <= CM_WT_PROFILE_MAX)
    {
        size_t index = commandId - CM_WT_PROFILE_MIN;
        if (index >= catalog.profiles.size())
            return true;
        target.kind = ShellTargetKind::WindowsTerminalProfile;
        target.profileGuid = catalog.profiles[index].guid;
        target.profileName = catalog.profiles[index].name;
    }
    else
    {
        target = LoadConfiguredTarget();
        if (target.kind != ShellTargetKind::ComSpec)
        {
            ShellTarget resolved;
            if (!catalog.installed || !gWindowsTerminalService->ResolveTarget(target, resolved))
            {
                gPrompter->ShowError(LoadStrW(IDS_ERROREXECPROMPT),
                                     LoadStrW(IDS_SHELL_TARGET_UNAVAILABLE));
                return true;
            }
            else
            {
                target = std::move(resolved);
            }
        }
    }

    if (!Sally::ShellProfiles::Available(catalog, target))
        gPrompter->ShowError(LoadStrW(IDS_ERROREXECPROMPT),
            LoadStrW(commandId >= CM_SHELL_SHORTCUT1 && commandId <= CM_SHELL_SHORTCUT3
                ? IDS_SHELL_PROFILE_UNAVAILABLE : IDS_SHELL_TARGET_UNAVAILABLE));
    else
        LaunchWithFeedback(mainWindow, activePanel, target, runAsAdministrator);
    return true;
}

bool CommandShellShortcutConflicts(size_t slot)
{
    int plugin = -1, item = -1;
    return Plugins.FindHotKey(Sally::ShellProfiles::ShortcutHotKey(slot, false), TRUE, nullptr, &plugin, &item) ||
           Plugins.FindHotKey(Sally::ShellProfiles::ShortcutHotKey(slot, true), TRUE, nullptr, &plugin, &item);
}

bool IsAssignedCommandShellShortcut(WORD hotKey)
{
    const BYTE modifiers = HIBYTE(hotKey);
    const int slot = Sally::ShellProfiles::ShortcutForKey(LOBYTE(hotKey),
        (modifiers & HOTKEYF_CONTROL) != 0, (modifiers & HOTKEYF_SHIFT) != 0, (modifiers & HOTKEYF_ALT) != 0);
    return slot >= 0 && Configuration.CommandShellShortcuts[slot].kind != ShellTargetKind::Unassigned &&
           !CommandShellShortcutConflicts(slot);
}

bool TranslateCommandShellShortcut(CMainWindow* mainWindow, const MSG& message)
{
    if (mainWindow == nullptr || (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) ||
        !mainWindow->CaptionIsActive || mainWindow->HasLockedUI() || mainWindow->HDisabledKeyboard != nullptr ||
        mainWindow->DragMode || mainWindow->QuickRenameWindowActive() ||
        GetActiveWindow() != mainWindow->HWindow || !IsWindowEnabled(mainWindow->HWindow) || GetCapture() != nullptr)
        return false;
    CFilesWindow* panel = mainWindow->GetActivePanel();
    if (panel == nullptr || (message.hwnd != panel->GetListBoxHWND() && message.hwnd != mainWindow->GetEditLineHWND()))
        return false;
    if (mainWindow->EditWindow != nullptr && mainWindow->EditWindow->Dropped())
        return false;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000))
        return false;
    const int slot = Sally::ShellProfiles::ShortcutForKey(static_cast<unsigned>(message.wParam),
        (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_MENU) & 0x8000) != 0);
    if (slot < 0 || Configuration.CommandShellShortcuts[slot].kind == ShellTargetKind::Unassigned || CommandShellShortcutConflicts(slot))
        return false;
    if ((message.lParam & 0x40000000) == 0) // One launch per press, not every auto-repeat.
        PostMessageW(mainWindow->HWindow, WM_COMMAND, CM_SHELL_SHORTCUT1 + slot, 0);
    return true;
}
