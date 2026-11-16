// The sftp:// URL of a remote item, for "Copy URL" in the context menu. Pure string work; no Sally, no I/O.
#pragma once

#include <string>

// "sftp://user@host/path", ":port" only when it is not 22, an IPv6 host in brackets, no user part when 'user' is empty.
// 'path' is the absolute remote path as text. Everything that is not a letter, digit, '-', '.', '_' or '~' is
// percent-encoded as UTF-8 (the '/' between path parts stays), so "@" or a space in a name or user cannot break the URL.
std::wstring BuildSftpUrl(const std::wstring& user, const std::wstring& host, int port, const std::wstring& path);
