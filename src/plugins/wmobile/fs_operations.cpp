// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "wmobile_delete_plan_core.h"
#include "wmobile_fileops_core.h"
#include "wmobile_path_core.h"

#include <vector>

//
// ****************************************************************************
// CPluginFSInterface
//

#define FILE_ATTRIBUTES_MASK (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM)

static BOOL WMobileCutDirectoryOwned(std::wstring& path, std::wstring* cutDirectory = NULL)
{
    return SPLCutDirectoryOwned(SalamanderGeneral, path, cutDirectory);
}

static BOOL WMobileRemovePointsFromPathOwned(std::wstring& path, size_t rootLength)
{
    if (rootLength > path.size())
        return FALSE;
    return SPLSalRemovePointsFromPathOwned(SalamanderGeneral, path,
                                           rootLength);
}

// Wide sibling. The cache key is composed wide by the copy loop; the narrow
// form below stays for the three call sites that still hold a narrow key.
static void WMobileRemoveFileFromCacheWide(const wchar_t* uniqueFileName)
{
    if (uniqueFileName == NULL)
        return;

    // Lowercased on a copy, as the narrow form did: the disk cache is case-sensitive and
    // device names are not, so the key is only ever stored folded.
    std::wstring key(uniqueFileName);
    if (!key.empty())
        SPLToLowerCaseOwned(SalamanderGeneral, key);
    SalamanderGeneral->RemoveOneFileFromCache(key.c_str());
}

CPluginFSInterface::CPluginFSInterface()
{
    Path.clear();
    PathError = FALSE;
    FatalError = FALSE;
}

void WINAPI
CPluginFSInterface::ReleaseObject(HWND parent)
{
    // if the FS is initialized, remove our copies of files in the disk cache when closing
    if (!Path.empty())
        EmptyCache();
}

BOOL WINAPI
CPluginFSInterface::GetRootPath(CSalamanderStringBuffer* userPart)
{
    return userPart != NULL &&
           sally::plugin_abi::WriteStringBuffer(*userPart, L"\\");
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
    std::wstring fullName = Path;
    if (isDir == 2)
    {
        if (!WMobileCutDirectoryOwned(fullName))
            return FALSE;
    }
    else
        wmobile::AppendDeviceComponent(fullName, file.Name);
    return fullNameBuffer != NULL &&
           sally::plugin_abi::WriteStringBuffer(*fullNameBuffer, fullName);
}

BOOL WINAPI
CPluginFSInterface::GetFullFSPath(HWND parent, const wchar_t* fsName,
                                  CSalamanderStringBuffer* path, BOOL& success)
{
    if (Path.empty())
        return FALSE; // translation is not possible, let Salamander report the error itself

    std::wstring pathPart;
    if (path == NULL || !sally::plugin_abi::ReadStringBuffer(*path, pathPart))
    {
        success = FALSE;
        return FALSE;
    }
    std::wstring root = L"\\"; // in Windows Mobile the root is always "\\"
    if (pathPart.empty() || pathPart.front() != L'\\')
        root = Path; // paths such as "path" inherit the current FS path
    wmobile::AppendDeviceComponent(root, pathPart.c_str());
    if (root.empty())
        root = L"\\";
    const std::wstring fullPath = std::wstring(fsName != NULL ? fsName : L"") + L":" + root;
    success = sally::plugin_abi::WriteStringBuffer(*path, fullPath);
    if (!success)
    {
        SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_PATHTOOLONG).c_str(),
                                         TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
    }
    return TRUE;
}

BOOL WINAPI
CPluginFSInterface::IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
{
    return userPart != NULL && SalamanderGeneral->IsTheSamePath(Path.c_str(), userPart);
}

BOOL WINAPI
CPluginFSInterface::IsOurPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
{
    return TRUE; //JR REVIEW: Who else would it belong to?
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
        return FALSE; // ListCurrentPath failed due to memory, fatal error
    }

    if (forceRefresh)
        EmptyCache();

    std::wstring errBuf;
    std::wstring path = userPart != NULL ? userPart : L"";
    int err = 0;

    BOOL fileNameAlreadyCut = FALSE;
    if (PathError) // error while listing the path (the user already saw the error in ListCurrentPath)
    {              // try to trim the path
        PathError = FALSE;
        if (!WMobileCutDirectoryOwned(path))
            return FALSE; // nowhere to shorten, fatal error
        fileNameAlreadyCut = TRUE;
        if (pathWasCut != NULL)
            *pathWasCut = TRUE;
    }
    while (1)
    {
        DWORD attr = CRAPI::GetFileAttributesWide(path.c_str(), TRUE);
        if (attr != 0xFFFFFFFF && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) // success, use the path as current
        {
            if (!errBuf.empty()) // if we have a message, print it here (it arose during trimming)
            {
                const std::wstring message = SPLFormatStringOwned(
                    LangStr(IDS_PATH_ERROR).c_str(), userPart != NULL ? userPart : L"", errBuf.c_str());
                SalamanderGeneral->ShowMessageBox(message.c_str(), TitleWMobileError, MSGBOX_ERROR);
            }
            Path = path;
            return TRUE;
        }
        else // failure, try to shorten the path
        {
            err = CRAPI::GetLastError();

            if (mode != 3 && attr != 0xFFFFFFFF || // a file instead of a path -> report as an error
                mode != 1 && attr == 0xFFFFFFFF)   // non-existent path -> report as an error
            {
                if (attr != 0xFFFFFFFF)
                {
                    errBuf = LangStr(IDS_ERR_FILEINPATH).c_str();
                }
                else
                    errBuf = SPLGetErrorTextOwned(SalamanderGeneral, err);

                // if opening the FS is time-consuming and we want to adjust Change Directory (Shift+F7)
                // to behave like archives, comment out the following line with "break" for mode 3

                //JR try trimming only if RAPI reports that the path does not exist
                if (mode == 3 || err != ERROR_FILE_NOT_FOUND)
                    break;
            }

            std::wstring cut;
            if (!WMobileCutDirectoryOwned(path, &cut)) // nowhere to shorten, fatal error
            {
                errBuf = SPLGetErrorTextOwned(SalamanderGeneral, err);
                break;
            }
            else
            {
                if (pathWasCut != NULL)
                    *pathWasCut = TRUE;
                if (!fileNameAlreadyCut) // it can be a file name only during the first trim
                {
                    fileNameAlreadyCut = TRUE;
                    if (cutFileName != NULL && attr != 0xFFFFFFFF &&
                        !sally::plugin_abi::WriteStringBuffer(*cutFileName, cut))
                        return FALSE; // it is a file
                }
                else
                {
                    if (cutFileName != NULL &&
                        !sally::plugin_abi::WriteStringBuffer(*cutFileName, std::wstring()))
                        return FALSE; // it can no longer be a file name
                }
            }
        }
    }

    const std::wstring message = SPLFormatStringOwned(
        LangStr(IDS_PATH_ERROR).c_str(), userPart != NULL ? userPart : L"", errBuf.c_str());
    SalamanderGeneral->ShowMessageBox(message.c_str(), TitleWMobileError, MSGBOX_ERROR);
    PathError = FALSE;
    return FALSE; // fatal path error
}

BOOL WINAPI
CPluginFSInterface::ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                    CPluginDataInterfaceAbstract*& pluginData,
                                    int& iconsType, BOOL forceRefresh)
{
    if (forceRefresh)
        EmptyCache();

    CFileData file;

    iconsType = pitFromRegistry;

    std::wstring curPath = Path;
    wmobile::AppendDeviceComponent(curPath, L"*");

    DWORD count;
    RapiNS::LPCE_FIND_DATA pFindDataArray;
    if (!CRAPI::FindAllFilesWide(curPath.c_str(), FAF_NAME | FAF_ATTRIBUTES | FAF_SIZE_LOW | FAF_SIZE_HIGH | FAF_LASTWRITE_TIME,
                             &count, &pFindDataArray, TRUE))
    {
        DWORD err = CRAPI::GetLastError();
        const std::wstring message = SPLFormatStringOwned(LangStr(IDS_PATH_ERROR).c_str(), Path.c_str(),
                                                          SPLGetErrorTextOwned(SalamanderGeneral, err).c_str());
        SalamanderGeneral->ShowMessageBox(message.c_str(), TitleWMobileError, MSGBOX_ERROR);
        PathError = TRUE;
        return FALSE;
    }

    DWORD i = 0;

    if (Path != L"\\") //JR If we are not at the root, add the ".." entry
    {
        file.Name = SalamanderGeneral->DupStr(L"..");
        if (file.Name == NULL)
            goto ONERROR;
        file.NameLen = 2;
        file.Ext = file.Name + file.NameLen;
        file.Size = CQuadWord(0, 0);
        file.Attr = FILE_ATTRIBUTE_DIRECTORY;
        file.LastWrite.dwLowDateTime = 0;
        file.LastWrite.dwHighDateTime = 0;
        file.Hidden = 0;
        file.DosName = NULL; // Windows Mobile has no 8.3 file names
        file.IsLink = 0;
        file.IsOffline = 0;

        if (!dir->AddDir(NULL, file, NULL))
            goto ONERROR;
    }

    int sortByExtDirsAsFiles;
    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &sortByExtDirsAsFiles,
                                          sizeof(sortByExtDirsAsFiles), NULL);

    for (; i < count; i++)
    {
        RapiNS::CE_FIND_DATA& data = pFindDataArray[i];

        if (data.cFileName[0] != 0 &&
            (data.cFileName[0] != L'.' || //JR Windows Mobile does not return "." or ".." paths, but handle it just in case
             (data.cFileName[1] != 0 && (data.cFileName[1] != L'.' || data.cFileName[2] != 0))))
        {
            file.Name = SalamanderGeneral->DupStr(data.cFileName);
            if (file.Name == NULL)
                goto ONERROR;
            file.NameLen = lstrlenW(file.Name);
            if (!sortByExtDirsAsFiles && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                file.Ext = file.Name + file.NameLen; // directories have no extensions
            }
            else
            {
                wchar_t* s;
                s = wcsrchr(file.Name, L'.');
                if (s != NULL)
                    file.Ext = s + 1; // ".cvspass" is treated as an extension in Windows
                else
                    file.Ext = file.Name + file.NameLen;
            }
            file.Size = CQuadWord(data.nFileSizeLow, data.nFileSizeHigh);
            file.Attr = data.dwFileAttributes;
            file.Attr &= ~(FILE_ATTRIBUTE_COMPRESSED);

            file.LastWrite = data.ftLastWriteTime;
            file.Hidden = (file.Attr & FILE_ATTRIBUTE_HIDDEN) != 0;
            file.DosName = NULL; // Windows Mobile has no 8.3 file names
            file.IsOffline = 0;
            if (file.Attr & FILE_ATTRIBUTE_DIRECTORY)
                file.IsLink = 0;
            else
                file.IsLink = SalamanderGeneral->IsFileLink(file.Ext);

            if ((file.Attr & FILE_ATTRIBUTE_DIRECTORY) == 0 && !dir->AddFile(NULL, file, NULL) ||
                (file.Attr & FILE_ATTRIBUTE_DIRECTORY) != 0 && !dir->AddDir(NULL, file, NULL))
            {
                goto ONERROR;
            }
        }
    }

    CRAPI::FreeBuffer(pFindDataArray);
    return TRUE;

ONERROR:
    TRACE_E("Low memory");
    if (file.Name != NULL)
        SalamanderGeneral->Free(file.Name);
    CRAPI::FreeBuffer(pFindDataArray);

    FatalError = TRUE;
    return FALSE;
}

BOOL WINAPI
CPluginFSInterface::TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
{
    //JR The Windows Mobile plugin always disconnects
    detach = FALSE;
    return TRUE;
}

void WINAPI
CPluginFSInterface::Event(int event, DWORD param)
{
    switch (event)
    {
    case FSE_ACTIVATEREFRESH: // user activated Salamander (switched from another application)
        if (!CRAPI::CheckConnection())
        {
            int panel1 = param;
            int panel2 = (panel1 == PANEL_LEFT ? PANEL_RIGHT : PANEL_LEFT);

            if (CRAPI::ReInit())
            {
                SalamanderGeneral->PostRefreshPanelPath(panel1);
                if (SalamanderGeneral->GetPanelPluginFS(panel2) != NULL)
                    SalamanderGeneral->PostRefreshPanelPath(panel2);
            }
            else if (SalamanderGeneral->ShowMessageBox(LangStr(IDS_YESNO_CONNETCLOSEPLUGIN).c_str(), TitleWMobileQuestion,
                                                       MSGBOX_QUESTION) == IDYES)
            {
                SalamanderGeneral->DisconnectFSFromPanel(SalamanderGeneral->GetMainWindowHWND(), panel1);
                if (SalamanderGeneral->GetPanelPluginFS(panel2) != NULL)
                    SalamanderGeneral->DisconnectFSFromPanel(SalamanderGeneral->GetMainWindowHWND(), panel2);
            }
        }
        break;
    case FSE_CLOSEORDETACHCANCELED:
    case FSE_OPENED:
    case FSE_ATTACHED:
    case FSE_DETACHED:
        // no operation
        break;
    }
}

