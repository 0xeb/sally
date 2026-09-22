// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

static char* DupControlConnectionText(const char* text)
{
    if (text == NULL)
        return NULL;
    const int length = (int)strlen(text) + 1;
    char* copy = (char*)SalamanderGeneral->Alloc(length);
    if (copy != NULL)
        memcpy(copy, text, length);
    return copy;
}

CClosedCtrlConChecker ClosedCtrlConChecker; // handles informing the user about "control connection" closure outside of operations
CListingCache ListingCache;                 // cache of directory listings on servers (used when changing and listing directories)

int CLogData::NextLogUID = 0;          // global counter for log objects
int CLogData::OldestDisconnectNum = 0; // disconnect number of the oldest server log that disconnected
int CLogData::NextDisconnectNum = 0;   // disconnect number for the next server log that will disconnect

CLogs Logs; // logs of all connections to FTP servers

CControlConnectionSocket* LeftPanelCtrlCon = NULL;
CControlConnectionSocket* RightPanelCtrlCon = NULL;
CRITICAL_SECTION PanelCtrlConSect; // critical section for access to LeftPanelCtrlCon and RightPanelCtrlCon

//
// ****************************************************************************
// CControlConnectionSocket
//

CControlConnectionSocket::CControlConnectionSocket() : Events(5, 5), TextPolicy(FtpLocalTextCodePage())
{
    HANDLES(InitializeCriticalSection(&EventCritSect));
    EventsUsedCount = 0;
    NewEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL)); // auto, nonsignaled
    if (NewEvent == NULL)
        TRACE_E("Unable to create synchronization event object needed for handling of socket events.");
    RewritableEvent = FALSE;

    ProxyServer = NULL;
    Host.clear();
    Port = 0;
    User.clear();
    Password.clear();
    Account.clear();
    UseListingsCache = TRUE;
    InitFTPCommands.clear();
    UsePassiveMode = TRUE;
    ListCommand.clear();
    UseLIST_aCommand = FALSE;

    ServerIP = INADDR_NONE;
    CanSendOOBData = TRUE;
    ServerSystem.clear();
    ServerFirstReply.clear();
    HaveWorkingPath = FALSE;
    WorkingPath.clear();
    CurrentTransferMode = ctrmUnknown;

    EventConnectSent = FALSE;

    BytesToWrite = NULL;
    BytesToWriteCount = 0;
    BytesToWriteOffset = 0;
    BytesToWriteAllocatedSize = 0;

    ReadBytes = NULL;
    ReadBytesCount = 0;
    ReadBytesOffset = 0;
    ReadBytesAllocatedSize = 0;

    StartTime = 0;

    LogUID = -1;

    ConnectionLostMsg.clear();

    OurWelcomeMsgDlg = NULL;

    KeepAliveMode = kamNone;
    KeepAliveEnabled = FALSE;
    KeepAliveSendEvery = Config.KeepAliveSendEvery;
    KeepAliveStopAfter = Config.KeepAliveStopAfter;
    KeepAliveCommand = Config.KeepAliveCommand;
    KeepAliveStart = 0;
    KeepAliveCmdAllBytesWritten = TRUE;
    KeepAliveFinishedEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL)); // auto, nonsignaled
    if (KeepAliveFinishedEvent == NULL)
        TRACE_E("Unable to create synchronization event object needed for handling of keep alive commands.");
    KeepAliveDataCon = NULL;
    KeepAliveDataConState = kadcsNone;
    EncryptControlConnection = EncryptDataConnection = 0;
    CompressData = 0;
}

CControlConnectionSocket::~CControlConnectionSocket()
{
    if (KeepAliveFinishedEvent != NULL)
        HANDLES(CloseHandle(KeepAliveFinishedEvent));

    // std::string members (ConnectionLostMsg, InitFTPCommands, ListCommand,
    // ServerSystem, ServerFirstReply) are automatically freed

    if (BytesToWrite != NULL)
        free(BytesToWrite);
    if (ReadBytes != NULL)
        free(ReadBytes);

    if (ProxyServer != NULL)
        delete ProxyServer;

    FTPSecureWipe(Password);
    FTPSecureWipe(Account);
    if (NewEvent != NULL)
        HANDLES(CloseHandle(NewEvent));
    HANDLES(DeleteCriticalSection(&EventCritSect));

    // Logs cannot touch the "control connection" (nested critical sections are forbidden),
    // this call synchronizes only the validity of the pointer to the "control connection" (not the object contents,
    // therefore it can be at the very end of the destructor)
    if (LogUID != -1)
        Logs.ClosingConnection(LogUID);
}

DWORD
CControlConnectionSocket::GetTimeFromStart()
{
    DWORD t = GetTickCount();
    return t - StartTime; // works even for t < StartTime (tick counter wraparound)
}

DWORD
CControlConnectionSocket::GetWaitTime(DWORD showTime)
{
    DWORD waitTime = GetTimeFromStart();
    if (waitTime < showTime)
        return showTime - waitTime;
    else
        return 0;
}

BOOL CControlConnectionSocket::AddEvent(CControlConnectionSocketEvent event, DWORD data1,
                                        DWORD data2, BOOL rewritable)
{
    CALL_STACK_MESSAGE5("CControlConnectionSocket::AddEvent(%d, %u, %u, %d)",
                        (int)event, data1, data2, rewritable);
    BOOL ret = FALSE;
    HANDLES(EnterCriticalSection(&EventCritSect));
    CControlConnectionSocketEventData* e = NULL;
    if (RewritableEvent && EventsUsedCount > 0)
        e = Events[EventsUsedCount - 1];
    else
    {
        if (EventsUsedCount < Events.Count)
            e = Events[EventsUsedCount++]; // we already have preallocated space for the event
        else                               // we must allocate it
        {
            e = new CControlConnectionSocketEventData;
            if (e != NULL)
            {
                Events.Add(e);
                if (Events.IsGood())
                    EventsUsedCount++;
                else
                {
                    Events.ResetState();
                    delete e;
                    e = NULL;
                }
            }
            else
                TRACE_E(LOW_MEMORY);
        }
    }
    if (e != NULL) // we have space for the event, store the data into it
    {
        e->Event = event;
        e->Data1 = data1;
        e->Data2 = data2;
        RewritableEvent = rewritable;
        ret = TRUE;
    }
    HANDLES(LeaveCriticalSection(&EventCritSect));
    if (ret)
        SetEvent(NewEvent);
    return ret;
}

BOOL CControlConnectionSocket::GetEvent(CControlConnectionSocketEvent* event, DWORD* data1, DWORD* data2)
{
    CALL_STACK_MESSAGE1("CControlConnectionSocket::GetEvent(,,)");
    BOOL ret = FALSE;
    HANDLES(EnterCriticalSection(&EventCritSect));
    if (EventsUsedCount > 0)
    {
        CControlConnectionSocketEventData* e = Events[0];
        *event = e->Event;
        *data1 = e->Data1;
        *data2 = e->Data2;
        EventsUsedCount--;
        Events.Detach(0);
        if (!Events.IsGood())
            Events.ResetState();
        Events.Add(e); // the event object will be reused later, put it at the end of the queue
        if (!Events.IsGood())
        {
            delete e; // failed to add it, too bad
            Events.ResetState();
        }
        RewritableEvent = FALSE;
        ret = TRUE;
        if (EventsUsedCount > 0)
            SetEvent(NewEvent); // there is another one
    }
    HANDLES(LeaveCriticalSection(&EventCritSect));
    return ret;
}

void CControlConnectionSocket::WaitForEventOrESC(HWND parent, CControlConnectionSocketEvent* event,
                                                 DWORD* data1, DWORD* data2, int milliseconds,
                                                 CWaitWindow* waitWnd, CSendCmdUserIfaceAbstract* userIface,
                                                 BOOL waitForUserIfaceFinish)
{
    CALL_STACK_MESSAGE3("CControlConnectionSocket::WaitForEventOrESC(, , , , %d, , , %d)",
                        milliseconds, waitForUserIfaceFinish);

    const DWORD cycleTime = 200; // period of testing the ESC key press in ms (200 = 5 times per second)
    DWORD timeStart = GetTickCount();
    DWORD restOfWaitTime = milliseconds; // remaining waiting time

    HANDLE watchedEvent;
    BOOL watchingUserIface;
    if (waitForUserIfaceFinish && userIface != NULL)
    {
        if ((watchedEvent = userIface->GetFinishedEvent()) == NULL)
        {
            TRACE_E("Unexpected situation in CControlConnectionSocket::WaitForEventOrESC(): userIface->GetFinishedEvent() returned NULL!");
            Sleep(200); // so the whole machine does not freeze...
            *event = ccsevUserIfaceFinished;
            *data1 = 0;
            *data2 = 0;
            return; // report completion of work in the user interface
        }
        watchingUserIface = TRUE;
    }
    else
    {
        watchingUserIface = FALSE;
        watchedEvent = NewEvent;
    }

    GetAsyncKeyState(VK_ESCAPE); // init GetAsyncKeyState - see help
    while (1)
    {
        DWORD waitTime;
        if (milliseconds != INFINITE)
            waitTime = min(cycleTime, restOfWaitTime);
        else
            waitTime = cycleTime;
        DWORD waitRes = MsgWaitForMultipleObjects(1, &watchedEvent, FALSE, waitTime, QS_ALLINPUT);

        // first check for ESC press (so we do not ignore it for the user)
        if (milliseconds != 0 &&                                                          // if the timeout is zero, we are only pumping messages, ignore ESC
            ((GetAsyncKeyState(VK_ESCAPE) & 0x8001) && GetForegroundWindow() == parent || // ESC key pressed
             waitWnd != NULL && waitWnd->GetWindowClosePressed() ||                       // close button in the wait window
             userIface != NULL && userIface->GetWindowClosePressed()))                    // close button in the user interface
        {
            // cannot read the event now, leave it for next time
            if (waitRes == WAIT_OBJECT_0 && !watchingUserIface)
                SetEvent(NewEvent);

            MSG msg; // discard buffered ESC
            while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
                ;
            *event = ccsevESC;
            *data1 = 0;
            *data2 = 0;
            break; // report ESC
        }
        if (waitRes == WAIT_OBJECT_0) // try to read a new event
        {
            if (!watchingUserIface)
            {
                if (GetEvent(event, data1, data2))
                    break; // report a new event
            }
            else // user interface reports completion
            {
                *event = ccsevUserIfaceFinished;
                *data1 = 0;
                *data2 = 0;
                break; // report completion of work in the user interface
            }
        }
        else
        {
            if (waitRes == WAIT_OBJECT_0 + 1) // process Windows messages
            {
                MSG msg;
                while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            else
            {
                if (waitRes == WAIT_TIMEOUT &&
                    restOfWaitTime == waitTime) // not just the timeout of the ESC key test cycle, but the global timeout
                {
                    *event = ccsevTimeout; // no time remains
                    *data1 = 0;
                    *data2 = 0;
                    break; // report timeout
                }
            }
        }
        if (milliseconds != INFINITE) // recalculate the remaining waiting time (based on real time)
        {
            DWORD t = GetTickCount() - timeStart; // works even when the tick counter wraps around
            if (t < (DWORD)milliseconds)
                restOfWaitTime = (DWORD)milliseconds - t;
            else
                restOfWaitTime = 0; // let it report the timeout (we must not do it ourselves - the event has priority over the timeout)
        }
    }
}

BOOL CControlConnectionSocket::SetConnectionParameters(const wchar_t* host, unsigned short port, const wchar_t* user,
                                                       const wchar_t* password, BOOL useListingsCache,
                                                       const char* initFTPCommands, BOOL usePassiveMode,
                                                       const char* listCommand, BOOL keepAliveEnabled,
                                                       int keepAliveSendEvery, int keepAliveStopAfter,
                                                       int keepAliveCommand, int proxyServerUID,
                                                       int encryptControlConnection, int encryptDataConnection,
                                                       int compressData) noexcept
{
    CALL_STACK_MESSAGE12("CControlConnectionSocket::SetConnectionParameters(%ls, %u, %d, %s, %d, %s, %d, %d, %d, %d, %d)",
                         host, (unsigned)port, useListingsCache, initFTPCommands,
                         usePassiveMode, listCommand, keepAliveEnabled, keepAliveSendEvery,
                         keepAliveStopAfter, keepAliveCommand, proxyServerUID);
    std::wstring stagedHost;
    std::wstring stagedUser;
    std::wstring stagedPassword;
    std::string stagedInitFTPCommands;
    std::string stagedListCommand;
    if (!FtpStoreWideText(host != NULL ? host : L"", stagedHost) ||
        !FtpStoreWideText(user != NULL ? user : L"", stagedUser) ||
        !FtpStoreWideText(password != NULL ? password : L"", stagedPassword) ||
        !FtpStoreProtocolBytes(initFTPCommands != NULL ? initFTPCommands : "", stagedInitFTPCommands) ||
        !FtpStoreProtocolBytes(listCommand != NULL ? listCommand : "", stagedListCommand))
        return FALSE;

    if (proxyServerUID == -2)
        proxyServerUID = Config.DefaultProxySrvUID;
    CFTPProxyServer* stagedProxyServer = NULL;
    if (proxyServerUID != -1)
    {
        BOOL lowMemory = FALSE;
        stagedProxyServer = Config.FTPProxyServerList.MakeCopyOfProxyServer(proxyServerUID, &lowMemory);
        if (stagedProxyServer == NULL && lowMemory)
        {
            FTPSecureWipe(stagedPassword);
            return FALSE;
        }
        if (stagedProxyServer != NULL)
        {
            if (stagedProxyServer->ProxyEncryptedPassword != NULL)
            {
                // decrypt the password into ProxyPlainPassword
                CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
                std::wstring plainPassword;
                if (!FTPDecryptPasswordW(passwordManager,
                                         stagedProxyServer->ProxyEncryptedPassword,
                                         stagedProxyServer->ProxyEncryptedPasswordSize,
                                         &plainPassword))
                {
                    TRACE_E("CControlConnectionSocket::SetConnectionParameters(): internal error, cannot decrypt password!");
                    delete stagedProxyServer;
                    FTPSecureWipe(stagedPassword);
                    return FALSE;
                }
                if (!stagedProxyServer->SetProxyPassword(plainPassword.c_str()))
                {
                    FTPSecureWipe(plainPassword);
                    delete stagedProxyServer;
                    FTPSecureWipe(stagedPassword);
                    return FALSE;
                }
                FTPSecureWipe(plainPassword);
            }
        }
    }

    HANDLES(EnterCriticalSection(&SocketCritSect));
    CFTPProxyServer* oldProxyServer = ProxyServer;
    ProxyServer = stagedProxyServer;
    stagedProxyServer = NULL;
    Host.swap(stagedHost);
    Port = port;
    User.swap(stagedUser);
    Password.swap(stagedPassword);
    UseListingsCache = useListingsCache;
    InitFTPCommands.swap(stagedInitFTPCommands);
    UsePassiveMode = usePassiveMode;
    ListCommand.swap(stagedListCommand);
    UseLIST_aCommand = FALSE;
    KeepAliveEnabled = keepAliveEnabled;
    KeepAliveSendEvery = keepAliveSendEvery;
    KeepAliveStopAfter = keepAliveStopAfter;
    KeepAliveCommand = keepAliveCommand;
    EncryptControlConnection = encryptControlConnection;
    EncryptDataConnection = encryptDataConnection;
    CompressData = compressData;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    delete oldProxyServer;
    FTPSecureWipe(stagedPassword);
    return TRUE;
}

CFtpTextCodec CControlConnectionSocket::GetTextCodec()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    const CFtpTextCodec codec = TextPolicy.GetCodec();
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return codec;
}

