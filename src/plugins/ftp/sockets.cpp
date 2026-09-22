// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <utility>

WSADATA WinSocketsData; // information about the Windows Sockets implementation

CThreadQueue SocketsThreadQueue("FTP Sockets"); // queue of all threads used in connection with sockets

CSocketsThread* SocketsThread = NULL;   // handler thread for all sockets
CRITICAL_SECTION SocketsThreadCritSect; // for synchronizing the termination of the SocketsThread thread

LPCWSTR SOCKETSWINDOW_CLASSNAME = L"SocketsHiddenWindowClass";

int CSocket::NextSocketUID = 0;                  // global counter for socket objects
CRITICAL_SECTION CSocket::NextSocketUIDCritSect; // critical section of the counter (sockets are created in different threads)

#ifdef _DEBUG
BOOL InDeleteSocket = FALSE; // TRUE if we are inside DeleteSocket (for testing direct calls to "delete socket")
#endif

DWORD CSocketsThread::LastWM_TIMER_Processing = 0; // GetTickCount() from the moment WM_TIMER was last processed (WM_TIMER arrives only during "idle" message loops, which is unacceptable for us)

// ***************************************************************************************

BOOL InitSockets(HWND parent)
{
    // initialize Windows Sockets library
    int err;
    if ((err = WSAStartup(MAKEWORD(1, 1), &WinSocketsData)) != 0) // error
    {
        try
        {
            std::string errorBytes;
            std::wstring errorText;
            std::wstring message;
            if (FTPGetErrorText(err, errorBytes) &&
                FtpDecodeLocalText(errorBytes, errorText) &&
                FtpFormatWideText(LangStr(IDS_WINSOCKETSERROR).c_str(), errorText,
                                  message))
            {
                SalamanderGeneral->ShowMessageBox(
                    message.c_str(), LangStr(IDS_FTPPLUGINTITLE).c_str(), MSGBOX_ERROR);
            }
            else
            {
                SalamanderGeneral->ShowMessageBox(
                    LangStr(IDS_OPERDOPPR_LOWMEM).c_str(),
                    LangStr(IDS_FTPPLUGINTITLE).c_str(), MSGBOX_ERROR);
            }
        }
        catch (...)
        {
            MessageBoxW(parent, L"Windows Sockets initialization failed.", L"FTP Client",
                        MB_OK | MB_ICONERROR);
        }
        return FALSE;
    }

    HANDLES(InitializeCriticalSection(&SocketsThreadCritSect));
    HANDLES(InitializeCriticalSection(&CSocket::NextSocketUIDCritSect));

    // start Sockets thread
    SocketsThread = new CSocketsThread;
    if (SocketsThread != NULL)
    {
        if (!SocketsThread->IsGood() ||
            SocketsThread->Create(SocketsThreadQueue) == NULL)
        {
            delete SocketsThread;
            SocketsThread = NULL;
        }
        else
        {
            if (!SocketsThread->IsRunning())
                SocketsThread = NULL; // an error occurred, the thread deallocates itself
        }
    }
    else
        TRACE_E(LOW_MEMORY);
    if (SocketsThread == NULL)
    {
        WSACleanup();
        return FALSE;
    }

    return TRUE;
}

void ReleaseSockets()
{
    if (SocketsThread != NULL)
    {
        // we have to null out SocketsThread so other threads know there is no one left to receive
        // their result (unless SocketsThreadQueue.KillAll removes them earlier)
        CSocketsThread* s = SocketsThread;
        HANDLES(EnterCriticalSection(&SocketsThreadCritSect));
        SocketsThread = NULL;
        HANDLES(LeaveCriticalSection(&SocketsThreadCritSect));
        HANDLE thread = s->GetHandle();
        s->Terminate();
        // object deallocation is automatic (if the thread terminates normally) - 's' must not be used
        // afterwards
        CALL_STACK_MESSAGE1("ReleaseSockets(): SocketsThreadQueue.WaitForExit()");
        SocketsThreadQueue.WaitForExit(thread, INFINITE); // wait for the thread to finish
    }

    // if they did not finish "legally", we will kill them
    SocketsThreadQueue.KillAll(TRUE, 0, 0);

    HANDLES(DeleteCriticalSection(&CSocket::NextSocketUIDCritSect));
    HANDLES(DeleteCriticalSection(&SocketsThreadCritSect)); // all threads are dead, no one will use the section anymore

    if (WSACleanup() == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
#ifdef _DEBUG
        std::string errorText;
        FTPGetErrorText(err, errorText);
        TRACE_E("Unable to release Windows Sockets: " << errorText.c_str());
#endif
    }
}

void DeleteSocket(class CSocket* socket)
{
#ifdef _DEBUG
    InDeleteSocket = TRUE;
#endif
    if (SocketsThread != NULL)
        SocketsThread->DeleteSocket(socket);
    else
    {
        if (socket != NULL)
            delete socket;
    }
#ifdef _DEBUG
    InDeleteSocket = FALSE;
#endif
}

//
// ****************************************************************************
// CSocket
//

CSocket::CSocket()
{
    HANDLES(InitializeCriticalSection(&SocketCritSect));
    HANDLES(EnterCriticalSection(&NextSocketUIDCritSect));
    UID = NextSocketUID++;
    HANDLES(LeaveCriticalSection(&NextSocketUIDCritSect));
    Msg = -1;
    Socket = INVALID_SOCKET;
    SSLConn = NULL;
    ReuseSSLSession = 0 /* try */;
    ReuseSSLSessionFailed = FALSE;
    pCertificate = NULL;
    OurShutdown = FALSE;
    IsDataConnection = FALSE;
    SocketState = ssNotOpened;
    HostAddress.clear();
    HostAddressWire.clear();
    HostIP = INADDR_NONE;
    HostPort = -1;
    ProxyUserBytes.clear();
    ProxyPasswordBytes.clear();
    ProxyIP = INADDR_NONE;
    ProxyErrorCode = pecNoError;
    ProxyWinError = NO_ERROR;
    ShouldPostFD_WRITE = FALSE;
    HTTP11_FirstLineOfReply.clear();
    HTTP11_EmptyRowCharsReceived = 0;
    IsSocketConnectedLastCallTime = 0;
}

CSocket::~CSocket()
{
    HANDLES(DeleteCriticalSection(&SocketCritSect));
    if (Socket != INVALID_SOCKET)
        TRACE_E("CSocket::~CSocket(): Associated Windows socket object was not closed!");
    if (SSLConn != NULL)
        TRACE_E("CSocket::~CSocket(): Associated SSL connection was not closed!");
#ifdef _DEBUG
    if (!InDeleteSocket)
        TRACE_E("CSocket::~CSocket(): Incorrect use of operator delete, use DeleteSocket() instead.");
#endif
    FTPSecureWipe(ProxyPasswordBytes);
    if (pCertificate)
        pCertificate->Release();
}

BOOL CSocket::Shutdown(DWORD* error)
{
    CALL_STACK_MESSAGE1("CSocket::Shutdown()");
    if (error != NULL)
        *error = NO_ERROR;

    HANDLES(EnterCriticalSection(&SocketCritSect));

    BOOL ret = FALSE;
    if (Socket != INVALID_SOCKET) // the socket is connected (otherwise it cannot be closed)
    {
        if (SSLConn)
        {
            int err = SSLLib.SSL_shutdown(SSLConn);
            SSLLib.SSL_free(SSLConn);
            SSLConn = NULL;
        }
        if (shutdown(Socket, SD_SEND) != SOCKET_ERROR)
        {
            ret = TRUE; // success
            OurShutdown = TRUE;
        }
        else // an error occurred, find out which one
        {
            if (error != NULL)
                *error = WSAGetLastError();
        }
    }
    else
        TRACE_I("CSocket::Shutdown(): socket is already closed.");

    HANDLES(LeaveCriticalSection(&SocketCritSect));

    return ret;
}

BOOL CSocket::CloseSocket(DWORD* error)
{
    CALL_STACK_MESSAGE1("CSocket::CloseSocket()");
    if (error != NULL)
        *error = NO_ERROR;

    HANDLES(EnterCriticalSection(&SocketCritSect));

    BOOL ret = FALSE;
    if (Socket != INVALID_SOCKET) // the socket is connected (otherwise it cannot be closed)
    {
        if (SSLConn)
        {
            int err = SSLLib.SSL_shutdown(SSLConn);
            SSLLib.SSL_free(SSLConn);
            SSLConn = NULL;
        }
        if (closesocket(Socket) != SOCKET_ERROR)
        {
            ret = TRUE; // success
            Socket = INVALID_SOCKET;
            OurShutdown = FALSE;
            SocketState = ssNotOpened;
            ReuseSSLSession = 0 /* try */;
            ReuseSSLSessionFailed = FALSE;
        }
        else // an error occurred, find out which one
        {
            if (error != NULL)
                *error = WSAGetLastError();
        }
    }
    else
        TRACE_I("CSocket::CloseSocket(): socket is already closed.");

    HANDLES(LeaveCriticalSection(&SocketCritSect));

    return ret;
}

BOOL CSocket::IsConnected()
{
    CALL_STACK_MESSAGE1("CSocket::IsConnected()");
    HANDLES(EnterCriticalSection(&SocketCritSect));
    BOOL ret = Socket != INVALID_SOCKET;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}

BOOL CSocket::ConnectWithProxy(DWORD serverIP, unsigned short serverPort, CFTPProxyServerType proxyType,
                               DWORD* err, const wchar_t* host, unsigned short port,
                               const wchar_t* proxyUser, const wchar_t* proxyPassword, DWORD hostIP)
{
    CALL_STACK_MESSAGE8("CSocket::ConnectWithProxy(0x%X, %u, %d, , %ls, %u, %ls, , 0x%X)",
                        serverIP, serverPort, proxyType, host, port, proxyUser, hostIP);
    switch (proxyType)
    {
    case fpstSocks4:
    case fpstSocks4A:
    case fpstSocks5:
    case fpstHTTP1_1:
    {
        SocketsThread->LockSocketsThread();
        HANDLES(EnterCriticalSection(&SocketCritSect));
        if (err != NULL)
            *err = NO_ERROR;
        BOOL ret = FALSE;
        if (SocketState != ssNotOpened ||
            !SetProxyData(host, port, proxyType, proxyUser, proxyPassword, hostIP, err, INADDR_NONE))
        {
            if (SocketState != ssNotOpened)
                TRACE_E("CSocket::ConnectWithProxy(): SocketState != ssNotOpened");
        }
        else
        {
            switch (proxyType)
            {
            case fpstSocks4:
                SocketState = ssSocks4_Connect;
                break;
            case fpstSocks4A:
                SocketState = ssSocks4A_Connect;
                break;
            case fpstSocks5:
                SocketState = ssSocks5_Connect;
                break;
            case fpstHTTP1_1:
                SocketState = ssHTTP1_1_Connect;
                break;
            }
            if (Connect(serverIP, serverPort, err, TRUE))
                ret = TRUE;
            else
                SocketState = ssNotOpened;
        }
        HANDLES(LeaveCriticalSection(&SocketCritSect));
        SocketsThread->UnlockSocketsThread();
        return ret;
    }

    default:
        std::wstring stagedHost;
        std::string stagedWireHost;
        if (host == NULL || *host == 0 ||
            !FtpStoreWideText(host, stagedHost) ||
            !FtpEncodeNetworkHost(stagedHost.c_str(), stagedWireHost))
        {
            if (err != NULL)
                *err = ERROR_INVALID_NAME;
            return FALSE;
        }
        HostAddress.swap(stagedHost); // Needed by certificate verification.
        HostAddressWire.swap(stagedWireHost);
        return Connect(serverIP, serverPort, err);
    }
}

void CSocket::SetSndRcvBuffers()
{
#ifdef DATACON_USES_OUR_BUF_SIZES
    CALL_STACK_MESSAGE1("CSocket::SetSndRcvBuffers()");

    if (IsDataConnection) // for non-data connections we keep the defaults
    {
        int bufSize;
        int len = sizeof(bufSize);
        if (getsockopt(Socket, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, &len) != 0)
            bufSize = 0;
        else
        {
            if (len != sizeof(bufSize))
            {
                TRACE_E("CSocket::SetSndRcvBuffers(): getsockopt(SO_SNDBUF) did not return int-number");
                bufSize = 0;
            }
        }
        if (bufSize < DATACON_SNDBUF_SIZE)
        {
            bufSize = DATACON_SNDBUF_SIZE;
            if (setsockopt(Socket, SOL_SOCKET, SO_SNDBUF, (char*)&bufSize, sizeof(bufSize)) != 0)
                TRACE_E("CSocket::SetSndRcvBuffers(): setsockopt(SO_SNDBUF) failed");
        }

        len = sizeof(bufSize);
        if (getsockopt(Socket, SOL_SOCKET, SO_RCVBUF, (char*)&bufSize, &len) != 0)
            bufSize = 0;
        else
        {
            if (len != sizeof(bufSize))
            {
                TRACE_E("CSocket::SetSndRcvBuffers(): getsockopt(SO_RCVBUF) did not return int-number");
                bufSize = 0;
            }
        }
        if (bufSize < DATACON_RCVBUF_SIZE)
        {
            bufSize = DATACON_RCVBUF_SIZE;
            if (setsockopt(Socket, SOL_SOCKET, SO_RCVBUF, (char*)&bufSize, sizeof(bufSize)) != 0)
                TRACE_E("CSocket::SetSndRcvBuffers(): setsockopt(SO_RCVBUF) failed");
        }
    }
#endif // DATACON_USES_OUR_BUF_SIZES
}

