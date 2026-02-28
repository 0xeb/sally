// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "darkmode.h"

#include <tchar.h>

namespace
{
typedef HRESULT(WINAPI * PFNDWMSETWINDOWATTRIBUTE)(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD cbAttribute);

PFNDWMSETWINDOWATTRIBUTE DwmSetWindowAttributePtr = NULL;
BOOL Initialized = FALSE;
int ThemeMode = THEME_MODE_LIGHT;
BOOL InitSupportLogged = FALSE;
BOOL SupportWarningLogged = FALSE;
BOOL CaptionColorAttrSupported = TRUE;
BOOL TextColorAttrSupported = TRUE;

const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_NEW = 20; // Win10 1903+
const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19; // older Win10 builds
const DWORD DWMWA_CAPTION_COLOR = 35;
const DWORD DWMWA_TEXT_COLOR = 36;
const COLORREF DWMWA_COLOR_DEFAULT = 0xFFFFFFFF;
const COLORREF MAINFRAME_DARK_FILL = RGB(45, 45, 48);
const COLORREF MAINFRAME_DARK_LINE_DARK = RGB(28, 28, 28);
const COLORREF MAINFRAME_DARK_LINE_LIGHT = RGB(62, 62, 66);
const COLORREF MAINFRAME_DARK_BORDER = RGB(70, 70, 70);
const COLORREF DIALOG_DARK_BG = RGB(45, 45, 48);
const COLORREF DIALOG_DARK_TEXT = RGB(232, 232, 232);
const COLORREF DIALOG_DARK_INPUT_BG = RGB(30, 30, 30);
const COLORREF DIALOG_DARK_INPUT_TEXT = RGB(245, 245, 245);
const TCHAR* IMMERSIVE_COLOR_SET_PARAM = TEXT("ImmersiveColorSet");
const TCHAR* WINDOWS_THEME_ELEMENT_PARAM = TEXT("WindowsThemeElement");

HBRUSH DialogDarkBrush = NULL;
HBRUSH DialogDarkInputBrush = NULL;

void DebugOutA(const char* text)
{
    OutputDebugStringA(text);
}

int NormalizeThemeMode(int mode)
{
    if (mode == THEME_MODE_DARK || mode == THEME_MODE_SYSTEM)
        return mode;
    return THEME_MODE_LIGHT;
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
    if (!SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0))
        return FALSE;
    return (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

BOOL ReadSystemPrefersDarkApps()
{
    HKEY hKey = NULL;
    if (RegOpenKeyEx(HKEY_CURRENT_USER,
                     TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                     0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return FALSE;

    DWORD value = 1;
    DWORD valueSize = sizeof(value);
    DWORD type = 0;
    LONG regRet = RegQueryValueEx(hKey, TEXT("AppsUseLightTheme"), NULL, &type, (LPBYTE)&value, &valueSize);
    RegCloseKey(hKey);

    if (regRet != ERROR_SUCCESS || type != REG_DWORD)
        return FALSE;

    return value == 0;
}

void EnsureInitialized()
{
    if (Initialized)
        return;

    Initialized = TRUE;

    HMODULE hDwm = GetModuleHandle(TEXT("dwmapi.dll"));
    if (hDwm == NULL)
        hDwm = LoadLibrary(TEXT("dwmapi.dll"));

    if (hDwm != NULL)
        DwmSetWindowAttributePtr = (PFNDWMSETWINDOWATTRIBUTE)GetProcAddress(hDwm, "DwmSetWindowAttribute");

    if (!InitSupportLogged)
    {
        InitSupportLogged = TRUE;
        TRACE_I("DarkMode init: Windows10AndLater=" << Windows10AndLater
                                                    << ", DwmSetWindowAttribute=" << (void*)DwmSetWindowAttributePtr);
        char msg[200];
        sprintf_s(msg, "DarkMode init: Windows10AndLater=%d DwmSetWindowAttribute=%p\n",
                  (int)Windows10AndLater, (void*)DwmSetWindowAttributePtr);
        DebugOutA(msg);
    }
}

BOOL ShouldUseDarkColorsInternal()
{
    if (IsHighContrastEnabled())
        return FALSE;

    switch (NormalizeThemeMode(ThemeMode))
    {
    case THEME_MODE_DARK:
        return TRUE;

    case THEME_MODE_SYSTEM:
        return ReadSystemPrefersDarkApps();

    default:
        return FALSE;
    }
}

BOOL IsThemeSettingHint(LPARAM lParam)
{
    if (lParam == 0)
        return TRUE;

    LPCTSTR valueName = (LPCTSTR)lParam;
    if (valueName == NULL || *valueName == 0)
        return FALSE;

    return _tcsicmp(valueName, IMMERSIVE_COLOR_SET_PARAM) == 0 ||
           _tcsicmp(valueName, WINDOWS_THEME_ELEMENT_PARAM) == 0;
}

BOOL CALLBACK ApplyThreadWindowProc(HWND hwnd, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    DarkMode_ApplyTitleBar(hwnd);
    return TRUE;
}

void EnsureDialogBrushes()
{
    if (DialogDarkBrush == NULL)
        DialogDarkBrush = CreateSolidBrush(DIALOG_DARK_BG);
    if (DialogDarkInputBrush == NULL)
        DialogDarkInputBrush = CreateSolidBrush(DIALOG_DARK_INPUT_BG);
}

} // namespace

void DarkMode_Initialize()
{
    EnsureInitialized();
}

BOOL DarkMode_IsSupported()
{
    EnsureInitialized();
    return DwmSetWindowAttributePtr != NULL;
}

void DarkMode_SetThemeMode(int themeMode)
{
    int normalized = NormalizeThemeMode(themeMode);
    if (ThemeMode != normalized)
    {
        ThemeMode = normalized;
        TRACE_I("DarkMode theme mode changed to " << ThemeMode);
        char msg[80];
        sprintf_s(msg, "DarkMode theme mode changed to %d\n", ThemeMode);
        DebugOutA(msg);
    }
    else
        ThemeMode = normalized;
}

BOOL DarkMode_ShouldUseDark()
{
    EnsureInitialized();
    return ShouldUseDarkColorsInternal();
}

BOOL DarkMode_GetMainFramePalette(DarkModeMainFramePalette* palette)
{
    if (palette == NULL)
        return FALSE;

    BOOL useDark = DarkMode_ShouldUseDark();
    if (useDark)
    {
        palette->Fill = MAINFRAME_DARK_FILL;
        palette->LineDark = MAINFRAME_DARK_LINE_DARK;
        palette->LineLight = MAINFRAME_DARK_LINE_LIGHT;
        palette->Border = MAINFRAME_DARK_BORDER;
    }
    else
    {
        palette->Fill = GetSysColor(COLOR_BTNFACE);
        palette->LineDark = GetSysColor(COLOR_BTNSHADOW);
        palette->LineLight = GetSysColor(COLOR_BTNHIGHLIGHT);
        palette->Border = GetSysColor(COLOR_BTNFACE);
    }
    return useDark;
}

HBRUSH DarkMode_GetDialogCtlColorBrush(UINT msg, HDC hdc, HWND hCtrl)
{
    UNREFERENCED_PARAMETER(hCtrl);

    if (hdc == NULL || !DarkMode_ShouldUseDark())
        return NULL;

    EnsureDialogBrushes();
    if (DialogDarkBrush == NULL || DialogDarkInputBrush == NULL)
        return NULL;

    switch (msg)
    {
    case WM_CTLCOLORDLG:
        SetBkColor(hdc, DIALOG_DARK_BG);
        return DialogDarkBrush;

    case WM_CTLCOLORSTATIC:
        SetTextColor(hdc, DIALOG_DARK_TEXT);
        SetBkColor(hdc, DIALOG_DARK_BG);
        SetBkMode(hdc, TRANSPARENT);
        return DialogDarkBrush;

    case WM_CTLCOLOREDIT:
        SetTextColor(hdc, DIALOG_DARK_INPUT_TEXT);
        SetBkColor(hdc, DIALOG_DARK_INPUT_BG);
        SetBkMode(hdc, OPAQUE);
        return DialogDarkInputBrush;

    case WM_CTLCOLORLISTBOX:
        SetTextColor(hdc, DIALOG_DARK_INPUT_TEXT);
        SetBkColor(hdc, DIALOG_DARK_INPUT_BG);
        SetBkMode(hdc, OPAQUE);
        return DialogDarkInputBrush;
    }

    return NULL;
}

BOOL DarkMode_OnSettingChange(LPARAM lParam)
{
    EnsureInitialized();

    if (!DarkMode_IsSupported())
        return FALSE;

    BOOL changed = IsThemeSettingHint(lParam);
    if (changed)
        TRACE_I("DarkMode: relevant WM_SETTINGCHANGE received");
    return changed;
}

void DarkMode_ApplyTitleBar(HWND hwnd)
{
    EnsureInitialized();

    if (!DarkMode_IsSupported())
    {
        if (!SupportWarningLogged)
        {
            SupportWarningLogged = TRUE;
            TRACE_I("DarkMode unsupported: DwmSetWindowAttribute is unavailable");
            DebugOutA("DarkMode unsupported: DwmSetWindowAttribute is unavailable\n");
        }
        return;
    }

    if (!IsTopLevelWindow(hwnd))
        return;

    BOOL useDark = DarkMode_ShouldUseDark();
    HRESULT hrNew = DwmSetWindowAttributePtr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_NEW, &useDark, sizeof(useDark));
    HRESULT hrOld = S_OK;
    HRESULT finalHr = hrNew;
    if (FAILED(finalHr))
    {
        hrOld = DwmSetWindowAttributePtr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &useDark, sizeof(useDark));
        finalHr = hrOld;
    }

    char msg[220];
    sprintf_s(msg, "DarkMode apply: hwnd=%p mode=%d useDark=%d hrNew=0x%08lX hrOld=0x%08lX\n",
              hwnd, ThemeMode, (int)useDark, (unsigned long)hrNew, (unsigned long)hrOld);
    DebugOutA(msg);

    if (FAILED(finalHr))
    {
        TRACE_E("DarkMode: failed to apply title bar mode, hwnd=" << hwnd << ", hr=" << std::hex << finalHr);
    }

    // Some systems accept immersive dark mode but keep a light caption.
    // In explicit Dark mode, enforce caption/text colors to make mode visible.
    int normalizedTheme = NormalizeThemeMode(ThemeMode);
    COLORREF captionColor = DWMWA_COLOR_DEFAULT;
    COLORREF textColor = DWMWA_COLOR_DEFAULT;
    if (normalizedTheme == THEME_MODE_DARK)
    {
        captionColor = RGB(32, 32, 32);
        textColor = RGB(255, 255, 255);
    }

    HRESULT hrCaption = S_OK;
    HRESULT hrText = S_OK;
    if (CaptionColorAttrSupported)
    {
        hrCaption = DwmSetWindowAttributePtr(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
        if (FAILED(hrCaption))
            CaptionColorAttrSupported = FALSE;
    }
    if (TextColorAttrSupported)
    {
        hrText = DwmSetWindowAttributePtr(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
        if (FAILED(hrText))
            TextColorAttrSupported = FALSE;
    }
    if (FAILED(hrCaption) || FAILED(hrText))
    {
        TRACE_I("DarkMode: caption/text color attributes not available or failed, hwnd=" << hwnd
                                                                                           << ", hrCaption=" << std::hex << hrCaption
                                                                                           << ", hrText=" << std::hex << hrText);
    }
}

void DarkMode_ApplyToThreadTopLevelWindows(DWORD threadId)
{
    if (threadId == 0)
        threadId = GetCurrentThreadId();

    EnumThreadWindows(threadId, ApplyThreadWindowProc, 0);
}
