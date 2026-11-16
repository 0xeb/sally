// Stress tests with random cancel (M7). Real operations and panel sessions against the Docker test server; the test
// thread throws random Pause / Resume / Cancel / destroy calls at them and then checks what must always hold:
//   - nothing hangs (every wait has a time limit),
//   - no worker slot is left behind (WorkerSlots().Held() == 0),
//   - no file on either side is wrong: a partial file is a prefix of its source, and a complete name is complete,
//   - what was not cancelled finishes with the right bytes, and a final clean run is byte-identical,
//   - a panel session still works after any Cancel().
//
// The tests are named Stress*, so `--gtest_filter=-Stress*` leaves them out. Each takes about 20 seconds.
//   SFTP_STRESS_SEED=<n>    repeat a run (the seed of every run is printed as "[stress] seed=...")
//   SFTP_STRESS_ROUNDS=<n>  run exactly n rounds per test instead of a time budget (for a long soak)
#include "operation_test_support.h"

#include "conflict.h"
#include "operations_list.h"
#include "panel_session.h"
#include "worker_slots.h"

#include <iostream>
#include <map>
#include <random>

namespace
{
using Clock = std::chrono::steady_clock;

unsigned long EnvNumber(const char* name, unsigned long fallback)
{
    char buffer[32];
    const DWORD n = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
    if (n == 0 || n >= sizeof(buffer))
        return fallback;
    return strtoul(buffer, nullptr, 10);
}

class StressTest : public OperationTest
{
protected:
    void SetUp() override
    {
        OperationTest::SetUp();
        if (IsSkipped())
            return;
        const unsigned seed = static_cast<unsigned>(EnvNumber("SFTP_STRESS_SEED", std::random_device{}()));
        rng_.seed(seed);
        fixedRounds_ = static_cast<int>(EnvNumber("SFTP_STRESS_ROUNDS", 0));
        deadline_ = Clock::now() + std::chrono::seconds(18);
        std::cout << "[stress] seed=" << seed << " (repeat with SFTP_STRESS_SEED=" << seed << ")" << std::endl;
        ASSERT_EQ(WorkerSlots().Held(), 0) << "another test left a slot behind";
        BuildData();
    }

    int Rand(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }

    // True while another round may start: the time budget, or the fixed number of rounds. A failure ends it early.
    bool MoreRounds(int done) const
    {
        if (HasFailure())
            return false;
        return fixedRounds_ > 0 ? done < fixedRounds_ : Clock::now() < deadline_;
    }

    // 24 files of about 100 KiB and one of 6 MiB, on the server (remoteDir_/srcdata) and on disk (local_/srcdata).
    void BuildData()
    {
        ASSERT_TRUE(admin_.Mkdir(remoteDir_ + "/srcdata")) << admin_.LastError().message;
        for (int i = 0; i < 24; ++i)
            files_["f" + std::to_string(i) + ".bin"] = PatternBytes(96 * 1024 + static_cast<size_t>(i) * 1000, i + 1);
        files_["big.bin"] = PatternBytes(6 << 20, 99);
        for (const auto& file : files_)
        {
            PutRemoteFile(remoteDir_ + "/srcdata/" + file.first, file.second);
            WriteAll(local_ / "srcdata" / file.first, file.second);
        }
    }

    // A download or an upload of the whole data folder into a fresh target. 'target' is where the result will be:
    // a local folder (download) or a remote folder (upload), both with "srcdata" below.
    std::unique_ptr<COperation> MakeRound(bool download, int workers, fs::path& localTarget, std::string& remoteTarget)
    {
        const int n = ++roundNo_;
        if (download)
        {
            localTarget = local_ / ("dl" + std::to_string(n));
            fs::create_directories(localTarget);
            COperationSettings settings = Settings(TransferDirection::Download, false, workers);
            settings.localRoot = localTarget.wstring();
            return std::make_unique<COperation>(
                settings, Params(),
                COperation::BuildDownloadRoots({RemoteSrc(remoteDir_ + "/srcdata", true)}, settings, CNameCodec::Auto()));
        }
        remoteTarget = remoteDir_ + "/up" + std::to_string(n);
        EXPECT_TRUE(admin_.Mkdir(remoteTarget)) << admin_.LastError().message;
        return MakeUpload({LocalSrc(local_ / "srcdata", true)}, remoteTarget, false, workers);
    }

