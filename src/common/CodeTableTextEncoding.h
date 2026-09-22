// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>

namespace sally::code_table
{

// convert.cfg is a historical process-ACP byte format. Decode it once at the parser boundary;
// table payloads themselves remain opaque 256-byte substitution maps.
inline bool DecodeLegacyText(const char* bytes, size_t length,
                             std::wstring& text) noexcept
{
    return static_cast<bool>(
        Win32DecodeTextPermissive(GetACP(), bytes, length, text));
}

inline wchar_t* DuplicateLegacyText(const char* text) noexcept
{
    if (text == nullptr)
        return nullptr;

    std::wstring decoded;
    if (!DecodeLegacyText(text, std::strlen(text), decoded) ||
        decoded.size() == (std::numeric_limits<size_t>::max)() ||
        decoded.size() + 1 >
            (std::numeric_limits<size_t>::max)() / sizeof(wchar_t))
    {
        return nullptr;
    }

    wchar_t* copy = static_cast<wchar_t*>(
        std::malloc((decoded.size() + 1) * sizeof(wchar_t)));
    if (copy == nullptr)
        return nullptr;
    std::wmemcpy(copy, decoded.c_str(), decoded.size() + 1);
    return copy;
}

} // namespace sally::code_table
