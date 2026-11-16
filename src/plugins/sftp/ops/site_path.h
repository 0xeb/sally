// The part of a file system path after the "sftp:" prefix, as a value type with a parser and a formatter.
// Pure functions over std::wstring; no Sally, no I/O.
//
// Forms:  user@host/path   user@host:2222/path   user@[::1]:2222/path   @SiteName/path
// The user may contain '@' (the split is at the LAST '@' of the part before the first '/'). The port is 1..65535
// (default 22). The path is absolute, defaults to "/", and has trailing slashes removed (except for the root).
#pragma once

#include <string>

struct CSftpPath
{
    bool isSite = false;   // "@SiteName/...": user/host/port come from the saved site
    std::wstring siteName; // when isSite
    std::wstring user;     // otherwise
    std::wstring host;     // IPv6 without the brackets
    int port = 22;
    std::wstring path = L"/"; // always starts with '/'
};

// False for anything that is not one of the forms above (no host, bad port, ...). On failure out is unchanged.
bool ParseSftpPath(const std::wstring& afterPrefix, CSftpPath& out);

// The text after "sftp:". The port is omitted when it is 22; an IPv6 host gets its brackets back.
std::wstring FormatSftpPath(const CSftpPath& p);

// Same server, root directory.
CSftpPath RootOf(const CSftpPath& p);

// Same connection: for saved sites the names (case-insensitive), otherwise user (exact) + host (case-insensitive)
// + port. A site reference never equals an explicit user@host.
bool SameServer(const CSftpPath& a, const CSftpPath& b);

// A full file system path "fsName:userPart" (what Sally passes as a target): true when the text starts with fsName and a
// colon (the name is compared without case); userPart is the rest. A Windows path like "c:\dir" is not matched by "sftp".
bool SplitFsPath(const std::wstring& text, const std::wstring& fsName, std::wstring& userPart);

// True when 'path' is 'base' itself, or (with includingSubdirs) somewhere below it. Both are normalized absolute paths
// without a trailing slash, compared exactly (names are case sensitive on a server).
bool IsPathWithin(const std::wstring& path, const std::wstring& base, bool includingSubdirs);
