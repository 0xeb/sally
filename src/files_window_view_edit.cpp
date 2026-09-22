// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "filesbox.h"
#include "dialogs.h"
#include "snooper.h"
#include "worker.h"
#include "cache.h"
#include "usermenu.h"
#include "execute.h"
#include "pack.h"
#include "viewer.h"
#include "codetbl.h"
#include "find.h"
#include "menu.h"
#include "common/widepath.h"
#include "common/fsutil.h"
#include "common/CreateDirectoryFlow.h"
#include "common/ViewerEditorLauncher.h"
#include "ui/IPrompter.h"
#include "common/IFileSystem.h"
#include "common/DiagnosticTextEncoding.h"
#include "common/unicode/helpers.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/unicode/PathIdentityPolicy.h"
#include "common/unicode/RenameRetryPolicy.h"
#include "common/text/CaseFolding.h"
#include <vector>

namespace
{
BOOL FileNameInvalidForManualCreateW(const wchar_t* path)
{
    const wchar_t* name = wcsrchr(path, L'\\');
    if (name != NULL)
    {
        name++;
        int nameLen = (int)wcslen(name);
        return nameLen > 0 && (*name <= L' ' || name[nameLen - 1] <= L' ' || name[nameLen - 1] == L'.');
    }
    return FALSE;
}

} // namespace

//
// ****************************************************************************
// CFilesWindow
//

