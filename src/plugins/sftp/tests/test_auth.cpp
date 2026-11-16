#include "test_support.h"

#include <libssh2.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>
#include <vector>

#include "key_file.h"
#include "ssh_init.h"
#include "ssh_session.h"

namespace
{
// Accepts every host key once and answers prompts from a script.
struct CAuthCallbacks : ISshCallbacks
{
    std::vector<std::string> answers; // next answers for Prompt, in order
    bool refuse = false;              // Prompt returns false (user cancels)
    int prompts = 0;
    std::vector<std::string> promptTexts;
    std::vector<bool> echoFlags;
    std::function<void()> onPrompt; // runs inside Prompt

    HostKeyDecision VerifyHostKey(const std::string&, int, const std::string&, const std::string&, bool) override
    {
        return HostKeyDecision::AcceptOnce;
    }
    bool Prompt(PromptKind, const std::string&, const std::string&, const std::string& text, bool echo, CSecret& answer) override
    {
        ++prompts;
        promptTexts.push_back(text);
        echoFlags.push_back(echo);
        if (onPrompt)
            onPrompt();
        if (refuse)
            return false;
        std::string next = answers.empty() ? std::string() : answers.front();
        if (!answers.empty())
            answers.erase(answers.begin());
        answer.assign(next);
        return true;
    }
    bool Progress(uint64_t, uint64_t) override { return true; }
};

std::string Key(const char* name)
{
    return (std::filesystem::path(sftptest::FixtureDir()) / name).string();
}

std::string Passphrase()
{
    std::ifstream in(Key("passphrase.txt"), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

CSshSite Site(int port = sftptest::kPort)
{
    CSshSite site;
    site.host = sftptest::kHost;
    site.port = port;
    site.user = sftptest::kUser;
    site.timeoutSeconds = 10;
    return site;
}

class AuthTest : public ::testing::Test
{
protected:
    void SetUp() override { ASSERT_TRUE(SshGlobalInit()); }
    void TearDown() override { SshGlobalExit(); }

    // transport + authentication on a fresh session
    bool Login(CSshSession& session, const CSshSite& site)
    {
        return session.OpenTransport(site) && session.Authenticate(site);
    }

    CAuthCallbacks cb;
};
} // namespace

TEST_F(AuthTest, PasswordOk)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    site.password.assign(sftptest::kPassword);
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, site)) << session.LastError().message;
    EXPECT_TRUE(session.IsAuthenticated());
    EXPECT_EQ(cb.prompts, 0);
}

TEST_F(AuthTest, WrongPasswordListsOfferedMethods)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    site.password.assign("wrong");
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::AuthFailed);
    EXPECT_FALSE(session.IsAuthenticated());
    const std::string& m = session.LastError().message;
    EXPECT_NE(m.find("publickey"), std::string::npos) << m;
    EXPECT_NE(m.find("password"), std::string::npos) << m;
    EXPECT_NE(m.find("keyboard-interactive"), std::string::npos) << m;
    EXPECT_TRUE(session.IsConnected()); // the transport stays open; the caller decides
}

TEST_F(AuthTest, KeyboardInteractiveStoredPasswordAnswersFirstPrompt)
{
    REQUIRE_SERVER();
    CSshSite site = Site(sftptest::kPortKbdInt);
    site.password.assign(sftptest::kPassword);
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, site)) << session.LastError().message;
    EXPECT_EQ(cb.prompts, 0);
}

TEST_F(AuthTest, KeyboardInteractiveCallbackOnly)
{
    REQUIRE_SERVER();
    cb.answers = {sftptest::kPassword};
    CSshSite site = Site(sftptest::kPortKbdInt);
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, site)) << session.LastError().message;
    ASSERT_EQ(cb.prompts, 1);
    EXPECT_NE(cb.promptTexts[0].find("assword"), std::string::npos) << cb.promptTexts[0];
    EXPECT_FALSE(cb.echoFlags[0]); // a password prompt is not echoed
}

