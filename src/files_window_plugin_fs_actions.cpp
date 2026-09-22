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
#include "zip.h"
#include "shellib.h"
#include "pack.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/unicode/AnsiFallbackPolicy.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/IFileSystem.h"
#include "common/widepath.h"
#include "common/IEnvironment.h"

void CFilesWindow::PluginFSFilesAction(CPluginFSActionType type)
{
    CALL_STACK_MESSAGE2("CFilesWindow::PluginFSFilesAction(%d)", type);
    if (Dirs->Count + Files->Count == 0)
        return;
    if (!Is(ptPluginFS) || !GetPluginFS()->NotEmpty())
        return;
    int panel = MainWindow->LeftPanel == this ? PANEL_LEFT : PANEL_RIGHT;
    CFilesWindow* target = (MainWindow->LeftPanel == this ? MainWindow->RightPanel : MainWindow->LeftPanel);
    BOOL unselect = FALSE;

    BeginSuspendMode(); // the snooper takes a break
    BeginStopRefresh(); // to suppress path change messages

    int count = GetSelCount();
    int selectedDirs = 0;
    if (count > 0)
    {
        // count how many directories are selected (the remaining selected items are files)
        int i;
        for (i = 0; i < Dirs->Count; i++) // ".." cannot be selected, the check would be useless
        {
            if (Dirs->At(i).Selected)
                selectedDirs++;
        }
    }
    else
        count = 0;

    // wide - CMessageBox::DialogProc reads Text.GetW() unconditionally (no
    // IsWide() fallback), so a narrow-only 'str' below showed an EMPTY confirmation
    // dialog body on every plugin-FS delete - not just a non-ASCII-name defect, this
    // was blank for every deletion. subject/formatedFileName/expanded were narrow
    // fixed scratch buffers used only to build 'str'; widened in place.
    std::wstring formatedFileNameW;
    std::wstring expandedW;
    if (count <= 1) // one selected item or none
    {
        int i;
        if (count == 0)
            i = GetCaretIndex();
        else
            GetSelItems(1, &i);

        if (i < 0 || i >= Dirs->Count + Files->Count)
        {
            EndStopRefresh();
            EndSuspendMode(); // the snooper resumes now
            return;           // invalid index (no files)
        }
        BOOL isDir = i < Dirs->Count;
        CFileData* f = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
        formatedFileNameW = AlterFileNameW(f->Name,
                                           Configuration.FileNameFormat, 0, isDir != FALSE);
        expandedW = LoadStrW(isDir ? IDS_QUESTION_DIRECTORY : IDS_QUESTION_FILE);
    }
    else
    {
        expandedW = ExpandPluralFilesDirsTextW(count - selectedDirs, selectedDirs, epfdmNormal, FALSE);
    }

    int resID = 0;
    switch (type)
    {
    case atCopy:
        resID = IDS_COPYTO;
        break;
    case atMove:
        resID = IDS_MOVETO;
        break;
    case atDelete:
        resID = IDS_CONFIRM_DELETE;
        break;
    }
    CTruncatedString str;
    if (resID != 0)
    {
        // IDS_COPYTO/IDS_MOVETO/IDS_CONFIRM_DELETE contain ampersands, cancel it
        wchar_t templW[200];
        lstrcpynW(templW, LoadStrW(resID), 200);
        RemoveAmpersands(templW);
        // same two-stage %s nesting as the sibling Pack/Unpack Subject fixes: templW's
        // %s is filled with expandedW, whose own unfilled %s (single-item case) is then
        // filled by SetW's internal substitution with the actual item name.
        const std::wstring subjectW = FormatStrW(templW, expandedW.c_str());
        str.SetW(subjectW.c_str(), count > 1 ? NULL : formatedFileNameW.c_str());
    }

    switch (type)
    {
    case fsatMove:
    case fsatCopy:
    {
        if (type == fsatMove && GetPluginFS()->IsServiceSupported(FS_SERVICE_MOVEFROMFS) ||
            type == fsatCopy && GetPluginFS()->IsServiceSupported(FS_SERVICE_COPYFROMFS)) // "always true"
        {
            // lower thread's priority to "normal" (so that operations don't burden the machine too much)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

            BOOL copy = (type == fsatCopy);
            BOOL operationMask = FALSE;
            BOOL cancelOrHandlePath = FALSE;

            std::wstring targetPath;
            if (target->Is(ptDisk))
                target->GetGeneralPath(targetPath);
            else
                targetPath.clear();

            BOOL ret = GetPluginFS()->CopyOrMoveFromFS(copy, 1, GetPluginFS()->GetPluginFSName(),
                                                       HWindow, panel, count - selectedDirs,
                                                       selectedDirs, targetPath, NULL, operationMask,
                                                       cancelOrHandlePath, NULL);
            while (!ret)
            {
                if (!cancelOrHandlePath) // standard dialog
                {
                    CCopyMoveDialog dlg(HWindow, targetPath,
                                       LoadStrW(copy ? IDS_COPY : IDS_MOVE), &str,
                                       copy ? IDD_COPYDIALOG : IDD_MOVEDIALOG,
                                       Configuration.CopyHistory, COPY_HISTORY_SIZE,
                                       TRUE);
                    if (dlg.Execute() == IDOK)
                    {
                        UpdateWindow(MainWindow->HWindow);

                        operationMask = FALSE;
                        cancelOrHandlePath = FALSE;
                        ret = GetPluginFS()->CopyOrMoveFromFS(copy, 2, GetPluginFS()->GetPluginFSName(),
                                                              HWindow, panel, count - selectedDirs,
                                                              selectedDirs, targetPath, NULL, operationMask,
                                                              cancelOrHandlePath, NULL);
                    }
                    else
                    {
                        UpdateWindow(MainWindow->HWindow);
                        ret = TRUE;
                        cancelOrHandlePath = TRUE; // cancel operation
                    }
                }
                else // standard path processing
                {
                    // restore DefaultDir
                    MainWindow->UpdateDefaultDir(MainWindow->GetActivePanel() == this);

                    // for disk paths flip '/' to '\\' and drop duplicate '\\'
                    if (targetPath.size() >= 2 &&
                        (targetPath[1] == L':' ||
                         ((targetPath[0] == L'/' || targetPath[0] == L'\\') &&
                          (targetPath[1] == L'/' || targetPath[1] == L'\\'))))
                    { // this is a disk path (absolute or relative) - normalize its separators
                        SlashesToBackslashesAndRemoveDups(targetPath);
                        targetPath.resize(wcslen(targetPath.c_str()));
                    }

                    const wchar_t* errTitle = LoadStrW(copy ? IDS_ERRORCOPY : IDS_ERRORMOVE);
                    BOOL pathError = FALSE;

                    const size_t len = targetPath.size();
                    BOOL backslashAtEnd = (len > 0 && targetPath[len - 1] == L'\\'); // path ends with backslash -> must be a directory
                    const wchar_t drive = len > 0 ? sally::unicode::FoldCharW(targetPath[0]) : 0;
                    BOOL mustBePath = (len == 2 && drive >= L'a' && drive <= L'z' &&
                                       targetPath[1] == L':'); // a path like "c:" must stay a path after expansion (not a file)

                    int pathType;
                    BOOL pathIsDir;
                    wchar_t* secondPart;
                    int error;
                    std::wstring targetMask;
                    if (ParsePathW(targetPath, pathType, pathIsDir, secondPart, errTitle, NULL, &error))
                    {
                        // instead of using a 'switch' statement, we use 'if' so that 'break' and 'continue' work properly
                        if (pathType == PATH_TYPE_WINDOWS) // Windows path (disk + UNC)
                        {
                            const size_t secondPartOffset = static_cast<size_t>(secondPart - targetPath.data());
                            if (SalSplitWindowsPathOwnedW(HWindow, LoadStrW(copy ? IDS_COPY : IDS_MOVE),
                                                          errTitle, count, targetPath, secondPartOffset,
                                                          pathIsDir, backslashAtEnd || mustBePath,
                                                          NULL, NULL, targetMask))
                            {
                                if (!operationMask && !targetMask.empty() &&
                                    (targetMask == L"*.*" || targetMask == L"*"))
                                    targetMask.clear();
                                if (!operationMask && !targetMask.empty()) // mask exists but isn't allowed
                                {
                                    if (!targetPath.empty() && targetPath.back() != L'\\')
                                        targetPath.push_back(L'\\');
                                    targetPath += targetMask;
                                    targetMask.clear();

                                    gPrompter->ShowError(errTitle, LoadStrW(IDS_FSCOPYMOVE_OPMASKSNOTSUP));
                                    pathError = TRUE; // path error -> mode==4
                                }
                            }
                            else
                            {
                                pathError = TRUE; // path error -> mode==4
                            }
                        }
                        else
                        {
                            gPrompter->ShowError(errTitle,
                                                 LoadStrW(pathType == PATH_TYPE_ARCHIVE ? IDS_FSCOPYMOVE_ONLYDISK_A : IDS_FSCOPYMOVE_ONLYDISK_FS));
                            if (pathType == PATH_TYPE_ARCHIVE && (backslashAtEnd || mustBePath))
                                SalPathAddBackslashW(targetPath);
                            pathError = TRUE; // path error -> mode==4
                        }
                    }
                    else
                    {
                        if (error == SPP_INCOMLETEPATH) // additionally report an error for a relative path on FS
                        {
                            gPrompter->ShowError(errTitle, LoadStrW(IDS_FSCOPYMOVE_INCOMPLETEPATH));
                        }
                        pathError = TRUE; // path error -> mode==4
                    }

                    operationMask = FALSE;
                    cancelOrHandlePath = FALSE;
                    ret = GetPluginFS()->CopyOrMoveFromFS(copy, pathError ? 4 : 3,
                                                          GetPluginFS()->GetPluginFSName(), HWindow, panel,
                                                          count - selectedDirs, selectedDirs, targetPath,
                                                          pathError ? NULL : &targetMask,
                                                          operationMask, cancelOrHandlePath, NULL);
                }
            }

            if (ret && !cancelOrHandlePath)
            {
                if (!targetPath.empty()) // switch focus to 'targetPath'
                {
                    NextFocusNameW = targetPath.c_str();
                    // RefreshDirectory may not run - the source might not have changed - just to be safe, post a message
                    PostMessage(HWindow, WM_USER_DONEXTFOCUS, 0, 0);
                }

                unselect = TRUE; // operation successful, deselect the source
            }

            // raise thread's priority again, operation finished
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        }
        break;
    }

    case fsatDelete:
    {
        if (GetPluginFS()->IsServiceSupported(FS_SERVICE_DELETE)) // "always true"
        {
            // lower thread's priority to "normal" (so that operations don't burden the machine too much)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

            BOOL cancelOrError = FALSE;
            BOOL ret = GetPluginFS()->Delete(GetPluginFS()->GetPluginFSName(), 1, HWindow,
                                             panel, count - selectedDirs,
                                             selectedDirs, cancelOrError);
            if (!cancelOrError) // not a cancel/operation error
            {
                if (!ret)
                {
                    int res;
                    if (Configuration.CnfrmFileDirDel)
                    {                                                                                                           // ask only if the user wants it
                        HICON hIcon = (HICON)HANDLES(LoadImage(Shell32DLL, MAKEINTRESOURCE(WindowsVistaAndLater ? 16777 : 161), // delete icon
                                                               IMAGE_ICON, 32, 32, IconLRFlags));
                        int myRes = CMessageBox(HWindow, MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_SILENT,
                                                LoadStrW(IDS_CONFIRM_DELETE_TITLE), &str, NULL,
                                                NULL, hIcon, 0, NULL, NULL, NULL, NULL)
                                        .Execute();
                        HANDLES(DestroyIcon(hIcon));
                        res = (myRes == IDYES ? IDOK : IDCANCEL);
                        UpdateWindow(MainWindow->HWindow);
                    }
                    else
                        res = IDOK;

                    if (res == IDOK)
                    {
                        ret = GetPluginFS()->Delete(GetPluginFS()->GetPluginFSName(), 2, HWindow,
                                                    panel, count - selectedDirs,
                                                    selectedDirs, cancelOrError);
                    }
                }

                if (ret && !cancelOrError)
                    unselect = TRUE; // operation completed successfully
            }

            // raise thread's priority again, operation finished
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        }
        break;
    }

    case fsatCountSize:
    {
        break;
    }

    case fsatChangeAttrs:
    {
        break;
    }
    }

    if (unselect) // should items be deselected?
    {
        SetSel(FALSE, -1, TRUE);                        // explicit redraw
        PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
        UpdateWindow(MainWindow->HWindow);
    }

    EndStopRefresh();
    EndSuspendMode(); // the snooper resumes now
}