void CFilesWindow::Convert()
{
    CALL_STACK_MESSAGE1("CFilesWindow::Convert()");
    if (Dirs->Count + Files->Count == 0)
        return;
    BeginStopRefresh(); // snooper takes a break

    if (!FilesActionInProgress)
    {
        FilesActionInProgress = TRUE;

        BOOL subDir;
        if (Dirs->Count > 0)
            subDir = (wcscmp(Dirs->At(0).Name, L"..") == 0);
        else
            subDir = FALSE;

        CConvertFilesDlg convertDlg(HWindow, SelectionContainsDirectory());
        while (1)
        {
            if (convertDlg.Execute() == IDOK)
            {
                UpdateWindow(MainWindow->HWindow);
                if (convertDlg.CodeType == 0 && convertDlg.EOFType == 0)
                    break; // nothing to do

                CCriteriaData filter;
                filter.UseMasks = TRUE;
                filter.Masks.SetMasksString(convertDlg.Mask.c_str());
                int errpos = 0;
                if (!filter.Masks.PrepareMasks(errpos))
                    break; // invalid mask

                if (CheckPath(TRUE) != ERROR_SUCCESS) // the path we need to work on failed
                {
                    FilesActionInProgress = FALSE;
                    EndStopRefresh(); // snooper will start again now
                    return;
                }

                CConvertData dlgData;

                dlgData.EOFType = convertDlg.EOFType;

                // the CodeTables object was initialized in the Convert dialog
                if (!CodeTables.GetCode(dlgData.CodeTable, convertDlg.CodeType))
                {
                    // if we fail to obtain the encoding table or no encoding is selected,
                    // perform one-to-one encoding, i.e. no conversion
                    int i;
                    for (i = 0; i < 256; i++)
                        dlgData.CodeTable[i] = i;
                }

                //---  determine which directories and files are selected
                std::unique_ptr<int[]> indexes; // RAII: auto-deleted when scope exits
                CFileData* f = NULL;
                int count = GetSelCount();
                if (count != 0)
                {
                    indexes = std::make_unique<int[]>(count);
                    GetSelItems(count, indexes.get());
                }
                else // nothing is selected
                {
                    int i = GetCaretIndex();
                    if (i < 0 || i >= Dirs->Count + Files->Count)
                    {
                        FilesActionInProgress = FALSE;
                        EndStopRefresh(); // snooper will start again now
                        return;           // invalid index (no files)
                    }
                    if (i == 0 && subDir)
                    {
                        FilesActionInProgress = FALSE;
                        EndStopRefresh(); // snooper will start again now
                        return;           // we do not work with ".."
                    }
                    f = (i < Dirs->Count) ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                }
                //---
                COperations* script = new COperations(1000, 500, NULL, NULL, NULL);
                if (script == NULL)
                    TRACE_E(LOW_MEMORY);
                else
                {
                    HWND hFocusedWnd = GetFocus();
                    CreateSafeWaitWindow(LoadStrW(IDS_ANALYSINGDIRTREEESC), NULL, 1000, TRUE, MainWindow->HWindow);
                    EnableWindow(MainWindow->HWindow, FALSE);

                    HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

                    BOOL res = BuildScriptMain(script, convertDlg.SubDirs ? atRecursiveConvert : atConvert,
                                               NULL, NULL, count, indexes.get(), f, NULL, NULL, FALSE, &filter);
                    // ReanchorWideSourcePaths deleted — snapshot-
                    // built ops carry explicit wide names; nothing to repair.
                    if (script->Count == 0)
                        res = FALSE;
                    // reordered to allow the main window to activate (must not be disabled), otherwise it switches to another app
                    EnableWindow(MainWindow->HWindow, TRUE);
                    DestroySafeWaitWindow();

                    // if Salamander is active, call SetFocus on the stored window (SetFocus fails
                    // if the main window is disabled - after deactivation/activation of the disabled main window the active panel
                    // does not have focus)
                    HWND hwnd = GetForegroundWindow();
                    while (hwnd != NULL && hwnd != MainWindow->HWindow)
                        hwnd = GetParent(hwnd);
                    if (hwnd == MainWindow->HWindow)
                        SetFocus(hFocusedWnd);

                    SetCursor(oldCur);

                    // prepare refresh of manually refreshed directories
                    // change in the directory displayed in the panel and also in subdirectories if work was done there as well
                    script->SetWorkPath1W(GetPathW(), convertDlg.SubDirs);

                if (!res || !StartProgressDialog(script, LoadStrW(IDS_CONVERTTITLE), NULL, &dlgData))
                {
                    if (script->IsGood() && script->Count == 0)
                    {
                        gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_NOFILE_MATCHEDMASK));

                        SetSel(FALSE, -1, TRUE);                        // explicit repaint
                        PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // selection change notify
                    }
                    UpdateWindow(MainWindow->HWindow);
                    if (!script->IsGood())
                        script->ResetState();
                    FreeScript(script);
                }
                    else // removal of selection index (no waiting for operation finish, operation runs in another thread)
                    {
                        SetSel(FALSE, -1, TRUE);                        // explicit repaint
                        PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // selection change notify
                        UpdateWindow(MainWindow->HWindow);
                    }
                }
                // RAII: indexes auto-deleted when scope exits
            }
            UpdateWindow(MainWindow->HWindow);
            break;
        }
        FilesActionInProgress = FALSE;
    }
    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::ChangeAttr(BOOL setCompress, BOOL compressed, BOOL setEncryption, BOOL encrypted)
{
    CALL_STACK_MESSAGE5("CFilesWindow::ChangeAttr(%d, %d, %d, %d)", setCompress, compressed, setEncryption, encrypted);
    if (Dirs->Count + Files->Count == 0)
        return;
    int selectedCount = GetSelCount();
    if (selectedCount == 0 || selectedCount == 1)
    {
        int index;
        if (selectedCount == 0)
            index = GetCaretIndex();
        else
            GetSelItems(1, &index);
        // focus is on UpDir -- nothing to convert
        if (Dirs->Count > 0 && index == 0 && wcscmp(Dirs->At(0).Name, L"..") == 0)
            return;
    }
    BeginStopRefresh(); // snooper takes a break

    // if no item is selected, select the one under focus and store its name
    std::wstring temporarySelected;
    if ((!setCompress || Configuration.CnfrmNTFSPress) &&
        (!setEncryption || Configuration.CnfrmNTFSCrypt))
    {
        temporarySelected = SelectFocusedItemAndGetName();
    }

    if (Is(ptDisk))
    {
        if (!FilesActionInProgress)
        {
            FilesActionInProgress = TRUE;

            BOOL subDir;
            if (Dirs->Count > 0)
                subDir = (wcscmp(Dirs->At(0).Name, L"..") == 0);
            else
                subDir = FALSE;

            DWORD attr, attrDiff;
            SYSTEMTIME timeModified;
            SYSTEMTIME timeCreated;
            SYSTEMTIME timeAccessed;
            if (!setCompress && !setEncryption)
            {
                int count = GetSelCount();
                if (count == 1 || count == 0)
                {
                    int index;
                    if (count == 0)
                        index = GetCaretIndex();
                    else
                        GetSelItems(1, &index);
                    if (index >= 0 && index < Files->Count + Dirs->Count)
                    {
                        CFileData* f = (index < Dirs->Count) ? &Dirs->At(index) : &Files->At(index - Dirs->Count);
                        if (wcscmp(f->Name, L"..") != 0)
                        {
                            BOOL isDir = index < Dirs->Count;

                            BOOL timeObtained = FALSE;

                            // BuildPathW(const char*, const char*) narrows the removed ANSI mirror
                            // and f->Name via MultiByteToWideChar - but both are ALREADY lossy CP_ACP
                            // mirrors, so that only re-widens strings that never recovered their true
                            // characters. For a panel path or filename outside CP_ACP this named a
                            // nonexistent/wrong file, GetFileInfoW silently failed, and the dialog fell
                            // back to collapsing Created/Accessed/Modified into the cached f->LastWrite -
                            // wrong data with no visible error. Same wide-native idiom already used for
                            // ViewFile a few hundred lines below in this file.
                            std::wstring fullPath = BuildPathW(GetPathW(), f->Name);
                            SalFileInfo fileInfo = GetFileInfoW(fullPath.c_str());
                            if (fileInfo.IsValid)
                            {
                                FILETIME ft;
                                if (FileTimeToLocalFileTime(&fileInfo.CreationTime, &ft) &&
                                    FileTimeToSystemTime(&ft, &timeCreated) &&
                                    FileTimeToLocalFileTime(&fileInfo.LastAccessTime, &ft) &&
                                    FileTimeToSystemTime(&ft, &timeAccessed) &&
                                    FileTimeToLocalFileTime(&fileInfo.LastWriteTime, &ft) &&
                                    FileTimeToSystemTime(&ft, &timeModified))
                                {
                                    timeObtained = TRUE;
                                }
                            }
                            if (!timeObtained)
                            {
                                // if we failed to obtain the time from the file, use at least the one we have
                                FILETIME ft;
                                if (!FileTimeToLocalFileTime(&f->LastWrite, &ft) ||
                                    !FileTimeToSystemTime(&ft, &timeModified))
                                {
                                    GetLocalTime(&timeModified); // the time we have is invalid, use the current time
                                }
                                timeCreated = timeModified;
                                timeAccessed = timeModified;
                            }

                            attr = f->Attr;
                            attrDiff = 0;
                            count = -1;
                        }
                    }
                }
                if (count != -1)
                {
                    GetLocalTime(&timeModified);
                    timeAccessed = timeModified;
                    timeCreated = timeModified;
                    attr = 0;
                    attrDiff = 0;
                    BOOL first = TRUE;

                    int totalCount = Dirs->Count + Files->Count;
                    CFileData* f;
                    int i;
                    for (i = 0; i < totalCount; i++)
                    {
                        BOOL isDir = i < Dirs->Count;
                        f = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                        if (i == 0 && isDir && wcscmp(Dirs->At(0).Name, L"..") == 0)
                            continue;
                        if (f->Selected == 1)
                        {
                            if (first)
                            {
                                attr = f->Attr;
                                first = FALSE;
                            }
                            else
                            {
                                if (f->Attr != attr)
                                    attrDiff |= f->Attr ^ attr;
                            }
                        }
                    }
                }
            }

            CChangeAttrDialog chDlg(HWindow, attr, attrDiff,
                                    SelectionContainsDirectory(), FileBasedCompression,
                                    FileBasedEncryption,
                                    &timeModified, &timeCreated, &timeAccessed);
            if (setCompress || setEncryption)
            {
                chDlg.Archive = 2;
                chDlg.ReadOnly = 2;
                chDlg.Hidden = 2;
                chDlg.System = 2;
                if (setCompress)
                {
                    chDlg.Compressed = compressed;
                    chDlg.Encrypted = compressed ? 0 : 2; // compression excludes encryption; without compression encryption may remain as is
                }
                else
                {
                    chDlg.Compressed = encrypted ? 0 : 2; // encryption excludes compression; without encryption compression may remain as is
                    chDlg.Encrypted = encrypted;
                }
                chDlg.ChangeTimeModified = FALSE;
                chDlg.ChangeTimeCreated = FALSE;
                chDlg.ChangeTimeAccessed = FALSE;
                chDlg.RecurseSubDirs = TRUE;

                if (setCompress && Configuration.CnfrmNTFSPress || // ask whether to compress/decompress
                    setEncryption && Configuration.CnfrmNTFSCrypt) // ask whether to encrypt/decrypt
                {
                    // CMessageBox::DialogProc reads Text.GetW() unconditionally
                    // (msgbox.cpp) - CTruncatedString::Set() (narrow) always leaves UseWideText
                    // FALSE, so this NTFS compress/encrypt confirmation showed a completely
                    // blank body. Same bug class as the earlier 8ac53e4d and the earlier Pack
                    // fix. Built wide throughout; subject/expanded/path (narrow) had no other
                    // consumer in this block, so they're replaced rather than duplicated.
                    std::wstring expandedW;
                    int count = GetSelCount();
                    std::wstring pathW;
                    if (count > 1)
                    {
                        int totalCount = Dirs->Count + Files->Count;
                        int files = 0;
                        int dirs = 0;
                        CFileData* f;
                        int i;
                        for (i = 0; i < totalCount; i++)
                        {
                            BOOL isDir = i < Dirs->Count;
                            f = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                            if (i == 0 && isDir && wcscmp(Dirs->At(0).Name, L"..") == 0)
                                continue;
                            if (f->Selected == 1)
                            {
                                if (isDir)
                                    dirs++;
                                else
                                    files++;
                            }
                        }

                        expandedW = ExpandPluralFilesDirsTextW(files, dirs, epfdmNormal, FALSE);
                    }
                    else
                    {
                        int index;
                        if (count == 0)
                            index = GetCaretIndex();
                        else
                            GetSelItems(1, &index);

                        BOOL isDir = index < Dirs->Count;
                        CFileData* f = isDir ? &Dirs->At(index) : &Files->At(index - Dirs->Count);
                        pathW = AlterFileNameW(f->Name,
                                               Configuration.FileNameFormat, 0, index < Dirs->Count);
                        expandedW = LoadStrW(isDir ? IDS_QUESTION_DIRECTORY : IDS_QUESTION_FILE);
                    }
                    int resTextID;
                    int resTitleID;
                    if (setCompress)
                    {
                        resTextID = compressed ? IDS_CONFIRM_NTFSCOMPRESS : IDS_CONFIRM_NTFSUNCOMPRESS;
                        resTitleID = compressed ? IDS_CONFIRM_NTFSCOMPRESS_TITLE : IDS_CONFIRM_NTFSUNCOMPRESS_TITLE;
                    }
                    else
                    {
                        resTextID = encrypted ? IDS_CONFIRM_NTFSENCRYPT : IDS_CONFIRM_NTFSDECRYPT;
                        resTitleID = encrypted ? IDS_CONFIRM_NTFSENCRYPT_TITLE : IDS_CONFIRM_NTFSDECRYPT_TITLE;
                    }
                    std::wstring subjectW = FormatStrW(LoadStrW(resTextID), expandedW.c_str());
                    CTruncatedString str;
                    str.SetW(subjectW.c_str(), count > 1 ? NULL : pathW.c_str());
                    CMessageBox msgBox(HWindow, MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT,
                                       LoadStrW(resTitleID), &str, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL);
                    if (msgBox.Execute() != IDYES)
                    {
                        // if we selected an item, deselect it again
                        UnselectItemWithName(temporarySelected);
                        FilesActionInProgress = FALSE;
                        EndStopRefresh(); // snooper will start again now
                        return;
                    }
                    UpdateWindow(MainWindow->HWindow);
                }
            }
            if (setCompress || setEncryption || chDlg.Execute() == IDOK)
            {
                UpdateWindow(MainWindow->HWindow);

                if (CheckPath(TRUE) != ERROR_SUCCESS)
                {
                    // if we selected an item, we deselect it again
                    UnselectItemWithName(temporarySelected);
                    FilesActionInProgress = FALSE;
                    EndStopRefresh(); // snooper will start again now
                    return;
                }

                CChangeAttrsData dlgData;
                dlgData.ChangeTimeModified = chDlg.ChangeTimeModified;
                if (dlgData.ChangeTimeModified)
                {
                    FILETIME ft;
                    SystemTimeToFileTime(&chDlg.TimeModified, &ft);
                    LocalFileTimeToFileTime(&ft, &dlgData.TimeModified);
                }
                dlgData.ChangeTimeCreated = chDlg.ChangeTimeCreated;
                if (dlgData.ChangeTimeCreated)
                {
                    FILETIME ft;
                    SystemTimeToFileTime(&chDlg.TimeCreated, &ft);
                    LocalFileTimeToFileTime(&ft, &dlgData.TimeCreated);
                }
                dlgData.ChangeTimeAccessed = chDlg.ChangeTimeAccessed;
                if (dlgData.ChangeTimeAccessed)
                {
                    FILETIME ft;
                    SystemTimeToFileTime(&chDlg.TimeAccessed, &ft);
                    LocalFileTimeToFileTime(&ft, &dlgData.TimeAccessed);
                }
                //---  determine which directories and files are selected
                std::unique_ptr<int[]> indexes; // RAII: auto-deleted when scope exits
                CFileData* f = NULL;
                int count = GetSelCount();
                if (count != 0)
                {
                    indexes = std::make_unique<int[]>(count);
                    GetSelItems(count, indexes.get());
                }
                else // nothing is selected
                {
                    int i = GetCaretIndex();
                    if (i < 0 || i >= Dirs->Count + Files->Count)
                    {
                        // if we selected an item, we deselect it again
                        UnselectItemWithName(temporarySelected);
                        FilesActionInProgress = FALSE;
                        EndStopRefresh(); // snooper will start again now
                        return;           // invalid index (no files)
                    }
                    if (i == 0 && subDir)
                    {
                        // if we selected an item, we deselect it again
                        UnselectItemWithName(temporarySelected);
                        FilesActionInProgress = FALSE;
                        EndStopRefresh(); // snooper will start again now
                        return;           // we do not work with ".."
                    }
                    f = (i < Dirs->Count) ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                }
                //---
                COperations* script = new COperations(1000, 500, NULL, NULL, NULL);
                if (script == NULL)
                    TRACE_E(LOW_MEMORY);
                else
                {
                    HWND hFocusedWnd = GetFocus();
                    CreateSafeWaitWindow(LoadStrW(IDS_ANALYSINGDIRTREEESC), NULL, 1000, TRUE, MainWindow->HWindow);
                    EnableWindow(MainWindow->HWindow, FALSE);

                    HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

                    // ensure a correct relationship between Compressed and Encrypted
                    if (chDlg.Encrypted == 1)
                    {
                        if (chDlg.Compressed != 0)
                            TRACE_E("CFilesWindow::ChangeAttr(): unexpected value of chDlg.Compressed!");
                        chDlg.Compressed = 0;
                    }
                    else
                    {
                        if (chDlg.Compressed == 1)
                        {
                            if (chDlg.Encrypted != 0)
                                TRACE_E("CFilesWindow::ChangeAttr(): unexpected value of chDlg.Encrypted!");
                            chDlg.Encrypted = 0;
                        }
                    }

                    CAttrsData attrsData;
                    attrsData.AttrAnd = 0xFFFFFFFF;
                    attrsData.AttrOr = 0;
                    attrsData.SubDirs = chDlg.RecurseSubDirs;
                    attrsData.ChangeCompression = FALSE;
                    attrsData.ChangeEncryption = FALSE;
                    dlgData.ChangeCompression = FALSE;
                    dlgData.ChangeEncryption = FALSE;

                    if (chDlg.Archive == 0)
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_ARCHIVE);
                    if (chDlg.ReadOnly == 0)
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_READONLY);
                    if (chDlg.Hidden == 0)
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_HIDDEN);
                    if (chDlg.System == 0)
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_SYSTEM);
                    if (chDlg.Compressed == 0)
                    {
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_COMPRESSED);
                        attrsData.ChangeCompression = TRUE;
                        dlgData.ChangeCompression = TRUE;
                    }
                    if (chDlg.Encrypted == 0)
                    {
                        attrsData.AttrAnd &= ~(FILE_ATTRIBUTE_ENCRYPTED);
                        attrsData.ChangeEncryption = TRUE;
                        dlgData.ChangeEncryption = TRUE;
                    }

                    if (chDlg.Archive == 1)
                        attrsData.AttrOr |= FILE_ATTRIBUTE_ARCHIVE;
                    if (chDlg.ReadOnly == 1)
                        attrsData.AttrOr |= FILE_ATTRIBUTE_READONLY;
                    if (chDlg.Hidden == 1)
                        attrsData.AttrOr |= FILE_ATTRIBUTE_HIDDEN;
                    if (chDlg.System == 1)
                        attrsData.AttrOr |= FILE_ATTRIBUTE_SYSTEM;
                    if (chDlg.Compressed == 1)
                    {
                        attrsData.AttrOr |= FILE_ATTRIBUTE_COMPRESSED;
                        attrsData.ChangeCompression = TRUE;
                        dlgData.ChangeCompression = TRUE;
                    }
                    if (chDlg.Encrypted == 1)
                    {
                        attrsData.AttrOr |= FILE_ATTRIBUTE_ENCRYPTED;
                        attrsData.ChangeEncryption = TRUE;
                        dlgData.ChangeEncryption = TRUE;
                    }

                    script->ClearReadonlyMask = 0xFFFFFFFF;
                    BOOL res = BuildScriptMain(script, atChangeAttrs, NULL, NULL, count,
                                               indexes.get(), f, &attrsData, NULL, FALSE, NULL);
                    if (script->Count == 0)
                        res = FALSE;
                    // reordered to allow the main window to activate (must not be disabled), otherwise it switches to another app
                    EnableWindow(MainWindow->HWindow, TRUE);
                    DestroySafeWaitWindow();

                    // if Salamander is active, call SetFocus on the stored window (SetFocus fails
                    // if the main window is disabled - after deactivation/activation of the disabled main window the active panel
                    // does not have focus)
                    HWND hwnd = GetForegroundWindow();
                    while (hwnd != NULL && hwnd != MainWindow->HWindow)
                        hwnd = GetParent(hwnd);
                    if (hwnd == MainWindow->HWindow)
                        SetFocus(hFocusedWnd);

                    SetCursor(oldCur);

                    // prepare refresh of manually refreshed directories
                    // change in the directory displayed in the panel and also in subdirectories if work was done there as well
                    script->SetWorkPath1W(GetPathW(), chDlg.RecurseSubDirs);

                    if (!res || !StartProgressDialog(script, LoadStrW(IDS_CHANGEATTRSTITLE), &dlgData, NULL))
                    {
                        UpdateWindow(MainWindow->HWindow);
                        if (!script->IsGood())
                            script->ResetState();
                        FreeScript(script);
                    }
                    else // removal of selection index (no waiting for operation finish, operation runs in another thread)
                    {
                        SetSel(FALSE, -1, TRUE);                        // explicit repaint
                        PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // selection change notify
                        UpdateWindow(MainWindow->HWindow);
                    }
                }
                // RAII: indexes auto-deleted when scope exits
            }
            UpdateWindow(MainWindow->HWindow);
            FilesActionInProgress = FALSE;
        }
    }
    else
    {
        if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
            GetPluginFS()->IsServiceSupported(FS_SERVICE_CHANGEATTRS)) // FS is in the panel
        {
            // lower the thread priority to "normal" (so the operations don't overload the machine)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

            int panel = MainWindow->LeftPanel == this ? PANEL_LEFT : PANEL_RIGHT;

            int count = GetSelCount();
            int selectedDirs = 0;
            if (count > 0)
            {
                // count how many directories are selected (the rest of the selected items are files)
                int i;
                for (i = 0; i < Dirs->Count; i++) // ".." cannot be selected, the check would be unnecessary
                {
                    if (Dirs->At(i).Selected)
                        selectedDirs++;
                }
            }
            else
                count = 0;

            BOOL success = GetPluginFS()->ChangeAttributes(GetPluginFS()->GetPluginFSName(), HWindow,
                                                           panel, count - selectedDirs, selectedDirs);

            // raise the thread priority again, the operation has finished
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

            if (success) // success -> unselect
            {
                SetSel(FALSE, -1, TRUE);                        // explicit repaint
                PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // selection change notify
                UpdateWindow(MainWindow->HWindow);
            }
        }
    }
    // if we selected an item, we deselect it again
    UnselectItemWithName(temporarySelected);

    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::FindFile()
{
    CALL_STACK_MESSAGE1("CFilesWindow::FindFile()");
    if (Is(ptDisk) && CheckPath(TRUE) != ERROR_SUCCESS)
        return;

    if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
        GetPluginFS()->IsServiceSupported(FS_SERVICE_OPENFINDDLG))
    { // try to open Find for the FS in the panel; if it succeeds, there is no point in opening the standard Find dialog
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
        BOOL done = GetPluginFS()->OpenFindDialog(GetPluginFS()->GetPluginFSName(),
                                                  this == MainWindow->LeftPanel ? PANEL_LEFT : PANEL_RIGHT);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        if (done)
            return;
    }

    if (SystemPolicies.GetNoFind() || SystemPolicies.GetNoShellSearchButton())
    {
        gPrompter->ShowErrorWithHelp(LoadStrW(IDS_POLICIESRESTRICTION_TITLE),
                                     LoadStrW(IDS_POLICIESRESTRICTION), IDH_GROUPPOLICY);
        return;
    }

    OpenFindDialog(MainWindow->HWindow, Is(ptDisk) ? GetPathW() : L"");
}

