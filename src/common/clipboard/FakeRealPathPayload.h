// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cwchar>
#include <string>

namespace sally::clipboard
{
// SALCF_FAKE_REALPATH is a bounded UTF-16 payload whose first code unit is the
// item kind ('D' or 'F'). The marker is transport metadata, not part of the path.
inline bool TryParseFakeRealPath(const wchar_t* payload, std::size_t capacity,
                                 std::wstring& path, wchar_t* itemKind = nullptr)
{
    path.clear();
    if (itemKind != nullptr)
        *itemKind = L'\0';
    if (payload == nullptr || capacity < 3 ||
        (payload[0] != L'D' && payload[0] != L'F'))
    {
        return false;
    }

    const wchar_t* terminator = static_cast<const wchar_t*>(
        std::wmemchr(payload + 1, L'\0', capacity - 1));
    if (terminator == nullptr || terminator == payload + 1)
        return false;

    path.assign(payload + 1, terminator);
    if (itemKind != nullptr)
        *itemKind = payload[0];
    return true;
}
} // namespace sally::clipboard
