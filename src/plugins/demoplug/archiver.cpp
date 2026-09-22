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

#include "precomp.h"

// shared interface for archiver plugin data
CArcPluginDataInterface ArcPluginDataInterface;

// ****************************************************************************
// ARCHIVER SECTION
// ****************************************************************************

//
// ****************************************************************************
// CArcPluginDataInterface
//

// callback invoked by Salamander to obtain text
// see spl_com.h / FColumnGetText for details
void WINAPI GetSzText()
{
    if (*TransferIsDir && !(*TransferFileData)->SizeValid)
    {
        // TransferBuffer is wide; CopyMemory silently accepted the mismatched
        // narrow literal (void* args), a latent bug matching fs_init.cpp's column.Name fix.
        CopyMemory(TransferBuffer, L"Dir", 3 * sizeof(wchar_t));
        *TransferLen = 3;
    }
    else
        *TransferLen = swprintf_s(TransferBuffer, 50, L"%I64d", (*TransferFileData)->Size.Value);
}

void WINAPI
CArcPluginDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                   const CFileData* upperDir)
{
    view->GetTransferVariables(TransferFileData, TransferIsDir, TransferBuffer, TransferLen, TransferRowData,
                               TransferPluginDataIface, TransferActCustomData);

    // adjust columns only in detailed mode
    if (view->GetViewMode() == VIEW_MODE_DETAILED)
    {
        // try to find the standard Size column and insert after it; if it is not found,
        // append the column at the end
        int sizeIndex = view->GetColumnsCount();
        int i;
        for (i = 0; i < sizeIndex; i++)
            if (view->GetColumn(i)->ID == COLUMN_ID_SIZE)
            {
                sizeIndex = i + 1;
                break;
            }

        CColumn column;
        lstrcpyW(column.Name, L"Size2");
        lstrcpyW(column.Description, L"Size v jinem provedeni");
        column.GetText = GetSzText;
        column.SupportSorting = 1;
        column.LeftAlignment = 0;
        column.ID = COLUMN_ID_CUSTOM;
        column.Width = leftPanel ? LOWORD(Size2Width) : HIWORD(Size2Width);
        column.FixedWidth = leftPanel ? LOWORD(Size2FixedWidth) : HIWORD(Size2FixedWidth);
        view->InsertColumn(sizeIndex, &column);
    }
}

void WINAPI
CArcPluginDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (leftPanel)
        Size2FixedWidth = MAKELONG(newFixedWidth, HIWORD(Size2FixedWidth));
    else
        Size2FixedWidth = MAKELONG(LOWORD(Size2FixedWidth), newFixedWidth);
    if (newFixedWidth)
        ColumnWidthWasChanged(leftPanel, column, column->Width);
}

void WINAPI
CArcPluginDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    if (leftPanel)
        Size2Width = MAKELONG(newWidth, HIWORD(Size2Width));
    else
        Size2Width = MAKELONG(LOWORD(Size2Width), newWidth);
}

BOOL WINAPI
CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                         CSalamanderDirectoryAbstract* dir,
                                         CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, , ,)", fileName);
