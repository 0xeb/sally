// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h> // LOGFONT

// The bottom toolbar's keyboard-shortcut labels (F1, F2, F5 ...) were drawn in the
// plain environment font and were hard to distinguish, especially in dark mode. They now
// use a BOLD variant of the environment font. This derives that bold LOGFONT from the base
// environment LOGFONT (weight -> FW_BOLD, everything else preserved so size/face/DPI match
// the rest of the UI). Pure so it can be exercised headlessly.
LOGFONT MakeBoldToolbarLogFont(const LOGFONT& base);
