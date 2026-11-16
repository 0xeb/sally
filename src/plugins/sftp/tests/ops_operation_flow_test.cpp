// Tests for the life of a COperation: cancel and resume, pause, reconnect with backoff, host key and prompt
// interactions, and COperationsList. Docker test server; a CStallProxy plays the server that goes silent.
#include "operation_test_support.h"

#include "operations_list.h"

namespace
{
// A "server" that closes every connection at once: the SSH handshake fails with ConnectFailed, immediately (a closed
// port would take Windows about two seconds per attempt to refuse). Counts the attempts.
class CClosingServer
{
public:
    CClosingServer()
    {
        sftptest::EnsureWinsock();
        listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        inet_pton(AF_INET, sftptest::kHost, &addr.sin_addr);
        bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(listen_, 8);
        int len = sizeof(addr);
        getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { Loop(); });
    }
    ~CClosingServer()
    {
        stop_ = true;
        thread_.join();
        closesocket(listen_);
    }
    CClosingServer(const CClosingServer&) = delete;
    CClosingServer& operator=(const CClosingServer&) = delete;

    int Port() const { return port_; }
    int Attempts() const { return attempts_.load(); }

private:
    void Loop()
    {
        while (!stop_)
        {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listen_, &readSet);
            timeval slice = {0, 20 * 1000};
            if (select(0, &readSet, nullptr, nullptr, &slice) <= 0)
                continue;
            SOCKET c = accept(listen_, nullptr, nullptr);
            if (c == INVALID_SOCKET)
                continue;
            ++attempts_;
            closesocket(c);
        }
    }

    SOCKET listen_ = INVALID_SOCKET;
    int port_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> attempts_{0};
    std::thread thread_;
};

class OperationFlowTest : public OperationTest
{
protected:

    // Makes the first byte of a local file wrong: a later resume keeps it, a restart does not.
    static void CorruptFirstByte(const fs::path& path)
    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(file.is_open());
        file.put('Z');
    }

    // A 24 MiB remote file "big.bin" in the test folder; returns its content.
    std::string PutBigRemoteFile(unsigned seed)
    {
        const std::string content = PatternBytes(24 << 20, seed);
        PutRemoteFile(remoteDir_ + "/big.bin", content);
        return content;
    }

    std::unique_ptr<COperation> MakeBigDownload()
    {
        return MakeDownload({RemoteSrc(remoteDir_ + "/big.bin", false, 24 << 20)}, false, 1);
    }

    // Starts the operation and waits until the proxy went silent after the limit; the transfer is then in the middle.
    static bool StartAndWaitForStall(COperation& op, CTestObserver& observer, sftptest::CStallProxy& proxy)
    {
        op.SetObserver(&observer);
        if (!op.Start())
            return false;
        if (!WaitUntil([&] { return proxy.IsStalled(); }, 60000))
            return false;
        Sleep(300); // what was in flight arrives
        return true;
    }

    static ItemState StateOfFirstItem(COperation& op)
    {
        const std::vector<CQueueItem> items = op.Queue().Items(false);
        return items.empty() ? ItemState::Done : items[0].state;
    }
};
} // namespace

// ---- cancel and resume ---------------------------------------------------------------------------------------------

TEST_F(OperationFlowTest, CancelMidFileThenANewOperationResumesTheLocalFile)
{
    const std::string content = PutBigRemoteFile(11);
    sftptest::CStallProxy proxy(sftptest::kPort);
    proxy.StallAfter(8 << 20);
    port_ = proxy.Port();

    {
        auto op = MakeBigDownload();
        CTestObserver observer;
        ASSERT_TRUE(StartAndWaitForStall(*op, observer, proxy));
        op->RequestCancel();
        ASSERT_TRUE(op->WaitFinished(30000));
        const std::vector<CQueueItem> items = op->Queue().Items(false);
        ASSERT_EQ(items.size(), 1u);
        EXPECT_EQ(items[0].state, ItemState::Waiting);
        // items[0].bytesDone is not checked: the progress callback fires at most every 100 ms, and an optimized build
        // moves the first 8 MiB over loopback faster than that. The partial file below is what resume uses.
        EXPECT_TRUE(items[0].ownsPartial);
    }
    const uint64_t partial = fs::file_size(local_ / "big.bin");
    EXPECT_GT(partial, 0u);
    EXPECT_LT(partial, content.size());

    CorruptFirstByte(local_ / "big.bin");
    port_ = sftptest::kPort;
    policy_ = ConflictPolicy::Resume;
    auto again = MakeBigDownload();
    CTestObserver observer;
    ASSERT_TRUE(Run(*again, observer));
    EXPECT_EQ(again->State(), OperationState::Finished) << Describe(*again);
    EXPECT_TRUE(ReadAll(local_ / "big.bin") == "Z" + content.substr(1)); // continued after the partial size
}

