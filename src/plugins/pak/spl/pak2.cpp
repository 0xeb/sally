// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "dumpmem.h"
#include "array2.h"

#include "..\dll\pakiface.h"
#include "pak.rh"
#include "pak.rh2"
#include "lang\lang.rh"
#include "pak.h"
#include "pak_text.h"

DWORD ComputeDirDepth(const wchar_t* fileName)
{
    CALL_STACK_MESSAGE_NONE
    DWORD ret = 0;
    while (*fileName)
    {
        if (*fileName++ == L'\\')
            ret++;
    }
    return ret;
}

BOOL CPluginInterfaceForArchiver::MakeFileList3(TIndirectArray2<CFileInfo>& files, BOOL* del, std::string_view archiveRoot, const wchar_t* sourcePath,
                                                SalEnumSelection2 next, void* nextParam)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::MakeFileList3(, , , %ls, , )", sourcePath);
    BOOL isDir;
    CQuadWord nextSize;
    const wchar_t* nextName;
    std::string pakName;
    DWORD pakSize;
    std::wstring sourName;
    BOOL skip;
    int errorOccured = 0;

    ProgressTotal = CQuadWord(0, 0);
    *del = FALSE;

    while ((nextName = next(SalamanderGeneral->GetMsgBoxParent(), 3, NULL, &isDir, &nextSize, NULL,
                            NULL, nextParam, &errorOccured)) != NULL)
    {
        skip = FALSE;
        if (isDir)
            continue;
        sourName = sourcePath;
        SPLSalPathAppendOwned(sourName, nextName);
        if (JoinPakEntryName(archiveRoot, nextName, pakName) != PakTextStatus::Success)
        {
            if (Silent & SF_LONGNAMES)
                continue;
            switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_SKIPCANCEL, sourName.c_str(), LangStr(IDS_TOOLONGNAME2).c_str(), NULL))
            {
            case DIALOG_SKIPALL:
                Silent |= SF_LONGNAMES;
            case DIALOG_SKIP:
                break;
            default:
                return FALSE;
            }
            continue;
        }
        if (!PakIFace->FindFile(pakName.c_str(), &pakSize))
            return FALSE;
        if (pakSize != -1 && (Silent & SF_SKIPALL))
            continue;
        if (pakSize != -1)
        {
            if (!(Silent & SF_OVEWRITEALL))
            {
                std::wstring name1(PakFileName);
                std::wstring pakNameW;
                if (DecodePakText(pakName, pakNameW) != PakTextStatus::Success)
                    return FALSE;
                FILETIME ft;

                SPLSalPathAppendOwned(name1, pakNameW.c_str());
                PakIFace->GetPakTime(&ft);
                const std::wstring data1 = GetInfo(&ft, pakSize);
                HANDLE file;
                file = INVALID_HANDLE_VALUE;
                while (file == INVALID_HANDLE_VALUE && !skip)
                {
                    file = CreateFileW(sourName.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (file != INVALID_HANDLE_VALUE)
                        break;
                    if (Silent & SF_IOERRORS)
                    {
                        skip = TRUE;
                        break;
                    }
                    const std::wstring error = LangStr(IDS_ERROPEN) +
                                               SPLGetErrorTextOwned(SalamanderGeneral, GetLastError());
                    switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, sourName.c_str(), error.c_str(), NULL))
                    {
                    case DIALOG_SKIPALL:
                        Silent |= SF_IOERRORS;
                    case DIALOG_SKIP:
                        skip = TRUE;
                        break;
                    case DIALOG_CANCEL:
                    case DIALOG_FAIL:
                        return FALSE;
                    }
                }
                if (skip)
                    continue;
                GetFileTime(file, NULL, NULL, &ft);
                CloseHandle(file);
                const std::wstring data2 = GetInfo(&ft, (unsigned)nextSize.Value);
                // CONFIRM FILE OVERWRITE: filename1+filedata1+filename2+filedata2, buttons yes/all/skip/skip all/cancel
                switch (SalamanderGeneral->DialogOverwrite(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_YESALLSKIPCANCEL, name1.c_str(), data1.c_str(), sourName.c_str(), data2.c_str()))
                {
                case DIALOG_ALL:
                    Silent |= SF_OVEWRITEALL;
                case DIALOG_YES:
                    break;
                case DIALOG_SKIPALL:
                    Silent |= SF_SKIPALL;
                case DIALOG_SKIP:
                    skip = TRUE;
                    break;
                case DIALOG_CANCEL:
                case DIALOG_FAIL:
                    return FALSE;
                }
                if (skip)
                    continue;
            }
            if (!PakIFace->MarkForDelete())
                return FALSE;
            *del = TRUE;
        }
        if (nextSize > CQuadWord(0x80000000, 0))
        {
            if (Silent & SF_LARGE)
                continue;
            switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_SKIPCANCEL, sourName.c_str(), LangStr(IDS_TOOLARGE).c_str(), NULL))
            {
            case DIALOG_SKIPALL:
                Silent |= SF_LARGE;
            case DIALOG_SKIP:
                skip = TRUE;
                break;
            case DIALOG_CANCEL:
            case DIALOG_FAIL:
                return FALSE;
            }
        }
        if (skip)
            continue;
        const DWORD dirDepth = ComputeDirDepth(sourName.c_str());
        CFileInfo* f = NewFileInfo(std::move(sourName), 0, dirDepth, (DWORD)nextSize.Value);
        if (!f)
        {
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            return FALSE;
        }
        if (!files.Add(f))
        {
            delete f;
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            return FALSE;
        }
        ProgressTotal += nextSize;
    }
    return errorOccured != SALENUM_CANCEL; // cancel aborts the operation
}

