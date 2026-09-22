// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "dialogs.h"
#include "snooper.h"
#include "worker.h"
#include "pack.h"
#include "pack_target_policy.h"
#include "mapi.h"
#include "ui/IPrompter.h"
#include "common/fsutil.h"
#include "common/IFileSystem.h"
#include "common/unicode/helpers.h"
#include "common/unicode/AnsiToolPathPolicy.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/widepath.h"
#include "common/IEnvironment.h"
#include "common/IShell.h"

//
// ****************************************************************************
// CFilesWindow
//

// PathContainsValidComponents lives in common/PathDisplayUtils.cpp for private-test reuse.

BOOL CFilesWindow::DeleteThroughRecycleBin(int* selection, int selCount, CFileData* oneFile)
{
    CALL_STACK_MESSAGE2("CFilesWindow::DeleteThroughRecycleBin(, %d,)", selCount);

    int i = 0;
    // Use wide path for full Unicode support (no MAX_PATH limit)
    // Take it from the wide source, not by re-widening the CP_ACP mirror: everything
    // below this line is scrupulous about wide *names* while the directory they hang off
    // was "D:\???\", so F8 to the Recycle Bin deleted nothing and let the shell report
    // its own "Could not find this item" (audit A11).
    std::wstring pathW = GetPathW();
    if (!pathW.empty() && pathW.back() != L'\\')
        pathW += L'\\';

    // Verify that the path does not contain components ending with a space or dot;
    // as this can confuse the Recycle Bin and cause it to delete from a different path (it quietly trims
    // those spaces or dots)
    if (!PathContainsValidComponents(pathW.c_str()))
    {
        // TODO: Use wide format string once IDS_RECYCLEBINERROR supports wide
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), (pathW + L" - invalid path component").c_str());
        return FALSE; // quick dirty bloody hack - Recycle Bin simply cannot handle names ending with a space or dot (it deletes a different name created by trimming those characters, which we definitely don't want)
    }

    std::vector<std::wstring> paths;
    paths.reserve(selCount > 0 ? selCount : 1);
    do
    {
        if (selCount > 0)
        {
            oneFile = (selection[i] < Dirs->Count) ? &Dirs->At(selection[i]) : &Files->At(selection[i] - Dirs->Count);
            i++;
        }
        if (oneFile->Name[oneFile->NameLen - 1] <= ' ' || oneFile->Name[oneFile->NameLen - 1] == '.')
        {
            std::wstring nameW = oneFile->Name;
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), (nameW + L" - invalid filename (ends with space or dot)").c_str());
            return FALSE; // quick dirty bloody hack - Recycle Bin simply cannot handle names ending with a space or dot (it deletes a different name created by trimming those characters, which we definitely do not want)
        }
        // oneFile points to the selected item or the caret item in the filebox
        paths.push_back(pathW + oneFile->Name);
    } while (i < selCount);

    gEnvironment->SetCurrentDirectory(pathW.c_str()); // for faster operation

    CShellExecuteWnd shellExecuteWnd;
    HWND shellParent = shellExecuteWnd.Create(MainWindow->HWindow,
                                              L"SEW: CFilesWindow::DeleteThroughRecycleBin");
    CALL_STACK_MESSAGE1("CFilesWindow::DeleteThroughRecycleBin::IShell::FileOperation");
    const ShellResult shellResult =
        gShell->FileOperation(ShellFileOp::Delete, paths, std::wstring(), OpAllowUndo, shellParent);
    SetCurrentDirectoryToSystem();

    // SHFileOperationW, which this replaced, always reached the shell and the shell always
    // reported. IFileOperation has a setup stage in front of it - COM, the object, and one
    // IShellItem per name - and a failure there produces no UI at all. Discarding the result
    // as well meant F8 could do nothing whatsoever and say nothing whatsoever. A user cancel
    // is not an error and stays silent.
    if (!shellResult.success && shellResult.errorCode != ERROR_CANCELLED)
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE),
                             GetErrorTextOwned(shellResult.errorCode).c_str());
    }

    return FALSE; /*ret && !fo.fAnyOperationsAborted*/
    ;             // for now, we simply keep the selection, the return value does not work
}

void PluginFSConvertPathToExternal(std::wstring& path)
{
    std::wstring fsName;
    const wchar_t* fsUserPart;
    int index;
    int fsNameIndex;
    if (IsPluginFSPath(path.c_str(), &fsName, &fsUserPart) &&
        Plugins.IsPluginFS(fsName.c_str(), index, fsNameIndex))
    {
        CPluginData* plugin = Plugins.Get(index);
        if (plugin != NULL && plugin->InitDLL(MainWindow->HWindow, FALSE, TRUE, FALSE)) // the plugin may not be loaded, let it load if needed
        {
            const size_t userPartOffset = (size_t)(fsUserPart - path.c_str());
            std::wstring userPart = fsUserPart;
            if (plugin->GetPluginInterfaceForFS()->ConvertPathToExternalW(fsName.c_str(), fsNameIndex, userPart))
            {
                path.resize(userPartOffset);
                path += userPart;
            }
        }
    }
}