#ifndef DEMOPLUG_QUIET
    SalamanderGeneral->ShowMessageBox(L"CPluginInterfaceForArchiver::ListArchive", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
#endif // DEMOPLUG_QUIET

    // define which data fields in 'file' are valid
    dir->SetValidData(VALID_DATA_EXTENSION |
                      VALID_DATA_DOSNAME |
                      VALID_DATA_SIZE |
                      VALID_DATA_TYPE |
                      VALID_DATA_DATE |
                      VALID_DATA_TIME |
                      VALID_DATA_ATTRIBUTES |
                      VALID_DATA_HIDDEN |
                      VALID_DATA_ISLINK |
                      VALID_DATA_ISOFFLINE |
                      VALID_DATA_ICONOVERLAY);

    pluginData = &ArcPluginDataInterface;

    CFileData file;

    file.Name = SalamanderGeneral->DupStr(L"test.dop");
    if (file.Name == NULL)
    {
        dir->Clear(pluginData);
        return FALSE;
    }
    file.NameLen = (int)wcslen(file.Name);
    wchar_t* s = wcsrchr(file.Name, L'.');
    if (s != NULL)
        file.Ext = s + 1; // ".cvspass" is a Windows extension...
    else
        file.Ext = file.Name + file.NameLen;
    file.Size = CQuadWord(666, 0);
    file.Attr = FILE_ATTRIBUTE_ARCHIVE;
    file.Hidden = 0;
    file.PluginData = 666; // redundant, just for show

    SYSTEMTIME st;
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, &file.LastWrite);
    /*
  SYSTEMTIME t;
  t.wYear = yr;
  if (t.wYear < 100)
  {
    if (t.wYear < 80) t.wYear += 2000;
    else t.wYear += 1900;
  }
  t.wMonth = mo;
  t.wDayOfWeek = 0;     // ignored
  t.wDay = dy;
  t.wHour = hh;
  t.wMinute = mm;
  t.wSecond = 0;
  t.wMilliseconds = 0;
  FILETIME lt;
  SystemTimeToFileTime(&t, &lt);                   // local time
  LocalFileTimeToFileTime(&lt, &dir.LastWrite);    // system time (universal time)
*/
    file.DosName = NULL;
    file.IsLink = SalamanderGeneral->IsFileLink(file.Ext);
    file.IsOffline = 0;
    file.IconOverlayIndex = 1; // icon-overlay: slow file

    // automatically adds two directories "test" and "path" (because they do not yet exist)
    // so that it can add 'file'
    if (!dir->AddFile(L"test\\path", file, pluginData))
    {
        SalamanderGeneral->Free(file.Name);
        dir->Clear(pluginData);
        return FALSE;
    }

    // add two more files
    file.Name = SalamanderGeneral->DupStr(L"test2.txt");
    if (file.Name == NULL)
    {
        dir->Clear(pluginData);
        return FALSE;
    }
    file.NameLen = (int)wcslen(file.Name);
    s = wcsrchr(file.Name, L'.');
    if (s != NULL)
        file.Ext = s + 1; // ".cvspass" is a Windows extension...
    else
        file.Ext = file.Name + file.NameLen;
    file.Size = CQuadWord(555, 0);
    file.IsLink = SalamanderGeneral->IsFileLink(file.Ext);
    file.IconOverlayIndex = 0; // icon-overlay: shared
    if (!dir->AddFile(L"test\\path", file, pluginData))
    {
        SalamanderGeneral->Free(file.Name);
        dir->Clear(pluginData);
        return FALSE;
    }
    file.Name = SalamanderGeneral->DupStr(L"test3.txt");
    if (file.Name == NULL)
    {
        dir->Clear(pluginData);
        return FALSE;
    }
    file.NameLen = (int)wcslen(file.Name);
    s = wcsrchr(file.Name, L'.');
    if (s != NULL)
        file.Ext = s + 1; // ".cvspass" is a Windows extension...
    else
        file.Ext = file.Name + file.NameLen;
    file.Size = CQuadWord(444, 0);
    file.Attr |= FILE_ATTRIBUTE_ENCRYPTED;
    file.IsLink = SalamanderGeneral->IsFileLink(file.Ext);
    file.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED; // no icon overlay
    if (!dir->AddFile(L"test\\path", file, pluginData))
    {
        SalamanderGeneral->Free(file.Name);
        dir->Clear(pluginData);
        return FALSE;
    }

    int sortByExtDirsAsFiles;
    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                          sizeof(sortByExtDirsAsFiles), NULL);
    file.Name = SalamanderGeneral->DupStr(L"test");
    if (file.Name == NULL)
    {
        dir->Clear(pluginData);
        return FALSE;
    }
    file.NameLen = (int)wcslen(file.Name);
    if (!sortByExtDirsAsFiles)
        file.Ext = file.Name + file.NameLen; // directories have no extension
    else
    {
        s = wcsrchr(file.Name, L'.');
        if (s != NULL)
            file.Ext = s + 1; // ".cvspass" is a Windows extension...
        else
            file.Ext = file.Name + file.NameLen;
    }
    file.Size = CQuadWord(0, 0);
    file.Attr = FILE_ATTRIBUTE_DIRECTORY;
    file.Hidden = 0;
    file.PluginData = 666; // redundant, just for show
    file.IsLink = 0;
    file.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED; // no icon overlay

    // change the data of the "test" directory (created automatically, see the previous AddFile) to 'file'
    if (!dir->AddDir(L"", file, pluginData))
    {
        SalamanderGeneral->Free(file.Name);
        dir->Clear(pluginData);
        return FALSE;
    }

    return TRUE;
}

