// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

// #96: The bottom bar's F-key indicators came from bottomtb.bmp through a fixed
// ImageList_Create(BOTTOMBAR_CX /*17*/, BOTTOMBAR_CY /*13*/, ...) - a hard-coded 17x13 image
// list with no DPI scaling, unlike the main toolbar right above it which uses the DPI-scaled
// iconSize. So at high DPI the labels grew with the environment font while the key glyphs
// stayed 17px: "too small and hard to distinguish".
//
// These draw the key caps instead, at a size derived from the font, so they scale with DPI.

// Size of one key cap for 'hFont', measured from the widest label ("F12") plus padding.
// Never smaller than the legacy 17x13 so the bar cannot shrink.
SIZE CalcBottomBarKeyCapSize(HDC hDC, HFONT hFont);

// Draws one key cap: 'fill' background with 'textColor' text centred in 'rect'. Pass the same
// colour for both to get a flat (unfilled) look.
// Draws one key cap: 'fill' background, a 1px 'outline' frame, and 'textColor' text centred.
// The reference build draws a cap only slightly lighter than the bar with a dark outline and
// light text - deliberately NOT an inverted white block.
void DrawBottomBarKeyCap(HDC hDC, const RECT* rect, const wchar_t* keyText,
                         COLORREF fill, COLORREF textColor, COLORREF outline);
