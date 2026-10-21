// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string_view>

namespace sally::configuration
{
    // Column widths are stored in unsigned 16-bit fields. Invalid or transient
    // edit text must never wrap or overwrite the last usable value.
    inline bool TryParseColumnWidth(std::wstring_view text, unsigned& width)
    {
        if (!text.empty() && text.front() == L'+')
            text.remove_prefix(1);
        if (text.empty())
            return false;
        unsigned candidate = 0;
        for (wchar_t character : text)
        {
            if (character < L'0' || character > L'9')
                return false;
            const unsigned digit = static_cast<unsigned>(character - L'0');
            if (candidate > (65535u - digit) / 10u)
                return false;
            candidate = candidate * 10u + digit;
        }
        width = candidate;
        return true;
    }
} // namespace sally::configuration
