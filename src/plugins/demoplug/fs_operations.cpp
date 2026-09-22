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

//
// ****************************************************************************
// CDeleteProgressDlg
//

CDeleteProgressDlg::CDeleteProgressDlg(HWND parent, CObjectOrigin origin)
    : CCommonDialog(HLanguage, IDD_PROGRESSDLG, parent, origin)
{
    ProgressBar = NULL;
    WantCancel = FALSE;
    LastTickCount = 0;
    TextCache.clear();
    TextCacheIsDirty = FALSE;
    ProgressCache = 0;
    ProgressCacheIsDirty = FALSE;
}

void CDeleteProgressDlg::Set(const wchar_t* fileName, DWORD progress, BOOL dalayedPaint)
{
    TextCache = fileName != NULL ? fileName : L"";
    TextCacheIsDirty = TRUE;

    if (progress != ProgressCache)
    {
        ProgressCache = progress;
        ProgressCacheIsDirty = TRUE;
    }

    if (!dalayedPaint)
        FlushDataToControls();
}

void CDeleteProgressDlg::EnableCancel(BOOL enable)
{
    if (HWindow != NULL)
    {
        HWND cancel = GetDlgItem(HWindow, IDCANCEL);
        if (IsWindowEnabled(cancel) != enable)
        {
            EnableWindow(cancel, enable);
            if (enable)
                SetFocus(cancel);
            PostMessage(cancel, BM_SETSTYLE, enable ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);

            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) // give the user a brief timeslice ...
            {
                if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
    }
}

BOOL CDeleteProgressDlg::GetWantCancel()
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, TRUE)) // give the user a brief timeslice ...
    {
        if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // repaint changed data (text + progress bars) every 100 ms
    DWORD ticks = GetTickCount();
    if (ticks - LastTickCount > 100)
    {
        LastTickCount = ticks;
        FlushDataToControls();
    }

    return WantCancel;
}

void CDeleteProgressDlg::FlushDataToControls()
{
    if (HWindow != NULL)
    {
        if (TextCacheIsDirty)
        {
            SetDlgItemTextW(HWindow, IDT_FILENAME, TextCache.c_str());
            TextCacheIsDirty = FALSE;
        }

        if (ProgressCacheIsDirty)
        {
            ProgressBar->SetProgress(ProgressCache, NULL);
            ProgressCacheIsDirty = FALSE;
        }
    }
}

INT_PTR
CDeleteProgressDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CPathDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // use the Salamander-styled progress bar
        ProgressBar = SalamanderGUI->AttachProgressBar(HWindow, IDP_PROGRESSBAR);
        if (ProgressBar == NULL)
        {
            DestroyWindow(HWindow); // error -> do not show the dialog
            return FALSE;           // stop processing
        }

        break; // let DefDlgProc handle focus
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDCANCEL)
        {
            if (!WantCancel)
            {
                FlushDataToControls();

                if (SalamanderGeneral->SalMessageBox(HWindow, L"Do you want to cancel delete?", L"DFS",
                                                     MB_YESNO | MB_ICONQUESTION) == IDYES)
                {
                    WantCancel = TRUE;
                    EnableCancel(FALSE);
                }
            }
            return TRUE;
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CPluginFSInterface
//

CPluginFSInterface::CPluginFSInterface()
{
    Path.clear();
    PathError = FALSE;
    FatalError = FALSE;
    CalledFromDisconnectDialog = FALSE;
}

void WINAPI
CPluginFSInterface::ReleaseObject(HWND parent)
{
    if (!Path.empty()) // if the FS is initialized, remove our disk-cache copies when closing
    {
        // build a unique name for this FS root in the disk cache (covers all files from this FS)
        std::wstring rootPath;
        if (!SPLGetRootPathOwned(SalamanderGeneral, Path.c_str(), rootPath))
            return;
        std::wstring uniqueFileName = AssignedFSName + L":" + rootPath;
        // filenames on disk are case-insensitive, the disk cache is case-sensitive, converting
        // to lowercase makes the disk cache behave case-insensitively as well.
        std::transform(uniqueFileName.begin(), uniqueFileName.end(), uniqueFileName.begin(),
                       [](wchar_t ch) { return (wchar_t)towlower(ch); });
        SalamanderGeneral->RemoveFilesFromCache(uniqueFileName.c_str());
    }
}

static BOOL CopyWideResult(const std::wstring& value, wchar_t* output, int outputSize)
{
    if (output == NULL || outputSize <= 0 || value.size() >= (size_t)outputSize)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    std::copy(value.begin(), value.end(), output);
    output[value.size()] = L'\0';
    return TRUE;
}

static std::wstring GetGFNErrorTextOwned(int error)
{
    std::wstring text;
    SPLGetGFNErrorTextOwned(SalamanderGeneral, error, text);
    return text;
}

BOOL WINAPI
CPluginFSInterface::GetRootPath(CSalamanderStringBuffer* userPart)
{
    std::wstring root;
    if (!Path.empty() && !SPLGetRootPathOwned(SalamanderGeneral, Path.c_str(), root))
        return FALSE;
    return userPart != NULL && sally::plugin_abi::WriteStringBuffer(*userPart, root);
}

BOOL WINAPI
CPluginFSInterface::GetCurrentPath(CSalamanderStringBuffer* userPart)
{
    return userPart != NULL && sally::plugin_abi::WriteStringBuffer(*userPart, Path);
}

BOOL WINAPI
CPluginFSInterface::GetFullName(CFileData& file, int isDir,
                                CSalamanderStringBuffer* fullNameBuffer)
{
    std::wstring fullName(Path);
    const BOOL ok = isDir == 2 ? SPLCutDirectoryOwned(SalamanderGeneral, fullName) :
                                (SPLSalPathAppendOwned(fullName, file.Name), TRUE);
    return ok && fullNameBuffer != NULL &&
           sally::plugin_abi::WriteStringBuffer(*fullNameBuffer, fullName);
}

BOOL WINAPI
CPluginFSInterface::GetFullFSPath(HWND parent, const wchar_t* fsName,
                                  CSalamanderStringBuffer* path, BOOL& success)
{
    if (Path.empty())
        return FALSE;

    std::wstring relativePath;
    if (path == NULL || !sally::plugin_abi::ReadStringBuffer(*path, relativePath))
    {
        success = FALSE;
        return FALSE;
    }
    std::wstring root;
    if (!SPLGetRootPathOwned(SalamanderGeneral, Path.c_str(), root))
        return FALSE;
    const size_t rootLen = root.size();
    if (relativePath.empty() || relativePath.front() != L'\\')
        root = Path;
    SPLSalPathAppendOwned(root, relativePath.c_str());
    if (root.size() < rootLen)
        SPLSalPathAddBackslashOwned(root);

    const std::wstring fullPath = std::wstring(fsName != NULL ? fsName : L"") + L":" + root;
    success = sally::plugin_abi::WriteStringBuffer(*path, fullPath);
    if (!success)
        SalamanderGeneral->SalMessageBox(parent, L"Unable to finish operation because of too long path.",
                                         L"DFS Error", MB_OK | MB_ICONEXCLAMATION);
    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
{
    return currentFSNameIndex == fsNameIndex && SalamanderGeneral->IsTheSamePath(Path.c_str(), userPart);
}

BOOL WINAPI
CPluginFSInterface::IsOurPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
{
    if (ConnectData.UseConnectData)
        return FALSE;
    return Path.empty() || SalamanderGeneral->HasTheSameRootPath(Path.c_str(), userPart);
}

BOOL WINAPI
CPluginFSInterface::ChangePath(int currentFSNameIndex, CSalamanderStringBuffer* fsName,
                               int fsNameIndex, const wchar_t* userPart,
                               CSalamanderStringBuffer* cutFileName, BOOL* pathWasCut,
                               BOOL forceRefresh, int mode)
{
    std::wstring fsNameValue;
    if (fsName == NULL || !sally::plugin_abi::ReadStringBuffer(*fsName, fsNameValue) ||
        (cutFileName != NULL &&
         !sally::plugin_abi::WriteStringBuffer(*cutFileName, std::wstring())))
        return FALSE;
    std::wstring requested = userPart != NULL ? userPart : L"";
    if (requested.empty() && ConnectData.UseConnectData)
        requested = ConnectData.UserPart;

#ifndef DEMOPLUG_QUIET
    const std::wstring prompt = SPLFormatStringOwned(
        L"What should ChangePath return (No==FALSE)?\n\nPath: %s:%s", fsNameValue.c_str(), requested.c_str());
    if (SalamanderGeneral->ShowMessageBox(prompt.c_str(), L"DFS", MSGBOX_QUESTION) == IDNO)
    {
        FatalError = FALSE;
        PathError = FALSE;
        return FALSE;
    }
    if (forceRefresh)
        SalamanderGeneral->ShowMessageBox(L"ChangePath should change path without any caching! (forceRefresh==TRUE)", L"DFS", MSGBOX_INFO);
#endif

    if (mode != 3 && (pathWasCut != NULL || cutFileName != NULL))
    {
        TRACE_E("Incorrect value of 'mode' in CPluginFSInterface::ChangePath().");
        mode = 3;
    }
    if (pathWasCut != NULL)
        *pathWasCut = FALSE;
    if (FatalError)
    {
        FatalError = FALSE;
        return FALSE;
    }

    SalamanderGeneral->SalUpdateDefaultDir(TRUE);
    int err = 0;
    std::wstring path(requested);
    if (SPLSalGetFullNameOwned(SalamanderGeneral, path, &err))
    {
        BOOL fileNameAlreadyCut = FALSE;
        if (PathError)
        {
            PathError = FALSE;
            if (!SPLCutDirectoryOwned(SalamanderGeneral, path))
                return FALSE;
            fileNameAlreadyCut = TRUE;
            if (pathWasCut != NULL)
                *pathWasCut = TRUE;
        }

        std::wstring errorText;
        for (;;)
        {
            DWORD attr = SalamanderGeneral->SalGetFileAttributes(path.c_str());
#ifndef DEMOPLUG_QUIET
            if (attr != 0xFFFFFFFF && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                const std::wstring existsPrompt = SPLFormatStringOwned(L"Press No if you don't want path \"%s\" to exist.", path.c_str());
                if (SalamanderGeneral->ShowMessageBox(existsPrompt.c_str(), L"DFS", MSGBOX_QUESTION) == IDNO)
                    attr = 0xFFFFFFFF;
            }
#endif
            if (attr != 0xFFFFFFFF && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                if (!errorText.empty())
                {
                    const std::wstring message = L"Path: " + requested + L"\nError: " + errorText;
                    SalamanderGeneral->ShowMessageBox(message.c_str(), L"DFS Error", MSGBOX_ERROR);
                }
                Path = path;
                return TRUE;
            }

            err = GetLastError();
            if ((mode != 3 && attr != 0xFFFFFFFF) || (mode != 1 && attr == 0xFFFFFFFF))
            {
                errorText = attr != 0xFFFFFFFF ? L"The path specified contains path to a file. Unable to open file." :
                                                SPLGetErrorTextOwned(SalamanderGeneral, err);
                if (mode == 3)
                    break;
            }

            const size_t separator = path.find_last_of(L"\\/");
            const std::wstring cut = separator == std::wstring::npos ? path : path.substr(separator + 1);
            if (!SPLCutDirectoryOwned(SalamanderGeneral, path))
            {
                errorText = SPLGetErrorTextOwned(SalamanderGeneral, err);
                break;
            }
            if (pathWasCut != NULL)
                *pathWasCut = TRUE;
            if (!fileNameAlreadyCut)
            {
                fileNameAlreadyCut = TRUE;
                if (cutFileName != NULL && attr != 0xFFFFFFFF &&
                    !sally::plugin_abi::WriteStringBuffer(*cutFileName, cut))
                    return FALSE;
            }
            else if (cutFileName != NULL &&
                     !sally::plugin_abi::WriteStringBuffer(*cutFileName, std::wstring()))
                return FALSE;
        }

        const std::wstring message = L"Path: " + requested + L"\nError: " + errorText;
        SalamanderGeneral->ShowMessageBox(message.c_str(), L"DFS Error", MSGBOX_ERROR);
    }
    else
    {
        const std::wstring message = L"Path: " + requested + L"\nError: " + GetGFNErrorTextOwned(err);
        SalamanderGeneral->ShowMessageBox(message.c_str(), L"DFS Error", MSGBOX_ERROR);
    }
    PathError = FALSE;
    return FALSE;
}

BOOL WINAPI
CPluginFSInterface::ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                    CPluginDataInterfaceAbstract*& pluginData,
                                    int& iconsType, BOOL forceRefresh)
{
#ifndef DEMOPLUG_QUIET
    if (SalamanderGeneral->ShowMessageBox(L"What should ListCurrentPath return (No==FALSE)?", L"DFS", MSGBOX_QUESTION) == IDNO)
    {
        PathError = TRUE; // simulate a path error -> ChangePath will start shortening it
        return FALSE;
    }
    if (forceRefresh)
    {
        SalamanderGeneral->ShowMessageBox(L"ListCurrentPath refreshes current path (should do it without any caching)! (forceRefresh==TRUE)", L"DFS", MSGBOX_INFO);
    }
#endif // DEMOPLUG_QUIET

    CFileData file;
    WIN32_FIND_DATAW data;
    std::wstring itemPath;

    pluginData = new CPluginFSDataInterface(Path.c_str());
    if (pluginData == NULL)
    {
        TRACE_E("Low memory");
        FatalError = TRUE;
        return FALSE;
    }
    iconsType = pitFromPlugin;

    std::wstring rootPath;
    SPLGetRootPathOwned(SalamanderGeneral, Path.c_str(), rootPath);
    const BOOL isRootPath = Path.length() <= rootPath.length();
    std::wstring searchPath(Path);
    SPLSalPathAppendOwned(searchPath, L"*.*");
    HANDLE find = HANDLES_Q(FindFirstFileW(searchPath.c_str(), &data));

    if (find == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES) // an actual error occurred
        {
            const std::wstring message = L"Path: " + Path + L"\nError: " + SPLGetErrorTextOwned(SalamanderGeneral, err);
            SalamanderGeneral->ShowMessageBox(message.c_str(), L"DFS Error", MSGBOX_ERROR);
            PathError = TRUE;
            goto ERR_3;
        }
    }

    /*
  //j.r.
  dir->SetFlags(SALDIRFLAG_CASESENSITIVE | SALDIRFLAG_IGNOREDUPDIRS);
  //j.r.
*/

    // declare which fields in 'file' are valid
    dir->SetValidData(VALID_DATA_EXTENSION |
                      /*VALID_DATA_DOSNAME |*/
                      VALID_DATA_SIZE |
                      VALID_DATA_TYPE |
                      VALID_DATA_DATE |
                      VALID_DATA_TIME |
                      VALID_DATA_ATTRIBUTES |
                      VALID_DATA_HIDDEN |
                      VALID_DATA_ISLINK |
                      VALID_DATA_ISOFFLINE |
                      VALID_DATA_ICONOVERLAY);

    int sortByExtDirsAsFiles;
    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                          sizeof(sortByExtDirsAsFiles), NULL);

    while (find != INVALID_HANDLE_VALUE)
    {
        if (data.cFileName[0] != 0 && wcscmp(data.cFileName, L".") != 0 && // skip "."
            (!isRootPath || wcscmp(data.cFileName, L"..") != 0))           // skip ".." at the root
        {
            file.Name = SalamanderGeneral->DupStr(data.cFileName);
            if (file.Name == NULL)
                goto ERR_2;
            file.NameLen = (int)wcslen(file.Name);
            if (!sortByExtDirsAsFiles && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                file.Ext = file.Name + file.NameLen; // directories have no extension
            }
            else
            {
                wchar_t* s;
                s = wcsrchr(file.Name, L'.');
                if (s != NULL)
                    file.Ext = s + 1; // Windows treats ".cvspass" as an extension...
                else
                    file.Ext = file.Name + file.NameLen;
            }
            file.Size = CQuadWord(data.nFileSizeLow, data.nFileSizeHigh);
            file.Attr = data.dwFileAttributes;
            file.LastWrite = data.ftLastWriteTime;
            file.Hidden = file.Attr & FILE_ATTRIBUTE_HIDDEN ? 1 : 0;
            // always set IconOverlayIndex; Salamander will ignore it if it doesn't apply
            // read-only = slow file overlay, system = shared overlay
            file.IconOverlayIndex = file.Attr & FILE_ATTRIBUTE_READONLY ? 1 : file.Attr & FILE_ATTRIBUTE_SYSTEM ? 0
                                                                                                                : ICONOVERLAYINDEX_NOTUSED;

            SHFILEINFOW shfi;
            itemPath = Path;
            SPLSalPathAppendOwned(itemPath, file.Name);
            BOOL isUpDir;
            isUpDir = wcscmp(file.Name, L"..") == 0;
            if (!isUpDir)
            {
                if (!SHGetFileInfoW(itemPath.c_str(), 0, &shfi, sizeof(shfi), SHGFI_TYPENAME))
                {
                    lstrcpyW(shfi.szTypeName, L"(error)");
                }
            }
            else
                lstrcpyW(shfi.szTypeName, L"Go to Upper Directory");

            CFSData* extData;
            extData = new CFSData(data.ftCreationTime, data.ftLastAccessTime, shfi.szTypeName);
            if (extData == NULL || !extData->IsGood())
                goto ERR_1;
            file.PluginData = (DWORD_PTR)extData;

            /*      if (data.cAlternateFileName[0] != 0)
      {
        file.DosName = SalamanderGeneral->DupStr(data.cAlternateFileName);
        if (file.DosName == NULL) goto ERR_1;
      }
      else */
            file.DosName = NULL;

            if (file.Attr & FILE_ATTRIBUTE_DIRECTORY)
                file.IsLink = 0; // simplify things (ignore volume mount points and junction points)
            else
                file.IsLink = SalamanderGeneral->IsFileLink(file.Ext);

            file.IsOffline = !isUpDir && (file.Attr & FILE_ATTRIBUTE_OFFLINE) ? 1 : 0;

            if ((file.Attr & FILE_ATTRIBUTE_DIRECTORY) == 0 && !dir->AddFile(NULL, file, pluginData) ||
                (file.Attr & FILE_ATTRIBUTE_DIRECTORY) != 0 && !dir->AddDir(NULL, file, pluginData))
            {
                if (file.DosName != NULL)
                    SalamanderGeneral->Free(file.DosName);
            ERR_1:
                if (extData != NULL)
                    delete extData;
                SalamanderGeneral->Free(file.Name);
            ERR_2:
                TRACE_E("Low memory");
                dir->Clear(pluginData);
                HANDLES(FindClose(find));
                FatalError = TRUE;
            ERR_3:
                delete pluginData;
                return FALSE;
            }
        }

        /*
    //j.r.
    // each name is inserted up to three times with different character casing
    static int cntr = 0;
    if (cntr > 1)
      cntr = 0;
    else
    {
      if (data.cFileName[cntr] != 0 && strcmp(data.cFileName, "..") != 0)
      {
        if (isupper(data.cFileName[cntr]))
          data.cFileName[cntr] = tolower(data.cFileName[cntr]);
        else
          data.cFileName[cntr] = toupper(data.cFileName[cntr]);
        cntr++;
        continue;
      }
      else
        cntr = 0;
    }
    //end j.r.
*/
        if (!FindNextFileW(find, &data))
        {
            HANDLES(FindClose(find));
            break; // end of enumeration
        }
    }

    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
{
    if (CalledFromDisconnectDialog)
    {
        detach = FALSE; // we want to close the FS in any case
        return TRUE;
    }
    if (!forceClose)
    {
        if (canDetach) // close+detach
        {
            int r = SalamanderGeneral->ShowMessageBox(L"What should TryCloseOrDetach return (Yes==close, No==detach, Cancel==reject)?", L"DFS", MSGBOX_EX_QUESTION);
            if (r == IDCANCEL)
                return FALSE;
            detach = r == IDNO;
            return TRUE;
        }
        else // close
        {
#ifdef DEMOPLUG_QUIET
            return TRUE;
#else  // DEMOPLUG_QUIET
            return SalamanderGeneral->ShowMessageBox(L"What should TryCloseOrDetach return (Yes==close, No==reject)?", L"DFS", MSGBOX_QUESTION) == IDYES;
#endif // DEMOPLUG_QUIET
        }
    }
    else // force close
    {
#ifndef DEMOPLUG_QUIET

        if (SalamanderGeneral->IsCriticalShutdown())
            return TRUE; // do not ask anything during a critical shutdown

        SalamanderGeneral->ShowMessageBox(L"TryCloseOrDetach: FS is forced to close.", L"DFS", MSGBOX_INFO);
#endif // DEMOPLUG_QUIET
        return TRUE;
    }
}