// ---- pause ---------------------------------------------------------------------------------------------------------

TEST_F(OperationFlowTest, PauseStopsTheTransferAndResumeContinuesItsOwnPartialFile)
{
    const std::string content = PutBigRemoteFile(12);
    sftptest::CStallProxy proxy(sftptest::kPort);
    proxy.StallAfter(8 << 20);
    port_ = proxy.Port();
    // policy Ask: the half-written file is the operation's own, so it is not asked about

    auto op = MakeBigDownload();
    CTestObserver observer;
    ASSERT_TRUE(StartAndWaitForStall(*op, observer, proxy));
    op->Pause(); // the network is silent: the pause must not wait for the idle timeout
    ASSERT_TRUE(WaitUntil([&] { return StateOfFirstItem(*op) == ItemState::Waiting; }, 20000));
    EXPECT_TRUE(op->IsPaused());
    EXPECT_EQ(op->State(), OperationState::Running);

    // the server could answer again, but the paused operation does nothing
    proxy.StallAfter(0);
    proxy.Stall(false);
    const uint64_t done = op->Statistics().bytesDone;
    const int progress = observer.progress.load();
    EXPECT_GT(done, 0u);
    Sleep(500);
    EXPECT_EQ(op->Statistics().bytesDone, done);
    EXPECT_EQ(observer.progress.load(), progress);
    EXPECT_EQ(StateOfFirstItem(*op), ItemState::Waiting);
    EXPECT_FALSE(op->IsFinished());

    CorruptFirstByte(local_ / "big.bin");
    op->Resume();
    EXPECT_FALSE(op->IsPaused());
    ASSERT_TRUE(op->WaitFinished(60000));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_TRUE(ReadAll(local_ / "big.bin") == "Z" + content.substr(1)); // not restarted, not asked
}

// Review 12: the user solved a parked item with "Overwrite"; the answer is kept on the item, but it must only restart the
// file once. A pause after that continues the operation's own half-written file instead of starting again.
TEST_F(OperationFlowTest, PauseAfterASolveOverwriteContinuesTheOwnPartialFile)
{
    const std::string content = PutBigRemoteFile(14);
    WriteAll(local_ / "big.bin", "OLD"); // policy Ask: the item is parked first
    sftptest::CStallProxy proxy(sftptest::kPort);
    proxy.StallAfter(8 << 20);
    port_ = proxy.Port();

    auto op = MakeBigDownload();
    CTestObserver observer;
    op->SetObserver(&observer);
    ASSERT_TRUE(op->Start());
    ASSERT_TRUE(WaitUntil([&] { return StateOfFirstItem(*op) == ItemState::UserInputNeeded; }, 30000));
    const std::vector<CQueueItem> parked = op->Queue().Items(false);
    ASSERT_EQ(parked.size(), 1u);
    EXPECT_EQ(op->Solve(parked[0].uid, SolveAction::Overwrite), 1);
    ASSERT_TRUE(WaitUntil([&] { return proxy.IsStalled(); }, 60000));
    Sleep(300);

    op->Pause();
    ASSERT_TRUE(WaitUntil([&] { return StateOfFirstItem(*op) == ItemState::Waiting; }, 20000));
    CorruptFirstByte(local_ / "big.bin");
    proxy.StallAfter(0);
    proxy.Stall(false);
    op->Resume();
    ASSERT_TRUE(op->WaitFinished(60000));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_TRUE(ReadAll(local_ / "big.bin") == "Z" + content.substr(1)); // continued, not downloaded again
}

TEST_F(OperationFlowTest, PausedUploadContinuesItsOwnFilepartWhateverThePolicy)
{
    const std::string source = PatternBytes(24 << 20, 13);
    WriteAll(local_ / "src" / "up.bin", source);
    sftptest::CStallProxy proxy(sftptest::kPort);
    proxy.StallAfter(8 << 20);
    port_ = proxy.Port();

    auto op = MakeUpload({LocalSrc(local_ / "src" / "up.bin", false)}, remoteDir_, false, 1);
    CTestObserver observer;
    ASSERT_TRUE(StartAndWaitForStall(*op, observer, proxy));
    op->Pause();
    ASSERT_TRUE(WaitUntil([&] { return StateOfFirstItem(*op) == ItemState::Waiting; }, 20000));
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/up.bin")); // not there before the rename

    CSftpEntry part;
    ASSERT_TRUE(admin_.Stat(remoteDir_ + "/up.bin.filepart", part));
    ASSERT_GT(part.size, 0u);
    ASSERT_LT(part.size, source.size());
    // the same bytes with a wrong first one: only a real continuation keeps it
    PutRemoteFile(remoteDir_ + "/up.bin.filepart", "Z" + source.substr(1, part.size - 1));

    proxy.StallAfter(0);
    proxy.Stall(false);
    op->Resume();
    ASSERT_TRUE(op->WaitFinished(60000));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_TRUE(GetRemoteFile(remoteDir_ + "/up.bin") == "Z" + source.substr(1));
    EXPECT_FALSE(RemoteExists(remoteDir_ + "/up.bin.filepart"));
}