void CFilesWindow::RefreshVisibleItemsArray()
{
    CALL_STACK_MESSAGE1("CFilesWindow::RefreshVisibleItemsArray()");

    if (!VisibleItemsArray.IsArrValid(NULL))
        VisibleItemsArray.RefreshArr(this);
    if (!VisibleItemsArraySurround.IsArrValid(NULL))
        VisibleItemsArraySurround.RefreshArr(this);
}

void CFilesWindow::DragDropToArcOrFS(CTmpDragDropOperData* data)
{
    CALL_STACK_MESSAGE1("CFilesWindow::DragDropToArcOrFS()");
    if (data->Data->Names.Count == 0)
        return; // nothing to do
    if (data->Data->SrcPath.empty())
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_SRCPATHUNICODEONLY));
        return;
    }
    if (data->Data->Names.Count > 1) // sort file and directory names for faster array searching
        SortNames(data->Data->Names.GetData(), 0, data->Data->Names.Count - 1);

    int* nameFound = NULL; // each name in data->Data->Names has TRUE/FALSE here (found/not found on disk)
    nameFound = (int*)malloc(sizeof(int) * data->Data->Names.Count);
    if (nameFound == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return;
    }
    memset(nameFound, 0, sizeof(int) * data->Data->Names.Count);

    CSalamanderDirectory* baseDir = new CSalamanderDirectory(TRUE); // container for files and directories from the source disk
    if (baseDir == NULL)
    {
        TRACE_E(LOW_MEMORY);
        if (nameFound != NULL)
            free(nameFound);
        return;
    }

    // load complete data about files and directories, their names are in data->Data
    std::wstring path = data->Data->SrcPath;
    std::wstring searchPath = path;
    if (!searchPath.empty() && searchPath.back() != L'\\')
        searchPath.push_back(L'\\');
    searchPath.push_back(L'*');
    std::wstring text;
    WIN32_FIND_DATAW file;
    HANDLE find = SalFindFirstFileHW(searchPath.c_str(), &file);
    if (find == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), (path + L": " + GetErrorTextOwned(err).c_str()).c_str());
            if (nameFound != NULL)
                free(nameFound);
            delete baseDir;
            return;
        }
    }
    else
    {
        BOOL ok = TRUE;
        CFileData newF;       // we no longer work with these items
        newF.PluginData = -1; // -1 is arbitrary, thevalue will be ignored
        newF.Association = 0;
        newF.Selected = 0;
        newF.Shared = 0;
        newF.Archive = 0;
        newF.SizeValid = 0;
        newF.Dirty = 0; // unnecessary, just for formality
        newF.CutToClip = 0;
        newF.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
        newF.IconOverlayDone = 0;
        BOOL testFindNextErr = TRUE;

        do
        {
            if (file.cFileName[0] == 0 || file.cFileName[0] == L'.' && (file.cFileName[1] == 0 ||
                                                                        (file.cFileName[1] == L'.' && file.cFileName[2] == 0)))
                continue; // "." and ".."

            int foundIndex;
            if (ContainsString(&data->Data->Names, file.cFileName, &foundIndex))
            {
                if (nameFound[foundIndex] == FALSE)
                    nameFound[foundIndex] = TRUE;
                else // duplicate = all names are processed (some may not have been selected); if it causes issues, handle it with case-sensitive comparison first
                    TRACE_E("CFilesWindow::DragDropToArcOrFS(): duplicate names found! (names are compared case-insensitive)");
            }
            else
                continue; // user is not interested in this file/directory (name wasn't in the data object)

            newF.Name = DupStr(file.cFileName);
            newF.DosName = NULL;
            if (newF.Name == NULL)
            {
                ok = FALSE;
                testFindNextErr = FALSE;
                break;
            }
            newF.NameLen = static_cast<DWORD>(wcslen(newF.Name)); // WIN32_FIND_DATA name is DWORD-bounded
            if (!Configuration.SortDirsByExt && (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) // directory, certainly a disk
            {
                newF.Ext = newF.Name + newF.NameLen; // directories have no extensions
            }
            else
            {
                newF.Ext = wcsrchr(newF.Name, L'.');
                if (newF.Ext == NULL)
                    newF.Ext = newF.Name + newF.NameLen; // ".cvspass" in Windows is an extension ...
                                                         //      if (newF.Ext == NULL || newF.Ext == newF.Name) newF.Ext = newF.Name + newF.NameLen;
                else
                    newF.Ext++;
            }

            if (file.cAlternateFileName[0] != 0)
            {
                newF.DosName = DupStr(file.cAlternateFileName);
                if (newF.DosName == NULL)
                {
                    free(newF.Name);
                    ok = FALSE;
                    testFindNextErr = FALSE;
                    break;
                }
            }

            newF.Size = CQuadWord(file.nFileSizeLow, file.nFileSizeHigh);
            newF.Attr = file.dwFileAttributes;
            newF.LastWrite = file.ftLastWriteTime;
            newF.Hidden = newF.Attr & FILE_ATTRIBUTE_HIDDEN ? 1 : 0;
            newF.IsOffline = newF.Attr & FILE_ATTRIBUTE_OFFLINE ? 1 : 0;

            if (newF.Attr & FILE_ATTRIBUTE_DIRECTORY) // directory, certainly a disk
            {
                newF.IsLink = (newF.Attr & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0; // volume mount point or junction point = display directory with link overlay
            }
            else
            {
                if (newF.Attr & FILE_ATTRIBUTE_REPARSE_POINT)
                    newF.IsLink = 1; // if the file is a reparse point (might not be even possible) = display it with a link overlay
                else
                    newF.IsLink = IsFileLink(newF.Ext);
            }

            if ((newF.Attr & FILE_ATTRIBUTE_DIRECTORY) && !baseDir->AddDir(L"", newF, NULL) ||     // directory, certainly a disk
                (newF.Attr & FILE_ATTRIBUTE_DIRECTORY) == 0 && !baseDir->AddFile(L"", newF, NULL)) // file
            {
                free(newF.Name);
                if (newF.DosName != NULL)
                    free(newF.DosName);
                ok = FALSE;
                testFindNextErr = FALSE;
                break;
            }
        } while (SalLPFindNextFile(find, &file));
        DWORD err = GetLastError();
        SalLPFindClose(find);

        if (testFindNextErr && err != ERROR_NO_MORE_FILES)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), (path + L": " + GetErrorTextOwned(err).c_str()).c_str());
            if (nameFound != NULL)
                free(nameFound);
            delete baseDir;
            return;
        }

        if (!ok)
        {
            if (nameFound != NULL)
                free(nameFound);
            delete baseDir;
            return;
        }
    }

    // check if we found all files and directories (i.e., found all names)
    BOOL cancel = FALSE;
    int i;
    for (i = 0; i < data->Data->Names.Count; i++)
    {
        if (nameFound[i] == FALSE)
        {
            if (gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), LoadStrW(IDS_SRCFILESNOTFOUND)).type == PromptResult::kNo)
            {
                cancel = TRUE;
            }
            break;
        }
    }

    if (!cancel && !FilesActionInProgress &&
        CheckPath(TRUE, data->Data->SrcPath.c_str()) == ERROR_SUCCESS)
    { // perform the operation itself
        FilesActionInProgress = TRUE;

        BeginSuspendMode(); // the snooper takes a break
        BeginStopRefresh(); // to suppress path change messages

        CPanelTmpEnumData dataEnum;
        dataEnum.Dirs = baseDir->GetDirs(L"");
        dataEnum.Files = baseDir->GetFiles(L"");
        dataEnum.IndexesCount = dataEnum.Dirs->Count + dataEnum.Files->Count;
        for (i = 0; i < dataEnum.IndexesCount; i++)
            nameFound[i] = i;
        dataEnum.Indexes = nameFound;
        dataEnum.WorkPathW = data->Data->SrcPath;
        dataEnum.EnumLastIndex = -1;

        if (dataEnum.IndexesCount > 0)
        {
            if (data->ToArchive)
            {
                //---  check whether it is a zero-length file
                BOOL nullFile;
                BOOL haveSize = FALSE;
                CQuadWord size;
                DWORD err;
                const std::wstring& archiveW = data->ArchiveOrFSName;
                HANDLE hFile = gFileSystem->CreateFile(archiveW.c_str(), GENERIC_READ, 0, NULL,
                                                       OPEN_EXISTING, 0, NULL);
                DWORD openError = GetLastError();
                HANDLES_ADD_EX(__otQuiet, hFile != INVALID_HANDLE_VALUE, __htFile,
                               __hoCreateFile, hFile, openError, TRUE);
                if (hFile != INVALID_HANDLE_VALUE)
                {
                    uint64_t fileSize = 0;
                    FileResult sizeResult = gFileSystem->GetHandleFileSize(hFile, &fileSize);
                    haveSize = sizeResult.success;
                    if (haveSize)
                        size = CQuadWord((DWORD)fileSize, (DWORD)(fileSize >> 32));
                    else
                        err = sizeResult.errorCode;
                    HANDLES_REMOVE(hFile, __htFile, "IFileSystem::CloseHandle");
                    gFileSystem->CloseFileHandle(hFile);
                }
                else
                    err = openError;
                if (haveSize)
                {
                    nullFile = (size == CQuadWord(0, 0));

                    //---  if it is a zero-length file we must delete it, archivers can't handle them
                    DWORD nullFileAttrs;
                    if (nullFile)
                    {
                        nullFileAttrs = gFileSystem->GetFileAttributes(archiveW.c_str());
                        ClearReadOnlyAttr(archiveW.c_str(), nullFileAttrs); // to allow deletion even if read-only
                        gFileSystem->DeleteFile(archiveW.c_str());
                    }
                    //---  actual packing
                    gEnvironment->SetCurrentDirectory(data->Data->SrcPath.c_str());
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                    if (PackCompress(HWindow, this, data->ArchiveOrFSName.c_str(), data->ArchivePathOrUserPart.c_str(),
                                     !data->Copy, data->Data->SrcPath.c_str(), PanelEnumDiskSelection, &dataEnum))
                    {                   // packing succeeded
                        if (nullFile && // zero-length file might have had a different compressed attribute, set archive accordingly
                            nullFileAttrs != INVALID_FILE_ATTRIBUTES)
                        {
                            HANDLE hFile2 = gFileSystem->CreateFile(archiveW.c_str(), GENERIC_READ | GENERIC_WRITE,
                                                                   0, NULL, OPEN_EXISTING, 0, NULL);
                            HANDLES_ADD_EX(__otQuiet, hFile2 != INVALID_HANDLE_VALUE, __htFile,
                                           __hoCreateFile, hFile2, GetLastError(), TRUE);
                            if (hFile2 != INVALID_HANDLE_VALUE)
                            {
                                // restore the "compressed" flag; on FAT and FAT32 it simply won't succeed
                                gFileSystem->SetHandleCompression(
                                    hFile2, (nullFileAttrs & FILE_ATTRIBUTE_COMPRESSED) != 0);
                                HANDLES_REMOVE(hFile2, __htFile, "IFileSystem::CloseHandle");
                                gFileSystem->CloseFileHandle(hFile2);
                                gFileSystem->SetFileAttributes(archiveW.c_str(), nullFileAttrs);
                            }
                        }
                    }
                    else
                    {
                        if (nullFile) // operation failed, we must recreate it
                        {
                            HANDLE hFile2 = gFileSystem->CreateFile(archiveW.c_str(), GENERIC_READ | GENERIC_WRITE,
                                                                   0, NULL, OPEN_ALWAYS, 0, NULL);
                            HANDLES_ADD_EX(__otQuiet, hFile2 != INVALID_HANDLE_VALUE, __htFile,
                                           __hoCreateFile, hFile2, GetLastError(), TRUE);
                            if (hFile2 != INVALID_HANDLE_VALUE)
                            {
                                if (nullFileAttrs != INVALID_FILE_ATTRIBUTES)
                                {
                                    // restore the "compressed" flag; on FAT and FAT32 it simply won't succeed
                                    gFileSystem->SetHandleCompression(
                                        hFile2, (nullFileAttrs & FILE_ATTRIBUTE_COMPRESSED) != 0);
                                }
                                HANDLES_REMOVE(hFile2, __htFile, "IFileSystem::CloseHandle");
                                gFileSystem->CloseFileHandle(hFile2);
                                if (nullFileAttrs != INVALID_FILE_ATTRIBUTES)
                                    gFileSystem->SetFileAttributes(archiveW.c_str(), nullFileAttrs);
                            }
                        }
                    }
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                    SetCurrentDirectoryToSystem();

                    UpdateWindow(MainWindow->HWindow);

                    //---  refresh non-automatically refreshed directories
                    // change in the directory with the target archive (archive file is modified)
                    text = data->ArchiveOrFSName;
                    CutDirectoryW(text); // 'text' is the archive name -> must always succeed
                    MainWindow->PostChangeOnPathNotificationW(text.c_str(), FALSE);
                    if (!data->Copy)
                    {
                        // changes on the source path (when moving files to the archive,
                        // files/directories should have been deleted)
                        MainWindow->PostChangeOnPathNotificationW(data->Data->SrcPath.c_str(), TRUE);
                    }
                }
                else
                {
                    std::wstring msg = FormatStrW(LoadStrW(IDS_FILEERRORFORMAT), data->ArchiveOrFSName.c_str(), GetErrorTextOwned(err).c_str());
                    gPrompter->ShowError(LoadStrW(data->Copy ? IDS_ERRORCOPY : IDS_ERRORMOVE), msg.c_str());
                }
            }
            else // to FS
            {
                int selFiles = dataEnum.Files->Count;
                int selDirs = dataEnum.Dirs->Count;

                // lower thread's priority to "normal" (so that operations don't burden the machine too much)
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

                // select the FS that performs the operation (priority: active, then new)
                std::wstring targetPath = data->ArchiveOrFSName + L":" + data->ArchivePathOrUserPart;
                BOOL done = FALSE;
                CPluginFSInterfaceEncapsulation* fs = NULL;
                if (Is(ptPluginFS))
                    fs = GetPluginFS();

                int fsNameIndex;
                if (fs != NULL && fs->NotEmpty() &&                                         // interface is valid
                    fs->IsFSNameFromSamePluginAsThisFS(data->ArchiveOrFSName.c_str(), fsNameIndex)) // FS name is from the same plugin (otherwise it's not worth trying)
                {
                    BOOL invalidPathOrCancel;
                    if (fs->CopyOrMoveFromDiskToFS(data->Copy, 3, fs->GetPluginFSName(),
                                                   HWindow, data->Data->SrcPath.c_str(),
                                                   PanelEnumDiskSelection, &dataEnum,
                                                   selFiles, selDirs, targetPath, &invalidPathOrCancel))
                    {
                        done = TRUE; // finished/canceled/error (either way,no point in trying a new FS)
                    }
                    else
                    {
                        // reset before next use (so enumeration starts again from the beginning)
                        dataEnum.Reset();

                        if (invalidPathOrCancel)
                            done = TRUE; // invalid path + user cannot fix it, ending
                                         // else ; // we should try a new FS
                    }
                }
                if (!done) // active FS failed, create a new FS
                {
                    int index;
                    int fsNameIndex2;
                    if (Plugins.IsPluginFS(data->ArchiveOrFSName.c_str(), index, fsNameIndex2)) // determine plugin index
                    {
                        // obtain the plug-in associated with the FS
                        CPluginData* plugin = Plugins.Get(index);
                        if (plugin != NULL)
                        {
                            // open a new FS
                            // load the plug-in before obtaining DLLName, Version and plugin interfaces
                            CPluginFSInterfaceAbstract* auxFS = plugin->OpenFS(data->ArchiveOrFSName.c_str(), fsNameIndex2);
                            CPluginFSInterfaceEncapsulation pluginFS(auxFS, plugin->DLLName.c_str(), plugin->Version.c_str(),
                                                                     plugin->GetPluginInterfaceForFS()->GetInterface(),
                                                                     plugin->GetPluginInterface()->GetInterface(),
                                                                     data->ArchiveOrFSName.c_str(), fsNameIndex2, -1, 0,
                                                                     plugin->BuiltForVersion);
                            if (pluginFS.NotEmpty())
                            {
                                Plugins.SetWorkingPluginFS(&pluginFS);
                                BOOL invalidPathOrCancel;
                                if (!pluginFS.CopyOrMoveFromDiskToFS(data->Copy, 3, pluginFS.GetPluginFSName(),
                                                                     HWindow, data->Data->SrcPath.c_str(),
                                                                     PanelEnumDiskSelection, &dataEnum,
                                                                     selFiles, selDirs, targetPath,
                                                                     &invalidPathOrCancel))
                                { // syntax error/plugin error
                                    if (!invalidPathOrCancel)
                                    { // plugin error (new FS, but returns error 'this FS cannot perform the requested operation')
                                        TRACE_E("CopyOrMoveFromDiskToFS on new (empty) FS may not return error 'unable to process operation'.");
                                    }
                                }

                                pluginFS.ReleaseObject(HWindow);
                                plugin->GetPluginInterfaceForFS()->CloseFS(pluginFS.GetInterface());
                                Plugins.SetWorkingPluginFS(NULL);
                            }
                            else
                                TRACE_E("Plugin has refused to open FS (maybe it even does not start).");
                        }
                        else
                            TRACE_E("Unexpected situation in CFilesWindow::DragDropToArcOrFS() - unable to work with plugin.");
                    }
                    else
                    {
                        TRACE_EW(L"Unexpected situation in CFilesWindow::DragDropToArcOrFS() - file-system " << data->ArchiveOrFSName << L" was not found.");
                    }
                }

                // raise thread's priority again, operation finished
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                SetCurrentDirectoryToSystem(); // restore current directory in any case
            }
        }

        EndStopRefresh();
        EndSuspendMode();

        FilesActionInProgress = FALSE;
    }
    else
    {
        if (FilesActionInProgress)
            TRACE_E("Unexpected situation in CFilesWindow::DragDropToArcOrFS(): FilesActionInProgress is TRUE!");
    }

    // release data
    if (nameFound != NULL)
        free(nameFound);
    delete baseDir;
}

