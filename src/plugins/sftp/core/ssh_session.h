// One SSH connection (libssh2 session + socket). Step 3: transport and host key verification.
// Authentication and the SFTP subsystem are added on top of the same members (session_, socket_).
#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include "known_hosts.h"
#include "ssh_types.h"

typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;
typedef struct _LIBSSH2_SFTP LIBSSH2_SFTP;

// A broken connection: any libssh2 step that times out, loses the socket or is cancelled leaves the non-blocking
// libssh2 session with an unfinished request whose state the next call of the same kind would silently continue
// with the OLD arguments. So such an event marks the session broken: the socket is shut down and every later
// operation, Authenticate, OpenSftp, OpenTransport and Connect fails at once (kind Timeout/ConnectFailed, "The
// connection is no longer usable after a timeout; disconnect and reconnect") until Disconnect() resets it. The
// timeout is an IDLE timeout: it restarts whenever libssh2 returns, so time spent in the Prompt callback (or any
// other callback) never counts. IsConnected() is false for a broken session.
//
// Threading: every method except Cancel must be called from one thread (the owner thread) for the whole life of the
// object. Cancel may be called from any thread. After a Cancel (also one that arrived too late to matter) call
// Disconnect on the owner thread before reusing the object; Disconnect clears the cancel request.
class CSshSession
{
public:
    // cb and hosts are not owned and must outlive the session. cb == nullptr: unknown or changed host keys are
    // rejected. hosts == nullptr: no host is ever known (every key goes to the callback, nothing can be saved).
    CSshSession(ISshCallbacks* cb, CKnownHosts* hosts);
    ~CSshSession();
    CSshSession(const CSshSession&) = delete;
    CSshSession& operator=(const CSshSession&) = delete;

    // Resolves and connects (every address in turn), runs the handshake with a modern algorithm set and verifies the
    // host key. On success the session is ready for authentication. On failure LastError() says why and everything
    // is closed again. Blocks up to the site timeout per step (DNS resolution itself cannot be cancelled).
    bool OpenTransport(const CSshSite& site);

    // Logs in on the open transport: SSH agent, key file, password, keyboard-interactive, each only when the site
    // asks for it and the server offers it. Fails (and leaves the transport open) without a verified transport.
    bool Authenticate(const CSshSite& site);

    // Starts the SFTP subsystem on the authenticated session. Kind NoSftp when the server has none.
    bool OpenSftp();

    // OpenTransport + Authenticate + OpenSftp; everything is closed again on failure.
    bool Connect(const CSshSite& site);

    // ---- SFTP operations (ssh_sftp_ops.cpp). Need OpenSftp() done. All names and paths are raw bytes, passed to the
    // server unchanged; a path is absolute or relative to the login directory. false: see LastError() (kinds
    // NotFound, PermissionDenied, Exists, NotEmpty, Unsupported, Io, Timeout, Cancelled, ConnectFailed, Other).

    // Lists a directory without "." and "..". Owner and group are the numeric uid/gid as text (the SFTP v3 attributes
    // carry no names; the "ls -l" long entry would give names but only on some servers and with fragile parsing).
    // isLink comes from the attributes of the entry itself (not followed), linkTargetIsDir from a stat of the target
    // (a broken link gives false). An error while reading is a failure, never a short list: out is empty then.
    // A name that is not a valid entry name (IsValidRemoteEntryName: empty, with '/' or NUL) fails the whole listing
    // with kind Protocol, as do more than 10 million entries. A server cannot be trusted to send harmless names, and
    // the names are raw bytes: the layer above (ops/) must still validate/sanitise them as LOCAL Windows names
    // (reserved device names, trailing dots, ':', backslash, ...) before using them in a local path.
    // The second look per entry (links, missing permissions) aborts the listing on a connection-level error; only
    // an SFTP status for that one entry (e.g. a broken link) is tolerated.
    bool ListDir(const std::string& path, std::vector<CSftpEntry>& out);
    // Attributes, following a final symlink / not following it. out.rawName is left empty.
    bool Stat(const std::string& path, CSftpEntry& out);
    bool LStat(const std::string& path, CSftpEntry& out);
    bool RealPath(const std::string& path, std::string& out);
    bool ReadLink(const std::string& path, std::string& target);
    bool Mkdir(const std::string& path, int mode = 0755);
    bool Rmdir(const std::string& path);
    bool Remove(const std::string& path); // file or symlink
    // overwrite: libssh2_sftp_rename_ex with OVERWRITE|ATOMIC|NATIVE; libssh2 sends those flags only for SFTP v5+ and
    // posix-rename@openssh.com is not reachable, so on a v3 server (OpenSSH) an existing target (file, link or empty
    // directory) is MOVED ASIDE to ".<name>.sally-<random>" in the same folder, the source is renamed to the target
    // and the aside copy is deleted; when the second step fails the aside copy is renamed back and the error of that
    // step is returned, so a failure never loses the old target (if the connection breaks in between, the message
    // names the leftover aside file). NOT atomic: for a moment the target name does not exist. A non-empty directory
    // as target gives Exists and nothing is touched. If only deleting the aside copy fails the call succeeds and
    // LastError() (kind None) names the leftover. Without overwrite an existing target gives Exists.
    bool Rename(const std::string& from, const std::string& to, bool overwrite);
    // Creates the symbolic link linkPath that points to target (target is stored as given).
    bool Symlink(const std::string& target, const std::string& linkPath);
    bool Chmod(const std::string& path, uint32_t mode); // permission bits only (mode & 07777)
    // libssh2 carries the times as unsigned long (32 bits on Windows): values outside 0..4294967295 give Unsupported.
    bool SetTimes(const std::string& path, int64_t atime, int64_t mtime);

