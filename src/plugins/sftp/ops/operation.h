// One copy/move run: the queue, the worker threads and the statistics. Sally-free; the UI layer watches it through an
// IOperationObserver and reads State()/Summary()/Statistics().
//
// Life of an operation:  construct (roots go into the queue) -> SetObserver -> Start -> workers run -> the queue has no
// more work (OperationState != Running) -> OnFinished (once per run) -> RequestCancel (or the destructor) ends the
// workers.
//
// Runs. The workers stay alive (idle, without a connection) when the work has run out, so the user can solve parked
// items or retry failed ones afterwards: they become Waiting again, the idle workers wake up and the operation is
// Running again (a new "run"). OnFinished fires once per run, WaitFinished/IsFinished describe the current run. After
// RequestCancel the operation is finished for good. Use Solve/RetryAllFailed below (or CQueue's own, the result is the
// same: the observer just hears about the state change a little later).
//
// Pause. Pause() makes every transfer stop at its next progress callback (like a cancel: the session is closed, the
// item goes back to Waiting and keeps the size it has), and the workers take no new item until Resume(). The operation
// state stays Running and IsPaused() is true. Resume() wakes the workers; they reconnect and continue the partial files.
//
// Interactions. A worker that meets an unknown host key or a prompt publishes it (see TryGetInteraction) and waits for
// AnswerHostKey/AnswerPrompt; one interaction at a time. Without an observer, with settings.interactive == false, after
// a cancel, or when the operation is destroyed, the answer is "reject".
//
// The destructor cancels and joins everything, so a COperation can be destroyed at any time, but NOT from inside an
// observer callback (a worker thread would then wait for itself).
//
// Lock order: operations list -> operation -> queue -> worker. COperation's own mutexes are leaves around plain data;
// none is held while the observer, a worker or libssh2 is called (the queue is called under mutex_ in two places: it
// never calls out).
#pragma once

#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "explored_dirs.h"
#include "op_queue.h"
#include "op_settings.h"
#include "op_worker.h"
#include "panel_session.h"

// Told about changes by the worker threads (and by Start/RequestCancel/Pause/Resume on the calling thread).
// Implementations must be cheap and thread safe: typically they post a message to a window and return. Exceptions are
// swallowed. Never destroy or Restart the operation from inside a callback.
class IOperationObserver
{
public:
    virtual ~IOperationObserver() = default;
    virtual void OnItemChanged(uint64_t uid) = 0; // an item changed state (done, failed, parked, explored)
    virtual void OnProgress() = 0;                // bytes moved; called at most every 100 ms
    virtual void OnStateChanged() = 0;            // the operation state, the pause flag or the cancel flag changed
    virtual void OnFinished() = 0;                // once per run: nothing is waiting or running any more (or cancelled)
    // A worker waits for the user: call TryGetInteraction on any thread, show a dialog, answer.
    virtual void OnInteractionPending() = 0;
};

struct COperationStats
{
    uint64_t bytesTotal = 0; // all transfer items found so far (grows while directories are explored)
    uint64_t bytesDone = 0;
    int filesTotal = 0;
    int filesDone = 0;
    double bytesPerSecond = 0;  // average over the last ~3 seconds
    int64_t etaSeconds = -1;    // -1 = unknown (no speed yet); 0 = nothing left
};

// A selected item to download: the full raw remote path and what the listing said about it.
struct CRemoteSource
{
    std::string path;
    bool isDir = false;
    uint64_t size = 0;
    int64_t mtime = 0;
    std::wstring targetName; // the local name to use instead of the remote one (a rename on download); empty: keep
};

// A selected item to upload.
struct CLocalSource
{
    std::wstring path;
    bool isDir = false;
    uint64_t size = 0;
    int64_t mtime = 0; // Unix seconds
};

class COperation : private IWorkerHost
{
public:
    // The roots are the selected items (see BuildDownloadRoots/BuildUploadRoots). 'params' must be valid for Start to
    // succeed: it carries the site, the secrets and the host key fingerprint the workers trust.
    COperation(COperationSettings settings, CPanelConnectParams params, std::vector<CQueueItem> roots);
    ~COperation();
    COperation(const COperation&) = delete;
    COperation& operator=(const COperation&) = delete;

