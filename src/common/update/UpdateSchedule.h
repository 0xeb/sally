// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// When Sally looks for a newer release by itself, and what it takes over from the CheckVer
// plugin, which did this up to 1.0.36. Times are UTC FILETIME values (100 ns units).
//
// Sally keeps the time of the last check that reached GitHub, and a time before which it does not
// try again: an hour while a check is under way (so that two Sallys starting together do not
// both check), a day after GitHub could not be reached.

#pragma once

#include <windows.h>

#include <string>

namespace Sally::Update
{

// The values of Configuration > General > Check for updates; the same as CheckVer's
// AutoCheckMode, which they replace.
enum UpdateCheckMode : DWORD
{
    kCheckNever = 0,
    kCheckDaily = 1,
    kCheckWeekly = 2,
    kCheckMonthly = 3,
    kCheckQuarterly = 4,
    kCheckHalfYearly = 5,
};

constexpr DWORD kDefaultUpdateCheckMode = kCheckWeekly;
constexpr ULONGLONG kFileTimeHour = 36000000000ull;
constexpr ULONGLONG kFileTimeDay = 24 * kFileTimeHour;

// 0 for Never or an unknown value.
inline ULONGLONG CheckInterval(DWORD mode)
{
    switch (mode)
    {
    case kCheckDaily:
        return kFileTimeDay;
    case kCheckWeekly:
        return 7 * kFileTimeDay;
    case kCheckMonthly:
        return 30 * kFileTimeDay;
    case kCheckQuarterly:
        return 3 * 30 * kFileTimeDay;
    case kCheckHalfYearly:
        return 6 * 30 * kFileTimeDay;
    default:
        return 0;
    }
}

inline bool IsCheckDue(DWORD mode, ULONGLONG lastCheck, ULONGLONG notBefore, ULONGLONG now)
{
    const ULONGLONG interval = CheckInterval(mode);
    if (interval == 0)
        return false;
    if (notBefore != 0 && now < notBefore && notBefore - now <= kFileTimeDay)
        return false; // a later "not before" is a clock that went back: not trusted
    // A last check in the future is a clock that went back too: check.
    return lastCheck == 0 || lastCheck > now || now - lastCheck >= interval;
}

// What the CheckVer plugin had stored (Plugins Configuration\CHECKVER).
struct CheckVerSettings
{
    bool Present = false;        // the key exists
    bool RemovedByUser = false;  // the user removed the plugin in the Plugins Manager
    DWORD Version = 0;           // CheckVer read its values only from version 5 on
    bool HasAutoCheckMode = false;
    DWORD AutoCheckMode = 0;
    std::wstring ReleaseFeedUrl; // a mirror, set by hand; HTTPS only
    ULONGLONG NextOpen = 0;      // when CheckVer would have opened next (UTC); 0: at the next start
};

struct MigratedUpdateSettings
{
    DWORD Mode = kDefaultUpdateCheckMode;
    ULONGLONG LastCheck = 0; // so that the first check is due when CheckVer's would have been
    std::wstring FeedUrl;
};

// CheckVer's schedule carries over as it was: its "open without connecting" option is not taken
// over (the update window opens only when there is a newer release). A plugin the user removed
// had no schedule: Never. No CheckVer at all: the default.
inline MigratedUpdateSettings MigrateCheckVerSettings(const CheckVerSettings& old)
{
    MigratedUpdateSettings migrated;
    if (old.RemovedByUser)
    {
        migrated.Mode = kCheckNever;
        return migrated;
    }
    if (!old.Present)
        return migrated;
    // Before version 5, CheckVer used its defaults: monthly.
    migrated.Mode = old.Version >= 5 && old.HasAutoCheckMode ? old.AutoCheckMode : kCheckMonthly;
    if (CheckInterval(migrated.Mode) == 0)
        migrated.Mode = kCheckNever;
    const ULONGLONG interval = CheckInterval(migrated.Mode);
    if (old.NextOpen > interval)
        migrated.LastCheck = old.NextOpen - interval;
    if (old.ReleaseFeedUrl.size() > 8 &&
        CompareStringOrdinal(old.ReleaseFeedUrl.c_str(), 8, L"https://", 8, TRUE) == CSTR_EQUAL)
        migrated.FeedUrl = old.ReleaseFeedUrl;
    return migrated;
}

} // namespace Sally::Update
