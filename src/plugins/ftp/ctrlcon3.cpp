// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

//
// ****************************************************************************
// CControlConnectionSocket
//

enum CSendFTPCmdStates // states of the automaton for CControlConnectionSocket::SendFTPCommand
{
    // sending an FTP command
    sfcsSendCommand,

    // aborting an FTP command (sending the "ABOR" command)
    sfcsAbortCommand,

    // sending the abort of an FTP command again without OOB data (sending the "ABOR" command)
    sfcsResendAbortCommand,

    // fatal error (the resource ID of the text is in 'fatalErrorTextID'; -1 uses 'directErrorText')
    sfcsFatalError,

    // fatal error of the operation (the resource ID of the text is in 'opFatalErrorTextID' and the Windows error number in
    // 'opFatalError'; -1 uses 'directErrorText')
    sfcsOperationFatalError,

    // method finished (success or failure indicated by the TRUE/FALSE value of 'ret')
    sfcsDone
};

// **************************************************************************************
// helper object CSendCmdUserIfaceWaitWnd for CControlConnectionSocket::SendFTPCommand()

class CSendCmdUserIfaceWaitWnd : public CSendCmdUserIfaceAbstract
{
protected:
    CWaitWindow WaitWnd;

public:
    CSendCmdUserIfaceWaitWnd(HWND parent) : WaitWnd(parent, TRUE) {}

    virtual void Init(HWND parent, const char* logCmd, const wchar_t* waitWndText,
                      const CFtpTextCodec& textCodec);
    virtual void BeforeAborting() { WaitWnd.SetText(LangStr(IDS_ABORTINGCOMMAND).c_str()); }
    virtual void AfterWrite(BOOL aborting, DWORD showTime) { WaitWnd.Create(showTime); }
    virtual BOOL GetWindowClosePressed() { return WaitWnd.GetWindowClosePressed(); }
    virtual BOOL HandleESC(HWND parent, BOOL isSend, BOOL allowCmdAbort);
    virtual void SendingFinished() { WaitWnd.Destroy(); }
    virtual BOOL IsTimeout(DWORD* start, DWORD serverTimeout, int* errorTextID,
                           std::string& errorText) { return TRUE; }
    virtual void MaybeSuccessReplyReceived(const char* reply, int replySize) {}
    virtual void CancelDataCon() {}

    virtual BOOL CanFinishSending(int replyCode, BOOL* useTimeout) { return TRUE; }
    virtual void BeforeWaitingForFinish(int replyCode, BOOL* useTimeout) {}
    virtual void HandleDataConTimeout(DWORD* start) {}
    virtual HANDLE GetFinishedEvent() { return NULL; }
    virtual void HandleESCWhenWaitingForFinish(HWND parent) {}
};

void CSendCmdUserIfaceWaitWnd::Init(HWND parent, const char* logCmd, const wchar_t* waitWndText,
                                    const CFtpTextCodec& textCodec)
{
    if (waitWndText == NULL) // standard text of the wait window
    {
        std::wstring command;
        if (!textCodec.Decode(logCmd, strlen(logCmd), command))
            command = L"<invalid command text>";
        while (!command.empty() && (command.back() == L'\r' || command.back() == L'\n'))
            command.pop_back();
        try
        {
            const std::wstring text = SPLFormatStringOwned(
                LangStr(IDS_SENDINGCOMMAND).c_str(), command.c_str());
            WaitWnd.SetText(text.c_str());
        }
        catch (...)
        {
            WaitWnd.SetText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str());
        }
    }
    else
        WaitWnd.SetText(waitWndText);
}

BOOL CSendCmdUserIfaceWaitWnd::HandleESC(HWND parent, BOOL isSend, BOOL allowCmdAbort)
{
    WaitWnd.Show(FALSE);
    BOOL esc = SalamanderGeneral->SalMessageBox(parent,
                                                SPLLoadStrOwned(SalamanderGeneral, HLanguage, isSend ? (allowCmdAbort ? IDS_SENDCOMMANDESC : IDS_SENDCOMMANDESC2) : IDS_ABORTCOMMANDESC).c_str(),
                                                SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                MB_YESNO | MSGBOXEX_ESCAPEENABLED | MB_ICONQUESTION) == IDYES;
    if (!esc)
    {
        SalamanderGeneral->WaitForESCRelease(); // prevent the next action from being interrupted by a lingering ESC state from the previous message box
        WaitWnd.Show(TRUE);
    }
    return esc;
}

// *********************************************************************************

static void LogControlDiagnostic(int logUID, const wchar_t* format, ...) noexcept
{
    va_list args;
    va_start(args, format);
    try
    {
        const std::wstring message = SPLFormatStringOwnedV(format, args);
        Logs.LogMessage(logUID, message.c_str(), static_cast<int>(message.size()), TRUE);
    }
    catch (...)
    {
        // Diagnostic logging must not escape the socket callback.
    }
    va_end(args);
}

void WriteUnexpReplyToLog(int logUID, const char* unexpReply,
                          const CFtpTextCodec& textCodec) noexcept
{ // helper function - write "unexpected reply: %s" to the log
    try
    {
        std::wstring reply;
        if (!textCodec.Decode(unexpReply, strlen(unexpReply), reply))
            reply = L"<invalid server text>";
        while (!reply.empty() && (reply.back() == L'\r' || reply.back() == L'\n'))
            reply.pop_back();

        std::wstring message = LangStr(IDS_LOGMSGUNEXPREPLY);
        message.append(reply);
        message.append(L"\r\n");
        Logs.LogMessage(logUID, message.c_str(), static_cast<int>(message.size()));
    }
    catch (...)
    {
        // Unexpected replies are diagnostic; allocation failure must not abort the command.
    }
}

static std::wstring DecodeServerText(const char* bytes, const CFtpTextCodec& textCodec)
{
    std::wstring text;
    if (bytes == NULL || !textCodec.Decode(bytes, strlen(bytes), text))
        return L"<invalid server text>";
    return text;
}

static std::wstring FormatChangeWorkingPathError(const char* path, const std::string& reply,
                                                  const CFtpTextCodec& textCodec)
{
    std::wstring message;
    if (!FtpFormatServerReplyMessage(textCodec,
                                     LangStr(IDS_CHANGEWORKPATHERROR).c_str(),
                                     path != NULL ? std::string_view(path) : std::string_view(),
                                     reply, message))
        TRACE_E(LOW_MEMORY);
    return message;
}

// *********************************************************************************

BOOL CControlConnectionSocket::SendFTPCommand(HWND parent, const char* ftpCmd, const char* logCmd,
                                              const wchar_t* waitWndText, int waitWndTime, BOOL* cmdAborted,
                                              int* ftpReplyCode, std::string* ftpReply, BOOL allowCmdAbort,
                                              BOOL resetWorkingPathCache, BOOL resetCurrentTransferModeCache,
                                              BOOL* canRetry, std::string* retryMessage,
                                              CSendCmdUserIfaceAbstract* specialUserInterface)
{
    return SendFTPCommandInternal(parent, ftpCmd, logCmd, waitWndText, waitWndTime, cmdAborted,
                                  ftpReplyCode, ftpReply, allowCmdAbort,
                                  resetWorkingPathCache, resetCurrentTransferModeCache, canRetry,
                                  retryMessage, specialUserInterface);
}

