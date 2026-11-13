// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// F5/F6 and drag & drop: the copy/move services of the SFTP file system. Downloads and uploads are handed to the
// operations registry (workers with their own connections), so these functions return as soon as the operation runs.

#include "precomp.h"

#include "fs_object.h"

#include "fs_data.h"
#include "fs_messages.h"
#include "listing_format.h"
#include "operation.h"
#include "operations_registry.h"
#include "../ui/copy_move_dialog.h"
#include "site_config.h"
#include "text_util.h"
#include "wait_for_request.h"

namespace
{
bool WriteBuffer(CSalamanderStringBuffer* buffer, const std::wstring& text)
{
    return buffer != nullptr && sally::plugin_abi::WriteStringBuffer(*buffer, text);
}

// A Windows path (drive or UNC): the only kind of target Sally lets a download use.
bool LooksLikeWindowsPath(const std::wstring& path)
{
    return path.size() >= 2 && (path[1] == L':' || (path[0] == L'\\' && path[1] == L'\\'));
}

// The file name part of a typed Windows target (after the last backslash) has * or ? (other than "*" and "*.*").
bool HasWildcardFileName(const std::wstring& target)
{
    const size_t slash = target.find_last_of(L"\\/");
    std::wstring ignored;
    return ClassifyTargetMask(slash == std::wstring::npos ? target : target.substr(slash + 1), ignored) ==
           TargetMaskKind::Wildcard;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Helpers

std::wstring CPluginFSInterface::FullFsPath(const std::string& rawPath) const
{
    return AssignedFSName + L":" + UserPartOf(rawPath);
}

// The selected items of the panel (or the focused one when nothing is selected), with the raw names kept in the
// listing data. The up-dir item is never included.
bool CPluginFSInterface::CollectSelection(int panel, int selectedFiles, int selectedDirs,
                                          std::vector<CSelectedItem>& items) const
{
    const bool focused = selectedFiles == 0 && selectedDirs == 0;
    int index = 0;
    for (;;)
    {
        BOOL isDir = FALSE;
        const CFileData* file = focused ? SalamanderGeneral->GetPanelFocusedItem(panel, &isDir)
                                        : SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
        if (file == nullptr)
            break;
        const CSftpFileData* data = reinterpret_cast<const CSftpFileData*>(file->PluginData);
        if (data != nullptr || wcscmp(file->Name, L"..") != 0) // the up-dir has no plugin data
        {
            CSelectedItem item;
            item.rawName = data != nullptr ? data->rawName : codec_.Encode(file->Name);
            item.isDir = isDir != FALSE;
            item.isLink = data != nullptr && (data->mode & 0170000) == 0120000; // S_IFLNK
            item.mode = data != nullptr ? data->mode : 0;
            item.modeKnown = data != nullptr && data->modeKnown;
            item.size = file->Size.Value;
            item.mtime = FileTimeToUnixTime(file->LastWrite);
            items.push_back(std::move(item));
        }
        if (focused)
            break;
    }
    return !items.empty();
}

// The workers check the host key against the same files as the panel (their own CKnownHosts, built on the worker).
void CPluginFSInterface::SetKnownHostsPaths(CPanelConnectParams& params)
{
    const CNameCodec utf8 = CNameCodec::Utf8();
    params.knownHostsOwnPath = utf8.Encode(KnownHostsOwnPath());
    params.knownHostsUserPath = utf8.Encode(KnownHostsUserPath());
}

// What a worker needs to log in: the panel connection's site, secrets and accepted host key when this FS is connected to
// that server (D35); otherwise (a temporary FS from OpenFS, or a panel connection that is gone) the site from the
// configuration with its remembered secrets, asking for a password when needed. Without a pinned host key the worker
// asks through the operation window.
bool CPluginFSInterface::MakeWorkerParams(const CSftpPath& server, CPanelConnectParams& params)
{
    if (connected_ && session_ && SameServer(identity_, server))
    {
        params = session_->CopyConnectParams();
        if (params.valid)
        {
            SetKnownHostsPaths(params);
            return true;
        }
    }
    CSite site;
    CSecret password;
    CSecret passphrase;
    if (!ResolveSite(server, site, password, passphrase))
        return false;
    params = CPanelConnectParams();
    params.valid = true;
    params.site = site;
    params.password = std::move(password);
    params.passphrase = std::move(passphrase);
    SetKnownHostsPaths(params); // no pinned key here: the workers check the known_hosts files or ask
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Download (F5/F6 from the panel to a Windows folder)

BOOL WINAPI
CPluginFSInterface::CopyOrMoveFromFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent, int panel,
                                     int selectedFiles, int selectedDirs, CSalamanderStringBuffer* targetPath,
                                     BOOL& operationMask, BOOL& cancelOrHandlePath, HWND dropTarget)
{
    CALL_STACK_MESSAGE6("CPluginFSInterface::CopyOrMoveFromFS(%d, %d, , , %d, %d, %d, , , ,)", copy, mode, panel,
                        selectedFiles, selectedDirs);
    CBusyGuard busy(this); // declared first: destroyed last, and may delete 'this' (see ChangePath)
    try
    {
        std::wstring payload;
        if (targetPath == nullptr || !sally::plugin_abi::ReadStringBuffer(*targetPath, payload) || closeRequested_)
        {
            cancelOrHandlePath = TRUE;
            return TRUE; // cancel
        }
        std::wstring path;
        std::wstring mask; // only a single file gets one (see AskDownloadTarget): the new name of that file
        SplitPathAndMask(payload, path, mask);
        const BOOL result = DoCopyOrMoveFromFS(copy, mode, fsName, parent, panel, selectedFiles, selectedDirs, path, mask,
                                               operationMask, cancelOrHandlePath);
        if (!WriteBuffer(targetPath, path))
        {
            cancelOrHandlePath = TRUE;
            return TRUE;
        }
        return result;
    }
    catch (...)
    {
        TRACE_E("CopyOrMoveFromFS: unexpected exception.");
        cancelOrHandlePath = TRUE;
        return TRUE;
    }
}

BOOL CPluginFSInterface::DoCopyOrMoveFromFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent, int panel,
                                            int selectedFiles, int selectedDirs, std::wstring& path,
                                            const std::wstring& mask, BOOL& operationMask, BOOL& cancelOrHandlePath)
{
    if (!connected_)
    {
        ShowMessage(LangStr(IDS_ERR_NOT_CONNECTED));
        cancelOrHandlePath = TRUE;
        return TRUE;
    }
    if (mode == 5)
        return DropOnTarget(copy, fsName, parent, panel, selectedFiles, selectedDirs, path, cancelOrHandlePath);

    if (mode != 3) // 1: first call, 2 and 4: Sally wants the target again (it never asks for 2 here), the path was wrong
        return AskDownloadTarget(copy, fsName, parent, panel, selectedFiles, selectedDirs, mode, path, operationMask,
                                 cancelOrHandlePath);

    // Mode 3: Sally has checked the Windows folder and created what was missing. The dialog's choices wait in
    // downloadOptions_ (defaults when the dialog did not run).
    const CDownloadOptions options = downloadOptions_;
    downloadOptions_ = CDownloadOptions();
    const ConflictPolicy policy = options.valid ? options.policy : ConflictPolicy::Ask;
    const bool preserveTime = options.valid ? options.preserveTime : site_.preserveTime;
    std::wstring targetName; // the single file's new name; empty: keep the remote name
    if (ClassifyTargetMask(mask, targetName) == TargetMaskKind::Wildcard)
    {
        ShowMessage(LangStr(IDS_ERR_MASK_UNSUPPORTED)); // the dialog already refuses these; this is the safety net
        cancelOrHandlePath = TRUE;
        return TRUE;
    }
    const bool started = StartDownload(copy, panel, selectedFiles, selectedDirs, path, policy, preserveTime, targetName);
    cancelOrHandlePath = started ? FALSE : TRUE;
    path.clear(); // no item to focus
    return TRUE;
}

// Modes 1, 2 and 4: the Copy/Move dialog. A Windows target is handed back to Sally (it checks the folder and calls again
// with mode 3). An sftp: target never reaches Sally (it can only handle Windows paths): a Move on the same server is
// done here as a rename, anything else is refused and the dialog comes back.
BOOL CPluginFSInterface::AskDownloadTarget(BOOL copy, const wchar_t* fsName, HWND parent, int panel, int selectedFiles,
                                           int selectedDirs, int mode, std::wstring& path, BOOL& operationMask,
                                           BOOL& cancelOrHandlePath)
{
    std::wstring sourceText;
    SPLGetCommonFSOperSourceDescrOwned(SalamanderGeneral, panel, selectedFiles, selectedDirs, nullptr, FALSE, FALSE,
                                       sourceText);
    const std::wstring subject = SPLFormatStringOwned(
        LangStr(copy ? IDS_COPYMOVE_SUBJECT_COPY : IDS_COPYMOVE_SUBJECT_MOVE).c_str(), sourceText.c_str());
    if (mode == 1 && !path.empty())
        SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_TARGET); // the default action works with the target panel path

