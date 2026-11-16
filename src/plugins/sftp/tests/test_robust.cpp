// Robustness of the session: timeouts break it, prompts do not count against the timeout, legacy servers are refused.
#include "test_support.h"

#include <chrono>
#include <thread>

#include <libssh2.h>

#include "ssh_algorithms.h"
#include "ssh_init.h"
#include "ssh_session.h"

namespace
{
using Clock = std::chrono::steady_clock;

struct CCallbacks : ISshCallbacks
{
    HostKeyDecision VerifyHostKey(const std::string&, int, const std::string&, const std::string&, bool) override
    {
        return HostKeyDecision::AcceptOnce;
    }
    bool Prompt(PromptKind, const std::string&, const std::string&, const std::string&, bool, CSecret& answer) override
    {
        ++prompts;
        std::this_thread::sleep_for(std::chrono::milliseconds(promptDelayMs));
        answer.assign(sftptest::kPassword);
        return true;
    }
    bool Progress(uint64_t, uint64_t) override { return true; }

    int prompts = 0;
    int promptDelayMs = 0;
};

CSshSite Site(int port, int timeoutSeconds)
{
    CSshSite site;
    site.host = sftptest::kHost;
    site.port = port;
    site.user = sftptest::kUser;
    site.password.assign(sftptest::kPassword);
    site.timeoutSeconds = timeoutSeconds;
    site.nameCodec = CNameCodec::Auto();
    return site;
}

long long MsSince(const Clock::time_point& start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}
} // namespace

// B1: after a timeout the session is broken; later calls fail at once instead of finishing the old request.
TEST(Robust, TimeoutBreaksTheSessionUntilDisconnect)
{
    REQUIRE_SERVER();
    sftptest::CStallProxy proxy(sftptest::kPort);
    CCallbacks cb;
    CSshSession session(&cb, nullptr);
    const CSshSite site = Site(proxy.Port(), 1);
    ASSERT_TRUE(session.Connect(site)) << session.LastError().message;
    CSftpEntry e;
    ASSERT_TRUE(session.Stat("fixtures/small", e)) << session.LastError().message;
    EXPECT_TRUE(e.isDir);

    proxy.Stall(true);
    auto start = Clock::now();
    EXPECT_FALSE(session.Stat("fixtures/nested", e));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::Timeout) << session.LastError().message;
    EXPECT_GE(MsSince(start), 900);
    EXPECT_FALSE(session.IsConnected());

    // the next calls of the same and of other kinds fail at once, with the broken-session message
    proxy.Stall(false); // even with a healthy network again: the old request must not be continued
    start = Clock::now();
    EXPECT_FALSE(session.Stat("fixtures/empty", e));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::Timeout);
    EXPECT_NE(session.LastError().message.find("no longer usable"), std::string::npos) << session.LastError().message;
    std::vector<CSftpEntry> list;
    EXPECT_FALSE(session.ListDir("fixtures", list));
    EXPECT_TRUE(list.empty());
    EXPECT_FALSE(session.Mkdir("upload/broken_session_dir"));
    EXPECT_FALSE(session.Connect(site));
    EXPECT_NE(session.LastError().message.find("no longer usable"), std::string::npos);
    EXPECT_FALSE(session.Authenticate(site));
    EXPECT_FALSE(session.OpenSftp());
    EXPECT_FALSE(session.OpenTransport(site));
    EXPECT_LT(MsSince(start), 500);

    start = Clock::now();
    session.Disconnect();
    EXPECT_LT(MsSince(start), 1500); // no graceful goodbye on a broken session
    ASSERT_TRUE(session.Connect(site)) << session.LastError().message;
    ASSERT_TRUE(session.Stat("fixtures/empty", e)) << session.LastError().message;
    EXPECT_TRUE(e.isDir);

    // the earlier Mkdir never reached the server
    EXPECT_FALSE(session.Stat("upload/broken_session_dir", e));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::NotFound);
}

// B1: a timeout during the handshake/login closes everything; the object can connect again.
TEST(Robust, StalledServerDuringConnectFailsAndObjectIsReusable)
{
    REQUIRE_SERVER();
    sftptest::CStallProxy proxy(sftptest::kPort);
    proxy.Stall(true);
    CCallbacks cb;
    CSshSession session(&cb, nullptr);
    const auto start = Clock::now();
    EXPECT_FALSE(session.Connect(Site(proxy.Port(), 1)));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::Timeout) << session.LastError().message;
    EXPECT_LT(MsSince(start), 5000);
    EXPECT_FALSE(session.IsConnected());

    proxy.Stall(false);
    EXPECT_TRUE(session.Connect(Site(proxy.Port(), 5))) << session.LastError().message;
}

