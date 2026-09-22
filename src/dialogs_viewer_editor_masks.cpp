// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/IPathService.h"
#include "cfgdlg.h"
#include "darkmode.h"
#include "dialogs.h"
#include "usermenu.h"
#include "execute.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "gui.h"
#include "shellib.h"

#include <uxtheme.h>

// this build doesn't define _UNICODE, so <commctrl.h>'s ListView item
// helpers resolve to the A form only; mirrors the same local macros already used by
// the dbviewer/pictview plugins for the identical need.
#define ListView_InsertItemW(hwndLV, pitemW) \
    ((int)SNDMSG((hwndLV), LVM_INSERTITEMW, 0, (LPARAM)(const LV_ITEMW*)(pitemW)))
#define ListView_InsertColumnW(hwndLV, iCol, pcolW) \
    ((int)SNDMSG((hwndLV), LVM_INSERTCOLUMNW, (WPARAM)(int)(iCol), (LPARAM)(const LVCOLUMNW*)(pcolW)))
#define ListView_SetItemTextW(hwndLV, i, iSubItem_, pszText_) \
    {                                                         \
        LV_ITEMW _ms_lvi;                                     \
        _ms_lvi.iSubItem = iSubItem_;                         \
        _ms_lvi.pszText = pszText_;                           \
        SNDMSG((hwndLV), LVM_SETITEMTEXTW, (WPARAM)(i), (LPARAM)(LV_ITEM*)&_ms_lvi); \
    }

static const UINT_PTR SIZE_RESULTS_COMBO_SKIN_SUBCLASS_ID = 1;
static const UINT_PTR SIZE_RESULTS_COMBO_EDIT_SKIN_SUBCLASS_ID = 1;
static const COLORREF SIZE_RESULTS_DARK_LINE = RGB(55, 55, 58);
static const COLORREF SIZE_RESULTS_DARK_FRAME = RGB(62, 62, 66);
static const COLORREF SIZE_RESULTS_DARK_SECTION_LINE = RGB(70, 70, 74);

struct CSizeResultsComboSkinState
{
    LONG_PTR ComboStyle;
    LONG_PTR ComboExStyle;
    LONG_PTR EditStyle;
    LONG_PTR EditExStyle;
    HWND HEdit;
    BOOL EditStyleKnown;
    BOOL EditExStyleKnown;
    BOOL ApplyingTheme;
    BOOL ThemeKnown;
    BOOL LastUseDark;
};

static LRESULT CALLBACK SizeResultsComboSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static LRESULT CALLBACK SizeResultsComboEditSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

static void SizeResultsFillRectSolid(HDC hdc, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    COLORREF oldColor = SetDCBrushColor(hdc, color);
    FillRect(hdc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
}

static void SizeResultsDrawRectOutline(HDC hdc, const RECT* rect, COLORREF color)
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

static void SizeResultsDrawDownArrow(HDC hdc, const RECT* rect, COLORREF color)
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

static BOOL GetSizeResultsChildRectInParent(HWND hParent, HWND hChild, RECT* rect)
{
    if (hParent == NULL || hChild == NULL || rect == NULL || !IsWindow(hParent) || !IsWindow(hChild))
        return FALSE;

    if (!GetWindowRect(hChild, rect))
        return FALSE;
    MapWindowPoints(NULL, hParent, (POINT*)rect, 2);
    return TRUE;
}

static BOOL GetSizeResultsChildRectInDialog(HWND hDialog, int ctrlID, RECT* rect)
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

static void SetSizeResultsWindowStyle(HWND hwnd, LONG_PTR style)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    if (GetWindowLongPtr(hwnd, GWL_STYLE) == style)
        return;

    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void SetSizeResultsWindowExStyle(HWND hwnd, LONG_PTR exStyle)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    if (GetWindowLongPtr(hwnd, GWL_EXSTYLE) == exStyle)
        return;

    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static BOOL PaintSizeResultsDarkCombo(HWND hwnd, HDC paintDC)
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
    BOOL haveEditRect = GetSizeResultsChildRectInParent(hwnd, cbi.hwndItem, &editRect);

    int savedDC = SaveDC(paintDC);
    if (haveEditRect)
        ExcludeClipRect(paintDC, editRect.left, editRect.top, editRect.right, editRect.bottom);

    SizeResultsFillRectSolid(paintDC, &client, colors.InputBackground);

    int buttonWidth = max(GetSystemMetrics(SM_CXVSCROLL), client.bottom - client.top);
    RECT button = client;
    button.left = max(client.left + 1, client.right - buttonWidth - 1);
    button.top = client.top + 1;
    button.right = client.right - 1;
    button.bottom = client.bottom - 1;
    if (button.right > button.left && button.bottom > button.top)
    {
        SizeResultsFillRectSolid(paintDC, &button, colors.InputBackground);
        HGDIOBJ oldPen = SelectObject(paintDC, GetStockObject(DC_PEN));
        COLORREF oldPenColor = SetDCPenColor(paintDC, SIZE_RESULTS_DARK_LINE);
        MoveToEx(paintDC, button.left, button.top, NULL);
        LineTo(paintDC, button.left, button.bottom);
        SetDCPenColor(paintDC, oldPenColor);
        SelectObject(paintDC, oldPen);
        SizeResultsDrawDownArrow(paintDC, &button, IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText);
    }

    RestoreDC(paintDC, savedDC);
    SizeResultsDrawRectOutline(paintDC, &client, SIZE_RESULTS_DARK_FRAME);
    return TRUE;
}

static void PaintSizeResultsDarkComboFrame(HWND hwnd)
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
    SizeResultsDrawRectOutline(hdc, &rect, SIZE_RESULTS_DARK_FRAME);
    ReleaseDC(hwnd, hdc);
}

static void PaintSizeResultsDarkComboEditFrame(HWND hwnd)
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
    SizeResultsFillRectSolid(hdc, &window, colors.InputBackground);
    RestoreDC(hdc, savedDC);

    ReleaseDC(hwnd, hdc);
}

static void ApplySizeResultsComboEditSkin(HWND hEdit, BOOL useDark)
{
    if (hEdit == NULL || !IsWindow(hEdit))
        return;

    SetWindowSubclass(hEdit, SizeResultsComboEditSkinSubclassProc, SIZE_RESULTS_COMBO_EDIT_SKIN_SUBCLASS_ID, 0);
    SetWindowTheme(hEdit, useDark ? L"" : NULL, NULL);
    RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
}

