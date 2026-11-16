// Host key database: Sally's own known_hosts file (read and written) and, optionally, the user's OpenSSH
// ~/.ssh/known_hosts (read only, never written). OpenSSH file format, parsed by libssh2's knownhost API.
#pragma once

#include <winsock2.h> // must precede windows.h (pulled in by ssh_types.h)

#include <string>

#include "ssh_types.h"

typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;

enum class HostKeyCheck
{
    Match,    // the host is known with exactly this key
    NotFound, // no entry for this host at all
    Mismatch, // the host is known, but with a different key (also: known only under another key type)
    Failure   // the known_hosts data could not be checked (libssh2 error): never treat as "unknown host"
};

// "SHA256:<base64 without padding>", the form OpenSSH prints. key/len is the raw host key blob.
std::string Sha256Fingerprint(const char* key, size_t len);

// Host names are case insensitive: ASCII lower case, the form every known_hosts lookup and entry uses.
std::string LowerHost(std::string host);

// Entry name: "host" for port 22, "[host]:port" otherwise (OpenSSH convention). The host is lower-cased.
std::string KnownHostName(const std::string& host, int port);

// OpenSSH name of a LIBSSH2_HOSTKEY_TYPE_* ("ssh-ed25519", "ecdsa-sha2-nistp256", "ssh-rsa", ...).
const char* HostKeyTypeName(int hostKeyType);

// Holds only the file paths; the files are read on every call (they are small, and another Sally instance or
// ssh may have changed them). libssh2's knownhost API needs a session, so each call takes the caller's session.
// Not thread safe by itself: use it from the session's owner thread (two instances writing the same file at the
// same moment can lose one entry; the file itself is replaced atomically, never half written).
class CKnownHosts
{
public:
    // ownPath: UTF-8 path of Sally's file (may not exist yet). userPath: UTF-8 path of the user's file, empty = ignore.
    explicit CKnownHosts(std::string ownPath, std::string userPath = std::string());

    // hostKeyType: LIBSSH2_HOSTKEY_TYPE_* as returned by libssh2_session_hostkey. For a port other than 22 only the
    // "[host]:port" entries count, never the bare host (OpenSSH does the same). A host that is known only under a
    // different key type than the presented one gives Mismatch (its key changed from the user's point of view).
    // Limits of libssh2's parser: "@revoked" and "@cert-authority" lines are not understood and are ignored, i.e.
    // a revoked key is NOT refused here, and AddOrReplace drops such lines from Sally's own file when it rewrites it.
    HostKeyCheck Check(LIBSSH2_SESSION* session, const std::string& host, int port, const char* key, size_t len,
                       int hostKeyType) const;

    // Stores the key in the own file, replacing ALL existing entries of the same host name (every key type: the
    // user has just accepted a changed key). Returns false
    // (err filled, kind Io) when the file cannot be written.
    bool AddOrReplace(LIBSSH2_SESSION* session, const std::string& host, int port, const char* key, size_t len,
                      int hostKeyType, CSshError* err) const;

private:
    std::string ownPath_;
    std::string userPath_;
};
