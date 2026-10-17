// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// Resolve an RGB+flags scheme entry without changing its owner. Bit 24 denotes
// an inherited/default color (the SALCOLOR SCF_DEFAULT contract).
COLORREF ResolvePanelColor(int colorIndex, DWORD storedColor, bool darkMode, bool customScheme);

// Source and destination are separate owners; destination retains the flags.
void ResolvePanelPalette(const DWORD* stored, DWORD* effective, bool darkMode, bool customScheme);
