// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "digest_parser.h"

#include <cstring>

namespace
{
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
