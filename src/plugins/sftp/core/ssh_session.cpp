#include "ssh_session.h"

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include <climits>

#include "ssh_algorithms.h"
#include "ssh_init.h"

namespace
{
constexpr int kSliceMs = 100; // how often a waiting connect looks at the cancel flag

// English text for the log; the translated texts get only the number ("(Winsock error %2)" is in their pattern).
std::string WsaMessage(const std::string& code)
{
    return "Winsock error " + code;
}

std::wstring Widen(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    if (n <= 0)
    {
        codePage = CP_ACP;
        n = MultiByteToWideChar(codePage, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (n <= 0)
            return std::wstring();
    }
    std::wstring out(static_cast<size_t>(n), L' '); // overwritten below
    MultiByteToWideChar(codePage, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], n);
    return out;
}

// "address:port" of a resolved address for the log (IPv6 gets brackets).
std::string AddressText(const ADDRINFOEXW* ai)
{
    char host[NI_MAXHOST] = {};
    char port[NI_MAXSERV] = {};
    if (getnameinfo(ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen), host, sizeof(host), port, sizeof(port),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0)
        return "(unknown address)";
    const std::string h = host;
    return (h.find(':') != std::string::npos ? "[" + h + "]" : h) + ":" + port;
}

std::string Msec(int ms)
{
    return std::to_string(ms / 1000) + " s";
}

// The same time limit as a plain number for a translated text ("... %1 s"): the unit is in the pattern.
std::string Seconds(int ms)
{
    return std::to_string(ms / 1000);
}
} // namespace

CSshSession::CSshSession(ISshCallbacks* cb, CKnownHosts* hosts) : cb_(cb), hosts_(hosts)
{
    globalInit_ = SshGlobalInit(&error_);
}

CSshSession::~CSshSession()
{
    CloseAll();
    if (globalInit_)
        SshGlobalExit();
}

void CSshSession::Cancel()
{
    cancelled_ = true;
    std::lock_guard<std::mutex> lock(socketMutex_);
    if (socket_ != INVALID_SOCKET)
        shutdown(socket_, SD_BOTH); // wakes the owner thread out of select/recv; it closes the socket itself
}

void CSshSession::Disconnect()
{
    CloseAll();
    cancelled_ = false;
}

bool CSshSession::SendKeepalive()
{
    if (IsCancelled())
        return Fail(SshErrorKind::Cancelled, std::string());
    if (broken_)
        return FailBroken();
    if (!connected_ || session_ == nullptr)
        return Fail(SshErrorKind::ConnectFailed, SshMsg::NotConnected, {}, "Not connected");
    libssh2_keepalive_config(session_, 0, 2); // 2 s is libssh2's smallest interval; the caller decides how often to call
    int secondsToNext = 0;
    const int rc = Run([&] { return libssh2_keepalive_send(session_, &secondsToNext); }, timeoutMs_);
    if (rc != 0)
        return FailFromSession("Keepalive failed", rc);
    Log(SshLogLevel::Normal, "Keepalive sent");
    return true;
}

std::string CSshSession::NegotiatedMethod(int methodType) const
{
    if (session_ == nullptr || !connected_)
        return std::string();
    const char* m = libssh2_session_methods(session_, methodType);
    return m != nullptr ? m : "";
}

CSshServerInfo CSshSession::ServerInfo() const
{
    CSshServerInfo info;
    if (session_ == nullptr || !connected_)
        return info;
    const char* banner = libssh2_session_banner_get(session_);
    if (banner != nullptr)
        info.banner = banner;
    info.sftpVersionSupported = LIBSSH2_SFTP_VERSION;
    return info;
}

CSshMethods CSshSession::NegotiatedMethods() const
{
    CSshMethods m;
    m.kex = NegotiatedMethod(LIBSSH2_METHOD_KEX);
    m.hostKey = NegotiatedMethod(LIBSSH2_METHOD_HOSTKEY);
    m.cipherClientToServer = NegotiatedMethod(LIBSSH2_METHOD_CRYPT_CS);
    m.cipherServerToClient = NegotiatedMethod(LIBSSH2_METHOD_CRYPT_SC);
    m.macClientToServer = NegotiatedMethod(LIBSSH2_METHOD_MAC_CS);
    m.macServerToClient = NegotiatedMethod(LIBSSH2_METHOD_MAC_SC);
    m.compressionClientToServer = NegotiatedMethod(LIBSSH2_METHOD_COMP_CS);
    m.compressionServerToClient = NegotiatedMethod(LIBSSH2_METHOD_COMP_SC);
    return m;
}

