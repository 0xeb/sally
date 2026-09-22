// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/PanelTextPainter.h"

inline BOOL FileCompFillRect(HDC hdc, const RECT* rect)
{
    // FileComp uses empty ExtTextOut calls as background fills. Keep that
    // behavior, but route through the shared clipped text policy.
    return sally::ui::DrawPanelTextW(hdc, 0, 0, ETO_OPAQUE, rect, L"", 0, NULL);
}