TEST_F(AuthTest, KeyboardInteractiveWrongStoredPasswordFailsWithoutAskingTheCallback)
{
    REQUIRE_SERVER();
    CSshSite site = Site(sftptest::kPortKbdInt);
    site.password.assign("wrong");
    cb.answers = {sftptest::kPassword};
    CSshSession session(&cb, nullptr);
    // The stored password answers the first prompt; sshd/PAM refuses it and ends the method (no second round that
    // would reach the callback), so the login fails and the user is not asked.
    EXPECT_FALSE(Login(session, site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::AuthFailed) << session.LastError().message;
    EXPECT_EQ(cb.prompts, 0);
    EXPECT_FALSE(session.IsAuthenticated());
}

TEST_F(AuthTest, KeyboardInteractiveWrongAnswerFails)
{
    REQUIRE_SERVER();
    cb.answers = {"wrong", "wrong", "wrong"};
    CSshSite site = Site(sftptest::kPortKbdInt);
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::AuthFailed);
}

TEST_F(AuthTest, KeyboardInteractiveUserCancelsPrompt)
{
    REQUIRE_SERVER();
    cb.refuse = true;
    CSshSite site = Site(sftptest::kPortKbdInt);
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::AuthFailed);
    EXPECT_EQ(cb.prompts, 1);
}

TEST_F(AuthTest, Ed25519KeyFile)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    site.keyFile = Key("id_ed25519");
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, site)) << session.LastError().message;
    EXPECT_EQ(cb.prompts, 0);
}

TEST_F(AuthTest, RsaKeyFile)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    site.keyFile = Key("id_rsa");
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, site)) << session.LastError().message;
}

TEST_F(AuthTest, KeyFileWithNonAsciiPath)
{
    REQUIRE_SERVER();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / L"sally_klíč_ключ";
    std::filesystem::create_directories(dir);
    std::filesystem::copy_file(Key("id_ed25519"), dir / L"klíč.key",
                               std::filesystem::copy_options::overwrite_existing);
    CSshSite site = Site();
    site.keyFile = (dir / L"klíč.key").u8string().c_str();
    CSshSession session(&cb, nullptr);
    bool ok = Login(session, site);
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    ASSERT_TRUE(ok) << session.LastError().message;
}

TEST_F(AuthTest, MissingKeyFileGivesKeyFileError)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    site.keyFile = Key("does_not_exist");
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::KeyFileError);
}

TEST_F(AuthTest, PassphraseFromSite)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    site.keyFile = Key("id_ed25519_pass");
    site.passphrase.assign(Passphrase());
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, site)) << session.LastError().message;
    EXPECT_EQ(cb.prompts, 0);
}

TEST_F(AuthTest, PassphraseAskedOnceViaCallback)
{
    REQUIRE_SERVER();
    cb.answers = {Passphrase()};
    CSshSite site = Site();
    site.keyFile = Key("id_ed25519_pass");
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, site)) << session.LastError().message;
    EXPECT_EQ(cb.prompts, 1);
    EXPECT_NE(cb.promptTexts[0].find("id_ed25519_pass"), std::string::npos);
    EXPECT_FALSE(cb.echoFlags[0]);
}

TEST_F(AuthTest, WrongPassphraseFromCallbackFailsWithoutLoop)
{
    REQUIRE_SERVER();
    cb.answers = {"wrong", "wrong", "wrong"};
    CSshSite site = Site(sftptest::kPortPubKey); // no keyboard-interactive fallback that would prompt too
    site.keyFile = Key("id_ed25519_pass");
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::KeyFileError);
    EXPECT_EQ(cb.prompts, 1);
}

TEST_F(AuthTest, WrongPassphraseFromSiteIsNotAskedAgain)
{
    REQUIRE_SERVER();
    CSshSite site = Site(sftptest::kPortPubKey); // no keyboard-interactive fallback that would prompt too
    site.keyFile = Key("id_ed25519_pass");
    site.passphrase.assign("wrong");
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::KeyFileError);
    EXPECT_EQ(cb.prompts, 0);
}

