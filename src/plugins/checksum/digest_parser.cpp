// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "digest_parser.h"

#include <cstring>
#include <algorithm>

namespace
{
    constexpr size_t DigestTextLengths[] = {8, 32, 40, 64, 128};
    std::string_view TrimStart(std::string_view text)
    {
        size_t first = 0;
        while (first < text.size() && static_cast<unsigned char>(text[first]) <= ' ')
            ++first;
        return text.substr(first);
    }

    std::string_view FirstWord(std::string_view text, char delimiter = 0)
    {
        text = TrimStart(text);
        size_t end = 0;
        while (end < text.size() && static_cast<unsigned char>(text[end]) > ' ' && text[end] != delimiter)
            ++end;
        return text.substr(0, end);
    }

    std::string_view LastWord(std::string_view text, char delimiter = 0)
    {
        size_t end = text.size();
        while (end > 0 && static_cast<unsigned char>(text[end - 1]) <= ' ')
            --end;
        size_t first = end;
        while (first > 0 && static_cast<unsigned char>(text[first - 1]) > ' ' && text[first - 1] != delimiter)
            --first;
        return text.substr(first, end - first);
    }

    template <class Char>
    bool IsHexWord(std::basic_string_view<Char> text, size_t length)
    {
        return text.size() == length && std::all_of(text.begin(), text.end(), [](Char value)
                                                    { return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F') || (value >= 'a' && value <= 'f'); });
    }

unsigned char Hex(char value)
{
    if (value >= '0' && value <= '9')
        return static_cast<unsigned char>(value - '0');
    if (value >= 'A' && value <= 'F')
        return static_cast<unsigned char>(value - 'A' + 10);
    if (value >= 'a' && value <= 'f')
        return static_cast<unsigned char>(value - 'a' + 10);
    return 0;
}

void GetFirstWord(char* text, int& position, int& length, char delimiter = 0)
{
    position = 0;
    while (text[position] && static_cast<unsigned char>(text[position]) <= ' ')
        position++;
    length = position;
    while (text[length] && static_cast<unsigned char>(text[length]) > ' ' &&
           text[length] != delimiter)
        length++;
    length -= position;
}

void GetLastWord(char* text, int& position, int& length, char delimiter = 0)
{
    length = static_cast<int>(std::strlen(text));
    while (length > 0 && static_cast<unsigned char>(text[length - 1]) <= ' ')
        length--;
    position = length;
    while (position > 0 && static_cast<unsigned char>(text[position - 1]) > ' ' &&
           text[position - 1] != delimiter)
        position--;
    length -= position;
}
}

namespace checksum
{
    eHASH_TYPE AnalyzeDigestFormat(std::string_view text, eHASH_TYPE preferred)
    {
        // The existing checksum-file grammar is NUL-terminated encoded bytes.
        const size_t nul = text.find('\0');
        if (nul != std::string_view::npos)
            text = text.substr(0, nul);
        bool possible[HT_COUNT] = {true, true, true, true, true};
        constexpr std::string_view names[] = {"MD5", "SHA1", "SHA256", "SHA512"};
        bool hasData = false;
        while (!text.empty())
        {
            const size_t end = text.find_first_of("\r\n");
            const std::string_view line = TrimStart(text.substr(0, end));
            if (end == std::string_view::npos)
                text = {};
            else
                text.remove_prefix(end + 1);
            if (line.empty() || line.front() == ';' || line.front() == '#')
                continue;
            hasData = true;
            possible[HT_CRC] = possible[HT_CRC] && IsHexWord(LastWord(line), DigestTextLengths[HT_CRC]);
            const auto first = FirstWord(line);
            const auto name = FirstWord(line, '(');
            const auto last = LastWord(line, '=');
            for (int i = HT_MD5; i < HT_COUNT; ++i)
            {
                const size_t hash = static_cast<size_t>(i - HT_MD5);
                possible[i] = possible[i] && (IsHexWord(first, DigestTextLengths[i]) ||
                                              (name == names[hash] && IsHexWord(last, DigestTextLengths[i])));
            }
        }
        if (!hasData)
            return HT_CRC; // preserve the historical empty/comment-only selection
        if (preferred >= HT_CRC && preferred < HT_COUNT && possible[preferred])
            return preferred;
        for (int i = HT_CRC; i < HT_COUNT; ++i)
            if (possible[i])
                return static_cast<eHASH_TYPE>(i);
        return HT_COUNT;
    }

    bool IsDigestText(eHASH_TYPE type, std::wstring_view text)
    {
        return type >= HT_CRC && type < HT_COUNT && IsHexWord(text, DigestTextLengths[type]);
    }

void ParseCrcDigest(char* line, std::string& fileName, char* digest)
{
    int position;
    int length;
    GetLastWord(line, position, length);
    for (int i = 0; i < 4; i++)
        digest[i] = static_cast<char>((Hex(line[position + 2 * i]) << 4) |
                                      Hex(line[position + 2 * i + 1]));
    while (position > 0 && (line[position - 1] == ' ' || line[position - 1] == '\t'))
        position--;
    fileName.assign(line, static_cast<size_t>(position));
}

void ParseGenericDigest(char* line, int idLength, int digestLength,
                        std::string& fileName, char* digest)
{
    int position;
    int length;
    GetFirstWord(line, position, length, '(');
    if (length == idLength)
    {
        GetLastWord(line, position, length, '=');
        for (int i = 0; i < digestLength; i++)
            digest[i] = static_cast<char>((Hex(line[position + 2 * i]) << 4) |
                                          Hex(line[position + 2 * i + 1]));
        while (position > 0 && (line[position - 1] == ' ' || line[position - 1] == '\t'))
            position--;
        if (position > 0 && line[position - 1] == '=')
            position--;
        while (position > 0 && (line[position - 1] == ' ' || line[position - 1] == '\t'))
            position--;
        line[position] = 0;

        position = idLength;
        while (line[position] == ' ' || line[position] == '\t')
            position++;
        if (line[position] == '(')
        {
            position++;
            length = static_cast<int>(std::strlen(line));
            if (length > 0 && line[length - 1] == ')')
                line[length - 1] = 0;
            else
                position--;
        }
    }
    else
    {
        for (int i = 0; i < digestLength; i++)
            digest[i] = static_cast<char>((Hex(line[position + 2 * i]) << 4) |
                                          Hex(line[position + 2 * i + 1]));
        position += length;
        while (line[position] && (line[position] == ' ' || line[position] == '\t'))
            position++;
        if (line[position] == '*')
            position++;
    }
    fileName.assign(line + position);
}
}
