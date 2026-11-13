#include "panel_session.h"

#include <cassert>
#include <exception>
#include <new>

#include "global_config.h"
#include "listing_format.h"
#include "local_name.h"
#include "ssh_init.h"

CPanelRequest::CPanelRequest(Kind kind) : kind_(kind), done_(CreateEventW(nullptr, TRUE, FALSE, nullptr))
{
    if (done_ == nullptr)
        throw std::bad_alloc();
}

CPanelRequest::~CPanelRequest()
{
    CloseHandle(done_);
}

CPanelSession::CPanelSession(CKnownHosts* hosts)
    : hosts_(hosts), interactionEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr))
{
    if (interactionEvent_ == nullptr)
        throw std::bad_alloc();
    TouchActivity();
    lastTraffic_ = IdleClock::now();
    try
    {
        // The new thread starts with lock+unlock of mutex_, so it never runs before threadId_ is set.
        std::lock_guard<std::mutex> lock(mutex_);
        thread_ = std::thread([this] { ThreadMain(); });
        threadId_ = thread_.get_id();
    }
    catch (...)
    {
        CloseHandle(interactionEvent_);
        throw;
    }
}

CPanelSession::~CPanelSession()
{
    Close();
    CloseHandle(interactionEvent_);
}

// ---- owner thread --------------------------------------------------------------------------------------------

std::shared_ptr<CPanelRequest> CPanelSession::Submit(std::shared_ptr<CPanelRequest> req)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!closing_)
        {
            TouchActivity();
            queue_.push_back(req);
            queueCv_.notify_one();
            return req;
        }
    }
    Finish(*req, SshErrorKind::Cancelled, "The session is closed");
    return req;
}

std::shared_ptr<CPanelRequest> CPanelSession::Connect(const CSite& site, CSecret password, CSecret passphrase)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::Connect));
    req->site_ = site;
    req->password_ = std::move(password);
    req->passphrase_ = std::move(passphrase);
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::List(const std::string& rawPath, bool withFreeSpace)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::List));
    req->path_ = rawPath;
    req->withFreeSpace_ = withFreeSpace;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::RealPath(const std::string& rawPath)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::RealPath));
    req->path_ = rawPath;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::Stat(const std::string& rawPath)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::Stat));
    req->path_ = rawPath;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::Rename(const std::string& fromRaw, const std::string& toRaw, bool overwrite)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::Rename));
    req->path_ = fromRaw;
    req->path2_ = toRaw;
    req->overwrite_ = overwrite;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::Mkdir(const std::string& rawPath)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::Mkdir));
    req->path_ = rawPath;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::RemoveFile(const std::string& rawPath)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::RemoveFile));
    req->path_ = rawPath;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::Chmod(const std::string& rawPath, uint32_t mode)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::Chmod));
    req->path_ = rawPath;
    req->mode_ = mode & 07777;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::Statvfs(const std::string& rawPath)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::Statvfs));
    req->path_ = rawPath;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::LStat(const std::string& rawPath)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::LStat));
    req->path_ = rawPath;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::ReadLink(const std::string& rawPath)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::ReadLink));
    req->path_ = rawPath;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::DownloadToFile(const std::string& rawPath, const std::wstring& localPath,
                                                             int64_t mtime)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::DownloadToFile));
    req->path_ = rawPath;
    req->localPath_ = localPath;
    req->mtime_ = mtime;
    return Submit(req);
}

std::shared_ptr<CPanelRequest> CPanelSession::Exec(const std::string& command, const CExecOptions& options)
{
    std::shared_ptr<CPanelRequest> req(new CPanelRequest(CPanelRequest::Kind::Exec));
    req->command_ = command;
    req->execOptions_ = options;
    return Submit(req);
}

CPanelSessionInfo CPanelSession::Info() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return info_;
}

CPanelConnectParams CPanelSession::CopyConnectParams() const
{
    CPanelConnectParams params;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasSite_ || hostKeyFingerprint_.empty())
        return params;
    params.site = site_;
    params.password = password_.Clone();
    params.passphrase = passphrase_.Clone();
    params.hostKeyType = hostKeyType_;
    params.hostKeyFingerprint = hostKeyFingerprint_;
    params.valid = true;
    return params;
}

