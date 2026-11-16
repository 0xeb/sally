// Tests for the conflict handling of COperation workers: every policy for downloads and uploads, Ask -> parked -> Solve,
// renaming, safe upload and resume. Docker test server, one folder per test.
#include "operation_test_support.h"

namespace
{
class OperationConflictTest : public OperationTest
{
protected:
    // The source file of the policy tables, always 20 bytes.
    static std::string Source() { return "0123456789ABCDEFGHIJ"; }

    // Names of the entries in the test folder on the server.
    std::vector<std::string> RemoteNames()
    {
        std::vector<std::string> names;
        std::vector<CSftpEntry> entries;
        EXPECT_TRUE(admin_.ListDir(remoteDir_, entries)) << admin_.LastError().message;
        for (const CSftpEntry& entry : entries)
            names.push_back(entry.rawName);
        return names;
    }

    bool HasFilepart()
    {
        for (const std::string& name : RemoteNames())
            if (IsSafeUploadName(name))
                return true;
        return false;
    }

    // Runs the operation until it has nothing left (a parked item counts as "nothing left").
    bool RunToEnd(COperation& op, CTestObserver& observer) { return Run(op, observer, 60000); }

    // Waits for the next OnFinished (a solved item makes a new run).
    static bool WaitForRun(CTestObserver& observer, int runNumber)
    {
        return WaitUntil([&] { return observer.finished.load() >= runNumber; }, 30000);
    }
};

// ---- the policy tables ---------------------------------------------------------------------------------------------

enum class Existing
{
    Missing,
    SameSize, // 20 bytes, other content
    Smaller,  // 10 bytes, "ZZZZZZZZZZ": a corrupt prefix, so a resume is visible in the result
    Larger,   // 30 bytes
    SameSizeOld,
    SameSizeNew
};

struct CPolicyCase
{
    ConflictPolicy policy;
    Existing existing;
    const char* expectedContent; // of the original name afterwards
    OperationState expectedState;
    const char* renamedContent;  // content of "<name> (2).txt", nullptr = must not exist
};

constexpr int64_t kLongAgo = 946684800;   // 2000-01-01
constexpr int64_t kFarFuture = 4000000000; // 2096, still inside the 32-bit time of the protocol

std::string ContentOf(Existing existing)
{
    switch (existing)
    {
    case Existing::Smaller:
        return "ZZZZZZZZZZ";
    case Existing::Larger:
        return std::string(30, 'y');
    default:
        return std::string(20, 'x');
    }
}

int64_t ExistingMtime(Existing existing)
{
    return existing == Existing::SameSizeOld ? kLongAgo : existing == Existing::SameSizeNew ? kFarFuture : 0;
}

// The table: policy x state of the target -> result. The source is "0123456789ABCDEFGHIJ" and "now" old.
const CPolicyCase kCases[] = {
    {ConflictPolicy::Ask, Existing::Missing, "0123456789ABCDEFGHIJ", OperationState::Finished, nullptr},
    {ConflictPolicy::Ask, Existing::SameSize, "xxxxxxxxxxxxxxxxxxxx", OperationState::NeedsUser, nullptr},
    {ConflictPolicy::Overwrite, Existing::Missing, "0123456789ABCDEFGHIJ", OperationState::Finished, nullptr},
    {ConflictPolicy::Overwrite, Existing::SameSize, "0123456789ABCDEFGHIJ", OperationState::Finished, nullptr},
    {ConflictPolicy::Overwrite, Existing::Smaller, "0123456789ABCDEFGHIJ", OperationState::Finished, nullptr},
    {ConflictPolicy::Overwrite, Existing::Larger, "0123456789ABCDEFGHIJ", OperationState::Finished, nullptr},
    {ConflictPolicy::Skip, Existing::Missing, "0123456789ABCDEFGHIJ", OperationState::Finished, nullptr},
    {ConflictPolicy::Skip, Existing::SameSize, "xxxxxxxxxxxxxxxxxxxx", OperationState::FinishedWithSkips, nullptr},
    {ConflictPolicy::Resume, Existing::Missing, "0123456789ABCDEFGHIJ", OperationState::Finished, nullptr},
    {ConflictPolicy::Resume, Existing::Smaller, "ZZZZZZZZZZABCDEFGHIJ", OperationState::Finished, nullptr},
    {ConflictPolicy::Resume, Existing::SameSize, "xxxxxxxxxxxxxxxxxxxx", OperationState::FinishedWithSkips, nullptr},
    {ConflictPolicy::Resume, Existing::Larger, "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyy", OperationState::NeedsUser, nullptr},
    {ConflictPolicy::OverwriteIfOlder, Existing::SameSizeOld, "0123456789ABCDEFGHIJ", OperationState::Finished, nullptr},
    {ConflictPolicy::OverwriteIfOlder, Existing::SameSizeNew, "xxxxxxxxxxxxxxxxxxxx", OperationState::FinishedWithSkips,
     nullptr},
    {ConflictPolicy::Rename, Existing::Missing, "0123456789ABCDEFGHIJ", OperationState::Finished, nullptr},
    {ConflictPolicy::Rename, Existing::SameSize, "xxxxxxxxxxxxxxxxxxxx", OperationState::Finished,
     "0123456789ABCDEFGHIJ"},
};
} // namespace

