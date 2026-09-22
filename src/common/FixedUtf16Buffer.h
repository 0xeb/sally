// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstring>
#include <string>

namespace sally::unicode
{
// Projects an owned UTF-16 value into a fixed external record. Failure is explicit:
// callers must not operate on a silently truncated path.
template <size_t N>
bool TryProjectUtf16ToFixedBuffer(const std::wstring& source, wchar_t (&destination)[N])
{
    static_assert(N > 0, "a fixed projection needs room for a terminator");
    if (source.size() >= N)
    {
        destination[0] = L'\0';
        return false;
    }
    std::memcpy(destination, source.c_str(), (source.size() + 1) * sizeof(wchar_t));
    return true;
}
} // namespace sally::unicode