void WINAPI
CPluginFSInterface::Event(int event, DWORD param)
{
    const wchar_t* panelName = param == PANEL_LEFT ? L"left" : L"right";
    std::wstring message;
    if (event == FSE_CLOSEORDETACHCANCELED)
    {
        message = SPLFormatStringOwned(L"Close or detach of path \"%s\" was canceled (%s).", Path.c_str(), panelName);
#ifdef DEMOPLUG_QUIET
        TRACE_I("DemoPlug FS event");
#else  // DEMOPLUG_QUIET
        SalamanderGeneral->ShowMessageBox(message.c_str(), L"FS Event", MSGBOX_INFO);
#endif // DEMOPLUG_QUIET
    }

    if (event == FSE_OPENED)
    {
        message = SPLFormatStringOwned(L"Path \"%s\" was opened in %s panel.", Path.c_str(), panelName);
#ifdef DEMOPLUG_QUIET
        TRACE_I("DemoPlug FS event");
#else  // DEMOPLUG_QUIET
        SalamanderGeneral->ShowMessageBox(message.c_str(), L"FS Event", MSGBOX_INFO);
#endif // DEMOPLUG_QUIET
    }

    if (event == FSE_DETACHED)
    {
        LastDetachedFS = this;

        message = SPLFormatStringOwned(L"Path \"%s\" was detached (%s).", Path.c_str(), panelName);
#ifdef DEMOPLUG_QUIET
        TRACE_I("DemoPlug FS event");
#else  // DEMOPLUG_QUIET
        SalamanderGeneral->ShowMessageBox(message.c_str(), L"FS Event", MSGBOX_INFO);
#endif // DEMOPLUG_QUIET
    }

    if (event == FSE_ATTACHED)
    {
        if (this == LastDetachedFS)
            LastDetachedFS = NULL;

        message = SPLFormatStringOwned(L"Path \"%s\" was attached (%s).", Path.c_str(), panelName);
#ifdef DEMOPLUG_QUIET
        TRACE_I("DemoPlug FS event");
#else  // DEMOPLUG_QUIET
        SalamanderGeneral->ShowMessageBox(message.c_str(), L"FS Event", MSGBOX_INFO);
#endif // DEMOPLUG_QUIET
    }

    if (event == FSE_ACTIVATEREFRESH) // the user activated Salamander (switched from another application)
    {
        // refresh the path;
        // we are inside CPluginFSInterface, so RefreshPanelPath cannot be used
        //    SalamanderGeneral->PostRefreshPanelPath((int)param);
        SalamanderGeneral->PostRefreshPanelFS(this);

        message = SPLFormatStringOwned(L"Activate refresh on path \"%s\" (%s).", Path.c_str(), panelName);
#ifdef DEMOPLUG_QUIET
        TRACE_I("DemoPlug FS event");
#else  // DEMOPLUG_QUIET
        SalamanderGeneral->ShowMessageBox(message.c_str(), L"FS Event", MSGBOX_INFO);
#endif // DEMOPLUG_QUIET
    }

    /*  // simple test of receiving the "timer" event after the timer expires
  if (event == FSE_TIMER)
  {
    TRACE_I("CPluginFSInterface::Event(): timer event " << param);
    if (param == 1234)
    {
      SalamanderGeneral->AddPluginFSTimer(2000, this, 123456);
    }
    if (param == 123456)
    {
      SalamanderGeneral->AddPluginFSTimer(2000, this, 123452);
      SalamanderGeneral->AddPluginFSTimer(2000, this, 123452);
      SalamanderGeneral->AddPluginFSTimer(1500, this, 1234);
    }
  }
*/
}

DWORD WINAPI
CPluginFSInterface::GetSupportedServices()
{
    return FS_SERVICE_CONTEXTMENU |
           FS_SERVICE_SHOWPROPERTIES |
           FS_SERVICE_CHANGEATTRS |
           FS_SERVICE_COPYFROMDISKTOFS |
           FS_SERVICE_MOVEFROMDISKTOFS |
           FS_SERVICE_MOVEFROMFS |
           FS_SERVICE_COPYFROMFS |
           FS_SERVICE_DELETE |
           FS_SERVICE_VIEWFILE |
           FS_SERVICE_CREATEDIR |
           FS_SERVICE_ACCEPTSCHANGENOTIF |
           FS_SERVICE_QUICKRENAME |
           FS_SERVICE_COMMANDLINE |
           FS_SERVICE_SHOWINFO |
           FS_SERVICE_GETFREESPACE |
           FS_SERVICE_GETFSICON |
           FS_SERVICE_GETNEXTDIRLINEHOTPATH |
           FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM |
           FS_SERVICE_GETPATHFORMAINWNDTITLE;
}

BOOL WINAPI
CPluginFSInterface::GetChangeDriveOrDisconnectItem(const wchar_t* fsName, wchar_t*& title, HICON& icon, BOOL& destroyIcon)
{
    std::wstring txt = L"\t" + std::wstring(fsName != NULL ? fsName : L"") + L":" + Path + L"\t";
    for (size_t amp = 0; (amp = txt.find(L'&', amp)) != std::wstring::npos; amp += 2)
        txt.insert(amp, 1, L'&');
    // append information about free space
    CQuadWord space;
    SalamanderGeneral->GetDiskFreeSpace(&space, Path.c_str(), NULL);
    if (space != CQuadWord(-1, -1))
    {
        txt.append(SPLPrintDiskSizeOwned(SalamanderGeneral, space, 0));
    }
    title = SalamanderGeneral->DupStr(txt.c_str());
    if (title == NULL)
        return FALSE; // low-memory, no item will be shown

    std::wstring rootPath;
    if (!SPLGetRootPathOwned(SalamanderGeneral, Path.c_str(), rootPath))
        icon = NULL;
    else if (!SalamanderGeneral->GetFileIcon(rootPath.c_str(), &icon,
                                             SALICONSIZE_16, TRUE, TRUE))
        icon = NULL;
    // switched to our own implementation (lower memory use, working XOR icons)
    //SHFILEINFO shi;
    //if (SHGetFileInfo(txt, 0, &shi, sizeof(shi),
    //                  SHGFI_ICON | SHGFI_SMALLICON | SHGFI_SHELLICONSIZE))
    //{
    //  icon = shi.hIcon;  // icon successfully retrieved
    //}
    //else icon = NULL;  // no icon available
    destroyIcon = TRUE;
    return TRUE;
}

HICON WINAPI
CPluginFSInterface::GetFSIcon(BOOL& destroyIcon)
{
    HICON icon;
    std::wstring rootPath;
    if (!SPLGetRootPathOwned(SalamanderGeneral, Path.c_str(), rootPath) ||
        !SalamanderGeneral->GetFileIcon(rootPath.c_str(), &icon,
                                        SALICONSIZE_16, TRUE, TRUE))
        icon = NULL;
    // switched to our own implementation (lower memory use, working XOR icons)
    //SHFILEINFO shi;
    //if (SHGetFileInfo(root, 0, &shi, sizeof(shi),
    //                  SHGFI_ICON | SHGFI_SMALLICON | SHGFI_SHELLICONSIZE))
    //{
    //  icon = shi.hIcon;  // icon successfully retrieved
    //}
    //else icon = NULL;  // no icon available (the standard one will be used)
    destroyIcon = TRUE;
    return icon;
}

void WINAPI
CPluginFSInterface::GetDropEffect(const wchar_t* srcFSPath, const wchar_t* tgtFSPath,
                                  DWORD allowedEffects, DWORD keyState, DWORD* dropEffect)
{                                                                                       // if Copy and Move are both available, choose Move when both FS instances share the same root
    // AssignedFSName is this plugin's own registered FS-name identifier (ASCII-only,
    // plugin-internal, not user data) - its byte length equals its WCHAR length, so
    // AssignedFSNameLen is safe to use as an offset into the now-wide 'srcFSPath'/'tgtFSPath'.
    if ((*dropEffect & DROPEFFECT_MOVE) && *dropEffect != DROPEFFECT_MOVE &&            // otherwise there is no point in checking
        SalamanderGeneral->StrNICmp(srcFSPath, AssignedFSName.c_str(), AssignedFSNameLen) == 0) // only paths on our FS are relevant
    {
        const wchar_t* src = srcFSPath + AssignedFSNameLen + 1;
        const wchar_t* tgt = tgtFSPath + AssignedFSNameLen + 1;
        if (SalamanderGeneral->HasTheSameRootPath(src, tgt))
            *dropEffect = DROPEFFECT_MOVE;
    }
}

void WINAPI
CPluginFSInterface::GetFSFreeSpace(CQuadWord* retValue)
{
    if (Path.empty())
        *retValue = CQuadWord(-1, -1);
    else
        SalamanderGeneral->GetDiskFreeSpace(retValue, Path.c_str(), NULL);
}

BOOL WINAPI
CPluginFSInterface::GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset)
{
    const wchar_t* root = text; // pointer past the root portion of the path
    while (*root != 0 && *root != L':')
        root++;
    if (*root == L':')
    {
        root++;
        if (*root == L'\\') // UNC path
        {
            root++;
            int c = 3;
            while (*root != 0)
            {
                if (*root == L'\\' && --c == 0)
                    break;
                root++;
            }
        }
        else // standard path
        {
            int c = 3;
            while (*++root != 0 && --c)
                ;
        }
    }
    const wchar_t* s = text + offset;
    const wchar_t* end = text + pathLen;
    if (s >= end)
        return FALSE;
    if (s < root)
        offset = (int)(root - text);
    else
    {
        if (*s == '\\')
            s++;
        while (s < end && *s != '\\')
            s++;
        offset = (int)(s - text);
    }
    return s < end;
}

