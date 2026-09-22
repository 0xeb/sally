// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "HexPattern.h"

#include <limits>
#include <new>
#include <stdexcept>

namespace Sally::Unicode
{

bool FormatHexPattern(const std::uint8_t* bytes, std::size_t size,
                      std::wstring& text) noexcept
{
    if (bytes == nullptr && size != 0)
        return false;
    if (size > (std::numeric_limits<std::size_t>::max)() / 3)
        return false;
    const std::size_t outputSize = size == 0 ? 0 : size * 3 - 1;
    if (outputSize > std::wstring().max_size())
        return false;

    static constexpr wchar_t digits[] = L"0123456789ABCDEF";
    try
    {
        std::wstring formatted(outputSize, L' ');
        for (std::size_t i = 0; i < size; ++i)
        {
            formatted[i * 3] = digits[bytes[i] >> 4];
            formatted[i * 3 + 1] = digits[bytes[i] & 0x0f];
        }
        text.swap(formatted);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}

namespace
{
int HexDigit(wchar_t ch)
{
    if (ch >= L'0' && ch <= L'9')
        return ch - L'0';
    if (ch >= L'a' && ch <= L'f')
        return 10 + ch - L'a';
    if (ch >= L'A' && ch <= L'F')
        return 10 + ch - L'A';
    return -1;
}

void AppendUtf8(std::uint32_t scalar, std::vector<std::uint8_t>& bytes)
{
    if (scalar <= 0x7F)
    {
        bytes.push_back(static_cast<std::uint8_t>(scalar));
    }
    else if (scalar <= 0x7FF)
    {
        bytes.push_back(static_cast<std::uint8_t>(0xC0 | (scalar >> 6)));
        bytes.push_back(static_cast<std::uint8_t>(0x80 | (scalar & 0x3F)));
    }
    else if (scalar <= 0xFFFF)
    {
        bytes.push_back(static_cast<std::uint8_t>(0xE0 | (scalar >> 12)));
        bytes.push_back(static_cast<std::uint8_t>(0x80 | ((scalar >> 6) & 0x3F)));
        bytes.push_back(static_cast<std::uint8_t>(0x80 | (scalar & 0x3F)));
    }
    else
    {
        bytes.push_back(static_cast<std::uint8_t>(0xF0 | (scalar >> 18)));
        bytes.push_back(static_cast<std::uint8_t>(0x80 | ((scalar >> 12) & 0x3F)));
        bytes.push_back(static_cast<std::uint8_t>(0x80 | ((scalar >> 6) & 0x3F)));
        bytes.push_back(static_cast<std::uint8_t>(0x80 | (scalar & 0x3F)));
    }
}
} // namespace

bool ParseHexPattern(std::wstring_view text, std::vector<std::uint8_t>& bytes)
{
    bytes.clear();
    std::vector<std::uint8_t> parsed;
    for (std::size_t i = 0; i < text.size();)
    {
        if (text[i] == L' ')
        {
            ++i;
            continue;
        }

        if (text[i] == L'"')
        {
            ++i;
            while (i < text.size() && text[i] != L'"')
            {
                std::uint32_t scalar = static_cast<std::uint32_t>(text[i++]);
                if (scalar >= 0xD800 && scalar <= 0xDBFF)
                {
                    if (i == text.size())
                        return false;
                    const std::uint32_t low = static_cast<std::uint32_t>(text[i++]);
                    if (low < 0xDC00 || low > 0xDFFF)
                        return false;
                    scalar = 0x10000 + ((scalar - 0xD800) << 10) + (low - 0xDC00);
                }
                else if (scalar >= 0xDC00 && scalar <= 0xDFFF)
                {
                    return false;
                }
                if (scalar > 0x10FFFF)
                    return false;
                AppendUtf8(scalar, parsed);
            }
            if (i == text.size())
                return false;
            ++i;
            continue;
        }

        const int high = HexDigit(text[i++]);
        if (high < 0)
            return false;

        int value = high;
        if (i < text.size() && text[i] != L' ' && text[i] != L'"')
        {
            const int low = HexDigit(text[i++]);
            if (low < 0)
                return false;
            value = (high << 4) | low;
        }
        parsed.push_back(static_cast<std::uint8_t>(value));
    }
    bytes.swap(parsed);
    return true;
}

void NormalizeHexPatternInput(std::wstring& text, int& selectionStart, int& selectionEnd)
{
    const size_t leadingSpaces = text.find_first_not_of(L' ');
    if (leadingSpaces != 0 && leadingSpaces != std::wstring::npos)
    {
        selectionStart -= (int)leadingSpaces;
        selectionEnd -= (int)leadingSpaces;
        if (selectionStart < 0)
            selectionStart = 0;
        if (selectionEnd < 0)
            selectionEnd = 0;
        text.erase(0, leadingSpaces);
    }
    else if (leadingSpaces == std::wstring::npos)
    {
        text.clear();
        selectionStart = selectionEnd = 0;
    }

    bool openedQuotes = false;
    size_t tokenStart = 0;
    size_t pos = 0;
    while (pos < text.size())
    {
        if (text[pos] == L'"')
        {
            if (!openedQuotes && pos > 0 && text[pos - 1] != L' ')
            {
                if (selectionStart > (int)pos)
                    selectionStart++;
                if (selectionEnd > (int)pos)
                    selectionEnd++;
                text.insert(pos, 1, L' ');
                pos++;
            }
            if (openedQuotes)
            {
                if (pos + 1 < text.size() && text[pos + 1] != L' ')
                {
                    if (selectionStart >= (int)pos + 1)
                        selectionStart++;
                    if (selectionEnd >= (int)pos + 1)
                        selectionEnd++;
                    text.insert(pos + 1, 1, L' ');
                }
                const bool hasSeparator = pos + 1 < text.size();
                tokenStart = pos + (hasSeparator ? 2 : 1);
                openedQuotes = false;
                pos += hasSeparator ? 2 : 1;
                continue;
            }
            openedQuotes = true;
        }
        else if (!openedQuotes)
        {
            if (text[pos] == L' ')
            {
                if (tokenStart == pos)
                {
                    if (selectionStart >= (int)pos)
                        selectionStart--;
                    if (selectionEnd >= (int)pos)
                        selectionEnd--;
                    text.erase(pos, 1);
                    continue;
                }
                tokenStart = pos + 1;
            }
            else if (pos - tokenStart == 2)
            {
                if (selectionStart >= (int)pos)
                    selectionStart++;
                if (selectionEnd >= (int)pos)
                    selectionEnd++;
                text.insert(pos, 1, L' ');
                tokenStart = pos + 1;
            }
        }
        pos++;
    }
}
}
