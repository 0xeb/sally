// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "dialogs.h"
#include "zip.h"
#include "pack.h"
#include "ui/IPrompter.h"
#include "common/IFileSystem.h"
#include "common/DiagnosticTextEncoding.h"
#include "common/SalGetFullName.h"
#include "common/SalPathWide.h"
#include "common/unicode/helpers.h"
#include "common/unicode/AnsiFallbackPolicy.h"
#include "common/unicode/AnsiToolPathPolicy.h"
#include "common/unicode/CopyNamePolicy.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/widepath.h"
#include "common/fsutil.h"
#include "common/IEnvironment.h"

//
// ****************************************************************************
// CFilesWindow
//

CPanelTmpEnumData::CPanelTmpEnumData()
{
    Indexes = NULL;
    CurrentIndex = 0;
    IndexesCount = 0;
    ZIPPath = NULL;
    Dirs = NULL;
    Files = NULL;
    ArchiveDir = NULL;
    WorkPathW.clear();
    EnumLastDir = NULL;
    EnumLastIndex = 0;
    EnumLastPath.clear();
    EnumTmpFileName.clear();
    DiskDirectoryTree = NULL;
    EnumLastDosPath.clear();
    EnumTmpDosFileName.clear();
    FilesCountReturnedFromWP = 0;
}

CPanelTmpEnumData::~CPanelTmpEnumData()
{
    if (DiskDirectoryTree != NULL)
        delete DiskDirectoryTree;
}

void CPanelTmpEnumData::Reset()
{
    CurrentIndex = 0;
    EnumLastDir = NULL;
    EnumLastIndex = -1;
    EnumLastPath.clear();
    EnumTmpFileName.clear();
    EnumLastDosPath.clear();
    EnumTmpDosFileName.clear();
    FilesCountReturnedFromWP = 0;
    EnumWideDirStack.clear();
    LastNameW.clear();
}

// The wide name of an item. CFileData::Name is already the exact wide form;
// the narrow NameW mirror it used to fall back from is gone.
static std::wstring WideNameOf(const CFileData* f)
{
    return std::wstring(f->Name);
}

// The accumulated wide directory path, joined from the stack. Empty at the archive root.
static std::wstring JoinWideDirs(const std::vector<std::wstring>& parts)
{
    std::wstring out;
    for (const std::wstring& part : parts)
    {
        if (!out.empty())
            out += L'\\';
        out += part;
    }
    return out;
}

static void AppendPathComponent(std::wstring& path, const wchar_t* name)
{
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
    path += name;
}

