// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>

namespace sally::unicode
{
// Hex and explicitly legacy text views preserve one screen cell per source
// byte. Map those byte cells to UTF-16 without treating a byte count as a
// character count or imposing a rendering ceiling.
inline bool TryMapLegacyByteCells(const char* bytes, size_t byteCount,
                                  const wchar_t (&mapping)[256],
                                  std::wstring& output) noexcept
{
    if (bytes == nullptr && byteCount != 0)
        return false;
    try
    {
        std::wstring staged(byteCount, L'\0');
        for (size_t i = 0; i < byteCount; ++i)
            staged[i] = mapping[static_cast<unsigned char>(bytes[i])];
        output.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
} // namespace sally::unicode