std::shared_ptr<CPanelRequest> CPanelSession::Disconnect()
{
    return Submit(std::shared_ptr<CPanelRequest>(new CPanelRequest(CPanelRequest::Kind::Disconnect)));
}

bool CPanelSession::TryGetInteraction(CInteraction& out)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (interaction_.kind == CInteraction::Kind::None || interactionDelivered_)
        return false;
    if (AbortRequested())
        return false; // the request was cancelled (or the session closed): a dialog for it would be stale
    out = interaction_;
    interactionDelivered_ = true;
    ResetEvent(interactionEvent_);
    return true;
}

bool CPanelSession::AnswerHostKey(HostKeyDecision decision)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (interaction_.kind != CInteraction::Kind::HostKey || answered_)
        return false;
    hostKeyAnswer_ = decision;
    answered_ = true;
    answerCv_.notify_all();
    return true;
}

bool CPanelSession::AnswerPrompt(bool ok, CSecret answer)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (interaction_.kind != CInteraction::Kind::Prompt || answered_)
        return false;
    promptOk_ = ok;
    promptAnswer_ = std::move(answer);
    answered_ = true;
    answerCv_.notify_all();
    return true;
}

void CPanelSession::Cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    // Oldest unfinished request: the running one, else the first queued one.
    std::shared_ptr<CPanelRequest> target = running_ != nullptr ? running_ : (queue_.empty() ? nullptr : queue_.front());
    if (target == nullptr)
        return; // nothing to cancel; a flag set now would hit a later request
    target->cancelled_ = true;
    if (target == running_ && target->executed_)
        session_->Cancel(); // session_ exists: executed_ is set only by the session thread after creating it
    else if (target != running_ && keepaliveRunning_)
    {
        // The request waits behind a keepalive: interrupt it, so the request is not delayed. The session thread
        // disconnects afterwards (KeepaliveTick), the next request reconnects.
        keepaliveCancelled_ = true;
        session_->Cancel();
    }
    answerCv_.notify_all(); // wakes an interaction wait (AbortRequested sees the flag)
}

void CPanelSession::Close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closing_ = true;
        if (running_ != nullptr)
        {
            running_->cancelled_ = true;
            if (running_->executed_)
                session_->Cancel();
        }
        else if (keepaliveRunning_)
            session_->Cancel(); // a keepalive must not delay the close
        queueCv_.notify_all();
        answerCv_.notify_all();
    }
    if (thread_.joinable() && std::this_thread::get_id() != threadId_)
        thread_.join();
}

// ---- session thread ------------------------------------------------------------------------------------------

void CPanelSession::Finish(CPanelRequest& req, SshErrorKind kind, const std::string& message) noexcept
{
    SetError(req, kind, message);
    SetEvent(req.done_);
}

// Never throws (it is also used in catch blocks): when the message cannot be copied, the kind alone is reported.
void CPanelSession::SetError(CPanelRequest& req, SshErrorKind kind, const std::string& message, SshMsg msgId,
                             std::vector<std::string> args) noexcept
{
    req.error_ = CSshError();
    req.error_.kind = kind;
    try
    {
        req.error_.message = message;
        req.error_.msgId = msgId;
        req.error_.args = std::move(args);
    }
    catch (...)
    {
        req.error_.message.clear();
    }
}

