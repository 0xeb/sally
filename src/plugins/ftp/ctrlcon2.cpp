// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "ftp_text_codec.h"

const char* LogsSeparator = "\r\n=================\r\n\r\n";
const wchar_t* LogsSeparatorW = L"\r\n=================\r\n\r\n";

// ****************************************************************************

char* CopyStr(char* buf, int bufSize, const char* txt, int size)
{
    if (bufSize <= 0)
        return NULL;
    if (size >= bufSize)
        size = bufSize - 1;
    memcpy(buf, txt, size);
    buf[size] = 0;

    // If necessary, convert LF to CRLF
    int insBytes = 0;
    char* s = buf;
    while (*s != 0)
    {
        if (*s == '\n' && (s == buf || *(s - 1) != '\r'))
        {
            if (++size >= bufSize && --size == (s - buf) + 1) // small buffer
            {
                *s = 0; // trim the last LF (CRLF would not fit)
                break;
            }
            memmove(s + 1, s, size - (s - buf) - 1);
            *s++ = '\r';
            buf[size] = 0;
        }
        s++;
    }

    return buf;
}

BOOL CopyStr(std::string& output, const char* text, int size) noexcept
{
    if (text == NULL || size < 0)
    {
        output.clear();
        return FALSE;
    }
    if (!FtpStoreReplyTextBytes(std::string_view(text, static_cast<size_t>(size)), output))
    {
        output.clear();
        return FALSE;
    }
    return TRUE;
}

// ****************************************************************************

//
// ****************************************************************************
// CDynString
//

BOOL CDynString::Append(const char* str, int len)
{
    if (len == -1)
        len = (int)strlen(str);
    if (Length + len >= Allocated)
    {
        int size = Length + len + 1 + 256; // add 256 characters in reserve so we don't reallocate so often
        char* newBuf = (char*)realloc(Buffer, size);
        if (newBuf != NULL)
        {
            Buffer = newBuf;
            Allocated = size;
        }
        else // insufficient memory, tough luck...
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
    }
    memmove(Buffer + Length, str, len);
    Length += len;
    Buffer[Length] = 0;
    return TRUE;
}

void CDynString::SkipBeginning(DWORD len, int* skippedChars, int* skippedLines)
{
    if (Length > 0)
    {
        if (len > (DWORD)Length)
            len = Length;
        char* skipEnd = Buffer + len;
        char* end = Buffer + Length;
        int lines = 0;
        char* s = Buffer;
        while (s < end)
        {
            if (*s == '\r' && s + 1 < end && *(s + 1) == '\n')
            {
                lines++;
                s += 2;
                if (s >= skipEnd)
                    break;
            }
            else
            {
                if (*s == '\n')
                {
                    lines++;
                    if (++s >= skipEnd)
                        break;
                }
                else
                    s++;
            }
        }
        *skippedLines += lines;
        *skippedChars += (int)(s - Buffer);
        Length -= (int)(s - Buffer);
        memmove(Buffer, s, Length);
        Buffer[Length] = 0;
    }
}

BOOL CDynStringW::Append(const wchar_t* str, int len)
{
    if (len == -1)
        len = (int)wcslen(str);
    if (Length + len >= Allocated)
    {
        int size = Length + len + 1 + 256;
        wchar_t* newBuf = (wchar_t*)realloc(Buffer, size * sizeof(wchar_t));
        if (newBuf == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        Buffer = newBuf;
        Allocated = size;
    }
    memmove(Buffer + Length, str, len * sizeof(wchar_t));
    Length += len;
    Buffer[Length] = 0;
    return TRUE;
}

void CDynStringW::TrimToUtf8Size(DWORD maxBytes, int* skippedChars, int* skippedLines)
{
    size_t lines = 0;
    const size_t trim = FtpFindLogTrimPrefix(Buffer, Length, maxBytes, &lines);
    if (trim == 0)
        return;
    *skippedLines += (int)lines;
    *skippedChars += (int)trim;
    Length -= (int)trim;
    memmove(Buffer, Buffer + trim, Length * sizeof(wchar_t));
    Buffer[Length] = 0;
}

//
// ****************************************************************************
// CControlConnectionSocket
//

void CControlConnectionSocket::CloseControlConnection(HWND parent)
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::CloseControlConnection()");

    parent = FindPopupParent(parent);
    const DWORD showWaitWndTime = WAITWND_CLOSECON; // time to display the wait window
    int serverTimeout = Config.GetServerRepliesTimeout() * 1000;
    if (serverTimeout < 1000)
        serverTimeout = 1000; // at least a second
    SetStartTime();

    // Remember the focus from 'parent' (if the focus is not from 'parent', store NULL)
    HWND focusedWnd = GetFocus();
    HWND hwnd = focusedWnd;
    while (hwnd != NULL && hwnd != parent)
        hwnd = GetParent(hwnd);
    if (hwnd != parent)
        focusedWnd = NULL;
    // Disable 'parent'; when enabling again we'll also restore the focus
    EnableWindow(parent, FALSE);

    // Show the wait cursor over the parent; unfortunately we cannot do it differently
    CSetWaitCursorWindow* winParent = new CSetWaitCursorWindow;
    if (winParent != NULL)
        winParent->AttachToWindow(parent);

    // Wait for any active keep-alive command to finish and switch the keep-alive mode
    // to 'kamForbidden', because a regular command is currently in progress
    WaitForEndOfKeepAlive(parent, GetWaitTime(showWaitWndTime));

    CWaitWindow waitWnd(parent, TRUE);

    HANDLES(EnterCriticalSection(&SocketCritSect));
    int logUID = LogUID;
    HANDLES(LeaveCriticalSection(&SocketCritSect));

    Logs.LogMessage(logUID, LangStr(IDS_LOGMSGDISCONNECT).c_str(), -1, TRUE);

    BOOL socketClosed = FALSE;
    int cmdLen;
    std::string command;
    std::string logCommand;
    std::wstring hostForDisplay;
    if (PrepareFTPCommand(command, &logCommand, ftpcmdQuit, &cmdLen))
    {
        DWORD error;
        BOOL allBytesWritten;
        if (Write(command.c_str(), cmdLen, &error, &allBytesWritten))
        {
            // Compose the wait-window message text
            HANDLES(EnterCriticalSection(&SocketCritSect));
            Logs.LogMessage(logUID, logCommand.c_str(), -1);
            const BOOL hostReady = FtpStoreWideText(Host.c_str(), hostForDisplay);
            HANDLES(LeaveCriticalSection(&SocketCritSect));

            try
            {
                if (!hostReady)
                    throw std::bad_alloc();
                if (hostForDisplay.size() > 22)
                {
                    hostForDisplay.resize(20);
                    hostForDisplay.append(L"...");
                }
                const std::wstring waitText = SPLFormatStringOwned(
                    LangStr(IDS_CLOSINGCONNECTION).c_str(), hostForDisplay.c_str());
                waitWnd.SetText(waitText.c_str());
            }
            catch (...)
            {
                waitWnd.SetText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str());
            }
            waitWnd.Create(GetWaitTime(showWaitWndTime));

            DWORD start = GetTickCount();
            BOOL shutdownCalled = FALSE;
            BOOL run = TRUE;
            while (run)
            {
                // Wait for either a socket event (server reply) or ESC
                CControlConnectionSocketEvent event;
                DWORD data1, data2;
                DWORD now = GetTickCount();
                if (now - start > (DWORD)serverTimeout)
                    now = start + (DWORD)serverTimeout;
                WaitForEventOrESC(parent, &event, &data1, &data2, serverTimeout - (now - start),
                                  NULL, NULL, FALSE);
                switch (event)
                {
                case ccsevESC:
                {
                    waitWnd.Show(FALSE);
                    if (SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CLOSECONESC).c_str(),
                                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                         MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                                             MB_ICONQUESTION) == IDYES)
                    { // the user wants to force the connection to terminate
                        run = FALSE;
                        Logs.LogMessage(logUID, LangStr(IDS_LOGMSGACTIONCANCELED).c_str(), -1, TRUE); // record that ESC (Cancel) was pressed
                    }
                    else
                    {
                        SalamanderGeneral->WaitForESCRelease(); // ensure the next action is not interrupted by the ESC from the previous message box
                        waitWnd.Show(TRUE);
                    }
                    break;
                }

                case ccsevTimeout:
                {
                    Logs.LogMessage(logUID, LangStr(IDS_LOGMSGDISCONTIMEOUT).c_str(), -1, TRUE); // record the disconnection timeout in the log
                    run = FALSE;
                    break;
                }

                case ccsevWriteDone:    // all bytes have been sent (handles the fact that ccsevWriteDone could overwrite ccsevNewBytesRead)
                case ccsevClosed:       // possibly an unexpected connection loss (also handles ccsevClosed overwriting ccsevNewBytesRead)
                case ccsevNewBytesRead: // new bytes were read
                {
                    char* reply;
                    int replySize;

                    HANDLES(EnterCriticalSection(&SocketCritSect));
                    while (ReadFTPReply(&reply, &replySize)) // process responses from the server as long as we have any
                    {
                        Logs.LogServerMessage(logUID, reply, replySize, TextPolicy);

                        if (!shutdownCalled) // a reply arrived (success or error), so shut down the socket
                        {
                            shutdownCalled = TRUE;
                            run = Shutdown(NULL); // continue only if shutdown started successfully
                        }
                        SkipFTPReply(replySize);
                    }
                    HANDLES(LeaveCriticalSection(&SocketCritSect));

                    if (event == ccsevClosed)
                    {
                        run = FALSE;
                        socketClosed = TRUE; // the connection was closed, we are done
                    }
                    break;
                }
                }
            }
            waitWnd.Destroy();
        }
    }

    // Remove the wait cursor from the parent
    if (winParent != NULL)
    {
        winParent->DetachWindow();
        delete winParent;
    }

    // Re-enable 'parent'
    EnableWindow(parent, TRUE);
    // If 'parent' is active, restore the focus
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

    if (!socketClosed)
    {
        CloseSocket(NULL); // close the socket (if it is open); the system will attempt a "graceful" shutdown (we won't learn the result)
        Logs.SetIsConnected(logUID, IsConnected());
        Logs.RefreshListOfLogsInLogsDlg(); // show the "connection inactive" notification
    }

    // Release the keep-alive; it is no longer needed because the connection is gone
    ReleaseKeepAlive();
}

