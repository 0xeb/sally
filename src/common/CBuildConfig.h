// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// CBuildConfig — lightweight configuration for standalone script building.
//
// Replaces direct access to the Configuration global and volume detection
// results in BuildScriptMain/Dir/File. Fields here are those read by the
// build-script functions that are NOT already carried by CSelectionSnapshot.
//
// Pure value type — no UI, no global dependencies.

#pragma once

#include <windows.h>

struct CBuildConfig
{
    // --- Volume capabilities (detected from source/target paths) ---
    BOOL SourceSupportsADS = FALSE; // source volume supports Alternate Data Streams
    BOOL TargetSupportsADS = FALSE; // target volume supports ADS
    BOOL TargetIsFAT32 = FALSE;     // target is FAT32 (4 GB file-size limit)

    // --- Configuration.* fields used by BuildScriptMain/Dir/File ---

    // Clear read-only attribute when copying from CD/CDFS media.
    // Read in BuildScriptMain (line ~1203) from Configuration.ClearReadOnly.
    BOOL ClearReadOnly = FALSE;

    // Allow fast directory move on Novell NetWare volumes.
    // Read in BuildScriptMain (line ~1199) from Configuration.NetwareFastDirMove.
    BOOL NetwareFastDirMove = TRUE;

    // Confirm deletion of system/hidden directories.
    // Read in BuildScriptDir (line ~1654) from Configuration.CnfrmSHDirDel.
    BOOL ConfirmDeleteSystemHiddenDir = TRUE;

    // Confirm deletion of non-empty directories.
    // Read in BuildScriptDir (line ~2101) from Configuration.CnfrmNEDirDel.
    BOOL ConfirmDeleteNonEmptyDir = TRUE;
};
