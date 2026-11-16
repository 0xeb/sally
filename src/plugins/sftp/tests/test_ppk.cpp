#include "test_support.h"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "key_file.h"
#include "ppk.h"
#include "ssh_init.h"
#include "ssh_session.h"

namespace
{
std::string Path(const std::string& name)
{
    return (std::filesystem::path(sftptest::FixtureDir()) / name).string();
}

std::string ReadText(const std::string& name)
{
    std::ifstream in(Path(name), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string Hex(const std::string& bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : bytes)
    {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 15]);
    }
    return out;
}

std::string Base64Decode(const std::string& text)
{
    std::string out;
    uint32_t bits = 0;
    int count = 0;
    for (char c : text)
    {
        int v = -1;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 52;
        else if (c == '+')
            v = 62;
        else if (c == '/')
            v = 63;
        if (v < 0)
            continue;
        bits = (bits << 6) | static_cast<uint32_t>(v);
        if (++count == 4)
        {
            out += {static_cast<char>(bits >> 16), static_cast<char>(bits >> 8), static_cast<char>(bits)};
            count = 0;
        }
    }
    if (count == 2)
        out.push_back(static_cast<char>(bits >> 4));
    else if (count == 3)
        out += {static_cast<char>(bits >> 10), static_cast<char>(bits >> 2)};
    return out;
}

std::string Base64Encode(const std::string& in)
{
    static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < in.size(); i += 3)
    {
        const size_t left = in.size() - i;
        uint32_t v = static_cast<unsigned char>(in[i]) << 16;
        if (left > 1)
            v |= static_cast<unsigned char>(in[i + 1]) << 8;
        if (left > 2)
            v |= static_cast<unsigned char>(in[i + 2]);
        out += a[(v >> 18) & 63];
        out += a[(v >> 12) & 63];
        out += left > 1 ? a[(v >> 6) & 63] : '=';
        out += left > 2 ? a[v & 63] : '=';
    }
    return out;
}

// Lines of a header field block: the base64 text of "Public-Lines" decoded.
std::string PublicBlobOf(const std::string& ppk)
{
    std::istringstream in(ppk);
    std::string line, b64;
    while (std::getline(in, line))
    {
        if (line.rfind("Public-Lines: ", 0) == 0)
        {
            int n = std::stoi(line.substr(14));
            for (int i = 0; i < n && std::getline(in, line); ++i)
                b64 += line.substr(0, line.find_first_of("\r\n"));
            break;
        }
    }
    return Base64Decode(b64);
}

// the second field of an OpenSSH .pub file, decoded
std::string PubFileBlob(const std::string& name)
{
    std::istringstream in(ReadText(name));
    std::string type, b64;
    in >> type >> b64;
    return Base64Decode(b64);
}

std::string Replace(std::string text, const std::string& from, const std::string& to)
{
    const size_t pos = text.find(from);
    if (pos != std::string::npos)
        text.replace(pos, from.size(), to);
    return text;
}

void PutString(std::string& out, const std::string& s)
{
    const uint32_t n = static_cast<uint32_t>(s.size());
    out += {static_cast<char>(n >> 24), static_cast<char>(n >> 16), static_cast<char>(n >> 8), static_cast<char>(n)};
    out += s;
}

// A valid unencrypted v3 file around the given blobs (the MAC key of a plain v3 key is empty).
std::string MakePlainPpk(const std::string& type, const std::string& pub, const std::string& priv)
{
    std::string data;
    PutString(data, type);
    PutString(data, "none");
    PutString(data, "c");
    PutString(data, pub);
    PutString(data, priv);
    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    EVP_MAC_CTX* ctx = EVP_MAC_CTX_new(mac);
    char digest[] = "SHA256";
    OSSL_PARAM params[2] = {OSSL_PARAM_construct_utf8_string("digest", digest, 0), OSSL_PARAM_construct_end()};
    unsigned char result[64];
    size_t length = 0;
    EVP_MAC_init(ctx, reinterpret_cast<const unsigned char*>(""), 0, params);
    EVP_MAC_update(ctx, reinterpret_cast<const unsigned char*>(data.data()), data.size());
    EVP_MAC_final(ctx, result, &length, sizeof(result));
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return "PuTTY-User-Key-File-3: " + type + "\nEncryption: none\nComment: c\nPublic-Lines: 1\n" + Base64Encode(pub) +
           "\nPrivate-Lines: 1\n" + Base64Encode(priv) + "\nPrivate-MAC: " +
           Hex(std::string(reinterpret_cast<char*>(result), length)) + "\n";
}

