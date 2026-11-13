// Tests for the global connection cap with real operations (D58): COperation against the Docker test server. A
// CStallProxy plays the transfer that keeps its slot for as long as the test wants.
#include "operation_test_support.h"

#include "global_config.h"
#include "worker_slots.h"

namespace
{
class OperationSlotsTest : public OperationTest
{
protected:
    void SetUp() override
    {
        OperationTest::SetUp();
        if (IsSkipped())
            return;
        saved_ = GlobalConfigStore().Get();
        configSaved_ = true;
        ASSERT_EQ(WorkerSlots().Held(), 0) << "another test left a slot behind";
        WorkerSlots().ResetPeak();
    }

    void TearDown() override
    {
        if (configSaved_)
            GlobalConfigStore().Set(saved_);
        OperationTest::TearDown();
    }

    static void SetCap(int cap)
    {
        CGlobalConfig config = GlobalConfigStore().Get();
        config.maxWorkersTotal = cap;
        GlobalConfigStore().Set(config);
    }

    // Several small remote files: enough for two workers to be busy for a moment.
    std::vector<CRemoteSource> PutManyFiles(const std::string& folder, int count)
    {
        EXPECT_TRUE(admin_.Mkdir(remoteDir_ + "/" + folder));
        std::vector<CRemoteSource> sources;
        for (int i = 0; i < count; ++i)
        {
            const std::string path = remoteDir_ + "/" + folder + "/f" + std::to_string(i) + ".bin";
            PutRemoteFile(path, PatternBytes(256 * 1024, static_cast<unsigned>(i + 1)));
            sources.push_back(RemoteSrc(path, false, 256 * 1024));
        }
        return sources;
    }

    std::unique_ptr<COperation> MakeDownloadTo(const std::vector<CRemoteSource>& sources, const std::string& folder)
    {
        COperationSettings settings = Settings(TransferDirection::Download, false, 2);
        settings.localRoot = (local_ / folder).wstring();
        fs::create_directories(local_ / folder);
        return std::make_unique<COperation>(settings, Params(),
                                            COperation::BuildDownloadRoots(sources, settings, CNameCodec::Auto()));
    }

    // An operation whose single worker keeps its slot: the proxy stalls in the middle of a big file.
    void StartHolder(std::unique_ptr<COperation>& op, CTestObserver& observer, sftptest::CStallProxy& proxy)
    {
        PutRemoteFile(remoteDir_ + "/big.bin", PatternBytes(24 << 20, 21));
        proxy.StallAfter(4 << 20);
        port_ = proxy.Port();
        op = MakeDownload({RemoteSrc(remoteDir_ + "/big.bin", false, 24 << 20)}, false, 1);
        op->SetObserver(&observer);
        ASSERT_TRUE(op->Start());
        ASSERT_TRUE(WaitUntil([&] { return proxy.IsStalled(); }, 60000));
        port_ = sftptest::kPort;
    }

    CGlobalConfig saved_;
    bool configSaved_ = false;
};
} // namespace

TEST_F(OperationSlotsTest, ThreeOperationsWithTwoWorkersNeverUseMoreThanThreeSlots)
{
    SetCap(3);
    std::vector<std::unique_ptr<CTestObserver>> observers; // first: destroyed last, the operations still call them
    std::vector<std::unique_ptr<COperation>> operations;
    for (int i = 0; i < 3; ++i)
    {
        const std::string folder = "set" + std::to_string(i);
        operations.push_back(MakeDownloadTo(PutManyFiles(folder, 12), folder));
        observers.push_back(std::make_unique<CTestObserver>());
        operations.back()->SetObserver(observers.back().get());
    }
    for (auto& op : operations)
        ASSERT_TRUE(op->Start());
    for (auto& op : operations)
    {
        ASSERT_TRUE(op->WaitFinished(120000));
        EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    }
    EXPECT_LE(WorkerSlots().Peak(), 3);
    EXPECT_GE(WorkerSlots().Peak(), 2); // the operations did run in parallel
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(fs::exists(local_ / ("set" + std::to_string(i)) / "f11.bin"));

    // idle workers of a finished operation give their slots back
    EXPECT_TRUE(WaitUntil([] { return WorkerSlots().Held() == 0; }, 5000));
}

