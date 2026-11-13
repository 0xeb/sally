// The global cap on transfer connections (D58): every worker of every operation holds one slot while it works, so all
// operations together never use more than "Transfer connections in total" (the Configuration dialog) SFTP sessions.
// Panel sessions are not counted: they never wait for a slot.
//
// C#: CWorkerSlots is a SemaphoreSlim whose maximum can change at run time, with Wait(CancellationToken). The limit is
// read at every decision, so a changed setting takes effect at once; a lower limit never stops a worker that already
// holds a slot, it only keeps new ones waiting.
//
// Locking: the slots mutex is a LEAF lock. It is never held while another lock of the plugin is taken, while libssh2 or
// the SDK is called, or while the "should I stop" callback runs; and no other lock is held while Acquire waits.
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>

class CWorkerSlots
{
public:
    // 'limit' answers "how many slots are there now"; the default reads the global configuration.
    explicit CWorkerSlots(std::function<int()> limit = std::function<int()>());

    CWorkerSlots(const CWorkerSlots&) = delete;
    CWorkerSlots& operator=(const CWorkerSlots&) = delete;

    // Takes a slot when one is free right now.
    bool TryAcquire();
    // Waits for a free slot. Returns false (and takes nothing) as soon as shouldStop() says true: the waiter looks at it
    // before the first try and then at least every kWaitSliceMs, so a cancel or a pause is noticed promptly.
    bool Acquire(const std::function<bool()>& shouldStop);
    // Gives a slot back. Call once for every successful Acquire/TryAcquire.
    void Release();

    int Held() const;    // slots in use
    int Waiting() const; // threads inside Acquire
    int Peak() const;    // most slots in use at one time since ResetPeak (tests)
    void ResetPeak();

    static constexpr int kWaitSliceMs = 50;

private:
    int CurrentLimit() const;
    bool TakeLocked(int limit); // mutex_ held

    std::function<int()> limit_;
    mutable std::mutex mutex_;
    std::condition_variable freed_;
    int held_ = 0;
    int waiting_ = 0;
    int peak_ = 0;
};

// The plugin's instance, shared by all operations (a function-local static: no start-up order problems).
CWorkerSlots& WorkerSlots();

// One worker's slot. Lives as long as the worker, is used by the worker's thread only, and gives the slot back in its
// destructor, so no way out of the worker (cancel, error, exception) can leak one. C#: an IDisposable around Wait/Release.
class CSlotLease
{
public:
    explicit CSlotLease(CWorkerSlots& slots) : slots_(slots) {}
    ~CSlotLease() { Release(); }
    CSlotLease(const CSlotLease&) = delete;
    CSlotLease& operator=(const CSlotLease&) = delete;

    bool Held() const { return held_; }
    bool TryAcquire(); // true when the lease holds a slot afterwards
    bool Acquire(const std::function<bool()>& shouldStop);
    void Release(); // does nothing without a slot

private:
    CWorkerSlots& slots_;
    bool held_ = false;
};
