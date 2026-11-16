// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// F8 (delete): the Delete service of the SFTP file system.
//
// One file (or one symbolic link, which is deleted as a link) is removed at once with a request on the panel session,
// in the safe wait window (ESC cancels), like F2/F7. Anything else (a directory, or several items) becomes a "delete"
// operation: workers with their own connections explore the directories and delete from the bottom up, and the
// operation window shows the progress, so the panel stays usable.
//
// Sally's protocol (mode): 1 = "ask the user": we return FALSE with cancelOrError FALSE and Sally shows its standard
// confirmation (only when the user wants one in the configuration), then calls again with mode 2. In mode 2: TRUE with
// cancelOrError FALSE = done, Sally deselects the items; cancelOrError TRUE = cancelled or failed, nothing is deselected.

#include "precomp.h"

#include "fs_object.h"

#include "fs_data.h"
#include "fs_messages.h"
#include "listing_format.h"
#include "operation.h"
#include "operations_registry.h"
#include "site_config.h"
#include "text_util.h"
#include "wait_for_request.h"

BOOL WINAPI
CPluginFSInterface::Delete(const wchar_t* fsName, int mode, HWND parent, int panel, int selectedFiles, int selectedDirs,
                           BOOL& cancelOrError)
{
    CALL_STACK_MESSAGE5("CPluginFSInterface::Delete(, %d, , %d, %d, %d, )", mode, panel, selectedFiles, selectedDirs);
    CBusyGuard busy(this); // declared first: destroyed last, and may delete 'this' (see ChangePath)
    cancelOrError = FALSE;
    if (mode == 1)
        return FALSE; // Sally's standard confirmation
    try
    {
        return DoDelete(parent, panel, selectedFiles, selectedDirs, cancelOrError);
    }
    catch (...) // no exception may leave an SDK entry point
    {
        TRACE_E("Delete: unexpected exception.");
        cancelOrError = TRUE;
        return TRUE;
    }
}

BOOL CPluginFSInterface::DoDelete(HWND parent, int panel, int selectedFiles, int selectedDirs, BOOL& cancelOrError)
{
    cancelOrError = TRUE; // every exit below that does not set it back is "cancelled or failed"
    if (closeRequested_ || !connected_ || IsWaiting())
        return TRUE;
    std::vector<CSelectedItem> items;
    if (!CollectSelection(panel, selectedFiles, selectedDirs, items))
        return TRUE;

    // a link to a directory counts as a file here: it is removed as a link, nothing below it is touched
    const bool single = items.size() == 1 && (!items[0].isDir || items[0].isLink);
    const bool done = single ? DeleteOneOnSession(parent, items[0]) : StartDeleteOperation(items);
    cancelOrError = done ? FALSE : TRUE;
    return TRUE;
}

// Removes one file or link on the panel session and waits for it. True: it is gone (an already missing item counts).
bool CPluginFSInterface::DeleteOneOnSession(HWND parent, const CSelectedItem& item)
{
    DropListings(curRaw_, item.isDir); // also when ESC stops the wait (NotifyFolderChanged below is then not reached)
    std::shared_ptr<CPanelRequest> request = session_->RemoveFile(JoinRemotePath(curRaw_, item.rawName));
    if (!WaitForNameRequest(parent, request, LangStr(IDS_WAIT_DELETING)))
        return false; // cancelled with ESC, or the FS was closed meanwhile
    const bool gone = request->Ok() || request->Error().kind == SshErrorKind::NotFound;
    if (!gone)
        ShowMessage(parent, SPLFormatStringOwned(LangStr(IDS_ERR_DELETE_ITEM).c_str(), codec_.Decode(item.rawName).c_str(),
                                                 DescribeError(request->Error()).c_str()));
    // Also after a failure: the listing may have changed. A link to a directory was shown as a folder: a panel that
    // is inside it (through the link) must refresh too, so the subfolders are included.
    NotifyFolderChanged(item.isDir);
    return gone;
}

// Builds the queue from the selection and starts the operation (its window opens). The operation copies what it needs
// from this FS (D35) and never touches it again.
bool CPluginFSInterface::StartDeleteOperation(const std::vector<CSelectedItem>& items)
{
    CPanelConnectParams params;
    if (!MakeWorkerParams(identity_, params))
        return false;
    DropListings(curRaw_, true); // the operation changes things below the current folder; its end notifies again
    COperationSettings settings = SettingsFromSite(params.site, TransferDirection::Delete, false);
    settings.remoteRoot = curRaw_;

    std::vector<CRemoteSource> sources;
    for (const CSelectedItem& item : items)
    {
        CRemoteSource source;
        source.path = JoinRemotePath(curRaw_, item.rawName);
        source.isDir = item.isDir && !item.isLink; // a link is deleted as a file
        sources.push_back(std::move(source));
    }

    COperationLaunch launch;
    launch.source = FullFsPath(curRaw_);
    launch.hostName = HostTextOf(params.site);
    launch.changedPaths.push_back(launch.source); // the panel refreshes when a run of the operation ends
    std::vector<CQueueItem> roots = COperation::BuildDeleteRoots(sources);
    return OperationsRegistry.Launch(std::move(settings), std::move(params), std::move(roots), launch);
}
