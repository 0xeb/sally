// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

namespace
{
BOOL FormatListingErrorMessage(const CFtpTextCodec& codec, int suffixResourceID,
                               std::string_view detail, BOOL detailIsServerText,
                               std::wstring& message) noexcept
{
    try
    {
        std::wstring detailText;
        if (detailIsServerText)
        {
            if (!FtpDecodeServerTextForPresentation(codec, detail, detailText))
                return FALSE;
        }
        else if (!FtpDecodeLocalText(detail, detailText))
            detailText = L"<invalid local error text>";

        std::wstring staged = LangStr(IDS_UNABLETOREADLIST);
        staged.append(SPLFormatStringOwned(LangStr(suffixResourceID).c_str(),
                                           detailText.c_str()));
        message.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}
}

//
// ****************************************************************************
// CControlConnectionSocket
//

BOOL CControlConnectionSocket::SetCurrentTransferMode(HWND parent, BOOL asciiMode, BOOL* success,
                                                      std::string* ftpReply,
                                                      BOOL forceRefresh, BOOL* canRetry,
                                                      std::string* retryMessage)
{
    CALL_STACK_MESSAGE3("CControlConnectionSocket::SetCurrentTransferMode(, %d, , , %d, ,)",
                        asciiMode, forceRefresh);

    if (success != NULL)
        *success = FALSE;
    if (ftpReply != NULL)
        ftpReply->clear();
    if (canRetry != NULL)
        *canRetry = FALSE;
    if (retryMessage != NULL)
        retryMessage->clear();

    HANDLES(EnterCriticalSection(&SocketCritSect));
    BOOL leaveSect = TRUE;
    BOOL ret = TRUE;

    if (forceRefresh ||
        asciiMode && CurrentTransferMode != ctrmASCII ||
        !asciiMode && CurrentTransferMode != ctrmBinary)
    {
        std::string cmdBuf;
        std::string logBuf;

        HANDLES(LeaveCriticalSection(&SocketCritSect));
        leaveSect = FALSE;

        // change the transfer mode on the server
        int ftpReplyCode;
        if (PrepareFTPCommand(cmdBuf, &logBuf, ftpcmdSetTransferMode, NULL, asciiMode) &&
            SendFTPCommand(parent, cmdBuf.c_str(), logBuf.c_str(), NULL, GetWaitTime(WAITWND_COMOPER), NULL,
                           &ftpReplyCode, ftpReply, FALSE, FALSE, TRUE,
                           canRetry, retryMessage, NULL))
        {
            if (FTP_DIGIT_1(ftpReplyCode) == FTP_D1_SUCCESS) // success is returned (should be 200)
            {
                HANDLES(EnterCriticalSection(&SocketCritSect));
                leaveSect = TRUE;
                CurrentTransferMode = (asciiMode ? ctrmASCII : ctrmBinary); // the transfer mode was changed
            }
            else
                CurrentTransferMode = ctrmUnknown; // unknown error, might not matter, but return the error for the caller to judge
        }
        else
            ret = FALSE; // error -> connection closed
    }

    if (leaveSect) // the requested transfer mode is already set successfully
    {
        if (success != NULL)
            *success = TRUE;
        HANDLES(LeaveCriticalSection(&SocketCritSect));
    }
    return ret;
}

//
// **************************************************************************************
// CSendCmdUserIfaceForListAndDownload
//

BOOL CSendCmdUserIfaceForListAndDownload::HadError()
{
    DWORD netErr, tgtFileErr;
    BOOL lowMem;
    int sslErrorOccured;
    BOOL decomprErrorOccured;
    DataConnection->GetError(&netErr, &lowMem, &tgtFileErr, NULL, &sslErrorOccured, &decomprErrorOccured);
    return netErr != NO_ERROR || lowMem || tgtFileErr != NO_ERROR || sslErrorOccured != SSLCONERR_NOERROR ||
           decomprErrorOccured;
}

void CSendCmdUserIfaceForListAndDownload::GetError(DWORD* netErr, BOOL* lowMem, DWORD* tgtFileErr,
                                                   BOOL* noDataTrTimeout, int* sslErrorOccured,
                                                   BOOL* decomprErrorOccured)
{
    DataConnection->GetError(netErr, lowMem, tgtFileErr, noDataTrTimeout, sslErrorOccured,
                             decomprErrorOccured);
}

HANDLE
CSendCmdUserIfaceForListAndDownload::GetFinishedEvent()
{
    return DataConnection->GetTransferFinishedEvent();
}

void CSendCmdUserIfaceForListAndDownload::InitWnd(const char* fileName, const wchar_t* host,
                                                  const char* path, CFTPServerPathType pathType,
                                                  const CFtpTextCodec& textCodec)
{
    std::wstring fileNameText;
    std::wstring pathText;
    if (fileName != NULL && !textCodec.Decode(fileName, strlen(fileName), fileNameText))
        fileNameText = L"<invalid server text>";
    if (!textCodec.Decode(path, strlen(path), pathText))
        pathText = L"<invalid server text>";
    try
    {
        const std::wstring title = !ForDownload
                                       ? SPLFormatStringOwned(LangStr(IDS_LISTWNDDOWNLOADING).c_str(), host)
                                       : SPLFormatStringOwned(LangStr(IDS_LISTWNDDOWNLOADINGFILE).c_str(),
                                                              fileNameText.c_str(), host);
        WaitWnd.SetText(title.c_str());
    }
    catch (...)
    {
        WaitWnd.SetText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str());
    }
    WaitWnd.SetPath(pathText.c_str(), pathType);
}

void CSendCmdUserIfaceForListAndDownload::AfterWrite(BOOL aborting, DWORD showTime)
{
    WaitWnd.Create(showTime);
    if (!aborting)
        DataConnection->ActivateConnection();
}

BOOL CSendCmdUserIfaceForListAndDownload::HandleESC(HWND parent, BOOL isSend, BOOL allowCmdAbort)
{
    BOOL offerAbort = isSend && allowCmdAbort ||           // if abort is possible and we have not done it yet
                      DataConnection->IsTransfering(NULL); // if no data transfer is in progress, we cannot abort (only terminate the "control connection")

    WaitWnd.Show(FALSE);
    BOOL esc = SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, !AlreadyAborted ? (offerAbort ? (ForDownload ? IDS_LISTWNDDOWNLFILESENDCMDESC : IDS_LISTWNDSENDCOMMANDESC) : (ForDownload ? IDS_LISTWNDDOWNLFILESENDCMDESC2 : IDS_LISTWNDSENDCOMMANDESC2)) : IDS_LISTWNDABORTCOMMANDESC).c_str(),
                                                SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                MB_YESNO | MSGBOXEX_ESCAPEENABLED | MB_ICONQUESTION) == IDYES;
    if (esc)
    {
        WaitWnd.SetText(LangStr(ForDownload ? IDS_LISTWNDDOWNLFILEABORTING : IDS_LISTWNDABORTINGCOMMAND).c_str());
        if (!offerAbort)
            AlreadyAborted = TRUE;
        if (!AlreadyAborted)
        {
            if (DataConnection->IsTransfering(NULL) ||  // just in case, maybe the connection is already closed and at the same time
                DataConnection->IsFlushingDataToDisk()) // there is nothing left to flush; avoid logging nonsense too often (it can still happen, never mind)
            {
                DataConnection->CancelConnectionAndFlushing(); // close the "data connection"; the system attempts a "graceful" shutdown (we will not learn the result)
                Logs.LogMessage(LogUID, LangStr(IDS_LOGMSGDATACONTERMINATED).c_str(), -1, TRUE);
            }
            AlreadyAborted = TRUE;
            if (!allowCmdAbort)
                esc = FALSE; // that is not ESC for listing yet
        }
    }
    else
        SalamanderGeneral->WaitForESCRelease(); // a measure to keep the next action from being interrupted after every ESC in the previous message box
    if (!esc)
        WaitWnd.Show(TRUE); // nothing happened; continue showing the wait window (during abort a different text is displayed in the window)
    return esc;
}

void CSendCmdUserIfaceForListAndDownload::SendingFinished()
{
    WaitWnd.Destroy();
}

BOOL CSendCmdUserIfaceForListAndDownload::IsTimeout(DWORD* start, DWORD serverTimeout, int* errorTextID,
                                                    std::string& errorText)
{
    BOOL trFinished;
    BOOL ret = FALSE;
    if (DataConnection->IsTransfering(&trFinished))
        *start = GetTickCount(); // waiting for data, so this is not a timeout
    else
    {
        if (trFinished)
        {
            *start = DataConnection->GetSocketCloseTime();
            ret = (GetTickCount() - *start) >= serverTimeout; // the timeout is measured from the connection closing (the moment since when the server can react and also learns about the closure)
        }
        else
            ret = TRUE; // the connection has not opened yet -> treat it as a timeout
    }
    if (ret)
    {
        std::string proxyTimeoutText;
        if (DataConnection->GetProxyTimeoutDescr(proxyTimeoutText))
        {
            if (FTPFormatString(errorText, LoadStr(IDS_LOGMSGDATCONERROR),
                                proxyTimeoutText.c_str()))
                *errorTextID = -1; // the description is directly in 'errorText'
            else
                *errorTextID = IDS_OPERDOPPR_LOWMEM;
        }
        else
            *errorTextID = ForDownload ? IDS_LISTWNDDOWNLFILETIMEOUT : IDS_LISTCMDTIMEOUT;
    }
    return ret;
}

