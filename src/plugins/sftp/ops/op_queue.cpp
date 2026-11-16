#include "op_queue.h"

#include <chrono>
#include <utility>

namespace
{
bool IsSolvable(ItemState state)
{
    return state == ItemState::Failed || state == ItemState::UserInputNeeded;
}

bool IsDelayedKind(ItemState state)
{
    return state == ItemState::Delayed || state == ItemState::ForcedToFail;
}
} // namespace

// ---- helpers (mutex_ held) ------------------------------------------------------------------------------------

CQueue::Node* CQueue::Find(uint64_t uid)
{
    auto it = items_.find(uid);
    return it == items_.end() ? nullptr : &it->second;
}

CQueueCounters& CQueue::KidsOf(uint64_t parentUid)
{
    if (parentUid == 0)
        return rootKids_;
    return items_.find(parentUid)->second.kids; // a parent always exists: items are never removed
}

void CQueue::Count(CQueueCounters& counters, ItemState state, int delta)
{
    switch (state)
    {
    case ItemState::Waiting:
    case ItemState::Processing:
    case ItemState::Delayed:
        counters.notDone += delta;
        break;
    case ItemState::Skipped:
        counters.skipped += delta;
        break;
    case ItemState::Failed:
    case ItemState::ForcedToFail:
        counters.failed += delta;
        break;
    case ItemState::UserInputNeeded:
        counters.userInput += delta;
        break;
    case ItemState::Done:
        break;
    }
}

uint64_t CQueue::Insert(CQueueItem item, uint64_t parentUid)
{
    item.uid = nextUid_++;
    item.parentUid = parentUid;
    item.state = ItemState::Waiting;
    item.problem = Problem::None;
    item.errorText.clear();
    item.sshError = CSshError();
    const uint64_t uid = item.uid;

    Count(KidsOf(parentUid), ItemState::Waiting, +1);
    ++stateCount_[static_cast<int>(ItemState::Waiting)];
    (IsExploreType(item.type) ? waitingExplore_ : waitingOther_).insert(uid);

    Node node;
    node.item = std::move(item);
    items_.emplace(uid, std::move(node));
    NoteRunState();
    wake_.notify_all();
    return uid;
}

void CQueue::NoteRunState()
{
    const bool running = ComputeOperationState() == OperationState::Running;
    if (running && !wasRunning_)
        ++runId_;
    wasRunning_ = running;
}

// The only place where a state changes: keeps the parent's counters, the operation's counts and the waiting sets in step.
void CQueue::SetState(Node& node, ItemState state)
{
    const ItemState old = node.item.state;
    if (old == state)
        return;

    CQueueCounters& siblings = KidsOf(node.item.parentUid);
    Count(siblings, old, -1);
    Count(siblings, state, +1);
    --stateCount_[static_cast<int>(old)];
    ++stateCount_[static_cast<int>(state)];

    std::set<uint64_t>& waiting = IsExploreType(node.item.type) ? waitingExplore_ : waitingOther_;
    if (old == ItemState::Waiting)
        waiting.erase(node.item.uid);
    if (state == ItemState::Waiting)
        waiting.insert(node.item.uid);
    if (old == ItemState::Processing)
        processing_.erase(node.item.uid);
    if (state == ItemState::Processing)
        processing_.insert(node.item.uid);

    node.item.state = state;
    NoteRunState();
    wake_.notify_all(); // a waiting item to take, or the operation may be over
}

ItemState CQueue::StateForDelayedDirectory(const CQueueCounters& kids)
{
    if (kids.notDone > 0)
        return ItemState::Delayed;
    if (kids.skipped > 0 || kids.failed > 0 || kids.userInput > 0)
        return ItemState::ForcedToFail;
    return ItemState::Waiting;
}

// A child of directory 'uid' changed state: bring the directory (and then its parent, and so on) in line.
// Stops at the first directory whose state does not change, or that is not a delayed directory.
void CQueue::Reevaluate(uint64_t uid)
{
    while (uid != 0)
    {
        Node* directory = Find(uid);
        if (directory == nullptr || !IsDelayedKind(directory->item.state))
            return;
        const ItemState wanted = StateForDelayedDirectory(directory->kids);
        if (wanted == directory->item.state)
            return;
        SetState(*directory, wanted);
        uid = directory->item.parentUid;
    }
}

void CQueue::ApplySolve(Node& node, SolveAction action, const std::wstring& newName)
{
    CQueueItem& item = node.item;
    if (action == SolveAction::Skip)
    {
        SetState(node, ItemState::Skipped); // keeps problem and text: they say why it was skipped
    }
    else
    {
        item.forceAction = action;
        item.forceNewName = action == SolveAction::Rename ? newName : std::wstring();
        item.problem = Problem::None;
        item.errorText.clear();
        item.sshError = CSshError();
        SetState(node, ItemState::Waiting);
    }
    Reevaluate(item.parentUid);
}

