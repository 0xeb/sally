// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <climits>
#include <new>
#include <string>

inline bool ReadWindowTextOwnedW(HWND window, std::wstring& output)
{
    try
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const int length = GetWindowTextLengthW(window);
            if (length < 0)
                return false;
            std::wstring staged(static_cast<size_t>(length) + 1, L'\0');
            SetLastError(ERROR_SUCCESS);
            const int copied = GetWindowTextW(window, staged.data(), length + 1);
            if (copied == 0 && length != 0 && GetLastError() != ERROR_SUCCESS)
                return false;
            if (GetWindowTextLengthW(window) > copied)
                continue;
            staged.resize(static_cast<size_t>(copied));
            output.swap(staged);
            return true;
        }
    }
    catch (const std::bad_alloc&)
    {
    }
    return false;
}

inline bool ReadWindowClassOwnedW(HWND window, std::wstring& output)
{
    try
    {
        size_t capacity = 32;
        for (;;)
        {
            if (capacity > static_cast<size_t>(INT_MAX))
                return false;
            std::wstring staged(capacity, L'\0');
            const int copied = GetClassNameW(window, staged.data(), static_cast<int>(capacity));
            if (copied == 0)
                return false;
            if (static_cast<size_t>(copied) < capacity - 1)
            {
                staged.resize(static_cast<size_t>(copied));
                output.swap(staged);
                return true;
            }
            capacity *= 2;
        }
    }
    catch (const std::bad_alloc&)
    {
    }
    return false;
}