TEST_F(OperationConflictTest, DownloadPolicyTable)
{
    int number = 0;
    for (const CPolicyCase& c : kCases)
    {
        ++number;
        const std::string name = "dl" + std::to_string(number) + ".txt";
        const std::string renamedName = "dl" + std::to_string(number) + " (2).txt";
        SCOPED_TRACE("case " + std::to_string(number) + " policy " + std::to_string(static_cast<int>(c.policy)) +
                     " existing " + std::to_string(static_cast<int>(c.existing)));

        PutRemoteFile(remoteDir_ + "/" + name, Source());
        if (c.existing != Existing::Missing)
        {
            WriteAll(local_ / name, ContentOf(c.existing));
            if (ExistingMtime(c.existing) != 0)
                SetLocalMtime(local_ / name, ExistingMtime(c.existing));
        }
        policy_ = c.policy;
        auto op = MakeDownload({RemoteSrc(remoteDir_ + "/" + name, false, Source().size())});
        CTestObserver observer;
        ASSERT_TRUE(RunToEnd(*op, observer));

        EXPECT_EQ(op->State(), c.expectedState) << Describe(*op);
        EXPECT_EQ(ReadAll(local_ / name), c.expectedContent);
        if (c.renamedContent != nullptr)
            EXPECT_EQ(ReadAll(local_ / renamedName), c.renamedContent);
        else
            EXPECT_FALSE(fs::exists(local_ / renamedName));
    }
}

TEST_F(OperationConflictTest, UploadPolicyTableWithSafeUpload)
{
    int number = 0;
    for (const CPolicyCase& c : kCases)
    {
        ++number;
        const std::string name = "ul" + std::to_string(number) + ".txt";
        const std::string renamedName = "ul" + std::to_string(number) + " (2).txt";
        SCOPED_TRACE("case " + std::to_string(number) + " policy " + std::to_string(static_cast<int>(c.policy)) +
                     " existing " + std::to_string(static_cast<int>(c.existing)));

        WriteAll(local_ / "src" / name, Source());
        if (c.existing != Existing::Missing)
        {
            PutRemoteFile(remoteDir_ + "/" + name, ContentOf(c.existing));
            if (ExistingMtime(c.existing) != 0)
                ASSERT_TRUE(admin_.SetTimes(remoteDir_ + "/" + name, ExistingMtime(c.existing), ExistingMtime(c.existing)));
        }
        policy_ = c.policy;
        safeUpload_ = true;
        auto op = MakeUpload({LocalSrc(local_ / "src" / name, false)}, remoteDir_);
        CTestObserver observer;
        ASSERT_TRUE(RunToEnd(*op, observer));

        EXPECT_EQ(op->State(), c.expectedState) << Describe(*op);
        EXPECT_EQ(GetRemoteFile(remoteDir_ + "/" + name), c.expectedContent);
        if (c.renamedContent != nullptr)
            EXPECT_EQ(GetRemoteFile(remoteDir_ + "/" + renamedName), c.renamedContent);
        else
            EXPECT_FALSE(RemoteExists(remoteDir_ + "/" + renamedName));
        EXPECT_FALSE(HasFilepart()) << "a .filepart was left behind";
    }
}