// countSizeMode - 0 normal calculation, 1 calculation for the selected item,
// 2 calculation for all subdirectories
void CFilesWindow::FilesAction(CActionType type, CFilesWindow* target, int countSizeMode)
{
    CALL_STACK_MESSAGE3("CFilesWindow::FilesAction(%d, , %d)", type, countSizeMode);
    if (Dirs->Count + Files->Count == 0)
        return;

    // restore DefaultDir
    MainWindow->UpdateDefaultDir(MainWindow->GetActivePanel() == this);

    BOOL invertRecycleBin = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (!FilesActionInProgress)
    {
        if (CheckPath(TRUE) != ERROR_SUCCESS)
            return; // the source is always the current directory

        if (type != atCountSize && countSizeMode != 0)
            countSizeMode = 0; // just to be sure (only countSizeMode is checked later)

        FilesActionInProgress = TRUE;

        BeginSuspendMode(); // the snooper takes a break
        BeginStopRefresh(); // just to prevent path change notifications from being distributed

        //---  find out how many directories and files are selected
        BOOL subDir;
        if (Dirs->Count > 0)
            subDir = (wcscmp(Dirs->At(0).Name, L"..") == 0);
        else
            subDir = FALSE;

        std::unique_ptr<int[]> indexes; // RAII: auto-deleted when scope exits
        int files = 0;
        int dirs = 0;
        int count = GetSelCount();
        if (countSizeMode == 0 && count > 0)
        {
            indexes = std::make_unique<int[]>(count);
            GetSelItems(count, indexes.get());
            int i = count;
            while (i--)
            {
                if (indexes[i] < Dirs->Count)
                    dirs++;
                else
                    files++;
            }
        }
        else
        {
            if (countSizeMode == 2) // calculate size of all subdirectories
            {
                count = Dirs->Count;
                if (subDir)
                    count--;
                if (count > 0)
                {
                    indexes = std::make_unique<int[]>(count);
                    int i = (subDir ? 1 : 0);
                    int j;
                    for (j = 0; j < count; j++)
                        indexes[j] = i++;
                }
            }
            else
                count = 0;
        }

#ifndef _WIN64
        if (Windows64Bit && (type == atMove || type == atDelete))
        {
            int oneIndex;
            if (count == 0)
            {
                oneIndex = GetCaretIndex();
                if (oneIndex == 0 && subDir)
                    oneIndex = -1;
            }
            std::wstring redirectedDir;
            if ((count > 0 || oneIndex != -1) &&
                ContainsWin64RedirectedDir(this, count > 0 ? indexes.get() : &oneIndex,
                                           count > 0 ? count : 1, redirectedDir, FALSE))
            {
                // Was a narrow _snprintf_s format string writing into a
                // wide scratch buffer via a %s that received a wide argument - garbage output,
                // invisible on this build (this whole block is x86-only, #ifndef _WIN64,
                // so it never compiled under the x64 toolchain that reports error counts).
                std::wstring msg = FormatStrW(LoadStrW(type == atMove ? IDS_ERRMOVESELCONTW64ALIAS : IDS_ERRDELETESELCONTW64ALIAS),
                                              redirectedDir.c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                // RAII: indexes auto-deleted when scope exits
                FilesActionInProgress = FALSE;
                EndStopRefresh();
                EndSuspendMode(); // the snooper starts again now
                return;
            }
        }
#endif // _WIN64

        //---  build the target path for copy/move
        std::wstring pathW;
        target->GetGeneralPath(pathW);
        if (target->Is(ptDisk))
        {
            pathW = target->GetPathW();
            SalPathAppendW(pathW, L"*.*");
        }
        else
        {
            if (target->Is(ptZIPArchive))
            {
                SalPathAddBackslashW(pathW);

                // if packing to the archive in the other panel is not possible, leave the path empty
                int format = PackerFormatConfig.PackIsArchive(target->GetZIPArchive());
                if (format != 0) // we found a supported archive
                {
                    if (!PackerFormatConfig.GetUsePacker(format - 1)) // no edit -> empty operation target
                    {
                        pathW.clear();
                    }
                }
            }
            else
            {
                if (target->Is(ptPluginFS) && (type == atCopy || type == atMove))
                {
                    if (target->GetPluginFS()->NotEmpty() &&
                        (type == atCopy && target->GetPluginFS()->IsServiceSupported(FS_SERVICE_COPYFROMDISKTOFS) ||
                         type == atMove && target->GetPluginFS()->IsServiceSupported(FS_SERVICE_MOVEFROMDISKTOFS)))
                    {
                        // // this is just a modification of the target path text in the plug-in -> no point in lowering the thread's priority
                        int selFiles = 0;
                        int selDirs = 0;
                        if (count > 0) // some files are selected
                        {
                            selFiles = files;
                            selDirs = count - files;
                        }
                        else // take the focused item
                        {
                            int index = GetCaretIndex();
                            if (index >= Dirs->Count)
                                selFiles = 1;
                            else
                                selDirs = 1;
                        }
                        std::wstring pluginTargetPath = pathW;
                        if (!target->GetPluginFS()->CopyOrMoveFromDiskToFS(type == atCopy, 1,
                                                                           target->GetPluginFS()->GetPluginFSName(),
                                                                           HWindow, NULL, NULL, NULL,
                                                                           selFiles, selDirs, pluginTargetPath, NULL))
                        {
                            pathW.clear(); // error while retrieving the target path
                        }
                        else
                        {
                            // convert the path to external format (before showing it in the dialog)
                            PluginFSConvertPathToExternal(pluginTargetPath);
                            pathW = std::move(pluginTargetPath);
                        }
                    }
                    else
                    {
                        pathW.clear(); // no copy/move from disk to FS
                    }
                }
            }
        }
        if (!pathW.empty())
        {
            target->UserWorkedOnThisPath = TRUE; // default action = working with the path in the target panel
        }
        //---
        int recycle = 0;
        BOOL canUseRecycleBin = TRUE;
        BOOL containsReservedNul = FALSE;
        if (type == atDelete)
        {
            if (count > 0)
            {
                int i;
                for (i = 0; i < count; i++)
                {
                    int index = indexes[i];
                    if (index < 0 || index >= Dirs->Count + Files->Count)
                        continue;
                    if (index == 0 && subDir)
                        continue; // ".."
                    CFileData* item = (index < Dirs->Count) ? &Dirs->At(index) : &Files->At(index - Dirs->Count);
                    if (ShouldBypassRecycleBinForDeleteW(item->Name))
                    {
                        containsReservedNul = TRUE;
                        break;
                    }
                }
            }
            else
            {
                int index = GetCaretIndex();
                if (index >= 0 && index < Dirs->Count + Files->Count &&
                    !(index == 0 && subDir))
                {
                    CFileData* item = (index < Dirs->Count) ? &Dirs->At(index) : &Files->At(index - Dirs->Count);
                    containsReservedNul = ShouldBypassRecycleBinForDeleteW(item->Name);
                }
            }

            // Gates whether deleted files go to the Recycle Bin; asked of the
            // CP_ACP mirror, a non-ASCII panel path could misread the drive type and
            // silently change recycle-vs-permanent-delete behavior.
            BOOL driveIsFixed = MyGetDriveTypeW(GetPathW()) == DRIVE_FIXED;
            canUseRecycleBin = driveIsFixed;
            recycle = ComputeDeleteRecycleMode(driveIsFixed, Configuration.UseRecycleBin,
                                               invertRecycleBin, containsReservedNul);
        }

        CFileData* f = NULL;
        std::wstring formatedFileNameW;
        std::wstring expandedW;
        BOOL deleteLink = FALSE;
        if (count <= 1) // one selected item or none
        {
            if (countSizeMode != 2) // not calculating all subdirectory sizes (count is the number of selected items)
            {
                int i;
                if (count == 0)
                    i = GetCaretIndex();
                else
                    GetSelItems(1, &i);

                if (i < 0 || i >= Dirs->Count + Files->Count || // invalid index (no files)
                    i == 0 && subDir)                           // we do not work with ".."
                {
                    // RAII: indexes auto-deleted when scope exits
                    FilesActionInProgress = FALSE;
                    EndStopRefresh();
                    EndSuspendMode(); // thesnooper starts again now
                    return;
                }
                BOOL isDir = i < Dirs->Count;
                f = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
                if (type == atDelete && recycle != 1 && (f->Attr & FILE_ATTRIBUTE_REPARSE_POINT))
                { // it's a link (junction, symlink or volume mount point)
                    // Probe wide. Composed from the two mirrors this asked the OS about a
                    // path spelled with '?', which does not exist, so the probe failed and
                    // the link was reported as an ordinary directory - the user was then
                    // asked to confirm deleting a "directory" that is really a junction
                    // (audit A25). Both halves have to be wide: a junction with an ASCII
                    // name inside a non-ANSI folder is mangled by the path alone.
                    std::wstring probeW = GetPathW();
                    ResolveSubstsW(probeW);
                    SalPathAppendW(probeW, f->Name);
                    int repPointType;
                    if (GetReparsePointDestinationOwnedW(probeW.c_str(), NULL, &repPointType, TRUE))
                    {
                        expandedW = LoadStrW(repPointType == 1 /* MOUNT POINT */ ? IDS_QUESTION_VOLMOUNTPOINT : repPointType == 2 /* JUNCTION POINT */ ? IDS_QUESTION_JUNCTION
                                                                                                                                                        : IDS_QUESTION_SYMLINK);
                        deleteLink = TRUE;
                    }
                }
                formatedFileNameW = AlterFileNameW(f->Name, Configuration.FileNameFormat, 0, isDir != 0);
                if (expandedW.empty())
                {
                    int questionID = isDir ? IDS_QUESTION_DIRECTORY : IDS_QUESTION_FILE;
                    expandedW = LoadStrW(questionID);
                }
            }
        }
        else // count-files in directories and individualfiles
        {
            expandedW = ExpandPluralFilesDirsTextW(files, count - files, epfdmNormal, FALSE);
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
        {
            switch (recycle)
            {
            case 0:
                resID = IDS_CONFIRM_DELETE;
                break;
            case 1:
                break; // this message will not be shown because DeleteThroughRecycleBin performs the confirmation
            case 2:
                resID = deleteLink ? IDS_CONFIRM_DELETE : IDS_CONFIRM_DELETE2;
                break;
            }
            break;
        }
        }
        CTruncatedString str;
        if (resID != 0)
        {
            // The count<=1 arms merged. They built the same wide subject and
            // differed only in where the substring came from; the second used the narrow
            // CTruncatedString::Set, whose text CMessageBox never reads (it calls GetW()
            // unconditionally), so that arm produced a blank confirmation body.
            if (count <= 1)
            {
                std::wstring subjectW = FormatStrW(LoadStrW(resID), expandedW.c_str());
                str.SetW(subjectW.c_str(), formatedFileNameW.c_str());
            }
            else
            {
                std::wstring subjectW = FormatStrW(LoadStrW(resID), expandedW.c_str());
                str.SetW(subjectW.c_str(), NULL);
            }
        }

        //---
        int res;
        DWORD clusterSize = 0;
        CChangeCaseData changeCaseData;
        CCriteriaData criteria;
        if (CopyMoveOptions.Get() != NULL) // if they exist, pull the defaults
            criteria = *CopyMoveOptions.Get();
        CCriteriaData* criteriaPtr = NULL; // pointer to 'criteria'; if NULL, they are ignored
        BOOL copyToExistingDir = FALSE;
        std::wstring nextFocusW;
        std::wstring operationMaskW;
        switch (type)
        {
        case atCopy:
        case atMove:
        {
            BOOL havePermissions = FALSE;
            // Asked of the CP_ACP mirror, this could misreport ADS support for
            // a non-ASCII panel path - same class of defect as the adjacent ACL-support query
            // fixed earlier, just missed for this sibling query at the time.
            BOOL supportsADS = IsPathOnVolumeSupADSW(GetPathW(), NULL);
            DWORD dummy1, flags;
            // Asked of the CP_ACP mirror, this could misreport ACL support for
            // a non-ASCII panel path; GetPathW() is already the panel's authoritative value.
            if (MyGetVolumeInformationW(GetPathW(), NULL, NULL, NULL, NULL, NULL, &dummy1, &flags, NULL))
                havePermissions = (flags & FS_PERSISTENT_ACLS) != 0;
            while (1)
            {
                CCopyMoveMoreDialog copyMoveDlg(HWindow, pathW,
                                               (type == atCopy) ? LoadStrW(IDS_COPY) : LoadStrW(IDS_MOVE), &str,
                                               (type == atCopy) ? IDD_COPYDIALOG : IDD_MOVEDIALOG,
                                               Configuration.CopyHistory, COPY_HISTORY_SIZE,
                                               &criteria, havePermissions, supportsADS);
                res = (int)copyMoveDlg.Execute();
                if (!havePermissions)
                    criteria.CopySecurity = FALSE;
                if (res != IDOK)
                    break;
                criteriaPtr = criteria.IsDirty() ? &criteria : NULL;
                UpdateWindow(MainWindow->HWindow);

                if (!IsPluginFSPath(pathW.c_str()) &&
                    (pathW.length() >= 2 && pathW[1] == L':' ||
                     pathW.length() >= 2 && (pathW[0] == L'/' || pathW[0] == L'\\') && (pathW[1] == L'/' || pathW[1] == L'\\') ||
                     Is(ptDisk) || Is(ptZIPArchive)))                                              // disk/archive relative paths
                {                                                                                  // it's a disk path (absolute or relative) - convert all '/' to '\' and remove duplicate '\'
                    SlashesToBackslashesAndRemoveDups(pathW);
                    pathW.resize(wcslen(pathW.c_str()));
                }

                int len = (int)pathW.length();
                BOOL backslashAtEnd = (len > 0 && pathW[len - 1] == L'\\');
                BOOL mustBePath = (len == 2 && towlower(pathW[0]) >= L'a' && towlower(pathW[0]) <= L'z' &&
                                   pathW[1] == L':');

                int pathType;
                BOOL pathIsDir;
                wchar_t* secondPart = NULL;
                nextFocusW.clear();
                operationMaskW.clear();

                // #94: A Copy/Move target that names an existing, packable archive without a
                // trailing backslash was silently overwritten (the whole archive destroyed) via
                // the plain file-overwrite path. Detect that and append a backslash so ParsePath
                // below routes it to the archive/pack flow ("Add to existing archive?"), matching
                // drag & drop. Brand-new "name.zip" targets and non-archives are left untouched.
                if ((type == atCopy || type == atMove) && !backslashAtEnd && !pathW.empty() &&
                    !IsPluginFSPath(pathW.c_str()))
                {
                    std::wstring full(pathW);
                    if (SalGetFullNameW(full, NULL, GetPathW()))
                    {
                        int fmt = PackerFormatConfig.PackIsArchive(full.c_str());
                        DWORD attrs = (fmt != 0) ? gFileSystem->GetFileAttributes(full.c_str())
                                                 : INVALID_FILE_ATTRIBUTES;
                        BOOL exists = (attrs != INVALID_FILE_ATTRIBUTES);
                        BOOL isDir = exists && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
                        if (ShouldRerouteCopyTargetToArchive(true, false, exists != FALSE, isDir != FALSE,
                                                             fmt, fmt != 0 && PackerFormatConfig.GetUsePacker(fmt - 1)))
                        {
                            SalPathAddBackslashW(pathW);
                            backslashAtEnd = TRUE;
                            len = (int)pathW.length();
                        }
                    }
                }

                BOOL parsedOk = ParsePathW(pathW, pathType, pathIsDir, secondPart,
                                           type == atCopy ? LoadStrW(IDS_ERRORCOPY) : LoadStrW(IDS_ERRORMOVE),
                                           count <= 1 ? &nextFocusW : NULL, NULL);
                if (parsedOk)
                {
                    // use 'if' instead of a 'switch' to ensure that 'break' and 'continue' work correctly
                    if (pathType == PATH_TYPE_WINDOWS) // Windows path (drive + UNC)
                    {
                        CFileData* dir = (count == 0) ? f : ((indexes[0] < Dirs->Count) ? &Dirs->At(indexes[0]) : &Files->At(indexes[0] - Dirs->Count));
                        const size_t secondPartOffset = static_cast<size_t>(secondPart - pathW.data());
                        const BOOL secondPartWasEmpty = *secondPart == L'\0';
                        BOOL splitOk = SalSplitWindowsPathOwnedW(HWindow, LoadStrW(type == atCopy ? IDS_COPY : IDS_MOVE),
                                                                LoadStrW(type == atCopy ? IDS_ERRORCOPY : IDS_ERRORMOVE),
                                                                count, pathW, secondPartOffset, pathIsDir,
                                                                backslashAtEnd || mustBePath, dir->Name,
                                                                GetPathW(), operationMaskW);
                        if (splitOk)
                        {
                            if (!nextFocusW.empty() && secondPartWasEmpty)
                                copyToExistingDir = TRUE;
                            break; // exit the Copy/Move loop and perform the operation
                        }
                        else
                        {
                            continue; // back to the Copy/Move dialog
                        }
                    }
                    else
                    {
                        if (pathType == PATH_TYPE_ARCHIVE) // path into an archive
                        {
                            if (criteriaPtr != NULL && Configuration.CnfrmCopyMoveOptionsNS) // archives do not support options from the Copy/Move dialog
                            {
                                bool dontShow = !Configuration.CnfrmCopyMoveOptionsNS;
                                gPrompter->ShowInfoWithCheckbox(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_MOVECOPY_OPTIONS_NOTSUPPORTED),
                                                                LoadStrW(IDS_MOVECOPY_OPTIONS_NOTSUPPORTED_AGAIN), &dontShow);
                                Configuration.CnfrmCopyMoveOptionsNS = !dontShow;
                            }

                            CPanelTmpEnumData data;
                            int oneIndex = -1;
                            if (count > 0) // some files are selected
                            {
                                data.IndexesCount = count;
                                data.Indexes = indexes.get(); // RAII: auto-deleted via unique_ptr
                            }
                            else // take the focused item
                            {
                                oneIndex = GetCaretIndex();
                                data.IndexesCount = 1;
                                data.Indexes = &oneIndex; // not deallocated
                            }
                            data.CurrentIndex = 0;
                            data.ZIPPath = GetZIPPath();
                            data.Dirs = Dirs;
                            data.Files = Files;
                            data.ArchiveDir = GetArchiveDir();
                            data.WorkPathW = GetPathW();
                            data.EnumLastDir = NULL;
                            data.EnumLastIndex = -1;

                            //---  check if it's a zero-size file
                            BOOL nullFile;
                            BOOL hasPath = *secondPart != 0;
                            if (hasPath && !backslashAtEnd && !mustBePath) // check whether they used an operational mask -> we are not able to handle that
                            {
                                gPrompter->ShowError((type == atCopy) ? LoadStrW(IDS_ERRORCOPY) : LoadStrW(IDS_ERRORMOVE),
                                                     LoadStrW(IDS_MOVECOPY_OPMASKSNOTSUP));
                                continue; // back to the copy/move dialog
                            }

                            const size_t archiveEnd = static_cast<size_t>(secondPart - pathW.data());
                            const std::wstring archivePath(pathW.data(), archiveEnd);
                            const std::wstring pathInArchive = hasPath ? secondPart + 1 : L"";
                            BOOL haveSize = FALSE;
                            CQuadWord size;
                            DWORD err;
                            HANDLE hFile = gFileSystem->CreateFile(archivePath.c_str(), GENERIC_READ, 0, NULL,
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

                                //---  if it's a zero-size file, we must delete it; archivers can not handle them
                                DWORD nullFileAttrs;
                                if (nullFile)
                                {
                                    nullFileAttrs = gFileSystem->GetFileAttributes(archivePath.c_str());
                                    ClearReadOnlyAttr(archivePath.c_str(), nullFileAttrs); // so it's possible to delete even read-only files
                                    gFileSystem->DeleteFile(archivePath.c_str());
                                }
                                //---  custom packing
                                gEnvironment->SetCurrentDirectory(GetPathW());
                                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                                if (PackCompress(HWindow, this, archivePath.c_str(), pathInArchive.c_str(),
                                                 type == atMove, GetPathW(), PanelEnumDiskSelection, &data))
                                {                   // packing succeeded
                                    if (nullFile && // a zero-size file might have a different compressed attribute; set the archive to the same
                                        nullFileAttrs != INVALID_FILE_ATTRIBUTES)
                                    {
                                        HANDLE hFile2 = gFileSystem->CreateFile(archivePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                                                               0, NULL, OPEN_EXISTING, 0, NULL);
                                        HANDLES_ADD_EX(__otQuiet, hFile2 != INVALID_HANDLE_VALUE, __htFile,
                                                       __hoCreateFile, hFile2, GetLastError(), TRUE);
                                        if (hFile2 != INVALID_HANDLE_VALUE)
                                        {
                                            // restore the 'compressed' flag; it simply doesn't work on FAT or FAT32
                                            gFileSystem->SetHandleCompression(
                                                hFile2, (nullFileAttrs & FILE_ATTRIBUTE_COMPRESSED) != 0);
                                            HANDLES_REMOVE(hFile2, __htFile, "IFileSystem::CloseHandle");
                                            gFileSystem->CloseFileHandle(hFile2);
                                            gFileSystem->SetFileAttributes(archivePath.c_str(), nullFileAttrs);
                                        }
                                    }
                                    SetSel(FALSE, -1, TRUE);                        // explicit redraw
                                    PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
                                }
                                else
                                {
                                    if (nullFile) // it failed, we have to create it again
                                    {
                                        HANDLE hFile2 = gFileSystem->CreateFile(archivePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                                                               0, NULL, OPEN_ALWAYS, 0, NULL);
                                        HANDLES_ADD_EX(__otQuiet, hFile2 != INVALID_HANDLE_VALUE, __htFile,
                                                       __hoCreateFile, hFile2, GetLastError(), TRUE);
                                        if (hFile2 != INVALID_HANDLE_VALUE)
                                        {
                                            if (nullFileAttrs != INVALID_FILE_ATTRIBUTES)
                                            {
                                                // restore the "compressed" flag; on FAT and FAT32 it simply doesn't work
                                                gFileSystem->SetHandleCompression(
                                                    hFile2, (nullFileAttrs & FILE_ATTRIBUTE_COMPRESSED) != 0);
                                            }
                                            HANDLES_REMOVE(hFile2, __htFile, "IFileSystem::CloseHandle");
                                            gFileSystem->CloseFileHandle(hFile2);
                                            if (nullFileAttrs != INVALID_FILE_ATTRIBUTES)
                                                gFileSystem->SetFileAttributes(archivePath.c_str(), nullFileAttrs);
                                        }
                                    }
                                }
                                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                                SetCurrentDirectoryToSystem();

                                UpdateWindow(MainWindow->HWindow);

                                //---  refresh of non-auto-refreshed directories
                                // change in the directory with the target archive (the archive file changes)
                                std::wstring archiveDirectory = archivePath;
                                CutDirectoryW(archiveDirectory);
                                MainWindow->PostChangeOnPathNotificationW(archiveDirectory.c_str(), FALSE);
                                if (type == atMove)
                                {
                                    // changes on the source path (file/directory deletion should take place when moving a file into the archive
                                    MainWindow->PostChangeOnPathNotificationW(GetPathW(), TRUE);
                                }
                            }
                            else
                            {
                                std::wstring msg = FormatStrW(LoadStrW(IDS_FILEERRORFORMAT), archivePath.c_str(), GetErrorTextOwned(err).c_str());
                                gPrompter->ShowError((type == atCopy) ? LoadStrW(IDS_ERRORCOPY) : LoadStrW(IDS_ERRORMOVE), msg.c_str());
                                if (backslashAtEnd || mustBePath)
                                    SalPathAddBackslashW(pathW);
                                continue; // back to the copy/move dialog
                            }
                            // RAII: indexes auto-deleted when scope exits
                            //---  if a Salamander window is active, suspend mode ends
                            EndStopRefresh();
                            EndSuspendMode();
                            FilesActionInProgress = FALSE;
                            return;
                        }
                        else
                        {
                            if (pathType == PATH_TYPE_FS) // file-system path
                            {
                                if (criteriaPtr != NULL && Configuration.CnfrmCopyMoveOptionsNS) // file systems do not support options from the Copy/Move dialog
                                {
                                    bool dontShow = !Configuration.CnfrmCopyMoveOptionsNS;
                                    gPrompter->ShowInfoWithCheckbox(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_MOVECOPY_OPTIONS_NOTSUPPORTED),
                                                                    LoadStrW(IDS_MOVECOPY_OPTIONS_NOTSUPPORTED_AGAIN), &dontShow);
                                    Configuration.CnfrmCopyMoveOptionsNS = !dontShow;
                                }

                                // prepare data to enumerate files and directories from the panel
                                CPanelTmpEnumData data;
                                int oneIndex = -1;
                                int selFiles = 0;
                                int selDirs = 0;
                                if (count > 0) // some files are selected
                                {
                                    selFiles = files;
                                    selDirs = count - files;
                                    data.IndexesCount = count;
                                    data.Indexes = indexes.get(); // RAII: auto-deleted via unique_ptr
                                }
                                else // take the focused item
                                {
                                    oneIndex = GetCaretIndex();
                                    if (oneIndex >= Dirs->Count)
                                        selFiles = 1;
                                    else
                                        selDirs = 1;
                                    data.IndexesCount = 1;
                                    data.Indexes = &oneIndex; // not deallocated
                                }
                                data.CurrentIndex = 0;
                                data.ZIPPath = GetZIPPath();
                                data.Dirs = Dirs;
                                data.Files = Files;
                                data.ArchiveDir = GetArchiveDir();
                                data.WorkPathW = GetPathW();
                                data.EnumLastDir = NULL;
                                data.EnumLastIndex = -1;

                                // obtain the file-system name
                                const size_t fsNameLen = static_cast<size_t>(secondPart - pathW.data()) - 1;
                                const std::wstring fsName(pathW.data(), fsNameLen);

                                // lower the thread priority to "normal" so the operation does not overload the machine
                                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

                                // choose the FS that will perform the operation (order: active, detached, new)
                                BOOL invalidPath = FALSE;
                                BOOL unselect = FALSE;
                                CDetachedFSList* list = MainWindow->DetachedFSList;
                                int i;
                                for (i = -1; i < list->Count; i++)
                                {
                                    CPluginFSInterfaceEncapsulation* fs = NULL;
                                    if (i == -1) // first try the active FS in the target panel
                                    {
                                        if (target->Is(ptPluginFS))
                                            fs = target->GetPluginFS();
                                    }
                                    else
                                        fs = list->At(i); // then detached FS

                                    int fsNameIndex;
                                    if (fs != NULL && fs->NotEmpty() &&                          // interface is valid
                                        fs->IsFSNameFromSamePluginAsThisFS(fsName.c_str(), fsNameIndex)) // the FS name comes from the same plugin (otherwise there's no point in trying)
                                    {
                                        BOOL invalidPathOrCancel;
                                        std::wstring pluginTargetPath = pathW;
                                        std::wstring userPart = pluginTargetPath.substr(fsName.size() + 1);
                                        fs->GetPluginInterfaceForFS()->ConvertPathToInternalW(fsName.c_str(), fsNameIndex, userPart);
                                        pluginTargetPath.resize(fsName.size() + 1);
                                        pluginTargetPath += userPart;
                                        if (fs->CopyOrMoveFromDiskToFS(type == atCopy, 2, fs->GetPluginFSName(),
                                                                       HWindow, GetPathW(), PanelEnumDiskSelection, &data,
                                                                       selFiles, selDirs, pluginTargetPath, &invalidPathOrCancel))
                                        {
                                            unselect = !invalidPathOrCancel;
                                            break; // done
                                        }
                                        else // error
                                        {
                                            // before the next use we must reset it (so it enumerates from the beginning again)
                                            data.Reset();

                                            if (invalidPathOrCancel)
                                            {
                                                // convert the path to external format (before displaying it in the dialog)
                                                PluginFSConvertPathToExternal(pluginTargetPath);
                                                pathW = std::move(pluginTargetPath);
                                                invalidPath = TRUE;
                                                break; // we must go back to the copy/move dialog
                                            }
                                            // trying another FS
                                        }
                                    }
                                }
                                if (i == list->Count) // active and detached FS couldn't handle it, we will create a new FS
                                {
                                    int index;
                                    int fsNameIndex;
                                    if (Plugins.IsPluginFS(fsName.c_str(), index, fsNameIndex)) // determine the plugin index
                                    {
                                        // obtain the plugin with the FS
                                        CPluginData* plugin = Plugins.Get(index);
                                        if (plugin != NULL)
                                        {
                                            // open a new FS
                                            // load the plugin before obtaining DLLName, Version and plugin interfaces
                                            CPluginFSInterfaceAbstract* auxFS = plugin->OpenFS(fsName.c_str(), fsNameIndex);
                                            CPluginFSInterfaceEncapsulation pluginFS(auxFS, plugin->DLLName.c_str(), plugin->Version.c_str(),
                                                                                     plugin->GetPluginInterfaceForFS()->GetInterface(),
                                                                                     plugin->GetPluginInterface()->GetInterface(),
                                                                                     fsName.c_str(), fsNameIndex, -1, 0, plugin->BuiltForVersion);
                                            if (pluginFS.NotEmpty())
                                            {
                                                Plugins.SetWorkingPluginFS(&pluginFS);
                                                BOOL invalidPathOrCancel;
                                                std::wstring pluginTargetPath = pathW;
                                                std::wstring userPart = pluginTargetPath.substr(fsName.size() + 1);
                                                pluginFS.GetPluginInterfaceForFS()->ConvertPathToInternalW(fsName.c_str(), fsNameIndex, userPart);
                                                pluginTargetPath.resize(fsName.size() + 1);
                                                pluginTargetPath += userPart;
                                                if (pluginFS.CopyOrMoveFromDiskToFS(type == atCopy, 2,
                                                                                    pluginFS.GetPluginFSName(),
                                                                                    HWindow, GetPathW(),
                                                                                    PanelEnumDiskSelection, &data,
                                                                                    selFiles, selDirs, pluginTargetPath,
                                                                                    &invalidPathOrCancel))
                                                { // done/cancel
                                                    unselect = !invalidPathOrCancel;
                                                }
                                                else // syntax error/plugin error
                                                {
                                                    if (invalidPathOrCancel)
                                                    {
                                                        // convert the path to external format (before displaying it in the dialog)
                                                        PluginFSConvertPathToExternal(pluginTargetPath);
                                                        pathW = std::move(pluginTargetPath);
                                                        invalidPath = TRUE; // we must go back to the Copy/Move dialog
                                                    }
                                                    else // plugin error (new FS, but returns "requested operation cannot be performed on this FS" error)
                                                    {
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
                                            TRACE_E("Unexpected situation in CFilesWindow::FilesAction() - unable to work with plugin.");
                                    }
                                    else
                                    {
                                        TRACE_EW(L"Unexpected situation in CFilesWindow::FilesAction() - file-system " << fsName.c_str() << L" was not found.");
                                    }
                                }

                                // raise the thread's priority again, the operation has finished
                                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                                SetCurrentDirectoryToSystem(); // in any case restore the current directory as well

                                if (invalidPath)
                                    continue; // back to the copy/move dialog

                                if (unselect) // unselect files/directories in the panel
                                {
                                    SetSel(FALSE, -1, TRUE);                        // explicit redraw
                                    PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
                                }
                                // RAII: indexes auto-deleted when scope exits
                                //---  if any Salamander window is active, suspend mode ends
                                EndStopRefresh();
                                EndSuspendMode();
                                FilesActionInProgress = FALSE;
                                return;
                            }
                        }
                    }
                }
            }
            break;
        }

        case atDelete:
        {
            if (Configuration.CnfrmFileDirDel && recycle != 1)
            { 
                // Ask only if requested and if we don't use the SHFileOperation API for deletion
                PromptResult pr = gPrompter->ConfirmDelete(str.GetW(), recycle == 1);
                res = (pr.type == PromptResult::kYes ? IDOK : IDCANCEL);
                UpdateWindow(MainWindow->HWindow);
            }
            else
            {
                res = IDOK;
            }
            break;
        }

        case atCountSize:
            res = IDOK;
            break;

        case atChangeCase:
        {
            CChangeCaseDlg dlg(HWindow, SelectionContainsDirectory());
            res = (int)dlg.Execute();
            UpdateWindow(MainWindow->HWindow);
            changeCaseData.FileNameFormat = dlg.FileNameFormat;
            changeCaseData.Change = dlg.Change;
            changeCaseData.SubDirs = dlg.SubDirs;
            break;
        }
        }
        if (res == IDOK && // begin disk operation
            CheckPath(TRUE) == ERROR_SUCCESS)
        {
            if (type == atDelete && recycle == 1)
            {
                if (DeleteThroughRecycleBin(indexes.get(), count, f))
                {
                    SetSel(FALSE, -1, TRUE);                        // explicit redraw
                    PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
                    UpdateWindow(MainWindow->HWindow);
                }

                //---  refresh of directories that are not auto-refreshed
                // change in the directory displayed in the panel and its subdirectories
                MainWindow->PostChangeOnPathNotificationW(GetPathW(), TRUE);
            }
            else
            {
                COperations* script;
                if (type == atCopy || type == atMove)
                {
                    // [unicode:P1.5cl] The queue title was assembled twice: a legacy branch
                    // sent narrow sprintf through wchar_t buffers, while a conditional wide
                    // sibling carried the exact name. Compose one authoritative UTF-16 title
                    // and keep it in the wide constructor fields; the legacy byte mirrors are
                    // deliberately empty rather than best-fit narrowing it.
                    std::wstring subjectW = FormatStrW(LoadStrW(type == atCopy ? IDS_COPYDLGTITLE : IDS_MOVEDLGTITLE),
                                                       expandedW.c_str());
                    if (count <= 1)
                    {
                        subjectW = FormatStrW(subjectW.c_str(), formatedFileNameW.c_str());
                    }
                    script = new COperations(1000, 500, subjectW.c_str(), GetPathW(), pathW.c_str());
                }
                else
                    script = new COperations(1000, 500, NULL, NULL, NULL);
                if (script == NULL)
                    TRACE_E(LOW_MEMORY);
                else
                {
                    const wchar_t* captionW = nullptr;
                    switch (type)
                    {
                    case atCopy:
                    {
                        captionW = LoadStrW(IDS_COPY);
                        script->ShowStatus = TRUE;
                        script->IsCopyOperation = TRUE;
                        script->IsCopyOrMoveOperation = TRUE;
                        break;
                    }

                    case atMove:
                    {
                        // Same defect as BuildScriptMain's fast-dir-move gate
                        // (fixed): asked of the CP_ACP mirrors, two different non-ASCII
                        // volumes can both render as '?'-strings and compare equal.
                        // GetPathW()/pathW are already the wide mirrors in scope here.
                        BOOL sameRootPath = HasTheSameRootPath(GetPathW(), pathW.c_str());
                        // Same defect, same wide mirrors, now via the new
                        // native-wide HasTheSameRootPathAndVolume implementation.
                        script->SameRootButDiffVolume = sameRootPath &&
                                                        !HasTheSameRootPathAndVolume(GetPathW(), pathW.c_str());
                        script->ShowStatus = !sameRootPath || script->SameRootButDiffVolume;
                        script->IsCopyOperation = FALSE;
                        script->IsCopyOrMoveOperation = TRUE;
                        captionW = LoadStrW(IDS_MOVE);
                        break;
                    }

                    case atDelete:
                        captionW = LoadStrW(IDS_DELETE);
                        break;
                    case atChangeCase:
                        captionW = LoadStrW(IDS_CHANGECASE);
                        break;
                    default:
                        captionW = L"";
                    }
                    if (criteriaPtr != NULL && criteriaPtr->UseSpeedLimit)
                        script->SetSpeedLimit(TRUE, criteriaPtr->SpeedLimit);
                    HWND hFocusedWnd = GetFocus();
                    CreateSafeWaitWindow(LoadStrW(IDS_ANALYSINGDIRTREEESC), NULL, 1000, TRUE, MainWindow->HWindow);
                    MainWindow->StartAnimate(); // example of  its usage
                    EnableWindow(MainWindow->HWindow, FALSE);

                    HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

                    script->InvertRecycleBin = invertRecycleBin;
                    script->CanUseRecycleBin = canUseRecycleBin;

                    const wchar_t* auxTargetPath = NULL;
                    if (type == atCopy || type == atMove)
                        auxTargetPath = pathW.c_str();
                    const wchar_t* auxTargetPathW = NULL;
                    if ((type == atCopy || type == atMove) && !pathW.empty())
                        auxTargetPathW = pathW.c_str();
                    const wchar_t* operationMask = operationMaskW.empty() ? NULL : operationMaskW.c_str();
                    BOOL res2 = BuildScriptMain(script, type, auxTargetPath, operationMask, count, indexes.get(),
                                                f, NULL, type == atChangeCase ? &changeCaseData : NULL,
                                                countSizeMode != 0,
                                                criteriaPtr, auxTargetPathW);
                    // ReanchorWideSourcePaths deleted — with the
                    // legacy do-loop gone, every op here is snapshot-built with
                    // explicit wide names; there is nothing to repair.
                    // if there's nothing to do, don't show the progress dialog
                    BOOL emptyScript = script->Count == 0 && type != atCountSize;

                    // swapped to allow activation of the main window (must not be disabled), otherwise it switches to another app
                    EnableWindow(MainWindow->HWindow, TRUE);
                    DestroySafeWaitWindow();
                    if (type == atCountSize) // additional directory sizes have been calculated
                    {
                        // perform sorting if counting was done across multiple selected directories or all directories
                        if (((countSizeMode == 0 && dirs > 1) || countSizeMode == 2) && SortType == stSize)
                        {
                            ChangeSortType(stSize, FALSE, TRUE);
                        }

                        // Here the disabled "remember calculated directory
                        // sizes" feature used to call DirectorySizesHolder.Store(this).
                        // CDirectorySizes/CDirectorySizesHolder are deleted: both call
                        // sites had been commented out for years and the class could not
                        // compile, so re-enabling means writing it fresh against wide types.

                        RefreshListBox(-1, -1, FocusedIndex, FALSE, FALSE); // recalculate column widths
                    }

                    // if Salamander is active, call SetFocus on the remembered window (SetFocus does not work
                    // when the main window is disabled - after deactivating/activating the disabled main window the active panel
                    // does not have focus)
                    HWND hwnd = GetForegroundWindow();
                    while (hwnd != NULL && hwnd != MainWindow->HWindow)
                        hwnd = GetParent(hwnd);
                    if (hwnd == MainWindow->HWindow)
                        SetFocus(hFocusedWnd);

                    SetCursor(oldCur);

                    BOOL cancel = FALSE;
                    if (!emptyScript && res2 && (type == atCopy || type == atMove))
                    {
                        CQuadWord requiredSpace;
                        if (ShouldWarnNotEnoughSpaceForCopyMove(script, pathW.c_str(), &requiredSpace))
                        {
                            const std::wstring requiredSpaceText = NumberToStr(requiredSpace);
                            const std::wstring freeSpaceText = NumberToStr(script->FreeSpace);
                            std::wstring msg = FormatStrW(LoadStrW(IDS_NOTENOUGHSPACE),
                                                          requiredSpaceText.c_str(), freeSpaceText.c_str());
                            cancel = gPrompter->AskYesNo(captionW, msg.c_str()).type != PromptResult::kYes;
                        }
                    }

                    if (!cancel)
                    {
                        // prepare refresh of directories that are not auto-refreshed
                        if (!emptyScript && type != atCountSize)
                        {
                            if (type == atDelete || type == atChangeCase || type == atMove)
                            {
                                // change in the directory displayed in the panel and its subdirectories
                                script->SetWorkPath1W(GetPathW(), TRUE);
                            }
                            if (type == atCopy)
                            {
                                // change in the target directory and its subdirectories
                                script->SetWorkPath1W(pathW.c_str(), TRUE);
                            }
                            if (type == atMove)
                            {
                                // change in the target directory and its subdirectories
                                script->SetWorkPath2W(pathW.c_str(), TRUE);
                            }
                        }

                        if (!emptyScript &&
                            (!res2 || type == atCountSize ||
                             !StartProgressDialog(script, captionW, NULL, NULL)))
                        {
                            if (res2 && type == atCountSize && countSizeMode == 0)
                            {
                                CSizeResultsDlg result(MainWindow->HWindow, script->TotalSize,
                                                       script->CompressedSize, script->OccupiedSpace,
                                                       script->FilesCount, script->DirsCount,
                                                       &script->Sizes);
                                result.Execute();
                            }
                            else
                                UpdateWindow(MainWindow->HWindow);
                            if (!script->IsGood())
                                script->ResetState();
                            FreeScript(script);
                        }
                        else // removing selected index
                        {
                            if (res2)
                            {
                                SetSel(FALSE, -1, TRUE);                        // explicit redraw
                                PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
                            }
                            if (!emptyScript && !nextFocusW.empty())
                            {
                                NextFocusNameW = nextFocusW;
                                DontClearNextFocusName = TRUE;
                                if (type == atCopy && copyToExistingDir)
                                { // when copying to a directory, it is necessary (RefreshDirectory might not happen)
                                    PostMessage(HWindow, WM_USER_DONEXTFOCUS, 0, 0);
                                }
                            }
                            if (emptyScript) // if the script is empty we must deallocate it
                            {
                                if (!script->IsGood())
                                    script->ResetState();
                                FreeScript(script);
                            }
                            UpdateWindow(MainWindow->HWindow);
                        }
                    }
                    else
                    {
                        if (!script->IsGood())
                            script->ResetState();
                        FreeScript(script);
                    }
                    MainWindow->StopAnimate(); // example of its usage
                }
            }
        }
        // RAII: indexes auto-deleted when scope exits
        //---  if any Salamander window is active, suspend mode ends
        EndStopRefresh();
        EndSuspendMode();
        FilesActionInProgress = FALSE;
    }
}

// Names a file for Simple MAPI, which is ANSI BY CONTRACT: MapiFileDesc::lpszPathName
// is LPSTR and Sally resolves the ANSI export "MAPISendMail" by name (mapi.h:20, mapi.cpp:44).
// There is no wide Simple MAPI to migrate to, so this is not a widening that was postponed - the
// narrow string is the protocol.
//
// That makes it the AnsiToolPathPolicy case rather than the "widen it" case: ask for an ANSI
// designator, accept the 8.3 alias when CP_ACP cannot spell the path, and refuse when neither
// exists - instead of handing MAPI a '?'-bearing path, which is what the previous code did every
// time and which names no file at all.
static bool NameForMapi(const std::wstring& widePath, std::wstring& out)
{
    auto shortName = [](const std::wstring& p) { return GetShortPathW(p.c_str()); };
    const auto resolved = sally::unicode::ResolveAnsiToolPath(widePath, shortName);
    if (!resolved.Usable())
    {
        // Needs BOTH a name CP_ACP cannot spell AND 8.3 generation disabled on the volume.
        // Aborting matches how every other AddFile failure here already behaves. A dedicated
        // message would be better, but adding a string id costs a translated row in all nine
        // sally.slt packs, and inventing that text is worse than the trace.
        TRACE_E("EmailFiles: no ANSI name designates this file, cannot attach it via Simple MAPI.");
        return false;
    }
    out = resolved.WidePath;
    return true;
}

// extracts all subdirectories from 'path' directory and calls itself recursively
// adds files mapi
// returns TRUE, if everything succeeded; otherwise returns FALSE
BOOL EmailFilesAddDirectoryW(CSimpleMAPI* mapi, const wchar_t* path, BOOL* errGetFileSizeOfLnkTgtIgnAll)
{
    std::wstring myPath(path);
    if (!myPath.empty() && myPath.back() != L'\\')
        myPath += L'\\';
    const size_t baseLen = myPath.length();

    WIN32_FIND_DATAW file;
    HANDLE find = SalFindFirstFileHW((myPath + L'*').c_str(), &file);
    if (find == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES)
        {
            std::wstring msg = std::wstring(path) + L": " + GetErrorTextOwned(err).c_str();
            if (gPrompter->ConfirmError(LoadStrW(IDS_ERRORTITLE), msg.c_str()).type == PromptResult::kCancel)
            {
                SetCursor(LoadCursor(NULL, IDC_WAIT));
                return FALSE; // user wants to quit
            }
            SetCursor(LoadCursor(NULL, IDC_WAIT));
        }
        return TRUE; // user wants to continue
    }
    BOOL ok = TRUE;
    do
    {
        if (file.cFileName[0] == 0 || wcscmp(file.cFileName, L".") == 0 ||
            wcscmp(file.cFileName, L"..") == 0)
        {
            continue;
        }

        myPath.resize(baseLen);
        myPath += file.cFileName;

        if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!EmailFilesAddDirectoryW(mapi, myPath.c_str(), errGetFileSizeOfLnkTgtIgnAll))
            {
                ok = FALSE;
                break;
            }
        }
        else
        {
            // links: size == 0, the file size must be obtained additionally via GetLinkTgtFileSize()
            BOOL cancel = FALSE;
            CQuadWord size(file.nFileSizeLow, file.nFileSizeHigh);
            if ((file.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                !GetLinkTgtFileSize(MainWindow->HWindow, myPath.c_str(), NULL, &size, &cancel, errGetFileSizeOfLnkTgtIgnAll))
            {
                size.Set(file.nFileSizeLow, file.nFileSizeHigh);
            }
            std::wstring mapiName;
            // Keep the original wide path or the exact 8.3 wide alias. CSimpleMAPI owns it as
            // UTF-16 and performs the one exact conversion at MAPISendMail's external ANSI ABI.
            if (cancel || !NameForMapi(myPath, mapiName) || !mapi->AddFile(mapiName.c_str(), &size))
            {
                ok = FALSE;
                break;
            }
        }
    } while (SalLPFindNextFile(find, &file));
    SalLPFindClose(find);
    return ok;
}

// pulls the selection from the panel and processes every item:
// if it's a directory, calls EmailFilesAddDirectoryW for it
// if it's a file, adds it to mapi
// if everything succeeds, creates the email
void CFilesWindow::EmailFiles()
{
    CALL_STACK_MESSAGE1("CFilesWindow::EmailFiles()");
    if (Dirs->Count + Files->Count == 0)
        return;
    if (!Is(ptDisk))
        return;

    BeginStopRefresh(); // the snooper takes a break

    if (!FilesActionInProgress)
    {
        FilesActionInProgress = TRUE;

        CSimpleMAPI* mapi = new CSimpleMAPI;
        if (mapi != NULL)
        {
            if (mapi->Init(HWindow))
            {
                BOOL send = TRUE;
                int selCount = GetSelCount();
                int alloc = (selCount == 0 ? 1 : selCount);

                std::unique_ptr<int[]> indexes = std::make_unique<int[]>(alloc); // RAII: auto-deleted when scope exits
                {
                    HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

                    if (selCount > 0)
                        GetSelItems(selCount, indexes.get());
                    else
                        indexes[0] = GetCaretIndex();

                    CFileData* f;
                    BOOL errGetFileSizeOfLnkTgtIgnAll = FALSE;
                    for (int i = 0; i < alloc; i++)
                    {
                        if (indexes[i] >= 0 && indexes[i] < Dirs->Count + Files->Count)
                        {
                            BOOL isDir = indexes[i] < Dirs->Count;
                            f = (indexes[i] < Dirs->Count) ? &Dirs->At(indexes[i]) : &Files->At(indexes[i] - Dirs->Count);
                            // Built entirely from wide sources. The base is GetPathW()
                            // rather than the removed ANSI mirror this used to be filled from, and
                            // the leaf is f->Name, which is the exact wide name (P1.3 retired the
                            // separate NameW mirror this comment used to reference).
                            std::wstring itemPathW = GetPathW();
                            SalPathAppendW(itemPathW, f->Name);
                            if (isDir)
                            {
                                if (!EmailFilesAddDirectoryW(mapi, itemPathW.c_str(), &errGetFileSizeOfLnkTgtIgnAll))
                                {
                                    send = FALSE;
                                    break;
                                }
                            }
                            else
                            {
                                // links: f->Size == 0, file size must be obtained additionally via GetLinkTgtFileSize()
                                BOOL cancel = FALSE;
                                CQuadWord size = f->Size;
                                if ((f->Attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                                    !GetLinkTgtFileSize(HWindow, itemPathW.c_str(), NULL, &size, &cancel, &errGetFileSizeOfLnkTgtIgnAll))
                                {
                                    size = f->Size;
                                }
                                std::wstring mapiName;
                                if (cancel || !NameForMapi(itemPathW, mapiName) ||
                                    !mapi->AddFile(mapiName.c_str(), &size))
                                {
                                    send = FALSE;
                                    break;
                                }
                            }
                        }
                    }
                    // RAII: indexes auto-deleted when scope exits
                    SetCursor(oldCur);
                }
                if (send && mapi->GetFilesCount() == 0)
                {
                    // no file to send; display information and exit
                    gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_WANTEMAIL_NOFILE));
                    send = FALSE;
                }
                if (send)
                {
                    bool confirmed = true;
                    bool dontShow = !Configuration.CnfrmSendEmail;
                    if (!dontShow)
                    {
                        const std::wstring expanded = ExpandPluralFilesDirsTextW(
                            mapi->GetFilesCount(), 0, epfdmNormal, FALSE);
                        CQuadWord size;
                        mapi->GetTotalSize(&size);
                        const std::wstring totalSize = PrintDiskSize(size, 0);
                        std::wstring msg = FormatStrW(LoadStrW(IDS_CONFIRM_EMAIL), expanded.c_str(), totalSize.c_str());
                        PromptResult res = gPrompter->AskYesNoWithCheckbox(LoadStrW(IDS_QUESTION), msg.c_str(),
                                                                           LoadStrW(IDS_DONTSHOWAGAINSE), &dontShow);
                        confirmed = (res.type == PromptResult::kYes);
                        Configuration.CnfrmSendEmail = !dontShow;
                    }
                    if (confirmed)
                    {
                        SimpleMAPISendMail(mapi); // own thread; handles destruction of the 'mapi' object
                    }
                    else
                        delete mapi;
                }
                else
                    delete mapi;
            }
            else
                delete mapi;
        }
        else
            TRACE_E(LOW_MEMORY);
        FilesActionInProgress = FALSE;
    }
    EndStopRefresh(); // the snooper starts again now
}

