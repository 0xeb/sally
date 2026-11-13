// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "file_mode.h"
#include "idle_policy.h"
#include "known_hosts.h"
#include "listing_cache.h"
#include "op_types.h"
#include "panel_session.h"
#include "site.h"
#include "site_path.h"

struct CPropertiesInput; // ui/properties_dialog.h

// What went wrong with the last path change; decides what the next ChangePath does (same idea as the FTP plugin).
enum class FsErrorState
{
    Ok,
    Fatal,            // the connection is gone: the next ChangePath returns FALSE (Salamander then closes the FS)
    InaccessiblePath  // the current path could not be listed: the next ChangePath shortens it and returns TRUE
};

// One open SFTP connection shown in a panel (or kept detached). Everything here runs on Sally's main thread; the
// network work happens on the session thread inside CPanelSession and is waited for with WaitForRequest.
class CPluginFSInterface : public CPluginFSInterfaceAbstract
{
public:
    CPluginFSInterface();
    ~CPluginFSInterface();

    // ---- paths
    virtual BOOL WINAPI GetCurrentPath(CSalamanderStringBuffer* userPart);
    virtual BOOL WINAPI GetFullName(CFileData& file, int isDir, CSalamanderStringBuffer* fullName);
    virtual BOOL WINAPI GetFullFSPath(HWND parent, const wchar_t* fsName, CSalamanderStringBuffer* path,
                                      BOOL& success);
    virtual BOOL WINAPI GetRootPath(CSalamanderStringBuffer* userPart);
    virtual BOOL WINAPI IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart);
    virtual BOOL WINAPI IsOurPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart);
    virtual BOOL WINAPI ChangePath(int currentFSNameIndex, CSalamanderStringBuffer* fsName, int fsNameIndex,
                                   const wchar_t* userPart, CSalamanderStringBuffer* cutFileName, BOOL* pathWasCut,
                                   BOOL forceRefresh, int mode);
    virtual BOOL WINAPI ListCurrentPath(CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData,
                                        int& iconsType, BOOL forceRefresh);

    // ---- life cycle
    virtual BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason);
    virtual void WINAPI Event(int event, DWORD param);
    virtual void WINAPI ReleaseObject(HWND parent);

    // ---- services and items
    virtual DWORD WINAPI GetSupportedServices();
    virtual BOOL WINAPI GetChangeDriveOrDisconnectItem(const wchar_t* fsName, wchar_t*& title, HICON& icon,
                                                       BOOL& destroyIcon);
    virtual HICON WINAPI GetFSIcon(BOOL& destroyIcon);
    virtual BOOL WINAPI GetPathForMainWindowTitle(const wchar_t* fsName, int mode, CSalamanderStringBuffer* buf);

    // ---- copy and move (fs/fs_transfer.cpp). Downloads and uploads run as operations (see operations_registry.h);
    // an sftp: target of F6 on the same server is a rename on the server.
    virtual BOOL WINAPI CopyOrMoveFromFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent, int panel,
                                         int selectedFiles, int selectedDirs, CSalamanderStringBuffer* targetPath,
                                         BOOL& operationMask, BOOL& cancelOrHandlePath, HWND dropTarget);
    virtual BOOL WINAPI CopyOrMoveFromDiskToFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                               const wchar_t* sourcePath, SalEnumSelection2 next, void* nextParam,
                                               int sourceFiles, int sourceDirs, CSalamanderStringBuffer* targetPath,
                                               BOOL* invalidPathOrCancel);
    virtual void WINAPI GetDropEffect(const wchar_t* srcFSPath, const wchar_t* tgtFSPath, DWORD allowedEffects,
                                      DWORD keyState, DWORD* dropEffect);
    virtual void WINAPI GetAllowedDropEffects(int mode, const wchar_t* tgtFSPath, DWORD* allowedEffects);
    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* fsName, const wchar_t* path,
                                                       BOOL includingSubdirs);

    // ---- information services (fs/fs_info.cpp, fs/fs_properties.cpp)
    virtual void WINAPI GetFSFreeSpace(CQuadWord* retValue);
    virtual void WINAPI ShowInfoDialog(const wchar_t* fsName, HWND parent);
    virtual void WINAPI ShowSecurityInfo(HWND parent);
    virtual void WINAPI ShowProperties(const wchar_t* fsName, HWND parent, int panel, int selectedFiles,
                                       int selectedDirs);

    // ---- directory line hot paths, context menu, Shift+F3 (fs/fs_menu.cpp)
    virtual BOOL WINAPI GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset);
    virtual BOOL WINAPI CompleteDirectoryLineHotPath(CSalamanderStringBuffer* path);
    // ---- F2 and F7 (fs/fs_namespace.cpp)
    virtual BOOL WINAPI QuickRename(const wchar_t* fsName, int mode, HWND parent, CFileData& file, BOOL isDir,
                                    CSalamanderStringBuffer* newName, BOOL& cancel);
    virtual BOOL WINAPI CreateDir(const wchar_t* fsName, int mode, HWND parent, CSalamanderStringBuffer* newName,
                                  BOOL& cancel);
    // ---- F3 (fs/fs_view.cpp)
    virtual void WINAPI ViewFile(const wchar_t* fsName, HWND parent, CSalamanderForViewFileOnFSAbstract* salamander,
                                 CFileData& file);
    // ---- command line under the panels (fs/fs_commandline.cpp)
    virtual BOOL WINAPI ExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command, int& selFrom, int& selTo);
    // ---- F8 (fs/fs_delete.cpp)
    virtual BOOL WINAPI Delete(const wchar_t* fsName, int mode, HWND parent, int panel, int selectedFiles,
                               int selectedDirs, BOOL& cancelOrError);
    // ---- Ctrl+F2 (fs/fs_chmod.cpp)
    virtual BOOL WINAPI ChangeAttributes(const wchar_t* fsName, HWND parent, int panel, int selectedFiles,
                                         int selectedDirs);
    virtual void WINAPI ContextMenu(const wchar_t* fsName, HWND parent, int menuX, int menuY, int type, int panel,
                                    int selectedFiles, int selectedDirs);
    virtual BOOL WINAPI HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) { return FALSE; }
    virtual BOOL WINAPI OpenFindDialog(const wchar_t* fsName, int panel) { return FALSE; }
    virtual void WINAPI OpenActiveFolder(const wchar_t* fsName, HWND parent) {} // Shift+F3: not offered (the FTP plugin has none)
    virtual BOOL WINAPI GetNoItemsInPanelText(CSalamanderStringBuffer* textBuf) { return FALSE; }

    // ---- used by CPluginInterfaceForFS (not part of the SDK interface)

    // Cancels what is running and joins the session thread (idempotent). Called from ReleaseObject, CloseFS and when
    // the plugin unloads.
    void CloseSession();

    // CloseFS: Salamander is done with this object. When a call (ChangePath/ListCurrentPath) is still running on the
    // stack, only cancels it and returns false: the outermost call deletes the object when it has unwound (see
    // CBusyGuard). Otherwise deletes it at once and returns true. The object must not be touched after the call.
    bool CloseOrDefer();

    // Plugin unload: a busy object (a wait is on the stack) is deleted by that call when it unwinds.
    void MarkClosedIfBusy()
    {
        if (busy_ > 0)
            closeRequested_ = true;
    }

    // A call that waits is running (CBusyGuard).
    bool IsBusy() const { return busy_ > 0; }
    bool CloseRequested() const { return closeRequested_; }

    // Closes every detached FS that has been unused for the configured time (detachedIdleMinutes). Runs as the menu
    // command SftpCmdCloseIdleDetached, because CloseDetachedFS must not be called inside an FS method. Only objects that
    // are still registered as detached are touched.
    static void CloseIdleDetached(HWND parent);

    // Id of this connection's session log (see CPanelSession::LogId); 0 when there is none.
    unsigned LogId() const { return session_ != nullptr ? session_->LogId() : 0; }

    // The user asked to disconnect (Disconnect dialog or menu): TryCloseOrDetach must not ask anything.
    void SetDisconnectRequested(bool requested) { disconnectRequested_ = requested; }

    // Path of the directory 'file' (isDir 1) or of the parent (isDir 2) as the user part for
    // ChangePanelPathToPluginFS, plus the name to focus afterwards. Remembers the raw path so that ChangePath uses the
    // exact bytes of names that are not valid in the site's encoding. False when this FS is not connected.
    bool BuildNavigation(const CFileData& file, int isDir, std::wstring& userPart, std::wstring& focusName);