bool CSshSession::Fail(SshErrorKind kind, const std::string& message, int libssh2Code)
{
    error_.kind = IsCancelled() ? SshErrorKind::Cancelled : kind;
    error_.libssh2Code = libssh2Code;
    error_.sftpStatus = 0;
    error_.message = IsCancelled() ? "The operation was cancelled" : message;
    error_.msgId = SshMsg::None;
    error_.args.clear();
    // every failure of the session passes here: the log gets each one with its English text
    if (error_.kind == SshErrorKind::Cancelled)
        Log(SshLogLevel::Normal, "Cancelled");
    else
        Log(SshLogLevel::Error, "Error: " + error_.message + (libssh2Code != 0 ? " (libssh2 code " + std::to_string(libssh2Code) + ")" : ""));
    return false;
}

bool CSshSession::Fail(SshErrorKind kind, SshMsg msgId, std::vector<std::string> args, const std::string& message,
                       int libssh2Code)
{
    Fail(kind, message, libssh2Code);
    if (!IsCancelled()) // a cancel replaces the error; the id would no longer match the message
    {
        error_.msgId = msgId;
        error_.args = std::move(args);
    }
    return false;
}

int CSshSession::TimeoutMsOf(const CSshSite& site)
{
    const int seconds = site.timeoutSeconds > 0 ? site.timeoutSeconds : 30;
    return seconds > INT_MAX / 1000 ? INT_MAX / 1000 * 1000 : seconds * 1000;
}

bool CSshSession::IsConnectionError(int code)
{
    return code == LIBSSH2_ERROR_TIMEOUT || code == LIBSSH2_ERROR_SOCKET_DISCONNECT ||
           code == LIBSSH2_ERROR_SOCKET_SEND || code == LIBSSH2_ERROR_SOCKET_RECV ||
           code == LIBSSH2_ERROR_SOCKET_TIMEOUT || code == LIBSSH2_ERROR_SOCKET_NONE;
}

void CSshSession::ShutdownSocket()
{
    std::lock_guard<std::mutex> lock(socketMutex_);
    if (socket_ != INVALID_SOCKET)
        shutdown(socket_, SD_BOTH);
}

void CSshSession::MarkBroken(const char* reason)
{
    if (broken_)
        return;
    broken_ = true;
    brokenReason_ = reason;
    Log(SshLogLevel::Error, std::string("The connection is broken (") + reason + ")");
    ShutdownSocket(); // nothing else can reach the server on this socket, whatever libssh2 still has queued
}

bool CSshSession::FailBroken()
{
    const std::string reason = brokenReason_.empty() ? std::string("timeout") : brokenReason_;
    SshMsg msgId = SshMsg::BrokenAfterError;
    if (reason == "timeout")
        msgId = SshMsg::BrokenAfterTimeout;
    else if (reason == "cancel")
        msgId = SshMsg::BrokenAfterCancel;
    return Fail(brokenReason_ == "timeout" ? SshErrorKind::Timeout : SshErrorKind::ConnectFailed, msgId, {},
                "The connection is no longer usable after a " + reason + "; disconnect and reconnect");
}

// Error of the libssh2 call that failed with code: kind from the code, text from libssh2.
bool CSshSession::FailFromSession(const std::string& what, int code, SshMsg msgId, std::vector<std::string> args)
{
    if (code == kBrokenCode)
        return FailBroken();
    std::string message = what;
    if (code == LIBSSH2_ERROR_TIMEOUT)
    {
        message += ": no answer within " + Msec(timeoutMs_);
        // The caller's id (the step that timed out) stays: the UI layer shows "step: no answer from the server". Only a
        // call without an id gets the generic "No answer within N s".
        if (msgId == SshMsg::None)
        {
            msgId = SshMsg::NoAnswerTimeout;
            args = {Seconds(timeoutMs_)};
        }
    }
    else
    {
        char* text = nullptr;
        int len = 0;
        libssh2_session_last_error(session_, &text, &len, 0);
        if (text != nullptr && *text != 0)
        {
            message += ": " + std::string(text);
            if (msgId != SshMsg::None)
                args.push_back(text);
        }
        else if (args.empty())
            msgId = SshMsg::None; // the sentence needs the libssh2 text, and there is none
    }
    return Fail(code == LIBSSH2_ERROR_TIMEOUT ? SshErrorKind::Timeout : SshErrorKind::ConnectFailed, msgId, args,
                message, code);
}

