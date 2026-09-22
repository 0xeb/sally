// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "ui/IPrompter.h"
#include "common/IFileSystem.h"
#include "common/Win32TextCodec.h"
#include "common/unicode/helpers.h"
#include "common/fsutil.h" // GetShortPathW
#include "stswnd.h"
#include "editwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "usermenu.h"
#include "mainwnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "execute.h"
#include "cache.h"
#include "toolbar.h"
#include "salinflt.h"
#include "menu.h"
#include "common/CommandShellService.h"
#include "common/widepath.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "zip.h"

BOOL ImageDragging = FALSE;
BOOL ImageDraggingVisible = FALSE;
BOOL ShowCaretAfterDrop = FALSE;
int ImageDraggingVisibleLevel = 0;
int ImageDragX = INT_MAX;
int ImageDragY = INT_MAX;
int ImageDragW = INT_MAX;
int ImageDragH = INT_MAX;
int ImageDragDxHotspot = INT_MAX;
int ImageDragDyHotspot = INT_MAX;

//****************************************************************************
//
// CMainWindow
//

void RecursiveFindAndCopy(char* srcPath, char* dstPath, char** fromBuf, char** toBuf, int* freeBufNames);

void CMainWindow::ClearPluginFSFromHistory(CPluginFSInterfaceAbstract* fs)
{
    DirHistory->ClearPluginFSFromHistory(fs);
}

void CMainWindow::DirHistoryAddPathUnique(int type, const wchar_t* pathOrArchiveOrFSName,
                                          const wchar_t* archivePathOrFSUserPart, HICON hIcon,
                                          CPluginFSInterfaceAbstract* pluginFS,
                                          CPluginFSInterfaceEncapsulation* curPluginFS)
{
    if (CanAddToDirHistory)
    {
        DirHistory->AddPathUnique(type, pathOrArchiveOrFSName, archivePathOrFSUserPart, hIcon,
                                  pluginFS, curPluginFS);
        if (LeftPanel != NULL)
            LeftPanel->DirectoryLine->SetHistory(DirHistory->HasPaths());
        if (RightPanel != NULL)
            RightPanel->DirectoryLine->SetHistory(DirHistory->HasPaths());
    }
    else
    {
        if (hIcon != NULL)
            HANDLES(DestroyIcon(hIcon));
    }
}

void CMainWindow::DirHistoryRemoveActualPath(CFilesWindow* panel)
{
    if (panel->Is(ptZIPArchive))
    {
        DirHistory->RemoveActualPath(1, panel->GetZIPArchive(), panel->GetZIPPath(), NULL, NULL);
    }
    else
    {
        if (panel->Is(ptDisk))
        {
            DirHistory->RemoveActualPath(0, panel->GetPathW(), NULL, NULL, NULL);
        }
        else
        {
            if (panel->Is(ptPluginFS))
            {
                std::wstring curPath;
                if (panel->GetPluginFS()->NotEmpty() && panel->GetPluginFS()->GetCurrentPathW(curPath))
                {
                    DirHistory->RemoveActualPath(2, panel->GetPluginFS()->GetPluginFSName(), curPath.c_str(),
                                                 panel->GetPluginFS()->GetInterface(), panel->GetPluginFS());
                }
            }
        }
    }
    if (LeftPanel != NULL)
        LeftPanel->DirectoryLine->SetHistory(DirHistory->HasPaths());
    if (RightPanel != NULL)
        RightPanel->DirectoryLine->SetHistory(DirHistory->HasPaths());
}

void CMainWindow::GetSplitRect(RECT& r)
{
    r.left = SplitPositionPix;
    r.top = TopRebarHeight;
    r.right = SplitPositionPix + MainWindow->GetSplitBarWidth();
    r.bottom = WindowHeight - EditHeight - BottomToolBarHeight;
}

void CMainWindow::GetWindowSplitRect(RECT& r)
{
    GetClientRect(HWindow, &r);
    r.top = TopRebarHeight;
    r.bottom = WindowHeight - EditHeight - BottomToolBarHeight;
}

BOOL CMainWindow::PtInChild(HWND hChild, POINT p)
{
    if (hChild == NULL)
        return FALSE;
    RECT r;
    GetWindowRect(hChild, &r);
    MapWindowPoints(NULL, HWindow, (POINT*)&r, 2);
    return PtInRect(&r, p);
}

BOOL CMainWindow::CloseDetachedFS(HWND parent, CPluginFSInterfaceEncapsulation* detachedFS)
{
    CALL_STACK_MESSAGE1("CMainWindow::CloseDetachedFS()");
    BOOL dummy; // ignored return value
    if (!detachedFS->TryCloseOrDetach(CriticalShutdown, FALSE, dummy, FSTRYCLOSE_UNLOADCLOSEDETACHEDFS) &&
        !CriticalShutdown) // test close; forceClose==TRUE only during a "critical shutdown"
    {                      // ask the user whether to close it even against the FS wishes
        std::wstring path = detachedFS->GetPluginFSName();
        path += L':';
        std::wstring userPart;
        if (detachedFS->NotEmpty() && detachedFS->GetCurrentPathW(userPart))
            path += userPart;

        std::wstring msg = FormatStrW(LoadStrW(IDS_FSFORCECLOSE), path.c_str());
        if (gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), msg.c_str()).type == PromptResult::kYes) // user chooses "close"
        {
            detachedFS->TryCloseOrDetach(TRUE, FALSE, dummy, FSTRYCLOSE_UNLOADCLOSEDETACHEDFS);
        }
        else
            return FALSE; // user doesn't want to close the detached FS
    }

    // close the FS
    CPluginInterfaceForFSEncapsulation plugin(detachedFS->GetPluginInterfaceForFS()->GetInterface(),
                                              detachedFS->GetPluginInterfaceForFS()->GetBuiltForVersion());
    if (plugin.NotEmpty())
    {
        detachedFS->ReleaseObject(parent);
        plugin.CloseFS(detachedFS->GetInterface());
    }
    else
        TRACE_E("Unexpected situation (2) in CMainWindow::CloseDetachedFS()");

    return TRUE; // FS is closed
}

BOOL CMainWindow::CanUnloadPlugin(HWND parent, CPluginInterfaceAbstract* plugin)
{
    CALL_STACK_MESSAGE1("CMainWindow::CanUnloadPlugin()");
    if (LeftPanel != NULL && !LeftPanel->CanUnloadPlugin(parent, plugin))
        return FALSE;
    if (RightPanel != NULL && !RightPanel->CanUnloadPlugin(parent, plugin))
        return FALSE;

    // find detached FS belonging to the plug-in 'plugin' and attempt to close them
    int i;
    for (i = DetachedFSList->Count - 1; i >= 0; i--) // iterate backwards; as we will be deleting from the array (quadratic complexity)
    {
        CPluginFSInterfaceEncapsulation* detachedFS = DetachedFSList->At(i);
        if (detachedFS->GetPluginInterface() == plugin) // belongs to plug-in 'plugin'
        {
            if (CloseDetachedFS(parent, detachedFS))
            {
                DetachedFSList->Delete(i); // remove the detached FS from DetachedFSList
                if (!DetachedFSList->IsGood())
                    DetachedFSList->ResetState();
            }
            else
                return FALSE; // unload cannot proceed (user refused to close the plug-in's detached FS)
        }
    }

    // check if data from the plugin is not in SalShExtPastedData
    // (panels leaving archives might have stored them there due to plug-in unload)
    if (!SalShExtPastedData.CanUnloadPlugin(parent, plugin))
        return FALSE; // unload cannot proceed

    return TRUE; // unload is possible; all plug-in resources were released
}

