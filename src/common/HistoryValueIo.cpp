// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "common/HistoryValueIo.h"

#include "consts.h"

namespace sally::registry
{

std::wstring HonestValueName(int index)
{
    return std::to_wstring(index) + L"W";
}

std::string LegacyValueName(int index)
{
    return std::to_string(index);
}

bool WriteHistoryEntry(HKEY key, int index, const wchar_t* text)
{
    if (text == NULL)
        return false;

    const DWORD bytes = (DWORD)((wcslen(text) + 1) * sizeof(wchar_t));

    return SetValueW(key, HonestValueName(index).c_str(), REG_SZ, text, bytes) != FALSE;
}

std::wstring ReadHistoryEntry(HKEY key, int index)
{
    // Honest value wins whenever it exists.
    const std::wstring honestName = HonestValueName(index);
    DWORD size = 0;
    if (GetSizeW(key, honestName.c_str(), REG_SZ, size) && size >= sizeof(wchar_t))
    {
        std::wstring value(size / sizeof(wchar_t), L'\0');
        if (GetValueW(key, honestName.c_str(), REG_SZ, &value[0], size))
        {
            value.resize(wcsnlen(value.c_str(), value.size()));
            return value;
        }
    }

    // Named read-only import of the legacy shape. The wide bytes come back
    // through the ANSI API exactly as they went in, so the buffer already IS
    // the wide string. New code never writes this form.
    const std::string legacyName = LegacyValueName(index);
    DWORD legacySize = 0;
    DWORD legacyType = REG_NONE;
    if (RegQueryValueExA(key, legacyName.c_str(), NULL, &legacyType, NULL, &legacySize) == ERROR_SUCCESS &&
        legacyType == REG_SZ && legacySize >= sizeof(wchar_t))
    {
        // +1 wchar of headroom: the stored byte count is whatever the writer
        // passed, and a truncated or odd-sized value must not leave the buffer
        // unterminated.
        std::wstring value(legacySize / sizeof(wchar_t) + 1, L'\0');
        DWORD readSize = legacySize;
        if (RegQueryValueExA(key, legacyName.c_str(), NULL, NULL,
                             reinterpret_cast<BYTE*>(&value[0]), &readSize) == ERROR_SUCCESS)
        {
            value.resize(wcsnlen(value.c_str(), value.size()));
            return value;
        }
    }

    return std::wstring();
}

bool HistoryEntryExists(HKEY key, int index)
{
    DWORD size = 0;
    if (GetSizeW(key, HonestValueName(index).c_str(), REG_SZ, size))
        return true;
    const std::string legacyName = LegacyValueName(index);
    return RegQueryValueExA(key, legacyName.c_str(), NULL, NULL, NULL, &size) == ERROR_SUCCESS;
}

} // namespace sally::registry
