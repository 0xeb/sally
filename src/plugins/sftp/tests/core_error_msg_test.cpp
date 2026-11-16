// The translatable error ids (SshMsg) set by CSshSession; no server needed.
#include "test_support.h"

#include <windows.h>

#include <libssh2.h>

#include <filesystem>
#include <fstream>
#include <vector>

#include "key_file.h"
#include "known_hosts.h"
#include "ssh_init.h"
#include "ssh_session.h"

namespace
{
struct CNoCallbacks : ISshCallbacks
{
    HostKeyDecision VerifyHostKey(const std::string&, int, const std::string&, const std::string&, bool) override
    {
        return HostKeyDecision::Reject;
    }
    bool Prompt(PromptKind, const std::string&, const std::string&, const std::string&, bool, CSecret&) override { return false; }
    bool Progress(uint64_t, uint64_t) override { return true; }
};
} // namespace

TEST(ErrorMsg, ExecOnASessionThatWasNeverConnectedIsSessionNotOpen)
{
    ASSERT_TRUE(SshGlobalInit());
    {
        CNoCallbacks cb;
        CSshSession session(&cb, nullptr);
        CExecResult result;
        EXPECT_FALSE(session.Exec("echo x", CExecOptions(), result));
        const CSshError& error = session.LastError();
        EXPECT_EQ(error.msgId, SshMsg::SessionNotOpen);
        EXPECT_TRUE(error.args.empty());
        EXPECT_EQ(error.message, "The SSH session is not open"); // the English text stays
    }
    SshGlobalExit();
}

TEST(ErrorMsg, UnresolvableHostIsResolveFailedWithHostAndSystemText)
{
    ASSERT_TRUE(SshGlobalInit());
    {
        CNoCallbacks cb;
        CSshSession session(&cb, nullptr);
        CSshSite site;
        site.host = "no-such-host.invalid"; // .invalid never resolves (RFC 2606)
        site.timeoutSeconds = 20;
        EXPECT_FALSE(session.Connect(site));
        const CSshError& error = session.LastError();
        // a very slow DNS could end as a timeout instead; both are converted ids with the host as the first argument
        ASSERT_TRUE(error.msgId == SshMsg::ResolveFailed || error.msgId == SshMsg::ResolveTimeout) << error.message;
        ASSERT_EQ(error.args.size(), 2u);
        EXPECT_EQ(error.args[0], "no-such-host.invalid");
        EXPECT_FALSE(error.args[1].empty());
        EXPECT_NE(error.message.find("no-such-host.invalid"), std::string::npos);
    }
    SshGlobalExit();
}

TEST(ErrorMsg, ACancelledConnectCarriesNoId)
{
    ASSERT_TRUE(SshGlobalInit());
    {
        CNoCallbacks cb;
        CSshSession session(&cb, nullptr);
        session.Cancel();
        CSshSite site;
        site.host = "no-such-host.invalid";
        EXPECT_FALSE(session.Connect(site));
        EXPECT_EQ(session.LastError().kind, SshErrorKind::Cancelled);
        EXPECT_EQ(session.LastError().msgId, SshMsg::None);
        EXPECT_TRUE(session.LastError().args.empty());
    }
    SshGlobalExit();
}

TEST(ErrorMsg, EmptyHostIsNoHostGiven)
{
    ASSERT_TRUE(SshGlobalInit());
    {
        CNoCallbacks cb;
        CSshSession session(&cb, nullptr);
        CSshSite site; // host stays empty
        EXPECT_FALSE(session.Connect(site));
        EXPECT_EQ(session.LastError().msgId, SshMsg::NoHostGiven);
        EXPECT_TRUE(session.LastError().args.empty());
        EXPECT_EQ(session.LastError().message, "No host name given");
    }
    SshGlobalExit();
}

TEST(ErrorMsg, AuthenticateAndOpenSftpOnAClosedSession)
{
    ASSERT_TRUE(SshGlobalInit());
    {
        CNoCallbacks cb;
        CSshSession session(&cb, nullptr);
        CSshSite site;
        EXPECT_FALSE(session.Authenticate(site));
        EXPECT_EQ(session.LastError().msgId, SshMsg::AuthNeedsConnection);
        EXPECT_FALSE(session.OpenSftp());
        EXPECT_EQ(session.LastError().msgId, SshMsg::SftpNeedsAuth);
    }
    SshGlobalExit();
}

TEST(ErrorMsg, MissingKeyFileIsKeyFileOpenFailedWithPathAndWindowsError)
{
    const std::string path = "C:/no-such-folder-sally/id_nope";
    CSecret key;
    CSshError error;
    EXPECT_FALSE(ReadKeyFile(path, key, error));
    EXPECT_EQ(error.kind, SshErrorKind::KeyFileError);
    EXPECT_EQ(error.msgId, SshMsg::KeyFileOpenFailed);
    ASSERT_EQ(error.args.size(), 2u);
    EXPECT_EQ(error.args[0], path);
    EXPECT_EQ(error.args[1], std::to_string(ERROR_PATH_NOT_FOUND));
    EXPECT_NE(error.message.find("Cannot open the key file"), std::string::npos);
}

