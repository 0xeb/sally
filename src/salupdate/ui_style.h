// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include "common/DialogFont.h"

namespace salupdate
{
inline LOGFONTW DialogFontForDpi(unsigned dpi)
{
    return Sally::Appearance::SystemDialogFont(dpi);
}
} // namespace salupdate
