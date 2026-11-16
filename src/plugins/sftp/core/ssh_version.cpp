#include "ssh_version.h"

#include <libssh2.h>
#include <openssl/crypto.h>

std::string SshLibraryVersions()
{
    return std::string("libssh2 ") + libssh2_version(0) + ", " + OpenSSL_version(OPENSSL_VERSION);
}
