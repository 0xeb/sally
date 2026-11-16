// Pure helpers that turn directory entries and remote paths into what the panel shows. Everything here is free of
// Sally and UI code so it can be unit tested; fs/ only calls these functions.
//
// Remote paths are raw bytes (std::string): '/' separated, absolute, never decoded here.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

#include "ssh_types.h"

// ---- Entries

// "drwxr-xr-x" (always 10 characters). The file type comes from the S_IFMT bits of 'mode'; when they are zero the
// isDir/isLink flags decide. setuid/setgid/sticky show as s/S/t/T like ls does.
std::wstring FormatPermissions(uint32_t mode, bool isDir, bool isLink);

// Unix seconds -> FILETIME (UTC). Values before 1601 clamp to 0.
FILETIME UnixTimeToFileTime(int64_t unixSeconds);

// FILETIME (UTC) -> Unix seconds (rounded down). Values before 1970 are negative.
int64_t FileTimeToUnixTime(const FILETIME& fileTime);

// Index of the first character of the extension (after the last '.'), or name.size() when there is none.
// A leading dot counts ("." + "bashrc" -> extension "bashrc"), like Sally does on Windows.
size_t ExtensionOffset(const std::wstring& name);

// Names that start with '.' are hidden on Unix.
bool IsHiddenName(const std::string& rawName);

// A directory, or a symbolic link whose target is a directory (the panel treats both as a folder).
bool EntryIsDirectory(const CSftpEntry& entry);

// FILE_ATTRIBUTE_DIRECTORY for folders, FILE_ATTRIBUTE_REPARSE_POINT for links, FILE_ATTRIBUTE_HIDDEN for dot names,
// FILE_ATTRIBUTE_NORMAL when nothing else applies (never 0).
DWORD EntryAttributes(const CSftpEntry& entry);

// Total order on raw names, never 0 for different names (used by the plugin data interface).
int CompareRawNames(const std::string& a, const std::string& b);

// ---- Remote paths

// Absolute path with "//", "." and ".." resolved ("/a/../b/" -> "/b"); ".." never goes above the root. A relative
// input is taken as relative to the root. "" -> "/".
std::string NormalizeRemotePath(const std::string& path);

bool IsRootRemotePath(const std::string& path);

// The parent directory; the root's parent is the root.
std::string ParentRemotePath(const std::string& path);

// The last component ("" for the root).
std::string LastRemoteComponent(const std::string& path);

// base + "/" + name, normalized.
std::string JoinRemotePath(const std::string& base, const std::string& name);

// 'input' as typed by the user: absolute -> normalized as it is, relative -> joined to 'base'. "" -> base.
std::string ResolveRemotePath(const std::string& base, const std::string& input);

// ---- Window title

// Text for Sally's main window title. prefix is "sftp:user@host" (no path), path an absolute display path.
// mode 1: the last directory name (the full text for the root); mode 2: prefix + "/.../" + last name (the full text
// when the path has only one component); any other mode: the full text.
std::wstring FormatWindowTitlePath(const std::wstring& prefix, const std::wstring& path, int mode);

// ---- Errors

// True for errors that are about one path (it does not exist, is not allowed, is not a directory): the panel may
// then shorten the path. False for everything about the connection or the user cancelling.
bool IsPathLevelError(SshErrorKind kind);
