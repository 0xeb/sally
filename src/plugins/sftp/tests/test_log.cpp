// The session's log lines (CSshSession::SetLogSink) against the Docker server: what a connect writes, the Verbose SFTP
// lines, errors, and that no secret ever reaches the log.
#include "test_support.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "ssh_init.h"
#include "ssh_session.h"

namespace
{
struct CCollectSink : ISshLogSink
{
    struct CLine
    {
        SshLogLevel level;
        std::string text;
    };
    void Log(SshLogLevel level, const std::string& text) override
    {
        std::lock_guard<std::mutex> guard(lock);
        lines.push_back({level, text});
    }
    bool Has(const std::string& part) const { return Find(part) != nullptr; }
    const CLine* Find(const std::string& part) const
    {
        for (const CLine& l : lines)
            if (l.text.find(part) != std::string::npos)
                return &l;
        return nullptr;
    }
    std::mutex lock;
    std::vector<CLine> lines;
};

struct CAcceptCallbacks : ISshCallbacks
{
    HostKeyDecision VerifyHostKey(const std::string&, int, const std::string&, const std::string&, bool) override
    {
        return HostKeyDecision::AcceptOnce;
    }
    bool Prompt(PromptKind, const std::string&, const std::string&, const std::string&, bool, CSecret& answer) override
    {
        answer.assign("Kbd-Answer-Zq9"); // must never appear in a log
        return true;
    }
    bool Progress(uint64_t, uint64_t) override { return true; }
};

CSshSite ServerSite(const char* password)
{
    CSshSite site;
    site.host = sftptest::kHost;
    site.port = sftptest::kPort;
    site.user = sftptest::kUser;
    site.password.assign(password);
    site.timeoutSeconds = 20;
    return site;
}
} // namespace

TEST(SessionLog, ConnectWritesTheExpectedLines)
{
    REQUIRE_SERVER();
    ASSERT_TRUE(SshGlobalInit());
    CAcceptCallbacks cb;
    CCollectSink sink;
    CSshSession session(&cb, nullptr);
    session.SetLogSink(&sink);
    ASSERT_TRUE(session.Connect(ServerSite(sftptest::kPassword))) << session.LastError().message;

    EXPECT_TRUE(sink.Has("Resolving 127.0.0.1"));
    EXPECT_TRUE(sink.Has("Connecting to 127.0.0.1:2222"));
    EXPECT_TRUE(sink.Has("TCP connection established"));
    EXPECT_TRUE(sink.Has("Server: SSH-2.0-"));
    EXPECT_TRUE(sink.Has("Key exchange: "));
    EXPECT_TRUE(sink.Has("Cipher: "));
    EXPECT_TRUE(sink.Has("Host key "));
    EXPECT_TRUE(sink.Has("SHA256:"));
    EXPECT_TRUE(sink.Has("Host key decision: accepted for this session"));
    EXPECT_TRUE(sink.Has("Logging in as 'test'; the server offers: "));
    EXPECT_TRUE(sink.Has("Trying a password"));
    EXPECT_TRUE(sink.Has("Password: accepted"));
    EXPECT_TRUE(sink.Has("Logged in"));
    EXPECT_TRUE(sink.Has("SFTP subsystem opened"));

    // everything up to here is Normal: nothing Verbose and no error
    for (const CCollectSink::CLine& l : sink.lines)
    {
        EXPECT_NE(l.level, SshLogLevel::Verbose) << l.text;
        EXPECT_NE(l.level, SshLogLevel::Error) << l.text;
    }

    session.Disconnect();
    EXPECT_TRUE(sink.Has("Disconnecting"));
}