struct CFixture
{
    const char* file;
    bool encrypted;
    const char* type;
    const char* pubFile; // OpenSSH .pub of the same key
};

const CFixture kFixtures[] = {
    {"ed25519_v2.ppk", false, "ssh-ed25519", "id_ed25519.pub"},
    {"ed25519_v3.ppk", false, "ssh-ed25519", "id_ed25519.pub"},
    {"ed25519_pass_v2.ppk", true, "ssh-ed25519", "id_ed25519_pass.pub"},
    {"ed25519_pass_v3.ppk", true, "ssh-ed25519", "id_ed25519_pass.pub"},
    {"rsa_v2.ppk", false, "ssh-rsa", "id_rsa.pub"},
    {"rsa_v3.ppk", false, "ssh-rsa", "id_rsa.pub"},
    {"rsa_pass_v2.ppk", true, "ssh-rsa", "id_rsa_pass.pub"},
    {"rsa_pass_v3.ppk", true, "ssh-rsa", "id_rsa_pass.pub"},
    {"ecdsa_v2.ppk", false, "ecdsa-sha2-nistp256", "id_ecdsa.pub"},
    {"ecdsa_v3.ppk", false, "ecdsa-sha2-nistp256", "id_ecdsa.pub"},
};

struct CPromptCallbacks : ISshCallbacks
{
    std::vector<std::string> answers;
    bool refuse = false;
    int prompts = 0; // passphrase prompts only (keyboard-interactive may follow a failed key)
    HostKeyDecision VerifyHostKey(const std::string&, int, const std::string&, const std::string&, bool) override
    {
        return HostKeyDecision::AcceptOnce;
    }
    bool Prompt(PromptKind kind, const std::string&, const std::string&, const std::string&, bool, CSecret& answer) override
    {
        if (kind != PromptKind::KeyPassphrase)
        {
            answer.assign(std::string()); // a keyboard-interactive prompt after the key failed: empty answer
            return true;
        }
        ++prompts;
        if (refuse)
            return false;
        answer.assign(answers.empty() ? std::string() : answers.front());
        if (!answers.empty())
            answers.erase(answers.begin());
        return true;
    }
    bool Progress(uint64_t, uint64_t) override { return true; }
};

class PpkTest : public ::testing::Test
{
protected:
    void SetUp() override { ASSERT_TRUE(SshGlobalInit()); }
    void TearDown() override { SshGlobalExit(); }

    CSecret Load(const std::string& name)
    {
        CSecret bytes;
        std::string why;
        if (!ReadKeyFile(Path(name), bytes, why))
            return CSecret();
        return bytes;
    }
    std::string Passphrase() { return ReadText("passphrase.txt"); }

    bool Login(CSshSession& session, const CSshSite& site)
    {
        return session.OpenTransport(site) && session.Authenticate(site);
    }
    CSshSite Site(const std::string& keyFile, const std::string& pass = "")
    {
        CSshSite site;
        site.host = sftptest::kHost;
        site.port = sftptest::kPort;
        site.user = sftptest::kUser;
        site.timeoutSeconds = 10;
        site.keyFile = Path(keyFile);
        if (!pass.empty())
            site.passphrase.assign(pass);
        return site;
    }
    CSecret Secret(const std::string& s) { return CSecret(s); }

    CPromptCallbacks cb;
};
} // namespace

#define LOAD_OR_SKIP(var, name)                                                                                \
    CSecret var = Load(name);                                                                                  \
    if (var.empty())                                                                                           \
        GTEST_SKIP() << "Fixture " << (name) << " is missing - run tests/docker/setup.ps1";