//****************************************************************************
//
// CVisibleItemsArray
//

CVisibleItemsArray::CVisibleItemsArray(BOOL surroundArr)
{
    HANDLES(InitializeCriticalSection(&Monitor));
    ArrVersionNum = 0;
    ArrIsValid = FALSE;
    ArrNames = NULL;
    ArrNamesCount = 0;
    ArrNamesAllocated = 0;
    SurroundArr = surroundArr;
    FirstVisibleItem = -1;
    LastVisibleItem = -1;
}

CVisibleItemsArray::~CVisibleItemsArray()
{
    HANDLES(DeleteCriticalSection(&Monitor));
    if (ArrNamesAllocated > 0 && ArrNames != NULL)
        free(ArrNames);
    ArrNamesCount = 0;
    ArrNamesAllocated = 0;
}

BOOL CVisibleItemsArray::IsArrValid(int* versionNum)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CVisibleItemsArray::IsArrValid()");
    HANDLES(EnterCriticalSection(&Monitor));
    if (versionNum != NULL)
        *versionNum = ArrVersionNum;
    BOOL ret = ArrIsValid;
    HANDLES(LeaveCriticalSection(&Monitor));
    return ret;
}

void CVisibleItemsArray::InvalidateArr()
{
    CALL_STACK_MESSAGE1("CVisibleItemsArray::InvalidateArr()");
    HANDLES(EnterCriticalSection(&Monitor));
    ArrIsValid = FALSE;
    ArrNamesCount = 0;
    FirstVisibleItem = -1;
    LastVisibleItem = -1;
    HANDLES(LeaveCriticalSection(&Monitor));
}

