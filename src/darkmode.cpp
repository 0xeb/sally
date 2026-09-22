// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "darkmode.h"
#include "combo_dark_paint.h"
#include "common/IRegistry.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <vector>

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
BOOL BorderColorAttrSupported = TRUE;
thread_local int ListTreeThemeApplyDepth = 0;
thread_local int GroupBoxThemeApplyDepth = 0;

const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_NEW = 20; // Win10 1903+
const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19; // older Win10 builds
const DWORD DWMWA_BORDER_COLOR = 34;
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
const COLORREF DIALOG_DARK_DISABLED_TEXT = RGB(160, 160, 160);
const COLORREF DIALOG_DARK_HIGHLIGHT = RGB(0, 120, 215);
const COLORREF DIALOG_DARK_INACTIVE_SELECTION = RGB(75, 75, 78);
const COLORREF DIALOG_DARK_TOOLTIP_BG = RGB(43, 43, 43);
const COLORREF DIALOG_DARK_FRAME = RGB(62, 62, 66);
const COLORREF DIALOG_DARK_SUBTLE_LINE = RGB(55, 55, 58);
const wchar_t* IMMERSIVE_COLOR_SET_PARAM = L"ImmersiveColorSet";
const wchar_t* WINDOWS_THEME_ELEMENT_PARAM = L"WindowsThemeElement";
const wchar_t* LIGHT_SURFACE_PROP = L"SallyLightSurface";
const wchar_t* BUTTON_CLASS_NAME = L"Button";
const wchar_t* COMBOBOX_CLASS_NAME = L"ComboBox";
const wchar_t* EDIT_CLASS_NAME = L"Edit";
const wchar_t* HEADER_CLASS_NAME = L"SysHeader32";
const wchar_t* LISTBOX_CLASS_NAME = L"ListBox";
const wchar_t* SCROLLBAR_CLASS_NAME = L"ScrollBar";
const wchar_t* STATIC_CLASS_NAME = L"Static";
const WCHAR* UXTHEME_DARKMODE_EXPLORER = L"DarkMode_Explorer";
const WCHAR* UXTHEME_EXPLORER = L"Explorer";
const UINT_PTR GROUPBOX_SUBCLASS_ID = 1;
const UINT_PTR STATIC_EDGE_SUBCLASS_ID = 2;
const UINT_PTR HEADER_SUBCLASS_ID = 4;
const UINT_PTR CHECK_RADIO_SUBCLASS_ID = 8;
const UINT_PTR EDIT_FRAME_SUBCLASS_ID = 16;
const wchar_t* CHECK_RADIO_HOT_PROP = L"SallyCheckRadioHot";

HBRUSH DialogDarkBrush = NULL;
HBRUSH DialogDarkInputBrush = NULL;

