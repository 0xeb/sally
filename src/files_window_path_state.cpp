// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "common/fsutil.h" // GetRootPathW
#include "common/IChangeNotifier.h"
#include "common/DiagnosticTextEncoding.h"

#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/IEnvironment.h"

#include "cfgdlg.h"
#include "dialogs.h"
#include "mainwnd.h"
#include "usermenu.h"
#include "plugins.h"
#include "fileswnd.h"
#include "stswnd.h"
#include "snooper.h"
#include "zip.h"
#include "shellib.h"
#include "pack.h"
#include "thumbnl.h"
#include "geticon.h"
#include "shiconov.h"
#include "common/widepath.h"

namespace
{
bool GetVolumeRootForPathW(const std::wstring& path, std::wstring& root)
{
    if (path.empty())
        return false;

    // A volume root cannot exceed the supplied path except for the trailing separator.
    // Size from that semantic input instead of adding an arbitrary MAX_PATH reserve.
    std::vector<wchar_t> buffer(path.length() + 2);
    if (GetVolumePathNameW(path.c_str(), buffer.data(), (DWORD)buffer.size()))
    {
        root = buffer.data();
        return !root.empty();
    }

    return false;
}

BOOL GetVolumeInformationForPathW(const std::wstring& path, LPDWORD flags)
{
    std::wstring root;
    if (!GetVolumeRootForPathW(path, root))
        return FALSE;

    DWORD dummyMaximumComponentLength;
    return GetVolumeInformationW(root.c_str(), NULL, 0, NULL,
                                 &dummyMaximumComponentLength, flags, NULL, 0);
}

UINT GetDriveTypeForPathW(const std::wstring& path)
{
    std::wstring root;
    if (GetVolumeRootForPathW(path, root))
        return GetDriveTypeW(root.c_str());

    return GetDriveTypeW(path.c_str());
}
} // namespace

//
// ****************************************************************************
// CFilesWindowAncestor
//

CFilesWindowAncestor::CFilesWindowAncestor(BOOL headlessPanel)
{
    CALL_STACK_MESSAGE_NONE
    Files = new CFilesArray;
    Dirs = new CFilesArray;
    SelectedCount = 0;

    PathW.clear();
    SuppressAutoRefresh = FALSE;
    HeadlessPanel = headlessPanel;
    PanelType = ptDisk;
    MonitorChanges = TRUE;
    DriveType = DRIVE_UNKNOWN;

    ArchiveDir = NULL;
    ZIPArchiveW.clear();
    ZIPPathW.clear();

    PluginFS.Init(NULL, NULL, NULL, NULL, NULL, NULL, -1, 0, 0, 0);
    PluginFSDir = NULL;
    PluginIconsType = pitSimple;
    SimplePluginIcons = NULL;

    // Seed wide-NATIVE: the wide system directory straight from
    // IEnvironment, its root taken by the wide helper, and the ANSI mirror
    // derived from that. The previous version ran ANSI-first
    // (EnvGetSystemDirectoryA -> GetRootPath(char*) -> AnsiToWide) and was safe
    // only by the accident that a drive root is always ASCII - the one place the
    // class's own invariant ran backwards.
    std::wstring systemDirW;
    if (!gEnvironment->GetSystemDirectory(systemDirW).success || systemDirW.empty())
        systemDirW = L"C:\\"; // last resort; the setter below still owns the invariant
    PathW = GetRootPath(systemDirW.c_str());
    PathIsLossy = FALSE; // a drive root is always representable

    OnlyDetachFSListing = FALSE;
    NewFSFiles = NULL;
    NewFSDirs = NULL;
    NewFSPluginFSDir = NULL;
    NewFSIconCache = NULL;

    PluginIface = NULL;
    PluginIfaceLastIndex = -1;
}

CFilesWindowAncestor::~CFilesWindowAncestor()
{
    CALL_STACK_MESSAGE1("CFilesWindowAncestor::~CFilesWindowAncestor()");
    if (Files != NULL)
        delete Files;
    if (Dirs != NULL)
        delete Dirs;
    if (PluginFS.NotEmpty() || PluginData.NotEmpty() ||
        ArchiveDir != NULL || PluginFSDir != NULL)
    {
        TRACE_E("Unexpected situation in CFilesWindowAncestor::~CFilesWindowAncestor()");
    }
}

DWORD
CFilesWindowAncestor::CheckPath(BOOL echo, const wchar_t* path, DWORD err, BOOL postRefresh, HWND parent)
{
    parent = (parent == NULL) ? HWindow : parent;
    if (path == NULL || *path == L'\0')
        path = GetPathW();

    return SalCheckPathW(echo, path, err, postRefresh, parent);
}

void CFilesWindowAncestor::ReleaseListing()
{
    CALL_STACK_MESSAGE_NONE

        ((CFilesWindow*)this)
            ->VisibleItemsArray.InvalidateArr();
    ((CFilesWindow*)this)->VisibleItemsArraySurround.InvalidateArr();
    if (OnlyDetachFSListing)
    {
        // disconnect the listing from the panel including icons
        Files = NewFSFiles;
        Dirs = NewFSDirs;
        SetPluginFSDir(NewFSPluginFSDir);
        PluginData.Init(NULL, NULL, NULL, NULL, 0);
        if (NewFSIconCache != NULL)
            ((CFilesWindow*)this)->IconCache = NewFSIconCache;
        ((CFilesWindow*)this)->SetValidFileData(GetPluginFSDir()->GetValidData());

        OnlyDetachFSListing = FALSE;
        NewFSFiles = NULL;
        NewFSDirs = NULL;
        NewFSPluginFSDir = NULL;
        NewFSIconCache = NULL;
    }
    else
    {
        ReleaseListingBody(PanelType, ArchiveDir, PluginFSDir, PluginData, Files, Dirs, FALSE);
    }
    SelectedCount = 0;
}

BOOL CFilesWindowAncestor::IsPathFromActiveFS(const wchar_t* fsName, std::wstring& fsUserPart, int& fsNameIndex,
                                              BOOL& convertPathToInternal)
{
    CALL_STACK_MESSAGE_NONE
    fsNameIndex = -1;
    if (Is(ptPluginFS) && PluginFS.NotEmpty())
    {
        if (Plugins.AreFSNamesFromSamePlugin(PluginFS.GetPluginFSName(), fsName, fsNameIndex)) // we compare whether the file systems are from the same plug-in
        {
            if (convertPathToInternal)
            {
                PluginFS.GetPluginInterfaceForFS()->ConvertPathToInternalW(fsName, fsNameIndex, fsUserPart);
                convertPathToInternal = FALSE;
            }
            return PluginFS.IsOurPath(PluginFS.GetPluginFSNameIndex(), fsNameIndex, fsUserPart.c_str());
        }
    }
    return FALSE;
}

BOOL CFilesWindowAncestor::GetGeneralPath(std::wstring& buf, BOOL convertFSPathToExternal)
{
    CALL_STACK_MESSAGE_NONE
    buf.clear();
    if (Is(ptDisk))
    {
        buf = GetPathW();
        return TRUE;
    }
    if (Is(ptZIPArchive))
    {
        buf = GetZIPArchive();
        if (GetZIPPath()[0] != 0)
        {
            if (GetZIPPath()[0] != L'\\')
                buf += L'\\';
            buf += GetZIPPath();
        }
        return TRUE;
    }
    if (Is(ptPluginFS) && PluginFS.NotEmpty())
    {
        std::wstring userPartW;
        if (PluginFS.GetCurrentPathW(userPartW))
        {
            if (convertFSPathToExternal &&
                !PluginFS.GetPluginInterfaceForFS()->ConvertPathToExternalW(
                    PluginFS.GetPluginFSName(), PluginFS.GetPluginFSNameIndex(), userPartW))
                return FALSE;
            buf = PluginFS.GetPluginFSName();
            buf += L":";
            buf += userPartW;
            return TRUE;
        }
    }
    return FALSE;
}

void CFilesWindowAncestor::SetPath(const wchar_t* path)
{
    std::wstring newPath = path != NULL ? path : L"";

    // Record here, once, whether CP_ACP can name this path exactly, so a caller that has
    // to hand an ANSI string to a char*-only consumer can tell in advance that it is about
    // to be wrong. TryWideToAnsiRoundTripExact asks the strict question
    // (WC_NO_BEST_FIT_CHARS, usedDefaultChar checked, full round-trip compared) - unlike
    // plain WideToAnsi, which best-fits and narrows silently.
    std::string exactCheck;
    PathIsLossy = !Win32EncodeAcpExact(newPath, exactCheck);

    // Diagnostic only: the call-stack log is a narrow subsystem by design, so a lossy
    // rendering is acceptable HERE and nowhere else in this function.
    CALL_STACK_MESSAGE2("CFilesWindowAncestor::SetPath(%s)", sally::diagnostic::EncodeAcpLossy(newPath).c_str());
    // Compare the WIDE truth: the mirrors can be lossy, and two
    // different Unicode directories share one lossy mirror, so an ANSI compare
    // could call them "the same path" and wrongly keep auto-refresh suppressed.
    if (SuppressAutoRefresh && (!Is(ptDisk) || !IsTheSamePath(newPath.c_str(), PathW.c_str())))
        SuppressAutoRefresh = FALSE;
    DetachDirectory((CFilesWindow*)this);
    PathW = newPath;

    //--- detection of file-based compression/encryption and FAT32
    DWORD flags;
    if ((Is(ptDisk) || Is(ptZIPArchive)) && !newPath.empty() &&
        GetVolumeInformationForPathW(newPath, &flags))
    {
        ((CFilesWindow*)this)->FileBasedCompression = (flags & FS_FILE_COMPRESSION) != 0 && Is(ptDisk);
        ((CFilesWindow*)this)->FileBasedEncryption = (flags & FILE_SUPPORTS_ENCRYPTION) != 0 && Is(ptDisk);
        ((CFilesWindow*)this)->SupportACLS = (flags & FS_PERSISTENT_ACLS) != 0 && Is(ptDisk);
    }
    else
    {
        ((CFilesWindow*)this)->FileBasedCompression = FALSE;
        ((CFilesWindow*)this)->FileBasedEncryption = FALSE;
        ((CFilesWindow*)this)->SupportACLS = FALSE;
    }

    MonitorChanges = FALSE;
    DriveType = DRIVE_UNKNOWN;
    if (!Is(ptPluginFS)) // pluginFS handles changes differently...
    {
        DriveType = GetDriveTypeForPathW(newPath);
        switch (DriveType)
        {
        case DRIVE_REMOVABLE:
        {
            BOOL isDriveFloppy = FALSE; // floppies have their own configuration beside other removable drives
            // derive from the wide truth (drive letters are ASCII,
            // but Path is a possibly-lossy mirror and must not drive decisions).
            const wchar_t pathLead = !PathW.empty() ? PathW[0] : L'\0';
            int drv = (int)towupper(pathLead) - 'A' + 1;
            if (drv >= 1 && drv <= 26) // we perform a range check just to be sure
            {
                DWORD medium = GetDriveFormFactor(drv);
                if (medium == 350 || medium == 525 || medium == 800 || medium == 1)
                    isDriveFloppy = TRUE;
            }
            MonitorChanges = isDriveFloppy ? Configuration.DrvSpecFloppyMon : Configuration.DrvSpecRemovableMon;
            break;
        }

        case DRIVE_REMOTE:
        {
            MonitorChanges = Configuration.DrvSpecRemoteMon;
            break;
        }

        case DRIVE_CDROM:
        {
            MonitorChanges = Configuration.DrvSpecCDROMMon;
            break;
        }

        default: // case DRIVE_FIXED:   // not only fixed drives but also the others (RAM DISK, etc.)
        {
            MonitorChanges = Configuration.DrvSpecFixedMon;
            break;
        }
        }

        // we handle suppression of auto refresh
        if (SuppressAutoRefresh || HeadlessPanel)
            MonitorChanges = FALSE;

        if (MonitorChanges)
        {
            // Through IChangeNotifier, unconditionally wide. The
            // ANSI fallback is gone with the ANSI entry points: PathW is the
            // panel's truth, and watching the CP_ACP mirror meant a directory
            // the code page cannot spell was watched at the WRONG path (or not
            // at all) and the panel silently stopped auto-refreshing.
            gChangeNotifier->AddWatch(this, PathW.c_str(),
                                      DriveType == DRIVE_REMOVABLE || DriveType == DRIVE_FIXED);
        }
        else // if changes are not monitored, Snooper does not call SetAutomaticRefresh -> we do it here
        {
            ((CFilesWindow*)this)->SetAutomaticRefresh(FALSE, TRUE);
        }
    }
    else // ptPluginFS - do not perform any refreshes; the plug-in manages them itself
    {
        ((CFilesWindow*)this)->SetAutomaticRefresh(TRUE, TRUE);
    }
}

