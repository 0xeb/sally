// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace pictview
{

// Steps of the moving outline ("marching ants"): the hatch repeats every eight pixels.
constexpr int SelectionOutlinePhaseCount = 8;

// Draws the one-pixel selection outline along the inside of `rect` (window
// coordinates of `dc`'s logical space). The outline is an opaque black and white
// hatch, so it shows on any image: on white, black, grey or noise every stretch of
// it has both a black and a white run. `phase` (0..SelectionOutlinePhaseCount-1)
// shifts the hatch along the outline; advancing it animates the outline.
//
// Every pixel of the outline is overwritten, so redrawing it at a new phase over a
// previous one needs no repaint of the image underneath.
void DrawSelectionOutline(HDC dc, const RECT& rect, int phase);

} // namespace pictview
