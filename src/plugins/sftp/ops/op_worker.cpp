#include "op_worker.h"

#include <exception>
#include <utility>

#include "listing_format.h"
#include "local_name.h"
#include "op_trace.h"
#include "ssh_init.h"
#include "text_util.h"

namespace
{
constexpr int kIdleWaitMs = 200; // a waiting worker looks again at least this often
// An idle worker of a running operation gives its slot and connection back after this long (D58); not at once, so short
// gaps while a directory is explored do not cause reconnects.
constexpr int kIdleReleaseMs = 3000;
constexpr uint32_t kModeTypeMask = 0170000;
constexpr uint32_t kModeRegular = 0100000;
constexpr uint32_t kModeDirectory = 0040000;
constexpr uint32_t kModeSymlink = 0120000;

std::wstring ToWide(const std::string& utf8)
{
    return CNameCodec::Utf8().Decode(utf8);
}

std::wstring WindowsErrorText(DWORD error)
{
    if (error == 0)
        error = ERROR_GEN_FAILURE;
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text;
    if (length > 0 && buffer != nullptr)
        text.assign(buffer, length);
    LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
        text.pop_back();
    return text + L" (" + std::to_wstring(error) + L")";
}

Problem ProblemFromWin32(DWORD error, Problem fallback)
{
    if (error == ERROR_DISK_FULL || error == ERROR_HANDLE_DISK_FULL)
        return Problem::DiskFull;
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return Problem::NotFound;
    return fallback;
}

// The core reports local file errors as kind Io with "(Windows error N)" in the message and remote ones as kind Io
// without it; that is all the information there is.
Problem ProblemFromSshError(const CSshError& error, Problem fallback)
{
    switch (error.kind)
    {
    case SshErrorKind::NotFound:
        return Problem::NotFound;
    case SshErrorKind::PermissionDenied:
        return Problem::PermissionDenied;
    case SshErrorKind::Exists:
        return Problem::TargetExists;
    case SshErrorKind::Io:
    {
        if (error.message.find("Windows error 112)") != std::string::npos ||
            error.message.find("Windows error 39)") != std::string::npos)
            return Problem::DiskFull;
        const bool local = error.message.find("(Windows error ") != std::string::npos;
        return local ? Problem::LocalIoError : Problem::TransferFailed;
    }
    default:
        return fallback;
    }
}

int64_t UnixTimeOf(const FILETIME& ft)
{
    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    constexpr int64_t kEpochDiff100ns = 116444736000000000LL; // 1601 -> 1970
    return (static_cast<int64_t>(t.QuadPart) - kEpochDiff100ns) / 10000000;
}

uint64_t SizeOf(DWORD high, DWORD low)
{
    return (static_cast<uint64_t>(high) << 32) | low;
}

std::string ParentOfRemote(const std::string& path)
{
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    const size_t slash = p.find_last_of('/');
    if (slash == std::string::npos)
        return std::string(); // relative to the login directory
    return slash == 0 ? std::string("/") : p.substr(0, slash);
}

std::wstring ParentOfLocal(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool IsSpecialFile(const CSftpEntry& entry)
{
    // devices, pipes and sockets would block or return nothing; "no type bits" (a server that sends none) is a file
    const uint32_t type = entry.permissions & kModeTypeMask;
    return type != 0 && type != kModeRegular && type != kModeDirectory && type != kModeSymlink;
}

// Deletes a local file, also a read-only one.
bool DeleteLocalFileForce(const std::wstring& path, DWORD& error)
{
    if (DeleteFileW(path.c_str()))
        return true;
    error = GetLastError();
    if (error == ERROR_ACCESS_DENIED && SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL))
    {
        if (DeleteFileW(path.c_str()))
            return true;
        error = GetLastError();
    }
    return false;
}

// Closes a handle when the function that owns it returns (like a using block in C#).
class CHandleGuard
{
public:
    explicit CHandleGuard(HANDLE handle) : handle_(handle) {}
    ~CHandleGuard() { Close(); }
    void Close() // early, e.g. before the file is deleted
    {
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    CHandleGuard(const CHandleGuard&) = delete;
    CHandleGuard& operator=(const CHandleGuard&) = delete;

private:
    HANDLE handle_;
};

// What is at a local path now (missing and "cannot tell" both count as missing: a real problem shows when it is opened).
CTransferSide StatLocalSide(const std::wstring& path)
{
    CTransferSide side;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return side;
    side.exists = true;
    side.isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    side.size = SizeOf(data.nFileSizeHigh, data.nFileSizeLow);
    side.mtime = UnixTimeOf(data.ftLastWriteTime);
    return side;
}

std::wstring LastLocalComponent(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// "name.ext" -> "name (2).ext" in the same folder; empty when no free name was found.
std::wstring FreeLocalPath(const std::wstring& target)
{
    const std::wstring dir = ParentOfLocal(target);
    const std::wstring free = AutoRenameLocal(LastLocalComponent(target), [&](const std::wstring& candidate)
                                              { return StatLocalSide(MakeLocalPath(dir, candidate)).exists; });
    return free.empty() ? std::wstring() : MakeLocalPath(dir, free);
}

// Closes a FindFirstFile handle on every way out (also when something in the loop throws). Plain CloseHandle is wrong
// for these handles, hence an own guard (like a using block around the enumeration in C#).
class CFindGuard
{
public:
    explicit CFindGuard(HANDLE find) : find_(find) {}
    ~CFindGuard()
    {
        if (find_ != INVALID_HANDLE_VALUE)
            FindClose(find_);
    }
    CFindGuard(const CFindGuard&) = delete;
    CFindGuard& operator=(const CFindGuard&) = delete;

private:
    HANDLE find_;
};

// A symbolic link or junction (not any reparse point: cloud placeholders and the like are normal files and folders).
bool IsLocalLink(const std::wstring& path)
{
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(ExtendedLocalPath(path).c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return false;
    CFindGuard findGuard(find);
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
           (data.dwReserved0 == IO_REPARSE_TAG_SYMLINK || data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
}

// Names a local directory after links are followed (volume + file index), for the loop guard. Empty when that cannot be
// read: the depth limit still stops a loop then.
std::string LocalDirKey(const std::wstring& path)
{
    HANDLE h = CreateFileW(ExtendedLocalPath(path).c_str(), FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return std::string();
    CHandleGuard guard(h);
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info))
        return std::string();
    return "local:" + std::to_string(info.dwVolumeSerialNumber) + ":" +
           std::to_string(SizeOf(info.nFileIndexHigh, info.nFileIndexLow));
}

void MarkViaLink(std::vector<CQueueItem>& items, bool viaLink)
{
    for (CQueueItem& item : items)
        item.viaLink = viaLink;
}

// Counts a worker as "waiting for a slot" for exactly as long as it lives, also when something throws.
class CSlotWaitGuard
{
public:
    explicit CSlotWaitGuard(IWorkerHost& host) : host_(host) { host_.WorkerWaitingForSlot(true); }
    ~CSlotWaitGuard() { host_.WorkerWaitingForSlot(false); }
    CSlotWaitGuard(const CSlotWaitGuard&) = delete;
    CSlotWaitGuard& operator=(const CSlotWaitGuard&) = delete;

private:
    IWorkerHost& host_;
};

// A symbolic link or junction: take size, time and "is a directory" from what it points to.
void FollowReparsePoint(const std::wstring& path, DWORD& attributes, uint64_t& size, FILETIME& writeTime)
{
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return; // a broken link stays what it is; the upload of it fails later with a clear error
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileInformationByHandle(h, &info))
    {
        attributes = info.dwFileAttributes;
        size = SizeOf(info.nFileSizeHigh, info.nFileSizeLow);
        writeTime = info.ftLastWriteTime;
    }
    CloseHandle(h);
}
} // namespace

// ---- shared helpers -------------------------------------------------------------------------------------------

std::string AppendRemoteName(const std::string& dir, const std::string& rawName)
{
    if (dir.empty())
        return rawName;
    return dir.back() == '/' ? dir + rawName : dir + "/" + rawName;
}

std::string LastRemoteName(const std::string& path)
{
    std::string p = path;
    while (p.size() > 1 && p.back() == '/')
        p.pop_back();
    const size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

void AddDownloadItems(std::vector<CQueueItem>& out, const std::string& remotePath, const std::string& rawName, bool isDir,
                      uint64_t size, int64_t mtime, const std::wstring& localDir, const CNameCodec& codec, int depth,
                      const std::wstring& targetName)
{
    bool lossy = false;
    const std::wstring name = targetName.empty() ? DecodeRemoteNameToLocal(codec, rawName, &lossy) : targetName;
    const bool valid = !lossy && IsValidLocalName(name);

    CQueueItem item;
    item.remotePath = remotePath;
    item.localPath = valid ? MakeLocalPath(localDir, name) : std::wstring(); // empty: the worker parks the item
    item.parentLocal = localDir;
    item.depth = depth;
    if (isDir)
    {
        if (valid)
        {
            item.type = ItemType::MakeDirLocal;
            out.push_back(item);
        }
        item.type = ItemType::ExploreRemote;
    }
    else
    {
        item.type = ItemType::Download;
        item.size = size;
        item.mtime = mtime;
    }
    out.push_back(std::move(item));
}

void AddUploadItems(std::vector<CQueueItem>& out, const std::wstring& localPath, const std::wstring& name, bool isDir,
                    uint64_t size, int64_t mtime, const std::string& remoteDir, const CNameCodec& codec, int depth)
{
    bool unrepresentable = false;
    const std::string rawName = codec.Encode(name, &unrepresentable);
    // An empty or unusable encoded name (or a target folder with a NUL) must never become a path: the item would
    // otherwise address the parent folder. The worker parks such an item (empty remotePath).
    if (!IsValidRemoteEntryName(rawName) || remoteDir.find('\0') != std::string::npos)
        unrepresentable = true;

    CQueueItem item;
    item.localPath = localPath;
    item.remotePath = unrepresentable ? std::string() : AppendRemoteName(remoteDir, rawName);
    item.parentRemote = remoteDir;
    item.depth = depth;
    if (isDir)
    {
        if (!unrepresentable)
        {
            item.type = ItemType::MakeDirRemote;
            out.push_back(item);
        }
        item.type = ItemType::ExploreLocal;
    }
    else
    {
        item.type = ItemType::Upload;
        item.size = size;
        item.mtime = mtime;
    }
    out.push_back(std::move(item));
}

// ---- CItemResult builders -------------------------------------------------------------------------------------

CWorker::CItemResult CWorker::Failure(Problem problem, std::wstring text)
{
    CItemResult r;
    r.kind = CItemResult::Kind::Failed;
    r.problem = problem;
    r.text = std::move(text);
    return r;
}

CWorker::CItemResult CWorker::FailureOfError(Problem problem, const CSshError& error)
{
    CItemResult r = Failure(problem, ToWide(error.message));
    r.sshError = error;
    return r;
}

CWorker::CItemResult CWorker::Parked(Problem problem, std::wstring text)
{
    CItemResult r;
    r.kind = CItemResult::Kind::Parked;
    r.problem = problem;
    r.text = std::move(text);
    return r;
}

CWorker::CItemResult CWorker::Skipped()
{
    CItemResult r;
    r.kind = CItemResult::Kind::Skipped;
    return r;
}

CWorker::CItemResult CWorker::Returned()
{
    CItemResult r;
    r.kind = CItemResult::Kind::ReturnToWaiting;
    return r;
}

CWorker::CItemResult CWorker::Replaced()
{
    CItemResult r;
    r.kind = CItemResult::Kind::Replaced;
    return r;
}

// ---- thread ---------------------------------------------------------------------------------------------------

CWorker::CWorker(IWorkerHost& host, int index)
    : host_(host), index_(index),
      hosts_(host.ConnectParams().knownHostsOwnPath, host.ConnectParams().knownHostsUserPath), slot_(WorkerSlots()),
      session_(this, &hosts_)
{
    session_.SetLogSink(&logSink_);
}

CWorker::~CWorker()
{
    Join();
}

void CWorker::Start()
{
    thread_ = std::thread([this] { ThreadMain(); });
}

void CWorker::Cancel()
{
    session_.Cancel();
}

void CWorker::Join()
{
    if (thread_.joinable())
        thread_.join();
}

void CWorker::ThreadMain()
{
    try
    {
        CSshThreadScope scope; // first: it must outlive every libssh2/OpenSSL call of this thread
        try
        {
            Run();
        }
        catch (...)
        {
            // nothing may leave a thread function; an item that was being processed stays Processing (only possible
            // after an out-of-memory in the queue itself)
        }
        session_.Disconnect();
        logSink_.End(); // the log lives as long as the worker (idle disconnects only add a line)
    }
    catch (...)
    {
    }
    try
    {
        ReleaseSlot(); // also done by the destructor of slot_, but the operation's counters want to hear it
    }
    catch (...)
    {
    }
    try
    {
        host_.WorkerExited();
    }
    catch (...)
    {
    }
}

void CWorker::Run()
{
    codec_ = host_.ConnectParams().site.MakeCodec();
    CQueue& queue = host_.Queue();
    CQueueItem item;
    OpTrace("worker %d: started", index_);
    // A worker lives until the operation is cancelled: when the work has run out it idles, so a solved or retried item
    // (which becomes Waiting again) is picked up at once, without starting new threads.
    while (!host_.IsCancelRequested())
    {
        if (host_.IsPaused())
        {
            session_.Disconnect(); // a paused worker holds no connection; the next item reconnects
            ReleaseSlot();
            host_.WaitWhilePaused();
            continue;
        }
        if (!queue.HasWaiting())
        {
            WaitForNextChance();
            continue;
        }
        if (!AcquireSlot())
            continue; // cancelled or paused while waiting: the loop looks again
        if (queue.TakeNextWaiting(item))
        {
            ProcessItem(item);
            idle_ = false; // the idle time counts from the end of the item
            continue;
        }
        WaitForNextChance(); // another worker was faster
    }
}

// The slot comes before the item: a worker that takes an item and then waits for a slot would keep the item from the
// workers that already hold one. False when the operation was cancelled or paused during the wait.
bool CWorker::AcquireSlot()
{
    if (slot_.Held())
        return true;
    if (slot_.TryAcquire())
    {
        host_.WorkerHoldsSlot(true);
        return true;
    }
    BeginLog(); // so the wait is in the log
    logSink_.Line(SshLogLevel::Normal, L"Waiting for a free connection slot");
    OpTrace("worker %d: waiting for a connection slot", index_);
    bool got = false;
    {
        CSlotWaitGuard waiting(host_);
        got = slot_.Acquire([this] { return host_.IsCancelRequested() || host_.IsPaused(); });
    }
    if (!got)
        return false;
    host_.WorkerHoldsSlot(true);
    logSink_.Line(SshLogLevel::Normal, L"Got a connection slot");
    return true;
}

void CWorker::ReleaseSlot()
{
    if (!slot_.Held())
        return;
    slot_.Release();
    host_.WorkerHoldsSlot(false);
}

void CWorker::WaitForNextChance()
{
    CQueue& queue = host_.Queue();
    if (queue.GetOperationState() == OperationState::Running)
    {
        ReleaseWhenIdleForLong();
        queue.WaitForWork(kIdleWaitMs); // other workers are busy and may produce items (explore)
        return;
    }
    if (session_.IsConnected() || session_.HasSftp())
        session_.Disconnect(); // nothing to do until the user solves something: do not hold the connection
    ReleaseSlot();
    host_.CheckRunOver();
    queue.WaitForWaitingItem(kIdleWaitMs);
}

// An idle worker of a running operation keeps its slot and connection for a short while (an explore item may produce
// work any moment), then gives both back so another operation can use them. Run() takes a slot again when work comes.
void CWorker::ReleaseWhenIdleForLong()
{
    const auto now = std::chrono::steady_clock::now();
    if (!idle_)
    {
        idle_ = true;
        idleSince_ = now;
        return;
    }
    if (now - idleSince_ < std::chrono::milliseconds(kIdleReleaseMs) || !slot_.Held())
        return;
    session_.Disconnect(); // the next item reconnects
    ReleaseSlot();
}

void CWorker::ProcessItem(const CQueueItem& item)
{
    CItemResult result;
    try
    {
        result = RunItem(item);
    }
    catch (const std::exception& e)
    {
        result = Failure(Problem::Other, ToWide(e.what()));
    }
    catch (...)
    {
        result = Failure(Problem::Other, std::wstring());
    }
    if (result.kind == CItemResult::Kind::ReturnToWaiting)
        session_.Disconnect(); // a cancelled session must be closed before it is used again

    try
    {
        Report(item, result);
    }
    catch (...)
    {
    }
}

void CWorker::Report(const CQueueItem& item, const CItemResult& result)
{
    CQueue& queue = host_.Queue();
    switch (result.kind)
    {
    case CItemResult::Kind::Done:
        queue.Finish(item.uid, ItemState::Done);
        break;
    case CItemResult::Kind::Failed:
        queue.Finish(item.uid, ItemState::Failed, result.problem, result.text, result.sshError);
        break;
    case CItemResult::Kind::Parked:
        queue.Finish(item.uid, ItemState::UserInputNeeded, result.problem, result.text, result.sshError);
        break;
    case CItemResult::Kind::Skipped:
        queue.Finish(item.uid, ItemState::Skipped);
        break;
    case CItemResult::Kind::ReturnToWaiting:
        queue.ReturnToWaiting(item.uid);
        break;
    case CItemResult::Kind::Replaced:
        break; // the queue call that replaced the item already changed its state
    }
    host_.ItemChanged(item.uid);
}

bool CWorker::NeedsConnection(ItemType type)
{
    switch (type)
    {
    case ItemType::ExploreRemote:
    case ItemType::Download:
    case ItemType::Upload:
    case ItemType::MakeDirRemote:
    case ItemType::DeleteRemoteFile:
    case ItemType::DeleteRemoteDir:
    case ItemType::SetAttrs:
        return true;
    default:
        return false;
    }
}

// An item that fails because the connection broke (or cannot be made) is tried again on a fresh connection after the
// backoff delays of the settings (default 1 s, 3 s, 10 s); the item stays Processing meanwhile. When the last attempt
// fails the item is Failed with Problem::ConnectionLost (RetryAllFailed brings it back). A cancel or a pause during the
// wait gives the item back to the queue.
CWorker::CItemResult CWorker::RunItem(const CQueueItem& taken)
{
    CQueue& queue = host_.Queue();
    const std::vector<unsigned>& delays = host_.Settings().reconnectDelaysMs;
    CItemResult result;
    for (size_t attempt = 0;; ++attempt)
    {
        if (host_.IsCancelRequested() || host_.IsPaused())
            return Returned();
        CQueueItem item; // fresh: an earlier attempt may have changed it (partial file owned, new target name)
        if (!queue.GetItem(taken.uid, item))
            item = taken;
        if (!NeedsConnection(item.type))
            return ExecuteOnce(item);

        if (EnsureConnected(result))
        {
            unreachable_ = false;
            result = ExecuteOnce(item);
        }
        if (result.kind == CItemResult::Kind::Failed || result.problem != Problem::None)
            OpTrace("worker %d: item %llu failed: kind %d problem %d connectionLost %d", index_,
                    static_cast<unsigned long long>(item.uid), static_cast<int>(result.kind),
                    static_cast<int>(result.problem), result.connectionLost ? 1 : 0);
        if (!result.connectionLost)
            return result;

        session_.Disconnect();
        // after a worker gave up on one item the others fail at once: the server will not be back within seconds
        const size_t allowed = unreachable_ ? 0 : delays.size();
        if (attempt >= allowed)
        {
            unreachable_ = true;
            return result; // Failed, Problem::ConnectionLost
        }
        if (!host_.SleepUnlessInterrupted(delays[attempt]))
            return Returned();
    }
}

CWorker::CItemResult CWorker::ExecuteOnce(const CQueueItem& item)
{
    switch (item.type)
    {
    case ItemType::ExploreRemote:
        return ExploreRemote(item);
    case ItemType::ExploreLocal:
        return ExploreLocal(item);
    case ItemType::Download:
        return Download(item);
    case ItemType::Upload:
        return Upload(item);
    case ItemType::MakeDirLocal:
        return MakeDirLocal(item);
    case ItemType::MakeDirRemote:
        return MakeDirRemote(item);
    case ItemType::DeleteRemoteFile:
        return DeleteRemote(item, false);
    case ItemType::DeleteRemoteDir:
        return DeleteRemote(item, true);
    case ItemType::DeleteLocalFile:
        return DeleteLocal(item, false);
    case ItemType::DeleteLocalDir:
        return DeleteLocal(item, true);
    case ItemType::SetAttrs:
        return SetAttrs(item);
    }
    return Failure(Problem::Other, std::wstring());
}

// ---- connection -----------------------------------------------------------------------------------------------

// Called only for a key the known_hosts files do not vouch for. Exactly the key the panel accepted is trusted without
// asking. When the panel pinned a key and the server shows another one, that is a changed key whatever the files say
// (a man in the middle looks like this); every other key goes to the operation (which asks the user, or rejects).
HostKeyDecision CWorker::VerifyHostKey(const std::string& host, int port, const std::string& keyType,
                                       const std::string& fingerprintSha256, bool changed)
{
    const std::string& pinned = host_.ConnectParams().hostKeyFingerprint;
    if (!pinned.empty())
    {
        if (fingerprintSha256 == pinned)
            return HostKeyDecision::AcceptOnce;
        changed = true;
    }
    return host_.AskHostKey(index_, host, port, keyType, fingerprintSha256, changed);
}

bool CWorker::Prompt(PromptKind kind, const std::string& name, const std::string& instruction,
                     const std::string& promptText, bool echo, CSecret& answer)
{
    return host_.AskPrompt(index_, kind, name, instruction, promptText, echo, answer);
}

bool CWorker::Progress(uint64_t done, uint64_t)
{
    if (host_.IsCancelRequested() || host_.IsPaused())
        return false; // pause stops the transfer like a cancel; the item goes back to Waiting with its partial size
    if (done > reported_)
    {
        host_.AddTransferred(done - reported_);
        reported_ = done;
    }
    host_.Queue().SetProgress(transferUid_, done);
    return true;
}

void CWorker::BeginTransfer(uint64_t uid, uint64_t startOffset)
{
    transferUid_ = uid;
    transferStart_ = startOffset;
    reported_ = startOffset;
    host_.Queue().SetProgress(uid, startOffset);
}

// The progress callback is throttled, so the last bytes (and every byte of a small file) are added here.
void CWorker::EndTransfer(uint64_t bytesTransferred)
{
    const uint64_t end = transferStart_ + bytesTransferred;
    if (end > reported_)
    {
        host_.AddTransferred(end - reported_);
        reported_ = end;
    }
}

// A problem that every further item would meet in the same way (the server or the user said no); network trouble is not.
bool CWorker::IsLoginProblem(SshErrorKind kind)
{
    return kind == SshErrorKind::HostKeyRejected || kind == SshErrorKind::HostKeyChanged ||
           kind == SshErrorKind::AuthFailed || kind == SshErrorKind::KeyFileError || kind == SshErrorKind::NoSftp;
}

// An interactive operation pauses (the window shows the reason, Resume tries again) and the item goes back to Waiting.
// Without a user the item fails and the next ones fail at once, without another login attempt.
void CWorker::StopAfterLoginProblem(CItemResult& failure)
{
    if (host_.Settings().interactive)
    {
        host_.PauseForLoginProblem(failure.text, failure.sshError);
        failure = Returned();
        return;
    }
    loginFailed_ = true;
    loginFailure_ = failure;
}

bool CWorker::EnsureConnected(CItemResult& failure)
{
    if (session_.IsConnected() && session_.HasSftp())
        return true;
    if (loginFailed_)
    {
        failure = loginFailure_;
        return false;
    }
    session_.Disconnect(); // a broken or half-open session starts from nothing; this also forgets an earlier Cancel
    // so a cancel or pause that came in since RunItem looked is checked again here (later ones reach Connect itself)
    if (host_.IsCancelRequested() || host_.IsPaused())
    {
        failure = Returned();
        return false;
    }

    const CPanelConnectParams& params = host_.ConnectParams();
    BeginLog();
    if (!session_.Connect(ToSshSite(params.site, params.password.Clone(), params.passphrase.Clone())))
    {
        const CSshError error = session_.LastError();
        session_.Disconnect();
        OpTrace("worker %d: connect failed, kind %d: %s", index_, static_cast<int>(error.kind), error.message.c_str());
        if (error.kind == SshErrorKind::Cancelled)
        {
            failure = Returned();
            return false;
        }
        failure = FailureOfError(Problem::ConnectionLost, error);
        // network trouble may pass; a refused host key, a failed login or a bad key file will not
        failure.connectionLost = error.kind == SshErrorKind::ConnectFailed || error.kind == SshErrorKind::Timeout;
        if (IsLoginProblem(error.kind))
            StopAfterLoginProblem(failure);
        return false;
    }
    return true;
}

// Opens the worker's log (does nothing when it is open already).
void CWorker::BeginLog()
{
    const CPanelConnectParams& params = host_.ConnectParams();
    logSink_.Begin(WorkerLogTitle(SessionLogTarget(params.site.user, params.site.host, params.site.port),
                                  host_.OperationNumber(), index_ + 1));
}

CWorker::CItemResult CWorker::FromSessionError(Problem fallback)
{
    const CSshError& error = session_.LastError();
    if (error.kind == SshErrorKind::Cancelled)
        return Returned();
    CItemResult result = FailureOfError(ProblemFromSshError(error, fallback), error);
    if (!session_.IsConnected())
    {
        result.problem = Problem::ConnectionLost;
        result.connectionLost = true;
    }
    return result;
}

// ---- explore --------------------------------------------------------------------------------------------------

// childrenViaLink: the directory is a link, or below one (see CQueueItem::viaLink). A move then still removes the link
// itself (this item's "after" action deletes it as a link) but nothing below it; below a link no "after" item is made.
CWorker::CItemResult CWorker::ReplaceWithChildren(const CQueueItem& item, std::vector<CQueueItem> children,
                                                  ItemType afterType, bool childrenViaLink)
{
    CQueue& queue = host_.Queue();
    MarkViaLink(children, childrenViaLink);
    // a move deletes the source directory afterwards; a delete operation deletes the directory it explored; a chmod
    // changes the directory itself after its content (when directories are to be changed)
    const COperationSettings& settings = host_.Settings();
    const bool deleting = settings.direction == TransferDirection::Delete;
    const bool changing = settings.direction == TransferDirection::Chmod && settings.chmodDirs;
    if ((settings.move || deleting || changing) && !item.viaLink)
    {
        CQueueItem after; // the directory itself is dealt with once everything in it is done
        after.type = afterType;
        after.remotePath = item.remotePath;
        after.localPath = item.localPath;
        after.depth = item.depth;
        queue.ReplaceWithChildren(item.uid, std::move(children), &after);
    }
    else
    {
        queue.ReplaceWithChildren(item.uid, std::move(children));
    }
    return Replaced();
}

// Delete mode of ExploreRemote: the children are delete items (files and links) and explore items (real directories),
// and the directory itself is deleted last (the "after" item, Delayed until its children are done). A symbolic link is
// NEVER followed: a link to a directory is deleted as a link, so what it points to survives.
CWorker::CItemResult CWorker::ExploreForDelete(const CQueueItem& item)
{
    if (item.depth > kMaxExploreDepth)
        return Failure(Problem::SymlinkLoop, std::wstring());
    std::vector<CQueueItem> children;
    if (!IsRemoteLink(item.remotePath)) // a link has no children here: its "after" item removes the link
    {
        std::vector<CSftpEntry> entries;
        if (!session_.ListDir(item.remotePath, entries))
        {
            if (session_.LastError().kind == SshErrorKind::NotFound)
                return Done(); // already gone
            return FromSessionError(Problem::Other);
        }
        for (const CSftpEntry& entry : entries)
        {
            if (!IsValidRemoteEntryName(entry.rawName))
                continue; // the directory then cannot be removed and the item parks with the server's message
            CQueueItem child;
            child.remotePath = AppendRemoteName(item.remotePath, entry.rawName);
            child.depth = item.depth + 1;
            child.type = entry.isDir && !entry.isLink ? ItemType::ExploreRemote : ItemType::DeleteRemoteFile;
            children.push_back(std::move(child));
        }
    }
    return ReplaceWithChildren(item, std::move(children), ItemType::DeleteRemoteDir, false);
}

// Chmod mode of ExploreRemote (a recursive chmod): the children are SetAttrs items (files, when files are to be changed)
// and explore items (real directories); the directory itself is changed last (the "after" item). A symbolic link inside
// the tree is skipped completely: chmod on a link would change what it points to, which may be outside the tree.
CWorker::CItemResult CWorker::ExploreForChmod(const CQueueItem& item)
{
    if (item.depth > kMaxExploreDepth)
        return Failure(Problem::SymlinkLoop, std::wstring());
    if (IsRemoteLink(item.remotePath))
        return Done(); // a root that is a link: chmod would change its target, so it is skipped, never walked into
    std::vector<CSftpEntry> entries;
    if (!session_.ListDir(item.remotePath, entries))
    {
        if (session_.LastError().kind == SshErrorKind::NotFound)
            return Done(); // already gone
        return FromSessionError(Problem::Other);
    }
    std::vector<CQueueItem> children;
    for (const CSftpEntry& entry : entries)
    {
        if (!IsValidRemoteEntryName(entry.rawName) || entry.isLink)
            continue;
        if (!entry.isDir && !host_.Settings().chmodFiles)
            continue;
        CQueueItem child;
        child.remotePath = AppendRemoteName(item.remotePath, entry.rawName);
        child.depth = item.depth + 1;
        child.type = entry.isDir ? ItemType::ExploreRemote : ItemType::SetAttrs;
        children.push_back(std::move(child));
    }
    return ReplaceWithChildren(item, std::move(children), ItemType::SetAttrs, false);
}

CWorker::CItemResult CWorker::ExploreRemote(const CQueueItem& item)
{
    if (host_.Settings().direction == TransferDirection::Delete)
        return ExploreForDelete(item);
    if (host_.Settings().direction == TransferDirection::Chmod)
        return ExploreForChmod(item);
    if (item.depth > kMaxExploreDepth)
        return Failure(Problem::SymlinkLoop, std::wstring());
    std::wstring localDir;
    bool renamed = false;
    CItemResult stop;
    if (!ResolveLocalTarget(item, localDir, renamed, stop))
        return stop;
    if (renamed && !EnsureDirectory(localDir)) // no MakeDirLocal item exists for a directory that had an invalid name
        return Failure(Problem::DirCreateFailed, WindowsErrorText(GetLastError()));

    const bool isLink = host_.Settings().move && !item.viaLink && IsRemoteLink(item.remotePath);

    // Loop guard: a link back to a directory that was explored already resolves to the same real path.
    std::string realPath;
    if (!session_.RealPath(item.remotePath, realPath))
        return FromSessionError(Problem::Other);
    if (host_.ExploredDirs().Contains(realPath))
        return Failure(Problem::SymlinkLoop, ToWide(realPath));

    std::vector<CSftpEntry> entries;
    if (!session_.ListDir(item.remotePath, entries))
        return FromSessionError(Problem::Other);
    if (!host_.ExploredDirs().Insert(realPath)) // another worker was faster
        return Failure(Problem::SymlinkLoop, ToWide(realPath));

    std::vector<CQueueItem> children;
    for (const CSftpEntry& entry : entries)
    {
        if (!IsValidRemoteEntryName(entry.rawName) || IsSpecialFile(entry))
            continue;
        const std::string remote = AppendRemoteName(item.remotePath, entry.rawName);
        const bool isDir = EntryIsDirectory(entry);
        uint64_t size = entry.size;
        int64_t mtime = entry.mtime;
        if (!isDir && entry.isLink)
        {
            // the listing describes the link itself; the file it points to has the size that will be transferred
            CSftpEntry target;
            if (session_.Stat(remote, target))
            {
                size = target.size;
                mtime = target.mtime;
            }
            else if (!session_.IsConnected())
            {
                return FromSessionError(Problem::Other);
            }
        }
        AddDownloadItems(children, remote, entry.rawName, isDir, size, mtime, localDir, codec_, item.depth + 1);
    }
    return ReplaceWithChildren(item, std::move(children), ItemType::DeleteRemoteDir, item.viaLink || isLink);
}

bool CWorker::IsRemoteLink(const std::string& path)
{
    CSftpEntry entry;
    return session_.LStat(path, entry) && entry.isLink; // a failed LStat: the next call reports the problem
}

CWorker::CItemResult CWorker::ExploreLocal(const CQueueItem& item)
{
    if (item.depth > kMaxExploreDepth)
        return Failure(Problem::SymlinkLoop, std::wstring());
    std::string remoteDir;
    bool renamed = false;
    CItemResult stop;
    if (!ResolveRemoteTarget(item, remoteDir, renamed, stop))
        return stop;

    // Loop guard like the remote one: a junction or link back to a directory that was explored already.
    const std::string dirKey = LocalDirKey(item.localPath);
    if (!dirKey.empty() && host_.ExploredDirs().Contains(dirKey))
        return Failure(Problem::SymlinkLoop, item.localPath);
    const bool isLink = host_.Settings().move && !item.viaLink && IsLocalLink(item.localPath);

    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(MakeLocalPath(item.localPath, L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return Failure(ProblemFromWin32(error, Problem::LocalIoError), WindowsErrorText(error));
    }
    CFindGuard findGuard(find);

    std::vector<CQueueItem> children;
    if (renamed)
    {
        // no MakeDirRemote item exists for a directory that could not be named: the first child creates it
        CQueueItem makeDir;
        makeDir.type = ItemType::MakeDirRemote;
        makeDir.localPath = item.localPath;
        makeDir.remotePath = remoteDir;
        makeDir.parentRemote = item.parentRemote;
        makeDir.depth = item.depth;
        children.push_back(std::move(makeDir));
    }
    for (bool more = true; more; more = FindNextFileW(find, &data) != FALSE)
    {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..")
            continue;
        const std::wstring path = MakeLocalPath(item.localPath, name);
        DWORD attributes = data.dwFileAttributes;
        uint64_t size = SizeOf(data.nFileSizeHigh, data.nFileSizeLow);
        FILETIME writeTime = data.ftLastWriteTime;
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            FollowReparsePoint(path, attributes, size, writeTime);
        AddUploadItems(children, path, name, (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0, size, UnixTimeOf(writeTime),
                       remoteDir, codec_, item.depth + 1);
    }
    const DWORD error = GetLastError(); // of the failed FindNextFileW: ERROR_NO_MORE_FILES at the normal end
    if (error != ERROR_NO_MORE_FILES)
        return Failure(Problem::LocalIoError, WindowsErrorText(error));
    // remembered only now: a listing that failed above can be retried
    if (!dirKey.empty() && !host_.ExploredDirs().Insert(dirKey)) // another worker was faster
        return Failure(Problem::SymlinkLoop, item.localPath);
    return ReplaceWithChildren(item, std::move(children), ItemType::DeleteLocalDir, item.viaLink || isLink);
}

// ---- transfers ------------------------------------------------------------------------------------------------

// truncate: start the file anew; otherwise keep the content (resume). A missing parent directory is created: its
// MakeDirLocal item may not have run yet (several workers take items in order but not in lock step).
HANDLE CWorker::OpenLocalTarget(const std::wstring& path, bool truncate, DWORD& error)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               truncate ? CREATE_ALWAYS : OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            return h;
        error = GetLastError();
        if (error != ERROR_PATH_NOT_FOUND || !EnsureDirectory(ParentOfLocal(path)))
            break;
    }
    return INVALID_HANDLE_VALUE;
}

// ---- conflicts ------------------------------------------------------------------------------------------------

// Where a download goes. Normally the item's local path; a Rename solve with a name of the user builds a new path in the
// same folder (this also rescues an item whose remote name is no Windows name). A name that is not valid parks again.
bool CWorker::ResolveLocalTarget(const CQueueItem& item, std::wstring& target, bool& userNamed, CItemResult& stop)
{
    userNamed = false;
    if (item.forceAction == SolveAction::Rename && !item.forceNewName.empty())
    {
        if (!IsValidLocalName(item.forceNewName))
        {
            stop = Parked(Problem::InvalidLocalName, item.forceNewName);
            return false;
        }
        const std::wstring dir = item.parentLocal.empty() ? ParentOfLocal(item.localPath) : item.parentLocal;
        target = MakeLocalPath(dir, item.forceNewName);
        userNamed = true;
        return true;
    }
    if (item.localPath.empty())
    {
        stop = Parked(Problem::InvalidLocalName);
        return false;
    }
    target = item.localPath;
    return true;
}

// The same for an upload; the new name is encoded with the site's encoding.
bool CWorker::ResolveRemoteTarget(const CQueueItem& item, std::string& target, bool& userNamed, CItemResult& stop)
{
    userNamed = false;
    if (item.forceAction == SolveAction::Rename && !item.forceNewName.empty())
    {
        bool unrepresentable = false;
        const std::string raw = codec_.Encode(item.forceNewName, &unrepresentable);
        if (unrepresentable || !IsValidRemoteEntryName(raw))
        {
            stop = Failure(Problem::InvalidRemoteName, item.forceNewName);
            return false;
        }
        target = AppendRemoteName(item.parentRemote, raw);
        userNamed = true;
        return true;
    }
    // an empty path, or one that addresses the item's own parent folder, can only come from a bug: never write there
    if (item.remotePath.empty() ||
        (!item.parentRemote.empty() && NormalizeRemotePath(item.remotePath) == NormalizeRemotePath(item.parentRemote)))
    {
        stop = Failure(Problem::InvalidRemoteName, std::wstring());
        return false;
    }
    target = item.remotePath;
    return true;
}

// The user's own name for the target is never overwritten without asking; our own half-written target is continued
// (when the target can hold a partial: always for downloads, for uploads only without safe upload); everything else
// is up to the policy and the user's solve action.
CConflictDecision CWorker::Decide(const CQueueItem& item, const CTransferSide& source, const CTransferSide& target,
                                  bool userNamed, bool resumeOwnTarget) const
{
    if (userNamed)
        return DecideConflict(ConflictPolicy::Ask, SolveAction::Retry, source, target);
    if (resumeOwnTarget && item.ownsPartial && item.forceAction == SolveAction::Retry && target.exists && !target.isDir)
    {
        CConflictDecision own;
        own.action = ConflictAction::Transfer;
        own.resumeOffset = target.size <= source.size ? target.size : 0; // longer than the source: start again
        return own;
    }
    return DecideConflict(host_.Settings().policy, item.forceAction, source, target);
}

CWorker::CItemResult CWorker::ParkConflict(const CQueueItem& item, const CConflictDecision& decision,
                                           const CTransferSide& target, const std::wstring& shownPath)
{
    host_.Queue().SetTargetInfo(item.uid, target.size, target.mtime);
    return Parked(decision.problem, shownPath); // the window words the problem; the text is the target
}

// The size and time of what is at a remote path (links followed). A missing path is not an error.
bool CWorker::StatRemoteSide(const std::string& path, CTransferSide& side, CItemResult& failure)
{
    side = CTransferSide();
    CSftpEntry entry;
    if (session_.Stat(path, entry))
    {
        side.exists = true;
        side.isDir = entry.isDir;
        side.size = entry.size;
        side.mtime = entry.mtime;
        return true;
    }
    if (session_.LastError().kind == SshErrorKind::NotFound)
        return true;
    failure = FromSessionError(Problem::Other);
    return false;
}

// A free " (n)" name next to 'target'; empty when none was found or the server could not answer (statFailed).
std::string CWorker::FreeRemotePath(const std::string& target, bool& statFailed)
{
    const std::string dir = ParentOfRemote(target);
    statFailed = false;
    const std::string free = AutoRenameRemote(LastRemoteName(target), [&](const std::string& candidate)
    {
        CSftpEntry entry;
        if (session_.Stat(AppendRemoteName(dir, candidate), entry))
            return true;
        if (session_.LastError().kind != SshErrorKind::NotFound)
            statFailed = true; // stop the search: every further Stat would fail the same way
        return false;
    });
    return free.empty() || statFailed ? std::string() : AppendRemoteName(dir, free);
}

// The size of the safe-upload partial file to continue, 0 when there is none or this attempt cannot trust it. A partial
// the operation wrote itself, or one the user asked to resume, is continued when it is not longer than the source.
bool CWorker::PartialToResume(const std::string& partialPath, uint64_t sourceSize, bool trusted, uint64_t& size,
                              CItemResult& failure)
{
    size = 0;
    if (!trusted)
        return true;
    CTransferSide side;
    if (!StatRemoteSide(partialPath, side, failure))
        return false;
    if (side.exists && !side.isDir && side.size > 0 && side.size <= sourceSize)
        size = side.size;
    return true;
}

// ---- transfers ------------------------------------------------------------------------------------------------

CWorker::CItemResult CWorker::Download(const CQueueItem& item)
{
    std::wstring target;
    bool userNamed = false;
    CItemResult stop;
    if (!ResolveLocalTarget(item, target, userNamed, stop))
        return stop;
    const COperationSettings& settings = host_.Settings();
    CQueue& queue = host_.Queue();

    uint64_t offset = 0;
    const CTransferSide existing = StatLocalSide(target);
    if (existing.exists)
    {
        CTransferSide source; // the size and time the server reports now (the listing may be old, or the root of a selection)
        CItemResult failure;
        if (!StatRemoteSide(item.remotePath, source, failure))
            return failure;
        if (!source.exists)
            return Failure(Problem::NotFound, ToWide(item.remotePath));

        const CConflictDecision decision = Decide(item, source, existing, userNamed, true);
        if (decision.action == ConflictAction::Skip)
            return Skipped();
        if (decision.action == ConflictAction::Park)
            return ParkConflict(item, decision, existing, target);
        if (decision.action == ConflictAction::Rename)
        {
            target = FreeLocalPath(target);
            if (target.empty())
                return Failure(Problem::TargetExists, std::wstring());
        }
        else
        {
            offset = decision.resumeOffset;
        }
    }
    DWORD error = 0;
    HANDLE file = OpenLocalTarget(target, offset == 0, error);
    if (file == INVALID_HANDLE_VALUE)
        return Failure(ProblemFromWin32(error, Problem::LocalIoError), WindowsErrorText(error));
    CHandleGuard guard(file);
    queue.SetOwnsPartial(item.uid);
    // The user's answer (Overwrite, Resume) is used up now: the file is ours, so a retry after a pause or a lost
    // connection continues it (Decide) instead of starting again. Also records a renamed target.
    queue.Retarget(item.uid, target, std::string());

    BeginTransfer(item.uid, offset);
    uint64_t transferred = 0;
    const bool ok = session_.Download(item.remotePath, file, offset, &transferred, nullptr);
    EndTransfer(transferred);
    if (ok && settings.preserveTime && item.mtime > 0)
    {
        const FILETIME time = UnixTimeToFileTime(item.mtime);
        SetFileTime(file, nullptr, nullptr, &time); // best effort: a file with the wrong time is still a good copy
    }
    if (!ok)
        return FromSessionError(Problem::TransferFailed); // the partial file stays: the next attempt continues it

    if (settings.move && !item.viaLink && !RemoveRemote(item.remotePath, false))
        return FromSessionError(Problem::DeleteFailed);
    return Done();
}

bool CWorker::WantsResume(const CQueueItem& item) const
{
    return item.forceAction == SolveAction::Resume ||
           (item.forceAction == SolveAction::Retry && host_.Settings().policy == ConflictPolicy::Resume);
}

// Safe upload: the data goes to "<name>.filepart" and the final name appears with one rename after the last byte, so
// the existing target stays untouched until then. A partial left by an earlier attempt is continued when that attempt
// was ours (a pause, a lost connection) or the user asked to resume; else it is written again from the start.
CWorker::CItemResult CWorker::Upload(const CQueueItem& item)
{
    std::string target;
    bool userNamed = false;
    CItemResult stop;
    if (!ResolveRemoteTarget(item, target, userNamed, stop))
        return stop;
    const COperationSettings& settings = host_.Settings();
    CQueue& queue = host_.Queue();

    HANDLE file = CreateFileW(ExtendedLocalPath(item.localPath).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return Failure(ProblemFromWin32(error, Problem::LocalIoError), WindowsErrorText(error));
    }
    CHandleGuard guard(file);

    CTransferSide source; // read from the file itself: the size of a selection root is not known to the queue
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(file, &info))
        return Failure(Problem::LocalIoError, WindowsErrorText(GetLastError()));
    source.exists = true;
    source.size = SizeOf(info.nFileSizeHigh, info.nFileSizeLow);
    source.mtime = UnixTimeOf(info.ftLastWriteTime);

    CTransferSide existing;
    CItemResult failure;
    if (!StatRemoteSide(target, existing, failure))
        return failure;

    // What follows a finished upload: the time and permissions of the target, and for a move the deletion of the source.
    auto finishUpload = [&]() -> CItemResult
    {
        if (settings.preserveTime && source.mtime > 0)
            session_.SetTimes(target, source.mtime, source.mtime); // best effort, like the local side
        if (settings.setUploadPermissions)
            session_.Chmod(target, settings.uploadPermissions); // best effort too: the file is there and complete

        if (settings.move && !item.viaLink)
        {
            guard.Close(); // the open source file blocks its own deletion
            DWORD error = 0;
            if (!DeleteLocalFileForce(ExtendedLocalPath(item.localPath), error))
                return Failure(Problem::DeleteFailed, WindowsErrorText(error));
        }
        return Done();
    };

    // An earlier attempt sent the final rename and never saw the answer (a pause or a dropped connection at that very
    // moment), and the server had done it: the target is our finished file and the .filepart is gone. A rename that was
    // not carried out leaves the .filepart behind, so a missing .filepart with a full-size target can only be ours. A
    // pending answer of the user (Overwrite, Rename ...) is never second-guessed.
    if (existing.exists && !existing.isDir && settings.safeUpload && item.renameSent &&
        item.forceAction == SolveAction::Retry && existing.size == source.size)
    {
        CTransferSide leftover;
        if (!StatRemoteSide(SafeUploadName(target), leftover, failure))
            return failure;
        if (!leftover.exists)
            return finishUpload();
    }

    uint64_t resumeOffset = 0; // from the conflict decision: the target itself is a partial copy
    if (existing.exists)
    {
        const CConflictDecision decision = Decide(item, source, existing, userNamed, !settings.safeUpload);
        if (decision.action == ConflictAction::Skip)
            return Skipped();
        if (decision.action == ConflictAction::Park)
            return ParkConflict(item, decision, existing, codec_.Decode(target));
        if (decision.action == ConflictAction::Rename)
        {
            bool statFailed = false;
            target = FreeRemotePath(target, statFailed);
            if (statFailed)
                return FromSessionError(Problem::Other);
            if (target.empty())
                return Failure(Problem::TargetExists, std::wstring());
            existing = CTransferSide();
        }
        else
        {
            resumeOffset = decision.resumeOffset;
        }
    }
    if (target != item.remotePath)
        queue.Retarget(item.uid, std::wstring(), target);

    // Where the bytes go and where they start.
    std::string writeTo = target;
    uint64_t startAt = resumeOffset;
    bool viaPartial = false;
    if (settings.safeUpload)
    {
        const std::string partialPath = SafeUploadName(target);
        uint64_t partialSize = 0;
        if (!PartialToResume(partialPath, source.size, item.ownsPartial || WantsResume(item), partialSize, failure))
            return failure;
        if (partialSize > 0 || resumeOffset == 0)
        {
            writeTo = partialPath; // an existing partial (partialSize) or a new one
            startAt = partialSize;
            viaPartial = true;
        }
    }

    queue.SetOwnsPartial(item.uid);
    // Without safe upload the target itself is our partial file: use up the user's answer (Overwrite) so a retry after
    // a pause continues it (Decide). With safe upload the real target still exists, so the answer must stay; the own
    // .filepart is continued through ownsPartial.
    if (!settings.safeUpload)
        queue.Retarget(item.uid, std::wstring(), target);
    BeginTransfer(item.uid, startAt);
    uint64_t transferred = 0;
    bool ok = session_.Upload(file, writeTo, startAt, startAt > 0, &transferred);
    if (!ok && startAt == 0 && session_.LastError().kind == SshErrorKind::NotFound &&
        EnsureRemoteDir(ParentOfRemote(writeTo), 0))
    {
        // the MakeDirRemote item of the parent may not have run yet
        ok = session_.Upload(file, writeTo, 0, false, &transferred);
    }
    EndTransfer(transferred);
    if (!ok)
        return FromSessionError(Problem::TransferFailed); // the partial stays: the next attempt can continue it

    // Without overwrite the rename refuses a target that appeared meanwhile (kind Exists) instead of replacing it.
    if (viaPartial)
    {
        queue.SetRenameSent(item.uid); // if the answer is lost, the next attempt looks at what the server did
        if (!session_.Rename(writeTo, target, existing.exists))
            return FromSessionError(Problem::TransferFailed);
    }

    return finishUpload();
}

// ---- directories and deletes ----------------------------------------------------------------------------------

CWorker::CItemResult CWorker::MakeDirLocal(const CQueueItem& item)
{
    if (item.localPath.empty())
        return Parked(Problem::InvalidLocalName);
    if (EnsureDirectory(item.localPath))
        return Done();
    DWORD error = GetLastError();
    if (error == ERROR_SUCCESS)
        error = ERROR_ALREADY_EXISTS; // exists, but is a file
    return Failure(Problem::DirCreateFailed, WindowsErrorText(error));
}

bool CWorker::MkdirOrExists(const std::string& path)
{
    if (session_.Mkdir(path))
        return true;
    if (session_.LastError().kind != SshErrorKind::Exists)
        return false;
    // Exists is fine when it is a directory (a merge, or another worker was faster); a file keeps the Exists error
    CSftpEntry entry;
    return session_.Stat(path, entry) && entry.isDir;
}

// Like "mkdir -p": a missing parent is created first.
bool CWorker::EnsureRemoteDir(const std::string& path, int depth)
{
    if (path.empty() || path == "/")
        return true;
    if (MkdirOrExists(path))
        return true;
    if (session_.LastError().kind != SshErrorKind::NotFound || depth >= kMaxExploreDepth)
        return false;
    const std::string parent = ParentOfRemote(path);
    return parent != path && EnsureRemoteDir(parent, depth + 1) && MkdirOrExists(path);
}

CWorker::CItemResult CWorker::MakeDirRemote(const CQueueItem& item)
{
    if (item.remotePath.empty())
        return Failure(Problem::InvalidRemoteName, std::wstring());
    if (EnsureRemoteDir(item.remotePath, 0))
        return Done();
    return FromSessionError(Problem::DirCreateFailed);
}

// A file or a link is removed with Remove, a real directory with Rmdir. Already gone counts as removed.
bool CWorker::RemoveRemote(const std::string& path, bool isDir)
{
    bool ok = false;
    if (!isDir)
    {
        ok = session_.Remove(path);
    }
    else
    {
        CSftpEntry entry;
        const bool isLink = session_.LStat(path, entry) && entry.isLink;
        ok = isLink ? session_.Remove(path) : session_.Rmdir(path);
    }
    return ok || session_.LastError().kind == SshErrorKind::NotFound;
}

CWorker::CItemResult CWorker::DeleteRemote(const CQueueItem& item, bool isDir)
{
    if (RemoveRemote(item.remotePath, isDir))
        return Done();
    return FromSessionError(Problem::DeleteFailed);
}

// Reads the current mode (a link is followed, like chmod does) and changes it. Nothing is sent when the mode would stay
// the same, so a tree that is already right causes no writes (and no errors on items we may not change).
CWorker::CItemResult CWorker::SetAttrs(const CQueueItem& item)
{
    CSftpEntry current;
    if (!session_.Stat(item.remotePath, current))
    {
        if (session_.LastError().kind == SshErrorKind::NotFound)
            return Done(); // already gone
        return FromSessionError(Problem::ChmodFailed);
    }
    CItemMode old;
    old.mode = PermissionBitsOf(current.permissions);
    old.known = current.hasPermissions;
    uint32_t newMode = 0;
    // an unknown mode is never computed from 0: only a change that decides every bit can be applied to it
    if (!ApplyToItemMode(old, host_.Settings().modeChange, newMode))
        return Failure(Problem::PermissionsUnknown, std::wstring());
    if (old.known && newMode == old.mode)
        return Done();
    if (session_.Chmod(item.remotePath, newMode))
        return Done();
    return FromSessionError(Problem::ChmodFailed);
}

CWorker::CItemResult CWorker::DeleteLocal(const CQueueItem& item, bool isDir)
{
    DWORD error = 0;
    bool ok = false;
    const std::wstring path = ExtendedLocalPath(item.localPath);
    if (isDir)
    {
        ok = RemoveDirectoryW(path.c_str()) != FALSE;
        if (!ok)
            error = GetLastError();
    }
    else
    {
        ok = DeleteLocalFileForce(path, error);
    }
    if (ok || error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return Done();
    return Failure(Problem::DeleteFailed, WindowsErrorText(error));
}
