// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// Tar member names are byte fields. Modern ustar/GNU producers write UTF-8; old archives often
// contain bytes in the user's legacy Windows code page. Decode strict UTF-8 first for tar and
// use ACP only as an explicit compatibility fallback. CPIO keeps its historical ACP behavior.
bool DecodeArchiveMemberName(const char* bytes, bool preferUtf8, std::wstring& decoded);

// Same two strict attempts, then a last resort that cannot fail: each remaining byte becomes
// the code point of the same value (ISO-8859-1), which is total, lossless and reversible.
//
// This exists because "neither strict UTF-8 nor strict ACP" is a real archive, not a corrupt
// one - a tarball written on a KOI8-R or ISO-8859-2 system, read on a machine whose ACP is
// neither. The listing code used to drop such a member silently: no entry in the panel, no
// message, no way to extract it. Pre-unicode never decoded at all and simply showed the bytes,
// so the member was always there. Refusing to guess an encoding is right; refusing to show the
// file is not. Use this wherever the alternative is dropping the member; use the strict form
// where a wrong identity would be worse than no identity.
bool DecodeArchiveMemberNameLenient(const char* bytes, bool preferUtf8, std::wstring& decoded);

// Returns the last path= value from one POSIX PAX extended-header payload.
bool ParsePaxPath(const char* payload, size_t size, std::string& path);