// Waits until the socket is ready in the direction libssh2 is blocked on, in slices so a cancel is noticed.
// Returns false on timeout or cancel.
bool CSshSession::WaitSocket(const Clock::time_point& deadline)
{
    while (!IsCancelled() && Clock::now() < deadline)
    {
        int dirs = libssh2_session_block_directions(session_);
        fd_set readSet, writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        if ((dirs & LIBSSH2_SESSION_BLOCK_INBOUND) != 0 || dirs == 0)
            FD_SET(socket_, &readSet);
        if ((dirs & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0)
            FD_SET(socket_, &writeSet);
        timeval slice = {0, kSliceMs * 1000};
        if (select(0, &readSet, &writeSet, nullptr, &slice) != 0) // ready, or an error: let libssh2 report it
            return true;
    }
    return false;
}

void CSshSession::SetSocket(SOCKET s)
{
    std::lock_guard<std::mutex> lock(socketMutex_);
    socket_ = s;
}

void CSshSession::CloseSocket()
{
    std::lock_guard<std::mutex> lock(socketMutex_);
    if (socket_ != INVALID_SOCKET)
    {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

void CSshSession::CloseAll()
{
    // Graceful steps only on a healthy, not cancelled session. When one of them does not complete (Run times out and
    // marks the session broken) or is skipped, the socket is shut down so that libssh2 cannot answer EAGAIN to the
    // final free and leak the session.
    bool graceful = !broken_ && !IsCancelled();
    if (session_ != nullptr || socket_ != INVALID_SOCKET)
        Log(SshLogLevel::Normal, "Disconnecting");
    if (sftp_ != nullptr)
    {
        if (graceful)
            graceful = Run([this] { return libssh2_sftp_shutdown(sftp_); }, 2000) == 0;
        else
            libssh2_sftp_shutdown(sftp_); // non-blocking: the goodbye may not go out, the handle is freed anyway
        sftp_ = nullptr;
    }
    authenticated_ = false;
    if (session_ != nullptr)
    {
        if (connected_ && graceful)
            graceful = Run([this] { return libssh2_session_disconnect(session_, "Sally: closing"); }, 2000) == 0;
        else
            graceful = false;
        if (!graceful)
            ShutdownSocket();
        // libssh2_session_free may still say EAGAIN once (a pending send); a few more calls finish it
        for (int i = 0; i < 10; ++i)
            if (libssh2_session_free(session_) != LIBSSH2_ERROR_EAGAIN)
                break;
        session_ = nullptr;
    }
    connected_ = false;
    CloseSocket();
    broken_ = false;
    brokenReason_.clear();
}

// Non-blocking connect to one address, polled in short slices so the cancel flag and the timeout are honoured.
SOCKET CSshSession::ConnectAddress(const ADDRINFOEXW* ai, int timeoutMs, std::string& why)
{
    SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == INVALID_SOCKET)
    {
        why = std::to_string(WSAGetLastError());
        return INVALID_SOCKET;
    }
    SetSocket(s);
    if (IsCancelled()) // Cancel() may have run before the handle was published
    {
        CloseSocket();
        return INVALID_SOCKET;
    }
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    BOOL noDelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

    if (connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == SOCKET_ERROR)
    {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK)
        {
            why = std::to_string(err);
            CloseSocket();
            return INVALID_SOCKET;
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;)
        {
            if (IsCancelled())
            {
                CloseSocket();
                return INVALID_SOCKET;
            }
            if (Clock::now() >= deadline)
            {
                why = "timeout";
                CloseSocket();
                return INVALID_SOCKET;
            }
            fd_set writable, failed;
            FD_ZERO(&writable);
            FD_ZERO(&failed);
            FD_SET(s, &writable);
            FD_SET(s, &failed);
            timeval slice = {0, kSliceMs * 1000};
            int n = select(0, nullptr, &writable, &failed, &slice);
            if (n == SOCKET_ERROR)
            {
                why = std::to_string(WSAGetLastError());
                CloseSocket();
                return INVALID_SOCKET;
            }
            if (n == 0)
                continue;
            int soError = 0;
            int len = sizeof(soError);
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
            if (FD_ISSET(s, &failed) || soError != 0)
            {
                why = std::to_string(soError != 0 ? soError : WSAECONNREFUSED);
                CloseSocket();
                return INVALID_SOCKET;
            }
            break; // writable: connected
        }
    }
    return s;
}

// Name resolution that Cancel() and the timeout can interrupt: getaddrinfo cannot be cancelled and may block for many
// seconds on a bad name. The overlapped GetAddrInfoExW is polled in the same slices as everything else here. After
// GetAddrInfoExCancel we always wait for the completion event, so the OVERLAPPED and the result are never used after
// they went out of scope. On success 'list' is freed by the caller with FreeAddrInfoExW.
bool CSshSession::ResolveHost(const CSshSite& site, int timeoutMs, ADDRINFOEXW*& list)
{
    list = nullptr;
    const std::wstring host = Widen(site.host);
    const std::wstring port = std::to_wstring(site.port);
    ADDRINFOEXW hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr)
        return Fail(SshErrorKind::Other, "Cannot create the name resolution event");

    ADDRINFOEXW* result = nullptr;
    HANDLE cancelHandle = nullptr;
    int rc = GetAddrInfoExW(host.c_str(), port.c_str(), NS_ALL, nullptr, &hints, &result, nullptr, &overlapped, nullptr,
                            &cancelHandle);
    bool cancelled = false;
    bool timedOut = false;
    if (rc == WSA_IO_PENDING)
    {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (WaitForSingleObject(overlapped.hEvent, kSliceMs) != WAIT_OBJECT_0)
        {
            cancelled = IsCancelled();
            timedOut = !cancelled && Clock::now() >= deadline;
            if (cancelled || timedOut)
            {
                GetAddrInfoExCancel(&cancelHandle);
                WaitForSingleObject(overlapped.hEvent, INFINITE); // the completion follows the cancel
                break;
            }
        }
        rc = GetAddrInfoExOverlappedResult(&overlapped);
    }
    CloseHandle(overlapped.hEvent);

    if (cancelled || timedOut)
    {
        if (result != nullptr)
            FreeAddrInfoExW(result); // it completed before the cancel took effect; we do not use it
        if (cancelled)
            return Fail(SshErrorKind::Cancelled, std::string());
        return Fail(SshErrorKind::Timeout, SshMsg::ResolveTimeout, {site.host, Seconds(timeoutMs)},
                    "Resolving host '" + site.host + "' timed out after " + Msec(timeoutMs));
    }
    if (rc != 0 || result == nullptr)
    {
        if (result != nullptr)
            FreeAddrInfoExW(result);
        const std::string code = std::to_string(rc);
        return Fail(SshErrorKind::ConnectFailed, SshMsg::ResolveFailed, {site.host, code},
                    "Cannot resolve host '" + site.host + "' (" + WsaMessage(code) + ")");
    }
    list = result;
    return true;
}

