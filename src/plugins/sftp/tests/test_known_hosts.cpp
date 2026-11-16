#include "test_support.h"

#include <libssh2.h>
#include <openssl/evp.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "known_hosts.h"
#include "ssh_init.h"

namespace
{
// ssh-keygen -lf id_ed25519.pub of the fixture key: SHA256:9dr5PHnouIKtz1HXjVrD07Irv2gZD3EQuf0xya3Yr68
const char kSampleBlobBase64[] = "AAAAC3NzaC1lZDI1NTE5AAAAIEryyFvapkyohP5ZNq+Ple1JD27mU8E/QA3yz+hx8aUu";
const char kSampleFingerprint[] = "SHA256:9dr5PHnouIKtz1HXjVrD07Irv2gZD3EQuf0xya3Yr68";
// a second, different ed25519 key (id_ed25519_pass.pub is not used: this one is self contained)
const char kOtherBlobBase64[] = "AAAAC3NzaC1lZDI1NTE5AAAAIPCXkH/dYSFrBOlTqCsONq3uq3/NVgQ9Ti8sm0cmbPdm";

std::string Decode64(const char* text)
{
    std::string out(strlen(text), '\0');
    int n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(&out[0]), reinterpret_cast<const unsigned char*>(text),
                            static_cast<int>(strlen(text)));
    out.resize(n < 0 ? 0 : static_cast<size_t>(n));
    return out;
}

