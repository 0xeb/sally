// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Maximum number of shell icon-overlay handlers Sally will load and resolve per file.
//
// The ceiling is dictated by the plugin ABI, not by Windows: a file's resolved overlay is
// stored in CFileData::IconOverlayIndex (a 5-bit field, 0..31) with the top value
// ICONOVERLAYINDEX_NOTUSED (31) reserved as the "no overlay" sentinel. So the maximum valid
// index is 30 and at most 31 handlers can be loaded. (Explorer itself only shows ~11-15
// overlays via its system image list; Sally does its own overlay resolution and is not
// bound by that image-list limit — with the previous cap of 15, TortoiseGit/SVN handlers
// were being squeezed out by OneDrive/Dropbox.)
//
// This value MUST equal ICONOVERLAYINDEX_NOTUSED (plugins/shared/spl_com.h). shiconov.cpp
// static_asserts the two stay in sync so a valid index (cap-1) can never collide with the
// "no overlay" sentinel. Kept dependency-free (a bare literal) so headless units can include
// it without pulling in the plugin SDK header.
#define MAX_SHELL_ICON_OVERLAYS 31

// True once the number of already-loaded overlay handlers has reached the cap, meaning the
// next handler could not be assigned a valid non-sentinel index and must be rejected.
inline bool ShellIconOverlayCapReached(int loadedCount)
{
    return loadedCount >= MAX_SHELL_ICON_OVERLAYS;
}