void CSendCmdUserIfaceForListAndDownload::CancelDataCon()
{
    DataConnection->CancelConnectionAndFlushing();
}

void CSendCmdUserIfaceForListAndDownload::MaybeSuccessReplyReceived(const char* reply, int replySize)
{
    DataConnection->EncryptPassiveDataCon();

    CQuadWord size;
    if (FTPGetDataSizeInfoFromSrvReply(size, reply, replySize))
    {
        // we have the total size of the listing - 'size'
        DataConnection->SetDataTotalSize(size);
    }
}

BOOL CSendCmdUserIfaceForListAndDownload::CanFinishSending(int replyCode, BOOL* useTimeout)
{
    BOOL trFinished;
    BOOL ret = DataConnection->IsTransfering(&trFinished);
    if (!ret && !trFinished && DataConnection->IsConnected()) // the connection has not been established yet and the socket is open
    {
        if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS)
        {
            *useTimeout = TRUE; // do not close the socket; bypass WarFTPD bugs: it can return "success" even before the data-connection socket is accepted (before the data transfer starts) - we do not bother printing an error if a timeout occurs in this situation (WarFTPD will not perform the data transfer); list & view are not that important and it is unlikely to happen
            ret = TRUE;         // simulate that data are being transferred right now
        }
        else
            DataConnection->CloseSocketEx(NULL); // close the socket (it only waits for the connection); the server apparently reports a command error (listing)
    }
    if (!ret && ForDownload)
    {
        SocketsThread->LockSocketsThread();
        ret = !DataConnection->AreAllDataFlushed(FALSE);
        SocketsThread->UnlockSocketsThread();
        if (!ret)
            DataConnection->CloseTgtFile(); // successful file closing after the data flush completes
    }
    return !ret; // either the connection was never established or it is already closed
}

void CSendCmdUserIfaceForListAndDownload::BeforeWaitingForFinish(int replyCode, BOOL* useTimeout)
{
    if (FTP_DIGIT_1(replyCode) != FTP_D1_SUCCESS) // LIST does not return success - it may not close
    {                                             // the data connection (e.g., WarFTPD) - wait for the remaining data, but preferably with a timeout
        *useTimeout = TRUE;
        //    DataConnection->CloseSocketEx(NULL);   // to have something to show in the Show Raw Listing panel we must fetch the remaining data
    }
}

void CSendCmdUserIfaceForListAndDownload::HandleDataConTimeout(DWORD* start)
{
    DWORD lastActTime = DataConnection->GetLastActivityTime();
    if (*start < lastActTime)
        *start = lastActTime;
    else
    {
        BOOL trFinished;
        if (!DataConnection->IsTransfering(&trFinished) && !trFinished &&
            DataConnection->IsConnected()) // the connection has not been established yet and the socket is open
        {
            Logs.LogMessage(LogUID, LangStr(IDS_LOGMSGDATACONNOTOPENED).c_str(), -1, TRUE);
        }
        DatConCancelled = TRUE;
        DataConnection->CancelConnectionAndFlushing(); // stop waiting for data; we have been waiting too long (a timeout occurred)
    }
}

void CSendCmdUserIfaceForListAndDownload::HandleESCWhenWaitingForFinish(HWND parent)
{
    WaitWnd.Show(FALSE);
    BOOL esc = SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, ForDownload ? IDS_LISTWNDDOWNLFILESENDCMDESC : IDS_LISTWNDSENDCOMMANDESC).c_str(),
                                                SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                MB_YESNO | MSGBOXEX_ESCAPEENABLED | MB_ICONQUESTION) == IDYES;
    if (esc)
    {
        // WaitWnd.SetText(LoadStr(IDS_LISTWNDABORTINGCOMMAND)); // unnecessary, the window will not be shown again
        // while the user decides how to respond to the abort, the data connection may finish (that is why
        // the dialog says "listing may be incomplete") - then it makes sense to ignore the abort
        if (DataConnection->IsTransfering(NULL) || DataConnection->IsFlushingDataToDisk())
        {
            DataConnection->CancelConnectionAndFlushing(); // close the "data connection"; the system attempts a "graceful" shutdown (we will not learn the result)
            Logs.LogMessage(LogUID, LangStr(IDS_LOGMSGDATACONTERMINATED).c_str(), -1, TRUE);
            AlreadyAborted = TRUE;
        }
    }
    else
        SalamanderGeneral->WaitForESCRelease(); // a measure to keep the next action from being interrupted after every ESC in the previous message box
    if (!esc)
        WaitWnd.Show(TRUE);
}

// ***********************************************************************************

BOOL CControlConnectionSocket::IsListCommandLIST_a()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    BOOL ret = _stricmp(UseLIST_aCommand ? LIST_a_CMD_TEXT : (!ListCommand.empty() ? ListCommand.c_str() : LIST_CMD_TEXT),
                        LIST_a_CMD_TEXT) == 0;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}

void CControlConnectionSocket::ToggleListCommandLIST_a()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    if (IsListCommandLIST_a())
    {
        if (UseLIST_aCommand)
            UseLIST_aCommand = FALSE;
        else
        {
            ListCommand.clear();
        }
    }
    else
        UseLIST_aCommand = TRUE;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
}

