// Tests for the chmod operation (COperation with TransferDirection::Chmod): recursive trees with separate rules for files
// and directories, links that must stay alone, items the server refuses, cancel. Docker test server.
#include "operation_test_support.h"

#include "file_mode.h"
#include "solve_offers.h"

namespace
{
// ---- root items (pure, no server) -----------------------------------------------------------------------------------

COperationSettings ChmodSettings(bool recursive, bool files, bool dirs)
{
    COperationSettings settings;
    settings.direction = TransferDirection::Chmod;
    settings.chmodRecursive = recursive;
    settings.chmodFiles = files;
    settings.chmodDirs = dirs;
    return settings;
}

CRemoteSource Source(const std::string& path, bool isDir)
{
    CRemoteSource source;
    source.path = path;
    source.isDir = isDir;
    return source;
}

TEST(ChmodRoots, WithoutRecursionEverySourceGetsTheChange)
{
    const std::vector<CQueueItem> roots = COperation::BuildChmodRoots(
        {Source("/d/dir/", true), Source("/d/file", false)}, ChmodSettings(false, true, true));
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0].type, ItemType::SetAttrs);
    EXPECT_EQ(roots[0].remotePath, "/d/dir"); // no trailing slash
    EXPECT_EQ(roots[1].type, ItemType::SetAttrs);
}

TEST(ChmodRoots, RecursionExploresDirectoriesAndChangesFiles)
{
    const std::vector<CQueueItem> roots = COperation::BuildChmodRoots(
        {Source("/d/dir", true), Source("/d/file", false)}, ChmodSettings(true, true, true));
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0].type, ItemType::ExploreRemote);
    EXPECT_EQ(roots[1].type, ItemType::SetAttrs);
}

TEST(ChmodRoots, RecursiveDirectoriesOnlyLeavesSelectedFilesOut)
{
    const std::vector<CQueueItem> roots = COperation::BuildChmodRoots(
        {Source("/d/dir", true), Source("/d/file", false)}, ChmodSettings(true, false, true));
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].type, ItemType::ExploreRemote);
}

// ---- against the server ---------------------------------------------------------------------------------------------

class OperationChmodTest : public OperationTest
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
        for (const char* dir : {"tree", "tree/sub", "tree/sub/deep", "tree/empty"})
            ASSERT_TRUE(admin_.Chmod(Path(dir), 0755));
        for (const char* file : {"tree/a.txt", "tree/sub/b.txt", "tree/sub/deep/c.txt"})
            ASSERT_TRUE(admin_.Chmod(Path(file), 0644));
    }

    // The permission bits (07777) of a remote item, links followed; 0xFFFF when it cannot be read.
    uint32_t ModeOf(const std::string& path)
    {
        CSftpEntry entry;
        if (!admin_.Stat(path, entry))
            return 0xFFFF;
        return entry.permissions & kModeAll;
    }

    // The change "set exactly this mode" (what typing an octal value in the dialog gives).
    static CModeChange Exactly(uint32_t mode) { return ChangeOfState(StateOfMode(mode)); }

    // Waits for the operation to end.
    bool RunOk(COperation& op, CTestObserver& observer) { return Run(op, observer); }
};

TEST_F(OperationChmodTest, FilesAndDirectoriesGetTheirOwnModes)
{
    MakeTree();
    {
        auto op = MakeChmod({RemoteSrc(Path("tree"), true)}, Exactly(0640), true, true, false); // files only
        CTestObserver observer;
        ASSERT_TRUE(RunOk(*op, observer));
        EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
        EXPECT_EQ(op->Statistics().filesTotal, 3); // the three files
        EXPECT_EQ(op->Statistics().filesDone, 3);
    }
    for (const char* file : {"tree/a.txt", "tree/sub/b.txt", "tree/sub/deep/c.txt"})
        EXPECT_EQ(ModeOf(Path(file)), 0640u) << file;
    for (const char* dir : {"tree", "tree/sub", "tree/sub/deep", "tree/empty"})
        EXPECT_EQ(ModeOf(Path(dir)), 0755u) << dir; // not asked for

    {
        auto op = MakeChmod({RemoteSrc(Path("tree"), true)}, Exactly(0750), true, false, true); // directories only
        CTestObserver observer;
        ASSERT_TRUE(RunOk(*op, observer));
        EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
        EXPECT_EQ(op->Statistics().filesTotal, 4); // the four directories, the selected one included
        EXPECT_EQ(op->Statistics().filesDone, 4);
    }
    for (const char* dir : {"tree", "tree/sub", "tree/sub/deep", "tree/empty"})
        EXPECT_EQ(ModeOf(Path(dir)), 0750u) << dir;
    for (const char* file : {"tree/a.txt", "tree/sub/b.txt", "tree/sub/deep/c.txt"})
        EXPECT_EQ(ModeOf(Path(file)), 0640u) << file; // still what the first run set
}

