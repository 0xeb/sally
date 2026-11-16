#include "ssh_algorithms.h"

#include <libssh2.h>

#include <cstring>

namespace
{
bool StartsWith(const std::string& s, const char* prefix)
{
    return s.compare(0, strlen(prefix), prefix) == 0;
}
} // namespace

bool IsAllowedKex(const std::string& a)
{
    return StartsWith(a, "curve25519-sha256") || StartsWith(a, "sntrup761x25519-sha512") ||
           StartsWith(a, "ecdh-sha2-nistp") || a == "diffie-hellman-group-exchange-sha256" ||
           a == "diffie-hellman-group14-sha256" || a == "diffie-hellman-group16-sha512" ||
           a == "diffie-hellman-group18-sha512";
}

bool IsAllowedHostKey(const std::string& a)
{
    if (a.find("cert") != std::string::npos)
        return false;
    return a == "ssh-ed25519" || StartsWith(a, "ecdsa-sha2-") || a == "rsa-sha2-512" || a == "rsa-sha2-256";
}

bool IsAllowedCipher(const std::string& a)
{
    return a == "chacha20-poly1305@openssh.com" || a == "aes256-gcm@openssh.com" || a == "aes128-gcm@openssh.com" ||
           (StartsWith(a, "aes") && a.find("-ctr") != std::string::npos);
}

bool IsAllowedMac(const std::string& a)
{
    return StartsWith(a, "hmac-sha2-");
}

std::string FilterAlgorithms(const std::vector<std::string>& supported, bool (*allowed)(const std::string&))
{
    std::string list;
    for (const std::string& a : supported)
    {
        if (!allowed(a))
            continue;
        if (!list.empty())
            list += ',';
        list += a;
    }
    return list;
}

bool RestrictAlgorithms(LIBSSH2_SESSION* session, int methodType, bool (*allowed)(const std::string&))
{
    const char** algs = nullptr;
    const int count = libssh2_session_supported_algs(session, methodType, &algs);
    if (count <= 0 || algs == nullptr)
        return false;
    std::vector<std::string> supported;
    for (int i = 0; i < count; ++i)
        if (algs[i] != nullptr)
            supported.push_back(algs[i]);
    libssh2_free(session, algs);

    const std::string list = FilterAlgorithms(supported, allowed);
    return !list.empty() && libssh2_session_method_pref(session, methodType, list.c_str()) == 0;
}