    enum class End
    {
        None,      // nothing ended it: it runs to the end
        Cancelled, // RequestCancel
        Destroyed  // the operation was destroyed (what Release does to the last owner)
    };

    // The random part: up to four actions at random times. An operation that is still paused at the end is resumed.
    End ThrowRandomActions(std::unique_ptr<COperation>& op)
    {
        Sleep(static_cast<DWORD>(Rand(0, 300)));
        for (int step = 0; step < 4; ++step)
        {
            if (op->IsFinished())
                return End::None;
            const int pick = Rand(0, 7);
            if (pick <= 2)
                op->Pause();
            else if (pick <= 5)
                op->Resume();
            else if (pick == 6)
            {
                op->RequestCancel();
                return End::Cancelled;
            }
            else
            {
                const auto start = Clock::now();
                op.reset();
                EXPECT_LT(Clock::now() - start, std::chrono::seconds(10)) << "destroying the operation took too long";
                return End::Destroyed;
            }
            Sleep(static_cast<DWORD>(Rand(0, 150)));
        }
        op->Resume();
        return End::None;
    }

    // A local result: every file is known, is a prefix of its source (a partial file is never a wrong one), and with
    // 'complete' all of them are there in full.
    void CheckLocalResult(const fs::path& target, bool complete)
    {
        const fs::path dir = target / "srcdata";
        for (const auto& file : files_)
        {
            const fs::path path = dir / file.first;
            std::error_code ec;
            if (!fs::exists(path, ec))
            {
                if (complete)
                    ADD_FAILURE() << "missing after a finished download: " << file.first;
                continue;
            }
            const std::string got = ReadAll(path);
            if (got.size() > file.second.size() || file.second.compare(0, got.size(), got) != 0)
                ADD_FAILURE() << "wrong bytes in a downloaded file: " << file.first << " (" << got.size() << " bytes)";
            else if (complete && got.size() != file.second.size())
                ADD_FAILURE() << "short file after a finished download: " << file.first;
        }
    }

    // A remote result: a name without ".filepart" must be complete and identical (safe upload renames only after the
    // last byte); a ".filepart" is at most as long as its source. With 'complete' every file is there.
    void CheckRemoteResult(const std::string& target, bool complete)
    {
        const std::string dir = target + "/srcdata";
        std::vector<CSftpEntry> entries;
        if (!admin_.ListDir(dir, entries))
        {
            if (complete)
                ADD_FAILURE() << "the upload folder is missing after a finished upload";
            return;
        }
        std::map<std::string, bool> seen;
        for (const CSftpEntry& entry : entries)
        {
            std::string name = entry.rawName;
            const bool partial = IsSafeUploadName(name);
            if (partial)
                name.resize(name.size() - 9); // strlen(".filepart")
            const auto known = files_.find(name);
            if (known == files_.end())
            {
                ADD_FAILURE() << "unexpected remote file: " << entry.rawName;
                continue;
            }
            if (partial)
            {
                if (entry.size > known->second.size())
                    ADD_FAILURE() << "a .filepart is longer than its source: " << entry.rawName;
                continue;
            }
            seen[name] = true;
            if (GetRemoteFile(dir + "/" + name) != known->second)
                ADD_FAILURE() << "a file with its final name is not identical to the source: " << name;
        }
        if (complete && seen.size() != files_.size())
            ADD_FAILURE() << "not every file is there after a finished upload (" << seen.size() << " of " << files_.size() << ")";
    }

