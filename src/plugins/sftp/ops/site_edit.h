// The text-level model behind the site edit and site list dialogs: what the edit controls hold (CSiteForm), its
// conversion to a validated CSite, the field/tab an error belongs to, and the strings the site list shows.
// Pure functions; no Sally, no UI.
#pragma once

#include <string>

#include "site_list.h"
#include "site_path.h"

// Everything the edit controls hold, as typed. Secrets are not part of it.
struct CSiteForm
{
    std::wstring name;
    std::wstring host;
    std::wstring port;
    std::wstring user;
    bool rememberPassword = false;
    bool useAgent = false;
    std::wstring keyFile;
    std::wstring timeout;
    bool compression = false;
    CSite::Encoding encoding = CSite::Encoding::Auto;
    std::wstring codePage; // meaningful when encoding == CodePage; otherwise kept as it was
    std::wstring initialRemotePath;
    std::wstring initialLocalPath;
    std::wstring workers;
    bool safeUpload = true;
    bool preserveTime = true;
    bool setUploadPermissions = false;
    std::wstring uploadPermissions; // octal digits, e.g. "644"; checked only when setUploadPermissions
};

// What the site edit dialog does with the remembered secrets after OK: they are dropped when "remember" is off or
// the user ticked "forget the stored password and passphrase" (typed text is then ignored). Otherwise typed secrets
// replace the stored ones and the rest is kept.
bool ShouldClearSecrets(bool rememberPassword, bool forgetSecrets);

// The control a SiteError refers to.
enum class SiteField
{
    Name,
    Host,
    Port,
    User,
    Timeout,
    CodePage,
    Workers,
    UploadPermissions
};

// The tabs of the site edit dialog.
enum SiteTab
{
    SiteTabSession = 0,
    SiteTabAuthentication = 1,
    SiteTabConnection = 2,
    SiteTabDirectories = 3,
    SiteTabCount = 4
};

CSiteForm FormFromSite(const CSite& site);

// Trims the text fields, parses the numbers (an unparsable number is the same error as an out-of-range one) and runs
// ValidateSite. On None 'out' is the site; otherwise 'out' is unchanged.
SiteError SiteFromForm(const CSiteForm& form, CSite& out);

SiteField FieldOfError(SiteError error); // SiteError::None maps to Name
SiteTab TabOfField(SiteField field);

// Host column of the site list: "host", "host:port" when the port is not 22, IPv6 literals in brackets.
std::wstring SiteHostColumn(const CSite& site);

// Quick connect line: "user@host", "user@host:port", "user@host/path", optionally with the "sftp:" prefix. False for
// everything else, including "@SiteName" (saved sites are chosen in the list).
bool ParseQuickConnect(const std::wstring& text, CSftpPath& out);

// The user part of the file system path that opens a saved site (after "sftp:"): "@Name/". The site's own initial
// directory is applied by the connect, so the path is the root.
std::wstring SiteConnectPath(const std::wstring& siteName);
