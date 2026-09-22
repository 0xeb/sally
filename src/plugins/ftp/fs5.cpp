// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <vector>

namespace
{
class CScopedWideSecretWipe
{
public:
    explicit CScopedWideSecretWipe(std::wstring& value) noexcept
        : Value(value)
    {
    }

    ~CScopedWideSecretWipe()
    {
        FTPSecureWipe(Value);
    }

private:
    std::wstring& Value;
};
} // namespace

//
// ****************************************************************************
// CPluginFSInterface
//

BOOL CPluginFSInterface::ChangeAttributes(const wchar_t* fsName, HWND parent, int panel,
                                          int selectedFiles, int selectedDirs) try
{
    CALL_STACK_MESSAGE4("CPluginFSInterface::ChangeAttributes(, , %d, %d, %d)",
                        panel, selectedFiles, selectedDirs);

    if (ControlConnection == NULL)
    {
        TRACE_E("Unexpected situation in CPluginFSInterface::ChangeAttributes(): ControlConnection == NULL!");
        return FALSE; // cancellation
    }

    // test whether the panel has only a simple listing -> in that case we cannot do anything yet
    CPluginDataInterfaceAbstract* pluginDataIface = SalamanderGeneral->GetPanelPluginData(panel);
    if (pluginDataIface != NULL && (void*)pluginDataIface == (void*)&SimpleListPluginDataInterface)
    {
        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_NEEDPARSEDLISTING).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(), MB_OK | MB_ICONINFORMATION);
        return FALSE; // cancellation
    }

    // build a description of what will be processed for the Change Attributes dialog
    std::wstring subjectSrcW;
    SPLGetCommonFSOperSourceDescrOwned(SalamanderGeneral, panel, selectedFiles,
                                       selectedDirs, NULL, FALSE, FALSE,
                                       subjectSrcW);
    std::wstring dlgSubjectSrcW;
    SPLGetCommonFSOperSourceDescrOwned(SalamanderGeneral, panel, selectedFiles,
                                       selectedDirs, NULL, FALSE, TRUE,
                                       dlgSubjectSrcW);
    const std::wstring subject = SPLFormatStringOwned(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CHANGEATTRSONFTP).c_str(),
        subjectSrcW.c_str());

    DWORD attr = -1;
    DWORD attrDiff = 0;
    BOOL displayWarning = TRUE; // warning that this is probably not a Unix server, so chmod will not work
    if (pluginDataIface != NULL && pluginDataIface != &SimpleListPluginDataInterface)
    { // we only care about data iface objects of type CFTPListingPluginDataInterface
        CFTPListingPluginDataInterface* dataIface = (CFTPListingPluginDataInterface*)pluginDataIface;
        int rightsCol = dataIface->FindRightsColumn();
        if (rightsCol != -1) // if the Rights column exists (it does not have to be Unix, that is handled later)
        {
            displayWarning = FALSE;
            const CFileData* f = NULL; // pointer to the file/directory in the panel that should be processed
            BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
            BOOL isDir = FALSE; // TRUE if 'f' is a directory
            int index = 0;
            while (1)
            {
                // fetch data about the processed file/directory
                if (focused)
                    f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
                else
                    f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

                // determine attributes of the file/directory
                if (f != NULL)
                {
                    char* rights = dataIface->GetStringFromColumn(*f, rightsCol);
                    DWORD actAttr;
                    if (GetAttrsFromUNIXRights(&actAttr, &attrDiff, rights)) // convert the string to a number
                    {
                        if (attr == -1)
                            attr = actAttr; // first file/directory
                        else
                        {
                            if (attr != actAttr) // if they differ, store the differences
                                attrDiff |= (attr ^ actAttr);
                        }
                    }
                    else // not a (normal) Unix system, give up
                    {
                        displayWarning = TRUE;
                        attr = -1;
                        attrDiff = 0;
                        break;
                    }
                }

                // determine whether it makes sense to continue (if there is another selected item)
                if (focused || f == NULL)
                    break;
            }
        }
    }

    if (!displayWarning || // optionally display a warning that this is not a UNIX server with the traditional rights model (e.g. we do not support ACL)
        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CHATTRNOTUNIXSRV).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                         MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                             MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
    {
        BOOL selDirs = selectedDirs > 0;
        if (selectedFiles == 0 && selectedDirs == 0)
            SalamanderGeneral->GetPanelFocusedItem(panel, &selDirs);
        CChangeAttrsDlg dlg(parent, subject.c_str(), attr, attrDiff, selDirs);
        if (dlg.Execute() == IDOK)
        {
            BOOL failed = TRUE; // pre-initialize the operation error
            // create the operation object
            CFTPOperation* oper = new CFTPOperation;
            if (oper != NULL)
            {
                oper->SetEncryptControlConnection(ControlConnection->GetEncryptControlConnection());
                oper->SetEncryptDataConnection(ControlConnection->GetEncryptDataConnection());
                CCertificate* cert = ControlConnection->GetCertificate();
                oper->SetCertificate(cert);
                if (cert)
                    cert->Release();
                oper->SetCompressData(ControlConnection->GetCompressData());
                if (ControlConnection->InitOperation(oper)) // initialize the connection to the server according to the "control connection"
                {
                    if (!oper->SetBasicData(dlgSubjectSrcW.c_str(), (AutodetectSrvType ? NULL : LastServerType.c_str())))
                    {
                        delete oper;
                        return FALSE;
                    }
                    std::wstring pathText;
                    if (!BuildFullPathText(fsName, Path.c_str(), pathText))
                    {
                        delete oper;
                        return FALSE;
                    }
                    CFTPServerPathType pathType = ControlConnection->GetFTPServerPathType(Path.c_str());
                    if (!oper->SetOperationChAttr(Path.c_str(), pathText.c_str(), FTPGetPathDelimiter(pathType), TRUE, dlg.IncludeSubdirs,
                                                  (WORD)dlg.AttrAndMask, (WORD)dlg.AttrOrMask,
                                                  dlg.SelFiles, dlg.SelDirs, Config.OperationsUnknownAttrs))
                    {
                        delete oper;
                        return FALSE;
                    }
                    int operUID;
                    if (FTPOperationsList.AddOperation(oper, &operUID))
                    {
                        BOOL ok = TRUE;
                        BOOL emptyQueue = FALSE;

                        // build the queue of operation items
                        CFTPQueue* queue = new CFTPQueue(ControlConnection->GetTextCodec());
                        if (queue != NULL)
                        {
                            CFTPListingPluginDataInterface* dataIface = (CFTPListingPluginDataInterface*)pluginDataIface;
                            if (dataIface != NULL && (void*)dataIface == (void*)&SimpleListPluginDataInterface)
                                dataIface = NULL; // we only care about data iface objects of type CFTPListingPluginDataInterface
                            int rightsCol = -1;   // index of the column with rights (used to detect links)
                            if (dataIface != NULL)
                                rightsCol = dataIface->FindRightsColumn();
                            const CFileData* f = NULL; // pointer to the file/directory/link in the panel that should be processed
                            BOOL isDir = FALSE;        // TRUE if 'f' is a directory
                            BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
                            int skippedItems = 0;  // number of skipped items inserted into the queue
                            int uiNeededItems = 0; // number of user-input-needed items inserted into the queue
                            int index = 0;
                            while (1)
                            {
                                // fetch data about the processed file
                                if (focused)
                                    f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
                                else
                                    f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

                                // process the file/directory/link
                                if (f != NULL)
                                {
                                    CFTPQueueItemType type;
                                    CFTPQueueItemState state;
                                    DWORD problemID;
                                    BOOL skip;
                                    CFTPQueueItem* item = CreateItemForChangeAttrsOperation(f, isDir, rightsCol, dataIface,
                                                                                            &type, &ok, &state, &problemID,
                                                                                            &skippedItems, &uiNeededItems,
                                                                                            &skip, dlg.SelFiles,
                                                                                            dlg.SelDirs, dlg.IncludeSubdirs,
                                                                                            dlg.AttrAndMask, dlg.AttrOrMask,
                                                                                            Config.OperationsUnknownAttrs);
                                    if (item != NULL)
                                    {
                                        std::string itemNameBytes;
                                        if (ok && (dataIface == NULL ||
                                                   !dataIface->GetWireName(*f, ControlConnection->GetTextCodec(), itemNameBytes)))
                                            ok = FALSE;
                                        if (ok)
                                            item->SetItem(-1, type, state, problemID, Path.c_str(), itemNameBytes.c_str());
                                        if (!ok || !queue->AddItem(item)) // add the operation to the queue
                                        {
                                            ok = FALSE;
                                            delete item;
                                        }
                                    }
                                    else
                                    {
                                        if (!skip) // only if this is not skipping the item but a low-memory error
                                        {
                                            TRACE_E(LOW_MEMORY);
                                            ok = FALSE;
                                        }
                                    }
                                }
                                // determine whether it makes sense to continue (if there is no error and another selected item exists)
                                if (!ok || focused || f == NULL)
                                    break;
                            }
                            int itemsCount = queue->GetCount();
                            emptyQueue = itemsCount == 0;
                            if (ok)
                                oper->SetChildItems(itemsCount, skippedItems, 0, uiNeededItems);
                            else
                            {
                                delete queue;
                                queue = NULL;
                            }
                        }
                        else
                        {
                            TRACE_E(LOW_MEMORY);
                            ok = FALSE;
                        }

                        if (ok) // the queue with operation items has been filled
                        {
                            if (!emptyQueue) // only if the queue of operation items is not empty
                            {
                                oper->SetQueue(queue); // set the queue of its items for the operation
                                queue = NULL;
                                if (Config.ChAttrAddToQueue)
                                    failed = FALSE; // perform the operation later -> for now the operation is successful
                                else                // perform the operation in the active "control connection"
                                {
                                    // open the operation progress window and start the operation
                                    if (RunOperation(SalamanderGeneral->GetMsgBoxParent(), operUID, oper, NULL))
                                        failed = FALSE; // operation succeeded
                                    else
                                        ok = FALSE;
                                }
                            }
                            else
                            {
                                failed = FALSE; // operation succeeded (but there is nothing to do)
                                FTPOperationsList.DeleteOperation(operUID, TRUE);
                                delete queue;
                                queue = NULL;
                            }
                        }
                        if (!ok)
                            FTPOperationsList.DeleteOperation(operUID, TRUE);
                        oper = NULL; // the operation is already added in the array, do not free it with 'delete' (see below)
                    }
                }
                if (oper != NULL)
                    delete oper;
            }
            else
                TRACE_E(LOW_MEMORY);
            return !failed; // return the operation success (TRUE = clear the selection in the panel)
        }
    }
    return FALSE; // cancellation
}
catch (...)
{
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
}

