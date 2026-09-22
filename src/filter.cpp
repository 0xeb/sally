// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "darkmode.h"
#include "ui/IPrompter.h"

#include <uxtheme.h>

// Attributes
const wchar_t* FILTERCRITERIA_ATTRIBUTESMASK_REG = L"Attributes Mask";
const wchar_t* FILTERCRITERIA_ATTRIBUTESVALUE_REG = L"Attributes Value";
// Size Min/Max
const wchar_t* FILTERCRITERIA_USEMINSIZE_REG = L"UseMinSize";
const wchar_t* FILTERCRITERIA_MINSIZELO_REG = L"MinSizeLo";
const wchar_t* FILTERCRITERIA_MINSIZEHI_REG = L"MinSizeHi";
const wchar_t* FILTERCRITERIA_MINSIZEUNITS_REG = L"MinSizeUnits";
const wchar_t* FILTERCRITERIA_USEMAXSIZE_REG = L"UseMaxSize";
const wchar_t* FILTERCRITERIA_MAXSIZELO_REG = L"MaxSizeLo";
const wchar_t* FILTERCRITERIA_MAXSIZEHI_REG = L"MaxSizeHi";
const wchar_t* FILTERCRITERIA_MAXSIZEUNITS_REG = L"MaxSizeUnits";
// Date & Time
const wchar_t* FILTERCRITERIA_TIMEMODE_REG = L"TimeMode";
const wchar_t* FILTERCRITERIA_DURINGTIMELO_REG = L"DuringTimeLo";
const wchar_t* FILTERCRITERIA_DURINGTIMEHI_REG = L"DuringTimeHi";
const wchar_t* FILTERCRITERIA_DURINGUNITS_REG = L"DuringUnits";
const wchar_t* FILTERCRITERIA_USEFROMDATE_REG = L"UseFromDate";
const wchar_t* FILTERCRITERIA_USEFROMTIME_REG = L"UseFromTime";
const wchar_t* FILTERCRITERIA_FROMLO_REG = L"FromLo";
const wchar_t* FILTERCRITERIA_FROMHI_REG = L"FromHi";
const wchar_t* FILTERCRITERIA_USETODATE_REG = L"UseToDate";
const wchar_t* FILTERCRITERIA_USETOTIME_REG = L"UseToTime";
const wchar_t* FILTERCRITERIA_TOLO_REG = L"ToLo";
const wchar_t* FILTERCRITERIA_TOHI_REG = L"ToHi";

// we used the following variables in Altap Salamander 2.5,
// where we switched to CFilterCriteria and its Save/Load
const wchar_t* OLD_FINDOPTIONSITEM_ARCHIVE_REG = L"Archive";
const wchar_t* OLD_FINDOPTIONSITEM_READONLY_REG = L"ReadOnly";
const wchar_t* OLD_FINDOPTIONSITEM_HIDDEN_REG = L"Hidden";
const wchar_t* OLD_FINDOPTIONSITEM_SYSTEM_REG = L"System";
const wchar_t* OLD_FINDOPTIONSITEM_COMPRESSED_REG = L"Compressed";
const wchar_t* OLD_FINDOPTIONSITEM_DIRECTORY_REG = L"Directory";
const wchar_t* OLD_FINDOPTIONSITEM_SIZEACTION_REG = L"SizeAction";
const wchar_t* OLD_FINDOPTIONSITEM_SIZELO_REG = L"SizeLo";
const wchar_t* OLD_FINDOPTIONSITEM_SIZEHI_REG = L"SizeHi";
const wchar_t* OLD_FINDOPTIONSITEM_DATEACTION_REG = L"DateAction";
const wchar_t* OLD_FINDOPTIONSITEM_DAY_REG = L"Day";
const wchar_t* OLD_FINDOPTIONSITEM_MONTH_REG = L"Month";
const wchar_t* OLD_FINDOPTIONSITEM_YEAR_REG = L"Year";
const wchar_t* OLD_FINDOPTIONSITEM_TIMEACTION_REG = L"TimeAction";
const wchar_t* OLD_FINDOPTIONSITEM_HOUR_REG = L"Hour";
const wchar_t* OLD_FINDOPTIONSITEM_MINUTE_REG = L"Minute";
const wchar_t* OLD_FINDOPTIONSITEM_SECOND_REG = L"Second";

static const UINT_PTR FILTER_DARK_SKIN_SUBCLASS_ID = 1;
static const COLORREF FILTER_DARK_LINE = RGB(55, 55, 58);
static const COLORREF FILTER_DARK_FRAME = RGB(62, 62, 66);
static const COLORREF FILTER_DARK_BUTTON = RGB(52, 52, 56);
static const COLORREF FILTER_DARK_SECTION_LINE = RGB(82, 82, 86);

enum CFilterDarkSkinKind
{
    fdskStaticLine,
    fdskEdit,
    fdskCombo,
    fdskUpDown,
    fdskDateTime
};

struct CFilterDarkSkinState
{
    LONG_PTR Style;
    LONG_PTR ExStyle;
    CFilterDarkSkinKind Kind;
};

static LRESULT CALLBACK FilterDarkSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

static void FilterFillRectSolid(HDC hdc, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    COLORREF oldColor = SetDCBrushColor(hdc, color);
    FillRect(hdc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
}

static void FilterDrawRectOutline(HDC hdc, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    COLORREF oldColor = SetDCPenColor(hdc, color);
    Rectangle(hdc, rect->left, rect->top, rect->right, rect->bottom);
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
}

static void FilterDrawDownArrow(HDC hdc, const RECT* rect, COLORREF color)
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

static void FilterDrawUpArrow(HDC hdc, const RECT* rect, COLORREF color)
{
    int centerX = (rect->left + rect->right) / 2;
    int centerY = (rect->top + rect->bottom) / 2;
    POINT arrow[3] = {
        {centerX - 3, centerY + 2},
        {centerX + 4, centerY + 2},
        {centerX, centerY - 2},
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

static void FilterDrawCheckMark(HDC hdc, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    COLORREF oldColor = SetDCPenColor(hdc, color);
    int left = rect->left + max(2, (rect->right - rect->left) / 4);
    int midY = (rect->top + rect->bottom) / 2;
    MoveToEx(hdc, left, midY, NULL);
    LineTo(hdc, left + 3, midY + 3);
    LineTo(hdc, rect->right - 2, rect->top + 3);
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldPen);
}

static void SetFilterWindowStyle(HWND hwnd, LONG_PTR style)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    if (GetWindowLongPtr(hwnd, GWL_STYLE) == style)
        return;

    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void SetFilterWindowExStyle(HWND hwnd, LONG_PTR exStyle)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    if (GetWindowLongPtr(hwnd, GWL_EXSTYLE) == exStyle)
        return;

    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static BOOL GetFilterChildRectInDialog(HWND hDialog, int ctrlID, RECT* rect)
{
    if (hDialog == NULL || rect == NULL || !IsWindow(hDialog))
        return FALSE;

    HWND hChild = GetDlgItem(hDialog, ctrlID);
    if (hChild == NULL || !IsWindow(hChild))
        return FALSE;

    if (!GetWindowRect(hChild, rect))
        return FALSE;
    MapWindowPoints(NULL, hDialog, (POINT*)rect, 2);
    return TRUE;
}

static void PaintFilterDarkEditFrame(HWND hwnd)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return;

    HDC hdc = GetWindowDC(hwnd);
    if (hdc == NULL)
        return;

    RECT window;
    GetWindowRect(hwnd, &window);
    OffsetRect(&window, -window.left, -window.top);

    RECT client;
    GetClientRect(hwnd, &client);
    MapWindowPoints(hwnd, NULL, (POINT*)&client, 2);
    RECT screenWindow;
    GetWindowRect(hwnd, &screenWindow);
    OffsetRect(&client, -screenWindow.left, -screenWindow.top);

    int savedDC = SaveDC(hdc);
    ExcludeClipRect(hdc, client.left, client.top, client.right, client.bottom);
    FilterFillRectSolid(hdc, &window, colors.InputBackground);
    RestoreDC(hdc, savedDC);

    FilterDrawRectOutline(hdc, &window, FILTER_DARK_FRAME);
    ReleaseDC(hwnd, hdc);
}

static BOOL PaintFilterDarkStaticLine(HWND hwnd, HDC paintDC)
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
    FilterFillRectSolid(hdc, &client, colors.DialogBackground);
    int y = max(client.top, min(client.bottom - 1, (client.top + client.bottom) / 2));

    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    COLORREF oldColor = SetDCPenColor(hdc, FILTER_DARK_LINE);
    MoveToEx(hdc, client.left, y, NULL);
    LineTo(hdc, client.right, y);
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldPen);

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

static BOOL GetFilterComboText(HWND hwnd, LPWSTR text, int textLen)
{
    if (text == NULL || textLen <= 0)
        return FALSE;

    text[0] = 0;
    int curSel = (int)SendMessage(hwnd, CB_GETCURSEL, 0, 0);
    if (curSel >= 0)
    {
        SendMessage(hwnd, CB_GETLBTEXT, curSel, (LPARAM)text);
        text[textLen - 1] = 0;
        return text[0] != 0;
    }

    GetWindowTextW(hwnd, text, textLen);
    text[textLen - 1] = 0;
    return text[0] != 0;
}

