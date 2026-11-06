// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// GitHub answers the anonymous release check only so many times an hour for one network address
// (60 when this was written). After that it refuses with 403, or 429, and its headers say when
// requests are accepted again: X-RateLimit-Remaining is 0 and X-RateLimit-Reset is that moment in
// Unix seconds, or Retry-After gives the seconds to wait. Without them the updater showed
// only "HTTP 403".

#pragma once

#include <string>

namespace Sally::Update
{

struct RateLimitAnswer
{
    bool Limited = false;
    unsigned long long RetryAfterSeconds = 0; // 0 when the server did not say
};

namespace detail
{
// Digits only (headers may carry spaces around them); false for anything else or an overflow.
inline bool ParseSeconds(const std::wstring& text, unsigned long long& value)
{
    size_t begin = text.find_first_not_of(L" \t");
    size_t end = text.find_last_not_of(L" \t");
    if (begin == std::wstring::npos)
        return false;
    value = 0;
    for (size_t i = begin; i <= end; ++i)
    {
        const wchar_t ch = text[i];
        if (ch < L'0' || ch > L'9' || value > (~0ULL - 9) / 10)
            return false;
        value = value * 10 + static_cast<unsigned long long>(ch - L'0');
    }
    return true;
}
} // namespace detail

// status: the HTTP status; the header values as received (empty when absent); now: Unix seconds.
inline RateLimitAnswer ReadRateLimit(unsigned status, const std::wstring& remaining, const std::wstring& reset,
                                     const std::wstring& retryAfter, unsigned long long now)
{
    RateLimitAnswer answer;
    unsigned long long left = 1;
    const bool exhausted = detail::ParseSeconds(remaining, left) && left == 0;
    unsigned long long wait = 0;
    const bool told = detail::ParseSeconds(retryAfter, wait);
    // A 403 without these headers is a plain refusal (a proxy, a blocked address), not a limit.
    answer.Limited = status == 429 || (status == 403 && (exhausted || told));
    if (!answer.Limited)
        return answer;
    unsigned long long at = 0;
    if (told)
        answer.RetryAfterSeconds = wait;
    else if (detail::ParseSeconds(reset, at) && at > now)
        answer.RetryAfterSeconds = at - now;
    return answer;
}

// The moment to show for "try again after": now plus the wait, rounded up to the next whole minute
// so a time shown without seconds is never before the server accepts requests again. Both are
// FILETIME-style 100 ns ticks. 0 when the wait is unknown (0) or more than a day away.
inline unsigned long long RetryShownAt(unsigned long long nowTicks, unsigned long long retryAfterSeconds)
{
    constexpr unsigned long long kSecond = 10000000ULL;
    constexpr unsigned long long kMinute = 60 * kSecond;
    if (retryAfterSeconds == 0 || retryAfterSeconds > 24ULL * 60 * 60)
        return 0;
    const unsigned long long at = nowTicks + retryAfterSeconds * kSecond;
    return (at + kMinute - 1) / kMinute * kMinute;
}

} // namespace Sally::Update