void DebugOutW(const wchar_t* text)
{
    OutputDebugStringW(text);
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
    IRegistry* registry = gRegistry != NULL ? gRegistry : GetWin32Registry();
    HKEY hKey = NULL;
    if (!registry->OpenKeyRead(HKEY_CURRENT_USER,
                               SAL_REG_KEY_WINDOWS_THEME_PERSONALIZE_W,
                               hKey).success)
        return FALSE;

    DWORD value = 1;
    const RegistryResult result = registry->GetDWord(hKey, SAL_REG_VALUE_APPS_USE_LIGHT_THEME_W, value);
    registry->CloseKey(hKey);
    if (!result.success)
        return FALSE;

    return value == 0;
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

    if (!InitSupportLogged)
    {
        InitSupportLogged = TRUE;
        TRACE_I("DarkMode init: Windows10AndLater=" << Windows10AndLater
                                                    << ", DwmSetWindowAttribute=" << (void*)DwmSetWindowAttributePtr);
        wchar_t msg[200];
        swprintf_s(msg, _countof(msg), L"DarkMode init: Windows10AndLater=%d DwmSetWindowAttribute=%p\n",
                   (int)Windows10AndLater, (void*)DwmSetWindowAttributePtr);
        DebugOutW(msg);
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
    // WM_SETTINGCHANGE with NULL lParam is used for many unrelated updates.
    if (lParam == 0)
        return FALSE;

    LPCWSTR valueName = (LPCWSTR)lParam;
    if (valueName == NULL || *valueName == 0)
        return FALSE;

    return _wcsicmp(valueName, IMMERSIVE_COLOR_SET_PARAM) == 0 ||
           _wcsicmp(valueName, WINDOWS_THEME_ELEMENT_PARAM) == 0;
}

BOOL CALLBACK ApplyThreadWindowProc(HWND hwnd, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    DarkMode_ApplyTitleBar(hwnd);
    DarkMode_ApplyListTreeThemeRecursive(hwnd);
    return TRUE;
}

void EnsureDialogBrushes()
{
    if (DialogDarkBrush == NULL)
        DialogDarkBrush = CreateSolidBrush(DIALOG_DARK_BG);
    if (DialogDarkInputBrush == NULL)
        DialogDarkInputBrush = CreateSolidBrush(DIALOG_DARK_INPUT_BG);
}

void FillRectSolid(HDC hdc, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    COLORREF oldColor = SetDCBrushColor(hdc, color);
    FillRect(hdc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
}

void DrawRectOutline(HDC hdc, const RECT* rect, COLORREF color)
{
    if (rect == NULL || rect->right <= rect->left || rect->bottom <= rect->top)
        return;

    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    COLORREF oldColor = SetDCPenColor(hdc, color);
    Rectangle(hdc, rect->left, rect->top, rect->right, rect->bottom);
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
}

BOOL HasClassName(HWND hwnd, LPCWSTR expectedClassName)
{
    if (hwnd == NULL || expectedClassName == NULL || !IsWindow(hwnd))
        return FALSE;

    wchar_t className[64] = {0};
    if (GetClassNameW(hwnd, className, _countof(className)) == 0)
        return FALSE;

    return _wcsicmp(className, expectedClassName) == 0;
}

BOOL HasAnyClassName(HWND hwnd, LPCWSTR firstClassName, LPCWSTR secondClassName = NULL, LPCWSTR thirdClassName = NULL)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return FALSE;

    wchar_t className[64] = {0};
    if (GetClassNameW(hwnd, className, _countof(className)) == 0)
        return FALSE;

    return (firstClassName != NULL && _wcsicmp(className, firstClassName) == 0) ||
           (secondClassName != NULL && _wcsicmp(className, secondClassName) == 0) ||
           (thirdClassName != NULL && _wcsicmp(className, thirdClassName) == 0);
}

BOOL IsGroupBox(HWND hwnd)
{
    if (!HasClassName(hwnd, BUTTON_CLASS_NAME))
        return FALSE;

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    return (style & BS_TYPEMASK) == BS_GROUPBOX;
}

BOOL IsRadioButtonStyle(LONG_PTR style)
{
    switch (style & BS_TYPEMASK)
    {
    case BS_RADIOBUTTON:
    case BS_AUTORADIOBUTTON:
        return TRUE;
    }
    return FALSE;
}

BOOL IsCheckButtonStyle(LONG_PTR style)
{
    switch (style & BS_TYPEMASK)
    {
    case BS_CHECKBOX:
    case BS_AUTOCHECKBOX:
    case BS_3STATE:
    case BS_AUTO3STATE:
        return TRUE;
    }
    return FALSE;
}

BOOL IsCheckOrRadioButton(HWND hwnd)
{
    if (!HasClassName(hwnd, BUTTON_CLASS_NAME))
        return FALSE;

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    if ((style & BS_PUSHLIKE) != 0)
        return FALSE;

    return IsCheckButtonStyle(style) || IsRadioButtonStyle(style);
}

BOOL IsStaticEdge(HWND hwnd)
{
    if (!HasClassName(hwnd, STATIC_CLASS_NAME))
        return FALSE;

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    switch (style & SS_TYPEMASK)
    {
    case SS_ETCHEDHORZ:
    case SS_ETCHEDVERT:
    case SS_GRAYFRAME:
    case SS_BLACKFRAME:
    case SS_WHITEFRAME:
        return TRUE;
    }
    return FALSE;
}

void InvalidateGroupBox(HWND hwnd)
{
    if (hwnd != NULL && IsWindow(hwnd))
        InvalidateRect(hwnd, NULL, TRUE);
}

void InvalidateCheckRadio(HWND hwnd)
{
    if (hwnd != NULL && IsWindow(hwnd))
        InvalidateRect(hwnd, NULL, TRUE);
}

BOOL PaintDarkStaticEdge(HWND hwnd, HDC paintDC)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return FALSE;

    PAINTSTRUCT ps;
    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = BeginPaint(hwnd, &ps);
    if (hdc == NULL)
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    switch (style & SS_TYPEMASK)
    {
    case SS_ETCHEDHORZ:
    {
        FillRectSolid(hdc, &client, colors.DialogBackground);
        int y = max(client.top, min(client.bottom - 1, (client.top + client.bottom) / 2));
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
        COLORREF oldColor = SetDCPenColor(hdc, DIALOG_DARK_SUBTLE_LINE);
        MoveToEx(hdc, client.left, y, NULL);
        LineTo(hdc, client.right, y);
        SetDCPenColor(hdc, oldColor);
        SelectObject(hdc, oldPen);
        break;
    }

    case SS_ETCHEDVERT:
    {
        FillRectSolid(hdc, &client, colors.DialogBackground);
        int x = max(client.left, min(client.right - 1, (client.left + client.right) / 2));
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
        COLORREF oldColor = SetDCPenColor(hdc, DIALOG_DARK_SUBTLE_LINE);
        MoveToEx(hdc, x, client.top, NULL);
        LineTo(hdc, x, client.bottom);
        SetDCPenColor(hdc, oldColor);
        SelectObject(hdc, oldPen);
        break;
    }

    default:
        FillRectSolid(hdc, &client, colors.DialogBackground);
        InflateRect(&client, -1, -1);
        if (client.right > client.left && client.bottom > client.top)
            DrawRectOutline(hdc, &client, DIALOG_DARK_SUBTLE_LINE);
        break;
    }

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

BOOL PaintDarkHeader(HWND hwnd, HDC paintDC)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return FALSE;

    PAINTSTRUCT ps;
    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = BeginPaint(hwnd, &ps);
    if (hdc == NULL)
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    FillRectSolid(hdc, &client, colors.InputBackground);

    HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HFONT hOldFont = NULL;
    if (hFont != NULL)
        hOldFont = (HFONT)SelectObject(hdc, hFont);

    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF oldTextColor = SetTextColor(hdc, colors.InputText);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    COLORREF oldPenColor = SetDCPenColor(hdc, DIALOG_DARK_FRAME);

    int count = Header_GetItemCount(hwnd);
    for (int i = 0; i < count; i++)
    {
        RECT itemRect;
        if (!Header_GetItemRect(hwnd, i, &itemRect))
            continue;

        wchar_t text[256] = {0};
        HDITEMW item = {0};
        item.mask = HDI_TEXT | HDI_FORMAT;
        item.pszText = text;
        item.cchTextMax = _countof(text);
        if (!SendMessageW(hwnd, HDM_GETITEMW, i, (LPARAM)&item))
            continue;

        RECT textRect = itemRect;
        textRect.left += 6;
        textRect.right -= 6;
        DWORD flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
        if ((item.fmt & HDF_CENTER) != 0)
            flags |= DT_CENTER;
        else if ((item.fmt & HDF_RIGHT) != 0)
            flags |= DT_RIGHT;
        else
            flags |= DT_LEFT;
        DrawTextW(hdc, text, -1, &textRect, flags);

        MoveToEx(hdc, itemRect.right - 1, itemRect.top, NULL);
        LineTo(hdc, itemRect.right - 1, itemRect.bottom);
    }

    MoveToEx(hdc, client.left, client.bottom - 1, NULL);
    LineTo(hdc, client.right, client.bottom - 1);

    SetDCPenColor(hdc, oldPenColor);
    SelectObject(hdc, oldPen);
    SetTextColor(hdc, oldTextColor);
    SetBkMode(hdc, oldBkMode);
    if (hOldFont != NULL)
        SelectObject(hdc, hOldFont);

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

DWORD GetGroupBoxTextFlags(HWND hwnd)
{
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    DWORD flags = DT_SINGLELINE | DT_VCENTER;

    if ((style & BS_CENTER) == BS_CENTER)
        flags |= DT_CENTER;
    else if ((style & BS_RIGHT) == BS_RIGHT)
        flags |= DT_RIGHT;
    else
        flags |= DT_LEFT;

    LRESULT uiState = SendMessage(hwnd, WM_QUERYUISTATE, 0, 0);
    if ((uiState & UISF_HIDEACCEL) != 0)
        flags |= DT_HIDEPREFIX;

    return flags;
}

BOOL PaintDarkGroupBox(HWND hwnd, HDC paintDC)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return FALSE;

    PAINTSTRUCT ps;
    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = BeginPaint(hwnd, &ps);
    if (hdc == NULL)
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    if (client.right > client.left && client.bottom > client.top)
    {
        HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        if (hFont == NULL)
            hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        int oldBkMode = SetBkMode(hdc, TRANSPARENT);
        COLORREF oldTextColor = SetTextColor(hdc, IsWindowEnabled(hwnd) ? colors.DialogText : colors.DisabledText);
        COLORREF oldBkColor = SetBkColor(hdc, colors.DialogBackground);

        TEXTMETRICW tm;
        memset(&tm, 0, sizeof(tm));
        GetTextMetricsW(hdc, &tm);
        int frameTop = max(1, (tm.tmHeight + 1) / 2);

        HPEN hPen = CreatePen(PS_SOLID, 1, colors.Border);
        HPEN hOldPen = NULL;
        if (hPen != NULL)
            hOldPen = (HPEN)SelectObject(hdc, hPen);

        RECT frame = client;
        frame.top = min(frame.bottom - 1, frameTop);
        frame.right--;
        frame.bottom--;
        if (frame.right > frame.left && frame.bottom > frame.top)
        {
            MoveToEx(hdc, frame.left, frame.top, NULL);
            LineTo(hdc, frame.right, frame.top);
            LineTo(hdc, frame.right, frame.bottom);
            LineTo(hdc, frame.left, frame.bottom);
            LineTo(hdc, frame.left, frame.top);
        }

        if (hOldPen != NULL)
            SelectObject(hdc, hOldPen);
        if (hPen != NULL)
            DeleteObject(hPen);

        int textLen = GetWindowTextLengthW(hwnd);
        if (textLen > 0)
        {
            wchar_t* text = new wchar_t[textLen + 1];
            if (text != NULL)
            {
                int copied = GetWindowTextW(hwnd, text, textLen + 1);
                if (copied > 0)
                {
                    DWORD textFlags = GetGroupBoxTextFlags(hwnd);
                    DWORD calcFlags = (textFlags & ~(DWORD)(DT_CENTER | DT_RIGHT)) | DT_LEFT | DT_CALCRECT;
                    RECT textCalc = {0, 0, max(0, client.right - client.left), tm.tmHeight + tm.tmExternalLeading + 4};
                    DrawTextW(hdc, text, copied, &textCalc, calcFlags);

                    int textWidth = textCalc.right - textCalc.left;
                    int textHeight = max(tm.tmHeight, textCalc.bottom - textCalc.top);
                    int margin = max(7, tm.tmAveCharWidth);
                    int pad = max(2, tm.tmAveCharWidth / 2);
                    int maxTextWidth = max(0, client.right - client.left - 2 * margin);
                    if (textWidth > maxTextWidth)
                        textWidth = maxTextWidth;

                    int textLeft = margin;
                    if ((textFlags & DT_CENTER) != 0)
                        textLeft = (client.right - client.left - textWidth) / 2;
                    else if ((textFlags & DT_RIGHT) != 0)
                        textLeft = client.right - margin - textWidth;
                    textLeft = max(margin, textLeft);

                    RECT gap = {
                        max(client.left, textLeft - pad),
                        client.top,
                        min(client.right, textLeft + textWidth + pad),
                        min(client.bottom, max(textHeight, frameTop + 2))};
                    HBRUSH hBrush = CreateSolidBrush(colors.DialogBackground);
                    if (hBrush != NULL)
                    {
                        FillRect(hdc, &gap, hBrush);
                        DeleteObject(hBrush);
                    }

                    RECT textRect = {
                        textLeft,
                        client.top,
                        min(client.right - margin, textLeft + textWidth),
                        gap.bottom};
                    DrawTextW(hdc, text, copied, &textRect, textFlags);
                }
                delete[] text;
            }
        }

        SetBkColor(hdc, oldBkColor);
        SetTextColor(hdc, oldTextColor);
        SetBkMode(hdc, oldBkMode);
        if (hOldFont != NULL)
            SelectObject(hdc, hOldFont);
    }

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

DWORD GetCheckRadioTextFlags(HWND hwnd)
{
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    DWORD flags = 0;

    if ((style & BS_CENTER) == BS_CENTER)
        flags |= DT_CENTER;
    else if ((style & BS_RIGHT) == BS_RIGHT)
        flags |= DT_RIGHT;
    else
        flags |= DT_LEFT;

    if ((style & BS_MULTILINE) != 0)
        flags |= DT_WORDBREAK;
    else
    {
        flags |= DT_SINGLELINE | DT_END_ELLIPSIS;
        LONG_PTR vAlign = style & BS_VCENTER;
        if (vAlign == BS_BOTTOM)
            flags |= DT_BOTTOM;
        else if (vAlign != BS_TOP)
            flags |= DT_VCENTER;
    }

#ifdef BS_NOPREFIX
    if ((style & BS_NOPREFIX) != 0)
        flags |= DT_NOPREFIX;
    else
#endif
    {
        LRESULT uiState = SendMessage(hwnd, WM_QUERYUISTATE, 0, 0);
        if ((uiState & UISF_HIDEACCEL) != 0)
            flags |= DT_HIDEPREFIX;
    }

    return flags;
}

int GetCheckRadioThemeState(HWND hwnd, LONG_PTR style, BOOL hot)
{
    BOOL enabled = IsWindowEnabled(hwnd);
    BOOL pressed = (SendMessage(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
    LRESULT checkState = SendMessage(hwnd, BM_GETCHECK, 0, 0);
    int stateOffset = 0;

    if (!enabled)
        stateOffset = 3;
    else if (pressed)
        stateOffset = 2;
    else if (hot)
        stateOffset = 1;

    if (IsRadioButtonStyle(style))
        return (checkState == BST_CHECKED ? RBS_CHECKEDNORMAL : RBS_UNCHECKEDNORMAL) + stateOffset;

    if (checkState == BST_INDETERMINATE)
        return CBS_MIXEDNORMAL + stateOffset;
    if (checkState == BST_CHECKED)
        return CBS_CHECKEDNORMAL + stateOffset;
    return CBS_UNCHECKEDNORMAL + stateOffset;
}

BOOL PaintDarkCheckRadioButton(HWND hwnd, HDC paintDC)
{
    if (!IsCheckOrRadioButton(hwnd))
        return FALSE;

    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return FALSE;

    PAINTSTRUCT ps;
    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = BeginPaint(hwnd, &ps);
    if (hdc == NULL)
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    FillRectSolid(hdc, &client, colors.DialogBackground);

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    if (hFont == NULL)
        hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF oldTextColor = SetTextColor(hdc, IsWindowEnabled(hwnd) ? colors.DialogText : colors.DisabledText);
    COLORREF oldBkColor = SetBkColor(hdc, colors.DialogBackground);

    TEXTMETRICW tm;
    memset(&tm, 0, sizeof(tm));
    GetTextMetricsW(hdc, &tm);

    BOOL hot = GetPropW(hwnd, CHECK_RADIO_HOT_PROP) != NULL;
    BOOL radio = IsRadioButtonStyle(style);
    int part = radio ? BP_RADIOBUTTON : BP_CHECKBOX;
    int state = GetCheckRadioThemeState(hwnd, style, hot);
    SIZE glyphSize = {GetSystemMetrics(SM_CXMENUCHECK), GetSystemMetrics(SM_CYMENUCHECK)};
    HTHEME hTheme = OpenThemeData(hwnd, L"Button");
    if (hTheme != NULL)
    {
        SIZE themeSize = {0};
        if (SUCCEEDED(GetThemePartSize(hTheme, hdc, part, state, NULL, TS_TRUE, &themeSize)) &&
            themeSize.cx > 0 && themeSize.cy > 0)
            glyphSize = themeSize;
    }
    if (glyphSize.cx <= 0)
        glyphSize.cx = max(12, tm.tmHeight);
    if (glyphSize.cy <= 0)
        glyphSize.cy = max(12, tm.tmHeight);

    int margin = max(2, tm.tmAveCharWidth / 2);
    int spacing = max(4, tm.tmAveCharWidth / 2);
    int height = client.bottom - client.top;
    LONG_PTR vAlign = style & BS_VCENTER;
    int glyphTop = client.top + (height - glyphSize.cy) / 2;
    if (vAlign == BS_TOP)
        glyphTop = client.top;
    else if (vAlign == BS_BOTTOM)
        glyphTop = client.bottom - glyphSize.cy;

    RECT glyphRect = {0};
    glyphRect.top = glyphTop;
    glyphRect.bottom = glyphTop + glyphSize.cy;

    RECT textRect = client;
    textRect.top = client.top;
    textRect.bottom = client.bottom;

    BOOL glyphOnRight = (style & BS_LEFTTEXT) != 0;
    if (glyphOnRight)
    {
        glyphRect.right = client.right - margin;
        glyphRect.left = glyphRect.right - glyphSize.cx;
        textRect.left = client.left + margin;
        textRect.right = max(textRect.left, glyphRect.left - spacing);
    }
    else
    {
        glyphRect.left = client.left + margin;
        glyphRect.right = glyphRect.left + glyphSize.cx;
        textRect.left = min(client.right, glyphRect.right + spacing);
        textRect.right = client.right - margin;
    }

    BOOL glyphDrawn = FALSE;
    if (hTheme != NULL)
    {
        glyphDrawn = SUCCEEDED(DrawThemeBackground(hTheme, hdc, part, state, &glyphRect, NULL));
        CloseThemeData(hTheme);
    }
    if (!glyphDrawn)
    {
        UINT glyphState = radio ? DFCS_BUTTONRADIO : DFCS_BUTTONCHECK;
        LRESULT checkState = SendMessage(hwnd, BM_GETCHECK, 0, 0);
        if (!radio && checkState == BST_INDETERMINATE)
            glyphState = DFCS_BUTTON3STATE | DFCS_CHECKED;
        else if (checkState == BST_CHECKED)
            glyphState |= DFCS_CHECKED;
        if (!IsWindowEnabled(hwnd))
            glyphState |= DFCS_INACTIVE;
        if ((SendMessage(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0)
            glyphState |= DFCS_PUSHED;
        DrawFrameControl(hdc, &glyphRect, DFC_BUTTON, glyphState);
    }

    int textLen = GetWindowTextLengthW(hwnd);
    if (textLen > 0 && textRect.right > textRect.left && textRect.bottom > textRect.top)
    {
        wchar_t* text = new wchar_t[textLen + 1];
        if (text != NULL)
        {
            int copied = GetWindowTextW(hwnd, text, textLen + 1);
            if (copied > 0)
                DrawTextW(hdc, text, copied, &textRect, GetCheckRadioTextFlags(hwnd));
            delete[] text;
        }
    }

    if (GetFocus() == hwnd || (SendMessage(hwnd, BM_GETSTATE, 0, 0) & BST_FOCUS) != 0)
    {
        RECT focusRect = textRect;
        if (focusRect.right <= focusRect.left || focusRect.bottom <= focusRect.top)
            focusRect = glyphRect;
        InflateRect(&focusRect, 1, 1);
        IntersectRect(&focusRect, &focusRect, &client);
        if (focusRect.right > focusRect.left && focusRect.bottom > focusRect.top)
            DrawFocusRect(hdc, &focusRect);
    }

    SetBkColor(hdc, oldBkColor);
    SetTextColor(hdc, oldTextColor);
    SetBkMode(hdc, oldBkMode);
    if (hOldFont != NULL)
        SelectObject(hdc, hOldFont);

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

LRESULT CALLBACK CheckRadioSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkCheckRadioButton(hwnd, NULL))
            return 0;
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkCheckRadioButton(hwnd, (HDC)wParam))
            return 0;
        break;
    }

    case WM_ERASEBKGND:
    {
        if (DarkMode_ShouldUseDark() && IsCheckOrRadioButton(hwnd))
            return TRUE;
        break;
    }

    case WM_MOUSEMOVE:
    {
        if (DarkMode_ShouldUseDark() && IsCheckOrRadioButton(hwnd) && GetPropW(hwnd, CHECK_RADIO_HOT_PROP) == NULL)
        {
            SetPropW(hwnd, CHECK_RADIO_HOT_PROP, (HANDLE)(UINT_PTR)1);
            TRACKMOUSEEVENT tme = {0};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            InvalidateCheckRadio(hwnd);
        }
        break;
    }

    case WM_MOUSELEAVE:
    {
        RemovePropW(hwnd, CHECK_RADIO_HOT_PROP);
        InvalidateCheckRadio(hwnd);
        break;
    }

    case WM_SETTEXT:
    case WM_SETFONT:
    case WM_ENABLE:
    case WM_UPDATEUISTATE:
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_SIZE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
    case BM_SETCHECK:
    case BM_SETSTATE:
    case BM_SETSTYLE:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        InvalidateCheckRadio(hwnd);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemovePropW(hwnd, CHECK_RADIO_HOT_PROP);
        RemoveWindowSubclass(hwnd, CheckRadioSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK StaticEdgeSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkStaticEdge(hwnd, NULL))
            return 0;
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkStaticEdge(hwnd, (HDC)wParam))
            return 0;
        break;
    }

    case WM_ERASEBKGND:
    {
        if (DarkMode_ShouldUseDark())
            return TRUE;
        break;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        InvalidateRect(hwnd, NULL, TRUE);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, StaticEdgeSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK HeaderSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkHeader(hwnd, NULL))
            return 0;
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkHeader(hwnd, (HDC)wParam))
            return 0;
        break;
    }

    case WM_ERASEBKGND:
    {
        if (DarkMode_ShouldUseDark())
            return TRUE;
        break;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_SIZE:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        InvalidateRect(hwnd, NULL, TRUE);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, HeaderSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK GroupBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkGroupBox(hwnd, NULL))
            return 0;
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintDarkGroupBox(hwnd, (HDC)wParam))
            return 0;
        break;
    }

    case WM_ERASEBKGND:
    {
        if (DarkMode_ShouldUseDark())
            return TRUE;
        break;
    }

    case WM_SETTEXT:
    case WM_SETFONT:
    case WM_ENABLE:
    case WM_UPDATEUISTATE:
    case WM_THEMECHANGED:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        InvalidateGroupBox(hwnd);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, GroupBoxSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void ApplyWindowTheme(HWND hwnd, BOOL useDark, LPCWSTR lightTheme = NULL)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    SetWindowTheme(hwnd, useDark ? UXTHEME_DARKMODE_EXPLORER : lightTheme, NULL);
}

