// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SALLY_BOTTOMBAR_KEYCAPS_STANDALONE lets the headless test compile this real translation
// unit without Sally's precompiled header.
#ifndef SALLY_BOTTOMBAR_KEYCAPS_STANDALONE
#include "precomp.h"
#endif

#include "bottombar_keycaps.h"

#include <stdio.h>

namespace
{
// The legacy bitmap size. The generated caps never go below it, so the bar keeps its height
// on a standard-DPI display and only grows where the font does.
const int LEGACY_CAP_CX = 17;
const int LEGACY_CAP_CY = 13;
} // namespace

SIZE CalcBottomBarKeyCapSize(HDC hDC, HFONT hFont)
{
    SIZE size;
    size.cx = LEGACY_CAP_CX;
    size.cy = LEGACY_CAP_CY;
    if (hDC == NULL)
        return size;

    HFONT hOldFont = hFont != NULL ? (HFONT)SelectObject(hDC, hFont) : NULL;
    RECT r = {0, 0, 0, 0};
    // "F12" is the widest label, so every cap is one uniform width - the bar stays aligned.
    DrawTextW(hDC, L"F12", 3, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    if (hOldFont != NULL)
        SelectObject(hDC, hOldFont);

    const int textW = r.right - r.left;
    const int textH = r.bottom - r.top;
    if (textW + 6 > size.cx)
        size.cx = textW + 6; // 3px padding either side
    if (textH + 2 > size.cy)
        size.cy = textH + 2;
    return size;
}

void DrawBottomBarKeyCap(HDC hDC, const RECT* rect, const wchar_t* keyText,
                         COLORREF fill, COLORREF textColor, COLORREF outline)
{
    if (hDC == NULL || rect == NULL || keyText == NULL)
        return;

    HBRUSH hBrush = CreateSolidBrush(fill);
    if (hBrush != NULL)
    {
        FillRect(hDC, rect, hBrush);
        DeleteObject(hBrush);
    }
    HBRUSH hOutline = CreateSolidBrush(outline);
    if (hOutline != NULL)
    {
        FrameRect(hDC, rect, hOutline); // the reference build outlines the cap
        DeleteObject(hOutline);
    }

    const int oldBkMode = SetBkMode(hDC, TRANSPARENT);
    const COLORREF oldText = SetTextColor(hDC, textColor);
    RECT textRect = *rect;
    DrawTextW(hDC, keyText, -1, &textRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
    SetTextColor(hDC, oldText);
    SetBkMode(hDC, oldBkMode);
}