// The same for an upload without safe upload: the target itself is the partial file.
TEST_F(OperationFlowTest, PauseAfterASolveOverwriteContinuesTheOwnPartialUploadWithoutSafeUpload)
{
    safeUpload_ = false;
    const std::string source = PatternBytes(24 << 20, 15);
    WriteAll(local_ / "src" / "up.bin", source);
    PutRemoteFile(remoteDir_ + "/up.bin", "OLD"); // policy Ask: the item is parked first
    sftptest::CStallProxy proxy(sftptest::kPort);
    proxy.StallAfter(8 << 20);
    port_ = proxy.Port();

    auto op = MakeUpload({LocalSrc(local_ / "src" / "up.bin", false)}, remoteDir_, false, 1);
    CTestObserver observer;
    op->SetObserver(&observer);
    ASSERT_TRUE(op->Start());
    ASSERT_TRUE(WaitUntil([&] { return StateOfFirstItem(*op) == ItemState::UserInputNeeded; }, 30000));
    const std::vector<CQueueItem> parked = op->Queue().Items(false);
    ASSERT_EQ(parked.size(), 1u);
    EXPECT_EQ(op->Solve(parked[0].uid, SolveAction::Overwrite), 1);
    ASSERT_TRUE(WaitUntil([&] { return proxy.IsStalled(); }, 60000));
    Sleep(300);

    op->Pause();
    ASSERT_TRUE(WaitUntil([&] { return StateOfFirstItem(*op) == ItemState::Waiting; }, 20000));
    CSftpEntry part;
    ASSERT_TRUE(admin_.Stat(remoteDir_ + "/up.bin", part));
    ASSERT_GT(part.size, 3u);
    ASSERT_LT(part.size, source.size());
    // the same bytes with a wrong first one: only a real continuation keeps it
    PutRemoteFile(remoteDir_ + "/up.bin", "Z" + source.substr(1, part.size - 1));
    proxy.StallAfter(0);
    proxy.Stall(false);
    op->Resume();
    ASSERT_TRUE(op->WaitFinished(60000));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_TRUE(GetRemoteFile(remoteDir_ + "/up.bin") == "Z" + source.substr(1)); // continued, not uploaded again
}

TEST_F(OperationFlowTest, AnOperationPausedBeforeItsWorkersStartedTakesNothingUntilResume)
{
    const DWORD handlesBefore = HandleCount();
    {
        auto op = MakeDownload({RemoteSrc("fixtures/small", true)});
        CTestObserver observer;
        op->SetObserver(&observer);
        op->Pause();
        ASSERT_TRUE(op->Start());
        Sleep(500);
        EXPECT_FALSE(fs::exists(local_ / "small"));
        EXPECT_FALSE(op->IsFinished());
        for (const CQueueItem& item : op->Queue().Items(false)) // the folder and its explore item, untouched
            EXPECT_EQ(item.state, ItemState::Waiting);

        op->Resume();
        ASSERT_TRUE(op->WaitFinished(30000));
        EXPECT_EQ(op->State(), OperationState::Finished);
        EXPECT_EQ(ReadAll(local_ / "small" / "hello.txt"), "hello\n");

        // pause again while nothing is left, then cancel while paused: everything wakes up and ends
        op->Pause();
        op->RequestCancel();
        EXPECT_TRUE(op->WaitFinished(10000));
    }
    EXPECT_LE(HandleCount(), handlesBefore + 2);
}

TEST_F(OperationFlowTest, CancelWhilePausedEndsTheOperation)
{
    auto op = MakeDownload({RemoteSrc("fixtures/many", true)}, false, 3);
    CTestObserver observer;
    op->SetObserver(&observer);
    op->Pause();
    ASSERT_TRUE(op->Start());
    Sleep(200);
    const auto start = std::chrono::steady_clock::now();
    op->RequestCancel();
    EXPECT_TRUE(op->WaitFinished(10000));
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(3));
    EXPECT_EQ(observer.finished.load(), 1);
}

// ---- connection loss -----------------------------------------------------------------------------------------------

