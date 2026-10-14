// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace sally::ui
{
// A window DC starts at the non-client origin. Exclude its client in the same coordinates
// before painting a border, preserving any existing clipping region of the caller.
inline bool ExcludeClientFromWindowDC(HDC dc, const RECT& window, const RECT& clientOnScreen)
{
    return ExcludeClipRect(dc, clientOnScreen.left - window.left, clientOnScreen.top - window.top,
                          clientOnScreen.right - window.left, clientOnScreen.bottom - window.top) != ERROR;
}
} // namespace sally::ui
