// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "usermenu.h"
#include "execute.h"
#include "plugins.h"
#include "fileswnd.h"
#include "dialogs.h"
#include "worker.h"
#include "cache.h"
#include "pack.h"
#include "shellib.h"
#include "filesbox.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/unicode/AnsiToolPathPolicy.h"
#include "common/IFileEnumerator.h" // loss detector
#include "common/unicode/CopyNamePolicy.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/unicode/WideVariableExpansion.h"
#include "common/FileListTextEncoding.h"
#include "common/text/CaseFolding.h"
#include "common/SalPathWide.h"
#include "common/IEnvironment.h"
#include "common/IFileSystem.h"
#include "common/fsutil.h"
#include "common/widepath.h"

#include "common/AdsPolicy.h"
#include "common/BuildScript.h"
#include "common/CBuildScriptState.h"
#include "common/CSelectionSnapshot.h"
#include <limits>

CSelectionSnapshot CFilesWindow::TakeSnapshot(CActionType type, int selCount,
                                              int* selection, CFileData* oneFile)
{
    CSelectionSnapshot snap;
    snap.SourcePathW = GetPathW();

    // Map CActionType to EActionType
    switch (type)
    {
    case atCopy:
        snap.Action = EActionType::Copy;
        break;
    case atMove:
        snap.Action = EActionType::Move;
        break;
    case atDelete:
        snap.Action = EActionType::Delete;
        break;
    case atCountSize:
        snap.Action = EActionType::CountSize;
        break;
    case atChangeAttrs:
        snap.Action = EActionType::ChangeAttrs;
        break;
    case atChangeCase:
        snap.Action = EActionType::ChangeCase;
        break;
    case atRecursiveConvert:
        snap.Action = EActionType::RecursiveConvert;
        break;
    case atConvert:
        snap.Action = EActionType::Convert;
        break;
    }

    // Capture selected items
    if (selCount > 0 || oneFile != NULL)
    {
        int i = 0;
        do
        {
            const CFileData* file;
            if (selCount > 1 || oneFile == NULL)
            {
                file = (selection[i] < Dirs->Count)
                           ? &Dirs->At(selection[i])
                           : &Files->At(selection[i] - Dirs->Count);
            }
            else
            {
                file = oneFile;
            }
            i++;

            CSnapshotItem item = {};
            item.NameW = file->Name;
            item.IsDir = (file->Attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
            item.Size = file->Size.Value;
            item.Attr = file->Attr;
            item.LastWrite = file->LastWrite;

            snap.Items.push_back(std::move(item));
        } while (i < selCount);
    }

    return snap;
}

namespace
{
BOOL IsSnapshotBuilderDefaultMask(const wchar_t* mask)
{
    return mask == NULL || wcscmp(mask, L"*.*") == 0;
}

std::wstring MaskNameW(const std::wstring& name, const wchar_t* mask)
{
    if (mask == NULL)
        return name;

    const std::wstring maskW = mask;
    int ignPoints = 0;
    for (wchar_t ch : name)
        if (ch == L'.')
            ignPoints++;
    for (wchar_t ch : maskW)
    {
        if (ch == L'.')
        {
            ignPoints--;
            break;
        }
    }
    if (ignPoints < 0)
        ignPoints = 0;

    size_t n = 0;
    std::wstring out;
    out.reserve(name.length() + maskW.length());
    for (size_t s = 0; s < maskW.length(); ++s)
    {
        switch (maskW[s])
        {
        case L'*':
            while (n < name.length())
            {
                if (name[n] == L'.')
                {
                    if (ignPoints > 0)
                        ignPoints--;
                    else
                        break;
                }
                out.push_back(name[n++]);
            }
            break;

        case L'?':
            if (n < name.length())
            {
                if (name[n] == L'.')
                {
                    if (ignPoints > 0)
                    {
                        ignPoints--;
                        out.push_back(name[n++]);
                    }
                }
                else
                    out.push_back(name[n++]);
            }
            break;

        case L'.':
            out.push_back(L'.');
            while (n < name.length())
            {
                if (name[n] == L'.')
                {
                    if (ignPoints > 0)
                        ignPoints--;
                    else
                        break;
                }
                n++;
            }
            if (n < name.length() && name[n] == L'.')
                n++;
            break;

        default:
            out.push_back(maskW[s]);
            if (n < name.length())
            {
                if (name[n] != L'.')
                    n++;
                else if (ignPoints > 0)
                {
                    ignPoints--;
                    n++;
                }
            }
            break;
        }
    }

    while (!out.empty() && out.back() == L'.')
        out.pop_back();
    return out;
}

BOOL PopulateSnapshotTargetNames(CSelectionSnapshot& snapshot, const wchar_t* mask)
{
    if (IsSnapshotBuilderDefaultMask(mask))
        return TRUE;

    for (CSnapshotItem& item : snapshot.Items)
    {
        const std::wstring& sourceNameW = item.NameW;
        if (sourceNameW.empty())
            return FALSE;

        std::wstring targetNameW = MaskNameW(sourceNameW, mask);
        if (targetNameW.empty())
            return FALSE;

        item.TargetNameW = targetNameW;
        item.HasTargetName = true;
    }
    return TRUE;
}

// Masked DELETE rides the generic filter seam: files are matched
// against the mask at every level (legacy semantics); directories always
// traverse and delete only when emptied (movedAll gating already does that).
BOOL SnapshotDeleteMaskPredicate(const CBuildFilterEntry& entry, void* context)
{
    const wchar_t* mask = static_cast<const wchar_t*>(context);
    if (mask == NULL || entry.IsDir)
        return TRUE;
    // AgreeMask is wide now; match natively against NameW
    // instead of the old ANSI mirror + narrowing-loss refusal, which used to
    // reject any name outside CP_ACP even though it could match validly.
    const wchar_t* name = entry.NameW;
    if (name == NULL || name[0] == 0)
        return FALSE;
    const wchar_t* dot = wcsrchr(name, L'.');
    const BOOL hasExtension = dot != NULL && dot[1] != 0;
    return AgreeMask(name, mask, hasExtension, FALSE);
}

BOOL SnapshotFilterPredicate(const CBuildFilterEntry& entry, void* context)
{
    CCriteriaData* criteria = static_cast<CCriteriaData*>(context);
    if (criteria == NULL)
        return TRUE;

    WIN32_FIND_DATAW data = {};
    data.dwFileAttributes = entry.Attr;
    data.nFileSizeLow = (DWORD)(entry.Size & 0xFFFFFFFF);
    data.nFileSizeHigh = (DWORD)(entry.Size >> 32);
    data.ftLastWriteTime = entry.LastWrite;
    if (entry.NameW == NULL || entry.NameW[0] == L'\0')
        return FALSE;
    lstrcpynW(data.cFileName, entry.NameW, MAX_PATH);

    return criteria->AgreeMasksAndAdvanced(&data);
}

BOOL DirectoryTreeNeedsLegacyADS(const std::wstring& sourcePathW, BOOL targetSupADS)
{
    std::wstring searchPathW = sourcePathW;
    if (!searchPathW.empty() && searchPathW.back() != L'\\' && searchPathW.back() != L'/')
        searchPathW.push_back(L'\\');
    searchPathW.push_back(L'*');

    WIN32_FIND_DATAW data = {};
    HANDLE find = gFileSystem->FindFirstFile(searchPathW.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return TRUE;

    do
    {
        if (data.cFileName[0] == L'\0' ||
            data.cFileName[0] == L'.' &&
                (data.cFileName[1] == L'\0' || data.cFileName[1] == L'.' && data.cFileName[2] == L'\0'))
        {
            continue;
        }

        const std::wstring childPathW = sally::unicode::BuildPanelChildPathW(
            sourcePathW, data.cFileName);
        const BOOL isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        CQuadWord adsSize;
        DWORD adsWinError = NO_ERROR;
        if (CheckFileOrDirADS(childPathW, isDir, &adsSize, NULL, NULL,
                              &adsWinError, 0, NULL, NULL) ||
            adsWinError != NO_ERROR)
        {
            gFileSystem->CloseFind(find);
            // Pure ADS loss is handleable via the build-time
            // AdsLossPromptCallback now; only a PROBE ERROR still forces
            // legacy (an error is not a loss - the builder rejects it).
            return adsWinError != NO_ERROR;
        }

        if (isDir && DirectoryTreeNeedsLegacyADS(childPathW, targetSupADS))
        {
            gFileSystem->CloseFind(find);
            return TRUE;
        }
    } while (gFileSystem->FindNextFile(find, &data));

    DWORD err = GetLastError();
    gFileSystem->CloseFind(find);
    return err != ERROR_NO_MORE_FILES;
}

// ShouldReportADSProbeError + NormalizeADSReadErrorResponse moved to
// common/AdsPolicy.h (pure policy, shared with private tests).

} // namespace

// Public routing-gate predicate (declared in fileswnd.h): TRUE when a snapshot
// selection carries ADS the snapshot builder cannot yet handle and must stay
// on the legacy builder.
BOOL SnapshotSelectionNeedsLegacyADS(CActionType type, BOOL sourceSupADS,
                                     BOOL targetSupADS,
                                     const CSelectionSnapshot& snapshot,
                                     const wchar_t* sourcePath)
{
    if ((type != atCopy && type != atMove) || !sourceSupADS || sourcePath == NULL || sourcePath[0] == L'\0')
        return FALSE;

    for (const CSnapshotItem& item : snapshot.Items)
    {
        const std::wstring& itemNameW = item.NameW;
        const wchar_t* itemSourceParent =
            !item.SourceParentW.empty() ? item.SourceParentW.c_str() : sourcePath;
        const std::wstring fullPathW = sally::unicode::BuildPanelChildPathW(
            itemSourceParent, itemNameW.c_str());

        CQuadWord adsSize;
        DWORD adsWinError = NO_ERROR;
        if (CheckFileOrDirADS(fullPathW, item.IsDir, &adsSize, NULL, NULL,
                              &adsWinError, 0, NULL, NULL) ||
            adsWinError != NO_ERROR)
        {
            return adsWinError != NO_ERROR; // loss is promptable now
        }

        if (item.IsDir && DirectoryTreeNeedsLegacyADS(fullPathW, targetSupADS))
            return TRUE;
    }

    return FALSE;
}

// Declared later in this TU (the legacy builder region); the production
// prompt callbacks below reuse them for exact legacy parity.
void GetADSStreamsNames(const std::wstring& fileNameW, BOOL isDir,
                        std::wstring& list);

namespace
{

// Production build-time prompts for the snapshot builder - the
// same wording, buttons and answer mapping as the legacy builder's inline
// prompts (kb decision 2026-07-03: build-time prompts route via CBuildConfig
// callbacks to gPrompter, not IWorkerObserver).
CBuildDeletePromptResult SnapshotDeletePrompt(CBuildDeletePromptKind kind,
                                               const wchar_t* name,
                                               void* /*context*/)
{
    const int msgId = kind == CBuildDeletePromptKind::SystemHiddenDir
                          ? IDS_DELETESHDIR
                          : IDS_NONEMPTYDIRDELCONFIRM;
    std::wstring msg = FormatStrW(LoadStrW(msgId), name != NULL ? name : L"");
    PromptResult res = gPrompter->AskYesNoCancel(LoadStrW(IDS_QUESTION), msg.c_str());
    if (MainWindow != NULL)
        UpdateWindow(MainWindow->HWindow);
    if (res.type == PromptResult::kYes)
        return CBuildDeletePromptResult::Proceed;
    if (res.type == PromptResult::kNo)
        return CBuildDeletePromptResult::Skip;
    return CBuildDeletePromptResult::Cancel;
}

struct SnapshotAdsLossContext
{
    CActionType Type;
    CBuildScriptState* BsState;
    HWND Parent;
};

struct SnapshotLinkContentContext
{
    HWND Parent;
    bool* CancelledByUser;
};

// CountSize compressed-size error: the legacy AskYesNo ("skip all
// further errors?") — YES sets the script's sticky flag; the size falls back to
// the logical size either way.
void SnapshotCountSizeErrorPrompt(const wchar_t* name, DWORD winError, void* context)
{
    COperations* script = static_cast<COperations*>(context);
    std::wstring msg = (name != NULL ? std::wstring(name) : std::wstring()) +
                       L": " + GetErrorTextOwned(winError).c_str();
    if (gPrompter->AskYesNo(LoadStrW(IDS_ERRORTITLE), msg.c_str()).type ==
        PromptResult::kYes)
    {
        script->SkipAllCountSizeErrors = TRUE;
    }
    if (MainWindow != NULL)
        UpdateWindow(MainWindow->HWindow);
}

// A directory could not be listed. Restores the legacy BuildScriptDir prompt
// before the legacy recursive builder was removed): "Cannot read
// directory ...", Skip / Skip All / Cancel, and the build continues on a skip.
// The builder owns the Skip All latch, so this is only reached while asking.
CBuildConfig::CBuildSkipAction SnapshotListDirErrorPrompt(const wchar_t* dir,
                                                                  DWORD winError,
                                                                  void* context)
{
    std::wstring msg = FormatStrW(LoadStrW(IDS_CANNOTREADDIR),
                                  dir != NULL ? dir : L"",
                                  GetErrorTextOwned(winError).c_str());
    PromptResult res = gPrompter->AskSkipSkipAllCancel(LoadStrW(IDS_ERRORTITLE), msg.c_str());
    if (MainWindow != NULL)
        UpdateWindow(MainWindow->HWindow);

    if (res.type == PromptResult::kCancel)
    {
        // The user already knows why the build stopped — remember that so the
        // caller does not stack a redundant "error building script" box on top.
        if (context != NULL)
            *static_cast<bool*>(context) = true;
        return CBuildConfig::CBuildSkipAction::Cancel;
    }
    if (res.type == PromptResult::kSkipAll)
        return CBuildConfig::CBuildSkipAction::SkipAll;
    return CBuildConfig::CBuildSkipAction::Skip;
}

// A file's or directory's alternate data streams could not be enumerated.
// Restores the legacy CErrorReadingADSDlg (Retry / Ignore / Ignore All /
// Cancel) that went away with the old recursive builder: only Cancel aborted
// the operation, every other answer copied on without the streams. The
// builder owns the Ignore All latch, so this is only reached while asking.
CBuildConfig::CBuildADSProbeErrorAction SnapshotADSProbeErrorPrompt(const wchar_t* sourceName,
                                                                   DWORD winError,
                                                                   void* context)
{
    HWND parent = context != NULL ? *static_cast<HWND*>(context)
                                  : (MainWindow != NULL ? MainWindow->HWindow : NULL);
    const int res = (int)CErrorReadingADSDlg(parent,
                                             sourceName != NULL ? sourceName : L"",
                                             GetErrorTextOwned(winError).c_str(),
                                             LoadStrW(IDS_ERRORREADINGADS))
                        .Execute();
    if (MainWindow != NULL)
        UpdateWindow(MainWindow->HWindow);

    BOOL ignoreAll = FALSE;
    switch (NormalizeADSReadErrorResponse(res, &ignoreAll))
    {
    case IDRETRY:
        return CBuildConfig::CBuildADSProbeErrorAction::Retry;
    case IDCANCEL:
        return CBuildConfig::CBuildADSProbeErrorAction::Cancel;
    default:
        return ignoreAll ? CBuildConfig::CBuildADSProbeErrorAction::IgnoreAll
                         : CBuildConfig::CBuildADSProbeErrorAction::Ignore;
    }
}

// A reparse-point FILE carries size 0 in the directory entry. Resolves the link
// target's real size for the snapshot builder so the free-space check, the
// progress total and the FAT32 4 GB guard see what will actually be written —
// the call legacy made at copy_move.cpp:3574 and that went away with the old
// builder ("the builder no longer needs mid-build link sizing" was wrong for
// reparse FILES; it only ever held for junctions). Owns the Ignore All latch.
struct SnapshotLinkTargetSizeContext
{
    HWND Parent;
    BOOL IgnoreAll;
};

CBuildConfig::CBuildLinkTargetSizeResult SnapshotLinkTargetSizePrompt(const wchar_t* linkPath,
                                                                     unsigned __int64* size,
                                                                     void* context)
{
    SnapshotLinkTargetSizeContext* ctx = static_cast<SnapshotLinkTargetSizeContext*>(context);
    if (ctx == NULL || linkPath == NULL || size == NULL)
        return CBuildConfig::CBuildLinkTargetSizeResult::Ignore;

    CQuadWord tgtSize(0, 0);
    BOOL cancel = FALSE;
    if (GetLinkTgtFileSize(ctx->Parent, linkPath, NULL, &tgtSize, &cancel, &ctx->IgnoreAll))
    {
        *size = tgtSize.Value;
        return CBuildConfig::CBuildLinkTargetSizeResult::Resolved;
    }
    if (MainWindow != NULL)
        UpdateWindow(MainWindow->HWindow);
    return cancel ? CBuildConfig::CBuildLinkTargetSizeResult::Cancel
                  : CBuildConfig::CBuildLinkTargetSizeResult::Ignore;
}

// A file the FAT32 target cannot physically hold (>= 4 GB). Legacy warned before
// starting the copy; without it the operation dies mid-file after moving 4 GB.
CBuildConfig::CBuildSkipAction SnapshotFat32TooBigPrompt(const wchar_t* sourceFile,
                                                         void* context)
{
    std::wstring msg = FormatStrW(LoadStrW(IDS_FILEISTOOBIGFORFAT32),
                                  sourceFile != NULL ? sourceFile : L"");
    PromptResult res = gPrompter->AskSkipSkipAllCancel(LoadStrW(IDS_ERRORTITLE), msg.c_str());
    if (MainWindow != NULL)
        UpdateWindow(MainWindow->HWindow);

    if (res.type == PromptResult::kCancel)
    {
        if (context != NULL)
            *static_cast<bool*>(context) = true;
        return CBuildConfig::CBuildSkipAction::Cancel;
    }
    if (res.type == PromptResult::kSkipAll)
        return CBuildConfig::CBuildSkipAction::SkipAll;
    return CBuildConfig::CBuildSkipAction::Skip;
}

// Makes the wait window's "press the ESC key to cancel" promise real again
// (legacy copy_move.cpp:3043). The builder throttles how often this runs.
bool SnapshotCancelPoll(void* context)
{
    if (!UserWantsToCancelSafeWaitWindow())
        return false;

    MSG msg; // discard the buffered ESC so it cannot leak into the dialog
    while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
        ;
    const bool cancel = gPrompter->AskYesNo(LoadStrW(IDS_QUESTION),
                                            LoadStrW(IDS_CANCELOPERATION))
                            .type == PromptResult::kYes;
    if (MainWindow != NULL)
        UpdateWindow(MainWindow->HWindow);
    if (cancel && context != NULL)
        *static_cast<bool*>(context) = true;
    return cancel;
}

// A reparse-point directory is being copied or moved: the link itself, or what
// it points at? Restores the legacy question (copy_move.cpp:2887). The dialog it
// drives survived the port to the snapshot builder — it was even widened to
// wchar_t — but nothing asked any more, so every junction was cloned as a
// junction. Copied to another volume that leaves a link back to the SOURCE path,
// dangling anywhere else, where the user was expecting the files.
CBuildLinkContentPromptResult SnapshotLinkContentPrompt(const wchar_t* linkPath,
                                                       void* contextRaw)
{
    SnapshotLinkContentContext* ctx = static_cast<SnapshotLinkContentContext*>(contextRaw);
    const std::wstring linkPathW = linkPath != NULL ? linkPath : L"";

    // Describe the link exactly as legacy did: a volume mount point names
    // itself, a junction or symlink names its destination with the parentheses
    // the info-dialog string carries stripped off, and an unresolvable reparse
    // tag says so instead of leaving the field blank.
    std::wstring detailsW;
    std::wstring linkTargetW;
    int repPointType = 0;
    if (GetReparsePointDestinationOwnedW(linkPathW.c_str(), &linkTargetW, &repPointType, FALSE))
    {
        if (repPointType == 1) // MOUNT POINT
            detailsW = LoadStrW(IDS_VOLMOUNTPOINT);
        else
        {
            detailsW = FormatStrW(LoadStrW(repPointType == 2 ? IDS_INFODLGTYPE9   // JUNCTION
                                                             : IDS_INFODLGTYPE10), // SYMLINK
                                  linkTargetW.c_str());
            if (!detailsW.empty() && detailsW.front() == L'(')
                detailsW.erase(0, 1);
            if (!detailsW.empty() && detailsW.back() == L')')
                detailsW.pop_back();
        }
    }
    else
        detailsW = LoadStrW(IDS_UNABLETORESOLVELINK);

    const int res = (int)CConfirmLinkTgtCopyDlg(ctx->Parent, linkPathW.c_str(),
                                                detailsW.c_str())
                        .Execute();
    if (MainWindow != NULL)
        UpdateWindow(MainWindow->HWindow);

    switch (res)
    {
    case IDB_ALL:
        return CBuildLinkContentPromptResult::CopyContentAll;
    case IDYES:
        return CBuildLinkContentPromptResult::CopyContent;
    case IDB_SKIPALL:
        return CBuildLinkContentPromptResult::CloneLinkAll;
    case IDB_SKIP:
        return CBuildLinkContentPromptResult::CloneLink;
    default:
        // The user already knows why the build stopped; keep the generic
        // "error building script" box off the top of it.
        if (ctx->CancelledByUser != nullptr)
            *ctx->CancelledByUser = true;
        return CBuildLinkContentPromptResult::Cancel;
    }
}

CBuildAdsLossPromptResult SnapshotAdsLossPrompt(const wchar_t* name,
                                                 bool isDir,
                                                 void* contextRaw)
{
    SnapshotAdsLossContext* ctx = static_cast<SnapshotAdsLossContext*>(contextRaw);
    int res;
    if (ctx->BsState->ConfirmADSLossAll)
        res = IDYES;
    else if (ctx->BsState->ConfirmADSLossSkipAll)
        res = IDB_SKIP;
    else
    {
        // Same flow as the legacy builder: show the actual stream names; an
        // empty listing is an automatic Yes (nothing user-visible to lose).
        std::wstring nameWStr = name != NULL ? name : L"";
        std::wstring streamsW;
        GetADSStreamsNames(nameWStr, isDir, streamsW);
        if (streamsW.empty())
            res = IDYES;
        else
            res = (int)CConfirmADSLossDlg(ctx->Parent, !isDir,
                                          nameWStr.c_str(), streamsW.c_str(),
                                          ctx->Type == atMove)
                      .Execute();
    }
    switch (res)
    {
    case IDB_ALL:
        ctx->BsState->ConfirmADSLossAll = TRUE; // intentional fallthrough
    case IDYES:
        return CBuildAdsLossPromptResult::Proceed;
    case IDB_SKIPALL:
        ctx->BsState->ConfirmADSLossSkipAll = TRUE; // intentional fallthrough
    case IDB_SKIP:
        return CBuildAdsLossPromptResult::Skip;
    default:
        return CBuildAdsLossPromptResult::Reject; // Cancel: abort the build
    }
}

} // namespace

// External linkage so the private tests exercise the REAL routing gate (same
// rationale as SnapshotSelectionNeedsLegacyADS; declared in fileswnd.h).
// fileswnd.h declared targetPath/mask/sourcePath wide, but this
// definition stayed narrow (targetPath/mask lagging, and sourcePath's own
// AnsiToWide fallback below quietly running on wrong data) - the same
// link-time-masked overload-not-redefinition defect the freefn-width scan
// already caught for AgreeMask/AgreeQSMask. BuildScriptMain's one real call
// site already passes wide data for all three (its own targetPath/mask
// params and sourcePath are owned UTF-16), so the wide declaration
// was always what production needed.
BOOL CanBuildFirstTrancheFromSnapshot(BOOL isDiskPanel, CActionType type,
                                      const wchar_t* targetPath, const wchar_t* mask,
                                      CAttrsData* attrsData,
                                      CChangeCaseData* chCaseData,
                                      BOOL onlySize,
                                      BOOL sourceSupADS,
                                      BOOL targetSupADS,
                                      const wchar_t* sourcePath,
                                      const wchar_t* sourcePathW,
                                      CCriteriaData* filterCriteria,
                                      const CSelectionSnapshot& snapshot)
{
    if (!isDiskPanel || snapshot.Items.empty())
        return FALSE;
    if (onlySize && type != atCountSize)
        return FALSE; // counting is the one legitimate onlySize flow

    // The six capabilities are production now: ChangeAttrs
    // and ChangeCase route to the snapshot builder (attribute compression/
    // encryption changes have no capability and stay out until one exists).
    switch (type)
    {
    case atCopy:
    case atMove:
    case atDelete:
        break;
    case atChangeAttrs:
        if (attrsData == NULL || attrsData->ChangeCompression || attrsData->ChangeEncryption)
            return FALSE;
        break;
    case atChangeCase:
        if (chCaseData == NULL)
            return FALSE;
        break;
    case atConvert:
    case atRecursiveConvert:
        // Convert was builder-supported all along; the gate just
        // never admitted the type. Filters ride the generic FilterPredicate.
        break;
    case atCountSize: // Alt+F10 counting (dialog + column modes)
        break;
    default:
        return FALSE;
    }
    if (attrsData != NULL && type != atChangeAttrs)
        return FALSE;
    if (chCaseData != NULL && type != atChangeCase)
        return FALSE;

    if (filterCriteria != NULL && type != atCopy && type != atMove &&
        type != atConvert && type != atRecursiveConvert)
        return FALSE;

    if ((type == atCopy || type == atMove) &&
        (targetPath == NULL || targetPath[0] == 0))
    {
        return FALSE;
    }

    // Masked delete is absorbed (SnapshotDeleteMaskPredicate);
    // no default-mask requirement remains for atDelete.

    for (const CSnapshotItem& item : snapshot.Items)
    {
        // Reparse dirs are absorbed for delete AND copy/move
        // (copy-the-link; EnableReparseDelete/EnableReparseCopyMove).
        // A dir under NON-recursive Convert rejects (mirrors the
        // builder validate rule so the build never aborts the operation).
        if (item.IsDir && type == atConvert)
            return FALSE;
    }

    // sourcePath is now the same wide data as sourcePathW's own source (both
    // ultimately come from the panel's wide path) - no conversion needed for
    // the fallback anymore, just prefer sourcePathW when it is populated.
    const wchar_t* sourcePathForADS = sourcePathW != NULL && sourcePathW[0] != L'\0' ? sourcePathW : sourcePath;
    if (SnapshotSelectionNeedsLegacyADS(type, sourceSupADS, targetSupADS, snapshot,
                                        sourcePathForADS))
    {
        return FALSE;
    }

    return TRUE;
}

namespace
{

bool SplitFullPathW(const wchar_t* fullPath, std::wstring& dir, std::wstring& name)
{
    if (fullPath == NULL || fullPath[0] == L'\0')
        return false;
    const wchar_t* slash = wcsrchr(fullPath, L'\\');
    if (slash == NULL || slash == fullPath || slash[1] == L'\0')
        return false;
    dir.assign(fullPath, slash - fullPath);
    name.assign(slash + 1);
    return !dir.empty() && !name.empty();
}

bool GenerateCopyOfTargetNameW(const std::wstring& directoryWithBackslash,
                               const std::wstring& sourceNameW,
                               bool isDir,
                               std::vector<std::wstring>& reservedNames,
                               std::wstring& targetNameW)
{
    // LoadStrW is safe here: the earlier "e2e test host catches a real bug"
    // read was wrong. texts.rc2 (where IDS_NEWNAME_COPY lives) is compiled only into the
    // language-pack module (lang/lang.rc2 -> texts.rc2), never into sally.rc, so a real
    // running Sally always resolves this through a genuinely loaded .slg - the e2e test
    // host's HLanguage == HInstance has no langpack loaded and can't resolve ANY IDS_*
    // string via either LoadStr or LoadStrW (confirmed empirically: both return their
    // "ERROR LOADING [WIDE ]STRING" fallback there). The two affected tests
    // (gtest_f5_unicode_copy_e2e) no longer depend on the literal token text.
    const std::wstring copyTokenW = LoadStrW(IDS_NEWNAME_COPY);
    if (!isDir)
    {
        if (!sally::unicode::TryGenerateUniqueCopyName(directoryWithBackslash, sourceNameW,
                                                       copyTokenW, reservedNames,
                                                       targetNameW,
                                                       [](const wchar_t* path)
                                                       { return gFileSystem->GetFileAttributes(path); }))
        {
            return false;
        }
        reservedNames.push_back(targetNameW);
        return true;
    }

    if (directoryWithBackslash.empty() || sourceNameW.empty() || copyTokenW.empty())
        return false;
    if (directoryWithBackslash.back() != L'\\' && directoryWithBackslash.back() != L'/')
        return false;

    DWORD dirAttrs = gFileSystem->GetFileAttributes(directoryWithBackslash.c_str());
    if (dirAttrs == INVALID_FILE_ATTRIBUTES || (dirAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return false;

    for (int copyIndex = 1; copyIndex < 10000; ++copyIndex)
    {
        std::wstring candidate = sourceNameW;
        candidate += L" - ";
        candidate += copyTokenW;
        if (copyIndex > 1)
        {
            candidate += L" (";
            candidate += std::to_wstring(copyIndex);
            candidate += L")";
        }

        if (sally::unicode::ContainsNameIgnoreCase(reservedNames, candidate))
            continue;
        if (!sally::unicode::IsOccupiedPathW(
                directoryWithBackslash, candidate,
                [](const wchar_t* path) { return gFileSystem->GetFileAttributes(path); }))
        {
            targetNameW = candidate;
            reservedNames.push_back(targetNameW);
            return true;
        }
    }
    return false;
}

} // namespace

BOOL CanBuildMain2FromSnapshot(BOOL isDiskPanel,
                               BOOL copy,
                               const wchar_t* targetDir,
                               const std::wstring& targetPathWithSlash,
                               BOOL targetSupADS,
                               BOOL targetIsFAT32,
                               CCopyMoveData* data,
                               CSelectionSnapshot& snapshot,
                               CBuildConfig& config,
                               COperations* script)
{
    if (!isDiskPanel || targetDir == NULL || targetDir[0] == L'\0' ||
        targetPathWithSlash.empty() ||
        data == NULL || data->Count < 1 ||
        (data->MakeCopyOfName && !copy))
    {
        return FALSE;
    }

    std::wstring sourceDirW;
    std::vector<std::wstring> reservedNames;

    snapshot.Action = copy ? EActionType::Copy : EActionType::Move;
    snapshot.TargetPathW = targetDir;
    snapshot.Mask = L"*.*";
    snapshot.CopySecurity = script->CopySecurity != FALSE;
    snapshot.CopyAttrs = script->CopyAttrs != FALSE;
    snapshot.PreserveDirTime = script->PreserveDirTime != FALSE;
    snapshot.StartOnIdle = script->StartOnIdle != FALSE;

    // Main2 absorption: the three legacy specials route here now.
    // 1. Multi-directory drops - per-item SourceParentW (task-10 model).
    // 2. MapName rename-maps (shell file-group descriptors) - the explicit
    //    target leaf rides item.TargetNameW/HasTargetName like copy-of names.
    // 3. fWide=FALSE ANSI drops - decoded at the bounded clipboard adapter before records exist.
    for (int i = 0; i < data->Count; ++i)
    {
        CCopyMoveRecord* record = data->At(i);
        if (record == NULL || !record->IsValid())
            return FALSE;

        std::wstring itemSourceDirW;
        std::wstring sourceNameW;
        if (!SplitFullPathW(record->FileName.c_str(), itemSourceDirW, sourceNameW))
        {
            return FALSE;
        }

        if (i == 0)
        {
            sourceDirW = itemSourceDirW;
            snapshot.SourcePathW = sourceDirW;
        }

        FileInfo fileInfo = {};
        if (!gFileSystem->GetFileInfo(record->FileName.c_str(), fileInfo).success)
            return FALSE;
        DWORD attrs = fileInfo.attributes;
        const bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

        const BOOL sourceAndTargetSamePath = IsTheSamePath(itemSourceDirW.c_str(), targetPathWithSlash.c_str());
        // Legacy-loop semantics: "make copy of" names apply only when the item
        // pastes back into its own directory; a different target is a plain
        // copy. A same-path paste WITHOUT copy-of or a rename-map is a no-op
        // shape - refuse it.
        const bool wantCopyOfName = data->MakeCopyOfName && sourceAndTargetSamePath;
        if (!data->MakeCopyOfName && sourceAndTargetSamePath && !record->MapName.has_value())
            return FALSE;

        std::wstring targetNameW;
        if (wantCopyOfName)
        {
            if (!GenerateCopyOfTargetNameW(targetPathWithSlash, sourceNameW,
                                           isDir, reservedNames, targetNameW))
            {
                return FALSE;
            }
        }

        CSnapshotItem item = {};
        item.NameW = sourceNameW;
        if (_wcsicmp(sourceDirW.c_str(), itemSourceDirW.c_str()) != 0)
        {
            item.SourceParentW = itemSourceDirW;
        }
        if (wantCopyOfName)
        {
            item.TargetNameW = targetNameW;
            item.HasTargetName = true;
        }
        else if (record->MapName.has_value() && !record->MapName->empty())
        {
            // Legacy contract: MapName is the explicit target LEAF name
            // (BuildScriptDir/File took it as their targetName parameter).
            item.TargetNameW = *record->MapName;
            item.HasTargetName = true;
        }
        item.IsDir = isDir;
        item.Size = isDir ? 0 : fileInfo.size;
        item.Attr = attrs;
        item.LastWrite = fileInfo.lastWriteTime;
        snapshot.Items.push_back(item);
    }

    if (sourceDirW.empty() || snapshot.Items.empty())
        return FALSE;

    BOOL sourceSupADS = IsPathOnVolumeSupADSW(sourceDirW.c_str(), NULL);

    const CActionType actionType = copy ? atCopy : atMove;
    if (SnapshotSelectionNeedsLegacyADS(actionType, sourceSupADS, targetSupADS, snapshot,
                                        sourceDirW.c_str()))
    {
        snapshot.Items.clear();
        return FALSE;
    }

    config.SourceSupportsADS = sourceSupADS;
    config.TargetSupportsADS = targetSupADS;
    config.TargetIsFAT32 = targetIsFAT32;
    CTargetPathState targetPathState = GetTargetPathState(tpsUnknown, targetDir);
    config.TargetPathIsEncrypted = targetPathState == tpsEncryptedExisting ||
                                   targetPathState == tpsEncryptedNotExisting;
    config.EnableADS = sourceSupADS && targetSupADS;
    config.ADSProbe = BuildScriptLegacyADSProbe;
    config.ADSProbeErrorCallback = SnapshotADSProbeErrorPrompt;
    config.SourcePathIsNetwork = script != NULL ? script->SourcePathIsNetwork : FALSE;
    static SnapshotLinkTargetSizeContext mainLinkSizeCtx;
    mainLinkSizeCtx = {MainWindow != NULL ? MainWindow->HWindow : NULL, FALSE};
    config.LinkTargetSizeCallback = SnapshotLinkTargetSizePrompt;
    config.LinkTargetSizeContext = &mainLinkSizeCtx;
    config.EnableExplicitTargetNames = data->MakeCopyOfName != FALSE;
    config.EnableRecursiveDirectories = TRUE;
    config.ClearReadOnly = script->ClearReadonlyMask == ~(FILE_ATTRIBUTE_READONLY);
    return TRUE;
}

// The former SALLY_PRIVATE_TESTS sally::test forwarders were promoted:
// ShouldReportADSProbeError / NormalizeADSReadErrorResponse live in
// common/AdsPolicy.h; SnapshotSelectionNeedsLegacyADS is a public predicate
// declared in fileswnd.h.

// Transient state for BuildScriptMain/Dir/File. The legacy builder is recursive,
// so route all existing bsState references through a per-call active state.
static thread_local CBuildScriptState* ActiveBuildScriptState = nullptr;

static CBuildScriptState& GetActiveBuildScriptState()
{
    if (ActiveBuildScriptState == nullptr)
    {
        TRACE_E("GetActiveBuildScriptState() used without CScopedBuildScriptState");
        _ASSERTE(ActiveBuildScriptState != nullptr);
    }
    return *ActiveBuildScriptState;
}

class CScopedBuildScriptState
{
public:
    CScopedBuildScriptState()
        : Previous(ActiveBuildScriptState)
    {
        State.Reset();
        ActiveBuildScriptState = &State;
    }

    ~CScopedBuildScriptState()
    {
        ActiveBuildScriptState = Previous;
    }

private:
    CBuildScriptState State;
    CBuildScriptState* Previous;
};

static void SetScriptWorkPath1(COperations* script, const wchar_t* pathW, BOOL inclSubDirs)
{
    if (script == NULL || pathW == NULL || pathW[0] == L'\0')
        return;
    script->SetWorkPath1W(pathW, inclSubDirs);
}

static void SetScriptWorkPath2(COperations* script, const wchar_t* pathW, BOOL inclSubDirs)
{
    if (script == NULL || pathW == NULL || pathW[0] == L'\0')
        return;
    script->SetWorkPath2W(pathW, inclSubDirs);
}

// sourcePathW/targetPathW are the callers' already-resolved effective
// wide paths - both BuildScriptMain call sites now always have one, so the old
// ansi-mirror-with-wide-preference fallback (EffectiveWorkPathW) is dead weight.
static void StampBuildScriptWorkPaths(COperations* script, CActionType type,
                                      const wchar_t* sourcePathW,
                                      const wchar_t* targetPathW)
{
    if (type != atCopy && type != atMove && type != atDelete)
        return;

    SetScriptWorkPath1(script, sourcePathW, TRUE);
    if (type == atCopy || type == atMove)
        SetScriptWorkPath2(script, targetPathW, TRUE);
}

//
// ****************************************************************************
// CFilesWindow
//


void CFilesWindow::Activate(BOOL shares)
{
    CALL_STACK_MESSAGE_NONE
    //  TRACE_I("CFilesWindow::Activate");
    LastInactiveRefreshStart = LastInactiveRefreshEnd; // activation cancels information about the last refresh in the inactive window
    BOOL needToRefreshIcons = InactWinOptimizedReading;
    if (Is(ptDisk) || Is(ptZIPArchive)) // disks and archives
    {
        if (!SkipOneActivateRefresh && (!GetNetworkDrive() || !Configuration.DrvSpecRemoteDoNotRefreshOnAct) ||
            InactiveRefreshTimerSet) // delayed refresh in an inactive window must be performed immediately upon activation
        {
            DWORD checkPathRet;
            if ((checkPathRet = CheckPath(FALSE)) != ERROR_SUCCESS)
            {
                if (checkPathRet == ERROR_USER_TERMINATED) // user pressed ESC -> switch to fixed drive
                {
                    if (MainWindow->LeftPanel == this)
                    {
                        if (!ChangeLeftPanelToFixedWhenIdleInProgress)
                            ChangeLeftPanelToFixedWhenIdle = TRUE;
                    }
                    else
                    {
                        if (!ChangeRightPanelToFixedWhenIdleInProgress)
                            ChangeRightPanelToFixedWhenIdle = TRUE;
                    }
                }
                else // another path error, schedule a refresh
                {
                    HANDLES(EnterCriticalSection(&TimeCounterSection));
                    int t1 = MyTimeCounter++;
                    HANDLES(LeaveCriticalSection(&TimeCounterSection));
                    PostMessage(HWindow, WM_USER_REFRESH_DIR, 0, t1);
                }
                needToRefreshIcons = FALSE;
            }
            else // path appears to be OK
            {
                if (!AutomaticRefresh && !GetNetworkDrive() ||           // manual disk refresh (excluding network drives)
                    GetNetworkDrive() &&                                 // for network drives, we refresh on every
                        !Configuration.DrvSpecRemoteDoNotRefreshOnAct || // activation unless explicitly disabled (used to handle Samba behavior)
                    shares && !GetNetworkDrive() ||                      // restore shares (not relevant for network drives)
                    InactiveRefreshTimerSet)                             // delayed refresh in inactive window must be executed immediately upon activation
                {
                    if (InactiveRefreshTimerSet)
                    {
                        //            TRACE_I("Refreshing on window activation (refresh in inactive window was delayed)");
                        KillTimer(HWindow, IDT_INACTIVEREFRESH);
                        InactiveRefreshTimerSet = FALSE;
                    }
                    HANDLES(EnterCriticalSection(&TimeCounterSection));
                    int t1 = MyTimeCounter++;
                    HANDLES(LeaveCriticalSection(&TimeCounterSection));
                    PostMessage(HWindow, WM_USER_REFRESH_DIR_EX, FALSE, t1); // we know this is probably an unnecessary refresh
                    needToRefreshIcons = FALSE;
                }
                else // on automatically refreshed drives update at least disk-free-space
                {
                    RefreshDiskFreeSpace(FALSE, TRUE);
                }
            }
        }
    }
    else
    {
        if (Is(ptPluginFS)) // plug-in FS: send FSE_ACTIVATEREFRESH so the plug-in can refresh itself
        {
            if (!SkipOneActivateRefresh)
                PostMessage(HWindow, WM_USER_REFRESH_PLUGINFS, 0, 0);
        }
    }
    if (needToRefreshIcons)
    {
        //    TRACE_I("Refreshing icons/thumbnails/icon-overlays (we have read only visible ones)");
        SleepIconCacheThread();
        InactWinOptimizedReading = FALSE;
        WakeupIconCacheThread();
    }
}

namespace
{

bool FileStartsWithUtf8Bom(HANDLE hFile)
{
    uint64_t original = 0;
    if (!gFileSystem->SeekHandle(hFile, 0, FILE_CURRENT, &original).success)
        return false;
    gFileSystem->SeekHandle(hFile, 0, FILE_BEGIN, NULL);
    BYTE bom[3] = {};
    DWORD read = 0;
    bool hasBom = gFileSystem->ReadFromHandle(hFile, bom, sizeof(bom), &read).success &&
                  read == sizeof(bom) && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF;
    gFileSystem->SeekHandle(hFile, (int64_t)original, FILE_BEGIN, NULL);
    return hasBom;
}

bool WriteAll(HANDLE hFile, const void* data, size_t len)
{
    const BYTE* next = static_cast<const BYTE*>(data);
    while (len != 0)
    {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(len, MAXDWORD));
        DWORD written = 0;
        FileResult result = gFileSystem->WriteToHandle(hFile, next, chunk, &written);
        if (!result.success)
            SetLastError(result.errorCode);
        if (!result.success || written != chunk)
            return false;
        next += written;
        len -= written;
    }
    return true;
}

} // namespace

BOOL CFilesWindow::MakeFileList(HANDLE hFile)
{
    CALL_STACK_MESSAGE_NONE
    BOOL ret = TRUE;

    if (FilesActionInProgress)
        return FALSE;

    FilesActionInProgress = TRUE;

    int focusIndex = 0;
    int alloc;
    int count = GetSelCount();
    if (count == 0)
    {
        focusIndex = GetCaretIndex();
        alloc = 1;
    }
    else
        alloc = count;

    std::unique_ptr<int[]> indexes = std::make_unique<int[]>(alloc); // RAII: auto-deleted when scope exits
    if (count > 0)
        GetSelItems(count, indexes.get());
    else
        indexes[0] = focusIndex;

        int files = 0;
        int dirs = 0;
        CFileData* f;

        // in the first phase, compute maximum width of variables (if some use $(name:max))
        int maxSizes[100];
        int maxSizesCount = 100;
        ZeroMemory(maxSizes, sizeof(maxSizes));
        int i;
        for (i = 0; i < alloc; i++)
        {
            if (indexes[i] >= 0 && indexes[i] < Dirs->Count + Files->Count)
            {
                f = (indexes[i] < Dirs->Count) ? &Dirs->At(indexes[i]) : &Files->At(indexes[i] - Dirs->Count);

                if (!ExpandMakeFileListW(HWindow, Configuration.FileListHistory[0], &PluginData, f,
                                         indexes[i] < Dirs->Count, NULL, TRUE, maxSizes, maxSizesCount,
                                         ValidFileData, GetPathW(), i != 0))
                {
                    FilesActionInProgress = FALSE;
                    return FALSE;
                }
            }
        }
        std::wstring output;
        sally::file_list::TextEncoding outputEncoding = sally::file_list::TextEncoding::LegacyAcp;
        std::string encodedOutput;

        // in the second phase, apply these widths
        for (i = 0; i < alloc; i++)
        {
            if (indexes[i] >= 0 && indexes[i] < Dirs->Count + Files->Count)
            {
                f = (indexes[i] < Dirs->Count) ? &Dirs->At(indexes[i]) : &Files->At(indexes[i] - Dirs->Count);

                std::wstring buff;
                if (ExpandMakeFileListW(HWindow, Configuration.FileListHistory[0], &PluginData, f,
                                        indexes[i] < Dirs->Count, &buff, FALSE, maxSizes, maxSizesCount,
                                        ValidFileData, GetPathW(), TRUE))
                {
                    output += buff;
                }
                else
                {
                    FilesActionInProgress = FALSE;
                    return FALSE;
                }
            }
        }
        const Win32TextConversionResult encodeResult =
            sally::file_list::EncodeText(output, encodedOutput, outputEncoding);
        if (!encodeResult.Succeeded())
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE),
                                 GetErrorTextOwned(encodeResult.Win32Error).c_str());
            FilesActionInProgress = FALSE;
            return FALSE;
        }
        uint64_t pos = 0;
        gFileSystem->SeekHandle(hFile, 0, FILE_CURRENT, &pos);
        if (outputEncoding == sally::file_list::TextEncoding::LegacyAcp)
        {
            if (!WriteAll(hFile, encodedOutput.data(), encodedOutput.length()))
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), GetErrorTextOwned(GetLastError()).c_str());
                FilesActionInProgress = FALSE;
                return FALSE;
            }
        }
        else
        {
            bool hasUtf8Bom = pos > 0 && FileStartsWithUtf8Bom(hFile);
            if (pos > 0 && !hasUtf8Bom)
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), L"Cannot append Unicode file-list output to a non-Unicode file.");
                FilesActionInProgress = FALSE;
                return FALSE;
            }
            const BYTE bom[] = {0xEF, 0xBB, 0xBF};
            if ((pos == 0 && !WriteAll(hFile, bom, sizeof(bom))) ||
                !WriteAll(hFile, encodedOutput.data(), encodedOutput.length()))
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), GetErrorTextOwned(GetLastError()).c_str());
                FilesActionInProgress = FALSE;
                return FALSE;
            }
        }
    // RAII: indexes auto-deleted when scope exits
    FilesActionInProgress = FALSE;
    return TRUE;
}

