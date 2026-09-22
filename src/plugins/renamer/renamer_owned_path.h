// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

inline bool CutRenamerPath(std::wstring& path)
{
    if (path.empty())
        return false;
    const size_t last = path.find_last_of(L'\\');
    if (last == std::wstring::npos)
        return false;
    const size_t previous = last == 0 ? std::wstring::npos : path.find_last_of(L'\\', last - 1);
    if (path.rfind(L"\\\\", 0) == 0 && previous != std::wstring::npos && previous <= 1)
        return false;
    if (previous == std::wstring::npos)
        path.resize(last + 1);
    else
        path.resize(last);
    return true;
}

inline void AppendRenamerPath(std::wstring& path, const wchar_t* name)
{
    if (name == nullptr)
        return;
    while (*name == L'\\')
        ++name;
    while (!path.empty() && path.back() == L'\\')
        path.pop_back();
    if (*name != L'\0')
    {
        if (!path.empty())
            path.push_back(L'\\');
        path.append(name);
    }
}

inline size_t RenamerPathRootLength(const std::wstring& path)
{
    if (path.empty())
        return 0;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        const size_t serverEnd = path.find(L'\\', 2);
        if (serverEnd == std::wstring::npos)
            return path.size();
        const size_t shareEnd = path.find(L'\\', serverEnd + 1);
        return shareEnd == std::wstring::npos ? path.size() : shareEnd + 1;
    }
    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\')
        return 3;
    return path[0] == L'\\' ? 1 : 0;
}