std::string ReadAll(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

class KnownHostsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(SshGlobalInit());
        session_ = libssh2_session_init(); // knownhost lists need a session for their allocator
        dir_ = std::filesystem::temp_directory_path() /
               (std::string("sally_kh_") + std::to_string(GetCurrentProcessId()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }
    void TearDown() override
    {
        libssh2_session_free(session_);
        SshGlobalExit();
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    std::string P(const char* name) const { return (dir_ / name).string(); }

    LIBSSH2_SESSION* session_ = nullptr;
    std::filesystem::path dir_;
};
} // namespace

TEST(Fingerprint, SampleKey)
{
    ASSERT_TRUE(SshGlobalInit());
    std::string blob = Decode64(kSampleBlobBase64);
    ASSERT_EQ(blob.size(), 51u);
    EXPECT_EQ(Sha256Fingerprint(blob.data(), blob.size()), kSampleFingerprint);
    SshGlobalExit();
}

TEST(Fingerprint, EmptyInputIsStillWellFormed)
{
    ASSERT_TRUE(SshGlobalInit());
    // SHA-256 of nothing: 47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU=
    EXPECT_EQ(Sha256Fingerprint("", 0), "SHA256:47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU");
    SshGlobalExit();
}

TEST(KnownHostName, PortRule)
{
    EXPECT_EQ(KnownHostName("example.com", 22), "example.com");
    EXPECT_EQ(KnownHostName("example.com", 2222), "[example.com]:2222");
}

TEST_F(KnownHostsTest, UnknownHostIsNotFound)
{
    std::string blob = Decode64(kSampleBlobBase64);
    CKnownHosts hosts(P("own"));
    EXPECT_EQ(hosts.Check(session_, "myhost", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::NotFound);
}

TEST_F(KnownHostsTest, AddThenMatchAndFileFormat)
{
    std::string blob = Decode64(kSampleBlobBase64);
    CKnownHosts hosts(P("own"));
    CSshError err;
    ASSERT_TRUE(hosts.AddOrReplace(session_, "myhost", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519, &err))
        << err.message;
    EXPECT_EQ(hosts.Check(session_, "myhost", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Match);
    EXPECT_FALSE(std::filesystem::exists(P("own.tmp")));

    std::string text = ReadAll(P("own"));
    EXPECT_EQ(text, std::string("myhost ssh-ed25519 ") + kSampleBlobBase64 + "\n");
}

TEST_F(KnownHostsTest, ReplaceChangedKey)
{
    std::string blob = Decode64(kSampleBlobBase64);
    std::string other = Decode64(kOtherBlobBase64);
    CKnownHosts hosts(P("own"));
    CSshError err;
    ASSERT_TRUE(hosts.AddOrReplace(session_, "myhost", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519, &err));
    EXPECT_EQ(hosts.Check(session_, "myhost", 22, other.data(), other.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Mismatch);

    ASSERT_TRUE(hosts.AddOrReplace(session_, "myhost", 22, other.data(), other.size(), LIBSSH2_HOSTKEY_TYPE_ED25519, &err));
    EXPECT_EQ(hosts.Check(session_, "myhost", 22, other.data(), other.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Match);
    EXPECT_EQ(hosts.Check(session_, "myhost", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Mismatch);
    EXPECT_EQ(ReadAll(P("own")), std::string("myhost ssh-ed25519 ") + kOtherBlobBase64 + "\n"); // one entry, not two
}

TEST_F(KnownHostsTest, NonStandardPortUsesBracketName)
{
    std::string blob = Decode64(kSampleBlobBase64);
    std::string other = Decode64(kOtherBlobBase64);
    CKnownHosts hosts(P("own"));
    CSshError err;
    ASSERT_TRUE(hosts.AddOrReplace(session_, "myhost", 2222, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519, &err));
    EXPECT_EQ(ReadAll(P("own")), std::string("[myhost]:2222 ssh-ed25519 ") + kSampleBlobBase64 + "\n");

    EXPECT_EQ(hosts.Check(session_, "myhost", 2222, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Match);
    // the same host on port 22 is another entry
    EXPECT_EQ(hosts.Check(session_, "myhost", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::NotFound);
    // a different port with a different key does not replace it
    ASSERT_TRUE(hosts.AddOrReplace(session_, "myhost", 2223, other.data(), other.size(), LIBSSH2_HOSTKEY_TYPE_ED25519, &err));
    EXPECT_EQ(hosts.Check(session_, "myhost", 2222, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Match);
}

TEST_F(KnownHostsTest, ReadsOpenSshFileWithCommentsAndHashedEntry)
{
    std::string blob = Decode64(kSampleBlobBase64);
    {
        std::ofstream out(P("own"), std::ios::binary);
        out << "# a comment\r\n\r\nexample.com,192.0.2.1 ssh-ed25519 " << kSampleBlobBase64 << " comment\r\n";
    }
    CKnownHosts hosts(P("own"));
    EXPECT_EQ(hosts.Check(session_, "example.com", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Match);
    EXPECT_EQ(hosts.Check(session_, "192.0.2.1", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Match);
}

TEST_F(KnownHostsTest, UserFileIsReadButNeverWritten)
{
    std::string blob = Decode64(kSampleBlobBase64);
    std::string other = Decode64(kOtherBlobBase64);
    const std::string userText = std::string("userhost ssh-ed25519 ") + kSampleBlobBase64 + "\n";
    {
        std::ofstream out(P("user"), std::ios::binary);
        out << userText;
    }
    CKnownHosts hosts(P("own"), P("user"));
    EXPECT_EQ(hosts.Check(session_, "userhost", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Match);
    EXPECT_EQ(hosts.Check(session_, "userhost", 22, other.data(), other.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Mismatch);

    CSshError err;
    ASSERT_TRUE(hosts.AddOrReplace(session_, "userhost", 22, other.data(), other.size(), LIBSSH2_HOSTKEY_TYPE_ED25519,
                                   &err));
    EXPECT_EQ(ReadAll(P("user")), userText); // byte for byte unchanged
    // the own file holds only what was added, not a copy of the user's entries
    EXPECT_EQ(ReadAll(P("own")), std::string("userhost ssh-ed25519 ") + kOtherBlobBase64 + "\n");
}

TEST_F(KnownHostsTest, CreatesMissingFolder)
{
    std::string blob = Decode64(kSampleBlobBase64);
    CKnownHosts hosts(P("sub\\deeper\\known_hosts"));
    CSshError err;
    EXPECT_TRUE(hosts.AddOrReplace(session_, "myhost", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519, &err))
        << err.message;
}

namespace
{
// A synthetic ssh-rsa blob (type string + junk): libssh2's known-host list never parses the key itself
std::string FakeRsaBlob()
{
    std::string blob("\0\0\0\7ssh-rsa", 11);
    blob += std::string(40, '\x5A');
    return blob;
}
} // namespace

TEST_F(KnownHostsTest, HostKnownUnderAnotherKeyTypeIsMismatch)
{
    std::string ed = Decode64(kSampleBlobBase64);
    std::string rsa = FakeRsaBlob();
    CKnownHosts hosts(P("own"));
    CSshError err;
    ASSERT_TRUE(hosts.AddOrReplace(session_, "myhost", 22, ed.data(), ed.size(), LIBSSH2_HOSTKEY_TYPE_ED25519, &err));
    // the server now presents an RSA key: the host is known, so this is a change, not a first contact
    EXPECT_EQ(hosts.Check(session_, "myhost", 22, rsa.data(), rsa.size(), LIBSSH2_HOSTKEY_TYPE_RSA),
              HostKeyCheck::Mismatch);
    EXPECT_EQ(hosts.Check(session_, "otherhost", 22, rsa.data(), rsa.size(), LIBSSH2_HOSTKEY_TYPE_RSA),
              HostKeyCheck::NotFound);

    // accepting the changed key replaces every entry of the host, whatever its type
    ASSERT_TRUE(hosts.AddOrReplace(session_, "myhost", 22, rsa.data(), rsa.size(), LIBSSH2_HOSTKEY_TYPE_RSA, &err));
    EXPECT_EQ(hosts.Check(session_, "myhost", 22, rsa.data(), rsa.size(), LIBSSH2_HOSTKEY_TYPE_RSA), HostKeyCheck::Match);
    EXPECT_EQ(hosts.Check(session_, "myhost", 22, ed.data(), ed.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Mismatch);
    const std::string text = ReadAll(P("own"));
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 1) << text;
    EXPECT_EQ(text.find("ssh-ed25519"), std::string::npos) << text;
}

TEST_F(KnownHostsTest, ReplaceRemovesEntriesOfAllTypesOfTheHostOnly)
{
    std::string ed = Decode64(kSampleBlobBase64);
    std::string other = Decode64(kOtherBlobBase64);
    std::string rsa = FakeRsaBlob();
    std::string rsaText;
    rsaText.resize(((rsa.size() + 2) / 3) * 4 + 4);
    int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&rsaText[0]),
                            reinterpret_cast<const unsigned char*>(rsa.data()), static_cast<int>(rsa.size()));
    rsaText.resize(static_cast<size_t>(n));
    {
        std::ofstream out(P("own"), std::ios::binary);
        out << "myhost ssh-ed25519 " << kSampleBlobBase64 << "\nmyhost ssh-rsa " << rsaText << "\nother ssh-ed25519 "
            << kSampleBlobBase64 << "\n";
    }
    CKnownHosts hosts(P("own"));
    CSshError err;
    ASSERT_TRUE(hosts.AddOrReplace(session_, "myhost", 22, other.data(), other.size(), LIBSSH2_HOSTKEY_TYPE_ED25519,
                                   &err));
    const std::string text = ReadAll(P("own"));
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 2) << text;
    EXPECT_EQ(text.find("ssh-rsa"), std::string::npos) << text;
    EXPECT_EQ(hosts.Check(session_, "other", 22, ed.data(), ed.size(), LIBSSH2_HOSTKEY_TYPE_ED25519), HostKeyCheck::Match);
    EXPECT_EQ(hosts.Check(session_, "myhost", 22, other.data(), other.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Match);
}

TEST_F(KnownHostsTest, OtherPortNeverFallsBackToBareHost)
{
    std::string blob = Decode64(kSampleBlobBase64);
    std::string other = Decode64(kOtherBlobBase64);
    CKnownHosts hosts(P("own"));
    CSshError err;
    ASSERT_TRUE(hosts.AddOrReplace(session_, "myhost", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519, &err));
    // the entry of the bare host (port 22) says nothing about port 2222: neither a match nor a mismatch
    EXPECT_EQ(hosts.Check(session_, "myhost", 2222, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::NotFound);
    EXPECT_EQ(hosts.Check(session_, "myhost", 2222, other.data(), other.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::NotFound);
}

TEST_F(KnownHostsTest, HostNamesAreCaseInsensitive)
{
    std::string blob = Decode64(kSampleBlobBase64);
    CKnownHosts hosts(P("own"));
    CSshError err;
    ASSERT_TRUE(hosts.AddOrReplace(session_, "MyHost.Example", 22, blob.data(), blob.size(),
                                   LIBSSH2_HOSTKEY_TYPE_ED25519, &err));
    EXPECT_EQ(ReadAll(P("own")), std::string("myhost.example ssh-ed25519 ") + kSampleBlobBase64 + "\n");
    EXPECT_EQ(hosts.Check(session_, "MYHOST.EXAMPLE", 22, blob.data(), blob.size(), LIBSSH2_HOSTKEY_TYPE_ED25519),
              HostKeyCheck::Match);
    EXPECT_EQ(KnownHostName("Host", 2222), "[host]:2222");
    EXPECT_EQ(LowerHost("AbC.D\xC3\xA9"), "abc.d\xC3\xA9"); // ASCII only: other bytes are left alone
}
