// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_SAL_COLORS_STANDALONE
#include "precomp.h"
#endif
#include "sal_colors.h"
#include "sal_color_ids.h"

COLORREF ResolvePanelColor(int colorIndex, DWORD storedColor, bool darkMode, bool customScheme)
{
    constexpr DWORD defaultFlag = 0x01000000;
    const COLORREF storedRgb = storedColor & 0x00ffffff;
    if (!darkMode || (customScheme && (storedColor & defaultFlag) == 0))
        return storedRgb;
    switch (colorIndex)
    {
    case ITEM_FG_SELECTED:
    case ITEM_FG_FOCSEL:
    case FOCUS_ACTIVE_SELECTED:
    case FOCUS_FG_INACTIVE_SELECTED:
    case ICON_BLEND_SELECTED:
    case ICON_BLEND_FOCSEL:
    case THUMBNAIL_FRAME_SELECTED:
    case THUMBNAIL_FRAME_FOCSEL:
        return RGB(255, 209, 102);
    case ITEM_BK_SELECTED:
    case FOCUS_BK_INACTIVE_SELECTED:
        return RGB(43, 62, 82);
    case ITEM_BK_FOCSEL:
        return RGB(36, 75, 112);
    case ITEM_BK_FOCUSED:
    case PROGRESS_BK_SELECTED:
        return RGB(62, 125, 231);
    case ITEM_FG_NORMAL:
    case PROGRESS_FG_NORMAL:
        return RGB(232, 232, 232);
    case ITEM_FG_FOCUSED:
    case ITEM_FG_HIGHLIGHT:
    case PROGRESS_FG_SELECTED:
        return RGB(255, 255, 255);
    case ITEM_BK_NORMAL:
    case FOCUS_BK_INACTIVE_NORMAL:
    case PROGRESS_BK_NORMAL:
        return RGB(30, 30, 30);
    case ITEM_BK_HIGHLIGHT:
    case ACTIVE_CAPTION_BK:
        return RGB(45, 45, 48);
    case FOCUS_ACTIVE_NORMAL:
        return RGB(145, 145, 145);
    case FOCUS_FG_INACTIVE_NORMAL:
        return RGB(120, 120, 120);
    case ICON_BLEND_FOCUSED:
        return RGB(150, 150, 150);
    case HOT_PANEL:
        return RGB(120, 170, 255);
    case HOT_ACTIVE:
        return RGB(170, 200, 255);
    case HOT_INACTIVE:
        return RGB(140, 170, 220);
    case ACTIVE_CAPTION_FG:
        return RGB(235, 235, 235);
    case INACTIVE_CAPTION_FG:
        return RGB(180, 180, 180);
    case INACTIVE_CAPTION_BK:
        return RGB(37, 37, 38);
    default:
        return storedRgb;
    }
}

void ResolvePanelPalette(const DWORD* stored, DWORD* effective, bool darkMode, bool customScheme)
{
    for (int i = 0; i < NUMBER_OF_COLORS; ++i)
        effective[i] = (stored[i] & 0xff000000) | ResolvePanelColor(i, stored[i], darkMode, customScheme);
}
