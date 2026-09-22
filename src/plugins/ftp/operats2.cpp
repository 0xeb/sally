// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

static BOOL FormatOperationLogDateTime(const SYSTEMTIME& value, BOOL date,
                                       std::wstring& output) noexcept
{
    try
    {
        const int needed = date
                               ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE,
                                                &value, NULL, NULL, 0)
                               : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value,
                                                NULL, NULL, 0);
        if (needed > 0)
        {
            std::wstring staged(static_cast<size_t>(needed), L'\0');
            const int written = date
                                    ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE,
                                                     &value, NULL, staged.data(), needed)
                                    : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value,
                                                     NULL, staged.data(), needed);
            if (written == needed)
            {
                staged.resize(static_cast<size_t>(needed - 1));
                output.swap(staged);
                return TRUE;
            }
        }

        std::wstring fallback =
            date ? SPLFormatStringOwned(L"%u.%u.%u", value.wDay, value.wMonth, value.wYear)
                 : SPLFormatStringOwned(L"%u:%02u:%02u", value.wHour, value.wMinute,
                                        value.wSecond);
        output.swap(fallback);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

//
// ****************************************************************************
// COperationDlgThread
//

class COperationDlgThread : public CThread
{
protected:
    COperationDlg* OperDlg;
    BOOL AlwaysOnTop;
    HWND DropTargetWnd;

public:
    COperationDlgThread(COperationDlg* operDlg, HWND dropTargetWnd) : CThread(L"Operation Dialog")
    {
        OperDlg = operDlg;
        AlwaysOnTop = FALSE;
        SalamanderGeneral->GetConfigParameter(SALCFG_ALWAYSONTOP, &AlwaysOnTop, sizeof(AlwaysOnTop), NULL);
        DropTargetWnd = dropTargetWnd;
    }

    virtual unsigned Body()
    {
        CALL_STACK_MESSAGE1("COperationDlgThread::Body()");

        // 'sendWMClose': the dialog sets it to TRUE when WM_CLOSE is received
        // when a modal dialog above the operation dialog is open - once that
        // modal dialog closes, WM_CLOSE is sent again to the operation dialog
        BOOL sendWMClose = FALSE;
        OperDlg->SendWMClose = &sendWMClose;
        if (OperDlg->Create() == NULL || OperDlg->CloseDlg)
        {
            if (!OperDlg->CloseDlg)
                OperDlg->Oper->SetOperationDlg(NULL);
            if (OperDlg->HWindow != NULL)
                DestroyWindow(OperDlg->HWindow); // WM_CLOSE cannot arrive because nothing can deliver it (the message loop is not running yet)
        }
        else
        {
            HWND dlg = OperDlg->HWindow; // safely stored window handle (valid even after OperDlg is destroyed)
            if (AlwaysOnTop)             // handle always-on-top at least "statically" (it's not in the system menu)
                SetWindowPos(dlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

            SetForegroundWindow(dlg);

            // ensure the drop target activates during drag & drop instead of the operation dialog
            if (DropTargetWnd != NULL)
                SalamanderGeneral->ActivateDropTarget(DropTargetWnd, dlg);

            // message loop - wait until the modeless dialog is closed
            MSG msg;
            while (GetMessageW(&msg, NULL, 0, 0))
            {
                if (!IsDialogMessage(dlg, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                if (sendWMClose)
                {
                    sendWMClose = FALSE;
                    PostMessage(dlg, WM_CLOSE, 0, 0);
                }
            }
        }
        delete OperDlg;
        return 0;
    }
};

//
// ****************************************************************************
// CFTPOperation
//

static char* DupOperationText(const char* text)
{
    if (text == NULL)
        return NULL;
    const int length = (int)strlen(text) + 1;
    char* copy = (char*)SalamanderGeneral->Alloc(length);
    if (copy != NULL)
        memcpy(copy, text, length);
    return copy;
}

static wchar_t* DupOperationWideText(const wchar_t* text)
{
    if (text == NULL)
        return NULL;
    const size_t length = wcslen(text) + 1;
    if (length > INT_MAX / sizeof(wchar_t))
        return NULL;
    wchar_t* copy = (wchar_t*)SalamanderGeneral->Alloc((int)(length * sizeof(wchar_t)));
    if (copy != NULL)
        memcpy(copy, text, length * sizeof(wchar_t));
    return copy;
}

BOOL CFTPOperation::SetConnection(CFTPProxyServer* proxyServer, const wchar_t* host, unsigned short port,
                                  const CFtpTextCodec& identityCodec,
                                  const wchar_t* user, const wchar_t* password, const wchar_t* account,
                                  const char* initFTPCommands, BOOL usePassiveMode,
                                  const char* listCommand, DWORD serverIP,
                                  const char* serverSystem, const char* serverFirstReply,
                                  BOOL useListingsCache, DWORD hostIP) noexcept
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetConnection()");

    if (host == NULL || *host == 0)
        return FALSE;
    std::wstring stagedHost;
    if (!FtpStoreWideText(host, stagedHost))
        return FALSE;
    std::wstring stagedUser;
    std::wstring stagedPassword;
    std::wstring stagedAccount;
    std::string stagedInitFTPCommands;
    std::string stagedListCommand;
    std::string stagedServerSystem;
    std::string stagedServerFirstReply;
    std::string stagedProxyScriptText;
    CFTPProxyServer* stagedProxyServer = proxyServer != NULL ? proxyServer->MakeCopy() : NULL;
    BOOL err = proxyServer != NULL && stagedProxyServer == NULL;
    err = err || !FtpStoreWideText(user != NULL ? user : L"", stagedUser) ||
          !FtpStoreWideText(password != NULL ? password : L"", stagedPassword) ||
          !FtpStoreWideText(account != NULL ? account : L"", stagedAccount) ||
          !FtpStoreProtocolBytes((initFTPCommands != NULL && *initFTPCommands != 0) ? initFTPCommands : "", stagedInitFTPCommands) ||
          !FtpStoreProtocolBytes(listCommand != NULL ? listCommand : "", stagedListCommand) ||
          !FtpStoreProtocolBytes(serverSystem != NULL ? serverSystem : "", stagedServerSystem) ||
          !FtpStoreProtocolBytes(serverFirstReply != NULL ? serverFirstReply : "", stagedServerFirstReply);

    CFTPProxyServerType proxyType = fpstNotUsed;
    if (stagedProxyServer != NULL)
        proxyType = stagedProxyServer->ProxyType;
    const char* selectedProxyScript;
    if (proxyType == fpstOwnScript)
        selectedProxyScript = stagedProxyServer->ProxyScript.c_str();
    else
    {
        selectedProxyScript = GetProxyScriptText(proxyType, FALSE);
        if (selectedProxyScript[0] == 0)
            selectedProxyScript = GetProxyScriptText(fpstNotUsed, FALSE); // undefined script = "not used (direct connection)" script - SOCKS 4/4A/5, HTTP 1.1
    }
    if (!FtpStoreProtocolBytes(selectedProxyScript, stagedProxyScriptText))
        err = TRUE;
    std::wstring stagedConnectToHost;
    unsigned short stagedConnectToPort = 21;
    size_t proxyScriptStartOffset = 0;
    if (!err)
    {
        CProxyScriptParams proxyScriptParams(stagedProxyServer, stagedHost.c_str(), port, stagedUser.c_str(), stagedPassword.c_str(), stagedAccount.c_str(),
                                             stagedPassword.empty());
        std::string errorDescription;
        BOOL lowMemory = !proxyScriptParams.IsGood();
        const char* stagedExecPoint = NULL;
        if (proxyScriptParams.IsGood() &&
            ProcessProxyScript(FtpLocalTextCodec(), stagedProxyScriptText.c_str(), &stagedExecPoint, -1,
                               &proxyScriptParams, &stagedConnectToHost, &stagedConnectToPort,
                               NULL, NULL, &errorDescription, NULL, &lowMemory))
        {
            if (proxyScriptParams.NeedUserInput()) // theoretically should not happen (already verified by running it in the panel)
            {
                err = TRUE;
                TRACE_E("CFTPOperation::SetConnection(): unexpected situation: proxy script needs user input!");
            }
            else
            {
                proxyScriptStartOffset = static_cast<size_t>(stagedExecPoint - stagedProxyScriptText.c_str());
            }
        }
        else // theoretically should never happen (stored scripts are validated and already verified by running them in the panel)
        {
            err = TRUE;
            if (lowMemory)
                TRACE_E(LOW_MEMORY);
            else
                TRACE_E("CFTPOperation::SetConnection(): proxy script error: " << errorDescription.c_str());
        }
    }
    if (err)
    {
        delete stagedProxyServer;
        FTPSecureWipe(stagedPassword);
        FTPSecureWipe(stagedAccount);
        return FALSE;
    }

    Host.swap(stagedHost);
    Port = port;
    IdentityCodec = identityCodec;
    User.swap(stagedUser);
    Password.swap(stagedPassword);
    Account.swap(stagedAccount);
    InitFTPCommands.swap(stagedInitFTPCommands);
    UsePassiveMode = usePassiveMode;
    SizeCmdIsSupported = TRUE;
    ListCommand.swap(stagedListCommand);
    ServerSystem.swap(stagedServerSystem);
    ServerFirstReply.swap(stagedServerFirstReply);
    ServerIP = serverIP;
    UseListingsCache = useListingsCache;
    HostIP = hostIP;
    ProxyServer = stagedProxyServer;
    ProxyScriptText.swap(stagedProxyScriptText);
    ProxyScriptStartExecPoint = ProxyScriptText.c_str() + proxyScriptStartOffset;
    ConnectToHost.swap(stagedConnectToHost);
    ConnectToPort = stagedConnectToPort;
    FTPSecureWipe(stagedPassword);
    FTPSecureWipe(stagedAccount);
    return TRUE;
}

BOOL CFTPOperation::SetBasicData(const wchar_t* operationSubject, const char* listingServerType)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetBasicData()");

    std::wstring stagedOperationSubject;
    std::string stagedListingServerType;
    if (!FtpStoreWideText(operationSubject != NULL ? operationSubject : L"",
                          stagedOperationSubject) ||
        (listingServerType != NULL &&
         !FtpStoreLocalTextBytes(listingServerType, stagedListingServerType)))
        return FALSE;
    OperationSubject.swap(stagedOperationSubject);
    ListingServerType.swap(stagedListingServerType);
    return TRUE;
}

BOOL CFTPOperation::SetOperationDelete(const char* remoteSourcePath, const wchar_t* sourcePath, char srcPathSeparator,
                                       BOOL srcPathCanChange, BOOL srcPathCanChangeInclSubdirs,
                                       int confirmDelOnNonEmptyDir, int confirmDelOnHiddenFile,
                                       int confirmDelOnHiddenDir)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetOperationDelete()");

    char* stagedRemoteSourcePath = DupOperationText(remoteSourcePath);
    wchar_t* stagedSourcePath = DupOperationWideText(sourcePath);
    if (stagedRemoteSourcePath == NULL || stagedSourcePath == NULL)
    {
        if (stagedRemoteSourcePath != NULL)
            SalamanderGeneral->Free(stagedRemoteSourcePath);
        if (stagedSourcePath != NULL)
            SalamanderGeneral->Free(stagedSourcePath);
        return FALSE;
    }
    Type = fotDelete;
    RemoteSourcePath = stagedRemoteSourcePath;
    SourcePath = stagedSourcePath;
    SrcPathSeparator = srcPathSeparator;
    SrcPathCanChange = srcPathCanChange;
    SrcPathCanChangeInclSubdirs = srcPathCanChangeInclSubdirs;
    ConfirmDelOnNonEmptyDir = confirmDelOnNonEmptyDir;
    ConfirmDelOnHiddenFile = confirmDelOnHiddenFile;
    ConfirmDelOnHiddenDir = confirmDelOnHiddenDir;
    return TRUE;
}

