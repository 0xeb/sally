// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Sally::Shell
{

// Sally passes context-menu commands a short-lived window of its own as their parent. Some
// commands keep working after InvokeCommand returns, and stop as soon as that window goes away:
// Send To > Compressed (zipped) folder starts the archive and deletes it again. So the window is
// hidden and parked when the invoke returns instead of being destroyed, and parked windows are
// released later: the oldest ones above 'maxCount', any older than 'maxAgeMs', and all of them
// when the main window closes.
inline constexpr std::size_t MaxParkedShellParents = 16;
inline constexpr std::uint64_t MaxParkedShellParentAgeMs = 60ull * 60ull * 1000ull; // an hour

// The positions in 'parkedAt' (tick counts, oldest first) to release at 'now'.
inline std::vector<std::size_t> ParkedShellParentsToRelease(const std::vector<std::uint64_t>& parkedAt,
                                                            std::uint64_t now,
                                                            std::size_t maxCount = MaxParkedShellParents,
                                                            std::uint64_t maxAgeMs = MaxParkedShellParentAgeMs)
{
    std::vector<std::size_t> release;
    std::size_t kept = parkedAt.size();
    for (std::size_t i = 0; i < parkedAt.size(); i++)
    {
        const bool tooOld = now >= parkedAt[i] && now - parkedAt[i] > maxAgeMs;
        if (tooOld || kept > maxCount)
        {
            release.push_back(i);
            kept--;
        }
    }
    return release;
}

} // namespace Sally::Shell
