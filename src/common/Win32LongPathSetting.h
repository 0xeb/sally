// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include "configuration/LongPathSetting.h"

namespace sally::configuration
{
// Narrow Windows adapter. Only this boundary reads HKLM or launches elevation.
class Win32LongPathSetting
{
    HANDLE Process = nullptr;
    bool Launching = false;
    bool RequestedEnabled = true;
public:
    Win32LongPathSetting() = default;
    Win32LongPathSetting(const Win32LongPathSetting&) = delete;
    Win32LongPathSetting& operator=(const Win32LongPathSetting&) = delete;
    ~Win32LongPathSetting();
    LongPathState Read() const;
    DWORD Begin(HWND owner, bool enabled);
    LongPathChangeResult Poll();
};
}
