// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "rename_utils.h"

namespace pictview
{

void TrimRenameFileName(std::wstring& name)
{
    size_t first = 0;
    while (first < name.size() && name[first] <= L' ')
        ++first;

    size_t last = name.size();
    while (last > first && (name[last - 1] <= L' ' || name[last - 1] == L'.'))
        --last;

    name = name.substr(first, last - first);
}

bool RenamePatternContainsInvalidCharacter(const std::wstring& pattern)
{
    for (wchar_t ch : pattern)
    {
        if (ch < 32 || ch == L'\\' || ch == L'/' || ch == L':' || ch == L'<' ||
            ch == L'>' || ch == L'|' || ch == L'"')
        {
            return true;
        }
    }
    return false;
}

bool FileNameContainsInvalidRenameCharacter(const std::wstring& name)
{
    for (wchar_t ch : name)
    {
        if (ch < 32 || ch == L'\\' || ch == L'/' || ch == L':' || ch == L'<' ||
            ch == L'>' || ch == L'|' || ch == L'"' || ch == L'*' || ch == L'?')
        {
            return true;
        }
    }
    return false;
}

} // namespace pictview
