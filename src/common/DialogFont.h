// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>

namespace Sally::Appearance
{
inline SIZE DialogFontUnits(HWND window, HFONT font)
{
    HDC dc = GetDC(window);
    if (dc == nullptr || font == nullptr)
    {
        if (dc != nullptr) ReleaseDC(window, dc);
        return {};
    }
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE extent = {};
    TEXTMETRICW metrics = {};
    const bool measured = GetTextExtentPoint32W(dc, L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz", 52, &extent) &&
        GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    ReleaseDC(window, dc);
    return measured ? SIZE{(extent.cx / 26 + 1) / 2, metrics.tmHeight} : SIZE{};
}

inline void MapCurrentDialogRect(HWND window, RECT& rect)
{
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    const SIZE units = DialogFontUnits(window, font);
    if (units.cx <= 0 || units.cy <= 0)
    {
        MapDialogRect(window, &rect);
        return;
    }
    rect.left = MulDiv(rect.left, units.cx, 4);
    rect.right = MulDiv(rect.right, units.cx, 4);
    rect.top = MulDiv(rect.top, units.cy, 8);
    rect.bottom = MulDiv(rect.bottom, units.cy, 8);
}

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
