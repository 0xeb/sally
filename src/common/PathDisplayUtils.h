// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Wide display/validation name helpers (pure string manipulation, no I/O).
// Standalone-compilable: consumed by production (declarations also appear in
// consts.h; the compiler cross-checks in TUs that see both) and directly by
// the private tests.

#pragma once

#include <windows.h>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// Scratch storage for PathCompactPathW, which compacts IN PLACE.
//
// The API builds candidates of the form <truncated directory> + "..." +
// <file name> and writes the first one whose RENDERED width fits back over the
// caller's string. Its first candidates are longer than the input: three
// ellipsis characters can be narrower on screen than the one or two characters
// they replace, so "C:\WWW\report.txt" may legitimately come back as
// "C:\WW...\report.txt". Sizing the buffer to the input length is therefore a
// heap overflow, not a tight fit.
//
// shlwapi states the requirement as "a null-terminated string of length
// MAX_PATH". That is the scratch the API needs, not a limit on what Sally may
// display, so a path longer than MAX_PATH gets its own length plus the same
// scratch. This is the only place the constant is named for this contract; the
// three callers (editwnd.cpp, find_dialog_results.cpp, filecomp/controls.cpp)
// all go through here.
//
// Inline because filecomp is a plugin and does not link PathDisplayUtils.cpp.
inline std::vector<wchar_t> MakeCompactPathBuffer(std::wstring_view text)
{
    std::vector<wchar_t> buffer(text.size() + MAX_PATH + 1, L'\0');
    if (!text.empty())
        memcpy(buffer.data(), text.data(), text.size() * sizeof(wchar_t));
    return buffer;
}

// Returns path with trailing backslash added if needed
// (to prevent Windows from trimming trailing spaces/dots).
std::wstring MakeCopyWithBackslashIfNeededW(const wchar_t* name);

// Wide version of NameEndsWithBackslash.
BOOL NameEndsWithBackslashW(const wchar_t* name);

// Checks if path contains components ending with space or dot.
// Returns FALSE if an invalid component is found.
BOOL PathContainsValidComponents(const wchar_t* path);

// Wide version of AlterFileName - returns formatted filename.
// format: 0 none, 1 capitalize, 2 lower, 3 upper, 5 explorer style,
//         6 VC style (3 for dirs, 2 for files), 7 name mixed + ext lower.
// change: 0 both, 1 name only, 2 extension only.
std::wstring AlterFileNameW(const wchar_t* filename, int format, int change, bool isDir);
