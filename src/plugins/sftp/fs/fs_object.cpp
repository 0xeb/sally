// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "fs_object.h"

#include "cancel_policy.h"
#include "fs_data.h"
#include "fs_messages.h"
#include "listing_format.h"
#include "../ui/prompt_dialog.h"
#include "site_config.h"
#include "text_util.h"
#include "wait_for_request.h"

namespace
{
bool WriteBuffer(CSalamanderStringBuffer* buffer, const std::wstring& text)
{
    return buffer != nullptr && sally::plugin_abi::WriteStringBuffer(*buffer, text);
}

std::string ToUtf8Path(const std::wstring& path)
{
    return CNameCodec::Utf8().Encode(path);
}

// Whether the user is told about a path problem while the path is shortened (the rules of the "mode" parameter of
// ChangePath: mode 1 = refresh, does not report a path that no longer exists).
bool ShouldReport(int mode, SshErrorKind kind)
{
    if (kind == SshErrorKind::NotFound)
        return mode != 1;
    return true;
}
} // namespace

CPluginFSInterface::CPluginFSInterface()
    : hosts_(ToUtf8Path(KnownHostsOwnPath()), ToUtf8Path(KnownHostsUserPath()))
{
    EnsureDirectory(AppDataSallyDir()); // the own known_hosts file is created in it on the first accepted key
    session_.reset(new CPanelSession(&hosts_));
}

CPluginFSInterface::~CPluginFSInterface()
{
    CloseSession(); // joins the session thread before hosts_ goes away
    session_.reset();
}

void CPluginFSInterface::CloseSession()
{
    pendingListing_.reset();
    cacheEntries_.reset();
    cacheRaw_.clear();
    reestablish_ = false;
    if (session_)
        session_->Close();
}

bool CPluginFSInterface::CloseOrDefer()
{
    if (busy_ > 0)
    {
        closeRequested_ = true;
        if (session_)
            session_->Cancel(); // the wait ends; no join here (we are inside the wait)
        return false;
    }
    delete this;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Helpers

std::wstring CPluginFSInterface::DisplayPath(const std::string& rawPath) const
{
    return codec_.Decode(NormalizeRemotePath(rawPath));
}

std::wstring CPluginFSInterface::UserPartOf(const std::string& rawPath) const
{
    CSftpPath p = identity_;
    p.path = DisplayPath(rawPath);
    return FormatSftpPath(p);
}

std::wstring CPluginFSInterface::ConnectionText() const
{
    if (identity_.isSite)
        return L"@" + identity_.siteName;
    return identity_.user + L"@" + identity_.host;
}

void CPluginFSInterface::ShowError(const std::wstring& text) const
{
    SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(), text.c_str(),
                                     LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
}

// ---------------------------------------------------------------------------------------------------------------
// Paths

BOOL WINAPI
CPluginFSInterface::GetCurrentPath(CSalamanderStringBuffer* userPart)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::GetCurrentPath()");
    try
    {
        return WriteBuffer(userPart, connected_ ? UserPartOf(curRaw_) : std::wstring());
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL WINAPI
CPluginFSInterface::GetFullName(CFileData& file, int isDir, CSalamanderStringBuffer* fullName)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::GetFullName(, %d,)", isDir);
    try
    {
        if (!connected_)
            return FALSE;
        if (isDir == 2) // up-dir: the parent
            return WriteBuffer(fullName, UserPartOf(ParentRemotePath(curRaw_)));
        const CSftpFileData* data = reinterpret_cast<const CSftpFileData*>(file.PluginData);
        const std::string raw = data != nullptr ? data->rawName : codec_.Encode(file.Name);
        return WriteBuffer(fullName, UserPartOf(JoinRemotePath(curRaw_, raw)));
    }
    catch (...)
    {
        return FALSE;
    }
}

// A relative path typed by the user (Shift+F7 with "subdir" or ".."): make it absolute on this server.
BOOL WINAPI
CPluginFSInterface::GetFullFSPath(HWND parent, const wchar_t* fsName, CSalamanderStringBuffer* path, BOOL& success)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::GetFullFSPath()");
    try
    {
        if (!connected_)
            return FALSE;
        std::wstring relative;
        if (path == nullptr || !sally::plugin_abi::ReadStringBuffer(*path, relative))
        {
            success = FALSE;
            return FALSE;
        }
        const std::string absolute = ResolveRemotePath(curRaw_, codec_.Encode(relative));
        success = WriteBuffer(path, std::wstring(fsName != nullptr ? fsName : L"") + L":" + UserPartOf(absolute));
        return TRUE;
    }
    catch (...)
    {
        success = FALSE;
        return FALSE;
    }
}