static void UpdateSizeResultsComboSkin(HWND hCombo, CSizeResultsComboSkinState* state)
{
    if (hCombo == NULL || state == NULL || !IsWindow(hCombo))
        return;

    COMBOBOXINFO cbi = {0};
    cbi.cbSize = sizeof(cbi);
    GetComboBoxInfo(hCombo, &cbi);

    BOOL editChanged = cbi.hwndItem != NULL && cbi.hwndItem != state->HEdit;
    if (editChanged)
    {
        state->HEdit = cbi.hwndItem;
        state->EditStyle = GetWindowLongPtr(cbi.hwndItem, GWL_STYLE);
        state->EditExStyle = GetWindowLongPtr(cbi.hwndItem, GWL_EXSTYLE);
        state->EditStyleKnown = TRUE;
        state->EditExStyleKnown = TRUE;
    }

    BOOL useDark = DarkMode_ShouldUseDark();
    LONG_PTR darkStyleMask = WS_BORDER;
    LONG_PTR darkEdgeMask = WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
    SetSizeResultsWindowStyle(hCombo, useDark ? (state->ComboStyle & ~darkStyleMask) : state->ComboStyle);
    SetSizeResultsWindowExStyle(hCombo, useDark ? (state->ComboExStyle & ~darkEdgeMask) : state->ComboExStyle);

    if (state->HEdit != NULL && state->EditStyleKnown && IsWindow(state->HEdit))
        SetSizeResultsWindowStyle(state->HEdit, useDark ? (state->EditStyle & ~darkStyleMask) : state->EditStyle);
    if (state->HEdit != NULL && state->EditExStyleKnown && IsWindow(state->HEdit))
        SetSizeResultsWindowExStyle(state->HEdit, useDark ? (state->EditExStyle & ~darkEdgeMask) : state->EditExStyle);

    if (!state->ApplyingTheme &&
        (!state->ThemeKnown || state->LastUseDark != useDark || editChanged))
    {
        state->ApplyingTheme = TRUE;
        SetWindowTheme(hCombo, useDark ? L"" : NULL, NULL);
        ApplySizeResultsComboEditSkin(state->HEdit, useDark);
        if (cbi.hwndList != NULL && IsWindow(cbi.hwndList))
            SetWindowTheme(cbi.hwndList, useDark ? L"DarkMode_Explorer" : NULL, NULL);
        state->ThemeKnown = TRUE;
        state->LastUseDark = useDark;
        state->ApplyingTheme = FALSE;
    }

    RedrawWindow(hCombo, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

static void ApplySizeResultsComboSkin(HWND hCombo)
{
    if (hCombo == NULL || !IsWindow(hCombo))
        return;

    DWORD_PTR data = 0;
    CSizeResultsComboSkinState* state = NULL;
    if (GetWindowSubclass(hCombo, SizeResultsComboSkinSubclassProc, SIZE_RESULTS_COMBO_SKIN_SUBCLASS_ID, &data))
        state = (CSizeResultsComboSkinState*)data;
    else
    {
        state = new CSizeResultsComboSkinState;
        if (state == NULL)
            return;
        state->ComboStyle = GetWindowLongPtr(hCombo, GWL_STYLE);
        state->ComboExStyle = GetWindowLongPtr(hCombo, GWL_EXSTYLE);
        state->EditStyle = 0;
        state->EditExStyle = 0;
        state->HEdit = NULL;
        state->EditStyleKnown = FALSE;
        state->EditExStyleKnown = FALSE;
        state->ApplyingTheme = FALSE;
        state->ThemeKnown = FALSE;
        state->LastUseDark = FALSE;
        if (!SetWindowSubclass(hCombo, SizeResultsComboSkinSubclassProc, SIZE_RESULTS_COMBO_SKIN_SUBCLASS_ID, (DWORD_PTR)state))
        {
            delete state;
            return;
        }
    }

    UpdateSizeResultsComboSkin(hCombo, state);
}

static void ApplySizeResultsDialogTheme(HWND hDialog)
{
    if (hDialog == NULL || !IsWindow(hDialog))
        return;

    ApplySizeResultsComboSkin(GetDlgItem(hDialog, IDC_EST_CLUSTER));

    int lineIDs[] = {IDC_STATIC_16, IDC_STATIC_10, IDC_STATIC_15};
    BOOL useDark = DarkMode_ShouldUseDark();
    for (int i = 0; i < _countof(lineIDs); i++)
    {
        HWND hLine = GetDlgItem(hDialog, lineIDs[i]);
        if (hLine != NULL && IsWindow(hLine))
            ShowWindow(hLine, useDark ? SW_HIDE : SW_SHOWNA);
    }

    InvalidateRect(hDialog, NULL, TRUE);
}

static BOOL PaintSizeResultsDialogSectionLines(HWND hDialog, HDC paintDC)
{
    if (!DarkMode_ShouldUseDark())
        return FALSE;

    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = GetDC(hDialog);
    if (hdc == NULL)
        return FALSE;

    int lineIDs[] = {IDC_STATIC_16, IDC_STATIC_10, IDC_STATIC_15};
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    COLORREF oldColor = SetDCPenColor(hdc, SIZE_RESULTS_DARK_SECTION_LINE);
    for (int i = 0; i < _countof(lineIDs); i++)
    {
        RECT rect;
        if (!GetSizeResultsChildRectInDialog(hDialog, lineIDs[i], &rect))
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

static LRESULT CALLBACK SizeResultsComboEditSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            PaintSizeResultsDarkComboEditFrame(hwnd);
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (DarkMode_ShouldUseDark())
            PaintSizeResultsDarkComboEditFrame(hwnd);
        return ret;
    }

    case WM_ERASEBKGND:
    {
        DarkModeColors colors;
        if (DarkMode_GetColors(&colors))
        {
            RECT client;
            GetClientRect(hwnd, &client);
            SizeResultsFillRectSolid((HDC)wParam, &client, colors.InputBackground);
            return TRUE;
        }
        break;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_ENABLE:
    case WM_SIZE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, SizeResultsComboEditSkinSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK SizeResultsComboSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    CSizeResultsComboSkinState* state = (CSizeResultsComboSkinState*)dwRefData;

    switch (uMsg)
    {
    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            PaintSizeResultsDarkComboFrame(hwnd);
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            PAINTSTRUCT ps;
            HDC hdc = HANDLES(BeginPaint(hwnd, &ps));
            if (hdc != NULL)
                PaintSizeResultsDarkCombo(hwnd, hdc);
            HANDLES(EndPaint(hwnd, &ps));
            return 0;
        }
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintSizeResultsDarkCombo(hwnd, (HDC)wParam))
            return 0;
        break;
    }

    case WM_ERASEBKGND:
    {
        if (DarkMode_ShouldUseDark())
            return TRUE;
        break;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        HBRUSH hBrush = DarkMode_GetDialogCtlColorBrush(uMsg, (HDC)wParam, (HWND)lParam);
        if (hBrush != NULL)
            return (LRESULT)hBrush;
        break;
    }

    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_ENABLE:
    case WM_SIZE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case CB_SETCURSEL:
    case CB_ADDSTRING:
    case CB_DELETESTRING:
    case CB_RESETCONTENT:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (state != NULL && !state->ApplyingTheme)
            UpdateSizeResultsComboSkin(hwnd, state);
        else
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, SizeResultsComboSkinSubclassProc, uIdSubclass);
        delete state;
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static BOOL BuildModuleRelativePathW(HINSTANCE module, const wchar_t* relativePath, std::wstring& path)
{
    std::wstring modulePath;
    if (gPathService == NULL || !gPathService->GetModuleFileName(module, modulePath).success)
        return FALSE;
    const size_t slash = modulePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return FALSE;
    path.assign(modulePath, 0, slash + 1);
    path.append(relativePath);
    return TRUE;
}

//****************************************************************************
//
// CViewerMasksItem
//

// this number keeps growing - is used as a source for unique IDs
DWORD ViewerHandlerID = 0;

CViewerMasksItem::CViewerMasksItem(const wchar_t* masks, const wchar_t* command, const wchar_t* arguments, const wchar_t* initDir,
                                   int viewerType, BOOL oldType)
{
    CALL_STACK_MESSAGE7("CViewerMasksItem(%ls, %ls, %ls, %ls, %d, %d)",
                        masks, command, arguments, initDir, viewerType, oldType);
    OldType = oldType;
    Masks = NULL;
    ViewerType = viewerType;
    HandlerID = ViewerHandlerID++;
    Set(masks, command, arguments, initDir);
}

CViewerMasksItem::CViewerMasksItem()
{
    CALL_STACK_MESSAGE1("CViewerMasksItem()");
    Masks = NULL;
    ViewerType = VIEWER_EXTERNAL;
    HandlerID = ViewerHandlerID++;
    OldType = FALSE;
    Set(L"", L"", L"\"$(Name)\"", L"$(FullPath)");
}

CViewerMasksItem::CViewerMasksItem(CViewerMasksItem& item)
{
    CALL_STACK_MESSAGE1("CViewerMasksItem(&)");
    Masks = NULL;
    ViewerType = item.ViewerType;
    OldType = item.OldType;
    HandlerID = item.HandlerID;
    Set(item.Masks->GetMasksString(), item.Command.c_str(), item.Arguments.c_str(), item.InitDir.c_str());
}

CViewerMasksItem::~CViewerMasksItem()
{
    if (Masks != NULL)
        delete Masks;
}

BOOL CViewerMasksItem::IsGood()
{
    return Masks != NULL;
}

BOOL CViewerMasksItem::Set(const wchar_t* masks, const wchar_t* command, const wchar_t* arguments, const wchar_t* initDir)
{
    CALL_STACK_MESSAGE5("CViewerMasksItem::Set(%ls, %ls, %ls, %ls)", masks, command, arguments, initDir);

    if (Masks == NULL)
        Masks = new CMaskGroup;
    if (Masks == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    Masks->SetMasksString(masks);
    Command = command;
    Arguments = arguments;
    InitDir = initDir;

    return TRUE;
}

BOOL CViewerMasks::Load(CViewerMasks& source)
{
    CALL_STACK_MESSAGE1("CViewerMasks::Load()");
    CViewerMasksItem* item;
    DestroyMembers();
    int i;
    for (i = 0; i < source.Count; i++)
    {
        item = new CViewerMasksItem(*source[i]);
        if (!item->IsGood())
        {
            delete item;
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        Add(item);
        if (!IsGood())
        {
            delete item;
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
    }
    return TRUE;
}

//****************************************************************************
//
// CEditorMasksItem
//

// this number keeps growing - is used as a source for unique IDs
DWORD EditorHandlerID = 0;

CEditorMasksItem::CEditorMasksItem(const wchar_t* masks, const wchar_t* command, const wchar_t* arguments, const wchar_t* initDir)
{
    CALL_STACK_MESSAGE5("CEditorMasksItem(%ls, %ls, %ls, %ls)", masks, command, arguments, initDir);
    Masks = new CMaskGroup;
    HandlerID = EditorHandlerID++;
    Set(masks, command, arguments, initDir);
}

CEditorMasksItem::CEditorMasksItem()
{
    CALL_STACK_MESSAGE1("CEditorMasksItem()");
    Masks = new CMaskGroup;
    HandlerID = EditorHandlerID++;
    Set(L"", L"", L"\"$(Name)\"", L"$(FullPath)");
}

CEditorMasksItem::CEditorMasksItem(CEditorMasksItem& item)
{
    CALL_STACK_MESSAGE1("CEditorMasksItem(&)");
    Masks = new CMaskGroup;
    HandlerID = item.HandlerID;
    Set(item.Masks->GetMasksString(), item.Command.c_str(), item.Arguments.c_str(), item.InitDir.c_str());
}

CEditorMasksItem::~CEditorMasksItem()
{
    if (Masks != NULL)
        delete Masks;
}

BOOL CEditorMasksItem::Set(const wchar_t* masks, const wchar_t* command, const wchar_t* arguments, const wchar_t* initDir)
{
    CALL_STACK_MESSAGE5("CEditorMasksItem::Set(%ls, %ls, %ls, %ls)", masks, command, arguments, initDir);
    if (Masks != NULL)
        Masks->SetMasksString(masks);
    Command = command;
    Arguments = arguments;
    InitDir = initDir;
    return TRUE;
}

BOOL CEditorMasksItem::IsGood()
{
    return Masks != NULL;
}

BOOL CEditorMasks::Load(CEditorMasks& source)
{
    CALL_STACK_MESSAGE1("CEditorMasks::Load()");
    CEditorMasksItem* item;
    DestroyMembers();
    int i;
    for (i = 0; i < source.Count; i++)
    {
        item = new CEditorMasksItem(*source[i]);
        if (!item->IsGood())
        {
            delete item;
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        Add(item);
        if (!IsGood())
        {
            delete item;
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
    }
    return TRUE;
}

//
// ****************************************************************************
// CCommonDialog
//

void CCommonDialog::NotifDlgJustCreated()
{
    ArrangeHorizontalLines(HWindow);
}

INT_PTR
CCommonDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // prevent panels from refreshing on the background of modal dialogs or messageboxes
        if (Modal && MainWindow != NULL && Parent != NULL && Parent == MainWindow->HWindow)
        {
            BeginStopRefresh(FALSE, TRUE); // the sniffer takes a break
            CallEndStopRefresh = TRUE;
        }
        else
            CallEndStopRefresh = FALSE;

        // when opening the dialog set the plug-ins' msgbox parent to this dialog (main thread only)
        if (Modal && MainThreadID == GetCurrentThreadId())
        {
            HOldPluginMsgBoxParent = PluginMsgBoxParent;
            PluginMsgBoxParent = HWindow;
        }

        HWND hCenterBy;
        if (HCenterAgains != NULL)
            hCenterBy = HCenterAgains;
        else
            hCenterBy = Parent;

        if (hCenterBy != NULL)
            MultiMonCenterWindow(HWindow, hCenterBy, TRUE);
        else
            MultiMonCenterWindow(HWindow, NULL, FALSE);

        break;
    }

        /* j.r.: the VK_ESCAPE variant seems better because clicking IDCANCEL does not set a variable
    case WM_COMMAND:
    {
      if (LOWORD(wParam) == IDCANCEL) // measure to avoid interrupting panel listing after each ESC
        WaitForESCReleaseBeforeTestingESC = TRUE;
      break;
    }
    */

    case WM_DESTROY:
    {
        if (GetKeyState(VK_ESCAPE) & 0x8000) // measure to avoid interrupting panel listing after each ESC
            WaitForESCReleaseBeforeTestingESC = TRUE;

        // the dialog is closing - the user might have changed the clipboard
        // (for example pasted text from an editline), so we'll verify it
        IdleRefreshStates = TRUE;  // force the state variables check during the next Idle
        IdleCheckClipboard = TRUE; // also let the clipboard be checked

        // when closing the dialog restore the msgbox parent for plug-ins
        if (HOldPluginMsgBoxParent != NULL)
            PluginMsgBoxParent = HOldPluginMsgBoxParent;

        if (CallEndStopRefresh)
        {
            EndStopRefresh(TRUE, FALSE, TRUE); // the sniffer will start again now
            CallEndStopRefresh = FALSE;
        }
        break;
    }
    }

    return CDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCommonPropSheetPage
//

void CCommonPropSheetPage::NotifDlgJustCreated()
{
    ArrangeHorizontalLines(HWindow);
}

//
// ****************************************************************************
// CSizeResultsDlg
//

CSizeResultsDlg::CSizeResultsDlg(HWND parent, const CQuadWord& size, const CQuadWord& compressed,
                                 const CQuadWord& occupied, int files, int dirs, TDirectArray<CQuadWord>* sizes)
    : CCommonDialog(HLanguage, IDD_SIZERESULTS, IDD_SIZERESULTS, parent)
{
    Size = size;
    Compressed = compressed;
    Occupied = occupied;
    Files = files;
    Dirs = dirs;
    Sizes = sizes;
}

void CSizeResultsDlg::UpdateEstimate()
{
    const std::wstring clusterText = GetWindowTextStringW(GetDlgItem(HWindow, IDC_EST_CLUSTER));
    int bytesPerCluster = _wtoi(clusterText.c_str());

    if (Sizes != NULL && Sizes->IsGood() && bytesPerCluster > 0)
    {
        if (Sizes->Count != Files)
            TRACE_E("Sizes array is not consistent with number of files.");

        CQuadWord estimated(0, 0);
        CQuadWord s;
        int i;
        for (i = 0; i < Sizes->Count; i++)
        {
            s = Sizes->At(i);
            estimated += s - ((s - CQuadWord(1, 0)) % CQuadWord(bytesPerCluster, 0)) +
                         CQuadWord(bytesPerCluster - 1, 0);
        }

        SetWindowTextW(GetDlgItem(HWindow, IDC_EST_SIZE), PrintDiskSize(estimated, 1).c_str());

        std::wstring utilizationText = L"0 %";
        if (estimated != CQuadWord(0, 0))
        {
            utilizationText = FormatStrW(L"%-1.4lg %%", 100 * Size.GetDouble() / estimated.GetDouble());
            PointToLocalDecimalSeparator(utilizationText);
        }
        SetWindowTextW(GetDlgItem(HWindow, IDC_EST_UTIL), utilizationText.c_str());

        EnableWindow(GetDlgItem(HWindow, IDC_EST_SIZE), TRUE);
        EnableWindow(GetDlgItem(HWindow, IDC_EST_UTIL), TRUE);
    }
    else
    {
        EnableWindow(GetDlgItem(HWindow, IDC_EST_SIZE), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_EST_UTIL), FALSE);
        SetWindowTextW(GetDlgItem(HWindow, IDC_EST_SIZE), UnknownText.c_str());
        SetWindowTextW(GetDlgItem(HWindow, IDC_EST_UTIL), UnknownText.c_str());
    }
}

INT_PTR
CSizeResultsDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CSizeResultsDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_PAINT:
    {
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        PaintSizeResultsDialogSectionLines(HWindow, NULL);
        return ret;
    }

    case WM_PRINTCLIENT:
    {
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        PaintSizeResultsDialogSectionLines(HWindow, (HDC)wParam);
        return ret;
    }

    case WM_INITDIALOG:
    {
        UnknownText = GetWindowTextStringW(GetDlgItem(HWindow, IDS_OCCUPIED));

        SetWindowTextW(GetDlgItem(HWindow, IDS_FILESCOUNT), NumberToStr(CQuadWord(Files, 0)).c_str());
        SetWindowTextW(GetDlgItem(HWindow, IDS_DIRSCOUNT), NumberToStr(CQuadWord(Dirs, 0)).c_str());

        if (Occupied != CQuadWord(-1, -1))
        {
            SetWindowTextW(GetDlgItem(HWindow, IDS_OCCUPIED), PrintDiskSize(Occupied, 1).c_str());
            std::wstring utilizationText = L"0 %";
            if (Occupied != CQuadWord(0, 0))
            {
                double result = 100 * Size.GetDouble() / Occupied.GetDouble();
                // patch for a 2GB sparse file where 3.052e+006 % was shown instead of 3051757.83 %
                // for values above 1000, lg prints exponential form so we use lf
                // for smaller numbers lg is better because it prints 100 rather than 100.00
                if (result > 1000)
                    utilizationText = FormatStrW(L"%-1.2lf %%", result);
                else
                    utilizationText = FormatStrW(L"%-1.4lg %%", result);
                PointToLocalDecimalSeparator(utilizationText);
            }
            SetWindowTextW(GetDlgItem(HWindow, IDS_DISKUTILIZATION), utilizationText.c_str());
        }
        else
        {
            EnableWindow(GetDlgItem(HWindow, IDS_OCCUPIED), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDS_DISKUTILIZATION), FALSE);
        }

        SetWindowTextW(GetDlgItem(HWindow, IDS_SIZE), PrintDiskSize(Size, 1).c_str());
        if (Compressed != CQuadWord(-1, -1))
        {
            SetWindowTextW(GetDlgItem(HWindow, IDS_COMPSIZE), PrintDiskSize(Compressed, 1).c_str());
            std::wstring ratioText = L"100 %";
            if (Size != CQuadWord(0, 0))
            {
                ratioText = FormatStrW(L"%-1.4lg %%", 100 * Compressed.GetDouble() / Size.GetDouble());
                PointToLocalDecimalSeparator(ratioText);
            }
            SetWindowTextW(GetDlgItem(HWindow, IDS_COMPRATIO), ratioText.c_str());
        }
        else
        {
            EnableWindow(GetDlgItem(HWindow, IDS_COMPSIZE), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDS_COMPRATIO), FALSE);
        }

        // fill the combobox

        DWORD clusterSize = 2048; // most likely used for CDs
        CFilesWindow* panel = MainWindow->GetNonActivePanel();
        if (panel->Is(ptDisk))
        {
            // wide - same reparse-point-resolves-the-whole-path issue as
            // CFilesWindow::RefreshDiskFreeSpace; GetPathW() is the authoritative source
            // (fileswnd.h), the removed ANSI mirror a CP_ACP rendering that silently failed this
            // probe for a non-ASCII path component, leaving clusterSize at its CD-sized
            // 2048 default on an ordinary NTFS volume.
            DWORD sectorsPerCluster, bytesPerSector, numberOfFreeClusters, totalNumberOfClusters;
            if (MyGetDiskFreeSpaceW(MainWindow->GetNonActivePanel()->GetPathW(),
                                   &sectorsPerCluster, &bytesPerSector,
                                   &numberOfFreeClusters, &totalNumberOfClusters))
            {
                clusterSize = sectorsPerCluster * bytesPerSector;
            }
        }

        HWND hCombo = GetDlgItem(HWindow, IDC_EST_CLUSTER);
        SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
        SendMessage(hCombo, CB_LIMITTEXT, 11, 0);

        int selIndex = -1;
        DWORD arr[] = {512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, (DWORD)-1};
        int i;
        for (i = 0; arr[i] != -1; i++)
        {
            const std::wstring clusterText = std::to_wstring(arr[i]);
            SendMessageW(hCombo, CB_ADDSTRING, 0,
                         (LPARAM)clusterText.c_str());
            if (clusterSize == arr[i])
                selIndex = i;
        }

        if (selIndex != -1)
            SendMessage(hCombo, CB_SETCURSEL, selIndex, 0);
        else
        {
            const std::wstring clusterText = std::to_wstring(clusterSize);
            SendMessageW(hCombo, WM_SETTEXT, 0,
                         (LPARAM)clusterText.c_str());
        }

        if (Sizes == NULL || !Sizes->IsGood())
            EnableWindow(hCombo, FALSE);

        UpdateEstimate();
        ApplySizeResultsDialogTheme(HWindow);

        break;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == CBN_SELCHANGE)
        {
            PostMessage(HWindow, WM_COMMAND, MAKELPARAM(0, CBN_EDITCHANGE), 0);
        }
        if (HIWORD(wParam) == CBN_EDITCHANGE)
        {
            UpdateEstimate();
        }
        break;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
    {
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        ApplySizeResultsDialogTheme(HWindow);
        return ret;
    }
    }

    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CSelectDialog