TEST_F(OperationConflictTest, UploadPolicyTableWithoutSafeUpload)
{
    // the same rules when the data goes straight to the target (resume continues the target itself)
    int number = 0;
    for (const CPolicyCase& c : kCases)
    {
        ++number;
        const std::string name = "ud" + std::to_string(number) + ".txt";
        const std::string renamedName = "ud" + std::to_string(number) + " (2).txt";
        SCOPED_TRACE("case " + std::to_string(number));

        WriteAll(local_ / "src" / name, Source());
        if (c.existing != Existing::Missing)
        {
            PutRemoteFile(remoteDir_ + "/" + name, ContentOf(c.existing));
            if (ExistingMtime(c.existing) != 0)
                ASSERT_TRUE(admin_.SetTimes(remoteDir_ + "/" + name, ExistingMtime(c.existing), ExistingMtime(c.existing)));
        }
        policy_ = c.policy;
        safeUpload_ = false;
        auto op = MakeUpload({LocalSrc(local_ / "src" / name, false)}, remoteDir_);
        CTestObserver observer;
        ASSERT_TRUE(RunToEnd(*op, observer));

        EXPECT_EQ(op->State(), c.expectedState) << Describe(*op);
        EXPECT_EQ(GetRemoteFile(remoteDir_ + "/" + name), c.expectedContent);
        if (c.renamedContent != nullptr)
            EXPECT_EQ(GetRemoteFile(remoteDir_ + "/" + renamedName), c.renamedContent);
        else
            EXPECT_FALSE(RemoteExists(remoteDir_ + "/" + renamedName));
        EXPECT_FALSE(HasFilepart());
    }
}

TEST_F(OperationConflictTest, ADirectoryWhereAFileIsExpectedIsParkedForEveryPolicy)
{
    PutRemoteFile(remoteDir_ + "/clash", "file\n");
    fs::create_directories(local_ / "clash"); // a directory where the download wants a file
    policy_ = ConflictPolicy::Overwrite;
    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/clash", false, 5)});
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*op, observer));
    EXPECT_EQ(op->State(), OperationState::NeedsUser);
    ASSERT_EQ(ItemsWithProblem(*op, Problem::TargetExists).size(), 1u);
    // even a solve with Overwrite must not replace a directory
    const uint64_t uid = ItemsWithProblem(*op, Problem::TargetExists)[0].uid;
    EXPECT_EQ(op->Solve(uid, SolveAction::Overwrite), 1);
    ASSERT_TRUE(WaitForRun(observer, 2));
    EXPECT_EQ(op->State(), OperationState::NeedsUser);
    EXPECT_TRUE(fs::is_directory(local_ / "clash"));
}

// ---- Ask -> parked -> Solve ----------------------------------------------------------------------------------------

TEST_F(OperationConflictTest, AskParksAndSolveOverwriteForOneThenSkipForTheOther)
{
    PutRemoteFile(remoteDir_ + "/a.txt", "remote-a\n");
    PutRemoteFile(remoteDir_ + "/b.txt", "remote-b\n");
    WriteAll(local_ / "a.txt", "local-a\n");
    WriteAll(local_ / "b.txt", "local-b\n");
    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/a.txt", false, 9), RemoteSrc(remoteDir_ + "/b.txt", false, 9)});
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*op, observer));
    EXPECT_EQ(op->State(), OperationState::NeedsUser);
    EXPECT_EQ(observer.finished.load(), 1);

    const std::vector<CQueueItem> parked = ItemsWithProblem(*op, Problem::TargetExists);
    ASSERT_EQ(parked.size(), 2u);
    for (const CQueueItem& item : parked)
    {
        EXPECT_FALSE(item.errorText.empty()); // names the target
        EXPECT_EQ(item.targetSize, 8u);       // "local-a\n": what the solve dialog shows
        EXPECT_GT(item.targetMtime, 0);
    }

    EXPECT_EQ(op->Solve(parked[0].uid, SolveAction::Overwrite), 1);
    ASSERT_TRUE(WaitForRun(observer, 2)); // the idle workers picked the solved item up: a second run
    EXPECT_EQ(op->State(), OperationState::NeedsUser);
    EXPECT_EQ(ReadAll(local_ / "a.txt"), "remote-a\n");
    EXPECT_EQ(ReadAll(local_ / "b.txt"), "local-b\n");

    EXPECT_EQ(op->Solve(parked[1].uid, SolveAction::Skip), 1);
    EXPECT_EQ(op->State(), OperationState::FinishedWithSkips);
    EXPECT_TRUE(op->IsFinished());
    EXPECT_EQ(observer.finished.load(), 2); // no work ran: no new run
    EXPECT_EQ(ReadAll(local_ / "b.txt"), "local-b\n");
}