BOOL WINAPI
CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander,
                                           const wchar_t* fileName, CPluginDataInterfaceAbstract* pluginData,
                                           const wchar_t* targetDir, const wchar_t* archiveRoot,
                                           SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName,
                        targetDir, archiveRoot);
#ifndef DEMOPLUG_QUIET
    SalamanderGeneral->ShowMessageBox(L"CPluginInterfaceForArchiver::UnpackArchive", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
#endif // DEMOPLUG_QUIET

    // internal packers will probably use salCalls->SafeCreateFile for direct
    //   extraction
    // for the rest there is an approach that unpacks to a temporary directory and then
    //   moves the files to the correct location on disk (handles overwriting files, etc.):

    BOOL ret = FALSE;
    DWORD err;
    // SalGetTempFileName is wide; tmpNameSize counts WCHARs. targetDir/tmpExtractDir
    // are real on-disk paths now, so this stays wide throughout - no narrow round-trip needed.
    std::wstring tmpExtractDir;
    if (!SPLSalGetTempFileNameOwned(SalamanderGeneral, targetDir, L"Sal", tmpExtractDir, FALSE, &err))
    {
        TRACE_E("SalGetTempFileName() error: " << err);
    }
    else
    {
        BOOL isDir;
        CQuadWord size;
        CQuadWord totalSize(0, 0);
        const wchar_t* name;
        const CFileData* fileData;
        while ((name = next(NULL, 0, &isDir, &size, &fileData, nextParam, NULL)) != NULL)
        {
            totalSize += size;

            // building the list of files that should be extracted
        }

        /*  // repeat the enumeration (just as a demonstration of "resetting" the enumeration)
    totalSize = 0;
    next(NULL, -1, NULL, NULL, NULL, nextParam, NULL);
    while ((name = next(NULL, 0, &isDir, &size, &fileData, nextParam, NULL)) != NULL)
    {
      totalSize += size;

      // building the list of files that should be extracted
    }
*/

        BOOL delTempDir = TRUE;
        if (SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                             tmpExtractDir.c_str(), totalSize, L"Unpacking DemoPlug Archive"))
        {
            /*
      // demonstration of a progress dialog with a single progress bar
      salamander->OpenProgressDialog("Unpacking DemoPlug Archive", FALSE, NULL, FALSE);
      salamander->ProgressSetTotalSize(CQuadWord(30, 0), CQuadWord(-1, -1));
      // performing the extraction - the following methods are called sequentially:
      salamander->ProgressDialogAddText("preparing data...", FALSE); // delayedPaint==FALSE because we do not want to wait for the timer and we are not going to call ProgressAddSize
      Sleep(1000);  // activity simulation
      ret = TRUE;
      int c = 30;
      while (c--)
      {
        salamander->ProgressDialogAddText("test text", TRUE);  // delayedPaint==TRUE so that we do not slow the UI down
        Sleep(50);  // activity simulation
        if (!salamander->ProgressAddSize(1, TRUE))  // delayedPaint==TRUE so that we do not slow the UI down
        {
          salamander->ProgressDialogAddText("canceling operation, please wait...", FALSE);
          salamander->ProgressEnableCancel(FALSE);
          Sleep(1000);  // cleanup simulation
          ret = FALSE;
          break;   // cancel the action
        }
      }
      Sleep(500);  // activity simulation
      salamander->CloseProgressDialog();
*/

            // demonstration of a progress dialog with two progress bars
            salamander->OpenProgressDialog(L"Unpacking DemoPlug Archive", TRUE, NULL, FALSE);
            salamander->ProgressSetTotalSize(CQuadWord(30, 0), CQuadWord(90, 0));
            // performing the extraction - the following methods are called repeatedly:
            salamander->ProgressDialogAddText(L"preparing data...", FALSE); // delayedPaint==FALSE because we do not want to wait for the timer and we are not going to call ProgressAddSize
            Sleep(1000);                                                   // activity simulation
            ret = TRUE;
            int c = 90;
            while (c--)
            {
                salamander->ProgressDialogAddText(L"test text", TRUE); // delayedPaint==TRUE so that we do not slow the UI down
                Sleep(50);                                            // activity simulation
                if ((c + 1) % 30 == 0)                                // simulating "another file"
                {
                    // salamander->ProgressSetTotalSize(CQuadWord(30, 0), CQuadWord(-1, -1)); // configuring the "size" of the next file (commented out here so it is not repeatedly set to 30)
                    salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE); // delayedPaint==TRUE so that we do not slow the UI down
                }
                if (!salamander->ProgressAddSize(1, TRUE)) // delayedPaint==TRUE so that we do not slow the UI down
                {
                    salamander->ProgressDialogAddText(L"canceling operation, please wait...", FALSE);
                    salamander->ProgressEnableCancel(FALSE);
                    Sleep(1000); // cleanup simulation
                    ret = FALSE;
                    break; // cancel the action
                }
            }
            Sleep(500); // activity simulation
            salamander->CloseProgressDialog();

            // ret is TRUE on success; otherwise it remains FALSE
            if (ret)
            {
                // the files are unpacked in the temporary directory and must be placed
                if (!salamander->MoveFiles(tmpExtractDir.c_str(), targetDir, tmpExtractDir.c_str(), fileName))
                    delTempDir = FALSE;
            }
        }

        if (delTempDir)
            SalamanderGeneral->RemoveTemporaryDir(tmpExtractDir.c_str());
    }

    return ret;
}