    // For a failed upload round: is the target of an item that did not finish a complete, identical copy of its source?
    // (A pause that lands while the final rename is in flight leaves exactly that behind.)
    std::string TargetEvidence(COperation& op)
    {
        std::string text;
        for (const CQueueItem& item : op.Queue().Items(true))
        {
            if (item.state == ItemState::Done || item.remotePath.empty())
                continue;
            const auto known = files_.find(item.remotePath.substr(item.remotePath.rfind('/') + 1));
            if (known == files_.end())
                continue;
            CSftpEntry target, partial;
            text += "\n  evidence for " + item.remotePath + ": ";
            if (admin_.LStat(item.remotePath, target))
                text += "target exists, " + std::to_string(target.size) + " of " + std::to_string(known->second.size()) +
                        " bytes, identical to the source: " +
                        (GetRemoteFile(item.remotePath) == known->second ? "YES" : "no");
            else
                text += "target missing";
            text += admin_.LStat(item.remotePath + ".filepart", partial)
                        ? ", .filepart present (" + std::to_string(partial.size) + " bytes)"
                        : ", no .filepart";
        }
        return text;
    }

    // Waits for the answer of a panel request like fs/ does; host key questions (also after a reconnect) are accepted.
    static bool Pump(CPanelSession& s, CPanelRequest& req, DWORD timeoutMs = 30000)
    {
        const auto end = Clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
            if (left <= 0)
                return req.IsDone();
            HANDLE handles[2] = {req.DoneEvent(), s.InteractionEvent()};
            const DWORD r = WaitForMultipleObjects(2, handles, FALSE, static_cast<DWORD>(left));
            if (r == WAIT_OBJECT_0)
                return true;
            if (r != WAIT_OBJECT_0 + 1)
                return req.IsDone();
            CInteraction in;
            if (!s.TryGetInteraction(in))
                continue;
            if (in.kind == CInteraction::Kind::HostKey)
                s.AnswerHostKey(HostKeyDecision::AcceptOnce);
            else
                s.AnswerPrompt(false, CSecret());
        }
    }

    std::mt19937 rng_;
    int fixedRounds_ = 0;
    Clock::time_point deadline_;
    int roundNo_ = 0;
    std::map<std::string, std::string> files_; // name -> content of the data folder
};
} // namespace

TEST_F(StressTest, OperationsSurviveRandomPauseResumeAndCancel)
{
    int rounds = 0, finished = 0, cancelled = 0, destroyed = 0;
    for (; MoreRounds(rounds); ++rounds)
    {
        const bool download = Rand(0, 1) == 0;
        fs::path localTarget;
        std::string remoteTarget;
        CTestObserver observer; // before the operation: the workers still call it while the operation is destroyed
        std::unique_ptr<COperation> op = MakeRound(download, 3, localTarget, remoteTarget);
        op->SetObserver(&observer);
        ASSERT_TRUE(op->Start());

        const End end = ThrowRandomActions(op);
        if (end == End::None)
        {
            ASSERT_TRUE(op->WaitFinished(60000)) << "the operation did not finish: a hang";
            EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op) << (download ? "" : TargetEvidence(*op));
            ++finished;
        }
        else if (end == End::Cancelled)
        {
            ASSERT_TRUE(op->WaitFinished(30000)) << "a cancelled operation did not end: a hang";
            ++cancelled;
        }
        else
            ++destroyed;
        op.reset(); // joins the workers

        EXPECT_EQ(WorkerSlots().Held(), 0) << "a worker slot was left behind (round " << rounds << ")";
        if (download)
            CheckLocalResult(localTarget, end == End::None);
        else
            CheckRemoteResult(remoteTarget, end == End::None);
    }
    std::cout << "[stress] operations: " << rounds << " rounds, " << finished << " ran to the end, " << cancelled
              << " cancelled, " << destroyed << " destroyed" << std::endl;
    EXPECT_GE(rounds, 1);

    // A clean run after all that: nothing is left over that could disturb it, and the bytes are identical.
    for (const bool download : {true, false})
    {
        fs::path localTarget;
        std::string remoteTarget;
        CTestObserver observer;
        std::unique_ptr<COperation> op = MakeRound(download, 3, localTarget, remoteTarget);
        ASSERT_TRUE(Run(*op, observer));
        EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
        op.reset();
        if (download)
            CheckLocalResult(localTarget, true);
        else
            CheckRemoteResult(remoteTarget, true);
    }
    EXPECT_EQ(WorkerSlots().Held(), 0);
}