void CPanelSession::ThreadMain()
{
    { std::lock_guard<std::mutex> wait(mutex_); } // see the constructor
    CSshThreadScope threadScope; // frees OpenSSL's per-thread state when the thread ends (declared first: destroyed last)
    try
    {
        session_.reset(new CSshSession(this, hosts_));
        session_->SetLogSink(&logSink_);
    }
    catch (...)
    {
    }
    for (;;)
    {
        std::shared_ptr<CPanelRequest> req;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // Wake up at least once a second: an idle session may owe the server a keepalive.
            queueCv_.wait_for(lock, std::chrono::seconds(1), [this] { return closing_ || !queue_.empty(); });
            if (closing_)
                break;
            if (queue_.empty())
            {
                lock.unlock();
                KeepaliveTick(); // between requests, on this thread: never concurrent with a request
                continue;
            }
            req = queue_.front();
            queue_.pop_front();
            running_ = req;
            // A request cancelled while queued is not run (Disconnect always runs: it cannot be undone by a cancel).
            req->executed_ = session_ != nullptr && (!req->cancelled_ || req->kind_ == CPanelRequest::Kind::Disconnect);
        }
        if (req->executed_)
        {
            try
            {
                Execute(*req);
                if (ShouldRetrySilently(*req))
                {
                    logSink_.Line(SshLogLevel::Normal, L"Connection lost: reconnecting and repeating the request");
                    ResetResult(*req);
                    Execute(*req); // reconnects through EnsureConnected (host key and prompts work as usual)
                }
            }
            catch (const std::exception& e)
            {
                try
                {
                    SetError(*req, SshErrorKind::Other, std::string("Internal error: ") + e.what());
                }
                catch (...) // building the text failed (out of memory)
                {
                    SetError(*req, SshErrorKind::Other, "Internal error");
                }
            }
            catch (...)
            {
                SetError(*req, SshErrorKind::Other, "Internal error");
            }
        }
        else if (session_ == nullptr)
            SetError(*req, SshErrorKind::Other, "The SSH session could not be created");

        bool wasCancelled;
        {
            // Under the lock, so a Cancel() sees either "running" (handled here) or "finished" (then it is a no-op).
            std::lock_guard<std::mutex> lock(mutex_);
            wasCancelled = req->cancelled_ && req->kind_ != CPanelRequest::Kind::Disconnect;
            running_.reset();
        }
        if (wasCancelled)
        {
            if (req->executed_)
            {
                try
                {
                    session_->Disconnect(); // a cancelled session is broken until Disconnect (also clears the cancel flag)
                }
                catch (...)
                {
                }
            }
            SetError(*req, SshErrorKind::Cancelled, "Cancelled");
            req->entries_.clear();
            req->exec_ = CExecResult();
        }
        lastTraffic_ = IdleClock::now();
        TouchActivity(); // before the owner is released: it may look at LastActivity right away
        SetEvent(req->done_);
    }

    // closing: finish what is left, drop the connection and the secrets
    std::deque<std::shared_ptr<CPanelRequest>> rest;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rest.swap(queue_);
    }
    for (const std::shared_ptr<CPanelRequest>& r : rest)
        Finish(*r, SshErrorKind::Cancelled, "The session is closed");
    try
    {
        if (session_ != nullptr)
            session_->Disconnect();
        session_.reset();
    }
    catch (...)
    {
    }
    logSink_.End();
    ForgetSite();
}

bool CPanelSession::IsReadOnly(CPanelRequest::Kind kind)
{
    switch (kind)
    {
    case CPanelRequest::Kind::List:
    case CPanelRequest::Kind::RealPath:
    case CPanelRequest::Kind::Stat:
    case CPanelRequest::Kind::LStat:
    case CPanelRequest::Kind::ReadLink:
    case CPanelRequest::Kind::Statvfs:
        return true;
    default: // Connect, Disconnect and everything that changes something (it may have run before the connection died)
        return false;
    }
}

bool CPanelSession::ShouldRetrySilently(const CPanelRequest& req)
{
    if (!IsReadOnly(req.kind_) || req.Ok() || session_ == nullptr || !session_->IsBroken())
        return false;
    // only "the server or the network closed the connection"; a timeout would make the user wait twice, a cancel is final
    if (session_->BrokenReason() != "connection error")
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return hasSite_ && !req.cancelled_ && !closing_;
}

void CPanelSession::ResetResult(CPanelRequest& req)
{
    req.error_ = CSshError();
    req.entries_.clear();
    req.realPath_.clear();
    req.attributes_ = CSftpEntry();
    req.vfs_ = CSftpVfs();
    req.hasVfs_ = false;
}

void CPanelSession::KeepaliveTick()
{
    if (session_ == nullptr || !session_->IsConnected() || !session_->HasSftp())
        return;
    const int fromTest = keepaliveOverride_.load();
    const int seconds = fromTest >= 0 ? fromTest : GlobalConfigStore().Get().keepaliveSeconds;
    if (!KeepaliveDue(lastTraffic_, IdleClock::now(), seconds))
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closing_ || !queue_.empty())
            return;
        keepaliveRunning_ = true;
        keepaliveCancelled_ = false;
    }
    bool ok = false;
    try
    {
        ok = session_->SendKeepalive(); // the core logs the success and the error text
    }
    catch (...)
    {
    }
    bool cancelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        keepaliveRunning_ = false;
        cancelled = keepaliveCancelled_;
        keepaliveCancelled_ = false;
    }
    lastTraffic_ = IdleClock::now();
    if (cancelled)
    {
        // A cancelled session is broken until Disconnect (which also clears the cancel flag); then the request that
        // waited for us finds a clean session and reconnects through EnsureConnected.
        try
        {
            session_->Disconnect();
        }
        catch (...)
        {
        }
        logSink_.Line(SshLogLevel::Normal, L"Keepalive cancelled: the connection is reopened by the next request");
        return;
    }
    if (!ok)
        logSink_.Line(SshLogLevel::Normal, L"Keepalive failed: the connection is reopened by the next request");
}

