// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>

#include "spl_thum.h"

namespace pictview
{

struct ThumbnailLoadOptions
{
    bool IgnoreEmbeddedThumbnails = false;
    bool ApplyExifOrientation = true;
    uint32_t MaxSourceMegapixels = 90;
    COLORREF Background = RGB(255, 255, 255); // transparent pixels are blended onto it (the panel's)
};

// The thumbnail pixel for a BGRA source pixel, blended onto 'background' by its alpha.
DWORD BlendThumbnailPixel(const unsigned char* bgra, COLORREF background);
// Whether a decoded thumbnail is only a preview: an embedded or decoder thumbnail smaller than
// the box Sally asked for while the image itself is larger. Sally then asks again for quality.
bool ThumbnailIsOnlyPreview(bool fromFullFrame, uint32_t thumbWidth, uint32_t thumbHeight, uint32_t boxWidth,
                            uint32_t boxHeight, uint32_t imageWidth, uint32_t imageHeight);

BOOL LoadThumbnailFromPath(const wchar_t* path, int thumbWidth, int thumbHeight,
                           CSalamanderThumbnailMakerAbstract* thumbMaker,
                           BOOL fastThumbnail);
BOOL LoadThumbnailFromPath(const wchar_t* path, int thumbWidth, int thumbHeight,
                           CSalamanderThumbnailMakerAbstract* thumbMaker,
                           BOOL fastThumbnail,
                           const ThumbnailLoadOptions& options);

} // namespace pictview
