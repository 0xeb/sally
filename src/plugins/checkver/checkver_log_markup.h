// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cwchar>
#include <string>

namespace checkver
{

inline bool ExtractFlexLink(const wchar_t* line, size_t characterIndex, std::wstring& link)
{
    if (line == nullptr)
        return false;

    const size_t length = std::wcslen(line);
    if (characterIndex > length)
        return false;

    const wchar_t* begin = line + characterIndex;
    while (*begin != L'\0' && *begin != L'\t')
        ++begin;
    if (*begin != L'\t' || begin[1] != L'l')
        return false;
    begin += 2;

    const wchar_t* end = begin;
    while (*end != L'\0' && *end != L'\t')
        ++end;
    if (end == begin)
        return false;

    std::wstring extracted(begin, end);
    link.swap(extracted);
    return true;
}

} // namespace checkver