TEST_F(OperationFlowTest, ReconnectsAfterTheServerWentSilentAndContinuesTheFile)
{
    const std::string content = PutBigRemoteFile(14);
    sftptest::CStallProxy proxy(sftptest::kPort);
    proxy.StallAfter(8 << 20);
    port_ = proxy.Port();
    timeoutSeconds_ = 1;              // the stall is noticed after one second
    delays_ = {1500, 10, 10};         // and the first reconnect waits 1.5 s

    auto op = MakeBigDownload();
    CTestObserver observer;
    op->SetObserver(&observer);
    ASSERT_TRUE(op->Start());
    ASSERT_TRUE(WaitUntil([&] { return proxy.IsStalled(); }, 60000));
    Sleep(1300); // idle timeout hit, worker is in the backoff wait: the item stays Processing
    EXPECT_EQ(StateOfFirstItem(*op), ItemState::Processing);
    proxy.StallAfter(0);
    proxy.Stall(false);

    ASSERT_TRUE(op->WaitFinished(60000));
    EXPECT_EQ(op->State(), OperationState::Finished) << Describe(*op);
    EXPECT_EQ(observer.finished.load(), 1);
    EXPECT_TRUE(ReadAll(local_ / "big.bin") == content);
}

TEST_F(OperationFlowTest, ParksAnItemAfterThreeReconnectAttemptsAndLetsTheOthersFailFast)
{
    CClosingServer server; // every connect attempt ends at once
    port_ = server.Port();
    delays_ = {150, 150, 150};
    auto op = MakeDownload({RemoteSrc("fixtures/small/hello.txt", false, 6), RemoteSrc("fixtures/small/random1k.bin", false, 1024)},
                           false, 1);
    CTestObserver observer;
    op->SetObserver(&observer);
    const auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(op->Start());

    ASSERT_TRUE(WaitUntil([&] { return server.Attempts() >= 1; }, 5000));
    Sleep(100);
    EXPECT_EQ(StateOfFirstItem(*op), ItemState::Processing); // waiting for its next attempt, not parked

    ASSERT_TRUE(op->WaitFinished(30000));
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(440)); // three waits of 150 ms
    EXPECT_EQ(op->State(), OperationState::FinishedWithErrors);
    // the first item: one attempt and three reconnects; the second one only a single try (the server is known to be away)
    EXPECT_EQ(server.Attempts(), 5);
    const std::vector<CQueueItem> failed = ItemsWithProblem(*op, Problem::ConnectionLost);
    ASSERT_EQ(failed.size(), 2u);
    for (const CQueueItem& item : failed)
        EXPECT_EQ(item.state, ItemState::Failed); // Failed: RetryAllFailed brings them back

    // the server is still away: retrying fails again (one attempt each)
    EXPECT_EQ(op->RetryAllFailed(), 2);
    ASSERT_TRUE(WaitUntil([&] { return observer.finished.load() == 2; }, 30000));
    EXPECT_EQ(op->State(), OperationState::FinishedWithErrors);
    EXPECT_EQ(server.Attempts(), 7);
}

TEST_F(OperationFlowTest, ACancelInTheBackoffWaitEndsTheOperationAtOnce)
{
    CClosingServer server;
    port_ = server.Port();
    delays_ = {60000, 60000, 60000}; // a minute: only the cancel can end this in time
    auto op = MakeDownload({RemoteSrc("fixtures/small/hello.txt", false, 6)}, false, 1);
    CTestObserver observer;
    op->SetObserver(&observer);
    ASSERT_TRUE(op->Start());
    ASSERT_TRUE(WaitUntil([&] { return server.Attempts() >= 1; }, 5000));
    Sleep(200);
    EXPECT_EQ(StateOfFirstItem(*op), ItemState::Processing);
    op->RequestCancel();
    EXPECT_TRUE(op->WaitFinished(5000));
    EXPECT_EQ(StateOfFirstItem(*op), ItemState::Waiting); // given back, not failed
}

// ---- interactions --------------------------------------------------------------------------------------------------

TEST_F(OperationFlowTest, AnUnknownHostKeyIsPublishedAndAcceptOnceTrustsItForAllWorkers)
{
    CPanelConnectParams params = Params();
    params.hostKeyFingerprint = "SHA256:not-the-key"; // the panel saw another key
    params.knownHostsOwnPath = (local_ / "kh_once").string();
    const COperationSettings settings = Settings(TransferDirection::Download, false, 2);
    COperation op(settings, std::move(params),
                  COperation::BuildDownloadRoots({RemoteSrc("fixtures/small", true), RemoteSrc("fixtures/nested", true)},
                                                 settings, CNameCodec::Auto()));
    CTestObserver observer;
    op.SetObserver(&observer);
    ASSERT_TRUE(op.Start());
    ASSERT_TRUE(WaitUntil([&] { return observer.interactions.load() > 0; }, 20000));

    CInteraction ask;
    int worker = -1;
    ASSERT_TRUE(op.TryGetInteraction(ask, &worker));
    EXPECT_EQ(ask.kind, CInteraction::Kind::HostKey);
    EXPECT_EQ(ask.fingerprint, admin_.HostKeyFingerprint());
    EXPECT_EQ(ask.host, "127.0.0.1");
    EXPECT_EQ(ask.port, sftptest::kPort);
    EXPECT_TRUE(ask.changed); // the panel pinned another key: a changed key, whatever the known_hosts files say
    EXPECT_GE(worker, 0);
    EXPECT_NE(ask.threadId, std::this_thread::get_id()); // asked on a worker thread
    CInteraction again;
    EXPECT_FALSE(op.TryGetInteraction(again)); // handed out once
    EXPECT_FALSE(op.AnswerPrompt(true, CSecret("x"))); // wrong kind

    EXPECT_TRUE(op.AnswerHostKey(HostKeyDecision::AcceptOnce));
    EXPECT_FALSE(op.AnswerHostKey(HostKeyDecision::AcceptOnce)); // answered already

    ASSERT_TRUE(op.WaitFinished(60000));
    EXPECT_EQ(op.State(), OperationState::Finished) << Describe(op);
    EXPECT_EQ(observer.interactions.load(), 1); // the second worker did not ask again
    EXPECT_EQ(ReadAll(local_ / "small" / "hello.txt"), "hello\n");
    EXPECT_EQ(ReadAll(local_ / "nested" / "a" / "b" / "c" / "deep.txt"), "deep\n");
    EXPECT_FALSE(fs::exists(local_ / "kh_once")); // accepted once: nothing saved
}

