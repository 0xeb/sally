// Tests for ops/COperation and CWorker against the Docker test server: whole copy/move runs with real worker threads.
#include "operation_test_support.h"

TEST_F(OperationTest, DownloadsANestedTreeWithContentAndTime)
{
    auto op = MakeDownload({RemoteSrc("fixtures/nested", true), RemoteSrc("fixtures/small", true),
                            RemoteSrc("fixtures/empty", true)});
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));

    EXPECT_EQ(op->State(), OperationState::Finished);
    EXPECT_TRUE(op->IsFinished());
    EXPECT_FALSE(op->WasCancelled());
    EXPECT_EQ(observer.finished.load(), 1);
    EXPECT_GT(observer.items.load(), 0);

    EXPECT_EQ(ReadAll(local_ / "nested" / "a" / "b" / "c" / "deep.txt"), "deep\n");
    EXPECT_EQ(ReadAll(local_ / "small" / "hello.txt"), "hello\n");
    EXPECT_TRUE(fs::is_directory(local_ / "empty"));
    EXPECT_TRUE(fs::is_directory(local_ / "nested" / "a" / "b"));

    for (const char* name : {"random1k.bin", "random1m.bin", "hello.txt"})
    {
        const std::string remote = std::string("fixtures/small/") + name;
        EXPECT_EQ(ReadAll(local_ / "small" / name), GetRemoteFile(remote)) << name;
        CSftpEntry e;
        ASSERT_TRUE(admin_.Stat(remote, e));
        EXPECT_EQ(UnixMtimeOfLocal(local_ / "small" / name), e.mtime) << name;
    }

    const COperationStats stats = op->Statistics();
    EXPECT_EQ(stats.filesTotal, stats.filesDone);
    EXPECT_GE(stats.bytesTotal, 1048576u);
    EXPECT_EQ(stats.bytesTotal, stats.bytesDone);
}

TEST_F(OperationTest, DownloadFollowsSymlinks)
{
    auto op = MakeDownload({RemoteSrc("fixtures/dirlink", true), RemoteSrc("fixtures/filelink", false, 6)});
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished);
    EXPECT_EQ(ReadAll(local_ / "dirlink" / "a" / "b" / "c" / "deep.txt"), "deep\n");
    EXPECT_EQ(ReadAll(local_ / "filelink"), "hello\n");
}

TEST_F(OperationTest, UploadsANestedTreeIntoAMissingFolder)
{
    const fs::path src = local_ / "src" / "tree";
    const std::string big = RandomBytes(700 * 1024, 7);
    WriteAll(src / "a.txt", "alpha\n");
    WriteAll(src / "sub" / "b.bin", big);
    WriteAll(src / "sub" / "deeper" / "c.txt", "gamma\n");
    fs::create_directories(src / "empty");
    const int64_t when = 1577836800; // 2020-01-01
    {
        HANDLE h = CreateFileW((src / "a.txt").c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                               nullptr);
        ASSERT_NE(h, INVALID_HANDLE_VALUE);
        const FILETIME ft = UnixTimeToFileTime(when);
        ASSERT_TRUE(SetFileTime(h, nullptr, nullptr, &ft));
        CloseHandle(h);
    }

    const std::string target = remoteDir_ + "/new/target"; // "new" does not exist yet
    auto op = MakeUpload({LocalSrc(src, true)}, target);
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(observer.finished.load(), 1);

    EXPECT_EQ(GetRemoteFile(target + "/tree/a.txt"), "alpha\n");
    EXPECT_EQ(GetRemoteFile(target + "/tree/sub/b.bin"), big);
    EXPECT_EQ(GetRemoteFile(target + "/tree/sub/deeper/c.txt"), "gamma\n");
    CSftpEntry e;
    ASSERT_TRUE(admin_.Stat(target + "/tree/empty", e));
    EXPECT_TRUE(e.isDir);
    ASSERT_TRUE(admin_.Stat(target + "/tree/a.txt", e));
    EXPECT_EQ(e.mtime, when);
    EXPECT_TRUE(fs::exists(src / "a.txt")); // a copy leaves the source alone
}

