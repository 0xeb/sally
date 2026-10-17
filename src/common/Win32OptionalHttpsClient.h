// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "update/HttpsClient.h"
#include <atomic>

namespace Sally::Update
{
    // An independent optional-content session; every request shares this monotonic deadline.
    class Win32OptionalHttpsClient final : public IHttpsClient
    {
    public:
        explicit Win32OptionalHttpsClient(const std::atomic<bool>& cancel, ULONGLONG deadline) : Cancel(cancel), Deadline(deadline) {}
        bool Get(const std::wstring& url, const wchar_t* headers, unsigned long long maxBytes,
                 const HttpsSink& sink, HttpsFailure& failure) override;

    private:
        const std::atomic<bool>& Cancel;
        ULONGLONG Deadline;
    };
} // namespace Sally::Update