// #98: The white hairline around edits and comboboxes in dark mode is the control's
// NON-CLIENT frame, which Windows paints with system (light) colours. No theme class governs
// it - DarkMode_Explorer and DarkMode_CFD were both tried and neither touches it - and it is
// not the 3D client edge either, so stripping WS_EX_CLIENTEDGE does nothing. The only way to
// darken it is to paint it ourselves, which is exactly what the Find dialog has always done
// for its combos (FindComboSkinSubclassProc) and why Find was the one dialog that looked
// right. This generalises that mechanism to every edit-like control.
// #98 (third attempt). The first two were reverted; the third drew a 1px outline over the
// control AFTER letting it paint itself, which measured as a dark hairline with the control's
// own WHITE 2px frame still inside it, and an untouched WHITE drop-down button. An outline is
// not a fix - the control has to stop painting light in the first place.
//
// The Find dialog has always looked right because FindComboSkinSubclassProc OWNER-DRAWS its
// combos: it fills the client, draws the button, the separator and the arrow, then the frame,
// and returns without calling the default handler at all. This is that painter, generalised.
// Colours match Find's so the two look identical.
const COLORREF DARK_COMBO_LINE = RGB(55, 55, 58);
const COLORREF DARK_COMBO_BUTTON = RGB(52, 52, 56);

