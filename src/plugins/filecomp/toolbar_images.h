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
