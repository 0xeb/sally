// SPDX-FileCopyrightText: 2023 Open Salamander Authors
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

struct CBuildFilterEntry
{
    const wchar_t* NameW = nullptr;
    BOOL IsDir = FALSE;
    DWORD Attr = 0;
    unsigned __int64 Size = 0;
    FILETIME LastWrite = {};
};

using CBuildFilterPredicate = BOOL (*)(const CBuildFilterEntry& entry, void* context);

struct CBuildADSProbeResult
{
    BOOL HasADS = FALSE;
    BOOL HasProbeError = FALSE;
    unsigned __int64 Size = 0;
    unsigned __int64 OccupiedSpace = 0;
    DWORD WinError = NO_ERROR;
    BOOL OnlyDiscardableStreams = FALSE;
};

using CBuildADSProbe = BOOL (*)(const wchar_t* sourceName,
                                BOOL isDir,
                                DWORD bytesPerCluster,
                                CBuildADSProbeResult* result,
                                void* context);

// Build-time delete confirmation. Lets the snapshot builder handle the
// prompt-sensitive delete cases the legacy builder used to own, without
// common/ depending on gPrompter: production wires the callback to gPrompter,
// tests script it. Default null → the builder rejects those cases (production
// gate unchanged, legacy handles the prompt).
enum class CBuildDeletePromptKind
{
    NonEmptyDir,     // deleting a non-empty directory (CnfrmNEDirDel)
    SystemHiddenDir, // deleting a system/hidden directory (CnfrmSHDirDel)
};

enum class CBuildDeletePromptResult
{
    Proceed, // build the delete
    Skip,    // skip this directory (do not delete it or its parent)
    Cancel,  // abort the whole build
};

using CBuildDeletePrompt = CBuildDeletePromptResult (*)(CBuildDeletePromptKind kind,
                                                         const wchar_t* dirName,
                                                         void* context);

// Build-time ADS-loss confirmation. When a source carries Alternate Data
// Streams the target volume cannot hold, the legacy builder asked "ADS will be
// lost — continue?". This callback lets the snapshot builder ask instead of
// rejecting to legacy. Default null → reject (production gate unchanged). Fires
// only for pure ADS loss; probe errors always reject (an error is not a loss).
enum class CBuildAdsLossPromptResult
{
    Proceed, // build the op, silently dropping the streams
    Skip,    // B3: skip THIS item, keep building the rest (legacy parity)
    Reject,  // abort the build (fall back to legacy)
};

using CBuildAdsLossPrompt = CBuildAdsLossPromptResult (*)(const wchar_t* sourceName,
                                                          bool isDir,
                                                          void* context);

// Build-time "the link, or what it points at?" confirmation for a reparse-point
// DIRECTORY on copy/move. Legacy asked it (copy_move.cpp:2887, via
// CConfirmLinkTgtCopyDlg) and the snapshot builder dropped the question while
// keeping the dialog, so every junction was cloned as a junction with no choice
// offered. That silently loses an ability: a junction copied to another volume
// lands as a link back to the SOURCE path, which dangles on any other machine —
// where the user was expecting the files.
//
// Only the link path crosses this boundary. Describing what the link is (volume
// mount point / junction to X / symlink to X / unresolvable) needs resource
// strings, so the caller's prompt does it and common/ stays resource-free.
//
// Default null → CloneLink, which is exactly the behaviour before the callback
// existed, so headless callers and the production gate are unchanged.
enum class CBuildLinkContentPromptResult
{
    CopyContent,    // follow the link: enumerate the target, copy the real files
    CopyContentAll, // ... and stop asking; every later link copies its content
    CloneLink,      // copy/move the link object itself (the builder's default)
    CloneLinkAll,   // ... and stop asking
    Cancel,         // abort the whole build
};

using CBuildLinkContentPrompt = CBuildLinkContentPromptResult (*)(const wchar_t* linkPath,
                                                                 void* context);