    // Size and free space of the file system that holds path (libssh2_sftp_statvfs). Kind Unsupported when the server
    // does not know the "statvfs@openssh.com" extension.
    bool Statvfs(const std::string& path, CSftpVfs& out);

    // ---- Remote command (ssh_exec.cpp). Needs an authenticated session (the SFTP subsystem is not used). Runs
    // "command" through the user's login shell on a new channel and waits until it ends. Kinds: Unsupported when the
    // server refuses to run commands (the exact refusal depends on the server: a ForceCommand may run something else
    // instead, which libssh2 cannot tell), Timeout/ConnectFailed/Cancelled as for the SFTP calls. Cancel() or an
    // idle timeout breaks the session like everywhere else (D24). The channel is closed on every path.
    bool Exec(const std::string& command, const CExecOptions& options, CExecResult& out);

    // ---- Transfers (ssh_transfer.cpp). Progress goes to ISshCallbacks::Progress (at most every 100 ms, plus once at the
    // end); it reports the position in the whole file (done includes the start offset) and returning false cancels
    // (kind Cancelled). Cancel() from another thread also gives Cancelled. On failure or cancel the partial data stays
    // where it is; *bytesTransferred (optional) is the number of bytes moved by this call. The remote handle is closed
    // on every path. Local errors give kind Io with the Win32 error code in the message.

    // Reads the remote file from startOffset to its end into localFile. The caller owns the HANDLE: opened for
    // writing, never flushed or closed here. Its position is set to startOffset (the data before it, e.g. from an
    // earlier partial download, is the caller's business; the file is not truncated). totalSize (optional) gets the
    // remote size when the server reports it. A startOffset beyond the remote size is an error (kind Io).
    bool Download(const std::string& remotePath, HANDLE localFile, uint64_t startOffset, uint64_t* bytesTransferred,
                  uint64_t* totalSize);

    // Writes localFile (opened for reading by the caller, not closed here) from startOffset to its end to the remote
    // file. startOffset == 0: the remote file is created or truncated. startOffset > 0 (resume): the remote file must
    // exist and be at least that long (else kind Io, "remote file is shorter than the resume offset"); writing
    // continues at startOffset and nothing is truncated. resume only documents the intent of a nonzero offset.
    // keepMtime: after a complete transfer the remote mtime is set to the local last write time; a failure of that
    // (e.g. a time outside 1970..2106) makes the call return false with the data already complete.
    bool Upload(HANDLE localFile, const std::string& remotePath, uint64_t startOffset, bool resume,
                uint64_t* bytesTransferred, bool keepMtime = false);

    // Size of the read/write buffer of one transfer step (default 256 KiB). libssh2 splits larger reads into several
    // pipelined requests, so bigger is usually faster. Clamped to 4 KiB .. 8 MiB.
    void SetTransferBufferSize(size_t bytes);

    // Where the session writes its log lines (connect, host key, login, SFTP calls, errors; never secrets). Not owned,
    // must outlive the session; nullptr = no log. Owner thread, before the first call.
    void SetLogSink(ISshLogSink* sink) { logSink_ = sink; }

    // Closes the SFTP session, the SSH session and the socket (also after a failed or cancelled OpenTransport), clears
    // the cancel request and the broken state.
    void Disconnect();