BOOL CPluginInterfaceForArchiver::DelFilesToBeOverwritten(unsigned* deleted)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::DelFilesToBeOverwritten()");
    Salamander->ProgressDialogAddText(LangStr(IDS_DELFILES).c_str(), TRUE);
    unsigned total;
    if (!PakIFace->GetDelProgressTotal(&total))
        return FALSE;
    ProgressTotal += CQuadWord(total, 0);
    BOOL opt;
    if (!PakIFace->DeleteFiles(&opt))
    {
        if (opt)
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_NEEDOPT).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
        return FALSE;
    }
    *deleted = total;
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::AddFiles(TIndirectArray2<CFileInfo>& files, unsigned deleted, const wchar_t* sourPath, std::string_view archiveRoot)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::AddFiles(, 0x%X, %ls, )", deleted, sourPath);
    Abort = TRUE;
    CFileInfo* f;
    BOOL skip;
    CQuadWord currentProgress = CQuadWord(deleted, 0);
    int i;
    for (i = 0; i < files.Count; i++)
    {
        skip = FALSE;
        f = files[i];
        const std::wstring message = LangStr(IDS_PACKING) + f->Name;
        Salamander->ProgressDialogAddText(message.c_str(), TRUE);
        IOFile = INVALID_HANDLE_VALUE;
        while (IOFile == INVALID_HANDLE_VALUE)
        {
            IOFile = CreateFileW(f->Name.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (IOFile != INVALID_HANDLE_VALUE)
                break;
            if (Silent & SF_IOERRORS)
            {
                skip = TRUE;
                goto l_next;
            }
            const std::wstring error = LangStr(IDS_ERROPEN) +
                                       SPLGetErrorTextOwned(SalamanderGeneral, GetLastError());
            switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, f->Name.c_str(), error.c_str(), NULL))
            {
            case DIALOG_SKIPALL:
                Silent |= SF_IOERRORS;
            case DIALOG_SKIP:
                skip = TRUE;
                goto l_next;
            case DIALOG_CANCEL:
            case DIALOG_FAIL:
                return FALSE;
            }
        }
        IOFileName = f->Name.c_str();
        if (!Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
            return FALSE;
        Salamander->ProgressSetTotalSize(CQuadWord(f->Size, 0), ProgressTotal);
        BOOL ret;
        {
            std::string pakName;
            if (f->Name.compare(0, wcslen(sourPath), sourPath) != 0)
            {
                CloseHandle(IOFile);
                return FALSE;
            }
            std::wstring rootW = f->Name.substr(wcslen(sourPath));
            if (!rootW.empty() && rootW[0] == L'\\')
                rootW.erase(0, 1);
            if (JoinPakEntryName(archiveRoot, rootW, pakName) != PakTextStatus::Success)
            {
                CloseHandle(IOFile);
                return FALSE;
            }
            ret = PakIFace->AddFile(pakName.c_str(), f->Size);
        }
        CloseHandle(IOFile);
        if (!ret && Abort)
            return FALSE;
        f->Status = STATUS_OK;

    l_next:
        currentProgress += CQuadWord(f->Size, 0);
        if (!Salamander->ProgressSetSize(CQuadWord(f->Size, 0), currentProgress, TRUE))
            return FALSE;
    }
    return TRUE;
}

