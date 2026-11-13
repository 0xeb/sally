// Tests for the idle handling: the pure decisions (keepalive due, detached FS idle), the keepalive of the panel session
// and the silent retry of read-only requests (D57, D59). Docker test server; a CStallProxy closes the connection like
// a server that went away.
#include "test_support.h"

#include <windows.h>

#include <chrono>
#include <string>

#include "idle_policy.h"
#include "panel_session.h"
#include "session_log.h"
#include "site.h"

namespace
{
using Clock = std::chrono::steady_clock;

Clock::time_point At(int seconds)
{
    return Clock::time_point() + std::chrono::seconds(seconds);
}

CSite SiteOnPort(int port)
{
    CSite site;
    site.name = L"test";
    site.host = L"127.0.0.1";
    site.port = port;
    site.user = L"test";
    site.timeoutSeconds = 20;
    return site;
}

// Waits for a request like fs/ does and accepts every host key (the test has no known_hosts: each connect asks).
bool Pump(CPanelSession& s, CPanelRequest& req, DWORD timeoutMs = 30000)
{
    const auto end = Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!req.IsDone())
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
        if (left <= 0)
            return false;
        HANDLE handles[2] = {req.DoneEvent(), s.InteractionEvent()};
        const DWORD r = WaitForMultipleObjects(2, handles, FALSE, static_cast<DWORD>(left));
        if (r == WAIT_OBJECT_0 + 1)
        {
            CInteraction in;
            if (s.TryGetInteraction(in) && in.kind == CInteraction::Kind::HostKey)
                s.AnswerHostKey(HostKeyDecision::AcceptOnce);
        }
    }
    return true;
}

std::wstring LogText(CPanelSession& s)
{
    std::wstring text;
    SessionLogStore().GetText(s.LogId(), text);
    return text;
}

class KeepaliveTest : public ::testing::Test
{
protected:
    void SetUp() override { REQUIRE_SERVER(); }

    static bool Connect(CPanelSession& s, int port)
    {
        auto req = s.Connect(SiteOnPort(port), CSecret(sftptest::kPassword), CSecret());
        return Pump(s, *req) && req->Ok();
    }
};
} // namespace

// ---- pure decisions (no server) ------------------------------------------------------------------------------------

TEST(IdlePolicy, KeepaliveIsDueAfterTheInterval)
{
    EXPECT_FALSE(KeepaliveDue(At(100), At(159), 60));
    EXPECT_TRUE(KeepaliveDue(At(100), At(160), 60));
    EXPECT_TRUE(KeepaliveDue(At(100), At(1000), 60));
}

TEST(IdlePolicy, KeepaliveZeroOrNegativeIsOff)
{
    EXPECT_FALSE(KeepaliveDue(At(0), At(100000), 0));
    EXPECT_FALSE(KeepaliveDue(At(0), At(100000), -5));
}

TEST(IdlePolicy, DetachedFsExpiresAfterTheConfiguredMinutes)
{
    EXPECT_FALSE(DetachedIdleExpired(At(0), At(30 * 60 - 1), 30));
    EXPECT_TRUE(DetachedIdleExpired(At(0), At(30 * 60), 30));
    EXPECT_TRUE(DetachedIdleExpired(At(10), At(10 + 31 * 60), 30));
}

TEST(IdlePolicy, DetachedFsNeverExpiresWithZeroMinutes)
{
    EXPECT_FALSE(DetachedIdleExpired(At(0), At(1000000), 0));
}

TEST(IdlePolicy, UseResetsTheIdleTime)
{
    // lastUsed moves forward with every use, so the same "now" is no longer idle
    EXPECT_TRUE(DetachedIdleExpired(At(0), At(40 * 60), 30));
    EXPECT_FALSE(DetachedIdleExpired(At(20 * 60), At(40 * 60), 30));
}

// ---- keepalive -----------------------------------------------------------------------------------------------------

TEST_F(KeepaliveTest, IdleSessionSendsKeepalives)
{
    CPanelSession s(nullptr);
    s.SetKeepaliveSeconds(2);
    ASSERT_TRUE(Connect(s, sftptest::kPort));
    EXPECT_EQ(LogText(s).find(L"Keepalive sent"), std::wstring::npos);
    Sleep(5000);
    EXPECT_NE(LogText(s).find(L"Keepalive sent"), std::wstring::npos);

    // the connection is still good afterwards
    auto list = s.List("fixtures/small");
    ASSERT_TRUE(Pump(s, *list));
    EXPECT_TRUE(list->Ok()) << list->Error().message;
}

TEST_F(KeepaliveTest, KeepaliveZeroSendsNothing)
{
    CPanelSession s(nullptr);
    s.SetKeepaliveSeconds(0);
    ASSERT_TRUE(Connect(s, sftptest::kPort));
    Sleep(4000);
    EXPECT_EQ(LogText(s).find(L"Keepalive"), std::wstring::npos);
}