BOOL CFTPOperation::SetOperationCopyMoveDownload(BOOL isCopy, const char* remoteSourcePath,
                                                 const wchar_t* sourcePath, char srcPathSeparator,
                                                 BOOL srcPathCanChange, BOOL srcPathCanChangeInclSubdirs,
                                                 const wchar_t* targetPath,
                                                 char tgtPathSeparator, BOOL tgtPathCanChange,
                                                 BOOL tgtPathCanChangeInclSubdirs, const wchar_t* asciiFileMasks,
                                                 int autodetectTrMode, int useAsciiTransferMode,
                                                 int cannotCreateFile, int cannotCreateDir,
                                                 int fileAlreadyExists, int dirAlreadyExists, int retryOnCreatedFile,
                                                 int retryOnResumedFile, int asciiTrModeButBinFile)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetOperationCopyMoveDownload()");

    Type = isCopy ? fotCopyDownload : fotMoveDownload;
    BOOL err = FALSE;
    RemoteSourcePath = DupOperationText(remoteSourcePath);
    SourcePath = DupOperationWideText(sourcePath);
    SrcPathSeparator = srcPathSeparator;
    SrcPathCanChange = srcPathCanChange;
    SrcPathCanChangeInclSubdirs = srcPathCanChangeInclSubdirs;
    TargetPath = DupOperationWideText(targetPath);
    err = RemoteSourcePath == NULL || SourcePath == NULL || TargetPath == NULL;
    TgtPathSeparator = tgtPathSeparator;
    TgtPathCanChange = tgtPathCanChange;
    TgtPathCanChangeInclSubdirs = tgtPathCanChangeInclSubdirs;
    if (asciiFileMasks != NULL) // non-empty mask; otherwise creating a group-mask object makes no sense
    {
        ASCIIFileMasks = SalamanderGeneral->AllocSalamanderMaskGroup();
        if (ASCIIFileMasks != NULL)
        {
            ASCIIFileMasks->SetMasksString(asciiFileMasks, FALSE);
            int errorPos;
            if (!ASCIIFileMasks->PrepareMasks(errorPos))
                err = TRUE;
        }
        else
            err = TRUE;
    }
    AutodetectTrMode = autodetectTrMode;
    UseAsciiTransferMode = useAsciiTransferMode;
    CannotCreateFile = cannotCreateFile;
    CannotCreateDir = cannotCreateDir;
    FileAlreadyExists = fileAlreadyExists;
    DirAlreadyExists = dirAlreadyExists;
    RetryOnCreatedFile = retryOnCreatedFile;
    RetryOnResumedFile = retryOnResumedFile;
    AsciiTrModeButBinFile = asciiTrModeButBinFile;
    return !err;
}

BOOL CFTPOperation::SetOperationCopyMoveUpload(BOOL isCopy, const wchar_t* sourcePath, char srcPathSeparator,
                                               BOOL srcPathCanChange, BOOL srcPathCanChangeInclSubdirs,
                                               const char* remoteTargetPath, const wchar_t* targetPath, char tgtPathSeparator,
                                               BOOL tgtPathCanChange, BOOL tgtPathCanChangeInclSubdirs,
                                               const wchar_t* asciiFileMasks, int autodetectTrMode,
                                               int useAsciiTransferMode, int uploadCannotCreateFile,
                                               int uploadCannotCreateDir, int uploadFileAlreadyExists,
                                               int uploadDirAlreadyExists, int uploadRetryOnCreatedFile,
                                               int uploadRetryOnResumedFile, int uploadAsciiTrModeButBinFile)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetOperationCopyMoveUpload()");

    Type = isCopy ? fotCopyUpload : fotMoveUpload;
    BOOL err = FALSE;
    SourcePath = DupOperationWideText(sourcePath);
    SrcPathSeparator = srcPathSeparator;
    SrcPathCanChange = srcPathCanChange;
    SrcPathCanChangeInclSubdirs = srcPathCanChangeInclSubdirs;
    RemoteTargetPath = DupOperationText(remoteTargetPath);
    TargetPath = DupOperationWideText(targetPath);
    err = SourcePath == NULL || RemoteTargetPath == NULL || TargetPath == NULL;
    TgtPathSeparator = tgtPathSeparator;
    TgtPathCanChange = tgtPathCanChange;
    TgtPathCanChangeInclSubdirs = tgtPathCanChangeInclSubdirs;
    if (asciiFileMasks != NULL) // non-empty mask; otherwise creating a group-mask object makes no sense
    {
        ASCIIFileMasks = SalamanderGeneral->AllocSalamanderMaskGroup();
        if (ASCIIFileMasks != NULL)
        {
            ASCIIFileMasks->SetMasksString(asciiFileMasks, FALSE);
            int errorPos;
            if (!ASCIIFileMasks->PrepareMasks(errorPos))
                err = TRUE;
        }
        else
            err = TRUE;
    }
    AutodetectTrMode = autodetectTrMode;
    UseAsciiTransferMode = useAsciiTransferMode;
    UploadCannotCreateFile = uploadCannotCreateFile;
    UploadCannotCreateDir = uploadCannotCreateDir;
    UploadFileAlreadyExists = uploadFileAlreadyExists;
    UploadDirAlreadyExists = uploadDirAlreadyExists;
    UploadRetryOnCreatedFile = uploadRetryOnCreatedFile;
    UploadRetryOnResumedFile = uploadRetryOnResumedFile;
    UploadAsciiTrModeButBinFile = uploadAsciiTrModeButBinFile;
    return !err;
}

BOOL CFTPOperation::SetOperationChAttr(const char* remoteSourcePath, const wchar_t* sourcePath, char srcPathSeparator,
                                       BOOL srcPathCanChange, BOOL srcPathCanChangeInclSubdirs,
                                       WORD attrAnd, WORD attrOr, int chAttrOfFiles, int chAttrOfDirs,
                                       int unknownAttrs)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetOperationChAttr()");

    char* stagedRemoteSourcePath = DupOperationText(remoteSourcePath);
    wchar_t* stagedSourcePath = DupOperationWideText(sourcePath);
    if (stagedRemoteSourcePath == NULL || stagedSourcePath == NULL)
    {
        if (stagedRemoteSourcePath != NULL)
            SalamanderGeneral->Free(stagedRemoteSourcePath);
        if (stagedSourcePath != NULL)
            SalamanderGeneral->Free(stagedSourcePath);
        return FALSE;
    }
    Type = fotChangeAttrs;
    RemoteSourcePath = stagedRemoteSourcePath;
    SourcePath = stagedSourcePath;
    SrcPathSeparator = srcPathSeparator;
    SrcPathCanChange = srcPathCanChange;
    SrcPathCanChangeInclSubdirs = srcPathCanChangeInclSubdirs;
    AttrAnd = attrAnd;
    AttrOr = attrOr;
    ChAttrOfFiles = chAttrOfFiles;
    ChAttrOfDirs = chAttrOfDirs;
    UnknownAttrs = unknownAttrs;
    return TRUE;
}

