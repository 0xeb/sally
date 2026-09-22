// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "filesbox.h"
#include "mainwnd.h"
#include "stswnd.h"
#include "dialogs.h"
#include "shellib.h"
#include "pack.h"
#include "drivelst.h"
#include "snooper.h"
#include "zip.h"
#include "common/IFileSystem.h"
#include "common/DiagnosticTextEncoding.h"
#include "common/fsutil.h"
#include "shiconov.h"
#include "common/widepath.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/unicode/AnsiFallbackPolicy.h"
#include "common/IEnvironment.h"
#include "common/text/CaseFolding.h"

//
// ****************************************************************************
// CFilesWindow
//

int DeltaForTotalCount(int total)
{
    int delta = total / 10;
    if (delta < 1)
        delta = 1;
    else if (delta > 10000)
        delta = 10000;
    return delta;
}

static IFileSystem* GetPanelFileSystem()
{
    return gFileSystem != NULL ? gFileSystem : GetWin32FileSystem();
}

#ifndef _WIN64

BOOL AddWin64RedirectedDir(const wchar_t* path, CFilesArray* dirs, WIN32_FIND_DATAW* fileData,
                           int* index, BOOL* dirWithSameNameExists); // is further in this module

#endif // _WIN64

#ifndef IO_REPARSE_TAG_FILE_PLACEHOLDER
#define IO_REPARSE_TAG_FILE_PLACEHOLDER (0x80000015L) // winnt
#endif                                                // IO_REPARSE_TAG_FILE_PLACEHOLDER