void DrawComboArrow(HDC hdc, const RECT* rect, COLORREF color)
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

BOOL GetChildRectInParent(HWND hParent, HWND hChild, RECT* rect)
{
    if (hParent == NULL || hChild == NULL || rect == NULL || !IsWindow(hParent) || !IsWindow(hChild))
        return FALSE;
    // A drop-list combo reports itself (or nothing) as hwndItem. Treating the control as its own
    // child produced a rect covering the whole client, and the ExcludeClipRect below then clipped
    // away everything the painter was about to draw.
    if (hChild == hParent)
        return FALSE;
    if (!GetWindowRect(hChild, rect))
        return FALSE;
    MapWindowPoints(NULL, hParent, (POINT*)rect, 2);
    return TRUE;
}

// [merge:main->unicode] main shipped this as its own TCHAR/GetClassName/_tcsicmp helper, which
// cannot compile here: COMBOBOX_CLASS_NAME is a wide literal on this branch and <tchar.h> is gone.
// HasClassName() above is the identical predicate, already wide, so the helper is redundant
// rather than in need of porting - deleted, and its three call sites now use HasClassName
// directly. Strict simplification: same behaviour, one implementation instead of two.

BOOL PaintDarkComboClient(HWND hwnd, HDC paintDC)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    if (client.right <= client.left || client.bottom <= client.top)
        return TRUE;

    COMBOBOXINFO cbi = {0};
    cbi.cbSize = sizeof(cbi);
    GetComboBoxInfo(hwnd, &cbi);

    RECT editRect;
    BOOL haveEditRect = GetChildRectInParent(hwnd, cbi.hwndItem, &editRect);

    int savedDC = SaveDC(paintDC);
    if (haveEditRect)
        ExcludeClipRect(paintDC, editRect.left, editRect.top, editRect.right, editRect.bottom);

    FillRectSolid(paintDC, &client, colors.InputBackground);

    int buttonWidth = max(GetSystemMetrics(SM_CXVSCROLL), client.bottom - client.top);
    RECT button = client;
    button.left = max(client.left + 1, client.right - buttonWidth - 1);
    button.top = client.top + 1;
    button.right = client.right - 1;
    button.bottom = client.bottom - 1;
    if (button.right > button.left && button.bottom > button.top)
    {
        FillRectSolid(paintDC, &button, DARK_COMBO_BUTTON);
        HGDIOBJ oldPen = SelectObject(paintDC, GetStockObject(DC_PEN));
        COLORREF oldPenColor = SetDCPenColor(paintDC, DARK_COMBO_LINE);
        MoveToEx(paintDC, button.left, button.top, NULL);
        LineTo(paintDC, button.left, button.bottom);
        SetDCPenColor(paintDC, oldPenColor);
        SelectObject(paintDC, oldPen);
        DrawComboArrow(paintDC, &button, IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText);
    }

    // A CBS_DROPDOWNLIST combo has no edit child, so nothing else will ever draw its selected
    // item - the control would render as an empty box with an arrow. Draw it here.
    //
    // This omission was inherited: the Find painter this was generalised from has it too, and
    // went unnoticed because three of its four combos are CBS_DROPDOWN, where the edit child
    // covers for it. Generalising took the defect from one control to every combo in the
    // application - including the theme selector on the Appearance page.
    if (!haveEditRect)
        ComboDarkDrawSelectedItem(hwnd, paintDC, client, button, {colors.InputText, colors.DisabledText, colors.Highlight, colors.HighlightText});

    RestoreDC(paintDC, savedDC);
    DrawRectOutline(paintDC, &client, DIALOG_DARK_FRAME);
    return TRUE;
}