static BOOL GetFileSystemNameForPathW(const wchar_t* path, DWORD* maximumComponentLength,
                                      DWORD* fileSystemFlags, std::wstring& fileSystemName)
{
    return MyGetVolumeInformationW(path, NULL, NULL, NULL, NULL, NULL,
                                   maximumComponentLength, fileSystemFlags,
                                   &fileSystemName);
}

// Wide target owners use the real implementation directly.
// Drive letters are ASCII by definition, so only that root byte is folded.
static DWORD GetPathFlagsForCopyOpW(const wchar_t* path, DWORD netFlag, DWORD fixedFlag)
{
    if (path == NULL || path[0] == L'\0')
        return 0;
    if (IsUNCPathW(path))
        return netFlag;

    const UINT drvType = MyGetDriveTypeW(path);
    if (drvType == DRIVE_REMOTE)
        return netFlag;
    if (drvType == DRIVE_FIXED || drvType == DRIVE_RAMDISK || drvType == DRIVE_CDROM)
        return fixedFlag;

    wchar_t drive = path[0];
    if (drive >= L'a' && drive <= L'z')
        drive -= L'a' - L'A';
    if (drvType == DRIVE_REMOVABLE && drive >= L'A' && drive <= L'Z' && path[1] == L':' &&
        GetDriveFormFactor((int)(drive - L'A' + 1)) == 0 /* not a floppy */)
    {
        return fixedFlag; // removable but not a floppy, e.g. USB stick or a camera via USB (e.g. FZ45) - we treat them as fixed, they're fast enough
    }
    return 0;
}

