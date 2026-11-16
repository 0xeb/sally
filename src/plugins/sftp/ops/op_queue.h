// The work list of one operation (pure model: no threads of its own, no network, no Sally).
//
// An operation (copy/move/...) starts with a few root items (the selected files and directories). A worker takes one
// item, does it, and reports the result. A directory is first an "explore" item; exploring it replaces it with one item
// per entry. Every item belongs to a parent (parentUid, 0 = the operation itself).
//
// Directories that must do something AFTER their content (a move deletes the source directory) are "delayed" items:
//   Delayed       -> some child is still waiting or running
//   Waiting       -> all children are done, now the directory's own action runs (e.g. delete the empty directory)
//   ForcedToFail  -> no child is waiting any more, but at least one was skipped, failed or needs the user: the
//                    directory's own action must NOT run. It is not a failure of its own.
// The state changes ripple up the parents (a ForcedToFail directory can make its Delayed parent ForcedToFail too) and
// back down again when the user retries or solves a child.
//
// Thread safety: ONE mutex guards everything. Every public method takes it, does a short piece of work and returns;
// no method calls out (no callbacks, no libssh2, no Sally) while the lock is held. Methods that hand out items return
// COPIES, never pointers or references into the queue, because other threads change the queue at any time.
// In the lock order (operations list -> operation -> queue -> worker) the queue lock is a leaf for this class.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "op_types.h"
#include "ssh_types.h"

enum class ItemType
{
    ExploreRemote, // list a remote directory and replace this item with its content
    ExploreLocal,  // the same for a local directory
    Download,
    Upload,
    MakeDirRemote,
    MakeDirLocal,
    DeleteRemoteFile, // move: delete the source after a successful upload
    DeleteRemoteDir,
    DeleteLocalFile,
    DeleteLocalDir,
    SetAttrs // chmod: apply the operation's mode change to this remote file or directory
};

// Error states are last, so "state >= Skipped" means "needs a look from the user" (see IsErrorState).
enum class ItemState
{
    Waiting,         // a worker can take it
    Processing,      // a worker has it
    Delayed,         // a directory waiting for its children
    Done,
    Skipped,         // the user (or the policy) skipped it
    Failed,          // failed, the user can retry or solve
    UserInputNeeded, // parked: a decision is needed (e.g. the target exists and the policy is Ask)
    ForcedToFail     // a directory whose own action cannot run because a child is skipped/failed/parked
};

inline bool IsErrorState(ItemState state)
{
    return state >= ItemState::Skipped;
}

inline bool IsExploreType(ItemType type)
{
    return type == ItemType::ExploreRemote || type == ItemType::ExploreLocal;
}

inline bool IsTransferType(ItemType type)
{
    return type == ItemType::Download || type == ItemType::Upload;
}

struct CQueueItem
{
    uint64_t uid = 0;       // unique in the queue, never reused; set by the queue
    uint64_t parentUid = 0; // 0 = a root item of the operation; set by the queue
    ItemType type = ItemType::Download;
    ItemState state = ItemState::Waiting;
    std::string remotePath;  // raw bytes as the server sent them (no decoding)
    std::wstring localPath;
    uint64_t size = 0;       // source size in bytes (transfers)
    int64_t mtime = 0;       // source modification time, Unix seconds, 0 = unknown
    Problem problem = Problem::None; // why it is in an error state
    std::wstring errorText;          // details for the user
    CSshError sshError;              // the SSH error behind errorText, when there is one (the UI words it from the string table)
    SolveAction forceAction = SolveAction::Retry; // the user's answer; Retry = none, the policy decides
    std::wstring forceNewName;                    // for forceAction == Rename
    uint64_t bytesDone = 0;          // kept when the item goes back to Waiting, so a resume knows the offset
    int depth = 0;                   // explore items: how deep below the root (0 = root); the workers set it for children

