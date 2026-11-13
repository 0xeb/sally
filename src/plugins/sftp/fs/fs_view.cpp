// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// F3 (view): the ViewFile service of the SFTP file system.
//
// Sally cannot open a file on a server, so the plugin keeps a local copy in Sally's disk cache and Sally opens the viewer
// on it. The steps (the same as the FTP plugin's):
//  1. AllocFileNameInCache with a unique name. Same name = same file: when the copy is already in the cache it is used
//     at once. The name holds the size and modification time (cache_name.h), so a file that changed on the server is
//     not served from an old copy.
//  2. A copy that is not in the cache yet is downloaded by the panel session (a request, like F2/F7) into the file Sally
//     named. The user waits in the safe wait window; ESC cancels and nothing is opened.
//  3. OpenViewer, and always FreeFileNameInCache as the pair of step 1.
//
// Cache invalidation (not here): see CPluginInterface::AcceptChangeOnPathNotification and Release in sftp.cpp.

#include "precomp.h"

#include "fs_object.h"

#include "cache_name.h"
#include "fs_data.h"
#include "fs_messages.h"
#include "listing_format.h"
#include "progress_format.h"
#include "text_util.h"
#include "wait_for_request.h"

namespace
{
const uint32_t ModeTypeMask = 0170000; // S_IFMT
const uint32_t ModeLink = 0120000;     // S_IFLNK
const uint32_t ModeDir = 0040000;      // S_IFDIR

// The name of the copy in the cache: the file name Sally shows in the viewer title, made valid for Windows.
std::wstring CacheFileName(const std::wstring& displayName)
{
    std::wstring name = displayName;
    if (!SPLSalMakeValidFileNameComponentOwned(SalamanderGeneral, name) || name.empty())
        name = L"file";
    return name;
}

// Calls FreeFileNameInCache when it goes out of scope, with the values set so far. Until told otherwise a new copy is
// not complete and is removed at once (what an exception in the middle of the download needs).
class CCacheNameGuard
{
public:
    CCacheNameGuard(CSalamanderForViewFileOnFSAbstract* salamander, const std::wstring& uniqueName, bool fileExists)
        : salamander_(salamander), uniqueName_(uniqueName), fileExists_(fileExists), removeAsSoonAsPossible_(!fileExists)
    {
    }
    ~CCacheNameGuard()
    {
        salamander_->FreeFileNameInCache(uniqueName_.c_str(), fileExists_ ? TRUE : FALSE, newOk_ ? TRUE : FALSE,
                                         newSize_, viewerLock_, viewerLockOwner_, removeAsSoonAsPossible_ ? TRUE : FALSE);
    }
    CCacheNameGuard(const CCacheNameGuard&) = delete;
    CCacheNameGuard& operator=(const CCacheNameGuard&) = delete;

    void SetNewCopy(bool ok, const CQuadWord& size)
    {
        newOk_ = ok;
        newSize_ = size;
    }
    void SetViewerLock(HANDLE lock, BOOL owner)
    {
        viewerLock_ = lock;
        viewerLockOwner_ = owner;
    }
    void SetRemoveAsSoonAsPossible(bool remove) { removeAsSoonAsPossible_ = remove; }

private:
    CSalamanderForViewFileOnFSAbstract* salamander_;
    std::wstring uniqueName_;
    bool fileExists_;
    bool newOk_ = false;
    CQuadWord newSize_ = CQuadWord(0, 0);
    HANDLE viewerLock_ = nullptr;
    BOOL viewerLockOwner_ = FALSE;
    bool removeAsSoonAsPossible_;
};
} // namespace

void WINAPI
CPluginFSInterface::ViewFile(const wchar_t* fsName, HWND parent, CSalamanderForViewFileOnFSAbstract* salamander,
                             CFileData& file)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::ViewFile(, , ,)");
    CBusyGuard busy(this); // declared first: destroyed last, and may delete 'this' (see ChangePath)
    try
    {
        DoViewFile(parent, salamander, file);
    }
    catch (...) // no exception may leave an SDK entry point
    {
        TRACE_E("ViewFile: unexpected exception.");
    }
}

