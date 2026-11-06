// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h> // DWORD, DROPEFFECT_*, MK_*

// For a drag over Sally's own panel folder, map the keyboard modifiers to a drop
// effect. The old code stripped DROPEFFECT_LINK unconditionally (and even returned
// DROPEFFECT_NONE for the Ctrl+Shift link combo), so Alt+drag / Ctrl+Shift+drag could
// never create a shortcut - the cursor showed "blocked" and no shortcut was offered.
//
// Correct mapping (Explorer conventions):
//   Alt, or Ctrl+Shift  -> DROPEFFECT_LINK  (create shortcut), when the source allows LINK
//   Shift (only)        -> DROPEFFECT_MOVE
//   Ctrl  (only)        -> DROPEFFECT_COPY
//   otherwise           -> the first of Copy/Move the source allows
//
// LINK is only ever a legitimate answer when the SHELL drop target is available to perform it:
// Drop() masks the effect to COPY|MOVE for its own fast path, and the .lnk is created by the
// shell target it forwards to. Nothing in Sally creates a shortcut itself.
//
// So the caller must say whether that target exists. It is reached from the own-folder branch,
// which runs precisely BECAUSE CurDirDropTarget could not be created - and that failure is a
// deterministic function of the path, so the retry at Drop time fails identically. Advertising
// LINK there showed a shortcut cursor and then performed nothing at all: no copy, no move, no
// shortcut. An honest "blocked" cursor is better than a false promise.
//
// (The previous version of this comment asserted the opposite - that LINK "flows to the shell
// drop path" - which is exactly what made the defect easy to miss.)
//
// Pure so the modifier->effect mapping can be exercised headlessly.
//   shellTargetAvailable - a shell IDropTarget exists that could actually create the shortcut
DWORD ComputeOwnFolderDropEffect(DWORD allowedEffects, DWORD keyState,
                                 bool shellTargetAvailable);
