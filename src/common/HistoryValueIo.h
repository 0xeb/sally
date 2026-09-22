// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// Reading and writing ONE history entry, in a shape any tool can read
//.
//
// THE DEFECT THIS MIGRATES AWAY FROM. Sally's history entries are UTF-16, but
// `SaveHistoryW` stored them by handing their raw bytes to the ANSI value API
// under REG_SZ. `RegSetValueExA` transcodes its buffer CP_ACP -> UTF-16 before
// storing, so what lands on disk is the wide bytes REINTERPRETED as ANSI
// characters. Read back through `RegQueryValueExA` the transcode reverses and
// the string survives — which is why nobody noticed. Live example from a real
// profile, the stored UTF-16 for "C:\Profiles\Example\...":
//
//     43 00 00 00  3A 00 00 00  5C 00 00 00  55 00 00 00
//      C   \0        :   \0       \   \0       U   \0
//
// Every other WCHAR is a NUL. The round trip is byte-symmetric only on a
// single-byte code page. On a DBCS code page a byte pair can transcode as one
// double-byte character, and on a UTF-8 active code page a lone high byte is an
// invalid sequence that becomes U+FFFD — in both cases the entry is corrupted on
// the way to disk. Sally ships to exactly those locales.
//
// THE MIGRATION. Existing values ARE the user's data, so the reader retains a
// named legacy import. New values are written exactly once under an honest
// UTF-16 name: "1" is legacy and read-only, while "1W" is native UTF-16. The
// name carries the shape.
//
// That choice is what keeps this readable: there is NO heuristic anywhere that
// sniffs a buffer to guess which shape it is in. A sniffer would have to
// distinguish "honest text that happens to contain a NUL-looking pattern" from
// legacy bytes, and would be wrong eventually. Preferring "1W" and falling back
// to "1" is total and unambiguous.
//
namespace sally::registry
{

// Writes one honest UTF-16 entry under `<index>W`. `<index>` remains a read-only
// legacy import. `index` is 1-based, matching the existing on-disk convention.
bool WriteHistoryEntry(HKEY key, int index, const wchar_t* text);

// Reads one entry, preferring the honest value and falling back to the legacy
// one. Returns an empty string when neither is present — a caller that must
// distinguish "absent" from "empty" should use HistoryEntryExists.
std::wstring ReadHistoryEntry(HKEY key, int index);

// TRUE when either shape is present for this index.
bool HistoryEntryExists(HKEY key, int index);

// The two value names for an index, exposed so tests can assert the on-disk
// layout without restating the convention.
std::wstring HonestValueName(int index);
std::string LegacyValueName(int index);

} // namespace sally::registry