void WINAPI
CPluginFSInterface::ShowInfoDialog(const wchar_t* fsName, HWND parent)
{
    CQuadWord f;
    GetFSFreeSpace(&f);
    std::wstring num;
    if (f != CQuadWord(-1, -1))
        num = SPLPrintDiskSizeOwned(SalamanderGeneral, f, 1);
    else
        num = L"(unknown)";

    const std::wstring message = SPLFormatStringOwned(
        L"Path: %s:%s\nFree Space: %s", fsName, Path.c_str(), num.c_str());
    SalamanderGeneral->SalMessageBox(parent, message.c_str(), L"DFS Info", MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI
CPluginFSInterface::ExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command,
                                       int& selFrom, int& selTo)
{
    std::wstring value;
    if (command == NULL || !sally::plugin_abi::ReadStringBuffer(*command, value))
        return FALSE;
    const BOOL result = ExecuteCommandLineOwned(parent, value, selFrom, selTo);
    return sally::plugin_abi::WriteStringBuffer(*command, value) ? result : FALSE;
}

BOOL CPluginFSInterface::ExecuteCommandLineOwned(HWND parent, std::wstring& command,
                                                 int& selFrom, int& selTo)
{
    SalamanderGeneral->SalMessageBox(parent, command.c_str(), L"DFS Command", MB_OK | MB_ICONINFORMATION);
    command.clear();
    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::QuickRename(const wchar_t* fsName, int mode, HWND parent, CFileData& file,
                                BOOL isDir, CSalamanderStringBuffer* newName, BOOL& cancel)
{
    std::wstring value;
    if (newName == NULL || !sally::plugin_abi::ReadStringBuffer(*newName, value))
        return FALSE;
    const BOOL result = QuickRenameOwned(fsName, mode, parent, file, isDir, value, cancel);
    return sally::plugin_abi::WriteStringBuffer(*newName, value) ? result : FALSE;
}

BOOL CPluginFSInterface::QuickRenameOwned(const wchar_t* fsName, int mode, HWND parent,
                                          CFileData& file, BOOL isDir,
                                          std::wstring& newName, BOOL& cancel)
{
    // if the plugin opens its own dialog, it should use CSalamanderGeneralAbstract::AlterFileName
    // ('format' according to SalamanderGeneral->GetConfigParameter(SALCFG_FILENAMEFORMAT))
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // request the standard dialog

#ifndef DEMOPLUG_QUIET
    const std::wstring prompt = SPLFormatStringOwned(L"From: %s\nTo: %s", file.Name, newName.c_str());
    SalamanderGeneral->SalMessageBox(parent, prompt.c_str(), L"DFS Quick Rename", MB_OK | MB_ICONINFORMATION);
#endif // DEMOPLUG_QUIET

    // validate the entered name (syntactically)
    const wchar_t* s = newName.c_str();
    while (*s != 0 && *s != L'\\' && *s != L'/' && *s != L':' &&
           *s >= 32 && *s != L'<' && *s != L'>' && *s != L'|' && *s != L'"')
        s++;
    if (newName.empty() || *s != 0)
    {
        SalamanderGeneral->SalMessageBox(parent, SPLGetErrorTextOwned(SalamanderGeneral, ERROR_INVALID_NAME).c_str(),
                                         L"DFS Quick Rename Error", MB_OK | MB_ICONEXCLAMATION);
        return FALSE; // invalid name, let the user fix it
    }

    std::wstring maskedName;
    if (!SPLMaskNameOwned(SalamanderGeneral, file.Name, newName.c_str(), maskedName))
        return FALSE;
    newName = std::move(maskedName);

    std::wstring nameFrom(Path);
    std::wstring nameTo(Path);
    SPLSalPathAppendOwned(nameFrom, file.Name);
    SPLSalPathAppendOwned(nameTo, newName.c_str());
    if (!MoveFileW(nameFrom.c_str(), nameTo.c_str()))
    {
        // (overwriting is not handled here; treat it as an error as well)
        SalamanderGeneral->SalMessageBox(parent, SPLGetErrorTextOwned(SalamanderGeneral, GetLastError()).c_str(),
                                         L"DFS Quick Rename Error", MB_OK | MB_ICONEXCLAMATION);
        // 'newName' is returned after adjustment (mask applied)
        return FALSE; // error -> show the standard dialog again
    }
    else // operation succeeded - report the change on the path (trigger refresh) and report success
    {
        if (SalamanderGeneral->StrICmp(nameFrom.c_str(), nameTo.c_str()) != 0)
        { // if it is more than just a case change (DFS is not case-sensitive)
            // remove the source of the operation from the disk cache (the original name is no longer valid)
            std::wstring dfsFileName = std::wstring(fsName) + L":" + nameFrom;
            // filenames on disk are case-insensitive, the disk cache is case-sensitive, converting
            // to lowercase makes the disk cache behave case-insensitively as well
            std::transform(dfsFileName.begin(), dfsFileName.end(), dfsFileName.begin(),
                           [](wchar_t ch) { return (wchar_t)towlower(ch); });
            SalamanderGeneral->RemoveOneFileFromCache(dfsFileName.c_str());
            // if overwriting is possible, the destination should also be removed from the disk cache (a "file change" happened)
        }

        // report a change on the 'Path' path (without subdirectories when renaming files)
        // NOTE: a typical plugin should send the full FS path here
        SalamanderGeneral->PostChangeOnPathNotification(Path.c_str(), isDir);

        return TRUE;
    }
}

void WINAPI
CPluginFSInterface::AcceptChangeOnPathNotification(const wchar_t* fsName, const wchar_t* path, BOOL includingSubdirs)
{
#ifndef DEMOPLUG_QUIET
    const std::wstring prompt = SPLFormatStringOwned(L"Path: %s\nSubdirs: %s", path, includingSubdirs ? L"yes" : L"no");
    SalamanderGeneral->ShowMessageBox(prompt.c_str(), L"DFS Change On Path Notification", MSGBOX_INFO);
#endif // DEMOPLUG_QUIET

    // WARNING: a regular plugin should work with FS paths here
    // for DFS we simplify the logic to operate on disk paths because DFS
    // only exposes a disk path; see the WMobile implementation below

    // test whether the paths match or at least share a prefix (only disk paths matter;
    // FS paths in 'path' are excluded automatically because they can never match Path).
    std::wstring path1(path != NULL ? path : L"");
    std::wstring path2(Path);
    while (path1.size() > 1 && path1.back() == L'\\')
        path1.pop_back();
    while (path2.size() > 1 && path2.back() == L'\\')
        path2.pop_back();
    const int len1 = (int)path1.size();
    BOOL refresh = !includingSubdirs && SalamanderGeneral->StrICmp(path1.c_str(), path2.c_str()) == 0 ||
                   includingSubdirs && path2.size() >= path1.size() &&
                       SalamanderGeneral->StrNICmp(path1.c_str(), path2.c_str(), len1) == 0 &&
                       (path2.size() == path1.size() || path2[path1.size()] == L'\\');
    if (!refresh && SPLCutDirectoryOwned(SalamanderGeneral, path1))
    {
        while (path1.size() > 1 && path1.back() == L'\\')
            path1.pop_back();
        // on NTFS the last subdirectory's timestamp changes as well (it shows only after entering
        // that subdirectory; hopefully it will be fixed someday)
        refresh = SalamanderGeneral->StrICmp(path1.c_str(), path2.c_str()) == 0;
    }
    if (refresh)
    {
        SalamanderGeneral->PostRefreshPanelFS(this); // refresh the panel if this FS is displayed there
    }

    // example of an implementation from the WMobile plugin:
    /*
  // test whether the paths match or at least share a prefix (only paths on our FS qualify;
  // disk paths and paths on other FSs in 'path' are excluded automatically,
  // because they can never match 'fsName'+':' at the beginning of 'path2' below)
  char path1[2 * MAX_PATH];
  char path2[2 * MAX_PATH];
  lstrcpyn(path1, path, 2 * MAX_PATH);
  sprintf(path2, "%s:%s", fsName, (const char*)Path);
  SPLSalPathRemoveBackslashOwned(SalamanderGeneral, path1);
  SPLSalPathRemoveBackslashOwned(SalamanderGeneral, path2);
  int len1 = (int)strlen(path1);
  BOOL refresh = SalamanderGeneral->StrNICmp(ToWideArg(path1).c_str(), ToWideArg(path2).c_str(), (int)ToWideArg(path1, len1).size()) == 0 &&
                 (path2[len1] == 0 || includingSubdirs && path2[len1] == '\\');
  if (refresh)
    SalamanderGeneral->PostRefreshPanelFS(this);   // refresh the panel if this FS is displayed there
*/
}

BOOL WINAPI
CPluginFSInterface::CreateDir(const wchar_t* fsName, int mode, HWND parent,
                              CSalamanderStringBuffer* newName, BOOL& cancel)
{
    std::wstring value;
    if (newName == NULL || !sally::plugin_abi::ReadStringBuffer(*newName, value))
        return FALSE;
    const BOOL result = CreateDirOwned(fsName, mode, parent, value, cancel);
    return sally::plugin_abi::WriteStringBuffer(*newName, value) ? result : FALSE;
}

BOOL CPluginFSInterface::CreateDirOwned(const wchar_t* fsName, int mode, HWND parent,
                                        std::wstring& newName, BOOL& cancel)
{
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // request for the standard dialog

#ifndef DEMOPLUG_QUIET
    const std::wstring prompt = SPLFormatStringOwned(L"New directory: %s", newName.c_str());
    SalamanderGeneral->SalMessageBox(parent, prompt.c_str(), L"DFS Create Directory", MB_OK | MB_ICONINFORMATION);
#endif // DEMOPLUG_QUIET

    SalamanderGeneral->SalUpdateDefaultDir(TRUE); // update before using SalParsePath (internally uses SalGetFullName)

    std::wstring parsed(newName);
    std::wstring nextFocus;
    int type;
    BOOL isDir;
    size_t secondPartOffset = std::wstring::npos;
    int error;
    if (!SPLSalParsePathOwned(SalamanderGeneral, parent, parsed, type, isDir,
                              secondPartOffset, L"DFS Create Directory Error",
                              FALSE, NULL, &error))
    {
        if (error == SPP_EMPTYPATHNOTALLOWED) // empty string -> abort without performing the operation
        {
            cancel = TRUE;
            return TRUE; // the return value no longer matters
        }

        if (error == SPP_INCOMLETEPATH) // relative path on the FS; build an absolute path manually
        {
            const std::wstring entered(newName);
            const size_t slash = entered.find(L'\\');
            if (slash == std::wstring::npos || slash + 1 == entered.size())
            {
                nextFocus = slash == std::wstring::npos ? entered : entered.substr(0, slash);
            }

            std::wstring userPart;
            if (!entered.empty() && entered.front() == L'\\')
            {
                if (!SPLGetRootPathOwned(SalamanderGeneral, Path.c_str(), userPart))
                    return FALSE;
                userPart.append(entered.substr(1));
            }
            else
            {
                userPart = Path;
                SPLSalPathAppendOwned(userPart, entered.c_str());
            }
            parsed = std::wstring(fsName) + L":" + userPart;
            secondPartOffset = wcslen(fsName) + 1;
            type = PATH_TYPE_FS;
        }
        else
            return FALSE; // error -> show the standard dialog again
    }

    if (type != PATH_TYPE_FS)
    {
        SalamanderGeneral->SalMessageBox(parent, L"Sorry, but this plugin is not able "
                                                 L"to create directory on disk or archive path.",
                                         L"DFS Create Directory", MB_OK | MB_ICONEXCLAMATION);
        // 'newName' is returned already adjusted (expanded path)
        return FALSE; // error -> show the standard dialog again
    }

    if (secondPartOffset == std::wstring::npos || secondPartOffset == 0 ||
        secondPartOffset - 1 != wcslen(fsName) ||
        SalamanderGeneral->StrNICmp(parsed.c_str(), fsName, (int)secondPartOffset - 1) != 0)
    { // not a DFS path
        SalamanderGeneral->SalMessageBox(parent, L"Sorry, but this plugin is not able "
                                                 L"to create directory on specified file-system.",
                                         L"DFS Create Directory", MB_OK | MB_ICONEXCLAMATION);
        // 'newName' is returned already adjusted (expanded path)
        return FALSE; // error -> show the standard dialog again
    }

    std::wstring userPart = parsed.substr(secondPartOffset);
    if (!SalamanderGeneral->HasTheSameRootPath(Path.c_str(), userPart.c_str()))
    { // DFS operates only within a single drive (e.g. FTP would also have to check
        // whether the user is trying to create a directory on another server)
        SalamanderGeneral->SalMessageBox(parent, L"Sorry, but this plugin is not able "
                                                 L"to create directory outside of currently opened drive.",
                                         L"DFS Create Directory", MB_OK | MB_ICONEXCLAMATION);
        // 'newName' is returned already adjusted (expanded path)
        return FALSE; // error -> show the standard dialog again
    }

    // the full path on this FS may contain "." and ".." - remove them
    std::wstring rootPath;
    if (!SPLGetRootPathOwned(SalamanderGeneral, userPart.c_str(), rootPath))
        return FALSE;
    const size_t rootLen = (std::min)(rootPath.size(), userPart.size());
    if (!SPLSalRemovePointsFromPathOwned(SalamanderGeneral, userPart,
                                         rootLen))
    {
        SalamanderGeneral->SalMessageBox(parent, L"The path specified is invalid.",
                                         L"DFS Create Directory", MB_OK | MB_ICONEXCLAMATION);
        // 'newName' is returned already adjusted (expanded path) and with any ".." and "." normalized
        return FALSE; // error -> show the standard dialog again
    }

    // trim the redundant backslash
    if (userPart.size() > 1 && userPart[1] == L':') // path type "c:\path"
    {
        if (userPart.size() > 3) // not a root path
        {
            if (userPart.back() == L'\\')
                userPart.pop_back(); // trim trailing backslashes
        }
        else
        {
            userPart.resize(3);
            userPart[2] = L'\\'; // root path, backslash required ("c:\")
        }
    }
    else if (!userPart.empty() && userPart.back() == L'\\')
        userPart.pop_back();
    parsed = std::wstring(fsName) + L":" + userPart;
    newName = parsed;

    // finally create the directory
    DWORD err;
    if (!SalamanderGeneral->SalCreateDirectoryEx(userPart.c_str(), &err))
    {
        SalamanderGeneral->SalMessageBox(parent, SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(),
                                         L"DFS Create Directory Error", MB_OK | MB_ICONEXCLAMATION);
        // 'newName' is returned already adjusted (expanded path)
        return FALSE; // error -> show the standard dialog again
    }
    else // operation succeeded - report the path change (triggers refresh) and return success
    {
        // change on the path (without subdirectories)
        SPLCutDirectoryOwned(SalamanderGeneral, userPart); // must succeed (cannot be root)
        // NOTE: a typical plugin should send the full FS path here
        SalamanderGeneral->PostChangeOnPathNotification(userPart.c_str(), FALSE);
        newName = nextFocus;
        return TRUE;
    }
}

void WINAPI
CPluginFSInterface::ViewFile(const wchar_t* fsName, HWND parent,
                             CSalamanderForViewFileOnFSAbstract* salamander,
                             CFileData& file)
{
    const size_t fsNameLen = wcslen(fsName);
    std::wstring uniqueFileName = std::wstring(fsName) + L":" + Path;
    SPLSalPathAppendOwned(uniqueFileName, file.Name);
    // filenames on disk are case-insensitive, the disk cache is case-sensitive, converting
    // to lowercase makes the disk cache behave case-insensitively as well
    std::transform(uniqueFileName.begin(), uniqueFileName.end(), uniqueFileName.begin(),
                   [](wchar_t ch) { return (wchar_t)towlower(ch); });

    // obtain the cache copy name
    BOOL fileExists;
    const wchar_t* tmpFileName = salamander->AllocFileNameInCache(parent, uniqueFileName.c_str(), file.Name, NULL, fileExists);
    if (tmpFileName == NULL)
        return; // fatal error

    // determine whether a copy needs to be prepared in the disk cache (download)
    BOOL newFileOK = FALSE;
    CQuadWord newFileSize(0, 0);
    if (!fileExists) // preparing the file copy (download) is necessary
    {
        const wchar_t* name = uniqueFileName.c_str() + fsNameLen + 1;
        if (CopyFileW(name, tmpFileName, TRUE)) // the copy succeeded
        {
            newFileOK = TRUE; // if determining the file size fails, newFileSize stays zero (not too important)
            HANDLE hFile = HANDLES_Q(CreateFileW(tmpFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                 NULL, OPEN_EXISTING, 0, NULL));
            if (hFile != INVALID_HANDLE_VALUE)
            { // ignore errors; the exact file size is not essential
                DWORD err;
                SalamanderGeneral->SalGetFileSize(hFile, newFileSize, err); // ignore errors
                HANDLES(CloseHandle(hFile));
            }
        }
        else // copy (download) failed
        {
            DWORD err = GetLastError();
            const std::wstring errorText = SPLFormatStringOwned(
                L"Unable to download file %s to disk file %s.\nError: %s",
                uniqueFileName.c_str(), tmpFileName, SPLGetErrorTextOwned(SalamanderGeneral, err).c_str());
            SalamanderGeneral->SalMessageBox(parent, errorText.c_str(), L"DFS Error", MB_OK | MB_ICONEXCLAMATION);
        }
    }

    // open the viewer
    HANDLE fileLock;
    BOOL fileLockOwner;
    if (!fileExists && !newFileOK || // open the viewer only if the file copy is OK
        !salamander->OpenViewer(parent, tmpFileName, &fileLock, &fileLockOwner))
    { // on failure reset the "lock"
        fileLock = NULL;
        fileLockOwner = FALSE;
    }

    // call FreeFileNameInCache to pair with AllocFileNameInCache (connects
    // the viewer and the disk cache)
    salamander->FreeFileNameInCache(uniqueFileName.c_str(), fileExists, newFileOK,
                                    newFileSize, fileLock, fileLockOwner, FALSE /* do not delete immediately after closing the viewer */);
}

BOOL WINAPI
CPluginFSInterface::Delete(const wchar_t* fsName, int mode, HWND parent, int panel,
                           int selectedFiles, int selectedDirs, BOOL& cancelOrError)
{
    // if the plugin opened the dialog itself, it should use CSalamanderGeneralAbstract::AlterFileName
    // ('format' according to SalamanderGeneral->GetConfigParameter(SALCFG_FILENAMEFORMAT))
    cancelOrError = FALSE;
    if (mode == 1)
        return FALSE; // request the standard prompt (if SALCFG_CNFRMFILEDIRDEL is TRUE) - see CPluginFSInterface::CopyOrMoveFromFS for how to build the question text

#ifndef DEMOPLUG_QUIET
    const std::wstring prompt = SPLFormatStringOwned(
        L"Delete %d files and %d directories from %s panel.", selectedFiles, selectedDirs,
        panel == PANEL_LEFT ? L"left" : L"right");
    SalamanderGeneral->SalMessageBox(parent, prompt.c_str(), L"DFS Delete", MB_OK | MB_ICONINFORMATION);
#endif // DEMOPLUG_QUIET

    /*
  // example of using the wait window - useful e.g. when reading names slated for deletion
  // (preparation for overall progress)
  SalamanderGeneral->CreateSafeWaitWindow(ToWideArg("Reading DFS path structure, please wait...").c_str(), NULL,
                                          500, FALSE, SalamanderGeneral->GetMainWindowHWND());
  Sleep(2000);  // simulate some work
  SalamanderGeneral->DestroySafeWaitWindow();
*/

    // find the parent's top-level window (may be Salamander's main window)
    HWND mainWnd = parent;
    HWND parentWin;
    while ((parentWin = GetParent(mainWnd)) != NULL && IsWindowEnabled(parentWin))
        mainWnd = parentWin;
    // disable 'mainWnd'
    EnableWindow(mainWnd, FALSE);

    BOOL retSuccess = FALSE;
    BOOL enableMainWnd = TRUE;
    CDeleteProgressDlg delDlg(mainWnd, ooStatic); // use 'ooStatic' so the modeless dialog can live on the stack
    if (delDlg.Create() != NULL)                  // the dialog opened successfully
    {
        SetForegroundWindow(delDlg.HWindow);

        delDlg.Set(L"reading directory tree...", 0, FALSE);
        Sleep(1500); // simulate activity
        delDlg.Set(L"preparing data...", 0, FALSE);
        Sleep(500); // simulate activity

        int i;
        for (i = 0; i <= 1000; i++)
        {
            if (delDlg.GetWantCancel())
            {
                delDlg.Set(L"canceling operation...", i, FALSE);
                Sleep(500); // simulate the "cancel" action
                break;
            }

            const std::wstring fileName = SPLFormatStringOwned(L"filename_%d.test", i);
            delDlg.Set(fileName.c_str(), i, TRUE); // delayedPaint == TRUE so we do not slow things down

            Sleep(20); // simulate activity
        }
        retSuccess = (i > 1000);

        // re-enable 'mainWnd' (otherwise Windows cannot bring it to the foreground)
        EnableWindow(mainWnd, TRUE);
        enableMainWnd = FALSE;

        DestroyWindow(delDlg.HWindow); // close the progress dialog
    }

    if (enableMainWnd)
    { // re-enable 'mainWnd' (no foreground change occurred - the progress never opened)
        EnableWindow(mainWnd, TRUE);
    }
    return retSuccess; // success only if Cancel was not pressed and the progress dialog opened

    /*
  // fetch the "Confirm on" configuration values
  BOOL ConfirmOnNotEmptyDirDelete, ConfirmOnSystemHiddenFileDelete, ConfirmOnSystemHiddenDirDelete;
  SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMNEDIRDEL, &ConfirmOnNotEmptyDirDelete, 4, NULL);
  SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHFILEDEL, &ConfirmOnSystemHiddenFileDelete, 4, NULL);
  SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHDIRDEL, &ConfirmOnSystemHiddenDirDelete, 4, NULL);

  std::wstring buf;  // dynamically owned error text

  std::wstring fileName;  // dynamically owned full name
  strcpy(fileName, Path);
  char *end = fileName + strlen(fileName);  // space reserved for names from the panel
  if (end > fileName && *(end - 1) != '\\')
  {
    *end++ = '\\';
    *end = 0;
  }
  int endSize = fileName.Size() - (int)(end - fileName);  // maximum number of characters available for a panel name

  std::wstring dfsFileName;  // dynamically owned full DFS name
  sprintf(dfsFileName, "%s:%s", fsName, (const char*)fileName);
  char *endDFSName = dfsFileName + strlen(dfsFileName);  // space reserved for names from the panel
  int endDFSNameSize = dfsFileName.Size() - (int)(endDFSName - (char*)dfsFileName); // maximum number of characters available for a panel name

  const CFileData *f = NULL;  // pointer to the file/directory in the panel to process
  BOOL isDir = FALSE;         // TRUE if 'f' is a directory
  BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
  int index = 0;
  BOOL success = TRUE;        // FALSE if an error occurs or the user cancels
  BOOL skipAllSHFD = FALSE;   // skip all deletes of system or hidden files
  BOOL yesAllSHFD = FALSE;    // delete all system or hidden files
  BOOL skipAllSHDD = FALSE;   // skip all deletes of system or hidden dirs
  BOOL yesAllSHDD = FALSE;    // delete all system or hidden dirs
  BOOL skipAllErrors = FALSE; // skip all errors
  BOOL changeInSubdirs = FALSE;
  while (1)
  {
    // fetch data for the file being processed
    if (focused) f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
    else f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

    // delete the file/directory
    if (f != NULL)
    {
      // assemble the full names; trimming to MAX_PATH (2 * MAX_PATH) is theoretically unnecessary
      // but unfortunately required in practice
      lstrcpyn(end, f->Name, endSize);
      lstrcpyn(endDFSName, f->Name, endDFSNameSize);

      if (isDir)
      {
        BOOL skip = FALSE;
        if (ConfirmOnSystemHiddenDirDelete &&
            (f->Attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
        {
          if (!skipAllSHDD && !yesAllSHDD)
          {
            int res = SalamanderGeneral->DialogQuestion(parent, BUTTONS_YESALLSKIPCANCEL, ToWideArg(dfsFileName).c_str(),
                                                        ToWideArg("Do you want to delete the directory with "
                                                        "SYSTEM or HIDDEN attribute?").c_str(),
                                                        ToWideArg("Confirm Directory Delete").c_str());
            switch (res)
            {
              case DIALOG_ALL: yesAllSHDD = TRUE;
              case DIALOG_YES: break;

              case DIALOG_SKIPALL: skipAllSHDD = TRUE;
              case DIALOG_SKIP: skip = TRUE; break;

              default: success = FALSE; break; // DIALOG_CANCEL
            }
          }
          else  // skip all or delete all
          {
            if (skipAllSHDD) skip = TRUE;
          }
        }

        if (success && !skip)   // not canceled and not skipped
        {

          // handle ConfirmOnNotEmptyDirDelete plus recursive delete here,
          // also update the progress (after deleting/skipping files/directories)
          // deleted files should call SalamanderGeneral->RemoveOneFileFromCache();

          changeInSubdirs = TRUE;   // changes may also occur in subdirectories
        }
      }
      else
      {
        BOOL skip = FALSE;
        if (ConfirmOnSystemHiddenFileDelete &&
            (f->Attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
        {
          if (!skipAllSHFD && !yesAllSHFD)
          {
            int res = SalamanderGeneral->DialogQuestion(parent, BUTTONS_YESALLSKIPCANCEL, ToWideArg(dfsFileName).c_str(),
                                                        ToWideArg("Do you want to delete the file with "
                                                        "SYSTEM or HIDDEN attribute?").c_str(),
                                                        ToWideArg("Confirm File Delete").c_str());
            switch (res)
            {
              case DIALOG_ALL: yesAllSHFD = TRUE;
              case DIALOG_YES: break;

              case DIALOG_SKIPALL: skipAllSHFD = TRUE;
              case DIALOG_SKIP: skip = TRUE; break;

              default: success = FALSE; break; // DIALOG_CANCEL
            }
          }
          else  // skip all or delete all
          {
            if (skipAllSHFD) skip = TRUE;
          }
        }

        if (success && !skip)   // not canceled and not skipped
        {
          BOOL skip = FALSE;
          while (1)
          {
            SalamanderGeneral->ClearReadOnlyAttr(ToWideArg(fileName).c_str(), f->Attr);  // allow deletion of read-only items
            if (!DeleteFile(fileName))
            {
              if (!skipAllErrors)
              {
                SalamanderGeneral->GetErrorText(GetLastError(), buf, buf.Size());
                int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, ToWideArg(dfsFileName).c_str(), ToWideArg(buf).c_str(), ToWideArg("DFS Delete Error").c_str());
                switch (res)
                {
                  case DIALOG_RETRY: break;

                  case DIALOG_SKIPALL: skipAllErrors = TRUE;
                  case DIALOG_SKIP: skip = TRUE; break;

                  default: success = FALSE; break; // DIALOG_CANCEL
                }
              }
              else skip = TRUE;
            }
            else
            {
              // filenames on disk are case-insensitive, the disk cache is case-sensitive, converting
              // to lowercase makes the disk cache behave case-insensitively as well
              SalamanderGeneral->ToLowerCase(dfsFileName);
              // remove the deleted file's copy from the disk cache (if it is cached)
              SalamanderGeneral->RemoveOneFileFromCache(dfsFileName);
              break;   // delete succeeded
            }
            if (!success || skip) break;
          }

          if (success)
          {

            // update the progress here (after deleting/skipping a single file)

          }
        }
      }
    }

    // check whether it makes sense to continue (if there is no error and another selected item exists)
    if (!success || focused || f == NULL) break;
  }

  // change on the Path path (without subdirectories if only files were deleted)
  // NOTE: a typical plugin should send the full FS path here
  SalamanderGeneral->PostChangeOnPathNotification(Path.c_str(), changeInSubdirs);
  return success;
*/
}

// 'path1'/'path2' are real filesystem-path segments; the narrow 'LowerCase'
// table is a 256-entry ANSI lookup and would be an out-of-range read for any wchar_t above
// U+00FF (class 11), so ASCII-only case folding replaces it - the structural comparison below
// only ever needs to fold ASCII letters ('\\', ':', drive letters).
static wchar_t DFSAsciiLower(wchar_t c)
{
    return (c >= L'A' && c <= L'Z') ? (wchar_t)(c - L'A' + L'a') : c;
}

BOOL WINAPI DFS_IsTheSamePath(const wchar_t* path1, const wchar_t* path2)
{
    while (*path1 != 0 && DFSAsciiLower(*path1) == DFSAsciiLower(*path2))
    {
        path1++;
        path2++;
    }
    if (*path1 == L'\\')
        path1++;
    if (*path2 == L'\\')
        path2++;
    return *path1 == 0 && *path2 == 0;
}

enum CDFSPathError
{
    dfspeNone,
    dfspeServerNameMissing,
    dfspeShareNameMissing,
    dfspeRelativePath, // relative paths are not supported ("PATH", "\PATH", or "C:PATH")
};

BOOL DFS_IsValidPath(const wchar_t* path, CDFSPathError* err)
{
    const wchar_t* s = path;
    if (err != NULL)
        *err = dfspeNone;
    if (*s == L'\\' && *(s + 1) == L'\\') // UNC (\\server\share\...)
    {
        s += 2;
        if (*s == 0 || *s == L'\\')
        {
            if (err != NULL)
                *err = dfspeServerNameMissing;
        }
        else
        {
            while (*s != 0 && *s != L'\\')
                s++; // skip the server name
            if (*s == L'\\')
                s++;
            if (*s == 0 || *s == L'\\')
            {
                if (err != NULL)
                    *err = dfspeShareNameMissing;
            }
            else
                return TRUE; // path OK
        }
    }
    else // path specified via a drive (c:\...)
    {
        wchar_t lowered = DFSAsciiLower(*s);
        if (lowered >= L'a' && lowered <= L'z' && *(s + 1) == L':' && *(s + 2) == L'\\') // "c:\..."
        {
            return TRUE; // path OK
        }
        else
        {
            if (err != NULL)
                *err = dfspeRelativePath;
        }
    }
    return FALSE;
}

BOOL WINAPI
CPluginFSInterface::CopyOrMoveFromFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                     int panel, int selectedFiles, int selectedDirs,
                                     CSalamanderStringBuffer* targetPath, BOOL& operationMask,
                                     BOOL& cancelOrHandlePath, HWND dropTarget)
{
    std::wstring payload;
    if (targetPath == NULL ||
        !sally::plugin_abi::ReadStringBuffer(*targetPath, payload))
        return FALSE;
    const size_t separator = payload.find(L'\0');
    std::wstring path(payload.data(), separator);
    std::wstring mask;
    if (separator != std::wstring::npos)
        mask.assign(payload.data() + separator + 1,
                    payload.size() - separator - 1);
    const BOOL result = CopyOrMoveFromFSOwned(
        copy, mode, fsName, parent, panel, selectedFiles, selectedDirs,
        path, mask, operationMask, cancelOrHandlePath, dropTarget);
    path.resize(wcslen(path.c_str()));
    return sally::plugin_abi::WriteStringBuffer(*targetPath, path) ? result : FALSE;
}

BOOL CPluginFSInterface::CopyOrMoveFromFSOwned(
    BOOL copy, int mode, const wchar_t* fsName, HWND parent, int panel,
    int selectedFiles, int selectedDirs, std::wstring& targetPath,
    const std::wstring& suppliedMask, BOOL& operationMask,
    BOOL& cancelOrHandlePath, HWND dropTarget)
{
    // if the plugin opened the dialog itself, it should use CSalamanderGeneralAbstract::AlterFileName
    // ('format' according to SalamanderGeneral->GetConfigParameter(SALCFG_FILENAMEFORMAT))
    operationMask = FALSE;
    cancelOrHandlePath = FALSE;
    if (mode == 1) // first call to CopyOrMoveFromFS
    {
        /*
    // example of composing the edit-line title with the copy target
    // (if the subject is "files" and "directories", you can simply call
    //  SalamanderGeneral->GetCommonFSOperSourceDescr(subjectSrc, MAX_PATH + 100,
    //  panel, selectedFiles, selectedDirs, NULL, FALSE, FALSE), which replaces the code below building subjectSrc)
    char subjectSrc[MAX_PATH + 100];
    if (selectedFiles + selectedDirs <= 1)  // a single selected item or the focused one
    {
      BOOL isDir;
      const CFileData *f;
      if (selectedFiles == 0 && selectedDirs == 0)
        f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
      else
      {
        int index = 0;
        f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);
      }
      int fileNameFormat;
      SalamanderGeneral->GetConfigParameter(SALCFG_FILENAMEFORMAT, &fileNameFormat,
                                            sizeof(fileNameFormat), NULL);
      std::wstring formatedFileName;
      SalamanderGeneral->AlterFileName(formatedFileName, f->Name, fileNameFormat, 0, isDir);
      _snprintf_s(subjectSrc, _TRUNCATE, isDir ? "directory \"%s\"" : "file \"%s\"", (const char*)formatedFileName);
      subjectSrc[MAX_PATH + 100 - 1] = 0;
    }
    else  // multiple directories and files
    {
      SalamanderGeneral->ExpandPluralFilesDirs(subjectSrc, MAX_PATH + 100, selectedFiles,
                                               selectedDirs, epfdmNormal, FALSE);
    }
    char subject[MAX_PATH + 200];
    sprintf(subject, "Copy %s from FS to", subjectSrc);
*/

        // if no path is proposed, check whether the other panel has DFS mounted and suggest it
        if (targetPath.empty())
        {
            int targetPanel = (panel == PANEL_LEFT ? PANEL_RIGHT : PANEL_LEFT);
            int type;
            std::wstring path;
            size_t fsOffset = std::wstring::npos;
            if (SPLGetPanelPathOwned(SalamanderGeneral, targetPanel, path,
                                     &type, &fsOffset))
            {
                if (type == PATH_TYPE_FS && fsOffset == wcslen(fsName) &&
                    SalamanderGeneral->StrNICmp(path.c_str(), fsName,
                                                static_cast<int>(fsOffset)) == 0)
                {
                    targetPath = std::move(path);
                }
            }
        }
        // if a path is proposed, append the *.* mask (operation masks will be processed)
        if (!targetPath.empty())
        {
            SPLSalPathAppendOwned(targetPath, L"*.*");
            SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_TARGET); // default action = work with the target panel path
        }

        return FALSE; // request for the standard dialog
    }

    if (mode == 4) // error in the standard Salamander processing of the destination path
    {
        // 'targetPath' contains an invalid path; the user was notified, just let them edit it
        return FALSE; // request for the standard dialog
    }

    const wchar_t* title = copy ? L"DFS Copy" : L"DFS Move";
    const wchar_t* errTitle = copy ? L"DFS Copy Error" : L"DFS Move Error";

#ifndef DEMOPLUG_QUIET
    if (mode == 2 || mode == 5)
    {
        const std::wstring prompt = SPLFormatStringOwned(
            L"%s %d files and %d directories from %s panel to: %s",
            copy ? L"Copy" : L"Move", selectedFiles, selectedDirs,
            panel == PANEL_LEFT ? L"left" : L"right", targetPath.c_str());
        SalamanderGeneral->SalMessageBox(parent, prompt.c_str(), title, MB_OK | MB_ICONINFORMATION);
    }
#endif // DEMOPLUG_QUIET

    std::wstring nextFocus;
    std::wstring operationMaskValue;

    BOOL diskPath = TRUE;    // for mode==3 'targetPath' is a Windows path (FALSE = a path on this FS)
    wchar_t* userPart = NULL; // pointer within 'targetPath' to the FS user-part (used when diskPath is FALSE)
    BOOL rename = FALSE;     // TRUE means rename/copy of a directory onto itself
    const wchar_t* opMask = NULL; // operation mask

    if (mode == 2) // the user entered a path in the standard dialog
    {
        // resolve relative paths ourselves (Salamander cannot do that)
        if ((targetPath.size() < 2 || targetPath[0] != L'\\' || targetPath[1] != L'\\') && // not an UNC path
            (targetPath.empty() || targetPath.size() < 2 || targetPath[1] != L':'))        // not a standard drive path
        {                                                         // so it is neither Windows nor archive syntax
            userPart = wcschr(targetPath.data(), L':');
            if (userPart == NULL) // the path has no FS name, therefore it is relative
            {                     // a relative path containing ':' is not allowed (it would look like a full FS path)

                // For disk paths it would be better to call SalGetFullName:
                // SalamanderGeneral->SalGetFullName(targetPath, &errTextID, Path, nextFocus) plus error handling.
                // Then we would only need to prepend the FS name to the resulting path.
                // Here we deliberately demonstrate a custom implementation (using SalRemovePointsFromPath, etc.):

                const std::wstring entered(targetPath.c_str());
                const size_t slash = entered.find(L'\\');
                if (slash == std::wstring::npos || slash + 1 == entered.size())
                {
                    nextFocus = slash == std::wstring::npos ? entered : entered.substr(0, slash);
                }

                std::wstring expandedUserPart;
                if (targetPath[0] == L'\\') // "\\path" -> build root + newName
                {
                    if (!SPLGetRootPathOwned(SalamanderGeneral, Path.c_str(), expandedUserPart))
                        return FALSE;
                    expandedUserPart.append(targetPath.c_str() + 1);
                }
                else // "path" -> combine Path + newName
                {
                    expandedUserPart = Path;
                    SPLSalPathAppendOwned(expandedUserPart, targetPath.c_str());
                }

                const std::wstring expanded = std::wstring(fsName) + L":" + expandedUserPart;
                targetPath = expanded;
                userPart = targetPath.data() + wcslen(fsName) + 1;
            }
            else
                userPart++;

            // FS target path ('targetPath' is the full path, 'userPart' points to the FS-specific segment)
            // This is the place where the plugin can process FS paths (its own or foreign ones).
            // Salamander cannot work with these paths yet; perhaps it will one day orchestrate basic operations
            // via TEMP (for example download from FTP into TEMP, then upload from TEMP to FTP - if a faster way
            // exists, such as native FTP transfers, the plugin should handle it here).

            wchar_t* targetData = targetPath.data();
            if ((userPart - targetData) - 1 == lstrlenW(fsName) &&
                SalamanderGeneral->StrNICmp(targetData, fsName, (int)(userPart - targetData) - 1) == 0)
            { // this is DFS (otherwise let Salamander handle it normally)
                CDFSPathError err;
                BOOL invPath = !DFS_IsValidPath(userPart, &err);

                // The full path on this FS might still contain "." or ".." - strip them
                int rootLen = 0;
                if (!invPath)
                {
                    std::wstring rootPath;
                    SPLGetRootPathOwned(SalamanderGeneral, userPart, rootPath);
                    rootLen = (int)rootPath.size();
                    int userPartLen = lstrlenW(userPart);
                    if (userPartLen < rootLen)
                        rootLen = userPartLen;
                }
                const size_t userPartOffset = userPart - targetData;
                if (invPath ||
                    !SPLSalRemovePointsFromPathOwned(
                        SalamanderGeneral, targetPath,
                        userPartOffset + static_cast<size_t>(rootLen)))
                {
                    // optionally 'err' could be displayed when invPath is TRUE; we ignore it here for simplicity
                    SalamanderGeneral->SalMessageBox(parent, L"The path specified is invalid.",
                                                     errTitle, MB_OK | MB_ICONEXCLAMATION);
                    // return 'targetPath' after expansion (some ".." and "." may be adjusted)
                    return FALSE; // error -> re-open the standard dialog
                }
                targetData = targetPath.data();
                userPart = targetData + userPartOffset;

                // trim any superfluous trailing backslash
                int l = lstrlenW(userPart);
                BOOL backslashAtEnd = l > 0 && userPart[l - 1] == L'\\';
                if (l > 1 && userPart[1] == L':') // a drive path such as "c:\path"
                {
                    if (l > 3) // not just the root
                    {
                        if (userPart[l - 1] == L'\\')
                            userPart[l - 1] = 0; // drop the trailing backslash
                    }
                    else
                    {
                        userPart[2] = L'\\'; // for a root path keep the backslash ("c:\")
                        userPart[3] = 0;
                    }
                }
                else // UNC path
                {
                    if (l > 0 && userPart[l - 1] == L'\\')
                        userPart[l - 1] = 0; // drop the trailing backslash
                }

                // Analyze the path: find the existing and missing parts plus the operation mask.
                // Determine what portion already exists and whether it is a file or a directory,
                // then decide what kind of action this is:
                //   - writing to a path (possibly with a missing part) with an operation mask;
                //     the mask is the last non-existent segment of the path without a trailing backslash
                //     (for multiple source items ensure the mask contains '*' or at least '?', otherwise
                //     only a single destination name makes sense)
                //   - manual change of directory name case via Move (writing to the path that is also
                //     the source of the operation, i.e. focused/selected as the only item in the panel);
                //     names may differ only by letter casing
                //   - writing into an archive (the path contains an archive file or something else, in
                //     which case the error is "Salamander does not know how to open this file")
                //   - overwriting a file (the entire path is just the target file name; it must not end with a backslash)

                // Determine how much of the path exists (split it into existing and non-existing parts)
                HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
                wchar_t* end = targetData + lstrlenW(targetData);
                wchar_t* afterRoot = userPart + rootLen;
                wchar_t lastChar = 0;
                BOOL pathIsDir = TRUE;
                BOOL pathError = FALSE;

                // If the path contains a mask, cut it off without calling SalGetFileAttributes
                if (end > afterRoot) // there is more than the root
                {
                    wchar_t* end2 = end;
                    BOOL cut = FALSE;
                    while (*--end2 != L'\\') // at least one backslash must follow after the root
                    {
                        if (*end2 == L'*' || *end2 == L'?')
                            cut = TRUE;
                    }
                    if (cut) // the name contains a mask -> trim it
                    {
                        end = end2;
                        lastChar = *end;
                        *end = 0;
                    }
                }

                while (end > afterRoot) // there is still more than the root
                {
                    DWORD attrs = SalamanderGeneral->SalGetFileAttributes(userPart);
                    if (attrs != 0xFFFFFFFF) // this part of the path exists
                    {
                        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) // it is a file
                        {
                            // An existing path must not contain a file name (see SalSplitGeneralPath); trim it.
                            *end = lastChar;   // restore 'targetPath'
                            pathIsDir = FALSE; // the existing part of the path is a file
                            while (*--end != L'\\')
                                ;            // there is at least one backslash after the root
                            lastChar = *end; // keep the path intact
                            break;
                        }
                        else
                            break;
                    }
                    else
                    {
                        DWORD err2 = GetLastError();
                        if (err2 != ERROR_FILE_NOT_FOUND && err2 != ERROR_INVALID_NAME &&
                            err2 != ERROR_PATH_NOT_FOUND && err2 != ERROR_BAD_PATHNAME &&
                            err2 != ERROR_DIRECTORY) // unexpected error -> report it
                        {
                            const std::wstring message = SPLFormatStringOwned(
                                L"Path: %s\nError: %s", targetData, SPLGetErrorTextOwned(SalamanderGeneral, err2).c_str());
                            SalamanderGeneral->SalMessageBox(parent, message.c_str(), errTitle, MB_OK | MB_ICONEXCLAMATION);
                            pathError = TRUE;
                            break; // report the error
                        }
                    }

                    *end = lastChar; // restore 'targetPath'
                    while (*--end != L'\\')
                        ; // there is guaranteed to be at least one backslash after the root
                    lastChar = *end;
                    *end = 0;
                }
                *end = lastChar; // repair 'targetPath'
                SetCursor(oldCur);

                if (!pathError) // splitting succeeded without errors
                {
                    if (*end == L'\\')
                        end++;

                    const wchar_t* dirName = NULL;
                    const wchar_t* curPath = NULL;
                    std::wstring currentFSPath;
                    if (selectedFiles + selectedDirs <= 1)
                    {
                        const CFileData* f;
                        if (selectedFiles == 0 && selectedDirs == 0)
                            f = SalamanderGeneral->GetPanelFocusedItem(panel, NULL);
                        else
                        {
                            int index = 0;
                            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, NULL);
                        }
                        dirName = f->Name;

                        currentFSPath = std::wstring(fsName) + L":" + Path;
                        curPath = currentFSPath.c_str();
                    }

                    const size_t userPartOffset = static_cast<size_t>(userPart - targetData);
                    const size_t afterRootOffset = static_cast<size_t>(afterRoot - targetData);
                    const size_t secondPartOffset = static_cast<size_t>(end - targetData);
                    std::wstring splitPath(targetData);
                    std::wstring splitMask;
                    std::wstring newDirs;
                    if (SPLSalSplitGeneralPathOwned(SalamanderGeneral, parent, title, errTitle,
                                                    selectedFiles + selectedDirs, splitPath,
                                                    afterRootOffset, secondPartOffset, pathIsDir,
                                                    backslashAtEnd, dirName, curPath, splitMask,
                                                    &newDirs, DFS_IsTheSamePath))
                    {
                        if (!newDirs.empty()) // the target path needs new subdirectories created
                        {
                            // NOTE: if creating subdirectories on the target path is not supported,
                            //       pass newDirs==NULL to SalSplitGeneralPath(); it will report the error itself

                            // NOTE: if the path were created here, PostChangeOnPathNotification would have to be
                            //       called (it is processed later, so ideally call it immediately after creating the
                            //       path rather than at the end of the operation)

                            SalamanderGeneral->SalMessageBox(parent, L"Sorry, but creating of target path is not supported.",
                                                             errTitle, MB_OK | MB_ICONEXCLAMATION);
                            SPLSalPathAppendOwned(splitPath, splitMask.c_str());
                            targetPath = splitPath;
                            pathError = TRUE;
                        }
                        else if (userPartOffset > splitPath.size())
                            pathError = TRUE;
                        else
                        {
                            targetPath = splitPath;
                            userPart = targetPath.data() + userPartOffset;
                            operationMaskValue = splitMask;
                            opMask = operationMaskValue.c_str();
                            if (dirName != NULL && curPath != NULL && SalamanderGeneral->StrICmp(dirName, opMask) == 0 &&
                                DFS_IsTheSamePath(splitPath.c_str(), curPath))
                            {
                                // rename/copy of a directory onto itself (differing only by letter case) – "change-case".
                                // Do not treat this as an operation mask (the supplied target path exists; splitting into
                                // the mask is the result of the analysis).

                                rename = TRUE;
                            }

                            /*
              // the following code handles the situation when the FS does not support operation masks
              if (mask != NULL && (strcmp(mask, "*.*") == 0 || strcmp(mask, "*") == 0))
              {  // masks are unsupported and the mask is empty -> cut it off
                *mask = 0;  // double-null terminated
              }
              if (!rename)  // for rename this is not an error
              {
                if (mask != NULL && *mask != 0)  // the mask exists but is not allowed
                {
                  char *e = targetPath + strlen(targetPath);   // fix 'targetPath' (join 'targetPath' and 'mask')
                  if (e > targetPath && *(e - 1) != '\\') *e++ = '\\';
                  if (e != mask) memmove(e, mask, strlen(mask) + 1);  // shift the mask if needed

                  SalamanderGeneral->SalMessageBox(parent, "DFS doesn't support operation masks (target "
                                                   "path must exist or end on backslash)", errTitle,
                                                   MB_OK | MB_ICONEXCLAMATION);
                  pathError = TRUE;
                }
              }
*/

                            if (!pathError)
                                diskPath = FALSE; // the path for this FS was successfully analyzed
                        }
                    }
                    else
                    {
                        targetPath = splitPath;
                        pathError = TRUE;
                    }
                }

                if (pathError)
                {
                    // return 'targetPath' after adjustment (expansion of the path and possible tweaks to ".." and ".")
                    return FALSE; // error -> re-open the standard dialog
                }
            }
        }

        if (diskPath)
        {
            // Windows path, archive path, or an unknown FS -> let Salamander handle the standard processing
            operationMask = TRUE; // operation masks are supported
            cancelOrHandlePath = TRUE;
            return FALSE; // let Salamander process the path
        }
    }

    if (mode == 5)                // the operation target was specified via drag & drop
    {
        // If this is a disk path, set the operation mask and continue (same as mode==3).
        // For an archive path, show "not supported"; for a DFS path set diskPath=FALSE and compute userPart
        // (points into the DFS user-part). For other FS paths report "not supported".

        BOOL ok = FALSE;
        opMask = L"*.*";
        int type;
        BOOL isDir;
        std::wstring parsedTarget(targetPath.c_str());
        if ((parsedTarget.size() >= 2 && parsedTarget[1] == L':') ||
            (parsedTarget.size() >= 2 && parsedTarget[0] == L'\\' && parsedTarget[1] == L'\\'))
        {                                                     // ensure the trailing backslash so it's always a path (mode 5 always passes a path)
            SPLSalPathAddBackslashOwned(parsedTarget);
        }
        size_t secondPartOffset = std::wstring::npos;
        if (SPLSalParsePathOwned(SalamanderGeneral, parent, parsedTarget, type, isDir,
                                 secondPartOffset, errTitle, FALSE, NULL))
        {
            targetPath = parsedTarget;
            wchar_t* targetData = targetPath.data();
            wchar_t* secondPart = targetData + secondPartOffset;
            switch (type)
            {
            case PATH_TYPE_WINDOWS:
            {
                if (*secondPart != 0)
                {
                    SalamanderGeneral->SalMessageBox(parent, L"Target path doesn't exist. DFS doesn't support creating of target path.",
                                                     errTitle, MB_OK | MB_ICONEXCLAMATION);
                }
                else
                    ok = TRUE;
                break;
            }

            case PATH_TYPE_FS:
            {
                userPart = secondPart;
                if ((userPart - targetData) - 1 == lstrlenW(fsName) &&
                    SalamanderGeneral->StrNICmp(targetData, fsName, (int)(userPart - targetData) - 1) == 0)
                { // je to DFS
                    diskPath = FALSE;
                    ok = TRUE;
                }
                else // another FS -> report "not supported"
                {
                    SalamanderGeneral->SalMessageBox(parent, L"DFS doesn't support copying nor moving to other "
                                                             L"plugin file-systems.",
                                                     errTitle,
                                                     MB_OK | MB_ICONEXCLAMATION);
                }
                break;
            }

            //case PATH_TYPE_ARCHIVE:
            default: // archive -> report "not supported"
            {
                SalamanderGeneral->SalMessageBox(parent, L"DFS doesn't support copying nor moving to archives.",
                                                 errTitle, MB_OK | MB_ICONEXCLAMATION);
                break;
            }
            }
        }
        if (!ok)
        {
            cancelOrHandlePath = TRUE;
            return TRUE;
        }
    }

    // 'mode' is 2, 3, or 5

    /*
  // example of using the wait window - useful e.g. when reading names that should be copied
  // (preparation for overall progress)
  SalamanderGeneral->CreateSafeWaitWindow(ToWideArg("Reading DFS path structure, please wait...").c_str(), NULL,
                                          500, FALSE, SalamanderGeneral->GetMainWindowHWND());
  Sleep(2000);  // simulate some work
  SalamanderGeneral->DestroySafeWaitWindow();
*/

    // fetch the "Confirm on" configuration values
    BOOL ConfirmOnFileOverwrite, ConfirmOnDirOverwrite, ConfirmOnSystemHiddenFileOverwrite;
    SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMFILEOVER, &ConfirmOnFileOverwrite, 4, NULL);
    SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMDIROVER, &ConfirmOnDirOverwrite, 4, NULL);
    SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHFILEOVER, &ConfirmOnSystemHiddenFileOverwrite, 4, NULL);
    // if path analysis with optional creation of missing subdirectories were performed here,
    // SALCFG_CNFRMCREATEPATH would also come in handy (show "do you want to create target path?")

    // determine the operation mask (the destination path is stored in 'targetPath')
    if (opMask == NULL)
        opMask = suppliedMask.c_str();

    /*  // description of the operation destination gathered in the previous code:
  if (diskPath)  // 'targetPath' is a Windows path, 'opMask' is the operation mask
  {
  }
  else   // 'targetPath' is a path on this FS ('userPart' points to the FS user-part path), 'opMask' is the operation mask
  {
    // if 'rename' is TRUE we are renaming/copying a directory into itself
  }
*/

    const std::wstring targetBase = diskPath ? std::wstring(targetPath.c_str()) : std::wstring();

    const CFileData* f = NULL; // pointer to the file/directory in the panel to process
    BOOL isDir = FALSE;        // TRUE if 'f' is a directory
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL success = TRUE;                     // FALSE if an error occurs or the user cancels
    BOOL skipAllErrors = FALSE;              // skip all errors
    BOOL sourcePathChanged = FALSE;          // TRUE if the source path changed (move operation)
    BOOL subdirsOfSourcePathChanged = FALSE; // TRUE if source subdirectories changed as well
    BOOL targetPathChanged = FALSE;          // TRUE if the target path changed
    BOOL subdirsOfTargetPathChanged = FALSE; // TRUE if target subdirectories changed as well

    HANDLE fileLock = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));
    if (fileLock == NULL)
    {
        DWORD err = GetLastError();
        TRACE_EW(L"Unable to create fileLock event: " << SPLGetErrorTextOwned(SalamanderGeneral, err));
        cancelOrHandlePath = TRUE;
        return TRUE; // error/cancel
    }

    while (1)
    {
        // fetch data for the file being processed
        if (focused)
            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        else
            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

        // perform the copy/move on the file or directory
        if (f != NULL)
        {
            std::wstring sourceName(Path);
            SPLSalPathAppendOwned(sourceName, f->Name);
            std::wstring dfsSourceName = std::wstring(fsName) + L":" + sourceName;
            // filenames on disk are case-insensitive, the disk cache is case-sensitive, converting
            // to lowercase makes the disk cache behave case-insensitively as well
            std::transform(dfsSourceName.begin(), dfsSourceName.end(), dfsSourceName.begin(),
                           [](wchar_t ch) { return (wchar_t)towlower(ch); });

            if (isDir) // directory
            {
                // DEMOPLUG does not implement directory operations (recursion would need either
                // processing items sequentially without overall progress or scripting with total progress tracking)

                // progress reporting should also be handled here (count processed/skipped files/directories)

                // report changes on the source and destination paths:
                // sourcePathChanged = !copy;
                // subdirsOfSourcePathChanged = TRUE;
                // targetPathChanged = TRUE;
                // subdirsOfTargetPathChanged = TRUE;
            }
            else // file
            {
                BOOL skip = FALSE;
                if (diskPath) // Windows destination path
                {
                    const std::wstring maskedName = SPLMaskNameOwned(SalamanderGeneral, f->Name, opMask);
                    std::wstring targetName(targetBase);
                    SPLSalPathAppendOwned(targetName, maskedName.c_str());

                    const wchar_t* tmpName;
                    BOOL fileFromCache = SalamanderGeneral->GetFileFromCache(dfsSourceName.c_str(), tmpName, fileLock);
                    if (!fileFromCache) // the file is not in the disk cache
                    {
                        // copy the file directly from DFS
                        // the demo plug-in does not handle overwriting files; real code should confirm overwrites here
                        // (the ConfirmOnFileOverwrite and ConfirmOnSystemHiddenFileOverwrite flags apply)
                        while (1)
                        {
                            if (!CopyFileW(sourceName.c_str(), targetName.c_str(), TRUE))
                            {
                                const DWORD copyError = GetLastError();
                                if (!skipAllErrors)
                                {
                                    const std::wstring details = SPLFormatStringOwned(L"from: %s to: %s", dfsSourceName.c_str(), targetName.c_str());
                                    int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL,
                                                                             details.c_str(), SPLGetErrorTextOwned(SalamanderGeneral, copyError).c_str(), errTitle);
                                    switch (res)
                                    {
                                    case DIALOG_RETRY:
                                        break;

                                    case DIALOG_SKIPALL:
                                        skipAllErrors = TRUE;
                                    case DIALOG_SKIP:
                                        skip = TRUE;
                                        break;

                                    default:
                                        success = FALSE;
                                        break; // DIALOG_CANCEL
                                    }
                                }
                                else
                                    skip = TRUE;
                            }
                            else
                            {
                                targetPathChanged = TRUE;
                                break; // copied successfully
                            }
                            if (!success || skip)
                                break;
                        }

                        // if this is not a move (the source remains), nothing was skipped or canceled, and the destination is a Windows path,
                        // add the file to the disk cache (if it is not larger than 1 MB - ideally configurable,
                        // which the demo plug-in leaves unimplemented
                        if (success && copy && !skip && f->Size <= CQuadWord(1048576, 0))
                        {
                            // copy the file into the TEMP directory and move it to the disk cache
                            // errors are ignored; the file simply is not cached
                            int err = 0;
                            std::wstring tmpName2W;
                            if (SPLSalGetTempFileNameOwned(SalamanderGeneral, NULL, L"DFS", tmpName2W, TRUE, NULL))
                            {
                                if (CopyFileW(targetName.c_str(), tmpName2W.c_str(), FALSE))
                                {
                                    BOOL alreadyExists;
                                    if (!SalamanderGeneral->MoveFileToCache(dfsSourceName.c_str(), f->Name, NULL, tmpName2W.c_str(),
                                                                            f->Size, &alreadyExists))
                                    {
                                        err = alreadyExists ? 1 : 2;
                                    }
                                }
                                else
                                    err = 4;

                                if (err != 0) // disk-cache save failed, remove the TEMP file
                                {
                                    // clear the read-only attribute so the temporary copy can be deleted
                                    SalamanderGeneral->ClearReadOnlyAttr(tmpName2W.c_str());
                                    DeleteFileW(tmpName2W.c_str());
                                }
                            }
                            else
                                err = 3;
                            if (err != 0)
                            {
                                const wchar_t* s;
                                switch (err)
                                {
                                case 1:
                                    s = L"already exists";
                                    break; // not an error, just a concurrency case (e.g. View and Copy)
                                case 2:
                                    s = L"fatal error";
                                    break;
                                case 3:
                                    s = L"unable to create file in TEMP directory";
                                    break;
                                default:
                                    s = L"unable to copy file to TEMP directory";
                                    break;
                                }
                                TRACE_EW(L"Unable to store file into disk-cache: " << s);
                            }
                        }
                    }
                    else // the file is stored in the disk cache
                    {
                        // copy the file from the disk cache
                        // the demo plug-in does not handle overwriting files; real code should confirm overwrites here
                        // (the ConfirmOnFileOverwrite and ConfirmOnSystemHiddenFileOverwrite flags apply)
                        while (1)
                        {
                            if (!CopyFileW(tmpName, targetName.c_str(), TRUE))
                            {
                                const DWORD copyError = GetLastError();
                                if (!skipAllErrors)
                                {
                                    const std::wstring details = SPLFormatStringOwned(
                                        L"from: %s (in cache: %s) to: %s", dfsSourceName.c_str(), tmpName, targetName.c_str());
                                    int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL,
                                                                             details.c_str(), SPLGetErrorTextOwned(SalamanderGeneral, copyError).c_str(), errTitle);
                                    switch (res)
                                    {
                                    case DIALOG_RETRY:
                                        break;

                                    case DIALOG_SKIPALL:
                                        skipAllErrors = TRUE;
                                    case DIALOG_SKIP:
                                        skip = TRUE;
                                        break;

                                    default:
                                        success = FALSE;
                                        break; // DIALOG_CANCEL
                                    }
                                }
                                else
                                    skip = TRUE;
                            }
                            else
                            {
                                targetPathChanged = TRUE;
                                break; // copied successfully
                            }
                            if (!success || skip)
                                break;
                        }

                        // unlock the cached file copy
                        SalamanderGeneral->UnlockFileInCache(fileLock);
                    }

                    if (success && !copy && !skip) // this is a move and the file was not skipped -> delete the source file
                    {
                        // delete the file on the DFS
                        while (1)
                        {
                            // allow deletion of read-only items
                            SalamanderGeneral->ClearReadOnlyAttr(sourceName.c_str(), f->Attr);
                            if (!DeleteFileW(sourceName.c_str()))
                            {
                                const DWORD deleteError = GetLastError();
                                if (!skipAllErrors)
                                {
                                    int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL,
                                                                             dfsSourceName.c_str(), SPLGetErrorTextOwned(SalamanderGeneral, deleteError).c_str(), errTitle);
                                    switch (res)
                                    {
                                    case DIALOG_RETRY:
                                        break;

                                    case DIALOG_SKIPALL:
                                        skipAllErrors = TRUE;
                                    case DIALOG_SKIP:
                                        skip = TRUE;
                                        break;

                                    default:
                                        success = FALSE;
                                        break; // DIALOG_CANCEL
                                    }
                                }
                                else
                                    skip = TRUE;
                            }
                            else
                            {
                                // remove the deleted file's copy from the disk cache (if it is cached)
                                if (fileFromCache)
                                {
                                    SalamanderGeneral->RemoveOneFileFromCache(dfsSourceName.c_str());
                                }
                                sourcePathChanged = TRUE;
                                // subdirsOfSourcePathChanged = TRUE;

                                break; // delete succeeded
                            }
                            if (!success || skip)
                                break;
                        }
                    }
                }
                else // DFS destination path
                {
                    // if 'rename' is TRUE we are renaming/copying a directory into itself

                    // DEMOPLUG does not implement operations within DFS (no disk cache; entirely up to the FS)
                }

                // report changes on the source and destination paths:
                // sourcePathChanged = !copy;
                // subdirsOfSourcePathChanged = TRUE;
                // targetPathChanged = TRUE;
                // subdirsOfTargetPathChanged = TRUE;

                if (success)
                {

                    // progress handling belongs here (add after processing/skipping a single file)
                }
            }
        }

        // determine whether it makes sense to continue (if not canceled and another selected item exists)
        if (!success || focused || f == NULL)
            break;
    }
    HANDLES(CloseHandle(fileLock));

    // change on the source path 'Path' (mainly for move operations)
    if (sourcePathChanged)
    {
        // NOTE: a typical plugin should send the full FS path here
        // (for DFS we leverage the fact it works with disk paths and send only
        // the raw disk path; this cannot be used for other FS types)
        SalamanderGeneral->PostChangeOnPathNotification(Path.c_str(), subdirsOfSourcePathChanged);
    }
    // change on the destination path 'targetPath' (may be a path on our FS or on disk)
    if (targetPathChanged)
    {
        SalamanderGeneral->PostChangeOnPathNotification(targetPath.c_str(), subdirsOfTargetPathChanged);
    }

    if (success)
        targetPath = nextFocus;
    else
        cancelOrHandlePath = TRUE; // error/cancel
    return TRUE;                   // success or error/cancel
}