static BOOL PaintFilterDarkCombo(HWND hwnd, HDC paintDC)
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
    FilterFillRectSolid(hdc, &client, colors.InputBackground);

    int buttonWidth = max(GetSystemMetrics(SM_CXVSCROLL), client.bottom - client.top);
    RECT button = client;
    button.left = max(client.left + 1, client.right - buttonWidth - 1);
    button.top = client.top + 1;
    button.right = client.right - 1;
    button.bottom = client.bottom - 1;

    RECT textRect = client;
    textRect.left += 4;
    textRect.right = max(textRect.left, button.left - 3);

    HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HFONT hOldFont = NULL;
    if (hFont != NULL)
        hOldFont = (HFONT)SelectObject(hdc, hFont);

    wchar_t text[256];
    GetFilterComboText(hwnd, text, _countof(text));

    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF oldTextColor = SetTextColor(hdc, IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText);
    DrawTextW(hdc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SetTextColor(hdc, oldTextColor);
    SetBkMode(hdc, oldBkMode);
    if (hOldFont != NULL)
        SelectObject(hdc, hOldFont);

    if (button.right > button.left && button.bottom > button.top)
    {
        FilterFillRectSolid(hdc, &button, FILTER_DARK_BUTTON);
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
        COLORREF oldColor = SetDCPenColor(hdc, FILTER_DARK_LINE);
        MoveToEx(hdc, button.left, button.top, NULL);
        LineTo(hdc, button.left, button.bottom);
        SetDCPenColor(hdc, oldColor);
        SelectObject(hdc, oldPen);
        FilterDrawDownArrow(hdc, &button, IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText);
    }

    FilterDrawRectOutline(hdc, &client, FILTER_DARK_FRAME);

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

static BOOL PaintFilterDarkUpDown(HWND hwnd, HDC paintDC)
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
    FilterFillRectSolid(hdc, &client, FILTER_DARK_BUTTON);

    int midY = (client.top + client.bottom) / 2;
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    COLORREF oldColor = SetDCPenColor(hdc, FILTER_DARK_LINE);
    MoveToEx(hdc, client.left, midY, NULL);
    LineTo(hdc, client.right, midY);
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldPen);

    RECT up = client;
    up.bottom = midY;
    RECT down = client;
    down.top = midY;
    COLORREF arrowColor = IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText;
    FilterDrawUpArrow(hdc, &up, arrowColor);
    FilterDrawDownArrow(hdc, &down, arrowColor);
    FilterDrawRectOutline(hdc, &client, FILTER_DARK_FRAME);

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

static BOOL GetFilterDateTimeText(HWND hwnd, LPWSTR text, int textLen, BOOL* hasValue)
{
    if (text == NULL || textLen <= 0)
        return FALSE;

    text[0] = 0;
    if (hasValue != NULL)
        *hasValue = FALSE;

    SYSTEMTIME st;
    LRESULT state = DateTime_GetSystemtime(hwnd, &st);
    if (state != GDT_VALID)
        return FALSE;

    if (hasValue != NULL)
        *hasValue = TRUE;

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    BOOL ok;
    if ((style & DTS_UPDOWN) != 0)
        ok = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, text, textLen) != 0;
    else
        ok = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, text, textLen) != 0;
    if (!ok)
        text[0] = 0;
    text[textLen - 1] = 0;
    return ok;
}

static BOOL PaintFilterDarkDateTime(HWND hwnd, HDC paintDC)
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
    FilterFillRectSolid(hdc, &client, colors.InputBackground);

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    int buttonWidth = max(GetSystemMetrics(SM_CXVSCROLL), client.bottom - client.top);
    RECT button = client;
    button.left = max(client.left + 1, client.right - buttonWidth - 1);
    button.top = client.top + 1;
    button.right = client.right - 1;
    button.bottom = client.bottom - 1;

    RECT textRect = client;
    textRect.left += 4;
    textRect.right = max(textRect.left, button.left - 3);

    BOOL hasValue = FALSE;
    if ((style & DTS_SHOWNONE) != 0)
    {
        int boxSize = max(9, min(13, client.bottom - client.top - 4));
        RECT check = {
            client.left + 4,
            client.top + max(1, (client.bottom - client.top - boxSize) / 2),
            client.left + 4 + boxSize,
            client.top + max(1, (client.bottom - client.top - boxSize) / 2) + boxSize};
        FilterFillRectSolid(hdc, &check, colors.InputBackground);
        FilterDrawRectOutline(hdc, &check, FILTER_DARK_FRAME);

        wchar_t probe[8];
        GetFilterDateTimeText(hwnd, probe, _countof(probe), &hasValue);
        if (hasValue)
            FilterDrawCheckMark(hdc, &check, IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText);
        textRect.left = check.right + 4;
    }

    wchar_t text[128];
    GetFilterDateTimeText(hwnd, text, _countof(text), &hasValue);

    HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HFONT hOldFont = NULL;
    if (hFont != NULL)
        hOldFont = (HFONT)SelectObject(hdc, hFont);

    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF oldTextColor = SetTextColor(hdc, IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText);
    DrawTextW(hdc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SetTextColor(hdc, oldTextColor);
    SetBkMode(hdc, oldBkMode);
    if (hOldFont != NULL)
        SelectObject(hdc, hOldFont);

    if (button.right > button.left && button.bottom > button.top)
    {
        FilterFillRectSolid(hdc, &button, FILTER_DARK_BUTTON);
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
        COLORREF oldColor = SetDCPenColor(hdc, FILTER_DARK_LINE);
        MoveToEx(hdc, button.left, button.top, NULL);
        LineTo(hdc, button.left, button.bottom);
        if ((style & DTS_UPDOWN) != 0)
        {
            int midY = (button.top + button.bottom) / 2;
            MoveToEx(hdc, button.left, midY, NULL);
            LineTo(hdc, button.right, midY);
        }
        SetDCPenColor(hdc, oldColor);
        SelectObject(hdc, oldPen);

        COLORREF arrowColor = IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText;
        if ((style & DTS_UPDOWN) != 0)
        {
            RECT up = button;
            up.bottom = (button.top + button.bottom) / 2;
            RECT down = button;
            down.top = up.bottom;
            FilterDrawUpArrow(hdc, &up, arrowColor);
            FilterDrawDownArrow(hdc, &down, arrowColor);
        }
        else
            FilterDrawDownArrow(hdc, &button, arrowColor);
    }

    FilterDrawRectOutline(hdc, &client, FILTER_DARK_FRAME);

    if (paintDC == NULL)
        EndPaint(hwnd, &ps);
    return TRUE;
}

static void PaintFilterDarkFrame(HWND hwnd)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return;

    HDC hdc = GetWindowDC(hwnd);
    if (hdc == NULL)
        return;

    RECT rect;
    GetWindowRect(hwnd, &rect);
    OffsetRect(&rect, -rect.left, -rect.top);
    FilterDrawRectOutline(hdc, &rect, FILTER_DARK_FRAME);
    ReleaseDC(hwnd, hdc);
}

static void SetFilterDateTimeCalendarColors(HWND hwnd, BOOL useDark)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    DarkModeColors colors;
    DarkMode_GetColors(&colors);
    SendMessage(hwnd, DTM_SETMCCOLOR, MCSC_BACKGROUND, useDark ? colors.DialogBackground : CLR_DEFAULT);
    SendMessage(hwnd, DTM_SETMCCOLOR, MCSC_MONTHBK, useDark ? colors.InputBackground : CLR_DEFAULT);
    SendMessage(hwnd, DTM_SETMCCOLOR, MCSC_TEXT, useDark ? colors.InputText : CLR_DEFAULT);
    SendMessage(hwnd, DTM_SETMCCOLOR, MCSC_TITLEBK, useDark ? FILTER_DARK_BUTTON : CLR_DEFAULT);
    SendMessage(hwnd, DTM_SETMCCOLOR, MCSC_TITLETEXT, useDark ? colors.InputText : CLR_DEFAULT);
    SendMessage(hwnd, DTM_SETMCCOLOR, MCSC_TRAILINGTEXT, useDark ? colors.DisabledText : CLR_DEFAULT);
}

static void ApplyFilterDarkSkin(HWND hwnd, CFilterDarkSkinKind kind)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    DWORD_PTR data = 0;
    CFilterDarkSkinState* state = NULL;
    if (GetWindowSubclass(hwnd, FilterDarkSkinSubclassProc, FILTER_DARK_SKIN_SUBCLASS_ID, &data))
        state = (CFilterDarkSkinState*)data;
    else
    {
        state = new CFilterDarkSkinState;
        if (state == NULL)
            return;
        state->Style = GetWindowLongPtr(hwnd, GWL_STYLE);
        state->ExStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
        state->Kind = kind;
        if (!SetWindowSubclass(hwnd, FilterDarkSkinSubclassProc, FILTER_DARK_SKIN_SUBCLASS_ID, (DWORD_PTR)state))
        {
            delete state;
            return;
        }
    }

    state->Kind = kind;
    BOOL useDark = DarkMode_ShouldUseDark();
    LONG_PTR style = state->Style;
    LONG_PTR exStyle = state->ExStyle;
    if (useDark && kind != fdskStaticLine)
    {
        style &= ~WS_BORDER;
        exStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
    }

    SetFilterWindowStyle(hwnd, style);
    SetFilterWindowExStyle(hwnd, exStyle);

    if (kind == fdskStaticLine)
        ShowWindow(hwnd, useDark ? SW_HIDE : SW_SHOWNA);

    if (kind != fdskStaticLine)
        SetWindowTheme(hwnd, useDark ? L"" : NULL, NULL);

    if (kind == fdskCombo)
    {
        COMBOBOXINFO cbi = {0};
        cbi.cbSize = sizeof(cbi);
        if (GetComboBoxInfo(hwnd, &cbi) && cbi.hwndList != NULL && IsWindow(cbi.hwndList))
            SetWindowTheme(cbi.hwndList, useDark ? L"DarkMode_Explorer" : NULL, NULL);
    }
    else if (kind == fdskDateTime)
        SetFilterDateTimeCalendarColors(hwnd, useDark);

    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

