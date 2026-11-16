// One panel connection: a CSshSession plus the single thread that owns it. The owner thread (Sally's main thread, or a
// test) submits requests and waits for them with a Win32 event; it never calls libssh2.
//
// Threading rules
//  - Connect/List/Stat/RealPath/Disconnect/Answer*/TryGetInteraction/Close are called from ONE owner thread.
//  - Cancel() is meant for the owner thread too (while it waits for a request); it is thread safe anyway.
//  - The CSshSession is created, used and destroyed on the session thread only (except CSshSession::Cancel).
//
// Requests run in FIFO order, one at a time. Cancel() cancels the oldest unfinished request (the running one, or
// when none is running the first queued one) and nothing else: requests queued behind it still run. A cancelled
// session is broken (core rule), so the session thread disconnects after the cancelled request and the next
// List/Stat/RealPath reconnects automatically with the remembered site (kept in memory, secrets as CSecret, wiped
// by Disconnect and Close). A cancel that arrives when no request is pending does nothing, so it can never leak
// into a later request.
#pragma once

#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "idle_policy.h"
#include "session_log.h"
#include "site.h"
#include "ssh_session.h"

// One request. Created by CPanelSession::Connect/List/..., shared with the session thread. Everything except
// DoneEvent()/IsDone()/Wait() may be read only after the request is done.
class CPanelRequest
{
public:
    enum class Kind
    {
        Connect,
        List,
        RealPath,
        Stat,
        Rename,
        Mkdir,
        RemoveFile, // (not "DeleteFile": windows.h defines that name as a macro)
        Chmod,
        Statvfs,
        LStat,
        ReadLink,
        DownloadToFile,
        Exec,
        Disconnect
    };

    ~CPanelRequest();
    CPanelRequest(const CPanelRequest&) = delete;
    CPanelRequest& operator=(const CPanelRequest&) = delete;

    // Manual-reset event, signalled when the request is done (successful or not). Wait on it with
    // MsgWaitForMultipleObjects; the handle belongs to the request (valid while you hold the shared_ptr).
    HANDLE DoneEvent() const { return done_; }
    bool IsDone() const { return WaitForSingleObject(done_, 0) == WAIT_OBJECT_0; }
    // True when done within timeoutMs (INFINITE allowed).
    bool Wait(DWORD timeoutMs) const { return WaitForSingleObject(done_, timeoutMs) == WAIT_OBJECT_0; }

    // ---- valid after done
    bool Ok() const { return !error_.IsError(); }
    bool Cancelled() const { return error_.kind == SshErrorKind::Cancelled; }
    const CSshError& Error() const { return error_; }
    Kind GetKind() const { return kind_; }
    // List: the entries (without "." and ".."). RealPath, List: the resolved absolute path. Stat: the attributes
    // (links followed; rawName empty).
    const std::vector<CSftpEntry>& Entries() const { return entries_; }
    const std::string& ResolvedPath() const { return realPath_; }
    const CSftpEntry& Attributes() const { return attributes_; }
    // Statvfs, and List made with withFreeSpace: the file system size. HasVfs is false when the server cannot tell (the
    // list itself is still fine).
    bool HasVfs() const { return hasVfs_; }
    const CSftpVfs& Vfs() const { return vfs_; }
    // ReadLink: the link target (raw bytes).
    const std::string& LinkTarget() const { return realPath_; }
    // DownloadToFile, readable from any thread while it runs: bytes written so far and the size of the remote file
    // (0 until the server told it).
    uint64_t BytesDone() const { return bytesDone_.load(); }
    uint64_t BytesTotal() const { return bytesTotal_.load(); }
    // Exec: the command's output (stdout and stderr in arrival order, raw bytes, cut at the output limit), its exit
    // status, the signal that ended it ("" = none) and whether the output was cut. A command that ran and failed is an
    // Ok() request: look at ExitStatus().
    const std::string& Output() const { return exec_.output; }
    int ExitStatus() const { return exec_.exitStatus; }
    const std::string& ExitSignal() const { return exec_.exitSignal; }
    bool OutputTruncated() const { return exec_.truncated; }

private:
    friend class CPanelSession;
    explicit CPanelRequest(Kind kind);

