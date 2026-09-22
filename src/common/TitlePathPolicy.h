// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <string>

namespace sally::unicode
{

inline std::wstring MakeCompositePathTitle(std::wstring path, size_t rootLength)
{
    rootLength = (std::min)(rootLength, path.size());
    const size_t lastSeparator = path.find_last_of(L'\\');
    if (lastSeparator != std::wstring::npos && lastSeparator > rootLength &&
        lastSeparator + 1 < path.size())
    {
        path.replace(rootLength, lastSeparator - rootLength, L"...");
    }
    return path;
}

inline std::wstring MakeDirectoryPathTitle(std::wstring path, size_t rootLength)
{
    rootLength = (std::min)(rootLength, path.size());
    while (path.size() > rootLength && path.back() == L'\\')
        path.pop_back();

    const size_t lastSeparator = path.find_last_of(L'\\');
    if (lastSeparator != std::wstring::npos && lastSeparator + 1 >= rootLength &&
        lastSeparator + 1 < path.size())
    {
        path.erase(0, lastSeparator + 1);
    }
    return path;
}

} // namespace sally::unicode