struct CBuildConfig
{
    // --- Volume capabilities (detected from source/target paths) ---
    BOOL SourceSupportsADS = FALSE; // source volume supports Alternate Data Streams
    // The source path is a network path. Only used to recognise the ADS probe
    // false positives that legacy deliberately copied past (see
    // ShouldReportADSProbeError); the feasibility pass has no COperations to
    // read script->SourcePathIsNetwork from, so the flag lives here too.
    BOOL SourcePathIsNetwork = FALSE;    BOOL TargetSupportsADS = FALSE; // target volume supports ADS
    BOOL TargetIsFAT32 = FALSE;     // target is FAT32 (4 GB file-size limit)
    BOOL TargetPathIsEncrypted = FALSE; // target path inherits the Encrypted attribute

    // --- Configuration.* fields used by BuildScriptMain/Dir/File ---

    // Enable the recursive directory tranche in the snapshot builder. Kept
    // opt-in so existing directory rejection tests and production fallback
    // remain explicit until the gate chooses the wider surface.
    BOOL EnableRecursiveDirectories = FALSE;

    // Enable copy/move filters through a small callback so common/ does not
    // depend on panel UI types. The callback is applied to files, matching the
    // legacy recursive builder's filter point.
    BOOL EnableFilters = FALSE;
    BOOL SkipEmptyDirs = FALSE;
    CBuildFilterPredicate FilterPredicate = nullptr;
    void* FilterContext = nullptr;

    // Enable ADS copy accounting when both source and target support ADS.
    // Prompt-heavy loss/error cases are still rejected by the production gate.
    BOOL EnableADS = FALSE;
    BOOL IgnoreADS = FALSE;
    CBuildADSProbe ADSProbe = nullptr;
    void* ADSProbeContext = nullptr;

    // The source volume claimed ADS support but enumerating the streams failed.
    // Legacy (files_window_copy_move.cpp:3678-3726) did TWO things the snapshot
    // builder dropped, neither of which is a path-length ceiling:
    //   1. It filtered known false positives through ShouldReportADSProbeError
    //      (\\tsclient\* -> ERROR_INVALID_FUNCTION, network paths ->
    //      ERROR_INVALID_PARAMETER / ERROR_NO_MORE_ITEMS) and copied ANYWAY.
    //   2. For a real error it asked Retry / Ignore / Ignore All / Cancel, and
    //      every answer except Cancel CONTINUED the build without the streams.
    // Replacing both with a bare `return false` made any probe hiccup abort the
    // whole operation with "error building script". Default null keeps that
    // propagate-the-failure behavior so headless callers are unchanged; the
    // false-positive filter (1) applies regardless of this callback, because it
    // never needed a UI. The builder owns the IgnoreAll latch (CBuildScriptState)
    // so suppression is testable without a UI.
    enum class CBuildADSProbeErrorAction
    {
        Retry,     // probe the streams again (the user fixed the media)
        Ignore,    // copy this item without its streams, keep building
        IgnoreAll, // and stop asking for every later item that fails the same way
        Cancel,    // abort the whole build (the user said Cancel)
    };
    using CBuildADSProbeErrorPrompt = CBuildADSProbeErrorAction (*)(const wchar_t* sourceName,
                                                                    DWORD winError,
                                                                    void* context);
    CBuildADSProbeErrorPrompt ADSProbeErrorCallback = nullptr;
    void* ADSProbeErrorContext = nullptr;