void CFTPOperation::SetQueue(CFTPQueue* queue)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetQueue()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (Queue == NULL)
        Queue = queue;
    else
    {
        TRACE_E("Unexpected situation in CFTPOperation::SetQueue(): queue already exists!");
        delete queue;
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

CFTPQueue*
CFTPOperation::GetQueue()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetQueue()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (Queue == NULL)
        TRACE_E("Unexpected situation in CFTPOperation::GetQueue(): queue doesn't exist!");
    CFTPQueue* ret = Queue;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

CFTPWorker*
CFTPOperation::AllocNewWorker()
{
    CALL_STACK_MESSAGE1("CFTPOperation::AllocNewWorker()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    CFTPWorker* ret = new CFTPWorker(this, Queue, Host.c_str(), Port, User.c_str());
    if (ret == NULL)
        TRACE_E(LOW_MEMORY);
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::SendHeaderToLog(int logUID)
{
    CALL_STACK_MESSAGE2("CFTPOperation::SendHeaderToLog(%d)", logUID);

    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ok = TRUE;
    // building the window title
    int titleResID = 0;
    switch (Type)
    {
    case fotDelete:
        titleResID = IDS_OPERDLGDELETETITLE;
        break;
    case fotCopyDownload:
        titleResID = IDS_OPERDLGCOPYDOWNLOADTITLE;
        break;
    case fotMoveDownload:
        titleResID = IDS_OPERDLGMOVEDOWNLOADTITLE;
        break;
    case fotCopyUpload:
        titleResID = IDS_OPERDLGCOPYUPLOADTITLE;
        break;
    case fotMoveUpload:
        titleResID = IDS_OPERDLGMOVEUPLOADTITLE;
        break;
    case fotChangeAttrs:
        titleResID = IDS_OPERDLGCHATTRSTITLE;
        break;
    default:
        TRACE_E("CFTPOperation::SendHeaderToLog(): unknown operation type!");
        ok = FALSE;
        break;
    }
    if (ok)
    {
        try
        {
            std::wstring title = SPLFormatStringOwned(
                LangStr(titleResID).c_str(), OperationSubject.c_str(), Host.c_str());
            title.append(L"\r\n");
            Logs.LogMessage(logUID, title.c_str(), -1);

            SYSTEMTIME st;
            GetLocalTime(&st);
            std::wstring dateText;
            std::wstring timeText;
            if (!FormatOperationLogDateTime(st, TRUE, dateText) ||
                !FormatOperationLogDateTime(st, FALSE, timeText))
            {
                ok = FALSE;
            }
            else
            {
                dateText.append(L" - ");
                dateText.append(timeText);
                const std::wstring header = SPLFormatStringOwned(
                    LangStr(IDS_WORKERLOGHEADER).c_str(), Host.c_str(), Port, logUID,
                    dateText.c_str());
                Logs.LogMessage(logUID, header.c_str(), -1);
            }
        }
        catch (...)
        {
            ok = FALSE;
        }
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetChildItems(int notDone, int skipped, int failed, int uiNeeded)
{
    CALL_STACK_MESSAGE5("CFTPOperation::SetChildItems(%d, %d, %d, %d)",
                        notDone, skipped, failed, uiNeeded);

    HANDLES(EnterCriticalSection(&OperCritSect));
    ChildItemsNotDone = notDone;
    ChildItemsSkipped = skipped;
    ChildItemsFailed = failed;
    ChildItemsUINeeded = uiNeeded;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::AddToNotDoneSkippedFailed(int notDone, int skipped, int failed, int uiNeeded,
                                              BOOL onlyUINeededOrFailedToSkipped)
{
    SLOW_CALL_STACK_MESSAGE1("CFTPOperation::AddToNotDoneSkippedFailed()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    ChildItemsNotDone += notDone;
    ChildItemsSkipped += skipped;
    ChildItemsFailed += failed;
    ChildItemsUINeeded += uiNeeded;
    if (ChildItemsNotDone < 0 || ChildItemsSkipped < 0 || ChildItemsFailed < 0 || ChildItemsUINeeded < 0)
    {
        TRACE_E("Unexpected situation in CFTPOperation::AddToNotDoneSkippedFailed(): some counter is negative! "
                "NotDone="
                << ChildItemsNotDone << ", Skipped=" << ChildItemsSkipped << ", Failed=" << ChildItemsFailed << ", UINeeded=" << ChildItemsUINeeded);
    }
    COperationState state = GetOperationState(FALSE);
    if (LastReportedOperState != state)
    {
        if (state == opstInProgress) // a retry happened, we are returning to work, so reset the global speed meter (we cannot compute an average for the time spent waiting for the user)
        {
            GlobalTransferSpeedMeter.Clear();
            GlobalTransferSpeedMeter.JustConnected();
            GlobalLastActivityTime.Set(GetTickCount()); // retry counts as activity
        }
        ReportOperationStateChange();
    }
    if (state == opstSuccessfullyFinished ||
        !onlyUINeededOrFailedToSkipped && (state == opstFinishedWithSkips || state == opstFinishedWithErrors))
    {
        BOOL softRefresh = state == opstFinishedWithErrors || // FIXME: once a window with the operation queue exists, we must replace OperationDlg->DlgWillCloseIfOpFinWithSkips with a different detection of whether the worker closes (passing the connection back to the panel)
                           state == opstFinishedWithSkips && (OperationDlg != NULL ? !OperationDlg->DlgWillCloseIfOpFinWithSkips : TRUE);
        PostChangeOnPathNotifications(softRefresh); // the line is free now (at least for this operation), so we can afford listing refreshes
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

BOOL CFTPOperation::IsASCIIFile(const wchar_t* name, const wchar_t* ext)
{
    CALL_STACK_MESSAGE_NONE

    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = ASCIIFileMasks != NULL && ASCIIFileMasks->AgreeMasks(name, ext);
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::AddToTotalSize(const CQuadWord& size, BOOL sizeInBytes)
{
    CALL_STACK_MESSAGE1("CFTPOperation::AddToTotalSize(,)");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (sizeInBytes)
        TotalSizeInBytes += size;
    else
        TotalSizeInBlocks += size;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SubFromTotalSize(const CQuadWord& size, BOOL sizeInBytes)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SubFromTotalSize(,)");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (sizeInBytes)
        TotalSizeInBytes -= size;
    else
        TotalSizeInBlocks -= size;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetOperationDlg(COperationDlg* operDlg)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetOperationDlg()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    OperationDlg = operDlg;
    ReportChangeInWorkerID = -2;  // dialog changed, reset reporting of changes
    ReportProgressChange = FALSE; // dialog changed, reset reporting of changes
    ReportChangeInItemUID = -3;   // dialog changed, reset reporting of changes
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

BOOL CFTPOperation::ActivateOperationDlg(HWND dropTargetWnd)
{
    CALL_STACK_MESSAGE1("CFTPOperation::ActivateOperationDlg()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = FALSE;
    if (OperationDlg != NULL)
    {
        if (OperationDlg->HWindow != NULL) // "always true" (otherwise the dialog is only just opening and will activate itself)
        {
            if (IsIconic(OperationDlg->HWindow))
                ShowWindow(OperationDlg->HWindow, SW_RESTORE);
            SetForegroundWindow(OperationDlg->HWindow);
        }
        ret = TRUE;
    }
    else
    {
        OperationDlg = new COperationDlg(NULL, SalamanderGeneral->GetMainWindowHWND(),
                                         this, Queue, &WorkersList);
        ReportChangeInWorkerID = -2;  // dialog changed, reset reporting of changes
        ReportProgressChange = FALSE; // dialog changed, reset reporting of changes
        ReportChangeInItemUID = -3;   // dialog changed, reset reporting of changes
        if (OperationDlg != NULL)
        {
            COperationDlgThread* t = new COperationDlgThread(OperationDlg, dropTargetWnd);
            if (t != NULL)
            {
                if ((OperationDlgThread = t->Create(AuxThreadQueue)) == NULL)
                { // thread did not start, error
                    delete t;
                    delete OperationDlg;
                    OperationDlg = NULL;
                    ReportChangeInWorkerID = -2;  // dialog changed, reset reporting of changes
                    ReportProgressChange = FALSE; // dialog changed, reset reporting of changes
                    ReportChangeInItemUID = -3;   // dialog changed, reset reporting of changes
                }
                else
                    ret = TRUE; // success
            }
            else // low memory, error
            {
                delete OperationDlg;
                OperationDlg = NULL;
                ReportChangeInWorkerID = -2;  // dialog changed, reset reporting of changes
                ReportProgressChange = FALSE; // dialog changed, reset reporting of changes
                ReportChangeInItemUID = -3;   // dialog changed, reset reporting of changes
                TRACE_E(LOW_MEMORY);
            }
        }
        else
            TRACE_E(LOW_MEMORY); // low memory, error
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::CloseOperationDlg(HANDLE* dlgThread)
{
    CALL_STACK_MESSAGE1("CFTPOperation::CloseOperationDlg()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (OperationDlg != NULL)
    {
        OperationDlg->CloseDlg = TRUE; // there is no synchronized access to CloseDlg, hopefully unnecessarily
        if (OperationDlg->HWindow != NULL)
            PostMessage(OperationDlg->HWindow, WM_CLOSE, 0, 0);
        OperationDlg = NULL;          // the dialog thread takes care of deallocation
        ReportChangeInWorkerID = -2;  // dialog changed, reset reporting of changes
        ReportProgressChange = FALSE; // dialog changed, reset reporting of changes
        ReportChangeInItemUID = -3;   // dialog changed, reset reporting of changes
    }
    if (dlgThread != NULL)
        *dlgThread = OperationDlgThread;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

BOOL CFTPOperation::AddWorker(CFTPWorker* newWorker)
{
    CALL_STACK_MESSAGE1("CFTPOperation::AddWorker()");
    BOOL ret = WorkersList.AddWorker(newWorker); // synchronization is inside WorkersList (the OperCritSect section is not needed here)
    if (ret)
    {
        GlobalLastActivityTime.Set(GetTickCount()); // adding a worker counts as activity
        OperationStatusMaybeChanged();
    }
    return ret;
}

void CFTPOperation::OperationStatusMaybeChanged()
{
    CALL_STACK_MESSAGE1("CFTPOperation::OperationStatusMaybeChanged()");

    BOOL someIsWorkingAndNotPaused;
    WorkersList.SomeWorkerIsWorking(&someIsWorkingAndNotPaused);
    BOOL paused = !someIsWorkingAndNotPaused;
    BOOL stopping = WorkersList.EmptyOrAllShouldStop();

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (!paused &&
        ChildItemsNotDone - ChildItemsSkipped - ChildItemsFailed - ChildItemsUINeeded <= 0)
    { // workers are running but items are not => the operation is not running
        paused = TRUE;
    }
    if (paused || stopping) // the operation is supposed to be "paused"
    {
        if (OperationEnd == -1) // the operation is running
        {
            OperationEnd = GetTickCount();
            if (OperationEnd == -1)
                OperationEnd++; // avoid colliding with the value -1 (shift the time by 1 ms)
        }
    }
    else // the operation is supposed to be "resumed"
    {
        if (OperationEnd != -1) // the operation is not running
        {
            GlobalTransferSpeedMeter.Clear();
            GlobalTransferSpeedMeter.JustConnected();

            OperationStart = GetTickCount() - (OperationEnd - OperationStart);
            OperationEnd = -1;
        }
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

BOOL CFTPOperation::InformWorkersAboutStop(int workerInd, CFTPWorker** victims,
                                           int maxVictims, int* foundVictims)
{
    CALL_STACK_MESSAGE1("CFTPOperation::InformWorkersAboutStop()");
    return WorkersList.InformWorkersAboutStop(workerInd, victims, maxVictims, foundVictims); // synchronization is inside WorkersList (the OperCritSect section is not needed here)
}

BOOL CFTPOperation::InformWorkersAboutPause(int workerInd, CFTPWorker** victims,
                                            int maxVictims, int* foundVictims, BOOL pause)
{
    CALL_STACK_MESSAGE1("CFTPOperation::InformWorkersAboutPause()");
    return WorkersList.InformWorkersAboutPause(workerInd, victims, maxVictims, foundVictims, pause); // synchronization is inside WorkersList (the OperCritSect section is not needed here)
}

BOOL CFTPOperation::CanCloseWorkers(int workerInd)
{
    CALL_STACK_MESSAGE1("CFTPOperation::CanCloseWorkers()");
    return WorkersList.CanCloseWorkers(workerInd); // synchronization is inside WorkersList (the OperCritSect section is not needed here)
}

BOOL CFTPOperation::ForceCloseWorkers(int workerInd, CFTPWorker** victims,
                                      int maxVictims, int* foundVictims)
{
    CALL_STACK_MESSAGE1("CFTPOperation::ForceCloseWorkers()");
    return WorkersList.ForceCloseWorkers(workerInd, victims, maxVictims, foundVictims); // synchronization is inside WorkersList (the OperCritSect section is not needed here)
}

BOOL CFTPOperation::DeleteWorkers(int workerInd, CFTPWorker** victims,
                                  int maxVictims, int* foundVictims,
                                  CUploadWaitingWorker** uploadFirstWaitingWorker)
{
    CALL_STACK_MESSAGE1("CFTPOperation::DeleteWorkers()");
    BOOL ret = WorkersList.DeleteWorkers(workerInd, victims, maxVictims, foundVictims, uploadFirstWaitingWorker); // synchronization is inside WorkersList (the OperCritSect section is not needed here)
    OperationStatusMaybeChanged();
    return ret;
}

BOOL CFTPOperation::InitOperDlg(COperationDlg* dlg)
{
    CALL_STACK_MESSAGE1("CFTPOperation::InitOperDlg()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ok = TRUE;

    // building the window title
    int titleResID = 0;
    switch (Type)
    {
    case fotDelete:
        titleResID = IDS_OPERDLGDELETETITLE;
        break;
    case fotCopyDownload:
        titleResID = IDS_OPERDLGCOPYDOWNLOADTITLE;
        break;
    case fotMoveDownload:
        titleResID = IDS_OPERDLGMOVEDOWNLOADTITLE;
        break;
    case fotCopyUpload:
        titleResID = IDS_OPERDLGCOPYUPLOADTITLE;
        break;
    case fotMoveUpload:
        titleResID = IDS_OPERDLGMOVEUPLOADTITLE;
        break;
    case fotChangeAttrs:
        titleResID = IDS_OPERDLGCHATTRSTITLE;
        break;
    default:
        TRACE_E("CFTPOperation::InitOperDlg(): unknown operation type!");
        ok = FALSE;
        break;
    }
    if (ok)
    {
        try
        {
            std::wstring title = SPLFormatStringOwned(
                LangStr(titleResID).c_str(), OperationSubject.c_str(), Host.c_str());
            dlg->TitleText.swap(title);
        }
        catch (...)
        {
            ok = FALSE;
        }
    }

    // set the source and target paths
    if (ok)
    {
        dlg->Source->SetPathSeparator(SrcPathSeparator);
        if (Type == fotCopyDownload || Type == fotMoveDownload ||
            Type == fotCopyUpload || Type == fotMoveUpload)
        {
            SetDlgItemTextW(dlg->HWindow, IDT_OPSOURCETITLE, LangStr(IDS_OPERDLGSRCPATH).c_str());
            SetDlgItemTextW(dlg->HWindow, IDT_OPTARGETTITLE, LangStr(IDS_OPERDLGTGTPATH).c_str());
            dlg->Target->SetPathSeparator(TgtPathSeparator);
            if (!dlg->Source->SetText(SourcePath) ||
                !dlg->Target->SetText(TargetPath))
                ok = FALSE;
        }
        else // move + ch-attrs
        {
            SetDlgItemTextW(dlg->HWindow, IDT_OPSOURCETITLE, LangStr(IDS_OPERDLGPATH).c_str());
            if (!dlg->Source->SetText(SourcePath))
                ok = FALSE;
        }
    }

    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ok;
}

BOOL CFTPOperation::GetServerAddress(DWORD* serverIP, std::wstring& host, BOOL* hostReady) noexcept
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetServerAddress(, ,)");
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = TRUE;
    *hostReady = TRUE;
    *serverIP = ServerIP;
    if (ServerIP == INADDR_NONE) // IP address is unknown, return the host name
    {
        if (!FtpStoreWideText(ConnectToHost.c_str(), host))
        {
            *hostReady = FALSE;
        }
        ret = FALSE;
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::SetServerIP(DWORD serverIP)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetServerIP()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    if (ServerIP != serverIP && ServerIP != INADDR_NONE)
        TRACE_E("Unexpected situation in CFTPOperation::SetServerIP(): two different IP addresses for one host!");
    ServerIP = serverIP;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

BOOL CFTPOperation::GetConnectInfo(DWORD* serverIP, unsigned short* port, std::wstring& host,
                                   CFTPProxyServerType* proxyType, DWORD* hostIP, unsigned short* hostPort,
                                   std::wstring& proxyUser, std::wstring& proxyPassword) noexcept
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetConnectInfo(, , ,)");
    std::wstring proxyUserW;
    std::wstring proxyPasswordW;
    DWORD stagedServerIP;
    unsigned short stagedPort;
    std::wstring stagedHost;
    DWORD stagedHostIP;
    unsigned short stagedHostPort;
    CFTPProxyServerType stagedProxyType = fpstNotUsed;
    HANDLES(EnterCriticalSection(&OperCritSect));
    stagedServerIP = ServerIP;
    stagedPort = ConnectToPort;
    BOOL stored = FtpStoreWideText(Host.c_str(), stagedHost);
    stagedHostIP = HostIP;
    stagedHostPort = Port;
    if (ProxyServer != NULL)
    {
        stagedProxyType = ProxyServer->ProxyType;
        stored = FtpStoreWideText(ProxyServer->ProxyUser, proxyUserW) &&
                 FtpStoreWideText(ProxyServer->ProxyPlainPassword, proxyPasswordW);
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    if (!stored)
    {
        FTPSecureWipe(proxyPasswordW);
        return FALSE;
    }
    *serverIP = stagedServerIP;
    *port = stagedPort;
    host.swap(stagedHost);
    *hostIP = stagedHostIP;
    *hostPort = stagedHostPort;
    *proxyType = stagedProxyType;
    proxyUser.swap(proxyUserW);
    proxyPassword.swap(proxyPasswordW);
    FTPSecureWipe(proxyPasswordW);
    return TRUE;
}

BOOL CFTPOperation::GetConnectLogMsg(BOOL isReconnect, std::string& text, int attemptNumber, const char* dateTime)
{
    CALL_STACK_MESSAGE2("CFTPOperation::GetConnectLogMsg(%d, , , ,)", isReconnect);
    HANDLES(EnterCriticalSection(&OperCritSect));
    in_addr srvAddr;
    srvAddr.s_addr = ServerIP;
    std::string userBytes;
    std::string proxyUserBytes;
    std::string hostBytes;
    std::string connectToHostBytes;
    std::string proxyHostBytes;
    BOOL result = FtpEncodeLocalText(User.c_str(), userBytes) &&
                  FtpEncodeNetworkHost(Host.c_str(), hostBytes) &&
                  FtpEncodeNetworkHost(ConnectToHost.c_str(), connectToHostBytes) &&
                  (ProxyServer == NULL ||
                   (FtpEncodeLocalText(ProxyServer->ProxyUser.c_str(), proxyUserBytes) &&
                    FtpEncodeNetworkHost(ProxyServer->ProxyHost.c_str(), proxyHostBytes)));
    if (isReconnect)
    {
        if (ProxyServer != NULL)
        {
            std::string proxyNameForLog;
            std::string proxyTypeName;
            result = result &&
                     FtpEncodeLocalTextForByteLog(ProxyServer->ProxyName.c_str(),
                                                  "<Unicode proxy profile>", proxyNameForLog) &&
                     GetProxyTypeName(ProxyServer->ProxyType, proxyTypeName) &&
                     FTPFormatString(text, LoadStr(IDS_PRXSRVRECONLOGHEADER), hostBytes.c_str(), Port, userBytes.c_str(),
                                      proxyNameForLog.c_str(), proxyTypeName.c_str(),
                                      proxyHostBytes.c_str(), ProxyServer->ProxyPort,
                                     proxyUserBytes.c_str(), connectToHostBytes.c_str(), inet_ntoa(srvAddr),
                                     ConnectToPort, attemptNumber, dateTime);
        }
        else
        {
            result = result && FTPFormatString(text, LoadStr(IDS_RECONLOGHEADER), connectToHostBytes.c_str(), inet_ntoa(srvAddr), ConnectToPort,
                                     attemptNumber, dateTime);
        }
    }
    else if (ProxyServer != NULL)
    {
        std::string proxyNameForLog;
        std::string proxyTypeName;
        result = result &&
                 FtpEncodeLocalTextForByteLog(ProxyServer->ProxyName.c_str(),
                                              "<Unicode proxy profile>", proxyNameForLog) &&
                 GetProxyTypeName(ProxyServer->ProxyType, proxyTypeName) &&
                 FTPFormatString(text, LoadStr(IDS_PRXSRVWORKERCONLOGHDR), hostBytes.c_str(), Port, userBytes.c_str(),
                                  proxyNameForLog.c_str(), proxyTypeName.c_str(),
                                  proxyHostBytes.c_str(), ProxyServer->ProxyPort,
                                 proxyUserBytes.c_str(), connectToHostBytes.c_str(), inet_ntoa(srvAddr), ConnectToPort);
    }
    else
        result = result && FTPFormatString(text, LoadStr(IDS_WORKERCONLOGHDR), connectToHostBytes.c_str(), inet_ntoa(srvAddr), ConnectToPort);
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return result;
}

BOOL CFTPOperation::SetServerSystem(const char* reply, int replySize) noexcept
{
    CALL_STACK_MESSAGE2("CFTPOperation::SetServerSystem(, %d)", replySize);
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL result = TRUE;
    if (ServerSystem.empty())
    {
        std::string staged;
        result = CopyStr(staged, reply, replySize);
        if (result)
            ServerSystem.swap(staged);
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return result;
}

BOOL CFTPOperation::SetServerFirstReply(const char* reply, int replySize) noexcept
{
    CALL_STACK_MESSAGE2("CFTPOperation::SetServerFirstReply(, %d)", replySize);
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL result = TRUE;
    if (ServerFirstReply.empty())
    {
        std::string staged;
        result = CopyStr(staged, reply, replySize);
        if (result)
            ServerFirstReply.swap(staged);
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return result;
}

BOOL CFTPOperation::PrepareNextScriptCmd(std::string& command, std::string& logCommand, int* cmdLen,
                                         const char** proxyScriptExecPoint, int proxyScriptLastCmdReply,
                                         std::string& errorDescription, BOOL* needUserInput)
{
    CALL_STACK_MESSAGE1("CFTPOperation::PrepareNextScriptCmd()");
    HANDLES(EnterCriticalSection(&OperCritSect));

    *cmdLen = 0;
    errorDescription.clear();
    *needUserInput = FALSE;

    CProxyScriptParams proxyScriptParams(ProxyServer, Host.c_str(), Port, User.c_str(), Password.c_str(), Account.c_str(),
                                         Password.empty());
    std::string proxySendCmdBuf;
    CFTPSecureByteStringGuard proxySendCmdGuard(proxySendCmdBuf);
    std::string proxyLogCmdBuf;
    BOOL ret = TRUE;
    if (*proxyScriptExecPoint == NULL)
        *proxyScriptExecPoint = ProxyScriptStartExecPoint; // we should prepare the first script command
    BOOL lowMemory = FALSE;
    if (ProcessProxyScript(FtpLocalTextCodec(), ProxyScriptText.c_str(), proxyScriptExecPoint, proxyScriptLastCmdReply,
                           &proxyScriptParams, NULL, NULL, &proxySendCmdBuf,
                           &proxyLogCmdBuf, &errorDescription, NULL, &lowMemory))
    {
        if (proxyScriptParams.NeedUserInput()) // some details need to be entered (user, password, etc.)
        {
            *needUserInput = TRUE;
            int resID = 0;
            if (proxyScriptParams.NeedProxyHost)
            {
                resID = IDS_WORKERUNKNOWNPROXYHOST; // we do say we need it, but the user cannot provide it - if it is ever required (should not happen yet because the login in the panel succeeded even without ProxyHost, so it likely will not be needed here either), add a dialog for entering ProxyHost...
                TRACE_E("CFTPOperation::PrepareNextScriptCmd(): unexpected situation: ProxyHost is empty!");
            }
            if (proxyScriptParams.NeedProxyPassword)
                resID = IDS_WORKERUNKNOWNPROXYPASSWORD;
            if (proxyScriptParams.NeedUser)
                TRACE_E("CFTPOperation::PrepareNextScriptCmd(): unexpected situation: User is empty!");
            if (proxyScriptParams.NeedPassword)
                resID = IDS_WORKERUNKNOWNPASSWD;
            if (proxyScriptParams.NeedAccount)
                resID = IDS_WORKERUNKNOWNACCOUNT;
            if (resID != 0)
                ret = FtpStoreProtocolBytes(LoadStr(resID), errorDescription);
        }
        command.swap(proxySendCmdBuf);
        logCommand.swap(proxyLogCmdBuf);
        *cmdLen = static_cast<int>(command.size());
    }
    else // stored scripts are validated; FALSE without a description is allocation failure
    {
        ret = FALSE;
        if (lowMemory && errorDescription.empty())
            FtpStoreProtocolBytes(LoadStr(IDS_OPERDOPPR_LOWMEM), errorDescription);
    }

    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::AllocProxyForDataCon(CFTPProxyForDataCon** newDataConProxyServer)
{
    CALL_STACK_MESSAGE1("CFTPOperation::AllocProxyForDataCon()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    *newDataConProxyServer = ProxyServer == NULL ? NULL : ProxyServer->AllocProxyForDataCon(ServerIP, Host.c_str(), HostIP, Port);
    BOOL ret = ProxyServer == NULL || *newDataConProxyServer != NULL;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::GetRetryLoginWithoutAsking()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetRetryLoginWithoutAsking()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = RetryLoginWithoutAsking;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::GetInitFTPCommands(std::string& commands)
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetInitFTPCommands(,)");
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL result = FTPFormatString(commands, "%s", InitFTPCommands.c_str());
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return result;
}

BOOL CFTPOperation::GetLoginErrorDlgInfo(std::wstring& user, std::wstring& password,
                                         std::wstring& account, BOOL* retryLoginWithoutAsking,
                                         BOOL* proxyUsed, std::wstring& proxyUser,
                                         std::wstring& proxyPassword) noexcept
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetLoginErrorDlgInfo()");
    std::wstring anonymousPasswordW;
    if (!Config.GetAnonymousPasswd(anonymousPasswordW))
        return FALSE;
    std::wstring stagedUser;
    std::wstring stagedPassword;
    std::wstring stagedAccount;
    std::wstring stagedProxyUser;
    std::wstring stagedProxyPassword;
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL result = FtpStoreWideText(User.empty() ? L"anonymous" : User, stagedUser) &&
                  FtpStoreWideText(Password.empty() && User.empty() ? anonymousPasswordW : Password, stagedPassword) &&
                  FtpStoreWideText(Account, stagedAccount);
    *proxyUsed = ProxyServer != NULL;
    if (ProxyServer != NULL)
        result = result && FtpStoreWideText(ProxyServer->ProxyUser, stagedProxyUser) &&
                 FtpStoreWideText(ProxyServer->ProxyPlainPassword, stagedProxyPassword);
    *retryLoginWithoutAsking = RetryLoginWithoutAsking;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    FTPSecureWipe(anonymousPasswordW);
    if (!result)
    {
        FTPSecureWipe(stagedPassword);
        FTPSecureWipe(stagedProxyPassword);
        return FALSE;
    }
    user.swap(stagedUser);
    password.swap(stagedPassword);
    account.swap(stagedAccount);
    proxyUser.swap(stagedProxyUser);
    proxyPassword.swap(stagedProxyPassword);
    FTPSecureWipe(stagedPassword);
    FTPSecureWipe(stagedProxyPassword);
    return TRUE;
}

BOOL CFTPOperation::SetLoginErrorDlgInfo(const wchar_t* password, const wchar_t* account,
                                         BOOL retryLoginWithoutAsking, BOOL proxyUsed,
                                         const wchar_t* proxyUser, const wchar_t* proxyPassword) noexcept
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetLoginErrorDlgInfo()");
    std::wstring stagedPassword;
    std::wstring stagedAccount;
    if (!FtpStoreWideText(password != NULL ? password : L"", stagedPassword) ||
        !FtpStoreWideText(account != NULL ? account : L"", stagedAccount))
        return FALSE;
    HANDLES(EnterCriticalSection(&OperCritSect));
    Password.swap(stagedPassword);
    Account.swap(stagedAccount);
    RetryLoginWithoutAsking = retryLoginWithoutAsking;
    if (proxyUsed && ProxyServer != NULL)
    {
        ProxyServer->SetProxyUser(proxyUser);
        ProxyServer->SetProxyPassword(proxyPassword);
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    FTPSecureWipe(stagedPassword);
    FTPSecureWipe(stagedAccount);
    return TRUE;
}

void CFTPOperation::ReportWorkerChange(int workerID, BOOL reportProgressChange)
{
    CALL_STACK_MESSAGE1("CFTPOperation::ReportWorkerChange()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (OperationDlg != NULL && OperationDlg->HWindow != NULL) // only if the dialog is open
    {
        if (reportProgressChange)
            ReportProgressChange = TRUE;
        if (ReportChangeInWorkerID == -2) // not reporting any changes yet
        {
            ReportChangeInWorkerID = workerID;
            PostMessage(OperationDlg->HWindow, WM_APP_WORKERCHANGEREP, 0, 0); // notify the dialog that it should fetch the changes
        }
        else
        {
            if (ReportChangeInWorkerID != workerID)
                ReportChangeInWorkerID = -1; // changes in more than one worker
        }
    }
    else
        ReportChangeInWorkerID = -2; // the dialog is not open, reporting changes makes no sense
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

int CFTPOperation::GetChangedWorker(BOOL* reportProgressChange)
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetChangedWorker()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = ReportChangeInWorkerID == -2 ? -1 : ReportChangeInWorkerID; // -2 should not occur (but to be safe, return "change in all")
    ReportChangeInWorkerID = -2;
    if (reportProgressChange != NULL)
        *reportProgressChange = ReportProgressChange;
    ReportProgressChange = FALSE;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::ReportItemChange(int itemUID)
{
    CALL_STACK_MESSAGE1("CFTPOperation::ReportItemChange()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (OperationDlg != NULL && OperationDlg->HWindow != NULL) // only if the dialog is open
    {
        if (ReportChangeInItemUID == -3) // not reporting any changes yet
        {
            if (itemUID != -1)
            {
                ReportChangeInItemUID2 = itemUID;
                ReportChangeInItemUID = -2;
            }
            else
                ReportChangeInItemUID = -1;                                 // reporting multiple changes
            PostMessage(OperationDlg->HWindow, WM_APP_ITEMCHANGEREP, 0, 0); // notify the dialog that it should fetch the changes
        }
        else
        {
            if (itemUID != -1)
            {
                if (ReportChangeInItemUID == -2) // currently reporting one change
                {
                    if (ReportChangeInItemUID2 != itemUID)
                        ReportChangeInItemUID = itemUID; // already reporting two changes
                }
                else
                {
                    if (ReportChangeInItemUID != itemUID && ReportChangeInItemUID2 != itemUID)
                        ReportChangeInItemUID = -1; // changes in more than two items
                }
            }
            else
                ReportChangeInItemUID = -1; // reporting multiple changes
        }
    }
    else
        ReportChangeInItemUID = -3; // the dialog is not open, reporting changes makes no sense
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::GetChangedItems(int* firstUID, int* secondUID)
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetChangedItems()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (ReportChangeInItemUID == -1 || ReportChangeInItemUID == -3)
    { // -3 should not occur (but to be safe, return "change in all")
        *firstUID = -1;
        *secondUID = -1;
    }
    else
    {
        if (ReportChangeInItemUID == -2)
        {
            *firstUID = ReportChangeInItemUID2;
            *secondUID = -1;
        }
        else
        {
            *firstUID = ReportChangeInItemUID2;
            *secondUID = ReportChangeInItemUID;
        }
    }
    ReportChangeInItemUID = -3;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::ReportOperationStateChange()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFTPOperation::ReportOperationStateChange()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (OperationDlg != NULL && OperationDlg->HWindow != NULL && !OperStateChangedPosted)
    {                                                                     // only if the dialog is open and the report has not been posted yet (PostMessage)
        PostMessage(OperationDlg->HWindow, WM_APP_OPERSTATECHANGE, 0, 0); // notify the dialog that it should fetch the changes
        OperStateChangedPosted = TRUE;
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

COperationState
CFTPOperation::GetOperationState(BOOL calledFromSetupCloseButton)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFTPOperation::GetOperationState()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (calledFromSetupCloseButton)
        OperStateChangedPosted = FALSE; // it again makes sense to send messages about state changes
    COperationState state;
    if (ChildItemsNotDone - ChildItemsSkipped - ChildItemsFailed - ChildItemsUINeeded > 0)
        state = opstInProgress;
    else if (ChildItemsFailed + ChildItemsUINeeded > 0)
        state = opstFinishedWithErrors;
    else
    {
        if (ChildItemsSkipped > 0)
            state = opstFinishedWithSkips; // if there is no Skip at the operation level, there must be a ForcedToFail at the operation level -> the result is opstFinishedWithErrors
        else
            state = opstSuccessfullyFinished;
    }
    if (calledFromSetupCloseButton)
        LastReportedOperState = state;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return state;
}

void CFTPOperation::DebugGetCounters(int* childItemsNotDone, int* childItemsSkipped,
                                     int* childItemsFailed, int* childItemsUINeeded)
{
    CALL_STACK_MESSAGE1("CFTPOperation::DebugGetCounters()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    *childItemsNotDone = ChildItemsNotDone;
    *childItemsSkipped = ChildItemsSkipped;
    *childItemsFailed = ChildItemsFailed;
    *childItemsUINeeded = ChildItemsUINeeded;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::PostChangeOnPathNotifications(BOOL softRefresh)
{
    CALL_STACK_MESSAGE1("CFTPOperation::PostChangeOnPathNotifications()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (SrcPathCanChange)
    {
        TRACE_I("PostChangeOnPathNotification: soft: " << softRefresh << ", source subdirs: " << SrcPathCanChangeInclSubdirs);
        BOOL isDiskPath = RemoteSourcePath == NULL;
        SalamanderGeneral->PostChangeOnPathNotification(SourcePath, SrcPathCanChangeInclSubdirs | (isDiskPath ? 0 : (softRefresh ? 0x02 /* soft refresh */ : 0)));
    }
    if (TgtPathCanChange)
    {
        TRACE_I("PostChangeOnPathNotification: soft: " << softRefresh << ", target subdirs: " << TgtPathCanChangeInclSubdirs);
        BOOL isDiskPath = RemoteTargetPath == NULL;
        SalamanderGeneral->PostChangeOnPathNotification(TargetPath, TgtPathCanChangeInclSubdirs | (isDiskPath ? 0 : (softRefresh ? 0x02 /* soft refresh */ : 0)));
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

CFTPOperationType
CFTPOperation::GetOperationType()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetOperationType()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    CFTPOperationType type = Type;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return type;
}

int CFTPOperation::GetCopyProgress(CQuadWord* downloaded, CQuadWord* total, CQuadWord* waiting,
                                   int* unknownSizeCount, int* errorsCount, int* doneOrSkippedCount,
                                   int* totalCount, CFTPQueue* queue)
{
    int progress = -1;
    total->Set(0, 0);
    waiting->Set(0, 0);
    CQuadWord totalWithoutErrors;
    queue->GetCopyProgressInfo(downloaded, unknownSizeCount, &totalWithoutErrors,
                               errorsCount, doneOrSkippedCount, totalCount, this);
    WorkersList.AddCurrentDownloadSize(downloaded);
    if (totalWithoutErrors >= *downloaded)
        *waiting = totalWithoutErrors - *downloaded;
    // else; occurs on servers where the byte size is unknown (the download is in progress, but we do not yet know the estimated total size)

    HANDLES(EnterCriticalSection(&OperCritSect));
    if (TotalSizeInBlocks != CQuadWord(0, 0))
    {
        if (GetApproxByteSize(total, TotalSizeInBlocks))
            *total += TotalSizeInBytes;
    }
    else
        *total += TotalSizeInBytes;
    if (*total != CQuadWord(0, 0))
    {
        if (*total < *downloaded)
            *total = *downloaded;
        progress = (int)((CQuadWord(1000, 0) * *downloaded) / *total).Value;
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return progress;
}

int CFTPOperation::GetCopyUploadProgress(CQuadWord* uploaded, CQuadWord* total, CQuadWord* waiting,
                                         int* unknownSizeCount, int* errorsCount, int* doneOrSkippedCount,
                                         int* totalCount, CFTPQueue* queue)
{
    int progress = -1;
    total->Set(0, 0);
    waiting->Set(0, 0);
    CQuadWord totalWithoutErrors;
    queue->GetCopyUploadProgressInfo(uploaded, unknownSizeCount, &totalWithoutErrors,
                                     errorsCount, doneOrSkippedCount, totalCount, this);
    WorkersList.AddCurrentUploadSize(uploaded);
    if (totalWithoutErrors >= *uploaded)
        *waiting = totalWithoutErrors - *uploaded;
    // else; happens for example with ASCII transfers where the source file is smaller than the target (a file with LF line endings uploaded to a Windows FTP server)

    HANDLES(EnterCriticalSection(&OperCritSect));
    *total = TotalSizeInBytes;
    if (*total != CQuadWord(0, 0))
    {
        if (*total < *uploaded)
            *total = *uploaded;
        progress = (int)((CQuadWord(1000, 0) * *uploaded) / *total).Value;
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return progress;
}

void CFTPOperation::AddBlkSizeInfo(CQuadWord const& sizeInBytes, CQuadWord const& sizeInBlocks)
{
    CALL_STACK_MESSAGE1("CFTPOperation::AddBlkSizeInfo()");

    // block size can only be detected for files larger than one block (otherwise we risk determining a smaller
    // block size - e.g. a 1 byte file would be reported as 1 block regardless of the actual block size)
    if (sizeInBlocks > CQuadWord(1, 0))
    {
        HANDLES(EnterCriticalSection(&OperCritSect));
        BlkSizeTotalInBytes += sizeInBytes;
        BlkSizeTotalInBlocks += sizeInBlocks;
        if (BlkSizeActualValue == -1)
            BlkSizeActualValue = 1024; // the most common value
        // adjust the current size so that it becomes the nearest higher power of two (1, 2, 4, 8, 16, 32, ...)
        while (1) // BlkSizeTotalInBlocks must not be zero (otherwise it would be an infinite loop), which holds because sizeInBlocks > CQuadWord(1, 0) and BlkSizeTotalInBlocks is initialized to zero
        {
            if (CQuadWord(BlkSizeActualValue / 2, 0) * BlkSizeTotalInBlocks >= BlkSizeTotalInBytes)
                BlkSizeActualValue /= 2;
            else
            {
                if (CQuadWord(BlkSizeActualValue, 0) * BlkSizeTotalInBlocks < BlkSizeTotalInBytes)
                    BlkSizeActualValue *= 2;
                else
                    break;
            }
        }
        HANDLES(LeaveCriticalSection(&OperCritSect));
    }
}

BOOL CFTPOperation::GetApproxByteSize(CQuadWord* sizeInBytes, CQuadWord const& sizeInBlocks)
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetApproxByteSize()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = BlkSizeActualValue != -1;
    if (ret)
        *sizeInBytes = sizeInBlocks * CQuadWord(BlkSizeActualValue, 0);
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::IsBlkSizeKnown()
{
    CALL_STACK_MESSAGE1("CFTPOperation::IsBlkSizeKnown()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = BlkSizeActualValue != -1;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

DWORD
CFTPOperation::GetElapsedSeconds()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetElapsedSeconds()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    DWORD ret;
    if (OperationEnd == -1)
        ret = GetTickCount() - OperationStart;
    else
        ret = OperationEnd - OperationStart;
    ret /= 1000;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::GetDataActivityInLastPeriod()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetDataActivityInLastPeriod()");
    return (GetTickCount() - GlobalLastActivityTime.Get()) <= WORKER_STATUSUPDATETIMEOUT;
}

BOOL CFTPOperation::GetTargetPath(std::wstring& path) noexcept
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetTargetPath()");

    std::wstring staged;
    BOOL ok = FALSE;
    HANDLES(EnterCriticalSection(&OperCritSect));
    try
    {
        staged = TargetPath != NULL ? TargetPath : L"";
        ok = TRUE;
    }
    catch (...)
    {
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    if (ok)
        path.swap(staged);
    return ok;
}

void CFTPOperation::GetDiskOperDefaults(CFTPDiskWork* diskWork)
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetDiskOperDefaults()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    diskWork->CannotCreateDir = CannotCreateDir;
    diskWork->DirAlreadyExists = DirAlreadyExists;
    diskWork->CannotCreateFile = CannotCreateFile;
    diskWork->FileAlreadyExists = FileAlreadyExists;
    diskWork->RetryOnCreatedFile = RetryOnCreatedFile;
    diskWork->RetryOnResumedFile = RetryOnResumedFile;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

int CFTPOperation::GetCannotCreateDir()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetCannotCreateDir()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = CannotCreateDir;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetDirAlreadyExists()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetDirAlreadyExists()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = DirAlreadyExists;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetCannotCreateFile()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetCannotCreateFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = CannotCreateFile;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetFileAlreadyExists()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetFileAlreadyExists()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = FileAlreadyExists;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetRetryOnCreatedFile()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetRetryOnCreatedFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = RetryOnCreatedFile;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetRetryOnResumedFile()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetRetryOnResumedFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = RetryOnResumedFile;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetUnknownAttrs()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUnknownAttrs()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = UnknownAttrs;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::GetResumeIsNotSupported()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetResumeIsNotSupported()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = ResumeIsNotSupported;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::GetDataConWasOpenedForAppendCmd()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetDataConWasOpenedForAppendCmd()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = DataConWasOpenedForAppendCmd;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetAsciiTrModeButBinFile()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetAsciiTrModeButBinFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = AsciiTrModeButBinFile;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetUploadCannotCreateDir()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUploadCannotCreateDir()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = UploadCannotCreateDir;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetUploadDirAlreadyExists()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUploadDirAlreadyExists()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = UploadDirAlreadyExists;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetUploadCannotCreateFile()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUploadCannotCreateFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = UploadCannotCreateFile;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetUploadFileAlreadyExists()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUploadFileAlreadyExists()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = UploadFileAlreadyExists;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetUploadRetryOnCreatedFile()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUploadRetryOnCreatedFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = UploadRetryOnCreatedFile;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetUploadRetryOnResumedFile()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUploadRetryOnResumedFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = UploadRetryOnResumedFile;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

int CFTPOperation::GetUploadAsciiTrModeButBinFile()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUploadAsciiTrModeButBinFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret = UploadAsciiTrModeButBinFile;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::SetCertificate(CCertificate* certificate)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetCertificate()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    CCertificate* old = pCertificate; // ensures AddRef is called via Release (in case pCertificate == certificate)
    pCertificate = certificate;
    if (pCertificate)
        pCertificate->AddRef();
    if (old)
        old->Release();
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

CCertificate*
CFTPOperation::GetCertificate()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetCertificate()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    CCertificate* ret = pCertificate;
    if (ret != NULL)
        ret->AddRef();
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::SetCannotCreateDir(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetCannotCreateDir()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    CannotCreateDir = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetDirAlreadyExists(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetDirAlreadyExists()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    DirAlreadyExists = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetCannotCreateFile(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetCannotCreateFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    CannotCreateFile = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetFileAlreadyExists(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetFileAlreadyExists()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    FileAlreadyExists = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetRetryOnCreatedFile(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetRetryOnCreatedFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    RetryOnCreatedFile = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetRetryOnResumedFile(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetRetryOnResumedFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    RetryOnResumedFile = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetUnknownAttrs(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetUnknownAttrs()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    UnknownAttrs = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetResumeIsNotSupported(BOOL value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetResumeIsNotSupported()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    ResumeIsNotSupported = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetDataConWasOpenedForAppendCmd(BOOL value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetDataConWasOpenedForAppendCmd()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    DataConWasOpenedForAppendCmd = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetAsciiTrModeButBinFile(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetAsciiTrModeButBinFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    AsciiTrModeButBinFile = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetUploadCannotCreateDir(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetUploadCannotCreateDir()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    UploadCannotCreateDir = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetUploadDirAlreadyExists(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetUploadDirAlreadyExists()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    UploadDirAlreadyExists = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetUploadCannotCreateFile(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetUploadCannotCreateFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    UploadCannotCreateFile = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetUploadFileAlreadyExists(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetUploadFileAlreadyExists()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    UploadFileAlreadyExists = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetUploadRetryOnCreatedFile(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetUploadRetryOnCreatedFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    UploadRetryOnCreatedFile = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetUploadRetryOnResumedFile(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetUploadRetryOnResumedFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    UploadRetryOnResumedFile = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetUploadAsciiTrModeButBinFile(int value)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetUploadAsciiTrModeButBinFile()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    UploadAsciiTrModeButBinFile = value;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::PostNewWorkAvailable(BOOL onlyOneItem)
{
    CALL_STACK_MESSAGE1("CFTPOperation::PostNewWorkAvailable(,)");
    WorkersList.PostNewWorkAvailable(onlyOneItem); // synchronization is inside WorkersList (the OperCritSect section is not needed here)
}

BOOL CFTPOperation::GiveWorkToSleepingConWorker(CFTPWorker* sourceWorker)
{
    CALL_STACK_MESSAGE1("CFTPOperation::GiveWorkToSleepingConWorker()");
    return WorkersList.GiveWorkToSleepingConWorker(sourceWorker); // synchronization is inside WorkersList (the OperCritSect section is not needed here)
}

CFTPServerPathType
CFTPOperation::GetFTPServerPathType(const char* path)
{
    CALL_STACK_MESSAGE2("CFTPOperation::GetFTPServerPathType(%s)", path);

    HANDLES(EnterCriticalSection(&OperCritSect));
    CFTPServerPathType type = ::GetFTPServerPathType(ServerFirstReply.c_str(), ServerSystem.c_str(), path);
    HANDLES(LeaveCriticalSection(&OperCritSect));

    return type;
}

CFtpTextCodec CFTPOperation::GetPathTextCodec()
{
    HANDLES(EnterCriticalSection(&OperCritSect));
    const CFtpTextCodec codec = IdentityCodec;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return codec;
}

BOOL CFTPOperation::IsServerSystem(const char* systemName)
{
    CALL_STACK_MESSAGE1("CFTPOperation::IsServerSystem()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    const std::string_view sysName = FTPGetServerSystem(ServerSystem.c_str());
    HANDLES(LeaveCriticalSection(&OperCritSect));

    return systemName != NULL && FtpEqualAsciiTokenNoCase(sysName, systemName);
}

BOOL CFTPOperation::IsAlreadyExploredPath(const char* path)
{
    CALL_STACK_MESSAGE2("CFTPOperation::IsAlreadyExploredPath(%s)", path);
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = ExploredPaths.ContainsPath(path);
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::AddToExploredPaths(const char* path)
{
    CALL_STACK_MESSAGE2("CFTPOperation::AddToExploredPaths(%s)", path);
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = ExploredPaths.AddPath(path);
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::GetUsePassiveMode()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUsePassiveMode()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = UsePassiveMode;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::SetUsePassiveMode(BOOL usePassiveMode)
{
    CALL_STACK_MESSAGE2("CFTPOperation::SetUsePassiveMode(%d)", usePassiveMode);
    HANDLES(EnterCriticalSection(&OperCritSect));
    UsePassiveMode = usePassiveMode;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

BOOL CFTPOperation::GetSizeCmdIsSupported()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetSizeCmdIsSupported()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = SizeCmdIsSupported;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::SetSizeCmdIsSupported(BOOL sizeCmdIsSupported)
{
    CALL_STACK_MESSAGE2("CFTPOperation::SetSizeCmdIsSupported(%d)", sizeCmdIsSupported);
    HANDLES(EnterCriticalSection(&OperCritSect));
    SizeCmdIsSupported = sizeCmdIsSupported;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

BOOL CFTPOperation::GetListCommand(std::string& command)
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetListCommand()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL result = FTPFormatString(command, "%s\r\n", !ListCommand.empty() ? ListCommand.c_str() : LIST_CMD_TEXT);
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return result;
}

BOOL CFTPOperation::GetUseListingsCache()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUseListingsCache()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    BOOL ret = UseListingsCache;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

char* CFTPOperation::AllocServerSystemReply()
{
    CALL_STACK_MESSAGE1("CFTPOperation::AllocServerSystemReply()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    char* ret = DupOperationText(ServerSystem.c_str());
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

char* CFTPOperation::AllocServerFirstReply()
{
    CALL_STACK_MESSAGE1("CFTPOperation::AllocServerFirstReply()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    char* ret = DupOperationText(ServerFirstReply.c_str());
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::GetListingServerType(std::string& type) noexcept
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetListingServerType()");
    std::string staged;
    HANDLES(EnterCriticalSection(&OperCritSect));
    const BOOL ret = FtpStoreLocalTextBytes(ListingServerType, staged);
    HANDLES(LeaveCriticalSection(&OperCritSect));
    if (ret)
        type.swap(staged);
    return ret;
}

int CFTPOperation::GetTransferMode()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetTransferMode()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    int ret;
    if (AutodetectTrMode)
        ret = trmAutodetect;
    else
    {
        if (UseAsciiTransferMode)
            ret = trmASCII;
        else
            ret = trmBinary;
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::GetParamsForChAttrsOper(BOOL* selFiles, BOOL* selDirs, BOOL* includeSubdirs,
                                            DWORD* attrAndMask, DWORD* attrOrMask,
                                            int* operationsUnknownAttrs)
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetParamsForChAttrsOper()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    *selFiles = ChAttrOfFiles;
    *selDirs = ChAttrOfDirs;
    *includeSubdirs = SrcPathCanChangeInclSubdirs;
    *attrAndMask = AttrAnd;
    *attrOrMask = AttrOr;
    *operationsUnknownAttrs = UnknownAttrs;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::AddToItemOrOperationCounters(int itemDir, int childItemsNotDone,
                                                 int childItemsSkipped, int childItemsFailed,
                                                 int childItemsUINeeded, BOOL onlyUINeededOrFailedToSkipped)
{
    SLOW_CALL_STACK_MESSAGE1("CFTPOperation::AddToItemOrOperationCounters()");
    if (childItemsNotDone != 0 || childItemsSkipped != 0 || childItemsFailed != 0 || childItemsUINeeded != 0)
    {
        if (itemDir == -1)
        {
            AddToNotDoneSkippedFailed(childItemsNotDone, childItemsSkipped, childItemsFailed,
                                      childItemsUINeeded, onlyUINeededOrFailedToSkipped);
        }
        else
        {
            HANDLES(EnterCriticalSection(&OperCritSect));
            CFTPQueue* queue = Queue;
            HANDLES(LeaveCriticalSection(&OperCritSect));
            if (queue != NULL) // "always true"
            {
                queue->AddToNotDoneSkippedFailed(itemDir, childItemsNotDone,
                                                 childItemsSkipped, childItemsFailed, childItemsUINeeded, this);
            }
            else
                TRACE_E("Unexpected situation in CFTPOperation::AddToItemOrOperationCounters(): Queue is NULL!");
        }
    }
}

void CFTPOperation::GetParamsForDeleteOper(int* confirmDelOnNonEmptyDir, int* confirmDelOnHiddenFile,
                                           int* confirmDelOnHiddenDir)
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetParamsForDeleteOper()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    if (confirmDelOnNonEmptyDir != NULL)
        *confirmDelOnNonEmptyDir = ConfirmDelOnNonEmptyDir;
    if (confirmDelOnHiddenFile != NULL)
        *confirmDelOnHiddenFile = ConfirmDelOnHiddenFile;
    if (confirmDelOnHiddenDir != NULL)
        *confirmDelOnHiddenDir = ConfirmDelOnHiddenDir;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

void CFTPOperation::SetParamsForDeleteOper(int* confirmDelOnNonEmptyDir, int* confirmDelOnHiddenFile,
                                           int* confirmDelOnHiddenDir)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SetParamsForDeleteOper()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    if (confirmDelOnNonEmptyDir != NULL)
        ConfirmDelOnNonEmptyDir = *confirmDelOnNonEmptyDir;
    if (confirmDelOnHiddenFile != NULL)
        ConfirmDelOnHiddenFile = *confirmDelOnHiddenFile;
    if (confirmDelOnHiddenDir != NULL)
        ConfirmDelOnHiddenDir = *confirmDelOnHiddenDir;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

DWORD
CFTPOperation::GiveLastErrorOccurenceTime()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GiveLastErrorOccurenceTime()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    DWORD ret = ++LastErrorOccurenceTime;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::SearchWorkerWithNewError(int* index)
{
    CALL_STACK_MESSAGE1("CFTPOperation::SearchWorkerWithNewError()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    DWORD lastErrorOccurenceTime = LastErrorOccurenceTime;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return WorkersList.SearchWorkerWithNewError(index, lastErrorOccurenceTime);
}

BOOL CFTPOperation::CanMakeChangesOnPath(const wchar_t* user, const wchar_t* host, unsigned short port,
                                         const char* path, CFTPServerPathType pathType)
{
    CALL_STACK_MESSAGE1("CFTPOperation::CanMakeChangesOnPath()");
    BOOL ret = FALSE;
    HANDLES(EnterCriticalSection(&OperCritSect));
    const wchar_t* normalizedUser = user != NULL && *user != 0 ? user : L"anonymous";
    const BOOL identityMatches = host != NULL && !Host.empty() && Port == port &&
                                  User == normalizedUser &&
                                  CompareStringOrdinal(Host.c_str(), -1, host, -1, TRUE) == CSTR_EQUAL;
    if (identityMatches && SrcPathCanChange && RemoteSourcePath != NULL)
    {
        ret = FTPIsPrefixOfServerPath(pathType,
                                      RemoteSourcePath,
                                      path, !SrcPathCanChangeInclSubdirs);
    }
    if (!ret && identityMatches && TgtPathCanChange && RemoteTargetPath != NULL)
    {
        ret = FTPIsPrefixOfServerPath(pathType,
                                      RemoteTargetPath,
                                      path, !TgtPathCanChangeInclSubdirs);
    }
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

BOOL CFTPOperation::IsUploadingToServer(const wchar_t* user, const wchar_t* host, unsigned short port)
{
    CALL_STACK_MESSAGE1("CFTPOperation::IsUploadingToServer()");
    BOOL ret = FALSE;
    HANDLES(EnterCriticalSection(&OperCritSect));
    const wchar_t* normalizedUser = user != NULL && *user != 0 ? user : L"anonymous";
    const BOOL identityMatches = host != NULL && !Host.empty() && Port == port &&
                                  User == normalizedUser &&
                                  CompareStringOrdinal(Host.c_str(), -1, host, -1, TRUE) == CSTR_EQUAL;

    if (identityMatches && (Type == fotCopyUpload || Type == fotMoveUpload) && RemoteTargetPath != NULL)
        ret = TRUE;

    HANDLES(LeaveCriticalSection(&OperCritSect));
    return ret;
}

void CFTPOperation::GetUserHostPort(const wchar_t** user, const wchar_t*& host, unsigned short* port) noexcept
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUserHostPort()");
    HANDLES(EnterCriticalSection(&OperCritSect));
    host = Host.c_str();
    if (user != NULL)
        *user = User.empty() ? L"anonymous" : User.c_str();
    if (port != NULL)
        *port = Port;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}

//
// ****************************************************************************
// CFTPQueueItem
//

CFTPQueueItem::CFTPQueueItem()
{
    HANDLES(EnterCriticalSection(&NextItemUIDCritSect));
    UID = NextItemUID++;
    HANDLES(LeaveCriticalSection(&NextItemUIDCritSect));

    ParentUID = -1;
    Type = fqitNone;
    ProblemID = ITEMPR_OK;
    WinError = NO_ERROR;
    ErrAllocDescr = NULL;

    ErrorOccurenceTime = -1;

    ForceAction = fqiaNone;

    Path = NULL;
    Name = NULL;
    LocalPath = NULL;
    LocalName = NULL;
}

CFTPQueueItem::~CFTPQueueItem()
{
    if (Path != NULL)
        SalamanderGeneral->Free(Path);
    if (Name != NULL)
        SalamanderGeneral->Free(Name);
    if (LocalPath != NULL)
        SalamanderGeneral->Free(LocalPath);
    if (LocalName != NULL)
        SalamanderGeneral->Free(LocalName);
    if (ErrAllocDescr != NULL)
        SalamanderGeneral->Free(ErrAllocDescr);
}

void CFTPQueueItem::SetItem(int parentUID, CFTPQueueItemType type, CFTPQueueItemState state,
                            DWORD problemID, const char* path, const char* name)
{
    ParentUID = parentUID;
    Type = type;
    SetStateInternal(state);
    ProblemID = problemID;
    Path = DupOperationText(path);
    Name = DupOperationText(name);
}

void CFTPQueueItem::SetLocalItem(int parentUID, CFTPQueueItemType type, CFTPQueueItemState state,
                                 DWORD problemID, const wchar_t* path, const wchar_t* name)
{
    ParentUID = parentUID;
    Type = type;
    SetStateInternal(state);
    ProblemID = problemID;
    LocalPath = DupOperationWideText(path);
    LocalName = DupOperationWideText(name);
}

BOOL CFTPQueueItem::HasErrorToSolve(BOOL* canSkip, BOOL* canRetry)
{
    BOOL solvableErr = ProblemID != ITEMPR_INVALIDPATHTODIR && // not an unsolvable problem (no Retry can help)
                       ProblemID != ITEMPR_DIREXPLENDLESSLOOP &&
                       ProblemID != ITEMPR_INVALIDPATHTOLINK;
    if (canSkip != NULL)
    {
        *canSkip = solvableErr &&
                   (GetItemState() == sqisWaiting || GetItemState() == sqisFailed ||
                    GetItemState() == sqisUserInputNeeded);
    }
    if (GetItemState() >= sqisSkipped /* sqisSkipped, sqisFailed, sqisForcedToFail or sqisUserInputNeeded */ &&
        GetItemState() != sqisForcedToFail /* cannot be solved directly, it must be handled through child items */ &&
        solvableErr)
    {
        if (canRetry != NULL)
            *canRetry = TRUE;
        return ProblemID != ITEMPR_SKIPPEDBYUSER; // "Solve Error" for "skipped by user" makes no sense
    }
    else
    {
        if (canRetry != NULL)
            *canRetry = FALSE;
        return FALSE;
    }
}

BOOL CFTPQueueItem::GetProblemDescr(const CFtpTextCodec& codec, std::wstring& description) noexcept
{
    std::string message;
    std::string errorText;
    BOOL addErrAllocDescr = FALSE; // TRUE = if we have some server reply in ErrAllocDescr, append its first line to the message
    BOOL success = TRUE;
    switch (ProblemID)
    {
    case ITEMPR_OK:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_OK));
        break;
    case ITEMPR_LOWMEM:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_LOWMEM));
        break;

    case ITEMPR_CANNOTCREATETGTFILE:
    case ITEMPR_CANNOTCREATETGTDIR:
    case ITEMPR_TGTFILEREADERROR:
    case ITEMPR_SRCFILEREADERROR:
    case ITEMPR_TGTFILEWRITEERROR:
    case ITEMPR_UPLOADCANNOTLISTSRCPATH:
    case ITEMPR_UNABLETODELETEDISKDIR:
    case ITEMPR_UNABLETODELETEDISKFILE:
    case ITEMPR_UPLOADCANNOTOPENSRCFILE:
    {
        if (WinError != NO_ERROR)
            success = FTPGetErrorText(WinError, errorText);
        else
            success = FtpStoreProtocolBytes(LoadStr(IDS_UNKNOWNERROR), errorText);
        while (!errorText.empty() && (errorText.back() == '\r' || errorText.back() == '\n' || errorText.back() == '.'))
            errorText.pop_back();
        int resID = 0;
        switch (ProblemID)
        {
        case ITEMPR_CANNOTCREATETGTFILE: resID = IDS_OPERDOPPR_CANTCRTGTFILE; break;
        case ITEMPR_CANNOTCREATETGTDIR: resID = IDS_OPERDOPPR_CANTCRTGTDIR; break;
        case ITEMPR_TGTFILEREADERROR: resID = IDS_OPERDOPPR_TGTFILEREADERROR; break;
        case ITEMPR_SRCFILEREADERROR: resID = IDS_OPERDOPPR_SRCFILEREADERROR; break;
        case ITEMPR_TGTFILEWRITEERROR: resID = IDS_OPERDOPPR_TGTFILEWRITEERROR; break;
        case ITEMPR_UPLOADCANNOTLISTSRCPATH: resID = IDS_OPERDOPPR_UPLCANTLISTSRCPATH; break;
        case ITEMPR_UNABLETODELETEDISKDIR: resID = IDS_OPERDOPPR_UPLCANTDELSRCDISKDIR; break;
        case ITEMPR_UPLOADCANNOTOPENSRCFILE: resID = IDS_OPERDOPPR_UPLCANNOTOPENSRCFILE; break;
        case ITEMPR_UNABLETODELETEDISKFILE: resID = IDS_OPERDOPPR_UPLCANTDELSRCDISKFILE; break;
        }
        success = success && FTPFormatString(message, LoadStr(resID), errorText.c_str());
        break;
    }

    case ITEMPR_TGTFILEALREADYEXISTS:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_TGTFILEEXISTS));
        break;
    case ITEMPR_TGTDIRALREADYEXISTS:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_TGTDIREXISTS));
        break;

    case ITEMPR_RETRYONCREATFILE:
    case ITEMPR_RETRYONRESUMFILE:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_TRANSFERFAILED));
        break;

    case ITEMPR_ASCIITRFORBINFILE:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_ASCIITRBINFILE));
        break;

    case ITEMPR_UNKNOWNATTRS:
    {
        const char* attrs;
        switch (Type)
        {
        case fqitChAttrsFile: attrs = ((CFTPQueueItemChAttr*)this)->OrigRights; break;
        case fqitChAttrsDir: attrs = ((CFTPQueueItemChAttrDir*)this)->OrigRights; break;
        case fqitChAttrsExploreDir: attrs = ((CFTPQueueItemChAttrExplore*)this)->OrigRights; break;
        default:
            TRACE_E("Unexpected situation in CFTPQueueItem::GetProblemDescr(): ITEMPR_UNKNOWNATTRS with unknown original attributes!");
            attrs = "???";
            break;
        }
        if (attrs == NULL)
            attrs = LoadStr(IDS_OPERDOPPR_UNKEXISTATTR);
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UNKNOWNATTRS), attrs);
        break;
    }

    case ITEMPR_INVALIDPATHTODIR:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_INVALIDPATHTODIR));
        break;
    case ITEMPR_INVALIDPATHTOLINK:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_INVALIDPATHTOLINK));
        break;

    case ITEMPR_UNABLETOCWD:
    case ITEMPR_UNABLETOCWDONLYPATH:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UNABLETOCWD));
        addErrAllocDescr = TRUE;
        break;

    case ITEMPR_UNABLETOPWD:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UNABLETOPWD));
        addErrAllocDescr = TRUE;
        break;

    case ITEMPR_DIREXPLENDLESSLOOP:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_DIREXPLENDLESSLOOP));
        break;

    case ITEMPR_LISTENFAILURE:
        if (WinError != NO_ERROR)
            success = FTPGetErrorText(WinError, errorText);
        else
            success = FtpStoreProtocolBytes(LoadStr(IDS_UNKNOWNERROR), errorText);
        while (!errorText.empty() && (errorText.back() == '\r' || errorText.back() == '\n' || errorText.back() == '.'))
            errorText.pop_back();
        success = success && FTPFormatString(message, LoadStr(IDS_OPERDOPPR_LISTENFAILURE), errorText.c_str());
        break;

    case ITEMPR_UPLOADCANNOTLISTTGTPATH:
    case ITEMPR_INCOMPLETELISTING:
    case ITEMPR_INCOMPLETEDOWNLOAD:
    case ITEMPR_INCOMPLETEUPLOAD:
        if (ErrAllocDescr != NULL)
        {
            success = FTPFormatString(message,
                                      LoadStr(ProblemID == ITEMPR_INCOMPLETELISTING ? IDS_OPERDOPPR_INCOMPLLISTING2 : ProblemID == ITEMPR_INCOMPLETEDOWNLOAD ? IDS_OPERDOPPR_INCOMPLDOWNLOAD2
                                                                                                                  : ProblemID == ITEMPR_INCOMPLETEUPLOAD     ? IDS_OPERDOPPR_INCOMPLUPLOAD2
                                                                                                                                                             : IDS_OPERDOPPR_UPLCANTLISTTGTPATH2));
            addErrAllocDescr = TRUE;
        }
        else
        {
            if (WinError != NO_ERROR)
                success = FTPGetErrorText(WinError, errorText);
            else
            {
                if (ProblemID == ITEMPR_UPLOADCANNOTLISTTGTPATH)
                    errorText.clear();
                else
                    success = FtpStoreProtocolBytes(LoadStr(IDS_UNKNOWNERROR), errorText);
            }
            while (!errorText.empty() && (errorText.back() == '\r' || errorText.back() == '\n' || errorText.back() == '.'))
                errorText.pop_back();
            success = success && FTPFormatString(message,
                                                 LoadStr(ProblemID == ITEMPR_INCOMPLETELISTING ? IDS_OPERDOPPR_INCOMPLLISTING1 : ProblemID == ITEMPR_INCOMPLETEDOWNLOAD ? IDS_OPERDOPPR_INCOMPLDOWNLOAD1
                                                                                                                             : ProblemID == ITEMPR_INCOMPLETEUPLOAD     ? IDS_OPERDOPPR_INCOMPLUPLOAD1
                                                                                                                              : errorText.empty()                        ? IDS_OPERDOPPR_UPLCANTLISTTGTPATH
                                                                                                                                                                         : IDS_OPERDOPPR_UPLCANTLISTTGTPATH3),
                                                 errorText.c_str());
        }
        break;

    case ITEMPR_UNABLETOPARSELISTING:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UNABLETOPARSELISTING));
        break;

    case ITEMPR_DIRISHIDDEN: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_DIRISHIDDEN)); break;
    case ITEMPR_DIRISNOTEMPTY: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_DIRISNOTEMPTY)); break;
    case ITEMPR_FILEISHIDDEN: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_FILEISHIDDEN)); break;

    case ITEMPR_UNABLETORESOLVELNK:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UNABLETORESOLVELNK));
        addErrAllocDescr = TRUE;
        break;

    case ITEMPR_UNABLETODELETEFILE:
    case ITEMPR_UNABLETODELSRCFILE:
        success = FTPFormatString(message, LoadStr(ProblemID == ITEMPR_UNABLETODELETEFILE ? IDS_OPERDOPPR_UNABLETODELFILE : IDS_OPERDOPPR_UNABLETODELSRCFILE));
        addErrAllocDescr = TRUE;
        break;

    case ITEMPR_UNABLETODELETEDIR:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UNABLETODELDIR));
        addErrAllocDescr = TRUE;
        break;

    case ITEMPR_UNABLETOCHATTRS:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UNABLETOCHATTRS));
        addErrAllocDescr = TRUE;
        break;

    case ITEMPR_UNABLETORESUME: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UNABLETORESUME)); break;
    case ITEMPR_RESUMETESTFAILED: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_RESUMETESTFAILED)); break;

    case ITEMPR_UPLOADCANNOTCREATETGTDIR:
        if (ErrAllocDescr == NULL)
        {
            success = FTPFormatString(message, LoadStr(WinError == ERROR_ALREADY_EXISTS ? IDS_OPERDOPPR_UPLCANTCRTGTDIRFILEEX : IDS_OPERDOPPR_UPLCANTCRTGTDIRINV));
        }
        else
        {
            success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UPLCANTCRTGTDIR));
            addErrAllocDescr = TRUE;
        }
        break;

    case ITEMPR_UPLOADTGTDIRALREADYEXISTS: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UPLTGTDIREXISTS)); break;
    case ITEMPR_UPLOADCRDIRAUTORENFAILED:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UPLCRDIRAUTORENFAILED));
        addErrAllocDescr = TRUE;
        break;
    case ITEMPR_UPLOADFILEAUTORENFAILED:
        success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UPLFILEAUTORENFAILED));
        addErrAllocDescr = TRUE;
        break;

    case ITEMPR_UPLOADCANNOTCREATETGTFILE:
        if (ErrAllocDescr == NULL)
        {
            success = FTPFormatString(message, LoadStr(WinError == ERROR_ALREADY_EXISTS ? IDS_OPERDOPPR_UPLCANTCRTGTFILEDIREX : IDS_OPERDOPPR_UPLCANTCRTGTFILEINV));
        }
        else
        {
            success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UPLCANTCRTGTFILE));
            addErrAllocDescr = TRUE;
        }
        break;

    case ITEMPR_UPLOADTGTFILEALREADYEXISTS: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UPLTGTFILEEXISTS)); break;
    case ITEMPR_SRCFILEINUSE: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_SRCFILEINUSE)); break;
    case ITEMPR_TGTFILEINUSE: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_TGTFILEINUSE)); break;
    case ITEMPR_UPLOADASCIIRESUMENOTSUP: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UPLASCIIRESNOTSUP)); break;
    case ITEMPR_UPLOADUNABLETORESUMEUNKSIZ: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UPUNABLERESUNKSIZ)); break;
    case ITEMPR_UPLOADUNABLETORESUMEBIGTGT: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UPUNABLERESBIGTGT)); break;
    case ITEMPR_SKIPPEDBYUSER: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_SKIPPEDBYUSER)); break;
    case ITEMPR_UPLOADTESTIFFINISHEDNOTSUP: success = FTPFormatString(message, LoadStr(IDS_OPERDOPPR_UPLTESTIFFINNOTSUP)); break;

    default:
        TRACE_E("Unexpected situation in CFTPQueueItem::GetProblemDescr(): unknown ProblemID!");
        message.clear();
        break;
    }

    std::wstring staged;
    if (!success || !FtpDecodeLocalText(message, staged))
        return FALSE;
    if (addErrAllocDescr && ErrAllocDescr != NULL)
    {
        size_t detailLength = 0;
        while (ErrAllocDescr[detailLength] >= ' ')
            ++detailLength;
        std::wstring detail;
        if (!FtpDecodeServerTextForPresentation(codec,
                                                std::string_view(ErrAllocDescr, detailLength),
                                                detail))
            return FALSE;
        try
        {
            staged.push_back(L' ');
            staged.append(detail);
        }
        catch (...)
        {
            return FALSE;
        }
    }
    description.swap(staged);
    return TRUE;
}

