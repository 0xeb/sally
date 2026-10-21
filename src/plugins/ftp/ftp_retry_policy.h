// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <utility>

using CFtpRetryClock = std::uint32_t (*)();
enum class CFtpRetryDecision
{
    AskUser,
    Scheduled,
    Exhausted,
    NoMemory
};

// Owned by one operation queue. Stable item identity, rather than worker
// ownership, preserves budgets and deadlines through requeue/reassignment.
class CFtpRetryPolicy
{
    struct CWait
    {
        std::uint32_t Problem, Due;
    };
    std::set<std::uint32_t> Remembered;
    std::map<std::pair<int, std::uint32_t>, unsigned> Attempts;
    std::map<int, CWait> Waiting;

public:
    static constexpr unsigned MaxAttempts = 3;
    static constexpr std::uint32_t DelayMs = 1000;
    bool Remember(std::uint32_t problem, bool enabled) noexcept;
    bool IsRemembered(std::uint32_t problem) const noexcept { return Remembered.find(problem) != Remembered.end(); }
    CFtpRetryDecision OnError(int uid, std::uint32_t problem, std::uint32_t now) noexcept;
    std::uint32_t RemainingDelay(int uid, std::uint32_t now) const noexcept;
    void ManualRetry(int uid) noexcept;
};
bool FtpIsItemRetryChoice(int dialog, int result) noexcept;