BOOL WINAPI
CPluginFSInterface::GetRootPath(CSalamanderStringBuffer* userPart)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::GetRootPath()");
    try
    {
        return WriteBuffer(userPart, connected_ ? UserPartOf("/") : std::wstring());
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL WINAPI
CPluginFSInterface::IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::IsCurrentPath(, ,)");
    try
    {
        CSftpPath p;
        if (!connected_ || userPart == nullptr || !ParseSftpPath(userPart, p))
            return FALSE;
        return currentFSNameIndex == fsNameIndex && SameServer(identity_, p) && p.path == DisplayPath(curRaw_);
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL WINAPI
CPluginFSInterface::IsOurPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::IsOurPath(, ,)");
    try
    {
        CSftpPath p;
        if (userPart == nullptr || !ParseSftpPath(userPart, p))
            return FALSE;
        // A new object that is not connected yet takes any path (that is how an FS is opened); an open one only
        // paths on its own server.
        return !connected_ || SameServer(identity_, p);
    }
    catch (...)
    {
        return FALSE;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Connecting and listing (every wait goes through WaitForRequest)

// The site the path names (a saved site, or user@host[:port] with defaults), its remembered secrets, and a password typed
// by the user when nothing else can log in. Shows its own messages. False: failed or cancelled (silent when cancelled).
bool CPluginFSInterface::ResolveSite(const CSftpPath& wanted, CSite& site, CSecret& password, CSecret& passphrase)
{
    HWND parent = SalamanderGeneral->GetMsgBoxParent();

    if (wanted.isSite)
    {
        const CSite* saved = SiteConfig.Sites().Find(wanted.siteName);
        if (saved == nullptr)
        {
            ShowError(SPLFormatStringOwned(LangStr(IDS_ERR_SITE_UNKNOWN).c_str(), wanted.siteName.c_str()));
            return false;
        }
        site = *saved;
    }
    else // quick connect: user@host[:port], everything else at its default
    {
        site = CSite();
        site.user = wanted.user;
        site.host = wanted.host;
        site.port = wanted.port;
    }

    if (wanted.isSite && site.rememberPassword)
    {
        // main thread: the password manager may ask for the master password
        SiteConfig.GetPassword(site.name, password, parent);
        if (!site.keyFile.empty())
            SiteConfig.GetPassphrase(site.name, passphrase, parent);
    }

    // The session can only ask for a password through keyboard-interactive; plain password login needs it up front.
    if (SiteNeedsPasswordPrompt(site, !password.empty()))
    {
        CPromptItem item;
        item.text = SPLFormatStringOwned(LangStr(IDS_PASSWORD_PROMPT).c_str(), site.user.c_str(), site.host.c_str());
        item.echo = false;
        CPromptDialog dialog(parent, LangStr(IDS_PASSWORD_TITLE), std::wstring(), {item});
        std::vector<CSecret> answers;
        if (!dialog.Run(answers) || answers.size() != 1)
            return false; // cancelled
        password = std::move(answers[0]);
    }
    return true;
}

// Resolves the site, gets the secrets, asks for the password when nothing else can log in, connects.
// Shows its own messages. False: failed or cancelled (silent when the user cancelled).
bool CPluginFSInterface::OpenConnection(const CSftpPath& wanted)
{
    HWND parent = SalamanderGeneral->GetMsgBoxParent();

    CSite site;
    CSecret password;
    CSecret passphrase;
    if (!ResolveSite(wanted, site, password, passphrase))
        return false;

    site_ = site;
    codec_ = site.MakeCodec();
    identity_ = CSftpPath();
    identity_.isSite = wanted.isSite;
    identity_.siteName = wanted.isSite ? site.name : std::wstring();
    identity_.user = site.user;
    identity_.host = site.host;
    identity_.port = site.port;

    std::shared_ptr<CPanelRequest> request = session_->Connect(site_, std::move(password), std::move(passphrase));
    const WaitResult waited = WaitForRequest(parent, *session_, request,
                                             SPLFormatStringOwned(LangStr(IDS_WAIT_CONNECTING).c_str(), site.host.c_str()));
    if (waited == WaitResult::Cancelled || request->Cancelled() || !request->IsDone() || closeRequested_)
        return false;
    if (!request->Ok())
    {
        ShowError(SPLFormatStringOwned(LangStr(IDS_ERR_CONNECT).c_str(), site.host.c_str(),
                                       DescribeError(request->Error()).c_str()));
        return false;
    }
    connected_ = true;
    return true;
}

CPluginFSInterface::CListResult CPluginFSInterface::RunList(const std::string& rawPath)
{
    CListResult result;
    std::shared_ptr<CPanelRequest> request = session_->List(rawPath, true); // true: also the free space
    const WaitResult waited = WaitForRequest(SalamanderGeneral->GetMsgBoxParent(), *session_, request,
                                             LangStr(IDS_WAIT_LISTING));
    if (waited == WaitResult::Cancelled || request->Cancelled() || !request->IsDone() || closeRequested_)
    {
        result.outcome = ListOutcome::Cancelled;
        return result;
    }
    if (request->Ok())
    {
        result.outcome = ListOutcome::Ok;
        result.entries = request->Entries();
        result.resolvedPath = request->ResolvedPath();
        if (request->HasVfs())
            StoreFreeSpace(request->Vfs());
        else
            freeKnown_ = false; // the number of the previous folder must not stay
        return result;
    }
    result.error = request->Error();
    result.outcome = IsPathLevelError(result.error.kind) ? ListOutcome::PathError : ListOutcome::ConnectionError;
    return result;
}

// True when the path exists and is not a directory (used to tell "a file was typed" from "no such path").
bool CPluginFSInterface::IsFile(const std::string& rawPath)
{
    std::shared_ptr<CPanelRequest> request = session_->Stat(rawPath);
    const WaitResult waited = WaitForRequest(SalamanderGeneral->GetMsgBoxParent(), *session_, request,
                                             LangStr(IDS_WAIT_LISTING));
    if (waited == WaitResult::Cancelled || !request->IsDone() || !request->Ok() || closeRequested_)
        return false;
    return !request->Attributes().isDir;
}

BOOL WINAPI
CPluginFSInterface::ChangePath(int currentFSNameIndex, CSalamanderStringBuffer* fsName, int fsNameIndex,
                               const wchar_t* userPart, CSalamanderStringBuffer* cutFileName, BOOL* pathWasCut,
                               BOOL forceRefresh, int mode)
{
    CALL_STACK_MESSAGE5("CPluginFSInterface::ChangePath(%d, , %d, , , , %d, %d)", currentFSNameIndex, fsNameIndex,
                        forceRefresh, mode);
    CBusyGuard busy(this); // declared before everything else: destroyed last, and may delete 'this'
    try
    {
        if (closeRequested_)
            return FALSE;
        if (fsName == nullptr || !sally::plugin_abi::IsValidStringBuffer(*fsName))
            return FALSE;
        // the FS name stays as Salamander gave it (this plugin has one name)
        if (cutFileName != nullptr && !WriteBuffer(cutFileName, std::wstring()))
            return FALSE;
        if (pathWasCut != nullptr)
            *pathWasCut = FALSE;
        if (mode != 3 && (pathWasCut != nullptr || cutFileName != nullptr))
        {
            TRACE_E("Incorrect value of 'mode' in CPluginFSInterface::ChangePath().");
            mode = 3;
        }
        if (IsWaiting())
        {
            TRACE_E("ChangePath called while another wait is running.");
            return FALSE;
        }
        return DoChangePath(userPart, cutFileName, pathWasCut, mode);
    }
    catch (...)
    {
        TRACE_E("ChangePath: unexpected exception.");
        errorState_ = FsErrorState::Fatal;
        return FALSE;
    }
}

// The path change itself. "path" below is always raw bytes.
BOOL CPluginFSInterface::DoChangePath(const wchar_t* userPart, CSalamanderStringBuffer* cutFileName,
                                      BOOL* pathWasCut, int mode)
{
    const FsErrorState last = errorState_;
    errorState_ = FsErrorState::Ok; // ready for the next call
    if (last == FsErrorState::Fatal)
        return FALSE;

    const bool reestablish = reestablish_; // one shot
    reestablish_ = false;
    pendingListing_.reset();
    if (DecideChangePathEntry(reestablish, HaveCache()) == ChangePathEntry::ServeCachedPath)
    {
        // The ListCurrentPath of the previous call was cancelled: stay on the same path, no network traffic; the
        // ListCurrentPath that follows serves the cached listing.
        hintUserPart_.clear();
        hintRawPath_.clear();
        pendingListing_ = cacheEntries_;
        return TRUE;
    }

    const std::wstring requested = userPart != nullptr ? userPart : L"";
    const bool opening = !connected_; // the first ChangePath of a new object opens the connection
    const bool hinted = !hintUserPart_.empty() && hintUserPart_ == requested;
    const std::string hintedRaw = hintRawPath_;
    hintUserPart_.clear();
    hintRawPath_.clear();

    CSftpPath wanted;
    if (!ParseSftpPath(requested, wanted))
    {
        ShowError(SPLFormatStringOwned(LangStr(IDS_ERR_BAD_PATH).c_str(), requested.c_str()));
        return FALSE;
    }

    std::string path; // the directory we try to list; "" = the login directory
    bool cut = false; // the path was shortened
    if (opening)
    {
        if (!OpenConnection(wanted) || closeRequested_)
            return FALSE;
        if (requested.find(L'/') == std::wstring::npos) // no path typed: the site's initial directory
            path = site_.initialRemotePath.empty() ? std::string()
                                                   : NormalizeRemotePath(codec_.Encode(site_.initialRemotePath));
        else
            path = NormalizeRemotePath(codec_.Encode(wanted.path));
    }
    else if (last == FsErrorState::InaccessiblePath)
    {
        // ListCurrentPath could not list the current path: go to its parent
        if (IsRootRemotePath(curRaw_))
            return FALSE;
        path = ParentRemotePath(curRaw_);
        cut = true;
    }
    else
        path = hinted ? hintedRaw : NormalizeRemotePath(codec_.Encode(wanted.path));

    // Mode 3 (Change Directory) must not shorten a missing path of an open FS (it reports and stays); everything
    // else, including opening, shortens to the nearest directory that can be listed.
    const bool mayShorten = mode != 3 || opening || last == FsErrorState::InaccessiblePath;

    std::wstring pendingMessage; // the first problem; shown once the final path is known
    std::wstring cutName;        // when a file was typed instead of a directory (mode 3)
    bool firstTry = !cut;
    CListResult listing;
    for (;;)
    {
        listing = RunList(path);
        if (closeRequested_)
            return FALSE; // closed meanwhile: nothing more to do here
        if (listing.outcome == ListOutcome::Ok)
            break;

        if (listing.outcome == ListOutcome::Cancelled)
        {
            // ESC. With a listing already loaded the panel stays as it was: the same path is reported (also when we
            // were navigating into another directory) and ListCurrentPath serves the cached listing. Opening: fail.
            // Without a cache (not expected once connected): fall back to the parent without a message.
            const CancelAction action = DecideOnCancel(CancelPoint::InChangePath, opening, HaveCache());
            if (action == CancelAction::ReturnCachedNow)
            {
                pendingListing_ = cacheEntries_; // curRaw_ is unchanged
                return TRUE;
            }
            if (action == CancelAction::FailOpen || !mayShorten || path.empty() || IsRootRemotePath(path))
                return FALSE;
            path = ParentRemotePath(path);
            cut = true;
            firstTry = false;
            continue;
        }

        if (listing.outcome == ListOutcome::ConnectionError)
        {
            cacheEntries_.reset();
            ShowError(SPLFormatStringOwned(LangStr(IDS_ERR_LIST).c_str(), DisplayPath(path.empty() ? "/" : path).c_str(),
                                           DescribeError(listing.error).c_str()));
            return FALSE;
        }

        // PathError: a file instead of a directory?
        if (firstTry && !path.empty() && !cut && IsFile(path))
        {
            cutName = codec_.Decode(LastRemoteComponent(path));
            if (mode != 3 && pendingMessage.empty()) // modes 1 and 2 tell the user, mode 3 just focuses the file
                pendingMessage = SPLFormatStringOwned(LangStr(IDS_ERR_FILE_NOT_DIR).c_str(), DisplayPath(path).c_str());
            path = ParentRemotePath(path);
            cut = true;
            firstTry = false;
            continue;
        }
        firstTry = false;

        const std::wstring message = SPLFormatStringOwned(LangStr(IDS_ERR_LIST).c_str(),
                                                          DisplayPath(path.empty() ? "/" : path).c_str(),
                                                          DescribeError(listing.error).c_str());
        if (!mayShorten || path.empty() || IsRootRemotePath(path))
        {
            ShowError(pendingMessage.empty() ? message : pendingMessage); // nothing left to try
            return FALSE;
        }
        if (pendingMessage.empty() && ShouldReport(mode, listing.error.kind))
            pendingMessage = message;
        path = ParentRemotePath(path);
        cut = true;
        cutName.clear(); // a name is only returned for a file that was typed (set again then)
    }

    // success: remember the listing for ListCurrentPath. For the login directory the server tells the real path.
    curRaw_ = path.empty() ? NormalizeRemotePath(listing.resolvedPath) : path;
    pendingListing_ = std::make_shared<const std::vector<CSftpEntry>>(std::move(listing.entries));
    cacheEntries_ = pendingListing_;
    cacheRaw_ = curRaw_;
    if (pathWasCut != nullptr)
        *pathWasCut = cut ? TRUE : FALSE;
    if (cutFileName != nullptr && !cutName.empty())
        WriteBuffer(cutFileName, cutName);
    if (!pendingMessage.empty())
        ShowError(pendingMessage);
    return TRUE;
}

// ---------------------------------------------------------------------------------------------------------------
// Listing

BOOL WINAPI
CPluginFSInterface::ListCurrentPath(CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData,
                                    int& iconsType, BOOL forceRefresh)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::ListCurrentPath(, , , %d)", forceRefresh);
    CBusyGuard busy(this); // see ChangePath
    try
    {
        if (closeRequested_)
            return FALSE;
        if (IsWaiting())
        {
            TRACE_E("ListCurrentPath called while another wait is running.");
            return FALSE;
        }
        return DoListCurrentPath(dir, pluginData, iconsType);
    }
    catch (...)
    {
        TRACE_E("ListCurrentPath: unexpected exception.");
        errorState_ = FsErrorState::Fatal;
        return FALSE;
    }
}

BOOL CPluginFSInterface::DoListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                           CPluginDataInterfaceAbstract*& pluginData, int& iconsType)
{
    ListingPtr entries;
    if (pendingListing_ != nullptr)
    {
        // ChangePath listed the directory moments ago (also for a hard refresh), or restored the cached listing
        // after ESC: use that, once
        entries = std::move(pendingListing_);
        pendingListing_.reset();
    }
    else
    {
        CListResult listing = RunList(curRaw_);
        switch (listing.outcome)
        {
        case ListOutcome::Ok:
            entries = std::make_shared<const std::vector<CSftpEntry>>(std::move(listing.entries));
            cacheEntries_ = entries;
            cacheRaw_ = curRaw_;
            break;
        case ListOutcome::Cancelled: // ESC: Salamander calls ChangePath next
            if (DecideOnCancel(CancelPoint::InListCurrentPath, false, HaveCache()) ==
                CancelAction::ReturnFalseThenCache)
                reestablish_ = true; // that ChangePath keeps this path and ListCurrentPath serves the cache
            else
                errorState_ = FsErrorState::InaccessiblePath; // no cache: it goes to the parent
            return FALSE;
        case ListOutcome::PathError:
            ShowError(SPLFormatStringOwned(LangStr(IDS_ERR_LIST).c_str(), DisplayPath(curRaw_).c_str(),
                                           DescribeError(listing.error).c_str()));
            errorState_ = FsErrorState::InaccessiblePath;
            return FALSE;
        default: // the connection is broken
            cacheEntries_.reset();
            ShowError(SPLFormatStringOwned(LangStr(IDS_ERR_LIST).c_str(), DisplayPath(curRaw_).c_str(),
                                           DescribeError(listing.error).c_str()));
            errorState_ = FsErrorState::Fatal;
            return FALSE;
        }
    }

    CPluginDataInterface* data = new CPluginDataInterface;
    bool ok = false;
    try
    {
        ok = AddListing(dir, data, *entries);
    }
    catch (...)
    {
        TRACE_E("ListCurrentPath: out of memory while building the listing.");
    }
    if (!ok)
    {
        dir->Clear(data); // frees what was added so far (also the per-item data)
        delete data;
        errorState_ = FsErrorState::Fatal;
        return FALSE;
    }
    pluginData = data;
    iconsType = pitSimple;
    return TRUE;
}

// Fills the Salamander directory object. On false the caller clears 'dir' and deletes 'data'.
bool CPluginFSInterface::AddListing(CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract* data,
                                    const std::vector<CSftpEntry>& entries)
{
    dir->SetFlags(SALDIRFLAG_CASESENSITIVE | SALDIRFLAG_IGNOREDUPDIRS);
    dir->SetValidData(VALID_DATA_EXTENSION | VALID_DATA_SIZE | VALID_DATA_TYPE | VALID_DATA_DATE | VALID_DATA_TIME |
                      VALID_DATA_ATTRIBUTES | VALID_DATA_HIDDEN | VALID_DATA_ISLINK);

    int sortByExtDirsAsFiles = 0;
    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                          sizeof(sortByExtDirsAsFiles), nullptr);

    int dirCount = 0;
    for (const CSftpEntry& e : entries)
        if (EntryIsDirectory(e))
            dirCount++;
    dir->SetApproximateCount(static_cast<int>(entries.size()) - dirCount, dirCount + 1);

    const CNameCodec utf8 = CNameCodec::Utf8();

    if (!IsRootRemotePath(curRaw_)) // the up-dir item
    {
        CFileData up;
        memset(&up, 0, sizeof(up));
        up.Name = SalamanderGeneral->DupStr(L"..");
        if (up.Name == nullptr)
            return false;
        up.NameLen = 2;
        up.Ext = up.Name + up.NameLen;
        up.Attr = FILE_ATTRIBUTE_DIRECTORY;
        up.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
        if (!dir->AddDir(nullptr, up, data))
        {
            SalamanderGeneral->Free(up.Name);
            return false;
        }
    }

    for (const CSftpEntry& e : entries)
    {
        if (!IsValidRemoteEntryName(e.rawName)) // a hostile server could send "../x" or similar
            continue;

        const bool isDir = EntryIsDirectory(e);
        const std::wstring name = codec_.Decode(e.rawName);

        std::unique_ptr<CSftpFileData> pluginItem(new CSftpFileData);
        pluginItem->rawName = e.rawName;
        pluginItem->mode = e.permissions;
        pluginItem->modeKnown = e.hasPermissions;
        pluginItem->permissions = FormatPermissions(e.permissions, e.isDir, e.isLink);
        pluginItem->owner = utf8.Decode(e.owner);
        pluginItem->group = utf8.Decode(e.group);

        CFileData file;
        memset(&file, 0, sizeof(file));
        file.Name = SalamanderGeneral->DupStr(name.c_str());
        if (file.Name == nullptr)
            return false;
        file.NameLen = static_cast<DWORD>(name.size());
        file.Ext = (isDir && !sortByExtDirsAsFiles) ? file.Name + file.NameLen : file.Name + ExtensionOffset(name);
        file.Size = isDir ? CQuadWord(0, 0)
                          : CQuadWord(static_cast<DWORD>(e.size & 0xFFFFFFFFULL), static_cast<DWORD>(e.size >> 32));
        file.Attr = EntryAttributes(e);
        file.LastWrite = UnixTimeToFileTime(e.mtime);
        file.Hidden = IsHiddenName(e.rawName) ? 1 : 0;
        file.IsLink = e.isLink ? 1 : 0;
        file.IsOffline = 0;
        file.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
        file.PluginData = reinterpret_cast<DWORD_PTR>(pluginItem.get());

        const BOOL added = isDir ? dir->AddDir(nullptr, file, data) : dir->AddFile(nullptr, file, data);
        if (!added)
        {
            SalamanderGeneral->Free(file.Name);
            return false; // pluginItem is deleted here, Salamander never saw it
        }
        pluginItem.release(); // owned by the directory now (released through ReleasePluginData)
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Navigation from the panel (called by CPluginInterfaceForFS::ExecuteOnFS)

bool CPluginFSInterface::BuildNavigation(const CFileData& file, int isDir, std::wstring& userPart,
                                         std::wstring& focusName)
{
    try
    {
        if (!connected_)
            return false;
        std::string target;
        focusName.clear();
        if (isDir == 2) // up-dir: parent; focus the directory we come from
        {
            target = ParentRemotePath(curRaw_);
            focusName = codec_.Decode(LastRemoteComponent(curRaw_));
        }
        else
        {
            const CSftpFileData* data = reinterpret_cast<const CSftpFileData*>(file.PluginData);
            target = JoinRemotePath(curRaw_, data != nullptr ? data->rawName : codec_.Encode(file.Name));
        }
        userPart = UserPartOf(target);
        hintUserPart_ = userPart; // ChangePath gets exactly this text back and then uses the raw bytes
        hintRawPath_ = target;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Closing, detaching, events

BOOL WINAPI
CPluginFSInterface::TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
{
    CALL_STACK_MESSAGE4("CPluginFSInterface::TryCloseOrDetach(%d, %d, , %d)", forceClose, canDetach, reason);
    detach = FALSE;
    try
    {
        BOOL ret = TRUE;
        // Ask only when the user changes away from a live connection and nothing forces or explains the close.
        const bool mayAsk = !disconnectRequested_ && !forceClose && connected_ &&
                            reason == FSTRYCLOSE_CHANGEPATH && canDetach && !SalamanderGeneral->IsCriticalShutdown();
        if (mayAsk)
        {
            const std::wstring text = SPLFormatStringOwned(LangStr(IDS_DETACH_PROMPT).c_str(), ConnectionText().c_str());
            const std::wstring caption = LangStr(IDS_PLUGINNAME);
            const std::wstring aliases = SPLFormatStringOwned(L"%d\t%s\t%d\t%s", DIALOG_YES,
                                                              LangStr(IDS_BTN_DISCONNECT).c_str(), DIALOG_NO,
                                                              LangStr(IDS_BTN_KEEP_CONNECTION).c_str());
            MSGBOXEX_PARAMS params;
            memset(&params, 0, sizeof(params));
            params.HParent = SalamanderGeneral->GetMsgBoxParent();
            params.Flags = MSGBOXEX_YESNOCANCEL | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT | MSGBOXEX_HINT;
            params.Caption = caption.c_str();
            params.Text = text.c_str();
            params.AliasBtnNames = aliases.c_str();
            const int res = SalamanderGeneral->SalMessageBoxEx(&params);
            UpdateWindow(SalamanderGeneral->GetMainWindowHWND()); // do not leave the dialog's remains on screen
            detach = (res == IDNO) ? TRUE : FALSE; // "No" = keep the connection
            ret = (res != IDCANCEL) ? TRUE : FALSE;
        }
        if (ret && !detach && session_)
            session_->Cancel(); // closing: nothing may still be running (the thread is joined in ReleaseObject)
        return ret;
    }
    catch (...)
    {
        detach = FALSE;
        return TRUE; // closing is always safe
    }
}

void WINAPI
CPluginFSInterface::Event(int event, DWORD param)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::Event(%d,)", event);
    switch (event)
    {
    case FSE_PATHCHANGED:
        UpdateSecurityIcon(static_cast<int>(param));
        break;
    case FSE_DETACHED:
        detached_ = true;
        break;
    case FSE_ATTACHED:
    case FSE_OPENED:
        detached_ = false;
        break;
    default: // FSE_CLOSEORDETACHCANCELED, FSE_TIMER: nothing to do yet
        break;
    }
    // The panel is shown (again) or may be refreshed: a listing read earlier must not be used for a later,
    // non-forced ListCurrentPath. (ChangePath and its ListCurrentPath come back to back, before these events.)
    if (event == FSE_PATHCHANGED || event == FSE_ACTIVATEREFRESH || event == FSE_OPENED || event == FSE_ATTACHED)
    {
        pendingListing_.reset();
        reestablish_ = false;
    }
}

void WINAPI
CPluginFSInterface::ReleaseObject(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::ReleaseObject()");
    if (busy_ > 0)
    {
        // A wait is running up the stack: cancel only. CloseFS follows and defers the delete (CloseOrDefer); the thread
        // is joined by the destructor.
        if (session_)
            session_->Cancel();
        return;
    }
    CloseSession();
}

// ---------------------------------------------------------------------------------------------------------------
// Services

DWORD WINAPI
CPluginFSInterface::GetSupportedServices()
{
    return FS_SERVICE_GETFREESPACE | FS_SERVICE_SHOWINFO | FS_SERVICE_SHOWSECURITYINFO | FS_SERVICE_SHOWPROPERTIES |
           FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM | FS_SERVICE_GETFSICON | FS_SERVICE_GETPATHFORMAINWNDTITLE |
           FS_SERVICE_COPYFROMFS | FS_SERVICE_MOVEFROMFS | FS_SERVICE_COPYFROMDISKTOFS |
           FS_SERVICE_MOVEFROMDISKTOFS | FS_SERVICE_ACCEPTSCHANGENOTIF |
           FS_SERVICE_QUICKRENAME | FS_SERVICE_CREATEDIR | FS_SERVICE_DELETE | FS_SERVICE_CHANGEATTRS |
           FS_SERVICE_VIEWFILE | FS_SERVICE_COMMANDLINE |
           FS_SERVICE_GETNEXTDIRLINEHOTPATH | FS_SERVICE_CONTEXTMENU;
}

// Item of this connection in the Change Drive menu and the Disconnect dialog: "\t" + "sftp:user@host/path"
// (the same shape as the FTP plugin's item; the first column, the hotkey, stays empty).
BOOL WINAPI
CPluginFSInterface::GetChangeDriveOrDisconnectItem(const wchar_t* fsName, wchar_t*& title, HICON& icon,
                                                   BOOL& destroyIcon)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::GetChangeDriveOrDisconnectItem()");
    try
    {
        if (!connected_)
            return FALSE;
        std::wstring text = L"\t";
        text += fsName != nullptr ? fsName : L"";
        text += L':';
        text += UserPartOf(curRaw_);
        if (!text.empty() && text.back() == L'/')
            text.pop_back(); // the path must look "relative" in the menu
        for (size_t at = 0; (at = text.find(L'&', at)) != std::wstring::npos; at += 2)
            text.insert(at, 1, L'&'); // '&' would be taken as a menu hotkey
        title = SalamanderGeneral->DupStr(text.c_str());
        if (title == nullptr)
            return FALSE;
        icon = GetFSIcon(destroyIcon);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

namespace
{
// A 50% blended copy of a 16x16 icon (through an image list); NULL when something fails (the caller keeps the original).
HICON MakeFadedIcon(HICON icon)
{
    HIMAGELIST list = ImageList_Create(16, 16, ILC_COLOR32 | ILC_MASK, 1, 0);
    if (list == nullptr)
        return nullptr;
    HICON faded = nullptr;
    if (ImageList_AddIcon(list, icon) >= 0)
        faded = ImageList_GetIcon(list, 0, ILD_TRANSPARENT | ILD_BLEND50);
    ImageList_Destroy(list);
    return faded;
}
} // namespace

HICON WINAPI
CPluginFSInterface::GetFSIcon(BOOL& destroyIcon)
{
    // Loaded at 16x16 on purpose (LoadIcon would return 32x32, see the SDK note); Salamander destroys it after use.
    destroyIcon = TRUE;
    HICON icon = static_cast<HICON>(LoadImageW(DLLInstance, MAKEINTRESOURCEW(IDI_SFTP_FS), IMAGE_ICON, 16, 16,
                                               LR_DEFAULTCOLOR));
    if (icon != nullptr && detached_)
    {
        HICON faded = MakeFadedIcon(icon); // a detached connection looks "dimmed"
        if (faded != nullptr)
        {
            DestroyIcon(icon);
            icon = faded;
        }
    }
    return icon;
}

BOOL WINAPI
CPluginFSInterface::GetPathForMainWindowTitle(const wchar_t* fsName, int mode, CSalamanderStringBuffer* buf)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::GetPathForMainWindowTitle(, %d,)", mode);
    try
    {
        if (!connected_)
            return FALSE;
        CSftpPath rootPath = identity_;
        rootPath.path = L"/";
        std::wstring prefix = FormatSftpPath(rootPath);
        prefix.pop_back(); // "user@host" without the root slash
        prefix = std::wstring(fsName != nullptr ? fsName : L"") + L":" + prefix;
        return WriteBuffer(buf, FormatWindowTitlePath(prefix, DisplayPath(curRaw_), mode));
    }
    catch (...)
    {
        return FALSE;
    }
}