static BOOL PaintFilterCriteriaDialogSectionLines(HWND hDialog, HDC paintDC)
{
    if (!DarkMode_ShouldUseDark())
        return FALSE;

    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = GetDC(hDialog);
    if (hdc == NULL)
        return FALSE;

    int lineIDs[] = {IDC_STATIC_2, IDC_STATIC_4, IDC_STATIC_6, IDC_STATIC_8};
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    COLORREF oldColor = SetDCPenColor(hdc, FILTER_DARK_SECTION_LINE);
    for (int i = 0; i < _countof(lineIDs); i++)
    {
        RECT rect;
        if (!GetFilterChildRectInDialog(hDialog, lineIDs[i], &rect))
            continue;

        int y = max(rect.top, min(rect.bottom - 1, (rect.top + rect.bottom) / 2));
        MoveToEx(hdc, rect.left, y, NULL);
        LineTo(hdc, rect.right, y);
    }
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldPen);

    if (paintDC == NULL)
        ReleaseDC(hDialog, hdc);
    return TRUE;
}

static void ApplyFilterCriteriaDialogTheme(HWND hDialog)
{
    if (hDialog == NULL || !IsWindow(hDialog))
        return;

    DarkMode_ApplyTitleBar(hDialog);
    DarkMode_ApplyListTreeThemeRecursive(hDialog);

    int lineIDs[] = {IDC_STATIC_2, IDC_STATIC_4, IDC_STATIC_6, IDC_STATIC_8};
    for (int i = 0; i < _countof(lineIDs); i++)
        ApplyFilterDarkSkin(GetDlgItem(hDialog, lineIDs[i]), fdskStaticLine);

    int editIDs[] = {IDC_FFA_SIZEMIN_VALUE, IDC_FFA_SIZEMAX_VALUE, IDC_FFA_TIMEDURING_VALUE};
    for (int i = 0; i < _countof(editIDs); i++)
        ApplyFilterDarkSkin(GetDlgItem(hDialog, editIDs[i]), fdskEdit);

    int upDownIDs[] = {IDC_FFA_SIZEMIN_UPDOWN, IDC_FFA_SIZEMAX_UPDOWN, IDC_FFA_TIMEDURING_UPDOWN};
    for (int i = 0; i < _countof(upDownIDs); i++)
        ApplyFilterDarkSkin(GetDlgItem(hDialog, upDownIDs[i]), fdskUpDown);

    int comboIDs[] = {IDC_FFA_SIZEMIN_UNITS, IDC_FFA_SIZEMAX_UNITS, IDC_FFA_TIMEDURING_UNITS};
    for (int i = 0; i < _countof(comboIDs); i++)
        ApplyFilterDarkSkin(GetDlgItem(hDialog, comboIDs[i]), fdskCombo);

    int dateTimeIDs[] = {IDC_FFA_FROM_DATE, IDC_FFA_FROM_TIME, IDC_FFA_TO_DATE, IDC_FFA_TO_TIME};
    for (int i = 0; i < _countof(dateTimeIDs); i++)
        ApplyFilterDarkSkin(GetDlgItem(hDialog, dateTimeIDs[i]), fdskDateTime);

    InvalidateRect(hDialog, NULL, TRUE);
    RedrawWindow(hDialog, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

static void RedrawFilterCriteriaDialogSkin(HWND hDialog)
{
    if (hDialog == NULL || !DarkMode_ShouldUseDark())
        return;

    int controlIDs[] = {
        IDC_STATIC_2, IDC_STATIC_4, IDC_STATIC_6, IDC_STATIC_8,
        IDC_FFA_SIZEMIN_VALUE, IDC_FFA_SIZEMAX_VALUE, IDC_FFA_TIMEDURING_VALUE,
        IDC_FFA_SIZEMIN_UPDOWN, IDC_FFA_SIZEMAX_UPDOWN, IDC_FFA_TIMEDURING_UPDOWN,
        IDC_FFA_SIZEMIN_UNITS, IDC_FFA_SIZEMAX_UNITS, IDC_FFA_TIMEDURING_UNITS,
        IDC_FFA_FROM_DATE, IDC_FFA_FROM_TIME, IDC_FFA_TO_DATE, IDC_FFA_TO_TIME};

    for (int i = 0; i < _countof(controlIDs); i++)
    {
        HWND hCtrl = GetDlgItem(hDialog, controlIDs[i]);
        if (hCtrl != NULL && IsWindow(hCtrl))
            RedrawWindow(hCtrl, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    }
}

static LRESULT CALLBACK FilterDarkSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    CFilterDarkSkinState* state = (CFilterDarkSkinState*)dwRefData;
    CFilterDarkSkinKind kind = state != NULL ? state->Kind : fdskEdit;

    switch (uMsg)
    {
    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark() && kind != fdskStaticLine)
        {
            if (kind == fdskEdit)
                PaintFilterDarkEditFrame(hwnd);
            else
                PaintFilterDarkFrame(hwnd);
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            switch (kind)
            {
            case fdskStaticLine:
                if (PaintFilterDarkStaticLine(hwnd, NULL))
                    return 0;
                break;

            case fdskCombo:
                if (PaintFilterDarkCombo(hwnd, NULL))
                    return 0;
                break;

            case fdskUpDown:
                if (PaintFilterDarkUpDown(hwnd, NULL))
                    return 0;
                break;

            case fdskDateTime:
                if (PaintFilterDarkDateTime(hwnd, NULL))
                    return 0;
                break;

            case fdskEdit:
            {
                LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
                PaintFilterDarkEditFrame(hwnd);
                return ret;
            }
            }
        }
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark())
        {
            switch (kind)
            {
            case fdskStaticLine:
                if (PaintFilterDarkStaticLine(hwnd, (HDC)wParam))
                    return 0;
                break;

            case fdskCombo:
                if (PaintFilterDarkCombo(hwnd, (HDC)wParam))
                    return 0;
                break;

            case fdskUpDown:
                if (PaintFilterDarkUpDown(hwnd, (HDC)wParam))
                    return 0;
                break;

            case fdskDateTime:
                if (PaintFilterDarkDateTime(hwnd, (HDC)wParam))
                    return 0;
                break;

            default:
                break;
            }
        }
        break;
    }

    case WM_ERASEBKGND:
    {
        DarkModeColors colors;
        if (DarkMode_GetColors(&colors))
        {
            RECT client;
            GetClientRect(hwnd, &client);
            FilterFillRectSolid((HDC)wParam, &client, kind == fdskStaticLine ? colors.DialogBackground : colors.InputBackground);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORLISTBOX:
    {
        HBRUSH hBrush = DarkMode_GetDialogCtlColorBrush(uMsg, (HDC)wParam, (HWND)lParam);
        if (hBrush != NULL)
            return (LRESULT)hBrush;
        break;
    }

    case WM_ENABLE:
    case WM_SETTEXT:
    case WM_SIZE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case CB_SETCURSEL:
    case CB_ADDSTRING:
    case CB_DELETESTRING:
    case CB_RESETCONTENT:
    case DTM_SETSYSTEMTIME:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, FilterDarkSkinSubclassProc, uIdSubclass);
        delete state;
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// leap year
#define IsLeapYear(_yr) ((!((_yr) % 400) || ((_yr) % 100) && !((_yr) % 4)) ? TRUE : FALSE)

// number of days in months
const static WORD DaysPerMonth[] =
    {
        31, //  JAN
        28, //  FEB
        31, //  MAR
        30, //  APR
        31, //  MAY
        30, //  JUN
        31, //  JUL
        31, //  AUG
        30, //  SEP
        31, //  OCT
        30, //  NOV
        31  //  DEC
};

//****************************************************************************
//
// CFilterCriteria
//

CFilterCriteria::CFilterCriteria()
{
    Reset();
}

void CFilterCriteria::Reset()
{
    // Attributes
    AttributesMask = 0; // 0 -> indeterminate checkbox state
    AttributesValue = 0;

    // Size Min/Max
    UseMinSize = FALSE;
    MinSize.Set(1, 0);
    MinSizeUnits = fcsuKB;
    UseMaxSize = FALSE;
    MaxSize.Set(1, 0);
    MaxSizeUnits = fcsuKB;

    // Date & Time
    TimeMode = fctmIgnore;
    DuringTime.Set(1, 0);
    DuringTimeUnits = fctuDays;
    UseFromDate = TRUE;
    UseFromTime = FALSE;
    UseToDate = TRUE;
    UseToTime = FALSE;

    // Set 'From' to one day back and 'To' to the current date
    SYSTEMTIME st;
    GetSystemTime(&st);
    SystemTimeToFileTime(&st, (FILETIME*)&To);
    From = To - (unsigned __int64)10000000 * 60 * 60 * 24; // one day back

    // internal variables
    NeedPrepare = FALSE; // in this given situation it is not necessary to call PrepareForTest
    UseMinTime = FALSE;
    UseMaxTime = FALSE;
}

void SizeToBytes(CQuadWord* out, const CQuadWord* in, CFilterCriteriaSizeUnitsEnum units)
{
    switch (units)
    {
    case fcsuBytes:
        *out = *in;
        break;
    case fcsuKB:
        out->SetUI64(in->Value * (unsigned __int64)1024);
        break;
    case fcsuMB:
        out->SetUI64(in->Value * (unsigned __int64)1024 * 1024);
        break;
    case fcsuGB:
        out->SetUI64(in->Value * (unsigned __int64)1024 * 1024 * 1024);
        break;
    case fcsuTB:
        out->SetUI64(in->Value * (unsigned __int64)1024 * 1024 * 1024 * 1024);
        break;
    case fcsuPB:
        out->SetUI64(in->Value * (unsigned __int64)1024 * 1024 * 1024 * 1024 * 1024);
        break;
    case fcsuEB:
        out->SetUI64(in->Value * (unsigned __int64)1024 * 1024 * 1024 * 1024 * 1024 * 1024);
        break;
    default:
    {
        TRACE_E("Unknown units=" << units);
    }
    }
}

// returns the maximum number that still fits into CQuadWord after conversion to bytes
void MaxSizeForUnits(CQuadWord* out, CFilterCriteriaSizeUnitsEnum units)
{
    switch (units)
    {
    case fcsuBytes:
        out->SetUI64((unsigned __int64)0xffffffffffffffff);
        break;
    case fcsuKB:
        out->SetUI64((unsigned __int64)0xffffffffffffffff / ((unsigned __int64)1024));
        break;
    case fcsuMB:
        out->SetUI64((unsigned __int64)0xffffffffffffffff / ((unsigned __int64)1024 * 1024));
        break;
    case fcsuGB:
        out->SetUI64((unsigned __int64)0xffffffffffffffff / ((unsigned __int64)1024 * 1024 * 1024));
        break;
    case fcsuTB:
        out->SetUI64((unsigned __int64)0xffffffffffffffff / ((unsigned __int64)1024 * 1024 * 1024 * 1024));
        break;
    case fcsuPB:
        out->SetUI64((unsigned __int64)0xffffffffffffffff / ((unsigned __int64)1024 * 1024 * 1024 * 1024 * 1024));
        break;
    case fcsuEB:
        out->SetUI64((unsigned __int64)0xffffffffffffffff / ((unsigned __int64)1024 * 1024 * 1024 * 1024 * 1024 * 1024));
        break;
    default:
    {
        TRACE_E("Unknown units=" << units);
    }
    }
}

// returns the maximum number that after conversion to seconds still fits into unsigned __int64
void MaxTimeForUnits(unsigned __int64* out, CFilterCriteriaTimeUnitsEnum units)
{
    // we'll only handle searching 300 years back :)
    switch (units)
    {
    case fctuSeconds:
        *out = (unsigned __int64)300 * 365 * 24 * 60 * 60;
        break;
    case fctuMinutes:
        *out = (unsigned __int64)300 * 365 * 24 * 60;
        break;
    case fctuHours:
        *out = (unsigned __int64)300 * 365 * 24;
        break;
    case fctuDays:
        *out = (unsigned __int64)300 * 365;
        break;
    case fctuWeeks:
        *out = (unsigned __int64)300 * 52;
        break;
    case fctuMonths:
        *out = (unsigned __int64)300 * 12;
        break;
    case fctuYears:
        *out = (unsigned __int64)300;
        break;
    default:
    {
        TRACE_E("Unknown units=" << units);
    }
    }
}

void CFilterCriteria::PrepareForTest()
{
    UseMinTime = FALSE;
    UseMaxTime = FALSE;

    // determine sizes in bytes
    if (UseMinSize)
        SizeToBytes(&MinSizeBytes, &MinSize, MinSizeUnits);

    if (UseMaxSize)
        SizeToBytes(&MaxSizeBytes, &MaxSize, MaxSizeUnits);

    if (TimeMode == fctmDuring)
    {
        SYSTEMTIME st; // 'st' can be modified, see the reset of hours, minutes, and seconds
        GetLocalTime(&st);
        // weekday is redundant information; we will not work with it
        st.wDayOfWeek = 0;
        SYSTEMTIME stCurrent = st; // current time that we won't modify

        unsigned __int64 offset = 0;
        switch (DuringTimeUnits)
        {
        case fctuSeconds:
        {
            offset = DuringTime.Value * (unsigned __int64)10000000;
            break;
        }

        case fctuMinutes:
        {
            offset = DuringTime.Value * (unsigned __int64)10000000 * 60;
            break;
        }

        case fctuHours:
        {
            offset = DuringTime.Value * (unsigned __int64)10000000 * 60 * 60;
            break;
        }

        case fctuDays:
        case fctuWeeks:
        {
            // in the case of days and weeks, the time value does not matter, we go by the date only
            st.wHour = 0;
            st.wMinute = 0;
            st.wSecond = 0;
            st.wMilliseconds = 0;

            unsigned __int64 days;
            if (DuringTimeUnits == fctuDays)
                days = DuringTime.Value; // days
            else
                days = DuringTime.Value * 7; // weeks

            offset = days * (unsigned __int64)10000000 * 60 * 60 * 24;

            break;
        }

        case fctuMonths:
        case fctuYears:
        {
            // in the case of months and years, the time value does not matter, we go by the date only
            st.wHour = 0;
            st.wMinute = 0;
            st.wSecond = 0;
            st.wMilliseconds = 0;

            if (DuringTimeUnits == fctuMonths)
            {
                unsigned __int64 i;
                for (i = 0; i < DuringTime.Value; i++)
                {
                    // we go back by one month
                    if (st.wMonth > 1)
                        st.wMonth--;
                    else
                    {
                        // January -> December and one year back
                        st.wMonth = 12;
                        st.wYear--;
                    }
                }
            }
            else
                st.wYear = st.wYear - (WORD)DuringTime.Value;

            // if the day does not exist, we move back to an existing one
            if (st.wMonth >= 1 && st.wMonth <= 12)
            {
                int maxDay = DaysPerMonth[st.wMonth - 1];
                if (IsLeapYear(st.wYear) && st.wMonth == 2)
                    maxDay++; // February has one extra day in a leap year
                if (st.wDay > maxDay)
                    st.wDay = maxDay;
            }
            break;
        }
        }

        if (SystemTimeToFileTime(&st, (FILETIME*)&MinTime))
        {
            MinTime -= offset;
            UseMinTime = TRUE;
        }
        if (SystemTimeToFileTime(&stCurrent, (FILETIME*)&MaxTime))
        {
            // since 2.52b1, we do not include future files in "Modified during"; see report on the forum
            // https://forum.altap.cz/viewtopic.php?t=2818
            UseMaxTime = TRUE;
        }
    }

    if (TimeMode == fctmFromTo)
    {
        if (UseFromDate)
        {
            SYSTEMTIME st;
            if (FileTimeToSystemTime((FILETIME*)&From, &st))
            {
                st.wMilliseconds = 0; // the control returns suspicious values, so we trim them
                if (!UseFromTime)
                {
                    st.wHour = 0;
                    st.wMinute = 0;
                    st.wSecond = 0;
                }
                if (SystemTimeToFileTime(&st, (FILETIME*)&MinTime))
                {
                    UseMinTime = TRUE;
                }
            }
        }

        if (UseToDate)
        {
            SYSTEMTIME st;
            if (FileTimeToSystemTime((FILETIME*)&To, &st))
            {
                st.wMilliseconds = 0; // the control returns suspicious values, so we trim them
                if (!UseToTime)
                {
                    st.wHour = 23;
                    st.wMinute = 59;
                    st.wSecond = 59;
                }
                if (SystemTimeToFileTime(&st, (FILETIME*)&MaxTime))
                {
                    // we want to be the absolute maximum time, sticking right at the very end of the interval
                    // at the resolution of FILETIME
                    MaxTime += 9999999; // almost one second

                    UseMaxTime = TRUE;
                }
            }
        }
    }

    NeedPrepare = FALSE;
}

BOOL CFilterCriteria::Test(DWORD attributes, const CQuadWord* size, const FILETIME* modified)
{
    if (NeedPrepare)
        TRACE_E("You must call PrepareForTest before Test method is called");

    // Attributes
    BOOL ok = ((attributes & AttributesMask) == (AttributesValue & AttributesMask));

    // Size Min/Max
    if (ok && (UseMinSize || UseMaxSize))
    {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            // it's a directory, so we'll skip it
            ok = FALSE;
        }
        else
        {
            // it's a file, so we can compare its size
            if (UseMinSize)
                ok = *size >= MinSizeBytes;
            if (ok && UseMaxSize)
                ok = *size <= MaxSizeBytes;
        }
    }

    // Date & Time
    if (ok && TimeMode != fctmIgnore)
    {
        unsigned __int64 time;
        if (!FileTimeToLocalFileTime(modified, (FILETIME*)&time))
            time = 0; // in case of an error, set to zero (so that the behavior is deterministic)

        if (UseMinTime)
            ok = time >= MinTime;

        if (ok && UseMaxTime)
            ok = time <= MaxTime;
    }

    return ok;
}

std::wstring CFilterCriteria::GetAdvancedDescription(BOOL& dirty)
{
    int count = 1;

    PrepareForTest();

    std::wstring description = LoadStrW(IDS_FFA_OPTIONS);

    if (AttributesMask != 0)
    {
        description += LoadStrW(IDS_FFA_ATTRIBUTES);
        description += L": ";
        if (AttributesMask & FILE_ATTRIBUTE_ARCHIVE)
            description += AttributesValue & FILE_ATTRIBUTE_ARCHIVE ? L"+A" : L"-A";
        if (AttributesMask & FILE_ATTRIBUTE_READONLY)
            description += AttributesValue & FILE_ATTRIBUTE_READONLY ? L"+R" : L"-R";
        if (AttributesMask & FILE_ATTRIBUTE_HIDDEN)
            description += AttributesValue & FILE_ATTRIBUTE_HIDDEN ? L"+H" : L"-H";
        if (AttributesMask & FILE_ATTRIBUTE_SYSTEM)
            description += AttributesValue & FILE_ATTRIBUTE_SYSTEM ? L"+S" : L"-S";
        if (AttributesMask & FILE_ATTRIBUTE_COMPRESSED)
            description += AttributesValue & FILE_ATTRIBUTE_COMPRESSED ? L"+C" : L"-C";
        if (AttributesMask & FILE_ATTRIBUTE_ENCRYPTED)
            description += AttributesValue & FILE_ATTRIBUTE_ENCRYPTED ? L"+E" : L"-E";
        if (AttributesMask & FILE_ATTRIBUTE_DIRECTORY)
            description += AttributesValue & FILE_ATTRIBUTE_DIRECTORY ? L"+D" : L"-D";
        count++;
    }

    if (UseMinSize || UseMaxSize)
    {
        if (count > 1)
            description += L", ";
        description += LoadStrW(IDS_FFA_SIZE);
        count++;
    }

    if (TimeMode != fctmIgnore)
    {
        if (count > 1)
            description += L", ";
        if (TimeMode == fctmFromTo || DuringTimeUnits >= fctuDays)
            description += LoadStrW(IDS_FFA_DATE);
        else
            description += LoadStrW(IDS_FFA_TIME);
        count++;
    }

    if (TimeMode == fctmFromTo &&
        (UseFromTime || UseToTime))
    {
        if (count > 1)
            description += L", ";
        description += LoadStrW(IDS_FFA_TIME);
        count++;
    }

    if (count == 1)
        description = LoadStrW(IDS_FFA_NONE);

    dirty = count > 1;
    return description;
}

BOOL CFilterCriteria::Save(HKEY hKey)
{
    // space optimization in the Registry: we store only "non-default values"
    // before saving, it is necessary to clear the key where we will store the data
    CFilterCriteria def;

    // Attributes
    if (AttributesMask != def.AttributesMask)
        SetValueW(hKey, FILTERCRITERIA_ATTRIBUTESMASK_REG, REG_DWORD, &AttributesMask, sizeof(DWORD));
    if (AttributesValue != def.AttributesValue)
        SetValueW(hKey, FILTERCRITERIA_ATTRIBUTESVALUE_REG, REG_DWORD, &AttributesValue, sizeof(DWORD));

    // Size Min/Max
    if (UseMinSize != def.UseMinSize)
        SetValueW(hKey, FILTERCRITERIA_USEMINSIZE_REG, REG_DWORD, &UseMinSize, sizeof(DWORD));
    if (MinSize != def.MinSize)
    {
        SetValueW(hKey, FILTERCRITERIA_MINSIZELO_REG, REG_DWORD, &MinSize.LoDWord, sizeof(DWORD));
        SetValueW(hKey, FILTERCRITERIA_MINSIZEHI_REG, REG_DWORD, &MinSize.HiDWord, sizeof(DWORD));
    }
    if (MinSizeUnits != def.MinSizeUnits)
        SetValueW(hKey, FILTERCRITERIA_MINSIZEUNITS_REG, REG_DWORD, &MinSizeUnits, sizeof(DWORD));
    if (UseMaxSize != def.UseMaxSize)
        SetValueW(hKey, FILTERCRITERIA_USEMAXSIZE_REG, REG_DWORD, &UseMaxSize, sizeof(DWORD));
    if (MaxSize != def.MaxSize)
    {
        SetValueW(hKey, FILTERCRITERIA_MAXSIZELO_REG, REG_DWORD, &MaxSize.LoDWord, sizeof(DWORD));
        SetValueW(hKey, FILTERCRITERIA_MAXSIZEHI_REG, REG_DWORD, &MaxSize.HiDWord, sizeof(DWORD));
    }
    if (MaxSizeUnits != def.MaxSizeUnits)
        SetValueW(hKey, FILTERCRITERIA_MAXSIZEUNITS_REG, REG_DWORD, &MaxSizeUnits, sizeof(DWORD));

    // Date & Time
    if (TimeMode != def.TimeMode)
        SetValueW(hKey, FILTERCRITERIA_TIMEMODE_REG, REG_DWORD, &TimeMode, sizeof(DWORD));
    if (DuringTime != def.DuringTime)
    {
        SetValueW(hKey, FILTERCRITERIA_DURINGTIMELO_REG, REG_DWORD, &DuringTime.LoDWord, sizeof(DWORD));
        SetValueW(hKey, FILTERCRITERIA_DURINGTIMEHI_REG, REG_DWORD, &DuringTime.HiDWord, sizeof(DWORD));
    }
    if (DuringTimeUnits != def.DuringTimeUnits)
        SetValueW(hKey, FILTERCRITERIA_DURINGUNITS_REG, REG_DWORD, &DuringTimeUnits, sizeof(DWORD));
    if (UseFromDate != def.UseFromDate)
        SetValueW(hKey, FILTERCRITERIA_USEFROMDATE_REG, REG_DWORD, &UseFromDate, sizeof(DWORD));
    if (UseFromTime != def.UseFromTime)
        SetValueW(hKey, FILTERCRITERIA_USEFROMTIME_REG, REG_DWORD, &UseFromTime, sizeof(DWORD));

    // note: starting with 2.53 we'll "forget" the times in disabled FROM/TO controls (see TimeMode == fctmFromTo condition)
    // if users request a return to the old behavior, we could by default disable the checkboxes in the Date controls,
    // which would not meet the UseFromDate/UseToDate condition; we would enable the checkbox only when the user enables the control via radio buttons
    if (From != def.From && TimeMode == fctmFromTo && (UseFromDate || UseFromTime)) // there's no point in storing times when they are not used (controls would insert the current time)
    {
        SetValueW(hKey, FILTERCRITERIA_FROMLO_REG, REG_DWORD, &(((FILETIME*)&From)->dwLowDateTime), sizeof(DWORD));
        SetValueW(hKey, FILTERCRITERIA_FROMHI_REG, REG_DWORD, &(((FILETIME*)&From)->dwHighDateTime), sizeof(DWORD));
    }
    if (UseToDate != def.UseToDate)
        SetValueW(hKey, FILTERCRITERIA_USETODATE_REG, REG_DWORD, &UseToDate, sizeof(DWORD));
    if (UseToTime != def.UseToTime)
        SetValueW(hKey, FILTERCRITERIA_USETOTIME_REG, REG_DWORD, &UseToTime, sizeof(DWORD));
    if (To != def.To && TimeMode == fctmFromTo && (UseToDate || UseToTime)) // there's no point in storing times when they are not used (controls would insert the current time)
    {
        SetValueW(hKey, FILTERCRITERIA_TOLO_REG, REG_DWORD, &(((FILETIME*)&To)->dwLowDateTime), sizeof(DWORD));
        SetValueW(hKey, FILTERCRITERIA_TOHI_REG, REG_DWORD, &(((FILETIME*)&To)->dwHighDateTime), sizeof(DWORD));
    }
    return TRUE;
}

BOOL CFilterCriteria::Load(HKEY hKey)
{
    // Attributes
    GetValueW(hKey, FILTERCRITERIA_ATTRIBUTESMASK_REG, REG_DWORD, &AttributesMask, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_ATTRIBUTESVALUE_REG, REG_DWORD, &AttributesValue, sizeof(DWORD));

    // Size Min/Max
    GetValueW(hKey, FILTERCRITERIA_USEMINSIZE_REG, REG_DWORD, &UseMinSize, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_MINSIZELO_REG, REG_DWORD, &MinSize.LoDWord, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_MINSIZEHI_REG, REG_DWORD, &MinSize.HiDWord, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_MINSIZEUNITS_REG, REG_DWORD, &MinSizeUnits, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_USEMAXSIZE_REG, REG_DWORD, &UseMaxSize, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_MAXSIZELO_REG, REG_DWORD, &MaxSize.LoDWord, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_MAXSIZEHI_REG, REG_DWORD, &MaxSize.HiDWord, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_MAXSIZEUNITS_REG, REG_DWORD, &MaxSizeUnits, sizeof(DWORD));
    // Date & Time
    GetValueW(hKey, FILTERCRITERIA_TIMEMODE_REG, REG_DWORD, &TimeMode, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_DURINGTIMELO_REG, REG_DWORD, &DuringTime.LoDWord, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_DURINGTIMEHI_REG, REG_DWORD, &DuringTime.HiDWord, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_DURINGUNITS_REG, REG_DWORD, &DuringTimeUnits, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_USEFROMDATE_REG, REG_DWORD, &UseFromDate, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_USEFROMTIME_REG, REG_DWORD, &UseFromTime, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_FROMLO_REG, REG_DWORD, &(((FILETIME*)&From)->dwLowDateTime), sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_FROMHI_REG, REG_DWORD, &(((FILETIME*)&From)->dwHighDateTime), sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_USETODATE_REG, REG_DWORD, &UseToDate, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_USETOTIME_REG, REG_DWORD, &UseToTime, sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_TOLO_REG, REG_DWORD, &(((FILETIME*)&To)->dwLowDateTime), sizeof(DWORD));
    GetValueW(hKey, FILTERCRITERIA_TOHI_REG, REG_DWORD, &(((FILETIME*)&To)->dwHighDateTime), sizeof(DWORD));

    NeedPrepare = TRUE;

    return TRUE;
}

BOOL CFilterCriteria::LoadOld(HKEY hKey)
{
    // attributes
    int archive = 2;
    int readOnly = 2;
    int hidden = 2;
    int system = 2;
    int compressed = 2;
    int directory = 2;

    GetValueW(hKey, OLD_FINDOPTIONSITEM_ARCHIVE_REG, REG_DWORD, &archive, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_READONLY_REG, REG_DWORD, &readOnly, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_HIDDEN_REG, REG_DWORD, &hidden, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_SYSTEM_REG, REG_DWORD, &system, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_COMPRESSED_REG, REG_DWORD, &compressed, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_DIRECTORY_REG, REG_DWORD, &directory, sizeof(DWORD));

    AttributesMask = 0;
    AttributesValue = 0;

    if (archive != 2)
    {
        AttributesMask |= FILE_ATTRIBUTE_ARCHIVE;
        if (archive == 1)
            AttributesValue |= FILE_ATTRIBUTE_ARCHIVE;
    }

    if (readOnly != 2)
    {
        AttributesMask |= FILE_ATTRIBUTE_READONLY;
        if (readOnly == 1)
            AttributesValue |= FILE_ATTRIBUTE_READONLY;
    }

    if (hidden != 2)
    {
        AttributesMask |= FILE_ATTRIBUTE_HIDDEN;
        if (hidden == 1)
            AttributesValue |= FILE_ATTRIBUTE_HIDDEN;
    }

    if (system != 2)
    {
        AttributesMask |= FILE_ATTRIBUTE_SYSTEM;
        if (system == 1)
            AttributesValue |= FILE_ATTRIBUTE_SYSTEM;
    }

    if (compressed != 2)
    {
        AttributesMask |= FILE_ATTRIBUTE_COMPRESSED;
        if (compressed == 1)
            AttributesValue |= FILE_ATTRIBUTE_COMPRESSED;
    }

    if (directory != 2)
    {
        AttributesMask |= FILE_ATTRIBUTE_DIRECTORY;
        if (directory == 1)
            AttributesValue |= FILE_ATTRIBUTE_DIRECTORY;
    }

    // size
    UseMinSize = FALSE;
    UseMaxSize = FALSE;

    int sizeAction = 0;
    CQuadWord size = CQuadWord(0, 0);
    GetValueW(hKey, OLD_FINDOPTIONSITEM_SIZEACTION_REG, REG_DWORD, &sizeAction, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_SIZELO_REG, REG_DWORD, &size.LoDWord, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_SIZEHI_REG, REG_DWORD, &size.HiDWord, sizeof(DWORD));

    switch (sizeAction) // size:
    {
    case 1: // greater than
    {
        UseMinSize = TRUE;
        MinSize.SetUI64(size.Value + (unsigned __int64)1);
        MinSizeUnits = fcsuBytes;
        break;
    }

    case 2: // greater or equal to
    {
        UseMinSize = TRUE;
        MinSize = size;
        MinSizeUnits = fcsuBytes;
        break;
    }

    case 3: // equal to
    {
        UseMinSize = TRUE;
        MinSize = size;
        MinSizeUnits = fcsuBytes;
        UseMaxSize = TRUE;
        MaxSize = size;
        MaxSizeUnits = fcsuBytes;
        break;
    }

    case 4: // smaller or equal to
    {
        UseMaxSize = TRUE;
        MaxSize = size;
        MaxSizeUnits = fcsuBytes;
        break;
    }

    case 5: // smaller than
    {
        UseMaxSize = TRUE;
        if (size.Value > (unsigned __int64)0)
            MaxSize.SetUI64(size.Value - (unsigned __int64)1);
        MaxSizeUnits = fcsuBytes;
        break;
    }
    }

    // date/time
    UseFromDate = FALSE;
    UseToDate = FALSE;
    int dateAction;
    int day;
    int month;
    int year;
    int timeAction;
    int hour;
    int minute;
    int second;
    GetValueW(hKey, OLD_FINDOPTIONSITEM_DATEACTION_REG, REG_DWORD, &dateAction, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_DAY_REG, REG_DWORD, &day, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_MONTH_REG, REG_DWORD, &month, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_YEAR_REG, REG_DWORD, &year, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_TIMEACTION_REG, REG_DWORD, &timeAction, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_HOUR_REG, REG_DWORD, &hour, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_MINUTE_REG, REG_DWORD, &minute, sizeof(DWORD));
    GetValueW(hKey, OLD_FINDOPTIONSITEM_SECOND_REG, REG_DWORD, &second, sizeof(DWORD));

    SYSTEMTIME st;
    ZeroMemory(&st, sizeof(st));
    st.wYear = year;
    st.wMonth = month;
    st.wDay = day;
    st.wHour = hour;
    st.wMinute = minute;
    st.wSecond = second;

    switch (dateAction)
    {
    case 1: // older than
    case 2: // older or equal to
    {
        if (timeAction == 0)
        {
            st.wHour = 0;
            st.wMinute = 0;
            st.wSecond = 0;
        }
        if (SystemTimeToFileTime(&st, (FILETIME*)&To))
        {
            UseToDate = TRUE;
            if (timeAction == 1)
                UseToTime = TRUE;
            if (dateAction == 1) // older than
            {
                if (timeAction == 1)
                    To -= (unsigned __int64)10000000; // second
                else
                    To -= (unsigned __int64)10000000 * 60 * 60 * 24; // day
            }
        }
        break;
    }

    case 3: // equal to
    {
        if (timeAction == 0)
        {
            st.wHour = 0;
            st.wMinute = 0;
            st.wSecond = 0;
        }
        if (SystemTimeToFileTime(&st, (FILETIME*)&From))
        {
            To = From;
            UseFromDate = TRUE;
            UseToDate = TRUE;

            if (timeAction == 1)
            {
                UseFromTime = TRUE;
                UseToTime = TRUE;
            }
        }
        break;
    }

    case 4: // newer or equal to
    case 5: // newer than
    {
        if (timeAction == 0)
        {
            st.wHour = 0;
            st.wMinute = 0;
            st.wSecond = 0;
        }
        if (SystemTimeToFileTime(&st, (FILETIME*)&From))
        {
            UseFromDate = TRUE;
            if (timeAction == 1)
                UseFromTime = TRUE;
            if (dateAction == 5) // newer than
            {
                if (timeAction == 1)
                    From += (unsigned __int64)10000000; // second
                else
                    From += (unsigned __int64)10000000 * 60 * 60 * 24; // day
            }
        }
        break;
    }
    }

    if (UseFromDate || UseToDate)
        TimeMode = fctmFromTo;

    NeedPrepare = TRUE;
    return TRUE;
}

//****************************************************************************
//
// CFilterCriteriaDialog
//

CFilterCriteriaDialog::CFilterCriteriaDialog(HWND hParent, CFilterCriteria* data, BOOL enableDirectory)
    : CCommonDialog(HLanguage, IDD_FINDADVANCED, IDD_FINDADVANCED, hParent)
{
    Data = data;
    EnableDirectory = enableDirectory;
}

BOOL QuadWordEditLineTransfer(CTransferInfo* ti, int ctrlID, CQuadWord& value)
{
    BOOL ret = TRUE;
    HWND HWindow;
    wchar_t buff[50];
    if (ti->GetControl(HWindow, ctrlID))
    {
        switch (ti->Type)
        {
        case ttDataToWindow:
        {
            SendMessage(HWindow, EM_LIMITTEXT, 21, 0);
            SendMessage(HWindow, WM_SETTEXT, 0, (LPARAM)_ui64tow(value.Value, buff, 10));
            break;
        }

        case ttDataFromWindow:
        {
            SendMessage(HWindow, WM_GETTEXT, 22, (LPARAM)buff);
            value.Value = _wcstoui64(buff, NULL, 10);
            break;
        }
        }
    }
    return ret;
}

BOOL AttributeCheckBox(CTransferInfo* ti, int ctrlID, DWORD attribute, DWORD* attrMask, DWORD* attrValue)
{
    BOOL ret = TRUE;
    HWND HWindow;
    if (ti->GetControl(HWindow, ctrlID))
    {
        switch (ti->Type)
        {
        case ttDataToWindow:
        {
            int value = 2; // indeterminate state
            if (((*attrMask) & attribute) != 0)
            {
                if (((*attrValue) & attribute) != 0)
                    value = 1; // checked
                else
                    value = 0; // unchecked
            }
            SendMessage(HWindow, BM_SETCHECK, value, 0);
            break;
        }

        case ttDataFromWindow:
        {
            int value = (int)SendMessage(HWindow, BM_GETCHECK, 0, 0);
            if (value == 2)
            {
                *attrMask &= ~attribute;
                *attrValue &= ~attribute; // just for the form
            }
            else
            {
                *attrMask |= attribute;
                if (value == 1)
                    *attrValue |= attribute;
                else
                    *attrValue &= ~attribute;
            }

            break;
        }
        }
    }
    return ret;
}

void CFilterCriteriaDialog::FillUnits(int editID, int comboID, int* units, BOOL appendSizes)
{
    HWND hEdit = GetDlgItem(HWindow, editID);
    HWND hCombo = GetDlgItem(HWindow, comboID);

    // extract the control value from the edit line
    wchar_t buff[100];
    wchar_t buff2[100];
    SendMessageW(hEdit, WM_GETTEXT, 22, (LPARAM)buff);
    buff[21] = 0;
    CQuadWord editValue;
    editValue.Value = StrToUInt64W(buff, (int)wcslen(buff));

    BOOL dirty = FALSE; // if we keep it clean
    SendMessage(hCombo, WM_SETREDRAW, FALSE, 0);
    int curSel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);

    int origCount = (int)SendMessage(hCombo, CB_GETCOUNT, 0, 0);

    int i;
    for (i = 0; units[i] != -1; i++)
    {
        ExpandPluralStringW(buff, 100, LoadStrW(units[i]), 1, &editValue);

        if (curSel >= 0 && !dirty && i == curSel)
        {
            SendMessageW(hCombo, CB_GETLBTEXT, i, (LPARAM)buff2);
            if (wcscmp(buff, buff2) != 0)
                dirty = TRUE;
        }

        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)buff);
    }

    for (i = 0; i < origCount; i++)
        SendMessage(hCombo, CB_DELETESTRING, 0, 0);

    if (appendSizes)
    {
        int sizes[] = {IDS_SIZE_KB, IDS_SIZE_MB, IDS_SIZE_GB, IDS_SIZE_TB, IDS_SIZE_PB, IDS_SIZE_EB, 0};
        for (i = 0; sizes[i] != 0; i++)
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)LoadStrW(sizes[i]));
    }

    SendMessage(hCombo, CB_SETCURSEL, curSel, 0);
    SendMessage(hCombo, WM_SETREDRAW, TRUE, 0);
    if (dirty)
        InvalidateRect(hCombo, NULL, FALSE);
}

void CFilterCriteriaDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CFilterCriteriaDialog::Transfer()");

    // Attributes
    AttributeCheckBox(&ti, IDC_FFA_ATTRARCHIVE, FILE_ATTRIBUTE_ARCHIVE, &Data->AttributesMask, &Data->AttributesValue);
    AttributeCheckBox(&ti, IDC_FFA_ATTRREADONLY, FILE_ATTRIBUTE_READONLY, &Data->AttributesMask, &Data->AttributesValue);
    AttributeCheckBox(&ti, IDC_FFA_ATTRHIDDEN, FILE_ATTRIBUTE_HIDDEN, &Data->AttributesMask, &Data->AttributesValue);
    AttributeCheckBox(&ti, IDC_FFA_ATTRSYSTEM, FILE_ATTRIBUTE_SYSTEM, &Data->AttributesMask, &Data->AttributesValue);
    AttributeCheckBox(&ti, IDC_FFA_ATTRCOMPRESSED, FILE_ATTRIBUTE_COMPRESSED, &Data->AttributesMask, &Data->AttributesValue);
    AttributeCheckBox(&ti, IDC_FFA_ATTRENCRYPTED, FILE_ATTRIBUTE_ENCRYPTED, &Data->AttributesMask, &Data->AttributesValue);
    AttributeCheckBox(&ti, IDC_FFA_ATTRDIRECTORY, FILE_ATTRIBUTE_DIRECTORY, &Data->AttributesMask, &Data->AttributesValue);

    // Size Min/Max
    ti.CheckBox(IDC_FFA_SIZEMIN, Data->UseMinSize);
    QuadWordEditLineTransfer(&ti, IDC_FFA_SIZEMIN_VALUE, Data->MinSize);
    if (ti.Type == ttDataToWindow)
        SendDlgItemMessage(HWindow, IDC_FFA_SIZEMIN_UNITS, CB_SETCURSEL, (WPARAM)Data->MinSizeUnits, 0);
    else
        Data->MinSizeUnits = (CFilterCriteriaSizeUnitsEnum)SendDlgItemMessage(HWindow, IDC_FFA_SIZEMIN_UNITS, CB_GETCURSEL, 0, 0);
    ti.CheckBox(IDC_FFA_SIZEMAX, Data->UseMaxSize);
    QuadWordEditLineTransfer(&ti, IDC_FFA_SIZEMAX_VALUE, Data->MaxSize);
    if (ti.Type == ttDataToWindow)
        SendDlgItemMessage(HWindow, IDC_FFA_SIZEMAX_UNITS, CB_SETCURSEL, (WPARAM)Data->MaxSizeUnits, 0);
    else
        Data->MaxSizeUnits = (CFilterCriteriaSizeUnitsEnum)SendDlgItemMessage(HWindow, IDC_FFA_SIZEMAX_UNITS, CB_GETCURSEL, 0, 0);

    // Date & Time
    int mode = Data->TimeMode;
    ti.RadioButton(IDC_FFA_TIMEIGNORE, fctmIgnore, mode);
    ti.RadioButton(IDC_FFA_TIMEDURING, fctmDuring, mode);
    ti.RadioButton(IDC_FFA_TIMEFROMTO, fctmFromTo, mode);
    Data->TimeMode = (CFilterCriteriaTimeModeEnum)mode;

    QuadWordEditLineTransfer(&ti, IDC_FFA_TIMEDURING_VALUE, Data->DuringTime);
    if (ti.Type == ttDataToWindow)
        SendDlgItemMessage(HWindow, IDC_FFA_TIMEDURING_UNITS, CB_SETCURSEL, (WPARAM)Data->DuringTimeUnits, 0);
    else
        Data->DuringTimeUnits = (CFilterCriteriaTimeUnitsEnum)SendDlgItemMessage(HWindow, IDC_FFA_TIMEDURING_UNITS, CB_GETCURSEL, 0, 0);

    if (ti.Type == ttDataToWindow)
    {
        SYSTEMTIME st;
        FileTimeToSystemTime((FILETIME*)&Data->From, &st);
        DateTime_SetSystemtime(GetDlgItem(HWindow, IDC_FFA_FROM_DATE),
                               Data->UseFromDate ? GDT_VALID : GDT_NONE, &st);
        DateTime_SetSystemtime(GetDlgItem(HWindow, IDC_FFA_FROM_TIME),
                               Data->UseFromTime ? GDT_VALID : GDT_NONE, &st);
        FileTimeToSystemTime((FILETIME*)&Data->To, &st);
        DateTime_SetSystemtime(GetDlgItem(HWindow, IDC_FFA_TO_DATE),
                               Data->UseToDate ? GDT_VALID : GDT_NONE, &st);
        DateTime_SetSystemtime(GetDlgItem(HWindow, IDC_FFA_TO_TIME),
                               Data->UseToTime ? GDT_VALID : GDT_NONE, &st);
    }
    else
    {
        SYSTEMTIME st;
        SYSTEMTIME st2;

        Data->UseFromDate = DateTime_GetSystemtime(GetDlgItem(HWindow, IDC_FFA_FROM_DATE), &st) == GDT_VALID;
        Data->UseFromTime = DateTime_GetSystemtime(GetDlgItem(HWindow, IDC_FFA_FROM_TIME), &st2) == GDT_VALID;
        if (Data->UseFromTime)
        {
            st.wHour = st2.wHour;
            st.wMinute = st2.wMinute;
            st.wSecond = st2.wSecond;
        }
        else
        {
            st.wHour = 0;
            st.wMinute = 0;
            st.wSecond = 0;
        }
        st.wMilliseconds = 0;
        if (!SystemTimeToFileTime(&st, (FILETIME*)&Data->From))
            Data->From = (unsigned __int64)0; // error for validation

        Data->UseToDate = DateTime_GetSystemtime(GetDlgItem(HWindow, IDC_FFA_TO_DATE), &st) == GDT_VALID;
        Data->UseToTime = DateTime_GetSystemtime(GetDlgItem(HWindow, IDC_FFA_TO_TIME), &st2) == GDT_VALID;
        if (Data->UseToTime)
        {
            st.wHour = st2.wHour;
            st.wMinute = st2.wMinute;
            st.wSecond = st2.wSecond;
        }
        else
        {
            st.wHour = 0;
            st.wMinute = 0;
            st.wSecond = 0;
        }
        st.wMilliseconds = 0;
        if (!SystemTimeToFileTime(&st, (FILETIME*)&Data->To))
            Data->To = (unsigned __int64)0; // error for validation
    }

    if (ti.Type == ttDataToWindow)
        EnableControls();
    else
        Data->NeedPrepare = TRUE;
}