void CPluginFSInterface::DoViewFile(HWND parent, CSalamanderForViewFileOnFSAbstract* salamander, CFileData& file)
{
    if (salamander == nullptr || closeRequested_ || !connected_ || IsWaiting())
        return;
    const CSftpFileData* data = reinterpret_cast<const CSftpFileData*>(file.PluginData);
    if (data != nullptr && (data->mode & ModeTypeMask) == ModeDir)
        return; // a directory cannot be viewed (Sally does not ask for it)
    const std::string raw = data != nullptr ? data->rawName : codec_.Encode(file.Name);
    const std::string remotePath = JoinRemotePath(curRaw_, raw);
    const int64_t mtime = FileTimeToUnixTime(file.LastWrite);
    const bool isLink = data != nullptr && (data->mode & ModeTypeMask) == ModeLink;

    // Case-sensitive like the server's names. The name is only a key of the cache, never a disk path.
    const std::wstring uniqueName = BuildCacheName(FullFsPath(remotePath), file.Size.Value, mtime);
    const std::wstring displayName = file.Name;
    const std::wstring nameInCache = CacheFileName(displayName);

    BOOL fileExists = FALSE;
    const wchar_t* copyPath =
        salamander->AllocFileNameInCache(parent, uniqueName.c_str(), nameInCache.c_str(), nullptr, fileExists);
    if (copyPath == nullptr)
        return; // Sally has reported the problem; FreeFileNameInCache is not wanted when Alloc failed

    // The pair of AllocFileNameInCache: its destructor runs on every way out, also when an exception is thrown below.
    CCacheNameGuard guard(salamander, uniqueName, fileExists != FALSE);

    CQuadWord newSize(0, 0);
    bool newOk = false; // a copy made now is complete
    if (!fileExists)
        newOk = DownloadCopy(parent, remotePath, copyPath, displayName, site_.preserveTime ? mtime : 0, newSize);
    guard.SetNewCopy(newOk, newSize);

    if (fileExists || newOk)
    {
        HANDLE viewerLock = nullptr;
        BOOL viewerLockOwner = FALSE;
        if (salamander->OpenViewer(parent, copyPath, &viewerLock, &viewerLockOwner))
            guard.SetViewerLock(viewerLock, viewerLockOwner);
        // else: the viewer did not open: no binding to the cache
    }

    // A failed copy is removed at once. The size and time of a link are those of the link, not of its target, so a
    // link's copy is kept only while its viewer is open.
    guard.SetRemoveAsSoonAsPossible(!fileExists && (!newOk || isLink));
}

// Downloads the remote file into 'copyPath' on the panel session and waits for it. True: the complete file is there.
// ESC (or a closed FS): false, silently, and the session leaves no file behind. An error: a message and false.
bool CPluginFSInterface::DownloadCopy(HWND parent, const std::string& remotePath, const wchar_t* copyPath,
                                      const std::wstring& displayName, int64_t mtime, CQuadWord& size)
{
    std::shared_ptr<CPanelRequest> request = session_->DownloadToFile(remotePath, copyPath, mtime);
    // The wait window is sized from its FIRST text and left-aligns every line, so the first text already contains an
    // empty bar line: the window is as wide as the bar and the bar fills it.
    const int BarCells = 36;
    const std::wstring startText = SPLFormatStringOwned(LangStr(IDS_WAIT_VIEWING).c_str(), displayName.c_str()) +
                                   L"\n" + FormatTextBar(0, 1, BarCells);
    // runs on this thread about ten times a second: "Downloading x ... 1.5 MB of 12 MB" once the
    // size is known, then a thin text bar under it (Sally's wait window can only show text)
    const auto textNow = [&]() -> std::wstring {
        const uint64_t total = request->BytesTotal();
        if (total == 0)
            return startText;
        return SPLFormatStringOwned(LangStr(IDS_WAIT_VIEWING_PROGRESS).c_str(), displayName.c_str(),
                                    FormatBytes(request->BytesDone()).c_str(), FormatBytes(total).c_str()) +
               L"\n" + FormatTextBar(request->BytesDone(), total, BarCells);
    };
    const WaitResult waited = WaitForRequest(parent, *session_, request, startText, textNow);
    if (waited != WaitResult::Done || request->Cancelled() || !request->IsDone() || closeRequested_)
        return false;
    if (!request->Ok())
    {
        ShowMessage(parent, SPLFormatStringOwned(LangStr(IDS_ERR_VIEW).c_str(), displayName.c_str(),
                                                 DescribeError(request->Error()).c_str()));
        return false;
    }
    const uint64_t done = request->BytesDone();
    size = CQuadWord(static_cast<DWORD>(done & 0xFFFFFFFF), static_cast<DWORD>(done >> 32));
    return true;
}