void CPanelSession::ForgetSite()
{
    std::lock_guard<std::mutex> lock(mutex_);
    hasSite_ = false;
    site_ = CSite();
    password_.clear();
    passphrase_.clear();
    hostKeyType_.clear();
    hostKeyFingerprint_.clear();
    info_ = CPanelSessionInfo();
}

bool CPanelSession::DoConnect(CPanelRequest& req)
{
    session_->Disconnect(); // also resets a broken session - and clears a cancel request that arrived before this line,
    {                       // so look at our own flag (set under the lock before CSshSession::Cancel) afterwards
        std::lock_guard<std::mutex> lock(mutex_);
        if (req.cancelled_ || closing_)
        {
            SetError(req, SshErrorKind::Cancelled, "Cancelled");
            return false;
        }
    }
    CSshSite site = ToSshSite(site_, password_.Clone(), passphrase_.Clone());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        info_ = CPanelSessionInfo(); // the old connection's data must not outlive it
        hostKeyAsked_ = false;
    }
    vfsUnsupported_ = false;
    logSink_.Begin(PanelLogTitle(SessionLogTarget(site_.user, site_.host, site_.port)));
    if (session_->Connect(site))
    {
        // A successful connect means the host key was trusted (known_hosts match, AcceptOnce or AcceptAndSave), so the
        // session's key is the one workers must see, whichever way it was accepted.
        // read from the session before the lock is taken (no lock held across session calls)
        const auto serverInfo = session_->ServerInfo();
        const auto methods = session_->NegotiatedMethods();
        const auto keyType = session_->HostKeyType();
        const auto keyFingerprint = session_->HostKeyFingerprint();
        std::lock_guard<std::mutex> lock(mutex_);
        hostKeyType_ = keyType;
        hostKeyFingerprint_ = keyFingerprint;
        info_.valid = true;
        ++connectCount_;
        info_.server = serverInfo;
        info_.methods = methods;
        info_.hostKeyType = hostKeyType_;
        info_.hostKeyFingerprint = hostKeyFingerprint_;
        info_.trust = !hostKeyAsked_ ? HostKeyTrust::Known
                      : lastHostKeyDecision_ == HostKeyDecision::AcceptAndSave ? HostKeyTrust::AcceptedAndSaved
                                                                              : HostKeyTrust::AcceptedOnce;
        return true;
    }
    req.error_ = session_->LastError();
    return false;
}

// Reconnects with the remembered site when the connection is gone (cancelled, timed out, closed by the server).
bool CPanelSession::EnsureConnected(CPanelRequest& req)
{
    if (session_->IsConnected() && session_->HasSftp())
        return true;
    if (!hasSite_)
    {
        SetError(req, SshErrorKind::ConnectFailed, "Not connected", SshMsg::NotConnected, {});
        return false;
    }
    return DoConnect(req);
}

