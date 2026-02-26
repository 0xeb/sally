// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_fs) // so that structures are independent of the set alignment
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

struct CFileData;
class CPluginFSInterfaceAbstract;
class CSalamanderDirectoryAbstract;
class CPluginDataInterfaceAbstract;

//
// ****************************************************************************
// CSalamanderForViewFileOnFSAbstract
//
// set of methods from Salamander to support ViewFile execution in CPluginFSInterfaceAbstract,
// the interface validity is limited to the method to which the interface is passed as a parameter

class CSalamanderForViewFileOnFSAbstract
{
public:
    // finds an existing copy of the file in the disk-cache or if the file copy is not yet
    // in the disk-cache, reserves a name for it (target file e.g. for download from FTP);
    // 'uniqueFileName' is the unique name of the original file (the disk-cache is searched
    // by this name; the full file name in Salamander format should be sufficient -
    // "fs-name:fs-user-part"; WARNING: the name is compared "case-sensitive", if
    // the plugin requires "case-insensitive", it must convert all names e.g. to lowercase
    // - see CSalamanderGeneralAbstract::ToLowerCase); 'nameInCache' is the name
    // of the file copy located in the disk-cache (the last part of the original file name
    // is expected here, so that it later reminds the user of the original file in the viewer title);
    // if 'rootTmpPath' is NULL, the disk cache is in the Windows TEMP
    // directory, otherwise the path to the disk-cache is in 'rootTmpPath'; on system error returns
    // NULL (should not occur at all), otherwise returns the full name of the file copy in disk-cache
    // and in 'fileExists' returns TRUE if the file exists in disk-cache (e.g. download from FTP
    // already completed) or FALSE if the file still needs to be prepared (e.g. perform
    // its download); 'parent' is the parent of the error messagebox (for example too long
    // file name)
    // WARNING: if it did not return NULL (no system error occurred), FreeFileNameInCache must be called later
    //          (for the same 'uniqueFileName')
    // NOTE: if the FS uses disk-cache, it should at least call
    //       CSalamanderGeneralAbstract::RemoveFilesFromCache("fs-name:") when unloading the plugin, otherwise
    //       its file copies will unnecessarily clutter the disk-cache
    virtual const char* WINAPI AllocFileNameInCache(HWND parent, const char* uniqueFileName, const char* nameInCache,
                                                    const char* rootTmpPath, BOOL& fileExists) = 0;

    // opens file 'fileName' from a Windows path in the user-requested viewer (either
    // via viewer association or through the View With command); 'parent' is the parent of the error
    // messagebox; if 'fileLock' and 'fileLockOwner' are not NULL, the binding to
    // the opened viewer is returned in them (used as a parameter of the FreeFileNameInCache method); returns TRUE
    // if the viewer was opened
    virtual BOOL WINAPI OpenViewer(HWND parent, const char* fileName, HANDLE* fileLock,
                                   BOOL* fileLockOwner) = 0;

    // must pair with AllocFileNameInCache, called after opening the viewer (or after an error when
    // preparing the file copy or opening the viewer); 'uniqueFileName' is the unique name
    // of the original file (use the same string as when calling AllocFileNameInCache);
    // 'fileExists' is FALSE if the file copy did not exist in disk-cache and TRUE if
    // it already existed (same value as the output parameter 'fileExists' of AllocFileNameInCache method);
    // if 'fileExists' is TRUE, 'newFileOK' and 'newFileSize' are ignored, otherwise 'newFileOK' is
    // TRUE if the file copy was successfully prepared (e.g. download completed successfully) and
    // 'newFileSize' contains the size of the prepared file copy; if 'newFileOK' is FALSE,
    // 'newFileSize' is ignored; 'fileLock' and 'fileLockOwner' bind the opened viewer
    // with file copies in disk-cache (after closing the viewer, disk-cache allows deleting the file
    // copy - when the copy is deleted depends on the disk-cache size on disk), both
    // these parameters can be obtained when calling the OpenViewer method; if the viewer
    // failed to open (or failed to prepare the file copy to disk-cache or the viewer
    // has no binding with disk-cache), 'fileLock' is set to NULL and 'fileLockOwner' to FALSE;
    // if 'fileExists' is TRUE (file copy existed), the value 'removeAsSoonAsPossible'
    // is ignored, otherwise: if 'removeAsSoonAsPossible' is TRUE, the file copy in disk-cache
    // will not be stored longer than necessary (after closing the viewer it will be deleted immediately; if
    // the viewer was not opened at all ('fileLock' is NULL), the file will not be inserted into disk-cache,
    // but deleted)
    virtual void WINAPI FreeFileNameInCache(const char* uniqueFileName, BOOL fileExists, BOOL newFileOK,
                                            const CQuadWord& newFileSize, HANDLE fileLock,
                                            BOOL fileLockOwner, BOOL removeAsSoonAsPossible) = 0;
};

//
// ****************************************************************************
// CPluginFSInterfaceAbstract
//
// set of plugin methods that Salamander needs for working with the file system

// type of icons in panel when listing FS (used in CPluginFSInterfaceAbstract::ListCurrentPath())
#define pitSimple 0       // simple icons for files and directories - by extension (association)
#define pitFromRegistry 1 // icons loaded from registry by file/directory extension
#define pitFromPlugin 2   // icons provided by plugin (icons obtained through CPluginDataInterfaceAbstract)

// event codes (and meaning of 'param' parameter) on FS, received by CPluginFSInterfaceAbstract::Event():
// CPluginFSInterfaceAbstract::TryCloseOrDetach returned TRUE, but the new path failed
// to open, so we stay on the current path (FS that receives this message);
// 'param' is the panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_CLOSEORDETACHCANCELED 0

// successful connection of a new FS to the panel (after path change and its listing)
// 'param' is the panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_OPENED 1

// successful addition to the list of detached FS (end of "panel" FS mode, start of "detached" FS mode);
// 'param' is the panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_DETACHED 2

// successful connection of a detached FS (end of "detached" FS mode, start of "panel" FS mode);
// 'param' is the panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_ATTACHED 3

// activation of Salamander main window (when minimized, waits for restore/maximize,
// and only then sends this event, so that any error windows are shown above Salamander),
// sent only to FS that is in the panel (not detached), if changes on FS are not monitored automatically,
// this event indicates a suitable moment for refresh;
// 'param' is the panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_ACTIVATEREFRESH 4

// timeout expired for one of the timers of this FS, 'param' is the parameter of this timer;
// WARNING: the CPluginFSInterfaceAbstract::Event() method with FSE_TIMER code is called
// from the main thread after WM_TIMER message is delivered to the main window (so e.g.
// any modal dialog may be currently open), so the timer response should
// happen silently (do not open any windows, etc.); calling the
// CPluginFSInterfaceAbstract::Event() method with FSE_TIMER code can happen right after
// calling the CPluginInterfaceForFS::OpenFS method (if a timer is added for
// the newly created FS object)
#define FSE_TIMER 5

// path change (or refresh) just occurred in this FS in the panel or connection
// of this detached FS to the panel (this event is sent after path change and its
// listing); FSE_PATHCHANGED is sent after every successful ListCurrentPath call
// NOTE: FSE_PATHCHANGED closely follows all FSE_OPENED and FSE_ATTACHED
// 'param' is the panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_PATHCHANGED 6

