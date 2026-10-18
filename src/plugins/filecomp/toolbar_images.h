// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <commctrl.h>

namespace filecomp
{
inline int ToolbarIconSize(unsigned dpi)
{
    return MulDiv(16, dpi == 0 ? 96 : dpi, 96);
}

// Two simple vector glyphs: wrapped text and stacked comparison panes.
inline bool AppendToggleImages(HIMAGELIST images, int size)
{
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = screen != NULL ? CreateCompatibleBitmap(screen, size * 2, size) : NULL;
    if (screen != NULL) ReleaseDC(NULL, screen);
    if (dc == NULL || bitmap == NULL)
    {
        if (dc != NULL) DeleteDC(dc);
        if (bitmap != NULL) DeleteObject(bitmap);
        return false;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    HBRUSH background = CreateSolidBrush(RGB(255, 0, 255));
    RECT area = {0, 0, size * 2, size};
    FillRect(dc, &area, background);
    DeleteObject(background);
    HPEN pen = CreatePen(PS_SOLID, (std::max)(1, size / 16), RGB(40, 40, 40));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    auto line = [&](int x1, int y1, int x2, int y2)
    {
        MoveToEx(dc, MulDiv(x1, size, 16), MulDiv(y1, size, 16), NULL);
        LineTo(dc, MulDiv(x2, size, 16), MulDiv(y2, size, 16));
    };
    line(2, 3, 14, 3); line(2, 7, 12, 7); line(12, 7, 12, 11);
    line(12, 11, 7, 11); line(7, 11, 9, 9); line(7, 11, 9, 13);
    line(2, 13, 5, 13);
    Rectangle(dc, size + MulDiv(2, size, 16), MulDiv(2, size, 16),
              size + MulDiv(14, size, 16), MulDiv(14, size, 16));
    line(18, 8, 30, 8);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen); SelectObject(dc, oldBitmap);
    DeleteObject(pen);
    const int added = ImageList_AddMasked(images, bitmap, RGB(255, 0, 255));
    DeleteObject(bitmap); DeleteDC(dc);
    return added == 6;
}

// The resource is a strip of 16-pixel images with a magenta transparency key.
inline HIMAGELIST CreateToolbarImages(HBITMAP source, int iconSize)
{
    BITMAP bitmap = {};
    if (source == NULL || iconSize <= 0 || GetObjectW(source, sizeof(bitmap), &bitmap) == 0 ||
        bitmap.bmHeight != 16 || bitmap.bmWidth <= 0 || bitmap.bmWidth % 16 != 0)
        return NULL;
    const int count = bitmap.bmWidth / 16;
    HDC from = CreateCompatibleDC(NULL);
    HDC to = CreateCompatibleDC(NULL);
    HDC screen = GetDC(NULL);
    HBITMAP scaled = screen != NULL ? CreateCompatibleBitmap(screen, count * iconSize, iconSize) : NULL;
    if (screen != NULL)
        ReleaseDC(NULL, screen);
    HIMAGELIST images = NULL;
    if (from != NULL && to != NULL && scaled != NULL)
    {
        HGDIOBJ oldSource = SelectObject(from, source);
        HGDIOBJ oldScaled = SelectObject(to, scaled);
        // Keep the transparency key exact when magnifying the original pixel artwork.
        SetStretchBltMode(to, COLORONCOLOR);
        const BOOL drawn = StretchBlt(to, 0, 0, count * iconSize, iconSize, from, 0, 0,
                                      bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
        SelectObject(from, oldSource);
        SelectObject(to, oldScaled);
        // ImageList_AddMasked selects the bitmap into its own DC.
        if (drawn)
        {
            images = ImageList_Create(iconSize, iconSize, ILC_COLORDDB | ILC_MASK, count, 1);
            if (images != NULL && ImageList_AddMasked(images, scaled, RGB(255, 0, 255)) < 0)
            {
                ImageList_Destroy(images);
                images = NULL;
            }
        }
    }
    if (scaled != NULL)
        DeleteObject(scaled);
    if (from != NULL)
        DeleteDC(from);
    if (to != NULL)
        DeleteDC(to);
    return images;
}
} // namespace filecomp