    const std::wstring ownName = fsName != nullptr ? fsName : AssignedFSName;
    const bool singleFile = IsSingleFile(panel, selectedFiles, selectedDirs);
    CCopyMoveChoice choice;
    choice.target = path;
    if (!copy && mode == 1 && path.empty()) // only a Move can go to an sftp: folder (a Copy there is refused)
        choice.target = SftpPathOfTargetPanel(ownName); // Sally proposes nothing when the other panel is an FS
    choice.preserveTime = site_.preserveTime;
    for (;;)
    {
        CCopyMoveDialog dialog(parent, copy != FALSE, subject);
        const bool accepted = dialog.Run(choice);
        UpdateWindow(SalamanderGeneral->GetMainWindowHWND()); // do not leave the dialog's remains on screen
        if (!accepted)
        {
            cancelOrHandlePath = TRUE;
            return TRUE; // cancel
        }

        const SftpTargetResult result =
            TryMoveToSftpTarget(copy, choice.target, ownName, parent, panel, selectedFiles, selectedDirs);
        if (result == SftpTargetResult::NotSftp)
        {
            // a Windows path (or something Sally reports itself); a wildcard in the file name is ours to refuse
            if (!(singleFile && HasWildcardFileName(choice.target)))
                break;
            ShowMessage(LangStr(IDS_ERR_MASK_UNSUPPORTED));
        }
        else if (result != SftpTargetResult::Refused)
        {
            cancelOrHandlePath = result == SftpTargetResult::Moved ? FALSE : TRUE;
            path.clear(); // no item to focus
            return TRUE;
        }
        if (closeRequested_)
        {
            cancelOrHandlePath = TRUE;
            return TRUE;
        }
    }