void CControlConnectionSocket::CheckCtrlConClose(BOOL notInPanel, BOOL leftPanel, HWND parent, BOOL quiet)
{
    CALL_STACK_MESSAGE4("CControlConnectionSocket::CheckCtrlConClose(%d, %d, , %d)",
                        notInPanel, leftPanel, quiet);

    // The socket is definitely closed and nothing is actively using it (no event monitoring, etc.)

    HANDLES(EnterCriticalSection(&EventCritSect));
    DWORD error = NO_ERROR;
    BOOL found = FALSE;
    int i;
    for (i = 0; i < EventsUsedCount; i++) // check whether the ccsevClosed event is present
    {
        if (Events[i]->Event == ccsevClosed)
        {
            error = Events[i]->Data1;
            found = TRUE;
            break;
        }
    }
    int logUID = LogUID;
    std::string auxConnectionLostMsg = ConnectionLostMsg;
    HANDLES(LeaveCriticalSection(&EventCritSect));

    if (found ||                           // the ccsevClosed event is waiting for us�the user still does not know that the connection is closed
        !auxConnectionLostMsg.empty()) // we cached the message captured when the connection closed (at that moment it only went into the log)
    {
        std::string errorText;

        if (found)
        {
            char* reply;
            int replySize;
            int replyCode;
            BOOL haveErr = FALSE;

            HANDLES(EnterCriticalSection(&SocketCritSect));
            while (ReadFTPReply(&reply, &replySize, &replyCode)) // process responses from the server as long as we have any
            {
                Logs.LogServerMessage(logUID, reply, replySize, TextPolicy, TRUE);

                if (replyCode != -1 &&
                    (FTP_DIGIT_1(replyCode) == FTP_D1_TRANSIENTERROR ||
                     FTP_DIGIT_1(replyCode) == FTP_D1_ERROR))
                {
                    CopyStr(errorText, reply, replySize); // error message (take the last one in the sequence)
                    haveErr = TRUE;
                }
                if (!haveErr)
                    CopyStr(errorText, reply, replySize); // if there is no error yet, take every message (the last one in the sequence)
                SkipFTPReply(replySize);
            }
            HANDLES(LeaveCriticalSection(&SocketCritSect));

            if (errorText.empty())
            {
                if (!auxConnectionLostMsg.empty()) // we cached the message captured when the connection closed (at that moment it only went into the log)
                {                                  // show it again in a message box
                    HANDLES(EnterCriticalSection(&SocketCritSect));
                    if (!ConnectionLostMsg.empty())
                        FtpStoreProtocolBytes(ConnectionLostMsg, errorText);
                    else
                        errorText.clear();
                    ConnectionLostMsg.clear(); // it is no longer needed
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                }
                else
                {
                    if (error == NO_ERROR)
                        FtpStoreProtocolBytes(LoadStr(IDS_NONEREPLY), errorText);
                    else
                        FTPGetErrorText(error, errorText);
                }
            }
            if (error != NO_ERROR)
            {
                std::string logText;
                if (FTPGetErrorTextForLog(error, logText))
                    Logs.LogMessage(logUID, logText.c_str(), -1, TRUE);
            }
        }
        else // we cached the message captured when the connection closed (at that moment it only went into the log)
        {    // show it again in a message box
            HANDLES(EnterCriticalSection(&SocketCritSect));
            if (!ConnectionLostMsg.empty())
                FtpStoreProtocolBytes(ConnectionLostMsg, errorText);
            else
                errorText.clear();
            ConnectionLostMsg.clear(); // it is no longer needed
            HANDLES(LeaveCriticalSection(&SocketCritSect));
        }

        if (quiet)
        {
            HANDLES(EnterCriticalSection(&SocketCritSect));
            FtpStoreProtocolBytes(errorText, ConnectionLostMsg);
            HANDLES(LeaveCriticalSection(&SocketCritSect));
        }
        else
        {
            if (Config.WarnWhenConLost)
            {
                BOOL actWelcomeMsg = (OurWelcomeMsgDlg != NULL && GetForegroundWindow() == OurWelcomeMsgDlg);
                std::wstring errorTextW;
                if (!FtpDecodeLocalText(errorText, errorTextW))
                    errorTextW = L"<invalid local error text>";
                const std::wstring connectionLostText = SPLFormatStringOwned(
                    LangStr(notInPanel ? IDS_DCONLOSTFORMATERROR : (leftPanel ? IDS_LCONLOSTFORMATERROR : IDS_RCONLOSTFORMATERROR)).c_str(),
                    errorTextW.c_str());
                MSGBOXEX_PARAMS params;
                const std::wstring caption = LangStr(IDS_FTPPLUGINTITLE);
                const std::wstring checkBoxText = LangStr(IDS_WARNWHENCONLOST);
                memset(&params, 0, sizeof(params));
                params.HParent = parent;
                params.Flags = MSGBOXEX_OK | MSGBOXEX_ICONINFORMATION | MSGBOXEX_HINT;
                params.Caption = caption.c_str();
                params.Text = connectionLostText.c_str();
                params.CheckBoxText = checkBoxText.c_str();
                int doNotWarnWhenConLost = !Config.WarnWhenConLost;
                params.CheckBoxValue = &doNotWarnWhenConLost;
                SalamanderGeneral->SalMessageBoxEx(&params);
                Config.WarnWhenConLost = !doNotWarnWhenConLost;
                if (actWelcomeMsg)
                    ActivateWelcomeMsg();
            }
        }
        // Tear down the remaining data (intentionally after closing the box so every event arrives); it is no longer needed
        ResetBuffersAndEvents();
    }
}

int CControlConnectionSocket::GetLogUID()
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::GetLogUID()");
    HANDLES(EnterCriticalSection(&SocketCritSect));
    int logUID = LogUID;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return logUID;
}

BOOL CControlConnectionSocket::LogMessage(const char* str, int len, BOOL addTimeToLog)
{
    CALL_STACK_MESSAGE4("CControlConnectionSocket::LogMessage(%s, %d, %d)", str, len, addTimeToLog);
    HANDLES(EnterCriticalSection(&SocketCritSect));
    int logUID = LogUID;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return Logs.LogMessage(logUID, str, len, addTimeToLog);
}

BOOL CControlConnectionSocket::LogMessage(const wchar_t* str, int len, BOOL addTimeToLog)
{
    CALL_STACK_MESSAGE4("CControlConnectionSocket::LogMessage(%ls, %d, %d)", str, len, addTimeToLog);
    HANDLES(EnterCriticalSection(&SocketCritSect));
    int logUID = LogUID;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return Logs.LogMessage(logUID, str, len, addTimeToLog);
}

BOOL CControlConnectionSocket::ReadFTPReply(char** reply, int* replySize, int* replyCode)
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::ReadFTPReply(, ,)");

#ifdef _DEBUG
    if (SocketCritSect.RecursionCount == 0 /* does not catch the situation when
      the section is used by another thread */
    )
        TRACE_E("Incorrect call to CControlConnectionSocket::ReadFTPReply: not from section SocketCritSect!");
#endif

    return FTPReadFTPReply(ReadBytes, ReadBytesCount, ReadBytesOffset, reply, replySize, replyCode);
}

void CControlConnectionSocket::SkipFTPReply(int replySize)
{
    CALL_STACK_MESSAGE2("CControlConnectionSocket::SkipFTPReply(%d)", replySize);

#ifdef _DEBUG
    if (SocketCritSect.RecursionCount == 0 /* does not catch the situation when
      the section is used by another thread */
    )
        TRACE_E("Incorrect call to CControlConnectionSocket::SkipFTPReply: not from section SocketCritSect!");
#endif

    ReadBytesOffset += replySize;
    if (ReadBytesOffset >= ReadBytesCount) // everything has been read already - reset the buffer
    {
        if (ReadBytesOffset > ReadBytesCount)
            TRACE_E("Error in call to CControlConnectionSocket::SkipFTPReply(): trying to skip more bytes than is read");
        ReadBytesOffset = 0;
        ReadBytesCount = 0;
    }
}