// "Accept and save" writes the plugin's known_hosts file; the next operation finds the key there and asks nothing, even
// with a different pinned key (the file vouches for it).
TEST_F(OperationFlowTest, AcceptAndSaveWritesTheKnownHostsFileAndTheNextOperationDoesNotAsk)
{
    const fs::path hostsFile = local_ / "kh_saved";
    {
        CPanelConnectParams params = Params();
        params.hostKeyFingerprint.clear();
        params.knownHostsOwnPath = hostsFile.string();
        const COperationSettings settings = Settings(TransferDirection::Download, false, 1);
        COperation op(settings, std::move(params),
                      COperation::BuildDownloadRoots({RemoteSrc("fixtures/small/hello.txt", false, 6)}, settings,
                                                     CNameCodec::Auto()));
        CTestObserver observer;
        op.SetObserver(&observer);
        ASSERT_TRUE(op.Start());
        ASSERT_TRUE(WaitUntil([&] { return observer.interactions.load() > 0; }, 20000));
        CInteraction ask;
        ASSERT_TRUE(op.TryGetInteraction(ask));
        EXPECT_FALSE(ask.changed); // no pinned key, nothing in the files: just unknown
        EXPECT_TRUE(op.AnswerHostKey(HostKeyDecision::AcceptAndSave));
        ASSERT_TRUE(op.WaitFinished(30000));
        EXPECT_EQ(op.State(), OperationState::Finished) << Describe(op);
    }
    const std::string saved = ReadAll(hostsFile);
    EXPECT_NE(saved.find("[127.0.0.1]:" + std::to_string(sftptest::kPort) + " ssh-ed25519 "), std::string::npos) << saved;

    fs::remove(local_ / "hello.txt"); // the first operation downloaded it
    CPanelConnectParams params = Params();
    params.hostKeyFingerprint = "SHA256:not-the-key";
    params.knownHostsOwnPath = hostsFile.string();
    const COperationSettings settings = Settings(TransferDirection::Download, false, 1);
    COperation op(settings, std::move(params),
                  COperation::BuildDownloadRoots({RemoteSrc("fixtures/small/hello.txt", false, 6)}, settings,
                                                 CNameCodec::Auto()));
    CTestObserver observer;
    ASSERT_TRUE(Run(op, observer));
    EXPECT_EQ(observer.interactions.load(), 0);
    EXPECT_EQ(op.State(), OperationState::Finished) << Describe(op);
}

// A key that the known_hosts file knows differently is reported as changed even without a pinned key.
TEST_F(OperationFlowTest, AWrongKeyInTheKnownHostsFileIsReportedAsChanged)
{
    const fs::path hostsFile = local_ / "kh_wrong";
    WriteAll(hostsFile, "[127.0.0.1]:" + std::to_string(sftptest::kPort) +
                            " ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhscHR4f\n");
    CPanelConnectParams params = Params();
    params.hostKeyFingerprint.clear();
    params.knownHostsOwnPath = hostsFile.string();
    const COperationSettings settings = Settings(TransferDirection::Download, false, 1);
    COperation op(settings, std::move(params),
                  COperation::BuildDownloadRoots({RemoteSrc("fixtures/small/hello.txt", false, 6)}, settings,
                                                 CNameCodec::Auto()));
    CTestObserver observer;
    op.SetObserver(&observer);
    ASSERT_TRUE(op.Start());
    ASSERT_TRUE(WaitUntil([&] { return observer.interactions.load() > 0; }, 20000));
    CInteraction ask;
    ASSERT_TRUE(op.TryGetInteraction(ask));
    EXPECT_TRUE(ask.changed);
    EXPECT_TRUE(op.AnswerHostKey(HostKeyDecision::Reject));
    ASSERT_TRUE(WaitUntil([&] { return op.IsPaused(); }, 10000));
}