TEST_F(OperationSlotsTest, ASecondOperationWaitsForTheSlotAndRunsWhenItIsFree)
{
    SetCap(1);
    sftptest::CStallProxy proxy(sftptest::kPort);
    std::unique_ptr<COperation> holder;
    CTestObserver holderObserver;
    StartHolder(holder, holderObserver, proxy);
    EXPECT_EQ(WorkerSlots().Held(), 1);
    EXPECT_FALSE(holder->IsWaitingForSlot());

    const std::vector<CRemoteSource> sources = PutManyFiles("waiter", 4);
    auto waiter = MakeDownloadTo(sources, "waiter");
    CTestObserver waiterObserver;
    waiter->SetObserver(&waiterObserver);
    ASSERT_TRUE(waiter->Start());
    ASSERT_TRUE(WaitUntil([&] { return waiter->IsWaitingForSlot(); }, 10000));
    EXPECT_EQ(WorkerSlots().Waiting(), 2); // both workers wait
    EXPECT_EQ(waiter->State(), OperationState::Running);
    EXPECT_FALSE(waiter->IsFinished());
    EXPECT_EQ(waiter->Summary().totals.filesDone, 0);

    holder->RequestCancel(); // gives its slot back
    ASSERT_TRUE(holder->WaitFinished(30000));
    ASSERT_TRUE(waiter->WaitFinished(60000));
    EXPECT_EQ(waiter->State(), OperationState::Finished) << Describe(*waiter);
    EXPECT_FALSE(waiter->IsWaitingForSlot());
    EXPECT_LE(WorkerSlots().Peak(), 1);
}

TEST_F(OperationSlotsTest, WithOneFreeSlotTwoWorkersRunAsOneAndTheOtherJoinsWhenASlotFrees)
{
    SetCap(2);
    sftptest::CStallProxy proxy(sftptest::kPort);
    std::unique_ptr<COperation> holder;
    CTestObserver holderObserver;
    StartHolder(holder, holderObserver, proxy);

    const std::vector<CRemoteSource> sources = PutManyFiles("two", 6);
    auto op = MakeDownloadTo(sources, "two");
    CTestObserver observer;
    op->SetObserver(&observer);
    ASSERT_TRUE(op->Start());
    ASSERT_TRUE(WaitUntil([&] { return WorkerSlots().Waiting() == 1; }, 10000)); // one worker has the free slot
    EXPECT_FALSE(op->IsWaitingForSlot()); // it runs, so it is not "waiting"
    ASSERT_TRUE(WaitUntil([&] { return op->Summary().totals.filesDone > 0; }, 30000));

    holder->RequestCancel(); // the second worker may join now
    ASSERT_TRUE(op->WaitFinished(60000));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_LE(WorkerSlots().Peak(), 2);
}

// The idle grace period of a worker is 3 seconds (kIdleReleaseMs in op_worker.cpp), so the waits below are long.
TEST_F(OperationSlotsTest, AnIdleWorkerOfARunningOperationGivesItsSlotBackAndTakesOneForNewWork)
{
    SetCap(2);
    sftptest::CStallProxy proxy(sftptest::kPort);
    PutRemoteFile(remoteDir_ + "/big.bin", PatternBytes(24 << 20, 21));
    // Only the connection of the big file freezes: the other worker may connect (even late, on a loaded machine) and
    // fail its missing file without being caught in the stall, so it is surely the idle one.
    proxy.FreezeConnectionAfter(4 << 20);
    port_ = proxy.Port();
    // worker 1 stalls in the big file; worker 2 gets the missing file, fails at once and has nothing more to do
    auto op = MakeDownload({RemoteSrc(remoteDir_ + "/big.bin", false, 24 << 20),
                            RemoteSrc(remoteDir_ + "/missing.bin", false, 10)},
                           false, 2);
    CTestObserver observer;
    op->SetObserver(&observer);
    ASSERT_TRUE(op->Start());
    ASSERT_TRUE(WaitUntil([&] { return proxy.IsStalled(); }, 60000));
    port_ = sftptest::kPort;
    ASSERT_TRUE(WaitUntil([] { return WorkerSlots().Held() == 2; }, 10000));
    ASSERT_TRUE(WaitUntil([&] { return op->Summary().counters.failed == 1; }, 15000)); // the idle one is really idle

    // the idle worker gives its slot back while the operation still runs
    ASSERT_TRUE(WaitUntil([] { return WorkerSlots().Held() == 1; }, 15000));
    EXPECT_EQ(op->State(), OperationState::Running);
    {
        CSlotLease other(WorkerSlots()); // another operation can have the freed slot
        EXPECT_TRUE(other.TryAcquire());
        EXPECT_EQ(WorkerSlots().Held(), 2);
    }
    EXPECT_EQ(WorkerSlots().Held(), 1);

    // new work: the idle worker takes a slot again
    proxy.Stall(false); // its new connection goes through the proxy too
    EXPECT_EQ(op->RetryAllFailed(), 1);
    ASSERT_TRUE(WaitUntil([] { return WorkerSlots().Held() == 2; }, 15000));

    op->RequestCancel();
    ASSERT_TRUE(op->WaitFinished(30000));
    EXPECT_TRUE(WaitUntil([] { return WorkerSlots().Held() == 0; }, 5000));
}

