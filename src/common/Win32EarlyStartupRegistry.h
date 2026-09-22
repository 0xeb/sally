// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

namespace sally::registry
{

// Raw, allocation-free registry query for Sally's custom pre-CRT entry point.
// dataBytes follows RegQueryValueExW and is always a byte count.
LONG QueryValueForEarlyStartupW(HKEY key, const wchar_t* valueName, DWORD* type,
                                BYTE* data, DWORD* dataBytes);

} // namespace sally::registry
