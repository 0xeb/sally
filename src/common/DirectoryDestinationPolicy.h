// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace sally::operation_planner
{
    inline std::wstring NormalizeDirectoryComparisonPath(std::wstring text)
    {
        std::replace(text.begin(), text.end(), L'/', L'\\');
        if (text.size() >= 8 && _wcsnicmp(text.c_str(), L"\\\\?\\UNC\\", 8) == 0)
            text = L"\\\\" + text.substr(8);
        else if (text.size() >= 6 && text.compare(0, 4, L"\\\\?\\") == 0 && text[5] == L':')
            text.erase(0, 4);
        std::wstring prefix;
        size_t position = 0, floor = 0;
        bool driveRelative = false;
        if (text.size() >= 2 && text[1] == L':')
        {
            prefix = text.substr(0, 2);
            position = 2;
            driveRelative = true;
            if (position < text.size() && text[position] == L'\\')
            {
                prefix.push_back(L'\\');
                ++position;
                driveRelative = false;
            }
        }
        else if (text.compare(0, 2, L"\\\\") == 0)
        {
            prefix = L"\\\\";
            position = 2;
            floor = 2;
        }
        else if (!text.empty() && text.front() == L'\\')
        {
            prefix = L"\\";
            position = 1;
        }
        std::vector<std::wstring_view> parts;
        while (position < text.size())
        {
            const size_t end = text.find(L'\\', position);
            const size_t count = end == std::wstring::npos ? text.size() - position : end - position;
            const std::wstring_view part(text.data() + position, count);
            if (!part.empty() && part != L".")
            {
                if (part == L"..")
                {
                    if (parts.size() > floor && parts.back() != L"..")
                        parts.pop_back();
                    else if (prefix.empty() || driveRelative)
                        parts.push_back(part);
                }
                else
                    parts.push_back(part);
            }
            if (end == std::wstring::npos)
                break;
            position = end + 1;
        }
        std::wstring result = prefix;
        for (auto part : parts)
        {
            if (!result.empty() && result.back() != L'\\' && !(driveRelative && result.size() == 2))
                result.push_back(L'\\');
            result.append(part);
        }
        return result;
    }

    inline bool IsSameOrDescendantDirectory(const std::wstring& source, const std::wstring& target)
    {
        if (source.empty() || target.size() < source.size() || _wcsnicmp(source.c_str(), target.c_str(), source.size()) != 0)
            return false;
        return target.size() == source.size() || source.back() == L'\\' || target[source.size()] == L'\\';
    }
} // namespace sally::operation_planner
