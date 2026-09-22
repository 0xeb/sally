// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include "common/unicode/WideTextRange.h"
#include "registry_names.h"

// current configuration version (see main_window_config_persistence.cpp for description)
extern const DWORD THIS_CONFIG_VERSION;

// Version expiration: uncomment for beta and PB builds; comment out for release builds:
//#define USE_BETA_EXPIRATION_DATE

// For PB (EAP) builds uncomment; for other builds keep commented:
//#define THIS_IS_EAP_VERSION

#ifdef USE_BETA_EXPIRATION_DATE

// specifies the first day when this beta version stops running
extern SYSTEMTIME BETA_EXPIRATION_DATE;

#endif // USE_BETA_EXPIRATION_DATE

// DEBUG builds only: enables debugging of bug report creation (by default no bug report is created;
// the exception is passed to the MSVC debugger)
//#define ENABLE_BUGREPORT_DEBUGGING   1

// used to detect whether the wheel message came through a hook or directly
extern BOOL MouseWheelMSGThroughHook; // TRUE: message went through a hook at MouseWheelMSGTime; FALSE: through the panel at MouseWheelMSGTime
extern DWORD MouseWheelMSGTime;       // timestamp of the last message
#define MOUSEWHEELMSG_VALID 100       // [ms] number of milliseconds one channel (hook vs window) remains valid

enum
{
    otViewerWindow = 10,
};

// support for horizontal scroll (works on W2K/XP with Intellipoint drivers; officially supported since Vista)
#define WM_MOUSEHWHEEL 0x020E
BOOL PostMouseWheelMessage(MSG* pMSG);

// Tries to determine with high probability (not guaranteed) whether Salamander will not be "busy"
// in the next moments (no modal dialog open and no messages being processed). In that case returns TRUE,
// otherwise FALSE. If 'lastIdleTime' is not NULL, it receives the GetTickCount() from the moment of the last
// transition from "idle" to "busy" state. Can be called from any thread.
BOOL SalamanderIsNotBusy(DWORD* lastIdleTime);

// Opens Salamander or plug-in HTML help. The help language (directory with .chm files) is chosen as follows:
// - directory obtained from the current Salamander .slg file (see SLGHelpDir in shared\versinfo.rc)
// - HELP\ENGLISH\*.chm
// - the first subdirectory found under HELP
// 'helpFileName' is the .chm file name to work with (file name without path). If NULL, "sally.chm" is used.
// 'parent' is the parent for any error message box; 'command' is the HTML Help command, see HHCDisplayXXX;
// 'dwData' is the parameter for the HTML Help command, see HHCDisplayXXX. Can be called from any thread.
// If 'quiet' is TRUE, no error message is shown. Returns TRUE on success, otherwise FALSE.
BOOL OpenHtmlHelp(const wchar_t* helpFileName, HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData, BOOL quiet);

extern CRITICAL_SECTION OpenHtmlHelpCS; // critical section for OpenHtmlHelp()

/* Simple way to ensure execution within a critical section, usage example:
  static CCriticalSection cs;
  CEnterCriticalSection enterCS(cs);
*/

class CCriticalSection
{
public:
    CRITICAL_SECTION cs;

    CCriticalSection() { InitializeCriticalSection(&cs); }
    ~CCriticalSection() { DeleteCriticalSection(&cs); }

    void Enter() { EnterCriticalSection(&cs); }
    void Leave() { LeaveCriticalSection(&cs); }
};

class CEnterCriticalSection
{
protected:
    CCriticalSection* CS;

public:
    CEnterCriticalSection(CCriticalSection& cs)
    {
        CS = &cs;
        CS->Enter();
    }

    ~CEnterCriticalSection()
    {
        CS->Leave();
    }
};

// Because Windows GetTempFileName doesn't work properly, we wrote our own clone.
// 2026-08-25: the narrow SalGetTempFileName(char*, ...) was deleted -
// confirmed-dead (zero callers; the legacy v107 ABI shim forwards to WideGeneral.SalGetTempFileName,
// never to the free narrow function). Creates a file/directory (based on 'file') at path 'path'
// (empty -> Windows TEMP dir), with prefix 'prefix', returns the path of the created file/directory,
// or an empty string on failure (SetLastError carries the Windows error code, for compatibility).
std::wstring SalGetTempFileNameW(const wchar_t* path, const wchar_t* prefix, bool file);

// Because Windows MoveFile can't rename files with read-only attribute on Novell,
// we wrote our own (if an error occurs during MoveFile, it tries to clear read-only, perform the operation,
// and then set it back)
BOOL SalMoveFile(const wchar_t* srcName, const wchar_t* destName);

// Variant of Windows GetFileSize (has simpler error handling); 'file' is an open
// file for calling GetFileSize(); returns the obtained file size in 'size'; returns success,
// on FALSE (error) 'err' contains Windows error code and 'size' is zero
BOOL SalGetFileSize(HANDLE file, CQuadWord& size, DWORD& err);
BOOL SalGetFileSize2(const wchar_t* fileName, CQuadWord& size, DWORD* err); // 'err' can be NULL if we don't care

struct COperation;

// Gets the size of the file that symlink 'fileName' points to; if 'op' is not NULL,
// on cancel the contents of 'op' are freed; if 'fileName' is NULL, 'op->SourceName' is used;
// returns the size in 'size'; 'ignoreAll' is in + out, if TRUE all errors are ignored
// (must be set to FALSE before the action, otherwise the error window won't show at all, then don't
// change it); on error displays a standard dialog with Retry / Ignore / Ignore All / Cancel
// with parent 'parent'; if size is successfully obtained, returns TRUE; on error and pressing
// Ignore / Ignore All button in the error window, returns FALSE and 'cancel' returns FALSE;
// if 'ignoreAll' is TRUE, the window is not shown, no button press is awaited, behaves as if
// user pressed Ignore; on error and pressing Cancel in the error window returns FALSE and
// 'cancel' returns TRUE
BOOL GetLinkTgtFileSize(HWND parent, const wchar_t* fileName, COperation* op, CQuadWord* size, // wide
                        BOOL* cancel, BOOL* ignoreAll);

// Because Windows GetFileAttributes can't work with names ending in space/dot,
// we wrote our own (for these names adds backslash at the end, which makes
// GetFileAttributes work correctly, but only for directories, for files with space/dot at
// the end we have no solution, but at least it doesn't get info from a different file - Windows version
// trims spaces/dots and thus works with a different file/directory)
DWORD SalGetFileAttributes(const wchar_t* fileName);

// If file/directory 'name' has read-only attribute, we try to clear it
// (reason: e.g. so it can be deleted via DeleteFile); if we already have 'name' attributes
// loaded, pass them in 'attr', if 'attr' is -1, attributes are read from disk;
// returns TRUE if an attempt to change the attribute is made (success is not checked)
// NOTE: only clears read-only attribute, so that in case of multiple hardlinks there's no
// unnecessarily large attribute change on remaining hardlinks of the file (attributes
// are shared by all hardlinks)
BOOL ClearReadOnlyAttr(const wchar_t* name, DWORD attr = -1);

// Deletes a directory link (junction point, symbolic link, mount point); on success
// returns TRUE; on error returns FALSE and if 'err' is not NULL, returns error code in 'err'
BOOL DeleteDirLink(const wchar_t* name, DWORD* err);

// Returns TRUE if path 'path' is on a NOVELL volume (used to detect whether
// fast-directory-move can be used)
// The W form is the real one - it matches against the WIDE network
// enumeration. The ANSI form re-widens its argument, which is honest only because a
// path that reached an ANSI caller was already spellable.
BOOL IsNOVELLDriveW(const wchar_t* path);

// Returns TRUE if path 'path' is on a LANTASTIC volume (used to detect whether
// file size needs to be checked after copying); for optimization purposes
// 'lastLantasticCheckRoot' is used (for first call "", then don't change)
// and 'lastIsLantasticPath' (result for 'lastLantasticCheckRoot')
// Wide only - its one caller (worker.cpp) has wide target paths.
BOOL IsLantasticDriveW(const wchar_t* path, std::wstring& lastLantasticCheckRoot,
                       BOOL& lastIsLantasticPath);

// Returns TRUE if 'path' is on a volume that supports ADS (or an error occurred while
// determining the file system type) and we're on NT/W2K/XP; if 'isFAT32' is not NULL,
// returns TRUE in it if 'path' is on a FAT32 volume; returns FALSE only if it's
// certain that FS doesn't support ADS
// 2026-08-26: the narrow IsPathOnVolumeSupADS(char*, ...) was deleted -
// confirmed-dead (zero callers anywhere). IsPathOnVolumeSupADSW is the real, widely-used
// implementation.
BOOL IsPathOnVolumeSupADSW(const wchar_t* path, BOOL* isFAT32); // wide sibling

// Test if this is Samba (Linux support for disk sharing with Windows)
// 2026-08-26: the narrow IsSambaDrivePath(char*) thin wrapper was deleted -
// confirmed-dead (zero callers anywhere). IsSambaDrivePathW is the real implementation.
BOOL IsSambaDrivePathW(const wchar_t* path);

// Test if this is a UNC path (detects both formats: \\server\share and \\?\UNC\server\share)

// 2026-08-25: the narrow IsUNCRootPath(char*) was deleted - confirmed-dead (zero
// callers anywhere). IsUNCRootPathW (common/fsutil.h) is the real, widely-used implementation.

// Creates a file named 'fileName' via classic Win32 API call
// CreateFile (lpSecurityAttributes==NULL, dwCreationDisposition==CREATE_NEW,
// hTemplateFile==NULL); this method resolves collision of 'fileName' with DOS name
// of already existing file/directory (only if it's not also a collision with long
// file/directory name) - ensures DOS name change so the file with name 'fileName'
// can be created (method: temporarily renames conflicting file/directory to another
// name and after creating 'fileName' renames it back);
// returns file handle or INVALID_HANDLE_VALUE on error (error is in GetLastError());
// if 'encryptionNotSupported' is not NULL and file can't be opened with Encrypted
// attribute, tries to open without Encrypted attribute, if that succeeds,
// file is deleted and TRUE is written to 'encryptionNotSupported' - return value
// and GetLastError() contain the "original" error (opening with Encrypted attribute)
HANDLE SalCreateFileEx(const wchar_t* fileName, DWORD desiredAccess, // wide
                       DWORD shareMode, DWORD flagsAndAttributes,
                       BOOL* encryptionNotSupported);

// Trims spaces from beginning and end of name (CutWS or StripWS or CutWhiteSpace or StripWhiteSpace)
// returns TRUE if trimming occurred
// 2026-08-26: the narrow CutSpacesFromBothSides(char*) was deleted - confirmed-dead
// (zero callers anywhere). CutSpacesFromBothSidesW is the real, widely-used implementation.
BOOL CutSpacesFromBothSidesW(wchar_t* path);

// Trims spaces from beginning and spaces and dots from end of name, Explorer does this
// and people insisted they want it too, see https://forum.altap.cz/viewtopic.php?f=16&t=5891
// and https://forum.altap.cz/viewtopic.php?f=2&t=4210
// returns TRUE if contents of 'path' change
// 2026-08-26: the narrow MakeValidFileName(char*) was deleted - confirmed-dead
// (zero callers anywhere). MakeValidFileNameW is the real, widely-used implementation.

// Returns TRUE if name ends with backslash ('\\' added at end solves invalid names)
// 2026-08-26: the narrow NameEndsWithBackslash(char*) was deleted - confirmed-dead
// (zero callers anywhere). NameEndsWithBackslashW is the real implementation.
std::wstring MakeCopyWithBackslashIfNeededW(const wchar_t* name);
BOOL NameEndsWithBackslashW(const wchar_t* name);

// If 'name' ends with space/dot or contains ':' (collision with ADS), returns TRUE, otherwise FALSE,
// if 'ignInvalidName' is TRUE, returns TRUE only if 'name' contains ':' (collision with ADS)
// 2026-08-26: the narrow FileNameIsInvalid(char*, ...) was deleted - confirmed-dead
// (zero callers anywhere). FileNameIsInvalidW below is the real, widely-used implementation
// (shellib.cpp, worker.cpp).
// Wide version of FileNameIsInvalid for Unicode paths
BOOL FileNameIsInvalidW(const wchar_t* name, BOOL isFullName, BOOL ignInvalidName = FALSE);

// Returns FALSE if contained path components end with space/dot
// and if 'cutPath' is TRUE, also shortens path to the first invalid component
// (for error message), otherwise returns TRUE
BOOL PathContainsValidComponents(const wchar_t* path);

// Creates a directory named 'name' via classic Win32 API call
// CreateDirectory(lpSecurityAttributes==NULL); this method resolves collision of 'name'
// with DOS name of already existing file/directory (only if it's not also a collision
// with long file/directory name) - ensures DOS name change so directory with name 'name'
// can be created (method: temporarily renames conflicting file/directory to another name
// and after creating 'name' renames it back); also handles names ending with spaces
// (can create them, unlike CreateDirectory which silently trims spaces and thus creates
// a different directory); returns TRUE on success, FALSE on error (returns Windows
// error code in 'err' if not NULL)
// 2026-08-25: the narrow SalCreateDirectoryEx(char*, ...) was deleted -
// confirmed-dead (zero callers; the legacy v107 ABI shim forwards to
// WideGeneral.SalCreateDirectoryEx). SalCreateDirectoryExW (common/CreateDirectoryFlow.h) is the
// sole surviving implementation.

void InitLocales(); // must be called before using NumberToStr and PrintDiskSize
std::wstring NumberToStr(const CQuadWord& number);
// 2026-08-25: NumberToStr2 (an alternate int-returning, thousands-separator-
// inserting variant) was deleted - confirmed-dead (zero callers anywhere; not the same function
// as NumberToStr above, which is genuinely still used).
std::wstring GetErrorTextOwned(DWORD error);
BOOL IsDirError(DWORD err);                               // is the error related to directory operations?

// Normal and UNC paths: do they have the same root?
BOOL HasTheSameRootPath(const wchar_t* path1, const wchar_t* path2);

// Checks if both paths have the same root and are on the same volume (handles
// paths containing reparse points and substs)
// WARNING: this is a quite SLOW function (up to 200ms)
BOOL HasTheSameRootPathAndVolume(const wchar_t* p1, const wchar_t* p2);

// Returns TRUE if paths 'path1' and 'path2' are on the same volume; in 'resIsOnlyEstimation'
// (if not NULL) returns TRUE if the result is not certain (certain only when "volume name"
// (GUID) was successfully obtained for both paths, which is only possible for local
// paths on W2K or newer NT systems)
// WARNING: this is a quite SLOW function (up to 200ms)
// The narrow form was deleted (2026-08-22): zero remaining callers once every
// call site moved to this wide sibling, which is a genuinely separate, already-existing
// implementation (not a signature twin) - two DIFFERENT Unicode directories could collapse to
// the same '?'-bearing path under the narrow form, so the narrow form could answer "same
// volume" for paths it never distinguished.
BOOL PathsAreOnTheSameVolumeW(const wchar_t* path1, const wchar_t* path2, BOOL* resIsOnlyEstimation);

// Compares two paths: case-insensitive, also ignores one backslash at start and end of paths
BOOL IsTheSamePath(const wchar_t* path1, const wchar_t* path2);

// consts.h was THE LAGGING HEADER at scale. Twenty-three free
// functions below had their .cpp bodies swept wide earlier while
// the declaration here stayed narrow. A free function's decl/def width
// disagreement is not diagnosed - it makes two overloads - so each narrow
// declaration was a promise with no body: narrow callers compiled into a link
// failure and wide callers errored. Every signature in this sweep was copied
// from its definition, which is the sole implementation in each case.
// Checks if the path is a plugin FS type, 'path' is the path being checked, 'fsName' is
// an owned FS name (or NULL), returns 'userPart' (if != NULL) - pointer
// into 'path' to first char of plugin-defined path (after the first ':')
BOOL IsPluginFSPath(const wchar_t* path, std::wstring* fsName = NULL, const wchar_t** userPart = NULL);
BOOL IsPluginFSPath(wchar_t* path, std::wstring* fsName = NULL, wchar_t** userPart = NULL);

// Test if this is a URL path, e.g. "file:///c|/WINDOWS/clock.avi" = "c:\\WINDOWS\\clock.avi"
BOOL IsFileURLPath(const wchar_t* path);

// Determines by file extension if it's a link (.lnk, .pif or .url); if so, returns 1,
// otherwise returns 0
int IsFileLink(const wchar_t* fileExtension);

// Returns TRUE if 'path' (UNC and normal path) can be shortened by the last directory
// (cut at last backslash - in the cut path backslash remains at end only for 'c:\'),
// 'cutDir' returns pointer to the last directory (the cut part)
// replacement for PathRemoveFileSpec
BOOL CutDirectory(wchar_t* path, wchar_t** cutDir = NULL);
// CutDirectoryW + SalPath*W wide helpers live in common/SalPathWide.h (shared
// with the private tests); included below next to the other SalPath declarations.

#include "common/SalPathWide.h" // SalPath*W + CutDirectoryW wide helpers

// 2026-08-25: the narrow SalPathRemoveBackslash/SalPathStripPath/
// SalPathRemoveExtension/SalPathAddExtension/SalPathRenameExtension(char*, ...) were deleted -
// confirmed-dead (zero callers anywhere; the legacy v107 ABI shim forwards each to the
// corresponding WideGeneral.SalPath...W method, never to these free narrow functions). The wide
// SalPath...W siblings in common/SalPathWide.h above are the sole surviving implementations.

// Converts all '/' to '\\' and also if there are two or more '\\' in a row,
// keeps only one (except two '\\' at string start, which denotes UNC path)
void SlashesToBackslashesAndRemoveDups(std::wstring& path);

// Works for normal and UNC paths.
// Returns number of chars of common path. On normal path root must be terminated with backslash,
// otherwise function returns 0. ("C:\"+"C:"->0, "C:\A\B"+"C:\"->3, "C:\A\B\"+"C:\A"->4,
// "C:\AA\BB"+"C:\AA\CC"->5)
int CommonPrefixLength(const wchar_t* path1, const wchar_t* path2);

// Returns TRUE if path 'prefix' is the base of path 'path'. Otherwise returns FALSE.
// "C:\aa","C:\Aa\BB"->TRUE
// "C:\aa","C:\aaa"->FALSE
// "C:\aa\","C:\Aa"->TRUE
// "\\server\share","\\server\share\aaa"->TRUE
// Works for normal and UNC paths.
BOOL SalPathIsPrefix(const wchar_t* prefix, const wchar_t* path);

// Removes ".." (skips ".." together with one subdirectory to the left) and "." (skips just ".")
// from path; backslash as subdirectory separator is required; 'afterRoot' points past root
// of the processed path (path changes happen only after 'afterRoot'); returns TRUE if changes
// succeeded, FALSE if ".." cannot be removed (root is already on the left)
// The narrow overload that used to sit above this line was the
// stale half of an A/W overload PAIR - the WCHAR form below already matched the
// wide definition in common/SalGetFullName.cpp. Deleted rather than widened,
// which would only have declared the same signature twice.
BOOL SalRemovePointsFromPath(WCHAR* afterRoot);

