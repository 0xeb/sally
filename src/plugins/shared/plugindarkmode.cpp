// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "plugindarkmode.h"
#include "combo_dark_paint.h"
#include "plugin_window_text.h"

#include <climits>
#include <new>
#include <string>
#include <vector>

#include <commctrl.h>

#include "registry_names.h"

namespace
{
typedef HRESULT(WINAPI* PFNDWMSETWINDOWATTRIBUTE)(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute);
typedef HRESULT(WINAPI* PFNSETWINDOWTHEME)(HWND hwnd, LPCWSTR pszSubAppName, LPCWSTR pszSubIdList);

const int PLUGIN_THEME_MODE_LIGHT = 0;
const int PLUGIN_THEME_MODE_DARK = 1;
const int PLUGIN_THEME_MODE_SYSTEM = 2;

const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_NEW = 20;
const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19;
const DWORD DWMWA_CAPTION_COLOR = 35;
const DWORD DWMWA_TEXT_COLOR = 36;
const COLORREF DWMWA_COLOR_DEFAULT = 0xFFFFFFFF;

const COLORREF DARK_DIALOG_BG = RGB(45, 45, 48);
const COLORREF DARK_DIALOG_TEXT = RGB(232, 232, 232);
const COLORREF DARK_INPUT_BG = RGB(30, 30, 30);
const COLORREF DARK_INPUT_TEXT = RGB(245, 245, 245);
const COLORREF DARK_DISABLED_TEXT = RGB(160, 160, 160);
const COLORREF DARK_BORDER = RGB(70, 70, 70);
const COLORREF DARK_HIGHLIGHT = RGB(0, 120, 215);
const COLORREF DARK_INACTIVE_SELECTION = RGB(75, 75, 78);
const COLORREF DARK_TOOLTIP_BG = RGB(43, 43, 43);
const COLORREF DARK_CAPTION_BG = RGB(32, 32, 32);
const COLORREF DARK_INACTIVE_CAPTION_BG = RGB(48, 48, 48);

const wchar_t* IMMERSIVE_COLOR_SET_PARAM = L"ImmersiveColorSet";
const wchar_t* WINDOWS_THEME_ELEMENT_PARAM = L"WindowsThemeElement";
const wchar_t* SCROLLBAR_CLASS_NAME = L"ScrollBar";
const wchar_t* BUTTON_CLASS_NAME = L"Button";
const wchar_t* EDIT_CLASS_NAME = L"Edit";
const wchar_t* COMBOBOX_CLASS_NAME = L"ComboBox";
const WCHAR* UXTHEME_DARKMODE_EXPLORER = L"DarkMode_Explorer";
const UINT_PTR PLUGIN_EDIT_FRAME_SUBCLASS_ID = 16;

BOOL Initialized = FALSE;
PFNDWMSETWINDOWATTRIBUTE DwmSetWindowAttributePtr = NULL;
PFNSETWINDOWTHEME SetWindowThemePtr = NULL;
BOOL CaptionColorAttrSupported = TRUE;
BOOL TextColorAttrSupported = TRUE;
HBRUSH DialogDarkBrush = NULL;
HBRUSH InputDarkBrush = NULL;
thread_local int ListTreeThemeApplyDepth = 0;

BOOL ReadEnvironmentVariableOwned(const wchar_t* name, std::wstring& output)
{
    try
    {
        DWORD capacity = GetEnvironmentVariableW(name, NULL, 0);
        while (capacity > 0)
        {
            std::wstring staged(static_cast<size_t>(capacity), L'\0');
            const DWORD written = GetEnvironmentVariableW(name, staged.data(), capacity);
            if (written == 0)
                return FALSE;
            if (written < capacity)
            {
                staged.resize(written);
                output.swap(staged);
                return TRUE;
            }
            capacity = written + 1;
        }
    }
    catch (const std::bad_alloc&)
    {
    }
    return FALSE;
}

int NormalizeThemeMode(int mode)
{
    if (mode == PLUGIN_THEME_MODE_DARK || mode == PLUGIN_THEME_MODE_SYSTEM)
        return mode;
    return PLUGIN_THEME_MODE_LIGHT;
}

BOOL IsTopLevelWindow(HWND hwnd)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return FALSE;

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    return (style & WS_CHILD) == 0;
}

