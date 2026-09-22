// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "widefind.h"

#include <cwctype>

namespace sally::dbviewer
{

bool IsWordCharacterWide(wchar_t c)
{
    return c == L'_' || iswalnum(c) != 0;
}

int FindWideSubstring(const wchar_t* haystack, std::size_t haystackLen,
                      const wchar_t* needle, std::size_t needleLen,
                      bool caseSensitive, bool wholeWords, int startOffset)
{
    if (needleLen == 0 || startOffset < 0 || (std::size_t)startOffset + needleLen > haystackLen)
        return -1;
    for (std::size_t pos = (std::size_t)startOffset; pos + needleLen <= haystackLen; pos++)
    {
        bool match = true;
        for (std::size_t k = 0; k < needleLen; k++)
        {
            wchar_t a = haystack[pos + k];
            wchar_t b = needle[k];
            if (!caseSensitive)
            {
                if (a >= L'a' && a <= L'z')
                    a = (wchar_t)(a - L'a' + L'A');
                if (b >= L'a' && b <= L'z')
                    b = (wchar_t)(b - L'a' + L'A');
            }
            if (a != b)
            {
                match = false;
                break;
            }
        }
        if (!match)
            continue;
        if (wholeWords)
        {
            bool leftOk = (pos == 0) || !IsWordCharacterWide(haystack[pos - 1]);
            bool rightOk = (pos + needleLen >= haystackLen) || !IsWordCharacterWide(haystack[pos + needleLen]);
            if (!leftOk || !rightOk)
                continue;
        }
        return (int)pos;
    }
    return -1;
}

FindTextDomain ChooseFindTextDomain(bool regularExpression, bool databaseIsUnicode)
{
    if (!databaseIsUnicode)
        return FindTextDomain::AnsiBytes; // already ANSI bytes on disk, in both modes
    return regularExpression ? FindTextDomain::Utf8Bytes : FindTextDomain::Wide;
}

bool IsUtf8WordByte(unsigned char b)
{
    return b >= 0x80;
}

} // namespace sally::dbviewer
