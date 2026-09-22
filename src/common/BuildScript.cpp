// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// BuildScript.cpp — standalone COperations script builder from CSelectionSnapshot.
// See BuildScript.h for interface documentation.

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include <algorithm>
#include <vector>

#include "worker.h"
#include "common/BuildScript.h"
#include "common/AdsPolicy.h"
#include "common/IFileEnumerator.h"
#include "common/IFileSystem.h"
#include "common/PathDisplayUtils.h"
#include "common/SnapshotOperationPlanner.h"
#include "common/unicode/helpers.h"

namespace opplan = sally::operation_planner;

// Helper: allocate a full path string "dir\name" (malloc'd, caller owns).
// Returns NULL on failure.
static char* AllocFullPath(const char* dir, const char* name)
{
    int l1 = (int)strlen(dir);
    int l2 = (int)strlen(name);
    int needSep = (l1 > 0 && dir[l1 - 1] != '\\') ? 1 : 0;
    int len = l1 + needSep + l2;
    char* buf = (char*)malloc(len + 1);
    if (buf == NULL)
        return NULL;
    memcpy(buf, dir, l1);
    if (needSep)
        buf[l1] = '\\';
    memcpy(buf + l1 + needSep, name, l2 + 1);
    return buf;
}

static char* DupAnsiString(const char* text)
{
    if (text == NULL)
        return NULL;

    size_t len = strlen(text);
    char* buf = (char*)malloc(len + 1);
    if (buf == NULL)
        return NULL;
    memcpy(buf, text, len + 1);
    return buf;
}

// Root of a wide path for same-disk comparison: "X:" for drive paths,
// "\\server\share" for UNC, else empty (never matches).
static std::wstring PathRootW(const std::wstring& path)
{
    if (path.size() >= 2 && path[1] == L':')
        return std::wstring(1, (wchar_t)towlower(path[0])) + L":";
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        size_t p = path.find(L'\\', 2);       // end of server
        if (p != std::wstring::npos)
            p = path.find(L'\\', p + 1);      // end of share
        std::wstring root = (p == std::wstring::npos) ? path : path.substr(0, p);
        for (wchar_t& c : root)
            c = (wchar_t)towlower(c);
        return root;
    }
    return std::wstring();
}

static bool SameRootPathW(const std::wstring& a, const std::wstring& b)
{
    std::wstring ra = PathRootW(a);
    return !ra.empty() && ra == PathRootW(b);
}

static bool FastMoveTargetDirExists(const std::wstring& targetDirW)
{
    IFileSystem* fs = gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();
    return fs != nullptr && fs->DirectoryExists(targetDirW.c_str());
}

// legacy re-derives the target-encryption state per
// directory level (copy_move.cpp:2559 GetTargetPathState) — an EXISTING
// encrypted directory inside the target tree encrypts everything copied under
// it. A missing target inherits the parent's state, exactly like legacy.
static bool TargetDirIsEncrypted(const std::wstring& targetDirW, bool inherited)
{
    IFileSystem* fs = gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();
    if (fs == nullptr || targetDirW.empty())
        return inherited;
    const DWORD a = fs->GetFileAttributes(targetDirW.c_str());
    if (a == INVALID_FILE_ATTRIBUTES)
        return inherited;
    return (a & FILE_ATTRIBUTE_ENCRYPTED) != 0;
}

static bool HasTrailingSlashW(const std::wstring& path)
{
    return !path.empty() && (path.back() == L'\\' || path.back() == L'/');
}

