// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "regedt_find_pattern.h"

#include "common/Win32TextCodec.h"

#include <cstring>
#include <cwctype>
#include <limits>

// regedt's byte regexp engine runs entirely in the ambient process code page.
// That is ONE boundary, named here once, crossed in three directions: an exact
// pattern projection, a lossy subject projection, and an error decode.
static constexpr UINT RegedtSearchCodePage = CP_ACP;

bool EncodeRegedtSearchPattern(std::wstring_view pattern, std::string& bytes) noexcept
{
    return Win32EncodeText(RegedtSearchCodePage, pattern.data(), pattern.size(), bytes).Succeeded();
}

bool EncodeRegedtSearchSubject(std::wstring_view text, std::string& bytes) noexcept
{
    // The subject is only scanned by the byte regexp engine; it never names a key
    // or a value. An unrepresentable character therefore has to degrade that one
    // line, not remove the whole item from the search - which is what the strict
    // pattern encoder above would do, and what pre-unicode's best-effort
    // WStrToStr never did. The PATTERN stays exact: a substituted character there
    // would silently search for something the user did not type.
    return Win32EncodeTextLossy(RegedtSearchCodePage, text.data(), text.size(), bytes).Succeeded();
}

bool MakeRegedtUtf16SearchBytes(std::wstring_view pattern, std::vector<char>& bytes) noexcept
{
    if (pattern.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) / sizeof(wchar_t))
        return false;
    try
    {
        std::vector<char> staged(pattern.size() * sizeof(wchar_t));
        if (!staged.empty())
            memcpy(staged.data(), pattern.data(), staged.size());
        bytes.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

namespace
{
bool IsHexDigit(wchar_t value)
{
    return (value >= L'0' && value <= L'9') ||
           (value >= L'a' && value <= L'f') ||
           (value >= L'A' && value <= L'F');
}

unsigned char HexDigitValue(wchar_t value)
{
    if (value >= L'0' && value <= L'9')
        return static_cast<unsigned char>(value - L'0');
    return static_cast<unsigned char>(10 + std::towlower(value) - L'a');
}

bool ReadDecimal(std::wstring_view text, size_t& offset, unsigned short& value)
{
    if (offset == text.size() || !iswdigit(text[offset]))
        return false;
    unsigned int parsed = 0;
    size_t count = 0;
    while (offset < text.size() && iswdigit(text[offset]) && count < 2)
    {
        parsed = parsed * 10 + static_cast<unsigned int>(text[offset++] - L'0');
        ++count;
    }
    value = static_cast<unsigned short>(parsed);
    return true;
}

bool ReadYear(std::wstring_view text, size_t& offset, unsigned short& value)
{
    if (offset == text.size() || !iswdigit(text[offset]))
        return false;
    unsigned int parsed = 0;
    size_t count = 0;
    while (offset < text.size() && iswdigit(text[offset]) && count < 4)
    {
        parsed = parsed * 10 + static_cast<unsigned int>(text[offset++] - L'0');
        ++count;
    }
    if (offset < text.size() && iswdigit(text[offset]))
        return false;
    value = static_cast<unsigned short>(parsed);
    return true;
}
} // namespace

bool DecodeRegedtHexSearchPattern(std::wstring_view pattern, std::vector<char>& bytes) noexcept
{
    try
    {
        std::vector<char> staged;
        staged.reserve(pattern.size());
        bool asciiQuote = false;
        bool unicodeQuote = false;
        for (size_t offset = 0; offset < pattern.size();)
        {
            const wchar_t current = pattern[offset];
            if (current == L'"' && !asciiQuote)
            {
                unicodeQuote = !unicodeQuote;
                ++offset;
            }
            else if (current == L'\'' && !unicodeQuote)
            {
                asciiQuote = !asciiQuote;
                ++offset;
            }
            else if (unicodeQuote)
            {
                const char* raw = reinterpret_cast<const char*>(&pattern[offset++]);
                staged.insert(staged.end(), raw, raw + sizeof(wchar_t));
            }
            else if (asciiQuote)
            {
                std::string encoded;
                if (!EncodeRegedtSearchPattern(pattern.substr(offset, 1), encoded) || encoded.size() != 1)
                    return false;
                staged.push_back(encoded[0]);
                ++offset;
            }
            else if (IsHexDigit(current))
            {
                unsigned char value = HexDigitValue(current);
                ++offset;
                if (offset < pattern.size() && IsHexDigit(pattern[offset]))
                    value = static_cast<unsigned char>((value << 4) | HexDigitValue(pattern[offset++]));
                staged.push_back(static_cast<char>(value));
            }
            else
                ++offset;
        }
        if (staged.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return false;
        bytes.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool DecodeRegedtRegexError(const char* bytes, std::wstring& text) noexcept
{
    if (bytes == nullptr)
        return false;
    return Win32DecodeText(RegedtSearchCodePage, bytes, std::strlen(bytes), text).Succeeded();
}

bool ParseRegedtFindTime(std::wstring_view text, bool maximum, SYSTEMTIME& time) noexcept
{
    while (!text.empty() && iswspace(text.back()))
        text.remove_suffix(1);
    if (text.empty())
        return false;

    SYSTEMTIME staged{};
    size_t offset = 0;
    const size_t firstColon = text.find(L':');
    if (firstColon == 1 || firstColon == 2)
    {
        if (!ReadDecimal(text, offset, staged.wHour) || offset >= text.size() || text[offset++] != L':' ||
            !ReadDecimal(text, offset, staged.wMinute) || offset >= text.size() || text[offset++] != L':' ||
            !ReadDecimal(text, offset, staged.wSecond) || offset >= text.size() || !iswspace(text[offset]))
            return false;
        ++offset;
    }
    else if (maximum)
    {
        staged.wHour = 23;
        staged.wMinute = 59;
        staged.wSecond = 59;
    }

    if (!ReadDecimal(text, offset, staged.wDay) || offset >= text.size() || text[offset++] != L'.' ||
        !ReadDecimal(text, offset, staged.wMonth) || offset >= text.size() || text[offset++] != L'.' ||
        !ReadYear(text, offset, staged.wYear) || offset != text.size())
        return false;

    FILETIME fileTime{};
    if (!SystemTimeToFileTime(&staged, &fileTime))
        return false;
    time = staged;
    return true;
}
