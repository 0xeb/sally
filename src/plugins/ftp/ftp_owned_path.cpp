// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ftp_owned_path.h"

#include <limits>

bool FtpRememberSelectedDirectory(std::wstring_view fileName,
                                  std::wstring& initDir) noexcept
{
    const size_t slash = fileName.find_last_of(L"\\/");
    if (slash == std::wstring_view::npos)
        return false;
    try
    {
        std::wstring staged(fileName.substr(0, slash));
        initDir.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool FtpParseTrailingPositiveIndex(std::string_view stem,
                                   size_t& digitsBegin, int& value) noexcept
{
    size_t firstDigit = stem.size();
    while (firstDigit > 0 && stem[firstDigit - 1] >= '0' && stem[firstDigit - 1] <= '9')
        --firstDigit;
    if (firstDigit == stem.size())
        return false;

    int parsed = 0;
    for (size_t current = firstDigit; current < stem.size(); ++current)
    {
        const int digit = stem[current] - '0';
        if (parsed > ((std::numeric_limits<int>::max)() - 1 - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    digitsBegin = firstDigit;
    value = parsed;
    return true;
}
