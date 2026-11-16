// Tests for ops/op_queue (the work list of an operation): order, delayed directories, error propagation, solving,
// counters and a small multi-thread race test. Pure: no network.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <set>
#include <thread>
#include <vector>

#include "op_queue.h"

namespace
{
CQueueItem Make(ItemType type, const char* name, uint64_t size = 0)
{
    CQueueItem item;
    item.type = type;
    item.remotePath = name;
    item.size = size;
    return item;
}

CQueueItem Take(CQueue& queue)
{
    CQueueItem item;
    EXPECT_TRUE(queue.TakeNextWaiting(item));
    return item;
}

ItemState StateOf(const CQueue& queue, uint64_t uid)
{
    CQueueItem item;
    EXPECT_TRUE(queue.GetItem(uid, item));
    return item.state;
}

// A move of "root" with a file F and a sub directory "sub" holding a file G, as the workers would build it.
struct MoveTree
{
    CQueue queue;
    uint64_t root = 0; // uid of the root directory item (explore, then DeleteRemoteDir "root")
    uint64_t sub = 0;  // uid of the sub directory item
    uint64_t fileF = 0;
    uint64_t fileG = 0;

    MoveTree()
    {
        root = queue.AddRootItems({Make(ItemType::ExploreRemote, "root")})[0];
        CQueueItem e = Take(queue);
        CQueueItem afterRoot = Make(ItemType::DeleteRemoteDir, "root");
        queue.ReplaceWithChildren(e.uid, {Make(ItemType::ExploreRemote, "root/sub"), Make(ItemType::Download, "root/F", 10)},
                                  &afterRoot);

        CQueueItem s = Take(queue); // explore first
        sub = s.uid;
        CQueueItem afterSub = Make(ItemType::DeleteRemoteDir, "root/sub");
        queue.ReplaceWithChildren(s.uid, {Make(ItemType::Download, "root/sub/G", 20)}, &afterSub);

        for (const CQueueItem& item : queue.Items(false))
        {
            if (item.remotePath == "root/F")
                fileF = item.uid;
            if (item.remotePath == "root/sub/G")
                fileG = item.uid;
        }
    }

    // takes the item with this path (they are handed out in uid order: F before G)
    void Run(uint64_t uid, ItemState result, Problem problem = Problem::None)
    {
        CQueueItem item = Take(queue);
        ASSERT_EQ(item.uid, uid);
        ASSERT_TRUE(queue.Finish(uid, result, problem));
    }
};
} // namespace

TEST(OpQueue, EmptyQueueIsFinished)
{
    CQueue queue;
    CQueueItem item;
    EXPECT_FALSE(queue.TakeNextWaiting(item));
    EXPECT_EQ(queue.GetOperationState(), OperationState::Finished);
}

TEST(OpQueue, ExploreItemsComeFirst)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::Download, "d1"), Make(ItemType::Upload, "u1"), Make(ItemType::ExploreRemote, "e1"),
                        Make(ItemType::ExploreLocal, "e2"), Make(ItemType::Download, "d2")});
    EXPECT_EQ(Take(queue).remotePath, "e1");
    EXPECT_EQ(Take(queue).remotePath, "e2");
    EXPECT_EQ(Take(queue).remotePath, "d1");
    EXPECT_EQ(Take(queue).remotePath, "u1");
    EXPECT_EQ(Take(queue).remotePath, "d2");
    CQueueItem none;
    EXPECT_FALSE(queue.TakeNextWaiting(none));
}

TEST(OpQueue, TakeMarksProcessingAndReturnsACopy)
{
    CQueue queue;
    const uint64_t uid = queue.AddRootItems({Make(ItemType::Download, "a", 5)})[0];
    CQueueItem item = Take(queue);
    EXPECT_EQ(item.uid, uid);
    EXPECT_EQ(item.parentUid, 0u);
    EXPECT_EQ(StateOf(queue, uid), ItemState::Processing);
    item.size = 999; // changing the copy must not change the queue
    CQueueItem again;
    ASSERT_TRUE(queue.GetItem(uid, again));
    EXPECT_EQ(again.size, 5u);
    EXPECT_EQ(queue.GetOperationState(), OperationState::Running);
}

