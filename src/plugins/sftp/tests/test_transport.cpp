#include "test_support.h"

#include <libssh2.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "known_hosts.h"
#include "ssh_init.h"
#include "ssh_session.h"

namespace
{
using Clock = std::chrono::steady_clock;

// A wrong ed25519 key to pre-seed (the Docker sshd negotiates ed25519 first)
const char kWrongBlobBase64[] = "AAAAC3NzaC1lZDI1NTE5AAAAIPCXkH/dYSFrBOlTqCsONq3uq3/NVgQ9Ti8sm0cmbPdm";

struct CTestCallbacks : ISshCallbacks
{
    HostKeyDecision decision = HostKeyDecision::Reject;
    int calls = 0;
    bool lastChanged = false;
    std::string lastHost, lastType, lastFingerprint;
    int lastPort = 0;

    HostKeyDecision VerifyHostKey(const std::string& host, int port, const std::string& keyType,
                                  const std::string& fingerprint, bool changed) override
    {
        ++calls;
        lastHost = host;
        lastPort = port;
        lastType = keyType;
        lastFingerprint = fingerprint;
        lastChanged = changed;
        return decision;
    }
    bool Prompt(PromptKind, const std::string&, const std::string&, const std::string&, bool, CSecret&) override { return false; }
    bool Progress(uint64_t, uint64_t) override { return true; }
};

std::string ReadAll(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

CSshSite ServerSite(int timeoutSeconds = 10)
{
    CSshSite site;
    site.host = sftptest::kHost;
    site.port = sftptest::kPort;
    site.user = sftptest::kUser;
    site.timeoutSeconds = timeoutSeconds;
    return site;
}

// A local TCP listener that accepts connections into its backlog and never reads or writes: a silent server.
class CSilentListener
{
public:
    CSilentListener()
    {
        sftptest::EnsureWinsock();
        s_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        bind(s_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(s_, 8);
        int len = sizeof(addr);
        getsockname(s_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
    }
    ~CSilentListener() { closesocket(s_); }
    int port() const { return port_; }
    // Closes the listener but returns its port, which is then very likely closed
    int ReleasePort()
    {
        closesocket(s_);
        s_ = INVALID_SOCKET;
        return port_;
    }

private:
    SOCKET s_ = INVALID_SOCKET;
    int port_ = 0;
};

class TransportTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(SshGlobalInit());
        dir_ = std::filesystem::temp_directory_path() /
               (std::string("sally_tr_") + std::to_string(GetCurrentProcessId()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        own_ = (dir_ / "own_known_hosts").string();
    }
    void TearDown() override
    {
        SshGlobalExit();
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    // one connect attempt with a fresh session, like a new connection
    bool Connect(CKnownHosts& hosts, CTestCallbacks& cb, const CSshSite& site, CSshError* err = nullptr,
                 std::string* type = nullptr)
    {
        CSshSession session(&cb, &hosts);
        bool ok = session.OpenTransport(site);
        if (err != nullptr)
            *err = session.LastError();
        if (type != nullptr)
            *type = session.HostKeyType();
        return ok;
    }

    std::filesystem::path dir_;
    std::string own_;
};
} // namespace

TEST_F(TransportTest, UnknownKeyAcceptAndSave)
{
    REQUIRE_SERVER();
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    cb.decision = HostKeyDecision::AcceptAndSave;

    CSshSession session(&cb, &hosts);
    ASSERT_TRUE(session.OpenTransport(ServerSite())) << session.LastError().message;
    EXPECT_TRUE(session.IsConnected());
    EXPECT_EQ(cb.calls, 1);
    EXPECT_FALSE(cb.lastChanged);
    EXPECT_EQ(cb.lastHost, sftptest::kHost);
    EXPECT_EQ(cb.lastPort, sftptest::kPort);
    EXPECT_EQ(cb.lastType, "ssh-ed25519");
    EXPECT_EQ(cb.lastFingerprint.compare(0, 7, "SHA256:"), 0);
    EXPECT_EQ(session.HostKeyFingerprint(), cb.lastFingerprint);
    EXPECT_EQ(session.HostKeyType(), "ssh-ed25519");
    session.Disconnect();
    EXPECT_FALSE(session.IsConnected());

    // the entry is in the file under the [host]:port name
    EXPECT_EQ(ReadAll(own_).compare(0, 17, "[127.0.0.1]:2222 "), 0) << ReadAll(own_);

    // a second connection asks nothing
    ASSERT_TRUE(Connect(hosts, cb, ServerSite()));
    EXPECT_EQ(cb.calls, 1);
}

TEST_F(TransportTest, AcceptOnceDoesNotSave)
{
    REQUIRE_SERVER();
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    cb.decision = HostKeyDecision::AcceptOnce;
    ASSERT_TRUE(Connect(hosts, cb, ServerSite()));
    EXPECT_EQ(cb.calls, 1);
    EXPECT_FALSE(std::filesystem::exists(own_));

    ASSERT_TRUE(Connect(hosts, cb, ServerSite()));
    EXPECT_EQ(cb.calls, 2); // asks again
    EXPECT_FALSE(std::filesystem::exists(own_));
}

TEST_F(TransportTest, RejectGivesHostKeyRejected)
{
    REQUIRE_SERVER();
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    cb.decision = HostKeyDecision::Reject;
    CSshSession session(&cb, &hosts);
    EXPECT_FALSE(session.OpenTransport(ServerSite()));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::HostKeyRejected);
    EXPECT_FALSE(session.IsConnected());
    EXPECT_EQ(cb.calls, 1);
    EXPECT_FALSE(std::filesystem::exists(own_));
}

TEST_F(TransportTest, NoCallbackRejects)
{
    REQUIRE_SERVER();
    CKnownHosts hosts(own_);
    CSshSession session(nullptr, &hosts);
    EXPECT_FALSE(session.OpenTransport(ServerSite()));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::HostKeyRejected);
    EXPECT_FALSE(std::filesystem::exists(own_));
}

TEST_F(TransportTest, ChangedKeyIsReportedAndAcceptReplaces)
{
    REQUIRE_SERVER();
    {
        std::ofstream out(own_, std::ios::binary);
        out << "[127.0.0.1]:2222 ssh-ed25519 " << kWrongBlobBase64 << "\n";
    }
    CKnownHosts hosts(own_);
    CTestCallbacks cb;

    cb.decision = HostKeyDecision::Reject;
    CSshError err;
    EXPECT_FALSE(Connect(hosts, cb, ServerSite(), &err));
    EXPECT_EQ(err.kind, SshErrorKind::HostKeyChanged);
    EXPECT_EQ(cb.calls, 1);
    EXPECT_TRUE(cb.lastChanged);
    EXPECT_NE(ReadAll(own_).find(kWrongBlobBase64), std::string::npos); // untouched

    cb.decision = HostKeyDecision::AcceptAndSave;
    ASSERT_TRUE(Connect(hosts, cb, ServerSite(), &err)) << err.message;
    EXPECT_EQ(cb.calls, 2);
    EXPECT_TRUE(cb.lastChanged);
    std::string text = ReadAll(own_);
    EXPECT_EQ(text.find(kWrongBlobBase64), std::string::npos);
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 1); // replaced, not appended

    ASSERT_TRUE(Connect(hosts, cb, ServerSite()));
    EXPECT_EQ(cb.calls, 2); // silent now
}

TEST_F(TransportTest, UserKnownHostsIsReadButNeverWritten)
{
    REQUIRE_SERVER();
    // learn the real key into a scratch file, then use that file as the user's file
    const std::string learned = (dir_ / "learned").string();
    {
        CKnownHosts hosts(learned);
        CTestCallbacks cb;
        cb.decision = HostKeyDecision::AcceptAndSave;
        ASSERT_TRUE(Connect(hosts, cb, ServerSite()));
    }
    const std::string user = (dir_ / "user_known_hosts").string();
    std::filesystem::copy_file(learned, user);
    const std::string before = ReadAll(user);

    CKnownHosts hosts(own_, user);
    CTestCallbacks cb; // would reject, so a silent success proves the user file was used
    ASSERT_TRUE(Connect(hosts, cb, ServerSite()));
    EXPECT_EQ(cb.calls, 0);
    EXPECT_FALSE(std::filesystem::exists(own_));
    EXPECT_EQ(ReadAll(user), before);

    // a changed key in the user file plus accept: the own file gets the entry, the user file stays
    {
        std::ofstream out(user, std::ios::binary);
        out << "[127.0.0.1]:2222 ssh-ed25519 " << kWrongBlobBase64 << "\n";
    }
    const std::string wrong = ReadAll(user);
    cb.decision = HostKeyDecision::AcceptAndSave;
    ASSERT_TRUE(Connect(hosts, cb, ServerSite()));
    EXPECT_EQ(cb.calls, 1);
    EXPECT_TRUE(cb.lastChanged);
    EXPECT_EQ(ReadAll(user), wrong);
    EXPECT_EQ(ReadAll(own_), before);
}

TEST_F(TransportTest, CompressionOnAndOff)
{
    REQUIRE_SERVER();
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    cb.decision = HostKeyDecision::AcceptAndSave;

    CSshSite off = ServerSite();
    CSshSession s1(&cb, &hosts);
    ASSERT_TRUE(s1.OpenTransport(off)) << s1.LastError().message;
    EXPECT_EQ(s1.NegotiatedMethod(LIBSSH2_METHOD_COMP_CS), "none");

    CSshSite on = ServerSite();
    on.compression = true;
    CSshSession s2(&cb, &hosts);
    ASSERT_TRUE(s2.OpenTransport(on)) << s2.LastError().message;
    EXPECT_NE(s2.NegotiatedMethod(LIBSSH2_METHOD_COMP_CS), "none");
    EXPECT_FALSE(s2.NegotiatedMethod(LIBSSH2_METHOD_COMP_CS).empty());
}

TEST_F(TransportTest, NoLegacyAlgorithmsNegotiated)
{
    REQUIRE_SERVER();
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    cb.decision = HostKeyDecision::AcceptOnce;
    CSshSession s(&cb, &hosts);
    ASSERT_TRUE(s.OpenTransport(ServerSite())) << s.LastError().message;

    const int types[] = {LIBSSH2_METHOD_KEX,      LIBSSH2_METHOD_HOSTKEY, LIBSSH2_METHOD_CRYPT_CS,
                         LIBSSH2_METHOD_CRYPT_SC, LIBSSH2_METHOD_MAC_CS,  LIBSSH2_METHOD_MAC_SC};
    for (int t : types)
    {
        std::string m = s.NegotiatedMethod(t);
        EXPECT_FALSE(m.empty()) << t;
        EXPECT_EQ(m.find("cbc"), std::string::npos) << m;
        EXPECT_EQ(m.find("3des"), std::string::npos) << m;
        EXPECT_EQ(m.find("sha1"), std::string::npos) << m;
        EXPECT_NE(m, "ssh-rsa");
        EXPECT_NE(m, "ssh-dss");
    }
}

TEST_F(TransportTest, ConnectToClosedPortFails)
{
    CSilentListener l;
    int port = l.ReleasePort();
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    CSshSite site = ServerSite(10);
    site.port = port;
    CSshSession s(&cb, &hosts);
    EXPECT_FALSE(s.OpenTransport(site));
    EXPECT_EQ(s.LastError().kind, SshErrorKind::ConnectFailed) << s.LastError().message;
    EXPECT_FALSE(s.LastError().message.empty());
    EXPECT_EQ(cb.calls, 0);
}

TEST_F(TransportTest, UnresolvableHostFails)
{
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    CSshSite site = ServerSite(5);
    site.host = "no-such-host.invalid";
    CSshSession s(&cb, &hosts);
    EXPECT_FALSE(s.OpenTransport(site));
    EXPECT_EQ(s.LastError().kind, SshErrorKind::ConnectFailed);
}

TEST_F(TransportTest, UnresolvableHostReturnsAnErrorPromptly)
{
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    CSshSite site = ServerSite(20);
    site.host = "no-such-host.invalid";
    CSshSession s(&cb, &hosts);
    auto start = Clock::now();
    EXPECT_FALSE(s.OpenTransport(site));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    EXPECT_EQ(s.LastError().kind, SshErrorKind::ConnectFailed) << s.LastError().message;
    EXPECT_NE(s.LastError().message.find("Cannot resolve host 'no-such-host.invalid'"), std::string::npos)
        << s.LastError().message;
    EXPECT_LT(ms, 15000);
}

// Whatever the resolver does with the name (fails quickly or stalls), Cancel from another thread must end the
// connect promptly: with Cancelled, or with the resolution error when the resolver was faster than the cancel.
TEST_F(TransportTest, CancelDuringNameResolution)
{
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    CSshSite site = ServerSite(60);
    site.host = "slow-lookup-cancel-test.invalid";
    CSshSession s(&cb, &hosts);

    std::thread canceller(
        [&s]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            s.Cancel();
        });
    auto start = Clock::now();
    EXPECT_FALSE(s.OpenTransport(site));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    canceller.join();
    EXPECT_LT(ms, 2000);
    const SshErrorKind kind = s.LastError().kind;
    EXPECT_TRUE(kind == SshErrorKind::Cancelled || kind == SshErrorKind::ConnectFailed) << s.LastError().message;
    EXPECT_FALSE(s.IsConnected());