TEST_F(OperationSlotsTest, CancelAndPauseWhileWaitingForASlot)
{
    SetCap(1);
    sftptest::CStallProxy proxy(sftptest::kPort);
    std::unique_ptr<COperation> holder;
    CTestObserver holderObserver;
    StartHolder(holder, holderObserver, proxy);

    const std::vector<CRemoteSource> sources = PutManyFiles("waiter", 3);
    auto waiter = MakeDownloadTo(sources, "waiter");
    CTestObserver observer;
    waiter->SetObserver(&observer);
    ASSERT_TRUE(waiter->Start());
    ASSERT_TRUE(WaitUntil([&] { return waiter->IsWaitingForSlot(); }, 10000));

    waiter->Pause(); // the waiters step aside; nothing was taken from the queue
    ASSERT_TRUE(WaitUntil([&] { return WorkerSlots().Waiting() == 0; }, 5000));
    EXPECT_TRUE(waiter->IsPaused());
    EXPECT_FALSE(waiter->IsWaitingForSlot());
    EXPECT_EQ(waiter->Summary().totals.filesDone, 0);
    EXPECT_EQ(WorkerSlots().Held(), 1);

    waiter->Resume(); // they queue for the slot again
    ASSERT_TRUE(WaitUntil([&] { return waiter->IsWaitingForSlot(); }, 10000));

    waiter->RequestCancel();
    ASSERT_TRUE(waiter->WaitFinished(10000));
    EXPECT_EQ(WorkerSlots().Held(), 1); // only the holder's: the cancelled waiters leaked nothing
    EXPECT_EQ(WorkerSlots().Waiting(), 0);

    holder->RequestCancel();
    ASSERT_TRUE(holder->WaitFinished(30000));
    EXPECT_TRUE(WaitUntil([] { return WorkerSlots().Held() == 0; }, 5000));
}

TEST_F(OperationSlotsTest, SlotsAreReleasedWhenTheOperationIsDestroyed)
{
    SetCap(2);
    sftptest::CStallProxy proxy(sftptest::kPort);
    std::unique_ptr<COperation> holder;
    CTestObserver holderObserver;
    StartHolder(holder, holderObserver, proxy);
    EXPECT_EQ(WorkerSlots().Held(), 1);
    holder.reset(); // cancels and joins
    EXPECT_EQ(WorkerSlots().Held(), 0);
}

TEST_F(OperationSlotsTest, ChangingTheCapAtRunTimeLetsWaitersIn)
{
    SetCap(1);
    sftptest::CStallProxy proxy(sftptest::kPort);
    std::unique_ptr<COperation> holder;
    CTestObserver holderObserver;
    StartHolder(holder, holderObserver, proxy);

    const std::vector<CRemoteSource> sources = PutManyFiles("waiter", 3);
    auto waiter = MakeDownloadTo(sources, "waiter");
    CTestObserver observer;
    waiter->SetObserver(&observer);
    ASSERT_TRUE(waiter->Start());
    ASSERT_TRUE(WaitUntil([&] { return waiter->IsWaitingForSlot(); }, 10000));

    SetCap(4); // no restart: the waiting workers notice the new limit
    ASSERT_TRUE(waiter->WaitFinished(60000));
    EXPECT_EQ(waiter->State(), OperationState::Finished) << Describe(*waiter);
    EXPECT_EQ(holder->State(), OperationState::Running); // the holder was not touched

    holder->RequestCancel();
    ASSERT_TRUE(holder->WaitFinished(30000));
}