// Wide version of SalGetFullName + SalRemovePointsFromPath live in
// common/SalGetFullName.h (shared with the private tests).
#include "common/SalGetFullName.h"

// Tries to access path 'path' (normal or UNC), runs in a secondary thread, so it allows
// interruption with ESC key (after some time shows a message about ESC)
// 'echo' TRUE means error message display is allowed (if path is not accessible);
// 'err' different from ERROR_SUCCESS in combination with 'echo' TRUE only displays error (path
// is not accessed anymore); 'postRefresh' is parameter for EndStopRefresh call (normally TRUE);
// 'parent' is parent of messagebox; returns ERROR_SUCCESS if path is OK,
// otherwise returns standard Windows error code or ERROR_USER_TERMINATED if user
// used ESC key to interrupt the test
DWORD SalCheckPathW(BOOL echo, const wchar_t* path, DWORD err, BOOL postRefresh, HWND parent);

// Tries if path 'path' is accessible, optionally restores network connections using functions
// CheckAndRestoreNetworkConnection and CheckAndConnectUNCNetworkPath; returns TRUE if
// path is accessible; 'parent' is parent of messagebox; 'tryNet' is TRUE if it makes sense
// to try to restore network connections
BOOL SalCheckAndRestorePathW(HWND parent, const wchar_t* path, BOOL tryNet);

// Tries if path 'path' is accessible, optionally shortens it; if 'tryNet' is TRUE, optionally restores
// network connections using CheckAndRestoreNetworkConnection and CheckAndConnectUNCNetworkPath functions
// (if 'donotReconnect' is TRUE, only error is determined, connection is not restored) and sets
// 'tryNet' to FALSE; returns 'err' (error code of current path), 'lastErr' (error code leading to
// path shortening), 'pathInvalid' (TRUE if network connection restore was attempted without success),
// 'cut' (TRUE if resulting path is shortened); 'parent' is parent of messagebox; returns TRUE
// if resulting path 'path' is accessible
BOOL SalCheckAndRestorePathWithCutW(HWND parent, std::wstring& path, BOOL& tryNet, DWORD& err, DWORD& lastErr,
                                    BOOL& pathInvalid, BOOL& cut, BOOL donotReconnect);

// Recognizes what type of path (FS/windows/archive) it is and handles splitting it into
// its parts (for FS it's fs-name and fs-user-part, for archive it's path-to-archive and
// path-in-archive, for windows paths it's existing part and rest of path), for FS paths
// nothing is checked, for windows (normal + UNC) paths it checks how far the path exists
// (optionally restores network connection), for archives it checks existence of archive file
// (archive distinction by extension); uses SalGetFullName, so it's good to call
// CMainWindow::UpdateDefaultDir first)
// 'path' is full or relative path (buffer min. 'pathBufSize' chars; for relative paths
// current path 'curPath' (if not NULL) is used as base for evaluating full path;
// 'curPathIsDiskOrArchive' is TRUE if 'curPath' is windows or archive path;
// if current path is archive, 'curArchivePath' contains archive name, otherwise NULL),
// resulting full path is stored in 'path' (must be min. 'pathBufSize' chars); returns TRUE on
// successful recognition, then 'type' is path type (see PATH_TYPE_XXX) and 'secondPart' is set:
// - into 'path' at position past existing path (past '\\' or at end of string; if there's
//   a file in path, points past path to this file) (windows path type), WARNING: length
//   of returned path part is not checked (whole path can be longer than MAX_PATH)
// - past archive file (archive path type), WARNING: path length in archive is not checked (can be
//   longer than MAX_PATH)
// - past ':' after file-system name - user-part of file-system path (FS path type), WARNING:
//   user-part path length is not checked (can be longer than MAX_PATH);
// if returns TRUE, 'isDir' is also set to:
// - TRUE if existing path part is directory, FALSE == file (windows path type)
// - FALSE for archive and FS path types;
// if returns FALSE, error was displayed to user (except one exception - see SPP_INCOMLETEPATH description),
// that occurred during recognition (if 'error' is not NULL, one of SPP_XXX constants is returned in it);
// 'errorTitle' is title of error messagebox; if 'nextFocus' != NULL and windows/archive path
// doesn't contain '\\' or only ends with '\\', path is copied to 'nextFocus' (see SalGetFullName)
//
// 2026-08-25: the narrow SalParsePath free function (and its SalSplitWindowsPath/
// SalSplitGeneralPath siblings below) was deleted - confirmed exhaustively dead (zero callers
// anywhere in core, plugins, or the legacy ABI bridge; every real caller already goes through
// this wide form, either directly or via CSalamanderGeneral::SalParsePath in zip.cpp).
BOOL SalParsePathW(HWND parent, std::wstring& path, int& type, BOOL& isDir, wchar_t*& secondPart,
                   const wchar_t* errorTitle, std::wstring* nextFocus, BOOL curPathIsDiskOrArchive,
                   const wchar_t* curPath, const wchar_t* curArchivePath, int* error);

// Gets existing part and operation mask from windows target path; allows creating non-existing part;
// on success returns TRUE and existing windows target path (in 'path') and found operation mask
// (in 'mask' - points into 'path' buffer, but path and mask are separated by null; if there's no
// mask in path, automatically creates mask "*.*"); 'parent' - parent of any messageboxes;
// 'title' + 'errorTitle' are titles of info + error messagebox; 'selCount' is number of selected
// files and directories; 'path' is input target path to process, on output (at least 2 * MAX_PATH
// chars) existing target path; 'secondPart' points into 'path' at position past existing path
// (past '\\' or at end of string; if there's a file in path, points past path to this file);
// 'pathIsDir' is TRUE/FALSE if existing path part is directory/file; 'backslashAtEnd' is TRUE
// if there was backslash at end of 'path' before "parse" (e.g. SalParsePathW removes such backslash);
// 'dirName' + 'curDiskPath' are not NULL if max one file/directory is selected (its name without
// path is in 'dirName'; if nothing is selected, focus is taken) and current path is windows
// (path is in 'curDiskPath'); 'mask' is on output pointer to operation mask in 'path' buffer;
// if there's error in path, method returns FALSE, problem was already reported to user
//
// 2026-08-25: the narrow SalSplitWindowsPath free function was deleted -
// confirmed exhaustively dead (zero callers).
BOOL SalSplitWindowsPathOwnedW(HWND parent, const wchar_t* title, const wchar_t* errorTitle, int selCount,
                               std::wstring& path, size_t secondPartOffset, BOOL pathIsDir,
                               BOOL backslashAtEnd, const wchar_t* dirName, const wchar_t* curDiskPath,
                               std::wstring& mask);

// Gets existing part and operation mask from target path; recognizes non-existing part; on
// success returns TRUE, relative path to create (in 'newDirs'), existing target path (in 'path';
// existing only assuming creation of relative path 'newDirs') and found operation mask
// (in 'mask' - points into 'path' buffer, but path and mask are separated by null; if there's no
// mask in path, automatically creates mask "*.*"); 'parent' - parent of any messageboxes;
// 'title' + 'errorTitle' are titles of info + error messagebox; 'selCount' is number of selected
// files and directories; 'path' is input target path to process, on output (at least 2 * MAX_PATH
// chars) existing target path (always ends with backslash); 'afterRoot' points into 'path' past path root
// (past '\\' or at end of string); 'secondPart' points into 'path' at position past existing path (past
// '\\' or at end of string; if there's a file in path, points past path to this file);
// 'pathIsDir' is TRUE/FALSE if existing path part is directory/file; 'backslashAtEnd' is
// TRUE if there was backslash at end of 'path' before "parse" (e.g. SalParsePath removes such
// backslash); 'dirName' + 'curPath' are not NULL if max one file/directory is selected
// (its name without path is in 'dirName'; its path is in 'curPath'; if nothing is selected,
// focus is taken); 'mask' is on output pointer to operation mask in 'path' buffer; if 'newDirs' is not NULL,
// then it's a buffer (of at least MAX_PATH size) for relative path (relative to existing path
// in 'path') that needs to be created (user agreed to creation, same prompt was used as
// for disk to disk copy; empty string = create nothing); if 'newDirs' is NULL and relative path
// needs to be created, only error is displayed; 'isTheSamePathF' is function for comparing two paths
// (needed only if 'curPath' is not NULL), if NULL then IsTheSamePath is used;
// if there's error in path, method returns FALSE, problem was already reported to user
//
// 2026-08-25: the narrow SalSplitGeneralPath free function (and its
// SGP_IsTheSamePathAF callback typedef, used only by that declaration) was deleted - confirmed
// exhaustively dead (zero callers).
BOOL SalSplitGeneralPathOwnedW(HWND parent, const wchar_t* title, const wchar_t* errorTitle, int selCount,
                              std::wstring& path, size_t afterRootOffset, size_t secondPartOffset,
                              BOOL pathIsDir, BOOL backslashAtEnd, const wchar_t* dirName,
                              const wchar_t* curPath, std::wstring& mask, std::wstring* newDirs,
                              SGP_IsTheSamePathF isTheSamePathF);

// Checks if string 'fileNameComponent' can be used as a name component
// on Windows filesystem (handles strings longer than MAX_PATH-4 (4 = "C:\"
// + null-terminator), empty string, strings of '.' chars, whitespace strings,
// chars "*?\\/<>|\":" and simple names like "prn" and "prn  .txt")
// Wide sibling. Prefer it whenever a wide name is available: '?' is a reject character
// here and also the CP_ACP substitute, so the ANSI form rejects every name the code page
// cannot spell (audit C5). It validates syntax only; the destination filesystem decides
// its actual per-component limit.
BOOL SalIsValidFileNameComponentW(const wchar_t* fileNameComponent);

// Transforms string 'fileNameComponent' so it can be used as a name component
// on Windows filesystem (handles strings longer than MAX_PATH-4 (4 = "C:\"
// + null-terminator), handles empty string, strings of '.' chars, whitespace
// strings, replaces chars "*?\\/<>|\":" with '_' + simple names like "prn"
// and "prn  .txt" get '_' appended at end of name); 'fileNameComponent' must be
// extendable by at least one char (but at most MAX_PATH bytes from 'fileNameComponent'
// are used)
// Wide sibling: same syntax rules, applied to the true wide name and returned as a new,
// untruncated string. The destination filesystem enforces its own component limit.
std::wstring SalMakeValidFileNameComponentW(const wchar_t* fileNameComponent);

// Prints disk space size, mode==0 "1.23 MB", mode==1 "1 230 000 bytes, 1.23 MB",
// mode==2 "1 230 000 bytes", mode==3 (always in whole KB), mode==4 (like mode==0, but always
// at least 3 significant digits, e.g. "2.00 MB")
// The definition (sally_viewer_enumeration.cpp:456) has been wchar_t* all
// along; this narrow declaration had NO definition anywhere, so it was a pure link-error
// lie - invisible while Phase 1 never links. plugins.h:2005 already agrees.
std::wstring PrintDiskSize(const CQuadWord& size, int mode);

// Converts number of seconds to string ("5 sec", "1 hr 34 min", etc.); 'buf' is
// buffer for resulting text, must be at least 100 chars; 'secs' is number of seconds;
// returns 'buf'
std::wstring PrintTimeLeft(CQuadWord const& secs);

// Duplicates '&' - useful for paths displayed in menu ('&&' is displayed as '&');
// 'buffer' is input/output string, 'bufferSize' is size of 'buffer' in CHARACTERS;
// returns TRUE if duplication didn't cause loss of chars from end of string (buffer was big
// enough)
BOOL DuplicateAmpersands(wchar_t* buffer, int bufferSize, BOOL skipFirstAmpersand = FALSE);

// Removes '&' - useful for menu commands we need to display without hotkeys;
// if it finds pair "&&", replaces it with single '&' char
// 'text' is input/output string
void RemoveAmpersands(wchar_t* text);

// Duplicates '\\' for texts sent to LookForSubTexts, which reduces '\\\\' back to '\\'.
BOOL DuplicateBackslashes(std::wstring& text) noexcept;

// Duplicates '$' - used for importing old paths (hotpaths), which may contain $(SalDir)
// and newly supports Sal/Env variables like $(SalDir) or $(WinDir)
// during implementation I found that 2.5RC1, where we added support for these variables for editors,
// viewers, archivers, this expansion wasn't done; I'm not fixing it retroactively, only introducing
// conversion for HotPaths
// Trims '"' from beginning and end of 'path' (CutDoubleQuotes or StripDoubleQuotes or CutQuotes or StripQuotes)
// returns TRUE if trimming occurred
BOOL CutDoubleQuotesFromBothSides(wchar_t* path);

// Wait up to 1/5 second for ESC release (so that after ESC in dialog, for example
// reading listing in panel isn't immediately interrupted)
void WaitForESCRelease();

// Checks if root-parent of window 'parent' is foreground window, if not,
// FlashWindow(root-parent, TRUE) is called and root-parent is returned, otherwise NULL is returned
HWND GetWndToFlash(HWND parent);

// Enumerates all windows in thread 'tid' (0=current) using EnumThreadWindows and posts WM_CLOSE to all
// enabled and visible dialogs (class name "#32770") owned by window 'parent';
// used during critical shutdown to unblock window/dialog that has open modal dialogs,
// if there's risk of multiple layers, must be called repeatedly
void CloseAllOwnedEnabledDialogs(HWND parent, DWORD tid = 0);

// Returns displayable form of file/directory attributes; 'text' is buffer of at least 10 chars;
// 'attrs' are file/directory attributes
void GetAttrsStringW(wchar_t* text, DWORD attrs);

// Creates full file name (allocated); if 'dosName' is not NULL and 'path'+'name' is too
// long name, tries to join 'path'+'dosName'; if 'skip', 'skipAll' and 'sourcePath' are not
// NULL and "too long name" error occurs, allows user to skip this name (in this case
// returns NULL and TRUE in 'skip'), if user chooses "Skip All", sets 'skipAll' to TRUE

// Returns date+time from panel for file/directory 'f' (also handles dates+times provided by plugins - may not
// be valid)
void GetFileDateAndTimeFromPanel(DWORD validFileData, CPluginDataInterfaceEncapsulation* pluginData,
                                 const CFileData* f, BOOL isDir, SYSTEMTIME* st, BOOL* validDate,
                                 BOOL* validTime);

// Returns size from panel for file/directory 'f' (also handles sizes provided by plugins - may not
// be valid)
void GetFileSizeFromPanel(DWORD validFileData, CPluginDataInterfaceEncapsulation* pluginData,
                          const CFileData* f, BOOL isDir, CQuadWord* size, BOOL* validSize);

void DrawSplitLine(HWND HWindow, int newDragSplitX, int oldDragSplitX, RECT client);
BOOL InitializeCheckThread(); // initialization of thread for CFilesWindow::CheckPath()
void ReleaseCheckThreads();   // release of thread for CFilesWindow::CheckPath()
void InitDefaultDir();        // initialization of DefaultDir array (last visited paths on all drives)

// Shows/hides message in its own thread (doesn't drain message-queue), shows only one
// message at a time, repeated calls report error to TRACE (not fatal), 'delay' is delay
// before opening the window (from the moment CreateSafeWaitWindow is called)
// 'message' can be multiline; individual lines are separated by '\n'
// 'caption' can be NULL: then "Open Salamander" is used
// 'showCloseButton' indicates whether the window will contain Close button
// 'hForegroundWnd' specifies window that must be active for the window to be shown
// and also specifies window that will be activated when clicking the wait window
// Both text inputs are copied into wide cross-thread storage before this call
// returns; the caption then becomes the title of a Unicode CWaitWindow.
void CreateSafeWaitWindow(const wchar_t* message, const wchar_t* caption, int delay,
                          BOOL showCloseButton, HWND hForegroundWnd);
void DestroySafeWaitWindow(BOOL killThread = FALSE);
// Hides with 'show'==FALSE and then shows with 'show'==TRUE created window
// call as response to WM_ACTIVATE from hForegroundWnd window:
//    case WM_ACTIVATE:
//    {
//      ShowSafeWaitWindow(LOWORD(wParam) != WA_INACTIVE);
//      break;
//    }
// If thread (from which the window was created) is busy, messages are not
// distributed, so WM_ACTIVATE won't be delivered when clicking on another
// application. Messages are delivered only when messagebox is shown,
// which is exactly what we need: temporarily hide and later (after closing
// messagebox and activating hForegroundWnd window) show again.
void ShowSafeWaitWindow(BOOL show);
// After calling CreateSafeWaitWindow or ShowSafeWaitWindow returns FALSE until
// user clicks mouse on Close button (if shown); then returns TRUE
BOOL GetSafeWaitWindowClosePressed();
// Returns TRUE if user presses ESC or clicked mouse on Close button
BOOL UserWantsToCancelSafeWaitWindow();
// Used for additional text change in window
// WARNING: no window relayout occurs and if text stretches more,
// it will be clipped; use for example for countdown: 60s, 55s, 50s, ...
void SetSafeWaitWindowText(const wchar_t* message);

// Returns TRUE if Salamander is active (foreground window PID == current PID)
BOOL SalamanderActive();

// Removes directory including its contents (SHFileOperation is terribly slow)
// 2026-08-25: the narrow RemoveTemporaryDir(char*) thin adapter was deleted -
// confirmed-dead (zero callers; the legacy v107 ABI shim forwards to WideGeneral.RemoveTemporaryDir).
// Wide primary. The recursion is wide-native: the find result is used exactly as
// Windows returns it, so a temp file whose name the active code page cannot spell is deleted
// rather than leaked (and best-fit mapping can no longer aim the delete at a different file).
void RemoveTemporaryDirW(const wchar_t* dir);

// Helper function for adding name to list of names (separated by space), returns success
// If directory doesn't exist, allows creating it,
// if directory exists or is successfully created returns TRUE
// parent is parent of error messageboxes, NULL = main Salamander window
// quiet = TRUE - don't ask if user wants to create, but note, shows errors if errBuf == NULL
// if errBuf != NULL, errBufSize is size of buffer for error description,
// if newDir != NULL, first created subdirectory is returned in newDir (full path),
// if path completely exists, newDir=="", newDir points to buffer of size MAX_PATH
// noRetryButton = TRUE - error dialogs should not contain Retry/Cancel buttons, only OK button
// manualCrDir = TRUE - don't allow creating directory with space at beginning (when manually creating
// directory, otherwise Windows doesn't mind spaces at beginning)
// 2026-08-25: the narrow CheckAndCreateDirectory(char*, ...) thin adapter was
// deleted - confirmed-dead (zero callers; the legacy v107 ABI shim forwards to
// WideGeneral.CheckAndCreateDirectory).
// Wide primary; 'newDirSize' and 'errBufSize' count WCHARs.
BOOL CheckAndCreateDirectoryW(const wchar_t* dir, HWND parent = NULL, BOOL quiet = FALSE,
                              wchar_t* errBuf = NULL, int errBufSize = 0, wchar_t* newDir = NULL,
                              int newDirSize = 0, BOOL noRetryButton = FALSE, BOOL manualCrDir = FALSE);
