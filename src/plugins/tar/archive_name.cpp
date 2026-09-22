// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <windows.h>

#include <cstring>

#include "archive_name.h"
#include "../../common/Win32TextCodec.h"

#include <limits>

bool DecodeArchiveMemberName(const char* bytes, bool preferUtf8, std::wstring& decoded)
{
    if (bytes == NULL)
        return false;
    const size_t length = strlen(bytes);
    if (preferUtf8 && Win32DecodeText(CP_UTF8, bytes, length, decoded).Succeeded())
        return true;
    return Win32DecodeText(CP_ACP, bytes, length, decoded).Succeeded();
}

bool DecodeArchiveMemberNameLenient(const char* bytes, bool preferUtf8, std::wstring& decoded)
{
    if (bytes == NULL)
        return false;
    if (DecodeArchiveMemberName(bytes, preferUtf8, decoded))
        return true;

    // ISO-8859-1 by construction, not by MultiByteToWideChar: byte N is code point N. No code
    // page is consulted, so this adds no ambient conversion site and cannot fail or substitute.
    const size_t length = strlen(bytes);
    std::wstring latin1;
    latin1.reserve(length);
    for (size_t index = 0; index < length; ++index)
        latin1.push_back(static_cast<wchar_t>(static_cast<unsigned char>(bytes[index])));
    decoded.swap(latin1);
    return true;
}

bool ParsePaxPath(const char* payload, size_t size, std::string& path)
{
    path.clear();
    if (payload == NULL)
        return false;

    size_t offset = 0;
    while (offset < size)
    {
        size_t cursor = offset;
        size_t recordLength = 0;
        while (cursor < size && payload[cursor] >= '0' && payload[cursor] <= '9')
        {
            const size_t digit = static_cast<size_t>(payload[cursor++] - '0');
            if (recordLength > ((std::numeric_limits<size_t>::max)() - digit) / 10)
                return false;
            recordLength = recordLength * 10 + digit;
        }
        if (cursor == offset || cursor >= size || payload[cursor] != ' ' ||
            recordLength == 0 || recordLength > size - offset)
            return false;

        const size_t recordEnd = offset + recordLength;
        if (cursor + 1 >= recordEnd)
            return false;
        const char* valueBegin = payload + cursor + 1;
        const char* equals = static_cast<const char*>(
            memchr(valueBegin, '=', recordEnd - static_cast<size_t>(valueBegin - payload)));
        if (equals == NULL || payload[recordEnd - 1] != '\n')
            return false;
        if (static_cast<size_t>(equals - valueBegin) == 4 && !memcmp(valueBegin, "path", 4))
            path.assign(equals + 1, payload + recordEnd - 1);
        offset = recordEnd;
    }
    return true;
}
