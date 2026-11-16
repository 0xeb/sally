#include "ppk.h"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr size_t kMaxLines = 10000;            // per Public-Lines / Private-Lines
constexpr uint64_t kMaxArgon2MemoryKiB = 1048576; // 1 GiB; a hostile file must not make us allocate more
constexpr uint64_t kMaxArgon2Passes = 4096;
constexpr uint64_t kMaxArgon2Lanes = 64;

bool Fail(CSshError& err, SshErrorKind kind, SshMsg msgId, std::vector<std::string> args, const std::string& message)
{
    err = CSshError();
    err.kind = kind;
    err.message = message;
    err.msgId = msgId;
    err.args = std::move(args);
    return false;
}

// Errors without a translated text (the English message is shown).
bool Fail(CSshError& err, SshErrorKind kind, const std::string& message)
{
    return Fail(err, kind, SshMsg::None, {}, message);
}

// All "invalid / truncated / incomplete" cases share one user text; the precise reason stays in the message.
bool FailDamaged(CSshError& err, const std::string& message)
{
    return Fail(err, SshErrorKind::KeyFileError, SshMsg::PpkDamaged, {}, message);
}

// ---- byte helpers -------------------------------------------------------------------------------------------

void Put(CSecret& out, const void* data, size_t length)
{
    const size_t old = out.size();
    out.resize(old + length);
    if (length > 0)
        memcpy(out.data() + old, data, length);
}

void PutU32(CSecret& out, uint32_t value)
{
    const unsigned char b[4] = {static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16),
                                static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value)};
    Put(out, b, 4);
}

void PutString(CSecret& out, const void* data, size_t length)
{
    PutU32(out, static_cast<uint32_t>(length));
    Put(out, data, length);
}

void PutString(CSecret& out, std::string_view text)
{
    PutString(out, text.data(), text.size());
}

// Reads SSH wire data (uint32, length-prefixed strings) without ever reading past the end.
class CReader
{
public:
    CReader(const void* data, size_t length) : p_(static_cast<const unsigned char*>(data)), n_(length) {}

    bool String(std::string_view& out)
    {
        if (n_ < 4)
            return false;
        const size_t length = (static_cast<size_t>(p_[0]) << 24) | (static_cast<size_t>(p_[1]) << 16) |
                              (static_cast<size_t>(p_[2]) << 8) | p_[3];
        if (length > n_ - 4)
            return false;
        out = std::string_view(reinterpret_cast<const char*>(p_ + 4), length);
        p_ += 4 + length;
        n_ -= 4 + length;
        return true;
    }

private:
    const unsigned char* p_;
    size_t n_;
};

int Base64Value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

bool Base64Decode(std::string_view text, std::string& out)
{
    out.clear();
    out.reserve(text.size() / 4 * 3 + 3); // no reallocation: a freed buffer would keep key bytes
    uint32_t bits = 0;
    int count = 0;
    size_t padding = 0;
    for (char c : text)
    {
        if (c == '=')
        {
            ++padding;
            continue;
        }
        const int v = Base64Value(c);
        if (v < 0 || padding > 0)
            return false;
        bits = (bits << 6) | static_cast<uint32_t>(v);
        if (++count == 4)
        {
            out.push_back(static_cast<char>(bits >> 16));
            out.push_back(static_cast<char>(bits >> 8));
            out.push_back(static_cast<char>(bits));
            bits = 0;
            count = 0;
        }
    }
    if (count == 1 || padding > 2)
        return false;
    if (count == 2)
        out.push_back(static_cast<char>(bits >> 4));
    else if (count == 3)
    {
        out.push_back(static_cast<char>(bits >> 10));
        out.push_back(static_cast<char>(bits >> 2));
    }
    return true;
}