    // Before Start; the observer must outlive the operation (or at least its callbacks).
    void SetObserver(IOperationObserver* observer) { observer_ = observer; }

    // Creates the workers. False (nothing started) when called twice or the connect params are not valid.
    bool Start();
    // Any thread, returns at once. Running network steps are interrupted; unfinished items stay Waiting. Final.
    void RequestCancel();
    // See "Pause" above. No effect after a cancel. Any thread, returns at once.
    void Pause();
    void Resume();
    bool IsPaused() const override { return paused_.load(); }
    // The number in the operations list; the registry sets it before Start. Names the logs of the workers.
    void SetNumber(uint64_t number) { number_.store(number); }
    uint64_t OperationNumber() const override { return number_.load(); }
    // Why the operation paused itself (a login that failed, see IWorkerHost::PauseForLoginProblem); empty for a pause of
    // the user and while running. Cleared by Resume.
    std::wstring PauseReason() const;
    // The SSH error behind PauseReason (kind None when there is none): the UI words it from the string table.
    CSshError PauseError() const;
    // True when the current run is finished (observer told) or the operation was cancelled and the workers ended,
    // within timeoutMs (INFINITE allowed).
    bool WaitFinished(DWORD timeoutMs);

    bool IsFinished() const;
    // Running, but no worker has a connection slot yet and some wait for one (the global cap is used up by other
    // operations). The window says "waiting for a free connection" then.
    bool IsWaitingForSlot() const;
    // Finished with nothing left for the user to do: all done, only skips, or cancelled with the workers gone. A parked
    // or failed item can still be retried, so such an operation is not disposable.
    bool IsDisposable() const;
    bool WasCancelled() const { return cancelRequested_.load(); }
    OperationState State() const { return queue_.GetOperationState(); } // Running also after a cancel
    CQueueSummary Summary() const { return queue_.Summary(); }
    COperationStats Statistics() const;

    // For the operation window: the item list (the queue is thread safe).
    CQueue& Queue() override { return queue_; }
    const COperationSettings& Settings() const override { return settings_; }

    // The user's answer for a parked or failed item (see CQueue::Solve). The workers pick the item up again.
    int Solve(uint64_t uid, SolveAction action, const std::wstring& newName = std::wstring(), bool applyToAll = false);
    int RetryAllFailed();

    // ---- worker interactions. Hands out each interaction once (false when none is waiting or it was handed out
    // already). 'workerIndex' (optional) tells which worker asks. The answer functions return false when no such
    // interaction is waiting (answered already, cancelled, or of the other kind). AcceptAndSave: the worker that
    // asked writes the key into the plugin's known_hosts file (its own libssh2 session is needed for that); either way
    // all workers trust the key for the rest of this operation.
    bool TryGetInteraction(CInteraction& out, int* workerIndex = nullptr);
    bool AnswerHostKey(HostKeyDecision decision);
    bool AnswerPrompt(bool ok, CSecret answer);