BOOL CheckAndCreateDirectoryOwnedW(const wchar_t* dir, HWND parent = NULL, BOOL quiet = FALSE,
                                   std::wstring* errorText = NULL, std::wstring* firstCreatedDir = NULL,
                                   BOOL noRetryButton = FALSE, BOOL manualCrDir = FALSE);

// Deletes empty subdirectories in 'dir' from disk and if 'dir' is empty after subdirectory deletion,
// it is also deleted
// 2026-08-25: the narrow RemoveEmptyDirs(char*) thin adapter was deleted -
// confirmed-dead (zero callers anywhere; unlike its neighbors this one was never part of the
// plugin SDK/ABI at all, purely a core-internal helper superseded by RemoveEmptyDirsW).
// Wide primary, same shape as RemoveTemporaryDirW above it: the recursion is
// wide-native, so a subdirectory name the active code page cannot spell is walked and removed
// correctly instead of being narrowed to '?' first (which could also collide with an unrelated
// real directory name under WC_NO_BEST_FIT_CHARS).
void RemoveEmptyDirsW(const wchar_t* dir);

// Executes routine for opening viewer - used in CFilesWindow::ViewFile and
// CSalamanderForViewFileOnFS::OpenViewer; further use is not expected, therefore parameters
// and return values are not described
BOOL ViewFileInt(HWND parent, const wchar_t* name, BOOL altView, DWORD handlerID, BOOL returnLock,
                 HANDLE& lock, BOOL& lockOwner, BOOL addToHistory, int enumFileNamesSourceUID,
                 int enumFileNamesLastFileIndex);

// Converts string ('str' of length 'len') to unsigned __int64 (can be preceded by
// '+' sign; ignores white-spaces at beginning and end of string);
// if 'isNum' is not NULL, TRUE is returned in it if the entire string
// 'str' represents a number
unsigned __int64 StrToUInt64W(const wchar_t* str, int len, BOOL* isNum = NULL);

// Runs exception handler for "in-page-error" and "access violation - read/write on XXX" (tests
// if exception relates to file - 'fileMem' is start address, 'fileMemSize' is size of
// current view of mapped file), used when mapping file to memory (read/write
// error triggers this exception)
int HandleFileException(EXCEPTION_POINTERS* e, char* fileMem, DWORD fileMemSize);

struct CSalamanderVarStrEntry;
struct CSalamanderStringBuffer;
namespace sally::unicode
{
struct WideVarEntry;
}

// ValidateVarString and ExpandVarString:
// methods for validating and expanding strings with variables in format "$(var_name)", "$(var_name:num)"
// (num is variable width, a numeric value from 1 to 9999), "$(var_name:max)" ("max" is
// a symbol indicating that variable width is controlled by value in 'maxVarWidths' array, details
// at ExpandVarString) and "$[env_var]" (expands environment variable value); used when user
// can enter string format (like in info-line) example of string with variables:
// "$(files) files and $(dirs) directories" - variables 'files' and 'dirs'

// Checks syntax of 'varText' (string with variables), returns FALSE if error is found, its
// position is placed in 'errorPos1' (offset of error start) and 'errorPos2' (offset of error end);
// 'variables' is array of CSalamanderVarStrEntry structures, terminated by structure with
// Name==NULL; 'msgParent' is parent of error message-box, if NULL, errors are not displayed
// Variable names are UTF-16 and validation never calls CSalamanderVarStrEntry::Execute.
// errorPos1/errorPos2 are offsets in WCHARs into 'varText'.
BOOL ValidateVarStringW(HWND msgParent, const wchar_t* varText, int& errorPos1, int& errorPos2,
                        const CSalamanderVarStrEntry* variables);

// Native counterpart for core-owned callbacks that return std::wstring values.
BOOL ValidateWideVarStringW(HWND msgParent, const wchar_t* varText, int& errorPos1, int& errorPos2,
                            const sally::unicode::WideVarEntry* variables);

// Fills caller-owned 'buffer' with result of 'varText' expansion (string with variables),
// growing it through its Reserve callback. Returns FALSE on invalid storage, growth failure,
// syntax/callback error, or if the user cancels an environment-variable error;
// 'variables' is array of CSalamanderVarStrEntry structures, terminated by structure
// with Name==NULL; 'param' is pointer passed to CSalamanderVarStrEntry::Execute
// when expanding found variable; 'msgParent' is parent of error message-box, if NULL,
// errors are not displayed; if 'ignoreEnvVarNotFoundOrTooLong' is TRUE, environment-variable
// errors are ignored (not found or too large), if FALSE, messagebox with error is shown;
// if 'varPlacements' is not NULL, points to DWORD array with '*varPlacementsCount' items,
// which will be filled with DWORDs composed of variable WCHAR position in output buffer (low WORD)
// and variable WCHAR count (high WORD); if 'varPlacementsCount' is not NULL, number of
// filled items in 'varPlacements' array is returned in it (essentially the count of variables
// in input string);
// if this method is used only for expanding string for one 'param' value, 'detectMaxVarWidths'
// should be set to FALSE, 'maxVarWidths' to NULL and 'maxVarWidthsCount' to 0; however if
// this method is used for repeatedly expanding string for a certain set of 'param' values
// (e.g. in Make File List it's expanding line for all selected files and directories),
// it makes sense to use variables in format "$(varname:max)", for these variables width is
// determined as largest width of expanded variable across the entire set of values; measuring
// the largest width of expanded variable is done in first cycle (for all set values) of calling
// ExpandVarString, in first cycle parameter 'detectMaxVarWidths' has value TRUE and array
// 'maxVarWidths' with 'maxVarWidthsCount' items is pre-zeroed (used for storing maximums between
// individual ExpandVarString calls); actual expansion then occurs in second cycle (for all set values)
// of calling ExpandVarString, in second cycle parameter 'detectMaxVarWidths' has value FALSE and
// array 'maxVarWidths' with 'maxVarWidthsCount' items contains pre-computed largest widths
// (from first cycle)
// :num and maxVarWidths use UTF-16 code-unit widths.
BOOL ExpandVarString(HWND msgParent, const wchar_t* varText, CSalamanderStringBuffer* buffer,
                     const CSalamanderVarStrEntry* variables, void* param,
                     BOOL ignoreEnvVarNotFoundOrTooLong = FALSE,
                     CSalamanderTextRangeBuffer* varPlacements = NULL,
                     BOOL detectMaxVarWidths = FALSE, int* maxVarWidths = NULL,
                     int maxVarWidthsCount = 0);

// Expands native Sally UTF-16 callbacks with the same UI/environment-error policy
// as ExpandVarString. Widths and output capacity are measured in WCHARs.
BOOL ExpandWideVarStringW(HWND msgParent, const wchar_t* varText, wchar_t* buffer, int bufferLen,
                          const sally::unicode::WideVarEntry* variables, void* param,
                          BOOL ignoreEnvVarNotFoundOrTooLong = FALSE,
                          std::vector<sally::unicode::WideTextRange>* varPlacements = NULL,
                          BOOL detectMaxVarWidths = FALSE, int* maxVarWidths = NULL,
                          int maxVarWidthsCount = 0);
BOOL ExpandWideVarStringW(HWND msgParent, const wchar_t* varText, std::wstring& output,
                          const sally::unicode::WideVarEntry* variables, void* param,
                          BOOL ignoreEnvVarNotFoundOrTooLong = FALSE);

// Clipboard adapters for explicitly ACP-encoded byte text.
// returns ERROR_SUCCESS or GetLastError
DWORD AddAcpTextToClipboard(const char* bytes, int byteCount);

// Puts text on clipboard; if showEcho, shows message box that it's OK
// if textLen==-1, calculates length itself
BOOL CopyAcpTextToClipboard(const char* bytes, int byteCount = -1, BOOL showEcho = FALSE, HWND hEchoParent = NULL);
BOOL CopyTextToClipboardW(const wchar_t* text, int textLen = -1, BOOL showEcho = FALSE, HWND hEchoParent = NULL);
BOOL CopyAcpHTextToClipboard(HGLOBAL hGlobalBytes, int byteCount = -1, BOOL showEcho = FALSE, HWND hEchoParent = NULL);
BOOL CopyHTextToClipboardW(HGLOBAL hGlobalText, int textLen = -1);

// Determines from buffer 'pattern' of length 'patternLen' if it's text (there's a code page
// in which it contains only allowed chars - displayable and control) and if it's text, also
// determines its code page (most probable); 'parent' is parent of messagebox; if 'forceText'
// is TRUE, check for forbidden chars is not performed (used if 'pattern' contains text);
// if 'isText' is not NULL, TRUE is returned in it if it's text; if 'codePage' is not NULL, it's
// a buffer (min. 101 chars) for code page name (most probable)
void RecognizeFileType(HWND parent, const char* pattern, int patternLen, BOOL forceText,
                       BOOL* isText, std::wstring* codePage);

// Sets name of calling thread in VC debugger
void SetThreadNameInVC(const wchar_t* threadName);

// Sets name of calling thread in VC debugger and Trace Server
void SetThreadNameInVCAndTrace(const wchar_t* name);

// configuration loading
class CEditorMasks;
class CViewerMasks;

// functions related to drag&drop and other shell things

class CFilesWindow;

// shell operations
enum CShellAction
{
    saLeftDragFiles,
    saRightDragFiles,
    saContextMenu,
    saCopyToClipboard,
    saCutToClipboard,
    saProperties,
    saPermissions, // same as saProperties, just tries to select "security tab"
};

class CCopyMoveData;
struct CDragDropOperData;

// Both lagged their own definitions (shellsup.cpp:289/:682), which
// already return const wchar_t* - and so does the CGetCurDir callback typedef these
// are handed to (shellib.h:298). Only these two declarations disagreed, and the
// resulting C2556 pair was invisible until shellsup.cpp came off the 100-error cap.
const wchar_t* GetCurrentDir(POINTL& pt, void* param, DWORD* effect, BOOL rButton, BOOL& tgtFile,
                             DWORD keyState, int& tgtType, int srcType);
const wchar_t* GetCurrentDirClipboard(POINTL& pt, void* param, DWORD* effect, BOOL rButton,
                                      BOOL& isTgtFile, DWORD keyState, int& tgtType, int srcType);
BOOL DoCopyMove(BOOL copy, const wchar_t* targetDir, CCopyMoveData* data, void* param);
void DoDragDropOper(BOOL copy, BOOL toArchive, const wchar_t* archiveOrFSName, const wchar_t* archivePathOrUserPart,
                    CDragDropOperData* data, void* param);
void DoGetFSToFSDropEffect(const wchar_t* srcFSPath, const wchar_t* tgtFSPath,
                           DWORD allowedEffects, DWORD keyState,
                           DWORD* dropEffect, void* param);
BOOL UseOwnRutine(IDataObject* pDataObject);
BOOL MouseConfirmDrop(DWORD& effect, DWORD& defEffect, DWORD& grfKeyState);
void DropEnd(BOOL drop, BOOL shortcuts, void* param, BOOL ownRutine, BOOL isFakeDataObject, int tgtType);
void EnterLeaveDrop(BOOL enter, void* param);

// Saves preferred drop effect and info about Salamander origin to clipboard
BOOL SetClipCutCopyInfo(HWND hwnd, BOOL copy, BOOL salObject);

void ShellAction(CFilesWindow* panel, CShellAction action, BOOL useSelection = TRUE,
                 BOOL posByMouse = TRUE, BOOL onlyPanelMenu = FALSE);
// 2026-08-25: the narrow ExecuteAssociation(char*, char*) was deleted -
// confirmed-dead (zero callers anywhere; a stale comment claimed files_window_copy_move.cpp
// still called it directly, but that file calls ExecuteAssociationW). BOTH the directory and
// the name are wide: a folder whose own path is outside the ANSI code page must reach the
// shell intact even when the file inside it has a perfectly ordinary ASCII name.
void ExecuteAssociationW(HWND hWindow, const wchar_t* pathW, const wchar_t* nameW);

BOOL CanUseShellExecuteWndAsParent(const wchar_t* cmdName);

// Determines if file is placeholder (online file in OneDrive folder),
// see http://msdn.microsoft.com/en-us/library/windows/desktop/dn323738%28v=vs.85%29.aspx
BOOL IsFilePlaceholder(WIN32_FIND_DATA const* findData);

// Before opening editor or viewer, placeholder is converted to offline file,
// so that viewer/editor can work with it
//BOOL MakeFileAvailOfflineIfOneDriveOnWin81(HWND parent, const char *name);

// Sets thread priority to normal and calls menu->InvokeCommand() in try-except block
// before finishing sets thread priority back to original value
BOOL SafeInvokeCommand(IContextMenu2* menu, CMINVOKECOMMANDINFO& ici);

// If 'hInstance' is NULL, reads from HLanguage; otherwise from 'hInstance'.
// LoadStrOwned is the native owner; LoadStrW remains a transitional pointer-returning
// compatibility surface for callers not yet migrated to explicit ownership.
std::wstring LoadStrOwned(int resID, HINSTANCE hInstance = NULL);
WCHAR* LoadStrW(int resID, HINSTANCE hInstance = NULL);

// Support for creating parameterized texts (handling singular and plural forms
// in texts); 'lpFmt' is format string for result text - its format description
// follows; result text is returned in buffer 'lpOut' of size 'nOutMax' bytes;
// 'lpParArray' is array of text parameters, 'nParCount' is count of these parameters;
// returns length of result text
//
// format string description:
//   - at start of each format string is signature "{!}"
//   - following escape sequences are recognized for suppressing meaning of special
//     chars in format string (backslash char in this description
//     is not doubled): "\\" = "\", "\{" = "{", "\}" = "}", "\:" = ":" and "\|" = "|"
//   - text not in curly braces is transferred to result string
//     without changes (except escape sequences)
//   - parameterized text is in curly braces
//   - each parameterized text uses one parameter from 'lpParArray' - it's
//     a 64-bit unsigned int
//   - parameterized text contains different result texts for different intervals
//     of parameter values
//   - individual result texts and interval limits are separated by "|"
//   - parameterized text "{}" is used for skipping one parameter
//     from 'lpParArray' array (creates no output text)
//   - if at start of parameterized text there's a number followed by colon,
//     it's parameter index (from one to parameter count), to be used,
//     if index is not specified, it's assigned automatically (sequentially from one to
//     parameter count)
//   - when specifying parameter index, sequentially assigned index doesn't change,
//     e.g. in "{!}%d file{2:s|0||1|s} and %d director{y|1|ies}" for first
//     parameterized text parameter with index 2 is used and for second
//     with index 1
//   - parameterized texts with specified index can be used any number of times
//
// format string examples:
//   - "{!}director{y|1|ies}" for parameter value from 0 to 1 (inclusive) will be
//     "directory" and for value from 2 to "infinity" (2^64-1) will be "directories"
//   - "{!}file{s|0||1|s|4|s}" for parameter value 0 will be "files",
//     for 1 will be "file", for 2 to 4 (inclusive) will be "files" and from 5
//     to "infinity" will be "files"
int ExpandPluralStringW(wchar_t* lpOut, int nOutMax, const wchar_t* lpFmt, int nParCount,
                        const CQuadWord* lpParArray);
std::wstring ExpandPluralStringOwnedW(const wchar_t* format, int parameterCount,
                                      const CQuadWord* parameters);

//
// Writes string to lpOut depending on variables files and dirs:
// files > 0 && dirs == 0  ->  XXX (selected/hidden) files
// files == 0 && dirs > 0  ->  YYY (selected/hidden) directories
// files > 0 && dirs > 0   ->  XXX (selected/hidden) files and YYY directories
//
// where XXX and YYY correspond to values of variables files and dirs.
// Variable selectedForm controls insertion of word selected
//
// forDlgCaption is TRUE/FALSE if text is/isn't intended for dialog title
// (in English, capital initial letters are required).
//
// Returns count of copied chars without terminator.
//
// description of epfdmXXX constants see spl_gen.h
int ExpandPluralFilesDirsW(wchar_t* lpOut, int nOutMax, int files, int dirs,
                           int mode, BOOL forDlgCaption);
std::wstring ExpandPluralFilesDirsTextW(int files, int dirs, int mode, BOOL forDlgCaption);
int ExpandPluralBytesFilesDirsW(wchar_t* lpOut, int nOutMax, const CQuadWord& selectedBytes,
                                int files, int dirs, BOOL useSubTexts);
std::wstring ExpandPluralBytesFilesDirsTextW(const CQuadWord& selectedBytes, int files, int dirs,
                                             BOOL useSubTexts);

// Finds pairs '<' '>' in text, removes them from buffer and adds references to
// their content to 'varPlacements'. 'varPlacements' is array of DWORDs with '*varPlacementsCount'
// items, DWORDs are composed of reference position in output buffer (low WORD)
// and reference char count (high WORD). Strings "\<", "\>", "\\" are understood
// as escape sequences and will be replaced with chars '<', '>' and '\\'.
// Returns TRUE on success, otherwise FALSE; always sets 'varPlacementsCount' to
// count of processed variables.
BOOL LookForSubTexts(std::wstring& text,
                     std::vector<sally::unicode::WideTextRange>& varPlacements);

void MinimizeApp(HWND mainWnd);             // minimize app
void RestoreApp(HWND mainWnd, HWND dlgWnd); // restore from minimized state of app
// 2026-08-25: the narrow AlterFileName(char*, ...) was deleted - confirmed-dead
// (zero callers; the legacy v107 ABI shim forwards to WideGeneral.AlterFileName, never to this
// free function). AlterFileNameW below is the sole surviving implementation.
std::wstring AlterFileNameW(const wchar_t* filename, int format, int change, bool isDir);

// Returns string with file size and times; Returns time in 'fileTime', variable can be NULL;
// if 'getTimeFailed' is not NULL, TRUE is written to it on file time retrieval error
void GetFileOverwriteInfoW(wchar_t* buff, int buffLen, HANDLE file, const wchar_t* fileName, FILETIME* fileTime = NULL, BOOL* getTimeFailed = NULL);

void ColorsChanged(BOOL refresh, BOOL colorsOnly, BOOL reloadUMIcons);                // call after color change
HICON GetDriveIconW(const wchar_t* root, UINT type, BOOL accessible, BOOL large = FALSE); // drive icon
HICON SalLoadIcon(HINSTANCE hDLL, int id, int iconSize);

// SetCurrentDirectory(system directory) - disconnect from directory in panel
void SetCurrentDirectoryToSystem();

// Replaces substs in path 'resPath' with their target paths (conversion to path without SUBST drive-letters);
// returns FALSE on error
// Wide-native — it queries QueryDosDeviceW, so a SUBST whose
// TARGET the code page cannot spell now resolves correctly. It was previously a
// wrapper that narrowed its argument and called the ANSI implementation, which
// destroyed exactly the paths it existed to handle.
BOOL ResolveSubstsW(std::wstring& resPath); // no length cap — prefer this one

// Wide sibling. Outputs are std::wstring, so unlike the SDK method
// of the same name it has no out-buffer problem; pass NULL for what you do not
// want.
BOOL GetResolvedPathMountPointAndGUIDW(const wchar_t* path, std::wstring* mountPoint, std::wstring* guidPath);