void CFilesWindow::ViewFile(const wchar_t* name, BOOL altView, DWORD handlerID, int enumFileNamesSourceUID,
                            int enumFileNamesLastFileIndex)
{
    CALL_STACK_MESSAGE6("CFilesWindow::ViewFile(%ls, %d, %u, %d, %d)", name, altView, handlerID,
                        enumFileNamesSourceUID, enumFileNamesLastFileIndex);
    // verify that the file is on an accessible path
    if (name == NULL) // file from the panel
    {
        if (Is(ptDisk) || Is(ptZIPArchive))
        {
            if (CheckPath(TRUE) != ERROR_SUCCESS)
                return;
        }
    }
    else // file from a Windows path (Find results, Alt+F11)
    {
        // one branch now: there used to be two, because 'name' could be a
        // lossy structural mirror and only the nameW twin was safe to check the directory with.
        const wchar_t* backSlash = wcsrchr(name, L'\\');
        if (backSlash != NULL)
        {
            std::wstring dirW(name, backSlash - name);
            if (CheckPath(TRUE, dirW.c_str()) != ERROR_SUCCESS)
                return;
        }
    }

    BOOL addToHistory = name != NULL;
    // if viewing/editing from the panel, obtain the full long name
    BOOL useDiskCache = FALSE;          // TRUE only for ZIP - uses disk-cache
    BOOL arcCacheCacheCopies = TRUE;    // cache copies in disk-cache unless the archiver plugin requests otherwise
    std::wstring dcFileName; // ZIP disk-cache key
    std::wstring viewNameW;
    if (name == NULL)
    {
        int i = GetCaretIndex();
        if (i >= Dirs->Count && i < Dirs->Count + Files->Count)
        {
            CFileData* f = &Files->At(i - Dirs->Count);
            if (Is(ptDisk))
            {
                if (enumFileNamesLastFileIndex == -1)
                    enumFileNamesLastFileIndex = i - Dirs->Count;
                viewNameW = sally::unicode::BuildPanelChildPathW(GetPathW(), f->Name);
                if (f->DosName != NULL && SalLPGetFileAttributes(viewNameW.c_str()) == INVALID_FILE_ATTRIBUTES)
                {
                    DWORD err = GetLastError();
                    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_INVALID_NAME)
                    {
                        std::wstring dosPathW = sally::unicode::BuildPanelChildPathW(GetPathW(), f->DosName);
                        if (SalLPGetFileAttributes(dosPathW.c_str()) != INVALID_FILE_ATTRIBUTES)
                            viewNameW = dosPathW;
                    }
                }
                name = viewNameW.c_str();
                addToHistory = TRUE;
            }
            else
            {
                if (Is(ptZIPArchive))
                {
                    useDiskCache = TRUE;
                    dcFileName = sally::text::Fold(GetZIPArchive()); // folded archive path is the cache key
                    SalPathAppendW(dcFileName, GetZIPPath());
                    SalPathAppendW(dcFileName, f->Name);

                    // setting disk-cache for the plugin (standard values change only for the plugin)
                    std::wstring arcCacheTmpPath;
                    BOOL arcCacheOwnDelete = FALSE;
                    CPluginInterfaceAbstract* plugin = NULL; // != NULL if the plugin handles its own deletion
                    int format = PackerFormatConfig.PackIsArchive(GetZIPArchive());
                    if (format != 0) // a supported archive was found
                    {
                        format--;
                        int index = PackerFormatConfig.GetUnpackerIndex(format);
                        if (index < 0) // view: is the processing internal (plugin)?
                        {
                            CPluginData* data = Plugins.Get(-index - 1);
                            if (data != NULL)
                            {
                                data->GetCacheInfo(arcCacheTmpPath, &arcCacheOwnDelete, &arcCacheCacheCopies);
                                if (arcCacheOwnDelete)
                                    plugin = data->GetPluginInterface()->GetInterface();
                            }
                        }
                    }

                    const std::wstring nameInArchive = dcFileName.substr(wcslen(GetZIPArchive()) + 1);

                    // besides itself, compare the file with all the others and look for a case-sensitive identical name;
                    // if it exists, these two files must be distinguished in the disk-cache; I chose
                    // an allocated Name address - in opposite panels with the same archive the disk-cache won't be used,
                    // but given the improbability of this case, this approach is more than sufficient
                    int x;
                    for (x = 0; x < Files->Count; x++)
                    {
                        if (i - Dirs->Count != x)
                        {
                            CFileData* f2 = &Files->At(x);
                            if (wcscmp(f2->Name, f->Name) == 0)
                            {
                                wchar_t duplicateSuffix[32];
                                swprintf_s(duplicateSuffix, L":0x%p", (const void*)f->Name);
                                dcFileName += duplicateSuffix;
                                break;
                            }
                        }
                    }

                    BOOL exists;
                    int errorCode;
                    std::wstring validTmpName;
                    // f->Name is the exact wide name (NameW/UseWideName retired
                    // at P1.3); validate and sanitize it directly through the wide siblings,
                    // no ANSI mirror to fall back to.
                    if (!SalIsValidFileNameComponentW(f->Name))
                    {
                        validTmpName = SalMakeValidFileNameComponentW(f->Name);
                    }
                    name = DiskCache.GetName(dcFileName.c_str(), // returns const wchar_t*; the (char*) cast was silencing that
                                                    !validTmpName.empty() ? validTmpName.c_str() : f->Name,
                                                    &exists, FALSE,
                                                    !arcCacheTmpPath.empty() ? arcCacheTmpPath.c_str() : NULL,
                                                    plugin != NULL, plugin, &errorCode);
                    if (name == NULL)
                    {
                        return;
                    }

                    if (!exists) // we must unpack it
                    {
                        const wchar_t* backSlash = wcsrchr(name, L'\\');
                        const std::wstring tmpPath(name, backSlash);
                        BeginStopRefresh(); // snooper takes a break
                        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                        HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
                        BOOL renamingNotSupported = FALSE;
                        if (PackUnpackOneFile(this, GetZIPArchive(), PluginData.GetInterface(), nameInArchive.c_str(), f, tmpPath.c_str(),
                                              validTmpName.empty() ? NULL : validTmpName.c_str(),
                                              validTmpName.empty() ? NULL : &renamingNotSupported))
                        {
                            SetCursor(oldCur);
                            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                            CQuadWord size(0, 0);
                            HANDLE file = gFileSystem->CreateFile(name, GENERIC_READ,
                                                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                                 NULL, OPEN_EXISTING, 0, NULL);
                            if (file != INVALID_HANDLE_VALUE)
                            {
                                DWORD err;
                                SalGetFileSize(file, size, err); // ignore errors; file size isn't that important
                                gFileSystem->CloseFileHandle(file);
                            }
                            DiskCache.NamePrepared(dcFileName.c_str(), size);
                        }
                        else
                        {
                            SetCursor(oldCur);
                            if (renamingNotSupported) // to avoid repeating the same message for many plugins, display it here for all of them
                                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_UNPACKINVNAMERENUNSUP));
                            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                            DiskCache.ReleaseName(dcFileName.c_str(), FALSE); // not unpacked, nothing to cache
                            EndStopRefresh();                         // snooper will start again now
                            return;
                        }
                        EndStopRefresh(); // snooper will start again now
                    }
                }
                else
                {
                    if (Is(ptPluginFS))
                    {
                        if (GetPluginFS()->NotEmpty() && // FS is fine and supports view-file
                            GetPluginFS()->IsServiceSupported(FS_SERVICE_VIEWFILE))
                        {
                            // lower the thread priority to "normal" (so the operations don't overload the machine)
                            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

                            CSalamanderForViewFileOnFS sal(altView, handlerID);
                            GetPluginFS()->ViewFile(GetPluginFS()->GetPluginFSName(), HWindow, &sal, *f);

                            // raise the thread priority again, the operation has finished
                            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                        }
                        return; // view on the FS is already done
                    }
                    else
                    {
                        TRACE_E("Incorrect call to CFilesWindow::ViewFile()");
                        return;
                    }
                }
            }
        }
        else
        {
            return;
        }
    }

    // The panel branch above builds its own viewNameW from CFileData; an external caller
    // (Find results) passes the name itself, which is now already the exact wide form.
    if (viewNameW.empty() && name != NULL && name[0] != 0)
        viewNameW = name;

    HANDLE lock = NULL;
    BOOL lockOwner = FALSE;
    ViewFileInt(HWindow, !viewNameW.empty() ? viewNameW.c_str() : name, altView, handlerID,
                useDiskCache, lock, lockOwner, addToHistory,
                enumFileNamesSourceUID, enumFileNamesLastFileIndex);

    if (useDiskCache)
    {
        if (lock != NULL) // ensure association between the viewer and disk-cache
        {
            DiskCache.AssignName(dcFileName.c_str(), lock, lockOwner, arcCacheCacheCopies ? crtCache : crtDirect);
        }
        else // viewer didn't open or has no "lock" object - try leaving the file in disk-cache
        {
            DiskCache.ReleaseName(dcFileName.c_str(), arcCacheCacheCopies);
        }
    }
}

BOOL ViewFileInt(HWND parent, const wchar_t* name, BOOL altView, DWORD handlerID, BOOL returnLock,
                 HANDLE& lock, BOOL& lockOwner, BOOL addToHistory, int enumFileNamesSourceUID,
                 int enumFileNamesLastFileIndex)
{
    BOOL success = FALSE;
    lock = NULL;
    lockOwner = FALSE;

    // obtain the full DOS name
    std::wstring dosName = GetShortPathW(name);
    if (dosName.empty())
    {
        TRACE_E("GetShortPathName() failed");
    }

    // find the file name and check if it has an extension - needed for masks
    const wchar_t* namePart = wcsrchr(name, L'\\');
    if (namePart == NULL)
    {
        TRACE_E("Invalid parameter for ViewFileInt(): " << sally::diagnostic::EncodeAcpLossy(name));
        return FALSE;
    }
    namePart++;
    const wchar_t* tmpExt = wcsrchr(namePart, L'.');
    //if (tmpExt == NULL || tmpExt == namePart) tmpExt = namePart + lstrlen(namePart); // ".cvspass" is not an extension...
    if (tmpExt == NULL)
        tmpExt = namePart + (int)wcslen(namePart); // ".cvspass" is treated as an extension in Windows...
    else
        tmpExt++;

    // position for viewers
    WINDOWPLACEMENT place;
    place.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(MainWindow->HWindow, &place);
    // GetWindowPlacement respects the Taskbar, so if the Taskbar is at the top or left,
    // the values are shifted by its dimensions. Perform correction.
    RECT monitorRect;
    RECT workRect;
    MultiMonGetClipRectByRect(&place.rcNormalPosition, &workRect, &monitorRect);
    OffsetRect(&place.rcNormalPosition, workRect.left - monitorRect.left,
               workRect.top - monitorRect.top);

    // if called, for example, from find and the main window is minimized,
    // we do not want a minimized viewer
    if (place.showCmd == SW_MINIMIZE || place.showCmd == SW_SHOWMINIMIZED ||
        place.showCmd == SW_SHOWMINNOACTIVE)
        place.showCmd = SW_SHOWNORMAL;

    // find the correct viewer and launch it
    CViewerMasks* masks = (altView ? MainWindow->AltViewerMasks : MainWindow->ViewerMasks);
    CViewerMasksItem* viewer = NULL;

    if (handlerID != 0xFFFFFFFF)
    {
        // attempt to find a viewer with matching HandlerID
        int j;
        for (j = 0; viewer == NULL && j < 2; j++)
        {
            CViewerMasks* masks2 = (j == 0 ? MainWindow->ViewerMasks : MainWindow->AltViewerMasks);
            int i;
            for (i = 0; viewer == NULL && i < masks2->Count; i++)
            {
                if (masks2->At(i)->HandlerID == handlerID)
                    viewer = masks2->At(i);
            }
        }
    }

    if (viewer == NULL)
    {
        int i;
        for (i = 0; i < masks->Count; i++)
        {
            int err;
            if (masks->At(i)->Masks->PrepareMasks(err))
            {
                // one path: namePart is the true wide leaf now, so the
                // AgreeMasks(char*) fallback that re-widened a lossy CP_ACP leaf is gone.
                BOOL masksMatch = masks->At(i)->Masks->AgreeMasks(namePart, NULL);
                if (masksMatch)
                {
                    viewer = masks->At(i);

                    if (viewer != NULL && viewer->ViewerType != VIEWER_EXTERNAL &&
                        viewer->ViewerType != VIEWER_INTERNAL)
                    { // plug-in viewers only
                        CPluginData* plugin = Plugins.Get(-viewer->ViewerType - 1);
                        if (plugin != NULL && plugin->SupportViewer)
                        {
                            if (!plugin->CanViewFile(name))
                                continue; // try to find another viewer, this one won't do it
                        }
                        else
                            TRACE_E("Unexpected error (before CanViewFile) in (Alt)ViewerMasks (invalid ViewerType).");
                    }
                    break; // everything is fine, open the viewer
                }
            }
            else
                TRACE_E("Unexpected error in group mask.");
        }
    }

    if (viewer != NULL)
    {
        //    if (MakeFileAvailOfflineIfOneDriveOnWin81(parent, name))
        //    {
        if (addToHistory)
            MainWindow->FileHistory->AddFile(fhitView, viewer->HandlerID, name); // add file to history

        switch (viewer->ViewerType)
        {
        case VIEWER_EXTERNAL:
        {
            std::wstring expCommandW;
            std::wstring expArgumentsW;
            std::wstring expInitDirW;
            if (ExpandCommand(parent, viewer->Command.c_str(), expCommandW, FALSE) &&
                ExpandArguments(parent, name, dosName.c_str(), viewer->Arguments.c_str(), expArgumentsW, NULL) &&
                ExpandInitDir(parent, name, dosName.c_str(), viewer->InitDir.c_str(), expInitDirW, FALSE))
            {
                if (SystemPolicies.GetMyRunRestricted() &&
                    !SystemPolicies.GetMyCanRun(expCommandW.c_str()))
                {
                    gPrompter->ShowErrorWithHelp(LoadStrW(IDS_POLICIESRESTRICTION_TITLE),
                                                 LoadStrW(IDS_POLICIESRESTRICTION), IDH_GROUPPOLICY);
                    break;
                }

                MainWindow->SetDefaultDirectories();

                if (expInitDirW.empty()) // matches the original ANSI "this should never happen" branch
                {
                    expInitDirW = name;
                    size_t lastSlash = expInitDirW.find_last_of(L'\\');
                    if (lastSlash != std::wstring::npos)
                        expInitDirW.resize(lastSlash);
                    else
                        expInitDirW.clear();
                }

                std::wstring cmdLineW = expCommandW;
                if (!cmdLineW.empty() && cmdLineW.front() != L'"' &&
                    cmdLineW.find(L' ') != std::wstring::npos)
                {
                    cmdLineW.insert(cmdLineW.begin(), L'"');
                    cmdLineW.push_back(L'"');
                }
                cmdLineW.push_back(L' ');
                cmdLineW.append(expArgumentsW);

                ViewerEditorProcessLaunchRequest request;
                request.commandLine = cmdLineW;
                request.workingDirectory = expInitDirW;
                request.creationFlags = NORMAL_PRIORITY_CLASS;
                request.usePosition = true;
                request.x = place.rcNormalPosition.left;
                request.y = place.rcNormalPosition.top;
                request.useSize = true;
                request.width = place.rcNormalPosition.right - place.rcNormalPosition.left;
                request.height = place.rcNormalPosition.bottom - place.rcNormalPosition.top;
                request.useShowWindow = true;
                request.showWindow = SW_SHOWNORMAL;

                ExternalToolResult launchResult = gViewerEditorLauncher != NULL
                                                      ? gViewerEditorLauncher->LaunchProcess(request)
                                                      : ExternalToolResult::Error(ERROR_INVALID_PARAMETER);
                if (!launchResult.success)
                {
                    std::wstring msg = FormatStrW(LoadStrW(IDS_ERROREXECVIEW),
                                                  expCommandW.c_str(),
                                                  GetErrorTextOwned(launchResult.errorCode).c_str());
                    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                }
                else
                {
                    HANDLE processHandle = launchResult.DetachNativeProcessHandle();
                    if (processHandle == NULL)
                    {
                        DWORD err = GetLastError();
                        launchResult.CloseProcess();
                        std::wstring msg = FormatStrW(LoadStrW(IDS_ERROREXECVIEW),
                                                      expCommandW.c_str(),
                                                      GetErrorTextOwned(err).c_str());
                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                    }
                    else
                    {
                        HANDLES_ADD(__htProcess, __hoCreateProcess, processHandle);
                        success = TRUE;
                        if (returnLock)
                        {
                            lock = processHandle;
                            lockOwner = TRUE;
                        }
                        else
                            HANDLES(CloseHandle(processHandle));
                    }
                }
            }
            break;
        }

        case VIEWER_INTERNAL:
        {
            if (Configuration.SavePosition &&
                Configuration.WindowPlacement.length != 0)
            {
                place = Configuration.WindowPlacement;
                // GetWindowPlacement respects the Taskbar, so if the Taskbar is at the top or left,
                // the values are shifted by its dimensions. Perform correction.
                RECT monitorRect2;
                RECT workRect2;
                MultiMonGetClipRectByRect(&place.rcNormalPosition, &workRect2, &monitorRect2);
                OffsetRect(&place.rcNormalPosition, workRect2.left - monitorRect2.left,
                           workRect2.top - monitorRect2.top);
                MultiMonEnsureRectVisible(&place.rcNormalPosition, TRUE);
            }

            HANDLE lockAux = NULL;
            BOOL lockOwnerAux = FALSE;
            // one call: OpenViewer/OpenViewerW were a pair whose only
            // difference was which half the CALL_STACK_MESSAGE printed.
            BOOL viewerOpened = OpenViewer(name, vtText,
                                            place.rcNormalPosition.left,
                                            place.rcNormalPosition.top,
                                            place.rcNormalPosition.right - place.rcNormalPosition.left,
                                            place.rcNormalPosition.bottom - place.rcNormalPosition.top,
                                            place.showCmd,
                                            returnLock, &lockAux, &lockOwnerAux, NULL,
                                            enumFileNamesSourceUID, enumFileNamesLastFileIndex);
            if (viewerOpened)
            {
                success = TRUE;
                if (returnLock && lockAux != NULL)
                {
                    lock = lockAux;
                    lockOwner = lockOwnerAux;
                }
            }
            break;
        }

        default: // plug-ins
        {
            HANDLE lockAux = NULL;
            BOOL lockOwnerAux = FALSE;

            CPluginData* plugin = Plugins.Get(-viewer->ViewerType - 1);
            if (plugin != NULL && plugin->SupportViewer)
            {
                if (plugin->ViewFile(name, place.rcNormalPosition.left, place.rcNormalPosition.top,
                                     place.rcNormalPosition.right - place.rcNormalPosition.left,
                                     place.rcNormalPosition.bottom - place.rcNormalPosition.top,
                                     place.showCmd, Configuration.AlwaysOnTop,
                                     returnLock, &lockAux, &lockOwnerAux,
                                     enumFileNamesSourceUID, enumFileNamesLastFileIndex))
                {
                    success = TRUE;
                    if (returnLock && lockAux != NULL)
                    {
                        lock = lockAux;
                        lockOwner = lockOwnerAux;
                    }
                }
            }
            else
                TRACE_E("Unexpected error in (Alt)ViewerMasks (invalid ViewerType).");
            break;
        }
        }
        //    }
    }
    else
    {
        int textID = altView ? IDS_CANT_VIEW_FILE_ALT : IDS_CANT_VIEW_FILE;
        std::wstring message = FormatStrW(LoadStrW(textID), name);
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), message.c_str());
    }
    return success;
}

