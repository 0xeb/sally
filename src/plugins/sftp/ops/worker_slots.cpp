#include "worker_slots.h"

#include <algorithm>
#include <chrono>

#include "global_config.h"

CWorkerSlots::CWorkerSlots(std::function<int()> limit) : limit_(std::move(limit)) {}

int CWorkerSlots::CurrentLimit() const
{
    if (limit_)
        return (std::max)(limit_(), 1);
    return (std::max)(GlobalConfigStore().Get().maxWorkersTotal, 1);
}

bool CWorkerSlots::TakeLocked(int limit)
{
    if (held_ >= limit)
        return false;
    ++held_;
    peak_ = (std::max)(peak_, held_);
    return true;
}

bool CWorkerSlots::TryAcquire()
{
    const int limit = CurrentLimit(); // before the lock: the configuration has a lock of its own
    std::lock_guard<std::mutex> lock(mutex_);
    return TakeLocked(limit);
}

bool CWorkerSlots::Acquire(const std::function<bool()>& shouldStop)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++waiting_;
    }
    bool got = false;
    for (;;)
    {
        if (shouldStop && shouldStop()) // no lock held: the callback belongs to the caller
            break;
        const int limit = CurrentLimit();
        std::unique_lock<std::mutex> lock(mutex_);
        if (TakeLocked(limit))
        {
            got = true;
            break;
        }
        // Woken by Release; the time slice covers a cancel, a pause and a changed limit, which nobody signals.
        freed_.wait_for(lock, std::chrono::milliseconds(kWaitSliceMs));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    --waiting_;
    return got;
}

void CWorkerSlots::Release()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (held_ == 0)
            return; // a Release without Acquire is a bug; do not go negative
        --held_;
    }
    freed_.notify_all();
}

int CWorkerSlots::Held() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return held_;
}

int CWorkerSlots::Waiting() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return waiting_;
}

int CWorkerSlots::Peak() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return peak_;
}

void CWorkerSlots::ResetPeak()
{
    std::lock_guard<std::mutex> lock(mutex_);
    peak_ = held_;
}

CWorkerSlots& WorkerSlots()
{
    static CWorkerSlots slots;
    return slots;
}

bool CSlotLease::TryAcquire()
{
    if (!held_)
        held_ = slots_.TryAcquire();
    return held_;
}

bool CSlotLease::Acquire(const std::function<bool()>& shouldStop)
{
    if (!held_)
        held_ = slots_.Acquire(shouldStop);
    return held_;
}

void CSlotLease::Release()
{
    if (!held_)
        return;
    held_ = false;
    slots_.Release();
}