// Base64 with a newline after every 70 characters (the OpenSSH key file layout), straight into a secret.
void Base64EncodeLines(const CSecret& in, CSecret& out)
{
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char* data = reinterpret_cast<const unsigned char*>(in.data());
    const size_t chars = (in.size() + 2) / 3 * 4;
    out.resize(0);
    out.resize(chars + chars / 70 + 1);
    size_t pos = 0, column = 0;
    auto emit = [&](char c)
    {
        out.data()[pos++] = c;
        if (++column == 70)
        {
            out.data()[pos++] = '\n';
            column = 0;
        }
    };
    for (size_t i = 0; i < in.size(); i += 3)
    {
        const size_t left = in.size() - i;
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (left > 1)
            v |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (left > 2)
            v |= data[i + 2];
        emit(kAlphabet[(v >> 18) & 63]);
        emit(kAlphabet[(v >> 12) & 63]);
        emit(left > 1 ? kAlphabet[(v >> 6) & 63] : '=');
        emit(left > 2 ? kAlphabet[v & 63] : '=');
    }
    if (column != 0)
        out.data()[pos++] = '\n';
    out.resize(pos);
}

bool HexDecode(std::string_view text, std::string& out)
{
    out.clear();
    if (text.size() % 2 != 0)
        return false;
    auto nibble = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < text.size(); i += 2)
    {
        const int hi = nibble(text[i]), lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out.push_back(static_cast<char>(hi * 16 + lo));
    }
    return true;
}

