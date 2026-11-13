// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// F2 (quick rename) and F7 (create directory): the two FS services that change names in the panel's folder. Both run
// as one request on the panel session thread and wait for it in the safe wait window (ESC cancels), like MoveOnServer.
//
// Sally's protocol (mode): 1 = "show your dialog": we return FALSE with cancel FALSE and Sally shows its standard
// dialog; 2 = Sally passes the typed name in newName and we do the work. Result in mode 2: TRUE = done (newName = the
// item to focus); FALSE with cancel FALSE = error, Sally reopens the dialog with the name in newName; FALSE with
// cancel TRUE = the user gave up.

#include "precomp.h"

#include "fs_object.h"

#include "fs_data.h"
#include "fs_messages.h"
#include "listing_format.h"
#include "new_name.h"
#include "text_util.h"
#include "wait_for_request.h"

namespace
{
// Reads the name Sally passes in 'newName'. False when the buffer cannot be read.
bool ReadNewName(CSalamanderStringBuffer* buffer, std::wstring& name)
{
    return buffer != nullptr && sally::plugin_abi::ReadStringBuffer(*buffer, name);
}

bool WriteNewName(CSalamanderStringBuffer* buffer, const std::wstring& name)
{
    return buffer != nullptr && sally::plugin_abi::WriteStringBuffer(*buffer, name);
}
} // namespace

// Shows the message for a name the user must correct. False: the name is fine.
bool CPluginFSInterface::RefuseBadName(HWND parent, const std::wstring& name) const
{
    const NewNameCheck check = CheckNewName(name);
    if (check == NewNameCheck::Ok)
        return false;
    ShowMessage(parent, LangStr(check == NewNameCheck::Empty ? IDS_ERR_NAME_EMPTY : IDS_ERR_NAME_INVALID));
    return true;
}

// Encodes the typed name with the site's encoding. A character the encoding cannot hold would silently become '?' on
// the server, so that is refused with a message instead. False: refused.
bool CPluginFSInterface::EncodeNewName(HWND parent, const std::wstring& name, std::string& raw) const
{
    bool lossy = false;
    raw = codec_.Encode(name, &lossy);
    if (lossy)
    {
        ShowMessage(parent, LangStr(IDS_ERR_NAME_ENCODING));
        return false;
    }
    return true;
}

// Waits for the request in the wait window. True: it finished and the FS is still alive, so the result can be used.
// False: cancelled (ESC), closed meanwhile, or still running (the caller then reports "cancelled").
bool CPluginFSInterface::WaitForNameRequest(HWND parent, const std::shared_ptr<CPanelRequest>& request,
                                            const std::wstring& waitText)
{
    const WaitResult waited = WaitForRequest(parent, *session_, request, waitText);
    return waited == WaitResult::Done && !request->Cancelled() && request->IsDone() && !closeRequested_;
}

BOOL WINAPI
CPluginFSInterface::QuickRename(const wchar_t* fsName, int mode, HWND parent, CFileData& file, BOOL isDir,
                                CSalamanderStringBuffer* newName, BOOL& cancel)
{
    CALL_STACK_MESSAGE3("CPluginFSInterface::QuickRename(, %d, , , %d, ,)", mode, isDir);
    CBusyGuard busy(this); // declared first: destroyed last, and may delete 'this' (see ChangePath)
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // Sally's standard dialog, with the item's name filled in
    try
    {
        return DoQuickRename(parent, file, newName, cancel);
    }
    catch (...)
    {
        TRACE_E("QuickRename: unexpected exception.");
        cancel = TRUE;
        return FALSE;
    }
}

