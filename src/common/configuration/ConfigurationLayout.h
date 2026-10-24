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

    // Saved heights are outer pixels. A preferred viewport is independent of
    // oversized page content; only the true drag minimum constrains a saved size.
    inline int FitOpeningHeight(unsigned long saved, int preferred, int minimum, int workArea)
    {
        const auto requested = saved == 0 ? static_cast<unsigned long>((std::max)(0, preferred)) : saved;
        return (std::max)(0, (std::min)(workArea,
            (std::max)(minimum, static_cast<int>((std::min)(requested, static_cast<unsigned long>((std::max)(0, workArea)))))));
    }

    struct PageExtent { int Width, Height; };
    struct ViewportLayout
    {
        PageExtent View, Content;
        bool Horizontal, Vertical;
    };

    // Measure without moving controls or changing live scrollbar ranges. Starting
    // with no bars finds the least fixed point, including after a narrow->wide resize.
    template<class Measure>
    ViewportLayout FitPageViewport(PageExtent outer, PageExtent bars, Measure measure)
    {
        ViewportLayout result = {outer, outer, false, false};
        for (int pass = 0; pass != 4; ++pass)
        {
            result.View = {(std::max)(1, outer.Width - (result.Vertical ? bars.Width : 0)),
                           (std::max)(1, outer.Height - (result.Horizontal ? bars.Height : 0))};
            result.Content = measure(result.View);
            const bool horizontal = result.Horizontal || result.Content.Width > result.View.Width;
            const bool vertical = result.Vertical || result.Content.Height > result.View.Height;
            if (horizontal == result.Horizontal && vertical == result.Vertical) break;
            result.Horizontal = horizontal;
            result.Vertical = vertical;
        }
        return result;
    }

    inline int FitNavigationHeader(int preferred, int available)
    {
        return (std::max)(0, (std::min)(preferred, (std::max)(0, available) * 2 / 3));
    }
} // namespace sally::configuration
