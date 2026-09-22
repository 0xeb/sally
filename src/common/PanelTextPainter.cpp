// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "PanelTextPainter.h"

namespace
{
sally::ui::GdiPanelTextPainter DefaultPanelTextPainter;
sally::ui::IPanelTextPainter* TestPanelTextPainter = NULL;
}

namespace sally::ui
{
UINT GetPanelTextDrawOptions(UINT options)
{
    return options | ETO_CLIPPED;
}

BOOL GdiPanelTextPainter::DrawWide(HDC hdc, int x, int y, UINT options, const RECT* rect,
                                   const wchar_t* text, UINT count, const INT* dx)
{
    return ExtTextOutW(hdc, x, y, GetPanelTextDrawOptions(options), rect, text, count, dx);
}

IPanelTextPainter* GetPanelTextPainter()
{
    return TestPanelTextPainter != NULL ? TestPanelTextPainter : &DefaultPanelTextPainter;
}

void SetPanelTextPainterForTests(IPanelTextPainter* painter)
{
    TestPanelTextPainter = painter;
}

BOOL DrawPanelTextW(HDC hdc, int x, int y, UINT options, const RECT* rect,
                    const wchar_t* text, UINT count, const INT* dx)
{
    return GetPanelTextPainter()->DrawWide(hdc, x, y, options, rect, text, count, dx);
}
} // namespace sally::ui