BOOL CControlConnectionSocket::ListWorkingPath(HWND parent, const char* path, std::wstring& user,
                                               std::optional<std::string>& listing,
                                               CFTPDate* listingDate,
                                               BOOL* pathListingIsIncomplete, BOOL* pathListingIsBroken,
                                               BOOL* pathListingMayBeOutdated,
                                               DWORD* pathListingStartTime, BOOL forceRefresh,
                                               int* totalAttemptNum, BOOL* fatalError,
                                               BOOL dontClearCache)
{
    CALL_STACK_MESSAGE3("CControlConnectionSocket::ListWorkingPath(, %s, , , , , , , , , %d, , ,)",
                        path, forceRefresh);

    *fatalError = FALSE;
    *pathListingIsBroken = FALSE;
    if (listing.has_value())
        FTPSecureWipe(*listing);
    listing.reset();
    BOOL ok = TRUE;
    BOOL ret = TRUE;
    std::string cmdBuf;
    std::string logBuf;
    std::string replyBuf;
    std::string listCmd;

    HANDLES(EnterCriticalSection(&SocketCritSect));
    BOOL listCmdOK = TRUE;
    try
    {
        listCmd = UseLIST_aCommand ? LIST_a_CMD_TEXT : (!ListCommand.empty() ? ListCommand : LIST_CMD_TEXT);
        listCmd.append("\r\n");
    }
    catch (const std::bad_alloc&)
    {
        listCmdOK = FALSE;
    }
    catch (const std::length_error&)
    {
        listCmdOK = FALSE;
    }
    BOOL usePassiveModeAux = UsePassiveMode;
    int logUID = LogUID; // log UID of this connection
    int useListingsCacheAux = UseListingsCache;
    std::wstring hostTmp;
    const BOOL hostReady = FtpStoreWideText(Host.c_str(), hostTmp);
    CFTPProxyForDataCon* dataConProxyServer = ProxyServer == NULL ? NULL : ProxyServer->AllocProxyForDataCon(ServerIP, Host.c_str(), HostIP, Port);
    BOOL dataConProxyServerOK = ProxyServer == NULL || dataConProxyServer != NULL;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    if (!listCmdOK || !hostReady)
    {
        if (dataConProxyServer != NULL)
            delete dataConProxyServer;
        *fatalError = TRUE;
        return FALSE;
    }

    int attemptNum = 1;
    if (totalAttemptNum != NULL)
        attemptNum = *totalAttemptNum;
    const std::string* retryMessageToUse = NULL;
    BOOL canRetry = FALSE;
    std::string nextRetryMessage;
    // obtain the date when the listing was created (we assume the server creates it first and only then sends it)
    SYSTEMTIME st;
    GetLocalTime(&st);
    DWORD lstStTime = 0;

    // allocate the object for the "data connection"
    CDataConnectionSocket* dataConnection = dataConProxyServerOK ? new CDataConnectionSocket(FALSE, dataConProxyServer, EncryptDataConnection, pCertificate, CompressData, this) : NULL;
    if (dataConnection == NULL || !dataConnection->IsGood())
    {
        if (dataConnection != NULL)
            DeleteSocket(dataConnection); // it will only be deallocated
        else
        {
            if (dataConProxyServer != NULL)
                delete dataConProxyServer;
        }
        dataConnection = NULL;
        TRACE_E(LOW_MEMORY);
        *fatalError = TRUE; // fatal error
    }
    else
    {
        while (1)
        {
            ReuseSSLSessionFailed = FALSE;
            if (ok && usePassiveModeAux) // passive mode (PASV)
            {
                int ftpReplyCode;
                if (PrepareFTPCommand(cmdBuf, &logBuf, ftpcmdPassive, NULL) &&
                    SendFTPCommand(parent, cmdBuf.c_str(), logBuf.c_str(), NULL, GetWaitTime(WAITWND_COMOPER), NULL,
                                   &ftpReplyCode, &replyBuf, FALSE, FALSE, FALSE, &canRetry,
                                   &nextRetryMessage, NULL))
                {
                    DWORD ip;
                    unsigned short port;
                    if (FTP_DIGIT_1(ftpReplyCode) == FTP_D1_SUCCESS &&      // success (should be 227)
                        FTPGetIPAndPortFromReply(replyBuf, &ip, &port)) // managed to obtain IP and port
                    {
                        dataConnection->SetPassive(ip, port, logUID);
                        dataConnection->PassiveConnect(NULL); // the first attempt; the result does not matter (it is checked later)
                    }
                    else // passive mode is not supported
                    {
                        HANDLES(EnterCriticalSection(&SocketCritSect));
                        UsePassiveMode = usePassiveModeAux = FALSE; // try it again in the active mode (PORT)
                        HANDLES(LeaveCriticalSection(&SocketCritSect));

                        Logs.LogMessage(logUID, LangStr(IDS_LOGMSGPASVNOTSUPPORTED).c_str(), -1);
                    }
                }
                else // error -> connection closed
                {
                    ok = FALSE;
                    if (canRetry)
                        retryMessageToUse = &nextRetryMessage; // "retry" is allowed; go to the next reconnect
                    else
                    {
                        *fatalError = TRUE; // fatal error
                        break;
                    }
                }
            }

            if (ok && !usePassiveModeAux) // active mode (PORT)
            {
                DWORD localIP;
                GetLocalIP(&localIP, NULL);   // should not be able to fail
                unsigned short localPort = 0; // listen on any port
                dataConnection->SetActive(logUID);
                if (OpenForListeningAndWaitForRes(parent, dataConnection, &localIP, &localPort, &canRetry,
                                                  &nextRetryMessage, GetWaitTime(WAITWND_COMOPER)))
                {
                    int ftpReplyCode;
                    if (!PrepareFTPCommand(cmdBuf, &logBuf, ftpcmdSetPort, NULL, localIP, localPort) ||
                        !SendFTPCommand(parent, cmdBuf.c_str(), logBuf.c_str(), NULL, GetWaitTime(WAITWND_COMOPER), NULL,
                                        &ftpReplyCode, NULL, FALSE, FALSE, FALSE, &canRetry,
                                        &nextRetryMessage, NULL)) // we ignore the server's response; the error will appear later (timeout while listing)
                    {                                            // error -> connection closed
                        ok = FALSE;
                        if (canRetry)
                            retryMessageToUse = &nextRetryMessage; // "retry" is allowed; go to the next reconnect
                        else
                        {
                            *fatalError = TRUE; // fatal error
                            break;
                        }
                    }
                }
                else // failed to open the "listen" socket for receiving the data connection from the server ->
                {    // connection closed (so that the standard Retry can be used)
                    ok = FALSE;
                    if (canRetry)
                        retryMessageToUse = &nextRetryMessage; // "retry" is allowed; go to the next reconnect
                    else
                    {
                        *fatalError = TRUE; // fatal error
                        break;
                    }
                }
            }

            if (ok) // if we are still connected, switch the transfer mode to ASCII (ignore success)
            {
                ok = SetCurrentTransferMode(parent, TRUE, NULL, NULL, forceRefresh, &canRetry,
                                            &nextRetryMessage);
                if (!ok) // error -> connection closed
                {
                    if (canRetry)
                        retryMessageToUse = &nextRetryMessage; // "retry" is allowed; go to the next reconnect
                    else
                    {
                        *fatalError = TRUE; // fatal error
                        break;
                    }
                }
            }

            BOOL sslErrReconnect = FALSE;     // TRUE = reconnect because of SSL errors
            BOOL fastSSLErrReconnect = FALSE; // TRUE = the server certificate changed; an immediate reconnect is desirable (without waiting 20 seconds)
            if (ok)
            {
                int ftpReplyCode;
                CSendCmdUserIfaceForListAndDownload userIface(FALSE, parent, dataConnection, logUID);

                //        if (UsePassiveMode) {
                //          dataConnection->EncryptConnection();
                //        }
                HANDLES(EnterCriticalSection(&SocketCritSect));
                CFTPServerPathType pathType = ::GetFTPServerPathType(ServerFirstReply.c_str(), ServerSystem.c_str(), path);
                HANDLES(LeaveCriticalSection(&SocketCritSect));

                userIface.InitWnd(NULL, hostTmp.c_str(), path, pathType, GetTextCodec());
                lstStTime = IncListingCounter();
                if (SendFTPCommand(parent, listCmd.c_str(), listCmd.c_str(), NULL, GetWaitTime(WAITWND_COMOPER), NULL,
                                   &ftpReplyCode, &replyBuf, FALSE, FALSE, FALSE, &canRetry,
                                   &nextRetryMessage, &userIface))
                {
                    if (!userIface.GetDatConCancelled() && !userIface.WasAborted() && !userIface.HadError() &&
                        FTP_DIGIT_1(ftpReplyCode) != FTP_D1_SUCCESS &&
                        FTP_DIGIT_2(ftpReplyCode) != FTP_D2_CONNECTION) // it is not just a connection (network) error
                    {                                                   // the server refuses to list
                        BOOL skipMessage = FTPIsEmptyDirListErrReply(replyBuf.c_str());
                        if (!skipMessage)
                        {
                            std::wstring errorText;
                            if (!FtpFormatServerReplyMessage(GetTextCodec(), LangStr(IDS_LISTPATHERROR).c_str(),
                                                             std::string_view(path), replyBuf, errorText))
                                errorText = LangStr(IDS_OPERDOPPR_LOWMEM);
                            // if the user answers IDNO, we stop - the path cannot be listed -> a path change is required
                            ret = SalamanderGeneral->SalMessageBox(parent, errorText.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                                   MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                                                       MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
                            if (!ret)
                                SalamanderGeneral->WaitForESCRelease(); // a measure to keep the next action from being interrupted after every ESC in the previous message box

                            *pathListingIsBroken = TRUE; // to make it clear that the returned listing is not OK (VxWorks: while listing it can report "error reading entry: 16" and return "550 no files found or ...")
                        }
                        // VMS returns 550 for an empty directory: we cannot leave the path and the listing can easily
                        // be considered OK (it can even be cached - it was not interrupted and the server
                        // will most likely not return a different listing)
                        // ret = FALSE;   // stop - the path cannot be listed -> a path change is required

                        break; // report a "successful listing" (allows working in an empty/unlistable directory)
                    }
                    else
                    {
                        if (userIface.WasAborted()) // the user aborted the listing - finish with an error (an incomplete listing)
                            ok = FALSE;             // do not display the "list can be incomplete" message; the user was warned during abort
                        else
                        {
                            if (FTP_DIGIT_1(ftpReplyCode) != FTP_D1_SUCCESS &&
                                    FTP_DIGIT_2(ftpReplyCode) == FTP_D2_CONNECTION || // it is only a network error
                                userIface.HadError() ||                               // the data connection recorded an error reported by the system
                                userIface.GetDatConCancelled())                       // the data connection was interrupted (either it did not open or it closed after an error reported by the server in response to LIST)
                            {
                                ok = FALSE;

                                DWORD err;
                                BOOL lowMem, noDataTrTimeout;
                                int sslErrorOccured;
                                userIface.GetError(&err, &lowMem, NULL, &noDataTrTimeout, &sslErrorOccured, NULL);

                                BOOL sslReuseErr = ReuseSSLSessionFailed &&
                                                   (FTP_DIGIT_1(ftpReplyCode) == FTP_D1_TRANSIENTERROR ||
                                                    FTP_DIGIT_1(ftpReplyCode) == FTP_D1_ERROR);
                                if (sslErrorOccured == SSLCONERR_UNVERIFIEDCERT || sslErrorOccured == SSLCONERR_CANRETRY ||
                                    sslReuseErr)
                                {                                                                       // we need to perform a reconnect
                                    CloseControlConnection(parent);                                     // close the current control connection
                                    if (!FtpStoreProtocolBytes(LoadStr(IDS_ERRDATACONSSLCONNECTERROR), nextRetryMessage))
                                        TRACE_E(LOW_MEMORY);
                                    retryMessageToUse = &nextRetryMessage;
                                    sslErrReconnect = TRUE;
                                    fastSSLErrReconnect = sslErrorOccured == SSLCONERR_UNVERIFIEDCERT || sslReuseErr;
                                }
                                else
                                {
                                    // display the "list can be incomplete" message; the user has not been warned yet
                                    BOOL systErr = FALSE;
                                    BOOL trModeHint = FTP_DIGIT_1(ftpReplyCode) == FTP_D1_TRANSIENTERROR &&
                                                      FTP_DIGIT_2(ftpReplyCode) == FTP_D2_CONNECTION;

                                    if (FTP_DIGIT_1(ftpReplyCode) == FTP_D1_SUCCESS ||
                                        FTP_DIGIT_2(ftpReplyCode) != FTP_D2_CONNECTION ||
                                        noDataTrTimeout || sslErrorOccured != SSLCONERR_NOERROR)
                                    { // if we do not have a description of the network error from the server, we settle for the system description
                                        systErr = TRUE;
                                        if (!trModeHint)
                                            trModeHint = err == WSAETIMEDOUT || sslErrorOccured != SSLCONERR_NOERROR;
                                        if (sslErrorOccured != SSLCONERR_NOERROR)
                                        {
                                            if (!FTPFormatString(replyBuf, "%s\r\n", LoadStr(IDS_ERRDATACONSSLCONNECTERROR)))
                                                replyBuf.clear();
                                        }
                                        else
                                        {
                                            if (noDataTrTimeout)
                                            {
                                                if (!FTPFormatString(replyBuf, "%s", LoadStr(IDS_ERRDATACONNODATATRTIMEOUT)))
                                                    replyBuf.clear();
                                            }
                                            else
                                            {
                                                if (err != NO_ERROR)
                                                {
                                                    if (!dataConnection->GetProxyError(replyBuf, NULL, TRUE) &&
                                                        !FTPGetErrorText(err, replyBuf))
                                                        replyBuf.clear();
                                                }
                                                else
                                                {
                                                    if (userIface.GetDatConCancelled())
                                                    {
                                                        if (!FTPFormatString(replyBuf, "%s", LoadStr(IDS_ERRDATACONNOTOPENED)))
                                                            replyBuf.clear();
                                                    }
                                                    else
                                                    {
                                                        if (!FTPFormatString(replyBuf, "%s", LoadStr(IDS_UNKNOWNERROR)))
                                                            replyBuf.clear();
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    const int suffixResourceID = systErr ? (trModeHint ? IDS_UNABLETOREADLISTSUFFIX3 : IDS_UNABLETOREADLISTSUFFIX)
                                                                         : (trModeHint ? IDS_UNABLETOREADLISTSUFFIX4 : IDS_UNABLETOREADLISTSUFFIX2);
                                    std::wstring errorText;
                                    if (!FormatListingErrorMessage(GetTextCodec(), suffixResourceID,
                                                                   replyBuf, !systErr, errorText))
                                        errorText = LangStr(IDS_OPERDOPPR_LOWMEM);
                                    SalamanderGeneral->SalMessageBox(parent, errorText.c_str(),
                                                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                                     MB_OK | MB_ICONEXCLAMATION);
                                }
                            }
                        }
                        if (!sslErrReconnect)
                            break; // aborted or a total success (everything listed - 'ok' == TRUE)
                    }
                }
                else // connection closed
                {
                    if (userIface.WasAborted()) // the user aborted the listing, which terminated the connection (for example on sunsolve.sun.com (Sun Unix) or ftp.chg.ru) - finish with an error (an incomplete listing)
                    {
                        ok = FALSE;   // do not display the "list can be incomplete" message; the user was warned during abort
                        if (canRetry) // take over the message for the message box that announces the connection interruption
                        {
                            HANDLES(EnterCriticalSection(&SocketCritSect));
                            ConnectionLostMsg = nextRetryMessage;
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                        }
                        break; // aborted
                    }

                    // error -> 'ok' remains FALSE; proceed to the next reconnect
                    ok = FALSE;
                    if (canRetry)
                        retryMessageToUse = &nextRetryMessage; // "retry" is allowed
                    else
                    {
                        *fatalError = TRUE; // fatal error
                        break;
                    }
                }
            }

            if (!ok) // the connection was interrupted; ask whether to reconnect
            {
                if (dataConnection->IsConnected())       // close the old "data connection" (in case the FD_CONNECT did not arrive)
                    dataConnection->CloseSocketEx(NULL); // shutdown (we will not learn the result)

                SetStartTime();
                BOOL startRet = StartControlConnection(parent, user, TRUE, NULL,
                                                       &attemptNum, retryMessageToUse, FALSE, sslErrReconnect ? IDS_LISTCOMMANDERROR : -1,
                                                       fastSSLErrReconnect);
                retryMessageToUse = NULL;
                if (totalAttemptNum != NULL)
                    *totalAttemptNum = attemptNum;
                if (startRet) // the connection has been restored
                {
                    if (pCertificate) // the control-connection certificate may have changed; pass any new one to the data connection
                        dataConnection->SetCertificate(pCertificate);

                    // change the path to 'path' (the path we are listing)
                    int ftpReplyCode;
                    if (PrepareFTPCommand(cmdBuf, &logBuf, ftpcmdChangeWorkingPath, NULL, path) &&
                        SendFTPCommand(parent, cmdBuf.c_str(), logBuf.c_str(), NULL, GetWaitTime(WAITWND_COMOPER), NULL,
                                       &ftpReplyCode, &replyBuf, FALSE, TRUE, FALSE, &canRetry,
                                       &nextRetryMessage, NULL))
                    {
                        BOOL pathError = TRUE;
                        if (FTP_DIGIT_1(ftpReplyCode) == FTP_D1_SUCCESS) // there is hope for success; better verify the path
                        {
                            std::string currentWorkingPath;
                            if (GetCurrentWorkingPath(parent, currentWorkingPath, TRUE, &canRetry,
                                                      &nextRetryMessage))
                            {
                                if (currentWorkingPath == path) // we have the desired working directory on the server
                                                               // (assumption: the server returns the same working-path string)
                                {
                                    pathError = FALSE;
                                    ok = TRUE; // successful reconnect; list again
                                }
                            }
                            else
                            {
                                pathError = FALSE; // error -> connection closed - 'ok' stays FALSE; proceed to the next reconnect
                                if (canRetry)
                                    retryMessageToUse = &nextRetryMessage; // "retry" is allowed
                                else
                                {
                                    *fatalError = TRUE; // fatal error
                                    break;
                                }
                            }
                        }

                        if (pathError) // display the path error and stop
                        {
                            std::wstring errorText;
                            if (!FtpFormatServerReplyMessage(GetTextCodec(), LangStr(IDS_CHANGEWORKPATHERROR).c_str(),
                                                             std::string_view(path), replyBuf, errorText))
                                errorText = LangStr(IDS_OPERDOPPR_LOWMEM);
                            SalamanderGeneral->SalMessageBox(parent, errorText.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                             MB_OK | MB_ICONEXCLAMATION);
                            ret = FALSE; // stop - the path cannot be listed -> a path change is required

                            // if we did not find any accessible path, disconnect at this point,
                            // the issue is handled during connect (in CControlConnectionSocket::ChangeWorkingPath()),
                            // I am out of patience here ;-)

                            break;
                        }
                    }
                    else // error -> connection closed - 'ok' stays FALSE; proceed to the next reconnect
                    {
                        if (canRetry)
                            retryMessageToUse = &nextRetryMessage; // "retry" is allowed
                        else
                        {
                            *fatalError = TRUE; // fatal error
                            break;
                        }
                    }
                }
                else // reconnect failed - finish with an error (an incomplete listing)
                {
                    SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNABLETOREADLIST).c_str(),
                                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                     MB_OK | MB_ICONEXCLAMATION);
                    break;
                }
            }
        }
    }

    if (ret && !*fatalError) // there is neither a path error nor a fatal error
    {
        if (dataConnection->IsConnected()) // error: the "data connection" should have been closed long ago
        {
            TRACE_E("Unexpected situation in CControlConnectionSocket::ListWorkingPath(): data connection has left opened!");
            dataConnection->CloseSocketEx(NULL); // shutdown (we will not learn the result)
        }

        // Take over the explicitly encoded bytes from the data connection.
        BOOL decomprErr;
        std::string receivedListing;
        if (dataConnection->GiveData(receivedListing, &decomprErr))
            listing.emplace(std::move(receivedListing));
        else
        {
            *fatalError = TRUE;
            ret = FALSE;
        }

        if (listing.has_value() && decomprErr && ok)
        {
            ok = FALSE;

            // display the "list can be incomplete" message; the user has not been warned yet
            std::wstring errorText;
            if (!FormatListingErrorMessage(GetTextCodec(), IDS_UNABLETOREADLISTSUFFIX,
                                           std::string_view(LoadStr(IDS_ERRDATACONDECOMPRERROR)),
                                           FALSE, errorText))
                errorText = LangStr(IDS_OPERDOPPR_LOWMEM);
            SalamanderGeneral->SalMessageBox(parent, errorText.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                             MB_OK | MB_ICONEXCLAMATION);
        }

        if (listing.has_value())
        {
            *pathListingIsIncomplete = !ok; // TRUE in case of a failure/interruption/connection error
            *pathListingMayBeOutdated = FALSE;

            // store the date when the listing was created
            listingDate->Year = st.wYear;
            listingDate->Month = (BYTE)st.wMonth;
            listingDate->Day = (BYTE)st.wDay;
            *pathListingStartTime = lstStTime;

            std::wstring userTmp;
            if (forceRefresh &&  // treat a hard refresh as a sign of distrust in the path; drop it from the cache including
                !dontClearCache) // all subpaths (ignore useListingsCacheAux; it does not affect the distrust)
            {                    // called only once we have a replacement listing (until then the user will certainly prefer
                                 // an outdated listing over none at all)
                HANDLES(EnterCriticalSection(&SocketCritSect));
                unsigned short portTmp = Port;
                const BOOL haveUser = FtpStoreWideText(User.c_str(), userTmp);
                CFTPServerPathType pathType = ::GetFTPServerPathType(ServerFirstReply.c_str(), ServerSystem.c_str(), path);
                HANDLES(LeaveCriticalSection(&SocketCritSect));

                if (haveUser)
                    ListingCache.RefreshOnPath(hostTmp.c_str(), portTmp, userTmp.c_str(), pathType, path);
            }

            if (ok) // we have a complete listing
            {
                if (!*pathListingIsBroken && useListingsCacheAux)
                { // the user wants to use the cache -> add the newly fetched listing to the cache
                    HANDLES(EnterCriticalSection(&SocketCritSect));
                    unsigned short portTmp = Port;
                    const BOOL haveUser = FtpStoreWideText(User.c_str(), userTmp);
                    CFTPServerPathType pathType = ::GetFTPServerPathType(ServerFirstReply.c_str(), ServerSystem.c_str(), path);
                    BOOL isFTPS = EncryptControlConnection == 1;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));

                    if (haveUser)
                        ListingCache.AddOrUpdatePathListing(hostTmp.c_str(), portTmp, userTmp.c_str(), pathType, path,
                                                            GetTextCodec(), listCmd.c_str(), isFTPS, *listing,
                                                            listingDate, *pathListingStartTime);
                }
            }
            else // failure/interruption/connection error = return at least what we have (the user already knows that "list can be incomplete")
                FtpTrimIncompleteListingBytes(*listing);
        }
    }
    if (dataConnection != NULL) // release and possibly close the "data connection"
    {
        if (dataConnection->IsConnected())       // close the "data connection"; the system attempts a "graceful"
            dataConnection->CloseSocketEx(NULL); // shutdown (we will not learn the result)
        DeleteSocket(dataConnection);
    }
    if (*fatalError)
        ret = FALSE; // we certainly will not return success on a fatal error
    return ret;
}

class CFinishingKeepAliveUserIface : public CSendCmdUserIfaceAbstract
{
protected:
    CWaitWindow* WaitWnd;
    HANDLE FinishedEvent;

public:
    CFinishingKeepAliveUserIface(CWaitWindow* waitWnd, HANDLE finishedEvent)
    {
        WaitWnd = waitWnd;
        FinishedEvent = finishedEvent;
    }

    virtual BOOL GetWindowClosePressed() { return WaitWnd->GetWindowClosePressed(); }
    virtual HANDLE GetFinishedEvent() { return FinishedEvent; }

    // the remaining methods are not used
    virtual void Init(HWND parent, const char* logCmd, const wchar_t* waitWndText,
                      const CFtpTextCodec& textCodec) {}
    virtual void BeforeAborting() {}
    virtual void AfterWrite(BOOL aborting, DWORD showTime) {}
    virtual BOOL HandleESC(HWND parent, BOOL isSend, BOOL allowCmdAbort) { return FALSE; }
    virtual void SendingFinished() {}
    virtual BOOL IsTimeout(DWORD* start, DWORD serverTimeout, int* errorTextID,
                           std::string& errorText) { return FALSE; }
    virtual void MaybeSuccessReplyReceived(const char* reply, int replySize) {}
    virtual void CancelDataCon() {}
    virtual BOOL CanFinishSending(int replyCode, BOOL* useTimeout) { return FALSE; }
    virtual void BeforeWaitingForFinish(int replyCode, BOOL* useTimeout) {}
    virtual void HandleDataConTimeout(DWORD* start) {}
    virtual void HandleESCWhenWaitingForFinish(HWND parent) {}
};

void CControlConnectionSocket::WaitForEndOfKeepAlive(HWND parent, int waitWndTime)
{
    CALL_STACK_MESSAGE2("CControlConnectionSocket::WaitForEndOfKeepAlive(, %d)", waitWndTime);

    DWORD startTime = GetTickCount(); // operation start time

    HANDLES(EnterCriticalSection(&SocketCritSect));

#ifdef _DEBUG
    if (SocketCritSect.RecursionCount > 1)
        TRACE_E("Incorrect call to CControlConnectionSocket::WaitForEndOfKeepAlive(): from section SocketCritSect!");
#endif

    if (!KeepAliveEnabled && KeepAliveMode != kamNone)
        TRACE_E("CControlConnectionSocket::WaitForEndOfKeepAlive(): Keep-Alive is disabled, but Mode == " << (int)KeepAliveMode);

    if (KeepAliveEnabled &&
        (KeepAliveMode == kamProcessing ||               // a keep-alive command is running; we must wait for it to finish
         KeepAliveMode == kamWaitingForEndOfProcessing)) // we are already waiting for completion (should not happen)
    {
        KeepAliveMode = kamWaitingForEndOfProcessing;
        HANDLE finishedEvent = KeepAliveFinishedEvent;
        int logUID = LogUID;
        HANDLES(LeaveCriticalSection(&SocketCritSect));

        // show a wait window to indicate we are waiting for the keep-alive command to finish
        CWaitWindow waitWnd(parent, TRUE);
        waitWnd.SetText(LangStr(IDS_FINISHINGKEEPALIVECMD).c_str());
        DWORD start = GetTickCount();
        DWORD waitTime = start - startTime;
        waitWnd.Create(waitTime < (DWORD)waitWndTime ? waitWndTime - waitTime : 0);

        // wait for the keep-alive command to finish or be interrupted (ESC/timeout)
        int serverTimeout = Config.GetServerRepliesTimeout() * 1000;
        if (serverTimeout < 1000)
            serverTimeout = 1000; // at least one second
        CFinishingKeepAliveUserIface userIface(&waitWnd, finishedEvent);
        BOOL wait = TRUE;
        while (wait)
        {
            CControlConnectionSocketEvent event;
            DWORD data1, data2;
            DWORD now = GetTickCount();
            if (now - start > (DWORD)serverTimeout)
                now = start + (DWORD)serverTimeout;
            WaitForEventOrESC(parent, &event, &data1, &data2, serverTimeout - (now - start),
                              NULL, &userIface, TRUE);
            switch (event)
            {
            case ccsevESC:
            {
                waitWnd.Show(FALSE);
                if (SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_KEEPALIVECMDESC).c_str(),
                                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                     MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                                         MB_ICONQUESTION) == IDYES)
                { // cancel
                    Logs.LogMessage(logUID, LangStr(IDS_LOGMSGACTIONCANCELED).c_str(), -1, TRUE);
                    ReleaseKeepAlive(); // release the keep-alive
                    CloseSocket(NULL);  // close the connection
                    Logs.SetIsConnected(logUID, IsConnected());
                    Logs.RefreshListOfLogsInLogsDlg(); // "connection inactive" notification
                    wait = FALSE;
                }
                else
                {
                    SalamanderGeneral->WaitForESCRelease(); // a measure to keep the next action from being interrupted after every ESC in the previous message box
                    waitWnd.Show(TRUE);
                }
                break;
            }

            case ccsevTimeout:
            {
                BOOL isTimeout = TRUE;

                HANDLES(EnterCriticalSection(&SocketCritSect));
                if (KeepAliveDataCon != NULL)
                {
                    BOOL trFinished;
                    if (KeepAliveDataCon->IsTransfering(&trFinished))
                    { // waiting for data, so this is not a timeout
                        start = GetTickCount();
                        isTimeout = FALSE;
                    }
                    else
                    {
                        if (trFinished)
                        {
                            start = KeepAliveDataCon->GetSocketCloseTime();
                            isTimeout = (GetTickCount() - start) >= (DWORD)serverTimeout; // the timeout is measured from the connection closing (the moment since when the server can react and also learns about the closure)
                        }
                        // else isTimeout = TRUE;  // the connection has not opened yet -> treat it as a timeout
                    }
                }
                HANDLES(LeaveCriticalSection(&SocketCritSect));

                if (isTimeout)
                {
                    Logs.LogMessage(logUID, LangStr(IDS_LOGMSGKEEPALIVECMDTIMEOUT).c_str(), -1, TRUE);
                    ReleaseKeepAlive(); // release the keep-alive
                    CloseSocket(NULL);  // close the connection
                    Logs.SetIsConnected(logUID, IsConnected());
                    Logs.RefreshListOfLogsInLogsDlg(); // "connection inactive" notification
                    wait = FALSE;
                }
                break;
            }

            case ccsevNewBytesRead:
                break; // ignore it (at worst some old event; after this method we write the command anyway and only then the server replies)

            case ccsevClosed: // connection closed; just wrap up keep-alive and let someone else handle it
            {
                ReleaseKeepAlive();
                AddEvent(ccsevClosed, data1, data2);
                wait = FALSE;
                break;
            }

            case ccsevUserIfaceFinished:
                wait = FALSE;
                break; // the keep-alive command has finished

            default:
            {
                TRACE_E("CControlConnectionSocket::WaitForEndOfKeepAlive: Unexpected event (" << (int)event << ").");
                break;
            }
            }
        }
        waitWnd.Destroy();

        // the keep-alive command has already finished or was interrupted (ESC/timeout)
        HANDLES(EnterCriticalSection(&SocketCritSect));
        KeepAliveMode = kamForbidden;
        HANDLES(LeaveCriticalSection(&SocketCritSect));
    }
    else
    {
        if (KeepAliveEnabled)
        {
            BOOL deleteTimer = FALSE;
            int uid;
            if (KeepAliveMode == kamWaiting)
            {
                deleteTimer = TRUE;
                uid = UID;
            }
            KeepAliveMode = kamForbidden;
            HANDLES(LeaveCriticalSection(&SocketCritSect));

            if (deleteTimer)
            {
                // in mode 'kamForbidden' the keep-alive timer makes no sense (if it times out,
                // it is simply ignored); delete it
                SocketsThread->DeleteTimer(uid, CTRLCON_KEEPALIVE_TIMERID);
            }
        }
        else
            HANDLES(LeaveCriticalSection(&SocketCritSect));
    }
}