// fileswnd.h:1454 has declared all four parameters const wchar_t*; only this
// definition lagged. CALL_STACK_MESSAGE stays narrow and takes %ls for wide arguments.
BOOL CFilesWindow::MoveFiles(const wchar_t* source, const wchar_t* target, const wchar_t* remapNameFrom,
                             const wchar_t* remapNameTo)
{
    CALL_STACK_MESSAGE5("CFilesWindow::MoveFiles(%ls, %ls, %ls, %ls)",
                        source, target, remapNameFrom, remapNameTo);
    if (!FilesActionInProgress)
    {
        if (CheckPath(TRUE, source) != ERROR_SUCCESS)
            return FALSE;

        FilesActionInProgress = TRUE;

        gEnvironment->SetCurrentDirectory(source); // for a faster move (the system prefers it)

        CScopedBuildScriptState scopedBuildScriptState;
        CBuildScriptState& bsState = GetActiveBuildScriptState();

        //---  create the script object
        COperations* script = new COperations(100, 50, NULL, NULL, NULL);
        if (script == NULL)
        {
            TRACE_E(LOW_MEMORY);
            FilesActionInProgress = FALSE;
            SetCurrentDirectoryToSystem();
            return FALSE;
        }
        // These two lengths are the ones RemapNames sizes its copies with.
        // They were computed with strlen() on const wchar_t* - a unit bug feeding a unit bug.
        script->RemapNameFrom = remapNameFrom;
        script->RemapNameFromLen = (int)wcslen(remapNameFrom);
        script->RemapNameTo = remapNameTo;
        script->RemapNameToLen = (int)wcslen(remapNameTo);

        // MoveFiles already receives exact UTF-16 paths. Keep root/volume
        // classification on those values instead of routing them into the legacy ANSI helpers.
        BOOL sameRootPath = HasTheSameRootPath(source, target);
        script->SameRootButDiffVolume = sameRootPath && !HasTheSameRootPathAndVolume(source, target);
        script->ShowStatus = !sameRootPath || script->SameRootButDiffVolume;
        script->IsCopyOperation = FALSE;
        // script->IsCopyOrMoveOperation = TRUE;   // commented out because we don't want to add this Move to the Copy/Move operation queue

        BOOL fastDirectoryMove = TRUE;          // Configuration.FastDirectoryMove;
        if (fastDirectoryMove &&                // fast-dir-move is not globally disabled
            sameRootPath)                       // + within the same drive
        {
            UINT sourceType = DRIVE_REMOTE;
            if (source[0] != L'\\') // not a UNC path (that is always "remote")
            {
                wchar_t root[4] = L" :\\";
                root[0] = source[0];
                sourceType = GetDriveTypeW(root);
            }

            if (sourceType == DRIVE_REMOTE) // network drive
            {                               // detect Novell disks - fast-directory-move doesn't work on them
                if (IsNOVELLDriveW(source))
                    fastDirectoryMove = Configuration.NetwareFastDirMove;
            }
        }

        //---  initialize build interruption test
        bsState.LastTickCount = GetTickCount();

        //---  snapshot build: the last BuildScriptDir/File consumer
        //     (archive unpack temp-move with remap) rides the single builder.
        std::wstring sourceW = source;
        while (!sourceW.empty() && sourceW.back() == L'\\')
            sourceW.pop_back();
        std::wstring targetW = target;
        while (!targetW.empty() && targetW.back() == L'\\')
            targetW.pop_back();

        {
            CreateSafeWaitWindow(LoadStrW(IDS_ANALYSINGDIRTREEESC), NULL, 1000, TRUE, MainWindow->HWindow);
            HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
            GetAsyncKeyState(VK_ESCAPE); // initialize GetAsyncKeyState - see help

            // sourceW (already trimmed above) is the authoritative value here -
            // narrowing it via WideToAnsi just to feed the narrow overload was a pointless
            // round trip now that IsPathOnVolumeSupADSW exists.
            BOOL sourceSupADS = IsPathOnVolumeSupADSW(sourceW.c_str(), NULL);
            BOOL targetIsFAT32;
            BOOL targetSupADS = IsPathOnVolumeSupADSW(target, &targetIsFAT32);
            CTargetPathState targetPathState = GetTargetPathState(tpsUnknown, target);

            CSelectionSnapshot snapshot;
            snapshot.Action = EActionType::Move;
            snapshot.SourcePathW = sourceW;
            snapshot.TargetPathW = targetW;
            snapshot.Mask = L"*.*";

            BOOL enumOK = TRUE;
            HENUM topEnum = gFileEnumerator->StartEnum((sourceW + L"\\*").c_str());
            if (topEnum == INVALID_HENUM)
                enumOK = FALSE;
            else
            {
                FileEnumEntry entry;
                EnumResult er;
                while ((er = gFileEnumerator->NextFile(topEnum, entry)).success && !er.noMoreFiles)
                {
                    if (entry.name == L"." || entry.name == L"..")
                        continue;
                    CSnapshotItem item = {};
                    item.NameW = entry.name;
                    item.IsDir = (entry.attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    item.Size = item.IsDir ? 0 : entry.size;
                    item.Attr = entry.attributes;
                    item.LastWrite = entry.lastWriteTime;
                    snapshot.Items.push_back(item);
                }
                if (!er.success)
                    enumOK = FALSE;
                gFileEnumerator->EndEnum(topEnum);
            }

            BOOL scriptOK = FALSE;
            if (enumOK)
            {
                CBuildConfig config;
                config.EnableRecursiveDirectories = TRUE;
                config.EnableFastDirMove = fastDirectoryMove;
                config.EnableReparseCopyMove = TRUE;
                config.SourceSupportsADS = sourceSupADS;
                config.TargetSupportsADS = targetSupADS;
                config.TargetIsFAT32 = targetIsFAT32;
                config.TargetPathIsEncrypted = targetPathState == tpsEncryptedExisting ||
                                               targetPathState == tpsEncryptedNotExisting;
                config.EnableADS = sourceSupADS && targetSupADS;
                config.ADSProbe = BuildScriptLegacyADSProbe;
                config.ADSProbeErrorCallback = SnapshotADSProbeErrorPrompt;
                config.ADSProbeErrorContext = &HWindow;
                static SnapshotLinkTargetSizeContext linkSizeCtx;
                linkSizeCtx = {HWindow, FALSE};
                config.LinkTargetSizeCallback = SnapshotLinkTargetSizePrompt;
                config.LinkTargetSizeContext = &linkSizeCtx;
                config.SourcePathIsNetwork = script->SourcePathIsNetwork;
                config.AdsLossPromptCallback = SnapshotAdsLossPrompt;
                static SnapshotAdsLossContext adsCtx;
                adsCtx = {atMove, &bsState, HWindow};
                config.AdsLossPromptContext = &adsCtx;

                scriptOK = BuildScriptFromSnapshot(snapshot, config, bsState, script);
            }
            SetCursor(oldCur);
            DestroySafeWaitWindow();
            // script built, let it execute
            if (script->Count != 0)
            {
                CProgressDialog dlg(HWindow, script, LoadStrW(IDS_UNPACKTMPMOVE), NULL, NULL, FALSE, NULL);
                int res = 0;
                if (!scriptOK || (res = (int)dlg.Execute()) == IDABORT || res == 0 || res == -1)
                {
                    UpdateWindow(MainWindow->HWindow);
                    if (!script->IsGood())
                        script->ResetState();
                    FreeScript(script);
                    FilesActionInProgress = FALSE;
                    SetCurrentDirectoryToSystem();
                    return FALSE;
                }
                else
                    UpdateWindow(MainWindow->HWindow);
            }
            else
            {
                FreeScript(script);
                UpdateWindow(MainWindow->HWindow);
            }
            FilesActionInProgress = FALSE;
            SetCurrentDirectoryToSystem();
            return TRUE;
        }
    }
    return FALSE;
}

BOOL ContainsString(TIndirectArray<wchar_t>* usedNames, const wchar_t* name, int* index)
{
    CALL_STACK_MESSAGE_NONE
    if (usedNames != NULL)
    {
        if (usedNames->Count == 0)
        {
            if (index != NULL)
                *index = 0;
            return FALSE;
        }

        int l = 0, r = usedNames->Count - 1, m;
        while (1)
        {
            m = (l + r) / 2;
            wchar_t* hw = usedNames->At(m);
            int res = StrICmpW(hw, name);
            if (res == 0) // found
            {
                if (index != NULL)
                    *index = m;
                return TRUE;
            }
            else
            {
                if (res > 0)
                {
                    if (l == r || l > m - 1) // not found
                    {
                        if (index != NULL)
                            *index = m; // should be at this position
                        return FALSE;
                    }
                    r = m - 1;
                }
                else
                {
                    if (l == r) // not found
                    {
                        if (index != NULL)
                            *index = m + 1; // should be right after this position
                        return FALSE;
                    }
                    l = m + 1;
                }
            }
        }
    }
    return FALSE;
}

BOOL CFilesWindow::BuildScriptMain2(COperations* script, BOOL copy, const wchar_t* targetDir,
                                    CCopyMoveData* data)
{
    CALL_STACK_MESSAGE3("CFilesWindow::BuildScriptMain2(, %d, %ls, )", copy, targetDir);
    if (!script->IsGood())
        return FALSE;
    script->CompressedSize = CQuadWord(0, 0);
    script->ClearReadonlyMask = 0xFFFFFFFF;
    script->TotalSize = CQuadWord(0, 0);
    script->OccupiedSpace = CQuadWord(0, 0);
    script->TotalFileSize = CQuadWord(0, 0);

    CScopedBuildScriptState scopedBuildScriptState;

    std::wstring fsName;
    DWORD dummy, flags;

    BOOL fastDirectoryMove = TRUE; // Configuration.FastDirectoryMove;
    if (data->Count > 0)
    {
        const wchar_t* name = data->At(0)->FileName.c_str();
        UINT sourceType = DRIVE_REMOTE;
        if (name != NULL &&
            ((name[0] >= L'a' && name[0] <= L'z') || (name[0] >= L'A' && name[0] <= L'Z')) &&
            name[1] == L':') // not a UNC path (UNC paths are always "remote")
        {
            sourceType = MyGetDriveTypeW(name);
        }
        if (name != NULL)
        {
            if (sourceType == DRIVE_REMOTE || sourceType == DRIVE_REMOVABLE)
                gEnvironment->SetCurrentDirectory(GetRootPath(name).c_str());
            else
                gEnvironment->SetCurrentDirectory(targetDir);

            if (fastDirectoryMove &&                    // fast-dir-move is not globally disabled
                !copy && sourceType == DRIVE_REMOTE && // + move operation + network disk
                HasTheSameRootPath(name, targetDir))  // + within one drive
            {                                          // detect Novell disks - fast-directory-move doesn't work on them
                if (IsNOVELLDriveW(name))
                    fastDirectoryMove = Configuration.NetwareFastDirMove;
            }

            if (sourceType == DRIVE_REMOVABLE)
                script->RemovableSrcDisk = TRUE;

            if (Configuration.ClearReadOnly)
            {
                if (sourceType == DRIVE_REMOTE)
                {
                    if (GetFileSystemNameForPathW(name, &dummy, &flags, fsName) &&
                        StrICmpW(fsName.c_str(), L"CDFS") == 0)
                    {
                        script->ClearReadonlyMask = ~(FILE_ATTRIBUTE_READONLY);
                    }
                }
                else
                {
                    if (sourceType == DRIVE_CDROM)
                        script->ClearReadonlyMask = ~(FILE_ATTRIBUTE_READONLY);
                }
            }
        }
    }

    // check if the target is a removable medium (floppy, ZIP) -> larger buffer is used for speed
    if (((targetDir[0] >= L'a' && targetDir[0] <= L'z') ||
         (targetDir[0] >= L'A' && targetDir[0] <= L'Z')) &&
        targetDir[1] == L':')
    {
        if (MyGetDriveTypeW(targetDir) == DRIVE_REMOVABLE)
            script->RemovableTgtDisk = TRUE;
    }

    std::wstring targetPathWithSlash = targetDir;
    SalPathAddBackslashW(targetPathWithSlash);
    BOOL targetIsFAT32 /*, targetSupEFS*/;
    BOOL targetSupADS = IsPathOnVolumeSupADSW(targetPathWithSlash.c_str(), &targetIsFAT32);
    script->TargetPathSupADS = targetSupADS;
    //  script->TargetPathSupEFS = targetSupEFS;
    DWORD d1, d2, d3, d4;
    if (MyGetDiskFreeSpaceW(targetPathWithSlash.c_str(), &d1, &d2, &d3, &d4))
    {
        script->BytesPerCluster = d1 * d2;
        // W2K and later: the product d1 * d2 * d3 did not work on DFS trees, reported by Ludek.Vydra@k2atmitec.cz
        script->FreeSpace = MyGetDiskFreeSpaceW(targetPathWithSlash.c_str());
    }

    CSelectionSnapshot snapshot;
    CBuildConfig buildConfig;
    if (CanBuildMain2FromSnapshot(Is(ptDisk), copy, targetDir,
                                  targetPathWithSlash,
                                  targetSupADS, targetIsFAT32,
                                  data, snapshot, buildConfig, script))
    {
        const BOOL built = BuildScriptFromSnapshot(snapshot, buildConfig,
                                                  GetActiveBuildScriptState(), script);
        SetCurrentDirectoryToSystem();
        return built;
    }

    // THE MAIN2 FLIP: no legacy fallback. Every drop shape the
    // legacy loop served (multi-directory sources, MapName rename-maps,
    // ANSI-only records, copy-of names, ADS-bearing trees) routes through the
    // snapshot gate above. A rejection reaching this point is a genuinely
    // unsupported form; refuse honestly instead of narrowing through CP_ACP.
    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_ERRORBUILDINGSCRIPT));
    SetCurrentDirectoryToSystem();
    return FALSE;
}