BOOL CSocket::Connect(DWORD ip, unsigned short port, DWORD* error, BOOL calledFromConnect)
{
    CALL_STACK_MESSAGE4("CSocket::Connect(0x%X, %u, , %d)", ip, port, calledFromConnect);

    if (!calledFromConnect)
    {
        SocketsThread->LockSocketsThread();
        HANDLES(EnterCriticalSection(&SocketCritSect));
    }

    HWND socketsWindow = SocketsThread->GetHiddenWindow();

    /*  // commented out: if we are already inside CSocketsThread::CritSect, entering SocketCritSect is not a problem
#ifdef _DEBUG
  if (SocketCritSect.RecursionCount > 1) TRACE_E("Incorrect call to CSocket::Connect: from section SocketCritSect!");
#endif
*/
    if (error != NULL)
        *error = NO_ERROR;

    if (!calledFromConnect && SocketState != ssNotOpened || Socket != INVALID_SOCKET)
    {
        if (!calledFromConnect && SocketState != ssNotOpened)
            TRACE_E("CSocket::Connect(): SocketState != ssNotOpened");
        else
            TRACE_E("Socket is already opened!");
        if (!calledFromConnect)
        {
            HANDLES(LeaveCriticalSection(&SocketCritSect));
            SocketsThread->UnlockSocketsThread();
        }
        return FALSE;
    }
    if (!calledFromConnect)
        SocketState = ssNoProxyOrConnected;
    ProxyErrorCode = pecNoError;
    ProxyWinError = NO_ERROR;
    ShouldPostFD_WRITE = FALSE;

    BOOL addCalled = FALSE;
    if (Msg == -1) // the socket is not in SocketsThread, add it
    {
        if (!SocketsThread->AddSocket(this)) // we are inside CSocketsThread::CritSect, so this call is possible even from CSocket::SocketCritSect
        {
            if (!calledFromConnect)
            {
                SocketState = ssNotOpened;
                HANDLES(LeaveCriticalSection(&SocketCritSect));
                SocketsThread->UnlockSocketsThread();
            }
            return FALSE;
        }
        addCalled = TRUE;
    }

    BOOL ret = FALSE;
    Socket = socket(AF_INET, SOCK_STREAM, 0);
    if (Socket != INVALID_SOCKET)
    {
        // disable the Nagle algorithm, we do not want any unnecessary waiting
        // WARNING: when downloading on a local network there were frequent dropouts, the transfer often
        //          restarted, and I did not notice any speedup, so I decided
        //          to give this up (this was still at accept())
        // BOOL noDelayOn = TRUE;
        // setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, (char *)&noDelayOn, sizeof(noDelayOn));

        SetSndRcvBuffers();

        if (WSAAsyncSelect(Socket, socketsWindow,
                           Msg, FD_CONNECT | FD_CLOSE | FD_READ | FD_WRITE) != SOCKET_ERROR)
        {
            SOCKADDR_IN addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = ip;
            if (connect(Socket, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR)
            {
                DWORD err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK) // normal reaction to connecting a non-blocking socket
                {
                    ret = TRUE;
                }
                else // a different connect error
                {
                    if (error != NULL)
                        *error = WSAGetLastError();
                }
            }
            else // returned NO_ERROR, behaves like the blocking variant of "connect", should never happen
            {
                TRACE_E("CSocket::Connect(): connect has returned unexpected value!");
            }
        }
        else
        {
            if (error != NULL)
                *error = WSAGetLastError();
        }

        if (!ret)
        {
            closesocket(Socket);
            Socket = INVALID_SOCKET;
            OurShutdown = FALSE;
        }
    }
    else
    {
        if (error != NULL)
            *error = WSAGetLastError();
    }
    if (!calledFromConnect && !ret)
        SocketState = ssNotOpened;
    if (!ret && addCalled)
        SocketsThread->DeleteSocket(this, TRUE); // detach the object from SocketsThread; we are inside CSocketsThread::CritSect, so this call is possible even from CSocket::SocketCritSect
    if (!calledFromConnect)
    {
        HANDLES(LeaveCriticalSection(&SocketCritSect));
        SocketsThread->UnlockSocketsThread();
    }
    return ret;
}

BOOL CSocket::SetProxyData(const wchar_t* hostAddress, unsigned short hostPort,
                           CFTPProxyServerType proxyType,
                           const wchar_t* proxyUser, const wchar_t* proxyPassword,
                           DWORD hostIP, DWORD* error, DWORD proxyIP)
{
    DWORD failure = NO_ERROR;
    if (hostAddress == NULL || *hostAddress == 0)
    {
        TRACE_E("CSocket::SetProxyData(): hostAddress cannot be empty!");
        failure = ERROR_INVALID_NAME;
    }
    std::wstring stagedHost;
    std::string stagedWireHost;
    if (failure == NO_ERROR && !FtpStoreWideText(hostAddress, stagedHost))
        failure = ERROR_NOT_ENOUGH_MEMORY;
    if (failure == NO_ERROR && !FtpEncodeNetworkHost(stagedHost.c_str(), stagedWireHost))
        failure = ERROR_INVALID_NAME;

    std::string stagedUserBytes;
    std::string stagedPasswordBytes;
    DWORD credentialError = NO_ERROR;
    if (failure == NO_ERROR &&
        !FtpEncodeProxyCredentials(proxyUser, proxyPassword,
                                   proxyType == fpstSocks5,
                                   stagedUserBytes, stagedPasswordBytes,
                                   credentialError))
        failure = credentialError;
    if (failure != NO_ERROR)
    {
        FTPSecureWipe(stagedPasswordBytes);
        if (error != NULL)
            *error = failure;
        return FALSE;
    }

    HostAddress.swap(stagedHost);
    HostAddressWire.swap(stagedWireHost);
    HostIP = hostIP;
    HostPort = hostPort;
    ProxyUserBytes.swap(stagedUserBytes);
    ProxyPasswordBytes.swap(stagedPasswordBytes);
    ProxyIP = proxyIP;
    FTPSecureWipe(stagedPasswordBytes);
    return TRUE;
}

BOOL CSocket::GetLocalIP(DWORD* ip, DWORD* error)
{
    CALL_STACK_MESSAGE1("CSocket::GetLocalIP()");

    SOCKADDR_IN addr;
    memset(&addr, 0, sizeof(addr));
    int len = sizeof(addr);
    if (error != NULL)
        *error = NO_ERROR;

    HANDLES(EnterCriticalSection(&SocketCritSect));

    if (getsockname(Socket, (SOCKADDR*)&addr, &len) != SOCKET_ERROR)
        *ip = addr.sin_addr.s_addr;
    else
    {
        if (error != NULL)
            *error = WSAGetLastError();
        *ip = INADDR_ANY;
    }

    HANDLES(LeaveCriticalSection(&SocketCritSect));

    return *ip != INADDR_ANY; // INADDR_ANY is not a valid IP
}

BOOL CSocket::OpenForListening(DWORD* listenOnIP, unsigned short* listenOnPort, DWORD* error)
{
    CALL_STACK_MESSAGE3("CSocket::OpenForListening(0x%X, %u,)", *listenOnIP, *listenOnPort);

    SocketsThread->LockSocketsThread();
    HANDLES(EnterCriticalSection(&SocketCritSect));

    HWND socketsWindow = SocketsThread->GetHiddenWindow();

#ifdef _DEBUG
    if (SocketCritSect.RecursionCount > 1)
        TRACE_E("Incorrect call to CSocket::OpenForListening: from section SocketCritSect!");
#endif

    if (error != NULL)
        *error = NO_ERROR;

    if (SocketState != ssNotOpened || Socket != INVALID_SOCKET)
    {
        if (SocketState != ssNotOpened)
            TRACE_E("CSocket::OpenForListening(): SocketState != ssNotOpened!");
        else
            TRACE_E("Socket is already opened!");
        HANDLES(LeaveCriticalSection(&SocketCritSect));
        SocketsThread->UnlockSocketsThread();
        return FALSE;
    }
    SocketState = ssNoProxyOrConnected;
    ProxyErrorCode = pecNoError;
    ProxyWinError = NO_ERROR;

    BOOL addCalled = FALSE;
    if (Msg == -1) // the socket is not in SocketsThread, add it
    {
        if (!SocketsThread->AddSocket(this)) // we are inside CSocketsThread::CritSect, so this call is possible even from CSocket::SocketCritSect
        {
            SocketState = ssNotOpened;
            HANDLES(LeaveCriticalSection(&SocketCritSect));
            SocketsThread->UnlockSocketsThread();
            return FALSE;
        }
        addCalled = TRUE;
    }

    BOOL ret = FALSE;
    Socket = socket(AF_INET, SOCK_STREAM, 0);
    if (Socket != INVALID_SOCKET)
    {
        SetSndRcvBuffers();

        if (WSAAsyncSelect(Socket, socketsWindow,
                           Msg, FD_ACCEPT | FD_CLOSE | FD_READ | FD_WRITE) != SOCKET_ERROR)
        {
            SOCKADDR_IN addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(*listenOnPort); // 0 = use any port (choose between 1024 and 5000)
            addr.sin_addr.s_addr = INADDR_ANY;    // INADDR_ANY = choose appropriate local address
            if (bind(Socket, (SOCKADDR*)&addr, sizeof(addr)) != SOCKET_ERROR)
            {
                int len = sizeof(addr);
                if (getsockname(Socket, (SOCKADDR*)&addr, &len) != SOCKET_ERROR)
                {
                    if (addr.sin_addr.s_addr != INADDR_ANY)
                        *listenOnIP = addr.sin_addr.s_addr;
                    *listenOnPort = ntohs(addr.sin_port);
                    if (listen(Socket, 5) != SOCKET_ERROR)
                    {
                        ret = TRUE;
                    }
                }
            }
        }

        if (!ret)
        {
            if (error != NULL)
                *error = WSAGetLastError(); // determine the error of the last WSA call (select, bind, get, listen)

            closesocket(Socket);
            Socket = INVALID_SOCKET;
            OurShutdown = FALSE;
        }
    }
    else
    {
        if (error != NULL)
            *error = WSAGetLastError();
    }
    if (!ret)
        SocketState = ssNotOpened;
    if (!ret && addCalled)
        SocketsThread->DeleteSocket(this, TRUE); // detach the object from SocketsThread; we are inside CSocketsThread::CritSect, so this call is possible even from CSocket::SocketCritSect
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    SocketsThread->UnlockSocketsThread();
    return ret;
}

BOOL CSocket::OpenForListeningWithProxy(DWORD listenOnIP, unsigned short listenOnPort,
                                        const wchar_t* host, DWORD hostIP, unsigned short hostPort,
                                        CFTPProxyServerType proxyType, DWORD proxyIP,
                                        unsigned short proxyPort, const wchar_t* proxyUser,
                                        const wchar_t* proxyPassword, BOOL* listenError, DWORD* err)
{
    CALL_STACK_MESSAGE10("CSocket::OpenForListeningWithProxy(0x%X, %u, %ls, 0x%X, %u, %d, 0x%X, %u, %ls, , ,)",
                         listenOnIP, listenOnPort, host, hostIP, hostPort,
                         (int)proxyType, proxyIP, proxyPort, proxyUser);

    switch (proxyType)
    {
    case fpstSocks4:
    case fpstSocks4A:
    case fpstSocks5:
    case fpstHTTP1_1:
    {
        if (listenError != NULL)
            *listenError = FALSE;
        SocketsThread->LockSocketsThread();
        HANDLES(EnterCriticalSection(&SocketCritSect));
        if (err != NULL)
            *err = NO_ERROR;
        BOOL ret = FALSE;
        if (SocketState != ssNotOpened ||
            !SetProxyData(host, hostPort, proxyType, proxyUser, proxyPassword, hostIP, err, proxyIP))
        {
            if (SocketState != ssNotOpened)
                TRACE_E("CSocket::OpenForListeningWithProxy(): SocketState != ssNotOpened");
        }
        else
        {
            switch (proxyType)
            {
            case fpstSocks4:
                SocketState = ssSocks4_Listen;
                break;
            case fpstSocks4A:
                SocketState = ssSocks4A_Listen;
                break;
            case fpstSocks5:
                SocketState = ssSocks5_Listen;
                break;
            case fpstHTTP1_1:
                SocketState = ssHTTP1_1_Listen;
                break;
            }
            if (Connect(proxyIP, proxyPort, err, TRUE))
                ret = TRUE;
            else
                SocketState = ssNotOpened;
        }
        HANDLES(LeaveCriticalSection(&SocketCritSect));
        SocketsThread->UnlockSocketsThread();
        return ret;
    }

    default:
    {
        if (listenError != NULL)
            *listenError = TRUE;
        BOOL ret = OpenForListening(&listenOnIP, &listenOnPort, err);
        if (ret)
            ListeningForConnection(listenOnIP, listenOnPort, FALSE);
        return ret;
    }
    }
}

class CGetHostByNameThread : public CThread
{
protected:
    std::wstring Address; // address being looked up
    int HostUID;   // hostUID from the GetHostByAddress call that created this thread
    int SocketMsg; // Msg from the socket object that should receive the result
    int SocketUID; // UID from the socket object that should receive the result

public:
    CGetHostByNameThread(std::wstring&& address, int hostUID, int socketMsg,
                         int socketUID) : CThread(L"GetHostByName"), Address(std::move(address))
    {
        HostUID = hostUID;
        SocketMsg = socketMsg;
        SocketUID = socketUID;
    }

    virtual ~CGetHostByNameThread()
    {
    }

    virtual unsigned Body()
    {
        CALL_STACK_MESSAGE2("CGetHostByNameThread::Body(%ls)", Address.c_str());
        int err = 0;
        DWORD ip = INADDR_NONE; // error
        if (!Address.empty())
        {
            ADDRINFOW hints = {};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            ADDRINFOW* result = NULL;
            const int lookupResult = GetAddrInfoW(Address.c_str(), NULL, &hints, &result);
            if (lookupResult != 0)
                err = lookupResult;
            else if (result != NULL && result->ai_addrlen >= sizeof(SOCKADDR_IN))
                ip = reinterpret_cast<SOCKADDR_IN*>(result->ai_addr)->sin_addr.s_addr;
            if (result != NULL)
                FreeAddrInfoW(result);
        }
        // send the result to the "sockets" thread
        HANDLES(EnterCriticalSection(&SocketsThreadCritSect));
        if (SocketsThread != NULL)
        {
            SocketsThread->PostHostByAddressResult(SocketMsg, SocketUID, ip, HostUID, err); // post result
        }
        HANDLES(LeaveCriticalSection(&SocketsThreadCritSect));
        return 0;
    }
};