BOOL CControlConnectionSocket::EncodeText(const wchar_t* text, std::string& bytes)
{
    const CFtpTextCodec codec = GetTextCodec();
    return codec.Encode(text, wcslen(text), bytes);
}

BOOL CControlConnectionSocket::DecodeText(const char* bytes, size_t length, std::wstring& text)
{
    const CFtpTextCodec codec = GetTextCodec();
    return codec.Decode(bytes, length, text);
}

enum CStartCtrlConStates // states of the automaton for CControlConnectionSocket::StartControlConnection
{
    // obtain an IP address from the textual address of the FTP server
    sccsGetIP,

    // fatal error (resource ID of the text is in 'fatalErrorTextID'; -1 uses 'directErrorText')
    sccsFatalError,

    // fatal operation error (resource ID of the text is in 'opFatalErrorTextID' and the Windows error number in
    // 'opFatalError'; -1 uses 'directErrorText'; if 'opFatalErrorTextID' is -1,
    // 'operationFormatText' contains the presentation format)
    sccsOperationFatalError,

    // connect to the FTP server (retrieved IP is in 'auxServerIP')
    sccsConnect,

    // retry the connection (based on Config.DelayBetweenConRetries + Config.ConnectRetries),
    // if it should not retry anymore, it transitions to the state from 'noRetryState' + if 'fastRetry' is TRUE, it must not
    // wait before the next attempt
    sccsRetry,

    // now connected, read the message from the server (expecting "220 Service ready for new user")
    sccsServerReady,

    // initialize sending of the login sequence of commands - according to the proxy server script
    sccsStartLoginScript,

    // gradually send the login sequence of commands - according to the proxy server script
    sccsProcessLoginScript,

    // retry the login (without waiting and losing the connection) - only transitions to sccsStartLoginScript - cannot be used with a proxy server!!!
    sccsRetryLogin,

    // end of the method (successful or unsuccessful - according to 'ret' TRUE/FALSE)
    sccsDone
};

BOOL GetFatalErrorText(int fatalErrorTextID, const std::string& directErrorText,
                       std::string& errorText) noexcept
{
    return FtpStoreProtocolBytes(
        fatalErrorTextID == -1 ? std::string_view(directErrorText)
                               : std::string_view(LoadStr(fatalErrorTextID)),
        errorText);
}

BOOL GetOperationFatalErrorText(int opFatalError, const std::string& directErrorText,
                                std::string& errorText) noexcept
{
    if (opFatalError == -1)
        return FtpStoreProtocolBytes(directErrorText, errorText);
    if (opFatalError != NO_ERROR)
        return FTPGetErrorText(opFatalError, errorText);
    return FtpStoreProtocolBytes(LoadStr(IDS_UNKNOWNERROR), errorText);
}

void TrimLineEnds(std::string& text) noexcept
{
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
}

BOOL GetLocaleDateTimePart(const SYSTEMTIME& time, BOOL date,
                           std::string& text) noexcept
{
    try
    {
        const int length = date ? GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time,
                                                  NULL, NULL, 0)
                                : GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &time,
                                                  NULL, NULL, 0);
        if (length > 0)
        {
            std::string staged(static_cast<size_t>(length), '\0');
            const int written = date ? GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time,
                                                        NULL, staged.data(), length)
                                     : GetTimeFormatA(LOCALE_USER_DEFAULT, 0, &time,
                                                        NULL, staged.data(), length);
            if (written == length)
            {
                staged.resize(static_cast<size_t>(length - 1));
                text.swap(staged);
                return TRUE;
            }
        }
    }
    catch (...)
    {
        return FALSE;
    }
    return date ? FTPFormatString(text, "%u.%u.%u", time.wDay, time.wMonth, time.wYear)
                : FTPFormatString(text, "%u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
}

BOOL GetToken(char** s, char** next)
{
    char* t = *next;
    *s = t;
    if (*t == 0)
        return FALSE; // end of string, no more tokens
    char* dst = NULL;
    do
    {
        if (*t == ';')
        {
            if (*(t + 1) == ';') // escape sequence: ";;" -> ";"
            {
                if (dst == NULL)
                    dst = t;
                t++;
            }
            else
            {
                if (dst == NULL)
                    *t++ = 0;
                else
                    t++;
                break;
            }
        }
        if (dst != NULL)
            *dst++ = *t;
        t++;
    } while (*t != 0);
    if (dst != NULL)
        *dst = 0;
    *next = t;
    return TRUE;
}

typedef enum eSSLInit
{
    sslisAUTH,
    sslisPBSZ,
    sslisPROT,
    sslisNone,
} eSSLInit;

HWND FindPopupParent(HWND wnd)
{
    HWND win = wnd;
    for (;;)
    {
        HWND w = (GetWindowLong(win, GWL_STYLE) & WS_CHILD) ? GetParent(win) : NULL;
        if (w == NULL)
            break;
        win = w;
    }
    //  if (win != wnd) TRACE_E("FindPopupParent(): found! (" << win << " for " << wnd << ")");
    return win;
}