// Attempt to return correct values (handles reparse points too - full path is given instead of root)
CQuadWord MyGetDiskFreeSpaceW(const wchar_t* path, CQuadWord* total = NULL); // wide sibling
// WARNING: don't use return values 'lpNumberOfFreeClusters' and 'lpTotalNumberOfClusters', because for larger
//          disks they contain nonsense (DWORD may not be enough for total cluster count), solve via
//          MyGetDiskFreeSpaceW(path, total), which returns 64-bit numbers
BOOL MyGetDiskFreeSpaceW(const wchar_t* path, LPDWORD lpSectorsPerCluster,
                         LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                         LPDWORD lpTotalNumberOfClusters); // wide sibling

// Improved GetVolumeInformation: works with path (traverses reparse points and substs).
// In 'rootOrCurReparsePoint' (if not NULL) returns either root or the current
// (last) local reparse point on 'path'. In 'junctionOrSymlinkTgt' (if not NULL) returns
// target of current reparse point or empty string (if no reparse point exists or
// is of unknown type or is volume mount point); in 'linkType' (if not NULL) returns type of current
// reparse point: 0 (unknown or doesn't exist), 1 (MOUNT POINT), 2 (JUNCTION POINT), 3 (SYMBOLIC LINK)
// String outputs are dynamically owned; pass NULL for what you do not want.
BOOL MyGetVolumeInformationW(const wchar_t* path, std::wstring* rootOrCurReparsePoint,
                             std::wstring* junctionOrSymlinkTgt, int* linkType,
                             std::wstring* volumeName, LPDWORD lpVolumeSerialNumber,
                             LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
                             std::wstring* fileSystemName);
// Returns target path of reparse point 'repPointDir' in buffer 'repPointDstBuf' (if not NULL)
// of size 'repPointDstBufSize'; 'repPointDir' and 'repPointDstBuf' can point
// to same buffer (IN/OUT buffer); if 'makeRelPathAbs' is TRUE and it's a relative
// symbolic link, converts link target path to absolute;
// returns TRUE on success + in 'repPointType' (if not NULL) returns reparse point type:
// 1 (MOUNT POINT), 2 (JUNCTION POINT), 3 (SYMBOLIC LINK)
// The destination is dynamically owned UTF-16; no fixed-capacity core wrapper exists.
BOOL GetReparsePointDestinationOwnedW(const wchar_t* repPointDir, std::wstring* repPointDst,
                                      int* repPointType, BOOL makeRelPathAbs);
// The dynamically-owned current-reparse-point API is declared in
// common/LocalPathResolution.h (included below).

// Call only for paths 'path' whose root (after removing subst) is DRIVE_FIXED (elsewhere it makes no sense to look for
// reparse points); we look for path without reparse points leading to same volume as 'path'; for path
// containing symlink leading to network path (UNC or mapped) we return only root of this network path
// (even Vista can't work with reparse points on network paths, so it's probably pointless to try);
// if such path doesn't exist because current (last) local reparse point is volume mount
// point (or unknown type reparse point), we return path to this volume mount point (or reparse
// point of unknown type); if path contains more than 50 reparse points (probably infinite loop),
// we return original path;
//
// 'resPath' is buffer for result of size MAX_PATH; 'path' is original path; in 'cutResPathIsPossible'
// (must not be NULL) we return FALSE if resulting path in 'resPath' ends with reparse point (volume
// mount point or unknown type reparse point) and thus we must not shorten it (we'd probably get to
// different volume); if 'rootOrCurReparsePointSet' is not NULL and contains FALSE and there's at least
// one local reparse point on original path (we ignore reparse points on network part of path), we return
// TRUE in this variable + in 'rootOrCurReparsePoint' (if not NULL) we return full path to current (last
// local) reparse point (note, not where it leads); target path of current reparse point (only if it's
// junction or symlink) is returned in 'junctionOrSymlinkTgt' (if not NULL) + type is returned in 'linkType':
// 2 (JUNCTION POINT), 3 (SYMBOLIC LINK); in 'netPath' (if not NULL) we return network path to which
// current (last) local symlink in path leads - in this situation network path root is returned in 'resPath'
// The wide walk + its result struct live in their own header so consumers can
// reach them without the whole-app header.
#include "common/LocalPathResolution.h"

// Improved GetDriveType: works with path (traverses reparse points and substs)
UINT MyGetDriveTypeW(const wchar_t* path); // wide sibling

// Our dynamically-owned QueryDosDevice wrapper. 'driveNum' is 0-based (0=A: 2=C: ...).
BOOL MyQueryDosDeviceW(BYTE driveNum, std::wstring& target);

// Detects if 'driveNum' (0=A: 2=C: ...) is substed and if so, where it's connected to
// if drive is not substed, returns FALSE
// if substed, returns TRUE and stores path where subst is connected
// in the owned UTF-16 string 'path'
// can return path in UNC format
// The parse itself lives in common/SubstResolution.h
// so it is testable without real SUBST drives; this pairs it with the live
// QueryDosDeviceW query.
BOOL GetSubstInformationW(BYTE driveNum, std::wstring& path);

// Replaces the last '.' in text with the decimal separator from LOCALE_SDECIMAL.
BOOL PointToLocalDecimalSeparator(std::wstring& text) noexcept;

typedef WINBASEAPI LONG(WINAPI* MY_FMExtensionProc)(HWND hwnd,
                                                    WORD wMsg,
                                                    LONG lParam);
void GetMessagePos(POINT& p);

// Returns icon handle obtained via SHGetFileInfo or NULL on failure.
// Caller is responsible for icon destruction. Icon is assigned to HANDLES.
HICON GetFileOrPathIconAuxW(const wchar_t* path, BOOL large, BOOL isDir);

// If root of UNCPath is inaccessible (for listing), tries to establish network connection,
// asks for username and password itself, returns TRUE if connection was established,
// returns FALSE if UNCPath is not UNC path or root of UNCPath is accessible or
// if connection couldn't be established, in 'pathInvalid' returns TRUE if user interrupted
// username+password dialog or we unsuccessfully tried to establish connection (e.g.
// "credentials conflict"); if 'donotReconnect' is TRUE, doesn't try to establish network
// connection, returns immediately that it didn't work
BOOL CheckAndConnectUNCNetworkPathW(HWND parent, const wchar_t* UNCPath, BOOL& pathInvalid,
                                    BOOL donotReconnect);

// Tries to restore network connection (if it existed) on 'drive:', parent - dialog parent,
// returns TRUE if connection restore succeeded (network drive is mapped again)
// in 'pathInvalid' returns TRUE if user interrupted username+password dialog or
// we unsuccessfully tried to establish connection (e.g. "credentials conflict")
BOOL CheckAndRestoreNetworkConnection(HWND parent, const char drive, BOOL& pathInvalid);

// Functions for thread management, when process ends these threads should close,
// if they don't close themselves, they must be terminated
void AddAuxThread(HANDLE view, BOOL testIfFinished = FALSE);
void TerminateAuxThreads();

// Returns TRUE if specified file exists; otherwise returns FALSE.
BOOL FileExistsW(const wchar_t* fileName); // wide primary

// Returns TRUE if specified directory exists; otherwise returns FALSE
// 2026-08-25: the narrow DirExists(char*) thin adapter was deleted -
// confirmed-dead (zero callers anywhere in core).
BOOL DirExistsW(const wchar_t* dirName); // wide primary

// tool tip
void SetCurrentToolTip(HWND hNotifyWindow, DWORD id, int showDelay = 0); // description in tooltip.h
void SuppressToolTipOnCurrentMousePos();                                 // description in tooltip.h

// Ensures cursor jumps on Ctrl+Left/Right at backslashes and spaces
// assigns EditWordBreakProc to editline or combobox
// can be called for example in WM_INITDIALOG
// no need to uninstall
BOOL InstallWordBreakProc(HWND hWindow);

// Clears all items from dropdown listbox of combobox
// used when clearing histories
void ClearComboboxListbox(HWND hCombo);
std::wstring GetWindowTextStringW(HWND window);

// structure for WM_USER_VIEWFILE and WM_USER_VIEWFILEWITH
struct COpenViewerData
{
    // FileName IS the exact wide name. It used to be a char* that could be
    // a lossy structural (backslash/extension) mirror, with a FileNameW twin beside it for
    // senders that had the real thing; there is nothing left for the twin to correct.
    const wchar_t* FileName;
    int EnumFileNamesSourceUID;
    int EnumFileNamesLastFileIndex;
};

// structure for WM_USER_EDITFILE and WM_USER_EDITFILEWITH. Replaces the bare 'char* name'
// wParam; the wide twin it was introduced to carry is now the only member.
struct CEditFileData
{
    const wchar_t* FileName;
};

// structure for WM_USER_FOCUSFILEW; both members are the exact wide forms,
// never a re-widened mirror. Owned by the sender for the duration of the SendMessage.
struct CFocusFileDataW
{
    const wchar_t* Name;
    const wchar_t* Path;
};

//
// ****************************************************************************

#define WM_USER_REFRESH_DIR WM_APP + 100   // [BOOL setRefreshEvent, time]
#define WM_USER_S_REFRESH_DIR WM_APP + 101 // [BOOL setRefreshEvent, time]

#define WM_USER_SETDIALOG WM_APP + 103 // [CProgressData *data, 0] \
                                       // or [0, 0] - setprogress
#define WM_USER_DIALOG WM_APP + 104    // [int dlgID, void *data]

// icon reader just loaded an icon, inserted it into iconcache and tells the panel
// running in the main thread so it can repaint and backup the icon to associations
// index is the found position of the item in CFilesWindow::Files/Dirs
#define WM_USER_REFRESHINDEX WM_APP + 105 // [int index, 0]

#define WM_USER_END_SUSPMODE WM_APP + 106  // [0, 0] - faster window activation
#define WM_USER_DRIVES_CHANGE WM_APP + 107 // [0, 0]
#define WM_USER_ICON_NOTIFY WM_APP + 108   // [0, 0] - mouse is over icon in taskbar
#define WM_USER_EDIT WM_APP + 110          // [begin, end] select this interval
#define WM_USER_SM_END_NOTIFY WM_APP + 111 // [0, 0] schedules WM_USER_SM_END_NOTIFY_DELAYED after 200ms
#define WM_USER_DISPLAYPOPUP WM_APP + 112  // [0, commandID] popup menu needs to be shown
//#define WM_USER_SETPATHS        WM_APP + 113    // do not use, old message that old applications could theoretically send

#define WM_USER_CHAR WM_APP + 114 // notification from listview
// [command, index]
// command = 0 for normal configuration,
// command = 1 for opening Hot Paths page; index then represents the item
// command = 2 for opening User Menu page
// command = 3 for opening Internal Viewer page
// command = 4 for opening Views page, index represents the view
// command = 5 for opening Panels page
#define WM_USER_CONFIGURATION WM_APP + 115
#define WM_USER_MOUSEWHEEL WM_APP + 116     // [wParam, lParam] z WM_MOUSEWHEEL
#define WM_USER_SKIPONEREFRESH WM_APP + 117 // [0, 0] after 500 ms SkipOneActivateRefresh = FALSE
#define WM_USER_FLASHWINDOW WM_APP + 118    // [0, 0] flashes the window
#define WM_USER_SHOWWINDOW WM_APP + 119     // [0, 0] brings window to foreground (restores if needed)
#define WM_USER_DROPCOPYMOVE WM_APP + 120   // [CTmpDropData *, 0]
// Posted only by CTextDropTarget. Ownership of newDirW transfers to the receiver
// when PostMessage succeeds; the receiver deletes it after calling ChangeDir.
#define WM_USER_CHANGEDIRW WM_APP + 127 // [postProcessUserText, std::wstring *newDirW]
#define WM_USER_FOCUSFILE WM_APP + 122      // [fileName, newPath] - panel changes its path and selects the corresponding file
// Wide form of WM_USER_FOCUSFILE. Sent with a CFocusFileDataW*, valid only
// for the duration of the SendMessage.
#define WM_USER_FOCUSFILEW WM_APP + 125 // [CFocusFileDataW *, 0]
#define WM_USER_CLOSEFIND WM_APP + 123      // [0, 0] - calls DestroyWindow from find window thread
#define WM_USER_SELCHANGED WM_APP + 124     // [0, 0] - notification about selection change
#define WM_USER_MOUSEHWHEEL WM_APP + 126    // [wParam, lParam] z WM_MOUSEHWHEEL

#define WM_USER_CLOSEMENU WM_APP + 130 // [0, 0] - internal for menu - needs to close

#define WM_USER_REFRESH_PLUGINFS WM_APP + 133 // [0, 0] - call FS Event(FSE_ACTIVATEREFRESH)
#define WM_USER_REFRESH_SHARES WM_APP + 134   // [0, 0] - snooper.cpp reports share change in registry
#define WM_USER_PROCESSDELETEMAN WM_APP + 135 // [0, 0] - cache.cpp: DeleteManager - start processing new data in main thread

#define WM_USER_CANCELPROGRDLG WM_APP + 136  // [0, 0] - CProgressDlgArray: after delivering this message to CProgressDialog, operation will be cancelled (without prompt; operation dialog closes)
#define WM_USER_FOCUSPROGRDLG WM_APP + 137   // [0, 0] - CProgressDlgArray: after delivering this message to CProgressDialog, dialog (or its popup) will be activated
#define WM_USER_ICONREADING_END WM_APP + 138 // [0, 0] - notification about icon loading completion in panel

//moved to shexreg.h (constant must not change): #define WM_USER_SALSHEXT_PASTE  WM_APP + 139 // [postMsgIndex, 0] - SalamExt requests execution of Paste command

#define WM_USER_DROPUNPACK WM_APP + 140    // [allocatedTgtPath, operation] - drag&drop from archive: target path + operation determined, we perform unpack
#define WM_USER_PROGRDLGEND WM_APP + 141   // [cmd, 0] - CProgressDialog: probably unnecessary under W2K+: bypass bugs (closed dialogs remained in taskbar) - delayed dialog close
#define WM_USER_PROGRDLGSTART WM_APP + 142 // [0, 0] - CProgressDialog: probably unnecessary under W2K+: bypass bugs (garbage remained on screen after dialog) - delayed worker thread start

//moved to shexreg.h (constant must not change): #define WM_USER_SALSHEXT_TRYRELDATA WM_APP + 143 // [0, 0] - SalamExt reports unblocking of paste-data (see CSalShExtSharedMem::BlockPasteDataRelease), if data is no longer protected, we release it

#define WM_USER_DROPFROMFS WM_APP + 144    // [allocatedTgtPath, operation] - drag&drop from FS: target path + operation determined, we perform copy/move from FS
#define WM_USER_DROPTOARCORFS WM_APP + 145 // [CTmpDragDropOperData *, 0]

#define WM_USER_SHCHANGENOTIFY WM_APP + 146 // message for SHChangeNotifyRegister [pidlList, SHCNE_xxx (event that occured)]

#define WM_USER_REFRESH_DIR_EX WM_APP + 147 // [long_wait, time] - after (long_wait?5000:200) ms sends WM_USER_REFRESH_DIR_EX_DELAYED

#define WM_USER_SETPROGRESS WM_APP + 148 // [progress, text] used for crossing thread boundary

// icon reader just loaded icon-overlay and tells the panel running in main thread so
// it can repaint
// index is the position of the item in CFilesWindow::Files/Dirs
#define WM_USER_REFRESHINDEX2 WM_APP + 149 // [int index, 0]

#define WM_USER_DONEXTFOCUS WM_APP + 150       // [0, 0] - notification about NextFocusName change
#define WM_USER_CREATEWAITWND WM_APP + 151     // [parent or NULL, delay] - message for safe-wait-message thread: "create && show"
#define WM_USER_DESTROYWAITWND WM_APP + 152    // [killThread, 0] - message for safe-wait-message thread: "hide && destroy"
#define WM_USER_SHOWWAITWND WM_APP + 153       // [show, 0] - message for safe-wait-message thread: "show || hide"
#define WM_USER_SETWAITMSG WM_APP + 154        // [0, 0] - message for safe-wait-message thread: text was changed--repaint
#define WM_USER_REPAINTALLICONS WM_APP + 155   // [0, 0] - refresh icons in both panels
#define WM_USER_REPAINTSTATUSBARS WM_APP + 156 // [0, 0] - refresh throbber (dirline) in both panels

#define WM_USER_VIEWERCONFIG WM_APP + 158 // [hWnd, 0] - hWnd identifies viewer that will be attempted to bring to front after configuration

#define WM_USER_UPDATEPANEL WM_APP + 159 // [0, 0] - if delivered \
                                         // (delivered by message loop when opening messagebox), \
                                         // panel will be invalidated and scrollbar will be set \
                                         // (for internal use only !!!)

#define WM_USER_AUTOCONFIG WM_APP + 160     // KICKER - autoconfig
#define WM_USER_ACFINDFINISHED WM_APP + 161 // KICKER - autoconfig
#define WM_USER_ACSEARCHING WM_APP + 162    // KICKER - autoconfig
#define WM_USER_ACADDFILE WM_APP + 163      // KICKER - autoconfig
#define WM_USER_ACERROR WM_APP + 164        // KICKER - autoconfig

#define WM_USER_QUERYCLOSEFIND WM_APP + 170  // [0, quiet] - asks in find window thread if we can close it + on query stops any ongoing search
#define WM_USER_COLORCHANGEFIND WM_APP + 171 // [0, 0] - informs find windows about color change

#define WM_USER_HELPHITTEST WM_APP + 172  // lResult = dwContext, lParam = MAKELONG(x,y)
#define WM_USER_EXITHELPMODE WM_APP + 173 // [0, 0]

#define WM_USER_POSTCMDORUNLOADPLUGIN WM_APP + 180 // [plug-in iface, 0, 1 or salCmd+2 or menuCmd+502] - sets ShouldUnload or ShouldRebuildMenu or adds salCmd/menuCmd to plug-in data
#define WM_USER_POSTMENUEXTCMD WM_APP + 181        // [plug-in iface, cmdID] - post menu-ext-cmd from plug-in

#define WM_USER_SHOWPLUGINMSGBOX WM_APP + 185 // [0, 0] - opening msg-box about plug-in above Bug Report dialog

// commands for main thread (cannot be executed in other thread) - used by Find dialog (runs in its own thread)
#define WM_USER_VIEWFILE WM_APP + 190     // [COpenViewerData *, altView] - opening file in (alternate) viewer
// wParam is a CEditFileData* (was a bare 'char* name'; that couldn't
// carry an exact wide twin alongside it, see COpenViewerData::FileNameW above)
#define WM_USER_EDITFILE WM_APP + 191     // [CEditFileData *, 0] - opening file in editor
#define WM_USER_VIEWFILEWITH WM_APP + 192 // [COpenViewerData *, handlerID] - opening file in selected viewer
#define WM_USER_EDITFILEWITH WM_APP + 193 // [CEditFileData *, handlerID] - opening file in selected editor

#define WM_USER_DISPACHCHANGENOTIF WM_APP + 194 // [0, time] - request to dispatch messages about path changes

#define WM_USER_DISPACHCFGCHANGE WM_APP + 195 // [0, 0] - request to dispatch messages about configuration changes between plug-ins

#define WM_USER_CFGCHANGED WM_APP + 196 // [0, 0] - sent to internal viewers and find windows after configuration change