// constants indicating the reason for calling CPluginFSInterfaceAbstract::TryCloseOrDetach();
// in parentheses are always the possible values of forceClose ("FALSE->TRUE" means "first
// tries without force, if FS refuses, asks the user and possibly does it with force") and canDetach:
//
// (FALSE, TRUE) when changing path outside FS opened in panel
#define FSTRYCLOSE_CHANGEPATH 1
// (FALSE->TRUE, FALSE) for FS opened in panel during plugin unload (user requests unload +
// closing Salamander + before plugin removal + unload on plugin request)
#define FSTRYCLOSE_UNLOADCLOSEFS 2
// (FALSE, TRUE) when changing path or refresh (Ctrl+R) of FS opened in panel, it was found
// that no path on FS is accessible anymore - Salamander tries to change the path in panel
// to fixed-drive (if FS does not allow it, FS stays in panel without files and directories)
#define FSTRYCLOSE_CHANGEPATHFAILURE 3
// (FALSE, FALSE) when connecting a detached FS back to the panel, it was found that no path
// on this FS is accessible anymore - Salamander tries to close this detached FS (if FS refuses,
// it stays on the list of detached FS - e.g. in Alt+F1/F2 menu)
#define FSTRYCLOSE_ATTACHFAILURE 4
// (FALSE->TRUE, FALSE) for detached FS during plugin unload (user requests unload +
// closing Salamander + before plugin removal + unload on plugin request)
#define FSTRYCLOSE_UNLOADCLOSEDETACHEDFS 5
// (FALSE, FALSE) plugin called CSalamanderGeneral::CloseDetachedFS() for detached FS
#define FSTRYCLOSE_PLUGINCLOSEDETACHEDFS 6

// flags indicating which file-system services the plugin provides - which methods
// of CPluginFSInterfaceAbstract are defined):
// copy from FS (F5 on FS)
#define FS_SERVICE_COPYFROMFS 0x00000001
// move from FS + rename on FS (F6 on FS)
#define FS_SERVICE_MOVEFROMFS 0x00000002
// copy from disk to FS (F5 on disk)
#define FS_SERVICE_COPYFROMDISKTOFS 0x00000004
// move from disk to FS (F6 on disk)
#define FS_SERVICE_MOVEFROMDISKTOFS 0x00000008
// delete on FS (F8)
#define FS_SERVICE_DELETE 0x00000010
// quick rename on FS (F2)
#define FS_SERVICE_QUICKRENAME 0x00000020
// view from FS (F3)
#define FS_SERVICE_VIEWFILE 0x00000040
// edit from FS (F4)
#define FS_SERVICE_EDITFILE 0x00000080
// edit new file from FS (Shift+F4)
#define FS_SERVICE_EDITNEWFILE 0x00000100
// change attributes on FS (Ctrl+F2)
#define FS_SERVICE_CHANGEATTRS 0x00000200
// create directory on FS (F7)
#define FS_SERVICE_CREATEDIR 0x00000400
// show info about FS (Ctrl+F1)
#define FS_SERVICE_SHOWINFO 0x00000800
// show properties on FS (Alt+Enter)
#define FS_SERVICE_SHOWPROPERTIES 0x00001000
// calculate occupied space on FS (Alt+F10 + Ctrl+Shift+F10 + calc. needed space + spacebar key in panel)
#define FS_SERVICE_CALCULATEOCCUPIEDSPACE 0x00002000
// command line for FS (otherwise command line is disabled)
#define FS_SERVICE_COMMANDLINE 0x00008000
// get free space on FS (number in directory line)
#define FS_SERVICE_GETFREESPACE 0x00010000
// get icon of FS (icon in directory line or Disconnect dialog)
#define FS_SERVICE_GETFSICON 0x00020000
// get next directory-line FS hot-path (for shortening of current FS path in panel)
#define FS_SERVICE_GETNEXTDIRLINEHOTPATH 0x00040000
// context menu on FS (Shift+F10)
#define FS_SERVICE_CONTEXTMENU 0x00080000
// get item for change drive menu or Disconnect dialog (item for active/detached FS in Alt+F1/F2 or Disconnect dialog)
#define FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM 0x00100000
// accepts change on path notifications from Salamander (see PostChangeOnPathNotification)
#define FS_SERVICE_ACCEPTSCHANGENOTIF 0x00200000
// get path for main-window title (text in window caption) (see Configuration/Appearance/Display current path...)
// if it's not defined, full path is displayed in window caption in all display modes
#define FS_SERVICE_GETPATHFORMAINWNDTITLE 0x00400000
// Find (Alt+F7 on FS) - if it's not defined, standard Find Files and Directories dialog
// is opened even if FS is opened in panel
#define FS_SERVICE_OPENFINDDLG 0x00800000
// open active folder (Shift+F3)
#define FS_SERVICE_OPENACTIVEFOLDER 0x01000000
// show security information (click on security icon in Directory Line, see CSalamanderGeneralAbstract::ShowSecurityIcon)
#define FS_SERVICE_SHOWSECURITYINFO 0x02000000

// missing: Change Case, Convert, Properties, Make File List

// context menu types for CPluginFSInterfaceAbstract::ContextMenu() method
#define fscmItemsInPanel 0 // context menu for items in panel (selected/focused files and directories)
#define fscmPathInPanel 1  // context menu for current path in panel
#define fscmPanel 2        // context menu for panel

#define SALCMDLINE_MAXLEN 8192 // maximum length of command from Salamander command line

class CPluginFSInterfaceAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginFSInterfaceEncapsulation)
    friend class CPluginFSInterfaceEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // returns user-part of the current path in this FS, 'userPart' is a buffer of MAX_PATH size
    // for the path, returns success
    virtual BOOL WINAPI GetCurrentPath(char* userPart) = 0;

    // returns user-part of the full name of file/directory/up-dir 'file' ('isDir' is 0/1/2) on the current
    // path in this FS; for up-dir directory (first in the directory list and named ".."),
    // 'isDir'==2 and the method should return the current path shortened by the last component; 'buf'
    // is a buffer of 'bufSize' for the resulting full name, returns success
    virtual BOOL WINAPI GetFullName(CFileData& file, int isDir, char* buf, int bufSize) = 0;

    // returns absolute path (including fs-name) corresponding to relative path 'path' on this FS;
    // returns FALSE if this method is not implemented (other return values are then ignored);
    // 'parent' is the parent of any messageboxes; 'fsName' is the current FS name; 'path' is a buffer
    // of 'pathSize' characters, on input it contains the relative path on FS, on output it contains
    // the corresponding absolute path on FS; in 'success' returns TRUE if the path was successfully translated
    // (the string in 'path' should be used - otherwise it is ignored) - path change follows (if it is
    // a path on this FS, ChangePath() is called); if it returns FALSE in 'success', it is assumed
    // that the user has already seen the error message
    virtual BOOL WINAPI GetFullFSPath(HWND parent, const char* fsName, char* path, int pathSize,
                                      BOOL& success) = 0;

    // returns user-part of the root of the current path in this FS, 'userPart' is a buffer of MAX_PATH size
    // for the path (used in "goto root" function), returns success
    virtual BOOL WINAPI GetRootPath(char* userPart) = 0;

    // compares the current path in this FS and the path specified via 'fsNameIndex' and 'userPart'
    // (the FS name in the path is from this plugin and is given by index 'fsNameIndex'), returns TRUE
    // if the paths are identical; 'currentFSNameIndex' is the index of the current FS name
    virtual BOOL WINAPI IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const char* userPart) = 0;

    // returns TRUE if the path is from this FS (which means Salamander can pass the path
    // to ChangePath of this FS); the path is always to one of the FS of this plugin (e.g. Windows
    // paths and archive paths never come here); 'fsNameIndex' is the index of the FS name
    // in the path (index is zero for fs-name specified in CSalamanderPluginEntryAbstract::SetBasicPluginData,
    // for other fs-names the index is returned by CSalamanderPluginEntryAbstract::AddFSName); user-part
    // of the path is 'userPart'; 'currentFSNameIndex' is the index of the current FS name
    virtual BOOL WINAPI IsOurPath(int currentFSNameIndex, int fsNameIndex, const char* userPart) = 0;

    // changes the current path in this FS to the path specified via 'fsName' and 'userPart' (exactly
    // or to the nearest accessible subpath of 'userPart' - see 'mode' value); in case
    // the path is shortened because it is a path to a file (a guess that it might be
    // a path to a file is sufficient - after listing the path it is verified if the file exists, or
    // an error is shown to the user) and 'cutFileName' is not NULL (possible only in 'mode' 3), returns
    // in the buffer 'cutFileName' (of MAX_PATH characters size) the name of this file (without path),
    // otherwise returns an empty string in the buffer 'cutFileName'; 'currentFSNameIndex' is the index
    // of the current FS name; 'fsName' is a buffer of MAX_PATH size, on input it contains the FS name
    // in the path, which is from this plugin (but does not have to match the current FS name
    // in this object, it is sufficient if IsOurPath() returns TRUE for it), on output 'fsName' contains
    // the current FS name in this object (must be from this plugin); 'fsNameIndex' is the index
    // of FS name 'fsName' in the plugin (for easier detection of which FS name it is); if
    // 'pathWasCut' is not NULL, TRUE is returned in it if the path was shortened; Salamander
    // uses 'cutFileName' and 'pathWasCut' in the Change Directory command (Shift+F7) when entering
    // a file name - the file gets focused; if 'forceRefresh' is TRUE, it is a
    // hard refresh (Ctrl+R) and the plugin should change the path without using cache information
    // (it is necessary to verify if the new path exists); 'mode' is the path change mode:
    //   1 (refresh path) - shortens the path if needed; do not report path non-existence (shorten
    //                      without message), report file instead of path, path inaccessibility and other errors
    //   2 (calling ChangePanelPathToPluginFS, back/forward in history, etc.) - shortens the path
    //                      if needed; report all path errors (file
    //                      instead of path, non-existence, inaccessibility and others)
    //   3 (change-dir command) - shortens the path only if it is a file or the path cannot be listed
    //                      (ListCurrentPath returns FALSE for it); do not report file instead of path
    //                      (shorten without message and return file name), report all other
    //                      path errors (non-existence, inaccessibility and others)
    // if 'mode' is 1 or 2, returns FALSE only if no path on this FS is accessible
    // (e.g. when connection is lost); if 'mode' is 3, returns FALSE if the requested
    // path or file is not accessible (path shortening occurs only if it is a file);
    // in case opening the FS is time-consuming (e.g. connecting to FTP server) and 'mode'
    // is 3, it is possible to adjust behavior like for archives - shorten the path if needed and return FALSE
    // only if no path on FS is accessible, error reporting does not change
    virtual BOOL WINAPI ChangePath(int currentFSNameIndex, char* fsName, int fsNameIndex,
                                   const char* userPart, char* cutFileName, BOOL* pathWasCut,
                                   BOOL forceRefresh, int mode) = 0;

    // loads files and directories from the current path, stores them in the 'dir' object (for path NULL or
    // "", files and directories on other paths are ignored; if a directory named
    // ".." is added, it is drawn as "up-dir" symbol; file and directory names are fully
    // dependent on the plugin, Salamander only displays them); Salamander obtains the content
    // of plugin-added columns via the 'pluginData' interface (if the plugin does not add columns
    // and has no custom icons, returns 'pluginData'==NULL); in 'iconsType' returns the requested method
    // of obtaining file and directory icons for the panel, pitFromPlugin degrades to pitSimple if
    // 'pluginData' is NULL (without 'pluginData' pitFromPlugin cannot be ensured); if 'forceRefresh' is
    // TRUE, it is a hard refresh (Ctrl+R) and the plugin should load files and directories without using
    // cache; returns TRUE on successful load, if it returns FALSE it is an error and ChangePath will be called
    // on the current path, it is expected that ChangePath will select an accessible subpath
    // or return FALSE, after a successful ChangePath call, ListCurrentPath will be called again;
    // if it returns FALSE, the 'pluginData' return value is ignored (data in 'dir' needs to be
    // released using 'dir.Clear(pluginData)', otherwise only the Salamander part of data is released);
    virtual BOOL WINAPI ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                        CPluginDataInterfaceAbstract*& pluginData,
                                        int& iconsType, BOOL forceRefresh) = 0;

    // prepares FS for closing/detaching from panel or closing a detached FS; if 'forceClose' is
    // TRUE, the FS will be closed regardless of return values, the action was forced by user or
    // critical shutdown is in progress (see more at CSalamanderGeneralAbstract::IsCriticalShutdown), anyway
    // there is no point in asking the user anything, FS should simply be closed immediately (do not open any windows);
    // if 'forceClose' is FALSE, FS can be closed or detached ('canDetach' TRUE) or only
    // closed ('canDetach' FALSE); in 'detach' returns TRUE if it only wants to detach, FALSE means
    // close; 'reason' contains the reason for calling this method (one of FSTRYCLOSE_XXX); returns TRUE
    // if it can be closed/detached, otherwise returns FALSE
    virtual BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason) = 0;

    // receives events on this FS, see event codes FSE_XXX; 'param' is the event parameter
    virtual void WINAPI Event(int event, DWORD param) = 0;

    // releases all FS resources except listing data (during this method call the listing
    // may still be displayed in the panel); called just before removing the listing from the panel
    // (listing is removed only for active FS, detached FS have no listing) and CloseFS for this FS;
    // 'parent' is the parent of any messageboxes, if critical shutdown is in progress (see more at
    // CSalamanderGeneralAbstract::IsCriticalShutdown), do not display any windows
    virtual void WINAPI ReleaseObject(HWND parent) = 0;

    // obtains the set of supported FS services (see FS_SERVICE_XXX constants); returns the logical
    // sum of constants; called after opening this FS (see CPluginInterfaceForFSAbstract::OpenFS),
    // and then after each ChangePath and ListCurrentPath call of this FS
    virtual DWORD WINAPI GetSupportedServices() = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM:
    // obtains the item for this FS (active or detached) for the Change Drive menu (Alt+F1/F2)
    // or Disconnect dialog (hotkey: F12; any disconnect of this FS is handled by the method
    // CPluginInterfaceForFSAbstract::DisconnectFS; if GetChangeDriveOrDisconnectItem returns
    // FALSE and FS is in the panel, an item with icon obtained via GetFSIcon and root path is added);
    // if the return value is TRUE, an item with icon 'icon' and text 'title' is added;
    // 'fsName' is the current FS name; if 'icon' is NULL, the item has no icon; if
    // 'destroyIcon' is TRUE and 'icon' is not NULL, 'icon' is released after use via Win32 API
    // function DestroyIcon; 'title' is text allocated on Salamander heap and can contain
    // up to three columns separated by '\t' (see Alt+F1/F2 menu), in Disconnect dialog
    // only the second column is used; if the return value is FALSE, the return values
    // 'title', 'icon' and 'destroyIcon' are ignored (no item is added)
    virtual BOOL WINAPI GetChangeDriveOrDisconnectItem(const char* fsName, char*& title,
                                                       HICON& icon, BOOL& destroyIcon) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_GETFSICON:
    // obtains the FS icon for the directory-line toolbar or possibly for the Disconnect dialog (F12);
    // the icon for Disconnect dialog is obtained here only if the GetChangeDriveOrDisconnectItem method
    // does not return an item for this FS (e.g. RegEdit and WMobile);
    // returns icon or NULL if the standard icon should be used; if 'destroyIcon' is TRUE
    // and returns an icon (not NULL), the returned icon is released after use via Win32 API
    // function DestroyIcon
    // Warning: if the icon resource is loaded via LoadIcon in 16x16 dimensions, LoadIcon returns
    //          a 32x32 icon. When subsequently drawing it into 16x16, colored contours will appear
    //          around the icon. The 16->32->16 conversion can be avoided by using LoadImage:
    //          (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(id), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    //
    // no windows must be displayed in this method (panel content is not consistent, messages must not
    // be distributed - redraw, etc.)
    virtual HICON WINAPI GetFSIcon(BOOL& destroyIcon) = 0;

    // returns the requested drop-effect for drag&drop operation from FS (can be this FS too) to this FS;
    // 'srcFSPath' is the source path; 'tgtFSPath' is the target path (it is from this FS); 'allowedEffects'
    // contains the allowed drop-effects; 'keyState' is the key state (combination of MK_CONTROL,
    // MK_SHIFT, MK_ALT, MK_BUTTON, MK_LBUTTON, MK_MBUTTON and MK_RBUTTON flags, see IDropTarget::Drop);
    // 'dropEffect' contains the recommended drop-effects (equal to 'allowedEffects' or limited to
    // DROPEFFECT_COPY or DROPEFFECT_MOVE if the user holds Ctrl or Shift keys) and
    // the chosen drop-effect is returned in it (DROPEFFECT_COPY, DROPEFFECT_MOVE or DROPEFFECT_NONE);
    // if the method does not change 'dropEffect' and it contains multiple effects, Copy operation
    // is preferentially selected
    virtual void WINAPI GetDropEffect(const char* srcFSPath, const char* tgtFSPath,
                                      DWORD allowedEffects, DWORD keyState,
                                      DWORD* dropEffect) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_GETFREESPACE:
    // returns in 'retValue' (must not be NULL) the size of free space on FS (displayed
    // on the right of directory-line); if free space cannot be determined, returns
    // CQuadWord(-1, -1) (the value is not displayed)
    virtual void WINAPI GetFSFreeSpace(CQuadWord* retValue) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_GETNEXTDIRLINEHOTPATH:
    // finding delimiter points in the Directory Line text (for path shortening via mouse - hot-tracking);
    // 'text' is the text in Directory Line (path + optionally filter); 'pathLen' is the path length in 'text'
    // (the rest is filter); 'offset' is the character offset from which to search for delimiter point; returns TRUE
    // if the next delimiter point exists, its position is returned in 'offset'; returns FALSE if no next
    // delimiter point exists (end of text is not considered a delimiter point)
    virtual BOOL WINAPI GetNextDirectoryLineHotPath(const char* text, int pathLen, int& offset) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_GETNEXTDIRLINEHOTPATH:
    // adjustment of the shortened path text to be displayed in the panel (Directory Line - path
    // shortening via mouse - hot-tracking); used when the hot-text from Directory Line does not match
    // the path exactly (e.g. missing closing bracket - VMS paths on FTP - "[DIR1.DIR2.DIR3]");
    // 'path' is in/out buffer with the path (buffer size is 'pathBufSize')
    virtual void WINAPI CompleteDirectoryLineHotPath(char* path, int pathBufSize) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_GETPATHFORMAINWNDTITLE:
    // obtains the text to be displayed in the main window title if displaying
    // the current path in the main window title is enabled (see Configuration/Appearance/Display current
    // path...); 'fsName' is the current FS name; if 'mode' is 1, it is the
    // "Directory Name Only" mode (only the current directory name should be displayed - the last
    // path component); if 'mode' is 2, it is the "Shortened Path" mode (the shortened
    // form of path should be displayed - root (including path separator) + "..." + path
    // separator + last path component); 'buf' is a buffer of 'bufSize' for
    // the resulting text; returns TRUE if it returns the requested text; returns FALSE if
    // the text should be created based on delimiter point data obtained via the
    // GetNextDirectoryLineHotPath() method
    // NOTE: if GetSupportedServices() does not also return FS_SERVICE_GETPATHFORMAINWNDTITLE,
    //       the full FS path is displayed in the main window title in all title
    //       display modes (including "Directory Name Only" and "Shortened Path")
    virtual BOOL WINAPI GetPathForMainWindowTitle(const char* fsName, int mode, char* buf, int bufSize) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_SHOWINFO:
    // displays a dialog with information about the FS (free space, capacity, name, options, etc.);
    // 'fsName' is the current FS name; 'parent' is the suggested parent of the displayed dialog
    virtual void WINAPI ShowInfoDialog(const char* fsName, HWND parent) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_COMMANDLINE:
    // executes a command for the FS in the active panel from the command line below the panels; returns FALSE on error
    // (command is not inserted into command line history and other return values are ignored);
    // returns TRUE on successful command execution (note: command results do not matter - what matters
    // is only whether it was executed (e.g. for FTP it is about whether it was delivered to the server));
    // 'parent' is the suggested parent of any displayed dialogs; 'command' is a buffer
    // of size SALCMDLINE_MAXLEN+1, which on input contains the command to execute (the actual
    // maximum command length depends on the Windows version and the COMSPEC environment variable content)
    // and on output the new command line content (usually just cleared to empty string);
    // 'selFrom' and 'selTo' return the selection position in the new command line content (if they match,
    // only the cursor is positioned; if the output is an empty line, these values are ignored)
    // WARNING: this method should not directly change the path in the panel - there is a risk of FS closing on path error
    //          (the this pointer would cease to exist for the method)
    virtual BOOL WINAPI ExecuteCommandLine(HWND parent, char* command, int& selFrom, int& selTo) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_QUICKRENAME:
    // quick rename of a file or directory ('isDir' is FALSE/TRUE) 'file' on FS;
    // allows opening a custom dialog for quick rename (parameter 'mode' is 1)
    // or using the standard dialog (when 'mode'==1 returns FALSE and 'cancel' also FALSE,
    // then Salamander opens the standard dialog and passes the obtained new name in 'newName' in
    // the next QuickRename call with 'mode'==2); 'fsName' is the current FS name; 'parent' is
    // the suggested parent of any displayed dialogs; 'newName' is the new name if
    // 'mode'==2; if it returns TRUE, the new name is returned in 'newName' (max. MAX_PATH characters;
    // not full name, just the item name in panel) - Salamander will try to focus it after
    // refresh (the FS itself handles refresh, e.g. using the
    // CSalamanderGeneralAbstract::PostRefreshPanelFS method); if it returns FALSE and 'mode'==2,
    // the erroneous new name is returned in 'newName' (possibly modified in some way - e.g.
    // operation mask may already be applied) if the user wants to cancel the operation,
    // 'cancel' returns TRUE; if 'cancel' returns FALSE, the method returns TRUE on successful completion
    // of the operation, if it returns FALSE when 'mode'==1, the standard dialog for
    // quick rename should be opened, if it returns FALSE when 'mode'==2, it is an operation error (the erroneous
    // new name is returned in 'newName' - the standard dialog is reopened and the user can
    // correct the erroneous name there)
    virtual BOOL WINAPI QuickRename(const char* fsName, int mode, HWND parent, CFileData& file,
                                    BOOL isDir, char* newName, BOOL& cancel) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_ACCEPTSCHANGENOTIF:
    // receives information about a change on path 'path' (if 'includingSubdirs' is TRUE,
    // it also includes changes in subdirectories of path 'path'); this method should decide
    // whether a refresh of this FS is needed (e.g. using the
    // CSalamanderGeneralAbstract::PostRefreshPanelFS method); applies to both active FS and
    // detached FS; 'fsName' is the current FS name
    // NOTE: for the plugin as a whole, there is the method
    //       CPluginInterfaceAbstract::AcceptChangeOnPathNotification()
    virtual void WINAPI AcceptChangeOnPathNotification(const char* fsName, const char* path,
                                                       BOOL includingSubdirs) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_CREATEDIR:
    // creates a new directory on FS; allows opening a custom dialog for creating
    // a directory (parameter 'mode' is 1) or using the standard dialog (when 'mode'==1 returns
    // FALSE and 'cancel' also FALSE, then Salamander opens the standard dialog and passes the obtained
    // directory name in 'newName' in the next CreateDir call with 'mode'==2);
    // 'fsName' is the current FS name; 'parent' is the suggested parent of any displayed
    // dialogs; 'newName' is the name of the new directory if 'mode'==2; if it returns TRUE,
    // the name of the new directory is returned in 'newName' (max. 2 * MAX_PATH characters; not a full name,
    // just the item name in panel) - Salamander will try to focus it after refresh (the FS itself
    // handles refresh, e.g. using the CSalamanderGeneralAbstract::PostRefreshPanelFS method);
    // if it returns FALSE and 'mode'==2, the erroneous directory name is returned in 'newName' (max. 2 * MAX_PATH
    // characters, possibly converted to absolute form); if the user wants to cancel the operation,
    // 'cancel' returns TRUE; if 'cancel' returns FALSE, the method returns TRUE on successful completion
    // of the operation, if it returns FALSE when 'mode'==1, the standard dialog for creating
    // a directory should be opened, if it returns FALSE when 'mode'==2, it is an operation error (the erroneous
    // directory name is returned in 'newName' - the standard dialog is reopened and the user can
    // correct the erroneous name there)
    virtual BOOL WINAPI CreateDir(const char* fsName, int mode, HWND parent,
                                  char* newName, BOOL& cancel) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_VIEWFILE:
    // viewing a file (directories cannot be viewed via the View function) 'file' on the current path
    // on FS; 'fsName' is the current FS name; 'parent' is the parent of any error
    // messageboxes; 'salamander' is a set of methods from Salamander needed for implementing
    // viewing with caching
    virtual void WINAPI ViewFile(const char* fsName, HWND parent,
                                 CSalamanderForViewFileOnFSAbstract* salamander,
                                 CFileData& file) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_DELETE:
    // deleting files and directories selected in the panel; allows opening a custom dialog with a delete
    // confirmation (parameter 'mode' is 1; whether to display a confirmation depends on the value of
    // SALCFG_CNFRMFILEDIRDEL - TRUE means the user wants to confirm deletion)
    // or using the standard confirmation (when 'mode'==1 returns FALSE and 'cancelOrError' also FALSE,
    // then Salamander opens the standard confirmation (if SALCFG_CNFRMFILEDIRDEL is TRUE)
    // and in case of a positive answer calls Delete again with 'mode'==2); 'fsName' is the current FS name;
    // 'parent' is the suggested parent of any displayed dialogs; 'panel' identifies the panel
    // (PANEL_LEFT or PANEL_RIGHT) in which the FS is open (files/directories to be deleted are
    // obtained from this panel); 'selectedFiles' + 'selectedDirs' - number of selected
    // files and directories, if both values are zero, the file/directory under the cursor
    // (focus) is deleted, before calling the Delete method either files and directories are selected or there is at least
    // focus on a file/directory, so there is always something to work with (no additional tests are needed);
    // if it returns TRUE and 'cancelOrError' is FALSE, the operation completed correctly and the selected
    // files/directories should be deselected (if they survived the deletion); if the user wants to cancel
    // the operation or an error occurs, 'cancelOrError' returns TRUE and no deselection
    // of files/directories occurs; if it returns FALSE when 'mode'==1 and 'cancelOrError' is FALSE,
    // the standard delete confirmation should be opened
    virtual BOOL WINAPI Delete(const char* fsName, int mode, HWND parent, int panel,
                               int selectedFiles, int selectedDirs, BOOL& cancelOrError) = 0;

    // copy/move from FS (parameter 'copy' is TRUE/FALSE), in the following text only
    // copy is mentioned, but everything applies equally to move; 'copy' can be TRUE (copy) only
    // if GetSupportedServices() also returns FS_SERVICE_COPYFROMFS; 'copy' can be FALSE
    // (move or rename) only if GetSupportedServices() also returns FS_SERVICE_MOVEFROMFS;
    //
    // copying files and directories (from FS) selected in the panel; allows opening a custom dialog for
    // specifying the copy target (parameter 'mode' is 1) or using the standard dialog (returns FALSE
    // and 'cancelOrHandlePath' also FALSE, then Salamander opens the standard dialog and passes the obtained target
    // path in 'targetPath' in the next CopyOrMoveFromFS call with 'mode'==2); when 'mode'==2,
    // 'targetPath' is the exact string entered by the user (CopyOrMoveFromFS can parse it
    // as it sees fit); if CopyOrMoveFromFS supports only Windows target paths (or cannot
    // process the user-entered path - e.g. it leads to another FS or to an archive), it can use the
    // standard path processing in Salamander (currently can only process Windows paths,
    // in the future it may also process FS and archive paths via TEMP directory using a sequence of several basic
    // operations) - returns FALSE, 'cancelOrHandlePath' TRUE and 'operationMask' TRUE/FALSE
    // (supports/does not support operation masks - if it does not support them and the path contains a mask, an
    // error message is displayed), then Salamander processes the path returned in 'targetPath' (currently only splitting
    // a Windows path into existing part, non-existing part and possibly a mask; also allows creating
    // subdirectories from the non-existing part) and if the path is OK, calls CopyOrMoveFromFS again
    // with 'mode'==3 and in 'targetPath' with the target path and possibly an operation mask (two strings
    // separated by a null; no mask -> two nulls at the end of the string), if there is some
    // error in the path, calls CopyOrMoveFromFS again with 'mode'==4 and in 'targetPath' with the adjusted erroneous target
    // path (the error was already reported to the user; the user should be given a chance to correct the path;
    // "." and ".." may have been removed from the path, etc.);
    //
    // if the user initiates the operation via drag&drop (drops files/directories from FS to the same panel
    // or to another drop-target), 'mode'==5 and 'targetPath' contains the target path of the operation (can
    // be a Windows path, FS path and in the future archive paths can be expected too),
    // 'targetPath' is terminated with two nulls (for compatibility with 'mode'==3); 'dropTarget' is
    // in this case the drop-target window (used for reactivating the drop-target after opening
    // the progress window of the operation, see CSalamanderGeneralAbstract::ActivateDropTarget); when 'mode'==5 only
    // the return value TRUE is meaningful;
    //
    // 'fsName' is the current FS name; 'parent' is the suggested parent of any displayed dialogs;
    // 'panel' identifies the panel (PANEL_LEFT or PANEL_RIGHT) in which the FS is open (files/directories
    // to be copied are obtained from this panel);
    // 'selectedFiles' + 'selectedDirs' - number of selected files and directories, if both
    // values are zero, the file/directory under the cursor (focus) is copied, before calling
    // the CopyOrMoveFromFS method either files and directories are selected or there is at least focus
    // on a file/directory, so there is always something to work with (no additional tests
    // are needed); on input 'targetPath' when 'mode'==1 contains the suggested target path
    // (only Windows paths without mask or an empty string), when 'mode'==2 contains the
    // target path string entered by the user in the standard dialog, when 'mode'==3 contains the target
    // path and mask (separated by null), when 'mode'==4 contains the erroneous target path, when 'mode'==5
    // contains the target path (Windows, FS or archive) terminated with two nulls; if
    // the method returns FALSE, 'targetPath' on output (buffer of 2 * MAX_PATH characters) when
    // 'cancelOrHandlePath'==FALSE contains the suggested target path for the standard dialog and when
    // 'cancelOrHandlePath'==TRUE contains the target path string to be processed; if the method returns TRUE and
    // 'cancelOrHandlePath' is FALSE, 'targetPath' contains the name of the item to focus
    // in the source panel (buffer of 2 * MAX_PATH characters; not a full name, just the item name in panel;
    // if empty string, focus remains unchanged); 'dropTarget' is not NULL only when
    // the path is specified via drag&drop (see description above)
    //
    // if it returns TRUE and 'cancelOrHandlePath' is FALSE, the operation completed correctly and the selected
    // files/directories should be deselected; if the user wants to cancel the operation or an error
    // occurred, the method returns TRUE and 'cancelOrHandlePath' TRUE, in both cases no deselection
    // of files/directories occurs; if it returns FALSE, the standard dialog should be opened ('cancelOrHandlePath'
    // is FALSE) or the path should be processed in the standard way ('cancelOrHandlePath' is TRUE)
    //
    // NOTE: if the option to copy/move to the target panel path is offered,
    //       CSalamanderGeneralAbstract::SetUserWorkedOnPanelPath should be called for the target
    //       panel, otherwise the path in that panel will not be inserted into the list of working
    //       directories - List of Working Directories (Alt+F12)
    virtual BOOL WINAPI CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                         int panel, int selectedFiles, int selectedDirs,
                                         char* targetPath, BOOL& operationMask,
                                         BOOL& cancelOrHandlePath, HWND dropTarget) = 0;

    // copy/move from a Windows path to FS (parameter 'copy' is TRUE/FALSE), in the following text
    // only copy is mentioned, but everything applies equally to move; 'copy' can be TRUE (copy)
    // only if GetSupportedServices() also returns FS_SERVICE_COPYFROMDISKTOFS; 'copy' can be FALSE
    // (move or rename) only if GetSupportedServices() also returns FS_SERVICE_MOVEFROMDISKTOFS;
    //
    // copying selected (in the panel or elsewhere) files and directories to FS; when 'mode'==1 allows
    // preparing the target path text for the user in the standard copy dialog, this is the situation
    // when the source panel (the panel where the Copy command is invoked (F5 key)) has a Windows
    // path and the target panel has this FS; when 'mode'==2 and 'mode'==3 the plugin can perform the copy operation or
    // report one of two errors: "path error" (e.g. contains invalid characters or does not exist)
    // and "the requested operation cannot be performed on this FS" (e.g. it is FTP, but the path opened
    // on this FS differs from the target path (e.g. different FTP server for FTP) - a different/new FS needs to be opened;
    // a newly opened FS cannot report this error);
    // WARNING: this method can be called for any target FS path of this plugin (so it can
    //          also be a path with a different FS name of this plugin)
    //
    // 'fsName' is the current FS name; 'parent' is the suggested parent of any displayed
    // dialogs; 'sourcePath' is the source Windows path (all selected files and directories
    // are addressed relative to it), when 'mode'==1 it is NULL; selected files and directories
    // are specified by the enumeration function 'next' whose parameter is 'nextParam', when 'mode'==1
    // they are NULL; 'sourceFiles' + 'sourceDirs' - number of selected files and directories (the sum
    // is always non-zero); 'targetPath' is an in/out buffer of at least 2 * MAX_PATH characters for the target
    // path; when 'mode'==1 'targetPath' on input is the current path on this FS and on output the target
    // path for the standard copy dialog; when 'mode'==2 'targetPath' on input is the user-entered
    // target path (unmodified, including mask, etc.) and on output is ignored except when
    // the method returns FALSE (error) and 'invalidPathOrCancel' is TRUE (path error), in this case on
    // output is the adjusted target path (e.g. "." and ".." removed), which the user will correct
    // in the standard copy dialog; when 'mode'==3 'targetPath' on input is the drag&drop-specified
    // target path and on output is ignored; if 'invalidPathOrCancel' is not NULL (only 'mode'==2
    // and 'mode'==3), TRUE is returned in it if the path is incorrectly specified (contains invalid characters or
    // does not exist, etc.) or the operation was cancelled (cancel) - error/cancel message is displayed
    // before this method returns
    //
    // when 'mode'==1 the method returns TRUE on success, if it returns FALSE, an empty string is used as the target path
    // for the standard copy dialog; if the method returns FALSE when 'mode'==2 and 'mode'==3,
    // another FS should be found to process the operation (if 'invalidPathOrCancel' is FALSE) or the
    // user should correct the target path (if 'invalidPathOrCancel' is TRUE); if the method returns TRUE
    // when 'mode'==2 or 'mode'==3, the operation was performed and selected files and directories should be deselected
    // (if 'invalidPathOrCancel' is FALSE) or an error/cancellation occurred and selected files and directories
    // should not be deselected (if 'invalidPathOrCancel' is TRUE)
    //
    // WARNING: The CopyOrMoveFromDiskToFS method can be called in three situations:
    //          - this FS is active in the panel
    //          - this FS is detached
    //          - this FS was just created (by calling OpenFS) and will be immediately destroyed after
    //            the method ends (by calling CloseFS) - no other method was called on it (not even ChangePath)
    virtual BOOL WINAPI CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                               const char* sourcePath, SalEnumSelection2 next,
                                               void* nextParam, int sourceFiles, int sourceDirs,
                                               char* targetPath, BOOL* invalidPathOrCancel) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_CHANGEATTRS:
    // changing attributes of files and directories selected in the panel; each plugin has
    // its own dialog for specifying attribute changes;
    // 'fsName' is the current FS name; 'parent' is the suggested parent of the custom dialog; 'panel'
    // identifies the panel (PANEL_LEFT or PANEL_RIGHT) in which the FS is open (files/directories
    // to work with are obtained from this panel);
    // 'selectedFiles' + 'selectedDirs' - number of selected files and directories,
    // if both values are zero, the file/directory under the cursor
    // (focus) is used, before calling the ChangeAttributes method either files and directories are selected or
    // there is at least focus on a file/directory, so there is always something to work with (no additional tests
    // are needed); if it returns TRUE, the operation completed correctly and the selected files/directories
    // should be deselected; if the user wants to cancel the operation or an error occurs, the method
    // returns FALSE and no deselection of files/directories occurs
    virtual BOOL WINAPI ChangeAttributes(const char* fsName, HWND parent, int panel,
                                         int selectedFiles, int selectedDirs) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_SHOWPROPERTIES:
    // displaying a window with properties of files and directories selected in the panel; each plugin
    // has its own properties window;
    // 'fsName' is the current FS name; 'parent' is the suggested parent of the custom window
    // (the Windows properties window is modeless - WARNING: a modeless window must
    // have its own thread); 'panel' identifies the panel (PANEL_LEFT or PANEL_RIGHT)
    // in which the FS is open (files/directories to work with are obtained from this panel);
    // 'selectedFiles' + 'selectedDirs' - number of selected
    // files and directories, if both values are zero, the file/directory
    // under the cursor (focus) is used, before calling the ShowProperties method either
    // files and directories are selected or there is at least focus on a file/directory, so there is always
    // something to work with (no additional tests are needed)
    virtual void WINAPI ShowProperties(const char* fsName, HWND parent, int panel,
                                       int selectedFiles, int selectedDirs) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_CONTEXTMENU:
    // displaying a context menu for files and directories selected in the panel (right mouse button click
    // on items in the panel) or for the current path in the panel (right mouse button click
    // on the change-drive button in the panel toolbar) or for the panel (right mouse button click
    // after items in the panel); each plugin has its own context menu;
    //
    // 'fsName' is the current FS name; 'parent' is the suggested parent of the context menu;
    // 'menuX' + 'menuY' are the suggested coordinates of the top-left corner of the context menu;
    // 'type' is the context menu type (see descriptions of fscmXXX constants); 'panel'
    // identifies the panel (PANEL_LEFT or PANEL_RIGHT) for which the context
    // menu should be opened (files/directories/path to work with are obtained from this panel);
    // when 'type'==fscmItemsInPanel, 'selectedFiles' + 'selectedDirs' is the
    // number of selected files and directories, if both values are zero, the
    // file/directory under the cursor (focus) is used, before calling the ContextMenu method either
    // files and directories are selected (and were clicked on) or there is at least focus on a
    // file/directory (not on up-dir), so there is always something to work with (no additional tests
    // are needed); if 'type'!=fscmItemsInPanel, 'selectedFiles' + 'selectedDirs'
    // are always set to zero (ignored)
    virtual void WINAPI ContextMenu(const char* fsName, HWND parent, int menuX, int menuY, int type,
                                    int panel, int selectedFiles, int selectedDirs) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_CONTEXTMENU:
    // if FS is open in the panel and one of the messages WM_INITPOPUP, WM_DRAWITEM,
    // WM_MENUCHAR or WM_MEASUREITEM arrives, Salamander calls HandleMenuMsg to allow the plugin
    // to work with IContextMenu2 and IContextMenu3
    // the plugin returns TRUE if it processed the message and FALSE otherwise
    virtual BOOL WINAPI HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_OPENFINDDLG:
    // opens the Find dialog for FS in the panel; 'fsName' is the current FS name; 'panel' identifies
    // the panel (PANEL_LEFT or PANEL_RIGHT) for which the Find dialog should be opened (from this panel
    // the search path is usually obtained); returns TRUE on successful opening of the Find dialog;
    // if it returns FALSE, Salamander opens the standard Find Files and Directories dialog
    virtual BOOL WINAPI OpenFindDialog(const char* fsName, int panel) = 0;

    // only if GetSupportedServices() also returns FS_SERVICE_OPENACTIVEFOLDER:
    // opens an Explorer window for the current path in the panel
    // 'fsName' is the current FS name; 'parent' is the suggested parent of the displayed dialog
    virtual void WINAPI OpenActiveFolder(const char* fsName, HWND parent) = 0;

    // only if GetSupportedServices() returns FS_SERVICE_MOVEFROMFS or FS_SERVICE_COPYFROMFS:
    // allows influencing the allowed drop-effects during drag&drop from this FS; if 'allowedEffects' is not
    // NULL, on input it contains the currently allowed drop-effects (combination of DROPEFFECT_MOVE and DROPEFFECT_COPY),
    // on output it contains the drop-effects allowed by this FS (effects should only be removed);
    // 'mode' is 0 when called immediately before starting a drag&drop operation, the effects returned
    // in 'allowedEffects' are used for the DoDragDrop call (applies to the entire drag&drop operation);
    // 'mode' is 1 during mouse dragging over an FS from this process (can be this FS or FS from the other
    // panel); when 'mode' is 1, 'tgtFSPath' contains the target path that will be used if a drop occurs,
    // otherwise 'tgtFSPath' is NULL; 'mode' is 2 when called immediately after the drag&drop
    // operation completes (both successful and unsuccessful)
    virtual void WINAPI GetAllowedDropEffects(int mode, const char* tgtFSPath, DWORD* allowedEffects) = 0;

    // allows the plugin to change the standard message "There are no items in this panel." displayed
    // when there are no items (file/directory/up-dir) in the panel; returns FALSE if
    // the standard message should be used (the return value 'textBuf' is then ignored); returns TRUE
    // if the plugin returns its alternative message in 'textBuf' (buffer of 'textBufSize' characters)
    virtual BOOL WINAPI GetNoItemsInPanelText(char* textBuf, int textBufSize) = 0;

    // only if GetSupportedServices() returns FS_SERVICE_SHOWSECURITYINFO:
    // the user clicked on the security icon (see CSalamanderGeneralAbstract::ShowSecurityIcon;
    // e.g. FTPS displays a dialog with the server certificate); 'parent' is the suggested parent of the dialog
    virtual void WINAPI ShowSecurityInfo(HWND parent) = 0;

    /* remaining to be completed:
// calculate occupied space on FS (Alt+F10 + Ctrl+Shift+F10 + calc. needed space + spacebar key in panel)
#define FS_SERVICE_CALCULATEOCCUPIEDSPACE
// edit from FS (F4)
#define FS_SERVICE_EDITFILE
// edit new file from FS (Shift+F4)
#define FS_SERVICE_EDITNEWFILE
*/
};

