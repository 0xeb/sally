// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "uncab_text.h"

#include "common/Win32TextCodec.h"

#include <cstring>
#include <cwchar>
#include <limits>

bool ProjectCabBytesToWide(const char* source, std::wstring& target)
{
    return DecodeCabMemberName(source, false, target);
}

bool DecodeCabMemberName(const char* source, bool utf8, std::wstring& target)
{
    if (source == nullptr)
        return false;
    const size_t length = std::strlen(source);
    if (utf8)
        return Win32DecodeText(CP_UTF8, source, length, target).Succeeded();
    Win32DecodeTextLenient(CP_ACP, source, length, target);
    return true;
}

bool ProjectWideToCabBytes(const wchar_t* source, std::string& target)
{
    return source != nullptr &&
           Win32EncodeText(CP_ACP, source, std::wcslen(source), target).Succeeded();
}

bool EqualCabBytesIgnoringCase(const char* left, const char* right)
{
    return left != nullptr && right != nullptr &&
           CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE, left, -1, right, -1) ==
               CSTR_EQUAL;
}

bool EqualCabMemberNames(const std::wstring& left, const std::wstring& right) noexcept
{
    if (left.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) ||
        right.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return false;
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
                                right.c_str(), static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

std::vector<std::wstring> SplitUnCabMasks(const wchar_t* masks)
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
