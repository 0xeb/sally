// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace pictview
{

    // Returns an owned bitmap. Each square glyph is resized separately, preserving color-key pixels.
    HBITMAP ScaleToolbarBitmap(HBITMAP source, int imageCount, int imageSize);

} // namespace pictview