BOOL CPluginFSInterface::RunOperation(HWND parent, int operUID, CFTPOperation* oper, HWND dropTargetWnd)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::RunOperation(, %d, ,)", operUID);

    BOOL ok = TRUE;

    CFTPWorker* workerWithCon = NULL; // if we passed the connection, this points to who received it
    int i;
    for (i = 0; i < 1; i++) // FIXME: eventually we may place the initial number of operation workers into the configuration: just replace "1" with the appropriate count...
    {
        CFTPWorker* newWorker = oper->AllocNewWorker();
        if (newWorker != NULL)
        {
            if (!SocketsThread->AddSocket(newWorker) ||   // add it to the sockets thread
                !newWorker->RefreshCopiesOfUIDAndMsg() || // refresh copies of UID+Msg (they changed)
                !oper->AddWorker(newWorker))              // add it among the operation workers
            {
                DeleteSocket(newWorker);
                ok = FALSE;
                break;
            }
            else // the worker was successfully created and placed among the operation workers
            {
                if (i == 0) // hand over the current "control connection" to the first worker
                {
                    ControlConnection->GiveConnectionToWorker(newWorker, parent);
                    workerWithCon = newWorker;
                }
            }
        }
        else // error, cancel the operation
        {
            ok = FALSE;
            break;
        }
    }

    // open the operation window
    if (ok)
    {
        BOOL success;
        if (!FTPOperationsList.ActivateOperationDlg(operUID, success, dropTargetWnd) || !success)
            ok = FALSE;
    }

    // in case of an error we must stop all workers (necessary before deleting the operation)
    if (!ok)
    {
        if (workerWithCon != NULL) // optionally take back the "control connection" from the worker
            ControlConnection->GetConnectionFromWorker(workerWithCon);
        FTPOperationsList.StopWorkers(parent, operUID, -1 /* all workers */);
    }

    return ok;
}

void CPluginFSInterface::GetConnectionFromWorker(CFTPWorker* workerWithCon)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::GetConnectionFromWorker()");
    if (ControlConnection != NULL)
        ControlConnection->GetConnectionFromWorker(workerWithCon);
}

void CPluginFSInterface::ActivateWelcomeMsg()
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::ActivateWelcomeMsg()");
    if (ControlConnection != NULL)
        ControlConnection->ActivateWelcomeMsg(); // activate the welcome-msg window (the keyboard cannot do it, so the user can actually close it)
}

BOOL CPluginFSInterface::IsFTPS()
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::IsFTPS()");
    return ControlConnection != NULL && ControlConnection->GetEncryptControlConnection() == 1;
}

BOOL CPluginFSInterface::ContainsConWithUID(int controlConUID)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::ContainsConWithUID()");
    return ControlConnection != NULL ? ControlConnection->GetUID() == controlConUID : FALSE;
}

BOOL CPluginFSInterface::ContainsHost(const wchar_t* host, int port, const wchar_t* user)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::ContainsHost()");
    return host != NULL && SalamanderGeneral->StrICmp(host, Host.c_str()) == 0 && // same host (case-insensitive - Internet conventions)
           Port == port &&                                                // identical port
           user != NULL && User == user;                                  // same user name (case-sensitive - Unix accounts)
}