OperationState CQueue::ComputeOperationState() const
{
    auto count = [this](ItemState s) { return stateCount_[static_cast<int>(s)]; };
    if (count(ItemState::Waiting) + count(ItemState::Processing) + count(ItemState::Delayed) > 0)
        return OperationState::Running;
    if (count(ItemState::UserInputNeeded) > 0)
        return OperationState::NeedsUser;
    if (count(ItemState::Failed) > 0)
        return OperationState::FinishedWithErrors;
    if (count(ItemState::Skipped) > 0)
        return OperationState::FinishedWithSkips;
    return OperationState::Finished;
}

// ---- public ---------------------------------------------------------------------------------------------------

std::vector<uint64_t> CQueue::AddRootItems(std::vector<CQueueItem> items)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint64_t> uids;
    for (CQueueItem& item : items)
        uids.push_back(Insert(std::move(item), 0));
    return uids;
}

bool CQueue::TakeNextWaiting(CQueueItem& item)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<uint64_t>& source = !waitingExplore_.empty() ? waitingExplore_ : waitingOther_;
    if (source.empty())
        return false;
    Node* node = Find(*source.begin());
    SetState(*node, ItemState::Processing); // also removes it from the waiting set
    item = node->item;
    return true;
}

bool CQueue::HasWaiting() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !waitingExplore_.empty() || !waitingOther_.empty();
}

bool CQueue::ReplaceWithChildren(uint64_t uid, std::vector<CQueueItem> children, const CQueueItem* after)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Node* node = Find(uid);
    if (node == nullptr || node->item.state != ItemState::Processing || !IsExploreType(node->item.type))
        return false;

    if (after != nullptr)
    {
        // the explore item turns into the "after" action, delayed until its children are done
        const uint64_t parentUid = node->item.parentUid;
        node->item = *after;
        node->item.uid = uid;
        node->item.parentUid = parentUid;
        node->item.state = ItemState::Processing; // SetState below moves it on (and fixes the counters)
        node->item.problem = Problem::None;
        node->item.errorText.clear();
        node->item.sshError = CSshError();
        node->item.bytesDone = 0;
        SetState(*node, ItemState::Delayed);
        for (CQueueItem& child : children)
            Insert(std::move(child), uid);
        Reevaluate(uid); // no children: Waiting at once
        return true;
    }

    // children first, then Done: the parent never sees a moment with nothing left to do
    for (CQueueItem& child : children)
        Insert(std::move(child), node->item.parentUid);
    SetState(*node, ItemState::Done);
    Reevaluate(node->item.parentUid);
    return true;
}

bool CQueue::Finish(uint64_t uid, ItemState state, Problem problem, const std::wstring& errorText,
                    const CSshError& sshError)
{
    const bool allowed = state == ItemState::Done || state == ItemState::Skipped || state == ItemState::Failed ||
                         state == ItemState::UserInputNeeded;
    std::lock_guard<std::mutex> lock(mutex_);
    Node* node = Find(uid);
    if (!allowed || node == nullptr || node->item.state != ItemState::Processing)
        return false;

    CQueueItem& item = node->item;
    item.problem = problem;
    item.errorText = errorText;
    item.sshError = sshError;
    if (state == ItemState::Done && IsTransferType(item.type))
        item.bytesDone = item.size;
    SetState(*node, state);
    Reevaluate(item.parentUid);
    return true;
}

bool CQueue::SetProgress(uint64_t uid, uint64_t bytesDone)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Node* node = Find(uid);
    if (node == nullptr)
        return false;
    node->item.bytesDone = bytesDone;
    return true;
}

bool CQueue::ReturnToWaiting(uint64_t uid)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Node* node = Find(uid);
    if (node == nullptr || node->item.state != ItemState::Processing)
        return false;
    SetState(*node, ItemState::Waiting); // both are "not done" for the parent: no re-evaluation needed
    return true;
}

bool CQueue::SetOwnsPartial(uint64_t uid)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Node* node = Find(uid);
    if (node == nullptr)
        return false;
    node->item.ownsPartial = true;
    return true;
}

bool CQueue::SetRenameSent(uint64_t uid, bool sent)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Node* node = Find(uid);
    if (node == nullptr)
        return false;
    node->item.renameSent = sent;
    return true;
}