//

void CSelectDialog::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CSelectDialog::Validate()");
    HWND hWnd;
    if (ti.GetControl(hWnd, IDE_FILEMASK))
    {
        if (ti.Type == ttDataFromWindow)
        {
            const std::wstring candidate = GetWindowTextStringW(hWnd);
            CMaskGroup mask(candidate.c_str());
            int errorPos;
            if (!mask.PrepareMasks(errorPos))
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
                SetFocus(hWnd);
                SendMessageW(hWnd, CB_SETEDITSEL, 0, MAKELPARAM(errorPos, errorPos + 1));
                ti.ErrorOn(IDE_FILEMASK);
            }
        }
    }
}

void CSelectDialog::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CSelectDialog::Transfer()");
    wchar_t** history = Configuration.SelectHistory;
    HWND hWnd;
    if (ti.GetControl(hWnd, IDE_FILEMASK))
    {
        if (ti.Type == ttDataToWindow)
        {
            LoadComboFromStdHistoryValues(hWnd, history, SELECT_HISTORY_SIZE);
            SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)Mask.c_str());
        }
        else
        {
            Mask = GetWindowTextStringW(hWnd);
            AddValueToStdHistoryValues(history, SELECT_HISTORY_SIZE, Mask.c_str(), FALSE);
        }
    }
}

