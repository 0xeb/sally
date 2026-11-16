// Process-wide initialisation of the SSH libraries (Winsock, libssh2, OpenSSL) and per-thread cleanup.
#pragma once

#include "ssh_types.h"

// Reference counted and thread safe. The first call initialises Winsock 2.2, libssh2 and OpenSSL (without reading
// openssl.cnf); the last SshGlobalExit undoes it. Every successful SshGlobalInit needs one SshGlobalExit.
// On failure nothing stays initialised and err (optional) gets kind Other and the failing library's code in libssh2Code.
bool SshGlobalInit(CSshError* err = nullptr);
void SshGlobalExit();

// Put one on the stack at the top of every thread that calls libssh2/OpenSSL. A static libcrypto has no DllMain
// thread-detach hook, so without this each such thread leaks its OpenSSL per-thread state when it exits.
class CSshThreadScope
{
public:
    CSshThreadScope() = default;
    ~CSshThreadScope();
    CSshThreadScope(const CSshThreadScope&) = delete;
    CSshThreadScope& operator=(const CSshThreadScope&) = delete;
};
