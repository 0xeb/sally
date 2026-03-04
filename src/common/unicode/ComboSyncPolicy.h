// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace sally::unicode
{
inline bool ShouldSyncUnicodeComboSelection(UINT notifyCode, BOOL isDropdownOpen)
{
    // Sync only on explicit list selection, never while typing.
    if (notifyCode == CBN_SELENDOK)
        return true;
    if (notifyCode == CBN_SELCHANGE && isDropdownOpen)
        return true;
    return false;
}
} // namespace sally::unicode