TEST(OpQueue, UidsAreUniqueAndIncreasing)
{
    CQueue queue;
    auto first = queue.AddRootItems({Make(ItemType::Download, "a"), Make(ItemType::Download, "b")});
    auto second = queue.AddRootItems({Make(ItemType::Download, "c")});
    EXPECT_LT(first[0], first[1]);
    EXPECT_LT(first[1], second[0]);
}

TEST(OpQueue, NestedDelayedDirectoriesFinishInOrder)
{
    MoveTree t;
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::Delayed);
    EXPECT_EQ(StateOf(t.queue, t.sub), ItemState::Delayed);

    t.Run(t.fileF, ItemState::Done);
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::Delayed); // sub is not done yet
    t.Run(t.fileG, ItemState::Done);
    EXPECT_EQ(StateOf(t.queue, t.sub), ItemState::Waiting); // its own action (delete sub) may run now
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::Delayed);

    CQueueItem subAction = Take(t.queue);
    EXPECT_EQ(subAction.uid, t.sub);
    EXPECT_EQ(subAction.type, ItemType::DeleteRemoteDir);
    EXPECT_EQ(subAction.remotePath, "root/sub");
    ASSERT_TRUE(t.queue.Finish(t.sub, ItemState::Done));
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::Waiting);

    CQueueItem rootAction = Take(t.queue);
    EXPECT_EQ(rootAction.type, ItemType::DeleteRemoteDir);
    EXPECT_EQ(t.queue.GetOperationState(), OperationState::Running);
    ASSERT_TRUE(t.queue.Finish(t.root, ItemState::Done));
    EXPECT_EQ(t.queue.GetOperationState(), OperationState::Finished);
}

TEST(OpQueue, ChildFailureForcesTwoLevelsToFail)
{
    MoveTree t;
    t.Run(t.fileF, ItemState::Done);
    t.Run(t.fileG, ItemState::Failed, Problem::TransferFailed);

    EXPECT_EQ(StateOf(t.queue, t.sub), ItemState::ForcedToFail);
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::ForcedToFail);
    CQueueItem none;
    EXPECT_FALSE(t.queue.TakeNextWaiting(none)); // the delete actions must not run
    EXPECT_EQ(t.queue.GetOperationState(), OperationState::FinishedWithErrors);
    // the ForcedToFail directories are consequences, only G counts as failed
    EXPECT_EQ(t.queue.Summary().counters.failed, 1);
}

TEST(OpQueue, ForcedToFailWaitsForTheLastSibling)
{
    MoveTree t;
    // G fails first, F is still waiting: sub is ForcedToFail already, root is still Delayed (F not done)
    CQueueItem f = Take(t.queue);
    ASSERT_EQ(f.uid, t.fileF);
    CQueueItem g = Take(t.queue);
    ASSERT_EQ(g.uid, t.fileG);
    ASSERT_TRUE(t.queue.Finish(t.fileG, ItemState::Failed, Problem::TransferFailed));
    EXPECT_EQ(StateOf(t.queue, t.sub), ItemState::ForcedToFail);
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::Delayed);
    EXPECT_EQ(t.queue.GetOperationState(), OperationState::Running);
    ASSERT_TRUE(t.queue.Finish(t.fileF, ItemState::Done));
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::ForcedToFail);
}

TEST(OpQueue, RetryOfAChildRevivesForcedToFailParents)
{
    MoveTree t;
    t.Run(t.fileF, ItemState::Done);
    t.Run(t.fileG, ItemState::Failed, Problem::TransferFailed);
    ASSERT_EQ(StateOf(t.queue, t.root), ItemState::ForcedToFail);

    EXPECT_EQ(t.queue.Solve(t.fileG, SolveAction::Retry), 1);
    EXPECT_EQ(StateOf(t.queue, t.fileG), ItemState::Waiting);
    EXPECT_EQ(StateOf(t.queue, t.sub), ItemState::Delayed);
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::Delayed);
    EXPECT_EQ(t.queue.GetOperationState(), OperationState::Running);

    t.Run(t.fileG, ItemState::Done);
    EXPECT_EQ(StateOf(t.queue, t.sub), ItemState::Waiting);
}

TEST(OpQueue, SkippingAChildStillForcesParentsToFailButIsASkip)
{
    MoveTree t;
    t.Run(t.fileF, ItemState::Done);
    t.Run(t.fileG, ItemState::Skipped);
    EXPECT_EQ(StateOf(t.queue, t.sub), ItemState::ForcedToFail);
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::ForcedToFail);
    EXPECT_EQ(t.queue.GetOperationState(), OperationState::FinishedWithSkips);
}

