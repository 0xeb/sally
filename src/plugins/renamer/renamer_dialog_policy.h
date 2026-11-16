// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// "Remove Emptied Directories" deletes every source folder a rename leaves empty. It is off
// in a new Batch Rename window and in the default Last Used options, and Reset (Ctrl+D)
// promises those same defaults, so it must not be the one place that turns deleting
// folders on.
inline constexpr BOOL RenamerDefaultRemoveEmptiedDirectories = FALSE;

// The close box, Alt+F4 and the window menu's Close arrive as WM_SYSCOMMAND/SC_CLOSE. They
// are the same user request as Esc and the Exit item (whose label shows Alt+F4), so they
// take the same route, including the "Do you want to close Batch Rename?" question.
//
// A plain WM_CLOSE stays a direct close: Sally posts it when it unloads the plugin or exits
// (and then waits for the window to go) and the dialog posts it to itself during a
// critical system shutdown, and none of these may stop for a question. During a critical
// shutdown even a user close goes straight through.
inline bool RenamerSysCommandAsksLikeExit(WPARAM sysCommand, bool criticalShutdown)
{
    // The low four bits of the command are used internally by the system.
    return (sysCommand & 0xFFF0) == SC_CLOSE && !criticalShutdown;
}