    const Kind kind_;
    HANDLE done_;
    // input
    std::string path_;   // raw bytes
    std::string path2_;  // Rename: the new path (raw bytes)
    bool overwrite_ = false; // Rename
    bool withFreeSpace_ = false; // List: also read the free space of the listed directory
    uint32_t mode_ = 0;      // Chmod: the permission bits
    std::wstring localPath_; // DownloadToFile: the target file
    std::string command_;    // Exec: the shell text
    CExecOptions execOptions_; // Exec
    int64_t mtime_ = 0;      // DownloadToFile: Unix seconds to give the file, 0 = leave it
    CSite site_;         // Connect
    CSecret password_;
    CSecret passphrase_;
    // state, guarded by CPanelSession::mutex_
    bool cancelled_ = false;
    bool executed_ = false; // the session thread started it (so CSshSession::Cancel is meaningful)
    // output, written by the session thread before done_ is signalled
    CSshError error_;
    std::vector<CSftpEntry> entries_;
    std::string realPath_;
    CSftpEntry attributes_;
    CSftpVfs vfs_;
    bool hasVfs_ = false;
    CExecResult exec_;
    std::atomic<uint64_t> bytesDone_{0}; // DownloadToFile progress, written by the session thread
    std::atomic<uint64_t> bytesTotal_{0};
};

// What the session thread is asking the user. Plain data; show a dialog for it and call AnswerHostKey/AnswerPrompt.
struct CInteraction
{
    enum class Kind
    {
        None,
        HostKey, // host, port, keyType, fingerprint, changed
        Prompt   // promptKind, name, instruction, promptText, echo
    };
    Kind kind = Kind::None;
    std::string host; // UTF-8 like everything here
    int port = 22;
    std::string keyType;
    std::string fingerprint; // "SHA256:..."
    bool changed = false;    // a known key differs (show a warning)
    PromptKind promptKind = PromptKind::KeyboardInteractive; // KeyPassphrase: promptText is the key file
    std::string name;        // prompt: window title from the server (may be empty)
    std::string instruction; // prompt: text above the prompts (may be empty)
    std::string promptText;
    bool echo = false;        // false: ask like a password
    std::thread::id threadId; // thread the callback ran on (diagnostics; tests check it is not the owner thread)
};

// How the host key of the current connection became trusted.
enum class HostKeyTrust
{
    Known,            // it matched a known_hosts file (the plugin's own or the user's OpenSSH file)
    AcceptedOnce,     // the user accepted it for this session only
    AcceptedAndSaved  // the user accepted it and it was saved to the plugin's known_hosts
};

// What the session learned at connect time, for the info and security dialogs. A copy: safe to keep and to read on any
// thread, no network traffic.
struct CPanelSessionInfo
{
    bool valid = false; // false: not connected (never, failed, disconnected, closed)
    CSshServerInfo server;
    CSshMethods methods;
    std::string hostKeyType;        // "ssh-ed25519", ...
    std::string hostKeyFingerprint; // "SHA256:..."
    HostKeyTrust trust = HostKeyTrust::Known;
};

// What a worker connection needs to log in like the panel did (see CPanelSession::CopyConnectParams).
struct CPanelConnectParams
{
    bool valid = false; // false: no site has been connected successfully (never, failed, disconnected, closed)
    CSite site;
    CSecret password;   // independent copies
    CSecret passphrase;
    std::string hostKeyType;        // "ssh-ed25519", ...
    std::string hostKeyFingerprint; // "SHA256:..." of the key the panel connection accepted
    // Filled by the caller (fs/). Each worker builds its own CKnownHosts from them: the own file is read, and written for
    // "Accept and save"; the user's OpenSSH file is only read. Empty = not used. UTF-8.
    std::string knownHostsOwnPath;
    std::string knownHostsUserPath;
};

class CPanelSession : private ISshCallbacks
{
public:
    // hosts is owned by the caller and must outlive this object; nullptr = no known hosts (see CSshSession).
    explicit CPanelSession(CKnownHosts* hosts);
    ~CPanelSession();
    CPanelSession(const CPanelSession&) = delete;
    CPanelSession& operator=(const CPanelSession&) = delete;