BOOL CFilesWindow::DropCopyMove(BOOL copy, const wchar_t* targetPath, CCopyMoveData* data)
{
    CALL_STACK_MESSAGE3("CFilesWindow::DropCopyMove(%d, %ls, )", copy, targetPath);
    BOOL started = FALSE;
    if (!FilesActionInProgress)
    {
        FilesActionInProgress = TRUE;
        SetForegroundWindow(MainWindow->HWindow); // must activate immediately after the drop
        BeginStopRefresh();                       // otherwise WM_ACTIVATEAPP arrives but won't activate...
        COperations* script = new COperations(100, 50, NULL, NULL, NULL);
        if (script == NULL)
            TRACE_E(LOW_MEMORY);
        else
        {
            if (!copy && data->Count > 0)
            {
                const CCopyMoveRecord* record = data->At(0);
                std::wstring sourceDirW = record != NULL && record->IsValid()
                                              ? record->FileName
                                              : L"";
                CutDirectoryW(sourceDirW);
                BOOL sameRootPath = HasTheSameRootPath(sourceDirW.c_str(), targetPath);
                script->SameRootButDiffVolume = sameRootPath &&
                                                 !HasTheSameRootPathAndVolume(sourceDirW.c_str(), targetPath);
                script->ShowStatus = !sameRootPath || script->SameRootButDiffVolume;
            }
            if (copy)
                script->ShowStatus = TRUE;
            script->IsCopyOperation = copy;
            script->IsCopyOrMoveOperation = TRUE;

            const wchar_t* captionW = copy ? LoadStrW(IDS_COPY) : LoadStrW(IDS_MOVE);

            HWND hFocusedWnd = GetFocus();
            CreateSafeWaitWindow(LoadStrW(IDS_ANALYSINGDIRTREEESC), NULL, 1000, TRUE, MainWindow->HWindow);
            EnableWindow(MainWindow->HWindow, FALSE);

            HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

            BOOL res = BuildScriptMain2(script, copy, targetPath, data);

            // swapped so the main window can be activated (must not be disabled), otherwise it switches to another app
            EnableWindow(MainWindow->HWindow, TRUE);
            DestroySafeWaitWindow();

            // if Salamander is active, call SetFocus on the remembered window (SetFocus doesn't work
            // when the main window is disabled - after reactivation/activation of the disabled main window
            // the active panel lacks focus)
            HWND hwnd = GetForegroundWindow();
            while (hwnd != NULL && hwnd != MainWindow->HWindow)
                hwnd = GetParent(hwnd);
            if (hwnd == MainWindow->HWindow)
                SetFocus(hFocusedWnd);

            SetCursor(oldCur);

            BOOL cancel = FALSE;
            if (res)
            {
                CQuadWord requiredSpace;
                if (ShouldWarnNotEnoughSpaceForCopyMove(script, targetPath, &requiredSpace))
                {
                    const std::wstring requiredSpaceText = NumberToStr(requiredSpace);
                    const std::wstring freeSpaceText = NumberToStr(script->FreeSpace);
                    std::wstring msg = FormatStrW(LoadStrW(IDS_NOTENOUGHSPACE),
                                                  requiredSpaceText.c_str(), freeSpaceText.c_str());
                    cancel = gPrompter->AskYesNo(captionW, msg.c_str()).type != PromptResult::kYes;
                }
            }

            // prepare a refresh for non-auto-refreshed directories
            // change in the target directory and its subdirectories
            script->SetWorkPath1W(targetPath, TRUE);
            if (!copy) // a move operation modifies the source as well
            {
                if (data->Count > 0)
                {
                    CCopyMoveRecord* record = data->At(0);
                    const wchar_t* name = record != NULL && record->IsValid()
                                              ? record->FileName.c_str()
                                              : NULL;
                    if (name != NULL)
                    {
                        std::wstring path = name;
                        if (CutDirectoryW(path)) // assume a single source directory (panel operations only, not Find)
                        {
                            // change in the source directory and its subdirectories
                            script->SetWorkPath2W(path.c_str(), TRUE);
                        }
                    }
                }
            }

            if (cancel || !res || !StartProgressDialog(script, captionW, NULL, NULL))
            {
                UpdateWindow(MainWindow->HWindow);
                if (!script->IsGood())
                    script->ResetState();
                FreeScript(script);
            }
            else
            {
                UpdateWindow(MainWindow->HWindow);
                started = TRUE;
            }
        }
        //---  if any Salamander window activated, suspend mode ends
        EndStopRefresh();
        FilesActionInProgress = FALSE;
    }
    return started;
}

