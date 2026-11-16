// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Alt+Enter (properties): the ShowProperties service of the SFTP file system.
//
// A modal dialog on Sally's main thread. One item: its attributes are read from the server with panel-session requests
// in the safe wait window (ESC cancels, then no dialog); a symbolic link also gets its target and the target's
// permissions. Several items: the dialog shows what the listing already knows (counts, sum of the file sizes, common
// permissions), so there is no network traffic and nothing is walked into.

#include "precomp.h"

#include "fs_object.h"

#include "../ui/properties_dialog.h"
#include "fs_data.h"
#include "fs_messages.h"
#include "listing_format.h"
#include "text_util.h"
#include "wait_for_request.h"

void WINAPI
CPluginFSInterface::ShowProperties(const wchar_t* fsName, HWND parent, int panel, int selectedFiles, int selectedDirs)
{
    CALL_STACK_MESSAGE4("CPluginFSInterface::ShowProperties(, , %d, %d, %d)", panel, selectedFiles, selectedDirs);
    CBusyGuard busy(this); // declared first: destroyed last, and may delete 'this' (see ChangePath)
    try
    {
        DoShowProperties(parent, panel, selectedFiles, selectedDirs);
    }
    catch (...) // no exception may leave an SDK entry point
    {
        TRACE_E("ShowProperties: unexpected exception.");
    }
}

void CPluginFSInterface::DoShowProperties(HWND parent, int panel, int selectedFiles, int selectedDirs)
{
    if (closeRequested_ || !connected_ || IsWaiting())
        return;
    std::vector<CSelectedItem> items;
    if (!CollectSelection(panel, selectedFiles, selectedDirs, items))
        return;

    CPropertiesInput input;
    input.itemCount = static_cast<int>(items.size());
    input.location = DisplayPath(curRaw_);
    if (items.size() == 1)
    {
        if (!ReadOneItemProperties(parent, items[0], input))
            return; // cancelled or failed (a message was shown)
    }
    else
    {
        std::vector<CSummaryItem> summaryItems;
        for (const CSelectedItem& item : items)
        {
            CSummaryItem s;
            s.isDir = item.isDir;
            s.isLink = item.isLink;
            s.size = item.size;
            s.mode = item.modeKnown ? item.mode : 0; // the summary takes 0 as "not reported"
            summaryItems.push_back(s);
        }
        input.summary = SummarizeSelection(summaryItems);
    }
    if (closeRequested_ || !connected_) // the connection can go away while we waited
        return;
    ShowPropertiesDialog(parent, input);
}

// Fills the one-item part of the input. False: ESC, or the server refused (then a message was shown).
bool CPluginFSInterface::ReadOneItemProperties(HWND parent, const CSelectedItem& item, CPropertiesInput& input)
{
    const std::string path = JoinRemotePath(curRaw_, item.rawName);
    const std::wstring name = codec_.Decode(item.rawName);

    // LStat: the item itself, a link is not followed
    std::shared_ptr<CPanelRequest> request = session_->LStat(path);
    if (!WaitForNameRequest(parent, request, LangStr(IDS_WAIT_PROPERTIES)))
        return false;
    if (!request->Ok())
    {
        ShowMessage(parent, SPLFormatStringOwned(LangStr(IDS_ERR_PROPERTIES).c_str(), name.c_str(),
                                                 DescribeError(request->Error()).c_str()));
        return false;
    }
    const CSftpEntry& attributes = request->Attributes();
    const CNameCodec utf8 = CNameCodec::Utf8();
    input.name = name;
    input.isDir = item.isDir; // for a link: what it points to
    input.isLink = attributes.isLink || item.isLink;
    input.size = attributes.size;
    input.mtime = attributes.mtime;
    input.atime = attributes.atime;
    input.owner = utf8.Decode(attributes.owner);
    input.group = utf8.Decode(attributes.group);
    input.mode = attributes.hasPermissions ? attributes.permissions : 0;
    if (!input.isLink)
        return true;

    // A link: where it points to, and the permissions of the target (the mode of the link itself is always 0777).
    // Both are extras: a broken link or a refusal just leaves them out.
    input.mode = 0;
    std::shared_ptr<CPanelRequest> target = session_->ReadLink(path);
    if (!WaitForNameRequest(parent, target, LangStr(IDS_WAIT_PROPERTIES)))
        return false;
    if (target->Ok())
    {
        input.hasLinkTarget = true;
        input.linkTarget = codec_.Decode(target->LinkTarget());
    }
    std::shared_ptr<CPanelRequest> followed = session_->Stat(path);
    if (!WaitForNameRequest(parent, followed, LangStr(IDS_WAIT_PROPERTIES)))
        return false;
    if (followed->Ok() && followed->Attributes().hasPermissions)
        input.mode = followed->Attributes().permissions;
    return true;
}
