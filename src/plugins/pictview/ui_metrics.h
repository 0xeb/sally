// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

namespace pictview
{

    struct ViewerUiMetrics
    {
        unsigned Dpi = 96;

        int Scale(int pixels) const
        {
            return static_cast<int>((static_cast<int64_t>(pixels) * Dpi + 48) / 96);
        }

        int IconSize() const
        {
            // Match Sally's system-DPI icon buckets, including custom display scales.
            const int scale = Dpi <= 96 ? 100 : Dpi <= 120 ? 125
                                            : Dpi <= 144   ? 150
                                            : Dpi <= 192   ? 200
                                            : Dpi <= 240   ? 250
                                            : Dpi <= 288   ? 300
                                            : Dpi <= 384   ? 400
                                                           : 500;
            return 16 * scale / 100;
        }

        int StatusIconMargin() const
        {
            return IconSize() + Scale(16);
        }
    };

    inline ViewerUiMetrics UiMetricsForDpi(unsigned dpi)
    {
        return {dpi == 0 ? 96 : dpi};
    }

} // namespace pictview