TEST(SessionLog, SftpCallsAreVerboseAndErrorsAreErrors)
{
    REQUIRE_SERVER();
    ASSERT_TRUE(SshGlobalInit());
    CAcceptCallbacks cb;
    CCollectSink sink;
    CSshSession session(&cb, nullptr);
    session.SetLogSink(&sink);
    ASSERT_TRUE(session.Connect(ServerSite(sftptest::kPassword)));

    std::vector<CSftpEntry> list;
    ASSERT_TRUE(session.ListDir("fixtures", list));
    const CCollectSink::CLine* listLine = sink.Find("SFTP list 'fixtures'");
    ASSERT_NE(listLine, nullptr);
    EXPECT_EQ(listLine->level, SshLogLevel::Verbose);

    CSftpEntry entry;
    EXPECT_FALSE(session.Stat("fixtures/does-not-exist", entry));
    const CCollectSink::CLine* statLine = sink.Find("SFTP stat 'fixtures/does-not-exist'");
    ASSERT_NE(statLine, nullptr);
    EXPECT_EQ(statLine->level, SshLogLevel::Verbose);
    bool errorLogged = false;
    for (const CCollectSink::CLine& l : sink.lines)
        if (l.level == SshLogLevel::Error && l.text.find("Error: ") == 0 &&
            l.text.find(session.LastError().message) != std::string::npos)
            errorLogged = true;
    EXPECT_TRUE(errorLogged) << "the English error message is in the log";

    EXPECT_FALSE(session.Rename("fixtures/nope-a", "fixtures/nope-b", false));
    EXPECT_TRUE(sink.Has("SFTP rename 'fixtures/nope-a' -> 'fixtures/nope-b'"));
}

TEST(SessionLog, AWrongPasswordIsNeverLogged)
{
    REQUIRE_SERVER();
    ASSERT_TRUE(SshGlobalInit());
    CAcceptCallbacks cb;
    CCollectSink sink;
    CSshSession session(&cb, nullptr);
    session.SetLogSink(&sink);
    const char* secret = "Wrong-Pw-Zq9-Xv7";
    EXPECT_FALSE(session.Connect(ServerSite(secret)));

    EXPECT_TRUE(sink.Has("Password: not accepted"));
    EXPECT_TRUE(sink.Has("Error: Authentication failed"));
    EXPECT_FALSE(sink.Has(secret));
    EXPECT_FALSE(sink.Has("Kbd-Answer-Zq9"));
}

TEST(SessionLog, KeyboardInteractiveAnswerIsNeverLogged)
{
    REQUIRE_SERVER();
    ASSERT_TRUE(SshGlobalInit());
    CAcceptCallbacks cb;
    CCollectSink sink;
    CSshSession session(&cb, nullptr);
    session.SetLogSink(&sink);
    CSshSite site = ServerSite("");
    site.port = sftptest::kPortKbdInt;
    session.Connect(site); // the answer is wrong on purpose: the outcome does not matter, only the log
    EXPECT_TRUE(sink.Has("Trying keyboard-interactive"));
    EXPECT_FALSE(sink.Has("Kbd-Answer-Zq9"));
}

TEST(SessionLog, PasswordOfASuccessfulLoginIsNotInTheLog)
{
    REQUIRE_SERVER();
    ASSERT_TRUE(SshGlobalInit());
    CAcceptCallbacks cb;
    CCollectSink sink;
    CSshSession session(&cb, nullptr);
    session.SetLogSink(&sink);
    ASSERT_TRUE(session.Connect(ServerSite(sftptest::kPassword)));
    // the test password "test" is also the user name, so look for the places where a password could be written
    for (const CCollectSink::CLine& l : sink.lines)
    {
        EXPECT_EQ(l.text.find("password: test"), std::string::npos) << l.text;
        EXPECT_EQ(l.text.find("Password test"), std::string::npos) << l.text;
        EXPECT_EQ(l.text.find("password=test"), std::string::npos) << l.text;
    }
}

TEST(SessionLog, AFailedConnectIsLogged)
{
    REQUIRE_SERVER();
    ASSERT_TRUE(SshGlobalInit());
    CAcceptCallbacks cb;
    CCollectSink sink;
    CSshSession session(&cb, nullptr);
    session.SetLogSink(&sink);
    CSshSite site = ServerSite(sftptest::kPassword);
    site.port = 1; // nothing listens there
    site.timeoutSeconds = 3;
    EXPECT_FALSE(session.Connect(site));
    EXPECT_TRUE(sink.Has("Connecting failed"));
    EXPECT_TRUE(sink.Has("Error: "));
}

TEST(SessionLog, NoSinkIsFine)
{
    REQUIRE_SERVER();
    ASSERT_TRUE(SshGlobalInit());
    CAcceptCallbacks cb;
    CSshSession session(&cb, nullptr);
    EXPECT_TRUE(session.Connect(ServerSite(sftptest::kPassword)));
    std::vector<CSftpEntry> list;
    EXPECT_TRUE(session.ListDir("fixtures", list));
}