BOOL CSocket::GetHostByAddress(const wchar_t* address, int hostUID)
{
    CALL_STACK_MESSAGE3("CSocket::GetHostByAddress(%ls, %d)", address, hostUID);

    SocketsThread->LockSocketsThread();
    HANDLES(EnterCriticalSection(&SocketCritSect));
    /*  // commented out: if we are already inside CSocketsThread::CritSect, entering SocketCritSect is not a problem
#ifdef _DEBUG
  if (SocketCritSect.RecursionCount > 1) TRACE_E("Incorrect call to CSocket::GetHostByAddress: from section SocketCritSect!");
#endif
*/
    BOOL addCalled = FALSE;
    if (Msg == -1) // the socket is not in SocketsThread, add it
    {
        if (!SocketsThread->AddSocket(this))
        {
            HANDLES(LeaveCriticalSection(&SocketCritSect));
            SocketsThread->UnlockSocketsThread();
            return FALSE;
        }
        addCalled = TRUE;
    }

    BOOL postMsg = FALSE; // if TRUE, SocketsThread->PostHostByAddressResult should be called with the following parameters:
    int postSocketMsg = Msg;
    int postSocketUID = UID;
    DWORD postIP;
    int postHostUID = hostUID;
    int postErr = 0;

    BOOL maybeOK = FALSE;
    IN_ADDR numericAddress = {};
    const int numericResult = InetPtonW(AF_INET, address, &numericAddress);
    DWORD ip = numericResult == 1 ? numericAddress.s_addr : INADDR_NONE;
    if (numericResult != 1) // not an IPv4 string, resolve the Unicode DNS name
    {
        std::wstring ownedAddress;
        CGetHostByNameThread* t = NULL;
        if (FtpStoreWideText(address != NULL ? address : L"", ownedAddress))
            t = new CGetHostByNameThread(std::move(ownedAddress), hostUID, Msg, UID);
        if (t != NULL)
        {
            if (t->Create(SocketsThreadQueue) == NULL)
            {
                postErr = GetLastError();
                if (postErr == NO_ERROR)
                    postErr = ERROR_NOT_ENOUGH_MEMORY;
                delete t; // the thread did not start, error
            }
            else
                maybeOK = TRUE; // the thread is running, the address lookup may succeed
        }
        else
        {
            TRACE_E(LOW_MEMORY); // low memory, error
            postErr = ERROR_NOT_ENOUGH_MEMORY;
        }
        if (!maybeOK)
        {
            postMsg = TRUE; // post the error to the object
            postIP = INADDR_NONE;
        }
    }
    else
    {
        maybeOK = TRUE;
        postMsg = TRUE; // post the IP
        postIP = ip;
    }
    if (!maybeOK && addCalled)
        SocketsThread->DeleteSocket(this, TRUE); // detach the object from SocketsThread

    HANDLES(LeaveCriticalSection(&SocketCritSect));
    SocketsThread->UnlockSocketsThread();

    if (postMsg) // to keep it outside the SocketCritSect critical section
    {
        SocketsThread->PostHostByAddressResult(postSocketMsg, postSocketUID, postIP, postHostUID, postErr);
    }
    return maybeOK;
}

void CSocket::ReceiveHostByAddress(DWORD ip, int hostUID, int err)
{
    /*
  CALL_STACK_MESSAGE4("CSocket::ReceiveHostByAddress(0x%X, %d, %d)", ip, hostUID, err);
  if (ip != INADDR_NONE)
  {
    in_addr addr;
    addr.s_addr = ip;
    TRACE_I("CSocket::ReceiveHostByAddress(): received IP: " << inet_ntoa(addr));
  }
  else
  {
    std::string errorText;
    if (err != 0) FTPGetErrorText(err, errorText);
    TRACE_I("CSocket::ReceiveHostByAddress(): error: " << errorText.c_str());
  }
*/
}

void CSocket::ReceiveHostByAddressInt(DWORD ip, int hostUID, int err, int index)
{
    CALL_STACK_MESSAGE5("CSocket::ReceiveHostByAddressInt(0x%X, %d, %d, %d)", ip, hostUID, err, index);

    HANDLES(EnterCriticalSection(&SocketCritSect));
    if (SocketState == ssSocks4_WaitForIP)
    {
        if (ip != INADDR_NONE) // we have the FTP server's IP address, send a connect request to the proxy server
        {
            HostIP = ip;
            SocketState = ssSocks4_WaitForCon;
            BOOL csLeft;
            Socks4SendRequest(1 /* connect request */, index, &csLeft, TRUE /* connect */, FALSE /* Socks 4 */);
            if (!csLeft)
                HANDLES(LeaveCriticalSection(&SocketCritSect));
        }
        else // error while obtaining the FTP server's IP address
        {
            ProxyErrorCode = pecGettingHostIP;
            ProxyWinError = err;
            SocketState = ssConnectFailed;
            HANDLES(LeaveCriticalSection(&SocketCritSect));
            ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
        }
    }
    else
    {
        HANDLES(LeaveCriticalSection(&SocketCritSect));
        ReceiveHostByAddress(ip, hostUID, err);
    }
}

BOOL CSocket::GetProxyError(std::string& errorText, std::string* formatText,
                            BOOL oneLineText) noexcept
{
    CALL_STACK_MESSAGE1("CSocket::GetProxyError(dynamic)");

    HANDLES(EnterCriticalSection(&SocketCritSect));
    BOOL ret = FALSE;
    if (ProxyErrorCode != pecNoError)
    {
        std::string detail;
        BOOL formatted = TRUE;
        if (ProxyErrorCode != pecUnexpectedReply && ProxyErrorCode != pecProxySrvError &&
            ProxyErrorCode != pecNoAuthUnsup && ProxyErrorCode != pecUserPassAuthUnsup &&
            ProxyErrorCode != pecUserPassAuthFail && ProxyErrorCode != pecListenUnsup &&
            ProxyErrorCode != pecHTTPProxySrvError)
        {
            if (ProxyWinError == NO_ERROR)
                formatted = FTPFormatString(detail, "%s", LoadStr(ProxyErrorCode == pecReceivingBytes ? IDS_CONNECTIONLOSTERROR : IDS_UNKNOWNERROR));
            else if (FTPGetErrorText(ProxyWinError, detail))
            {
                while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r'))
                    detail.pop_back();
            }
            else
                formatted = FALSE;
        }
        else if (ProxyErrorCode == pecHTTPProxySrvError && !HTTP11_FirstLineOfReply.empty())
        {
            const char* text = HTTP11_FirstLineOfReply.c_str();
            while (*text != 0 && *text > ' ')
                ++text;
            while (*text != 0 && *text <= ' ')
                ++text;
            formatted = FTPFormatString(detail, "%s", text);
            while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r'))
                detail.pop_back();
        }
        else if (ProxyErrorCode == pecProxySrvError)
            formatted = FTPFormatString(detail, "%s", LoadStr(ProxyWinError));

        std::string stagedError;
        std::string stagedFormat;
        if (formatted && oneLineText)
        {
            switch (ProxyErrorCode)
            {
            case pecGettingHostIP:
            case pecUnexpectedReply:
                formatted = FTPFormatString(stagedError,
                                            LoadStr(ProxyErrorCode == pecUnexpectedReply ? IDS_PROXYUNEXPECTEDREPLY : IDS_PROXYERRGETIP),
                                            HostAddressWire.c_str(), detail.c_str());
                break;
            case pecSendingBytes:
            case pecReceivingBytes:
                formatted = FTPFormatString(stagedError,
                                            LoadStr(ProxyErrorCode == pecSendingBytes ? IDS_PROXYERRSENDREQ : IDS_PROXYERRRECVREP),
                                            detail.c_str());
                break;
            case pecConPrxSrvError:
                formatted = FTPFormatString(stagedError, LoadStr(IDS_PROXYERRUNABLETOCON2), detail.c_str());
                break;
            case pecNoAuthUnsup:
                formatted = FTPFormatString(stagedError, "%s", LoadStr(IDS_PROXYERRNOAUTHUNSUP));
                break;
            case pecUserPassAuthUnsup:
                formatted = FTPFormatString(stagedError, "%s", LoadStr(IDS_PROXYERRUSERPASSAUTHUNSUP));
                break;
            case pecUserPassAuthFail:
                formatted = FTPFormatString(stagedError, "%s", LoadStr(IDS_PROXYERRUSERPASSFAIL));
                break;
            case pecListenUnsup:
                formatted = FTPFormatString(stagedError, "%s", LoadStr(IDS_PROXYERRLISTENUNSUP));
                break;
            default:
                formatted = FTPFormatString(stagedError, LoadStr(IDS_PROXYERROPENCON), HostAddressWire.c_str(), HostPort, detail.c_str());
                break;
            }
        }
        else if (formatted)
        {
            if (formatText != NULL)
                formatted = FTPFormatString(stagedFormat,
                                            LoadStr(ProxyErrorCode == pecGettingHostIP       ? IDS_GETIPERROR
                                                    : ProxyErrorCode == pecSendingBytes    ? IDS_PROXYSENDREQERROR
                                                    : ProxyErrorCode == pecReceivingBytes  ? IDS_PROXYRECVREPERROR
                                                    : ProxyErrorCode == pecUnexpectedReply ? IDS_PROXYUNEXPECTEDREPLY
                                                    : ProxyErrorCode == pecNoAuthUnsup      ? IDS_PROXYERRNOAUTHUNSUP
                                                    : ProxyErrorCode == pecUserPassAuthUnsup ? IDS_PROXYERRUSERPASSAUTHUNSUP
                                                    : ProxyErrorCode == pecUserPassAuthFail ? IDS_PROXYERRUSERPASSFAIL
                                                    : ProxyErrorCode == pecListenUnsup       ? IDS_PROXYERRLISTENUNSUP
                                                                                             : IDS_PROXYOPENCONERROR),
                                            HostAddressWire.c_str(), HostPort);
            stagedError.swap(detail);
        }
        if (formatted)
        {
            errorText.swap(stagedError);
            if (formatText != NULL)
                formatText->swap(stagedFormat);
            ret = TRUE;
        }
    }
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}

BOOL CSocket::GetProxyTimeoutDescr(std::string& text) noexcept
{
    CALL_STACK_MESSAGE1("CSocket::GetProxyTimeoutDescr(dynamic)");

    HANDLES(EnterCriticalSection(&SocketCritSect));
    BOOL ret = FALSE;
    switch (SocketState)
    {
    case ssSocks4_Connect:
    case ssSocks4A_Connect:
    case ssSocks5_Connect:
    case ssSocks5_WaitForMeth:
    case ssSocks5_WaitForLogin:
    case ssHTTP1_1_Connect:
    case ssSocks4_Listen:
    case ssSocks4A_Listen:
    case ssSocks5_Listen:
    case ssSocks5_ListenWaitForMeth:
    case ssSocks5_ListenWaitForLogin:
    case ssHTTP1_1_Listen:
        ret = FTPFormatString(text, "%s", LoadStr(IDS_OPENPROXYSRVCONTIMEOUT));
        break;
    case ssSocks4_WaitForIP:
        ret = FTPFormatString(text, "%s", LoadStr(IDS_GETIPTIMEOUT));
        break;
    }
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}

void CSocket::ProxySendBytes(const char* buf, int bufLen, int index, BOOL* csLeft, BOOL isConnect)
{
    *csLeft = FALSE;
    if (Socket != INVALID_SOCKET && bufLen > 0)
    { // we simplified the situation - if sending 'buf' at once fails, we report an error and that's it
        // (proper handling would wait for FD_WRITE and send the remaining bytes, but this is most likely unnecessary here,
        // 'bufLen' is a small number (around 1000))
        int len = 0;
        if (!SSLConn)
        {
            while (1) // loop necessary because of the 'send' function (if a "would block" error occurs, it reports it only in the next iteration)
            {
                int sentLen = send(Socket, buf + len, bufLen - len, 0);
                if (sentLen != SOCKET_ERROR) // at least something was sent successfully (or rather accepted by Windows, delivery is uncertain)
                {
                    len += sentLen;
                    if (len >= bufLen)
                        break; // stop sending (nothing left)
                }
                else // an error occurred while sending bytes to the proxy server -> done: connect failed
                {
                    ProxyErrorCode = pecSendingBytes;
                    // if WSAEWOULDBLOCK -> nothing else can be sent (Windows no longer has buffer space), send the rest after FD_WRITE (not implemented yet, hopefully unnecessary)
                    ProxyWinError = WSAGetLastError();
                    SocketState = isConnect ? ssConnectFailed : ssListenFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    if (isConnect)
                        ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                    else
                        ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                    *csLeft = TRUE;
                    break;
                }
            }
        }
        else
        {
            while (1) // loop necessary because of the 'send' function (if a "would block" error occurs, it reports it only in the next iteration)
            {
                int sentLen = SSLLib.SSL_write(SSLConn, buf + len, bufLen - len);
                if (sentLen > 0) // at least something was sent successfully (or rather accepted by Windows, delivery is uncertain)
                {
                    len += sentLen;
                    if (len >= bufLen)
                        break; // stop sending (nothing left)
                }
                else // an error occurred while sending bytes to the proxy server -> done: connect failed
                {
                    ProxyErrorCode = pecSendingBytes;
                    // if WSAEWOULDBLOCK -> nothing else can be sent (Windows no longer has buffer space), send the rest after FD_WRITE (not implemented yet, hopefully unnecessary)
                    ProxyWinError = SSLtoWS2Error(SSLLib.SSL_get_error(SSLConn, sentLen));
                    SocketState = isConnect ? ssConnectFailed : ssListenFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    if (isConnect)
                        ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                    else
                        ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                    *csLeft = TRUE;
                    break;
                }
            }
        }
    }
}

void CSocket::ProxyRequestBuildFailed(int index, BOOL* csLeft, BOOL isConnect, DWORD error)
{
    ProxyErrorCode = pecSendingBytes;
    ProxyWinError = error;
    SocketState = isConnect ? ssConnectFailed : ssListenFailed;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    if (isConnect)
        ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION), index);
    else
        ListeningForConnection(INADDR_NONE, 0, TRUE);
    *csLeft = TRUE;
}

