// CSshSession::Exec against the Docker test server (the command is run by the user's shell on the server).
#include "test_support.h"

#include <windows.h>

#include <chrono>
#include <memory>
#include <thread>

#include "ssh_init.h"
#include "ssh_session.h"

namespace
{
struct CQuiet : ISshCallbacks
{
    HostKeyDecision VerifyHostKey(const std::string&, int, const std::string&, const std::string&, bool) override
    {
        return HostKeyDecision::AcceptOnce;
    }
    bool Prompt(PromptKind, const std::string&, const std::string&, const std::string&, bool, CSecret&) override { return false; }
    bool Progress(uint64_t, uint64_t) override { return true; }
};

CSshSite ServerSite()
{
    CSshSite site;
    site.host = sftptest::kHost;
    site.port = sftptest::kPort;
    site.user = sftptest::kUser;
    site.password.assign(sftptest::kPassword);
    site.timeoutSeconds = 20;
    site.nameCodec = CNameCodec::Auto();
    return site;
}

class ExecTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        REQUIRE_SERVER();
        ASSERT_TRUE(SshGlobalInit());
        initialised_ = true;
        session_.reset(new CSshSession(&cb_, nullptr));
        ASSERT_TRUE(session_->Connect(ServerSite())) << session_->LastError().message;
    }
    void TearDown() override
    {
        session_.reset();
        if (initialised_)
            SshGlobalExit();
    }

    CQuiet cb_;
    std::unique_ptr<CSshSession> session_;
    bool initialised_ = false;
};
} // namespace

TEST_F(ExecTest, EchoGivesTheOutputAndExitStatusZero)
{
    CExecResult result;
    ASSERT_TRUE(session_->Exec("echo hello", CExecOptions(), result)) << session_->LastError().message;
    EXPECT_EQ(result.output, "hello\n");
    EXPECT_EQ(result.exitStatus, 0);
    EXPECT_TRUE(result.exitSignal.empty());
    EXPECT_FALSE(result.truncated);
}

TEST_F(ExecTest, ExitStatusOfAFailingCommandIsReportedAsSuccess)
{
    CExecResult result;
    ASSERT_TRUE(session_->Exec("exit 3", CExecOptions(), result)) << session_->LastError().message;
    EXPECT_EQ(result.exitStatus, 3);
    EXPECT_TRUE(result.output.empty());
}

TEST_F(ExecTest, StderrIsPartOfTheOutput)
{
    CExecResult result;
    ASSERT_TRUE(session_->Exec("echo to-err 1>&2; exit 1", CExecOptions(), result)) << session_->LastError().message;
    EXPECT_EQ(result.output, "to-err\n");
    EXPECT_EQ(result.exitStatus, 1);
}

TEST_F(ExecTest, TwoCommandsInARowOnTheSameSession)
{
    CExecResult a, b;
    ASSERT_TRUE(session_->Exec("echo one", CExecOptions(), a));
    ASSERT_TRUE(session_->Exec("echo two", CExecOptions(), b));
    EXPECT_EQ(a.output, "one\n");
    EXPECT_EQ(b.output, "two\n");
    CSftpEntry entry; // the SFTP channel still works next to the exec channels
    EXPECT_TRUE(session_->Stat("fixtures", entry)) << session_->LastError().message;
}

TEST_F(ExecTest, OutputOverTheCapIsCutButTheCommandEnds)
{
    CExecOptions options;
    options.maxOutputBytes = 1000;
    CExecResult result;
    ASSERT_TRUE(session_->Exec("head -c 300000 /dev/zero | tr '\\0' x", options, result))
        << session_->LastError().message;
    EXPECT_EQ(result.output.size(), 1000u);
    EXPECT_TRUE(result.truncated);
    EXPECT_EQ(result.exitStatus, 0);
}

TEST_F(ExecTest, ACommandKilledBySignalGivesTheSignalName)
{
    CExecResult result;
    ASSERT_TRUE(session_->Exec("kill -TERM $$", CExecOptions(), result)) << session_->LastError().message;
    // bash reports the signal as exit status 143 when it runs inside a shell; some servers send the signal name
    EXPECT_TRUE(result.exitSignal == "TERM" || result.exitStatus == 143) << result.exitStatus;
}

TEST_F(ExecTest, CancelEndsALongCommandQuicklyAndTheSessionIsBroken)
{
    std::thread canceller(
        [this]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            session_->Cancel();
        });
    const auto start = std::chrono::steady_clock::now();
    CExecResult result;
    EXPECT_FALSE(session_->Exec("sleep 30", CExecOptions(), result));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    canceller.join();
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Cancelled);
    EXPECT_LT(elapsed, std::chrono::seconds(5));
    EXPECT_FALSE(session_->IsConnected());

    session_->Disconnect(); // clears the cancel and the broken state
    ASSERT_TRUE(session_->Connect(ServerSite())) << session_->LastError().message;
    ASSERT_TRUE(session_->Exec("echo again", CExecOptions(), result));
    EXPECT_EQ(result.output, "again\n");
}

TEST_F(ExecTest, IdleTimeoutEndsAQuietCommandAndBreaksTheSession)
{
    CExecOptions options;
    options.idleTimeoutSeconds = 1;
    CExecResult result;
    EXPECT_FALSE(session_->Exec("sleep 30", options, result));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Timeout);
    EXPECT_EQ(session_->LastError().msgId, SshMsg::ExecIdleTimeout);
    ASSERT_EQ(session_->LastError().args.size(), 1u);
    EXPECT_EQ(session_->LastError().args[0], "1"); // plain number, the unit is in the pattern
    EXPECT_FALSE(session_->IsConnected());
}

TEST_F(ExecTest, ExecOnAnUnconnectedSessionFails)
{
    CSshSession idle(&cb_, nullptr);
    CExecResult result;
    EXPECT_FALSE(idle.Exec("echo x", CExecOptions(), result));
    EXPECT_TRUE(idle.LastError().IsError());
}

// More than the SSH receive window (2 MiB): libssh2 only sends its window adjustment from inside a read call, so a
// reader that sleeps on the socket with data still queued stalls here.
TEST_F(ExecTest, OutputLargerThanTheReceiveWindowArrivesComplete)
{
    CExecOptions options;
    options.idleTimeoutSeconds = 10;
    options.maxOutputBytes = 8u * 1024 * 1024;
    CExecResult result;
    ASSERT_TRUE(session_->Exec("head -c 5000000 /dev/zero | tr '\\0' x", options, result))
        << session_->LastError().message;
    EXPECT_EQ(result.output.size(), 5000000u);
    EXPECT_FALSE(result.truncated);
}

TEST_F(ExecTest, CancelEndsACommandThatNeverStopsWriting)
{
    std::thread canceller(
        [this]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            session_->Cancel();
        });
    const auto start = std::chrono::steady_clock::now();
    CExecResult result;
    EXPECT_FALSE(session_->Exec("yes", CExecOptions(), result));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    canceller.join();
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Cancelled);
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}