TEST_F(AuthTest, PassphrasePromptCancelled)
{
    REQUIRE_SERVER();
    cb.refuse = true;
    CSshSite site = Site();
    site.keyFile = Key("id_ed25519_pass");
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::AuthFailed);
    EXPECT_EQ(cb.prompts, 1);
}

TEST_F(AuthTest, MethodsNotOfferedAreSkipped)
{
    REQUIRE_SERVER();
    // the public-key-only server offers neither password nor keyboard-interactive: a password site must not try them
    CSshSite site = Site(sftptest::kPortPubKey);
    site.password.assign(sftptest::kPassword);
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::AuthFailed);
    const std::string& m = session.LastError().message;
    EXPECT_NE(m.find("offers: publickey"), std::string::npos) << m;
    EXPECT_NE(m.find("Tried: nothing"), std::string::npos) << m;
    EXPECT_EQ(cb.prompts, 0);

    // the same server with a key works
    CSshSite keySite = Site(sftptest::kPortPubKey);
    keySite.keyFile = Key("id_ed25519");
    keySite.password.assign("ignored");
    CSshSession session2(&cb, nullptr);
    ASSERT_TRUE(Login(session2, keySite)) << session2.LastError().message;
}

TEST_F(AuthTest, KeyFileFallsBackToPassword)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    site.keyFile = Key("does_not_exist");
    site.password.assign(sftptest::kPassword);
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, site)) << session.LastError().message;
}

TEST_F(AuthTest, AgentWhenAvailable)
{
    REQUIRE_SERVER();
    CSshSite site = Site(sftptest::kPortPubKey);
    site.useAgent = true;
    CSshSession session(&cb, nullptr);
    if (!Login(session, site))
        GTEST_SKIP() << "No SSH agent holding the test key is reachable: " << session.LastError().message;
    EXPECT_TRUE(session.IsAuthenticated());
}

TEST_F(AuthTest, NoSftpServer)
{
    REQUIRE_SERVER();
    CSshSite site = Site(sftptest::kPortNoSftp);
    site.password.assign(sftptest::kPassword);
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, site)) << session.LastError().message; // authentication itself works
    EXPECT_FALSE(session.OpenSftp());
    EXPECT_EQ(session.LastError().kind, SshErrorKind::NoSftp);
    EXPECT_EQ(session.LastError().message, "The server does not provide the SFTP subsystem");
    EXPECT_NE(session.LastError().libssh2Code, 0);
    EXPECT_FALSE(session.HasSftp());

    CSshSession whole(&cb, nullptr);
    EXPECT_FALSE(whole.Connect(site));
    EXPECT_EQ(whole.LastError().kind, SshErrorKind::NoSftp);
    EXPECT_FALSE(whole.IsConnected());
    EXPECT_FALSE(whole.IsAuthenticated());
}

TEST_F(AuthTest, ConnectEndToEndAndReconnectTwice)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    site.password.assign(sftptest::kPassword);
    CSshSession session(&cb, nullptr);
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(session.Connect(site)) << "round " << i << ": " << session.LastError().message;
        EXPECT_TRUE(session.IsConnected());
        EXPECT_TRUE(session.IsAuthenticated());
        EXPECT_TRUE(session.HasSftp());
        session.Disconnect();
        EXPECT_FALSE(session.IsConnected());
        EXPECT_FALSE(session.IsAuthenticated());
        EXPECT_FALSE(session.HasSftp());
    }
}

TEST_F(AuthTest, ConnectFailureClosesEverything)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    site.password.assign("wrong");
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(session.Connect(site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::AuthFailed);
    EXPECT_FALSE(session.IsConnected());
    site.password.assign(sftptest::kPassword);
    ASSERT_TRUE(session.Connect(site)) << session.LastError().message; // reusable after a failure
}

