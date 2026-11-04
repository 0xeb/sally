// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "WindowsTerminalService.h"
#include "IRegistry.h"
#include <array>

namespace Sally::ShellProfiles
{
using Shortcuts = std::array<ShellTarget, 3>;
Shortcuts EmptyShortcuts();
bool SameTarget(const ShellTarget& left, const ShellTarget& right);
bool Available(const WindowsTerminalCatalog& catalog, const ShellTarget& target);
// Only exact Ctrl/Shift/Alt + NUMPAD divide chords; -1 means ordinary key handling.
int ShortcutForKey(unsigned key, bool control, bool shift, bool alt);
WORD ShortcutHotKey(size_t slot, bool extended);
Shortcuts LoadShortcuts(IRegistry* registry, HKEY key);
RegistryResult SaveShortcuts(IRegistry* registry, HKEY key, const Shortcuts& shortcuts);
}