void CSocket::Socks4SendRequest(int request, int index, BOOL* csLeft, BOOL isConnect, BOOL isSocks4A)
{
    *csLeft = FALSE;
    if (isSocks4A && HostIP == INADDR_NONE)
        HostIP = inet_addr(HostAddressWire.c_str()); // some proxy servers cannot resolve names
    std::string requestBytes;
    if (!FtpBuildSocks4Request(static_cast<BYTE>(request), HostIP, HostPort,
                               HostAddressWire, ProxyUserBytes, isSocks4A,
                               requestBytes))
    {
        ProxyRequestBuildFailed(index, csLeft, isConnect, ERROR_INVALID_DATA);
        return;
    }
    ProxySendBytes(requestBytes.data(), static_cast<int>(requestBytes.size()), index, csLeft, isConnect);
    FTPSecureWipe(requestBytes);
}

void CSocket::Socks5SendMethods(int index, BOOL* csLeft, BOOL isConnect)
{
    *csLeft = FALSE;
    char buf[10];
    buf[0] = 5;                         // 5 = version
    buf[1] = ProxyUserBytes.empty() ? 1 : 2; // number of methods
    int off = 2;
    if (!ProxyUserBytes.empty())
        buf[off++] = 2; // "user+password"
    buf[off] = 0;       // "none" (anonymous access)
    ProxySendBytes(buf, off + 1, index, csLeft, isConnect);
}

void CSocket::Socks5SendLogin(int index, BOOL* csLeft, BOOL isConnect)
{
    *csLeft = FALSE;
    std::string requestBytes;
    if (!FtpBuildSocks5Login(ProxyUserBytes, ProxyPasswordBytes, requestBytes))
    {
        ProxyRequestBuildFailed(index, csLeft, isConnect, ERROR_INVALID_DATA);
        return;
    }
    ProxySendBytes(requestBytes.data(), static_cast<int>(requestBytes.size()), index, csLeft, isConnect);
    FTPSecureWipe(requestBytes);
}

void CSocket::Socks5SendRequest(int request, int index, BOOL* csLeft, BOOL isConnect)
{
    *csLeft = FALSE;
    if (HostIP == INADDR_NONE)
        HostIP = inet_addr(HostAddressWire.c_str()); // some proxy servers cannot resolve names
    std::string requestBytes;
    if (!FtpBuildSocks5Request(static_cast<BYTE>(request), HostIP, HostPort,
                               HostAddressWire, requestBytes))
    {
        ProxyRequestBuildFailed(index, csLeft, isConnect, ERROR_INVALID_DATA);
        return;
    }
    ProxySendBytes(requestBytes.data(), static_cast<int>(requestBytes.size()), index, csLeft, isConnect);
    FTPSecureWipe(requestBytes);
}

void CSocket::HTTP11SendRequest(int index, BOOL* csLeft)
{
    *csLeft = FALSE;
    std::string requestBytes;
    if (!FtpBuildHttpConnectRequest(HostAddressWire, HostPort,
                                    ProxyUserBytes, ProxyPasswordBytes,
                                    !ProxyUserBytes.empty() || !ProxyPasswordBytes.empty(), requestBytes))
    {
        ProxyRequestBuildFailed(index, csLeft, TRUE, ERROR_INVALID_DATA);
        return;
    }
    ProxySendBytes(requestBytes.data(), static_cast<int>(requestBytes.size()), index, csLeft, TRUE);
    FTPSecureWipe(requestBytes);
}

BOOL CSocket::ProxyReceiveBytes(LPARAM lParam, char* buf, int* read, int index, BOOL isConnect,
                                BOOL isListen, BOOL readOnlyToEOL)
{
    DWORD event = WSAGETSELECTEVENT(lParam);
    BOOL ret = FALSE;
    if (event == FD_READ || event == FD_CLOSE) // FD_CLOSE sometimes arrives before the last FD_READ, so we must first try FD_READ and if it succeeds, post FD_CLOSE again (FD_READ may succeed once more before it)
    {
        BOOL reportError = FALSE;
        DWORD err = WSAGETSELECTERROR(lParam);
        if (err == NO_ERROR)
        {
            if (Socket != INVALID_SOCKET) // the socket is connected
            {
                int len;
                if (!readOnlyToEOL)
                    len = recv(Socket, buf, *read, 0);
                else
                {
                    int i;
                    for (i = 0; i < *read; i++)
                    {
                        int r = recv(Socket, buf + i, 1, 0);
                        if (r != SOCKET_ERROR)
                        {
                            if (buf[i] == '\n')
                            {
                                len = i + 1;
                                break; // stop once LF is read
                            }
                        }
                        else
                        {
                            if (i > 0 && WSAGetLastError() == WSAEWOULDBLOCK)
                                len = i; // there is simply nothing more to read
                            else
                                len = SOCKET_ERROR; // some error occurred or we did not read anything at all
                            break;
                        }
                    }
                    if (i == *read)
                        len = i; // reading ended because of the limited buffer size
                }
                if (len != SOCKET_ERROR) // maybe we read something (0 = the connection is already closed)
                {
                    if (len > 0)
                    {
                        *read = len;
                        ret = TRUE;
                    }
                    else
                    {
                        if (event == FD_CLOSE)
                        {
                            ProxyErrorCode = pecReceivingBytes;
                            ProxyWinError = NO_ERROR; // text IDS_CONNECTIONLOSTERROR
                            SocketState = isConnect ? ssConnectFailed : ssListenFailed;
                            reportError = TRUE;
                        }
                    }
                }
                else
                {
                    err = WSAGetLastError();
                    if (err != WSAEWOULDBLOCK) // if it is not the "nothing to read" error
                    {
                        ProxyErrorCode = pecReceivingBytes;
                        ProxyWinError = err;
                        SocketState = isConnect ? ssConnectFailed : ssListenFailed;
                        reportError = TRUE;
                    }
                }
            }
            if (!ret && !reportError)
                HANDLES(LeaveCriticalSection(&SocketCritSect));
        }
        else
        {
            ProxyErrorCode = pecReceivingBytes;
            ProxyWinError = err;
            SocketState = isConnect ? ssConnectFailed : ssListenFailed;
            reportError = TRUE;
        }
        if (reportError)
        {
            if (isConnect)
            {
                HANDLES(LeaveCriticalSection(&SocketCritSect));
                ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
            }
            else
            {
                if (isListen)
                {
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                }
                else
                {
                    ConnectionAccepted(FALSE, NO_ERROR, TRUE /* proxy error */);
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                }
            }
        }
    }
    else
        HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}

DWORD GetSOCKS4ErrDescr(char replyCode)
{
    switch (replyCode)
    {
    case 92:
        return IDS_PROXYERRNEEDIDENTD; // request rejected because the SOCKS server cannot connect to identd on the client
    case 93:
        return IDS_PROXYERRDIFFUSERS; // request rejected because the client program and identd report different user-ids
    // case 91: // rejected or failed
    default:
        return IDS_PROXYERRREJORFAIL;
    }
}

DWORD GetSOCKS5ErrDescr(char replyCode)
{
    switch (replyCode)
    {
    case 2:
        return IDS_PROXYERRRULESET; // connection not allowed by ruleset
    case 3:
        return IDS_PROXYERRNETUNR; // Network unreachable
    case 4:
        return IDS_PROXYERRHOSTUNR; // Host unreachable
    case 5:
        return IDS_PROXYERRCONREF; // Connection refused
    case 6:
        return IDS_PROXYERRTTLEXP; // TTL expired
    case 7:
        return IDS_PROXYERRCMDNOTSUP; // Command not supported
    case 8:
        return IDS_PROXYERRADRTYPEUNSUP; // Address type not supported
    // case 1: // general SOCKS server failure
    default:
        return IDS_PROXYERRGENFAIL;
    }
}

// Capacity of the proxy-reply buffer in ReceiveNetEventInt below, and the cap on
// every read into it. The HTTP 1.1 CONNECT arm reads a whole response line, which
// is far longer than any fixed SOCKS reply, so this is sized for the largest
// reader rather than the smallest.
static const int PROXY_REPLY_BUF_SIZE = 200;