//#define DIR_DEPTH(i) (((CFileInfo *)(*files)[i])->DirDepth)

void SortByDirDepth(int left, int right, TIndirectArray2<CFileInfo>& files)
{
    CALL_STACK_MESSAGE_NONE
    int i = left, j = right;
    DWORD pivotOffset = files[(i + j) / 2]->DirDepth;
    do
    {
        while (files[i]->DirDepth <= pivotOffset && i < right)
            i++;
        while (pivotOffset <= files[j]->DirDepth && j > left)
            j--;
        if (i <= j)
        {
            CFileInfo* tmp = files[i];
            files[i] = files[j];
            files[j] = tmp;
            i++;
            j--;
        }
    } while (i <= j); //do they have to match?
    if (left < j)
        SortByDirDepth(left, j, files);
    if (i < right)
        SortByDirDepth(i, right, files);
}

void CPluginInterfaceForArchiver::DeleteSourceFiles(TIndirectArray2<CFileInfo>& files, const wchar_t* sourcePath)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::DeleteSourceFiles(, %ls)", sourcePath);
    if (files.Count == 0)
        return;
    CFileInfo* f;
    std::wstring path;
    SortByDirDepth(0, files.Count - 1, files);
    DWORD sourceDepth = ComputeDirDepth(sourcePath);

    int i;
    for (i = files.Count - 1; i >= 0; i--)
    {
        f = files[i];
        if (f->Status == STATUS_OK)
        {
            SalamanderGeneral->ClearReadOnlyAttr(f->Name.c_str());
            DeleteFileW(f->Name.c_str());
            path = f->Name;
            while (ComputeDirDepth(path.c_str()) > sourceDepth + 1)
            {
                SPLCutDirectoryOwned(SalamanderGeneral, path);
                if (!RemoveDirectoryW(path.c_str()))
                    break;
            }
        }
    }
}