DWORD WINAPI
CPluginFSInterface::GetSupportedServices()
{
    return 0 |
           FS_SERVICE_CONTEXTMENU |
           //JR TODO: add FS_SERVICE_SHOWPROPERTIES |
           FS_SERVICE_CHANGEATTRS |
           FS_SERVICE_COPYFROMDISKTOFS |
           FS_SERVICE_MOVEFROMDISKTOFS |
           FS_SERVICE_MOVEFROMFS |
           FS_SERVICE_COPYFROMFS |
           FS_SERVICE_DELETE |
           FS_SERVICE_VIEWFILE |
           //JR TODO: FS_SERVICE_EDITFILE (not yet supported in Salamander) |
           FS_SERVICE_CREATEDIR |
           FS_SERVICE_ACCEPTSCHANGENOTIF |
           FS_SERVICE_QUICKRENAME |
           //JR TODO: FS_SERVICE_CALCULATEOCCUPIEDSPACE (not yet supported in Salamander) |
           FS_SERVICE_COMMANDLINE |
           //JR TODO: add FS_SERVICE_SHOWINFO |
           FS_SERVICE_GETFREESPACE |
           FS_SERVICE_GETFSICON |
           FS_SERVICE_GETNEXTDIRLINEHOTPATH;
    //         FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM;
}

HICON WINAPI
CPluginFSInterface::GetFSIcon(BOOL& destroyIcon)
{
    destroyIcon = TRUE;
    // return LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_FS)); // colors glitch in Alt+F1/F2: As Other Panel
    return (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_FS),
                            IMAGE_ICON, 16, 16, SalamanderGeneral->GetIconLRFlags());
}

void WINAPI
CPluginFSInterface::GetFSFreeSpace(CQuadWord* retValue)
{
    retValue->LoDWord = -1;
    retValue->HiDWord = -1;

    RapiNS::STORE_INFORMATION si;
    if (!Path.empty() && CRAPI::GetStoreInformation(&si))
    {
        retValue->LoDWord = si.dwFreeSize;
        retValue->HiDWord = 0;
    }
}

BOOL WINAPI
CPluginFSInterface::GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset)
{
    const wchar_t* root = text; // pointer to the position after the root path

    while (*root != 0 && *root != ':')
        root++; //JR Skip 'FSNAME'
    if (*root == ':')
        root++; //JR Skip ':'
    if (*root == '\\')
        root++; //JR Skip '\\'

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
    std::wstring commandLine;
    if (command.empty() || command.front() != L'\\')
    {
        commandLine = Path;
        wmobile::AppendDeviceComponent(commandLine, command.c_str());
    }

    if ((!command.empty() && command.front() == L'\\') ||
        !CRAPI::CreateProcessWide(commandLine.c_str(), NULL))
    {
        // On failure, retry without the path, using only the user input
        if (!CRAPI::CreateProcessWide(command.c_str(), NULL))
        {
            DWORD err = CRAPI::GetLastError();
            SalamanderGeneral->SalMessageBox(parent, SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(),
                                             TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);

            return FALSE;
        }
    }

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
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // request for the standard dialog

    // Verify the provided name syntactically
    const wchar_t* s = newName.c_str();
    while (*s != 0 && *s != L'\\' && *s != L'/' && *s != L':' &&
           *s >= 32 && *s != L'<' && *s != L'>' && *s != L'|' && *s != L'"')
        s++;
    if (newName.empty() || *s != 0)
    {
        SalamanderGeneral->SalMessageBox(parent, SPLGetErrorTextOwned(SalamanderGeneral, ERROR_INVALID_NAME).c_str(),
                                         TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
        return FALSE; // invalid name; let the user correct it
    }

    // process the mask in newName
    std::wstring maskedNameW;
    if (!SPLMaskNameOwned(SalamanderGeneral, file.Name, newName.c_str(), maskedNameW))
        return FALSE;
    newName = maskedNameW;

    // perform the rename operation
    // Wide. file.Name has been wide since P1.3, and narrowing it here - with
    // refuse-on-loss - meant renaming a file whose name is outside the machine's code page
    // returned FALSE before the device was ever asked. The device path is composed wide now,
    // through the PathAppend covered by gtest_wmobile_path_core.
    std::wstring nameFrom = Path;
    std::wstring nameTo = Path;
    wmobile::AppendDeviceComponent(nameFrom, file.Name);
    wmobile::AppendDeviceComponent(nameTo, maskedNameW.c_str());

    //JR TODO: ConfirmOverwrite + Delete

    if (!CRAPI::MoveFileWide(nameFrom.c_str(), nameTo.c_str()))
    {
        // potential overwrites are not handled here; treat them as errors as well
        DWORD err = CRAPI::GetLastError();
        SalamanderGeneral->SalMessageBox(parent, SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(),
                                         TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
        // 'newName' is already returned after the mask adjustment
        return FALSE; // error -> show the standard dialog again
    }
    else // operation succeeded - report the change on the path (triggers refresh) and return success
    {
        if (SalamanderGeneral->StrICmp(nameFrom.c_str(), nameTo.c_str()) != 0)
        { // if it is more than just a case change (CEFS is case-insensitive)
            // remove the source file from the disk cache (the original name is no longer valid)
            // The disk-cache key stays narrow - that cache is byte-keyed - so the device path
            // bridges here, at the cache boundary rather than before the device ever sees it.
            std::wstring cefsFileNameW = std::wstring(fsName) + L":" + nameFrom;
            // disk names are case-insensitive while the disk cache is case-sensitive; converting
            // to lowercase makes the disk cache behave case-insensitively as well
            SPLToLowerCaseOwned(SalamanderGeneral, cefsFileNameW);
            SalamanderGeneral->RemoveOneFileFromCache(cefsFileNameW.c_str());
            // if overwriting is possible, the target should be removed from the disk cache as well ("file changed")
        }

        // change notification on Path (without subdirectories when renaming files)
        const std::wstring changedPath = std::wstring(fsName) + L":" + Path;
        SalamanderGeneral->PostChangeOnPathNotification(changedPath.c_str(), isDir);

        return TRUE;
    }
}

void WINAPI
CPluginFSInterface::AcceptChangeOnPathNotification(const wchar_t* fsName, const wchar_t* path, BOOL includingSubdirs)
{
    // test whether the paths or at least their prefixes match (only paths on our FS have a chance;
    // disk paths and other FS paths in 'path' are excluded automatically because they can never
    // match 'fsName'+':' at the start of 'path2' below)
    std::wstring path1W = path;
    std::wstring path2W = std::wstring(fsName) + L":" + Path;
    SPLSalPathRemoveBackslashOwned(SalamanderGeneral, path1W);
    SPLSalPathRemoveBackslashOwned(SalamanderGeneral, path2W);
    int len1 = lstrlenW(path1W.c_str());
    BOOL refresh = SalamanderGeneral->StrNICmp(path1W.c_str(), path2W.c_str(), len1) == 0 &&
                   (path2W[len1] == 0 || includingSubdirs && path2W[len1] == L'\\');
    if (refresh)
        SalamanderGeneral->PostRefreshPanelFS(this); // refresh the panel if the FS is visible there
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
    // One wide function. The narrow twin this used to delegate to is gone, and
    // with it the refusal at the top: the old entry narrowed fsName and newName with
    // refuse-on-loss, so creating a directory under a path containing a non-ANSI component
    // failed before anything was attempted. Everything here was already available wide - the
    // SDK's CreateDir is wide, and the *Narrow helpers were only dressing down wide SDK calls.
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // request for the standard dialog

    int type;
    BOOL isDir;
    size_t secondPartOffset = std::wstring::npos;
    std::wstring parsedName(newName);
    std::wstring nextFocus;
    std::wstring path;
    int error;
    if (!SPLSalParsePathOwned(SalamanderGeneral, parent, parsedName, type, isDir,
                              secondPartOffset, TitleWMobileError, FALSE, NULL,
                              &error, &nextFocus))
    {
        if (error == SPP_EMPTYPATHNOTALLOWED) // empty string -> stop without performing the operation
        {
            cancel = TRUE;
            return TRUE; // return value no longer matters
        }

        if (error == SPP_INCOMLETEPATH) // relative FS path; build the absolute path manually
        {
            const size_t slash = newName.find(L'\\');
            if (slash == std::wstring::npos || slash + 1 == newName.size())
                nextFocus = slash == std::wstring::npos ? newName : newName.substr(0, slash);
            int errTextID;
            parsedName = newName;
            if (!SPLSalGetFullNameOwned(SalamanderGeneral, parsedName, &errTextID,
                                        Path.c_str()))
            {
                std::wstring errorText;
                SPLGetGFNErrorTextOwned(SalamanderGeneral, errTextID, errorText);
                SalamanderGeneral->ShowMessageBox(errorText.c_str(), TitleWMobileError, MSGBOX_ERROR);
                return FALSE;
            }
            secondPartOffset = wcslen(fsName) + 1;
            type = PATH_TYPE_FS;
        }
        else
            return FALSE; // error -> show the standard dialog again
    }

    if (secondPartOffset > parsedName.size())
        return FALSE;
    newName = parsedName;
    std::wstring secondPart = parsedName.substr(secondPartOffset);

    if (type != PATH_TYPE_FS)
    {
        SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_SORRY_CREATEDIR1).c_str(),
                                         TitleWMobile, MB_OK | MB_ICONEXCLAMATION);
        return FALSE; // error -> show the standard dialog again
    }

    if (secondPartOffset == 0 || secondPartOffset - 1 != wcslen(fsName) ||
        SalamanderGeneral->StrNICmp(parsedName.c_str(), fsName,
                                    static_cast<int>(secondPartOffset - 1)) != 0)
    { // not a CEFS path
        SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_SORRY_CREATEDIR2).c_str(),
                                         TitleWMobile, MB_OK | MB_ICONEXCLAMATION);
        return FALSE; // error -> show the standard dialog again
    }

    if (secondPart.empty() || secondPart.front() != L'\\')
    {
        SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_SORRY_CREATEDIR3).c_str(),
                                         TitleWMobile, MB_OK | MB_ICONEXCLAMATION);
        return FALSE; // error -> show the standard dialog again
    }

    // remove any "." and ".." segments from the full path to this FS
    if (!WMobileRemovePointsFromPathOwned(secondPart, 1))
    {
        SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_INVALIDPATH).c_str(),
                                         TitleWMobile, MB_OK | MB_ICONEXCLAMATION);
        return FALSE; // error -> show the standard dialog again
    }

    // trim any redundant trailing backslash
    while (secondPart.size() > 1 && secondPart.back() == L'\\')
        secondPart.pop_back();

    // finally create the directory
    if (!CRAPI::CreateDirectoryWide(secondPart.c_str(), NULL))
    {
        DWORD err = CRAPI::GetLastError();
        SalamanderGeneral->SalMessageBox(parent, SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(),
                                         TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
        return FALSE; // error -> show the standard dialog again
    }

    // operation succeeded - report the change on the path (triggers refresh)
    WMobileCutDirectoryOwned(secondPart); // must succeed (cannot be the root)
    path = std::wstring(fsName) + L":" + secondPart;
    SalamanderGeneral->PostChangeOnPathNotification(path.c_str(), FALSE);
    newName = nextFocus;

    return TRUE;
}

void WINAPI
CPluginFSInterface::ViewFile(const wchar_t* fsName, HWND parent,
                             CSalamanderForViewFileOnFSAbstract* salamander,
                             CFileData& file)
{
    if (!CRAPI::CheckConnection())
        CRAPI::ReInit();

    // What stood here narrowed file.Name and RETURNED on failure, so viewing a
    // file whose name is outside the system code page did nothing at all - no viewer, no error,
    // no trace. The name is used wide throughout now, and the only conversion left is of Path,
    // which is the panel path and belongs to a separate campaign.
    std::wstring remoteFileName = Path;
    wmobile::AppendDeviceComponent(remoteFileName, file.Name);

    // build a unique file name for the disk cache (standard Salamander path format)
    const std::wstring& assignedFSNameW = AssignedFSName;
    std::wstring uniqueFileName = assignedFSNameW + L":" + remoteFileName;
    // disk names are case-insensitive while the disk cache is case-sensitive; converting
    // to lowercase makes the disk cache behave case-insensitively as well
    SPLToLowerCaseOwned(SalamanderGeneral, uniqueFileName);

    // obtain the disk-cache copy name
    BOOL fileExists;
    const wchar_t* tmpFileName = salamander->AllocFileNameInCache(parent, uniqueFileName.c_str(), file.Name, NULL, fileExists);
    if (tmpFileName == NULL)
        return; // fatal error


    // determine whether a disk-cache copy of the file needs to be prepared (download)
    BOOL newFileOK = FALSE;
    CQuadWord newFileSize(0, 0);
    if (!fileExists) // preparing a copy (download) is necessary
    {
        const wchar_t* name = remoteFileName.c_str();

        HWND mainWnd = parent;
        HWND parentWin;
        while ((parentWin = GetParent(mainWnd)) != NULL && IsWindowEnabled(parentWin))
            mainWnd = parentWin;
        // disable 'mainWnd'

        CProgressDlg dlg(mainWnd, LangStr(IDS_READ).c_str(), LangStr(IDS_READING).c_str(), ooStatic); // use 'ooStatic' so the modeless dialog can live on the stack

        dlg.Create();
        EnableWindow(mainWnd, FALSE);
        SetForegroundWindow(dlg.HWindow);

        dlg.Set(name, 0, TRUE);

        const wchar_t* errFileName = L"";
        DWORD err = CRAPI::CopyFileToPC(name, tmpFileName, TRUE, &dlg, 0, 0, &errFileName);

        EnableWindow(mainWnd, TRUE);
        DestroyWindow(dlg.HWindow); // close the progress dialog

        if (err == 0) // the copy succeeded
        {
            newFileOK = TRUE; // if the size query fails, newFileSize stays zero (not critical)
            HANDLE hFile = HANDLES_Q(CreateFileW(tmpFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                NULL, OPEN_EXISTING, 0, NULL));
            if (hFile != INVALID_HANDLE_VALUE)
            { // ignore errors; the exact file size is not essential
                DWORD err2;
                SalamanderGeneral->SalGetFileSize(hFile, newFileSize, err2); // ignore errors
                HANDLES(CloseHandle(hFile));
            }
        }
        else if (err != -1)
        {
            const std::wstring message = SPLFormatStringOwned(LangStr(IDS_PATH_ERROR).c_str(), errFileName,
                                                              SPLGetErrorTextOwned(SalamanderGeneral, err).c_str());
            SalamanderGeneral->ShowMessageBox(message.c_str(), TitleWMobileError, MSGBOX_ERROR);
        }
    }

    // open the viewer
    HANDLE fileLock;
    BOOL fileLockOwner;
    if (!fileExists && !newFileOK || // open the viewer only if the file copy is ready
        !salamander->OpenViewer(parent, tmpFileName, &fileLock, &fileLockOwner))
    { // on error, reset "lock"
        fileLock = NULL;
        fileLockOwner = FALSE;
    }

    // call FreeFileNameInCache to pair with AllocFileNameInCache (link the viewer and the disk cache)
    salamander->FreeFileNameInCache(uniqueFileName.c_str(), fileExists, newFileOK,
                                    newFileSize, fileLock, fileLockOwner, FALSE);
}