    downloadOptions_.policy = choice.policy;
    downloadOptions_.preserveTime = choice.preserveTime;
    downloadOptions_.valid = true;
    path = choice.target;
    // One file: "C:\dir\new.ext" is the new name of the file (Sally hands it back as the mask in mode 3). Several items:
    // no masks, so a missing last folder is created as before.
    operationMask = singleFile ? TRUE : FALSE;
    cancelOrHandlePath = TRUE; // "process this path": Sally parses it and calls again with mode 3 (or 4)
    return FALSE;
}

// An sftp: target (typed in the Copy/Move dialog or dropped). It never reaches Sally, which can only handle Windows
// paths: a Move on the same server is done here as renames, anything else is refused with a message.
CPluginFSInterface::SftpTargetResult CPluginFSInterface::TryMoveToSftpTarget(BOOL copy, const std::wstring& targetText,
                                                                             const std::wstring& ownName, HWND parent,
                                                                             int panel, int selectedFiles,
                                                                             int selectedDirs)
{
    std::wstring userPart;
    if (!SplitFsPath(targetText, ownName, userPart))
        return SftpTargetResult::NotSftp;

    CSftpPath target;
    if (!ParseSftpPath(userPart, target))
    {
        ShowMessage(SPLFormatStringOwned(LangStr(IDS_ERR_BAD_PATH).c_str(), targetText.c_str()));
        return SftpTargetResult::Refused;
    }
    if (copy || !SameServer(identity_, target))
    {
        ShowMessage(LangStr(IDS_ERR_SFTP_TARGET_UNSUPPORTED));
        return SftpTargetResult::Refused;
    }
    return MoveOnServer(parent, panel, selectedFiles, selectedDirs, target) ? SftpTargetResult::Moved
                                                                           : SftpTargetResult::MoveFailed;
}