BOOL WINAPI
CPluginFSInterface::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                           const wchar_t* sourcePath, SalEnumSelection2 next,
                                           void* nextParam, int sourceFiles, int sourceDirs,
                                           CSalamanderStringBuffer* targetPath, BOOL* invalidPathOrCancel)
{
    std::wstring path;
    if (targetPath == NULL ||
        !sally::plugin_abi::ReadStringBuffer(*targetPath, path))
        return FALSE;
    const BOOL result = CopyOrMoveFromDiskToFSOwned(
        copy, mode, fsName, parent, sourcePath, next, nextParam,
        sourceFiles, sourceDirs, path, invalidPathOrCancel);
    path.resize(wcslen(path.c_str()));
    return sally::plugin_abi::WriteStringBuffer(*targetPath, path) ? result : FALSE;
}

BOOL CPluginFSInterface::CopyOrMoveFromDiskToFSOwned(
    BOOL copy, int mode, const wchar_t* fsName, HWND parent,
    const wchar_t* sourcePath, SalEnumSelection2 next, void* nextParam,
    int sourceFiles, int sourceDirs, std::wstring& targetPath,
    BOOL* invalidPathOrCancel)
{
    if (invalidPathOrCancel != NULL)
        *invalidPathOrCancel = FALSE;

    if (mode == 1)
    {
        // append the *.* mask to the destination path (operation masks will be processed)
        SPLSalPathAppendOwned(targetPath, L"*.*");
        return TRUE;
    }

    const wchar_t* title = copy ? L"DFS Copy" : L"DFS Move";
    const wchar_t* errTitle = copy ? L"DFS Copy Error" : L"DFS Move Error";

#ifndef DEMOPLUG_QUIET
    if (mode == 2 || mode == 3)
    {
        const std::wstring prompt = SPLFormatStringOwned(
            L"%s %d files and %d directories from disk path \"%s\" to FS path \"%s\"",
            copy ? L"Copy" : L"Move", sourceFiles, sourceDirs, sourcePath, targetPath.c_str());
        SalamanderGeneral->SalMessageBox(parent, prompt.c_str(), title, MB_OK | MB_ICONINFORMATION);
    }
#endif // DEMOPLUG_QUIET

    if (mode == 2 || mode == 3)
    {
        // 'targetPath' contains the raw path entered by the user (all we know is that it
        // belongs to this FS, otherwise Salamander would not call this method)
        const size_t colon = targetPath.find(L':');
        if (colon == std::wstring::npos)
        {
            if (invalidPathOrCancel != NULL)
                *invalidPathOrCancel = TRUE;
            return FALSE;
        }
        wchar_t* targetData = targetPath.data();
        wchar_t* userPart = targetData + colon + 1;

        CDFSPathError err;
        BOOL invPath = !DFS_IsValidPath(userPart, &err);

        // check whether the operation can be performed in this FS; also remove any "." and ".."
        // that the user may have used in the full path on this FS
        int rootLen = 0;
        if (!invPath)
        {
            if (!Path.empty() && // not a newly opened FS (it has a current path)
                !SalamanderGeneral->HasTheSameRootPath(Path.c_str(), userPart))
            {
                return FALSE; // DemoPlug: the operation cannot be performed in this FS (different disk root)
            }

            std::wstring rootPath;
            SPLGetRootPathOwned(SalamanderGeneral, userPart, rootPath);
            rootLen = (int)rootPath.size();
            int userPartLen = lstrlenW(userPart);
            if (userPartLen < rootLen)
                rootLen = userPartLen;
        }
        const size_t userPartOffset = userPart - targetData;
        if (invPath ||
            !SPLSalRemovePointsFromPathOwned(
                SalamanderGeneral, targetPath,
                userPartOffset + static_cast<size_t>(rootLen)))
        {
            // additionally we could display 'err' when 'invPath' is TRUE; ignored here for simplicity
            SalamanderGeneral->SalMessageBox(parent, L"The path specified is invalid.",
                                             errTitle, MB_OK | MB_ICONEXCLAMATION);
            // 'targetPath' is returned after any ".." and "." adjustments
            if (invalidPathOrCancel != NULL)
                *invalidPathOrCancel = TRUE;
            return FALSE; // let the user correct the path
        }
        targetData = targetPath.data();
        userPart = targetData + userPartOffset;

        // trim the redundant backslash
        int l = lstrlenW(userPart);
        BOOL backslashAtEnd = l > 0 && userPart[l - 1] == L'\\';
        if (l > 1 && userPart[1] == L':') // path type "c:\path"
        {
            if (l > 3) // not a root path
            {
                if (userPart[l - 1] == L'\\')
                    userPart[l - 1] = 0; // trim trailing backslashes
            }
            else
            {
                userPart[2] = L'\\'; // root path, backslash required ("c:\")
                userPart[3] = 0;
            }
        }
        else // UNC path
        {
            if (l > 0 && userPart[l - 1] == L'\\')
                userPart[l - 1] = 0; // trim trailing backslashes
        }

        // analyze the path - find the existing part, the missing part, and the operation mask
        //
        // - determine which part of the path exists and whether it is a file or directory,
        //   then decide what the operation is:
        //   - write to the path (possibly with a missing segment) using a mask - the mask is the last nonexistent
        //     part of the path without a trailing backslash (verify that multiple source files/directories
        //     have '*' or at least '?' in the mask; otherwise it makes no sense -> only one destination name)
        //   - write into an archive (the path contains an archive file or it may not even be an archive,
        //     resulting in the "Salamander does not know how to open this file" error)
        //   - overwrite a file (the entire path is just the destination file name; must not end with a backslash)

        // detect how much of the path already exists (split into existing and non-existing segments)
        HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
        wchar_t* end = targetData + lstrlenW(targetData);
        wchar_t* afterRoot = userPart + rootLen;
        wchar_t lastChar = 0;
        BOOL pathIsDir = TRUE;
        BOOL pathError = FALSE;

        // if the path contains a mask, trim it without calling SalGetFileAttributes
        if (end > afterRoot) // not down to just the root yet
        {
            wchar_t* end2 = end;
            BOOL cut = FALSE;
            while (*--end2 != L'\\') // there is guaranteed to be at least one '\\' past the root
            {
                if (*end2 == L'*' || *end2 == L'?')
                    cut = TRUE;
            }
            if (cut) // the name contains a mask -> trim it
            {
                end = end2;
                lastChar = *end;
                *end = 0;
            }
        }

        while (end > afterRoot) // not down to just the root yet
        {
            DWORD attrs = SalamanderGeneral->SalGetFileAttributes(userPart);
            if (attrs != 0xFFFFFFFF) // this part of the path exists
            {
                if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) // it is a file
                {
                    // an existing path must not include a file name (see SalSplitGeneralPath) -> trim it
                    *end = lastChar;   // restore 'targetPath'
                    pathIsDir = FALSE; // the existing part of the path is a file
                    while (*--end != L'\\')
                        ;            // there is guaranteed to be at least one '\\' past the root
                    lastChar = *end; // keep the path intact
                    break;
                }
                else
                    break;
            }
            else
            {
                DWORD err2 = GetLastError();
                if (err2 != ERROR_FILE_NOT_FOUND && err2 != ERROR_INVALID_NAME &&
                    err2 != ERROR_PATH_NOT_FOUND && err2 != ERROR_BAD_PATHNAME &&
                    err2 != ERROR_DIRECTORY) // unusual error - just display it
                {
                    const std::wstring message = SPLFormatStringOwned(
                        L"Path: %s\nError: %s", targetData, SPLGetErrorTextOwned(SalamanderGeneral, err2).c_str());
                    SalamanderGeneral->SalMessageBox(parent, message.c_str(), errTitle, MB_OK | MB_ICONEXCLAMATION);
                    pathError = TRUE;
                    break; // report the error
                }
            }

            *end = lastChar; // restore 'targetPath'
            while (*--end != L'\\')
                ; // there is guaranteed to be at least one '\\' past the root
            lastChar = *end;
            *end = 0;
        }
        *end = lastChar; // fix 'targetPath'
        SetCursor(oldCur);

        std::wstring operationMaskValue;
        const wchar_t* opMask = NULL;
        if (!pathError) // the split succeeded without errors
        {
            if (*end == L'\\')
                end++;

            const size_t userPartOffset = static_cast<size_t>(userPart - targetData);
            const size_t afterRootOffset = static_cast<size_t>(afterRoot - targetData);
            const size_t secondPartOffset = static_cast<size_t>(end - targetData);
            std::wstring splitPath(targetData);
            std::wstring splitMask;
            std::wstring newDirs;
            if (SPLSalSplitGeneralPathOwned(SalamanderGeneral, parent, title, errTitle,
                                            sourceFiles + sourceDirs, splitPath,
                                            afterRootOffset, secondPartOffset, pathIsDir,
                                            backslashAtEnd, NULL, NULL, splitMask, &newDirs,
                                            NULL /* 'isTheSamePathF' not needed */))
            {
                if (!newDirs.empty()) // the destination path needs new subdirectories created
                {
                    // NOTE: if creating subdirectories on the destination path is unsupported, just pass
                    //       'newDirs'==NULL to SalSplitGeneralPath(); it will report the error itself

                    // NOTE: if the path were created here, PostChangeOnPathNotification would have to be called
                    //       (handled later, so ideally call it right after the path is created, not after
                    //       the entire operation finishes)

                    SalamanderGeneral->SalMessageBox(parent, L"Sorry, but creating of target path is not supported.",
                                                     errTitle, MB_OK | MB_ICONEXCLAMATION);
                    SPLSalPathAppendOwned(splitPath, splitMask.c_str());
                    targetPath = splitPath;
                    pathError = TRUE;
                }
                else if (userPartOffset > splitPath.size())
                    pathError = TRUE;
                else
                {
                    targetPath = splitPath;
                    userPart = targetPath.data() + userPartOffset;
                    operationMaskValue = splitMask;
                    opMask = operationMaskValue.c_str();
                    /*
          // the following code handles the situation when the FS does not support operation masks
          if (opMask != NULL && (strcmp(opMask, "*.*") == 0 || strcmp(opMask, "*") == 0))
          {  // masks are unsupported and the mask is empty -> cut it off
            *opMask = 0;  // double-null terminated
          }
          if (opMask != NULL && *opMask != 0)  // the mask exists but is not allowed
          {
            char *e = targetPath + strlen(targetPath);   // fix 'targetPath' by joining it with 'opMask'
            if (e > targetPath && *(e - 1) != '\\') *e++ = '\\';
            if (e != opMask) memmove(e, opMask, strlen(opMask) + 1);  // shift the mask if necessary

            SalamanderGeneral->SalMessageBox(parent, "DFS doesn't support operation masks (target "
                                             "path must exist or end on backslash)", errTitle,
                                             MB_OK | MB_ICONEXCLAMATION);
            pathError = TRUE;
          }
*/

                    // if 'pathError' is FALSE, the target path was successfully analyzed
                }
            }
            else
            {
                targetPath = splitPath;
                pathError = TRUE;
            }
        }

        if (pathError)
        {
            // 'targetPath' is returned after resolving ".." and "." plus any appended mask
            if (invalidPathOrCancel != NULL)
                *invalidPathOrCancel = TRUE;
            return FALSE; // path error - let the user correct it
        }

        /*
    // example of using the wait window - useful when reading the names to copy
    // (preparation for overall progress) - the directory structure is read on the first call to
    // the 'next' function (for enumFiles == 1 or 2)
    SalamanderGeneral->CreateSafeWaitWindow(ToWideArg("Reading disk path structure, please wait...").c_str(), NULL,
                                            500, FALSE, SalamanderGeneral->GetMainWindowHWND());
    Sleep(2000);  // simulate some work
    SalamanderGeneral->DestroySafeWaitWindow();
  */

        // load the "Confirm on" configuration values
        BOOL ConfirmOnFileOverwrite, ConfirmOnDirOverwrite, ConfirmOnSystemHiddenFileOverwrite;
        SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMFILEOVER, &ConfirmOnFileOverwrite, 4, NULL);
        SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMDIROVER, &ConfirmOnDirOverwrite, 4, NULL);
        SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHFILEOVER, &ConfirmOnSystemHiddenFileOverwrite, 4, NULL);
        // if path analysis with optional creation of missing subdirectories were performed here,
        // we would also use SALCFG_CNFRMCREATEPATH (show "do you want to create target path?")

        // description of the operation destination gathered above:
        // 'targetPath' is a path on this FS ('userPart' points to the FS user-part path), 'opMask' is the operation mask

        const std::wstring sourceBase(sourcePath != NULL ? sourcePath : L"");
        const std::wstring targetBase(userPart);

        BOOL success = TRUE;                     // FALSE if an error occurs or the user cancels
        BOOL skipAllErrors = FALSE;              // skip all errors
        BOOL sourcePathChanged = FALSE;          // TRUE if the source path changed (move operation)
        BOOL subdirsOfSourcePathChanged = FALSE; // TRUE if source subdirectories changed as well
        BOOL targetPathChanged = FALSE;          // TRUE if the target path changed
        BOOL subdirsOfTargetPathChanged = FALSE; // TRUE if target subdirectories changed as well

        BOOL isDir;
        const wchar_t* name;
        const wchar_t* dosName; // dummy
        CQuadWord size;
        DWORD attr;
        FILETIME lastWrite;
        while ((name = next(NULL, 0, &dosName, &isDir, &size, &attr, &lastWrite, nextParam, NULL)) != NULL)
        { // perform the copy/move on a file or directory
            std::wstring sourceName(sourceBase);
            SPLSalPathAppendOwned(sourceName, name);

            if (isDir) // directory
            {
                // DEMOPLUG does not implement directory operations (recursion would need either
                // processing items sequentially without overall progress or scripting with total progress tracking)

                // progress reporting should also be handled here (count processed/skipped files/directories)

                // reporting changes on the source and destination paths:
                // sourcePathChanged = !copy;
                // subdirsOfSourcePathChanged = TRUE;
                // targetPathChanged = TRUE;
                // subdirsOfTargetPathChanged = TRUE;
            }
            else // file
            {
                BOOL skip = FALSE;
                const std::wstring maskedName = SPLMaskNameOwned(SalamanderGeneral, name, opMask);
                std::wstring targetName(targetBase);
                SPLSalPathAppendOwned(targetName, maskedName.c_str());

                // copy the file directly to the DFS
                // the demo plug-in does not handle overwriting files; real code should confirm overwrites here
                // (the ConfirmOnFileOverwrite and ConfirmOnSystemHiddenFileOverwrite flags apply)
                while (1)
                {
                    if (!CopyFileW(sourceName.c_str(), targetName.c_str(), TRUE))
                    {
                        if (!skipAllErrors)
                        {
                            const DWORD copyError = GetLastError();
                            const std::wstring details = SPLFormatStringOwned(
                                L"from: %s to: %s:%s", sourceName.c_str(), fsName, targetName.c_str());
                            int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL,
                                                                     details.c_str(), SPLGetErrorTextOwned(SalamanderGeneral, copyError).c_str(), errTitle);
                            switch (res)
                            {
                            case DIALOG_RETRY:
                                break;

                            case DIALOG_SKIPALL:
                                skipAllErrors = TRUE;
                            case DIALOG_SKIP:
                                skip = TRUE;
                                break;

                            default:
                                success = FALSE;
                                break; // DIALOG_CANCEL
                            }
                        }
                        else
                            skip = TRUE;
                    }
                    else
                    {
                        targetPathChanged = TRUE;
                        break; // copied successfully
                    }
                    if (!success || skip)
                        break;
                }

                if (success && !copy && !skip) // we are doing a move and the file was not skipped -> delete the source file
                {
                    // remove the file from disk
                    while (1)
                    {
                        // allow deletion of read-only items
                        SalamanderGeneral->ClearReadOnlyAttr(sourceName.c_str(), attr);

                        if (!DeleteFileW(sourceName.c_str()))
                        {
                            if (!skipAllErrors)
                            {
                                const DWORD deleteError = GetLastError();
                                int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL,
                                                                         sourceName.c_str(), SPLGetErrorTextOwned(SalamanderGeneral, deleteError).c_str(), errTitle);
                                switch (res)
                                {
                                case DIALOG_RETRY:
                                    break;

                                case DIALOG_SKIPALL:
                                    skipAllErrors = TRUE;
                                case DIALOG_SKIP:
                                    skip = TRUE;
                                    break;

                                default:
                                    success = FALSE;
                                    break; // DIALOG_CANCEL
                                }
                            }
                            else
                                skip = TRUE;
                        }
                        else
                        {
                            sourcePathChanged = TRUE;
                            // subdirsOfSourcePathChanged = TRUE;

                            break; // delete succeeded
                        }
                        if (!success || skip)
                            break;
                    }
                }

                // reporting changes on the source and destination paths:
                // sourcePathChanged = !copy;
                // subdirsOfSourcePathChanged = TRUE;
                // targetPathChanged = TRUE;
                // subdirsOfTargetPathChanged = TRUE;

                if (success)
                {

                    // progress handling belongs here (add after processing/skipping a single file)
                }
            }

            // determine whether it makes sense to continue (if not canceled)
            if (!success)
                break;
        }

        // changes on the source path (especially for move operations)
        if (sourcePathChanged)
        {
            SalamanderGeneral->PostChangeOnPathNotification(sourcePath, subdirsOfSourcePathChanged);
        }
        // changes on the destination path (normally 'targetPath' on the FS, but DFS uses disk paths,
        // so we report the change directly on the disk path 'userPart')
        if (targetPathChanged)
        {
            SalamanderGeneral->PostChangeOnPathNotification(userPart, subdirsOfTargetPathChanged);
        }

        if (success)
            return TRUE; // operation finished successfully
        else
        {
            if (invalidPathOrCancel != NULL)
                *invalidPathOrCancel = TRUE;
            return TRUE; // cancellation requested
        }
    }

    return FALSE; // unknown 'mode'
}