BOOL CFilesWindow::BuildScriptMain(COperations* script, CActionType type,
                                   const wchar_t* targetPath, const wchar_t* mask, int selCount,
                                   int* selection, CFileData* oneFile,
                                   CAttrsData* attrsData, CChangeCaseData* chCaseData,
                                   BOOL onlySize, CCriteriaData* filterCriteria,
                                   const wchar_t* targetPathW)
{
    CALL_STACK_MESSAGE5("CFilesWindow::BuildScriptMain(, %d, %ls, %ls, %d, , , , , ,)",
                        type, targetPath, mask, selCount);
    // count == 0, selection == NULL => oneFile points to the current file
    // otherwise selection contains indexes of the count selected items in the filebox
    if (!script->IsGood())
        return FALSE;
    script->TotalSize = CQuadWord(0, 0);
    script->CompressedSize = CQuadWord(0, 0);
    script->OccupiedSpace = CQuadWord(0, 0);
    script->TotalFileSize = CQuadWord(0, 0);

    CScopedBuildScriptState scopedBuildScriptState;
    CBuildScriptState& bsState = GetActiveBuildScriptState();

    std::wstring fsName;

    //---  when copying/moving from CD, clear the read-only attribute
    //     and set CurrentDirectory to the slower medium
    BOOL fastDirectoryMove = TRUE; // Configuration.FastDirectoryMove;
    if (type == atCopy || type == atMove)
    {
        UINT sourceType = DRIVE_REMOTE;
        if (GetPathW()[0] != L'\\') // not a UNC path (those are always "remote")
        {
            // Asked of the mirror, this returns the drive type of whatever
            // the '?'-string names - or DRIVE_NO_ROOT_DIR for nothing at all. It decides
            // RemovableSrcDisk, which current directory to sit in, and the fast-dir-move
            // gate below, so a wrong answer here mis-plans the whole operation.
            sourceType = MyGetDriveTypeW(GetPathW());
        }

        if (sourceType == DRIVE_REMOTE || sourceType == DRIVE_REMOVABLE)
        {
            gEnvironment->SetCurrentDirectory(GetPathW());
        }
        else if (targetPathW != NULL && targetPathW[0] != 0)
            gEnvironment->SetCurrentDirectory(targetPathW); // the typed destination, unnarrowed
        else
            gEnvironment->SetCurrentDirectory(targetPath);

        if (sourceType == DRIVE_REMOVABLE)
            script->RemovableSrcDisk = TRUE;

        if (fastDirectoryMove &&                            // fast-dir-move isn't globally disabled
            sourceType == DRIVE_REMOTE && type == atMove && // network disk + move operation
            HasTheSameRootPath(GetPathW(),
                               targetPathW != NULL && targetPathW[0] != 0 ? targetPathW : targetPath)) // + within the same drive
        {                                                   // detect Novell disks - fast-directory-move doesn't work on them
            // Completes the fast-dir-move gate started above: the same-root
            // test went wide in the previous round, but the provider lookup was still
            // matching the CP_ACP mirror against the ANSI network enumeration.
            if (IsNOVELLDriveW(GetPathW()))
                fastDirectoryMove = Configuration.NetwareFastDirMove;
        }

        script->ClearReadonlyMask = 0xFFFFFFFF;
        if (Configuration.ClearReadOnly)
        {
            if (sourceType == DRIVE_REMOTE)
            {
                DWORD dummy, flags;
                if (GetFileSystemNameForPathW(GetPathW(), &dummy, &flags, fsName) &&
                    StrICmpW(fsName.c_str(), L"CDFS") == 0)
                {
                    script->ClearReadonlyMask = ~(FILE_ATTRIBUTE_READONLY);
                }
            }
            else
            {
                if (sourceType == DRIVE_CDROM)
                    script->ClearReadonlyMask = ~(FILE_ATTRIBUTE_READONLY);
            }
        }

        // check if the target is removable media (floppy, ZIP) -> a larger buffer is used for speed
        if (towlower(*targetPath) >= L'a' && towlower(*targetPath) <= L'z' &&
            *(targetPath + 1) == L':')
        {
            wchar_t root2[4] = L" :\\";
            root2[0] = targetPath[0];
            UINT targetType = GetDriveTypeW(root2);

            if (targetType == DRIVE_REMOVABLE)
                script->RemovableTgtDisk = TRUE;
        }
    }
    else
        script->ClearReadonlyMask = 0xFFFFFFFF;

    // the mask must not be modified via PrepareMask !!! see MaskName()
    std::wstring nameMask;
    if (mask != NULL)
    {
        nameMask = mask;
        mask = nameMask.c_str();
    }

    // file access is much faster in the current directory/disk
    if (type != atMove && type != atCopy)
        gEnvironment->SetCurrentDirectory(GetPathW());

    GetAsyncKeyState(VK_ESCAPE); // initialize GetAsyncKeyState - see help

    std::wstring sourcePath = GetPathW();
    // GetPathW() is already the authoritative wide panel path; no ansi-fallback
    // projection is needed here (EffectivePanelPathW would just return it back).
    const wchar_t* sourcePathWArg = GetPathW();

    BOOL sourceSupADS = FALSE;
    BOOL targetSupADS = FALSE;
    BOOL targetIsFAT32 = FALSE;
    CTargetPathState targetPathState = tpsUnknown;
    DWORD srcAndTgtPathsFlags = 0;        // flags only for Copy and Move
    if (type == atMove || type == atCopy) // outside Copy and Move it makes no sense to check
    {
        sourceSupADS = (filterCriteria == NULL || !filterCriteria->IgnoreADS) &&
                       IsPathOnVolumeSupADSW(sourcePath.c_str(), NULL);
        //    BOOL targetSupEFS;
        targetSupADS = IsPathOnVolumeSupADSW(targetPath, &targetIsFAT32);
        targetPathState = GetTargetPathState(targetPathState, targetPath);
        script->TargetPathSupADS = targetSupADS;
        //    script->TargetPathSupEFS = targetSupEFS;
        srcAndTgtPathsFlags |= GetPathFlagsForCopyOpW(sourcePathWArg, OPFL_SRCPATH_IS_NET, OPFL_SRCPATH_IS_FAST) |
                               GetPathFlagsForCopyOpW(targetPathW != NULL && targetPathW[0] != 0 ? targetPathW : targetPath,
                                                      OPFL_TGTPATH_IS_NET, OPFL_TGTPATH_IS_FAST);
        script->SourcePathIsNetwork = (srcAndTgtPathsFlags & OPFL_SRCPATH_IS_NET) != 0;

        if (filterCriteria != NULL)
        {
            script->OverwriteOlder = filterCriteria->OverwriteOlder;
            script->CopySecurity = filterCriteria->CopySecurity;
            script->PreserveDirTime = filterCriteria->PreserveDirTime;
            script->CopyAttrs = filterCriteria->CopyAttrs;
            script->StartOnIdle = filterCriteria->StartOnIdle;

            if (script->CopySecurity)
            {
                DWORD dummy1, flags;
                if (MyGetVolumeInformationW(targetPath, NULL, NULL, NULL, NULL, NULL, &dummy1, &flags,
                                            NULL) &&
                    (flags & FS_PERSISTENT_ACLS) == 0)
                { // wants to copy permissions, but the target path doesn't support them, so we inform the user (the API function for setting security doesn't report any errors — which is poor design)
                    PromptResult res = gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), LoadStrW(IDS_ACLNOTSUPPORTEDONTGTPATH));
                    UpdateWindow(MainWindow->HWindow);
                    if (res.type == PromptResult::kNo) // user chose NO -> abort
                        return FALSE;
                }
            }
        }
    }

    BOOL subDirectories = ((type != atChangeCase) || chCaseData->SubDirs) && type != atConvert;
    BOOL countSize = (type == atCountSize);
    CQuadWord oldTotalSize;

    wchar_t* useName = (oneFile != NULL ? oneFile->Name : NULL);
    wchar_t* useDOSName = (oneFile != NULL ? oneFile->DosName : NULL);
    if (type == atDelete && selCount <= 1 && oneFile != NULL && oneFile->DosName != NULL)
    {
        std::wstring candidatePath = sourcePath;
        SalPathAppendW(candidatePath, oneFile->Name);
        // try whether the file name is valid; if not, try its DOS name
        // (handles files accessible only via Unicode or DOS names)
        if (gFileSystem->GetFileAttributes(candidatePath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_INVALID_NAME)
            {
                candidatePath = sourcePath;
                SalPathAppendW(candidatePath, oneFile->DosName);
                if (gFileSystem->GetFileAttributes(candidatePath.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    useName = oneFile->DosName;
                    useDOSName = NULL;
                }
            }
        }
    }

    CSelectionSnapshot snapshot = TakeSnapshot(type, selCount, selection, oneFile);
    BOOL snapshotTargetNamesReady = TRUE;
    if (type == atCopy || type == atMove)
        snapshotTargetNamesReady = PopulateSnapshotTargetNames(snapshot, mask);

    if (selCount > 0 || oneFile != NULL)
    {
        if (type == atMove || type == atCopy) // outside Copy and Move it makes no sense to check
        {
            DWORD d1, d2, d3, d4;
            if (MyGetDiskFreeSpaceW(targetPath, &d1, &d2, &d3, &d4))
            {
                script->BytesPerCluster = d1 * d2;
                // W2K and later: the product d1 * d2 * d3 did not work on DFS trees, reported by Ludek.Vydra@k2atmitec.cz
                script->FreeSpace = MyGetDiskFreeSpaceW(targetPath);
            }
        }

        BOOL wouldUseFastDirMove = FALSE;
        if (type == atMove && Is(ptDisk))
        {
            BOOL selectionHasDir = FALSE;
            for (const CSnapshotItem& item : snapshot.Items)
            {
                if (item.IsDir)
                {
                    selectionHasDir = TRUE;
                    break;
                }
            }
            wouldUseFastDirMove =
                selectionHasDir && fastDirectoryMove && !script->CopySecurity &&
                (script->CopyAttrs ||
                 (targetPathState != tpsEncryptedExisting &&
                  targetPathState != tpsEncryptedNotExisting)) &&
                (filterCriteria == NULL ||
                 (!filterCriteria->UseMasks && !filterCriteria->UseAdvanced &&
                  !filterCriteria->SkipEmptyDirs)) &&
                !script->SameRootButDiffVolume &&
                HasTheSameRootPath(sourcePath.c_str(), targetPathW != NULL && targetPathW[0] != 0 ? targetPathW : targetPath);
        }

        if (snapshotTargetNamesReady &&
            CanBuildFirstTrancheFromSnapshot(Is(ptDisk), type, targetPath, mask,
                                             attrsData, chCaseData, onlySize,
                                             sourceSupADS, targetSupADS, sourcePath.c_str(),
                                             sourcePathWArg,
                                             filterCriteria, snapshot))
        {
            if (targetPathW != NULL && targetPathW[0] != 0)
                snapshot.TargetPathW = targetPathW;
            else if (targetPath != NULL)
                snapshot.TargetPathW = targetPath;
            snapshot.Mask = mask != NULL ? mask : L"";
            snapshot.UseRecycleBin = script->CanUseRecycleBin != FALSE;
            snapshot.InvertRecycleBin = script->InvertRecycleBin != FALSE;
            snapshot.OverwriteOlder = script->OverwriteOlder != FALSE;
            snapshot.CopySecurity = script->CopySecurity != FALSE;
            snapshot.CopyAttrs = script->CopyAttrs != FALSE;
            snapshot.PreserveDirTime = script->PreserveDirTime != FALSE;
            snapshot.IgnoreADS = filterCriteria != NULL && filterCriteria->IgnoreADS != FALSE;
            snapshot.SkipEmptyDirs = filterCriteria != NULL && filterCriteria->SkipEmptyDirs != FALSE;
            snapshot.StartOnIdle = script->StartOnIdle != FALSE;
            snapshot.UseSpeedLimit = filterCriteria != NULL && filterCriteria->UseSpeedLimit != FALSE;
            snapshot.SpeedLimit = filterCriteria != NULL ? filterCriteria->SpeedLimit : 0;

            CBuildConfig buildConfig;
            buildConfig.SourceSupportsADS = sourceSupADS;
            buildConfig.TargetSupportsADS = targetSupADS;
            buildConfig.TargetIsFAT32 = targetIsFAT32;
            buildConfig.TargetPathIsEncrypted = targetPathState == tpsEncryptedExisting ||
                                                targetPathState == tpsEncryptedNotExisting;
            buildConfig.EnableRecursiveDirectories = TRUE;
            buildConfig.EnableFilters = filterCriteria != NULL &&
                                        (filterCriteria->UseMasks || filterCriteria->UseAdvanced);
            buildConfig.SkipEmptyDirs = filterCriteria != NULL && filterCriteria->SkipEmptyDirs;
            buildConfig.FilterPredicate = buildConfig.EnableFilters ? SnapshotFilterPredicate : nullptr;
            buildConfig.FilterContext = buildConfig.EnableFilters ? filterCriteria : nullptr;
            if (type == atDelete && !IsSnapshotBuilderDefaultMask(mask))
            {
                // Masked delete: match files at every level.
                buildConfig.EnableFilters = TRUE;
                buildConfig.FilterPredicate = SnapshotDeleteMaskPredicate;
                buildConfig.FilterContext = const_cast<wchar_t*>(mask);
            }
            buildConfig.EnableADS = sourceSupADS && targetSupADS;
            buildConfig.ADSProbe = BuildScriptLegacyADSProbe;
            buildConfig.ADSProbeErrorCallback = SnapshotADSProbeErrorPrompt;
            buildConfig.ADSProbeErrorContext = &HWindow;
            SnapshotLinkTargetSizeContext linkSizeContext = {HWindow, FALSE};
            buildConfig.LinkTargetSizeCallback = SnapshotLinkTargetSizePrompt;
            buildConfig.LinkTargetSizeContext = &linkSizeContext;
            buildConfig.SourcePathIsNetwork = script->SourcePathIsNetwork;
            buildConfig.IgnoreADS = snapshot.IgnoreADS;
            buildConfig.EnableExplicitTargetNames = !IsSnapshotBuilderDefaultMask(mask);
            buildConfig.ClearReadOnly = script->ClearReadonlyMask == ~(FILE_ATTRIBUTE_READONLY);
            buildConfig.NetwareFastDirMove = fastDirectoryMove;
            buildConfig.ConfirmDeleteSystemHiddenDir = Configuration.CnfrmSHDirDel;
            buildConfig.ConfirmDeleteNonEmptyDir = Configuration.CnfrmNEDirDel;

            // The six capabilities go live in production.
            buildConfig.EnableCountSize = TRUE;
            buildConfig.CountCompressedSizes = type == atCountSize && !onlySize;
            buildConfig.CountSizeErrorCallback = SnapshotCountSizeErrorPrompt;
            buildConfig.CountSizeErrorContext = script;
            // An unreadable directory must not kill the whole operation in
            // silence: ask, and keep building on Skip (legacy parity).
            bool buildCancelledByUser = false;
            buildConfig.ListDirErrorCallback = SnapshotListDirErrorPrompt;
            buildConfig.ListDirErrorContext = &buildCancelledByUser;
            buildConfig.CancelPollCallback = SnapshotCancelPoll;
            buildConfig.CancelPollContext = &buildCancelledByUser;
            buildConfig.Fat32TooBigCallback = SnapshotFat32TooBigPrompt;
            buildConfig.Fat32TooBigContext = &buildCancelledByUser;
            buildConfig.EnableFastDirMove = TRUE;
            buildConfig.EnableReparseDelete = TRUE;
            buildConfig.EnableReparseCopyMove = TRUE; // copy-the-link
            buildConfig.DeletePromptCallback = SnapshotDeletePrompt;
            SnapshotAdsLossContext adsLossContext = {type, &bsState, HWindow};
            buildConfig.AdsLossPromptCallback = SnapshotAdsLossPrompt;
            buildConfig.AdsLossPromptContext = &adsLossContext;
            SnapshotLinkContentContext linkContentContext = {HWindow, &buildCancelledByUser};
            buildConfig.LinkContentPromptCallback = SnapshotLinkContentPrompt;
            buildConfig.LinkContentPromptContext = &linkContentContext;
            if (type == atChangeAttrs && attrsData != NULL)
            {
                buildConfig.EnableChangeAttrs = TRUE;
                buildConfig.ChangeAttrsAnd = attrsData->AttrAnd;
                buildConfig.ChangeAttrsOr = attrsData->AttrOr;
                buildConfig.ChangeAttrsSubDirs = attrsData->SubDirs;
                snapshot.AttrsData.AttrAnd = attrsData->AttrAnd;
                snapshot.AttrsData.AttrOr = attrsData->AttrOr;
                snapshot.AttrsData.SubDirs = attrsData->SubDirs != FALSE;
            }
            if (type == atChangeCase && chCaseData != NULL)
            {
                buildConfig.EnableChangeCase = TRUE;
                buildConfig.ChangeCaseFormat = chCaseData->FileNameFormat;
                buildConfig.ChangeCaseChange = chCaseData->Change;
                buildConfig.ChangeCaseSubDirs = chCaseData->SubDirs;
                snapshot.ChangeCaseData.FileNameFormat = chCaseData->FileNameFormat;
                snapshot.ChangeCaseData.Change = chCaseData->Change;
                snapshot.ChangeCaseData.SubDirs = chCaseData->SubDirs != FALSE;
            }

            if (!BuildScriptFromSnapshot(snapshot, buildConfig, bsState, script))
            {
                // Never fail in silence. This used to be a bare
                // `return FALSE`, so one unreadable directory aborted the whole
                // operation with no message at all — Ctrl+Q simply did nothing.
                // A user cancel already explained itself, so it stays quiet.
                //
                // A source and target naming the same object gets the message
                // legacy gave it. Blaming the script builder for that is both
                // unhelpful and untrue: nothing failed, the request was empty.
                int selfOpMsgId = 0;
                switch (bsState.SelfOpReject)
                {
                case CBuildScriptState::ESelfOpReject::CopyFileToItself:
                    selfOpMsgId = IDS_CANNOTCOPYFILETOITSELF;
                    break;
                case CBuildScriptState::ESelfOpReject::MoveFileToItself:
                    selfOpMsgId = IDS_CANNOTMOVEFILETOITSELF;
                    break;
                case CBuildScriptState::ESelfOpReject::MoveDirToItself:
                    selfOpMsgId = IDS_CANNOTMOVEDIRTOITSELF;
                    break;
                case CBuildScriptState::ESelfOpReject::None:
                    break;
                }
                if (selfOpMsgId != 0)
                {
                    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE),
                                         LoadStrW(selfOpMsgId));
                }
                else if (!buildCancelledByUser)
                {
                    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE),
                                         LoadStrW(IDS_ERRORBUILDINGSCRIPT));
                }
                SetCurrentDirectoryToSystem();
                return FALSE;
            }

            // CountSize: write the per-item deltas back into the
            // panel rows (legacy did this inside its do-loop, copy_move :2408).
            if (type == atCountSize)
            {
                for (int wj = 0; wj < selCount && wj < (int)bsState.PerItemTotalSizes.size(); wj++)
                {
                    CFileData* wf = (selection[wj] < Dirs->Count)
                                        ? &Dirs->At(selection[wj])
                                        : &Files->At(selection[wj] - Dirs->Count);
                    if (selection[wj] < Dirs->Count)
                    {
                        wf->Size.SetUI64(bsState.PerItemTotalSizes[wj]);
                        wf->SizeValid = 1;
                    }
                }
                if (selCount == 0 && oneFile != NULL &&
                    (oneFile->Attr & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                    !bsState.PerItemTotalSizes.empty())
                {
                    oneFile->Size.SetUI64(bsState.PerItemTotalSizes[0]);
                    oneFile->SizeValid = 1;
                }
            }

            StampBuildScriptWorkPaths(script, type, sourcePathWArg,
                                      targetPathW != NULL && targetPathW[0] != 0 ? targetPathW : targetPath);
            SetCurrentDirectoryToSystem();
            return TRUE;
        }