    // A reparse-point FILE (a file symlink, not a junction/dir link) has a
    // directory-entry size of 0 — its real size lives on the link target. Legacy
    // called GetLinkTgtFileSize before accounting for it, so TotalFileSize,
    // OccupiedSpace, the free-space check and the FAT32 4 GB guard all saw the
    // size that would actually be written. Without this the builder accounts a
    // symlinked 8 GB file as 0 bytes: the free-space check passes on a full
    // disk, the progress bar is wrong, and the FAT32 guard — an external
    // filesystem limit that must survive — can never fire for a link.
    enum class CBuildLinkTargetSizeResult
    {
        Resolved, // *size holds the target's real size
        Ignore,   // could not read it; account the entry size and keep going
        Cancel,   // the user aborted the build
    };
    using CBuildLinkTargetSizeQuery = CBuildLinkTargetSizeResult (*)(const wchar_t* linkPath,
                                                                    unsigned __int64* size,
                                                                    void* context);
    CBuildLinkTargetSizeQuery LinkTargetSizeCallback = nullptr;
    void* LinkTargetSizeContext = nullptr;

    // Enable explicit per-item target names for rename masks / copy-of style
    // mapping. The builder never invents mapped names implicitly.
    BOOL EnableExplicitTargetNames = FALSE;

    // Clear read-only attribute when copying from CD/CDFS media.
    // Read in BuildScriptMain (line ~1203) from Configuration.ClearReadOnly.
    BOOL ClearReadOnly = FALSE;

    // Allow fast directory move on Novell NetWare volumes.
    // Read in BuildScriptMain (line ~1199) from Configuration.NetwareFastDirMove.
    BOOL NetwareFastDirMove = TRUE;

    // Master opt-in for snapshot-builder fast-dir-move absorption. Default
    // OFF so the production gate is unchanged (fast moves still route to legacy
    // via the wouldUseFastDirMove bypass) until a direct parity test + manual
    // validation + release soak. When ON, a same-root move of a whole directory
    // into a not-yet-existing target emits a single ocMoveDir op.
    BOOL EnableFastDirMove = FALSE;

    // Confirm deletion of system/hidden directories.
    // Read in BuildScriptDir (line ~1654) from Configuration.CnfrmSHDirDel.
    BOOL ConfirmDeleteSystemHiddenDir = TRUE;

    // Confirm deletion of non-empty directories.
    // Read in BuildScriptDir (line ~2101) from Configuration.CnfrmNEDirDel.
    BOOL ConfirmDeleteNonEmptyDir = TRUE;

    // Build-time delete confirmation callback. When set, the builder asks
    // instead of rejecting the prompt-sensitive delete case. Default null keeps
    // the reject-to-legacy behavior (production gate unchanged).
    CBuildDeletePrompt DeletePromptCallback = nullptr;
    void* DeletePromptContext = nullptr;

    // Build-time ADS-loss confirmation callback. When set, the builder asks
    // instead of rejecting a source whose ADS the target cannot hold. Default
    // null keeps reject-to-legacy (production gate unchanged).
    CBuildAdsLossPrompt AdsLossPromptCallback = nullptr;

    // Copy/move a reparse-point directory as the LINK (clone the
    // raw reparse buffer; same-volume move = single rename). Never the target,
    // unless LinkContentPromptCallback below answers CopyContent.
    BOOL EnableReparseCopyMove = FALSE;

    // Build-time link-content confirmation callback (see
    // CBuildLinkContentPrompt). When set, a reparse-point directory being copied
    // or moved asks whether to follow the link. Default null keeps the
    // clone-the-link-always behaviour. The builder owns both "stop asking"
    // latches (in CBuildScriptState) so the suppression is testable headlessly.
    CBuildLinkContentPrompt LinkContentPromptCallback = nullptr;
    void* LinkContentPromptContext = nullptr;

    // CountSize (Alt+F10): accumulate sizes without emitting ops.
    // CountCompressedSizes = the dialog mode's on-disk sizes (compressed/sparse
    // via IFileSystem::GetCompressedSize); the column modes stay fast.
    BOOL EnableCountSize = FALSE;
    BOOL CountCompressedSizes = FALSE;
    // Fired once per compressed-size failure (unless the answer said stop
    // asking); the production impl shows the legacy AskYesNo. The size falls
    // back to the logical size either way (legacy parity).
    using CBuildCountSizeErrorPrompt = void (*)(const wchar_t* name, DWORD winError,
                                                void* context);
    CBuildCountSizeErrorPrompt CountSizeErrorCallback = nullptr;
    void* CountSizeErrorContext = nullptr;
    void* AdsLossPromptContext = nullptr;

