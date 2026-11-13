// One worker of an operation: a thread with its own SSH connection that takes items from the queue and does them.
//
// C++/Win32 notes for a .NET reader:
//  - std::thread is like System.Threading.Thread, but it MUST be joined before it is destroyed (else the program
//    terminates). ~CWorker joins.
//  - An exception that leaves a thread function ends the whole process, so ThreadMain catches everything.
//
// Threading: the CSshSession is used by the worker thread only (it is created before the thread starts and destroyed
// after it was joined, when it is closed). Cancel() may be called from any thread. The queue is shared (it has its own
// lock); the worker holds no lock while it calls libssh2, the file system or the host.
#pragma once

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "conflict.h"
#include "explored_dirs.h"
#include "op_queue.h"
#include "op_settings.h"
#include "panel_session.h"
#include "session_log.h"
#include "ssh_session.h"

constexpr int kMaxExploreDepth = 64; // deeper than this is taken for a symlink loop

// What a worker needs from its operation. Implemented by COperation; every method is thread safe and cheap.
class IWorkerHost
{
public:
    virtual ~IWorkerHost() = default;
    virtual CQueue& Queue() = 0;
    virtual const COperationSettings& Settings() const = 0;
    virtual const CPanelConnectParams& ConnectParams() const = 0;
    virtual bool IsCancelRequested() const = 0;
    virtual uint64_t OperationNumber() const = 0;   // the number in the operations list (0 = not listed); names the worker logs
    virtual void ItemChanged(uint64_t uid) = 0;      // an item changed state
    virtual void AddTransferred(uint64_t bytes) = 0; // statistics, throttled notification of the observer
    virtual void WorkerExited() = 0;                 // last call of a worker thread

    // Pause: transfers stop (the progress callback says "cancel"), items go back to Waiting, workers take nothing new.
    virtual bool IsPaused() const = 0;
    virtual void WaitWhilePaused() = 0;                  // returns when resumed or cancelled
    virtual bool SleepUnlessInterrupted(unsigned ms) = 0; // false when cancelled or paused before the time was up
    // A worker has nothing to do and the queue is not Running: lets the operation report the end of the run.
    virtual void CheckRunOver() = 0;
    virtual CExploredDirs& ExploredDirs() = 0;
    // A login that will fail the same way for every item (rejected host key, wrong password, bad key file, cancelled
    // prompt): pauses the operation and shows 'text'; Resume tries the login again.
    virtual void PauseForLoginProblem(const std::wstring& text, const CSshError& error) = 0;

    // Logins the worker cannot decide alone (a host key other than the pinned one, a prompt). They block until the
    // user answered, the operation was cancelled or the operation cannot ask (then: reject / false).
    virtual HostKeyDecision AskHostKey(int workerIndex, const std::string& host, int port, const std::string& keyType,
                                       const std::string& fingerprint, bool changed) = 0;
    virtual bool AskPrompt(int workerIndex, PromptKind kind, const std::string& name, const std::string& instruction,
                           const std::string& promptText, bool echo, CSecret& answer) = 0;
};

// Remote path helpers (raw bytes, no normalisation: a relative path stays relative to the login directory).
std::string AppendRemoteName(const std::string& dir, const std::string& rawName); // dir + "/" + name
std::string LastRemoteName(const std::string& path);                              // ignores trailing '/'

// Queue items for one source entry. Download: the local name is the decoded raw name (or 'targetName', a rename, when given); when it is not a valid Windows
// name (or the decoding lost data) the items get an EMPTY localPath, which makes the worker park them with
// Problem::InvalidLocalName. A directory gives MakeDirLocal + ExploreRemote. Upload: the remote name is the encoded
// local name; one the site's encoding cannot write gets an EMPTY remotePath (worker: Problem::InvalidRemoteName).
void AddDownloadItems(std::vector<CQueueItem>& out, const std::string& remotePath, const std::string& rawName, bool isDir,
                      uint64_t size, int64_t mtime, const std::wstring& localDir, const CNameCodec& codec, int depth,
                      const std::wstring& targetName = std::wstring());
void AddUploadItems(std::vector<CQueueItem>& out, const std::wstring& localPath, const std::wstring& name, bool isDir,
                    uint64_t size, int64_t mtime, const std::string& remoteDir, const CNameCodec& codec, int depth);

class CWorker : private ISshCallbacks
{
public:
    CWorker(IWorkerHost& host, int index);
    ~CWorker(); // cancels nothing: the operation cancels first, then destroys the workers (which join)
    CWorker(const CWorker&) = delete;
    CWorker& operator=(const CWorker&) = delete;

