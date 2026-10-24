// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <functional>
#include <vector>

namespace sally::configuration
{
    struct LayoutRect
    {
        int X, Y, Width, Height;
    };

    struct LayoutControl
    {
        int Id;
        LayoutRect Bounds;
    };

    // Plans use immutable, font-scaled resource geometry. Never use the last
    // resized rectangle as the input to the next resize.
    inline LayoutRect AnchorControl(LayoutRect original, int extraWidth, int extraHeight,
                                    bool stretch, bool right, bool grow, bool below)
    {
        if (stretch) original.Width = (std::max)(1, original.Width + extraWidth);
        else if (right) original.X += extraWidth;
        if (grow) original.Height = (std::max)(1, original.Height + extraHeight);
        else if (below) original.Y += extraHeight;
        return original;
    }

    // A small UI-free row planner. The adapter supplies translated-caption
    // measurements using the current font.
    class PageRows
    {
        std::function<int(int, int)> Measure;
        int Width, Gap, Line, Button, Y;
    public:
        std::vector<LayoutControl> Controls;
        PageRows(int width, int gap, int line, int button, std::function<int(int, int)> measure)
            : Measure(measure), Width((std::max)(1, width)), Gap(gap), Line(line), Button(button), Y(gap) {}
        int Position() const { return Y; }
        int ContentHeight() const { return Y + Gap; }
        void Space() { Y += Gap; }
        void Place(int id, int x, int y, int width, int height)
        {
            Controls.push_back({id, {x, y, (std::max)(1, width), (std::max)(1, height)}});
        }
        void Text(int id, bool check = false)
        {
            const int height = (std::max)(check ? Button : Line, Measure(id, Width - 2 * Gap));
            Place(id, Gap, Y, Width - 2 * Gap, height);
            Y += height + Gap;
        }
        void Field(int label, int field, int trailing = 0)
        {
            Text(label);
            const int suffix = trailing != 0 ? Button + Gap : 0;
            Place(field, Gap, Y, Width - 2 * Gap - suffix, Button);
            if (trailing != 0) Place(trailing, Width - Gap - Button, Y, Button, Button);
            Y += Button + Gap;
        }
        void Fields(int firstLabel, int first, int secondLabel, int second)
        {
            const int half = (Width - 3 * Gap) / 2;
            const int height = (std::max)({Line, Measure(firstLabel, half), Measure(secondLabel, half)});
            Place(firstLabel, Gap, Y, half, height);
            Place(secondLabel, 2 * Gap + half, Y, half, height);
            Y += height + Gap;
            Place(first, Gap, Y, half, Button);
            Place(second, 2 * Gap + half, Y, half, Button);
            Y += Button + Gap;
        }
        void Checks(const std::vector<int>& ids, int columns)
        {
            const int width = (Width - (columns + 1) * Gap) / columns;
            for (std::size_t first = 0; first < ids.size(); first += columns)
            {
                int height = Button;
                for (int column = 0; column < columns && first + column < ids.size(); ++column)
                    height = (std::max)(height, Measure(ids[first + column], width));
                for (int column = 0; column < columns && first + column < ids.size(); ++column)
                    Place(ids[first + column], Gap + column * (width + Gap), Y, width, height);
                Y += height + Gap;
            }
        }
        void ButtonField(int button, int field, int buttonWidth)
        {
            const int height = (std::max)(Button, Measure(button, buttonWidth));
            Place(button, Gap, Y, buttonWidth, height);
            Place(field, 2 * Gap + buttonWidth, Y, Width - 3 * Gap - buttonWidth, height);
            Y += height + Gap;
        }
        void Color(int label, int button, int buttonWidth)
        {
            const int textWidth = Width - 3 * Gap - buttonWidth;
            const int height = (std::max)(Button, Measure(label, textWidth));
            Place(label, Gap, Y, textWidth, height);
            Place(button, Width - Gap - buttonWidth, Y, buttonWidth, height);
            Y += height + Gap;
        }
        void List(int label, int list, int height)
        {
            // CEditListBox's toolbar is a CHILD of the header. Its rectangle
            // itself must be tall enough; blank space below cannot prevent
            // child clipping.
            Place(label, Gap, Y, Width - 2 * Gap, Button);
            Y += Button;
            Place(list, Gap, Y, Width - 2 * Gap, height);
            Y += height + Gap;
        }
        void FillHeight(int list, int available)
        {
            const int extra = (std::max)(0, available - ContentHeight());
            for (auto& control : Controls)
                if (control.Id == list)
                {
                    const int bottom = control.Bounds.Y + control.Bounds.Height;
                    control.Bounds.Height += extra;
                    for (auto& following : Controls)
                        if (following.Id != list && following.Bounds.Y >= bottom) following.Bounds.Y += extra;
                    Y += extra;
                    break;
                }
        }
    };
} // namespace sally::configuration