void CPanelSession::Execute(CPanelRequest& req)
{
    // "libssh2 only on the session thread", checked in debug builds
    assert(std::this_thread::get_id() == threadId_);
    const std::string path = req.path_.empty() ? "." : req.path_;
    switch (req.kind_)
    {
    case CPanelRequest::Kind::Connect:
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            site_ = req.site_;
            password_ = std::move(req.password_);
            passphrase_ = std::move(req.passphrase_);
            hostKeyType_.clear(); // belongs to the previous connection until this one is accepted
            hostKeyFingerprint_.clear();
            hasSite_ = true;
        }
        if (!DoConnect(req))
        {
            bool cancelled;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                cancelled = req.cancelled_ || closing_;
            }
            if (!cancelled)
                ForgetSite(); // a cancelled connect can be retried by the next request
        }
        break;
    }
    case CPanelRequest::Kind::Disconnect:
        session_->Disconnect();
        logSink_.End();
        ForgetSite();
        break;
    case CPanelRequest::Kind::RealPath:
        if (EnsureConnected(req) && !session_->RealPath(path, req.realPath_))
            req.error_ = session_->LastError();
        break;
    case CPanelRequest::Kind::Stat:
        if (EnsureConnected(req) && !session_->Stat(path, req.attributes_))
            req.error_ = session_->LastError();
        break;
    case CPanelRequest::Kind::LStat:
        if (EnsureConnected(req) && !session_->LStat(path, req.attributes_))
            req.error_ = session_->LastError();
        break;
    case CPanelRequest::Kind::ReadLink:
        if (EnsureConnected(req) && !session_->ReadLink(path, req.realPath_))
            req.error_ = session_->LastError();
        break;
    case CPanelRequest::Kind::Statvfs:
        if (!EnsureConnected(req))
            break;
        if (session_->Statvfs(path, req.vfs_))
            req.hasVfs_ = true;
        else
            req.error_ = session_->LastError();
        break;
    case CPanelRequest::Kind::Rename:
        if (EnsureConnected(req) && !session_->Rename(req.path_, req.path2_, req.overwrite_))
            req.error_ = session_->LastError();
        break;
    case CPanelRequest::Kind::Mkdir:
        if (EnsureConnected(req) && !session_->Mkdir(req.path_))
            req.error_ = session_->LastError();
        break;
    case CPanelRequest::Kind::RemoveFile:
        if (EnsureConnected(req) && !session_->Remove(req.path_))
            req.error_ = session_->LastError();
        break;
    case CPanelRequest::Kind::Chmod:
        if (EnsureConnected(req) && !session_->Chmod(req.path_, req.mode_))
            req.error_ = session_->LastError();
        break;
    case CPanelRequest::Kind::DownloadToFile:
        DownloadFile(req);
        break;
    case CPanelRequest::Kind::Exec:
        if (EnsureConnected(req) && !session_->Exec(req.command_, req.execOptions_, req.exec_))
            req.error_ = session_->LastError();
        break;
    case CPanelRequest::Kind::List:
        if (!EnsureConnected(req))
            break;
        // resolve first ("", ".", relative paths, links in the path), then list the real directory
        if (!session_->RealPath(path, req.realPath_) || !session_->ListDir(req.realPath_, req.entries_))
            req.error_ = session_->LastError();
        else if (req.withFreeSpace_ && !vfsUnsupported_)
            ReadFreeSpaceAfterList(req);
        break;
    }
}

namespace
{
// The "<name>.part" file of a download: closed and deleted when it goes out of scope unless Commit() renamed it, so
// every exit path (error, cancel, exception) leaves nothing behind. (Like a C# using block with a delete in Dispose.)
class CPartialFile
{
public:
    explicit CPartialFile(const std::wstring& path)
        : path_(ExtendedLocalPath(path)),
          handle_(CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr))
    {
    }
    ~CPartialFile()
    {
        Close();
        if (!committed_ && handle_ != INVALID_HANDLE_VALUE)
            DeleteFileW(path_.c_str());
    }
    CPartialFile(const CPartialFile&) = delete;
    CPartialFile& operator=(const CPartialFile&) = delete;

    bool IsOpen() const { return handle_ != INVALID_HANDLE_VALUE && !closed_; }
    HANDLE Handle() const { return handle_; }
    void Close()
    {
        if (IsOpen())
            CloseHandle(handle_);
        closed_ = true;
    }
    // Gives the closed file its final name.
    bool Commit(const std::wstring& finalPath)
    {
        Close();
        committed_ = MoveFileExW(path_.c_str(), ExtendedLocalPath(finalPath).c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
        return committed_;
    }

private:
    std::wstring path_;
    HANDLE handle_;
    bool closed_ = false;
    bool committed_ = false;
};
} // namespace