BOOL CFilesWindow::OpenFocusedInOtherPanel(BOOL activate)
{
    CFilesWindow* otherPanel = (this == MainWindow->LeftPanel) ? MainWindow->RightPanel : MainWindow->LeftPanel;
    if (otherPanel == NULL)
        return FALSE;

    // allow opening the up-dir
    //  if (FocusedIndex == 0 && FocusedIndex < Dirs->Count &&
    //      strcmp(Dirs->At(0).Name, "..") == 0) return FALSE;   // do not handle the up-dir
    if (FocusedIndex < 0 || FocusedIndex >= Files->Count + Dirs->Count)
        return FALSE; // ignore invalid index

    std::wstring buff;

    CFileData* file = (FocusedIndex < Dirs->Count) ? &Dirs->At(FocusedIndex) : &Files->At(FocusedIndex - Dirs->Count);

    if (Is(ptDisk) || Is(ptZIPArchive))
    {
        GetGeneralPath(buff);
        BOOL nethoodPath = FALSE;
        if (FocusedIndex == 0 && 0 < Dirs->Count && wcscmp(Dirs->At(0).Name, L"..") == 0 &&
            IsUNCRootPathW(buff.c_str())) // up-dir on a UNC root path => switch to Nethood
        {
            const size_t separator = buff.find(L'\\', 2);
            CPluginData* nethoodPlugin = NULL;
            std::wstring fsName;
            if (separator != std::wstring::npos && Plugins.GetFirstNethoodPluginFSName(&fsName, &nethoodPlugin))
            {
                nethoodPath = TRUE;
                const std::wstring server = buff.substr(0, separator);
                buff = fsName;
                buff += L':';
                buff += server;
            }
        }
        if (!nethoodPath)
        {
            SalPathAppendW(buff, file->Name);
        }
    }
    else if (Is(ptPluginFS) && GetPluginFS()->NotEmpty())
    {
        int isDir;
        if (FocusedIndex == 0 && 0 < Dirs->Count && wcscmp(Dirs->At(0).Name, L"..") == 0)
            isDir = 2;
        else
            isDir = (FocusedIndex < Dirs->Count ? 1 : 0);
        std::wstring fullName;
        if (GetPluginFS()->GetFullNameW(*file, isDir, fullName))
        {
            buff = GetPluginFS()->GetPluginFSName();
            buff += L':';
            buff += fullName;
        }
        else
            buff.clear();
    }

    if (!buff.empty())
    {
        int failReason;
        BOOL ret = otherPanel->ChangeDir(buff.c_str(), -1, NULL, 3 /* change-dir */, &failReason, FALSE);
        if (activate && this == MainWindow->GetActivePanel())
        {
            if (ret || (failReason == CHPPFR_SUCCESS ||
                        failReason == CHPPFR_SHORTERPATH ||
                        failReason == CHPPFR_FILENAMEFOCUSED))
            {
                // if the path changed in the other panel, activate the panel
                MainWindow->ChangePanel();
                return TRUE;
            }
        }
    }
    return FALSE;
}