    // A directory could not be listed (access denied, an unresolvable reparse
    // tag such as a WSL symlink -> ERROR_CANT_ACCESS_FILE, a vanished tree...).
    // Legacy BuildScriptDir asked Skip / Skip All / Cancel here and CONTINUED on
    // skip; the snapshot builder replaced it with a bare `return false`, which
    // aborted the whole operation with no message at all. Default null keeps
    // that propagate-the-failure behavior so headless callers are unchanged.
    // The builder owns each "don't ask again" latch (in CBuildScriptState) so the
    // suppression is testable without a UI: after a SkipAll answer the callback
    // must not be invoked again.
    enum class CBuildSkipAction
    {
        Skip,    // ignore this item, keep building the rest
        SkipAll, // ignore this and every later item that hits the same problem
        Cancel,  // abort the whole build (the user said Cancel)
    };
    using CBuildListDirErrorPrompt = CBuildSkipAction (*)(const wchar_t* dir,
                                                          DWORD winError,
                                                          void* context);
    CBuildListDirErrorPrompt ListDirErrorCallback = nullptr;
    void* ListDirErrorContext = nullptr;

    // A file too large for a FAT32 target. Unlike a path-length ceiling — which
    // this codebase deliberately no longer imposes — 4 GB is a property of the
    // FILESYSTEM, not of Sally: FAT32 stores the size in 32 bits, so the write
    // WILL fail. Legacy warned before starting; without it the copy dies after
    // moving 4 GB, once per oversized file. TargetIsFAT32 above gates it.
    using CBuildFat32TooBigPrompt = CBuildSkipAction (*)(const wchar_t* sourceFile,
                                                         void* context);
    CBuildFat32TooBigPrompt Fat32TooBigCallback = nullptr;
    void* Fat32TooBigContext = nullptr;

    // Polled during the recursive walk so the "press ESC to cancel" promise on
    // the wait window is real again (legacy copy_move.cpp:3043, BS_TIMEOUT
    // throttled). Returns true when the user confirmed the abort.
    using CBuildCancelPoll = bool (*)(void* context);
    CBuildCancelPoll CancelPollCallback = nullptr;
    void* CancelPollContext = nullptr;

    // Change-attributes tranche. Opt-in; DEFAULT OFF so the production gate
    // still routes ChangeAttrs to legacy. When on, the builder emits ocChangeAttrs
    // ops. The new attributes are (sourceAttr & ChangeAttrsAnd) | ChangeAttrsOr.
    BOOL EnableChangeAttrs = FALSE;
    DWORD ChangeAttrsAnd = 0xFFFFFFFF;
    DWORD ChangeAttrsOr = 0;
    BOOL ChangeAttrsCompression = FALSE;
    BOOL ChangeAttrsEncryption = FALSE;
    BOOL ChangeAttrsSubDirs = FALSE; // recurse into directory contents

    // Change-case tranche. Opt-in; DEFAULT OFF so the production gate still
    // routes ChangeCase to legacy. When on, the builder emits a rename op whose
    // target leaf is AlterFileNameW(sourceLeaf, format, change). Format/change
    // codes are documented in PathDisplayUtils.h.
    BOOL EnableChangeCase = FALSE;
    int ChangeCaseFormat = 0;
    int ChangeCaseChange = 0;
    BOOL ChangeCaseSubDirs = FALSE; // recurse into directory contents

    // Reparse-point delete absorption. Opt-in; DEFAULT OFF so reparse dirs
    // still reject to legacy. When on, a delete of a reparse-point directory
    // (junction/symlink) emits a single ocDeleteDirLink op that removes the LINK
    // only — the builder never recurses into the link target (data-loss safe).
    BOOL EnableReparseDelete = FALSE;
};
