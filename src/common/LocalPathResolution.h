// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// The wide reparse-point walk.
//
// Split out of consts.h so a consumer — production or test — can reach this API
// without pulling in the whole-app header. The walk itself is inherited Open
// Salamander logic ported wide, so both holders stay on this file.
//
// WHAT THE WALK IS FOR. Three volume-identity questions (are two paths on the
// same volume, what is this path's mount point and GUID, is this path on an SSD)
// cannot be answered from a path as typed, because a junction or symlink part
// way along it can move the rest of the path to a different volume. The walk
// follows those links and reports where the path really lands.
//
// WHY IT HAD TO GO WIDE. The narrow form reaches the filesystem through CP_ACP,
// so a link under a directory the active code page cannot spell is not found at
// all — and a walk that finds nothing looks exactly like a path that genuinely
// has no links. The callers then answer volume questions about the wrong path,
// with no indication anything went wrong.

#pragma once

#include <windows.h>

#include <string>

// Result of ResolveLocalPathWithReparsePointsW.
//
// Replaces the narrow form's SEVEN out-parameters, six of which every caller but
// one passes as NULL. The struct exists so a caller asks for what it wants by
// name instead of counting NULLs, and so adding an output later is not a
// signature break.
struct CLocalPathResolutionW
{
    std::wstring ResPath; // where the path really lands after following links

    // FALSE when ResPath ends AT a reparse point and must not be shortened —
    // cutting it would likely land on a different volume.
    BOOL CutResPathIsPossible = TRUE;

    BOOL RootOrCurReparsePointSet = FALSE; // TRUE if the path crossed a local reparse point
    std::wstring RootOrCurReparsePoint;    // full path OF that reparse point (NOT its target)
    std::wstring JunctionOrSymlinkTgt;     // target, only when the FIRST link is a junction/symlink
    int LinkType = 0;                      // 0 UNKNOWN, 2 JUNCTION POINT, 3 SYMBOLIC LINK
    std::wstring NetPath;                  // network path a local symlink led to; then ResPath is its root
};

// Call only for paths whose root (after removing SUBST) is DRIVE_FIXED —
// reparse points make no sense elsewhere. A path containing more than 50 links
// is treated as a loop and comes back as the original path.
void ResolveLocalPathWithReparsePointsW(const wchar_t* path, CLocalPathResolutionW& res);

// Returns the current (last) local reparse point on 'path'; on failure returns
// the plain root and FALSE. 'error' is vestigial — see the implementation.
BOOL GetCurrentLocalReparsePointW(const wchar_t* path, std::wstring& currentReparsePoint);