TEST_F(OperationFlowTest, RejectingTheHostKeyPausesTheOperationAndResumeAsksAgain)
{
    CPanelConnectParams params = Params();
    params.hostKeyFingerprint = "SHA256:not-the-key";
    const COperationSettings settings = Settings(TransferDirection::Download, false, 1);
    COperation op(settings, std::move(params),
                  COperation::BuildDownloadRoots({RemoteSrc("fixtures/small/hello.txt", false, 6)}, settings,
                                                 CNameCodec::Auto()));
    CTestObserver observer;
    op.SetObserver(&observer);
    ASSERT_TRUE(op.Start());
    ASSERT_TRUE(WaitUntil([&] { return observer.interactions.load() > 0; }, 20000));
    CInteraction ask;
    ASSERT_TRUE(op.TryGetInteraction(ask));
    EXPECT_TRUE(op.AnswerHostKey(HostKeyDecision::Reject));

    // a refused key would be refused for every item: the operation pauses and the item keeps its state
    ASSERT_TRUE(WaitUntil([&] { return op.IsPaused(); }, 10000));
    EXPECT_FALSE(op.PauseReason().empty());
    EXPECT_EQ(op.State(), OperationState::Running);
    EXPECT_EQ(ItemsWithProblem(op, Problem::ConnectionLost).size(), 0u);
    EXPECT_FALSE(fs::exists(local_ / "hello.txt"));

    op.Resume(); // tries the login again: the key is asked again
    ASSERT_TRUE(WaitUntil([&] { return observer.interactions.load() > 1; }, 20000));
    EXPECT_TRUE(op.PauseReason().empty());
    ASSERT_TRUE(op.TryGetInteraction(ask));
    EXPECT_TRUE(op.AnswerHostKey(HostKeyDecision::AcceptOnce));
    ASSERT_TRUE(op.WaitFinished(30000));
    EXPECT_EQ(op.State(), OperationState::Finished) << Describe(op);
    EXPECT_EQ(ReadAll(local_ / "hello.txt"), "hello\n");
}

// A wrong password fails the same way for every item: the operation pauses after the first attempts instead of logging
// in again for each of the files.
TEST_F(OperationFlowTest, AWrongPasswordPausesTheOperationInsteadOfLoggingInPerItem)
{
    sftptest::CStallProxy proxy(sftptest::kPort); // counts the connections
    port_ = proxy.Port();
    for (int i = 0; i < 30; ++i)
        WriteAll(local_ / "many" / ("f" + std::to_string(i) + ".txt"), "x\n");
    CPanelConnectParams params = Params();
    params.password = CSecret("wrong password");
    const COperationSettings settings = Settings(TransferDirection::Upload, false, 2);
    COperation op(settings, std::move(params),
                  COperation::BuildUploadRoots({LocalSrc(local_ / "many", true)}, settings, CNameCodec::Auto()));
    CTestObserver observer;
    op.SetObserver(&observer);
    ASSERT_TRUE(op.Start());
    ASSERT_TRUE(WaitUntil([&] { return op.IsPaused(); }, 20000));
    Sleep(500); // would be enough for many more attempts

    EXPECT_LE(proxy.Accepted(), 4); // at most a few per worker (the first items), nowhere near 30
    EXPECT_FALSE(op.PauseReason().empty());
    EXPECT_EQ(op.State(), OperationState::Running);
    EXPECT_TRUE(op.Queue().Items(true).empty()) << "no item failed; they wait for Resume";
}

TEST_F(OperationFlowTest, AFailedItemKeepsTheSessionErrorSoTheUiCanWordItInTheUsersLanguage)
{
    delays_ = {10, 10, 10};
    CPanelConnectParams params = Params();
    params.site.host = L"no-such-host.invalid"; // .invalid never resolves (RFC 2606)
    const COperationSettings settings = Settings(TransferDirection::Download, false, 1);
    COperation op(settings, std::move(params),
                  COperation::BuildDownloadRoots({RemoteSrc("fixtures/small/hello.txt", false, 6)}, settings,
                                                 CNameCodec::Auto()));
    CTestObserver observer;
    op.SetObserver(&observer);
    ASSERT_TRUE(op.Start());
    ASSERT_TRUE(op.WaitFinished(60000));

    const std::vector<CQueueItem> failed = ItemsWithProblem(op, Problem::ConnectionLost);
    ASSERT_EQ(failed.size(), 1u);
    const CSshError& error = failed[0].sshError;
    // a very slow DNS could end as a timeout instead; both carry the host as the first argument
    ASSERT_TRUE(error.msgId == SshMsg::ResolveFailed || error.msgId == SshMsg::ResolveTimeout) << error.message;
    ASSERT_FALSE(error.args.empty());
    EXPECT_EQ(error.args[0], "no-such-host.invalid");
    EXPECT_FALSE(failed[0].errorText.empty()); // the English text stays as the fallback

    // a retry clears the stored error; the item fails again with a fresh one
    EXPECT_EQ(op.RetryAllFailed(), 1);
    ASSERT_TRUE(op.WaitFinished(60000));
    const std::vector<CQueueItem> again = ItemsWithProblem(op, Problem::ConnectionLost);
    ASSERT_EQ(again.size(), 1u);
    EXPECT_TRUE(again[0].sshError.IsError());
}