bool CQueue::Retarget(uint64_t uid, const std::wstring& localPath, const std::string& remotePath)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Node* node = Find(uid);
    if (node == nullptr)
        return false;
    if (!localPath.empty())
        node->item.localPath = localPath;
    if (!remotePath.empty())
    {
        if (node->item.remotePath != remotePath)
            node->item.renameSent = false; // a rename that was sent belongs to the old name
        node->item.remotePath = remotePath;
    }
    node->item.forceAction = SolveAction::Retry;
    node->item.forceNewName.clear();
    return true;
}

bool CQueue::SetTargetInfo(uint64_t uid, uint64_t size, int64_t mtime)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Node* node = Find(uid);
    if (node == nullptr)
        return false;
    node->item.targetSize = size;
    node->item.targetMtime = mtime;
    return true;
}

int CQueue::Solve(uint64_t uid, SolveAction action, const std::wstring& newName, bool applyToAll)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Node* first = Find(uid);
    if (first == nullptr || !IsSolvable(first->item.state))
        return 0;

    if (!applyToAll || action == SolveAction::Rename)
    {
        ApplySolve(*first, action, newName);
        return 1;
    }

    const Problem problem = first->item.problem;
    int changed = 0;
    for (auto& entry : items_)
    {
        Node& node = entry.second;
        if (IsSolvable(node.item.state) && node.item.problem == problem)
        {
            ApplySolve(node, action, newName);
            ++changed;
        }
    }
    return changed;
}

int CQueue::RetryAllFailed()
{
    std::lock_guard<std::mutex> lock(mutex_);
    int changed = 0;
    for (auto& entry : items_)
    {
        Node& node = entry.second;
        if (node.item.state == ItemState::Failed)
        {
            ApplySolve(node, SolveAction::Retry, L"");
            ++changed;
        }
    }
    return changed;
}

CQueueCounters CQueue::ChildCounters(uint64_t uid) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (uid == 0)
        return rootKids_;
    auto it = items_.find(uid);
    return it == items_.end() ? CQueueCounters() : it->second.kids;
}

OperationState CQueue::GetOperationState() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ComputeOperationState();
}

CQueueSummary CQueue::Summary() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    CQueueSummary summary;
    summary.state = ComputeOperationState();

    auto count = [this](ItemState s) { return stateCount_[static_cast<int>(s)]; };
    summary.counters.notDone = count(ItemState::Waiting) + count(ItemState::Processing) + count(ItemState::Delayed);
    summary.counters.skipped = count(ItemState::Skipped);
    summary.counters.failed = count(ItemState::Failed);
    summary.counters.userInput = count(ItemState::UserInputNeeded);

    for (const auto& entry : items_)
    {
        const CQueueItem& item = entry.second.item;
        if (item.type == ItemType::DeleteRemoteFile || item.type == ItemType::DeleteRemoteDir ||
            item.type == ItemType::SetAttrs)
        {
            ++summary.totals.metaTotal;
            if (item.state == ItemState::Done)
                ++summary.totals.metaDone;
        }
        if (!IsTransferType(item.type))
            continue;
        const bool done = item.state == ItemState::Done;
        summary.totals.bytesTotal += item.size;
        summary.totals.bytesDone += done ? item.size : item.bytesDone;
        ++summary.totals.filesTotal;
        if (done)
            ++summary.totals.filesDone;
    }
    return summary;
}

uint64_t CQueue::RunId() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return runId_;
}

void CQueue::WaitForWaitingItem(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex_);
    const uint64_t generation = wakeGeneration_;
    wake_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                   [&] { return !waitingExplore_.empty() || !waitingOther_.empty() || wakeGeneration_ != generation; });
}

void CQueue::WaitForWork(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex_);
    const uint64_t generation = wakeGeneration_;
    wake_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                   [&]
                   {
                       return !waitingExplore_.empty() || !waitingOther_.empty() || wakeGeneration_ != generation ||
                              ComputeOperationState() != OperationState::Running;
                   });
}

void CQueue::WakeAll()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++wakeGeneration_;
    }
    wake_.notify_all();
}

std::vector<CQueueItem> CQueue::Items(bool errorsOnly) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CQueueItem> result;
    for (const auto& entry : items_)
    {
        if (!errorsOnly || IsErrorState(entry.second.item.state))
            result.push_back(entry.second.item);
    }
    return result;
}

std::vector<CQueueItem> CQueue::ProcessingItems(size_t maxCount) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CQueueItem> result;
    for (uint64_t uid : processing_)
    {
        if (result.size() >= maxCount)
            break;
        auto it = items_.find(uid);
        if (it != items_.end())
            result.push_back(it->second.item);
    }
    return result;
}

bool CQueue::GetItem(uint64_t uid, CQueueItem& item) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = items_.find(uid);
    if (it == items_.end())
        return false;
    item = it->second.item;
    return true;
}