    // ---- Requests (owner thread). After Close() every call returns an already finished, failed request.
    // Connect: disconnects, connects with the site (host key / prompt interactions may happen) and remembers the site
    // and the secrets for automatic reconnects. A failed (not cancelled) connect forgets them again.
    std::shared_ptr<CPanelRequest> Connect(const CSite& site, CSecret password, CSecret passphrase);
    // Raw bytes; "" means the login directory. Result: entries + the resolved real path.
    // withFreeSpace: after the listing the session also asks for the free space of that directory (one more round trip;
    // a failure there, e.g. a server without the extension, is ignored). The answer is in Vfs()/HasVfs().
    std::shared_ptr<CPanelRequest> List(const std::string& rawPath, bool withFreeSpace = false);
    std::shared_ptr<CPanelRequest> RealPath(const std::string& rawPath);
    std::shared_ptr<CPanelRequest> Stat(const std::string& rawPath);
    // Renames/moves on the server (same rules as CSshSession::Rename: an existing target gives Exists unless overwrite).
    std::shared_ptr<CPanelRequest> Rename(const std::string& fromRaw, const std::string& toRaw, bool overwrite);
    // Creates one directory (mode 0755). An existing path gives Exists, a missing parent NotFound.
    std::shared_ptr<CPanelRequest> Mkdir(const std::string& rawPath);
    // Removes one file or one symbolic link (a link is removed itself, never what it points to). A directory gives an
    // error; a missing path gives NotFound.
    std::shared_ptr<CPanelRequest> RemoveFile(const std::string& rawPath);
    // Sets the permission bits (mode & 07777, the special bits setuid/setgid/sticky included) of one path. Like chmod(1)
    // it follows a symbolic link. A missing path gives NotFound, a refusal PermissionDenied.
    std::shared_ptr<CPanelRequest> Chmod(const std::string& rawPath, uint32_t mode);
    // Size and free space of the file system of one path (kind Unsupported without the server extension).
    std::shared_ptr<CPanelRequest> Statvfs(const std::string& rawPath);
    // Attributes of one path without following a final link (Attributes()), and the target of a link (LinkTarget()).
    std::shared_ptr<CPanelRequest> LStat(const std::string& rawPath);
    std::shared_ptr<CPanelRequest> ReadLink(const std::string& rawPath);
    // Copies one remote file (a link is followed) to the local file 'localPath' (for F3 view). The data goes to
    // "<localPath>.part" first and is renamed to 'localPath' only after the last byte, so an incomplete file never has the
    // final name; on any failure or cancel the ".part" file is deleted again and 'localPath' is not touched. mtime > 0:
    // the finished file gets that modification time (Unix seconds). Progress: BytesDone()/BytesTotal(). A local error
    // gives kind Io.
    std::shared_ptr<CPanelRequest> DownloadToFile(const std::string& rawPath, const std::wstring& localPath, int64_t mtime);
    // Runs a shell command on the server and waits for it on the session thread (the owner waits for the request as for
    // any other; Cancel() ends a command that runs too long and the next request reconnects). See CSshSession::Exec for
    // the options and the error kinds; the default waits for the command without an idle timeout and keeps 1 MiB of output.
    std::shared_ptr<CPanelRequest> Exec(const std::string& command, const CExecOptions& options = CExecOptions());
    // Closes the connection and forgets the site and the secrets (no automatic reconnect until the next Connect).
    std::shared_ptr<CPanelRequest> Disconnect();

    // ---- Interaction. InteractionEvent() is a manual-reset event that is signalled when a NEW interaction waits for an
    // answer and reset by TryGetInteraction (which hands each interaction out once), so a wait loop cannot spin.
    // The session thread blocks (the core's idle timeout does not run meanwhile) until AnswerHostKey/AnswerPrompt, a
    // Cancel of the running request or Close; then the callback returns reject/cancel.
    HANDLE InteractionEvent() const { return interactionEvent_; }
    bool TryGetInteraction(CInteraction& out);
    // false when no matching interaction is waiting (already answered, cancelled, or of the other kind).
    bool AnswerHostKey(HostKeyDecision decision);
    bool AnswerPrompt(bool ok, CSecret answer);

    // Any thread. Copies the remembered site, the secrets and the host key the connection accepted, so worker
    // connections can log in the same way without asking again (the fingerprint is the key they must see). The result
    // is invalid until a Connect succeeded. Safe while a request runs.
    CPanelConnectParams CopyConnectParams() const;

    // Any thread. Banner, negotiated algorithms and host key of the last successful connect (updated by every reconnect);
    // valid == false before the first connect and after a disconnect. Does not talk to the server.
    CPanelSessionInfo Info() const;

    // See the class comment. Returns at once.
    void Cancel();

    // Cancels, stops accepting requests, joins the thread, disconnects, wipes the secrets. Idempotent; safe while a
    // request runs or an interaction waits. Pending requests finish as Cancelled.
    void Close();

    // Any thread. Id of this connection's log in SessionLogStore(); 0 while there is none (before the first connect and
    // after Disconnect). The log is opened by the first connect and closed by Disconnect or Close.
    unsigned LogId() const { return logSink_.LogId(); }

    // Any thread. A line of the plugin itself in this connection's log (dropped above the configured level).
    void LogLine(SshLogLevel level, const std::wstring& text) { logSink_.Line(level, text); }

    // Any thread. Counts the successful connects (the first one and every reconnect). A change tells the owner that
    // what it learned from the old connection (a cached listing) may be out of date.
    unsigned ConnectCount() const { return connectCount_.load(); }

    // Any thread. When the last request was submitted or finished (the keepalives do not count). A detached FS that has
    // been unused for long is closed; see DetachedIdleExpired.
    IdleClock::time_point LastActivity() const { return IdleClock::time_point(IdleClock::duration(lastActivity_.load())); }