void CFilesWindow::EditFile(const wchar_t* name, DWORD handlerID)
{
    CALL_STACK_MESSAGE3("CFilesWindow::EditFile(%ls, %u)", name, handlerID);
    if (!Is(ptDisk) && name == NULL)
    {
        TRACE_E("Incorrect call to CFilesWindow::EditFile()");
        return;
    }

    // verify that the file is on an accessible path
    std::wstring path;
    if (name == NULL)
    {
        if (CheckPath(TRUE) != ERROR_SUCCESS)
            return;
    }
    else // file from a Windows path (Find results, Alt+F11, history)
    {
        // one branch: 'name' can no longer be a lossy structural mirror,
        // so the wide-twin branch and the narrow fallback have the same job.
        const wchar_t* backSlash = wcsrchr(name, L'\\');
        if (backSlash != NULL)
        {
            std::wstring dirW(name, backSlash - name);
            if (CheckPath(TRUE, dirW.c_str()) != ERROR_SUCCESS)
                return;
        }
    }

    BOOL addToHistory = name != NULL && Is(ptDisk);

    // Wide twin of `name`. Built from GetPathW() + f->NameW when available so
    // that Unicode panel paths (e.g. "C:\Temp\SalLongPathTest\zz中文\한글_x.txt")
    // can be passed to CreateProcessW without going through CP_ACP — otherwise
    // the working-directory parameter resolves to "zz??" and CreateProcessA
    // returns ERROR_DIRECTORY (267). An external caller's exact wide twin
    // (Find results) is seeded here directly; the panel branch below builds
    // its own from CFileData when name == NULL.
    std::wstring nameW;
    if (name != NULL && name[0] != 0)
        nameW = name;

    // if viewing/editing from the panel, obtain the full long name
    if (name == NULL)
    {
        int i = GetCaretIndex();
        if (i >= Dirs->Count && i < Dirs->Count + Files->Count)
        {
            CFileData* f = &Files->At(i - Dirs->Count);
            if (Is(ptDisk))
            {
                path = sally::unicode::BuildPanelChildPathW(GetPathW(), f->Name);
                // try whether the file name is valid, otherwise try its DOS name as well
                // (handles files accessible only through Unicode or DOS names)
                if (f->DosName != NULL && SalLPGetFileAttributes(path.c_str()) == INVALID_FILE_ATTRIBUTES)
                {
                    DWORD err = GetLastError();
                    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_INVALID_NAME)
                    {
                        const std::wstring dosPath =
                            sally::unicode::BuildPanelChildPathW(GetPathW(), f->DosName);
                        if (SalLPGetFileAttributes(dosPath.c_str()) != INVALID_FILE_ATTRIBUTES)
                            path = dosPath;
                    }
                }
                name = path.c_str();
                addToHistory = TRUE;
                // path is wide-native (f->Name is the exact wide name,
                // NameW/UseWideName retired at P1.3) - path already IS the wide twin,
                // no separate CP_ACP-lossy reconstruction needed.
                nameW = path;
            }
        }
        else
        {
            return;
        }
    }

    // The narrow fallback is gone: every caller now supplies the exact wide
    // name, so there is no lossy mirror left to re-derive one from.

    // obtain the full DOS name
    std::wstring dosName = GetShortPathW(name);
    if (dosName.empty())
    {
        TRACE_I("GetShortPathName() failed.");
    }

    // find the file name and check if it has an extension - needed for masks
    const wchar_t* namePart = wcsrchr(name, L'\\');
    if (namePart == NULL)
    {
        TRACE_E("Invalid parameter CFilesWindow::EditFile(): " << sally::diagnostic::EncodeAcpLossy(name));
        return;
    }
    namePart++;
    const wchar_t* tmpExt = wcsrchr(namePart, L'.');
    //if (tmpExt == NULL || tmpExt == namePart) tmpExt = namePart + lstrlen(namePart); // ".cvspass" is not an extension...
    if (tmpExt == NULL)
        tmpExt = namePart + (int)wcslen(namePart); // ".cvspass" is treated as an extension in Windows...
    else
        tmpExt++;

    // position for editors
    WINDOWPLACEMENT place;
    place.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(MainWindow->HWindow, &place);
    // GetWindowPlacement respects the Taskbar, so if the Taskbar is at the top or left,
    // the values are shifted by its dimensions. Perform correction.
    RECT monitorRect;
    RECT workRect;
    MultiMonGetClipRectByRect(&place.rcNormalPosition, &workRect, &monitorRect);
    OffsetRect(&place.rcNormalPosition, workRect.left - monitorRect.left,
               workRect.top - monitorRect.top);
    // if called, for example, from find and the main window is minimized,
    // we do not want a minimized editor
    if (place.showCmd == SW_MINIMIZE || place.showCmd == SW_SHOWMINIMIZED ||
        place.showCmd == SW_SHOWMINNOACTIVE)
        place.showCmd = SW_SHOWNORMAL;

    // find the correct editor and launch it
    CEditorMasks* masks = MainWindow->EditorMasks;

    CEditorMasksItem* editor = NULL;

    if (handlerID != 0xFFFFFFFF)
    {
        // attempt to find an editor with matching HandlerID
        int i;
        for (i = 0; editor == NULL && i < masks->Count; i++)
        {
            if (masks->At(i)->HandlerID == handlerID)
                editor = masks->At(i);
        }
    }

    if (editor == NULL)
    {
        int i;
        for (i = 0; i < masks->Count; i++)
        {
            int err;
            if (masks->At(i)->Masks->PrepareMasks(err))
            {
                // wide: nameW is always populated by this point from the caller's
                // exact wide value. Mask-match the true wide leaf instead of re-widening the
                // already-lossy CP_ACP namePart internally.
                const wchar_t* namePartW = wcsrchr(nameW.c_str(), L'\\');
                namePartW = namePartW != NULL ? namePartW + 1 : nameW.c_str();
                if (masks->At(i)->Masks->AgreeMasks(namePartW, NULL))
                {
                    editor = masks->At(i);
                    break;
                }
            }
            else
                TRACE_E("Unexpected error in group mask");
        }
    }

    if (editor != NULL)
    {
        if (addToHistory)
            MainWindow->FileHistory->AddFile(fhitEdit, editor->HandlerID, name); // add file to history

        std::wstring expCommandW;
        std::wstring expArgumentsW;
        std::wstring expInitDirW;
        if (ExpandCommand(HWindow, editor->Command.c_str(), expCommandW, FALSE) &&
            ExpandArguments(HWindow, name, dosName.c_str(), editor->Arguments.c_str(), expArgumentsW, NULL) &&
            ExpandInitDir(HWindow, name, dosName.c_str(), editor->InitDir.c_str(), expInitDirW, FALSE))
        {
            if (SystemPolicies.GetMyRunRestricted() &&
                !SystemPolicies.GetMyCanRun(expCommandW.c_str()))
            {
                gPrompter->ShowErrorWithHelp(LoadStrW(IDS_POLICIESRESTRICTION_TITLE),
                                             LoadStrW(IDS_POLICIESRESTRICTION), IDH_GROUPPOLICY);
                return;
            }

            MainWindow->SetDefaultDirectories();

            if (expInitDirW.empty()) // belt-and-suspenders fallback (parallel of the original ANSI "this should never happen" branch)
            {
                expInitDirW = name;
                size_t lastSlash = expInitDirW.find_last_of(L'\\');
                if (lastSlash != std::wstring::npos)
                    expInitDirW.resize(lastSlash);
                else
                    expInitDirW.clear();
            }

            // Assemble wide cmdLine = quoted command + ' ' + arguments. Quote the
            // command (binary path) if it contains spaces and isn't already quoted.
            std::wstring cmdLineW = expCommandW;
            if (!cmdLineW.empty() && cmdLineW.front() != L'"' &&
                cmdLineW.find(L' ') != std::wstring::npos)
            {
                cmdLineW.insert(cmdLineW.begin(), L'"');
                cmdLineW.push_back(L'"');
            }
            cmdLineW.push_back(L' ');
            cmdLineW.append(expArgumentsW);

            ViewerEditorProcessLaunchRequest request;
            request.commandLine = cmdLineW;
            request.workingDirectory = expInitDirW;
            request.creationFlags = NORMAL_PRIORITY_CLASS;
            request.usePosition = true;
            request.x = place.rcNormalPosition.left;
            request.y = place.rcNormalPosition.top;
            request.useSize = true;
            request.width = place.rcNormalPosition.right - place.rcNormalPosition.left;
            request.height = place.rcNormalPosition.bottom - place.rcNormalPosition.top;
            request.useShowWindow = true;
            request.showWindow = SW_SHOWNORMAL;

            ExternalToolResult launchResult = gViewerEditorLauncher != NULL
                                                  ? gViewerEditorLauncher->LaunchProcess(request)
                                                  : ExternalToolResult::Error(ERROR_INVALID_PARAMETER);
            if (!launchResult.success)
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_ERROREXECEDIT),
                                              expCommandW.c_str(),
                                              GetErrorTextOwned(launchResult.errorCode).c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
            }
            else
            {
                HANDLE processHandle = launchResult.DetachNativeProcessHandle();
                if (processHandle == NULL)
                {
                    DWORD err = GetLastError();
                    launchResult.CloseProcess();
                    std::wstring msg = FormatStrW(LoadStrW(IDS_ERROREXECEDIT),
                                                  expCommandW.c_str(),
                                                  GetErrorTextOwned(err).c_str());
                    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                }
                else
                {
                    HANDLES_ADD(__htProcess, __hoCreateProcess, processHandle);
                    HANDLES(CloseHandle(processHandle));
                }
            }
        }
    }
    else
    {
        std::wstring message = FormatStrW(LoadStrW(IDS_CANT_EDIT_FILE), name);
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), message.c_str());
    }
}

