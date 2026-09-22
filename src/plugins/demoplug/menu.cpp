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

// ****************************************************************************
// MENU SECTION
// ****************************************************************************

DWORD WINAPI
CPluginInterfaceForMenuExt::GetMenuItemState(int id, DWORD eventMask)
{
    if (id == MENUCMD_DOPFILES)
    {
        // must be on disk or in our plugin
        if ((eventMask & (MENU_EVENT_DISK | MENU_EVENT_THIS_PLUGIN_ARCH)) == 0)
            return 0;

        // use either the selected files or the file under focus
        const CFileData* file = NULL;
        BOOL isDir;
        if ((eventMask & MENU_EVENT_FILES_SELECTED) == 0)
        {
            if ((eventMask & MENU_EVENT_FILE_FOCUSED) == 0)
                return 0;
            file = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, &isDir); // when nothing is selected the enumeration fails
        }

        BOOL ret = TRUE;
        int count = 0;

        // wide end to end - AgreeMask below is already wide, so the mask never
        // narrows and the conversion that used to sit at the AgreeMask call is gone.
        const std::wstring mask =
            SPLPrepareMaskOwned(SalamanderGeneral, L"*.dop");

        int index = 0;
        if (file == NULL)
            file = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);
        while (file != NULL)
        {
            if (!isDir && SalamanderGeneral->AgreeMask(file->Name, mask.c_str(), *file->Ext != 0))
                count++;
            else
            {
                ret = FALSE;
                break;
            }
            file = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);
        }

        return (count != 0 && ret) ? MENU_ITEM_STATE_ENABLED : 0; // all selected files are *.dop
    }
    else
    {
        if (id == MENUCMD_SEP || id == MENUCMD_HIDDENITEM) // handle the separator and the item hidden when Shift is held
        {
            return MENU_ITEM_STATE_ENABLED |
                   ((GetKeyState(VK_SHIFT) & 0x8000) ? MENU_ITEM_STATE_HIDDEN : 0);
        }
        else
            TRACE_E("Unexpected call to CPluginInterfaceForMenuExt::GetMenuItemState()");
    }
    return 0;
}

struct CTestItem
{
    int a;
    char b[100];
    CTestItem(int i)
    {
        a = i;
        b[0] = 0;
    }
    ~CTestItem()
    {
        a = 0;
    }
};

struct CDEMOPLUGOperFromDiskData
{
    CQuadWord* RetSize;
    HWND Parent;
    BOOL Success;
};

void WINAPI DEMOPLUGOperationFromDisk(const wchar_t* sourcePath, SalEnumSelection2 next,
                                      void* nextParam, void* param)
{
    CDEMOPLUGOperFromDiskData* data = (CDEMOPLUGOperFromDiskData*)param;
    CQuadWord* retSize = data->RetSize;

    data->Success = TRUE; // report the operation as successful for now

    BOOL isDir;
    const wchar_t* name;
    const wchar_t* dosName; // dummy
    CQuadWord size;
    DWORD attr;
    FILETIME lastWrite;
    CQuadWord totalSize(0, 0);
    int errorOccured;
    while ((name = next(data->Parent, 3, &dosName, &isDir, &size, &attr, &lastWrite,
                        nextParam, &errorOccured)) != NULL)
    {
        if (errorOccured == SALENUM_ERROR) // SALENUM_CANCEL cannot arrive here
            data->Success = FALSE;
        if (!isDir)
            totalSize += size;
    }
    if (errorOccured != SALENUM_SUCCESS)
    {
        data->Success = FALSE;
        // an enumeration error occurred and the user requested to cancel the operation
        // if (errorOccured == SALENUM_CANCEL);
    }

    *retSize = totalSize;
}