//
// ****************************************************************************
// CFTPQueueItemAncestor
//

void CFTPQueueItemAncestor::ChangeStateAndCounters(CFTPQueueItemState state, CFTPOperation* oper,
                                                   CFTPQueue* queue)
{
    if (State == state)
        return; // nothing to do
    int childItemsNotDone = 1;
    int childItemsFailed = 0;
    int childItemsSkipped = 0;
    int childItemsUINeeded = 0;
    switch (State)
    {
    case sqisDone:
        childItemsNotDone = 0;
        break;
    case sqisSkipped:
        childItemsSkipped = 1;
        break;
    case sqisUserInputNeeded:
        childItemsUINeeded = 1;
        break;

    case sqisFailed:
    case sqisForcedToFail:
        childItemsFailed = 1;
        break;
    }
    BOOL onlyUINeededOrFailedToSkipped = FALSE;
    switch (state)
    {
    case sqisDone:
    {
        childItemsNotDone = 0 - childItemsNotDone;
        childItemsFailed = 0 - childItemsFailed;
        childItemsSkipped = 0 - childItemsSkipped;
        childItemsUINeeded = 0 - childItemsUINeeded;
        break;
    }

    case sqisSkipped:
    {
        onlyUINeededOrFailedToSkipped = State == sqisUserInputNeeded || State == sqisFailed;
        childItemsNotDone = 1 - childItemsNotDone;
        childItemsFailed = 0 - childItemsFailed;
        childItemsSkipped = 1 - childItemsSkipped;
        childItemsUINeeded = 0 - childItemsUINeeded;
        break;
    }

    case sqisUserInputNeeded:
    {
        childItemsNotDone = 1 - childItemsNotDone;
        childItemsFailed = 0 - childItemsFailed;
        childItemsSkipped = 0 - childItemsSkipped;
        childItemsUINeeded = 1 - childItemsUINeeded;
        break;
    }

    case sqisFailed:
    case sqisForcedToFail:
    {
        childItemsNotDone = 1 - childItemsNotDone;
        childItemsFailed = 1 - childItemsFailed;
        childItemsSkipped = 0 - childItemsSkipped;
        childItemsUINeeded = 0 - childItemsUINeeded;
        break;
    }

    default: // sqisWaiting, sqisProcessing, sqisDelayed
    {
        childItemsNotDone = 1 - childItemsNotDone;
        childItemsFailed = 0 - childItemsFailed;
        childItemsSkipped = 0 - childItemsSkipped;
        childItemsUINeeded = 0 - childItemsUINeeded;
        break;
    }
    }
    queue->UpdateCounters((CFTPQueueItem*)this, FALSE); // handle the state change by virtually removing the item and adding it back after the state changes
    State = state;
    queue->UpdateCounters((CFTPQueueItem*)this, TRUE);
    if (((CFTPQueueItem*)this)->IsItemInSimpleErrorState())
        ((CFTPQueueItem*)this)->ErrorOccurenceTime = queue->GiveLastErrorOccurenceTime();
    if (childItemsNotDone != 0 || childItemsFailed != 0 || childItemsSkipped != 0 || childItemsUINeeded != 0)
    {
        oper->AddToItemOrOperationCounters(((CFTPQueueItem*)this)->ParentUID, childItemsNotDone,
                                           childItemsSkipped, childItemsFailed, childItemsUINeeded,
                                           onlyUINeededOrFailedToSkipped);
    }
}