void CSocket::ReceiveNetEventInt(LPARAM lParam, int index)
{
    //  CALL_STACK_MESSAGE3("CSocket::ReceiveNetEventInt(0x%IX, %d)", lParam, index);

    HANDLES(EnterCriticalSection(&SocketCritSect));
    if (SocketState == ssNoProxyOrConnected)
    {
        HANDLES(LeaveCriticalSection(&SocketCritSect));
        ReceiveNetEvent(lParam, index); // redirect normal operation into ReceiveNetEvent()
    }
    else
    {
        // One constant for the buffer AND for every read into it, so the two can
        // no longer disagree. They did: this was shrunk to char buf[10] to fit
        // "the largest fixed SOCKS reply", but the ssHTTP1_1_WaitForCon arm below
        // reads up to PROXY_REPLY_BUF_SIZE bytes into the same buffer, one byte
        // at a time until LF. Any real proxy response line overran it
        // ("HTTP/1.1 200 Connection established\r\n" is 37 bytes) using bytes
        // that come straight off the network.
        char buf[PROXY_REPLY_BUF_SIZE];
        DWORD err = WSAGETSELECTERROR(lParam);
        DWORD event = WSAGETSELECTEVENT(lParam);
        if (event == FD_WRITE)
            ShouldPostFD_WRITE = TRUE;
        switch (SocketState)
        {
        case ssNotOpened: // the socket should equal INVALID_SOCKET, so execution should never get here
        {                 // it can happen: at the start of the method we enter the critical section, and before entering, another thread inside the critical section sets Socket to INVALID_SOCKET and SocketState to ssNotOpened
            // TRACE_E("CSocket::ReceiveNetEventInt(): unexpected situation: called when SocketState == ssNotOpened");
            HANDLES(LeaveCriticalSection(&SocketCritSect));
            break;
        }

        case ssSocks4_Connect:
        case ssSocks4A_Connect:
        case ssSocks5_Connect:
        {
            if (event == FD_CONNECT)
            {
                if (err != NO_ERROR) // error connecting to the proxy server => done: connect failed
                {
                    SocketState = ssConnectFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ReceiveNetEvent(MAKELPARAM(FD_CONNECT, err), index);
                }
                else // we are connected to the proxy server
                {
                    if (SocketState == ssSocks5_Connect) // SOCKS 5
                    {
                        SocketState = ssSocks5_WaitForMeth;
                        BOOL csLeft;
                        Socks5SendMethods(index, &csLeft, TRUE /* connect */);
                        if (!csLeft)
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                    }
                    else
                    {
                        if (SocketState == ssSocks4A_Connect) // SOCKS 4A
                        {
                            SocketState = ssSocks4A_WaitForCon;
                            BOOL csLeft;
                            Socks4SendRequest(1 /* connect request */, index, &csLeft, TRUE /* connect */, TRUE /* Socks 4A */);
                            if (!csLeft)
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                        }
                        else // SOCKS 4
                        {
                            if (HostIP != INADDR_NONE) // we have the FTP server's IP address, send the CONNECT request
                            {
                                SocketState = ssSocks4_WaitForCon;
                                BOOL csLeft;
                                Socks4SendRequest(1 /* connect request */, index, &csLeft, TRUE /* connect */, FALSE /* Socks 4 */);
                                if (!csLeft)
                                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                            }
                            else // first translate the host address to an IP address
                            {
                                // we are already inside CSocketsThread::CritSect, so it is possible to call this method even from SocketCritSect
                                if (!GetHostByAddress(HostAddress.c_str(), 0)) // error obtaining the IP => done: connect failed
                                {
                                    ProxyErrorCode = pecGettingHostIP;
                                    ProxyWinError = NO_ERROR;
                                    SocketState = ssConnectFailed;
                                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                                    ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                                }
                                else
                                {
                                    SocketState = ssSocks4_WaitForIP;
                                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                                }
                            }
                        }
                    }
                }
            }
            else
                HANDLES(LeaveCriticalSection(&SocketCritSect));
            break;
        }

        case ssSocks4_WaitForIP:
        {
            if (event == FD_CLOSE) // the connection to the proxy server closed => done: connect failed
            {
                SocketState = ssConnectFailed;
                HANDLES(LeaveCriticalSection(&SocketCritSect));
                if (err == NO_ERROR)
                    err = WSAECONNABORTED; // we need to report some error, it simply cannot be a successful connect
                ReceiveNetEvent(MAKELPARAM(FD_CONNECT, err), index);
            }
            else
                HANDLES(LeaveCriticalSection(&SocketCritSect));
            break; // in this state we wait for the IP in ReceiveHostByAddressInt (we do not handle timeout, the global timeout for receiving FD_CONNECT is enough)
        }

        case ssSocks4_WaitForCon:  // waiting for the result of the request to connect to the FTP server (we do not handle timeout, the global timeout for receiving FD_CONNECT is enough)
        case ssSocks4A_WaitForCon: // waiting for the result of the request to connect to the FTP server (we do not handle timeout, the global timeout for receiving FD_CONNECT is enough)
        {
            int read = 8;
            if (ProxyReceiveBytes(lParam, buf, &read, index, TRUE /* connect */, FALSE, FALSE))
            {                                 // process the SOCKS4 proxy server response
                if (read == 8 && buf[0] == 0) // the response should have 8 bytes and the "version" should be 0
                {
                    if (buf[1] == 90) // reply code == success
                    {
                        SocketState = ssNoProxyOrConnected;
                        if (ShouldPostFD_WRITE) // to ensure ReceiveNetEvent() also receives FD_WRITE (FD_READ is generated on its own, so we do not have to post it)
                        {
                            PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                                        (WPARAM)Socket, MAKELPARAM(FD_WRITE, NO_ERROR));
                        }
                        if (event == FD_CLOSE) // this close occurred after a successful connection to the FTP server, it will be processed later...
                        {
                            PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                                        (WPARAM)Socket, lParam);
                        }
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        ReceiveNetEvent(MAKELPARAM(FD_CONNECT, NO_ERROR), index); // announce a successful connect
                    }
                    else // error
                    {
                        ProxyErrorCode = pecProxySrvError;
                        ProxyWinError = GetSOCKS4ErrDescr(buf[1] /* reply code */);
                        SocketState = ssConnectFailed;
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                    }
                }
                else
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssConnectFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                }
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssSocks5_WaitForMeth: // waiting to see which authentication method the server chooses
        {
            int read = 2;
            if (ProxyReceiveBytes(lParam, buf, &read, index, TRUE /* connect */, FALSE, FALSE))
            {                  // process the first response from the SOCKS5 proxy server
                if (read == 2) // the response should have 2 bytes (the first byte is the version, but they do not say what it should be set to, so we ignore it)
                {
                    if (buf[1] == 0 /* anonymous */)
                    {
                        SocketState = ssSocks5_WaitForCon;
                        BOOL csLeft;
                        Socks5SendRequest(1 /* connect request */, index, &csLeft, TRUE /* connect */);
                        if (!csLeft)
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                    }
                    else
                    {
                        if (buf[1] == 2 /* user+password */ && !ProxyUserBytes.empty())
                        {
                            SocketState = ssSocks5_WaitForLogin;
                            BOOL csLeft;
                            Socks5SendLogin(index, &csLeft, TRUE /* connect */);
                            if (!csLeft)
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                        }
                        else // error
                        {
                            // WinGate does not send 0xFF, but always 0/2 depending on whether it wants user+password,
                            // so skip this test: if (buf[1] == 0xFF)  // no acceptable method
                            ProxyErrorCode = ProxyUserBytes.empty() ? pecNoAuthUnsup : pecUserPassAuthUnsup;
                            SocketState = ssConnectFailed;
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                            ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                        }
                    }
                }
                else
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssConnectFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                }
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssSocks5_WaitForLogin: // waiting for the proxy server login result (we sent user+password)
        {
            int read = 2;
            if (ProxyReceiveBytes(lParam, buf, &read, index, TRUE /* connect */, FALSE, FALSE))
            {                  // process the SOCKS5 proxy server response to user+password
                if (read == 2) // the response should have 2 bytes (the first byte is the version, but they do not say what it should be set to, so we ignore it)
                {
                    if (buf[1] == 0) // success
                    {
                        SocketState = ssSocks5_WaitForCon;
                        BOOL csLeft;
                        Socks5SendRequest(1 /* connect request */, index, &csLeft, TRUE /* connect */);
                        if (!csLeft)
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                    }
                    else // the server rejected us, we are done
                    {
                        ProxyErrorCode = pecUserPassAuthFail;
                        SocketState = ssConnectFailed;
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                    }
                }
                else
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssConnectFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                }
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssSocks5_WaitForCon: // waiting for the result of the request to connect to the FTP server
        {
            int read = 10;
            if (ProxyReceiveBytes(lParam, buf, &read, index, TRUE /* connect */, FALSE, FALSE))
            {                                                 // process the SOCKS5 proxy server response to the CONNECT request
                if (read == 10 && buf[0] == 5 && buf[3] == 1) // the response should have 10 bytes, the "version" should be 5, and the address type should be 1 (IPv4)
                {
                    if (buf[1] == 0) // reply code == success
                    {
                        SocketState = ssNoProxyOrConnected;
                        if (ShouldPostFD_WRITE) // to ensure ReceiveNetEvent() also receives FD_WRITE (FD_READ is generated on its own, so we do not have to post it)
                        {
                            PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                                        (WPARAM)Socket, MAKELPARAM(FD_WRITE, NO_ERROR));
                        }
                        if (event == FD_CLOSE) // this close occurred after a successful connection to the FTP server, it will be processed later...
                        {
                            PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                                        (WPARAM)Socket, lParam);
                        }
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        ReceiveNetEvent(MAKELPARAM(FD_CONNECT, NO_ERROR), index); // announce a successful connect
                    }
                    else // error
                    {
                        ProxyErrorCode = pecProxySrvError;
                        ProxyWinError = GetSOCKS5ErrDescr(buf[1] /* reply code */);
                        SocketState = ssConnectFailed;
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                    }
                }
                else
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssConnectFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                }
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssSocks4_Listen:
        case ssSocks4A_Listen:
        case ssSocks5_Listen:
        {
            if (event == FD_CONNECT)
            {
                if (err != NO_ERROR) // error connecting to the proxy server => done: listen failed
                {
                    ProxyErrorCode = pecConPrxSrvError;
                    ProxyWinError = err;
                    SocketState = ssListenFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                }
                else // we are connected to the proxy server, send the LISTEN request
                {
                    if (SocketState == ssSocks5_Listen) // SOCKS 5
                    {
                        SocketState = ssSocks5_ListenWaitForMeth;
                        BOOL csLeft;
                        Socks5SendMethods(index, &csLeft, FALSE /* listen */);
                        if (!csLeft)
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                    }
                    else
                    {
                        if (SocketState == ssSocks4A_Listen) // SOCKS 4A
                        {
                            SocketState = ssSocks4A_WaitForListenRes;
                            BOOL csLeft;
                            Socks4SendRequest(2 /* bind (listen) request */, index, &csLeft, FALSE /* listen */, TRUE /* Socks 4A */);
                            if (!csLeft)
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                        }
                        else // SOCKS 4
                        {
                            SocketState = ssSocks4_WaitForListenRes;
                            BOOL csLeft;
                            Socks4SendRequest(2 /* bind (listen) request */, index, &csLeft, FALSE /* listen */, FALSE /* Socks 4 */);
                            if (!csLeft)
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                        }
                    }
                }
            }
            else
                HANDLES(LeaveCriticalSection(&SocketCritSect));
            break;
        }

        case ssSocks5_ListenWaitForMeth: // waiting to see which authentication method the server chooses
        {
            int read = 2;
            if (ProxyReceiveBytes(lParam, buf, &read, index, FALSE, TRUE /* listen */, FALSE))
            {                  // process the first response from the SOCKS5 proxy server
                if (read == 2) // the response should have 2 bytes (the first byte is the version, but they do not say what it should be set to, so we ignore it)
                {
                    if (buf[1] == 0 /* anonymous */)
                    {
                        SocketState = ssSocks5_WaitForListenRes;
                        BOOL csLeft;
                        Socks5SendRequest(2 /* bind (listen) request */, index, &csLeft, FALSE /* listen */);
                        if (!csLeft)
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                    }
                    else
                    {
                        if (buf[1] == 2 /* user+password */ && !ProxyUserBytes.empty())
                        {
                            SocketState = ssSocks5_ListenWaitForLogin;
                            BOOL csLeft;
                            Socks5SendLogin(index, &csLeft, FALSE /* listen */);
                            if (!csLeft)
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                        }
                        else // error
                        {
                            if (buf[1] == 0xFF) // no acceptable method
                                ProxyErrorCode = ProxyUserBytes.empty() ? pecNoAuthUnsup : pecUserPassAuthUnsup;
                            else
                                ProxyErrorCode = pecUnexpectedReply; // unexpected response (the server selected a different method than we requested)
                            SocketState = ssListenFailed;
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                            ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                        }
                    }
                }
                else
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssListenFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                }
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssSocks5_ListenWaitForLogin: // waiting for the proxy server login result (we sent user+password)
        {
            int read = 2;
            if (ProxyReceiveBytes(lParam, buf, &read, index, FALSE, TRUE /* listen */, FALSE))
            {                  // process the SOCKS5 proxy server response to user+password
                if (read == 2) // the response should have 2 bytes (the first byte is the version, but they do not say what it should be set to, so we ignore it)
                {
                    if (buf[1] == 0) // success
                    {
                        SocketState = ssSocks5_WaitForListenRes;
                        BOOL csLeft;
                        Socks5SendRequest(2 /* bind (listen) request */, index, &csLeft, FALSE /* listen */);
                        if (!csLeft)
                            HANDLES(LeaveCriticalSection(&SocketCritSect));
                    }
                    else // the server rejected us, we are done
                    {
                        ProxyErrorCode = pecUserPassAuthFail;
                        SocketState = ssListenFailed;
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                    }
                }
                else
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssListenFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                }
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssSocks4_WaitForListenRes:  // waiting for the proxy to open a port for "listen" and return the IP+port where it listens or an error (timeout should be handled outside - timeout for calling ListeningForConnection())
        case ssSocks4A_WaitForListenRes: // waiting for the proxy to open a port for "listen" and return the IP+port where it listens or an error (timeout should be handled outside - timeout for calling ListeningForConnection())
        {
            int read = 8;
            if (ProxyReceiveBytes(lParam, buf, &read, index, FALSE, TRUE /* listen */, FALSE))
            {                                 // process the SOCKS4 proxy server response
                if (read == 8 && buf[0] == 0) // the response should have 8 bytes + "version" should be 0
                {
                    if (buf[1] == 90) // reply code == success
                    {
                        if (SocketState == ssSocks4A_WaitForListenRes)
                            SocketState = ssSocks4A_WaitForAccept;
                        else
                            SocketState = ssSocks4_WaitForAccept;
                        DWORD ip = *(DWORD*)(buf + 4);
                        if (ip == 0)
                            ip = ProxyIP; // the proxy server wants us to use its IP address here
                        int port = ntohs(*(unsigned short*)(buf + 2));
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        ListeningForConnection(ip, port, FALSE); // report the IP+port where we wait for the connection from the FTP server
                    }
                    else // error
                    {
                        ProxyErrorCode = pecProxySrvError;
                        ProxyWinError = GetSOCKS4ErrDescr(buf[1] /* reply code */);
                        SocketState = ssListenFailed;
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                    }
                }
                else
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssListenFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                }
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssSocks5_WaitForListenRes: // waiting for the proxy to open a port for "listen" and return the IP+port where it listens or an error (timeout should be handled outside - timeout for calling ListeningForConnection())
        {
            int read = 10;
            if (ProxyReceiveBytes(lParam, buf, &read, index, FALSE, TRUE /* listen */, FALSE))
            {                                                 // process the SOCKS5 proxy server response
                if (read == 10 && buf[0] == 5 && buf[3] == 1) // the response should have 10 bytes + "version" should be 5 + address type should be 1 (IPv4)
                {
                    if (buf[1] == 0) // reply code == success
                    {
                        SocketState = ssSocks5_WaitForAccept;
                        DWORD ip = *(DWORD*)(buf + 4);
                        if (ip == 0)
                            ip = ProxyIP; // not in the documentation, but "antinat" returns zero = apparently expects the same behavior as SOCKS4/4A: the proxy server wants us to use its IP address here
                        int port = ntohs(*(unsigned short*)(buf + 8));
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        ListeningForConnection(ip, port, FALSE); // report the IP+port where we wait for the connection from the FTP server
                    }
                    else // error
                    {
                        ProxyErrorCode = pecProxySrvError;
                        ProxyWinError = GetSOCKS5ErrDescr(buf[1] /* reply code */);
                        SocketState = ssListenFailed;
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                        ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                    }
                }
                else
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssListenFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
                }
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssSocks4_WaitForAccept:  // waiting for the proxy to accept a connection from the FTP server or return an error (we do not handle timeout, the global timeout for the ConnectionAccepted() method is enough)
        case ssSocks4A_WaitForAccept: // waiting for the proxy to accept a connection from the FTP server or return an error (we do not handle timeout, the global timeout for the ConnectionAccepted() method is enough)
        {
            int read = 8;
            if (ProxyReceiveBytes(lParam, buf, &read, index, FALSE, FALSE /* accept */, FALSE))
            {                                 // process the SOCKS4 proxy server response
                if (read == 8 && buf[0] == 0) // the response should have 8 bytes and the "version" should be 0
                {
                    if (buf[1] == 90) // reply code == success
                    {
                        SocketState = ssNoProxyOrConnected;
                        if (ShouldPostFD_WRITE) // to ensure ReceiveNetEvent() also receives FD_WRITE (FD_READ is generated on its own, so we do not have to post it)
                        {
                            PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                                        (WPARAM)Socket, MAKELPARAM(FD_WRITE, NO_ERROR));
                        }
                        if (event == FD_CLOSE) // this close occurred after a successful connection from the FTP server, it will be processed later...
                        {
                            PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                                        (WPARAM)Socket, lParam);
                        }
                        ConnectionAccepted(TRUE, NO_ERROR, FALSE); // announce a successful connection establishment
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                    }
                    else // error - connection from an unknown IP
                    {
                        ProxyErrorCode = pecProxySrvError;
                        ProxyWinError = IDS_PROXYERRINVHOST;
                        SocketState = ssListenFailed;
                        ConnectionAccepted(FALSE, NO_ERROR, TRUE /* proxy error */);
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                    }
                }
                else
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssListenFailed;
                    ConnectionAccepted(FALSE, NO_ERROR, TRUE /* proxy error */);
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                }
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssSocks5_WaitForAccept: // waiting for the proxy to accept a connection from the FTP server (or return an error)
        {
            int read = 10;
            if (ProxyReceiveBytes(lParam, buf, &read, index, FALSE, FALSE /* accept */, FALSE))
            {                                                 // process the SOCKS5 proxy server response
                if (read == 10 && buf[0] == 5 && buf[3] == 1) // the response should have 10 bytes, the "version" should be 5, and the address type should be 1 (IPv4)
                {
                    if (buf[1] == 0) // reply code == success
                    {
                        SocketState = ssNoProxyOrConnected;
                        if (ShouldPostFD_WRITE) // to ensure ReceiveNetEvent() also receives FD_WRITE (FD_READ is generated on its own, so we do not have to post it)
                        {
                            PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                                        (WPARAM)Socket, MAKELPARAM(FD_WRITE, NO_ERROR));
                        }
                        if (event == FD_CLOSE) // this close occurred after a successful connection from the FTP server, it will be processed later...
                        {
                            PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                                        (WPARAM)Socket, lParam);
                        }
                        ConnectionAccepted(TRUE, NO_ERROR, FALSE); // announce a successful connection establishment
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                    }
                    else // error
                    {
                        ProxyErrorCode = pecProxySrvError;
                        ProxyWinError = GetSOCKS5ErrDescr(buf[1] /* reply code */);
                        SocketState = ssListenFailed;
                        ConnectionAccepted(FALSE, NO_ERROR, TRUE /* proxy error */);
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                    }
                }
                else
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssListenFailed;
                    ConnectionAccepted(FALSE, NO_ERROR, TRUE /* proxy error */);
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                }
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssHTTP1_1_Connect:
        {
            if (event == FD_CONNECT)
            {
                if (err != NO_ERROR) // error connecting to the proxy server => done: connect failed
                {
                    SocketState = ssConnectFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    ReceiveNetEvent(MAKELPARAM(FD_CONNECT, err), index);
                }
                else // we are connected to the proxy server
                {
                    SocketState = ssHTTP1_1_WaitForCon;
                    HTTP11_FirstLineOfReply.clear();
                    HTTP11_EmptyRowCharsReceived = 0;
                    BOOL csLeft;
                    HTTP11SendRequest(index, &csLeft);
                    if (!csLeft)
                        HANDLES(LeaveCriticalSection(&SocketCritSect));
                }
            }
            else
                HANDLES(LeaveCriticalSection(&SocketCritSect));
            break;
        }

        case ssHTTP1_1_WaitForCon: // HTTP 1.1 - CONNECT: waiting for the result of the request to connect to the FTP server
        {
            int read = PROXY_REPLY_BUF_SIZE;
            if (ProxyReceiveBytes(lParam, buf, &read, index, TRUE /* connect */, FALSE,
                                  TRUE /* read only to first LF */) &&
                read > 0)
            { // process another part of the HTTP1.1 proxy server response to the CONNECT request
                if (HTTP11_EmptyRowCharsReceived > 0 && HTTP11_EmptyRowCharsReceived != 4)
                {
                    const char* endline = "\r\n\r\n" + HTTP11_EmptyRowCharsReceived;
                    int i;
                    for (i = 0; i < read; i++)
                    {
                        if (*endline++ == buf[i])
                        {
                            if (++HTTP11_EmptyRowCharsReceived == 4)
                                break;
                        }
                        else
                        {
                            HTTP11_EmptyRowCharsReceived = 0;
                            break;
                        }
                    }
                }
                if (HTTP11_EmptyRowCharsReceived == 0)
                {
                    if (buf[read - 1] == '\r')
                        HTTP11_EmptyRowCharsReceived = 1;
                    if (read > 1 && buf[read - 2] == '\r' && buf[read - 1] == '\n')
                        HTTP11_EmptyRowCharsReceived = 2;
                }
                BOOL unexpReply = FALSE;
                size_t len = HTTP11_FirstLineOfReply.size();
                if (len == 0 || HTTP11_FirstLineOfReply.back() != '\n') // only if the entire first line has not been read yet
                {
                    try
                    {
                        HTTP11_FirstLineOfReply.append(buf, static_cast<size_t>(read));
                        len = HTTP11_FirstLineOfReply.size();
                    }
                    catch (...)
                    {
                        TRACE_E(LOW_MEMORY);
                        unexpReply = TRUE; // low probability of an error, simulate a bad server response...
                    }
                }

                BOOL csLeft = FALSE;
                if (!unexpReply && len > 0 && HTTP11_FirstLineOfReply.back() == '\n')
                { // if the entire first response line has already been read
                    if (_strnicmp("HTTP/", HTTP11_FirstLineOfReply.c_str(), 5) == 0)
                    {
                        const char* s = HTTP11_FirstLineOfReply.c_str() + 5;
                        while (*s != 0 && *s > ' ')
                            s++;
                        while (*s != 0 && *s <= ' ')
                            s++;
                        if (*s != 0)
                        {
                            if (*s == '2') // success
                            {
                                if (HTTP11_EmptyRowCharsReceived == 4) // we have also read the end of the response (an empty line)
                                {
                                    // upon successful connection discard the text of the first response line, it is no longer useful
                                    HTTP11_FirstLineOfReply.clear();

                                    SocketState = ssNoProxyOrConnected;
                                    if (ShouldPostFD_WRITE) // to ensure ReceiveNetEvent() also receives FD_WRITE (FD_READ is generated on its own, so we do not have to post it)
                                    {
                                        PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                                                    (WPARAM)Socket, MAKELPARAM(FD_WRITE, NO_ERROR));
                                    }
                                    if (event == FD_CLOSE) // this close occurred after a successful connection to the FTP server, it will be processed later...
                                    {
                                        PostMessage(SocketsThread->GetHiddenWindow(), WM_APP_SOCKET_MIN + index,
                                                    (WPARAM)Socket, lParam);
                                    }
                                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                                    csLeft = TRUE;
                                    ReceiveNetEvent(MAKELPARAM(FD_CONNECT, NO_ERROR), index); // announce a successful connect
                                }
                            }
                            else // the proxy server reports an error
                            {
                                ProxyErrorCode = pecHTTPProxySrvError; // the message will be taken from HTTP11_FirstLineOfReply
                                SocketState = ssConnectFailed;
                                HANDLES(LeaveCriticalSection(&SocketCritSect));
                                csLeft = TRUE;
                                ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                            }
                        }
                        else
                            unexpReply = TRUE;
                    }
                    else
                        unexpReply = TRUE;
                }
                if (unexpReply)
                {
                    ProxyErrorCode = pecUnexpectedReply;
                    SocketState = ssConnectFailed;
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
                    csLeft = TRUE;
                    ReceiveNetEvent(MAKELPARAM(FD_CONNECT, ERROR_INVALID_FUNCTION /* it just must not be NO_ERROR */), index);
                }
                if (!csLeft)
                    HANDLES(LeaveCriticalSection(&SocketCritSect));
            }
            // else HANDLES(LeaveCriticalSection(&SocketCritSect));  // ProxyReceiveBytes() returns FALSE = the SocketCritSect section has already been left
            break;
        }

        case ssHTTP1_1_Listen:
        {
            if (event == FD_CONNECT)
            {
                ProxyErrorCode = pecListenUnsup;
                SocketState = ssListenFailed;
                HANDLES(LeaveCriticalSection(&SocketCritSect));
                ListeningForConnection(INADDR_NONE, 0, TRUE /* proxy error */);
            }
            else
                HANDLES(LeaveCriticalSection(&SocketCritSect));
            break;
        }

        case ssConnectFailed:
        case ssListenFailed:
        {
            HANDLES(LeaveCriticalSection(&SocketCritSect));
            break; // we already reported the connect error, nothing else to do
        }

        default:
        {
            TRACE_E("CSocket::ReceiveNetEventInt(): unknown SocketState: " << SocketState);
            HANDLES(LeaveCriticalSection(&SocketCritSect));
            break;
        }
        }
    }
}

void CSocket::ReceiveNetEvent(LPARAM lParam, int index)
{
    CALL_STACK_MESSAGE3("CSocket::ReceiveNetEvent(0x%IX, %d)", lParam, index);
    DWORD eventError = WSAGETSELECTERROR(lParam); // extract error code of event
    switch (WSAGETSELECTEVENT(lParam))
    {
    case FD_CLOSE:
    {
        DWORD error;
        if (eventError == NO_ERROR) // so far it looks like a graceful close
        {
            // sequence according to the help: "Graceful Shutdown, Linger Options, and Socket Closure"

            HANDLES(EnterCriticalSection(&SocketCritSect));

            // according to the help for "shutdown" we should also call recv (most likely unnecessary - we ignore the data) +
            // the server is performing the shutdown, so with incorrect handling of FD_CLOSE there may remain unread data on the socket (we only
            // print a warning for the programmer)
            if (Socket != INVALID_SOCKET) // the socket is connected (otherwise CloseSocket reports the "not connected" error)
            {
                char buf[500];
                if (!SSLConn)
                {
                    while (1)
                    {
                        int r = recv(Socket, buf, 500, 0);
                        if (r == SOCKET_ERROR || r == 0)
                            break; // loop until an error or zero (0 = gracefully closed)
                        else
                        {
                            if (OurShutdown) // shutdown was initiated by the client
                                TRACE_E("Unexpected: please inform Petr Solin: recv() read some bytes in FD_CLOSE on " << index);
                            else // shutdown was initiated by the server
                                TRACE_E("Probably invalid handling of FD_CLOSE: recv() read some bytes in FD_CLOSE on " << index);
                        }
                    }
                }
                else
                {
                    while (1)
                    {
                        int r = SSLLib.SSL_read(SSLConn, buf, 500);
                        if (r <= 0)
                            break; // loop until an error or zero (0 = gracefully closed)
                        else
                        {
                            if (OurShutdown) // shutdown was initiated by the client
                                TRACE_E("Unexpected: please inform Petr Solin: SSL_read() read some bytes in FD_CLOSE on " << index);
                            else // shutdown was initiated by the server
                                TRACE_E("Probably invalid handling of FD_CLOSE: SSL_read() read some bytes in FD_CLOSE on " << index);
                        }
                    }
                }
            }
            BOOL ourShutdown = OurShutdown;
            HANDLES(LeaveCriticalSection(&SocketCritSect));

            if (!ourShutdown) // shutdown was initiated by the server
            {
                if (!Shutdown(&error))
                    eventError = error;
            }
        }
        if (!CloseSocket(&error) && eventError == NO_ERROR)
            eventError = error;

        SocketWasClosed(eventError); // report the socket closure (and possibly the error)
        break;
    }

    case FD_ACCEPT:
    {
        HWND socketsWindow = SocketsThread->GetHiddenWindow(); // to keep it outside the SocketCritSect section

        HANDLES(EnterCriticalSection(&SocketCritSect));

#ifdef _DEBUG
        if (SocketCritSect.RecursionCount > 1)
            TRACE_E("Incorrect call to CSocket::ReceiveNetEvent(FD_ACCEPT): from section SocketCritSect!");
#endif

        if (eventError == NO_ERROR) // only accept without error, ignore the rest (the connection is retried until timeout)
        {
            if (Socket != INVALID_SOCKET) // the socket is connected (otherwise ignore it - the user probably aborted)
            {
                SOCKADDR_IN addr;
                memset(&addr, 0, sizeof(addr));
                int len = sizeof(addr);
                SOCKET sock = accept(Socket, (SOCKADDR*)&addr, &len);
                if (sock != INVALID_SOCKET &&
                    // according to the help calling WSAAsyncSelect is not necessary, but supposedly some versions of Windows Sockets
                    // do not do it automatically, so it must be done - an anomaly occurred: when stepping through sending
                    // the LIST command it generates FD_XXX twice (the second set arrives after the first FD_CLOSE -> it is undeliverable)
                    WSAAsyncSelect(sock, socketsWindow, Msg, FD_READ | FD_WRITE | FD_CLOSE) != SOCKET_ERROR)
                {
                    closesocket(Socket); // close the "listen" socket, it will no longer be needed - calling CloseSocketEx is undesirable
                    Socket = sock;
                    OurShutdown = FALSE;
                    ConnectionAccepted(TRUE, NO_ERROR, FALSE);
                }
                else // error - at least log it
                {
                    DWORD err = WSAGetLastError();
                    if (sock != INVALID_SOCKET)
                        closesocket(sock);
                    ConnectionAccepted(FALSE, err, FALSE);
                }
            }
        }
        HANDLES(LeaveCriticalSection(&SocketCritSect));
        break;
    }
    }
}

void CSocket::SetMsgIndex(int index)
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    if (Msg != -1)
        TRACE_E("CSocket::SetMsgIndex(): message index has already beet set!");
    Msg = index + WM_APP_SOCKET_MIN;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
}