bool ParseUInt(std::string_view text, uint64_t maxValue, uint64_t& out)
{
    if (text.empty() || text.size() > 12)
        return false;
    uint64_t value = 0;
    for (char c : text)
    {
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    if (value > maxValue)
        return false;
    out = value;
    return true;
}

void Wipe(std::string& s)
{
    if (!s.empty())
        OPENSSL_cleanse(&s[0], s.size());
}

// ---- file parsing -------------------------------------------------------------------------------------------

struct CPpkFile
{
    int version = 0;
    std::string keyType;
    std::string encryption;
    std::string comment;
    std::string publicBlob;
    std::string privateBlob; // as stored: ciphertext (or plain text for "none"); not secret before decryption
    std::string mac;         // binary
    std::string kdf;         // v3 encrypted: "Argon2d" / "Argon2i" / "Argon2id"
    uint64_t memoryKiB = 0, passes = 0, lanes = 0;
    std::string salt;

    bool Encrypted() const { return encryption != "none"; }
    CPpkFile() = default;
    CPpkFile(const CPpkFile&) = delete;
    CPpkFile& operator=(const CPpkFile&) = delete;
    // a plain key's private part is the secret itself
    ~CPpkFile() { Wipe(privateBlob); }
};

// Splits the text into lines; "\r\n" and "\n" both end a line.
class CLines
{
public:
    explicit CLines(std::string_view text) : text_(text) {}
    bool Next(std::string_view& line)
    {
        if (pos_ >= text_.size())
            return false;
        size_t end = text_.find('\n', pos_);
        const size_t next = end == std::string_view::npos ? text_.size() : end + 1;
        if (end == std::string_view::npos)
            end = text_.size();
        line = text_.substr(pos_, end - pos_);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        pos_ = next;
        return true;
    }

private:
    std::string_view text_;
    size_t pos_ = 0;
};

bool ReadBase64Block(CLines& lines, std::string_view countText, std::string& out, CSshError& err)
{
    uint64_t count = 0;
    if (!ParseUInt(countText, kMaxLines, count) || count == 0)
        return FailDamaged(err, "The PuTTY key file has an invalid line count");
    std::vector<std::string_view> parts;
    size_t total = 0;
    for (uint64_t i = 0; i < count; ++i)
    {
        std::string_view line;
        if (!lines.Next(line))
            return FailDamaged(err, "The PuTTY key file is truncated");
        parts.push_back(line);
        total += line.size();
    }
    std::string text;
    text.reserve(total); // no reallocation: a freed buffer would keep key bytes
    for (std::string_view line : parts)
        text.append(line);
    const bool decoded = Base64Decode(text, out);
    Wipe(text);
    if (!decoded || out.empty())
        return FailDamaged(err, "The PuTTY key file contains invalid base64 data");
    return true;
}

bool ParseFile(const CSecret& bytes, CPpkFile& file, CSshError& err)
{
    CLines lines(std::string_view(bytes.data(), bytes.size()));
    std::string_view line;
    static const std::string_view kMagic = "PuTTY-User-Key-File-";
    if (!lines.Next(line) || line.substr(0, kMagic.size()) != kMagic)
        return Fail(err, SshErrorKind::KeyFileError, SshMsg::PpkNotPuttyFile, {}, "Not a PuTTY key file (unknown header)");
    const size_t colon = line.find(':');
    uint64_t version = 0;
    if (colon == std::string_view::npos || !ParseUInt(line.substr(kMagic.size(), colon - kMagic.size()), 1000000, version))
        return Fail(err, SshErrorKind::KeyFileError, SshMsg::PpkNotPuttyFile, {}, "Not a PuTTY key file (invalid header)");
    if (version != 2 && version != 3)
        return Fail(err, SshErrorKind::Unsupported,
                    "Unsupported PuTTY key file format version " + std::to_string(version) +
                        " (only versions 2 and 3 are supported)");
    file.version = static_cast<int>(version);
    std::string_view type = line.substr(colon + 1);
    while (!type.empty() && type.front() == ' ')
        type.remove_prefix(1);
    file.keyType = std::string(type);

    bool haveMac = false, havePublic = false, havePrivate = false, haveEncryption = false;
    while (!haveMac && lines.Next(line))
    {
        if (line.empty())
            continue;
        const size_t sep = line.find(": ");
        if (sep == std::string_view::npos)
            return FailDamaged(err, "The PuTTY key file has an invalid header line");
        const std::string_view name = line.substr(0, sep);
        std::string_view value = line.substr(sep + 2);
        while (!value.empty() && value.front() == ' ')
            value.remove_prefix(1);

        if (name == "Encryption")
        {
            file.encryption = std::string(value);
            haveEncryption = true;
        }
        else if (name == "Comment")
            file.comment = std::string(value);
        else if (name == "Public-Lines")
        {
            if (!ReadBase64Block(lines, value, file.publicBlob, err))
                return false;
            havePublic = true;
        }
        else if (name == "Private-Lines")
        {
            if (!ReadBase64Block(lines, value, file.privateBlob, err))
                return false;
            havePrivate = true;
        }
        else if (name == "Private-MAC")
        {
            if (!HexDecode(value, file.mac))
                return FailDamaged(err, "The PuTTY key file has an invalid MAC field");
            haveMac = true;
        }
        else if (name == "Key-Derivation")
            file.kdf = std::string(value);
        else if (name == "Argon2-Memory")
        {
            if (!ParseUInt(value, UINT32_MAX, file.memoryKiB))
                return FailDamaged(err, "The PuTTY key file has an invalid Argon2 parameter");
        }
        else if (name == "Argon2-Passes")
        {
            if (!ParseUInt(value, UINT32_MAX, file.passes))
                return FailDamaged(err, "The PuTTY key file has an invalid Argon2 parameter");
        }
        else if (name == "Argon2-Parallelism")
        {
            if (!ParseUInt(value, UINT32_MAX, file.lanes))
                return FailDamaged(err, "The PuTTY key file has an invalid Argon2 parameter");
        }
        else if (name == "Argon2-Salt")
        {
            if (!HexDecode(value, file.salt))
                return FailDamaged(err, "The PuTTY key file has an invalid Argon2 salt");
        }
        else if (name == "Private-Hash")
            return Fail(err, SshErrorKind::Unsupported, SshMsg::PpkUnsupportedOldFormat, {},
                        "Unsupported PuTTY key file (old format without a MAC)");
        // other fields are ignored
    }
    if (!haveEncryption || !havePublic || !havePrivate || !haveMac)
        return FailDamaged(err, "The PuTTY key file is incomplete or damaged");
    if (file.encryption != "none" && file.encryption != "aes256-cbc")
        return Fail(err, SshErrorKind::Unsupported, SshMsg::PpkUnsupportedEncryption, {file.encryption},
                    "Unsupported PuTTY key encryption '" + file.encryption + "'");
    if (file.mac.size() != (file.version == 2 ? 20u : 32u))
        return FailDamaged(err, "The PuTTY key file has an invalid MAC length");
    if (file.Encrypted() && file.privateBlob.size() % 16 != 0)
        return FailDamaged(err, "The PuTTY key file is damaged (bad private data length)");
    return true;
}

// ---- crypto -------------------------------------------------------------------------------------------------

bool Sha1(const CSecret& in, CSecret& out)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    const bool ok = EVP_Digest(in.data(), in.size(), digest, &length, EVP_sha1(), nullptr) == 1;
    if (ok)
        out.assign(reinterpret_cast<const char*>(digest), length);
    OPENSSL_cleanse(digest, sizeof(digest));
    return ok;
}

