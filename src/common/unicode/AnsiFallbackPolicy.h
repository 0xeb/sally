// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace sally::unicode
{
inline bool WideStringRequiresWidePath(const std::wstring& value)
{
    if (value.empty())
        return false;

    BOOL usedDefaultChar = FALSE;
    int required = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.c_str(), -1,
                                       NULL, 0, NULL, &usedDefaultChar);
    if (required <= 0)
        return true;

    std::vector<char> ansi((size_t)required);
    usedDefaultChar = FALSE;
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.c_str(), -1,
                            ansi.data(), required, NULL, &usedDefaultChar) == 0)
    {
        return true;
    }

    return usedDefaultChar == TRUE;
}
} // namespace sally::unicode
