// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

namespace sally::find
{
inline std::wstring NormalizeFindMasksForSearch(const std::wstring& input)
{
    std::wstring result;
    result.reserve(input.length() + 16);
    const wchar_t* begin = input.c_str();
    while (true)
    {
        const wchar_t* end = begin;
        while (*end != 0)
        {
            if (*end == L'|')
                break;
            if (*end == L';')
            {
                if (*(end + 1) != L';')
                    break;
                end++;
            }
            end++;
        }

        while (*begin != 0 && *begin <= L' ')
            begin++;
        const wchar_t* trimmedEnd = end;
        while (trimmedEnd > begin && *(trimmedEnd - 1) <= L' ')
            trimmedEnd--;

        if (trimmedEnd > begin)
        {
            bool hasWildcardOrDot = false;
            for (const wchar_t* current = begin; current < trimmedEnd; current++)
            {
                if (*current == L'*' || *current == L'?' || *current == L'.')
                {
                    hasWildcardOrDot = true;
                    break;
                }
            }
            if (!hasWildcardOrDot)
                result.push_back(L'*');
            result.append(begin, trimmedEnd);
            if (!hasWildcardOrDot)
                result.push_back(L'*');
        }

        if (*end == 0)
            break;
        result.push_back(*end);
        begin = end + 1;
    }
    return result.empty() ? L"*" : result;
}

inline std::wstring MakeRelativeSelectionName(const std::wstring& prefix,
                                              const std::wstring& directory,
                                              const std::wstring& name)
{
    std::wstring relative = directory.size() >= prefix.size()
                                ? directory.substr(prefix.size())
                                : directory;
    const size_t first = relative.find_first_not_of(L'\\');
    if (first == std::wstring::npos)
        relative.clear();
    else if (first != 0)
        relative.erase(0, first);
    if (!relative.empty() && relative.back() != L'\\')
        relative.push_back(L'\\');
    relative += name;
    return relative;
}

inline bool SplitFindLogFocusPath(std::wstring fullPath,
                                  std::wstring& directory,
                                  std::wstring& name)
{
    while (!fullPath.empty() && fullPath.back() == L'\\')
        fullPath.pop_back();
    const size_t separator = fullPath.rfind(L'\\');
    if (separator == std::wstring::npos)
        return false;
    directory.assign(fullPath, 0, separator);
    name.assign(fullPath, separator + 1, std::wstring::npos);
    return !name.empty();
}
} // namespace sally::find