static bool IsDotDirectory(const wchar_t* name)
{
    return name != NULL &&
           name[0] == L'.' &&
           (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
}

static bool HasUnsupportedAttributes(DWORD attr)
{
    return (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// Reparse handling below was written for Copy/Move/Delete, where a junction has
// to be copied-as-link or unlinked rather than followed. Counting has no such
// question: legacy had NO reparse special-case for it (copy_move.cpp special
// cased only atDelete :2520 and copy's skip-content-for-links :2755), and when
// counting moved into this builder it silently inherited the veto — which is
// why Ctrl+Q went dead on any tree holding a junction, a WSL symlink or a cloud
// placeholder. Counting walks a reparse dir like any other directory; an
// unresolvable tag then simply fails to enumerate and reaches the Skip / Skip
// All / Cancel prompt, exactly as in the pre-Unicode build.
static bool ReparseNeedsSpecialHandling(EActionType action, DWORD attr)
{
    return HasUnsupportedAttributes(attr) && action != EActionType::CountSize;
}

static bool ShouldPromptForSystemHiddenDelete(EActionType action,
                                              const CBuildConfig& config,
                                              DWORD attr)
{
    return action == EActionType::Delete &&
           config.ConfirmDeleteSystemHiddenDir &&
           (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
}

struct ADSProbeResult
{
    bool HasADS = false;
    bool HasProbeError = false;
    DWORD WinError = NO_ERROR;
    CQuadWord Size;
    CQuadWord OccupiedSpace;
};

static ADSProbeResult ProbeSourceADS(const std::wstring& source,
                                      BOOL isDir,
                                     const CBuildConfig& config,
                                     COperations* script)
{
    ADSProbeResult result;
    if (!config.SourceSupportsADS || config.IgnoreADS)
        return result;
    if (config.ADSProbe == nullptr)
    {
        result.HasProbeError = true;
        return result;
    }

    CBuildADSProbeResult probe = {};
    const DWORD bytesPerCluster = script != NULL ? script->BytesPerCluster : 0;
    if (!config.ADSProbe(source.c_str(), isDir, bytesPerCluster,
                         &probe, config.ADSProbeContext))
    {
        result.HasProbeError = true;
        return result;
    }

    // Legacy filtered known false positives here and copied ANYWAY: \\tsclient\*
    // answers ERROR_INVALID_FUNCTION because RDP redirects have no streams, and
    // SMB paths answer ERROR_INVALID_PARAMETER / ERROR_NO_MORE_ITEMS. Treating
    // those as real errors turns an ordinary copy off a share into a hard
    // "error building script". Note BuildScriptLegacyADSProbe derives
    // HasProbeError from WinError, so this classification has to be
    // authoritative rather than merely additive.
    const BOOL sourceIsNet = config.SourcePathIsNetwork ||
                             (script != NULL && script->SourcePathIsNetwork);
    bool probeFailed = probe.HasProbeError || probe.WinError != NO_ERROR;
    if (probeFailed && probe.WinError != NO_ERROR &&
        !ShouldReportADSProbeError(source.c_str(), probe.WinError, sourceIsNet))
    {
        probeFailed = false; // benign: no streams to lose, carry on
    }

    result.WinError = probeFailed ? probe.WinError : NO_ERROR;
    result.HasProbeError = probeFailed;
    result.HasADS = probe.HasADS && !result.HasProbeError;
    if (result.HasADS)
    {
        result.Size.SetUI64(probe.Size);
        result.OccupiedSpace.SetUI64(probe.OccupiedSpace);
    }

    return result;
}

// The source streams could not be enumerated. Legacy asked Retry / Ignore /
// Ignore All / Cancel and continued the build on anything but Cancel, so a
// transient probe failure cost the streams, not the whole operation.
static CBuildConfig::CBuildADSProbeErrorAction
DecideADSProbeError(const std::wstring& source,
                    DWORD winError,
                    const CBuildConfig& config,
                    CBuildScriptState* state)
{
    if (state != nullptr && state->ErrReadingADSIgnoreAll)
        return CBuildConfig::CBuildADSProbeErrorAction::Ignore;
    if (config.ADSProbeErrorCallback == nullptr)
        return CBuildConfig::CBuildADSProbeErrorAction::Cancel; // headless default: propagate, as before

    const CBuildConfig::CBuildADSProbeErrorAction action =
        config.ADSProbeErrorCallback(source.c_str(), winError, config.ADSProbeErrorContext);
    if (action == CBuildConfig::CBuildADSProbeErrorAction::IgnoreAll && state != nullptr)
        state->ErrReadingADSIgnoreAll = TRUE;
    return action;
}

static bool ConfigureADSForOperation(const std::wstring& source,
                                      BOOL isDir,
                                     const CBuildConfig& config,
                                     COperations* script,
                                     COperation& op,
                                     CBuildScriptState* state = nullptr,
                                     bool* skipItem = nullptr)
{
    ADSProbeResult ads = ProbeSourceADS(source, isDir, config, script);
    while (ads.HasProbeError)
    {
        const CBuildConfig::CBuildADSProbeErrorAction action =
            DecideADSProbeError(source, ads.WinError, config, state);
        if (action == CBuildConfig::CBuildADSProbeErrorAction::Cancel)
            return false;
        if (action != CBuildConfig::CBuildADSProbeErrorAction::Retry)
            break; // Ignore / IgnoreAll: emit the item without its streams
        ads = ProbeSourceADS(source, isDir, config, script);
    }
    if (ads.HasProbeError)
        ads.HasADS = false;
    if (!ads.HasADS)
        return true;
    if (!config.EnableADS || !config.TargetSupportsADS)
    {
        // The target cannot hold the streams. Without a prompt callback, reject
        // to legacy; with one, ask — proceeding drops the streams.
        if (config.AdsLossPromptCallback == nullptr)
            return false;
        CBuildAdsLossPromptResult r = config.AdsLossPromptCallback(
            source.c_str(), isDir != FALSE, config.AdsLossPromptContext);
        // legacy offers a per-item Skip on the ADS-loss prompt;
        // Reject still aborts the whole build, Skip drops only this item.
        if (r == CBuildAdsLossPromptResult::Skip && skipItem != nullptr)
        {
            *skipItem = true;
            return true; // op is NOT built; caller drops the item and continues
        }
        if (r != CBuildAdsLossPromptResult::Proceed)
            return false;
        return true; // op built WITHOUT OPFL_COPY_ADS — streams intentionally lost
    }

    op.OpFlags |= OPFL_COPY_ADS;
    op.Size += ads.Size;
    if (script != NULL)
    {
        script->TotalFileSize += ads.Size;
        script->OccupiedSpace += ads.OccupiedSpace;
    }
    return true;
}

// Feasibility (no prompt): does the ADS situation force a reject of the snapshot
// path? Preserves the exact legacy gate when no callback is set; only pure ADS
// loss (not a probe error) becomes feasible when an AdsLossPromptCallback exists
// — the actual prompt then fires in the build pass (ConfigureADSForOperation).
static bool ADSForcesReject(const std::wstring& source,
                             BOOL isDir,
                            const CBuildConfig& config,
                            COperations* script)
{
    ADSProbeResult ads = ProbeSourceADS(source, isDir, config, script);
    const bool legacyReject = (ads.HasADS || ads.HasProbeError) &&
                              (!config.EnableADS || !config.TargetSupportsADS);
    if (!legacyReject)
        return false;
    if (config.AdsLossPromptCallback != nullptr && ads.HasADS && !ads.HasProbeError)
        return false; // ADS loss is authorizable via the prompt
    return true;
}

struct DirectoryEntry
{
    std::wstring NameW;
    DWORD Attr;
    unsigned __int64 Size;
    FILETIME LastWrite;
    bool IsDir;
};

static bool FilterAcceptsFile(const CBuildConfig& config,
                              const std::wstring& nameW,
                              DWORD attr,
                              unsigned __int64 size,
                              FILETIME lastWrite)
{
    if (!config.EnableFilters || config.FilterPredicate == nullptr)
        return true;

    CBuildFilterEntry entry;
    entry.NameW = nameW.c_str();
    entry.IsDir = FALSE;
    entry.Attr = attr;
    entry.Size = size;
    entry.LastWrite = lastWrite;
    return config.FilterPredicate(entry, config.FilterContext) != FALSE;
}

// Reports an unreadable directory through config.ListDirErrorCallback and maps
// the answer onto "keep going" / "abort". Restores the legacy BuildScriptDir
// prompt (copy_move.cpp:2508/2730/2860), which skipped the directory and kept
// recursing; without a callback the failure propagates exactly as before.
static bool ListDirErrorSaysContinue(const std::wstring& dirW, DWORD err,
                                     const CBuildConfig& config,
                                     CBuildScriptState* state,
                                     bool feasibilityOnly)
{
    if (state != nullptr && state->ErrListDirSkipAll)
        return true; // the user already said Skip All — do not ask again
    if (config.ListDirErrorCallback == nullptr)
        return false;

    // The validate pass walks the same tree before the build pass does. Prompting
    // in both would ask twice per unreadable directory, so feasibility stays
    // silent and simply reports "handleable" — the same rule the delete and ADS
    // prompts already follow. BuildDirectoryTree does the actual asking.
    if (feasibilityOnly)
        return true;

    const CBuildConfig::CBuildSkipAction action =
        config.ListDirErrorCallback(dirW.c_str(), err, config.ListDirErrorContext);
    if (action == CBuildConfig::CBuildSkipAction::Cancel)
        return false;
    if (action == CBuildConfig::CBuildSkipAction::SkipAll && state != nullptr)
        state->ErrListDirSkipAll = TRUE;
    return true;
}

// Asks about a file that cannot fit on a FAT32 target. Three outcomes, not two:
// Proceed (emit the copy as normal and let the write fail at the filesystem —
// the behavior with no callback wired, documented and intended) is a different
// answer from Skip (drop the file, the parent survives) - collapsing them into
// one bool made every headless or callback-less caller silently DROP every
// oversized file from the script instead of attempting the copy at all, the
// opposite of "the copy still runs and the write fails at the filesystem".
enum class Fat32TooBigOutcome
{
    Proceed,
    Skip,
    Cancel,
};

static Fat32TooBigOutcome Fat32TooBigSaysSkip(const std::wstring& fileW,
                                              const CBuildConfig& config,
                                              CBuildScriptState* state)
{
    if (state != nullptr && state->ErrTooBigFileFAT32SkipAll)
        return Fat32TooBigOutcome::Skip; // the user already said Skip All — do not ask again
    if (config.Fat32TooBigCallback == nullptr)
        return Fat32TooBigOutcome::Proceed; // no warning wired: attempt the copy, let it fail at the FS

    const CBuildConfig::CBuildSkipAction action =
        config.Fat32TooBigCallback(fileW.c_str(), config.Fat32TooBigContext);
    if (action == CBuildConfig::CBuildSkipAction::Cancel)
        return Fat32TooBigOutcome::Cancel;
    if (action == CBuildConfig::CBuildSkipAction::SkipAll && state != nullptr)
        state->ErrTooBigFileFAT32SkipAll = TRUE;
    return Fat32TooBigOutcome::Skip;
}

// Mirrors BS_TIMEOUT (consts.h:1332). Kept local so this headless module does
// not take a dependency on the Sally host headers just to throttle a poll.
static const DWORD BUILD_CANCEL_POLL_INTERVAL_MS = 200;

// Legacy polled the wait window every BS_TIMEOUT ms and offered to abort
// (copy_move.cpp:3043). Returns true when the build should stop.
static bool BuildWasCancelled(const CBuildConfig& config, CBuildScriptState* state)
{
    if (config.CancelPollCallback == nullptr)
        return false;
    if (state != nullptr)
    {
        const DWORD now = GetTickCount();
        if (now - state->LastTickCount <= BUILD_CANCEL_POLL_INTERVAL_MS)
            return false;
        state->LastTickCount = now;
    }
    return config.CancelPollCallback(config.CancelPollContext);
}

static bool EnumerateDirectoryEntries(const std::wstring& dirW,
                                      std::vector<DirectoryEntry>& entries,
                                      const CBuildConfig& config,
                                      CBuildScriptState* state,
                                      bool feasibilityOnly = false)
{
    // Interface-mediated (Axis D): the enumerator adds the "\*" pattern and the
    // \\?\ long-path decoration internally, so this loop stays Win32-free and
    // is drivable by a MockFileEnumerator in tests.
    IFileEnumerator* fenum =
        gFileEnumerator != nullptr ? gFileEnumerator : GetWin32FileEnumerator();
    if (fenum == nullptr)
        return false;

    HENUM h = fenum->StartEnum(dirW.c_str(), nullptr);
    if (h == INVALID_HENUM)
    {
        // ERROR_PATH_NOT_FOUND means the directory itself is gone — that must
        // fail the build (emitting ops for a vanished source would create an
        // empty target and fail later in the worker), not read as "empty".
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES)
            return true;
        // Unreadable (denied, unresolvable reparse tag, ...): ask, and on skip
        // return an EMPTY listing so the parent keeps building — that is what
        // legacy did, and it is why the answer is not simply `false`.
        entries.clear();
        return ListDirErrorSaysContinue(dirW, err, config, state, feasibilityOnly);
    }

    FileEnumEntry fe;
    bool truncated = false;
    DWORD truncatedErr = NO_ERROR;
    for (;;)
    {
        EnumResult r = fenum->NextFile(h, fe);
        if (r.noMoreFiles)
            break;
        if (!r.success)
        {
            // Legacy prompted on a FindNextFile failure too and kept whatever it
            // had already collected (copy_move.cpp:2860). Fall out of the loop so
            // the partial listing is still sorted before the caller sees it.
            truncated = true;
            truncatedErr = r.errorCode;
            break;
        }
        if (fe.name.empty() || IsDotDirectory(fe.name.c_str()))
            continue;

        DirectoryEntry entry = {};
        entry.NameW = fe.name;
        entry.Attr = fe.attributes;
        entry.Size = fe.size;
        entry.LastWrite = fe.lastWriteTime;
        entry.IsDir = fe.IsDirectory();
        entries.push_back(entry);
    }
    fenum->EndEnum(h);

    std::sort(entries.begin(), entries.end(),
              [](const DirectoryEntry& left, const DirectoryEntry& right) {
                  return _wcsicmp(left.NameW.c_str(), right.NameW.c_str()) < 0;
              });
    if (truncated)
        return ListDirErrorSaysContinue(dirW, truncatedErr, config, state, feasibilityOnly);
    return true;
}

static bool AddOperation(COperations* script, COperation& op)
{
    script->Add(op);
    return script->IsGood() != FALSE;
}

static DWORD TargetEncryptionFlag(DWORD sourceAttr,
                                   bool targetPathIsEncrypted,
                                   COperations* script)
{
    if (script != NULL && !script->CopyAttrs &&
        ((sourceAttr & FILE_ATTRIBUTE_ENCRYPTED) != 0 ||
         targetPathIsEncrypted))
    {
        return OPFL_AS_ENCRYPTED;
    }
    return 0;
}

static CQuadWord ClusterRoundedSize(const CQuadWord& fileSize, DWORD bytesPerCluster)
{
    if (bytesPerCluster == 0 || fileSize == CQuadWord(0, 0))
        return CQuadWord(0, 0);

    const CQuadWord cluster(bytesPerCluster, 0);
    return fileSize - ((fileSize - CQuadWord(1, 0)) % cluster) + CQuadWord(bytesPerCluster - 1, 0);
}

static bool MoveNeedsCopyAccounting(const COperation& op, const COperations* script)
{
    return (op.OpFlags & OPFL_AS_ENCRYPTED) != 0 ||
           (script != NULL && script->SameRootButDiffVolume) ||
           !op.HasSameRootPath();
}

static bool AddFileOperation(EActionType action,
                             const std::wstring& sourceParentW,
                             const std::wstring& targetParentW,
                             const std::wstring& itemNameW,
                             const std::wstring& targetNameW,
                             unsigned __int64 size,
                             DWORD attr,
                             FILETIME lastWrite,
                             const CBuildConfig& config,
                             COperations* script,
                             CBuildScriptState* state,
                             bool* skippedItem = nullptr,
                             int targetEncryptedOverride = -1)
{
    COperation op;

    if (!FilterAcceptsFile(config, itemNameW, attr, size, lastWrite))
        return true;

    // A reparse-point FILE reports size 0 in the directory entry; the bytes are
    // on the target. Resolve the real size BEFORE the FAT32 guard and before any
    // accounting, exactly where legacy did it — otherwise a 5 GB symlinked file
    // slips past the 4 GB FAT32 check and the free-space estimate under-counts.
    // (Reparse DIRECTORIES are handled by the link/content prompt and never
    // reach here as files.)
    if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
        (action == EActionType::Copy || action == EActionType::Move) &&
        config.LinkTargetSizeCallback != nullptr)
    {
        unsigned __int64 tgtSize = size;
        switch (config.LinkTargetSizeCallback(opplan::JoinPathW(sourceParentW, itemNameW).c_str(),
                                              &tgtSize, config.LinkTargetSizeContext))
        {
        case CBuildConfig::CBuildLinkTargetSizeResult::Cancel:
            return false;
        case CBuildConfig::CBuildLinkTargetSizeResult::Resolved:
            size = tgtSize;
            break;
        case CBuildConfig::CBuildLinkTargetSizeResult::Ignore:
            break; // keep the entry size, as legacy did on Ignore
        }
    }

    // FAT32 stores a file size in 32 bits, so anything past 4 GB - 1 cannot be
    // written there at all. This is a filesystem limit, not one of ours: warn
    // before the copy starts rather than after moving 4 GB of it (legacy
    // copy_move.cpp's FAT_TOO_BIG_FILE arm).
    if (config.TargetIsFAT32 &&
        (action == EActionType::Copy || action == EActionType::Move) &&
        size > 0xFFFFFFFFull)
    {
        switch (Fat32TooBigSaysSkip(opplan::JoinPathW(sourceParentW, itemNameW), config, state))
        {
        case Fat32TooBigOutcome::Cancel:
            return false; // abort the whole build
        case Fat32TooBigOutcome::Skip:
            if (skippedItem != nullptr)
                *skippedItem = true; // drop this file, the parent survives
            return true;
        case Fat32TooBigOutcome::Proceed:
            break; // no callback wired: fall through and emit the op as normal
        }
    }

    if (action == EActionType::ChangeCase)
    {
        // Change-case is a rename: the target leaf is the source leaf with
        // AlterFileNameW applied. Same-name results are a no-op skip.
        const std::wstring alteredW =
            AlterFileNameW(itemNameW.c_str(), config.ChangeCaseFormat,
                           config.ChangeCaseChange, false);
        if (alteredW.empty() || alteredW == itemNameW)
            return true; // no rename needed — not an error
        op.Opcode = ocMoveFile;
        op.OpFlags = 0;
        op.Size = MOVE_FILE_SIZE;
        op.Attr = attr;
        op.SetSourceNameW(sourceParentW, itemNameW);
        op.SetTargetNameW(sourceParentW, alteredW);
        if (!script->FastMoveUsed)
            script->FastMoveUsed = TRUE;
        script->FilesCount++;
        return AddOperation(script, op);
    }

    if (action == EActionType::ChangeAttrs)
    {
        op.Opcode = ocChangeAttrs;
        op.OpFlags = 0;
        op.Attr = attr;
        CQuadWord fileSize((DWORD)(size & 0xFFFFFFFF), (DWORD)(size >> 32));
        op.Size = (config.ChangeAttrsCompression || config.ChangeAttrsEncryption)
                      ? (fileSize >= COMPRESS_ENCRYPT_MIN_FILE_SIZE ? fileSize : COMPRESS_ENCRYPT_MIN_FILE_SIZE)
                      : CHATTRS_FILE_SIZE;
        op.SetSourceNameW(sourceParentW, itemNameW);
        op.NewAttrs = (attr & config.ChangeAttrsAnd) | config.ChangeAttrsOr;
        script->FilesCount++;
        return AddOperation(script, op);
    }

    if (action == EActionType::Delete)
    {
        op.Opcode = ocDeleteFile;
        op.OpFlags = 0;
        op.Size = DELETE_FILE_SIZE;
        op.Attr = attr;
        op.SetSourceNameW(sourceParentW, itemNameW);

        script->FilesCount++;
        return AddOperation(script, op);
    }

    if (action == EActionType::CountSize)
    {
        // Count, do not emit (legacy copy_move.cpp:4060-4112).
        IFileSystem* fs = gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();

        // Calculate Occupied Space has no target path, so nothing has filled
        // BytesPerCluster in - Copy/Move set it from the TARGET before building.
        // Legacy therefore looked the cluster size up from the SOURCE here, on
        // demand; without that, ClusterRoundedSize saw a zero cluster and the
        // command reported an occupied space of zero bytes.
        //
        // 'fs' is optional: a headless host that only exercises the enumerator
        // supplies no filesystem at all, and counting must still work there. The
        // zero-cluster fallback below is exactly the answer for that case.
        if (script->BytesPerCluster == 0 && fs != nullptr)
        {
            VolumeCapabilities caps = {};
            if (fs->QueryVolumeCapabilities(sourceParentW.c_str(), caps).success)
                script->BytesPerCluster = caps.bytesPerCluster;
        }

        CQuadWord fileSize;
        fileSize.SetUI64(size);
        CQuadWord onDisk = fileSize;
        if (config.CountCompressedSizes && fs != nullptr &&
            (attr & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE)) != 0)
        {
            uint64_t compressed = 0;
            const std::wstring fullW = opplan::JoinPathW(sourceParentW, itemNameW);
            FileResult r = fs->GetCompressedSize(fullW.c_str(), &compressed);
            if (r.success)
            {
                onDisk.SetUI64(compressed);
            }
            else if (!script->SkipAllCountSizeErrors)
            {
                if (config.CountSizeErrorCallback != nullptr)
                    config.CountSizeErrorCallback(itemNameW.c_str(), r.errorCode,
                                                  config.CountSizeErrorContext);
                // fall back to the logical size (legacy parity)
            }
        }
        script->Sizes.Add(fileSize);
        script->TotalSize += fileSize;
        // With no cluster size to round to, legacy charged the size itself rather
        // than nothing at all - an unrounded estimate beats a zero.
        script->OccupiedSpace += script->BytesPerCluster != 0
                                     ? ClusterRoundedSize(onDisk, script->BytesPerCluster)
                                     : onDisk;
        // Both of these are on-disk figures in legacy ("TotalFileSize += s"), and the
        // Occupied Space dialog prints them as such; charging the LOGICAL size here
        // made a compressed or sparse selection report its uncompressed size.
        script->TotalFileSize += onDisk;
        script->CompressedSize += onDisk;
        script->FilesCount++;
        return true;
    }

    if (action == EActionType::Convert || action == EActionType::RecursiveConvert)
    {
        op.Opcode = ocConvert;
        op.OpFlags = 0;
        op.Attr = attr;
        op.FileSize = CQuadWord((DWORD)(size & 0xFFFFFFFF), (DWORD)(size >> 32));
        op.Size = op.FileSize >= CONVERT_MIN_FILE_SIZE ? op.FileSize : CONVERT_MIN_FILE_SIZE;
        op.SetSourceNameW(sourceParentW, itemNameW);

        script->FilesCount++;
        return AddOperation(script, op);
    }

    COperationCode fileOp = (action == EActionType::Copy) ? ocCopyFile : ocMoveFile;
    op.Opcode = fileOp;
    const bool effTargetEncrypted = targetEncryptedOverride >= 0
                                        ? targetEncryptedOverride != 0
                                        : config.TargetPathIsEncrypted != FALSE;
    op.OpFlags = TargetEncryptionFlag(attr, effTargetEncrypted, script);
    if (action == EActionType::Move && (op.OpFlags & OPFL_AS_ENCRYPTED) != 0 &&
        script != NULL && !script->ShowStatus)
    {
        script->ShowStatus = TRUE;
    }
    op.Attr = attr;
    op.FileSize = CQuadWord((DWORD)(size & 0xFFFFFFFF), (DWORD)(size >> 32));

    CQuadWord fileSizeLoc = op.FileSize;
    op.Size = fileSizeLoc >= COPY_MIN_FILE_SIZE ? fileSizeLoc : COPY_MIN_FILE_SIZE;

    const std::wstring sourceFullW = opplan::JoinPathW(sourceParentW, itemNameW);

    op.SetSourceNameW(sourceParentW, itemNameW);
    op.SetTargetNameW(targetParentW, targetNameW);

    // Source and target naming the same file is not an operation, and legacy
    // said so precisely before aborting. Copy kept the test but lost the
    // message; Move lost the test as well, so moving a file onto itself was
    // queued as an ocMoveFile with source == target and reported nothing at all.
    //
    // Move compares EXACTLY, as legacy did: a target equal only
    // case-insensitively is a case-only rename, which is a real operation.
    if (action == EActionType::Copy && op.AreSourceAndTargetSamePath())
    {
        if (state != nullptr)
            state->SelfOpReject = CBuildScriptState::ESelfOpReject::CopyFileToItself;
        return false;
    }
    if (action == EActionType::Move && op.AreSourceAndTargetExactlySamePath())
    {
        if (state != nullptr)
            state->SelfOpReject = CBuildScriptState::ESelfOpReject::MoveFileToItself;
        return false;
    }

    if (action == EActionType::Move && (op.OpFlags & OPFL_AS_ENCRYPTED) != 0 &&
        (attr & FILE_ATTRIBUTE_ENCRYPTED) != 0 &&
        script != NULL && !script->SameRootButDiffVolume && op.HasSameRootPath())
    {
        op.OpFlags &= ~OPFL_AS_ENCRYPTED;
    }

    const bool copyLikeOperation = action == EActionType::Copy || MoveNeedsCopyAccounting(op, script);
    if (copyLikeOperation &&
        !ConfigureADSForOperation(sourceFullW, FALSE, config, script, op,
                                  state, skippedItem))
    {
        return false;
    }
    if (skippedItem != nullptr && *skippedItem)
        return true; // B3: item skipped at the ADS-loss prompt; emit nothing

    script->FilesCount++;
    if (copyLikeOperation)
    {
        script->TotalFileSize += fileSizeLoc;
        script->OccupiedSpace += ClusterRoundedSize(fileSizeLoc, script->BytesPerCluster);
    }
    else
    {
        op.Size = MOVE_FILE_SIZE;
        if (!script->FastMoveUsed)
            script->FastMoveUsed = TRUE;
    }
    return AddOperation(script, op);
}

static bool AddPlannedFileOperation(const opplan::CPlannedFileOperation& plan,
                                    const CBuildConfig& config,
                                    COperations* script,
                                    CBuildScriptState* state,
                                    bool* skippedItem = nullptr,
                                    int targetEncryptedOverride = -1)
{
    if (plan.IsDir)
        return false;

    return AddFileOperation(plan.Action,
                            plan.SourceParentW, plan.TargetParentW,
                            plan.ItemNameW, plan.TargetNameW,
                            plan.Size, plan.Attr, plan.LastWrite,
                            config, script, state, skippedItem, targetEncryptedOverride);
}

static bool AddDirectoryDeleteOperation(const std::wstring& sourceParentW,
                                        const std::wstring& itemNameW,
                                        DWORD attr,
                                        COperations* script)
{
    COperation op;
    op.Opcode = ocDeleteDir;
    op.OpFlags = 0;
    op.Size = DELETE_DIR_SIZE;
    op.Attr = attr;
    op.SetSourceNameW(sourceParentW, itemNameW);
    return AddOperation(script, op);
}

static bool AddDirectoryCreateOperation(const std::wstring& sourceParentW,
                                        const std::wstring& targetParentW,
                                        const std::wstring& itemNameW,
                                        const std::wstring& targetNameW,
                                        DWORD attr,
                                        const CBuildConfig& config,
                                        COperations* script,
                                        CBuildScriptState* state,
                                        int& createDirIndex,
                                        bool* skippedItem = nullptr,
                                        int targetEncryptedOverride = -1)
{
    COperation op;
    op.Opcode = ocCreateDir;
    const bool effDirTargetEncrypted = targetEncryptedOverride >= 0
                                           ? targetEncryptedOverride != 0
                                           : config.TargetPathIsEncrypted != FALSE;
    op.OpFlags = OPFL_IGNORE_INVALID_NAME |
                 TargetEncryptionFlag(attr, effDirTargetEncrypted, script);
    if ((op.OpFlags & OPFL_AS_ENCRYPTED) != 0 && script != NULL && !script->ShowStatus)
        script->ShowStatus = TRUE;
    op.Size = CREATE_DIR_SIZE;
    op.Attr = attr;
    op.SetSourceNameW(sourceParentW, itemNameW);
    op.SetTargetNameW(targetParentW, targetNameW);

    const std::wstring sourceDirW = opplan::JoinPathW(sourceParentW, itemNameW);
    if (!ConfigureADSForOperation(sourceDirW, TRUE, config, script, op,
                                  state, skippedItem))
        return false;
    if (skippedItem != nullptr && *skippedItem)
        return true; // B3: whole subtree skipped at the ADS-loss prompt

    createDirIndex = script->Add(op);
    if (!script->IsGood())
        return false;

    return true;
}

static bool AddDirectoryTimeOperation(COperations* script,
                                      int createDirIndex,
                                      FILETIME lastWrite)
{
    if (!script->PreserveDirTime || createDirIndex < 0)
        return true;

    COperation op;
    op.Opcode = ocCopyDirTime;
    op.OpFlags = 0;
    op.Size = CHATTRS_FILE_SIZE;
    op.DirTime = lastWrite;
    op.Attr = 0;
    if (script->At(createDirIndex).HasWideTarget())
        op.SetTargetNameW(script->At(createDirIndex).TargetNameW, std::wstring());
    return AddOperation(script, op);
}

static bool AddCreateDirSkipLabel(COperations* script,
                                  int createDirIndex,
                                  CQuadWord totalFileSizeBeforeDir)
{
    if (createDirIndex < 0)
        return true;

    COperation op;
    op.Opcode = ocLabelForSkipOfCreateDir;
    op.OpFlags = 0;
    op.Size.SetUI64(0);
    op.SkippedDirSize = script->TotalFileSize - totalFileSizeBeforeDir;
    op.Attr = createDirIndex;
    return AddOperation(script, op);
}

// A reparse-point directory delete is absorbable as a single link removal (no
// recursion into the target) when EnableReparseDelete is set.
static bool IsReparseDeleteLink(EActionType action, DWORD attr, const CBuildConfig& config)
{
    return action == EActionType::Delete &&
           (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
           config.EnableReparseDelete != FALSE;
}

// Copy/move of a reparse-point directory operates on the LINK
// (clone the blob / rename the link) unless the caller's link-content prompt
// says to follow it. With no prompt wired this stays link-only, so it remains
// data-loss-safe by construction for every headless caller.
static bool IsReparseCopyMoveLink(EActionType action, DWORD attr, const CBuildConfig& config)
{
    return (action == EActionType::Copy || action == EActionType::Move) &&
           (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
           config.EnableReparseCopyMove != FALSE;
}

// Asks whether a reparse-point directory should be copied as the link or as what
// it points at, and owns the two "stop asking" latches so a headless test can
// prove the callback is not invoked again after an All answer.
//
// Collapses to CopyContent / CloneLink / Cancel: the *All variants are an answer
// plus a latch, never a third outcome.
static CBuildLinkContentPromptResult AskLinkContent(const std::wstring& linkPathW,
                                                    const CBuildConfig& config,
                                                    CBuildScriptState* state)
{
    if (state != nullptr && state->ConfirmCopyLinkContentAll)
        return CBuildLinkContentPromptResult::CopyContent;
    if (state != nullptr && state->ConfirmCopyLinkContentSkipAll)
        return CBuildLinkContentPromptResult::CloneLink;
    if (config.LinkContentPromptCallback == nullptr)
        return CBuildLinkContentPromptResult::CloneLink;

    switch (config.LinkContentPromptCallback(linkPathW.c_str(),
                                             config.LinkContentPromptContext))
    {
    case CBuildLinkContentPromptResult::CopyContentAll:
        if (state != nullptr)
            state->ConfirmCopyLinkContentAll = TRUE;
        return CBuildLinkContentPromptResult::CopyContent;

    case CBuildLinkContentPromptResult::CloneLinkAll:
        if (state != nullptr)
            state->ConfirmCopyLinkContentSkipAll = TRUE;
        return CBuildLinkContentPromptResult::CloneLink;

    case CBuildLinkContentPromptResult::CopyContent:
        return CBuildLinkContentPromptResult::CopyContent;

    case CBuildLinkContentPromptResult::Cancel:
        return CBuildLinkContentPromptResult::Cancel;

    default:
        return CBuildLinkContentPromptResult::CloneLink;
    }
}

static bool IsReparseAbsorbable(EActionType action, DWORD attr, const CBuildConfig& config)
{
    return IsReparseDeleteLink(action, attr, config) ||
           IsReparseCopyMoveLink(action, attr, config);
}

// Emits a single ocDeleteDirLink op removing the junction/symlink itself —
// matching the legacy builder (copy_move.cpp:2519). NEVER recurses.
static bool EmitDeleteDirLinkOp(const std::wstring& fullPath, DWORD attr,
                                COperations* script)
{
    COperation op;
    op.Opcode = ocDeleteDirLink;
    op.OpFlags = 0;
    op.Size = DELETE_DIRLINK_SIZE;
    op.Attr = attr;
    op.SetSourceNameW(fullPath, std::wstring());
    return AddOperation(script, op);
}

static bool BuildDirectoryTree(EActionType action,
                               const std::wstring& sourceParentW,
                               const std::wstring& targetParentW,
                               const std::wstring& itemNameW,
                               const std::wstring& targetNameW,
                               DWORD attr,
                               FILETIME lastWrite,
                               const CBuildConfig& config,
                               COperations* script,
                               CBuildScriptState* state,
                               bool& emittedAny,
                               bool& movedAll,
                               int depth = 0,
                               int inheritedTargetEncrypted = -1)
{
    // legacy counts the directory at BuildScriptDir ENTRY
    // (copy_move.cpp:2443) — fast-moved, self-renamed and prompt-skipped dirs
    // are all part of the totals the progress dialog was tuned against. The
    // late increment under-counted every early-return arm.
    if (script != nullptr)
        script->DirsCount++;
    emittedAny = false;
    movedAll = true;

    opplan::CPlannedSnapshotItem dirPlan;
    if (!opplan::TryPlanChildItem(action,
                                  sourceParentW, targetParentW,
                                  itemNameW, targetNameW,
                                  true, 0, attr, lastWrite,
                                  dirPlan))
    {
        return false;
    }

    if (ReparseNeedsSpecialHandling(action, attr))
    {
        // Reparse-point directory: delete the LINK only (never recurse) when
        // absorption is enabled; otherwise reject to legacy.
        if (IsReparseDeleteLink(action, attr, config))
        {
            emittedAny = true;
            movedAll = true;
            return EmitDeleteDirLinkOp(dirPlan.SourcePathW, attr, script);
        }
        // Copy/move the LINK. Same-volume move is a single rename
        // (ocMoveDir moves the link object atomically); everything else clones
        // the raw reparse buffer (ocCreateDirLink), and a move then removes the
        // source link (ocDeleteDirLink). No arm here enumerates the target.
        //
        // Unless the user asked to follow the link: then nothing is emitted for
        // the link itself and control falls through to the ordinary recursive
        // directory copy below, which enumerates through the reparse point and
        // copies the real files (legacy's "Yes" answer). An unresolvable link
        // surfaces there as an ordinary listing failure, so the Skip/Skip
        // All/Cancel prompt covers it.
        if (!IsReparseCopyMoveLink(action, attr, config))
            return false; // a reparse case this builder does not absorb

        const CBuildLinkContentPromptResult linkAnswer =
            AskLinkContent(dirPlan.SourcePathW, config, state);
        if (linkAnswer == CBuildLinkContentPromptResult::Cancel)
            return false;

        if (linkAnswer == CBuildLinkContentPromptResult::CloneLink)
        {
            const std::wstring sourceLinkW = dirPlan.SourcePathW;
            const std::wstring targetLinkW = dirPlan.HasTarget() ? dirPlan.TargetPathW
                                                                 : opplan::JoinPathW(targetParentW, targetNameW);
            if (action == EActionType::Move &&
                SameRootPathW(sourceLinkW, targetLinkW) &&
                !FastMoveTargetDirExists(targetLinkW))
            {
                COperation op = {};
                op.Opcode = ocMoveDir;
                op.OpFlags = OPFL_IGNORE_INVALID_NAME;
                op.Size = MOVE_DIR_SIZE;
                op.Attr = attr;
                op.SetSourceNameW(sourceLinkW, std::wstring());
                op.SetTargetNameW(targetLinkW, std::wstring());
                if (!script->FastMoveUsed)
                    script->FastMoveUsed = TRUE;
                emittedAny = true;
                movedAll = true;
                return AddOperation(script, op);
            }

            COperation op = {};
            op.Opcode = ocCreateDirLink;
            op.OpFlags = OPFL_IGNORE_INVALID_NAME;
            op.Size = CREATE_DIRLINK_SIZE;
            op.Attr = attr;
            op.SetSourceNameW(sourceLinkW, std::wstring());
            op.SetTargetNameW(targetLinkW, std::wstring());
            if (!AddOperation(script, op))
                return false;
            if (action == EActionType::Move)
            {
                COperation del = {};
                del.Opcode = ocDeleteDirLink;
                del.OpFlags = 0;
                del.Size = DELETE_DIRLINK_SIZE;
                del.Attr = attr;
                del.SetSourceNameW(sourceLinkW, std::wstring());
                if (!AddOperation(script, del))
                    return false;
            }
            emittedAny = true;
            movedAll = true;
            return true;
        }

        // CopyContent: emit nothing for the link and treat it as an ordinary
        // directory from here on, so the walk below enumerates through it.
    }
    // A system/hidden-directory prompt is a delete policy. Copy and move must
    // not inherit it merely because they share this recursive builder.
    const bool needsSHPrompt = ShouldPromptForSystemHiddenDelete(action, config, attr);
    if (needsSHPrompt && config.DeletePromptCallback == nullptr)
    {
        return false;
    }

    const std::wstring sourceDirW = dirPlan.SourcePathW;
    const std::wstring targetDirW = dirPlan.HasTarget() ? dirPlan.TargetPathW : opplan::JoinPathW(targetParentW, targetNameW);

    if (needsSHPrompt)
    {
        CBuildDeletePromptResult r = config.DeletePromptCallback(
            CBuildDeletePromptKind::SystemHiddenDir, sourceDirW.c_str(), config.DeletePromptContext);
        if (r == CBuildDeletePromptResult::Cancel)
            return false;
        if (r == CBuildDeletePromptResult::Skip)
        {
            movedAll = false;
            emittedAny = false;
            return true;
        }
    }

    if ((action == EActionType::Copy || action == EActionType::Move) &&
        ADSForcesReject(sourceDirW, TRUE, config, script))
    {
        return false;
    }

    // a target StrICmp-equal to the source (case-only rename,
    // or the identical path) is a single ocMoveDir in legacy REGARDLESS of the
    // fast-move guards (copy_move.cpp: the else-branch "jen rename" and the
    // StrICmp==0 arm of the emit condition). The target "exists" only because
    // it IS the source; recursing would create-into and merge the directory
    // with itself.
    const bool moveTargetMatchesSource =
        action == EActionType::Move &&
        _wcsicmp(sourceDirW.c_str(), targetDirW.c_str()) == 0;

    // An EXACTLY identical source and target is a directory being moved onto
    // itself, which legacy refused by name (copy_move.cpp's
    // `strcmp(sourcePath, targetPath) == 0` arm -> IDS_CANNOTMOVEDIRTOITSELF).
    // Folding it into the case-rename arm below turned that into a silent
    // ocMoveDir whose source and target are the same directory.
    if (moveTargetMatchesSource && sourceDirW == targetDirW && !sourceDirW.empty())
    {
        if (state != nullptr)
            state->SelfOpReject = CBuildScriptState::ESelfOpReject::MoveDirToItself;
        return false;
    }

    const bool moveIsSelfRename = moveTargetMatchesSource;

    // Fast directory move: a same-root disk move of a whole directory into
    // a not-yet-existing target is a single ocMoveDir rename — matching the
    // legacy builder (copy_move.cpp:2569-2613). Opt-in via EnableFastDirMove so
    // the production gate is unchanged until parity/soak.
    // per-level target-encryption state (copy/move only; else inherit).
    const bool levelTargetEncrypted =
        (action == EActionType::Copy || action == EActionType::Move)
            ? TargetDirIsEncrypted(targetDirW,
                                   inheritedTargetEncrypted >= 0
                                       ? inheritedTargetEncrypted != 0
                                       : config.TargetPathIsEncrypted != FALSE)
            : false;
    const int levelTargetEncryptedInt = levelTargetEncrypted ? 1 : 0;

    if (moveIsSelfRename ||
        (action == EActionType::Move && config.EnableFastDirMove &&
        config.NetwareFastDirMove && script != nullptr &&
        !script->CopySecurity &&
        (script->CopyAttrs || !config.TargetPathIsEncrypted) &&
        !config.EnableFilters && !config.SkipEmptyDirs &&
        !script->SameRootButDiffVolume &&
        SameRootPathW(sourceDirW, targetDirW) &&
        !FastMoveTargetDirExists(targetDirW)))
    {
        COperation op = {};
        op.Opcode = ocMoveDir;
        op.OpFlags = OPFL_IGNORE_INVALID_NAME;
        op.Size = MOVE_DIR_SIZE;
        op.Attr = attr;
        op.SetSourceNameW(sourceDirW, std::wstring());
        op.SetTargetNameW(targetDirW, std::wstring());
        if (!script->FastMoveUsed)
            script->FastMoveUsed = TRUE;
        emittedAny = true;
        movedAll = true;
        return AddOperation(script, op);
    }

    std::vector<DirectoryEntry> entries;
    if (!EnumerateDirectoryEntries(sourceDirW, entries, config, state))
        return false;

    // ChangeAttrs / ChangeCase directory. ChangeAttrs applies the dir's own
    // attribute change then (if SubDirs) recurses. ChangeCase renames contents
    // FIRST and the directory itself LAST (inner-before-outer, matching legacy
    // copy_move.cpp:3137). Both recurse through the same child loop.
    if (action == EActionType::ChangeAttrs || action == EActionType::ChangeCase)
    {
        const bool recurse = (action == EActionType::ChangeAttrs) ? (config.ChangeAttrsSubDirs != FALSE)
                                                                  : (config.ChangeCaseSubDirs != FALSE);

        // ChangeAttrs: the directory's own op comes first.
        if (action == EActionType::ChangeAttrs)
        {
            COperation dop;
            dop.Opcode = ocChangeAttrs;
            dop.OpFlags = 0;
            dop.Attr = attr;
            dop.Size = CHATTRS_FILE_SIZE;
            dop.SetSourceNameW(sourceDirW, std::wstring());
            dop.NewAttrs = (attr & config.ChangeAttrsAnd) | config.ChangeAttrsOr;
            if (!AddOperation(script, dop))
                return false;
            emittedAny = true;
        }

        if (recurse)
        {
            for (const DirectoryEntry& entry : entries)
            {
                if (HasUnsupportedAttributes(entry.Attr))
                    return false;
                opplan::CPlannedSnapshotItem childPlan;
                if (!opplan::TryPlanChildItem(action,
                                              sourceDirW, targetDirW,
                                              entry.NameW, entry.NameW,
                                              entry.IsDir, entry.Size,
                                              entry.Attr, entry.LastWrite,
                                              childPlan))
                    return false;

                if (childPlan.IsDir)
                {
                    bool childEmitted = false;
                    bool childMovedAll = true;
                    if (!BuildDirectoryTree(action,
                                            childPlan.SourceParentW, childPlan.TargetParentW,
                                            childPlan.ItemNameW, childPlan.TargetNameW,
                                            childPlan.Attr, childPlan.LastWrite,
                                            config, script, state, childEmitted, childMovedAll,
                                            depth + 1, levelTargetEncryptedInt))
                        return false;
                    emittedAny = emittedAny || childEmitted;
                }
                else
                {
                    if (!AddPlannedFileOperation(childPlan, config, script, state))
                        return false;
                    emittedAny = true;
                }
            }
        }

        // ChangeCase: the directory's own rename comes LAST (after contents).
        if (action == EActionType::ChangeCase)
        {
            const std::wstring alteredW =
                AlterFileNameW(dirPlan.ItemNameW.c_str(), config.ChangeCaseFormat,
                               config.ChangeCaseChange, true);
            if (!alteredW.empty() && alteredW != dirPlan.ItemNameW)
            {
                COperation dop;
                dop.Opcode = ocMoveDir;
                dop.OpFlags = 0;
                dop.Size = MOVE_DIR_SIZE;
                dop.Attr = attr;
                dop.SetSourceNameW(sourceDirW, std::wstring());
                dop.SetTargetNameW(dirPlan.SourceParentW, alteredW);
                if (!script->FastMoveUsed)
                    script->FastMoveUsed = TRUE;
                if (!AddOperation(script, dop))
                    return false;
            }
            emittedAny = true;
        }

        movedAll = true;
        return true;
    }

    if (action == EActionType::Delete &&
        config.ConfirmDeleteNonEmptyDir &&
        !entries.empty() &&
        depth == 0)
    {
        // P4: without a prompt callback the builder rejects this (legacy owns
        // the prompt). With one, ask and honor the answer.
        // depth == 0 — legacy prompts for THE SELECTED
        // directory only; re-prompting at every recursion level was a prompt
        // storm legacy never showed. The Skip/Cancel answer covers the tree.
        if (config.DeletePromptCallback == nullptr)
            return false;
        CBuildDeletePromptResult r = config.DeletePromptCallback(
            CBuildDeletePromptKind::NonEmptyDir, sourceDirW.c_str(), config.DeletePromptContext);
        if (r == CBuildDeletePromptResult::Cancel)
            return false;
        if (r == CBuildDeletePromptResult::Skip)
        {
            // Do not delete this directory (nor let the parent remove itself).
            movedAll = false;
            emittedAny = false;
            return true;
        }
        // Proceed: fall through and build the delete ops.
    }

    int createDirIndex = -1;
    CQuadWord totalFileSizeBeforeDir = script->TotalFileSize;
    if (action == EActionType::Copy || action == EActionType::Move)
    {
        bool dirAdsSkipped = false;
        if (!AddDirectoryCreateOperation(dirPlan.SourceParentW, dirPlan.TargetParentW,
                                         dirPlan.ItemNameW, dirPlan.TargetNameW, attr, config,
                                         script, state, createDirIndex, &dirAdsSkipped,
                                         levelTargetEncryptedInt))
        {
            return false;
        }
        if (dirAdsSkipped)
        {
            // the directory (and thus its subtree) was skipped at the
            // ADS-loss prompt; the parent keeps building and must survive.
            movedAll = false;
            emittedAny = false;
            return true;
        }
    }

    for (const DirectoryEntry& entry : entries)
    {
        if (BuildWasCancelled(config, state))
            return false;

        opplan::CPlannedSnapshotItem childPlan;
        if (!opplan::TryPlanChildItem(action,
                                      sourceDirW, targetDirW,
                                      entry.NameW, entry.NameW,
                                      entry.IsDir, entry.Size,
                                      entry.Attr, entry.LastWrite,
                                      childPlan))
        {
            return false;
        }

        // Reparse children reject as usual, EXCEPT an absorbable reparse delete:
        // a junction child flows to BuildDirectoryTree (which emits a link op and
        // does NOT recurse), a reparse-file child to a normal link-removing
        // ocDeleteFile below.
        if (ReparseNeedsSpecialHandling(action, entry.Attr) &&
            !IsReparseAbsorbable(action, entry.Attr, config))
            return false;

        if (childPlan.IsDir)
        {
            bool childEmitted = false;
            bool childMovedAll = true;
            if (!BuildDirectoryTree(action,
                                    childPlan.SourceParentW, childPlan.TargetParentW,
                                    childPlan.ItemNameW, childPlan.TargetNameW,
                                    childPlan.Attr, childPlan.LastWrite,
                                    config, script, state, childEmitted, childMovedAll,
                                    depth + 1, levelTargetEncryptedInt))
            {
                return false;
            }
            emittedAny = emittedAny || childEmitted;
            movedAll = movedAll && childMovedAll;
        }
        else
        {
            const bool accepted = FilterAcceptsFile(config, childPlan.ItemNameW,
                                                   childPlan.Attr, childPlan.Size, childPlan.LastWrite);
            if (!accepted)
            {
                movedAll = false;
                continue;
            }
            bool adsSkipped = false;
            if (!AddPlannedFileOperation(childPlan, config, script, state, &adsSkipped,
                                         levelTargetEncryptedInt))
            {
                return false;
            }
            if (adsSkipped)
            {
                movedAll = false; // B3: item skipped; the parent must survive
                continue;
            }
            emittedAny = true;
        }
    }

    if ((action == EActionType::Copy || action == EActionType::Move) &&
        config.SkipEmptyDirs && !emittedAny)
    {
        if (createDirIndex >= 0)
        {
            script->Delete(createDirIndex);
            if (!script->IsGood())
                return false;
        }
        movedAll = false;
        return true;
    }

    if (action == EActionType::Copy || action == EActionType::Move)
    {
        if (!AddDirectoryTimeOperation(script, createDirIndex, lastWrite))
            return false;
    }

    if ((action == EActionType::Move || action == EActionType::Delete) && movedAll)
    {
        if (!AddDirectoryDeleteOperation(dirPlan.SourceParentW, dirPlan.ItemNameW,
                                         attr, script))
        {
            return false;
        }
    }

    if (action == EActionType::Copy || action == EActionType::Move)
    {
        if (!AddCreateDirSkipLabel(script, createDirIndex, totalFileSizeBeforeDir))
            return false;
    }

    emittedAny = true;

    return true;
}

static bool ValidateDirectoryTree(EActionType action,
                                  const std::wstring& sourceParentW,
                                  const std::wstring& targetParentW,
                                  const std::wstring& itemNameW,
                                  const std::wstring& targetNameW,
                                  DWORD attr,
                                  const CBuildConfig& config,
                                  CBuildScriptState* state)
{
    opplan::CPlannedSnapshotItem dirPlan;
    if (!opplan::TryPlanChildItem(action,
                                  sourceParentW, targetParentW,
                                  itemNameW, targetNameW,
                                  true, 0, attr, FILETIME{},
                                  dirPlan))
    {
        return false;
    }

    if (ReparseNeedsSpecialHandling(action, attr))
    {
        // An absorbable reparse-point delete is feasible as a single link
        // removal — feasibility must return here WITHOUT enumerating (never open
        // the junction / touch the link target).
        if (IsReparseAbsorbable(action, attr, config))
            return true;
        return false;
    }
    // Feasibility only (no prompt): a system/hidden dir delete is handleable
    // iff a prompt callback exists; otherwise reject to legacy.
    if (ShouldPromptForSystemHiddenDelete(action, config, attr) &&
        config.DeletePromptCallback == nullptr)
    {
        return false;
    }

    const std::wstring sourceDirW = dirPlan.SourcePathW;
    const std::wstring targetDirW = dirPlan.HasTarget() ? dirPlan.TargetPathW : opplan::JoinPathW(targetParentW, targetNameW);
    if ((action == EActionType::Copy || action == EActionType::Move) &&
        ADSForcesReject(sourceDirW, TRUE, config, nullptr))
    {
        return false;
    }

    std::vector<DirectoryEntry> entries;
    if (!EnumerateDirectoryEntries(sourceDirW, entries, config, state, /*feasibilityOnly*/ true))
        return false;

    // the build pass does not descend for ChangeAttrs/
    // ChangeCase without SubDirs — validating (and enumerating) the subtree
    // here was over-strict: an unreadable grandchild rejected a build that
    // would never touch it. Mirror the build arm's recursion scope.
    if ((action == EActionType::ChangeAttrs && config.ChangeAttrsSubDirs == FALSE) ||
        (action == EActionType::ChangeCase && config.ChangeCaseSubDirs == FALSE))
    {
        return true;
    }

    // Feasibility only — do NOT prompt here (BuildDirectoryTree does the actual
    // prompt during emission). A non-empty delete is handleable iff a prompt
    // callback exists; otherwise reject to legacy.
    if (action == EActionType::Delete &&
        config.ConfirmDeleteNonEmptyDir &&
        !entries.empty() &&
        config.DeletePromptCallback == nullptr)
    {
        return false;
    }

    for (const DirectoryEntry& entry : entries)
    {
        if (BuildWasCancelled(config, state))
            return false;

        opplan::CPlannedSnapshotItem childPlan;
        if (!opplan::TryPlanChildItem(action,
                                      sourceDirW, targetDirW,
                                      entry.NameW, entry.NameW,
                                      entry.IsDir, entry.Size,
                                      entry.Attr, entry.LastWrite,
                                      childPlan))
        {
            return false;
        }

        if (ReparseNeedsSpecialHandling(action, entry.Attr) &&
            !IsReparseAbsorbable(action, entry.Attr, config))
            return false;

        if (childPlan.IsDir)
        {
            if (!ValidateDirectoryTree(action,
                                       childPlan.SourceParentW, childPlan.TargetParentW,
                                       childPlan.ItemNameW, childPlan.TargetNameW,
                                       childPlan.Attr, config, state))
            {
                return false;
            }
        }
        else if (action == EActionType::Copy || action == EActionType::Move)
        {
            if (ADSForcesReject(childPlan.SourcePathW, FALSE, config, nullptr))
                return false;
        }
    }

    return true;
}

static bool ValidateFirstTrancheSnapshot(const CSelectionSnapshot& snapshot,
                                         const CBuildConfig& config,
                                         CBuildScriptState* state,
                                         const std::wstring& sourcePathW,
                                         const std::wstring& targetPathW)
{
    switch (snapshot.Action)
    {
    case EActionType::Delete:
    case EActionType::Convert:
    case EActionType::RecursiveConvert:
        if (sourcePathW.empty())
            return false;
        break;

    case EActionType::CountSize:
        if (!config.EnableCountSize || sourcePathW.empty())
            return false;
        break;

    case EActionType::ChangeAttrs:
        // P5 (opt-in): default OFF routes to legacy. Directories require the
        // recursive-directory surface (checked per item below).
        if (!config.EnableChangeAttrs)
            return false;
        if (sourcePathW.empty())
            return false;
        break;

    case EActionType::ChangeCase:
        // P5 (opt-in): default OFF routes to legacy. Directories require the
        // recursive-directory surface (checked per item below).
        if (!config.EnableChangeCase)
            return false;
        if (sourcePathW.empty())
            return false;
        break;

    case EActionType::Copy:
    case EActionType::Move:
        if (sourcePathW.empty() ||
            targetPathW.empty())
        {
            return false;
        }
        if (!opplan::IsDefaultMask(snapshot.Mask) && !config.EnableExplicitTargetNames)
            return false;
        break;

    default:
        return false;
    }

    for (const CSnapshotItem& item : snapshot.Items)
    {
        if (item.IsDir && snapshot.Action == EActionType::Convert)
            return false;
        if (item.IsDir && !config.EnableRecursiveDirectories)
            return false;

        opplan::CPlannedSnapshotItem plan;
        if (!opplan::TryPlanSnapshotItem(snapshot, config, item, plan))
            return false;

        if (!plan.IsDir)
            continue;

        if (item.IsDir && ReparseNeedsSpecialHandling(snapshot.Action, item.Attr) &&
            !IsReparseAbsorbable(snapshot.Action, item.Attr, config))
            return false;

        if (item.IsDir &&
            !ValidateDirectoryTree(snapshot.Action,
                                   plan.SourceParentW, plan.TargetParentW,
                                   plan.ItemNameW, plan.TargetNameW,
                                   item.Attr, config, state))
        {
            return false;
        }
    }

    return true;
}

BOOL BuildScriptFromSnapshot(
    const CSelectionSnapshot& snapshot,
    const CBuildConfig& config,
    CBuildScriptState& state,
    COperations* script)
{
    if (script == NULL)
        return FALSE;

    const std::wstring& sourcePathW = snapshot.SourcePathW;
    const std::wstring& targetPathW = snapshot.TargetPathW;

    if (!ValidateFirstTrancheSnapshot(snapshot, config, &state, sourcePathW, targetPathW))
        return FALSE;

    // Configure COperations fields from snapshot options
    script->IsCopyOrMoveOperation = (snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move);
    script->IsCopyOperation = (snapshot.Action == EActionType::Copy);
    script->OverwriteOlder = snapshot.OverwriteOlder;
    script->CopySecurity = snapshot.CopySecurity;
    script->CopyAttrs = snapshot.CopyAttrs;
    script->PreserveDirTime = snapshot.PreserveDirTime;
    script->TargetPathSupADS = config.TargetSupportsADS;
    script->InvertRecycleBin = snapshot.InvertRecycleBin;
    script->StartOnIdle = snapshot.StartOnIdle;
    if (snapshot.UseSpeedLimit && snapshot.SpeedLimit > 0)
    {
        script->ChangeSpeedLimit = TRUE;
        script->SetSpeedLimit(TRUE, snapshot.SpeedLimit);
    }

    // Set work paths for change notifications
    script->SetWorkPath1W(sourcePathW.c_str(), TRUE);
    if (snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move)
    {
        script->SetWorkPath2W(targetPathW.c_str(), TRUE);
    }

    // ClearReadOnly mask: if ClearReadOnly config is set, remove FILE_ATTRIBUTE_READONLY
    if (config.ClearReadOnly)
        script->ClearReadonlyMask = ~FILE_ATTRIBUTE_READONLY;

    // ChangeAttrs: mirror the snapshot's attr masks into a working config
    // so AddFileOperation can compute the new attributes.
    CBuildConfig runConfig = config;
    if (snapshot.Action == EActionType::ChangeAttrs)
    {
        runConfig.ChangeAttrsAnd = snapshot.AttrsData.AttrAnd;
        runConfig.ChangeAttrsOr = snapshot.AttrsData.AttrOr;
        runConfig.ChangeAttrsCompression = snapshot.AttrsData.ChangeCompression;
        runConfig.ChangeAttrsEncryption = snapshot.AttrsData.ChangeEncryption;
        runConfig.ChangeAttrsSubDirs = snapshot.AttrsData.SubDirs;
    }
    if (snapshot.Action == EActionType::ChangeCase)
    {
        runConfig.ChangeCaseFormat = snapshot.ChangeCaseData.FileNameFormat;
        runConfig.ChangeCaseChange = snapshot.ChangeCaseData.Change;
        runConfig.ChangeCaseSubDirs = snapshot.ChangeCaseData.SubDirs;
    }

    // Process each item in the snapshot
    for (size_t i = 0; i < snapshot.Items.size(); i++)
    {
        const CSnapshotItem& item = snapshot.Items[i];
        // CountSize: record the per-item TotalSize delta for the
        // caller's panel-row writeback (Size/SizeValid).
        const CQuadWord totalBeforeItem = script->TotalSize;
        opplan::CPlannedSnapshotItem plan;
        if (!opplan::TryPlanSnapshotItem(snapshot, runConfig, item, plan))
            return FALSE;

        if (!plan.IsDir)
        {
            bool adsSkipped = false; // B3: Skip drops this item, build continues
            if (!AddPlannedFileOperation(plan, runConfig, script, &state, &adsSkipped))
            {
                return FALSE;
            }
            if (snapshot.Action == EActionType::CountSize)
                state.PerItemTotalSizes.push_back(
                    (script->TotalSize - totalBeforeItem).Value);
            continue;
        }

        bool emittedAny = false;
        bool movedAll = true;
        if (!BuildDirectoryTree(snapshot.Action,
                                plan.SourceParentW, plan.TargetParentW,
                                plan.ItemNameW, plan.TargetNameW,
                                item.Attr, item.LastWrite,
                                runConfig, script, &state, emittedAny, movedAll))
        {
            return FALSE;
        }

        if (snapshot.Action == EActionType::CountSize)
            state.PerItemTotalSizes.push_back(
                (script->TotalSize - totalBeforeItem).Value);
    }

    // CountSize accumulates TotalSize directly (no ops exist);
    // recomputing from operations would zero it.
    if (snapshot.Action == EActionType::CountSize)
        return TRUE;

    // Compute TotalSize from all operations
    CQuadWord totalSize(0, 0);
    for (int i = 0; i < script->Count; i++)
        totalSize += script->At(i).Size;
    script->TotalSize = totalSize;

    return TRUE;
}