    // Any thread. Keepalive interval in seconds for THIS session: 0 = off, negative = use the global configuration
    // (the default). Mainly for tests, which cannot wait a minute.
    void SetKeepaliveSeconds(int seconds) { keepaliveOverride_ = seconds; }

    // The session thread's id (valid from the constructor on).
    std::thread::id SessionThreadId() const { return threadId_; }

private:
    // ISshCallbacks, called on the session thread
    HostKeyDecision VerifyHostKey(const std::string& host, int port, const std::string& keyType,
                                  const std::string& fingerprintSha256, bool changed) override;
    bool Prompt(PromptKind kind, const std::string& name, const std::string& instruction, const std::string& promptText,
                bool echo, CSecret& answer) override;
    bool Progress(uint64_t done, uint64_t total) override;

    std::shared_ptr<CPanelRequest> Submit(std::shared_ptr<CPanelRequest> req);
    static void Finish(CPanelRequest& req, SshErrorKind kind, const std::string& message) noexcept;
    // msgId/args: the translatable text of the error (see CSshError::msgId); message stays the English fallback.
    static void SetError(CPanelRequest& req, SshErrorKind kind, const std::string& message, SshMsg msgId = SshMsg::None,
                         std::vector<std::string> args = std::vector<std::string>()) noexcept;

    // session thread
    void ThreadMain();
    void Execute(CPanelRequest& req);
    bool EnsureConnected(CPanelRequest& req);
    bool DoConnect(CPanelRequest& req);
    void ForgetSite();
    void TouchActivity() { lastActivity_ = IdleClock::now().time_since_epoch().count(); }
    // Idle tick of the session thread (no request waits): sends a keepalive when the connection has been quiet long enough.
    void KeepaliveTick();
    // The silent retry of D59: a request that only reads (so running it twice is harmless) and failed because the
    // connection was lost (not cancelled, not a timeout) is run once more after a reconnect.
    bool ShouldRetrySilently(const CPanelRequest& req);
    static bool IsReadOnly(CPanelRequest::Kind kind);
    static void ResetResult(CPanelRequest& req);
    void ReadFreeSpaceAfterList(CPanelRequest& req);
    void DownloadFile(CPanelRequest& req);
    bool WaitForAnswer(std::unique_lock<std::mutex>& lock); // true: answered, false: aborted
    bool AbortRequested() const;                            // mutex_ held

    CKnownHosts* hosts_;
    CSessionLogSink logSink_; // before session_: the session writes to it until it is destroyed
    std::thread thread_;
    std::thread::id threadId_; // same as thread_.get_id(), readable while thread_ is being joined

    // guarded by mutex_
    mutable std::mutex mutex_;
    std::condition_variable queueCv_;
    std::condition_variable answerCv_;
    std::deque<std::shared_ptr<CPanelRequest>> queue_;
    std::shared_ptr<CPanelRequest> running_;
    bool closing_ = false;
    bool keepaliveRunning_ = false; // the session thread is inside SendKeepalive (Close and Cancel interrupt it)
    bool keepaliveCancelled_ = false; // Cancel interrupted that keepalive: the session thread disconnects afterwards
    CInteraction interaction_; // kind None = nothing waiting
    bool interactionDelivered_ = false;
    bool answered_ = false;
    HostKeyDecision hostKeyAnswer_ = HostKeyDecision::Reject;
    bool promptOk_ = false;
    CSecret promptAnswer_;

    HANDLE interactionEvent_;

    std::atomic<IdleClock::rep> lastActivity_{0};
    std::atomic<int> keepaliveOverride_{-1};
    std::atomic<unsigned> connectCount_{0};
    IdleClock::time_point lastTraffic_; // session thread only: last request or keepalive

    // session_ lives on the session thread only (Cancel reads it only while running_ is executing)
    std::unique_ptr<CSshSession> session_;
    // Written by the session thread under mutex_, read by it without the lock and by CopyConnectParams with the lock.
    CSite site_;
    CSecret password_;
    CSecret passphrase_;
    bool hasSite_ = false;
    std::string hostKeyType_;        // of the last successful connect
    std::string hostKeyFingerprint_;
    CPanelSessionInfo info_;
    bool hostKeyAsked_ = false; // VerifyHostKey ran during the current connect (the key matched no known host)
    HostKeyDecision lastHostKeyDecision_ = HostKeyDecision::Reject;
    CPanelRequest* downloading_ = nullptr; // session thread only: the DownloadToFile request whose progress is reported
    bool vfsUnsupported_ = false; // session thread only: the server said "no statvfs", not asked again until reconnect
};