BOOL WINAPI
CPluginFSInterface::ChangeAttributes(const wchar_t* fsName, HWND parent, int panel,
                                     int selectedFiles, int selectedDirs)
{
    const wchar_t* title = L"DFS Change Attributes";
    //  const wchar_t *errTitle = L"DFS Change Attributes Error";

#ifndef DEMOPLUG_QUIET
    const std::wstring message = SPLFormatStringOwned(
        L"Change attributes of %d files and %d directories.", selectedFiles, selectedDirs);
    SalamanderGeneral->SalMessageBox(parent, message.c_str(), title, MB_OK | MB_ICONINFORMATION);
#endif // DEMOPLUG_QUIET

    // show the custom dialog (not implemented - no attribute changes are performed; depends on the FS)
    SalamanderGeneral->ShowMessageBox(L"Here should user specify how to change attributes. "
                                       L"It's not implemented in DemoPlug.",
                                       title, MSGBOX_INFO);

    /*
  // example of using the wait window - useful when reading names (preparation for overall progress)
  SalamanderGeneral->CreateSafeWaitWindow(L"Reading DFS path structure, please wait...", NULL,
                                          500, FALSE, SalamanderGeneral->GetMainWindowHWND());
  Sleep(2000);  // simulate some work
  SalamanderGeneral->DestroySafeWaitWindow();
*/

    const CFileData* f = NULL; // pointer to the file/directory in the panel to process
    BOOL isDir = FALSE;        // TRUE if 'f' is a directory
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL success = TRUE;               // FALSE if an error occurs or the user cancels
    BOOL skipAllErrors = FALSE;        // skip all errors
    BOOL pathChanged = FALSE;          // TRUE if the path changed
    BOOL subdirsOfPathChanged = FALSE; // TRUE if subdirectories of the path changed as well

    while (1)
    {
        // fetch data for the file being processed
        if (focused)
            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        else
            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

        // perform the operation on the file or directory
        if (f != NULL)
        {
            std::wstring name(Path);
            SPLSalPathAppendOwned(name, f->Name);

            // performing the attribute change is not implemented here

            // reporting changes on the source path:
            // pathChanged = TRUE;
            // subdirsOfPathChanged = TRUE;

            if (success)
            {

                // progress handling belongs here (add after processing/skipping a single file)
            }
        }

        // determine whether it makes sense to continue (if not canceled and if more items are selected)
        if (!success || focused || f == NULL)
            break;
    }

    // change on the source path 'Path'
    if (pathChanged)
    {
        // NOTE: a typical plugin should send the full FS path here
        SalamanderGeneral->PostChangeOnPathNotification(Path.c_str(), subdirsOfPathChanged);
    }

    //  return success;
    return FALSE; // cancellation requested
}

