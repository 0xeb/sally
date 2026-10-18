// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>

namespace Sally::Appearance
{
inline LOGFONTW SystemDialogFont(unsigned dpi)
{
    HDC dc = GetDC(nullptr);
    const unsigned sourceDpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    const unsigned targetDpi = dpi ? dpi : sourceDpi;
    NONCLIENTMETRICSW metrics = {sizeof(metrics)};
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        auto font = metrics.lfMessageFont;
        if (sourceDpi && sourceDpi != targetDpi)
            font.lfHeight = MulDiv(font.lfHeight, targetDpi, sourceDpi);
        return font;
    }
    LOGFONTW font = {};
    font.lfHeight = -MulDiv(9, targetDpi ? targetDpi : 96, 72);
    font.lfWeight = FW_NORMAL; font.lfCharSet = DEFAULT_CHARSET;
    lstrcpyW(font.lfFaceName, L"Segoe UI");
    return font;
}
} // namespace Sally::Appearance
