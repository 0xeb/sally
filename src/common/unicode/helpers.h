// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <windows.h>

// UTF-16 conversion helpers used during decoupling and Unicode work.
inline std::wstring AnsiToWide(const char* s)
{
    if (s == NULL)
        return std::wstring();
    int len = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (len <= 0)
        return std::wstring();
    std::wstring out(len - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, out.data(), len);
    return out;
}