void CFilesWindow::EditNewFile()
{
    CALL_STACK_MESSAGE1("CFilesWindow::EditNewFile()");
    BeginStopRefresh(); // snooper takes a break

    // restore DefaultDir
    MainWindow->UpdateDefaultDir(TRUE);

    std::wstring pathW;
    if (Configuration.UseEditNewFileDefault)
    {
        pathW = Configuration.EditNewFileDefault;
    }
    else
    {
        // Try to derive name from the focused item (issue #12)
        BOOL usedFocused = FALSE;
        int totalCount = Dirs->Count + Files->Count;
        if (FocusedIndex >= 0 && FocusedIndex < totalCount)
        {
            if (FocusedIndex >= Dirs->Count)
            {
                // Focused on a file
                CFileData* f = &Files->At(FocusedIndex - Dirs->Count);
                // f->NameW/f->Name-is-lossy ternary retired - f->Name is
                // unconditionally the exact wide name now.
                const wchar_t* nameW = f->Name;
                const wchar_t* dot = wcsrchr(nameW, L'.');
                std::wstring suggestion;
                if (dot != NULL && dot > nameW)
                {
                    // "report.docx" -> "report-new.docx"
                    suggestion.assign(nameW, dot);
                    suggestion += L"-new";
                    suggestion += dot;
                }
                else
                {
                    // "Makefile" -> "Makefile-new"
                    suggestion = std::wstring(nameW) + L"-new";
                }
                pathW = suggestion;
                usedFocused = TRUE;
            }
            else
            {
                // Focused on a directory — skip ".."
                CFileData* d = &Dirs->At(FocusedIndex);
                if (wcscmp(d->Name, L"..") != 0)
                {
                    // "MyFolder" -> "MyFolder-new.txt"
                    pathW = std::wstring(d->Name) + L"-new.txt";
                    usedFocused = TRUE;
                }
            }
        }
        if (!usedFocused)
        {
            pathW = LoadStrW(IDS_EDITNEWFILE_DEFAULTNAME);
        }
    }
    CTruncatedString subject;
    subject.SetW(LoadStrW(IDS_NEWFILENAME), NULL);

    BOOL first = TRUE;

    while (1)
    {
        CEditNewFileDialog dlg(HWindow, pathW, &subject,
                               Configuration.EditNewHistory, EDITNEW_HISTORY_SIZE);

        // Some users always create .txt and are satisfied with overwriting just the extension; others create various files and want to overwrite the whole name,
        // so we compromised and introduced a dedicated option for Edit New File in the configuration.
        // ------------------
        // For EditNew, the smart selection of only the name makes no sense because people also change the extension, see our forum:
        // https://forum.altap.cz/viewtopic.php?t=2655
        // -----------------
        // Since Windows Vista, Microsoft introduced a demanded feature: quick rename selects only the name without the dot and extension
        // the same code appears here four times
        if (!Configuration.EditNewSelectAll)
        {
            int selectionEnd = -1;
            if (first)
            {
                const wchar_t* dot = wcsrchr(pathW.c_str(), L'.');
                if (dot != NULL && dot > pathW.c_str()) // although ".cvspass" is an extension in Windows, Explorer selects the entire name, so we do the same
                                                        //      if (dot != NULL)
                    selectionEnd = (int)(dot - pathW.c_str());
                dlg.SetSelectionEnd(selectionEnd);
                first = FALSE; // after an error we get the full file name, so we select it all
            }
        }

        if (dlg.Execute() == IDOK)
        {
            UpdateWindow(MainWindow->HWindow);

            std::wstring inputW = pathW;
            if (!inputW.empty())
            {
                wchar_t* writable = &inputW[0];
                wchar_t* lastCompNameW = wcsrchr(writable, L'\\');
                MakeValidFileNameComponentW(lastCompNameW != NULL ? lastCompNameW + 1 : writable);
            }
            pathW = inputW;

            // clean the name from undesirable characters at the beginning and end
            // we do this only for the last component; the previous ones already exist and it doesn't matter
            // (the system handles it) or they are checked during creation and an error is shown
            // (we don't clean them, we let the user do some work, it's easy enough)
            std::wstring errText;
            int errTextID;
            std::wstring nextFocusW;
            // GetPathW() is the panel's authoritative path. The removed ANSI mirror
            // was CP_ACP-mangled and could not recover the original non-ASCII directory name.
            if (SalGetFullNameW(pathW, &errTextID, Is(ptDisk) ? GetPathW() : NULL, &nextFocusW, NULL, FALSE))
            {
                std::wstring checkPathW = pathW;
                if (!CutDirectoryW(checkPathW))
                {
                    errText = LoadStrW(IDS_PATHISINVALID);
                }
                else if (SalCheckPathW(TRUE, checkPathW.c_str(), ERROR_SUCCESS, TRUE, HWindow) != ERROR_SUCCESS)
                {
                    EndStopRefresh(); // snooper will start again now
                    return;
                }
                else
                {
                    BOOL invalidName = FileNameInvalidForManualCreateW(pathW.c_str());
                    HANDLE hFile = INVALID_HANDLE_VALUE;
                    if (invalidName)
                        SetLastError(ERROR_INVALID_NAME);
                    else
                    {
                        hFile = gFileSystem->CreateFile(pathW.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
                        HANDLES_ADD_EX(__otQuiet, hFile != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, hFile, GetLastError(), TRUE);
                    }
                    BOOL editExisting = FALSE;
                    if (hFile == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS)
                    {
                        PromptResult pr = gPrompter->ConfirmOverwrite(NULL, LoadStrW(IDS_EDITNEWALREADYEX));
                        if (pr.type == PromptResult::kYes)
                            editExisting = TRUE;
                        else
                            break;
                    }
                    if (hFile != INVALID_HANDLE_VALUE || editExisting)
                    {
                        if (!editExisting)
                            HANDLES(CloseHandle(hFile));

                        if (!nextFocusW.empty())
                        {
                            // RefreshDirectory's focus consumer checks NextFocusNameW
                            // first (exact match); writing only the narrow mirror silently dropped
                            // auto-focus accuracy for a name CP_ACP can't spell, same shape as 178.
                            NextFocusNameW = nextFocusW;
                        }

                        EditFile(pathW.c_str());

                        // change only in the directory where the file was created
                        MainWindow->PostChangeOnPathNotificationW(checkPathW.c_str(), FALSE);

                        break;
                    }
                    else
                        errText = GetErrorTextOwned(GetLastError()).c_str();
                }
            }
            else
                errText = LoadStrW(errTextID);
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), errText.c_str());
        }
        else
            break;
    }
    EndStopRefresh(); // snooper will start again now
}

// fills the popup based on available viewers
void CFilesWindow::FillViewWithMenu(CMenuPopup* popup)
{
    CALL_STACK_MESSAGE1("CFilesWindow::FillViewWithMenu()");

    // remove existing items
    popup->RemoveAllItems();

    // retrieve the list of viewer indexes
    TDirectArray<CViewerMasksItem*> items(50, 10);
    if (!FillViewWithData(&items))
        return;

    MENU_ITEM_INFO mii;
    int i;
    for (i = 0; i < items.Count; i++)
    {
        CViewerMasksItem* item = items[i];
        std::wstring menuText;

        int imgIndex = -1; // no icon
        if (item->ViewerType < 0)
        {
            int pluginIndex = -item->ViewerType - 1;
            CPluginData* plugin = Plugins.Get(pluginIndex);
            menuText = plugin->Name;
            if (plugin->PluginIconIndex != -1) // the plugin has an icon
                imgIndex = pluginIndex;
        }
        if (item->ViewerType == VIEWER_EXTERNAL)
            menuText = FormatStrW(LoadStrW(IDS_VIEWWITH_EXTERNAL), item->Command.c_str());
        if (item->ViewerType == VIEWER_INTERNAL)
            menuText = LoadStrW(IDS_VIEWWITH_INTERNAL);

        mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID | MENU_MASK_IMAGEINDEX;
        mii.Type = MENU_TYPE_STRING;
        mii.String = const_cast<wchar_t*>(menuText.c_str());
        mii.ID = CM_VIEWWITH_MIN + i;
        mii.ImageIndex = imgIndex;
        if (mii.ID > CM_VIEWWITH_MAX)
        {
            TRACE_E("mii.ID > CM_VIEWWITH_MAX");
            break;
        }
        popup->InsertItem(-1, TRUE, &mii);
    }
    if (popup->GetItemCount() == 0)
    {
        mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_STATE;
        mii.Type = MENU_TYPE_STRING;
        mii.State = MENU_STATE_GRAYED;
        mii.String = LoadStrW(IDS_VIEWWITH_EMPTY);
        popup->InsertItem(-1, TRUE, &mii);
    }
    else
        popup->AssignHotKeys();
}

BOOL CFilesWindow::FillViewWithData(TDirectArray<CViewerMasksItem*>* items)
{
    // merging proceeds through normal and alternate viewers
    int type;
    for (type = 0; type < 2; type++)
    {
        CViewerMasks* masks;
        if (type == 0)
            masks = MainWindow->ViewerMasks;
        else
            masks = MainWindow->AltViewerMasks;

        int i;
        for (i = 0; i < masks->Count; i++)
        {
            CViewerMasksItem* item = masks->At(i);

            BOOL alreadyAdded = FALSE; // we do not want the item listed more than once

            int j;
            for (j = 0; j < items->Count; j++)
            {
                CViewerMasksItem* oldItem = items->At(j);

                if (item->ViewerType == VIEWER_EXTERNAL)
                {
                    if (_wcsicmp(item->Command.c_str(), oldItem->Command.c_str()) == 0 &&
                        _wcsicmp(item->Arguments.c_str(), oldItem->Arguments.c_str()) == 0 &&
                        _wcsicmp(item->InitDir.c_str(), oldItem->InitDir.c_str()) == 0)
                    {
                        alreadyAdded = TRUE;
                        break;
                    }
                }
                else
                {
                    if (item->ViewerType == oldItem->ViewerType)
                    {
                        alreadyAdded = TRUE;
                        break;
                    }
                }
            }

            if (!alreadyAdded)
            {
                items->Add(masks->At(i));
                if (!items->IsGood())
                {
                    items->ResetState();
                    return FALSE;
                }
            }
        }
    }
    return TRUE;
}