    // Set by the workers while they transfer
    std::wstring parentLocal;  // downloads: the folder the target goes into (a Rename solve builds the new path from it)
    std::string parentRemote;  // uploads: the same on the server (raw bytes)
    // Reached through a directory link (a remote symlink or a local junction/symlink): a move transfers it but must not
    // delete the source, because that is the content of the link's target. The link itself is removed as a link.
    bool viaLink = false;
    bool ownsPartial = false;  // this operation has written (part of) the target: a retry resumes it whatever the policy
    // Safe upload: the worker sent the final rename (.filepart -> name). When the answer was lost (a pause or a dropped
    // connection at that moment) the server may have done it; a retry then finds the finished file and no .filepart.
    bool renameSent = false;
    uint64_t targetSize = 0;   // what the last conflict check saw at the target (for the solve dialog)
    int64_t targetMtime = 0;
};

// How many direct children of one directory (or of the operation) are in which state.
struct CQueueCounters
{
    int notDone = 0;    // Waiting, Processing or Delayed
    int skipped = 0;
    int failed = 0;     // Failed; for a directory's children also ForcedToFail children
    int userInput = 0;  // UserInputNeeded
};

enum class OperationState
{
    Running,            // something is waiting, running or delayed
    Finished,           // all done
    FinishedWithSkips,  // nothing left to do; some items were skipped
    FinishedWithErrors, // nothing left to do; some items failed
    NeedsUser           // nothing left to do except items parked for a decision
};

struct CQueueTotals
{
    uint64_t bytesTotal = 0; // sum of the sizes of all transfer items (Download/Upload)
    uint64_t bytesDone = 0;  // a Done transfer counts its full size
    int filesTotal = 0;
    int filesDone = 0;
    // Remote delete items (DeleteRemoteFile/Dir; a move creates them too) and chmod items (SetAttrs). A delete or chmod
    // operation reports these as its items.
    int metaTotal = 0;
    int metaDone = 0;
};

// Everything the operation window shows, taken in one go under one lock so the numbers fit together.
struct CQueueSummary
{
    OperationState state = OperationState::Finished;
    // Whole operation: counted over ALL items (not only the root ones). 'failed' counts Failed items only; a
    // ForcedToFail directory is a consequence of its children and is not counted again.
    CQueueCounters counters;
    CQueueTotals totals;
};

class CQueue
{
public:
    // Adds root items (parentUid 0). Their state becomes Waiting and they get new uids (returned in the same order).
    std::vector<uint64_t> AddRootItems(std::vector<CQueueItem> items);

    // Hands out the next Waiting item (explore items first, then the others, each in the order they were added),
    // marks it Processing and copies it to 'item'. False when nothing is waiting.
    bool TakeNextWaiting(CQueueItem& item);
    // True when an item is Waiting (a look only: another worker may take it before the caller does).
    bool HasWaiting() const;

    // Called by a worker for a Processing explore item 'uid' after it listed the directory.
    //  - Without 'after': the children become children of the explore item's parent and the explore item is Done.
    //  - With 'after' (for example the item "delete this source directory" of a move): the explore item becomes that
    //    item (same uid, state Delayed) and the children become ITS children. When the last child is done it turns
    //    Waiting and a worker runs it; with no children it turns Waiting at once.
    // Children get new uids and the state Waiting. False when 'uid' is unknown or not Processing.
    bool ReplaceWithChildren(uint64_t uid, std::vector<CQueueItem> children, const CQueueItem* after = nullptr);

    // A worker reports the result of the Processing item 'uid'. 'state' must be Done, Skipped, Failed or
    // UserInputNeeded. For Done a transfer's bytesDone becomes its size. False when the call does not apply.
    bool Finish(uint64_t uid, ItemState state, Problem problem = Problem::None, const std::wstring& errorText = L"",
                const CSshError& sshError = CSshError());

    // A worker reports the bytes it has transferred so far (progress for the window and the resume offset).
    bool SetProgress(uint64_t uid, uint64_t bytesDone);

    // Pause or lost connection: a Processing item goes back to Waiting. bytesDone is kept.
    bool ReturnToWaiting(uint64_t uid);

    // Worker bookkeeping for a transfer item (all return false for an unknown uid):
    // - SetOwnsPartial: the operation is about to write the target (see CQueueItem::ownsPartial).
    // - Retarget: the item now goes to another name (a rename was decided), or the operation now owns the target
    //   it writes. Empty paths are left alone. The user's forced action and new name are used up, so a later retry
    //   does not decide again.
    // - SetTargetInfo: size and time of the existing target, kept for the solve dialog.
    bool SetOwnsPartial(uint64_t uid);
    // The worker is about to send the final rename of a safe upload (see CQueueItem::renameSent).
    bool SetRenameSent(uint64_t uid);
    bool Retarget(uint64_t uid, const std::wstring& localPath, const std::string& remotePath);
    bool SetTargetInfo(uint64_t uid, uint64_t size, int64_t mtime);

