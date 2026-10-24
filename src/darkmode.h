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

struct DarkModeColors
{
    COLORREF DialogBackground;
    COLORREF DialogText;
    COLORREF InputBackground;
    COLORREF InputText;
    COLORREF DisabledText;
    COLORREF Border;
    COLORREF Highlight;
    COLORREF HighlightText;
    COLORREF InactiveSelection;
    COLORREF ToolTipBackground;
    COLORREF ToolTipText;
    COLORREF ViewerBackground;
    COLORREF ViewerText;
    COLORREF ViewerSelectionBackground;
    COLORREF ViewerSelectionText;
};

void DarkMode_Initialize();
BOOL DarkMode_IsSupported();
void DarkMode_SetThemeMode(int themeMode);
BOOL DarkMode_ShouldUseDark();
BOOL DarkMode_GetColors(DarkModeColors* colors);
BOOL DarkMode_GetMainFramePalette(DarkModeMainFramePalette* palette);
HBRUSH DarkMode_GetMainFrameBrush(); // #95: dark background brush for the main window class
HBRUSH DarkMode_GetDialogCtlColorBrush(UINT msg, HDC hdc, HWND hCtrl);
void DarkMode_SetLightSurface(HWND hwnd, BOOL enable);
BOOL DarkMode_IsLightSurface(HWND hwnd);
BOOL DarkMode_OnSettingChange(LPARAM lParam);
void DarkMode_ApplyTitleBar(HWND hwnd);
void DarkMode_ApplyToThreadTopLevelWindows(DWORD threadId);
void DarkMode_ApplyListTreeThemeRecursive(HWND root);
void DarkMode_ApplyGroupBoxThemeRecursive(HWND root);
void DarkMode_DrawSunkenFrame(HDC hDC, const RECT* r, const DarkModeMainFramePalette& palette);

// Keep native scrollbar input/tracking, but retain visible themed arrow glyphs.
void DarkMode_PaintConfigurationScrollbarArrows(HWND window);