void CMainWindow::MakeFileList()
{
    CALL_STACK_MESSAGE1("CMainWindow::MakeFileList()");

    BOOL files = FALSE; // cursor is on a file or directory or there is a selection
    BOOL upDir = FALSE; // presence of ".."

    CFilesWindow* panel = GetActivePanel();

    upDir = (panel->Dirs->Count != 0 && wcscmp(panel->Dirs->At(0).Name, L"..") == 0);
    int caret = panel->GetCaretIndex();
    if (caret >= 0)
    {
        if (caret == 0)
        {
            if (!upDir)
                files = (panel->Dirs->Count + panel->Files->Count > 0);
            else
            {
                int count = panel->GetSelCount();
                if (count == 1)
                {
                    files = (panel->GetSel(0) == FALSE);
                }
                else
                    files = (count > 0);
            }
        }
        else
            files = TRUE;
    }

    if (!files)
        return;

    // restore DefaultDir
    MainWindow->UpdateDefaultDir(TRUE);

    BeginStopRefresh(); // snooper takes a break

    CFileListDialog dlg(HWindow);
    if (dlg.Execute() == IDOK)
    {
        std::wstring fileNameW;

        switch (Configuration.FileListDestination)
        {
        case 0: // clipboard
        case 1: // viewer
        {
            fileNameW = SalGetTempFileNameW(NULL, L"MFL", true);
            if (fileNameW.empty())
            {
                DWORD err = GetLastError();
                std::wstring msg = FormatStrW(L"%s\n\n%s", LoadStrW(IDS_ERRORCREATINGTMPFILE), GetErrorTextOwned(err).c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
            }
            break;
        }

        case 2: // file
        {
            // Configuration.FileListName has since become a genuine wide
            // config value (std::wstring, cfgdlg.h) - this comment's "narrow-only"
            // premise is stale, use it directly instead of a needless AnsiToWide re-widen.
            // The current directory and next-focus target also have genuine wide sources:
            // GetPathW() (the removed ANSI mirror can name a different path or none), and
            // NextFocusNameW is consumed directly by the panel's refresh logic.
            fileNameW = Configuration.FileListName;
            int errTextID;
            if (!SalGetFullNameW(fileNameW, &errTextID,
                                 GetActivePanel()->Is(ptDisk) ? GetActivePanel()->GetPathW() : NULL,
                                 &panel->NextFocusNameW))
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(errTextID));
                fileNameW.clear();
            }
            break;
        }

        default:
        {
            TRACE_E("Unknown destination!");
            fileNameW.clear();
        }
        }

        if (!fileNameW.empty())
        {
            BOOL append = (Configuration.FileListDestination == 2 && Configuration.FileListAppend);
            // genuine wide path from above, not a re-widened narrow mirror.
            HANDLE hFile = gFileSystem->CreateFile(fileNameW.c_str(), GENERIC_WRITE | GENERIC_READ,
                                                   FILE_SHARE_READ, NULL,
                                                   append ? OPEN_ALWAYS : CREATE_ALWAYS,
                                                   FILE_FLAG_RANDOM_ACCESS,
                                                   NULL);
            DWORD openError = GetLastError();
            HANDLES_ADD_EX(__otQuiet, hFile != INVALID_HANDLE_VALUE, __htFile,
                           __hoCreateFile, hFile, openError, TRUE);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                // position the file pointer
                gFileSystem->SeekHandle(hFile, 0, append ? FILE_END : FILE_BEGIN, NULL);

                // fill the file with data -- insert one entry for each file or directory
                BOOL deleteFile = TRUE;
                if (panel->MakeFileList(hFile))
                {
                    panel->SetSel(FALSE, -1, TRUE);                        // force redraw
                    PostMessage(panel->HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
                    deleteFile = FALSE;
                }

                if (!deleteFile && Configuration.FileListDestination == 0) // clipboard
                {
                    uint64_t fileSize = 0;
                    if (gFileSystem->GetHandleFileSize(hFile, &fileSize).success &&
                        fileSize > 0 && fileSize <= MAXDWORD)
                    {
                        gFileSystem->SeekHandle(hFile, 0, FILE_BEGIN, NULL);
                        char* buff = (char*)malloc((size_t)fileSize);
                        if (buff != NULL)
                        {
                            DWORD read = 0;
                            FileResult readResult = gFileSystem->ReadFromHandle(
                                hFile, buff, (DWORD)fileSize, &read);
                            if (readResult.success)
                            {
                                if (read >= 3 &&
                                    (BYTE)buff[0] == 0xEF && (BYTE)buff[1] == 0xBB && (BYTE)buff[2] == 0xBF)
                                {
                                    std::wstring wideText;
                                    if (Win32DecodeText(CP_UTF8, buff + 3, read - 3, wideText).Succeeded())
                                        CopyTextToClipboardW(wideText.c_str(), (int)wideText.length(), FALSE, NULL);
                                }
                                else
                                    CopyAcpTextToClipboard(buff, read, FALSE, NULL);
                            }
                            else
                            {
                                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE),
                                                     GetErrorTextOwned(readResult.errorCode).c_str());
                            }
                            free(buff);
                        }
                        else
                            TRACE_E(LOW_MEMORY);
                    }
                }
                HANDLES_REMOVE(hFile, __htFile, "IFileSystem::CloseHandle");
                gFileSystem->CloseFileHandle(hFile);

                // if the destination was the clipboard, delete the temporary file
                if (deleteFile || Configuration.FileListDestination == 0) // clipboard
                    gFileSystem->DeleteFile(fileNameW.c_str());           // genuine wide path
                else
                {
                    if (Configuration.FileListDestination == 1) // viewer
                    {
                        // show the file in the internal viewer, which will delete it afterwards
                        CSalamanderPluginInternalViewerData viewerData;
                        viewerData.Size = sizeof(viewerData);
                        viewerData.FileName = fileNameW.c_str();
                        viewerData.Mode = 0; // text mode
                        const std::wstring caption = LoadStrOwned(IDS_MAKEFILELIST_OUTPUT);
                        viewerData.Caption = caption.c_str();
                        viewerData.WholeCaption = TRUE;
                        int error;
                        if (!ViewFileInPluginViewerW(fileNameW.c_str(), NULL, &viewerData, TRUE,
                                                     NULL, L"mfl.txt", error))
                        {
                            // delete the file even when opening fails
                        }
                    }
                }

                if (Configuration.FileListDestination == 2) // file
                {
                    //---  refresh manually refreshed directories
                    // change in the directory where the file list was created.
                    // CutDirectory and the notification queue both retain the genuine wide path.
                    CutDirectory(&fileNameW[0]);
                    fileNameW.resize(wcslen(fileNameW.c_str()));
                    MainWindow->PostChangeOnPathNotificationW(fileNameW.c_str(), FALSE);
                }
            }
            else
            {
                DWORD err = GetLastError();
                std::wstring msg = FormatStrW(LoadStrW(IDS_FILEERRORFORMAT), fileNameW.c_str(), GetErrorTextOwned(err).c_str()); // genuine wide path
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
            }
        }
    }
    EndStopRefresh(); // the snooper will start again now
}

