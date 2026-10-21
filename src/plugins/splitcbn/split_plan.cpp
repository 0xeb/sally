// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "split_plan.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sally::split
{
    uint64_t PartsForSize(uint64_t total, uint64_t partBytes)
    {
        return partBytes == 0 ? 0 : total / partBytes + (total % partBytes != 0);
    }
    uint64_t LastPartForSize(uint64_t total, uint64_t partBytes)
    {
        if (total == 0 || partBytes == 0)
            return 0;
        const uint64_t remainder = total % partBytes;
        return remainder != 0 ? remainder : (std::min)(total, partBytes);
    }
    bool Plan::Valid() const
    {
        if (Total == 0)
            return false;
        switch (Kind)
        {
        case Mode::Size:
            return PartBytes != 0 && PartCount() < static_cast<uint64_t>((std::numeric_limits<int>::max)());
        case Mode::Count:
            return Count != 0 && Count <= Total && Count < static_cast<unsigned>((std::numeric_limits<int>::max)());
        case Mode::Automatic:
            return true;
        }
        return false;
    }
    uint64_t Plan::PartCount() const
    {
        return Kind == Mode::Size ? PartsForSize(Total, PartBytes) : Kind == Mode::Count ? Count
                                                                                         : 0;
    }
    uint64_t Plan::Wanted(unsigned index, uint64_t remaining) const
    {
        if (Kind == Mode::Count)
            return Count != 0 && index < Count ? Total / Count + (index < Total % Count) : 0;
        return Kind == Mode::Size ? (std::min)(PartBytes, remaining) : 0;
    }
    bool ParsePartBytes(double number, uint32_t multiplier, uint64_t& bytes)
    {
        const double scaled = number * multiplier;
        // The upper bound is exactly 2^64, unlike double(UINT64_MAX), which
        // rounds to that value and cannot safely be cast back to uint64_t.
        if (!std::isfinite(scaled) || scaled < 1.0 || scaled >= 18446744073709551616.0)
            return false;
        bytes = static_cast<uint64_t>(scaled);
        return true;
    }
    bool ParsePartCount(std::wstring_view text, uint64_t total, unsigned& count, bool& clamped)
    {
        if (text.empty())
            return false;
        unsigned requested = 0;
        for (wchar_t character : text)
        {
            if (character < L'0' || character > L'9')
                return false;
            const unsigned digit = static_cast<unsigned>(character - L'0');
            if (requested > ((std::numeric_limits<unsigned>::max)() - digit) / 10)
                return false;
            requested = requested * 10 + digit;
        }
        if (requested == 0)
            return false;
        // Preserve the dialog's minimum-two-byte policy and keep the ordinal
        // representable by the existing part-name/batch-file counters.
        const auto maximum = static_cast<unsigned>((std::min)(total / 2,
                                                              static_cast<uint64_t>((std::numeric_limits<int>::max)() - 1)));
        if (maximum == 0)
            return false;
        count = (std::min)(requested, maximum);
        clamped = count != requested;
        return true;
    }
} // namespace sally::split