void CFilesWindow::OnViewFileWith(int index)
{
    BeginStopRefresh(); // snooper takes a break

    // get the list of viewer indexes
    TDirectArray<CViewerMasksItem*> items(50, 10);
    if (!FillViewWithData(&items))
    {
        EndStopRefresh(); // snooper will start again now
        return;
    }

    if (index < 0 || index >= items.Count)
    {
        TRACE_E("index=" << index);
        EndStopRefresh(); // snooper will start again now
        return;
    }

    ViewFile(NULL, FALSE, items[index]->HandlerID, Is(ptDisk) ? EnumFileNamesSourceUID : -1,
             -1 /* index determined by focus */);

    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::ViewFileWith(const wchar_t* name, HWND hMenuParent, const POINT* menuPoint, DWORD* handlerID,
                                int enumFileNamesSourceUID, int enumFileNamesLastFileIndex)
{
    CALL_STACK_MESSAGE5("CFilesWindow::ViewFileWith(%ls, , , %s, %d, %d)", name,
                        (handlerID == NULL ? "NULL" : "non-NULL"), enumFileNamesSourceUID,
                        enumFileNamesLastFileIndex);
    BeginStopRefresh(); // snooper takes a break
    if (handlerID != NULL)
        *handlerID = 0xFFFFFFFF;

    CMenuPopup contextPopup(CML_FILES_VIEWWITH);
    FillViewWithMenu(&contextPopup);
    DWORD cmd = contextPopup.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                   menuPoint->x, menuPoint->y, hMenuParent, NULL);
    if (cmd >= CM_VIEWWITH_MIN && cmd <= CM_VIEWWITH_MAX)
    {
        // get the list of viewer indexes
        TDirectArray<CViewerMasksItem*> items(50, 10);
        if (!FillViewWithData(&items))
        {
            EndStopRefresh(); // snooper will start again now
            return;
        }

        int index = cmd - CM_VIEWWITH_MIN;
        if (handlerID == NULL)
            ViewFile(name, FALSE, items[index]->HandlerID, enumFileNamesSourceUID, enumFileNamesLastFileIndex);
        else
            *handlerID = items[index]->HandlerID;
    }

    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::FillEditWithMenu(CMenuPopup* popup)
{
    CALL_STACK_MESSAGE1("CFilesWindow::FillEditWithMenu()");

    // remove existing items
    popup->RemoveAllItems();

    // retrieve the list of editor indexes
    CEditorMasks* masks = MainWindow->EditorMasks;

    MENU_ITEM_INFO mii;
    int i;
    for (i = 0; i < masks->Count; i++)
    {
        mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID;
        mii.Type = MENU_TYPE_STRING;
        mii.ID = CM_EDITWITH_MIN + i;
        if (mii.ID > CM_EDITWITH_MAX)
        {
            TRACE_E("mii.ID > CM_EDITWITH_MAX");
            break;
        }

        CEditorMasksItem* item = masks->At(i);

        // if users have multiple rows of masks associated with one viewer/editor,
        // insert the item into the list only once
        BOOL alreadyAdded = FALSE;
        int j;
        for (j = 0; j < i; j++)
        {
            CEditorMasksItem* oldItem = masks->At(j);
            if (_wcsicmp(item->Command.c_str(), oldItem->Command.c_str()) == 0 &&
                _wcsicmp(item->Arguments.c_str(), oldItem->Arguments.c_str()) == 0 &&
                _wcsicmp(item->InitDir.c_str(), oldItem->InitDir.c_str()) == 0)
            {
                alreadyAdded = TRUE;
                break;
            }
        }
        if (!alreadyAdded)
        {
            std::wstring menuText = FormatStrW(LoadStrW(IDS_EDITWITH_EXTERNAL), item->Command.c_str());
            mii.String = const_cast<wchar_t*>(menuText.c_str());
            popup->InsertItem(-1, TRUE, &mii);
        }
    }
    if (popup->GetItemCount() == 0)
    {
        mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_STATE;
        mii.Type = MENU_TYPE_STRING;
        mii.State = MENU_STATE_GRAYED;
        mii.String = LoadStrW(IDS_EDITWITH_EMPTY);
        popup->InsertItem(-1, TRUE, &mii);
    }
    else
        popup->AssignHotKeys();
}

void CFilesWindow::OnEditFileWith(int index)
{
    BeginStopRefresh(); // snooper takes a break

    // get the list of viewer indexes
    // get the list of editor indexes
    CEditorMasks* masks = MainWindow->EditorMasks;

    if (index < 0 || index >= masks->Count)
    {
        TRACE_E("index=" << index);
        EndStopRefresh(); // snooper will start again now
        return;
    }

    EditFile(NULL, masks->At(index)->HandlerID);

    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::EditFileWith(const wchar_t* name, HWND hMenuParent, const POINT* menuPoint, DWORD* handlerID)
{
    CALL_STACK_MESSAGE3("CFilesWindow::EditFileWith(%ls, , , %s)", name,
                        (handlerID == NULL ? "NULL" : "non-NULL"));
    BeginStopRefresh(); // snooper takes a break
    if (handlerID != NULL)
        *handlerID = 0xFFFFFFFF;

    // get the list of editor indexes
    CEditorMasks* masks = MainWindow->EditorMasks;

    // create menu
    CMenuPopup contextPopup;
    FillEditWithMenu(&contextPopup);
    DWORD cmd = contextPopup.Track(MENU_TRACK_NONOTIFY | MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                   menuPoint->x, menuPoint->y, hMenuParent, NULL);
    if (cmd >= CM_EDITWITH_MIN && cmd <= CM_EDITWITH_MAX)
    {
        int index = cmd - CM_EDITWITH_MIN;
        if (handlerID == NULL)
            EditFile(name, masks->At(index)->HandlerID);
        else
            *handlerID = masks->At(index)->HandlerID;
    }

    EndStopRefresh(); // snooper will start again now
}

// 2026-08-26: the narrow MakeValidFileName(char*) and CutSpacesFromBothSides(char*)
// were deleted - confirmed-dead (zero callers anywhere: core, plugins, tests). Their wide
// siblings, MakeValidFileNameW (this file, 2 real callers) and CutSpacesFromBothSidesW
// (common/SalPathWide.cpp, 3 real callers), are the ones actually used.

// CutSpacesFromBothSidesW moved to common/SalPathWide.cpp (shared with private tests).

BOOL CutDoubleQuotesFromBothSides(wchar_t* path)
{
    int len = (int)wcslen(path);
    if (len >= 2 && path[0] == L'"' && path[len - 1] == L'"')
    {
        // len counts CHARACTERS; memmove wants BYTES.
        memmove(path, path + 1, (size_t)(len - 2) * sizeof(wchar_t));
        path[len - 2] = 0;
        return TRUE;
    }
    return FALSE;
}

void CFilesWindow::CreateDir(CFilesWindow* target)
{
    CALL_STACK_MESSAGE1("CFilesWindow::CreateDir()");
    BeginStopRefresh(); // snooper takes a break

    std::wstring pathW;

    // restore DefaultDir
    MainWindow->UpdateDefaultDir(MainWindow->GetActivePanel() == this);

    if (Is(ptDisk)) // create directory on disk
    {
        CTruncatedString subject;
        subject.SetW(LoadStrW(IDS_CREATEDIRECTORY_TEXT), NULL);
        CCopyMoveDialog dlg(HWindow, pathW, LoadStrW(IDS_CREATEDIRECTORY_TITLE),
                            &subject, IDD_CREATEDIRDIALOG,
                            Configuration.CreateDirHistory, CREATEDIR_HISTORY_SIZE,
                            FALSE);

    CREATE_AGAIN:

        if (dlg.Execute() == IDOK)
        {
            UpdateWindow(MainWindow->HWindow);
            sally::filesystem::CreateDirectoryPlan plan;
            sally::filesystem::CreateDirectoryFailure failure;
            if (!sally::filesystem::PrepareCreateDirectoryTargetW(pathW, GetPathW(), plan, &failure))
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORCREATINGDIR), LoadStrW(failure.errorTextId));
                goto CREATE_AGAIN;
            }

            std::wstring rootPathW = GetRootPath(plan.fullPath.c_str());
            if (SalCheckPathW(TRUE, rootPathW.c_str(), ERROR_SUCCESS, TRUE, HWindow) != ERROR_SUCCESS)
                goto CREATE_AGAIN;

            if (!sally::filesystem::DirectoryExistsW(plan.parentPath))
            {
                bool createParents = true;
                if (Configuration.CnfrmCreateDir)
                {
                    std::wstring msg = FormatStrW(LoadStrW(IDS_CREATEDIRECTORY), plan.parentPath.c_str());
                    bool dontShow = !Configuration.CnfrmCreateDir;
                    PromptResult res = gPrompter->ConfirmWithCheckbox(LoadStrW(IDS_QUESTION), msg.c_str(),
                                                                      LoadStrW(IDS_DONTSHOWAGAINCD), &dontShow);
                    Configuration.CnfrmCreateDir = !dontShow;
                    createParents = res.type == PromptResult::kOk;
                }
                if (!createParents)
                    goto CREATE_AGAIN;
            }

            std::wstring firstCreatedDirW;
            while (!sally::filesystem::EnsureDirectoryTreeExistsW(plan.parentPath, true, &firstCreatedDirW, &failure))
            {
                std::wstring errorText = failure.errorTextId != 0 ? LoadStrW(failure.errorTextId) : GetErrorTextOwned(failure.errorCode).c_str();
                if (gPrompter->AskRetryCancel(LoadStrW(IDS_ERRORCREATINGDIR), errorText.c_str()).type != PromptResult::kRetry)
                    goto CREATE_AGAIN;
            }

            if (!firstCreatedDirW.empty())
            {
                std::wstring notifyPathW = firstCreatedDirW;
                CutDirectoryW(notifyPathW);
                MainWindow->PostChangeOnPathNotificationW(notifyPathW.c_str(), FALSE);
            }

            while (1)
            {
                HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

                DWORD err = ERROR_SUCCESS;
                BOOL invalidName = sally::filesystem::IsManualCreateLeafInvalidW(plan.fullPath);
                if (!invalidName && SalCreateDirectoryExW(plan.fullPath.c_str(), &err))
                {
                    SetCursor(oldCur);

                    NextFocusNameW = plan.nextFocus;
                    MainWindow->PostChangeOnPathNotificationW(plan.parentPath.c_str(), FALSE);

                    EndStopRefresh(); // snooper will start again now
                    return;
                }

                if (invalidName)
                    err = ERROR_INVALID_NAME;
                SetCursor(oldCur);

                if (gPrompter->AskRetryCancel(LoadStrW(IDS_ERRORCREATINGDIR), GetErrorTextOwned(err).c_str()).type != PromptResult::kRetry)
                    goto CREATE_AGAIN;
            }
        }
    }
    else
    {
        if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
            GetPluginFS()->IsServiceSupported(FS_SERVICE_CREATEDIR)) // FS is in the panel
        {
            // lower the thread priority to "normal" (so operations don't overload the machine)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

            std::wstring newName;
            BOOL cancel = FALSE;
            BOOL ret = GetPluginFS()->CreateDir(GetPluginFS()->GetPluginFSName(), 1, HWindow, newName, cancel);
            if (!cancel) // not a cancel of the operation
            {
                if (!ret)
                {
                    CTruncatedString subject;
                    subject.SetW(LoadStrW(IDS_CREATEDIRECTORY_TEXT), NULL);
                    CCopyMoveDialog dlg(HWindow, pathW, LoadStrW(IDS_CREATEDIRECTORY_TITLE),
                                        &subject, IDD_CREATEDIRDIALOG,
                                        Configuration.CreateDirHistory, CREATEDIR_HISTORY_SIZE,
                                        FALSE);
                    while (1)
                    {
                        // open the standard dialog
                        if (dlg.Execute() == IDOK)
                        {
                            newName = pathW;
                            ret = GetPluginFS()->CreateDir(GetPluginFS()->GetPluginFSName(), 2, HWindow, newName, cancel);
                            if (ret || cancel)
                                break; // not an error (cancel or success)
                            pathW = newName;
                        }
                        else
                        {
                            WaitForESCRelease();
                            cancel = TRUE;
                            break;
                        }
                    }
                }

                if (ret && !cancel) // operation completed successfully
                {
                    NextFocusNameW = newName; // ensure focus of the new name after refresh
                }
            }

            // raise the thread priority again, the operation has finished
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        }
    }

    UpdateWindow(MainWindow->HWindow);
    EndStopRefresh(); // snooper will start again now
}