#define WM_USER_CLEARHISTORY WM_APP + 197 // [0, 0] - informs window that it should clear all comboboxes containing histories

#define WM_USER_REFRESHTOOLTIP WM_APP + 198 // posted to tooltip window: causes text re-extraction, window resize and repaint
#define WM_USER_HIDETOOLTIP WM_APP + 199    // message for crossing thread boundary; hides tooltip

////////////////////////////////////////////////////////
//                                                    //
// Space WM_APP + 200 to WM_APP + 399 is reserved     //
// for messages also going to plugin windows.         //
// Definitions are in plugins\shared\spl_*.h          //
//                                                    //
////////////////////////////////////////////////////////

#define WM_USER_ENUMFILENAMES WM_APP + 400 // [requestUID, 0] - informs source (panels and Finds) that they should handle request for file enumeration for viewer

#define WM_USER_SM_END_NOTIFY_DELAYED WM_APP + 401  // [0, 0] notification about suspend mode end (delayed by 200ms to avoid collision with WM_QUERYENDSESSION during Shutdown / Log Off)
#define WM_USER_REFRESH_DIR_EX_DELAYED WM_APP + 402 // [FALSE, time] - differs from WM_USER_REFRESH_DIR in that it's probably unnecessary refresh (reason: window activation, lock-volume request (similar to "hands-off"), etc.) (delayed by 200ms or 5s to avoid collision with WM_QUERYENDSESSION during Shutdown / Log Off or to give process requesting lock-volume time to lock the volume)

#define WM_USER_CLOSE_MAINWND WM_APP + 403 // [0, 0] - used instead of WM_CLOSE for closing Salamander main window (advantage: can detect if not being distributed from other than main message-loop)

#define WM_USER_HELP_MOUSEMOVE WM_APP + 405  // [0, mousePos] - sent during Shift+F1 (ctx help) mode to all child windows; after receiving one or more WM_USER_MOUSEMOVE, WM_USER_MOUSELEAVE comes; used for tracking mouse cursor without setting capture
#define WM_USER_HELP_MOUSELEAVE WM_APP + 406 // [0, 0] - comes after WM_USER_MOUSEMOVE when cursor leaves child

//#define WM_USER_RENAME_NEXT_ITEM       WM_APP + 407 // [next, 0] - posted after pressing (Shift)Tab in inplace QuickRename to move to (previous) next item; inspired by Vista; 'next' is TRUE for next and FALSE for previous

#define WM_USER_PROGRDLG_UPDATEICON WM_APP + 408 // [0, 0] - CProgressDlgArray: after delivering this message to CProgressDialog, dialog icon will be updated

#define WM_USER_FORCECLOSE_MAINWND WM_APP + 409 // [0, 0] - forced close of Salamander main window

#define WM_USER_INACTREFRESH_DIR WM_APP + 410 // [0, time] - delayed refresh when Salamander main window is inactive

#define WM_USER_WAKEUP_FROM_IDLE WM_APP + 411 // [0, 0] - if main thread is in IDLE, it wakes up

#define WM_USER_FINDFULLROWSEL WM_APP + 412 // [0, 0] - find windows should set their list view to match Configuration.FindFullRowSelect variable

#define WM_USER_SLGINCOMPLETE WM_APP + 414 // [0, 0] - notification that SLG is not completely translated, motivational text to encourage participation

#define WM_USER_USERMENUICONS_READY WM_APP + 415 // [bkgndReaderData, threadID] - notification for main window that reading icons for User Menu completed in thread with ID 'threadID'

// states for Shift+F1 help mode
#define HELP_INACTIVE 0 // not in Shift+F1 help mode (must be 0)
#define HELP_ACTIVE 1   // in Shift+F1 help mode (non-zero)
#define HELP_ENTERING 2 // entering Shift+F1 help mode (non-zero)

#define STACK_CALLS_BUF_SIZE 5000       // each thread will have 5KB space for text call-stack
#define STACK_CALLS_MAX_MESSAGE_LEN 500 // we assume longest message is 500 characters

#define MENU_MARK_CX 9 // check mark dimensions for menu
#define MENU_MARK_CY 9

#define BOTTOMBAR_CX 17 // button dimension in bottomtb.bmp file (in points)
#define BOTTOMBAR_CY 13

// Panel/viewer color indices into CurrentColors[] (FOCUS_*, ITEM_*, ICON_BLEND_*,
// PROGRESS_*, HOT_*, *_CAPTION_*, THUMBNAIL_FRAME_*, VIEWER_*) and NUMBER_OF_COLORS.
// Extracted so headless units (sal_colors.cpp + its test) can reuse them without consts.h.
#include "sal_color_ids.h"

// internal color holder that also contains flag
typedef DWORD SALCOLOR;

// SALCOLOR flags
#define SCF_DEFAULT 0x01 // color component is ignored and default value is used

#define GetCOLORREF(rgbf) ((COLORREF)rgbf & 0x00ffffff)
#define RGBF(r, g, b, f) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16) | (((DWORD)(BYTE)(f)) << 24)))
#define GetFValue(rgbf) ((BYTE)((rgbf) >> 24))

inline void SetRGBPart(SALCOLOR* salColor, COLORREF rgb)
{
    *salColor = rgb & 0x00ffffff | (((DWORD)(BYTE)((BYTE)((*salColor) >> 24))) << 24);
}

extern SALCOLOR* CurrentColors;               // current colors
extern SALCOLOR UserColors[NUMBER_OF_COLORS]; // modified colors

extern SALCOLOR SalamanderColors[NUMBER_OF_COLORS]; // standard colors
extern SALCOLOR ExplorerColors[NUMBER_OF_COLORS];   // standard colors
extern SALCOLOR NortonColors[NUMBER_OF_COLORS];     // standard colors
extern SALCOLOR NavigatorColors[NUMBER_OF_COLORS];  // standard colors

extern SALCOLOR ViewerColors[NUMBER_OF_VIEWERCOLORS]; // viewer colors

extern COLORREF CustomColors[NUMBER_OF_CUSTOMCOLORS]; // for standard color dialog

#define CARET_WIDTH 2
#define MIN_PANELWIDTH 5 // narrower panel doesn't get focus

#define REFRESH_PAUSE 200 // pause between two closest refreshes

extern int SPACE_WIDTH; // space between columns in detailed view

#define MENU_CHECK_WIDTH 8 // check bitmap dimensions for menu
#define MENU_CHECK_HEIGHT 8

// counts of remembered strings
#define SELECT_HISTORY_SIZE 20    // select / unselect
#define COPY_HISTORY_SIZE 20      // copy / move
#define EDIT_HISTORY_SIZE 30      // command line
#define CHANGEDIR_HISTORY_SIZE 20 // Shift+F7
#define PATH_HISTORY_SIZE 30      // forward/backward path history + visited paths history (Alt+F12)
#define FILTER_HISTORY_SIZE 15    // filter
#define FILELIST_HISTORY_SIZE 15
#define CREATEDIR_HISTORY_SIZE 20   // create directory
#define QUICKRENAME_HISTORY_SIZE 20 // quick rename
#define EDITNEW_HISTORY_SIZE 20     // edit new
#define CONVERT_HISTORY_SIZE 15     // convert

#define VK_LBRACKET 219
#define VK_BACKSLASH 220
#define VK_RBRACKET 221

// when to test attempt to interrupt script building
#define BS_TIMEOUT 200 // milliseconds since last test

// band identification in rebar
#define BANDID_MENU 1
#define BANDID_TOPTOOLBAR 2
#define BANDID_UMTOOLBAR 3
#define BANDID_DRIVEBAR 4
#define BANDID_DRIVEBAR2 5
#define BANDID_WORKER 6
#define BANDID_HPTOOLBAR 7
#define BANDID_PLUGINSBAR 8

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

// specifies reason why some files/directories are not contained in panel
#define HIDDEN_REASON_ATTRIBUTE 0x00000001 // have hidden or system attribute and configuration suppresses display of such files/directories
#define HIDDEN_REASON_FILTER 0x00000002    // file is filtered out based on panel filter
#define HIDDEN_REASON_HIDECMD 0x00000004   // name was hidden by Hide Selected/Unselected Names command

// bit field of drives 'a' .. 'z'
#define DRIVES_MASK 0x03FFFFFF

//
// ****************************************************************************

// Windows XP, Windows 2003.NET, Vista, Windows 7, Windows 8, Windows 8.1, Windows 10
extern BOOL WindowsXP64AndLater;  // Windows XP64 or later
extern BOOL WindowsVistaAndLater; // Windows Vista or later
extern BOOL Windows7AndLater;     // Windows 7 or later
extern BOOL Windows8AndLater;     // Windows 8 or later
extern BOOL Windows8_1AndLater;   // Windows 8.1 or later
extern BOOL Windows10AndLater;    // Windows 10 or later

extern BOOL Windows64Bit; // x64 version of Windows

extern BOOL RunningAsAdmin; // TRUE if Salamander runs "As Administrator"

extern DWORD CCVerMajor; // common controls DLL version
extern DWORD CCVerMinor;

extern const char* SALAMANDER_TEXT_VERSION; // text application label including version
// Native UTF-16 application label built directly from wide version literals.
const wchar_t* SALAMANDER_TEXT_VERSIONW();

extern const char* LOW_MEMORY;
extern const wchar_t* MAINWINDOW_NAME;
// wchar_t-generic: registered through CWindow::RegisterUniversalClass's own
// LPCWSTR parameter (CExecuteWindow paints itself directly via DrawTextW regardless of the
// class's own ANSI/Unicode registration, so this has no deeper runtime dependency - see the
// comment at CExecuteWindow's declaration).
extern const wchar_t* SAVEBITS_CLASSNAME;

extern const wchar_t* CFILESBOX_CLASSNAME;
extern const wchar_t* CMAINWINDOW_CLASSNAME;
extern const wchar_t* SAFEWAIT_CLASSNAMEW;
extern const wchar_t* SHELLEXECUTE_CLASSNAMEW;

extern const wchar_t* STR_NONE; // "(none)" - plug-ins: for DLLName and Version if they are unknown

extern std::wstring DefaultDir['Z' - 'A' + 1]; // where to go on drive change
// This said `char` while sally_entry_lifecycle.cpp defines it wchar_t, and
// the bound said 'z'-'a' against a definition indexed by 'A'. Both now match the definition.

extern int MyTimeCounter;                   // increment after each use !
extern CRITICAL_SECTION TimeCounterSection; // for synchronization of access to ^

extern HINSTANCE NtDLL;               // handle to ntdll.dll
extern HINSTANCE Shell32DLL;          // handle to shell32.dll (icons)
extern HINSTANCE ImageResDLL;         // handle to imageres.dll (icons - Vista+)
extern HINSTANCE User32DLL;           // handle to user32.dll (DisableProcessWindowsGhosting)
extern HINSTANCE HLanguage;           // handle to language-dependent resources (path: Configuration.LoadedSLGName)
extern std::wstring CurrentHelpDir; // after first use of help contains path to help directory (location of all .chm files)
extern WORD LanguageID;               // language-id of language-dependent resources (.SLG file)

extern BOOL UseCustomPanelFont; // if TRUE, Font and FontUL come from LogFont structure; otherwise from system font (default)
extern HFONT Font;              // panel font
extern HFONT FontUL;            // underlined version
extern int FontCharHeight;      // font height
extern LOGFONT LogFont;         // structure describing panel font

BOOL CreatePanelFont(); // fills Font, FontUL and FontCharHeight based on LogFont

// #96: bold labels in the bottom key bar. OFF by default: it made the words heavier without
// touching the F-key glyphs, which are what the issue actually calls too small (they are a
// fixed 17x13 bitmap that never scales - see the ledger). Flip to 1 to bring the bold back.
#define SALLY_BOTTOMBAR_BOLD_LABELS 0

extern HFONT EnvFont;         // environment font (edit, toolbar, header, status)
extern HFONT EnvFontBold;     // bold variant of EnvFont (bottom toolbar shortcut labels, #96)
extern HFONT EnvFontUL;       // underlined listbox font
extern int EnvFontCharHeight; // font height
extern HFONT TooltipFont;     // font for tooltips (and statusbars, but we don't use it there)

BOOL GetSystemGUIFont(LOGFONT* lf); // returns font used for Salamander main window
BOOL CreateEnvFonts();              // fills EnvFont, EnvFontUL, EnvFontCharHeight, TooltipFont based on metrics

extern DWORD MouseHoverTime; // after what time highlighting should occur

extern HBRUSH HNormalBkBrush;        // normal item background in panel
extern HBRUSH HFocusedBkBrush;       // focused item background in panel
extern HBRUSH HSelectedBkBrush;      // selected item background in panel
extern HBRUSH HFocSelBkBrush;        // focused & selected item background in panel
extern HBRUSH HDialogBrush;          // dialog background fill
extern HBRUSH HButtonTextBrush;      // button text
extern HBRUSH HDitherBrush;          // 1 bit color depth checkerboard; when drawing, color can be set via SetTextColor/SetBkColor
extern HBRUSH HActiveCaptionBrush;   // active panel caption background
extern HBRUSH HInactiveCaptionBrush; // inactive panel caption background

extern HBRUSH HMenuSelectedBkBrush;
extern HBRUSH HMenuSelectedTextBrush;
extern HBRUSH HMenuHilightBrush;
extern HBRUSH HMenuGrayTextBrush;

extern HACCEL AccelTable1; // accelerators in panels and cmdline
extern HACCEL AccelTable2; // accelerators in cmdline

extern int SystemDPI;

enum CIconSizeEnum
{
    ICONSIZE_16,   // 16x16 @ 100%DPI, 20x20 @ 125%DPI, 24x24 @ 150%DPI, ...
    ICONSIZE_32,   // 32x32 @ 100%DPI, ...
    ICONSIZE_48,   // 48x48 @ 100%DPI, ...
    ICONSIZE_COUNT // items count
};

extern int IconSizes[ICONSIZE_COUNT]; // icon sizes: 16, 32, 48
extern int IconLRFlags;               // controls color depth of loaded icons

// WARNING: on Vista for 48x48 icons ICONSIZE_32 overlay is used and for thumbnails ICONSIZE_48 overlay is used
extern HICON HSharedOverlays[ICONSIZE_COUNT];   // shared (hand) in all sizes
extern HICON HShortcutOverlays[ICONSIZE_COUNT]; // shortcut (bottom left corner) in all sizes
extern HICON HSlowFileOverlays[ICONSIZE_COUNT]; // slow files

extern CIconList* SimpleIconLists[ICONSIZE_COUNT]; // simple icons in all sizes

#define THROBBER_WIDTH 12 // single cell dimensions
#define THROBBER_HEIGHT 12
#define THROBBER_COUNT 36     // total number of cells
#define IDT_THROBBER_DELAY 30 // delay [ms] in animation of single cell
extern CIconList* ThrobberFrames;

#define LOCK_WIDTH 8 // single cell dimensions
#define LOCK_HEIGHT 13
extern CIconList* LockFrames;

extern HICON HGroupIcon;   // group for UserMenu popups
extern HICON HFavoritIcon; // hot path

#define TILE_LEFT_MARGIN 4 // number of points to the left before icon

extern RGBQUAD ColorTable[256]; // palette used for all toolbars (including plugins)

// individual positions in SymbolsImageList and LargeSymbolsImageList imagelist
enum CSymbolsImageListIndexes
{
    symbolsExecutable,    // 0: exe/bat/pif/com
    symbolsDirectory,     // 1: dir
    symbolsNonAssociated, // 2: non-associated file
    symbolsAssociated,    // 3: associated file
    symbolsUpDir,         // 4: up-dir ".."
    symbolsArchive,       // 5: archive
    symbolsCount          // TERMINATOR
};

extern HIMAGELIST HFindSymbolsImageList; // symbols for find
extern HIMAGELIST HMenuMarkImageList;    // check marks for menu
extern HIMAGELIST HGrayToolBarImageList; // toolbar and menu in gray version (calculated from color version)
extern HIMAGELIST HHotToolBarImageList;  // toolbar and menu in color version
extern HIMAGELIST HBottomTBImageList;    // bottom toolbar (F1 - F12)
extern HIMAGELIST HHotBottomTBImageList; // bottom toolbar (F1 - F12)

extern HPEN HActiveNormalPen; // pens for frame around item
extern HPEN HActiveSelectedPen;
extern HPEN HInactiveNormalPen;
extern HPEN HInactiveSelectedPen;

extern HPEN HThumbnailNormalPen; // pens for frame around thumbnail
extern HPEN HThumbnailFucsedPen;
extern HPEN HThumbnailSelectedPen;
extern HPEN HThumbnailFocSelPen;

extern HPEN BtnShadowPen;
extern HPEN BtnHilightPen;
extern HPEN Btn3DLightPen;
extern HPEN BtnFacePen;
extern HPEN WndFramePen;
extern HPEN WndPen;

extern HBITMAP HFilter; // bitmap - panel hides some files or directories

extern HBITMAP HHeaderSort; // arrows for HeaderLine

extern CBitmap ItemBitmap; // helper for everything: drawing items in panel, header line, ...

extern HBITMAP HUpDownBitmap; // Arrows for scrolling inside short popup menus.
extern HBITMAP HZoomBitmap;   // Panel zoom

//extern HBITMAP HWorkerBitmap;

extern HCURSOR HHelpCursor; // Context Help cursor - loaded only when needed

#define THUMBNAIL_SIZE_DEFAULT 94 // according to XP
#define THUMBNAIL_SIZE_MIN 48     // if we want to support smaller than 48, smaller icons need to be displayed
#define THUMBNAIL_SIZE_MAX 1000

extern BOOL DragFullWindows; // if TRUE, change panel size realtime, otherwise after release (optimization for remote desktop)

// CConfiguration::SizeFormat (format of Size column in panels)
// !WARNING! do not change constants, they are exported to plugins via SALCFG_SIZEFORMAT
#define SIZE_FORMAT_BYTES 0 // in bytes (Open Salamander)
#define SIZE_FORMAT_KB 1    // in KB (Windows Explorer)
#define SIZE_FORMAT_MIXED 2 // bytes, KB, MB, GB, ...

// registry key names
extern const wchar_t* SALAMANDER_ROOT_REG;

