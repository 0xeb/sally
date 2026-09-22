// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "number_text.h"

std::wstring FormatSize2W(long long size, bool nozero)
{
    if (!size && nozero)
        return std::wstring();

    std::wstring result = std::to_wstring(size);
    const std::size_t firstDigit = !result.empty() && result[0] == L'-' ? 1 : 0;
    for (std::size_t position = result.size(); position > firstDigit + 3;)
    {
        position -= 3;
        result.insert(position, 1, L' ');
    }
    return result;
}