void PaintDarkControlFrame(HWND hwnd)
{
    HDC hdc = GetWindowDC(hwnd);
    if (hdc == NULL)
        return;
    RECT rect;
    GetWindowRect(hwnd, &rect);
    OffsetRect(&rect, -rect.left, -rect.top);
    DrawRectOutline(hdc, &rect, DIALOG_DARK_FRAME);
    ReleaseDC(hwnd, hdc);
}

LRESULT CALLBACK EditFrameSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            // Scrollbars live in the NON-CLIENT area and are drawn by the default handler.
            // Suppressing it outright left multiline edits (the FTP log, for one) with a blank
            // gutter and no usable scrollbar. Let the default painter run for those, then put
            // our frame back over the edge; only frameless controls skip it entirely.
            const LONG style = GetWindowLong(hwnd, GWL_STYLE);
            if ((style & (WS_VSCROLL | WS_HSCROLL)) != 0)
            {
                LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
                PaintDarkControlFrame(hwnd);
                return ret;
            }
            PaintDarkControlFrame(hwnd); // replace the light system frame entirely
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        // A combobox must be owner-drawn outright. Letting it paint and then outlining the edge
        // leaves its own white frame and white drop-down button underneath - that was the third
        // failed attempt at #98.
        if (DarkMode_ShouldUseDark() && HasClassName(hwnd, COMBOBOX_CLASS_NAME))
        {
            PAINTSTRUCT ps;
            HDC hdc = HANDLES(BeginPaint(hwnd, &ps));
            if (hdc != NULL)
                PaintDarkComboClient(hwnd, hdc);
            HANDLES(EndPaint(hwnd, &ps));
            return 0;
        }

        // Plain edits paint their client correctly already; only the non-client frame is light.
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (DarkMode_ShouldUseDark())
            PaintDarkControlFrame(hwnd);
        return ret;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && HasClassName(hwnd, COMBOBOX_CLASS_NAME) &&
            PaintDarkComboClient(hwnd, (HDC)wParam))
        {
            return 0;
        }
        break;
    }

    case WM_ERASEBKGND:
    {
        // The owner-draw above covers the whole client; erasing first only flickers.
        if (DarkMode_ShouldUseDark() && HasClassName(hwnd, COMBOBOX_CLASS_NAME))
            return TRUE;
        break;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, EditFrameSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

void ApplyEditLikeTheme(HWND hwnd, BOOL useDark)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    ApplyWindowTheme(hwnd, useDark);
    if (useDark)
        SetWindowSubclass(hwnd, EditFrameSubclassProc, EDIT_FRAME_SUBCLASS_ID, 0);
    else
        RemoveWindowSubclass(hwnd, EditFrameSubclassProc, EDIT_FRAME_SUBCLASS_ID);

    // SWP_FRAMECHANGED so the non-client area is repainted through the (new) handler
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    InvalidateRect(hwnd, NULL, TRUE);
}