void CControlConnectionSocket::SetupKeepAliveTimer(BOOL immediate)
{
    CALL_STACK_MESSAGE2("CControlConnectionSocket::SetupKeepAliveTimer(%d)", immediate);

    HANDLES(EnterCriticalSection(&SocketCritSect));

#ifdef _DEBUG
    if (SocketCritSect.RecursionCount > 1)
        TRACE_E("Incorrect call to CControlConnectionSocket::SetupKeepAliveTimer(): from section SocketCritSect!");
#endif

    if (!KeepAliveEnabled && KeepAliveMode != kamNone)
        TRACE_E("CControlConnectionSocket::SetupKeepAliveTimer(): Keep-Alive is disabled, but Mode == " << (int)KeepAliveMode);
    BOOL timer = FALSE;
    int msg;
    int uid;
    DWORD ti;
    if (KeepAliveEnabled && KeepAliveMode == kamForbidden) // called after completing a normal command
    {
        KeepAliveMode = kamWaiting;
        timer = TRUE;
        msg = Msg;
        uid = UID;
        KeepAliveStart = GetTickCount();                                   // time of the last normal command executed in the "control connection"
        ti = KeepAliveStart + (immediate ? 0 : KeepAliveSendEvery * 1000); // time when the first keep-alive command should be sent
    }
    else
    {
        if (KeepAliveMode != kamNone)
            TRACE_E("CControlConnectionSocket::SetupKeepAliveTimer(): unexpected Mode == " << (int)KeepAliveMode);
    }
    HANDLES(LeaveCriticalSection(&SocketCritSect));

    if (timer) // we need to arm the keep-alive timer
        SocketsThread->AddTimer(msg, uid, ti, CTRLCON_KEEPALIVE_TIMERID, NULL);
}

