// Local (Windows) file names and paths for downloads. Pure functions: no threads, no network, no Sally.
#pragma once

#include <string>

#include "encoding.h"

// True when `name` (one path component, no directory part) can be created on Windows: not empty, not "." or "..",
// at most 255 characters, none of <>:"/\|?* or a control character (< 0x20), no trailing dot or space, and not a
// reserved device name (CON PRN AUX NUL COM1-9 LPT1-9, case-insensitive, also with an extension like "con.txt").
bool IsValidLocalName(const std::wstring& name);

// Joins `dir` and `name` with a backslash (no doubled backslash when `dir` already ends with one). When the result
// is 248 characters or longer (the limit of CreateDirectory), the extended-length prefix is added: `\\?\C:\...` for a drive path,
// `\\?\UNC\server\share\...` for a UNC path. Input that already starts with `\\?\` or `\\.\` is not prefixed again.
// (In .NET terms: the prefix is what lets Win32 file APIs accept paths longer than 260 characters.)
std::wstring MakeLocalPath(const std::wstring& dir, const std::wstring& name);

// The same prefix rule for a complete path (a path the user picked, which MakeLocalPath never saw).
std::wstring ExtendedLocalPath(const std::wstring& path);

// Decodes a raw remote name (bytes) to a local file name. `lossy` (optional) is set to true when the decoding lost
// data (the result contains U+FFFD that the raw bytes did not contain), so the caller can park the item with
// Problem::InvalidLocalName instead of silently writing a mangled name. The raw bytes stay with the caller.
std::wstring DecodeRemoteNameToLocal(const CNameCodec& codec, const std::string& rawName, bool* lossy);