TEST_F(PpkTest, KnownAnswerV2Kdf)
{
    // computed independently (Python hashlib) for the passphrase "sally-test"
    CSecret key, mac;
    ASSERT_TRUE(PpkV2CipherKey(CSecret("sally-test"), key));
    EXPECT_EQ(Hex(key.ToStdString()), "b07c70fff777d25395781fc74d925f89826fc7d7afeb82d33a0e9219734c60b0");
    ASSERT_TRUE(PpkV2MacKey(CSecret("sally-test"), mac));
    EXPECT_EQ(Hex(mac.ToStdString()), "de855d07a495d1147d3947050574a28c20934ea9");
}

TEST_F(PpkTest, ConvertsEveryFixtureAndKeepsThePublicKey)
{
    for (const CFixture& f : kFixtures)
    {
        LOAD_OR_SKIP(file, f.file);
        EXPECT_EQ(PpkIsEncrypted(file), f.encrypted) << f.file;
        CSecret pem;
        std::string blob;
        CSshError err;
        ASSERT_TRUE(ConvertPpk(file, CSecret(f.encrypted ? Passphrase() : ""), pem, &blob, err))
            << f.file << ": " << err.message;
        EXPECT_EQ(pem.ToStdString().rfind("-----BEGIN OPENSSH PRIVATE KEY-----\n", 0), 0u) << f.file;
        EXPECT_EQ(blob, PublicBlobOf(file.ToStdString())) << f.file;
        EXPECT_EQ(blob, PubFileBlob(f.pubFile)) << f.file; // the same key the server authorizes
        // the public key is also inside the converted key
        std::string body = pem.ToStdString();
        body = body.substr(body.find("\n") + 1);
        body.resize(body.rfind("-----END"));
        EXPECT_NE(Base64Decode(body).find(blob), std::string::npos) << f.file;
    }
}

TEST_F(PpkTest, WrongOrMissingPassphraseIsKeyFileError)
{
    for (const char* name : {"ed25519_pass_v2.ppk", "ed25519_pass_v3.ppk", "rsa_pass_v2.ppk", "rsa_pass_v3.ppk"})
    {
        LOAD_OR_SKIP(file, name);
        for (const char* pass : {"wrong", ""})
        {
            CSecret pem;
            CSshError err;
            EXPECT_FALSE(ConvertPpk(file, CSecret(pass), pem, nullptr, err)) << name;
            EXPECT_EQ(err.kind, SshErrorKind::KeyFileError) << name;
            EXPECT_NE(err.message.find("passphrase"), std::string::npos) << err.message;
            EXPECT_TRUE(pem.empty());
        }
    }
}

TEST_F(PpkTest, FlippedPrivateByteIsDetectedByTheMac)
{
    for (const char* name : {"ed25519_v2.ppk", "ed25519_v3.ppk", "rsa_v3.ppk", "ed25519_pass_v3.ppk"})
    {
        LOAD_OR_SKIP(file, name);
        std::string text = file.ToStdString();
        const size_t at = text.find("Private-Lines:");
        ASSERT_NE(at, std::string::npos);
        const size_t body = text.find('\n', at) + 1;
        text[body + 3] = text[body + 3] == 'A' ? 'B' : 'A';
        CSecret pem;
        CSshError err;
        EXPECT_FALSE(ConvertPpk(CSecret(text), CSecret(Passphrase()), pem, nullptr, err)) << name;
        EXPECT_EQ(err.kind, SshErrorKind::KeyFileError) << name;
        const bool encrypted = std::string(name).find("pass") != std::string::npos;
        EXPECT_NE(err.message.find(encrypted ? "passphrase" : "damaged"), std::string::npos) << err.message;
    }
}