void CFilterCriteriaDialog::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CFilterCriteriaDialog::Validate()");

    // we store a pointer to the data
    CFilterCriteria* oldData = Data;
    // and we switch to a temporary copy
    CFilterCriteria data;
    memmove(&data, Data, sizeof(data));
    Data = &data;

    TransferData(ttDataFromWindow);

    // Size Min/Max

    if (ti.IsGood() && Data->UseMinSize)
    {
        // the value converted to bytes must fit into unsigned __int64
        CQuadWord limit;
        MaxSizeForUnits(&limit, Data->MinSizeUnits);
        if (Data->MinSize > limit)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_SIZE_LIMIT_16EB));
            ti.ErrorOn(IDC_FFA_SIZEMIN_VALUE);
        }
    }

    if (ti.IsGood() && Data->UseMaxSize)
    {
        // the value converted to bytes must fit into unsigned __int64
        CQuadWord limit;
        MaxSizeForUnits(&limit, Data->MaxSizeUnits);
        if (Data->MaxSize > limit)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_SIZE_LIMIT_16EB));
            ti.ErrorOn(IDC_FFA_SIZEMAX_VALUE);
        }
    }

    if (ti.IsGood() && Data->UseMinSize && Data->UseMaxSize)
    {
        // if the user sets both limits, the maximum must not be lower than the minimum
        CQuadWord minSizeBytes;
        CQuadWord maxSizeBytes;
        SizeToBytes(&minSizeBytes, &Data->MinSize, Data->MinSizeUnits);
        SizeToBytes(&maxSizeBytes, &Data->MaxSize, Data->MaxSizeUnits);
        if (maxSizeBytes < minSizeBytes)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_SIZE_MAX_MIN));
            ti.ErrorOn(IDC_FFA_SIZEMAX_VALUE);
        }
    }

    if (Data->TimeMode == fctmDuring)
    {
        // the value must be at least 1
        if (ti.IsGood() && Data->DuringTime.Value < 1)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_TIME_MIN));
            ti.ErrorOn(IDC_FFA_TIMEDURING_VALUE);
        }

        if (ti.IsGood())
        {
            // the value must not exceed the defined limit
            unsigned __int64 limit;
            MaxTimeForUnits(&limit, Data->DuringTimeUnits);
            if (Data->DuringTime.Value > limit)
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_TIME_MAX));
                ti.ErrorOn(IDC_FFA_TIMEDURING_VALUE);
            }
        }

        if (ti.IsGood())
        {
            // verify that in PrepareForTest the time was successfully converted to an existing date
            Data->PrepareForTest();
            if (!Data->UseMinTime)
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INVALIDDATE));
                ti.ErrorOn(IDC_FFA_TIMEDURING_VALUE);
            }
        }
    }

    if (Data->TimeMode == fctmFromTo)
    {
        // FROM
        if (ti.IsGood() && !Data->UseFromDate && !Data->UseToDate)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_SPECIFY_DATE));
            ti.ErrorOn(IDC_FFA_FROM_DATE);
        }

        if (ti.IsGood() && Data->UseFromTime && Data->From == (unsigned __int64)0)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INVALIDDATE));
            ti.ErrorOn(IDC_FFA_FROM_DATE);
        }

        // TO
        if (ti.IsGood() && Data->UseToTime && Data->To == (unsigned __int64)0)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INVALIDDATE));
            ti.ErrorOn(IDC_FFA_TO_DATE);
        }

        if (ti.IsGood() && Data->UseFromDate && Data->UseToDate)
        {
            Data->PrepareForTest();
            if (Data->MinTime > Data->MaxTime)
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_TIME_MAX_MIN));
                ti.ErrorOn(IDC_FFA_TO_DATE);
            }
        }
    }

    // return to the original date
    Data = oldData;
}

