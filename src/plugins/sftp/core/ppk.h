// PuTTY private key files (.ppk, format versions 2 and 3): decrypt, verify and convert to an unencrypted OpenSSH
// private key that libssh2 accepts from memory. Written from the public .ppk format description of PuTTY.
// Supported key types: ssh-rsa, ssh-ed25519, ecdsa-sha2-nistp256/384/521.
#pragma once

#include <string>

#include "secret.h"
#include "ssh_types.h"

// True when the file header says the private part is encrypted ("Encryption:" other than "none").
// False for plain keys and for text that is not a readable .ppk header.
bool PpkIsEncrypted(const CSecret& fileBytes);

// Converts a .ppk to the OpenSSH private key format ("-----BEGIN OPENSSH PRIVATE KEY-----", cipher none) in
// opensshKey. The MAC is checked before the decrypted data is used. publicKeyBlob (optional) receives the SSH wire
// public key blob. Errors: KeyFileError (damaged file, wrong passphrase), Unsupported (key type, format version,
// cipher or KDF). On failure opensshKey is empty. Every intermediate secret is wiped on every path.
// err.msgId is one of the Ppk* ids; its %1 (the key file) is NOT set here: err.args holds only the further arguments
// (e.g. the encryption name). The caller (AuthPuttyKey in ssh_auth.cpp) puts the key file in front.
bool ConvertPpk(const CSecret& fileBytes, const CSecret& passphrase, CSecret& opensshKey, std::string* publicKeyBlob,
                CSshError& err);

// Building blocks, exposed for tests (known-answer vectors).
// v2 cipher key: first 32 bytes of SHA1(00000000 || pass) || SHA1(00000001 || pass).
bool PpkV2CipherKey(const CSecret& passphrase, CSecret& key32);
// v2 MAC key: SHA1("putty-private-key-file-mac-key" || pass) (pass is empty for unencrypted keys).
bool PpkV2MacKey(const CSecret& passphrase, CSecret& macKey);
