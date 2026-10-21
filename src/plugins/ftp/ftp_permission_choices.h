// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sally::ftp
{
    struct PermissionMasks
    {
        std::uint32_t And = 0777;
        std::uint32_t Or = 0;
    };

    class PermissionChoices
    {
    public:
        inline static constexpr std::array<std::uint32_t, 9> Bits = {0400, 0200, 0100, 0040, 0020, 0010, 0004, 0002, 0001};
        // Native three-state controls: 0 clears, 1 sets, 2 preserves the bit.
        std::array<int, 9> States{};

        PermissionChoices(std::uint32_t attributes, std::uint32_t differences)
        {
            const bool unknown = attributes == (std::numeric_limits<std::uint32_t>::max)();
            for (std::size_t i = 0; i < Bits.size(); ++i)
                States[i] = unknown || (differences & Bits[i]) ? 2 : ((attributes & Bits[i]) != 0);
        }

        PermissionMasks Masks() const
        {
            PermissionMasks result;
            for (std::size_t i = 0; i < Bits.size(); ++i)
            {
                if (States[i] == 0)
                    result.And &= ~Bits[i];
                else if (States[i] == 1)
                    result.Or |= Bits[i];
            }
            return result;
        }
    };
} // namespace sally::ftp
