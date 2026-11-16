// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Information services of the SFTP file system: free space in the directory line, the info dialog (Ctrl+F1) and the
// security icon with its dialog. None of them talks to the server: they show what the panel session learned at connect
// time (CPanelSession::Info) and the free space that came with the last listing.
//
// Free space: every listing request also asks the server for the free space of the listed directory (RunList). Sally
// calls GetFSFreeSpace after each listing and refresh of the panel, so the value is read from the cache right then.
// There is no way for a plugin to ask Sally to re-read it later, which is why the value travels with the listing.

#include "precomp.h"

#include "fs_object.h"

#include "../ui/info_dialog.h"

void CPluginFSInterface::StoreFreeSpace(const CSftpVfs& vfs)
{
    freeKnown_ = true;
    freeBytes_ = vfs.bytesAvailableToUser;
    totalBytes_ = vfs.bytesTotal;
}

// Sally shows the value right of the directory line; (-1, -1) hides it. Must not block: cached value only.
void WINAPI
CPluginFSInterface::GetFSFreeSpace(CQuadWord* retValue)
{
    if (retValue == nullptr)
        return;
    if (!freeKnown_)
    {
        *retValue = CQuadWord(0xFFFFFFFF, 0xFFFFFFFF);
        return;
    }
    *retValue = CQuadWord(static_cast<DWORD>(freeBytes_ & 0xFFFFFFFFULL), static_cast<DWORD>(freeBytes_ >> 32));
}

void WINAPI
CPluginFSInterface::ShowInfoDialog(const wchar_t* fsName, HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::ShowInfoDialog()");
    CBusyGuard busy(this); // the dialog is modal: a close request during it must wait until it is gone
    try
    {
        if (closeRequested_ || !connected_ || !session_)
            return;
        CInfoDialogInput input;
        input.connection = ConnectionText();
        input.host = identity_.host;
        input.port = identity_.port;
        input.user = identity_.user;
        input.session = session_->Info();
        input.freeKnown = freeKnown_;
        input.bytesFree = freeBytes_;
        input.totalKnown = freeKnown_;
        input.bytesTotal = totalBytes_;
        ShowServerInfoDialog(parent, input);
    }
    catch (...) // no exception may leave an SDK entry point
    {
        TRACE_E("ShowInfoDialog: unexpected exception.");
    }
}

// Click on the lock icon.
void WINAPI
CPluginFSInterface::ShowSecurityInfo(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::ShowSecurityInfo()");
    CBusyGuard busy(this);
    try
    {
        if (closeRequested_ || !connected_ || !session_)
            return;
        ShowSecurityDialog(parent, ConnectionText(), session_->Info());
    }
    catch (...)
    {
        TRACE_E("ShowSecurityInfo: unexpected exception.");
    }
}

// Shows (or hides) the lock in the directory line of 'panel'. Called when the path changed: Sally hides the icon before
// every path change and refresh, so it has to be shown again each time. Closed lock = the host key is trusted (found in
// a known_hosts file or saved by the user); open lock = accepted for this session only.
void CPluginFSInterface::UpdateSecurityIcon(int panel)
{
    try
    {
        CPanelSessionInfo info;
        if (connected_ && session_)
            info = session_->Info();
        if (!info.valid)
        {
            SalamanderGeneral->ShowSecurityIcon(panel, FALSE, FALSE, nullptr);
            return;
        }
        const bool trusted = IsHostKeyTrusted(info);
        const std::wstring tip = LangStr(trusted ? IDS_SEC_ICON_TRUSTED : IDS_SEC_ICON_UNVERIFIED);
        SalamanderGeneral->ShowSecurityIcon(panel, TRUE, trusted ? TRUE : FALSE, tip.c_str());
    }
    catch (...)
    {
        TRACE_E("UpdateSecurityIcon: unexpected exception.");
    }
}