const wchar_t* _PanelSalEnumSelection(int enumFiles, const wchar_t** dosName, BOOL* isDir, CQuadWord* size,
                                   const CFileData** fileData, void* param, HWND parent, int* errorOccured)
{
    SLOW_CALL_STACK_MESSAGE2("_PanelSalEnumSelection(%d, , , , , , ,)", enumFiles);
    CPanelTmpEnumData* data = (CPanelTmpEnumData*)param;
    if (dosName != NULL)
        *dosName = NULL;
    if (isDir != NULL)
        *isDir = FALSE;
    if (size != NULL)
        *size = CQuadWord(0, 0);
    if (fileData != NULL)
        *fileData = NULL;
    data->LastNameW.clear();

    if (enumFiles == -1)
    {
        data->Reset();
        return NULL;
    }

    static wchar_t errText[1000];
    const wchar_t* curZIPPath;
    if (data->DiskDirectoryTree == NULL)
        curZIPPath = data->ZIPPath;
    else
        curZIPPath = L"";

ENUM_NEXT:

    if (data->CurrentIndex >= data->IndexesCount)
        return NULL;
    if (enumFiles == 0)
    {
        int i = data->Indexes[data->CurrentIndex++];
        BOOL localIsDir = i < data->Dirs->Count;
        if (isDir != NULL)
            *isDir = localIsDir;
        CFileData* f = &(localIsDir ? data->Dirs->At(i) : data->Files->At(i - data->Dirs->Count));
        if (localIsDir)
        {
            if (size != NULL)
                *size = data->ArchiveDir->GetDirSize(data->ZIPPath, f->Name);
        }
        else
        {
            if (size != NULL)
                *size = f->Size;
            data->FilesCountReturnedFromWP++; // just in case someone changes 'enumFiles' between 0 and 3
        }
        if (fileData != NULL)
            *fileData = f;
        data->LastNameW = WideNameOf(f);
        return f->Name;
    }
    else
    {
        if (data->EnumLastDir == NULL) // the directory needs to be "opened"
        {
            int i = data->Indexes[data->CurrentIndex];
            BOOL localIsDir = i < data->Dirs->Count;
            if (isDir != NULL)
                *isDir = localIsDir;
            CFileData* f = &(localIsDir ? data->Dirs->At(i) : data->Files->At(i - data->Dirs->Count));
            if (localIsDir)
            {
                {
                    data->EnumLastPath.assign(curZIPPath);
                    AppendPathComponent(data->EnumLastPath, f->Name);
                    data->EnumWideDirStack.assign(1, WideNameOf(f));
                    if (data->DiskDirectoryTree != NULL)
                    { // if EnumLastDosPath is used, zipPathLen will be 0
                        data->EnumLastDosPath.assign((f->DosName == NULL) ? f->Name : f->DosName);
                    }
                    if (data->DiskDirectoryTree == NULL)
                        data->EnumLastDir = data->ArchiveDir;
                    else
                        data->EnumLastDir = data->DiskDirectoryTree;
                    data->EnumLastDir = data->EnumLastDir->GetSalamanderDir(data->EnumLastPath.c_str(), TRUE);
                    if (data->EnumLastDir == NULL)
                        return NULL; // most likely the ".." directory, otherwise an unexpected error
                    data->EnumLastIndex = 0;
                    goto FIND_NEXT;
                }
            }
            else
            {
                if (size != NULL)
                {
                    *size = f->Size;
                    if (enumFiles == 3 && data->DiskDirectoryTree != NULL && (f->Attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    { // the determined size of the target file for the link must be taken from data->DiskDirectoryTree
                        CFileData const* f2 = data->DiskDirectoryTree->GetFile(data->FilesCountReturnedFromWP);
                        if (f2 != NULL && wcscmp(f2->Name, f->Name) == 0) // always true
                            *size = f2->Size;
                        else
                        {
                            TRACE_E("_PanelSalEnumSelection(): unexpected situation: file \"" << sally::diagnostic::EncodeAcpLossy(f->Name) << "\" not found or different in data->DiskDirectoryTree");
                        }
                    }
                }
                data->CurrentIndex++;
                if (data->DiskDirectoryTree != NULL)
                {
                    if (dosName != NULL)
                        *dosName = (f->DosName == NULL) ? f->Name : f->DosName;
                }
                if (fileData != NULL)
                    *fileData = f;
                data->FilesCountReturnedFromWP++;
                data->LastNameW = WideNameOf(f);
                return f->Name;
            }
        }
        else
        {
            data->EnumLastIndex++; // move to the next item

        FIND_NEXT: // find the next file in the tree

            while (1)
            {
                if (data->EnumLastDir->IsDirectory(data->EnumLastIndex)) // directory -> descend
                {
                    CFileData* f = data->EnumLastDir->GetDirEx(data->EnumLastIndex);
                    AppendPathComponent(data->EnumLastPath, f->Name);
                    data->EnumWideDirStack.push_back(WideNameOf(f));
                    if (data->DiskDirectoryTree != NULL)
                    {
                        AppendPathComponent(data->EnumLastDosPath, (f->DosName == NULL) ? f->Name : f->DosName);
                    }
                    data->EnumLastDir = data->EnumLastDir->GetSalamanderDir(data->EnumLastIndex);
                    data->EnumLastIndex = 0;
                }
                else
                {
                    if (data->EnumLastDir->IsFile(data->EnumLastIndex)) // file -> found
                    {
                        if (enumFiles == 2)
                            goto ENUM_NEXT; // no files from subdirectories, subdirectories are enough

                        CFileData* f = data->EnumLastDir->GetFileEx(data->EnumLastIndex);
                        const size_t zipPathLen = wcslen(curZIPPath);
                        const size_t relativeOffset = zipPathLen + (zipPathLen > 0 ? 1 : 0);
                        if (isDir != NULL)
                            *isDir = FALSE;
                        if (size != NULL)
                            *size = f->Size;
                        data->EnumTmpFileName = data->EnumLastPath.substr(relativeOffset);
                        AppendPathComponent(data->EnumTmpFileName, f->Name);
                        data->LastNameW = JoinWideDirs(data->EnumWideDirStack) + L'\\' + WideNameOf(f);
                        if (data->DiskDirectoryTree != NULL)
                        {
                            data->EnumTmpDosFileName.assign(data->EnumLastDosPath); // if used, zipPathLen == 0
                            AppendPathComponent(data->EnumTmpDosFileName, (f->DosName == NULL) ? f->Name : f->DosName);
                            if (dosName != NULL)
                                *dosName = data->EnumTmpDosFileName.c_str();
                        }
                        if (fileData != NULL)
                            *fileData = f;
                        return data->EnumTmpFileName.c_str();
                    }
                    else // we are at the end of a directory -> exit
                    {
                        // split the path into directory and subdirectory
                        const size_t separator = data->EnumLastPath.find_last_of(L'\\');
                        std::wstring subDir;
                        if (separator != std::wstring::npos)
                        {
                            subDir.assign(data->EnumLastPath, separator + 1, std::wstring::npos);
                            data->EnumLastPath.resize(separator);
                        }
                        else // we have definitely exited the tree
                        {
                            subDir.swap(data->EnumLastPath);
                        }
                        std::wstring subDirDos;
                        if (data->DiskDirectoryTree != NULL)
                        {
                            const size_t dosSeparator = data->EnumLastDosPath.find_last_of(L'\\');
                            if (dosSeparator != std::wstring::npos)
                            {
                                subDirDos.assign(data->EnumLastDosPath, dosSeparator + 1, std::wstring::npos);
                                data->EnumLastDosPath.resize(dosSeparator);
                            }
                            else
                                subDirDos.swap(data->EnumLastDosPath);
                        }
                        // check whether we have already exited the tree
                        if (data->EnumLastPath.size() == wcslen(curZIPPath))
                        {
                            if (fileData != NULL) // find CFileData for the subdirectory that we are exiting
                            {
                                int i = data->Indexes[data->CurrentIndex];
                                if (i < data->Dirs->Count)
                                    *fileData = &data->Dirs->At(i);
                                else
                                    TRACE_E("Unexpected situation in _PanelSalEnumSelection.");
                            }

                            data->CurrentIndex++; // we have already processed this directory
                            data->EnumLastDir = NULL;
                            data->EnumLastIndex = -1;

                            if (isDir != NULL)
                                *isDir = TRUE;
                            if (size != NULL)
                                *size = CQuadWord(0, 0);
                            if (data->DiskDirectoryTree != NULL)
                            {
                                if (dosName != NULL)
                                {
                                    data->EnumTmpDosFileName = subDirDos;
                                    *dosName = data->EnumTmpDosFileName.c_str();
                                }
                            }
                            // Leaving the tree entirely: 'subDir' is the single component
                            // just below the archive root, so the stack holds exactly it.
                            if (!data->EnumWideDirStack.empty())
                                data->LastNameW = data->EnumWideDirStack.back();
                            data->EnumWideDirStack.clear();
                            data->EnumTmpFileName = subDir;
                            return data->EnumTmpFileName.c_str(); // return the directory when exiting
                        }
                        // finish exiting and move forward
                        if (data->DiskDirectoryTree == NULL)
                            data->EnumLastDir = data->ArchiveDir;
                        else
                            data->EnumLastDir = data->DiskDirectoryTree;
                        data->EnumLastDir = data->EnumLastDir->GetSalamanderDir(data->EnumLastPath.c_str(), TRUE);
                        data->EnumLastIndex = data->EnumLastDir->GetIndex(subDir.c_str());

                        if (fileData != NULL) // find CFileData for the subdirectory that we are exiting
                        {
                            *fileData = data->EnumLastDir->GetDirEx(data->EnumLastIndex);
                        }

                        if (isDir != NULL)
                            *isDir = TRUE;
                        if (size != NULL)
                            *size = CQuadWord(0, 0);
                        const size_t zipPathLen = wcslen(curZIPPath);
                        const size_t relativeOffset = zipPathLen + (zipPathLen > 0 ? 1 : 0);
                        data->EnumTmpFileName = data->EnumLastPath.substr(relativeOffset);
                        AppendPathComponent(data->EnumTmpFileName, subDir.c_str());
                        // EnumLastPath was truncated above to the parent, and 'subDir' is
                        // the component being left - so pop that component off the stack
                        // and compose the same way.
                        if (!data->EnumWideDirStack.empty())
                        {
                            const std::wstring exiting = data->EnumWideDirStack.back();
                            data->EnumWideDirStack.pop_back();
                            data->LastNameW = JoinWideDirs(data->EnumWideDirStack) + L'\\' + exiting;
                        }
                        if (data->DiskDirectoryTree != NULL)
                        {
                            data->EnumTmpDosFileName.assign(data->EnumLastDosPath); // if used, zipPathLen == 0
                            AppendPathComponent(data->EnumTmpDosFileName, subDirDos.c_str());
                            if (dosName != NULL)
                                *dosName = data->EnumTmpDosFileName.c_str();
                        }
                        return data->EnumTmpFileName.c_str(); // return the directory when exiting
                    }
                }
            }
        }
    }
    return NULL;
}

const wchar_t* WINAPI PanelSalEnumSelection(HWND parent, int enumFiles, BOOL* isDir, CQuadWord* size,
                                         const CFileData** fileData, void* param, int* errorOccured)
{
    CALL_STACK_MESSAGE_NONE
    if (errorOccured != NULL)
        *errorOccured = SALENUM_SUCCESS;
    if (enumFiles == 3)
    {
        TRACE_E("PanelSalEnumSelection(): invalid parameter: enumFiles==3, changing to 1");
        enumFiles = 1;
    }
    return _PanelSalEnumSelection(enumFiles, NULL, isDir, size, fileData, param, parent, errorOccured);
}

void CFilesWindow::UnpackZIPArchive(CFilesWindow* target, BOOL deleteOp, const wchar_t* tgtPath)
{
    CALL_STACK_MESSAGE3("CFilesWindow::UnpackZIPArchive(, %d, %ls)", deleteOp, tgtPath);
    if (Files->Count + Dirs->Count == 0)
        return;

    // restore DefaultDir
    MainWindow->UpdateDefaultDir(MainWindow->GetActivePanel() == this);

    BeginStopRefresh(); // the snooper takes a break

    //---  obtain the files and directories to work with
    std::wstring path;
    // wide - CMessageBox::DialogProc reads Text.GetW() unconditionally
    // (no IsWide() fallback), so a narrow-only CTruncatedString here showed an EMPTY
    // delete-confirmation dialog body on every DeleteFromZIPArchive - not just a
    // non-ASCII-name defect, this was blank for every deletion. subject/expanded were
    // fixed scratch buffers used only to build 'str'; widened in place.
    std::wstring pathW;
    std::wstring expandedW;
    CPanelTmpEnumData data;
    BOOL subDir;
    if (Dirs->Count > 0)
        subDir = (wcscmp(Dirs->At(0).Name, L"..") == 0);
    else
        subDir = FALSE;
    data.IndexesCount = GetSelCount();
    if (data.IndexesCount > 1) // valid selection
    {
        int files = 0; // number of selected files
        data.Indexes = new int[data.IndexesCount];
        if (data.Indexes == NULL)
        {
            TRACE_E(LOW_MEMORY);
            EndStopRefresh(); // the snooper resumes now
            return;
        }
        else
        {
            GetSelItems(data.IndexesCount, data.Indexes);
            int i = data.IndexesCount;
            while (i--)
            {
                BOOL isDir = data.Indexes[i] < Dirs->Count;
                CFileData* f = isDir ? &Dirs->At(data.Indexes[i]) : &Files->At(data.Indexes[i] - Dirs->Count);
                if (!isDir)
                    files++;
            }
        }
        // build the subject for the dialog
        expandedW = ExpandPluralFilesDirsTextW(files, data.IndexesCount - files, epfdmNormal, FALSE);
    }
    else // take the selected file or directory
    {
        int index;
        if (data.IndexesCount == 0)
            index = GetCaretIndex();
        else
            GetSelItems(1, &index);

        if (subDir && index == 0)
        {
            EndStopRefresh(); // the snooper resumes now
            return;           // nothing to do
        }
        else
        {
            data.Indexes = new int[1];
            if (data.Indexes == NULL)
            {
                TRACE_E(LOW_MEMORY);
                EndStopRefresh(); // the snooper resumes now
                return;
            }
            else
            {
                data.Indexes[0] = index;
                data.IndexesCount = 1;
                // build the subject for the dialog
                BOOL isDir = index < Dirs->Count;
                CFileData* f = isDir ? &Dirs->At(index) : &Files->At(index - Dirs->Count);
                pathW = AlterFileNameW(f->Name,
                                       Configuration.FileNameFormat, 0, isDir != FALSE);
                expandedW = LoadStrW(isDir ? IDS_QUESTION_DIRECTORY : IDS_QUESTION_FILE);
            }
        }
    }
    CTruncatedString str;
    // same two-stage %s nesting as CFilesWindow::Pack's identical fix -
    // IDS_CONFIRM_DELETEFROMARCHIVE/IDS_COPYFROMARCHIVETO's own %s is filled with
    // expandedW, whose own unfilled %s (single-item case) is then filled by SetW's
    // internal substitution with the actual item name.
    const std::wstring subjectW = FormatStrW(LoadStrW(deleteOp ? IDS_CONFIRM_DELETEFROMARCHIVE : IDS_COPYFROMARCHIVETO), expandedW.c_str());
    str.SetW(subjectW.c_str(), data.IndexesCount > 1 ? NULL : pathW.c_str());

    data.CurrentIndex = 0;
    data.ZIPPath = GetZIPPath();
    data.Dirs = Dirs;
    data.Files = Files;
    data.ArchiveDir = GetArchiveDir();
    data.EnumLastDir = NULL;
    data.EnumLastIndex = -1;

    std::wstring changesRoot; // directory from which changes on disk are taken into account

    if (!deleteOp) // copy
    {
        //---  obtain the target directory
        std::wstring pathW;
        if (target != NULL && target->Is(ptDisk))
        {
            pathW = target->GetPathW();
            path = target->GetPathW();

            target->UserWorkedOnThisPath = TRUE; // default action = operate with the path in the target panel
        }
        else
            path.clear();

        CCopyMoveDialog dlg(HWindow, pathW, LoadStrW(IDS_UNPACKCOPY), &str, IDD_COPYDIALOG,
                            Configuration.CopyHistory, COPY_HISTORY_SIZE, TRUE);

    _DLG_AGAIN:

        if (tgtPath != NULL || dlg.Execute() == IDOK)
        {
            if (tgtPath != NULL)
            {
                pathW = tgtPath;
            }
            path = pathW;
            UpdateWindow(MainWindow->HWindow);
            //---  for disk paths, convert '/' to '\\' and remove duplicate '\\'
            if (!IsPluginFSPath(path.c_str()) &&
                (path[0] != 0 && path[1] == ':' ||                                             // paths like X:...
                 (path[0] == '/' || path[0] == '\\') && (path[1] == '/' || path[1] == '\\') || // UNC paths
                 Is(ptDisk) || Is(ptZIPArchive)))                                              // disk+archive relative paths
            {                                                                                  // this is a disk path (absolute or relative) - convert all '/' to '\\' and remove duplicate '\\'
                SlashesToBackslashesAndRemoveDups(path);
                path.resize(wcslen(path.c_str()));
            }
            //---  adjust the entered path -> convert to absolute, without '.' and '..'

            int len = (int)path.length();
            BOOL backslashAtEnd = (len > 0 && path[len - 1] == '\\'); // path ends with a backslash -> must be a directory
            BOOL mustBePath = (len == 2 && towlower(path[0]) >= L'a' && towlower(path[0]) <= L'z' &&
                               path[1] == ':'); // a path like "c:" must remain a directory after expansion (not a file)

            int pathType;
            BOOL pathIsDir;
            wchar_t* secondPart;
            if (ParsePathW(path, pathType, pathIsDir, secondPart, LoadStrW(IDS_ERRORCOPY), NULL, NULL))
            {
                // instead of a 'switch', use 'if' so that 'break' and 'continue' work correctly
                if (pathType == PATH_TYPE_WINDOWS) // Windows path (disk + UNC)
                {
                    std::wstring newDirs; // if a directory is being created for the operation, remember its name (so we can delete it in case of an error)

                    if (pathIsDir) // the existing part of the path is a directory
                    {
                        if (*secondPart != 0) // the path contains a segment that does not exist
                        {
                            if (!backslashAtEnd && !mustBePath) // the new path must end with a backslash; otherwise it is an operation mask (unsupported)
                            {
                                gPrompter->ShowError(LoadStrW(IDS_ERRORCOPY), LoadStrW(IDS_UNPACK_OPMASKSNOTSUP));
                                if (tgtPath != NULL)
                                {
                                    UpdateWindow(MainWindow->HWindow);
                                    delete[] (data.Indexes);
                                    EndStopRefresh();
                                    return;
                                }
                                pathW = path;
                                goto _DLG_AGAIN;
                            }

                            // create the new directories
                            newDirs = path;

                            if (Configuration.CnfrmCreatePath) // ask whether the path should be created
                            {
                                bool dontShow = false;
                                std::wstring msg = FormatStrW(LoadStrW(IDS_MOVECOPY_CREATEPATH), newDirs.c_str());
                                PromptResult res = gPrompter->AskYesNoWithCheckbox(LoadStrW(IDS_UNPACKCOPY), msg.c_str(),
                                                                                   LoadStrW(IDS_MOVECOPY_CREATEPATH_CNFRM), &dontShow);
                                BOOL cont = (res.type != PromptResult::kYes);
                                Configuration.CnfrmCreatePath = !dontShow;
                                if (cont)
                                {
                                    SalPathAddBackslashW(path);
                                    if (tgtPath != NULL)
                                    {
                                        UpdateWindow(MainWindow->HWindow);
                                        delete[] (data.Indexes);
                                        EndStopRefresh();
                                        return;
                                    }
                                    pathW = path;
                                    goto _DLG_AGAIN;
                                }
                            }

                            BOOL ok = TRUE;
                            const size_t existingLength = static_cast<size_t>(secondPart - path.data());
                            size_t componentStart = existingLength;
                            size_t firstSlash = std::wstring::npos;
                            while (1)
                            {
                                const size_t slash = newDirs.find(L'\\', componentStart);
                                const size_t componentEnd = slash == std::wstring::npos ? newDirs.length() : slash;
                                BOOL invalidPath = componentStart < newDirs.length() && newDirs[componentStart] <= L' ';
                                if (componentEnd > componentStart &&
                                    (newDirs[componentEnd - 1] <= L' ' || newDirs[componentEnd - 1] == L'.'))
                                {
                                    invalidPath = TRUE;
                                }
                                if (slash != std::wstring::npos && firstSlash == std::wstring::npos)
                                {
                                    firstSlash = slash;
                                }
                                const std::wstring directory = newDirs.substr(0, componentEnd);
                                if (invalidPath || !SalLPCreateDirectory(directory.c_str(), NULL))
                                {
                                    DWORD lastErr = invalidPath ? ERROR_INVALID_NAME : GetLastError();
                                    // ERROR_ALREADY_EXISTS is not a failure - the directory is there, which is what we want
                                    if (lastErr != ERROR_ALREADY_EXISTS)
                                    {
                                        std::wstring msg = FormatStrW(LoadStrW(IDS_CREATEDIRFAILED), directory.c_str());
                                        gPrompter->ShowError(LoadStrW(IDS_ERRORCOPY), msg.c_str());
                                        ok = FALSE;
                                        break;
                                    }
                                }
                                if (slash == std::wstring::npos)
                                    break; // that was the last '\\'
                                componentStart = slash + 1;
                            }

                            // determine the original path (from which new directories were created)
                            changesRoot.assign(path.data(), existingLength);

                            if (!ok)
                            {
                                //---  refresh directories that are not automatically refreshed
                                // if directory creation failed, report immediately changes (the user may
                                // choose a completely different path next time); the new path is kept (almost dead code)
                                MainWindow->PostChangeOnPathNotificationW(changesRoot.c_str(), TRUE);

                                SalPathAddBackslashW(path);
                                if (tgtPath != NULL)
                                {
                                    UpdateWindow(MainWindow->HWindow);
                                    delete[] (data.Indexes);
                                    EndStopRefresh();
                                    return;
                                }
                                pathW = path;
                                goto _DLG_AGAIN;
                            }
                            if (firstSlash != std::wstring::npos)
                                newDirs.resize(firstSlash); // remember the first created directory for rollback
                        }
                    }
                    else // overwrite file - 'secondPart' points to the filename in 'path'
                    {
                        gPrompter->ShowError(LoadStrW(IDS_ERRORCOPY), LoadStrW(IDS_UNPACK_OPMASKSNOTSUP));
                        if (backslashAtEnd || mustBePath)
                            SalPathAddBackslashW(path);
                        if (tgtPath != NULL)
                        {
                            UpdateWindow(MainWindow->HWindow);
                            delete[] (data.Indexes);
                            EndStopRefresh();
                            return;
                        }
                        pathW = path;
                        goto _DLG_AGAIN;
                    }

                    // if no new directories are created, changes start at the target path
                    if (changesRoot.empty())
                        changesRoot = path;

                    //---  actual unpacking
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                    if (PackUncompress(MainWindow->HWindow, this, GetZIPArchive(), PluginData.GetInterface(),
                                       path.c_str(), GetZIPPath(), PanelSalEnumSelection, &data))
                    {                        // unpacking succeeded
                        if (tgtPath == NULL) // if it is not drag&drop (selection is not cleared there)
                        {
                            SetSel(FALSE, -1, TRUE);                        // explicit redraw
                            PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
                        }
                    }
                    else
                    {
                        if (!newDirs.empty())
                            RemoveEmptyDirsW(newDirs.c_str());
                    }
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

                    if (GetForegroundWindow() == MainWindow->HWindow) // for unknown reasons focus disappears from the panel when dragging to Explorer; return it
                        RestoreFocusInSourcePanel();
                }
                else
                {
                    gPrompter->ShowError(LoadStrW(IDS_ERRORCOPY), LoadStrW(IDS_UNPACK_ONLYDISK));
                    if (pathType == PATH_TYPE_ARCHIVE && (backslashAtEnd || mustBePath))
                    {
                        SalPathAddBackslashW(path);
                    }
                    if (tgtPath != NULL)
                    {
                        UpdateWindow(MainWindow->HWindow);
                        delete[] (data.Indexes);
                        EndStopRefresh();
                        return;
                    }
                    pathW = path;
                    goto _DLG_AGAIN;
                }
            }
            else
            {
                if (tgtPath != NULL)
                {
                    UpdateWindow(MainWindow->HWindow);
                    delete[] (data.Indexes);
                    EndStopRefresh();
                    return;
                }
                pathW = path;
                goto _DLG_AGAIN;
            }

            //---  refresh directories that are not automatically refreshed
            // changes on the target path and its subdirectories (creating new directories and unpacking
            // files/directories)
            MainWindow->PostChangeOnPathNotificationW(changesRoot.c_str(), TRUE);
            // change in the directory containing the archive (should not occur during unpack, but refresh just in case it does)
            MainWindow->PostChangeOnPathNotificationW(GetPathW(), FALSE);
        }
    }
    else // delete
    {
        //---  ask whether the user is sure they want to delete
        HICON hIcon = (HICON)HANDLES(LoadImage(Shell32DLL, MAKEINTRESOURCE(WindowsVistaAndLater ? 16777 : 161), // delete icon
                                               IMAGE_ICON, 32, 32, IconLRFlags));
        if (!Configuration.CnfrmFileDirDel ||
            CMessageBox(HWindow, MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_SILENT,
                        LoadStrW(IDS_CONFIRM_DELETE_TITLE), &str, NULL,
                        NULL, hIcon, 0, NULL, NULL, NULL, NULL)
                    .Execute() == IDYES)
        {
            UpdateWindow(MainWindow->HWindow);
            //---  finding non-empty directories - ask about deleting them if needed
            BOOL cancel = FALSE;
            if (Configuration.CnfrmNEDirDel)
            {
                int i;
                for (i = 0; i < data.IndexesCount; i++)
                {
                    if (data.Indexes[i] < Dirs->Count)
                    {
                        int dirsCount = 0;
                        int filesCount = 0;
                        GetArchiveDir()->GetDirSize(GetZIPPath(), Dirs->At(data.Indexes[i]).Name,
                                                    &dirsCount, &filesCount);
                        if (dirsCount + filesCount > 0)
                        {
                            // Built wide throughout via the already-synced
                            // authoritative wide archive/path/name accessors, instead of the old
                            // narrow fixed scratch value blanket-converted at the point of use - an
                            // archive path, in-archive subpath, or directory name outside
                            // CP_ACP would otherwise show a mangled path in this delete
                            // confirmation prompt.
                            std::wstring nameW = GetZIPArchive();
                            if (GetZIPPath()[0] != 0)
                            {
                                if (GetZIPPath()[0] != L'\\')
                                    nameW += L'\\';
                                nameW += GetZIPPath();
                            }
                            nameW += L'\\';
                            nameW += WideNameOf(&Dirs->At(data.Indexes[i]));

                            std::wstring msg = FormatStrW(LoadStrW(IDS_NONEMPTYDIRDELCONFIRM), nameW.c_str());
                            PromptResult res = gPrompter->AskYesNoCancel(LoadStrW(IDS_QUESTION), msg.c_str());
                            if (res.type == PromptResult::kCancel)
                            {
                                cancel = TRUE;
                                break;
                            }
                            if (res.type == PromptResult::kNo)
                            {
                                memmove(data.Indexes + i, data.Indexes + i + 1, (data.IndexesCount - i - 1) * sizeof(int));
                                data.IndexesCount--;
                                i--;
                            }
                        }
                    }
                }
                if (data.IndexesCount == 0)
                    cancel = TRUE;
            }
            //---  actual deletion
            if (!cancel)
            {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                if (PackDelFromArc(MainWindow->HWindow, this, GetZIPArchive(), PluginData.GetInterface(),
                                   GetZIPPath(), PanelSalEnumSelection, &data))
                {                                                   // deletion succeeded
                    SetSel(FALSE, -1, TRUE);                        // explicit redraw
                    PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
                }
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

                //---  refresh directories that are not automatically refreshed
                // change in the directory containing the archive
                MainWindow->PostChangeOnPathNotificationW(GetPathW(), FALSE);
            }
        }
        HANDLES(DestroyIcon(hIcon));
    }

    UpdateWindow(MainWindow->HWindow);
    delete[] (data.Indexes);

    //---  if any Salamander window is active, suspend mode ends
    EndStopRefresh();
}

void CFilesWindow::DeleteFromZIPArchive()
{
    CALL_STACK_MESSAGE1("CFilesWindow::DeleteFromZIPArchive()");
    UnpackZIPArchive(NULL, TRUE); // almost the same operation
}

BOOL _ReadDirectoryTree(HWND parent, const std::wstring& basePath, const wchar_t* name, CSalamanderDirectory* dir,
                        int* errorOccured, BOOL getLinkTgtFileSize, BOOL* errGetFileSizeOfLnkTgtIgnAll,
                        int* containsDirLinks, std::wstring* linkNameW)
{
    CALL_STACK_MESSAGE4("_ReadDirectoryTree(, %ls, %ls, , , %d, , ,)", basePath.c_str(), name, getLinkTgtFileSize);
    std::wstring path = basePath;
    SalPathAppendW(path, name);
    std::wstring pattern = path;
    SalPathAppendW(pattern, L"*");

    WIN32_FIND_DATAW file;
    HANDLE find = SalFindFirstFileHW(pattern.c_str(), &file);
    if (find == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES)
        {
            if (errorOccured != NULL)
                *errorOccured = SALENUM_ERROR;
            if (parent != NULL &&
                gPrompter->ConfirmError(LoadStrW(IDS_ERRORTITLE),
                    (path + L": " + GetErrorTextOwned(err).c_str()).c_str()).type == PromptResult::kCancel)
            {
                if (errorOccured != NULL)
                    *errorOccured = SALENUM_CANCEL;
                return FALSE; // user wants to quit
            }
        }
        return TRUE; // user wants to continue
    }
    else
    {
        BOOL ok = TRUE;
        CFileData newF; // we no longer work with these items
        if (dir != NULL)
        {
            newF.PluginData = -1; // -1 is arbitrary, ignored
            newF.Association = 0;
            newF.Selected = 0;
            newF.Shared = 0;
            newF.Archive = 0;
            newF.SizeValid = 0;
            newF.Dirty = 0; // unnecessary, just to keep structure consistent
            newF.CutToClip = 0;
            newF.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
            newF.IconOverlayDone = 0;
        }
        else
            memset(&newF, 0, sizeof(newF));
        BOOL testFindNextErr = TRUE;

        do
        {
            if (file.cFileName[0] == 0 ||
                file.cFileName[0] == L'.' &&
                    (file.cFileName[1] == 0 || (file.cFileName[1] == L'.' && file.cFileName[2] == 0)))
                continue; // "." a ".."

            static DWORD lastBreakCheck = 0;
            if (containsDirLinks != NULL && GetTickCount() - lastBreakCheck > 200)
            {
                lastBreakCheck = GetTickCount();
                if (UserWantsToCancelSafeWaitWindow())
                {
                    *containsDirLinks = 2; // after interruption simulate an error to ensure the search immediately ends
                    ok = FALSE;
                    testFindNextErr = FALSE;
                    break;
                }
            }

            BOOL cancel = FALSE;
            if (dir != NULL)
            {
                newF.Size = CQuadWord(file.nFileSizeLow, file.nFileSizeHigh);

                if (getLinkTgtFileSize &&
                    (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&   // it's a file
                    (file.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) // it's a link
                {                                                                // for a symlink determine the target file size
                    CQuadWord size;
                    std::wstring linkPath = path;
                    SalPathAppendW(linkPath, file.cFileName);
                    if (GetLinkTgtFileSize(parent, linkPath.c_str(), NULL, &size, &cancel, errGetFileSizeOfLnkTgtIgnAll))
                        newF.Size = size;
                    else if (cancel && errorOccured != NULL)
                        *errorOccured = SALENUM_CANCEL;
                }

                newF.Name = !cancel ? DupStr(file.cFileName) : NULL;
                newF.DosName = NULL;
                if (cancel || newF.Name == NULL)
                {
                    ok = FALSE;
                    testFindNextErr = FALSE;
                    break;
                }
                newF.NameLen = static_cast<DWORD>(wcslen(newF.Name)); // WIN32_FIND_DATA name is DWORD-bounded
                if (!Configuration.SortDirsByExt && (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) // directory, so it is certainly a disk
                {
                    newF.Ext = newF.Name + newF.NameLen; // directories have no extensions
                }
                else
                {
                    newF.Ext = wcsrchr(newF.Name, L'.');
                    if (newF.Ext == NULL)
                        newF.Ext = newF.Name + newF.NameLen; // ".cvspass" is treated as an extension in Windows ...
                                                             //        if (newF.Ext == NULL || newF.Ext == newF.Name) newF.Ext = newF.Name + newF.NameLen;
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

                newF.Attr = file.dwFileAttributes;
                newF.LastWrite = file.ftLastWriteTime;
                newF.Hidden = newF.Attr & FILE_ATTRIBUTE_HIDDEN ? 1 : 0;
                newF.IsOffline = newF.Attr & FILE_ATTRIBUTE_OFFLINE ? 1 : 0;
            }

            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) // directory, so it is certainly a disk
            {
                CSalamanderDirectory* salDir = NULL;
                if (dir != NULL)
                {
                    newF.IsLink = (newF.Attr & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0; // volume mount point or junction point = show the directory with a link overlay
                    BOOL addDirOK = dir->AddDir(L"", newF, NULL);
                    if (addDirOK)
                        salDir = dir->GetSalamanderDir(newF.Name, FALSE); // allocate a sal-dir for the record
                    else
                    {
                        free(newF.Name);
                        if (newF.DosName != NULL)
                            free(newF.DosName);
                    }
                    if (salDir == NULL)
                    {
                        ok = FALSE;
                        testFindNextErr = FALSE;
                        break;
                    }
                }
                else // we are only looking for the first directory link
                {
                    if ((file.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                    {
                        *containsDirLinks = 1; // after finding one simulate an error to end the search immediately
                        if (linkNameW != NULL)
                        {
                            *linkNameW = path;
                            SalPathAppendW(*linkNameW, file.cFileName);
                        }
                        ok = FALSE;
                        testFindNextErr = FALSE;
                        break;
                    }
                }
                if (!_ReadDirectoryTree(parent, path, file.cFileName, salDir, errorOccured, getLinkTgtFileSize,
                                        errGetFileSizeOfLnkTgtIgnAll, containsDirLinks, linkNameW))
                {
                    ok = FALSE;
                    testFindNextErr = FALSE;
                    break;
                }
            }
            else // file
            {
                if (dir != NULL)
                {
                    if (newF.Attr & FILE_ATTRIBUTE_REPARSE_POINT)
                        newF.IsLink = 1; // if the file is a reparse point (maybe impossible) display it with a link overlay
                    else
                        newF.IsLink = IsFileLink(newF.Ext);

                    if (!dir->AddFile(L"", newF, NULL))
                    {
                        free(newF.Name);
                        if (newF.DosName != NULL)
                            free(newF.DosName);
                        ok = FALSE;
                        testFindNextErr = FALSE;
                        break;
                    }
                }
            }
        } while (SalLPFindNextFile(find, &file));
        DWORD err = GetLastError();
        SalLPFindClose(find);

        if (testFindNextErr && err != ERROR_NO_MORE_FILES)
        {
            if (errorOccured != NULL)
                *errorOccured = SALENUM_ERROR;
            if (parent != NULL &&
                gPrompter->ConfirmError(LoadStrW(IDS_ERRORTITLE),
                    (path + L": " + GetErrorTextOwned(err).c_str()).c_str()).type == PromptResult::kCancel)
            {
                if (errorOccured != NULL)
                    *errorOccured = SALENUM_CANCEL;
                return FALSE; // user wants to quit
            }
        }

        if (!ok)
        {
            if (errorOccured != NULL && *errorOccured == SALENUM_SUCCESS)
                *errorOccured = SALENUM_ERROR;
            return FALSE;
        }
    }
    return TRUE;
}

CSalamanderDirectory* ReadDirectoryTree(HWND parent, CPanelTmpEnumData* data, int* errorOccured,
                                        BOOL getLinkTgtFileSize, int* containsDirLinks, std::wstring* linkNameW)
{
    CALL_STACK_MESSAGE2("ReadDirectoryTree(, , , %d, ,)", getLinkTgtFileSize);
    if (errorOccured != NULL)
        *errorOccured = SALENUM_SUCCESS;
    if (containsDirLinks != NULL)
        *containsDirLinks = 0;
    BOOL cancel = FALSE;
    if (data->CurrentIndex >= data->IndexesCount || data->WorkPathW.empty())
    {
        TRACE_E("Unexpected situation in ReadDirectoryTree().");
        if (errorOccured != NULL)
            *errorOccured = SALENUM_ERROR;
        return NULL; // nothing to do
    }

    BOOL errGetFileSizeOfLnkTgtIgnAll = parent == NULL; // silent mode = do not show an error, return success

    CSalamanderDirectory* dir = containsDirLinks == NULL ? new CSalamanderDirectory(TRUE) : NULL;
    if (dir == NULL && containsDirLinks == NULL)
    {
        if (errorOccured != NULL)
            *errorOccured = SALENUM_ERROR;
        return NULL; // out of memory
    }

    int index = data->CurrentIndex;
    CFileData newF;
    if (dir != NULL)
    {
        newF.PluginData = -1; // -1 is arbitrary, ignored
        newF.Association = 0;
        newF.Selected = 0;
        newF.Shared = 0;
        newF.Archive = 0;
        newF.SizeValid = 0;
        newF.Dirty = 0; // unnecessary, just to keep structure consistent
        newF.CutToClip = 0;
        newF.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
        newF.IconOverlayDone = 0;
    }
    else
        memset(&newF, 0, sizeof(newF));
    while (index < data->IndexesCount)
    {
        int i = data->Indexes[index++];
        BOOL isDir = i < data->Dirs->Count;
        CFileData* f = &(isDir ? data->Dirs->At(i) : data->Files->At(i - data->Dirs->Count));
        // skip ".." as a precaution (there was a bug report in "1.6 beta 5" about this; unclear how it could occur here)
        if (f->Name[0] == L'.' && f->Name[1] == L'.' && f->Name[2] == 0)
            continue;

        if (dir != NULL)
        {
            newF.Name = DupStr(f->Name);
            newF.DosName = NULL;
            if (newF.Name == NULL)
                goto RETURN_ERROR;
            if (f->DosName != NULL)
            {
                newF.DosName = DupStr(f->DosName);
                if (newF.DosName == NULL)
                    goto RETURN_ERROR;
            }
            newF.NameLen = f->NameLen;
            newF.Ext = newF.Name + (f->Ext - f->Name);
            newF.Size = f->Size;
            newF.Attr = f->Attr;
            newF.LastWrite = f->LastWrite;
            newF.Hidden = f->Hidden;
            newF.IsLink = f->IsLink;
            newF.IsOffline = f->IsOffline;
        }

        if (isDir) // directory
        {
            CSalamanderDirectory* salDir = NULL;
            if (dir != NULL)
            {
                BOOL addDirOK = dir->AddDir(L"", newF, NULL);
                if (addDirOK)
                {
                    newF.Name = NULL; // already in dir, must not call free() on it - in case of an error below
                    newF.DosName = NULL;
                    salDir = dir->GetSalamanderDir(f->Name, FALSE); // allocate a sal-dir for the record
                }
                if (salDir == NULL)
                    goto RETURN_ERROR;
            }
            else
            {
                if ((f->Attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0) // directory link found, stop...
                {
                    *containsDirLinks = 1;
                    if (linkNameW != NULL)
                    {
                        *linkNameW = sally::unicode::TrimTrailingPathSeparatorsW(data->WorkPathW);
                        SalPathAppendW(*linkNameW, f->Name);
                    }
                    break;
                }
            }

            if (!_ReadDirectoryTree(parent, data->WorkPathW, f->Name, salDir, errorOccured, getLinkTgtFileSize,
                                    &errGetFileSizeOfLnkTgtIgnAll, containsDirLinks, linkNameW))
            {
                goto RETURN_ERROR;
            }
        }
        else // file
        {
            if (dir != NULL)
            {
                if (getLinkTgtFileSize && (newF.Attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                { // for a symlink determine the target file size
                    CQuadWord size;
                    std::wstring linkPathW = data->WorkPathW;
                    SalPathAppendW(linkPathW, newF.Name);
                    if (GetLinkTgtFileSize(parent, linkPathW.c_str(), NULL, &size, &cancel, &errGetFileSizeOfLnkTgtIgnAll))
                        newF.Size = size;
                    else
                    {
                        if (cancel && errorOccured != NULL)
                            *errorOccured = SALENUM_CANCEL;
                    }
                }
                if (cancel || !dir->AddFile(L"", newF, NULL))
                {
                RETURN_ERROR:

                    if (errorOccured != NULL && *errorOccured == SALENUM_SUCCESS)
                        *errorOccured = SALENUM_ERROR;
                    if (newF.Name != NULL)
                        free(newF.Name);
                    if (newF.DosName != NULL)
                        free(newF.DosName);
                    if (dir != NULL)
                        delete dir;
                    return NULL;
                }
            }
        }
    }
    return dir;
}

const wchar_t* WINAPI PanelEnumLastNameW(void* param)
{
    CPanelTmpEnumData* data = (CPanelTmpEnumData*)param;
    if (data == NULL || data->LastNameW.empty())
        return NULL;
    return data->LastNameW.c_str();
}

const wchar_t* WINAPI PanelEnumDiskSelection(HWND parent, int enumFiles, const wchar_t** dosName, BOOL* isDir,
                                          CQuadWord* size, DWORD* attr, FILETIME* lastWrite, void* param,
                                          int* errorOccured)
{
    CALL_STACK_MESSAGE_NONE
    CPanelTmpEnumData* data = (CPanelTmpEnumData*)param;
    if (errorOccured != NULL)
        *errorOccured = SALENUM_SUCCESS;

    if (enumFiles == -1)
    {
        if (dosName != NULL)
            *dosName = NULL;
        if (isDir != NULL)
            *isDir = FALSE;
        if (size != NULL)
            *size = CQuadWord(0, 0);
        if (attr != NULL)
            *attr = 0;
        if (lastWrite != NULL)
            memset(lastWrite, 0, sizeof(FILETIME));
        data->Reset();
        return NULL;
    }

    if (enumFiles > 0)
    {
        if (data->DiskDirectoryTree == NULL)
        {
            data->DiskDirectoryTree = ReadDirectoryTree(parent, data, errorOccured, enumFiles == 3, NULL, NULL);
            if (data->DiskDirectoryTree == NULL)
                return NULL; // error, stop
        }
        const CFileData* f = NULL;
        const wchar_t* ret = _PanelSalEnumSelection(enumFiles, dosName, isDir, size, &f, data, parent, errorOccured);
        if (ret != NULL)
        {
            if (f != NULL)
            {
                if (attr != NULL)
                    *attr = f->Attr;
                if (lastWrite != NULL)
                    *lastWrite = f->LastWrite;
            }
            else
            {
                if (attr != NULL)
                    *attr = 0;
                if (lastWrite != NULL)
                    memset(lastWrite, 0, sizeof(FILETIME));
            }
        }
        return ret;
    }
    else
    {
        if (data->CurrentIndex >= data->IndexesCount)
            return NULL;
        int i = data->Indexes[data->CurrentIndex++];
        if (isDir != NULL)
            *isDir = i < data->Dirs->Count;
        CFileData* f = &(i < data->Dirs->Count ? data->Dirs->At(i) : data->Files->At(i - data->Dirs->Count));
        if (dosName != NULL)
            *dosName = (f->DosName == NULL) ? f->Name : f->DosName;
        if (size != NULL)
            *size = f->Size;
        if (attr != NULL)
            *attr = f->Attr;
        if (lastWrite != NULL)
            *lastWrite = f->LastWrite;
        return f->Name;
    }
}

void CFilesWindow::Pack(CFilesWindow* target, int pluginIndex, const wchar_t* pluginName, int delFilesAfterPacking)
{
    CALL_STACK_MESSAGE4("CFilesWindow::Pack(, %d, %ls, %d)", pluginIndex, pluginName, delFilesAfterPacking);
    if (Files->Count + Dirs->Count == 0)
        return;

    // restore DefaultDir
    MainWindow->UpdateDefaultDir(MainWindow->GetActivePanel() == this);

    BeginStopRefresh(); // the snooper takes a break

    //---  obtain the files and directories to work with
    std::wstring nameByItemLeafW;
    // wide - the Pack dialog's Subject text; subject/expanded were narrow
    // fixed scratch buffers, used nowhere else in this function, so widened in place.
    std::wstring expandedW;
    BOOL nameByItem;
    CPanelTmpEnumData data;
    BOOL subDir;
    if (Dirs->Count > 0)
        subDir = (wcscmp(Dirs->At(0).Name, L"..") == 0);
    else
        subDir = FALSE;
    data.IndexesCount = GetSelCount();
    int files = 0;             // number of selected files
    if (data.IndexesCount > 1) // valid selection
    {
        nameByItem = FALSE;
        data.Indexes = new int[data.IndexesCount];
        if (data.Indexes == NULL)
        {
            TRACE_E(LOW_MEMORY);
            EndStopRefresh(); // the snooper resumes now
            return;
        }
        else
        {
            GetSelItems(data.IndexesCount, data.Indexes);
            int i = data.IndexesCount;
            while (i--)
            {
                BOOL isDir = data.Indexes[i] < Dirs->Count;
                CFileData* f = isDir ? &Dirs->At(data.Indexes[i]) : &Files->At(data.Indexes[i] - Dirs->Count);
                if (!isDir)
                    files++;
            }
        }
        // build the subject for the dialog
        expandedW = ExpandPluralFilesDirsTextW(files, data.IndexesCount - files, epfdmNormal, FALSE);
    }
    else // take the selected file or directory
    {
        int index;
        if (data.IndexesCount == 0)
        {
            index = GetCaretIndex();
            nameByItem = TRUE; // for compatibility with Sal 2.0
        }
        else
        {
            GetSelItems(1, &index);
            nameByItem = FALSE; // for compatibility with Sal 2.0
        }

        // note about compatibility with 2.0
        // the current implementation is illogical: with a single selected item
        // Salamander behaves differently than with a single focused item, but it has one advantage:
        // the user can choose the suggested file name

        if (subDir && index == 0)
        {
            EndStopRefresh(); // the snooper resumes now
            return;           // nothing to do
        }
        else
        {
            data.Indexes = new int[1];
            if (data.Indexes == NULL)
            {
                TRACE_E(LOW_MEMORY);
                EndStopRefresh(); // the snooper resumes now
                return;
            }
            else
            {
                data.Indexes[0] = index;
                data.IndexesCount = 1;
                // build the subject for the dialog
                BOOL isDir = index < Dirs->Count;
                if (!isDir)
                    files = 1;
                CFileData* f = isDir ? &Dirs->At(index) : &Files->At(index - Dirs->Count);
                nameByItemLeafW = AlterFileNameW(f->Name,
                                                 Configuration.FileNameFormat, 0, isDir != FALSE);
                expandedW = LoadStrW(isDir ? IDS_QUESTION_DIRECTORY : IDS_QUESTION_FILE);
            }
        }
    }
    CTruncatedString str;
    // same two-stage %s nesting the narrow code used to rely on:
    // IDS_PACKTOARCHIVE's own %s is filled with expandedW (which, in the single-item
    // case, still carries its own unfilled %s from IDS_QUESTION_FILE/DIRECTORY), then
    // SetW's internal substitution fills that one with the actual item name.
    const std::wstring subjectW = FormatStrW(LoadStrW(IDS_PACKTOARCHIVE), expandedW.c_str());
    str.SetW(subjectW.c_str(), data.IndexesCount > 1 ? NULL : nameByItemLeafW.c_str());

    data.CurrentIndex = 0;
    data.ZIPPath = GetZIPPath();
    data.Dirs = Dirs;
    data.Files = Files;
    data.ArchiveDir = GetArchiveDir();
    data.WorkPathW = GetPathW();
    data.EnumLastDir = NULL;
    data.EnumLastIndex = -1;

    //---  we are packing into a new file, ask for its name
    // The proposed archive name is UTF-16 from its source through the dialog. The
    // external packer receives a checked ANSI/short-path designator only at its ABI.
    std::wstring fileBufW;

    if (nameByItem) // if only one item (file/directory) is selected, the archive inherits its name
    {
        const size_t ext = nameByItemLeafW.find_last_of(L'.');
        if (data.Indexes[0] < Dirs->Count || ext == std::wstring::npos) // ".cvspass" is treated as an extension in Windows ...
        {                                                               // subdirectory or no extension
            fileBufW = nameByItemLeafW + L'.';
        }
        else
        {
            fileBufW = nameByItemLeafW.substr(0, ext + 1);
        }
    }
    else
    {
        // build the default archive name
        std::wstring parentW = sally::unicode::TrimTrailingPathSeparatorsW(std::wstring(GetPathW()));
        std::wstring directoryNameW;
        if (CutDirectoryW(parentW, &directoryNameW))
        {
            fileBufW = directoryNameW + L'.';
        }
        else
            fileBufW = LoadStrW(IDS_NEW_ARCHIVE);
    }

    if (pluginIndex != -1)
    {
        int i;
        for (i = 0; i < PackerConfig.GetPackersCount(); i++)
        {
            if (PackerConfig.GetPackerType(i) == -pluginIndex - 1)
            {
                PackerConfig.SetPreferedPacker(i);
                break;
            }
        }
        if (i == PackerConfig.GetPackersCount()) // the requested plugin was not found
        {
            std::wstring msg = FormatStrW(LoadStrW(IDS_PLUGINPACKERNOTFOUND), pluginName);
            gPrompter->ShowError(LoadStrW(IDS_PACKTITLE), msg.c_str());
            delete[] (data.Indexes);
            EndStopRefresh(); // the snooper resumes now
            return;
        }
    }

    if (PackerConfig.GetPreferedPacker() == -1)
    { // if no preferred packer is set, choose the first one so users do not stare at an empty combo box
        PackerConfig.SetPreferedPacker(0);
    }
    if (PackerConfig.GetPreferedPacker() != -1) // necessary even after Set (it might have failed -> still returns -1)
        fileBufW += PackerConfig.GetPackerExt(PackerConfig.GetPreferedPacker());

    std::wstring fileBufAltW = fileBufW;

    if (target->Is(ptDisk))
    {
        // based on the configuration adjust one of the paths so it goes to the target panel
        if (Configuration.UseAnotherPanelForPack)
            target->UserWorkedOnThisPath = TRUE; // default action = work with the path in the target panel

        std::wstring& buffW = Configuration.UseAnotherPanelForPack ? fileBufW : fileBufAltW;
        std::wstring prefixW = sally::unicode::TrimTrailingPathSeparatorsW(std::wstring(target->GetPathW()));
        buffW = prefixW + L'\\' + buffW;
    }

    // if no item is selected, choose the focused item and store its name
    const std::wstring temporarySelected = SelectFocusedItemAndGetName();

    if (delFilesAfterPacking == 1)
        PackerConfig.Move = TRUE;
    if (delFilesAfterPacking == 0 || // Petr: changed default - user must always enable deletion, it is too risky
        delFilesAfterPacking == 2)
    {
        PackerConfig.Move = FALSE;
    }

    BOOL first = TRUE;

_PACK_AGAIN:

    CPackDialog dlg(HWindow, fileBufW, fileBufAltW, &str, &PackerConfig);

    // Since Windows Vista Microsoft introduced an odd behavior: quick rename selects only the name without the dot and extension
    // the same code is in another place as well
    int selectionEnd = -1;
    if (first)
    {
        if (!Configuration.QuickRenameSelectAll)
        {
            const size_t dot = fileBufW.find_last_of(L'.');
            if (dot != std::wstring::npos && dot > 0) // although ".cvspass" is technically an extension in Windows, Explorer selects the entire name, so we do the same
                                                     //    if (dot != std::wstring::npos)
                selectionEnd = (int)dot;
            dlg.SetSelectionEnd(selectionEnd);
        }
        first = FALSE; // after an error we get the full filename, so select it entirely
    }

    if (dlg.Execute() == IDOK)
    {
        UpdateWindow(MainWindow->HWindow);
        //--- adjust the archive name to its full form
        int errTextID;
        BOOL empty = FALSE;
        if (SalGetFullNameW(fileBufW, &errTextID, Is(ptDisk) ? GetPathW() : NULL))
        {
            //---  searching for a directory link in the packing source; cannot be combined with "delete files after packing"
            BOOL performPack = TRUE;
            if (PackerConfig.Move)
            {
                int containsDirLinks = 0;
                gEnvironment->SetCurrentDirectory(GetPathW());
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

                GetAsyncKeyState(VK_ESCAPE); // initialize GetAsyncKeyState - see help
                CreateSafeWaitWindow(LoadStrW(IDS_ANALYSINGDIRTREEESC), NULL, 3000, TRUE, NULL);

                // try to find the first directory link; if found, simulate an error to stop the search
                std::wstring linkNameW;
                ReadDirectoryTree(NULL /* silent mode */, &data, NULL, FALSE, &containsDirLinks, &linkNameW);

                DestroySafeWaitWindow();

                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                SetCurrentDirectoryToSystem();
                // the directory contains a link and cannot be combined with "delete files after packing":
                // I can't handle the situation where packing a single file from the directory fails
                // (e.g., if the file is locked or access is denied), and I entered the directory via the link.
                // Deleting the whole link is wrong because it won't show that packing failed,
                // and deleting everything except one file after traversing the link is also wrong,
                // because it alters the original directory content, which users report as a bug since it's unexpected.
                if (containsDirLinks == 1)
                {
                    std::wstring msg = FormatStrW(LoadStrW(IDS_DELFILESAFTERPACKINGNOLINKS), linkNameW.c_str());
                    gPrompter->ShowError(LoadStrW(IDS_PACKTITLE), msg.c_str());
                    PackerConfig.Move = FALSE;
                    goto _PACK_AGAIN;
                }
                if (containsDirLinks == 2) // user canceled loading (ESC pressed or closed the wait window)
                    performPack = FALSE;
            }

            //--- confirmation for adding (updating) to an existing archive
            if (performPack && Configuration.CnfrmAddToArchive && gFileSystem->FileExists(fileBufW.c_str()))
            {
                BOOL dontShow = !Configuration.CnfrmAddToArchive;

                // CMessageBox::DialogProc reads Text.GetW() unconditionally (no
                // IsWide() fallback, see msgbox.cpp) - CTruncatedString::Set (narrow) always
                // leaves UseWideText FALSE, so GetW() returns L"" and this confirmation's body
                // was blank every time it showed. Same bug class as the earlier 8ac53e4d
                // (blank delete-confirmation body): a real defect, not Unicode fidelity debt.
                const std::wstring filesDirsW = ExpandPluralFilesDirsTextW(
                    files, data.IndexesCount - files, epfdmNormal, FALSE);

                std::wstring buffW = FormatStrW(LoadStrW(IDS_CONFIRM_ADDTOARCHIVE), L"%s", filesDirsW.c_str());

                size_t slashW = fileBufW.find_last_of(L'\\');
                std::wstring namePartW = (slashW == std::wstring::npos) ? fileBufW : fileBufW.substr(slashW + 1);
                CTruncatedString str2;
                str2.SetW(buffW.c_str(), namePartW.c_str());

                wchar_t alias[200];
                swprintf_s(alias, L"%d\t%s\t%d\t%s",
                           DIALOG_YES, LoadStrW(IDS_CONFIRM_ADDTOARCHIVE_ADD),
                           DIALOG_NO, LoadStrW(IDS_CONFIRM_ADDTOARCHIVE_OVER));
                CMessageBox msgBox(HWindow,
                                   MSGBOXEX_YESNOCANCEL | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT | MSGBOXEX_HINT,
                                   LoadStrW(IDS_QUESTION),
                                   &str2,
                                   LoadStrW(IDS_CONFIRM_ADDTOARCHIVE_NOASK),
                                   &dontShow,
                                   NULL, 0, NULL, alias, NULL, NULL);
                int msgBoxRed = msgBox.Execute();
                performPack = (msgBoxRed == IDYES);
                if (msgBoxRed == IDNO) // OVERWRITE
                {
                    // fileBufW remains the authoritative normalized name.
                    ClearReadOnlyAttr(fileBufW.c_str()); // so it can be deleted...
                    if (!gFileSystem->DeleteFile(fileBufW.c_str()).success)
                    {
                        DWORD err = GetLastError();
                        gPrompter->ShowError(LoadStrW(IDS_ERROROVERWRITINGFILE), GetErrorTextOwned(err).c_str());
                        // fall through to _PACK_AGAIN
                    }
                    else
                        performPack = TRUE;
                }
                Configuration.CnfrmAddToArchive = !dontShow;
                if (!performPack)
                    goto _PACK_AGAIN;
            }

            //---  actual packing
            if (performPack)
            {
                std::wstring archiveForPacker = fileBufW;
                std::wstring sourceForPacker = GetPathW();
                const int preferredPacker = PackerConfig.GetPreferedPacker();
                if (preferredPacker != -1 &&
                    PackerConfig.GetPackerType(preferredPacker) == CUSTOMPACKER_EXTERNAL)
                {
                    // External archiver command lines may still require an ANSI/8.3
                    // designator. Keep that compatibility boundary here, exactly as
                    // Unpack does; built plugins receive the exact live UTF-16 paths.
                    //
                    // The gate used to run for every packer. A plugin archiver takes
                    // wchar_t* (plugins/shared/spl_arc.h:95), and an sdk107 plugin gets
                    // its own exact-or-refuse narrowing at the frozen boundary
                    // (core_to_legacy.cpp NarrowOptionalText), so demanding an ANSI name
                    // of them refused to pack into any path CP_ACP cannot spell on a
                    // volume with 8.3 disabled - with the ZIP plugin, which handles that
                    // path perfectly well.
                    //
                    // Where the gate does apply it must still refuse rather than hand
                    // over a best-fit guess: '?' is a wildcard to most archivers, so a
                    // mangled name does not merely fail, it can pack unrelated files
                    // (audit A12).
                    auto shortName = [](const std::wstring& p) { return GetShortPathW(p.c_str()); };
                    const auto archiveAnsi = sally::unicode::ResolveAnsiToolPath(fileBufW, shortName);
                    const auto sourceAnsi = sally::unicode::ResolveAnsiToolPath(sourceForPacker, shortName);
                    if (!archiveAnsi.Usable() || !sourceAnsi.Usable())
                    {
                        const std::wstring& offending = archiveAnsi.Usable() ? sourceForPacker : fileBufW;
                        gPrompter->ShowError(LoadStrW(IDS_PACKTITLE),
                                             FormatStrW(LoadStrW(IDS_UNPACK_ANSI_ONLY), offending.c_str()).c_str());
                        goto _PACK_AGAIN;
                    }
                    archiveForPacker = archiveAnsi.WidePath;
                    sourceForPacker = sourceAnsi.WidePath;
                }
                gEnvironment->SetCurrentDirectory(GetPathW());
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                if (PackerConfig.ExecutePacker(this, archiveForPacker.c_str(), PackerConfig.Move,
                                               sourceForPacker.c_str(),
                                               PanelEnumDiskSelection, &data,
                                               PanelEnumLastNameW))
                { // packing succeeded
                    // if (nextFocus[0] != 0) strcpy(NextFocusName, nextFocus);
                    FocusFirstNewItem = TRUE; // focus also archives renamed by a plugin (e.g. SFX -> archive.exe)

                    SetSel(FALSE, -1, TRUE);                        // explicit redraw
                    PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
                }
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                SetCurrentDirectoryToSystem();

                //---  refresh directories that are not automatically refreshed
                // changes in the directory where the new archive is located
                std::wstring notificationPathW = fileBufW;
                CutDirectoryW(notificationPathW); // may fail, but we do not handle this case (an extra refresh is harmless)
                MainWindow->PostChangeOnPathNotificationW(notificationPathW.c_str(), FALSE);
                // moving from disk to archive -> also a change on disk (files/directories removed)
                if (PackerConfig.Move)
                {
                    // changes in the current directory in the panel including its subdirectories
                    MainWindow->PostChangeOnPathNotificationW(GetPathW(), TRUE);
                }
            }
        }
        else
        {
            gPrompter->ShowError(LoadStrW(IDS_PACKTITLE), LoadStrW(errTextID));
            goto _PACK_AGAIN;
        }
    }

    // if we selected an item, deselect it again
    UnselectItemWithName(temporarySelected);

    UpdateWindow(MainWindow->HWindow);
    delete[] (data.Indexes);

    //---  if any Salamander window is active, suspend mode ends
    EndStopRefresh();
}

void CFilesWindow::Unpack(CFilesWindow* target, int pluginIndex, const wchar_t* pluginName, const wchar_t* unpackMask)
{
    CALL_STACK_MESSAGE2("CFilesWindow::Unpack(, %d)", pluginIndex);

    // restore DefaultDir
    MainWindow->UpdateDefaultDir(MainWindow->GetActivePanel() == this);

    int i = GetCaretIndex();
    if (i >= Dirs->Count && i < Dirs->Count + Files->Count)
    {
        BeginStopRefresh();

        CFileData* file = &Files->At(i - Dirs->Count);
        // The archive's real identity. the removed ANSI mirror and file->Name are the CP_ACP mirrors,
        // and for a name this code page cannot spell the mirror is not merely ugly:
        // best-fit mapping can produce the name of a *different* archive that genuinely
        // exists in the same folder, which is how audit A18 unpacks the wrong file and
        // reports success. Everything below decides from the wide form; an ANSI
        // designator is derived once, checked, just before the char*-only unpacker runs.
        const std::wstring archiveNameW = file->Name;
        const std::wstring archiveW = sally::unicode::BuildPanelChildPathW(GetPathW(), file->Name);

        // Destination seeds and dialog buffers stay wide end-to-end.
        std::wstring pathW, pathAltW;
        if (target->Is(ptDisk))
        {
            if (Configuration.UseAnotherPanelForUnpack)
            {
                target->UserWorkedOnThisPath = TRUE; // default action = work with the path in the target panel
                pathW = target->GetPathW();
            }
            else
                pathAltW = target->GetPathW();
        }
        const std::wstring fileNameW = AlterFileNameW(archiveNameW.c_str(), Configuration.FileNameFormat, 0, false);
        if (Configuration.UseSubdirNameByArchiveForUnpack)
        {
            // The subdirectory is named after the archive, so it has to be cut from the
            // wide name too - derived from the mirror it unpacks into a folder called
            // "???", and two different archives then collide on one folder.
            std::wstring stemW, extW;
            sally::unicode::SplitFileNameAndExtension(fileNameW, stemW, extW);
            for (std::wstring* buff : {&pathW, &pathAltW})
            {
                if (!buff->empty() && buff->back() != L'\\')
                    *buff += L'\\';
                *buff += stemW;
            }
        }
        std::wstring maskW = unpackMask != NULL ? unpackMask : L"*.*";
        CTruncatedString str;
        // wide - fileNameW is the audited archive name (see the comment
        // above about audit A18); narrowing it here just to feed CTruncatedString
        // undid that work before it ever reached the Unpack dialog's Subject line.
        str.SetW(LoadStrW(IDS_UNPACKARCHIVE), fileNameW.c_str());

        if (pluginIndex != -1)
        {
            int i2;
            for (i2 = 0; i2 < UnpackerConfig.GetUnpackersCount(); i2++)
            {
                if (UnpackerConfig.GetUnpackerType(i2) == -pluginIndex - 1)
                {
                    UnpackerConfig.SetPreferedUnpacker(i2);
                    break;
                }
            }
            if (i2 == UnpackerConfig.GetUnpackersCount()) // requested plugin not found
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_PLUGINUNPACKERNOTFOUND), pluginName);
                gPrompter->ShowError(LoadStrW(IDS_ERRORUNPACK), msg.c_str());
                EndStopRefresh(); // the snooper resumes now
                return;
            }
        }
        else // choose the unpacker based on the extension
        {
            CMaskGroup tmpmask;
            if (UnpackerConfig.GetPreferedUnpacker() == -1)
            { // if none is preferred, pick the first one (so users do not stare at an empty combo box)
                UnpackerConfig.SetPreferedUnpacker(0);
            }
            if (UnpackerConfig.GetPreferedUnpacker() != -1)
            {
                tmpmask.SetMasksString(UnpackerConfig.GetUnpackerExt(UnpackerConfig.GetPreferedUnpacker()), TRUE);
            }
            else
            {
                tmpmask.SetMasksString(L"", TRUE);
            }
            int errpos = 0;
            tmpmask.PrepareMasks(errpos);
            // wide: file->Name/Ext are the same CP_ACP mirror audit A18 already
            // flagged above (archiveNameW) - best-fit collisions can pick the wrong archive's
            // unpacker. Match the audited wide name instead.
            if (!tmpmask.AgreeMasks(archiveNameW.c_str(), NULL))
            {
                int i2;
                for (i2 = 0; i2 < UnpackerConfig.GetUnpackersCount(); i2++)
                {
                    tmpmask.SetMasksString(UnpackerConfig.GetUnpackerExt(i2), TRUE);
                    tmpmask.PrepareMasks(errpos);
                    if (tmpmask.AgreeMasks(archiveNameW.c_str(), NULL))
                    {
                        UnpackerConfig.SetPreferedUnpacker(i2);
                        break;
                    }
                }
            }
        }
        BOOL delArchiveWhenDone = FALSE;
    DO_AGAIN:

        CUnpackDialog unpackDlg(HWindow, pathW, pathAltW, maskW, &str, &UnpackerConfig, &delArchiveWhenDone);
        if (unpackDlg.Execute() == IDOK)
        {
            UpdateWindow(MainWindow->HWindow);
            //--- adjust the archive name to its full form
            int errTextID;
            std::wstring nextFocusW;
            const wchar_t* text = NULL;
            if (!SalGetFullNameW(pathW, &errTextID, Is(ptDisk) ? GetPathW() : NULL, &nextFocusW))
            {
                if (errTextID == IDS_EMPTYNAMENOTALLOWED)
                    pathW = GetPathW();
                else
                    text = LoadStrW(errTextID);
            }
            if (text == NULL)
            {
                std::wstring archiveForUnpacker = archiveW;
                std::wstring targetForUnpacker = pathW;
                const int preferredUnpacker = UnpackerConfig.GetPreferedUnpacker();
                if (preferredUnpacker != -1 &&
                    UnpackerConfig.GetUnpackerType(preferredUnpacker) == CUSTOMUNPACKER_EXTERNAL)
                {
                    // External archiver command lines may still require an ANSI/8.3
                    // designator. Keep that compatibility boundary here; built plugins
                    // receive the exact live UTF-16 paths above.
                    auto shortName = [](const std::wstring& p) { return GetShortPathW(p.c_str()); };
                    const auto archiveAnsi = sally::unicode::ResolveAnsiToolPath(archiveW, shortName);
                    const auto targetAnsi = sally::unicode::ResolveAnsiToolPath(pathW, shortName);
                    if (!archiveAnsi.Usable() || !targetAnsi.Usable())
                    {
                        const std::wstring& offending = archiveAnsi.Usable() ? pathW : archiveW;
                        gPrompter->ShowError(LoadStrW(IDS_ERRORUNPACK),
                                             FormatStrW(LoadStrW(IDS_UNPACK_ANSI_ONLY), offending.c_str()).c_str());
                        if (!archiveAnsi.Usable())
                        {
                            // Re-asking for a destination cannot help; the archive is the problem.
                            EndStopRefresh(); // the snooper resumes now
                            return;
                        }
                        goto DO_AGAIN;
                    }
                    archiveForUnpacker = archiveAnsi.WidePath;
                    targetForUnpacker = targetAnsi.WidePath;
                }
                std::wstring newDir;
                if (CheckAndCreateDirectoryOwnedW(pathW.c_str(), NULL, TRUE, NULL, &newDir,
                                                  FALSE, TRUE))
                {
                    // launch the unpacker
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                    CDynamicStringImp archiveVolumes;
                    if (!UnpackerConfig.ExecuteUnpacker(MainWindow->HWindow, this, archiveForUnpacker.c_str(), maskW.c_str(),
                                                        targetForUnpacker.c_str(), delArchiveWhenDone,
                                                        delArchiveWhenDone ? &archiveVolumes : NULL))
                    {
                        if (!newDir.empty())
                            RemoveEmptyDirsW(newDir.c_str());
                    }
                    else // unpacking succeeded (no Cancel, Skip may have occurred)
                    {
                        if (delArchiveWhenDone && archiveVolumes.Length > 0)
                        {
                            wchar_t* name = archiveVolumes.Text;
                            BOOL skipAll = FALSE;
                            do
                            {
                                if (*name != 0)
                                {
                                    while (1)
                                    {
                                        ClearReadOnlyAttr(name); // allow deletion of read-only files too
                                        if (!gFileSystem->DeleteFile(name).success && !skipAll)
                                        {
                                            DWORD err = GetLastError();
                                            if (err == ERROR_FILE_NOT_FOUND)
                                                break; // if the user already managed to delete the file, all is OK
                                            int res = (int)CFileErrorDlg(MainWindow->HWindow, LoadStrW(IDS_ERRORDELETINGFILE), name, GetErrorTextOwned(err).c_str()).Execute();
                                            if (res == IDB_SKIPALL)
                                                skipAll = TRUE;
                                            if (res == IDB_SKIPALL || res == IDB_SKIP)
                                                break;           // skip
                                            if (res == IDCANCEL) // cancel
                                            {
                                                name = archiveVolumes.Text + archiveVolumes.Length - 1;
                                                nextFocusW.clear(); // keep the cursor on the archive, do not jump to the directory with the unpacked archive
                                                break;
                                            }
                                            // let IDRETRY attempt the next loop iteration
                                        }
                                        else
                                            break; // deleted
                                    }
                                }
                                name = name + wcslen(name) + 1;
                            } while (name - archiveVolumes.Text < archiveVolumes.Length);
                        }
                        if (!nextFocusW.empty())
                        {
                            // Keep the exact UTF-16 focus request for WM_USER_DONEXTFOCUS.
                            // for a destination folder name outside the system code page.
                            NextFocusNameW = nextFocusW;
                            PostMessage(HWindow, WM_USER_DONEXTFOCUS, 0, 0); // it is a directory, must be
                        }
                    }
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

                    //---  refresh directories that are not automaticallyrefreshed
                    // change in the directory containing the archive (should not happen during unpack,
                    // but refresh anyway)
                    MainWindow->PostChangeOnPathNotificationW(GetPathW(), FALSE);
                    if (newDir[0] != 0) // some new subdirectories were created along the path
                    {
                        std::wstring notificationPathW = newDir;
                        CutDirectoryW(notificationPathW); // should always work (path to the first newly created directory)
                        // changes in the directory where the first new subdirectory was created
                        MainWindow->PostChangeOnPathNotificationW(notificationPathW.c_str(), TRUE);
                    }
                    else
                    {
                        // changes in the directory where files were unpacked
                        MainWindow->PostChangeOnPathNotificationW(pathW.c_str(), TRUE);
                    }
                }
                else
                {
                    if (newDir[0] != 0)
                    {
                        std::wstring notificationPathW = newDir;
                        CutDirectoryW(notificationPathW); // should always work (path to the first newly created directory)

                        //---  refresh directories that are not automatically refreshed
                        // if creating directories failed, report changes immediately (the user may
                        // choose a completely different path next time); the newly created path is kept (almost dead code)
                        MainWindow->PostChangeOnPathNotificationW(notificationPathW.c_str(), TRUE);
                    }
                    goto DO_AGAIN;
                }
            }
            else
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORUNPACK), text);
                goto DO_AGAIN;
            }
        }
        UpdateWindow(MainWindow->HWindow);

        //---  if any Salamander window is active, suspend mode ends
        EndStopRefresh();
    }
}

// countSizeMode - 0 normal calculation, 1 for the selected item, 2 for all subdirectories
void CFilesWindow::CalculateOccupiedZIPSpace(int countSizeMode)
{
    CALL_STACK_MESSAGE2("CFilesWindow::CalculateOccupiedZIPSpace(%d)", countSizeMode);
    if (Is(ptZIPArchive) && (ValidFileData & VALID_DATA_SIZE)) // only if CFileData::Size is valid (sizes defined via plugin data are unlikely for archives, so we ignore them here for now)
    {
        BeginStopRefresh(); // the snooper will wait

        TDirectArray<CQuadWord> sizes(200, 400);
        CQuadWord totalSize(0, 0); // calculated size
        int files = 0;
        int dirs = 0;

        int selIndex = -1;
        BOOL upDir;
        if (Dirs->Count > 0)
            upDir = (wcscmp(Dirs->At(0).Name, L"..") == 0);
        else
            upDir = FALSE;
        int count = GetSelCount();
        if (countSizeMode == 0 && count != 0 || countSizeMode == 2) // valid selection
        {
            if (countSizeMode == 2)
                count = Dirs->Count - (upDir ? 1 : 0);
            int* indexes = new int[count];
            if (indexes == NULL)
            {
                TRACE_E(LOW_MEMORY);
                EndStopRefresh(); // the snooperresumes now
                return;
            }
            else
            {
                if (countSizeMode == 2)
                {
                    int i = (upDir ? 1 : 0);
                    int j;
                    for (j = 0; j < count; j++)
                        indexes[j] = i++;
                }
                else
                    GetSelItems(count, indexes);
                int i = count;
                while (i--)
                {
                    CFileData* f = (indexes[i] < Dirs->Count) ? &Dirs->At(indexes[i]) : &Files->At(indexes[i] - Dirs->Count);
                    if (indexes[i] < Dirs->Count)
                    {
                        f->SizeValid = 1;
                        f->Size = GetArchiveDir()->GetDirSize(GetZIPPath(), f->Name, &dirs, &files, &sizes);
                        totalSize += f->Size;
                        dirs++;
                    }
                    else
                    {
                        sizes.Add(f->Size); // adding errors are handled only in the output dialog
                        totalSize += f->Size;
                        files++;
                    }
                }
            }
            delete[] (indexes);
        }
        else // take the selected file or directory
        {
            selIndex = GetCaretIndex();
            if (upDir && selIndex == 0)
            {
                EndStopRefresh(); // the snooper resumes now
                return;           // nothing to do
            }
            else
            {
                if (countSizeMode == 0)
                {
                    SetSel(TRUE, selIndex);
                    PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0);
                }
                CFileData* f = (selIndex < Dirs->Count) ? &Dirs->At(selIndex) : &Files->At(selIndex - Dirs->Count);
                if (selIndex < Dirs->Count)
                {
                    f->SizeValid = 1;
                    f->Size = GetArchiveDir()->GetDirSize(GetZIPPath(), f->Name, &dirs, &files, &sizes);
                    totalSize += f->Size;
                    dirs++;
                }
                else
                {
                    sizes.Add(f->Size); // adding errors are handled only in the output dialog
                    totalSize += f->Size;
                    files++;
                }
            }
        }

        // sort if we counted over more than one selected directory or over all of them
        if (((countSizeMode == 0 && dirs > 1) || countSizeMode == 2) && SortType == stSize)
        {
            ChangeSortType(stSize, FALSE, TRUE);
        }
        RefreshListBox(-1, -1, FocusedIndex, FALSE, FALSE); // recalculate column widths
        if (countSizeMode == 0)
        {
            CSizeResultsDlg(HWindow, totalSize, CQuadWord(-1, -1), CQuadWord(-1, -1),
                            files, dirs, &sizes)
                .Execute();
            //      CZIPSizeResultsDlg(HWindow, totalSize, files, dirs).Execute();
        }
        if (selIndex != -1 && countSizeMode == 0)
        {
            SetSel(FALSE, selIndex);
            PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0);
        }
        RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);
        UpdateWindow(MainWindow->HWindow);

        EndStopRefresh(); // the snooper resumes now
    }
}

void CFilesWindow::AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs)
{
    CALL_STACK_MESSAGE3("CFilesWindow::AcceptChangeOnPathNotification(%s, %d)",
                        path != NULL ? sally::diagnostic::EncodeAcpLossy(path).c_str() : "<null>", includingSubdirs);

    BOOL refresh = FALSE;
    if ((Is(ptDisk) || Is(ptZIPArchive)) && (!AutomaticRefresh || GetNetworkDrive()))
    {
        // test the equality of paths or at least their prefix (we only care about disk paths,
        // FS paths in 'path' are automatically excluded because they can never match the removed ANSI mirror)
        std::wstring path1 = path != NULL ? path : L"";
        std::wstring path2 = GetPathW(); // for archives this is the path to the archive
        SalPathRemoveBackslashW(path1);
        SalPathRemoveBackslashW(path2);
        refresh = !includingSubdirs && IsTheSamePath(path1.c_str(), path2.c_str()) || // exact match
                  includingSubdirs && PathStartsWithW(path2.c_str(), path1.c_str());
        if (Is(ptDisk) && !refresh && CutDirectoryW(path1)) // pointless for archives
        {
            SalPathRemoveBackslashW(path1);
            // on NTFS the last subdirectory timestamp also changes (unfortunately visible only after entering
            // that subdirectory, but perhaps it will be fixed eventually, so refresh proactively)
            refresh = IsTheSamePath(path1.c_str(), path2.c_str());
        }
        if (refresh)
        {
            HANDLES(EnterCriticalSection(&TimeCounterSection));
            int t1 = MyTimeCounter++;
            HANDLES(LeaveCriticalSection(&TimeCounterSection));
            PostMessage(HWindow, WM_USER_REFRESH_DIR, 0, t1);
        }
    }
    else
    {
        if (Is(ptPluginFS) && GetPluginFS()->NotEmpty()) // send notification to the FS
        {
            // the EnterPlugin+LeavePlugin section must be exposed up to here (not wrapped inside the interface)
            EnterPlugin();
            GetPluginFS()->AcceptChangeOnPathNotification(GetPluginFS()->GetPluginFSName(), path != NULL ? path : L"", includingSubdirs);
            LeavePlugin();
        }
    }

    if (Is(ptDisk) && !refresh &&           // only disks have free space (archives do not and FS is handled elsewhere)
        HasTheSameRootPath(path, GetPathW()) // same root -> possible change in free disk space size

        /* && (!AutomaticRefresh ||  // commented out because notifications do not arrive for subdirectory changes on auto-refreshed paths causing the free space info to remain invalid
       !IsTheSamePath(path, GetPathW()))*/
        ) // this path is not monitored for changes -> refresh will definitely not arrive
    {
        RefreshDiskFreeSpace(TRUE, TRUE);
    }
}