    // the object is reusable after Disconnect
    s.Disconnect();
    CSshSite closed = ServerSite(5);
    closed.port = CSilentListener().ReleasePort();
    EXPECT_FALSE(s.OpenTransport(closed));
    EXPECT_EQ(s.LastError().kind, SshErrorKind::ConnectFailed);
}

TEST_F(TransportTest, CancelBeforeNameResolution)
{
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    CSshSite site = ServerSite(60);
    site.host = "cancel-first.invalid";
    CSshSession s(&cb, &hosts);
    s.Cancel();
    auto start = Clock::now();
    EXPECT_FALSE(s.OpenTransport(site));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    EXPECT_EQ(s.LastError().kind, SshErrorKind::Cancelled) << s.LastError().message;
    EXPECT_LT(ms, 2000);
}

TEST_F(TransportTest, HandshakeTimeoutAgainstSilentServer)
{
    CSilentListener l;
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    CSshSite site = ServerSite(1);
    site.port = l.port();
    CSshSession s(&cb, &hosts);

    auto start = Clock::now();
    EXPECT_FALSE(s.OpenTransport(site));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    EXPECT_EQ(s.LastError().kind, SshErrorKind::Timeout) << s.LastError().message;
    EXPECT_EQ(s.LastError().msgId, SshMsg::HandshakeFailed); // the step stays; the UI adds the "no answer" reason
    EXPECT_TRUE(s.LastError().args.empty());
    EXPECT_GE(ms, 800);
    EXPECT_LT(ms, 5000);
    EXPECT_EQ(cb.calls, 0);
}