INT_PTR
CSelectDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CSelectDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        InstallWordBreakProc(GetDlgItem(HWindow, IDE_FILEMASK)); // install WordBreakProc to the combobox

        CHyperLink* hl = new CHyperLink(HWindow, IDC_FILEMASK_HINT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStrW(IDS_MASKS_HINT));

        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CImportConfigDialog
//

CImportConfigDialog::CImportConfigDialog()
    : CCommonDialog(HLanguage, IDD_IMPORTCONFIG, NULL)
{
}

CImportConfigDialog::~CImportConfigDialog()
{
}

extern const wchar_t* SalamanderConfigurationVersions[SALCFG_ROOTS_COUNT];

void CImportConfigDialog::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataToWindow)
    {
        wchar_t buff[5000];
        wchar_t buff2[5000];

        // CAPTION: Welcome to %s
        GetWindowTextW(HWindow, buff, 5000);
        // SALAMANDER_TEXT_VERSIONW(), not SALAMANDER_TEXT_VERSION: the format string and the
        // destination are wide, and feeding the narrow spelling to a %s of a wide printf reads the
        // rdata bytes two at a time - "Sally 5.0" came out as CJK and kept going past the literal
        // until a wide NUL happened to appear. consts.h carries both spellings side by side.
        _snwprintf_s(buff2, _TRUNCATE, buff, SALAMANDER_TEXT_VERSIONW());
        SetWindowTextW(HWindow, buff2);

        // COMBOBOX Import Configuration
        SendDlgItemMessageW(HWindow, IDC_IMPORTCONFIG, CB_ADDSTRING, 0, (LPARAM)LoadStrW(IDS_IMPORTCFG_DEFCFG));
        int selIndex = 0; // use the default item if nothing better is found
        int i;
        for (i = 0; i < SALCFG_ROOTS_COUNT; i++)
        {
            if (ConfigurationExist[i])
            {
                // detect whether this is "Sally", "Open Salamander", "Altap Salamander", or the old "Servant Salamander"
                BOOL sally = StrIStr(SalamanderConfigurationRoots[i], L"Sally") != NULL;
                BOOL openSalamander = StrIStr(SalamanderConfigurationRoots[i], L"Open Salamander") != NULL;
                BOOL altapSalamander = StrIStr(SalamanderConfigurationRoots[i], L"Altap Salamander") != NULL;
                const wchar_t* name = sally              ? L"Sally %s"
                                   : openSalamander   ? L"Open Salamander %s"
                                   : altapSalamander  ? L"Altap Salamander %s"
                                                      : L"Servant Salamander %s";
                _snwprintf_s(buff, _TRUNCATE, name, SalamanderConfigurationVersions[i]);
                SendDlgItemMessageW(HWindow, IDC_IMPORTCONFIG, CB_ADDSTRING, 0, (LPARAM)buff);
                if (selIndex == 0)
                    selIndex = 1; // the last configuration becomes default
            }
        }
        if (selIndex == 0) // nothing to choose from, disable the combobox
        {
            EnableWindow(GetDlgItem(HWindow, IDC_IMPORTCONFIG), FALSE);
        }
        SendDlgItemMessageW(HWindow, IDC_IMPORTCONFIG, CB_SETCURSEL, selIndex, NULL);

        // LISTVIEW Remove Configuration
        HWND hListView = GetDlgItem(HWindow, IDC_REMOVECONFIG);
        selIndex = -1;
        int index = 0;
        for (i = 0; i < SALCFG_ROOTS_COUNT; i++)
        {
            if (ConfigurationExist[i])
            {
                LVITEMW lvi;
                lvi.mask = LVIF_TEXT | LVIF_STATE;
                lvi.iItem = index;
                lvi.iSubItem = 0;
                lvi.state = 0;

                // detect whether this is "Sally", "Open Salamander", "Altap Salamander", or the old "Servant Salamander"
                BOOL sally = StrIStr(SalamanderConfigurationRoots[i], L"Sally") != NULL;
                BOOL openSalamander = StrIStr(SalamanderConfigurationRoots[i], L"Open Salamander") != NULL;
                BOOL altapSalamander = StrIStr(SalamanderConfigurationRoots[i], L"Altap Salamander") != NULL;
                const wchar_t* name = sally              ? L"Sally %s"
                                   : openSalamander   ? L"Open Salamander %s"
                                   : altapSalamander  ? L"Altap Salamander %s"
                                                      : L"Servant Salamander %s";
                _snwprintf_s(buff, _TRUNCATE, name, SalamanderConfigurationVersions[i]);
                lvi.pszText = buff;
                ListView_InsertItemW(hListView, &lvi);
                index++;
                if (selIndex == -1)
                {
                    DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
                    ListView_SetItemState(hListView, 0, state, state);
                    selIndex = 0;
                }
            }
        }
    }
    else
    {
        // COMBOBOX Import Configuration
        int sel = (int)SendDlgItemMessageW(HWindow, IDC_IMPORTCONFIG, CB_GETCURSEL, 0, NULL);
        if (sel > 0)
        {
            sel--; // the first item is Don't import
            int index = 0;
            int i;
            for (i = 0; i < SALCFG_ROOTS_COUNT; i++)
            {
                if (ConfigurationExist[i])
                {
                    if (sel == index)
                    {
                        IndexOfConfigurationToLoad = i;
                        break;
                    }
                    index++;
                }
            }
        }

        // LISTVIEW Remove Configuration
        HWND hListView = GetDlgItem(HWindow, IDC_REMOVECONFIG);
        int itemsCount = ListView_GetItemCount(hListView);
        int index = 0;
        int i;
        for (i = 0; i < SALCFG_ROOTS_COUNT; i++)
        {
            if (ConfigurationExist[i])
            {
                DWORD state = ListView_GetItemState(hListView, index, LVIS_STATEIMAGEMASK);
                if (state == INDEXTOSTATEIMAGEMASK(2))
                    DeleteConfigurations[i] = TRUE;
                index++;
            }
        }
    }
}