// M1: time spent in a prompt callback does not count against the timeout.
TEST(Robust, SlowPromptDoesNotTimeOut)
{
    REQUIRE_SERVER();
    if (!sftptest::ServerReachable(sftptest::kPortKbdInt))
        GTEST_SKIP() << "keyboard-interactive server not reachable";
    CCallbacks cb;
    cb.promptDelayMs = 3000;
    CSshSite site = Site(sftptest::kPortKbdInt, 2); // timeout 2 s, the prompt takes 3 s
    site.password.assign("");
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(session.Connect(site)) << session.LastError().message;
    EXPECT_EQ(cb.prompts, 1);
    CSftpEntry e;
    EXPECT_TRUE(session.Stat("fixtures", e)) << session.LastError().message;
}

// M8a: a server that offers only legacy algorithms is refused (the filters leave no common algorithm).
TEST(Robust, LegacyOnlyServerIsRefused)
{
    REQUIRE_SERVER();
    if (!sftptest::ServerReachable(sftptest::kPortLegacy))
        GTEST_SKIP() << "legacy server not reachable on 2226 - re-run tests/docker/setup.ps1";
    CCallbacks cb;
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(session.Connect(Site(sftptest::kPortLegacy, 10)));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::ConnectFailed) << session.LastError().message;
    EXPECT_EQ(session.LastError().libssh2Code, LIBSSH2_ERROR_KEX_FAILURE) << session.LastError().message;
    EXPECT_NE(session.LastError().message.find("handshake"), std::string::npos) << session.LastError().message;
    EXPECT_FALSE(session.IsConnected());
    EXPECT_EQ(cb.prompts, 0);
}

// M8: the algorithm policy itself
TEST(Algorithms, LegacyListsAreFilteredToNothing)
{
    EXPECT_EQ(FilterAlgorithms({"diffie-hellman-group14-sha1", "diffie-hellman-group1-sha1"}, IsAllowedKex), "");
    EXPECT_EQ(FilterAlgorithms({"ssh-rsa", "ssh-dss", "ssh-rsa-cert-v01@openssh.com"}, IsAllowedHostKey), "");
    EXPECT_EQ(FilterAlgorithms({"aes128-cbc", "3des-cbc", "aes256-cbc"}, IsAllowedCipher), "");
    EXPECT_EQ(FilterAlgorithms({"hmac-sha1", "hmac-md5", "hmac-sha1-96"}, IsAllowedMac), "");
}

TEST(Algorithms, ModernAlgorithmsPassInOrder)
{
    EXPECT_EQ(FilterAlgorithms({"curve25519-sha256", "diffie-hellman-group14-sha1", "ecdh-sha2-nistp256",
                                "diffie-hellman-group16-sha512"},
                               IsAllowedKex),
              "curve25519-sha256,ecdh-sha2-nistp256,diffie-hellman-group16-sha512");
    EXPECT_EQ(FilterAlgorithms({"ssh-ed25519", "ssh-rsa", "rsa-sha2-512", "ecdsa-sha2-nistp256",
                                "ssh-ed25519-cert-v01@openssh.com"},
                               IsAllowedHostKey),
              "ssh-ed25519,rsa-sha2-512,ecdsa-sha2-nistp256");
    EXPECT_EQ(FilterAlgorithms({"aes256-ctr", "aes128-cbc", "chacha20-poly1305@openssh.com", "aes256-gcm@openssh.com"},
                               IsAllowedCipher),
              "aes256-ctr,chacha20-poly1305@openssh.com,aes256-gcm@openssh.com");
    EXPECT_EQ(FilterAlgorithms({"hmac-sha2-256", "hmac-sha1", "hmac-sha2-512-etm@openssh.com"}, IsAllowedMac),
              "hmac-sha2-256,hmac-sha2-512-etm@openssh.com");
}

// M8b: when the policy leaves nothing, setting the preference fails; libssh2's defaults are not kept.
TEST(Algorithms, RestrictFailsWhenNothingIsAllowed)
{
    ASSERT_TRUE(SshGlobalInit());
    LIBSSH2_SESSION* session = libssh2_session_init();
    ASSERT_NE(session, nullptr);
    EXPECT_TRUE(RestrictAlgorithms(session, LIBSSH2_METHOD_KEX, IsAllowedKex));
    EXPECT_TRUE(RestrictAlgorithms(session, LIBSSH2_METHOD_HOSTKEY, IsAllowedHostKey));
    EXPECT_TRUE(RestrictAlgorithms(session, LIBSSH2_METHOD_CRYPT_CS, IsAllowedCipher));
    EXPECT_TRUE(RestrictAlgorithms(session, LIBSSH2_METHOD_MAC_CS, IsAllowedMac));
    EXPECT_FALSE(RestrictAlgorithms(session, LIBSSH2_METHOD_KEX, [](const std::string&) { return false; }));
    libssh2_session_free(session);
    SshGlobalExit();
}
