// The operation window: a modeless dialog on its OWN thread that shows one COperation (progress, speed, items with
// problems) and lets the user pause, cancel, retry and answer the questions workers ask (host key, prompts).
//
// Who owns whom
//   COperationWindowHost::Create builds the operation AND its host. The host is the operation's IOperationObserver (the
//   "bridge": workers call it, it posts messages to the window and returns). The returned shared_ptr<COperation> keeps
//   the host alive through its deleter, so the observer pointer inside the operation can never dangle, no matter who
//   holds the operation (operations list, window, ...). The host holds only a raw pointer to the operation; that is
//   valid because the deleter closes the window (joins its thread) BEFORE it destroys the operation:
//       last shared_ptr<COperation> released -> host->Shutdown() (answers open questions "reject", joins the window
//       thread) -> ~COperation (cancels and joins the workers; may still call the bridge, which only posts or rejects)
//       -> the deleter's copy of the host is released.
//   (C# analogy: the operation's Dispose first disposes the window that watches it.) The deleter must not run on the
//   window thread itself; the window thread never holds a shared_ptr<COperation>, so it cannot.
//
// Order of use:  Create -> operation->Start() -> host->Show().  Show before Start works as well: a question that arrives
//   while the window thread is still starting is kept until the window exists. A question that arrives while NO window
//   exists (closed, or after Shutdown) is answered "reject" at once so the worker never waits for nobody.
//
// Threads: Show/Hide/Close/Shutdown are for the main thread (or any thread other than the window thread). The observer
//   callbacks come from workers at any time. The window thread is the only one that touches the window and the list.
//   Locks: mutex_ guards only the window handle and two flags; it is a leaf (taken for a PostMessage, never held while
//   calling the operation, the SDK or a dialog). lifecycleMutex_ serializes Show/Hide/Close and is held while joining
//   the window thread; the window thread never takes it.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "centered_dialog.h"
#include "operation.h"

// What the owner of an operation tells its window. All callbacks run on the window thread (so they must be thread
// safe) and are optional.
struct COperationWindowOptions
{
    std::wstring source;   // shown after "From:" (e.g. sftp:@Site/dir)
    std::wstring target;   // shown after "To:"
    std::wstring hostName; // "host:port", named in the questions the workers ask
    bool closeWhenDone = false;  // initial state of "Close this window when done"
    bool errorsOnly = true;      // initial state of "Errors only"
    // The checkbox "Close this window when done" changed.
    std::function<void(bool)> closeWhenDoneChanged;
    // A run of the operation is over (also after a cancel). Called on a WORKER thread, so it must be thread safe and
    // cheap; exceptions are swallowed. The owner uses it to tell Sally which paths changed.
    std::function<void()> finished;
    // The "Solve error..." button. Default: ShowSolveErrorDialog. Gets the window (as dialog parent), the operation
    // and the uid of the selected item.
    std::function<void(HWND, COperation&, uint64_t)> solveError;
};

class COperationWindowHost;

struct CWindowedOperation
{
    std::shared_ptr<COperation> operation;
    std::shared_ptr<COperationWindowHost> window;
};

class COperationWindow; // the dialog itself, private to operation_window.cpp

class COperationWindowHost : public IOperationObserver
{
public:
    // The operation is NOT started. Throws what COperation's constructor throws (std::bad_alloc).
    static CWindowedOperation Create(COperationSettings settings, CPanelConnectParams params,
                                     std::vector<CQueueItem> roots, COperationWindowOptions options);

    ~COperationWindowHost() override;
    COperationWindowHost(const COperationWindowHost&) = delete;
    COperationWindowHost& operator=(const COperationWindowHost&) = delete;

    // Creates the window (and its thread) when needed, shows it and brings it to the front. False after Shutdown or when
    // the thread could not be started.
    bool Show();
    // Hides the window; it keeps working and shows itself again when a worker needs an answer. No-op without a window.
    void Hide();
    // Destroys the window and joins its thread. The operation keeps running; Show creates a new window.
    // An open question dialog is answered "reject".
    void Close();
    bool IsWindowOpen() const; // the window thread is running (visible or hidden)

    // IOperationObserver: called by workers, any thread, cheap.
    void OnItemChanged(uint64_t uid) override;
    void OnProgress() override;
    void OnStateChanged() override;
    void OnFinished() override;
    void OnInteractionPending() override;

private:
    friend class COperationWindow;

    explicit COperationWindowHost(COperationWindowOptions options);
    void Shutdown(); // from the deleter of the operation (see the top of this file)
    void WindowThreadMain();
    void CloseLocked();                    // lifecycleMutex_ held
    void Post(std::atomic<bool>& pending, UINT message);
    void RejectPendingInteraction();
    void NoteStateForElapsedTime();

    // window thread -> host
    bool OnWindowCreated(HWND window, bool& wantVisible); // false: close was requested meanwhile
    void OnWindowDestroyed();
    void SetWantVisible(bool visible);

    COperationWindowOptions options_;
    COperation* operation_ = nullptr; // not owned; valid until Shutdown returns (see above)

    std::mutex lifecycleMutex_; // Show/Hide/Close/Shutdown; held while joining the window thread
    std::thread thread_;        // guarded by lifecycleMutex_

    mutable std::mutex mutex_; // the leaf lock: guards hwnd_, closing_, shutdown_, wantVisible_, running_
    HWND hwnd_ = nullptr;         // valid only while set; the window thread clears it in WM_DESTROY
    bool closing_ = false;
    bool shutdown_ = false;
    bool wantVisible_ = true;
    bool running_ = false; // the window thread has started and not ended

    // one pending WM_APP message per kind (the window clears the flag before it reads the new state)
    std::atomic<bool> pendingItems_{false};
    std::atomic<bool> pendingProgress_{false};
    std::atomic<bool> pendingState_{false};
    std::atomic<bool> pendingInteraction_{false};

    // elapsed time of the operation, kept here so a reopened window shows the right value
    std::atomic<long long> startMs_{0};
    std::atomic<long long> endMs_{0}; // 0 = still running
    std::atomic<bool> closeWhenDone_{false};
};