TEST_F(PpkTest, ModifiedHeaderFieldsFailTheMac)
{
    LOAD_OR_SKIP(file, "ed25519_v3.ppk");
    CSecret pem;
    CSshError err;
    // the comment is part of the MAC
    EXPECT_FALSE(ConvertPpk(CSecret(Replace(file.ToStdString(), "Comment: sally-test", "Comment: other")),
                            CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.kind, SshErrorKind::KeyFileError);
}

TEST_F(PpkTest, CrLfLineEndingsAreAccepted)
{
    LOAD_OR_SKIP(file, "rsa_pass_v3.ppk");
    std::string crlf;
    for (char c : file.ToStdString())
    {
        if (c == '\n')
            crlf += '\r';
        crlf += c;
    }
    CSecret pem;
    CSshError err;
    EXPECT_TRUE(ConvertPpk(CSecret(crlf), CSecret(Passphrase()), pem, nullptr, err)) << err.message;
}

TEST_F(PpkTest, TruncatedFilesNeverCrashOrSucceed)
{
    for (const char* name : {"ed25519_v3.ppk", "rsa_pass_v2.ppk", "rsa_pass_v3.ppk", "ecdsa_v2.ppk"})
    {
        LOAD_OR_SKIP(file, name);
        const std::string text = file.ToStdString();
        for (size_t len = 0; len < text.size() - 1; len += 5)
        {
            CSecret pem;
            CSshError err;
            // dropping only the final newline would still be a complete file, so stop before that
            EXPECT_FALSE(ConvertPpk(CSecret(text.substr(0, len)), CSecret(Passphrase()), pem, nullptr, err))
                << name << " length " << len;
            EXPECT_NE(err.kind, SshErrorKind::None);
        }
    }
}

TEST_F(PpkTest, GarbageAndEmptyInput)
{
    CSshError err;
    CSecret pem;
    EXPECT_FALSE(ConvertPpk(CSecret(), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.kind, SshErrorKind::KeyFileError);
    EXPECT_FALSE(ConvertPpk(CSecret("hello world\n"), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.kind, SshErrorKind::KeyFileError);
    EXPECT_FALSE(ConvertPpk(CSecret("PuTTY-User-Key-File-3"), CSecret(), pem, nullptr, err));
    EXPECT_FALSE(ConvertPpk(CSecret("PuTTY-User-Key-File-x: ssh-rsa\n"), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.kind, SshErrorKind::KeyFileError);
    EXPECT_FALSE(ConvertPpk(CSecret(std::string(5000, '\0') + "PuTTY"), CSecret(), pem, nullptr, err));
    EXPECT_FALSE(PpkIsEncrypted(CSecret("garbage")));
}

TEST_F(PpkTest, HugeCounts)
{
    CSshError err;
    CSecret pem;
    const std::string head = "PuTTY-User-Key-File-3: ssh-ed25519\nEncryption: none\nComment: c\n";
    EXPECT_FALSE(ConvertPpk(CSecret(head + "Public-Lines: 4000000000\nAAAA\n"), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.kind, SshErrorKind::KeyFileError);
    EXPECT_FALSE(ConvertPpk(CSecret(head + "Public-Lines: 9999\nAAAA\n"), CSecret(), pem, nullptr, err));
    EXPECT_FALSE(ConvertPpk(CSecret(head + "Public-Lines: 99999999999999999999999\nAAAA\n"), CSecret(), pem, nullptr, err));
    EXPECT_FALSE(ConvertPpk(CSecret(head + "Public-Lines: -1\nAAAA\n"), CSecret(), pem, nullptr, err));
}

TEST_F(PpkTest, HugeLengthFieldsInsideTheBlobs)
{
    std::string huge = {'\xff', '\xff', '\xff', '\xff', 'x'};
    std::string goodPub;
    PutString(goodPub, "ssh-ed25519");
    PutString(goodPub, std::string(32, 'k'));
    std::string goodPriv;
    PutString(goodPriv, std::string(32, 's'));
    CSshError err;
    CSecret pem;
    // sanity: the helper builds a file that converts
    EXPECT_TRUE(ConvertPpk(CSecret(MakePlainPpk("ssh-ed25519", goodPub, goodPriv)), CSecret(), pem, nullptr, err))
        << err.message;
    // huge string length in the private blob / the public blob (the MAC is valid, so the parser itself must refuse)
    for (const char* type : {"ssh-ed25519", "ssh-rsa", "ecdsa-sha2-nistp256"})
    {
        std::string pubHuge;
        PutString(pubHuge, type);
        pubHuge += huge;
        for (const std::string& pub : {pubHuge, goodPub, std::string("\x00\x00", 2), std::string()})
            for (const std::string& priv : {huge, goodPriv, std::string("\x00\x00\x00\x05" "ab", 6)})
            {
                if (pub == goodPub && priv == goodPriv && std::string(type) == "ssh-ed25519")
                    continue;
                EXPECT_FALSE(ConvertPpk(CSecret(MakePlainPpk(type, pub, priv)), CSecret(), pem, nullptr, err))
                    << type;
                EXPECT_EQ(err.kind, SshErrorKind::KeyFileError);
            }
    }
}

TEST_F(PpkTest, UnknownKeyTypeIsUnsupported)
{
    CSshError err;
    CSecret pem;
    std::string pub;
    PutString(pub, "ssh-dss");
    EXPECT_FALSE(ConvertPpk(CSecret(MakePlainPpk("ssh-dss", pub, pub)), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.kind, SshErrorKind::Unsupported);
}

TEST_F(PpkTest, UnknownFormatVersionIsUnsupported)
{
    LOAD_OR_SKIP(file, "ed25519_v3.ppk");
    CSshError err;
    CSecret pem;
    for (const char* v : {"1", "4", "99"})
    {
        EXPECT_FALSE(ConvertPpk(CSecret(Replace(file.ToStdString(), "File-3:", std::string("File-") + v + ":")),
                                CSecret(), pem, nullptr, err));
        EXPECT_EQ(err.kind, SshErrorKind::Unsupported) << v;
    }
}

TEST_F(PpkTest, UnknownEncryptionAndKdfAreUnsupported)
{
    LOAD_OR_SKIP(file, "ed25519_pass_v3.ppk");
    CSshError err;
    CSecret pem;
    EXPECT_FALSE(ConvertPpk(CSecret(Replace(file.ToStdString(), "aes256-cbc", "chacha")), CSecret(Passphrase()), pem,
                            nullptr, err));
    EXPECT_EQ(err.kind, SshErrorKind::Unsupported);
    EXPECT_FALSE(ConvertPpk(CSecret(Replace(file.ToStdString(), "Key-Derivation: Argon2id", "Key-Derivation: scrypt")),
                            CSecret(Passphrase()), pem, nullptr, err));
    EXPECT_EQ(err.kind, SshErrorKind::Unsupported);
}

TEST_F(PpkTest, Argon2ParametersAreValidatedAndCapped)
{
    LOAD_OR_SKIP(file, "ed25519_pass_v3.ppk");
    const std::string text = file.ToStdString();
    const size_t at = text.find("Argon2-Memory: ");
    ASSERT_NE(at, std::string::npos);
    const size_t end = text.find('\n', at);
    auto with = [&](const std::string& field)
    { return text.substr(0, at) + field + text.substr(end); };
    CSshError err;
    CSecret pem;
    const auto start = std::chrono::steady_clock::now();
    for (const char* mem : {"4294967295", "0", "7", "99999999999999999999", "abc"})
    {
        EXPECT_FALSE(ConvertPpk(CSecret(with(std::string("Argon2-Memory: ") + mem)), CSecret(Passphrase()), pem, nullptr,
                                err))
            << mem;
        EXPECT_EQ(err.kind, SshErrorKind::KeyFileError) << mem;
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5)); // refused, not computed
}

TEST_F(PpkTest, FailureLeavesNoKeyMaterialInTheOutput)
{
    LOAD_OR_SKIP(file, "rsa_pass_v2.ppk");
    CSecret pem("previous content");
    CSshError err;
    EXPECT_FALSE(ConvertPpk(file, CSecret("wrong"), pem, nullptr, err));
    EXPECT_TRUE(pem.empty());

    // after a success the converted key is wiped by clear(), like any CSecret
    ASSERT_TRUE(ConvertPpk(file, CSecret(Passphrase()), pem, nullptr, err));
    const char* buffer = pem.data();
    const size_t capacity = pem.capacity();
    ASSERT_GT(pem.size(), 100u);
    pem.clear();
    for (size_t i = 0; i < capacity; ++i)
        ASSERT_EQ(buffer[i], '\0');
}

// ---- login with the converted keys --------------------------------------------------------------------------

TEST_F(PpkTest, LoginWithEveryFixture)
{
    REQUIRE_SERVER();
    for (const CFixture& f : kFixtures)
    {
        LOAD_OR_SKIP(file, f.file);
        CSshSession session(&cb, nullptr);
        CSshSite site = Site(f.file, f.encrypted ? Passphrase() : "");
        ASSERT_TRUE(Login(session, site)) << f.file << ": " << session.LastError().message;
        EXPECT_TRUE(session.IsAuthenticated());
    }
    EXPECT_EQ(cb.prompts, 0);
}

TEST_F(PpkTest, PassphraseFromCallbackIsAskedOnce)
{
    REQUIRE_SERVER();
    LOAD_OR_SKIP(file, "rsa_pass_v3.ppk");
    cb.answers = {Passphrase()};
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, Site("rsa_pass_v3.ppk"))) << session.LastError().message;
    EXPECT_EQ(cb.prompts, 1);
}

TEST_F(PpkTest, WrongPassphraseFromCallbackIsNotAskedAgain)
{
    REQUIRE_SERVER();
    LOAD_OR_SKIP(file, "ed25519_pass_v2.ppk");
    cb.answers = {"wrong", "wrong"};
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, Site("ed25519_pass_v2.ppk")));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::KeyFileError);
    EXPECT_EQ(cb.prompts, 1);
}

