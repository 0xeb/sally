#include "operation.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace
{
constexpr int kMinWorkers = 1;
constexpr int kMaxWorkers = 8;
constexpr long long kNotifyIntervalMs = 100; // OnProgress at most this often
constexpr long long kSpeedWindowMs = 3000;

long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::wstring LastLocalName(const std::wstring& path)
{
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/'))
        --end;
    const size_t slash = path.find_last_of(L"\\/", end == 0 ? 0 : end - 1);
    const size_t begin = slash == std::wstring::npos ? 0 : slash + 1;
    return path.substr(begin, end - begin);
}
} // namespace

COperationSettings SettingsFromSite(const CSite& site, TransferDirection direction, bool move)
{
    COperationSettings settings;
    settings.direction = direction;
    settings.move = move;
    settings.preserveTime = site.preserveTime;
    settings.safeUpload = site.safeUpload;
    settings.setUploadPermissions = site.setUploadPermissions;
    settings.uploadPermissions = site.uploadPermissions;
    settings.workers = site.workers;
    return settings;
}

// ---- roots ----------------------------------------------------------------------------------------------------

std::vector<CQueueItem> COperation::BuildDownloadRoots(const std::vector<CRemoteSource>& sources,
                                                       const COperationSettings& settings, const CNameCodec& codec)
{
    std::vector<CQueueItem> items;
    for (const CRemoteSource& source : sources)
    {
        std::string path = source.path;
        while (path.size() > 1 && path.back() == '/')
            path.pop_back();
        AddDownloadItems(items, path, LastRemoteName(path), source.isDir, source.size, source.mtime, settings.localRoot,
                         codec, 0, source.targetName);
    }
    return items;
}

std::vector<CQueueItem> COperation::BuildDeleteRoots(const std::vector<CRemoteSource>& sources)
{
    std::vector<CQueueItem> items;
    for (const CRemoteSource& source : sources)
    {
        std::string path = source.path;
        while (path.size() > 1 && path.back() == '/')
            path.pop_back();
        CQueueItem item;
        item.remotePath = path;
        // A directory is explored first (its content is deleted, then the directory itself); a file or a link is deleted
        // at once. The FS passes a link with isDir == false; if a caller passes a link as a directory anyway, the worker
        // sees that it is a link and removes only the link.
        item.type = source.isDir ? ItemType::ExploreRemote : ItemType::DeleteRemoteFile;
        items.push_back(std::move(item));
    }
    return items;
}

std::vector<CQueueItem> COperation::BuildChmodRoots(const std::vector<CRemoteSource>& sources,
                                                    const COperationSettings& settings)
{
    std::vector<CQueueItem> items;
    for (const CRemoteSource& source : sources)
    {
        const bool explore = settings.chmodRecursive && source.isDir;
        if (!explore && settings.chmodRecursive && !settings.chmodFiles)
            continue; // a recursive change of directories only: a selected file is not one
        std::string path = source.path;
        while (path.size() > 1 && path.back() == '/')
            path.pop_back();
        CQueueItem item;
        item.remotePath = path;
        item.type = explore ? ItemType::ExploreRemote : ItemType::SetAttrs;
        items.push_back(std::move(item));
    }
    return items;
}

std::vector<CQueueItem> COperation::BuildUploadRoots(const std::vector<CLocalSource>& sources,
                                                     const COperationSettings& settings, const CNameCodec& codec)
{
    std::vector<CQueueItem> items;
    for (const CLocalSource& source : sources)
        AddUploadItems(items, source.path, LastLocalName(source.path), source.isDir, source.size, source.mtime,
                       settings.remoteRoot, codec, 0);
    return items;
}

// ---- life -----------------------------------------------------------------------------------------------------

COperation::COperation(COperationSettings settings, CPanelConnectParams params, std::vector<CQueueItem> roots)
    : settings_(std::move(settings)), params_(std::move(params))
{
    settings_.workers = (std::min)((std::max)(settings_.workers, kMinWorkers), kMaxWorkers);
    queue_.AddRootItems(std::move(roots));
}

COperation::~COperation()
{
    RequestCancel();
    std::vector<std::unique_ptr<CWorker>> workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        workers.swap(workers_);
    }
    workers.clear(); // ~CWorker joins the thread; no lock is held, the threads may still need mutex_ to finish
}

