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

// Helper to show an error/info via gPrompter if available, otherwise fallback MessageBox.
inline void ShowErrorViaPrompter(const wchar_t* title, const wchar_t* message, HWND hwndFallback)
{
    if (gPrompter != NULL)
    {
        gPrompter->ShowError(title, message);
    }
    else
    {
        MessageBoxW(hwndFallback, message, title, MB_OK | MB_ICONEXCLAMATION);
    }
}

inline void ShowInfoViaPrompter(const wchar_t* title, const wchar_t* message, HWND hwndFallback)
{
    if (gPrompter != NULL)
    {
        gPrompter->ShowInfo(title, message);
    }
    else
    {
        MessageBoxW(hwndFallback, message, title, MB_OK | MB_ICONINFORMATION);
    }
}
