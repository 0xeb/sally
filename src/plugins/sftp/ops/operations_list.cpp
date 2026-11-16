#include "operations_list.h"

#include <algorithm>
#include <utility>

uint64_t COperationsList::Add(std::shared_ptr<COperation> operation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t uid = nextUid_++;
    operations_.emplace(uid, std::move(operation));
    return uid;
}

std::shared_ptr<COperation> COperationsList::Get(uint64_t uid) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = operations_.find(uid);
    return it == operations_.end() ? nullptr : it->second;
}

bool COperationsList::Remove(uint64_t uid)
{
    std::shared_ptr<COperation> removed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = operations_.find(uid);
        if (it == operations_.end())
            return false;
        removed = std::move(it->second);
        operations_.erase(it);
    }
    return true; // 'removed' goes out of scope here, without the lock: the destructor may join threads
}

bool COperationsList::RemoveFinished(uint64_t uid)
{
    const std::shared_ptr<COperation> operation = Get(uid); // asked without the list lock (lock order)
    if (!operation || !operation->IsDisposable())
        return false;
    return Remove(uid); // 'operation' above still holds it: the destructor runs when this function returns, no lock held
}

std::vector<COperationInfo> COperationsList::Snapshot() const
{
    std::vector<std::shared_ptr<COperation>> operations;
    std::vector<uint64_t> uids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : operations_)
        {
            uids.push_back(entry.first);
            operations.push_back(entry.second);
        }
    }
    std::vector<COperationInfo> result; // the operations are asked without the list lock
    for (size_t i = 0; i < operations.size(); ++i)
    {
        COperationInfo info;
        info.uid = uids[i];
        info.state = operations[i]->State();
        info.paused = operations[i]->IsPaused();
        info.finished = operations[i]->IsFinished();
        info.cancelled = operations[i]->WasCancelled();
        info.operation = operations[i];
        result.push_back(std::move(info));
    }
    return result;
}

bool COperationsList::HasRunning() const
{
    for (const COperationInfo& info : Snapshot())
    {
        if (!info.finished)
            return true;
    }
    return false;
}

void COperationsList::CancelAll()
{
    for (const COperationInfo& info : Snapshot())
        info.operation->RequestCancel();
}

bool COperationsList::WaitAllFinished(DWORD timeoutMs)
{
    const DWORD start = GetTickCount();
    for (const COperationInfo& info : Snapshot())
    {
        DWORD wait = INFINITE;
        if (timeoutMs != INFINITE)
        {
            const DWORD elapsed = GetTickCount() - start;
            wait = elapsed >= timeoutMs ? 0 : timeoutMs - elapsed;
        }
        if (!info.operation->WaitFinished(wait))
            return false;
    }
    return true;
}

void COperationsList::Clear()
{
    CancelAll();
    std::map<uint64_t, std::shared_ptr<COperation>> operations;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        operations.swap(operations_);
    }
    operations.clear(); // the destructors cancel (again) and join the threads
}
