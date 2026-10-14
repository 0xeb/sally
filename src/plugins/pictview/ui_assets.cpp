// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ui_assets.h"

#include <limits>

namespace pictview
{

    HBITMAP ScaleToolbarBitmap(HBITMAP source, int imageCount, int imageSize)
    {
        BITMAP original = {};
        if (source == nullptr || imageCount <= 0 || imageSize <= 0 ||
            GetObjectW(source, sizeof(original), &original) != sizeof(original) ||
            original.bmHeight <= 0 || original.bmWidth / original.bmHeight != imageCount ||
            original.bmWidth % original.bmHeight != 0 ||
            imageSize > (std::numeric_limits<int>::max)() / imageCount)
            return nullptr;

        HDC sourceDC = CreateCompatibleDC(nullptr);
        HDC targetDC = CreateCompatibleDC(nullptr);
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = imageCount * imageSize;
        info.bmiHeader.biHeight = -imageSize;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP target = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        bool copied = false;
        if (sourceDC != nullptr && targetDC != nullptr && target != nullptr)
        {
            HGDIOBJ oldSource = SelectObject(sourceDC, source);
            HGDIOBJ oldTarget = SelectObject(targetDC, target);
            if (oldSource != nullptr && oldSource != HGDI_ERROR && oldTarget != nullptr && oldTarget != HGDI_ERROR)
            {
                // Nearest-neighbour scaling retains the exact magenta color key and cannot sample
                // pixels from the next glyph in the strip.
                SetStretchBltMode(targetDC, COLORONCOLOR);
                copied = true;
                for (int image = 0; image < imageCount; ++image)
                {
                    if (!StretchBlt(targetDC, image * imageSize, 0, imageSize, imageSize,
                                    sourceDC, image * original.bmHeight, 0, original.bmHeight, original.bmHeight, SRCCOPY))
                    {
                        copied = false;
                        break;
                    }
                }
            }
            if (oldSource != nullptr && oldSource != HGDI_ERROR)
                SelectObject(sourceDC, oldSource);
            if (oldTarget != nullptr && oldTarget != HGDI_ERROR)
                SelectObject(targetDC, oldTarget);
        }
        if (sourceDC != nullptr)
            DeleteDC(sourceDC);
        if (targetDC != nullptr)
            DeleteDC(targetDC);
        if (!copied && target != nullptr)
        {
            DeleteObject(target);
            target = nullptr;
        }
        return target;
    }

} // namespace pictview