void CFilterCriteriaDialog::EnableControls()
{
    CALL_STACK_MESSAGE1("CFilterCriteriaDialog::EnableControls()");
    BOOL checked;
    HWND hFocus = GetFocus();

    BOOL minOrMax = FALSE;

    // Size Min
    checked = IsDlgButtonChecked(HWindow, IDC_FFA_SIZEMIN) == BST_CHECKED;
    minOrMax |= checked;
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_SIZEMIN_VALUE), checked);
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_SIZEMIN_UPDOWN), checked);
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_SIZEMIN_UNITS), checked);

    // Size Max
    checked = IsDlgButtonChecked(HWindow, IDC_FFA_SIZEMAX) == BST_CHECKED;
    minOrMax |= checked;
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_SIZEMAX_VALUE), checked);
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_SIZEMAX_UPDOWN), checked);
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_SIZEMAX_UNITS), checked);

    if (minOrMax)
        CheckDlgButton(HWindow, IDC_FFA_ATTRDIRECTORY, BST_INDETERMINATE);
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_ATTRDIRECTORY), EnableDirectory && !minOrMax);

    // Time mode (Ignore/During/FromTo)
    CFilterCriteriaTimeModeEnum timeMode = fctmIgnore;
    if (IsDlgButtonChecked(HWindow, IDC_FFA_TIMEDURING) == BST_CHECKED)
        timeMode = fctmDuring;
    if (IsDlgButtonChecked(HWindow, IDC_FFA_TIMEFROMTO) == BST_CHECKED)
        timeMode = fctmFromTo;

    // mode During
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_TIMEDURING_VALUE), timeMode == fctmDuring);
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_TIMEDURING_UPDOWN), timeMode == fctmDuring);
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_TIMEDURING_UNITS), timeMode == fctmDuring);

    // mode From To
    SYSTEMTIME st;
    // from
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_FROM_DATE), timeMode == fctmFromTo);
    BOOL enabledFrom = (timeMode == fctmFromTo) &&
                       DateTime_GetSystemtime(GetDlgItem(HWindow, IDC_FFA_FROM_DATE), &st) == GDT_VALID;
    if (timeMode == fctmFromTo && !enabledFrom)
    {
        DateTime_GetSystemtime(GetDlgItem(HWindow, IDC_FFA_FROM_TIME), &st);
        DateTime_SetSystemtime(GetDlgItem(HWindow, IDC_FFA_FROM_TIME), GDT_NONE, &st);
    }
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_FROM_TIME), enabledFrom);

    // to
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_TO_DATE), timeMode == fctmFromTo);
    BOOL enabledTo = (timeMode == fctmFromTo) &&
                     DateTime_GetSystemtime(GetDlgItem(HWindow, IDC_FFA_TO_DATE), &st) == GDT_VALID;
    if (timeMode == fctmFromTo && !enabledTo)
    {
        DateTime_GetSystemtime(GetDlgItem(HWindow, IDC_FFA_TO_TIME), &st);
        DateTime_SetSystemtime(GetDlgItem(HWindow, IDC_FFA_TO_TIME), GDT_NONE, &st);
    }
    EnableWindow(GetDlgItem(HWindow, IDC_FFA_TO_TIME), enabledTo);

    // if the user pressed the Reset button via hot key, it could
    // disable the control that currently has focus; we handle it
    if (hFocus != NULL && !IsWindowEnabled(hFocus))
        SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDC_FFA_SIZEMIN), TRUE);

    RedrawFilterCriteriaDialogSkin(HWindow);
}

