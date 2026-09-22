// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <windows.h>

namespace sally::unicode
{
// Returns true when the wide-cache value is populated (non-null and non-empty).
//
// Use this to gate the wide-vs-ANSI dispatch when a panel keeps both a wide
// source-of-truth path/name and a lossy ANSI mirror. Whenever this returns
// true, downstream code must prefer the wide cache; the ANSI cache may
// contain `?`-mangled bytes for non-CP_ACP characters and routing to ANSI
// validators with that buffer will misreport the path as nonexistent.
//
// Centralized here so the single decision can be tested in isolation and
// reused everywhere a "wide cache populated?" question is asked.
inline bool HasWidePathW(const wchar_t* widePath)
{
    return widePath != NULL && widePath[0] != L'\0';
}

inline bool IsPathSeparatorW(wchar_t ch)
{
    return ch == L'\\' || ch == L'/';
}

inline size_t RootLengthW(const wchar_t* path, size_t len)
{
    if (path == NULL || len == 0)
        return 0;

    if (len >= 2 && IsPathSeparatorW(path[0]) && IsPathSeparatorW(path[1]))
    {
        size_t pos = 2;
        while (pos < len && !IsPathSeparatorW(path[pos]))
            ++pos;
        if (pos == len)
            return len;

        ++pos;
        while (pos < len && !IsPathSeparatorW(path[pos]))
            ++pos;
        if (pos == len)
            return len;

        return pos + 1;
    }

    if (len >= 2 && path[1] == L':')
    {
        if (len >= 3 && IsPathSeparatorW(path[2]))
            return 3;
        return 2;
    }

    if (IsPathSeparatorW(path[0]))
        return 1;

    return 0;
}

inline std::wstring TrimTrailingPathSeparatorsW(std::wstring path)
{
    size_t rootLen = RootLengthW(path.c_str(), path.length());
    while (path.length() > rootLen && IsPathSeparatorW(path.back()))
        path.pop_back();
    return path;
}

inline bool HasTrailingSlashW(const std::wstring& path)
{
    return !path.empty() && (path.back() == L'\\' || path.back() == L'/');
}

// This took an (ANSI, wide) name pair until CFileData::NameW was retired
// in P1.3 and Name itself became wide. There is one name now, so there is one parameter.
inline std::wstring BuildPanelChildPathW(const std::wstring& parentPathW,
                                         const wchar_t* childName_)
{
    std::wstring childName = childName_ != nullptr ? std::wstring(childName_) : std::wstring();
    if (parentPathW.empty())
        return childName;
    if (childName.empty())
        return parentPathW;

    std::wstring result = parentPathW;
    if (!HasTrailingSlashW(result))
        result.push_back(L'\\');
    result += childName;
    return result;
}

} // namespace sally::unicode