TEST_F(KeepaliveTest, BusyOrFreshSessionIsNotPinged)
{
    CPanelSession s(nullptr);
    s.SetKeepaliveSeconds(3);
    ASSERT_TRUE(Connect(s, sftptest::kPort));
    // a request every second keeps the connection "not idle for 3 s"
    for (int i = 0; i < 5; ++i)
    {
        auto stat = s.Stat("fixtures/small/hello.txt");
        ASSERT_TRUE(Pump(s, *stat));
        Sleep(1000);
    }
    EXPECT_EQ(LogText(s).find(L"Keepalive sent"), std::wstring::npos);
}

TEST_F(KeepaliveTest, ActivityTimeMovesWithRequests)
{
    CPanelSession s(nullptr);
    s.SetKeepaliveSeconds(0);
    ASSERT_TRUE(Connect(s, sftptest::kPort));
    const auto before = s.LastActivity();
    Sleep(50);
    auto stat = s.Stat("fixtures/small/hello.txt");
    ASSERT_TRUE(Pump(s, *stat));
    EXPECT_GT(s.LastActivity(), before);
}

// ---- silent retry (D59) --------------------------------------------------------------------------------------------

TEST_F(KeepaliveTest, ListIsRetriedOnceAfterTheConnectionWasClosed)
{
    sftptest::CStallProxy proxy(sftptest::kPort);
    CPanelSession s(nullptr);
    s.SetKeepaliveSeconds(0);
    ASSERT_TRUE(Connect(s, proxy.Port()));
    ASSERT_EQ(proxy.Accepted(), 1);

    proxy.DropConnections();
    auto list = s.List("fixtures/small");
    ASSERT_TRUE(Pump(s, *list));
    EXPECT_TRUE(list->Ok()) << list->Error().message;
    EXPECT_FALSE(list->Entries().empty());
    EXPECT_EQ(proxy.Accepted(), 2); // one reconnect
    EXPECT_NE(LogText(s).find(L"repeating the request"), std::wstring::npos);
}

TEST_F(KeepaliveTest, StatAndRealPathAreRetriedToo)
{
    sftptest::CStallProxy proxy(sftptest::kPort);
    CPanelSession s(nullptr);
    s.SetKeepaliveSeconds(0);
    ASSERT_TRUE(Connect(s, proxy.Port()));

    proxy.DropConnections();
    auto stat = s.Stat("fixtures/small/hello.txt");
    ASSERT_TRUE(Pump(s, *stat));
    EXPECT_TRUE(stat->Ok()) << stat->Error().message;

    proxy.DropConnections();
    auto real = s.RealPath("fixtures");
    ASSERT_TRUE(Pump(s, *real));
    EXPECT_TRUE(real->Ok()) << real->Error().message;
    EXPECT_EQ(proxy.Accepted(), 3);
}

TEST_F(KeepaliveTest, RenameIsNeverRetried)
{
    sftptest::CStallProxy proxy(sftptest::kPort);
    CPanelSession s(nullptr);
    s.SetKeepaliveSeconds(0);
    ASSERT_TRUE(Connect(s, proxy.Port()));

    proxy.DropConnections();
    auto rename = s.Rename("upload/keepalive_never_there", "upload/keepalive_never_there2", false);
    ASSERT_TRUE(Pump(s, *rename));
    EXPECT_FALSE(rename->Ok());
    EXPECT_EQ(proxy.Accepted(), 1); // no reconnect for a request that changes something
    EXPECT_EQ(LogText(s).find(L"repeating the request"), std::wstring::npos);

    // the next request reconnects as before (the session is broken, not stuck)
    auto list = s.List("fixtures/small");
    ASSERT_TRUE(Pump(s, *list));
    EXPECT_TRUE(list->Ok()) << list->Error().message;
    EXPECT_EQ(proxy.Accepted(), 2);
}

TEST_F(KeepaliveTest, MkdirAndRemoveAreNeverRetried)
{
    sftptest::CStallProxy proxy(sftptest::kPort);
    CPanelSession s(nullptr);
    s.SetKeepaliveSeconds(0);
    ASSERT_TRUE(Connect(s, proxy.Port()));

    proxy.DropConnections();
    auto mkdir = s.Mkdir("upload/keepalive_never_made");
    ASSERT_TRUE(Pump(s, *mkdir));
    EXPECT_FALSE(mkdir->Ok());
    EXPECT_EQ(proxy.Accepted(), 1);

    auto list = s.List("fixtures/small"); // reconnects
    ASSERT_TRUE(Pump(s, *list));
    ASSERT_TRUE(list->Ok());
    proxy.DropConnections();
    auto remove = s.RemoveFile("upload/keepalive_never_there");
    ASSERT_TRUE(Pump(s, *remove));
    EXPECT_FALSE(remove->Ok());
    EXPECT_EQ(proxy.Accepted(), 2);
}

TEST_F(KeepaliveTest, ErrorOfTheServerIsNotRetried)
{
    // a missing path is an answer of the server, not a lost connection: no reconnect
    sftptest::CStallProxy proxy(sftptest::kPort);
    CPanelSession s(nullptr);
    s.SetKeepaliveSeconds(0);
    ASSERT_TRUE(Connect(s, proxy.Port()));
    auto list = s.List("fixtures/does-not-exist");
    ASSERT_TRUE(Pump(s, *list));
    EXPECT_EQ(list->Error().kind, SshErrorKind::NotFound);
    EXPECT_EQ(proxy.Accepted(), 1);
}