// The path of the other panel when it shows a folder on this same server (the natural target of a Move), else empty.
std::wstring CPluginFSInterface::SftpPathOfTargetPanel(const std::wstring& ownName) const
{
    std::wstring full;
    int type = 0;
    if (!SPLGetPanelPathOwned(SalamanderGeneral, PANEL_TARGET, full, &type) || type != PATH_TYPE_FS)
        return std::wstring();
    std::wstring userPart;
    CSftpPath target;
    if (!SplitFsPath(full, ownName, userPart) || !ParseSftpPath(userPart, target) || !SameServer(identity_, target))
        return std::wstring();
    return full;
}

// True for exactly one selected file, or no selection and a file in focus (a directory or several items never get a
// new name).
bool CPluginFSInterface::IsSingleFile(int panel, int selectedFiles, int selectedDirs) const
{
    if (selectedFiles == 1 && selectedDirs == 0)
        return true;
    if (selectedFiles != 0 || selectedDirs != 0)
        return false;
    BOOL isDir = FALSE;
    const CFileData* file = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
    return file != nullptr && !isDir && wcscmp(file->Name, L"..") != 0;
}

// Mode 5: files were dropped on 'path'. A Move to the same server is done on the server; otherwise only an existing
// Windows folder is a possible target.
BOOL CPluginFSInterface::DropOnTarget(BOOL copy, const wchar_t* fsName, HWND parent, int panel, int selectedFiles,
                                      int selectedDirs, std::wstring& path, BOOL& cancelOrHandlePath)
{
    const SftpTargetResult result = TryMoveToSftpTarget(copy, path, fsName != nullptr ? fsName : AssignedFSName, parent,
                                                        panel, selectedFiles, selectedDirs);
    if (result == SftpTargetResult::NotSftp)
        return DropToWindowsFolder(copy, parent, panel, selectedFiles, selectedDirs, path, cancelOrHandlePath);
    cancelOrHandlePath = result == SftpTargetResult::Moved ? FALSE : TRUE;
    path.clear(); // no item to focus
    return TRUE;
}

// Mode 5: files were dropped from the panel on 'path'. Only an existing Windows folder is a possible target.
BOOL CPluginFSInterface::DropToWindowsFolder(BOOL copy, HWND parent, int panel, int selectedFiles, int selectedDirs,
                                             std::wstring& path, BOOL& cancelOrHandlePath)
{
    cancelOrHandlePath = TRUE;
    if (LooksLikeWindowsPath(path))
        SPLSalPathAddBackslashOwned(path); // mode 5 always gives a folder

    const std::wstring title = LangStr(IDS_PLUGINNAME);
    std::wstring parsed = path;
    int type = 0;
    BOOL isDir = FALSE;
    size_t secondPartOffset = std::wstring::npos;
    if (!SPLSalParsePathOwned(SalamanderGeneral, parent, parsed, type, isDir, secondPartOffset, title.c_str(), FALSE,
                              nullptr, nullptr))
        return TRUE; // a syntax error: the parser has reported it
    if (type != PATH_TYPE_WINDOWS || secondPartOffset != parsed.size()) // an FS, an archive, or a missing folder
    {
        ShowMessage(LangStr(IDS_ERR_DROP_TARGET));
        return TRUE;
    }
    const bool started = StartDownload(copy, panel, selectedFiles, selectedDirs, parsed, ConflictPolicy::Ask,
                                       site_.preserveTime, std::wstring());
    cancelOrHandlePath = started ? FALSE : TRUE;
    return TRUE;
}

