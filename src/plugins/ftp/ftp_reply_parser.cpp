// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ftp_reply_parser.h"

#include <new>
#include <stdexcept>

namespace
{
constexpr std::string_view KnownSystemNames[] = {
    "UNIX", "Windows", "NETWARE", "TANDEM", "OS/2", "VMS", "MVS", "VM", "OS/400",
};

char AsciiLower(char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool EqualAsciiNoCase(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
        return false;
    for (size_t index = 0; index < left.size(); ++index)
    {
        if (AsciiLower(left[index]) != AsciiLower(right[index]))
            return false;
    }
    return true;
}

bool IsKnownSystemName(std::string_view candidate) noexcept
{
    for (std::string_view known : KnownSystemNames)
    {
        if (EqualAsciiNoCase(candidate, known))
            return true;
    }
    return false;
}
}

bool FTPParseWorkingDirectoryReply(std::string_view reply,
                                   std::string& directory) noexcept
{
    if (reply.size() < 4)
        return false;

    try
    {
        size_t quote = reply.find_first_of("\"'", 4);
        if (quote == std::string_view::npos)
            return false;

        // Some localized AIX servers delimit the path with apostrophes. Match
        // the legacy rule, but search only inside the caller-supplied reply.
        if (reply[quote] == '\'')
        {
            const size_t lastQuote = reply.find_last_of('\'');
            if (lastQuote > quote &&
                reply.find('"', lastQuote + 1) == std::string_view::npos)
            {
                std::string parsed(reply.substr(quote + 1,
                                                lastQuote - quote - 1));
                directory.swap(parsed);
                return true;
            }
            quote = reply.find('"', quote + 1);
            if (quote == std::string_view::npos)
                return false;
        }

        std::string parsed;
        parsed.reserve(reply.size() - quote - 1);
        for (size_t index = quote + 1; index < reply.size(); ++index)
        {
            const char value = reply[index];
            if (value != '"')
            {
                parsed.push_back(value);
                continue;
            }
            if (index + 1 < reply.size() && reply[index + 1] == '"')
            {
                parsed.push_back('"');
                ++index;
                continue;
            }
            directory.swap(parsed);
            return true;
        }
    }
    catch (const std::bad_alloc&)
    {
    }
    catch (const std::length_error&)
    {
    }
    return false;
}

bool FTPParsePassiveReply(std::string_view reply, std::uint32_t& ip,
                          std::uint16_t& port) noexcept
{
    if (reply.size() < 4)
        return false;

    size_t cursor = 4;
    while (cursor < reply.size())
    {
        if (reply[cursor] >= '0' && reply[cursor] <= '9')
        {
            unsigned values[6] = {};
            size_t valueIndex = 0;
            size_t current = cursor;
            for (; valueIndex < 6; ++valueIndex)
            {
                unsigned value = 0;
                bool hasDigit = false;
                while (current < reply.size() && reply[current] >= '0' && reply[current] <= '9')
                {
                    hasDigit = true;
                    if (value <= 255)
                    {
                        value = value * 10 + static_cast<unsigned>(reply[current] - '0');
                        if (value > 255)
                            value = 256;
                    }
                    ++current;
                }
                if (!hasDigit || value > 255)
                    break;
                values[valueIndex] = value;
                if (valueIndex == 5)
                {
                    ip = (values[3] << 24) | (values[2] << 16) |
                         (values[1] << 8) | values[0];
                    port = static_cast<std::uint16_t>((values[4] << 8) | values[5]);
                    return true;
                }

                bool comma = false;
                bool whitespace = false;
                while (current < reply.size())
                {
                    const char byte = reply[current];
                    if (byte == ',')
                    {
                        if (comma)
                            break;
                        comma = true;
                        ++current;
                        continue;
                    }
                    if (byte > ' ')
                        break;
                    whitespace = true;
                    ++current;
                }
                if ((!comma && !whitespace) || current >= reply.size() ||
                    reply[current] < '0' || reply[current] > '9')
                    break;
            }
            // Do not rescan suffixes of a numeric candidate that was already
            // rejected (for example a huge overflowing octet). Apart from
            // accepting a false tail sextet, that made hostile replies O(n^2).
            cursor = current;
            continue;
        }
        ++cursor;
    }
    return false;
}

std::string_view FTPParseServerSystemReply(std::string_view reply) noexcept
{
    if (reply.size() <= 4 || reply[0] != '2')
        return {};

    size_t systemStart = 4;
    if (reply[3] != ' ')
    {
        size_t lineEnd = reply.size();
        while (lineEnd > 0 && (reply[lineEnd - 1] == '\r' || reply[lineEnd - 1] == '\n'))
            --lineEnd;
        const size_t lineStart = reply.find_last_of("\r\n", lineEnd == 0 ? 0 : lineEnd - 1);
        const size_t candidateStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
        if (candidateStart + 4 < lineEnd)
            systemStart = candidateStart + 4;
    }

    while (systemStart < reply.size() &&
           static_cast<unsigned char>(reply[systemStart]) <= static_cast<unsigned char>(' '))
        ++systemStart;
    size_t firstEnd = systemStart;
    while (firstEnd < reply.size() &&
           static_cast<unsigned char>(reply[firstEnd]) > static_cast<unsigned char>(' '))
        ++firstEnd;
    if (firstEnd == systemStart)
        return {};

    const std::string_view first = reply.substr(systemStart, firstEnd - systemStart);
    if (IsKnownSystemName(first))
        return first;

    size_t cursor = firstEnd;
    while (cursor < reply.size())
    {
        while (cursor < reply.size() &&
               static_cast<unsigned char>(reply[cursor]) <= static_cast<unsigned char>(' '))
            ++cursor;
        const size_t candidateStart = cursor;
        while (cursor < reply.size() &&
               static_cast<unsigned char>(reply[cursor]) > static_cast<unsigned char>(' '))
            ++cursor;
        if (candidateStart == cursor)
            break;
        const std::string_view candidate = reply.substr(candidateStart, cursor - candidateStart);
        if (IsKnownSystemName(candidate))
            return candidate;
    }
    return first;
}
