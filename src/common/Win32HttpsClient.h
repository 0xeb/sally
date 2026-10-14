// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// IHttpsClient over WinINet: the system's proxy settings, HTTPS only, no cache, no UI.
// Linked into utils\salupdate.exe only; sally.exe never goes online for updates.

#pragma once

#include "update/HttpsClient.h"

#include <atomic>

namespace Sally::Update
{

class Win32HttpsClient final : public IHttpsClient
{
public:
    // cancel, when given, is polled between reads: true stops the download.
    explicit Win32HttpsClient(const std::atomic<bool>* cancel = nullptr) : Cancel(cancel) {}

    bool Get(const std::wstring& url, const wchar_t* headers, unsigned long long maxBytes, const HttpsSink& sink,
             HttpsFailure& failure) override;

private:
    const std::atomic<bool>* Cancel;
};

// The description of a system or WinINet error code, without the trailing line break.
std::wstring Win32NetworkErrorText(DWORD error, LANGID language);

} // namespace Sally::Update