BOOL CPluginInterfaceForArchiver::PackToArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                const wchar_t* archiveRoot, BOOL move, const wchar_t* sourcePath,
                                                SalEnumSelection2 next, void* nextParam)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::PackToArchive(, %ls, %ls, %d, %ls, ,)", fileName,
                        archiveRoot, move, sourcePath);

    PakIFace = PAKGetIFace();
    if (!PakIFace)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    BOOL ret = TRUE;
    Salamander = salamander;
    PakFileName = fileName;
    Silent = 0;

    CPakCallbacks pakCalls(this);
    PakIFace->Init(&pakCalls);
    const std::wstring title = SPLFormatStringOwned(
        LangStr(IDS_ADDPROGTITLE).c_str(), SalamanderGeneral->SalPathFindFileName(PakFileName));
    Salamander->OpenProgressDialog(title.c_str(), TRUE, NULL, FALSE);
    Salamander->ProgressDialogAddText(LangStr(IDS_PREPAREDATA).c_str(), FALSE);

    if (!PakIFace->OpenPak(fileName, OP_WRITE_MODE))
        ret = FALSE;
    else
    {
        TIndirectArray2<CFileInfo> files(256);
        BOOL del;
        std::string arcRootStr;
        if (EncodePakEntryName(archiveRoot, arcRootStr) != PakTextStatus::Success)
        {
            SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_OK,
                                           archiveRoot, LangStr(IDS_TOOLONGNAME2).c_str(), NULL);
            ret = FALSE;
        }
        else
        {
            std::string_view arcRoot(arcRootStr);
            if (!arcRoot.empty() && arcRoot.front() == '\\')
                arcRoot.remove_prefix(1);
            if (!MakeFileList3(files, &del, arcRoot, sourcePath, next, nextParam))
            {
                ret = FALSE;
            }
            unsigned deleted = 0;
            if (ret && del && !DelFilesToBeOverwritten(&deleted))
                ret = FALSE;
            else if (ret)
            {
                if (!PakIFace->StartAdding(files.Count))
                    ret = FALSE;
                else
                {
                    Salamander->ProgressDialogAddText(LangStr(IDS_ADDFILES).c_str(), TRUE);
                    if (!AddFiles(files, deleted, sourcePath, arcRoot))
                        ret = FALSE;
                    ret = PakIFace->FinalizeAdding() && ret;
                    if (ret && move)
                        DeleteSourceFiles(files, sourcePath);
                }
            }
        }
        PakIFace->ClosePak();
    }

    Salamander->CloseProgressDialog();

    PAKReleaseIFace(PakIFace);
    return ret;
}

BOOL CPluginInterfaceForArchiver::DeleteFiles(std::string_view archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::DeleteFiles(, , )");
    const wchar_t* nextName;
    std::string nextFull;
    BOOL isDir;
    std::string file;
    DWORD size;
    unsigned total;

    if (!archiveRoot.empty() && archiveRoot.front() == '\\')
        archiveRoot.remove_prefix(1);

    while ((nextName = next(NULL, 0, &isDir, NULL, NULL, nextParam, NULL)) != NULL)
    {
        if (JoinPakEntryName(archiveRoot, nextName, nextFull) != PakTextStatus::Success)
            return FALSE;
        const int len = static_cast<int>(nextFull.size());
        if (!ReadFirstPakEntry(PakIFace, file, size))
            return FALSE;
        while (!file.empty())
        {
            if (file.size() >= nextFull.size() &&
                CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE, nextFull.c_str(), len, file.c_str(), len) == CSTR_EQUAL &&
                (file[len] == 0 || file[len] == '\\'))
            {
                if (!PakIFace->MarkForDelete())
                    return FALSE;
                if (!isDir)
                    break;
            }
            if (!ReadNextPakEntry(PakIFace, file, size))
                return FALSE;
        }
    }
    Salamander->ProgressDialogAddText(LangStr(IDS_DELFILES).c_str(), TRUE);
    if (!PakIFace->GetDelProgressTotal(&total))
        return FALSE;
    ProgressTotal = CQuadWord(total, 0);
    BOOL opt;
    if (!PakIFace->DeleteFiles(&opt))
    {
        if (opt)
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_NEEDOPT).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
        return FALSE;
    }
    return TRUE;
}

