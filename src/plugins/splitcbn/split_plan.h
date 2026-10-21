// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <string_view>

namespace sally::split
{
    enum class Mode
    {
        Size,
        Count,
        Automatic
    };
    struct Plan
    {
        Mode Kind = Mode::Size;
        uint64_t Total = 0;
        uint64_t PartBytes = 0;
        unsigned Count = 0;
        bool Valid() const;
        uint64_t PartCount() const;
        uint64_t Wanted(unsigned zeroBasedPart, uint64_t remaining) const;
    };
    uint64_t PartsForSize(uint64_t total, uint64_t partBytes);
    uint64_t LastPartForSize(uint64_t total, uint64_t partBytes);
    bool ParsePartBytes(double number, uint32_t multiplier, uint64_t& bytes);
    bool ParsePartCount(std::wstring_view text, uint64_t total, unsigned& count, bool& clamped);
} // namespace sally::split