INT_PTR
CFilterCriteriaDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CFilterCriteriaDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_PAINT:
    {
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        PaintFilterCriteriaDialogSectionLines(HWindow, NULL);
        return ret;
    }

    case WM_PRINTCLIENT:
    {
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        PaintFilterCriteriaDialogSectionLines(HWindow, (HDC)wParam);
        return ret;
    }

    case WM_INITDIALOG:
    {
        // attach the UpDown control to the edit line
        int resID[] = {IDC_FFA_SIZEMIN_VALUE, IDC_FFA_SIZEMAX_VALUE, IDC_FFA_TIMEDURING_VALUE, -1};
        int upDownID[] = {IDC_FFA_SIZEMIN_UPDOWN, IDC_FFA_SIZEMAX_UPDOWN, IDC_FFA_TIMEDURING_UPDOWN};
        int i;
        for (i = 0; resID[i] != -1; i++)
        {
            HWND hEdit = GetDlgItem(HWindow, resID[i]);
            HWND hWnd = CreateUpDownControl(WS_VISIBLE | WS_CHILD | WS_BORDER | UDS_SETBUDDYINT |
                                                UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS,
                                            0, 0, 0, 0, HWindow, upDownID[i], HInstance,
                                            hEdit, 10000, i == 2 ? 1 : 0, 0);
            // move the UpDown control in the z-order right after the edit line, otherwise
            // on a slow machine the dialog display looked odd
            // (the UpDown was drawn after all the other controls)
            SetWindowPos(hWnd, hEdit, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        }
        if (!EnableDirectory)
            EnableWindow(GetDlgItem(HWindow, IDC_FFA_ATTRDIRECTORY), FALSE);

        ApplyFilterCriteriaDialogTheme(HWindow);
        break;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    {
        HBRUSH hBrush = DarkMode_GetDialogCtlColorBrush(uMsg, (HDC)wParam, (HWND)lParam);
        if (hBrush != NULL)
            return (INT_PTR)hBrush;
        break;
    }

    case WM_COMMAND:
    {
        // a change on some combo box
        if (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == CBN_SELCHANGE)
            EnableControls();

        // a change in controlling edit line
        if (HIWORD(wParam) == EN_UPDATE)
        {
            int sizeUnits[] = {IDS_FFA_BYTES,
                               -1};

            int timeUnits[] = {IDS_FFA_SECONDS,
                               IDS_FFA_MINUTES,
                               IDS_FFA_HOURS,
                               IDS_FFA_DAYS,
                               IDS_FFA_WEEKS,
                               IDS_FFA_MONTHS,
                               IDS_FFA_YEARS,
                               -1};

            if (LOWORD(wParam) == IDC_FFA_SIZEMIN_VALUE)
            {
                // size min
                FillUnits(IDC_FFA_SIZEMIN_VALUE, IDC_FFA_SIZEMIN_UNITS, sizeUnits, TRUE);
            }
            if (LOWORD(wParam) == IDC_FFA_SIZEMAX_VALUE)
            {
                // size max
                FillUnits(IDC_FFA_SIZEMAX_VALUE, IDC_FFA_SIZEMAX_UNITS, sizeUnits, TRUE);
            }
            if (LOWORD(wParam) == IDC_FFA_TIMEDURING_VALUE)
            {
                // date during
                FillUnits(IDC_FFA_TIMEDURING_VALUE, IDC_FFA_TIMEDURING_UNITS, timeUnits, FALSE);
            }
        }

        if (LOWORD(wParam) == IDC_FFA_DEFAULTVALS)
        {
            Data->Reset();
            TransferData(ttDataToWindow);
            return 0;
        }
        break;
    }

    case WM_NOTIFY:
    {
        LPNMHDR nmh = (LPNMHDR)lParam;
        if (nmh->code == DTN_DATETIMECHANGE)
            EnableControls();
        break;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
    {
        ApplyFilterCriteriaDialogTheme(HWindow);
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}