BOOL CControlConnectionSocket::SendFTPCommandInternal(HWND parent, const char* ftpCmd, const char* logCmd,
                                                      const wchar_t* waitWndText, int waitWndTime, BOOL* cmdAborted,
                                                      int* ftpReplyCode, std::string* ftpReply, BOOL allowCmdAbort,
                                                      BOOL resetWorkingPathCache, BOOL resetCurrentTransferModeCache,
                                                      BOOL* canRetry, std::string* retryMessage,
                                                      CSendCmdUserIfaceAbstract* specialUserInterface)
{
    CALL_STACK_MESSAGE6("CControlConnectionSocket::SendFTPCommand(, , %s, , %d, , , , %d, %d, %d, , ,)",
                        logCmd, waitWndTime, allowCmdAbort, resetWorkingPathCache,
                        resetCurrentTransferModeCache);

    parent = FindPopupParent(parent);
    DWORD startTime = GetTickCount(); // start time of the operation
    if (canRetry != NULL)
        *canRetry = FALSE;
    if (retryMessage != NULL)
        retryMessage->clear();

    std::string directErrorText;
    std::string abortCommand;
    std::string abortLogCommand;

    // if the CSendCmdUserIfaceWaitWnd user interface should be used, create it here
    CSendCmdUserIfaceAbstract* userIface = specialUserInterface;
    CSendCmdUserIfaceWaitWnd objSendCmdUserIfaceWaitWnd(parent); // do not allocate it (unnecessary error handling)
    if (userIface == NULL)
        userIface = &objSendCmdUserIfaceWaitWnd;

    userIface->Init(parent, logCmd, waitWndText, GetTextCodec());

    if (cmdAborted != NULL)
        *cmdAborted = FALSE;
    *ftpReplyCode = -1;
    if (ftpReply != NULL)
        ftpReply->clear();

    const auto publishReply = [ftpReply](const char* reply, int replySize) -> BOOL
    {
        if (ftpReply == NULL)
            return TRUE;
        return FtpStoreProtocolBytes(
            std::string_view(reply, static_cast<size_t>(replySize)), *ftpReply);
    };

    HWND focusedWnd = NULL;
    BOOL parentIsEnabled = IsWindowEnabled(parent);
    CSetWaitCursorWindow* winParent = NULL;
    if (parentIsEnabled) // we cannot leave the parent enabled (the wait window is not modal)
    {
        // store the focus from 'parent' (if the focus is not from 'parent', store NULL)
        focusedWnd = GetFocus();
        HWND hwnd = focusedWnd;
        while (hwnd != NULL && hwnd != parent)
            hwnd = GetParent(hwnd);
        if (hwnd != parent)
            focusedWnd = NULL;
        // disable the 'parent'; when enabling it restore the focus as well
        EnableWindow(parent, FALSE);

        // set the wait cursor over the parent, unfortunately we do not know another way
        winParent = new CSetWaitCursorWindow;
        if (winParent != NULL)
            winParent->AttachToWindow(parent);
    }

    BOOL ret = FALSE;
    int fatalErrorTextID = 0;
    int opFatalErrorTextID = 0;
    int opFatalError = 0;
    BOOL fatalErrLogMsg = TRUE; // FALSE = do not print the error message to the log (reason: it has already been printed there)
    BOOL aborting = FALSE;
    BOOL cmdReplyReceived = FALSE; // only when aborting: TRUE = the reply to the command has already arrived (we can wait for the abort reply)
    BOOL donotRetry = FALSE;       // TRUE = retry makes no sense for this error

    int serverTimeout = Config.GetServerRepliesTimeout() * 1000;
    if (serverTimeout < 1000)
        serverTimeout = 1000; // at least one second

    HANDLES(EnterCriticalSection(&SocketCritSect));
    int logUID = LogUID; // log UID of this connection
    BOOL auxCanSendOOBData = CanSendOOBData;
    BOOL handleKeepAlive = KeepAliveMode != kamForbidden; // TRUE if keep-alive is not handled at a higher level (it must be handled here)
    HANDLES(LeaveCriticalSection(&SocketCritSect));

    if (handleKeepAlive)
    {
        // wait for the keep-alive command to finish (if it is in progress) + set
        // keep-alive to 'kamForbidden' (a normal command is running)
        DWORD waitTime = GetTickCount() - startTime;
        WaitForEndOfKeepAlive(parent, waitTime < (DWORD)waitWndTime ? waitWndTime - waitTime : 0);
    }

    CSendFTPCmdStates state = sfcsSendCommand;
    while (state != sfcsDone)
    {
        CALL_STACK_MESSAGE2("state = %d", state); // aids debugging by showing where the automaton stalled or failed
        switch (state)
        {
        case sfcsResendAbortCommand:
        {
            state = sfcsAbortCommand; // continue processing as sfcsAbortCommand
                                      // break intentionally omitted
        }
        case sfcsAbortCommand:
        {
            if (!PrepareFTPCommand(abortCommand, &abortLogCommand, ftpcmdAbort, NULL))
            { // unexpected error ("always false")
                state = sfcsDone;
                userIface->CancelDataCon();
                break;
            }

            userIface->BeforeAborting();
            aborting = TRUE;
            // break intentionally omitted (continue processing as sfcsSendCommand)
        }
        case sfcsSendCommand:
        {
            CSendFTPCmdStates sendState = state;

            DWORD error;
            BOOL allBytesWritten;

            if (aborting && auxCanSendOOBData)
            {
                // notify the server about the abort (see RFC 959 - sending the ABOR command)
                HANDLES(EnterCriticalSection(&SocketCritSect));
                if (Socket != INVALID_SOCKET &&
                    BytesToWriteCount == 0) // "always true" (all data should have been sent)
                {
                    // send the "TELNET IP" sequence (interrupt process)
                    int sentLen;
                    if ((sentLen = send(Socket, "\xff\xf4" /* IAC+IP */, 2, 0)) != 2)
                    {                     // almost "always false", log the error; it might be useful for debugging problems
                        if (sentLen == 1) // the second byte was not sent, add it to the abort command (it will be sent afterwards)
                        {
                            try
                            {
                                abortCommand.insert(abortCommand.begin(), '\xf4'); // IP (IAC has already been sent)
                            }
                            catch (...)
                            {
                                TRACE_E(LOW_MEMORY);
                            }
                        }
                        DWORD err = WSAGetLastError();
                        LogControlDiagnostic(logUID, L"Unable to send TELNET-IP: error = %u (%d)\r\n", err, sentLen);
                    }

                    // send the TELNET "Synch" signal - the socket is non-blocking, so the only risk is that the TELNET
                    // "Synch" is not sent; we will not handle this error (it is unlikely and unimportant)
                    if (sentLen == 2 && // only if IAC+IP was sent successfully (otherwise it makes no sense)
                        (sentLen = send(Socket, "\xF2" /* DM */, 1, MSG_OOB)) != 1)
                    { // almost "always false", log the error; it might be useful for debugging problems
                        DWORD err = WSAGetLastError();
                        LogControlDiagnostic(logUID, L"Unable to send TELNET \"Synch\" signal: error = %u (%d)\r\n", err, sentLen);
                    }
                }
                HANDLES(LeaveCriticalSection(&SocketCritSect));
            }

            std::string unexpReply;
            int unexpReplyCode = -1;
            if (!aborting) // try to skip extra server replies (they should not exist at all, but unfortunately they do - WarFTPD generates "550 access denied" twice after listing a directory the user cannot access)
            {
                char* reply;
                int replySize;
                int replyCode;

                HANDLES(EnterCriticalSection(&SocketCritSect));
                while (ReadFTPReply(&reply, &replySize, &replyCode)) // as long as we have any server reply
                {
                    if (!unexpReply.empty())
                        WriteUnexpReplyToLog(logUID, unexpReply.c_str(), GetTextCodec());

                    try
                    {
                        unexpReply.assign(reply, static_cast<size_t>(replySize));
                        unexpReplyCode = replyCode;
                    }
                    catch (...)
                    {
                        unexpReply.clear();
                        unexpReplyCode = -1;
                    }

                    SkipFTPReply(replySize);
                }
                HANDLES(LeaveCriticalSection(&SocketCritSect));
            }

            if (Write(!aborting ? ftpCmd : abortCommand.c_str(), -1, &error, &allBytesWritten))
            {
                if (!unexpReply.empty()) // the write succeeded, we no longer need the unexpected reply -> write it to the log and discard it
                {
                    WriteUnexpReplyToLog(logUID, unexpReply.c_str(), GetTextCodec());
                    unexpReply.clear();
                }
                Logs.LogServerMessage(logUID, !aborting ? logCmd : abortLogCommand.c_str(), -1,
                                      TextPolicy);

                DWORD start = GetTickCount();
                DWORD waitTime = start - startTime;
                userIface->AfterWrite(aborting, waitTime < (DWORD)waitWndTime ? waitWndTime - waitTime : 0);

                BOOL isCanceled = FALSE;
                while (!allBytesWritten || state == sendState)
                {
                    // wait for an event on the socket (server reply) or ESC
                    CControlConnectionSocketEvent event;
                    DWORD data1, data2;
                    DWORD now = GetTickCount();
                    if (now - start > (DWORD)serverTimeout)
                        now = start + (DWORD)serverTimeout;
                    WaitForEventOrESC(parent, &event, &data1, &data2, serverTimeout - (now - start),
                                      NULL, userIface, FALSE);
                    switch (event)
                    {
                    case ccsevESC:
                    {
                        if (userIface->HandleESC(parent, state == sfcsSendCommand, allowCmdAbort))
                        {                                                  // cancel
                            if (allowCmdAbort && state == sfcsSendCommand) // cancel for the command -> start aborting the command
                            {
                                state = sfcsAbortCommand;
                                // allBytesWritten = TRUE;   // we must wait until the command is sent before starting to send the abort
                                // we will not display the wait window again; in theory sending should never stall -> ignore it (the user will wait without the wait window)
                            }
                            else // cannot use abort (we must close the connection) or cancel while aborting the command
                            {
                                state = sfcsDone;
                                isCanceled = TRUE;
                                allBytesWritten = TRUE;                                               // no longer important now, the socket will be closed
                                Logs.LogMessage(logUID, LangStr(IDS_LOGMSGACTIONCANCELED).c_str(), -1, TRUE); // ESC (cancel) to the log
                            }
                        }
                        break;
                    }

                    case ccsevTimeout:
                    {
                        int errorTextID = IDS_SNDORABORCMDTIMEOUT;
                        directErrorText.clear();
                        if (userIface->IsTimeout(&start, serverTimeout, &errorTextID, directErrorText))
                        {
                            fatalErrorTextID = errorTextID;
                            state = sfcsFatalError;
                            allBytesWritten = TRUE; // no longer important now, the socket will be closed
                        }
                        break;
                    }

                    case ccsevWriteDone:
                        allBytesWritten = TRUE; // all bytes have already been sent (also handle that ccsevWriteDone could overwrite ccsevNewBytesRead)
                    case ccsevClosed:           // possible unexpected loss of connection (also handle that ccsevClosed could overwrite ccsevNewBytesRead)
                    case ccsevNewBytesRead:     // new bytes have been read
                    {
                        char* reply;
                        int replySize;
                        int replyCode;

                        HANDLES(EnterCriticalSection(&SocketCritSect));
                        while (ReadFTPReply(&reply, &replySize, &replyCode)) // as long as we have any server reply
                        {
                            Logs.LogServerMessage(logUID, reply, replySize, TextPolicy);

                            if (state != sfcsFatalError && state != sfcsOperationFatalError && // only if we do not already have another error
                                replyCode == -1)                                               // not an FTP reply, we are done
                            {
                                opFatalErrorTextID = IDS_NOTFTPSERVERERROR;
                                allBytesWritten = TRUE; // no longer important now, the socket will be closed
                                if (FtpStoreProtocolBytes(std::string_view(reply, static_cast<size_t>(replySize)), directErrorText))
                                {
                                    opFatalError = -1; // the error reply is in directErrorText
                                    state = sfcsOperationFatalError;
                                    fatalErrLogMsg = FALSE; // it is already in the log, no point adding it again
                                }
                                else
                                {
                                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                    state = sfcsFatalError;
                                }
                                donotRetry = TRUE;      // retry makes no sense
                                SkipFTPReply(replySize);
                                break;
                            }

                            if (FTP_DIGIT_1(replyCode) != FTP_D1_MAYBESUCCESS)
                            { // replies of type FTP_D1_MAYBESUCCESS are logged only (we are waiting for the server's "last word")
                                if (event != ccsevClosed)
                                {
                                    if (!aborting) // send command
                                    {              // state can also be sfcsAbortCommand; then we must abort the command even if it succeeded just now (the user ordered an abort)
                                        if (state != sfcsAbortCommand)
                                        {
                                            state = sfcsDone;
                                            *ftpReplyCode = replyCode;
                                            if (!publishReply(reply, replySize))
                                                TRACE_E("Unable to allocate storage for the FTP server reply.");
                                            ret = TRUE; // SUCCESS, we have the server's reply! (for sending we only care about a single server reply)
                                        }
                                        // else; // continue waiting for the abort command (we have not sent ABOR yet)
                                        SkipFTPReply(replySize);
                                        break;
                                    }
                                    else // abort command
                                    {
                                        if (auxCanSendOOBData &&                      // we were sending OOB data
                                            FTP_DIGIT_1(replyCode) == FTP_D1_ERROR && // the reply is a syntax error
                                            FTP_DIGIT_2(replyCode) == FTP_D2_SYNTAX)
                                        {                                               // the server probably does not understand OOB data and inserted them directly into the data stream (the server does not know the "\xF2ABOR" command)
                                            auxCanSendOOBData = CanSendOOBData = FALSE; // do not try OOB again on this "control connection"
                                            state = sfcsResendAbortCommand;
                                            SkipFTPReply(replySize);
                                            break;
                                        }
                                        else
                                        {
                                            // this is the reply for the sent or aborted command; we will still try to read
                                            // additional server replies (some servers send one more for ABOR)
                                            if (!cmdReplyReceived) // return the first reply (it should belong to the command but it may be
                                            {                      // for ABOR as well) - ignore any possible second reply (to ABOR)
                                                state = sfcsDone;
                                                *ftpReplyCode = replyCode;
                                                if (!publishReply(reply, replySize))
                                                    TRACE_E("Unable to allocate storage for the FTP server reply.");
                                                if (cmdAborted != NULL)
                                                    *cmdAborted = TRUE;
                                                ret = TRUE; // SUCCESS, we have the server's reply to the command/abort
                                                cmdReplyReceived = TRUE;
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    if (state != sfcsFatalError && state != sfcsOperationFatalError &&
                                        (FTP_DIGIT_1(replyCode) == FTP_D1_TRANSIENTERROR ||
                                         FTP_DIGIT_1(replyCode) == FTP_D1_ERROR)) // e.g. 421 Service not available, closing control connection
                                    {                                             // discard all server replies one by one, log the first error we find
                                        if (FtpStoreProtocolBytes(std::string_view(reply, static_cast<size_t>(replySize)), directErrorText))
                                        {
                                            fatalErrorTextID = -1; // the error text is in directErrorText
                                            state = sfcsFatalError;
                                            allBytesWritten = TRUE; // no longer important now, the socket will be closed
                                            fatalErrLogMsg = FALSE; // it is already in the log, no point adding it again
                                        }
                                        else
                                        {
                                            fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                            state = sfcsFatalError;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                userIface->MaybeSuccessReplyReceived(reply, replySize); // passive mode: try to encrypt the data connection (only if the command uses it)
                            }
                            SkipFTPReply(replySize);
                        }
                        HANDLES(LeaveCriticalSection(&SocketCritSect));

                        if (event == ccsevClosed)
                        {
                            allBytesWritten = TRUE; // no longer important now, the socket was closed
                            if (state == sfcsSendCommand || state == sfcsAbortCommand || state == sfcsResendAbortCommand)
                            { // close without a cause (whether during/after send or before/during/after abort)
                                fatalErrorTextID = IDS_CONNECTIONLOSTERROR;
                                state = sfcsFatalError;
                            }
                            if (data1 != NO_ERROR)
                            {
                                std::string errorText;
                                if (FTPGetErrorTextForLog(data1, errorText))
                                    Logs.LogMessage(logUID, errorText.c_str(), -1);
                            }
                        }
                        break;
                    }

                    default:
                        TRACE_E("Unexpected event = " << event);
                        break;
                    }
                }

                if (!isCanceled && !aborting && state == sfcsDone)
                { // the server reports "done", we will still wait for the user interface ("data connection") to close
                    BOOL calledBeforeWaitingForFinish = FALSE;
                    BOOL useTimeout = FALSE;    // TRUE = use the 'serverTimeout2' timeout while waiting for the data connection to finish
                    int serverTimeout2 = 10000; // timeout for finishing the data connection when LIST returns an error or the connection has not been opened yet is 10 seconds
                    DWORD start2 = GetTickCount();
                    while (!userIface->CanFinishSending(*ftpReplyCode, &useTimeout))
                    {
                        if (!calledBeforeWaitingForFinish) // call only the first time
                        {
                            DWORD waitTime2 = GetTickCount() - startTime;
                            userIface->BeforeWaitingForFinish(*ftpReplyCode, &useTimeout);
                            calledBeforeWaitingForFinish = TRUE;
                        }

                        // wait for the user interface to close, for a timeout, or for ESC
                        CControlConnectionSocketEvent event;
                        DWORD data1, data2;
                        DWORD now = GetTickCount();
                        if (now - start2 > (DWORD)serverTimeout2)
                            now = start2 + (DWORD)serverTimeout2;
                        WaitForEventOrESC(parent, &event, &data1, &data2,
                                          useTimeout ? serverTimeout2 - (now - start2) : INFINITE,
                                          NULL, userIface, TRUE);
                        switch (event)
                        {
                        case ccsevUserIfaceFinished:
                            break; // CanFinishSending() will hopefully return TRUE now

                        case ccsevTimeout:
                        {
                            if (useTimeout)
                                userIface->HandleDataConTimeout(&start2);
                            else
                                TRACE_E("Unexpected event ccsevTimeout!");
                            break;
                        }

                        case ccsevESC:
                        {
                            userIface->HandleESCWhenWaitingForFinish(parent);
                            break;
                        }

                        default:
                            TRACE_E("Unexpected event (waiting for closing of user-iface) = " << event);
                            break;
                        }
                    }

                    // check whether the control connection closed while finishing reading the data connection
                    if (!IsConnected())
                    {
                        HANDLES(EnterCriticalSection(&EventCritSect));
                        DWORD error2 = NO_ERROR;
                        int i;
                        for (i = 0; i < EventsUsedCount; i++) // check whether the ccsevClosed event is present
                        {
                            if (Events[i]->Event == ccsevClosed)
                            {
                                error2 = Events[i]->Data1;
                                break;
                            }
                        }
                        HANDLES(LeaveCriticalSection(&EventCritSect));

                        fatalErrorTextID = IDS_CONNECTIONLOSTERROR;
                        state = sfcsFatalError;
                        if (error2 != NO_ERROR)
                        {
                            std::string errorText;
                            if (FTPGetErrorTextForLog(error2, errorText))
                                Logs.LogMessage(logUID, errorText.c_str(), -1);
                        }
                        ret = FALSE; // the control connection is closed
                    }
                }
                else
                {
                    if (state != sfcsResendAbortCommand && state != sfcsAbortCommand)
                        userIface->CancelDataCon();
                }

                userIface->SendingFinished();
            }
            else // Write error (low memory, disconnected, non-blocking "send" error)
            {
                if (aborting)
                    userIface->CancelDataCon();
                while (state == sendState)
                {
                    // pick an event on the socket
                    CControlConnectionSocketEvent event;
                    DWORD data1, data2;
                    WaitForEventOrESC(parent, &event, &data1, &data2, 0, NULL, NULL, FALSE); // do not wait, just collect events
                    switch (event)
                    {
                    // case ccsevESC:   // (the user cannot press ESC during a 0 ms timeout)
                    case ccsevTimeout: // no message is waiting -> display the error from Write directly
                    {
                        opFatalErrorTextID = !aborting ? IDS_SENDCOMMANDERROR : IDS_ABORTCOMMANDERROR;
                        opFatalError = error;
                        state = sfcsOperationFatalError;
                        break;
                    }

                    case ccsevClosed:       // unexpected loss of connection (also handle that ccsevClosed could overwrite ccsevNewBytesRead)
                    case ccsevNewBytesRead: // read new bytes (possibly the error description that caused the disconnect)
                    {
                        char* reply;
                        int replySize;
                        int replyCode;

                        BOOL done = FALSE;
                        if (!unexpReply.empty())
                        {
                            Logs.LogMessage(logUID, unexpReply.c_str(), -1);

                            if (unexpReplyCode == -1 ||                                 // not an FTP reply
                                FTP_DIGIT_1(unexpReplyCode) == FTP_D1_TRANSIENTERROR || // description of a temporary error
                                FTP_DIGIT_1(unexpReplyCode) == FTP_D1_ERROR)            // description of an error
                            {
                                opFatalErrorTextID = !aborting ? IDS_SENDCOMMANDERROR : IDS_ABORTCOMMANDERROR;
                                if (FtpStoreProtocolBytes(unexpReply.c_str(), directErrorText))
                                {
                                    opFatalError = -1;      // the error reply is in directErrorText
                                    fatalErrLogMsg = FALSE; // the error is already in the log, do not add it again
                                    state = sfcsOperationFatalError;
                                    done = TRUE; // no need to read another message
                                }
                                else
                                {
                                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                    state = sfcsFatalError;
                                    done = TRUE;
                                }
                            }
                        }

                        if (!done)
                        {
                            HANDLES(EnterCriticalSection(&SocketCritSect));
                            while (ReadFTPReply(&reply, &replySize, &replyCode)) // as long as we have any server reply
                            {
                                Logs.LogServerMessage(logUID, reply, replySize, TextPolicy);

                                if (replyCode == -1 ||                                 // not an FTP reply
                                    FTP_DIGIT_1(replyCode) == FTP_D1_TRANSIENTERROR || // description of a temporary error
                                    FTP_DIGIT_1(replyCode) == FTP_D1_ERROR)            // description of an error
                                {
                                    opFatalErrorTextID = !aborting ? IDS_SENDCOMMANDERROR : IDS_ABORTCOMMANDERROR;
                                    const BOOL storedReply = FtpStoreProtocolBytes(
                                        std::string_view(reply, static_cast<size_t>(replySize)), directErrorText);
                                    SkipFTPReply(replySize);
                                    if (storedReply)
                                    {
                                        opFatalError = -1;      // the error reply is in directErrorText
                                        fatalErrLogMsg = FALSE; // the error is already in the log, do not add it again
                                        state = sfcsOperationFatalError;
                                    }
                                    else
                                    {
                                        fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                        state = sfcsFatalError;
                                    }
                                    break; // no need to read another message
                                }
                                SkipFTPReply(replySize);
                            }
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                        }

                        if (event == ccsevClosed)
                        {
                            if (state == sendState) // close without a cause
                            {
                                fatalErrorTextID = IDS_CONNECTIONLOSTERROR;
                                state = sfcsFatalError;
                            }
                            if (data1 != NO_ERROR)
                            {
                                std::string errorText;
                                if (FTPGetErrorTextForLog(data1, errorText))
                                    Logs.LogMessage(logUID, errorText.c_str(), -1);
                            }
                        }
                        break;
                    }
                    }
                }
            }
            break;
        }

        case sfcsFatalError: // fatal error (resource ID in 'fatalErrorTextID'; -1 uses 'directErrorText')
        {
            std::string errorText;
            if (!GetFatalErrorText(fatalErrorTextID, directErrorText, errorText))
            {
                if (!FtpEncodeLocalTextForByteLog(LangStr(IDS_OPERDOPPR_LOWMEM).c_str(),
                                                  "Low memory", errorText))
                    errorText = "Low memory";
            }
            TrimLineEnds(errorText);
            if (fatalErrLogMsg)
            {
                std::string logText;
                if (FTPFormatString(logText, "%s\r\n", errorText.c_str()))
                    Logs.LogMessage(logUID, logText.c_str(), -1, TRUE);
            }
            fatalErrLogMsg = TRUE;
            if (canRetry == NULL || retryMessage == NULL || donotRetry) // "retry" is not possible or does not make sense
            {
                std::wstring errorTextW;
                if (!FtpDecodeLocalText(errorText, errorTextW))
                    errorTextW = L"<invalid local error text>";
                SalamanderGeneral->SalMessageBox(parent, errorTextW.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                 MB_OK | MB_ICONEXCLAMATION);
            }
            else if (FtpStoreProtocolBytes(errorText, *retryMessage))
            {
                *canRetry = TRUE;
            }
            else
                TRACE_E(LOW_MEMORY);
            state = sfcsDone;
            break;
        }

        case sfcsOperationFatalError: // fatal operation error (resource ID in 'opFatalErrorTextID'; the Windows error
        {                             // is in 'opFatalError'; -1 uses 'directErrorText')
            std::string errorText;
            if (!GetOperationFatalErrorText(opFatalError, directErrorText, errorText))
            {
                if (!FtpEncodeLocalTextForByteLog(LangStr(IDS_OPERDOPPR_LOWMEM).c_str(),
                                                  "Low memory", errorText))
                    errorText = "Low memory";
            }
            TrimLineEnds(errorText);
            if (fatalErrLogMsg)
            {
                std::string logText;
                if (FTPFormatString(logText, "%s\r\n", errorText.c_str()))
                    Logs.LogMessage(logUID, logText.c_str(), -1, TRUE);
            }
            fatalErrLogMsg = TRUE;

            if (canRetry == NULL || retryMessage == NULL || donotRetry) // "retry" is not possible or does not make sense
            {
                try
                {
                    std::wstring errorTextW;
                    if (!FtpDecodeLocalText(errorText, errorTextW))
                        errorTextW = L"<invalid local error text>";
                    const std::wstring message = SPLFormatStringOwned(LangStr(opFatalErrorTextID).c_str(), errorTextW.c_str());
                    SalamanderGeneral->SalMessageBox(parent, message.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                     MB_OK | MB_ICONEXCLAMATION);
                }
                catch (...)
                {
                    SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_OPERDOPPR_LOWMEM).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                     MB_OK | MB_ICONEXCLAMATION);
                }
            }
            else if (FtpStoreProtocolBytes(errorText, *retryMessage))
            {
                *canRetry = TRUE;
            }
            else
                TRACE_E(LOW_MEMORY);
            state = sfcsDone;
            break;
        }

        default: // (always false)
        {
            TRACE_E("Unexpected situation in CControlConnectionSocket::SendFTPCommand(): state = " << state);
            state = sfcsDone;
            break;
        }
        }
    }

    if (parentIsEnabled) // if we disabled the parent, enable it again
    {
        // remove the wait cursor over the parent
        if (winParent != NULL)
        {
            winParent->DetachWindow();
            delete winParent;
        }

        // enable the 'parent'
        EnableWindow(parent, TRUE);
        // if the 'parent' is active, restore focus as well
        if (GetForegroundWindow() == parent)
        {
            if (parent == SalamanderGeneral->GetMainWindowHWND())
                SalamanderGeneral->RestoreFocusInSourcePanel();
            else
            {
                if (focusedWnd != NULL)
                    SetFocus(focusedWnd);
            }
        }
    }

    if (resetWorkingPathCache)
        ResetWorkingPathCache(); // if a change to the working path is likely, reset the cache
    if (resetCurrentTransferModeCache)
        ResetCurrentTransferModeCache(); // if a change to the transfer mode is likely, reset the cache

    if (ret) // the connection is OK, no timeout occurred
    {
        if (handleKeepAlive)
        {
            // if everything is OK, set up the keep-alive timer
            SetupKeepAliveTimer();
        }
    }
    else // connection interrupted or timeout (the socket cannot be used anymore)
    {
        CloseSocket(NULL); // close the socket (if it is open); the system will attempt a "graceful" shutdown (we will not learn the result)
        Logs.SetIsConnected(logUID, IsConnected());
        Logs.RefreshListOfLogsInLogsDlg(); // "connection inactive" notification

        if (handleKeepAlive)
        {
            // release keep-alive, it is no longer needed (the connection is no longer established)
            ReleaseKeepAlive();
        }
    }

    return ret;
}

BOOL CControlConnectionSocket::GetCurrentWorkingPath(HWND parent, std::string& path,
                                                     BOOL forceRefresh, BOOL* canRetry,
                                                     std::string* retryMessage)
{
    CALL_STACK_MESSAGE2("CControlConnectionSocket::GetCurrentWorkingPath(, , %d, , ,)",
                        forceRefresh);

    if (canRetry != NULL)
        *canRetry = FALSE;
    if (retryMessage != NULL)
        retryMessage->clear();

    HANDLES(EnterCriticalSection(&SocketCritSect));
    BOOL leaveSect = TRUE;

    if (!HaveWorkingPath || forceRefresh)
    {
        std::string cmdBuf;
        std::string logBuf;
        std::string reply;

        HANDLES(LeaveCriticalSection(&SocketCritSect));
        leaveSect = FALSE;

        // determine the working directory on the server
        int ftpReplyCode;
        if (PrepareFTPCommand(cmdBuf, &logBuf, ftpcmdPrintWorkingPath, NULL) &&
            SendFTPCommand(parent, cmdBuf.c_str(), logBuf.c_str(), NULL, GetWaitTime(WAITWND_COMOPER), NULL,
                           &ftpReplyCode, &reply, FALSE, FALSE, FALSE, canRetry,
                           retryMessage, NULL))
        {
            std::string parsedWorkingPath;
            const BOOL successfulReply = FTP_DIGIT_1(ftpReplyCode) == FTP_D1_SUCCESS;
            const BOOL validWorkingPath = !successfulReply ||
                                          FTPGetDirectoryFromReply(reply, parsedWorkingPath);
            HANDLES(EnterCriticalSection(&SocketCritSect));
            if (validWorkingPath)
            {
                if (successfulReply)
                    WorkingPath.swap(parsedWorkingPath);
                else
                    WorkingPath.clear(); // temporarily use an empty path
                leaveSect = TRUE;
                HaveWorkingPath = TRUE; // we have the working directory
            }
            else // fatal error, cannot determine the working directory; close the connection and return an error
            {
                int logUID = LogUID; // log UID of this connection
                HANDLES(LeaveCriticalSection(&SocketCritSect));

                CloseSocket(NULL); // close the socket (if it is open); the system will attempt a "graceful" shutdown (we will not learn the result)
                Logs.SetIsConnected(logUID, IsConnected());
                Logs.RefreshListOfLogsInLogsDlg(); // "connection inactive" notification
                Logs.LogMessage(logUID, LangStr(IDS_LOGMSGFATALERROR).c_str(), -1, TRUE);

                ReleaseKeepAlive(); // on error release keep-alive (cannot be used without an established connection)

                std::wstring replyText;
                if (!DecodeText(reply.data(), reply.size(), replyText))
                    replyText = L"<invalid server text>";
                std::wstring messageText = LangStr(IDS_GETCURWORKPATHERROR);
                std::wstring formattedMessage;
                if (FtpFormatWideText(messageText.c_str(), replyText, formattedMessage))
                    messageText.swap(formattedMessage);
                SalamanderGeneral->SalMessageBox(parent, messageText.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                 MB_OK | MB_ICONEXCLAMATION);
            }
        }
        // else; // error -> connection closed
    }

    if (leaveSect) // HaveWorkingPath == TRUE at the same time
    {
        std::string result;
        try
        {
            result = WorkingPath;
        }
        catch (const std::bad_alloc&)
        {
            leaveSect = FALSE;
        }
        catch (const std::length_error&)
        {
            leaveSect = FALSE;
        }
        HANDLES(LeaveCriticalSection(&SocketCritSect));
        if (leaveSect)
            path.swap(result);
    }
    return leaveSect;
}

void CControlConnectionSocket::ResetWorkingPathCache()
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::ResetWorkingPathCache()");
    HANDLES(EnterCriticalSection(&SocketCritSect));
    HaveWorkingPath = FALSE;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
}

void CControlConnectionSocket::ResetCurrentTransferModeCache()
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::ResetCurrentTransferModeCache()");
    HANDLES(EnterCriticalSection(&SocketCritSect));
    CurrentTransferMode = ctrmUnknown;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
}

BOOL CControlConnectionSocket::SendChangeWorkingPath(BOOL notInPanel, BOOL leftPanel, HWND parent,
                                                     const char* path, std::wstring& user,
                                                     BOOL* success, std::string& ftpReply,
                                                     const char* startPath, int* totalAttemptNum,
                                                     const std::string* retryMessage, BOOL skipFirstReconnectIfNeeded,
                                                     BOOL* userRejectsReconnect)
{
    CALL_STACK_MESSAGE7("CControlConnectionSocket::SendChangeWorkingPath(%d, %d, , %s, , , , %s, , %s, %d,)",
                        notInPanel, leftPanel, path, startPath,
                        retryMessage != NULL ? retryMessage->c_str() : NULL, skipFirstReconnectIfNeeded);

    ftpReply.clear();
    if (userRejectsReconnect != NULL)
        *userRejectsReconnect = FALSE;

    BOOL ret = FALSE;
    *success = FALSE;
    std::string cmdBuf;
    std::string logBuf;
    std::string replyBuf;
    std::string newPath;
    BOOL reconnected = FALSE;
    int attemptNum = 1;
    if (totalAttemptNum != NULL)
        attemptNum = *totalAttemptNum;
    const std::string* retryMessageToUse = retryMessage;
    BOOL canRetry = FALSE;
    std::string nextRetryMessage;

    if (skipFirstReconnectIfNeeded && retryMessage != NULL)
    {
        TRACE_E("CControlConnectionSocket::SendChangeWorkingPath(): Invalid value (TRUE) of 'skipFirstReconnectIfNeeded' ('retryMsg' != NULL)!");
        skipFirstReconnectIfNeeded = FALSE;
    }

    BOOL firstRound = TRUE;
    while (skipFirstReconnectIfNeeded ||
           ReconnectIfNeeded(notInPanel, leftPanel, parent, user, &reconnected, FALSE,
                             &attemptNum, retryMessageToUse, firstRound ? userRejectsReconnect : NULL, -1, FALSE))
    {
        firstRound = FALSE;
        skipFirstReconnectIfNeeded = FALSE;
        BOOL run = FALSE;
        int i;
        for (i = 0; i < 2; i++)
        {
            const char* p;

            BOOL needChangeDir = i == 0 && reconnected && startPath != NULL; // after reconnect try to set 'startPath' again
            if (i == 0 && !reconnected && startPath != NULL)                 // we have been connected for a while, check
            {                                                                // whether the working directory matches 'startPath'
                // use the cache; under normal circumstances the path should be there
                if (GetCurrentWorkingPath(parent, newPath, FALSE, &canRetry, &nextRetryMessage))
                {
                    if (newPath != startPath) // the working directory on the server differs - change required
                        needChangeDir = TRUE;            // (assumption: the server always returns the same working path string)
                }
                else
                {
                    if (canRetry) // "retry" is allowed
                    {
                        run = TRUE;
                        retryMessageToUse = &nextRetryMessage;
                    }
                    break; // connection closed, terminate the inner loop
                }
            }

            if (needChangeDir)
            { // restored connection + relative path -> first change to the absolute path we base it on
                p = startPath;
            }
            else
            {
                p = path;
                i = 1; // end of the loop
            }
            if (PrepareFTPCommand(cmdBuf, &logBuf,
                                  ftpcmdChangeWorkingPath, NULL, p))
            {
                int ftpReplyCode;
                if (SendFTPCommand(parent, cmdBuf.c_str(), logBuf.c_str(), NULL, GetWaitTime(WAITWND_COMOPER), NULL,
                                   &ftpReplyCode, &replyBuf, FALSE, TRUE, FALSE, &canRetry,
                                   &nextRetryMessage, NULL))
                {
                    if (p == startPath)
                    {
                        if (FTP_DIGIT_1(ftpReplyCode) != FTP_D1_SUCCESS)
                        {               // failure (the absolute path we base it on does not exist)
                            ret = TRUE; // the change happened (with an error)
                            ftpReply.swap(replyBuf);
                            break; // failure -> finish
                        }
                    }
                    else
                    {
                        ret = TRUE; // the change happened (successfully or with an error)
                        ftpReply.swap(replyBuf);
                        if (FTP_DIGIT_1(ftpReplyCode) == FTP_D1_SUCCESS) // success is returned (should be 250)
                            *success = TRUE;
                    }
                }
                else
                {
                    if (canRetry) // "retry" is allowed
                    {
                        run = TRUE;
                        retryMessageToUse = &nextRetryMessage;
                    }
                    break; // connection closed, terminate the inner loop
                }
            }
            else // unexpected error ("always false") -> close the connection
            {
                TRACE_E("CControlConnectionSocket::SendChangeWorkingPath(): unable to allocate command storage");

                HANDLES(EnterCriticalSection(&SocketCritSect));
                int logUID = LogUID; // log UID of this connection
                HANDLES(LeaveCriticalSection(&SocketCritSect));

                CloseSocket(NULL); // close the socket (if it is open); the system will attempt a "graceful" shutdown (we will not learn the result)
                Logs.SetIsConnected(logUID, IsConnected());
                Logs.RefreshListOfLogsInLogsDlg(); // "connection inactive" notification
                Logs.LogMessage(logUID, LangStr(IDS_LOGMSGFATALERROR2).c_str(), -1, TRUE);

                ReleaseKeepAlive(); // on error release keep-alive (cannot be used without an established connection)

                break;
            }
        }
        if (!run)
            break; // end the loop if there was no connection interruption that needs to be restored
    }
    if (totalAttemptNum != NULL)
        *totalAttemptNum = attemptNum;

    return ret;
}

CFTPServerPathType
CControlConnectionSocket::GetFTPServerPathType(const char* path)
{
    CALL_STACK_MESSAGE2("CControlConnectionSocket::GetFTPServerPathType(%s)", path);

    HANDLES(EnterCriticalSection(&SocketCritSect));
    CFTPServerPathType type = ::GetFTPServerPathType(ServerFirstReply.c_str(), ServerSystem.c_str(), path);
    HANDLES(LeaveCriticalSection(&SocketCritSect));

    return type;
}

BOOL CControlConnectionSocket::IsServerSystem(const char* systemName)
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::IsServerSystem()");

    HANDLES(EnterCriticalSection(&SocketCritSect));
    const std::string_view sysName = FTPGetServerSystem(ServerSystem.c_str());
    HANDLES(LeaveCriticalSection(&SocketCritSect));

    return systemName != NULL && FtpEqualAsciiTokenNoCase(sysName, systemName);
}

BOOL CControlConnectionSocket::ChangeWorkingPath(BOOL notInPanel, BOOL leftPanel, HWND parent, std::string& path,
                                                 std::wstring& user,
                                                 BOOL parsedPath, BOOL forceRefresh, int mode,
                                                 BOOL cutDirectory, std::string* cutFileName, BOOL* pathWasCut,
                                                 std::string& rescuePath, BOOL showChangeInLog,
                                                 std::optional<std::string>& cachedListing,
                                                 CFTPDate* cachedListingDate,
                                                 DWORD* cachedListingStartTime, int* totalAttemptNum,
                                                 BOOL skipFirstReconnectIfNeeded)
{
    CALL_STACK_MESSAGE11("CControlConnectionSocket::ChangeWorkingPath(%d, %d, , %s, , %d, %d, %d, %d, , , %s, %d, , , , , , %d)",
                         notInPanel, leftPanel, path.c_str(), parsedPath,
                         forceRefresh, mode, cutDirectory, rescuePath.c_str(), showChangeInLog,
                         skipFirstReconnectIfNeeded);

    if (cachedListing.has_value())
        FTPSecureWipe(*cachedListing);
    cachedListing.reset();

    // first phase: estimate the path text
    BOOL ret = TRUE;
    CFTPServerPathType pathType = ftpsptEmpty;
    BOOL donotTestPath = FALSE;
    std::string newPath;
    std::string prevUsedPath;
    BOOL fileNameAlreadyCut = FALSE;
    if (ret)
    {
        if (path.empty()) // only right after connecting - otherwise the path change (Shift+F7) to
        {                 // a relative path (e.g. "ftp://localhost") is ignored
                          // + when optimizing ChangePath() called right after obtaining the working directory
            // forceRefresh should be only FALSE + it is always preceded by a GetCurrentWorkingPath() call -> it should always
            // take the path from the cache (does not touch the connection)
            ret = GetCurrentWorkingPath(parent, path, forceRefresh, NULL, NULL);
            if (ret)
            {
                donotTestPath = TRUE; // no point testing the path obtained from the server - list it right away
                pathType = GetFTPServerPathType(path.c_str());
            }
        }
        else
        {
            if (parsedPath &&                                  // except for connecting from the "Connect to FTP Server" dialog it is always TRUE
                (path[0] == '/' || path[0] == '\\')) // 'path' always starts with '/' or '\\' ("always true")
            {
                pathType = GetFTPServerPathType(path.c_str() + 1);
                if (pathType == ftpsptOpenVMS || pathType == ftpsptMVS || pathType == ftpsptIBMz_VM ||
                    pathType == ftpsptOS2 && GetFTPServerPathType("") == ftpsptOS2) // OS/2 paths get confused with the Unix path "/C:/path", so distinguish OS/2 paths even just by the SYST reply
                {                                                                   // VMS + MVS + IBM_z/VM + OS/2 do not have '/' or '\\' at the start of the path
                    path.erase(0, 1);                                               // remove the '/' or '\\' character from the start of the path
                    if (path.empty())                                               // generic root -> supplement it according to the system type
                    {
                        if (pathType == ftpsptOpenVMS)
                            path = "[000000]";
                        else
                        {
                            if (pathType == ftpsptMVS)
                                path = "''";
                            else
                            {
                                if (pathType == ftpsptIBMz_VM)
                                {
                                    if (rescuePath.empty() || !FTPGetIBMz_VMRootPath(path, rescuePath.c_str()))
                                    {
                                        path = "/"; // the tested server supported the Unix root "/"; maybe someone will report it, then we will handle it further...
                                    }
                                }
                                else
                                {
                                    if (pathType == ftpsptOS2)
                                    {
                                        if (rescuePath.empty() || !FTPGetOS2RootPath(path, rescuePath.c_str()))
                                        {
                                            path = "/"; // try at least the Unix root "/", we know nothing else; maybe someone will report it, then we will handle it further...
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        if (pathType == ftpsptOpenVMS && mode == 3 && !fileNameAlreadyCut &&
                            cutFileName != NULL && !cutDirectory)
                        { // try whether it is a file name (with VMS it is distinguished by syntax)
                            prevUsedPath = path;
                            BOOL fileNameCouldBeCut;
                            if (FTPCutDirectory(pathType, prevUsedPath, &newPath, &fileNameCouldBeCut) &&
                                fileNameCouldBeCut) // with VMS, 'fileNameCouldBeCut' == TRUE means it is definitely a file
                            {
                                if (!FtpStoreProtocolBytes(newPath, *cutFileName))
                                    return FALSE;
                                path = prevUsedPath;
                                fileNameAlreadyCut = TRUE;
                                if (pathWasCut != NULL)
                                    *pathWasCut = TRUE;
                            }
                        }
                    }
                }
                else
                    pathType = GetFTPServerPathType(path.c_str());
            }
            else
                pathType = GetFTPServerPathType(path.c_str());
        }
    }

    std::string replyBuf;
    std::wstring errorMessage;
    if (ret && showChangeInLog && // if it should be shown in the log
        !cutDirectory)            // only on the first pass (do not report shortening due to a faulty listing)
    {
        const std::wstring pathText = DecodeServerText(path.c_str(), GetTextCodec());
        const std::wstring logText = SPLFormatStringOwned(
            LangStr(forceRefresh ? IDS_LOGMSGREFRESHINGPATH : IDS_LOGMSGCHANGINGPATH).c_str(),
            pathText.c_str());
        LogMessage(logText.c_str(), -1, TRUE);
    }

    prevUsedPath.clear();
    if (ret && cutDirectory) // still OK + the path should be shortened before use (if the path could not be listed)
    {
        if (donotTestPath)
            donotTestPath = FALSE;                  // path change - we must test the modified one
        prevUsedPath = path; // remember the previous path on the server (returned by the server)
        if (!FTPCutDirectory(pathType, path, NULL, NULL))
        {
            if (!rescuePath.empty()) // try the rescue path as well
            {
                path = rescuePath;
                pathType = GetFTPServerPathType(path.c_str());
                rescuePath.clear(); // do not try it next time (avoid loops)
            }
            else // no need to report any error (listing already reported an error); we quietly tried
            {    // to find an accessible path (which did not work)
                ret = FALSE;
            }
        }
        if (ret) // either shortened or another path, in any case it is not the requested path
        {
            fileNameAlreadyCut = TRUE;
            if (pathWasCut != NULL)
                *pathWasCut = TRUE;
        }
    }

    // second phase: find on the server the requested or the closest matching path that
    //               is either cached or accessible
    if (ret)
    {
        HANDLES(EnterCriticalSection(&SocketCritSect));
        std::wstring hostTmp;
        const BOOL hostReady = FtpStoreWideText(Host.c_str(), hostTmp);
        unsigned short portTmp = Port;
        std::string listCmd;
        const BOOL listCommandReady = FTPFormatString(
            listCmd, "%s\r\n",
            UseLIST_aCommand ? LIST_a_CMD_TEXT : (!ListCommand.empty() ? ListCommand.c_str() : LIST_CMD_TEXT));
        BOOL isFTPS = EncryptControlConnection == 1;
        int useListingsCacheAux = UseListingsCache;
        BOOL resuscitateKeepAlive = (IsConnected() && KeepAliveEnabled && KeepAliveMode == kamNone); // if keep-alive has already turned off (revival time expired), we must restart it
        KeepAliveStart = GetTickCount();                                                             // beware, it is not enough to do it simply; 'resuscitateKeepAlive' must be used
        HANDLES(LeaveCriticalSection(&SocketCritSect));
        if (!hostReady || !listCommandReady)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }

        auto clearCachedListing = [&cachedListing]() noexcept
        {
            if (cachedListing.has_value())
                FTPSecureWipe(*cachedListing);
            cachedListing.reset();
        };
        auto lookupCachedListing = [&]() noexcept
        {
            std::string stagedListing;
            const CListingCacheLookupStatus status = ListingCache.GetPathListing(
                hostTmp.c_str(), portTmp, user.c_str(), pathType, path, listCmd.c_str(), isFTPS,
                stagedListing, cachedListingDate, cachedListingStartTime);
            clearCachedListing();
            if (status == CListingCacheLookupStatus::Found)
                cachedListing.emplace(std::move(stagedListing));
            return status;
        };

        if (donotTestPath)
        {
            if (useListingsCacheAux && !forceRefresh &&
                lookupCachedListing() == CListingCacheLookupStatus::LowMemory)
            {
                ret = FALSE;
            }
        }
        else
        {
            errorMessage.clear();

            int attemptNum = 1;
            if (totalAttemptNum != NULL)
                attemptNum = *totalAttemptNum;
            const std::string* retryMessageToUse = NULL;
            BOOL canRetry = FALSE;
            std::string nextRetryMessage;
            BOOL firstRound = TRUE;

            while (1)
            {
                const CListingCacheLookupStatus cacheStatus = useListingsCacheAux && !forceRefresh
                                                                  ? lookupCachedListing()
                                                                  : CListingCacheLookupStatus::NotFound;
                BOOL inCache = cacheStatus == CListingCacheLookupStatus::Found;
                std::string pathSearchedInCache;
                if (useListingsCacheAux && !forceRefresh)
                    pathSearchedInCache = path;
                else
                    pathSearchedInCache.clear();

                if (cacheStatus == CListingCacheLookupStatus::LowMemory)
                {
                    ret = FALSE;   // the listing is in the cache, but there is not enough memory to allocate it
                    errorMessage.clear(); // any possible message is unnecessary; the printed error would only be confusing
                    break;         // fatal error
                }
                if (!inCache) // the listing is not in the cache (or we must not use it)
                {
                    resuscitateKeepAlive = FALSE; // the connection will be touched, keep-alive will revive automatically

                    BOOL success;
                    BOOL userRejectsReconnect;

                TRY_CHANGE_AGAIN:

                    if (SendChangeWorkingPath(notInPanel, leftPanel, parent, path.c_str(), user,
                                              &success, replyBuf, NULL,
                                              &attemptNum, retryMessageToUse, skipFirstReconnectIfNeeded,
                                              &userRejectsReconnect))
                    {
                        firstRound = FALSE;
                        skipFirstReconnectIfNeeded = FALSE;
                        retryMessageToUse = NULL;
                        if (success) // the server reports success - retrieve the new path
                        {
                            std::string currentWorkingPath;
                            if (GetCurrentWorkingPath(parent, currentWorkingPath, forceRefresh,
                                                      &canRetry, &nextRetryMessage))
                            {
                                if (cutDirectory && currentWorkingPath == prevUsedPath)
                                { // the server is making fools of us (does not change the path but claims it did)
                                    errorMessage = FormatChangeWorkingPathError(path.c_str(), replyBuf, GetTextCodec());
                                }
                                else
                                {
                                    path.swap(currentWorkingPath);
                                    if (useListingsCacheAux && !forceRefresh && // the user wants to use the cache and this is not a hard refresh
                                        path != pathSearchedInCache) // if we have not searched for this path in the cache yet, try it
                                    {
                                        pathType = GetFTPServerPathType(path.c_str());
                                        if (lookupCachedListing() == CListingCacheLookupStatus::LowMemory)
                                        {                  // fatal error
                                            ret = FALSE;   // the listing is in the cache, but there is not enough memory to allocate it
                                            errorMessage.clear(); // any possible message is unnecessary; the printed error would only be confusing
                                        }
                                    }
                                    break; // fatal error or success (path changed) + possibly: the path is cached, take the listing from the cache
                                }
                            }
                            else
                            {
                                if (canRetry) // "retry" is allowed
                                {
                                    retryMessageToUse = &nextRetryMessage;

                                    goto TRY_CHANGE_AGAIN;
                                }

                                ret = FALSE;
                                errorMessage.clear(); // the user has already received the fatal error message; another message is unnecessary
                                break;         // fatal error - the connection is already closed
                            }
                        }
                        else // error, generate a message
                        {
                            errorMessage = FormatChangeWorkingPathError(path.c_str(), replyBuf, GetTextCodec());
                        }

                        if (rescuePath == path)
                            rescuePath.clear(); // path error identical to 'rescuePath' -> 'rescuePath' is no longer meaningful

                        // try to shorten the path (a successful path change would not reach this point)
                        BOOL fileNameCouldBeCut;
                        if (!FTPCutDirectory(pathType, path, &newPath, &fileNameCouldBeCut))
                        {
                            if (!rescuePath.empty()) // try the rescue path as well
                            {
                                path = rescuePath;
                                fileNameAlreadyCut = TRUE;
                                pathType = GetFTPServerPathType(path.c_str());
                                rescuePath.clear(); // do not try it next time (avoid loops)
                            }
                            else
                            {
                                // even if we did not find any accessible path, we do not want a disconnect, therefore do the following:
                                std::string currentWorkingPath;
                                if (GetCurrentWorkingPath(parent, currentWorkingPath, TRUE,
                                                          &canRetry, &nextRetryMessage))
                                {
                                    if (currentWorkingPath.empty()) // we care only about the case when there is no current path on the server (otherwise we should not get here - rescuePath would be used)
                                    {
                                        if (pathWasCut != NULL)
                                            *pathWasCut = TRUE; // we are on a path other than the requested one
                                        path.clear(); // there is no current path on the server
                                        break;           // go try listing...
                                    }
                                }
                                else
                                {
                                    if (canRetry) // "retry" is allowed
                                    {
                                        retryMessageToUse = &nextRetryMessage;

                                        goto TRY_CHANGE_AGAIN;
                                    }

                                    ret = FALSE;
                                    errorMessage.clear(); // the user has already received the fatal error message; another message is unnecessary
                                    break;         // fatal error - the connection is already closed
                                }

                                ret = FALSE; // report the last error (in all types of 'mode')
                                break;       // fatal error (no accessible path exists on the FS) - leave the connection open
                            }
                        }
                        if (fileNameCouldBeCut && !fileNameAlreadyCut && mode == 3) // first shortening -> it may be a file name
                        {
                            errorMessage.clear(); // in 'mode' 3 this is not reported as an error (we are trying to focus on the file)
                            if (cutFileName != NULL)
                            {
                                if (!FtpStoreProtocolBytes(newPath, *cutFileName))
                                    return FALSE;
                            }
                        }
                        else
                        {
                            if (cutFileName != NULL)
                                cutFileName->clear(); // it can no longer be a file name
                        }
                        fileNameAlreadyCut = TRUE;
                        if (pathWasCut != NULL)
                            *pathWasCut = TRUE;
                        if (mode == 1)
                            errorMessage.clear(); // in 'mode' 1 only root errors are reported
                    }
                    else
                    {
                        // if this is a hard refresh and the user refused reconnect and the path has a cached listing,
                        // use the cached listing (the user knows they answered "NO" to reconnect, so they will not expect
                        // a refreshed listing)
                        if (firstRound && userRejectsReconnect && useListingsCacheAux && forceRefresh)
                        {
                            const CListingCacheLookupStatus retryCacheStatus = lookupCachedListing();
                            inCache = retryCacheStatus == CListingCacheLookupStatus::Found;
                            if (retryCacheStatus == CListingCacheLookupStatus::LowMemory)
                            {
                                ret = FALSE;   // the listing is in the cache, but there is not enough memory to allocate it
                                errorMessage.clear(); // any possible message is unnecessary; the printed error would only be confusing
                                break;         // fatal error
                            }
                            if (inCache)
                                break; // the path is cached - do not check whether it still exists, take the listing from the cache
                        }

                        ret = FALSE;
                        errorMessage.clear(); // the user has already received the fatal error message; another message is unnecessary
                        break;         // fatal error - the connection is already closed
                    }
                    skipFirstReconnectIfNeeded = TRUE; // the next SendChangeWorkingPath() call follows the previous successful SendChangeWorkingPath() call
                }
                else
                    break; // the path is cached - do not check whether it still exists, take the listing from the cache
            }
            if (totalAttemptNum != NULL)
                *totalAttemptNum = attemptNum;
            if (!errorMessage.empty()) // if we have an error message, display it here
            {
                SalamanderGeneral->SalMessageBox(parent, errorMessage.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                 MB_OK | MB_ICONEXCLAMATION);
            }
        }
        // it should be revived and the listing is from the cache (otherwise it must be revived on the first sent command)
        if (resuscitateKeepAlive && cachedListing.has_value())
        {
            WaitForEndOfKeepAlive(parent, 0); // the wait window cannot be shown
            SetupKeepAliveTimer(TRUE);
        }
    }

    if (ret && rescuePath == path)
        rescuePath.clear(); // trying a path identical to 'rescuePath' -> 'rescuePath' is no longer meaningful
    return ret;
}

void CControlConnectionSocket::GiveConnectionToWorker(CFTPWorker* newWorker, HWND parent)
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::GiveConnectionToWorker(,)");

    parent = FindPopupParent(parent);

    if (IsConnected()) // only if the connection is open
    {
        // first stop keep-alive:
        // store the focus from 'parent' (if the focus is not from 'parent', store NULL)
        HWND focusedWnd = GetFocus();
        HWND hwnd = focusedWnd;
        while (hwnd != NULL && hwnd != parent)
            hwnd = GetParent(hwnd);
        if (hwnd != parent)
            focusedWnd = NULL;
        // disable the 'parent'; when enabling it restore the focus as well
        EnableWindow(parent, FALSE);

        // set the wait cursor over the parent, unfortunately we do not know another way
        CSetWaitCursorWindow* winParent = new CSetWaitCursorWindow;
        if (winParent != NULL)
            winParent->AttachToWindow(parent);

        // wait for the keep-alive command to finish (if it is currently running) + set
        // keep-alive to 'kamForbidden' (normal commands will run)
        WaitForEndOfKeepAlive(parent, WAITWND_CONTOOPER);

        // remove the wait cursor over the parent
        if (winParent != NULL)
        {
            winParent->DetachWindow();
            delete winParent;
        }

        // enable the 'parent'
        EnableWindow(parent, TRUE);
        // if the 'parent' is active, restore the focus as well
        if (GetForegroundWindow() == parent)
        {
            if (parent == SalamanderGeneral->GetMainWindowHWND())
                SalamanderGeneral->RestoreFocusInSourcePanel();
            else
            {
                if (focusedWnd != NULL)
                    SetFocus(focusedWnd);
            }
        }

        // after stopping keep-alive we can hand over the active "control connection" to the worker
        // (the timer and post-socket message associated with keep-alive have been cleared/delivered)
        if (IsConnected()) // only if the connection is open even after the keep-alive command finishes
        {
            // swap sockets and the object's internal data related to the socket (read/write buffers, etc.)
            SocketsThread->BeginSocketsSwap(this, newWorker);
            // This check is rather complicated for sanity purposes: pCertificate is always NULL
            if (pCertificate)
                pCertificate->Release();
            pCertificate = newWorker->GetCertificate(); // Keep the certificate
            newWorker->RefreshCopiesOfUIDAndMsg();      // refresh copies of UID+Msg (they changed)
            BOOL ok = newWorker->IsConnected();
            if (ok) // paranoid check: the connection might still drop between IsConnected() and SocketsThread->BeginSocketsSwap(), swapping would make no sense then
            {
                HANDLES(EnterCriticalSection(&newWorker->WorkerCritSect));
                newWorker->SocketClosed = FALSE;      // the socket is no longer closed; we are taking over the socket from the panel
                newWorker->ConnectAttemptNumber = 1;  // the connection is established, so this must be one
                int workerLogUID = newWorker->LogUID; // log UID of this worker
                newWorker->ErrorDescr.clear();        // start collecting error messages
                HANDLES(LeaveCriticalSection(&newWorker->WorkerCritSect));

                HANDLES(EnterCriticalSection(&SocketCritSect));
                HANDLES(EnterCriticalSection(&newWorker->SocketCritSect));

                // pass the worker information about the connection and the socket data
                newWorker->ControlConnectionUID = UID;
                if (HaveWorkingPath)
                {
                    newWorker->HaveWorkingPath = TRUE;
                    newWorker->WorkingPath.swap(WorkingPath);
                    HaveWorkingPath = FALSE;
                }
                newWorker->CurrentTransferMode = CurrentTransferMode;
                newWorker->TextPolicy = TextPolicy;
                newWorker->ResetBuffersAndEvents();
                newWorker->EventConnectSent = EventConnectSent;
                newWorker->ReadBytes = ReadBytes;
                newWorker->ReadBytesCount = ReadBytesCount;
                newWorker->ReadBytesOffset = ReadBytesOffset;
                newWorker->ReadBytesAllocatedSize = ReadBytesAllocatedSize;
                ReadBytes = NULL;
                ReadBytesCount = 0;
                ReadBytesOffset = 0;
                ReadBytesAllocatedSize = 0;
                int logUID = LogUID; // log UID of this connection

                HANDLES(LeaveCriticalSection(&newWorker->SocketCritSect));
                HANDLES(LeaveCriticalSection(&SocketCritSect));

                ResetBuffersAndEvents(); // clear the event queue (it should contain only ccsevNewBytesRead)

                // inform the log that the "control connection" has been handed to the worker (and is thus "inactive")
                Logs.LogMessage(logUID, LangStr(IDS_LOGMSGCONINWORKER).c_str(), -1, TRUE);
                Logs.SetIsConnected(logUID, IsConnected());
                Logs.LogMessage(workerLogUID, LangStr(IDS_LOGMSGWORKERUSECON).c_str(), -1, TRUE);
                Logs.SetIsConnected(workerLogUID, newWorker->IsConnected());
                Logs.RefreshListOfLogsInLogsDlg();
            }
            else // if swapping makes no sense, restore the objects to their original state
            {
                SocketsThread->BeginSocketsSwap(this, newWorker);
                newWorker->RefreshCopiesOfUIDAndMsg(); // refresh copies of UID+Msg (they changed)
                SocketsThread->EndSocketsSwap();
            }
            SocketsThread->EndSocketsSwap();
        }

        // release keep-alive, it is no longer needed (the connection is no longer established)
        ReleaseKeepAlive();
    }
}

void CControlConnectionSocket::GetConnectionFromWorker(CFTPWorker* workerWithCon)
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::GetConnectionFromWorker()");

    if (!IsConnected() && workerWithCon->IsConnected()) // only if the connection is not open and the worker has an open connection
    {
        // swap sockets and the object's internal data related to the socket (read/write buffers, etc.)
        SocketsThread->BeginSocketsSwap(this, workerWithCon);
        workerWithCon->RefreshCopiesOfUIDAndMsg(); // refresh copies of UID+Msg (they changed)
        BOOL ok = IsConnected();
        if (ok) // paranoid check: the connection might still drop between workerWithCon->IsConnected() and SocketsThread->BeginSocketsSwap(); swapping would make no sense then
        {
            HANDLES(EnterCriticalSection(&workerWithCon->WorkerCritSect));
            workerWithCon->SocketClosed = TRUE;       // the socket is no longer open; we are taking over the closed socket from the panel
            int workerLogUID = workerWithCon->LogUID; // log UID of this worker
            workerWithCon->ErrorDescr.clear();        // handing over the connection is not an error
            HANDLES(LeaveCriticalSection(&workerWithCon->WorkerCritSect));

            HANDLES(EnterCriticalSection(&SocketCritSect));
            HANDLES(EnterCriticalSection(&workerWithCon->SocketCritSect));

            // take over from the worker the information about the connection and the socket data
            workerWithCon->ControlConnectionUID = -1;
            if (workerWithCon->HaveWorkingPath)
            {
                HaveWorkingPath = TRUE;
                WorkingPath.swap(workerWithCon->WorkingPath);
                workerWithCon->HaveWorkingPath = FALSE;
            }
            CurrentTransferMode = workerWithCon->CurrentTransferMode;
            TextPolicy = workerWithCon->TextPolicy;
            ResetBuffersAndEvents();
            EventConnectSent = workerWithCon->EventConnectSent;
            if (ReadBytes != NULL)
                free(ReadBytes);
            ReadBytes = workerWithCon->ReadBytes;
            ReadBytesCount = workerWithCon->ReadBytesCount;
            ReadBytesOffset = workerWithCon->ReadBytesOffset;
            ReadBytesAllocatedSize = workerWithCon->ReadBytesAllocatedSize;
            workerWithCon->EventConnectSent = FALSE;
            workerWithCon->ReadBytes = NULL;
            workerWithCon->ReadBytesCount = 0;
            workerWithCon->ReadBytesOffset = 0;
            workerWithCon->ReadBytesAllocatedSize = 0;
            ConnectionLostMsg.clear();
            int logUID = LogUID; // log UID of this connection

            HANDLES(LeaveCriticalSection(&workerWithCon->SocketCritSect));
            HANDLES(LeaveCriticalSection(&SocketCritSect));

            workerWithCon->ResetBuffersAndEvents(); // clear the event queue (it should contain only ccsevNewBytesRead)

            // inform the log that the "control connection" has been taken from the worker (and is therefore "active" again)
            Logs.LogMessage(logUID, LangStr(IDS_LOGMSGCONFROMWORKER).c_str(), -1, TRUE);
            Logs.SetIsConnected(logUID, IsConnected());
            Logs.LogMessage(workerLogUID, LangStr(IDS_LOGMSGWORKERRETCON).c_str(), -1, TRUE);
            Logs.SetIsConnected(workerLogUID, workerWithCon->IsConnected());
            Logs.RefreshListOfLogsInLogsDlg(); // "connection active" notification
        }
        else // if swapping makes no sense, restore the objects to their original state
        {
            SocketsThread->BeginSocketsSwap(this, workerWithCon);
            workerWithCon->RefreshCopiesOfUIDAndMsg(); // refresh copies of UID+Msg (they changed)
            SocketsThread->EndSocketsSwap();
        }
        SocketsThread->EndSocketsSwap();

        if (ok)
        {
            // restart keep-alive
            ReleaseKeepAlive();
            WaitForEndOfKeepAlive(SalamanderGeneral->GetMsgBoxParent(), 0); // cannot open the wait window (it is in state 'kamNone')
            SetupKeepAliveTimer(TRUE);                                      // set up the keep-alive timer; trigger the keep-alive command right away (we do not know how long the connection was inactive, so let us avoid losing it)
        }
    }
}

BOOL CControlConnectionSocket::GetUseListingsCache()
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::GetConnectionFromWorker()");

    HANDLES(EnterCriticalSection(&SocketCritSect));
    BOOL ret = UseListingsCache;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}