// no legacy fallback. Every admitted shape routes
        // through the snapshot builder above; a gate reject reaching here is a
        // genuinely unsupported form (ADS probe error, attribute compression/
        // encryption change) and refuses honestly instead of running the
        // retired lossy pipeline. The panel flows are single-builder from here.
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_ERRORBUILDINGSCRIPT));
        SetCurrentDirectoryToSystem();
        return FALSE;
    }

    SetCurrentDirectoryToSystem();
    int i;
    for (i = 0; i < script->Count; i++)
        script->TotalSize += script->At(i).Size;
    if (!onlySize)
        StampBuildScriptWorkPaths(script, type, sourcePathWArg,
                                  targetPathW != NULL && targetPathW[0] != 0 ? targetPathW : targetPath);
    return TRUE;
}

void GetADSStreamsNames(const std::wstring& fileNameW, BOOL isDir,
                        std::wstring& list)
{
    list.clear();
    std::vector<std::wstring> streamNames;
    BOOL lowMemory = FALSE;
    if (CheckFileOrDirADS(fileNameW, isDir, NULL, &streamNames, &lowMemory,
                          NULL, 0, NULL, NULL) &&
        !lowMemory)
    {
        for (size_t i = 0; i < streamNames.size(); i++)
        {
            size_t start = !streamNames[i].empty() && streamNames[i][0] == L':' ? 1 : 0;
            size_t end = streamNames[i].find(L':', start);
            if (end == std::wstring::npos)
                end = streamNames[i].size();
            for (size_t pos = start; pos < end; pos++)
            {
                const wchar_t ch = streamNames[i][pos];
                if (ch < L' ')
                {
                    wchar_t escaped[8];
                    swprintf_s(escaped, L"\\x%02X", (unsigned int)ch);
                    list.append(escaped);
                }
                else
                    list.push_back(ch);
            }
            if (i + 1 < streamNames.size())
                list.append(L", ");
        }
    }

    if (_wcsicmp(list.c_str(), L"Zone.Identifier") == 0 || // created by Windows and safe to ignore
        _wcsicmp(list.c_str(), L"encryptable") == 0)      // commonly attached to thumbs.db
    {
        list.clear();
    }
}