BOOL IsHighContrastEnabled()
{
    HIGHCONTRAST highContrast = {0};
    highContrast.cbSize = sizeof(highContrast);
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0))
        return FALSE;
    return (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

BOOL ReadRegistryDword(HKEY rootKey, const wchar_t* subKey, const wchar_t* valueName, DWORD* value)
{
    HKEY hKey = NULL;
    if (RegOpenKeyExW(rootKey, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;

    DWORD type = 0;
    DWORD size = sizeof(*value);
    LONG result = RegQueryValueExW(hKey, valueName, NULL, &type, (LPBYTE)value, &size);
    RegCloseKey(hKey);

    return result == ERROR_SUCCESS && type == REG_DWORD;
}

BOOL ReadSystemPrefersDarkApps()
{
    DWORD value = 1;
    if (!ReadRegistryDword(HKEY_CURRENT_USER,
                           SAL_REG_KEY_WINDOWS_THEME_PERSONALIZE_W,
                           SAL_REG_VALUE_APPS_USE_LIGHT_THEME_W,
                           &value))
    {
        return FALSE;
    }

    return value == 0;
}

// Reads "Theme mode" from the configuration key the HOST is actually using.
//
// This used to try two hardcoded roots. That is wrong whenever Sally is not running on the
// default one: a Debug build lives under "Software\\Sally\\1.0 Debug", so the core read the
// theme from that key while every plugin dialog read it from "Software\\Sally\\1.0". Switching
// the core to the light theme left the FTP and Welcome dialogs black across restarts, because
// the two keys really did hold different values. Preview builds and imported configurations
// split the same way.
//
// The core now publishes the root it resolved into the process environment
// (PublishConfigRootToEnvironment), so prefer that and treat the hardcoded roots as a fallback
// for a plugin running outside a Sally that publishes it.
BOOL ReadConfiguredThemeMode(int* themeMode)
{
    try
    {
        std::wstring hostRoot;
        if (ReadEnvironmentVariableOwned(SAL_ENV_CONFIG_ROOT_W, hostRoot))
        {
            const std::wstring hostKey = hostRoot + L"\\" SAL_REG_SUBKEY_CONFIGURATION_W;
            DWORD value = PLUGIN_THEME_MODE_LIGHT;
            if (ReadRegistryDword(HKEY_CURRENT_USER, hostKey.c_str(), L"Theme mode", &value))
            {
                *themeMode = NormalizeThemeMode((int)value);
                return TRUE;
            }
            // The host named its root and it carries no "Theme mode" yet: that means the
            // default, NOT "go look in some other build's key".
            *themeMode = PLUGIN_THEME_MODE_LIGHT;
            return TRUE;
        }

        static const wchar_t* configRoots[] = {
            SAL_REG_ROOT_SALLY_1_0_W L"\\" SAL_REG_SUBKEY_CONFIGURATION_W,
            SAL_REG_ROOT_OPENSAL_5_0_W L"\\" SAL_REG_SUBKEY_CONFIGURATION_W,
        };

        for (int i = 0; i < _countof(configRoots); i++)
        {
            DWORD value = PLUGIN_THEME_MODE_LIGHT;
            if (ReadRegistryDword(HKEY_CURRENT_USER, configRoots[i], L"Theme mode", &value))
            {
                *themeMode = NormalizeThemeMode((int)value);
                return TRUE;
            }
        }
    }
    catch (const std::bad_alloc&)
    {
    }

    return FALSE;
}

void EnsureInitialized()
{
    if (Initialized)
        return;

    Initialized = TRUE;

    HMODULE hDwm = GetModuleHandleW(L"dwmapi.dll");
    if (hDwm == NULL)
        hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm != NULL)
        DwmSetWindowAttributePtr = (PFNDWMSETWINDOWATTRIBUTE)GetProcAddress(hDwm, "DwmSetWindowAttribute");

    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (hUxTheme == NULL)
        hUxTheme = LoadLibraryW(L"uxtheme.dll");
    if (hUxTheme != NULL)
        SetWindowThemePtr = (PFNSETWINDOWTHEME)GetProcAddress(hUxTheme, "SetWindowTheme");
}

void EnsureDialogBrushes()
{
    if (DialogDarkBrush == NULL)
        DialogDarkBrush = CreateSolidBrush(DARK_DIALOG_BG);
    if (InputDarkBrush == NULL)
        InputDarkBrush = CreateSolidBrush(DARK_INPUT_BG);
}

void MaybeSetWindowTheme(HWND hwnd, BOOL useDark)
{
    EnsureInitialized();
    if (SetWindowThemePtr != NULL)
        SetWindowThemePtr(hwnd, useDark ? UXTHEME_DARKMODE_EXPLORER : NULL, NULL);
}

BOOL IsThemeSettingHint(LPARAM lParam)
{
    if (lParam == 0)
        return FALSE;

    const wchar_t* valueName = reinterpret_cast<const wchar_t*>(lParam);
    if (valueName == NULL || *valueName == 0)
        return FALSE;

    return _wcsicmp(valueName, IMMERSIVE_COLOR_SET_PARAM) == 0 ||
           _wcsicmp(valueName, WINDOWS_THEME_ELEMENT_PARAM) == 0;
}

// #98: same white hairline as in the core - the control's NON-CLIENT frame, painted by
// Windows with system (light) colours. No theme class governs it, so paint it ourselves, the
// way the core's Find dialog has always done for its combos.
// #98: a combobox has to be OWNER-DRAWN, not outlined.
//
// Painting the control normally and then drawing a 1px frame over the edge leaves the
// control's own white 2px frame and its white drop-down button underneath - measured on the
// FTP Connect dialog. Kept in lockstep with darkmode.cpp's PaintDarkComboClient(); the
// issue98 test pins core/plugin parity.
const COLORREF DARK_COMBO_LINE = RGB(55, 55, 58);
const COLORREF DARK_COMBO_BUTTON = RGB(52, 52, 56);

void PluginFillRectSolid(HDC hdc, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    COLORREF oldColor = SetDCBrushColor(hdc, color);
    FillRect(hdc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
}

void PluginDrawComboArrow(HDC hdc, const RECT* rect, COLORREF color)
{
    int centerX = (rect->left + rect->right) / 2;
    int centerY = (rect->top + rect->bottom) / 2;
    POINT arrow[3] = {
        {centerX - 3, centerY - 1},
        {centerX + 4, centerY - 1},
        {centerX, centerY + 3},
    };

    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    COLORREF oldPenColor = SetDCPenColor(hdc, color);
    COLORREF oldBrushColor = SetDCBrushColor(hdc, color);
    Polygon(hdc, arrow, 3);
    SetDCBrushColor(hdc, oldBrushColor);
    SetDCPenColor(hdc, oldPenColor);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
}

BOOL PluginGetChildRectInParent(HWND hParent, HWND hChild, RECT* rect)
{
    if (hParent == NULL || hChild == NULL || rect == NULL || !IsWindow(hParent) || !IsWindow(hChild))
        return FALSE;

    // A drop-list combo reports itself (or nothing) as hwndItem; treating the control as its own
    // child yields a rect covering the whole client, and ExcludeClipRect then erases everything
    // the painter is about to draw.
    if (hChild == hParent)
        return FALSE;
    if (!GetWindowRect(hChild, rect))
        return FALSE;
    MapWindowPoints(NULL, hParent, (POINT*)rect, 2);
    return TRUE;
}

BOOL PluginIsComboBoxControl(HWND hwnd)
{
    std::wstring className;
    if (!ReadWindowClassOwnedW(hwnd, className))
        return FALSE;
    return _wcsicmp(className.c_str(), COMBOBOX_CLASS_NAME) == 0;
}

BOOL PaintPluginDarkComboClient(HWND hwnd, HDC paintDC)
{
    PluginDarkModeColors colors;
    if (!PluginDarkMode_GetColors(&colors))
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    if (client.right <= client.left || client.bottom <= client.top)
        return TRUE;

    COMBOBOXINFO cbi = {0};
    cbi.cbSize = sizeof(cbi);
    GetComboBoxInfo(hwnd, &cbi);

    RECT editRect;
    BOOL haveEditRect = PluginGetChildRectInParent(hwnd, cbi.hwndItem, &editRect);

    int savedDC = SaveDC(paintDC);
    if (haveEditRect)
        ExcludeClipRect(paintDC, editRect.left, editRect.top, editRect.right, editRect.bottom);

    PluginFillRectSolid(paintDC, &client, colors.InputBackground);

    int scrollWidth = GetSystemMetrics(SM_CXVSCROLL);
    int clientHeight = client.bottom - client.top;
    int buttonWidth = scrollWidth > clientHeight ? scrollWidth : clientHeight;
    RECT button = client;
    int buttonLeft = client.right - buttonWidth - 1;
    button.left = buttonLeft > client.left + 1 ? buttonLeft : client.left + 1;
    button.top = client.top + 1;
    button.right = client.right - 1;
    button.bottom = client.bottom - 1;
    if (button.right > button.left && button.bottom > button.top)
    {
        PluginFillRectSolid(paintDC, &button, DARK_COMBO_BUTTON);
        HGDIOBJ oldPen = SelectObject(paintDC, GetStockObject(DC_PEN));
        COLORREF oldPenColor = SetDCPenColor(paintDC, DARK_COMBO_LINE);
        MoveToEx(paintDC, button.left, button.top, NULL);
        LineTo(paintDC, button.left, button.bottom);
        SetDCPenColor(paintDC, oldPenColor);
        SelectObject(paintDC, oldPen);
        PluginDrawComboArrow(paintDC, &button,
                             IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText);
    }

    // A CBS_DROPDOWNLIST combo has no edit child, so without this its selected item is never
    // drawn and the control renders as an empty box with an arrow. Plugin dialogs are full of
    // them - the FTP Logs picker, the confirmation combos, 7zip and renamer settings.
    if (!haveEditRect)
        ComboDarkDrawSelectedItem(hwnd, paintDC, client, button, {colors.InputText, colors.DisabledText, colors.Highlight, colors.HighlightText});

    RestoreDC(paintDC, savedDC);
    HBRUSH frameBrush = CreateSolidBrush(colors.Border);
    if (frameBrush != NULL)
    {
        FrameRect(paintDC, &client, frameBrush);
        DeleteObject(frameBrush);
    }
    return TRUE;
}

void PaintPluginDarkControlFrame(HWND hwnd)
{
    PluginDarkModeColors colors;
    if (!PluginDarkMode_GetColors(&colors))
        return;
    HDC hdc = GetWindowDC(hwnd);
    if (hdc == NULL)
        return;
    RECT rect;
    GetWindowRect(hwnd, &rect);
    OffsetRect(&rect, -rect.left, -rect.top);
    HBRUSH hBrush = CreateSolidBrush(colors.Border);
    if (hBrush != NULL)
    {
        FrameRect(hdc, &rect, hBrush);
        DeleteObject(hBrush);
    }
    ReleaseDC(hwnd, hdc);
}

LRESULT CALLBACK PluginEditFrameSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_NCPAINT:
    {
        if (PluginDarkMode_ShouldUseDark())
        {
            // Scrollbars are non-client and painted by the default handler. Suppressing it left
            // multiline edits - the FTP log window, renamer stderr, dbviewer properties - with a
            // blank gutter and no usable scrollbar.
            const LONG style = GetWindowLong(hwnd, GWL_STYLE);
            if ((style & (WS_VSCROLL | WS_HSCROLL)) != 0)
            {
                LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
                PaintPluginDarkControlFrame(hwnd);
                return ret;
            }
            PaintPluginDarkControlFrame(hwnd);
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        // A combobox is owner-drawn outright - see PaintPluginDarkComboClient.
        if (PluginDarkMode_ShouldUseDark() && PluginIsComboBoxControl(hwnd))
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (hdc != NULL)
                PaintPluginDarkComboClient(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (PluginDarkMode_ShouldUseDark())
            PaintPluginDarkControlFrame(hwnd);
        return ret;
    }

    case WM_PRINTCLIENT:
    {
        if (PluginDarkMode_ShouldUseDark() && PluginIsComboBoxControl(hwnd) &&
            PaintPluginDarkComboClient(hwnd, (HDC)wParam))
        {
            return 0;
        }
        break;
    }

    case WM_ERASEBKGND:
    {
        if (PluginDarkMode_ShouldUseDark() && PluginIsComboBoxControl(hwnd))
            return TRUE;
        break;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, PluginEditFrameSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void ApplyPluginEditLikeTheme(HWND hwnd, BOOL useDark)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;
    MaybeSetWindowTheme(hwnd, useDark);
    if (useDark)
        SetWindowSubclass(hwnd, PluginEditFrameSubclassProc, PLUGIN_EDIT_FRAME_SUBCLASS_ID, 0);
    else
        RemoveWindowSubclass(hwnd, PluginEditFrameSubclassProc, PLUGIN_EDIT_FRAME_SUBCLASS_ID);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    InvalidateRect(hwnd, NULL, TRUE);
}

void ApplyComboBoxChildThemes(HWND hwnd, BOOL useDark)
{
    COMBOBOXINFO cbi = {0};
    cbi.cbSize = sizeof(cbi);
    if (!GetComboBoxInfo(hwnd, &cbi))
        return;
    if (cbi.hwndItem != NULL)
        ApplyPluginEditLikeTheme(cbi.hwndItem, useDark); // the edit inside the combo
    if (cbi.hwndList != NULL)
        MaybeSetWindowTheme(cbi.hwndList, useDark); // the drop-down list
}

void ApplyListTreeThemeToControl(HWND hwnd, BOOL useDark)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    std::wstring className;
    if (!ReadWindowClassOwnedW(hwnd, className))
        return;

    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);

    if (_wcsicmp(className.c_str(), SCROLLBAR_CLASS_NAME) == 0)
    {
        MaybeSetWindowTheme(hwnd, useDark);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_wcsicmp(className.c_str(), WC_LISTVIEWW) == 0)
    {
        MaybeSetWindowTheme(hwnd, useDark);
        ListView_SetBkColor(hwnd, colors.InputBackground);
        ListView_SetTextBkColor(hwnd, colors.InputBackground);
        ListView_SetTextColor(hwnd, colors.InputText);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    if (_wcsicmp(className.c_str(), WC_TREEVIEWW) == 0)
    {
        MaybeSetWindowTheme(hwnd, useDark);
        TreeView_SetBkColor(hwnd, colors.InputBackground);
        TreeView_SetTextColor(hwnd, colors.InputText);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    if (_wcsicmp(className.c_str(), TOOLTIPS_CLASSW) == 0)
    {
        PluginDarkMode_ApplyTooltipTheme(hwnd);
        return;
    }

    // #98: plugin dialogs already got dark BACKGROUNDS (WM_CTLCOLOR* in winliblt), but
    // buttons, edits and comboboxes were never themed, so Windows drew them with their
    // light frames - bright white borders on every control in dark mode. The core does
    // theme these classes (darkmode.cpp ApplyListTreeThemeToControl); this helper did not,
    // which is why EVERY plugin dialog (not just FTP) looked half-themed.
    if (_wcsicmp(className.c_str(), BUTTON_CLASS_NAME) == 0)
    {
        MaybeSetWindowTheme(hwnd, useDark);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_wcsicmp(className.c_str(), EDIT_CLASS_NAME) == 0)
    {
        ApplyPluginEditLikeTheme(hwnd, useDark); // #98: paints its own frame in dark mode
        return;
    }

    if (_wcsicmp(className.c_str(), COMBOBOX_CLASS_NAME) == 0)
    {
        ApplyPluginEditLikeTheme(hwnd, useDark); // #98
        ApplyComboBoxChildThemes(hwnd, useDark);
        return;
    }
}

BOOL CALLBACK ApplyListTreeThemeEnumProc(HWND hwnd, LPARAM lParam)
{
    ApplyListTreeThemeToControl(hwnd, (BOOL)lParam);
    return TRUE;
}

} // namespace

void PluginDarkMode_Initialize()
{
    EnsureInitialized();
}

BOOL PluginDarkMode_ShouldUseDark()
{
    EnsureInitialized();

    if (IsHighContrastEnabled())
        return FALSE;

    int themeMode = PLUGIN_THEME_MODE_LIGHT;
    if (!ReadConfiguredThemeMode(&themeMode))
        return FALSE;

    switch (themeMode)
    {
    case PLUGIN_THEME_MODE_DARK:
        return TRUE;

    case PLUGIN_THEME_MODE_SYSTEM:
        return ReadSystemPrefersDarkApps();

    default:
        return FALSE;
    }
}

BOOL PluginDarkMode_GetColors(PluginDarkModeColors* colors)
{
    if (colors == NULL)
        return FALSE;

    BOOL useDark = PluginDarkMode_ShouldUseDark();
    if (useDark)
    {
        colors->DialogBackground = DARK_DIALOG_BG;
        colors->DialogText = DARK_DIALOG_TEXT;
        colors->InputBackground = DARK_INPUT_BG;
        colors->InputText = DARK_INPUT_TEXT;
        colors->DisabledText = DARK_DISABLED_TEXT;
        colors->Border = DARK_BORDER;
        colors->Highlight = DARK_HIGHLIGHT;
        colors->HighlightText = RGB(255, 255, 255);
        colors->InactiveSelection = DARK_INACTIVE_SELECTION;
        colors->ToolTipBackground = DARK_TOOLTIP_BG;
        colors->ToolTipText = DARK_INPUT_TEXT;
        colors->CaptionBackground = DARK_CAPTION_BG;
        colors->CaptionText = RGB(255, 255, 255);
        colors->InactiveCaptionBackground = DARK_INACTIVE_CAPTION_BG;
        colors->InactiveCaptionText = DARK_DIALOG_TEXT;
    }
    else
    {
        colors->DialogBackground = GetSysColor(COLOR_BTNFACE);
        colors->DialogText = GetSysColor(COLOR_BTNTEXT);
        colors->InputBackground = GetSysColor(COLOR_WINDOW);
        colors->InputText = GetSysColor(COLOR_WINDOWTEXT);
        colors->DisabledText = GetSysColor(COLOR_GRAYTEXT);
        colors->Border = GetSysColor(COLOR_BTNSHADOW);
        colors->Highlight = GetSysColor(COLOR_HIGHLIGHT);
        colors->HighlightText = GetSysColor(COLOR_HIGHLIGHTTEXT);
        colors->InactiveSelection = GetSysColor(COLOR_3DFACE);
        colors->ToolTipBackground = GetSysColor(COLOR_INFOBK);
        colors->ToolTipText = GetSysColor(COLOR_INFOTEXT);
        colors->CaptionBackground = GetSysColor(COLOR_ACTIVECAPTION);
        colors->CaptionText = GetSysColor(COLOR_CAPTIONTEXT);
        colors->InactiveCaptionBackground = GetSysColor(COLOR_INACTIVECAPTION);
        colors->InactiveCaptionText = GetSysColor(COLOR_INACTIVECAPTIONTEXT);
    }

    return useDark;
}

BOOL PluginDarkMode_OnSettingChange(LPARAM lParam)
{
    return IsThemeSettingHint(lParam);
}

void PluginDarkMode_ApplyTitleBar(HWND hwnd)
{
    EnsureInitialized();

    if (DwmSetWindowAttributePtr == NULL || !IsTopLevelWindow(hwnd))
        return;

    BOOL useDark = PluginDarkMode_ShouldUseDark();
    HRESULT hrNew = DwmSetWindowAttributePtr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_NEW, &useDark, sizeof(useDark));
    if (FAILED(hrNew))
        DwmSetWindowAttributePtr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &useDark, sizeof(useDark));

    COLORREF captionColor = DWMWA_COLOR_DEFAULT;
    COLORREF textColor = DWMWA_COLOR_DEFAULT;
    if (useDark)
    {
        captionColor = DARK_CAPTION_BG;
        textColor = RGB(255, 255, 255);
    }

    if (CaptionColorAttrSupported)
    {
        HRESULT hrCaption = DwmSetWindowAttributePtr(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
        if (FAILED(hrCaption))
            CaptionColorAttrSupported = FALSE;
    }
    if (TextColorAttrSupported)
    {
        HRESULT hrText = DwmSetWindowAttributePtr(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
        if (FAILED(hrText))
            TextColorAttrSupported = FALSE;
    }
}

void PluginDarkMode_ApplyListTreeThemeRecursive(HWND root)
{
    if (root == NULL || !IsWindow(root))
        return;

    if (ListTreeThemeApplyDepth > 0)
        return;

    ListTreeThemeApplyDepth++;
    BOOL useDark = PluginDarkMode_ShouldUseDark();
    ApplyListTreeThemeToControl(root, useDark);
    EnumChildWindows(root, ApplyListTreeThemeEnumProc, (LPARAM)useDark);
    ListTreeThemeApplyDepth--;
}

void PluginDarkMode_ApplyTooltipTheme(HWND hwndTooltip)
{
    if (hwndTooltip == NULL || !IsWindow(hwndTooltip))
        return;

    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);
    SendMessageW(hwndTooltip, TTM_SETTIPBKCOLOR, colors.ToolTipBackground, 0);
    SendMessageW(hwndTooltip, TTM_SETTIPTEXTCOLOR, colors.ToolTipText, 0);
    InvalidateRect(hwndTooltip, NULL, TRUE);
}

HBRUSH PluginDarkMode_GetDialogCtlColorBrush(UINT msg, HDC hdc, HWND hCtrl)
{
    if (hdc == NULL || !PluginDarkMode_ShouldUseDark())
        return NULL;

    EnsureDialogBrushes();
    if (DialogDarkBrush == NULL || InputDarkBrush == NULL)
        return NULL;

    switch (msg)
    {
    case WM_CTLCOLORDLG:
        SetBkColor(hdc, DARK_DIALOG_BG);
        return DialogDarkBrush;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor(hdc, hCtrl != NULL && !IsWindowEnabled(hCtrl) ? DARK_DISABLED_TEXT : DARK_DIALOG_TEXT);
        SetBkColor(hdc, DARK_DIALOG_BG);
        SetBkMode(hdc, TRANSPARENT);
        return DialogDarkBrush;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(hdc, DARK_INPUT_TEXT);
        SetBkColor(hdc, DARK_INPUT_BG);
        SetBkMode(hdc, OPAQUE);
        return InputDarkBrush;
    }

    return NULL;
}

BOOL PluginDarkMode_FillRect(HDC hdc, const RECT* rect, COLORREF color)
{
    if (hdc == NULL || rect == NULL)
        return FALSE;

    HBRUSH brush = CreateSolidBrush(color);
    if (brush == NULL)
        return FALSE;

    FillRect(hdc, rect, brush);
    DeleteObject(brush);
    return TRUE;
}

BOOL PluginDarkMode_FillDialogBackground(HDC hdc, const RECT* rect)
{
    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);
    return PluginDarkMode_FillRect(hdc, rect, colors.DialogBackground);
}

void PluginDarkMode_FillOwnerDrawBackground(HDC hdc, const RECT* rect, BOOL selected, BOOL focused)
{
    PluginDarkModeColors colors;
    BOOL useDark = PluginDarkMode_GetColors(&colors);
    COLORREF color = selected ? (focused || !useDark ? colors.Highlight : colors.InactiveSelection) : colors.InputBackground;
    PluginDarkMode_FillRect(hdc, rect, color);
}

COLORREF PluginDarkMode_GetOwnerDrawTextColor(BOOL selected, BOOL enabled)
{
    PluginDarkModeColors colors;
    PluginDarkMode_GetColors(&colors);
    if (!enabled)
        return colors.DisabledText;
    return selected ? colors.HighlightText : colors.InputText;
}
