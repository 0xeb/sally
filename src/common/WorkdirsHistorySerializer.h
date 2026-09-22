// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/IRegistry.h"

#include <string>
#include <vector>
#include <windows.h>

namespace sally::path::history
{
// Plain-data DirHistory entry projection.
//
// CPathHistory holds CPathHistoryItem, which carries plugin FS interface
// pointers, icons, and TIndirectArray storage — all heavyweight and
// hard to construct in tests. The serializer operates on this minimal
// projection so the registry round-trip can be exercised without any
// of that machinery.
//
// kind matches CPathHistoryItem's `Type` field (0 = disk, 1 = archive,
// 2 = plugin FS) so adapters in CPathHistory can map both directions
// trivially.
enum class EntryKind
{
    Disk = 0,
    Archive = 1,
    PluginFS = 2,
};

struct Entry
{
    EntryKind kind;
    std::wstring nameW;
    std::wstring userPartW; // empty for Disk
};

// Serialize one Entry to the wide REG_SZ payload.
//
// Disk: nameW alone.
// Archive / PluginFS: nameW + L":" + userPartW (matches the legacy ANSI
// format CPathHistory::SaveToRegistry produced, so a Win32 ANSI reader
// of the new wide value still gets a parseable — if lossy — string).
std::wstring SerializeEntry(const Entry& entry);

// Plugin-FS detector callback. The serializer is decoupled from the rest
// of the Sally object graph so headless tests can link against a tiny
// dependency surface; production wires the wide IsPluginFSPath through it.
using PluginFSPathDetector = bool (*)(const wchar_t* path,
                                     std::wstring& outFsName,
                                     std::wstring& outUserPart);

// Parse a wide REG_SZ payload back into an Entry.
//
// Detection rules (mirror the legacy ANSI parser):
//   - leading "\\\\" or position-1 ':' → disk root or archive depending
//     on whether a ':' separator is present at or after position 2.
//   - otherwise → plugin FS via the injected detector (when supplied);
//     plugin FS user-parts are still ANSI today, so the detector hands
//     back ANSI strings that ParseEntry widens.
//
// Returns false when the payload is too short or unrecognisable, or when
// the plugin FS shape is encountered with no detector; the
// CPathHistory::LoadFromRegistry caller logs and skips such entries.
bool ParseEntry(const std::wstring& payloadW, Entry& outEntry,
                PluginFSPathDetector detector = nullptr);

// Round-trip helpers used by CPathHistory::SaveToRegistry / LoadFromRegistry.
//
// WriteEntries clears the existing values and writes one REG_SZ Unicode
// per entry, indexed "1", "2", ... — same indexing scheme the legacy
// ANSI writer used.
//
// ReadEntries iterates "1", "2", ... and stops at the first missing
// index (matches the legacy reader's termination rule). Entries that
// fail ParseEntry are silently skipped — the caller has already seen
// any older invalid data and should not re-warn.
//
// Both helpers operate on `IRegistry*` so a test harness can swap in a
// mock if it wants; the production path uses the global gRegistry.
void WriteEntries(IRegistry* reg, HKEY historyKey, const std::vector<Entry>& entries);
void ReadEntries(IRegistry* reg, HKEY historyKey, std::vector<Entry>& outEntries,
                 PluginFSPathDetector detector = nullptr);
} // namespace sally::path::history