BOOL CPluginInterfaceForArchiver::DeleteFromArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                    CPluginDataInterfaceAbstract* pluginData, const wchar_t* archiveRoot,
                                                    SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::DeleteFromArchive(, %ls, , %ls, ,)", fileName, archiveRoot);

    PakIFace = PAKGetIFace();
    if (!PakIFace)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    BOOL ret = TRUE;
    Salamander = salamander;
    PakFileName = fileName;

    CPakCallbacks pakCalls(this);
    PakIFace->Init(&pakCalls);
    const std::wstring title = SPLFormatStringOwned(
        LangStr(IDS_DELPROGTITLE).c_str(), SalamanderGeneral->SalPathFindFileName(PakFileName));
    Salamander->OpenProgressDialog(title.c_str(), TRUE, NULL, FALSE);
    Salamander->ProgressDialogAddText(LangStr(IDS_PREPAREDATA).c_str(), FALSE);

    if (!PakIFace->OpenPak(fileName, OP_WRITE_MODE))
        ret = FALSE;
    else
    {
        Salamander->ProgressDialogAddText(LangStr(IDS_EXTRACTFILES).c_str(), FALSE);
        std::string arcRootStr;
        if (EncodePakEntryName(archiveRoot, arcRootStr) != PakTextStatus::Success ||
            !DeleteFiles(arcRootStr, next, nextParam))
            ret = FALSE;
        PakIFace->ClosePak();
    }

    Salamander->CloseProgressDialog();

    PAKReleaseIFace(PakIFace);
    return ret;
}

BOOL CPakCallbacks::DelNotify(const char* fileName, unsigned fileProgressTotal)
{
    CALL_STACK_MESSAGE3("CPakCallbacks::DelNotify(%s, 0x%X)", fileName,
                        fileProgressTotal);
    try
    {
        std::wstring decoded;
        if (fileName == nullptr ||
            DecodePakText(fileName, decoded) != PakTextStatus::Success)
            return FALSE;
        const std::wstring message = LangStr(IDS_DELETING) + decoded;
        Plugin->Salamander->ProgressDialogAddText(message.c_str(), TRUE);
        if (!Plugin->Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
        {
            Plugin->Salamander->ProgressDialogAddText(LangStr(IDS_CANCELING).c_str(), FALSE);
            Plugin->Salamander->ProgressEnableCancel(FALSE);
            return FALSE;
        }
        Plugin->Salamander->ProgressSetTotalSize(CQuadWord(fileProgressTotal, 0), Plugin->ProgressTotal);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL CPakCallbacks::Read(void* buffer, DWORD size)
{
    CALL_STACK_MESSAGE2("CPakCallbacks::Read(, 0x%X)", size);
    try
    {
    if (size == 0)
        return TRUE;
    DWORD pos;
    while (1)
    {
        pos = SetFilePointer(Plugin->IOFile, 0, NULL, FILE_CURRENT);
        if (pos != 0xFFFFFFFF)
            break;
        const std::wstring message = LangStr(IDS_UNABLEGETFIELPOS) +
                                     SPLGetErrorTextOwned(SalamanderGeneral, GetLastError());
        if (Plugin->Silent & SF_IOERRORS)
        {
            Plugin->Abort = FALSE;
            return FALSE;
        }
        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, Plugin->IOFileName, message.c_str(), NULL))
        {
        case DIALOG_SKIPALL:
            Plugin->Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            Plugin->Abort = FALSE;
            return FALSE;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            return FALSE;
        }
    }
    DWORD read;
    while (1)
    {
        if (ReadFile(Plugin->IOFile, buffer, size, &read, NULL) && size == read)
            return TRUE;
        const std::wstring message = LangStr(IDS_UNABLEREAD) +
                                     SPLGetErrorTextOwned(SalamanderGeneral, GetLastError());
        if (Plugin->Silent & SF_IOERRORS)
        {
            Plugin->Abort = FALSE;
            return FALSE;
        }
        switch (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL, Plugin->IOFileName, message.c_str(), NULL))
        {
        case DIALOG_SKIPALL:
            Plugin->Silent |= SF_IOERRORS;
        case DIALOG_SKIP:
            Plugin->Abort = FALSE;
            return FALSE;
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            return FALSE;
        }
        if (!SafeSeek(pos))
            return FALSE;
    }
    }
    catch (...)
    {
        Plugin->Abort = FALSE;
        return FALSE;
    }
}