void ApplyComboBoxChildThemes(HWND hwnd, BOOL useDark)
{
    COMBOBOXINFO cbi = {0};
    cbi.cbSize = sizeof(cbi);
    if (GetComboBoxInfo(hwnd, &cbi))
    {
        ApplyEditLikeTheme(cbi.hwndItem, useDark); // the edit inside the combo
        ApplyWindowTheme(cbi.hwndList, useDark);   // the drop-down list stays Explorer-themed
        if (cbi.hwndItem != NULL)
            InvalidateRect(cbi.hwndItem, NULL, TRUE);
        if (cbi.hwndList != NULL)
            InvalidateRect(cbi.hwndList, NULL, TRUE);
    }
}

void ApplyTooltipTheme(HWND hwndTooltip)
{
    if (hwndTooltip == NULL || !IsWindow(hwndTooltip))
        return;

    DarkModeColors colors;
    DarkMode_GetColors(&colors);
    SendMessage(hwndTooltip, TTM_SETTIPBKCOLOR, colors.ToolTipBackground, 0);
    SendMessage(hwndTooltip, TTM_SETTIPTEXTCOLOR, colors.ToolTipText, 0);
    InvalidateRect(hwndTooltip, NULL, TRUE);
}

void ApplyListTreeThemeToControl(HWND hwnd, BOOL useDark)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;
    if (DarkMode_IsLightSurface(hwnd))
        return;

    wchar_t className[64] = {0};
    if (GetClassNameW(hwnd, className, _countof(className)) == 0)
        return;

    if (_wcsicmp(className, SCROLLBAR_CLASS_NAME) == 0)
    {
        // Custom panel scrollbars are separate controls and need explicit theming.
        ApplyWindowTheme(hwnd, useDark);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_wcsicmp(className, WC_LISTVIEWW) == 0)
    {
        ApplyWindowTheme(hwnd, useDark, UXTHEME_EXPLORER);
        HWND hHeader = ListView_GetHeader(hwnd);
        if (hHeader != NULL)
        {
            ApplyWindowTheme(hHeader, useDark, UXTHEME_EXPLORER);
            SetWindowSubclass(hHeader, HeaderSubclassProc, HEADER_SUBCLASS_ID, 0);
            InvalidateRect(hHeader, NULL, TRUE);
        }
        DarkModeColors colors;
        DarkMode_GetColors(&colors);
        COLORREF bgColor = colors.InputBackground;
        COLORREF textColor = colors.InputText;
        ListView_SetBkColor(hwnd, bgColor);
        ListView_SetTextBkColor(hwnd, bgColor);
        ListView_SetTextColor(hwnd, textColor);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    if (_wcsicmp(className, WC_TREEVIEWW) == 0)
    {
        ApplyWindowTheme(hwnd, useDark, UXTHEME_EXPLORER);
        DarkModeColors colors;
        DarkMode_GetColors(&colors);
        COLORREF bgColor = colors.InputBackground;
        COLORREF textColor = colors.InputText;
        TreeView_SetBkColor(hwnd, bgColor);
        TreeView_SetTextColor(hwnd, textColor);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    if (_wcsicmp(className, LISTBOX_CLASS_NAME) == 0)
    {
        ApplyWindowTheme(hwnd, useDark, UXTHEME_EXPLORER);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_wcsicmp(className, WC_HEADERW) == 0 ||
        _wcsicmp(className, HEADER_CLASS_NAME) == 0)
    {
        ApplyWindowTheme(hwnd, useDark, UXTHEME_EXPLORER);
        SetWindowSubclass(hwnd, HeaderSubclassProc, HEADER_SUBCLASS_ID, 0);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_wcsicmp(className, TOOLTIPS_CLASSW) == 0)
    {
        ApplyWindowTheme(hwnd, useDark);
        ApplyTooltipTheme(hwnd);
        return;
    }

    if (_wcsicmp(className, STATIC_CLASS_NAME) == 0)
    {
        ApplyWindowTheme(hwnd, useDark);
        if (IsStaticEdge(hwnd))
            SetWindowSubclass(hwnd, StaticEdgeSubclassProc, STATIC_EDGE_SUBCLASS_ID, 0);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_wcsicmp(className, BUTTON_CLASS_NAME) == 0)
    {
        ApplyWindowTheme(hwnd, useDark);
        if (IsCheckOrRadioButton(hwnd))
            SetWindowSubclass(hwnd, CheckRadioSubclassProc, CHECK_RADIO_SUBCLASS_ID, 0);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }

    if (_wcsicmp(className, EDIT_CLASS_NAME) == 0 ||
        _wcsicmp(className, UPDOWN_CLASSW) == 0)
    {
        ApplyEditLikeTheme(hwnd, useDark); // #98: paints its own frame in dark mode
        return;
    }

    if (_wcsicmp(className, COMBOBOX_CLASS_NAME) == 0)
    {
        ApplyEditLikeTheme(hwnd, useDark); // #98
        ApplyComboBoxChildThemes(hwnd, useDark);
        return;
    }

    if (_wcsicmp(className, WC_COMBOBOXEXW) == 0)
    {
        ApplyWindowTheme(hwnd, useDark);
        HWND hCombo = (HWND)SendMessage(hwnd, CBEM_GETCOMBOCONTROL, 0, 0);
        ApplyWindowTheme(hCombo, useDark);
        if (hCombo != NULL)
            ApplyComboBoxChildThemes(hCombo, useDark);
        InvalidateRect(hwnd, NULL, TRUE);
        return;
    }
}

void ApplyGroupBoxThemeToControl(HWND hwnd)
{
    if (!IsGroupBox(hwnd))
        return;

    SetWindowSubclass(hwnd, GroupBoxSubclassProc, GROUPBOX_SUBCLASS_ID, 0);
    InvalidateGroupBox(hwnd);
}

BOOL CALLBACK ApplyListTreeThemeEnumProc(HWND hwnd, LPARAM lParam)
{
    ApplyListTreeThemeToControl(hwnd, (BOOL)lParam);
    return TRUE;
}

BOOL CALLBACK ApplyGroupBoxThemeEnumProc(HWND hwnd, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    ApplyGroupBoxThemeToControl(hwnd);
    return TRUE;
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

HBRUSH DarkMode_GetMainFrameBrush()
{
    // #95: a solid dark brush for the main window CLASS background. Without it the class is
    // registered with COLOR_WINDOW (white), so the first WM_ERASEBKGND when the window is
    // shown paints the whole frame white for a moment before the dark children paint over it
    // - the startup flash. Process-lifetime brush.
    static HBRUSH mainFrameBrush = NULL;
    if (mainFrameBrush == NULL)
        mainFrameBrush = CreateSolidBrush(MAINFRAME_DARK_FILL);
    return mainFrameBrush;
}

void DarkMode_SetThemeMode(int themeMode)
{
    int normalized = NormalizeThemeMode(themeMode);
    if (ThemeMode != normalized)
    {
        ThemeMode = normalized;
        TRACE_I("DarkMode theme mode changed to " << ThemeMode);
        wchar_t msg[80];
        swprintf_s(msg, _countof(msg), L"DarkMode theme mode changed to %d\n", ThemeMode);
        DebugOutW(msg);
    }
    else
        ThemeMode = normalized;
}

BOOL DarkMode_ShouldUseDark()
{
    EnsureInitialized();
    return ShouldUseDarkColorsInternal();
}

BOOL DarkMode_GetColors(DarkModeColors* colors)
{
    if (colors == NULL)
        return FALSE;

    BOOL useDark = DarkMode_ShouldUseDark();
    if (useDark)
    {
        colors->DialogBackground = DIALOG_DARK_BG;
        colors->DialogText = DIALOG_DARK_TEXT;
        colors->InputBackground = DIALOG_DARK_INPUT_BG;
        colors->InputText = DIALOG_DARK_INPUT_TEXT;
        colors->DisabledText = DIALOG_DARK_DISABLED_TEXT;
        colors->Border = MAINFRAME_DARK_BORDER;
        colors->Highlight = DIALOG_DARK_HIGHLIGHT;
        colors->HighlightText = RGB(255, 255, 255);
        colors->InactiveSelection = DIALOG_DARK_INACTIVE_SELECTION;
        colors->ToolTipBackground = DIALOG_DARK_TOOLTIP_BG;
        colors->ToolTipText = DIALOG_DARK_INPUT_TEXT;
        colors->ViewerBackground = DIALOG_DARK_INPUT_BG;
        colors->ViewerText = DIALOG_DARK_INPUT_TEXT;
        colors->ViewerSelectionBackground = DIALOG_DARK_HIGHLIGHT;
        colors->ViewerSelectionText = RGB(255, 255, 255);
    }
    else
    {
        colors->DialogBackground = GetSysColor(COLOR_BTNFACE);
        colors->DialogText = GetSysColor(COLOR_WINDOWTEXT);
        colors->InputBackground = GetSysColor(COLOR_WINDOW);
        colors->InputText = GetSysColor(COLOR_WINDOWTEXT);
        colors->DisabledText = GetSysColor(COLOR_GRAYTEXT);
        colors->Border = GetSysColor(COLOR_3DLIGHT);
        colors->Highlight = GetSysColor(COLOR_HIGHLIGHT);
        colors->HighlightText = GetSysColor(COLOR_HIGHLIGHTTEXT);
        colors->InactiveSelection = GetSysColor(COLOR_3DFACE);
        colors->ToolTipBackground = GetSysColor(COLOR_INFOBK);
        colors->ToolTipText = GetSysColor(COLOR_INFOTEXT);
        colors->ViewerBackground = GetSysColor(COLOR_WINDOW);
        colors->ViewerText = GetSysColor(COLOR_WINDOWTEXT);
        colors->ViewerSelectionBackground = GetSysColor(COLOR_WINDOWTEXT);
        colors->ViewerSelectionText = GetSysColor(COLOR_WINDOW);
    }
    return useDark;
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

void DarkMode_SetLightSurface(HWND hwnd, BOOL enable)
{
    if (hwnd == NULL)
        return;

    if (enable)
        SetPropW(hwnd, LIGHT_SURFACE_PROP, (HANDLE)1);
    else
        RemovePropW(hwnd, LIGHT_SURFACE_PROP);
}

BOOL DarkMode_IsLightSurface(HWND hwnd)
{
    for (HWND current = hwnd; current != NULL; current = GetParent(current))
    {
        if (GetPropW(current, LIGHT_SURFACE_PROP) != NULL)
            return TRUE;
    }
    return FALSE;
}

HBRUSH DarkMode_GetDialogCtlColorBrush(UINT msg, HDC hdc, HWND hCtrl)
{
    if (hdc == NULL || !DarkMode_ShouldUseDark())
        return NULL;
    if (DarkMode_IsLightSurface(hCtrl))
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
        if (HasClassName(hCtrl, EDIT_CLASS_NAME))
        {
            SetTextColor(hdc, hCtrl != NULL && !IsWindowEnabled(hCtrl) ? DIALOG_DARK_DISABLED_TEXT : DIALOG_DARK_INPUT_TEXT);
            SetBkColor(hdc, DIALOG_DARK_INPUT_BG);
            SetBkMode(hdc, OPAQUE);
            return DialogDarkInputBrush;
        }
        SetTextColor(hdc, DIALOG_DARK_TEXT);
        if (hCtrl != NULL && !IsWindowEnabled(hCtrl))
            SetTextColor(hdc, DIALOG_DARK_DISABLED_TEXT);
        SetBkColor(hdc, DIALOG_DARK_BG);
        SetBkMode(hdc, TRANSPARENT);
        return DialogDarkBrush;

    case WM_CTLCOLORBTN:
        SetTextColor(hdc, DIALOG_DARK_TEXT);
        if (hCtrl != NULL && !IsWindowEnabled(hCtrl))
            SetTextColor(hdc, DIALOG_DARK_DISABLED_TEXT);
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

void DarkMode_DrawSunkenFrame(HDC hDC, const RECT* r, const DarkModeMainFramePalette& palette)
{
    HGDIOBJ oldPen = SelectObject(hDC, GetStockObject(DC_PEN));

    SetDCPenColor(hDC, palette.LineDark);
    MoveToEx(hDC, r->left, r->bottom - 1, NULL);
    LineTo(hDC, r->left, r->top);
    LineTo(hDC, r->right - 1, r->top);

    SetDCPenColor(hDC, palette.Border);
    MoveToEx(hDC, r->right - 1, r->top, NULL);
    LineTo(hDC, r->right - 1, r->bottom - 1);
    LineTo(hDC, r->left - 1, r->bottom - 1);

    SelectObject(hDC, oldPen);
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
            DebugOutW(L"DarkMode unsupported: DwmSetWindowAttribute is unavailable\n");
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

    wchar_t msg[220];
    swprintf_s(msg, _countof(msg), L"DarkMode apply: hwnd=%p mode=%d useDark=%d hrNew=0x%08lX hrOld=0x%08lX\n",
               hwnd, ThemeMode, (int)useDark, (unsigned long)hrNew, (unsigned long)hrOld);
    DebugOutW(msg);

    if (FAILED(finalHr))
    {
        TRACE_E("DarkMode: failed to apply title bar mode, hwnd=" << hwnd << ", hr=" << std::hex << finalHr);
    }

    // Some systems accept immersive dark mode but keep a light caption.
    // In explicit Dark mode, enforce caption/text colors to make mode visible.
    int normalizedTheme = NormalizeThemeMode(ThemeMode);
    COLORREF captionColor = DWMWA_COLOR_DEFAULT;
    COLORREF textColor = DWMWA_COLOR_DEFAULT;
    COLORREF borderColor = DWMWA_COLOR_DEFAULT;
    if (normalizedTheme == THEME_MODE_DARK)
    {
        captionColor = RGB(32, 32, 32);
        textColor = RGB(255, 255, 255);
    }
    if (useDark)
        borderColor = MAINFRAME_DARK_LINE_DARK;

    HRESULT hrCaption = S_OK;
    HRESULT hrText = S_OK;
    HRESULT hrBorder = S_OK;
    if (BorderColorAttrSupported)
    {
        hrBorder = DwmSetWindowAttributePtr(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
        if (FAILED(hrBorder))
            BorderColorAttrSupported = FALSE;
    }
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
    if (FAILED(hrBorder) || FAILED(hrCaption) || FAILED(hrText))
    {
        TRACE_I("DarkMode: border/caption/text color attributes not available or failed, hwnd=" << hwnd
                                                                                                 << ", hrBorder=" << std::hex << hrBorder
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

void DarkMode_ApplyListTreeThemeRecursive(HWND root)
{
    if (root == NULL || !IsWindow(root))
        return;
    if (DarkMode_IsLightSurface(root))
        return;

    // SetWindowTheme() sends WM_THEMECHANGED synchronously. Guard the theme walk so
    // scrollbar theming does not immediately re-enter this routine and overflow.
    if (ListTreeThemeApplyDepth > 0)
        return;

    ListTreeThemeApplyDepth++;
    BOOL useDark = DarkMode_ShouldUseDark();
    ApplyListTreeThemeToControl(root, useDark);
    EnumChildWindows(root, ApplyListTreeThemeEnumProc, (LPARAM)useDark);
    ListTreeThemeApplyDepth--;
}

void DarkMode_ApplyGroupBoxThemeRecursive(HWND root)
{
    if (root == NULL || !IsWindow(root))
        return;

    if (GroupBoxThemeApplyDepth > 0)
        return;

    GroupBoxThemeApplyDepth++;
    ApplyGroupBoxThemeToControl(root);
    EnumChildWindows(root, ApplyGroupBoxThemeEnumProc, 0);
    GroupBoxThemeApplyDepth--;
}
