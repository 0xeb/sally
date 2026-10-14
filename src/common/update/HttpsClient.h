// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// What the updater needs from the network: one HTTPS GET at a time, its body handed over in
// pieces. common/Win32HttpsClient.h is the real one; only utils\salupdate.exe goes online.

#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace Sally::Update
{

struct HttpsFailure
{
    DWORD Code = 0;        // a system or WinINet error, 0 for the other cases
    unsigned Status = 0;   // the HTTP status when the server answered with an error
    bool TooLarge = false; // the body is longer than allowed
    bool Cancelled = false;
    std::wstring Text; // the system's description of Code, or an English explanation
};

// Receives the body; false stops the download (error: why, a system error code).
using HttpsSink = std::function<bool(const BYTE* data, DWORD size, DWORD& error)>;

class IHttpsClient
{
public:
    virtual ~IHttpsClient() = default;
    // GET an https:// address. headers are extra request headers, each "Name: value" and a
    // line break, or NULL. At most maxBytes are taken; a longer body fails with TooLarge. Redirects are
    // followed only to https:// addresses.
    virtual bool Get(const std::wstring& url, const wchar_t* headers, unsigned long long maxBytes,
                     const HttpsSink& sink, HttpsFailure& failure) = 0;
};

inline bool IsHttpsUrl(const std::wstring& url)
{
    return url.size() > 8 && CompareStringOrdinal(url.c_str(), 8, L"https://", 8, TRUE) == CSTR_EQUAL;
}

} // namespace Sally::Update