    // Any thread. Makes the running or the next blocking step on the owner thread return kind Cancelled.
    void Cancel();

    bool IsConnected() const { return connected_ && !broken_; }
    bool IsAuthenticated() const { return authenticated_; }
    bool HasSftp() const { return sftp_ != nullptr; }
    const CSshError& LastError() const { return error_; }

    // Valid after the handshake (also when the host key was then rejected).
    const std::string& HostKeyType() const { return hostKeyType_; }       // "ssh-ed25519", ...
    const std::string& HostKeyFingerprint() const { return fingerprint_; } // "SHA256:..."
    // Algorithms negotiated by the handshake, e.g. LIBSSH2_METHOD_KEX; empty when not connected.
    std::string NegotiatedMethod(int methodType) const;
    // Banner and SFTP version; no network traffic (reads what the handshake stored). Empty when not connected.
    CSshServerInfo ServerInfo() const;
    // All negotiated algorithms at once (no network traffic).
    CSshMethods NegotiatedMethods() const;

private:
    bool Fail(SshErrorKind kind, const std::string& message, int libssh2Code = 0);
    // Same, plus the id and arguments of the translatable text; 'message' is its English form (logs, fallback).
    bool Fail(SshErrorKind kind, SshMsg msgId, std::vector<std::string> args, const std::string& message,
              int libssh2Code = 0);
    // Error of the failed libssh2 call. msgId + args describe 'what' in the user's language; the English text of
    // libssh2 is appended to args (when msgId != None and libssh2 has a text). On a timeout no libssh2 text is
    // added and msgId stays (the UI shows its own "no answer" reason); without msgId the error gets NoAnswerTimeout.
    bool FailFromSession(const std::string& what, int code, SshMsg msgId = SshMsg::None,
                         std::vector<std::string> args = std::vector<std::string>());
    bool IsCancelled() const { return cancelled_.load(); }
    void Log(SshLogLevel level, const std::string& line) const;
    void LogVerbose(const char* call, const std::string& rawPath, const std::string& rawPath2 = std::string()) const;
    void LogAuthResult(const char* method, bool ok) const;
    void LogNegotiated() const;

    using Clock = std::chrono::steady_clock;
    bool WaitSocket(const Clock::time_point& deadline);
    static bool IsConnectionError(int code);
    // Shuts the socket down and remembers why; see "A broken connection" above.
    void MarkBroken(const char* reason);
    bool FailBroken();
    void ShutdownSocket();
    static int TimeoutMsOf(const CSshSite& site);

    // Calls a non-blocking libssh2 function returning an int (0 or a LIBSSH2_ERROR_* code) until it stops returning
    // LIBSSH2_ERROR_EAGAIN, waiting on the socket in between. Gives LIBSSH2_ERROR_TIMEOUT after timeoutMs without
    // a libssh2 return (idle timeout, restarted after every fn() call) and LIBSSH2_ERROR_SOCKET_DISCONNECT when
    // cancelled. A timeout, a cancel and every socket-level error mark the session broken (see above); on a broken
    // session fn is not called and kBrokenCode comes back. Later steps wrap their libssh2 calls the same way.
    static constexpr int kBrokenCode = -100000; // not a libssh2 code
    template <class Fn>
    int Run(Fn fn, int timeoutMs)
    {
        if (broken_)
            return kBrokenCode;
        for (;;)
        {
            int rc = fn(); // may run user callbacks for a long time: the deadline starts after it
            if (rc != LIBSSH2_ERROR_EAGAIN)
            {
                if (IsConnectionError(rc))
                    MarkBroken("connection error");
                return rc;
            }
            if (IsCancelled())
            {
                MarkBroken("cancel");
                return LIBSSH2_ERROR_SOCKET_DISCONNECT;
            }
            const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
            if (!WaitSocket(deadline))
            {
                if (IsCancelled())
                {
                    MarkBroken("cancel");
                    return LIBSSH2_ERROR_SOCKET_DISCONNECT;
                }
                MarkBroken("timeout");
                return LIBSSH2_ERROR_TIMEOUT;
            }
        }
    }

    // Same for a libssh2 function returning a pointer: nullptr with last errno EAGAIN means "try again". On failure
    // code is the LIBSSH2_ERROR_* code (kNullNoError when the function gave nullptr without an error).
    static constexpr int kNullNoError = 1;
    template <class Fn>
    auto RunPtr(Fn fn, int timeoutMs, int& code) -> decltype(fn())
    {
        decltype(fn()) result = nullptr;
        code = Run(
            [&]
            {
                result = fn();
                if (result != nullptr)
                    return 0;
                int err = libssh2_session_last_errno(session_);
                return err != 0 ? err : kNullNoError;
            },
            timeoutMs);
        return code == 0 ? result : nullptr;
    }

