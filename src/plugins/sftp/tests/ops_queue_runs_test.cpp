// Tests for the step 4b additions of the queue (runs, transfer bookkeeping, waiting for items) and for the loop guard.
// Pure: no network.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "explored_dirs.h"
#include "op_queue.h"

namespace
{
CQueueItem MakeDownload(const char* name)
{
    CQueueItem item;
    item.type = ItemType::Download;
    item.remotePath = name;
    item.localPath = L"C:\\target\\file";
    item.size = 10;
    return item;
}

CQueueItem Take(CQueue& queue)
{
    CQueueItem item;
    EXPECT_TRUE(queue.TakeNextWaiting(item));
    return item;
}
} // namespace

TEST(QueueRuns, TheRunIdGrowsEachTimeTheQueueBecomesBusyAgain)
{
    CQueue queue;
    EXPECT_EQ(queue.RunId(), 0u);
    const uint64_t uid = queue.AddRootItems({MakeDownload("a"), MakeDownload("b")})[0];
    EXPECT_EQ(queue.RunId(), 1u); // the first items start the first run

    const CQueueItem a = Take(queue);
    queue.Finish(a.uid, ItemState::UserInputNeeded, Problem::TargetExists);
    EXPECT_EQ(queue.RunId(), 1u);
    const CQueueItem b = Take(queue);
    queue.Finish(b.uid, ItemState::Done);
    EXPECT_EQ(queue.GetOperationState(), OperationState::NeedsUser);
    EXPECT_EQ(queue.RunId(), 1u); // ending a run does not change the id

    EXPECT_EQ(queue.Solve(uid, SolveAction::Overwrite), 1);
    EXPECT_EQ(queue.RunId(), 2u); // solved: running again
    const CQueueItem again = Take(queue);
    queue.Finish(again.uid, ItemState::Done);
    EXPECT_EQ(queue.GetOperationState(), OperationState::Finished);
    EXPECT_EQ(queue.RunId(), 2u);
}

TEST(QueueRuns, SolveAllChangesTheRunIdOnce)
{
    CQueue queue;
    const std::vector<uint64_t> uids = queue.AddRootItems({MakeDownload("a"), MakeDownload("b"), MakeDownload("c")});
    for (int i = 0; i < 3; ++i)
        queue.Finish(Take(queue).uid, ItemState::UserInputNeeded, Problem::TargetExists);
    EXPECT_EQ(queue.RunId(), 1u);
    EXPECT_EQ(queue.Solve(uids[0], SolveAction::Overwrite, L"", true), 3);
    EXPECT_EQ(queue.RunId(), 2u);
}

TEST(QueueTransferBookkeeping, OwnsPartialRetargetAndTargetInfoAreKept)
{
    CQueue queue;
    const uint64_t uid = queue.AddRootItems({MakeDownload("a")})[0];
    CQueueItem item = Take(queue);
    EXPECT_FALSE(item.ownsPartial);

    EXPECT_TRUE(queue.SetOwnsPartial(uid));
    EXPECT_TRUE(queue.SetTargetInfo(uid, 123, 456));
    EXPECT_FALSE(queue.SetOwnsPartial(999));

    queue.Finish(uid, ItemState::UserInputNeeded, Problem::TargetExists);
    EXPECT_EQ(queue.Solve(uid, SolveAction::Rename, L"new.txt"), 1);
    CQueueItem solved;
    ASSERT_TRUE(queue.GetItem(uid, solved));
    EXPECT_EQ(solved.forceAction, SolveAction::Rename);
    EXPECT_EQ(solved.forceNewName, L"new.txt");
    EXPECT_TRUE(solved.ownsPartial); // survives a solve
    EXPECT_EQ(solved.targetSize, 123u);
    EXPECT_EQ(solved.targetMtime, 456);

    // retargeting: the forced action is used up, empty paths are left alone
    EXPECT_TRUE(queue.Retarget(uid, L"C:\\target\\new.txt", std::string()));
    ASSERT_TRUE(queue.GetItem(uid, solved));
    EXPECT_EQ(solved.localPath, L"C:\\target\\new.txt");
    EXPECT_EQ(solved.remotePath, "a");
    EXPECT_EQ(solved.forceAction, SolveAction::Retry);
    EXPECT_TRUE(solved.forceNewName.empty());
    EXPECT_TRUE(queue.Retarget(uid, std::wstring(), "b"));
    ASSERT_TRUE(queue.GetItem(uid, solved));
    EXPECT_EQ(solved.remotePath, "b");
    EXPECT_EQ(solved.localPath, L"C:\\target\\new.txt");
}