CFilesArray*
CFilesWindowAncestor::GetArchiveDirFiles(const wchar_t* zipPath)
{
    CALL_STACK_MESSAGE_NONE
    if (zipPath == NULL)
        zipPath = ZIPPathW.c_str();
    return ArchiveDir->GetFiles(zipPath);
}

CFilesArray*
CFilesWindowAncestor::GetArchiveDirDirs(const wchar_t* zipPath)
{
    CALL_STACK_MESSAGE_NONE
    if (zipPath == NULL)
        zipPath = ZIPPathW.c_str();
    return ArchiveDir->GetDirs(zipPath);
}

CFilesArray*
CFilesWindowAncestor::GetFSFiles()
{
    CALL_STACK_MESSAGE_NONE
    return PluginFSDir->GetFiles(L"");
}

CFilesArray*
CFilesWindowAncestor::GetFSDirs()
{
    CALL_STACK_MESSAGE_NONE
    return PluginFSDir->GetDirs(L"");
}

CPluginData*
CFilesWindowAncestor::GetPluginDataForPluginIface()
{
    return Plugins.GetPluginData(PluginIface, &PluginIfaceLastIndex);
}

void CFilesWindowAncestor::SetZIPPath(const wchar_t* path)
{
    CALL_STACK_MESSAGE_NONE
    if (path == NULL)
        path = L"";
    if (*path == L'\\')
        path++; // ZIPPath will not start with '\\'
    ZIPPathW = path;
    if (!ZIPPathW.empty() && ZIPPathW.back() == L'\\')
        ZIPPathW.pop_back(); // ZIPPath will not end with '\\'

}

void CFilesWindowAncestor::SetZIPArchive(const wchar_t* archive)
{
    CALL_STACK_MESSAGE_NONE
    ZIPArchiveW = archive != NULL ? archive : L"";
}

// Wide comparison. This feeds the snooper's change-notification
// dedup: on the ANSI mirrors two DIFFERENT Unicode directories can collapse to
// the same "C:\???\" string, so one panel's refresh could be suppressed as a
// duplicate of the other's.
BOOL CFilesWindowAncestor::SamePath(CFilesWindowAncestor* other)
{
    CALL_STACK_MESSAGE_NONE
    size_t l1 = PathW.size();
    if (l1 > 0 && PathW[l1 - 1] == L'\\')
        l1--;
    size_t l2 = other->PathW.size();
    if (l2 > 0 && other->PathW[l2 - 1] == L'\\')
        l2--;
    return (PanelType == ptDisk || PanelType == ptZIPArchive) &&
           (other->PanelType == ptDisk || other->PanelType == ptZIPArchive) &&
           l1 == l2 && _wcsnicmp(PathW.c_str(), other->PathW.c_str(), l1) == 0;
}

//
// ****************************************************************************
// CFilesWindow
//

