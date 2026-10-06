// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <wchar.h>

// Turns a web address as written in a language pack ("example.com/page" or
// "https://example.com/page") into a URL that can be opened. An address that
// already names its scheme is kept as it is; a bare host gets "https://".
inline std::wstring MakeOpenableWebUrl(const wchar_t* address)
{
    if (address == nullptr || *address == L'\0')
        return std::wstring();
    const wchar_t* sep = wcsstr(address, L"://");
    if (sep != nullptr && sep > address)
    {
        bool schemeOnly = true;
        for (const wchar_t* p = address; p < sep; ++p)
        {
            wchar_t c = *p;
            if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (p > address && ((c >= L'0' && c <= L'9') || c == L'+' || c == L'-' || c == L'.'))))
            {
                schemeOnly = false;
                break;
            }
        }
        if (schemeOnly)
            return std::wstring(address);
    }
    return std::wstring(L"https://") + address;
}