private:
    enum class ListOutcome
    {
        Ok,
        Cancelled,
        PathError,      // this path cannot be listed, a parent may work
        ConnectionError // the connection is broken
    };
    struct CListResult
    {
        ListOutcome outcome = ListOutcome::Cancelled;
        CSshError error;
        std::vector<CSftpEntry> entries;
        std::string resolvedPath;
    };

    // Counts the calls that wait; the last one to leave deletes the object when a close was requested meanwhile.
    class CBusyGuard
    {
    public:
        explicit CBusyGuard(CPluginFSInterface* fs) : fs_(fs) { fs_->busy_++; }
        ~CBusyGuard()
        {
            CPluginFSInterface* fs = fs_;
            if (--fs->busy_ == 0 && fs->closeRequested_)
                delete fs; // the very last thing: the caller has finished with the members
        }
        CBusyGuard(const CBusyGuard&) = delete;
        CBusyGuard& operator=(const CBusyGuard&) = delete;

    private:
        CPluginFSInterface* fs_;
    };

    BOOL DoChangePath(const wchar_t* userPart, CSalamanderStringBuffer* cutFileName, BOOL* pathWasCut, int mode,
                      BOOL forceRefresh);
    BOOL DoListCurrentPath(CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData,
                           int& iconsType, BOOL forceRefresh);

    bool OpenConnection(const CSftpPath& wanted);
    CListResult RunList(const std::string& rawPath);
    bool IsFile(const std::string& rawPath); // exists and is not a directory
    bool AddListing(CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract* data,
                    const std::vector<CSftpEntry>& entries);

    bool ResolveSite(const CSftpPath& wanted, CSite& site, CSecret& password, CSecret& passphrase);

    // ---- fs/fs_transfer.cpp
    struct CSelectedItem // one selected (or focused) panel item
    {
        std::string rawName;
        bool isDir = false;
        uint64_t size = 0;
        int64_t mtime = 0; // Unix seconds
        bool isLink = false; // a symbolic link (isDir then tells what it points to)
        uint32_t mode = 0;   // st_mode bits from the listing (for a link: of the link itself); 0 = not reported
        bool modeKnown = false; // the mode above was reported by the server (or read by a Stat)
    };
    struct CDownloadOptions // what the Copy/Move dialog chose, kept for the CopyOrMoveFromFS call with mode 3
    {
        ConflictPolicy policy = ConflictPolicy::Ask;
        bool preserveTime = true;
        bool valid = false;
    };

    // What happened to a target typed or dropped as an sftp: path (see TryMoveToSftpTarget).
    enum class SftpTargetResult
    {
        NotSftp,    // not an sftp: path: a Windows path (or something Sally reports itself)
        Moved,      // moved on the server
        MoveFailed, // started but cancelled or failed (a message was shown)
        Refused     // a bad path, a copy, or another server (a message was shown): nothing was done
    };

    BOOL DoCopyOrMoveFromFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent, int panel, int selectedFiles,
                            int selectedDirs, std::wstring& path, const std::wstring& mask, BOOL& operationMask,
                            BOOL& cancelOrHandlePath);
    SftpTargetResult TryMoveToSftpTarget(BOOL copy, const std::wstring& targetText, const std::wstring& ownName,
                                         HWND parent, int panel, int selectedFiles, int selectedDirs);
    bool IsSingleFile(int panel, int selectedFiles, int selectedDirs) const;
    std::wstring SftpPathOfTargetPanel(const std::wstring& ownName) const;
    bool IsSameServerFsPath(const wchar_t* fsPath) const;
    BOOL AskDownloadTarget(BOOL copy, const wchar_t* fsName, HWND parent, int panel, int selectedFiles,
                           int selectedDirs, int mode, std::wstring& path, BOOL& operationMask,
                           BOOL& cancelOrHandlePath);
    BOOL DropOnTarget(BOOL copy, const wchar_t* fsName, HWND parent, int panel, int selectedFiles, int selectedDirs,
                      std::wstring& path, BOOL& cancelOrHandlePath);
    BOOL DropToWindowsFolder(BOOL copy, HWND parent, int panel, int selectedFiles, int selectedDirs,
                             std::wstring& path, BOOL& cancelOrHandlePath);
    BOOL DoCopyOrMoveFromDiskToFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent, const wchar_t* sourcePath,
                                  SalEnumSelection2 next, void* nextParam, std::wstring& targetPath,
                                  BOOL* invalidPathOrCancel);
    bool CollectSelection(int panel, int selectedFiles, int selectedDirs, std::vector<CSelectedItem>& items) const;
    bool MakeWorkerParams(const CSftpPath& server, CPanelConnectParams& params);
    static void SetKnownHostsPaths(CPanelConnectParams& params);
    bool StartDownload(BOOL copy, int panel, int selectedFiles, int selectedDirs, const std::wstring& localDir,
                       ConflictPolicy policy, bool preserveTime, const std::wstring& targetName);
    bool MoveOnServer(HWND parent, int panel, int selectedFiles, int selectedDirs, const CSftpPath& target);
    std::wstring FullFsPath(const std::string& rawPath) const; // "sftp:user@host/path"

    // ---- fs/fs_delete.cpp
    BOOL DoDelete(HWND parent, int panel, int selectedFiles, int selectedDirs, BOOL& cancelOrError);
    bool DeleteOneOnSession(HWND parent, const CSelectedItem& item);
    bool StartDeleteOperation(const std::vector<CSelectedItem>& items);

    // ---- fs/fs_commandline.cpp
    BOOL DoExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command, int& selFrom, int& selTo);
    void ShowCommandResult(HWND parent, const CPanelRequest& request) const;

    // ---- fs/fs_chmod.cpp
    BOOL DoChangeAttributes(HWND parent, int panel, int selectedFiles, int selectedDirs);
    bool ResolveCurrentModes(HWND parent, std::vector<CSelectedItem>& items);
    bool ChmodOneOnSession(HWND parent, const CSelectedItem& item, const CModeChange& change);
    bool StartChmodOperation(const std::vector<CSelectedItem>& items, const CModeChange& change, bool recursive,
                             bool applyToFiles, bool applyToDirs);

    // ---- fs/fs_view.cpp
    void DoViewFile(HWND parent, CSalamanderForViewFileOnFSAbstract* salamander, CFileData& file);
    bool DownloadCopy(HWND parent, const std::string& remotePath, const wchar_t* copyPath, const std::wstring& displayName,
                      int64_t mtime, CQuadWord& size);

    // ---- fs/fs_menu.cpp
    std::wstring UrlOf(const std::string& rawPath) const; // "sftp://user@host:port/path"
    void CopyItemsToClipboard(HWND parent, int panel, int selectedFiles, int selectedDirs, bool asUrl);
    void CopyCurrentToClipboard(HWND parent, bool asUrl);
    void BuildContextMenu(CGUIMenuPopupAbstract* menu, int type, int panel) const;
    void RunContextCommand(const wchar_t* fsName, HWND parent, int command, int type, int panel, int selectedFiles,
                           int selectedDirs);
    // the current folder changed: refresh panels showing it, drop the F3 copies. includingSubdirs: folders below it may
    // have changed too (a command line can do anything)
    void NotifyFolderChanged(bool includingSubdirs = false);

    // ---- fs/fs_info.cpp
    void UpdateSecurityIcon(int panel);
    void StoreFreeSpace(const CSftpVfs& vfs);

    // ---- fs/fs_properties.cpp
    void DoShowProperties(HWND parent, int panel, int selectedFiles, int selectedDirs);
    bool ReadOneItemProperties(HWND parent, const CSelectedItem& item, CPropertiesInput& input);

    // ---- fs/fs_namespace.cpp
    bool DoQuickRename(HWND parent, CFileData& file, CSalamanderStringBuffer* newName, BOOL& cancel);
    bool DoCreateDir(HWND parent, CSalamanderStringBuffer* newName, BOOL& cancel);
    bool RefuseBadName(HWND parent, const std::wstring& name) const;
    bool EncodeNewName(HWND parent, const std::wstring& name, std::string& raw) const;
    bool WaitForNameRequest(HWND parent, const std::shared_ptr<CPanelRequest>& request, const std::wstring& waitText);

    // ---- idle close of a detached FS (fs_object.cpp)
    void OnIdleTimer();
    void ArmIdleTimer();
    bool IsIdleDetached(IdleClock::time_point now, int idleMinutes) const;

    std::wstring DisplayPath(const std::string& rawPath) const;
    std::wstring UserPartOf(const std::string& rawPath) const; // "user@host:port/path" or "@Site/path"
    std::wstring ConnectionText() const;                       // "user@host" (or the site name) for messages
    void ShowError(const std::wstring& text) const;

    // The known hosts must outlive the session: members are destroyed in reverse order, so hosts_ comes first.
    CKnownHosts hosts_;
    std::unique_ptr<CPanelSession> session_;

    CSftpPath identity_; // which server this FS is connected to (the path member is not used)
    CSite site_;
    CNameCodec codec_;
    bool execConfirmed_ = false; // the user agreed to run commands on this connection (ExecuteCommandLine)
    bool connected_ = false; // Connect succeeded at least once
    std::string curRaw_ = "/";
    FsErrorState errorState_ = FsErrorState::Ok;

    using ListingPtr = std::shared_ptr<const std::vector<CSftpEntry>>;

    bool HaveCache() const { return cacheEntries_ != nullptr && cacheRaw_ == curRaw_; }

    // The listing ChangePath already read to check the path; the next ListCurrentPath uses it (once). Null = none.
    ListingPtr pendingListing_;

    // The last successfully loaded listing, always of curRaw_ (one listing only, dropped on close and on a broken
    // connection). It is used ONLY right after the user cancelled a listing with ESC, so that the panel stays as it
    // was; every later request reads the server.
    ListingPtr cacheEntries_;
    std::string cacheRaw_;
    // A ListCurrentPath was cancelled: the next ChangePath (Salamander re-establishes the path) returns TRUE for the
    // current path without network traffic, and the ListCurrentPath after it serves the cache. One shot.
    bool reestablish_ = false;

    // ---- directory listing cache (D60). Separate from the "last good listing" above. Main thread only.
    // Folders visited before; used only by a non-forced ChangePath/ListCurrentPath, filled by every server read.
    CListingCache listingCache_;
    unsigned cacheConnectCount_ = 0; // the session's ConnectCount() the entries belong to