BOOL CControlConnectionSocket::StartControlConnection(HWND parent, std::wstring& user, BOOL reconnect,
                                                      std::string* workDir, int* totalAttemptNum,
                                                      const std::string* retryMessage, BOOL canShowWelcomeDlg,
                                                      int reconnectErrResID, BOOL useFastReconnect)
{
    CALL_STACK_MESSAGE6("CControlConnectionSocket::StartControlConnection(, , %d, , , %s, %d, %d, %d)",
                        reconnect, retryMessage != NULL ? retryMessage->c_str() : NULL,
                        canShowWelcomeDlg, reconnectErrResID,
                        useFastReconnect);

    parent = FindPopupParent(parent);
    if (workDir != NULL)
        workDir->clear();
    if (retryMessage != NULL)
        reconnect = TRUE; // in this case it certainly is a reconnect

    BOOL ret = FALSE;
    int fatalErrorTextID = 0;
    int opFatalErrorTextID = 0;
    int opFatalError = 0;
    CStartCtrlConStates noRetryState = sccsDone;
    BOOL retryLogError = TRUE;   // FALSE = do not print the error message to the log (reason: it has already been printed)
    BOOL fatalErrLogMsg = TRUE;  // FALSE = do not print the error message to the log (reason: it has already been printed)
    BOOL actionCanceled = FALSE; // TRUE = write "action canceled" to the log at 'sccsDone'

    int attemptNum = 1;
    if (totalAttemptNum != NULL)
        attemptNum = *totalAttemptNum; // initialize with the total number of attempts
    CDynString welcomeMessage;
    BOOL useWelcomeMessage = canShowWelcomeDlg && !reconnect && Config.ShowWelcomeMessage;
    BOOL retryLoginWithoutAsking = FALSE;
    BOOL fastRetry = FALSE;
    unsigned short port;
    in_addr srvAddr;
    int logUID = -1; // UID of the log for this connection: currently "invalid log"

    std::string proxyScriptText;
    std::wstring host;
    std::string hostBytes;
    std::string nextRetryMessage;
    std::string proxyErrorText;
    std::string proxyErrorFormat;
    std::string proxyScriptError;
    std::string directErrorText;
    std::string operationFormatText;
    std::wstring waitText;

    const DWORD showWaitWndTime = WAITWND_STARTCON; // show time of the wait window
    int serverTimeout = Config.GetServerRepliesTimeout() * 1000;
    if (serverTimeout < 1000)
        serverTimeout = 1000; // at least one second

    // store the focus from 'parent' (if the focus is not from 'parent', store NULL)
    HWND focusedWnd = GetFocus();
    HWND hwnd = focusedWnd;
    while (hwnd != NULL && hwnd != parent)
        hwnd = GetParent(hwnd);
    if (hwnd != parent)
        focusedWnd = NULL;

    // disable 'parent', restore the focus when re-enabling
    EnableWindow(parent, FALSE);

    // set a wait cursor over the parent, unfortunately we cannot do it otherwise
    CSetWaitCursorWindow* winParent = new CSetWaitCursorWindow;
    if (winParent != NULL)
        winParent->AttachToWindow(parent);

    OurWelcomeMsgDlg = NULL; // no need to synchronize, accessed only from the main thread

    CWaitWindow waitWnd(parent, TRUE);

    BOOL commandAllocationFailed = FALSE;
    HANDLES(EnterCriticalSection(&SocketCritSect));
    const CFtpTextCodec loginTextCodec(FALSE, TextPolicy.GetLegacyCodePage());
    std::wstring initialUser;
    commandAllocationFailed = !FtpStoreWideText(User, initialUser);
    CProxyScriptParams proxyScriptParams(ProxyServer, Host.c_str(), Port, User.c_str(), Password.c_str(),
                                         Account.c_str(), Password.empty() && reconnect);
    commandAllocationFailed = commandAllocationFailed || !proxyScriptParams.IsGood();
    CFTPProxyServerType proxyType = fpstNotUsed;
    if (ProxyServer != NULL)
        proxyType = ProxyServer->ProxyType;
    if (proxyType == fpstOwnScript)
        commandAllocationFailed = !FtpStoreLocalTextBytes(ProxyServer->ProxyScript, proxyScriptText);
    else
    {
        const char* txt = GetProxyScriptText(proxyType, FALSE);
        if (txt[0] == 0)
            txt = GetProxyScriptText(fpstNotUsed, FALSE); // undefined script = "not used (direct connection)" script - SOCKS 4/4A/5, HTTP 1.1
        commandAllocationFailed = !FTPFormatString(proxyScriptText, "%s", txt);
    }
    DWORD auxServerIP = ServerIP;
    srvAddr.s_addr = auxServerIP;
    ResetWorkingPathCache();         // after connecting to the server it is necessary to determine the working dir
    ResetCurrentTransferModeCache(); // after connecting to the server it is necessary to set the transfer mode (it should be ASCII, but we do not trust it)
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    if (!commandAllocationFailed)
        user.swap(initialUser);

    const char* proxyScriptExecPoint = NULL;
    const char* proxyScriptStartExecPoint = NULL; // first script command (line following "connect to:")
    int proxyLastCmdReply = -1;
    std::string proxyLastCmdReplyText;
    std::string proxySendCmdBuf;
    CFTPSecureByteStringGuard proxySendCmdGuard(proxySendCmdBuf);
    eSSLInit SSLInitSequence = EncryptControlConnection ? sslisAUTH : sslisNone;
    std::string proxyLogCmdBuf;
    std::wstring connectingToAs;
    bool bModeZSent = false;

    // prepare keep-alive for further use + set keep-alive to 'kamForbidden' (a normal command is in progress)
    ReleaseKeepAlive();
    WaitForEndOfKeepAlive(parent, 0); // cannot open the wait window (it is in the 'kamNone' state)

    CStartCtrlConStates state = (auxServerIP == INADDR_NONE ? sccsGetIP : sccsConnect);
    // Guards the RETRY_LABEL wipe below against erasing the AUTH TLS command
    // this function just staged, two statements above, before the state machine
    // has even started. False only for that one interval; every later pass
    // through the label (the retry goto) is a genuinely stale buffer from a
    // previous attempt and must be wiped.
    bool haveEnteredStateMachine = false;
    auto setCommandPair = [&](const char* command) noexcept
    {
        FTPSecureWipe(proxySendCmdBuf);
        return FTPFormatString(proxySendCmdBuf, "%s", command) &&
               FTPFormatString(proxyLogCmdBuf, "%s", proxySendCmdBuf.c_str());
    };
    if (commandAllocationFailed || sslisAUTH == SSLInitSequence && !setCommandPair("AUTH TLS\r\n"))
    {
        fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
        state = sccsFatalError;
    }

    if (retryMessage != NULL) // simulate the state when the connection was interrupted directly in this method - "retry" connection
    {
        if (FtpStoreProtocolBytes(*retryMessage, directErrorText))
        {
            opFatalErrorTextID = reconnectErrResID != -1 ? reconnectErrResID : IDS_SENDCOMMANDERROR;
            opFatalError = -1;                      // the error is directly in directErrorText
            noRetryState = sccsOperationFatalError; // if retry is not performed, execute sccsOperationFatalError
            retryLogError = FALSE;                  // the error is already in the log, do not add it again
            state = sccsRetry;
        }
        else
        {
            fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
            state = sccsFatalError;
        }
        // useWelcomeMessage = FALSE;  // we already printed it, repeating it makes no sense  -- "always FALSE"
        fastRetry = useFastReconnect;
    }

    BOOL proxyScriptLowMemory = FALSE;
    if (state != sccsFatalError &&
        ProcessProxyScript(loginTextCodec, proxyScriptText.c_str(), &proxyScriptExecPoint, proxyLastCmdReply,
                           &proxyScriptParams, &host, &port, NULL, NULL, &proxyScriptError, NULL,
                           &proxyScriptLowMemory))
    {
        if (proxyScriptParams.NeedUserInput()) // theoretically should not happen
        {                                      // only proxyScriptParams->NeedProxyHost can be TRUE (otherwise ProcessProxyScript would return an error)
            if (FtpStoreProtocolBytes(proxyScriptError, directErrorText))
            {
                opFatalError = -1; // error is directly in directErrorText
                opFatalErrorTextID = IDS_ERRINPROXYSCRIPT;
                state = sccsOperationFatalError;
            }
            else
            {
                fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                state = sccsFatalError;
            }
        }
        else
            proxyScriptStartExecPoint = proxyScriptExecPoint;
    }
    else if (state != sccsFatalError) // theoretically should never happen (saved scripts are validated)
    {
        if (proxyScriptLowMemory)
        {
            fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
            state = sccsFatalError;
        }
        else
        {
            if (FtpStoreProtocolBytes(proxyScriptError, directErrorText))
            {
                opFatalError = -1; // error is directly in directErrorText
                opFatalErrorTextID = IDS_ERRINPROXYSCRIPT;
                state = sccsOperationFatalError;
            }
            else
            {
                fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                state = sccsFatalError;
            }
        }
    }

    if (state != sccsFatalError && state != sccsOperationFatalError &&
        !host.empty() && !FtpEncodeNetworkHost(host.c_str(), hostBytes))
    {
        fatalErrorTextID = IDS_PRXSCRERR_INVHOSTORPORT;
        state = sccsFatalError;
    }

RETRY_LABEL:

    // On the very first pass this would wipe the "AUTH TLS\r\n" command staged
    // above before the state machine ever runs, and nothing refills it: for an
    // FTPS bookmark on a fresh connect, sccsProcessLoginScript short-circuits
    // ProcessProxyScript while an SSL sub-sequence is pending, so the handshake
    // was never sent - the connection died with a bogus "incomplete proxy
    // script" error, or (compression enabled) sent MODE Z on a still-plaintext
    // socket. On every later pass (the retry goto), the buffer genuinely is
    // stale from the previous attempt and this wipe is exactly the hygiene it
    // was added for.
    if (haveEnteredStateMachine)
        FTPSecureWipe(proxySendCmdBuf);
    haveEnteredStateMachine = true;

    while (state != sccsDone)
    {
        CALL_STACK_MESSAGE2("state = %d", state); // so we can see where it eventually crashed/froze
        switch (state)
        {
        case sccsGetIP: // obtain an IP address from the textual address of the FTP server
        {
            if (!GetHostByAddress(host.c_str(), 0)) // must be outside the SocketCritSect section
            {                               // no chance of success -> report an error
                if (FTPFormatString(operationFormatText, LoadStr(IDS_GETIPERROR), hostBytes.c_str()))
                {
                    opFatalErrorTextID = -1; // the text is in operationFormatText
                    opFatalError = NO_ERROR; // unknown error
                    state = sccsOperationFatalError;
                }
                else
                {
                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                    state = sccsFatalError;
                }
            }
            else
            {
                try
                {
                    waitText = SPLFormatStringOwned(LangStr(IDS_GETTINGIPOFSERVER).c_str(),
                                                    host.c_str());
                }
                catch (...)
                {
                    waitText = LangStr(IDS_OPERDOPPR_LOWMEM).c_str();
                }
                waitWnd.SetText(waitText.c_str());
                waitWnd.Create(GetWaitTime(showWaitWndTime));

                DWORD start = GetTickCount();
                while (state == sccsGetIP)
                {
                    // wait for an event on the socket (receiving the resolved IP address) or ESC
                    CControlConnectionSocketEvent event;
                    DWORD data1, data2;
                    DWORD now = GetTickCount();
                    if (now - start > (DWORD)serverTimeout)
                        now = start + (DWORD)serverTimeout;
                    WaitForEventOrESC(parent, &event, &data1, &data2, serverTimeout - (now - start),
                                      &waitWnd, NULL, FALSE);
                    switch (event)
                    {
                    case ccsevESC:
                    {
                        waitWnd.Show(FALSE);
                        if (SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_GETIPESC).c_str(),
                                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                             MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                                                 MB_ICONQUESTION) == IDYES)
                        { // cancel
                            state = sccsDone;
                            actionCanceled = TRUE;
                        }
                        else
                        {
                            SalamanderGeneral->WaitForESCRelease(); // measure to prevent interrupting the next action after every ESC in the previous message box
                            waitWnd.Show(TRUE);
                        }
                        break;
                    }

                    case ccsevTimeout:
                    {
                        fatalErrorTextID = IDS_GETIPTIMEOUT;
                        state = sccsFatalError;
                        break;
                    }

                    case ccsevIPReceived: // data1 == IP, data2 == error
                    {
                        if (data1 != INADDR_NONE) // we have an IP
                        {
                            HANDLES(EnterCriticalSection(&SocketCritSect));
                            auxServerIP = ServerIP = data1;
                            HANDLES(LeaveCriticalSection(&SocketCritSect));

                            state = sccsConnect;
                        }
                        else // error
                        {
                            if (FTPFormatString(operationFormatText, LoadStr(IDS_GETIPERROR), hostBytes.c_str()))
                            {
                                opFatalErrorTextID = -1; // the text is in operationFormatText
                                opFatalError = data2;
                                state = sccsOperationFatalError;
                            }
                            else
                            {
                                fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                state = sccsFatalError;
                            }
                        }
                        break;
                    }

                    default:
                        TRACE_E("Unexpected event = " << event);
                        break;
                    }
                }
                waitWnd.Destroy();
            }
            break;
        }

        case sccsConnect: // connect to the FTP server (retrieved IP is in 'auxServerIP')
        {
            srvAddr.s_addr = auxServerIP;

            // A new socket starts in the explicit legacy fallback. UTF-8 is
            // enabled only after this session accepts OPTS UTF8 ON.
            HANDLES(EnterCriticalSection(&SocketCritSect));
            TextPolicy.ResetForConnection();
            HANDLES(LeaveCriticalSection(&SocketCritSect));

            SYSTEMTIME st;
            GetLocalTime(&st);
            std::string dateText;
            std::string timeText;
            std::string timestamp;
            if (!GetLocaleDateTimePart(st, TRUE, dateText) ||
                !GetLocaleDateTimePart(st, FALSE, timeText) ||
                !FTPFormatString(timestamp, "%s - %s", dateText.c_str(), timeText.c_str()))
            {
                fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                state = sccsFatalError;
                break;
            }

            HANDLES(EnterCriticalSection(&SocketCritSect));
            std::string connectionLogHeader;
            std::string logUser;
            std::string logProxyUser;
            std::string configuredHostForLog;
            std::string proxyHostForLog;
            BOOL headerReady = FtpEncodeNetworkHost(Host.c_str(), configuredHostForLog) &&
                               (proxyScriptParams.ProxyHost.empty() ||
                                FtpEncodeNetworkHost(proxyScriptParams.ProxyHost.c_str(), proxyHostForLog)) &&
                               TextPolicy.GetCodec().Encode(User.c_str(), User.size(), logUser) &&
                               TextPolicy.GetCodec().Encode(proxyScriptParams.ProxyUser.c_str(),
                                                           proxyScriptParams.ProxyUser.size(), logProxyUser);

            // create a log and insert the header into the log
            if (!reconnect && LogUID == -1) // we do not have a log yet
            {
                if (Config.EnableLogging)
                    Logs.CreateLog(&LogUID, Host.c_str(), Port, User.c_str(), this, FALSE, FALSE);
                if (ProxyServer != NULL)
                {
                    std::string proxyNameForLog;
                    std::string proxyTypeName;
                    headerReady = headerReady &&
                                  FtpEncodeLocalTextForByteLog(ProxyServer->ProxyName.c_str(),
                                                               "<Unicode proxy profile>", proxyNameForLog) &&
                                  GetProxyTypeName(ProxyServer->ProxyType, proxyTypeName) &&
                                  FTPFormatString(connectionLogHeader, LoadStr(IDS_PRXSRVLOGHEADER), configuredHostForLog.c_str(), Port, logUser.c_str(),
                                                   proxyNameForLog.c_str(), proxyTypeName.c_str(),
                                                   proxyHostForLog.c_str(), proxyScriptParams.ProxyPort,
                                                   logProxyUser.c_str(), hostBytes.c_str(), inet_ntoa(srvAddr), port, LogUID, timestamp.c_str());
                }
                else
                {
                    headerReady = headerReady && FTPFormatString(connectionLogHeader, LoadStr(IDS_LOGHEADER), configuredHostForLog.c_str(), inet_ntoa(srvAddr), Port, LogUID, timestamp.c_str());
                }
                if (Config.AlwaysShowLogForActPan &&
                    (!Config.UseConnectionDataFromConfig || !Config.ChangingPathInInactivePanel))
                {
                    Logs.ActivateLog(LogUID);
                }
            }
            else
            {
                if (ProxyServer != NULL)
                {
                    std::string proxyNameForLog;
                    std::string proxyTypeName;
                    headerReady = headerReady &&
                                  FtpEncodeLocalTextForByteLog(ProxyServer->ProxyName.c_str(),
                                                               "<Unicode proxy profile>", proxyNameForLog) &&
                                  GetProxyTypeName(ProxyServer->ProxyType, proxyTypeName) &&
                                  FTPFormatString(connectionLogHeader, LoadStr(IDS_PRXSRVRECONLOGHEADER), configuredHostForLog.c_str(), Port, logUser.c_str(),
                                                   proxyNameForLog.c_str(), proxyTypeName.c_str(),
                                                   proxyHostForLog.c_str(), proxyScriptParams.ProxyPort,
                                                  logProxyUser.c_str(), hostBytes.c_str(), inet_ntoa(srvAddr), port, attemptNum, timestamp.c_str());
                }
                else
                {
                    headerReady = headerReady && FTPFormatString(connectionLogHeader, LoadStr(IDS_RECONLOGHEADER), configuredHostForLog.c_str(), inet_ntoa(srvAddr), Port, attemptNum, timestamp.c_str());
                }
            }
            logUID = LogUID;

            HANDLES(LeaveCriticalSection(&SocketCritSect));

            if (!headerReady)
            {
                fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                state = sccsFatalError;
                break;
            }

            // add the header - Host + Port + IP + User
            Logs.LogMessage(logUID, connectionLogHeader.c_str(), -1);

            ResetBuffersAndEvents(); // empty the buffers (discard old data) and discard old events
            if (useWelcomeMessage)
                welcomeMessage.Clear(); // clear the previous attempt (only makes sense after a "retry")

            if ((proxyType == fpstSocks5 || proxyType == fpstHTTP1_1) &&
                !proxyScriptParams.ProxyUser.empty() && proxyScriptParams.ProxyPassword.empty())
            { // the user should enter the proxy password
                try
                {
                    connectingToAs = SPLFormatStringOwned(
                        LangStr(IDS_CONNECTINGTOAS2).c_str(),
                        proxyScriptParams.ProxyHost.c_str(),
                        proxyScriptParams.ProxyUser.c_str());
                }
                catch (...)
                {
                    connectingToAs.clear();
                }
                if (CEnterStrDlg(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ENTERPRXPASSTITLE).c_str(),
                                 SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ENTERPRXPASSTEXT).c_str(),
                                 proxyScriptParams.ProxyPassword, TRUE,
                                 connectingToAs.c_str(), FALSE)
                        .Execute() != IDCANCEL)
                { // value change -> we must update the originals as well
                    HANDLES(EnterCriticalSection(&SocketCritSect));
                    BOOL stored = ProxyServer == NULL ||
                                  ProxyServer->SetProxyPassword(proxyScriptParams.ProxyPassword.c_str());
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    if (!stored)
                    {
                        fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                        state = sccsFatalError;
                        break;
                    }
                }
            }

            DWORD error = ERROR_NO_UNICODE_TRANSLATION;
            BOOL conRes = ConnectWithProxy(auxServerIP, port, proxyType,
                                           &error, proxyScriptParams.Host.c_str(), proxyScriptParams.Port,
                                           proxyScriptParams.ProxyUser.c_str(),
                                           proxyScriptParams.ProxyPassword.c_str(), INADDR_NONE);
            Logs.SetIsConnected(logUID, IsConnected());
            Logs.RefreshListOfLogsInLogsDlg();
            if (conRes)
            {
                std::wstring srvAddress;
                FTPFormatIPv4Address(srvAddr.s_addr, srvAddress);
                try
                {
                    if (proxyType == fpstNotUsed)
                        waitText = SPLFormatStringOwned(
                            LangStr(IDS_OPENINGCONTOSERVER).c_str(), host.c_str(),
                            srvAddress.empty() ? L"?" : srvAddress.c_str(), port);
                    else
                        waitText = SPLFormatStringOwned(
                            LangStr(IDS_OPENINGCONTOSERVER2).c_str(),
                            proxyScriptParams.Host.c_str(), proxyScriptParams.Port);
                }
                catch (...)
                {
                    waitText = LangStr(IDS_OPERDOPPR_LOWMEM).c_str();
                }
                waitWnd.SetText(waitText.c_str());
                waitWnd.Create(GetWaitTime(showWaitWndTime));

                DWORD start = GetTickCount();
                while (state == sccsConnect)
                {
                    // wait for an event on the socket (opening the connection to the server) or ESC
                    CControlConnectionSocketEvent event;
                    DWORD data1, data2;
                    DWORD now = GetTickCount();
                    if (now - start > (DWORD)serverTimeout)
                        now = start + (DWORD)serverTimeout;
                    WaitForEventOrESC(parent, &event, &data1, &data2, serverTimeout - (now - start),
                                      &waitWnd, NULL, FALSE);
                    switch (event)
                    {
                    case ccsevESC:
                    {
                        waitWnd.Show(FALSE);
                        if (SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_OPENCONESC).c_str(),
                                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                             MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                                                 MB_ICONQUESTION) == IDYES)
                        { // cancel
                            state = sccsDone;
                            actionCanceled = TRUE;
                        }
                        else
                        {
                            SalamanderGeneral->WaitForESCRelease(); // measure to prevent interrupting the next action after every ESC in the previous message box
                            waitWnd.Show(TRUE);
                        }
                        break;
                    }

                    case ccsevTimeout:
                    {
                        std::string timeoutText;
                        if (GetProxyTimeoutDescr(timeoutText))
                        {
                            if (FtpStoreProtocolBytes(timeoutText, directErrorText))
                                fatalErrorTextID = -1;
                            else
                                fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                        }
                        else
                            fatalErrorTextID = IDS_OPENCONTIMEOUT;
                        noRetryState = sccsFatalError; // if retry is not performed, execute sccsFatalError
                        state = sccsRetry;
                        break;
                    }

                    case ccsevConnected: // data1 == error
                    {
                        if (data1 == NO_ERROR)
                            state = sccsServerReady; // we are connected
                        else                         // error
                        {
                            if (GetProxyError(proxyErrorText, &proxyErrorFormat, FALSE))
                            {
                                directErrorText.swap(proxyErrorText);
                                operationFormatText.swap(proxyErrorFormat);
                                opFatalError = -1; // error detail is directly in directErrorText
                            }
                            else
                            {
                                opFatalError = data1;
                                if (!FTPFormatString(operationFormatText, LoadStr(IDS_OPENCONERROR),
                                                     hostBytes.c_str(), inet_ntoa(srvAddr), port))
                                {
                                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                    state = sccsFatalError;
                                    break;
                                }
                            }
                            opFatalErrorTextID = -1;                  // the text is in operationFormatText
                            noRetryState = sccsOperationFatalError;   // if retry is not performed, execute sccsOperationFatalError
                            if (opFatalError == -1 && directErrorText.empty()) // simple error -> convert it to sccsFatalError
                            {
                                fatalErrorTextID = -1;
                                directErrorText.swap(operationFormatText);
                                noRetryState = sccsFatalError; // if retry is not performed, execute sccsFatalError
                            }
                            state = sccsRetry;
                        }
                        break;
                    }

                    default:
                        TRACE_E("Unexpected event = " << event);
                        break;
                    }
                }

                waitWnd.Destroy();
            }
            else
            {
                opFatalError = error;
                if (FTPFormatString(operationFormatText, LoadStr(IDS_OPENCONERROR),
                                    hostBytes.c_str(), inet_ntoa(srvAddr), port))
                {
                    opFatalErrorTextID = -1;                // the text is in operationFormatText
                    noRetryState = sccsOperationFatalError; // if retry is not performed, execute sccsOperationFatalError
                    state = sccsRetry;
                }
                else
                {
                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                    state = sccsFatalError;
                }
            }
            break;
        }

        case sccsRetry: // try connecting again (based on Config.DelayBetweenConRetries + Config.ConnectRetries),
        {               // if it should no longer try, transition to the state from 'noRetryState' + if 'fastRetry' is TRUE,
                        // do not wait before the next attempt
            // Resend AUTH TLS if needed
            if (EncryptControlConnection)
            {
                SSLInitSequence = sslisAUTH;
                if (!setCommandPair("AUTH TLS\r\n"))
                {
                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                    state = sccsFatalError;
                    break;
                }
            }
            bModeZSent = false;

            std::string retryErrorText;
            switch (noRetryState) // text of the last error for the log
            {
            case sccsFatalError:
            {
                if (!GetFatalErrorText(fatalErrorTextID, directErrorText, retryErrorText))
                    retryErrorText = LoadStr(IDS_OPERDOPPR_LOWMEM);
                break;
            }

            case sccsOperationFatalError:
            {
                if (!GetOperationFatalErrorText(opFatalError, directErrorText, retryErrorText))
                    retryErrorText = LoadStr(IDS_OPERDOPPR_LOWMEM);
                break;
            }

            default:
            {
                retryErrorText.clear();
                TRACE_E("CControlConnectionSocket::StartControlConnection(): Unexpected value "
                        "of 'noRetryState': "
                        << noRetryState);
                break;
            }
            }
            TrimLineEnds(retryErrorText);
            if (retryLogError)
            {
                std::string logMessage;
                if (FTPFormatString(logMessage, "%s\r\n", retryErrorText.c_str()))
                    Logs.LogMessage(logUID, logMessage.c_str(), -1, TRUE);
            }
            retryLogError = TRUE;

            // when the server closes the control connection, keep-alive changes to 'kamNone', therefore:
            // prepare keep-alive for further use + set keep-alive to 'kamForbidden' (a normal command is in progress)
            ReleaseKeepAlive();
            WaitForEndOfKeepAlive(parent, 0); // cannot open the wait window (it is in the 'kamNone' state)

            if (fastRetry) // this "retry" may be due to user delay - we will not wait, it might succeed (ftp.novell.com - kills after 20s)
            {
                fastRetry = FALSE;

                // if necessary, close the socket; the system will attempt a graceful shutdown (we will not learn the result)
                CloseSocket(NULL);
                Logs.SetIsConnected(logUID, IsConnected());
                Logs.RefreshListOfLogsInLogsDlg(); // display "connection inactive"

                state = sccsConnect; // try to connect again (the IP is already known)
            }
            else
            {
                if (attemptNum < Config.GetConnectRetries() + 1) // try to connect again, but wait first
                {
                    attemptNum++; // increase the connection attempt number
                    if (totalAttemptNum != NULL)
                        *totalAttemptNum = attemptNum; // store the total number of attempts

                    // if necessary, close the socket; the system will attempt a graceful shutdown (we will not learn the result)
                    CloseSocket(NULL);
                    Logs.SetIsConnected(logUID, IsConnected());
                    Logs.RefreshListOfLogsInLogsDlg(); // display "connection inactive"

                    std::wstring srvAddress;
                    FTPFormatIPv4Address(srvAddr.s_addr, srvAddress);
                    std::wstring retryErrorTextW;
                    if (!FtpDecodeLocalText(retryErrorText, retryErrorTextW))
                        retryErrorTextW = L"<invalid local error text>";
                    std::wstring waitText;
                    const int delayBetweenConRetries = Config.GetDelayBetweenConRetries();
                    try
                    {
                        if (noRetryState == sccsFatalError)
                        {
                            if (proxyType == fpstNotUsed)
                                waitText = SPLFormatStringOwned(
                                    LangStr(IDS_WAITINGTORETRY).c_str(), host.c_str(),
                                    srvAddress.empty() ? L"?" : srvAddress.c_str(), port, retryErrorTextW.c_str());
                            else
                                waitText = SPLFormatStringOwned(
                                    LangStr(IDS_WAITINGTORETRY2).c_str(),
                                    proxyScriptParams.Host.c_str(),
                                    proxyScriptParams.Port, retryErrorTextW.c_str());
                        }
                        else
                        {
                            std::wstring format;
                            if (opFatalErrorTextID != -1)
                                format = LangStr(opFatalErrorTextID);
                            else if (!FtpDecodeLocalText(operationFormatText, format))
                                format = LangStr(IDS_OPERDOPPR_LOWMEM);
                            waitText = SPLFormatStringOwned(format.c_str(), retryErrorTextW.c_str());
                            const size_t firstLine = waitText.find(L'\n');
                            if (firstLine != std::wstring::npos && firstLine + 1 < waitText.size() &&
                                waitText[firstLine + 1] == L'\n')
                                waitText.erase(firstLine, 1);
                            while (!waitText.empty() &&
                                   (waitText.back() == L'\n' || waitText.back() == L'\r'))
                                waitText.pop_back();
                        }

                        const std::wstring retryText = SPLFormatStringOwned(
                            LangStr(IDS_WAITINGTORETRYSUF).c_str(), waitText.c_str(),
                            delayBetweenConRetries, attemptNum, Config.GetConnectRetries() + 1);
                        waitWnd.SetText(retryText.c_str());
                    }
                    catch (...)
                    {
                        waitWnd.SetText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str());
                    }
                    waitWnd.Create(0);

                    // wait for ESC or the waiting timeout
                    CControlConnectionSocketEvent event;
                    DWORD data1, data2;
                    BOOL run = TRUE;
                    DWORD start = GetTickCount();
                    while (run)
                    {
                        DWORD now = GetTickCount();
                        if (now - start < (DWORD)delayBetweenConRetries * 1000)
                        { // rebuild the text for the retry wait window (contains the countdown)
                            DWORD wait = delayBetweenConRetries * 1000 - (now - start);
                            if (now != start) // it makes no sense the first time
                            {
                                try
                                {
                                    const std::wstring retryText = SPLFormatStringOwned(
                                        LangStr(IDS_WAITINGTORETRYSUF).c_str(), waitText.c_str(),
                                        (1 + (wait - 1) / 1000), attemptNum,
                                        Config.GetConnectRetries() + 1);
                                    waitWnd.SetText(retryText.c_str());
                                }
                                catch (...)
                                {
                                    waitWnd.SetText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str());
                                }
                            }
                            BOOL notTimeout = FALSE; // TRUE = it cannot be a timeout
                            if (wait > 1500)
                            {
                                wait = 1000; // wait at most 1.5 seconds, at least 0.5 seconds
                                notTimeout = TRUE;
                            }
                            WaitForEventOrESC(parent, &event, &data1, &data2,
                                              wait, &waitWnd, NULL, FALSE);
                            switch (event) // we only care about ESC or timeout, ignore events
                            {
                            case ccsevESC:
                            {
                                waitWnd.Show(FALSE);
                                MSGBOXEX_PARAMS params;
                                const std::wstring caption = LangStr(IDS_FTPPLUGINTITLE);
                                const std::wstring text = LangStr(IDS_WAITRETRESC);
                                memset(&params, 0, sizeof(params));
                                params.HParent = parent;
                                params.Flags = MB_YESNOCANCEL | MB_ICONQUESTION;
                                params.Caption = caption.c_str();
                                params.Text = text.c_str();
                                /* used by the script export_mnu.py, which generates salmenu.mnu for Translator
   we let the message box buttons resolve hotkey collisions by simulating that this is a menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] =
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_WAITRETRESCABORTBTN
  {MNTT_IT, IDS_WAITRETRESCRETRYBTN
  {MNTT_IT, IDS_WAITRETRESCWAITBTN
  {MNTT_PE, 0
};
*/
                                const std::wstring aliasBtnNames = SPLFormatStringOwned(
                                    L"%d\t%s\t%d\t%s\t%d\t%s",
                                    DIALOG_YES, LangStr(IDS_WAITRETRESCABORTBTN).c_str(),
                                    DIALOG_NO, LangStr(IDS_WAITRETRESCRETRYBTN).c_str(),
                                    DIALOG_CANCEL, LangStr(IDS_WAITRETRESCWAITBTN).c_str());
                                params.AliasBtnNames = aliasBtnNames.c_str();
                                int msgRes = SalamanderGeneral->SalMessageBoxEx(&params);
                                if (msgRes == IDYES)
                                { // gives up further login attempts
                                    state = sccsDone;
                                    run = FALSE;
                                    // actionCanceled = TRUE;   // do not log cancel in "retry"
                                }
                                else
                                {
                                    if (msgRes == IDNO)
                                    {
                                        event = ccsevTimeout;
                                        run = FALSE; // immediately try the next login
                                    }
                                    else
                                    {
                                        SalamanderGeneral->WaitForESCRelease(); // measure to prevent interrupting the next action after every ESC in the previous message box
                                        waitWnd.Show(TRUE);                     // wants to continue waiting
                                    }
                                }
                                break;
                            }

                            case ccsevTimeout:
                                if (!notTimeout)
                                    run = FALSE;
                                break;
                            }
                        }
                        else // we will not try it anymore (timeout)
                        {
                            event = ccsevTimeout;
                            run = FALSE;
                        }
                    }

                    waitWnd.Destroy();

                    if (event == ccsevTimeout)
                        state = sccsConnect; // try to connect again (the IP is already known)
                }
                else
                {
                    if (noRetryState == sccsFatalError || noRetryState == sccsOperationFatalError)
                        fatalErrLogMsg = FALSE; // the message was already logged (if needed), do not log it again
                    state = noRetryState;       // no more attempts, continue with the state from 'noRetryState'
                }
            }
            break;
        }

        case sccsServerReady: // now connected, read the message from the server (expect "220 Service ready for new user")
        {
            waitWnd.SetText(LangStr(IDS_WAITINGFORLOGIN).c_str());
            waitWnd.Create(GetWaitTime(showWaitWndTime));

            DWORD start = GetTickCount();
            while (state == sccsServerReady)
            {
                // wait for an event on the socket (server reply) or ESC
                CControlConnectionSocketEvent event;
                DWORD data1, data2;
                DWORD now = GetTickCount();
                if (now - start > (DWORD)serverTimeout)
                    now = start + (DWORD)serverTimeout;
                WaitForEventOrESC(parent, &event, &data1, &data2, serverTimeout - (now - start),
                                  &waitWnd, NULL, FALSE);
                switch (event)
                {
                case ccsevESC:
                {
                    waitWnd.Show(FALSE);
                    if (SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_WAITFORLOGESC).c_str(),
                                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                         MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                                             MB_ICONQUESTION) == IDYES)
                    { // cancel
                        state = sccsDone;
                        actionCanceled = TRUE;
                    }
                    else
                    {
                        SalamanderGeneral->WaitForESCRelease(); // measure to prevent interrupting the next action after every ESC in the previous message box
                        waitWnd.Show(TRUE);
                    }
                    break;
                }

                case ccsevTimeout:
                {
                    fatalErrorTextID = IDS_WAITFORLOGTIMEOUT;
                    noRetryState = sccsFatalError; // if retry is not performed, execute sccsFatalError
                    state = sccsRetry;
                    break;
                }

                case ccsevClosed:       // possible unexpected connection loss (also handle that ccsevClosed could overwrite ccsevNewBytesRead)
                case ccsevNewBytesRead: // read new bytes
                {
                    char* reply;
                    int replySize;
                    int replyCode;

                    HANDLES(EnterCriticalSection(&SocketCritSect));
                    while (ReadFTPReply(&reply, &replySize, &replyCode)) // while we have some server response
                    {
                        if (useWelcomeMessage)
                            welcomeMessage.Append(reply, replySize);
                        Logs.LogServerMessage(logUID, reply, replySize, TextPolicy);

                        if (replyCode != -1)
                        {
                            if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS &&
                                FTP_DIGIT_2(replyCode) == FTP_D2_CONNECTION) // e.g. 220 - Service ready for new user
                            {
                                if (event != ccsevClosed) // if the connection is not closed yet
                                {
                                    std::string stagedFirstReply;
                                    if (FtpStoreProtocolBytes(
                                            std::string_view(reply, static_cast<size_t>(replySize)),
                                            stagedFirstReply))
                                    {
                                        ServerFirstReply.swap(stagedFirstReply); // source of server version info
                                        state = sccsStartLoginScript;            // send the login command sequence
                                    }
                                    else
                                    {
                                        fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                        state = sccsFatalError;
                                    }

                                    SkipFTPReply(replySize);
                                    break;
                                }
                            }
                            else
                            {
                                if (FTP_DIGIT_1(replyCode) == FTP_D1_MAYBESUCCESS &&
                                    FTP_DIGIT_2(replyCode) == FTP_D2_CONNECTION) // e.g. 120 - Service ready in nnn minutes
                                {                                                // ignore this notification (we have only one timeout)
                                }
                                else
                                {
                                    if (FTP_DIGIT_1(replyCode) == FTP_D1_TRANSIENTERROR ||
                                        FTP_DIGIT_1(replyCode) == FTP_D1_ERROR) // e.g. 421 Service not available, closing control connection
                                    {
                                        if (state == sccsServerReady) // if we are not reporting another error yet
                                        {
                                            if (FtpStoreProtocolBytes(
                                                    std::string_view(reply, static_cast<size_t>(replySize)),
                                                    directErrorText))
                                            {
                                                fatalErrorTextID = -1;         // the error text is in directErrorText
                                                noRetryState = sccsFatalError; // if retry is not performed, execute sccsFatalError
                                                retryLogError = FALSE;         // the error is already in the log, do not add it again
                                                state = sccsRetry;
                                            }
                                            else
                                            {
                                                fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                                state = sccsFatalError;
                                            }
                                        }
                                    }
                                    else // unexpected response, ignore it
                                    {
                                        std::string unexpectedReply;
                                        if (FtpStoreProtocolBytes(
                                                std::string_view(reply, static_cast<size_t>(replySize)),
                                                unexpectedReply))
                                            TRACE_E("Unexpected reply: " << unexpectedReply.c_str());
                                    }
                                }
                            }
                        }
                        else // not an FTP server
                        {
                            if (state == sccsServerReady) // if we are not reporting another error yet
                            {
                                opFatalErrorTextID = IDS_NOTFTPSERVERERROR;
                                if (FtpStoreProtocolBytes(
                                        std::string_view(reply, static_cast<size_t>(replySize)),
                                        directErrorText))
                                {
                                    opFatalError = -1; // the error is directly in directErrorText
                                    state = sccsOperationFatalError;
                                }
                                else
                                {
                                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                    state = sccsFatalError;
                                }
                                fatalErrLogMsg = FALSE; // already in the log, no point adding it again
                            }
                        }
                        SkipFTPReply(replySize);
                    }
                    HANDLES(LeaveCriticalSection(&SocketCritSect));

                    if (event == ccsevClosed)
                    {
                        if (state == sccsServerReady) // close without a reason
                        {
                            fatalErrorTextID = IDS_CONNECTIONLOSTERROR;
                            noRetryState = sccsFatalError; // if retry is not performed, execute sccsFatalError
                            state = sccsRetry;
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

            waitWnd.Destroy();
            break;
        }

        case sccsRetryLogin: // retry login (without waiting and losing the connection) - only transitions to sccsStartLoginScript - cannot be used with a proxy server!!!
        {
            state = sccsStartLoginScript;
            break;
        }

        case sccsStartLoginScript: // initialize sending of the login command sequence - according to the proxy server script
        {
            if (proxyScriptStartExecPoint == NULL)
                TRACE_E("CControlConnectionSocket::StartControlConnection(): proxyScriptStartExecPoint cannot be NULL here!");
            proxyScriptExecPoint = proxyScriptStartExecPoint;
            proxyLastCmdReply = -1;
            proxyLastCmdReplyText.clear();
            state = sccsProcessLoginScript;
            break;
        }

        case sccsProcessLoginScript: // gradually send the login command sequence - according to the proxy server script
        {
            while (1)
            {
                proxyScriptLowMemory = FALSE;
                if (sslisNone != SSLInitSequence ||
                    ProcessProxyScript(TextPolicy.GetCodec(), proxyScriptText.c_str(), &proxyScriptExecPoint, proxyLastCmdReply,
                                       &proxyScriptParams, NULL, NULL, &proxySendCmdBuf,
                                       &proxyLogCmdBuf, &proxyScriptError, NULL, &proxyScriptLowMemory))
                {
                    if (sslisNone == SSLInitSequence && proxyScriptParams.NeedUserInput()) // it is necessary to enter some data (user, password, etc.)
                    {
                        if (proxyScriptParams.NeedProxyHost)
                        {
                        ENTER_PROXYHOST_AGAIN:
                            std::wstring proxyHostW = proxyScriptParams.ProxyHost;
                            if (CEnterStrDlg(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ENTERPRXHOSTTITLE).c_str(),
                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ENTERPRXHOSTTEXT).c_str(),
                                             proxyHostW, FALSE, NULL, FALSE)
                                    .Execute() == IDCANCEL)
                            {
                                state = sccsDone; // user canceled -> finish
                                actionCanceled = TRUE;
                                break;
                            }
                            else // we have entered "proxyhost:port"
                            {
                                wchar_t* s = wcschr(proxyHostW.data(), L':');
                                int portNum = 0;
                                if (s != NULL) // the port is also specified
                                {
                                    wchar_t* hostEnd = s++;
                                    while (*s != 0 && *s >= L'0' && *s <= L'9')
                                    {
                                        portNum = 10 * portNum + (*s - L'0');
                                        s++;
                                    }
                                    if (*s != 0 || portNum < 1 || portNum > 65535)
                                    {
                                        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PORTISUSHORT).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                                         MB_OK | MB_ICONEXCLAMATION);
                                        goto ENTER_PROXYHOST_AGAIN;
                                    }
                                    *hostEnd = 0;
                                    proxyScriptParams.ProxyPort = portNum;
                                }
                                if (proxyHostW[0] == 0 ||
                                    !FtpStoreWideText(proxyHostW.c_str(), proxyScriptParams.ProxyHost))
                                {
                                    SalamanderGeneral->SalMessageBox(
                                        parent, LangStr(IDS_PRXSCRERR_HOSTEMPTY).c_str(),
                                        LangStr(IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
                                    goto ENTER_PROXYHOST_AGAIN;
                                }

                                HANDLES(EnterCriticalSection(&SocketCritSect));
                                if (ProxyServer != NULL)
                                {
                                    ProxyServer->SetProxyHost(proxyScriptParams.ProxyHost.c_str());
                                    if (portNum != 0)
                                        ProxyServer->SetProxyPort(portNum);
                                }
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                            }
                        }
                        if (proxyScriptParams.NeedProxyPassword)
                        {
                            try
                            {
                                connectingToAs = SPLFormatStringOwned(
                                    LangStr(IDS_CONNECTINGTOAS2).c_str(),
                                    proxyScriptParams.ProxyHost.c_str(),
                                    proxyScriptParams.ProxyUser.c_str());
                            }
                            catch (...)
                            {
                                connectingToAs.clear();
                            }
                            if (CEnterStrDlg(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ENTERPRXPASSTITLE).c_str(),
                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ENTERPRXPASSTEXT).c_str(),
                                             proxyScriptParams.ProxyPassword, TRUE,
                                             connectingToAs.c_str(), FALSE)
                                    .Execute() == IDCANCEL)
                            {
                                state = sccsDone; // user canceled -> finish
                                actionCanceled = TRUE;
                                break;
                            }
                            else // values changed -> we must update the originals
                            {
                                HANDLES(EnterCriticalSection(&SocketCritSect));
                                BOOL stored = ProxyServer == NULL ||
                                              ProxyServer->SetProxyPassword(proxyScriptParams.ProxyPassword.c_str());
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                                if (!stored)
                                {
                                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                    state = sccsFatalError;
                                    break;
                                }
                            }
                        }
                        if (proxyScriptParams.NeedUser)
                        {
                            try
                            {
                                connectingToAs = SPLFormatStringOwned(
                                    LangStr(IDS_CONNECTINGTOAS1).c_str(),
                                    proxyScriptParams.Host.c_str());
                            }
                            catch (...)
                            {
                                connectingToAs.clear();
                            }
                            if (CEnterStrDlg(parent, NULL, NULL, proxyScriptParams.User, FALSE,
                                           connectingToAs.c_str(), FALSE)
                                    .Execute() == IDCANCEL)
                            {
                                state = sccsDone; // user canceled -> finish
                                actionCanceled = TRUE;
                                break;
                            }
                            else // values changed -> we must update the originals
                            {
                                std::wstring stagedUser;
                                std::wstring returnedUser;
                                if (!FtpStoreWideText(proxyScriptParams.User, stagedUser) ||
                                    !FtpStoreWideText(proxyScriptParams.User, returnedUser))
                                {
                                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                    state = sccsFatalError;
                                    break;
                                }
                                HANDLES(EnterCriticalSection(&SocketCritSect));
                                User.swap(stagedUser);
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                                user.swap(returnedUser);
                                Logs.ChangeUser(logUID, proxyScriptParams.User.c_str());
                            }
                        }
                        if (proxyScriptParams.NeedPassword)
                        {
                            try
                            {
                                connectingToAs = SPLFormatStringOwned(
                                    LangStr(IDS_CONNECTINGTOAS2).c_str(),
                                    proxyScriptParams.Host.c_str(),
                                    proxyScriptParams.User.c_str());
                            }
                            catch (...)
                            {
                                connectingToAs.clear();
                            }
                            if (CEnterStrDlg(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ENTERPASSTITLE).c_str(),
                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ENTERPASSTEXT).c_str(),
                                             proxyScriptParams.Password, TRUE,
                                             connectingToAs.c_str(), TRUE)
                                    .Execute() == IDCANCEL)
                            {
                                state = sccsDone; // user canceled -> finish
                                actionCanceled = TRUE;
                                break;
                            }
                            else // values changed -> we must update the originals
                            {
                                if (proxyScriptParams.Password.empty())
                                    proxyScriptParams.AllowEmptyPassword = TRUE; // empty password at the user's request (we will not ask again)
                                std::wstring stagedPassword;
                                if (!FtpStoreWideText(proxyScriptParams.Password, stagedPassword))
                                {
                                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                    state = sccsFatalError;
                                    break;
                                }
                                HANDLES(EnterCriticalSection(&SocketCritSect));
                                Password.swap(stagedPassword);
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                                FTPSecureWipe(stagedPassword);
                            }
                        }
                        if (proxyScriptParams.NeedAccount)
                        {
                            try
                            {
                                connectingToAs = SPLFormatStringOwned(
                                    LangStr(IDS_CONNECTINGTOAS2).c_str(),
                                    proxyScriptParams.Host.c_str(),
                                    proxyScriptParams.User.c_str());
                            }
                            catch (...)
                            {
                                connectingToAs.clear();
                            }
                            if (CEnterStrDlg(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ENTERACCTTITLE).c_str(),
                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ENTERACCTTEXT).c_str(),
                                             proxyScriptParams.Account, TRUE,
                                             connectingToAs.c_str(), FALSE)
                                    .Execute() == IDCANCEL)
                            {
                                state = sccsDone; // user canceled -> finish
                                actionCanceled = TRUE;
                                break;
                            }
                            else // values changed -> we must update the originals
                            {
                                std::wstring stagedAccount;
                                if (!FtpStoreWideText(proxyScriptParams.Account, stagedAccount))
                                {
                                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                    state = sccsFatalError;
                                    break;
                                }
                                HANDLES(EnterCriticalSection(&SocketCritSect));
                                Account.swap(stagedAccount);
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                                FTPSecureWipe(stagedAccount);
                            }
                        }
                        // repaint the main window (so the user does not stare at the remainder after the dialog during the whole connect)
                        UpdateWindow(SalamanderGeneral->GetMainWindowHWND());
                    }
                    else
                    {
                        if (proxySendCmdBuf.empty() && CompressData && !bModeZSent)
                        {
                            if (!setCommandPair("MODE Z\r\n"))
                            {
                                fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                state = sccsFatalError;
                                break;
                            }
                            bModeZSent = true;
                        }
                        if (proxySendCmdBuf.empty()) // end of the login script
                        {
                            if (proxyLastCmdReply == -1) // the script contains no command sent to the server - e.g. commands skipped because they contain optional variables
                            {
                                fatalErrorTextID = IDS_INCOMPLETEPRXSCR2;
                                state = sccsFatalError;
                            }
                            else
                            {
                                if (FTP_DIGIT_1(proxyLastCmdReply) == FTP_D1_SUCCESS) // e.g. 230 User logged in, proceed
                                {
                                    state = sccsDone;
                                    ret = TRUE; // SUCCESS, we are logged in!
                                }
                                else // FTP_DIGIT_1(proxyLastCmdReply) == FTP_D1_PARTIALSUCCESS  // e.g. 331 User name okay, need password
                                {
                                    if (FtpStoreProtocolBytes(proxyLastCmdReplyText, directErrorText))
                                    {
                                        opFatalError = -1; // error is directly in directErrorText
                                        opFatalErrorTextID = IDS_INCOMPLETEPRXSCR;
                                        state = sccsOperationFatalError;
                                    }
                                    else
                                    {
                                        fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                        state = sccsFatalError;
                                    }
                                }
                            }
                        }
                        else // we have a command to send to the server
                        {
                            DWORD error;
                            BOOL allBytesWritten;
                            if (Write(proxySendCmdBuf.c_str(), static_cast<int>(proxySendCmdBuf.size()), &error, &allBytesWritten))
                            {
                                if (useWelcomeMessage)
                                    welcomeMessage.Append(proxyLogCmdBuf.c_str(), static_cast<int>(proxyLogCmdBuf.size()));
                                Logs.LogMessage(logUID, proxyLogCmdBuf.c_str(), static_cast<int>(proxyLogCmdBuf.size()));

                                const size_t firstLineLength = proxyLogCmdBuf.find('\r');
                                const size_t commandTextLength = firstLineLength == std::string::npos ? proxyLogCmdBuf.size() : firstLineLength;
                                std::wstring commandText;
                                if (!TextPolicy.GetCodec().Decode(proxyLogCmdBuf.data(), commandTextLength, commandText))
                                    commandText = L"<invalid command text>";
                                try
                                {
                                    waitText = SPLFormatStringOwned(
                                        LangStr(IDS_SENDINGLOGINCMD).c_str(), commandText.c_str());
                                }
                                catch (...)
                                {
                                    waitText = LangStr(IDS_OPERDOPPR_LOWMEM).c_str();
                                }
                                waitWnd.SetText(waitText.c_str());
                                waitWnd.Create(GetWaitTime(showWaitWndTime));

                                DWORD start = GetTickCount();
                                BOOL replyReceived = FALSE;
                                while (!allBytesWritten || state == sccsProcessLoginScript && !replyReceived)
                                {
                                    // wait for an event on the socket (server reply) or ESC
                                    CControlConnectionSocketEvent event;
                                    DWORD data1, data2;
                                    DWORD now = GetTickCount();
                                    if (now - start > (DWORD)serverTimeout)
                                        now = start + (DWORD)serverTimeout;
                                    WaitForEventOrESC(parent, &event, &data1, &data2, serverTimeout - (now - start),
                                                      &waitWnd, NULL, FALSE);
                                    switch (event)
                                    {
                                    case ccsevESC:
                                    {
                                        waitWnd.Show(FALSE);
                                        if (SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SENDCOMMANDESC2).c_str(),
                                                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                                             MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                                                                 MB_ICONQUESTION) == IDYES)
                                        { // cancel
                                            state = sccsDone;
                                            actionCanceled = TRUE;
                                            allBytesWritten = TRUE; // no longer important, the socket will be closed
                                        }
                                        else
                                        {
                                            SalamanderGeneral->WaitForESCRelease(); // measure to prevent interrupting the next action after every ESC in the previous message box
                                            waitWnd.Show(TRUE);
                                        }
                                        break;
                                    }

                                    case ccsevTimeout:
                                    {
                                        fatalErrorTextID = IDS_SNDORABORCMDTIMEOUT;
                                        noRetryState = sccsFatalError; // if retry is not performed, execute sccsFatalError
                                        state = sccsRetry;
                                        allBytesWritten = TRUE; // no longer important, the socket will be closed
                                        break;
                                    }

                                    case ccsevWriteDone:
                                        allBytesWritten = TRUE; // all bytes have been sent (also handle that ccsevWriteDone could overwrite ccsevNewBytesRead)
                                    case ccsevClosed:           // possible unexpected connection loss (also handle that ccsevClosed could overwrite ccsevNewBytesRead)
                                    case ccsevNewBytesRead:     // read new bytes
                                    {
                                        char* reply;
                                        int replySize;
                                        int replyCode;

                                        HANDLES(EnterCriticalSection(&SocketCritSect));
                                        BOOL sectLeaved = FALSE;
                                        while (ReadFTPReply(&reply, &replySize, &replyCode)) // while we have some server response
                                        {
                                            if (useWelcomeMessage)
                                                welcomeMessage.Append(reply, replySize);
                                            Logs.LogServerMessage(logUID, reply, replySize, TextPolicy);

                                            if (replyCode != -1)
                                            {
                                                if ((FTP_DIGIT_1(replyCode) == FTP_D1_ERROR) && CompressData &&
                                                    proxySendCmdBuf.compare(0, sizeof("MODE Z") - 1, "MODE Z") == 0)
                                                {
                                                    // Server does not support compression -> swallow the error, disable compression and go on
                                                    replyCode = 200; // Emulate Full success
                                                    CompressData = FALSE;
                                                    Logs.LogMessage(logUID, LangStr(IDS_MODEZ_LOG_UNSUPBYSERVER).c_str(), -1);
                                                }
                                                if (FTP_DIGIT_1(replyCode) == FTP_D1_SUCCESS ||      // e.g. 230 User logged in, proceed
                                                    FTP_DIGIT_1(replyCode) == FTP_D1_PARTIALSUCCESS) // e.g. 331 User name okay, need password
                                                {                                                    // we have a successful reply to the command, store it and continue executing the login script
                                                    fastRetry = FALSE;                               // any further "retry" will no longer be due to user delay
                                                    if (replyReceived)
                                                        TRACE_E("CControlConnectionSocket::StartControlConnection(): unexpected situation: more replies to one command!");
                                                    else
                                                    {
                                                        replyReceived = TRUE; // take only the first server reply to the command (another reply probably relates to the control connection state, e.g. "timeout")
                                                        proxyLastCmdReply = replyCode;
                                                        if (!FtpStoreProtocolBytes(
                                                                std::string_view(reply, static_cast<size_t>(replySize)),
                                                                proxyLastCmdReplyText))
                                                        {
                                                            fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                                            state = sccsFatalError;
                                                        }
                                                    }
                                                    if (event == ccsevClosed)
                                                        state = sccsProcessLoginScript; // ensure an error is reported later
                                                }
                                                else
                                                {
                                                    if (FTP_DIGIT_1(replyCode) == FTP_D1_TRANSIENTERROR || // e.g. 421 Service not available (too many users), closing control connection
                                                        FTP_DIGIT_1(replyCode) == FTP_D1_ERROR)            // e.g. 530 Not logged in (invalid password)
                                                    {
                                                        if (FTP_DIGIT_1(replyCode) == FTP_D1_TRANSIENTERROR)
                                                        { // convenient handling of the "too many users" error - no questions, immediately "retry"
                                                            // drawback: with this code comes a message that may require changing user/password
                                                            retryLoginWithoutAsking = TRUE;
                                                        }

                                                        if (!FtpStoreProtocolBytes(
                                                                std::string_view(reply, static_cast<size_t>(replySize)),
                                                                directErrorText))
                                                        {
                                                            fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                                            state = sccsFatalError;
                                                            SkipFTPReply(replySize);
                                                            break;
                                                        }
                                                        SkipFTPReply(replySize);
                                                        if (!retryLoginWithoutAsking)
                                                        {
                                                            fastRetry = TRUE; // user-induced delay, some servers do not like it (ftp.novell.com - kills after 20s)
                                                            BOOL proxyUsed = ProxyServer != NULL;

                                                            HANDLES(LeaveCriticalSection(&SocketCritSect)); // must leave the section - opening a dialog will take a while...
                                                            sectLeaved = TRUE;

                                                            waitWnd.Show(FALSE);
                                                            if (replyCode == 534 && EncryptDataConnection ||
                                                                FTP_DIGIT_1(replyCode) == FTP_D1_ERROR && SSLInitSequence == sslisAUTH)
                                                            {
                                                                // 534 comes after PROT when data connection encryption is requested but not supported
                                                                // 530 comes after AUTH when AUTH is not recognized
                                                                SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, (SSLInitSequence == sslisAUTH) ? IDS_SSL_ERR_CONTRENCUNSUP : IDS_SSL_ERR_DATAENCUNSUP).c_str(),
                                                                                                 SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(), MB_OK | MB_ICONSTOP);
                                                                state = sccsDone;
                                                                allBytesWritten = TRUE;      // no longer important, the socket is going to be closed
                                                                SSLInitSequence = sslisNone; // do not attempt to load OpenSSL libs
                                                            }
                                                            else
                                                            {
                                                                try
                                                                {
                                                                    connectingToAs = SPLFormatStringOwned(
                                                                        LangStr(IDS_CONNECTINGTOAS1).c_str(),
                                                                        proxyScriptParams.Host.c_str());
                                                                }
                                                                catch (...)
                                                                {
                                                                    connectingToAs.clear();
                                                                }
                                                                // 'dlg' outlives this statement (Execute() runs below), so the wide
                                                                // strings it borrows must too - named locals, not inline temporaries.
                                                                std::wstring errBufW;
                                                                if (!TextPolicy.GetCodec().Decode(
                                                                        directErrorText.data(), directErrorText.size(), errBufW))
                                                                    errBufW = L"<invalid server text>";
                                                                CLoginErrorDlg dlg(parent, errBufW.c_str(), &proxyScriptParams,
                                                                                   connectingToAs.c_str(),
                                                                                   NULL, NULL, NULL, FALSE, TRUE, proxyUsed);
                                                                if (dlg.Execute() == IDOK)
                                                                {
                                                                    if (proxyScriptParams.Password.empty())
                                                                        proxyScriptParams.AllowEmptyPassword = TRUE; // empty password at the user's request (we will not ask again)
                                                                    std::wstring stagedProxyUser;
                                                                    std::wstring stagedProxyPassword;
                                                                    std::wstring stagedUser;
                                                                    std::wstring stagedPassword;
                                                                    std::wstring stagedAccount;
                                                                    std::wstring returnedUser;
                                                                    std::string encodedUser;
                                                                    BOOL stored = FtpStoreWideText(proxyScriptParams.ProxyUser, stagedProxyUser) &&
                                                                                  FtpStoreWideText(proxyScriptParams.ProxyPassword, stagedProxyPassword) &&
                                                                                  FtpStoreWideText(proxyScriptParams.User, stagedUser) &&
                                                                                  FtpStoreWideText(proxyScriptParams.Password, stagedPassword) &&
                                                                                  FtpStoreWideText(proxyScriptParams.Account, stagedAccount) &&
                                                                                  FtpStoreWideText(proxyScriptParams.User, returnedUser);
                                                                    if (!stored)
                                                                    {
                                                                        fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                                                        state = sccsFatalError;
                                                                    }
                                                                    else if (!TextPolicy.GetCodec().Encode(stagedUser.c_str(), stagedUser.size(), encodedUser))
                                                                    {
                                                                        fatalErrorTextID = IDS_PRXSCRERR_CANNOTENCODE;
                                                                        state = sccsFatalError;
                                                                    }
                                                                    else
                                                                    {
                                                                        // Publish only no-throw swaps while holding the socket lock.
                                                                        HANDLES(EnterCriticalSection(&SocketCritSect));
                                                                        if (ProxyServer != NULL)
                                                                        {
                                                                            ProxyServer->ProxyUser.swap(stagedProxyUser);
                                                                            ProxyServer->ProxyPlainPassword.swap(stagedProxyPassword);
                                                                        }
                                                                        User.swap(stagedUser);
                                                                        Password.swap(stagedPassword);
                                                                        Account.swap(stagedAccount);
                                                                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                                                                        user.swap(returnedUser);
                                                                        Logs.ChangeUser(logUID, proxyScriptParams.User.c_str());
                                                                    }
                                                                    FTPSecureWipe(stagedProxyPassword);
                                                                    FTPSecureWipe(stagedPassword);
                                                                    FTPSecureWipe(stagedAccount);

                                                                    retryLoginWithoutAsking = dlg.RetryWithoutAsking;
                                                                    if (dlg.LoginChanged && event != ccsevClosed && !proxyUsed)
                                                                    { // retry login (without waiting and closing the connection) - handles responses such as "invalid user/password"
                                                                        state = sccsRetryLogin;
                                                                        // allBytesWritten = TRUE;   // the connection will not be closed, we must wait (besides, once a reply arrived, it is likely the whole command was sent)
                                                                    }
                                                                }
                                                                else // cancel
                                                                {
                                                                    state = sccsDone;
                                                                    actionCanceled = TRUE;
                                                                    allBytesWritten = TRUE; // no longer important, the socket will be closed
                                                                }
                                                                // repaint the main window (so the user does not stare at the remainder after the dialog during the whole connect)
                                                            }
                                                            UpdateWindow(SalamanderGeneral->GetMainWindowHWND());
                                                        }

                                                        if (state == sccsProcessLoginScript)
                                                        { // standard retry (connection closure + waiting) - handles responses like "too many users"
                                                            opFatalErrorTextID = IDS_LOGINERROR;
                                                            opFatalError = -1;                      // the error text is in directErrorText
                                                            noRetryState = sccsOperationFatalError; // if retry is not performed, execute sccsOperationFatalError
                                                            retryLogError = FALSE;                  // the error is already in the log, do not add it again
                                                            state = sccsRetry;
                                                            allBytesWritten = TRUE; // no longer important, the socket will be closed
                                                        }
                                                        // SkipFTPReply(replySize);  // done earlier - before leaving the critical section
                                                        break;
                                                    }
                                                    else // unexpected response, ignore it
                                                    {
                                                        std::string unexpectedReply;
                                                        if (FtpStoreProtocolBytes(
                                                                std::string_view(reply, static_cast<size_t>(replySize)),
                                                                unexpectedReply))
                                                            TRACE_E("Unexpected reply: " << unexpectedReply.c_str());
                                                    }
                                                }
                                            }
                                            else // not an FTP server
                                            {
                                                if (state == sccsProcessLoginScript) // if we are not reporting another error yet
                                                {
                                                    opFatalErrorTextID = IDS_NOTFTPSERVERERROR;
                                                    if (FtpStoreProtocolBytes(
                                                            std::string_view(reply, static_cast<size_t>(replySize)),
                                                            directErrorText))
                                                    {
                                                        opFatalError = -1; // the error is directly in directErrorText
                                                        state = sccsOperationFatalError;
                                                    }
                                                    else
                                                    {
                                                        fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                                        state = sccsFatalError;
                                                    }
                                                    fatalErrLogMsg = FALSE; // already in the log, no point adding it again
                                                    allBytesWritten = TRUE; // no longer important, the socket will be closed
                                                }
                                            }
                                            SkipFTPReply(replySize);
                                        }
                                        if (!sectLeaved)
                                            HANDLES(LeaveCriticalSection(&SocketCritSect));

                                        if (event == ccsevClosed)
                                        {
                                            allBytesWritten = TRUE;              // no longer important, the socket has been closed
                                            if (state == sccsProcessLoginScript) // close without a reason
                                            {
                                                fatalErrorTextID = IDS_CONNECTIONLOSTERROR;
                                                noRetryState = sccsFatalError; // if retry is not performed, execute sccsFatalError
                                                state = sccsRetry;
                                            }
                                            if (data1 != NO_ERROR)
                                            {
                                                std::string errorText;
                                                if (FTPGetErrorTextForLog(data1, errorText))
                                                    Logs.LogMessage(logUID, errorText.c_str(), -1);
                                            }
                                        }
                                        else
                                        {
                                            switch (SSLInitSequence)
                                            {
                                            case sslisAUTH:
                                            {
                                                int errID;
                                                if (InitSSL(logUID, &errID))
                                                {
                                                    int err;
                                                    CCertificate* unverifiedCert;
                                                    std::string sslErrorText;
                                                    if (!EncryptSocket(logUID, &err, &unverifiedCert, &errID, &sslErrorText,
                                                                       NULL /* it's always NULL for the control connection */))
                                                    {
                                                        allBytesWritten = TRUE; // no longer important, the socket will be closed
                                                        if (sslErrorText.empty())
                                                        {
                                                            state = sccsFatalError;
                                                            fatalErrorTextID = errID;
                                                        }
                                                        else
                                                        {
                                                            if (FtpStoreProtocolBytes(sslErrorText, directErrorText))
                                                            {
                                                                state = sccsOperationFatalError;
                                                                opFatalErrorTextID = errID;
                                                                opFatalError = -1;
                                                            }
                                                            else
                                                            {
                                                                state = sccsFatalError;
                                                                fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                                            }
                                                        }
                                                        if (err == SSLCONERR_CANRETRY)
                                                        {
                                                            noRetryState = state; // if retry is not performed, execute sccsFatalError or sccsOperationFatalError
                                                            state = sccsRetry;
                                                            retryLogError = FALSE;
                                                        }
                                                        else
                                                            fatalErrLogMsg = FALSE;
                                                    }
                                                    else
                                                    {
                                                        if (unverifiedCert != NULL) // socket is encrypted, but the server certificate is not verified; ask the user whether to trust it
                                                        {
                                                            fastRetry = TRUE; // user-induced delay, some servers do not like it (ftp.novell.com - kills after 20s)
                                                            waitWnd.Show(FALSE);

                                                            INT_PTR dlgRes;
                                                            std::wstring certificateError;
                                                            unverifiedCert->CheckCertificate(certificateError);
                                                            do
                                                            {
                                                                dlgRes = CCertificateErrDialog(parent, certificateError.c_str()).Execute();
                                                                switch (dlgRes)
                                                                {
                                                                case IDOK: // accept once
                                                                {
                                                                    Logs.LogMessage(logUID, LangStr(IDS_SSL_LOG_CERTACCEPTED).c_str(), -1, TRUE);
                                                                    SetCertificate(unverifiedCert);
                                                                    break;
                                                                }

                                                                case IDCANCEL:
                                                                {
                                                                    Logs.LogMessage(logUID, LangStr(IDS_SSL_LOG_CERTREJECTED).c_str(), -1, TRUE);
                                                                    allBytesWritten = TRUE; // no longer important, the socket will be closed
                                                                    state = sccsDone;
                                                                    break;
                                                                }

                                                                case IDB_CERTIFICATE_VIEW:
                                                                {
                                                                    unverifiedCert->ShowCertificate(parent);
                                                                    if (unverifiedCert->CheckCertificate(certificateError))
                                                                    { // the server certificate is already trusted (the user probably imported it manually)
                                                                        Logs.LogMessage(logUID, LangStr(IDS_SSL_LOG_CERTVERIFIED).c_str(), -1, TRUE);
                                                                        dlgRes = -1; // only to terminate the loop
                                                                        unverifiedCert->SetVerified(true);
                                                                        SetCertificate(unverifiedCert);
                                                                    }
                                                                    break;
                                                                }
                                                                }
                                                            } while (dlgRes == IDB_CERTIFICATE_VIEW);
                                                            unverifiedCert->Release();
                                                        }
                                                    }
                                                }
                                                else // Error! OpenSSL libs not found? Or not W2K+?
                                                {
                                                    allBytesWritten = TRUE; // no longer important, the socket will be closed
                                                    state = sccsFatalError;
                                                    fatalErrorTextID = errID;
                                                    fatalErrLogMsg = FALSE;
                                                }
                                                if (EncryptDataConnection)
                                                {
                                                    if (!setCommandPair("PBSZ 0\r\n"))
                                                    {
                                                        fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                                        state = sccsFatalError;
                                                    }
                                                    SSLInitSequence = sslisPBSZ;
                                                }
                                                else
                                                {
                                                    FTPSecureWipe(proxySendCmdBuf);
                                                    proxyLogCmdBuf.clear();
                                                    SSLInitSequence = sslisNone;
                                                }
                                                break;
                                            }
                                            case sslisPBSZ:
                                                if (!setCommandPair("PROT P\r\n"))
                                                {
                                                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                                    state = sccsFatalError;
                                                }
                                                SSLInitSequence = sslisPROT;
                                                break;
                                            case sslisPROT:
                                                FTPSecureWipe(proxySendCmdBuf);
                                                proxyLogCmdBuf.clear();
                                                SSLInitSequence = sslisNone;
                                                break;
                                            case sslisNone:
                                                break;
                                            }
                                        }
                                        if (sslisNone != SSLInitSequence && state != sccsFatalError &&
                                            !FTPFormatString(proxyLogCmdBuf, "%s", proxySendCmdBuf.c_str()))
                                        {
                                            fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                            state = sccsFatalError;
                                        }
                                        break;
                                    }

                                    default:
                                        TRACE_E("Unexpected event = " << event);
                                        break;
                                    }
                                }

                                waitWnd.Destroy();
                            }
                            else // Write error (low memory, disconnected, non-blocking "send" failure)
                            {
                                while (state == sccsProcessLoginScript)
                                {
                                    // pick an event on the socket
                                    CControlConnectionSocketEvent event;
                                    DWORD data1, data2;
                                    WaitForEventOrESC(parent, &event, &data1, &data2, 0, NULL, NULL, FALSE); // do not wait, just receive events
                                    switch (event)
                                    {
                                    // case ccsevESC:   // (the user cannot press ESC during a 0 ms timeout)
                                    case ccsevTimeout: // no message pending -> show the error from Write directly
                                    {
                                        opFatalErrorTextID = IDS_SENDCOMMANDERROR;
                                        opFatalError = error;
                                        noRetryState = sccsOperationFatalError; // if retry is not performed, execute sccsOperationFatalError
                                        state = sccsRetry;
                                        break;
                                    }

                                    case ccsevClosed:       // unexpected loss of connection (also handle that ccsevClosed could overwrite ccsevNewBytesRead)
                                    case ccsevNewBytesRead: // read new bytes (possibly an error description leading to disconnect)
                                    {
                                        char* reply;
                                        int replySize;
                                        int replyCode;

                                        HANDLES(EnterCriticalSection(&SocketCritSect));
                                        while (ReadFTPReply(&reply, &replySize, &replyCode)) // while we have some server response
                                        {
                                            Logs.LogServerMessage(logUID, reply, replySize, TextPolicy);

                                            if (replyCode == -1 ||                                 // not an FTP reply
                                                FTP_DIGIT_1(replyCode) == FTP_D1_TRANSIENTERROR || // description of a temporary error
                                                FTP_DIGIT_1(replyCode) == FTP_D1_ERROR)            // description of an error
                                            {
                                                opFatalErrorTextID = IDS_SENDCOMMANDERROR;
                                                const BOOL storedReply = FtpStoreProtocolBytes(
                                                    std::string_view(reply, static_cast<size_t>(replySize)),
                                                    directErrorText);
                                                SkipFTPReply(replySize);
                                                if (storedReply)
                                                {
                                                    opFatalError = -1;                      // the error is directly in directErrorText
                                                    noRetryState = sccsOperationFatalError; // if retry is not performed, execute sccsOperationFatalError
                                                    retryLogError = FALSE;                  // the error is already in the log, do not add it again
                                                    state = sccsRetry;
                                                }
                                                else
                                                {
                                                    fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                                                    state = sccsFatalError;
                                                }
                                                break; // no need to read another message
                                            }
                                            SkipFTPReply(replySize);
                                        }
                                        HANDLES(LeaveCriticalSection(&SocketCritSect));

                                        if (event == ccsevClosed)
                                        {
                                            if (state == sccsProcessLoginScript) // close without a reason
                                            {
                                                fatalErrorTextID = IDS_CONNECTIONLOSTERROR;
                                                noRetryState = sccsFatalError; // if retry is not performed, execute sccsFatalError
                                                state = sccsRetry;
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
                        }
                        break;
                    }
                }
                else // theoretically should never happen (saved scripts are validated)
                {
                    if (proxyScriptLowMemory)
                    {
                        fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                        state = sccsFatalError;
                    }
                    else
                    {
                        if (FtpStoreProtocolBytes(proxyScriptError, directErrorText))
                        {
                            opFatalError = -1; // error is directly in directErrorText
                            opFatalErrorTextID = IDS_ERRINPROXYSCRIPT;
                            state = sccsOperationFatalError;
                        }
                        else
                        {
                            fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                            state = sccsFatalError;
                        }
                    }
                    break;
                }
            }
            break;
        }

        case sccsFatalError: // fatal error (resource ID of the text is in 'fatalErrorTextID'; -1 uses 'directErrorText')
        {
            std::string errorText;
            if (!GetFatalErrorText(fatalErrorTextID, directErrorText, errorText))
                errorText = LoadStr(IDS_OPERDOPPR_LOWMEM);
            TrimLineEnds(errorText);
            if (fatalErrLogMsg)
            {
                std::string logMessage;
                if (FTPFormatString(logMessage, "%s\r\n", errorText.c_str()))
                    Logs.LogMessage(logUID, logMessage.c_str(), -1, TRUE);
            }
            fatalErrLogMsg = TRUE;
            std::wstring errorTextW;
            if (!FtpDecodeLocalText(errorText, errorTextW))
                errorTextW = L"<invalid local error text>";
            SalamanderGeneral->SalMessageBox(parent, errorTextW.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
            state = sccsDone;
            break;
        }

        case sccsOperationFatalError: // fatal operation error (resource ID of the text is in 'opFatalErrorTextID' and
        {                             // the Windows error number in 'opFatalError'; -1 uses 'directErrorText'; if
                                      // 'opFatalErrorTextID' is -1, the format is in 'operationFormatText')
            std::string errorText;
            if (!GetOperationFatalErrorText(opFatalError, directErrorText, errorText))
                errorText = LoadStr(IDS_OPERDOPPR_LOWMEM);
            TrimLineEnds(errorText);
            if (fatalErrLogMsg)
            {
                std::string logMessage;
                if (FTPFormatString(logMessage, "%s\r\n", errorText.c_str()))
                    Logs.LogMessage(logUID, logMessage.c_str(), -1, TRUE);
            }
            fatalErrLogMsg = TRUE;
            std::wstring errorTextW;
            if (!FtpDecodeLocalText(errorText, errorTextW))
                errorTextW = L"<invalid local error text>";
            try
            {
                std::wstring format;
                if (opFatalErrorTextID != -1)
                    format = LangStr(opFatalErrorTextID);
                else if (!FtpDecodeLocalText(operationFormatText, format))
                    format = LangStr(IDS_OPERDOPPR_LOWMEM);
                const std::wstring message = SPLFormatStringOwned(format.c_str(), errorTextW.c_str());
                SalamanderGeneral->SalMessageBox(parent, message.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                 MB_OK | MB_ICONEXCLAMATION);
            }
            catch (...)
            {
                SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_OPERDOPPR_LOWMEM).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                 MB_OK | MB_ICONEXCLAMATION);
            }
            state = sccsDone;
            break;
        }

        default: // (always false)
        {
            TRACE_E("Enexpected situation in CControlConnectionSocket::StartControlConnection(): state = " << state);
            state = sccsDone;
            break;
        }
        }
    }

    if (actionCanceled)
        Logs.LogMessage(logUID, LangStr(IDS_LOGMSGACTIONCANCELED).c_str(), -1, TRUE); // ESC (cancel) to the log

    // drop the wait cursor over the parent
    if (winParent != NULL)
    {
        winParent->DetachWindow();
        delete winParent;
    }

    // enable 'parent' again
    EnableWindow(parent, TRUE);
    // if 'parent' is active, restore the focus as well
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

    if (ret) // we are successfully connected to the FTP server
    {
        Logs.LogMessage(logUID, LangStr(IDS_LOGMSGLOGINSUCCESS).c_str(), -1, TRUE);

        BOOL canRetry = TRUE;
        int utf8ReplyCode = -1;
        if (SendFTPCommand(parent, "OPTS UTF8 ON\r\n", "OPTS UTF8 ON\r\n", NULL,
                           GetWaitTime(showWaitWndTime), NULL, &utf8ReplyCode, NULL,
                           FALSE, FALSE, FALSE, &canRetry, &nextRetryMessage, NULL))
        {
            HANDLES(EnterCriticalSection(&SocketCritSect));
            TextPolicy.ApplyUtf8OptionsReply(utf8ReplyCode);
            HANDLES(LeaveCriticalSection(&SocketCritSect));
        }
        else
            ret = FALSE;

        if (ret && useWelcomeMessage && welcomeMessage.Length > 0)
        { // display the "welcome message"
            std::wstring welcomeText;
            if (!DecodeText(welcomeMessage.GetString(), welcomeMessage.Length, welcomeText))
                welcomeText = L"";
            CWelcomeMsgDlg* w = new CWelcomeMsgDlg(SalamanderGeneral->GetMainWindowHWND(),
                                                   welcomeText.c_str());
            if (w != NULL)
            {
                if (w->Create() == NULL)
                {
                    delete w;
                    w = NULL;
                }
                else
                {
                    ModelessDlgs.Add(w);
                    if (!ModelessDlgs.IsGood())
                    {
                        DestroyWindow(w->HWindow); // also deallocates 'w'
                        ModelessDlgs.ResetState();
                    }
                    else
                    {
                        OurWelcomeMsgDlg = w->HWindow;
                        SalamanderGeneral->PostMenuExtCommand(FTPCMD_ACTIVWELCOMEMSG, TRUE);
                    }
                }
            }
        }

        // send the initial FTP commands
        std::string cmdBuf;
        HANDLES(EnterCriticalSection(&SocketCritSect));
        try
        {
            cmdBuf = InitFTPCommands;
        }
        catch (const std::bad_alloc&)
        {
            ret = FALSE;
            TRACE_E(LOW_MEMORY);
        }
        catch (const std::length_error&)
        {
            ret = FALSE;
            TRACE_E(LOW_MEMORY);
        }
        HANDLES(LeaveCriticalSection(&SocketCritSect));

        if (ret && !cmdBuf.empty())
        {
            char* next = cmdBuf.data();
            char* s;
            while (GetToken(&s, &next))
            {
                if (*s != 0 && *s <= ' ')
                    s++;     // strip only the first space (so the command can start with spaces)
                if (*s != 0) // if there is anything, send it to the server
                {
                    std::string initialCommand;
                    try
                    {
                        initialCommand.assign(s);
                        initialCommand.append("\r\n");
                    }
                    catch (const std::bad_alloc&)
                    {
                        ret = FALSE;
                        TRACE_E(LOW_MEMORY);
                        break;
                    }
                    catch (const std::length_error&)
                    {
                        ret = FALSE;
                        TRACE_E(LOW_MEMORY);
                        break;
                    }
                    int ftpReplyCode;
                    if (!SendFTPCommand(parent, initialCommand.c_str(), initialCommand.c_str(), NULL, GetWaitTime(showWaitWndTime),
                                        NULL, &ftpReplyCode, NULL, FALSE, TRUE, TRUE, &canRetry,
                                        &nextRetryMessage, NULL))
                    {
                        ret = FALSE; // we are no longer connected
                        break;       // go perform the "retry"
                    }
                }
            }
        }

        // find out the server operating system
        std::string systemCommand;
        std::string systemLogCommand;
        if (ret && PrepareFTPCommand(systemCommand, &systemLogCommand, ftpcmdSystem, NULL))
        {
            int ftpReplyCode;
            std::string systemReply;
            if (SendFTPCommand(parent, systemCommand.c_str(), systemLogCommand.c_str(), NULL, GetWaitTime(showWaitWndTime), NULL,
                               &ftpReplyCode, &systemReply, FALSE, FALSE, FALSE, &canRetry,
                               &nextRetryMessage, NULL))
            {
                HANDLES(EnterCriticalSection(&SocketCritSect));
                ServerSystem.swap(systemReply);
                HANDLES(LeaveCriticalSection(&SocketCritSect));
            }
            else
                ret = FALSE; // error -> connection closed - go perform the "retry"
        }

        if (ret && workDir != NULL &&
            !GetCurrentWorkingPath(parent, *workDir, FALSE, &canRetry, &nextRetryMessage))
        {
            ret = FALSE; // error -> connection closed - go perform the "retry"
        }

        if (canRetry && !ret) // assumes 'nextRetryMessage' has been set
        {
            if (FtpStoreProtocolBytes(nextRetryMessage, directErrorText))
            {
                opFatalErrorTextID = IDS_SENDCOMMANDERROR;
                opFatalError = -1;                      // the error is directly in directErrorText
                noRetryState = sccsOperationFatalError; // if retry is not performed, execute sccsOperationFatalError
                retryLogError = FALSE;                  // the error is already in the log, do not add it again
                state = sccsRetry;
            }
            else
            {
                fatalErrorTextID = IDS_OPERDOPPR_LOWMEM;
                state = sccsFatalError;
            }
            useWelcomeMessage = FALSE; // we already printed it, repeating it makes no sense

            // disable 'parent' again, restore the focus when re-enabling
            EnableWindow(parent, FALSE);

            // set a wait cursor over the parent again, unfortunately we cannot do it otherwise
            winParent = new CSetWaitCursorWindow;
            if (winParent != NULL)
                winParent->AttachToWindow(parent);

            goto RETRY_LABEL; // proceed to the next attempt
        }
    }
    else
    {
        CloseSocket(NULL); // close the socket (if open); the system attempts a "graceful" shutdown (we will not learn the result)
        Logs.SetIsConnected(logUID, IsConnected());
        Logs.RefreshListOfLogsInLogsDlg(); // display "connection inactive"
    }
    if (ret)
        SetupKeepAliveTimer(); // if everything is OK, set the timer for keep-alive
    else
        ReleaseKeepAlive(); // on error release keep-alive (cannot be used without an established connection)
    return ret;
}