BOOL CControlConnectionSocket::Write(const char* buffer, int bytesToWrite, DWORD* error, BOOL* allBytesWritten)
{
    CALL_STACK_MESSAGE2("CControlConnectionSocket::Write(, %d, ,)", bytesToWrite);
    if (bytesToWrite == -1)
        bytesToWrite = (int)strlen(buffer);
    if (error != NULL)
        *error = NO_ERROR;
    if (allBytesWritten != NULL)
        *allBytesWritten = FALSE;

    if (bytesToWrite == 0) // writing an empty buffer
    {
        if (allBytesWritten != NULL)
            *allBytesWritten = TRUE;
        return TRUE;
    }

    HANDLES(EnterCriticalSection(&SocketCritSect));

    BOOL ret = FALSE;
    if (Socket != INVALID_SOCKET) // the socket is connected
    {
        if (BytesToWriteCount == BytesToWriteOffset) // nothing is waiting to be sent, we can transmit
        {
            if (BytesToWriteCount != 0)
                TRACE_E("Unexpected value of BytesToWriteCount.");

            int len = 0;
            if (!SSLConn)
                while (1) // loop needed because 'send' does not post FD_WRITE when 'sentLen' < 'bytesToWrite'
                {
                    // WARNING: if the TELNET protocol is ever introduced again, sending IAC+IP before aborting
                    // a command in the SendFTPCommand() method must be adjusted

                    int sentLen = send(Socket, buffer + len, bytesToWrite - len, 0);
                    if (sentLen != SOCKET_ERROR) // at least something was sent successfully (or rather taken over by Windows; delivery is uncertain)
                    {
                        len += sentLen;
                        if (len >= bytesToWrite) // has everything been sent?
                        {
                            ret = TRUE;
                            break; // stop sending (there is nothing left)
                        }
                    }
                    else
                    {
                        DWORD err = WSAGetLastError();
                        if (err == WSAEWOULDBLOCK) // nothing else can be sent (Windows no longer have buffer space)
                        {
                            ret = TRUE;
                            break; // stop sending (the rest will be done after FD_WRITE)
                        }
                        else // send error
                        {
                            if (error != NULL)
                                *error = err;
                            break; // return the error
                        }
                    }
                }
            else
                while (1) // loop needed because 'send' does not post FD_WRITE when 'sentLen' < 'bytesToWrite'
                {
                    // WARNING: if the TELNET protocol is ever introduced again, sending IAC+IP before aborting
                    // a command in the SendFTPCommand() method must be adjusted

                    int sentLen = SSLLib.SSL_write(SSLConn, buffer + len, bytesToWrite - len);
                    if (sentLen >= 0) // at least something was sent successfully (or rather taken over by Windows; delivery is uncertain)
                    {
                        len += sentLen;
                        if (len >= bytesToWrite) // has everything been sent?
                        {
                            ret = TRUE;
                            break; // stop sending (there is nothing left)
                        }
                    }
                    else
                    {
                        DWORD err = SSLtoWS2Error(SSLLib.SSL_get_error(SSLConn, sentLen));
                        if (err == WSAEWOULDBLOCK) // nothing else can be sent (Windows no longer have buffer space)
                        {
                            ret = TRUE;
                            break; // stop sending (the rest will be done after FD_WRITE)
                        }
                        else // send error
                        {
                            if (error != NULL)
                                *error = err;
                            break; // return the error
                        }
                    }
                }

            if (ret) // successful send, 'len' holds the number of bytes sent (the rest is sent after FD_WRITE is received)
            {
                if (allBytesWritten != NULL)
                    *allBytesWritten = (len >= bytesToWrite);
                if (len < bytesToWrite) // store the rest in the 'BytesToWrite' buffer
                {
                    const char* buf = buffer + len;
                    int size = bytesToWrite - len;

                    if (BytesToWriteAllocatedSize - BytesToWriteCount < size) // not enough space in the 'BytesToWrite' buffer
                    {
                        int newSize = BytesToWriteCount + size + CRTLCON_BYTESTOWRITEONSOCKETPREALLOC;
                        char* newBuf = (char*)realloc(BytesToWrite, newSize);
                        if (newBuf != NULL)
                        {
                            BytesToWrite = newBuf;
                            BytesToWriteAllocatedSize = newSize;
                        }
                        else // not enough memory to store data in our buffer (only TRACE reports the error)
                        {
                            TRACE_E(LOW_MEMORY);
                            ret = FALSE;
                        }
                    }

                    if (ret) // we can write (the buffer has enough space)
                    {
                        memcpy(BytesToWrite + BytesToWriteCount, buf, size);
                        BytesToWriteCount += size;
                    }
                }
            }
        }
        else // not everything has been sent yet � incorrect use of Write
        {
            TRACE_E("Incorrect use of CControlConnectionSocket::Write(): called again before waiting for ccsevWriteDone event.");
        }
    }
    else
        TRACE_I("CControlConnectionSocket::Write(): Socket is already closed.");

    HANDLES(LeaveCriticalSection(&SocketCritSect));

    return ret;
}

void CControlConnectionSocket::ResetBuffersAndEvents()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));

    EventConnectSent = FALSE;

    BytesToWriteCount = 0;
    BytesToWriteOffset = 0;

    ReadBytesCount = 0;
    ReadBytesOffset = 0;

    HANDLES(EnterCriticalSection(&EventCritSect));
    EventsUsedCount = 0;
    RewritableEvent = FALSE;
    ResetEvent(NewEvent);
    HANDLES(LeaveCriticalSection(&EventCritSect));

    HANDLES(LeaveCriticalSection(&SocketCritSect));
}

void CControlConnectionSocket::ReceiveHostByAddress(DWORD ip, int hostUID, int err)
{
    CALL_STACK_MESSAGE4("CControlConnectionSocket::ReceiveHostByAddress(0x%X, %d, %d)", ip, hostUID, err);

    AddEvent(ccsevIPReceived, ip, err);
}

BOOL CControlConnectionSocket::SendKeepAliveCmd(int logUID, const char* ftpCmd)
{
    DWORD error;
    BOOL allBytesWritten;
    if (Write(ftpCmd, -1, &error, &allBytesWritten))
    {
        Logs.LogMessage(logUID, ftpCmd, -1);
        if (!allBytesWritten) // rather unlikely, but still handled
        {
            HANDLES(EnterCriticalSection(&SocketCritSect));
            KeepAliveCmdAllBytesWritten = FALSE;
            HANDLES(LeaveCriticalSection(&SocketCritSect));
        }
        return TRUE;
    }
    else // Write error (low memory, disconnected, non-blocking "send" failure)
    {
        // Add the error to the log; ClosedCtrlConChecker will alert the user about the lost connection
        std::string errorText;
        if (!FTPGetErrorTextForLog(error, errorText))
            FTPFormatString(errorText, "%s\r\n", LoadStr(IDS_UNKNOWNERROR));
        Logs.LogMessage(logUID, errorText.c_str(), -1, TRUE);

        ReleaseKeepAlive(); // nothing was sent (connection error, no point in continuing keep-alive), cancel keep-alive
        return FALSE;
    }
}

