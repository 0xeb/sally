// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h> // RECT

// After resume-from-sleep on a 4K/150% display (monitor reattach), the main window
// could collapse to a tiny size - a saved/normal rect was applied blind, with no check that
// it was still a sane size on an actual monitor. This sanitizes a normal-position rect:
//
//   - normalizes it (right>left, bottom>top),
//   - enforces a minimum width/height (minW x minH),
//   - if it is degenerate or does not overlap any monitor work area by a visible margin,
//     resizes (clamped to the monitor) and moves it onto the best/first monitor.
//
// A rect that is already sane and sufficiently visible is returned unchanged. 'monitors' is
// the array of monitor WORK areas; pass count 0 to only enforce the minimum size at the
// saved top-left. Pure so it can be exercised headlessly.
RECT SanitizeWindowRect(RECT rect, const RECT* monitors, int monitorCount, int minW, int minH);
