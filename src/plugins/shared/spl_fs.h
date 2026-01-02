// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

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

    // jen pokud GetSupportedServices() vraci i FS_SERVICE_ACCEPTSCHANGENOTIF:
    // prijem informace o zmene na ceste 'path' (je-li 'includingSubdirs' TRUE, tak
    // zahrnuje i zmenu v podadresarich cesty 'path'); tato metoda by mela rozhodnout
    // jestli je treba provest refresh tohoto FS (napriklad pomoci metody
    // CSalamanderGeneralAbstract::PostRefreshPanelFS); tyka se jak aktivnich FS, tak
    // odpojenych FS; 'fsName' je aktualni jmeno FS
    // POZNAMKA: pro plugin jako celek existuje metoda
    //           CPluginInterfaceAbstract::AcceptChangeOnPathNotification()
    virtual void WINAPI AcceptChangeOnPathNotification(const char* fsName, const char* path,
                                                       BOOL includingSubdirs) = 0;

    // jen pokud GetSupportedServices() vraci i FS_SERVICE_CREATEDIR:
    // vytvoreni noveho adresare na FS; umozni otevrit vlastni dialog pro vytvoreni
    // adresare (parametr 'mode' je 1) nebo pouzit standardni dialog (pri 'mode'==1 vrati
    // FALSE a 'cancel' take FALSE, pak Salamander otevre standardni dialog a ziskane jmeno
    // adresare preda v 'newName' pri dalsim volani CreateDir s 'mode'==2);
    // 'fsName' je aktualni jmeno FS; 'parent' je navrzeny parent pripadnych zobrazovanych
    // dialogu; 'newName' je jmeno noveho adresare pokud 'mode'==2; pokud vraci TRUE,
    // v 'newName' se vraci jmeno noveho adresare (max. 2 * MAX_PATH znaku; ne plne jmeno,
    // jen jmeno polozky v panelu) - Salamander se ho pokusi vyfokusit po refreshi (o refresh
    // se stara sam FS, napriklad pomoci metody CSalamanderGeneralAbstract::PostRefreshPanelFS);
    // pokud vraci FALSE a 'mode'==2, vraci se v 'newName' chybne jmeno adresare (max. 2 * MAX_PATH
    // znaku, pripadne upravene na absolutni tvar); pokud chce uzivatel prerusit operaci,
    // vraci 'cancel' TRUE; vraci-li 'cancel' FALSE, vraci metoda TRUE pri uspesnem dokonceni
    // operace, pokud vrati FALSE pri 'mode'==1, ma se otevrit standardni dialog pro vytvoreni
    // adresare, pokud vrati FALSE pri 'mode'==2, jde o chybu operace (chybne jmeno
    // adresare se vraci v 'newName' - znovu se otevre standardni dialog a uzivatel
    // zde muze chybne jmeno opravit)
    virtual BOOL WINAPI CreateDir(const char* fsName, int mode, HWND parent,
                                  char* newName, BOOL& cancel) = 0;

    // jen pokud GetSupportedServices() vraci i FS_SERVICE_VIEWFILE:
    // prohlizeni souboru (adresare nelze prohlizet pres funkci View) 'file' na aktualni ceste
    // na FS; 'fsName' je aktualni jmeno FS; 'parent' je parent pripadnych messageboxu
    // s chybami; 'salamander' je sada metod ze Salamandera potrebnych pro implementaci
    // prohlizeni s cachovanim
    virtual void WINAPI ViewFile(const char* fsName, HWND parent,
                                 CSalamanderForViewFileOnFSAbstract* salamander,
                                 CFileData& file) = 0;

    // jen pokud GetSupportedServices() vraci i FS_SERVICE_DELETE:
    // mazani souboru a adresaru oznacenych v panelu; umozni otevrit vlastni dialog s dotazem
    // na mazani (parametr 'mode' je 1; jestli se ma nebo nema zobrazit dotaz zavisi na hodnote
    // SALCFG_CNFRMFILEDIRDEL - TRUE znamena, ze uzivatel chce potvrzovat mazani)
    // nebo pouzit standardni dotaz (pri 'mode'==1 vrati FALSE a 'cancelOrError' take FALSE,
    // pak Salamander otevre standardni dotaz (pokud je SALCFG_CNFRMFILEDIRDEL TRUE)
    // a v pripade kladne odpovedi znovu zavola Delete s 'mode'==2); 'fsName' je aktualni jmeno FS;
    // 'parent' je navrzeny parent pripadnych zobrazovanych dialogu; 'panel' identifikuje panel
    // (PANEL_LEFT nebo PANEL_RIGHT), ve kterem je otevrene FS (z tohoto panelu se ziskavaji
    // soubory/adresare, ktere se maji mazat); 'selectedFiles' + 'selectedDirs' - pocet oznacenych
    // souboru a adresaru, pokud jsou obe hodnoty nulove, maze se soubor/adresar pod kurzorem
    // (fokus), pred volanim metody Delete jsou bud oznacene soubory a adresare nebo je alespon
    // fokus na souboru/adresari, takze je vzdy s cim pracovat (zadne dalsi testy nejsou treba);
    // pokud vraci TRUE a 'cancelOrError' je FALSE, operace probehla korektne a oznacene
    // soubory/adresare se maji odznacit (pokud prezily mazani); pokud chce uzivatel prerusit
    // operaci nebo nastane chyba vraci se 'cancelOrError' TRUE a nedojde k odznaceni
    // souboru/adresaru; pokud vraci FALSE pri 'mode'==1 a 'cancelOrError' je FALSE, ma se
    // otevrit standardni dotaz na mazani
    virtual BOOL WINAPI Delete(const char* fsName, int mode, HWND parent, int panel,
                               int selectedFiles, int selectedDirs, BOOL& cancelOrError) = 0;

    // kopirovani/presun z FS (parametr 'copy' je TRUE/FALSE), v dalsim textu se mluvi jen o
    // kopirovani, ale vse plati shodne i pro presun; 'copy' muze byt TRUE (kopirovani) jen
    // pokud GetSupportedServices() vraci i FS_SERVICE_COPYFROMFS; 'copy' muze byt FALSE
    // (presun nebo prejmenovani) jen pokud GetSupportedServices() vraci i FS_SERVICE_MOVEFROMFS;
    //
    // kopirovani souboru a adresaru (z FS) oznacenych v panelu; umozni otevrit vlastni dialog pro
    // zadani cile kopirovani (parametr 'mode' je 1) nebo pouzit standardni dialog (vrati FALSE
    // a 'cancelOrHandlePath' take FALSE, pak Salamander otevre standardni dialog a ziskanou cilovou
    // cestu preda v 'targetPath' pri dalsim volani CopyOrMoveFromFS s 'mode'==2); pri 'mode'==2
    // je 'targetPath' presne retezec zadany uzivatelem (CopyOrMoveFromFS si ho muze rozanalyzovat
    // po svem); pokud CopyOrMoveFromFS podporuje jen windowsove cilove cesty (nebo nedokaze
    // zpracovat uzivatelem zadanou cestu - napr. vede na jiny FS nebo do archivu), muze vyuzit
    // standardni zpusob zpracovani cesty v Salamanderovi (zatim umi zpracovat jen windowsove cesty,
    // casem mozna zpracuje i FS a archivove cesty pres TEMP adresar pomoci sledu nekolika zakladnich
    // operaci) - vrati FALSE, 'cancelOrHandlePath' TRUE a 'operationMask' TRUE/FALSE
    // (podporuje/nepodporuje operacni masky - pokud nepodporuje a v ceste je maska, zobrazi se
    // chybova hlaska), pak Salamander zpracuje cestu vracenou v 'targetPath' (zatim jen rozdeleni
    // windowsove cesty na existujici cast, neexistujici cast a pripadne masku; umozni take vytvorit
    // podadresare z neexistujici casti) a je-li cesta v poradku, zavola znovu CopyOrMoveFromFS
    // s 'mode'==3 a v 'targetPath' s cilovou cestou a pripadne i s operacni maskou (dva retezce
    // vzajemne oddelene nulou; zadna maska -> dve nuly na konci retezce), pokud je v ceste nejaka
    // chyba, zavola znovu CopyOrMoveFromFS s 'mode'==4 v 'targetPath' s upravenou chybnou cilovou
    // cestou (chyba uz byla uzivateli ohlasena; uzivatel by mel dostat moznost cestu opravit;
    // v ceste mohly byt odstraneny "." a "..", atp.);
    //
    // pokud uzivatel zada operaci drag&dropem (dropne soubory/adresare z FS do stejneho panelu
    // nebo do jineho drop-targetu), je 'mode'==5 a v 'targetPath' je cilova cesta operace (muze
    // byt windowsova cesta, FS cesta a do budoucna se da pocitat i s cestami do archivu),
    // 'targetPath' je ukoncena dvema nulami (pro kompatibilitu s 'mode'==3); 'dropTarget' je
    // v tomto pripade okno drop-targetu (vyuziva se pro reaktivaci drop-targetu po otevreni
    // progress-okna operace, viz CSalamanderGeneralAbstract::ActivateDropTarget); pri 'mode'==5 ma
    // smysl jen navratova hodnota TRUE;
    //
    // 'fsName' je aktualni jmeno FS; 'parent' je navrzeny parent pripadnych zobrazovanych dialogu;
    // 'panel' identifikuje panel (PANEL_LEFT nebo PANEL_RIGHT), ve kterem je otevreny FS (z tohoto
    // panelu se ziskavaji soubory/adresare, ktere se maji kopirovat);
    // 'selectedFiles' + 'selectedDirs' - pocet oznacenych souboru a adresaru, pokud jsou
    // obe hodnoty nulove, kopiruje se soubor/adresar pod kurzorem (fokus), pred volanim
    // metody CopyOrMoveFromFS jsou bud oznacene soubory a adresare nebo je alespon fokus
    // na souboru/adresari, takze je vzdy s cim pracovat (zadne dalsi testy
    // nejsou treba); na vstupu 'targetPath' pri 'mode'==1 obsahuje navrzenou cilovou cestu
    // (jen windowsove cesty bez masky nebo prazdny retezec), pri 'mode'==2 obsahuje retezec
    // cilove cesty zadany uzivatelem ve standardnim dialogu, pri 'mode'==3 obsahuje cilovou
    // cestu a masku (oddelene nulou), pri 'mode'==4 obsahuje chybnou cilovou cestu, pri 'mode'==5
    // obsahuje cilovou cestu (windowsovou, FS nebo do archivu) ukoncenou dvema nulami; pokud
    // metoda vraci FALSE, obsahuje 'targetPath' na vystupu (buffer 2 * MAX_PATH znaku) pri
    // 'cancelOrHandlePath'==FALSE navrzenou cilovou cestu pro standardni dialog a pri
    // 'cancelOrHandlePath'==TRUE retezec cilove cesty ke zpracovani; pokud metoda vraci TRUE a
    // 'cancelOrHandlePath' FALSE, obsahuje 'targetPath' jmeno polozky, na kterou ma prejit fokus
    // ve zdrojovem panelu (buffer 2 * MAX_PATH znaku; ne plne jmeno, jen jmeno polozky v panelu;
    // je-li prazdny retezec, fokus zustava beze zmeny); 'dropTarget' neni NULL jen v pripade
    // zadani cesty operace pomoci drag&dropu (viz popis vyse)
    //
    // pokud vraci TRUE a 'cancelOrHandlePath' je FALSE, operace probehla korektne a oznacene
    // soubory/adresare se maji odznacit; pokud chce uzivatel prerusit operaci nebo nastala
    // chyba, vraci metoda TRUE a 'cancelOrHandlePath' TRUE, v obou pripadech nedojde k odznaceni
    // souboru/adresaru; pokud vraci FALSE, ma se otevrit standardni dialog ('cancelOrHandlePath'
    // je FALSE) nebo se ma standardnim zpusobem zpracovat cesta ('cancelOrHandlePath' je TRUE)
    //
    // POZNAMKA: pokud je nabizena moznost kopirovat/presouvat na cestu do ciloveho panelu,
    //           je treba volat CSalamanderGeneralAbstract::SetUserWorkedOnPanelPath pro cilovy
    //           panel, jinak nebude cesta v tomto panelu vlozena do seznamu pracovnich
    //           adresaru - List of Working Directories (Alt+F12)
    virtual BOOL WINAPI CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                         int panel, int selectedFiles, int selectedDirs,
                                         char* targetPath, BOOL& operationMask,
                                         BOOL& cancelOrHandlePath, HWND dropTarget) = 0;

    // kopirovani/presun z windowsove cesty na FS (parametr 'copy' je TRUE/FALSE), v dalsim textu
    // se mluvi jen o kopirovani, ale vse plati shodne i pro presun; 'copy' muze byt TRUE (kopirovani)
    // jen pokud GetSupportedServices() vraci i FS_SERVICE_COPYFROMDISKTOFS; 'copy' muze byt FALSE
    // (presun nebo prejmenovani) jen pokud GetSupportedServices() vraci i FS_SERVICE_MOVEFROMDISKTOFS;
    //
    // kopirovani vybranych (v panelu nebo jinde) souboru a adresaru na FS; pri 'mode'==1 umoznuje
    // pripravit text cilove cesty pro uzivatele do standardniho dialogu pro kopirovani, jde o situaci,
    // kdy je ve zdrojovem panelu (panel, kde dojde ke spusteni prikazu Copy (klavesa F5)) windowsova
    // cesta a v cilovem panelu tento FS; pri 'mode'==2 a 'mode'==3 muze plugin provest operaci kopirovani nebo
    // ohlasit jednu ze dvou chyb: "chyba v ceste" (napr. obsahuje nepripustne znaky nebo neexistuje)
    // a "v tomto FS nelze provest pozadovanou operaci" (napr. jde sice o FTP, ale otevrena cesta
    // v tomto FS je rozdilna od cilove cesty (napr. u FTP jiny FTP server) - je potreba otevrit
    // jiny/novy FS; tuto chybu nemuze ohlasit nove otevreny FS);
    // POZOR: tato metoda se muze zavolat pro jakoukoliv cilovou FS cestu tohoto pluginu (muze tedy
    //        jit i o cestu s jinym jmenem FS tohoto pluginu)
    //
    // 'fsName' je aktualni jmeno FS; 'parent' je navrzeny parent pripadnych zobrazovanych
    // dialogu; 'sourcePath' je zdrojova windowsova cesta (vsechny vybrane soubory a adresare
    // jsou adresovany relativne k ni), pri 'mode'==1 je NULL; vybrane soubory a adresare
    // jsou zadany enumeracni funkci 'next' jejimz parametrem je 'nextParam', pri 'mode'==1
    // jsou NULL; 'sourceFiles' + 'sourceDirs' - pocet vybranych souboru a adresaru (soucet
    // je vzdy nenulovy); 'targetPath' je in/out buffer min. 2 * MAX_PATH znaku pro cilovou
    // cestu; pri 'mode'==1 je 'targetPath' na vstupu aktualni cesta na tomto FS a na vystupu cilova
    // cesta pro standardni dialog kopirovani; pri 'mode'==2 je 'targetPath' na vstupu uzivatelem
    // zadana cilova cesta (bez uprav, vcetne masky, atd.) a na vystupu se ignoruje az na pripad, kdy
    // metoda vraci FALSE (chyba) a 'invalidPathOrCancel' TRUE (chyba v ceste), v tomto pripade je na
    // vystupu upravena cilova cesta (napr. odstranene "." a ".."), kterou bude uzivatel opravovat
    // ve standardnim dialogu kopirovani; pri 'mode'==3 je 'targetPath' na vstupu drag&dropem
    // zadana cilova cesta a na vystupu se ignoruje; neni-li 'invalidPathOrCancel' NULL (jen 'mode'==2
    // a 'mode'==3), vraci se v nem TRUE, pokud je cesta spatne zadana (obsahuje nepripustne znaky nebo
    // neexistuje, atd.) nebo doslo k preruseni operace (cancel) - chybove/cancel hlaseni se zobrazuje
    // pred ukoncenim teto metody
    //
    // pri 'mode'==1 vraci metoda TRUE pri uspechu, pokud vraci FALSE, pouzije se jako cilova cesta
    // pro standardni dialog kopirovani prazdny retezec; pokud vraci metoda FALSE pri 'mode'==2 a 'mode'==3,
    // ma se pro zpracovani operace hledat jiny FS (je-li 'invalidPathOrCancel' FALSE) nebo ma
    // uzivatel opravit cilovou cestu (je-li 'invalidPathOrCancel' TRUE); pokud vraci metoda TRUE
    // pri 'mode'==2 nebo 'mode'==3, operace probehla a ma dojit k odznaceni vybranych souboru a adresaru
    // (je-li 'invalidPathOrCancel' FALSE) nebo doslo k chybe/preruseni operace a nema dojit
    // k odznaceni vybranych souboru a adresaru (je-li 'invalidPathOrCancel' TRUE)
    //
    // POZOR: Metoda CopyOrMoveFromDiskToFS se muze volat ve trech situacich:
    //        - tento FS je aktivni v panelu
    //        - tento FS je odpojeny
    //        - tento FS prave vzniknul (volanim OpenFS) a po ukonceni metody zase ihned zanikne
    //          (volanim CloseFS) - nebyla od nej volana zadna jina metoda (ani ChangePath)
    virtual BOOL WINAPI CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                               const char* sourcePath, SalEnumSelection2 next,
                                               void* nextParam, int sourceFiles, int sourceDirs,
                                               char* targetPath, BOOL* invalidPathOrCancel) = 0;

    // jen pokud GetSupportedServices() vraci i FS_SERVICE_CHANGEATTRS:
    // zmena atributu souboru a adresaru oznacenych v panelu; dialog se zadanim zmen atributu
    // ma kazdy plugin vlastni;
    // 'fsName' je aktualni jmeno FS; 'parent' je navrzeny parent vlastniho dialogu; 'panel'
    // identifikuje panel (PANEL_LEFT nebo PANEL_RIGHT), ve kterem je otevrene FS (z tohoto
    // panelu se ziskavaji soubory/adresare, se kterymi se pracuje);
    // 'selectedFiles' + 'selectedDirs' - pocet oznacenych souboru a adresaru,
    // pokud jsou obe hodnoty nulove, pracuje se se souborem/adresarem pod kurzorem
    // (fokus), pred volanim metody ChangeAttributes jsou bud oznacene soubory a adresare nebo
    // je alespon fokus na souboru/adresari, takze je vzdy s cim pracovat (zadne dalsi testy
    // nejsou treba); pokud vraci TRUE, operace probehla korektne a oznacene soubory/adresare
    // se maji odznacit; pokud chce uzivatel prerusit operaci nebo nastane chyba, vraci metoda
    // FALSE a nedojde k odznaceni souboru/adresaru
    virtual BOOL WINAPI ChangeAttributes(const char* fsName, HWND parent, int panel,
                                         int selectedFiles, int selectedDirs) = 0;

    // jen pokud GetSupportedServices() vraci i FS_SERVICE_SHOWPROPERTIES:
    // zobrazeni okna s vlastnostmi souboru a adresaru oznacenych v panelu; okno s vlastnostmi
    // ma kazdy plugin vlastni;
    // 'fsName' je aktualni jmeno FS; 'parent' je navrzeny parent vlastniho okna
    // (Windowsove okno s vlastnostmi je nemodalni - pozor: nemodalni okno musi
    // mit vlastni thread); 'panel' identifikuje panel (PANEL_LEFT nebo PANEL_RIGHT),
    // ve kterem je otevrene FS (z tohoto panelu se ziskavaji soubory/adresare,
    // se kterymi se pracuje); 'selectedFiles' + 'selectedDirs' - pocet oznacenych
    // souboru a adresaru, pokud jsou obe hodnoty nulove, pracuje se se souborem/adresarem
    // pod kurzorem (fokus), pred volanim metody ShowProperties jsou bud oznacene
    // soubory a adresare nebo je alespon fokus na souboru/adresari, takze je vzdy
    // s cim pracovat (zadne dalsi testy nejsou treba)
    virtual void WINAPI ShowProperties(const char* fsName, HWND parent, int panel,
                                       int selectedFiles, int selectedDirs) = 0;

    // jen pokud GetSupportedServices() vraci i FS_SERVICE_CONTEXTMENU:
    // zobrazeni kontextoveho menu pro soubory a adresare oznacene v panelu (kliknuti pravym
    // tlacitkem mysi na polozkach v panelu) nebo pro aktualni cestu v panelu (kliknuti pravym
    // tlacitkem mysi na change-drive tlacitku v panelove toolbare) nebo pro panel (kliknuti
    // pravym tlacitkem mysi za polozkami v panelu); kontextove menu ma kazdy plugin vlastni;
    //
    // 'fsName' je aktualni jmeno FS; 'parent' je navrzeny parent kontextoveho menu;
    // 'menuX' + 'menuY' jsou navrzene souradnice leveho horniho rohu kontextoveho menu;
    // 'type' je typ kontextoveho menu (viz popisy konstant fscmXXX); 'panel'
    // identifikuje panel (PANEL_LEFT nebo PANEL_RIGHT), pro ktery se ma kontextove
    // menu otevrit (z tohoto panelu se ziskavaji soubory/adresare/cesta, se kterymi
    // se pracuje); pri 'type'==fscmItemsInPanel je 'selectedFiles' + 'selectedDirs'
    // pocet oznacenych souboru a adresaru, pokud jsou obe hodnoty nulove, pracuje se se
    // souborem/adresarem pod kurzorem (focusem), pred volanim metody ContextMenu jsou bud
    // oznacene soubory a adresare (a bylo na nich kliknuto) nebo je alespon fokus na
    // souboru/adresari (neni na updiru), takze je vzdy s cim pracovat (zadne dalsi testy
    // nejsou treba); pokud 'type'!=fscmItemsInPanel, 'selectedFiles' + 'selectedDirs'
    // jsou vzdy nastaveny na nulu (ignoruji se)
    virtual void WINAPI ContextMenu(const char* fsName, HWND parent, int menuX, int menuY, int type,
                                    int panel, int selectedFiles, int selectedDirs) = 0;

    // jen pokud GetSupportedServices() vraci i FS_SERVICE_CONTEXTMENU:
    // pokud je v panelu otevreny FS a dorazi nektera ze zprav WM_INITPOPUP, WM_DRAWITEM,
    // WM_MENUCHAR nebo WM_MEASUREITEM, zavola Salamander HandleMenuMsg, aby pluginu
    // umoznil pracovat s IContextMenu2 a IContextMenu3
    // plugin vraci TRUE v pripade, ze zpravu zpracoval a FALSE jindy
    virtual BOOL WINAPI HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) = 0;

    // jen pokud GetSupportedServices() vraci i FS_SERVICE_OPENFINDDLG:
    // otevreni Find dialogu pro FS v panelu; 'fsName' je aktualni jmeno FS; 'panel' identifikuje
    // panel (PANEL_LEFT nebo PANEL_RIGHT), pro ktery se ma otevrit Find dialog (z tohoto panelu
    // se ziskava obvykle cesta pro hledani); vraci TRUE pri uspesnem otevreni Find dialogu;
    // pokud vrati FALSE, Salamander otevre standardni Find Files and Directories dialog
    virtual BOOL WINAPI OpenFindDialog(const char* fsName, int panel) = 0;

    // jen pokud GetSupportedServices() vraci i FS_SERVICE_OPENACTIVEFOLDER:
    // otevre okno Explorera pro aktualni cestu v panelu
    // 'fsName' je aktualni jmeno FS; 'parent' je navrzeny parent zobrazovaneho dialogu
    virtual void WINAPI OpenActiveFolder(const char* fsName, HWND parent) = 0;

    // jen pokud GetSupportedServices() vraci FS_SERVICE_MOVEFROMFS nebo FS_SERVICE_COPYFROMFS:
    // dovoluje ovlivnit povolene drop-effecty pri drag&dropu z tohoto FS; neni-li 'allowedEffects'
    // NULL, obsahuje na vstupu dosud povolene drop-effecty (kombinace DROPEFFECT_MOVE a DROPEFFECT_COPY),
    // na vystupu obsahuje drop-effecty povolene timto FS (effecty by se mely jen ubirat);
    // 'mode' je 0 pri volani, ktere tesne predchazi zahajeni drag&drop operace, effecty vracene
    // v 'allowedEffects' se pouziji pro volani DoDragDrop (tyka se cele drag&drop operace);
    // 'mode' je 1 behem tazeni mysi nad FS z tohoto procesu (muze byt toto FS nebo FS z druheho
    // panelu); pri 'mode' 1 je v 'tgtFSPath' cilova cesta, ktera se pouzije pokud dojde k dropu,
    // jinak je 'tgtFSPath' NULL; 'mode' je 2 pri volani, ktere tesne nasleduje po dokonceni
    // drag&drop operace (uspesnemu i neuspesnemu)
    virtual void WINAPI GetAllowedDropEffects(int mode, const char* tgtFSPath, DWORD* allowedEffects) = 0;

    // umoznuje pluginu zmenit standardni hlaseni "There are no items in this panel." zobrazovane
    // v situaci, kdy v panelu neni zadna polozka (soubor/adresar/up-dir); vraci FALSE pokud
    // se ma pouzit standardni hlaseni (navratova hodnota 'textBuf' se pak ignoruje); vraci TRUE
    // pokud plugin v 'textBuf' (buffer o velikosti 'textBufSize' znaku) vraci svou alternativu
    // teto hlasky
    virtual BOOL WINAPI GetNoItemsInPanelText(char* textBuf, int textBufSize) = 0;

    // jen pokud GetSupportedServices() vraci FS_SERVICE_SHOWSECURITYINFO:
    // uzivatel kliknul na ikone zabezpeceni (viz CSalamanderGeneralAbstract::ShowSecurityIcon;
    // napr. FTPS zobrazi dialog s certifikatem serveru); 'parent' je navrzeny parent dialogu
    virtual void WINAPI ShowSecurityInfo(HWND parent) = 0;

    /* zbyva dokoncit:
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
private: // ochrana proti nespravnemu primemu volani metod (viz CPluginInterfaceForFSEncapsulation)
    friend class CPluginInterfaceForFSEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // funkce pro "file system"; vola se pro otevreni FS; 'fsName' je jmeno FS,
    // ktery se ma otevrit; 'fsNameIndex' je index jmena FS, ktery se ma otevrit
    // (index je nula pro fs-name zadane v CSalamanderPluginEntryAbstract::SetBasicPluginData,
    // u ostatnich fs-name index vraci CSalamanderPluginEntryAbstract::AddFSName);
    // vraci ukazatel na interface otevreneho FS CPluginFSInterfaceAbstract nebo
    // NULL pri chybe
    virtual CPluginFSInterfaceAbstract* WINAPI OpenFS(const char* fsName, int fsNameIndex) = 0;

    // funkce pro "file system", vola se pro uzavreni FS, 'fs' je ukazatel na
    // interface otevreneho FS, po tomto volani uz je v Salamanderu interface 'fs'
    // povazovan za neplatny a nebude dale pouzivan (funkce paruje s OpenFS)
    // POZOR: v teto metode nesmi dojit k otevreni zadneho okna ani dialogu
    //        (okna lze otevirat v metode CPluginFSInterfaceAbstract::ReleaseObject)
    virtual void WINAPI CloseFS(CPluginFSInterfaceAbstract* fs) = 0;

    // provedeni prikazu na polozce pro FS v Change Drive menu nebo v Drive barach
    // (jeji pridani viz CSalamanderConnectAbstract::SetChangeDriveMenuItem);
    // 'panel' identifikuje panel, se kterym mame pracovat - pro prikaz z Change Drive
    // menu je 'panel' vzdy PANEL_SOURCE (toto menu muze byt vybaleno jen u aktivniho
    // panelu), pro prikaz z Drive bary muze byt PANEL_LEFT nebo PANEL_RIGHT (pokud
    // jsou zapnute dve Drive bary, muzeme pracovat i s neaktivnim panelem)
    virtual void WINAPI ExecuteChangeDriveMenuItem(int panel) = 0;

    // otevreni kontextoveho menu na polozce pro FS v Change Drive menu nebo v Drive
    // barach nebo pro aktivni/odpojeny FS v Change Drive menu; 'parent' je parent
    // kontextoveho menu; 'x' a 'y' jsou souradnice pro vybaleni kontextoveho menu
    // (misto kliknuti praveho tlacitka mysi nebo navrzene souradnice pri Shift+F10);
    // je-li 'pluginFS' NULL jde o polozku pro FS, jinak je 'pluginFS' interface
    // aktivniho/odpojeneho FS ('isDetachedFS' je FALSE/TRUE); neni-li 'pluginFS'
    // NULL, je v 'pluginFSName' jmeno FS otevreneho v 'pluginFS' (jinak je v
    // 'pluginFSName' NULL) a v 'pluginFSNameIndex' index jmena FS otevreneho
    // v 'pluginFS' (pro snazsi detekci o jake jmeno FS jde; jinak je v
    // 'pluginFSNameIndex' -1); pokud vrati FALSE, jsou ostatni navratove hodnoty
    // ignorovany, jinak maji tento vyznam: v 'refreshMenu' vraci TRUE pokud se ma
    // provest obnova Change Drive menu (pro Drive bary se ignoruje, protoze se na
    // nich neukazuji aktivni/odpojene FS); v 'closeMenu' vraci TRUE pokud se ma
    // zavrit Change Drive menu (pro Drive bary neni co zavirat); vraci-li 'closeMenu'
    // TRUE a 'postCmd' neni nula, je po zavreni Change Drive menu (pro Drive bary
    // ihned) jeste zavolano ExecuteChangeDrivePostCommand s parametry 'postCmd'
    // a 'postCmdParam'; 'panel' identifikuje panel, se kterym mame pracovat - pro
    // kontextove menu v Change Drive menu je 'panel' vzdy PANEL_SOURCE (toto menu
    // muze byt vybaleno jen u aktivniho panelu), pro kontextove menu v Drive barach
    // muze byt PANEL_LEFT nebo PANEL_RIGHT (pokud jsou zapnute dve Drive bary, muzeme
    // pracovat i s neaktivnim panelem)
    virtual BOOL WINAPI ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                       CPluginFSInterfaceAbstract* pluginFS,
                                                       const char* pluginFSName, int pluginFSNameIndex,
                                                       BOOL isDetachedFS, BOOL& refreshMenu,
                                                       BOOL& closeMenu, int& postCmd, void*& postCmdParam) = 0;

    // provedeni prikazu z kontextoveho menu na polozce pro FS nebo pro aktivni/odpojeny FS v
    // Change Drive menu az po zavreni Change Drive menu nebo provedeni prikazu z kontextoveho
    // menu na polozce pro FS v Drive barach (jen pro kompatibilitu s Change Drive menu); vola
    // se jako reakce na navratove hodnoty 'closeMenu' (TRUE), 'postCmd' a 'postCmdParam'
    // ChangeDriveMenuItemContextMenu po zavreni Change Drive menu (pro Drive bary ihned);
    // 'panel' identifikuje panel, se kterym mame pracovat - pro kontextove menu v Change Drive
    // menu je 'panel' vzdy PANEL_SOURCE (toto menu muze byt vybaleno jen u aktivniho panelu),
    // pro kontextove menu v Drive barach muze byt PANEL_LEFT nebo PANEL_RIGHT (pokud jsou
    // zapnute dve Drive bary, muzeme pracovat i s neaktivnim panelem)
    virtual void WINAPI ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam) = 0;

    // provede spusteni polozky v panelu s otevrenym FS (napr. reakce na klavesu Enter v panelu;
    // u podadresaru/up-diru (o up-dir jde pokud je jmeno ".." a zaroven jde o prvni adresar)
    // se predpoklada zmena cesty, u souboru otevreni kopie souboru na disku s tim, ze se pripadne
    // zmeny nactou zpet na FS); spousteni neni mozne provest v metode FS interfacu, protoze tam
    // nelze volat metody pro zmenu cesty (muze v nich totiz dojit i k zavreni FS);
    // 'panel' urcuje panel, ve kterem probiha spousteni (PANEL_LEFT nebo PANEL_RIGHT);
    // 'pluginFS' je interface FS otevreneho v panelu; 'pluginFSName' je jmeno FS otevreneho
    // v panelu; 'pluginFSNameIndex' je index jmena FS otevreneho v panelu (pro snazsi detekci
    // o jake jmeno FS jde); 'file' je spousteny soubor/adresar/up-dir ('isDir' je 0/1/2);
    // POZOR: volani metody pro zmenu cesty v panelu muze zneplatnit 'pluginFS' (po zavreni FS)
    //        a 'file'+'isDir' (zmena listingu v panelu -> zruseni polozek puvodniho listingu)
    // POZNAMKA: pokud se spousti soubor nebo se s nim jinak pracuje (napr. downloadi se),
    //           je treba volat CSalamanderGeneralAbstract::SetUserWorkedOnPanelPath pro panel
    //           'panel', jinak nebude cesta v tomto panelu vlozena do seznamu pracovnich
    //           adresaru - List of Working Directories (Alt+F12)
    virtual void WINAPI ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                    const char* pluginFSName, int pluginFSNameIndex,
                                    CFileData& file, int isDir) = 0;

    // provede disconnect FS, o ktery zadazal uzivatel v Disconnect dialogu; 'parent' je
    // parent pripadnych messageboxu (jde o stale jeste otevreny Disconnect dialog);
    // disconnect neni mozne provest v metode FS interfacu, protoze FS ma zaniknout;
    // 'isInPanel' je TRUE pokud je FS v panelu, pak 'panel' urcuje ve kterem panelu
    // (PANEL_LEFT nebo PANEL_RIGHT); 'isInPanel' je FALSE pokud je FS odpojeny, pak je
    // 'panel' 0; 'pluginFS' je interface FS; 'pluginFSName' je jmeno FS; 'pluginFSNameIndex'
    // je index jmena FS (pro snazsi detekci o jake jmeno FS jde); metoda vraci FALSE,
    // pokud se disconnect nepodaril a Disconnect dialog ma zustat otevreny (jeho obsah
    // se obnovi, aby se projevily predchozi uspesne disconnecty)
    virtual BOOL WINAPI DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                     CPluginFSInterfaceAbstract* pluginFS,
                                     const char* pluginFSName, int pluginFSNameIndex) = 0;

    // prevadi user-part cesty v bufferu 'fsUserPart' (velikost MAX_PATH znaku) z externiho
    // na interni format (napr. u FTP: interni format = cesty jak s nimi pracuje server,
    // externi format = URL format = cesty obsahuji hex-escape-sekvence (napr. "%20" = " "))
    virtual void WINAPI ConvertPathToInternal(const char* fsName, int fsNameIndex,
                                              char* fsUserPart) = 0;

    // prevadi user-part cesty v bufferu 'fsUserPart' (velikost MAX_PATH znaku) z interniho
    // na externi format
    virtual void WINAPI ConvertPathToExternal(const char* fsName, int fsNameIndex,
                                              char* fsUserPart) = 0;

    // tato metoda se vola jen u pluginu, ktery slouzi jako nahrada za Network polozku
    // v Change Drive menu a v Drive barach (viz CSalamanderGeneralAbstract::SetPluginIsNethood()):
    // Salamander volanim teto metody informuje plugin, ze uzivatel meni cestu z rootu UNC
    // cesty "\\server\share" pres symbol up-diru ("..") do pluginoveho FS na cestu s
    // user-part "\\server" v panelu 'panel' (PANEL_LEFT nebo PANEL_RIGHT); ucel teto metody:
    // plugin by mel bez cekani vylistovat na teto ceste aspon tento jeden share, aby mohlo
    // dojit k jeho fokusu v panelu (coz je bezne chovani pri zmene cesty pres up-dir)
    virtual void WINAPI EnsureShareExistsOnServer(int panel, const char* server, const char* share) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_fs)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__

/*   Predbezna verze helpu k pluginovemu interfacu

  Otevreni, zmena, listovani a refresh cesty:
    - pro otevreni cesty v novem FS se vola ChangePath (prvni volani ChangePath je vzdy pro otevreni cesty)
    - pro zmenu cesty se vola ChangePath (druhe a vsechny dalsi volani ChangePath jsou zmeny cesty)
    - pri fatalni chybe ChangePath vraci FALSE (FS cesta se v panelu neotevre, pokud slo
      o zmenu cesty, zkusi se nasledne volat ChangePath pro puvodni cestu, pokud ani to nevyjde,
      dojde k prechodu na fixed-drive cestu)
    - pokud ChangePath vrati TRUE (uspech) a nedoslo ke zkraceni cesty na puvodni (jejiz
      listing je prave nacteny), vola se ListCurrentPath pro ziskani noveho listingu
    - po uspesnem vylistovani vraci ListCurrentPath TRUE
    - pri fatalni chybe vraci ListCurrentPath FALSE a nasledne volani ChangePath
      musi take vratit FALSE
    - pokud aktualni cesta nejde vylistovat, vraci ListCurrentPath FALSE a nasledne volani
      ChangePath musi cestu zmenit a vratit TRUE (zavola se znovu ListCurrentPath), pokud jiz
      cestu nejde zmenit (root, atp.), vrati ChangePath take FALSE (FS cesta se v panelu neotevre,
      pokud slo o zmenu cesty, zkusi se nasledne volat ChangePath pro puvodni cestu, pokud ani to
      nevyjde, dojde k prechodu na fixed-drive cestu)
    - refresh cesty (Ctrl+R) se chova stejne jako zmena cesty na aktualni cestu (cesta
      se vubec nemusi zmenit nebo se muze zkratit nebo v pripade fatalni chyby zmenit na
      fixed-drive); pri refreshi cesty je parametr 'forceRefresh' TRUE pro vsechna volani metod
      ChangePath a ListCurrentPath (FS nesmi pouzit pro zmenu cesty ani nacteni listingu zadne
      cache - user nechce pouzivat cache);

  Pri pruchodu historii (back/forward) se FS interface, ve kterem probehne vylistovani
  FS cesty ('fsName':'fsUserPart'), ziska prvnim moznym zpusobem z nasledujicich:
    - FS interface, ve kterem byla cesta naposledy otevrena jeste nebyl zavren
      a je mezi odpojenymi nebo je aktivni v panelu (neni aktivni v druhem panelu)
    - aktivni FS interface v panelu ('currentFSName') je ze stejneho pluginu jako
      'fsName' a vrati na IsOurPath('currentFSName', 'fsName', 'fsUserPart') TRUE
    - prvni z odpojenych FS interfacu ('currentFSName'), ktery je ze stejneho
      pluginu jako 'fsName' a vrati na IsOurPath('currentFSName', 'fsName',
      'fsUserPart') TRUE
    - novy FS interface
*/