void CControlConnectionSocket::ReceiveNetEvent(LPARAM lParam, int index)
{
    CALL_STACK_MESSAGE3("CControlConnectionSocket::ReceiveNetEvent(0x%IX, %d)", lParam, index);
    DWORD eventError = WSAGETSELECTERROR(lParam); // extract error code of event
    switch (WSAGETSELECTEVENT(lParam))            // extract event
    {
    case FD_CLOSE: // sometimes arrives before the final FD_READ; we must first try FD_READ and, if it succeeds, post FD_CLOSE again (another FD_READ may succeed before it)
    case FD_READ:
    {
        BOOL sendFDCloseAgain = FALSE; // TRUE = FD_CLOSE arrived and there was data to read (handled as FD_READ), so post FD_CLOSE again (the current FD_CLOSE was a false alarm)
        HANDLES(EnterCriticalSection(&SocketCritSect));

        if (!EventConnectSent) // if FD_READ arrived before FD_CONNECT, send ccsevConnected before reading
        {
            EventConnectSent = TRUE;
            AddEvent(ccsevConnected, eventError, 0); // post an event with the result of the connection attempt
        }

        BOOL ret = FALSE;
        DWORD err = NO_ERROR;
        BOOL genEvent = FALSE;
        if (eventError == NO_ERROR)
        {
            if (Socket != INVALID_SOCKET) // the socket is connected
            {
                BOOL lowMem = FALSE;
                if (ReadBytesAllocatedSize - ReadBytesCount < CRTLCON_BYTESTOREADONSOCKET) // the 'ReadBytes' buffer is small
                {
                    if (ReadBytesOffset > 0) // is it possible to move data within the buffer?
                    {
                        memmove(ReadBytes, ReadBytes + ReadBytesOffset, ReadBytesCount - ReadBytesOffset);
                        ReadBytesCount -= ReadBytesOffset;
                        ReadBytesOffset = 0;
                    }

                    if (ReadBytesAllocatedSize - ReadBytesCount < CRTLCON_BYTESTOREADONSOCKET) // the 'ReadBytes' buffer is still small
                    {
                        int newSize = ReadBytesCount + CRTLCON_BYTESTOREADONSOCKET +
                                      CRTLCON_BYTESTOREADONSOCKETPREALLOC;
                        char* newBuf = (char*)realloc(ReadBytes, newSize);
                        if (newBuf != NULL)
                        {
                            ReadBytes = newBuf;
                            ReadBytesAllocatedSize = newSize;
                        }
                        else // not enough memory to store data in our buffer (only TRACE reports the error)
                        {
                            TRACE_E(LOW_MEMORY);
                            lowMem = TRUE;
                        }
                    }
                }

                if (!lowMem)
                { // read as many bytes as possible into the buffer; do not read in a loop so the data is processed gradually
                    // (smaller buffers are sufficient); if there is still something to read, we will receive FD_READ again
                    if (!SSLConn)
                    {
                        int len = recv(Socket, ReadBytes + ReadBytesCount, ReadBytesAllocatedSize - ReadBytesCount, 0);
                        if (len != SOCKET_ERROR) // we may have read something (0 = the connection is already closed)
                        {
                            if (len > 0)
                            {
                                ReadBytesCount += len; // adjust the number of bytes read by the newly received ones
                                ret = TRUE;
                                genEvent = TRUE;
                                if (WSAGETSELECTEVENT(lParam) == FD_CLOSE)
                                    sendFDCloseAgain = TRUE;
                            }
                        }
                        else
                        {
                            err = WSAGetLastError();
                            if (err != WSAEWOULDBLOCK)
                                genEvent = TRUE; // we will generate an event with the error
                        }
                    }
                    else
                    {
                        if (SSLLib.SSL_pending(SSLConn) > 0) // if the internal SSL buffer is not empty, recv() is not called at all and no FD_READ arrives, so post it ourselves to avoid stalling the transfer
                            PostMessage(SocketsThread->GetHiddenWindow(), Msg, (WPARAM)Socket, FD_READ);
                        int len = SSLLib.SSL_read(SSLConn, ReadBytes + ReadBytesCount, ReadBytesAllocatedSize - ReadBytesCount);
                        if (len >= 0) // we may have read something (0 = the connection is already closed)
                        {
                            if (len > 0)
                            {
                                ReadBytesCount += len; // adjust the number of bytes read by the newly received ones
                                ret = TRUE;
                                genEvent = TRUE;
                                if (WSAGETSELECTEVENT(lParam) == FD_CLOSE)
                                    sendFDCloseAgain = TRUE;
                            }
                        }
                        else
                        {
                            err = SSLtoWS2Error(SSLLib.SSL_get_error(SSLConn, len));
                            ;
                            if (err != WSAEWOULDBLOCK)
                                genEvent = TRUE; // we will generate an event with the error
                        }
                    }
                }
            }
            else
            {
                // May occur: the main thread manages to call CloseSocket() before FD_READ is delivered
                // TRACE_E("Unexpected situation in CControlConnectionSocket::ReceiveNetEvent(FD_READ): Socket is not connected.");
                // do not generate an event for this unexpected error (solution: the user presses ESC)
            }
        }
        else // reporting an error in FD_READ (according to the help only WSAENETDOWN)
        {
            if (WSAGETSELECTEVENT(lParam) != FD_CLOSE) // let FD_CLOSE handle the error itself
            {
                genEvent = TRUE;
                err = eventError;
            }
        }
        if (genEvent && (KeepAliveMode == kamProcessing || KeepAliveMode == kamWaitingForEndOfProcessing))
        {
            char* reply;
            int replySize;
            int replyCode;
            while (ReadFTPReply(&reply, &replySize, &replyCode)) // process responses from the server as long as we have any
            {
                Logs.LogServerMessage(LogUID, reply, replySize, TextPolicy);
                BOOL run = TRUE;
                BOOL leave = TRUE;
                BOOL setupNextKA = TRUE;
                BOOL sendList = FALSE;
                int logUID = LogUID; // log UID of this connection
                int listCmd = KeepAliveCommand;
                CKeepAliveDataConSocket* kaDataConnection = KeepAliveDataCon;
                switch (KeepAliveDataConState)
                {
                case kadcsWaitForPassiveReply: // response to PASV (open the data connection and send the listing command)
                {
                    setupNextKA = FALSE;
                    DWORD ip;
                    unsigned short port;
                    if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS &&             // success (should be 227)
                        FTPGetIPAndPortFromReply(reply, replySize, &ip, &port)) // successfully obtained IP and port
                    {
                        KeepAliveDataConState = kadcsWaitForListStart;
                        SkipFTPReply(replySize);
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        leave = FALSE;

                        kaDataConnection->SetPassive(ip, port, logUID);
                        kaDataConnection->PassiveConnect(NULL); // first attempt; the result is irrelevant (checked later)

                        sendList = TRUE;
                    }
                    else // passive mode is not supported, abort...
                    {
                        SkipFTPReply(replySize);
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        leave = FALSE;

                        Logs.LogMessage(logUID, LangStr(IDS_LOGMSGKAPASVNOTSUPPORTED).c_str(), -1, TRUE);
                        ReleaseKeepAlive();
                        run = FALSE; // abort...
                    }
                    break;
                }

                case kadcsWaitForSetPortReply:
                {
                    KeepAliveDataConState = kadcsWaitForListStart;
                    setupNextKA = FALSE;
                    sendList = TRUE;
                    break;
                }

                case kadcsWaitForListStart:
                {
                    if (FTP_DIGIT_1(replyCode) != FTP_D1_MAYBESUCCESS)
                    {
                        KeepAliveDataConState = kadcsDone;
                        SkipFTPReply(replySize);
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        leave = FALSE;

                        if (kaDataConnection->FinishDataTransfer(replyCode))
                        { // release the data connection via the SocketsThread method call
                            HANDLES(EnterCriticalSection(&SocketCritSect));
                            kaDataConnection = KeepAliveDataCon; // prevent double destruction (if the main thread waits for destruction, it is already NULL)
                            KeepAliveDataCon = NULL;
                            KeepAliveDataConState = kadcsNone;
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                            if (kaDataConnection != NULL)
                                DeleteSocket(kaDataConnection);
                        }
                        else
                            setupNextKA = FALSE; // the data connection has not finished yet; wait for it to end
                        run = FALSE;             // abort...
                    }
                    else
                    {
                        setupNextKA = FALSE;
                        SkipFTPReply(replySize);
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        leave = FALSE;

                        kaDataConnection->EncryptPassiveDataCon();
                    }
                    break;
                }
                }
                if (leave)
                {
                    SkipFTPReply(replySize);
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                }

                if (sendList) // send the "list" command ('listCmd' ('KeepAliveCommand') must be 2 or 3 to reach this point)
                {
                    const char* ftpCmd = listCmd == 3 ? "LIST\r\n" : "NLST\r\n";
                    if (SendKeepAliveCmd(logUID, ftpCmd))
                        kaDataConnection->ActivateConnection();
                    else
                        run = FALSE; // abort...
                }

                if (setupNextKA)
                {
                    // when the reply arrives, resume the main thread (if it is waiting) or set up the next
                    // keep-alive timer (if the main thread is not waiting), or release keep-alive completely
                    // (the keep-alive command period has expired)
                    SetupNextKeepAliveTimer();

                    // consume extra messages from the server (e.g. WarFTPD generates them when LISTing an inaccessible directory)
                    HANDLES(EnterCriticalSection(&SocketCritSect));
                    while (ReadFTPReply(&reply, &replySize, &replyCode)) // process responses from the server as long as we have any
                    {
                        Logs.LogServerMessage(LogUID, reply, replySize, TextPolicy);
                        SkipFTPReply(replySize);
                    }
                    break; // abort...
                }
                HANDLES(EnterCriticalSection(&SocketCritSect));
                if (!run)
                    break;
            }
            HANDLES(LeaveCriticalSection(&SocketCritSect));
            genEvent = FALSE;
        }
        else
            HANDLES(LeaveCriticalSection(&SocketCritSect));

        if (genEvent) // generate the ccsevNewBytesRead event
        {
            AddEvent(ccsevNewBytesRead, (!ret ? err : NO_ERROR), 0, ret); // overwritable only if it is not an error
        }

        // now process FD_CLOSE
        if (WSAGETSELECTEVENT(lParam) == FD_CLOSE)
        {
            if (sendFDCloseAgain) // FD_CLOSE arrived instead of FD_READ, so post FD_CLOSE again
            {
                PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                            (WPARAM)GetSocket(), lParam);
            }
            else // proper FD_CLOSE
            {
                CSocket::ReceiveNetEvent(lParam, index); // call the base method
            }
        }
        break;
    }

    case FD_WRITE:
    {
        HANDLES(EnterCriticalSection(&SocketCritSect));

        BOOL ret = FALSE;
        DWORD err = NO_ERROR;
        BOOL genEvent = FALSE;
        if (eventError == NO_ERROR)
        {
            if (BytesToWriteCount > BytesToWriteOffset) // we have remaining data, send the rest from the 'BytesToWrite' buffer
            {
                if (Socket != INVALID_SOCKET) // the socket is connected
                {
                    int len = 0;
                    if (!SSLConn)
                    {
                        while (1) // loop needed because 'send' does not post FD_WRITE when 'sentLen' < 'bytesToWrite'
                        {
                            int sentLen = send(Socket, BytesToWrite + BytesToWriteOffset + len,
                                               BytesToWriteCount - BytesToWriteOffset - len, 0);
                            if (sentLen != SOCKET_ERROR) // at least something was sent successfully (or rather taken over by Windows; delivery is uncertain)
                            {
                                len += sentLen;
                                if (len >= BytesToWriteCount - BytesToWriteOffset) // has everything been sent?
                                {
                                    ret = TRUE;
                                    break; // stop sending (there is nothing left)
                                }
                            }
                            else
                            {
                                err = WSAGetLastError();
                                if (err == WSAEWOULDBLOCK) // nothing else can be sent (Windows no longer have buffer space)
                                {
                                    ret = TRUE;
                                    break; // stop sending (the rest will be done after FD_WRITE)
                                }
                                else // different error - reset the buffer
                                {
                                    BytesToWriteOffset = 0;
                                    BytesToWriteCount = 0;
                                    break; // return the error
                                }
                            }
                        }
                    }
                    else
                    {
                        while (1) // loop needed because 'send' does not post FD_WRITE when 'sentLen' < 'bytesToWrite'
                        {
                            int sentLen = SSLLib.SSL_write(SSLConn, BytesToWrite + BytesToWriteOffset + len,
                                                           BytesToWriteCount - BytesToWriteOffset - len);
                            if (sentLen >= 0) // at least something was sent successfully (or rather taken over by Windows; delivery is uncertain)
                            {
                                len += sentLen;
                                if (len >= BytesToWriteCount - BytesToWriteOffset) // has everything been sent?
                                {
                                    ret = TRUE;
                                    break; // stop sending (there is nothing left)
                                }
                            }
                            else
                            {
                                err = SSLtoWS2Error(SSLLib.SSL_get_error(SSLConn, sentLen));
                                if (err == WSAEWOULDBLOCK) // nothing else can be sent (Windows no longer have buffer space)
                                {
                                    ret = TRUE;
                                    break; // stop sending (the rest will be done after FD_WRITE)
                                }
                                else // different error - reset the buffer
                                {
                                    BytesToWriteOffset = 0;
                                    BytesToWriteCount = 0;
                                    break; // return the error
                                }
                            }
                        }
                    }

                    if (ret && len > 0) // some data was sent, so adjust 'BytesToWriteOffset'
                    {
                        BytesToWriteOffset += len;
                        if (BytesToWriteOffset >= BytesToWriteCount) // everything sent, reset the buffer
                        {
                            BytesToWriteOffset = 0;
                            BytesToWriteCount = 0;
                        }
                    }

                    genEvent = (!ret || BytesToWriteCount == BytesToWriteOffset); // an error occurred or everything has been sent
                }
                else
                {
                    // May occur: the main thread manages to call CloseSocket() before FD_WRITE is delivered
                    //TRACE_E("Unexpected situation in CControlConnectionSocket::ReceiveNetEvent(FD_WRITE): Socket is not connected.");
                    BytesToWriteCount = 0; // error � reset the buffer
                    BytesToWriteOffset = 0;
                    // do not generate an event for this unexpected error (solution: the user presses ESC)
                }
            }
        }
        else // reporting an error in FD_WRITE (according to the help only WSAENETDOWN)
        {
            genEvent = TRUE;
            err = eventError;
            BytesToWriteCount = 0; // error � reset the buffer
            BytesToWriteOffset = 0;
        }
        if (genEvent && (KeepAliveMode == kamProcessing || KeepAliveMode == kamWaitingForEndOfProcessing))
        { // keep-alive command finished sending: instead of the ccsevWriteDone event just write to KeepAliveCmdAllBytesWritten
            KeepAliveCmdAllBytesWritten = TRUE;
            genEvent = FALSE;
        }
        HANDLES(LeaveCriticalSection(&SocketCritSect));

        if (genEvent) // generate the ccsevWriteDone event
        {
            AddEvent(ccsevWriteDone, (!ret ? err : NO_ERROR), 0);
        }
        break;
    }

    case FD_CONNECT:
    {
        HANDLES(EnterCriticalSection(&SocketCritSect));
        if (!EventConnectSent)
        {
            EventConnectSent = TRUE;
            AddEvent(ccsevConnected, eventError, 0); // post an event with the result of the connection attempt
        }
        HANDLES(LeaveCriticalSection(&SocketCritSect));
        break;
    }
    }
}