BOOL WINAPI
CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander,
                                            HWND parent, int id, DWORD eventMask)
{
    switch (id)
    {
    case MENUCMD_ALWAYS:
    {
        /*
      // test arraylt.h - TDirectArray
      TDirectArray<DWORD> arr(10, 5);
      int i;
      for (i = 0; i < 20; i++)
        arr.Add(i);
      arr.Delete(5);
      arr.Delete(10);
      arr.Delete(15);
      arr.Insert(5, 50);
      for (i = 0; i < arr.Count; i++)
      {
        int test = arr[i];
        if (test == 19)
        {
          TRACE_I("last item in array");
        }
      }

      // test arraylt.h - TIndirectArray
      TIndirectArray<CTestItem> arr2(10, 5);   // automatically uses dtDelete (individual elements should be deleted)
      for (i = 0; i < 20; i++)
        arr2.Add(new CTestItem(i));
      arr2.Delete(5);
      arr2.Delete(10);
      arr2.Delete(15);
      arr2.Insert(5, new CTestItem(50));
      for (i = 0; i < arr2.Count; i++)
      {
        CTestItem *test = arr2[i];
        if (test != NULL && test->a == 19)
        {
          TRACE_I("last item in array2");
        }
      }
//      arr2.DestroyMembers();   // if not called the destruction is performed automatically in arr2's destructor
*/
        /*      // select-all
      SalamanderGeneral->SelectAllPanelItems(PANEL_SOURCE, TRUE, TRUE);
*/
        /*
      // focus the first selected item or the first item
      int index = 0;
      BOOL isDir;
      const CFileData *file = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);
      if (file == NULL)
      {
        index = 0;
        file = SalamanderGeneral->GetPanelItem(PANEL_SOURCE, &index, &isDir);
      }
      if (file != NULL)
      {
        SalamanderGeneral->SetPanelFocusedItem(PANEL_SOURCE, file, TRUE);
      }
*/
        /*
      // invert the selection
      const CFileData *file;
      int index = 0;
      while (1)
      {
        file = SalamanderGeneral->GetPanelItem(PANEL_SOURCE, &index, NULL);
        if (file == NULL) break;
        SalamanderGeneral->SelectPanelItem(PANEL_SOURCE, file, !file->Selected);
      }
      SalamanderGeneral->RepaintChangedItems(PANEL_SOURCE);
*/
        /*
      char diskSize[100];
      SalamanderGeneral->PrintDiskSize(diskSize, 123456, 1);

      BOOL b1 = SalamanderGeneral->HasTheSameRootPath(ToWideArg("c:\\path1").c_str(), ToWideArg("c:\\path2").c_str());

      BOOL b2 = SalamanderGeneral->IsTheSamePath(ToWideArg("c:\\path").c_str(), ToWideArg("c:\\path\\").c_str());

      std::wstring root;
      // GetRootPath is wide; rootSize counts WCHARs.
      wchar_t rootPathW[MAX_PATH];
      int len = SalamanderGeneral->GetRootPath(rootPathW, _countof(rootPathW), ToWideArg("\\\\server\\share\\test\\path").c_str());
      WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, rootPathW, -1,
                          root, MAX_PATH, NULL, NULL);

      std::wstring path;
      strcpy(path, "\\\\server\\share\\test\\path");
      char *cutDir;
      BOOL b3 = SPLCutDirectoryOwned(SalamanderGeneral, path, &cutDir);
      strcpy(path, "\\\\server\\share\\test\\path");
      SPLSalPathAppendOwned(path, L"new");
      strcpy(path, "\\\\server\\share\\test\\path");
      SPLSalPathAddBackslashOwned(path);
      strcpy(path, "\\\\server\\share\\test\\path\\");
      SPLSalPathRemoveBackslashOwned(SalamanderGeneral, path);
      strcpy(path, "\\\\server\\share\\test\\file");
      SPLSalPathStripPathOwned(SalamanderGeneral, path);
      strcpy(path, "\\\\server\\share\\test\\file.ext");
      SPLSalPathAddExtensionOwned(SalamanderGeneral, path, L".txt");
      SPLSalPathRemoveExtensionOwned(SalamanderGeneral, path);
      SPLSalPathAddExtensionOwned(SalamanderGeneral, path, L".txt");
      strcpy(path, "\\\\server\\share\\test\\file.ext");
      SPLSalPathRenameExtensionOwned(SalamanderGeneral, path, L".txt");
      strcpy(path, "\\\\server\\share\\test\\file.ext");
      const char *name = SalamanderGeneral->SalPathFindFileName(path);

      strcpy(path, "path\\ignore\\..\\.\\name");
      int errTextID;
      std::wstring nextFocus;
      SalamanderGeneral->SalUpdateDefaultDir(TRUE);
      wchar_t pathW[MAX_PATH], nextFocusW[MAX_PATH];   // wide; sizes in WCHARs
      lstrcpynW(pathW, ToWideArg(path).c_str(), _countof(pathW));
      if (!SalamanderGeneral->SalGetFullName(pathW, &errTextID, L"c:\\junk",
                                             nextFocusW, _countof(nextFocusW), _countof(pathW)))
      {
        char buf[200];
        SalamanderGeneral->GetGFNErrorText(errTextID, buf, 200);
      }

      char buffer[50];
      char *res = SalamanderGeneral->NumberToStr(buffer, 1234567);
*/
        // test IsPluginInstalled
        //      BOOL installed = SalamanderGeneral->IsPluginInstalled("webviewer\\webviewer.dll");
        /*
      std::wstring path;   // owned; no caller buffer, no path ceiling
      BOOL havePath = SPLGetTargetDirectoryOwned(SalamanderGeneral, parent, parent, L"Change Directory",
                                                 L"Select the directory you want to visit.",
                                                 path, FALSE, L"C:\\");
*/
        /*
      int index = 0;
      const char *name, *table;
      while (SalamanderGeneral->EnumConversionTables(parent, &index, &name, &table))
      {
        TRACE_I("Conversion: " << (name == NULL ? "separator" : name));
      }
*/
        /*
      char codePage[101];
      SalamanderGeneral->GetWindowsCodePage(NULL, codePage);
      if (codePage[0] != 0)  // only if WindowsCodePage is known
      {
        char conversion[200];
        strcpy(conversion, "ISO-8859-2");  // original encoding (iso-2 in this example)
        strcat(conversion, codePage);  // append the destination encoding
        char table[256];
        if (SalamanderGeneral->GetConversionTable(NULL, table, conversion))
        {
          char buf[20] = "ľlu»oučký kůň";   // text to convert

          // RecognizeFileType test: verify whether 'buf' is detected as text with the ISO-2 code page
          BOOL isText;
          char recCodePage[101];
          SalamanderGeneral->RecognizeFileType(NULL, buf, strlen(buf), FALSE, &isText, recCodePage);

          char *s = buf;
          while (*s != 0)   // perform the conversion
          {
            *s = table[(unsigned char)*s];
            s++;
          }
          TRACE_I("After conversion: " << buf);   // print the result
        }
      }
*/
        /*
      // for simplicity we do not check whether something is selected or whether the focus is on the up-dir symbol
      CQuadWord size = CQuadWord(-1, -1);  // error
      CDEMOPLUGOperFromDiskData data;
      data.RetSize = &size;
      data.Parent = parent;
      data.Success = FALSE;
      SalamanderGeneral->CallPluginOperationFromDisk(PANEL_SOURCE, DEMOPLUGOperationFromDisk, &data);
      if (data.Success) TRACE_I("Selection size is " << size.GetDouble() << " bytes.");
      else TRACE_I("Not all files and directories were processed! Processed part of selection has size of " << size.GetDouble() << " bytes.");
*/
        /*
      std::wstring path;
      lstrcpyn(path, "F:\\DRIVE_D", path.Size());
      CQuadWord total;
      CQuadWord space;
      SalamanderGeneral->GetDiskFreeSpace(&space, ToWideArg(path).c_str(), &total);
      TRACE_I("Disk free space: " << space.GetDouble() << " from " << total.GetDouble() << " bytes.");
      DWORD a, b, donot_use_1, donot_use_2;
      if (SalamanderGeneral->SalGetDiskFreeSpace(path, &a, &b, &donot_use_1, &donot_use_2))
        TRACE_I("Disk parameters: " << a << ", " << b);
      std::wstring rootOrCurReparsePoint;
      char volumeNameBuffer[200];
      DWORD volumeSerialNumber;
      DWORD maximumComponentLength;
      DWORD fileSystemFlags;
      char fileSystemNameBuffer[100];
      if (SalamanderGeneral->SalGetVolumeInformation(path, rootOrCurReparsePoint, volumeNameBuffer, 200,
                                                     &volumeSerialNumber, &maximumComponentLength,
                                                     &fileSystemFlags, fileSystemNameBuffer, 100))
      {
        TRACE_I("Disk info (" << path << "): " << rootOrCurReparsePoint << ", " << volumeNameBuffer << ", " <<
                volumeSerialNumber << ", " << maximumComponentLength << ", " << fileSystemFlags <<
                ", " << fileSystemNameBuffer);
      }
      TRACE_I("Disk type: " << SalamanderGeneral->SalGetDriveType(path));
*/
        /*
      const char *text = "This text is an example in which we will search for a pattern. The text can be arbitrarily long.";
      int textLen = strlen(text);
      CSalamanderBMSearchData *bm = SalamanderGeneral->AllocSalamanderBMSearchData();
      if (bm != NULL)
      {
        bm->Set("text", SASF_CASESENSITIVE | SASF_FORWARD);   // sample 'text' (case-sensitive), search forward
        if (bm->IsGood())
        {
          int found = -2;
          while (found != -1)
          {
            found = bm->SearchForward(text, textLen, max(found + 1, 0));
            if (found != -1) TRACE_I("boyer-moore - found: " << found);
          }
        }
        SalamanderGeneral->FreeSalamanderBMSearchData(bm);
      }
      CSalamanderREGEXPSearchData *regexp = SalamanderGeneral->AllocSalamanderREGEXPSearchData();
      if (regexp != NULL)
      {
        if (regexp->Set("(^| )t[^ ]*", 0))  // words (preceded by a space or line start) beginning
        {                                   // with 't'/'T' (case-insensitive), search backward
          regexp->SetLine(text, text + textLen);
          int found = textLen;
          while (found != -1)
          {
            int foundLen;
            found = regexp->SearchBackward(found, foundLen);
            if (found != -1) TRACE_I("regexp - found: " << found << ", len = " << foundLen);
          }
        }
        else   // error - malformed regular expression (mismatched brackets, etc.) or out of memory
        {
          SalamanderGeneral->ShowMessageBox(ToWideArg(regexp->GetLastErrorText()).c_str(), ToWideArg("Regular Expression Error").c_str(),
                                            MSGBOX_ERROR);
        }
        SalamanderGeneral->FreeSalamanderREGEXPSearchData(regexp);
      }
*/
        /*
      BOOL again = TRUE;
      while (again)
      {
        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = parent;
        params.Flags = MSGBOXEX_OK | MSGBOXEX_ICONINFORMATION | MSGBOXEX_SILENT;
        params.Caption = LangStr(IDS_PLUGINNAME).c_str();
        params.Text = "Command Always. Notice silent opening of this box.";
        params.CheckBoxText = "Show this box again?";
        params.CheckBoxValue = &again;
        SalamanderGeneral->SalMessageBoxEx(&params);
      }
*/
        /*
      char *buf = "We want to compute the CRC32 of this terribly long text.";
      char *end = buf + strlen(buf);
      char *s = buf;
      DWORD crc32 = 0;  // initialize to zero
      while (s < end)
      {
        // processing 10 bytes per call is only an example - chunked processing is handy e.g. when reading files
        // with file reads; otherwise the larger the buffer, the shorter the total computation time
        int size = min(end - s, 10);
        crc32 = SalamanderGeneral->UpdateCrc32(s, size, crc32);
        s += size;
      }
*/
        /*
      int selectedFiles, selectedDirs;
      BOOL work = SalamanderGeneral->GetPanelSelection(PANEL_SOURCE, &selectedFiles, &selectedDirs);
*/
        /*
      const char *s1 = "Bread";
      const char *s2 = "brick";
      //const char *s2 = "cHlEbA";
      int l1 = strlen(s1);
      int l2 = strlen(s2);
      int res;
      BOOL numericalyEqual;
      res = SalamanderGeneral->RegSetStrICmp(ToWideArg(s1).c_str(), ToWideArg(s2).c_str());
      res = SalamanderGeneral->RegSetStrICmpEx(ToWideArg(s1, l1).c_str(), -1, ToWideArg(s2, l2).c_str(), -1, &numericalyEqual);
      res = SalamanderGeneral->RegSetStrCmp(ToWideArg(s1).c_str(), ToWideArg(s2).c_str());
      res = SalamanderGeneral->RegSetStrCmpEx(ToWideArg(s1, l1).c_str(), -1, ToWideArg(s2, l2).c_str(), -1, &numericalyEqual);
*/
        /*
      std::wstring fileComp;
      BOOL ok;
      strcpy(fileComp, "prn");
      ok = SalamanderGeneral->SalIsValidFileNameComponent(ToWideArg(fileComp).c_str());
      SalamanderGeneral->SalMakeValidFileNameComponent(fileComp);
      strcpy(fileComp, "hello:");
      ok = SalamanderGeneral->SalIsValidFileNameComponent(ToWideArg(fileComp).c_str());
      SalamanderGeneral->SalMakeValidFileNameComponent(fileComp);
*/
        /*
      wchar_t masks[MAX_GROUPMASK];   // wide; MAX_GROUPMASK counts characters
      if (SalamanderGeneral->GetFilterFromPanel(PANEL_SOURCE, masks, MAX_GROUPMASK))
      {
        TRACE_IW(L"Filter in source panel: " << masks);
      }
*/
        /*
      std::wstring firstCreatedDir;
      BOOL ok = SalamanderGeneral->CheckAndCreateDirectory(L"C:\\test\\test_dir\\second_dir",   // wide
                                                           NULL, FALSE, NULL, 0,
                                                           firstCreatedDir);
*/
        /*
      GetAsyncKeyState(VK_ESCAPE);  // initialize GetAsyncKeyState - see the help
      HWND waitWndParent = SalamanderGeneral->GetMsgBoxParent();
      SalamanderGeneral->CreateSafeWaitWindow(ToWideArg("Waiting for ESC key, press ESC key...").c_str(), LangStr(IDS_PLUGINNAME).c_str(),
                                              1000, TRUE, waitWndParent);
      while (1)
      {
        Sleep(100);  // simulate some waiting (for example for a thread to finish)

        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8001) && GetForegroundWindow() == waitWndParent ||
            SalamanderGeneral->GetSafeWaitWindowClosePressed())
        {
          MSG msg;   // discard the buffered ESC
          while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE));

          SalamanderGeneral->ShowSafeWaitWindow(FALSE);
          if (SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(),
                                               "Do you really want to end this waiting?",
                                               LangStr(IDS_PLUGINNAME).c_str(),
                                               MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                               MB_ICONQUESTION) == IDYES)
          {
            break;
          }
          else UpdateWindow(waitWndParent);  // avoid staring at leftover message box contents
          SalamanderGeneral->ShowSafeWaitWindow(TRUE);
        }
      }
      SalamanderGeneral->DestroySafeWaitWindow();
*/

        /*
      // password mananger
      CSalamanderPasswordManagerAbstract *passwordManager = NULL;
      passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
      if (passwordManager != NULL)
      {
        // show safe/unsafe password storage
        if (passwordManager->IsUsingMasterPassword())
          TRACE_I("The Password Manager is using the Master Password, stored password are AES-protected.");
        else
          TRACE_I("The Master Password is not set! Stored passwords are not protected.");

        // encrypt (or scramble only) plain text password
        const char *plainPassword = "PlainTextPwd123";
        BYTE *encryptedPassword = NULL;
        int encryptedPasswordSize = 0;
        BOOL encrypt = FALSE; // FALSE: scramble only; TRUE: encrypted with AES

        if (passwordManager->IsUsingMasterPassword() &&
            (passwordManager->IsMasterPasswordSet() || passwordManager->AskForMasterPassword(parent)))
        {
          encrypt = TRUE; // use AES
        }

        if (passwordManager->EncryptPassword(plainPassword, &encryptedPassword, &encryptedPasswordSize, encrypt))
        {
          char *decryptedPassword;
          if (passwordManager->DecryptPassword(encryptedPassword, encryptedPasswordSize, &decryptedPassword))
          {
            TRACE_I("Decrypted password: " << decryptedPassword);
            memset(decryptedPassword, 0, lstrlen(decryptedPassword));  // clear memory with plain password
            SalamanderGeneral->Free(decryptedPassword);
          }

          if (!encrypt) memset(encryptedPassword, 0, encryptedPasswordSize);  // clear memory with scrambled password
          SalamanderGeneral->Free(encryptedPassword);
        }
      }
*/

        /*
      // GetFocusedItemMenuPos() sample -- display context menu for focused item in active panel
      POINT p;
      SalamanderGeneral->GetFocusedItemMenuPos(&p);
      HMENU hMenu = CreatePopupMenu();
      AppendMenu(hMenu, MF_STRING | MF_ENABLED, 1, "Item 1");
      AppendMenu(hMenu, MF_STRING | MF_ENABLED, 2, "Item 2");
      AppendMenu(hMenu, MF_STRING | MF_ENABLED, 3, "Item 3");
      BOOL cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN, p.x, p.y, 0, SalamanderGeneral->GetMainWindowHWND(), NULL);
      DestroyMenu(hMenu);
      if (cmd != 0)
      {
      // handle commands
      }
*/

        /*
      // example of SafeWaitWindow usage including Escape/Close-click handling
      GetAsyncKeyState(VK_ESCAPE);  // init GetAsyncKeyState - see msdn documentation
      HWND waitWndParent = SalamanderGeneral->GetMainWindowHWND();
      SalamanderGeneral->CreateSafeWaitWindow(ToWideArg("aaa").c_str(), "bbb", 0, TRUE, waitWndParent);
      int i;
      for (i = 0; i < 100; i++) // for+Sleep() -- pretend some work; for this example only
      {
        Sleep(100); 
        BOOL cancel = FALSE;
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8001) && GetForegroundWindow() == waitWndParent)
        {
          MSG msg;   // remove ESC key from buffer
          while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE));
          cancel = TRUE;
        }
        else
        {
          cancel = SalamanderGeneral->GetSafeWaitWindowClosePressed();
        }
  
        if (cancel)
        {
          SalamanderGeneral->ShowSafeWaitWindow(FALSE);
          if (SalamanderGeneral->SalMessageBox(waitWndParent,
                                               "Are you sure?",
                                               LangStr(IDS_PLUGINNAME).c_str(),
                                               MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                               MB_ICONQUESTION) == IDYES)
          {
            break;
          }
          SalamanderGeneral->ShowSafeWaitWindow(TRUE);
        }
      }
      SalamanderGeneral->DestroySafeWaitWindow();
*/

        //      SalamanderGeneral->PostOpenPackDlgForThisPlugin(0);
        //      SalamanderGeneral->PostOpenUnpackDlgForThisPlugin(NULL);

        SalamanderGeneral->ShowMessageBox(L"Always", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
        SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_SOURCE); // treat this command as working with the path (it appears in Alt+F12)
        break;
    }

    case MENUCMD_DIR:
        SalamanderGeneral->ShowMessageBox(L"Directory", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
        break;
    case MENUCMD_ARCFILE:
        SalamanderGeneral->ShowMessageBox(L"Archive File", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
        break;
    case MENUCMD_FILEONDISK:
        SalamanderGeneral->ShowMessageBox(L"File on Disk", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
        break;
    case MENUCMD_ARCFILEONDISK:
        SalamanderGeneral->ShowMessageBox(L"Archive File on Disk", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
        break;

    case MENUCMD_DOPFILES:
    {
        // determine whether we work on the selection or the focused item
        BOOL focus = FALSE;
        if ((eventMask & MENU_EVENT_FILES_SELECTED) == 0)
        {
            if ((eventMask & MENU_EVENT_FILE_FOCUSED) == 0)
                return FALSE;
            focus = TRUE;
        }

        // perform the action in two phases - preparation + execution
        int count = 0;
        BOOL ret = TRUE;
        int stage;
        for (stage = 0; stage < 2; stage++)
        {
            if (stage == 1) // execution phase
            {
                salamander->OpenProgressDialog(L"Command \"*.D&OP File(s)\"", FALSE, NULL, FALSE);
                salamander->ProgressSetTotalSize(CQuadWord(count, 0), CQuadWord(-1, -1));
            }

            int index = 0;
            const CFileData* file;
            BOOL isDir;
            if (!focus)
                file = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);
            else
                file = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, &isDir);
            while (file != NULL)
            {
                // action on the 'file' entry
                if (stage == 0)
                    count++; // preparation - count the files
                else         // execution - advance the progress
                {
                    salamander->ProgressDialogAddText(file->Name, FALSE);
                    Sleep(500); // simulate some work
                    if (!salamander->ProgressAddSize(1, FALSE))
                    {
                        salamander->ProgressDialogAddText(L"canceling operation, please wait...", FALSE);
                        salamander->ProgressEnableCancel(FALSE);
                        Sleep(1000); // simulate the cleanup work
                        ret = FALSE; // Cancel -> keep the items selected
                        break;       // abort the action
                    }
                }
                if (!focus)
                    file = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);
                else
                    break;
            }

            if (stage == 1) // execution phase
            {
                Sleep(500); // simulate some work
                salamander->CloseProgressDialog();
            }
        }
        SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_SOURCE); // treat this command as working with the path (it appears in Alt+F12)
        return ret;
    }

    case MENUCMD_FILESDIRSINARC:
        SalamanderGeneral->ShowMessageBox(L"File(s) and/or Directory(ies) in Archive", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
        break;

    case MENUCMD_ENTERDISKPATH: // example of validating a path entered by the user
    {
        // proposed initial string - here just the Windows path from the target panel (otherwise empty)
        std::wstring path;
        int type;
        if (SPLGetPanelPathOwned(SalamanderGeneral, PANEL_TARGET, path, &type))
        {
            if (type != PATH_TYPE_WINDOWS)
                path.clear(); // accept only disk paths
        }

        // we need the current path to convert relative paths to absolute ones
        BOOL curPathIsDisk = FALSE;
        std::wstring curPath;
        if (SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, curPath, &type))
        {
            if (type != PATH_TYPE_WINDOWS)
                curPath.clear(); // accept only disk paths
            else
                curPathIsDisk = TRUE;
        }

        SalamanderGeneral->SalUpdateDefaultDir(TRUE); // refresh defaults before using SalParsePath

        BOOL filePath = FALSE; // TRUE/FALSE = path to a file/directory
        BOOL success = FALSE;  // TRUE = 'path' contains a valid file/directory path (no error occurred)
        while (1)              // keep asking until the path is valid or the user cancels
        {
            CPathDialog dlg(parent, path, &filePath);
            if (dlg.Execute() == IDOK)
            {
                // interpret the entered path
                const size_t len = path.size();
                BOOL backslashAtEnd = len > 0 && path[len - 1] == L'\\'; // a trailing backslash implies a directory
                BOOL mustBePath = len == 2 && towlower(path[0]) >= L'a' && towlower(path[0]) <= L'z' &&
                                  path[1] == L':'; // a path such as "c:" must stay a path even after expansion (not a file)
                int pathType;
                BOOL pathIsDir;
                size_t secondPartOffset = std::wstring::npos;
                if (SPLSalParsePathOwned(SalamanderGeneral, parent, path, pathType, pathIsDir,
                                         secondPartOffset, L"Path Error", curPathIsDisk,
                                         curPathIsDisk ? curPath.c_str() : NULL))
                {
                    const wchar_t* secondPart = path.c_str() + secondPartOffset;
                    if (pathType == PATH_TYPE_WINDOWS) // Windows path (disk + UNC)
                    {
                        if (pathIsDir) // the existing portion of the path is a directory
                        {
                            if (*secondPart != 0) // contains a non-existing path segment
                            {
                                if (filePath) // for a file path, ensure the missing part does not contain additional subdirectories
                                {
                                    const wchar_t* s = secondPart;
                                    while (*s != 0 && *s != L'\\')
                                        s++;
                                    if (*s == L'\\') // contains subdirectories which we cannot create, report an error
                                    {
                                        SalamanderGeneral->SalMessageBox(parent, L"Unable to create the file specified, because its path doesn't exist.",
                                                                         L"Path Error", MB_OK | MB_ICONEXCLAMATION);
                                        continue; // ask again
                                    }
                                }
                                else
                                {
                                    // error - the path must already exist
                                    SalamanderGeneral->SalMessageBox(parent, L"The path specified doesn't exist.",
                                                                     L"Path Error", MB_OK | MB_ICONEXCLAMATION);
                                    continue; // ask again
                                }
                            }
                        }
                        else // overwriting a file - 'secondPart' points to the file name within 'path'
                        {
                            if (!filePath)
                            {
                                // error - expected a file path, not a directory
                                SalamanderGeneral->SalMessageBox(parent, L"Unable to create the path specified, name has already been used for a file.",
                                                                 L"Path Error", MB_OK | MB_ICONEXCLAMATION);
                                continue; // ask again
                            }
                        }

                        success = TRUE; // 'path' is usable
                        break;
                    }
                    else // FS/archive path
                    {
                        if (pathType == PATH_TYPE_ARCHIVE && (backslashAtEnd || mustBePath)) // restore the removed backslash
                            SPLSalPathAddBackslashOwned(path);
                        // error - FS/archive paths are not supported; ask again
                        SalamanderGeneral->SalMessageBox(parent, L"File-system and archive paths are not supported here.",
                                                         L"Path Error", MB_OK | MB_ICONEXCLAMATION);
                    }
                }
                else
                {
                    // SalParsePath already reported the error; ask again
                }
            }
            else
                break; // cancel
        }

        if (success) // 'path' points to a file/directory, perform the requested action
        {
            // the DemoPlugin does not perform anything here
        }

        return FALSE; // do not deselect panel items
    }

    case MENUCMD_ALLUSERS:
    case MENUCMD_INTADVUSERS:
    case MENUCMD_ADVUSERS:
    {
        SalamanderGeneral->SalMessageBox(parent, L"Hello there!",
                                         L"User's skill level demonstration", MB_OK | MB_ICONINFORMATION);
        break;
    }

    case MENUCMD_SHOWCONTROLS:
    {
        CCtrlExampleDialog dlg(parent);
        dlg.Execute();
        break;
    }

    case MENUCMD_CHECKDEMOPLUGTMPDIR:
    {
        ClearTEMPIfNeeded(SalamanderGeneral->GetMsgBoxParent());
        return FALSE; // keep the selection
    }

    case MENUCMD_DISCONNECT_LEFT:
    case MENUCMD_DISCONNECT_RIGHT:
    case MENUCMD_DISCONNECT_ACTIVE:
    {
        int panel = PANEL_SOURCE;
        switch (id)
        {
        case MENUCMD_DISCONNECT_LEFT:
            panel = PANEL_LEFT;
            break;
        case MENUCMD_DISCONNECT_RIGHT:
            panel = PANEL_RIGHT;
            break;
        case MENUCMD_DISCONNECT_ACTIVE:
            panel = PANEL_SOURCE;
            break;
        }
        SalamanderGeneral->DisconnectFSFromPanel(parent, panel);
        return FALSE; // keep the selection
    }

    default:
        SalamanderGeneral->ShowMessageBox(L"Unknown command.", LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        break;
    }
    return FALSE; // keep the selection in the panel
}

BOOL WINAPI
CPluginInterfaceForMenuExt::HelpForMenuItem(HWND parent, int id)
{
    int helpID = 0;
    switch (id)
    {
    case MENUCMD_ENTERDISKPATH:
        helpID = IDH_ENTERDISKPATH;
        break;
    }
    if (helpID != 0)
        SalamanderGeneral->OpenHtmlHelp(parent, HHCDisplayContext, helpID, FALSE);
    return helpID != 0;
}

void WINAPI
CPluginInterfaceForMenuExt::BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander)
{
#ifdef ENABLE_DYNAMICMENUEXT
    salamander->AddMenuItem(0, L"E&nter Disk Path", SALHOTKEY('Z', HOTKEYF_CONTROL | HOTKEYF_SHIFT), MENUCMD_ENTERDISKPATH, FALSE, MENU_EVENT_TRUE, MENU_EVENT_DISK, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
    salamander->AddMenuItem(-1, L"&Disconnect", 0, MENUCMD_DISCONNECT_ACTIVE, FALSE, MENU_EVENT_TRUE, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);
    static int cycle = 0; // this menu has variant content: when opened for the first time, it has only two items, when opened for the second time it has five items, etc.
    if (cycle % 4 >= 1)
    {
        salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
        salamander->AddMenuItem(-1, L"&Always", 0, MENUCMD_ALWAYS, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1, L"D&irectory", 0, MENUCMD_DIR, FALSE, MENU_EVENT_TRUE, MENU_EVENT_DIR_FOCUSED, MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1, L"A&rchive File", 0, MENUCMD_ARCFILE, FALSE, MENU_EVENT_TRUE, MENU_EVENT_ARCHIVE_FOCUSED, MENU_SKILLLEVEL_ALL);
    }
    if (cycle % 4 >= 2)
    {
        salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
        salamander->AddMenuItem(-1, L"&File on Disk", 0, MENUCMD_FILEONDISK, FALSE,
                                MENU_EVENT_TRUE, MENU_EVENT_DISK | MENU_EVENT_FILE_FOCUSED, MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1, L"Ar&chive File on Disk", 0, MENUCMD_ARCFILEONDISK, FALSE, MENU_EVENT_TRUE,
                                MENU_EVENT_DISK | MENU_EVENT_ARCHIVE_FOCUSED, MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
        salamander->AddMenuItem(-1, L"*.D&OP File(s)", 0, MENUCMD_DOPFILES, TRUE, 0, 0, MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1, L"Fil&e(s) and/or Directory(ies) in Archive", 0, MENUCMD_FILESDIRSINARC, FALSE,
                                MENU_EVENT_FILE_FOCUSED | MENU_EVENT_DIR_FOCUSED |
                                    MENU_EVENT_FILES_SELECTED | MENU_EVENT_DIRS_SELECTED,
                                MENU_EVENT_THIS_PLUGIN_ARCH, MENU_SKILLLEVEL_ALL);
    }
    if (cycle % 4 >= 3)
    {
        salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
        salamander->AddSubmenuStart(-1, L"Skill Level Demo", 0, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE,
                                    MENU_SKILLLEVEL_BEGINNER | MENU_SKILLLEVEL_INTERMEDIATE | MENU_SKILLLEVEL_ADVANCED);
        salamander->AddMenuItem(-1, L"For Beginning, Intermediate, and Advanced Users", 0, MENUCMD_ALLUSERS, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE,
                                MENU_SKILLLEVEL_BEGINNER | MENU_SKILLLEVEL_INTERMEDIATE | MENU_SKILLLEVEL_ADVANCED);
        salamander->AddSubmenuStart(0, L"Intermediate and Advanced", 0, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE,
                                    MENU_SKILLLEVEL_INTERMEDIATE | MENU_SKILLLEVEL_ADVANCED);
        salamander->AddMenuItem(-1, L"For Intermediate and Advanced Users", 0, MENUCMD_INTADVUSERS, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE,
                                MENU_SKILLLEVEL_INTERMEDIATE | MENU_SKILLLEVEL_ADVANCED);
        salamander->AddMenuItem(0, L"For Advanced Users", 0, MENUCMD_ADVUSERS, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE,
                                MENU_SKILLLEVEL_ADVANCED);
        salamander->AddSubmenuEnd();
        salamander->AddSubmenuEnd();
        salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
        salamander->AddMenuItem(-1, L"&Controls provided by Open Salamander...", 0, MENUCMD_SHOWCONTROLS, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE,
                                MENU_SKILLLEVEL_BEGINNER | MENU_SKILLLEVEL_INTERMEDIATE | MENU_SKILLLEVEL_ADVANCED);
        salamander->AddMenuItem(-1, NULL, 0, MENUCMD_SEP, TRUE, 0, 0, MENU_SKILLLEVEL_ADVANCED); // separator
        salamander->AddMenuItem(-1, L"Press Shift key when opening menu to hide this item", 0, MENUCMD_HIDDENITEM, TRUE, 0, 0, MENU_SKILLLEVEL_ADVANCED);
    }
    cycle++;

    CGUIIconListAbstract* iconList = SalamanderGUI->CreateIconList();
    iconList->Create(16, 16, 1);
    HICON hIcon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_FS), IMAGE_ICON, 16, 16, SalamanderGeneral->GetIconLRFlags());
    iconList->ReplaceIcon(0, hIcon);
    DestroyIcon(hIcon);
    salamander->SetIconListForMenu(iconList); // Salamander takes care of destroying the icon list
#endif                                        // ENABLE_DYNAMICMENUEXT
}