void CControlConnectionSocket::SetupNextKeepAliveTimer()
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::SetupNextKeepAliveTimer()");

    HANDLES(EnterCriticalSection(&SocketCritSect));

#ifdef _DEBUG
    if (SocketCritSect.RecursionCount > 1)
        TRACE_E("Incorrect call to CControlConnectionSocket::SetupNextKeepAliveTimer(): from section SocketCritSect!");
#endif

    if (!KeepAliveCmdAllBytesWritten)
    { // this should never happen because the server's reply arrives only after the complete command is written
        // of the command (the command is always written at once; it is just a few bytes)
        TRACE_E("Unexpected situation in CControlConnectionSocket::SetupNextKeepAliveTimer(): KeepAliveCmdAllBytesWritten==FALSE!");
        KeepAliveCmdAllBytesWritten = TRUE;
    }

    if (KeepAliveDataCon != NULL || KeepAliveDataConState != kadcsNone)
        TRACE_E("Unexpected situation in CControlConnectionSocket::SetupNextKeepAliveTimer(): KeepAliveDataCon!=NULL or KeepAliveDataConState!=kadcsNone!");

    BOOL timer = FALSE;
    int msg;
    int uid;
    DWORD ti;
    if (KeepAliveMode == kamProcessing) // the keep-alive finished normally; decide whether to set the keep-alive timer again
    {
        ti = GetTickCount() + KeepAliveSendEvery * 1000; // time when the next keep-alive command should be sent
        if ((int)((ti - KeepAliveStart) / 60000) < KeepAliveStopAfter)
        {
            KeepAliveMode = kamWaiting;
            timer = TRUE;
            msg = Msg;
            uid = UID;
        }
        else
        {
            KeepAliveMode = kamNone;                                         // we should no longer perform keep-alive (there is no point in protecting the connection anymore)
            Logs.LogMessage(LogUID, LangStr(IDS_LOGMSGKASTOPPED).c_str(), -1, TRUE); // notify the user that the keep-alive mode has stopped
        }
    }
    else
    {
        if (KeepAliveMode == kamWaitingForEndOfProcessing) // the main thread is waiting for the keep-alive command to finish
        {
            SetEvent(KeepAliveFinishedEvent);
        }
        else
        {
            if (KeepAliveMode != kamNone) // kamNone = ReleaseKeepAlive() was called
                TRACE_E("CControlConnectionSocket::SetupNextKeepAliveTimer(): unexpected Mode == " << (int)KeepAliveMode);
        }
    }
    HANDLES(LeaveCriticalSection(&SocketCritSect));

    if (timer) // we need to arm the keep-alive timer
        SocketsThread->AddTimer(msg, uid, ti, CTRLCON_KEEPALIVE_TIMERID, NULL);
}