bool Hmac(const char* digestName, const CSecret& key, const CSecret& data, CSecret& out)
{
    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    EVP_MAC_CTX* ctx = mac != nullptr ? EVP_MAC_CTX_new(mac) : nullptr;
    unsigned char result[EVP_MAX_MD_SIZE];
    size_t length = 0;
    bool ok = false;
    if (ctx != nullptr)
    {
        OSSL_PARAM params[2] = {OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>(digestName), 0),
                                OSSL_PARAM_construct_end()};
        ok = EVP_MAC_init(ctx, reinterpret_cast<const unsigned char*>(key.data()), key.size(), params) == 1 &&
             EVP_MAC_update(ctx, reinterpret_cast<const unsigned char*>(data.data()), data.size()) == 1 &&
             EVP_MAC_final(ctx, result, &length, sizeof(result)) == 1;
    }
    if (ok)
        out.assign(reinterpret_cast<const char*>(result), length);
    OPENSSL_cleanse(result, sizeof(result));
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return ok;
}

bool AesCbcDecrypt(const CSecret& key, const CSecret& iv, const std::string& in, CSecret& out)
{
    if (key.size() != 32 || iv.size() != 16 || in.empty() || in.size() % 16 != 0)
        return false;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr)
        return false;
    out.resize(in.size());
    int written = 0, tail = 0;
    unsigned char spare[32];
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, reinterpret_cast<const unsigned char*>(key.data()),
                                 reinterpret_cast<const unsigned char*>(iv.data())) == 1 &&
              EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 &&
              EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &written,
                                reinterpret_cast<const unsigned char*>(in.data()), static_cast<int>(in.size())) == 1 &&
              EVP_DecryptFinal_ex(ctx, spare, &tail) == 1 && static_cast<size_t>(written + tail) == in.size();
    OPENSSL_cleanse(spare, sizeof(spare));
    EVP_CIPHER_CTX_free(ctx); // wipes the key schedule
    if (!ok)
        out.clear();
    return ok;
}

// 80 bytes from Argon2: 32 cipher key, 16 IV, 32 MAC key.
bool Argon2(const CPpkFile& file, const CSecret& passphrase, CSecret& okm, CSshError& err)
{
    const char* name = nullptr;
    if (file.kdf == "Argon2d")
        name = "ARGON2D";
    else if (file.kdf == "Argon2i")
        name = "ARGON2I";
    else if (file.kdf == "Argon2id")
        name = "ARGON2ID";
    else
        return Fail(err, SshErrorKind::Unsupported, SshMsg::PpkUnsupportedKdf, {file.kdf},
                    "Unsupported PuTTY key derivation '" + file.kdf + "'");
    if (file.memoryKiB == 0 || file.memoryKiB > kMaxArgon2MemoryKiB || file.passes == 0 ||
        file.passes > kMaxArgon2Passes || file.lanes == 0 || file.lanes > kMaxArgon2Lanes || file.salt.size() < 8 ||
        file.memoryKiB < 8 * file.lanes)
        return FailDamaged(err, "The PuTTY key file has invalid Argon2 parameters");

    EVP_KDF* kdf = EVP_KDF_fetch(nullptr, name, nullptr);
    EVP_KDF_CTX* ctx = kdf != nullptr ? EVP_KDF_CTX_new(kdf) : nullptr;
    bool ok = false;
    if (ctx != nullptr)
    {
        uint32_t iter = static_cast<uint32_t>(file.passes);
        uint32_t lanes = static_cast<uint32_t>(file.lanes);
        uint32_t memcost = static_cast<uint32_t>(file.memoryKiB);
        OSSL_PARAM params[] = {
            OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, const_cast<char*>(passphrase.data()),
                                              passphrase.size()),
            OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<char*>(file.salt.data()),
                                              file.salt.size()),
            OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iter),
            OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes),
            OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memcost),
            OSSL_PARAM_construct_end()};
        okm.resize(80);
        ok = EVP_KDF_derive(ctx, reinterpret_cast<unsigned char*>(okm.data()), okm.size(), params) == 1;
    }
    EVP_KDF_CTX_free(ctx);
    EVP_KDF_free(kdf);
    if (!ok)
    {
        okm.clear();
        return Fail(err, SshErrorKind::Other, "The Argon2 key derivation failed (not enough memory?)");
    }
    return true;
}