TEST(OpQueue, RetryAllFailedRevivesParents)
{
    MoveTree t;
    t.Run(t.fileF, ItemState::Failed, Problem::TransferFailed);
    t.Run(t.fileG, ItemState::Failed, Problem::TransferFailed);
    ASSERT_EQ(StateOf(t.queue, t.root), ItemState::ForcedToFail);

    EXPECT_EQ(t.queue.RetryAllFailed(), 2);
    EXPECT_EQ(StateOf(t.queue, t.fileF), ItemState::Waiting);
    EXPECT_EQ(StateOf(t.queue, t.sub), ItemState::Delayed);
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::Delayed);
    EXPECT_EQ(t.queue.RetryAllFailed(), 0);
}

TEST(OpQueue, SolveOneItem)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::Download, "a"), Make(ItemType::Download, "b")});
    CQueueItem a = Take(queue);
    CQueueItem b = Take(queue);
    ASSERT_TRUE(queue.Finish(a.uid, ItemState::UserInputNeeded, Problem::TargetExists, L"exists"));
    ASSERT_TRUE(queue.Finish(b.uid, ItemState::UserInputNeeded, Problem::TargetExists, L"exists"));
    EXPECT_EQ(queue.GetOperationState(), OperationState::NeedsUser);

    EXPECT_EQ(queue.Solve(a.uid, SolveAction::Rename, L"new name"), 1);
    CQueueItem after;
    ASSERT_TRUE(queue.GetItem(a.uid, after));
    EXPECT_EQ(after.state, ItemState::Waiting);
    EXPECT_EQ(after.forceAction, SolveAction::Rename);
    EXPECT_EQ(after.forceNewName, L"new name");
    EXPECT_EQ(after.problem, Problem::None);
    EXPECT_EQ(StateOf(queue, b.uid), ItemState::UserInputNeeded); // untouched
    EXPECT_EQ(queue.GetOperationState(), OperationState::Running);

    EXPECT_EQ(queue.Solve(b.uid, SolveAction::Skip), 1);
    EXPECT_EQ(StateOf(queue, b.uid), ItemState::Skipped);
    EXPECT_EQ(queue.Solve(b.uid, SolveAction::Retry), 0); // a skipped item cannot be solved again
}

TEST(OpQueue, SolveAllWithTheSameProblem)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::Download, "a"), Make(ItemType::Download, "b"), Make(ItemType::Download, "c"),
                        Make(ItemType::Download, "d")});
    CQueueItem a = Take(queue), b = Take(queue), c = Take(queue), d = Take(queue);
    queue.Finish(a.uid, ItemState::UserInputNeeded, Problem::TargetExists);
    queue.Finish(b.uid, ItemState::UserInputNeeded, Problem::TargetExists);
    queue.Finish(c.uid, ItemState::Failed, Problem::TargetExists); // Failed items with this problem are solvable too
    queue.Finish(d.uid, ItemState::Failed, Problem::PermissionDenied);

    EXPECT_EQ(queue.Solve(a.uid, SolveAction::Overwrite, L"", true), 3);
    for (uint64_t uid : {a.uid, b.uid, c.uid})
    {
        CQueueItem item;
        ASSERT_TRUE(queue.GetItem(uid, item));
        EXPECT_EQ(item.state, ItemState::Waiting);
        EXPECT_EQ(item.forceAction, SolveAction::Overwrite);
    }
    EXPECT_EQ(StateOf(queue, d.uid), ItemState::Failed); // another problem
}

TEST(OpQueue, SolveAllIgnoresApplyToAllForRename)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::Download, "a"), Make(ItemType::Download, "b")});
    CQueueItem a = Take(queue), b = Take(queue);
    queue.Finish(a.uid, ItemState::UserInputNeeded, Problem::TargetExists);
    queue.Finish(b.uid, ItemState::UserInputNeeded, Problem::TargetExists);
    EXPECT_EQ(queue.Solve(a.uid, SolveAction::Rename, L"x", true), 1);
    EXPECT_EQ(StateOf(queue, b.uid), ItemState::UserInputNeeded);
}