TEST_F(PpkTest, WrongStoredPassphraseDoesNotPrompt)
{
    REQUIRE_SERVER();
    LOAD_OR_SKIP(file, "ed25519_pass_v3.ppk");
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, Site("ed25519_pass_v3.ppk", "wrong")));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::KeyFileError);
    EXPECT_NE(session.LastError().message.find("passphrase"), std::string::npos);
    EXPECT_EQ(cb.prompts, 0);
}

TEST_F(PpkTest, UserCancelsPassphrasePrompt)
{
    REQUIRE_SERVER();
    LOAD_OR_SKIP(file, "ed25519_pass_v3.ppk");
    cb.refuse = true;
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, Site("ed25519_pass_v3.ppk")));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::AuthFailed);
    EXPECT_EQ(cb.prompts, 1);
}

TEST_F(PpkTest, PlainKeyNeverPrompts)
{
    REQUIRE_SERVER();
    LOAD_OR_SKIP(file, "rsa_v2.ppk");
    CSshSession session(&cb, nullptr);
    ASSERT_TRUE(Login(session, Site("rsa_v2.ppk"))) << session.LastError().message;
    EXPECT_EQ(cb.prompts, 0);
}

TEST_F(PpkTest, BadKeyFallsBackToThePassword)
{
    REQUIRE_SERVER();
    LOAD_OR_SKIP(file, "ed25519_pass_v3.ppk");
    CSshSite site = Site("ed25519_pass_v3.ppk", "wrong");
    site.password.assign(sftptest::kPassword);
    CSshSession session(&cb, nullptr);
    EXPECT_TRUE(Login(session, site)) << session.LastError().message;
}