BOOL WINAPI
CPluginInterfaceForArchiver::UnpackOneFile(CSalamanderForOperationsAbstract* salamander,
                                           const wchar_t* fileName, CPluginDataInterfaceAbstract* pluginData,
                                           const wchar_t* nameInArchive, const CFileData* fileData,
                                           const wchar_t* targetDir, const wchar_t* newFileName,
                                           BOOL* renamingNotSupported)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackOneFile(, %ls, , %ls, , %ls, ,)", fileName,
                        nameInArchive, targetDir);
#ifndef DEMOPLUG_QUIET
    SalamanderGeneral->ShowMessageBox(L"CPluginInterfaceForArchiver::UnpackOneFile", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
#endif // DEMOPLUG_QUIET

    if (newFileName != NULL)
    {
        *renamingNotSupported = TRUE;
        return FALSE;
    }

    // unpacking without a progress dialog

    // instead of unpacking we only create a test file
    // real on-disk target path - stays wide throughout, CreateFileW explicitly
    // (UNICODE is not defined in this build, so plain CreateFile still resolves narrow).
    std::wstring name(targetDir != NULL ? targetDir : L"");
    const wchar_t* lastComp = wcsrchr(nameInArchive, L'\\');
    if (lastComp != NULL)
        lastComp++;
    else
        lastComp = nameInArchive;
    SPLSalPathAppendOwned(name, lastComp);
    {
        HANDLE file = HANDLES_Q(CreateFileW(name.c_str(), GENERIC_WRITE,
                                            FILE_SHARE_READ, NULL,
                                            CREATE_ALWAYS,
                                            FILE_FLAG_SEQUENTIAL_SCAN,
                                            NULL));
        if (file != INVALID_HANDLE_VALUE)
        {
            ULONG written;
            WriteFile(file, "New File\r\n", 10, &written, NULL);
            HANDLES(CloseHandle(file));
            return TRUE; // the "unpacking" succeeded
        }
    }

    return FALSE;
}

BOOL WINAPI
CPluginInterfaceForArchiver::PackToArchive(CSalamanderForOperationsAbstract* salamander,
                                           const wchar_t* fileName, const wchar_t* archiveRoot,
                                           BOOL move, const wchar_t* sourcePath,
                                           SalEnumSelection2 next, void* nextParam)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::PackToArchive(, %ls, %ls, %d, %ls, ,)", fileName,
                        archiveRoot, move, sourcePath);

