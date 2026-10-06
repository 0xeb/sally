// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

// The Find dialog's skin re-applies a control's border and edge styles whenever the theme
// changes, from a copy taken the first time the control was skinned. Whether the control is
// shown or enabled is not the skin's business: the dialog hides and disables the "Search file
// content" controls when that section is collapsed. Writing the saved copy back whole made the
// hidden Containing combo visible and enabled again, underneath the Advanced row (#120).
inline LONG_PTR MergeFindSkinStyle(LONG_PTR skinStyle, LONG_PTR currentStyle)
{
    const LONG_PTR dialogOwned = WS_VISIBLE | WS_DISABLED;
    return (skinStyle & ~dialogOwned) | (currentStyle & dialogOwned);
}
