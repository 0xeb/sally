// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "regedt_number_parse.h"

#include <limits>

namespace
{
template <typename Char>
bool IsSpace(Char value) noexcept
{
    return value == static_cast<Char>(' ') || value == static_cast<Char>('\t') ||
           value == static_cast<Char>('\r') || value == static_cast<Char>('\n') ||
           value == static_cast<Char>('\f') || value == static_cast<Char>('\v');
}

template <typename Char>
int DigitValue(Char value, unsigned base) noexcept
{
    if (value >= static_cast<Char>('0') && value <= static_cast<Char>('9'))
    {
        const int digit = static_cast<int>(value - static_cast<Char>('0'));
        return digit < static_cast<int>(base) ? digit : -1;
    }
    if (base == 16 && value >= static_cast<Char>('a') && value <= static_cast<Char>('f'))
        return 10 + static_cast<int>(value - static_cast<Char>('a'));
    if (base == 16 && value >= static_cast<Char>('A') && value <= static_cast<Char>('F'))
        return 10 + static_cast<int>(value - static_cast<Char>('A'));
    return -1;
}

template <typename Char>
bool ParseUnsigned(std::basic_string_view<Char> text, unsigned base,
                   std::uint64_t& value) noexcept
{
    size_t index = 0;
    while (index < text.size() && IsSpace(text[index]))
        ++index;

    std::uint64_t staged = 0;
    bool hasDigit = false;
    const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    for (; index < text.size(); ++index)
    {
        const int digit = DigitValue(text[index], base);
        if (digit < 0)
            break;
        hasDigit = true;
        if (staged > (maximum - static_cast<unsigned>(digit)) / base)
            return false;
        staged = staged * base + static_cast<unsigned>(digit);
    }
    if (!hasDigit)
        return false;

    while (index < text.size() && IsSpace(text[index]))
        ++index;
    if (index != text.size())
        return false;

    value = staged;
    return true;
}
} // namespace

bool ParseRegedtUnsignedDecimal(std::string_view text, std::uint64_t& value) noexcept
{
    return ParseUnsigned(text, 10, value);
}

bool ParseRegedtUnsignedDecimal(std::wstring_view text, std::uint64_t& value) noexcept
{
    return ParseUnsigned(text, 10, value);
}

bool ParseRegedtUnsignedHex(std::string_view text, std::uint64_t& value) noexcept
{
    return ParseUnsigned(text, 16, value);
}

bool ParseRegedtUnsignedHex(std::wstring_view text, std::uint64_t& value) noexcept
{
    return ParseUnsigned(text, 16, value);
}
