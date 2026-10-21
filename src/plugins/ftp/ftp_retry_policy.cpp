// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "precomp.h"
#include "ftp_retry_policy.h"

bool CFtpRetryPolicy::Remember(std::uint32_t problem, bool enabled) noexcept
{
    try
    {
        if (enabled)
            Remembered.insert(problem);
        else
            Remembered.erase(problem);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
CFtpRetryDecision CFtpRetryPolicy::OnError(int uid, std::uint32_t problem, std::uint32_t now) noexcept
{
    if (Remembered.find(problem) == Remembered.end())
        return CFtpRetryDecision::AskUser;
    try
    {
        auto& count = Attempts[{uid, problem}];
        if (count >= MaxAttempts)
            return CFtpRetryDecision::Exhausted;
        Waiting[uid] = {problem, now + DelayMs};
        ++count;
        return CFtpRetryDecision::Scheduled;
    }
    catch (...)
    {
        return CFtpRetryDecision::NoMemory;
    }
}
std::uint32_t CFtpRetryPolicy::RemainingDelay(int uid, std::uint32_t now) const noexcept
{
    const auto wait = Waiting.find(uid);
    if (wait == Waiting.end())
        return 0;
    // Unsigned elapsed time handles tick wrap and long pauses without turning
    // an expired one-second wait into a positive signed difference of weeks.
    const std::uint32_t elapsed = now - (wait->second.Due - DelayMs);
    return elapsed < DelayMs ? DelayMs - elapsed : 0;
}
void CFtpRetryPolicy::ManualRetry(int uid) noexcept
{
    Waiting.erase(uid);
    auto i = Attempts.lower_bound({uid, 0});
    while (i != Attempts.end() && i->first.first == uid)
        i = Attempts.erase(i);
}
bool FtpIsItemRetryChoice(int dialog, int result) noexcept
{
    if (dialog == 8)
        return result == CM_SIEA_RETRY;
    if (dialog >= 9 && dialog <= 11)
        return result == CM_SISE_RETRY;
    if (dialog == 27 || dialog == 38)
        return result == CM_SATR_RETRY; // IDOK is Use Binary Mode
    return dialog >= 1 && dialog <= 51 && result == IDOK;
}
