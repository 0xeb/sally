// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

enum
{
    THEME_MODE_LIGHT = 0,
    THEME_MODE_DARK = 1,
    THEME_MODE_SYSTEM = 2,
};

struct DarkModeMainFramePalette
{
    COLORREF Fill;
    COLORREF LineDark;
    COLORREF LineLight;
    COLORREF Border;
};

void DarkMode_Initialize();
BOOL DarkMode_IsSupported();
void DarkMode_SetThemeMode(int themeMode);
BOOL DarkMode_ShouldUseDark();
BOOL DarkMode_GetMainFramePalette(DarkModeMainFramePalette* palette);
HBRUSH DarkMode_GetDialogCtlColorBrush(UINT msg, HDC hdc, HWND hCtrl);
BOOL DarkMode_OnSettingChange(LPARAM lParam);
void DarkMode_ApplyTitleBar(HWND hwnd);
void DarkMode_ApplyToThreadTopLevelWindows(DWORD threadId);
