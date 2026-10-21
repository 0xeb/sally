// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ContentSearcher.h"
#include "Utf8CaseFold.h"
#include <string_view>

namespace sally::text
{
    // Apply the existing UTF-16 literal-search word policy to UTF-8 byte-engine
    // offsets. A match must begin/end at character boundaries, never within a
    // multi-byte character. This does not change the byte regex engine itself.
    inline bool HasWideWordBoundariesUtf8(std::string_view text, std::size_t start, std::size_t length)
    {
        if (start > text.size() || length > text.size() - start)
            return false;
        const std::size_t end = start + length;
        const auto continuation = [](char byte)
        { return (static_cast<unsigned char>(byte) & 0xC0) == 0x80; };
        if ((start < text.size() && continuation(text[start])) ||
            (end < text.size() && continuation(text[end])))
            return false;

        // The shared literal predicate classifies UTF-16 code units. Its current
        // supplementary-character policy treats surrogate units as non-word;
        // preserve that policy rather than truncating a scalar into a BMP value.
        const auto word = [](char32_t scalar)
        { return scalar <= 0xFFFF && IsWordCharacterW(static_cast<wchar_t>(scalar)); };

        if (start != 0)
        {
            std::size_t previous = start - 1;
            while (previous != 0 && start - previous < 4 && continuation(text[previous]))
                --previous;
            char32_t scalar = 0;
            const std::size_t consumed = detail::DecodeUtf8(text, previous, scalar);
            if (consumed == 0 || consumed != start - previous || word(scalar))
                return false;
        }
        if (end != text.size())
        {
            char32_t scalar = 0;
            if (detail::DecodeUtf8(text, end, scalar) == 0 || word(scalar))
                return false;
        }
        return true;
    }
} // namespace sally::text