// Builds the queue from the selection and starts the operation (its window opens). The operation copies what it needs
// from this FS (D35) and never touches it again.
bool CPluginFSInterface::StartDownload(BOOL copy, int panel, int selectedFiles, int selectedDirs,
                                       const std::wstring& localDir, ConflictPolicy policy, bool preserveTime,
                                       const std::wstring& targetName)
{
    std::vector<CSelectedItem> items;
    if (!CollectSelection(panel, selectedFiles, selectedDirs, items))
        return false;
    CPanelConnectParams params;
    if (!MakeWorkerParams(identity_, params))
        return false;

    COperationSettings settings = SettingsFromSite(params.site, TransferDirection::Download, copy == FALSE);
    settings.policy = policy;
    settings.preserveTime = preserveTime;
    settings.localRoot = localDir;
    settings.remoteRoot = curRaw_;

    std::vector<CRemoteSource> sources;
    for (const CSelectedItem& item : items)
    {
        CRemoteSource source;
        source.path = JoinRemotePath(curRaw_, item.rawName);
        source.isDir = item.isDir;
        source.size = item.size;
        source.mtime = item.mtime;
        sources.push_back(std::move(source));
    }
    if (!targetName.empty() && sources.size() == 1 && !sources[0].isDir)
        sources[0].targetName = targetName; // a single file saved under a new name

    COperationLaunch launch;
    launch.source = FullFsPath(curRaw_);
    launch.target = localDir;
    launch.hostName = HostTextOf(params.site);
    launch.changedPaths.push_back(localDir);
    if (!copy)
        launch.changedPaths.push_back(launch.source); // the moved items are gone from here
    std::vector<CQueueItem> roots = COperation::BuildDownloadRoots(sources, settings, codec_);
    return OperationsRegistry.Launch(std::move(settings), std::move(params), std::move(roots), launch);
}

// F6 to a folder on the same server: one rename request per item on the panel session, no operation. Stops at the first
// problem (items already moved stay moved). True: everything was moved.
bool CPluginFSInterface::MoveOnServer(HWND parent, int panel, int selectedFiles, int selectedDirs,
                                      const CSftpPath& target)
{
    if (IsWaiting())
    {
        TRACE_E("MoveOnServer called while another wait is running.");
        return false;
    }
    std::vector<CSelectedItem> items;
    if (!CollectSelection(panel, selectedFiles, selectedDirs, items))
        return false;

    // the exact bytes of the current directory when the target is it (names that are not valid in the encoding)
    const std::string targetRaw = target.path == DisplayPath(curRaw_) ? curRaw_
                                                                      : NormalizeRemotePath(codec_.Encode(target.path));
    const std::wstring targetText = DisplayPath(targetRaw);
    for (const CSelectedItem& item : items)
    {
        const std::string from = JoinRemotePath(curRaw_, item.rawName);
        const std::string to = JoinRemotePath(targetRaw, item.rawName);
        if (from == to)
            continue;
        std::shared_ptr<CPanelRequest> request = session_->Rename(from, to, false);
        const WaitResult waited = WaitForRequest(parent, *session_, request, LangStr(IDS_WAIT_MOVING));
        if (waited == WaitResult::Cancelled || request->Cancelled() || !request->IsDone() || closeRequested_)
            return false;
        if (!request->Ok())
        {
            ShowMessage(SPLFormatStringOwned(LangStr(IDS_ERR_MOVE_ITEM).c_str(), codec_.Decode(item.rawName).c_str(),
                                             targetText.c_str(), DescribeError(request->Error()).c_str()));
            return false;
        }
    }
    // this panel and the one showing the target folder (this FS refreshes itself through AcceptChangeOnPathNotification)
    SalamanderGeneral->PostChangeOnPathNotification(FullFsPath(curRaw_).c_str(), FALSE);
    SalamanderGeneral->PostChangeOnPathNotification(FullFsPath(targetRaw).c_str(), FALSE);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Upload (F5/F6 or drag & drop from a Windows panel to sftp:)

BOOL WINAPI
CPluginFSInterface::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                           const wchar_t* sourcePath, SalEnumSelection2 next, void* nextParam,
                                           int sourceFiles, int sourceDirs, CSalamanderStringBuffer* targetPath,
                                           BOOL* invalidPathOrCancel)
{
    CALL_STACK_MESSAGE4("CPluginFSInterface::CopyOrMoveFromDiskToFS(%d, %d, , , , , , %d, , ,)", copy, mode,
                        sourceFiles);
    try
    {
        std::wstring payload;
        if (targetPath == nullptr || !sally::plugin_abi::ReadStringBuffer(*targetPath, payload))
        {
            if (invalidPathOrCancel != nullptr)
                *invalidPathOrCancel = TRUE;
            return FALSE;
        }
        // Sally appends a NUL (and an empty mask) to the path, like for CopyOrMoveFromFS: a NUL left in the path would
        // reach the server inside the remote names. An upload has no use for that empty mask (or "*.*"), but a real
        // mask typed by the user is refused (D43: masks are not supported); the dialog comes back with the same text.
        std::wstring path;
        std::wstring mask;
        SplitPathAndMask(payload, path, mask);
        std::wstring ignoredName;
        if (mode != 1 && ClassifyTargetMask(mask, ignoredName) == TargetMaskKind::Wildcard)
        {
            ShowMessage(LangStr(IDS_ERR_MASK_UNSUPPORTED));
            if (invalidPathOrCancel != nullptr)
                *invalidPathOrCancel = TRUE; // like a bad path: the user corrects it
            WriteBuffer(targetPath, path); // the dialog comes back with the path (the mask is dropped)
            return FALSE;
        }
        const BOOL result = DoCopyOrMoveFromDiskToFS(copy, mode, fsName, parent, sourcePath, next, nextParam, path,
                                                     invalidPathOrCancel);
        return WriteBuffer(targetPath, path) ? result : FALSE;
    }
    catch (...)
    {
        TRACE_E("CopyOrMoveFromDiskToFS: unexpected exception.");
        if (invalidPathOrCancel != nullptr)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }
}