// DownloadToFile on the session thread. The remote file is read into "<target>.part"; see CPartialFile.
void CPanelSession::DownloadFile(CPanelRequest& req)
{
    if (!EnsureConnected(req))
        return;
    CPartialFile partial(req.localPath_ + L".part");
    if (!partial.IsOpen())
    {
        const std::string code = std::to_string(GetLastError());
        SetError(req, SshErrorKind::Io, "The local file could not be created (Win32 error " + code + ")",
                 SshMsg::LocalFileCreateFailed, {code});
        return;
    }
    downloading_ = &req;
    uint64_t total = 0;
    const bool ok = session_->Download(req.path_, partial.Handle(), 0, nullptr, &total);
    downloading_ = nullptr;
    if (!ok)
    {
        req.error_ = session_->LastError();
        return;
    }
    if (req.mtime_ > 0)
    {
        const FILETIME time = UnixTimeToFileTime(req.mtime_);
        SetFileTime(partial.Handle(), nullptr, nullptr, &time); // best effort: a good copy with another time is fine
    }
    bool cancelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancelled = req.cancelled_ || closing_;
    }
    if (cancelled)
    {
        // the user gave up: no final-named file is left behind (the .part file is deleted when 'partial' goes away)
        SetError(req, SshErrorKind::Cancelled, "Cancelled");
        return;
    }
    if (!partial.Commit(req.localPath_))
    {
        const std::string code = std::to_string(GetLastError());
        SetError(req, SshErrorKind::Io, "The local file could not be renamed (Win32 error " + code + ")",
                 SshMsg::LocalFileRenameFailed, {code});
    }
}

// Called by the core on the session thread during a transfer.
bool CPanelSession::Progress(uint64_t done, uint64_t total)
{
    if (downloading_ != nullptr)
    {
        downloading_->bytesDone_ = done;
        downloading_->bytesTotal_ = total;
    }
    return true;
}

// Best effort after a successful listing: the list result stays good whatever happens here. A server that does not
// know the extension is not asked again until the next connect.
void CPanelSession::ReadFreeSpaceAfterList(CPanelRequest& req)
{
    if (session_->Statvfs(req.realPath_, req.vfs_))
        req.hasVfs_ = true;
    else if (session_->LastError().kind == SshErrorKind::Unsupported)
        vfsUnsupported_ = true;
    else if (!session_->IsConnected())
        req.error_ = session_->LastError(); // cancel or timeout: the connection is gone, report that
}

// ---- callbacks (session thread) ------------------------------------------------------------------------------

bool CPanelSession::AbortRequested() const
{
    return closing_ || (running_ != nullptr && running_->cancelled_);
}

// Posts interaction_ and blocks until it is answered or aborted. The core's idle timeout does not run meanwhile.
bool CPanelSession::WaitForAnswer(std::unique_lock<std::mutex>& lock)
{
    answered_ = false;
    interactionDelivered_ = false;
    SetEvent(interactionEvent_);
    answerCv_.wait(lock, [this] { return answered_ || AbortRequested(); });
    const bool ok = answered_;
    interaction_ = CInteraction();
    interactionDelivered_ = false;
    ResetEvent(interactionEvent_);
    return ok;
}

HostKeyDecision CPanelSession::VerifyHostKey(const std::string& host, int port, const std::string& keyType,
                                             const std::string& fingerprintSha256, bool changed)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (AbortRequested())
        return HostKeyDecision::Reject;
    interaction_ = CInteraction();
    interaction_.kind = CInteraction::Kind::HostKey;
    interaction_.host = host;
    interaction_.port = port;
    interaction_.keyType = keyType;
    interaction_.fingerprint = fingerprintSha256;
    interaction_.changed = changed;
    interaction_.threadId = std::this_thread::get_id();
    const bool answered = WaitForAnswer(lock);
    const HostKeyDecision decision = answered ? hostKeyAnswer_ : HostKeyDecision::Reject;
    hostKeyAsked_ = true;
    lastHostKeyDecision_ = decision;
    return decision;
}

bool CPanelSession::Prompt(PromptKind kind, const std::string& name, const std::string& instruction,
                           const std::string& promptText, bool echo, CSecret& answer)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (AbortRequested())
        return false;
    interaction_ = CInteraction();
    interaction_.kind = CInteraction::Kind::Prompt;
    interaction_.promptKind = kind;
    interaction_.name = name;
    interaction_.instruction = instruction;
    interaction_.promptText = promptText;
    interaction_.echo = echo;
    interaction_.threadId = std::this_thread::get_id();
    promptOk_ = false;
    if (!WaitForAnswer(lock) || !promptOk_)
    {
        promptAnswer_.clear();
        return false;
    }
    answer = std::move(promptAnswer_);
    return true;
}