void SortNamesCS(wchar_t** names, int left, int right)
{
    int i = left, j = right;
    wchar_t* pivot = names[(i + j) / 2];

    do
    {
        while (wcscmp(names[i], pivot) < 0 && i < right)
            i++;
        while (wcscmp(pivot, names[j]) < 0 && j > left)
            j--;

        if (i <= j)
        {
            wchar_t* swap = names[i];
            names[i] = names[j];
            names[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    if (left < j)
        SortNamesCS(names, left, j);
    if (i < right)
        SortNamesCS(names, i, right);
}

void CVisibleItemsArray::RefreshArr(CFilesWindow* panel)
{
    CALL_STACK_MESSAGE1("CVisibleItemsArray::RefreshArr()");
    HANDLES(EnterCriticalSection(&Monitor));
    int firstIndex, count;
    panel->ListBox->GetVisibleItems(&firstIndex, &count);

    if (SurroundArr)
    {
        int origFirstIndex = firstIndex;
        if (firstIndex > 0)
            firstIndex -= count;
        if (firstIndex < 0)
            firstIndex = 0;
        count = count * 2 + (origFirstIndex - firstIndex);
    }

    int dirsCount = panel->Dirs->Count;
    int filesCount = panel->Files->Count;
    if (firstIndex + count > filesCount + dirsCount)
        count = filesCount + dirsCount - firstIndex;
    if (count < 0)
        count = 0;
    if (count > ArrNamesAllocated)
    {
        wchar_t** n = (wchar_t**)realloc(ArrNames, count * sizeof(wchar_t*));
        if (n != NULL)
        {
            ArrNames = n;
            ArrNamesAllocated = count;
        }
        else
        {
            TRACE_E(LOW_MEMORY);
            if (ArrNames != NULL)
                free(ArrNames);
            ArrNamesAllocated = 0;
            ArrNamesCount = 0;
        }
    }
    if (count <= ArrNamesAllocated)
    {
        // TRACE_I("VisibleItemsArray: firstIndex=" << firstIndex << ", count=" << count);
        int end = firstIndex + count;
        int x = 0;
        int i;
        for (i = firstIndex; i < end; i++)
        {
            CFileData* f = &(i < dirsCount ? panel->Dirs->At(i) : panel->Files->At(i - dirsCount));
            ArrNames[x++] = f->Name;
            //#ifdef _DEBUG
            //      if (i == firstIndex) TRACE_I("VisibleItemsArray: first=" << f->Name);
            //      if (i + 1 == end) TRACE_I("VisibleItemsArray: last=" << f->Name);
            //#endif // _DEBUG
        }
        ArrNamesCount = count;
        if (ArrNamesCount > 1)
            SortNamesCS(ArrNames, 0, ArrNamesCount - 1);
        ArrIsValid = TRUE;
        ArrVersionNum++;
        FirstVisibleItem = firstIndex;
        LastVisibleItem = firstIndex + count - 1;
    }
    HANDLES(LeaveCriticalSection(&Monitor));
}

BOOL CVisibleItemsArray::ArrContains(const wchar_t* name, BOOL* isArrValid, int* versionNum)
{
    DEBUG_SLOW_CALL_STACK_MESSAGE1("CVisibleItemsArray::ArrContains()");
    HANDLES(EnterCriticalSection(&Monitor));
    if (versionNum != NULL)
        *versionNum = ArrVersionNum;
    if (isArrValid != NULL)
        *isArrValid = ArrIsValid;
    if (!ArrIsValid || ArrNamesCount <= 0)
    {
        HANDLES(LeaveCriticalSection(&Monitor));
        return FALSE;
    }

    int l = 0, r = ArrNamesCount - 1, m;
    while (1)
    {
        m = (l + r) / 2;
        int res = wcscmp(name, ArrNames[m]);
        if (res == 0)
        {
            HANDLES(LeaveCriticalSection(&Monitor));
            return TRUE; // found
        }
        else
        {
            if (res < 0)
            {
                if (l == r || l > m - 1)
                {
                    HANDLES(LeaveCriticalSection(&Monitor));
                    return FALSE; // not found
                }
                r = m - 1;
            }
            else
            {
                if (l == r)
                {
                    HANDLES(LeaveCriticalSection(&Monitor));
                    return FALSE; // not found
                }
                l = m + 1;
            }
        }
    }
}

BOOL CVisibleItemsArray::ArrContainsIndex(int index, BOOL* isArrValid, int* versionNum)
{
    DEBUG_SLOW_CALL_STACK_MESSAGE1("CVisibleItemsArray::ArrContainsIndex()");
    HANDLES(EnterCriticalSection(&Monitor));
    if (versionNum != NULL)
        *versionNum = ArrVersionNum;
    if (isArrValid != NULL)
        *isArrValid = ArrIsValid;
    if (!ArrIsValid || ArrNamesCount <= 0)
    {
        HANDLES(LeaveCriticalSection(&Monitor));
        return FALSE;
    }

    BOOL ret = index >= FirstVisibleItem && index <= LastVisibleItem;
    HANDLES(LeaveCriticalSection(&Monitor));
    return ret;
}

//****************************************************************************
//
// CCriteriaData
//

CCriteriaData::CCriteriaData()
{
    Reset();
}

void CCriteriaData::Reset()
{
    OverwriteOlder = FALSE;
    StartOnIdle = FALSE;
    CopySecurity = FALSE;
    CopyAttrs = FALSE;
    PreserveDirTime = FALSE;
    IgnoreADS = FALSE;
    SkipEmptyDirs = FALSE;
    UseMasks = FALSE;
    Masks.SetMasksString(L"*.*");
    UseAdvanced = FALSE;
    Advanced.Reset();
    UseSpeedLimit = FALSE;
    SpeedLimit = 1;
}

CCriteriaData&
CCriteriaData::operator=(const CCriteriaData& s)
{
    OverwriteOlder = s.OverwriteOlder;
    StartOnIdle = s.StartOnIdle;
    CopySecurity = s.CopySecurity;
    CopyAttrs = s.CopyAttrs;
    PreserveDirTime = s.PreserveDirTime;
    IgnoreADS = s.IgnoreADS;
    SkipEmptyDirs = s.SkipEmptyDirs;
    UseMasks = s.UseMasks;
    Masks = s.Masks;
    UseAdvanced = s.UseAdvanced;
    memmove(&Advanced, &s.Advanced, sizeof(Advanced));
    UseSpeedLimit = s.UseSpeedLimit;
    SpeedLimit = s.SpeedLimit;

    return *this;
}

BOOL CCriteriaData::IsDirty()
{
    return OverwriteOlder || StartOnIdle || CopySecurity || CopyAttrs ||
           PreserveDirTime || IgnoreADS || SkipEmptyDirs || UseMasks ||
           UseAdvanced || UseSpeedLimit;
}

BOOL CCriteriaData::AgreeMasksAndAdvanced(const CFileData* file)
{
    if (UseMasks && !Masks.AgreeMasks(file->Name, file->Ext))
        return FALSE;

    if (UseAdvanced)
    {
        if (!Advanced.Test(file->Attr, &file->Size, &file->LastWrite))
            return FALSE;
    }

    return TRUE;
}

BOOL CCriteriaData::AgreeMasksAndAdvanced(const WIN32_FIND_DATAW* file)
{
    // file->cFileName is already the exact wide name - narrowing it via
    // WideCharToMultiByte(WC_NO_BEST_FIT_CHARS) just to feed the narrow AgreeMasks (itself
    // a converting wrapper that re-widens via AnsiToWide, per masks.cpp's own
    // comment) was a pointless double round-trip. For a name outside CP_ACP this could
    // silently mis-decide include/exclude for a mask-filtered copy/move.
    if (UseMasks && !Masks.AgreeMasks(file->cFileName, NULL))
        return FALSE;

    if (UseAdvanced)
    {
        CQuadWord size(file->nFileSizeLow, file->nFileSizeHigh);
        if (!Advanced.Test(file->dwFileAttributes, &size, &file->ftLastWriteTime))
            return FALSE;
    }

    return TRUE;
}

const wchar_t* CRITERIADATA_OVERWRITEOLDER_REG = L"Overwrite Older";
const wchar_t* CRITERIADATA_STARTONIDLE_REG = L"Start On Idle";
const wchar_t* CRITERIADATA_COPYSECURITY_REG = L"Copy Security";
const wchar_t* CRITERIADATA_COPYATTRIBUTES_REG = L"Copy Attributes";
const wchar_t* CRITERIADATA_PRESERVEDIRTIME_REG = L"Preserve Dir Time";
const wchar_t* CRITERIADATA_IGNOREADS_REG = L"Ignore ADS";
const wchar_t* CRITERIADATA_SKIPEMPTYDIRS_REG = L"Skip Empty Dirs";
const wchar_t* CRITERIADATA_USENAMEMASK_REG = L"Use Name Masks";
const wchar_t* CRITERIADATA_NAMEMASKS_REG = L"Name Masks";
const wchar_t* CRITERIADATA_USESPEEDLIMIT_REG = L"Use Speed Limit";
const wchar_t* CRITERIADATA_SPEEDLIMIT_REG = L"Speed Limit";
const wchar_t* CRITERIADATA_USEADVANCED_REG = L"Use Advanced";

BOOL CCriteriaData::Save(HKEY hKey)
{
    // to optimize for registry size, only non-default values are saved.
    // this requires clearing the destination key before writing.
    CCriteriaData def;

    if (OverwriteOlder != def.OverwriteOlder)
        SetValueW(hKey, CRITERIADATA_OVERWRITEOLDER_REG, REG_DWORD, &OverwriteOlder, sizeof(DWORD));
    if (StartOnIdle != def.StartOnIdle)
        SetValueW(hKey, CRITERIADATA_STARTONIDLE_REG, REG_DWORD, &StartOnIdle, sizeof(DWORD));
    if (CopySecurity != def.CopySecurity)
        SetValueW(hKey, CRITERIADATA_COPYSECURITY_REG, REG_DWORD, &CopySecurity, sizeof(DWORD));
    if (CopyAttrs != def.CopyAttrs)
        SetValueW(hKey, CRITERIADATA_COPYATTRIBUTES_REG, REG_DWORD, &CopyAttrs, sizeof(DWORD));
    if (PreserveDirTime != def.PreserveDirTime)
        SetValueW(hKey, CRITERIADATA_PRESERVEDIRTIME_REG, REG_DWORD, &PreserveDirTime, sizeof(DWORD));
    if (IgnoreADS != def.IgnoreADS)
        SetValueW(hKey, CRITERIADATA_IGNOREADS_REG, REG_DWORD, &IgnoreADS, sizeof(DWORD));
    if (SkipEmptyDirs != def.SkipEmptyDirs)
        SetValueW(hKey, CRITERIADATA_SKIPEMPTYDIRS_REG, REG_DWORD, &SkipEmptyDirs, sizeof(DWORD));
    if (UseMasks != def.UseMasks)
        SetValueW(hKey, CRITERIADATA_USENAMEMASK_REG, REG_DWORD, &UseMasks, sizeof(DWORD));
    if (wcscmp(Masks.GetMasksString(), def.Masks.GetMasksString()) != 0)
        SetValueW(hKey, CRITERIADATA_NAMEMASKS_REG, REG_SZ, Masks.GetMasksString(), -1);
    if (UseSpeedLimit != def.UseSpeedLimit)
        SetValueW(hKey, CRITERIADATA_USESPEEDLIMIT_REG, REG_DWORD, &UseSpeedLimit, sizeof(DWORD));
    if (SpeedLimit != def.SpeedLimit)
        SetValueW(hKey, CRITERIADATA_SPEEDLIMIT_REG, REG_DWORD, &SpeedLimit, sizeof(DWORD));
    if (UseAdvanced != def.UseAdvanced)
        SetValueW(hKey, CRITERIADATA_USEADVANCED_REG, REG_DWORD, &UseAdvanced, sizeof(DWORD));

    Advanced.Save(hKey);

    return TRUE;
}

BOOL CCriteriaData::Load(HKEY hKey)
{
    GetValueW(hKey, CRITERIADATA_OVERWRITEOLDER_REG, REG_DWORD, &OverwriteOlder, sizeof(DWORD));
    GetValueW(hKey, CRITERIADATA_STARTONIDLE_REG, REG_DWORD, &StartOnIdle, sizeof(DWORD));
    GetValueW(hKey, CRITERIADATA_COPYSECURITY_REG, REG_DWORD, &CopySecurity, sizeof(DWORD));
    GetValueW(hKey, CRITERIADATA_COPYATTRIBUTES_REG, REG_DWORD, &CopyAttrs, sizeof(DWORD));
    GetValueW(hKey, CRITERIADATA_PRESERVEDIRTIME_REG, REG_DWORD, &PreserveDirTime, sizeof(DWORD));
    GetValueW(hKey, CRITERIADATA_IGNOREADS_REG, REG_DWORD, &IgnoreADS, sizeof(DWORD));
    GetValueW(hKey, CRITERIADATA_SKIPEMPTYDIRS_REG, REG_DWORD, &SkipEmptyDirs, sizeof(DWORD));
    GetValueW(hKey, CRITERIADATA_USENAMEMASK_REG, REG_DWORD, &UseMasks, sizeof(DWORD));
    // same boundary-conversion fix as main_window_config_persistence.cpp's
    // mask configs (iteration 105) - reading narrow REG_SZ bytes directly into the wide
    // buffer packed two ANSI bytes per wchar_t, corrupting the mask on every load. This
    // one also undid iteration 104's CCopyMoveMoreDialog fix on every restart, since
    // Criteria->Masks round-trips through this Load()/Save() pair.
    std::wstring nameMasks;
    GetStringValueW(hKey, CRITERIADATA_NAMEMASKS_REG, nameMasks);
    Masks.SetMasksString(nameMasks.c_str());
    GetValueW(hKey, CRITERIADATA_USESPEEDLIMIT_REG, REG_DWORD, &UseSpeedLimit, sizeof(DWORD));
    GetValueW(hKey, CRITERIADATA_SPEEDLIMIT_REG, REG_DWORD, &SpeedLimit, sizeof(DWORD));
    GetValueW(hKey, CRITERIADATA_USEADVANCED_REG, REG_DWORD, &UseAdvanced, sizeof(DWORD));

    Advanced.Load(hKey);

    return TRUE;
}

//****************************************************************************
//
// CCopyMoveOptions
//

void CCopyMoveOptions::Set(const CCriteriaData* item)
{
    // for now, only one item (default) or none is held
    if (Items.Count > 0)
        Items.DestroyMembers();
    if (item != NULL)
    {
        CCriteriaData* itemCopy = new CCriteriaData();
        *itemCopy = *item;
        Items.Add(itemCopy);
    }
}

const CCriteriaData*
CCopyMoveOptions::Get()
{
    if (Items.Count == 1)
        return Items[0];
    else
        return NULL;
}

BOOL CCopyMoveOptions::Save(HKEY hKey)
{
    ClearKey(hKey);
    HKEY subKey;
    wchar_t buf[30];
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        _itow(i + 1, buf, 10);
        if (CreateKeyW(hKey, buf, subKey))
        {
            Items[i]->Save(subKey);
            CloseKey(subKey);
        }
        else
            break;
    }
    return TRUE;
}

BOOL CCopyMoveOptions::Load(HKEY hKey)
{
    HKEY subKey;
    wchar_t buf[30];
    int i = 1;
    wcscpy(buf, L"1");
    Items.DestroyMembers();
    while (OpenKeyW(hKey, buf, subKey) && i == 1) // for now read only the first item
    {
        CCriteriaData* item = new CCriteriaData();
        item->Load(subKey);
        Items.Add(item);
        _itow(++i, buf, 10);
        CloseKey(subKey);
    }
    return TRUE;
}

CCopyMoveOptions CopyMoveOptions;