bool CPluginFSInterface::DoQuickRename(HWND parent, CFileData& file, CSalamanderStringBuffer* newName, BOOL& cancel)
{
    cancel = TRUE; // every early exit below that does not set it back is "nothing to correct"
    if (closeRequested_ || !connected_ || IsWaiting())
        return false;
    std::wstring typed;
    if (!ReadNewName(newName, typed))
        return false;
    cancel = FALSE; // from here on an error lets the user correct the name

    if (RefuseBadName(parent, typed))
        return false;
    const CSftpFileData* data = reinterpret_cast<const CSftpFileData*>(file.PluginData);
    const std::string oldRaw = data != nullptr ? data->rawName : codec_.Encode(file.Name);
    std::string newRaw;
    if (!EncodeNewName(parent, typed, newRaw))
        return false;
    if (newRaw == oldRaw)
        return true; // same name: nothing to do (newName already holds it, Sally focuses the item)

    // overwrite=false: an existing target is an error, never silently replaced
    DropListings(curRaw_, true); // also when ESC stops the wait: the server may have renamed it anyway (a folder: below too)
    std::shared_ptr<CPanelRequest> request =
        session_->Rename(JoinRemotePath(curRaw_, oldRaw), JoinRemotePath(curRaw_, newRaw), false);
    if (!WaitForNameRequest(parent, request, LangStr(IDS_WAIT_RENAMING)))
    {
        cancel = TRUE;
        return false;
    }
    if (!request->Ok())
    {
        if (request->Error().kind == SshErrorKind::Exists)
            ShowMessage(parent, SPLFormatStringOwned(LangStr(IDS_ERR_NAME_EXISTS).c_str(), typed.c_str()));
        else
            ShowMessage(parent, SPLFormatStringOwned(LangStr(IDS_ERR_RENAME_ITEM).c_str(),
                                                     codec_.Decode(oldRaw).c_str(), typed.c_str(),
                                                     DescribeError(request->Error()).c_str()));
        return false;
    }
    WriteNewName(newName, typed); // the name to focus after the refresh
    SalamanderGeneral->PostRefreshPanelFS(this);
    NotifyFolderChanged(); // extra: a second panel or detached FS on this folder, and the F3 copies
    return true;
}

BOOL WINAPI
CPluginFSInterface::CreateDir(const wchar_t* fsName, int mode, HWND parent, CSalamanderStringBuffer* newName,
                              BOOL& cancel)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::CreateDir(, %d, , ,)", mode);
    CBusyGuard busy(this); // see QuickRename
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // Sally's standard dialog
    try
    {
        return DoCreateDir(parent, newName, cancel);
    }
    catch (...)
    {
        TRACE_E("CreateDir: unexpected exception.");
        cancel = TRUE;
        return FALSE;
    }
}

bool CPluginFSInterface::DoCreateDir(HWND parent, CSalamanderStringBuffer* newName, BOOL& cancel)
{
    cancel = TRUE;
    if (closeRequested_ || !connected_ || IsWaiting())
        return false;
    std::wstring typed;
    if (!ReadNewName(newName, typed))
        return false;
    cancel = FALSE;

    // v1 creates one directory in the current folder; a sub-path ("a/b") is refused by CheckNewName
    if (RefuseBadName(parent, typed))
        return false;
    std::string raw;
    if (!EncodeNewName(parent, typed, raw))
        return false;

    DropListings(curRaw_, false); // also when ESC stops the wait
    std::shared_ptr<CPanelRequest> request = session_->Mkdir(JoinRemotePath(curRaw_, raw));
    if (!WaitForNameRequest(parent, request, LangStr(IDS_WAIT_CREATING_DIR)))
    {
        cancel = TRUE;
        return false;
    }
    if (!request->Ok())
    {
        if (request->Error().kind == SshErrorKind::Exists)
            ShowMessage(parent, SPLFormatStringOwned(LangStr(IDS_ERR_NAME_EXISTS).c_str(), typed.c_str()));
        else
            ShowMessage(parent, SPLFormatStringOwned(LangStr(IDS_ERR_CREATE_DIR).c_str(), typed.c_str(),
                                                     DescribeError(request->Error()).c_str()));
        return false;
    }
    WriteNewName(newName, typed); // the directory to focus after the refresh
    SalamanderGeneral->PostRefreshPanelFS(this);
    NotifyFolderChanged(); // extra: a second panel or detached FS on this folder
    return true;
}