void CControlConnectionSocket::ReleaseKeepAlive()
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::ReleaseKeepAlive()");

    HANDLES(EnterCriticalSection(&SocketCritSect));

#ifdef _DEBUG
    if (SocketCritSect.RecursionCount > 1)
        TRACE_E("Incorrect call to CControlConnectionSocket::ReleaseKeepAlive(): from section SocketCritSect!");
#endif

    if (KeepAliveMode == kamProcessing || KeepAliveMode == kamWaitingForEndOfProcessing)
        SetEvent(KeepAliveFinishedEvent); // let the main thread continue
    BOOL deleteTimer = FALSE;
    int uid;
    if (KeepAliveMode == kamWaiting)
    {
        deleteTimer = TRUE;
        uid = UID;
    }
    KeepAliveMode = kamNone; // keep-alive reinitialization
    KeepAliveCmdAllBytesWritten = TRUE;
    CKeepAliveDataConSocket* closeDataCon = KeepAliveDataCon;
    KeepAliveDataCon = NULL;
    KeepAliveDataConState = kadcsNone;
    HANDLES(LeaveCriticalSection(&SocketCritSect));

    // if the "data connection" is open, close it; it certainly will not be needed now
    if (closeDataCon != NULL)
    {
        if (closeDataCon->IsConnected())       // close the "data connection"; the system attempts a "graceful"
            closeDataCon->CloseSocketEx(NULL); // shutdown (we will not learn the result)
        DeleteSocket(closeDataCon);            // release the "data connection" through a SocketsThread method call
    }

    if (deleteTimer)
    {
        // in mode 'kamNone' the keep-alive timer makes no sense (if it times out,
        // it is simply ignored); delete it
        SocketsThread->DeleteTimer(uid, CTRLCON_KEEPALIVE_TIMERID);
    }
}

void CControlConnectionSocket::PostMsgToCtrlCon(int msgID, void* msgParam)
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::PostMsgToCtrlCon()");

    HANDLES(EnterCriticalSection(&SocketCritSect));
    int msg = Msg;
    int uid = UID;
    HANDLES(LeaveCriticalSection(&SocketCritSect));

    SocketsThread->PostSocketMessage(msg, uid, msgID, msgParam);
}

void CControlConnectionSocket::ReceiveTimer(DWORD id, void* param)
{
    CALL_STACK_MESSAGE2("CControlConnectionSocket::ReceiveTimer(%u,)", id);
    if (id == CTRLCON_KEEPALIVE_TIMERID)
    {
        BOOL sendKACmd = FALSE;
        int cmd;
        HANDLES(EnterCriticalSection(&SocketCritSect));
        int logUID = LogUID;
        BOOL usePassiveModeAux;
        if (KeepAliveEnabled && KeepAliveMode == kamWaiting) // nothing prevents sending the keep-alive command
        {
            KeepAliveMode = kamProcessing;
            ResetEvent(KeepAliveFinishedEvent); // prepare the event so the main thread can be blocked until the keep-alive command finishes
            sendKACmd = TRUE;
            usePassiveModeAux = UsePassiveMode;

            if (KeepAliveCommand == 2 /* NLST */ || KeepAliveCommand == 3 /* LIST */)
            {
                // allocate the object for the "data connection"
                if (KeepAliveDataCon != NULL)
                    TRACE_E("Unexpected situation in CControlConnectionSocket::ReceiveTimer(): KeepAliveDataCon is not NULL!");
                CFTPProxyForDataCon* dataConProxyServer = ProxyServer == NULL ? NULL : ProxyServer->AllocProxyForDataCon(ServerIP, Host.c_str(), HostIP, Port);
                BOOL dataConProxyServerOK = ProxyServer == NULL || dataConProxyServer != NULL;
                KeepAliveDataCon = dataConProxyServerOK ? new CKeepAliveDataConSocket(this, dataConProxyServer, EncryptDataConnection, pCertificate) : NULL;
                if (KeepAliveDataCon == NULL)
                {
                    if (dataConProxyServer != NULL)
                        delete dataConProxyServer;
                    if (dataConProxyServerOK)
                        TRACE_E(LOW_MEMORY);
                    KeepAliveCommand = 0; // send "NOOP" instead
                }
                else
                    KeepAliveDataConState = usePassiveModeAux ? kadcsWaitForPassiveReply : kadcsWaitForListen;
            }
            cmd = KeepAliveCommand;
        }
        CKeepAliveDataConSocket* keepAliveDataConAux = KeepAliveDataCon;
        HANDLES(LeaveCriticalSection(&SocketCritSect));

        if (sendKACmd)
        {
            // sending the keep-alive command (no delays; we must not wait for anything)
            std::string ftpCmd;
            BOOL waitForListen = FALSE;
            switch (cmd)
            {
            case 0: // NOOP
            {
                PrepareFTPCommand(ftpCmd, NULL, ftpcmdNoOperation, NULL);
                break;
            }

            case 1: // PWD
            {
                PrepareFTPCommand(ftpCmd, NULL, ftpcmdPrintWorkingPath, NULL);
                break;
            }

            case 2: // NLST
            case 3: // LIST
            {
                if (usePassiveModeAux) // passive mode of the "data connection"
                {
                    PrepareFTPCommand(ftpCmd, NULL, ftpcmdPassive, NULL);
                }
                else // active mode of the "data connection"
                {
                    DWORD localIP;
                    GetLocalIP(&localIP, NULL);   // should not be able to fail
                    unsigned short localPort = 0; // listen on any port
                    DWORD error;
                    keepAliveDataConAux->SetActive(logUID);
                    BOOL listenError;
                    if (!keepAliveDataConAux->OpenForListeningWithProxy(localIP, localPort, &listenError, &error))
                    { // failed to open the "listen" socket for receiving the data connection from
                        // the server (a local operation, this should almost never happen) and it can also be
                        // an error when connecting to the proxy server
                        Logs.LogMessage(logUID, LangStr(listenError ? IDS_LOGMSGOPENACTDATACONERROR : IDS_LOGMSGOPENACTDATACONERROR2).c_str(), -1, TRUE);
                    }
                    else
                        waitForListen = TRUE;
                }
                break;
            }

            default:
            {
                TRACE_E("CControlConnectionSocket::ReceiveTimer(): unknown keep-alive command!");
                ftpCmd.clear();
                break;
            }
            }

            if (!ftpCmd.empty() || waitForListen)
                Logs.LogMessage(logUID, LangStr(IDS_LOGMSGKEEPALIVE).c_str(), -1, TRUE);
            if (!ftpCmd.empty())
                SendKeepAliveCmd(logUID, ftpCmd.c_str()); // send the keep-alive command
            else
            {
                if (!waitForListen)
                    ReleaseKeepAlive(); // nothing was sent (continuing keep-alive makes no sense), cancel keep-alive
            }
        }
    }
}