// The cipher key, IV and MAC key for this file and passphrase.
bool DeriveKeys(const CPpkFile& file, const CSecret& passphrase, CSecret& cipherKey, CSecret& iv, CSecret& macKey,
                CSshError& err)
{
    if (file.version == 2)
    {
        if (file.Encrypted())
        {
            if (!PpkV2CipherKey(passphrase, cipherKey) || !PpkV2MacKey(passphrase, macKey))
                return Fail(err, SshErrorKind::Other, "Key derivation failed");
            iv.resize(16); // zero IV
            return true;
        }
        const CSecret empty;
        return PpkV2MacKey(empty, macKey) || Fail(err, SshErrorKind::Other, "Key derivation failed");
    }
    if (!file.Encrypted())
        return true; // v3: the MAC key of an unencrypted key is empty
    CSecret okm;
    if (!Argon2(file, passphrase, okm, err))
        return false;
    cipherKey.assign(okm.data(), 32);
    iv.assign(okm.data() + 32, 16);
    macKey.assign(okm.data() + 48, 32);
    return true;
}

bool VerifyMac(const CPpkFile& file, const CSecret& macKey, const CSecret& plainPrivate)
{
    CSecret data;
    PutString(data, file.keyType);
    PutString(data, file.encryption);
    PutString(data, file.comment);
    PutString(data, file.publicBlob);
    PutString(data, plainPrivate.data(), plainPrivate.size());
    CSecret mac;
    if (!Hmac(file.version == 2 ? "SHA1" : "SHA256", macKey, data, mac) || mac.size() != file.mac.size())
        return false;
    return CRYPTO_memcmp(mac.data(), file.mac.data(), mac.size()) == 0;
}

// ---- conversion to the OpenSSH private key format -----------------------------------------------------------

bool Damaged(CSshError& err)
{
    return FailDamaged(err, "The PuTTY key file is damaged (unexpected key data)");
}

// Appends the key-specific fields of the OpenSSH private section.
bool PutKeyFields(const CPpkFile& file, const CSecret& priv, CSecret& section, CSshError& err)
{
    CReader pub(file.publicBlob.data(), file.publicBlob.size());
    CReader sec(priv.data(), priv.size());
    std::string_view type;
    if (!pub.String(type) || type != file.keyType)
        return Damaged(err);

    if (file.keyType == "ssh-rsa")
    {
        std::string_view e, n, d, p, q, iqmp;
        if (!pub.String(e) || !pub.String(n) || !sec.String(d) || !sec.String(p) || !sec.String(q) ||
            !sec.String(iqmp))
            return Damaged(err);
        for (std::string_view part : {n, e, d, iqmp, p, q})
            PutString(section, part);
        return true;
    }
    if (file.keyType == "ssh-ed25519")
    {
        std::string_view key, seed;
        if (!pub.String(key) || !sec.String(seed) || key.size() != 32 || seed.size() != 32)
            return Damaged(err);
        CSecret pair; // seed || public key, the 64-byte OpenSSH secret
        Put(pair, seed.data(), seed.size());
        Put(pair, key.data(), key.size());
        PutString(section, key);
        PutString(section, pair.data(), pair.size());
        return true;
    }
    // ecdsa-sha2-nistpNNN
    std::string_view curve, point, d;
    if (!pub.String(curve) || !pub.String(point) || !sec.String(d) || file.keyType != "ecdsa-sha2-" + std::string(curve))
        return Damaged(err);
    PutString(section, curve);
    PutString(section, point);
    PutString(section, d);
    return true;
}

bool IsSupportedType(const std::string& t)
{
    return t == "ssh-rsa" || t == "ssh-ed25519" || t == "ecdsa-sha2-nistp256" || t == "ecdsa-sha2-nistp384" ||
           t == "ecdsa-sha2-nistp521";
}