    // SFTP helpers (ssh_sftp_ops.cpp)
    // Start of every SFTP call: cancel / broken / not open checks, and the Verbose log line "SFTP <call> '<path>' -> '<path2>'".
    bool BeginSftp(const char* call, const std::string& rawPath = std::string(), const std::string& rawPath2 = std::string());
    bool FailSftp(SshMsg msgId, const std::string& what, const std::string& path, int code);
    bool FailExistsIfPresent(const std::string& path);
    bool RenameRaw(const std::string& from, const std::string& to, bool overwrite);
    int DirEntryState(const std::string& path); // 1 has entries, 0 empty, -1 cannot tell
    int StatRaw(const std::string& path, int statType, LIBSSH2_SFTP_ATTRIBUTES& attrs);
    bool StatCommon(const std::string& path, int statType, const char* what, CSftpEntry& out);
    // stat (following links) of a link target; returns the libssh2 code (0 ok); isDir false on any error
    int IsDirectory(const std::string& path, bool& isDir);
    std::string Display(const std::string& raw) const;
    void CloseSftpHandle(LIBSSH2_SFTP_HANDLE* h);
    std::string AsideName(const std::string& target) const;
    bool RenameAside(const std::string& from, const std::string& to);

    // remote command (ssh_exec.cpp)
    bool ExecOnChannel(LIBSSH2_CHANNEL* channel, const std::string& command, const CExecOptions& options,
                       CExecResult& out);
    bool FailExec(SshMsg msgId, const std::string& what, int code);
    bool FailExecWaiting(SshMsg msgId, const std::string& what, int code, int idleTimeoutSeconds);
    void CloseChannel(LIBSSH2_CHANNEL* channel);

    // transfers (ssh_transfer.cpp)
    bool FailLocal(SshMsg msgId, const char* what, DWORD winError);
    bool DownloadLoop(LIBSSH2_SFTP_HANDLE* h, const std::string& remotePath, HANDLE localFile, uint64_t startOffset,
                      uint64_t* done, uint64_t* totalSize);
    bool UploadLoop(LIBSSH2_SFTP_HANDLE* h, const std::string& remotePath, HANDLE localFile, uint64_t startOffset,
                    uint64_t* done);

    // authentication (ssh_auth.cpp)
    struct CAuthState;
    bool AuthAgent(const CSshSite& site, CAuthState& state);
    bool AuthKeyFile(const CSshSite& site, CAuthState& state);
    bool AuthPuttyKey(const CSecret& file, const CSshSite& site, CAuthState& state);
    bool AuthPassword(const CSshSite& site, CAuthState& state);
    bool AuthKeyboardInteractive(const CSshSite& site, CAuthState& state);
    bool TryKey(const CSecret& key, const CSecret& passphrase, const CSshSite& site, int& code);
    bool FailAuth(const CSshSite& site, const std::string& offered, const CAuthState& state);

    bool ConnectSocket(const CSshSite& site);
    bool ResolveHost(const CSshSite& site, int timeoutMs, ADDRINFOEXW*& list);
    SOCKET ConnectAddress(const ADDRINFOEXW* ai, int timeoutMs, std::string& why);
    void SetSocket(SOCKET s);
    void CloseSocket();

    bool CreateSession(const CSshSite& site);
    bool Handshake();
    bool VerifyHostKey(const CSshSite& site);
    void CloseAll();

    ISshCallbacks* cb_;
    ISshLogSink* logSink_ = nullptr;
    CKnownHosts* hosts_;
    bool globalInit_ = false;

    LIBSSH2_SESSION* session_ = nullptr;
    SOCKET socket_ = INVALID_SOCKET;
    int timeoutMs_ = 30000;
    size_t transferBufferSize_ = 256 * 1024;
    bool connected_ = false; // handshake done and host key accepted
    bool authenticated_ = false;
    bool broken_ = false;
    std::string brokenReason_;
    LIBSSH2_SFTP* sftp_ = nullptr;

    CNameCodec codec_; // for paths in error messages only

    std::atomic<bool> cancelled_{false};
    std::mutex socketMutex_; // guards the socket handle against Cancel's shutdown() racing with closesocket()

    CSshError error_;
    std::string hostKeyType_;
    std::string fingerprint_;
};