TEST_F(TransportTest, CancelDuringHandshakeFromAnotherThread)
{
    CSilentListener l;
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    CSshSite site = ServerSite(30);
    site.port = l.port();
    CSshSession s(&cb, &hosts);

    std::thread canceller(
        [&s]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            s.Cancel();
        });
    auto start = Clock::now();
    EXPECT_FALSE(s.OpenTransport(site));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    canceller.join();
    EXPECT_EQ(s.LastError().kind, SshErrorKind::Cancelled) << s.LastError().message;
    EXPECT_LT(ms, 3000);
    EXPECT_FALSE(s.IsConnected());

    // after Disconnect the object is reusable
    s.Disconnect();
    CSshSite closed = ServerSite(5);
    closed.port = CSilentListener().ReleasePort();
    EXPECT_FALSE(s.OpenTransport(closed));
    EXPECT_EQ(s.LastError().kind, SshErrorKind::ConnectFailed);
}

TEST_F(TransportTest, CancelBeforeConnect)
{
    REQUIRE_SERVER();
    CKnownHosts hosts(own_);
    CTestCallbacks cb;
    CSshSession s(&cb, &hosts);
    s.Cancel();
    EXPECT_FALSE(s.OpenTransport(ServerSite()));
    EXPECT_EQ(s.LastError().kind, SshErrorKind::Cancelled);
}