TEST_F(AuthTest, AuthenticateWithoutTransportFailsCleanly)
{
    CSshSite site = Site();
    site.password.assign(sftptest::kPassword);
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(session.Authenticate(site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::Other);
    EXPECT_FALSE(session.IsAuthenticated());
    EXPECT_FALSE(session.OpenSftp());
    EXPECT_EQ(session.LastError().kind, SshErrorKind::Other);
}

TEST_F(AuthTest, OpenSftpBeforeAuthenticateFails)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(session.OpenTransport(site));
    EXPECT_FALSE(session.OpenSftp());
    EXPECT_EQ(session.LastError().kind, SshErrorKind::Other);
}

TEST_F(AuthTest, CancelDuringKeyboardInteractivePrompt)
{
    REQUIRE_SERVER();
    CSshSite site = Site(sftptest::kPortKbdInt);
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(session.OpenTransport(site));
    // the prompt callback blocks while a second thread cancels
    cb.answers = {sftptest::kPassword};
    cb.onPrompt = [&session]
    {
        std::thread canceller([&session] { session.Cancel(); });
        canceller.join();
    };
    EXPECT_FALSE(session.Authenticate(site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::Cancelled);
    session.Disconnect();
    // usable again after Disconnect
    cb.onPrompt = nullptr;
    cb.answers = {sftptest::kPassword};
    ASSERT_TRUE(Login(session, site)) << session.LastError().message;
}

TEST_F(AuthTest, CancelBeforeOpenSftp)
{
    REQUIRE_SERVER();
    CSshSite site = Site();
    site.password.assign(sftptest::kPassword);
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, site));
    std::thread canceller([&session] { session.Cancel(); });
    canceller.join();
    EXPECT_FALSE(session.OpenSftp());
    EXPECT_EQ(session.LastError().kind, SshErrorKind::Cancelled);
    session.Disconnect();
    ASSERT_TRUE(session.Connect(site)) << session.LastError().message;
}

TEST_F(AuthTest, CancelFromSecondThreadDuringConnect)
{
    REQUIRE_SERVER();
    CSshSite site = Site(sftptest::kPortKbdInt);
    CSshSession session(&cb, nullptr);
    cb.onPrompt = [] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); };
    cb.answers = {sftptest::kPassword};
    std::thread canceller(
        [&session]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            session.Cancel();
        });
    bool ok = session.Connect(site);
    canceller.join();
    // the cancel arrived while sshd was already waiting for the answer: whatever the timing, no hang and a clean state
    if (!ok)
        EXPECT_EQ(session.LastError().kind, SshErrorKind::Cancelled);
    session.Disconnect();
    EXPECT_FALSE(session.IsConnected());
}

TEST(KeyFile, ReadIntoWipedBuffer)
{
    CSecret key;
    std::string why;
    ASSERT_TRUE(ReadKeyFile(Key("id_ed25519"), key, why)) << why;
    ASSERT_GT(key.size(), 100u);
    EXPECT_EQ(std::string(key.data(), 11), "-----BEGIN ");
    EXPECT_FALSE(IsPuttyKey(key));

    // the CSecret keeps its allocation after clear(), so inspecting it reads no freed memory
    const char* buffer = key.data();
    const size_t capacity = key.capacity();
    key.clear();
    bool allZero = true;
    for (size_t i = 0; i < capacity; ++i)
        allZero = allZero && buffer[i] == '\0';
    EXPECT_TRUE(allZero);
}

TEST(KeyFile, DetectsPutty)
{
    CSecret key;
    std::string why;
    ASSERT_TRUE(ReadKeyFile(Key("rsa_v2.ppk"), key, why)) << why;
    EXPECT_TRUE(IsPuttyKey(key));
}

TEST(KeyFile, MissingAndEmptyPath)
{
    CSecret key("leftover");
    std::string why;
    EXPECT_FALSE(ReadKeyFile(Key("nope"), key, why));
    EXPECT_FALSE(why.empty());
    EXPECT_TRUE(key.empty());
    EXPECT_FALSE(ReadKeyFile("", key, why));
}