void CSocket::ResetMsgIndex()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    Msg = -1;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
}

int CSocket::GetMsgIndex()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    int msg = Msg;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    if (msg != -1)
        msg -= WM_APP_SOCKET_MIN;
    return msg;
}

int CSocket::GetMsg()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    int msg = Msg;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return msg;
}

int CSocket::GetUID()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    int uid = UID;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return uid;
}

SOCKET
CSocket::GetSocket()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    SOCKET sock = Socket;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return sock;
}

void CSocket::SetCertificate(CCertificate* certificate)
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    CCertificate* old = pCertificate; // ensures AddRef is called via Release (in case pCertificate == certificate)
    pCertificate = certificate;
    if (pCertificate)
        pCertificate->AddRef();
    if (old)
        old->Release();
    HANDLES(LeaveCriticalSection(&SocketCritSect));
}

CCertificate*
CSocket::GetCertificate()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    CCertificate* ret = pCertificate;
    if (ret != NULL)
        ret->AddRef();
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}

void CSocket::SwapSockets(CSocket* sock)
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    HANDLES(EnterCriticalSection(&sock->SocketCritSect)); // will not deadlock unless SwapSockets() is called simultaneously with reversed parameters (opposite order of entering sections)
    int swapMsg = Msg;
    Msg = sock->Msg;
    sock->Msg = swapMsg;
    SOCKET swapSocket = Socket;
    Socket = sock->Socket;
    sock->Socket = swapSocket;
    SSL* swapSSLConn = SSLConn;
    SSLConn = sock->SSLConn;
    sock->SSLConn = swapSSLConn;
    int swapReuseSSLSession = ReuseSSLSession;
    ReuseSSLSession = sock->ReuseSSLSession;
    sock->ReuseSSLSession = swapReuseSSLSession;
    BOOL swapReuseSSLSessionFailed = ReuseSSLSessionFailed;
    ReuseSSLSessionFailed = sock->ReuseSSLSessionFailed;
    sock->ReuseSSLSessionFailed = swapReuseSSLSessionFailed;
    CCertificate* swapSSLCert = pCertificate;
    pCertificate = sock->pCertificate;
    sock->pCertificate = swapSSLCert;
    BOOL swapOurShutdown = OurShutdown;
    OurShutdown = sock->OurShutdown;
    sock->OurShutdown = swapOurShutdown;
    CSocketState swapSocketState = SocketState;
    SocketState = sock->SocketState;
    sock->SocketState = swapSocketState;
    HostAddress.swap(sock->HostAddress);
    HostAddressWire.swap(sock->HostAddressWire);
    DWORD swapHostIP = HostIP;
    HostIP = sock->HostIP;
    sock->HostIP = swapHostIP;
    unsigned short swapHostPort = HostPort;
    HostPort = sock->HostPort;
    sock->HostPort = swapHostPort;
    ProxyUserBytes.swap(sock->ProxyUserBytes);
    ProxyPasswordBytes.swap(sock->ProxyPasswordBytes);
    DWORD swapProxyIP = ProxyIP;
    ProxyIP = sock->ProxyIP;
    sock->ProxyIP = swapProxyIP;
    CProxyErrorCode swapProxyErrorCode = ProxyErrorCode;
    ProxyErrorCode = sock->ProxyErrorCode;
    sock->ProxyErrorCode = swapProxyErrorCode;
    DWORD swapProxyWinError = ProxyWinError;
    ProxyWinError = sock->ProxyWinError;
    sock->ProxyWinError = swapProxyWinError;
    HANDLES(LeaveCriticalSection(&sock->SocketCritSect));
    HANDLES(LeaveCriticalSection(&SocketCritSect));
}

