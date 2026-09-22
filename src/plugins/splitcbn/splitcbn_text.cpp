// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "splitcbn_text.h"

#include "common/Win32TextCodec.h"

bool EncodeSplitBatchText(std::wstring_view text, std::string& bytes) noexcept
{
    return Win32EncodeText(GetOEMCP(), text.data(), text.size(), bytes).Succeeded();
}

bool DecodeSplitBatchText(std::string_view bytes, std::wstring& text) noexcept
{
    return Win32DecodeText(GetOEMCP(), bytes.data(), bytes.size(), text).Succeeded();
}

bool EscapeSplitBatchArgument(std::string_view bytes, std::string& escaped) noexcept
{
    try
    {
        std::string staged;
        staged.reserve(bytes.size());
        for (const char value : bytes)
        {
            if (value == '%')
                staged.push_back('%');
            staged.push_back(value);
        }
        escaped.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool EscapeSplitBatchEchoText(std::string_view bytes, std::string& escaped) noexcept
{
    try
    {
        std::string staged;
        staged.reserve(bytes.size());
        for (const char value : bytes)
        {
            if (value == '%')
                staged.push_back('%');
            else if (value == '&' || value == '^' || value == '|' || value == '<' || value == '>')
                staged.push_back('^');
            staged.push_back(value);
        }
        escaped.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

namespace
{
bool QueryDateTimePart(bool datePart, const SYSTEMTIME& value, std::wstring& text)
{
    const int required = datePart
                             ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value,
                                              nullptr, nullptr, 0)
                             : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value, nullptr, nullptr, 0);
    if (required <= 0)
        return false;

    std::wstring staged(static_cast<size_t>(required), L'\0');
    const int written = datePart
                            ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value,
                                             nullptr, staged.data(), required)
                            : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value, nullptr,
                                             staged.data(), required);
    if (written <= 0)
        return false;
    staged.resize(static_cast<size_t>(written - 1));
    text.swap(staged);
    return true;
}
} // namespace

bool FormatSplitLocalDateTime(const SYSTEMTIME& value, std::wstring& date,
                              std::wstring& time) noexcept
{
    try
    {
        std::wstring stagedDate;
        std::wstring stagedTime;
        if (!QueryDateTimePart(true, value, stagedDate) ||
            !QueryDateTimePart(false, value, stagedTime))
            return false;
        date.swap(stagedDate);
        time.swap(stagedTime);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