BOOL CPluginFSInterface::DoCopyOrMoveFromDiskToFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                                  const wchar_t* sourcePath, SalEnumSelection2 next, void* nextParam,
                                                  std::wstring& targetPath, BOOL* invalidPathOrCancel)
{
    if (invalidPathOrCancel != nullptr)
        *invalidPathOrCancel = TRUE;
    if (mode == 1)
        return TRUE; // Sally passes the current path of this FS: it is the proposed target as it is
    if (mode != 2 && mode != 3 || sourcePath == nullptr || next == nullptr)
        return FALSE;

    std::wstring userPart;
    CSftpPath wanted;
    if (!SplitFsPath(targetPath, fsName != nullptr ? fsName : AssignedFSName, userPart) ||
        !ParseSftpPath(userPart, wanted))
    {
        ShowMessage(SPLFormatStringOwned(LangStr(IDS_ERR_BAD_PATH).c_str(), targetPath.c_str()));
        return FALSE; // the user corrects the path
    }
    if (connected_ && !SameServer(identity_, wanted))
    {
        if (invalidPathOrCancel != nullptr)
            *invalidPathOrCancel = FALSE;
        return FALSE; // another FS (or a new one) must take this
    }

    // the selection: the roots only, the workers walk the directories
    std::vector<CLocalSource> sources;
    std::wstring base = sourcePath;
    SPLSalPathAddBackslashOwned(base);
    const wchar_t* name = nullptr;
    const wchar_t* dosName = nullptr;
    BOOL isDir = FALSE;
    CQuadWord size(0, 0);
    DWORD attributes = 0;
    FILETIME lastWrite = {0, 0};
    int enumError = SALENUM_SUCCESS;
    while ((name = next(parent, 0, &dosName, &isDir, &size, &attributes, &lastWrite, nextParam, &enumError)) != nullptr)
    {
        CLocalSource source;
        source.path = base + name;
        source.isDir = isDir != FALSE;
        source.size = isDir ? 0 : size.Value;
        source.mtime = FileTimeToUnixTime(lastWrite);
        sources.push_back(std::move(source));
    }
    if (enumError == SALENUM_CANCEL || sources.empty())
        return TRUE; // cancelled (invalidPathOrCancel stays TRUE: nothing is deselected)

    CPanelConnectParams params;
    if (!MakeWorkerParams(wanted, params))
        return TRUE; // cancelled or failed, the message was shown

    const CNameCodec codec = params.site.MakeCodec();
    COperationSettings settings = SettingsFromSite(params.site, TransferDirection::Upload, copy == FALSE);
    // policy stays Ask (D37): Sally's own Copy dialog has no "file exists" choice
    settings.localRoot = sourcePath;
    settings.remoteRoot = (connected_ && wanted.path == DisplayPath(curRaw_)) ? curRaw_
                                                                              : NormalizeRemotePath(codec.Encode(wanted.path));

    COperationLaunch launch;
    launch.source = sourcePath;
    launch.target = AssignedFSName + L":" + FormatSftpPath(wanted);
    launch.hostName = HostTextOf(params.site);
    launch.changedPaths.push_back(launch.target);
    if (!copy)
        launch.changedPaths.push_back(sourcePath); // the moved items are gone from here
    std::vector<CQueueItem> roots = COperation::BuildUploadRoots(sources, settings, codec);
    if (OperationsRegistry.Launch(std::move(settings), std::move(params), std::move(roots), launch) &&
        invalidPathOrCancel != nullptr)
        *invalidPathOrCancel = FALSE; // started: Sally deselects the source
    return TRUE;
}

