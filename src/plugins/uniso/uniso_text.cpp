// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "uniso_text.h"

#include "common/Win32TextCodec.h"

bool DecodeUnisoLegacyText(std::string_view bytes, std::wstring& text) noexcept
{
    // ISO 9660, Xbox and NRG metadata is a byte string with no recorded encoding, so the active ANSI
    // code page is a guess, not a contract. Pre-unicode never decoded at all - it handed the raw
    // bytes to the panel and let the user read whatever they rendered as. Refusing here instead
    // aborts the listing of the whole image over one odd byte in one name, which loses the ability
    // to open the image at all. Degrade the name; keep the image browsable.
    return Win32DecodeAcpPermissive(bytes.data(), bytes.size(), text).Succeeded();
}

bool EncodeUnisoReportText(std::wstring_view text, std::string& bytes) noexcept
{
    // The generated .txt viewer file is consumed by Sally's legacy text viewer using the active ANSI
    // code page, so the byte contract stays explicit. It is a REPORT, though - text that is read on
    // screen and never used to name or reopen anything - so a character the code page cannot spell
    // becomes that code page's default character. Refusing produced no report at all, misreported as
    // "Creation of temporary file has failed."
    return Win32EncodeAcpLossy(text.data(), text.size(), bytes).Succeeded();
}

bool CopyUnisoReportField(std::string_view bytes, std::string& field) noexcept
{
    try
    {
        const size_t nul = bytes.find('\0');
        std::string staged(bytes.substr(0, nul));
        while (!staged.empty() && staged.back() == ' ')
            staged.pop_back();
        field.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

namespace
{
bool QueryUnisoLocalePart(bool date, const SYSTEMTIME& time, std::wstring& text)
{
    const int required = date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr, nullptr, 0)
                              : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &time, nullptr, nullptr, 0);
    if (required <= 0)
        return false;

    std::wstring staged(static_cast<size_t>(required), L'\0');
    const int written = date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr, staged.data(), required)
                             : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &time, nullptr, staged.data(), required);
    if (written <= 0)
        return false;
    staged.resize(static_cast<size_t>(written - 1));
    text.swap(staged);
    return true;
}
}

bool FormatUnisoReportSystemTime(const SYSTEMTIME& time, std::string& text) noexcept
{
    try
    {
        std::wstring date;
        if (!QueryUnisoLocalePart(true, time, date))
            date = std::to_wstring(time.wDay) + L"." + std::to_wstring(time.wMonth) + L"." + std::to_wstring(time.wYear);

        std::wstring clock;
        if (!QueryUnisoLocalePart(false, time, clock))
        {
            clock = std::to_wstring(time.wHour) + L":";
            if (time.wMinute < 10)
                clock += L'0';
            clock += std::to_wstring(time.wMinute) + L":";
            if (time.wSecond < 10)
                clock += L'0';
            clock += std::to_wstring(time.wSecond);
        }

        std::string staged;
        if (!EncodeUnisoReportText(date + L" " + clock, staged))
            return false;
        text.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