TEST_F(OperationFlowTest, DestroyingTheOperationWhileAWorkerWaitsForTheUserDoesNotHang)
{
    const DWORD handlesBefore = HandleCount();
    {
        CPanelConnectParams params = Params();
        params.hostKeyFingerprint = "SHA256:not-the-key";
        const COperationSettings settings = Settings(TransferDirection::Download, false, 3);
        auto op = std::make_unique<COperation>(
            settings, std::move(params),
            COperation::BuildDownloadRoots({RemoteSrc("fixtures/small", true)}, settings, CNameCodec::Auto()));
        CTestObserver observer;
        op->SetObserver(&observer);
        ASSERT_TRUE(op->Start());
        ASSERT_TRUE(WaitUntil([&] { return observer.interactions.load() > 0; }, 20000));
        const auto start = std::chrono::steady_clock::now();
        op.reset(); // nobody answers
        EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
    }
    EXPECT_LE(HandleCount(), handlesBefore + 2);
}

TEST_F(OperationFlowTest, APromptOfTheServerIsPublishedAndAnswered)
{
    if (!sftptest::ServerReachable(sftptest::kPortKbdInt))
        GTEST_SKIP() << "port 2224 not reachable";
    CPanelConnectParams params = Params();
    params.hostKeyFingerprint.clear(); // no pinned key: asked first
    params.password = CSecret();       // no stored password: the server asks for it
    port_ = sftptest::kPortKbdInt;
    params.site = TestSite(sftptest::kPortKbdInt);
    const COperationSettings settings = Settings(TransferDirection::Download, false, 1);
    COperation op(settings, std::move(params),
                  COperation::BuildDownloadRoots({RemoteSrc("fixtures/small/hello.txt", false, 6)}, settings,
                                                 CNameCodec::Auto()));
    CTestObserver observer;
    op.SetObserver(&observer);
    ASSERT_TRUE(op.Start());

    bool sawPrompt = false;
    for (int i = 0; i < 4000 && !op.WaitFinished(5); ++i)
    {
        CInteraction ask;
        if (!op.TryGetInteraction(ask))
            continue;
        if (ask.kind == CInteraction::Kind::HostKey)
        {
            EXPECT_TRUE(op.AnswerHostKey(HostKeyDecision::AcceptOnce));
        }
        else
        {
            sawPrompt = true;
            EXPECT_NE(ask.promptText.find("assword"), std::string::npos) << ask.promptText;
            EXPECT_FALSE(ask.echo);
            EXPECT_TRUE(op.AnswerPrompt(true, CSecret(sftptest::kPassword)));
        }
    }
    ASSERT_TRUE(op.WaitFinished(30000));
    EXPECT_TRUE(sawPrompt);
    EXPECT_EQ(op.State(), OperationState::Finished) << Describe(op);
    EXPECT_EQ(ReadAll(local_ / "hello.txt"), "hello\n");
}

TEST_F(OperationFlowTest, NothingIsAskedWhenTheOperationIsNotInteractive)
{
    CPanelConnectParams params = Params();
    params.hostKeyFingerprint = "SHA256:not-the-key";
    COperationSettings settings = Settings(TransferDirection::Download, false, 1);
    settings.interactive = false;
    COperation op(settings, std::move(params),
                  COperation::BuildDownloadRoots({RemoteSrc("fixtures/small/hello.txt", false, 6)}, settings,
                                                 CNameCodec::Auto()));
    CTestObserver observer;
    ASSERT_TRUE(Run(op, observer));
    EXPECT_EQ(observer.interactions.load(), 0);
    EXPECT_EQ(ItemsWithProblem(op, Problem::ConnectionLost).size(), 1u);
}

// ---- COperationsList -----------------------------------------------------------------------------------------------

