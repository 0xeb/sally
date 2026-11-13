// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Ctrl+F2 (change attributes): the ChangeAttributes service of the SFTP file system.
//
// The plugin shows its own dialog (ui/chmod_dialog.*). Then one item without recursion is changed at once with a request
// on the panel session, in the safe wait window (ESC cancels), like F2/F7. Anything else (several items, or a recursive
// change of a directory) becomes a "chmod" operation: workers with their own connections walk the tree and change the
// items, and the operation window shows the progress, so the panel stays usable.
//
// Sally's protocol: TRUE = done, Sally deselects the items; FALSE = cancelled or failed, nothing is deselected.

#include "precomp.h"

#include "fs_object.h"

#include "../ui/chmod_dialog.h"
#include "fs_data.h"
#include "fs_messages.h"
#include "listing_format.h"
#include "operation.h"
#include "operations_registry.h"
#include "site_config.h"
#include "text_util.h"
#include "wait_for_request.h"

namespace
{
// A real directory: a link to a directory is not (chmod on it would change its target, so it is never walked into).
bool IsRealDirectory(bool isDir, bool isLink)
{
    return isDir && !isLink;
}
} // namespace

BOOL WINAPI
CPluginFSInterface::ChangeAttributes(const wchar_t* fsName, HWND parent, int panel, int selectedFiles, int selectedDirs)
{
    CALL_STACK_MESSAGE4("CPluginFSInterface::ChangeAttributes(, , %d, %d, %d)", panel, selectedFiles, selectedDirs);
    CBusyGuard busy(this); // declared first: destroyed last, and may delete 'this' (see ChangePath)
    try
    {
        return DoChangeAttributes(parent, panel, selectedFiles, selectedDirs);
    }
    catch (...) // no exception may leave an SDK entry point
    {
        TRACE_E("ChangeAttributes: unexpected exception.");
        return FALSE;
    }
}

BOOL CPluginFSInterface::DoChangeAttributes(HWND parent, int panel, int selectedFiles, int selectedDirs)
{
    if (closeRequested_ || !connected_ || IsWaiting())
        return FALSE;
    std::vector<CSelectedItem> items;
    if (!CollectSelection(panel, selectedFiles, selectedDirs, items))
        return FALSE;
    if (!ResolveCurrentModes(parent, items))
        return FALSE; // cancelled with ESC while the modes were read

    std::vector<CItemMode> modes;
    bool anyDirectory = false;
    for (const CSelectedItem& item : items)
    {
        CItemMode itemMode;
        itemMode.mode = item.mode;
        itemMode.known = item.modeKnown;
        modes.push_back(itemMode);
        anyDirectory = anyDirectory || IsRealDirectory(item.isDir, item.isLink);
    }
    CChmodDialogInput input;
    input.name = codec_.Decode(items[0].rawName);
    input.itemCount = static_cast<int>(items.size());
    input.anyDirectory = anyDirectory;
    input.state = StateOfItemModes(modes);
    CChmodChoice choice;
    if (!ShowChmodDialog(parent, input, choice))
        return FALSE;
    if (closeRequested_ || !connected_) // the connection can go away while the dialog is open
        return FALSE;

    // one item, nothing to walk through: a plain request. A single directory without recursion is such an item too.
    const bool single = items.size() == 1 && !choice.recursive;
    const bool done = single ? ChmodOneOnSession(parent, items[0], choice.change)
                             : StartChmodOperation(items, choice.change, choice.recursive, choice.applyToFiles,
                                                   choice.applyToDirs);
    return done ? TRUE : FALSE;
}

// The listing has the mode of a link itself (always rwxrwxrwx) and some servers give no mode at all. For those items
// the real mode comes from a Stat request (links followed), so the dialog starts with what chmod would change.
// False: cancelled with ESC (or the FS was closed). A Stat that fails leaves the listed mode.
bool CPluginFSInterface::ResolveCurrentModes(HWND parent, std::vector<CSelectedItem>& items)
{
    for (CSelectedItem& item : items)
    {
        if (!item.isLink && item.modeKnown)
            continue;
        std::shared_ptr<CPanelRequest> request = session_->Stat(JoinRemotePath(curRaw_, item.rawName));
        if (!WaitForNameRequest(parent, request, LangStr(IDS_WAIT_CHMOD)))
            return false;
        // a link whose target is unknown is not "known" any more: the mode of the link itself says nothing
        item.modeKnown = request->Ok() && request->Attributes().hasPermissions;
        item.mode = item.modeKnown ? request->Attributes().permissions : 0;
    }
    return true;
}

// Changes one item on the panel session and waits for it. True: done.
bool CPluginFSInterface::ChmodOneOnSession(HWND parent, const CSelectedItem& item, const CModeChange& change)
{
    CItemMode old;
    old.mode = item.mode;
    old.known = item.modeKnown;
    uint32_t mode = 0;
    if (!ApplyToItemMode(old, change, mode))
    {
        ShowMessage(parent, SPLFormatStringOwned(LangStr(IDS_ERR_CHMOD_UNKNOWN).c_str(), codec_.Decode(item.rawName).c_str()));
        return false;
    }
    if (old.known && mode == PermissionBitsOf(old.mode))
        return true; // nothing would change: nothing is sent
    DropListings(curRaw_, false);
    std::shared_ptr<CPanelRequest> request = session_->Chmod(JoinRemotePath(curRaw_, item.rawName), mode);
    if (!WaitForNameRequest(parent, request, LangStr(IDS_WAIT_CHMOD)))
    {
        NotifyFolderChanged(); // ESC: the server may have done it anyway
        return false;
    }
    if (!request->Ok())
        ShowMessage(parent, SPLFormatStringOwned(LangStr(IDS_ERR_CHMOD_ITEM).c_str(), codec_.Decode(item.rawName).c_str(),
                                                 DescribeError(request->Error()).c_str()));
    NotifyFolderChanged(); // also after a failure: the listing may have changed
    return request->Ok();
}

// Builds the queue from the selection and starts the operation (its window opens). The operation copies what it needs
// from this FS (D35) and never touches it again.
bool CPluginFSInterface::StartChmodOperation(const std::vector<CSelectedItem>& items, const CModeChange& change,
                                             bool recursive, bool applyToFiles, bool applyToDirs)
{
    CPanelConnectParams params;
    if (!MakeWorkerParams(identity_, params))
        return false;
    DropListings(curRaw_, true); // see StartDeleteOperation
    COperationSettings settings = SettingsFromSite(params.site, TransferDirection::Chmod, false);
    settings.remoteRoot = curRaw_;
    settings.modeChange = change;
    settings.chmodRecursive = recursive;
    settings.chmodFiles = applyToFiles;
    settings.chmodDirs = applyToDirs;

    std::vector<CRemoteSource> sources;
    for (const CSelectedItem& item : items)
    {
        CRemoteSource source;
        source.path = JoinRemotePath(curRaw_, item.rawName);
        source.isDir = IsRealDirectory(item.isDir, item.isLink);
        sources.push_back(std::move(source));
    }

    COperationLaunch launch;
    launch.source = FullFsPath(curRaw_);
    launch.hostName = HostTextOf(params.site);
    launch.changedPaths.push_back(launch.source); // the panel refreshes when a run of the operation ends
    std::vector<CQueueItem> roots = COperation::BuildChmodRoots(sources, settings);
    return OperationsRegistry.Launch(std::move(settings), std::move(params), std::move(roots), launch);
}