// CRAPI behind wmobile_fileops_core's interface, so the destructive step can be
// driven by a fake device in gtest_wmobile_fileops_core. Nothing here decides anything - the
// decisions are in the core; this just forwards.
//
// The paths stay narrow at this seam because CRAPI's delete/attribute signatures still are; the
// core is wide, so this is the one conversion left on the destructive path and it is now in a
// single place instead of two open-coded call sites.
class CRapiFileOps : public wmobile::DeviceFileOps
{
public:
    // Wide end to end. The narrowing that stood here is gone: these call CRAPI's
    // wide siblings, which hand the UTF-16 path straight to CE. A device file whose name is
    // outside the machine's code page used to be REFUSED here - visible in the panel but
    // impossible to delete. It is deletable now.
    bool SetAttributesOnDevice(const wchar_t* path, unsigned long attributes) override
    {
        return CRAPI::SetFileAttributesWide(path, (DWORD)attributes) != FALSE;
    }

    bool DeleteFileOnDevice(const wchar_t* path) override
    {
        return CRAPI::DeleteFileWide(path) != FALSE;
    }

    bool RemoveDirectoryOnDevice(const wchar_t* path) override
    {
        return CRAPI::RemoveDirectoryWide(path) != FALSE;
    }
};
BOOL WINAPI
CPluginFSInterface::Delete(const wchar_t* fsName, int mode, HWND parent, int panel,
                           int selectedFiles, int selectedDirs, BOOL& cancelOrError)
{
    cancelOrError = FALSE;
    if (mode == 1)
        return FALSE; // request for the standard prompt

    const std::wstring rootPath = Path;
    std::wstring fileName;

    // retrieve the "Confirm on" settings from the configuration
    BOOL ConfirmOnNotEmptyDirDelete, ConfirmOnSystemHiddenFileDelete, ConfirmOnSystemHiddenDirDelete;
    SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMNEDIRDEL, &ConfirmOnNotEmptyDirDelete, 4, NULL);
    SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHFILEDEL, &ConfirmOnSystemHiddenFileDelete, 4, NULL);
    SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHDIRDEL, &ConfirmOnSystemHiddenDirDelete, 4, NULL);

    BOOL skipAllSHFD = FALSE;   // skip all deletes of system or hidden files
    BOOL yesAllSHFD = FALSE;    // delete all system or hidden files
    BOOL skipAllSHDD = FALSE;   // skip all deletes of system or hidden dirs
    BOOL yesAllSHDD = FALSE;    // delete all system or hidden dirs
    BOOL skipAllErrors = FALSE; // skip all errors

    BOOL success = TRUE; // becomes FALSE on error or user interruption
    BOOL changeInSubdirs = FALSE;

    const CFileData* f = NULL; // pointer to the file/directory in the panel to process
    BOOL isDir = FALSE;        // TRUE if 'f' is a directory
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;

    SalamanderGeneral->CreateSafeWaitWindow(LangStr(IDS_WAIT_READINGDIRTREE).c_str(), TitleWMobile,
                                            500, FALSE, SalamanderGeneral->GetMainWindowHWND());
    CFileInfoArray array(10, 10);

    //JR load all files that will be deleted
    for (int block1 = 1;; block1++)
    {
        // retrieve data about the file being processed
        if (focused)
            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        else
            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

        // delete the file/directory
        //JR call FindAllFilesInTree even for individual files
        //JR this verifies the file still exists and gets its current attributes
        if (f != NULL)
        {
            success = CRAPI::FindAllFilesInTreeWide(rootPath.c_str(), f->Name, array, block1, FALSE);
        }

        // decide whether to continue (stop if there is an error or no additional selected item)
        if (!success || focused || f == NULL)
            break;
    }

    SalamanderGeneral->DestroySafeWaitWindow();

    if (!success)
        return FALSE;

    HWND mainWnd = parent;
    HWND parentWin;
    while ((parentWin = GetParent(mainWnd)) != NULL && IsWindowEnabled(parentWin))
        mainWnd = parentWin;
    // disable 'mainWnd'

    BOOL showProgressDialog = array.Count > 1;
    BOOL enableMainWnd = TRUE;
    CProgressDlg delDlg(mainWnd, LangStr(IDS_DELETE).c_str(), LangStr(IDS_DELETING).c_str(), ooStatic); // use 'ooStatic' so the modeless dialog can live on the stack

    if (showProgressDialog)
    {
        EnableWindow(mainWnd, FALSE);
        delDlg.Create();
    }

    if (!showProgressDialog || delDlg.HWindow != NULL) // dialog opened successfully
    {
        if (showProgressDialog)
            SetForegroundWindow(delDlg.HWindow);

        int block = 0;
        int i;
        for (i = 0; success && i < array.Count; i++)
        {
            CFileInfo& fi = array[i];

            // The file-list record owns the device's UTF-16 relative path; compose it directly.
            fileName = rootPath;
            wmobile::AppendDeviceComponent(fileName, fi.cFileName);

            if (showProgressDialog)
            {
                float progress = ((float)i / (float)array.Count);
                delDlg.Set(fileName.c_str(), (DWORD)(progress * 1000), TRUE); // delayedPaint == TRUE so we don't slow things down
            }

            if (showProgressDialog && delDlg.GetWantCancel())
            {
                success = FALSE;
                break;
            }

            if (ConfirmOnNotEmptyDirDelete && i < array.Count - 1)
            {
                //JR if a block contains more than one item, it represents a non-empty top-level directory
                // The two index decisions here moved to wmobile_delete_plan_core so
                // they can be tested - see gtest_wmobile_delete_plan_core. Both are consequential:
                // miss the first and a directory full of files is deleted with no 'not empty'
                // prompt at all; miscount the second and declining that prompt still deletes part
                // of what the user just declined.
                std::vector<wmobile::DeleteCandidate> candidates;
                candidates.reserve((size_t)array.Count);
                for (int c = 0; c < array.Count; c++)
                {
                    wmobile::DeleteCandidate cand;
                    cand.block = array[c].block;
                    cand.isDirectory = (array[c].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    candidates.push_back(cand);
                }

                if (wmobile::StartsNonEmptyDirectoryBlock(candidates.data(), candidates.size(),
                                                          (size_t)i, block))
                {
                    //JR the last path in the block is the directory itself
                    const int j = (int)wmobile::LastIndexOfBlock(candidates.data(),
                                                                 candidates.size(), (size_t)i);

                    // array[j].cFileName is wide now; this confirmation dialog's
                    // format string stays narrow (a separate, deferred campaign). Bridge here,
                    // refuse-on-loss - display '?' rather than a %s call silently misreading a
                    // wide pointer as narrow bytes.
                    // The prompt names the directory straight from the wide
                    // entry. The narrowing here could fail, and its fallback was an EMPTY
                    // string - so a directory whose name is outside the code page produced a
                    // confirmation prompt naming no directory at all.
                    const std::wstring message = SPLFormatStringOwned(LangStr(IDS_YESNO_DELETENOEMPTYDIR).c_str(), array[j].cFileName);
                    int res = SalamanderGeneral->ShowMessageBox(message.c_str(), TitleWMobileQuestion, MSGBOX_EX_QUESTION);
                    if (res == IDNO)
                    {
                        i = j; //JR skip the rest of the block
                        continue;
                    }
                    else if (res != IDYES)
                    {
                        success = FALSE;
                        break;
                    }
                }
            }

            block = fi.block;

            if (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                BOOL skip = FALSE;
                if (ConfirmOnSystemHiddenDirDelete &&
                    (fi.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
                {
                    if (!skipAllSHDD && !yesAllSHDD)
                    {
                        int res = SalamanderGeneral->DialogQuestion(parent, BUTTONS_YESALLSKIPCANCEL, fileName.c_str(),
                                                                    LangStr(IDS_YESNO_DELETEHIDDENDIR).c_str(), TitleWMobileQuestion);
                        switch (res)
                        {
                        case DIALOG_ALL:
                            yesAllSHDD = TRUE;
                        case DIALOG_YES:
                            break;

                        case DIALOG_SKIPALL:
                            skipAllSHDD = TRUE;
                        case DIALOG_SKIP:
                            skip = TRUE;
                            break;

                        default:
                            success = FALSE;
                            break; // DIALOG_CANCEL
                        }
                    }
                    else // skip all or delete all
                    {
                        if (skipAllSHDD)
                            skip = TRUE;
                    }
                }

                if (success && !skip) // neither canceled nor skipped
                {
                    skip = FALSE;
                    while (1)
                    {
                        // Clear-then-delete now lives in wmobile_fileops_core,
                        // where it is tested. Doing it in the other order fails on anything
                        // read-only and reports it as a device error.
                        CRapiFileOps deviceOps;
                        if (!wmobile::DeleteOneItem(deviceOps, fileName.c_str(),
                                                    fi.dwFileAttributes, true))
                        {
                            if (!skipAllErrors)
                            {
                                DWORD err = CRAPI::GetLastError();
                                int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, fileName.c_str(),
                                                                         SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                            const std::wstring cacheName = std::wstring(fsName) + L":" + fileName;
                            WMobileRemoveFileFromCacheWide(cacheName.c_str());
                            changeInSubdirs = TRUE; // changes may also have occurred in subdirectories
                            break;                  // successful delete
                        }
                        if (!success || skip)
                            break;
                    }
                }
            }
            else
            {
                BOOL skip = FALSE;
                if (ConfirmOnSystemHiddenFileDelete &&
                    (fi.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
                {
                    if (!skipAllSHFD && !yesAllSHFD)
                    {
                        int res = SalamanderGeneral->DialogQuestion(parent, BUTTONS_YESALLSKIPCANCEL, fileName.c_str(),
                                                                    LangStr(IDS_YESNO_DELETEHIDDENFILE).c_str(), TitleWMobileQuestion);
                        switch (res)
                        {
                        case DIALOG_ALL:
                            yesAllSHFD = TRUE;
                        case DIALOG_YES:
                            break;

                        case DIALOG_SKIPALL:
                            skipAllSHFD = TRUE;
                        case DIALOG_SKIP:
                            skip = TRUE;
                            break;

                        default:
                            success = FALSE;
                            break; // DIALOG_CANCEL
                        }
                    }
                    else // skip all or delete all
                    {
                        if (skipAllSHFD)
                            skip = TRUE;
                    }
                }

                if (success && !skip) // neither canceled nor skipped
                {
                    skip = FALSE;
                    while (1)
                    {
                        // Same tested step as the directory branch above - the
                        // two sites had the attribute triple written out separately, so a
                        // divergence between them would have been invisible.
                        CRapiFileOps deviceOps;
                        if (!wmobile::DeleteOneItem(deviceOps, fileName.c_str(),
                                                    fi.dwFileAttributes, false))
                        {
                            if (!skipAllErrors)
                            {
                                DWORD err = CRAPI::GetLastError();
                                int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, fileName.c_str(),
                                                                         SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                            const std::wstring cacheName = std::wstring(fsName) + L":" + fileName;
                            WMobileRemoveFileFromCacheWide(cacheName.c_str());
                            break; // successful delete
                        }
                        if (!success || skip)
                            break;
                    }
                }
            }
        }

        if (showProgressDialog)
        {
            // enable 'mainWnd' (otherwise Windows cannot make it the foreground/active window)
            EnableWindow(mainWnd, TRUE);
            enableMainWnd = FALSE;

            DestroyWindow(delDlg.HWindow); // close the progress dialog
        }
    }

    // enable 'mainWnd' if the foreground window never changed (the progress dialog never opened)
    if (showProgressDialog && enableMainWnd)
        EnableWindow(mainWnd, TRUE);

    SalamanderGeneral->RestoreFocusInSourcePanel();

    // report the change on Path (no subdirectories when only files were deleted)
    const std::wstring changedPath = std::wstring(fsName) + L":" + Path;
    SalamanderGeneral->PostChangeOnPathNotification(changedPath.c_str(), changeInSubdirs);

    return success;
}

// Case-insensitive device-path comparison; one trailing backslash on either side is ignored.
static BOOL WINAPI WMobileIsTheSamePathWide(const wchar_t* path1, const wchar_t* path2)
{
    if (path1 == NULL || path2 == NULL)
        return FALSE;

    std::wstring a(path1), b(path2);
    if (!a.empty() && a.back() == L'\\')
        a.pop_back();
    if (!b.empty() && b.back() == L'\\')
        b.pop_back();

    return SalamanderGeneral->StrICmp(a.c_str(), b.c_str()) == 0;
}

// The copy loop's name for the same predicate - it compares two composed device paths.
static BOOL CEFS_IsTheSamePathWide(const wchar_t* path1, const wchar_t* path2)
{
    return WMobileIsTheSamePathWide(path1, path2);
}

static BOOL WMobileSplitGeneralPath(HWND parent, const wchar_t* title,
                                    const wchar_t* errorTitle, int selCount,
                                    std::wstring& path, size_t afterRootOffset,
                                    size_t secondPartOffset, BOOL pathIsDir,
                                    BOOL backslashAtEnd, const wchar_t* dirName,
                                    const wchar_t* curPath, std::wstring& mask,
                                    std::wstring* newDirs, BOOL useSamePathCallback)
{
    return SPLSalSplitGeneralPathOwned(
        SalamanderGeneral, parent, title, errorTitle, selCount, path,
        afterRootOffset, secondPartOffset, pathIsDir, backslashAtEnd,
        dirName, curPath, mask, newDirs,
        useSamePathCallback ? WMobileIsTheSamePathWide : NULL);
}

static void WMobileRemoveFilesFromCacheWide(const wchar_t* uniqueFileName)
{
    if (uniqueFileName != NULL)
        SalamanderGeneral->RemoveFilesFromCache(uniqueFileName);
}

static std::wstring GetFileDataWide(const wchar_t* name)
{
    WIN32_FIND_DATAW data;

    HANDLE find = FindFirstFileW(name, &data);
    if (find == INVALID_HANDLE_VALUE)
        return L"?";

    CQuadWord size(data.nFileSizeLow, data.nFileSizeHigh);
    std::wstring result = SPLNumberToStrOwned(SalamanderGeneral, size);

    FILETIME time;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&data.ftLastWriteTime, &time);
    FileTimeToSystemTime(&time, &st);

    int dateLength = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, NULL, 0);
    std::wstring date;
    if (dateLength > 0)
    {
        std::vector<wchar_t> value(static_cast<size_t>(dateLength));
        if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, value.data(), dateLength))
            date.assign(value.data());
    }
    if (date.empty())
        date = SPLFormatStringOwned(L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);

    int timeLength = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, NULL, 0);
    std::wstring timeText;
    if (timeLength > 0)
    {
        std::vector<wchar_t> value(static_cast<size_t>(timeLength));
        if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, value.data(), timeLength))
            timeText.assign(value.data());
    }
    if (timeText.empty())
        timeText = SPLFormatStringOwned(L"%u:%u:%u", st.wHour, st.wMinute, st.wSecond);

    result += L", " + date + L", " + timeText;

    FindClose(find);
    return result;
}