//
// ****************************************************************************
// CFTPQueueItemDir
//

CFTPQueueItemDir::CFTPQueueItemDir()
{
    ChildItemsNotDone = 0;
    ChildItemsSkipped = 0;
    ChildItemsFailed = 0;
    ChildItemsUINeeded = 0;
}

BOOL CFTPQueueItemDir::SetItemDir(int childItemsNotDone, int childItemsSkipped, int childItemsFailed,
                                  int childItemsUINeeded)
{
    ChildItemsNotDone = childItemsNotDone;
    ChildItemsSkipped = childItemsSkipped;
    ChildItemsFailed = childItemsFailed;
    ChildItemsUINeeded = childItemsUINeeded;
    return TRUE;
}

void CFTPQueueItemDir::SetStateAndNotDoneSkippedFailed(int childItemsNotDone, int childItemsSkipped,
                                                       int childItemsFailed, int childItemsUINeeded)
{
    ChildItemsNotDone = childItemsNotDone;
    ChildItemsSkipped = childItemsSkipped;
    ChildItemsFailed = childItemsFailed;
    ChildItemsUINeeded = childItemsUINeeded;
    if (GetItemState() == sqisWaiting) // if the item is ready to process, check whether
    {                                  // it needs to be delayed or must fail because of child items
        if (ChildItemsNotDone - ChildItemsSkipped - ChildItemsFailed - ChildItemsUINeeded > 0)
            SetStateInternal(sqisDelayed);
        else
        {
            if (ChildItemsSkipped + ChildItemsFailed + ChildItemsUINeeded > 0)
                SetStateInternal(sqisForcedToFail);
        }
    }
}