void CFilesWindow::RenameFileInternal(CFileData* f, const std::wstring& newName, BOOL* mayChange, BOOL* tryAgain)
{
    *tryAgain = TRUE;
    // Do NOT clean the MASK here - only the expanded result below, as pre-unicode did.
    // MakeValidFileNameW strips a trailing dot, and a trailing dot is what makes the
    // standard mask "*." mean "drop the extension"; trimming it first turned that mask
    // into "*", so the rename expanded to the name the file already had and was thrown
    // away by the same-name check.
    const wchar_t* formatedFileName = newName.c_str();
    const wchar_t* s = formatedFileName;
    while (*s != 0 && *s != L'\\' && *s != L'/' && *s != L':' &&
           *s >= 32 && *s != L'<' && *s != L'>' && *s != L'|' && *s != L'"')
        s++;
    if (formatedFileName[0] != 0 && *s == 0)
    {
        std::wstring finalName = MaskNameOwnedW(f->Name, formatedFileName);
        MakeValidFileNameW(finalName);

        std::wstring basePath = GetPathW();
        SalPathAddBackslashW(basePath);
        std::wstring tgtPathW = basePath;
        tgtPathW += finalName;
        std::wstring pathW = basePath;
        pathW += f->Name;
            if (sally::unicode::ArePathsExactlySame(NULL, NULL, pathW, tgtPathW))
            {
                *tryAgain = FALSE;
                return; // no-op rename (same name)
            }

            BOOL ret = FALSE;

            // try renaming from the long name first and if there is a problem then
            // from the DOS name (handles files/directories accessible only via Unicode or DOS names) -
            // pathW/tgtPathW are owned UTF-16 paths, with no sizing or encoding bridge.

            BOOL handsOFF = FALSE;
            CFilesWindow* otherPanel = MainWindow->GetNonActivePanel();
            // The panel's narrow ANSI mirror was removed (fileswnd.h) - GetPathW() is
            // unconditionally the authoritative panel path now, so compare it directly.
            std::wstring otherPathW = otherPanel->GetPathW();
            // are we changing the path of the other panel?
            if (otherPathW.length() >= pathW.length() &&
                _wcsnicmp(pathW.c_str(), otherPathW.c_str(), pathW.length()) == 0 &&
                (otherPathW.length() == pathW.length() || otherPathW[pathW.length()] == L'\\'))
            {
                otherPanel->HandsOff(TRUE);
                handsOFF = TRUE;
            }

            *mayChange = TRUE;
            FileResult moveResult = gFileSystem->MoveFile(pathW.c_str(), tgtPathW.c_str());
            BOOL moveRet = moveResult.success;
            DWORD err = moveRet ? ERROR_SUCCESS : moveResult.errorCode;
            if (!moveRet)
            {
                if ((err == ERROR_FILE_NOT_FOUND || err == ERROR_INVALID_NAME) &&
                    f->DosName != NULL)
                {
                    pathW = basePath;
                    pathW += f->DosName;
                    moveResult = gFileSystem->MoveFile(pathW.c_str(), tgtPathW.c_str());
                    moveRet = moveResult.success;
                    if (!moveRet)
                        err = moveResult.errorCode;
                    pathW = basePath;
                    pathW += f->Name;
                }
            }

            if (moveRet)
            {

            REN_OPERATION_DONE:

                NextFocusNameW = finalName;
                ret = TRUE;
            }
            else
            {
                // wide: pathW/tgtPathW are full paths - a non-ASCII component
                // anywhere in either could collide on the CP_ACP mirror alone, wrongly treating
                // a genuine rename as a mere case-change no-op. Two wide-identical names are
                // always narrow-identical too (the mirror is a deterministic function of the
                // wide source), but not the reverse - so the wide compare alone is authoritative
                // here, not an addition to the narrow one. pathW/tgtPathW stay in sync with
                // the retry path; pathW is restored after the DOS-name retry above.
                if (_wcsicmp(pathW.c_str(), tgtPathW.c_str()) != 0 && // if it isn't just change-case
                    (err == ERROR_FILE_EXISTS ||   // check whether it's only rewriting the DOS name of the file
                     err == ERROR_ALREADY_EXISTS))
                {
                    WIN32_FIND_DATAW data;
                    HANDLE find = SalFindFirstFileHW(tgtPathW.c_str(), &data);
                    if (find != INVALID_HANDLE_VALUE)
                    {
                        gFileSystem->CloseFind(find);
                        // wide: the old cFileNameA mirror could collapse two
                        // distinct non-ASCII on-disk names to the same '?'-mirror, wrongly
                        // reporting "full name matches" and skipping the DOS-alias cleanup
                        // below. data.cFileName/cAlternateFileName are already the genuine wide
                        // forms (no conversion needed); tgtPathW is unmodified since its
                        // declaration above.
                        const wchar_t* tgtNameW = SalPathFindFileNameW(tgtPathW.c_str());
                        if (_wcsicmp(tgtNameW, data.cAlternateFileName) == 0 && // match only for DOS name
                            _wcsicmp(tgtNameW, data.cFileName) != 0)           // (full name differs)
                        {
                            // rename ("clean up") the file/directory with the conflicting DOS name to a temporary 8.3 name (no extra DOS name needed)
                            std::wstring tmpNameW = tgtPathW;
                            CutDirectoryW(tmpNameW);
                            SalPathAddBackslashW(tmpNameW);
                            size_t tmpNamePartPos = tmpNameW.size();
                            std::wstring origFullNameW = tmpNameW;
                            SalPathAppendW(origFullNameW, data.cFileName); // use wide cFileName directly
                            tmpNameW = origFullNameW;
                            {
                                DWORD num = (GetTickCount() / 10) % 0xFFF;
                                while (1)
                                {
                                    wchar_t tmpSuffix[8];
                                    swprintf(tmpSuffix, _countof(tmpSuffix), L"sal%03X", num++);
                                    tmpNameW.resize(tmpNamePartPos);
                                    tmpNameW += tmpSuffix;
                                    FileResult tempMove = gFileSystem->MoveFile(origFullNameW.c_str(),
                                                                               tmpNameW.c_str());
                                    if (tempMove.success)
                                        break;
                                    DWORD e = tempMove.errorCode;
                                    if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS)
                                    {
                                        tmpNameW.clear();
                                        break;
                                    }
                                }
                                if (!tmpNameW.empty()) // if we successfully "cleaned" the conflicting file, try moving
                                {                      // the file again, then return the temporary file its original name
                                    BOOL moveDone = gFileSystem->MoveFile(pathW.c_str(), tgtPathW.c_str()).success;
                                    if (!gFileSystem->MoveFile(tmpNameW.c_str(), origFullNameW.c_str()).success)
                                    { // this can apparently happen: Windows creates a file named origFullName instead of 'tgtPath' (DOS name)
                                        TRACE_I("CFilesWindow::RenameFileInternal(): Unexpected situation: unable to rename file from tmp-name to original long file name!");
                                        if (moveDone)
                                        {
                                            if (gFileSystem->MoveFile(tgtPathW.c_str(), pathW.c_str()).success)
                                                moveDone = FALSE;
                                            if (!gFileSystem->MoveFile(tmpNameW.c_str(), origFullNameW.c_str()).success)
                                                TRACE_E("CFilesWindow::RenameFileInternal(): Fatal unexpected situation: unable to rename file from tmp-name to original long file name!");
                                        }
                                    }

                                    if (moveDone)
                                        goto REN_OPERATION_DONE;
                                }
                            }
                        }
                    }
                }
                // wide: same collision risk and same reasoning as the "just
                // change-case" gate above - the wide compare alone is authoritative.
                if ((err == ERROR_ALREADY_EXISTS ||
                     err == ERROR_FILE_EXISTS) &&
                    _wcsicmp(pathW.c_str(), tgtPathW.c_str()) != 0) // overwrite the file?
                {
                    DWORD inAttr = gFileSystem->GetFileAttributes(pathW.c_str());
                    DWORD outAttr = gFileSystem->GetFileAttributes(tgtPathW.c_str());

                    if ((inAttr & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                        (outAttr & FILE_ATTRIBUTE_DIRECTORY) == 0)
                    { // only if both are files
                        HANDLE in = gFileSystem->CreateFile(pathW.c_str(), 0,
                                                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                        HANDLE out = gFileSystem->CreateFile(tgtPathW.c_str(), 0,
                                                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                        if (in != INVALID_HANDLE_VALUE && out != INVALID_HANDLE_VALUE)
                        {
                            // The wide info generator already existed and the
                            // wide paths are right here - this was building the attr strings
                            // narrow only because the dialog took char*.
                            wchar_t iAttr[101], oAttr[101];
                            GetFileOverwriteInfoW(iAttr, _countof(iAttr), in, pathW.c_str());
                            GetFileOverwriteInfoW(oAttr, _countof(oAttr), out, tgtPathW.c_str());
                            gFileSystem->CloseFileHandle(in);
                            gFileSystem->CloseFileHandle(out);

                            COverwriteDlg dlg(HWindow, tgtPathW.c_str(), oAttr, pathW.c_str(), iAttr, TRUE);
                            int res = (int)dlg.Execute();

                            switch (res)
                            {
                            case IDCANCEL:
                                ret = TRUE;
                            case IDNO:
                                err = ERROR_SUCCESS;
                                break;

                            case IDYES:
                            {
                                ClearReadOnlyAttr(tgtPathW.c_str()); // so it can be deleted ...
                                FileResult deleteResult = gFileSystem->DeleteFile(tgtPathW.c_str());
                                if (!deleteResult.success)
                                    err = deleteResult.errorCode;
                                else
                                {
                                    moveResult = gFileSystem->MoveFile(pathW.c_str(), tgtPathW.c_str());
                                    err = moveResult.success ? ERROR_SUCCESS : moveResult.errorCode;
                                }
                                if (err == ERROR_SUCCESS)
                                {
                                    ret = TRUE;
                                    NextFocusNameW = finalName;
                                }
                                break;
                            }
                            }
                        }
                        else
                        {
                            if (in == INVALID_HANDLE_VALUE)
                                TRACE_E("Unable to open file " << sally::diagnostic::EncodeAcpLossy(pathW));
                            else
                                gFileSystem->CloseFileHandle(in);
                            if (out == INVALID_HANDLE_VALUE)
                                TRACE_E("Unable to open file " << sally::diagnostic::EncodeAcpLossy(tgtPathW));
                            else
                                gFileSystem->CloseFileHandle(out);
                        }
                    }
                }

                if (err != ERROR_SUCCESS)
                {
                    gPrompter->ShowError(LoadStrW(IDS_ERRORRENAMINGFILE), GetErrorTextOwned(err).c_str());
                }
            }
            if (handsOFF)
                otherPanel->HandsOff(FALSE);
            *tryAgain = !ret;
    }
    else
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORRENAMINGFILE), GetErrorTextOwned(ERROR_INVALID_NAME).c_str());
    }
}

void CFilesWindow::RenameFile(int specialIndex)
{
    CALL_STACK_MESSAGE2("CFilesWindow::RenameFile(%d)", specialIndex);

    int i;
    if (specialIndex != -1)
        i = specialIndex;
    else
        i = GetCaretIndex();
    if (i < 0 || i >= Dirs->Count + Files->Count)
        return; // invalid index

    BOOL subDir;
    if (Dirs->Count > 0)
        subDir = (wcscmp(Dirs->At(0).Name, L"..") == 0);
    else
        subDir = FALSE;
    if (i == 0 && subDir)
        return; // we do not work with ".."

    CFileData* f = NULL;
    BOOL isDir = i < Dirs->Count;
    f = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);

    const std::wstring formatedFileName =
        AlterFileNameW(f->Name, Configuration.FileNameFormat, 0, isDir);

    CTruncatedString subject;
    {
        // real fix: the item's name isn't representable in CP_ACP (or the panel
        // path itself is lossy), so the old narrow formatted name couldn't stand in for it.
        // Build the subject from the genuine wide name instead of substituting the literal placeholder
        // "..." for it. Mirrors this file's own NTFS-confirm subject block a few hundred lines
        // above (AlterFileNameW/EffectiveItemNameW/FormatStrW/SetW).
        std::wstring buffW = FormatStrW(LoadStrW(IDS_RENAME_TO),
                                        LoadStrW(isDir ? IDS_QUESTION_DIRECTORY : IDS_QUESTION_FILE));
        subject.SetW(buffW.c_str(), formatedFileName.c_str());
    }
    std::wstring initialRenameNameW = f->Name;
    CCopyMoveDialog dlg(HWindow, initialRenameNameW, LoadStrW(IDS_RENAME_TITLE),
                        &subject, IDD_RENAMEDIALOG, Configuration.QuickRenameHistory,
                        QUICKRENAME_HISTORY_SIZE, FALSE);

    if (Is(ptDisk)) // rename on disk
    {
#ifndef _WIN64
        if (Windows64Bit && isDir)
        {
            std::wstring pathBuf = GetPathW();
            SalPathAppendW(pathBuf, f->Name);
            if (IsWin64RedirectedDir(pathBuf.c_str(), NULL, FALSE))
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), pathBuf.c_str());
                return;
            }
        }
#endif // _WIN64

        BeginSuspendMode(); // snooper takes a break

        BOOL mayChange = FALSE;
        while (1)
        {
            // if no item is selected, select the one under focus and store its name
            const std::wstring temporarySelected = SelectFocusedItemAndGetName();

            // Since Windows Vista, Microsoft introduced a demanded feature: quick rename selects only the name without the dot and extension
            // the same code appears here four times
            if (!Configuration.QuickRenameSelectAll)
            {
                int selectionEnd = -1;
                if (!isDir)
                {
                    const wchar_t* dot = wcsrchr(initialRenameNameW.c_str(), L'.');
                    if (dot != NULL && dot > initialRenameNameW.c_str())
                        selectionEnd = (int)(dot - initialRenameNameW.c_str());
                }
                dlg.SetSelectionEnd(selectionEnd);
            }

            int dlgRes = (int)dlg.Execute();

            // if we selected an item, we deselect it again
            UnselectItemWithName(temporarySelected);

            if (dlgRes == IDOK)
            {
                UpdateWindow(MainWindow->HWindow);

                BOOL tryAgain;
                RenameFileInternal(f, initialRenameNameW, &mayChange, &tryAgain);
                if (!tryAgain)
                    break;
            }
            else
                break;
        }

        // refresh of manually refreshed directories
        if (mayChange)
        {
            // change in the directory shown in the panel and, if a directory was renamed, then also in subdirectories
            MainWindow->PostChangeOnPathNotificationW(GetPathW(), isDir);
        }

        // if a Salamander window is active, end suspend mode
        EndSuspendMode();
    }
    else
    {
        if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
            GetPluginFS()->IsServiceSupported(FS_SERVICE_QUICKRENAME)) // FS is in the panel
        {
            BeginSuspendMode(); // snooper takes a break

            // lower the thread priority to "normal" (so operations don't overload the machine)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

            std::wstring newName;
            BOOL cancel = FALSE;

            // if no item is selected, select the one under focus and store its name
            std::wstring temporarySelected = SelectFocusedItemAndGetName();

            BOOL ret = GetPluginFS()->QuickRename(GetPluginFS()->GetPluginFSName(), 1, HWindow, *f, isDir, newName, cancel);

            // if we selected an item, we deselect it again
            UnselectItemWithName(temporarySelected);

            if (!cancel) // not a cancel of the operation
            {
                if (!ret)
                {
                    while (1)
                    {
                        // open the standard dialog
                        // if no item is selected, select the one under focus and store its name
                        temporarySelected = SelectFocusedItemAndGetName();

                        // Since Windows Vista, Microsoft introduced a demanded feature: quick rename selects only the name without the dot and extension
                        // the same code appears here four times
                        if (!Configuration.QuickRenameSelectAll)
                        {
                            int selectionEnd = -1;
                            if (!isDir)
                            {
                                const wchar_t* dot = wcsrchr(formatedFileName.c_str(), L'.');
                                if (dot != NULL && dot > formatedFileName.c_str()) // although ".cvspass" is an extension in Windows, Explorer selects the entire name, so we do the same
                                                                           //        if (dot != NULL)
                                    selectionEnd = (int)(dot - formatedFileName.c_str());
                            }
                            dlg.SetSelectionEnd(selectionEnd);
                        }

                        int dlgRes = (int)dlg.Execute();

                        // if we selected an item, we deselect it again
                        UnselectItemWithName(temporarySelected);

                        if (dlgRes == IDOK)
                        {
                            newName = initialRenameNameW;
                            ret = GetPluginFS()->QuickRename(GetPluginFS()->GetPluginFSName(), 2, HWindow, *f, isDir, newName, cancel);
                            if (ret || cancel)
                                break; // not an error (cancel or success)
                            initialRenameNameW = newName;
                        }
                        else
                        {
                            WaitForESCRelease();
                            cancel = TRUE;
                            break;
                        }
                    }
                }

                if (ret && !cancel) // operation completed successfully
                {
                    NextFocusNameW = newName; // ensure focus of the new name after refresh
                }
            }

            // raise the thread priority again, the operation has finished
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

            // if a Salamander window is active, end suspend mode
            EndSuspendMode();
        }
    }
}

