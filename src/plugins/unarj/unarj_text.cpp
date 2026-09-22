// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "unarj_text.h"

#include "common/Win32TextCodec.h"

#include <cwchar>

bool DecodeArjMemberName(const char* bytes, size_t length, std::wstring& name)
{
    if (bytes == nullptr && length != 0)
        return false;
    Win32DecodeTextLenient(CP_OEMCP, bytes, length, name);
    return true;
}

bool TryBuildNextArjVolumePath(const std::wstring& current, std::wstring& next)
{
    const size_t slash = current.find_last_of(L"\\/");
    const size_t dot = current.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        return false;

    const std::wstring extension = current.substr(dot);
    std::wstring nextExtension;
    if (_wcsicmp(extension.c_str(), L".arj") == 0)
    {
        nextExtension = L".a01";
    }
    else
    {
        const bool firstIsA = extension.size() > 1 &&
                              (extension[1] == L'A' || extension[1] == L'a');
        const bool firstIsDigit = extension.size() > 1 && extension[1] >= L'0' &&
                                  extension[1] <= L'9';
        if (extension.size() <= 3 || (!firstIsA && !firstIsDigit) ||
            extension[2] < L'0' || extension[2] > L'9' ||
            extension[3] < L'0' || extension[3] > L'9')
            return false;

        int number = firstIsA ? _wtoi(extension.c_str() + 2)
                              : _wtoi(extension.c_str() + 1);
        ++number;
        std::wstring digits = std::to_wstring(number);
        const size_t width = number > 99 || firstIsDigit ? 3u : 2u;
        if (digits.size() < width)
            digits.insert(0, width - digits.size(), L'0');
        nextExtension = number > 99 || firstIsDigit ? L"." + digits : L".a" + digits;
    }

    std::wstring staged(current, 0, dot);
    staged.append(nextExtension);
    next.swap(staged);
    return true;
}

std::vector<std::wstring> SplitArjMasks(const wchar_t* masks)
{
    std::vector<std::wstring> result;
    if (masks == nullptr)
        return result;

    std::wstring current;
    for (const wchar_t* cursor = masks;; ++cursor)
    {
        if (*cursor == L';' && cursor[1] == L';')
        {
            current.push_back(L';');
            ++cursor;
            continue;
        }
        if (*cursor == L';' || *cursor == L'\0')
        {
            const size_t first = current.find_first_not_of(L" \t\r\n");
            if (first != std::wstring::npos)
            {
                const size_t last = current.find_last_not_of(L" \t\r\n");
                result.emplace_back(current.substr(first, last - first + 1));
            }
            current.clear();
            if (*cursor == L'\0')
                break;
            continue;
        }
        current.push_back(*cursor);
    }
    return result;
}