void WINAPI
CPluginFSInterface::ShowProperties(const wchar_t* fsName, HWND parent, int panel,
                                   int selectedFiles, int selectedDirs)
{
    const wchar_t* title = L"DFS Show Properties";
    //  const wchar_t *errTitle = L"DFS Show Properties Error";

#ifndef DEMOPLUG_QUIET
    const std::wstring message = SPLFormatStringOwned(
        L"Show properties of %d files and %d directories.", selectedFiles, selectedDirs);
    SalamanderGeneral->SalMessageBox(parent, message.c_str(), title, MB_OK | MB_ICONINFORMATION);
#endif // DEMOPLUG_QUIET

    /*
  // example of using the wait window - useful e.g. when reading names (preparation for overall progress)
  SalamanderGeneral->CreateSafeWaitWindow(L"Reading DFS path structure, please wait...", NILL,
                                          500, FALSE, SalamanderGeneral->GetMainWindowHWND());
  Sleep(2000);  // simulate some work
  SalamanderGeneral->DestroySafeWaitWindow();
*/

    const CFileData* f = NULL; // pointer to the file/directory in the panel to process
    BOOL isDir = FALSE;        // TRUE if 'f' is a directory
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL success = TRUE;        // FALSE if an error occurs or the user cancels
    BOOL skipAllErrors = FALSE; // skip all errors

    while (1)
    {
        // fetch data for the file being processed
        if (focused)
            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        else
            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

        // perform the property query on the file or directory
        if (f != NULL)
        {
            std::wstring name(Path);
            SPLSalPathAppendOwned(name, f->Name);

            // retrieving the attributes is not implemented here

            if (success)
            {

                // progress handling belongs here (add after processing/skipping a single file)
            }
        }

        // determine whether it makes sense to continue (if not canceled and another selected item exists)
        if (!success || focused || f == NULL)
            break;
    }

    if (success)
    {
        // show the actual dialog (not implemented; depends on the FS)
        SalamanderGeneral->ShowMessageBox(L"Here should be properties of selected files and directories. "
                                           L"It's not implemented in DemoPlug.",
                                           title, MSGBOX_INFO);
    }
}

