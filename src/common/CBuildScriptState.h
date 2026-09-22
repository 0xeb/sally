// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// CBuildScriptState — transient state used during BuildScriptMain/Dir/File.
//
// Previously these were file-level globals in files_window_copy_move.cpp. Extracting them
// into a struct enables passing through BuildScript functions without global
// state, which is required for headless/parallel script building.

#pragma once

#include <vector>

#include <windows.h>

struct CBuildScriptState
{
    // CountSize: per-top-level-item TotalSize deltas, in item
    // order — the caller writes them back into panel rows (Size/SizeValid).
    std::vector<unsigned long long> PerItemTotalSizes;

    // "Skip all" / "Confirm all" flags — user answers propagated across items.
    //
    // Only latches with a live reader belong here. The ErrTooLong* family that
    // used to sit alongside these is gone on purpose: it backed a self-imposed
    // path-length ceiling this codebase no longer has. Paths are dynamically
    // owned now, so Sally does not pre-judge a length — it performs the
    // operation and reports whatever the filesystem says. The FAT32 latch below
    // is the opposite case and stays: 4 GB is the filesystem's limit, not ours.
    BOOL ConfirmADSLossAll;
    BOOL ConfirmADSLossSkipAll;
    BOOL ErrListDirSkipAll;
    BOOL ErrTooBigFileFAT32SkipAll;

    // "Ignore All" on the ADS read/probe error dialog. Back after being dropped
    // alongside the ErrTooLong* family, which it never belonged to: a stream
    // enumeration that fails is an error from the FILESYSTEM, not a length Sally
    // pre-judged, and legacy let the user copy on past it.
    BOOL ErrReadingADSIgnoreAll;

    // "Copy the link target / copy the link itself" answered for every later
    // reparse-point directory. These two are back after being dropped with the
    // legacy builder: the dialog they latch (CConfirmLinkTgtCopyDlg) survived the
    // port and was even widened, but nothing asked any more, so a junction was
    // always cloned as a junction whatever the user wanted.
    BOOL ConfirmCopyLinkContentAll;
    BOOL ConfirmCopyLinkContentSkipAll;

    // Tick count for periodic UI interruption checks
    DWORD LastTickCount;

    // A build refused because the source and the target name the same object.
    // Legacy said which case it was and the snapshot builder returned a bare
    // false, so a precise message degraded into the generic "error building
    // script" (and the Move-a-file case stopped being detected at all). The
    // builder stays UI-free: it records the case, the caller picks the string.
    enum class ESelfOpReject
    {
        None,
        CopyFileToItself, // IDS_CANNOTCOPYFILETOITSELF
        MoveFileToItself, // IDS_CANNOTMOVEFILETOITSELF
        MoveDirToItself,  // IDS_CANNOTMOVEDIRTOITSELF
    };
    ESelfOpReject SelfOpReject;

    CBuildScriptState()
    {
        Reset();
    }

    void Reset()
    {
        ConfirmADSLossAll = FALSE;
        ConfirmADSLossSkipAll = FALSE;
        ErrListDirSkipAll = FALSE;
        ErrTooBigFileFAT32SkipAll = FALSE;
        ErrReadingADSIgnoreAll = FALSE;
        ConfirmCopyLinkContentAll = FALSE;
        ConfirmCopyLinkContentSkipAll = FALSE;
        SelfOpReject = ESelfOpReject::None;
        LastTickCount = GetTickCount();
    }
};
