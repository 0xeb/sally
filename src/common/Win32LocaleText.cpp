// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/Win32LocaleText.h"

std::wstring Win32FormatLocalDateTimePart(const SYSTEMTIME& value, bool date)
{
    auto query = [&](wchar_t* buffer, int count)
    {
        return date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value, nullptr, buffer, count)
                    : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value, nullptr, buffer, count);
    };
    for (;;)
    {
        const int required = query(nullptr, 0);
        if (required <= 0)
            return {};
        std::wstring text(static_cast<size_t>(required), L'\0');
        const int copied = query(text.data(), required);
        if (copied > 0)
        {
            text.resize(static_cast<size_t>(copied - 1));
            return text;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return {};
    }
}