void CPluginFSInterface::ViewFile(const wchar_t* fsName, HWND parent,
                                  CSalamanderForViewFileOnFSAbstract* salamander,
                                  CFileData& file) try
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::ViewFile(, , ,)");

    parent = SalamanderGeneral->GetMsgBoxParent();
    if (ControlConnection == NULL)
    {
        TRACE_E("Unexpected situation in CPluginFSInterface::ViewFile(): ControlConnection == NULL!");
        return; // we are done
    }

    // test whether the panel contains only a simple listing -> in that case we cannot do anything yet
    CPluginDataInterfaceAbstract* pluginDataIface = SalamanderGeneral->GetPanelPluginData(PANEL_SOURCE); // we are sure the FS is in the source panel
    if (pluginDataIface != NULL && (void*)pluginDataIface == (void*)&SimpleListPluginDataInterface)
    {
        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_NEEDPARSEDLISTING).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(), MB_OK | MB_ICONINFORMATION);
        return; // we are done
    }

    CFTPListingPluginDataInterface* dataIface = (CFTPListingPluginDataInterface*)pluginDataIface;
    if (dataIface != NULL && (void*)dataIface == (void*)&SimpleListPluginDataInterface)
        dataIface = NULL;
    std::string fileNameBytes;
    if (dataIface == NULL ||
        !dataIface->GetWireName(file, ControlConnection->GetTextCodec(), fileNameBytes))
        return;
    const char* fileNameA = fileNameBytes.c_str();

    BOOL doNotCacheDownload = !ControlConnection->GetUseListingsCache(); // FALSE = cache it; TRUE = cache only for this open viewer (the next View will download the file again)

    // build a unique file name for the disk cache (standard Salamander path format)
    const std::wstring& canonicalFSName =
        SalamanderGeneral->StrICmp(fsName, AssignedFSNameFTPS.c_str()) == 0
            ? AssignedFSNameFTPS
            : AssignedFSName;
    std::wstring uniqueFileName = canonicalFSName;
    uniqueFileName += L':';
    size_t uniquePrefixLen = uniqueFileName.length();
    std::string fullNameBytes;
    BOOL fullNameOK = FALSE;
    if (!doNotCacheDownload)
    {
        if (FtpStoreProtocolBytes(Path, fullNameBytes) &&
            FTPPathAppend(GetFTPServerPathType(Path.c_str()), fullNameBytes, fileNameA, FALSE))
        {
            fullNameOK = BuildFullPathText(canonicalFSName.c_str(), fullNameBytes.c_str(), uniqueFileName);
        }
    }
    if (!fullNameOK)
        doNotCacheDownload = TRUE;

    // obtain the name of the file copy in the disk cache
    BOOL fileExists;
    const wchar_t* tmpFileName;
    std::wstring nameInCache(file.Name);
    if (GetFTPServerPathType(Path.c_str()) == ftpsptOpenVMS)
    {
        FTPVMSCutFileVersion(nameInCache.data(), -1);
        nameInCache.resize(wcslen(nameInCache.c_str()));
    }
    SPLSalMakeValidFileNameComponentOwned(SalamanderGeneral, nameInCache);
    while (1)
    {
        if (doNotCacheDownload)
        {
            std::wstring tickName;
            try
            {
                tickName = SPLFormatStringOwned(L"%08X", GetTickCount());
            }
            catch (...)
            {
                return;
            }
            uniqueFileName.resize(uniquePrefixLen);
            uniqueFileName += tickName;
        }
        tmpFileName = salamander->AllocFileNameInCache(parent, uniqueFileName.c_str(), nameInCache.c_str(), NULL, fileExists);
        if (tmpFileName == NULL)
            return; // fatal error
        if (!doNotCacheDownload || !fileExists)
            break;

        // no caching + the file already exists (unlikely, but handled anyway) - we must change uniqueFileName
        Sleep(20);
        salamander->FreeFileNameInCache(uniqueFileName.c_str(), fileExists, FALSE, CQuadWord(0, 0), NULL, FALSE, TRUE);
    }

    std::string logBuf;
    if (FTPFormatString(logBuf, LoadStr(fileExists ? IDS_LOGMSGVIEWCACHEDFILE : IDS_LOGMSGVIEWFILE), fileNameA))
        ControlConnection->LogMessage(logBuf.c_str(), -1, TRUE);

    // determine whether a copy of the file needs to be prepared in the disk cache (download)
    BOOL newFileCreated = FALSE;
    BOOL newFileIncomplete = FALSE;
    CQuadWord newFileSize(0, 0);
    if (!fileExists) // preparing a copy of the file (download) is required
    {
        TotalConnectAttemptNum = 1; // start of a user-requested action -> if reconnecting is needed, this is the first reconnect attempt
        int panel;
        BOOL notInPanel = !SalamanderGeneral->GetPanelWithPluginFS(this, panel);

        BOOL asciiMode = FALSE;
        const wchar_t *name, *ext; // helper variables for auto-detect-transfer-mode
        std::wstring basicNameStorage;
        if (TransferMode == trmAutodetect)
        {
            if (dataIface == NULL || !dataIface->GetBasicName(file, &name, &ext, basicNameStorage))
            {
                name = file.Name;
                ext = file.Ext;
            }
            int dummy;
            if (Config.ASCIIFileMasks->PrepareMasks(dummy))
                asciiMode = Config.ASCIIFileMasks->AgreeMasks(name, ext);
            else
            {
                TRACE_E("Unexpected situation in CPluginFSInterface::ViewFile(): Config.ASCIIFileMasks->PrepareMasks() failed!");
                asciiMode = FALSE; // the binary mode is still the lesser evil
            }
        }
        else
            asciiMode = TransferMode == trmASCII;

        CQuadWord fileSizeInBytes;
        BOOL sizeInBytes;
        if (dataIface == NULL || !dataIface->GetSize(file, fileSizeInBytes, sizeInBytes) || !sizeInBytes)
            fileSizeInBytes.Set(-1, -1); // the file size is unknown

        ControlConnection->DownloadOneFile(parent, fileNameA, fileSizeInBytes, asciiMode, Path.c_str(),
                                           tmpFileName, &newFileCreated, &newFileIncomplete, &newFileSize,
                                           &TotalConnectAttemptNum, panel, notInPanel,
                                            User);
    }

    // open the viewer
    HANDLE fileLock;
    BOOL fileLockOwner;
    if (!fileExists && !newFileCreated || // open the viewer only if the copy of the file is fine
        !salamander->OpenViewer(parent, tmpFileName, &fileLock, &fileLockOwner))
    { // on error reset the "lock"
        fileLock = NULL;
        fileLockOwner = FALSE;
    }

    // we still have to call FreeFileNameInCache as a pair to AllocFileNameInCache (link
    // the viewer and the disk cache)
    salamander->FreeFileNameInCache(uniqueFileName.c_str(), fileExists, newFileCreated,
                                    newFileSize, fileLock, fileLockOwner,
                                    doNotCacheDownload || newFileIncomplete);
}
catch (...)
{
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
}

BOOL CPluginFSInterface::CreateDir(const wchar_t* fsName, int mode, HWND parent,
                                   CSalamanderStringBuffer* newName, BOOL& cancel)
{
    try
    {
        std::wstring value;
        if (newName == NULL || !sally::plugin_abi::ReadStringBuffer(*newName, value))
            return FALSE;
        const BOOL result = CreateDirOwned(fsName, mode, parent, value, cancel);
        return sally::plugin_abi::WriteStringBuffer(*newName, value) ? result : FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL CPluginFSInterface::CreateDirOwned(const wchar_t* fsName, int mode, HWND parent,
                                        std::wstring& newName, BOOL& cancel)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::CreateDir(, %d, , ,)", mode);

    parent = SalamanderGeneral->GetMsgBoxParent();
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // let the standard dialog open

    if (mode == 2) // a name came from the standard dialog in 'newName'
    {
        if (ControlConnection == NULL)
            TRACE_E("Unexpected situation in CPluginFSInterface::CreateDir(): ControlConnection == NULL!");
        else
        {
            std::string encodedName;
            if (!ControlConnection->EncodeText(newName.c_str(), encodedName))
            {
                SalamanderGeneral->SalMessageBox(parent,
                                                 SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_DIRNAME_CANNOTENCODE).c_str(),
                                                 SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                 MB_OK | MB_ICONEXCLAMATION);
                return FALSE;
            }

            std::string logBuf;
            if (FTPFormatString(logBuf, LoadStr(IDS_LOGMSGCREATEDIR), encodedName.c_str()))
                ControlConnection->LogMessage(logBuf.c_str(), -1, TRUE);

            TotalConnectAttemptNum = 1; // start of a user-requested action -> if reconnecting is needed, this is the first reconnect attempt
            int panel;
            BOOL notInPanel = !SalamanderGeneral->GetPanelWithPluginFS(this, panel);
            std::string changedPath;
            BOOL res = ControlConnection->CreateDir(changedPath, parent, encodedName, Path.c_str(),
                                                    &TotalConnectAttemptNum, panel, notInPanel,
                                                    User);
            std::wstring returnedName;
            if (ControlConnection->DecodeText(encodedName.data(), encodedName.size(), returnedName))
                newName = returnedName;
            if (!changedPath.empty())
            {
                std::wstring postChangedPath;
                if (BuildFullPathText(fsName, changedPath.c_str(), postChangedPath))
                    SalamanderGeneral->PostChangeOnPathNotification(postChangedPath.c_str(), TRUE | 0x02 /* soft refresh */);
            }
            if (res)
                return TRUE; // success, the next refresh will focus on 'newName'
            else
                return FALSE; // returns the incorrect directory name in 'newName' (the standard dialog opens again)
        }
    }
    cancel = TRUE;
    return FALSE; // cancel
}