#ifndef DEMOPLUG_QUIET
    SalamanderGeneral->ShowMessageBox(L"CPluginInterfaceForArchiver::PackToArchive", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
#endif // DEMOPLUG_QUIET

    BOOL isDir;
    const wchar_t* name;
    const wchar_t* dosName; // dummy
    CQuadWord size;
    DWORD attr;
    FILETIME lastWrite;
    CQuadWord totalSize(0, 0);
    int errorOccured;

    // open progress dialog
    salamander->OpenProgressDialog(L"Packing DemoPlug Archive", FALSE, NULL, FALSE);
    salamander->ProgressDialogAddText(L"reading directory tree...", FALSE);

    while ((name = next(SalamanderGeneral->GetMsgBoxParent(), 3, &dosName, &isDir, &size,
                        &attr, &lastWrite, nextParam, &errorOccured)) != NULL)
    {
        if (errorOccured == SALENUM_ERROR) // SALENUM_CANCEL cannot appear here
            TRACE_I("Not all files and directories from disk will be packed.");

        totalSize += size;

        // building the list of files that should be packed
    }
    if (errorOccured != SALENUM_SUCCESS)
    {
        TRACE_I("Not all files and directories from disk will be packed.");
        // check whether an error occurred and the user requested to cancel the operation (Cancel button)
        if (errorOccured == SALENUM_CANCEL)
        {
            salamander->CloseProgressDialog();
            return FALSE;
        }
    }

    // real archive-file directory path (for TestFreeSpace) - stays wide, no
    // narrow round-trip needed now that fileName is wide.
    const wchar_t* s = wcsrchr(fileName, L'\\');
    const std::wstring archivePath = s != NULL ? std::wstring(fileName, s) : std::wstring();

    BOOL ret = FALSE;
    if (archivePath.empty() ||
        SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                         archivePath.c_str(), totalSize, L"Unpacking DemoPlug Archive"))
    {
        salamander->ProgressSetTotalSize(CQuadWord(100, 0) /*totalSize*/, CQuadWord(-1, -1));

        // perform the packing
        salamander->ProgressDialogAddText(L"test text 1", FALSE);
        Sleep(500); // activity simulation
        if (!salamander->ProgressAddSize(50, FALSE))
            ret = FALSE; // cancel the action
        else
        {
            salamander->ProgressDialogAddText(L"test text 2", FALSE);
            Sleep(500); // activity simulation
            if (!salamander->ProgressAddSize(50, FALSE))
                ret = FALSE; // cancel the action
            else
                ret = TRUE;
        }
        // ret is TRUE on success; otherwise it remains FALSE
    }
    salamander->CloseProgressDialog();

    // NOTE: do not forget to set the Archive attribute on the file (the archive file changed -> it must be marked for backup)

    return ret;
}

BOOL WINAPI
CPluginInterfaceForArchiver::DeleteFromArchive(CSalamanderForOperationsAbstract* salamander,
                                               const wchar_t* fileName, CPluginDataInterfaceAbstract* pluginData,
                                               const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::DeleteFromArchive(, %ls, , %ls, ,)",
                        fileName, archiveRoot);
#ifndef DEMOPLUG_QUIET
    SalamanderGeneral->ShowMessageBox(L"CPluginInterfaceForArchiver::DeleteFromArchive", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
#endif // DEMOPLUG_QUIET

    salamander->OpenProgressDialog(L"Deleting DemoPlug Archive Entries", FALSE, NULL, FALSE);
    salamander->ProgressDialogAddText(L"reading directory tree...", FALSE);

    BOOL ret = FALSE;
    BOOL isDir;
    CQuadWord size;
    CQuadWord totalSize(0, 0);
    const wchar_t* name;
    const CFileData* fileData;
    while ((name = next(NULL, 0, &isDir, &size, &fileData, nextParam, NULL)) != NULL)
    {
        totalSize += size;

        // building the list of files that should be deleted
    }

    salamander->ProgressSetTotalSize(CQuadWord(100, 0) /*totalSize*/, CQuadWord(-1, -1));

    // perform the deletion
    salamander->ProgressDialogAddText(L"test text 1", FALSE);
    Sleep(500); // activity simulation
    if (!salamander->ProgressAddSize(50, FALSE))
        ret = FALSE; // cancel the action
    else
    {
        salamander->ProgressDialogAddText(L"test text 2", FALSE);
        Sleep(500); // activity simulation
        if (!salamander->ProgressAddSize(50, FALSE))
            ret = FALSE; // cancel the action
        else
            ret = TRUE;
    }
    // ret is TRUE on success; otherwise it remains FALSE

    salamander->CloseProgressDialog();

    // NOTE: do not forget to set the Archive attribute on the file (the archive file changed -> it must be marked for backup)

    return ret;
}