bool CSshSession::ConnectSocket(const CSshSite& site)
{
    const int timeoutMs = TimeoutMsOf(site);
    ADDRINFOEXW* list = nullptr;
    Log(SshLogLevel::Normal, "Resolving " + site.host);
    if (!ResolveHost(site, timeoutMs, list))
        return false;

    std::string why;
    bool timedOut = false;
    bool ok = false;
    for (const ADDRINFOEXW* ai = list; ai != nullptr && !ok && !IsCancelled(); ai = ai->ai_next)
    {
        why.clear();
        Log(SshLogLevel::Normal, "Connecting to " + AddressText(ai));
        ok = ConnectAddress(ai, timeoutMs, why) != INVALID_SOCKET;
        timedOut = !ok && why == "timeout";
        if (ok)
            Log(SshLogLevel::Normal, "TCP connection established");
        else if (!IsCancelled())
            Log(SshLogLevel::Normal, "Connecting failed: " + (timedOut ? "no answer within " + Msec(timeoutMs) : WsaMessage(why)));
    }
    FreeAddrInfoExW(list);
    if (ok)
        return true;

    if (timedOut)
    {
        const std::string hostPort = site.host + ":" + std::to_string(site.port);
        return Fail(SshErrorKind::Timeout, SshMsg::ConnectTimeout, {hostPort, Seconds(timeoutMs)},
                    "Connecting to " + hostPort + " timed out after " + Msec(timeoutMs));
    }
    const std::string hostPort = site.host + ":" + std::to_string(site.port);
    // 'why' is a Winsock error number here: a timeout ("timeout") was handled above, a cancel replaces the error
    return Fail(SshErrorKind::ConnectFailed, SshMsg::ConnectFailed, {hostPort, why},
                "Cannot connect to " + hostPort + " (" + WsaMessage(why) + ")");
}

