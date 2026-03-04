// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <vector>
#include <wchar.h>

namespace sally::unicode
{
inline bool ContainsNameIgnoreCase(const std::vector<std::wstring>& names, const std::wstring& candidate)
{
    for (size_t i = 0; i < names.size(); i++)
    {
        if (_wcsicmp(names[i].c_str(), candidate.c_str()) == 0)
            return true;
    }
    return false;
}

inline void SplitFileNameAndExtension(const std::wstring& fileName, std::wstring& stem, std::wstring& extension)
{
    size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0)
    {
        stem = fileName;
        extension.clear();
        return;
    }

    stem = fileName.substr(0, dot);
    extension = fileName.substr(dot);
}

inline std::wstring BuildCopyCandidateName(const std::wstring& originalName, const std::wstring& copyToken, int copyIndex)
{
    std::wstring stem;
    std::wstring extension;
    SplitFileNameAndExtension(originalName, stem, extension);

    std::wstring candidate = stem;
    candidate += L" - ";
    candidate += copyToken;
    if (copyIndex > 1)
    {
        candidate += L" (";
        candidate += std::to_wstring(copyIndex);
        candidate += L")";
    }
    candidate += extension;
    return candidate;
}

inline bool IsOccupiedPathW(const std::wstring& directoryWithBackslash, const std::wstring& fileName)
{
    std::wstring fullPath = directoryWithBackslash;
    fullPath += fileName;
    return GetFileAttributesW(fullPath.c_str()) != INVALID_FILE_ATTRIBUTES;
}

inline bool TryGenerateUniqueCopyName(const std::wstring& directoryWithBackslash,
                                      const std::wstring& originalName,
                                      const std::wstring& copyToken,
                                      const std::vector<std::wstring>& reservedNames,
                                      std::wstring& outName)
{
    if (directoryWithBackslash.empty() || originalName.empty() || copyToken.empty())
        return false;
    if (directoryWithBackslash.back() != L'\\' && directoryWithBackslash.back() != L'/')
        return false;

    for (int copyIndex = 1; copyIndex < 10000; copyIndex++)
    {
        std::wstring candidate = BuildCopyCandidateName(originalName, copyToken, copyIndex);
        if (ContainsNameIgnoreCase(reservedNames, candidate))
            continue;
        if (!IsOccupiedPathW(directoryWithBackslash, candidate))
        {
            outName = candidate;
            return true;
        }
    }
    return false;
}
} // namespace sally::unicode