void CControlConnectionSocket::SocketWasClosed(DWORD error)
{
    CALL_STACK_MESSAGE2("CControlConnectionSocket::SocketWasClosed(%u)", error);

    AddEvent(ccsevClosed, error, 0);

    // Inform the user about the control connection closing if it does not happen
    // during a socket operation (when the user would be told about a timeout or a "kick"
    // leading to disconnection from the FTP server)
    ClosedCtrlConChecker.Add(this);

    HANDLES(EnterCriticalSection(&SocketCritSect));
    int logUID = LogUID; // log UID of this connection
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    Logs.SetIsConnected(logUID, IsConnected());
    Logs.RefreshListOfLogsInLogsDlg(); // display the "connection inactive" notification

    // Release the keep-alive; it is no longer needed because the connection is gone
    ReleaseKeepAlive();
}

//
// ****************************************************************************
// CClosedCtrlConChecker
//

CClosedCtrlConChecker::CClosedCtrlConChecker() : CtrlConSockets(2, 5, dtNoDelete)
{
    HANDLES(InitializeCriticalSection(&DataSect));
    CmdNotPost = TRUE;
}

CClosedCtrlConChecker::~CClosedCtrlConChecker()
{
    HANDLES(DeleteCriticalSection(&DataSect));
}

BOOL CClosedCtrlConChecker::Add(CControlConnectionSocket* sock)
{
    CALL_STACK_MESSAGE1("CClosedCtrlConChecker::Add()");
    HANDLES(EnterCriticalSection(&DataSect));

    BOOL ret = FALSE;
    CtrlConSockets.Add(sock);
    if (CtrlConSockets.IsGood())
    {
        ret = TRUE;
        if (CmdNotPost)
        {
            CmdNotPost = FALSE;
            SalamanderGeneral->PostMenuExtCommand(FTPCMD_CLOSECONNOTIF, TRUE);
        }
    }
    else
        CtrlConSockets.ResetState();

    HANDLES(LeaveCriticalSection(&DataSect));
    return ret;
}

void CClosedCtrlConChecker::Check(HWND parent)
{
    CALL_STACK_MESSAGE1("CClosedCtrlConChecker::Check()");
    HANDLES(EnterCriticalSection(&DataSect));

    CmdNotPost = TRUE;

    int k;
    for (k = 0; k < CtrlConSockets.Count; k++) // for all stored closed control connections (may already be deallocated)
    {
        CControlConnectionSocket* ctrlCon = CtrlConSockets[k];
        int i;
        for (i = 0; i < FTPConnections.Count; i++) // try to find a FS using 'ctrlCon' (the FS may already be closed)
        {
            CPluginFSInterface* fs = FTPConnections[i];
            if (fs->Contains(ctrlCon)) // found a FS with a closed control connection
            {
                fs->CheckCtrlConClose(parent); // if the user has not been informed yet, notify them now
                break;
            }
        }
    }
    CtrlConSockets.DestroyMembers();

    HANDLES(LeaveCriticalSection(&DataSect));
}

//
// ****************************************************************************
// CLogData
//

CLogData::CLogData(const wchar_t* host, unsigned short port, const wchar_t* user,
                   CControlConnectionSocket* ctrlCon, BOOL connected, BOOL isWorker) noexcept
{
    UID = NextLogUID++;
    if (UID == -1)
        UID = NextLogUID++; // -1 is reserved
    Port = port;
    Valid = FtpStoreWideText(host != NULL ? host : L"", Host) &&
            FtpStoreWideText(user != NULL ? user : L"", User);
    CtrlConOrWorker = !isWorker;
    WorkerIsAlive = isWorker;
    CtrlCon = ctrlCon;
    Connected = connected;
    DisconnectNum = -1;
    SkippedChars = 0;
    SkippedLines = 0;
}

CLogData::~CLogData()
{
}

BOOL CLogData::ChangeUser(const wchar_t* user) noexcept
{
    return FtpStoreWideText(user != NULL ? user : L"", User);
}

//
// ****************************************************************************
// CLogsDlgThread
//

class CLogsDlgThread : public CThread
{
protected:
    CLogsDlg* LogsDlg;
    BOOL AlwaysOnTop;

public:
    CLogsDlgThread(CLogsDlg* logsDlg) : CThread(L"Logs Dialog")
    {
        LogsDlg = logsDlg;
        AlwaysOnTop = FALSE;
        SalamanderGeneral->GetConfigParameter(SALCFG_ALWAYSONTOP, &AlwaysOnTop, sizeof(AlwaysOnTop), NULL);
    }