BOOL CPluginFSInterface::QuickRename(const wchar_t* fsName, int mode, HWND parent, CFileData& file,
                                     BOOL isDir, CSalamanderStringBuffer* newName, BOOL& cancel)
{
    try
    {
        std::wstring value;
        if (newName == NULL || !sally::plugin_abi::ReadStringBuffer(*newName, value))
            return FALSE;
        const BOOL result = QuickRenameOwned(fsName, mode, parent, file, isDir, value, cancel);
        return sally::plugin_abi::WriteStringBuffer(*newName, value) ? result : FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL CPluginFSInterface::QuickRenameOwned(const wchar_t* fsName, int mode, HWND parent,
                                          CFileData& file, BOOL isDir,
                                          std::wstring& newName, BOOL& cancel)
{
    CALL_STACK_MESSAGE3("CPluginFSInterface::QuickRename(, %d, , , %d, ,)", mode, isDir);

    parent = SalamanderGeneral->GetMsgBoxParent();
    cancel = FALSE;
    if (mode == 1)
        return FALSE; // let the standard dialog open

    if (mode == 2) // a name came from the standard dialog in 'newName'
    {
        if (ControlConnection == NULL)
            TRACE_E("Unexpected situation in CPluginFSInterface::QuickRename(): ControlConnection == NULL!");
        else
        {
            int renameAction = 1; // 1 = rename, 2 = do not rename and return the name for editing, 3 = cancel
            CFTPServerPathType pathType = ControlConnection->GetFTPServerPathType(Path.c_str());
            BOOL isVMS = pathType == ftpsptOpenVMS; // determine whether this might be a VMS listing

            // process the mask in newName (skip if it is not a mask (contains neither '*' nor '?') - so that renaming to "test^." works)
            if (newName.find_first_of(L"*?") != std::wstring::npos)
            {
                std::wstring masked;
                if (!SPLMaskNameOwned(SalamanderGeneral, file.Name,
                                      newName.c_str(), masked))
                    return FALSE;
                newName = std::move(masked);
            }

            if (!Config.AlwaysOverwrite)
            {
                BOOL tgtFileExists = FALSE; // the rename would overwrite an existing file
                BOOL tgtDirExists = FALSE;  // the rename would most likely fail, because a directory with that name already exists

                BOOL caseSensitive = FTPIsCaseSensitive(pathType);

                // find which parser handled the listing
                CServerTypeList* serverTypeList = Config.LockServerTypeList();
                int serverTypeListCount = serverTypeList->Count;
                BOOL err = TRUE; // TRUE = we cannot determine whether the target file will be overwritten
                int i;
                for (i = 0; i < serverTypeListCount; i++)
                {
                    CServerType* serverType = serverTypeList->At(i);
                    const char* s = serverType->TypeName;
                    if (*s == '*')
                        s++;
                    const CFtpTextCompareStatus comparison = FtpCompareLocalTextNoCase(LastServerType, s);
                    if (comparison == CFtpTextCompareStatus::Failure)
                        break;
                    if (comparison == CFtpTextCompareStatus::Equal)
                    {
                        // we found the serverType successfully used for listing, now parse the listing
                        if (!ParseListing(NULL, NULL, serverType, &err, isVMS, newName.c_str(), caseSensitive,
                                          &tgtFileExists, &tgtDirExists))
                            err = TRUE;
                        break;
                    }
                }
                Config.UnlockServerTypeList();

                if (tgtFileExists || tgtDirExists || err)
                {
                    int res = SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, tgtFileExists ? (!isDir ? IDS_RENAME_FILEEXISTS : IDS_RENAME_FILEEXISTS2) : tgtDirExists ? IDS_RENAME_DIREXISTS
                                                                                                                                                                                   : IDS_RENAME_UNABLETOGETLIST).c_str(),
                                                               SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                               MB_YESNOCANCEL | (tgtFileExists && !isDir ? 0 : MB_DEFBUTTON2) | MB_ICONQUESTION);

                    if (res == IDNO)
                        renameAction = 2;
                    else
                    {
                        if (res == IDCANCEL)
                            renameAction = 3;
                    }
                }
            }

            if (renameAction == 1) // rename
            {
                CPluginDataInterfaceAbstract* pluginData = SalamanderGeneral->GetPanelPluginData(PANEL_SOURCE);
                CFTPListingPluginDataInterface* dataIface = (CFTPListingPluginDataInterface*)pluginData;
                if (dataIface != NULL && (void*)dataIface == (void*)&SimpleListPluginDataInterface)
                    dataIface = NULL;
                std::string fromNameBytes;
                std::string encodedNewName;
                if (dataIface == NULL || !dataIface->GetWireName(file, ControlConnection->GetTextCodec(), fromNameBytes))
                    return FALSE;
                if (!ControlConnection->EncodeText(newName.c_str(), encodedNewName))
                {
                    SalamanderGeneral->SalMessageBox(parent,
                                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_RENAMENAME_CANNOTENCODE).c_str(),
                                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                     MB_OK | MB_ICONEXCLAMATION);
                    return FALSE;
                }
                const char* fromNameA = fromNameBytes.c_str();

                std::string logBuf;
                if (FTPFormatString(logBuf, LoadStr(IDS_LOGMSGQUICKRENAME), fromNameA, encodedNewName.c_str()))
                    ControlConnection->LogMessage(logBuf.c_str(), -1, TRUE);

                TotalConnectAttemptNum = 1; // start of a user-requested action -> if reconnecting is needed, this is the first reconnect attempt
                int panel;
                BOOL notInPanel = !SalamanderGeneral->GetPanelWithPluginFS(this, panel);
                std::string changedPath;

                BOOL res = ControlConnection->QuickRename(changedPath, parent, fromNameA, encodedNewName, Path.c_str(),
                                                          &TotalConnectAttemptNum, panel, notInPanel,
                                                          User, isVMS, isDir);
                std::wstring returnedName;
                if (ControlConnection->DecodeText(encodedNewName.data(), encodedNewName.size(), returnedName))
                    newName = returnedName;
                if (!changedPath.empty())
                {
                    std::wstring postChangedPath;
                    if (BuildFullPathText(fsName, changedPath.c_str(), postChangedPath))
                        SalamanderGeneral->PostChangeOnPathNotification(postChangedPath.c_str(), TRUE | 0x02 /* soft refresh */);
                }
                if (res)
                    return TRUE; // success, the next refresh will focus on 'newName'
                else
                    return FALSE; // returns an incorrect name in 'newName' (the standard dialog opens again)
            }
            else
            {
                if (renameAction == 2)
                    return FALSE; // do not rename and return the existing name in 'newName' (the standard dialog opens again)
                else              // cancel
                {
                    cancel = TRUE;
                    return FALSE;
                }
            }
        }
    }
    cancel = TRUE;
    return FALSE; // cancel
}