TEST(QueueTransferBookkeeping, ReturnToWaitingKeepsWhatTheWorkerLearned)
{
    CQueue queue;
    const uint64_t uid = queue.AddRootItems({MakeDownload("a")})[0];
    Take(queue);
    queue.SetOwnsPartial(uid);
    queue.SetProgress(uid, 5);
    EXPECT_TRUE(queue.ReturnToWaiting(uid));
    CQueueItem item;
    ASSERT_TRUE(queue.GetItem(uid, item));
    EXPECT_EQ(item.state, ItemState::Waiting);
    EXPECT_TRUE(item.ownsPartial);
    EXPECT_EQ(item.bytesDone, 5u);
}

TEST(QueueWait, WaitForWaitingItemIgnoresTheOperationState)
{
    CQueue queue; // empty: state Finished
    const auto start = std::chrono::steady_clock::now();
    queue.WaitForWaitingItem(150); // WaitForWork would return at once here
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(120));
}

TEST(QueueWait, WaitForWaitingItemWakesWhenAnItemAppearsOrOnWakeAll)
{
    CQueue queue;
    auto waited = [&](const std::function<void()>& poke)
    {
        const auto start = std::chrono::steady_clock::now();
        std::thread poker(
            [&]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                poke();
            });
        queue.WaitForWaitingItem(10000);
        poker.join();
        return std::chrono::steady_clock::now() - start;
    };
    EXPECT_LT(waited([&] { queue.AddRootItems({MakeDownload("a")}); }), std::chrono::seconds(5));
    EXPECT_LT(waited([&] { queue.WakeAll(); }), std::chrono::seconds(5));
}

TEST(ExploredDirs, ARepeatedRealPathIsRecognised)
{
    CExploredDirs dirs;
    EXPECT_FALSE(dirs.Contains("/srv/a"));
    EXPECT_TRUE(dirs.Insert("/srv/a"));
    EXPECT_TRUE(dirs.Contains("/srv/a"));
    EXPECT_FALSE(dirs.Insert("/srv/a")); // a link back to an explored directory resolves to the same path
    EXPECT_TRUE(dirs.Insert("/srv/a/b"));
    EXPECT_FALSE(dirs.Contains("/srv/A")); // paths are case sensitive on the server
}

TEST(ExploredDirs, OnlyOneOfTwoRacingThreadsWins)
{
    CExploredDirs dirs;
    std::atomic<int> winners{0};
    std::thread t1([&] { winners += dirs.Insert("/same") ? 1 : 0; });
    std::thread t2([&] { winners += dirs.Insert("/same") ? 1 : 0; });
    t1.join();
    t2.join();
    EXPECT_EQ(winners.load(), 1);
}

TEST(QueueTransferBookkeeping, RenameSentIsKeptWhenTheItemGoesBackToWaiting)
{
    CQueue queue;
    const uint64_t uid = queue.AddRootItems({MakeDownload("a")})[0];
    Take(queue);
    CQueueItem item;
    ASSERT_TRUE(queue.GetItem(uid, item));
    EXPECT_FALSE(item.renameSent);

    EXPECT_TRUE(queue.SetRenameSent(uid));
    EXPECT_FALSE(queue.SetRenameSent(999));
    EXPECT_TRUE(queue.ReturnToWaiting(uid)); // a pause or a lost connection
    ASSERT_TRUE(queue.GetItem(uid, item));
    EXPECT_TRUE(item.renameSent); // the next attempt must still know that the rename may have been done
}
