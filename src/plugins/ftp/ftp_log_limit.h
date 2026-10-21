// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace sally::ftp
{
    inline constexpr std::uint32_t LogKilobyteBytes = 1024;
    // TrimToUtf8Size consumes an unsigned DWORD byte budget, not a signed int.
    inline constexpr std::uint32_t MaximumLogKilobytes =
        (std::numeric_limits<std::uint32_t>::max)() / LogKilobyteBytes;

    inline bool IsValidLogKilobytes(int value) noexcept
    {
        return value > 0 && static_cast<std::uint32_t>(value) <= MaximumLogKilobytes;
    }

    // The persisted unlimited sentinel is handled by the configuration loader.
    // Normalize only enabled limits, without reducing an oversized limit to 1KB.
    inline int NormalizeStoredLogKilobytes(std::uint32_t value) noexcept
    {
        return static_cast<int>((std::max)(1u, (std::min)(value, MaximumLogKilobytes)));
    }

    inline std::uint32_t LogLimitBytes(int kilobytes) noexcept
    {
        return static_cast<std::uint32_t>(NormalizeStoredLogKilobytes(static_cast<std::uint32_t>(kilobytes))) * LogKilobyteBytes;
    }
} // namespace sally::ftp