INT_PTR
CImportConfigDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // under W2K when launched via a shortcut set to MAXIMIZED
        // the dialog appeared maximized; SC_RESTORE fixes it
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        SendMessage(HWindow, WM_SYSCOMMAND, SC_RESTORE, 0);

        // checkboxes for the listview
        HWND hListView = GetDlgItem(HWindow, IDC_REMOVECONFIG);
        DWORD exFlags = LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES;
        DWORD origFlags = ListView_GetExtendedListViewStyle(hListView);
        ListView_SetExtendedListViewStyle(hListView, origFlags | exFlags); // 4.71

        // add the Name column to the listview with columns
        LVCOLUMNW lvc;
        lvc.mask = LVCF_TEXT | LVCF_FMT;
        wchar_t buff[] = L"aa";
        lvc.pszText = buff;
        lvc.fmt = LVCFMT_LEFT;
        lvc.iSubItem = 0;
        ListView_InsertColumnW(hListView, 0, &lvc);
        ListView_SetColumnWidth(hListView, 0, LVSCW_AUTOSIZE);

        return ret;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CLanguageSelectorDialog
//

CLanguageSelectorDialog::CLanguageSelectorDialog(HWND hParent, std::wstring& slgName, const wchar_t* pluginName)
    : CCommonDialog(NULL, pluginName == NULL ? IDD_SLGSELECTOR : IDD_SLGSELECTORPLUG, hParent), Items(5, 5), SLGName(slgName)
{
    Web = NULL;
    OpenedFromConfiguration = hParent != NULL && pluginName == NULL;
    OpenedForPlugin = pluginName != NULL;
    HListView = NULL;
    PluginName = pluginName;
    ExitButtonLabel.clear();
}