// see description in mainwnd.h
BOOL GetNextFileFromPanel(int index, std::wstring& path, std::wstring& name, void* param)
{
    CALL_STACK_MESSAGE2("GetNextFileFromPanel(%d, , ,)", index);
    CUMDataFromPanel* data = (CUMDataFromPanel*)param;
    if (data->Count == -1) // retrieving data
    {
        BOOL upDir = (data->Window->Dirs->Count != 0 &&
                      wcscmp(data->Window->Dirs->At(0).Name, L"..") == 0);
        data->Count = data->Window->GetSelCount();
        if (data->Count < 0)
            data->Count = 0;
        if (data->Count == 0) // no selection -> use the focused item
        {
            index = data->Window->GetCaretIndex();
            data->Index = NULL;
            path = data->Window->GetPathW();
            if (index < 0 || index >= data->Window->Dirs->Count + data->Window->Files->Count ||
                index == 0 && upDir)
            {
                name.clear(); // for up-dir or for the first item of an empty panel the name will be empty...
            }
            else // copy the name for others
            {
                CFileData* f = &((index < data->Window->Dirs->Count) ? data->Window->Dirs->At(index) : data->Window->Files->At(index - data->Window->Dirs->Count));
                name = f->Name;
            }
            return TRUE;
        }
        data->Index = new int[data->Count];
        if (data->Index == NULL)
            return FALSE; // error
        data->Window->GetSelItems(data->Count, data->Index);
    }
    if (index >= 0 && index < data->Count)
    {
        CFileData* f = &((data->Index[index] < data->Window->Dirs->Count) ? data->Window->Dirs->At(data->Index[index]) : data->Window->Files->At(data->Index[index] - data->Window->Dirs->Count));
        path = data->Window->GetPathW();
        name = f->Name;
        return TRUE;
    }
    else
    {
        if (data->Index != NULL)
        {
            delete[] (data->Index);
            data->Index = NULL;
        }
        data->Window->SetSel(FALSE, -1, TRUE);                        // explicit redraw
        PostMessage(data->Window->HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
        return FALSE;
    }
}

BOOL CheckIfCanBeExecuted(BOOL buildBat, size_t commandLen, size_t argumentsLen)
{
    /*  MEASURED LIMITS:
  Batch file:
    W2K: 2041 including executable name, spaces and parameters
    XP64/XP: 8185 including executable name, spaces and parameters
    Win7/Vista: 32776 including executable name, spaces and parameters

  ShellExecuteEx:
    XP/XP64/Vista/W2K: 2080 including executable name (without quotes), spaces and parameters
    Win7: 32764 including executable name (without quotes), spaces and parameters
*/

    // WARNING: if a .bat file is executed that runs a .exe and passes all parameters (%*), a long .exe name
    // can still trigger "too long name" error even when respecting the limit here. The limit is exceeded once
    // parameters are passed to the .exe. (I wouldn't solve this issue; it would require parsing
    // .bat files etc., which is just nonsense.)

    size_t cmdLineLen = commandLen + argumentsLen + 1; // +1 for the space between command and arguments
    if (buildBat)                                   // launching via a .bat file
    {
        if (WindowsVistaAndLater)
            return cmdLineLen <= 8191; // Vista/Win7: in reality it's 32776 but only 8191 works (with longer parameters, probably due to a Windows bug, characters get erased; tested on Vista and Win7)
        return cmdLineLen <= 8185;     // XP/XP64
    }
    else // launching via ShellExecuteEx
    {
        if (Windows7AndLater)
            return cmdLineLen <= 32764; // Win7
        return cmdLineLen <= 2080;      // W2K/XP/XP64/Vista
    }
}

//*****************************************************************************
//
// ExpandCommand2
//
// parent       - parent window (for error dialogs)
// cmd          - buffer for the expanded command
// cmdSize      - size of 'cmd' buffer
// args         - buffer for receiving arguments
// argsSize     - size of 'args' buffer
// buildBat     - if TRUE, arguments are placed into 'cmd'
// initDir      - buffer for the path where the execution should take place
// initDirSize  - length of initDir buffer
// item         - user-menu item
// path         - long path to the file
// longName     - long filename
// fileNameUsed - returns TRUE if a file name or path was used during argument expansion
// userMenuAdvancedData - advanced parameter`s values for User Menu: the Arguments array
// ignoreEnvVarNotFoundOrTooLong - see ExpandVarString description
//
// returns success of the operation

BOOL ExpandCommand2(HWND parent,
                    std::wstring& cmd,
                    std::wstring& args, BOOL buildBat,
                    std::wstring& initDir,
                    CUserMenuItem* item,
                    const std::wstring& path,
                    const std::wstring& longName,
                    BOOL* fileNameUsed,
                    CUserMenuAdvancedData* userMenuAdvancedData,
                    BOOL ignoreEnvVarNotFoundOrTooLong)
{
    CALL_STACK_MESSAGE3("ExpandCommand2(, , , , , %ls, %ls, )", path.c_str(), longName.c_str());

    *fileNameUsed = FALSE;
    std::wstring command;
    if (ExpandCommand(parent, item->UMCommand.c_str(), command, ignoreEnvVarNotFoundOrTooLong))
    {
        if (!path.empty())
        {
            std::wstring fileName = path;
            if (!fileName.empty() && fileName.back() == L'\\')
                fileName.pop_back();

            std::wstring dosName;
            if (!longName.empty())
            {
                SalPathAppendW(fileName, longName.c_str());
                dosName = GetShortPathW(fileName.c_str());
                if (dosName.empty())
                {
                    TRACE_E("GetShortPathName() failed");
                }
            }
            else
            {
                if (fileName.length() == 2 && fileName[1] == L':') // append '\\' after a standard root path
                    fileName.push_back(L'\\');
                dosName = GetShortPathW(fileName.c_str());
                if (dosName.empty())
                {
                    TRACE_E("GetShortPathName() failed");
                }
                else
                    SalPathAddBackslashW(dosName);
                SalPathAddBackslashW(fileName);
            }

            std::wstring expArguments;
            if (ExpandUserMenuArguments(parent, fileName.c_str(), dosName.c_str(), item->Arguments.c_str(), expArguments,
                                        fileNameUsed, userMenuAdvancedData,
                                        ignoreEnvVarNotFoundOrTooLong) &&
                ExpandInitDir(parent, fileName.c_str(), dosName.c_str(), item->InitDir.c_str(), initDir,
                              ignoreEnvVarNotFoundOrTooLong))
            {
                if (CheckIfCanBeExecuted(buildBat, command.length(), expArguments.length()))
                {
                    if (!buildBat) // launching via ShellExecuteEx: pass arguments separately
                    {
                        cmd = std::move(command);
                        args = std::move(expArguments);
                        return TRUE;
                    }
                    else // launching via .bat: append arguments after the command
                    {
                        cmd = std::move(command);
                        cmd.push_back(L' ');
                        cmd.append(expArguments);
                        args.clear();
                        return TRUE;
                    }
                }
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_USRMNUTOOLONGCMDORARGS));
            }
        }
        else
        {
            if (CheckIfCanBeExecuted(buildBat, command.length(), 0))
            {
                cmd = std::move(command);
                args.clear();
                initDir.clear();
                return TRUE;
            }
            else
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_USRMNUTOOLONGCMDORARGS));
            }
        }
    }
    cmd.clear();
    args.clear();
    initDir.clear();
    return FALSE;
}

/*
void RemoveRedundantBackslahes(char *text)
{
  if (text == NULL)
  {
    TRACE_E("Unexpected situation in RemoveRedundantBackslahes().");
    return;
  }
  if (strlen(text) < 3)
    return;
  char *s = text + 2;
  char *d = s;
  while (*s != 0) 
  {
    *d = *s;
    if (*s == '\\')
    {
      while (*s == '\\') s++;
      d++;
    }
    else
    {
      s++;
      d++;
    }
  }
  *d = 0;
}
*/

