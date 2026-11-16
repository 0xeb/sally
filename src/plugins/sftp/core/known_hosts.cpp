#include "known_hosts.h"

#include <winsock2.h>
#include <windows.h>

#include <libssh2.h>
#include <openssl/evp.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
std::wstring ToWide(const std::string& utf8)
{
    return CNameCodec::Utf8().Decode(utf8);
}

// Owns the libssh2 collection.
class CHostList
{
public:
    explicit CHostList(LIBSSH2_SESSION* session) : hosts_(libssh2_knownhost_init(session)) {}
    ~CHostList()
    {
        if (hosts_ != nullptr)
            libssh2_knownhost_free(hosts_);
    }
    CHostList(const CHostList&) = delete;
    CHostList& operator=(const CHostList&) = delete;

    LIBSSH2_KNOWNHOSTS* get() const { return hosts_; }

    // Adds every entry of the file. A missing or unreadable file is an empty list; broken lines are skipped.
    void Load(const std::string& path)
    {
        if (hosts_ == nullptr || path.empty())
            return;
        std::ifstream in(std::filesystem::path(ToWide(path)), std::ios::binary);
        if (!in)
            return;
        std::string line;
        while (std::getline(in, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#')
                continue;
            libssh2_knownhost_readline(hosts_, line.c_str(), line.size(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        }
    }

private:
    LIBSSH2_KNOWNHOSTS* hosts_;
};

int KeyBits(int hostKeyType)
{
    switch (hostKeyType)
    {
    case LIBSSH2_HOSTKEY_TYPE_RSA:
        return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    case LIBSSH2_HOSTKEY_TYPE_DSS:
        return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
        return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
        return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
        return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
    case LIBSSH2_HOSTKEY_TYPE_ED25519:
        return LIBSSH2_KNOWNHOST_KEY_ED25519;
    default:
        return LIBSSH2_KNOWNHOST_KEY_UNKNOWN;
    }
}

int TypeMask(int hostKeyType)
{
    return LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | KeyBits(hostKeyType);
}

HostKeyCheck FromRc(int rc)
{
    if (rc == LIBSSH2_KNOWNHOST_CHECK_MATCH)
        return HostKeyCheck::Match;
    if (rc == LIBSSH2_KNOWNHOST_CHECK_MISMATCH)
        return HostKeyCheck::Mismatch;
    if (rc == LIBSSH2_KNOWNHOST_CHECK_FAILURE)
        return HostKeyCheck::Failure;
    return HostKeyCheck::NotFound;
}

// The entry name is built here ("[host]:port" for port != 22) and passed with port -1: libssh2_knownhost_checkp
// with a port >= 0 looks for "[host]:port" and then falls back to the bare host, which would let an entry of another
// service on the same host vouch for this port.
HostKeyCheck CheckOne(LIBSSH2_KNOWNHOSTS* hosts, const std::string& host, int port, const char* key, size_t len,
                      int hostKeyType)
{
    const std::string name = KnownHostName(host, port);
    HostKeyCheck result =
        FromRc(libssh2_knownhost_checkp(hosts, name.c_str(), -1, key, len, TypeMask(hostKeyType), nullptr));
    if (result != HostKeyCheck::NotFound)
        return result;
    // With a key type in the mask libssh2 skips entries of other types: look again for any type
    const int anyType = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW;
    return FromRc(libssh2_knownhost_checkp(hosts, name.c_str(), -1, key, len, anyType, nullptr));
}

void SetIoError(CSshError* err, const std::string& message, int libssh2Code = 0, SshMsg msgId = SshMsg::None,
                const std::vector<std::string>& args = std::vector<std::string>())
{
    if (err == nullptr)
        return;
    err->kind = SshErrorKind::Io;
    err->libssh2Code = libssh2Code;
    err->sftpStatus = 0;
    err->message = message;
    err->msgId = msgId;
    err->args = args;
}

// Serialises the collection into the text of one OpenSSH file.
bool Serialize(LIBSSH2_KNOWNHOSTS* hosts, std::string& text)
{
    libssh2_knownhost* node = nullptr;
    libssh2_knownhost* prev = nullptr;
    while (libssh2_knownhost_get(hosts, &node, prev) == 0)
    {
        std::vector<char> buf(4096);
        size_t outLen = 0;
        int rc = libssh2_knownhost_writeline(hosts, node, buf.data(), buf.size(), &outLen,
                                             LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        if (rc == LIBSSH2_ERROR_BUFFER_TOO_SMALL)
        {
            buf.resize(outLen + 1);
            rc = libssh2_knownhost_writeline(hosts, node, buf.data(), buf.size(), &outLen,
                                             LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        }
        if (rc != 0)
            return false;
        text.append(buf.data(), outLen);
        prev = node;
    }
    return true;
}

// Write to a temporary file next to the target, then replace the target: a crash never leaves half a file.
bool WriteFileAtomic(const std::string& path, const std::string& text, CSshError& error)
{
    std::filesystem::path target(ToWide(path));
    std::error_code ec;
    if (target.has_parent_path())
        std::filesystem::create_directories(target.parent_path(), ec);

    std::filesystem::path temp = target;
    temp += L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out)
        {
            SetIoError(&error, "Cannot write the known_hosts file: " + path, 0, SshMsg::KnownHostsWriteFailed, {path});
            return false;
        }
    }
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        SetIoError(&error, "Cannot replace the known_hosts file: " + path, 0, SshMsg::KnownHostsReplaceFailed, {path});
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}
} // namespace

const char* HostKeyTypeName(int hostKeyType)
{
    switch (hostKeyType)
    {
    case LIBSSH2_HOSTKEY_TYPE_RSA:
        return "ssh-rsa";
    case LIBSSH2_HOSTKEY_TYPE_DSS:
        return "ssh-dss";
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
        return "ecdsa-sha2-nistp256";
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
        return "ecdsa-sha2-nistp384";
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
        return "ecdsa-sha2-nistp521";
    case LIBSSH2_HOSTKEY_TYPE_ED25519:
        return "ssh-ed25519";
    default:
        return "unknown";
    }
}

std::string Sha256Fingerprint(const char* key, size_t len)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;
    if (EVP_Digest(key, len, digest, &digestLen, EVP_sha256(), nullptr) != 1)
        return std::string();

    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out = "SHA256:";
    unsigned int i = 0;
    for (; i + 2 < digestLen; i += 3)
    {
        unsigned int v = (digest[i] << 16) | (digest[i + 1] << 8) | digest[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    // The padding '=' is left out (OpenSSH style). 32 bytes leave 2 bytes: three characters.
    if (digestLen - i == 2)
    {
        unsigned int v = (digest[i] << 16) | (digest[i + 1] << 8);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
    }
    else if (digestLen - i == 1)
    {
        unsigned int v = digest[i] << 16;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
    }
    return out;
}

std::string LowerHost(std::string host)
{
    for (char& c : host)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return host;
}

std::string KnownHostName(const std::string& host, int port)
{
    const std::string lower = LowerHost(host);
    if (port == 22)
        return lower;
    return "[" + lower + "]:" + std::to_string(port);
}

CKnownHosts::CKnownHosts(std::string ownPath, std::string userPath)
    : ownPath_(std::move(ownPath)), userPath_(std::move(userPath))
{
}

HostKeyCheck CKnownHosts::Check(LIBSSH2_SESSION* session, const std::string& host, int port, const char* key,
                                size_t len, int hostKeyType) const
{
    HostKeyCheck result = HostKeyCheck::NotFound;
    bool failure = false;
    const std::string* paths[2] = {&ownPath_, &userPath_};
    for (const std::string* path : paths)
    {
        if (path->empty())
            continue;
        CHostList list(session);
        if (list.get() == nullptr)
        {
            failure = true;
            continue;
        }
        list.Load(*path);
        HostKeyCheck one = CheckOne(list.get(), host, port, key, len, hostKeyType);
        if (one == HostKeyCheck::Match)
            return HostKeyCheck::Match; // a match in either file wins
        if (one == HostKeyCheck::Failure)
            failure = true;
        else if (one == HostKeyCheck::Mismatch)
            result = HostKeyCheck::Mismatch;
    }
    return failure ? HostKeyCheck::Failure : result; // a match in one file was returned above
}

bool CKnownHosts::AddOrReplace(LIBSSH2_SESSION* session, const std::string& host, int port, const char* key,
                               size_t len, int hostKeyType, CSshError* err) const
{
    if (ownPath_.empty())
    {
        SetIoError(err, "No known_hosts file is configured", 0, SshMsg::NoKnownHostsFile);
        return false;
    }
    CHostList list(session);
    if (list.get() == nullptr)
    {
        SetIoError(err, "Cannot create the known_hosts list");
        return false;
    }
    list.Load(ownPath_);

    const std::string name = KnownHostName(host, port);

    // Collect first, delete afterwards: deleting invalidates the iteration.
    std::vector<libssh2_knownhost*> old;
    libssh2_knownhost* node = nullptr;
    libssh2_knownhost* prev = nullptr;
    while (libssh2_knownhost_get(list.get(), &node, prev) == 0)
    {
        if (node->name != nullptr && name == node->name)
            old.push_back(node);
        prev = node;
    }
    for (libssh2_knownhost* o : old)
        libssh2_knownhost_del(list.get(), o);

    int rc = libssh2_knownhost_addc(list.get(), name.c_str(), nullptr, key, len, nullptr, 0, TypeMask(hostKeyType),
                                    nullptr);
    if (rc != 0)
    {
        SetIoError(err, "Cannot add the host key to the known_hosts list", rc);
        return false;
    }

    std::string text;
    if (!Serialize(list.get(), text))
    {
        SetIoError(err, "Cannot serialise the known_hosts list");
        return false;
    }
    CSshError writeError;
    if (!WriteFileAtomic(ownPath_, text, writeError))
    {
        if (err != nullptr)
            *err = writeError;
        return false;
    }
    return true;
}
