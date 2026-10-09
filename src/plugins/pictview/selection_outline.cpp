// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "selection_outline.h"

namespace pictview
{

namespace
{

// 8x8 monochrome hatch, four pixels on and four off along each row, each row
// shifted by one: a diagonal stripe, the pattern the old PictView used. Rows of a
// monochrome bitmap are WORD aligned.
constexpr WORD HatchRows[8] = {0x000F, 0x001E, 0x003C, 0x0078, 0x00F0, 0x00E1, 0x00C3, 0x0087};

} // namespace

void DrawSelectionOutline(HDC dc, const RECT& rect, int phase)
{
    if (dc == nullptr || rect.right <= rect.left || rect.bottom <= rect.top)
        return;

    HBITMAP hatch = CreateBitmap(8, 8, 1, 1, HatchRows);
    if (hatch == nullptr)
        return;
    HBRUSH brush = CreatePatternBrush(hatch);
    if (brush == nullptr)
    {
        DeleteObject(hatch);
        return;
    }

    // A monochrome pattern paints its 0 bits in the text colour and its 1 bits in
    // the background colour: both are set, so the outline is opaque.
    const COLORREF oldText = SetTextColor(dc, RGB(0, 0, 0));
    const COLORREF oldBack = SetBkColor(dc, RGB(255, 255, 255));

    // The brush origin is in device units. Anchor the hatch to the logical origin,
    // so a frame drawn into an offset back buffer and one drawn straight to the
    // window line up, then shift it by the phase.
    POINT viewportOrigin = {};
    GetViewportOrgEx(dc, &viewportOrigin);
    const int step = ((phase % SelectionOutlinePhaseCount) + SelectionOutlinePhaseCount) % SelectionOutlinePhaseCount;
    POINT oldOrigin = {};
    SetBrushOrgEx(dc, (viewportOrigin.x + step) & 7, viewportOrigin.y & 7, &oldOrigin);

    FrameRect(dc, &rect, brush);

    SetBrushOrgEx(dc, oldOrigin.x, oldOrigin.y, nullptr);
    SetBkColor(dc, oldBack);
    SetTextColor(dc, oldText);
    DeleteObject(brush);
    DeleteObject(hatch);
}

} // namespace pictview