void CFilesWindow::CancelUI()
{
    if (QuickSearchMode)
        EndQuickSearch();
    QuickRenameEnd();
}

BOOL CFilesWindow::IsQuickRenameActive()
{
    return QuickRenameWindow.HWindow != NULL;
}

void CFilesWindow::AdjustQuickRenameRectW(const wchar_t* text, RECT* r)
{
    // measure the length of the text
    HDC hDC = HANDLES(GetDC(ListBox->HWindow));
    HFONT hOldFont = (HFONT)SelectObject(hDC, Font);
    SIZE sz;
    // Measured on the real text. The CP_ACP mirror can be a different
    // width entirely - every unspellable character collapses to one '?' byte - so the
    // edit box was sized for a string the user is not looking at.
    GetTextExtentPoint32W(hDC, text, (int)wcslen(text), &sz);
    TEXTMETRIC tm;
    GetTextMetrics(hDC, &tm);
    SelectObject(hDC, hOldFont);
    HANDLES(ReleaseDC(ListBox->HWindow, hDC));

    int minWidth = QuickRenameRect.right - QuickRenameRect.left + 2;
    int minHeight = QuickRenameRect.bottom - QuickRenameRect.top;

    int optimalWidth = sz.cx + 4 + tm.tmHeight;

    r->left--;

    r->right = r->left + optimalWidth;

    if (r->right - r->left < minWidth)
        r->right = r->left + minWidth;

    // we do not want to exceed the panel boundaries
    RECT maxR = ListBox->FilesRect;
    if (r->left < maxR.left)
        r->left = maxR.left;
    if (r->right > maxR.right)
        r->right = maxR.right;
}

void CFilesWindow::AdjustQuickRenameWindow()
{
    if (!IsQuickRenameActive())
    {
        //    TRACE_E("QuickRenameWindow is not active.");
        return;
    }

    RECT r;
    GetWindowRect(QuickRenameWindow.HWindow, &r);
    MapWindowPoints(NULL, HWindow, (POINT*)&r, 2);

    const std::wstring buffW = GetWindowTextStringW(QuickRenameWindow.HWindow);
    AdjustQuickRenameRectW(buffW.c_str(), &r);
    SetWindowPos(QuickRenameWindow.HWindow, NULL, 0, 0,
                 r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

/*
// I ran into a sorting issue: Vista keeps items in place, but Salamander needs to insert them
// so I'm shelving this for now
void
CFilesWindow::QuickRenameOnIndex(int index)
{
  if (index >= 0 && index < Dirs->Count + Files->Count)
  {
    QuickRenameIndex = index;
    SetCaretIndex(index, FALSE);

    RECT r;
    if (ListBox->GetItemRect(index, &r))
    {
      ListBox->GetIndex(r.left, r.top, FALSE, &QuickRenameRect);
      QuickRenameIndex = index;
      QuickRenameBegin(index, &QuickRenameRect);
    }
  }
}
*/

void CFilesWindow::QuickRenameBegin(int index, const RECT* labelRect)
{
    CALL_STACK_MESSAGE2("CFilesWindow::QuickRenameBegin(%d, )", index);

    if (!(Is(ptDisk) || Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
                            GetPluginFS()->IsServiceSupported(FS_SERVICE_QUICKRENAME)))
        return;

    if (QuickRenameWindow.HWindow != NULL)
    {
        TRACE_E("Quick Rename is already active");
        return;
    }

    if (index < 0 || index >= Dirs->Count + Files->Count)
        return; // invalid index

    BOOL subDir;
    if (Dirs->Count > 0)
        subDir = (wcscmp(Dirs->At(0).Name, L"..") == 0);
    else
        subDir = FALSE;
    if (index == 0 && subDir)
        return; // we do not work with ".."

    CFileData* f = NULL;
    BOOL isDir = index < Dirs->Count;
    f = isDir ? &Dirs->At(index) : &Files->At(index - Dirs->Count);

    // The whole in-place rename ran on the CP_ACP mirror: the edit box was
    // SEEDED with the mangled name and the result was READ BACK narrow. Renaming a file
    // the code page cannot spell was therefore impossible - confirming the unchanged box
    // renamed the file to its own '?'-string, or failed. Both ends are wide now.
    const std::wstring formatedFileNameW =
        AlterFileNameW(f->Name,
                       Configuration.FileNameFormat, 0, isDir);
    // The narrow twin this comment referred to (formatedFileName, fed via the
    // now-removed AlterFileName) had no remaining reader anywhere in this function - the
    // plugin-FS branch below only ever used formatedFileNameW/newName. Dead code, deleted
    // rather than mechanically widened.

    // Since Windows Vista, Microsoft introduced a demanded feature: quick rename selects only the name without the dot and extension
    // the same code appears here four times
    int selectionEnd = -1;
    if (!Configuration.QuickRenameSelectAll)
    {
        if (!isDir)
        {
            const size_t dot = formatedFileNameW.find_last_of(L'.');
            if (dot != std::wstring::npos && dot > 0) // although ".cvspass" is an extension in Windows, Explorer selects the entire name, so we do the same
                selectionEnd = (int)dot;
        }
    }

    // if this is a FS, we must first call QuickRename with mode=1
    // allowing the file system to open its own rename dialog
    if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
        GetPluginFS()->IsServiceSupported(FS_SERVICE_QUICKRENAME)) // FS is in the panel
    {
        BeginSuspendMode(); // snooper takes a break

        // lower the thread priority to "normal" (so operations don't overload the machine)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

        std::wstring newName;
        BOOL cancel = FALSE;

        // if no item is selected, select the one under focus and store its name
        const std::wstring temporarySelected = SelectFocusedItemAndGetName();

        BOOL ret = GetPluginFS()->QuickRename(GetPluginFS()->GetPluginFSName(), 1, HWindow, *f, isDir, newName, cancel);

        // if we selected an item, we deselect it again
        UnselectItemWithName(temporarySelected);

        if (ret && !cancel) // operation completed successfully
        {
            NextFocusNameW = newName; // ensure focus of the new name after refresh
        }

        // raise the thread priority again, the operation has finished
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        // if a Salamander window is active, end suspend mode
        EndSuspendMode();

        if (cancel || ret)
            return;
    }

    RECT r = *labelRect;
    AdjustQuickRenameRectW(formatedFileNameW.c_str(), &r);

    // CWindow's wide-only CreateEx adapter, so winlib
    // subclasses it through SetWindowLongPtrW. Both matter: the W creation puts the real
    // name in the control, and the W subclass keeps WM_CHAR in UTF-16 so a character the
    // code page cannot spell survives being TYPED as well as being displayed.
    HWND hWnd = QuickRenameWindow.CreateEx(0,
                                            L"edit",
                                            formatedFileNameW.c_str(),
                                            WS_BORDER | WS_CHILD | WS_CLIPSIBLINGS | ES_AUTOHSCROLL | ES_LEFT,
                                            r.left, r.top, r.right - r.left, r.bottom - r.top,
                                            GetListBoxHWND(),
                                            NULL,
                                            HInstance,
                                            &QuickRenameWindow);
    if (hWnd == NULL)
    {
        TRACE_E("Cannot create QuickRenameWindow");
        return;
    }

    BeginSuspendMode(TRUE); // snooper takes a break

    // font the same as the panel
    SendMessage(hWnd, WM_SETFONT, (WPARAM)Font, 0);
    int leftMargin = LOWORD(SendMessage(hWnd, EM_GETMARGINS, 0, 0));
    if (leftMargin < 2)
        SendMessage(hWnd, EM_SETMARGINS, EC_LEFTMARGIN, 2);

    //SendMessage(hWnd, EM_SETSEL, 0, -1); // select all
    // we can select only the name without dot and extension
    SendMessage(hWnd, EM_SETSEL, 0, selectionEnd);

    ShowWindow(hWnd, SW_SHOW);
    SetFocus(hWnd);
    return;
}

void CFilesWindow::QuickRenameEnd()
{
    CALL_STACK_MESSAGE1("CFilesWindow::QuickRenameEnd()");
    if (QuickRenameWindow.HWindow != NULL && QuickRenameWindow.GetCloseEnabled())
    {
        // if a Salamander window is active, end suspend mode
        EndSuspendMode(TRUE);

        // avoid cycles caused by WM_KILLFOCUS and similar
        BOOL old = QuickRenameWindow.GetCloseEnabled();
        QuickRenameWindow.SetCloseEnabled(FALSE);

        DestroyWindow(QuickRenameWindow.HWindow);

        QuickRenameWindow.SetCloseEnabled(old);
    }
}

BOOL CFilesWindow::HandeQuickRenameWindowKey(WPARAM wParam)
{
    CALL_STACK_MESSAGE2("CFilesWindow::HandeQuickRenameWindowKey(0x%IX)", wParam);

    if (wParam == VK_ESCAPE)
    {
        QuickRenameEnd();
        return TRUE;
    }

    int index = GetCaretIndex();
    if (index < 0 || index >= Dirs->Count + Files->Count)
        return TRUE; // invalid index
    CFileData* f = NULL;
    BOOL isDir = index < Dirs->Count;
    f = isDir ? &Dirs->At(index) : &Files->At(index - Dirs->Count);

    QuickRenameWindow.SetCloseEnabled(FALSE);

    HWND hWnd = QuickRenameWindow.HWindow;
    // The name the user actually typed. GetWindowText (ANSI) returned its
    // CP_ACP mirror, so a name outside the code page arrived here as '?'-bytes and the
    // file was renamed to those bytes - or the rename failed - with no way for the user
    // to tell why.
    std::wstring newNameW = GetWindowTextStringW(hWnd);

    // lower the thread priority to "normal" (so operations don't overload the machine)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    BOOL tryAgain = FALSE;
    BOOL mayChange = FALSE;
    if (Is(ptDisk))
    {
        // If this is an in-place rename and the user didn't change the name, we shouldn't
        // attempt to rename it because the user might be on a CD-ROM or other read-only disk
        // and we would display the "Access is denied" error. The user has no mouse option
        // to cancel the operation, so they would have to press Escape.
        // Explorer behaves this way now.
        // Compared wide, or a rename that only changes characters the code page cannot
        // spell reads as "unchanged" and is silently skipped.
        if (f->Name != newNameW)
            RenameFileInternal(f, newNameW, &mayChange, &tryAgain);
    }
    else if (Is(ptPluginFS) && GetPluginFS()->NotEmpty() &&
             GetPluginFS()->IsServiceSupported(FS_SERVICE_QUICKRENAME)) // FS is in the panel
    {
        // CPluginFSInterfaceEncapsulation::QuickRename is wide-native now
        // (plugins.h) - read the edit control wide directly, so a name the code page
        // cannot spell round-trips correctly instead of arriving here as '?'-bytes.
        // open the standard dialog
        BOOL cancel;
        BOOL ret = GetPluginFS()->QuickRename(GetPluginFS()->GetPluginFSName(), 2, HWindow, *f, isDir, newNameW, cancel);
        if (!ret && !cancel)
        {
            tryAgain = TRUE;
            SetWindowTextW(hWnd, newNameW.c_str());
        }
        else
        {
            if (ret && !cancel) // operation completed successfully
            {
                NextFocusNameW = newNameW; // ensure focus of the new name after refresh
            }
        }
    }

    // raise the thread priority again, the operation has finished
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    // refresh of manually refreshed directories
    if (mayChange)
    {
        // change in the directory shown in the panel and if a directory was renamed, then also in subdirectories
        MainWindow->PostChangeOnPathNotificationW(GetPathW(), isDir);
    }

    QuickRenameWindow.SetCloseEnabled(TRUE);
    if (!tryAgain)
    {
        QuickRenameEnd();
        //    if (wParam == VK_TAB)
        //    {
        //      BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        //      PostMessage(HWindow, WM_USER_RENAME_NEXT_ITEM, !shiftPressed, 0);
        //    }
        return TRUE;
    }
    else
    {
        SetFocus(QuickRenameWindow.HWindow);
        return FALSE;
    }
}

void CFilesWindow::KillQuickRenameTimer()
{
    if (QuickRenameTimer != 0)
    {
        KillTimer(GetListBoxHWND(), QuickRenameTimer);
        QuickRenameTimer = 0;
    }
}

//****************************************************************************
//
// CQuickRenameWindow
//

CQuickRenameWindow::CQuickRenameWindow()
    : CWindow(ooStatic) // see QuickRenameBegin
{
    FilesWindow = NULL;
    CloseEnabled = TRUE;
    SkipNextCharacter = FALSE;
}

void CQuickRenameWindow::SetPanel(CFilesWindow* filesWindow)
{
    FilesWindow = filesWindow;
}

void CQuickRenameWindow::SetCloseEnabled(BOOL closeEnabled)
{
    CloseEnabled = closeEnabled;
}

BOOL CQuickRenameWindow::GetCloseEnabled()
{
    return CloseEnabled;
}

LRESULT
CQuickRenameWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CHAR:
    {
        if (SkipNextCharacter)
        {
            SkipNextCharacter = FALSE; // prevent a beep
            return FALSE;
        }

        if (wParam == VK_ESCAPE || wParam == VK_RETURN /*|| wParam == VK_TAB*/)
        {
            FilesWindow->HandeQuickRenameWindowKey(wParam);
            return 0;
        }
        break;
    }

    case WM_KEYDOWN:
    {
        if (wParam == 'A')
        {
            // since Windows Vista, SelectAll works by default, so we leave select-all to them
            if (!WindowsVistaAndLater)
            {
                BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
                BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (controlPressed && !shiftPressed && !altPressed)
                {
                    SendMessage(HWindow, EM_SETSEL, 0, -1);
                    SkipNextCharacter = TRUE; // prevent a beep
                    return 0;
                }
            }
        }
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}
