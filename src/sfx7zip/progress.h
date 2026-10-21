// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SALLY_SFX_PROGRESS_H
#define SALLY_SFX_PROGRESS_H

static unsigned long long SfxProgressAdd(unsigned long long total, unsigned long long amount)
{
    return ~0ULL - total < amount ? ~0ULL : total + amount;
}

static unsigned SfxProgressPosition(unsigned long long total, unsigned long long current, int writing)
{
    unsigned percent = 0;
    if (total != 0)
    {
        if (current >= total)
            percent = 100;
        else
        {
            unsigned i;
            unsigned long long remainder = 0;
            // Compute floor(current * 100 / total) without overflowing either
            // multiplication or addition, even at the 64-bit limit.
            for (i = 0; i < 100; ++i)
            {
                if (remainder >= total - current)
                {
                    remainder -= total - current;
                    ++percent;
                }
                else
                    remainder += current;
            }
        }
    }
    return percent + (writing != 0 ? 100 : 0);
}

#endif