/*
void EnumAllItems(CSalamanderDirectoryAbstract const *dir, char *path, int pathBufSize)
{
  int count = dir->GetFilesCount();
  int i;
  for (i = 0; i < count; i++)
  {
    CFileData const *file = dir->GetFile(i);
    TRACE_I("EnumAllItems(): file: " << path << (path[0] != 0 ? "\\" : "") << file->Name);
  }
  count = dir->GetDirsCount();
  int pathLen = strlen(path);
  for (i = 0; i < count; i++)
  {
    CFileData const *file = dir->GetDir(i);
    TRACE_I("EnumAllItems(): directory: " << path << (path[0] != 0 ? "\\" : "") << file->Name);
    // SPLSalPathAppendOwned(path, file->Name); // use an owned std::wstring in live code
    CSalamanderDirectoryAbstract const *subDir = dir->GetSalDir(i);
    EnumAllItems(subDir, path, pathBufSize);
    path[pathLen] = 0;
  }
}
*/

BOOL WINAPI
CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander,
                                                const wchar_t* fileName, const wchar_t* mask,
                                                const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)", fileName,
                        mask, targetDir, delArchiveWhenDone);
#ifndef DEMOPLUG_QUIET
    SalamanderGeneral->ShowMessageBox(L"CPluginInterfaceForArchiver::UnpackWholeArchive", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
#endif // DEMOPLUG_QUIET

    /*
  CSalamanderDirectoryAbstract *dir = SalamanderGeneral->AllocSalamanderDirectory(FALSE);
  if (dir != NULL)
  {
    CPluginDataInterfaceAbstract *pluginData = NULL;
    if (ListArchive(salamander, fileName, dir, pluginData))
    {
      std::wstring path;
      path[0] = 0;
      EnumAllItems(dir, path, path.Size());
      dir->Clear(pluginData);
      if (pluginData != NULL) PluginInterface.ReleasePluginDataInterface(pluginData);
    }
    SalamanderGeneral->FreeSalamanderDirectory(dir);
  }
*/

    BOOL ret = FALSE;
    if (delArchiveWhenDone)
        archiveVolumes->Add(fileName, -2); // FIXME: once the plugin learns multi-volume archives we must add all archive volumes here (to delete the entire archive)
    salamander->OpenProgressDialog(L"Unpacking DemoPlug Archive", FALSE, NULL, FALSE);
    salamander->ProgressSetTotalSize(CQuadWord(100, 0), CQuadWord(-1, -1));

    // perform the unpacking
    salamander->ProgressDialogAddText(L"test text 1", FALSE);
    Sleep(500); // activity simulation
    if (!salamander->ProgressAddSize(50, FALSE))
        ret = FALSE; // cancel the action
    else
    {
        salamander->ProgressDialogAddText(L"test text 2", FALSE);
        Sleep(500); // activity simulation
        if (!salamander->ProgressAddSize(50, FALSE))
            ret = FALSE; // cancel the action
        else
            ret = TRUE;
    }
    // ret is TRUE on success; otherwise it remains FALSE

    salamander->CloseProgressDialog();

    return ret;
}

BOOL WINAPI
CPluginInterfaceForArchiver::CanCloseArchive(CSalamanderForOperationsAbstract* salamander,
                                             const wchar_t* fileName, BOOL force, int panel)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::CanCloseArchive(, %ls, %d, %d)",
                        fileName, force, panel);
#ifdef DEMOPLUG_QUIET
    return TRUE;
#else  // DEMOPLUG_QUIET

    if (SalamanderGeneral->IsCriticalShutdown())
        return TRUE; // during a critical shutdown we do not ask any questions

    return force && SalamanderGeneral->ShowMessageBox(L"CPluginInterfaceForArchiver::CanCloseArchive (can close).\n"
                                                      L"Return is forced to TRUE.", LangStr(IDS_PLUGINNAME).c_str(),
                                                      MSGBOX_INFO) == IDOK ||
           SalamanderGeneral->ShowMessageBox(L"CPluginInterfaceForArchiver::CanCloseArchive (can close).\n"
                                             L"What should it return?", LangStr(IDS_PLUGINNAME).c_str(),
                                             MSGBOX_QUESTION) == IDYES;