TEST_F(PpkTest, DamagedKeyReportedWhenNothingElseWorks)
{
    REQUIRE_SERVER();
    LOAD_OR_SKIP(file, "ed25519_v3.ppk");
    std::string text = file.ToStdString();
    const size_t body = text.find('\n', text.find("Private-Lines:")) + 1;
    text[body + 3] = text[body + 3] == 'A' ? 'B' : 'A';
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "sally_damaged.ppk";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << text;
    }
    CSshSite site = Site("ed25519_v3.ppk");
    site.keyFile = tmp.string();
    CSshSession session(&cb, nullptr);
    EXPECT_FALSE(Login(session, site));
    EXPECT_EQ(session.LastError().kind, SshErrorKind::KeyFileError);
    EXPECT_NE(session.LastError().message.find("damaged"), std::string::npos) << session.LastError().message;
    std::filesystem::remove(tmp);
}

// ---- translatable error ids (SshMsg); the English message stays as it was ----

TEST_F(PpkTest, MsgIdNotAPuttyFile)
{
    CSshError err;
    CSecret pem;
    EXPECT_FALSE(ConvertPpk(CSecret(std::string("just some text\n")), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.msgId, SshMsg::PpkNotPuttyFile);
    EXPECT_FALSE(ConvertPpk(CSecret(std::string("PuTTY-User-Key-File-x: ssh-rsa\n")), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.msgId, SshMsg::PpkNotPuttyFile);
}

TEST_F(PpkTest, MsgIdDamagedForTruncatedAndBrokenFiles)
{
    CSshError err;
    CSecret pem;
    EXPECT_FALSE(ConvertPpk(CSecret(std::string("PuTTY-User-Key-File-3: ssh-ed25519\n")), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.msgId, SshMsg::PpkDamaged);
    EXPECT_TRUE(err.args.empty());
    EXPECT_NE(err.message.find("incomplete"), std::string::npos);

    // an unencrypted key whose MAC does not match
    std::string pub;
    PutString(pub, "ssh-ed25519");
    std::string text = MakePlainPpk("ssh-ed25519", pub, pub);
    const size_t macAt = text.rfind("Private-MAC: ") + 13;
    text[macAt] = text[macAt] == '0' ? '1' : '0';
    EXPECT_FALSE(ConvertPpk(CSecret(text), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.msgId, SshMsg::PpkDamaged);
}

TEST_F(PpkTest, MsgIdUnsupportedKeyTypeCarriesTheName)
{
    CSshError err;
    CSecret pem;
    std::string pub;
    PutString(pub, "ssh-dss");
    EXPECT_FALSE(ConvertPpk(CSecret(MakePlainPpk("ssh-dss", pub, pub)), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.msgId, SshMsg::PpkUnsupportedKeyType);
    ASSERT_EQ(err.args.size(), 1u);
    EXPECT_EQ(err.args[0], "ssh-dss");
}

TEST_F(PpkTest, MsgIdOldFormatWithoutMac)
{
    std::string pub;
    PutString(pub, "ssh-ed25519");
    const std::string text = Replace(MakePlainPpk("ssh-ed25519", pub, pub), "Private-MAC:", "Private-Hash:");
    CSshError err;
    CSecret pem;
    EXPECT_FALSE(ConvertPpk(CSecret(text), CSecret(), pem, nullptr, err));
    EXPECT_EQ(err.kind, SshErrorKind::Unsupported);
    EXPECT_EQ(err.msgId, SshMsg::PpkUnsupportedOldFormat);
}

TEST_F(PpkTest, MsgIdUnsupportedEncryptionAndKdfCarryTheName)
{
    LOAD_OR_SKIP(file, "ed25519_pass_v3.ppk");
    CSshError err;
    CSecret pem;
    EXPECT_FALSE(ConvertPpk(CSecret(Replace(file.ToStdString(), "aes256-cbc", "chacha")), CSecret(Passphrase()), pem,
                            nullptr, err));
    EXPECT_EQ(err.msgId, SshMsg::PpkUnsupportedEncryption);
    ASSERT_EQ(err.args.size(), 1u);
    EXPECT_EQ(err.args[0], "chacha");
    EXPECT_FALSE(ConvertPpk(CSecret(Replace(file.ToStdString(), "Key-Derivation: Argon2id", "Key-Derivation: scrypt")),
                            CSecret(Passphrase()), pem, nullptr, err));
    EXPECT_EQ(err.msgId, SshMsg::PpkUnsupportedKdf);
    ASSERT_EQ(err.args.size(), 1u);
    EXPECT_EQ(err.args[0], "scrypt");
}

TEST_F(PpkTest, MsgIdWrongPassphraseReusesTheKeyFileText)
{
    LOAD_OR_SKIP(file, "ed25519_pass_v2.ppk");
    CSshError err;
    CSecret pem;
    EXPECT_FALSE(ConvertPpk(file, CSecret("wrong"), pem, nullptr, err));
    EXPECT_EQ(err.msgId, SshMsg::KeyFileWrongPassphrase);
}
