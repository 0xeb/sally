// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace UnmimeEncodedName
{
inline char LowerAscii(char value)
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

inline std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](char character) { return LowerAscii(character); });
    return value;
}

inline void SkipCommentsAndWhitespace(const char*& text)
{
    while (*text != '\0')
    {
        if (*text == ' ' || *text == '\t')
        {
            ++text;
            continue;
        }
        if (*text != '(')
            break;

        int depth = 1;
        ++text;
        while (*text != '\0' && depth != 0)
        {
            if (*text == '(')
                ++depth;
            else if (*text == ')')
                --depth;
            ++text;
        }
    }
}

inline std::string TakeWord(const char*& text, const char* delimiters)
{
    const char* begin = text;
    while (*text != '\0' && std::strchr(delimiters, *text) == nullptr)
        ++text;
    return std::string(begin, text);
}

inline bool GetParameter(const char* text, const char* parameter, std::string& value)
{
    if (text == nullptr || parameter == nullptr || *parameter == '\0')
        return false;

    const std::string lowerText = LowerAscii(text);
    const std::string lowerParameter = LowerAscii(parameter);
    size_t position = 0;
    while ((position = lowerText.find(lowerParameter, position)) != std::string::npos)
    {
        if (position == 0 || std::strchr(" \t;()", lowerText[position - 1]) != nullptr)
        {
            const char* cursor = text + position + lowerParameter.size();
            SkipCommentsAndWhitespace(cursor);
            if (*cursor == '=')
            {
                ++cursor;
                SkipCommentsAndWhitespace(cursor);
                value = *cursor == '"' ? TakeWord(++cursor, "\"") : TakeWord(cursor, " \t;()");
                return true;
            }
        }
        ++position;
    }
    return false;
}

inline void Trim(std::string& text, const char* characters)
{
    const size_t first = text.find_first_not_of(characters);
    if (first == std::string::npos)
    {
        text.clear();
        return;
    }
    const size_t last = text.find_last_not_of(characters);
    text = text.substr(first, last - first + 1);
}

inline void Sanitize(std::string& name)
{
    for (char& character : name)
        if (std::strchr("*?\\/<>|\":", character) != nullptr)
            character = '_';
}

inline void InsertSuffix(std::string& filename, int suffix)
{
    const size_t extension = filename.find_last_of('.');
    const std::string suffixText = "(" + std::to_string(suffix) + ")";
    filename.insert(extension == std::string::npos ? filename.size() : extension, suffixText);
}
}