    void Start(); // throws std::system_error when the thread cannot be created
    void Cancel(); // any thread: interrupts the running network step
    void Join();

private:
    // What happened to an item. The thread turns it into a queue call.
    struct CItemResult
    {
        enum class Kind
        {
            Done,
            Failed,
            Parked,          // UserInputNeeded
            Skipped,         // the conflict decision said: leave the target alone
            ReturnToWaiting, // cancelled or paused
            Replaced         // an explore item: the queue already replaced it with its children
        };
        Kind kind = Kind::Done;
        Problem problem = Problem::None;
        std::wstring text;
        CSshError sshError; // the SSH error behind text, when there is one (shown in the user's language by the UI)
        bool connectionLost = false; // the session is unusable or could not be made: reconnect (with backoff), try again
    };
    static CItemResult Done() { return CItemResult(); }
    static CItemResult Failure(Problem problem, std::wstring text);
    static CItemResult FailureOfError(Problem problem, const CSshError& error); // the English message is the text
    static CItemResult Parked(Problem problem, std::wstring text = std::wstring());
    static CItemResult Skipped();
    static CItemResult Returned();
    static CItemResult Replaced();

    // ISshCallbacks (called on the worker thread)
    HostKeyDecision VerifyHostKey(const std::string& host, int port, const std::string& keyType,
                                  const std::string& fingerprintSha256, bool changed) override;
    bool Prompt(PromptKind kind, const std::string& name, const std::string& instruction, const std::string& promptText,
                bool echo, CSecret& answer) override;
    bool Progress(uint64_t done, uint64_t total) override;

    void ThreadMain();
    void Run();
    void WaitForNextChance();
    void ProcessItem(const CQueueItem& item);
    void Report(const CQueueItem& item, const CItemResult& result);
    CItemResult RunItem(const CQueueItem& item);
    CItemResult ExecuteOnce(const CQueueItem& item);
    static bool NeedsConnection(ItemType type);

    // connection
    bool EnsureConnected(CItemResult& failure);
    static bool IsLoginProblem(SshErrorKind kind);
    void StopAfterLoginProblem(CItemResult& failure);
    CItemResult FromSessionError(Problem fallback);

    // items
    CItemResult ExploreRemote(const CQueueItem& item);
    CItemResult ExploreForDelete(const CQueueItem& item);
    CItemResult ExploreForChmod(const CQueueItem& item);
    CItemResult ExploreLocal(const CQueueItem& item);
    CItemResult Download(const CQueueItem& item);
    CItemResult Upload(const CQueueItem& item);
    CItemResult MakeDirLocal(const CQueueItem& item);
    CItemResult MakeDirRemote(const CQueueItem& item);
    CItemResult DeleteRemote(const CQueueItem& item, bool isDir);
    CItemResult SetAttrs(const CQueueItem& item);
    CItemResult DeleteLocal(const CQueueItem& item, bool isDir);
    CItemResult ReplaceWithChildren(const CQueueItem& item, std::vector<CQueueItem> children, ItemType afterType,
                                    bool childrenViaLink);
    bool IsRemoteLink(const std::string& path);

    // conflicts (a transfer item: where does it go, and what to do when the target exists)
    bool ResolveLocalTarget(const CQueueItem& item, std::wstring& target, bool& userNamed, CItemResult& stop);
    bool ResolveRemoteTarget(const CQueueItem& item, std::string& target, bool& userNamed, CItemResult& stop);
    CConflictDecision Decide(const CQueueItem& item, const CTransferSide& source, const CTransferSide& target,
                             bool userNamed, bool resumeOwnTarget) const;
    CItemResult ParkConflict(const CQueueItem& item, const CConflictDecision& decision, const CTransferSide& target,
                             const std::wstring& shownPath);
    bool StatRemoteSide(const std::string& path, CTransferSide& side, CItemResult& failure);
    std::string FreeRemotePath(const std::string& target, bool& statFailed);
    bool PartialToResume(const std::string& partialPath, uint64_t sourceSize, bool trusted, uint64_t& size,
                         CItemResult& failure);
    bool WantsResume(const CQueueItem& item) const;

    // helpers
    HANDLE OpenLocalTarget(const std::wstring& path, bool truncate, DWORD& error);
    bool MkdirOrExists(const std::string& path);
    bool EnsureRemoteDir(const std::string& path, int depth);
    bool RemoveRemote(const std::string& path, bool isDir);
    void BeginTransfer(uint64_t uid, uint64_t startOffset);
    void EndTransfer(uint64_t bytesTransferred);

    IWorkerHost& host_;
    const int index_;
    std::thread thread_;
    CKnownHosts hosts_; // this worker's view of the known_hosts files (before session_: it uses it)
    CSessionLogSink logSink_; // before session_: the session writes to it until it is destroyed
    CSshSession session_;
    CNameCodec codec_;
    bool unreachable_ = false; // the last item ended with "connection lost": no more backoff until a connect works
    // A login problem in a non-interactive operation (nobody to pause for): every later item fails the same way at once.
    bool loginFailed_ = false;
    CItemResult loginFailure_;

    // the running transfer (worker thread only)
    uint64_t transferUid_ = 0;
    uint64_t transferStart_ = 0; // the start offset of the running transfer
    uint64_t reported_ = 0; // position already reported to the host
};
