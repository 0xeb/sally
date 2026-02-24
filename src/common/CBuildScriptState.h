// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// CBuildScriptState — transient state used during BuildScriptMain/Dir/File.
//
// Previously these were file-level globals in fileswn6.cpp. Extracting them
// into a struct enables passing through BuildScript functions without global
// state, which is required for headless/parallel script building.

#pragma once

#include <windows.h>

struct CBuildScriptState
{
    // "Skip all" / "Confirm all" flags — user answers propagated across items
    BOOL ConfirmADSLossAll;
    BOOL ConfirmADSLossSkipAll;
    BOOL ConfirmCopyLinkContentAll;
    BOOL ConfirmCopyLinkContentSkipAll;
    BOOL ErrReadingADSIgnoreAll;
    BOOL ErrFileSkipAll;
    BOOL ErrTooLongNameSkipAll;
    BOOL ErrTooLongDirNameSkipAll;
    BOOL ErrTooLongTgtNameSkipAll;
    BOOL ErrTooLongTgtDirNameSkipAll;
    BOOL ErrTooLongSrcDirNameSkipAll;
    BOOL ErrListDirSkipAll;
    BOOL ErrTooBigFileFAT32SkipAll;
    BOOL ErrGetFileSizeOfLnkTgtIgnAll;

    // Tick count for periodic UI interruption checks
    DWORD LastTickCount;

    CBuildScriptState()
    {
        Reset();
    }

    void Reset()
    {
        ConfirmADSLossAll = FALSE;
        ConfirmADSLossSkipAll = FALSE;
        ConfirmCopyLinkContentAll = FALSE;
        ConfirmCopyLinkContentSkipAll = FALSE;
        ErrReadingADSIgnoreAll = FALSE;
        ErrFileSkipAll = FALSE;
        ErrTooLongNameSkipAll = FALSE;
        ErrTooLongDirNameSkipAll = FALSE;
        ErrTooLongTgtNameSkipAll = FALSE;
        ErrTooLongTgtDirNameSkipAll = FALSE;
        ErrTooLongSrcDirNameSkipAll = FALSE;
        ErrListDirSkipAll = FALSE;
        ErrTooBigFileFAT32SkipAll = FALSE;
        ErrGetFileSizeOfLnkTgtIgnAll = FALSE;
        LastTickCount = GetTickCount();
    }
};