#endif // DEMOPLUG_QUIET
}

static std::wstring GetMyDocumentsPath()
{
    PWSTR knownPath = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, NULL, &knownPath)))
    {
        std::wstring path(knownPath);
        CoTaskMemFree(knownPath);
        return path;
    }
    return std::wstring();
}

BOOL WINAPI
CPluginInterfaceForArchiver::GetCacheInfo(CSalamanderStringBuffer* tempPath, BOOL* ownDelete, BOOL* cacheCopies)
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::GetCacheInfo()");
    if (tempPath == NULL || ownDelete == NULL || cacheCopies == NULL)
        return FALSE;
    std::wstring path = GetMyDocumentsPath();
    SPLSalPathAppendOwned(path, L"DemoPlug Temporary Copies");
    if (path.empty())
        path.clear(); // error -> fall back to the system TEMP
    if (!sally::plugin_abi::WriteStringBuffer(*tempPath, path))
        return FALSE;
    *ownDelete = TRUE;
    *cacheCopies = FALSE;
    return TRUE;
}

void ClearTEMPIfNeeded(HWND parent)
{
    std::wstring tempRoot = GetMyDocumentsPath();
    if (!tempRoot.empty())
    {
        SPLSalPathAppendOwned(tempRoot, L"DemoPlug Temporary Copies");
        std::wstring searchPath(tempRoot);
        SPLSalPathAppendOwned(searchPath, L"SAL*.tmp");
        {
            TIndirectArray<wchar_t> tmpDirs(10, 50);

            WIN32_FIND_DATAW data;
            HANDLE find = HANDLES_Q(FindFirstFileW(searchPath.c_str(), &data));
            if (find != INVALID_HANDLE_VALUE)
            {
                do
                { // process all found directories (ignore search errors)
                    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcslen(data.cFileName) > 3)
                    {
                        wchar_t* s = data.cFileName + 3;
                        while (*s != 0 && *s != '.' &&
                               (*s >= '0' && *s <= '9' || *s >= 'a' && *s <= 'f' || *s >= 'A' && *s <= 'F'))
                            s++;
                        if (SalamanderGeneral->StrICmp(s, L".tmp") == 0) // matches "SAL" + hex number + ".tmp" = almost certainly our directory
                        {
                            wchar_t* tmp = SalamanderGeneral->DupStr(data.cFileName);
                            if (tmp != NULL)
                            {
                                tmpDirs.Add(tmp);
                                if (!tmpDirs.IsGood())
                                {
                                    SalamanderGeneral->Free(tmp);
                                    tmpDirs.ResetState();
                                }
                            }
                        }
                    }
                } while (FindNextFileW(find, &data));
                HANDLES(FindClose(find));
            }

            if (tmpDirs.IsGood() && tmpDirs.Count > 0)
            {
                MSGBOXEX_PARAMS params;
                const std::wstring caption = LangStr(IDS_PLUGINNAME);
                memset(&params, 0, sizeof(params));
                params.HParent = parent;
                params.Flags = MSGBOXEX_ABORTRETRYIGNORE | MSGBOXEX_ICONQUESTION | MSGBOXEX_DEFBUTTON3;
                params.Caption = caption.c_str();
                CQuadWord qwCount(tmpDirs.Count, 0);
                const std::wstring plural = SPLExpandPluralStringOwned(
                    SalamanderGeneral,
                    L"{!}Do you want to delete %d temporary director{y|1|ies} used "
                    L"by previous instances of DemoPlug plugin?",
                    1, &qwCount);
                const std::wstring text = SPLFormatStringOwned(plural.c_str(), tmpDirs.Count);
                params.Text = text.c_str();
                const std::wstring aliasBtnNames = SPLFormatStringOwned(
                    L"%d\t%s\t%d\t%s\t%d\t%s", DIALOG_ABORT, L"&Yes",
                    DIALOG_RETRY, L"&No", DIALOG_IGNORE, L"&Focus");
                params.AliasBtnNames = aliasBtnNames.c_str();
                int ret = SalamanderGeneral->SalMessageBoxEx(&params);
                if (ret == DIALOG_ABORT) // yes
                {
                    for (int i = 0; i < tmpDirs.Count; i++)
                    {
                        std::wstring tmpDir(tempRoot);
                        SPLSalPathAppendOwned(tmpDir, tmpDirs[i]);
                        SalamanderGeneral->RemoveTemporaryDir(tmpDir.c_str());
                    }
                }
                if (ret == IDIGNORE) // focus
                {
                    std::wstring tmpDir(tempRoot);
                    SPLSalPathAppendOwned(tmpDir, tmpDirs[0]);
                    SalamanderGeneral->FocusNameInPanel(PANEL_SOURCE, tmpDir.c_str(), L"");
                }
            }
        }
    }
    else
        TRACE_E("DemoPlug: Unable to clear TEMP directory: TEMP directory not defined!");
}