    // The user's answer for a Failed or UserInputNeeded item. Skip marks it Skipped; Retry/Overwrite/Resume/Rename put
    // it back to Waiting (Overwrite/Resume/Rename are remembered in forceAction; Rename with 'newName').
    // 'applyToAll': the same answer for every Failed/UserInputNeeded item with the same Problem as 'uid'
    // (ignored for Rename: one name cannot fit several items). Returns how many items were changed.
    int Solve(uint64_t uid, SolveAction action, const std::wstring& newName = L"", bool applyToAll = false);

    // Every Failed item goes back to Waiting (the toolbar "Retry all"). Returns how many.
    int RetryAllFailed();

    // Counters of the direct children of directory item 'uid'; uid 0 = the root items of the operation.
    // Here 'failed' includes ForcedToFail children, because for the parent they mean the same.
    CQueueCounters ChildCounters(uint64_t uid) const;

    OperationState GetOperationState() const;
    CQueueSummary Summary() const;

    // Counts the "runs": it grows every time the operation state changes from "not Running" to Running (the first
    // items are added, or the user solved something after the work had run out). COperation fires OnFinished once per run.
    uint64_t RunId() const;

    // For idle workers (like Monitor.Wait in .NET): returns when an item is Waiting, the operation is not Running any
    // more, WakeAll() was called, or after timeoutMs - whichever comes first. The caller must look again (TakeNextWaiting,
    // GetOperationState): a return does not promise that there is work.
    void WaitForWork(int timeoutMs);
    // For workers of an operation that has nothing to do (not Running): returns when an item is Waiting, WakeAll() was
    // called, or after timeoutMs. Unlike WaitForWork it does not return just because the state is "not Running".
    void WaitForWaitingItem(int timeoutMs);
    // Wakes every thread inside WaitForWork/WaitForWaitingItem (used by cancel and resume).
    void WakeAll();

    // Copies of the items in uid order; errorsOnly: only the items in an error state (Skipped and later).
    std::vector<CQueueItem> Items(bool errorsOnly) const;
    // Copies of the items a worker has right now (at most maxCount, in uid order): "what is being transferred".
    std::vector<CQueueItem> ProcessingItems(size_t maxCount) const;
    // A copy of one item. False when it does not exist.
    bool GetItem(uint64_t uid, CQueueItem& item) const;

private:
    struct Node
    {
        CQueueItem item;
        CQueueCounters kids; // the counters of its direct children
    };
    static constexpr int kStateCount = 8; // number of ItemState values

    // Everything below needs mutex_ to be held by the caller.
    Node* Find(uint64_t uid);
    CQueueCounters& KidsOf(uint64_t parentUid);
    uint64_t Insert(CQueueItem item, uint64_t parentUid);
    void SetState(Node& node, ItemState state);
    void Reevaluate(uint64_t uid);
    void ApplySolve(Node& node, SolveAction action, const std::wstring& newName);
    static void Count(CQueueCounters& counters, ItemState state, int delta);
    static ItemState StateForDelayedDirectory(const CQueueCounters& kids);
    OperationState ComputeOperationState() const;
    void NoteRunState();

    mutable std::mutex mutex_;
    std::condition_variable wake_; // signalled when an item is added or changes state, and by WakeAll
    uint64_t wakeGeneration_ = 0;  // WakeAll increments it
    std::map<uint64_t, Node> items_; // by uid, so iteration is in creation order
    CQueueCounters rootKids_;        // counters of the root items (parentUid 0)
    int stateCount_[kStateCount] = {};
    std::set<uint64_t> waitingExplore_; // uids of Waiting explore items
    std::set<uint64_t> waitingOther_;   // uids of the other Waiting items
    std::set<uint64_t> processing_;     // uids of the Processing items
    uint64_t nextUid_ = 1;
    uint64_t runId_ = 0;
    bool wasRunning_ = false;
};
