#include <winsock2.h> // before windows.h (pulled in by ssh_init.h)

#include "ssh_init.h"

#include <libssh2.h>
#include <openssl/crypto.h>

#include <mutex>

namespace
{
std::mutex g_initMutex;
int g_initCount = 0; // guarded by g_initMutex

void SetError(CSshError* err, int code, const char* what)
{
    if (err == nullptr)
        return;
    err->kind = SshErrorKind::Other;
    err->libssh2Code = code;
    err->sftpStatus = 0;
    err->message = what;
}
} // namespace

bool SshGlobalInit(CSshError* err)
{
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initCount > 0)
    {
        ++g_initCount;
        return true;
    }

    WSADATA wsa;
    const int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (wsaResult != 0)
    {
        SetError(err, wsaResult, "WSAStartup failed");
        return false;
    }
    if (OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr) != 1)
    {
        WSACleanup();
        SetError(err, 0, "OPENSSL_init_crypto failed");
        return false;
    }
    const int sshResult = libssh2_init(0);
    if (sshResult != 0)
    {
        WSACleanup();
        SetError(err, sshResult, "libssh2_init failed");
        return false;
    }
    g_initCount = 1;
    return true;
}

void SshGlobalExit()
{
    std::lock_guard<std::mutex> lock(g_initMutex);
    if (g_initCount <= 0)
        return; // unbalanced call; ignore
    if (--g_initCount == 0)
    {
        libssh2_exit();
        WSACleanup();
    }
}

CSshThreadScope::~CSshThreadScope()
{
    OPENSSL_thread_stop();
}