    virtual unsigned Body()
    {
        CALL_STACK_MESSAGE1("CLogsDlgThread::Body()");

        // 'sendWMClose': the dialog sets this to TRUE when WM_CLOSE is received
        // while a modal dialog is open above the logs dialog; once that
        // modal dialog finishes, WM_CLOSE is sent to the logs dialog again
        BOOL sendWMClose = FALSE;
        LogsDlg->SendWMClose = &sendWMClose;

        if (LogsDlg->Create() == NULL || LogsDlg->CloseDlg)
        {
            if (!LogsDlg->CloseDlg)
                Logs.SetLogsDlg(NULL);
            if (LogsDlg->HWindow != NULL)
                DestroyWindow(LogsDlg->HWindow); // WM_CLOSE cannot arrive because nothing delivers it yet (the message loop is not running)
        }
        else
        {
            HWND dlg = LogsDlg->HWindow;
            if (AlwaysOnTop) // handle always-on-top at least "statically" (not in the system menu)
                SetWindowPos(dlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

            SetForegroundWindow(dlg);

            // Message loop � wait until the modeless dialog ends
            MSG msg;
            while (GetMessageW(&msg, NULL, 0, 0))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (sendWMClose)
                {
                    sendWMClose = FALSE;
                    PostMessage(dlg, WM_CLOSE, 0, 0);
                }
            }
        }
        delete LogsDlg;
        return 0;
    }
};

//
// ****************************************************************************
// CLogs
//

void CLogs::AddLogsToCombo(HWND combo, int prevItemUID, int* focusIndex, BOOL* empty)
{
    *focusIndex = -1; // not found

    SendMessage(combo, CB_RESETCONTENT, 0, 0);

    HANDLES(EnterCriticalSection(&LogCritSect));
    *empty = (Data.Count <= 0);
    if (!*empty)
    {
        HANDLES(EnterCriticalSection(&PanelCtrlConSect));
        int i;
        for (i = 0; i < Data.Count; i++)
        {
            CLogData* d = Data[i];
            std::wstring display;
            BOOL displayReady = FALSE;

            int fsPosID = 0;
            if (d->CtrlConOrWorker) // connection in panel
            {
                if (d->CtrlCon != NULL)
                {
                    if (LeftPanelCtrlCon == d->CtrlCon)
                        fsPosID = IDS_FTPINLEFTPANEL; // FS in left panel
                    else
                    {
                        if (RightPanelCtrlCon == d->CtrlCon)
                            fsPosID = IDS_FTPINRIGHTPANEL; // FS in right panel
                        else
                            fsPosID = IDS_FTPNOTINPANEL; // detached FS
                    }
                }
                else
                    fsPosID = IDS_FTPDISCONNECTED; // closed FS
            }
            else // connection in worker used for operation (copy/move/delete)
            {
                if (d->WorkerIsAlive)
                    fsPosID = IDS_FTPOPERATION; // living worker
                else
                    fsPosID = IDS_OPERSTOPPED; // stopped worker
            }

            try
            {
                display = std::to_wstring(d->UID);
                display += L": ";
                if (!d->User.empty() && d->User != L"anonymous")
                {
                    display += d->User;
                    display += L'@';
                }
                display += d->Host;
                if (d->Port != IPPORT_FTP)
                {
                    display += L':';
                    display += std::to_wstring(d->Port);
                }
                display += L" (";
                display += LangStr(fsPosID);
                if ((d->CtrlCon != NULL || d->WorkerIsAlive) && !d->Connected)
                {
                    display += L", ";
                    display += LangStr(IDS_FTPINACTIVE);
                }
                display += L')';
                displayReady = TRUE;
            }
            catch (...)
            {
                TRACE_E(LOW_MEMORY);
            }

            // add the assembled name + log UID
            if (i == SendMessageW(combo, CB_ADDSTRING, 0,
                                  (LPARAM)(displayReady ? display.c_str() : L"<out of memory>")))
                SendMessage(combo, CB_SETITEMDATA, i, d->UID);
            if (d->UID == prevItemUID)
                *focusIndex = i;
        }
        HANDLES(LeaveCriticalSection(&PanelCtrlConSect));
    }
    else
    {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_NOLOGS).c_str());
        SendMessage(combo, CB_SETITEMDATA, 0, -1);
    }
    HANDLES(LeaveCriticalSection(&LogCritSect));
}

BOOL CLogs::GetLogIndex(int uid, int* index)
{
    if (LastUID != uid || LastIndex < 0 || LastIndex >= Data.Count || Data[LastIndex]->UID != uid)
    {
        int i;
        for (i = 0; i < Data.Count; i++)
        {
            if (Data[i]->UID == uid)
            {
                LastUID = uid;
                LastIndex = i;
                break;
            }
        }
        if (i == Data.Count) // log does not exist
        {
            *index = -1;
            return FALSE;
        }
    }
    *index = LastIndex;
    return TRUE;
}

void CLogs::SetLogToEdit(HWND edit, int logUID, BOOL update)
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    int index;
    if (logUID != -1 && GetLogIndex(logUID, &index))
    {
        BOOL lockUpdate = GetForegroundWindow() == GetParent(edit);
        CLogData* d = Data[index];
        if (!update) // set the log text
        {
            if (lockUpdate)
                LockWindowUpdate(edit);
            SetWindowTextW(edit, d->Text.GetString());
            SendMessage(edit, EM_SETSEL, d->Text.Length, d->Text.Length);
            SendMessage(edit, EM_SCROLLCARET, 0, 0);
            if (lockUpdate)
                LockWindowUpdate(NULL);
            d->SkippedChars = 0;
            d->SkippedLines = 0;
        }
        else // update the log text
        {
            if (lockUpdate)
                LockWindowUpdate(edit);
            int firstLine = (int)SendMessage(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
            int pos;
            SendMessage(edit, EM_GETSEL, 0, (LPARAM)&pos);
            if (pos == SendMessage(edit, WM_GETTEXTLENGTH, 0, 0))
                pos = d->Text.Length; // keep the end of the text
            else                      // keep the caret position
            {
                pos -= d->SkippedChars;
                if (pos < 0)
                    pos = 0;
                firstLine -= d->SkippedLines;
                if (firstLine < 0)
                    firstLine = 0;
            }

            // determine whether to scroll to the caret or to the first visible line from the previous content of the edit control
            BOOL scrollCaret = pos == d->Text.Length;
            SCROLLINFO si;
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_PAGE | SIF_POS | SIF_RANGE;
            if (scrollCaret && GetScrollInfo(edit, SB_VERT, &si) != 0)
                scrollCaret = (si.nMax == (int)si.nPage + si.nPos - 1);

            SetWindowTextW(edit, d->Text.GetString());
            SendMessage(edit, EM_SETSEL, pos, pos);
            if (scrollCaret)
                SendMessage(edit, EM_SCROLLCARET, 0, 0);
            else
                SendMessage(edit, EM_LINESCROLL, 0, firstLine);

            if (lockUpdate)
                LockWindowUpdate(NULL);
            d->SkippedChars = 0;
            d->SkippedLines = 0;
        }
    }
    else
        SetWindowTextW(edit, L""); // unknown log -> clear the edit
    HANDLES(LeaveCriticalSection(&LogCritSect));
}

void CLogs::ConfigChanged()
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    if (!Config.EnableLogging) // tear down the data
    {
        Data.DestroyMembers();
        if (!Data.IsGood())
            Data.ResetState();
        if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
            PostMessage(LogsDlg->HWindow, WM_APP_UPDATELISTOFLOGS, 0, 0);
    }
    else
    {
        if (Config.DisableLoggingOfWorkers) // remove worker logs
        {
            BOOL change = FALSE;
            int i;
            for (i = Data.Count - 1; i >= 0; i--)
            {
                if (!Data[i]->CtrlConOrWorker) // this is a worker log
                {
                    change = TRUE;
                    Data.Delete(i);
                    if (!Data.IsGood())
                        Data.ResetState();
                }
            }
            if (change && LogsDlg != NULL && LogsDlg->HWindow != NULL)
                PostMessage(LogsDlg->HWindow, WM_APP_UPDATELISTOFLOGS, 0, 0);
        }

        LimitClosedConLogs(); // handle changes to the limit of closed connection logs

        if (Config.UseLogMaxSize) // there is a limit for the log size
        {
            DWORD size = Config.LogMaxSize * 1024; // overflow is handled when the value is entered
            int i;
            for (i = 0; i < Data.Count; i++)
            {
                CLogData* d = Data[i];
                const int oldLength = d->Text.Length;
                d->Text.TrimToUtf8Size(size, &(d->SkippedChars), &(d->SkippedLines));
                if (d->Text.Length != oldLength)
                    if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
                        PostMessage(LogsDlg->HWindow, WM_APP_UPDATELOG, d->UID, 0);
            }
        }
    }
    HANDLES(LeaveCriticalSection(&LogCritSect));
}

BOOL CLogs::CreateLog(int* uid, const wchar_t* host, unsigned short port, const wchar_t* user,
                      CControlConnectionSocket* ctrlCon, BOOL connected, BOOL isWorker)
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    BOOL ok = FALSE;
    *uid = -1;
    CLogData* d = NULL;
    try
    {
        d = new CLogData(host, port, user, ctrlCon, connected, isWorker);
    }
    catch (...)
    {
        TRACE_E(LOW_MEMORY);
    }
    if (d != NULL && d->IsGood())
    {
        Data.Add(d);
        if (Data.IsGood())
        {
            if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
                PostMessage(LogsDlg->HWindow, WM_APP_UPDATELISTOFLOGS, 0, 0);
            ok = TRUE;
            *uid = d->UID;
            d = NULL;
        }
        else
            Data.ResetState();
    }
    if (d != NULL)
        delete d;
    HANDLES(LeaveCriticalSection(&LogCritSect));
    return ok;
}

