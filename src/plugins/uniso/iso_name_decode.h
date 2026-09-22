// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <string>

inline std::wstring DecodeJolietName(const char* bytes, size_t byteCount)
{
    std::wstring result;
    if (bytes == nullptr)
        return result;

    result.reserve(byteCount / 2);
    for (size_t offset = 0; offset + 1 < byteCount; offset += 2)
    {
        const wchar_t ch = (wchar_t)(((unsigned char)bytes[offset] << 8) |
                                     (unsigned char)bytes[offset + 1]);
        if (ch == L'\0')
            break;
        result.push_back(ch);
    }
    return result;
}

inline void StripIsoVersionSuffix(std::wstring& fileName)
{
    const size_t version = fileName.find(L';');
    if (version == std::wstring::npos)
        return;

    const size_t dot = fileName.find(L'.');
    fileName.resize(dot != std::wstring::npos && version - dot == 1 ? dot : version);
}