void CMainWindow::UserMenu(HWND parent, int itemIndex, UM_GetNextFileName getNextFile, void* data,
                           CUserMenuAdvancedData* userMenuAdvancedData)
{
    CALL_STACK_MESSAGE2("CMainWindow::UserMenu(%d, ,)", itemIndex);
    if (itemIndex >= 0 && itemIndex < UserMenuItems->Count)
    {
        UpdateWindow(parent);

        int errorPos1, errorPos2;
        CUserMenuValidationData userMenuValidationData;
        BOOL ok = TRUE;
        if (ValidateUserMenuArguments(parent, UserMenuItems->At(itemIndex)->Arguments.c_str(), errorPos1, errorPos2,
                                      &userMenuValidationData))
        {
            if (userMenuValidationData.UsesListOfSelNames && userMenuAdvancedData->ListOfSelNames.empty())
            {
                gPrompter->ShowError(LoadStrW(IDS_USERMENUERROR), LoadStrW(userMenuAdvancedData->ListOfSelNamesIsEmpty ? IDS_EMPTYLISTOFSELNAMES : IDS_TOOLONGLISTOFSELNAMES));
                ok = FALSE;
            }
            if (ok && userMenuValidationData.UsesListOfSelFullNames && userMenuAdvancedData->ListOfSelFullNames.empty())
            {
                gPrompter->ShowError(LoadStrW(IDS_USERMENUERROR), LoadStrW(userMenuAdvancedData->ListOfSelFullNamesIsEmpty ? IDS_EMPTYLISTOFSELFULLNAMES : IDS_TOOLONGLISTOFSELFULLNAMES));
                ok = FALSE;
            }
            if (ok && userMenuValidationData.UsesFullPathLeft && userMenuAdvancedData->FullPathLeft.empty())
            {
                gPrompter->ShowError(LoadStrW(IDS_USERMENUERROR), LoadStrW(IDS_NOTDEFFULLPATHLEFT));
                ok = FALSE;
            }
            if (ok && userMenuValidationData.UsesFullPathRight && userMenuAdvancedData->FullPathRight.empty())
            {
                gPrompter->ShowError(LoadStrW(IDS_USERMENUERROR), LoadStrW(IDS_NOTDEFFULLPATHRIGHT));
                ok = FALSE;
            }
            if (ok && userMenuValidationData.UsesFullPathInactive &&
                (userMenuAdvancedData->FullPathInactive == nullptr || userMenuAdvancedData->FullPathInactive->empty()))
            {
                gPrompter->ShowError(LoadStrW(IDS_USERMENUERROR), LoadStrW(IDS_NOTDEFFULLPATHINACTIVE));
                ok = FALSE;
            }
            if (ok && userMenuValidationData.UsedCompareType != 0)
            {
                if ((userMenuValidationData.UsedCompareType == 6 /* file-or-dir-left-right */ ||
                     userMenuValidationData.UsedCompareType == 7 /* file-or-dir-active-inactive */) &&
                    userMenuAdvancedData->CompareName1.empty() &&
                    userMenuAdvancedData->CompareName2.empty())
                { // we don't know if files or directories should be compared, ask the user (the name selection dialog differs for files/directories)
                    MSGBOXEX_PARAMS params;
                    memset(&params, 0, sizeof(params));
                    params.HParent = parent;
                    params.Flags = MB_YESNO | MB_ICONQUESTION | MSGBOXEX_SILENT;
                    params.Caption = LoadStrW(IDS_QUESTION);
                    params.Text = LoadStrW(IDS_COMPAREFILESORDIRS);
                    /* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   we let the message box buttons resolve hotkey collisions by pretending it's a menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_MSGBOXBTN_FILES
  {MNTT_IT, IDS_MSGBOXBTN_DIRS
  {MNTT_PE, 0
};
*/
                    const std::wstring aliasBtnNames = FormatStrW(L"%d\t%ls\t%d\t%ls",
                                                                 DIALOG_YES, LoadStrW(IDS_MSGBOXBTN_FILES),
                                                                 DIALOG_NO, LoadStrW(IDS_MSGBOXBTN_DIRS));
                    params.AliasBtnNames = aliasBtnNames.c_str();
                    userMenuAdvancedData->CompareNamesAreDirs = (SalMessageBoxEx(&params) == DIALOG_NO);
                }

                BOOL swapNames = FALSE;
                BOOL clearNames = FALSE;
                BOOL comparingFiles = TRUE;
                switch (userMenuValidationData.UsedCompareType)
                {
                case 1: // file-left-right
                {
                    if (userMenuAdvancedData->CompareNamesReversed)
                        swapNames = TRUE;
                    if (userMenuAdvancedData->CompareNamesAreDirs)
                        clearNames = TRUE;
                    break;
                }

                case 2: // file-active-inactive
                {
                    if (userMenuAdvancedData->CompareNamesAreDirs)
                        clearNames = TRUE;
                    break;
                }

                case 3: // dir-left-right
                {
                    comparingFiles = FALSE;
                    if (userMenuAdvancedData->CompareNamesReversed)
                        swapNames = TRUE;
                    if (!userMenuAdvancedData->CompareNamesAreDirs)
                        clearNames = TRUE;
                    break;
                }

                case 4: // dir-active-inactive
                {
                    comparingFiles = FALSE;
                    if (!userMenuAdvancedData->CompareNamesAreDirs)
                        clearNames = TRUE;
                    break;
                }

                case 6: // file-or-dir-left-right
                {
                    comparingFiles = !userMenuAdvancedData->CompareNamesAreDirs;
                    if (userMenuAdvancedData->CompareNamesReversed)
                        swapNames = TRUE;
                    break;
                }

                case 7: // file-or-dir-active-inactive
                {
                    comparingFiles = !userMenuAdvancedData->CompareNamesAreDirs;
                    break;
                }
                }
                if (clearNames)
                {
                    userMenuAdvancedData->CompareName1.clear();
                    userMenuAdvancedData->CompareName2.clear();
                }
                else
                {
                    if (swapNames)
                        std::swap(userMenuAdvancedData->CompareName1, userMenuAdvancedData->CompareName2);
                }
                if (Configuration.CnfrmShowNamesToCompare ||
                    userMenuAdvancedData->CompareName1.empty() ||
                    userMenuAdvancedData->CompareName2.empty())
                {
                    CCompareArgsDlg dlg(parent, comparingFiles, userMenuAdvancedData->CompareName1,
                                        userMenuAdvancedData->CompareName2, &Configuration.CnfrmShowNamesToCompare);
                    if (dlg.Execute() != IDOK)
                        ok = FALSE;
                }
            }
        }
        else
            ok = FALSE;
        if (ok)
        {
            BOOL buildBat = UserMenuItems->At(itemIndex)->ThroughShell;
            BOOL batNotEmpty = FALSE;
            const wchar_t* batName;
            HANDLE file;
            wchar_t batUniqueName[50]; // we need a unique name for the batch file in the cache
            DWORD lastErr;

            // Try to get a unique name for the batch file (retry loop replaces goto _TRY_AGAIN)
            while (buildBat)
            {
                wsprintfW(batUniqueName, L"Usermenu %X", GetTickCount());
                BOOL exists;
                batName = DiskCache.GetName(batUniqueName, L"usermenu.bat", &exists, TRUE, NULL, FALSE, NULL, NULL);
                if (batName == NULL) // error (if 'exists' is TRUE -> fatal, otherwise "file already exists")
                {
                    if (!exists) // file exists -> almost impossible, handle anyway
                    {
                        Sleep(100);
                        continue; // retry with new name
                    }
                    return; // fatal error
                }
                break; // got a unique name
            }

            // RAII guard for DiskCache name - will call ReleaseName in destructor if not dismissed
            CDiskCacheNameGuard cacheGuard(DiskCache, buildBat ? batUniqueName : nullptr, FALSE);

            if (buildBat)
            {
                file = gFileSystem->CreateFile(batName, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                               FILE_ATTRIBUTE_TEMPORARY, NULL);
                lastErr = GetLastError();
                HANDLES_ADD_EX(__otQuiet, file != INVALID_HANDLE_VALUE, __htFile,
                               __hoCreateFile, file, lastErr, TRUE);
                if (file == INVALID_HANDLE_VALUE)
                {
                    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), GetErrorTextOwned(lastErr).c_str());
                    UpdateWindow(parent);
                    return; // cacheGuard destructor will call ReleaseName
                }
            }

            // build the .bat file
            int index;
            index = 0;
            std::wstring cmdLine;
            std::wstring arguments;
            std::wstring initDir;
            std::wstring prevInitDir;
            std::wstring path;
            std::wstring name;
            BOOL error;
            error = FALSE;
            BOOL skipErrorMessage;
            skipErrorMessage = FALSE;
            DWORD written;
            auto writeExact = [&written, &file](const void* bytes, DWORD size)
            {
                FileResult result = gFileSystem->WriteToHandle(file, bytes, size, &written);
                if (!result.success)
                    SetLastError(result.errorCode);
                return result.success && written == size;
            };
            BOOL fileNameUsed;
            BOOL firstRound;
            firstRound = TRUE;
            while (getNextFile(index, path, name, data))
            {
                prevInitDir = initDir;
                BOOL expandOK = ExpandCommand2(parent,
                                               cmdLine,
                                               arguments, buildBat, // if we are running via a batch file, allow
                                               initDir,             // arguments will be inserted into cmdLine
                                               UserMenuItems->At(itemIndex),
                                               path, name, &fileNameUsed,
                                               userMenuAdvancedData,
                                               !firstRound);
                if (!expandOK)
                {
                    error = TRUE;
                    skipErrorMessage = TRUE;
                    break;
                }
                if (expandOK && (firstRound || fileNameUsed || initDir != prevInitDir)) // block running the same command for all items (a user mistake that happens often)
                {
                    if (buildBat) // building a .bat file
                    {
                        // A cmd.exe batch file is an OEM-encoded byte protocol. Keep that encoding
                        // explicit and reject unrepresentable Unicode instead of silently targeting
                        // a best-fit-mapped file.
                        std::string initDirOEM;
                        std::string cmdLineOEM;
                        const Win32TextConversionResult initConversion = Win32EncodeText(CP_OEMCP, initDir, initDirOEM);
                        const Win32TextConversionResult commandConversion = Win32EncodeText(CP_OEMCP, cmdLine, cmdLineOEM);
                        if (!initConversion || !commandConversion)
                        {
                            SetLastError(!initConversion ? initConversion.Win32Error : commandConversion.Win32Error);
                            error = TRUE;
                            break;
                        }
                        batNotEmpty = TRUE;
                        const bool driveDirectory = initDirOEM.length() >= 2 && initDirOEM[1] == ':';
                        if ((driveDirectory && // "@C:" followed by "@cd C:\\path"
                             (!writeExact("@", 1) ||
                              !writeExact(initDirOEM.data(), 2) ||
                              !writeExact("\r\n", 2) ||
                              !writeExact("@cd \"", 5) ||
                              !writeExact(initDirOEM.data(), (DWORD)initDirOEM.length()) ||
                              !writeExact("\"\r\n", 3))) ||
                            !writeExact("call ", 5) ||
                            !writeExact(cmdLineOEM.data(), (DWORD)cmdLineOEM.length()) ||
                            !writeExact("\r\n", 2))
                        {
                            error = TRUE;
                            break;
                        }
                    }
                    else // direct execution
                    {
                        // the original launching via CreateProcess couldn't run screen savers (*.SCR)
                        // or Control Panel items (*.cpl) and people kept complaining
                        //
                        // try executing it via ShellExecuteEx - it appears to work ;-)
                        // additionally, launch restrictions will be handled

                        // set correct default directories for individual drives
                        MainWindow->SetDefaultDirectories(initDir.empty() ? NULL : initDir.c_str());

                        // to work with old configurations, remove the " character from the start and end of cmdLine
                        if (cmdLine.length() > 1 && cmdLine.front() == L'\"' && cmdLine.back() == L'\"')
                        {
                            cmdLine.erase(cmdLine.begin());
                            cmdLine.pop_back();
                        }
                        // better not swallow backslashes so that we don't destroy some OLE paths
                        //RemoveRedundantBackslahes(cmdLine); // ShellExecuteEx dislikes multiple backslashes, "$(SalDir)\sally.exe"

                        CShellExecuteWnd shellExecuteWnd;
                        SHELLEXECUTEINFOW sei;
                        memset(&sei, 0, sizeof(SHELLEXECUTEINFOW));
                        sei.cbSize = sizeof(SHELLEXECUTEINFOW);
                        sei.hwnd = shellExecuteWnd.Create(parent, L"SEW: CMainWindow::UserMenu"); // handle to any message boxes that the system might produce while executing
                        sei.lpFile = cmdLine.c_str();
                        sei.lpParameters = arguments.c_str();
                        sei.lpDirectory = initDir.empty() ? NULL : initDir.c_str();
                        sei.nShow = SW_SHOWNORMAL;

                        if (!ShellExecuteExW(&sei))
                        {
                            DWORD err = GetLastError();
                            std::wstring displayCommand = cmdLine;
                            if (displayCommand.length() > 516)
                                displayCommand.replace(513, std::wstring::npos, L"...");
                            std::wstring msg = FormatStrW(LoadStrW(IDS_EXECERROR), displayCommand.c_str(), GetErrorTextOwned(err).c_str());
                            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                            break;
                        }
                    }
                }
                index++;
                firstRound = FALSE;
                if (userMenuValidationData.MustHandleItemsAsGroup)
                    break; // in this mode, only one command is executed for all selected items
            }

            if (buildBat)
            {
                lastErr = GetLastError();
                uint64_t size = 0;
                gFileSystem->GetHandleFileSize(file, &size);
                HANDLES_REMOVE(file, __htFile, "IFileSystem::CloseHandle");
                gFileSystem->CloseFileHandle(file);

                DiskCache.NamePrepared(batUniqueName,
                                       CQuadWord((DWORD)size, (DWORD)(size >> 32)));

                if (!error) // run the .bat
                {
                    if (batNotEmpty)
                    {
                        MainWindow->SetDefaultDirectories(initDir.empty() ? NULL : initDir.c_str());

                        CommandShellRequest request;
                        std::wstring batNameW = batName;
                        std::wstring initDirW;
                        if (!initDir.empty())
                            initDirW = initDir;

                        request.command = batNameW;
                        request.workingDirectory = initDirW;
                        request.windowTitle = LoadStrW(IDS_COMMANDSHELL);
                        request.keepOpen = UserMenuItems->At(itemIndex)->UseWindow &&
                                           !UserMenuItems->At(itemIndex)->CloseShell;
                        request.hideWindow = !UserMenuItems->At(itemIndex)->UseWindow;
                        request.useShowWindow = true;
                        request.showWindow = (UserMenuItems->At(itemIndex)->UseWindow ? SW_SHOWNORMAL : SW_HIDE);

                        POINT p;
                        if (UserMenuItems->At(itemIndex)->UseWindow &&
                            MultiMonGetDefaultWindowPos(MainWindow->HWindow, &p))
                        {
                            // if the main window is on another monitor, we should open
                            // the new window there, preferably at the default position (as on the primary monitor)
                            request.usePosition = true;
                            request.x = p.x;
                            request.y = p.y;
                        }

                        CommandShellResult result = gCommandShellService != NULL
                                                        ? gCommandShellService->LaunchCommand(request)
                                                        : CommandShellResult::Error(ERROR_INVALID_PARAMETER);
                        if (!result.success)
                        {
                            std::wstring msg = FormatStrW(LoadStrW(IDS_EXECERROR), batNameW.c_str(), GetErrorTextOwned(result.errorCode).c_str());
                            cacheGuard.ReleaseNow();
                            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                        }
                        else
                        {
                            HANDLE processHandle = result.DetachNativeProcessHandle();
                            if (processHandle == NULL)
                            {
                                DWORD err = GetLastError();
                                result.CloseProcess();
                                cacheGuard.ReleaseNow();
                                std::wstring msg = FormatStrW(LoadStrW(IDS_EXECERROR), batNameW.c_str(), GetErrorTextOwned(err).c_str());
                                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                            }
                            else
                            {
                                HANDLES_ADD(__htProcess, __hoCreateProcess, processHandle);
                                DiskCache.AssignName(batUniqueName, processHandle, TRUE, crtDirect);
                                cacheGuard.Dismiss(); // ownership transferred to AssignName
                            }
                        }
                    }
                    else // an empty .BAT is not worth running (in case of low memory or other crazy errors)
                    {
                        cacheGuard.ReleaseNow();
                    }
                }
                else // error occurred during script building
                {
                    cacheGuard.ReleaseNow();
                    if (!skipErrorMessage)
                    {
                        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), GetErrorTextOwned(lastErr).c_str());
                    }
                }
            }
        }
    }
    UpdateWindow(parent);
}