// The COperation leg is gone: both call sites pass op == NULL with
// an explicit name (the builder no longer needs mid-build link sizing).
// wide. SalGetFileSize2 is the native-wide owner, and the ADS error
// dialog has carried a 'fileW' slot since an earlier tranche, so nothing here
// needed inventing.
// NOTE: 'op' is dead - every caller passes NULL and the body never reads it.
// Left alone deliberately; removing it is a cleanup, not part of this widening.
BOOL GetLinkTgtFileSize(HWND parent, const wchar_t* fileName, COperation* op, CQuadWord* size,
                        BOOL* cancel, BOOL* ignoreAll)
{
    *cancel = FALSE;
    if (fileName == NULL)
        return FALSE;

READLINKTGTSIZE_AGAIN:

    DWORD err;
    if (SalGetFileSize2(fileName, *size, &err))
        return TRUE;
    else
    {
        int res;
        if (*ignoreAll)
            res = IDB_IGNORE;
        else
        {
            // The narrow duplicates are gone. They were already unread here -
            // the wide slots carried fileName / the wide title / owned error text, and the narrow
            // "" and the old narrow error text were dead arguments. GetErrorTextOwned keeps
            // FormatMessageW's native text intact instead of re-encoding it through CP_ACP.
            res = (int)CErrorReadingADSDlg(parent, fileName, GetErrorTextOwned(err).c_str(),
                                           LoadStrW(IDS_ERRORGETTINGLINKTGTSIZE))
                      .Execute();
        }
        switch (NormalizeADSReadErrorResponse(res, ignoreAll))
        {
        case IDRETRY:
            goto READLINKTGTSIZE_AGAIN;

        case IDB_IGNORE:
            break;

        case IDCANCEL:
        {
            *cancel = TRUE;
            break;
        }
        }
        return FALSE;
    }
}