TEST(ErrorMsg, EmptyKeyFilePathAndEmptyFile)
{
    CSecret key;
    CSshError error;
    EXPECT_FALSE(ReadKeyFile("", key, error));
    EXPECT_EQ(error.msgId, SshMsg::KeyFilePathInvalid);

    const std::filesystem::path empty =
        std::filesystem::temp_directory_path() / ("sally_msg_empty_" + std::to_string(GetCurrentProcessId()));
    { std::ofstream out(empty, std::ios::binary); }
    EXPECT_FALSE(ReadKeyFile(empty.string(), key, error));
    EXPECT_EQ(error.msgId, SshMsg::KeyFileBadSize);
    ASSERT_EQ(error.args.size(), 1u);
    std::error_code ec;
    std::filesystem::remove(empty, ec);
}

TEST(ErrorMsg, KnownHostsWithoutAFileAndWithAnUnwritableFile)
{
    ASSERT_TRUE(SshGlobalInit());
    {
        LIBSSH2_SESSION* session = libssh2_session_init();
        const char key[] = "0123456789abcdef0123456789abcdef";
        CSshError error;

        CKnownHosts none((std::string()));
        EXPECT_FALSE(none.AddOrReplace(session, "h", 22, key, sizeof(key) - 1, LIBSSH2_HOSTKEY_TYPE_ED25519, &error));
        EXPECT_EQ(error.msgId, SshMsg::NoKnownHostsFile);

        // a regular file where the folder of known_hosts should be: the file cannot be written
        const std::filesystem::path blocker =
            std::filesystem::temp_directory_path() / ("sally_msg_blocker_" + std::to_string(GetCurrentProcessId()));
        { std::ofstream out(blocker, std::ios::binary); out << "x"; }
        CKnownHosts blocked((blocker / "known_hosts").string());
        error = CSshError();
        EXPECT_FALSE(blocked.AddOrReplace(session, "h", 22, key, sizeof(key) - 1, LIBSSH2_HOSTKEY_TYPE_ED25519, &error));
        EXPECT_EQ(error.msgId, SshMsg::KnownHostsWriteFailed);
        ASSERT_EQ(error.args.size(), 1u);
        EXPECT_NE(error.message.find(error.args[0]), std::string::npos);
        std::error_code ec;
        std::filesystem::remove(blocker, ec);
        libssh2_session_free(session);
    }
    SshGlobalExit();
}

TEST(ErrorMsg, SftpCallOnASessionWithoutSftpIsSftpNotOpen)
{
    ASSERT_TRUE(SshGlobalInit());
    {
        CNoCallbacks cb;
        CSshSession session(&cb, nullptr);
        std::vector<CSftpEntry> list;
        EXPECT_FALSE(session.ListDir("/", list));
        EXPECT_EQ(session.LastError().msgId, SshMsg::SftpNotOpen);
        EXPECT_TRUE(session.LastError().args.empty());
        EXPECT_EQ(session.LastError().message, "The SFTP session is not open");
    }
    SshGlobalExit();
}

TEST(ErrorMsg, ACancelledSftpCallIsKindCancelledWithoutId)
{
    ASSERT_TRUE(SshGlobalInit());
    {
        CNoCallbacks cb;
        CSshSession session(&cb, nullptr);
        session.Cancel();
        std::vector<CSftpEntry> list;
        EXPECT_FALSE(session.ListDir("/", list));
        EXPECT_EQ(session.LastError().kind, SshErrorKind::Cancelled);
        EXPECT_EQ(session.LastError().msgId, SshMsg::None); // fs/ shows its "cancelled" text for the kind
        EXPECT_EQ(session.LastError().message, "The operation was cancelled");
    }
    SshGlobalExit();
}

TEST(ErrorMsg, ConnectTimeoutArgumentsArePlainNumbers)
{
    ASSERT_TRUE(SshGlobalInit());
    {
        CNoCallbacks cb;
        CSshSession session(&cb, nullptr);
        CSshSite site;
        site.host = "192.0.2.1"; // TEST-NET-1 (RFC 5737): nothing answers
        site.timeoutSeconds = 1;
        EXPECT_FALSE(session.Connect(site));
        const CSshError& error = session.LastError();
        if (error.msgId != SshMsg::ConnectTimeout) // a network that rejects at once gives another error: nothing to check
        {
            SshGlobalExit();
            GTEST_SKIP() << "no timeout on this network: " << error.message;
        }
        ASSERT_EQ(error.args.size(), 2u);
        EXPECT_EQ(error.args[0], "192.0.2.1:22");
        EXPECT_EQ(error.args[1], "1"); // the unit "s" is in the pattern
        EXPECT_EQ(error.message, "Connecting to 192.0.2.1:22 timed out after 1 s");
    }
    SshGlobalExit();
}