TEST_F(OperationConflictTest, SolveOverwriteForAllItemsWithTheProblem)
{
    for (const char* name : {"a.txt", "b.txt", "c.txt"})
    {
        PutRemoteFile(remoteDir_ + "/" + name, std::string("remote-") + name);
        WriteAll(local_ / name, "old");
    }
    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/a.txt", false, 12), RemoteSrc(remoteDir_ + "/b.txt", false, 12),
                            RemoteSrc(remoteDir_ + "/c.txt", false, 12)});
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*op, observer));
    const std::vector<CQueueItem> parked = ItemsWithProblem(*op, Problem::TargetExists);
    ASSERT_EQ(parked.size(), 3u);

    EXPECT_EQ(op->Solve(parked[0].uid, SolveAction::Overwrite, L"", true), 3);
    ASSERT_TRUE(WaitForRun(observer, 2));
    EXPECT_EQ(op->State(), OperationState::Finished);
    EXPECT_EQ(ReadAll(local_ / "a.txt"), "remote-a.txt");
    EXPECT_EQ(ReadAll(local_ / "c.txt"), "remote-c.txt");
}

TEST_F(OperationConflictTest, SolveResumeContinuesAfterTheExistingSize)
{
    PutRemoteFile(remoteDir_ + "/r.txt", "0123456789ABCDEFGHIJ");
    WriteAll(local_ / "r.txt", "ZZZZZZZZZZ"); // corrupt on purpose: only a real resume keeps it
    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/r.txt", false, 20)});
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*op, observer));
    ASSERT_EQ(op->State(), OperationState::NeedsUser);
    EXPECT_EQ(op->Solve(ItemsWithProblem(*op, Problem::TargetExists)[0].uid, SolveAction::Resume), 1);
    ASSERT_TRUE(WaitForRun(observer, 2));
    EXPECT_EQ(op->State(), OperationState::Finished);
    EXPECT_EQ(ReadAll(local_ / "r.txt"), "ZZZZZZZZZZABCDEFGHIJ");
}

TEST_F(OperationConflictTest, SolveRenameWithTheUsersNameAndAskAgainWhenItExists)
{
    PutRemoteFile(remoteDir_ + "/a.txt", "remote-a\n");
    WriteAll(local_ / "a.txt", "local-a\n");
    WriteAll(local_ / "taken.txt", "taken\n");
    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/a.txt", false, 9)});
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*op, observer));
    const uint64_t uid = ItemsWithProblem(*op, Problem::TargetExists)[0].uid;

    // a name that exists is not overwritten without asking
    EXPECT_EQ(op->Solve(uid, SolveAction::Rename, L"taken.txt"), 1);
    ASSERT_TRUE(WaitForRun(observer, 2));
    EXPECT_EQ(op->State(), OperationState::NeedsUser);
    EXPECT_EQ(ReadAll(local_ / "taken.txt"), "taken\n");

    // a name Windows cannot use parks with another problem
    EXPECT_EQ(op->Solve(uid, SolveAction::Rename, L"a:b"), 1);
    ASSERT_TRUE(WaitForRun(observer, 3));
    EXPECT_EQ(ItemsWithProblem(*op, Problem::InvalidLocalName).size(), 1u);

    EXPECT_EQ(op->Solve(uid, SolveAction::Rename, L"mine.txt"), 1);
    ASSERT_TRUE(WaitForRun(observer, 4));
    EXPECT_EQ(op->State(), OperationState::Finished);
    EXPECT_EQ(ReadAll(local_ / "mine.txt"), "remote-a\n");
    EXPECT_EQ(ReadAll(local_ / "a.txt"), "local-a\n");
}

