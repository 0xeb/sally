// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <windows.h>

namespace Sally::Artwork
{
// The caller owns each returned font. Creation is independent of window/profile
// state; a GDI handle belongs to the process that created it.
inline HFONT CreateIdentityFont(int pixels, const wchar_t* face, bool bold = false)
{
    LOGFONTW description = {};
    description.lfHeight = -pixels;
    description.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    description.lfCharSet = DEFAULT_CHARSET;
    description.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(description.lfFaceName, face);
    return CreateFontIndirectW(&description);
}
inline bool FontHasLinkSymbol(HDC dc, HFONT font)
{
    const auto previous = SelectObject(dc, font);
    if (!previous || previous == HGDI_ERROR) return false;
    WORD glyph = 0xffff;
    const DWORD mapped = GetGlyphIndicesW(dc, L"\x2197", 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS);
    SelectObject(dc, previous);
    return mapped != GDI_ERROR && glyph != 0xffff;
}
inline HFONT CreateIdentityLinkFont(HFONT body)
{
    LOGFONTW description = {};
    if (GetObjectW(body, sizeof(description), &description) != sizeof(description)) return NULL;
    description.lfUnderline = TRUE;
    HFONT font = CreateFontIndirectW(&description);
    if (!font) return NULL;
    HDC dc = GetDC(NULL);
    if (dc && !FontHasLinkSymbol(dc, font))
    {
        wcscpy_s(description.lfFaceName, L"Segoe UI Symbol");
        HFONT fallback = CreateFontIndirectW(&description);
        if (fallback && FontHasLinkSymbol(dc, fallback))
        {
            DeleteObject(font); font = fallback;
        }
        else if (fallback) DeleteObject(fallback);
    }
    if (dc) ReleaseDC(NULL, dc);
    return font;
}
// Return an owned smaller font only when the complete translated line needs it.
inline HFONT FitIdentityLine(HDC dc, HFONT source, const wchar_t* text, int width)
{
    LOGFONTW description = {};
    if (width < 1 || GetObjectW(source, sizeof(description), &description) != sizeof(description)) return NULL;
    auto old = SelectObject(dc, source);
    SIZE measured = {};
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &measured);
    SelectObject(dc, old);
    if (measured.cx <= width) return NULL;
    int pixels = (std::max)(1, MulDiv(-description.lfHeight, width, measured.cx));
    HFONT fitted = NULL;
    do
    {
        if (fitted) DeleteObject(fitted);
        description.lfHeight = -pixels;
        fitted = CreateFontIndirectW(&description);
        if (!fitted) return NULL;
        old = SelectObject(dc, fitted);
        GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &measured);
        SelectObject(dc, old);
    } while (measured.cx > width && --pixels > 0);
    return fitted;
}
} // namespace Sally::Artwork