CLanguageSelectorDialog::~CLanguageSelectorDialog()
{
    int i;
    for (i = 0; i < Items.Count; i++)
        Items[i].Free();
}

int CLanguageSelectorDialog::Execute()
{
    HINSTANCE hTmpLanguage = NULL;
    if (OpenedFromConfiguration || OpenedForPlugin)
    {
        // use the template from the currently running language version
        Modul = HLanguage;
    }
    else
    {
        // load the template from the best available SLG
        int index = GetPreferredLanguageIndex(SLGName.c_str());
        std::wstring pathW;
        // Items[].FileName is already wchar_t*; the AnsiToWide here was
        // the conversion layer applied to wide data (silent-failure class #2).
        std::wstring slgNameW = Items[index].FileName;
        if (BuildModuleRelativePathW(HInstance, (L"lang\\" + slgNameW).c_str(), pathW))
            hTmpLanguage = HANDLES(LoadLibraryW(pathW.c_str()));
        if (hTmpLanguage != NULL)
            Modul = hTmpLanguage;
    }
    const wchar_t* exitButtonLabel = NULL;
    const int exitButtonLabelLength = LoadStringW(Modul, IDS_SELLANGEXITBUTTON,
                                                   reinterpret_cast<wchar_t*>(&exitButtonLabel), 0);
    if (exitButtonLabelLength > 0)
        ExitButtonLabel.assign(exitButtonLabel, exitButtonLabelLength);
    else
        ExitButtonLabel = L"Exit";
    int ret = (int)CCommonDialog::Execute();
    if (hTmpLanguage != NULL)
    {
        Modul = NULL;
        HANDLES(FreeLibrary(hTmpLanguage));
    }

    return ret;
}

BOOL CLanguageSelectorDialog::GetSLGName(std::wstring& path, int index)
{
    if (index >= Items.Count)
        return FALSE;
    path = Items[index].FileName;
    return TRUE;
}

BOOL CLanguageSelectorDialog::SLGNameExists(const wchar_t* slgName)
{
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        if (StrICmpW(Items[i].FileName, slgName) == 0)
            return TRUE;
    }
    return FALSE;
}

void CLanguageSelectorDialog::FillControls()
{
    int index = ListView_GetNextItem(HListView, -1, LVIS_FOCUSED);
    if (index != -1)
    {
        SetDlgItemTextW(HWindow, IDC_SLG_AUTHOR, Items[index].AuthorW);
        SetDlgItemTextW(HWindow, IDC_SLG_WEB, Items[index].Web);
        SetDlgItemTextW(HWindow, IDC_SLG_COMMENT, Items[index].CommentW);
        if (PluginName == NULL)
            SetDlgItemTextW(HWindow, IDC_SLG_HELPDIR, Items[index].HelpDir);
        if (Web != NULL)
        {
            wchar_t buff[300];
            swprintf_s(buff, _countof(buff), L"http://%s", Items[index].Web);
            Web->SetActionOpen(buff);
        }
    }
}

void CLanguageSelectorDialog::LoadListView()
{
    wchar_t buff[500];
    // wide - GetLanguageName narrows a language's own native display
    // name through CP_ACP; this dialog already sets other controls wide
    // (FillControls's SetDlgItemTextW for Author/Comment), so it's wide-capable.
    wchar_t buffW[200];
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        LVITEMW lvi;
        lvi.mask = 0;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        ListView_InsertItemW(HListView, &lvi);

        Items[i].GetLanguageName(buffW, 200);
        ListView_SetItemTextW(HListView, i, 0, buffW);
        swprintf_s(buff, _countof(buff), L"lang\\%s", Items[i].FileName);
        ListView_SetItemTextW(HListView, i, 1, buff);
    }

    int preferredIndex = GetPreferredLanguageIndex(SLGName.c_str());
    DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
    ListView_SetItemState(HListView, preferredIndex, state, state);
    ListView_EnsureVisible(HListView, preferredIndex, FALSE);

    FillControls();
}

