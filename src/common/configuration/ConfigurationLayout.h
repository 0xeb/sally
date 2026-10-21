// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>

namespace sally::configuration
{
    struct PropertyLayout
    {
        int NavigationWidth;
        int ButtonWidth;
        int ButtonHeight;
        int ButtonColumns;
        int ButtonRows;
        int FooterHeight;
    };

    // The navigation column must leave usable space for the page. Actions wrap
    // together when their translated captions/font no longer fit one row.
    inline PropertyLayout FitPropertyLayout(int clientWidth, int preferredNavigation,
                                            int buttonCount, int buttonWidth, int buttonHeight,
                                            int buttonGap, int marginX, int marginY, int gripWidth, int textHeight = 0)
    {
        const int available = (std::max)(1, clientWidth - 2 * marginX - gripWidth / 2);
        const int width = (std::min)((std::max)(1, buttonWidth), available);
        const int columns = (std::max)(1, (std::min)(buttonCount, (available + buttonGap) / (width + buttonGap)));
        const int rows = (buttonCount + columns - 1) / columns;
        if (rows > 1 && textHeight > 0)
            buttonHeight = (std::min)(buttonHeight, textHeight + (std::max)(8, marginY / 2));
        const int navigation = (std::max)(1, (std::min)(preferredNavigation, (clientWidth - 3 * marginX) / 3));
        return {navigation, width, buttonHeight, columns, rows,
                rows * buttonHeight + (rows - 1) * buttonGap + 2 * marginY};
    }

    inline int FitNavigationHeader(int preferred, int available)
    {
        return (std::max)(0, (std::min)(preferred, (std::max)(0, available) * 2 / 3));
    }
} // namespace sally::configuration
