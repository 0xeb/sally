// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace salupdate
{
inline LOGFONTW DialogFontForDpi(unsigned dpi)
{
    LOGFONTW font = {};
    font.lfHeight = -MulDiv(8, dpi == 0 ? 96 : dpi, 72);
    font.lfWeight = FW_NORMAL;
    font.lfCharSet = DEFAULT_CHARSET;
    // Match Sally's dialog resource templates; GDI resolves the shell dialog font alias.
    lstrcpyW(font.lfFaceName, L"MS Shell Dlg");
    return font;
}
} // namespace salupdate