bool BuildOpenSsh(const CPpkFile& file, const CSecret& priv, CSecret& pem, CSshError& err)
{
    CSecret section;
    unsigned char check[4];
    if (RAND_bytes(check, sizeof(check)) != 1)
        return Fail(err, SshErrorKind::Other, "No random numbers available");
    Put(section, check, 4);
    Put(section, check, 4);
    PutString(section, file.keyType);
    if (!PutKeyFields(file, priv, section, err))
        return false;
    PutString(section, file.comment);
    for (unsigned char pad = 1; section.size() % 8 != 0; ++pad)
        Put(section, &pad, 1);

    CSecret binary;
    static const char kMagic[] = "openssh-key-v1"; // followed by its terminating NUL
    Put(binary, kMagic, sizeof(kMagic));
    PutString(binary, "none"); // cipher
    PutString(binary, "none"); // kdf
    PutString(binary, "");     // kdf options
    PutU32(binary, 1);         // number of keys
    PutString(binary, file.publicBlob.data(), file.publicBlob.size());
    PutString(binary, section.data(), section.size());

    CSecret body;
    Base64EncodeLines(binary, body);
    pem.clear();
    static const char kBegin[] = "-----BEGIN OPENSSH PRIVATE KEY-----\n";
    static const char kEnd[] = "-----END OPENSSH PRIVATE KEY-----\n";
    Put(pem, kBegin, sizeof(kBegin) - 1);
    Put(pem, body.data(), body.size());
    Put(pem, kEnd, sizeof(kEnd) - 1);
    return true;
}
} // namespace

bool PpkV2CipherKey(const CSecret& passphrase, CSecret& key32)
{
    CSecret all;
    for (unsigned char counter = 0; counter < 2; ++counter)
    {
        CSecret in, hash;
        const unsigned char prefix[4] = {0, 0, 0, counter};
        Put(in, prefix, 4);
        Put(in, passphrase.data(), passphrase.size());
        if (!Sha1(in, hash))
            return false;
        Put(all, hash.data(), hash.size());
    }
    key32.assign(all.data(), 32);
    return true;
}

bool PpkV2MacKey(const CSecret& passphrase, CSecret& macKey)
{
    static const char kPrefix[] = "putty-private-key-file-mac-key";
    CSecret in;
    Put(in, kPrefix, sizeof(kPrefix) - 1);
    Put(in, passphrase.data(), passphrase.size());
    return Sha1(in, macKey);
}

bool PpkIsEncrypted(const CSecret& fileBytes)
{
    CPpkFile file;
    CSshError ignored;
    return ParseFile(fileBytes, file, ignored) && file.Encrypted();
}

// The Ppk* ids set in err.msgId have the key file as %1, but err.args does not contain it (this function does not know
// the file's path): the caller prepends it.
bool ConvertPpk(const CSecret& fileBytes, const CSecret& passphrase, CSecret& opensshKey, std::string* publicKeyBlob,
                CSshError& err)
{
    opensshKey.clear();
    err = CSshError();
    CPpkFile file;
    if (!ParseFile(fileBytes, file, err))
        return false;
    if (!IsSupportedType(file.keyType))
        return Fail(err, SshErrorKind::Unsupported, SshMsg::PpkUnsupportedKeyType, {file.keyType},
                    "Unsupported PuTTY key type '" + file.keyType + "'");

    CSecret cipherKey, iv, macKey, plain;
    if (!DeriveKeys(file, passphrase, cipherKey, iv, macKey, err))
        return false;
    if (file.Encrypted())
    {
        if (!AesCbcDecrypt(cipherKey, iv, file.privateBlob, plain))
            return Fail(err, SshErrorKind::KeyFileError, SshMsg::PpkDecryptFailed, {}, "The PuTTY key file could not be decrypted");
    }
    else
    {
        plain.assign(file.privateBlob.data(), file.privateBlob.size());
    }
    // the MAC is checked before the decrypted data is looked at
    if (!VerifyMac(file, macKey, plain))
    {
        if (file.Encrypted())
            return Fail(err, SshErrorKind::KeyFileError, SshMsg::KeyFileWrongPassphrase, {},
                        "Wrong passphrase or damaged key file");
        return FailDamaged(err, "The key file is damaged");
    }

    CSecret pem;
    if (!BuildOpenSsh(file, plain, pem, err))
        return false;
    opensshKey = std::move(pem);
    if (publicKeyBlob != nullptr)
        *publicKeyBlob = file.publicBlob;
    return true;
}