void CFilesWindow::CalculateDirSizes()
{
    CALL_STACK_MESSAGE1("CFilesWindow::CalculateDirSizes()");
    if (Is(ptDisk))
    {
        FilesAction(atCountSize, MainWindow->GetNonActivePanel(), 2);
    }
    else
    {
        if (Is(ptZIPArchive))
        {
            CalculateOccupiedZIPSpace(2);
        }
    }
}

void CFilesWindow::ExecuteFromArchive(int index, BOOL edit, HWND editWithMenuParent,
                                      const POINT* editWithMenuPoint)
{
    CALL_STACK_MESSAGE3("CFilesWindow::ExecuteFromArchive(%d, %d)", index, edit);
    if (CheckPath(TRUE) != ERROR_SUCCESS)
        return;

    // check if we can pack into the archive (editing files from the archive is possible, otherwise we warn the user)
    if (edit)
    {
        int format = PackerFormatConfig.PackIsArchive(GetZIPArchive());
        if (format != 0) // "always-true" - we found a supported archive
        {
            if (!PackerFormatConfig.GetUsePacker(format - 1)) // no Edit?
            {
                if (gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), LoadStrW(IDS_EDITPACKNOTSUPPORTED)).type != PromptResult::kYes)
                {
                    return; // action aborted (user does not want to edit if the archive cannot be updated)
                }
            }
        }
    }

    //---  get the full long name
    std::wstring dcFileName = sally::text::Fold(GetZIPArchive());
    CFileData* f = &Files->At(index - Dirs->Count);

    // Validate the wide name. Asking the ANSI validator about the mirror meant every
    // non-ANSI entry in an archive was reported as having an invalid name, because the
    // '?' the mirror is made of is itself one of the rejected characters (audit C5).
    if (!SalIsValidFileNameComponentW(f->Name))
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_UNABLETOEDITINVFILES));
        return;
    }

    int count = Dirs->Count + Files->Count;
    int j;
    for (j = 0; j < count; j++)
    {
        if (index != j) // do not compare the same item
        {
            CFileData* f2 = j < Dirs->Count ? &Dirs->At(j) : &Files->At(j - Dirs->Count);
            // StrNICmp is wide and Name is the only name, so this compare IS
            // the wide truth; the PanelItemWideNamesAgree confirmation it used to carry is gone.
            if (f2->NameLen == f->NameLen &&
                StrNICmpW(f->Name, f2->Name, f2->NameLen) == 0)
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_UNABLETOEDITDUPFILES));
                return;
            }
        }
    }

    SalPathAppendW(dcFileName, GetZIPPath());
    SalPathAppendW(dcFileName, f->Name);

    // disk-cache settings for the plugin (default values change only for plugins)
    std::wstring arcCacheTmpPath;
    BOOL arcCacheOwnDelete = FALSE;
    BOOL arcCacheCacheCopies = TRUE;
    CPluginInterfaceAbstract* plugin = NULL; // != NULL if the plugin deletes files on its own
    int format = PackerFormatConfig.PackIsArchive(GetZIPArchive());
    if (format != 0) // found a supported archive
    {
        format--;
        int index2 = PackerFormatConfig.GetUnpackerIndex(format);
        if (index2 < 0) // view: is this internal handling (plug-in)?
        {
            CPluginData* data = Plugins.Get(-index2 - 1);
            if (data != NULL)
            {
                data->GetCacheInfo(arcCacheTmpPath, &arcCacheOwnDelete, &arcCacheCacheCopies);
                if (arcCacheOwnDelete)
                    plugin = data->GetPluginInterface()->GetInterface();
            }
        }
    }

    BOOL exists;
    CQuadWord fileSize = CQuadWord(-1, -1);
    FILETIME lastWrite;
    DWORD attr = -1;
    memset(&lastWrite, 0, sizeof(lastWrite));
    int errorCode;
    const wchar_t* name = DiskCache.GetName(dcFileName.c_str(), f->Name, &exists, FALSE,
                                            !arcCacheTmpPath.empty() ? arcCacheTmpPath.c_str() : NULL,
                                            plugin != NULL, plugin, &errorCode);
    if (name == NULL)
    {
        return;
    }
    WCHAR dosName[14];
    dosName[0] = 0;
    WIN32_FIND_DATAW data;
    if (!exists) // we must unpack it
    {
        const wchar_t* backSlash = wcsrchr(name, L'\\');
        const std::wstring tmpPath(name, backSlash);
        BeginStopRefresh(); // the snooper can take a break
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
        if (PackUnpackOneFile(this, GetZIPArchive(), PluginData.GetInterface(),
                              dcFileName.c_str() + wcslen(GetZIPArchive()) + 1, f, tmpPath.c_str(),
                              NULL, NULL))
        {
            SetCursor(oldCur);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

            HANDLE find = SalFindFirstFileHW(name, &data);
            if (find != INVALID_HANDLE_VALUE)
            {
                gFileSystem->CloseFind(find);
                fileSize = CQuadWord(data.nFileSizeLow, data.nFileSizeHigh);
                lastWrite = data.ftLastWriteTime;
                attr = data.dwFileAttributes;
                if (data.cAlternateFileName[0] != 0)
                    lstrcpynW(dosName, data.cAlternateFileName, 14);
            }

            DiskCache.NamePrepared(dcFileName.c_str(), fileSize);
            EndStopRefresh(); // the snooper resumes now
        }
        else
        {
            SetCursor(oldCur);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            DiskCache.ReleaseName(dcFileName.c_str(), FALSE); // not unpacked, nothing to cache
            EndStopRefresh();                         // the snooperresumes now
            return;
        }
    }

    // split the full file name into path (buf) and name (s)
    std::wstring buf;
    const wchar_t* s = wcsrchr(name, L'\\');
    if (s != NULL)
    {
        buf.assign(name, s);
        s++;
    }

    // launching the default item from the context menu (association)
    if (edit)
    {
        if (editWithMenuParent != NULL && editWithMenuPoint != NULL)
        {
            EditFileWith(name, editWithMenuParent, editWithMenuPoint);
        }
        else
            EditFile(name);
    }
    else
    {
        if (s != NULL)
        {
            HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
            MainWindow->SetDefaultDirectories();
            ExecuteAssociationW(GetListBoxHWND(), buf.c_str(), s);
            SetCursor(oldCur);
        }
    }

    if (fileSize == CQuadWord(-1, -1))
    {
        HANDLE find = SalFindFirstFileHW(name, &data);
        if (find != INVALID_HANDLE_VALUE)
        {
            gFileSystem->CloseFind(find);
            fileSize = CQuadWord(data.nFileSizeLow, data.nFileSizeHigh);
            lastWrite = data.ftLastWriteTime;
            attr = data.dwFileAttributes;
            if (data.cAlternateFileName[0] != 0)
                lstrcpynW(dosName, data.cAlternateFileName, 14);
        }
    }

    // The trailing f->NameW argument is gone with CFileTimeStampsItem's wide
    // twin: 's' is CDiskCache::GetName's tmpName leaf verbatim, so it is already the wide name.
    if (UnpackedAssocFiles.AddFile(GetZIPArchive(), GetZIPPath(), buf.c_str(), s, dosName, lastWrite, fileSize, attr))
    {                                                                         // this file doesn't have the disk-cache 'lock' object ExecuteAssocEvent yet
        DiskCache.AssignName(dcFileName.c_str(), ExecuteAssocEvent, FALSE, crtCache); // arcCacheCacheCopies has no effect – caching is done until the archive is closed, we won't unpack earlier
    }
    else
    { // it is unnecessary to add the same 'lock' object to a tmp file
        DiskCache.ReleaseName(dcFileName.c_str(), FALSE);
    }
    AssocUsed = TRUE;
}
