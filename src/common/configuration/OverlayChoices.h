// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace sally::configuration
{
    struct OverlayChoice
    {
        std::wstring Name;
        bool Disabled;
    };

    // Preserve the existing escaped-semicolon format and comparison semantics.
    // Unknown installed-state entries are history, not a request to enable them.
    inline std::wstring MergeDisabledOverlayNames(std::wstring_view previous,
                                                  const std::vector<OverlayChoice>& visible)
    {
        std::vector<std::wstring> names;
        std::wstring entry;
        for (size_t i = 0; i < previous.size(); ++i)
        {
            if (previous[i] != L';')
                entry.push_back(previous[i]);
            else if (i + 1 < previous.size() && previous[i + 1] == L';')
            {
                entry.push_back(L';');
                ++i;
            }
            else
            {
                names.push_back(std::move(entry));
                entry.clear();
            }
        }
        if (!previous.empty())
            names.push_back(std::move(entry));
        bool changed = false;
        for (const auto& choice : visible)
        {
            if (choice.Name.empty())
                continue;
            const auto matches = [&](const std::wstring& name)
            { return _wcsicmp(name.c_str(), choice.Name.c_str()) == 0; };
            const bool exists = std::any_of(names.begin(), names.end(), matches);
            if (choice.Disabled && !exists)
            {
                names.push_back(choice.Name);
                changed = true;
            }
            else if (!choice.Disabled && exists)
            {
                names.erase(std::remove_if(names.begin(), names.end(), matches), names.end());
                changed = true;
            }
        }
        // Avoid normalizing spelling/order/escaping or prompting for a restart
        // when the user accepted unchanged choices.
        if (!changed)
            return std::wstring(previous);
        std::wstring result;
        bool first = true;
        for (const auto& name : names)
        {
            if (!first)
                result.push_back(L';');
            first = false;
            for (wchar_t ch : name)
            {
                result.push_back(ch);
                if (ch == L';')
                    result.push_back(ch);
            }
        }
        return result;
    }
} // namespace sally::configuration