CFTPQueueItemState
CFTPQueueItemDir::GetStateFromCounters()
{
    if (ChildItemsNotDone - ChildItemsSkipped - ChildItemsFailed - ChildItemsUINeeded > 0)
        return sqisDelayed;
    else
    {
        if (ChildItemsSkipped + ChildItemsFailed + ChildItemsUINeeded > 0)
            return sqisForcedToFail;
        else
            return sqisWaiting;
    }
}

//
// ****************************************************************************
// CFTPQueueItemDel
//

CFTPQueueItemDel::CFTPQueueItemDel()
{
    IsHiddenFile = 0;
}

BOOL CFTPQueueItemDel::SetItemDel(int isHiddenFile)
{
    IsHiddenFile = isHiddenFile;
    return TRUE;
}

//
// ****************************************************************************
// CFTPQueueItemDelExplore
//

CFTPQueueItemDelExplore::CFTPQueueItemDelExplore()
{
    IsTopLevelDir = 0;
    IsHiddenDir = 0;
}

BOOL CFTPQueueItemDelExplore::SetItemDelExplore(int isTopLevelDir, int isHiddenDir)
{
    IsTopLevelDir = isTopLevelDir;
    IsHiddenDir = isHiddenDir;
    return TRUE;
}

//
// ****************************************************************************
// CFTPQueueItemCopyOrMove
//