bool CSshSession::CreateSession(const CSshSite& site)
{
    session_ = libssh2_session_init();
    if (session_ == nullptr)
        return Fail(SshErrorKind::Other, "Cannot create the SSH session");

    // Non-blocking libssh2 calls driven by Run(): a blocking libssh2 wait cannot be interrupted by Cancel on Windows
    // (select does not wake on a local shutdown), and this way the timeout and the cancel flag are checked every 100 ms.
    libssh2_session_set_blocking(session_, 0);
    timeoutMs_ = TimeoutMsOf(site);

    struct CPolicy
    {
        int method;
        bool (*allowed)(const std::string&);
        const char* name;
    };
    const CPolicy policy[] = {{LIBSSH2_METHOD_KEX, IsAllowedKex, "key exchange"},
                              {LIBSSH2_METHOD_HOSTKEY, IsAllowedHostKey, "host key"},
                              {LIBSSH2_METHOD_CRYPT_CS, IsAllowedCipher, "cipher"},
                              {LIBSSH2_METHOD_CRYPT_SC, IsAllowedCipher, "cipher"},
                              {LIBSSH2_METHOD_MAC_CS, IsAllowedMac, "MAC"},
                              {LIBSSH2_METHOD_MAC_SC, IsAllowedMac, "MAC"}};
    for (const CPolicy& p : policy)
        if (!RestrictAlgorithms(session_, p.method, p.allowed))
            return Fail(SshErrorKind::Other,
                        std::string("No allowed ") + p.name + " algorithm is available in this libssh2 build");

    if (site.compression)
    {
        libssh2_session_flag(session_, LIBSSH2_FLAG_COMPRESS, 1);
        libssh2_session_method_pref(session_, LIBSSH2_METHOD_COMP_CS, "zlib@openssh.com,zlib,none");
        libssh2_session_method_pref(session_, LIBSSH2_METHOD_COMP_SC, "zlib@openssh.com,zlib,none");
    }
    return true;
}

bool CSshSession::Handshake()
{
    int rc = Run([this] { return libssh2_session_handshake(session_, socket_); }, timeoutMs_);
    if (rc != 0)
        return FailFromSession("SSH handshake failed", rc, SshMsg::HandshakeFailed);
    LogNegotiated();
    return true;
}

// Banner and negotiated algorithms, right after the handshake.
void CSshSession::LogNegotiated() const
{
    if (logSink_ == nullptr)
        return;
    const char* banner = libssh2_session_banner_get(session_);
    if (banner != nullptr)
        Log(SshLogLevel::Normal, std::string("Server: ") + banner);
    // not NegotiatedMethods(): that one answers only after the host key was accepted
    const auto method = [this](int type)
    {
        const char* m = libssh2_session_methods(session_, type);
        return std::string(m != nullptr ? m : "-");
    };
    Log(SshLogLevel::Normal, "Key exchange: " + method(LIBSSH2_METHOD_KEX) + ", host key: " + method(LIBSSH2_METHOD_HOSTKEY));
    Log(SshLogLevel::Normal, "Cipher: " + method(LIBSSH2_METHOD_CRYPT_CS) + " / " + method(LIBSSH2_METHOD_CRYPT_SC) +
                                 ", MAC: " + method(LIBSSH2_METHOD_MAC_CS) + " / " + method(LIBSSH2_METHOD_MAC_SC) +
                                 ", compression: " + method(LIBSSH2_METHOD_COMP_CS) + " / " + method(LIBSSH2_METHOD_COMP_SC));
}