void CFilesWindow::IconOverlaysChangedOnPath(const wchar_t* path)
{
    //  if ((int)(GetTickCount() - NextIconOvrRefreshTime) < 0)
    //    TRACE_I("CFilesWindow::IconOverlaysChangedOnPath: skipping notification for: " << path);
    if ((int)(GetTickCount() - NextIconOvrRefreshTime) >= 0 &&             // refresh of icon overlays occurs at NextIconOvrRefreshTime; before that it makes no sense to track changes
        !IconOvrRefreshTimerSet && !NeedIconOvrRefreshAfterIconsReading && // icon overlay refresh not scheduled yet
        Configuration.EnableCustomIconOverlays && Is(ptDisk) &&
        (UseSystemIcons || UseThumbnails) && IconCache != NULL &&
        // native-wide: the caller supplies
        // the genuine wide path from SHGetPathFromIDListW (main_window_commands_help.cpp), not a
        // narrow best-fit mirror that could disagree with GetPathW() for a non-ASCII panel path.
        IsTheSamePath(path, GetPathW()))
    {
        DWORD elapsed = GetTickCount() - LastIconOvrRefreshTime;
        if (elapsed < ICONOVR_REFRESH_PERIOD) // wait before the next icon overlay refresh so we do not refresh too often
        {
            // TRACE_I("CFilesWindow::IconOverlaysChangedOnPath: setting timer for refresh");
            if (SetTimer(HWindow, IDT_ICONOVRREFRESH, max(200, ICONOVR_REFRESH_PERIOD - elapsed), NULL))
            {
                IconOvrRefreshTimerSet = TRUE;
                return;
            }
            // if the timer fails, attempt an immediate refresh...
        }
        // try to refresh immediately (as long as it didn't come too soon after the previous one)
        if (!IconCacheValid) // perform after icons finish loading (they may or may not load correctly)
        {
            // TRACE_I("CFilesWindow::IconOverlaysChangedOnPath: delaying refresh till end of reading of icons");
            NeedIconOvrRefreshAfterIconsReading = TRUE;
        }
        else // refresh immediately
        {
            // TRACE_I("CFilesWindow::IconOverlaysChangedOnPath: doing refresh: sleeping icon reader");
            SleepIconCacheThread();
            WaitOneTimeBeforeReadingIcons = 200; // during this time the icon reader waits before starting overlay loading; subsequent notifications from Tortoise SVN within 200 ms can be ignored
            LastIconOvrRefreshTime = GetTickCount();
            NextIconOvrRefreshTime = LastIconOvrRefreshTime + WaitOneTimeBeforeReadingIcons;
            WakeupIconCacheThread();
            // TRACE_I("CFilesWindow::IconOverlaysChangedOnPath: doing refresh: icon reader is awake again");
        }
    }
}