TEST_F(OperationTest, UploadSetsPermissionsWhenAsked)
{
    WriteAll(local_ / "src" / "p.txt", "perm\n");
    COperationSettings settings = Settings(TransferDirection::Upload);
    settings.setUploadPermissions = true;
    settings.uploadPermissions = 0600;
    auto op = std::make_unique<COperation>(
        settings, Params(), COperation::BuildUploadRoots({LocalSrc(local_ / "src" / "p.txt", false)}, settings, CNameCodec::Auto()));
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished);
    CSftpEntry e;
    ASSERT_TRUE(admin_.Stat(remoteDir_ + "/p.txt", e));
    EXPECT_EQ(e.permissions & 07777, 0600u);
}

// The call shape of Sally's F5 upload: an absolute remote root (the panel's current path), the source path of the
// enumeration (name appended to a folder with a trailing backslash), a file whose size/time the enumerator reported,
// default site settings (safe upload, time kept, workers 2), policy Ask.
TEST_F(OperationTest, UploadsAFileLikeSallyF5Does)
{
    const std::string content = PatternBytes(3 * 1024 * 1024 + 17, 3);
    WriteAll(local_ / "src" / "big.bin", content);
    std::string absolute;
    ASSERT_TRUE(admin_.RealPath(remoteDir_, absolute)) << admin_.LastError().message;

    std::wstring base = (local_ / "src").wstring();
    base += L'\\';
    CLocalSource source;
    source.path = base + L"big.bin";
    source.isDir = false;
    source.size = content.size();
    source.mtime = 1577836800;

    COperationSettings settings = SettingsFromSite(TestSite(), TransferDirection::Upload, false);
    settings.localRoot = (local_ / "src").wstring(); // no trailing backslash
    settings.remoteRoot = absolute;
    auto op = std::make_unique<COperation>(
        settings, Params(), COperation::BuildUploadRoots({source}, settings, CNameCodec::Auto()));
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer, 30000)) << Describe(*op);
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/big.bin"), content);
}

TEST_F(OperationTest, MoveDownRemovesTheRemoteSources)
{
    ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/tree"));
    ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/tree/sub"));
    ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/tree/emptydir"));
    PutRemoteFile(remoteDir_ + "/tree/one.txt", "one\n");
    PutRemoteFile(remoteDir_ + "/tree/sub/two.txt", "two\n");

    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/tree", true)}, true);
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);

    EXPECT_EQ(ReadAll(local_ / "tree" / "one.txt"), "one\n");
    EXPECT_EQ(ReadAll(local_ / "tree" / "sub" / "two.txt"), "two\n");
    EXPECT_TRUE(fs::is_directory(local_ / "tree" / "emptydir"));
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/tree/one.txt"));
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/tree/sub/two.txt"));
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/tree/sub"));
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/tree/emptydir"));
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/tree"));
}

TEST_F(OperationTest, MoveUpRemovesTheLocalSources)
{
    const fs::path src = local_ / "movesrc" / "tree";
    WriteAll(src / "one.txt", "one\n");
    WriteAll(src / "sub" / "two.txt", "two\n");
    fs::create_directories(src / "emptydir");
    SetFileAttributesW((src / "one.txt").c_str(), FILE_ATTRIBUTE_READONLY); // a read-only file must go too

    auto op = MakeUpload({LocalSrc(src, true)}, remoteDir_, true);
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);

    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/tree/one.txt"), "one\n");
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/tree/sub/two.txt"), "two\n");
    CSftpEntry e;
    EXPECT_TRUE(admin_.Stat(remoteDir_ + "/tree/emptydir", e));
    EXPECT_FALSE(fs::exists(src));
    EXPECT_TRUE(fs::exists(local_ / "movesrc")); // only the selected folder is the source
}