TEST_F(OperationChmodTest, FilesAndDirectoriesInOneRun)
{
    MakeTree();
    auto op = MakeChmod({RemoteSrc(Path("tree"), true)}, Exactly(0700), true);
    CTestObserver observer;
    ASSERT_TRUE(RunOk(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(op->Statistics().filesTotal, 7);
    EXPECT_EQ(op->Statistics().filesDone, 7);
    for (const char* path : {"tree", "tree/sub", "tree/sub/deep", "tree/empty", "tree/a.txt", "tree/sub/b.txt",
                             "tree/sub/deep/c.txt"})
        EXPECT_EQ(ModeOf(Path(path)), 0700u) << path;
}

TEST_F(OperationChmodTest, WithoutRecursionOnlyTheSelectedItemsChange)
{
    MakeTree();
    PutRemoteFile(Path("single.txt"), "x");
    ASSERT_TRUE(admin_.Chmod(Path("single.txt"), 0644));
    auto op = MakeChmod({RemoteSrc(Path("tree"), true), RemoteSrc(Path("single.txt"), false)}, Exactly(0711), false);
    CTestObserver observer;
    ASSERT_TRUE(RunOk(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(ModeOf(Path("tree")), 0711u);
    EXPECT_EQ(ModeOf(Path("single.txt")), 0711u);
    EXPECT_EQ(ModeOf(Path("tree/a.txt")), 0644u); // below a selected directory: untouched
    EXPECT_EQ(ModeOf(Path("tree/sub")), 0755u);
}

TEST_F(OperationChmodTest, OnlyTheChosenBitsChangeAndTheOthersStayAsEachItemHasThem)
{
    ASSERT_TRUE(admin_.Mkdir(Path("tree")));
    PutRemoteFile(Path("tree/x.txt"), "x");
    PutRemoteFile(Path("tree/y.txt"), "y");
    ASSERT_TRUE(admin_.Chmod(Path("tree/x.txt"), 0640));
    ASSERT_TRUE(admin_.Chmod(Path("tree/y.txt"), 0604));
    CModeChange change; // "others: no read", nothing else
    change.clearBits = 0004;
    auto op = MakeChmod({RemoteSrc(Path("tree"), true)}, change, true, true, false);
    CTestObserver observer;
    ASSERT_TRUE(RunOk(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(ModeOf(Path("tree/x.txt")), 0640u);
    EXPECT_EQ(ModeOf(Path("tree/y.txt")), 0600u);
}

TEST_F(OperationChmodTest, SpecialBitsAreKeptWhenTheChangeDoesNotMentionThem)
{
    ASSERT_TRUE(admin_.Mkdir(Path("tree")));
    ASSERT_TRUE(admin_.Chmod(Path("tree"), 01755)); // sticky
    CModeChange change;                             // only the owner write bit is decided
    change.setBits = 0200;
    auto op = MakeChmod({RemoteSrc(Path("tree"), true)}, change, false);
    CTestObserver observer;
    ASSERT_TRUE(RunOk(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(ModeOf(Path("tree")), 01755u);

    auto clear = MakeChmod({RemoteSrc(Path("tree"), true)}, Exactly(0755), false); // typed 0755: sticky goes
    CTestObserver observer2;
    ASSERT_TRUE(RunOk(*clear, observer2));
    EXPECT_EQ(ModeOf(Path("tree")), 0755u);
}

TEST_F(OperationChmodTest, ALinkInsideTheTreeIsSkippedAndItsTargetKeepsItsMode)
{
    ASSERT_TRUE(admin_.Mkdir(Path("keep")));
    PutRemoteFile(Path("keep/precious.txt"), "precious");
    PutRemoteFile(Path("outside.txt"), "o");
    ASSERT_TRUE(admin_.Chmod(Path("keep"), 0755));
    ASSERT_TRUE(admin_.Chmod(Path("keep/precious.txt"), 0644));
    ASSERT_TRUE(admin_.Chmod(Path("outside.txt"), 0644));
    MakeTree();
    ASSERT_TRUE(admin_.Symlink("../../keep", Path("tree/sub/link"))) << admin_.LastError().message;
    ASSERT_TRUE(admin_.Symlink("../outside.txt", Path("tree/filelink"))) << admin_.LastError().message;

    auto op = MakeChmod({RemoteSrc(Path("tree"), true)}, Exactly(0700), true);
    CTestObserver observer;
    ASSERT_TRUE(RunOk(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(ModeOf(Path("keep")), 0755u);
    EXPECT_EQ(ModeOf(Path("keep/precious.txt")), 0644u);
    EXPECT_EQ(ModeOf(Path("outside.txt")), 0644u);
    EXPECT_EQ(ModeOf(Path("tree/a.txt")), 0700u); // the rest of the tree did change
    EXPECT_EQ(op->Statistics().filesTotal, 7);     // links are not items at all
}

TEST_F(OperationChmodTest, ARootLinkToADirectoryIsSkippedEvenWhenListedAsADirectory)
{
    ASSERT_TRUE(admin_.Mkdir(Path("keep")));
    PutRemoteFile(Path("keep/precious.txt"), "precious");
    ASSERT_TRUE(admin_.Chmod(Path("keep"), 0755));
    ASSERT_TRUE(admin_.Chmod(Path("keep/precious.txt"), 0644));
    ASSERT_TRUE(admin_.Symlink("keep", Path("dirlink"))) << admin_.LastError().message;

    auto op = MakeChmod({RemoteSrc(Path("dirlink"), true)}, Exactly(0700), true); // listed as a directory
    CTestObserver observer;
    ASSERT_TRUE(RunOk(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(ModeOf(Path("keep")), 0755u);
    EXPECT_EQ(ModeOf(Path("keep/precious.txt")), 0644u);
}

TEST_F(OperationChmodTest, ASelectedLinkChangesWhatItPointsTo)
{
    // chmod(1) semantics for a link the user selected on purpose (inside a tree links are skipped, see above)
    PutRemoteFile(Path("target.txt"), "t");
    ASSERT_TRUE(admin_.Chmod(Path("target.txt"), 0644));
    ASSERT_TRUE(admin_.Symlink("target.txt", Path("link.txt"))) << admin_.LastError().message;
    auto op = MakeChmod({RemoteSrc(Path("link.txt"), false)}, Exactly(0600), true);
    CTestObserver observer;
    ASSERT_TRUE(RunOk(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(ModeOf(Path("target.txt")), 0600u);
}

TEST_F(OperationChmodTest, AnItemTheServerRefusesParksAndTheRestContinues)
{
    PutRemoteFile(Path("mine.txt"), "m");
    ASSERT_TRUE(admin_.Chmod(Path("mine.txt"), 0644));
    // /etc/passwd belongs to root in the test container: the test user may read it but not change its mode. The change
    // (others execute on) differs from its mode, so the worker really tries to write it. If the server is not like
    // that, the case is skipped rather than risking a change.
    CSftpEntry passwd;
    ASSERT_TRUE(admin_.Stat("/etc/passwd", passwd));
    if ((passwd.permissions & 0001) != 0)
        GTEST_SKIP() << "/etc/passwd of the test server has an unexpected mode";
    CModeChange change;
    change.setBits = 0001;

    auto op = MakeChmod({RemoteSrc(Path("mine.txt"), false), RemoteSrc("/etc/passwd", false)}, change, false);
    CTestObserver observer;
    ASSERT_TRUE(RunOk(*op, observer));
    EXPECT_EQ(op->State(), OperationState::FinishedWithErrors) << Describe(*op);
    EXPECT_EQ(ModeOf(Path("mine.txt")), 0645u); // the other item was not held up (0644 + others execute)

    const std::vector<CQueueItem> failed = ItemsWithProblem(*op, Problem::PermissionDenied);
    ASSERT_EQ(failed.size(), 1u) << Describe(*op);
    EXPECT_EQ(failed[0].state, ItemState::Failed);
    EXPECT_EQ(failed[0].type, ItemType::SetAttrs);
    const CSolveOffers offers = SolveOffersFor(failed[0].problem, failed[0].type);
    EXPECT_TRUE(offers.retry && offers.skip);
    EXPECT_FALSE(offers.overwrite || offers.resume || offers.rename);

    EXPECT_EQ(op->Solve(failed[0].uid, SolveAction::Skip), 1);
    EXPECT_EQ(op->State(), OperationState::FinishedWithSkips);
}

TEST_F(OperationChmodTest, ADirectoryThatCannotBeListedParksItsExploreItem)
{
    ASSERT_TRUE(admin_.Mkdir(Path("tree")));
    ASSERT_TRUE(admin_.Mkdir(Path("tree/locked")));
    PutRemoteFile(Path("tree/locked/inner.txt"), "i");
    PutRemoteFile(Path("tree/free.txt"), "f");
    ASSERT_TRUE(admin_.Chmod(Path("tree/free.txt"), 0644));
    ASSERT_TRUE(admin_.Chmod(Path("tree/locked/inner.txt"), 0644));
    ASSERT_TRUE(admin_.Chmod(Path("tree/locked"), 0300)); // can enter and write, cannot list

    auto op = MakeChmod({RemoteSrc(Path("tree"), true)}, Exactly(0600), true, true, false);
    CTestObserver observer;
    ASSERT_TRUE(RunOk(*op, observer));
    EXPECT_NE(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(ModeOf(Path("tree/free.txt")), 0600u); // the rest was done
    const std::vector<CQueueItem> failed = ItemsWithProblem(*op, Problem::PermissionDenied);
    ASSERT_EQ(failed.size(), 1u) << Describe(*op);
    EXPECT_EQ(failed[0].type, ItemType::ExploreRemote);
    ASSERT_TRUE(admin_.Chmod(Path("tree/locked"), 0755)); // so TearDown can clean up
}

TEST_F(OperationChmodTest, AModeThatIsAlreadyRightIsDoneWithoutAChange)
{
    MakeTree();
    auto op = MakeChmod({RemoteSrc(Path("tree"), true)}, Exactly(0644), true, true, false);
    CTestObserver observer;
    ASSERT_TRUE(RunOk(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(op->Statistics().filesDone, 3);
}

TEST_F(OperationChmodTest, CancelInTheMiddleEndsQuicklyAndANewRunFinishesTheJob)
{
    ASSERT_TRUE(admin_.Mkdir(Path("many")));
    for (int i = 0; i < 200; ++i)
        ASSERT_TRUE(admin_.Mkdir(Path("many/d" + std::to_string(i))));
    PutRemoteFile(Path("outside.txt"), "o");
    ASSERT_TRUE(admin_.Chmod(Path("outside.txt"), 0644));

    auto op = MakeChmod({RemoteSrc(Path("many"), true)}, Exactly(0711), true, true, true, 3);
    CTestObserver observer;
    op->SetObserver(&observer);
    ASSERT_TRUE(op->Start());
    Sleep(100);
    op->RequestCancel();
    EXPECT_TRUE(op->WaitFinished(20000));
    EXPECT_EQ(ModeOf(Path("outside.txt")), 0644u); // never more than asked

    auto again = MakeChmod({RemoteSrc(Path("many"), true)}, Exactly(0711), true, true, true, 3);
    CTestObserver observer2;
    ASSERT_TRUE(RunOk(*again, observer2));
    EXPECT_EQ(again->State(), OperationState::Finished) << Describe(*again);
    for (int i = 0; i < 200; ++i)
        ASSERT_EQ(ModeOf(Path("many/d" + std::to_string(i))), 0711u) << i;
    EXPECT_EQ(ModeOf(Path("many")), 0711u);
}
} // namespace