DWORD
CPluginInterfaceForMenuExt::GetMenuItemState(int id, DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::GetMenuItemState(%d, 0x%X)",
                        id, eventMask);
    if (id != OPTIMIZE_MENUID)
        return 0;
    /*if ((eventMask & (MENU_EVENT_ARCHIVE_FOCUSED | MENU_EVENT_DISK)) ==
      (MENU_EVENT_ARCHIVE_FOCUSED | MENU_EVENT_DISK) || eventMask & MENU_EVENT_THIS_PLUGIN)*/
    if (eventMask & MENU_EVENT_DISK &&
            eventMask & (MENU_EVENT_ARCHIVE_FOCUSED | MENU_EVENT_FILES_SELECTED) ||
        eventMask & (MENU_EVENT_THIS_PLUGIN_ARCH))
        return MENU_ITEM_STATE_ENABLED;
    return 0;
}

BOOL CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                                 int id, DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::ExecuteMenuItem(, , %d, 0x%X)", id,
                        eventMask);
    if (id != OPTIMIZE_MENUID)
        return FALSE;

    SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_SOURCE); // we treat this command as working with the path (it appears in Alt+F12)

    std::wstring pakFile;
    size_t archiveOffset = std::wstring::npos;
    BOOL selFiles = FALSE;
    int index = 0;

    InterfaceForArchiver.Salamander = salamander;
    if (!SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, pakFile, NULL, &archiveOffset))
        return FALSE;

    const BOOL panelShowsArchive = archiveOffset != std::wstring::npos;
    std::wstring diskDirectory;
    if (!panelShowsArchive)
    {
        diskDirectory = pakFile;
        selFiles = eventMask & MENU_EVENT_FILES_SELECTED;
    }

    InterfaceForArchiver.PakIFace = PAKGetIFace();
    if (!InterfaceForArchiver.PakIFace)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_LOWMEM).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        return FALSE;
    }
    CPakCallbacks pakCalls(&InterfaceForArchiver);
    InterfaceForArchiver.PakIFace->Init(&pakCalls);

    BOOL changesReported = FALSE; // helper variable - TRUE if path changes have already been reported
    do
    {
        if (panelShowsArchive)
        {
            pakFile.resize(archiveOffset);
        }
        else
        {
            const CFileData* fileData;
            if (selFiles)
                fileData = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, NULL);
            else
                fileData = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, NULL);
            if (!fileData)
                break; // end of enumeration, or an error (in the case of GetFocusedItem)
            pakFile = diskDirectory;
            SPLSalPathAppendOwned(pakFile, fileData->Name);
            DWORD attr = SalamanderGeneral->SalGetFileAttributes(pakFile.c_str());
            if (attr != 0xFFFFFFFF && attr & FILE_ATTRIBUTE_DIRECTORY)
                continue;
        }

        InterfaceForArchiver.PakFileName = pakFile.c_str();

        if (InterfaceForArchiver.PakIFace->OpenPak(pakFile.c_str(), OP_WRITE_MODE))
        {
            COptDlgData dlgData;
            InterfaceForArchiver.PakIFace->GetOptimizedState(&dlgData.PakSize, &dlgData.ValData);
            if (DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_OPTIMIZE), parent,
                               OptimizeDlgProc, (LPARAM)&dlgData) == IDOK)
            {
                unsigned progress;
                if (InterfaceForArchiver.PakIFace->InitOptimalization(&progress))
                {
                    const std::wstring title = SPLFormatStringOwned(
                        LangStr(IDS_OPTPROGTITLE).c_str(),
                        SalamanderGeneral->SalPathFindFileName(pakFile.c_str()));
                    InterfaceForArchiver.Salamander->OpenProgressDialog(title.c_str(), FALSE, NULL, FALSE);
                    InterfaceForArchiver.Salamander->ProgressDialogAddText(LangStr(IDS_OPTIMIZING).c_str(), FALSE);
                    InterfaceForArchiver.Salamander->ProgressSetTotalSize(CQuadWord(progress, 0), CQuadWord(-1, -1));
                    InterfaceForArchiver.PakIFace->OptimizePak();
                    InterfaceForArchiver.Salamander->CloseProgressDialog();
                }

                if (!changesReported) // path change and it has not been reported yet -> report it
                {
                    changesReported = TRUE;
                    // announce the change on the path where the modified PAK files reside (the notification happens after leaving
                    // the plugin code - after returning from this method)
                    std::wstring pakFileDir = pakFile;
                    SPLCutDirectoryOwned(SalamanderGeneral, pakFileDir); // must work, because it is an existing file
                    SalamanderGeneral->PostChangeOnPathNotification(pakFileDir.c_str(), FALSE);
                }
            }
            InterfaceForArchiver.PakIFace->ClosePak();
        }

    } while (selFiles); // loop until the GetSelectedItem method returns NULL

    PAKReleaseIFace(InterfaceForArchiver.PakIFace);

    return TRUE;
}