// Publishes SALAMANDER_ROOT_REG into this process's environment (SAL_ENV_CONFIG_ROOT_*) so
// in-process plugins read the same configuration key the core does. Call after every change to
// SALAMANDER_ROOT_REG, including when it becomes NULL.
void PublishConfigRootToEnvironment();
extern const wchar_t* SALAMANDER_SAVE_IN_PROGRESS;
extern const wchar_t* SALAMANDER_CONFIG_REG;
extern const wchar_t* SALAMANDER_VERSION_REG;
extern const wchar_t* SALAMANDER_VERSIONREG_REG;
extern const wchar_t* CONFIG_ONLYONEINSTANCE_REG;
extern const wchar_t* CONFIG_LANGUAGE_REG;
extern const wchar_t* CONFIG_ALTLANGFORPLUGINS_REG;
extern const wchar_t* CONFIG_LANGUAGECHANGED_REG;
extern const wchar_t* CONFIG_USEALTLANGFORPLUGINS_REG;
extern const wchar_t* CONFIG_STATUSAREA_REG;
extern const wchar_t* CONFIG_SHOWSPLASHSCREEN_REG;
extern const wchar_t* CONFIG_ENABLECUSTICOVRLS_REG;
extern const wchar_t* CONFIG_DISABLEDCUSTICOVRLS_REG;
extern const wchar_t* VIEWERS_MASKS_REG;
extern const wchar_t* VIEWERS_COMMAND_REG;
extern const wchar_t* VIEWERS_ARGUMENTS_REG;
extern const wchar_t* VIEWERS_INITDIR_REG;
extern const wchar_t* VIEWERS_TYPE_REG;
extern const wchar_t* EDITORS_MASKS_REG;
extern const wchar_t* EDITORS_COMMAND_REG;
extern const wchar_t* EDITORS_ARGUMENTS_REG;
extern const wchar_t* EDITORS_INITDIR_REG;
extern const wchar_t* SALAMANDER_PLUGINSCONFIG;
extern const wchar_t* SALAMANDER_PLUGINS_NAME;
extern const wchar_t* SALAMANDER_PLUGINS_DLLNAME;
extern const wchar_t* SALAMANDER_PLUGINS_VERSION;
extern const wchar_t* SALAMANDER_PLUGINS_COPYRIGHT;
extern const wchar_t* SALAMANDER_PLUGINS_EXTENSIONS;
extern const wchar_t* SALAMANDER_PLUGINS_DESCRIPTION;
extern const wchar_t* SALAMANDER_PLUGINS_LASTSLGNAME;
extern const wchar_t* SALAMANDER_PLUGINS_HOMEPAGE;
//extern const char *SALAMANDER_PLUGINS_PLGICONS;
extern const wchar_t* SALAMANDER_PLUGINS_PLGICONLIST;
extern const wchar_t* SALAMANDER_PLUGINS_PLGICONINDEX;
extern const wchar_t* SALAMANDER_PLUGINS_PLGSUBMENUICONINDEX;
extern const wchar_t* SALAMANDER_PLUGINS_SUBMENUINPLUGINSBAR;
extern const wchar_t* SALAMANDER_PLUGINS_THUMBMASKS;
extern const wchar_t* SALAMANDER_PLUGINS_REGKEYNAME;
extern const wchar_t* SALAMANDER_PLUGINS_FSNAME;
extern const wchar_t* SALAMANDER_PLUGINS_FUNCTIONS;
extern const wchar_t* SALAMANDER_PLUGINS_LOADONSTART;
extern const wchar_t* SALAMANDER_PLUGINS_LEGACYCOMPATAPPROVED;
extern const wchar_t* SALAMANDER_PLUGINS_MENU;
extern const wchar_t* SALAMANDER_PLUGINS_MENUITEMNAME;
extern const wchar_t* SALAMANDER_PLUGINS_MENUITEMHOTKEY;
extern const wchar_t* SALAMANDER_PLUGINS_MENUITEMSTATE;
extern const wchar_t* SALAMANDER_PLUGINS_MENUITEMID;
extern const wchar_t* SALAMANDER_PLUGINS_MENUITEMSKILLLEVEL;
extern const wchar_t* SALAMANDER_PLUGINS_MENUITEMICONINDEX;
extern const wchar_t* SALAMANDER_PLUGINS_MENUITEMTYPE;
extern const wchar_t* SALAMANDER_PLUGINS_FSCMDNAME;
extern const wchar_t* SALAMANDER_PLUGINS_FSCMDICON;
extern const wchar_t* SALAMANDER_PLUGINS_FSCMDVISIBLE;
extern const wchar_t* SALAMANDER_PLUGINSORDER_SHOW;
extern const wchar_t* SALAMANDER_PLUGINS_ISNETHOOD;
extern const wchar_t* SALAMANDER_PLUGINS_USESPASSWDMAN;

// following 8 strings are only for loading config version 6 and lower, newer versions
// already use SALAMANDER_PLUGINS_FUNCTIONS (stored in bits of DWORD function mask)
extern const wchar_t* SALAMANDER_PLUGINS_PANELVIEW;
extern const wchar_t* SALAMANDER_PLUGINS_PANELEDIT;
extern const wchar_t* SALAMANDER_PLUGINS_CUSTPACK;
extern const wchar_t* SALAMANDER_PLUGINS_CUSTUNPACK;
extern const wchar_t* SALAMANDER_PLUGINS_CONFIG;
extern const wchar_t* SALAMANDER_PLUGINS_LOADSAVE;
extern const wchar_t* SALAMANDER_PLUGINS_VIEWER;
extern const wchar_t* SALAMANDER_PLUGINS_FS;

// clipboard format for SalIDataObject (mark of our IDataObject on clipboard)
extern const char* SALCF_IDATAOBJECT;
// clipboard format for CFakeDragDropDataObject (specifies path that should appear after drop
// in directory-line, command-line + blocks drop to usermenu-toolbar); if more
// directories/files are dragged, path is empty string (drop to directory/command-line not possible)
extern const char* SALCF_FAKE_REALPATH;
// clipboard format for CFakeDragDropDataObject (specifies source type - 1=archive, 2=FS)
extern const char* SALCF_FAKE_SRCTYPE;
// clipboard format for CFakeDragDropDataObject (only if source is FS: source FS path)
extern const char* SALCF_FAKE_SRCFSPATH;

// variables for CanChangeDirectory() and AllowChangeDirectory()
extern int ChangeDirectoryAllowed; // 0 means directory can be changed
extern BOOL ChangeDirectoryRequest;
// function handling automatic change of current directory to system directory
// - due to unmapping from other software and deleting directories displayed by Salamander
BOOL CanChangeDirectory();
void AllowChangeDirectory(BOOL allow);

// variable for BeginStopRefresh() and EndStopRefresh()
extern int StopRefresh;
// after call no directory refresh will be performed
void BeginStopRefresh(BOOL debugSkipOneCaller = FALSE, BOOL debugDoNotTestCaller = FALSE);
// releases refreshing -> optionally sends WM_USER_SM_END_NOTIFY to main window (missed refreshes will occur)
void EndStopRefresh(BOOL postRefresh = TRUE, BOOL debugSkipOneCaller = FALSE, BOOL debugDoNotTestCaller = FALSE);

// variable checked in main message-loop in "idle" part, if TRUE, finds and unloads
// plug-ins with ShouldUnload==TRUE, rebuilds menu for plugins with ShouldRebuildMenu==TRUE + executes
// commands posted from plugins + requested Salamander commands
extern BOOL ExecCmdsOrUnloadMarkedPlugins;

// variable checked in main message-loop in "idle" part, if TRUE, opens Pack/Unpack
// dialog for plugins with OpenPackDlg==TRUE or OpenUnpackDlg==TRUE
extern BOOL OpenPackOrUnpackDlgForMarkedPlugins;

// variable for BeginStopIconRepaint() and EndStopIconRepaint()
extern int StopIconRepaint;
extern BOOL PostAllIconsRepaint;
// after call no icon refresh in panels will be performed
void BeginStopIconRepaint();
// releases repaint -> optionally sends WM_USER_REPAINTALLICONS to main window (refresh all icons)
void EndStopIconRepaint(BOOL postRepaint = TRUE);

// variable for BeginStopStatusbarRepaint() and EndStopStatusbarRepaint()
extern int StopStatusbarRepaint;
extern BOOL PostStatusbarRepaint;
// after call throbber stops repainting
void BeginStopStatusbarRepaint();
// releases repaint
void EndStopStatusbarRepaint();

// in msgbox.cpp module - centered messagebox according to real parent of hParent
// Core message-box presentation is native-wide. Frozen v107 and transitional v108 byte
// projections live in their respective plug-in adapters rather than in this core surface.
int SalMessageBoxW(HWND hParent, const wchar_t* lpText, const wchar_t* lpCaption, UINT uType);
int SalMessageBoxEx(const MSGBOXEX_PARAMS* params);

// draws icons from imagelist with set styles
#define IMAGE_STATE_FOCUSED 0x00000001
#define IMAGE_STATE_SELECTED 0x00000002
#define IMAGE_STATE_HIDDEN 0x00000004
#define IMAGE_STATE_SHARED 0x00000100
#define IMAGE_STATE_SHORTCUT 0x00000200
#define IMAGE_STATE_MASK 0x00000400
#define IMAGE_STATE_OFFLINE 0x00000800
BOOL StateImageList_Draw(CIconList* iconList, int imageIndex, HDC hDC, int xDst, int yDst,
                         DWORD state, CIconSizeEnum iconSize, DWORD iconOverlayIndex,
                         const RECT* overlayRect, BOOL overlayOnly, BOOL iconOverlayFromPlugin,
                         int pluginIconOverlaysCount, HICON* pluginIconOverlays);
DWORD GetImageListColorFlags(); // returns ILC_COLOR??? according to Windows version - tuned for imagelist usage in listviews

// API GetOpenFileName/GetSaveFileName in case file path (OPENFILENAME::lpstrFile)
// doesn't exist (or contains e.g. C:\) returns FALSE and CommDlgExtendedError() == FNERR_INVALIDFILENAME.
// Therefore we introduce their "safe" variant which detects this case and attempts to open
// dialog for Documents or Desktop.
BOOL SafeGetOpenFileName(LPOPENFILENAME lpofn);
BOOL SafeGetSaveFileName(LPOPENFILENAME lpofn);
BOOL SafeGetOpenFileNameW(LPOPENFILENAMEW lpofn);
BOOL SafeGetSaveFileNameW(LPOPENFILENAMEW lpofn);
BOOL SafeGetOpenFileNameOwnedW(LPOPENFILENAMEW lpofn, std::wstring& fileName);
BOOL SafeGetSaveFileNameOwnedW(LPOPENFILENAMEW lpofn, std::wstring& fileName);
BOOL SafeGetOpenFileNamesOwnedW(LPOPENFILENAMEW lpofn, std::vector<std::wstring>& fileNames);

extern std::wstring DecimalSeparator;
extern std::wstring ThousandsSeparator;

extern DWORD SalamanderStartTime;     // Salamander start time (GetTickCount)
extern DWORD SalamanderExceptionTime; // exception time in Salamander (GetTickCount) or time of last Bug Report dialog invocation

extern BOOL SkipOneActivateRefresh; // should refresh be skipped on main window activation? (for internal viewers)

extern int MenuNewExceptionHasOccured; // has menu New crashed? (maybe corrupted memory somewhere)
extern int FGIExceptionHasOccured;     // has SHGetFileInfo crashed?
extern int ICExceptionHasOccured;      // has InvokeCommand crashed?
extern int QCMExceptionHasOccured;     // has QueryContextMenu crashed?
extern int OCUExceptionHasOccured;     // has OleUninitialize or CoUninitialize crashed?
extern int GTDExceptionHasOccured;     // has GetTargetDirectory crashed?
extern int SHLExceptionHasOccured;     // has something from ShellLib crashed?
extern int RelExceptionHasOccured;     // has some IUnknown method Release() call crashed?

extern BOOL SalamanderBusy;          // is Salamander busy?
extern DWORD LastSalamanderIdleTime; // GetTickCount() from moment when SalamanderBusy last changed to TRUE

extern int PasteLinkIsRunning; // if greater than zero, Paste Shortcuts command is currently running in one of the panels

extern BOOL CannotCloseSalMainWnd; // TRUE = main window must not be closed

// One cache per string, wide. The narrow DirColumnStr/ColExtStr used to sit
// beside these, and BOTH of their only consumers immediately did AnsiToWide() on them - so the
// pair existed to push a LOCALIZED column heading through CP_ACP and back at every use. A
// heading outside the machine's code page came out corrupted for no reason at all.
extern std::wstring DirColumnStrW; // LoadStrW(IDS_DIRCOLUMN), used by UTF-16 panel columns
extern int DirColumnStrWLen;       // string length in WCHARs
extern std::wstring ColExtStrW;    // LoadStrW(IDS_COLUMN_NAME_EXT) - used too often, we cache it
extern int ColExtStrWLen;          // string length in WCHARs
extern int TextEllipsisWidth;         // width of "..." string displayed with 'Font' font
extern int TextEllipsisWidthEnv;      // width of "..." string displayed with 'FontEnv' font
// These three were already wide and initialised from LoadStrW; only their comments
// still said LoadStr. That is why the KB counted this "family" as five narrow globals when it
// was two - the count was taken from the comments rather than the types.
extern std::wstring ProgDlgHoursStr;   // LoadStrW(IDS_PROGDLGHOURS) - used too often, we cache it
extern std::wstring ProgDlgMinutesStr; // LoadStrW(IDS_PROGDLGMINUTES) - used too often, we cache it
extern std::wstring ProgDlgSecsStr;    // LoadStrW(IDS_PROGDLGSECS) - used too often, we cache it

// All four are consumed by the panel's Type column, whose buffer
// (sally.h:320 TransferBuffer) and formatter (GetCommonFileTypeStr) are already wide. The
// declaration of FolderTypeName used to say 'char' while its definition said 'wchar_t' - a
// C2371, and the reason every consumer TU was reading a 160-byte object as 80 narrow chars.
extern wchar_t FolderTypeName[80];       // file-type for all directories (retrieved from system directory)
extern int FolderTypeNameLen;            // FolderTypeName length in CHARACTERS
extern std::wstring UpDirTypeName;       // LoadStrW(IDS_UPDIRTYPENAME) - used too often, we cache it
extern int UpDirTypeNameLen;             // length in CHARACTERS
extern std::wstring CommonFileTypeName;  // LoadStrW(IDS_COMMONFILETYPE) - used too often, we cache it
extern int CommonFileTypeNameLen;        // CommonFileTypeName length in CHARACTERS
extern std::wstring CommonFileTypeName2; // LoadStrW(IDS_COMMONFILETYPE2) - used too often, we cache it

extern std::wstring WindowsDirectory; // cached result of GetWindowsDirectory

//#ifdef MSVC_RUNTIME_CHECKS
#define RTC_ERROR_DESCRIPTION_SIZE 2000 // buffer for run-time check error description
extern char RTCErrorDescription[RTC_ERROR_DESCRIPTION_SIZE];
//#endif // MSVC_RUNTIME_CHECKS

// path where we create bug report and minidump, location: up to Vista near sally.exe, in Vista (and later) in CSIDL_APPDATA + "\\Open Salamander"
// file name that will be imported (if exists) to registry
extern std::wstring ConfigurationNameW;
extern BOOL ConfigurationNameIgnoreIfNotExists;

extern HWND PluginProgressDialog; // if plug-in opens progress dialog, its HWND is here, otherwise NULL
extern HWND PluginMsgBoxParent;   // parent for plug-in messageboxes (main window, Plugins dialog, etc.)

extern BOOL CriticalShutdown; // TRUE = "critical shutdown" is in progress, no time to ask, finishing quickly, 5s until killed

// "translation" of POSIX names to MS
#define itoa _itoa
#define stricmp _stricmp
#define strnicmp _strnicmp

// skill levels
#define SKILL_LEVEL_BEGINNER 0
#define SKILL_LEVEL_INTERMEDIATE 1
#define SKILL_LEVEL_ADVANCED 2

// converts CConfiguration::SkillLevel variable to SkillLevel for menu.
DWORD CfgSkillLevelToMenu(BYTE cfgSkillLevel);

// attributes that we show in panel and that need to be masked for example when comparing directories
// WARNING: FILE_ATTRIBUTE_DIRECTORY is not displayed as attribute, so it doesn't belong in mask
#define DISPLAYED_ATTRIBUTES (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | \
                              FILE_ATTRIBUTE_SYSTEM | \
                              FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_ENCRYPTED | \
                              FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_COMPRESSED | \
                              FILE_ATTRIBUTE_OFFLINE)

// timers
#define IDT_SCROLL 930
#define IDT_REPAINT 931
#define IDT_DRAGDROPTESTAGAIN 932
#define IDT_PANELSCROLL 933
#define IDT_SINGLECLICKSELECT 934
#define IDT_FLASHICON 935
#define IDT_QUICKRENAMEBEGIN 936
#define IDT_PLUGINFSTIMERS 937
#define IDT_EDITLB 938
#define IDT_PROGRESSSELFMOVE 939
#define IDT_DELETEMNGR_PROCESS 940
#define IDT_ADDNEWMODULES 941
#define IDT_POSTENDSUSPMODE 942
#define IDT_ASSOCIATIONSCHNG 943
#define IDT_SM_END_NOTIFY 944
#define IDT_REFRESH_DIR_EX 945
#define IDT_UPDATESTATUS 946
#define IDT_ICONOVRREFRESH 947
#define IDT_INACTIVEREFRESH 948
#define IDT_THROBBER 949
#define IDT_DELAYEDTHROBBER 950
#define IDT_UPDATETASKLIST 951

// WARNING: almost all functions in this section display LOAD / SAVE configuration
//          error messages on failure, which makes them unsuitable for regular Registry access,
//          for solution see functions at the beginning of regwork.h: OpenKeyAux, CreateKeyAux, etc.
BOOL ClearKey(HKEY key);
// ****************************************************************************
// Direct, quiet registry facades used outside the configuration worker flow.
// Registry key/value names are always UTF-16. The raw value overloads preserve
// REG_BINARY and other explicitly encoded payload bytes unchanged.
BOOL CreateKeyW(HKEY hKey, const wchar_t* name, HKEY& createdKey);
BOOL OpenKeyW(HKEY hKey, const wchar_t* name, HKEY& openedKey);
BOOL DeleteKeyW(HKEY hKey, const wchar_t* name);
BOOL DeleteValueW(HKEY hKey, const wchar_t* name);
BOOL SetValueW(HKEY hKey, const wchar_t* name, DWORD type, const void* data, DWORD dataSize);
BOOL GetValueW(HKEY hKey, const wchar_t* name, DWORD type, void* buffer, DWORD bufferSize);
BOOL GetStringValueW(HKEY hKey, const wchar_t* name, std::wstring& value);
BOOL GetSizeW(HKEY hKey, const wchar_t* name, DWORD type, DWORD& bufferSize);

void CloseKey(HKEY key);
// Configuration load/save facades retain the worker-thread/message-pump behavior,
// but no longer accept or convert ACP key/value names.
BOOL CreateKey(HKEY hKey, const wchar_t* name, HKEY& createdKey);
BOOL OpenKey(HKEY hKey, const wchar_t* name, HKEY& openedKey);
BOOL DeleteKey(HKEY hKey, const wchar_t* name);
BOOL DeleteValue(HKEY hKey, const wchar_t* name);
BOOL SetValue(HKEY hKey, const wchar_t* name, DWORD type,
              const void* data, DWORD dataSize);
BOOL GetValue(HKEY hKey, const wchar_t* name, DWORD type, void* buffer, DWORD bufferSize);
BOOL GetSize(HKEY hKey, const wchar_t* name, DWORD type, DWORD& bufferSize);
BOOL GetValue2(HKEY hKey, const wchar_t* name, DWORD type1, DWORD type2,
               DWORD* returnedType, void* buffer, DWORD bufferSize);