TEST(OpQueue, ParkedChildMakesParentForcedToFailAndSolveRevivesIt)
{
    MoveTree t;
    t.Run(t.fileF, ItemState::UserInputNeeded, Problem::TargetExists);
    t.Run(t.fileG, ItemState::Done);
    t.Run(t.sub, ItemState::Done); // the delete action of "sub"
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::ForcedToFail);
    EXPECT_EQ(t.queue.GetOperationState(), OperationState::NeedsUser);

    t.queue.Solve(t.fileF, SolveAction::Overwrite);
    EXPECT_EQ(StateOf(t.queue, t.root), ItemState::Delayed);
}

TEST(OpQueue, OperationStateForEachOutcome)
{
    auto run = [](ItemState result, Problem problem) {
        CQueue queue;
        queue.AddRootItems({Make(ItemType::Download, "a"), Make(ItemType::Download, "b")});
        CQueueItem a = Take(queue);
        EXPECT_EQ(queue.GetOperationState(), OperationState::Running); // a running, b waiting
        queue.Finish(a.uid, ItemState::Done);
        CQueueItem b = Take(queue);
        queue.Finish(b.uid, result, problem);
        return queue.GetOperationState();
    };
    EXPECT_EQ(run(ItemState::Done, Problem::None), OperationState::Finished);
    EXPECT_EQ(run(ItemState::Skipped, Problem::None), OperationState::FinishedWithSkips);
    EXPECT_EQ(run(ItemState::Failed, Problem::TransferFailed), OperationState::FinishedWithErrors);
    EXPECT_EQ(run(ItemState::UserInputNeeded, Problem::TargetExists), OperationState::NeedsUser);
}

TEST(OpQueue, NeedsUserBeatsErrorsAndSkips)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::Download, "a"), Make(ItemType::Download, "b"), Make(ItemType::Download, "c")});
    CQueueItem a = Take(queue), b = Take(queue), c = Take(queue);
    queue.Finish(a.uid, ItemState::Skipped);
    queue.Finish(b.uid, ItemState::Failed, Problem::Other);
    EXPECT_EQ(queue.GetOperationState(), OperationState::Running); // c still runs
    queue.Finish(c.uid, ItemState::UserInputNeeded, Problem::TargetExists);
    EXPECT_EQ(queue.GetOperationState(), OperationState::NeedsUser);

    CQueueSummary summary = queue.Summary();
    EXPECT_EQ(summary.counters.notDone, 0);
    EXPECT_EQ(summary.counters.skipped, 1);
    EXPECT_EQ(summary.counters.failed, 1);
    EXPECT_EQ(summary.counters.userInput, 1);
}

TEST(OpQueue, ChildCountersPerDirectoryAndRoot)
{
    MoveTree t;
    CQueueCounters rootLevel = t.queue.ChildCounters(0);
    EXPECT_EQ(rootLevel.notDone, 1); // the root directory item
    CQueueCounters rootKids = t.queue.ChildCounters(t.root);
    EXPECT_EQ(rootKids.notDone, 2); // sub and F
    t.Run(t.fileF, ItemState::Failed, Problem::Other);
    rootKids = t.queue.ChildCounters(t.root);
    EXPECT_EQ(rootKids.notDone, 1);
    EXPECT_EQ(rootKids.failed, 1);
    t.Run(t.fileG, ItemState::Skipped);
    rootKids = t.queue.ChildCounters(t.root);
    EXPECT_EQ(rootKids.notDone, 0);
    EXPECT_EQ(rootKids.failed, 2); // F, and sub which is ForcedToFail
    CQueueCounters subKids = t.queue.ChildCounters(t.sub);
    EXPECT_EQ(subKids.skipped, 1);
}

TEST(OpQueue, ReturnToWaitingKeepsBytesDone)
{
    CQueue queue;
    const uint64_t uid = queue.AddRootItems({Make(ItemType::Download, "big", 1000)})[0];
    CQueueItem item = Take(queue);
    ASSERT_TRUE(queue.SetProgress(item.uid, 400));
    ASSERT_TRUE(queue.ReturnToWaiting(uid));
    EXPECT_FALSE(queue.ReturnToWaiting(uid)); // not Processing any more

    CQueueItem again = Take(queue); // comes back
    EXPECT_EQ(again.uid, uid);
    EXPECT_EQ(again.bytesDone, 400u);
    EXPECT_EQ(queue.Summary().totals.bytesDone, 400u);
    ASSERT_TRUE(queue.Finish(uid, ItemState::Done));
    EXPECT_EQ(queue.Summary().totals.bytesDone, 1000u);
}