CFTPQueueItemCopyOrMove::CFTPQueueItemCopyOrMove()
{
    LocalTgtPath = NULL;
    LocalTgtName = NULL;
    Size.SetUI64(0);
    AsciiTransferMode = 0;
    IgnoreAsciiTrModeForBinFile = 0;
    SizeInBytes = 0;
    TgtFileState = 0;
    DateAndTimeValid = 0;
    memset(&Date, 0, sizeof(Date));
    memset(&Time, 0, sizeof(Time));
}

CFTPQueueItemCopyOrMove::~CFTPQueueItemCopyOrMove()
{
    if (LocalTgtPath != NULL)
        SalamanderGeneral->Free(LocalTgtPath);
    if (LocalTgtName != NULL)
        SalamanderGeneral->Free(LocalTgtName);
}

void CFTPQueueItemCopyOrMove::SetItemCopyOrMove(const wchar_t* localTgtPath, const wchar_t* localTgtName,
                                                const CQuadWord& size,
                                                int asciiTransferMode, int sizeInBytes, int tgtFileState,
                                                BOOL dateAndTimeValid, const CFTPDate& date, const CFTPTime& time)
{
    LocalTgtPath = DupOperationWideText(localTgtPath);
    LocalTgtName = DupOperationWideText(localTgtName);
    Size = size;
    AsciiTransferMode = asciiTransferMode;
    SizeInBytes = sizeInBytes;
    TgtFileState = tgtFileState;
    DateAndTimeValid = (dateAndTimeValid != FALSE);
    Date = date;
    Time = time;
}

