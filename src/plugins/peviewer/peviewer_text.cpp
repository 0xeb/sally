// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "peviewer_text.h"

#include "common/Win32TextCodec.h"

bool EncodePeviewerReportText(std::wstring_view text, std::string& bytes) noexcept
{
    // PEViewer's generated report remains an active-code-page byte stream for the legacy text
    // viewer. All semantic text stays UTF-16 until this named, exact-or-placeholder boundary.
    //
    // Strict on purpose, and now with a scope that matches the intent: this is the
    // exact form, used where publishing a WRONG identity would be worse than
    // publishing none. gtest_peviewer_text's
    // ReportEncodingRefusesLossWithoutPublishingPartialBytes pins that contract.
    //
    // It is no longer what the report STREAM uses. The review filed the resulting
    // whole-dump suppression as a lost ability, and it was right: one unrepresentable
    // character in a PE's VERSIONINFO discarded a report that was otherwise complete.
    // The comment that stood here asked whether "malformed UTF-16" (what the test
    // exercises) and "valid but not representable in the ACP" (what the review hit)
    // deserve the same answer. They do not, and the report stream is where they part:
    // WriteWide takes EncodePeviewerReportTextLossy below, so a field the code page
    // cannot spell degrades to '?' the way pre-unicode's W2A did, while this exact
    // form keeps refusing for callers that need identity.
    return Win32EncodeText(CP_ACP, text.data(), text.size(), bytes).Succeeded();
}

bool EncodePeviewerReportTextLossy(std::wstring_view text, std::string& bytes) noexcept
{
    // WideCharToMultiByte(CP_ACP, 0, ...) with no WC_NO_BEST_FIT_CHARS and no
    // usedDefaultChar check - which is precisely what the ATL W2A macro this replaced
    // did, per character, for exactly these report fields. A Japanese CompanyName on a
    // CP-1252 machine costs that one field its glyphs, not the whole PE dump.
    return Win32EncodeTextLossy(CP_ACP, text.data(), text.size(), bytes).Succeeded();
}

bool EncodePeviewerResourceName(std::wstring_view name, std::string& bytes) noexcept
{
    if (name.empty())
    {
        std::string empty;
        bytes.swap(empty);
        return true;
    }

    if (EncodePeviewerReportText(name, bytes))
        return true;

    try
    {
        std::string placeholder("?");
        bytes.swap(placeholder);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

namespace
{
bool QueryLocaleText(LCID locale, LCTYPE type, std::wstring& text)
{
    const int required = GetLocaleInfoW(locale, type, nullptr, 0);
    if (required <= 0)
        return false;

    std::wstring staged(static_cast<size_t>(required), L'\0');
    const int written = GetLocaleInfoW(locale, type, staged.data(), required);
    if (written <= 0)
        return false;
    staged.resize(static_cast<size_t>(written - 1));
    text.swap(staged);
    return true;
}

bool QueryDateTimePart(bool date, const SYSTEMTIME& value, std::wstring& text)
{
    const int required = date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value, nullptr, nullptr, 0)
                              : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value, nullptr, nullptr, 0);
    if (required <= 0)
        return false;

    std::wstring staged(static_cast<size_t>(required), L'\0');
    const int written = date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value, nullptr, staged.data(), required)
                             : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value, nullptr, staged.data(), required);
    if (written <= 0)
        return false;
    staged.resize(static_cast<size_t>(written - 1));
    text.swap(staged);
    return true;
}
} // namespace

bool GetPeviewerLocaleLanguageText(LANGID language, std::string& text) noexcept
{
    if (language == 0)
    {
        std::string empty;
        text.swap(empty);
        return true;
    }

    try
    {
        std::wstring wide;
        if (!QueryLocaleText(MAKELCID(language, SORT_DEFAULT), LOCALE_SLANGUAGE, wide))
            return false;
        return EncodePeviewerResourceName(wide, text);
    }
    catch (...)
    {
        return false;
    }
}

bool FormatPeviewerTimestamp(DWORD timestamp, std::string& text) noexcept
{
    if (timestamp == 0 || timestamp == 0xFFFFFFFF)
        return false;

    try
    {
        constexpr ULONGLONG WindowsToUnixEpoch100ns = 0x019DB1DED53E8000ULL;
        const ULONGLONG value = WindowsToUnixEpoch100ns + static_cast<ULONGLONG>(timestamp) * 10000000ULL;
        const FILETIME utc{static_cast<DWORD>(value), static_cast<DWORD>(value >> 32)};
        FILETIME local{};
        SYSTEMTIME system{};
        if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &system))
            return false;

        std::wstring date;
        std::wstring clock;
        if (!QueryDateTimePart(true, system, date) || !QueryDateTimePart(false, system, clock))
            return false;

        std::string staged;
        if (!EncodePeviewerReportText(L" (" + date + L" " + clock + L")", staged))
            return false;
        text.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