TEST_F(OperationConflictTest, AnItemWithAnInvalidLocalNameCanBeRenamedBySolve)
{
    ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/names"));
    PutRemoteFile(remoteDir_ + "/names/good.txt", "good\n");
    PutRemoteFile(remoteDir_ + "/names/a:b", "bad\n");
    ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/names/aux")); // a reserved name, a directory
    PutRemoteFile(remoteDir_ + "/names/aux/inside.txt", "inside\n");
    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/names", true)});
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*op, observer));
    const std::vector<CQueueItem> parked = ItemsWithProblem(*op, Problem::InvalidLocalName);
    ASSERT_EQ(parked.size(), 2u);

    for (const CQueueItem& item : parked)
    {
        const bool isFile = item.type == ItemType::Download;
        EXPECT_EQ(op->Solve(item.uid, SolveAction::Rename, isFile ? L"ab.txt" : L"auxdir"), 1);
    }
    ASSERT_TRUE(WaitUntil([&] { return op->State() == OperationState::Finished; }, 30000)) << Describe(*op);
    EXPECT_EQ(ReadAll(local_ / "names" / "ab.txt"), "bad\n");
    EXPECT_EQ(ReadAll(local_ / "names" / "auxdir" / "inside.txt"), "inside\n");
}

TEST_F(OperationConflictTest, AutomaticRenameTakesTheNextFreeNumber)
{
    PutRemoteFile(remoteDir_ + "/src.txt", "remote\n");
    WriteAll(local_ / "src.txt", "one\n");
    WriteAll(local_ / "src (2).txt", "two\n");
    policy_ = ConflictPolicy::Rename;
    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/src.txt", false, 7)});
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*op, observer));
    EXPECT_EQ(op->State(), OperationState::Finished);
    EXPECT_EQ(ReadAll(local_ / "src (3).txt"), "remote\n");
    EXPECT_EQ(ReadAll(local_ / "src.txt"), "one\n");
    EXPECT_EQ(ReadAll(local_ / "src (2).txt"), "two\n");
}

TEST_F(OperationConflictTest, UploadAskParksAndSolveOverwriteReplacesTheTarget)
{
    PutRemoteFile(remoteDir_ + "/a.txt", "remote-old\n");
    WriteAll(local_ / "src" / "a.txt", "local-new\n");
    auto op = MakeUpload({LocalSrc(local_ / "src" / "a.txt", false)}, remoteDir_);
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*op, observer));
    ASSERT_EQ(op->State(), OperationState::NeedsUser);
    const CQueueItem parked = ItemsWithProblem(*op, Problem::TargetExists)[0];
    EXPECT_EQ(parked.targetSize, 11u);
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/a.txt"), "remote-old\n");

    EXPECT_EQ(op->Solve(parked.uid, SolveAction::Overwrite), 1);
    ASSERT_TRUE(WaitForRun(observer, 2));
    EXPECT_EQ(op->State(), OperationState::Finished);
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/a.txt"), "local-new\n");
    EXPECT_FALSE(HasFilepart());
}

TEST_F(OperationConflictTest, UploadSolveRenameUsesTheUsersName)
{
    PutRemoteFile(remoteDir_ + "/a.txt", "remote-old\n");
    WriteAll(local_ / "src" / "a.txt", "local-new\n");
    auto op = MakeUpload({LocalSrc(local_ / "src" / "a.txt", false)}, remoteDir_);
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*op, observer));
    const uint64_t uid = ItemsWithProblem(*op, Problem::TargetExists)[0].uid;

    EXPECT_EQ(op->Solve(uid, SolveAction::Rename, L"x/y"), 1); // a name with a slash is no name
    ASSERT_TRUE(WaitForRun(observer, 2));
    EXPECT_EQ(ItemsWithProblem(*op, Problem::InvalidRemoteName).size(), 1u);

    EXPECT_EQ(op->Solve(uid, SolveAction::Rename, L"renamed.txt"), 1);
    ASSERT_TRUE(WaitForRun(observer, 3));
    EXPECT_EQ(op->State(), OperationState::Finished);
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/renamed.txt"), "local-new\n");
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/a.txt"), "remote-old\n");
}

TEST_F(OperationConflictTest, RetryAllFailedBringsTheOperationBackToLife)
{
    // an upload whose source file is missing fails; it is fixed, then retried
    WriteAll(local_ / "src" / "other.txt", "other\n");
    auto op = MakeUpload({LocalSrc(local_ / "src" / "late.txt", false), LocalSrc(local_ / "src" / "other.txt", false)},
                         remoteDir_);
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*op, observer));
    EXPECT_EQ(op->State(), OperationState::FinishedWithErrors);
    WriteAll(local_ / "src" / "late.txt", "late\n");

    EXPECT_EQ(op->RetryAllFailed(), 1);
    ASSERT_TRUE(WaitForRun(observer, 2));
    EXPECT_EQ(op->State(), OperationState::Finished);
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/late.txt"), "late\n");
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/other.txt"), "other\n");
}

