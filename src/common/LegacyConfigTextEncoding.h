// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cwchar>
#include <string>
#include <string_view>

namespace sally::legacy_config
{

// Pre-Unicode Sally configuration stored some REG_SZ payloads as process-ACP bytes even though
// the registry type itself was textual. Keep that malformed historical representation confined
// to this import adapter; current semantic ownership and newly written values are UTF-16.
inline bool DecodeAcp(const char* bytes, size_t length, std::wstring& text) noexcept
{
    return static_cast<bool>(Win32DecodeTextPermissive(GetACP(), bytes, length, text));
}

inline bool ParseDecimal(std::wstring_view token, long& value) noexcept
{
    if (token.empty())
        return false;

    size_t pos = 0;
    bool negative = false;
    if (token[pos] == L'+' || token[pos] == L'-')
    {
        negative = token[pos] == L'-';
        if (++pos == token.size())
            return false;
    }

    unsigned long magnitude = 0;
    const unsigned long limit = negative ? static_cast<unsigned long>(LONG_MAX) + 1UL
                                         : static_cast<unsigned long>(LONG_MAX);
    for (; pos < token.size(); ++pos)
    {
        if (token[pos] < L'0' || token[pos] > L'9')
            return false;
        const unsigned digit = static_cast<unsigned>(token[pos] - L'0');
        if (magnitude > (limit - digit) / 10UL)
            return false;
        magnitude = magnitude * 10UL + digit;
    }

    if (negative && magnitude == static_cast<unsigned long>(LONG_MAX) + 1UL)
        value = LONG_MIN;
    else
        value = negative ? -static_cast<long>(magnitude) : static_cast<long>(magnitude);
    return true;
}

inline bool ParseLogFont(std::wstring_view text, LOGFONTW& font) noexcept
{
    try
    {
        std::wstring_view fields[9];
        size_t start = 0;
        for (size_t i = 0; i < 9; ++i)
        {
            const size_t comma = text.find(L',', start);
            if (i != 8 && comma == std::wstring_view::npos)
                return false;
            if (i == 8 && comma != std::wstring_view::npos)
                return false;
            const size_t end = comma == std::wstring_view::npos ? text.size() : comma;
            fields[i] = text.substr(start, end - start);
            start = end + 1;
        }

        LOGFONTW candidate = {};
        candidate.lfHeight = -10;
        candidate.lfWeight = FW_NORMAL;
        candidate.lfCharSet = DEFAULT_CHARSET;
        candidate.lfOutPrecision = OUT_DEFAULT_PRECIS;
        candidate.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        candidate.lfQuality = DEFAULT_QUALITY;
        candidate.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;

        const size_t faceLength = (std::min)(fields[0].size(), static_cast<size_t>(LF_FACESIZE - 1));
        if (faceLength != 0)
            std::wmemcpy(candidate.lfFaceName, fields[0].data(), faceLength);
        candidate.lfFaceName[faceLength] = L'\0';

        long values[8];
        for (size_t i = 0; i < 8; ++i)
            if (!ParseDecimal(fields[i + 1], values[i]))
                return false;

        if (values[0] < INT_MIN || values[0] > INT_MAX ||
            values[1] < INT_MIN || values[1] > INT_MAX)
        {
            return false;
        }
        for (size_t i = 2; i < 8; ++i)
            if (values[i] < 0 || values[i] > UCHAR_MAX)
                return false;

        candidate.lfHeight = static_cast<LONG>(values[0]);
        candidate.lfWeight = static_cast<LONG>(values[1]);
        candidate.lfItalic = static_cast<BYTE>(values[2]);
        candidate.lfCharSet = static_cast<BYTE>(values[3]);
        candidate.lfOutPrecision = static_cast<BYTE>(values[4]);
        candidate.lfClipPrecision = static_cast<BYTE>(values[5]);
        candidate.lfQuality = static_cast<BYTE>(values[6]);
        candidate.lfPitchAndFamily = static_cast<BYTE>(values[7]);
        font = candidate;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

inline bool FormatLogFont(const LOGFONTW& font, std::wstring& text) noexcept
{
    try
    {
        std::wstring candidate(font.lfFaceName);
        candidate.push_back(L',');
        candidate += std::to_wstring(font.lfHeight);
        candidate.push_back(L',');
        candidate += std::to_wstring(font.lfWeight);
        candidate.push_back(L',');
        candidate += std::to_wstring(font.lfItalic);
        candidate.push_back(L',');
        candidate += std::to_wstring(font.lfCharSet);
        candidate.push_back(L',');
        candidate += std::to_wstring(font.lfOutPrecision);
        candidate.push_back(L',');
        candidate += std::to_wstring(font.lfClipPrecision);
        candidate.push_back(L',');
        candidate += std::to_wstring(font.lfQuality);
        candidate.push_back(L',');
        candidate += std::to_wstring(font.lfPitchAndFamily);
        text.swap(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace sally::legacy_config
