// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

struct PluginDarkModeColors
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
    COLORREF CaptionBackground;
    COLORREF CaptionText;
    COLORREF InactiveCaptionBackground;
    COLORREF InactiveCaptionText;
};

// Optional owning-UI-thread override for standalone windows; plugins retain their
// existing environment/default lookup unless this is explicitly called.
void PluginDarkMode_SetProfileRoot(const wchar_t* root);
COLORREF PluginDarkMode_GetLinkColor();
LRESULT PluginDarkMode_DrawLink(LPARAM notification);
void PluginDarkMode_Initialize();
BOOL PluginDarkMode_ShouldUseDark();
BOOL PluginDarkMode_GetColors(PluginDarkModeColors* colors);
BOOL PluginDarkMode_OnSettingChange(LPARAM lParam);

void PluginDarkMode_ApplyTitleBar(HWND hwnd);
void PluginDarkMode_ApplyListTreeThemeRecursive(HWND root);
void PluginDarkMode_ApplyTooltipTheme(HWND hwndTooltip);

HBRUSH PluginDarkMode_GetDialogCtlColorBrush(UINT msg, HDC hdc, HWND hCtrl);

BOOL PluginDarkMode_FillRect(HDC hdc, const RECT* rect, COLORREF color);
BOOL PluginDarkMode_FillDialogBackground(HDC hdc, const RECT* rect);
void PluginDarkMode_FillOwnerDrawBackground(HDC hdc, const RECT* rect, BOOL selected, BOOL focused);
COLORREF PluginDarkMode_GetOwnerDrawTextColor(BOOL selected, BOOL enabled);
