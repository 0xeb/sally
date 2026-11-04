// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

class CFilesWindow;
class CMainWindow;
class CMenuPopup;

void UpdateWindowsTerminalCommandsMenu(CMenuPopup* popup);
DWORD CaptureCommandShellMenuSelection(DWORD commandId, HWND owner, bool helpMode);
bool CommandShellShortcutConflicts(size_t slot);
bool IsAssignedCommandShellShortcut(WORD hotKey);
bool TranslateCommandShellShortcut(CMainWindow* mainWindow, const MSG& message);
bool HandleCommandShellMenuCommand(CMainWindow* mainWindow, CFilesWindow* activePanel,
                                   DWORD commandId);