void IconThreadThreadFBodyAux(const wchar_t* path, SHFILEINFO& shi, CIconSizeEnum iconSize)
{
    CALL_STACK_MESSAGE_NONE
    __try
    {
        // do not let a default icon be returned; if it fails, simple icons are used
        if (!GetFileIcon(path, &shi.hIcon, iconSize, FALSE, FALSE))
            shi.hIcon = NULL;

        // We switched to our own implementation (lower memory usage, working XOR icons)
        // Additionally it does not support obtaining EXTRALARGE and JUMBO icons; accessing the system image list is required
        //SHGetFileInfo(path, 0, &shi, sizeof(shi),
        //              SHGFI_ICON | SHGFI_SMALLICON | SHGFI_SHELLICONSIZE);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 4))
    {
        FGIExceptionHasOccured++;
        shi.hIcon = NULL;
    }
}

unsigned IconThreadThreadFBody(void* parameter)
{
    CALL_STACK_MESSAGE1("IconThreadThreadFBody()");

    SetThreadNameInVCAndTrace(L"IconsReader");
    TRACE_I("Begin");
    CFilesWindow* window = (CFilesWindow*)parameter;

    // let shell extensions that retrieve icons via IconHandler and other COM/OLE stuff work correctly
    if (OleInitialize(NULL) != S_OK)
        TRACE_E("Error in OleInitialize.");

    IShellIconOverlayIdentifier** iconReadersIconOverlayIds = ShellIconOverlays.CreateIconReadersIconOverlayIds();

    HANDLE handles[2];
    handles[0] = window->ICEventTerminate;
    handles[1] = window->ICEventWork;
    DWORD wait = WAIT_TIMEOUT;
    BOOL run = TRUE;
    BOOL firstRound = TRUE; // on error a REFRESH is sent, but only the first time

    CSalamanderThumbnailMaker thumbMaker(window);

    while (run)
    {
        if (wait == WAIT_TIMEOUT) // otherwise wait is already set from work mode
            wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

        switch (wait)
        {
        case WAIT_OBJECT_0 + 1: // work
        {
            CALL_STACK_MESSAGE1("IconThreadThreadFBody::work");
            window->IconCacheValid = FALSE; // required for refreshes when the icon reader sleeps or wakes up; otherwise the main thread sets it

            // j.r. the original 200 ms delay was probably too long, reduced to 20 ms
            // j.r. 20 ms was still short; the thread could start when Enter was held
            // Petr: the main thread repaints with higher priority; with this sleep here
            //       icon overlays (e.g., Tortoise SVN) flickered even more than they do now
            // give the main thread some time to draw and to quickly interrupt when changing directories
            // (now used only as a "pause" during which RefreshDirectory() can push new icons into the cache, see 'WaitBeforeReadingIcons')
            if (window->WaitBeforeReadingIcons > 0)
                Sleep(window->WaitBeforeReadingIcons);
            if (window->WaitOneTimeBeforeReadingIcons > 0)
            {
                DWORD time = window->WaitOneTimeBeforeReadingIcons;
                window->WaitOneTimeBeforeReadingIcons = 0;
                Sleep(time); // wait before starting to read icon overlays; during this wait all notifications about changes from Tortoise SVN should arrive (see IconOverlaysChangedOnPath())
            }

            HANDLES(EnterCriticalSection(&window->ICSleepSection));

            // should we start new work (wake-up -> sleep -> wake-up) or terminate?
            wait = WaitForMultipleObjects(2, handles, FALSE, 0);

            //        BOOL postRefresh = FALSE;
            if (wait == WAIT_TIMEOUT && !window->ICStopWork)
            {
                //          TRACE_I("Start reading.");
                window->ICWorking = TRUE;

                CIconSizeEnum iconSize = window->GetIconSizeForCurrentViewMode();

                CIconList* iconList;
                int iconListIndex;
                SHFILEINFO shi; // for historical reasons (SHGetFileInfo) shi.hIcon is used for all icon types

                // Prepare the directory for file icons and overlays.
                std::wstring wPath;
                BOOL pathIsInvalid = FALSE;
                BOOL isGoogleDrivePath = FALSE;
                if (window->Is(ptDisk))
                {
                    const wchar_t* diskPath = window->GetPathW();
                    wPath = diskPath;
                    if (!wPath.empty() && wPath.back() != L'\\')
                        wPath.push_back(L'\\');
                    pathIsInvalid = !PathContainsValidComponents(wPath.c_str());
                    if (pathIsInvalid)
                        TRACE_IW(L"Path contains invalid components, shell cannot read icons from such paths! Path: " << wPath.c_str());
                    isGoogleDrivePath = ShellIconOverlays.IsGoogleDrivePath(wPath.c_str());
                }

                BOOL readOnlyVisibleItems = window->InactWinOptimizedReading; // refreshes from the snooper in an inactive window: read only visible icons/thumbnails/overlays to save CPU time (we are in the background)
                                                                              //          if (readOnlyVisibleItems) TRACE_I("Refresh in inactive window, reading only visible icons...");
                BOOL readOnlyVisibleItemsDueToUMI = FALSE;                    // description below
                if (!readOnlyVisibleItems && UserMenuIconBkgndReader.IsReadingIcons())
                {
                    //            TRACE_I("Reading of usermenu icons is in progress, reading only visible icons...");
                    readOnlyVisibleItems = TRUE;
                    readOnlyVisibleItemsDueToUMI = TRUE;
                }

                BOOL readThumbnails = window->UseThumbnails; // should we try to load thumbnails?

                if (window->StopThumbnailLoading)
                    readThumbnails = FALSE; // unwanted wake-up - at least suppress thumbnail loading

                BOOL pluginFSIconsFromPlugin = window->Is(ptPluginFS) &&
                                               window->GetPluginIconsType() == pitFromPlugin;
                BOOL pluginFSIconsFromRegistry = window->Is(ptPluginFS) &&
                                                 window->GetPluginIconsType() == pitFromRegistry;

                BOOL waitBeforeFirstReadIcon = FALSE; // TRUE only when jumping to SECOND_ROUND:
                BOOL repeatedRound = FALSE;           // TRUE when icons/thumbnails are reloaded because user-menu icon reading is still in progress

            SECOND_ROUND: // if some icon cannot be read from disk, a second attempt is made at the end

                DWORD wanted = -1;                                 // invalid -> does nothing and then sleeps
                if (window->Is(ptDisk) || pluginFSIconsFromPlugin) // disk + FS/icons-from-plugin
                {
                    wanted = 0; // first load new icons and only then the old ones
                }
                else
                {
                    if (window->Is(ptZIPArchive) || pluginFSIconsFromRegistry) // archive + FS/icons-from-registry
                    {
                        wanted = 3; // our icons are determined by their icon location
                    }
                    else
                        TRACE_E("Unexpected situation.");
                }
                // before starting set "ReadingDone" of all icon-cache items and "IconOverlayDone" of all panel items to FALSE
                int x;
                if (!repeatedRound)
                    for (x = 0; x < window->IconCache->Count; x++)
                        window->IconCache->At(x).SetReadingDone(0);
                if (firstRound && !repeatedRound)
                {
                    for (x = 0; x < window->Files->Count; x++)
                        window->Files->At(x).IconOverlayDone = 0;
                    for (x = 0; x < window->Dirs->Count; x++)
                        window->Dirs->At(x).IconOverlayDone = 0;
                }

                BOOL failed = FALSE;
                BOOL destroyPluginIcon = TRUE;

                int selectMode = 1;
                // 1 = sequential traversal (VisibleItemsArray.IsArrValid() == FALSE),
                // 2 = traversal according to VisibleItemsArray,
                // 3 = traversal according to VisibleItemsArraySurround,
                // 4 = sequential traversal (VisibleItemsArray.IsArrValid() == TRUE)

                BOOL canReadIconOverlays = firstRound && window->Is(ptDisk) && iconReadersIconOverlayIds != NULL;
                BOOL readIconOverlaysNow = FALSE; // TRUE = reading overlays now, FALSE = reading icons + thumbnails

                //          TRACE_I("wanted=" << wanted << ", selectMode=" << selectMode);

                int lastVisArrVersion = -1;
                BOOL someNameSkipped = FALSE;
                int thumbnailFlag = 0;
                int i = 0;
                while (1)
                {
                    BOOL callWaitForObjects = TRUE;                                                                        // optimization only - while searching for an item (takes almost no time) WaitForMultipleObjects is not called
                    if (i < (readIconOverlaysNow ? window->Files->Count + window->Dirs->Count : window->IconCache->Count)) // loading an icon from a file/directory or retrieving icon overlay for a file/directory
                    {
                        CIconData* iconData = readIconOverlaysNow ? NULL : &window->IconCache->At(i);

                        BOOL skipName = FALSE;
                        if (selectMode == 1)
                        {
                            int visArrVer;
                            if (window->VisibleItemsArray.IsArrValid(&visArrVer))
                            {
                                i = 0;
                                lastVisArrVersion = visArrVer;
                                selectMode = 2;
                                //                  TRACE_I("selectMode=" << selectMode);
                                readIconOverlaysNow = FALSE;
                                //                  TRACE_I("readIconOverlaysNow=" << readIconOverlaysNow);
                                continue;
                            }
                        }
                        else
                        {
                            if (selectMode == 2 || selectMode == 3)
                            {
                                int visArrVer;
                                BOOL visArrValid;
                                BOOL cont;
                                if (selectMode == 2)
                                {
                                    if (readIconOverlaysNow)
                                        cont = window->VisibleItemsArray.ArrContainsIndex(i, &visArrValid, &visArrVer);
                                    else
                                        cont = window->VisibleItemsArray.ArrContains(iconData->NameAndData,
                                                                                     &visArrValid, &visArrVer);
                                }
                                else
                                {
                                    if (readIconOverlaysNow)
                                        cont = window->VisibleItemsArraySurround.ArrContainsIndex(i, &visArrValid, &visArrVer);
                                    else
                                        cont = window->VisibleItemsArraySurround.ArrContains(iconData->NameAndData,
                                                                                             &visArrValid, &visArrVer);
                                }
                                if (!cont && visArrValid && visArrVer == lastVisArrVersion)
                                    skipName = TRUE;
                                else
                                {
                                    if (!visArrValid)
                                    {
                                        i = 0;
                                        selectMode = 1;
                                        //                      TRACE_I("selectMode=" << selectMode);
                                        readIconOverlaysNow = FALSE;
                                        //                      TRACE_I("readIconOverlaysNow=" << readIconOverlaysNow);
                                        continue;
                                    }
                                    else
                                    {
                                        if (visArrVer != lastVisArrVersion)
                                        {
                                            i = 0;
                                            lastVisArrVersion = visArrVer;
                                            selectMode = 2;
                                            //                        TRACE_I("selectMode=" << selectMode);
                                            readIconOverlaysNow = FALSE;
                                            //                        TRACE_I("readIconOverlaysNow=" << readIconOverlaysNow);
                                            continue;
                                        }
                                    }
                                }
                            }
                            else // selectMode == 4
                            {
                                int visArrVer;
                                if (window->VisibleItemsArray.IsArrValid(&visArrVer) && visArrVer != lastVisArrVersion)
                                {
                                    i = 0;
                                    lastVisArrVersion = visArrVer;
                                    selectMode = 2;
                                    //                    TRACE_I("selectMode=" << selectMode);
                                    readIconOverlaysNow = FALSE;
                                    //                    TRACE_I("readIconOverlaysNow=" << readIconOverlaysNow);
                                    continue;
                                }
                            }
                        }

                        if (!skipName)
                        {
                            if (readIconOverlaysNow) // new icons/thumbnails for the selected area (see 'selectMode') are loaded, now we read icon overlays
                            {
                                CFileData* fileData = i < window->Dirs->Count ? &window->Dirs->At(i) : &window->Files->At(i - window->Dirs->Count);
                                if (fileData->IconOverlayDone == 0 && (i > 0 || wcscmp(fileData->Name, L"..") != 0))
                                {
                                    fileData->IconOverlayDone = 1; // mark that this overlay was already retrieved so we don't repeat it in this cycle

                                    DWORD fileAttrs = fileData->Attr;
                                    // wide: snapshot the wide identity too, under the same lock,
                                    // for the post-call "is this still the same row" re-check below - the narrow
                                    // mirror alone can collide (two different Unicode names -> the same '?'-filled
                                    // best-fit string), which would falsely confirm identity and let the icon
                                    // overlay index computed for the OLD file get written onto a DIFFERENT one.
                                    std::wstring fileNameW = fileData->Name;
                                    int minPriority = 100;
                                    if (i >= window->Dirs->Count && fileData->IsLink || // file is a link
                                        fileData->IsOffline ||                          // file or directory is offline (slow)
                                        i < window->Dirs->Count && fileData->Shared)    // directory is shared
                                    {
                                        minPriority = 9; // overlays for links, shares and slow files (offline) have priority 10, so we take only overlays with a higher priority (numerically lower than 10)
                                    }

                                    if (window->ICSleep)
                                        goto GO_SLEEP_MODE;
                                    HANDLES(LeaveCriticalSection(&window->ICSleepSection));

                                    // let the icon be loaded from the file; the icon reader may enter sleep mode during loading
                                    // fileNameW, not fileData->Name: the lock was released above, so
                                    // the panel may free or replace its listing at any moment and the
                                    // live CFileData is no longer safe to dereference here. That is
                                    // what the snapshot taken under the lock is for - pre-unicode
                                    // passed its own snapshots (wName/fileName) for the same reason.
                                    SLOW_CALL_STACK_MESSAGE5("IconThreadThreadFBody::GetIconOverlayIndex(%ls%ls, 0x%08X, %d)",
                                                             wPath.c_str(), fileNameW.c_str(), fileAttrs, isGoogleDrivePath);
                                    DWORD iconOverlayIndex = ShellIconOverlays.GetIconOverlayIndex(wPath.c_str(), fileNameW.c_str(), fileAttrs,
                                                                                                   minPriority, iconReadersIconOverlayIds,
                                                                                                   isGoogleDrivePath);
                                    //                    TRACE_I("Getting icon overlay index is done.");

                                    HANDLES(EnterCriticalSection(&window->ICSleepSection));
                                    if (window->ICSleep)
                                        goto GO_SLEEP_MODE; // panel already wants to switch to sleep mode

                                    CFileData* fileDataCheck = i < window->Dirs->Count ? &window->Dirs->At(i) : i < window->Files->Count + window->Dirs->Count ? &window->Files->At(i - window->Dirs->Count)
                                                                                                                                                               : NULL;
                                    // wide: difference test - the wide identity check replaces the
                                    // narrow strcmp outright (case-sensitive, matching the original), it is not
                                    // AND'd with it: if the narrow mirror falsely says "same name" the AND would
                                    // short-circuit before the wide check ever ran, silently reproducing the bug.
                                    if (fileData != fileDataCheck ||
                                        fileData->Name != fileNameW)
                                    {
                                        if (fileData != fileDataCheck)
                                            TRACE_E("IconThreadThreadFBody::GetIconOverlayIndex: PRUSER!!! (fileData != fileDataCheck)");
                                        else
                                            TRACE_E("IconThreadThreadFBody::GetIconOverlayIndex: PRUSER!!! (file name changed)");
                                    }
                                    else
                                    {
                                        BOOL redraw = fileData->IconOverlayIndex != iconOverlayIndex;
                                        fileData->IconOverlayIndex = iconOverlayIndex;

                                        int visArrVer;
                                        BOOL visArrValid;
                                        if (redraw && // the index needs to be redrawn (icon overlay changed)
                                            (window->VisibleItemsArray.ArrContainsIndex(i, &visArrValid, &visArrVer) || !visArrValid))
                                        { // if we know the item is visible or if visibility is unknown, let the index be redrawn
                                            PostMessage(window->HWindow, WM_USER_REFRESHINDEX2, i, 0);
                                        }
                                    }
                                }
                                else
                                    callWaitForObjects = FALSE; // no work -> no waiting
                            }
                            else
                            {
                                if (iconData->GetReadingDone() == 0 &&
                                    iconData->GetFlag() == wanted)
                                {
                                    iconData->SetReadingDone(1);    // mark that we have already worked with this icon so we do not try again during this cycle
                                    if (wanted == 0 || wanted == 2) // loading icons directly from a file or from a plug-in
                                    {
                                        if (!pluginFSIconsFromPlugin) // icon on disk
                                        {
                                            std::wstring iconPathW = window->GetPathW();
                                            if (!iconPathW.empty() && iconPathW.back() != L'\\')
                                                iconPathW += L'\\';
                                            iconPathW += iconData->NameAndData;

                                            if (window->ICSleep)
                                                goto GO_SLEEP_MODE;
                                            HANDLES(LeaveCriticalSection(&window->ICSleepSection));

                                            if (waitBeforeFirstReadIcon)
                                            {
                                                waitBeforeFirstReadIcon = FALSE;
                                                Sleep(500);
                                            }

                                            CALL_STACK_MESSAGE3("IconThreadThreadFBody::GetFileIcon(%ls, %d)", iconPathW.c_str(), iconSize);
                                            if (!pathIsInvalid)
                                            {
                                                IconThreadThreadFBodyAux(iconPathW.c_str(), shi, iconSize);
                                                if (shi.hIcon == NULL)
                                                    TRACE_IW(L"Unable to get icon from: " << iconPathW);
                                            }
                                            else
                                                shi.hIcon = NULL;

                                            HANDLES(EnterCriticalSection(&window->ICSleepSection));
                                        }
                                        else // icon in a plug-in FS - reading cannot be interrupted (risk of PluginData being destroyed)
                                        {
                                            const CFileData* f = iconData->GetFSFileData();
                                            if (f != NULL)
                                            {
                                                shi.hIcon = window->PluginData.GetPluginIcon(f, iconSize, destroyPluginIcon);
                                                if (shi.hIcon == NULL)
                                                {
                                                    TRACE_IW(L"Unable to get icon from FS item: " << iconData->NameAndData);
                                                }
                                            }
                                            else
                                            {
                                                shi.hIcon = NULL;
                                                TRACE_EW(L"Unexpected error: Icon Cache doesn't contain FSFileData for item from FS with "
                                                         L"pitFromPlugin icon type! Item: "
                                                         << iconData->NameAndData);
                                            }
                                        }
                                    }
                                    else
                                    {
                                        if (wanted == 3) // loading icons from the icon-location
                                        {
                                            shi.hIcon = NULL;
                                            // NameAndData is wchar_t* now (Flag 3: name,
                                            // then zero-padded alignment, then the icon-location string -
                                            // icncache.h's own documented contract). Mirrors the exact
                                            // writer-side packed-buffer math (files_window_directory_read.cpp's
                                            // IsAssociatedStatic/IsAssociated NameAndData construction): the
                                            // name portion's WCHAR length converts to a BYTE count before the
                                            // '+4; -= &3' alignment, and the resulting BYTE offset converts
                                            // back to WCHAR units before advancing the wchar_t* pointer - the
                                            // old char-count math silently halved both the size and the offset.
                                            wchar_t* nameAndData = iconData->NameAndData;
                                            int nameLenBytes = (int)wcslen(nameAndData) * (int)sizeof(wchar_t);
                                            int size = nameLenBytes + 4;
                                            size -= (size & 0x3);                              // size % 4 (alignment to four bytes)
                                            wchar_t* s = nameAndData + size / sizeof(wchar_t); // skip the alignment zeros (WCHAR units)
                                            BOOL doExtractIcons = FALSE;
                                            BOOL doLoadImage = FALSE;
                                            int index = -1;
                                            wchar_t* num = wcsrchr(s, L','); // icon index follows the last comma
                                            std::wstring iconPathW;
                                            if (num != NULL)
                                            {
                                                index = _wtoi(num + 1);
                                                iconPathW.assign(s, num);
                                                doExtractIcons = !iconPathW.empty();
                                            }
                                            else
                                            {
                                                iconPathW = s;
                                                doLoadImage = !iconPathW.empty();
                                            }

                                            if (window->ICSleep)
                                                goto GO_SLEEP_MODE;
                                            HANDLES(LeaveCriticalSection(&window->ICSleepSection));

                                            if (waitBeforeFirstReadIcon)
                                            {
                                                waitBeforeFirstReadIcon = FALSE;
                                                //                          TRACE_I("Waiting 500ms before reading first icon in second round to have bigger chance to succeed.");
                                                Sleep(500); // take a short break before the second attempt to load the icon
                                            }

                                            if (doExtractIcons)
                                            {
                                                // load the icon from the file (ExtractIcons retrieves it by index);
                                                // the icon reader may go to sleep mode while loading
                                                CALL_STACK_MESSAGE4("IconThreadThreadFBody::ExtractIcons(%ls, %d, %d, ...)", iconPathW.c_str(), index, IconSizes[iconSize]);
                                                if (ExtractIconsW(iconPathW.c_str(), index, IconSizes[iconSize], IconSizes[iconSize], &shi.hIcon, NULL, 1, IconLRFlags) != 1)
                                                {
                                                    TRACE_IW(L"Unable to get icon from: " << iconPathW << L", " << index);
                                                    shi.hIcon = NULL;
                                                }
                                                //                          else
                                                //                            TRACE_I("ExtractIcons is done.");
                                            }

                                            if (doLoadImage)
                                            {
                                                {
                                                    // load the icon from a file (likely .ico); the icon reader can switch to sleep mode during loading
                                                    CALL_STACK_MESSAGE2("IconThreadThreadFBody::LoadImage(%ls)", iconPathW.c_str());
                                                    shi.hIcon = (HICON)NOHANDLES(LoadImageW(NULL, iconPathW.c_str(), IMAGE_ICON, IconSizes[iconSize], IconSizes[iconSize],
                                                                                           LR_LOADFROMFILE | IconLRFlags));
                                                    //                            TRACE_I("LoadImage " << (shi.hIcon == NULL ? "has failed, now trying ExtractIcons..." : "is done."));
                                                }
                                                if (shi.hIcon == NULL) // LoadImage failed; trying ExtractIcons as well (e.g., an icon without index from zipfldr.dll on XP: a .zip archive packed in a .7z archive)
                                                {
                                                    // let the first icon load from the file; the icon reader may enter sleep mode while loading
                                                    CALL_STACK_MESSAGE3("IconThreadThreadFBody::ExtractIcons(%ls, (0), %d, ...)", iconPathW.c_str(), IconSizes[iconSize]);
                                                    if (ExtractIconsW(iconPathW.c_str(), 0, IconSizes[iconSize], IconSizes[iconSize], &shi.hIcon, NULL, 1, IconLRFlags) != 1)
                                                    {
                                                        TRACE_IW(L"Unable to get first icon from: " << iconPathW);
                                                        shi.hIcon = NULL;
                                                    }
                                                    //                            else
                                                    //                              TRACE_I("ExtractIcons is done.");
                                                }
                                            }

                                            HANDLES(EnterCriticalSection(&window->ICSleepSection));
                                        }
                                        else // wanted == 4 or 6; loading thumbnails from a plug-in ("thumbnail loader")
                                        {
                                            shi.hIcon = NULL; // precaution against incorrect icon deallocation (none is created here)

                                            // NameAndData is wchar_t* now (Flag 4/5/6: name,
                                            // then zero-padded alignment, then CQuadWord Size + FILETIME
                                            // LastWrite + a NULL-terminated interface-pointer list -
                                            // icncache.h's own documented contract, matching the writer's
                                            // packed-buffer construction in files_window_directory_read.cpp).
                                            // The WCHAR name length converts to a BYTE count before the
                                            // '+4; -= &3' alignment, and every offset past the name is walked
                                            // via an explicit BYTE* - never wchar_t* pointer arithmetic, which
                                            // would silently double the advance (same bug class already found
                                            // and fixed on the writer side).
                                            wchar_t* s = iconData->NameAndData;
                                            int lenBytes = (int)wcslen(s) * (int)sizeof(wchar_t);
                                            int nameSize = lenBytes + 4;
                                            nameSize -= (nameSize & 0x3); // nameSize % 4 (alignment to four bytes)
                                            std::wstring thumbPathW = window->GetPathW();
                                            if (!thumbPathW.empty() && thumbPathW.back() != L'\\')
                                                thumbPathW += L'\\';
                                            thumbPathW += s;

                                            CPluginInterfaceForThumbLoaderEncapsulation** loader;
                                            BYTE* raw = (BYTE*)s;
                                            loader = (CPluginInterfaceForThumbLoaderEncapsulation**)(raw + nameSize + sizeof(CQuadWord) + sizeof(FILETIME));
                                            while (*loader != NULL)
                                            {
                                                int thumbnailSize = window->GetThumbnailSize();
                                                thumbMaker.Clear(thumbnailSize);
                                                CALL_STACK_MESSAGE3("IconThreadThreadFBody::LoadThumbnail(%ls, %d)", thumbPathW.c_str(), wanted == 4);
                                                if ((*loader)->LoadThumbnail(thumbPathW.c_str(), thumbnailSize, thumbnailSize, &thumbMaker, wanted == 4))
                                                {
                                                    thumbnailFlag = wanted == 4 /* first thumbnail loading round */ ? (thumbMaker.IsOnlyPreview() ? 6 /* low-quality/smaller */ : 5 /* quality */) : 5 /* in the second round all obtained thumbnails are quality */;
                                                    thumbMaker.HandleIncompleteImages();
                                                    break; // the thumbnail may be loaded; do not try another plug-in
                                                }
                                                loader++; // try the next plug-in in line, it might load the thumbnail
                                            }
                                            if (*loader == NULL)
                                                thumbMaker.Clear(); // failed thumbnail -> clean it up
                                        }
                                    }

                                    if (window->ICSleep) // the panel wants to switch to sleep mode
                                    {
                                        thumbMaker.Clear(); // the thumbnail will no longer be needed

                                        // if this is not an icon from a plug-in that forbids icon destruction, destroy it
                                        if (shi.hIcon != NULL && (!pluginFSIconsFromPlugin || destroyPluginIcon))
                                        {
                                            ::NOHANDLES(DestroyIcon(shi.hIcon));
                                        }
                                        goto GO_SLEEP_MODE;
                                    }

                                    if (wanted <= 3) // we were obtaining an icon
                                    {
                                        if (shi.hIcon == NULL)
                                            failed = TRUE;
                                        else
                                        {
                                            if (window->IconCache->GetIcon(iconData->GetIndex(),
                                                                           &iconList, &iconListIndex))
                                            {
                                                HANDLES(EnterCriticalSection(&window->ICSectionUsingIcon));

                                                iconList->ReplaceIcon(iconListIndex, shi.hIcon);
                                                iconData->SetFlag(1); // already loaded

                                                HANDLES(LeaveCriticalSection(&window->ICSectionUsingIcon));

                                                // find the index of the item for which we loaded the icon

                                                if (pluginFSIconsFromPlugin) // pitFromPlugin: let the plug-in compare items itself (must compare with no duplicates)
                                                {
                                                    const CFileData* file = iconData->GetFSFileData();
                                                    if (file != NULL)
                                                    {
                                                        CPluginDataInterfaceEncapsulation* dataIface = &window->PluginData;
                                                        CFilesArray* arr = window->Dirs;
                                                        int z;
                                                        for (z = 0; z < arr->Count; z++)
                                                        {
                                                            if (dataIface->CompareFilesFromFS(file, &arr->At(z)) == 0)
                                                            {
                                                                PostMessage(window->HWindow, WM_USER_REFRESHINDEX, z, 0);
                                                                break;
                                                            }
                                                        }
                                                        if (z == window->Dirs->Count) // it was not a directory
                                                        {
                                                            arr = window->Files;
                                                            int j;
                                                            for (j = 0; j < arr->Count; j++)
                                                            {
                                                                if (dataIface->CompareFilesFromFS(file, &arr->At(j)) == 0)
                                                                {
                                                                    PostMessage(window->HWindow, WM_USER_REFRESHINDEX,
                                                                                window->Dirs->Count + j, 0);
                                                                    break;
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                                else // duplicate names are not a problem (e.g., archives where identical names cannot have different icons)
                                                {
                                                    // NameAndData is wchar_t* now; the name
                                                    // portion is always NUL-terminated first regardless of
                                                    // any trailing tag data, so a direct wcscmp against it
                                                    // is correct and needs no packed-buffer decoding here.
                                                    wchar_t* name2 = iconData->NameAndData;
                                                    CFilesArray* arr = window->Dirs;
                                                    int z;
                                                    for (z = 0; z < arr->Count; z++)
                                                    {
                                                        if (wcscmp(name2, arr->At(z).Name) == 0)
                                                        {
                                                            PostMessage(window->HWindow, WM_USER_REFRESHINDEX, z, 0);
                                                            break;
                                                        }
                                                    }
                                                    if (z == window->Dirs->Count) // it was not a directory
                                                    {
                                                        arr = window->Files;
                                                        int j;
                                                        for (j = 0; j < arr->Count; j++)
                                                        {
                                                            if (wcscmp(name2, arr->At(j).Name) == 0)
                                                            {
                                                                PostMessage(window->HWindow, WM_USER_REFRESHINDEX,
                                                                            window->Dirs->Count + j, 0);
                                                                break;
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            // if this is not an icon from a plug-in that forbids icon destruction, destroy it
                                            if (!pluginFSIconsFromPlugin || destroyPluginIcon)
                                            {
                                                ::NOHANDLES(DestroyIcon(shi.hIcon));
                                            }
                                        }
                                    }
                                    else // we were obtaining a thumbnail
                                    {
                                        if (thumbMaker.ThumbnailReady())
                                        {
                                            CThumbnailData* thumbnailData;
                                            if (window->IconCache->GetThumbnail(iconData->GetIndex(),
                                                                                &thumbnailData))
                                            {
                                                BOOL thumbnailCreated = FALSE;

                                                HANDLES(EnterCriticalSection(&window->ICSectionUsingThumb));
                                                thumbMaker.TransformThumbnail();
                                                if (thumbMaker.RenderToThumbnailData(thumbnailData))
                                                {
                                                    iconData->SetFlag(thumbnailFlag); // already loaded
                                                    if (thumbnailFlag == 6 /* low-quality/smaller thumbnail in the first loading round */)
                                                        iconData->SetReadingDone(0); // another round will follow, so mark as not "done"
                                                    thumbnailCreated = TRUE;
                                                }
                                                HANDLES(LeaveCriticalSection(&window->ICSectionUsingThumb));

                                                if (thumbnailCreated)
                                                {
                                                    // find the index of the file (directories have no thumbnails) for which we loaded the thumbnail
                                                    // NameAndData is wchar_t* now; the name
                                                    // portion is always NUL-terminated first regardless of
                                                    // the trailing thumbnail tag, so a direct wcscmp works.
                                                    wchar_t* name2 = iconData->NameAndData;
                                                    int z;
                                                    for (z = 0; z < window->Files->Count; z++)
                                                    {
                                                        if (wcscmp(name2, window->Files->At(z).Name) == 0)
                                                        {
                                                            PostMessage(window->HWindow, WM_USER_REFRESHINDEX,
                                                                        window->Dirs->Count + z, 0);
                                                            break;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        thumbMaker.Clear(); // the thumbnail will not be needed anymore
                                    }
                                }
                                else
                                    callWaitForObjects = FALSE; // no work -> no waiting
                            }
                        }
                        else
                        {
                            someNameSkipped = TRUE;     // at least one name was skipped
                            callWaitForObjects = FALSE; // no work -> no waiting
                        }
                    }
                    else
                    {
                        if (canReadIconOverlays && !readIconOverlaysNow)
                        { // now we are going to read icon overlays
                            i = 0;
                            readIconOverlaysNow = TRUE;
                            //                TRACE_I("readIconOverlaysNow=" << readIconOverlaysNow);
                            continue;
                        }
                        else
                        {
                            readIconOverlaysNow = FALSE;
                            //                TRACE_I("readIconOverlaysNow=" << readIconOverlaysNow);
                        }

                        if (!readOnlyVisibleItems && (selectMode == 2 || selectMode == 3))
                        {
                            i = 0;
                            selectMode++;
                            //                TRACE_I("selectMode=" << selectMode);
                            continue;
                        }

                        // the first icon-reading round is over, so all icon overlays are loaded -> prevent needless attempts to read them again
                        canReadIconOverlays = FALSE;

                        // loading order: new icons, new thumbnails, old icons, old thumbnails
                        BOOL done = FALSE; // TRUE == break, everything is loaded
                        switch (wanted)
                        {
                        case 0: // new icons have already been loaded
                        {
                            // if thumbnails should be read and this is the first round (plug-ins do not work
                            // randomly like the system, so if they fail the first time they will never load), read
                            // new thumbnails (wanted == 4)
                            if (readThumbnails && firstRound)
                                wanted = 4;
                            else
                                wanted = 2; // otherwise reload old (inherited) icons
                            break;
                        }

                        case 4: // new thumbnails have already been loaded
                        {
                            wanted = 2; // reload old (inherited) icons
                            break;
                        }

                        case 2: // old icons have already been loaded
                        {
                            if (readThumbnails && firstRound)
                                wanted = 6; // reload old (inherited + low-quality/smaller) thumbnails
                            else
                                done = TRUE;
                            break;
                        }

                        default:
                            done = TRUE;
                            break;
                        }
                        if (done)
                            break; // finished - wanted 0 and 2 or 0, 4, 2 and 6 or just 3 or -1 (error)

                        //              TRACE_I("wanted=" << wanted);

                        if (selectMode == 4)
                        {
                            i = 0;
                            selectMode = 2;
                            //                TRACE_I("selectMode=" << selectMode);
                            readIconOverlaysNow = FALSE;
                            //                TRACE_I("readIconOverlaysNow=" << readIconOverlaysNow);
                            continue;
                        }

                        i = -1;                     // ensure 'i' becomes zero
                        callWaitForObjects = FALSE; // no work -> no waiting
                    }

                    i++;
                    if (callWaitForObjects)
                    {
                        wait = WaitForMultipleObjects(2, handles, FALSE, 0);
                        // we will not ignore the "work" signal because each "sleep->wake-up" means starting work from the beginning
                        if (wait != WAIT_TIMEOUT)
                            break; // process the wait event
                    }
                    // else wait = WAIT_TIMEOUT;  // needless, wait is already WAIT_TIMEOUT
                }
                repeatedRound = FALSE;

                if (wait == WAIT_TIMEOUT && readOnlyVisibleItemsDueToUMI)
                { // not all icons may be loaded due to priority given to usermenu icons (read before icons outside the visible area)
                    if (UserMenuIconBkgndReader.IsReadingIcons())
                    {
                        //              TRACE_I("Visible icons done, giving priority to usermenu icons...");
                        while (1)
                        {
                            if (window->ICSleep)
                                goto GO_SLEEP_MODE;
                            HANDLES(LeaveCriticalSection(&window->ICSleepSection));

                            wait = WaitForMultipleObjects(2, handles, FALSE, 100); // give some time for usermenu icon loading

                            HANDLES(EnterCriticalSection(&window->ICSleepSection));
                            if (window->ICSleep)
                                goto GO_SLEEP_MODE; // the panel already wants to switch to sleep mode

                            if (wait != WAIT_TIMEOUT)
                            {
                                //                  TRACE_I("Handling event...");
                                break; // process the wait event
                            }
                            int visArrVer; // check if the visible area changed; if so we must start reading icons again
                            if (someNameSkipped && window->VisibleItemsArray.IsArrValid(&visArrVer) && visArrVer != lastVisArrVersion)
                            {
                                //                  TRACE_I("Change of visible items array...");
                                break;
                            }
                            if (!UserMenuIconBkgndReader.IsReadingIcons())
                            {
                                //                  TRACE_I("Usermenu icons done...");
                                break; // if usermenu icons are already done, read the remaining icons in the panel
                            }
                        }
                    }
                    if (wait == WAIT_TIMEOUT) // reason to retry reading icons (visible area change or usermenu icons finished)
                    {
                        if (!UserMenuIconBkgndReader.IsReadingIcons()) // if usermenu icons are done, read icons outside the visible area
                        {
                            //                if (someNameSkipped) TRACE_I("Usermenu icons done, going to read the rest of icons in panel...");
                            readOnlyVisibleItems = FALSE;
                            readOnlyVisibleItemsDueToUMI = FALSE;
                        }
                        //              else
                        //                if (someNameSkipped) TRACE_I("Going to reread visible icons in panel...");
                        if (someNameSkipped)
                        {
                            repeatedRound = TRUE; // an extra round (we do not want to read icon overlays again)
                            goto SECOND_ROUND;
                        }
                        //              else
                        //                TRACE_I("All items in panel are visible, so no reason to reread icons...");
                    }
                }

                if (wait == WAIT_TIMEOUT) // work is done -> notify the main thread
                {
                    if (window->Is(ptDisk) && failed && firstRound)
                    {                                   // try again (not all icons were loaded)
                        firstRound = FALSE;             // only one extra round
                        waitBeforeFirstReadIcon = TRUE; // prevent immediate rereading (low chance of success)
                                                        //              TRACE_I("Going to second round of reading (some icons have not been read in the first round).");
                        goto SECOND_ROUND;
                        // postRefresh = TRUE;
                    }
                    else
                        firstRound = TRUE;

                    //            TRACE_I("Stop reading.");
                    // send a notification that icon reading in the panel has finished
                    if (window->HWindow == NULL ||
                        !PostMessage(window->HWindow, WM_USER_ICONREADING_END, 0, 0))
                    { // something failed ("always false"), set IconCacheValid = TRUE here
                        window->IconCacheValid = TRUE;
                    }

                    //            if (window->HWindow != NULL)  // continuous repainting is enough
                    //              InvalidateRect(window->HWindow, NULL, TRUE);
                }
                else
                {

                GO_SLEEP_MODE:

                    // interruption (sleep icon cache thread, new work, or terminate)
                    firstRound = TRUE;
                    //            TRACE_I("Reading terminated.");
                }

                window->ICWorking = FALSE;
            }

            window->ICSleep = FALSE;
            HANDLES(LeaveCriticalSection(&window->ICSleepSection));

            /*    // replaced with goto SECOND_ROUND (reading the entire directory again freezes on network drives)
        if (postRefresh)  // moved Sleep(500) out of the critical section—it was freezing unnecessarily...
        {
          HANDLES(EnterCriticalSection(&TimeCounterSection));  // take the time when a refresh is needed
          int t1 = MyTimeCounter++;
          HANDLES(LeaveCriticalSection(&TimeCounterSection));
          Sleep(500);  // a short breather
          PostMessage(window->HWindow, WM_USER_REFRESH_DIR, 0, t1);
        }
*/

            break;
        }

        default: // terminate
        {
            run = FALSE;
            break;
        }
        }
    }

    ShellIconOverlays.ReleaseIconReadersIconOverlayIds(iconReadersIconOverlayIds);

    OleUninitialize();

    TRACE_I("End");
    return 0;
}

unsigned IconThreadThreadFEH(void* param)
{
    CALL_STACK_MESSAGE_NONE
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return IconThreadThreadFBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread IconReader: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this call still performs some operations)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI IconThreadThreadF(void* param)
{
    CALL_STACK_MESSAGE_NONE
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    return IconThreadThreadFEH(param);
}

CFilesWindow::CFilesWindow(CMainWindow* parent)
    : CFilesWindowAncestor(parent == NULL),
      Columns(20, 10), ColumnsTemplate(20, 10), VisibleItemsArray(FALSE), VisibleItemsArraySurround(TRUE)
{
    CALL_STACK_MESSAGE1("CFilesWindow::CFilesWindow()");
    NarrowedNameColumn = FALSE;
    FullWidthOfNameCol = 0;
    WidthOfMostOfNames = 0;
    ColumnsTemplateIsForDisk = FALSE; // just initialization; set later in BuildColumnsTemplate()
    StopThumbnailLoading = FALSE;
    UserWorkedOnThisPath = FALSE;

    UnpackedAssocFiles.SetPanel(this);
    QuickRenameWindow.SetPanel(this);

    FilesMap.SetPanel(this);
    ScrollObject.SetPanel(this);
    HiddenDirsFilesReason = 0;
    HiddenDirsCount = HiddenFilesCount = 0;
    IconCacheValid = FALSE;
    InactWinOptimizedReading = FALSE;
    WaitBeforeReadingIcons = 0;
    WaitOneTimeBeforeReadingIcons = 0;
    EndOfIconReadingTime = GetTickCount() - 10000;
    ICEventTerminate = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    ICEventWork = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    ICSleep = FALSE;
    ICWorking = FALSE;
    ICStopWork = FALSE;
    HANDLES(InitializeCriticalSection(&ICSleepSection));
    HANDLES(InitializeCriticalSection(&ICSectionUsingIcon));
    HANDLES(InitializeCriticalSection(&ICSectionUsingThumb));
    DWORD ThreadID;
    IconCacheThread = NULL;
    if (parent != NULL && ICEventTerminate != NULL && ICEventWork != NULL)
        IconCacheThread = HANDLES(CreateThread(NULL, 0, IconThreadThreadF, this, 0, &ThreadID));
    if (parent == NULL)
    {
        IconCache = NULL;
    }
    else if (ICEventTerminate == NULL ||
             ICEventWork == NULL ||
             IconCacheThread == NULL)
    {
        TRACE_E("Unable to start icon-reader thread.");
        IconCache = NULL;
    }
    else
    {
        //    SetThreadPriority(IconCacheThread, THREAD_PRIORITY_IDLE); // loading then fails
        IconCache = new CIconCache();
    }

    OpenedDrivesList = NULL;

    Parent = parent;
    ViewTemplate = parent != NULL ? &parent->ViewTemplates.Items[2] : NULL; // detailed view
    if (ViewTemplate != NULL)
    {
        BuildColumnsTemplate();
        CopyColumnsTemplateToColumns();
    }
    ListBox = NULL;
    StatusLine = NULL;
    DirectoryLine = NULL;
    StatusLineVisible = TRUE;
    DirectoryLineVisible = TRUE;
    HeaderLineVisible = TRUE;

    SortType = stName;
    ReverseSort = FALSE;
    SortedWithRegSet = FALSE;    // initial state doesn't matter; set in SortDirectory()
    SortedWithDetectNum = FALSE; // initial state doesn't matter; set in SortDirectory()
    LastFocus = INT_MAX;
    SetValidFileData(VALID_DATA_ALL);
    AutomaticRefresh = TRUE;
    NextFocusNameW.clear();
    DontClearNextFocusName = FALSE;
    LastRefreshTime = 0;
    FilesActionInProgress = FALSE;
    CanDrawItems = TRUE;
    FileBasedCompression = FALSE;
    FileBasedEncryption = FALSE;
    SupportACLS = FALSE;
    DeviceNotification = NULL;
    ContextMenu = NULL;
    ContextSubmenuNew = new CMenuNew;
    UseSystemIcons = FALSE;
    UseThumbnails = FALSE;
    NeedRefreshAfterEndOfSM = FALSE;
    RefreshAfterEndOfSMTime = 0;
    PluginFSNeedRefreshAfterEndOfSM = FALSE;
    SmEndNotifyTimerSet = FALSE;
    RefreshDirExTimerSet = FALSE;
    RefreshDirExLParam = 0;
    InactiveRefreshTimerSet = FALSE;
    InactRefreshLParam = 0;
    LastInactiveRefreshStart = LastInactiveRefreshEnd = 0;

    NeedRefreshAfterIconsReading = FALSE;
    RefreshAfterIconsReadingTime = 0;

    PathHistory = new CPathHistory();

    DontDrawIndex = -1;
    DrawOnlyIndex = -1;

    FocusFirstNewItem = FALSE;

    ExecuteAssocEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));
    AssocUsed = FALSE;

    FilterEnabled = FALSE;
    Filter.SetMasksString(L"*.*");
    int errPos;
    Filter.PrepareMasks(errPos);

    QuickSearchMode = FALSE;
    CaretHeight = 1; // dummy
    QuickSearch.clear();
    QuickSearchMask.clear();
    SearchIndex = INT_MAX;
    FocusedIndex = 0;
    FocusVisible = FALSE;

    DropTargetIndex = -1;
    SingleClickIndex = -1;
    SingleClickAnchorIndex = -1;
    GetCursorPos(&OldSingleClickMousePos);

    TrackingSingleClick = FALSE;
    DragBox = FALSE;
    DragBoxVisible = FALSE;
    ScrollingWindow = FALSE;

    SkipCharacter = FALSE;
    SkipSysCharacter = FALSE;

    //  ShiftSelect = FALSE;
    DragSelect = FALSE;
    BeginDragDrop = FALSE;
    DragDropLeftMouseBtn = FALSE;
    BeginBoxSelect = FALSE;
    PersistentTracking = FALSE;

    TrackingSingleClick = 0;

    CutToClipChanged = FALSE;

    PerformingDragDrop = FALSE;

    GetPluginIconIndex = InternalGetPluginIconIndex;

    EnumFileNamesSourceUID = -1;

    TemporarilySimpleIcons = FALSE;
    NumberOfItemsInCurDir = 0;

    NeedIconOvrRefreshAfterIconsReading = FALSE;
    LastIconOvrRefreshTime = GetTickCount() - ICONOVR_REFRESH_PERIOD;
    IconOvrRefreshTimerSet = FALSE;
}

CFilesWindow::~CFilesWindow()
{
    CALL_STACK_MESSAGE1("CFilesWindow::~CFilesWindow()");

    if (DeviceNotification != NULL)
        TRACE_E("CFilesWindow::~CFilesWindow(): unexpected situation: DeviceNotification != NULL");

    ClearHistory();

    if (PathHistory != NULL)
        delete PathHistory;

    if (IconCacheThread != NULL)
    {
        SetEvent(ICEventTerminate); // icon reader, terminate yourself!
        if (WaitForSingleObject(IconCacheThread, 1000) == WAIT_TIMEOUT)
        { // it has one second to exit gracefully, then a kill is necessary (the window is being deallocated)
            TRACE_E("Terminating Icon Thread");
            TerminateThread(IconCacheThread, 666);
            WaitForSingleObject(IconCacheThread, INFINITE); // wait until the thread really ends; sometimes it takes quite a while
        }
        HANDLES(CloseHandle(IconCacheThread));
    }

    HANDLES(DeleteCriticalSection(&ICSectionUsingThumb));
    HANDLES(DeleteCriticalSection(&ICSectionUsingIcon));
    HANDLES(DeleteCriticalSection(&ICSleepSection));
    if (ICEventTerminate != NULL)
        HANDLES(CloseHandle(ICEventTerminate));
    if (ICEventWork != NULL)
        HANDLES(CloseHandle(ICEventWork));

    if (IconCache != NULL)
        delete IconCache;
    if (ContextSubmenuNew != NULL)
        delete ContextSubmenuNew;
    if (ExecuteAssocEvent != NULL)
        HANDLES(CloseHandle(ExecuteAssocEvent));
}

void CFilesWindow::ClearHistory()
{
    if (PathHistory != NULL)
        PathHistory->ClearHistory();

    OldSelection.Clear();
}

void CFilesWindow::SleepIconCacheThread()
{
    CALL_STACK_MESSAGE1("CFilesWindow::SleepIconCacheThread()");
    ICSleep = TRUE;          // to interrupt the icon-reading loop (ICSleepSection may not be left at all)
    ICStopWork = TRUE;       // to interrupt the icon-reading loop if ICStopWork has already been processed
    ResetEvent(ICEventWork); // to interrupt the icon-reading loop if ICStopWork has not been processed yet
    // wait until the icon reader enters a part where sleep mode is possible
    HANDLES(EnterCriticalSection(&ICSleepSection));
    ICSleep = ICWorking; // TRUE only if the icon reader is stuck in SHGetFileInfo
    HANDLES(LeaveCriticalSection(&ICSleepSection));
}

void CFilesWindow::WakeupIconCacheThread()
{
    CALL_STACK_MESSAGE_NONE
    ICStopWork = FALSE;    // so that the work is not interrupted right from the start
    SetEvent(ICEventWork); // switch to work mode without waiting for a response
    MSG msg;               // remove any WM_USER_ICONREADING_END that would set IconCacheValid = TRUE
    while (PeekMessageW(&msg, HWindow, WM_USER_ICONREADING_END, WM_USER_ICONREADING_END, PM_REMOVE))
        ;
}

BOOL CFilesWindow::CheckAndRestorePath(const wchar_t* path)
{
    CALL_STACK_MESSAGE2("CFilesWindow::CheckAndRestorePath(%S)", path);

    // we will not test network paths if we have just accessed them
    BOOL tryNet = (!Is(ptDisk) && !Is(ptZIPArchive)) || !HasTheSameRootPath(path, GetPathW());

    return SalCheckAndRestorePathW(HWindow, path, tryNet);
}

BOOL CFilesWindow::CanUnloadPlugin(HWND parent, CPluginInterfaceAbstract* plugin)
{
    CALL_STACK_MESSAGE1("CFilesWindow::CanUnloadPlugin()");

    if (Is(ptDisk))
    {
        if (UseThumbnails && // thumbnails are being loaded
            !IconCacheValid) // the icon reader has not finished loading yet
        {
            CPluginData* p = Plugins.GetPluginData(plugin);
            if (p != NULL) // "always true"
            {
                if (p->ThumbnailMasks.GetMasksString()[0] != 0)
                { // this plugin provides thumbnails—we aren't sure whether
                    // it also serves this panel, so we must stop reading icons
                    SleepIconCacheThread();
                    p->ThumbnailMasksDisabled = TRUE; // during plugin unload/remove this plugin cannot be used to load thumbnails
                    StopThumbnailLoading = TRUE;      // in case WakeupIconCacheThread is called; icon-cache data about "thumbnail loaders" can't be used
                    UseThumbnails = FALSE;            // prevent an unwanted icon-reader wake-up (WakeupIconCacheThread())
                    if (!CriticalShutdown)
                    {
                        HANDLES(EnterCriticalSection(&TimeCounterSection));
                        int t1 = MyTimeCounter++;
                        HANDLES(LeaveCriticalSection(&TimeCounterSection));
                        PostMessage(HWindow, WM_USER_REFRESH_DIR, 0, t1); // ensure the icon cache is refilled (ideally after the plug-in unload/remove)
                    }
                }
            }
            else
                TRACE_E("CFilesWindow::CanUnloadPlugin(): Unexpected situation!");
        }
    }
    else
    {
        BOOL used = FALSE;
        if ((Is(ptZIPArchive) || Is(ptPluginFS)) &&
            PluginData.NotEmpty() && PluginData.GetPluginInterface() == plugin)
            used = TRUE;
        else
        { // a filesystem may not use PluginData, so we must also check PluginFS
            if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
                GetPluginFS()->GetPluginInterface() == plugin)
                used = TRUE;
            else
            {
                if (Is(ptZIPArchive))
                { // an archive may not use PluginData, therefore we must also test archive associations
                    // this part matters only when shutting Salamander down—otherwise the plug-in
                    // could unload while the archiver is still in use (each archiver function loads the plug-in)
                    // NOTE: icon overlays from the plug-in are an exception; after unload they would stop drawing
                    //       (the plug-in's overlay table is released during unload)
                    int format = PackerFormatConfig.PackIsArchive(GetZIPArchive());
                    if (format != 0) // found a supported archive
                    {
                        format--;
                        CPluginData* data;
                        int index = PackerFormatConfig.GetUnpackerIndex(format);
                        if (index < 0) // view: is this internal processing (plug-in)?
                        {
                            data = Plugins.Get(-index - 1);
                            if (data != NULL && data->GetPluginInterface()->GetInterface() == plugin)
                                used = TRUE;
                        }
                        if (PackerFormatConfig.GetUsePacker(format)) // has an editor?
                        {
                            index = PackerFormatConfig.GetPackerIndex(format);
                            if (index < 0) // is this internal processing (plug-in)?
                            {
                                data = Plugins.Get(-index - 1);
                                if (data != NULL && data->GetPluginInterface()->GetInterface() == plugin)
                                    used = TRUE;
                            }
                        }
                    }
                }
            }
        }
        if (used)
        {
            if (Is(ptZIPArchive) || Is(ptPluginFS)) // archive -> just leave it; plug-in FS -> return to the last disk path
            {
                std::wstring path = GetPathW();

                DWORD err, lastErr;
                BOOL pathInvalid, cut;
                BOOL tryNet = FALSE; // no more network delays, unnecessary...
                if (SalCheckAndRestorePathWithCutW(HWindow, path, tryNet, err, lastErr, pathInvalid, cut, TRUE))
                { // switch to a path that should load without issues
                    ChangePathToDisk(parent, path.c_str(), -1, NULL, NULL, TRUE, TRUE, FALSE, NULL, FALSE, FSTRYCLOSE_UNLOADCLOSEFS);
                }
                else // the original path (or its subpath) is inaccessible -> switching to a fixed drive (cannot call
                     // ChangePathToDisk directly, because it would display an error like "X: not ready")
                {
                    ChangeToRescuePathOrFixedDrive(parent, NULL, TRUE, TRUE, FSTRYCLOSE_UNLOADCLOSEFS);
                }
                if (!Is(ptDisk))
                {
                    return FALSE; // switching to a disk path failed; unload is not possible
                }
            }
        }
    }
    return TRUE;
}

void CFilesWindow::RedrawFocusedIndex()
{
    CALL_STACK_MESSAGE1("CFilesWindow::RedrawFocusedIndex()");
    RedrawIndex(FocusedIndex);
}

void CFilesWindow::DirectoryLineSetText()
{
    CALL_STACK_MESSAGE1("CFilesWindow::DirectoryLineSetText()");
    // ZIPbuf/path/buf are fully wide now: GetZIPArchive()/GetZIPPath()/
    // GetPluginFSName() are already wide (no remaining narrow form), and CStatusWindow::SetText
    // itself now only takes const wchar_t* - a prior sweep left ZIPbuf/path as narrow types
    // that none of their actual sources or sinks matched anymore.
    const wchar_t* path = NULL;
    std::wstring ownedPath;
    if (Is(ptZIPArchive))
    {
        ownedPath = GetZIPArchive();
        if (GetZIPPath()[0] != 0)
        {
            if (GetZIPPath()[0] != L'\\')
                ownedPath += L'\\';
            ownedPath += GetZIPPath();
        }
        path = ownedPath.c_str();
        // AddPath collapsed to one wide value per slot (P1.5j) - no more
        // narrow+wide twin arguments.
        PathHistory->AddPath(1, GetZIPArchive(), GetZIPPath(), NULL, NULL);
    }
    else
    {
        if (Is(ptDisk))
        {
            PathHistory->AddPath(0, GetPathW(), NULL, NULL, NULL);
            path = GetPathW();
        }
        else
        {
            if (Is(ptPluginFS))
            {
                std::wstring userPart;
                const BOOL haveUserPart = GetPluginFS()->NotEmpty() && GetPluginFS()->GetCurrentPathW(userPart);
                ownedPath = GetPluginFS()->GetPluginFSName();
                ownedPath += L':';
                if (haveUserPart)
                {
                    ownedPath += userPart;
                    PathHistory->AddPath(2, GetPluginFS()->GetPluginFSName(), userPart.c_str(),
                                         GetPluginFS()->GetInterface(), GetPluginFS());
                }
                path = ownedPath.c_str();
            }
        }
    }

    if (path == NULL)
        return;

    if (FilterEnabled)
    {
        int pathLen = (int)wcslen(path);
        if (Is(ptDisk))
        {
            std::wstring bufW = GetPathW();
            int pathLenW = (int)bufW.size();
            if (!bufW.empty() && bufW.back() != L'\\')
                bufW.push_back(L'\\');
            // wide - GetMasksString() is itself a narrow rendering of the
            // real wide storage; converting it back with AnsiToWide double-narrowed the
            // filter mask shown in the directory line for any non-ANSI-codepage mask.
            bufW += Filter.GetMasksString();
            DirectoryLine->SetText(bufW.c_str(), pathLenW);
        }
        else if (Is(ptZIPArchive))
        {
            int pathLenW = (int)ownedPath.size();
            if (!ownedPath.empty() && ownedPath.back() != L'\\')
                ownedPath.push_back(L'\\');
            ownedPath += Filter.GetMasksString();
            DirectoryLine->SetText(ownedPath.c_str(), pathLenW);
        }
        else
        {
            if (Is(ptPluginFS))
            {
                std::wstring filteredPath(path);
                filteredPath += L':';
                //        if (FilterInverse) buf[l++] = '-';
                filteredPath += Filter.GetMasksString();
                DirectoryLine->SetText(filteredPath.c_str(), pathLen);
            }
        }
    }
    else
    {
        if (Is(ptDisk))
            DirectoryLine->SetText(GetPathW());
        else if (Is(ptZIPArchive))
            DirectoryLine->SetText(ownedPath.c_str());
        else
            DirectoryLine->SetText(path);
    }
}

void CFilesWindow::SelectUnselect(BOOL forceIncludeDirs, BOOL select, BOOL showMaskDlg)
{
    CALL_STACK_MESSAGE4("CFilesWindow::SelectUnselect(%d, %d, %d)", forceIncludeDirs, select, showMaskDlg);
    if (showMaskDlg)
    {
        BeginStopRefresh(); // snooper takes a break
    }
    if (!showMaskDlg || CSelectDialog(HLanguage, select ? IDD_SELECTMASK : IDD_DESELECTMASK,
                                      select ? IDD_SELECTMASK : IDD_DESELECTMASK /* helpID */,
                                      HWindow, MainWindow->SelectionMask)
                                .Execute() == IDOK)
    {
        BOOL includeDirs = Configuration.IncludeDirs | forceIncludeDirs;
        const wchar_t* maskStr = showMaskDlg ? MainWindow->SelectionMask.c_str() : L"*.*";
        CMaskGroup mask(maskStr);
        int err;
        if (mask.PrepareMasks(err))
        {
            int dirsCount = Dirs->Count;
            int count = dirsCount + Files->Count;
            int start;
            if (Dirs->Count > 0 && wcscmp(Dirs->At(0).Name, L"..") == 0)
                start = 1;
            else
                start = 0;
            int i = includeDirs ? start : Dirs->Count;
            BOOL changed = FALSE;
            for (; i < count; i++)
            {
                CFileData* d = (i < dirsCount) ? &Dirs->At(i) : &Files->At(i - dirsCount);
                // wide: d->Name is already the exact wide name (NameW retired,
                // it was a redundant mirror of the same value). AgreeMasks self-computes the
                // extension from NULL.
                if (!showMaskDlg || mask.AgreeMasks(d->Name, NULL)) // in the case of *.* we will not call agree mask
                {
                    SetSel(select, d);
                    changed = TRUE;
                }
            }
            if (changed)
            {
                PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0);
                RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);
            }
            else
                gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_NOMATCHESFOUND));
        }
    }
    if (showMaskDlg)
    {
        UpdateWindow(MainWindow->HWindow);
        EndStopRefresh(); // the snooper starts again now
    }
}

void CFilesWindow::InvertSelection(BOOL forceIncludeDirs)
{
    CALL_STACK_MESSAGE2("CFilesWindow::InvertSelection(%d)", forceIncludeDirs);
    BOOL includeDirs = Configuration.IncludeDirs | forceIncludeDirs;
    int count = GetSelCount();
    int firstIndex = 0;
    if (includeDirs)
    {
        if (Dirs->Count > 0 && wcscmp(Dirs->At(0).Name, L"..") == 0)
            firstIndex = 1;
    }
    else
    {
        firstIndex = Dirs->Count;
    }

    int lastIndex = Dirs->Count + Files->Count - 1;
    if (firstIndex <= lastIndex)
    {
        int i;
        for (i = firstIndex; i <= lastIndex; i++)
        {
            CFileData* item = (i < Dirs->Count) ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
            SetSel(item->Selected != 1, item);
        }
        RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);
        PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0);
    }
}

namespace
{
// wide: the "select same name / same extension" match key, computed on the
// genuine wide name instead of the CP_ACP mirror. Mirrors the narrow rule exactly (extension =
// substring after the LAST '.'; no dot => the whole name is the base and the extension is
// empty - same rule files_window_directory_read.cpp:587-595 uses to compute CFileData::Ext) so
// the wide and narrow keys always agree when the mirror is lossless, and the wide key is
// authoritative when it isn't.
std::wstring SelectUnselectMatchKeyW(const CFileData* item, BOOL itemIsDir, BOOL byName)
{
    if (!byName && itemIsDir)
        return std::wstring(); // directories never match by extension, same as the narrow branch

    std::wstring nameW = item->Name;
    size_t dot = itemIsDir ? std::wstring::npos : nameW.rfind(L'.');
    if (byName)
        return dot == std::wstring::npos ? nameW : nameW.substr(0, dot);
    return dot == std::wstring::npos ? std::wstring() : nameW.substr(dot + 1);
}
} // namespace

void CFilesWindow::SelectUnselectByFocusedItem(BOOL select, BOOL byName)
{
    CALL_STACK_MESSAGE3("CFilesWindow::SelectUnselectByFocusedItem(%d, %d)", select, byName);
    if (FocusedIndex >= 0 && FocusedIndex < Dirs->Count + Files->Count)
    {

        //    if (!byName && FocusedIndex < Dirs->Count)
        //    {
        //
        //    }

        BOOL isDir = FocusedIndex < Dirs->Count;
        const CFileData* focusedItem = isDir ? &Dirs->At(FocusedIndex) : &Files->At(FocusedIndex - Dirs->Count);

        int firstIndex = 0;
        if (Configuration.IncludeDirs)
        {
            if (Dirs->Count > 0 && wcscmp(Dirs->At(0).Name, L"..") == 0)
                firstIndex = 1;
        }
        else
        {
            firstIndex = Dirs->Count;
        }
        int lastIndex = Dirs->Count + Files->Count - 1;
        int lastSelectdCount = SelectedCount;
        const wchar_t* focusedStr = byName ? focusedItem->Name : (isDir ? L"" : focusedItem->Ext);
        int focusedLen = byName ? (isDir ? focusedItem->NameLen : (int)(focusedItem->Ext - focusedItem->Name)) : (isDir ? 0 : (int)wcslen(focusedItem->Ext));
        if (!isDir && byName && *focusedItem->Ext != 0)
            focusedLen--; // skip '.'
        std::wstring focusedKeyW = SelectUnselectMatchKeyW(focusedItem, isDir, byName);
        int i;
        for (i = firstIndex; i <= lastIndex; i++)
        {
            BOOL itemIsDir = i < Dirs->Count;
            CFileData* item = itemIsDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
            const wchar_t* str = byName ? item->Name : (itemIsDir ? L"" : item->Ext);
            int len = byName ? (itemIsDir ? item->NameLen : (int)(item->Ext - item->Name)) : (itemIsDir ? 0 : (int)wcslen(item->Ext));
            if (!itemIsDir && byName && *item->Ext != 0)
                len--; // skip '.'
            if (len == focusedLen && StrNICmpW(str, focusedStr, len) == 0 &&
                _wcsicmp(SelectUnselectMatchKeyW(item, itemIsDir, byName).c_str(), focusedKeyW.c_str()) == 0)
                SetSel(select, item);
        }
        if (SelectedCount != lastSelectdCount)
        {
            RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);
            PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0);
        }
    }
}

void CFilesWindow::StoreGlobalSelection()
{
    CALL_STACK_MESSAGE1("CFilesWindow::StoreGlobalSelection()");
    int count = GetSelCount();
    if (count != 0)
    {
        BeginStopRefresh(); // snooper takes a break

        BOOL clipboard = FALSE;
        CSaveSelectionDialog dlg(HWindow, &clipboard);
        if (dlg.Execute() == IDOK)
        {
            int totalCount = Dirs->Count + Files->Count;
            if (clipboard)
            {
                // we should put the list on the clipboard

                // f->Name is wchar_t*; NameLen is a WCHAR count.
                // This used to malloc a narrow (char*) buffer sized/filled from that WCHAR
                // count via memcpy - a raw byte copy of the wide bytes into a narrow buffer,
                // the same defect shape found in the renamer plugin: every selected name,
                // ASCII included, was truncated to its first character once
                // CopyTextToClipboard (which further narrows through AnsiToWide/strlen)
                // hit the embedded 0x00 byte after the first character. Build the buffer
                // wide and use CopyTextToClipboardW instead.

                // compute the required buffer size (name1CRLFname2CRLF...nameNCRLF), in WCHARs
                DWORD size = 0;
                int i;
                for (i = 0; i < totalCount; i++)
                {
                    CFileData* f = (i < Dirs->Count) ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                    if (f->Selected)
                        size += f->NameLen + 2; // nameCRLF
                }
                if (size > 0)
                {
                    wchar_t* buff = (wchar_t*)malloc(size * sizeof(wchar_t));
                    if (buff != NULL)
                    {
                        wchar_t* p = buff;
                        for (i = 0; i < totalCount; i++)
                        {
                            CFileData* f = (i < Dirs->Count) ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                            if (f->Selected)
                            {
                                wmemcpy(p, f->Name, f->NameLen);
                                p += f->NameLen;
                                wmemcpy(p, L"\r\n", 2);
                                p += 2;
                            }
                        }
                        CopyTextToClipboardW(buff, size);
                        free(buff);
                    }
                    else
                        TRACE_E(LOW_MEMORY);
                }
            }
            else
            {
                // store the list in GlobalSelection
                GlobalSelection.Clear();
                int i;
                for (i = 0; i < totalCount; i++)
                {
                    CFileData* f = (i < Dirs->Count) ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                    if (f->Selected)
                    {
                        if (!GlobalSelection.Add(i < Dirs->Count, f->Name))
                            break; // low memory
                    }
                }
                GlobalSelection.Sort();
            }
            IdleRefreshStates = TRUE; // force state variables check on next Idle
        }
        UpdateWindow(MainWindow->HWindow);

        EndStopRefresh(); // the snooper starts again now
    }
}

void CFilesWindow::RestoreGlobalSelection()
{
    CALL_STACK_MESSAGE1("CFilesWindow::RestoreGlobalSelection()");

    BOOL clipboardValid = IsTextOnClipboard();
    BOOL globalValid = GlobalSelection.GetCount() > 0;
    if (clipboardValid || globalValid)
    {
        BeginStopRefresh(); // snooper takes a break

        CLoadSelectionOperation operation = lsoCOPY;
        BOOL clipboard = !globalValid;
        CLoadSelectionDialog dlg(HWindow, &operation, &clipboard, clipboardValid, globalValid);
        if (dlg.Execute() == IDOK)
        {
            CNames* selection = &GlobalSelection;
            CNames clipboardSelection;
            if (clipboard)
            {
                clipboardSelection.LoadFromClipboard(HWindow);
                clipboardSelection.Sort();
                selection = &clipboardSelection;
            }

            int count = Files->Count + Dirs->Count;
            int i;
            for (i = 0; i < count; i++)
            {
                BOOL isDir = i < Dirs->Count;
                CFileData* file = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                if (clipboard)
                    isDir = FALSE; // when using the clipboard everything is in Files
                switch (operation)
                {
                case lsoCOPY:
                {
                    SetSel(selection->Contains(isDir, file->Name), file);
                    break;
                }

                case lsoOR:
                {
                    if (selection->Contains(isDir, file->Name))
                        SetSel(TRUE, file);
                    break;
                }

                case lsoDIFF:
                {
                    if (file->Selected)
                        SetSel(!selection->Contains(isDir, file->Name), file);
                    break;
                }

                case lsoAND:
                {
                    SetSel(file->Selected && selection->Contains(isDir, file->Name), file);
                    break;
                }

                default:
                {
                    TRACE_E("Unknown operation: " << operation);
                    break;
                }
                }
            }
            RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);
            PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0);
        }
        UpdateWindow(MainWindow->HWindow);
        EndStopRefresh(); // the snooper starts again now
    }
}

void CFilesWindow::StoreSelection()
{
    CALL_STACK_MESSAGE1("CFilesWindow::StoreSelection()");
    OldSelection.Clear();
    int count = GetSelCount();
    if (count != 0)
    {
        OldSelection.SetCaseSensitive(IsCaseSensitive());
        int totalCount = Files->Count + Dirs->Count;
        int i;
        for (i = 0; i < totalCount; i++)
        {
            BOOL isDir = i < Dirs->Count;
            CFileData* f = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
            if (f->Selected)
            {
                if (!OldSelection.Add(isDir, f->Name))
                    break; // low memory
            }
        }
        OldSelection.Sort();
        IdleRefreshStates = TRUE; // force state variables check on next Idle
    }
}

void CFilesWindow::Reselect()
{
    CALL_STACK_MESSAGE1("CFilesWindow::Reselect()");
    int count = Files->Count + Dirs->Count;
    int i;
    for (i = 0; i < count; i++)
    {
        BOOL isDir = i < Dirs->Count;
        CFileData* file = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
        if (OldSelection.Contains(isDir, file->Name))
            SetSel(TRUE, file);
        else
            SetSel(FALSE, file);
    }
    RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);
    PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0);
}

void CFilesWindow::ShowHideNames(int mode)
{
    BOOL refreshPanel = FALSE;
    switch (mode)
    {
    case 0: // show all
    {
        if (HiddenNames.GetCount() > 0)
        {
            HiddenNames.Clear();
            refreshPanel = TRUE;
        }
        break;
    }

    case 1: // hide selected names
    {
        int count = GetSelCount();
        if (count > 0)
        {
            int totalCount = Files->Count + Dirs->Count;
            int startIndex = 0;
            if (Dirs->Count > 0 && wcscmp(Dirs->At(0).Name, L"..") == 0) // ".." should not appear in the array
                startIndex = 1;
            int i;
            for (i = 0; i < totalCount; i++)
            {
                BOOL isDir = i < Dirs->Count;
                CFileData* f = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                if (f->Selected)
                {
                    if (!HiddenNames.Add(isDir, f->Name))
                        break; // low memory, we will not continue
                    refreshPanel = TRUE;
                }
            }
        }
        break;
    }

    case 2: // hide unselected name
    {
        int totalCount = Files->Count + Dirs->Count;
        int startIndex = 0;
        if (Dirs->Count > 0 && wcscmp(Dirs->At(0).Name, L"..") == 0) // ".." should not appear in the array
            startIndex = 1;
        int i;
        for (i = startIndex; i < totalCount; i++)
        {
            BOOL isDir = i < Dirs->Count;
            CFileData* f = (isDir) ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
            if (!f->Selected)
            {
                if (!HiddenNames.Add(isDir, f->Name))
                    break; // low memory, we will not continue
                refreshPanel = TRUE;
            }
        }
        break;
    }

    default:
    {
        TRACE_E("ShowHideNames: unknown mode=" << mode);
    }
    }

    if (refreshPanel)
    {
        if (mode == 1 || mode == 2)
            HiddenNames.SetCaseSensitive(IsCaseSensitive());
        HiddenNames.Sort();
        HANDLES(EnterCriticalSection(&TimeCounterSection));
        int t1 = MyTimeCounter++;
        HANDLES(LeaveCriticalSection(&TimeCounterSection));
        PostMessage(HWindow, WM_USER_REFRESH_DIR, 0, t1);
    }
}

void CFilesWindow::SetAutomaticRefresh(BOOL value, BOOL force)
{
    CALL_STACK_MESSAGE_NONE
    if (force || AutomaticRefresh != value)
    {
        AutomaticRefresh = value;
        /* // "throwing away" the refresh mark from the directory line
    // it crashed here; a destroyed object was called
    if (DirectoryLine != NULL)                       
      DirectoryLine->SetAutomatic(AutomaticRefresh);
*/
    }
}

void CFilesWindow::GotoRoot()
{
    CALL_STACK_MESSAGE1("CFilesWindow::GotoRoot()");
    TopIndexMem.Clear(); // long jump

    std::wstring root;
    if (Is(ptDisk) || Is(ptZIPArchive))
    {
        if (Is(ptZIPArchive) && GetZIPPath()[0] != 0) // we are not in the root of the archive -> go there
        {
            ChangePathToArchive(GetZIPArchive(), L"");
        }
        else // go to the root of the Windows path
        {
            // Decides whether up-dir from a UNC root switches the panel to the
            // Nethood plugin FS instead of going nowhere. On the CP_ACP mirror a share whose
            // server or name the code page cannot spell is not recognised as a UNC root, so
            // up-dir silently does nothing.
            if (IsUNCRootPathW(GetPathW()) && Plugins.GetFirstNethoodPluginFSName(&root))
            {
                ChangePathToPluginFS(root.c_str(), L"");
            }
            else
            {
                root = GetRootPath(GetPathW());
                if (!root.empty() && root[0] == L'\\')
                    root.pop_back(); // UNC paths should not end with '\\'
                ChangePathToDisk(HWindow, root.c_str());
            }
        }
    }
    else
    {
        if (Is(ptPluginFS))
        {
            std::wstring rootPath;
            if (GetPluginFS()->GetRootPathW(rootPath))
            {
                const std::wstring fsName = GetPluginFS()->GetPluginFSName(); // in case of changes, a local copy of the name
                ChangePathToPluginFS(fsName.c_str(), rootPath.c_str());
            }
        }
    }
}

void CFilesWindow::GotoHotPath(int index)
{
    CALL_STACK_MESSAGE2("CFilesWindow::GotoHotPath(%d)", index);
    if (index < 0 || index >= HOT_PATHS_COUNT)
        return;
    //---  switch to a hot path
    std::wstring path;
    if (MainWindow->GetExpandedHotPath(HWindow, index, path))
        ChangeDir(path.c_str());
}

void CFilesWindow::SetUnescapedHotPath(int index)
{
    CALL_STACK_MESSAGE2("CFilesWindow::SetUnescapedHotPath(%d)", index);
    if (index < 0 || index >= HOT_PATHS_COUNT)
        return;
    std::wstring path;
    GetGeneralPath(path, TRUE);
    MainWindow->SetUnescapedHotPath(index, path.c_str());
}

BOOL CFilesWindow::SetUnescapedHotPathToEmptyPos()
{
    CALL_STACK_MESSAGE1("CFilesWindow::SetUnescapedHotPathToEmptyPos()");
    int index = MainWindow->GetUnassignedHotPathIndex();
    if (index != -1)
    {
        std::wstring path;
        GetGeneralPath(path, TRUE);
        MainWindow->SetUnescapedHotPath(index, path.c_str());
        return TRUE;
    }
    return FALSE;
}

#ifndef _WIN64

BOOL AreNextPathComponents(const wchar_t* relPath, const wchar_t* nextComp)
{
    const size_t len = wcslen(nextComp);
    return _wcsnicmp(relPath, nextComp, len) == 0 && (relPath[len] == L'\\' || relPath[len] == 0);
}

#endif // _WIN64

void CFilesWindow::OpenActiveFolder()
{
    CALL_STACK_MESSAGE1("CFilesWindow::OpenActiveFolder()");
    if (Is(ptDisk) && CheckPath(TRUE) != ERROR_USER_TERMINATED)
    {
        UserWorkedOnThisPath = TRUE;
        const wchar_t* path = GetPathW();

#ifndef _WIN64
        // replace "C:\\Windows\\sysnative\\*" with "C:\\Windows\\system32\\*" on 64-bit systems
        // the Explorer process knows nothing about "sysnative", so let's not bother users with it,
        // also replace "C:\\Windows\\system32\\*" with "C:\\Windows\\SysWOW64\\*"
        //  (except for a group of directories excluded from the redirector that thus point back to System32)
        std::wstring dirName;
        if (Windows64Bit && !WindowsDirectory.empty())
        {
            BOOL done = FALSE;
            dirName = WindowsDirectory;
            SalPathAppendW(dirName, L"Sysnative");
            if (!dirName.empty())
            {
                const size_t len = dirName.length();
                if (_wcsnicmp(path, dirName.c_str(), len) == 0 && (path[len] == L'\\' || path[len] == 0))
                {
                    dirName = WindowsDirectory;
                    SalPathAppendW(dirName, L"System32");
                    dirName.append(path + len);
                    path = dirName.c_str();
                    done = TRUE;
                }
            }
            if (!done)
            {
                dirName = WindowsDirectory;
                SalPathAppendW(dirName, L"System32");
                if (!dirName.empty())
                {
                    const size_t len = dirName.length();
                    if (_wcsnicmp(path, dirName.c_str(), len) == 0 && (path[len] == L'\\' || path[len] == 0))
                    {
                        // check whether it is a directory excluded from the redirector
                        if (path[len] == '\\' &&
                            (AreNextPathComponents(path + len + 1, L"catroot") ||
                             AreNextPathComponents(path + len + 1, L"catroot2") ||
                             Windows7AndLater && AreNextPathComponents(path + len + 1, L"DriverStore") ||
                             AreNextPathComponents(path + len + 1, L"drivers\\etc") ||
                             AreNextPathComponents(path + len + 1, L"LogFiles") ||
                             AreNextPathComponents(path + len + 1, L"spool")))
                        {
                            done = TRUE;
                        }
                        if (!done)
                        {
                            dirName = WindowsDirectory;
                            SalPathAppendW(dirName, L"SysWOW64");
                            dirName.append(path + len);
                            path = dirName.c_str();
                        }
                    }
                }
            }
        }
#endif // _WIN64

        std::wstring itemName;
        if (FocusedIndex < Dirs->Count + Files->Count)
        {
            CFileData* item = (FocusedIndex < Dirs->Count) ? &Dirs->At(FocusedIndex) : &Files->At(FocusedIndex - Dirs->Count);
            // hack for people who need to focus a Unicode name in Explorer; we try it via the short name
            itemName = AlterFileNameW(item->DosName != NULL ? item->DosName : item->Name, Configuration.FileNameFormat, 0, FocusedIndex < Dirs->Count);
            if (FocusedIndex < Dirs->Count && FocusedIndex == 0 && itemName == L"..")
                itemName.clear();
        }

        OpenFolderAndFocusItemW(HWindow, path, itemName.c_str());
    }
    else if (Is(ptPluginFS) &&
             GetPluginFS()->NotEmpty() &&
             GetPluginFS()->IsServiceSupported(FS_SERVICE_OPENACTIVEFOLDER))
    {
        UserWorkedOnThisPath = TRUE;
        GetPluginFS()->OpenActiveFolder(GetPluginFS()->GetPluginFSName(), HWindow);
    }
}

BOOL CFilesWindow::CommonRefresh(HWND parent, int suggestedTopIndex, const wchar_t* suggestedFocusName,
                                 BOOL refreshListBox, BOOL readDirectory, BOOL isRefresh)
{
    CALL_STACK_MESSAGE6("CFilesWindow::CommonRefresh(, %d, %ls, %d, %d, %d)", suggestedTopIndex,
                        suggestedFocusName, refreshListBox, readDirectory, isRefresh);

    //TRACE_I("common refresh: begin");
    if (readDirectory) // if only the top index and focus name should be reflected, this is not needed (could even be harmful, so we do not call it)
    {
        DirectoryLineSetText();
        if (Parent->GetActivePanel() == this)
        {
            Parent->EditWindowSetDirectory();
        }
    }

    //TRACE_I("read directory: begin");
    BOOL ret = FALSE;
    if (!readDirectory || ReadDirectory(parent, isRefresh))
        ret = TRUE;
    else
    {
        if (Is(ptDisk) || Is(ptZIPArchive))
            DetachDirectory(this); // something went wrong
    }
    //TRACE_I("read directory: begin");

    if (refreshListBox)
    {
        // find the item that should be selected
        int suggestedFocusIndex = -1;
        int suggestedFocusIndexIgnCase = -1;
        if (suggestedFocusName != NULL)
        {
            // One wide comparison. The wide confirmation that used to be ANDed on
            // here existed to stop a collided CP_ACP mirror matching the wrong row; with
            // CFileData::Name wide (P1.3 deleted NameW) there is no narrow mirror to collide, and
            // EffectiveItemNameW's narrow+wide pair no longer has a second half to reconcile.
            int i;
            for (i = 0; i < Dirs->Count; i++)
            {
                if (StrICmpW(Dirs->At(i).Name, suggestedFocusName) == 0)
                {
                    if (suggestedFocusIndexIgnCase == -1)
                        suggestedFocusIndexIgnCase = i;
                    if (wcscmp(Dirs->At(i).Name, suggestedFocusName) == 0)
                    {
                        suggestedFocusIndex = i;
                        break; // found the exact requested name
                    }
                }
            }
            if (suggestedFocusIndex == -1) // search among files as well (e.g., when returning from a ZIP archive)
            {
                for (i = 0; i < Files->Count; i++)
                {
                    if (StrICmpW(Files->At(i).Name, suggestedFocusName) == 0)
                    {
                        if (suggestedFocusIndexIgnCase == -1)
                            suggestedFocusIndexIgnCase = i + Dirs->Count;
                        if (wcscmp(Files->At(i).Name, suggestedFocusName) == 0)
                        {
                            suggestedFocusIndex = i + Dirs->Count;
                            break; // found the exact requested name
                        }
                    }
                }
            }
            // if the exact requested name was not found, use the name matching aside from case (if any)
            if (suggestedFocusIndex == -1)
                suggestedFocusIndex = suggestedFocusIndexIgnCase;
        }

        //TRACE_I("refresh listbox: begin");
        RefreshListBox(0, suggestedTopIndex, suggestedFocusIndex, TRUE, !isRefresh);
        //TRACE_I("refresh listbox: end");
    }

    DirectoryLine->InvalidateIfNeeded();
    //TRACE_I("common refresh: end");
    return ret;
}