void CLanguageSelectorDialog::Transfer(CTransferInfo& ti)
{
    if (PluginName != NULL) // show this checkbox only when selecting an alternative language for a plug-in
        ti.CheckBox(IDC_USESAMESLGINOTHERPLUGINS, Configuration.UseAsAltSLGInOtherPlugins);

    if (ti.Type == ttDataToWindow)
    {
        LoadListView();

        // we do not want a horizontal scrollbar, so first fill items and only then set the column widths
        RECT r;
        GetClientRect(HListView, &r);
        ListView_SetColumnWidth(HListView, 0, r.right / 1.6);
        ListView_SetColumnWidth(HListView, 1, LVSCW_AUTOSIZE_USEHEADER);
    }
    else
    {
        int index = ListView_GetNextItem(HListView, -1, LVIS_FOCUSED);
        if (index != -1)
        {
            SLGName = Items[index].FileName;
            if (PluginName != NULL) // store the alternative language name only when selecting an alternative language for a plug-in
            {
                if (Configuration.UseAsAltSLGInOtherPlugins)
                    Configuration.AltPluginSLGName = SLGName;
                else
                    Configuration.AltPluginSLGName.clear();
            }
        }
    }
}

BOOL CLanguageSelectorDialog::Initialize(const wchar_t* slgSearchPath, HINSTANCE pluginDLL)
{
    std::wstring path;
    if (slgSearchPath == NULL)
    {
        if (!BuildModuleRelativePathW(NULL, L"lang\\*.slg", path))
            return FALSE;
    }
    else
        path = slgSearchPath;

    WIN32_FIND_DATAW file;
    HANDLE hFind = SalFindFirstFileHW(path.c_str(), &file);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            // cFileNameA was ALREADY wchar_t[] - the WideCharToMultiByte here
            // narrowed file.cFileName through CP_ACP only to hand it straight back to wide
            // consumers, and any .slg whose name CP_ACP cannot represent would have been
            // dropped. Renamed to say what it holds.
            const wchar_t* cFileName = file.cFileName;
            const wchar_t* point = wcsrchr(cFileName, L'.');
            if (point != NULL && _wcsicmp(point + 1, L"slg") == 0) // it was returning *.slg*
            {
                CLanguage lang;
                if (lang.Init(cFileName, pluginDLL))
                {
                    Items.Add(lang);
                    if (!Items.IsGood())
                    {
                        Items.ResetState();
                        lang.Free();
                        return FALSE;
                    }
                }
            }
        } while (SalLPFindNextFile(hFind, &file));
        SalLPFindClose(hFind);
    }
    return TRUE;
}

int CLanguageSelectorDialog::GetPreferredLanguageIndex(const wchar_t* selectSLGName, BOOL exactMatch)
{
    WORD langID = GetUserDefaultUILanguage();

    WORD primaryID = PRIMARYLANGID(langID);
    int localeIndex = -1;        // index corresponding to the user's locale
    int primarylocaleIndex = -1; // index corresponding to the user's primary language locale
    int englishIndex = -1;       // index of the file "english.slg"
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        if (selectSLGName != NULL && _wcsicmp(Items[i].FileName, selectSLGName) == 0)
            return i;
        if (localeIndex == -1 && Items[i].LanguageID == langID)
            localeIndex = i;
        if (primarylocaleIndex == -1 && PRIMARYLANGID(Items[i].LanguageID) == primaryID)
            primarylocaleIndex = i;
        if (_wcsicmp(Items[i].FileName, L"english.slg") == 0)
            englishIndex = i;
    }
    if (localeIndex == -1)
    {
        // if we didn't find a language exactly matching the user's settings
        if (primarylocaleIndex != -1)
        {
            // try to assign at least the primary language
            localeIndex = primarylocaleIndex;
        }
        else
        {
            if (!exactMatch)
            {
                if (englishIndex != -1)
                {
                    // if even that isn't found, prefer the English version
                    localeIndex = englishIndex;
                }
                else
                {
                    // otherwise take whichever one is available
                    localeIndex = 0;
                }
            }
        }
    }
    return localeIndex;
}