void WINAPI
CPluginInterfaceForArchiver::DeleteTmpCopy(const wchar_t* fileName, BOOL firstFile)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::DeleteTmpCopy(%ls, %d)", fileName, firstFile);

    /*
  // message box test (message boxes should be used only in an extreme situation) - it makes a mess
  // if another plugin has its own modal dialog open (cosmetic issue: the message box for this dialog is not modal
  // and after closing it activates the dialog's parent)
  char buf[500];
  sprintf(buf, "File \"%s\" will be deleted.", fileName);
  SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(),
                                   buf, LangStr(IDS_PLUGINNAME).c_str(),
                                   MB_OK | MB_ICONINFORMATION);
*/

    // if this is a critical shutdown it is not a good time for slow file deletion (our process will be killed soon),
    // at the first subsequent plugin start in the first Salamander instance this will be deleted "calmly",
    // we probably cannot come up with anything better
    if (SalamanderGeneral->IsCriticalShutdown())
        return;

    // demonstration of using the wait window
    static DWORD ti = 0; // time when deletion of the first file in the batch started (when deleting multiple files at once)
    DWORD showTime = 1000;
    if (firstFile)
        ti = GetTickCount(); // ensure that the wait window shows up after one second across the entire deletion batch
    else
    {
        DWORD work = GetTickCount() - ti; // how long the deletion has been running (since the first file in the batch)
        if (work < 1000)
            showTime -= work;
        else
            showTime = 0;
    }
    SalamanderGeneral->CreateSafeWaitWindow(L"Deleting temporary file unpacked from archive, please wait...",
                                            LangStr(IDS_PLUGINNAME).c_str(), showTime, FALSE /* TRUE if we can cancel the operation */,
                                            SalamanderGeneral->GetMsgBoxParent());
    // activity simulation (the window becomes visible after one second)
    Sleep(2000);

    // regular file deletion
    SalamanderGeneral->ClearReadOnlyAttr(fileName);

    if (DeleteFileW(fileName))
        TRACE_IW(L"Temporary copy from disk-cache (" << fileName << L") was deleted.");
    else
        TRACE_IW(L"Unable to delete temporary copy from disk-cache (" << fileName << L").");

    // close the wait window; the action finished
    SalamanderGeneral->DestroySafeWaitWindow();
}

BOOL WINAPI
CPluginInterfaceForArchiver::PrematureDeleteTmpCopy(HWND parent, int copiesCount)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::PrematureDeleteTmpCopy(, %d)", copiesCount);

    // if this is a critical shutdown it is not a good time for slow file deletion (our process will be killed soon),
    // at the first subsequent plugin start in the first Salamander instance this will be deleted "calmly",
    // we probably cannot come up with anything better
    if (SalamanderGeneral->IsCriticalShutdown())
        return FALSE; // during a critical shutdown we do not ask any questions

    const std::wstring message = SPLFormatStringOwned(
        L"%d temporary file(s) extracted from archive are still in use.\n"
        L"Do you want to delete them anyway?",
        copiesCount);
    return SalamanderGeneral->SalMessageBox(parent, message.c_str(), LangStr(IDS_PLUGINNAME).c_str(),
                                            MB_YESNO | MB_ICONQUESTION) == IDYES;
}