public:
    // Drops the cached listings of 'rawPath' and of its parent (an entry of the parent shows size and time of the
    // folder), and everything below 'rawPath' when 'includingSubdirs'. Called before and after every change we make.
    void DropListings(const std::string& rawPath, bool includingSubdirs);

private:
    void ClearListingCache(const wchar_t* why);
    void SyncListingCacheWithConnection();
    bool ListingCacheEnabled();
    ListingPtr LookupListing(const std::string& rawPath, BOOL forceRefresh);
    void StoreListing(const std::string& rawPath, const ListingPtr& entries);
    void LogCache(const std::wstring& text) const;
    void DropListingsForNotification(const std::wstring& changedPath, bool includingSubdirs);

    // Set by BuildNavigation, used by the next ChangePath for the same user part.
    std::wstring hintUserPart_;
    std::string hintRawPath_;

    CDownloadOptions downloadOptions_; // set by the Copy/Move dialog, used (once) by mode 3

    // The free space of the file system of the current directory, read together with each listing (see RunList). Main
    // thread only. GetFSFreeSpace returns it without any network traffic. Not known (freeKnown_ false) until a listing
    // brought it, and for a server without the statvfs extension.
    bool freeKnown_ = false;
    uint64_t freeBytes_ = 0;  // what a normal user can still write
    uint64_t totalBytes_ = 0;

    int busy_ = 0;                   // main thread only: calls that wait and are on the stack
    bool closeRequested_ = false;    // CloseFS/ReleaseObject came during a wait: no new wait, delete when unwound
    bool disconnectRequested_ = false;
    bool detached_ = false;
    IdleClock::time_point lastUsed_ = IdleClock::now(); // set when the FS is detached; requests are in the session
};