INT_PTR
CLanguageSelectorDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {

        // JRY: For AS 2.53 which ships with Czech, German and English we send other translations to the "Translations" section on the forum
        //     https://forum.altap.cz/viewforum.php?f=23 - in the hope that someone will be motivated to create a translation.

        // There is no download page for languages yet, so this button is disabled
        // EnableWindow(GetDlgItem(HWindow, IDB_GETMORELANGS), FALSE);

        if (!OpenedFromConfiguration && !OpenedForPlugin)
        {
            // put the program name in the title since this is the first window the user sees
            SetWindowTextW(HWindow, L"Sally");
        }
        else
        {
            if (PluginName != NULL)
            {
                // put the plug-in name in the title so the user knows which plug-in the language is for
                wchar_t buf[200];
                _snwprintf_s(buf, _TRUNCATE, L"%s: ", PluginName);
                buf[99] = 0; // use only 100 characters for the plug-in name so some space remains for the original title dialog
                int len = (int)wcslen(buf);
                if (GetWindowTextW(HWindow, buf + len, 200 - len))
                    SetWindowTextW(HWindow, buf);
            }
        }
        if (!OpenedFromConfiguration && PluginName == NULL) // turn the Cancel button into Exit
            SetDlgItemTextW(HWindow, IDCANCEL, ExitButtonLabel.c_str());
        if (PluginName != NULL) // disable closing
            EnableMenuItem(GetSystemMenu(HWindow, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);

        Web = new CHyperLink(HWindow, IDC_SLG_WEB, STF_HYPERLINK_COLOR);

        HListView = GetDlgItem(HWindow, IDC_SLG_LIST);

        DWORD exFlags = LVS_EX_FULLROWSELECT;
        DWORD origFlags = ListView_GetExtendedListViewStyle(HListView);
        ListView_SetExtendedListViewStyle(HListView, origFlags | exFlags); // 4.71

        // add the Language and Path columns to the listview
        wchar_t buff[100];
        LVCOLUMNW lvc;
        lvc.mask = LVCF_TEXT | LVCF_SUBITEM;
        lvc.pszText = buff;
        lvc.iSubItem = 0;
        GetDlgItemTextW(HWindow, IDC_SLG_DESCR, buff, 100);
        DestroyWindow(GetDlgItem(HWindow, IDC_SLG_DESCR));
        ListView_InsertColumnW(HListView, 0, &lvc);

        lvc.iSubItem = 1;
        GetDlgItemTextW(HWindow, IDC_SLG_PATH, buff, 100);
        DestroyWindow(GetDlgItem(HWindow, IDC_SLG_PATH));
        ListView_InsertColumnW(HListView, 1, &lvc);

        // under W2K when launched via a shortcut set to MAXIMIZED
        // the dialog appeared maximized; SC_RESTORE fixes it
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        SendMessage(HWindow, WM_SYSCOMMAND, SC_RESTORE, 0);
        return ret;
    }

    case WM_COMMAND:
    {
        if (PluginName != NULL && LOWORD(wParam) == IDCANCEL)
            return 0;
        if (LOWORD(wParam) == IDB_GETMORELANGS)
            ShellExecuteW(HWindow, L"open", L"https://github.com/0xeb/sally/discussions", NULL, NULL, SW_SHOWNORMAL);
        if (LOWORD(wParam) == IDB_REFRESHLANGS)
        {
            ListView_DeleteAllItems(HListView);
            int i;
            for (i = 0; i < Items.Count; i++)
                Items[i].Free();
            Items.DestroyMembers();
            Initialize();
            if (GetLanguagesCount() == 0) // should not happen because this dialog is loaded from the .slg module (that .slg cannot be deleted)
            {
                // wide: same MessageBoxW/SALAMANDER_TEXT_VERSIONW() pairing already
                // used at the equivalent startup-time check in sally_entry_lifecycle.cpp (198).
                MessageBoxW(HWindow, L"Unable to find any language file (.SLG) in subdirectory LANG.\n"
                                     L"Please reinstall Open Salamander.",
                            SALAMANDER_TEXT_VERSIONW(), MB_OK | MB_ICONERROR);
                TRACE_E("CLanguageSelectorDialog: unexpected situation (no language file): calling ExitProcess(667).");
                //          ExitProcess(667);
                TerminateProcess(GetCurrentProcess(), 667); // harder exit (this call still performs some operations)
            }
            LoadListView();
        }
        break;
    }

    case WM_NOTIFY:
    {
        if (wParam == IDC_SLG_LIST)
        {
            LPNMHDR nmh = (LPNMHDR)lParam;
            switch (nmh->code)
            {
            case NM_DBLCLK:
            {
                LVHITTESTINFO ht;
                DWORD pos = GetMessagePos();
                ht.pt.x = GET_X_LPARAM(pos);
                ht.pt.y = GET_Y_LPARAM(pos);
                ScreenToClient(HListView, &ht.pt);
                ListView_HitTest(HListView, &ht);
                int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
                if (index != -1 && ht.iItem == index)
                {
                    PostMessage(HWindow, WM_COMMAND, MAKELPARAM(IDOK, BN_CLICKED),
                                (LPARAM)GetDlgItem(HWindow, IDOK));
                    return 0;
                }
                break;
            }

            case LVN_ITEMCHANGED:
            {
                FillControls();
                return 0;
            }
            }
        }
        break;
    }

    case WM_SYSCOLORCHANGE:
    {
        ListView_SetBkColor(HListView, GetSysColor(COLOR_WINDOW));
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CSkillLevelDialog
//

CSkillLevelDialog::CSkillLevelDialog(HWND hParent, int* level)
    : CCommonDialog(HLanguage, IDD_SKILLLEVEL, IDD_SKILLLEVEL, hParent)
{
    Level = level;
}

void CSkillLevelDialog::Transfer(CTransferInfo& ti)
{
    ti.RadioButton(IDC_SL_BEGINNER, SKILL_LEVEL_BEGINNER, *Level);
    ti.RadioButton(IDC_SL_INTERMEDIATE, SKILL_LEVEL_INTERMEDIATE, *Level);
    ti.RadioButton(IDC_SL_ADVANCED, SKILL_LEVEL_ADVANCED, *Level);
}

//****************************************************************************
//
// CCompareArgsDlg
//

CCompareArgsDlg::CCompareArgsDlg(HWND parent, BOOL comparingFiles, std::wstring& compareName1,
                                 std::wstring& compareName2, int* cnfrmShowNamesToCompare)
    // unicodeWnd=TRUE. DialogProc sets this dialog's own caption with
    // SetWindowTextW, and on an ANSI-class dialog USER32 converts that straight
    // back through CP_ACP - so the wide call was silently doing nothing. The
    // caption is class-bound, unlike the SetDlgItemTextW calls beside it, which
    // reach standard child controls that USER32 registers wide either way.
    : CCommonDialog(HLanguage, IDD_USERMENUCOMPAREARGS, comparingFiles ? IDH_USERMENUCOMPAREARGS_F : IDH_USERMENUCOMPAREARGS_D, parent,
                    ooStandard, NULL),
      ComparingFiles(comparingFiles), CompareName1(compareName1), CompareName2(compareName2),
      CnfrmShowNamesToCompare(cnfrmShowNamesToCompare)
{
}

void CCompareArgsDlg::Validate(CTransferInfo& ti)
{
    std::wstring value = GetWindowTextStringW(GetDlgItem(HWindow, IDE_UMC_NAME1));
    if (value.empty())
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_FF_EMPTYSTRING));
        ti.ErrorOn(IDE_UMC_NAME1);
        return;
    }
    value = GetWindowTextStringW(GetDlgItem(HWindow, IDE_UMC_NAME2));
    if (value.empty())
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_FF_EMPTYSTRING));
        ti.ErrorOn(IDE_UMC_NAME2);
        return;
    }
}

void CCompareArgsDlg::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataToWindow)
    {
        SetDlgItemTextW(HWindow, IDE_UMC_NAME1, CompareName1.c_str());
        SetDlgItemTextW(HWindow, IDE_UMC_NAME2, CompareName2.c_str());
    }
    else
    {
        CompareName1 = GetWindowTextStringW(GetDlgItem(HWindow, IDE_UMC_NAME1));
        CompareName2 = GetWindowTextStringW(GetDlgItem(HWindow, IDE_UMC_NAME2));
    }

    int c = !*CnfrmShowNamesToCompare;
    ti.CheckBox(IDC_UMC_SHOWTHISDLG, c);
    *CnfrmShowNamesToCompare = !c;
}

INT_PTR
CCompareArgsDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (!ComparingFiles)
        {
            SetWindowTextW(HWindow, LoadStrW(IDS_USERMENUCOMPAREARGSTITLE));
            SetDlgItemTextW(HWindow, IDT_UMC_NAME1, LoadStrW(IDS_USERMENUCOMPAREARG1));
            SetDlgItemTextW(HWindow, IDT_UMC_NAME2, LoadStrW(IDS_USERMENUCOMPAREARG2));
        }
        CHyperLink* hl = new CHyperLink(HWindow, IDT_UMC_HOWTOREVERT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStrW(IDS_UMCCONFIRMHOWTOREV));
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDB_UMC_BROWSENAME1:
        case IDB_UMC_BROWSENAME2:
        {
            int editID = LOWORD(wParam) == IDB_UMC_BROWSENAME1 ? IDE_UMC_NAME1 : IDE_UMC_NAME2;
            if (ComparingFiles)
                BrowseCommand(HWindow, editID, IDS_ALLFILTER);
            else
            {
                // wide: GetTargetDirectory's browse dialog best-fit-narrows the
                // chosen path before Sally ever sees it - same shape as the earlier
                // siblings. Standard child controls (this edit box) are always registered wide
                // by USER32 regardless of the dialog's own unicodeWnd setting (see this file's
                // comment on CCompareArgsDlg above), so GetDlgItemTextW/
                // SetDlgItemTextW are safe here independent of the dialog's own class.
                const std::wstring initDirW = GetWindowTextStringW(GetDlgItem(HWindow, editID));
                std::wstring pathW;
                if (GetTargetDirectoryW(HWindow, HWindow, LoadStrW(IDS_BROWSEUMCDIRTITLE),
                                        LoadStrW(IDS_BROWSEUMCDIRTEXT), pathW, FALSE, initDirW.c_str()))
                {
                    SetDlgItemTextW(HWindow, editID, pathW.c_str());
                }
            }
            return TRUE;
        }
        }
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}