TEST_F(OperationTest, FiveThousandFilesWithFourWorkers)
{
    auto op = MakeDownload({RemoteSrc("fixtures/many", true)}, false, 4);
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer, 180000));
    EXPECT_EQ(op->State(), OperationState::Finished);

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(local_ / "many"))
    {
        (void)entry;
        ++count;
    }
    EXPECT_EQ(count, 5000u);
    EXPECT_EQ(op->Summary().totals.filesDone, 5000);
    EXPECT_EQ(observer.finished.load(), 1);
}

TEST_F(OperationTest, CancelReturnsPromptlyAndJoinsTheThreads)
{
    const DWORD handlesBefore = HandleCount();
    {
        // 5 GB sparse file: the download cannot finish within the test
        auto op = MakeDownload({RemoteSrc("fixtures/sparse5g.bin", false, 5ull << 30)}, false, 2);
        CTestObserver observer;
        op->SetObserver(&observer);
        ASSERT_TRUE(op->Start());
        for (int i = 0; i < 300 && observer.progress.load() == 0; ++i)
            Sleep(20);
        ASSERT_GT(observer.progress.load(), 0) << "the transfer did not start";

        const auto start = std::chrono::steady_clock::now();
        op->RequestCancel();
        EXPECT_TRUE(op->WaitFinished(10000));
        EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));

        EXPECT_TRUE(op->WasCancelled());
        EXPECT_EQ(observer.finished.load(), 1);
        EXPECT_EQ(op->State(), OperationState::Running); // the item went back to Waiting, it was not lost
        const std::vector<CQueueItem> items = op->Queue().Items(false);
        ASSERT_EQ(items.size(), 1u);
        EXPECT_EQ(items[0].state, ItemState::Waiting);
        EXPECT_GT(items[0].bytesDone, 0u); // the partial size is kept for a later resume
        EXPECT_TRUE(fs::exists(local_ / "sparse5g.bin"));
    }
    // sockets, threads and events are closed again (a little slack for lazily created process-wide handles)
    EXPECT_LE(HandleCount(), handlesBefore + 2);
}

TEST_F(OperationTest, DoesNotLeakHandles)
{
    policy_ = ConflictPolicy::Overwrite; // the same folder is filled four times
    auto runOnce = [this]
    {
        auto op = MakeDownload({RemoteSrc("fixtures/small", true)});
        CTestObserver observer;
        EXPECT_TRUE(Run(*op, observer));
        EXPECT_EQ(op->State(), OperationState::Finished);
    };
    runOnce(); // the first run creates process-wide state
    const DWORD before = HandleCount();
    for (int i = 0; i < 3; ++i)
        runOnce();
    EXPECT_LE(HandleCount(), before + 2);
}

TEST_F(OperationTest, ParksANameThatIsNotAValidWindowsName)
{
    ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/names"));
    PutRemoteFile(remoteDir_ + "/names/good.txt", "good\n");
    PutRemoteFile(remoteDir_ + "/names/a:b", "bad\n");
    ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/names/conx.d"));
    ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/names/aux")); // reserved device name, a directory this time
    PutRemoteFile(remoteDir_ + "/names/aux/inside.txt", "inside\n");

    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/names", true)});
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));

    EXPECT_EQ(op->State(), OperationState::NeedsUser);
    EXPECT_EQ(ReadAll(local_ / "names" / "good.txt"), "good\n");
    EXPECT_TRUE(fs::is_directory(local_ / "names" / "conx.d"));
    EXPECT_FALSE(fs::exists(local_ / "names" / "aux"));

    const std::vector<CQueueItem> parked = ItemsWithProblem(*op, Problem::InvalidLocalName);
    ASSERT_EQ(parked.size(), 2u);
    for (const CQueueItem& item : parked)
        EXPECT_EQ(item.state, ItemState::UserInputNeeded);
    const CQueueSummary summary = op->Summary();
    EXPECT_EQ(summary.counters.userInput, 2);
    EXPECT_EQ(summary.totals.filesDone, 1);

    // the user skips both: nothing is left, the run counts as "finished with skips"
    EXPECT_EQ(op->Queue().Solve(parked[0].uid, SolveAction::Skip, L"", true), 2);
    EXPECT_EQ(op->State(), OperationState::FinishedWithSkips);
}