BOOL CLogs::ChangeUser(int uid, const wchar_t* user)
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    BOOL ret = FALSE;
    int i;
    for (i = 0; i < Data.Count; i++)
    {
        CLogData* d = Data[i];
        if (d->UID == uid)
        {
            ret = d->ChangeUser(user);
            if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
                PostMessage(LogsDlg->HWindow, WM_APP_UPDATELOG, d->UID, 0);
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&LogCritSect));
    return ret;
}

void CLogs::LimitClosedConLogs()
{
    if (Config.UseMaxClosedConLogs) // there is a limit for the number of closed connection logs
    {
        int firstSurvival = CLogData::NextDisconnectNum - Config.MaxClosedConLogs;
        if (CLogData::OldestDisconnectNum < firstSurvival) // there is something to remove
        {
            int i;
            for (i = Data.Count - 1; i >= 0; i--)
            {
                CLogData* d = Data[i];
                if (d->CtrlCon == NULL && !d->WorkerIsAlive && // log of a closed connection (dead log)
                    d->DisconnectNum < firstSurvival)          // it is an overly old log
                {
                    Data.Delete(i);
                    if (!Data.IsGood())
                        Data.ResetState();
                }
            }
            if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
                PostMessage(LogsDlg->HWindow, WM_APP_UPDATELISTOFLOGS, 0, 0);
            CLogData::OldestDisconnectNum = firstSurvival;
        }
    }
}

void CLogs::SetIsConnected(int uid, BOOL isConnected)
{
    if (uid != -1)
    {
        HANDLES(EnterCriticalSection(&LogCritSect));
        int index;
        if (GetLogIndex(uid, &index))
        {
            CLogData* d = Data[index];
            d->Connected = isConnected;
        }
        else
            TRACE_I("CLogs::SetIsConnected(): uid (" << uid << ") not found");
        HANDLES(LeaveCriticalSection(&LogCritSect));
    }
}

BOOL CLogs::ClosingConnection(int uid)
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    BOOL ret = FALSE;
    int i;
    for (i = 0; i < Data.Count; i++)
    {
        CLogData* d = Data[i];
        if (d->UID == uid)
        {
            ret = TRUE;
            d->WorkerIsAlive = FALSE;
            d->CtrlCon = NULL;
            d->Connected = FALSE;
            d->DisconnectNum = CLogData::NextDisconnectNum++;
            LimitClosedConLogs();
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&LogCritSect));
    return ret;
}

BOOL CLogs::LogMessage(int uid, const char* str, int len, BOOL addTimeToLog)
{
    if (len == -1)
        len = (int)strlen(str);
    std::wstring text;
    if (!FtpLocalTextCodec().Decode(str, len, text))
        return FALSE;
    return LogMessage(uid, text.c_str(), (int)text.size(), addTimeToLog);
}

BOOL CLogs::LogServerMessage(int uid, const char* str, int len, const CFtpSessionTextPolicy& policy,
                             BOOL addTimeToLog)
{
    if (len == -1)
        len = (int)strlen(str);
    std::wstring text;
    if (!policy.GetCodec().Decode(str, len, text))
        return FALSE;
    return LogMessage(uid, text.c_str(), (int)text.size(), addTimeToLog);
}

BOOL CLogs::LogMessage(int uid, const wchar_t* str, int len, BOOL addTimeToLog)
{
    if (uid == -1)
        return TRUE; // "invalid UID" means there is nothing to log

    HANDLES(EnterCriticalSection(&LogCritSect));
    BOOL ret = FALSE;
    int index;
    if (GetLogIndex(uid, &index))
    {
        std::wstring timeText;
        if (addTimeToLog)
        {
            SYSTEMTIME st;
            GetLocalTime(&st); // use the standard time format to avoid slowing things down with GetTimeFormat
            try
            {
                timeText = SPLFormatStringOwned(L"(%u:%02u:%02u): ", st.wHour, st.wMinute, st.wSecond);
            }
            catch (...)
            {
                HANDLES(LeaveCriticalSection(&LogCritSect));
                return FALSE;
            }
        }

        if (len == -1)
            len = (int)wcslen(str);
        CLogData* d = Data[index];

        // Write the message to the log (in the most complex case write CR+LF before the text,
        // then the current time, and finally the rest of the text after the inserted CR+LF)
        const wchar_t* s = str;
        if (!timeText.empty())
        {
            const wchar_t* end = str + len;
            while (s < end && (*s == L'\r' || *s == L'\n'))
                s++;
        }
        if (s == str || (ret = d->Text.Append(str, (int)(s - str))) != 0)
            if (timeText.empty() || (ret = d->Text.Append(timeText.c_str(), static_cast<int>(timeText.size()))) != 0)
                ret = d->Text.Append(s, (int)(len - (s - str)));

        if (ret && Config.UseLogMaxSize)
            d->Text.TrimToUtf8Size(Config.LogMaxSize * 1024, &(d->SkippedChars), &(d->SkippedLines));

        if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
            PostMessage(LogsDlg->HWindow, WM_APP_UPDATELOG, d->UID, 0);
    }
    else
        TRACE_I("CLogs::LogMessage(): uid (" << uid << ") not found");
    HANDLES(LeaveCriticalSection(&LogCritSect));
    return ret;
}

BOOL CLogs::HasLogWithUID(int uid)
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    BOOL ret = FALSE;
    int i;
    for (i = 0; i < Data.Count; i++)
    {
        if (Data[i]->UID == uid)
        {
            ret = TRUE;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&LogCritSect));
    return ret;
}

void CLogs::SetLogsDlg(CLogsDlg* logsDlg)
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    LogsDlg = logsDlg;
    HANDLES(LeaveCriticalSection(&LogCritSect));
}

void CLogs::ActivateLog(int showLogUID)
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
        PostMessage(LogsDlg->HWindow, WM_APP_ACTIVATELOG, showLogUID, 0);
    HANDLES(LeaveCriticalSection(&LogCritSect));
}

BOOL CLogs::ActivateLogsDlg(int showLogUID)
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    BOOL ret = FALSE;
    if (LogsDlg != NULL)
    {
        if (LogsDlg->HWindow != NULL) // "always true" (otherwise the dialog will open and activate itself)
        {
            if (IsIconic(LogsDlg->HWindow))
                ShowWindow(LogsDlg->HWindow, SW_RESTORE);
            SetForegroundWindow(LogsDlg->HWindow);
            PostMessage(LogsDlg->HWindow, WM_APP_ACTIVATELOG, showLogUID, 0);
        }
        ret = TRUE;
    }
    else
    {
        LogsDlg = new CLogsDlg(NULL, SalamanderGeneral->GetMainWindowHWND(), GlobalShowLogUID);
        if (LogsDlg != NULL)
        {
            CLogsDlgThread* t = new CLogsDlgThread(LogsDlg);
            if (t != NULL)
            {
                if ((LogsThread = t->Create(AuxThreadQueue)) == NULL)
                { // the thread did not start, error
                    delete t;
                    delete LogsDlg;
                    LogsDlg = NULL;
                }
                else
                    ret = TRUE; // success
            }
            else // out of memory, error
            {
                delete LogsDlg;
                LogsDlg = NULL;
                TRACE_E(LOW_MEMORY);
            }
        }
        else
            TRACE_E(LOW_MEMORY); // out of memory, error
    }
    HANDLES(LeaveCriticalSection(&LogCritSect));
    return ret;
}

void CLogs::CloseLogsDlg()
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    if (LogsDlg != NULL)
    {
        LogsDlg->CloseDlg = TRUE; // access to CloseDlg is not synchronized, hopefully unnecessarily
        if (LogsDlg->HWindow != NULL)
            PostMessage(LogsDlg->HWindow, WM_CLOSE, 0, 0);
        LogsDlg = NULL; // deallocation is handled by the dialog thread
    }
    HANDLE t = LogsThread;
    HANDLES(LeaveCriticalSection(&LogCritSect));
    if (t != NULL)
    {
        CALL_STACK_MESSAGE1("CLogs::CloseLogsDlg(): AuxThreadQueue.WaitForExit()");
        AuxThreadQueue.WaitForExit(t, INFINITE);
    }
}

void CLogs::SaveLogsDlgPos()
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
    {
        Config.LogsDlgPlacement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(LogsDlg->HWindow, &Config.LogsDlgPlacement);
    }
    HANDLES(LeaveCriticalSection(&LogCritSect));
}

void CLogs::RefreshListOfLogsInLogsDlg()
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
        PostMessage(LogsDlg->HWindow, WM_APP_UPDATELISTOFLOGS, 0, 0);
    HANDLES(LeaveCriticalSection(&LogCritSect));
}