// The value NAME is wide; the stored DATA is not. LoadLogFont/SaveLogFont read and
// write a comma-separated REG_SZ that existing installations already have, and LoadRGB still
// understands the pre-2.53 "r,g,b" string spelling - those are on-disk formats, not text this
// campaign gets to widen.
BOOL LoadRGB(HKEY hKey, const wchar_t* name, COLORREF& color);
BOOL SaveRGB(HKEY hKey, const wchar_t* name, COLORREF color);
BOOL LoadRGBF(HKEY hKey, const wchar_t* name, SALCOLOR& color);
BOOL SaveRGBF(HKEY hKey, const wchar_t* name, SALCOLOR color);
BOOL LoadLogFont(HKEY hKey, const wchar_t* name, LOGFONT* logFont);
BOOL SaveLogFont(HKEY hKey, const wchar_t* name, LOGFONT* logFont);
// Imports the malformed process-ACP REG_SZ payload written by pre-Unicode releases and publishes
// only dynamically allocated UTF-16 entries. Current history values use LoadHistory instead.
BOOL LoadLegacyHistory(HKEY hKey, const wchar_t* name, wchar_t* history[], int maxCount);
BOOL LoadHistory(HKEY hKey, const wchar_t* name, wchar_t* history[], int maxCount);
BOOL SaveHistory(HKEY hKey, const wchar_t* name, wchar_t* history[], int maxCount, BOOL onlyClear = FALSE);
BOOL LoadViewers(HKEY hKey, const wchar_t* name, CViewerMasks* viewerMasks);
BOOL SaveViewers(HKEY hKey, const wchar_t* name, CViewerMasks* viewerMasks);
BOOL LoadEditors(HKEY hKey, const wchar_t* name, CEditorMasks* editorMasks);
BOOL SaveEditors(HKEY hKey, const wchar_t* name, CEditorMasks* editorMasks);

BOOL ExportConfigurationW(HWND hParent, const wchar_t* fileName, BOOL clearKeyBeforeImport);
BOOL ImportConfigurationW(HWND hParent, const wchar_t* fileName, BOOL ignoreIfNotExists,
                          BOOL autoImportConfig, BOOL* importCfgFromFileWasSkipped);

class CHighlightMasks;
void UpdateDefaultColors(SALCOLOR* colors, CHighlightMasks* highlightMasks, BOOL processColors, BOOL processMasks);

extern BOOL ImageDragging;                                                // image is currently being dragged
extern BOOL ImageDraggingVisible;                                         // is image currently displayed?
void ImageDragBegin(int width, int height, int dxHotspot, int dyHotspot); // size of dragged image
void ImageDragEnd();                                                      // end dragging
BOOL ImageDragInterfereRect(const RECT* rect);                            // rect is in screen coordinates, check if dragged item interferes with rect
void ImageDragEnter(int x, int y);                                        // x and y are screen coordinates
void ImageDragMove(int x, int y);                                         // x and y are screen coordinates
void ImageDragLeave();
void ImageDragShow(BOOL show); // hide / show, doesn't affect ImageDragging, only ImageDraggingVisible

// sets hand-shaped cursor
HCURSOR SetHandCursor();

//******************************************************************************
//
// CreateToolbarBitmaps
//
// IN:   hInstance    - instance where bitmap with resID is located
//       resID        - input bitmap identifier
//       transparent  - this color will be transparent
//       bkColorForAlpha - color that will show through alpha parts of icons (WinXP)
// OUT:  hMaskBitmap  - mask (b&w)
//       hGrayBitmap  - gray version
//       hColorBitmap - color version
//

struct CSVGIcon
{
    int ImageIndex;
    // wide, matching CButtonData::SVGName which feeds it and svg.h's
    // RenderSVGImage declaration which consumes it. These are ASCII asset stems.
    const wchar_t* SVGName;
};

BOOL CreateToolbarBitmaps(HINSTANCE hInstance, int resID, COLORREF transparent, COLORREF bkColorForAlpha,
                          HBITMAP& hMaskBitmap, HBITMAP& hGrayBitmap, HBITMAP& hColorBitmap, BOOL appendIcons,
                          const CSVGIcon* svgIcons, int svgIconsCount);

//****************************************************************************
//
// CreateGrayscaleAndMaskBitmaps
//
// Creates a new 24-bit bitmap, copies source bitmap into it
// and converts it to grayscale. Also prepares second bitmap
// with mask according to transparent color.
//

BOOL CreateGrayscaleAndMaskBitmaps(HBITMAP hSource, COLORREF transparent,
                                   HBITMAP& hGrayscale, HBITMAP& hMask);

//******************************************************************************
//
// UpdateCrc32
//   Updates CRC-32 (32-bit Cyclic Redundancy Check) with specified array of bytes.
//
// Parameters
//   'buffer'
//      [in] Pointer to the starting address of the block of memory to update 'crcVal' with.
//
//   'count'
//      [in] Size, in bytes, of the block of memory to update 'crcVal' with.
//
//   'crcVal'
//      [in] Initial crc value. Set this value to zero to calculate CRC-32 of the 'buffer'.
//
// Return Values
//   Returns updated CRC-32 value.
//

DWORD UpdateCrc32(const void* buffer, DWORD count, DWORD crcVal);

//******************************************************************************
//
// Idle processing control (CMainWindow::OnEnterIdle)
//
// variables are global for easy access
// and not as CMainWindow class attributes
//

extern BOOL IdleRefreshStates;  // if set, state variables for commands (toolbar, menu) will be retrieved at next CMainWindow::OnEnterIdle
extern BOOL IdleForceRefresh;   // if IdleRefreshStates is set, setting IdleForceRefresh disables cache at Salamander level
extern BOOL IdleCheckClipboard; // if IdleRefreshStates==TRUE and this variable is also set, clipboard will be checked too (time consuming)

// ".." is not counted among files|directories
extern DWORD EnablerUpDir;                // does parent directory exist?
extern DWORD EnablerRootDir;              // are we not in root yet? (note: UNC root has updir, but it's a root)
extern DWORD EnablerForward;              // is forward available in history?
extern DWORD EnablerBackward;             // is backward available in history?
extern DWORD EnablerFileOnDisk;           // focus is on file && panel is disk
extern DWORD EnablerFileOnDiskOrArchive;  // focus is on file && panel is disk or archive
extern DWORD EnablerFileOrDirLinkOnDisk;  // focus is on file or directory link && panel is disk
extern DWORD EnablerFiles;                // focus|select is on files|directories
extern DWORD EnablerFilesOnDisk;          // focus|select is on files|directories && panel is disk
extern DWORD EnablerFilesOnDiskCompress;  // focus|select is on files|directories && panel is disk && compression is supported
extern DWORD EnablerFilesOnDiskEncrypt;   // focus|select is on files|directories && panel is disk && encryption is supported
extern DWORD EnablerFilesOnDiskOrArchive; // focus|select is on files|directories && panel is disk or archive
extern DWORD EnablerOccupiedSpace;        // panel is disk or archive with VALID_DATA_SIZE and EnablerFilesOnDiskOrArchive applies
extern DWORD EnablerFilesCopy;            // focus|select is on files|directories && panel is disk, archive or FS with "copy from fs" support
extern DWORD EnablerFilesMove;            // focus|select is on files|directories && panel is disk or FS with "move from fs" support
extern DWORD EnablerFilesDelete;          // focus|select is on files|directories && (panel is disk, editable archive or FS with "delete" support)
extern DWORD EnablerFileDir;              // focus is on file|directory
extern DWORD EnablerFileDirANDSelected;   // focus is on file|directory && some files|directories are selected
extern DWORD EnablerQuickRename;          // focus is on file|directory && panel is disk or FS (with quick-rename support)
extern DWORD EnablerOnDisk;               // panel is disk
extern DWORD EnablerCalcDirSizes;         // panel is disk or archive with VALID_DATA_SIZE
extern DWORD EnablerPasteFiles;           // can Paste be performed? (files on clipboard) (used as memory of last clipboard state for 'pasteFiles' in CMainWindow::RefreshCommandStates())
extern DWORD EnablerPastePath;            // can Paste be performed? (path text on clipboard) (used as memory of last clipboard state for 'pastePath' in CMainWindow::RefreshCommandStates())
extern DWORD EnablerPasteLinks;           // can Paste Links be performed? (files via "copy" on clipboard) (used as memory of last clipboard state for 'pasteLinks' in CMainWindow::RefreshCommandStates())
extern DWORD EnablerPasteSimpleFiles;     // are files/directories from single path on clipboard? (i.e.: is there a chance for Paste to archive or FS?)
extern DWORD EnablerPasteDefEffect;       // what is default paste-effect, can be combination DROPEFFECT_COPY+DROPEFFECT_MOVE (i.e.: was it Copy or Cut?)
extern DWORD EnablerPasteFilesToArcOrFS;  // is Paste of files to archive/FS in current panel possible? (panel contains archive/FS && EnablerPasteSimpleFiles && operation according to EnablerPasteDefEffect is possible)
extern DWORD EnablerPaste;                // can Paste be performed? (files on clipboard && panel is disk || paste to archive or FS is possible || path text on clipboard)
extern DWORD EnablerPasteLinksOnDisk;     // can Paste Links be performed and panel is disk?
extern DWORD EnablerSelected;             // are some files|directories selected
extern DWORD EnablerUnselected;           // does at least one unselected file|directory exist (UpDir ".." is not considered)
extern DWORD EnablerHiddenNames;          // HiddenNames array contains some names
extern DWORD EnablerSelectionStored;      // is selection stored in OldSelection of active panel?
extern DWORD EnablerGlobalSelStored;      // is selection stored in GlobalSelection?
extern DWORD EnablerSelGotoPrev;          // does selected item exist before focus?
extern DWORD EnablerSelGotoNext;          // does selected item exist after focus?
extern DWORD EnablerLeftUpDir;            // does parent directory exist in left panel?
extern DWORD EnablerRightUpDir;           // does parent directory exist in right panel?
extern DWORD EnablerLeftRootDir;          // are we not in root yet in left panel? (note: UNC root has updir, but it's a root)
extern DWORD EnablerRightRootDir;         // are we not in root yet in right panel? (note: UNC root has updir, but it's a root)
extern DWORD EnablerLeftForward;          // is forward available in left panel history?
extern DWORD EnablerRightForward;         // is forward available in right panel history?
extern DWORD EnablerLeftBackward;         // is backward available in left panel history?
extern DWORD EnablerRightBackward;        // is backward available in right panel history?
extern DWORD EnablerFileHistory;          // is file available in view/edit history?
extern DWORD EnablerDirHistory;           // is directory available in directory history?
extern DWORD EnablerCustomizeLeftView;    // can columns be configured for left view?
extern DWORD EnablerCustomizeRightView;   // can columns be configured for right view?
extern DWORD EnablerDriveInfo;            // can Drive Info be displayed?
extern DWORD EnablerCreateDir;            // panel is disk or FS (with create-dir support)
extern DWORD EnablerViewFile;             // focus is on file && panel is disk, archive or FS (with view-file support)
extern DWORD EnablerChangeAttrs;          // focus|select is on files|directories && panel is disk or FS (with change-attributes support)
extern DWORD EnablerShowProperties;       // focus|select is on files|directories && panel is disk or FS (with show-properties support)
extern DWORD EnablerItemsContextMenu;     // focus|select is on files|directories && panel is disk or FS (with context-menu support)
extern DWORD EnablerOpenActiveFolder;     // panel is disk or FS (with open-active-folder support)
extern DWORD EnablerPermissions;          // focus|select is on files|directories && panel is disk, running on at least W2K, disk supports ACL (NTFS)

//******************************************************************************
//
// ToolBar Bitmap indexes
//
// Items can be added to the array.
// Array is divided into two parts. First are indexes for which there are
// actual images in the bitmap.
// Then follow indexes for icons pulled from shell32.dll.
// These two groups must always be contiguous and indexes cannot be alternated.
//

#define IDX_TB_CONNECTNET 0    // Connect Network Drive
#define IDX_TB_DISCONNECTNET 1 // Disconnect Network Drive
#define IDX_TB_SHARED_DIRS 2   // Shared Directories
#define IDX_TB_CHANGE_DIR 3    // Change Directory
#define IDX_TB_CREATEDIR 4     // Create Directory
#define IDX_TB_NEW 5           // New
#define IDX_TB_FINDFILE 6      // Find Files
#define IDX_TB_PREV_SELECTED 7 // Previous Selected Item
#define IDX_TB_NEXT_SELECTED 8 // Next Selected Item
#define IDX_TB_SORTBYNAME 9    // Sort by Name
#define IDX_TB_SORTBYTYPE 10   // Sort by Type
#define IDX_TB_SORTBYSIZE 11   // Sort by Size
#define IDX_TB_SORTBYDATE 12   // Sort by Date
#define IDX_TB_PARENTDIR 13    // Parent Directory
#define IDX_TB_ROOTDIR 14      // Root Directory
#define IDX_TB_FILTER 15       // Filter
#define IDX_TB_BACK 16         // Back
#define IDX_TB_FORWARD 17      // Forward
#define IDX_TB_REFRESH 18      // Refresh
#define IDX_TB_SWAPPANELS 19   // Swap Panels
#define IDX_TB_CHANGEATTR 20   // Change Attributes
#define IDX_TB_USERMENU 21     // User Menu
#define IDX_TB_COMMANDSHELL 22 // Command Shell
#define IDX_TB_COPY 23         // Copy
#define IDX_TB_MOVE 24         // Move
#define IDX_TB_DELETE 25       // Delete
// 1x not used
#define IDX_TB_COMPRESS 27       // Compress
#define IDX_TB_UNCOMPRESS 28     // UnCompress
#define IDX_TB_QUICKRENAME 29    // Quick Rename
#define IDX_TB_CHANGECASE 30     // Change Case
#define IDX_TB_VIEW 31           // View
#define IDX_TB_CLIPBOARDCUT 32   // Clipboard Cut
#define IDX_TB_CLIPBOARDCOPY 33  // Clipboard Copy
#define IDX_TB_CLIPBOARDPASTE 34 // Clipboard Paste
#define IDX_TB_PERMISSIONS 35    // Permissions
#define IDX_TB_PROPERTIES 36     // Properties
#define IDX_TB_COMPAREDIR 37     // Comapare Directories
#define IDX_TB_DRIVEINFO 38      // Drive Information
#define IDX_TB_RESELECT 39       // Reselect
#define IDX_TB_HELP 40           // Help
#define IDX_TB_CONTEXTHELP 41    // Context Help
// 1x not used
#define IDX_TB_EDIT 43              // Edit
#define IDX_TB_SORTBYEXT 44         // Sort by Extension
#define IDX_TB_SELECT 45            // Select
#define IDX_TB_UNSELECT 46          // Unselect
#define IDX_TB_INVERTSEL 47         // Invert selection
#define IDX_TB_SELECTALL 48         // Select all
#define IDX_TB_PACK 49              // Pack
#define IDX_TB_UNPACK 50            // UnPack
#define IDX_TB_CONVERT 51           // Convert
#define IDX_TB_UNSELECTALL 52       // Unselect all
#define IDX_TB_VIEW_MODE 53         // View Mode
#define IDX_TB_HOTPATHS 54          // Hot Paths
#define IDX_TB_FOCUS 55             // Focus (green arrow)
#define IDX_TB_STOP 56              // Stop (red circle with cross)
#define IDX_TB_EMAIL 57             // Email Files
#define IDX_TB_EDITNEW 58           // Edit New
#define IDX_TB_PASTESHORTCUT 59     // Paste Shortcut
#define IDX_TB_FOCUSSHORTCUT 60     // Focus Shortcut or Link Target
#define IDX_TB_CALCDIRSIZES 61      // Calculate Directory Sizes
#define IDX_TB_OCCUPIEDSPACE 62     // Calculate Occupied Space
#define IDX_TB_SAVESELECTION 63     // Save Selection
#define IDX_TB_LOADSELECTION 64     // Load Selection
#define IDX_TB_SEL_BY_EXT 65        // Select Files With Same Extension
#define IDX_TB_UNSEL_BY_EXT 66      // Unselect Files With Same Extension
#define IDX_TB_SEL_BY_NAME 67       // Select Files With Same Name
#define IDX_TB_UNSEL_BY_NAME 68     // Unselect Files With Same Name
#define IDX_TB_OPEN_FOLDER 69       // Open Folder
#define IDX_TB_CONFIGURARTION 70    // Configuration
#define IDX_TB_OPEN_IN_OTHER_ACT 71 // Focus Name in Other Panel
#define IDX_TB_OPEN_IN_OTHER 72     // Open Name in Other Panel
#define IDX_TB_AS_OTHER_PANEL 73    // Go To Path From Other Panel
#define IDX_TB_HIDE_UNSELECTED 74   // Hide Unselected Names
#define IDX_TB_HIDE_SELECTED 75     // Hide Selected Names
#define IDX_TB_SHOW_ALL 76          // Show All Names
#define IDX_TB_SMART_COLUMN_MODE 77 // Smart Column Mode

#define IDX_TB_FD 78 // first "dynamic added" index
// following icons will be added to bitmap dynamically
// and some will be loaded from shell32.dll

#define IDX_TB_CHANGEDRIVEL IDX_TB_FD + 0 // Change Drive Left
#define IDX_TB_CHANGEDRIVER IDX_TB_FD + 1 // Change Drive Right
#define IDX_TB_OPENACTIVE IDX_TB_FD + 2   // Open Active Folder
#define IDX_TB_OPENDESKTOP IDX_TB_FD + 3  // Open Desktop
#define IDX_TB_OPENMYCOMP IDX_TB_FD + 4   // Open Computer
#define IDX_TB_OPENCONTROL IDX_TB_FD + 5  // Open Control Panel
#define IDX_TB_OPENPRINTERS IDX_TB_FD + 6 // Open Printers
#define IDX_TB_OPENNETWORK IDX_TB_FD + 7  // Open Network
#define IDX_TB_OPENRECYCLE IDX_TB_FD + 8  // Open Recycle Bin
#define IDX_TB_OPENFONTS IDX_TB_FD + 9    // Open Fonts
#define IDX_TB_OPENMYDOC IDX_TB_FD + 10   // Open Documents

#define IDX_TB_COUNT IDX_TB_FD + 11 // number of bitmaps including those pulled from shell32.dll

//******************************************************************************
//
// Custom Exceptions
//

#define OPENSAL_EXCEPTION_RTC 0xE0EA4321   // we raise in rtc callback
#define OPENSAL_EXCEPTION_BREAK 0xE0EA4322 // we raise in case of break (from another Salamander or via salbreak)

//******************************************************************************

// mapping salCmd (Salamander command number launched from plug-in, see SALCMD_XXX)
// to command number for WM_COMMAND
int GetWMCommandFromSalCmd(int salCmd);

//******************************************************************************

// number of items in SalamanderConfigurationRoots array
#define SALCFG_ROOTS_COUNT 84

// main thread id (valid only after entering WinMain())
extern DWORD MainThreadID;

extern BOOL IsNotAlphaNorNum[256]; // TRUE/FALSE array for characters (TRUE = not letter nor digit)
extern BOOL IsAlpha[256];          // TRUE/FALSE array for characters (TRUE = letter)

extern int UserCharset; // default user charset for fonts