BOOL WINAPI
CPluginFSInterface::CopyOrMoveFromFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                     int panel, int selectedFiles, int selectedDirs,
                                     CSalamanderStringBuffer* targetPath, BOOL& operationMask,
                                     BOOL& cancelOrHandlePath, HWND dropTarget)
{
    std::wstring payload;
    wmobile::OperationTarget callbackTarget;
    if (targetPath == NULL ||
        !sally::plugin_abi::ReadStringBuffer(*targetPath, payload) ||
        !wmobile::DecodeOperationTarget(payload,
                                        mode == 3 || mode == 5, callbackTarget))
    {
        cancelOrHandlePath = TRUE;
        return TRUE;
    }

    const BOOL result = CopyOrMoveFromFSOwned(copy, mode, fsName, parent, panel,
                                               selectedFiles, selectedDirs, callbackTarget.path,
                                               callbackTarget.mask, operationMask,
                                               cancelOrHandlePath, dropTarget);
    if (!sally::plugin_abi::WriteStringBuffer(*targetPath, callbackTarget.path))
    {
        cancelOrHandlePath = TRUE;
        return TRUE;
    }
    return result;
}

BOOL WINAPI
CPluginFSInterface::CopyOrMoveFromFSOwned(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                          int panel, int selectedFiles, int selectedDirs,
                                          std::wstring& targetPath, const std::wstring& suppliedMask,
                                          BOOL& operationMask, BOOL& cancelOrHandlePath,
                                          HWND dropTarget)
{
    std::wstring path;
    operationMask = FALSE;
    cancelOrHandlePath = FALSE;
    if (mode == 1) // first call to CopyOrMoveFromFS
    {
        if (targetPath.empty())
        {
            int targetPanel = (panel == PANEL_LEFT ? PANEL_RIGHT : PANEL_LEFT);
            int type;
            size_t fsOffset = std::wstring::npos;
            if (SPLGetPanelPathOwned(SalamanderGeneral, targetPanel, path, &type, &fsOffset))
            {
                const std::wstring panelFSName = fsOffset != std::wstring::npos
                                                     ? path.substr(0, fsOffset)
                                                     : std::wstring();
                if (type == PATH_TYPE_FS && panelFSName == fsName)
                    targetPath = path;
            }
        }
        // If a path was suggested, append the *.* mask (we will process operation masks)
        if (!targetPath.empty())
        {
            SPLSalPathAppendOwned(targetPath, L"*.*");
            SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_TARGET); // default action = work with the path in the target panel
        }
        return FALSE; // request for the standard dialog
    }

    if (mode == 4) // error during the standard Salamander processing of the target path
    {
        // 'targetPath' contains an invalid path, the user has already been informed, so we just
        // let them edit the destination path again
        return FALSE; // request for the standard dialog
    }

    std::wstring nextFocus;
    std::wstring operationMaskValue;

    BOOL diskPath = TRUE;  // when 'mode'==3 'targetPath' holds a Windows path (FALSE = path on this FS)
    size_t userPartOffset = std::wstring::npos;
    BOOL rename = FALSE;   // TRUE means renaming/copying a directory into itself

    if (mode == 2) // a string arrived from the standard dialog entered by the user
    {
        // Handle relative paths ourselves (Salamander cannot do that)
        const BOOL uncPath = targetPath.size() >= 2 && targetPath[0] == L'\\' && targetPath[1] == L'\\';
        const BOOL drivePath = targetPath.size() >= 2 && targetPath[1] == L':';
        if (!uncPath && !drivePath)
        {                                                       // neither a Windows path, nor an archive path
            const size_t colon = targetPath.find(L':');
            if (colon == std::wstring::npos) // path does not contain an FS name, so it is relative
            {                     // a relative path with ':' is not allowed here (cannot be distinguished from an absolute path to some FS)

                // For disk paths we could use SalGetFullName:
                // SalamanderGeneral->SalGetFullName(targetPath, &errTextID, Path, nextFocus) + handle the errors
                // After that it would be enough to prepend the FS name to the obtained path
                // but instead we demonstrate our own implementation (using SalRemovePointsFromPath and others):

                std::wstring resolvedTarget;
                if (!wmobile::ResolveRelativeDeviceTarget(fsName, Path, targetPath,
                                                           resolvedTarget, userPartOffset,
                                                           nextFocus))
                    return FALSE;
                targetPath.swap(resolvedTarget);
            }
            else
                userPartOffset = colon + 1;

            // FS destination path ('targetPath' - full path, 'userPart' - pointer inside the full path to the user part)
            // At this point the plugin can handle FS paths (both its own and foreign ones)
            // Salamander cannot process these paths yet; in the future it might support a basic
            // sequence of operations via TEMP (for example download from FTP to TEMP, then upload
            // from TEMP back to FTP - if it can be done more efficiently, as with FTP, the plugin should handle it here)

            const std::wstring targetFSName = targetPath.substr(0, userPartOffset - 1);
            if (SalamanderGeneral->StrICmp(targetFSName.c_str(), fsName) == 0)
            { // it is CEFS (otherwise let Salamander process it normally)
                BOOL invPath = userPartOffset >= targetPath.size() || targetPath[userPartOffset] != L'\\';

                size_t rootLen = 0;
                if (!invPath)
                    rootLen = 1;

                // The full path to this FS may also contain "." and ".." entered by the user - remove them
                if (invPath)
                {
                    // Additionally we could display 'err' (when 'invPath' is TRUE); ignored here for simplicity
                    SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_INVALIDPATH).c_str(),
                                                     TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
                    // 'targetPath' is returned after the modification (path expansion) + possible adjustment of some ".." and "."
                    return FALSE; // error -> reopen the standard dialog
                }
                if (!WMobileRemovePointsFromPathOwned(targetPath, userPartOffset + rootLen))
                {
                    SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_INVALIDPATH).c_str(),
                                                     TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
                    return FALSE;
                }

                // Trim the unnecessary backslash
                const size_t userPartLength = targetPath.size() - userPartOffset;
                BOOL backslashAtEnd = userPartLength > 0 && targetPath.back() == L'\\';
                if (userPartLength > 1 && targetPath.back() == L'\\')
                    targetPath.pop_back();

                // Analyse the path - locate the existing part, the missing part, and the operation mask
                //
                // - Determine which portion exists and whether it is a file or a directory,
                //   then choose what action applies:
                //   - write to the path (possibly with a missing portion) with a mask - the mask is the last missing part
                //     after which there is no backslash (verify that when there are multiple source files/directories the mask
                //     contains '*' or at least '?'; otherwise it is nonsense -> there would be only one target name)
                //   - manual "change-case" of a subdirectory name via Move (writing to a path that is simultaneously the
                //     source of the operation because it is focused/selected as the only item in the panel); the names can
                //     differ only by letter case)
                //   - writing into an archive (the path contains an archive file, or it might not even be an archive, resulting
                //     in the "Salamander does not know how to open this file" error)
                //   - overwriting a file (the entire path is just the target file name; must not end with a backslash)

                // Determine how far the path exists (split into existing and missing parts)
                HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
                size_t end = targetPath.size();
                const size_t afterRoot = userPartOffset + 1; // JR root = '\\'
                BOOL pathIsDir = TRUE;
                BOOL pathError = FALSE;

                // If the path contains a mask, cut it off without calling GetFileAttributes
                if (end > afterRoot) // still more than just the root
                {
                    const size_t slash = targetPath.rfind(L'\\', end - 1);
                    if (slash != std::wstring::npos &&
                        targetPath.find_first_of(L"*?", slash + 1) != std::wstring::npos)
                        end = slash;
                }

                while (end > afterRoot) // still more than just the root
                {
                    const std::wstring existingPath = targetPath.substr(userPartOffset, end - userPartOffset);
                    DWORD attrs = CRAPI::GetFileAttributesWide(existingPath.c_str());
                    if (attrs != 0xFFFFFFFF) // this portion of the path exists
                    {
                        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) // it is a file
                        {
                            // An existing path must not include a file name (see SalSplitGeneralPath); trim it...
                            pathIsDir = FALSE; // the existing part of the path is a file
                            end = targetPath.rfind(L'\\', end - 1);
                            if (end == std::wstring::npos)
                                pathError = TRUE;
                            break;
                        }
                        else
                            break;
                    }
                    else
                    {
                        DWORD err = CRAPI::GetLastError();
                        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_INVALID_NAME &&
                            err != ERROR_PATH_NOT_FOUND && err != ERROR_BAD_PATHNAME &&
                            err != ERROR_DIRECTORY) // unexpected error - just report it
                        {
                            const std::wstring message = SPLFormatStringOwned(
                                LangStr(IDS_PATH_ERROR).c_str(), targetPath.c_str(),
                                SPLGetErrorTextOwned(SalamanderGeneral, err).c_str());
                            SalamanderGeneral->SalMessageBox(parent, message.c_str(), TitleWMobileError,
                                                             MB_OK | MB_ICONEXCLAMATION);
                            pathError = TRUE;
                            break; // report the error
                        }
                    }

                    end = targetPath.rfind(L'\\', end - 1);
                    if (end == std::wstring::npos)
                    {
                        pathError = TRUE;
                        break;
                    }
                }
                SetCursor(oldCur);

                if (!pathError) // the split finished without errors
                {
                    if (end < targetPath.size() && targetPath[end] == L'\\')
                        end++;

                    const wchar_t* dirName = NULL;
                    const wchar_t* curPath = NULL;
                    std::wstring currentFullPath;
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
                        if (f != NULL)
                            dirName = f->Name;
                        currentFullPath = std::wstring(fsName) + L":" + Path;
                        curPath = currentFullPath.c_str();
                    }

                    std::wstring newDirs;
                    if (WMobileSplitGeneralPath(
                            parent, TitleWMobile, TitleWMobileError,
                            selectedFiles + selectedDirs, targetPath, afterRoot, end,
                            pathIsDir, backslashAtEnd, dirName, curPath,
                            operationMaskValue, &newDirs, TRUE))
                    {
                        if (!newDirs.empty()) // need to create some subdirectories on the target path
                        {
                            const std::wstring targetUserPart = targetPath.substr(userPartOffset);
                            if (!CRAPI::CheckAndCreateDirectory(targetUserPart.c_str(), parent, true))
                            {
                                SPLSalPathAppendOwned(targetPath, operationMaskValue.c_str());
                                pathError = TRUE;
                            }
                        }
                        else if (dirName != NULL && curPath != NULL &&
                                 SalamanderGeneral->StrICmp(dirName, operationMaskValue.c_str()) == 0 &&
                                 CEFS_IsTheSamePathWide(targetPath.c_str(), curPath))
                        {
                            // Renaming/copying a directory into itself (differing only by letter case) - "change-case"
                            // cannot be treated as an operation mask (the specified target path exists; splitting it into a mask is
                            // the result of the analysis)

                            rename = TRUE;
                        }
                    }
                    else
                        pathError = TRUE;
                }

                if (pathError)
                {
                    // 'targetPath' is returned after the modification (path expansion) + the ".." and "." cleanup + the mask that may have been added
                    return FALSE; // error -> reopen the standard dialog
                }
                diskPath = FALSE; // path on this FS successfully analysed
            }
        }

        if (diskPath)
        {
            // Windows path, path into an archive, or to an unknown FS - let the standard processing handle it
            operationMask = TRUE; // operation masks are supported
            cancelOrHandlePath = TRUE;
            return FALSE; // let Salamander process the path
        }
    }

    if (mode == 5)             // operation target specified via drag&drop
    {
        // If it is a disk path, just set the operation mask and continue (same as with 'mode'==3);
        // if it is a path into an archive, throw a "not supported" error; for a CEFS path set
        // 'diskPath'=FALSE and compute 'userPart' (points to the user portion of the CEFS path); for
        // a path to another FS, throw a "not supported" error

        BOOL ok = FALSE;
        operationMaskValue = L"*.*";
        int type;
        size_t secondPartOffset = std::wstring::npos;
        BOOL isDir;
        if (targetPath.size() >= 2 &&
            (targetPath[1] == L':' ||
             targetPath[0] == L'\\' && targetPath[1] == L'\\'))
        {                                                   // append a trailing backslash so it is always treated as a path (for 'mode'==5 it is always a path)
            SPLSalPathAddBackslashOwned(targetPath);
        }
        if (SPLSalParsePathOwned(SalamanderGeneral, parent, targetPath, type, isDir,
                                 secondPartOffset, TitleWMobileError, FALSE, NULL))
        {
            switch (type)
            {
            case PATH_TYPE_WINDOWS:
            {
                if (secondPartOffset < targetPath.size())
                {
                    SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_TARGETPATHNOEXISTS).c_str(),
                                                     TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
                }
                else
                    ok = TRUE;
                break;
            }

            case PATH_TYPE_FS:
            {
                userPartOffset = secondPartOffset;
                const std::wstring targetFSName = targetPath.substr(0, userPartOffset - 1);
                if (SalamanderGeneral->StrICmp(targetFSName.c_str(), fsName) == 0)
                { // je to CEFS
                    diskPath = FALSE;
                    ok = TRUE;
                }
                else // different FS, just report "not supported"
                {
                    SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_COPYTOOTHERFS).c_str(), TitleWMobileError,
                                                     MB_OK | MB_ICONEXCLAMATION);
                }
                break;
            }

            //case PATH_TYPE_ARCHIVE:
            default: // archive, just report "not supported"
            {
                SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_COPYTOARCHIVES).c_str(),
                                                 TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
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

    // 'mode' is 2, 3 or 5

    if (mode == 3)
        operationMaskValue = suppliedMask;

    // Prepare buffers for names.
    //
    // All three semantic bases and every leaf stay owned UTF-16. Only the named v108 split/output
    // adapters above project through the live callback's fixed writable storage.
    wmobile::PathComposer sourceName; // the device path of the item being copied
    if (!sourceName.SetBase(Path.c_str()))
        return FALSE;

    // The CEFS name is a disk-cache LOOKUP KEY, not a path: "<fsName>:<device path>", lowercased
    // because the cache is case-sensitive while device names are not. It is composed from the
    // same leaf as sourceName, so the two must move width together or the key stops matching
    // what the cache stored.
    std::wstring cefsBase = std::wstring(fsName) + L":" + sourceName.Get();
    SPLToLowerCaseOwned(SalamanderGeneral, cefsBase);
    wmobile::PathComposer cefsSourceName;
    if (!cefsSourceName.SetBase(cefsBase.c_str()))
        return FALSE;

    wmobile::PathComposer targetName; // PC path when 'diskPath', otherwise another device path
    const std::wstring targetBase = diskPath ? targetPath : targetPath.substr(userPartOffset);
    if (!targetName.SetBase(targetBase.c_str()))
        return FALSE;

    const CFileData* f = NULL; // pointer to the file/directory in the panel to process
    BOOL isDir = FALSE;        // TRUE if 'f' is a directory
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL success = TRUE;                     // FALSE in case of an error or user cancellation
    BOOL skipAllErrors = FALSE;              // skip all errors
    BOOL sourcePathChanged = FALSE;          // TRUE if the source path changed (move operation)
    BOOL subdirsOfSourcePathChanged = FALSE; // TRUE if subdirectories of the source path changed
    BOOL targetPathChanged = FALSE;          // TRUE if the target path changed
    BOOL subdirsOfTargetPathChanged = FALSE; // TRUE if subdirectories of the target path changed
    BOOL skipAllOverwrite = FALSE;
    BOOL skipAllOverwriteSystemHidden = FALSE;

    rename = !copy && CEFS_IsTheSamePathWide(sourceName, targetName);

    // Retrieve the "Confirm on" values from the configuration
    BOOL ConfirmOnFileOverwrite, ConfirmOnSystemHiddenFileOverwrite;
    SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMFILEOVER, &ConfirmOnFileOverwrite, 4, NULL);
    SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHFILEOVER, &ConfirmOnSystemHiddenFileOverwrite, 4, NULL);

    SalamanderGeneral->CreateSafeWaitWindow(LangStr(IDS_WAIT_READINGDIRTREE).c_str(), TitleWMobile,
                                            500, FALSE, SalamanderGeneral->GetMainWindowHWND());
    CFileInfoArray array(10, 10);

    while (1)
    {
        // Retrieve data about the file being processed
        if (focused)
            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        else
            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

        // Perform copy/move on the file/directory
        if (f != NULL)
        {
            if (rename)
            {
                // fi.cFileName is wide now: store f->Name (already the genuine
                // wide panel-selection name, CFileData::Name) directly - no narrow round trip,
                // no refusal. The old code refused (aborted the WHOLE batch, since this branch
                // shared its check with the recursion branch below) the moment one selected
                // item's name couldn't survive a CP_ACP round trip it never actually needed
                // for a single-file rename.
                if (!array.AddOwned(f->Name, f->Attr,
                                    100, // JR Renaming takes roughly the same time regardless of size
                                    0))
                {
                    SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_MEMORYLOW).c_str(), TitleWMobileError, MSGBOX_ERROR);
                    TRACE_E("Low memory");
                    success = false;
                }
            }
            else
            {
                // Device enumeration is Unicode-native and keeps the selected name wide.
                success = CRAPI::FindAllFilesInTreeWide(Path.c_str(), f->Name, array, 0, TRUE);
            }
        }

        // Determine whether it makes sense to continue (when not cancelled and another selected item exists)
        if (!success || focused || f == NULL)
            break;
    }

    SalamanderGeneral->DestroySafeWaitWindow();

    HWND mainWnd = parent;
    HWND parentWin;
    while ((parentWin = GetParent(mainWnd)) != NULL && IsWindowEnabled(parentWin))
        mainWnd = parentWin;
    // disablujeme 'mainWnd'

    CProgress2Dlg dlg(mainWnd, LangStr(copy ? IDS_COPY : IDS_MOVE).c_str(), LangStr(copy ? IDS_COPYING : IDS_MOVING).c_str(), LangStr(IDS_TO).c_str(), ooStatic); // use 'ooStatic' so the modeless dialog can live on the stack

    dlg.Create();
    EnableWindow(mainWnd, FALSE);
    SetForegroundWindow(dlg.HWindow);

    INT64 totalsize = 0, copied = 0;
    int i;
    for (i = 0; i < array.Count; i++)
        totalsize += (array[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 100 : array[i].size;

    for (/*int*/ i = 0; i < array.Count; i++)
    {
        CFileInfo& fi = array[i];

        // fi.cFileName is used as it comes off the device. What stood here was a
        // narrowing bridge that did `continue` when a name left the system code page - so a copy
        // of a folder containing such a file quietly produced a folder MISSING that file, with
        // only a TRACE_E to say so. Nothing downstream needs the name narrow any more.
        std::wstring maskedName;
        const wchar_t* targetFile = SPLMaskNameOwned(SalamanderGeneral, fi.cFileName, operationMaskValue.c_str(), maskedName)
                                        ? maskedName.c_str()
                                        : NULL;

        // The composers replace their owned leaves without a path-sized capacity.
        if (targetFile == NULL ||
            !sourceName.SetLeaf(fi.cFileName) ||
            !cefsSourceName.SetLeaf(fi.cFileName) ||
            !targetName.SetLeaf(targetFile))
        {
            SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_NAMETOOLONG).c_str(),
                                             TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
            success = FALSE;
            break;
        }

        // Disk names are case-insensitive, the disk cache is case-sensitive; converting
        // to lowercase makes the disk cache behave case-insensitively as well
        const wchar_t* sourceLeaf = cefsSourceName.Leaf();
        if (sourceLeaf == NULL)
            return FALSE;
        std::wstring foldedLeaf(sourceLeaf);
        if (!SPLToLowerCaseOwned(SalamanderGeneral, foldedLeaf) ||
            !cefsSourceName.SetLeaf(foldedLeaf.c_str()))
            return FALSE;

        isDir = (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (copy && SalamanderGeneral->StrICmp(sourceName.Get(), targetName.Get()) == 0)
        {
            SalamanderGeneral->SalMessageBox(parent, LangStr(isDir ? IDS_ERR_COPYDIRTOITSELF : IDS_ERR_COPYFILETOITSELF).c_str(), TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
            success = FALSE;
            break;
        }

        dlg.Set(sourceName, targetName, FALSE);

        BOOL skip = FALSE;
        BOOL fileMoved = FALSE;
        if (isDir && !rename)
        {
            if (fi.block == -1)
            {
                while (1)
                {
                    // the SDK arm is wide; CRAPI:: is this plugin's own narrow helper
                    if (!(diskPath ? SalamanderGeneral->CheckAndCreateDirectory(targetName, parent, true) : CRAPI::CheckAndCreateDirectory(targetName, parent, true)))
                    {
                        if (!skipAllErrors)
                        {
                            DWORD err = diskPath ? GetLastError() : CRAPI::GetLastError();
                            int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, targetName.Get(),
                                                                     SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                        subdirsOfTargetPathChanged = TRUE;
                        break; // successfully created directory
                    }
                } // end of while(1)
            }
        }
        else
        {
            DWORD attr = 0xFFFFFFFF;

            if (!rename || SalamanderGeneral->StrICmp(sourceName, targetName) != 0) // Not a simple change in letter case
            {
                if (diskPath)
                    attr = SalamanderGeneral->SalGetFileAttributes(targetName);
                else
                    attr = CRAPI::GetFileAttributesWide(targetName);
            }

            if (attr != 0xFFFFFFFF)
            {
                if (!skipAllOverwrite)
                {
                    if (ConfirmOnFileOverwrite)
                    {
                        const std::wstring sourceData = CRAPI::GetFileDataWide(sourceName);
                        std::wstring targetData;
                        if (diskPath)
                            targetData = GetFileDataWide(targetName);
                        else
                            targetData = CRAPI::GetFileDataWide(targetName);

                        int res = SalamanderGeneral->DialogOverwrite(parent, BUTTONS_YESALLSKIPCANCEL,
                                                                    targetName, targetData.c_str(),
                                                                    sourceName, sourceData.c_str());
                        switch (res)
                        {
                        case DIALOG_ALL:
                            ConfirmOnFileOverwrite = FALSE;
                        case DIALOG_YES:
                            break;

                        case DIALOG_SKIPALL:
                            skipAllOverwrite = TRUE;
                        case DIALOG_SKIP:
                            skip = TRUE;
                            break;

                        default:
                            success = FALSE;
                            break;
                        }
                    }
                }
                else
                    skip = TRUE;

                if (success && !skip && ConfirmOnSystemHiddenFileOverwrite && (attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
                {
                    if (!skipAllOverwriteSystemHidden)
                    {
                        int res = SalamanderGeneral->DialogQuestion(parent, BUTTONS_YESALLSKIPCANCEL, targetName,
                                                                    LangStr(IDS_YESNO_OVERWRITEHIDDENFILE).c_str(), TitleWMobileQuestion);
                        switch (res)
                        {
                        case DIALOG_ALL:
                            ConfirmOnSystemHiddenFileOverwrite = FALSE;
                        case DIALOG_YES:
                            break;

                        case DIALOG_SKIPALL:
                            skipAllOverwriteSystemHidden = TRUE;
                        case DIALOG_SKIP:
                            skip = TRUE;
                            break;

                        default:
                            success = FALSE;
                            break;
                        }
                    }
                    else
                        skip = TRUE;
                }
            }

            if (success && !skip)
            {
                while (1)
                {
                    DWORD err = 0;
                    const wchar_t* errFileName = L"";
                    if (diskPath) // JR Windows destination path
                    {
                        if (attr != 0xFFFFFFFF &&
                            (attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY)))
                            ::SetFileAttributesW(targetName, FILE_ATTRIBUTE_ARCHIVE); // PC

                        err = CRAPI::CopyFileToPC(sourceName, targetName, FALSE, &dlg, copied, totalsize, &errFileName);
                    }
                    else
                    {
                        if (attr != 0xFFFFFFFF &&
                            (attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY)))
                            CRAPI::SetFileAttributesWide(targetName, FILE_ATTRIBUTE_ARCHIVE);

                        if (copy)
                        {
                            err = CRAPI::CopyFileWide(sourceName, targetName, FALSE, &dlg, copied, totalsize, &errFileName);
                        }
                        else
                        {
                            if (attr != 0xFFFFFFFF)
                                CRAPI::DeleteFileWide(targetName);

                            if (!CRAPI::MoveFileWide(sourceName, targetName))
                                err = CRAPI::GetLastError();
                            else if (dlg.GetWantCancel())
                                err = -1;
                            if (err == 0)
                                fileMoved = TRUE;
                        }
                    }

                    if (err != 0)
                    {
                        if (err == -1) // JR cancelled by the user
                            success = FALSE;
                        else if (!skipAllErrors)
                        {
                            int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, errFileName,
                                                                     SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                        if (fileMoved)
                            sourcePathChanged = TRUE;
                        break; // copied successfully
                    }

                    if (!success || skip)
                        break;
                } // end of while(1)
            }
        }

        if (success && !copy && !skip && !fileMoved) // it is a "move" and the file was not skipped -> delete the source file
        {
            while (1)
            {
                if (isDir)
                {
                    if (fi.block != -1)
                    {
                        if (fi.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY))
                            CRAPI::SetFileAttributesWide(sourceName, FILE_ATTRIBUTE_ARCHIVE);

                        if (!CRAPI::RemoveDirectoryWide(sourceName))
                        {
                            if (!skipAllErrors)
                            {
                                DWORD err = CRAPI::GetLastError();
                                int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, cefsSourceName.Get(),
                                                                         SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                            WMobileRemoveFilesFromCacheWide(cefsSourceName.Get());

                            sourcePathChanged = TRUE;
                            subdirsOfSourcePathChanged = TRUE;
                            break; // successful RemoveDirectory
                        }
                    }
                    else
                        break;
                }
                else
                {
                    // remove the file on CEFS
                    if (fi.dwFileAttributes & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY))
                        CRAPI::SetFileAttributesWide(sourceName, FILE_ATTRIBUTE_ARCHIVE);

                    if (!CRAPI::DeleteFileWide(sourceName))
                    {
                        if (!skipAllErrors)
                        {
                            DWORD err = CRAPI::GetLastError();
                            int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, cefsSourceName.Get(),
                                                                     SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                        WMobileRemoveFileFromCacheWide(cefsSourceName.Get());

                        sourcePathChanged = TRUE;
                        break; // successful delete
                    }
                }
                if (!success || skip)
                    break;
            }
        }

        if (success)
        {
            copied += isDir ? 100 : fi.size;

            float progress = (totalsize ? ((float)copied / (float)totalsize) : 0) * 1000;
            dlg.SetProgress((DWORD)progress, 0, FALSE);
        }
        else
            break; // Determine whether it makes sense to continue (when not cancelled)
    }

    EnableWindow(mainWnd, TRUE);
    DestroyWindow(dlg.HWindow); // close the progress dialog

    // Change on the source path Path (primarily move operations)
    if (sourcePathChanged)
    {
        const std::wstring changedPath = std::wstring(fsName) + L":" + Path;
        SalamanderGeneral->PostChangeOnPathNotification(changedPath.c_str(), subdirsOfSourcePathChanged);
    }

    // Change on the target path 'targetPath'
    if (targetPathChanged)
        SalamanderGeneral->PostChangeOnPathNotification(targetPath.c_str(), subdirsOfTargetPathChanged);

    SalamanderGeneral->RestoreFocusInSourcePanel();

    if (success)
        targetPath = nextFocus;
    else
        cancelOrHandlePath = TRUE; // error/cancel
    return TRUE;                   // success or error/cancel handled
}

static BOOL FindAllFilesInTree(const wchar_t* rootPath, std::wstring& path, const wchar_t* fileName,
                               CFileInfoArray& array, BOOL dirFirst, int block)
{
    HANDLE find = INVALID_HANDLE_VALUE;

    WIN32_FIND_DATAW data;
    std::wstring fullPath(rootPath);
    SPLSalPathAppendOwned(fullPath, path.c_str());
    SPLSalPathAppendOwned(fullPath, fileName);
    find = FindFirstFileW(fullPath.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_NO_MORE_FILES || err == ERROR_FILE_NOT_FOUND)
            return TRUE; // JR empty directory, stop

        const std::wstring message = SPLFormatStringOwned(LangStr(IDS_PATH_ERROR).c_str(), fullPath.c_str(),
                                                           SPLGetErrorTextOwned(SalamanderGeneral, err).c_str());
        SalamanderGeneral->ShowMessageBox(message.c_str(), TitleWMobileError, MSGBOX_ERROR);
        return FALSE;
    }

    for (;;)
    {
        // JR TODO: This does not work!
        if (SalamanderGeneral->GetSafeWaitWindowClosePressed())
        {
            if (SalamanderGeneral->ShowMessageBox(LangStr(IDS_YESNO_CANCEL).c_str(), TitleWMobileQuestion,
                                                  MSGBOX_QUESTION) == IDYES)
                goto ONERROR;
        }

        if (data.cFileName[0] != 0 &&
            (data.cFileName[0] != L'.' || // JR Windows Mobile does not return "." and ".." paths, but handle it just in case
             (data.cFileName[1] != 0 && (data.cFileName[1] != L'.' || data.cFileName[2] != 0))))
        {
            std::wstring relativePath(path);
            SPLSalPathAppendOwned(relativePath, data.cFileName);
            const BOOL isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if ((!isDirectory || dirFirst) &&
                !array.AddOwned(relativePath.c_str(), data.dwFileAttributes,
                                isDirectory ? 0 : data.nFileSizeLow,
                                isDirectory ? -1 : block))
            {
                SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_MEMORYLOW).c_str(), TitleWMobileError, MSGBOX_ERROR);
                TRACE_E("Low memory");
                goto ONERROR;
            }

            if (isDirectory)
            {
                const size_t oldLength = path.size();
                SPLSalPathAppendOwned(path, data.cFileName);
                if (!FindAllFilesInTree(rootPath, path, L"*.*", array, dirFirst, block))
                    goto ONERROR; // JR The error has already been reported
                path.resize(oldLength);

                if (!dirFirst)
                {
                    if (!array.AddOwned(relativePath.c_str(), data.dwFileAttributes, 0, block))
                    {
                        SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_MEMORYLOW).c_str(), TitleWMobileError, MSGBOX_ERROR);
                        TRACE_E("Low memory");
                        goto ONERROR;
                    }
                }
            }
        }

        if (!FindNextFileW(find, &data))
        {
            if (GetLastError() == ERROR_NO_MORE_FILES)
                break; // JR Everything is fine, stop

            DWORD err = GetLastError();
            SalamanderGeneral->ShowMessageBox(SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), TitleWMobileError, MSGBOX_ERROR);
            FindClose(find);
            return FALSE;
        }
    }

    FindClose(find);
    return TRUE;