void CMainWindow::SetDefaultDirectories(const wchar_t* curPath)
{
    CALL_STACK_MESSAGE2("CMainWindow::SetDefaultDirectories(%ls)", curPath);
    //---  restore DefaultDir
    MainWindow->UpdateDefaultDir(TRUE);
    //---  set environment variables
    wchar_t name[4] = L"= :";
    const wchar_t* dir;
    wchar_t d;
    for (d = L'a'; d <= L'z'; d++)
    {
        name[1] = d;
        if (curPath != NULL && d == towlower(curPath[0])) // UNC paths are ignored
            dir = curPath;
        else
            dir = DefaultDir[d - L'a'].c_str();

        if (dir[1] == L':' && dir[2] == L'\\' && dir[3] == 0)
            SetEnvironmentVariableW(name, NULL);
        else
            SetEnvironmentVariableW(name, dir);
    }
}

BOOL CMainWindow::HandleCtrlLetter(wchar_t c)
{
    CALL_STACK_MESSAGE2("CMainWindow::HandleCtrlLetter(%u)", c);
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
    { // change drive via Shift+letter
        GetActivePanel()->ChangeDrive(c);
    }
    else // NC + Windows Ctrl+? hotkeys
    {
        WPARAM cmd;
        switch (c) // only upper-case characters reach here
        {
        case 'A':
            cmd = CM_ACTIVESELECTALL;
            break;

        case 'C': // copy
        case 'X': // cut
        {
            BOOL files = FALSE;
            if (GetActivePanel() != NULL)
            {
                if (GetActivePanel()->GetCaretIndex() == 0)
                {
                    if (0 == GetActivePanel()->Dirs->Count ||
                        wcscmp(GetActivePanel()->Dirs->At(0).Name, L"..") != 0)
                    {
                        files = GetActivePanel()->Dirs->Count + GetActivePanel()->Files->Count > 0;
                    }
                    else
                    {
                        int count = GetActivePanel()->GetSelCount();
                        if (count == 1)
                        {
                            int index;
                            GetActivePanel()->GetSelItems(1, &index);
                            files = index != 0;
                        }
                        else
                            files = count > 0;
                    }
                }
                else
                    files = GetActivePanel()->GetCaretIndex() > 0;
            }
            if (!files)
                return FALSE; // cut and copy cannot be performed
            cmd = (c == 'C') ? CM_CLIPCOPY : CM_CLIPCUT;
            break;
        }

        case 'D':
            cmd = CM_ACTIVEUNSELECTALL;
            break;
        case 'E':
            cmd = CM_EMAILFILES;
            break;
        case 'F':
            cmd = CM_FINDFILE;
            break;
        case 'G':
            cmd = CM_ACTIVE_CHANGEDIR;
            break;
        case 'H':
            cmd = CM_TOGGLEHIDDENFILES;
            break;
        case 'I':
            cmd = CM_LAST_PLUGIN_CMD;
            break;
        case 'K':
            cmd = CM_CONVERTFILES;
            break;
        case 'L':
            cmd = CM_DRIVEINFO;
            break;
        case 'M':
            cmd = CM_FILELIST;
            break;
        case 'N':
            cmd = CM_TOGGLEELASTICSMART;
            break;
        case 'P':
            cmd = CM_SEC_PERMISSIONS;
            break;
        case 'Q':
            cmd = CM_OCCUPIEDSPACE;
            break;
        case 'R':
            cmd = CM_ACTIVEREFRESH;
            break;
        case 'S':
            cmd = CM_CLIPPASTELINKS;
            break;
        case 'T':
            cmd = CM_AFOCUSSHORTCUT;
            break;
        case 'U':
            cmd = CM_SWAPPANELS;
            break;
        case 'V':
            cmd = CM_CLIPPASTE;
            break;
        case 'W':
            cmd = CM_RESELECT;
            break;

        default:
            return FALSE;
        }
        SendMessage(HWindow, WM_COMMAND, cmd, 0);
    }
    return TRUE;
}

void CMainWindow::ChangePanel(BOOL force)
{
    CALL_STACK_MESSAGE1("CMainWindow::ChangePanel()");

    MainWindow->CancelPanelsUI(); // cancel QuickSearch and QuickEdit
    if (IsIconic(HWindow))
        return;

    CFilesWindow* p1 = GetActivePanel();
    CFilesWindow* p2 = GetNonActivePanel();

    BOOL change = FALSE;
    if (force || p2->CanBeFocused())
        change = TRUE;
    else
    {
        // if a panel is ZOOMed, minimize it and ZOOM the other one
        if (IsPanelZoomed(TRUE) || IsPanelZoomed(FALSE))
        {
            if (IsPanelZoomed(TRUE))
                SplitPosition = 0.0;
            else
                SplitPosition = 1.0;
            LayoutWindows();
            change = TRUE;
        }
    }

    if (change)
    {
        SetActivePanel(p2);

        // ensure the active panel header is redrawn
        if (p1->DirectoryLine != NULL)
            p1->DirectoryLine->InvalidateAndUpdate(FALSE);
        if (p2->DirectoryLine != NULL)
            p2->DirectoryLine->InvalidateAndUpdate(FALSE);

        UpdateDriveBars(); // press the correct drive in the drive bar

        //    ReleaseMenuNew();
        if (EditMode)
        {
            p1->RedrawIndex(p1->FocusedIndex);
            int i = p2->GetCaretIndex();
            i = max(i, 0);
            p2->SetCaretIndex(i, TRUE);
            p2->RedrawIndex(i);
        }
        else
        {
            if (GetFocus() != p2->GetListBoxHWND())
                SetFocus(p2->GetListBoxHWND());
            int i = p2->GetCaretIndex();
            i = max(i, 0);
            p2->SetCaretIndex(i, FALSE);
        }
        EditWindowSetDirectory();
        IdleRefreshStates = TRUE; // on the next Idle, force checking of state variables
        MainWindow->UpdateDefaultDir(TRUE);

        // broadcast this news to all loaded plugins
        Plugins.Event(PLUGINEVENT_PANELACTIVATED, p2 == LeftPanel ? PANEL_LEFT : PANEL_RIGHT);
    }
}

