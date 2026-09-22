// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/unicode/helpers.h"

#include <string>
#include <vector>
#include <windows.h>

namespace sally::find
{
struct LookInSeed
{
    std::wstring wide;
};

inline LookInSeed BuildLookInSeed(const wchar_t* path)
{
    LookInSeed seed;
    if (path != nullptr)
        seed.wide = path;
    return seed;
}

inline bool HasInitialLookInSeed(const LookInSeed& seed)
{
    return !seed.wide.empty();
}

inline int NormalizeFindFileTypeMode(int mode)
{
    return mode >= 0 && mode <= 2 ? mode : 0;
}

inline void TrimLookInPathW(std::wstring& path)
{
    size_t first = 0;
    while (first < path.length() && path[first] <= L' ')
        first++;

    size_t last = path.length();
    while (last > first && path[last - 1] <= L' ')
        last--;

    if (first != 0 || last != path.length())
        path = path.substr(first, last - first);
}

inline void KeepSingleTrailingSeparatorW(std::wstring& path)
{
    if (path.empty())
        return;

    size_t end = path.length();
    while (end > 1 && (path[end - 1] == L'\\' || path[end - 1] == L'/') &&
           (path[end - 2] == L'\\' || path[end - 2] == L'/'))
    {
        end--;
    }
    path.resize(end);
}

inline std::vector<std::wstring> SplitLookInPathsW(const std::wstring& text)
{
    std::vector<std::wstring> paths;
    std::wstring current;

    for (size_t i = 0; i < text.length(); i++)
    {
        if (text[i] == L';')
        {
            if (i + 1 < text.length() && text[i + 1] == L';')
            {
                current.push_back(L';');
                i++;
            }
            else
            {
                TrimLookInPathW(current);
                KeepSingleTrailingSeparatorW(current);
                if (!current.empty())
                    paths.push_back(current);
                current.clear();
            }
        }
        else
            current.push_back(text[i]);
    }

    TrimLookInPathW(current);
    KeepSingleTrailingSeparatorW(current);
    if (!current.empty())
        paths.push_back(current);

    return paths;
}
} // namespace sally::find