BOOL CSocket::GetIsSocketConnectedLastCallTime(DWORD* lastCallTime)
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    BOOL ret = FALSE;
    if (IsSocketConnectedLastCallTime != 0)
    {
        ret = TRUE;
        *lastCallTime = IsSocketConnectedLastCallTime;
    }
    HANDLES(LeaveCriticalSection(&SocketCritSect));
    return ret;
}

void CSocket::SetIsSocketConnectedLastCallTime()
{
    HANDLES(EnterCriticalSection(&SocketCritSect));
    IsSocketConnectedLastCallTime = GetTickCount();
    if (IsSocketConnectedLastCallTime == 0)
        IsSocketConnectedLastCallTime = 1;
    HANDLES(LeaveCriticalSection(&SocketCritSect));
}

//
// ****************************************************************************
// CSocketsThread
//

CSocketsThread::CSocketsThread()
    : CThread(L"Sockets"), Sockets(100, 100), MsgData(10, 10), Timers(50, 100), PostMsgs(50, 100)
{
    HANDLES(InitializeCriticalSection(&CritSect));
    RunningEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL)); // manual, non-signaled
    CanEndThread = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL)); // manual, non-signaled
    Running = FALSE;
    HWindow = NULL;
    FirstFreeIndexInSockets = -1;
    LockedTimers = -1;
    Terminating = FALSE;
}

CSocketsThread::~CSocketsThread()
{
    if (MsgData.Count > 0)
        TRACE_E("FTP Client: Zustalo " << MsgData.Count << " nedorucenych zprav WM_APP_SOCKET_ADDR.");
    if (PostMsgs.Count > 0)
        TRACE_E("FTP Client: Zustalo " << PostMsgs.Count << " nedorucenych zprav WM_APP_SOCKET_POSTMSG.");
    if (RunningEvent != NULL)
        HANDLES(CloseHandle(RunningEvent));
    if (CanEndThread != NULL)
        HANDLES(CloseHandle(CanEndThread));
    HANDLES(DeleteCriticalSection(&CritSect));
    if (Sockets.Count > 0)
    {
        TRACE_I("FTP Client: Bylo otevreno nejvice " << Sockets.Count << " socketu zaroven.");
        int c = 0;
        int i;
        for (i = 0; i < Sockets.Count; i++)
            if (Sockets[i] != NULL)
                c++;
        if (c != 0)
            TRACE_E("FTP Client: Zustalo otevreno " << c << " socketu!");
    }
}

void CSocketsThread::Terminate()
{
    CALL_STACK_MESSAGE1("CSocketsThread::Terminate()");
    HANDLES(EnterCriticalSection(&CritSect));
    if (HWindow != NULL)
    {
        KillTimer(HWindow, SOCKETSTHREAD_TIMERID); // stop the timer
        PostMessage(HWindow, WM_CLOSE, 0, 0);      // send the window a termination message
    }
    Terminating = TRUE; // the Windows timer can no longer be started
    HANDLES(LeaveCriticalSection(&CritSect));
}

BOOL CSocketsThread::AddSocket(CSocket* sock)
{
    CALL_STACK_MESSAGE1("CSocketsThread::AddSocket()");
    BOOL ret = TRUE;
    HANDLES(EnterCriticalSection(&CritSect));
    if (FirstFreeIndexInSockets != -1) // do we have a place to insert a new socket?
    {
        // insert 'sock' into the array
        Sockets[FirstFreeIndexInSockets] = sock;
        sock->SetMsgIndex(FirstFreeIndexInSockets);

        // search for the next free slot in the array
        int i;
        for (i = FirstFreeIndexInSockets + 1; i < Sockets.Count; i++)
        {
            if (Sockets[i] == NULL)
            {
                FirstFreeIndexInSockets = i;
                break;
            }
        }
        if (i >= Sockets.Count)
            FirstFreeIndexInSockets = -1;
    }
    else
    {
        if (WM_APP_SOCKET_MAX - WM_APP_SOCKET_MIN >= Sockets.Count) // can we fit a new socket?
        {
            int index = Sockets.Add(sock);
            if (Sockets.IsGood())
                sock->SetMsgIndex(index);
            else
            {
                Sockets.ResetState();
                ret = FALSE;
            }
        }
        else
        {
            TRACE_E("CSocketsThread::AddSocket(): Unable to add new socket - limit reached (count=" << Sockets.Count << ")");
            ret = FALSE;
        }
    }
    HANDLES(LeaveCriticalSection(&CritSect));
    return ret;
}

void CSocketsThread::DeleteSocketFromIndex(int index, BOOL onlyDetach)
{
    CALL_STACK_MESSAGE3("CSocketsThread::DeleteSocketFromIndex(%d, %d)", index, onlyDetach);
    HANDLES(EnterCriticalSection(&CritSect));
    if (index >= 0 && index < Sockets.Count && Sockets[index] != NULL)
    {
        if (!onlyDetach)
        {
#ifdef _DEBUG
            BOOL old = InDeleteSocket;
            InDeleteSocket = TRUE; // we may not be directly in ::DeleteSocket, but the call is correct
#endif
            delete Sockets[index]; // deallocate the socket object
#ifdef _DEBUG
            InDeleteSocket = old;
#endif
        }
        else
            Sockets[index]->ResetMsgIndex(); // detach the socket object
        Sockets[index] = NULL;               // free the position in the array (we do not shrink the array, it is not expected and would be unnecessary)
        if (FirstFreeIndexInSockets == -1 || index < FirstFreeIndexInSockets)
            FirstFreeIndexInSockets = index;
    }
    else
        TRACE_E("CSocketsThread::DeleteSocketFromIndex(): Pokus o vymaz neplatneho prvku pole!");
    HANDLES(LeaveCriticalSection(&CritSect));
}

void CSocketsThread::BeginSocketsSwap(CSocket* sock1, CSocket* sock2)
{
    CALL_STACK_MESSAGE1("CSocketsThread::BeginSocketsSwap(,)");

    HANDLES(EnterCriticalSection(&CritSect));
    int index1 = sock1->GetMsgIndex();
    int index2 = sock2->GetMsgIndex();
    if (index1 != -1 && index2 != -1)
    {
        // swap the internal data of the socket object (only data of the CSocket class, the caller handles the rest)
        sock1->SwapSockets(sock2);
        // swap the objects in the array of handled sockets
        Sockets[index1] = sock2;
        Sockets[index2] = sock1;
    }
    else
        TRACE_E("Incorrect use of CSocketsThread::BeginSocketsSwap(): at least one socket is not in Sockets array!");
}

void CSocketsThread::EndSocketsSwap()
{
    HANDLES(LeaveCriticalSection(&CritSect));
}

BOOL CSocketsThread::PostHostByAddressResult(int socketMsg, int socketUID, DWORD ip, int hostUID, int err)
{
    CALL_STACK_MESSAGE6("CSocketsThread::PostHostByAddressResult(%d, %d, 0x%X, %d, %d)",
                        socketMsg, socketUID, ip, hostUID, err);
    BOOL ret = FALSE;
    HANDLES(EnterCriticalSection(&CritSect));
    if (HWindow != NULL)
    {
        CMsgData* data = new CMsgData(socketMsg, socketUID, ip, hostUID, err);
        if (data != NULL)
        {
            MsgData.Add(data);
            if (MsgData.IsGood())
            {
                if (PostMessage(HWindow, WM_APP_SOCKET_ADDR, 0, 0))
                    ret = TRUE;
                else
                {
                    DWORD error = GetLastError();
                    TRACE_E("PostMessage has failed: err=" << error);
                    MsgData.Delete(MsgData.Count - 1);
                    if (!MsgData.IsGood())
                        MsgData.ResetState();
                }
            }
            else
            {
                MsgData.ResetState();
                delete data;
            }
        }
        else
            TRACE_E(LOW_MEMORY);
    }
    HANDLES(LeaveCriticalSection(&CritSect));
    return ret;
}

void CSocketsThread::ReceiveMsgData()
{
    HANDLES(EnterCriticalSection(&CritSect));
    if (MsgData.Count > 0)
    {
        CMsgData* data = MsgData[0]; // take the first unprocessed data inserted into the array
        int index = data->SocketMsg - WM_APP_SOCKET_MIN;
        if (index >= 0 && index < Sockets.Count) // "always true"
        {
            CSocket* s = Sockets[index];
            if (s != NULL && s->GetUID() == data->SocketUID) // it is the recipient of the message (it was waiting)
                s->ReceiveHostByAddressInt(data->IP, data->HostUID, data->Err, index);
            else // IP that did not arrive because the socket was cancelled or swapped,
            {    // when sockets are swapped, find the target socket sequentially and deliver the IP
                int i;
                for (i = 0; i < Sockets.Count; i++)
                {
                    CSocket* s2 = Sockets[i];
                    if (s2 != NULL && s2->GetUID() == data->SocketUID) // it is the recipient of the message (it was waiting)
                    {
                        s2->ReceiveHostByAddressInt(data->IP, data->HostUID, data->Err, i);
                        break;
                    }
                }
#ifdef _DEBUG
                if (i == Sockets.Count)
                {
                    // print a warning that the IP was lost (did not reach the socket object)
                    TRACE_I("Lost IP-address with host-UID " << data->HostUID << " for UID " << data->SocketUID);
                }
#endif
            }
        }
        else
            TRACE_E("Unexpected situation in CSocketsThread::ReceiveMsgData()");
        MsgData.Delete(0);
        if (!MsgData.IsGood())
            MsgData.ResetState();
    }
    else
        TRACE_E("Unexpected situation in CSocketsThread::ReceiveMsgData(): no data in array!");
    HANDLES(LeaveCriticalSection(&CritSect));
}

static BOOL IsValidPostedSocketTarget(int socketMsg, int socketUID)
{
    return socketMsg >= WM_APP_SOCKET_MIN && socketMsg <= WM_APP_SOCKET_MAX &&
           socketUID != -1;
}

BOOL CSocketsThread::PostSocketMessage(int socketMsg, int socketUID, DWORD id, void* param)
{
    CALL_STACK_MESSAGE4("CSocketsThread::PostSocketMessage(%d, %d, %u,)", socketMsg, socketUID, id);
    if (!IsValidPostedSocketTarget(socketMsg, socketUID))
    {
        TRACE_I("Dropping socket post-message for invalid target: msg=" << socketMsg << " uid=" << socketUID << " id=" << id);
        return FALSE;
    }

    BOOL ret = FALSE;
    HANDLES(EnterCriticalSection(&CritSect));
    if (HWindow != NULL)
    {
        CPostMsgData* data = new CPostMsgData(socketMsg, socketUID, id, param);
        if (data != NULL)
        {
            PostMsgs.Add(data);
            if (PostMsgs.IsGood())
            {
                if (PostMessage(HWindow, WM_APP_SOCKET_POSTMSG, 0, 0))
                    ret = TRUE;
                else
                {
                    DWORD error = GetLastError();
                    TRACE_E("PostMessage has failed: err=" << error);
                    PostMsgs.Delete(PostMsgs.Count - 1);
                    if (!PostMsgs.IsGood())
                        PostMsgs.ResetState();
                }
            }
            else
            {
                PostMsgs.ResetState();
                delete data;
            }
        }
        else
            TRACE_E(LOW_MEMORY);
    }
    HANDLES(LeaveCriticalSection(&CritSect));
    return ret;
}

BOOL CSocketsThread::IsSocketConnected(int socketUID, BOOL* isConnected)
{
    CALL_STACK_MESSAGE2("CSocketsThread::IsSocketConnected(%d,)", socketUID);
    if (isConnected != NULL)
        *isConnected = FALSE;
    BOOL ret = FALSE;
    HANDLES(EnterCriticalSection(&CritSect));
    int i;
    for (i = 0; i < Sockets.Count; i++)
    {
        CSocket* s = Sockets[i];
        if (s != NULL && s->GetUID() == socketUID) // this is the socket we are looking for
        {
            ret = TRUE;
            if (isConnected != NULL)
                *isConnected = s->IsConnected();
            s->SetIsSocketConnectedLastCallTime();
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&CritSect));
    return ret;
}

void CSocketsThread::ReceivePostMessage()
{
    HANDLES(EnterCriticalSection(&CritSect));
    if (PostMsgs.Count > 0)
    {
        CPostMsgData* data = PostMsgs[0]; // take the first unprocessed data inserted into the array
        int index = data->SocketMsg - WM_APP_SOCKET_MIN;
        if (index >= 0 && index < Sockets.Count) // "always true"
        {
            CSocket* s = Sockets[index];
            if (s != NULL && s->GetUID() == data->SocketUID) // it is the recipient of the message (it was waiting)
                s->ReceivePostMessage(data->ID, data->Param);
            else // a message that did not arrive because the socket was cancelled or swapped,
            {    // when sockets are swapped, find the target socket sequentially and deliver the message
                int i;
                for (i = 0; i < Sockets.Count; i++)
                {
                    CSocket* s2 = Sockets[i];
                    if (s2 != NULL && s2->GetUID() == data->SocketUID) // it is the recipient of the message (it was waiting)
                    {
                        s2->ReceivePostMessage(data->ID, data->Param);
                        break;
                    }
                }
                if (i == Sockets.Count)
                {
                    // print a warning that the message was lost (did not reach the socket object)
                    TRACE_I("Lost post-message " << data->ID << " for UID " << data->SocketUID);
                }
            }
        }
        else
            TRACE_I("Dropping socket post-message for out-of-range target: msg=" << data->SocketMsg << " uid=" << data->SocketUID << " id=" << data->ID);
        PostMsgs.Delete(0);
        if (!PostMsgs.IsGood())
            PostMsgs.ResetState();
    }
    else
        TRACE_E("Unexpected situation in CSocketsThread::ReceivePostMessage(): no data in array!");
    HANDLES(LeaveCriticalSection(&CritSect));
}