void CMainWindow::FocusPanel(CFilesWindow* focus, BOOL testIfMainWndActive)
{
    CALL_STACK_MESSAGE2("CMainWindow::FocusPanel(, %d)", testIfMainWndActive);
    MainWindow->CancelPanelsUI(); // cancel QuickSearch and QuickEdit

    if (!IsIconic(HWindow) && !focus->CanBeFocused())
        focus = ((focus == LeftPanel) ? RightPanel : LeftPanel);

    if (GetFocus() != focus->GetListBoxHWND())
    {
        if (!testIfMainWndActive || GetForegroundWindow() == HWindow) // focus only if main window is active (FTP plugin with non-modal Welcome Message window could focus the panel on command line shutdown -> deactivate Welcome Message)
            SetFocus(focus->GetListBoxHWND());
        else
            focus->OnSetFocus(FALSE); // simulate focus in the panel
    }

    CFilesWindow* old = GetActivePanel();
    SetActivePanel(focus);

    UpdateDriveBars(); // press the correct drive in the drive bar

    // ensure the active panel header is redrawn
    if (old != focus)
    {
        // activated a different panel, let it set its enablers
        RefreshCommandStates();
        // fixes a bug (present in 2.5b10) when users had one panel active with focus on UpDir
        // and then right-clicked a file in the passive panel and chose DELETE from the context menu
        // nothing happened because the EnablerFilesDelete enabler was FALSE (not updated for the new panel)
        // see https://forum.altap.cz/viewtopic.php?t=181

        // repaint the directory line of both panels
        if (old->DirectoryLine != NULL)
            old->DirectoryLine->InvalidateAndUpdate(FALSE);
        if (focus->DirectoryLine != NULL)
            focus->DirectoryLine->InvalidateAndUpdate(FALSE);
        //    ReleaseMenuNew();
        EditWindowSetDirectory();
        IdleRefreshStates = TRUE; // on the next Idle, force checking of state variables
        // broadcast this news to loaded plugins
        Plugins.Event(PLUGINEVENT_PANELACTIVATED, focus == LeftPanel ? PANEL_LEFT : PANEL_RIGHT);
    }
    else if (EditWindow != NULL)
        EditWindowSetDirectory();
    //---  restore DefaultDir
    MainWindow->UpdateDefaultDir(TRUE);
}

void CMainWindow::ShowCommandLine(BOOL focusLine)
{
    CALL_STACK_MESSAGE2("CMainWindow::ShowCommandLine(%d)", focusLine);
    if (EditWindow == NULL || EditWindow->HWindow != NULL)
        return;

    if (!EditWindow->Create(HWindow, IDC_EDITWINDOW))
        TRACE_E("Unable to create EditWindow.");
    else
    {
        LayoutWindows();
        EditWindow->RestoreContent();
        ShowWindow(EditWindow->HWindow, SW_SHOW);
        EditWindowSetDirectory();
        if (focusLine && EditWindow->IsEnabled())
            SetFocus(EditWindow->HWindow);
        IdleRefreshStates = TRUE; // on the next Idle, force checking of state variables
    }
}

void CMainWindow::HideCommandLine(BOOL storeContent, BOOL focusPanel)
{
    if (EditWindow == NULL || EditWindow->HWindow == NULL)
        return;

    if (storeContent)
        EditWindow->StoreContent();

    DestroyWindow(EditWindow->HWindow);
    if (focusPanel)
        FocusPanel(GetActivePanel());
    LayoutWindows();
    IdleRefreshStates = TRUE; // on the next Idle, force checking of state variables
}

//****************************************************************************
//
// Image Drag functions
//

void ImageDragBegin(int width, int height, int dxHotspot, int dyHotspot)
{
    if (ImageDragging)
        TRACE_E("ImageDragging == TRUE - this should never happen");
    ImageDragW = width;
    ImageDragH = height;
    ImageDragDxHotspot = dxHotspot;
    ImageDragDyHotspot = dyHotspot;
    ImageDragging = TRUE;
}

void ImageDragEnd()
{
    if (!ImageDragging)
        TRACE_E("ImageDragging == FALSE - this should never happen");
    ImageDragX = INT_MAX;
    ImageDragY = INT_MAX;
    ImageDragW = INT_MAX;
    ImageDragH = INT_MAX;
    ImageDragging = FALSE;
}

BOOL ImageDragInterfereRect(const RECT* rect)
{
    if (!ImageDraggingVisible)
        return FALSE;
    if (ImageDragX == INT_MAX || ImageDragY == INT_MAX)
    {
        TRACE_E("ImageDragX == INT_MAX || ImageDragY == INT_MAX");
        return TRUE; // just to be safe
    }
    if (ImageDragW == INT_MAX || ImageDragH == INT_MAX)
    {
        TRACE_E("ImageDragW == INT_MAX || ImageDragH == INT_MAX");
        return TRUE; // just to be safe
    }
    RECT r;
    r.left = ImageDragX - ImageDragDxHotspot;
    r.top = ImageDragY - ImageDragDyHotspot;
    r.right = r.left + ImageDragW;
    r.bottom = r.top + ImageDragH;
    RECT dstR;
    IntersectRect(&dstR, rect, &r);
    return !IsRectEmpty(&dstR);
}

void ImageDragEnter(int x, int y)
{
    CALL_STACK_MESSAGE3("ImageDragEnter(%d, %d)", x, y);
    if (ImageDraggingVisible)
        TRACE_E("ImageDraggingVisible == TRUE - this should never happen");
    if (!ImageDragging)
        TRACE_E("ImageDragging == FALSE - this should never happen");
    ImageDragX = x;
    ImageDragY = y;
    ShowCaretAfterDrop = MainWindow->EditWindow->HideCaret();
    ImageList_DragEnter(MainWindow->HWindow, x - MainWindow->WindowRect.left, y - MainWindow->WindowRect.top);
    ImageDraggingVisible = TRUE;
    ImageDraggingVisibleLevel = 1;
}

void ImageDragMove(int x, int y)
{
    CALL_STACK_MESSAGE3("ImageDragMove(%d, %d)", x, y);
    if (!ImageDragging)
        TRACE_E("ImageDragging == FALSE - this should never happen");
    ImageDragX = x;
    ImageDragY = y;
    ImageList_DragMove(x - MainWindow->WindowRect.left, y - MainWindow->WindowRect.top);
}

void ImageDragLeave()
{
    CALL_STACK_MESSAGE1("ImageDragLeave()");
    if (!ImageDragging)
        TRACE_E("ImageDragging == FALSE - this should never happen");
    ImageList_DragLeave(MainWindow->HWindow);
    ImageDraggingVisible = FALSE;
    ImageDraggingVisibleLevel = 0;
    ImageDragX = INT_MAX;
    ImageDragY = INT_MAX;
    if (ShowCaretAfterDrop)
    {
        MainWindow->EditWindow->ShowCaret();
        ShowCaretAfterDrop = FALSE;
    }
}

void ImageDragShow(BOOL show)
{
    CALL_STACK_MESSAGE2("ImageDragShow(%d)", show);
    if (!ImageDragging)
        TRACE_E("ImageDragging == FALSE - this should never happen");
    ImageDraggingVisibleLevel += show ? 1 : -1;

    if (show && ImageDraggingVisibleLevel == 1)
    {
        ImageList_DragShowNolock(TRUE);
        ImageDraggingVisible = TRUE;
    }
    if (!show && ImageDraggingVisibleLevel == 0)
    {
        ImageList_DragShowNolock(FALSE);
        ImageDraggingVisible = FALSE;
    }
}

//****************************************************************************
//
// Context Help (Shift+F1) support
//

/////////////////////////////////////////////////////////////////////////////
// useful message ranges

#define WM_SYSKEYFIRST WM_SYSKEYDOWN
#define WM_SYSKEYLAST WM_SYSDEADCHAR

#define WM_NCMOUSEFIRST WM_NCMOUSEMOVE
#define WM_NCMOUSELAST WM_NCMBUTTONDBLCLK

HWND GetParentOwner(HWND hWnd)
{
    CALL_STACK_MESSAGE_NONE
    // return parent in the Windows sense
    return (GetWindowLongPtr(hWnd, GWL_STYLE) & WS_CHILD) ? GetParent(hWnd) : GetWindow(hWnd, GW_OWNER);
}

HWND GetTopLevelParent(HWND hWindow)
{
    CALL_STACK_MESSAGE_NONE
    HWND hWndParent = hWindow;
    HWND hWndT;
    while ((hWndT = GetParentOwner(hWndParent)) != NULL)
        hWndParent = hWndT;

    return hWndParent;
}

BOOL IsDescendant(HWND hWndParent, HWND hWndChild)
{
    CALL_STACK_MESSAGE_NONE
    // helper for detecting whether child descendent of parent
    //  (works with owned popups as well)
    if (!IsWindow(hWndParent))
    {
        TRACE_E("hWndParent is not window");
        return FALSE;
    }
    if (!IsWindow(hWndChild))
    {
        TRACE_E("hWndChild is not window");
        return FALSE;
    }

    do
    {
        if (hWndParent == hWndChild)
            return TRUE;

        hWndChild = GetParentOwner(hWndChild);
    } while (hWndChild != NULL);

    return FALSE;
}

