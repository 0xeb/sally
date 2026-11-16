// A saved SFTP site (connection settings the user edits). Sally-free model; passwords and key passphrases are NOT
// members: they travel separately as CSecret so they live in wiped buffers only.
#pragma once

#include <string>

#include "ssh_types.h"

struct CSite
{
    enum class Encoding
    {
        Utf8,
        Auto,
        CodePage // codePage below
    };

    std::wstring name; // the name used in "@Name/path"
    std::wstring host;
    int port = 22;
    std::wstring user;
    std::wstring keyFile; // empty = none
    bool useAgent = false;
    bool compression = false;
    int timeoutSeconds = 30;
    Encoding encoding = Encoding::Auto; // per name: valid UTF-8 as UTF-8, otherwise the ANSI code page
    unsigned codePage = 1252; // used only when encoding == CodePage
    std::wstring initialRemotePath; // empty = the login directory
    std::wstring initialLocalPath;  // stored now, used for transfers later
    bool rememberPassword = false;

    // Transfer settings (used by the operations)
    int workers = 2;              // parallel transfer connections, 1..MaxSiteWorkers
    bool safeUpload = true;       // upload to "name.filepart" and rename when complete
    bool preserveTime = true;     // give uploaded/downloaded files the source modification time
    bool setUploadPermissions = false; // apply uploadPermissions to uploaded files (otherwise the server default)
    unsigned uploadPermissions = 0644; // used only when setUploadPermissions; 0..MaxUploadPermissions (octal 07777)

    CNameCodec MakeCodec() const;
};

// Reads the registry's stored Encoding value. False (and 'out' untouched) for anything that is not a known value, so
// a saved site never changes its encoding and a missing/unknown value keeps the default.
bool EncodingFromStored(unsigned long stored, CSite::Encoding& out);

// Text fields become UTF-8. The secrets are moved into the result.
CSshSite ToSshSite(const CSite& site, CSecret password, CSecret passphrase);

// True when the user has to be asked for a password before connecting: no password is known (not remembered, not
// typed) and the site has no other way to log in without asking inside the session (key file or agent).
bool SiteNeedsPasswordPrompt(const CSite& site, bool havePassword);