ONERROR:
    if (find != INVALID_HANDLE_VALUE)
        FindClose(find);
    return FALSE;
}

static BOOL FindAllFilesInTree(const wchar_t* rootPath, const wchar_t* fileName,
                               CFileInfoArray& array, BOOL dirFirst, int block)
{
    std::wstring path;
    return FindAllFilesInTree(rootPath, path, fileName, array, dirFirst, block);
}

BOOL WINAPI
CPluginFSInterface::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                           const wchar_t* sourcePath, SalEnumSelection2 next,
                                           void* nextParam, int sourceFiles, int sourceDirs,
                                           CSalamanderStringBuffer* targetPath, BOOL* invalidPathOrCancel)
{
    std::wstring payload;
    wmobile::OperationTarget callbackTarget;
    if (targetPath == NULL ||
        !sally::plugin_abi::ReadStringBuffer(*targetPath, payload) ||
        !wmobile::DecodeOperationTarget(payload, false, callbackTarget))
    {
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }
    const BOOL result = CopyOrMoveFromDiskToFSOwned(copy, mode, fsName, parent, sourcePath,
                                                     next, nextParam, sourceFiles, sourceDirs,
                                                     callbackTarget.path, invalidPathOrCancel);
    if (!sally::plugin_abi::WriteStringBuffer(*targetPath, callbackTarget.path))
    {
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }
    return result;
}

