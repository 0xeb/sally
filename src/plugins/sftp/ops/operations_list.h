// All operations of the plugin, by uid. The operations window and Release (unload) work with this list.
//
// Thread safe: one mutex guards the map. It is a leaf except for the operations' own destructors, which are never run
// while it is held (an operation's destructor cancels and joins its worker threads, which can take a moment).
// Lock order: operations list -> operation -> queue -> worker.
#pragma once

#include <winsock2.h>
#include <windows.h>

#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "operation.h"

// What a list row needs, taken in one go.
struct COperationInfo
{
    uint64_t uid = 0;
    OperationState state = OperationState::Running;
    bool paused = false;
    bool finished = false;
    bool cancelled = false;
    std::shared_ptr<COperation> operation; // keeps it alive while the caller looks at it
};

class COperationsList
{
public:
    ~COperationsList() { Clear(); }

    // Returns the uid (1, 2, 3 ...; never reused). The list shares ownership: an operation stays alive until it is
    // removed AND the last other shared_ptr is gone.
    uint64_t Add(std::shared_ptr<COperation> operation);
    std::shared_ptr<COperation> Get(uint64_t uid) const; // empty when unknown
    // Takes the operation out of the list. The caller of the last release runs its destructor (cancel + join).
    bool Remove(uint64_t uid);
    // Like Remove, but only for an operation that is disposable (COperation::IsDisposable: finished with nothing left
    // for the user). False (and nothing removed) for a running, paused, parked or unknown one.
    bool RemoveFinished(uint64_t uid);

    std::vector<COperationInfo> Snapshot() const;
    // True when some operation has not finished (a parked operation waiting for the user counts as finished).
    bool HasRunning() const;

    void CancelAll();
    // True when every operation finished within timeoutMs in total (INFINITE allowed). Cancel first for a quick end.
    bool WaitAllFinished(DWORD timeoutMs);
    // Cancels everything, waits for the threads and empties the list (used by Release).
    void Clear();

private:
    mutable std::mutex mutex_;
    std::map<uint64_t, std::shared_ptr<COperation>> operations_;
    uint64_t nextUid_ = 1;
};