TEST(OpQueue, ReplaceWithChildrenWithoutAfterItem)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::ExploreLocal, "dir")});
    CQueueItem e = Take(queue);
    ASSERT_TRUE(queue.ReplaceWithChildren(e.uid, {Make(ItemType::Upload, "dir/a", 3), Make(ItemType::Upload, "dir/b", 4)}));
    EXPECT_EQ(StateOf(queue, e.uid), ItemState::Done);
    EXPECT_EQ(queue.GetOperationState(), OperationState::Running);
    CQueueItem a = Take(queue);
    CQueueItem b = Take(queue);
    EXPECT_EQ(a.parentUid, 0u); // the children belong to the explore item's parent
    queue.Finish(a.uid, ItemState::Done);
    queue.Finish(b.uid, ItemState::Done);
    EXPECT_EQ(queue.GetOperationState(), OperationState::Finished);

    CQueueSummary summary = queue.Summary();
    EXPECT_EQ(summary.totals.filesTotal, 2);
    EXPECT_EQ(summary.totals.filesDone, 2);
    EXPECT_EQ(summary.totals.bytesTotal, 7u);
    EXPECT_EQ(summary.totals.bytesDone, 7u);
}

TEST(OpQueue, ReplaceWithChildrenWithAfterItemAndNoChildren)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::ExploreRemote, "empty")});
    CQueueItem e = Take(queue);
    CQueueItem after = Make(ItemType::DeleteRemoteDir, "empty");
    ASSERT_TRUE(queue.ReplaceWithChildren(e.uid, {}, &after));
    EXPECT_EQ(StateOf(queue, e.uid), ItemState::Waiting); // nothing to wait for
    CQueueItem action = Take(queue);
    EXPECT_EQ(action.uid, e.uid);
    EXPECT_EQ(action.type, ItemType::DeleteRemoteDir);
    queue.Finish(action.uid, ItemState::Done);
    EXPECT_EQ(queue.GetOperationState(), OperationState::Finished);
}

TEST(OpQueue, ReplaceWithChildrenRejectsWrongItems)
{
    CQueue queue;
    const uint64_t file = queue.AddRootItems({Make(ItemType::Download, "f"), Make(ItemType::ExploreRemote, "d")})[0];
    EXPECT_FALSE(queue.ReplaceWithChildren(999, {}));
    EXPECT_FALSE(queue.ReplaceWithChildren(file, {}));        // not an explore item
    CQueueItem e = Take(queue);                               // takes the explore item
    ASSERT_TRUE(queue.ReplaceWithChildren(e.uid, {}));
    EXPECT_FALSE(queue.ReplaceWithChildren(e.uid, {}));       // no longer Processing
}

TEST(OpQueue, FinishRejectsWrongCalls)
{
    CQueue queue;
    const uint64_t uid = queue.AddRootItems({Make(ItemType::Download, "a")})[0];
    EXPECT_FALSE(queue.Finish(uid, ItemState::Done));      // still Waiting
    Take(queue);
    EXPECT_FALSE(queue.Finish(uid, ItemState::Waiting));   // not a result state
    EXPECT_FALSE(queue.Finish(uid, ItemState::ForcedToFail));
    EXPECT_TRUE(queue.Finish(uid, ItemState::Done));
    EXPECT_FALSE(queue.Finish(uid, ItemState::Done));      // twice
    EXPECT_FALSE(queue.Finish(12345, ItemState::Done));
}

TEST(OpQueue, ItemsFilterAndErrorTextArePreserved)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::Download, "a"), Make(ItemType::Download, "b")});
    CQueueItem a = Take(queue);
    queue.Finish(a.uid, ItemState::Failed, Problem::DiskFull, L"disk is full");
    EXPECT_EQ(queue.Items(false).size(), 2u);
    std::vector<CQueueItem> errors = queue.Items(true);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].uid, a.uid);
    EXPECT_EQ(errors[0].problem, Problem::DiskFull);
    EXPECT_EQ(errors[0].errorText, L"disk is full");
}

