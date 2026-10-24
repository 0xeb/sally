// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

namespace Sally::Ui
{
// A surface belongs to its child controls. GetParent also returns popup owners;
// crossing that boundary would make an independent dialog inherit the surface.
inline bool IsLightSurface(HWND window, const wchar_t* property)
{
    for (HWND current = window; current != nullptr; current = GetParent(current))
    {
        if (GetPropW(current, property) != nullptr)
            return true;
        if (!(GetWindowLongPtrW(current, GWL_STYLE) & WS_CHILD))
            break;
    }
    return false;
}
} // namespace Sally::Ui