TEST_F(OperationTest, AMissingSourceFailsOnlyThatItem)
{
    PutRemoteFile(remoteDir_ + "/there.txt", "x\n");
    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/there.txt", false, 2), RemoteSrc(remoteDir_ + "/gone.txt", false, 5)});
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::FinishedWithErrors);
    EXPECT_EQ(ReadAll(local_ / "there.txt"), "x\n");
    const std::vector<CQueueItem> failed = ItemsWithProblem(*op, Problem::NotFound);
    ASSERT_EQ(failed.size(), 1u);
    EXPECT_EQ(failed[0].state, ItemState::Failed);
    EXPECT_FALSE(failed[0].errorText.empty());
}

TEST_F(OperationTest, AMissingLocalSourceFailsOnlyThatItem)
{
    WriteAll(local_ / "src" / "here.txt", "x\n");
    auto op = MakeUpload({LocalSrc(local_ / "src" / "here.txt", false), LocalSrc(local_ / "src" / "nope.txt", false)},
                         remoteDir_);
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::FinishedWithErrors);
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/here.txt"), "x\n");
    EXPECT_EQ(ItemsWithProblem(*op, Problem::NotFound).size(), 1u);
}

TEST_F(OperationTest, TooDeepExploreItemsAreTakenForALoop)
{
    CQueueItem deep;
    deep.type = ItemType::ExploreRemote;
    deep.remotePath = "fixtures/small";
    deep.localPath = (local_ / "deep").wstring();
    deep.depth = kMaxExploreDepth + 1;
    COperation op(Settings(TransferDirection::Download), Params(), {deep});
    CTestObserver observer;
    ASSERT_TRUE(Run(op, observer));
    EXPECT_EQ(op.State(), OperationState::FinishedWithErrors);
    EXPECT_EQ(ItemsWithProblem(op, Problem::SymlinkLoop).size(), 1u);
}

TEST_F(OperationTest, StartNeedsValidParamsAndWorksOnce)
{
    COperation bad(Settings(TransferDirection::Download), CPanelConnectParams(), {});
    EXPECT_FALSE(bad.Start());

    auto op = MakeDownload({});
    CTestObserver observer;
    op->SetObserver(&observer);
    EXPECT_TRUE(op->Start());
    EXPECT_FALSE(op->Start());
    EXPECT_TRUE(op->WaitFinished(20000));
    EXPECT_EQ(op->State(), OperationState::Finished); // an empty queue is finished at once
    EXPECT_EQ(observer.finished.load(), 1);
}

TEST_F(OperationTest, ARejectedHostKeyFailsTheItemsWithConnectionLost)
{
    CPanelConnectParams params = Params();
    params.hostKeyFingerprint = "SHA256:not-the-key";
    COperationSettings settings = Settings(TransferDirection::Download, false, 1);
    settings.interactive = false; // nobody answers: the key is rejected
    COperation op(settings, std::move(params),
                  COperation::BuildDownloadRoots({RemoteSrc("fixtures/small/hello.txt", false, 6)}, settings,
                                                 CNameCodec::Auto()));
    CTestObserver observer;
    ASSERT_TRUE(Run(op, observer));
    EXPECT_EQ(op.State(), OperationState::FinishedWithErrors);
    EXPECT_EQ(ItemsWithProblem(op, Problem::ConnectionLost).size(), 1u);
    EXPECT_FALSE(fs::exists(local_ / "hello.txt"));
}

TEST_F(OperationTest, DestroyingARunningOperationJoinsEverything)
{
    const DWORD before = HandleCount();
    for (int i = 0; i < 3; ++i)
    {
        auto op = MakeDownload({RemoteSrc("fixtures/many", true)}, false, 4);
        CTestObserver observer;
        op->SetObserver(&observer);
        ASSERT_TRUE(op->Start());
        Sleep(150); // workers are somewhere in connect/explore/transfer
    }                // ~COperation cancels and joins
    EXPECT_LE(HandleCount(), before + 2);
}