// Several threads take and finish items at the same time: no item may go to two threads, and all must be handed out.
TEST(OpQueue, ConcurrentTakeNeverHandsOutAnItemTwice)
{
    constexpr int kItems = 4000;
    constexpr int kThreads = 6;
    CQueue queue;
    std::vector<CQueueItem> roots;
    for (int i = 0; i < kItems; ++i)
        roots.push_back(Make(i % 10 == 0 ? ItemType::ExploreRemote : ItemType::Download, "x", 1));
    queue.AddRootItems(roots);

    std::vector<std::vector<uint64_t>> taken(kThreads);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&queue, &taken, t] {
            CQueueItem item;
            while (queue.TakeNextWaiting(item))
            {
                taken[t].push_back(item.uid);
                if (IsExploreType(item.type))
                    queue.ReplaceWithChildren(item.uid, {}); // finishes the explore item
                else
                    queue.Finish(item.uid, ItemState::Done);
            }
        });
    }
    for (std::thread& th : threads)
        th.join();

    std::set<uint64_t> unique;
    size_t total = 0;
    for (const auto& list : taken)
    {
        total += list.size();
        unique.insert(list.begin(), list.end());
    }
    EXPECT_EQ(total, static_cast<size_t>(kItems));
    EXPECT_EQ(unique.size(), static_cast<size_t>(kItems));
    EXPECT_EQ(queue.GetOperationState(), OperationState::Finished);
    EXPECT_EQ(queue.Summary().totals.filesDone, kItems - kItems / 10);
}

TEST(QueueWait, ReturnsAtOnceWhenSomethingIsWaiting)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::Download, "a", 1)});
    const auto start = std::chrono::steady_clock::now();
    queue.WaitForWork(5000);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(1000));
}

TEST(QueueWait, TimesOutWhenOnlyProcessingItemsRemain)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::Download, "a", 1)});
    Take(queue); // Running, nothing waiting
    const auto start = std::chrono::steady_clock::now();
    queue.WaitForWork(100);
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(80));
}

TEST(QueueWait, WakesWhenAnItemIsReturnedToWaitingOrWakeAllIsCalled)
{
    CQueue queue;
    const uint64_t uid = queue.AddRootItems({Make(ItemType::Download, "a", 1)})[0];
    Take(queue);

    auto waitedMs = [&queue](const std::function<void()>& poke)
    {
        const auto start = std::chrono::steady_clock::now();
        std::thread poker(
            [&poke]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                poke();
            });
        queue.WaitForWork(10000);
        poker.join();
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    };
    EXPECT_LT(waitedMs([&] { queue.ReturnToWaiting(uid); }), 5000);
    Take(queue);
    EXPECT_LT(waitedMs([&] { queue.WakeAll(); }), 5000);
    // nothing waiting now: only the finish of the item ends the wait
    EXPECT_TRUE(queue.GetOperationState() == OperationState::Running);
    EXPECT_LT(waitedMs([&] { queue.Finish(uid, ItemState::Done); }), 5000);
    EXPECT_EQ(queue.GetOperationState(), OperationState::Finished);
}

TEST(QueueDepth, IsKeptByTheQueue)
{
    CQueue queue;
    CQueueItem root = Make(ItemType::ExploreRemote, "r");
    root.depth = 3;
    const uint64_t uid = queue.AddRootItems({root})[0];
    CQueueItem e = Take(queue);
    EXPECT_EQ(e.depth, 3);
    CQueueItem child = Make(ItemType::ExploreRemote, "r/c");
    child.depth = e.depth + 1;
    queue.ReplaceWithChildren(uid, {child});
    bool found = false;
    for (const CQueueItem& item : queue.Items(false))
        found |= item.remotePath == "r/c" && item.depth == 4;
    EXPECT_TRUE(found);
}

TEST(OpQueue, ProcessingItemsListsWhatWorkersHold)
{
    CQueue queue;
    queue.AddRootItems({Make(ItemType::Download, "a", 1), Make(ItemType::Download, "b", 2), Make(ItemType::Download, "c", 3)});
    EXPECT_TRUE(queue.ProcessingItems(10).empty());

    const CQueueItem a = Take(queue);
    const CQueueItem b = Take(queue);
    std::vector<CQueueItem> processing = queue.ProcessingItems(10);
    ASSERT_EQ(processing.size(), 2u);
    EXPECT_EQ(processing[0].uid, a.uid);
    EXPECT_EQ(processing[1].uid, b.uid);
    EXPECT_EQ(queue.ProcessingItems(1).size(), 1u); // capped

    queue.Finish(a.uid, ItemState::Done);
    queue.ReturnToWaiting(b.uid);
    EXPECT_TRUE(queue.ProcessingItems(10).empty());
}
