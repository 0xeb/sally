// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Sally's side of its updater, utils\salupdate.exe, which looks for releases on GitHub, has the
// update window and installs (see common/update/UpdaterCommand.h). Sally itself never goes
// online for updates: it starts the updater for Help > Check for Updates, for the check it
// schedules (Configuration > General > Check for updates), and to report an update after it.
// Up to 1.0.36 the CheckVer plugin did this; 1.0.37 retires it.

#pragma once

#include <windows.h>

namespace UpdateCheck
{

// Removes what a release retired and an older updater left behind (each start; cheap when there
// is nothing to do).
void RemoveRetiredFiles();

// After the configuration is loaded and before the plugins are registered: takes over CheckVer's
// settings (once) and removes the plugin, and its registry key unless the configuration is
// imported from an older version's (canDeletePluginKey). TRUE when the configuration must be saved.
BOOL RetireCheckVer(HWND parent, BOOL canDeletePluginKey);

// Once the configuration is saved where it belongs, just before the message loop: reports the
// update Sally was just restarted by, or else starts the scheduled check a little later.
void OnStarted();

// Help > Check for Updates.
void CheckNow(HWND parent);
void ShowWhatsNew(HWND parent);

} // namespace UpdateCheck