//
// ****************************************************************************
// CPluginInterfaceForFSAbstract
//

class CPluginInterfaceForFSAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceForFSEncapsulation)
    friend class CPluginInterfaceForFSEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // function for "file system"; called to open an FS; 'fsName' is the name of the FS
    // to be opened; 'fsNameIndex' is the index of the FS name to be opened
    // (the index is zero for fs-name specified in CSalamanderPluginEntryAbstract::SetBasicPluginData,
    // for other fs-names the index is returned by CSalamanderPluginEntryAbstract::AddFSName);
    // returns a pointer to the interface of the opened FS CPluginFSInterfaceAbstract or
    // NULL on error
    virtual CPluginFSInterfaceAbstract* WINAPI OpenFS(const char* fsName, int fsNameIndex) = 0;

    // function for "file system", called to close an FS, 'fs' is a pointer to
    // the interface of the opened FS, after this call the interface 'fs' is considered
    // invalid in Salamander and will no longer be used (pairs with OpenFS)
    // WARNING: no window or dialog must be opened in this method
    //          (windows can be opened in the CPluginFSInterfaceAbstract::ReleaseObject method)
    virtual void WINAPI CloseFS(CPluginFSInterfaceAbstract* fs) = 0;

    // executes a command on the FS item in the Change Drive menu or in Drive bars
    // (adding it see CSalamanderConnectAbstract::SetChangeDriveMenuItem);
    // 'panel' identifies the panel to work with - for a command from the Change Drive
    // menu, 'panel' is always PANEL_SOURCE (this menu can only be expanded for the active
    // panel), for a command from the Drive bar it can be PANEL_LEFT or PANEL_RIGHT (if
    // two Drive bars are enabled, we can also work with the inactive panel)
    virtual void WINAPI ExecuteChangeDriveMenuItem(int panel) = 0;

    // opens a context menu on the FS item in the Change Drive menu or in Drive
    // bars or for the active/detached FS in the Change Drive menu; 'parent' is the parent
    // of the context menu; 'x' and 'y' are the coordinates for expanding the context menu
    // (the right mouse button click location or suggested coordinates for Shift+F10);
    // if 'pluginFS' is NULL it is an FS item, otherwise 'pluginFS' is the interface
    // of the active/detached FS ('isDetachedFS' is FALSE/TRUE); if 'pluginFS' is not
    // NULL, 'pluginFSName' contains the FS name opened in 'pluginFS' (otherwise
    // 'pluginFSName' is NULL) and 'pluginFSNameIndex' contains the index of the FS name opened
    // in 'pluginFS' (for easier detection of which FS name it is; otherwise
    // 'pluginFSNameIndex' is -1); if it returns FALSE, the other return values are
    // ignored, otherwise they have this meaning: 'refreshMenu' returns TRUE if the
    // Change Drive menu should be refreshed (ignored for Drive bars, because they do not
    // show active/detached FS); 'closeMenu' returns TRUE if the
    // Change Drive menu should be closed (there is nothing to close for Drive bars); if 'closeMenu'
    // returns TRUE and 'postCmd' is non-zero, after closing the Change Drive menu (for Drive bars
    // immediately) ExecuteChangeDrivePostCommand is also called with parameters 'postCmd'
    // and 'postCmdParam'; 'panel' identifies the panel to work with - for
    // a context menu in the Change Drive menu, 'panel' is always PANEL_SOURCE (this menu
    // can only be expanded for the active panel), for a context menu in Drive bars
    // it can be PANEL_LEFT or PANEL_RIGHT (if two Drive bars are enabled, we can
    // also work with the inactive panel)
    virtual BOOL WINAPI ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                       CPluginFSInterfaceAbstract* pluginFS,
                                                       const char* pluginFSName, int pluginFSNameIndex,
                                                       BOOL isDetachedFS, BOOL& refreshMenu,
                                                       BOOL& closeMenu, int& postCmd, void*& postCmdParam) = 0;

    // executes a command from the context menu on the FS item or for the active/detached FS in the
    // Change Drive menu after closing the Change Drive menu or executes a command from the context
    // menu on the FS item in Drive bars (only for compatibility with the Change Drive menu); called
    // as a reaction to the return values 'closeMenu' (TRUE), 'postCmd' and 'postCmdParam'
    // of ChangeDriveMenuItemContextMenu after closing the Change Drive menu (for Drive bars immediately);
    // 'panel' identifies the panel to work with - for a context menu in the Change Drive
    // menu, 'panel' is always PANEL_SOURCE (this menu can only be expanded for the active panel),
    // for a context menu in Drive bars it can be PANEL_LEFT or PANEL_RIGHT (if two
    // Drive bars are enabled, we can also work with the inactive panel)
    virtual void WINAPI ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam) = 0;

    // executes an item in a panel with an open FS (e.g. reaction to the Enter key in the panel;
    // for subdirectories/up-dir (it is an up-dir if the name is ".." and it is the first directory)
    // a path change is expected, for files opening a copy of the file on disk with the possibility
    // of loading any changes back to the FS); execution cannot be performed in the FS interface method,
    // because path change methods cannot be called there (as they may cause FS to be closed);
    // 'panel' specifies the panel where the execution takes place (PANEL_LEFT or PANEL_RIGHT);
    // 'pluginFS' is the interface of the FS open in the panel; 'pluginFSName' is the FS name opened
    // in the panel; 'pluginFSNameIndex' is the index of the FS name opened in the panel (for easier detection
    // of which FS name it is); 'file' is the executed file/directory/up-dir ('isDir' is 0/1/2);
    // WARNING: calling a path change method in the panel may invalidate 'pluginFS' (after FS closure)
    //          and 'file'+'isDir' (listing change in the panel -> destruction of original listing items)
    // NOTE: if a file is being executed or otherwise worked with (e.g. downloaded),
    //       CSalamanderGeneralAbstract::SetUserWorkedOnPanelPath should be called for the
    //       'panel' panel, otherwise the path in this panel will not be inserted into the list of working
    //       directories - List of Working Directories (Alt+F12)
    virtual void WINAPI ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                    const char* pluginFSName, int pluginFSNameIndex,
                                    CFileData& file, int isDir) = 0;

    // performs disconnect of the FS requested by the user in the Disconnect dialog; 'parent' is
    // the parent of any messageboxes (the Disconnect dialog is still open);
    // disconnect cannot be performed in the FS interface method, because the FS is to be destroyed;
    // 'isInPanel' is TRUE if the FS is in the panel, then 'panel' specifies which panel
    // (PANEL_LEFT or PANEL_RIGHT); 'isInPanel' is FALSE if the FS is detached, then
    // 'panel' is 0; 'pluginFS' is the FS interface; 'pluginFSName' is the FS name; 'pluginFSNameIndex'
    // is the index of the FS name (for easier detection of which FS name it is); the method returns FALSE
    // if the disconnect failed and the Disconnect dialog should remain open (its content
    // is refreshed to reflect any previous successful disconnects)
    virtual BOOL WINAPI DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                     CPluginFSInterfaceAbstract* pluginFS,
                                     const char* pluginFSName, int pluginFSNameIndex) = 0;

    // converts the user-part of the path in buffer 'fsUserPart' (size MAX_PATH characters) from external
    // to internal format (e.g. for FTP: internal format = paths as the server works with them,
    // external format = URL format = paths contain hex-escape-sequences (e.g. "%20" = " "))
    virtual void WINAPI ConvertPathToInternal(const char* fsName, int fsNameIndex,
                                              char* fsUserPart) = 0;

    // converts the user-part of the path in buffer 'fsUserPart' (size MAX_PATH characters) from internal
    // to external format
    virtual void WINAPI ConvertPathToExternal(const char* fsName, int fsNameIndex,
                                              char* fsUserPart) = 0;

    // this method is called only for plugins that serve as a replacement for the Network item
    // in the Change Drive menu and in Drive bars (see CSalamanderGeneralAbstract::SetPluginIsNethood()):
    // Salamander by calling this method informs the plugin that the user is changing the path from the root of a UNC
    // path "\\server\share" via the up-dir symbol ("..") to the plugin FS on a path with
    // user-part "\\server" in the panel 'panel' (PANEL_LEFT or PANEL_RIGHT); purpose of this method:
    // the plugin should without waiting list at least this one share on this path, so that
    // it can be focused in the panel (which is the normal behavior when changing paths via up-dir)
    virtual void WINAPI EnsureShareExistsOnServer(int panel, const char* server, const char* share) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_fs)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__