TEST_F(OperationConflictTest, MoveWithSkipLeavesTheSourceInPlace)
{
    PutRemoteFile(remoteDir_ + "/m.txt", "remote\n");
    WriteAll(local_ / "m.txt", "local\n");
    policy_ = ConflictPolicy::Skip;
    auto down = MakeDownload({RemoteSrc(remoteDir_ + "/m.txt", false, 7)}, true);
    CTestObserver observer;
    ASSERT_TRUE(RunToEnd(*down, observer));
    EXPECT_EQ(down->State(), OperationState::FinishedWithSkips);
    EXPECT_TRUE(RemoteExists(remoteDir_ + "/m.txt"));
    EXPECT_EQ(ReadAll(local_ / "m.txt"), "local\n");

    WriteAll(local_ / "src" / "u.txt", "up\n");
    PutRemoteFile(remoteDir_ + "/u.txt", "there\n");
    auto up = MakeUpload({LocalSrc(local_ / "src" / "u.txt", false)}, remoteDir_, true);
    CTestObserver observer2;
    ASSERT_TRUE(RunToEnd(*up, observer2));
    EXPECT_EQ(up->State(), OperationState::FinishedWithSkips);
    EXPECT_TRUE(fs::exists(local_ / "src" / "u.txt"));
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/u.txt"), "there\n");
}

TEST_F(OperationConflictTest, LongLocalSourcePathsAreUploaded)
{
    // std::filesystem cannot make (or later remove) such a tree, so use the extended prefix by hand
    const std::wstring prefix = L"\\\\?\\";
    std::vector<std::wstring> folders;
    std::wstring chain = prefix + local_.wstring() + L"\\long";
    ASSERT_TRUE(CreateDirectoryW(chain.c_str(), nullptr));
    folders.push_back(chain);
    while (chain.size() < 290)
    {
        chain += L"\\a_rather_long_folder_name";
        ASSERT_TRUE(CreateDirectoryW(chain.c_str(), nullptr));
        folders.push_back(chain);
    }
    const std::wstring file = chain + L"\\deep.txt";
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(h, "deep\n", 5, &written, nullptr);
    CloseHandle(h);

    CLocalSource source;
    source.path = file.substr(prefix.size()); // the way a panel hands it over: longer than MAX_PATH, no prefix
    ASSERT_GE(source.path.size(), 260u);
    {
        auto op = MakeUpload({source}, remoteDir_);
        CTestObserver observer;
        ASSERT_TRUE(Run(*op, observer));
        EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
        EXPECT_EQ(GetRemoteFile(remoteDir_ + "/deep.txt"), "deep\n");
    }

    DeleteFileW(file.c_str());
    for (auto it = folders.rbegin(); it != folders.rend(); ++it)
        RemoveDirectoryW(it->c_str());
}

// ---- safe upload ---------------------------------------------------------------------------------------------------

TEST_F(OperationConflictTest, SafeUploadLeavesTheTargetAloneUntilTheRename)
{
    const std::string source = PatternBytes(24 << 20, 3);
    WriteAll(local_ / "src" / "big.bin", source);
    PutRemoteFile(remoteDir_ + "/big.bin", "OLD");
    sftptest::CStallProxy proxy(sftptest::kPort);
    proxy.StallAfter(8 << 20); // the server stops answering after 8 MiB
    port_ = proxy.Port();
    policy_ = ConflictPolicy::Overwrite;

    auto op = MakeUpload({LocalSrc(local_ / "src" / "big.bin", false)}, remoteDir_);
    CTestObserver observer;
    op->SetObserver(&observer);
    ASSERT_TRUE(op->Start());
    ASSERT_TRUE(WaitUntil([&] { return proxy.IsStalled(); }, 60000));
    Sleep(300);

    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/big.bin"), "OLD"); // not touched
    CSftpEntry part;
    ASSERT_TRUE(admin_.Stat(remoteDir_ + "/big.bin.filepart", part)) << "no partial file";
    EXPECT_GT(part.size, 0u);
    EXPECT_LT(part.size, source.size());

    op->RequestCancel();
    ASSERT_TRUE(op->WaitFinished(30000));
    EXPECT_EQ(GetRemoteFile(remoteDir_ + "/big.bin"), "OLD");
    EXPECT_TRUE(RemoteExists(remoteDir_ + "/big.bin.filepart")); // kept for a resume
    op.reset();

    // a new operation with Resume continues the partial and swaps it in
    port_ = sftptest::kPort;
    policy_ = ConflictPolicy::Resume;
    auto again = MakeUpload({LocalSrc(local_ / "src" / "big.bin", false)}, remoteDir_);
    CTestObserver observer2;
    ASSERT_TRUE(Run(*again, observer2));
    EXPECT_EQ(again->State(), OperationState::Finished) << Describe(*again);
    EXPECT_TRUE(GetRemoteFile(remoteDir_ + "/big.bin") == source);
    EXPECT_FALSE(HasFilepart());
}