void CControlConnectionSocket::ReceivePostMessage(DWORD id, void* param)
{
    CALL_STACK_MESSAGE2("CControlConnectionSocket::ReceivePostMessage(%u,)", id);
    switch (id)
    {
    case CTRLCON_KAPOSTSETUPNEXT: // the keep-alive command's "data connection" has just finished; the server already sent the listing-end reply, so call SetupNextKeepAliveTimer()
    {
        HANDLES(EnterCriticalSection(&SocketCritSect));
        BOOL call = (KeepAliveMode == kamProcessing || KeepAliveMode == kamWaitingForEndOfProcessing); // nothing unexpected happened?
        CKeepAliveDataConSocket* closeDataCon = KeepAliveDataCon;
        if (call)
        {
            KeepAliveDataCon = NULL;
            KeepAliveDataConState = kadcsNone;
        }
        HANDLES(LeaveCriticalSection(&SocketCritSect));

        if (call)
        {
            if (closeDataCon != NULL)
                DeleteSocket(closeDataCon);
            SetupNextKeepAliveTimer();
        }
        break;
    }

    case CTRLCON_LISTENFORCON: // message about opening the "listen" port (on the proxy server)
    {
        AddEvent(ccsevListenForCon, (DWORD)(DWORD_PTR)param, 0);
        break;
    }

    case CTRLCON_KALISTENFORCON: // keep-alive: message about opening the "listen" port (on the proxy server)
    {
        HANDLES(EnterCriticalSection(&SocketCritSect));
        if ((KeepAliveMode == kamProcessing || KeepAliveMode == kamWaitingForEndOfProcessing) &&
            KeepAliveDataConState == kadcsWaitForListen)
        {
            CKeepAliveDataConSocket* kaDataConnection = KeepAliveDataCon;
            int logUID = LogUID; // log UID of this connection
            HANDLES(LeaveCriticalSection(&SocketCritSect));

            if ((int)(INT_PTR)param == kaDataConnection->GetUID()) // process the message only if it is for our data connection
            {
                DWORD listenOnIP;
                unsigned short listenOnPort;
                if (!kaDataConnection->GetListenIPAndPort(&listenOnIP, &listenOnPort)) // "listen" error
                {
                    std::string proxyError;
                    if (kaDataConnection->GetProxyError(proxyError, NULL, TRUE))
                    { // log the error
                        std::string logMessage;
                        if (FTPFormatString(logMessage, LoadStr(IDS_LOGMSGDATCONERROR),
                                            proxyError.c_str()))
                            Logs.LogMessage(logUID, logMessage.c_str(), -1, TRUE);
                    }
                    ReleaseKeepAlive(); // we are finishing...
                }
                else // success, send the "PORT" command
                {
                    HANDLES(EnterCriticalSection(&SocketCritSect));
                    KeepAliveDataConState = kadcsWaitForSetPortReply;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));

                    std::string command;
                    if (PrepareFTPCommand(command, NULL, ftpcmdSetPort, NULL, listenOnIP, listenOnPort))
                        SendKeepAliveCmd(logUID, command.c_str());
                    else
                        ReleaseKeepAlive();
                }
            }
        }
        else
            HANDLES(LeaveCriticalSection(&SocketCritSect));
        break;
    }
    }
}

BOOL CControlConnectionSocket::InitOperation(CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::InitOperation()");
    HANDLES(EnterCriticalSection(&SocketCritSect));
    BOOL ret = oper->SetConnection(ProxyServer, Host.c_str(), Port, GetTextCodec(), User.c_str(), Password.c_str(), Account.c_str(),
                                   InitFTPCommands.c_str(), UsePassiveMode,
                                   UseLIST_aCommand ? LIST_a_CMD_TEXT : ListCommand.c_str(),
                                   ServerIP, ServerSystem.c_str(), ServerFirstReply.c_str(),
                                   UseListingsCache, HostIP);
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}

//
// ****************************************************************************
// CListingCacheItem
//

CListingCacheItem::CListingCacheItem(const wchar_t* host, unsigned short port, const wchar_t* user,
                                     const char* path, const CFtpTextCodec& textCodec,
                                     const char* listCmd, BOOL isFTPS,
                                     std::string_view cachedListing,
                                     const CFTPDate& cachedListingDate,
                                     DWORD cachedListingStartTime, CFTPServerPathType pathType)
    : Port(port), Anonymous(FALSE), UserLength(0), Valid(FALSE), PathTextValid(FALSE),
      PathType(pathType), IsFTPS(isFTPS), CachedListingDate(cachedListingDate),
      CachedListingStartTime(cachedListingStartTime)
{
    if (user != NULL && wcscmp(user, L"anonymous") == 0)
        user = NULL;
    Anonymous = user == NULL;
    try
    {
        if (host == NULL || path == NULL || listCmd == NULL ||
            cachedListing.size() > static_cast<size_t>(INT_MAX))
            return;
        Host.assign(host);
        if (!Anonymous)
            User.assign(user);
        Path.assign(path);
        ListCmd.assign(listCmd);
        if (!cachedListing.empty())
            CachedListing.assign(cachedListing.data(), cachedListing.size());
        PathTextValid = textCodec.Decode(Path.data(), Path.size(), PathText);
        UserLength = Anonymous ? 0 : FTPGetUserLengthW(User.c_str());
        Valid = TRUE;
    }
    catch (...)
    {
        FTPSecureWipe(CachedListing);
        TRACE_E(LOW_MEMORY);
    }
}

CListingCacheItem::~CListingCacheItem()
{
    FTPSecureWipe(CachedListing);
}

//
// ****************************************************************************
// CListingCache
//

CListingCache::CListingCache() : Cache(100, 50), TotalCacheSize(0, 0)
{
    HANDLES(InitializeCriticalSection(&CacheCritSect));
}

CListingCache::~CListingCache()
{
#ifdef _DEBUG
    int i;
    for (i = 0; i < Cache.Count; i++)
        TotalCacheSize -= CQuadWord(static_cast<DWORD>(Cache[i]->CachedListing.size()), 0);
    if (TotalCacheSize != CQuadWord(0, 0))
        TRACE_E("CListingCache::~CListingCache(): TotalCacheSize is not zero when cache is empty!");
#endif
    HANDLES(DeleteCriticalSection(&CacheCritSect));
}

BOOL CListingCache::Find(const wchar_t* host, unsigned short port, const wchar_t* user,
                         CFTPServerPathType pathType, const char* path, const char* listCmd,
                         BOOL isFTPS, int* index)
{
    if (user != NULL && wcscmp(user, L"anonymous") == 0)
        user = NULL;
    int i;
    for (i = 0; i < Cache.Count; i++)
    {
        CListingCacheItem* item = Cache[i];
        if (SalamanderGeneral->StrICmp(host, item->Host.c_str()) == 0 &&
            (user == NULL && item->Anonymous ||
             !item->Anonymous && user != NULL && item->User == user) &&
            port == item->Port &&
            FTPIsTheSameServerPath(pathType, path, item->Path.c_str()) &&
            isFTPS == item->IsFTPS && listCmd != NULL &&
            _stricmp(listCmd, item->ListCmd.c_str()) == 0)
        {
            *index = i;
            return TRUE;
        }
    }
    return FALSE;
}