// Called right after the handshake, before any credential is sent.
bool CSshSession::VerifyHostKey(const CSshSite& site)
{
    size_t len = 0;
    int type = 0;
    const char* key = libssh2_session_hostkey(session_, &len, &type);
    if (key == nullptr || len == 0)
        return Fail(SshErrorKind::Protocol, SshMsg::NoHostKey, {}, "The server sent no host key");

    hostKeyType_ = HostKeyTypeName(type);
    fingerprint_ = Sha256Fingerprint(key, len);

    const std::string host = LowerHost(site.host); // host names are case insensitive
    HostKeyCheck check =
        hosts_ != nullptr ? hosts_->Check(session_, host, site.port, key, len, type) : HostKeyCheck::NotFound;
    Log(SshLogLevel::Normal, "Host key " + hostKeyType_ + " " + fingerprint_);
    if (check == HostKeyCheck::Match)
    {
        Log(SshLogLevel::Normal, "Host key matches a known host");
        return true;
    }
    if (check == HostKeyCheck::Failure)
        return Fail(SshErrorKind::Other, SshMsg::KnownHostsUnchecked, {host},
                    "The known_hosts data could not be checked; the host key of " + host + " was not verified");

    const bool changed = check == HostKeyCheck::Mismatch;
    Log(SshLogLevel::Normal, changed ? "Host key differs from the known one; asking" : "Host key is not known yet; asking");
    HostKeyDecision decision = HostKeyDecision::Reject;
    if (cb_ != nullptr)
        decision = cb_->VerifyHostKey(host, site.port, hostKeyType_, fingerprint_, changed);
    Log(SshLogLevel::Normal, decision == HostKeyDecision::Reject            ? "Host key decision: rejected"
                             : decision == HostKeyDecision::AcceptAndSave ? "Host key decision: accepted and saved"
                                                                          : "Host key decision: accepted for this session");

    if (IsCancelled())
        return Fail(SshErrorKind::Cancelled, std::string());
    if (decision == HostKeyDecision::Reject)
        return Fail(changed ? SshErrorKind::HostKeyChanged : SshErrorKind::HostKeyRejected,
                    changed ? SshMsg::HostKeyChanged : SshMsg::HostKeyRejected, {host, fingerprint_},
                    changed ? "The host key of " + host + " has changed (" + fingerprint_ + ") and was not accepted"
                            : "The host key of " + host + " (" + fingerprint_ + ") was not accepted");
    if (decision == HostKeyDecision::AcceptAndSave)
    {
        CSshError saveError;
        if (hosts_ == nullptr)
            return Fail(SshErrorKind::Io, SshMsg::NoKnownHostsFile, {}, "No known_hosts file is configured");
        if (!hosts_->AddOrReplace(session_, host, site.port, key, len, type, &saveError))
        {
            Fail(SshErrorKind::Io, saveError.msgId, saveError.args, saveError.message);
            error_.libssh2Code = saveError.libssh2Code;
            return false;
        }
    }
    return true;
}

bool CSshSession::OpenTransport(const CSshSite& site)
{
    error_ = CSshError();
    codec_ = site.nameCodec;
    hostKeyType_.clear();
    fingerprint_.clear();

    if (broken_)
        return FailBroken();
    if (!globalInit_ && !(globalInit_ = SshGlobalInit(&error_)))
        return false;
    if (session_ != nullptr || socket_ != INVALID_SOCKET)
        return Fail(SshErrorKind::Other, "The session is already open");
    if (site.host.empty())
        return Fail(SshErrorKind::Other, SshMsg::NoHostGiven, {}, "No host name given");

    bool ok = ConnectSocket(site) && CreateSession(site) && Handshake() && VerifyHostKey(site);
    if (!ok)
    {
        if (IsCancelled() && error_.kind != SshErrorKind::Cancelled)
            Fail(SshErrorKind::Cancelled, std::string());
        CloseAll();
        return false;
    }
    connected_ = true;
    return true;
}

bool CSshSession::OpenSftp()
{
    if (broken_)
        return FailBroken();
    if (!authenticated_ || session_ == nullptr)
        return Fail(SshErrorKind::Other, SshMsg::SftpNeedsAuth, {}, "The SFTP subsystem needs an authenticated session");
    if (sftp_ != nullptr)
        return true;
    int code = 0;
    sftp_ = RunPtr([this] { return libssh2_sftp_init(session_); }, timeoutMs_, code);
    if (sftp_ != nullptr)
    {
        Log(SshLogLevel::Normal, "SFTP subsystem opened");
        return true;
    }

    switch (code)
    {
    case LIBSSH2_ERROR_CHANNEL_FAILURE:
    case LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED:
    case LIBSSH2_ERROR_CHANNEL_CLOSED:
    case LIBSSH2_ERROR_CHANNEL_EOF_SENT:
    case LIBSSH2_ERROR_SFTP_PROTOCOL:
    {
        Fail(SshErrorKind::NoSftp, SshMsg::NoSftpSubsystem, {}, "The server does not provide the SFTP subsystem");
        error_.libssh2Code = code;
        return false;
    }
    default:
        return FailFromSession("Cannot start the SFTP subsystem", code, SshMsg::SftpSubsystemFailed);
    }
}

bool CSshSession::Connect(const CSshSite& site)
{
    if (broken_)
        return FailBroken(); // not closed here: Disconnect() is the caller's explicit reset
    if (OpenTransport(site) && Authenticate(site) && OpenSftp())
        return true;
    // keep the error of the failed step; Disconnect only closes
    CSshError error = error_;
    Disconnect();
    error_ = error;
    return false;
}