TEST_F(OperationConflictTest, SafeUploadContinuesAPartialOnlyWhenItCanBeTrusted)
{
    const std::string source = PatternBytes(2 << 20, 5);
    WriteAll(local_ / "src" / "f.bin", source);
    const std::string corruptPrefix = "Z" + source.substr(1, (1 << 20) - 1); // 1 MiB, the first byte is wrong
    PutRemoteFile(remoteDir_ + "/f.bin.filepart", corruptPrefix);

    // Overwrite: the foreign partial is not trusted, the file is written again from the start
    policy_ = ConflictPolicy::Overwrite;
    {
        auto op = MakeUpload({LocalSrc(local_ / "src" / "f.bin", false)}, remoteDir_);
        CTestObserver observer;
        ASSERT_TRUE(Run(*op, observer));
        EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
        EXPECT_TRUE(GetRemoteFile(remoteDir_ + "/f.bin") == source);
        EXPECT_FALSE(HasFilepart());
    }

    // Resume: the user asked for it, the partial is continued (the bad first byte survives)
    PutRemoteFile(remoteDir_ + "/g.bin.filepart", corruptPrefix);
    WriteAll(local_ / "src" / "g.bin", source);
    policy_ = ConflictPolicy::Resume;
    {
        auto op = MakeUpload({LocalSrc(local_ / "src" / "g.bin", false)}, remoteDir_);
        CTestObserver observer;
        ASSERT_TRUE(Run(*op, observer));
        EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
        EXPECT_TRUE(GetRemoteFile(remoteDir_ + "/g.bin") == "Z" + source.substr(1));
        EXPECT_FALSE(HasFilepart());
    }
}

TEST_F(OperationConflictTest, WithoutSafeUploadTheTargetIsWrittenDirectlyAndResumable)
{
    const std::string source = PatternBytes(24 << 20, 9);
    WriteAll(local_ / "src" / "direct.bin", source);
    PutRemoteFile(remoteDir_ + "/direct.bin", "OLD");
    sftptest::CStallProxy proxy(sftptest::kPort);
    proxy.StallAfter(8 << 20);
    port_ = proxy.Port();
    policy_ = ConflictPolicy::Overwrite;
    safeUpload_ = false;

    auto op = MakeUpload({LocalSrc(local_ / "src" / "direct.bin", false)}, remoteDir_);
    CTestObserver observer;
    op->SetObserver(&observer);
    ASSERT_TRUE(op->Start());
    ASSERT_TRUE(WaitUntil([&] { return proxy.IsStalled(); }, 60000));
    Sleep(300);
    op->RequestCancel();
    ASSERT_TRUE(op->WaitFinished(30000));
    op.reset();

    CSftpEntry entry;
    ASSERT_TRUE(admin_.Stat(remoteDir_ + "/direct.bin", entry));
    EXPECT_GT(entry.size, 0u);
    EXPECT_LT(entry.size, source.size()); // the target itself is the partial file
    EXPECT_FALSE(HasFilepart());

    port_ = sftptest::kPort;
    policy_ = ConflictPolicy::Resume;
    auto again = MakeUpload({LocalSrc(local_ / "src" / "direct.bin", false)}, remoteDir_);
    CTestObserver observer2;
    ASSERT_TRUE(Run(*again, observer2));
    EXPECT_EQ(again->State(), OperationState::Finished) << Describe(*again);
    EXPECT_TRUE(GetRemoteFile(remoteDir_ + "/direct.bin") == source);
}