void CFilesWindow::ChangePathToOtherPanelPath()
{
    CFilesWindow* panel = (this == MainWindow->LeftPanel) ? MainWindow->RightPanel : MainWindow->LeftPanel;
    if (panel == NULL)
        return;

    if (panel->Is(ptDisk))
    {
        if (sally::unicode::HasWidePathW(panel->GetPathW()))
            ChangePathToDisk(HWindow, panel->GetPathW());
        else
            ChangePathToDisk(HWindow, panel->GetPathW());
    }
    else
    {
        if (panel->Is(ptZIPArchive))
        {
            if (sally::unicode::HasWidePathW(panel->GetZIPArchive()))
                ChangePathToArchive(panel->GetZIPArchive(), panel->GetZIPPath());
            else
                ChangePathToArchive(panel->GetZIPArchive(), panel->GetZIPPath());
        }
        else
        {
            if (panel->Is(ptPluginFS))
            {
                std::wstring userPart;
                if (panel->GetPluginFS()->GetCurrentPathW(userPart))
                {
                    std::wstring path = panel->GetPluginFS()->GetPluginFSName();
                    path += L':';
                    path += userPart;
                    ChangeDir(path.c_str(), -1, NULL, 3 /*change-dir*/, NULL, FALSE);
                }
            }
        }
    }
}
