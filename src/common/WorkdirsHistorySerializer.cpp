// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WorkdirsHistorySerializer.h"

#include <cstdio>
#include <utility>

namespace sally::path::history
{
namespace
{
// Format a 1-based index as a wide REG_SZ value name. Matches the legacy
// ANSI itoa(... + 1, ...) used by CPathHistory::SaveToRegistry.
std::wstring IndexValueNameW(int oneBasedIndex)
{
    wchar_t buf[16];
    swprintf_s(buf, 16, L"%d", oneBasedIndex);
    return std::wstring(buf);
}
} // namespace

std::wstring SerializeEntry(const Entry& entry)
{
    std::wstring payload = entry.nameW;
    if (entry.kind == EntryKind::Archive || entry.kind == EntryKind::PluginFS)
    {
        payload.push_back(L':');
        payload += entry.userPartW;
    }
    return payload;
}

bool ParseEntry(const std::wstring& payloadW, Entry& outEntry, PluginFSPathDetector detector)
{
    if (payloadW.size() < 2)
        return false;

    // Disk or archive shape: leading "\\\\" (UNC) or position-1 ':' (drive).
    if ((payloadW[0] == L'\\' && payloadW[1] == L'\\') || payloadW[1] == L':')
    {
        // Look for the *next* ':' after position 2; that's the archive
        // separator. Drive-letter colons are at position 1, so starting at
        // 2 skips them.
        size_t sep = payloadW.find(L':', 2);
        if (sep == std::wstring::npos)
        {
            outEntry.kind = EntryKind::Disk;
            outEntry.nameW = payloadW;
            outEntry.userPartW.clear();
        }
        else
        {
            outEntry.kind = EntryKind::Archive;
            outEntry.nameW = payloadW.substr(0, sep);
            outEntry.userPartW = payloadW.substr(sep + 1);
        }
        return true;
    }

    // Plugin FS shape: dispatch through the injected detector. Tests that
    // don't care about plugin FS pass nullptr; production wires IsPluginFSPath.
    if (detector == nullptr)
        return false;

    std::wstring fsName;
    std::wstring userPart;
    if (!detector(payloadW.c_str(), fsName, userPart))
        return false;

    outEntry.kind = EntryKind::PluginFS;
    outEntry.nameW = std::move(fsName);
    outEntry.userPartW = std::move(userPart);
    return true;
}

void WriteEntries(IRegistry* reg, HKEY historyKey, const std::vector<Entry>& entries)
{
    if (reg == nullptr || historyKey == nullptr)
        return;

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const std::wstring payload = SerializeEntry(entries[i]);
        const std::wstring name = IndexValueNameW(static_cast<int>(i) + 1);
        reg->SetString(historyKey, name.c_str(), payload.c_str());
    }
}

void ReadEntries(IRegistry* reg, HKEY historyKey, std::vector<Entry>& outEntries,
                 PluginFSPathDetector detector)
{
    outEntries.clear();
    if (reg == nullptr || historyKey == nullptr)
        return;

    for (int i = 1;; ++i)
    {
        const std::wstring name = IndexValueNameW(i);
        std::wstring payload;
        RegistryResult result = reg->GetString(historyKey, name.c_str(), payload);
        if (!result.success)
            break; // first miss terminates the loop — matches the legacy reader.

        Entry entry;
        if (ParseEntry(payload, entry, detector))
            outEntries.push_back(std::move(entry));
        // Silently skip unparseable entries; the legacy reader emitted
        // TRACE_E in this case but the serializer is intentionally
        // dependency-light for headless tests.
    }
}
} // namespace sally::path::history
