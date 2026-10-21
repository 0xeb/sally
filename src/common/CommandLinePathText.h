// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>

namespace sally::input
{
    inline std::wstring QuoteCommandLinePath(std::wstring_view path)
    {
        if (!path.empty() && path.find_first_of(L" \t\r\n\"&|<>^()") == std::wstring_view::npos)
            return std::wstring(path);
        std::wstring result(1, L'"');
        size_t slashes = 0;
        for (wchar_t character : path)
        {
            if (character == L'\\')
            {
                ++slashes;
                continue;
            }
            result.append(character == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            result.push_back(character);
            slashes = 0;
        }
        // Keep a directory's final backslash from escaping the closing quote
        // when a Windows program parses the resulting command-line argument.
        result.append(slashes * 2, L'\\');
        result.push_back(L'"');
        return result;
    }
} // namespace sally::input
