// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string.h>
#include <wchar.h>

namespace sally::unicode
{
inline bool HasWidePathPair(const std::wstring& sourceW, const std::wstring& targetW)
{
    return !sourceW.empty() && !targetW.empty();
}

inline bool ArePathsExactlySame(const char* sourceA, const char* targetA,
                                const std::wstring& sourceW, const std::wstring& targetW)
{
    if (HasWidePathPair(sourceW, targetW))
        return wcscmp(sourceW.c_str(), targetW.c_str()) == 0;

    if (sourceA == NULL || targetA == NULL)
        return false;

    return strcmp(sourceA, targetA) == 0;
}

inline bool ArePathsEquivalentForCopy(const char* sourceA, const char* targetA,
                                      const std::wstring& sourceW, const std::wstring& targetW)
{
    if (HasWidePathPair(sourceW, targetW))
        return _wcsicmp(sourceW.c_str(), targetW.c_str()) == 0;

    if (sourceA == NULL || targetA == NULL)
        return false;

    return _stricmp(sourceA, targetA) == 0;
}
} // namespace sally::unicode