bool COperation::Start()
{
    std::vector<CWorker*> toStart;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_ || !params_.valid)
            return false;
        started_ = true;
        for (int i = 0; i < settings_.workers; ++i)
        {
            workers_.push_back(std::unique_ptr<CWorker>(new CWorker(*this, i))); // not make_unique: it cannot see the private base
            toStart.push_back(workers_.back().get());
        }
    }
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        samples_.push_back({NowMs(), 0});
        lastSampleMs_ = samples_.back().ms;
    }

    bool failed = false;
    for (CWorker* worker : toStart)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++activeWorkers_;
        }
        try
        {
            worker->Start();
        }
        catch (...)
        {
            ReleaseActive(); // this worker has no thread
            failed = true;
            break;
        }
    }
    if (failed)
        RequestCancel(); // the workers that did start must not run half an operation
    ReleaseActive();     // Start's own count: from here on the last exiting worker finishes the operation
    return !failed;
}

// Tells every worker's session to stop its running step (workers are only added by Start, so the list is stable).
void COperation::InterruptWorkers()
{
    std::vector<CWorker*> workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::unique_ptr<CWorker>& worker : workers_)
            workers.push_back(worker.get());
    }
    for (CWorker* worker : workers)
        worker->Cancel();
}

void COperation::RequestCancel()
{
    if (cancelRequested_.exchange(true))
        return;
    InterruptWorkers();
    // Everything that waits looks at the flag under its own mutex: taking the mutex once makes sure it cannot miss the
    // notification (like Monitor.Pulse in .NET, the waiter must not be between "check" and "wait").
    { std::lock_guard<std::mutex> lock(waitMutex_); }
    waitCv_.notify_all();
    { std::lock_guard<std::mutex> lock(interMutex_); }
    interCv_.notify_all();
    queue_.WakeAll();
    NotifyObserverState();
}

void COperation::Pause()
{
    if (cancelRequested_.load())
        return;
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        if (paused_.exchange(true))
            return;
    }
    waitCv_.notify_all();
    InterruptWorkers(); // a transfer in a stalled network wait must not wait for its idle timeout
    NotifyObserverState();
}

void COperation::PauseForLoginProblem(const std::wstring& text, const CSshError& error)
{
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        pauseReason_ = text.empty() ? std::wstring(1, L'?') : text; // non-empty: that is what tells it from a user pause
        pauseError_ = error;
    }
    Pause();
}

std::wstring COperation::PauseReason() const
{
    std::lock_guard<std::mutex> lock(waitMutex_);
    return pauseReason_;
}

CSshError COperation::PauseError() const
{
    std::lock_guard<std::mutex> lock(waitMutex_);
    return pauseError_;
}

void COperation::Resume()
{
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        if (!paused_.exchange(false))
            return;
        pauseReason_.clear();
        pauseError_ = CSshError();
    }
    waitCv_.notify_all();
    queue_.WakeAll();
    NotifyObserverState();
}

void COperation::WaitWhilePaused()
{
    std::unique_lock<std::mutex> lock(waitMutex_);
    waitCv_.wait(lock, [this] { return !paused_.load() || cancelRequested_.load(); });
}

bool COperation::SleepUnlessInterrupted(unsigned ms)
{
    std::unique_lock<std::mutex> lock(waitMutex_);
    const bool interrupted = waitCv_.wait_for(lock, std::chrono::milliseconds(ms),
                                              [this] { return paused_.load() || cancelRequested_.load(); });
    return !interrupted;
}

int COperation::Solve(uint64_t uid, SolveAction action, const std::wstring& newName, bool applyToAll)
{
    const int changed = queue_.Solve(uid, action, newName, applyToAll);
    if (changed > 0)
    {
        NotifyStateIfChanged();
        FinishRun(false); // a Skip can be the last open item
    }
    return changed;
}

int COperation::RetryAllFailed()
{
    const int changed = queue_.RetryAllFailed();
    if (changed > 0)
        NotifyStateIfChanged();
    return changed;
}

bool COperation::WaitFinished(DWORD timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeoutMs == INFINITE)
    {
        finishedCv_.wait(lock, [this] { return IsFinishedLocked(); });
        return true;
    }
    return finishedCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return IsFinishedLocked(); });
}

bool COperation::IsFinished() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return IsFinishedLocked();
}

bool COperation::IsWaitingForSlot() const
{
    return workersWaitingForSlot_.load() > 0 && workersHoldingSlot_.load() == 0 && !cancelRequested_.load() &&
           !paused_.load();
}

bool COperation::IsDisposable() const
{
    if (!IsFinished())
        return false;
    if (WasCancelled())
        return true;
    const OperationState state = State();
    return state == OperationState::Finished || state == OperationState::FinishedWithSkips;
}

// mutex_ held. The queue is called under it (queue is a leaf below the operation in the lock order).
bool COperation::IsFinishedLocked() const
{
    if (workersGone_)
        return true;
    return queue_.GetOperationState() != OperationState::Running && notifiedRun_ == queue_.RunId();
}