// ---- links and loops -----------------------------------------------------------------------------------------------

TEST_F(OperationTest, MovingADirectoryLinkRemovesTheLinkAndKeepsWhatItPointsTo)
{
    ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/dirA"));
    ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/dirB"));
    PutRemoteFile(remoteDir_ + "/dirB/file.txt", "kept\n");
    PutRemoteFile(remoteDir_ + "/dirA/own.txt", "own\n");
    ASSERT_TRUE(admin_.Symlink("../dirB", remoteDir_ + "/dirA/link")) << admin_.LastError().message;

    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/dirA", true)}, true);
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);

    EXPECT_EQ(ReadAll(local_ / "dirA" / "link" / "file.txt"), "kept\n"); // transferred through the link
    EXPECT_EQ(ReadAll(local_ / "dirA" / "own.txt"), "own\n");
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/dirB/file.txt"), "kept\n");    // the target of the link is untouched
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/dirA/link"));
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/dirA/own.txt"));
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/dirA"));
}

TEST_F(OperationTest, MovingUpAJunctionRemovesTheJunctionAndKeepsWhatItPointsTo)
{
    WriteAll(local_ / "src" / "tree" / "own.txt", "own\n");
    WriteAll(local_ / "other" / "inner.txt", "kept\n");
    if (!MakeJunction(local_ / "src" / "tree" / "jl", local_ / "other"))
        GTEST_SKIP() << "cannot create a junction here";

    auto op = MakeUpload({LocalSrc(local_ / "src" / "tree", true)}, remoteDir_ + "/dest", true);
    CTestObserver observer;
    ASSERT_TRUE(Run(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);

    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/dest/tree/jl/inner.txt"), "kept\n");
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/dest/tree/own.txt"), "own\n");
    EXPECT_EQ(ReadAll(local_ / "other" / "inner.txt"), "kept\n"); // not deleted
    EXPECT_FALSE(fs::exists(local_ / "src" / "tree"));            // own file, the junction and the folder are gone
}

TEST_F(OperationTest, AJunctionToAnAncestorFailsTheLoopItemOnly)
{
    WriteAll(local_ / "src" / "tree" / "a.txt", "a\n");
    const fs::path junction = local_ / "src" / "tree" / "back";
    if (!MakeJunction(junction, local_ / "src" / "tree"))
        GTEST_SKIP() << "cannot create a junction here";

    auto op = MakeUpload({LocalSrc(local_ / "src" / "tree", true)}, remoteDir_ + "/dest");
    CTestObserver observer;
    const bool ran = Run(*op, observer);
    RemoveDirectoryW(junction.c_str()); // before anything can walk the loop (the test folder is deleted afterwards)
    ASSERT_TRUE(ran);

    EXPECT_EQ(op->State(), OperationState::FinishedWithErrors);
    EXPECT_EQ(ItemsWithProblem(*op, Problem::SymlinkLoop).size(), 1u) << Describe(*op);
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/dest/tree/a.txt"), "a\n");
}

// ---- roots of an upload (no server needed) ---------------------------------------------------------------------

namespace
{
std::vector<CQueueItem> UploadRootsFor(const CNameCodec& codec, const std::wstring& path, const std::string& remoteRoot)
{
    CLocalSource source;
    source.path = path;
    source.isDir = false;
    source.size = 2159;
    COperationSettings settings;
    settings.direction = TransferDirection::Upload;
    settings.remoteRoot = remoteRoot;
    return COperation::BuildUploadRoots({source}, settings, codec);
}
} // namespace

TEST(UploadRoots, FileItemTargetsTheNameInsideTheRemoteRootForEveryCodec)
{
    const CNameCodec codecs[] = {CNameCodec::Auto(), CNameCodec::Utf8(), CNameCodec::CodePage(1252),
                                 CNameCodec::CodePage(0), CNameCodec::CodePage(65001), CNameCodec()};
    for (const CNameCodec& codec : codecs)
    {
        const auto roots = UploadRootsFor(codec, LR"(C:\temp\kc_logs.txt)", "/home/test/upload");
        ASSERT_EQ(roots.size(), 1u);
        EXPECT_EQ(roots[0].remotePath, "/home/test/upload/kc_logs.txt");
    }
}

TEST(UploadRoots, AnUnusableCodePageNeverTargetsTheParentFolder)
{
    // 1 is not a code page WideCharToMultiByte knows: the encoding fails and must not give an empty name
    const auto roots = UploadRootsFor(CNameCodec::CodePage(1), LR"(C:\temp\kc_logs.txt)", "/home/test/upload");
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_TRUE(roots[0].remotePath.empty() || roots[0].remotePath == "/home/test/upload/kc_logs.txt");
    EXPECT_NE(roots[0].remotePath, "/home/test/upload");
}

// Sally hands the upload target to the plugin as "path<NUL>mask"; a NUL that survives into the remote root once made
// every file item address "/home/test/upload" itself (the trace printed the path cut at the NUL).
TEST(UploadRoots, AFolderWithANulNeverBecomesAPath)
{
    const std::string withNul = std::string("/home/test/upload") + '\0';
    auto roots = UploadRootsFor(CNameCodec::Auto(), LR"(C:\temp\kc_logs.txt)", withNul);
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_TRUE(roots[0].remotePath.empty());
}

// ---- guard of the upload target (ResolveRemoteTarget is private in CWorker: tested through a whole operation) ----

// An upload item whose remote path is empty or is its own parent folder can only come from a bug; the worker must park
// it with InvalidRemoteName and never write there.
TEST_F(OperationTest, UploadItemAddressingItsParentFolderFailsWithInvalidRemoteName)
{
    WriteAll(local_ / "src" / "guard.txt", "guard\n");
    const COperationSettings settings = Settings(TransferDirection::Upload);

    CQueueItem item;
    item.type = ItemType::Upload;
    item.localPath = (local_ / "src" / "guard.txt").wstring();
    item.remotePath = remoteDir_; // the folder itself
    item.parentRemote = remoteDir_;
    COperation op(settings, Params(), {item});
    CTestObserver observer;
    ASSERT_TRUE(Run(op, observer)) << Describe(op);

    EXPECT_EQ(ItemsWithProblem(op, Problem::InvalidRemoteName).size(), 1u) << Describe(op);
    CSftpEntry e;
    ASSERT_TRUE(admin_.Stat(remoteDir_, e));
    EXPECT_TRUE(e.isDir); // still the folder, nothing replaced it
}

TEST_F(OperationTest, UploadItemWithAnEmptyRemoteNameFailsWithInvalidRemoteName)
{
    WriteAll(local_ / "src" / "guard.txt", "guard\n");
    const COperationSettings settings = Settings(TransferDirection::Upload);

    CQueueItem item;
    item.type = ItemType::Upload;
    item.localPath = (local_ / "src" / "guard.txt").wstring();
    item.parentRemote = remoteDir_; // remotePath stays empty
    COperation op(settings, Params(), {item});
    CTestObserver observer;
    ASSERT_TRUE(Run(op, observer)) << Describe(op);

    EXPECT_EQ(ItemsWithProblem(op, Problem::InvalidRemoteName).size(), 1u) << Describe(op);
}

// ---- an upload whose final rename was sent and its answer lost (found by the stress test, M7) --------------------------
// A pause or a dropped connection at the moment of the rename can leave the finished file on the server while the item goes
// back to Waiting. The retry must recognise it and not ask "the target exists" about the file it has just uploaded.

namespace
{
CQueueItem UploadItemFor(const fs::path& local, const std::string& remoteDir, const std::string& name, bool renameSent)
{
    CQueueItem item;
    item.type = ItemType::Upload;
    item.localPath = local.wstring();
    item.remotePath = remoteDir + "/" + name;
    item.parentRemote = remoteDir;
    item.renameSent = renameSent;
    return item;
}
} // namespace

TEST_F(OperationTest, ARetryAfterALostRenameAnswerFindsTheFinishedFileAndIsDone)
{
    const std::string content = PatternBytes(300 * 1024 + 5, 31);
    WriteAll(local_ / "src" / "done.bin", content);
    PutRemoteFile(remoteDir_ + "/done.bin", content); // what the server did before the answer got lost

    COperation op(Settings(TransferDirection::Upload), Params(),
                  {UploadItemFor(local_ / "src" / "done.bin", remoteDir_, "done.bin", true)});
    CTestObserver observer;
    ASSERT_TRUE(Run(op, observer)) << Describe(op);
    EXPECT_EQ(op.State(), OperationState::Finished) << Describe(op);
    EXPECT_TRUE(ItemsWithProblem(op, Problem::TargetExists).empty());
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/done.bin"), content);
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/done.bin.filepart"));
}

TEST_F(OperationTest, ARetryAfterALostRenameAnswerStillDeletesTheSourceOfAMove)
{
    const std::string content = PatternBytes(64 * 1024, 32);
    WriteAll(local_ / "src" / "moved.bin", content);
    PutRemoteFile(remoteDir_ + "/moved.bin", content);

    COperation op(Settings(TransferDirection::Upload, true), Params(),
                  {UploadItemFor(local_ / "src" / "moved.bin", remoteDir_, "moved.bin", true)});
    CTestObserver observer;
    ASSERT_TRUE(Run(op, observer)) << Describe(op);
    EXPECT_EQ(op.State(), OperationState::Finished) << Describe(op);
    EXPECT_FALSE(fs::exists(local_ / "src" / "moved.bin")); // a move is finished only when the source is gone
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/moved.bin"), content);
}

TEST_F(OperationTest, AFullSizeTargetWithoutAnySignOfOurRenameStillAsksAboutTheConflict)
{
    const std::string content = PatternBytes(64 * 1024, 33);
    WriteAll(local_ / "src" / "other.bin", content);
    PutRemoteFile(remoteDir_ + "/other.bin", PatternBytes(64 * 1024, 34)); // same size, somebody else's content

    COperation op(Settings(TransferDirection::Upload), Params(),
                  {UploadItemFor(local_ / "src" / "other.bin", remoteDir_, "other.bin", false)});
    CTestObserver observer;
    ASSERT_TRUE(Run(op, observer)) << Describe(op);
    EXPECT_EQ(ItemsWithProblem(op, Problem::TargetExists).size(), 1u) << Describe(op);
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/other.bin"), PatternBytes(64 * 1024, 34)); // untouched
}

TEST_F(OperationTest, ALostRenameAnswerIsNotTrustedWhenTheTargetHasAnotherSizeOrAPartialIsStillThere)
{
    const std::string content = PatternBytes(64 * 1024, 35);
    WriteAll(local_ / "src" / "size.bin", content);
    PutRemoteFile(remoteDir_ + "/size.bin", content.substr(0, 1000)); // another size: not our finished file
    WriteAll(local_ / "src" / "part.bin", content);
    PutRemoteFile(remoteDir_ + "/part.bin", content);
    PutRemoteFile(remoteDir_ + "/part.bin.filepart", content.substr(0, 500)); // the rename did not happen: a partial is left

    COperation op(Settings(TransferDirection::Upload), Params(),
                  {UploadItemFor(local_ / "src" / "size.bin", remoteDir_, "size.bin", true),
                   UploadItemFor(local_ / "src" / "part.bin", remoteDir_, "part.bin", true)});
    CTestObserver observer;
    ASSERT_TRUE(Run(op, observer)) << Describe(op);
    EXPECT_EQ(ItemsWithProblem(op, Problem::TargetExists).size(), 2u) << Describe(op);
}
