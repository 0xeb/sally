// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <limits>
#include <string_view>
#include <type_traits>

namespace sally::plugin_text
{
    inline bool ParseSignedInteger(std::wstring_view text, int& value)
    {
        if (text.empty())
            return false;
        const bool negative = text.front() == L'-';
        size_t position = negative || text.front() == L'+' ? 1 : 0;
        if (position == text.size())
            return false;

        using Unsigned = std::make_unsigned_t<int>;
        const Unsigned maximum = static_cast<Unsigned>((std::numeric_limits<int>::max)());
        const Unsigned limit = negative ? maximum + 1 : maximum;
        Unsigned magnitude = 0;
        for (; position < text.size(); ++position)
        {
            const wchar_t character = text[position];
            if (character < L'0' || character > L'9')
                return false;
            const Unsigned digit = static_cast<Unsigned>(character - L'0');
            if (magnitude > (limit - digit) / 10)
                return false;
            magnitude = magnitude * 10 + digit;
        }

        if (negative)
            value = magnitude == maximum + 1 ? (std::numeric_limits<int>::min)() : -static_cast<int>(magnitude);
        else
            value = static_cast<int>(magnitude);
        return true;
    }
} // namespace sally::plugin_text