BOOL WINAPI
CPluginFSInterface::CopyOrMoveFromDiskToFSOwned(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                                const wchar_t* sourcePath, SalEnumSelection2 next,
                                                void* nextParam, int sourceFiles, int sourceDirs,
                                                std::wstring& targetPath, BOOL* invalidPathOrCancel)
{
    if (invalidPathOrCancel != NULL)
        *invalidPathOrCancel = FALSE;

    if (mode == 1)
    {
        // Add the *.* mask to the target path (we will process operation masks)
        SPLSalPathAppendOwned(targetPath, L"*.*");
        return TRUE;
    }

    if (mode != 2 && mode != 3)
        return FALSE; // unknown 'mode'

    // 'targetPath' contains the raw path entered by the user (all we know is that it belongs
    // to this FS, otherwise Salamander would not call this method)
    const size_t colon = targetPath.find(L':');
    if (colon == std::wstring::npos)
    {
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }
    const size_t userPartOffset = colon + 1;

    BOOL invPath = userPartOffset >= targetPath.size() || targetPath[userPartOffset] != L'\\';

    // Check whether the operation can be executed on this FS; the user might also have used
    // "." and ".." in the full path to this FS - remove them
    size_t rootLen = 0;
    if (!invPath)
        rootLen = 1;

    if (invPath)
    {
        // Additionally we could display 'err' (when 'invPath' is TRUE); ignored here for simplicity
        SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_INVALIDPATH).c_str(),
                                         TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
        // 'targetPath' is returned after possibly adjusting some ".." and "."
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE; // let the user correct the path
    }
    if (!WMobileRemovePointsFromPathOwned(targetPath, userPartOffset + rootLen))
    {
        SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_INVALIDPATH).c_str(),
                                         TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }

    // Trim the unnecessary backslash
    const size_t userPartLength = targetPath.size() - userPartOffset;
    BOOL backslashAtEnd = userPartLength > 0 && targetPath.back() == L'\\';
    if (userPartLength > 1 && targetPath.back() == L'\\')
        targetPath.pop_back();

    // Analyse the path - locate the existing part, the missing part, and the operation mask
    //
    // - Determine which portion exists and whether it is a file or a directory,
    //   then select what applies:
    //   - write to the path (possibly with a missing portion) with a mask - the mask is the last missing part
    //     after which there is no backslash (verify that when multiple source files/directories are involved the mask
    //     contains '*' or at least '?'; otherwise it is nonsense -> only one target name is possible)
    //   - write into an archive (the path contains an archive file, or it may not even be an archive, resulting in
    //     the "Salamander does not know how to open this file" error)
    //   - overwrite a file (the entire path is just the target file name; must not end with a backslash)

    // Determine how far the path exists (split into existing and missing parts)
    HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
    size_t end = targetPath.size();
    const size_t afterRoot = userPartOffset + rootLen;
    BOOL pathIsDir = TRUE;
    BOOL pathError = FALSE;

    // If the path contains a mask, cut it off without calling GetFileAttributes
    if (end > afterRoot) // still more than just the root
    {
        const size_t slash = targetPath.rfind(L'\\', end - 1);
        if (slash != std::wstring::npos &&
            targetPath.find_first_of(L"*?", slash + 1) != std::wstring::npos)
            end = slash;
    }

    while (end > afterRoot) // still more than just the root
    {
        const std::wstring existingPath = targetPath.substr(userPartOffset, end - userPartOffset);
        DWORD attrs = CRAPI::GetFileAttributesWide(existingPath.c_str());
        if (attrs != 0xFFFFFFFF) // this portion of the path exists
        {
            if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) // it is a file
            {
                // An existing path must not include a file name (see SalSplitGeneralPath); trim it...
                pathIsDir = FALSE; // the existing part of the path is a file
                end = targetPath.rfind(L'\\', end - 1);
                if (end == std::wstring::npos)
                    pathError = TRUE;
                break;
            }
            else
                break;
        }
        else
        {
            DWORD err = CRAPI::GetLastError();
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_INVALID_NAME &&
                err != ERROR_PATH_NOT_FOUND && err != ERROR_BAD_PATHNAME &&
                err != ERROR_DIRECTORY) // unexpected error - just report it
            {
                const std::wstring message = SPLFormatStringOwned(
                    LangStr(IDS_PATH_ERROR).c_str(), targetPath.c_str(),
                    SPLGetErrorTextOwned(SalamanderGeneral, err).c_str());
                SalamanderGeneral->SalMessageBox(parent, message.c_str(), TitleWMobile,
                                                 MB_OK | MB_ICONEXCLAMATION);
                pathError = TRUE;
                break; // report the error
            }
        }

        end = targetPath.rfind(L'\\', end - 1);
        if (end == std::wstring::npos)
        {
            pathError = TRUE;
            break;
        }
    }
    SetCursor(oldCur);

    std::wstring operationMaskValue;
    if (!pathError) // the split finished without errors
    {
        if (end < targetPath.size() && targetPath[end] == L'\\')
            end++;

        std::wstring newDirs;
        if (WMobileSplitGeneralPath(
                parent, TitleWMobile, TitleWMobileError, sourceFiles + sourceDirs,
                targetPath, afterRoot, end, pathIsDir, backslashAtEnd,
                NULL, NULL, operationMaskValue, &newDirs, FALSE))
        {
            if (!newDirs.empty()) // need to create some subdirectories on the target path
            {
                const std::wstring targetUserPart = targetPath.substr(userPartOffset);
                if (!CRAPI::CheckAndCreateDirectory(targetUserPart.c_str(), parent, true))
                {
                    SPLSalPathAppendOwned(targetPath, operationMaskValue.c_str());
                    pathError = TRUE;
                }
            }
        }
        else
            pathError = TRUE;
    }

    if (pathError)
    {
        // 'targetPath' is returned after cleaning up ".." and "." + any mask that was added
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE; // path error - let the user fix it
    }

    // targetPath and its user-part offset remain owned UTF-16; operationMaskValue is the mask.

    // Prepare buffers for names. Same shape as the other direction's loop, and the same two
    // domains - here the SOURCE is on the PC and the TARGET is on the phone, which is the reverse
    // of CopyOrMoveFromFS. See wmobile_path_core's PathComposer.
    wmobile::PathComposer sourceName; // PC path of the item being copied
    if (!sourceName.SetBase(sourcePath))
        return FALSE;

    wmobile::PathComposer targetName; // device path it is being copied to
    const std::wstring targetBase = targetPath.substr(userPartOffset);
    if (!targetName.SetBase(targetBase.c_str()))
        return FALSE;

    SalamanderGeneral->CreateSafeWaitWindow(LangStr(IDS_WAIT_READINGDIRTREE).c_str(), TitleWMobile,
                                            500, FALSE, SalamanderGeneral->GetMainWindowHWND());
    CFileInfoArray array(10, 10);

    BOOL success = TRUE; // FALSE in case of an error or user cancellation

    BOOL isDir;
    const wchar_t* name;
    const wchar_t* dosName; // dummy
    CQuadWord size;
    DWORD attr1;
    FILETIME lastWrite;
    int errorOccured;

    while ((name = next(parent, 0, &dosName, &isDir, &size, &attr1, &lastWrite, nextParam, &errorOccured)) != NULL)
    { // perform copy/move on a file/directory
        success = FindAllFilesInTree(sourcePath, name, array, TRUE, 0);

        // Determine whether it makes sense to continue (when not cancelled)
        if (!success)
            break;
    }

    SalamanderGeneral->DestroySafeWaitWindow();

    BOOL skipAllErrors = FALSE;              // skip all errors
    BOOL sourcePathChanged = FALSE;          // TRUE if the source path changed (move operation)
    BOOL subdirsOfSourcePathChanged = FALSE; // TRUE if subdirectories of the source path changed
    BOOL targetPathChanged = FALSE;          // TRUE if the target path changed
    BOOL subdirsOfTargetPathChanged = FALSE; // TRUE if subdirectories of the target path changed
    BOOL skipAllOverwrite = FALSE;
    BOOL skipAllOverwriteSystemHidden = FALSE;

    // Retrieve the "Confirm on" values from the configuration
    BOOL ConfirmOnFileOverwrite, ConfirmOnSystemHiddenFileOverwrite;
    SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMFILEOVER, &ConfirmOnFileOverwrite, 4, NULL);
    SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHFILEOVER, &ConfirmOnSystemHiddenFileOverwrite, 4, NULL);

    HWND mainWnd = parent;
    HWND parentWin;
    while ((parentWin = GetParent(mainWnd)) != NULL && IsWindowEnabled(parentWin))
        mainWnd = parentWin;
    // Disable 'mainWnd'

    CProgress2Dlg dlg(mainWnd, LangStr(copy ? IDS_COPY : IDS_MOVE).c_str(), LangStr(copy ? IDS_COPYING : IDS_MOVING).c_str(), LangStr(IDS_TO).c_str(), ooStatic); // use 'ooStatic' so the modeless dialog can live on the stack

    dlg.Create();
    EnableWindow(mainWnd, FALSE);
    SetForegroundWindow(dlg.HWindow);

    INT64 totalsize = 0, copied = 0;
    int i;
    for (i = 0; i < array.Count; i++)
        totalsize += (array[i].dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 100 : array[i].size;

    for (/*int*/ i = 0; i < array.Count; i++)
    {
        CFileInfo& fi = array[i];

        // As in the other direction: what stood here narrowed the name and did
        // `continue` on failure, so a PC file whose name is outside the system code page was
        // dropped from the copy without the user being told.
        std::wstring maskedName;
        const wchar_t* targetFile = SPLMaskNameOwned(SalamanderGeneral, fi.cFileName, operationMaskValue.c_str(), maskedName)
                                        ? maskedName.c_str()
                                        : NULL;

        // 'name' covers only the root of the source path - no subdirectories - so the mask is
        // applied to the whole name. SetLeaf carries the length refusal the two explicit checks
        // used to do.
        if (targetFile == NULL ||
            !sourceName.SetLeaf(fi.cFileName) ||
            !targetName.SetLeaf(targetFile))
        {
            SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_ERR_NAMETOOLONG).c_str(),
                                             TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
            success = FALSE;
            break;
        }

        isDir = (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (SalamanderGeneral->StrICmp(sourceName, targetName) == 0)
        {
            SalamanderGeneral->SalMessageBox(parent,
                                             LangStr(copy ? (isDir ? IDS_ERR_COPYDIRTOITSELF : IDS_ERR_COPYFILETOITSELF) : (isDir ? IDS_ERR_MOVEDIRTOITSELF : IDS_ERR_MOVEFILETOITSELF)).c_str(),
                                             TitleWMobileError, MB_OK | MB_ICONEXCLAMATION);
            success = FALSE;
            break;
        }

        dlg.Set(sourceName, targetName, FALSE);

        BOOL skip = FALSE;
        if (isDir)
        {
            if (fi.block == -1)
            {
                while (1)
                {
                    if (!CRAPI::CheckAndCreateDirectory(targetName, parent, true))
                    {
                        if (!skipAllErrors)
                        {
                            DWORD err = CRAPI::GetLastError();
                            int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, targetName,
                                                                     SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                        subdirsOfTargetPathChanged = TRUE;
                        break; // successfully created directory
                    }
                } // end of while(1)
            }
        }
        else
        {
            // copy the file directly to CEFS

            DWORD attr = CRAPI::GetFileAttributesWide(targetName);

            if (attr != 0xFFFFFFFF)
            {
                if (!skipAllOverwrite)
                {
                    if (ConfirmOnFileOverwrite)
                    {
                        const std::wstring sourceData = GetFileDataWide(sourceName);
                        const std::wstring targetData = CRAPI::GetFileDataWide(targetName);

                        int res = SalamanderGeneral->DialogOverwrite(parent, BUTTONS_YESALLSKIPCANCEL,
                                                                    targetName, targetData.c_str(),
                                                                    sourceName, sourceData.c_str());
                        switch (res)
                        {
                        case DIALOG_ALL:
                            ConfirmOnFileOverwrite = FALSE;
                        case DIALOG_YES:
                            break;
                        case DIALOG_SKIPALL:
                            skipAllOverwrite = TRUE;
                        case DIALOG_SKIP:
                            skip = TRUE;
                            break;
                        default:
                            success = FALSE;
                            break;
                        }
                    }
                }
                else
                    skip = TRUE;

                if (success && !skip && ConfirmOnSystemHiddenFileOverwrite && (attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
                {
                    if (!skipAllOverwriteSystemHidden)
                    {
                        int res = SalamanderGeneral->DialogQuestion(parent, BUTTONS_YESALLSKIPCANCEL, targetName,
                                                                    LangStr(IDS_YESNO_OVERWRITEHIDDENFILE).c_str(), TitleWMobileQuestion);
                        switch (res)
                        {
                        case DIALOG_ALL:
                            ConfirmOnSystemHiddenFileOverwrite = FALSE;
                        case DIALOG_YES:
                            break;
                        case DIALOG_SKIPALL:
                            skipAllOverwriteSystemHidden = TRUE;
                        case DIALOG_SKIP:
                            skip = TRUE;
                            break;
                        default:
                            success = FALSE;
                            break;
                        }
                    }
                    else
                        skip = TRUE;
                }
            }
            else
                attr = 0;

            if (success && !skip)
            {
                while (1)
                {
                    if (attr & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY))
                        CRAPI::SetFileAttributesWide(targetName, FILE_ATTRIBUTE_ARCHIVE);

                    const wchar_t* errFileName = L"";
                    DWORD err = CRAPI::CopyFileToCE(sourceName, targetName, FALSE, &dlg, copied, totalsize, &errFileName);
                    if (err != 0)
                    {
                        if (err == -1) // JR cancelled by the user
                            success = FALSE;
                        else if (!skipAllErrors)
                        {
                            int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, errFileName,
                                                                     SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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

                        std::wstring cacheKey = std::wstring(fsName) + L":" + targetName.Get();
                        WMobileRemoveFileFromCacheWide(cacheKey.c_str());

                        break; // copied successfully
                    }
                    if (!success || skip)
                        break;
                }
            }
        }

        if (success && !copy && !skip) // it is a "move" and the file was not skipped -> delete the source file
        {

            // remove the file on disk
            while (1)
            {
                if (isDir)
                {
                    if (fi.block != -1)
                    {
                        SalamanderGeneral->ClearReadOnlyAttr(sourceName, fi.dwFileAttributes);

                        if (!::RemoveDirectoryW(sourceName) /* PC */)
                        {
                            if (!skipAllErrors)
                            {
                                DWORD err = GetLastError();
                                int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, sourceName,
                                                                         SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                            subdirsOfSourcePathChanged = TRUE;
                            break; // successful RemoveDirectory
                        }
                    }
                    else
                        skip = TRUE;
                }
                else
                {
                    SalamanderGeneral->ClearReadOnlyAttr(sourceName, fi.dwFileAttributes);

                    if (!::DeleteFileW(sourceName) /* PC */)
                    {
                        if (!skipAllErrors)
                        {
                            DWORD err = GetLastError();
                            int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, sourceName,
                                                                     SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                        break; // successful delete
                    }
                }
                if (!success || skip)
                    break;
            }
        }

        if (success)
        {
            copied += isDir ? 100 : fi.size;

            float progress = (totalsize ? ((float)copied / (float)totalsize) : 0) * 1000;
            dlg.SetProgress((DWORD)progress, 0, FALSE);
        }
        else
            break; // Determine whether it makes sense to continue (when not cancelled)
    }

    EnableWindow(mainWnd, TRUE);
    DestroyWindow(dlg.HWindow); // close the progress dialog

    // Change on the source path (primarily move operations), can only be a disk path
    if (sourcePathChanged)
        SalamanderGeneral->PostChangeOnPathNotification(sourcePath, subdirsOfSourcePathChanged);

    // Change on the target path.
    if (targetPathChanged)
    {
        const std::wstring changedPath = std::wstring(fsName) + L":" + targetPath.substr(userPartOffset);
        SalamanderGeneral->PostChangeOnPathNotification(changedPath.c_str(), subdirsOfTargetPathChanged);
    }

    SalamanderGeneral->RestoreFocusInSourcePanel();

    if (success)
        return TRUE; // operation finished successfully
    else
    {
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return TRUE; // cancel
    }
}

BOOL WINAPI
CPluginFSInterface::ChangeAttributes(const wchar_t* fsName, HWND parent, int panel,
                                     int selectedFiles, int selectedDirs)
{
    std::wstring fileName;

    const CFileData* f = NULL; // pointer to the file/directory in the panel to process
    BOOL isDir = FALSE;        // TRUE if 'f' is a directory
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0, count = 0;
    BOOL success = TRUE;        // FALSE in case of an error or user cancellation
    BOOL skipAllErrors = FALSE; // skip all errors

    RapiNS::CE_FIND_DATA findData;
    DWORD attr = 0, attrDiff = 0;
    SYSTEMTIME timeModified, timeCreated, timeAccessed;
    BOOL selectedDirectory = FALSE;

    // JR phase 1 - determine the current attributes
    while (1)
    {
        // Retrieve data about the file being processed
        if (focused)
            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        else
            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

        // Perform the operation on the file/directory
        if (f != NULL)
        {

            fileName = Path;
            wmobile::AppendDeviceComponent(fileName, f->Name);

            BOOL skip = FALSE;
            while (1)
            {
                if (count == 0)
                {
                    HANDLE handle = CRAPI::FindFirstFileWide(fileName.c_str(), &findData);
                    if (handle == INVALID_HANDLE_VALUE)
                    {
                        if (skipAllErrors)
                            skip = TRUE;
                        else
                        {
                            DWORD err = CRAPI::GetLastError();
                            if (err == ERROR_NO_MORE_FILES)
                                err = ERROR_FILE_NOT_FOUND;
                            int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, fileName.c_str(),
                                                                     SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                    }
                    else
                    {
                        CRAPI::FindClose(handle);

                        DWORD attrib = findData.dwFileAttributes;
                        if (attrib & FILE_ATTRIBUTE_DIRECTORY)
                            selectedDirectory = TRUE;

                        attrib &= FILE_ATTRIBUTES_MASK;
                        attr |= attrib;

                        FILETIME time;
                        FileTimeToLocalFileTime(&findData.ftLastWriteTime, &time);
                        FileTimeToSystemTime(&time, &timeModified);
                        FileTimeToLocalFileTime(&findData.ftCreationTime, &time);
                        FileTimeToSystemTime(&time, &timeCreated);
                        FileTimeToLocalFileTime(&findData.ftLastAccessTime, &time);
                        FileTimeToSystemTime(&time, &timeAccessed);

                        count++;
                        break;
                    }
                }
                else
                {
                    DWORD attrib = CRAPI::GetFileAttributesWide(fileName.c_str());
                    if (attr == 0xFFFFFFFF)
                    {
                        if (skipAllErrors)
                            skip = TRUE;
                        else
                        {
                            DWORD err = CRAPI::GetLastError();
                            //if (err == ERROR_NO_MORE_FILES) err = ERROR_FILE_NOT_FOUND;
                            int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, fileName.c_str(),
                                                                     SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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
                    }
                    else
                    {
                        if (attrib & FILE_ATTRIBUTE_DIRECTORY)
                            selectedDirectory = TRUE;

                        attrib &= FILE_ATTRIBUTES_MASK;
                        attrDiff |= (attr ^ attrib);
                        attr |= attrib;

                        count++;
                        break;
                    }
                }
                if (!success || skip)
                    break;
            }
        }

        // Determine whether it makes sense to continue (when not cancelled and another selected item exists)
        if (!success || focused || f == NULL)
            break;
    }

    if (!success || count == 0)
    {
        // JR The file/directory was probably deleted already
        if (count == 0)
        {
            const std::wstring path = std::wstring(fsName) + L":" + Path;
            SalamanderGeneral->PostChangeOnPathNotification(path.c_str(), FALSE);
        }
        return FALSE;
    }

    if (count > 1)
    {
        GetSystemTime(&timeModified);
        timeCreated = timeModified;
        timeAccessed = timeModified;
    }

    CChangeAttrDialog dlgAttr(parent, ooStatic, attr, attrDiff, selectedDirectory, &timeModified, &timeCreated, &timeAccessed);
    if (dlgAttr.Execute() != IDOK)
        return FALSE;

    SalamanderGeneral->CreateSafeWaitWindow(LangStr(IDS_WAIT_READINGDIRTREE).c_str(), TitleWMobile,
                                            500, FALSE, SalamanderGeneral->GetMainWindowHWND());

    CFileInfoArray array(count, 10);

    // JR phase 2 - load all files whose attributes will be modified

    index = 0;
    for (;;)
    {
        // Retrieve data about the file being processed
        if (focused)
            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
        else
            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

        // Delete the file/directory
        // JR Call FindAllFilesInTree even for individual files
        // JR This verifies that they still exist
        if (f != NULL)
        {
            if (dlgAttr.RecurseSubDirs)
            {
                success = CRAPI::FindAllFilesInTreeWide(Path.c_str(), f->Name, array, 0, FALSE);
            }
            else
            {
                // fi.cFileName is wide now: store f->Name (already the genuine
                // wide panel-selection name) directly - no narrow round trip, no refusal.
                if (!array.AddOwned(f->Name, f->Attr, 0, 0))
                {
                    SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_MEMORYLOW).c_str(), TitleWMobileError, MSGBOX_ERROR);
                    TRACE_E("Low memory");
                    success = false;
                }
            }
        }

        // Determine whether it makes sense to continue (when no error occurred and another selected item exists)
        if (!success || focused || f == NULL)
            break;
    }

    SalamanderGeneral->DestroySafeWaitWindow();

    if (!success || array.Count == 0)
    {
        // JR The file/directory was probably deleted already
        if (array.Count == 0)
        {
            const std::wstring path = std::wstring(fsName) + L":" + Path;
            SalamanderGeneral->PostChangeOnPathNotification(path.c_str(), FALSE);
        }
        return FALSE;
    }

    BOOL pathChanged = FALSE;                      // TRUE if the path changed
    BOOL changeInSubdirs = dlgAttr.RecurseSubDirs; // TRUE if changes occurred in subdirectories as well
    skipAllErrors = FALSE;                         // skip all errors

    // JR phase 3 - apply attributes

    HWND mainWnd = parent;
    HWND parentWin;
    while ((parentWin = GetParent(mainWnd)) != NULL && IsWindowEnabled(parentWin))
        mainWnd = parentWin;
    // Disable 'mainWnd'

    BOOL showProgressDialog = array.Count > 1;
    BOOL enableMainWnd = TRUE;
    CProgressDlg delDlg(mainWnd, LangStr(IDS_ATTRIBUTES).c_str(), LangStr(IDS_CHANGING).c_str(), ooStatic); // use 'ooStatic' so the modeless dialog can live on the stack

    if (showProgressDialog)
    {
        EnableWindow(mainWnd, FALSE);
        delDlg.Create();
    }

    if (!showProgressDialog || delDlg.HWindow != NULL) // dialog opened successfully
    {
        if (showProgressDialog)
            SetForegroundWindow(delDlg.HWindow);

        int block = 0;
        int i;
        for (i = 0; success && i < array.Count; i++)
        {
            CFileInfo& fi = array[i];

            fileName = Path;
            wmobile::AppendDeviceComponent(fileName, fi.cFileName);

            if (showProgressDialog)
            {
                float progress = ((float)i / (float)array.Count);
                delDlg.Set(fileName.c_str(), (DWORD)(progress * 1000), TRUE); // delayedPaint == TRUE so we do not slow things down
            }

            if (showProgressDialog && delDlg.GetWantCancel())
            {
                success = FALSE;
                break;
            }

            BOOL skip = FALSE;
            while (1)
            {
                DWORD attr2 = fi.dwFileAttributes;

                DWORD err = 0;
                if (dlgAttr.Archive == 0)
                    attr2 &= ~FILE_ATTRIBUTE_ARCHIVE;
                if (dlgAttr.Archive == 1)
                    attr2 |= FILE_ATTRIBUTE_ARCHIVE;
                if (dlgAttr.ReadOnly == 0)
                    attr2 &= ~FILE_ATTRIBUTE_READONLY;
                if (dlgAttr.ReadOnly == 1)
                    attr2 |= FILE_ATTRIBUTE_READONLY;
                if (dlgAttr.System == 0)
                    attr2 &= ~FILE_ATTRIBUTE_SYSTEM;
                if (dlgAttr.System == 1)
                    attr2 |= FILE_ATTRIBUTE_SYSTEM;
                if (dlgAttr.Hidden == 0)
                    attr2 &= ~FILE_ATTRIBUTE_HIDDEN;
                if (dlgAttr.Hidden == 1)
                    attr2 |= FILE_ATTRIBUTE_HIDDEN;

                if (fi.dwFileAttributes != attr2)
                {
                    if (!CRAPI::SetFileAttributesWide(fileName.c_str(), attr2))
                        err = CRAPI::GetLastError();
                }

                if (err == 0 && (attr2 & FILE_ATTRIBUTE_DIRECTORY) == 0) // JR Apparently timestamps cannot be changed for directories
                {
                    err = CRAPI::SetFileTimeWide(fileName.c_str(),
                                                 dlgAttr.ChangeTimeCreated ? &dlgAttr.TimeCreated : NULL,
                                                 dlgAttr.ChangeTimeAccessed ? &dlgAttr.TimeAccessed : NULL,
                                                 dlgAttr.ChangeTimeModified ? &dlgAttr.TimeModified : NULL);
                }

                if (err == 0)
                {
                    pathChanged = TRUE;
                    break; // JR succeeded, continue
                }
                else
                {
                    if (!skipAllErrors)
                    {
                        int res = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, fileName.c_str(),
                                                                 SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(), (TitleWMobileError != NULL ? TitleWMobileError : (const wchar_t*)NULL));
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

                if (!success || skip)
                    break;
            }
        }

        if (showProgressDialog)
        {
            // Enable 'mainWnd' (otherwise Windows cannot select it as the foreground/active window)
            EnableWindow(mainWnd, TRUE);
            enableMainWnd = FALSE;

            DestroyWindow(delDlg.HWindow); // close the progress dialog
        }
    }

    // Enable 'mainWnd' (no foreground change occurred - the progress dialog never opened)
    if (showProgressDialog && enableMainWnd)
        EnableWindow(mainWnd, TRUE);

    SalamanderGeneral->RestoreFocusInSourcePanel();

    // Change on the source path Path
    if (pathChanged)
    {
        const std::wstring path = std::wstring(fsName) + L":" + Path;
        SalamanderGeneral->PostChangeOnPathNotification(path.c_str(), changeInSubdirs);
    }

    return success;
}

void WINAPI
CPluginFSInterface::ShowProperties(const wchar_t* fsName, HWND parent, int panel,
                                   int selectedFiles, int selectedDirs)
{
}

void WINAPI
CPluginFSInterface::ContextMenu(const wchar_t* fsName, HWND parent, int menuX, int menuY, int menutype,
                                int panel, int selectedFiles, int selectedDirs)
{

    HMENU menu = CreatePopupMenu();
    if (menu == NULL)
    {
        TRACE_E("CPluginFSInterface::ContextMenu: Unable to create menu.");
        return;
    }
    MENUITEMINFOW mi;
    std::wstring nameBufW;

    switch (menutype)
    {
    case fscmPathInPanel:  // context menu for the current path in the panel
    case fscmPanel:        // context menu for the panel
    case fscmItemsInPanel: // context menu for panel items (selected/focused files and directories)
    {
        // insert Salamander commands
        int i = 0;
        int index = 0;
        int salCmd;
        BOOL enabled;
        int type, lastType = sctyUnknown;
        while (SPLEnumSalamanderCommandsOwned(
            SalamanderGeneral, &index, &salCmd, nameBufW, &enabled, &type))
        {
            if ((menutype == fscmItemsInPanel && type != sctyForCurrentPath && type != sctyForConnectedDrivesAndFS ||
                 menutype == fscmPanel && (type == sctyForCurrentPath || type == sctyForConnectedDrivesAndFS)) &&
                salCmd != SALCMD_CHANGECASE &&
                salCmd != SALCMD_EMAIL && salCmd != SALCMD_EDITNEWFILE)
            {
                if (type != lastType && lastType != sctyUnknown) // insert a separator
                {
                    memset(&mi, 0, sizeof(mi));
                    mi.cbSize = sizeof(mi);
                    mi.fMask = MIIM_TYPE;
                    mi.fType = MFT_SEPARATOR;
                    InsertMenuItemW(menu, i++, TRUE, &mi);
                }
                lastType = type;

                // insert Salamander commands
                memset(&mi, 0, sizeof(mi));
                mi.cbSize = sizeof(mi);
                mi.fMask = MIIM_TYPE | MIIM_ID | MIIM_STATE;
                mi.fType = MFT_STRING;
                mi.wID = salCmd + 1000;
                mi.dwTypeData = nameBufW.data();
                mi.cch = (UINT)nameBufW.size();
                mi.fState = enabled ? MFS_ENABLED : MFS_DISABLED;
                InsertMenuItemW(menu, i++, TRUE, &mi);
            }
        }
        if (i > 0)
        {
            DWORD cmd = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                         menuX, menuY, parent, NULL);
            if (cmd >= 1000)
                SalamanderGeneral->PostSalamanderCommand(cmd - 1000);
        }
    }
    break;
    }
}

void CPluginFSInterface::EmptyCache()
{
    // Build a unique name for this FS root in the disk cache (touch all cached copies of files from this FS)
    std::wstring uniqueFileName = AssignedFSName + L":\\";
    // Disk names are case-insensitive, the disk cache is case-sensitive; converting
    // to lowercase makes the disk cache behave case-insensitively as well
    SPLToLowerCaseOwned(SalamanderGeneral, uniqueFileName);
    SalamanderGeneral->RemoveFilesFromCache(uniqueFileName.c_str());
}