// taken from: http://msdn.microsoft.com/en-us/library/windows/desktop/dn323738%28v=vs.85%29.aspx
BOOL IsFilePlaceholder(WIN32_FIND_DATA const* findData)
{
    return (findData->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           (findData->dwReserved0 == IO_REPARSE_TAG_FILE_PLACEHOLDER);
}

// Wide version for Unicode enumeration
BOOL IsFilePlaceholderW(WIN32_FIND_DATAW const* findData)
{
    return (findData->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           (findData->dwReserved0 == IO_REPARSE_TAG_FILE_PLACEHOLDER);
}

BOOL CFilesWindow::ReadDirectory(HWND parent, BOOL isRefresh)
{
    CALL_STACK_MESSAGE1("CFilesWindow::ReadDirectory()");

    //  TRACE_I("ReadDirectory: begin");

    //  MainWindow->ReleaseMenuNew();  // in case of it's about this directory
    HiddenDirsFilesReason = 0;
    HiddenDirsCount = HiddenFilesCount = 0;

    CutToClipChanged = FALSE; // forget cut-to-clip flags by this operation

    FocusFirstNewItem = FALSE;
    UseSystemIcons = FALSE;
    UseThumbnails = FALSE;
    Files->DestroyMembers();
    Dirs->DestroyMembers();
    VisibleItemsArray.InvalidateArr();
    VisibleItemsArraySurround.InvalidateArr();
    SelectedCount = 0;
    NeedRefreshAfterIconsReading = FALSE; // refresh would make no sense now (if needed, it will be set again during icon reading)
    NumberOfItemsInCurDir = 0;
    InactWinOptimizedReading = FALSE;

    // icon-cache cleanup
    SleepIconCacheThread();
    IconCache->Release();
    EndOfIconReadingTime = GetTickCount() - 10000;
    StopThumbnailLoading = FALSE; // icon-cache is cleaned, the period of impossibility of using data about "thumbnail-loaders" in icon-cache ends

    TemporarilySimpleIcons = FALSE;

    if (ColumnsTemplateIsForDisk != Is(ptDisk))
        BuildColumnsTemplate();                      // it's necessary to build template again when panel type changes
    CopyColumnsTemplateToColumns();                  // fetching standard columns from cache
    DeleteColumnsWithoutData();                      // removing columns for which we don't have data (empty values would be shown in them)
    GetPluginIconIndex = InternalGetPluginIconIndex; // setting standard callback (just returns zero)

    if (Is(ptDisk))
    {
        // setting icon size for IconCache
        CIconSizeEnum iconSize = GetIconSizeForCurrentViewMode();
        IconCache->SetIconSize(iconSize);

        BOOL readThumbnails = (GetViewMode() == vmThumbnails);

        CALL_STACK_MESSAGE1("CFilesWindow::ReadDirectory::disk1");
        // choosing plugins which can load thumbnails (for optimization)
        TIndirectArray<CPluginData> thumbLoaderPlugins(10, 10, dtNoDelete);
        TIndirectArray<CPluginData> foundThumbLoaderPlugins(10, 10, dtNoDelete); // the array for plugins which can load thumbnails for the current file
        if (readThumbnails)
        {
            if (thumbLoaderPlugins.IsGood())
                Plugins.AddThumbLoaderPlugins(thumbLoaderPlugins);
            if (!thumbLoaderPlugins.IsGood() ||
                !foundThumbLoaderPlugins.IsGood() || // error (not enough memory?)
                thumbLoaderPlugins.Count == 0)       // or we do not have any plugin which can load thumbnails -> we will not load thumbnails
            {
                if (!thumbLoaderPlugins.IsGood())
                    thumbLoaderPlugins.ResetState();
                if (!foundThumbLoaderPlugins.IsGood())
                    foundThumbLoaderPlugins.ResetState();
                readThumbnails = FALSE;
            }
        }
        UseThumbnails = readThumbnails;

        gEnvironment->SetCurrentDirectory(GetPathW()); // so that it works better

#ifndef _WIN64
        BOOL isWindows64BitDir = Windows64Bit && !WindowsDirectory.empty() &&
                                 IsTheSamePath(GetPathW(), WindowsDirectory.c_str());
#endif // _WIN64

        RefreshDiskFreeSpace(FALSE);

        Files->SetDeleteData(TRUE);
        Dirs->SetDeleteData(TRUE);

        if (WaitForESCReleaseBeforeTestingESC) // waiting for ESC release (so that listing is not interrupted
                                               // immediately - this ESC probably ended modal dialog/messagebox)
        {
            WaitForESCRelease();
            WaitForESCReleaseBeforeTestingESC = FALSE; // another waiting makes no sense
        }

        GetAsyncKeyState(VK_ESCAPE); // init GetAsyncKeyState - see help

        const std::wstring rootPathW = GetRootPath(GetPathW());
        BOOL isRootPath = (wcslen(GetPathW()) <= rootPathW.length());

        //--- getting drive type (we will not bother network drives with getting shares)
        // Same CP_ACP-mirror defect as the PrepareSearchW fix two lines below:
        // asked of the '?'-mangled mirror, this can return the wrong drive type for a
        // non-ASCII panel path, misrouting the removable/remote/CD-ROM icon selection.
        UINT drvType = MyGetDriveTypeW(GetPathW());
        BOOL testShares = drvType != DRIVE_REMOTE;
        if (testShares)
        {
            // Selects which shares are candidates for this directory. Given the
            // CP_ACP mirror it selects them for a path that may not exist, and every share
            // overlay in the panel is then decided against the wrong candidate set.
            Shares.PrepareSearchW(GetPathW());
        }
        switch (drvType)
        {
        case DRIVE_REMOVABLE:
        {
            BOOL isDriveFloppy = FALSE; // floppies have their own configuration beside other removable drives
            int drv = towupper(rootPathW.empty() ? L'\0' : rootPathW[0]) - L'A' + 1;
            if (drv >= 1 && drv <= 26) // doing "range-check" for sure
            {
                DWORD medium = GetDriveFormFactor(drv);
                if (medium == 350 || medium == 525 || medium == 800 || medium == 1)
                    isDriveFloppy = TRUE;
            }
            UseSystemIcons = isDriveFloppy ? !Configuration.DrvSpecFloppySimple : !Configuration.DrvSpecRemovableSimple;
            break;
        }

        case DRIVE_REMOTE:
        {
            UseSystemIcons = !Configuration.DrvSpecRemoteSimple;
            break;
        }

        case DRIVE_CDROM:
        {
            UseSystemIcons = !Configuration.DrvSpecCDROMSimple;
            break;
        }

        default: // case DRIVE_FIXED:   // not just fixed, but also the others (RAM DISK, etc.)
        {
            UseSystemIcons = !Configuration.DrvSpecFixedSimple;
            break;
        }
        }

        if (*GetPathW() == L'\0')
        {
            SetCurrentDirectoryToSystem();
            DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
            //      TRACE_I("ReadDirectory: end");
            return FALSE; // empty string on input
        }
        std::wstring fileNameW = sally::unicode::BuildPanelChildPathW(GetPathW(), L"*");
        //--- preparing for reading icons
        if (UseSystemIcons)
        {
            IconCacheValid = FALSE;
            MSG msg; // we must destroy possible WM_USER_ICONREADING_END which would set IconCacheValid = TRUE
            while (PeekMessageW(&msg, HWindow, WM_USER_ICONREADING_END, WM_USER_ICONREADING_END, PM_REMOVE))
                ;

            int i;
            for (i = 0; i < Associations.Count; i++)
            {
                if (Associations[i].GetIndex(iconSize) == -3)
                    Associations[i].SetIndex(-1, iconSize); // removing the flag "loaded icon"
            }
        }
        else
        {
            if (UseThumbnails)
            {
                IconCacheValid = FALSE;
                MSG msg; // we must destroy possible WM_USER_ICONREADING_END which would set IconCacheValid = TRUE
                while (PeekMessageW(&msg, HWindow, WM_USER_ICONREADING_END, WM_USER_ICONREADING_END, PM_REMOVE))
                    ;
            }
        }
        //--- reading directory content
        BOOL upDir;
        BOOL UNCRootUpDir = FALSE;
        if (GetPathW()[0] == '\\' && GetPathW()[1] == '\\')
        {
            if (GetPathW()[2] == '.' && GetPathW()[3] == '\\' && GetPathW()[4] != 0 && GetPathW()[5] == ':') // "\\.\C:\" type path
            {
                upDir = wcslen(GetPathW()) > 7;
            }
            else // UNC path
            {
                const wchar_t* s2 = GetPathW() + 2;
                while (*s2 != 0 && *s2 != '\\')
                    s2++;
                if (*s2 != 0)
                    s2++;
                while (*s2 != 0 && *s2 != '\\')
                    s2++;
                upDir = (*s2 == '\\' && *(s2 + 1) != 0);
                if (!upDir && Plugins.GetFirstNethoodPluginFSName())
                {
                    upDir = TRUE;
                    UNCRootUpDir = TRUE;
                }
            }
        }
        else
            upDir = wcslen(GetPathW()) > 3;

        CALL_STACK_MESSAGE1("CFilesWindow::ReadDirectory::disk2");

        CIconData iconData;
        iconData.FSFileData = NULL;
        iconData.SetReadingDone(0); // just for the form
        BOOL addtoIconCache;
        CFileData file;
        // inicialization of structure members which will not be changed later
        // the wide-name mirror field was retired at P1.3 - CFileData::Name
        // is the wide name now, there is no separate narrow/wide pair to initialize here.
        file.PluginData = -1; // -1 just like that, ignored
        file.Selected = 0;
        file.SizeValid = 0;
        file.Dirty = 0; // unnecessary, just for the form
        file.CutToClip = 0;
        file.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
        file.IconOverlayDone = 0;
        int len;
#ifndef _WIN64
        int foundWin64RedirectedDirs = 0;
        BOOL isWin64RedirectedDir = FALSE;
#endif // _WIN64

    _TRY_AGAIN:

        // after 2000 ms we will show a window with a cancel prompt
        const std::wstring waitMessage = FormatStrW(LoadStrW(IDS_READINGPATHESC), GetPathW());
        CreateSafeWaitWindow(waitMessage.c_str(), NULL, 2000, TRUE, MainWindow->HWindow);

        DWORD lastEscCheckTime;
        //lastEscCheckTime = GetTickCount() - 200;  // the first ESC will go immediately
        lastEscCheckTime = GetTickCount(); // the first ESC will go after 200 ms -- it's a protection
                                           // against the cancel prompt for listing after the user
                                           // has closed a dialog (e.g. Files/Security/*) by Esc and
                                           // in the panel there was a network drive (and during the
                                           // opened dialog the user switched to Salamander and back,
                                           // so that there was a refresh of the directory)

        BOOL isUpDir = FALSE;
        WIN32_FIND_DATAW fileDataW;
        const wchar_t* s = NULL;
        wchar_t* st = NULL;
        HANDLE search;
        IFileSystem* fileSystem = GetPanelFileSystem();
        search = SalFindFirstFileHW(fileNameW.c_str(), &fileDataW);
        if (search == INVALID_HANDLE_VALUE)
        {
            DWORD err = GetLastError();
            DestroySafeWaitWindow();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_NO_MORE_FILES)
            {
                if (!upDir)
                {
                    StatusLine->SetText(LoadStrW(IDS_NOFILESFOUND)); // wide - see files_window_windowproc.cpp's WM_USER_SELCHANGED fix
                    SetCurrentDirectoryToSystem();
                    DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                    if (UseSystemIcons || UseThumbnails) // even though we don't have any icons, we need to start loading them (just to set IconCacheValid = TRUE)
                    {
                        if (IconCache->Count > 1)
                            IconCache->SortArray(0, IconCache->Count - 1, NULL);
                        WakeupIconCacheThread(); // start loading icons
                    }
                    //          TRACE_I("ReadDirectory: end");
                    return TRUE;
                }
            }
            else
            {
                SetCurrentDirectoryToSystem();
                RefreshListBox(0, -1, -1, FALSE, FALSE);
                DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                DirectoryLine->InvalidateIfNeeded();
                IdleRefreshStates = TRUE; // we will force checking of states of variables at the next Idle
                StatusLine->SetText(L"");
                UpdateWindow(HWindow);

                BOOL showErr = TRUE;
                if (err == ERROR_INVALID_PARAMETER || err == ERROR_NOT_READY)
                {
                    DWORD attrs = fileSystem->GetFileAttributes(GetPathW());
                    if (attrs != INVALID_FILE_ATTRIBUTES &&
                        (attrs & FILE_ATTRIBUTE_DIRECTORY) &&
                        (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
                    {
                        showErr = FALSE;
                        std::wstring drive;
                        UINT drvType2;
                        if (GetPathW()[0] == '\\' && GetPathW()[1] == '\\')
                        {
                            drvType2 = DRIVE_REMOTE;
                            drive = GetRootPath(GetPathW());
                            SalPathRemoveBackslashW(drive);
                        }
                        else
                        {
                            drive.assign(1, GetPathW()[0]);
                            // Same shape as line 169's fix in this function -
                            // asked of the '?'-mangled mirror, MyGetDriveType can misreport the
                            // drive type for a non-ASCII panel path, sending this reparse-point
                            // recovery down the wrong branch (DRIVE_REMOTE vs local).
                            drvType2 = MyGetDriveTypeW(GetPathW());
                        }
                        if (drvType2 != DRIVE_REMOTE)
                        {
                            std::wstring currentReparsePoint;
                            GetCurrentLocalReparsePointW(GetPathW(), currentReparsePoint);
                            CheckPathRootWithRetryMsgBox = currentReparsePoint;
                            if (currentReparsePoint.length() > 3)
                            {
                                drive = currentReparsePoint;
                                SalPathRemoveBackslashW(drive);
                            }
                        }
                        else
                            CheckPathRootWithRetryMsgBox = GetRootPath(GetPathW());
                        const std::wstring driveError = FormatStrW(LoadStrW(IDS_NODISKINDRIVE), drive.c_str());
                        int msgboxRes = (int)CDriveSelectErrDlg(parent, driveError.c_str(), GetPathW()).Execute();
                        CheckPathRootWithRetryMsgBox.clear();
                        UpdateWindow(MainWindow->HWindow);
                        if (msgboxRes == IDRETRY)
                            goto _TRY_AGAIN;
                    }
                }
                if (isRefresh &&
                    (err == ERROR_ACCESS_DENIED || err == ERROR_PATH_NOT_FOUND ||
                     err == ERROR_BAD_PATHNAME || err == ERROR_FILE_NOT_FOUND))
                { // when deleting a path shown in the panel, these errors are shown, which we don't want, we just silently shorten the path to the first existing one (unfortunately it's not caught earlier, because the path exists for some time after its deletion, something in Windows just didn't work out again)
                    //          TRACE_IW(L"ReadDirectory(): silently ignoring FindFirstFile failure: " << GetErrorTextOwned(err).c_str());
                    showErr = FALSE;
                }
                if (showErr)
                    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), GetErrorTextOwned(err).c_str());
                //        TRACE_I("ReadDirectory: end");
                return FALSE;
            }
        }
        else
        {
            BOOL testFindNextErr;
            testFindNextErr = TRUE;
            do
            {
                NumberOfItemsInCurDir++;

                // test ESC - doesn't user want to interrupt reading?
                if (GetTickCount() - lastEscCheckTime >= 200) // 5 times per second
                {
                    if (UserWantsToCancelSafeWaitWindow())
                    {
                        MSG msg; // remove buffered ESC
                        while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
                            ;

                        SetCurrentDirectoryToSystem();
                        RefreshListBox(0, -1, -1, FALSE, FALSE);

                        PromptResult resBut = gPrompter->AskYesNoCancel(LoadStrW(IDS_QUESTION), LoadStrW(IDS_READDIRTERMINATED));
                        UpdateWindow(MainWindow->HWindow);

                        WaitForESCRelease();
                        WaitForESCReleaseBeforeTestingESC = FALSE; // another waiting makes no sense
                        GetAsyncKeyState(VK_ESCAPE);               // new init GetAsyncKeyState - see help

                        if (resBut.type == PromptResult::kYes)
                        {
                            testFindNextErr = FALSE;
                            break; // finish reading
                        }
                        else
                        {
                            if (resBut.type == PromptResult::kNo)
                            {
                                if (GetMonitorChanges()) // need to suppress monitoring of changes (autorefresh)
                                {
                                    DetachDirectory((CFilesWindow*)this);
                                    SetMonitorChanges(FALSE); // the changes won't be monitored anymore
                                }

                                SetSuppressAutoRefresh(TRUE);
                            }
                        }
                    }
                    lastEscCheckTime = GetTickCount();
                }

                // This used to build a lossy CP_ACP projection of the real
                // wide name (via SalAnsiName, since deleted) to feed the now-retired
                // CFileData::NameW pair. NameW went away at P1.3 - CFileData::Name is the
                // wide name now, so there is nothing left to project: read the real name
                // straight from fileDataW, never through a best-fit narrow round-trip that
                // could silently misfile a non-ASCII name into the wrong hidden/filter/name
                // bucket.
                st = fileDataW.cFileName;
                len = (int)wcslen(st);
                BOOL isDir;
                isDir = (fileDataW.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                isUpDir = (len == 2 && *st == '.' && *(st + 1) == '.');
                //--- handling of "." and ".." and hidden/system files (file "." is not ignored, FLAME spyware uses these files, so let them be visible)
                if (len == 0 || len == 1 && *st == '.' && isDir ||
                    ((isRootPath || !isDir ||
                      CQuadWord(fileDataW.ftLastWriteTime.dwLowDateTime, // date on ".." is older or equal to 1.1.1980, we better read it later "properly"
                                fileDataW.ftLastWriteTime.dwHighDateTime) <= CQuadWord(2148603904, 27846551)) &&
                     isUpDir))
                    continue;

                if (Configuration.NotHiddenSystemFiles &&
                    !IsFilePlaceholderW(&fileDataW) && // placeholder is hidden, but Explorer shows it normally, so we will show it normally too
                    (fileDataW.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) &&
                    (len != 2 || *st != '.' || *(st + 1) != '.'))
                { // skip hidden/system file/directory
                    if (isDir)
                        HiddenDirsCount++;
                    else
                        HiddenFilesCount++;
                    HiddenDirsFilesReason |= HIDDEN_REASON_ATTRIBUTE;
                    continue;
                }
                //--- applying filter to files
                // wide: matching against ansiFileName (the CP_ACP best-fit mirror,
                // possibly '?'-mangled) let a filter mask wrongly hide/show non-ASCII names, since
                // '?' is itself a mask wildcard. AgreeMasks self-computes the extension the same
                // way when passed NULL, so match the true wide name directly.
                if (FilterEnabled && !isDir)
                {
                    if (!Filter.AgreeMasks(fileDataW.cFileName, NULL))
                    {
                        HiddenFilesCount++;
                        HiddenDirsFilesReason |= HIDDEN_REASON_FILTER;
                        continue;
                    }
                }

                //--- if the name is occupied in the array HiddenNames, we will discard it
                if (HiddenNames.Contains(isDir, st))
                {
                    if (isDir)
                        HiddenDirsCount++;
                    else
                        HiddenFilesCount++;
                    HiddenDirsFilesReason |= HIDDEN_REASON_HIDECMD;
                    continue;
                }

            ADD_ITEM: // to add ".."

                //--- name
                file.Name = (wchar_t*)malloc((len + 1) * sizeof(wchar_t)); // allocation
                if (file.Name == NULL)
                {
                    if (search != NULL)
                    {
                        DestroySafeWaitWindow();
                        SalLPFindClose(search);
                    }
                    TRACE_E(LOW_MEMORY);
                    SetCurrentDirectoryToSystem();
                    Files->DestroyMembers();
                    Dirs->DestroyMembers();
                    VisibleItemsArray.InvalidateArr();
                    VisibleItemsArraySurround.InvalidateArr();
                    DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                    //          TRACE_I("ReadDirectory: end");
                    return FALSE;
                }
                memmove(file.Name, st, (len + 1) * sizeof(wchar_t)); // copy of text
                file.NameLen = len;

                //--- extension
                if (!Configuration.SortDirsByExt && (fileDataW.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) // this is ptDisk
                {
                    file.Ext = file.Name + file.NameLen; // directories have no extension
                }
                else
                {
                    s = st + len;
                    while (--s >= st && *s != '.')
                        ;
                    if (s >= st)
                        file.Ext = file.Name + (s - st + 1); // ".cvspass" in Windows is an extension ...
                                                             //          if (s > st) file.Ext = file.Name + (s - st + 1);
                    else
                        file.Ext = file.Name + file.NameLen;
                }
                //--- others
                file.Size = CQuadWord(fileDataW.nFileSizeLow, fileDataW.nFileSizeHigh);
                file.Attr = fileDataW.dwFileAttributes;
                file.LastWrite = fileDataW.ftLastWriteTime;
                // placeholder is hidden, but Explorer shows it normally, so we will show it normally too (without ghosted icon)
                file.Hidden = (file.Attr & FILE_ATTRIBUTE_HIDDEN) && !IsFilePlaceholderW(&fileDataW) ? 1 : 0;

                file.IsOffline = !isUpDir && (file.Attr & FILE_ATTRIBUTE_OFFLINE) ? 1 : 0;
                if (testShares && (file.Attr & FILE_ATTRIBUTE_DIRECTORY)) // this is ptDisk
                {
                    // Was a UseWideName()/NameW ternary whose comment
                    // argued the narrow arm was faithful. Both members were deleted at
                    // P1.3 ("NameW is GONE", spl_com.h) - Name is the wide name now, so
                    // there is one arm and no choice to make.
                    file.Shared = Shares.SearchW(file.Name);
                }
                else
                    file.Shared = 0;

                if (fileDataW.cAlternateFileName[0] != 0)
                {
                    // The DOS 8.3 alternate name is already a real wide
                    // string from the OS (and is pure ASCII by DOS 8.3 convention), so
                    // there is nothing to convert - duplicate it directly.
                    int l = (int)wcslen(fileDataW.cAlternateFileName) + 1;
                    file.DosName = (wchar_t*)malloc(l * sizeof(wchar_t));
                    if (file.DosName == NULL)
                    {
                        free(file.Name);
                        if (search != NULL)
                        {
                            DestroySafeWaitWindow();
                            SalLPFindClose(search);
                        }
                        TRACE_E(LOW_MEMORY);
                        SetCurrentDirectoryToSystem();
                        Files->DestroyMembers();
                        Dirs->DestroyMembers();
                        VisibleItemsArray.InvalidateArr();
                        VisibleItemsArraySurround.InvalidateArr();
                        DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                        //            TRACE_I("ReadDirectory: end");
                        return FALSE;
                    }
                    memmove(file.DosName, fileDataW.cAlternateFileName, l * sizeof(wchar_t));
                }
                else
                    file.DosName = NULL;
                if (file.Attr & FILE_ATTRIBUTE_DIRECTORY) // this is ptDisk
                {
                    file.Association = 0;
                    file.Archive = 0;
#ifndef _WIN64
                    file.IsLink = (isWin64RedirectedDir || (fileDataW.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || // CAUTION: pseudo-directory must have IsLink set, otherwise ContainsWin64RedirectedDir must be changed
                                   isWindows64BitDir && file.NameLen == 8 && StrICmpW(file.Name, L"system32") == 0)
                                      ? 1
                                      : 0; // system32 directory in 32-bit Salamander is link to SysWOW64 + win64 redirected-dir + volume mount point or junction point = show directory with link overlay
#else                                      // _WIN64
                    file.IsLink = (fileDataW.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0; // volume mount point or junction point = show directory with link overlay
#endif                                     // _WIN64
                    if (len == 2 && *st == '.' && *(st + 1) == '.')
                    { // handling ".."
                        if (GetPathW()[3] != 0)
                            Dirs->Insert(0, file); // except of root...
                        else
                        {
                            if (file.Name != NULL)
                                free(file.Name);
                            if (file.DosName != NULL)
                                free(file.DosName);
                        }
                        addtoIconCache = FALSE;
                    }
                    else
                    {
                        Dirs->Add(file);
#ifndef _WIN64
                        addtoIconCache = isWin64RedirectedDir ? FALSE : TRUE;
#else  // _WIN64
                        addtoIconCache = TRUE;
#endif // _WIN64
                    }
                    if (!Dirs->IsGood())
                    {
                        Dirs->ResetState();
                        if (search != NULL)
                        {
                            DestroySafeWaitWindow();
                            SalLPFindClose(search);
                        }
                        SetCurrentDirectoryToSystem();
                        Files->DestroyMembers();
                        Dirs->DestroyMembers();
                        VisibleItemsArray.InvalidateArr();
                        VisibleItemsArraySurround.InvalidateArr();
                        DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                        //            TRACE_I("ReadDirectory: end");
                        return FALSE;
                    }
                }
                else
                {
                    if (s >= st) // an extension exists
                    {
                        // sally::text::Fold UPPERCASES - it is a comparison key, not a
                        // display form - and CAssociations stores its extensions folded
                        // the same way, so the lookups below are ordinal-correct. The
                        // literals must be folded too: spelled in lower case they never
                        // matched, which cost .lnk/.pif/.url their link overlay and cost
                        // .scr/.pif/.lnk their icon-cache handling.
                        const std::wstring foldedExtension = sally::text::Fold(s + 1);
                        const wchar_t* extension = foldedExtension.c_str();

                        if (fileDataW.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                            file.IsLink = 1; // if the file is reparse-point (maybe it's not possible at all) = show it with link overlay
                        else
                        {
                            file.IsLink = (wcscmp(extension, L"LNK") == 0 ||
                                           wcscmp(extension, L"PIF") == 0 ||
                                           wcscmp(extension, L"URL") == 0)
                                              ? 1
                                              : 0;
                        }

                        if (PackerFormatConfig.PackIsArchive(file.Name, file.NameLen)) // is it an archive which we can process?
                        {
                            file.Association = 1;
                            file.Archive = 1;
                            addtoIconCache = FALSE;
                        }
                        else
                        {
                            file.Association = Associations.IsAssociated(extension, addtoIconCache, iconSize);
                            file.Archive = 0;
                            if (wcscmp(extension, L"SCR") == 0 || // few exceptions
                                wcscmp(extension, L"PIF") == 0)
                            {
                                addtoIconCache = TRUE;
                            }
                            else
                            {
                                if (wcscmp(extension, L"LNK") == 0) // icons via link
                                {
                                    std::wstring linkTargetName(file.Name);
                                    const size_t extensionPos = linkTargetName.find_last_of(L'.');
                                    if (extensionPos != std::wstring::npos) // ".cvspass" in Windows is an extesion
                                    {
                                        linkTargetName.resize(extensionPos);
                                        if (PackerFormatConfig.PackIsArchive(linkTargetName.c_str())) // is it a link to archive which we can process?
                                        {
                                            file.Association = 1;
                                            file.Archive = 1;
                                            addtoIconCache = FALSE;
                                        }
                                    }
                                    addtoIconCache = TRUE;
                                }
                            }
                        }
                    }
                    else
                    {
                        file.Association = 0;
                        file.Archive = 0;
                        file.IsLink = (fileDataW.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0; // if the file is reparse-point (maybe it's not possible at all) = show it with link overlay
                        addtoIconCache = FALSE;
                    }

                    Files->Add(file);
                    if (!Files->IsGood())
                    {
                        Files->ResetState();
                        if (search != NULL)
                        {
                            DestroySafeWaitWindow();
                            SalLPFindClose(search);
                        }
                        SetCurrentDirectoryToSystem();
                        Files->DestroyMembers();
                        Dirs->DestroyMembers();
                        VisibleItemsArray.InvalidateArr();
                        VisibleItemsArraySurround.InvalidateArr();
                        DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                        //            TRACE_I("ReadDirectory: end");
                        return FALSE;
                    }
                }

                // at the file, we will check if it's necessary to load its thumbnail
                if (readThumbnails &&                              // thumbnail should be loaded
                    (file.Attr & FILE_ATTRIBUTE_DIRECTORY) == 0 && // (it is ptDisk, so using FILE_ATTRIBUTE_DIRECTORY is o.k.)
                    file.Archive == 0)                             // archive icon is preferred before thumbnail
                {
                    foundThumbLoaderPlugins.DestroyMembers();
                    int i;
                    for (i = 0; i < thumbLoaderPlugins.Count; i++)
                    {
                        CPluginData* p = thumbLoaderPlugins[i];
                        // wide: file.Name/Ext are the same lossy CP_ACP mirror
                        // fixed for FilterEnabled above - match the true wide name instead.
                        if (p->ThumbnailMasks.AgreeMasks(fileDataW.cFileName, NULL) &&
                            !p->ThumbnailMasksDisabled) // its unload/remove is not in progress
                        {
                            if (!p->GetLoaded()) // plugin needs to be loaded (possible change of mask for "thumbnail loader")
                            {
                                //                RefreshListBox(0, -1, -1, FALSE, FALSE); // replaced with ListBox->SetItemsCound + WM_USER_UPDATEPANEL, because it was blinking e.g. when adding the first *.doc file to a directory with images (Eroiica is loaded (for thumbnail *.doc))

                                // displaying "PictureView is not registered" dialog may occur -> in that case it's necessary
                                // to refresh listbox (otherwise we don't do any refresh, so that it doesn't blink with the panel)
                                // we protect listbox against errors caused by request for refresh (data is just being read from disk)
                                ListBox->SetItemsCount(0, 0, 0, TRUE); // TRUE - we will disable setting scrollbar
                                // If WM_USER_UPDATEPANEL is delivered, the panel will be redrawn and scrollbar will be set.
                                // Message loop can deliver it when message box (or dialog) is created.
                                // Otherwise the panel will behave as unchanged and the message will be removed from queue.
                                PostMessage(HWindow, WM_USER_UPDATEPANEL, 0, 0);

                                BOOL cont = FALSE;
                                if (p->InitDLL(HWindow, FALSE, TRUE, FALSE) &&  // plugin loaded successfully
                                    p->ThumbnailMasks.GetMasksString()[0] != 0) // plugin is still "thumbnail loader"
                                {
                                    if (!p->ThumbnailMasks.AgreeMasks(fileDataW.cFileName, NULL) || // it can't do thumbnail for this file anymore
                                        p->ThumbnailMasksDisabled)                            // its unload/remove is in progress
                                    {
                                        cont = TRUE;
                                    }
                                }
                                else // can't load -> we will remove it from the list of probed plugins (prevent from repeating error messages)
                                {
                                    TRACE_I("Unable to use plugin " << sally::diagnostic::EncodeAcpLossy(p->Name) << " as thumbnail loader.");
                                    thumbLoaderPlugins.Delete(i);
                                    readThumbnails = thumbLoaderPlugins.Count > 0;
                                    if (!thumbLoaderPlugins.IsGood())
                                        thumbLoaderPlugins.ResetState();
                                    else
                                        i--;
                                    cont = TRUE; // let's try our luck with another plugin
                                }

                                // cleanup message-queue from buffered WM_USER_UPDATEPANEL
                                MSG msg2;
                                PeekMessageW(&msg2, HWindow, WM_USER_UPDATEPANEL, WM_USER_UPDATEPANEL, PM_REMOVE);

                                if (cont)
                                    continue;
                            }

                            foundThumbLoaderPlugins.Add(p);
                        }
                    }
                    if (foundThumbLoaderPlugins.IsGood())
                    {
                        if (foundThumbLoaderPlugins.Count > 0)
                        {
                            // NameAndData is wchar_t*, but 'len' is a WCHAR count
                            // (wcslen, line ~470) and everything past the name (nameSize onward -
                            // CQuadWord/FILETIME/void* pointers) is raw packed bytes, not wchar_t-
                            // granular data. The old char-count math allocated/copied half the name's
                            // bytes and, worse, used a BYTE offset (nameSize) directly as a wchar_t*
                            // pointer offset (silently doubling it). Do all sizing/offsetting through
                            // an explicit BYTE count/pointer so nothing is silently rescaled.
                            int lenBytes = len * (int)sizeof(wchar_t);
                            int size = lenBytes + 4;
                            size -= (size & 0x3); // size % 4 (alignment per four bytes)
                            int nameSize = size;
                            size += sizeof(CQuadWord) + sizeof(FILETIME);
                            size += (foundThumbLoaderPlugins.Count + 1) * sizeof(void*); // space for pointers to plugin interfaces + NULL at the end
                            iconData.NameAndData = (wchar_t*)malloc(size);
                            if (iconData.NameAndData != NULL)
                            {
                                BYTE* raw = (BYTE*)iconData.NameAndData;
                                memcpy(raw, file.Name, lenBytes);
                                memset(raw + lenBytes, 0, nameSize - lenBytes); // end of name is zeroed
                                // size is added + time of last write to file
                                *(CQuadWord*)(raw + nameSize) = file.Size;
                                *(FILETIME*)(raw + nameSize + sizeof(CQuadWord)) = file.LastWrite;
                                // add list of pointers to encapsulation of plugin interfaces for getting thumbnails
                                void** ifaces = (void**)(raw + nameSize + sizeof(CQuadWord) + sizeof(FILETIME));
                                int i2;
                                for (i2 = 0; i2 < foundThumbLoaderPlugins.Count; i2++)
                                {
                                    *ifaces++ = foundThumbLoaderPlugins[i2]->GetPluginInterfaceForThumbLoader();
                                }
                                *ifaces = NULL;      // the end of list of plugin interfaces
                                iconData.SetFlag(4); // so far no unread thumbnail

                                // we have to allocate space for thumbnail, because it can't be done in the thread
                                iconData.SetIndex(IconCache->AllocThumbnail());

                                if (iconData.GetIndex() != -1)
                                {
                                    IconCache->Add(iconData);
                                    if (!IconCache->IsGood())
                                    {
                                        free(iconData.NameAndData);
                                        IconCache->ResetState();
                                    }
                                    else
                                        addtoIconCache = FALSE; // it's a thumbnail, it can't be an icon at the same time
                                }
                                else
                                    free(iconData.NameAndData);
                            }
                        }
                    }
                    else
                        foundThumbLoaderPlugins.ResetState();
                }

                // adding directory to IconCache -> we need to load icon
                if (UseSystemIcons && addtoIconCache)
                {
                    // NameAndData is wchar_t*; 'len' is a WCHAR count (wcslen, line
                    // ~470), so the DWORD-alignment and buffer size must be computed in BYTES - the
                    // old char-count math allocated/copied half the name's actual bytes.
                    int lenBytes = len * (int)sizeof(wchar_t);
                    int size = lenBytes + 4;
                    size -= (size & 0x3); // size % 4 (alignment per four bytes)
                    iconData.NameAndData = (wchar_t*)malloc(size);
                    if (iconData.NameAndData != NULL)
                    {
                        memmove(iconData.NameAndData, file.Name, lenBytes);
                        memset(iconData.NameAndData + len, 0, size - lenBytes); // end of name is zeroed
                        iconData.SetFlag(0);                               // no not-loaded icon yet
                                                                           // need to allocate space for bitmaps, can't be done in thread
                        iconData.SetIndex(IconCache->AllocIcon(NULL, NULL));
                        if (iconData.GetIndex() != -1)
                        {
                            IconCache->Add(iconData);
                            if (!IconCache->IsGood())
                            {
                                free(iconData.NameAndData);
                                IconCache->ResetState();
                            }
                        }
                        else
                            free(iconData.NameAndData);
                    }
                }
                if (search == NULL)
                {
                    testFindNextErr = FALSE;
#ifndef _WIN64
                    isWin64RedirectedDir = FALSE;
#endif                     // _WIN64
                    break; // the second pass (adding ".." or win64 redirected-dir)
                }
            } while (SalLPFindNextFile(search, &fileDataW));
            DWORD err = GetLastError();

            if (search != NULL) // the first pass
            {
                DestroySafeWaitWindow();
                SalLPFindClose(search);
            }

            if (testFindNextErr && err != ERROR_NO_MORE_FILES)
            {
                SetCurrentDirectoryToSystem();
                RefreshListBox(0, -1, -1, FALSE, FALSE);

                // TODO: Use wide format string when available
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), (std::wstring(GetPathW()) + L": " + GetErrorTextOwned(err).c_str()).c_str());
            }
        }

        if (upDir && (Dirs->Count == 0 || wcscmp(Dirs->At(0).Name, L"..") != 0))
        {
            upDir = FALSE;
            std::wstring currentPathW = GetPathW();
            if (!UNCRootUpDir)
                search = SalFindFirstFileHW(currentPathW.c_str(), &fileDataW);
            else
                search = INVALID_HANDLE_VALUE;
            if (search == INVALID_HANDLE_VALUE)
            {
                fileDataW.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY; // this is ptDisk
                SYSTEMTIME ltNone;
                ltNone.wYear = 1602;
                ltNone.wMonth = 1;
                ltNone.wDay = 1;
                ltNone.wDayOfWeek = 2;
                ltNone.wHour = 0;
                ltNone.wMinute = 0;
                ltNone.wSecond = 0;
                ltNone.wMilliseconds = 0;
                FILETIME ft;
                SystemTimeToFileTime(&ltNone, &ft);
                LocalFileTimeToFileTime(&ft, &fileDataW.ftCreationTime);
                LocalFileTimeToFileTime(&ft, &fileDataW.ftLastAccessTime);
                LocalFileTimeToFileTime(&ft, &fileDataW.ftLastWriteTime);

                fileDataW.nFileSizeHigh = 0;
                fileDataW.nFileSizeLow = 0;
                fileDataW.dwReserved0 = fileDataW.dwReserved1 = 0;
            }
            else
                SalLPFindClose(search);
            search = NULL;                                               // the second/third pass
            fileDataW.dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;      // this is ptDisk
            fileDataW.dwFileAttributes &= ~FILE_ATTRIBUTE_REPARSE_POINT; // need to remove flag FILE_ATTRIBUTE_REPARSE_POINT, otherwise link overlay will be on ".."
            wcscpy(fileDataW.cFileName, L"..");
            fileDataW.cAlternateFileName[0] = 0;
            st = fileDataW.cFileName;
            len = 2;
            isUpDir = TRUE;
            goto ADD_ITEM;
        }

#ifndef _WIN64

    FIND_NEXT_WIN64_REDIRECTEDDIR:

        BOOL dirWithSameNameExists;
        if (foundWin64RedirectedDirs < 10 &&
            AddWin64RedirectedDir(GetPathW(), Dirs, &fileDataW, &foundWin64RedirectedDirs, &dirWithSameNameExists))
        {
            foundWin64RedirectedDirs++; // e.g. under system32 there can be 5, I've added some reserve to 10...

            if (Configuration.NotHiddenSystemFiles &&
                (fileDataW.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
            { // skip hidden directory
                if (!dirWithSameNameExists)
                {
                    HiddenDirsCount++;
                    HiddenDirsFilesReason |= HIDDEN_REASON_ATTRIBUTE;
                }
                goto FIND_NEXT_WIN64_REDIRECTEDDIR;
            }

            //--- if the name is occupied in the array HiddenNames, we will discard it
            if (HiddenNames.Contains(TRUE, fileDataW.cFileName))
            {
                if (!dirWithSameNameExists)
                {
                    HiddenDirsCount++;
                    HiddenDirsFilesReason |= HIDDEN_REASON_HIDECMD;
                }
                goto FIND_NEXT_WIN64_REDIRECTEDDIR;
            }

            isWin64RedirectedDir = TRUE;
            search = NULL; // the second/third pass...
            st = fileDataW.cFileName;
            len = (int)wcslen(st);
            isUpDir = FALSE;
            goto ADD_ITEM;
        }
        if (foundWin64RedirectedDirs >= 10)
            TRACE_E("CFilesWindow::ReadDirectory(): foundWin64RedirectedDirs >= 10 (there are more redirected-dirs?)");

#endif // _WIN64

        SetCurrentDirectoryToSystem();

        if (Files->Count + Dirs->Count == 0)
            StatusLine->SetText(LoadStrW(IDS_NOFILESFOUND)); // wide - see files_window_windowproc.cpp's WM_USER_SELCHANGED fix

        // sorting of Files and Dirs according to the current sorting method
        SortDirectory();

        if (UseSystemIcons || UseThumbnails)
        {
            if (IconCache->Count > 1)
                IconCache->SortArray(0, IconCache->Count - 1, NULL);
            WakeupIconCacheThread(); // start loading icons
        }
    }
    else
    {
        if (Is(ptZIPArchive))
        {
            RefreshDiskFreeSpace();

            if (PluginData.NotEmpty())
            {
                CSalamanderView view(this);
                PluginData.SetupView(this == MainWindow->LeftPanel, &view, GetZIPPath(),
                                     GetArchiveDir()->GetUpperDir(GetZIPPath()));
            }

            // setting of icon size for IconCache
            CIconSizeEnum iconSize = GetIconSizeForCurrentViewMode();
            IconCache->SetIconSize(iconSize);

            CFilesArray* ZIPFiles = GetArchiveDirFiles();
            CFilesArray* ZIPDirs = GetArchiveDirDirs();

            Files->SetDeleteData(FALSE); // only a shallow copy of data
            Dirs->SetDeleteData(FALSE);  // only a shallow copy of data

            if (ZIPFiles != NULL && ZIPDirs != NULL)
            {
                // see comment in case of ptPluginFS
                Files->SetDelta(DeltaForTotalCount(ZIPFiles->Count));
                Dirs->SetDelta(DeltaForTotalCount(ZIPDirs->Count));

                int i;
                for (i = 0; i < ZIPFiles->Count; i++)
                {
                    CFileData* f = &ZIPFiles->At(i);
                    if (Configuration.NotHiddenSystemFiles &&
                        (f->Hidden || // both Hidden and Attr are nulled if they are invalid -> tests fail
                         (f->Attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))))
                    { // skip hidden file/directory
                        HiddenFilesCount++;
                        HiddenDirsFilesReason |= HIDDEN_REASON_ATTRIBUTE;
                        continue;
                    }

                    // Name is the exact wide name now (NameW retired at P1.3);
                    // no re-derivation needed.
                    if (FilterEnabled)
                    {
                        if (!Filter.AgreeMasks(f->Name, NULL))
                        {
                            HiddenFilesCount++;
                            HiddenDirsFilesReason |= HIDDEN_REASON_FILTER;
                            continue;
                        }
                    }

                    //--- if the name is occupied in the array HiddenNames, we will discard it
                    if (HiddenNames.Contains(FALSE, f->Name))
                    {
                        HiddenFilesCount++;
                        HiddenDirsFilesReason |= HIDDEN_REASON_HIDECMD;
                        continue;
                    }

                    Files->Add(*f);
                }
                CFileData upDir;
                static wchar_t buffUp[] = L"..";
                upDir.Name = buffUp; // free() won't be called, we can afford ".."
                upDir.Ext = upDir.Name + 2;
                upDir.Size = CQuadWord(0, 0);
                upDir.Attr = 0;
                upDir.LastWrite = GetZIPArchiveDate();
                upDir.DosName = NULL;
                upDir.PluginData = 0; // 0 just like that, plug-in will overwrite it with its value
                upDir.NameLen = 2;
                upDir.Hidden = 0;
                upDir.IsLink = 0;
                upDir.IsOffline = 0;
                upDir.Association = 0;
                upDir.Selected = 0;
                upDir.Shared = 0;
                upDir.Archive = 0;
                upDir.SizeValid = 0;
                upDir.Dirty = 0; // unnecessary, just for form
                upDir.CutToClip = 0;
                upDir.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
                upDir.IconOverlayDone = 0;

                if (PluginData.NotEmpty())
                    PluginData.GetFileDataForUpDir(GetZIPPath(), upDir);

                Dirs->Add(upDir);
                for (i = 0; i < ZIPDirs->Count; i++)
                {
                    CFileData* f = &ZIPDirs->At(i);
                    if (Configuration.NotHiddenSystemFiles &&
                        (f->Hidden || // both Hidden and Attr are nulled if they are invalid -> tests fail
                         (f->Attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))))
                    { // skip hidden file/directory
                        HiddenDirsCount++;
                        HiddenDirsFilesReason |= HIDDEN_REASON_ATTRIBUTE;
                        continue;
                    }

                    //--- if the name is occupied in the array HiddenNames, we will discard it
                    if (HiddenNames.Contains(TRUE, f->Name))
                    {
                        HiddenDirsCount++;
                        HiddenDirsFilesReason |= HIDDEN_REASON_HIDECMD;
                        continue;
                    }

                    Dirs->Add(*f);
                }
                if (!Files->IsGood() || !Dirs->IsGood())
                {
                    if (!Files->IsGood())
                        Files->ResetState();
                    if (!Dirs->IsGood())
                        Dirs->ResetState();
                    DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                    //          TRACE_I("ReadDirectory: end");
                    return FALSE;
                }
            }
            else
            {
                DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                //        TRACE_I("ReadDirectory: end");
                return FALSE; // the directory has ceased to exist ...
            }

            // sorting of Files and Dirs according to the current sorting method
            SortDirectory();

            UseSystemIcons = !Configuration.UseSimpleIconsInArchives;
            if (UseSystemIcons)
            {
                // preparing for loading of icons
                IconCacheValid = FALSE;
                MSG msg; // need to remove any WM_USER_ICONREADING_END, which would set IconCacheValid = TRUE
                while (PeekMessageW(&msg, HWindow, WM_USER_ICONREADING_END, WM_USER_ICONREADING_END, PM_REMOVE))
                    ;

                int i;
                for (i = 0; i < Associations.Count; i++)
                {
                    if (Associations[i].GetIndex(iconSize) == -3)
                        Associations[i].SetIndex(-1, iconSize);
                }

                // getting of necessary static icons (we won't unpack them)
                for (i = 0; i < Files->Count; i++)
                {
                    // wchar_t*: CAssociationData::ExtensionAndData (returned here by
                    // IsAssociatedStatic) is a packed [extension][DWORD-aligned zero padding]
                    // [icon-location text] wchar_t* buffer (CAssociations::InsertData, icncache.cpp) -
                    // never char* since the P1.5bc icncache widening.
                    const wchar_t* iconLocation = NULL;
                    CFileData* f = &Files->At(i);

                    if (*f->Ext != 0) // an extension exists
                    {
                        /*
            if (PackerFormatConfig.PackIsArchive(f->Name))   // is it an archive which we can process?
            {
              f->Association = TRUE;
              f->Archive = TRUE;
            }
            else
            {
*/
                        const std::wstring extension = sally::text::Fold(f->Ext);
                        f->Association = Associations.IsAssociatedStatic(extension.c_str(), iconLocation, iconSize);
                        f->Archive = FALSE;
                        /*
            }
*/
                    }
                    else
                    {
                        f->Association = FALSE;
                        f->Archive = FALSE;
                    }

                    if (iconLocation != NULL)
                    {
                        CIconData iconData;
                        iconData.FSFileData = NULL;
                        iconData.SetReadingDone(0); // just for form
                        // NameAndData/ExtensionAndData are wchar_t* now, so 'size'/
                        // 'nameLen' must be BYTE offsets (matching InsertData's own '(size + 3) & ~3'
                        // alignment) even though wcslen()/NameLen are WCHAR counts - the old char-count
                        // math allocated/copied half the bytes these strings actually need. Pointer
                        // arithmetic on the wchar_t* buffers stays in WCHAR units.
                        int size = (int)((wcslen(iconLocation) + 1) * sizeof(wchar_t));
                        size = (size + 3) & ~3;                                       // align to four bytes
                        const wchar_t* s = iconLocation + size / sizeof(wchar_t); // skip alignment from zeros
                        int len = (int)wcslen(s);
                        if (len > 0) // icon-location is not empty
                        {
                            int nameLenBytes = f->NameLen * (int)sizeof(wchar_t);
                            int nameLen = nameLenBytes + 4;
                            nameLen -= (nameLen & 0x3); // nameLen % 4  (alignment per four bytes)
                            int iLenBytes = (int)((len + 1) * sizeof(wchar_t));
                            iconData.NameAndData = (wchar_t*)malloc(nameLen + iLenBytes);
                            if (iconData.NameAndData != NULL)
                            {
                                memcpy(iconData.NameAndData, f->Name, nameLenBytes);                  // name +
                                memset(iconData.NameAndData + f->NameLen, 0, nameLen - nameLenBytes); // zeroes alignment +
                                memcpy(iconData.NameAndData + nameLen / sizeof(wchar_t), s, iLenBytes); // icon-location + '\0'

                                iconData.SetFlag(3); // not-loaded icon given by icon-location only
                                                     // we need to allocate space for bitmaps, can't be done in thread
                                iconData.SetIndex(IconCache->AllocIcon(NULL, NULL));
                                if (iconData.GetIndex() != -1)
                                {
                                    IconCache->Add(iconData);
                                    if (!IconCache->IsGood())
                                    {
                                        free(iconData.NameAndData);
                                        IconCache->ResetState();
                                    }
                                }
                                else
                                    free(iconData.NameAndData);
                            }
                        }
                    }
                }

                // waking up icon-reading
                if (IconCache->Count > 1)
                    IconCache->SortArray(0, IconCache->Count - 1, NULL);
                WakeupIconCacheThread(); // start to load icons
            }
            else
            {
                int i;
                for (i = 0; i < Files->Count; i++)
                {
                    CFileData* f = &Files->At(i);

                    if (*f->Ext != 0) // an extension exists
                    {
                        /*
            if (PackerFormatConfig.PackIsArchive(f->Name))   // is it an archive which we can process?
            {
              f->Association = TRUE;
              f->Archive = TRUE;
            }
            else
            {
*/
                        const std::wstring extension = sally::text::Fold(f->Ext);
                        f->Association = Associations.IsAssociated(extension.c_str());
                        f->Archive = FALSE;
                        /*
            }
*/
                    }
                    else
                    {
                        f->Association = FALSE;
                        f->Archive = FALSE;
                    }
                }
            }
        }
        else
        {
            if (Is(ptPluginFS))
            {
                RefreshDiskFreeSpace();

                if (PluginData.NotEmpty())
                {
                    CSalamanderView view(this);
                    PluginData.SetupView(this == MainWindow->LeftPanel, &view, NULL, NULL);
                }

                // setting of icon size for IconCache
                CIconSizeEnum iconSize = GetIconSizeForCurrentViewMode();
                IconCache->SetIconSize(iconSize);

                CFilesArray* FSFiles = GetFSFiles();
                CFilesArray* FSDirs = GetFSDirs();

                Files->SetDeleteData(FALSE); // only shallow copy of data
                Dirs->SetDeleteData(FALSE);  // only shallow copy of data

                if (FSFiles != NULL && FSDirs != NULL)
                {
                    // The Undelete plugin can show tens of thousands of files in one heap in the merged directory
                    // and the reallocation of CFilesArray after implicit 200 items then took several seconds.
                    // Because we know the number of items in advance, we can choose a better strategy for reallocations.
                    Files->SetDelta(DeltaForTotalCount(FSFiles->Count));
                    Dirs->SetDelta(DeltaForTotalCount(FSDirs->Count));

                    int i;
                    for (i = 0; i < FSFiles->Count; i++)
                    {
                        CFileData* f = &FSFiles->At(i);

                        if (Configuration.NotHiddenSystemFiles &&
                            (f->Hidden || // both Hidden and Attr are nulled if they are invalid -> tests fail
                             (f->Attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))))
                        { // skip hidden file/directory
                            HiddenFilesCount++;
                            HiddenDirsFilesReason |= HIDDEN_REASON_ATTRIBUTE;
                            continue;
                        }

                        // Name is the exact wide name now (NameW retired at P1.3);
                        // no re-derivation needed.
                        if (FilterEnabled)
                        {
                            if (!Filter.AgreeMasks(f->Name, NULL))
                            {
                                HiddenFilesCount++;
                                HiddenDirsFilesReason |= HIDDEN_REASON_FILTER;
                                continue;
                            }
                        }

                        //--- if the name is occupied in the array HiddenNames, we will discard it
                        if (HiddenNames.Contains(FALSE, f->Name))
                        {
                            HiddenFilesCount++;
                            HiddenDirsFilesReason |= HIDDEN_REASON_HIDECMD;
                            continue;
                        }

                        Files->Add(*f);
                    }

                    for (i = 0; i < FSDirs->Count; i++)
                    {
                        CFileData* f = &FSDirs->At(i);
                        if (Configuration.NotHiddenSystemFiles &&
                            (f->Hidden || // both Hidden and Attr are nulled if they are invalid -> tests fail
                             (f->Attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))))
                        { // skip hidden file/directory
                            HiddenDirsCount++;
                            HiddenDirsFilesReason |= HIDDEN_REASON_ATTRIBUTE;
                            continue;
                        }

                        //--- if the name is occupied in the array HiddenNames, we will discard it
                        if (HiddenNames.Contains(TRUE, f->Name))
                        {
                            HiddenDirsCount++;
                            HiddenDirsFilesReason |= HIDDEN_REASON_HIDECMD;
                            continue;
                        }

                        Dirs->Add(*f);
                    }

                    if (!Files->IsGood() || !Dirs->IsGood())
                    {
                        if (!Files->IsGood())
                            Files->ResetState();
                        if (!Dirs->IsGood())
                            Dirs->ResetState();
                        DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                        //            TRACE_I("ReadDirectory: end");
                        return FALSE;
                    }
                }
                else
                {
                    DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                    //          TRACE_I("ReadDirectory: end");
                    return FALSE; // the directory has ceased to exist ...
                }

                // if the panel is empty, we will set infoline to "No files found"
                if (Files->Count + Dirs->Count == 0)
                {
                    std::wstring buff;
                    std::vector<sally::unicode::WideTextRange> varPlacements;
                    if (PluginData.NotEmpty() &&
                        PluginData.GetInfoLineContent(MainWindow->LeftPanel == this ? PANEL_LEFT : PANEL_RIGHT,
                                                      NULL, FALSE, 0, 0, TRUE, CQuadWord(0, 0), buff,
                                                      varPlacements))
                    {
                        if (StatusLine->SetText(buff.c_str()))
                            StatusLine->SetSubTexts(varPlacements.data(), varPlacements.size());
                    }
                    else
                        StatusLine->SetText(LoadStrW(IDS_NOFILESFOUND)); // wide - see files_window_windowproc.cpp's WM_USER_SELCHANGED fix
                }

                // sorting of Files and Dirs according to the current sorting method
                SortDirectory();

                if (GetPluginIconsType() == pitFromRegistry)
                {
                    UseSystemIcons = TRUE; // for simplification of draw-item in panel

                    // preparing for loading of icons
                    IconCacheValid = FALSE;
                    MSG msg; // need to remove any WM_USER_ICONREADING_END, which would set IconCacheValid = TRUE
                    while (PeekMessageW(&msg, HWindow, WM_USER_ICONREADING_END, WM_USER_ICONREADING_END, PM_REMOVE))
                        ;

                    int i;
                    for (i = 0; i < Associations.Count; i++)
                    {
                        if (Associations[i].GetIndex(iconSize) == -3)
                            Associations[i].SetIndex(-1, iconSize);
                    }

                    // getting of necessary static icons (we won't unpack anything)
                    for (i = 0; i < Files->Count; i++)
                    {
                        // wchar_t*: CAssociationData::ExtensionAndData (returned here
                        // by IsAssociatedStatic) is a packed [extension][DWORD-aligned zero padding]
                        // [icon-location text] wchar_t* buffer (CAssociations::InsertData,
                        // icncache.cpp) - never char* since the P1.5bc icncache widening.
                        const wchar_t* iconLocation = NULL;
                        CFileData* f = &Files->At(i);

                        if (*f->Ext != 0) // an extension exists
                        {
                            /*
              if (PackerFormatConfig.PackIsArchive(f->Name))   // is it an archive which we can process?
              {
                f->Association = TRUE;
                f->Archive = TRUE;
              }
              else
              {
*/
                            const std::wstring extension = sally::text::Fold(f->Ext);
                            f->Association = Associations.IsAssociatedStatic(extension.c_str(), iconLocation, iconSize);
                            f->Archive = FALSE;
                            /*
              }
*/
                        }
                        else
                        {
                            f->Association = FALSE;
                            f->Archive = FALSE;
                        }

                        if (iconLocation != NULL)
                        {
                            CIconData iconData;
                            iconData.FSFileData = NULL;
                            iconData.SetReadingDone(0); // just for form
                            // NameAndData/ExtensionAndData are wchar_t* now, so 'size'/
                            // 'nameLen' must be BYTE offsets (matching InsertData's own
                            // '(size + 3) & ~3' alignment) even though wcslen()/NameLen are WCHAR
                            // counts - the old char-count math allocated/copied half the bytes these
                            // strings actually need. Pointer arithmetic on the wchar_t* buffers stays
                            // in WCHAR units.
                            int size = (int)((wcslen(iconLocation) + 1) * sizeof(wchar_t));
                            size = (size + 3) & ~3;                                       // align to four bytes
                            const wchar_t* s = iconLocation + size / sizeof(wchar_t); // skip alignment from zeros
                            int len = (int)wcslen(s);
                            if (len > 0) // icon-location is not empty
                            {
                                int nameLenBytes = f->NameLen * (int)sizeof(wchar_t);
                                int nameLen = nameLenBytes + 4;
                                nameLen -= (nameLen & 0x3); // nameLen % 4  (alignment per four bytes)
                                int iLenBytes = (int)((len + 1) * sizeof(wchar_t));
                                iconData.NameAndData = (wchar_t*)malloc(nameLen + iLenBytes);
                                if (iconData.NameAndData != NULL)
                                {
                                    memcpy(iconData.NameAndData, f->Name, nameLenBytes);                  // name +
                                    memset(iconData.NameAndData + f->NameLen, 0, nameLen - nameLenBytes); // zeroes alignment +
                                    memcpy(iconData.NameAndData + nameLen / sizeof(wchar_t), s, iLenBytes); // icon-location + '\0'

                                    iconData.SetFlag(3); // not-loaded icon given by icon-location only
                                                         // we need to allocate space for bitmaps, can't be done in thread
                                    iconData.SetIndex(IconCache->AllocIcon(NULL, NULL));
                                    if (iconData.GetIndex() != -1)
                                    {
                                        IconCache->Add(iconData);
                                        if (!IconCache->IsGood())
                                        {
                                            free(iconData.NameAndData);
                                            IconCache->ResetState();
                                        }
                                    }
                                    else
                                        free(iconData.NameAndData);
                                }
                            }
                        }
                    }

                    // waking up icon-reading
                    if (IconCache->Count > 1)
                        IconCache->SortArray(0, IconCache->Count - 1, NULL);
                    WakeupIconCacheThread(); // start to load icons
                }
                else
                {
                    if (GetPluginIconsType() == pitFromPlugin)
                    {
#ifdef _DEBUG // most likely no error, just it shouldn't happen theoretically
                        if (SimplePluginIcons != NULL)
                            TRACE_E("SimplePluginIcons is not NULL before GetSimplePluginIcons().");
#endif // _DEBUG
                        SimplePluginIcons = PluginData.GetSimplePluginIcons(iconSize);
                        if (SimplePluginIcons == NULL) // not a success -> degradation to pitSimple
                        {
                            SetPluginIconsType(pitSimple);
                        }
                    }

                    if (GetPluginIconsType() == pitSimple)
                    {
                        UseSystemIcons = FALSE; // for simplification of draw-item in panel

                        int i;
                        for (i = 0; i < Files->Count; i++)
                        {
                            CFileData* f = &Files->At(i);

                            if (*f->Ext != 0) // an extension exists
                            {
                                /*
                if (PackerFormatConfig.PackIsArchive(f->Name))   // is it an archive which we can process?
                {
                  f->Association = TRUE;
                  f->Archive = TRUE;
                }
                else
                {
  */
                                const std::wstring extension = sally::text::Fold(f->Ext);
                                f->Association = Associations.IsAssociated(extension.c_str());
                                f->Archive = FALSE;
                                /*
                }
  */
                            }
                            else
                            {
                                f->Association = FALSE;
                                f->Archive = FALSE;
                            }
                        }
                    }
                    else
                    {
                        if (GetPluginIconsType() == pitFromPlugin)
                        {
                            UseSystemIcons = TRUE; // for simplification of draw-item in panel

                            // SimplePluginIcons is already set (code before GetPluginIconsType() == pitSimple)

                            // preparing for loading of icons
                            IconCacheValid = FALSE;
                            MSG msg; // need to remove any WM_USER_ICONREADING_END, which would set IconCacheValid = TRUE
                            while (PeekMessageW(&msg, HWindow, WM_USER_ICONREADING_END, WM_USER_ICONREADING_END, PM_REMOVE))
                                ;

                            // file/directories which don't have a simple icon will be addeed to icon-cache
                            {
                                CALL_STACK_MESSAGE1("CFilesWindow::ReadDirectory::FS-icons-from-plugin");

                                int count = FSFiles->Count + FSDirs->Count;
                                int debugCount = 0;
                                int i;
                                for (i = 0; i < count; i++)
                                {
                                    BOOL isDir = i < FSDirs->Count;
                                    CFileData* f = (isDir ? &FSDirs->At(i) : &FSFiles->At(i - FSDirs->Count));

                                    // need a pointer to CFileData for PluginFSDir, because moving occurs in Files and Dirs
                                    // e.g. when sorting (Files and Dirs are arrays of CFileData, not (CFileData *), that's why these problems exist)
                                    if (Configuration.NotHiddenSystemFiles &&
                                        (f->Hidden || //both Hidden and Attr are zeroed if they are invalid -> tests fail
                                         (f->Attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))))
                                    { // skip hidden file/directory
                                        continue;
                                    }
                                    if (!isDir)
                                    {
                                        // Name is the exact wide name now (NameW
                                        // retired at P1.3); no re-derivation needed.
                                        if (FilterEnabled)
                                        {
                                            if (!Filter.AgreeMasks(f->Name, NULL))
                                                continue;
                                        }
                                    }
                                    //--- if the name is occupied in the array HiddenNames, we will discard it
                                    if (HiddenNames.Contains(isDir, f->Name))
                                        continue;
                                    debugCount++;

                                    if ((!isDir || i != 0 || wcscmp(f->Name, L"..") != 0) &&
                                        !PluginData.HasSimplePluginIcon(*f, isDir)) // add to icon-cache?
                                    {
                                        CIconData iconData;
                                        iconData.FSFileData = f;
                                        iconData.SetReadingDone(0); // just for form
                                        // NameAndData is wchar_t*; f->NameLen is a
                                        // WCHAR count (spl_com.h), so the DWORD-alignment and buffer
                                        // size must be computed in BYTES - the old char-count math
                                        // allocated/copied half the name's actual bytes.
                                        int nameLenBytes = f->NameLen * (int)sizeof(wchar_t);
                                        int nameLen = nameLenBytes + 4;
                                        nameLen -= (nameLen & 0x3); // nameLen % 4  (alignment per four bytes)
                                        iconData.NameAndData = (wchar_t*)malloc(nameLen);
                                        if (iconData.NameAndData != NULL)
                                        {
                                            memcpy(iconData.NameAndData, f->Name, nameLenBytes);                  // name +
                                            memset(iconData.NameAndData + f->NameLen, 0, nameLen - nameLenBytes); // zeroes alignment

                                            iconData.SetFlag(0); // so far not loaded icon (plugin-icon)
                                                                 // need to allocate space for bitmaps, can't be done in thread
                                            iconData.SetIndex(IconCache->AllocIcon(NULL, NULL));
                                            if (iconData.GetIndex() != -1)
                                            {
                                                IconCache->Add(iconData);
                                                if (!IconCache->IsGood())
                                                {
                                                    free(iconData.NameAndData);
                                                    IconCache->ResetState();
                                                }
                                            }
                                            else
                                                free(iconData.NameAndData);
                                        }
                                    }
                                }
                                if (debugCount != Files->Count + Dirs->Count)
                                {
                                    TRACE_E("CFilesWindow::ReadDirectory(): unexpected situation: different count of filtered items "
                                            "for icon-reading ("
                                            << debugCount << ") and panel (" << Files->Count + Dirs->Count << ")!");
                                }
                            }

                            // waking up icon-reading
                            if (IconCache->Count > 1)
                                IconCache->SortArray(0, IconCache->Count - 1, &PluginData);
                            WakeupIconCacheThread(); // start to load icons
                        }
                        else
                            TRACE_E("Unexpected situation (1) in v CFilesWindow::ReadDirectory().");
                    }
                }
            }
            else
            {
                TRACE_E("Unexpected situation (2) in CFilesWindow::ReadDirectory().");
                DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
                // setting of icon size for IconCache
                CIconSizeEnum iconSize = GetIconSizeForCurrentViewMode();
                IconCache->SetIconSize(iconSize);
                //        TRACE_I("ReadDirectory: end");
                return FALSE;
            }
        }
    }
    DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);

    // The disabled "restore remembered directory sizes for this path"
    // hook lived here (DirectorySizesHolder.Restore(this)); the holder is deleted - see
    // the matching note in files_window_delete_email.cpp.

    //  TRACE_I("ReadDirectory: end");
    return TRUE;
}

// sorts array Dirs and Files independently on global variables
void SortFilesAndDirectories(CFilesArray* files, CFilesArray* dirs, CSortType sortType, BOOL reverseSort, BOOL sortDirsByName)
{
    CALL_STACK_MESSAGE1("SortDirectoryAux()");

    // CAUTION: must correspond to sort-code in RefreshDirectory, ChangeSortType and CompareDirectories !!!

    if (dirs->Count > 0)
    {
        BOOL hasRoot = (dirs->At(0).NameLen == 2 && dirs->At(0).Name[0] == '.' &&
                        dirs->At(0).Name[1] == '.'); // root directory
        int firstIndex = hasRoot ? 1 : 0;
        if (dirs->Count - firstIndex > 1) // if there's one item only, there's nothing to sort
        {
            switch (sortType)
            {
            case stName:
                SortNameExt(*dirs, firstIndex, dirs->Count - 1, reverseSort);
                break;
            case stExtension:
                SortExtName(*dirs, firstIndex, dirs->Count - 1, reverseSort);
                break;
            case stTime:
            {
                if (sortDirsByName)
                    SortNameExt(*dirs, firstIndex, dirs->Count - 1, FALSE);
                else
                    SortTimeNameExt(*dirs, firstIndex, dirs->Count - 1, reverseSort);
                break;
            }
            case stSize:
                SortSizeNameExt(*dirs, firstIndex, dirs->Count - 1, reverseSort);
                break;
            case stAttr:
                SortAttrNameExt(*dirs, firstIndex, dirs->Count - 1, reverseSort);
                break;
            }
        }
    }
    if (files->Count > 1)
    {
        switch (sortType)
        {
        case stName:
            SortNameExt(*files, 0, files->Count - 1, reverseSort);
            break;
        case stExtension:
            SortExtName(*files, 0, files->Count - 1, reverseSort);
            break;
        case stTime:
            SortTimeNameExt(*files, 0, files->Count - 1, reverseSort);
            break;
        case stSize:
            SortSizeNameExt(*files, 0, files->Count - 1, reverseSort);
            break;
        case stAttr:
            SortAttrNameExt(*files, 0, files->Count - 1, reverseSort);
            break;
        }
    }
}

void CFilesWindow::SortDirectory(CFilesArray* files, CFilesArray* dirs)
{
    CALL_STACK_MESSAGE1("CFilesWindow::SortDirectory()");

    if (files == NULL)
        files = Files;
    if (dirs == NULL)
        dirs = Dirs;
    SortFilesAndDirectories(files, dirs, SortType, ReverseSort, Configuration.SortDirsByName);

    // single-purpose monitors for changes of Configuration.SortUsesLocale and Configuration.SortDetectNumbers
    // variables for the method CFilesWindow::RefreshDirectory
    SortedWithRegSet = Configuration.SortUsesLocale;
    SortedWithDetectNum = Configuration.SortDetectNumbers;
    VisibleItemsArray.InvalidateArr();
    VisibleItemsArraySurround.InvalidateArr();
}

#ifndef _WIN64

BOOL IsWin64RedirectedDirAux(const wchar_t* subDir, const wchar_t* redirectedDir, const wchar_t* redirectedDirLastComp,
                             const std::wstring& windowsPrefix, std::wstring* completedPath, BOOL failIfDirWithSameNameExists)
{
    if (IsTheSamePath(subDir, redirectedDir))
    {
        std::wstring probe = windowsPrefix;
        probe.append(redirectedDir);

        WIN32_FIND_DATAW find;
        HANDLE h;
        if (failIfDirWithSameNameExists)
        {
            h = SalFindFirstFileHW(probe.c_str(), &find);
            if (h != INVALID_HANDLE_VALUE)
            {
                SalLPFindClose(h);
                return FALSE; // this is not just a pseudo-directory, there is a directory with the same name, which means that e.g. context menu will work more or less normally
            }
        }

        probe.append(L"\\*");
        h = SalFindFirstFileHW(probe.c_str(), &find);
        if (h != INVALID_HANDLE_VALUE)
        {
            SalLPFindClose(h);
            if (completedPath != NULL)
            {
                const size_t lastSlash = completedPath->find_last_of(L'\\');
                completedPath->resize(lastSlash == std::wstring::npos ? 0 : lastSlash + 1);
                completedPath->append(redirectedDirLastComp != NULL ? redirectedDirLastComp : redirectedDir);
            }
            return TRUE;
        }
    }
    return FALSE;
}

BOOL IsWin64RedirectedDir(const wchar_t* path, std::wstring* completedPath, BOOL failIfDirWithSameNameExists)
{
    if (Windows64Bit && !WindowsDirectory.empty())
    {
        std::wstring windowsPrefix = WindowsDirectory;
        if (windowsPrefix.back() != L'\\')
            windowsPrefix.push_back(L'\\');
        const size_t len = windowsPrefix.length();
        if (_wcsnicmp(windowsPrefix.c_str(), path, len) == 0)
        {
            const wchar_t* subDir = path + len;
            if (IsWin64RedirectedDirAux(subDir, L"Sysnative", NULL, windowsPrefix, completedPath, failIfDirWithSameNameExists) ||
                IsWin64RedirectedDirAux(subDir, L"system32\\catroot", L"catroot", windowsPrefix, completedPath, failIfDirWithSameNameExists) ||
                IsWin64RedirectedDirAux(subDir, L"system32\\catroot2", L"catroot2", windowsPrefix, completedPath, failIfDirWithSameNameExists) ||
                Windows7AndLater && IsWin64RedirectedDirAux(subDir, L"system32\\DriverStore", L"DriverStore", windowsPrefix, completedPath, failIfDirWithSameNameExists) ||
                IsWin64RedirectedDirAux(subDir, L"system32\\drivers\\etc", L"etc", windowsPrefix, completedPath, failIfDirWithSameNameExists) ||
                IsWin64RedirectedDirAux(subDir, L"system32\\LogFiles", L"LogFiles", windowsPrefix, completedPath, failIfDirWithSameNameExists) ||
                IsWin64RedirectedDirAux(subDir, L"system32\\spool", L"spool", windowsPrefix, completedPath, failIfDirWithSameNameExists))
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

BOOL ContainsWin64RedirectedDir(CFilesWindow* panel, int* indexes, int count, std::wstring& redirectedDir, BOOL onlyAdded)
{
    redirectedDir.clear();
    if (Windows64Bit && !WindowsDirectory.empty())
    {
        const std::wstring panelPath = panel->GetPathW();
        int i;
        for (i = 0; i < count; i++)
        {
            if (indexes[i] >= 0 && indexes[i] < panel->Dirs->Count) // only subdirectories matter
            {
                CFileData* dir = &panel->Dirs->At(indexes[i]);
                if (dir->IsLink) // all pseudo-directories have IsLink set
                {
                    std::wstring path = panelPath;
                    SalPathAppendW(path, dir->Name);
                    if (IsWin64RedirectedDir(path.c_str(), NULL, onlyAdded))
                    {
                        redirectedDir = dir->Name;
                        return TRUE;
                    }
                }
            }
        }
    }
    return FALSE;
}

BOOL AddWin64RedirectedDirAux(const wchar_t* path, const wchar_t* subDir, const wchar_t* redirectedDirPrefix,
                              const wchar_t* redirectedDirLastComp, CFilesArray* dirs,
                              WIN32_FIND_DATAW* fileData, BOOL* dirWithSameNameExists)
{
    if (IsTheSamePath(subDir, redirectedDirPrefix))
    {
        int deleteIndex = -1;
        int i;
        for (i = 0; i < dirs->Count; i++)
            if (StrICmpW(dirs->At(i).Name, redirectedDirLastComp) == 0)
            {
                if (dirs->At(i).IsLink)
                    return FALSE; // this redirected-dir is already added
                deleteIndex = i;
                break;
            }

        std::wstring findPath(path);
        SalPathAppendW(findPath, redirectedDirLastComp);
        SalPathAppendW(findPath, L"*");
        if (!findPath.empty())
        {
            HANDLE h;
            WIN32_FIND_DATAW fileDataW;
            h = SalFindFirstFileHW(findPath.c_str(), &fileDataW); // find-data for redirected-dir can be obtained from the "." directory in the listing of redirected-dir
            if (h != INVALID_HANDLE_VALUE)
            {
                BOOL found = FALSE;
                do
                {
                    if (wcscmp(fileDataW.cFileName, L"..") == 0 &&
                        (fileDataW.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) // "." directory
                    {
                        // Copy non-name fields; name fields are overwritten below.
                        fileData->dwFileAttributes = fileDataW.dwFileAttributes;
                        fileData->ftCreationTime = fileDataW.ftCreationTime;
                        fileData->ftLastAccessTime = fileDataW.ftLastAccessTime;
                        fileData->ftLastWriteTime = fileDataW.ftLastWriteTime;
                        fileData->nFileSizeHigh = fileDataW.nFileSizeHigh;
                        fileData->nFileSizeLow = fileDataW.nFileSizeLow;
                        fileData->dwReserved0 = fileDataW.dwReserved0;
                        fileData->dwReserved1 = fileDataW.dwReserved1;
                        found = TRUE;
                        break;
                    }
                } while (SalLPFindNextFile(h, &fileDataW));
                SalLPFindClose(h);
                if (found)
                {
                    if (deleteIndex != -1)
                        dirs->Delete(deleteIndex); // there's is a directory here, we will delete it, redirected-dir has priority (redirector ignores this directory)
                    lstrcpynW(fileData->cFileName, redirectedDirLastComp, _countof(fileData->cFileName));
                    fileData->cAlternateFileName[0] = 0;

                    if (CutDirectoryW(findPath)) // find out if there's a directory with the same name as redirected-dir on the disk (it does not need to be in the 'dirs' array, e.g. because of the command "Hide Selected Names")
                    {
                        WIN32_FIND_DATAW fd;
                        h = SalFindFirstFileHW(findPath.c_str(), &fd);
                        if (h != INVALID_HANDLE_VALUE)
                        {
                            SalLPFindClose(h);
                            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                                StrICmpW(fd.cFileName, redirectedDirLastComp) == 0)
                            {
                                *dirWithSameNameExists = TRUE;
                            }
                        }
                    }

                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

BOOL AddWin64RedirectedDir(const wchar_t* path, CFilesArray* dirs, WIN32_FIND_DATAW* fileData,
                           int* index, BOOL* dirWithSameNameExists)
{
    *dirWithSameNameExists = FALSE;
    if (Windows64Bit && !WindowsDirectory.empty())
    {
        std::wstring windowsPrefix = WindowsDirectory;
        if (windowsPrefix.back() != L'\\')
            windowsPrefix.push_back(L'\\');
        size_t len = windowsPrefix.length();
        if (wcslen(path) + 1 == len)
            windowsPrefix.resize(--len); // patch due to path == win-dir (discard ending backslash)
        if (_wcsnicmp(windowsPrefix.c_str(), path, len) == 0)
        {
            const wchar_t* subDir = path + len;
            if (*index == 0 && AddWin64RedirectedDirAux(path, subDir, L"", L"Sysnative", dirs, fileData, dirWithSameNameExists) ||
                *index == 0 && AddWin64RedirectedDirAux(path, subDir, L"system32\\drivers", L"etc", dirs, fileData, dirWithSameNameExists) ||
                *index == 0 && AddWin64RedirectedDirAux(path, subDir, L"system32", L"catroot", dirs, fileData, dirWithSameNameExists) ||
                *index <= 1 && AddWin64RedirectedDirAux(path, subDir, L"system32", L"catroot2", dirs, fileData, dirWithSameNameExists) && (*index = 1) != 0 ||
                *index <= 2 && AddWin64RedirectedDirAux(path, subDir, L"system32", L"LogFiles", dirs, fileData, dirWithSameNameExists) && (*index = 2) != 0 ||
                *index <= 3 && AddWin64RedirectedDirAux(path, subDir, L"system32", L"spool", dirs, fileData, dirWithSameNameExists) && (*index = 3) != 0 ||
                Windows7AndLater && *index <= 4 && AddWin64RedirectedDirAux(path, subDir, L"system32", L"DriverStore", dirs, fileData, dirWithSameNameExists) && (*index = 4) != 0)
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}

#endif // _WIN64

BOOL CFilesWindow::ChangeDir(const wchar_t* newDir, int suggestedTopIndex, const wchar_t* suggestedFocusName,
                             int mode, int* failReason, BOOL convertFSPathToInternal, BOOL showNewDirPathInErrBoxes)
{
    CALL_STACK_MESSAGE7("CFilesWindow::ChangeDir(%ls, %d, %ls, %d, , %d, %d)", newDir, suggestedTopIndex,
                        suggestedFocusName, mode, convertFSPathToInternal, showNewDirPathInErrBoxes);

    // Back up arguments that may point into panel state changed during this operation.
    std::wstring newDirOwner;
    if (newDir != NULL)
    {
        newDirOwner = newDir;
        newDir = newDirOwner.c_str();
    }
    std::wstring suggestedFocusNameOwner;
    if (suggestedFocusName != NULL)
    {
        suggestedFocusNameOwner = suggestedFocusName;
        suggestedFocusName = suggestedFocusNameOwner.c_str();
    }

    MainWindow->CancelPanelsUI(); // cancel QuickSearch and QuickEdit
    std::wstring absFSPath;
    std::wstring path;
    std::wstring errBuf;
    BOOL sendDirectlyToPlugin = FALSE;
    std::wstring pathW;
    GetGeneralPath(pathW, TRUE);
    path = pathW;
    CChangeDirDlg dlg(HWindow, pathW, MainWindow->GetActivePanel()->Is(ptPluginFS) ? &sendDirectlyToPlugin : NULL);

    // refresh DefaultDir
    MainWindow->UpdateDefaultDir(TRUE);

    BOOL useStopRefresh = newDir == NULL;
    if (useStopRefresh)
        BeginStopRefresh(); // snooper will relax now

CHANGE_AGAIN:

    if (newDir != NULL || dlg.Execute() == IDOK)
    {
        if (newDir == NULL)
        {
            convertFSPathToInternal = TRUE; // after input from user, conversion to internal format is necessary
            // postprocessing will be done only for path, which is not to be sent directly to plugin
            if (!sendDirectlyToPlugin && !PostProcessPathFromUserW(HWindow, pathW))
                goto CHANGE_AGAIN;
            path = pathW;
        }
        BOOL sendDirectlyToPluginLocal = sendDirectlyToPlugin;
        TopIndexMem.Clear(); // long jump

    CHANGE_AGAIN_NO_DLG:

        UpdateWindow(MainWindow->HWindow);
        if (newDir != NULL)
            path = newDir;
        else // focus and top-index setting won't be done for path from dialog
        {
            suggestedTopIndex = -1;
            suggestedFocusName = NULL;
        }

        std::wstring fsName;
        const wchar_t* fsUserPart;
        if (!sendDirectlyToPluginLocal && IsPluginFSPath(path.c_str(), &fsName, &fsUserPart))
        {
            const size_t fsUserPartOffset = (size_t)(fsUserPart - path.c_str());
            std::wstring fsUserPartOwner = fsUserPart;
            int index;
            int fsNameIndex;
            if (Plugins.IsPluginFS(fsName.c_str(), index, fsNameIndex))
            {
                BOOL pluginFailure = FALSE;
                if (convertFSPathToInternal) // convert path to internal format
                {
                    CPluginData* plugin = Plugins.Get(index);
                    if (plugin != NULL && plugin->InitDLL(MainWindow->HWindow, FALSE, TRUE, TRUE)) // plugin does not have to be loaded, in which case we let it load
                        plugin->GetPluginInterfaceForFS()->ConvertPathToInternalW(fsName.c_str(), fsNameIndex, fsUserPartOwner);
                    else
                    {
                        pluginFailure = TRUE;
                        // TRACE_E("Unexpected situation in CFilesWindow::ChangeDir()");  // if the plugin is blocked by the user (if the registration key is missing)
                    }
                }

                // select FS for opening the path has this order: active FS, one of the detached FS, then new FS

                // CPluginData *p = Plugins.Get(index);
                // if (p != NULL) strcpy(fsName, );  // let the size of the letters be determined by the user, for conversion to the original size of the letters we would have to search the array p->FSNames
                int localFailReason;
                BOOL ret;
                BOOL done = FALSE;

                // if the path cannot be listed in the FS interface in the panel, we will try to find a detached FS interface,
                // which would be able to list the path (so that a new FS is not opened unnecessarily)
                int fsNameIndexDummy;
                BOOL convertPathToInternalDummy = FALSE;
                if (!Is(ptPluginFS) || // FS interface in the panel cannot list the path (ChangePathToPluginFS opens a new FS)
                    !IsPathFromActiveFS(fsName.c_str(), fsUserPartOwner, fsNameIndexDummy, convertPathToInternalDummy))
                {
                    CDetachedFSList* list = MainWindow->DetachedFSList;
                    int i;
                    for (i = 0; i < list->Count; i++)
                    {
                        if (list->At(i)->IsPathFromThisFS(fsName.c_str(), fsUserPartOwner.c_str()))
                        {
                            done = TRUE;
                            // trying to change to needed path, at the same time we will connect the detached FS
                            ret = ChangePathToDetachedFS(i, suggestedTopIndex, suggestedFocusName, TRUE,
                                                         &localFailReason, fsName.c_str(), fsUserPartOwner.c_str(), mode, mode == 3);

                            break;
                        }
                    }
                }

                if (!pluginFailure && !done)
                {
                    ret = ChangePathToPluginFS(fsName.c_str(), fsUserPartOwner.c_str(), suggestedTopIndex, suggestedFocusName,
                                               FALSE, mode, NULL, TRUE, &localFailReason, FALSE, mode == 3);
                }
                else
                {
                    if (!done) // only if 'ret' and 'localFailReason' from ChangePathToDetachedFS() are not set yet
                    {
                        ret = FALSE;
                        localFailReason = CHPPFR_INVALIDPATH;
                    }
                }

                if (!ret && newDir == NULL && localFailReason == CHPPFR_INVALIDPATH)
                { // path error (can be caused by the path itself or by the plugin - it cannot be loaded - this is very unlikely)
                    // convert path to external format
                    CPluginData* plugin = Plugins.Get(index);
                    if (!pluginFailure && plugin != NULL && plugin->InitDLL(MainWindow->HWindow, FALSE, TRUE, FALSE)) // plugin does not have to be loaded, in which case we let it load
                        plugin->GetPluginInterfaceForFS()->ConvertPathToExternalW(fsName.c_str(), fsNameIndex, fsUserPartOwner);
                    // else TRACE_E("Unexpected situation (2) in CFilesWindow::ChangeDir()");  // if the plugin is blocked by the user (if the registration key is missing)

                    pathW.assign(path, 0, fsUserPartOffset);
                    pathW += fsUserPartOwner;
                    goto CHANGE_AGAIN; // returning back to the dialog for entering the path
                }
                else
                {
                    if (useStopRefresh)
                        EndStopRefresh(); // snooper will be started again
                    if (failReason != NULL)
                        *failReason = localFailReason;
                    return ret;
                }
            }
            else
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_PATHERRORFORMAT), path.c_str(), LoadStrW(IDS_NOTPLUGINFS));
                gPrompter->ShowError(LoadStrW(IDS_ERRORCHANGINGDIR), msg.c_str());
                if (newDir != NULL)
                {
                    if (useStopRefresh)
                        EndStopRefresh(); // snooper will be started again
                    if (failReason != NULL)
                        *failReason = CHPPFR_INVALIDPATH;
                    return FALSE; // finished, cannot be repeated
                }
                pathW = path;
                goto CHANGE_AGAIN;
            }
        }
        else
        {
            if (!sendDirectlyToPluginLocal &&
                (path[0] != 0 && path[1] == ':' ||                                             // X: type paths
                 (path[0] == '/' || path[0] == '\\') && (path[1] == '/' || path[1] == '\\') || // UNC paths
                 Is(ptDisk) || Is(ptZIPArchive)))                                              // disk+archive of the relative path
            {                                                                                  // this is a disk path (absolute or relative) - turn all '/' to '\\' + remove duplicated '\\'
                SlashesToBackslashesAndRemoveDups(path);
                path.resize(wcslen(path.c_str()));
            }

            int errTextID;
            // Wide - text's only real consumer is the final error message
            // below; pathForMsg/path/newDir (the rest of this function's narrow buffers,
            // out of scope) are untouched.
            std::wstring text;                       // caution: textFailReason must be set
            int textFailReason = CHPPFR_INVALIDPATH; // if text != NULL, it contains the code of error which occurred
            std::wstring curPath;
            if (sendDirectlyToPluginLocal)
                errTextID = IDS_INCOMLETEFILENAME;
            else
            {
                //        MainWindow->GetActivePanel()->GetGeneralPath(curPath, curPath.Size());
                //        if (!SalGetFullName(path, &errTextID, (MainWindow->GetActivePanel()->Is(ptDisk) ||
                //                            MainWindow->GetActivePanel()->Is(ptZIPArchive)) ? curPath : NULL))
                if (Is(ptDisk) || Is(ptZIPArchive))
                    GetGeneralPath(curPath); // because of FTP plugin - relative path in "target panel path" when connecting
            }
            BOOL callNethood = FALSE;
            std::wstring resolvedPath = path;
            // sendDirectlyToPluginLocal short-circuits SalGetFullNameW entirely, same as the
            // original narrow call - resolvedPath stays an unchanged copy of 'path' in that case.
            BOOL enterFailureBlock = sendDirectlyToPluginLocal ||
                                     !SalGetFullNameW(resolvedPath, &errTextID, (Is(ptDisk) || Is(ptZIPArchive)) ? curPath.c_str() : NULL, NULL,
                                                      &callNethood);
            path = std::move(resolvedPath); // sync back regardless of outcome, matching SalGetFullName's in-place-mutate contract
            if (enterFailureBlock)
            {
                sendDirectlyToPluginLocal = FALSE;
                if ((errTextID == IDS_SERVERNAMEMISSING || errTextID == IDS_SHARENAMEMISSING) && callNethood)
                { // incomplete UNC path will be given to the first plugin which supports FS and called SalamanderGeneral->SetPluginIsNethood()
                    std::wstring nethoodFSName;
                    if (Plugins.GetFirstNethoodPluginFSName(&nethoodFSName))
                    {
                        nethoodFSName += L':';
                        nethoodFSName += path;
                        absFSPath = std::move(nethoodFSName);
                        if (newDir != NULL)
                        {
                            newDirOwner = absFSPath;
                            newDir = newDirOwner.c_str(); // in case the path is entered from outside
                        }
                        else
                            path = absFSPath;
                        goto CHANGE_AGAIN_NO_DLG; // try to open the incomplete UNC path in the plugin
                    }
                }
                if (errTextID == IDS_EMPTYNAMENOTALLOWED)
                {
                    if (useStopRefresh)
                        EndStopRefresh(); // snooper will be started again
                    if (failReason != NULL)
                        *failReason = CHPPFR_SUCCESS;
                    return FALSE; // empty string, nothing to do
                }
                if (errTextID == IDS_INCOMLETEFILENAME) // relative path on FS
                {
                    if (MainWindow->GetActivePanel()->Is(ptPluginFS) &&
                        MainWindow->GetActivePanel()->GetPluginFS()->NotEmpty())
                    {
                        BOOL success = TRUE;
                        absFSPath = path;
                        const BOOL haveAbsoluteFSPath =
                            MainWindow->GetActivePanel()->GetPluginFS()->GetFullFSPathW(
                                HWindow, MainWindow->GetActivePanel()->GetPluginFS()->GetPluginFSName(),
                                absFSPath, success);
                        if (!success || haveAbsoluteFSPath)
                        {
                            if (success) // we have the absolute path
                            {
                                if (newDir != NULL)
                                {
                                    newDirOwner = absFSPath;
                                    newDir = newDirOwner.c_str(); // in case the path is entered from outside
                                }
                                else
                                    path = absFSPath;
                                convertFSPathToInternal = FALSE; // the path is already in internal format, so we must not convert it to internal format again
                                goto CHANGE_AGAIN_NO_DLG;        // let's try the entered absolute path
                            }
                            else // the error has been displayed to the user
                            {
                                if (newDir != NULL)
                                {
                                    if (useStopRefresh)
                                        EndStopRefresh(); // snooper will be started again
                                    if (failReason != NULL)
                                        *failReason = CHPPFR_INVALIDPATH;
                                    return FALSE; // finished, cannot be repeated
                                }
                                pathW = path;
                                goto CHANGE_AGAIN; // show the path in dialog again, so that the user can correct it
                            }
                        }
                    }
                }
                text = LoadStrW(errTextID);
                textFailReason = CHPPFR_INVALIDPATH;
            }
            BOOL showErr = TRUE;
            if (text.empty())
            {
                if (!path.empty() && path.size() > 1 && path[1] == L':')
                    path[0] = towupper(path[0]); // "c:" path will be "C:"
                std::wstring copy = GetRootPath(path.c_str());
                int len = static_cast<int>(copy.size());

                if (!CheckAndRestorePath(copy.c_str()))
                {
                    if (newDir != NULL)
                    {
                        if (useStopRefresh)
                            EndStopRefresh(); // snooper will be started again
                        if (failReason != NULL)
                            *failReason = CHPPFR_INVALIDPATH;
                        return FALSE; // finished, cannot be repeated
                    }
                    pathW = path;
                    goto CHANGE_AGAIN;
                }

                if (len > 0 && len < static_cast<int>(path.size())) // not a root path
                {
                    size_t end = static_cast<size_t>(len - 1); // points to '\\'
                    copy.resize(end);
                    size_t writeStart = end;
                    while (end < path.size())
                    {
                        writeStart = AppendNextPathComponentW(path, end, copy);

                    _TRY_AGAIN:

                        BOOL pathEndsWithSpaceOrDot;
                        DWORD copyAttr;
                        const int copyLen = static_cast<int>(copy.size());
                        if (copyLen > 0 && (copy[copyLen - 1] <= ' ' || copy[copyLen - 1] == '.'))
                        {
                            copyAttr = gFileSystem->GetFileAttributes(copy.c_str());
                            pathEndsWithSpaceOrDot = copyAttr != INVALID_FILE_ATTRIBUTES;
                        }
                        else
                        {
                            pathEndsWithSpaceOrDot = FALSE;
                        }

                        WIN32_FIND_DATAW find;
                        HANDLE h;
                        if (!pathEndsWithSpaceOrDot)
                            h = SalFindFirstFileHW(copy.c_str(), &find);
                        else
                            h = INVALID_HANDLE_VALUE;
                        DWORD err;
                        if (h == INVALID_HANDLE_VALUE && !pathEndsWithSpaceOrDot)
                        {
                            err = GetLastError();
                            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND &&
                                err != ERROR_BAD_PATHNAME && err != ERROR_INVALID_NAME)
                            { // if there's chance that the path contains a directory to which we don't have access (we will try if there are other components of the path accessible)
                                DWORD firstErr = err;
                                const size_t firstCopyEnd = copy.size();
                                while (end < path.size())
                                {
                                    writeStart = AppendNextPathComponentW(path, end, copy); // preserve inaccessible components verbatim
                                    h = SalFindFirstFileHW(copy.c_str(), &find);
                                    if (h != INVALID_HANDLE_VALUE)
                                        break; // we've found an accessible component, continuing...
                                    err = GetLastError();
                                    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ||
                                        err == ERROR_BAD_PATHNAME || err == ERROR_INVALID_NAME)
                                        break; // an error in the path, we're finished...
                                }
                                if (end == path.size() && h == INVALID_HANDLE_VALUE) // another accessible component not found, try listing the current path
                                {
                                    std::wstring probe = copy;
                                    SalPathAppendW(probe, L"*.*");
                                    h = SalFindFirstFileHW(probe.c_str(), &find);
                                    if (h != INVALID_HANDLE_VALUE) // the path can be listed
                                    {
                                        SalLPFindClose(h);

                                        // changing the path to absolute windows path
                                        BOOL ret = ChangePathToDisk(HWindow, copy.c_str(), suggestedTopIndex, suggestedFocusName,
                                                                    NULL, TRUE, FALSE, FALSE, failReason);
                                        if (useStopRefresh)
                                            EndStopRefresh(); // snooper will be started again
                                        return ret;
                                    }
                                }
                                if (h == INVALID_HANDLE_VALUE) // accessible part of the path not found, we will report the first found error
                                {
                                    err = firstErr;
                                    copy.resize(firstCopyEnd);
                                }
                            }
#ifndef _WIN64
                            else
                            {
                                if (IsWin64RedirectedDir(copy.c_str(), &copy, FALSE))
                                {
                                    writeStart = copy.size();
                                    continue;
                                }
                            }
#endif // _WIN64
                        }

                        if (h != INVALID_HANDLE_VALUE || pathEndsWithSpaceOrDot)
                        {
                            if (h != INVALID_HANDLE_VALUE)
                            {
                                SalLPFindClose(h);
                                // find.cFileName and 'copy'/'st' are both wide now; the
                                // former ANSI round-trip here was purely vestigial (both sides were
                                // already the same domain) - compare/copy find.cFileName directly.
                                int len2 = (int)wcslen(find.cFileName); // must fit (only the size of letters is changed - result of FindFirstFile)
                                const std::wstring originalComponent = copy.substr(writeStart + 1);
                                if (static_cast<int>(originalComponent.size()) != len2) // e.g. "aaa  " returns "aaa"
                                {
                                    TRACE_E("CFilesWindow::ChangeDir(): unexpected situation: FindFirstFile returned name with "
                                            "different length: \""
                                            << sally::diagnostic::EncodeAcpLossy(find.cFileName) << "\" for \""
                                            << sally::diagnostic::EncodeAcpLossy(originalComponent) << "\"");
                                }
                                ReplacePathComponentW(copy, writeStart, find.cFileName);
                                writeStart = copy.size();
                            }
                            else
                                writeStart = copy.size();

                            // copy containts the "translated" path
                            if (!pathEndsWithSpaceOrDot)
                                copyAttr = find.dwFileAttributes;
                            if ((copyAttr & FILE_ATTRIBUTE_DIRECTORY) == 0)
                            { // file -> is it an archive?
                                if (PackerFormatConfig.PackIsArchive(copy.c_str()))
                                {
                                    const size_t archivePathOffset = end < path.size() ? end + 1 : end;
                                    const wchar_t* archivePath = path.c_str() + archivePathOffset;
                                    // changing the path to absolute path to archive
                                    int localFailReason;
                                    BOOL ret = ChangePathToArchive(copy.c_str(), archivePath, suggestedTopIndex, suggestedFocusName,
                                                                   FALSE, NULL, TRUE, &localFailReason, FALSE, TRUE);
                                    if (!ret && localFailReason == CHPPFR_SHORTERPATH)
                                    {
                                        std::wstring msg = FormatStrW(LoadStrW(IDS_PATHINARCHIVENOTFOUND), archivePath);
                                        gPrompter->ShowError(LoadStrW(IDS_ERRORCHANGINGDIR), msg.c_str());
                                    }
                                    if (failReason != NULL)
                                        *failReason = localFailReason;
                                    if (useStopRefresh)
                                        EndStopRefresh(); // snooper will be started again
                                    return ret;
                                }
                                else
                                {
                                    std::wstring shortenedPath = copy;
                                    std::wstring focusName;
                                    if (end == path.size() && CutDirectoryW(shortenedPath, &focusName)) // when the path does not end with '\\' (path to file)
                                    {
                                        // change of the path to absolute windows path + focus to the file
                                        ChangePathToDisk(HWindow, shortenedPath.c_str(), -1, focusName.c_str(), NULL, TRUE, FALSE, FALSE, failReason);
                                        if (useStopRefresh)
                                            EndStopRefresh(); // snooper will be started again
                                        if (failReason != NULL && *failReason == CHPPFR_SUCCESS)
                                            *failReason = CHPPFR_FILENAMEFOCUSED;
                                        return FALSE; // listing another path (file name is cut)
                                    }
                                    else
                                    {
                                        text = LoadStrW(IDS_NOTARCHIVEPATH);
                                        textFailReason = CHPPFR_INVALIDARCHIVE;
                                        break;
                                    }
                                }
                            }
                        }
                        else
                        {
                            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ||
                                err == ERROR_BAD_PATHNAME)
                            {
                                text = LoadStrW(IDS_PATHNOTFOUND);
                            }
                            else
                            {
                                if (err == ERROR_INVALID_PARAMETER || err == ERROR_NOT_READY)
                                {
                                    std::wstring drive = copy;
                                    if (CutDirectoryW(drive))
                                    {
                                        DWORD attrs = gFileSystem->GetFileAttributes(drive.c_str());
                                        if (attrs != INVALID_FILE_ATTRIBUTES &&
                                            (attrs & FILE_ATTRIBUTE_DIRECTORY) &&
                                            (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
                                        {
                                            UINT drvType;
                                            if (copy[0] == '\\' && copy[1] == '\\')
                                            {
                                                drvType = DRIVE_REMOTE;
                                                drive = GetRootPath(copy.c_str());
                                                if (!drive.empty() && drive.back() == L'\\')
                                                    drive.pop_back();
                                            }
                                            else
                                            {
                                                drive.assign(1, copy[0]);
                                                drvType = MyGetDriveTypeW(copy.c_str());
                                            }
                                            if (drvType != DRIVE_REMOTE)
                                            {
                                                std::wstring currentReparsePoint;
                                                GetCurrentLocalReparsePointW(copy.c_str(), currentReparsePoint);
                                                CheckPathRootWithRetryMsgBox = currentReparsePoint;
                                                if (currentReparsePoint.length() > 3)
                                                {
                                                    drive = currentReparsePoint;
                                                    SalPathRemoveBackslashW(drive);
                                                }
                                            }
                                            else
                                                CheckPathRootWithRetryMsgBox = GetRootPath(copy.c_str());
                                            errBuf = FormatStrW(LoadStrW(IDS_NODISKINDRIVE), drive.c_str());
                                            int msgboxRes = (int)CDriveSelectErrDlg(HWindow, errBuf.c_str(), copy.c_str()).Execute();
                                            CheckPathRootWithRetryMsgBox.clear();
                                            UpdateWindow(MainWindow->HWindow);
                                            if (msgboxRes == IDRETRY)
                                                goto _TRY_AGAIN;
                                            showErr = FALSE;
                                        }
                                    }
                                }
                                text = GetErrorTextOwned(err).c_str();
                            }
                            textFailReason = CHPPFR_INVALIDPATH;
                            break;
                        }
                    }
                    path = std::move(copy); // taking a new path
                }
            }

            if (!text.empty())
            {
                if (showErr)
                {
                    const wchar_t* pathForMsg = showNewDirPathInErrBoxes && newDir != NULL ? newDir : path.c_str();
                    std::wstring msg = FormatStrW(LoadStrW(IDS_PATHERRORFORMAT), pathForMsg, text.c_str());
                    gPrompter->ShowError(LoadStrW(IDS_ERRORCHANGINGDIR), msg.c_str());
                }
                if (newDir != NULL)
                {
                    if (useStopRefresh)
                        EndStopRefresh(); // snopper will be started again
                    if (failReason != NULL)
                        *failReason = textFailReason;
                    return FALSE; // finished, cannot be repeated
                }
                pathW = path;
                goto CHANGE_AGAIN;
            }
            else
            {
                // changing the path to absolute windows path
                BOOL ret = ChangePathToDisk(HWindow, path.c_str(), suggestedTopIndex, suggestedFocusName,
                                            NULL, TRUE, FALSE, FALSE, failReason);
                if (useStopRefresh)
                    EndStopRefresh(); // snooper will be started again
                return ret;
            }
        }
    }
    UpdateWindow(MainWindow->HWindow);
    if (useStopRefresh)
        EndStopRefresh(); // snooper will be started again
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CFilesWindow::ChangeDirLite(const wchar_t* newDir)
{
    int failReason;
    BOOL ret = ChangeDir(newDir, -1, NULL, 3, &failReason, TRUE);
    return ret || failReason == CHPPFR_SHORTERPATH || failReason == CHPPFR_FILENAMEFOCUSED ||
           failReason == CHPPFR_SUCCESS /* non-sense, but we are not bothered by it */;
}

BOOL CFilesWindow::ChangePathToDrvType(HWND parent, int driveType, const wchar_t* displayName)
{
    std::wstring path;
    const std::wstring* userFolderOneDrive = NULL;
    if (driveType == drvtOneDrive || driveType == drvtOneDriveBus)
    {
        InitOneDrivePath();
        if (driveType == drvtOneDrive && OneDrivePath.empty() ||
            driveType == drvtOneDriveBus && !OneDriveBusinessStorages.Find(displayName, &userFolderOneDrive))
        { // OneDrive Personal/Business has been disconnected, we will refresh Drive bars, so that its icon disappears or is updated
            if (MainWindow != NULL && MainWindow->HWindow != NULL)
                PostMessage(MainWindow->HWindow, WM_USER_DRIVES_CHANGE, 0, 0);
            return FALSE;
        }
    }
    if (driveType == drvtMyDocuments)
    {
        // real fix: GetMyDocumentsOrDesktopPathW already exists and is honest for a
        // Documents/Desktop path the active code page cannot spell (e.g. an accented user name),
        // unlike SHGetPathFromIDList's ANSI best-fit below.
        std::wstring pathW;
        if (GetMyDocumentsOrDesktopPathW(pathW))
            return ChangePathToDisk(parent, pathW.c_str());
        return FALSE;
    }
    if (driveType == drvtOneDrive)
    {
        if (!OneDrivePath.empty())
            return ChangePathToDisk(parent, OneDrivePath.c_str());
        return FALSE;
    }
    if (driveType == drvtOneDriveBus)
    {
        // The business account registry record and its selection identity are UTF-16 end to end.
        if (userFolderOneDrive != NULL && !userFolderOneDrive->empty())
            return ChangePathToDisk(parent, userFolderOneDrive->c_str());
        return FALSE;
    }
    if (driveType == drvtDropbox)
    {
        if (!DropboxPath.empty())
            return ChangePathToDisk(parent, DropboxPath.c_str());
        return FALSE;
    }
    if (driveType == drvtGoogleDrive && ShellIconOverlays.GetPathForGoogleDrive(path))
    {
        return ChangePathToDisk(parent, path.c_str());
    }
    return FALSE;
}

void CFilesWindow::ChangeDrive(wchar_t drive)
{
    CALL_STACK_MESSAGE2("CFilesWindow::ChangeDrive(%u)", drive);
    //--- DefaultDire refresh
    MainWindow->UpdateDefaultDir(MainWindow->GetActivePanel() != this);
    //---  possible disk selection from the dialog
    CFilesWindow* anotherPanel = (Parent->LeftPanel == this ? Parent->RightPanel : Parent->LeftPanel);
    if (drive == 0)
    {
        CDriveTypeEnum driveType = drvtUnknow; // dummy
        DWORD_PTR driveTypeParam = (Is(ptDisk) || Is(ptZIPArchive)) ? GetPathW()[0] : 0;
        int postCmd;
        void* postCmdParam;
        BOOL fromContextMenu;
        if (CDrivesList(this, ((Is(ptDisk) || Is(ptZIPArchive)) ? GetPathW() : L""),
                        &driveType, &driveTypeParam, &postCmd, &postCmdParam,
                        &fromContextMenu)
                    .Track() == FALSE ||
            fromContextMenu && postCmd == 0) // only close-menu executed from context menu
        {
            return;
        }

        if (!fromContextMenu && driveType != drvtPluginFS && driveType != drvtPluginCmd)
            TopIndexMem.Clear(); // long jump

        UpdateWindow(MainWindow->HWindow);

        switch (driveType)
        {
        case drvtMyDocuments:
        case drvtGoogleDrive:
        case drvtDropbox:
        case drvtOneDrive:
        case drvtOneDriveBus:
        {
            ChangePathToDrvType(HWindow, driveType, driveType == drvtOneDriveBus ? (const wchar_t*)driveTypeParam : NULL);
            if (driveType == drvtOneDriveBus)
                free((wchar_t*)driveTypeParam);
            return;
        }

        case drvtOneDriveMenu:
            TRACE_E("CFilesWindow::ChangeDrive(): unexpected drive type: drvtOneDriveMenu");
            break;

        // is it Network?
        case drvtNeighborhood:
        {
            // wide: GetTargetDirectory's browse dialog (SHBrowseForFolder/
            // SHGetPathFromIDList, unqualified - ANSI since core never defines UNICODE)
            // best-fit-narrows the chosen server/share name before Sally ever sees it - same
            // shape and fix as toolbar_drive_bar.cpp's CDriveBar::Execute and
            // this file's own sibling keyboard-shortcut handler in files_window_navigation.cpp
            //. 'path' was used only by this case, so it's replaced outright.
            std::wstring pathW;
            if (GetTargetDirectoryW(HWindow, HWindow, LoadStrW(IDS_CHANGEDRIVE),
                                    LoadStrW(IDS_CHANGEDRIVETEXT), pathW, TRUE))
            {
                UpdateWindow(MainWindow->HWindow);
                ChangePathToDisk(HWindow, pathW.c_str());
                return;
            }
            else
                return;
        }

        // is it other Panel?
        case drvtOtherPanel:
        {
            ChangePathToOtherPanelPath();
            return;
        }

        // is it Hot Path?
        case drvtHotPath:
        {
            GotoHotPath((int)driveTypeParam);
            return;
        }

        // it's a normal path
        case drvtUnknow:
        case drvtRemovable:
        case drvtFixed:
        case drvtRemote:
        case drvtCDROM:
        case drvtRAMDisk:
        {
            drive = (wchar_t)LOWORD(driveTypeParam);

            UpdateWindow(MainWindow->HWindow);
            break;
        }

        case drvtPluginFS:
        {
            CPluginFSInterfaceAbstract* fs = (CPluginFSInterfaceAbstract*)driveTypeParam;
            CPluginInterfaceForFSEncapsulation* ifaceForFS = NULL;
            // need to verify that 'fs' is still a valid interface
            if (Is(ptPluginFS) && GetPluginFS()->GetInterface() == fs)
            { // select active FS - perform refresh
                if (!fromContextMenu)
                    RefreshDirectory();
                else
                    ifaceForFS = GetPluginFS()->GetPluginInterfaceForFS();
            }
            else
            {
                CDetachedFSList* list = MainWindow->DetachedFSList;
                int i;
                for (i = 0; i < list->Count; i++)
                {
                    if (list->At(i)->GetInterface() == fs)
                    { // select detached FS
                        if (!fromContextMenu)
                            ChangePathToDetachedFS(i);
                        else
                            ifaceForFS = list->At(i)->GetPluginInterfaceForFS();
                        break;
                    }
                }
            }

            if (ifaceForFS != NULL) // post-cmd from context menu of active/detached FS
            {
                ifaceForFS->ExecuteChangeDrivePostCommand(PANEL_SOURCE, postCmd, postCmdParam);
            }
            return;
        }

        case drvtPluginFSInOtherPanel:
            return; // illegal action (FS from other panel cannot be given to active panel)

        case drvtPluginCmd:
        {
            const wchar_t* dllName = (const wchar_t*)driveTypeParam;
            CPluginData* data = Plugins.GetPluginData(dllName);
            if (data != NULL) // plugin exists, we can execute the command
            {
                if (!fromContextMenu) // FS item command
                {
                    data->ExecuteChangeDriveMenuItem(PANEL_SOURCE);
                }
                else // post-cmd from context menu of FS item
                {
                    data->GetPluginInterfaceForFS()->ExecuteChangeDrivePostCommand(PANEL_SOURCE, postCmd, postCmdParam);
                }
            }
            return;
        }

        default:
        {
            TRACE_E("Unknown DriveType = " << driveType);
            return;
        }
        }
    }
    else
        TopIndexMem.Clear(); // long jump

    ChangePathToDisk(HWindow, DefaultDir[towlower(drive) - L'a'].c_str(), -1, NULL,
                     NULL, TRUE, FALSE, FALSE, NULL, FALSE);
}

void CFilesWindow::UpdateFilterSymbol()
{
    CALL_STACK_MESSAGE_NONE
    DirectoryLine->SetHidden(HiddenFilesCount, HiddenDirsCount);
}

void CFilesWindow::UpdateDriveIcon(BOOL check)
{
    CALL_STACK_MESSAGE2("CFilesWindow::UpdateDriveIcon(%d)", check);
    if (Is(ptDisk))
    {
        if (!check || CheckPath(FALSE) == ERROR_SUCCESS)
        { // only if the path is accessible
            if (DirectoryLine->HWindow != NULL)
            {
                // MyGetDriveType(the removed ANSI mirror) calls GetDriveTypeA on an
                // already-lossy narrow mirror - for a UNC path whose server/share name isn't
                // representable in CP_ACP, this can misreport the drive type and silently pick
                // the wrong drive-line icon. MyGetDriveTypeW/GetPathW() already used for the
                // identical purpose a few lines above in this same file (line ~169).
                UINT type = MyGetDriveTypeW(GetPathW());
                const std::wstring root = GetRootPath(GetPathW());
                HICON hIcon = GetDriveIconW(root.c_str(), type, TRUE);
                DirectoryLine->SetDriveIcon(hIcon);
                HANDLES(DestroyIcon(hIcon));
            }
            // 2.5RC3: the button in drive bars must be set even if directory line is disabled
            MainWindow->UpdateDriveBars(); // press the correct disk in drive bar
        }
    }
    else
    {
        if (Is(ptZIPArchive))
        {
            if (DirectoryLine->HWindow != NULL)
            {
                HICON hIcon = LoadArchiveIcon(IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags);
                DirectoryLine->SetDriveIcon(hIcon);
                HANDLES(DestroyIcon(hIcon));
            }
        }
        else
        {
            if (Is(ptPluginFS))
            {
                if (DirectoryLine->HWindow != NULL)
                {
                    BOOL destroyIcon;
                    HICON icon = GetPluginFS()->GetFSIcon(destroyIcon);
                    if (icon != NULL) // defined by plugin
                    {
                        DirectoryLine->SetDriveIcon(icon);
                        if (destroyIcon)
                            HANDLES(DestroyIcon(icon));
                    }
                    else // standard
                    {
                        icon = SalLoadIcon(HInstance, IDI_PLUGINFS, IconSizes[ICONSIZE_16]);
                        DirectoryLine->SetDriveIcon(icon);
                        HANDLES(DestroyIcon(icon));
                    }
                }
                // 2.5RC3: the button in drive bars must be set even if directory line is disabled
                MainWindow->UpdateDriveBars(); // press the correct disk in drive bar
            }
        }
    }
}