    // Root items for a selection. Names that cannot be used (see AddDownloadItems) are parked by the worker.
    static std::vector<CQueueItem> BuildDownloadRoots(const std::vector<CRemoteSource>& sources,
                                                      const COperationSettings& settings, const CNameCodec& codec);
    // Delete: a file or link becomes DeleteRemoteFile, a directory ExploreRemote (the worker deletes in explore mode when
    // settings.direction is Delete). source.path is the full raw remote path; the other fields are not used.
    static std::vector<CQueueItem> BuildDeleteRoots(const std::vector<CRemoteSource>& sources);
    // Chmod: with settings.chmodRecursive a directory becomes ExploreRemote and a file SetAttrs (only when
    // settings.chmodFiles); without it every source (a link included) becomes SetAttrs. source.path is the full raw
    // remote path; isDir tells a real directory from a file or link.
    static std::vector<CQueueItem> BuildChmodRoots(const std::vector<CRemoteSource>& sources,
                                                   const COperationSettings& settings);
    static std::vector<CQueueItem> BuildUploadRoots(const std::vector<CLocalSource>& sources,
                                                    const COperationSettings& settings, const CNameCodec& codec);

private:
    // IWorkerHost (called on the worker threads)
    const CPanelConnectParams& ConnectParams() const override { return params_; }
    bool IsCancelRequested() const override { return cancelRequested_.load(); }
    void ItemChanged(uint64_t uid) override;
    void AddTransferred(uint64_t bytes) override;
    void WorkerExited() override;
    void WaitWhilePaused() override;
    bool SleepUnlessInterrupted(unsigned ms) override;
    void CheckRunOver() override { FinishRun(false); }
    CExploredDirs& ExploredDirs() override { return explored_; }
    void PauseForLoginProblem(const std::wstring& text, const CSshError& error) override;
    void WorkerWaitingForSlot(bool waiting) override;
    void WorkerHoldsSlot(bool holds) override;
    HostKeyDecision AskHostKey(int workerIndex, const std::string& host, int port, const std::string& keyType,
                               const std::string& fingerprint, bool changed) override;
    bool AskPrompt(int workerIndex, PromptKind kind, const std::string& name, const std::string& instruction,
                   const std::string& promptText, bool echo, CSecret& answer) override;

    void NotifyProgress();
    void NotifyStateIfChanged();
    void NotifyObserverState();
    void NotifyInteraction();
    void ReleaseActive();
    void FinishRun(bool workersGone);
    bool IsFinishedLocked() const;
    void InterruptWorkers();
    bool BeginInteraction(std::unique_lock<std::mutex>& lock);
    void EndInteraction();

    COperationSettings settings_;
    const CPanelConnectParams params_;
    CQueue queue_;
    CExploredDirs explored_;
    IOperationObserver* observer_ = nullptr;
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> workersWaitingForSlot_{0};
    std::atomic<int> workersHoldingSlot_{0};

    mutable std::mutex mutex_; // guards workers_, started_, activeWorkers_, workersGone_, claimedRun_, notifiedRun_
    std::condition_variable finishedCv_;
    std::vector<std::unique_ptr<CWorker>> workers_;
    bool started_ = false;
    int activeWorkers_ = 1; // 1 = Start itself, until every worker has been started
    bool workersGone_ = false;
    std::atomic<uint64_t> number_{0};
    // OnFinished bookkeeping (queue run ids, see CQueue::RunId): the run whose OnFinished was claimed / completed
    uint64_t claimedRun_ = UINT64_MAX;
    uint64_t notifiedRun_ = UINT64_MAX;

    // pause and backoff waits: a leaf lock around the flags, so a change cannot be missed
    mutable std::mutex waitMutex_;
    std::condition_variable waitCv_;
    std::wstring pauseReason_; // guarded by waitMutex_
    CSshError pauseError_;     // guarded by waitMutex_

    // the one interaction at a time: a leaf lock
    std::mutex interMutex_;
    std::condition_variable interCv_;
    bool interBusy_ = false; // a worker owns the interaction slot
    CInteraction inter_;
    int interWorker_ = -1;
    bool interHandedOut_ = false;
    bool interAnswered_ = false;
    HostKeyDecision hostDecision_ = HostKeyDecision::Reject;
    bool promptOk_ = false;
    CSecret promptAnswer_;
    std::set<std::string> trustedKeys_; // fingerprints the user accepted during this operation

    // statistics: a leaf lock
    struct CSample
    {
        long long ms; // steady clock
        uint64_t bytes;
    };
    mutable std::mutex statsMutex_;
    uint64_t transferred_ = 0; // bytes moved by all workers so far (retries count again)
    mutable std::deque<CSample> samples_;
    long long lastNotifyMs_ = 0;
    long long lastSampleMs_ = 0;
    std::atomic<int> reportedState_{static_cast<int>(OperationState::Running)};
};