BOOL WINAPI
CPluginInterfaceForMenuExt::HelpForMenuItem(HWND parent, int id)
{
    int helpID = 0;
    switch (id)
    {
    case OPTIMIZE_MENUID:
        helpID = IDH_OPTIMIZE;
        break;
    }
    if (helpID != 0)
        SalamanderGeneral->OpenHtmlHelp(parent, HHCDisplayContext, helpID, FALSE);
    return helpID != 0;
}

std::wstring PrintDiskSizeOwned(CQuadWord size)
{
    CALL_STACK_MESSAGE2("PrintDiskSize(, %g)", size.GetDouble());
    std::wstring result = SPLNumberToStrOwned(SalamanderGeneral, size);
    result += L" bytes";
    if (size.GetDouble() >= 1023.5)
    {
        result += L" (";
        result += SPLPrintDiskSizeOwned(SalamanderGeneral, size, 0);
        result += L')';
    }
    return result;
}

INT_PTR WINAPI OptimizeDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("OptimizeDlgProc(, 0x%X, 0x%IX, 0x%IX)", uMsg, wParam,
                        lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // SalamanderGUI->ArrangeHorizontalLines(hDlg); // should be called, but we skip it here; there are no horizontal lines
        if (((COptDlgData*)lParam)->PakSize <= ((COptDlgData*)lParam)->ValData)
        {
            EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
            SendMessage(hDlg, DM_SETDEFID, IDCANCEL, 0);
        }
        try
        {
            const COptDlgData* data = reinterpret_cast<const COptDlgData*>(lParam);
            const std::wstring validSize = PrintDiskSizeOwned(CQuadWord(data->ValData, 0));
            const std::wstring totalSize = PrintDiskSizeOwned(CQuadWord(data->PakSize, 0));
            const std::wstring wastedSpace = PrintDiskSizeOwned(CQuadWord(data->PakSize - data->ValData, 0));
            SetDlgItemTextW(hDlg, IDC_VALIDSIZE, validSize.c_str());
            SetDlgItemTextW(hDlg, IDC_TOTALSIZE, totalSize.c_str());
            SetDlgItemTextW(hDlg, IDC_WASTEDSPACE, wastedSpace.c_str());
        }
        catch (...)
        {
            EndDialog(hDlg, IDCANCEL);
            return FALSE;
        }

        HWND hParent = GetParent(hDlg);
        if (hParent != NULL)
            SalamanderGeneral->MultiMonCenterWindow(hDlg, hParent, TRUE);
        return FALSE;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
            EndDialog(hDlg, IDOK);
            return FALSE;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return FALSE;
        }
    }
    }
    return FALSE;
}