void CLogs::SaveLog(HWND parent, const wchar_t* itemName, int uid)
{ // itemName == NULL - "save all as..."
    static std::wstring initDir;
    if (initDir.empty())
        GetMyDocumentsPathW(initDir);
    std::wstring fileName = L"ftp.log";

    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    std::wstring filter = LangStr(IDS_SAVELOGFILTER).c_str();
    for (wchar_t& ch : filter)
        if (ch == L'|')
            ch = 0;
    filter.push_back(0);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrInitialDir = initDir.empty() ? NULL : initDir.c_str();
    ofn.lpstrDefExt = L"log";
    const std::wstring dialogTitle =
        LangStr(itemName == NULL ? IDS_SAVEALLASTITLE : IDS_SAVEASTITLE);
    ofn.lpstrTitle = dialogTitle.c_str();
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_LONGNAMES | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT |
                OFN_NOTESTFILECREATE | OFN_HIDEREADONLY;

    if (SPLSafeGetSaveFileNameOwned(SalamanderGeneral, &ofn, fileName))
    {
        HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
        FtpRememberSelectedDirectory(fileName, initDir);

        if (SalamanderGeneral->SalGetFileAttributes(fileName.c_str()) != 0xFFFFFFFF) // so that a read-only file can be overwritten
            SetFileAttributesW(fileName.c_str(), FILE_ATTRIBUTE_ARCHIVE);
        HANDLE file = HANDLES_Q(CreateFileW(fileName.c_str(), GENERIC_WRITE,
                                           FILE_SHARE_READ, NULL,
                                           CREATE_ALWAYS,
                                           FILE_FLAG_SEQUENTIAL_SCAN,
                                           NULL));
        if (file != INVALID_HANDLE_VALUE)
        {
            int sepLen = (int)strlen(LogsSeparator);

            HANDLES(EnterCriticalSection(&LogCritSect));
            DWORD err = NO_ERROR;
            int i;
            for (i = 0; i < Data.Count; i++)
            {
                CLogData* d = Data[i];
                if (uid == -1 || d->UID == uid)
                {
                    // write the log
                    ULONG written;
                    BOOL success;
                    std::string utf8;
                    const CFtpTextCodec utf8Codec = FtpUtf8TextCodec();
                    if (!utf8Codec.Encode(d->Text.GetString(), d->Text.Length, utf8))
                    {
                        err = ERROR_NO_UNICODE_TRANSLATION;
                        break;
                    }
                    if ((success = WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, NULL)) == 0 ||
                        written != (DWORD)utf8.size())
                    {
                        if (!success)
                            err = GetLastError();
                        else
                            err = ERROR_DISK_FULL;
                        break;
                    }

                    if (uid != -1)
                        break; // writing only one log, stop here

                    if (i + 1 < Data.Count) // write a separator
                    {
                        if ((success = WriteFile(file, LogsSeparator, sepLen, &written, NULL)) == 0 ||
                            written != (DWORD)sepLen)
                        {
                            if (!success)
                                err = GetLastError();
                            else
                                err = ERROR_DISK_FULL;
                            break;
                        }
                    }
                }
            }
            HANDLES(LeaveCriticalSection(&LogCritSect));

            HANDLES(CloseHandle(file));
            SetCursor(oldCur);
            if (err != NO_ERROR) // report the error
            {
                FTPShowSystemError(parent, IDS_SAVELOGERROR, err);
                DeleteFileW(fileName.c_str()); // delete the file when an error occurs
            }

            // announce a change on the path (our file may have appeared)
            std::wstring notificationPath = fileName;
            SPLCutDirectoryOwned(SalamanderGeneral, notificationPath);
            SalamanderGeneral->PostChangeOnPathNotification(notificationPath.c_str(), FALSE);
        }
        else
        {
            DWORD err = GetLastError();
            SetCursor(oldCur);
            FTPShowSystemError(parent, IDS_SAVELOGERROR, err);
        }
    }
}

void CLogs::CopyLog(HWND parent, const wchar_t* itemName, int uid)
{
    HANDLES(EnterCriticalSection(&LogCritSect));
    BOOL err = FALSE, found = FALSE;
    int i;
    for (i = 0; i < Data.Count; i++)
    {
        CLogData* d = Data[i];
        if (d->UID == uid)
        {
            err = !SalamanderGeneral->CopyTextToClipboard(d->Text.GetString(), d->Text.Length, FALSE, NULL);
            found = TRUE;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&LogCritSect));
    if (found)
    {
        if (!err)
        {
            SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_TEXTCOPIEDTOCLIPBOARD).c_str(),
                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                             MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_COPYTOCLIPBOARDERROR).c_str(),
                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                             MB_OK | MB_ICONEXCLAMATION);
        }
    }
}

void CLogs::ClearLog(HWND parent, const wchar_t* itemName, int uid)
{
    const std::wstring text = SPLFormatStringOwned(LangStr(IDS_CLEARLOGQUESTION).c_str(),
                                                   itemName != NULL ? itemName : L"");
    MSGBOXEX_PARAMS params;
    const std::wstring caption = LangStr(IDS_FTPPLUGINTITLE);
    memset(&params, 0, sizeof(params));
    params.HParent = parent;
    params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT;
    params.Caption = caption.c_str();
    params.Text = text.c_str();
    if (SalamanderGeneral->SalMessageBoxEx(&params) == IDYES)
    {
        HANDLES(EnterCriticalSection(&LogCritSect));
        int i;
        for (i = 0; i < Data.Count; i++)
        {
            CLogData* d = Data[i];
            if (d->UID == uid)
            {
                d->Text.Clear();
                if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
                    PostMessage(LogsDlg->HWindow, WM_APP_UPDATELOG, d->UID, 0);
                break;
            }
        }
        HANDLES(LeaveCriticalSection(&LogCritSect));
    }
}

void CLogs::RemoveLog(HWND parent, const wchar_t* itemName, int uid)
{
    const std::wstring text = SPLFormatStringOwned(LangStr(IDS_REMOVELOGQUESTION).c_str(),
                                                   itemName != NULL ? itemName : L"");
    MSGBOXEX_PARAMS params;
    const std::wstring caption = LangStr(IDS_FTPPLUGINTITLE).c_str();
    memset(&params, 0, sizeof(params));
    params.HParent = parent;
    params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT;
    params.Caption = caption.c_str();
    params.Text = text.c_str();
    if (SalamanderGeneral->SalMessageBoxEx(&params) == IDYES)
    {
        HANDLES(EnterCriticalSection(&LogCritSect));
        int i;
        for (i = 0; i < Data.Count; i++)
        {
            CLogData* d = Data[i];
            if (d->UID == uid)
            {
                Data.Delete(i);
                if (!Data.IsGood())
                    Data.ResetState();
                if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
                    PostMessage(LogsDlg->HWindow, WM_APP_UPDATELISTOFLOGS, 0, 0);
                break;
            }
        }
        HANDLES(LeaveCriticalSection(&LogCritSect));
    }
}

void CLogs::SaveAllLogs(HWND parent)
{
    SaveLog(parent, NULL, -1);
}

void CLogs::CopyAllLogs(HWND parent)
{
    int sepLen = (int)wcslen(LogsSeparatorW);

    HANDLES(EnterCriticalSection(&LogCritSect));
    int len = 0, i;
    for (i = 0; i < Data.Count; i++)
    {
        CLogData* d = Data[i];
        len += d->Text.Length;
    }
    if (Data.Count > 1)
        len += (Data.Count - 1) * sepLen;
    BOOL err = TRUE;
    wchar_t* txt = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (txt != NULL)
    {
        wchar_t* s = txt;
        for (i = 0; i < Data.Count; i++)
        {
            CLogData* d = Data[i];
            memcpy(s, d->Text.GetString(), d->Text.Length * sizeof(wchar_t));
            s += d->Text.Length;
            if (i + 1 < Data.Count)
            {
                memcpy(s, LogsSeparatorW, sepLen * sizeof(wchar_t));
                s += sepLen;
            }
        }
        *s = 0;
        err = !SalamanderGeneral->CopyTextToClipboard(txt, len, FALSE, NULL);
        free(txt);
    }
    else
        TRACE_E(LOW_MEMORY);
    HANDLES(LeaveCriticalSection(&LogCritSect));

    if (!err)
    {
        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_TEXTCOPIEDTOCLIPBOARD).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                         MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_COPYTOCLIPBOARDERROR).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
    }
}

void CLogs::RemoveAllLogs(HWND parent)
{
    MSGBOXEX_PARAMS params;
    const std::wstring caption = LangStr(IDS_FTPPLUGINTITLE).c_str();
    const std::wstring text = LangStr(IDS_REMOVEALLLOGSQUESTION).c_str();
    memset(&params, 0, sizeof(params));
    params.HParent = parent;
    params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT;
    params.Caption = caption.c_str();
    params.Text = text.c_str();
    if (SalamanderGeneral->SalMessageBoxEx(&params) == IDYES)
    {
        HANDLES(EnterCriticalSection(&LogCritSect));
        Data.DestroyMembers();
        if (!Data.IsGood())
            Data.ResetState();
        if (LogsDlg != NULL && LogsDlg->HWindow != NULL)
            PostMessage(LogsDlg->HWindow, WM_APP_UPDATELISTOFLOGS, 0, 0);
        HANDLES(LeaveCriticalSection(&LogCritSect));
    }
}