/*   Preliminary version of help for the plugin interface

  Opening, changing, listing and refreshing a path:
    - to open a path in a new FS, ChangePath is called (the first ChangePath call is always for opening a path)
    - to change a path, ChangePath is called (the second and all subsequent ChangePath calls are path changes)
    - on a fatal error, ChangePath returns FALSE (the FS path is not opened in the panel; if it was
      a path change, ChangePath is subsequently called for the original path; if that also fails,
      a transition to a fixed-drive path occurs)
    - if ChangePath returns TRUE (success) and the path was not shortened to the original one (whose
      listing is currently loaded), ListCurrentPath is called to obtain a new listing
    - after successful listing, ListCurrentPath returns TRUE
    - on a fatal error, ListCurrentPath returns FALSE and the subsequent ChangePath call
      must also return FALSE
    - if the current path cannot be listed, ListCurrentPath returns FALSE and the subsequent
      ChangePath call must change the path and return TRUE (ListCurrentPath is called again); if the
      path can no longer be changed (root, etc.), ChangePath also returns FALSE (the FS path is not opened in the panel;
      if it was a path change, ChangePath is subsequently called for the original path; if that also
      fails, a transition to a fixed-drive path occurs)
    - path refresh (Ctrl+R) behaves the same as changing the path to the current path (the path
      may not change at all or may be shortened or in case of a fatal error changed to a
      fixed-drive); during path refresh the parameter 'forceRefresh' is TRUE for all calls of the
      ChangePath and ListCurrentPath methods (FS must not use any cache for path change or listing
      loading - the user does not want to use cache);

  When traversing history (back/forward) the FS interface in which the listing of
  the FS path ('fsName':'fsUserPart') takes place is obtained by the first possible method from the following:
    - the FS interface in which the path was last opened has not yet been closed
      and is among the detached ones or is active in the panel (is not active in the other panel)
    - the active FS interface in the panel ('currentFSName') is from the same plugin as
      'fsName' and returns TRUE for IsOurPath('currentFSName', 'fsName', 'fsUserPart')
    - the first of the detached FS interfaces ('currentFSName') that is from the same
      plugin as 'fsName' and returns TRUE for IsOurPath('currentFSName', 'fsName',
      'fsUserPart')
    - a new FS interface
*/