TEST_F(OperationFlowTest, OperationsListAddsGetsRemovesAndCancelsEverything)
{
    const DWORD handlesBefore = HandleCount();
    {
        CTestObserver observer1; // declared first: destroyed last, after the list has ended the operations
        CTestObserver observer2;
        COperationsList list;
        std::shared_ptr<COperation> quick(MakeDownload({RemoteSrc("fixtures/small", true)}, false, 1).release());
        std::shared_ptr<COperation> huge(
            MakeDownload({RemoteSrc("fixtures/sparse5g.bin", false, 5ull << 30)}, false, 1).release());
        const uint64_t quickUid = list.Add(quick);
        const uint64_t hugeUid = list.Add(huge);
        EXPECT_NE(quickUid, hugeUid);
        EXPECT_EQ(list.Get(quickUid), quick);
        EXPECT_EQ(list.Get(hugeUid), huge);
        EXPECT_EQ(list.Get(9999), nullptr);
        EXPECT_TRUE(list.HasRunning()); // not started: not finished

        quick->SetObserver(&observer1);
        huge->SetObserver(&observer2);
        ASSERT_TRUE(quick->Start());
        ASSERT_TRUE(huge->Start());
        ASSERT_TRUE(quick->WaitFinished(30000));
        ASSERT_TRUE(WaitUntil([&] { return observer2.progress.load() > 0; }, 30000));

        const std::vector<COperationInfo> info = list.Snapshot();
        ASSERT_EQ(info.size(), 2u);
        EXPECT_EQ(info[0].uid, quickUid);
        EXPECT_TRUE(info[0].finished);
        EXPECT_EQ(info[0].state, OperationState::Finished);
        EXPECT_FALSE(info[1].finished);
        EXPECT_FALSE(info[1].cancelled);
        EXPECT_TRUE(list.HasRunning());
        EXPECT_FALSE(list.WaitAllFinished(100)); // the big one is still going
        EXPECT_FALSE(huge->IsDisposable());
        EXPECT_FALSE(list.RemoveFinished(hugeUid)); // running: stays
        EXPECT_FALSE(list.RemoveFinished(9999));
        EXPECT_NE(list.Get(hugeUid), nullptr);

        list.CancelAll();
        EXPECT_TRUE(list.WaitAllFinished(15000));
        EXPECT_FALSE(list.HasRunning());
        EXPECT_TRUE(huge->WasCancelled());

        EXPECT_TRUE(quick->IsDisposable());
        EXPECT_TRUE(huge->IsDisposable()); // cancelled and the workers are gone
        EXPECT_TRUE(list.RemoveFinished(quickUid));
        EXPECT_FALSE(list.RemoveFinished(quickUid));
        EXPECT_EQ(list.Get(quickUid), nullptr);
        EXPECT_EQ(list.Snapshot().size(), 1u);
        EXPECT_TRUE(list.Remove(hugeUid));
        EXPECT_FALSE(list.Remove(hugeUid));
    }
    EXPECT_LE(HandleCount(), handlesBefore + 2);
}

TEST_F(OperationFlowTest, ClearCancelsAndJoinsRunningOperations)
{
    const DWORD handlesBefore = HandleCount();
    {
        CTestObserver observer;
        COperationsList list;
        for (int i = 0; i < 3; ++i)
        {
            std::shared_ptr<COperation> op(MakeDownload({RemoteSrc("fixtures/many", true)}, false, 2).release());
            op->SetObserver(&observer);
            list.Add(op);
            ASSERT_TRUE(op->Start());
        }
        Sleep(150);
        const auto start = std::chrono::steady_clock::now();
        list.Clear(); // what Release does
        EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
        EXPECT_TRUE(list.Snapshot().empty());
        EXPECT_FALSE(list.HasRunning());
    }
    EXPECT_LE(HandleCount(), handlesBefore + 2);
}

TEST_F(OperationFlowTest, ClearEndsAWorkerThatSleepsInTheReconnectBackoff)
{
    const DWORD handlesBefore = HandleCount();
    {
        CTestObserver observer; // before the server and the list: the operation still calls it while Clear joins
        CClosingServer server;
        port_ = server.Port();
        delays_ = {60000, 60000, 60000}; // a minute: only Clear can end this in time
        COperationsList list;
        std::shared_ptr<COperation> op(MakeDownload({RemoteSrc("fixtures/small/hello.txt", false, 6)}, false, 2).release());
        op->SetObserver(&observer);
        list.Add(op);
        ASSERT_TRUE(op->Start());
        ASSERT_TRUE(WaitUntil([&] { return server.Attempts() >= 1; }, 5000));
        Sleep(200); // the worker is in its backoff wait
        {
            // the test is only meaningful when that is really so: the item is taken, and the server refused at least once
            const std::vector<CQueueItem> items = op->Queue().Items(false);
            ASSERT_EQ(items.size(), 1u);
            ASSERT_EQ(items[0].state, ItemState::Processing) << "the worker is not in its backoff wait";
            ASSERT_GE(server.Attempts(), 1);
        }
        const auto start = std::chrono::steady_clock::now();
        list.Clear();
        EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
        EXPECT_FALSE(list.HasRunning());
    }
    EXPECT_LE(HandleCount(), handlesBefore + 2);
}
