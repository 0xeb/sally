// Tests for the delete operation (COperation with TransferDirection::Delete): trees, links that must not be followed,
// directories that cannot be deleted, cancel and the solve offers of a failed delete item. Docker test server.
#include "operation_test_support.h"

#include "solve_offers.h"

namespace
{
class OperationDeleteTest : public OperationTest
{
protected:
    std::string Path(const std::string& relative) const { return remoteDir_ + "/" + relative; }

    // tree/{a.txt, sub/b.txt, sub/deep/c.txt, empty/}: 3 files and 4 directories
    void MakeTree()
    {
        ASSERT_TRUE(admin_.Mkdir(Path("tree")));
        ASSERT_TRUE(admin_.Mkdir(Path("tree/sub")));
        ASSERT_TRUE(admin_.Mkdir(Path("tree/sub/deep")));
        ASSERT_TRUE(admin_.Mkdir(Path("tree/empty")));
        PutRemoteFile(Path("tree/a.txt"), "a");
        PutRemoteFile(Path("tree/sub/b.txt"), "b");
        PutRemoteFile(Path("tree/sub/deep/c.txt"), "c");
    }
};

TEST_F(OperationDeleteTest, DeletesATreeWithNestedDirectories)
{
    MakeTree();
    auto op = MakeDelete({RemoteSrc(Path("tree"), true)});
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_FALSE(RemoteExists(Path("tree")));

    const COperationStats stats = op->Statistics(); // a delete counts items (3 files + 4 directories), not bytes
    EXPECT_EQ(stats.filesTotal, 7);
    EXPECT_EQ(stats.filesDone, 7);
    EXPECT_EQ(stats.bytesTotal, 0u);
}

TEST_F(OperationDeleteTest, DeletesAnEmptyDirectory)
{
    ASSERT_TRUE(admin_.Mkdir(Path("nothing")));
    auto op = MakeDelete({RemoteSrc(Path("nothing"), true)});
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_FALSE(RemoteExists(Path("nothing")));
    EXPECT_EQ(op->Statistics().filesDone, 1);
}

TEST_F(OperationDeleteTest, DeletesSeveralMixedItemsAndLeavesTheRestAlone)
{
    MakeTree();
    PutRemoteFile(Path("single.txt"), "x");
    PutRemoteFile(Path("untouched.txt"), "y");
    auto op = MakeDelete({RemoteSrc(Path("tree"), true), RemoteSrc(Path("single.txt"), false)});
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_FALSE(RemoteExists(Path("tree")));
    EXPECT_FALSE(RemoteExists(Path("single.txt")));
    EXPECT_EQ(GetRemoteFile(Path("untouched.txt")), "y");
}

TEST_F(OperationDeleteTest, ALinkToADirectoryInsideTheTreeIsDeletedAsALink)
{
    ASSERT_TRUE(admin_.Mkdir(Path("keep")));
    PutRemoteFile(Path("keep/precious.txt"), "precious");
    MakeTree();
    ASSERT_TRUE(admin_.Symlink("../../keep", Path("tree/sub/link"))) << admin_.LastError().message;
    ASSERT_TRUE(admin_.Symlink("a.txt", Path("tree/filelink"))) << admin_.LastError().message;

    auto op = MakeDelete({RemoteSrc(Path("tree"), true)});
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_FALSE(RemoteExists(Path("tree")));
    EXPECT_EQ(GetRemoteFile(Path("keep/precious.txt")), "precious"); // what the link pointed to survives
}

TEST_F(OperationDeleteTest, ARootLinkToADirectoryIsDeletedAsALinkEvenWhenListedAsADirectory)
{
    ASSERT_TRUE(admin_.Mkdir(Path("keep")));
    PutRemoteFile(Path("keep/precious.txt"), "precious");
    ASSERT_TRUE(admin_.Symlink("keep", Path("dirlink"))) << admin_.LastError().message;

    auto op = MakeDelete({RemoteSrc(Path("dirlink"), true)}); // the panel lists a link to a directory as a directory
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_FALSE(RemoteExists(Path("dirlink")));
    EXPECT_EQ(GetRemoteFile(Path("keep/precious.txt")), "precious");
}

TEST_F(OperationDeleteTest, ADirectoryThatCannotBeEmptiedParksAndTheRestContinues)
{
    ASSERT_TRUE(admin_.Mkdir(Path("tree")));
    ASSERT_TRUE(admin_.Mkdir(Path("tree/locked")));
    PutRemoteFile(Path("tree/locked/stuck.txt"), "s");
    PutRemoteFile(Path("tree/free.txt"), "f");
    ASSERT_TRUE(admin_.Chmod(Path("tree/locked"), 0555)); // no write permission: nothing inside can be deleted

    auto op = MakeDelete({RemoteSrc(Path("tree"), true)});
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::FinishedWithErrors) << Describe(*op);
    EXPECT_FALSE(RemoteExists(Path("tree/free.txt"))); // the other items were not held up
    EXPECT_TRUE(RemoteExists(Path("tree/locked/stuck.txt")));
    EXPECT_TRUE(RemoteExists(Path("tree/locked"))); // its own deletion must not even be tried
    EXPECT_TRUE(RemoteExists(Path("tree")));