CFTPQueueItem* CreateItemForCopyOrMoveUploadOperation(const wchar_t* name, BOOL isDir, const CQuadWord* size,
                                                      CFTPQueueItemType* type, int transferMode,
                                                      CFTPOperation* oper, BOOL copy, const char* targetPath,
                                                      const char* targetName, CQuadWord* totalSize,
                                                      BOOL isVMS)
{
    CFTPQueueItem* item = NULL;
    *type = fqitNone;
    if (isDir) // directory
    {
        *type = copy ? fqitUploadCopyExploreDir : fqitUploadMoveExploreDir;
        item = new CFTPQueueItemCopyMoveUploadExplore;
        if (item != NULL)
        {
            ((CFTPQueueItemCopyMoveUploadExplore*)item)->SetItemCopyMoveUploadExplore(targetPath, targetName, UPLOADTGTDIRSTATE_UNKNOWN);
        }
    }
    else // file
    {
        BOOL asciiTransferMode;
        if (transferMode == trmAutodetect)
        {
            std::wstring basicName = name;
            if (isVMS) // on VMS we must have the name trimmed to the base (the version number would break mask comparison)
                FTPVMSCutFileVersion(basicName.data(), -1);

            const wchar_t* ext = wcsrchr(basicName.c_str(), L'.');
            //      if (ext == NULL || ext == basicName.c_str()) ext = basicName.c_str() + basicName.length();   // ".cvspass" is a file extension in Windows ...
            if (ext == NULL)
                ext = basicName.c_str() + basicName.length();
            else
                ext++;
            asciiTransferMode = oper->IsASCIIFile(basicName.c_str(), ext);
        }
        else
            asciiTransferMode = transferMode == trmASCII;

        *type = copy ? fqitUploadCopyFile : fqitUploadMoveFile;
        item = new CFTPQueueItemCopyOrMoveUpload;
        *totalSize += *size;
        if (item != NULL)
        {
            ((CFTPQueueItemCopyOrMoveUpload*)item)->SetItemCopyOrMoveUpload(targetPath, targetName, *size, asciiTransferMode, UPLOADTGTFILESTATE_UNKNOWN);
        }
    }
    return item;
}