TEST_F(StressTest, PanelSessionWorksAfterEveryRandomCancel)
{
    CPanelSession session(nullptr);
    {
        auto connect = session.Connect(TestSite(), CSecret(sftptest::kPassword), CSecret());
        ASSERT_TRUE(Pump(session, *connect));
        ASSERT_TRUE(connect->Ok()) << connect->Error().message;
    }
    int rounds = 0, cancelled = 0;
    for (; MoreRounds(rounds); ++rounds)
    {
        const int kind = Rand(0, 3);
        const fs::path localFile = local_ / ("panel" + std::to_string(rounds) + ".bin");
        std::shared_ptr<CPanelRequest> req;
        if (kind == 0)
            req = session.List(remoteDir_ + "/srcdata");
        else if (kind == 1)
            req = session.Stat(remoteDir_ + "/srcdata/big.bin");
        else if (kind == 2)
            req = session.DownloadToFile(remoteDir_ + "/srcdata/big.bin", localFile.wstring(), 0);
        else
            req = session.List("fixtures/many"); // 5000 entries: a long listing
        Sleep(static_cast<DWORD>(Rand(0, 80)));
        session.Cancel();

        ASSERT_TRUE(Pump(session, *req)) << "the request did not end after Cancel: a hang (kind " << kind << ")";
        if (req->Cancelled())
            ++cancelled;
        else
            EXPECT_TRUE(req->Ok()) << "kind " << kind << ": " << req->Error().message;
        if (kind == 2 && req->Ok())
            EXPECT_TRUE(ReadAll(localFile) == files_["big.bin"]) << "a finished download is not identical";

        // D24: the session reconnects by itself; the next request must work.
        auto next = session.RealPath("");
        ASSERT_TRUE(Pump(session, *next)) << "the request after a Cancel did not end";
        EXPECT_TRUE(next->Ok()) << "the request after a Cancel failed: " << next->Error().message;
        std::error_code ignored;
        fs::remove(localFile, ignored);
    }
    std::cout << "[stress] panel session: " << rounds << " rounds, " << cancelled << " requests were cancelled" << std::endl;
    EXPECT_GE(rounds, 1);
}

TEST_F(StressTest, ClearAtARandomMomentEndsEveryRunningOperation)
{
    const DWORD handlesBefore = HandleCount();
    int rounds = 0;
    for (; MoreRounds(rounds); ++rounds)
    {
        CTestObserver observer; // before the list: the operations still call it while Clear joins them
        COperationsList list;
        const int count = Rand(2, 3);
        for (int i = 0; i < count; ++i)
        {
            fs::path localTarget;
            std::string remoteTarget;
            std::shared_ptr<COperation> op(MakeRound(Rand(0, 1) == 0, 2, localTarget, remoteTarget).release());
            op->SetObserver(&observer);
            list.Add(op);
            ASSERT_TRUE(op->Start());
        }
        Sleep(static_cast<DWORD>(Rand(0, 400)));
        const auto start = Clock::now();
        list.Clear(); // what Release does
        EXPECT_LT(Clock::now() - start, std::chrono::seconds(10)) << "Clear took too long (round " << rounds << ")";
        EXPECT_FALSE(list.HasRunning());
        EXPECT_EQ(WorkerSlots().Held(), 0) << "a worker slot was left behind (round " << rounds << ")";
    }
    std::cout << "[stress] clear: " << rounds << " rounds" << std::endl;
    EXPECT_GE(rounds, 1);
    EXPECT_LE(HandleCount(), handlesBefore + 10) << "handles grew over " << rounds << " rounds: a leak";
}