void WINAPI
CPluginFSInterface::ContextMenu(const wchar_t* fsName, HWND parent, int menuX, int menuY, int type,
                                int panel, int selectedFiles, int selectedDirs)
{
#ifndef DEMOPLUG_QUIET
    const std::wstring message = SPLFormatStringOwned(L"Show context menu (type %d).", (int)type);
    SalamanderGeneral->SalMessageBox(parent, message.c_str(), L"DFS Context Menu", MB_OK | MB_ICONINFORMATION);
#endif // DEMOPLUG_QUIET

    HMENU menu = CreatePopupMenu();
    if (menu == NULL)
    {
        TRACE_E("CPluginFSInterface::ContextMenu: Unable to create menu.");
        return;
    }
    MENUITEMINFOW mi;
    std::wstring nameBuf;

    switch (type)
    {
    case fscmItemsInPanel: // context menu for panel items (selected/focused files and directories)
    {
        int i = 0;

        // insert Salamander commands
        nameBuf = L"Always Command from DemoPlug Submenu";
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
        mi.fType = MFT_STRING;
        mi.wID = MENUCMD_ALWAYS;
        mi.dwTypeData = nameBuf.data();
        mi.cch = (UINT)nameBuf.size();
        mi.fState = MFS_ENABLED;
        InsertMenuItemW(menu, i++, TRUE, &mi);

        int index = 0;
        int salCmd;
        BOOL enabled;
        int type2, lastType = sctyUnknown;
        while (SPLEnumSalamanderCommandsOwned(
            SalamanderGeneral, &index, &salCmd, nameBuf, &enabled, &type2))
        {
            if (type2 != lastType /*&& lastType != sctyUnknown*/) // insert a separator
            {
                memset(&mi, 0, sizeof(mi));
                mi.cbSize = sizeof(mi);
                mi.fMask = MIIM_TYPE;
                mi.fType = MFT_SEPARATOR;
                InsertMenuItemW(menu, i++, TRUE, &mi);
            }
            lastType = type2;

            // insert Salamander commands
            memset(&mi, 0, sizeof(mi));
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
            mi.fType = MFT_STRING;
            mi.wID = salCmd + 1000; // shift Salamander commands by 1000 so they differ from ours
            mi.dwTypeData = nameBuf.data();
            mi.cch = (UINT)nameBuf.size();
            mi.fState = enabled ? MFS_ENABLED : MFS_DISABLED;
            InsertMenuItemW(menu, i++, TRUE, &mi);
        }
        DWORD cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                     menuX, menuY, parent, NULL);
        if (cmd != 0) // the user selected a command from the menu
        {
            if (cmd >= 1000)
            {
                if (SPLGetSalamanderCommandOwned(
                        SalamanderGeneral, cmd - 1000, nameBuf, &enabled,
                        &type2))
                {
                    TRACE_IW(L"Starting command: " << nameBuf.c_str());
                }

                SalamanderGeneral->PostSalamanderCommand(cmd - 1000);
            }
            else // our own command
            {
                TRACE_IW(L"Starting command: Always");
                SalamanderGeneral->PostMenuExtCommand(cmd, TRUE); // execute later in "sal-idle"
                                                                  /*
          SalamanderGeneral->PostMenuExtCommand(cmd, FALSE); // run once the main window receives the message
          // WARNING: after this call no window with a message loop may open,
          // otherwise the plugin command runs before this method finishes!
*/
            }
        }
        break;
    }

    case fscmPathInPanel: // context menu for the current path in the panel
    {
        int i = 0;

        // insert Salamander commands
        nameBuf = L"Menu For Actual Path: Always Command from DemoPlug Submenu";
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
        mi.fType = MFT_STRING;
        mi.wID = MENUCMD_ALWAYS;
        mi.dwTypeData = nameBuf.data();
        mi.cch = (UINT)nameBuf.size();
        mi.fState = MFS_ENABLED;
        InsertMenuItemW(menu, i++, TRUE, &mi);

        nameBuf = L"&Disconnect";
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
        mi.fType = MFT_STRING;
        mi.wID = panel == PANEL_LEFT ? MENUCMD_DISCONNECT_LEFT : MENUCMD_DISCONNECT_RIGHT;
        mi.dwTypeData = nameBuf.data();
        mi.cch = (UINT)nameBuf.size();
        mi.fState = MFS_ENABLED;
        InsertMenuItemW(menu, i++, TRUE, &mi);

        DWORD cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                     menuX, menuY, parent, NULL);
        if (cmd != 0)                                         // the user selected a command from the menu
            SalamanderGeneral->PostMenuExtCommand(cmd, TRUE); // execute later in "sal-idle"
        break;
    }

    case fscmPanel: // context menu for the panel
    {
        int i = 0;

        // insert Salamander commands
        nameBuf = L"Menu For Panel: Always Command from DemoPlug Submenu";
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
        mi.fType = MFT_STRING;
        mi.wID = MENUCMD_ALWAYS;
        mi.dwTypeData = nameBuf.data();
        mi.cch = (UINT)nameBuf.size();
        mi.fState = MFS_ENABLED;
        InsertMenuItemW(menu, i++, TRUE, &mi);

        nameBuf = L"&Disconnect";
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
        mi.fType = MFT_STRING;
        mi.wID = panel == PANEL_LEFT ? MENUCMD_DISCONNECT_LEFT : MENUCMD_DISCONNECT_RIGHT;
        mi.dwTypeData = nameBuf.data();
        mi.cch = (UINT)nameBuf.size();
        mi.fState = MFS_ENABLED;
        InsertMenuItemW(menu, i++, TRUE, &mi);

        DWORD cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                     menuX, menuY, parent, NULL);
        if (cmd != 0)                                         // the user selected a command from the menu
            SalamanderGeneral->PostMenuExtCommand(cmd, TRUE); // execute later in "sal-idle"
        break;
    }
    }
    DestroyMenu(menu);
}