// Called by every worker thread as its last act, and once by Start. The last one ends the operation.
void COperation::ReleaseActive()
{
    bool last = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last = --activeWorkers_ == 0;
    }
    if (!last)
        return;
    NotifyStateIfChanged();
    FinishRun(true);
}

// Fires OnFinished once per run when the work has run out (or the workers are gone), then wakes the waiters.
void COperation::FinishRun(bool workersGone)
{
    bool fire = false;
    uint64_t run = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (workersGone)
            workersGone_ = true;
        run = queue_.RunId();
        const bool over = workersGone || queue_.GetOperationState() != OperationState::Running;
        if (over && claimedRun_ != run)
        {
            claimedRun_ = run;
            fire = true;
        }
    }
    if (fire)
    {
        try
        {
            if (observer_ != nullptr)
                observer_->OnFinished();
        }
        catch (...)
        {
        }
        std::lock_guard<std::mutex> lock(mutex_);
        notifiedRun_ = run; // after OnFinished: whoever waits sees an operation whose observer was told
    }
    if (fire || workersGone)
        finishedCv_.notify_all();
}

// ---- IWorkerHost ----------------------------------------------------------------------------------------------

void COperation::WorkerExited()
{
    ReleaseActive();
}

void COperation::WorkerWaitingForSlot(bool waiting)
{
    workersWaitingForSlot_ += waiting ? 1 : -1;
    NotifyObserverState();
}

void COperation::WorkerHoldsSlot(bool holds)
{
    workersHoldingSlot_ += holds ? 1 : -1;
    NotifyObserverState();
}

void COperation::ItemChanged(uint64_t uid)
{
    try
    {
        if (observer_ != nullptr)
            observer_->OnItemChanged(uid);
    }
    catch (...)
    {
    }
    NotifyStateIfChanged();
    FinishRun(false);
}

void COperation::NotifyObserverState()
{
    try
    {
        if (observer_ != nullptr)
            observer_->OnStateChanged();
    }
    catch (...)
    {
    }
}

void COperation::NotifyStateIfChanged()
{
    const int now = static_cast<int>(queue_.GetOperationState());
    if (reportedState_.exchange(now) == now)
        return;
    NotifyObserverState();
}

// ---- interactions ---------------------------------------------------------------------------------------------

void COperation::NotifyInteraction()
{
    try
    {
        if (observer_ != nullptr)
            observer_->OnInteractionPending();
    }
    catch (...)
    {
    }
}

// interMutex_ held (the wait releases it). Takes the slot for one interaction; false after a cancel.
bool COperation::BeginInteraction(std::unique_lock<std::mutex>& lock)
{
    interCv_.wait(lock, [this] { return !interBusy_ || cancelRequested_.load(); });
    if (cancelRequested_.load())
        return false;
    interBusy_ = true;
    interHandedOut_ = false;
    interAnswered_ = false;
    return true;
}

// interMutex_ held
void COperation::EndInteraction()
{
    interBusy_ = false;
    inter_ = CInteraction();
    interWorker_ = -1;
    interCv_.notify_all();
}

HostKeyDecision COperation::AskHostKey(int workerIndex, const std::string& host, int port, const std::string& keyType,
                                       const std::string& fingerprint, bool changed)
{
    if (!settings_.interactive || observer_ == nullptr)
        return HostKeyDecision::Reject;

    std::unique_lock<std::mutex> lock(interMutex_);
    if (!BeginInteraction(lock))
        return HostKeyDecision::Reject;
    if (trustedKeys_.count(fingerprint) != 0) // another worker was asked about this key meanwhile
    {
        EndInteraction();
        return HostKeyDecision::AcceptOnce;
    }

    inter_ = CInteraction();
    inter_.kind = CInteraction::Kind::HostKey;
    inter_.host = host;
    inter_.port = port;
    inter_.keyType = keyType;
    inter_.fingerprint = fingerprint;
    inter_.changed = changed;
    inter_.threadId = std::this_thread::get_id();
    interWorker_ = workerIndex;
    lock.unlock();
    NotifyInteraction();
    lock.lock();

    interCv_.wait(lock, [this] { return interAnswered_ || cancelRequested_.load(); });
    const HostKeyDecision decision = interAnswered_ ? hostDecision_ : HostKeyDecision::Reject;
    if (decision != HostKeyDecision::Reject)
        trustedKeys_.insert(fingerprint);
    EndInteraction();
    return decision;
}