int CSocketsThread::FindIndexForNewTimer(DWORD timeoutAbs, int leftIndex)
{
    if (leftIndex >= Timers.Count)
        return leftIndex;

    // all times must be related to the nearest timeout, because only then can timeouts that exceed 0xFFFFFFFF be sorted
    DWORD timeoutAbsBase = Timers[leftIndex]->TimeoutAbs;
    if ((int)(timeoutAbs - timeoutAbsBase) < 0)
        timeoutAbsBase = timeoutAbs;
    timeoutAbs -= timeoutAbsBase;

    int l = leftIndex, r = Timers.Count - 1, m;
    while (1)
    {
        m = (l + r) / 2;
        DWORD actTimeoutAbs = Timers[m]->TimeoutAbs - timeoutAbsBase;
        if (actTimeoutAbs == timeoutAbs)
        {
            while (++m < Timers.Count && Timers[m]->TimeoutAbs - timeoutAbsBase == timeoutAbs)
                ;     // return the index after the last identical timer
            return m; // found
        }
        else if (actTimeoutAbs > timeoutAbs)
        {
            if (l == r || l > m - 1)
                return m; // not found, should be at this position
            r = m - 1;
        }
        else
        {
            if (l == r)
                return m + 1; // not found, should be after this position
            l = m + 1;
        }
    }
}

BOOL CSocketsThread::AddTimer(int socketMsg, int socketUID, DWORD timeoutAbs, DWORD id, void* param)
{
    HANDLES(EnterCriticalSection(&CritSect));

    BOOL ret = FALSE;
    CTimerData* data = new CTimerData(socketMsg, socketUID, timeoutAbs, id, param);
    if (data != NULL)
    {
        // we must insert only after the protected section (timers in this section are deleted after processing)
        int i = FindIndexForNewTimer(timeoutAbs, (LockedTimers == -1 ? 0 : LockedTimers));
        Timers.Insert(i, data);
        if (Timers.IsGood())
        {
            if (i == 0 && !Terminating) // inserting the timer with the shortest time into the timeout
            {
                DWORD ti = timeoutAbs - GetTickCount();
                if ((int)ti > 0) // if the new timer has not yet expired (the time difference can also be negative), adjust or start the Windows timer
                    SetTimer(GetHiddenWindow(), SOCKETSTHREAD_TIMERID, ti, NULL);
                else
                {
                    if ((int)ti < 0)
                        TRACE_E("CSocketsThread::AddTimer(): expired timer was added (" << (int)ti << " ms)");
                    KillTimer(GetHiddenWindow(), SOCKETSTHREAD_TIMERID);                // cancel the possible Windows timer, it is no longer needed
                    PostMessage(GetHiddenWindow(), WM_TIMER, SOCKETSTHREAD_TIMERID, 0); // process the next timeout as soon as possible
                }
            }
            ret = TRUE;
        }
        else
        {
            Timers.ResetState();
            delete data;
        }
    }
    else
        TRACE_E(LOW_MEMORY);

    HANDLES(LeaveCriticalSection(&CritSect));
    return ret;
}

BOOL CSocketsThread::DeleteTimer(int socketUID, DWORD id)
{
    HANDLES(EnterCriticalSection(&CritSect));

    BOOL ret = FALSE;
    BOOL setTimer = FALSE;
    int last = (LockedTimers != -1 ? LockedTimers : 0); // we can delete only beyond the protected section (timers in this section are deleted after processing)
    int i;
    for (i = Timers.Count - 1; i >= 0; i--)
    {
        CTimerData* timer = Timers[i];
        if (timer->SocketUID == socketUID && timer->ID == id) // delete all matching timers
        {
            if (i >= last)
            {
                if (i == 0)
                    setTimer = TRUE;
                Timers.Delete(i);
                if (!Timers.IsGood())
                    Timers.ResetState(); // Delete cannot fail, but the array might not shrink
            }
            else // deleting a timer from the locked area of the Timers array = only change SocketMsg to (WM_APP_SOCKET_MIN-1)
            {
                Timers[i]->SocketMsg = WM_APP_SOCKET_MIN - 1;
            }
            ret = TRUE;
        }
    }
    if (setTimer) // a timer with the nearest timeout was cancelled, adjust the timeout
    {
        if (Timers.Count > 0 && !Terminating)
        {
            DWORD ti = Timers[0]->TimeoutAbs - GetTickCount();
            if ((int)ti > 0) // if the new timer has not yet expired (the time difference can also be negative), adjust or start the Windows timer
                SetTimer(GetHiddenWindow(), SOCKETSTHREAD_TIMERID, ti, NULL);
            else
            {
                KillTimer(GetHiddenWindow(), SOCKETSTHREAD_TIMERID);                // cancel the possible Windows timer, it is no longer needed
                PostMessage(GetHiddenWindow(), WM_TIMER, SOCKETSTHREAD_TIMERID, 0); // process the next timeout as soon as possible
            }
        }
        else
            KillTimer(GetHiddenWindow(), SOCKETSTHREAD_TIMERID); // cancel the possible Windows timer, it is no longer needed
    }

    HANDLES(LeaveCriticalSection(&CritSect));
    return ret;
}

void CSocketsThread::ReceiveTimer()
{
    // cancel the possible Windows timer (to avoid repeated calls)
    KillTimer(GetHiddenWindow(), SOCKETSTHREAD_TIMERID);

    HANDLES(EnterCriticalSection(&CritSect));

    // guard against recursive calls to ReceiveTimer() - there is no point posting WM_TIMER until the first run completes
    // processing the first call to ReceiveTimer(), a new timer will be started or WM_TIMER posted
    if (LockedTimers == -1)
    {
        DWORD ti = GetTickCount();
        int last = FindIndexForNewTimer(ti, 0);
        if (last > 0) // if any timer timeout occurred
        {
            LockedTimers = last; // protect the timers being processed from deletion and array shifting
            int i;
            for (i = 0; i < last; i++)
            {
                CTimerData* timer = Timers[i];
                int index = timer->SocketMsg - WM_APP_SOCKET_MIN;
                if (index >= 0 && index < Sockets.Count) // "always true"
                {
                    CSocket* s = Sockets[index];
                    if (s != NULL && s->GetUID() == timer->SocketUID) // it is the recipient of the message (it was waiting)
                        s->ReceiveTimer(timer->ID, timer->Param);
                    else // a timer that did not arrive because the socket was cancelled or swapped,
                    {    // when sockets are swapped, find the target socket sequentially and deliver the timer
                        int j;
                        for (j = 0; j < Sockets.Count; j++)
                        {
                            CSocket* s2 = Sockets[j];
                            if (s2 != NULL && s2->GetUID() == timer->SocketUID) // it is the recipient of the message (it was waiting)
                            {
                                s2->ReceiveTimer(timer->ID, timer->Param);
                                break;
                            }
                        }
#ifdef _DEBUG
                        if (j == Sockets.Count)
                        {
                            // print a warning that the timer event was lost (did not reach the socket object)
                            TRACE_I("Lost timer-event " << timer->ID << " for UID " << timer->SocketUID);
                        }
#endif
                    }
                }
                else
                {
                    if (index != -1)
                        TRACE_E("Unexpected situation in CSocketsThread::ReceiveTimer()");
                }
            }
            Timers.Delete(0, last);
            LockedTimers = -1; // nothing left to protect
        }
        if (Timers.Count > 0 && !Terminating)
        {
            ti = Timers[0]->TimeoutAbs - GetTickCount();
            if ((int)ti > 0) // if another timeout has not yet occurred (the time difference can also be negative), start the timer again
                SetTimer(GetHiddenWindow(), SOCKETSTHREAD_TIMERID, ti, NULL);
            else
                PostMessage(GetHiddenWindow(), WM_TIMER, SOCKETSTHREAD_TIMERID, 0); // process the next timeout as soon as possible
        }
    }
    HANDLES(LeaveCriticalSection(&CritSect));
}

LRESULT
CSocketsThread::ReceiveNetEvent(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE1("CSocketsThread::ReceiveNetEvent()");

    HANDLES(EnterCriticalSection(&CritSect));
    int index = uMsg - WM_APP_SOCKET_MIN;
    if (index >= 0 && index < Sockets.Count) // "always true"
    {
        CSocket* s = Sockets[index];
        if (s != NULL && s->GetSocket() == (SOCKET)wParam) // it is the recipient of the message (it was waiting)
        {
            s->ReceiveNetEventInt(lParam, index);
        }
        else
        {
#ifdef _DEBUG
            // print a warning that the message was lost (it did not reach the socket object)
            switch (WSAGETSELECTEVENT(lParam))
            {
            case FD_READ: /*TRACE_I("Lost FD_READ on " << (uMsg - WM_APP_SOCKET_MIN));*/
                break;    // arrives after FD_CLOSE; arrives a second time (after FD_CLOSE) if WSAAsyncSelect() is used after accept()
            case FD_WRITE:
                TRACE_I("Lost FD_WRITE on " << (uMsg - WM_APP_SOCKET_MIN));
                break;     // arrives a second time (after FD_CLOSE) if WSAAsyncSelect() is used after accept()
            case FD_CLOSE: /*TRACE_I("Lost FD_CLOSE on " << (uMsg - WM_APP_SOCKET_MIN));*/
                break;     // arrives a second time (after FD_CLOSE) if WSAAsyncSelect() is used after accept()
            case FD_CONNECT:
                TRACE_I("Lost FD_CONNECT on " << (uMsg - WM_APP_SOCKET_MIN));
                break; // occurs for directories inaccessible for listing on VMS (the data connection closes before its connect completes)
            case FD_ACCEPT:
                TRACE_I("Lost FD_ACCEPT on " << (uMsg - WM_APP_SOCKET_MIN));
                break;
            default:
                TRACE_E("Lost socket event (" << WSAGETSELECTEVENT(lParam) << ") on " << (uMsg - WM_APP_SOCKET_MIN));
                break;
            }
#endif
        }
    }
    else
        TRACE_E("Unexpected situation in CSocketsThread::ReceiveNetEvent()");
    HANDLES(LeaveCriticalSection(&CritSect));
    return 0;
}

LRESULT CALLBACK
CSocketsThread::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    //  SLOW_CALL_STACK_MESSAGE5("CSocketsThread::WindowProc(0x%X, 0x%X, 0x%X, 0x%X)", hwnd, uMsg, wParam, lParam);

    if (GetTickCount() - LastWM_TIMER_Processing >= 500 && uMsg != WM_TIMER)
    {                                             // if 1000 ms passed since the last WM_TIMER, insert it for processing manually, because most likely
                                                  // the thread simply is not "idle", and therefore the system does not send WM_TIMER (it is unfortunately a low-priority message)
        LastWM_TIMER_Processing = GetTickCount(); // store when WM_TIMER was last processed
        if (SocketsThread != NULL)
            SocketsThread->ReceiveTimer();
    }

    if (uMsg >= WM_APP_SOCKET_MIN && uMsg <= WM_APP_SOCKET_MAX) // message for the socket from Windows Sockets
    {
        if (SocketsThread != NULL)
            return SocketsThread->ReceiveNetEvent(uMsg, wParam, lParam);
    }
    else
    {
        switch (uMsg)
        {
        case WM_APP_SOCKET_ADDR:
        {
            if (SocketsThread != NULL)
                SocketsThread->ReceiveMsgData();
            return 0;
        }

        case WM_TIMER:
        {
            // store when WM_TIMER was last processed
            LastWM_TIMER_Processing = GetTickCount();
            if (SocketsThread != NULL)
                SocketsThread->ReceiveTimer();
            return 0;
        }

        case WM_APP_SOCKET_POSTMSG:
        {
            if (SocketsThread != NULL)
                SocketsThread->ReceivePostMessage();
            return 0;
        }

        case WM_DESTROY: // closing the window ends the message loop
        {
            PostQuitMessage(0);
            return 0;
        }
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

BOOL CSocketsThread::IsRunning()
{
    CALL_STACK_MESSAGE1("CSocketsThread::IsRunning()");
    WaitForSingleObject(RunningEvent, INFINITE);
    BOOL run = Running;
    SetEvent(CanEndThread); // now the thread can terminate and this object can be deallocated
    return run;
}

unsigned
CSocketsThread::Body()
{
    TRACE_I("Begin");

    WNDCLASSW hiddenWinCls;
    memset(&hiddenWinCls, 0, sizeof(hiddenWinCls));
    hiddenWinCls.lpfnWndProc = CSocketsThread::WindowProc;
    hiddenWinCls.hInstance = DLLInstance;
    hiddenWinCls.hCursor = LoadCursor(NULL, IDC_ARROW);
    hiddenWinCls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    hiddenWinCls.lpszClassName = SOCKETSWINDOW_CLASSNAME;
    if (RegisterClassW(&hiddenWinCls) != 0)
    {
        HWindow = CreateWindowW(SOCKETSWINDOW_CLASSNAME, L"HiddenSocketsWindow", 0, CW_USEDEFAULT,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, DLLInstance, 0);
        if (HWindow != NULL)
        {
            // announce successful thread startup and initialization
            Running = TRUE;
            SetEvent(RunningEvent);
            WaitForSingleObject(CanEndThread, INFINITE);
            LastWM_TIMER_Processing = GetTickCount();

            // message loop
            MSG msg;
            while (GetMessageW(&msg, NULL, 0, 0))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            // clear the window handle - there is no point sending messages anymore
            HANDLES(EnterCriticalSection(&CritSect));
            HWindow = NULL;
            HANDLES(LeaveCriticalSection(&CritSect));
        }

        if (!UnregisterClassW(SOCKETSWINDOW_CLASSNAME, DLLInstance))
            TRACE_E("UnregisterClass(SOCKETSWINDOW_CLASSNAME) has failed");
    }
    if (!Running)
    {
        SetEvent(RunningEvent); // announce the thread's death in case of an error
        WaitForSingleObject(CanEndThread, INFINITE);
    }

    // Petr: if this thread used OpenSSL, we must release memory with the following call
    SSLThreadLocalCleanup();

    TRACE_I("End");
    return 0;
}