// allocation granularity (needed for using memory-mapped files)
extern DWORD AllocationGranularity;

// should we wait for ESC release before starting path browsing in panel?
extern BOOL WaitForESCReleaseBeforeTestingESC;

// returns position in screen coord. where context menu should be shown
// used when showing context menu via keyboard (Shift+F10 or VK_APP)
void GetListViewContextMenuPos(HWND hListView, POINT* p);

// based on display color depth determines whether to use 256 color
// or 16 color bitmaps.
BOOL Use256ColorsBitmap();

// restores focus in source panel (used when focus disappears - after disabling main window, etc.)
void RestoreFocusInSourcePanel();

#define ISSLGINCOMPLETE_SIZE 200
extern wchar_t IsSLGIncomplete[ISSLGINCOMPLETE_SIZE]; // matched forward to its
                                                      // wide definition (sally_entry_lifecycle.cpp)

//******************************************************************************
// file name enumeration from panel/Find for viewers

// init+release of data associated with enumeration
void InitFileNamesEnumForViewers();
void ReleaseFileNamesEnumForViewers();

enum CFileNamesEnumRequestType
{
    fnertFindNext,     // looking for next file in source
    fnertFindPrevious, // looking for previous file in source
    fnertIsSelected,   // checking file selection in source
    fnertSetSelection, // setting file selection in source
};

struct CFileNamesEnumData
{
    // request:
    int RequestUID;                        // request number
    CFileNamesEnumRequestType RequestType; // request type
    int SrcUID;
    int LastFileIndex;
    std::wstring LastFileName;
    BOOL PreferSelected;
    BOOL OnlyAssociatedExtensions;
    CPluginInterfaceAbstract* Plugin; // used when 'OnlyAssociatedExtensions'==TRUE, specifies for which plugin to filter file names ('Plugin'==NULL = internal viewer)
    BOOL Select;
    BOOL TimedOut; // TRUE if no one is waiting for result anymore (unnecessary to perform name search)

    // result:
    std::wstring FileNameW;
    BOOL Found; // TRUE if requested file name was found
    BOOL NoMoreFiles;
    BOOL SrcBusy;
    BOOL IsFileSelected;
};

// section for working with data associated with enumeration (FileNamesEnumSources, FileNamesEnumData,
// FileNamesEnumDone, NextRequestUID and NextSourceUID)
extern CRITICAL_SECTION FileNamesEnumDataSect;
// structure with enumeration request+results
extern CFileNamesEnumData FileNamesEnumData;
// event is "signaled" once source fills result into FileNamesEnumData
extern HANDLE FileNamesEnumDone;

#define FILENAMESENUM_TIMEOUT 1000 // timeout for delivering WM_USER_ENUMFILENAMES message to source window

// returns TRUE if enumeration source is panel, in 'panel' then returns PANEL_LEFT or
// PANEL_RIGHT; if enumeration source was not found or it's a Find window, returns FALSE
BOOL IsFileEnumSourcePanel(int srcUID, int* panel);

// returns next file name for viewer from source (left/right panel or Finds);
// 'srcUID' is unique source identifier (passed as parameter when opening
// viewer); 'lastFileIndex' (must not be NULL) is IN/OUT parameter that plugin should
// modify only if it wants to return first file name, in this case set 'lastFileIndex'
// to -1; initial value of 'lastFileIndex' is passed as parameter when opening
// viewer; 'lastFileName' is full name of current file (empty string if not
// known, e.g. if 'lastFileIndex' is -1); if 'preferSelected' is TRUE and at least one
// name is selected, selected names will be returned; if 'onlyAssociatedExtensions'
// is TRUE, returns only files with extension associated with this plugin's viewer (F3 on this
// file would attempt to open this plugin's viewer + ignores possible shadowing
// by another plugin's viewer); 'fileName' receives the dynamically owned UTF-16
// result; returns TRUE if name is obtained successfully; returns FALSE on error: no
// more file names in source (if 'noMoreFiles' is not NULL, TRUE is returned in it),
// source is busy (not processing messages; if 'srcBusy' is not NULL, TRUE is returned in it),
// otherwise source ceased to exist (path change in panel, sort change, etc.)
BOOL GetNextFileNameForViewer(int srcUID, int* lastFileIndex, const wchar_t* lastFileName,
                               BOOL preferSelected, BOOL onlyAssociatedExtensions,
                               std::wstring* fileName, BOOL* noMoreFiles, BOOL* srcBusy,
                               CPluginInterfaceAbstract* plugin);

// returns previous file name for viewer from source (left/right panel or Finds);
// 'srcUID' is unique source identifier (passed as parameter when opening
// viewer); 'lastFileIndex' (must not be NULL) is IN/OUT parameter that plugin should
// modify only if it wants to return last file name, in this case set 'lastFileIndex'
// to -1; initial value of 'lastFileIndex' is passed as parameter when opening
// viewer; 'lastFileName' is full name of current file (empty string if not
// known, e.g. if 'lastFileIndex' is -1); if 'preferSelected' is TRUE and at least one
// name is selected, selected names will be returned; if 'onlyAssociatedExtensions' is TRUE,
// returns only files with extension associated with this plugin's viewer (F3 on this
// file would attempt to open this plugin's viewer + ignores possible shadowing
// by another plugin's viewer); 'fileName' receives the dynamically owned UTF-16
// result; returns TRUE if name is obtained successfully; returns FALSE on error: no
// previous file name in source (if 'noMoreFiles' is not NULL, TRUE is returned in it),
// source is busy (not processing messages; if 'srcBusy' is not NULL, TRUE is returned
// in it), otherwise source ceased to exist (path change in panel, sort change, etc.)
BOOL GetPreviousFileNameForViewer(int srcUID, int* lastFileIndex, const wchar_t* lastFileName,
                                   BOOL preferSelected, BOOL onlyAssociatedExtensions,
                                   std::wstring* fileName, BOOL* noMoreFiles, BOOL* srcBusy,
                                   CPluginInterfaceAbstract* plugin);

// checks if current file from viewer is selected in source (left/right
// panel or Finds); 'srcUID' is unique source identifier (passed as parameter
// when opening viewer); 'lastFileIndex' is parameter that plugin should not modify,
// initial value of 'lastFileIndex' is passed as parameter when opening viewer;
// 'lastFileName' is full name of current file; returns TRUE if successfully determined
// whether current file is selected, result is in 'isFileSelected' (must not be NULL);
// returns FALSE on error: source ceased to exist (path change in panel, etc.) or file
// 'lastFileName' is no longer in source (for these two errors, if 'srcBusy' is not NULL,
// FALSE is returned in it), source is busy (not processing messages; for this error,
// if 'srcBusy' is not NULL, TRUE is returned in it)
BOOL IsFileNameForViewerSelected(int srcUID, int lastFileIndex, const wchar_t* lastFileName,
                                 BOOL* isFileSelected, BOOL* srcBusy);

// sets selection on current file from viewer in source (left/right
// panel or Finds); 'srcUID' is unique source identifier (passed as parameter
// when opening viewer); 'lastFileIndex' is parameter that plugin should not modify,
// initial value of 'lastFileIndex' is passed as parameter when opening viewer;
// 'lastFileName' is full name of current file; 'select' is TRUE/FALSE if current
// file should be selected/deselected; returns TRUE on success; returns FALSE on error:
// source ceased to exist (path change in panel, etc.) or file 'lastFileName' is no
// longer in source (for these two errors, if 'srcBusy' is not NULL, FALSE is returned in it),
// source is busy (not processing messages; for this error, if 'srcBusy' is not NULL,
// TRUE is returned in it)
BOOL SetSelectionOnFileNameForViewer(int srcUID, int lastFileIndex, const wchar_t* lastFileName,
                                     BOOL select, BOOL* srcBusy);

// changes source (panel or Find) UID (generates new one, updates
// FileNamesEnumSources array and returns new UID in 'srcUID')
void EnumFileNamesChangeSourceUID(HWND hWnd, int* srcUID);

// adds UID to source (panel or Find) (generates new one, adds hWnd+UID
// pair to FileNamesEnumSources array and returns new UID in 'srcUID')
void EnumFileNamesAddSourceUID(HWND hWnd, int* srcUID);

// removes source (panel or Find) from FileNamesEnumSources array
void EnumFileNamesRemoveSourceUID(HWND hWnd);

//******************************************************************************
// non-blocking reading of CD drive volume-name

extern CRITICAL_SECTION ReadCDVolNameCS;   // critical section for data access
extern UINT_PTR ReadCDVolNameReqUID;       // request UID (to identify if someone is still waiting for result)
extern std::wstring ReadCDVolNameBuffer; // IN/OUT value (root/volume name), protected by ReadCDVolNameCS

//******************************************************************************
// functions for working with histories of recently used values in comboboxes

// adds allocated copy of new value 'value' to shared history ('historyArr'+'historyItemsCount');
// if 'caseSensitiveValue' is TRUE, value (string) is searched in history array using
// case-sensitive comparison (FALSE = case-insensitive comparison),
// found value is only moved to first position in history array
void AddValueToStdHistoryValues(wchar_t** historyArr, int historyItemsCount,
                                const wchar_t* value, BOOL caseSensitiveValue);

// adds texts from shared history ('historyArr'+'historyItemsCount') to combobox ('combo');
// performs reset of combobox content before adding (see CB_RESETCONTENT)
void LoadComboFromStdHistoryValues(HWND combo, wchar_t** historyArr, int historyItemsCount);

// Re-selects the combo's font with DEFAULT_CHARSET when 'text' needs
// glyphs the current lfCharSet cannot supply. Returns an HFONT the CALLER owns and
// must DeleteObject, or NULL if no change was needed.
HFONT EnsureComboFontCanRenderW(HWND combo, const wchar_t* text);

//******************************************************************************

// function for adding all previously unknown active (loaded) process modules
void AddNewlyLoadedModulesToGlobalModulesStore();

//******************************************************************************

// quicksort with comparison via StrICmpW
void SortNames(wchar_t* files[], int left, int right);

// searches for string 'name' in 'usedNames' array (array is sorted using StrICmpW);
// returns TRUE if found + found index in 'index' (if not NULL); returns
// FALSE if element was not found + index for insertion in 'index' (if not NULL)
BOOL ContainsString(TIndirectArray<wchar_t>* usedNames, const wchar_t* name, int* index = NULL);

//******************************************************************************

// on success returns TRUE and path to "Documents", or "Desktop"
// on failure returns FALSE
BOOL GetMyDocumentsOrDesktopPathW(std::wstring& path);

// To optimize performance, it is good practice for applications to detect whether they
// are running in a Terminal Services client session. For example, when an application
// is running on a remote session, it should eliminate unnecessary graphic effects, as
// described in Graphic Effects. If the user is running the application in a console
// session (directly on the terminal), it is not necessary for the application to
// optimize its behavior.
//
// Returns TRUE if the application is running in a remote session and FALSE if the
// application is running on the console.
BOOL IsRemoteSession(void);

// returns TRUE if user is among Administrators
// returns FALSE on error
BOOL IsUserAdmin();

//******************************************************************************

// for ensuring escape from removed drives to fixed drive (after device ejection - USB flash drive, etc.)
extern BOOL ChangeLeftPanelToFixedWhenIdleInProgress; // TRUE = path is currently changing, setting ChangeLeftPanelToFixedWhenIdle to TRUE is useless
extern BOOL ChangeLeftPanelToFixedWhenIdle;
extern BOOL ChangeRightPanelToFixedWhenIdleInProgress; // TRUE = path is currently changing, setting ChangeRightPanelToFixedWhenIdle to TRUE is useless
extern BOOL ChangeRightPanelToFixedWhenIdle;
extern BOOL OpenCfgToChangeIfPathIsInaccessibleGoTo; // TRUE = in idle opens configuration on Drives and focuses "If path in panel is inaccessible, go to:"

// drive root (including UNC), for which "drive not ready" messagebox with Retry+Cancel
// buttons is displayed (used for automatic Retry after inserting media into drive)
extern std::wstring CheckPathRootWithRetryMsgBox;
// "drive not ready" dialog with Retry+Cancel buttons (used for automatic Retry after
// inserting media into drive)
extern HWND LastDriveSelectErrDlgHWnd;

// GetDriveFormFactor returns the drive form factor.
//  It returns 350 if the drive is a 3.5" floppy drive.
//  It returns 525 if the drive is a 5.25" floppy drive.
//  It returns 800 if the drive is a 8" floppy drive.
//  It returns   1 if the drive supports removable media other than 3.5", 5.25", and 8" floppies.
//  It returns   0 on error.
//  iDrive is 1 for drive A:, 2 for drive B:, etc.
DWORD GetDriveFormFactor(int iDrive);

//******************************************************************************

// sorts plugin array by PluginFSCreateTime (for display in Alt+F1/F2 and Disconnect dialog)
void SortPluginFSTimes(CPluginFSInterfaceEncapsulation** list, int left, int right);

// returns ordinal number for item text in change drive menu and Disconnect dialog;
// see CPluginFSInterfaceEncapsulation::ChngDrvDuplicateItemIndex
int GetIndexForDrvText(CPluginFSInterfaceEncapsulation** fsList, int count,
                       CPluginFSInterfaceAbstract* fsIface, int currentIndex);

//******************************************************************************

// users can change shortcut icon via TweakUI (default, custom, none)
// this function retrieves HShortcutOverlayXX
// discards existing one
BOOL GetShortcutOverlay();

// returns text form of 'hotKey' (LOBYTE=vk, HIBYTE=mods), 'buff' must have at least 50 characters
std::wstring GetHotKeyText(WORD hotKey);

// returns bits per pixel of display
int GetCurrentBPP(HDC hDC = NULL);

// iterates through parents towards the topmost one
HWND GetTopLevelParent(HWND hWindow);

//******************************************************************************

// variables used during configuration saving at shutdown, log-off or restart (we must
// pump messages so system doesn't kill us as "not responding" software)
class CWaitWindow;
extern CWaitWindow* GlobalSaveWaitWindow; // if global wait window for Save exists, it's here (otherwise NULL)
extern int GlobalSaveWaitWindowProgress;  // current progress value of global wait window for Save

extern BOOL IsSetSALAMANDER_SAVE_IN_PROGRESS; // TRUE = SALAMANDER_SAVE_IN_PROGRESS value is created in registry (detection of configuration saving interruption)

//******************************************************************************

// support structure and function for opening context menu + executing its items
// in CSalamanderGeneral::OpenNetworkContextMenu()

struct CTmpEnumData
{
    int* Indexes;
    CFilesWindow* Panel;
};

const wchar_t* EnumFileNames(int index, void* param);

// Both return the HRESULT from the wrapped shell call, or E_UNEXPECTED if it raised an
// exception. Callers may ignore it, but it is the only evidence we get when a shell
// extension refuses a command (see shellsup_diag.h and issues #13/#15/#20).
HRESULT ShellActionAux5(UINT flags, CFilesWindow* panel, HMENU h);
HRESULT AuxInvokeCommand(CFilesWindow* panel, CMINVOKECOMMANDINFO* ici);
void ShellActionAux6(CFilesWindow* panel);

//******************************************************************************

// Returns Configuration.IfPathIsInaccessibleGoTo, honoring the My Documents setting.
void GetIfPathIsInaccessibleGoToW(std::wstring& path, BOOL forceIsMyDocs = FALSE);

// loads icon overlay handler configuration from registry
void LoadIconOvrlsInfo(const wchar_t* root);

// returns TRUE if icon overlay handler is disabled (or all are disabled)
BOOL IsDisabledCustomIconOverlays(const wchar_t* name);

// returns TRUE if icon overlay handler is in the list of disabled icon overlay handlers
BOOL IsNameInListOfDisabledCustomIconOverlays(const wchar_t* name);

// clears the list of disabled icon overlay handlers
void ClearListOfDisabledCustomIconOverlays();

// adds 'name' to the list of disabled icon overlay handlers
BOOL AddToListOfDisabledCustomIconOverlays(const wchar_t* name);

// loads icon from ImageResDLL
HICON SalLoadImage(int vistaResID, int otherResID, int cx, int cy, UINT flags);

// loads an icon for archives
HICON LoadArchiveIcon(int cx, int cy, UINT flags);

// obtains credentials for the given network path and optionally restores its mapping
BOOL RestoreNetworkConnectionW(HWND parent, const wchar_t* name, const wchar_t* remoteName, DWORD* retErr = NULL,
                               LPNETRESOURCEW lpNetResource = NULL);

// builds text for the Type column for an unassociated file (e.g., "AAA File" or just "File")
void GetCommonFileTypeStr(wchar_t* buf, int* resLen, const wchar_t* ext);

// finds duplicate separators and removes the redundant ones (on Vista duplicate
// separators appeared in the context menu for .bar files)
void RemoveUselessSeparatorsFromMenu(HMENU h);

// Creates and returns Sally's application-data directory as dynamically-owned UTF-16.
BOOL CreateOurPathInRoamingAPPDATAW(std::wstring& path);

#ifndef _WIN64

// 32-bit build on Win64 only: checks whether the path is redirected by the file system redirector
// to SysWOW64 or back to System32
BOOL IsWin64RedirectedDir(const wchar_t* path, std::wstring* completedPath, BOOL failIfDirWithSameNameExists);

// 32-bit build on Win64 only: checks whether the selection contains a pseudo-directory that the
// redirector maps to SysWOW64 or back to System32, and that there is no real directory with the
// same name on disk (pseudo-directory added only via AddWin64RedirectedDir)
BOOL ContainsWin64RedirectedDir(CFilesWindow* panel, int* indexes, int count, std::wstring& redirectedDir,
                                BOOL onlyAdded);

#endif // _WIN64

// our variants of RegQueryValue and RegQueryValueEx; unlike the WinAPI versions they ensure a
// null terminator is present for REG_SZ, REG_MULTI_SZ, and REG_EXPAND_SZ
// NOTE: when determining the required buffer size they return one or two extra characters (two
//       only for REG_MULTI_SZ) to allow for terminating null(s)
extern "C"
{
    LONG SalRegQueryValue(HKEY hKey, LPCSTR lpSubKey, LPSTR lpData, PLONG lpcbData);
    // Wide sibling. 'lpcbData' stays a BYTE count on both sides.
    LONG SalRegQueryValueW(HKEY hKey, LPCWSTR lpSubKey, LPWSTR lpData, PLONG lpcbData);
}

// Wide core overload. The existing implementation and all data
// counts stay byte-domain; only the registry value name is UTF-16. Keeping it
// as a wide-only core function keeps byte counts explicit without a fixed ANSI mirror.
LONG SalRegQueryValueEx(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved,
                        LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);

// Win7 and newer OS: taskbar notification that a button has been created for a window
// set during Salamander startup; check if it is nonzero
extern UINT TaskbarBtnCreatedMsg;

// returns icon size with respect to SystemDPI
// if 'large' is TRUE, returns the size for a large icon, otherwise for a small one
int GetIconSizeForSystemDPI(CIconSizeEnum iconSize);

// returns current system DPI (96, 120, 144, ...)
int GetSystemDPI();

// returns scale corresponding to current DPI; instead of 1.0 returns 100, for 1.25 returns 125, etc.
int GetScaleForSystemDPI();