DWORD
CMainWindow::MapClientArea(POINT point)
{
    DWORD dwContext = 0;

    CMainWindowsHitTestEnum hit = HitTest(point.x, point.y);
    CToolBar* toolbar = NULL;

    switch (hit)
    {
    case mwhteMenu:
        dwContext = IDH_MENUBAR;
        break;

    case mwhteTopToolbar:
    {
        dwContext = IDH_TOPTOOLBAR;
        toolbar = TopToolBar;
        break;
    }

    case mwhtePluginsBar:
        dwContext = IDH_PLUGINSBAR;
        break;

    case mwhteMiddleToolbar:
    {
        dwContext = IDH_MIDDLETOOLBAR;
        toolbar = MiddleToolBar;
        break;
    }

    case mwhteUMToolbar:
        dwContext = IDH_UMTOOLBAR;
        break;

    case mwhteHPToolbar:
        dwContext = IDH_HPTOOLBAR;
        break;

    case mwhteDriveBar:
        dwContext = IDH_DRIVEBAR;
        break;

    case mwhteCmdLine:
        dwContext = IDH_COMMANDLINE;
        break;

    case mwhteBottomToolbar:
    {
        dwContext = IDH_BOTTOMTOOLBAR;
        toolbar = BottomToolBar;
        break;
    }

    case mwhteSplitLine:
        dwContext = IDH_SPLITBAR;
        break;

    case mwhteLeftDirLine:
    {
        dwContext = IDH_DIRECTORYLINE;

        if (LeftPanel->DirectoryLine->ToolBar != NULL &&
            LeftPanel->DirectoryLine->ToolBar->HWindow != NULL)
            toolbar = LeftPanel->DirectoryLine->ToolBar;
        break;
    }

    case mwhteRightDirLine:
    {
        dwContext = IDH_DIRECTORYLINE;

        if (RightPanel->DirectoryLine->ToolBar != NULL &&
            RightPanel->DirectoryLine->ToolBar->HWindow != NULL)
            toolbar = RightPanel->DirectoryLine->ToolBar;
        break;
    }

    case mwhteLeftHeaderLine:
    case mwhteRightHeaderLine:
        dwContext = IDH_HEADERLINE;
        break;

    case mwhteLeftStatusLine:
    case mwhteRightStatusLine:
        dwContext = IDH_INFOLINE;
        break;

    case mwhteLeftWorkingArea:
    case mwhteRightWorkingArea:
        dwContext = IDH_WORKINGAREA;
        break;
    }

    // get the ID of the button the user clicked
    if (toolbar != NULL)
    {
        POINT p;
        p = point;
        ScreenToClient(toolbar->HWindow, &p);
        int index = toolbar->HitTest(p.x, p.y);
        if (index != -1)
        {
            TLBI_ITEM_INFO2 tii;
            tii.Mask = TLBI_MASK_ID;
            if (toolbar->GetItemInfo2(index, TRUE, &tii))
                dwContext = tii.ID;
        }
    }
    return dwContext;
}

DWORD
CMainWindow::MapNonClientArea(int iHit)
{
    DWORD dwContext = 0;
    switch (iHit)
    {
        /*
    case HTBORDER:
    case HTBOTTOM:
    case HTBOTTOMLEFT:
    case HTBOTTOMRIGHT:
    case HTLEFT:
    case HTRIGHT:
    case HTTOP:
    case HTTOPLEFT:
    case HTTOPRIGHT:
    case HTCAPTION:
    case HTREDUCE:
    case HTZOOM:
*/

    case HTMINBUTTON:
    case HTMAXBUTTON:
    case HTCLOSE:
        dwContext = IDH_MINMAXCLOSEBTNS;
        break;
    }
    return dwContext;
}

BOOL CMainWindow::CanEnterHelpMode()
{
    CALL_STACK_MESSAGE1("CMainWindow::CanEnterHelpMode()");
    if (HelpMode == HELP_ACTIVE) // already in help mode?
        return FALSE;

    if (HHelpCursor == NULL)
    {
        HHelpCursor = LoadCursor(NULL, IDC_HELP);
        if (HHelpCursor == NULL)
            return FALSE;
    }

    return TRUE;
}

void CMainWindow::OnContextHelp()
{
    CALL_STACK_MESSAGE1("CMainWindow::OnContextHelp()");
    // don't enter twice, and don't enter if initialization fails
    if (HelpMode == HELP_ACTIVE || !CanEnterHelpMode())
        return;

    // don't enter help mode with pending WM_USER_EXITHELPMODE message
    MSG msg;
    if (PeekMessageW(&msg, HWindow, WM_USER_EXITHELPMODE, WM_USER_EXITHELPMODE, PM_REMOVE | PM_NOYIELD))
        return;

    BOOL bHelpMode = HelpMode;
    if (HelpMode != HELP_INACTIVE && HelpMode != HELP_ENTERING)
        return;
    HelpMode = HELP_ACTIVE;

    if (bHelpMode == HELP_INACTIVE)
    {
        // need to delay help startup until later
        PostMessage(HWindow, WM_COMMAND, CM_HELP_CONTEXT, 0);
        HelpMode = HELP_ENTERING;
        return;
    }

    IdleRefreshStates = TRUE; // trigger idle update
    OnEnterIdle();            // redraw the toolbar

    if (HelpMode != HELP_ACTIVE)
        return;

    MenuBar->SetHelpMode(TRUE);

    // reset the bottom toolbar to its normal state
    BottomToolBar->SetState(btbsNormal);
    BottomToolBar->UpdateItemsState();

    // if someone is monitoring the mouse, stop monitoring
    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_QUERY;
    if (TrackMouseEvent(&tme) && tme.hwndTrack != NULL)
        SendMessage(tme.hwndTrack, WM_MOUSELEAVE, 0, 0);

    DWORD dwContext = 0;
    POINT point;

    GetCursorPos(&point);
    SetHelpCapture(point, NULL);
    LONG lIdleCount = 0;

    BOOL first = TRUE;

    HWND hDirtyWindow = NULL;
    while (HelpMode)
    {
        if (PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE))
        {
            if (!ProcessHelpMsg(msg, &dwContext, &hDirtyWindow))
                break;
            if (dwContext != 0)
                return;
        }
        else
        {
            if (first)
            {
                // buffer a mouse move to fall through to the toolbar when the cursor is over disabled buttons
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(HWindow, &p);
                PostMessage(HWindow, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
                // originally this buffering was before the loop, but that misbehaved
                // if a tooltip for a disabled button was shown and I pressed Shift+F1:
                // when the tooltip vanished, WM_MOUSELEAVE was delivered (PeekMessage distributes messages, see MSDN).
                // The button under the cursor then drew as enabled and then immediately as disabled again.
                // With this trick I wait until all messages are processed and MOUSEMOVE is definitely handled

                first = FALSE;
            }
            else
            {
                WaitMessage();
            }
        }
    }
    if (hDirtyWindow != NULL)
        SendMessage(hDirtyWindow, WM_USER_HELP_MOUSELEAVE, 0, 0);

    MenuBar->SetHelpMode(FALSE);
    HelpMode = HELP_INACTIVE;
    ReleaseCapture();

    // make sure the cursor is set appropriately
    SetCapture(HWindow);
    ReleaseCapture();

    if (dwContext != 0)
        OpenHtmlHelp(NULL, HWindow, HHCDisplayContext, dwContext, FALSE);

    IdleRefreshStates = TRUE; // trigger idle update
}

HWND CMainWindow::SetHelpCapture(POINT point, BOOL* pbDescendant)
// set or release capture, depending on where the mouse is
// also assign the proper cursor to be displayed.
{
    CALL_STACK_MESSAGE1("CMainWindow::SetHelpCapture(,)");
    if (!HelpMode)
        return NULL;

    HWND hWndCapture = GetCapture();
    HWND hWndHit = WindowFromPoint(point);
    HWND hTopHit = GetTopLevelParent(hWndHit);
    HWND hTopActive = GetTopLevelParent(GetActiveWindow());
    BOOL bDescendant = FALSE;
    DWORD hCurTask = GetCurrentThreadId();
    DWORD hTaskHit = hWndHit != NULL ? GetWindowThreadProcessId(hWndHit, NULL) : NULL;

    if (hTopActive == NULL || hWndHit == GetDesktopWindow())
    {
        if (hWndCapture == HWindow)
            ReleaseCapture();
        SetCursor(HHelpCursor);
    }
    else if (hTopActive == NULL ||
             hWndHit == NULL || hCurTask != hTaskHit ||
             !IsDescendant(HWindow, hWndHit))
    {
        if (hCurTask != hTaskHit)
            hWndHit = NULL;
        if (hWndCapture == HWindow)
            ReleaseCapture();
    }
    else
    {
        bDescendant = TRUE;
        if (hTopActive != hTopHit)
            hWndHit = NULL;
        else
        {
            if (hWndCapture != HWindow)
                SetCapture(HWindow);
            SetCursor(HHelpCursor);
        }
    }
    if (pbDescendant != NULL)
        *pbDescendant = bDescendant;
    return hWndHit;
}