    const std::vector<CQueueItem> failed = ItemsWithProblem(*op, Problem::PermissionDenied);
    ASSERT_EQ(failed.size(), 1u) << Describe(*op);
    EXPECT_EQ(failed[0].state, ItemState::Failed);
    EXPECT_EQ(failed[0].type, ItemType::DeleteRemoteFile);

    // what the solve dialog offers for it: retry and skip, nothing that only makes sense for transfers
    const CSolveOffers offers = SolveOffersFor(failed[0].problem, failed[0].type);
    EXPECT_TRUE(offers.retry);
    EXPECT_TRUE(offers.skip);
    EXPECT_FALSE(offers.overwrite);
    EXPECT_FALSE(offers.resume);
    EXPECT_FALSE(offers.rename);

    // the user fixes the permission and retries: the whole tree goes
    ASSERT_TRUE(admin_.Chmod(Path("tree/locked"), 0755));
    EXPECT_EQ(op->Solve(failed[0].uid, SolveAction::Retry), 1);
    ASSERT_TRUE(WaitUntil([&] { return observer.finished.load() >= 2 && op->State() == OperationState::Finished; }, 30000))
        << Describe(*op);
    EXPECT_FALSE(RemoteExists(Path("tree")));
}

TEST_F(OperationDeleteTest, SkippingTheFailedItemFinishesWithSkipsAndKeepsTheDirectory)
{
    ASSERT_TRUE(admin_.Mkdir(Path("tree")));
    ASSERT_TRUE(admin_.Mkdir(Path("tree/locked")));
    PutRemoteFile(Path("tree/locked/stuck.txt"), "s");
    ASSERT_TRUE(admin_.Chmod(Path("tree/locked"), 0555));

    auto op = MakeDelete({RemoteSrc(Path("tree"), true)});
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    const std::vector<CQueueItem> failed = ItemsWithProblem(*op, Problem::PermissionDenied);
    ASSERT_EQ(failed.size(), 1u) << Describe(*op);
    EXPECT_EQ(op->Solve(failed[0].uid, SolveAction::Skip), 1);
    EXPECT_EQ(op->State(), OperationState::FinishedWithSkips);
    EXPECT_TRUE(RemoteExists(Path("tree/locked/stuck.txt")));
    ASSERT_TRUE(admin_.Chmod(Path("tree/locked"), 0755)); // so TearDown can clean up
}

TEST_F(OperationDeleteTest, CancelBeforeAnythingRanLeavesTheTreeAlone)
{
    MakeTree();
    auto op = MakeDelete({RemoteSrc(Path("tree"), true)});
    CTestObserver observer;
    op->SetObserver(&observer);
    op->Pause(); // no worker takes an item
    ASSERT_TRUE(op->Start());
    Sleep(200);
    op->RequestCancel();
    EXPECT_TRUE(op->WaitFinished(10000));
    EXPECT_TRUE(op->WasCancelled());
    EXPECT_TRUE(RemoteExists(Path("tree/sub/deep/c.txt")));
}

TEST_F(OperationDeleteTest, CancelInTheMiddleEndsQuicklyAndANewRunFinishesTheJob)
{
    ASSERT_TRUE(admin_.Mkdir(Path("many")));
    for (int i = 0; i < 200; ++i)
        ASSERT_TRUE(admin_.Mkdir(Path("many/d" + std::to_string(i))));
    PutRemoteFile(Path("outside.txt"), "o");

    auto op = MakeDelete({RemoteSrc(Path("many"), true)}, 3);
    CTestObserver observer;
    op->SetObserver(&observer);
    ASSERT_TRUE(op->Start());
    Sleep(100);
    op->RequestCancel();
    EXPECT_TRUE(op->WaitFinished(20000));
    EXPECT_TRUE(RemoteExists(Path("outside.txt"))); // never more than asked
    // a second operation finishes what the first one left
    auto again = MakeDelete({RemoteSrc(Path("many"), true)}, 3);
    CTestObserver observer2;
    ASSERT_TRUE(Run(*again, observer2));
    EXPECT_FALSE(RemoteExists(Path("many")));
}
} // namespace