//
// ****************************************************************************
// CFTPQueueItemCopyOrMoveUpload
//

CFTPQueueItemCopyOrMoveUpload::CFTPQueueItemCopyOrMoveUpload()
{
    TgtPath = NULL;
    TgtName = NULL;
    Size.SetUI64(0);
    SizeWithCRLF_EOLs.SetUI64(0);
    NumberOfEOLs.SetUI64(0);
    AutorenamePhase = 0;
    RenamedName = NULL;
    AsciiTransferMode = 0;
    IgnoreAsciiTrModeForBinFile = 0;
    TgtFileState = 0;
}

CFTPQueueItemCopyOrMoveUpload::~CFTPQueueItemCopyOrMoveUpload()
{
    if (TgtPath != NULL)
        SalamanderGeneral->Free(TgtPath);
    if (TgtName != NULL)
        SalamanderGeneral->Free(TgtName);
    if (RenamedName != NULL)
    {
        TRACE_E("Unexpected situation in CFTPQueueItemCopyOrMoveUpload::~CFTPQueueItemCopyOrMoveUpload(): RenamedName != NULL");
        free(RenamedName);
    }
}

void CFTPQueueItemCopyOrMoveUpload::SetItemCopyOrMoveUpload(const char* tgtPath, const char* tgtName,
                                                            const CQuadWord& size, int asciiTransferMode,
                                                            int tgtFileState)
{
    TgtPath = DupOperationText(tgtPath);
    TgtName = DupOperationText(tgtName);
    Size = size;
    AsciiTransferMode = asciiTransferMode;
    TgtFileState = tgtFileState;
}

//
// ****************************************************************************
// CFTPQueueItemCopyMoveExplore
//

CFTPQueueItemCopyMoveExplore::CFTPQueueItemCopyMoveExplore()
{
    LocalTgtPath = NULL;
    LocalTgtName = NULL;
    TgtDirState = 0;
}

CFTPQueueItemCopyMoveExplore::~CFTPQueueItemCopyMoveExplore()
{
    if (LocalTgtPath != NULL)
        SalamanderGeneral->Free(LocalTgtPath);
    if (LocalTgtName != NULL)
        SalamanderGeneral->Free(LocalTgtName);
}

void CFTPQueueItemCopyMoveExplore::SetItemCopyMoveExplore(const wchar_t* localTgtPath, const wchar_t* localTgtName,
                                                          int tgtDirState)
{
    LocalTgtPath = DupOperationWideText(localTgtPath);
    LocalTgtName = DupOperationWideText(localTgtName);
    TgtDirState = tgtDirState;
}

//
// ****************************************************************************
// CFTPQueueItemCopyMoveUploadExplore
//

CFTPQueueItemCopyMoveUploadExplore::CFTPQueueItemCopyMoveUploadExplore()
{
    TgtPath = NULL;
    TgtName = NULL;
    TgtDirState = 0;
}

CFTPQueueItemCopyMoveUploadExplore::~CFTPQueueItemCopyMoveUploadExplore()
{
    if (TgtPath != NULL)
        SalamanderGeneral->Free(TgtPath);
    if (TgtName != NULL)
        SalamanderGeneral->Free(TgtName);
}

void CFTPQueueItemCopyMoveUploadExplore::SetItemCopyMoveUploadExplore(const char* tgtPath,
                                                                      const char* tgtName,
                                                                      int tgtDirState)
{
    TgtPath = DupOperationText(tgtPath);
    TgtName = DupOperationText(tgtName);
    TgtDirState = tgtDirState;
}

//
// ****************************************************************************
// CFTPQueueItemChAttr
//

static char* DupQueueRights(const char* text)
{
    return text != NULL ? _strdup(text) : NULL;
}

CFTPQueueItemChAttr::CFTPQueueItemChAttr()
{
    Attr = 0;
    AttrErr = FALSE;
    OrigRights = NULL;
}

CFTPQueueItemChAttr::~CFTPQueueItemChAttr()
{
    if (OrigRights != NULL)
        free(OrigRights);
}

void CFTPQueueItemChAttr::SetItemChAttr(WORD attr, const char* origRights, BYTE attrErr)
{
    Attr = attr;
    AttrErr = attrErr;
    OrigRights = DupQueueRights(origRights);
}

//
// ****************************************************************************
// CFTPQueueItemChAttrDir
//

CFTPQueueItemChAttrDir::CFTPQueueItemChAttrDir()
{
    Attr = 0;
    AttrErr = FALSE;
    OrigRights = NULL;
}

CFTPQueueItemChAttrDir::~CFTPQueueItemChAttrDir()
{
    if (OrigRights != NULL)
        free(OrigRights);
}

void CFTPQueueItemChAttrDir::SetItemChAttrDir(WORD attr, const char* origRights, BYTE attrErr)
{
    Attr = attr;
    AttrErr = attrErr;
    OrigRights = DupQueueRights(origRights);
}

//
// ****************************************************************************
// CFTPQueueItemChAttrExplore
//

CFTPQueueItemChAttrExplore::CFTPQueueItemChAttrExplore()
{
    OrigRights = NULL;
}

CFTPQueueItemChAttrExplore::~CFTPQueueItemChAttrExplore()
{
    if (OrigRights != NULL)
        free(OrigRights);
}

void CFTPQueueItemChAttrExplore::SetItemChAttrExplore(const char* origRights)
{
    OrigRights = DupQueueRights(origRights);
}

//
// ****************************************************************************
// CUIDArray
//

CUIDArray::CUIDArray(int base, int delta) : Data(base, delta)
{
    HANDLES(InitializeCriticalSection(&UIDListCritSect));
}

CUIDArray::~CUIDArray()
{
    HANDLES(DeleteCriticalSection(&UIDListCritSect));
}

BOOL CUIDArray::Add(int uid)
{
    HANDLES(EnterCriticalSection(&UIDListCritSect));
    BOOL ok = TRUE;
    Data.Add(uid);
    if (!Data.IsGood())
    {
        Data.ResetState();
        ok = FALSE;
    }
    HANDLES(LeaveCriticalSection(&UIDListCritSect));
    return ok;
}

BOOL CUIDArray::GetFirstUID(int& uid)
{
    HANDLES(EnterCriticalSection(&UIDListCritSect));
    BOOL ok = TRUE;
    if (Data.Count > 0)
    {
        uid = Data[0];
        Data.Delete(0);
        if (!Data.IsGood())
            Data.ResetState();
    }
    else
    {
        uid = -1;
        TRACE_E("Unexpected situation in CUIDArray::GetFirstUID(): no data!");
        ok = FALSE;
    }
    HANDLES(LeaveCriticalSection(&UIDListCritSect));
    return ok;
}
