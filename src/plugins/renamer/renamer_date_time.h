// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "renamer_text.h"

// Formats the New Name menu's Time and Date items insert into $(Time:...) and $(Date:...).
//
// The user's locale formats are no use here: time formats almost always contain ':'
// (HH:mm:ss) and many short dates contain '/' (M/d/yyyy), and neither character can appear
// in a file name, so every generated name would be refused. These fixed formats sort
// naturally and contain only digits and '-'. They are plain ASCII, so they are valid
// engine (UTF-8) text as they stand.
inline constexpr const char RenamerMenuTimeFormat[] = "HH-mm-ss";
inline constexpr const char RenamerMenuDateFormat[] = "yyyy-MM-dd";

// $(Date:fmt) and $(Time:fmt), formatted wide and re-encoded as the UTF-8 the engine
// buffer holds.
//
// These two used the TCHAR GetDateFormat/GetTimeFormat, which resolved to the ANSI form
// while the plugin was built without UNICODE; pinning them to the explicit ...A kept that
// behaviour once UNICODE went build-wide. But the engine buffer is no longer ACP - it is
// UTF-8, and TryRenamerTextToWide decodes it as strict UTF-8 with no fallback. So the ACP
// bytes the ANSI formatter writes for any locale whose date or time names are not plain
// ASCII - Czech, Russian, Greek, Japanese - failed that decode and the whole rename was
// refused, where before the same bytes had simply become the ACP file name.
//
// The format string travels the same road in reverse: it comes from the user's expression,
// so it is UTF-8 and has to be widened before the W formatter sees it.
inline bool FormatWideDateTime(bool date, const std::wstring& format, const SYSTEMTIME& st,
                               std::string& utf8)
{
    utf8.clear();
    const int needed = date ? GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, format.c_str(), NULL, 0)
                            : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, format.c_str(), NULL, 0);
    if (needed <= 0)
        return false;
    std::vector<wchar_t> buffer(static_cast<size_t>(needed), L'\0');
    const int written = date ? GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, format.c_str(),
                                              buffer.data(), needed)
                             : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, format.c_str(),
                                              buffer.data(), needed);
    if (written <= 0)
        return false;
    // 'written' counts the terminator.
    return TryWideToRenamerText(buffer.data(), utf8, written - 1);
}