// ---------------------------------------------------------------------------------------------------------------
// Drag & drop and change notifications

// Dropping on an sftp: path (this FS is the target): only a Move of items of the same server (the drop is then done as
// renames on the server by CopyOrMoveFromFS, mode 5).
void WINAPI
CPluginFSInterface::GetDropEffect(const wchar_t* srcFSPath, const wchar_t* tgtFSPath, DWORD allowedEffects,
                                  DWORD keyState, DWORD* dropEffect)
{
    if (dropEffect == nullptr)
        return;
    DWORD effect = DROPEFFECT_NONE;
    try
    {
        if ((allowedEffects & DROPEFFECT_MOVE) != 0 && IsSameServerFsPath(srcFSPath) && IsSameServerFsPath(tgtFSPath))
            effect = DROPEFFECT_MOVE;
    }
    catch (...)
    {
        TRACE_E("GetDropEffect: unexpected exception.");
    }
    *dropEffect = effect;
}

// While dragging over any file system of this process ('tgtFSPath' is set): this FS can only move to its own server.
void WINAPI
CPluginFSInterface::GetAllowedDropEffects(int mode, const wchar_t* tgtFSPath, DWORD* allowedEffects)
{
    if (mode != 1 || tgtFSPath == nullptr || allowedEffects == nullptr)
        return;
    try
    {
        *allowedEffects = IsSameServerFsPath(tgtFSPath) ? (*allowedEffects & DROPEFFECT_MOVE) : DROPEFFECT_NONE;
    }
    catch (...)
    {
        TRACE_E("GetAllowedDropEffects: unexpected exception.");
        *allowedEffects = DROPEFFECT_NONE;
    }
}

// 'fsPath' is an "sftp:..." path of the server this FS is connected to.
bool CPluginFSInterface::IsSameServerFsPath(const wchar_t* fsPath) const
{
    std::wstring userPart;
    CSftpPath parsed;
    return connected_ && fsPath != nullptr && SplitFsPath(fsPath, AssignedFSName, userPart) &&
           ParseSftpPath(userPart, parsed) && SameServer(identity_, parsed);
}

// Something changed on 'path' (an FS path with the FS name): refresh the panel when it shows that folder, or (with
// includingSubdirs) a folder below it. Called on the main thread for the panel FS and for detached ones.
void WINAPI
CPluginFSInterface::AcceptChangeOnPathNotification(const wchar_t* fsName, const wchar_t* path, BOOL includingSubdirs)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::AcceptChangeOnPathNotification(, , %d)", includingSubdirs);
    try
    {
        std::wstring userPart;
        CSftpPath changed;
        if (!connected_ || closeRequested_ || path == nullptr ||
            !SplitFsPath(path, fsName != nullptr ? fsName : AssignedFSName, userPart) || !ParseSftpPath(userPart, changed))
            return;
        if (SameServer(identity_, changed) && IsPathWithin(DisplayPath(curRaw_), changed.path, includingSubdirs != FALSE))
            SalamanderGeneral->PostRefreshPanelFS(this);
    }
    catch (...)
    {
        TRACE_E("AcceptChangeOnPathNotification: unexpected exception.");
    }
}