BOOL CPluginFSInterface::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                                const wchar_t* sourcePath, SalEnumSelection2 next,
                                                void* nextParam, int sourceFiles, int sourceDirs,
                                                CSalamanderStringBuffer* targetPath, BOOL* invalidPathOrCancel)
{
    try
    {
        std::wstring path;
        if (targetPath == NULL ||
            !sally::plugin_abi::ReadStringBuffer(*targetPath, path))
            return FALSE;
        const BOOL result = CopyOrMoveFromDiskToFSOwned(
            copy, mode, fsName, parent, sourcePath, next, nextParam,
            sourceFiles, sourceDirs, path, invalidPathOrCancel);
        return sally::plugin_abi::WriteStringBuffer(*targetPath, path) ? result : FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL CPluginFSInterface::CopyOrMoveFromDiskToFSOwned(
    BOOL copy, int mode, const wchar_t* fsName, HWND parent,
    const wchar_t* sourcePath, SalEnumSelection2 next, void* nextParam,
    int sourceFiles, int sourceDirs, std::wstring& targetPath,
    BOOL* invalidPathOrCancel)
{
    if (invalidPathOrCancel != NULL)
        *invalidPathOrCancel = TRUE;

    if (mode == 1)
    {
        // find out whether an operation that could damage the current listing in the panel is running
        // (we perform this test again right before using the listing for the upload-listing-cache, but
        // if such an operation finished before this second test, we would not detect the damage);
        // this does not solve the case when such an operation finishes during drag&drop (while dragging
        // the mouse) from disk to the panel (it is a relatively short time, so we simply ignore it)
        CFTPServerPathType pathType = GetFTPServerPathType(Path.c_str());
        if (!PathListingMayBeOutdated && FTPOperationsList.CanMakeChangesOnPath(User.c_str(), Host.c_str(), Port, Path.c_str(), pathType, -1))
            PathListingMayBeOutdated = TRUE;

        // add the *.* or * mask to the target path (we will process operation masks)
        std::string targetPathBytes;
        if (!FtpEncodeLocalText(targetPath.c_str(), targetPathBytes))
            return TRUE;
        std::vector<char> targetPathStorage(targetPathBytes.size() + 8, '\0');
        memcpy(targetPathStorage.data(), targetPathBytes.c_str(),
               targetPathBytes.size() + 1);
        FTPAddOperationMask(pathType, targetPathStorage.data(),
                            static_cast<int>(targetPathStorage.size()),
                            sourceFiles == 0);
        std::wstring targetPathText;
        if (!FtpDecodeLocalText(targetPathStorage.data(), targetPathText))
            return TRUE;
        targetPath = std::move(targetPathText);
        return TRUE;
    }

    if (mode == 2 || mode == 3)
    {
        // 'targetPath' contains the raw path entered by the user (the only thing we know about it
        // is that it points to the FTP, otherwise Salamander would not call this method)
        int isFTPS = targetPath.size() > AssignedFSNameFTPS.size() &&
                     SalamanderGeneral->StrNICmp(targetPath.c_str(), AssignedFSNameFTPS.c_str(), (int)AssignedFSNameFTPS.size()) == 0 &&
                     targetPath[AssignedFSNameFTPS.size()] == L':';

        const wchar_t* userPartW = wcschr(targetPath.c_str(), L':');
        if (userPartW == NULL)
            return FALSE;
        userPartW++;

        // verify whether it will be possible to decrypt a potential password for the default proxy (we may enter SetConnectionParameters() only if it is possible)
        if (!Config.FTPProxyServerList.EnsurePasswordCanBeDecrypted(SalamanderGeneral->GetMsgBoxParent(), Config.DefaultProxySrvUID))
        {
            return FALSE; // fatal error
        }

        std::wstring newUserPartText;
        if (!FtpStoreWideText(userPartW, newUserPartText))
        {
            return FALSE;
        }
        CScopedWideSecretWipe userPartWipe(newUserPartText);
        wchar_t *u, *host, *p, *path, *password;
        wchar_t firstCharOfPath = L'/';
        const int userLength = ControlConnection != NULL ? FTPGetUserLengthW(User.c_str()) : 0;
        FTPSplitPathW(newUserPartText.data(), &u, &password, &host, &p, &path,
                      &firstCharOfPath, userLength);
        if (password != NULL && *password == 0)
            password = NULL;
        std::wstring parsedUser;
        if (u == NULL || *u == 0)
        {
            if (!FtpStoreWideText(L"anonymous", parsedUser))
                return FALSE;
        }
        else if (!FtpStoreWideText(u, parsedUser))
        {
            FTPSecureWipe(newUserPartText);
            return FALSE;
        }
        std::wstring parsedHost;
        if (host == NULL || *host == 0 || !FtpStoreWideText(host, parsedHost))
        {
            SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_HOSTNAMEMISSING).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MSGBOX_ERROR);
            FTPSecureWipe(newUserPartText);
            return FALSE;
        }
        int port = IPPORT_FTP;
        if (p != NULL && *p != 0)
            port = _wtoi(p);

        if (ControlConnection == NULL) // open the connection (open the path on the FTP server)
        {
            TotalConnectAttemptNum = 1; // opening the connection = first attempt to open the connection

            ControlConnection = new CControlConnectionSocket;
            if (ControlConnection == NULL || !ControlConnection->IsGood())
            {
                if (ControlConnection != NULL) // insufficient system resources for allocating the object
                {
                    DeleteSocket(ControlConnection);
                    ControlConnection = NULL;
                }
                else
                    TRACE_E(LOW_MEMORY);
                FTPSecureWipe(newUserPartText); // wipe the memory where the password appeared
                return TRUE;                                   // fatal error
            }

            AutodetectSrvType = TRUE; // use automatic detection of the server type
            LastServerType.clear();

            std::wstring passwordW;
            const BOOL havePassword = parsedUser == L"anonymous" && password == NULL ?
                                          Config.GetAnonymousPasswd(passwordW) :
                                          FtpStoreWideText(password != NULL ? password : L"", passwordW);
            if (!havePassword ||
                !ControlConnection->SetConnectionParameters(parsedHost.c_str(), port, parsedUser.c_str(), passwordW.c_str(),
                                                       Config.UseListingsCache, NULL, Config.PassiveMode,
                                                       NULL, Config.KeepAlive, Config.KeepAliveSendEvery,
                                                       Config.KeepAliveStopAfter, Config.KeepAliveCommand,
                                                       -2 /* default proxy server */,
                                                       isFTPS, isFTPS, Config.CompressData))
            {
                FTPSecureWipe(passwordW);
                DeleteSocket(ControlConnection);
                ControlConnection = NULL;
                return FALSE;
            }
            FTPSecureWipe(passwordW);
            Host.swap(parsedHost);
            Port = port;
            User.swap(parsedUser);
            Path.clear();
            TransferMode = Config.TransferMode;

            // connect to the server
            ControlConnection->SetStartTime();
            if (!ControlConnection->StartControlConnection(SalamanderGeneral->GetMsgBoxParent(),
                                                           User, FALSE, &RescuePath,
                                                           &TotalConnectAttemptNum,
                                                           NULL, FALSE, -1, FALSE))
            { // connection failed, release the socket object (signals the "never connected" state)
                DeleteSocket(ControlConnection);
                ControlConnection = NULL;
                Logs.RefreshListOfLogsInLogsDlg();
                FTPSecureWipe(newUserPartText); // wipe the memory where the password appeared
                return TRUE;                                   // cancel
            }
            if (!FtpStoreProtocolBytes(RescuePath, HomeDir))
                return FALSE; // store the current path after logging in to the server (home dir)
        }
        else // verify whether the target path is on the server opened in this FS
        {
            if (isFTPS != ControlConnection->GetEncryptControlConnection() ||  // should be FTPS or not, but the state differs
                parsedUser != User ||                                          // different user name (case-sensitive - Unix accounts)
                SalamanderGeneral->StrICmp(parsedHost.c_str(), Host.c_str()) != 0 || // different host (case-insensitive - Internet conventions - maybe test IP addresses later)
                port != Port)                                                  // different port
            {
                if (invalidPathOrCancel != NULL)
                    *invalidPathOrCancel = FALSE;
                FTPSecureWipe(newUserPartText); // wipe the memory where the password appeared
                return FALSE;                                  // need to find another FS
            }
            ControlConnection->SetStartTime();
        }

        std::string tgtPath;
        std::string mask = "*";
        if (path != NULL)
        {
            BOOL isSpecRootPath = FALSE;
            std::wstring tgtPathText;
            try
            {
                tgtPathText.push_back(firstCharOfPath);
                tgtPathText.append(path);
            }
            catch (...)
            {
                FTPSecureWipe(newUserPartText);
                return FALSE;
            }
            if (!ControlConnection->EncodeText(tgtPathText.c_str(), tgtPath))
            {
                FTPSecureWipe(newUserPartText);
                return FALSE;
            }
            FTPSecureWipe(newUserPartText); // wipe the memory where the password appeared

            // determine the path type and optionally skip '/' or '\\' at the beginning of the path (after the host name)
            CFTPServerPathType pathType = ftpsptEmpty;
            if (HomeDir.empty() || HomeDir[0] != '/' && HomeDir[0] != '\\')
            { // we try skipping '/' or '\\' at the beginning of the path only if the server home dir does not start with them (the PWD result after login)
                pathType = GetFTPServerPathType(tgtPath.c_str() + 1);
                if (pathType == ftpsptOpenVMS || pathType == ftpsptMVS || pathType == ftpsptIBMz_VM ||
                    pathType == ftpsptOS2 && GetFTPServerPathType("") == ftpsptOS2) // OS/2 paths clash with the Unix path "/C:/path", so we distinguish OS/2 paths even just by the SYST reply
                {                                                                   // VMS + MVS + IBM_z/VM + OS/2 do not have '/' or '\\' at the beginning of the path
                    tgtPath.erase(0, 1);                                            // remove the '/' or '\\' character from the start of the path
                    if (tgtPath.empty())                                            // generic root -> fill in according to the system type
                    {
                        isSpecRootPath = TRUE;
                        if (pathType == ftpsptOpenVMS)
                            tgtPath = "[000000]";
                        else
                        {
                            if (pathType == ftpsptMVS)
                                tgtPath = "''";
                            else
                            {
                                if (pathType == ftpsptIBMz_VM)
                                {
                                    if (HomeDir.empty() || !FTPGetIBMz_VMRootPath(tgtPath, HomeDir.c_str()))
                                    {
                                        tgtPath = "/"; // tested server supported the Unix root "/", someone might report otherwise and we will handle it later...
                                    }
                                }
                                else
                                {
                                    if (pathType == ftpsptOS2)
                                    {
                                        if (HomeDir.empty() || !FTPGetOS2RootPath(tgtPath, HomeDir.c_str()))
                                        {
                                            tgtPath = "/"; // try at least the Unix root "/", we cannot do anything else, someone might report otherwise and we will handle it later...
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else
                    pathType = GetFTPServerPathType(tgtPath.c_str());
            }
            else
                pathType = GetFTPServerPathType(tgtPath.c_str());

            if (pathType == ftpsptEmpty || pathType == ftpsptUnknown)
            {
                SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_INVALIDPATH).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MSGBOX_ERROR);
                return FALSE; // invalid path
            }

            // if this is a root (only specific cases, other types of root paths continue further), no more adjustments
            // nor path analysis make sense - use the path as is plus the "*" mask
            if (!isSpecRootPath)
            {
                // if the path ends with a separator, treat it as a path without a mask (e.g. "/pub/dir/" or
                // "PUB$DEVICE:[PUB.VMS.]"); otherwise continue with path analysis
                if (!FTPPathEndsWithDelimiter(pathType, tgtPath.c_str()))
                {
                    std::string cutTgtPath = tgtPath;
                    std::string cutMask;
                    BOOL cutMaybeFileName = FALSE;
                    if (FTPCutDirectory(pathType, cutTgtPath, &cutMask, &cutMaybeFileName))
                    { // if a part of the path can be trimmed, we will determine whether it is a mask (otherwise it is probably a root path, use the "*" mask)
                        std::string cutTgtPathIBMz_VM;
                        std::string cutMaskIBMz_VM;
                        BOOL done = FALSE;
                        if (pathType == ftpsptIBMz_VM)
                        {
                            cutTgtPathIBMz_VM = tgtPath;
                            if (FTPIBMz_VmCutTwoDirectories(cutTgtPathIBMz_VM, cutMaskIBMz_VM))
                            {
                                const char* sep = strchr(cutMaskIBMz_VM.c_str(), '.');
                                const char* ast = strchr(cutMaskIBMz_VM.c_str(), '*');
                                const char* exc = strchr(cutMaskIBMz_VM.c_str(), '?');
                                if (ast != NULL && ast < sep || exc != NULL && exc < sep)
                                { // the trimmed part contains '*' or '?' (wildcards) before '.' (definitely a file mask such as "*.*")
                                    tgtPath = cutTgtPathIBMz_VM;
                                    mask = cutMaskIBMz_VM;
                                    done = TRUE;
                                }
                            }
                            else
                            {
                                cutTgtPathIBMz_VM.clear();
                                cutMaskIBMz_VM.clear();
                            }
                        }
                        if (!done)
                        {
                            if (cutTgtPathIBMz_VM.empty() && // we need to test whether 'cutMaskIBMz_VM' contains a mask
                                    (strchr(cutMask.c_str(), '*') != NULL || strchr(cutMask.c_str(), '?') != NULL) ||
                                pathType == ftpsptOpenVMS && cutMaybeFileName)
                            { // the trimmed part contains '*' or '?' (wildcards) or it is a VMS file name (must be a mask, the target path is the path to that file)
                                tgtPath = cutTgtPath;
                                mask = cutMask;
                            }
                            else
                            {
                                TotalConnectAttemptNum = 1; // start of a user-requested action -> if reconnecting is needed, this is the first reconnect attempt
                                int panel;
                                BOOL notInPanel = !SalamanderGeneral->GetPanelWithPluginFS(this, panel);
                                BOOL success = FALSE;
                                std::string replyBuf;
                                if (strchr(cutMask.c_str(), '*') != NULL || strchr(cutMask.c_str(), '?') != NULL ||
                                    ControlConnection->SendChangeWorkingPath(notInPanel, panel == PANEL_LEFT,
                                                                             SalamanderGeneral->GetMsgBoxParent(),
                                                                              tgtPath.c_str(), User,
                                                                             &success, replyBuf, NULL, &TotalConnectAttemptNum,
                                                                             NULL, FALSE, NULL))
                                {
                                    if (!success) // if 'tgtPath' is a valid path, the mask is "*"; otherwise continue
                                    {
                                        if (ControlConnection->SendChangeWorkingPath(notInPanel, panel == PANEL_LEFT,
                                                                                     SalamanderGeneral->GetMsgBoxParent(),
                                                                                     cutTgtPath.c_str(), User,
                                                                                     &success, replyBuf, NULL, &TotalConnectAttemptNum,
                                                                                     NULL, FALSE, NULL))
                                        {
                                            if (success) // 'cutTgtPath' is a valid path - the mask is 'cutMask'
                                            {
                                                tgtPath = cutTgtPath;
                                                mask = cutMask;
                                            }
                                            else // otherwise continue
                                            {
                                                if (!cutTgtPathIBMz_VM.empty())
                                                {
                                                    if (ControlConnection->SendChangeWorkingPath(notInPanel, panel == PANEL_LEFT,
                                                                                                 SalamanderGeneral->GetMsgBoxParent(),
                                                                                                 cutTgtPathIBMz_VM.c_str(), User,
                                                                                                 &success, replyBuf, NULL, &TotalConnectAttemptNum,
                                                                                                 NULL, FALSE, NULL))
                                                    {
                                                        if (success) // 'cutTgtPathIBMz_VM' is a valid path - the mask is 'cutMaskIBMz_VM'
                                                        {
                                                            tgtPath = cutTgtPathIBMz_VM;
                                                            mask = cutMaskIBMz_VM;
                                                            done = TRUE;
                                                        }
                                                    }
                                                    else // connection cannot be established (even if the user does not want to reconnect)
                                                    {
                                                        return TRUE; // cancel
                                                    }
                                                }
                                                if (!done) // show the path error to the user
                                                {
                                                    const char* failedPath = !cutTgtPathIBMz_VM.empty() ? cutTgtPathIBMz_VM.c_str() : cutTgtPath.c_str();
                                                    std::wstring errorText;
                                                    if (!FtpFormatServerReplyMessage(ControlConnection->GetTextCodec(),
                                                                                     LangStr(IDS_CHANGEWORKPATHERROR).c_str(),
                                                                                     std::string_view(failedPath), replyBuf,
                                                                                     errorText))
                                                        errorText = LangStr(IDS_OPERDOPPR_LOWMEM);
                                                    SalamanderGeneral->ShowMessageBox(errorText.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MSGBOX_ERROR);
                                                    return FALSE; // invalid path
                                                }
                                            }
                                        }
                                        else // connection cannot be established (even if the user does not want to reconnect)
                                        {
                                            return TRUE; // cancel
                                        }
                                    }
                                }
                                else // connection cannot be established (even if the user does not want to reconnect)
                                {
                                    return TRUE; // cancel
                                }
                            }
                        }
                    }
                }
            }
        }
        else // the target path is the home dir
        {
            FTPSecureWipe(newUserPartText); // wipe the memory where the password appeared
            if (HomeDir.empty())                           // home dir is not defined (some servers require calling CWD first before PWD returns anything)
            {
                SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_HOMEDIRNOTDEFINED).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MSGBOX_ERROR);
                return FALSE; // invalid path
            }
            if (!FtpStoreProtocolBytes(HomeDir, tgtPath))
                return FALSE;
        }

        // moving/copying multiple files/directories into one name (they would overwrite each other) is probably nonsense
        if (sourceFiles + sourceDirs > 1 && strchr(mask.c_str(), '*') == NULL && strchr(mask.c_str(), '?') == NULL)
        {
            if (SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_COPYMOVE_NONSENSE).c_str(),
                                                 SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                 MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES)
            {
                return FALSE; // invalid path
            }
        }

        // the path is analyzed, start the operation:
        // 'tgtPath' is the target path, 'mask' is the operation mask
        BOOL success = FALSE; // pre-initialize cancel/error state of the operation

        std::wstring dlgSubjectSrcW;
        if (sourceFiles + sourceDirs <= 1) // one selected item
        {
            BOOL isDir;
            const wchar_t* name = next(parent, 0, NULL, &isDir, NULL, NULL, NULL, nextParam, NULL);
            if (name != NULL)
            {
                SPLGetCommonFSOperSourceDescrOwned(
                    SalamanderGeneral, -1, sourceFiles, sourceDirs, name,
                    isDir, TRUE, dlgSubjectSrcW);
            }
            else
            {
                TRACE_E("Unexpected situation in CPluginFSInterface::CopyOrMoveFromDiskToFS()!");
                dlgSubjectSrcW.clear();
            }
            next(NULL, -1, NULL, NULL, NULL, NULL, NULL, nextParam, NULL); // reset enumeration
        }
        else // several directories and files
        {
            SPLGetCommonFSOperSourceDescrOwned(
                SalamanderGeneral, -1, sourceFiles, sourceDirs, NULL, FALSE,
                TRUE, dlgSubjectSrcW);
        }

        // 'mask' was carved out of 'tgtPath' by FTPCutDirectory, and 'tgtPath' is the CONNECTION
        // codec's bytes (EncodeText above), not the local one's. Decoding it with the local ACP
        // codec round-trips the mask through the wrong encoding and then re-encodes the result with
        // the connection codec for every item, so on a UTF-8 server an edited target name is stored
        // mojibake'd - "Ünïcode.txt" becomes "Ãœnïcode.txt". A single-byte ACP almost never rejects
        // anything, so the wrong decode succeeds silently. DecodeText is EncodeText's partner and
        // uses the same codec instance.
        std::wstring maskW;
        if (!ControlConnection->DecodeText(mask.data(), mask.size(), maskW))
            return FALSE;
        const std::wstring asciiFileMasksW = SPLGetMasksStringOwned(Config.ASCIIFileMasks);
        // create the operation object
        CFTPOperation* oper = new CFTPOperation;
        if (oper != NULL)
        {
            oper->SetEncryptControlConnection(ControlConnection->GetEncryptControlConnection());
            oper->SetEncryptDataConnection(ControlConnection->GetEncryptDataConnection());
            CCertificate* cert = ControlConnection->GetCertificate();
            oper->SetCertificate(cert);
            if (cert)
                cert->Release();
            oper->SetCompressData(ControlConnection->GetCompressData());
            if (ControlConnection->InitOperation(oper)) // initialize the connection to the server according to the "control connection"
            {
                if (!oper->SetBasicData(dlgSubjectSrcW.c_str(), (AutodetectSrvType ? NULL : LastServerType.c_str())))
                {
                    delete oper;
                    return FALSE;
                }
                std::wstring targetPathText;
                if (!BuildFullPathText(fsName, tgtPath.c_str(), targetPathText))
                {
                    delete oper;
                    return FALSE;
                }
                CFTPServerPathType pathType = ControlConnection->GetFTPServerPathType(tgtPath.c_str());
                BOOL is_AS_400_QSYS_LIB_Path = pathType == ftpsptAS400 &&
                                               FTPIsPrefixOfServerPath(ftpsptAS400, "/QSYS.LIB", tgtPath.c_str());
                if (oper->SetOperationCopyMoveUpload(copy, sourcePath, '\\', !copy,
                                                     copy ? FALSE : (sourceDirs > 0),
                                                     tgtPath.c_str(), targetPathText.c_str(), FTPGetPathDelimiter(pathType),
                                                     TRUE, sourceDirs > 0, asciiFileMasksW.c_str(),
                                                     TransferMode == trmAutodetect, TransferMode == trmASCII,
                                                     Config.UploadCannotCreateFile,
                                                     Config.UploadCannotCreateDir,
                                                     Config.UploadFileAlreadyExists,
                                                     Config.UploadDirAlreadyExists,
                                                     Config.UploadRetryOnCreatedFile,
                                                     Config.UploadRetryOnResumedFile,
                                                     Config.UploadAsciiTrModeButBinFile))
                {
                    int operUID;
                    if (FTPOperationsList.AddOperation(oper, &operUID))
                    {
                        BOOL ok = TRUE;

                        // build the queue of operation items
                        CFTPQueue* queue = new CFTPQueue(ControlConnection->GetTextCodec());
                        if (queue != NULL)
                        {
                            CQuadWord totalSize(0, 0); // total size (in bytes or blocks)
                            BOOL isDir;
                            const wchar_t* name;
                            const wchar_t* dosName; // dummy
                            CQuadWord size;
                            DWORD attr; // dummy
                            BOOL useMask = wcschr(maskW.c_str(), L'*') != NULL || wcschr(maskW.c_str(), L'?') != NULL;
                            std::wstring linkPathPrefix = sourcePath;
                            SPLSalPathAddBackslashOwned(linkPathPrefix);
                            BOOL ignoreAll = FALSE;
                            while ((name = next(parent, 0, &dosName, &isDir, &size, &attr, NULL, nextParam, NULL)) != NULL)
                            {
                                // create the target name according to the operation mask (skip if it is not
                                // a mask (contains neither '*' nor '?') - so that renaming to "test^." works)
                                const std::wstring targetNameW = useMask
                                                                     ? SPLMaskNameOwned(SalamanderGeneral, name, maskW.c_str())
                                                                     : maskW;

                                std::string targetNameBytes;
                                if (!ControlConnection->GetTextCodec().EncodeUploadName(targetNameW.c_str(), targetNameW.size(), targetNameBytes))
                                {
                                    ok = FALSE;
                                    break;
                                }
                                if (is_AS_400_QSYS_LIB_Path)
                                    FTPAS400AddFileNamePart(targetNameBytes);

                                // links: size == 0, the file size must be obtained via GetLinkTgtFileSize() later
                                BOOL cancel = FALSE;
                                if (!isDir && (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                                { // this is a link to a file; stage its full path dynamically
                                    CQuadWord linkSize;
                                    const std::wstring linkNameW = linkPathPrefix + name;
                                    if (SalamanderGeneral->GetLinkTgtFileSize(parent, linkNameW.c_str(), &linkSize, &cancel, &ignoreAll))
                                        size = linkSize;
                                }

                                CFTPQueueItemType type;
                                CFTPQueueItem* item = cancel ? NULL : CreateItemForCopyOrMoveUploadOperation(name, isDir, &size, &type, TransferMode, oper, copy, tgtPath.c_str(), targetNameBytes.c_str(), &totalSize, pathType == ftpsptOpenVMS);
                                if (item != NULL)
                                {
                                    if (ok)
                                    {
                                        item->SetLocalItem(-1, type, sqisWaiting, ITEMPR_OK, sourcePath, name);
                                    }
                                    if (!ok || !queue->AddItem(item)) // add the operation to the queue
                                    {
                                        ok = FALSE;
                                        delete item;
                                    }
                                }
                                else
                                {
                                    if (!cancel)
                                        TRACE_E(LOW_MEMORY);
                                    ok = FALSE;
                                }
                                // determine whether it makes sense to continue (if there is no error)
                                if (!ok)
                                    break;
                            }
                            if (ok)
                            {
                                oper->SetChildItems(queue->GetCount(), 0, 0, 0);
                                oper->AddToTotalSize(totalSize, TRUE);
                            }
                            else
                            {
                                delete queue;
                                queue = NULL;
                            }
                        }
                        else
                        {
                            TRACE_E(LOW_MEMORY);
                            ok = FALSE;
                        }

                        if (ok) // the queue with operation items has been filled
                        {
                            // populate the UploadListingCache with the current panel contents (if the target path is in the panel
                            // and the panel contains an uninterrupted, intact, and up-to-date listing)
                            int panel;
                            if (SalamanderGeneral->GetPanelWithPluginFS(this, panel) &&
                                FTPIsTheSameServerPath(pathType, Path.c_str(), tgtPath.c_str()) &&
                                !PathListingIsIncomplete && !PathListingIsBroken &&
                                !PathListingMayBeOutdated && PathListing.has_value() &&
                                !FTPOperationsList.CanMakeChangesOnPath(User.c_str(), Host.c_str(), Port, Path.c_str(), pathType, operUID))
                            {
                                char* welcomeReply = ControlConnection->AllocServerFirstReply();
                                char* systReply = ControlConnection->AllocServerSystemReply();
                                if (welcomeReply != NULL && systReply != NULL)
                                {
                                    UploadListingCache.AddOrUpdateListing(User.c_str(), Host.c_str(), Port, Path.c_str(), pathType,
                                                                          PathListing->data(), static_cast<int>(PathListing->size()),
                                                                          PathListingDate, PathListingStartTime,
                                                                          FALSE, welcomeReply, systReply,
                                                                          AutodetectSrvType ? NULL : LastServerType.c_str(),
                                                                          ControlConnection->GetTextCodec());
                                }
                                if (welcomeReply != NULL)
                                    SalamanderGeneral->Free(welcomeReply);
                                if (systReply != NULL)
                                    SalamanderGeneral->Free(systReply);
                            }

                            oper->SetQueue(queue); // set the queue of its items for the operation
                            queue = NULL;
                            // FIXME: there is probably no place for an "only add to queue" checkbox: if (Config.UploadAddToQueue) success = TRUE;  // perform the operation later -> for now the operation is successful
                            // else // perform the operation in the active "control connection"
                            // {
                            // open the operation progress window and start the operation
                            if (RunOperation(SalamanderGeneral->GetMsgBoxParent(), operUID, oper, NULL))
                                success = TRUE; // operation succeeded
                            else
                                ok = FALSE;
                            // }
                        }
                        if (!ok)
                            FTPOperationsList.DeleteOperation(operUID, TRUE);
                        oper = NULL; // the operation is already added in the array, do not free it with 'delete' (see below)
                    }
                }
            }
            if (oper != NULL)
                delete oper;
        }
        else
            TRACE_E(LOW_MEMORY);

        if (success && invalidPathOrCancel != NULL)
            *invalidPathOrCancel = FALSE; // report "success" (otherwise "error/cancel")
        return TRUE;
    }
    return FALSE; // unknown 'mode'
}

void CPluginFSInterface::ShowSecurityInfo(HWND hParent)
{
    if (ControlConnection)
    {
        CCertificate* cert = ControlConnection->GetCertificate();
        if (cert != NULL)
        {
            cert->ShowCertificate(hParent);

            std::wstring certificateError;
            int panel;
            if (SalamanderGeneral->GetPanelWithPluginFS(this, panel))
            { // the user might have imported the certificate or deleted it from the MS store, verify the state and show it in the panel
                bool verified = cert->CheckCertificate(certificateError);
                cert->SetVerified(verified);
                SalamanderGeneral->ShowSecurityIcon(panel, TRUE, verified,
                                                    SPLLoadStrOwned(SalamanderGeneral, HLanguage, verified ? IDS_SSL_SECURITY_OK : IDS_SSL_SECURITY_UNVERIFIED).c_str());
            }
            cert->Release();
        }
    }
}
