// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace sally::ui
{
UINT GetPanelTextDrawOptions(UINT options);

class IPanelTextPainter
{
public:
    virtual ~IPanelTextPainter() {}

    virtual BOOL DrawWide(HDC hdc, int x, int y, UINT options, const RECT* rect,
                          const wchar_t* text, UINT count, const INT* dx) = 0;
};

class GdiPanelTextPainter : public IPanelTextPainter
{
public:
    BOOL DrawWide(HDC hdc, int x, int y, UINT options, const RECT* rect,
                  const wchar_t* text, UINT count, const INT* dx) override;
};

IPanelTextPainter* GetPanelTextPainter();
void SetPanelTextPainterForTests(IPanelTextPainter* painter);

BOOL DrawPanelTextW(HDC hdc, int x, int y, UINT options, const RECT* rect,
                    const wchar_t* text, UINT count, const INT* dx);
} // namespace sally::ui
