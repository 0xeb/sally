// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace sally::configuration
{
enum class LongPathState { Unsupported, Disabled, Enabled, Unknown };
enum class LongPathValue { Missing, Dword, Invalid, Unreadable };
enum class LongPathChangeResult { Pending, Cancelled, Failed, Verified };

inline LongPathState ClassifyLongPathSetting(bool supported, LongPathValue kind, std::uint32_t value)
{
    if (!supported) return LongPathState::Unsupported;
    if (kind == LongPathValue::Missing) return LongPathState::Disabled;
    if (kind != LongPathValue::Dword || value > 1) return LongPathState::Unknown;
    return value == 1 ? LongPathState::Enabled : LongPathState::Disabled;
}

// Windows state is observed, never part of Sally's Configuration draft/profile.
struct LongPathSettingModel
{
    LongPathState State = LongPathState::Unknown;
    bool Busy = false;
    bool RequestedEnabled = true;

    bool CanChange() const { return (State == LongPathState::Disabled || State == LongPathState::Enabled) && !Busy; }
    bool Begin(bool confirmed, bool enabled = true)
    {
        if (!confirmed || !CanChange() || (State == LongPathState::Enabled) == enabled) return false;
        RequestedEnabled = enabled;
        Busy = true;
        return true;
    }
    LongPathChangeResult Finish(bool cancelled, bool processSucceeded, LongPathState observed)
    {
        Busy = false;
        State = observed;
        if (cancelled) return LongPathChangeResult::Cancelled;
        if (!processSucceeded || State != (RequestedEnabled ? LongPathState::Enabled : LongPathState::Disabled)) return LongPathChangeResult::Failed;
        return LongPathChangeResult::Verified;
    }
};
}