bool COperation::AskPrompt(int workerIndex, PromptKind kind, const std::string& name, const std::string& instruction,
                           const std::string& promptText, bool echo, CSecret& answer)
{
    if (!settings_.interactive || observer_ == nullptr)
        return false;

    std::unique_lock<std::mutex> lock(interMutex_);
    if (!BeginInteraction(lock))
        return false;

    inter_ = CInteraction();
    inter_.kind = CInteraction::Kind::Prompt;
    inter_.promptKind = kind;
    inter_.name = name;
    inter_.instruction = instruction;
    inter_.promptText = promptText;
    inter_.echo = echo;
    inter_.threadId = std::this_thread::get_id();
    interWorker_ = workerIndex;
    promptOk_ = false;
    lock.unlock();
    NotifyInteraction();
    lock.lock();

    interCv_.wait(lock, [this] { return interAnswered_ || cancelRequested_.load(); });
    const bool ok = interAnswered_ && promptOk_;
    if (ok)
        answer = std::move(promptAnswer_);
    promptAnswer_ = CSecret();
    EndInteraction();
    return ok;
}

bool COperation::TryGetInteraction(CInteraction& out, int* workerIndex)
{
    std::lock_guard<std::mutex> lock(interMutex_);
    if (!interBusy_ || inter_.kind == CInteraction::Kind::None || interHandedOut_ || interAnswered_)
        return false;
    out = inter_;
    if (workerIndex != nullptr)
        *workerIndex = interWorker_;
    interHandedOut_ = true;
    return true;
}

bool COperation::AnswerHostKey(HostKeyDecision decision)
{
    {
        std::lock_guard<std::mutex> lock(interMutex_);
        if (!interBusy_ || inter_.kind != CInteraction::Kind::HostKey || interAnswered_)
            return false;
        hostDecision_ = decision;
        interAnswered_ = true;
    }
    interCv_.notify_all();
    return true;
}

bool COperation::AnswerPrompt(bool ok, CSecret answer)
{
    {
        std::lock_guard<std::mutex> lock(interMutex_);
        if (!interBusy_ || inter_.kind != CInteraction::Kind::Prompt || interAnswered_)
            return false;
        promptOk_ = ok;
        promptAnswer_ = std::move(answer);
        interAnswered_ = true;
    }
    interCv_.notify_all();
    return true;
}

void COperation::AddTransferred(uint64_t bytes)
{
    const long long now = NowMs();
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        transferred_ += bytes;
        if (now - lastSampleMs_ >= kNotifyIntervalMs)
        {
            samples_.push_back({now, transferred_});
            lastSampleMs_ = now;
        }
        while (samples_.size() >= 2 && samples_[1].ms <= now - kSpeedWindowMs)
            samples_.pop_front(); // keeps one sample at or before the start of the window as the baseline
        if (now - lastNotifyMs_ >= kNotifyIntervalMs)
        {
            lastNotifyMs_ = now;
            notify = true;
        }
    }
    if (notify)
        NotifyProgress();
}

void COperation::NotifyProgress()
{
    try
    {
        if (observer_ != nullptr)
            observer_->OnProgress();
    }
    catch (...)
    {
    }
}

COperationStats COperation::Statistics() const
{
    const CQueueSummary summary = queue_.Summary(); // before the stats lock: the queue lock is taken inside
    COperationStats stats;
    stats.bytesTotal = summary.totals.bytesTotal;
    stats.bytesDone = summary.totals.bytesDone;
    stats.filesTotal = summary.totals.filesTotal;
    stats.filesDone = summary.totals.filesDone;
    // Delete and chmod have no bytes: the items are what counts
    if (settings_.direction == TransferDirection::Delete || settings_.direction == TransferDirection::Chmod)
    {
        stats.filesTotal = summary.totals.metaTotal;
        stats.filesDone = summary.totals.metaDone;
    }

    const long long now = NowMs();
    std::lock_guard<std::mutex> lock(statsMutex_);
    while (samples_.size() >= 2 && samples_[1].ms <= now - kSpeedWindowMs)
        samples_.pop_front();
    if (!samples_.empty())
    {
        const CSample& baseline = samples_.front();
        const long long span = now - baseline.ms;
        if (span > 0)
            stats.bytesPerSecond = static_cast<double>(transferred_ - baseline.bytes) * 1000.0 / static_cast<double>(span);
    }
    if (stats.bytesPerSecond > 0)
    {
        const uint64_t left = stats.bytesTotal > stats.bytesDone ? stats.bytesTotal - stats.bytesDone : 0;
        stats.etaSeconds = static_cast<int64_t>(static_cast<double>(left) / stats.bytesPerSecond);
    }
    return stats;
}
