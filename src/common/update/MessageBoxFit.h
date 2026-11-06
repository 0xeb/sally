// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The updater's message boxes take Sally's panel font after Windows has laid them out for its
// own message font. Scaling the layout by the two fonts' average character widths does not
// guarantee that the text still fits: a sentence can be wider in the new font, and its wrapped
// last line would fall outside the text control. This grows the text control to the height its
// text needs and moves the controls below it (the buttons) down by the same amount.

#pragma once

#include <cstddef>
#include <vector>

namespace Sally::Update
{

struct ControlBox
{
    int Left = 0;
    int Top = 0;
    int Right = 0;
    int Bottom = 0;
};

// controls: a message box's controls in its client coordinates; message: the index of its text
// control; neededHeight: the height the text needs at that control's width. Returns how much the
// client area must grow (0 when the text already fits); the controls are updated in place.
inline int FitMessageHeight(std::vector<ControlBox>& controls, std::size_t message, int neededHeight)
{
    if (message >= controls.size())
        return 0;
    ControlBox& text = controls[message];
    const int missing = neededHeight - (text.Bottom - text.Top);
    if (missing <= 0)
        return 0;
    const int below = text.Bottom;
    text.Bottom += missing;
    for (std::size_t i = 0; i < controls.size(); ++i)
    {
        // The icon beside the text stays; what starts at or below the text's end moves down.
        if (i != message && controls[i].Top >= below)
        {
            controls[i].Top += missing;
            controls[i].Bottom += missing;
        }
    }
    return missing;
}

} // namespace Sally::Update
