// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "unlha_text.h"

#include "common/Win32TextCodec.h"

bool DecodeLhaName(const std::string& bytes, std::wstring& name)
{
    Win32DecodeTextLenient(CP_OEMCP, bytes.data(), bytes.size(), name);
    return true;
}

std::vector<std::wstring> SplitLhaMasks(const wchar_t* masks)
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
