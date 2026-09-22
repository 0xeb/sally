// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

template <class SamePath>
inline void AddFilecompPathToHistory(std::vector<std::wstring>& history,
                                     const wchar_t* path, size_t maximumEntries,
                                     SamePath samePath)
{
    if (path == nullptr || *path == L'\0' || maximumEntries == 0)
        return;

    for (std::vector<std::wstring>::iterator item = history.begin();
         item != history.end(); ++item)
    {
        if (samePath(item->c_str(), path))
        {
            history.erase(item);
            break;
        }
    }
    history.insert(history.begin(), path);
    if (history.size() > maximumEntries)
        history.resize(maximumEntries);
}