BOOL CControlConnectionSocket::ReconnectIfNeeded(BOOL notInPanel, BOOL leftPanel, HWND parent,
                                                 std::wstring& user, BOOL* reconnected,
                                                 BOOL setStartTimeIfConnected, int* totalAttemptNum,
                                                 const std::string* retryMessage, BOOL* userRejectsReconnect,
                                                 int reconnectErrResID, BOOL useFastReconnect)
{
    CALL_STACK_MESSAGE7("CControlConnectionSocket::ReconnectIfNeeded(%d, %d, , , , %d, , %s, , %d, %d)",
                        notInPanel, leftPanel, setStartTimeIfConnected,
                        retryMessage != NULL ? retryMessage->c_str() : NULL,
                        reconnectErrResID, useFastReconnect);
    if (reconnected != NULL)
        *reconnected = FALSE;
    if (userRejectsReconnect != NULL)
        *userRejectsReconnect = FALSE;
    if (retryMessage == NULL && IsConnected()) // unknown reason for disconnect + the connection is not interrupted
    {
        if (setStartTimeIfConnected)
            SetStartTime();
        return TRUE;
    }
    else
    {
        if (retryMessage == NULL) // connection interrupted for an unknown reason - display it and ask about reconnecting
        {
            // if we have not yet displayed what led to the connection closing, do it now
            CheckCtrlConClose(notInPanel, leftPanel, parent, FALSE);

            BOOL reconnectToSrv = FALSE;
            if (!Config.AlwaysReconnect)
            {
                MSGBOXEX_PARAMS params;
                const std::wstring caption = LangStr(IDS_FTPPLUGINTITLE).c_str();
                const std::wstring text = LangStr(IDS_RECONNECTTOSRV).c_str();
                const std::wstring checkBoxText = LangStr(IDS_ALWAYSRECONNECT).c_str();
                memset(&params, 0, sizeof(params));
                params.HParent = parent;
                params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_ICONQUESTION | MSGBOXEX_HINT;
                params.Caption = caption.c_str();
                params.Text = text.c_str();
                params.CheckBoxText = checkBoxText.c_str();
                params.CheckBoxValue = &Config.AlwaysReconnect;
                reconnectToSrv = SalamanderGeneral->SalMessageBoxEx(&params) == IDYES;
                if (userRejectsReconnect != NULL)
                    *userRejectsReconnect = !reconnectToSrv;
            }

            if (Config.AlwaysReconnect || reconnectToSrv)
            {
                SetStartTime();
                BOOL ret = StartControlConnection(parent, user, TRUE, NULL,
                                                  totalAttemptNum, retryMessage, FALSE,
                                                  reconnectErrResID, useFastReconnect);
                if (ret && reconnected != NULL)
                    *reconnected = TRUE;
                return ret;
            }
            else
                return FALSE; // the user does not even want to try reopening the "control connection"
        }
        else // connection interrupted for a known reason - run "retry" inside StartControlConnection()
        {
            BOOL ret = StartControlConnection(parent, user, TRUE, NULL,
                                              totalAttemptNum, retryMessage, FALSE,
                                              reconnectErrResID, useFastReconnect);
            if (ret && reconnected != NULL)
                *reconnected = TRUE;
            return ret;
        }
    }
}

void CControlConnectionSocket::ActivateWelcomeMsg()
{
    if (OurWelcomeMsgDlg != NULL && GetForegroundWindow() == SalamanderGeneral->GetMainWindowHWND())
    {
        int i;
        for (i = ModelessDlgs.Count - 1; i >= 0; i--) // search from the end (the last window is the last in the array)
        {
            if (ModelessDlgs[i]->HWindow == OurWelcomeMsgDlg) // the window is still open, activate it
            {
                SetForegroundWindow(OurWelcomeMsgDlg);
                break;
            }
        }
    }
}

char* CControlConnectionSocket::AllocServerSystemReply()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    char* ret = DupControlConnectionText(ServerSystem.c_str());
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}

char* CControlConnectionSocket::AllocServerFirstReply()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    char* ret = DupControlConnectionText(ServerFirstReply.c_str());
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}