CListingCacheLookupStatus CListingCache::GetPathListing(const wchar_t* host, unsigned short port,
                                                        const wchar_t* user,
                                                        CFTPServerPathType pathType,
                                                        std::string& path, const char* listCmd,
                                                        BOOL isFTPS, std::string& cachedListing,
                                                        CFTPDate* cachedListingDate,
                                                        DWORD* cachedListingStartTime) noexcept
{
    HANDLES(EnterCriticalSection(&CacheCritSect));

    int index;
    if (Find(host, port, user, pathType, path.c_str(), listCmd, isFTPS, &index)) // update the cache item
    {
        CListingCacheItem* item = Cache[index];
        std::string canonicalPath;
        std::string listingBytes;
        try
        {
            canonicalPath = item->Path;
            listingBytes = item->CachedListing;
        }
        catch (...)
        {
            TRACE_E(LOW_MEMORY);
            HANDLES(LeaveCriticalSection(&CacheCritSect));
            return CListingCacheLookupStatus::LowMemory;
        }
        *cachedListingDate = item->CachedListingDate;
        *cachedListingStartTime = item->CachedListingStartTime;
        path.swap(canonicalPath);
        cachedListing.swap(listingBytes);
        HANDLES(LeaveCriticalSection(&CacheCritSect));
        return CListingCacheLookupStatus::Found;
    }

    HANDLES(LeaveCriticalSection(&CacheCritSect));
    return CListingCacheLookupStatus::NotFound;
}

void CListingCache::AddOrUpdatePathListing(const wchar_t* host, unsigned short port, const wchar_t* user,
                                           CFTPServerPathType pathType, const char* path,
                                           const CFtpTextCodec& textCodec, const char* listCmd, BOOL isFTPS,
                                           std::string_view cachedListing,
                                           const CFTPDate* cachedListingDate,
                                           DWORD cachedListingStartTime)
{
    CListingCacheItem* item = new CListingCacheItem(host, port, user, path, textCodec, listCmd, isFTPS,
                                                    cachedListing,
                                                    *cachedListingDate,
                                                    cachedListingStartTime, pathType);
    if (item == NULL || !item->IsGood())
    {
        delete item;
        return;
    }

    HANDLES(EnterCriticalSection(&CacheCritSect));

    int replacedIndex;
    const BOOL replacing = Find(host, port, user, pathType, path, listCmd, isFTPS, &replacedIndex);
    const CQuadWord replacedSize = replacing
                                       ? CQuadWord(static_cast<DWORD>(Cache[replacedIndex]->CachedListing.size()), 0)
                                       : CQuadWord(0, 0);

    // Append first so allocation failure leaves an existing usable entry untouched.
    Cache.Add(item);
    if (Cache.IsGood())
    {
        TotalCacheSize += CQuadWord(static_cast<DWORD>(item->CachedListing.size()), 0);
        item = NULL;
        if (replacing)
        {
            TotalCacheSize -= replacedSize;
            Cache.Delete(replacedIndex);
            if (!Cache.IsGood())
                Cache.ResetState();
        }

        // If the cache is oversized, evict oldest entries but keep the newly appended entry.
        int count = 0;
        while (Cache.Count > count + 1 && TotalCacheSize > Config.CacheMaxSize)
            TotalCacheSize -= CQuadWord(static_cast<DWORD>(Cache[count++]->CachedListing.size()), 0);
        if (count > 0)
        {
            Cache.Delete(0, count);
            if (!Cache.IsGood())
                Cache.ResetState();
        }
    }
    else
        Cache.ResetState();
    if (item != NULL)
        delete item;

    HANDLES(LeaveCriticalSection(&CacheCritSect));
}

void CListingCache::RefreshOnPath(const wchar_t* host, unsigned short port, const wchar_t* user,
                                  CFTPServerPathType pathType, const char* path, BOOL ignorePath)
{
    HANDLES(EnterCriticalSection(&CacheCritSect));

    if (user != NULL && wcscmp(user, L"anonymous") == 0)
        user = NULL;
    int delIndex = 0; // variables for deleting in blocks (shifting the array is O(N*N), so we optimize)
    int delCount = 0;
    int i;
    for (i = 0; i < Cache.Count; i++)
    {
        CListingCacheItem* item = Cache[i];
        if (SalamanderGeneral->StrICmp(host, item->Host.c_str()) == 0 &&
            (user == NULL && item->Anonymous ||
             !item->Anonymous && user != NULL && item->User == user) &&
            port == item->Port &&
            (ignorePath || FTPIsPrefixOfServerPath(pathType, path, item->Path.c_str(), FALSE))) // consider the path including its subpaths
        {
            // remove the item from the cache
            TotalCacheSize -= CQuadWord(static_cast<DWORD>(item->CachedListing.size()), 0);
            if (delIndex + delCount == i)
                delCount++; // contiguous with the block being deleted; just extend it
            else            // we must create a new block and delete the previous one
            {
                if (delCount > 0)
                {
                    Cache.Delete(delIndex, delCount);
                    if (!Cache.IsGood())
                        Cache.ResetState();
                    i -= delCount; // adjust the index after deleting the previous block (it has to lie entirely before 'i')
                }
                delIndex = i;
                delCount = 1;
            }
        }
    }
    if (delCount > 0)
    {
        Cache.Delete(delIndex, delCount);
        if (!Cache.IsGood())
            Cache.ResetState();
    }

    HANDLES(LeaveCriticalSection(&CacheCritSect));
}

void CListingCache::AcceptChangeOnPathNotification(const wchar_t* userPart, BOOL includingSubdirs)
{
    if (userPart == NULL)
        return;
    std::wstring buf;
    const wchar_t* pathPart = NULL;
    wchar_t *user, *host, *portStr, *pathStr;
    int port;
    int userLength = -1;

    HANDLES(EnterCriticalSection(&CacheCritSect));

    int delIndex = 0; // variables for deleting in blocks (shifting the array is O(N*N), so we optimize)
    int delCount = 0;
    int i;
    for (i = 0; i < Cache.Count; i++)
    {
        CListingCacheItem* item = Cache[i];
        int itemUserLength = item->UserLength;
        if (userLength == -1 || userLength != itemUserLength)
        {
            userLength = itemUserLength;
            if (!FtpStoreWideText(userPart, buf))
            {
                HANDLES(LeaveCriticalSection(&CacheCritSect));
                return;
            }
            FTPSplitPathW(buf.data(), &user, NULL, &host, &portStr, &pathStr, NULL, userLength);
            if (pathStr != NULL && pathStr > buf.data())
                pathPart = userPart + (pathStr - buf.data()) - 1;
            port = portStr != NULL ? _wtoi(portStr) : IPPORT_FTP;
            if (user != NULL && wcscmp(user, L"anonymous") == 0)
                user = NULL;
            if (host == NULL || pathPart == NULL)
            { // this may still be just a coincidence; we need to try it with an unknown username length
                if (!FtpStoreWideText(userPart, buf))
                {
                    HANDLES(LeaveCriticalSection(&CacheCritSect));
                    return;
                }
                FTPSplitPathW(buf.data(), &user, NULL, &host, &portStr, &pathStr, NULL, 0);
                if (pathStr != NULL && pathStr > buf.data())
                    pathPart = userPart + (pathStr - buf.data()) - 1;
                port = portStr != NULL ? _wtoi(portStr) : IPPORT_FTP;
                if (user != NULL && wcscmp(user, L"anonymous") == 0)
                    user = NULL;
                if (host == NULL || pathPart == NULL)
                {
                    TRACE_EW(L"CListingCache::AcceptChangeOnPathNotification(): invalid (or relative) path received: " << userPart);
                    HANDLES(LeaveCriticalSection(&CacheCritSect));
                    return; // there are no such items in the cache; nothing to do
                }
            }
        }
        if (SalamanderGeneral->StrICmp(host, item->Host.c_str()) == 0 &&
            (user == NULL && item->Anonymous ||
             !item->Anonymous && user != NULL && item->User == user) &&
            port == item->Port && item->PathTextValid &&
            FTPIsPrefixOfServerPathW(item->PathType, FTPGetLocalPathW(pathPart, item->PathType),
                                     item->PathText.c_str(), !includingSubdirs))
        { // the item matches the changed path or its subdirectory, so remove it from the cache
            TotalCacheSize -= CQuadWord(static_cast<DWORD>(item->CachedListing.size()), 0);
            if (delIndex + delCount == i)
                delCount++; // contiguous with the block being deleted; just extend it
            else            // we must create a new block and delete the previous one
            {
                if (delCount > 0)
                {
                    Cache.Delete(delIndex, delCount);
                    if (!Cache.IsGood())
                        Cache.ResetState();
                    i -= delCount; // adjust the index after deleting the previous block (it has to lie entirely before 'i')
                }
                delIndex = i;
                delCount = 1;
            }
        }
    }
    if (delCount > 0)
    {
        Cache.Delete(delIndex, delCount);
        if (!Cache.IsGood())
            Cache.ResetState();
    }

    HANDLES(LeaveCriticalSection(&CacheCritSect));
}