BOOL CMainWindow::ProcessHelpMsg(MSG& msg, DWORD* pContext, HWND* hDirtyWindow)
{
    CALL_STACK_MESSAGE4("CMainWindow::ProcessHelpMsg(0x%X, 0x%IX, 0x%IX,)", msg.message, msg.wParam, msg.lParam);

    if (pContext == NULL)
    {
        TRACE_E("pContext == NULL");
        return FALSE;
    }
    if (msg.message == WM_USER_EXITHELPMODE ||
        (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE))
    {
        PeekMessageW(&msg, NULL, msg.message, msg.message, PM_REMOVE);
        return FALSE;
    }

    POINT point;
    if ((msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) ||
        (msg.message >= WM_NCMOUSEFIRST && msg.message <= WM_NCMOUSELAST))
    {
        BOOL bDescendant;
        HWND hWndHit = SetHelpCapture(msg.pt, &bDescendant);
        if (hWndHit == NULL)
        {
            PeekMessageW(&msg, NULL, msg.message, msg.message, PM_REMOVE); // eat the message
            return TRUE;
        }

        if (bDescendant)
        {
            if (msg.message != WM_LBUTTONDOWN)
            {
                // Hit one of our owned windows -- eat the message.
                PeekMessageW(&msg, NULL, msg.message, msg.message, PM_REMOVE);

                // notify windows that wish to highlight items during Shift+F1 mode
                if (msg.message == WM_MOUSEMOVE)
                {
                    if (*hDirtyWindow != NULL && *hDirtyWindow != hWndHit)
                        SendMessage(*hDirtyWindow, WM_USER_HELP_MOUSELEAVE, 0, 0);
                    *hDirtyWindow = hWndHit; // this window will need to receive a LEAVE message

                    POINT p = msg.pt;
                    ScreenToClient(hWndHit, &p);
                    SendMessage(hWndHit, WM_USER_HELP_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
                }
                return TRUE;
            }
            int iHit = (int)SendMessage(hWndHit, WM_NCHITTEST, 0,
                                        MAKELONG(msg.pt.x, msg.pt.y));
            if (iHit == HTSYSMENU)
            {
                if (GetCapture() != HWindow)
                {
                    TRACE_E("GetCapture() != HWindow");
                    return FALSE;
                }
                ReleaseCapture();
                // the message we peeked changes into a non-client because
                // of the release capture.
                GetMessageW(&msg, NULL, WM_NCLBUTTONDOWN, WM_NCLBUTTONDOWN);
                DispatchMessageW(&msg);
                GetCursorPos(&point);
                SetHelpCapture(point, NULL);
            }
            else if (iHit == HTCLIENT)
            {
                if (hWndHit == MenuBar->HWindow)
                {
                    PeekMessageW(&msg, NULL, msg.message, msg.message, PM_REMOVE);
                    ReleaseCapture();
                    POINT p = msg.pt;
                    ScreenToClient(hWndHit, &p);
                    msg.lParam = MAKELPARAM(p.x, p.y);
                    msg.hwnd = hWndHit;
                    msg.message = WM_MOUSEMOVE;
                    DispatchMessageW(&msg);
                    msg.message = WM_LBUTTONDOWN;
                    DispatchMessageW(&msg);
                    GetCursorPos(&point);
                    SetHelpCapture(point, NULL);
                }
                else
                {
                    *pContext = MapClientArea(msg.pt);
                    PeekMessageW(&msg, NULL, msg.message, msg.message, PM_REMOVE);
                    return FALSE;
                }
            }
            else
            {
                *pContext = MapNonClientArea(iHit);
                PeekMessageW(&msg, NULL, msg.message, msg.message, PM_REMOVE);
                return FALSE;
            }
        }
        else
        {
            // Hit one of our apps windows (or desktop) -- dispatch the message.
            PeekMessageW(&msg, NULL, msg.message, msg.message, PM_REMOVE);

            // Dispatch mouse messages that hit the desktop!
            DispatchMessageW(&msg);
        }
    }
    else if (msg.message == WM_SYSCOMMAND ||
             (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST))
    {
        if (GetCapture() != NULL)
        {
            ReleaseCapture();
            MSG msg2;
            while (PeekMessageW(&msg2, NULL, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE | PM_NOYIELD))
                ;
        }
        if (PeekMessageW(&msg, NULL, msg.message, msg.message, PM_NOREMOVE))
        {
            GetMessageW(&msg, NULL, msg.message, msg.message);

            // ensure sending messages to our menu (avoiding the need for a keyboard hook)
            // this supports entering the menu via Alt/F10/Alt+letter during help mode
            if (MenuBar == NULL || !MenuBar->IsMenuBarMessage(&msg))
            {
                TranslateMessage(&msg);
                if (msg.message == WM_SYSCOMMAND ||
                    (msg.message >= WM_SYSKEYFIRST &&
                     msg.message <= WM_SYSKEYLAST))
                {
                    // only dispatch system keys and system commands
                    DispatchMessageW(&msg);
                }
            }
        }
        GetCursorPos(&point);
        SetHelpCapture(point, NULL);
    }
    else
    {
        // allow all other messages to go through (capture still set)
        if (PeekMessageW(&msg, NULL, msg.message, msg.message, PM_REMOVE))
            DispatchMessageW(&msg);
    }

    return TRUE;
}

void CMainWindow::ExitHelpMode()
{
    CALL_STACK_MESSAGE1("CMainWindow::ExitHelpMode()");
    // if not in help mode currently, this is a no-op
    if (!HelpMode)
        return;

    // only post new WM_EXITHELPMODE message if one doesn't already exist
    //  in the queue.
    MSG msg;
    if (!PeekMessageW(&msg, HWindow, WM_USER_EXITHELPMODE, WM_USER_EXITHELPMODE, PM_REMOVE | PM_NOYIELD))
        PostMessage(HWindow, WM_USER_EXITHELPMODE, 0, 0);

    // release capture if this window has it
    if (GetCapture() == HWindow)
        ReleaseCapture();

    HelpMode = HELP_INACTIVE;
    IdleRefreshStates = TRUE; // trigger idle update
}

void CMainWindow::UpdateDriveBars()
{
    if (DriveBar == NULL || DriveBar2 == NULL)
        return;

    if (DriveBar->HWindow == NULL)
        return;

    if (DriveBar2->HWindow == NULL)
    {
        // when there is only one drive bar it belongs to the active panel
        DriveBar->SetCheckedDrive(GetActivePanel());
    }
    else
    {
        DriveBar->SetCheckedDrive(LeftPanel);
        DriveBar2->SetCheckedDrive(RightPanel);
    }
}

void CMainWindow::CancelPanelsUI()
{
    LeftPanel->CancelUI();
    RightPanel->CancelUI();
}

BOOL CMainWindow::QuickRenameWindowActive()
{
    return (LeftPanel->IsQuickRenameActive() || RightPanel->IsQuickRenameActive());
}

BOOL CMainWindow::DoQuickRename()
{
    if (LeftPanel->IsQuickRenameActive())
        return LeftPanel->HandeQuickRenameWindowKey(VK_RETURN);
    if (RightPanel->IsQuickRenameActive())
        return RightPanel->HandeQuickRenameWindowKey(VK_RETURN);
    return TRUE; // OK
}

//
// ****************************************************************************
// LockUI
//

void CMainWindow::LockUI(BOOL lock, HWND hToolWnd, const wchar_t* lockReason)
{
    if (LockedUI && lock)
    {
        TRACE_E("CMainWindow::LockUI(): main window is already locked! Ignoring this request...");
        return;
    }
    if (!LockedUI && !lock)
    {
        TRACE_E("CMainWindow::LockUI(): main window is not locked! Ignoring this request...");
        return;
    }

    LockedUI = lock;
    if (lock)
    {
        LockedUIToolWnd = hToolWnd;
        if (lockReason != NULL)
            LockedUIReason = lockReason;
    }
    else
    {
        LockedUIToolWnd = NULL;
        LockedUIReason.clear();
    }

    if (HTopRebar != NULL)
        EnableWindow(HTopRebar, !lock);
    if (MiddleToolBar != NULL && MiddleToolBar->HWindow != NULL)
        EnableWindow(MiddleToolBar->HWindow, !lock);
    if (BottomToolBar != NULL && BottomToolBar->HWindow != NULL)
        EnableWindow(BottomToolBar->HWindow, !lock);
    LeftPanel->LockUI(lock);
    RightPanel->LockUI(lock);
}

void CMainWindow::BringLockedUIToolWnd()
{
    if (LockedUIToolWnd != NULL)
        SetWindowPos(LockedUIToolWnd, HWindow, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOSENDCHANGING | SWP_NOREDRAW);
}

// ****************************************************************************

CFilesWindow*
CMainWindow::GetPanel(int panel)
{
    switch (panel)
    {
    case PANEL_SOURCE:
        return GetActivePanel();
    case PANEL_TARGET:
        return GetNonActivePanel();
    case PANEL_LEFT:
        return LeftPanel;
    case PANEL_RIGHT:
        return RightPanel;
    default:
        TRACE_E("Invalid panel (PANEL_XXX) constant: " << panel);
        return NULL;
    }
}
