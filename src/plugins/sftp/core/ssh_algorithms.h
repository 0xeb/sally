// The algorithm policy of the SSH transport: modern algorithms only.
#pragma once

#include <string>
#include <vector>

typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;

// No ssh-rsa (SHA-1) host keys, no 3DES/CBC ciphers, no SHA-1/MD5 MACs, no SHA-1 key exchange, no certificates.
bool IsAllowedKex(const std::string& algorithm);
bool IsAllowedHostKey(const std::string& algorithm);
bool IsAllowedCipher(const std::string& algorithm);
bool IsAllowedMac(const std::string& algorithm);

// The allowed algorithms of the list, in the original order, comma separated; empty when none is allowed.
std::string FilterAlgorithms(const std::vector<std::string>& supported, bool (*allowed)(const std::string&));

// Sets the preference of one method type (LIBSSH2_METHOD_*) to the allowed subset of what libssh2 supports.
// Returns false when nothing is left or libssh2 refuses the list: the libssh2 defaults (which include the weak
// algorithms) are then NOT left in place silently, the caller must fail the connection.
bool RestrictAlgorithms(LIBSSH2_SESSION* session, int methodType, bool (*allowed)(const std::string&));
