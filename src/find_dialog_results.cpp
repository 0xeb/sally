// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "ui/UnicodeHistoryUtils.h"
#include "combo_dark_paint.h"

#include <vector>

#include "menu.h"
#include "find_dialog_theme_ids.h"
#include "ui/IPrompter.h"
#include "common/IFileSystem.h"
#include "common/PathDisplayUtils.h" // MakeCompactPathBuffer
#include "common/unicode/ComboSyncPolicy.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/unicode/helpers.h"
#include "common/find/FindDialogSeed.h"
#include "common/find/FindResultPersistence.h"
#include "common/find/FindActionPaths.h"
#include "common/text/LegacySearchTextEncoding.h"
#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "viewer.h"
#include "shellib.h"
#include "find.h"
#include "gui.h"
#include "usermenu.h"
#include "execute.h"
#include "tasklist.h"
#include "darkmode.h"

#include <shlwapi.h>
#include <uxtheme.h>

// wide - the window title is built from a wide LoadStrW/GetMasksString
// pair below; a narrow format string would force a lossy round trip on every use.
const wchar_t* MINIMIZED_FINDING_CAPTION = L"(%d) %s [%s %s]";
const wchar_t* NORMAL_FINDING_CAPTION = L"%s [%s %s]";

BOOL FindManageInUse = FALSE;
BOOL FindIgnoreInUse = FALSE;

static const UINT_PTR FIND_COMBO_SKIN_SUBCLASS_ID = 1;
static const UINT_PTR FIND_COMBO_EDIT_SKIN_SUBCLASS_ID = 2;
static const UINT_PTR FIND_ADVANCED_TEXT_SKIN_SUBCLASS_ID = 3;
static const UINT_PTR FIND_STATUS_SKIN_SUBCLASS_ID = 1;
static const UINT WM_USER_FIND_DELAYED_THEME = WM_APP + 500;
// Reapply the active-panel seed after the framework's initial transfer.
static const UINT WM_USER_FIND_LOOKIN_W_OVERRIDE = WM_APP + 501;
static const COLORREF FIND_DARK_LINE = RGB(55, 55, 58);
static const COLORREF FIND_DARK_FRAME = RGB(62, 62, 66);
static const COLORREF FIND_DARK_BUTTON = RGB(52, 52, 56);
static const COLORREF FIND_DARK_SECTION_LINE = RGB(82, 82, 86);

struct CFindComboSkinState
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

struct CFindAdvancedTextSkinState
{
    LONG_PTR Style;
    LONG_PTR ExStyle;
};

static LRESULT CALLBACK FindComboSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static LRESULT CALLBACK FindComboEditSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static LRESULT CALLBACK FindAdvancedTextSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static LRESULT CALLBACK FindStatusSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
static void ApplyFindComboEditSkin(HWND hEdit, BOOL useDark);

static void EscapeFindLookInPathSeparatorsW(std::wstring& text)
{
    for (size_t pos = 0; pos < text.length(); pos++)
    {
        if (text[pos] == L';')
        {
            text.insert(pos, 1, L';');
            pos++;
        }
    }
}

// Read a control's text wide. Replaces
// CUnicodeNameInputController::GetText() now that the Look-in combo is the
// dialog's own control rather than a replacement the controller owned.
static std::wstring GetWindowTextWide(HWND hWnd)
{
    if (hWnd == NULL)
        return std::wstring();
    const int len = GetWindowTextLengthW(hWnd);
    if (len <= 0)
        return std::wstring();
    std::vector<wchar_t> buffer((size_t)len + 1, 0);
    GetWindowTextW(hWnd, buffer.data(), len + 1);
    return std::wstring(buffer.data());
}

// Takes the combo HWND directly; it used to take the controller by
// reference, which was the last thing tying this helper to that class.
// Puts the Look-in combo into wide mode. Replaces
// CUnicodeNameInputController::EnableForCombo, which used to hide this combo and
// build a Unicode replacement; since P0.5a the native control keeps wide text with
// the word-break subclass installed, so only seeding and the font remain.
// Keeps at most one font clone; the caller frees it on WM_DESTROY.
static void ActivateWideLookInCombo(HWND hDlg, const std::wstring& textW, HFONT& ownedFont)
{
    HWND hCombo = GetDlgItem(hDlg, IDC_FIND_LOOKIN);
    if (hCombo == NULL)
        return;

    // Never free the font while the combo still has it selected.
    //
    // Freeing first and asking afterwards was not merely a window in which a paint
    // could touch freed GDI memory. EnsureComboFontCanRenderW returns NULL whenever
    // the combo's CURRENT font already copes - which is exactly the case once our own
    // clone (created with DEFAULT_CHARSET) is installed - so on the second call the
    // old code deleted that clone, got NULL back, and left the control holding a dead
    // handle indefinitely.
    HFONT replacement = EnsureComboFontCanRenderW(hCombo, textW.c_str());
    if (replacement != NULL)
    {
        // A new clone is selected now, so nothing points at the previous one.
        if (ownedFont != NULL)
            DeleteObject(ownedFont);
        ownedFont = replacement;
    }
    // Otherwise the font in place already renders this text - often because it IS
    // the clone we own - so keep it selected and keep owning it.

    SendMessageW(hCombo, WM_SETTEXT, 0, (LPARAM)textW.c_str());
}

static void ReplaceFindLookInSelectionW(HWND hCombo,
                                        const std::wstring& replacement,
                                        DWORD start, DWORD end)
{
    std::wstring text = GetWindowTextWide(hCombo);
    size_t startPos = start;
    size_t endPos = end;
    if (startPos > text.length())
        startPos = text.length();
    if (endPos > text.length())
        endPos = text.length();
    if (endPos < startPos)
        endPos = startPos;

    std::wstring insert = replacement;
    EscapeFindLookInPathSeparatorsW(insert);

    if (startPos > 0)
    {
        size_t leftIndex = startPos - 1;
        size_t scan = leftIndex;
        while (scan < text.length() && text[scan] == L';')
        {
            if (scan == 0)
            {
                scan = text.length();
                break;
            }
            scan--;
        }
        size_t semicolonCount = (scan == text.length()) ? leftIndex + 1 : leftIndex - scan;
        if ((semicolonCount & 1) == 0)
            insert.insert(0, L"; ");
    }
    if (endPos < text.length() &&
        (text[endPos] != L';' || endPos + 1 < text.length() && text[endPos + 1] == L';'))
    {
        insert.append(L"; ");
    }

    text.replace(startPos, endPos - startPos, insert);
    SendMessageW(hCombo, WM_SETTEXT, 0, (LPARAM)text.c_str());

    DWORD caret = (DWORD)(startPos + insert.length());
    SendMessage(hCombo, CB_SETEDITSEL, 0, MAKELPARAM(caret, caret));
    SetFocus(hCombo);
}

static int CompareFindTextW(const std::wstring& left, const std::wstring& right)
{
    if (Configuration.SortDetectNumbers)
        return StrCmpLogicalW(left.c_str(), right.c_str());
    if (Configuration.SortUsesLocale)
    {
        int ret = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                                 left.c_str(), -1, right.c_str(), -1);
        if (ret != 0)
            return ret - CSTR_EQUAL;
    }
    return _wcsicmp(left.c_str(), right.c_str());
}

static BOOL FindTextMatchesW(const std::wstring& text, const wchar_t* pattern, BOOL partial)
{
    if (pattern == NULL)
        return FALSE;

    if (!partial)
        return CompareFindTextW(text, pattern) == 0;

    size_t patternLen = wcslen(pattern);
    if (patternLen == 0)
        return TRUE;
    if (text.length() < patternLen)
        return FALSE;

    int ret = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                             text.c_str(), (int)patternLen, pattern, (int)patternLen);
    if (ret != 0)
        return ret == CSTR_EQUAL;
    return _wcsnicmp(text.c_str(), pattern, patternLen) == 0;
}

static int FindListItemByNameW(CFoundFilesListView* listView, int startIndex, UINT flags, const wchar_t* pattern)
{
    int ret = -1;
    if ((flags & LVFI_STRING) == 0 && (flags & LVFI_PARTIAL) == 0)
        return ret;

    // The control normally sends LVFI_STRING only, so keep the historical
    // prefix-search behavior while comparing against the wide item name.
    BOOL partial = TRUE;
    int count = listView->GetCount();
    if (startIndex < 0)
        startIndex = 0;
    if (startIndex > count)
        startIndex = count;
    int i;
    for (i = startIndex; i < count; i++)
    {
        const CFoundFilesData* item = listView->At(i);
        if (FindTextMatchesW(item->NameW, pattern, partial))
            return i;
    }

    if (flags & LVFI_WRAP)
    {
        for (i = 0; i < startIndex; i++)
        {
            const CFoundFilesData* item = listView->At(i);
            if (FindTextMatchesW(item->NameW, pattern, partial))
                return i;
        }
    }
    return ret;
}

static void RedrawFindResultItem(HWND hListView, int itemIndex)
{
    if (hListView == NULL || itemIndex < 0 || itemIndex >= ListView_GetItemCount(hListView))
        return;

    ListView_RedrawItems(hListView, itemIndex, itemIndex);

    RECT itemRect;
    if (ListView_GetItemRect(hListView, itemIndex, &itemRect, LVIR_BOUNDS))
        InvalidateRect(hListView, &itemRect, TRUE);
}

static void FindFillRectSolid(HDC hdc, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(DC_BRUSH));
    COLORREF oldColor = SetDCBrushColor(hdc, color);
    FillRect(hdc, rect, (HBRUSH)GetStockObject(DC_BRUSH));
    SetDCBrushColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
}

static void FindDrawRectOutline(HDC hdc, const RECT* rect, COLORREF color)
{
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    COLORREF oldColor = SetDCPenColor(hdc, color);
    Rectangle(hdc, rect->left, rect->top, rect->right, rect->bottom);
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
}

static std::wstring LoadFindResultsFilterW()
{
    std::wstring filter = LoadStrW(IDS_FIND_RESULTS_FILTER);
    for (wchar_t& ch : filter)
    {
        if (ch == L'|')
            ch = L'\0';
    }
    filter.push_back(L'\0');
    return filter;
}

static bool FindResultsPathHasExtensionW(const std::wstring& fileName)
{
    size_t slash = fileName.find_last_of(L"\\/");
    size_t dot = fileName.find_last_of(L'.');
    return dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash);
}

static bool ConfirmFindResultsOverwriteIfNeeded(const std::wstring& fileName, bool extensionAppended)
{
    if (!extensionAppended)
        return true;

    IFileSystem* fs = gFileSystem != NULL ? gFileSystem : GetWin32FileSystem();
    if (!fs->FileExists(fileName.c_str()))
        return true;

    if (gPrompter == NULL)
        return MessageBoxW(NULL, fileName.c_str(), LoadStrW(IDS_QUESTION),
                           MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
    return gPrompter->ConfirmOverwrite(fileName.c_str(), NULL).type == PromptResult::kYes;
}

static bool BrowseFindResultsFileNameW(HWND owner, BOOL save, std::wstring& fileName,
                                       sally::find::FindResultsFormat& format)
{
    std::wstring filter = LoadFindResultsFilterW();

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = LoadStrW(save ? IDS_FIND_RESULTS_SAVE_TITLE : IDS_FIND_RESULTS_LOAD_TITLE);
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (save)
        ofn.Flags |= OFN_OVERWRITEPROMPT;
    else
        ofn.Flags |= OFN_FILEMUSTEXIST;

    const BOOL selected = save ? SafeGetSaveFileNameOwnedW(&ofn, fileName)
                               : SafeGetOpenFileNameOwnedW(&ofn, fileName);
    if (!selected)
        return false;
    if (!sally::find::TryFindResultsFormatFromPathOrFilter(fileName, ofn.nFilterIndex, &format))
    {
        if (gPrompter != NULL)
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_FIND_RESULTS_UNSUPPORTED));
        return false;
    }

    if (save)
    {
        bool hadExtension = FindResultsPathHasExtensionW(fileName);
        sally::find::AppendFindResultsDefaultExtension(fileName, format);
        if (!ConfirmFindResultsOverwriteIfNeeded(fileName, !hadExtension))
            return false;
    }
    return true;
}

static void FindDrawComboArrow(HDC hdc, const RECT* rect, COLORREF color)
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

static BOOL GetChildRectInParent(HWND hParent, HWND hChild, RECT* rect)
{
    if (hParent == NULL || hChild == NULL || rect == NULL || !IsWindow(hParent) || !IsWindow(hChild))
        return FALSE;

    // A drop-list combo reports itself (or nothing) as hwndItem; treating the control as its own
    // child gives a rect covering the whole client, which ExcludeClipRect then blanks out.
    if (hChild == hParent)
        return FALSE;

    if (!GetWindowRect(hChild, rect))
        return FALSE;
    MapWindowPoints(NULL, hParent, (POINT*)rect, 2);
    return TRUE;
}

static BOOL GetFindChildRectInDialog(HWND hDialog, int ctrlID, RECT* rect)
{
    HWND hChild = GetDlgItem(hDialog, ctrlID);
    return GetChildRectInParent(hDialog, hChild, rect);
}

static void SetFindWindowExStyle(HWND hwnd, LONG_PTR exStyle)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    if (GetWindowLongPtr(hwnd, GWL_EXSTYLE) == exStyle)
        return;

    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void SetFindWindowStyle(HWND hwnd, LONG_PTR style)
{
    if (hwnd == NULL || !IsWindow(hwnd))
        return;

    if (GetWindowLongPtr(hwnd, GWL_STYLE) == style)
        return;

    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void UpdateFindComboSkin(HWND hCombo, CFindComboSkinState* state)
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

    DarkModeColors colors;
    BOOL useDark = DarkMode_GetColors(&colors);
    LONG_PTR darkStyleMask = WS_BORDER;
    LONG_PTR darkEdgeMask = WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
    SetFindWindowStyle(hCombo, useDark ? (state->ComboStyle & ~darkStyleMask) : state->ComboStyle);
    SetFindWindowExStyle(hCombo, useDark ? (state->ComboExStyle & ~darkEdgeMask) : state->ComboExStyle);

    if (state->HEdit != NULL && state->EditStyleKnown && IsWindow(state->HEdit))
        SetFindWindowStyle(state->HEdit, useDark ? (state->EditStyle & ~darkStyleMask) : state->EditStyle);
    if (state->HEdit != NULL && state->EditExStyleKnown && IsWindow(state->HEdit))
        SetFindWindowExStyle(state->HEdit, useDark ? (state->EditExStyle & ~darkEdgeMask) : state->EditExStyle);

    if (!state->ApplyingTheme &&
        (!state->ThemeKnown || state->LastUseDark != useDark || editChanged))
    {
        state->ApplyingTheme = TRUE;
        SetWindowTheme(hCombo, useDark ? L"" : NULL, NULL);
        ApplyFindComboEditSkin(state->HEdit, useDark);
        if (cbi.hwndList != NULL && IsWindow(cbi.hwndList))
            SetWindowTheme(cbi.hwndList, useDark ? L"DarkMode_Explorer" : NULL, NULL);
        state->ThemeKnown = TRUE;
        state->LastUseDark = useDark;
        state->ApplyingTheme = FALSE;
    }

    RedrawWindow(hCombo, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

static void ApplyFindComboSkin(HWND hCombo)
{
    if (hCombo == NULL || !IsWindow(hCombo))
        return;

    DWORD_PTR data = 0;
    CFindComboSkinState* state = NULL;
    if (GetWindowSubclass(hCombo, FindComboSkinSubclassProc, FIND_COMBO_SKIN_SUBCLASS_ID, &data))
        state = (CFindComboSkinState*)data;
    else
    {
        state = new CFindComboSkinState;
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
        if (!SetWindowSubclass(hCombo, FindComboSkinSubclassProc, FIND_COMBO_SKIN_SUBCLASS_ID, (DWORD_PTR)state))
        {
            delete state;
            return;
        }
    }

    UpdateFindComboSkin(hCombo, state);
}

static void ApplyFindComboSkins(HWND hDialog)
{
    // #99: skin all four Find comboboxes (the "Type:" combo IDC_FIND_FILETYPE was missing
    // here, leaving it white in dark mode). The set lives in find_dialog_theme_ids.cpp.
    int count = 0;
    const int* comboIDs = GetFindThemedComboIds(count);
    for (int i = 0; i < count; i++)
        ApplyFindComboSkin(GetDlgItem(hDialog, comboIDs[i]));
}

static void ApplyFindSeparatorLines(HWND hDialog)
{
    int lineIDs[] = {IDC_FIND_LINE1, IDC_FIND_LINE2};
    BOOL useDark = DarkMode_ShouldUseDark();
    for (int i = 0; i < _countof(lineIDs); i++)
    {
        HWND hLine = GetDlgItem(hDialog, lineIDs[i]);
        if (hLine != NULL && IsWindow(hLine))
            ShowWindow(hLine, useDark ? SW_HIDE : SW_SHOWNA);
    }
}

static BOOL PaintFindDialogSeparatorLines(HWND hDialog, HDC paintDC)
{
    if (!DarkMode_ShouldUseDark())
        return FALSE;

    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = GetDC(hDialog);
    if (hdc == NULL)
        return FALSE;

    int lineIDs[] = {IDC_FIND_LINE1, IDC_FIND_LINE2};
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    COLORREF oldColor = SetDCPenColor(hdc, FIND_DARK_SECTION_LINE);
    for (int i = 0; i < _countof(lineIDs); i++)
    {
        RECT rect;
        if (!GetFindChildRectInDialog(hDialog, lineIDs[i], &rect))
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

static BOOL PaintFindDarkCombo(HWND hwnd, HDC paintDC)
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

    FindFillRectSolid(paintDC, &client, colors.InputBackground);

    int buttonWidth = max(GetSystemMetrics(SM_CXVSCROLL), client.bottom - client.top);
    RECT button = client;
    button.left = max(client.left + 1, client.right - buttonWidth - 1);
    button.top = client.top + 1;
    button.right = client.right - 1;
    button.bottom = client.bottom - 1;
    if (button.right > button.left && button.bottom > button.top)
    {
        FindFillRectSolid(paintDC, &button, FIND_DARK_BUTTON);
        HGDIOBJ oldPen = SelectObject(paintDC, GetStockObject(DC_PEN));
        COLORREF oldPenColor = SetDCPenColor(paintDC, FIND_DARK_LINE);
        MoveToEx(paintDC, button.left, button.top, NULL);
        LineTo(paintDC, button.left, button.bottom);
        SetDCPenColor(paintDC, oldPenColor);
        SelectObject(paintDC, oldPen);
        FindDrawComboArrow(paintDC, &button, IsWindowEnabled(hwnd) ? colors.InputText : colors.DisabledText);
    }

    // IDC_FIND_FILETYPE is CBS_DROPDOWNLIST and so has no edit child - without this its
    // selected value ("All files and folders") was never drawn and the field read as empty.
    // The other three skinned combos here are CBS_DROPDOWN, whose edit child covered for the
    // omission, which is why this went unnoticed since v1.0.14 and was then copied wholesale
    // into the general dark-mode painter.
    if (!haveEditRect)
        ComboDarkDrawSelectedItem(hwnd, paintDC, client, button, {colors.InputText, colors.DisabledText, colors.Highlight, colors.HighlightText});

    RestoreDC(paintDC, savedDC);
    FindDrawRectOutline(paintDC, &client, FIND_DARK_FRAME);
    return TRUE;
}

static void PaintFindDarkComboFrame(HWND hwnd)
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
    FindDrawRectOutline(hdc, &rect, FIND_DARK_FRAME);
    ReleaseDC(hwnd, hdc);
}

static void PaintFindDarkComboEditFrame(HWND hwnd)
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
    FindFillRectSolid(hdc, &window, colors.InputBackground);
    RestoreDC(hdc, savedDC);

    FindDrawRectOutline(hdc, &window, FIND_DARK_FRAME);
    ReleaseDC(hwnd, hdc);
}

static void ApplyFindComboEditSkin(HWND hEdit, BOOL useDark)
{
    if (hEdit == NULL || !IsWindow(hEdit))
        return;

    SetWindowSubclass(hEdit, FindComboEditSkinSubclassProc, FIND_COMBO_EDIT_SKIN_SUBCLASS_ID, 0);
    SetWindowTheme(hEdit, useDark ? L"" : NULL, NULL);
    RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
}

static LRESULT CALLBACK FindComboEditSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            PaintFindDarkComboEditFrame(hwnd);
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (DarkMode_ShouldUseDark())
            PaintFindDarkComboEditFrame(hwnd);
        return ret;
    }

    case WM_ERASEBKGND:
    {
        DarkModeColors colors;
        if (DarkMode_GetColors(&colors))
        {
            RECT client;
            GetClientRect(hwnd, &client);
            FindFillRectSolid((HDC)wParam, &client, colors.InputBackground);
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
        RemoveWindowSubclass(hwnd, FindComboEditSkinSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK FindComboSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    CFindComboSkinState* state = (CFindComboSkinState*)dwRefData;

    switch (uMsg)
    {
    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            PaintFindDarkComboFrame(hwnd);
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
                PaintFindDarkCombo(hwnd, hdc);
            HANDLES(EndPaint(hwnd, &ps));
            return 0;
        }
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintFindDarkCombo(hwnd, (HDC)wParam))
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
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (state != NULL && !state->ApplyingTheme)
            UpdateFindComboSkin(hwnd, state);
        else
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, FindComboSkinSubclassProc, uIdSubclass);
        delete state;
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK FindAdvancedTextSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    CFindAdvancedTextSkinState* state = (CFindAdvancedTextSkinState*)dwRefData;

    switch (uMsg)
    {
    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            PaintFindDarkComboEditFrame(hwnd);
            return 0;
        }
        break;
    }

    case WM_PAINT:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (DarkMode_ShouldUseDark())
            PaintFindDarkComboEditFrame(hwnd);
        return ret;
    }

    case WM_ERASEBKGND:
    {
        DarkModeColors colors;
        if (DarkMode_GetColors(&colors))
        {
            RECT client;
            GetClientRect(hwnd, &client);
            FindFillRectSolid((HDC)wParam, &client, colors.InputBackground);
            return TRUE;
        }
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
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, FindAdvancedTextSkinSubclassProc, uIdSubclass);
        delete state;
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static void ApplyFindAdvancedTextSkin(HWND hDialog)
{
    HWND hEdit = GetDlgItem(hDialog, IDC_FIND_ADVANCED_TEXT);
    if (hEdit == NULL || !IsWindow(hEdit))
        return;

    DWORD_PTR data = 0;
    CFindAdvancedTextSkinState* state = NULL;
    if (GetWindowSubclass(hEdit, FindAdvancedTextSkinSubclassProc, FIND_ADVANCED_TEXT_SKIN_SUBCLASS_ID, &data))
        state = (CFindAdvancedTextSkinState*)data;
    else
    {
        state = new CFindAdvancedTextSkinState;
        if (state == NULL)
            return;
        state->Style = GetWindowLongPtr(hEdit, GWL_STYLE);
        state->ExStyle = GetWindowLongPtr(hEdit, GWL_EXSTYLE);
        if (!SetWindowSubclass(hEdit, FindAdvancedTextSkinSubclassProc, FIND_ADVANCED_TEXT_SKIN_SUBCLASS_ID, (DWORD_PTR)state))
        {
            delete state;
            return;
        }
    }

    BOOL useDark = DarkMode_ShouldUseDark();
    SetFindWindowStyle(hEdit, useDark ? (state->Style & ~WS_BORDER) : state->Style);
    SetFindWindowExStyle(hEdit, useDark ? (state->ExStyle & ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)) : state->ExStyle);
    SetWindowTheme(hEdit, useDark ? L"" : NULL, NULL);
    RedrawWindow(hEdit, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
}

static void DrawFindStatusSizeGrip(HDC hdc, const RECT* client)
{
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
    COLORREF oldColor = SetDCPenColor(hdc, FIND_DARK_LINE);
    int right = client->right - 4;
    int bottom = client->bottom - 4;
    for (int i = 0; i < 3; i++)
    {
        int offset = i * 4;
        MoveToEx(hdc, right - offset - 8, bottom, NULL);
        LineTo(hdc, right, bottom - offset - 8);
    }
    SetDCPenColor(hdc, oldColor);
    SelectObject(hdc, oldPen);
}

static BOOL PaintFindDarkStatusBar(HWND hwnd, HDC paintDC)
{
    DarkModeColors colors;
    if (!DarkMode_GetColors(&colors))
        return FALSE;

    PAINTSTRUCT ps;
    HDC hdc = paintDC;
    if (hdc == NULL)
        hdc = HANDLES(BeginPaint(hwnd, &ps));
    if (hdc == NULL)
        return FALSE;

    RECT client;
    GetClientRect(hwnd, &client);
    FindFillRectSolid(hdc, &client, colors.DialogBackground);

    HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
    HFONT hOldFont = NULL;
    if (hFont != NULL)
        hOldFont = (HFONT)SelectObject(hdc, hFont);

    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    COLORREF oldTextColor = SetTextColor(hdc, colors.DialogText);

    int partCount = (int)SendMessage(hwnd, SB_GETPARTS, 0, 0);
    if (partCount <= 0)
        partCount = 1;

    for (int i = 0; i < partCount; i++)
    {
        RECT part = client;
        if (partCount > 1)
            SendMessage(hwnd, SB_GETRECT, i, (LPARAM)&part);
        part.left += 4;
        part.right -= 4;
        if (part.right <= part.left)
            continue;

        DWORD textInfo = (DWORD)SendMessageW(hwnd, SB_GETTEXTLENGTHW, i, 0);
        WORD textType = HIWORD(textInfo);
        int textLen = LOWORD(textInfo);
        std::wstring text;
        if (textLen > 0)
        {
            text.resize(textLen + 1);
            textInfo = (DWORD)SendMessageW(hwnd, SB_GETTEXTW, i, (LPARAM)&text[0]);
            textType = HIWORD(textInfo);
            text.resize(wcslen(text.c_str()));
        }

        if ((textType & SBT_OWNERDRAW) != 0)
        {
            DRAWITEMSTRUCT di = {0};
            di.CtlType = ODT_STATIC;
            di.CtlID = IDC_FIND_STATUS;
            di.itemID = i;
            di.itemAction = ODA_DRAWENTIRE;
            di.hwndItem = hwnd;
            di.hDC = hdc;
            di.rcItem = part;
            SendMessage(GetParent(hwnd), WM_DRAWITEM, IDC_FIND_STATUS, (LPARAM)&di);
        }
        else if (!text.empty())
        {
            DrawTextW(hdc, text.c_str(), -1, &part, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);
        }
    }

    if ((GetWindowLongPtr(hwnd, GWL_STYLE) & SBARS_SIZEGRIP) != 0)
        DrawFindStatusSizeGrip(hdc, &client);

    SetTextColor(hdc, oldTextColor);
    SetBkMode(hdc, oldBkMode);
    if (hOldFont != NULL)
        SelectObject(hdc, hOldFont);

    if (paintDC == NULL)
        HANDLES(EndPaint(hwnd, &ps));
    return TRUE;
}

static void PaintFindDarkStatusFrame(HWND hwnd)
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
    FindFillRectSolid(hdc, &rect, colors.DialogBackground);
    ReleaseDC(hwnd, hdc);
}

static LRESULT CALLBACK FindStatusSkinSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    UNREFERENCED_PARAMETER(dwRefData);

    switch (uMsg)
    {
    case WM_PAINT:
    {
        if (DarkMode_ShouldUseDark() && PaintFindDarkStatusBar(hwnd, NULL))
            return 0;
        break;
    }

    case WM_PRINTCLIENT:
    {
        if (DarkMode_ShouldUseDark() && PaintFindDarkStatusBar(hwnd, (HDC)wParam))
            return 0;
        break;
    }

    case WM_NCPAINT:
    {
        if (DarkMode_ShouldUseDark())
        {
            PaintFindDarkStatusFrame(hwnd);
            return TRUE;
        }
        break;
    }

    case WM_ERASEBKGND:
    {
        if (DarkMode_ShouldUseDark())
            return TRUE;
        break;
    }

    case SB_SETTEXTA:
    case SB_SETTEXTW:
    case SB_SETPARTS:
    case WM_SIZE:
    case WM_ENABLE:
    case WM_THEMECHANGED:
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    {
        LRESULT ret = DefSubclassProc(hwnd, uMsg, wParam, lParam);
        if (DarkMode_ShouldUseDark())
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        return ret;
    }

    case WM_NCDESTROY:
    {
        RemoveWindowSubclass(hwnd, FindStatusSkinSubclassProc, uIdSubclass);
        break;
    }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static void ApplyFindResultsListColors(HWND hDialog)
{
    HWND hListView = GetDlgItem(hDialog, IDC_FIND_RESULTS);
    if (hListView == NULL)
        return;

    if (DarkMode_ShouldUseDark())
    {
        DarkMode_ApplyListTreeThemeRecursive(hListView);
        return;
    }

    SetWindowTheme(hListView, L"Explorer", NULL);
    HWND hHeader = ListView_GetHeader(hListView);
    if (hHeader != NULL)
        SetWindowTheme(hHeader, L"Explorer", NULL);

    COLORREF bgColor = GetSysColor(COLOR_WINDOW);
    ListView_SetBkColor(hListView, bgColor);
    ListView_SetTextBkColor(hListView, bgColor);
    ListView_SetTextColor(hListView, GetSysColor(COLOR_WINDOWTEXT));
    if (hHeader != NULL)
        InvalidateRect(hHeader, NULL, TRUE);
    InvalidateRect(hListView, NULL, TRUE);
}

static void ApplyFindStatusBarColors(HWND hStatusBar)
{
    if (hStatusBar == NULL || !IsWindow(hStatusBar))
        return;

    DarkModeColors colors;
    BOOL useDark = DarkMode_GetColors(&colors);
    SetWindowSubclass(hStatusBar, FindStatusSkinSubclassProc, FIND_STATUS_SKIN_SUBCLASS_ID, 0);
    SetWindowTheme(hStatusBar, useDark ? L"" : NULL, NULL);
    SendMessage(hStatusBar, SB_SETBKCOLOR, 0, useDark ? colors.DialogBackground : CLR_DEFAULT);
    RedrawWindow(hStatusBar, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

static void ApplyFindDialogTheme(HWND hDialog, HWND hStatusBar)
{
    if (hDialog == NULL || !IsWindow(hDialog))
        return;

    DarkMode_ApplyTitleBar(hDialog);
    DarkMode_ApplyListTreeThemeRecursive(hDialog);
    ApplyFindComboSkins(hDialog);
    ApplyFindSeparatorLines(hDialog);
    ApplyFindAdvancedTextSkin(hDialog);
    ApplyFindResultsListColors(hDialog);
    ApplyFindStatusBarColors(hStatusBar);
    RedrawWindow(hDialog, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

//****************************************************************************
//
// CFindTBHeader
//

void CFindOptions::InitMenu(CMenuPopup* popup, BOOL enabled, int originalCount)
{
    int count = popup->GetItemCount();
    if (count > originalCount)
    {
        // remove any previously inserted items
        popup->RemoveItemsRange(originalCount, count - 1);
    }

    if (Items.Count > 0)
    {
        MENU_ITEM_INFO mii;

        // if there are items to append, insert a separator first
        mii.Mask = MENU_MASK_TYPE;
        mii.Type = MENU_TYPE_SEPARATOR;
        popup->InsertItem(-1, TRUE, &mii);

        // append the displayed portion of the items
        int maxCount = CM_FIND_OPTIONS_LAST - CM_FIND_OPTIONS_FIRST;
        int i;
        for (i = 0; i < min(Items.Count, maxCount); i++)
        {
            mii.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_STRING | MENU_MASK_ID;
            mii.Type = MENU_TYPE_STRING;
            mii.State = enabled ? 0 : MENU_STATE_GRAYED;
            if (Items[i]->AutoLoad)
                mii.State |= MENU_STATE_DEFAULT;
            mii.ID = CM_FIND_OPTIONS_FIRST + i;
            mii.String = const_cast<wchar_t*>(Items[i]->ItemName.c_str());
            popup->InsertItem(-1, TRUE, &mii);
        }
    }
}

//****************************************************************************
//
// CFoundFilesData
//

BOOL CFoundFilesData::Set(const wchar_t* path, const wchar_t* name, const CQuadWord& size, DWORD attr,
                          const FILETIME* lastWrite, BOOL isDir)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE5("CFoundFilesData::Set(%ls, %ls, %g, 0x%X, )", path, name, size.GetDouble(), attr);
    // was a four-argument mirror: narrow path/name stored as-is, wide halves
    // taken from the caller when supplied and AnsiToWide()d from the narrow half otherwise.
    PathW = path != NULL ? path : L"";
    NameW = name != NULL ? name : L"";
    Size = size;
    Attr = attr;
    LastWrite = *lastWrite;
    IsDir = isDir ? 1 : 0;
    return TRUE;
}

std::wstring CFoundFilesData::GetNameTextW(int fileNameFormat) const
{
    return AlterFileNameW(NameW.c_str(), fileNameFormat, 0, IsDir != 0);
}

std::wstring CFoundFilesData::GetFullNameW() const
{
    std::wstring fullName = PathW;
    if (!fullName.empty() && fullName[fullName.length() - 1] != L'\\')
        fullName += L'\\';
    fullName += NameW;
    return fullName;
}

std::wstring CFoundFilesData::GetFullNameTextW(int fileNameFormat) const
{
    std::wstring fullName = PathW;
    if (!fullName.empty() && fullName[fullName.length() - 1] != L'\\')
        fullName += L'\\';
    fullName += GetNameTextW(fileNameFormat);
    return fullName;
}

std::wstring CFoundFilesData::GetTextW(int i, int fileNameFormat) const
{
    switch (i)
    {
    case 0:
        return GetNameTextW(fileNameFormat);

    case 1:
        return PathW;

    case 2:
    {
        if (IsDir)
            return DirColumnStrW.c_str();

        return NumberToStr(Size);
    }

    case 3:
    {
        wchar_t buffer[100];
        SYSTEMTIME st;
        FILETIME ft;
        if (FileTimeToLocalFileTime(&LastWrite, &ft) &&
            FileTimeToSystemTime(&ft, &st))
        {
            if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, buffer, _countof(buffer)) == 0)
                swprintf_s(buffer, L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);
        }
        else
            wcscpy_s(buffer, LoadStrW(IDS_INVALID_DATEORTIME));
        return buffer;
    }

    case 4:
    {
        wchar_t buffer[100];
        SYSTEMTIME st;
        FILETIME ft;
        if (FileTimeToLocalFileTime(&LastWrite, &ft) &&
            FileTimeToSystemTime(&ft, &st))
        {
            if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, buffer, _countof(buffer)) == 0)
                swprintf_s(buffer, L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
        }
        else
            wcscpy_s(buffer, LoadStrW(IDS_INVALID_DATEORTIME));
        return buffer;
    }

    default:
    {
        wchar_t attrs[20];
        GetAttrsStringW(attrs, Attr);
        return attrs;
    }
    }
}

//****************************************************************************
//
// CFoundFilesListView
//

CFoundFilesListView::CFoundFilesListView(HWND dlg, int ctrlID, CFindDialog* findDialog)
    : Data(1000, 500), DataForRefine(1, 1000), CWindow(dlg, ctrlID)
{
    FindDialog = findDialog;
    HANDLES(InitializeCriticalSection(&DataCriticalSection));

    // add this panel to the array of sources for enumerating files in viewers
    EnumFileNamesAddSourceUID(HWindow, &EnumFileNamesSourceUID);
}

CFoundFilesListView::~CFoundFilesListView()
{
    // remove this panel from the array of sources for enumerating files in viewers
    EnumFileNamesRemoveSourceUID(HWindow);

    HANDLES(DeleteCriticalSection(&DataCriticalSection));
}

CFoundFilesData*
CFoundFilesListView::At(int index)
{
    CFoundFilesData* ptr;
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    ptr = Data[index];
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
    return ptr;
}

void CFoundFilesListView::DestroyMembers()
{
    //  HANDLES(EnterCriticalSection(&DataCriticalSection));
    Data.DestroyMembers();
    //  HANDLES(LeaveCriticalSection(&DataCriticalSection));
}

void CFoundFilesListView::Delete(int index)
{
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    Data.Delete(index);
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
}

int CFoundFilesListView::GetCount()
{
    int count;
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    count = Data.Count;
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
    return count;
}

int CFoundFilesListView::Add(CFoundFilesData* item)
{
    int index;
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    index = Data.Add(item);
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
    return index;
}

BOOL CFoundFilesListView::TakeDataForRefine()
{
    DataForRefine.DestroyMembers();
    int i;
    for (i = 0; i < Data.Count; i++)
    {
        CFoundFilesData* refineData = Data[i];
        DataForRefine.Add(refineData);
        if (!DataForRefine.IsGood())
        {
            DataForRefine.ResetState();
            DataForRefine.DetachMembers();
            return FALSE;
        }
    }
    Data.DetachMembers();
    return TRUE;
}

void CFoundFilesListView::DestroyDataForRefine()
{
    DataForRefine.DestroyMembers();
}

int CFoundFilesListView::GetDataForRefineCount()
{
    return DataForRefine.Count;
}

CFoundFilesData*
CFoundFilesListView::GetDataForRefine(int index)
{
    CFoundFilesData* ptr;
    ptr = DataForRefine[index];
    return ptr;
}

void CFoundFilesListView::GetSelectedPaths(std::vector<std::wstring>& paths)
{
    // this method is invoked only from the main thread
    paths.clear();
    int index = -1;
    do
    {
        index = ListView_GetNextItem(HWindow, index, LVIS_SELECTED);
        if (index != -1)
        {
            CFoundFilesData* ptr = Data[index];
            paths.push_back(sally::unicode::BuildPanelChildPathW(
                ptr->PathW.c_str(), ptr->NameW.c_str()));
        }
    } while (index != -1);
}

void CFoundFilesListView::CheckAndRemoveSelectedItems(BOOL forceRemove, int lastFocusedIndex, const CFoundFilesData* lastFocusedItem)
{
    int removedItems = 0;

    int totalCount = ListView_GetItemCount(HWindow);
    int i;
    for (i = totalCount - 1; i >= 0; i--)
    {
        if (ListView_GetItemState(HWindow, i, LVIS_SELECTED) & LVIS_SELECTED)
        {
            CFoundFilesData* ptr = Data[i];
            BOOL remove = forceRemove;
            if (!forceRemove)
            {
                std::wstring fullPath = ptr->GetFullNameW();
                IFileSystem* fs = gFileSystem != NULL ? gFileSystem : GetWin32FileSystem();
                remove = (fs->GetFileAttributes(fullPath.c_str()) == INVALID_FILE_ATTRIBUTES);
            }
            if (remove)
            {
                Delete(i);
                removedItems++;
            }
        }
    }
    if (removedItems > 0)
    {
        // inform the listview about the new item count
        totalCount = totalCount - removedItems;
        ListView_SetItemCount(HWindow, totalCount);
        if (totalCount > 0)
        {
            // clear selection of all items
            ListView_SetItemState(HWindow, -1, 0, LVIS_SELECTED);

            // try to locate the previously selected item and select it again if it still exists
            int selectIndex = -1;
            if (lastFocusedIndex != -1)
            {
                for (i = 0; i < totalCount; i++)
                {
                    CFoundFilesData* ptr = Data[i];
                    if (lastFocusedItem != NULL &&
                        !lastFocusedItem->NameW.empty() && CompareFindTextW(ptr->NameW, lastFocusedItem->NameW) == 0 &&
                        !lastFocusedItem->PathW.empty() && CompareFindTextW(ptr->PathW, lastFocusedItem->PathW) == 0)
                    {
                        selectIndex = i;
                        break;
                    }
                }
                if (selectIndex == -1)
                    selectIndex = min(lastFocusedIndex, totalCount - 1); // if we did not find it, keep the cursor in place but within item count
            }
            if (selectIndex == -1) // fallback -- first item
                selectIndex = 0;
            ListView_SetItemState(HWindow, selectIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(HWindow, selectIndex, FALSE);
        }
        else
            FindDialog->UpdateStatusBar = TRUE;
        FindDialog->UpdateListViewItems();
    }
}

BOOL CFoundFilesListView::IsGood()
{
    BOOL isGood;
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    isGood = Data.IsGood();
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
    return isGood;
}

void CFoundFilesListView::ResetState()
{
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    Data.ResetState();
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
}

void CFoundFilesListView::StoreItemsState()
{
    int count = GetCount();
    int i;
    for (i = 0; i < count; i++)
    {
        DWORD state = ListView_GetItemState(HWindow, i, LVIS_FOCUSED | LVIS_SELECTED);
        Data[i]->Selected = (state & LVIS_SELECTED) != 0 ? 1 : 0;
        Data[i]->Focused = (state & LVIS_FOCUSED) != 0 ? 1 : 0;
    }
}

void CFoundFilesListView::RestoreItemsState()
{
    int count = GetCount();
    int i;
    for (i = 0; i < count; i++)
    {
        DWORD state = 0;
        if (Data[i]->Selected)
            state |= LVIS_SELECTED;
        if (Data[i]->Focused)
            state |= LVIS_FOCUSED;
        ListView_SetItemState(HWindow, i, state, LVIS_FOCUSED | LVIS_SELECTED);
    }
}

void CFoundFilesListView::SortItems(int sortBy)
{
    if (sortBy == 5)
        return; // sorting by attributes is unsupported

    BOOL enabledNameSize = TRUE;
    BOOL enabledPathTime = TRUE;
    if (FindDialog->GrepData.FindDuplicates)
    {
        enabledPathTime = FALSE; // path and time are irrelevant for duplicates
        // sorting by name and size works for duplicates only
        // when searching for identical name and size
        enabledNameSize = (FindDialog->GrepData.FindDupFlags & FIND_DUPLICATES_NAME) &&
                          (FindDialog->GrepData.FindDupFlags & FIND_DUPLICATES_SIZE);
    }

    if (!enabledNameSize && (sortBy == 0 || sortBy == 2))
        return;
    if (!enabledPathTime && (sortBy == 1 || sortBy == 3 || sortBy == 4))
        return;

    HCURSOR hCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    HANDLES(EnterCriticalSection(&DataCriticalSection));

    //   EnumFileNamesChangeSourceUID(HWindow, &EnumFileNamesSourceUID);  // commented out, not sure why it is here: Petr

    // if some items are still in data but not in the listview, transfer them
    FindDialog->UpdateListViewItems();

    if (Data.Count > 0)
    {
        // save the selected and focused item state
        StoreItemsState();

        // sort the array by the requested criterion
        QuickSort(0, Data.Count - 1, sortBy);
        if (FindDialog->GrepData.FindDuplicates)
        {
            QuickSortDuplicates(0, Data.Count - 1, sortBy == 0);
            SetDifferentByGroup();
        }
        else
        {
            QuickSort(0, Data.Count - 1, sortBy);
        }

        // restore the item states
        RestoreItemsState();

        int focusIndex = ListView_GetNextItem(HWindow, -1, LVNI_FOCUSED);
        if (focusIndex != -1)
            ListView_EnsureVisible(HWindow, focusIndex, FALSE);
        ListView_RedrawItems(HWindow, 0, Data.Count - 1);
        UpdateWindow(HWindow);
    }

    HANDLES(LeaveCriticalSection(&DataCriticalSection));
    SetCursor(hCursor);
}

void CFoundFilesListView::SetDifferentByGroup()
{
    CFoundFilesData* lastData = NULL;
    int different = 0;
    if (Data.Count > 0)
    {
        lastData = Data.At(0);
        lastData->Different = different;
    }
    int i;
    for (i = 1; i < Data.Count; i++)
    {
        CFoundFilesData* data = Data.At(i);
        if (data->Group == lastData->Group)
        {
            data->Different = different;
        }
        else
        {
            different++;
            if (different > 1)
                different = 0;
            lastData = data;
            lastData->Different = different;
        }
    }
}

void CFoundFilesListView::ClearDuplicateState()
{
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    int i;
    for (i = 0; i < Data.Count; i++)
    {
        CFoundFilesData* data = Data[i];
        data->Group = 0;
        data->Different = 0;
    }
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
}

void CFoundFilesListView::QuickSort(int left, int right, int sortBy)
{

LABEL_QuickSort2:

    int i = left, j = right;
    CFoundFilesData* pivot = Data[(i + j) / 2];

    do
    {
        while (CompareFunc(Data[i], pivot, sortBy) < 0 && i < right)
            i++;
        while (CompareFunc(pivot, Data[j], sortBy) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CFoundFilesData* swap = Data[i];
            Data[i] = Data[j];
            Data[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced with a code that is much more stack-efficient  (max. log(N) recursion depth)
    //  if (left < j) QuickSort(left, j, sortBy);
    //  if (i < right) QuickSort(i, right, sortBy);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // both halves need sorting: recurse on the smaller one and process the other via 'goto'
            {
                QuickSort(left, j, sortBy);
                left = i;
                goto LABEL_QuickSort2;
            }
            else
            {
                QuickSort(i, right, sortBy);
                right = j;
                goto LABEL_QuickSort2;
            }
        }
        else
        {
            right = j;
            goto LABEL_QuickSort2;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_QuickSort2;
        }
    }
}

int CFoundFilesListView::CompareFunc(CFoundFilesData* f1, CFoundFilesData* f2, int sortBy)
{
    int res;
    int next = sortBy;
    do
    {
        if (f1->IsDir == f2->IsDir) // are the items from the same group (directories/files)?
        {
            switch (next)
            {
            case 0:
            {
                res = CompareFindTextW(f1->NameW, f2->NameW);
                break;
            }

            case 1:
            {
                res = CompareFindTextW(f1->PathW, f2->PathW);
                break;
            }

            case 2:
            {
                if (f1->Size < f2->Size)
                    res = -1;
                else
                {
                    if (f1->Size == f2->Size)
                        res = 0;
                    else
                        res = 1;
                }
                break;
            }

            default:
            {
                res = CompareFileTime(&f1->LastWrite, &f2->LastWrite);
                break;
            }
            }
        }
        else
            res = f1->IsDir ? -1 : 1;

        if (next == sortBy)
        {
            if (sortBy != 0)
                next = 0;
            else
                next = 1;
        }
        else if (next + 1 != sortBy)
            next++;
        else
            next += 2;
    } while (res == 0 && next <= 3);

    return res;
}

// quick sort routine for duplicate mode; it uses a special comparator
void CFoundFilesListView::QuickSortDuplicates(int left, int right, BOOL byName)
{

LABEL_QuickSortDuplicates:

    int i = left, j = right;
    CFoundFilesData* pivot = Data[(i + j) / 2];

    do
    {
        while (CompareDuplicatesFunc(Data[i], pivot, byName) < 0 && i < right)
            i++;
        while (CompareDuplicatesFunc(pivot, Data[j], byName) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CFoundFilesData* swap = Data[i];
            Data[i] = Data[j];
            Data[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced with a code that is much more stack-efficient (max. log(N) recursion depth)
    //  if (left < j) QuickSortDuplicates(left, j, byName);
    //  if (i < right) QuickSortDuplicates(i, right, byName);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // both halves need sorting: recurse on the smaller one and use 'goto' for the other
            {
                QuickSortDuplicates(left, j, byName);
                left = i;
                goto LABEL_QuickSortDuplicates;
            }
            else
            {
                QuickSortDuplicates(i, right, byName);
                right = j;
                goto LABEL_QuickSortDuplicates;
            }
        }
        else
        {
            right = j;
            goto LABEL_QuickSortDuplicates;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_QuickSortDuplicates;
        }
    }
}

// comparator for displayed duplicates; if 'byName', sorting is primarily by name, otherwise by size
int CFoundFilesListView::CompareDuplicatesFunc(CFoundFilesData* f1, CFoundFilesData* f2, BOOL byName)
{
    int res;
    if (byName)
    {
        // by name
        res = CompareFindTextW(f1->NameW, f2->NameW);
        if (res == 0)
        {
            // by size
            if (f1->Size < f2->Size)
                res = -1;
            else
            {
                if (f1->Size == f2->Size)
                {
                    // by group
                    if (f1->Group < f2->Group)
                        res = -1;
                    else
                    {
                        if (f1->Group == f2->Group)
                            res = 0;
                        else
                            res = 1;
                    }
                }
                else
                    res = 1;
            }
        }
    }
    else
    {
        // by size
        if (f1->Size < f2->Size)
            res = -1;
        else
        {
            if (f1->Size == f2->Size)
            {
                // by name
                res = CompareFindTextW(f1->NameW, f2->NameW);
                if (res == 0)
                {
                    // by group
                    if (f1->Group < f2->Group)
                        res = -1;
                    else
                    {
                        if (f1->Group == f2->Group)
                            res = 0;
                        else
                            res = 1;
                    }
                }
            }
            else
                res = 1;
        }
    }
    if (res == 0)
        res = CompareFindTextW(f1->PathW, f2->PathW);
    return res;
}

struct CUMDataFromFind
{
    HWND HWindow;
    int* Index;
    int Count;

    CUMDataFromFind(HWND hWindow)
    {
        Count = -1;
        Index = NULL;
        HWindow = hWindow;
    }
    ~CUMDataFromFind()
    {
        if (Index != NULL)
            delete[] (Index);
    }
};

// description -- see mainwnd.h
BOOL GetNextItemFromFind(int index, std::wstring& path, std::wstring& name, void* param)
{
    CALL_STACK_MESSAGE2("GetNextItemFromFind(%d, , ,)", index);
    CUMDataFromFind* data = (CUMDataFromFind*)param;

    CFoundFilesListView* listView = (CFoundFilesListView*)WindowsManager.GetWindowPtr(data->HWindow);
    if (listView == NULL)
    {
        TRACE_E("Unable to find object for ListView");
        return FALSE;
    }

    LV_ITEM item;
    item.mask = LVIF_PARAM;
    item.iSubItem = 0;
    if (data->Count == -1)
    {
        data->Count = ListView_GetSelectedCount(data->HWindow);
        if (data->Count == 0)
            return FALSE;
        data->Index = new int[data->Count];
        if (data->Index == NULL)
            return FALSE; // error
        int i = 0;
        int findItem = -1;
        while (i < data->Count)
        {
            findItem = ListView_GetNextItem(data->HWindow, findItem, LVNI_SELECTED);
            data->Index[i++] = findItem;
        }
    }

    if (index >= 0 && index < data->Count)
    {
        CFoundFilesData* file = listView->At(data->Index[index]);
        path = file->PathW;
        name = file->NameW;
        return TRUE;
    }
    if (data->Index != NULL)
    {
        delete[] (data->Index);
        data->Index = NULL;
    }
    return FALSE;
}

static void ApplyInitialFindLookInSeed(HWND hCombo, const sally::find::LookInSeed& seed,
                                      std::wstring& text)
{
    if (hCombo == NULL || !sally::find::HasInitialLookInSeed(seed))
        return;

    std::wstring initialLookInW = seed.wide;
    EscapeFindLookInPathSeparatorsW(initialLookInW);
    if (initialLookInW.empty())
        return;

    SendMessageW(hCombo, WM_SETTEXT, 0, (LPARAM)initialLookInW.c_str());
    text = initialLookInW;
}

LRESULT
CFoundFilesListView::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CFoundFilesListView::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_GETDLGCODE:
    {
        if (lParam != NULL)
        {
            // if it is the Enter key, we want to process it, otherwise the Enter would not be delivered
            MSG* msg = (LPMSG)lParam;
            if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN &&
                ListView_GetItemCount(HWindow) > 0)
                return DLGC_WANTMESSAGE;
        }
        return DLGC_WANTCHARS | DLGC_WANTARROWS;
    }

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        /* it seems this code is no longer needed; handled in the dialog wndproc
      if (wParam == VK_RETURN)
      {
        if (altPressed)
        {
          FindDialog->OnProperties();
          FindDialog->SkipCharacter = TRUE;
        }
        else
          FindDialog->OnOpen();
        return TRUE;
      }
*/
        if ((wParam == VK_F10 && shiftPressed || wParam == VK_APPS))
        {
            POINT p;
            GetListViewContextMenuPos(HWindow, &p);
            FindDialog->OnContextMenu(p.x, p.y);
            return TRUE;
        }
        break;
    }

    case WM_MOUSEACTIVATE:
    {
        // if Find is inactive and the user tries to drag & drop one of the items, the dialog must not pop up to the foreground
        return MA_NOACTIVATE;
    }

    case WM_SETFOCUS:
    {
        SendMessage(GetParent(HWindow), WM_USER_BUTTONS, 0, 0);
        break;
    }

    case WM_KILLFOCUS:
    {
        HWND next = (HWND)wParam;
        BOOL nextIsButton;
        if (next != NULL)
        {
            wchar_t className[30];
            WORD wl = LOWORD(GetWindowLongPtr(next, GWL_STYLE)); // only BS_ styles
            nextIsButton = (GetClassNameW(next, className, 30) != 0 &&
                            StrICmpW(className, L"BUTTON") == 0 &&
                            (wl == BS_PUSHBUTTON || wl == BS_DEFPUSHBUTTON));
        }
        else
            nextIsButton = FALSE;
        SendMessage(GetParent(HWindow), WM_USER_BUTTONS, nextIsButton ? wParam : 0, 0);
        break;
    }

    case WM_USER_ENUMFILENAMES: // searching for the next/previous name for the viewer
    {
        HANDLES(EnterCriticalSection(&FileNamesEnumDataSect));
        if ((int)wParam /* reqUID */ == FileNamesEnumData.RequestUID && // no further request was issued (this one would be pointless)
            EnumFileNamesSourceUID == FileNamesEnumData.SrcUID &&       // the source hasn't changed
            !FileNamesEnumData.TimedOut)                                // someone is still waiting for the result
        {
            HANDLES(EnterCriticalSection(&DataCriticalSection));

            BOOL selExists = FALSE;
            if (FileNamesEnumData.PreferSelected) // if needed, check whether there is a selection
            {
                int i = -1;
                int selCount = 0; // ignore the state where the only marked item is the focused one (this cannot logically be considered as selected items)
                while (1)
                {
                    i = ListView_GetNextItem(HWindow, i, LVNI_SELECTED);
                    if (i == -1)
                        break;
                    else
                    {
                        selCount++;
                        if (!Data[i]->IsDir)
                            selExists = TRUE;
                        if (selCount > 1 && selExists)
                            break;
                    }
                }
                if (selExists && selCount <= 1)
                    selExists = FALSE;
            }

            int index = FileNamesEnumData.LastFileIndex;
            int count = Data.Count;
            BOOL indexNotFound = TRUE;
            if (index == -1) // searching from the first or last item
            {
                if (FileNamesEnumData.RequestType == fnertFindPrevious)
                    index = count; // looking for the previous item, start at the end
                                   // else  // looking for the next item, start at the beginning
            }
            else
            {
                if (!FileNamesEnumData.LastFileName.empty()) // the full name at 'index' is known; check for shifts and search for a new index if needed
                {
                    BOOL ok = FALSE;
                    CFoundFilesData* f = (index >= 0 && index < count) ? Data[index] : NULL;
                    if (f != NULL && !f->PathW.empty() && !f->NameW.empty())
                    {
                        const std::wstring fileName = f->GetFullNameW();
                        if (StrICmpW(fileName.c_str(), FileNamesEnumData.LastFileName.c_str()) == 0)
                        {
                            ok = TRUE;
                            indexNotFound = FALSE;
                        }
                    }
                    if (!ok)
                    { // the name at index 'index' isn't FileNamesEnumData.LastFileName, try to find a new index for that name
                        int i;
                        for (i = 0; i < count; i++)
                        {
                            f = Data[i];
                            if (!f->PathW.empty() && !f->NameW.empty())
                            {
                                const std::wstring fileName = f->GetFullNameW();
                                if (StrICmpW(fileName.c_str(), FileNamesEnumData.LastFileName.c_str()) == 0)
                                    break;
                            }
                        }
                        if (i != count) // new index found
                        {
                            index = i;
                            indexNotFound = FALSE;
                        }
                    }
                }
                if (index >= count)
                {
                    if (FileNamesEnumData.RequestType == fnertFindNext)
                        index = count - 1;
                    else
                        index = count;
                }
                if (index < 0)
                    index = 0;
            }

            int wantedViewerType = 0;
            BOOL onlyAssociatedExtensions = FALSE;
            if (FileNamesEnumData.OnlyAssociatedExtensions) // does the viewer request filtering by associated extensions?
            {
                if (FileNamesEnumData.Plugin != NULL) // viewer from a plugin
                {
                    int pluginIndex = Plugins.GetIndex(FileNamesEnumData.Plugin);
                    if (pluginIndex != -1) // "always true"
                    {
                        wantedViewerType = -1 - pluginIndex;
                        onlyAssociatedExtensions = TRUE;
                    }
                }
                else // internal viewer
                {
                    wantedViewerType = VIEWER_INTERNAL;
                    onlyAssociatedExtensions = TRUE;
                }
            }

            BOOL preferSelected = selExists && FileNamesEnumData.PreferSelected;
            switch (FileNamesEnumData.RequestType)
            {
            case fnertFindNext: // next
            {
                CDynString strViewerMasks;
                if (MainWindow->GetViewersAssoc(wantedViewerType, &strViewerMasks))
                {
                    CMaskGroup masks;
                    int errorPos;
                    if (masks.PrepareMasks(errorPos, strViewerMasks.GetString()))
                    {
                        while (index + 1 < count)
                        {
                            index++;
                            if (preferSelected)
                            {
                                int i = ListView_GetNextItem(HWindow, index - 1, LVNI_SELECTED);
                                if (i != -1)
                                {
                                    index = i;
                                    if (!Data[index]->IsDir) // we only search for files
                                    {
                                        if (!onlyAssociatedExtensions || masks.AgreeMasks(Data[index]->NameW.c_str(), NULL))
                                        {
                                            FileNamesEnumData.Found = TRUE;
                                            break;
                                        }
                                    }
                                }
                                else
                                    index = count - 1;
                            }
                            else
                            {
                                if (!Data[index]->IsDir)
                                {
                                    if (!onlyAssociatedExtensions || masks.AgreeMasks(Data[index]->NameW.c_str(), NULL))
                                    {
                                        FileNamesEnumData.Found = TRUE;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    else
                        TRACE_E("Unexpected situation in Find::WM_USER_ENUMFILENAMES: grouped viewer's masks can't be prepared for use!");
                }
                break;
            }

            case fnertFindPrevious: // previous
            {
                CDynString strViewerMasks;
                if (MainWindow->GetViewersAssoc(wantedViewerType, &strViewerMasks))
                {
                    CMaskGroup masks;
                    int errorPos;
                    if (masks.PrepareMasks(errorPos, strViewerMasks.GetString()))
                    {
                        while (index - 1 >= 0)
                        {
                            index--;
                            if (!Data[index]->IsDir &&
                                (!preferSelected ||
                                 (ListView_GetItemState(HWindow, index, LVIS_SELECTED) & LVIS_SELECTED)))
                            {
                                if (!onlyAssociatedExtensions || masks.AgreeMasks(Data[index]->NameW.c_str(), NULL))
                                {
                                    FileNamesEnumData.Found = TRUE;
                                    break;
                                }
                            }
                        }
                    }
                    else
                        TRACE_E("Unexpected situation in Find::WM_USER_ENUMFILENAMES: grouped viewer's masks can't be prepared for use!");
                }
                break;
            }

            case fnertIsSelected: // check selection state
            {
                if (!indexNotFound && index >= 0 && index < Data.Count)
                {
                    FileNamesEnumData.IsFileSelected = (ListView_GetItemState(HWindow, index, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                    FileNamesEnumData.Found = TRUE;
                }
                break;
            }

            case fnertSetSelection: // set selection
            {
                if (!indexNotFound && index >= 0 && index < Data.Count)
                {
                    ListView_SetItemState(HWindow, index, FileNamesEnumData.Select ? LVIS_SELECTED : 0, LVIS_SELECTED);
                    FileNamesEnumData.Found = TRUE;
                }
                break;
            }
            }

            if (FileNamesEnumData.Found)
            {
                CFoundFilesData* f = Data[index];
                if (!f->PathW.empty() && !f->NameW.empty())
                {
                    // wide result first, narrow FileName stays the mirror.
                    FileNamesEnumData.FileNameW = f->PathW;
                    SalPathAppendW(FileNamesEnumData.FileNameW, f->NameW.c_str());
                    FileNamesEnumData.LastFileIndex = index;
                }
                else // should never happen
                {
                    TRACE_E("Unexpected situation in CFoundFilesListView::WindowProc(): handling of WM_USER_ENUMFILENAMES");
                    FileNamesEnumData.Found = FALSE;
                    FileNamesEnumData.NoMoreFiles = TRUE;
                }
            }
            else
                FileNamesEnumData.NoMoreFiles = TRUE;

            HANDLES(LeaveCriticalSection(&DataCriticalSection));
            SetEvent(FileNamesEnumDone);
        }
        HANDLES(LeaveCriticalSection(&FileNamesEnumDataSect));
        return 0;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

BOOL CFoundFilesListView::InitColumns()
{
    CALL_STACK_MESSAGE1("CFoundFilesListView::InitColumns()");
    LVCOLUMNW lvc;
    int header[] = {IDS_FOUNDFILESCOLUMN1, IDS_FOUNDFILESCOLUMN2,
                    IDS_FOUNDFILESCOLUMN3, IDS_FOUNDFILESCOLUMN4,
                    IDS_FOUNDFILESCOLUMN5, IDS_FOUNDFILESCOLUMN6,
                    -1};

    lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;
    int i;
    for (i = 0; header[i] != -1; i++) // create columns
    {
        if (i == 2)
            lvc.fmt = LVCFMT_RIGHT;
        lvc.pszText = LoadStrW(header[i]);
        lvc.iSubItem = i;
        if ((int)SendMessageW(HWindow, LVM_INSERTCOLUMNW, i, (LPARAM)&lvc) == -1)
            return FALSE;
    }

    auto getStringWidth = [this](const wchar_t* text) -> int
    {
        return (int)SendMessageW(HWindow, LVM_GETSTRINGWIDTHW, 0, (LPARAM)text);
    };

    RECT r;
    GetClientRect(HWindow, &r);
    DWORD cx = r.right - r.left - 1;
    ListView_SetColumnWidth(HWindow, 5, getStringWidth(L"ARH") + 20);

    wchar_t format1[200];
    wchar_t format2[200];
    SYSTEMTIME st;
    ZeroMemory(&st, sizeof(st));
    st.wYear = 2000; // the longest possible value
    st.wMonth = 12;  // the longest possible value
    st.wDay = 30;    // the longest possible value
    st.wHour = 10;   // morning (not sure whether AM or PM will be shorter, so try both)
    st.wMinute = 59; // the longest possible value
    st.wSecond = 59; // the longest possible value
    if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, format1, 200) == 0)
        _snwprintf_s(format1, _countof(format1), _TRUNCATE, L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    st.wHour = 20; // afternoon
    if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, format2, 200) == 0)
        _snwprintf_s(format2, _countof(format2), _TRUNCATE, L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);

    int maxWidth = getStringWidth(format1);
    int w = getStringWidth(format2);
    if (w > maxWidth)
        maxWidth = w;
    ListView_SetColumnWidth(HWindow, 4, maxWidth + 20);

    maxWidth = 0;
    if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, format1, 200) == 0)
        _snwprintf_s(format1, _countof(format1), _TRUNCATE, L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);
    else
    {
        // verify that the short date format does not contain alphabetic characters
        const wchar_t* p = format1;
        while (*p != 0 && !IsCharAlphaW(*p))
            p++;
        if (IsCharAlphaW(*p))
        {
            // contains alphabetic characters -- we must find the longest month and day text
            int maxMonth = 0;
            int sats[] = {1, 5, 4, 1, 6, 3, 1, 5, 2, 7, 4, 2};
            int mo;
            for (mo = 0; mo < 12; mo++) // iterate over all months starting from January; the weekday stays the same so its width doesn't influence the result, wDay is single digit for the same reason
            {
                st.wDay = sats[mo];
                st.wMonth = 1 + mo;
                if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, format1, 200) != 0)
                {
                    w = getStringWidth(format1);
                    if (w > maxWidth)
                    {
                        maxWidth = w;
                        maxMonth = st.wMonth;
                    }
                }
            }
            if (maxWidth > 0)
            {
                st.wMonth = maxMonth;
                for (st.wDay = 21; st.wDay < 28; st.wDay++) // all possible weekdays (doesn't have to start on Monday)
                {
                    if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, format1, 200) != 0)
                    {
                        w = getStringWidth(format1);
                        if (w > maxWidth)
                        {
                            maxWidth = w;
                        }
                    }
                }
            }
        }
    }

    ListView_SetColumnWidth(HWindow, 3, (maxWidth > 0 ? maxWidth : getStringWidth(format1)) + 20);
    ListView_SetColumnWidth(HWindow, 2, getStringWidth(L"000 000 000 000") + 20); // up to 1TB fits here
    int width;
    if (Configuration.FindColNameWidth != -1)
        width = Configuration.FindColNameWidth;
    else
        width = 20 + getStringWidth(L"XXXXXXXX.XXX") + 20;
    ListView_SetColumnWidth(HWindow, 0, width);
    cx -= ListView_GetColumnWidth(HWindow, 0) + ListView_GetColumnWidth(HWindow, 2) +
          ListView_GetColumnWidth(HWindow, 3) + ListView_GetColumnWidth(HWindow, 4) +
          ListView_GetColumnWidth(HWindow, 5) + GetSystemMetrics(SM_CXHSCROLL) - 1;
    ListView_SetColumnWidth(HWindow, 1, cx);
    ListView_SetImageList(HWindow, HFindSymbolsImageList, LVSIL_SMALL);

    return TRUE;
}

//****************************************************************************
//
// CFindDialog
//

CFindDialog::CFindDialog(HWND hCenterAgainst, const wchar_t* initPath)
    : CCommonDialog(HLanguage, IDD_FIND, NULL, ooStandard, hCenterAgainst),
      SearchForData(50, 10)
{

    // data needed to lay out the dialog
    FirstWMSize = TRUE;
    VMargin = 0;
    HMargin = 0;
    ButtonW = 0;
    ButtonH = 0;
    RegExpButtonW = 0;
    RegExpButtonY = 0;
    MenuBarHeight = 0;
    StatusHeight = 0;
    ResultsY = 0;
    AdvancedY = 0;
    AdvancedTextY = 0;
    AdvancedTextX = 0;
    FindTextY = 0;
    FindTextH = 0;
    CombosX = 0;
    CombosH = 0;
    BrowseY = 0;
    Line2X = 0;
    FindNowY = 0;
    Expanded = TRUE; // persistent
    MinDlgW = 0;
    MinDlgH = 0;

    // additional data
    DlgFailed = FALSE;
    MainMenu = NULL;
    TBHeader = NULL;
    MenuBar = NULL;
    HStatusBar = NULL;
    HProgressBar = NULL;
    TwoParts = FALSE;
    FoundFilesListView = NULL;
    SearchInProgress = FALSE;
    StateOfFindCloseQuery = sofcqNotUsed;
    CanClose = TRUE;
    GrepThread = NULL;
    wchar_t buf[100];
    // was sprintf() into a wchar_t buffer with a narrow format and LoadStr.
    _snwprintf_s(buf, _TRUNCATE, L"%ls ", LoadStrW(IDS_FF_SEARCHING));
    SearchingText.SetBase(buf);
    UpdateStatusBar = FALSE;
    ContextMenu = NULL;
    ZeroOnDestroy = NULL;
    OleInitialized = FALSE;
    ProcessingEscape = FALSE;
    OKButton = NULL;

    FileNameFormat = Configuration.FileNameFormat;
    SkipCharacter = FALSE;

    CacheBitmap = NULL;
    FlashIconsOnActivation = FALSE;

    FindNowText.clear();

    // if any option has AutoLoad set, load it now
    int i;
    for (i = 0; i < FindOptions.GetCount(); i++)
        if (FindOptions.At(i)->AutoLoad)
        {
            Data = *FindOptions.At(i);
            Data.AutoLoad = FALSE;
            break;
        }
    // The Type filter is a dialog-level preference in Sally. AutoLoad profiles
    // may carry an older value, but new dialogs should reopen with the last
    // live choice the user made.
    Data.FileTypeMode = sally::find::NormalizeFindFileTypeMode(Configuration.FindFileTypeMode);
    if (Configuration.FindFileTypeMode != Data.FileTypeMode)
    {
        Configuration.FindFileTypeMode = Data.FileTypeMode;
    }

    // data for controls
    //
    // The seed is reapplied by the deferred handler after initial history transfer.
    InitialLookInSeed = sally::find::BuildLookInSeed(initPath);
    LookInUnicodeFont = NULL;  // made only if the dialog font cannot render
    if (Data.NamedText.empty())
        Data.NamedText = L"*.*";

    if (sally::find::HasInitialLookInSeed(InitialLookInSeed))
    {
        std::wstring initialLookIn = InitialLookInSeed.wide;
        EscapeFindLookInPathSeparatorsW(initialLookIn);
        Data.LookInText = initialLookIn;
    }
}

CFindDialog::~CFindDialog()
{
    if (CacheBitmap != NULL)
        delete CacheBitmap;
}

void CFindDialog::GetLayoutParams()
{
    RECT wr;
    GetWindowRect(HWindow, &wr);
    MinDlgW = wr.right - wr.left;
    MinDlgH = wr.bottom - wr.top;

    RECT cr;
    GetClientRect(HWindow, &cr);

    RECT br;
    GetWindowRect(GetDlgItem(HWindow, IDC_FIND_RESULTS), &br);
    int windowMargin = ((wr.right - wr.left) - (cr.right)) / 2;
    HMargin = br.left - wr.left - windowMargin;
    VMargin = HMargin;

    int captionH = wr.bottom - wr.top - cr.bottom - windowMargin;
    ResultsY = br.top - wr.top - captionH;

    GetWindowRect(GetDlgItem(HWindow, IDOK), &br); //IDC_FIND_FINDNOW
    ButtonW = br.right - br.left;
    ButtonH = br.bottom - br.top;
    FindNowY = br.top - wr.top - captionH;

    GetWindowRect(GetDlgItem(HWindow, IDC_FIND_REGEXP_BROWSE), &br);
    RegExpButtonW = br.right - br.left;
    RegExpButtonY = br.top - wr.top - captionH;

    MenuBarHeight = MenuBar->GetNeededHeight();

    RECT r;
    GetWindowRect(HStatusBar, &r);
    StatusHeight = r.bottom - r.top;

    GetWindowRect(GetDlgItem(HWindow, IDC_FIND_NAMED), &r);
    CombosX = r.left - wr.left - windowMargin;

    GetWindowRect(GetDlgItem(HWindow, IDC_FIND_LOOKIN_BROWSE), &r);
    BrowseY = r.top - wr.top - captionH;

    GetWindowRect(GetDlgItem(HWindow, IDC_FIND_LINE2), &r);
    Line2X = r.left - wr.left - windowMargin;

    GetWindowRect(GetDlgItem(HWindow, IDC_FIND_SPACER), &r);
    SpacerH = r.bottom - r.top;

    GetWindowRect(GetDlgItem(HWindow, IDC_FIND_ADVANCED), &r);
    AdvancedY = r.top - wr.top - captionH;

    GetWindowRect(GetDlgItem(HWindow, IDC_FIND_ADVANCED_TEXT), &r);
    AdvancedTextY = r.top - wr.top - captionH;
    AdvancedTextX = r.left - wr.left - windowMargin;

    //  GetWindowRect(GetDlgItem(HWindow, IDC_FIND_FOUND_FILES), &r);
    FindTextH = TBHeader->GetNeededHeight();
    FindTextY = ResultsY - FindTextH;
}

void CFindDialog::SetTwoStatusParts(BOOL two, BOOL force)
{
    int margin = HMargin - 4;
    int parts[3] = {margin, -1, -1};
    RECT r;
    GetClientRect(HStatusBar, &r);

    int gripWidth = HMargin;
    if (!IsZoomed(HWindow))
        gripWidth = GetSystemMetrics(SM_CXVSCROLL);

    int progressWidth = 0;
    int progressHeight = 0;
    if (two)
    {
        progressWidth = 104; // 100 plus the frame
        if (HProgressBar == NULL)
        {
            HProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
                                          WS_CHILD | PBS_SMOOTH,
                                          0, 0,
                                          progressWidth, r.bottom - 2,
                                          HStatusBar, (HMENU)0, HInstance, NULL);
            SendMessage(HProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        }
    }
    else
    {
        if (HProgressBar != NULL)
        {
            DestroyWindow(HProgressBar);
            HProgressBar = NULL;
        }
    }

    parts[1] = r.right - progressWidth - gripWidth;

    if (HProgressBar != NULL)
    {
        parts[1] -= 10; // increase spacing from the progress bar
        SetWindowPos(HProgressBar, NULL,
                     r.right - progressWidth - gripWidth, 2, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
    }

    if (TwoParts != two || force)
    {
        TwoParts = two;
        SendMessage(HStatusBar, WM_SETREDRAW, FALSE, 0); // when redraw is enabled, the status bar ends up with a stray frame
        SendMessage(HStatusBar, SB_SETPARTS, 3, (LPARAM)parts);
        SendMessageW(HStatusBar, SB_SETTEXTW, 0 | SBT_NOBORDERS, (LPARAM)L"");
        SendMessageW(HStatusBar, SB_SETTEXTW, 2 | SBT_NOBORDERS, (LPARAM)L"");
        SendMessage(HStatusBar, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(HStatusBar, NULL, TRUE);
    }
    else
        SendMessage(HStatusBar, SB_SETPARTS, 3, (LPARAM)parts);
}

void CFindDialog::LayoutControls()
{
    RECT clientRect;

    if (CombosH == 0)
    {
        RECT r;
        GetWindowRect(GetDlgItem(HWindow, IDC_FIND_NAMED), &r);
        if (r.bottom - r.top != 0)
            CombosH = r.bottom - r.top;
    }

    GetClientRect(HWindow, &clientRect);
    clientRect.bottom -= StatusHeight;

    HDWP hdwp = HANDLES(BeginDeferWindowPos(14));
    if (hdwp != NULL)
    {
        // spacing between buttons
        int buttonMargin = ButtonH / 3;

        // position the MenuBar
        hdwp = HANDLES(DeferWindowPos(hdwp, MenuBar->HWindow, NULL,
                                      0, -1, clientRect.right, MenuBarHeight,
                                      SWP_NOZORDER));

        // position the Status Bar
        hdwp = HANDLES(DeferWindowPos(hdwp, HStatusBar, NULL,
                                      0, clientRect.bottom, clientRect.right, StatusHeight,
                                      SWP_NOZORDER));

        // position the Advanced button
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDC_FIND_ADVANCED), NULL,
                                      HMargin, AdvancedY, 0, 0, SWP_NOSIZE | SWP_NOZORDER));

        // place and stretch the Advanced edit line
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDC_FIND_ADVANCED_TEXT), NULL,
                                      AdvancedTextX, AdvancedTextY, clientRect.right - AdvancedTextX - HMargin, CombosH,
                                      SWP_NOZORDER));

        // position the "Found files" label
        hdwp = HANDLES(DeferWindowPos(hdwp, TBHeader->HWindow /*GetDlgItem(HWindow, IDC_FIND_FOUND_FILES)*/, NULL,
                                      HMargin, FindTextY, clientRect.right - 2 * HMargin, FindTextH, SWP_NOZORDER));

        // place and stretch the list view
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDC_FIND_RESULTS), NULL,
                                      HMargin, ResultsY, clientRect.right - 2 * HMargin,
                                      clientRect.bottom - ResultsY /*- VMargin*/, SWP_NOZORDER));

        // place and stretch the separator line under the menu
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDC_FIND_LINE1), NULL,
                                      0, MenuBarHeight - 1, clientRect.right, 2, SWP_NOZORDER));

        // stretch the "Named" combo box
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDC_FIND_NAMED), NULL,
                                      0, 0, clientRect.right - CombosX - HMargin - ButtonW - buttonMargin, CombosH,
                                      SWP_NOMOVE | SWP_NOZORDER));

        // position the "Find Now" button
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDOK), NULL, //IDC_FIND_FINDNOW
                                      clientRect.right - HMargin - ButtonW, FindNowY, 0, 0,
                                      SWP_NOSIZE | SWP_NOZORDER));

        // stretch the "Look in" combo box
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDC_FIND_LOOKIN), NULL,
                                      0, 0, clientRect.right - CombosX - HMargin - ButtonW - buttonMargin, CombosH,
                                      SWP_NOMOVE | SWP_NOZORDER));

        // position the Browse button
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDC_FIND_LOOKIN_BROWSE), NULL,
                                      clientRect.right - HMargin - ButtonW, BrowseY, 0, 0,
                                      SWP_NOSIZE | SWP_NOZORDER));

        // stretch the "Containing" combo box
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDC_FIND_CONTAINING), NULL,
                                      0, 0, clientRect.right - CombosX - HMargin - RegExpButtonW - buttonMargin, CombosH,
                                      SWP_NOMOVE | SWP_NOZORDER));

        // position the Regular Expression Browse button
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDC_FIND_REGEXP_BROWSE), NULL,
                                      clientRect.right - HMargin - RegExpButtonW, RegExpButtonY, 0, 0,
                                      SWP_NOSIZE | SWP_NOZORDER));

        // stretch the separator line next to "Search file content"
        hdwp = HANDLES(DeferWindowPos(hdwp, GetDlgItem(HWindow, IDC_FIND_LINE2), NULL,
                                      0, 0, clientRect.right - Line2X - HMargin, 2,
                                      SWP_NOMOVE | SWP_NOZORDER));

        HANDLES(EndDeferWindowPos(hdwp));
    }
    SetTwoStatusParts(TwoParts);
    ApplyFindComboSkins(HWindow);
    ApplyFindStatusBarColors(HStatusBar);
}

void CFindDialog::SetContentVisible(BOOL visible)
{
    if (Expanded != visible)
    {
        if (visible)
        {
            ResultsY += SpacerH;
            AdvancedY += SpacerH;
            AdvancedTextY += SpacerH;
            FindTextY += SpacerH;

            MinDlgH += SpacerH;
            LayoutControls();
            if (!IsIconic(HWindow) && !IsZoomed(HWindow))
            {
                RECT wr;
                GetWindowRect(HWindow, &wr);
                if (wr.bottom - wr.top < MinDlgH)
                {
                    SetWindowPos(HWindow, NULL, 0, 0, wr.right - wr.left, MinDlgH,
                                 SWP_NOMOVE | SWP_NOZORDER);
                }
            }
        }

        // when items appear, we must enable them
        Expanded = visible;
        if (visible)
        {
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_CONTAINING_TEXT), TRUE);
            EnableControls();
        }

        ShowWindow(GetDlgItem(HWindow, IDC_FIND_CONTAINING_TEXT), visible);
        ShowWindow(GetDlgItem(HWindow, IDC_FIND_CONTAINING), visible);
        ShowWindow(GetDlgItem(HWindow, IDC_FIND_REGEXP_BROWSE), visible);
        ShowWindow(GetDlgItem(HWindow, IDC_FIND_HEX), visible);
        ShowWindow(GetDlgItem(HWindow, IDC_FIND_CASE), visible);
        ShowWindow(GetDlgItem(HWindow, IDC_FIND_WHOLE), visible);
        ShowWindow(GetDlgItem(HWindow, IDC_FIND_REGULAR), visible);

        if (!visible)
        {
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_CONTAINING_TEXT), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_CONTAINING), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_REGEXP_BROWSE), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_HEX), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_CASE), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_WHOLE), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_REGULAR), FALSE);
        }

        if (!visible)
        {
            ResultsY -= SpacerH;
            AdvancedY -= SpacerH;
            AdvancedTextY -= SpacerH;
            FindTextY -= SpacerH;
            MinDlgH -= SpacerH;
            LayoutControls();
        }
    }
}

void CFindDialog::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CFindDialog::Validate()");

    HWND hNamesWnd;
    HWND hLookInWnd;

    if (ti.GetControl(hNamesWnd, IDC_FIND_NAMED) &&
        ti.GetControl(hLookInWnd, IDC_FIND_LOOKIN))
    {
        // back up the data
        const std::wstring bufNamed = Data.NamedText;
        const std::wstring bufLookIn = Data.LookInText;

        Data.NamedText = GetWindowTextStringW(hNamesWnd);

        CMaskGroup mask;
        int errorPos;
        if (!mask.PrepareMasks(errorPos, Data.NamedText.c_str()))
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
            SetFocus(hNamesWnd); // ensure the CB_SETEDITSEL message works correctly
            SendMessage(hNamesWnd, CB_SETEDITSEL, 0, MAKELPARAM(errorPos, errorPos + 1));
            ti.ErrorOn(IDC_FIND_NAMED);
        }

        if (ti.IsGood())
        {
            Data.LookInText = GetWindowTextWide(hLookInWnd);

            BuildSerchForData();
            if (SearchForData.Count == 0)
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_FF_EMPTYSTRING));
                ti.ErrorOn(IDC_FIND_LOOKIN);
            }
        }

        // restore data from the backup
        Data.LookInText = bufLookIn;
        Data.NamedText = bufNamed;
    }
}

void CFindDialog::Transfer(CTransferInfo& ti)
{
    HistoryComboBox(HWindow, ti, IDC_FIND_NAMED, Data.NamedText,
                    FALSE, FIND_NAMED_HISTORY_SIZE, FindNamedHistory);
    HistoryComboBox(HWindow, ti, IDC_FIND_LOOKIN, Data.LookInText,
                    FALSE, FIND_LOOKIN_HISTORY_SIZE, FindLookInHistory);
    if (ti.Type == ttDataToWindow)
    {
        ActivateWideLookInCombo(HWindow, Data.LookInText, LookInUnicodeFont);
    }

    ti.CheckBox(IDC_FIND_INCLUDE_SUBDIR, Data.SubDirectories);
    HWND hFileType = GetDlgItem(HWindow, IDC_FIND_FILETYPE);
    if (ti.Type == ttDataToWindow)
    {
        SendMessage(hFileType, CB_SETCURSEL, Data.FileTypeMode, 0);
    }
    else
    {
        int mode = (int)SendMessage(hFileType, CB_GETCURSEL, 0, 0);
        Data.FileTypeMode = sally::find::NormalizeFindFileTypeMode(mode);
        Configuration.FindFileTypeMode = Data.FileTypeMode;
    }
    HistoryComboBox(HWindow, ti, IDC_FIND_CONTAINING, Data.GrepText,
                    !Data.RegularExpresions && Data.HexMode, FIND_GREP_HISTORY_SIZE,
                    FindGrepHistory);
    ti.CheckBox(IDC_FIND_HEX, Data.HexMode);
    ti.CheckBox(IDC_FIND_CASE, Data.CaseSensitive);
    ti.CheckBox(IDC_FIND_WHOLE, Data.WholeWords);
    ti.CheckBox(IDC_FIND_REGULAR, Data.RegularExpresions);
}

void CFindDialog::UpdateAdvancedText()
{
    BOOL dirty;
    const std::wstring description = Data.Criteria.GetAdvancedDescription(dirty);
    SetDlgItemTextW(HWindow, IDC_FIND_ADVANCED_TEXT, description.c_str());
    EnableWindow(GetDlgItem(HWindow, IDC_FIND_ADVANCED_TEXT), dirty);
}

void CFindDialog::LoadControls(int index)
{
    CALL_STACK_MESSAGE2("CFindDialog::LoadControls(0x%X)", index);
    Data = *FindOptions.At(index);
    BOOL keepCurrentLookIn = Data.LookInText.empty();

    // if any edit line is empty, keep its previous value
    if (Data.NamedText.empty())
    {
        Data.NamedText = GetWindowTextStringW(GetDlgItem(HWindow, IDC_FIND_NAMED));
    }
    if (keepCurrentLookIn)
    {
        Data.LookInText = GetWindowTextWide(GetDlgItem(HWindow, IDC_FIND_LOOKIN));
    }
    if (Data.GrepText.empty())
        Data.GrepText = GetWindowTextStringW(GetDlgItem(HWindow, IDC_FIND_CONTAINING));

    TransferData(ttDataToWindow);

    // if Grep contains text and the dialog isn't expanded, expand it
    if (!Data.GrepText.empty() && !Expanded)
    {
        CheckDlgButton(HWindow, IDC_FIND_GREP, TRUE);
        SetContentVisible(TRUE);
    }

    UpdateAdvancedText();
    EnableControls();
}

void CFindDialog::BuildSerchForData()
{
    // Users often enter a bare fragment and expect substring matching.
    const std::wstring named = sally::find::NormalizeFindMasksForSearch(Data.NamedText);

    SearchForData.DestroyMembers();

    const std::wstring lookInTextW = Data.LookInText;

    std::vector<std::wstring> paths = sally::find::SplitLookInPathsW(lookInTextW);
    for (size_t i = 0; i < paths.size(); i++)
    {
        if (paths[i].empty())
            continue;

        // was WideToAnsi(paths[i]) with a literal "?" substituted when the path
        // had no CP_ACP form, handed in beside the wide path CSearchForData now keeps.
        CSearchForData* item = new CSearchForData(paths[i].c_str(), named.c_str(), Data.SubDirectories);
        if (item != NULL)
        {
            SearchForData.Add(item);
            if (!SearchForData.IsGood())
            {
                SearchForData.ResetState();
                delete item;
                return;
            }
        }
    }
}

void CFindDialog::StartSearch(WORD command)
{
    CALL_STACK_MESSAGE1("CFindDialog::StartSearch()");
    if (FoundFilesListView == NULL || GrepThread != NULL)
        return;

    // if we are searching for duplicates, ask for additional options
    CFindDuplicatesDialog findDupDlg(HWindow);
    if (command == CM_FIND_DUPLICATES)
    {
        if (findDupDlg.Execute() != IDOK)
            return;

        // better verify the output variables
        if (!findDupDlg.SameName && !findDupDlg.SameSize)
        {
            TRACE_E("Invalid output from CFindDuplicatesDialog dialog.");
            return;
        }
    }

    TBHeader->SetFoundCount(0);
    TBHeader->SetErrorsInfosCount(0, 0);

    EnumFileNamesChangeSourceUID(FoundFilesListView->HWindow, &(FoundFilesListView->EnumFileNamesSourceUID));

    ListView_SetItemCount(FoundFilesListView->HWindow, 0);
    UpdateWindow(FoundFilesListView->HWindow);

    // release any errors held from the previous search
    Log.Clean();

    GrepData.FindDuplicates = FALSE;
    GrepData.FindDupFlags = 0;

    GrepData.Refine = 0; // no refine

    switch (command)
    {
    case IDOK:
    case CM_FIND_NOW:
    {
        FoundFilesListView->DestroyMembers();
        break;
    }

    case CM_FIND_INTERSECT:
    {
        // if this is a refine operation, copy data into the DataForRefine array
        FoundFilesListView->TakeDataForRefine();
        GrepData.Refine = 1;
        break;
    }

    case CM_FIND_SUBTRACT:
    {
        // if this is a refine operation, copy data into the DataForRefine array
        FoundFilesListView->TakeDataForRefine();
        GrepData.Refine = 2;
        break;
    }

    case CM_FIND_APPEND:
    {
        break;
    }

    case CM_FIND_DUPLICATES:
    {
        GrepData.FindDuplicates = TRUE;
        GrepData.FindDupFlags = 0;
        if (findDupDlg.SameName)
            GrepData.FindDupFlags |= FIND_DUPLICATES_NAME;
        if (findDupDlg.SameSize)
            GrepData.FindDupFlags |= FIND_DUPLICATES_SIZE;
        if (findDupDlg.SameContent)
            GrepData.FindDupFlags |= FIND_DUPLICATES_SIZE | FIND_DUPLICATES_CONTENT;

        FoundFilesListView->DestroyMembers();
        break;
    }
    }
    UpdateListViewItems();

    if (Data.GrepText.empty())
        GrepData.Grep = FALSE;
    else
    {
        GrepData.EOL_CRLF = Configuration.EOL_CRLF;
        GrepData.EOL_CR = Configuration.EOL_CR;
        GrepData.EOL_LF = Configuration.EOL_LF;
        //    GrepData.EOL_NULL = Configuration.EOL_NULL;   // can't handle this with regexp :(
        GrepData.Regular = Data.RegularExpresions;
        GrepData.WholeWords = Data.WholeWords;
        if (Data.RegularExpresions)
        {
            const WORD regexpFlags = static_cast<WORD>(
                sfForward | (Data.CaseSensitive ? sfCaseSensitive : 0));
            GrepData.RegExp.Clear();
            GrepData.RegExpUtf8.Clear();
            // RegExpUtf8's pattern and subject are both UTF-8, so it must not fold case
            // through the ACP byte table - see CRegularExpression::FoldEncoding. Set
            // before Set(), which compiles the folded pattern.
            GrepData.RegExpUtf8.SetFoldEncoding(CRegularExpression::FoldEncoding::Utf8);

            const auto reportRegexError = [&](const std::wstring& error)
            {
                const std::wstring msg = FormatStrW(
                    LoadStrW(IDS_INVALIDREGEXP), Data.GrepText.c_str(), error.c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORFINDINGFILE), msg.c_str());
                if (GrepData.Refine != 0)
                    FoundFilesListView->DestroyDataForRefine();
            };

            std::string utf8Pattern;
            const Win32TextConversionResult utf8Result =
                sally::legacy_search::EncodePatternUtf8(Data.GrepText, utf8Pattern);
            if (!utf8Result)
            {
                reportRegexError(
                    utf8Result.Error == Win32TextConversionError::OutOfMemory
                        ? sally::legacy_search::DecodeEngineAcp(LOW_MEMORY)
                        : GetErrorTextOwned(utf8Result.Win32Error));
                return;
            }
            if (!GrepData.RegExpUtf8.Set(utf8Pattern.c_str(), regexpFlags))
            {
                reportRegexError(sally::legacy_search::DecodeEngineAcp(
                    GrepData.RegExpUtf8.GetLastErrorText()));
                return;
            }

            std::string acpPattern;
            const Win32TextConversionResult acpResult =
                sally::legacy_search::EncodePatternAcpExact(Data.GrepText, acpPattern);
            if (acpResult)
            {
                if (!GrepData.RegExp.Set(acpPattern.c_str(), regexpFlags))
                {
                    reportRegexError(sally::legacy_search::DecodeEngineAcp(
                        GrepData.RegExp.GetLastErrorText()));
                    return;
                }
            }
            else if (acpResult.Error != Win32TextConversionError::UnrepresentableCharacter)
            {
                reportRegexError(
                    acpResult.Error == Win32TextConversionError::OutOfMemory
                        ? sally::legacy_search::DecodeEngineAcp(LOW_MEMORY)
                        : GetErrorTextOwned(acpResult.Win32Error));
                return;
            }

            // UTF-8 is the authoritative regexp representation. The exact ACP twin is optional
            // and is used only for legacy byte content; an unrepresentable Unicode expression
            // correctly cannot match such a file, but remains searchable in UTF-8/UTF-16 files.
            GrepData.Grep = TRUE;
        }
        else
        {
            if (Data.HexMode)
            {
                // Hex mode is the byte-domain escape hatch. Literal hex stays byte-exact;
                // quoted Unicode text has the explicit, portable UTF-8 representation.
                std::vector<std::uint8_t> bytes;
                if (!Sally::Unicode::ParseHexPattern(Data.GrepText.c_str(), bytes))
                {
                    // Say so. DestroyMembers/TakeDataForRefine has already run above, so a
                    // bare return left the results list emptied and Find Now looking like a
                    // no-op. Pre-unicode's ConvertHexToString could not fail at all, so the
                    // silent refusal is new; the live DoHexValidation filter keeps typed
                    // input clean, but a pattern restored from history or from a saved Find
                    // Options entry reaches here unfiltered.
                    if (gPrompter != NULL)
                    {
                        const std::wstring msg =
                            FormatStrW(LoadStrW(IDS_FIND_HEX_INVALID), Data.GrepText.c_str());
                        gPrompter->ShowError(LoadStrW(IDS_ERRORFINDINGFILE), msg.c_str());
                    }
                    if (GrepData.Refine != 0)
                        FoundFilesListView->DestroyDataForRefine();
                    return;
                }
                GrepData.SearchData.Set(bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data()),
                                        static_cast<int>(bytes.size()),
                                        (WORD)(sfForward | (Data.CaseSensitive ? sfCaseSensitive : 0)));
            }
            else
                GrepData.SearchData.Clear();
            // Literal text is owned and searched as UTF-16. Hex mode alone stays on the byte
            // engine because its input denotes byte values rather than text.
            GrepData.GrepCaseSensitive = Data.CaseSensitive;
            GrepData.GrepText = Data.HexMode ? std::wstring() : Data.GrepText;
            GrepData.Grep = Data.HexMode ? GrepData.SearchData.IsGood() : TRUE;
        }
    }
    SetFocus(FoundFilesListView->HWindow);

    BuildSerchForData();
    GrepData.Data = &SearchForData;
    GrepData.StopSearch = FALSE;
    GrepData.HWindow = HWindow;

    // advanced search
    memmove(&GrepData.Criteria, &Data.Criteria, sizeof(Data.Criteria));
    GrepData.FileTypeMode = Data.FileTypeMode;

    GrepData.FoundFilesListView = FoundFilesListView;
    GrepData.FoundVisibleCount = 0;
    GrepData.FoundVisibleTick = GetTickCount();

    GrepData.SearchingText = &SearchingText;
    GrepData.SearchingText2 = &SearchingText2;

    DWORD threadId;
    GrepThread = HANDLES(CreateThread(NULL, 0, GrepThreadF, &GrepData, 0, &threadId));
    if (GrepThread == NULL)
    {
        TRACE_E("Unable to start GrepThread thread.");
        if (GrepData.Refine != 0)
            FoundFilesListView->DestroyDataForRefine();
        return;
    }

    if (OKButton != NULL) // hide the drop-down arrow
    {
        DWORD flags = OKButton->GetFlags();
        flags &= ~BTF_DROPDOWN;
        OKButton->SetFlags(flags, FALSE);
    }
    SetDlgItemTextW(HWindow, IDOK, LoadStrW(IDS_FF_STOP));

    SearchInProgress = TRUE;

    // start a timer to update the dirty text
    SetTimer(HWindow, IDT_REPAINT, 100, NULL);

    // force the first refresh of the status bar
    SearchingText.SetDirty(TRUE);
    PostMessage(HWindow, WM_TIMER, IDT_REPAINT, 0);

    const std::wstring caption = FormatStrW(NORMAL_FINDING_CAPTION, LoadStrW(IDS_FF_NAME),
                                            LoadStrW(IDS_FF_NAMED), SearchForData[0]->MasksGroup.GetMasksString());
    SetWindowTextW(HWindow, caption.c_str());

    EnableControls();
}

void CFindDialog::StopSearch()
{
    CALL_STACK_MESSAGE1("CFindDialog::StopSearch()");
    GrepData.StopSearch = TRUE;
    MSG msg;
    while (1)
    {
        BOOL oldCanClose = CanClose;
        CanClose = FALSE; // don't allow closing while we are inside this method

        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        { // message loop for messages from the grep thread
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        CanClose = oldCanClose;
        if (GrepThread == NULL)
            return; // DispatchMessage may call us again and we've already handled closing
        if (WaitForSingleObject(GrepThread, 100) != WAIT_TIMEOUT)
            break;
    }
    if (GrepThread != NULL)
        HANDLES(CloseHandle(GrepThread));
    GrepThread = NULL;

    SearchInProgress = FALSE;
    if (OKButton != NULL) // trigger the drop-down arrow
    {
        DWORD flags = OKButton->GetFlags();
        flags |= BTF_DROPDOWN;
        OKButton->SetFlags(flags, FALSE);
    }
    SetDlgItemTextW(HWindow, IDOK, FindNowText.c_str());

    // stop the timer used for updating the text
    KillTimer(HWindow, IDT_REPAINT);

    // if the second text appeared during search, it's time to hide it
    if (TwoParts)
    {
        SearchingText2.Set(L"");
        SetTwoStatusParts(FALSE);
    }

    SearchingText.Set(L"");
    UpdateStatusBar = FALSE;
    if (!GrepData.SearchStopped)
    {
        DWORD items = FoundFilesListView->GetCount();
        if (items == 0)
        {
            int msgID = GrepData.FindDuplicates ? IDS_FIND_NO_DUPS_FOUND : IDS_FIND_NO_FILES_FOUND;
            SendMessageW(HStatusBar, SB_SETTEXTW, 1 | SBT_NOBORDERS, (LPARAM)LoadStrW(msgID));
        }
        else
            UpdateStatusBar = TRUE;

        if (Log.GetErrorCount() > 0 && Configuration.ShowGrepErrors)
            OnShowLog();
    }
    else
    {
        SendMessageW(HStatusBar, SB_SETTEXTW, 1 | SBT_NOBORDERS, (LPARAM)LoadStrW(IDS_STOPPED));
    }

    SetWindowTextW(HWindow, LoadStrW(IDS_FF_NAME));
    if (GrepData.Refine != 0)
        FoundFilesListView->DestroyDataForRefine();
    UpdateListViewItems();
    EnableControls();
}

void CFindDialog::EnableToolBar()
{
    if (FoundFilesListView == NULL)
        return;
    BOOL lvFocused = GetFocus() == FoundFilesListView->HWindow;
    BOOL selectedCount = ListView_GetSelectedCount(FoundFilesListView->HWindow);
    int focusedIndex = ListView_GetNextItem(FoundFilesListView->HWindow, -1, LVNI_FOCUSED);
    BOOL focusedIsFile = focusedIndex != -1 && !FoundFilesListView->At(focusedIndex)->IsDir;

    TBHeader->EnableItem(CM_FIND_FOCUS, FALSE, lvFocused && focusedIndex != -1);
    TBHeader->EnableItem(CM_FIND_VIEW, FALSE, lvFocused && focusedIsFile);
    TBHeader->EnableItem(CM_FIND_EDIT, FALSE, lvFocused && focusedIsFile);
    TBHeader->EnableItem(CM_FIND_DELETE, FALSE, lvFocused && selectedCount > 0);
    TBHeader->EnableItem(CM_FIND_USERMENU, FALSE, lvFocused && selectedCount > 0);
    TBHeader->EnableItem(CM_FIND_PROPERTIES, FALSE, lvFocused && selectedCount > 0);
    TBHeader->EnableItem(CM_FIND_CLIPCUT, FALSE, lvFocused && selectedCount > 0);
    TBHeader->EnableItem(CM_FIND_CLIPCOPY, FALSE, lvFocused && selectedCount > 0);
    TBHeader->EnableItem(IDC_FIND_STOP, FALSE, SearchInProgress);
}

void CFindDialog::EnableControls(BOOL nextIsButton)
{
    CALL_STACK_MESSAGE2("CFindDialog::EnableButtons(%d)", nextIsButton);
    if (FoundFilesListView == NULL)
        return;

    EnableToolBar();

    HWND focus = GetFocus();
    if (SearchInProgress)
    {
        HWND hFocus = GetFocus();

        EnableWindow(GetDlgItem(HWindow, IDC_FIND_NAMED), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_LOOKIN), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_LOOKIN_BROWSE), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_INCLUDE_SUBDIR), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_INCLUDE_ARCHIVES), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_FILETYPE_TEXT), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_FILETYPE), FALSE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_GREP), FALSE);
        if (Expanded)
        {
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_CONTAINING), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_REGEXP_BROWSE), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_HEX), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_WHOLE), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_CASE), FALSE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_REGULAR), FALSE);
        }
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_ADVANCED), FALSE);

        if (hFocus != NULL && !IsWindowEnabled(hFocus))
            SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)FoundFilesListView->HWindow, TRUE);
    }
    else
    {
        HWND setFocus = NULL;

        BOOL enableHexMode = !Data.RegularExpresions;
        if (!enableHexMode && GetDlgItem(HWindow, IDC_FIND_HEX) == focus)
            setFocus = GetDlgItem(HWindow, IDC_FIND_CONTAINING);

        TBHeader->EnableItem(IDC_FIND_STOP, FALSE, FALSE);

        /*
    int foundItems = FoundFilesListView->GetCount();
    int refineItems = FoundFilesListView->GetDataForRefineCount();
    BOOL refine = FALSE;
    EnableWindow(GetDlgItem(HWindow, IDC_FIND_INCLUDE_ARCHIVES), foundItems > 0);
    if (foundItems == 0 && refineItems == 0)
    {
      if (IsDlgButtonChecked(HWindow, IDC_FIND_INCLUDE_ARCHIVES) == BST_CHECKED)
        CheckDlgButton(HWindow, IDC_FIND_INCLUDE_ARCHIVES, BST_UNCHECKED);
    }
    else
      refine = IsDlgButtonChecked(HWindow, IDC_FIND_INCLUDE_ARCHIVES) == BST_CHECKED;
//    EnableWindow(GetDlgItem(HWindow, IDC_FIND_LOOKIN), !refine);
//    EnableWindow(GetDlgItem(HWindow, IDC_FIND_LOOKIN_BROWSE), !refine);
//    EnableWindow(GetDlgItem(HWindow, IDC_FIND_INCLUDE_SUBDIR), !refine);
    */

        EnableWindow(GetDlgItem(HWindow, IDC_FIND_NAMED), TRUE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_LOOKIN), TRUE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_LOOKIN_BROWSE), TRUE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_INCLUDE_SUBDIR), TRUE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_FILETYPE_TEXT), TRUE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_FILETYPE), TRUE);
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_GREP), TRUE);
        if (Expanded)
        {
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_CONTAINING), TRUE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_REGEXP_BROWSE), TRUE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_HEX), enableHexMode);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_WHOLE), TRUE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_CASE), TRUE);
            EnableWindow(GetDlgItem(HWindow, IDC_FIND_REGULAR), TRUE);
        }
        EnableWindow(GetDlgItem(HWindow, IDC_FIND_ADVANCED), TRUE);

        if (setFocus != NULL)
            SetFocus(setFocus);
    }

    int defID;
    if (focus == FoundFilesListView->HWindow &&
        ListView_GetItemCount(FoundFilesListView->HWindow) > 0)
    { // without the default push button
        defID = (int)SendMessage(HWindow, DM_GETDEFID, 0, 0);
        if (HIWORD(defID) == DC_HASDEFID)
            defID = LOWORD(defID);
        else
            defID = -1;
        SendMessage(HWindow, DM_SETDEFID, -1, 0);
        if (defID != -1)
            SendMessage(GetDlgItem(HWindow, defID), BM_SETSTYLE,
                        BS_PUSHBUTTON, MAKELPARAM(TRUE, 0));
    }
    else // select the default push button
    {
        if (nextIsButton)
        {
            defID = (int)SendMessage(HWindow, DM_GETDEFID, 0, 0);
            if (HIWORD(defID) == DC_HASDEFID)
            {
                defID = LOWORD(defID);
                PostMessage(GetDlgItem(HWindow, defID), BM_SETSTYLE, BS_PUSHBUTTON,
                            MAKELPARAM(TRUE, 0));
            }
        }
        defID = IDOK;
        // the following code caused the Find Now button to flicker during search
        // when the mouse focus rested on it; removing it doesn't seem to break anything, we'll see...
        /*
    wchar_t className[30];
    WORD wl = LOWORD(GetWindowLongPtr(focus, GWL_STYLE));  // only BS_ styles
    if (GetClassName(focus, className, 30) != 0 &&
        StrICmpW(className, "BUTTON") == 0 &&
        (wl == BS_PUSHBUTTON || wl == BS_DEFPUSHBUTTON))
    {
      nextIsButton = TRUE;
      PostMessage(focus, BM_SETSTYLE, BS_DEFPUSHBUTTON, MAKELPARAM(TRUE, 0));
    }
*/
        SendMessage(HWindow, DM_SETDEFID, defID, 0);
        if (nextIsButton)
            PostMessage(GetDlgItem(HWindow, defID), BM_SETSTYLE, BS_PUSHBUTTON,
                        MAKELPARAM(TRUE, 0));
    }
}

void CFindDialog::UpdateListViewItems()
{
    if (FoundFilesListView != NULL)
    {
        int count = FoundFilesListView->GetCount();

        // inform the list view about the new item count
        ListView_SetItemCountEx(FoundFilesListView->HWindow,
                                count,
                                LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
        // for the first added data, select the first item
        if (GrepData.FoundVisibleCount == 0 && count > 0)
        {
            ListView_SetItemState(FoundFilesListView->HWindow, 0,
                                  LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED)
            RedrawFindResultItem(FoundFilesListView->HWindow, 0);
        }

        // write the number of items above the list view
        TBHeader->SetFoundCount(count);
        TBHeader->SetErrorsInfosCount(Log.GetErrorCount(), Log.GetInfoCount());

        // when minimized, display the item count in the title
        if (IsIconic(HWindow))
        {
            std::wstring caption;
            if (SearchInProgress)
            {
                caption = FormatStrW(MINIMIZED_FINDING_CAPTION, FoundFilesListView->GetCount(),
                                     LoadStrW(IDS_FF_NAME), LoadStrW(IDS_FF_NAMED), SearchForData[0]->MasksGroup.GetMasksString());
            }
            else
                caption = LoadStrW(IDS_FF_NAME);
            SetWindowTextW(HWindow, caption.c_str());
        }

        // used by the search thread to know when to notify us next
        GrepData.FoundVisibleCount = count;
        GrepData.FoundVisibleTick = GetTickCount();

        EnableToolBar();
    }
}

void CFindDialog::OnSaveResults()
{
    if (SearchInProgress || FoundFilesListView == NULL)
        return;

    int count = FoundFilesListView->GetCount();
    if (count <= 0)
        return;

    std::vector<sally::find::FindResultRecord> records;
    records.reserve((size_t)count);
    int i;
    for (i = 0; i < count; i++)
    {
        CFoundFilesData* data = FoundFilesListView->At(i);
        if (data == NULL)
            continue;

        sally::find::FindResultRecord record;
        record.Path = data->PathW;
        record.Name = data->NameW;
        record.Size = data->Size.Value;
        record.LastWrite = data->LastWrite;
        record.Attr = data->Attr;
        record.IsDir = data->IsDir != 0;
        records.push_back(record);
    }

    std::wstring fileName;
    sally::find::FindResultsFormat format = sally::find::FindResultsFormat::Csv;
    if (!BrowseFindResultsFileNameW(HWindow, TRUE, fileName, format))
        return;

    std::wstring error;
    if (!sally::find::SaveFindResultsFile(fileName, format, records, &error))
    {
        std::wstring msg = FormatStrW(LoadStrW(IDS_FIND_RESULTS_SAVE_ERROR), error.c_str());
        if (gPrompter != NULL)
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
        else
            MessageBoxW(HWindow, msg.c_str(), LoadStrW(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
    }
}

void CFindDialog::OnLoadResults()
{
    if (SearchInProgress || FoundFilesListView == NULL)
        return;

    std::wstring fileName;
    sally::find::FindResultsFormat format = sally::find::FindResultsFormat::Csv;
    if (!BrowseFindResultsFileNameW(HWindow, FALSE, fileName, format))
        return;

    std::vector<sally::find::FindResultRecord> records;
    size_t skippedRows = 0;
    std::wstring error;
    if (!sally::find::LoadFindResultsFile(fileName, format, records, &skippedRows, &error))
    {
        std::wstring msg = FormatStrW(LoadStrW(IDS_FIND_RESULTS_LOAD_ERROR), error.c_str());
        if (gPrompter != NULL)
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
        else
            MessageBoxW(HWindow, msg.c_str(), LoadStrW(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
        return;
    }

    if (records.empty())
    {
        if (gPrompter != NULL)
            gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_FIND_RESULTS_LOAD_EMPTY));
        return;
    }

    PromptResult replaceResult = {};
    if (gPrompter != NULL)
        replaceResult = gPrompter->AskYesNoCancel(LoadStrW(IDS_QUESTION), LoadStrW(IDS_FIND_RESULTS_LOAD_MODE));
    else
    {
        int msgResult = MessageBoxW(HWindow, LoadStrW(IDS_FIND_RESULTS_LOAD_MODE), LoadStrW(IDS_QUESTION),
                                    MB_YESNOCANCEL | MB_ICONQUESTION);
        replaceResult.type = msgResult == IDYES ? PromptResult::kYes : (msgResult == IDNO ? PromptResult::kNo : PromptResult::kCancel);
    }
    if (replaceResult.type == PromptResult::kCancel)
        return;

    BOOL replace = replaceResult.type == PromptResult::kYes;
    int oldCount = FoundFilesListView->GetCount();
    if (replace)
    {
        ListView_SetItemCount(FoundFilesListView->HWindow, 0);
        FoundFilesListView->DestroyMembers();
        FoundFilesListView->DestroyDataForRefine();
        Log.Clean();
        GrepData.FoundVisibleCount = 0;
    }
    else
        FoundFilesListView->ClearDuplicateState();

    GrepData.FindDuplicates = FALSE;
    GrepData.FindDupFlags = 0;
    GrepData.Refine = 0;

    EnumFileNamesChangeSourceUID(FoundFilesListView->HWindow, &(FoundFilesListView->EnumFileNamesSourceUID));

    for (const sally::find::FindResultRecord& record : records)
    {
        CFoundFilesData* item = new CFoundFilesData;
        CQuadWord size;
        size.SetUI64(record.Size);
        item->Set(record.Path.c_str(), record.Name.c_str(),
                  size, record.Attr, &record.LastWrite, record.IsDir ? TRUE : FALSE);
        FoundFilesListView->Add(item);
        if (!FoundFilesListView->IsGood())
        {
            TRACE_E(LOW_MEMORY);
            FoundFilesListView->ResetState();
            UpdateListViewItems();
            if (gPrompter != NULL)
            {
                // The guard used to cover only the declaration, so the ShowError beside
                // it ran unconditionally - dereferencing gPrompter exactly in the
                // out-of-memory case the guard exists for.
                const std::wstring error = sally::legacy_search::DecodeEngineAcp(LOW_MEMORY);
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), error.c_str());
            }
            return;
        }
    }

    FoundFilesListView->ClearDuplicateState();
    if (!replace && oldCount == 0)
        GrepData.FoundVisibleCount = 0;
    UpdateListViewItems();
    UpdateStatusText();

    if (skippedRows > 0 && gPrompter != NULL)
    {
        std::wstring msg = FormatStrW(LoadStrW(IDS_FIND_RESULTS_LOAD_REPORT),
                                      (unsigned)records.size(), (unsigned)skippedRows);
        gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), msg.c_str());
    }
}

void CFindDialog::OnFocusFile()
{
    CALL_STACK_MESSAGE1("CFindDialog::FocusButton()");

    int index = ListView_GetNextItem(FoundFilesListView->HWindow, -1, LVNI_FOCUSED);
    if (index < 0)
        return;

    if (SalamanderBusy)
    {
        Sleep(200); // give Salamander time-if we switched from the main window the menu's message queue might still be running
        if (SalamanderBusy)
        {
            gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_SALAMANDBUSY2));
            return;
        }
    }
    CFoundFilesData* data = FoundFilesListView->At(index);

    CFocusFileDataW focus;
    focus.Name = data->NameW.c_str();
    focus.Path = data->PathW.c_str();
    SendMessage(MainWindow->GetActivePanel()->HWindow, WM_USER_FOCUSFILEW, (WPARAM)&focus, 0);
}

BOOL CFindDialog::GetFocusedFile(std::wstring& fullName, int* viewedIndex)
{
    int index = ListView_GetNextItem(FoundFilesListView->HWindow, -1, LVNI_FOCUSED);
    if (index < 0)
        return FALSE;

    if (viewedIndex != NULL)
        *viewedIndex = index;

    CFoundFilesData* data = FoundFilesListView->At(index);
    if (data->IsDir)
        return FALSE;

    fullName = data->GetFullNameW();
    return TRUE;
}

void CFindDialog::UpdateInternalViewerData()
{
    // copy the find text to the internal viewer
    if (Configuration.CopyFindText)
    { // Alt+F3 never reaches here, so no alternate viewer...
        CFindSetDialog oldGlobalFindDialog = GlobalFindDialog;

        GlobalFindDialog.Forward = TRUE;
        CTransferInfo dummyTI(HWindow, ttDataFromWindow);
        dummyTI.CheckBox(IDC_FIND_WHOLE, GlobalFindDialog.WholeWords);
        dummyTI.CheckBox(IDC_FIND_CASE, GlobalFindDialog.CaseSensitive);
        dummyTI.CheckBox(IDC_FIND_HEX, GlobalFindDialog.HexMode);
        dummyTI.CheckBox(IDC_FIND_REGULAR, GlobalFindDialog.Regular);
        dummyTI.EditLineW(IDC_FIND_CONTAINING, GlobalFindDialog.Text);

        HistoryComboBox(NULL, dummyTI, 0, GlobalFindDialog.Text,
                        !GlobalFindDialog.Regular && GlobalFindDialog.HexMode,
                        VIEWER_HISTORY_SIZE,
                        ViewerHistory, TRUE);
        if (!dummyTI.IsGood()) // something went wrong (hex mode)
            GlobalFindDialog = oldGlobalFindDialog;
    }
}

void CFindDialog::OnViewFile(BOOL alternate)
{
    CALL_STACK_MESSAGE2("CFindDialog::OnViewFile(%d)", alternate);
    std::wstring longName;
    int viewedIndex = 0;
    if (!GetFocusedFile(longName, &viewedIndex))
        return;

    if (SalamanderBusy)
    {
        Sleep(200); // give Salamander time-if we switched from the main window
                    // the menu's message queue might still be running
        if (SalamanderBusy)
        {
            gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_SALAMANDBUSY2));
            return;
        }
    }
    UpdateInternalViewerData();
    COpenViewerData openData;
    openData.FileName = longName.c_str();
    openData.EnumFileNamesSourceUID = FoundFilesListView->EnumFileNamesSourceUID;
    openData.EnumFileNamesLastFileIndex = viewedIndex;
    SendMessage(MainWindow->GetActivePanel()->HWindow, WM_USER_VIEWFILE, (WPARAM)(&openData), (LPARAM)alternate);
}

void CFindDialog::OnEditFile()
{
    CALL_STACK_MESSAGE1("CFindDialog::OnEditFile()");
    std::wstring longName;
    if (!GetFocusedFile(longName, NULL))
        return;

    if (SalamanderBusy)
    {
        Sleep(200); // give Salamander time-if we switched from the main window
                    // the menu's message queue might still be running
        if (SalamanderBusy)
        {
            gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_SALAMANDBUSY2));
            return;
        }
    }
    CEditFileData editData;
    editData.FileName = longName.c_str();
    SendMessage(MainWindow->GetActivePanel()->HWindow, WM_USER_EDITFILE, (WPARAM)(&editData), 0);
}

void CFindDialog::OnViewFileWith()
{
    CALL_STACK_MESSAGE1("CFindDialog::OnViewFileWith()");
    std::wstring longName;
    int viewedIndex = 0;
    if (!GetFocusedFile(longName, &viewedIndex))
        return;

    if (SalamanderBusy)
    {
        Sleep(200); // give Salamander time-if we switched from the main window
                    // the menu's message queue might still be running
        if (SalamanderBusy)
        {
            gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_SALAMANDBUSY2));
            return;
        }
    }
    UpdateInternalViewerData();
    POINT menuPoint;
    GetListViewContextMenuPos(FoundFilesListView->HWindow, &menuPoint);
    DWORD handlerID;
    // this call isn't entirely correct because ViewFileWith lacks critical sections
    // for working with the configuration. the assumption for proper functioning is that the user does only one thing
    // (doesn't edit configuration while working in the Find window) -- hopefully almost always true
    MainWindow->GetActivePanel()->ViewFileWith(longName.c_str(), FoundFilesListView->HWindow, &menuPoint, &handlerID, -1, -1);
    if (handlerID != 0xFFFFFFFF)
    {
        if (SalamanderBusy) // almost impossible, but Salamander could be busy
        {
            Sleep(200); // give Salamander time-if we switched from the main window
                        // the menu's message queue might still be running
            if (SalamanderBusy)
            {
                gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_SALAMANDBUSY2));
                return;
            }
        }
        COpenViewerData openData;
        openData.FileName = longName.c_str();
        openData.EnumFileNamesSourceUID = FoundFilesListView->EnumFileNamesSourceUID;
        openData.EnumFileNamesLastFileIndex = viewedIndex;
        SendMessage(MainWindow->GetActivePanel()->HWindow, WM_USER_VIEWFILEWITH,
                    (WPARAM)(&openData), (LPARAM)handlerID);
    }
}

void CFindDialog::OnEditFileWith()
{
    CALL_STACK_MESSAGE1("CFindDialog::OnEditFileWith()");
    std::wstring longName;
    if (!GetFocusedFile(longName, NULL))
        return;

    if (SalamanderBusy)
    {
        Sleep(200); // give Salamander time-if we switched from the main window
                    // the menu's message queue might still be running
        if (SalamanderBusy)
        {
            gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_SALAMANDBUSY2));
            return;
        }
    }
    POINT menuPoint;
    GetListViewContextMenuPos(FoundFilesListView->HWindow, &menuPoint);
    DWORD handlerID;
    // this call isn't entirely correct because EditFileWith lacks critical sections
    // for working with the configuration. the assumption for proper functioning is that the user does only one thing
    // (doesn't change configuration while using the Find window) -- hopefully almost always true
    MainWindow->GetActivePanel()->EditFileWith(longName.c_str(), FoundFilesListView->HWindow, &menuPoint, &handlerID);
    if (handlerID != 0xFFFFFFFF)
    {
        if (SalamanderBusy) // almost impossible, but Salamander could be busy
        {
            Sleep(200); // give Salamander time-if we switched from the main window
                        // the menu's message queue might still be running
            if (SalamanderBusy)
            {
                gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_SALAMANDBUSY2));
                return;
            }
        }
        CEditFileData editData;
        editData.FileName = longName.c_str();
        SendMessage(MainWindow->GetActivePanel()->HWindow, WM_USER_EDITFILEWITH,
                    (WPARAM)(&editData), (LPARAM)handlerID);
    }
}

void CFindDialog::OnUserMenu()
{
    CALL_STACK_MESSAGE1("CFindDialog::OnUserMenu()");
    DWORD selectedCount = ListView_GetSelectedCount(FoundFilesListView->HWindow);
    if (selectedCount < 1)
        return;

    // This was the fourth (and last) of Find's ANSI-mirror refusal guards.
    // It refused whenever a selected row's name could not
    // round-trip CP_ACP exactly, because user menu commands used to receive the rows'
    // ANSI names as arguments. That is no longer true: ListOfSelNames/ListOfSelFullNames,
    // CompareName1/CompareName2, and the GetNextItemFromFind callback below all consume
    // file->NameW/PathW directly, and the expansion pipeline they feed
    // (ExpandCommand2/ExpandUserMenuArguments/ExpandInitDir, execute.cpp's
    // sally::unicode::WideVarEntry tables) is wide end to end. CFoundFilesData itself no
    // longer has a narrow half to fall back to (find.h's own comment: NameW/
    // PathW "are the only representation"), so there is nothing left for this guard to
    // protect - refusing here only blocked the whole User Menu action for a selection
    // containing a non-ASCII-named row, same shape already retired for
    // View/Edit/ViewWith/EditWith and Focus.

    UserMenuIconBkgndReader.BeginUserMenuIconsInUse();
    CMenuPopup menu;
    MainWindow->FillUserMenu(&menu, FALSE); // keep customization disabled
    POINT p;
    GetListViewContextMenuPos(FoundFilesListView->HWindow, &p);
    // another locking round (BeginUserMenuIconsInUse+EndUserMenuIconsInUse) will happen
    // inside WM_USER_ENTERMENULOOP and WM_USER_LEAVEMENULOOP; it's nested and has no overhead,
    // so we ignore it and don't try to fight it
    DWORD cmd = menu.Track(MENU_TRACK_RETURNCMD, p.x, p.y, HWindow, NULL);
    UserMenuIconBkgndReader.EndUserMenuIconsInUse();

    if (cmd != 0)
    {
        CUserMenuAdvancedData userMenuAdvancedData;

        std::wstring& list = userMenuAdvancedData.ListOfSelNames;
        int findItem = -1;
        DWORD i;
        for (i = 0; i < selectedCount; i++) // fill the list of selected names
        {
            findItem = ListView_GetNextItem(FoundFilesListView->HWindow, findItem, LVNI_SELECTED);
            if (findItem != -1)
            {
                CFoundFilesData* file = FoundFilesListView->At(findItem);
                if (!AppendUserMenuArgument(list, file->NameW.c_str(), file->NameW.length(), USRMNUARGS_MAXLEN - 1))
                    break;
            }
        }
        if (i < selectedCount)
            list.clear(); // the expanded command cannot carry the complete selection
        userMenuAdvancedData.ListOfSelNamesIsEmpty = FALSE; // not a concern for Find (otherwise the User Menu would not open)

        std::wstring& listFull = userMenuAdvancedData.ListOfSelFullNames;
        findItem = -1;
        for (i = 0; i < selectedCount; i++) // fill the list of selected names
        {
            findItem = ListView_GetNextItem(FoundFilesListView->HWindow, findItem, LVNI_SELECTED);
            if (findItem != -1)
            {
                CFoundFilesData* file = FoundFilesListView->At(findItem);
                std::wstring fullName = file->PathW;
                SalPathAppendW(fullName, file->NameW.c_str());
                if (!AppendUserMenuArgument(listFull, fullName.c_str(), fullName.size(), USRMNUARGS_MAXLEN - 1))
                    break;
            }
        }
        if (i < selectedCount)
            listFull.clear(); // the expanded command cannot carry the complete selection
        userMenuAdvancedData.ListOfSelFullNamesIsEmpty = FALSE; // not a concern for Find (otherwise the User Menu would not open)

        userMenuAdvancedData.FullPathLeft.clear();
        userMenuAdvancedData.FullPathRight.clear();
        userMenuAdvancedData.FullPathInactive = &userMenuAdvancedData.FullPathLeft;

        int comp1 = -1;
        int comp2 = -1;
        if (selectedCount == 1)
            comp1 = ListView_GetNextItem(FoundFilesListView->HWindow, -1, LVNI_SELECTED);
        else
        {
            if (selectedCount == 2)
            {
                comp1 = ListView_GetNextItem(FoundFilesListView->HWindow, -1, LVNI_SELECTED);
                comp2 = ListView_GetNextItem(FoundFilesListView->HWindow, comp1, LVNI_SELECTED);
            }
        }
        userMenuAdvancedData.CompareNamesAreDirs = FALSE;
        userMenuAdvancedData.CompareNamesReversed = FALSE;
        if (comp1 != -1 && comp2 != -1 &&
            FoundFilesListView->At(comp1)->IsDir != FoundFilesListView->At(comp2)->IsDir)
        {
            comp1 = -1;
            comp2 = -1;
        }
        if (comp1 == -1)
            userMenuAdvancedData.CompareName1.clear();
        else
        {
            CFoundFilesData* file = FoundFilesListView->At(comp1);
            userMenuAdvancedData.CompareNamesAreDirs = file->IsDir;
            userMenuAdvancedData.CompareName1 = file->PathW;
            SalPathAppendW(userMenuAdvancedData.CompareName1, file->NameW.c_str());
        }
        if (comp2 == -1)
            userMenuAdvancedData.CompareName2.clear();
        else
        {
            CFoundFilesData* file = FoundFilesListView->At(comp2);
            userMenuAdvancedData.CompareNamesAreDirs = file->IsDir;
            userMenuAdvancedData.CompareName2 = file->PathW;
            SalPathAppendW(userMenuAdvancedData.CompareName2, file->NameW.c_str());
        }

        CUMDataFromFind data(FoundFilesListView->HWindow);
        MainWindow->UserMenu(HWindow, cmd - CM_USERMENU_MIN,
                             GetNextItemFromFind, &data, &userMenuAdvancedData);
        SetFocus(FoundFilesListView->HWindow);
    }
}

void CFindDialog::OnCopyNameToClipboard(CCopyNameToClipboardModeEnum mode)
{
    CALL_STACK_MESSAGE1("CFindDialog::FocusButton()");
    DWORD selectedCount = ListView_GetSelectedCount(FoundFilesListView->HWindow);
    if (selectedCount != 1)
        return;
    int index = ListView_GetNextItem(FoundFilesListView->HWindow, -1, LVNI_SELECTED);
    if (index < 0)
        return;
    CFoundFilesData* data = FoundFilesListView->At(index);
    std::wstring textW;
    switch (mode)
    {
    case cntcmFullName:
    {
        textW = data->GetFullNameTextW(FileNameFormat);
        break;
    }

    case cntcmName:
    {
        textW = data->GetNameTextW(FileNameFormat);
        break;
    }

    case cntcmFullPath:
    {
        textW = data->PathW;
        break;
    }

    case cntcmUNCName:
    {
        // PathW is the same source cntcmFullPath above already uses; the
        // narrow Path/Name pair only ever named the file when the code page could spell it.
        CopyUNCPathToClipboardW(data->PathW.c_str(), data->GetNameTextW(FileNameFormat).c_str(),
                                data->IsDir, HWindow);
        return;
    }
    }
    if (mode != cntcmUNCName)
        CopyTextToClipboardW(textW.c_str());
}

BOOL CFindDialog::IsMenuBarMessage(CONST MSG* lpMsg)
{
    CALL_STACK_MESSAGE_NONE
    if (MenuBar == NULL)
        return FALSE;
    return MenuBar->IsMenuBarMessage(lpMsg);
}

void CFindDialog::InsertDrives(HWND hEdit, BOOL network)
{
    CALL_STACK_MESSAGE_NONE
    wchar_t drives[200];
    wchar_t* iterator = drives;
    wchar_t root[4] = L" :\\";
    wchar_t drive = L'A';
    DWORD mask = GetLogicalDrives();
    int i = 1;
    while (i != 0)
    {
        if (mask & i) // the drive is accessible
        {
            root[0] = drive;
            DWORD driveType = GetDriveTypeW(root);
            if (driveType == DRIVE_FIXED || network && driveType == DRIVE_REMOTE)
            {
                if (iterator > drives)
                {
                    *iterator++ = L';';
                }
                memmove(iterator, root, 3 * sizeof(wchar_t));
                iterator += 3;
            }
        }
        i <<= 1;
        drive++;
    }
    *iterator = L'\0';

    SetWindowTextW(hEdit, drives);
    SendMessageW(hEdit, EM_SETSEL, lstrlenW(drives), lstrlenW(drives));
}

BOOL CFindDialog::CanCloseWindow()
{
    // we used to get bug reports with crashes in CFindDialog::StopSearch()
    // likely because the window was being destroyed while still inside the method
    // this check on CanClose disappeared after version 1.52
    if (!CanClose)
        return FALSE;

    // if CShellExecuteWnd windows exist, we offer to cancel closing or send a bug report and terminate
    std::wstring reason = L"Some faulty shell extension has locked our find window.";
    if (EnumCShellExecuteWnd(HWindow, reason) > 0)
    {
        // ask whether Salamander should continue or generate a bug report
        if (SalMessageBoxW(HWindow, LoadStrW(IDS_SHELLEXTBREAK3), SALAMANDER_TEXT_VERSIONW(),
                           MSGBOXEX_CONTINUEABORT | MB_ICONINFORMATION | MSGBOXEX_SETFOREGROUND) != IDABORT)
        {
            return FALSE; // continue
        }

        // break into the debugger
        SetBugReportReasonBreak(std::move(reason));
        TaskList.FireEvent(TASKLIST_TODO_BREAK, GetCurrentProcessId());
        // freeze this thread
        while (1)
            Sleep(1000);
    }
    return TRUE;
}

BOOL CFindDialog::DoYouWantToStopSearching()
{
    PromptResult::Type ret = PromptResult::kYes;
    if (Configuration.CnfrmStopFind)
    {
        bool dontShow = !Configuration.CnfrmStopFind;
        ret = gPrompter->AskYesNoWithCheckbox(LoadStrW(IDS_WANTTOSTOPTITLE), LoadStrW(IDS_WANTTOSTOP),
                                              LoadStrW(IDS_DONTSHOWAGAINSS), &dontShow).type;
        Configuration.CnfrmStopFind = !dontShow;
    }
    return (ret == PromptResult::kYes);
}

// pull the text from the control and search for a hot key;
// if found, return its character (uppercase), otherwise return 0
wchar_t GetControlHotKey(HWND hWnd, int resID)
{
    wchar_t buff[500];
    if (!GetDlgItemTextW(hWnd, resID, buff, 500))
        return 0;
    const wchar_t* p = buff;
    while (*p != 0)
    {
        if (*p == L'&' && *(p + 1) != L'&' && *(p + 1) != 0)
            // UpperCase is a 256-entry byte table; a non-Latin-1 mnemonic (a
            // translated "&X" accelerator) falls outside it, so compare as-is.
            return *(p + 1) < 256 ? (wchar_t)UpperCase[(unsigned char)*(p + 1)] : *(p + 1);
        p++;
    }
    return 0;
}

BOOL CFindDialog::ManageHiddenShortcuts(const MSG* msg)
{
    if (msg->message == WM_SYSKEYDOWN)
    {
        BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!controlPressed && altPressed && !shiftPressed)
        {
            // if Alt+? is pressed while the Options section is collapsed, it makes sense to investigate further
            if (!IsDlgButtonChecked(HWindow, IDC_FIND_GREP))
            {
                // check the hotkeys of monitored controls
                int resID[] = {IDC_FIND_CONTAINING_TEXT, IDC_FIND_HEX, IDC_FIND_CASE,
                               IDC_FIND_WHOLE, IDC_FIND_REGULAR, -1}; // (terminate with -1)
                int i;
                for (i = 0; resID[i] != -1; i++)
                {
                    wchar_t key = GetControlHotKey(HWindow, resID[i]);
                    if (key != 0 && (WPARAM)key == msg->wParam)
                    {
                        // expand the Options section
                        CheckDlgButton(HWindow, IDC_FIND_GREP, BST_CHECKED);
                        SendMessage(HWindow, WM_COMMAND, MAKEWPARAM(IDC_FIND_GREP, BN_CLICKED), 0);
                        return FALSE; // expanded; IsDialogMessage will handle the rest after we return
                    }
                }
            }
        }
    }
    return FALSE; // not our message
}

void CFindDialog::SetFullRowSelect(BOOL fullRow)
{
    Configuration.FindFullRowSelect = fullRow;

    // notify all find dialogs about the change
    FindDialogQueue.BroadcastMessage(WM_USER_FINDFULLROWSEL, 0, 0);
}

INT_PTR
CFindDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CFindDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_PAINT:
    {
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        PaintFindDialogSeparatorLines(HWindow, NULL);
        return ret;
    }

    case WM_PRINTCLIENT:
    {
        INT_PTR ret = CCommonDialog::DialogProc(uMsg, wParam, lParam);
        PaintFindDialogSeparatorLines(HWindow, (HDC)wParam);
        return ret;
    }

    case WM_INITDIALOG:
    {
        FindDialogQueue.Add(new CWindowQueueItem(HWindow));

        FindNowText = GetWindowTextStringW(GetDlgItem(HWindow, IDOK));

        UpdateAdvancedText();

        InstallWordBreakProc(GetDlgItem(HWindow, IDC_FIND_NAMED));      // install WordBreakProc into the combo box
        InstallWordBreakProc(GetDlgItem(HWindow, IDC_FIND_LOOKIN));     // install WordBreakProc into the combo box
        InstallWordBreakProc(GetDlgItem(HWindow, IDC_FIND_CONTAINING)); // install WordBreakProc into the combo box

        CComboboxEdit* edit = new CComboboxEdit();
        if (edit != NULL)
        {
            HWND hCombo = GetDlgItem(HWindow, IDC_FIND_CONTAINING);
            edit->AttachToWindow(GetWindow(hCombo, GW_CHILD));
        }
        ChangeToArrowButton(HWindow, IDC_FIND_REGEXP_BROWSE);

        OKButton = new CButton(HWindow, IDOK, BTF_DROPDOWN);
        new CButton(HWindow, IDC_FIND_LOOKIN_BROWSE, BTF_RIGHTARROW);

        // set the checkbox controlling the visibility of the Content section of the dialog
        CheckDlgButton(HWindow, IDC_FIND_GREP, Configuration.SearchFileContent);
        HWND hFileType = GetDlgItem(HWindow, IDC_FIND_FILETYPE);
        SendMessageW(hFileType, CB_ADDSTRING, 0, (LPARAM)LoadStrW(IDS_FIND_TYPE_ALL));
        SendMessageW(hFileType, CB_ADDSTRING, 0, (LPARAM)LoadStrW(IDS_FIND_TYPE_FILES));
        SendMessageW(hFileType, CB_ADDSTRING, 0, (LPARAM)LoadStrW(IDS_FIND_TYPE_FOLDERS));
        SendMessage(hFileType, CB_SETCURSEL, Data.FileTypeMode, 0);

        // assign an icon to the window
        HICON findIcon = HANDLES(LoadIcon(ImageResDLL, MAKEINTRESOURCE(8)));
        if (findIcon == NULL)
            findIcon = HANDLES(LoadIcon(HInstance, MAKEINTRESOURCE(IDI_FIND)));
        SendMessage(HWindow, WM_SETICON, ICON_BIG, (LPARAM)findIcon);

        // construct the list view
        FoundFilesListView = new CFoundFilesListView(HWindow, IDC_FIND_RESULTS, this);
        ListView_SetUnicodeFormat(FoundFilesListView->HWindow, TRUE);

        SetFullRowSelect(Configuration.FindFullRowSelect);

        TBHeader = new CFindTBHeader(HWindow, IDC_FIND_FOUND_FILES);

        // create the status bar
        HStatusBar = CreateWindowExW(0,
                                    STATUSCLASSNAMEW,
                                    (LPCWSTR)NULL,
                                    SBARS_SIZEGRIP | WS_CHILD | CCS_BOTTOM | WS_VISIBLE,
                                    0, 0, 0, 0,
                                    HWindow,
                                    (HMENU)IDC_FIND_STATUS,
                                    HInstance,
                                    NULL);
        if (HStatusBar == NULL)
        {
            TRACE_E("Error creating StatusBar");
            DlgFailed = TRUE;
            PostMessage(HWindow, WM_COMMAND, IDCANCEL, 0);
            break;
        }

        SetTwoStatusParts(FALSE, TRUE);
        SendMessageW(HStatusBar, SB_SETTEXTW, 1 | SBT_NOBORDERS, (LPARAM)LoadStrW(IDS_FIND_INIT_HINT));

        // assign a menu to the window
        MainMenu = new CMenuPopup;

        BuildFindMenu(MainMenu);
        MenuBar = new CMenuBar(MainMenu, HWindow);
        if (!MenuBar->CreateWnd(HWindow))
        {
            TRACE_E("Error creating Menu");
            DlgFailed = TRUE;
            PostMessage(HWindow, WM_COMMAND, IDCANCEL, 0);
            break;
        }
        ShowWindow(MenuBar->HWindow, SW_SHOW);

        // load parameters for laying out the window
        GetLayoutParams();

        WINDOWPLACEMENT* wp = &Configuration.FindDialogWindowPlacement;
        if (wp->length != 0)
        {
            RECT r = wp->rcNormalPosition;
            SetWindowPos(HWindow, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
            if (wp->showCmd == SW_MAXIMIZE || wp->showCmd == SW_SHOWMAXIMIZED)
                ShowWindow(HWindow, SW_MAXIMIZE);
        }

        SetWindowPos(HWindow, Configuration.AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        LayoutControls();
        FoundFilesListView->InitColumns();
        SetContentVisible(Configuration.SearchFileContent);

        // remove WS_TABSTOP from IDC_FIND_ADVANCED_TEXT
        DWORD style = (DWORD)GetWindowLongPtr(GetDlgItem(HWindow, IDC_FIND_ADVANCED_TEXT), GWL_STYLE);
        style &= ~WS_TABSTOP;
        SetWindowLongPtr(GetDlgItem(HWindow, IDC_FIND_ADVANCED_TEXT), GWL_STYLE, style);

        ListView_SetItemCount(FoundFilesListView->HWindow, 0);

        SetDlgItemTextW(HWindow, IDOK, FindNowText.c_str());
        TBHeader->SetFoundCount(0);

        SetWindowTextW(HWindow, LoadStrW(IDS_FF_NAME));

        int i;
        for (i = 0; i < FindOptions.GetCount(); i++)
            if (FindOptions.At(i)->AutoLoad)
            {
                const std::wstring text = FormatStrW(LoadStrW(IDS_FF_AUTOLOAD),
                                                     FindOptions.At(i)->ItemName.c_str());
                SendMessageW(HStatusBar, SB_SETTEXTW, 1 | SBT_NOBORDERS,
                             (LPARAM)text.c_str());
                break;
            }

        // Not supported yet: keep IDC_FIND_INCLUDE_ARCHIVES for code compatibility while IDC_FIND_FILETYPE reuses its layout slot.
        ShowWindow(GetDlgItem(HWindow, IDC_FIND_INCLUDE_ARCHIVES), FALSE);

        EnableControls();
        ApplyFindDialogTheme(HWindow, HStatusBar);
        PostMessage(HWindow, WM_USER_FIND_DELAYED_THEME, 0, 0);
        // Defer the active-panel override past the framework's initial transfer.
        PostMessage(HWindow, WM_USER_FIND_LOOKIN_W_OVERRIDE, 0, 0);
        break;
    }

    case WM_TIMER:
    {
        if (wParam == IDT_REPAINT)
        {
            if (SearchingText.GetDirty())
            {
                SearchingText.SetDirty(FALSE); // already being redrawn - Get will be called; better to refresh twice than not at all
                if (DarkMode_ShouldUseDark())
                {
                    std::wstring text = SearchingText.GetWString();
                    SendMessageW(HStatusBar, SB_SETTEXTW, 1 | SBT_NOBORDERS, (LPARAM)text.c_str());
                }
                else
                    SendMessage(HStatusBar, SB_SETTEXT, 1 | SBT_NOBORDERS | SBT_OWNERDRAW, 0);
                RedrawWindow(HStatusBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            }
            if (SearchingText2.GetDirty())
            {
                if (!TwoParts)
                    SetTwoStatusParts(TRUE);
                SearchingText2.SetDirty(FALSE); // already being redrawn - Get will be called; better to refresh twice than not at all
                const std::wstring progress = SearchingText2.GetWString();
                int pos = progress.empty() ? 0 : progress[0]; // numeric value, not display text
                SendMessage(HProgressBar, PBM_SETPOS, pos, 0);
                RedrawWindow(HStatusBar, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            }
            return 0;
        }
        break;
    }

    case WM_USER_FLASHICON:
    {
        if (GetForegroundWindow() == HWindow && TBHeader != NULL)
            TBHeader->StartFlashIcon();
        else
            FlashIconsOnActivation = TRUE;
        return 0;
    }

    case WM_USER_COLORCHANGEFIND:
    {
        OnColorsChange();
        ApplyFindDialogTheme(HWindow, HStatusBar);
        return TRUE;
    }

    case WM_USER_FIND_DELAYED_THEME:
    {
        ApplyFindDialogTheme(HWindow, HStatusBar);
        return TRUE;
    }

    case WM_USER_FIND_LOOKIN_W_OVERRIDE:
    {
        // Re-apply the construction-time active-panel path after the framework's
        // initial transfer has finished touching controls. This keeps new Find
        // windows anchored to the active panel even when saved/autoloaded data
        // or combo history left a stale value in the edit control. Explicit
        // later preset loads can still replace the field through LoadControls.
        const sally::find::LookInSeed& seed = InitialLookInSeed;
        HWND hLegacyCombo = GetDlgItem(HWindow, IDC_FIND_LOOKIN);
        ApplyInitialFindLookInSeed(hLegacyCombo, seed, Data.LookInText);
        if (sally::find::HasInitialLookInSeed(seed))
        {
            ActivateWideLookInCombo(HWindow, Data.LookInText, LookInUnicodeFont);
            ApplyFindComboSkin(GetDlgItem(HWindow, IDC_FIND_LOOKIN));
        }
        return TRUE;
    }

    case WM_USER_FINDFULLROWSEL:
    {
        DWORD flags = ListView_GetExtendedListViewStyle(FoundFilesListView->HWindow);
        BOOL hasFullRow = (flags & LVS_EX_FULLROWSELECT) != 0;
        if (hasFullRow != Configuration.FindFullRowSelect)
        {
            if (Configuration.FindFullRowSelect)
                flags |= LVS_EX_FULLROWSELECT;
            else
                flags &= ~LVS_EX_FULLROWSELECT;
            ListView_SetExtendedListViewStyle(FoundFilesListView->HWindow, flags); // 4.71
        }
        return TRUE;
    }

    case WM_USER_CLEARHISTORY:
    {
        ClearComboboxListbox(GetDlgItem(HWindow, IDC_FIND_NAMED));
        ClearComboboxListbox(GetDlgItem(HWindow, IDC_FIND_LOOKIN));
        ClearComboboxListbox(GetDlgItem(HWindow, IDC_FIND_CONTAINING));
        return TRUE;
    }

    // used to test closing the window because Salamander is shutting down
    case WM_USER_QUERYCLOSEFIND:
    {
        BOOL query = TRUE;
        if (SearchInProgress)
        {
            if (lParam /* quiet */)
                StopSearch(); // no need to ask anything, stop the ongoing search anyway
            else
            {
                if (!DoYouWantToStopSearching())
                    query = FALSE;
                else
                {
                    if (SearchInProgress) // stop searching immediately if the user wants
                        StopSearch();
                }
            }
        }
        if (query)
            query = CanCloseWindow();
        if (StateOfFindCloseQuery == sofcqSentToFind)
            StateOfFindCloseQuery = query ? sofcqCanClose : sofcqCannotClose;
        return TRUE;
    }

    // used for remote closing of the window because Salamander is shutting down
    case WM_USER_CLOSEFIND:
    {
        if (SearchInProgress)
            StopSearch();
        DestroyWindow(HWindow);
        return 0;
    }

    case WM_USER_INITMENUPOPUP:
    {
        // menu enablers
        if (FoundFilesListView != NULL && FoundFilesListView->HWindow != NULL)
        {
            CMenuPopup* popup = (CMenuPopup*)(CGUIMenuPopupAbstract*)wParam;
            WORD popupID = HIWORD(lParam);

            BOOL lvFocused = GetFocus() == FoundFilesListView->HWindow;
            DWORD totalCount = ListView_GetItemCount(FoundFilesListView->HWindow);
            BOOL selectedCount = ListView_GetSelectedCount(FoundFilesListView->HWindow);
            int focusedIndex = ListView_GetNextItem(FoundFilesListView->HWindow, -1, LVNI_FOCUSED);
            BOOL focusedIsFile = focusedIndex != -1 && !FoundFilesListView->At(focusedIndex)->IsDir;

            switch (popupID)
            {
            case CML_FIND_FILES:
            {
                popup->EnableItem(CM_FIND_OPEN, FALSE, lvFocused && selectedCount > 0);
                popup->EnableItem(CM_FIND_OPENSEL, FALSE, lvFocused && selectedCount > 0);
                popup->EnableItem(CM_FIND_FOCUS, FALSE, lvFocused && focusedIndex != -1);
                popup->EnableItem(CM_FIND_HIDESEL, FALSE, lvFocused && selectedCount > 0);
                popup->EnableItem(CM_FIND_HIDE_DUP, FALSE, totalCount > 0);
                popup->EnableItem(CM_FIND_VIEW, FALSE, lvFocused && focusedIsFile);
                popup->EnableItem(CM_FIND_VIEW_WITH, FALSE, lvFocused && focusedIsFile);
                popup->EnableItem(CM_FIND_ALTVIEW, FALSE, lvFocused && focusedIsFile);
                popup->EnableItem(CM_FIND_EDIT, FALSE, lvFocused && focusedIsFile);
                popup->EnableItem(CM_FIND_EDIT_WITH, FALSE, lvFocused && focusedIsFile);
                popup->EnableItem(CM_FIND_DELETE, FALSE, lvFocused && selectedCount > 0);
                popup->EnableItem(CM_FIND_USERMENU, FALSE, lvFocused && selectedCount > 0);
                popup->EnableItem(CM_FIND_PROPERTIES, FALSE, lvFocused && selectedCount > 0);
                popup->EnableItem(CM_FIND_SAVE_RESULTS, FALSE, !SearchInProgress && totalCount > 0);
                popup->EnableItem(CM_FIND_LOAD_RESULTS, FALSE, !SearchInProgress);
                break;
            }

            case CML_FIND_FIND:
            {
                popup->EnableItem(CM_FIND_NOW, FALSE, !SearchInProgress);
                popup->EnableItem(CM_FIND_INTERSECT, FALSE, !SearchInProgress && totalCount > 0);
                popup->EnableItem(CM_FIND_SUBTRACT, FALSE, !SearchInProgress && totalCount > 0);
                popup->EnableItem(CM_FIND_APPEND, FALSE, !SearchInProgress && totalCount > 0);
                popup->EnableItem(CM_FIND_DUPLICATES, FALSE, !SearchInProgress);
                popup->EnableItem(CM_FIND_MESSAGES, FALSE, Log.GetCount() > 0);
                break;
            }

            case CML_FIND_EDIT:
            {
                popup->EnableItem(CM_FIND_CLIPCUT, FALSE, lvFocused && selectedCount > 0);
                popup->EnableItem(CM_FIND_CLIPCOPY, FALSE, lvFocused && selectedCount > 0);
                popup->EnableItem(CM_FIND_CLIPCOPYFULLNAME, FALSE, lvFocused && selectedCount == 1);
                popup->EnableItem(CM_FIND_CLIPCOPYNAME, FALSE, lvFocused && selectedCount == 1);
                popup->EnableItem(CM_FIND_CLIPCOPYFULLPATH, FALSE, lvFocused && selectedCount == 1);
                popup->EnableItem(CM_FIND_CLIPCOPYUNCNAME, FALSE, lvFocused && selectedCount == 1);
                popup->EnableItem(CM_FIND_SELECTALL, FALSE, lvFocused && totalCount > 0);
                popup->EnableItem(CM_FIND_INVERTSEL, FALSE, lvFocused && totalCount > 0);
                break;
            }

            case CML_FIND_VIEW:
            {
                BOOL enabledNameSize = TRUE;
                BOOL enabledPathTime = TRUE;
                if (GrepData.FindDuplicates)
                {
                    enabledPathTime = FALSE; // path and time are irrelevant for duplicates
                    // sorting by name and size works for duplicates only
                    // if the search was by the same name and size
                    enabledNameSize = (GrepData.FindDupFlags & FIND_DUPLICATES_NAME) &&
                                      (GrepData.FindDupFlags & FIND_DUPLICATES_SIZE);
                }
                popup->EnableItem(CM_FIND_NAME, FALSE, enabledNameSize && totalCount > 0);
                popup->EnableItem(CM_FIND_PATH, FALSE, enabledPathTime && totalCount > 0);
                popup->EnableItem(CM_FIND_TIME, FALSE, enabledPathTime && totalCount > 0);
                popup->EnableItem(CM_FIND_SIZE, FALSE, enabledNameSize && totalCount > 0);
                break;
            }

            case CML_FIND_OPTIONS:
            {
                static int count = -1;
                if (count == -1)
                    count = popup->GetItemCount();

                popup->CheckItem(CM_FIND_SHOWERRORS, FALSE, Configuration.ShowGrepErrors);
                popup->CheckItem(CM_FIND_FULLROWSEL, FALSE, Configuration.FindFullRowSelect);
                // if the manage dialog is open, disable it in another window and also disable adding to the list
                popup->EnableItem(CM_FIND_ADD_CURRENT, FALSE, !FindManageInUse);
                popup->EnableItem(CM_FIND_MANAGE, FALSE, !FindManageInUse);
                popup->EnableItem(CM_FIND_IGNORE, FALSE, !FindIgnoreInUse);
                FindOptions.InitMenu(popup, !SearchInProgress, count);
                break;
            }
            }
        }
        break;
    }

    case WM_USER_BUTTONDROPDOWN:
    {
        if (SearchInProgress)
            return 0;

        HWND hCtrl = GetDlgItem(HWindow, (int)wParam);
        RECT r;
        GetWindowRect(hCtrl, &r);

        CGUIMenuPopupAbstract* popup = MainMenu->GetSubMenu(CML_FIND_FIND, FALSE);
        if (popup != NULL)
        {
            BOOL selectMenuItem = LOWORD(lParam);
            DWORD flags = 0;
            if (selectMenuItem)
            {
                popup->SetSelectedItemIndex(0);
                flags |= MENU_TRACK_SELECT;
            }
            popup->Track(flags, r.left, r.bottom, HWindow, &r);
        }
        break;
    }

    case WM_SIZE:
    {
        // when restoring, refresh the window title
        if (SearchInProgress && (wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED)) // restore
        {
            const std::wstring caption = FormatStrW(NORMAL_FINDING_CAPTION, LoadStrW(IDS_FF_NAME),
                                                    LoadStrW(IDS_FF_NAMED), SearchForData[0]->MasksGroup.GetMasksString());
            SetWindowTextW(HWindow, caption.c_str());
        }

        //      if (FirstWMSize)
        //        FirstWMSize = FALSE;
        //      else
        LayoutControls();
        break;
    }

    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO lpmmi = (LPMINMAXINFO)lParam;
        lpmmi->ptMinTrackSize.x = MinDlgW;
        lpmmi->ptMinTrackSize.y = MinDlgH;
        break;
    }

    case WM_HELP:
    {
        PostMessage(HWindow, WM_COMMAND, CM_HELP_CONTENTS, 0);
        return TRUE;
    }

    case WM_COMMAND:
    {
        if (FoundFilesListView != NULL && ListView_GetEditControl(FoundFilesListView->HWindow) != NULL)
            return 0; // the list view sends some commands while editing
        // The selection-sync block that stood here copied the wide item
        // chosen in the drop-down into the replacement combo's edit, because the list and
        // the edit lived in two different controls. The native combo puts its own
        // selection into its own edit; there is nothing to synchronise.
        if (LOWORD(wParam) >= CM_FIND_OPTIONS_FIRST && LOWORD(wParam) <= CM_FIND_OPTIONS_LAST)
        {
            LoadControls(LOWORD(wParam) - CM_FIND_OPTIONS_FIRST);
            return TRUE;
        }

        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == IDC_FIND_GREP)
        {
            Configuration.SearchFileContent = IsDlgButtonChecked(HWindow, IDC_FIND_GREP);
            SetContentVisible(Configuration.SearchFileContent);
            if (!Configuration.SearchFileContent)
            {
                // grab the actual content of hidden elements
                SetDlgItemTextW(HWindow, IDC_FIND_CONTAINING, L"");
                CheckDlgButton(HWindow, IDC_FIND_HEX, FALSE);
                CheckDlgButton(HWindow, IDC_FIND_CASE, FALSE);
                CheckDlgButton(HWindow, IDC_FIND_WHOLE, FALSE);
                CheckDlgButton(HWindow, IDC_FIND_REGULAR, FALSE);
                Data.HexMode = FALSE;
                Data.RegularExpresions = FALSE;
            }
            return TRUE;
        }

        switch (LOWORD(wParam))
        {
        case CM_FIND_INTERSECT:
        case CM_FIND_SUBTRACT:
        case CM_FIND_APPEND:
        {
            DWORD totalCount = ListView_GetItemCount(FoundFilesListView->HWindow);
            if (!SearchInProgress && totalCount > 0)
            {
                if (ValidateData() && TransferData(ttDataFromWindow))
                    StartSearch(LOWORD(wParam));
            }
            return 0;
        }

        case CM_FIND_NOW:
        case CM_FIND_DUPLICATES:
        {
            if (!SearchInProgress)
            {
                if (ValidateData() && TransferData(ttDataFromWindow))
                    StartSearch(LOWORD(wParam));
            }
            return 0;
        }

        case IDOK:
        {
            if (SearchInProgress) // is this a stop request?
            {
                if (Configuration.MinBeepWhenDone && GetForegroundWindow() != HWindow)
                    MessageBeep(0);
                StopSearch();
                return TRUE;
            }
            else // no, it is the start
            {
                if (!ValidateData() || !TransferData(ttDataFromWindow))
                    return TRUE;
                StartSearch(LOWORD(wParam));
                return TRUE;
            }
        }

        case IDCANCEL:
        {
            if (!CanCloseWindow())
                return TRUE;
            if (SearchInProgress)
            {
                if (!DoYouWantToStopSearching())
                    return TRUE;

                if (SearchInProgress)
                    StopSearch();

                if (ProcessingEscape)
                    return TRUE;
                else
                    break;
            }
            else
            {
                if (ProcessingEscape && Configuration.CnfrmCloseFind)
                {
                    bool dontShow = !Configuration.CnfrmCloseFind;
                    PromptResult res = gPrompter->AskYesNoWithCheckbox(LoadStrW(IDS_WANTTOSTOPTITLE), LoadStrW(IDS_WANTTOCLOSEFIND),
                                                                       LoadStrW(IDS_DONTSHOWAGAINCF), &dontShow);
                    Configuration.CnfrmCloseFind = !dontShow;
                    if (res.type != PromptResult::kYes)
                        return 0;
                }
            }
            break;
        }

        case IDC_FIND_STOP:
        {
            if (SearchInProgress)
            {
                if (Configuration.MinBeepWhenDone && GetForegroundWindow() != HWindow)
                    MessageBeep(0);
                StopSearch();
                return TRUE;
            }
            break;
        }

        case IDC_FIND_HEX:
        {
            if (HIWORD(wParam) == BN_CLICKED)
            {
                Data.HexMode = (IsDlgButtonChecked(HWindow, IDC_FIND_HEX) != BST_UNCHECKED);
                if (Data.HexMode)
                    CheckDlgButton(HWindow, IDC_FIND_CASE, BST_CHECKED);
                return TRUE;
            }
            break;
        }

        case IDC_FIND_REGEXP_BROWSE:
        {
            const CExecuteItem* item = TrackExecuteMenu(HWindow, IDC_FIND_REGEXP_BROWSE,
                                                        IDC_FIND_CONTAINING, TRUE,
                                                        RegularExpressionItems);
            if (item != NULL)
            {
                BOOL regular = (IsDlgButtonChecked(HWindow, IDC_FIND_REGULAR) == BST_CHECKED);
                if (item->Keyword == EXECUTE_HELP)
                {
                    // open the help page dedicated to regular expressions
                    OpenHtmlHelp(NULL, HWindow, HHCDisplayContext, IDH_REGEXP, FALSE);
                }
                if (item->Keyword != EXECUTE_HELP && !regular)
                {
                    // the user chose a pattern -> check the checkbox for regular search
                    CheckDlgButton(HWindow, IDC_FIND_REGULAR, BST_CHECKED);
                    PostMessage(HWindow, WM_COMMAND, MAKELPARAM(IDC_FIND_REGULAR, BN_CLICKED), 0);
                }
            }
            return 0;
        }

        case IDC_FIND_REGULAR:
        {
            if (HIWORD(wParam) == BN_CLICKED)
            {
                Data.RegularExpresions = (IsDlgButtonChecked(HWindow, IDC_FIND_REGULAR) != BST_UNCHECKED);
                if (Data.RegularExpresions)
                {
                    Data.HexMode = FALSE;
                    CheckDlgButton(HWindow, IDC_FIND_HEX, FALSE);
                }
                EnableControls();
                return TRUE;
            }
            break;
        }

            /*
        case IDC_FIND_INCLUDE_ARCHIVES:
        {
          if (HIWORD(wParam) == BN_CLICKED)
          {
            EnableControls();
            return TRUE;
          }
          break;
        }
*/

        case IDC_FIND_CONTAINING:
        {
            if (!Data.RegularExpresions && Data.HexMode && HIWORD(wParam) == CBN_EDITUPDATE)
            {
                DoHexValidation((HWND)lParam);
                return TRUE;
            }
            break;
        }

        case IDC_FIND_ADVANCED:
        {
            CFilterCriteriaDialog dlg(HWindow, &Data.Criteria, TRUE);
            if (dlg.Execute() == IDOK)
                UpdateAdvancedText();
            return TRUE;
        }

        case CM_FIND_ADD_CURRENT:
        {
            CFindOptionsItem* item = new CFindOptionsItem();
            if (item != NULL)
            {
                TransferData(ttDataFromWindow);
                *item = Data;
                item->BuildItemName();
                if (!FindOptions.Add(item))
                    delete item;
            }
            else
                TRACE_E(LOW_MEMORY);

            return TRUE;
        }

        case CM_FIND_MANAGE:
        {
            if (FindManageInUse)
                return 0;
            FindManageInUse = TRUE;
            TransferData(ttDataFromWindow);
            CFindManageDialog dlg(HWindow, &Data);
            if (dlg.IsGood())
                dlg.Execute();
            FindManageInUse = FALSE;
            return 0;
        }

        case CM_FIND_IGNORE:
        {
            if (FindIgnoreInUse)
                return 0;
            FindIgnoreInUse = TRUE;
            TransferData(ttDataFromWindow);
            CFindIgnoreDialog dlg(HWindow, &FindIgnore);
            if (dlg.IsGood())
                dlg.Execute();
            FindIgnoreInUse = FALSE;
            return 0;
        }

        case IDC_FIND_LOOKIN_BROWSE:
        {
            RECT r;
            GetWindowRect(GetDlgItem(HWindow, IDC_FIND_LOOKIN_BROWSE), &r);
            POINT p;
            p.x = r.right;
            p.y = r.top;

            CMenuPopup menu;
            MENU_ITEM_INFO mii;
            mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID;
            mii.Type = MENU_TYPE_STRING;

            /* used by the export_mnu.py script which generates salmenu.mnu for the Translator
   keep synchronized with the InsertItem() call below...
MENU_TEMPLATE_ITEM FindLookInBrowseMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_FF_BROWSE
  {MNTT_IT, IDS_FF_LOCALDRIVES
  {MNTT_IT, IDS_FF_ALLDRIVES
  {MNTT_PE, 0
};
*/
            int ids[] = {IDS_FF_BROWSE, -1, IDS_FF_LOCALDRIVES, IDS_FF_ALLDRIVES, 0};
            int i;
            for (i = 0; ids[i] != 0; i++)
            {
                if (ids[i] == -1)
                    mii.Type = MENU_TYPE_SEPARATOR;
                else
                {
                    mii.Type = MENU_TYPE_STRING;
                    mii.String = LoadStrW(ids[i]);
                    mii.ID = i + 1;
                }
                menu.InsertItem(-1, TRUE, &mii);
            }

            DWORD cmd = menu.Track(MENU_TRACK_VERTICAL | MENU_TRACK_RETURNCMD, p.x, p.y, HWindow, &r);

            if (cmd != 0)
            {
                if (cmd == 1)
                {
                    // Browse...
                    HWND hCombo = GetDlgItem(HWindow, IDC_FIND_LOOKIN);
                    std::wstring current = GetWindowTextWide(hCombo);
                    DWORD start = 0;
                    DWORD end = 0;
                    SendMessage(hCombo, CB_GETEDITSEL, (WPARAM)&start, (LPARAM)&end);

                    std::wstring pathW;
                    size_t startPos = start;
                    size_t endPos = end;
                    if (startPos > current.length())
                        startPos = current.length();
                    if (endPos > current.length())
                        endPos = current.length();
                    if (endPos > startPos)
                        pathW.assign(current, startPos, endPos - startPos);

                    if (GetTargetDirectoryW(HWindow, HWindow, LoadStrW(IDS_CHANGE_DIRECTORY),
                                            LoadStrW(IDS_BROWSECHANGEDIRTEXT), pathW, FALSE, pathW.c_str()))
                        ReplaceFindLookInSelectionW(hCombo, pathW, start, end);
                    return TRUE;
                }
                if (cmd == 3 || cmd == 4)
                {
                    InsertDrives(GetDlgItem(HWindow, IDC_FIND_LOOKIN),
                                 cmd == 4); // local drives (3) || all drives (4)
                }
            }
            return 0;
        }

        case CM_FIND_NAME:
        {
            FoundFilesListView->SortItems(0);
            return TRUE;
        }

        case CM_FIND_PATH:
        {
            FoundFilesListView->SortItems(1);
            return TRUE;
        }

        case CM_FIND_TIME:
        {
            FoundFilesListView->SortItems(3);
            return TRUE;
        }

        case CM_FIND_SIZE:
        {
            FoundFilesListView->SortItems(2);
            return TRUE;
        }

        case CM_FIND_OPEN:
        {
            OnOpen(TRUE);
            return TRUE;
        }

        case CM_FIND_OPENSEL:
        {
            OnOpen(FALSE);
            return TRUE;
        }

        case CM_FIND_FOCUS:
        {
            OnFocusFile();
            return TRUE;
        }

        case CM_FIND_VIEW:
        {
            OnViewFile(FALSE);
            return TRUE;
        }

        case CM_FIND_VIEW_WITH:
        {
            OnViewFileWith();
            return TRUE;
        }

        case CM_FIND_ALTVIEW:
        {
            OnViewFile(TRUE);
            return TRUE;
        }

        case CM_FIND_EDIT:
        {
            OnEditFile();
            return TRUE;
        }

        case CM_FIND_EDIT_WITH:
        {
            OnEditFileWith();
            return TRUE;
        }

        case CM_FIND_USERMENU:
        {
            OnUserMenu();
            return TRUE;
        }

        case CM_FIND_PROPERTIES:
        {
            OnProperties();
            return TRUE;
        }

        case CM_FIND_HIDESEL:
        {
            OnHideSelection();
            return TRUE;
        }

        case CM_FIND_HIDE_DUP:
        {
            OnHideDuplicateNames();
            return TRUE;
        }

        case CM_FIND_SAVE_RESULTS:
        {
            OnSaveResults();
            return TRUE;
        }

        case CM_FIND_LOAD_RESULTS:
        {
            OnLoadResults();
            return TRUE;
        }

        case CM_FIND_DELETE:
        {
            OnDelete((GetKeyState(VK_SHIFT) & 0x8000) == 0);
            return TRUE;
        }

        case CM_FIND_CLIPCUT:
        {
            OnCutOrCopy(TRUE);
            return TRUE;
        }

        case CM_FIND_CLIPCOPY:
        {
            OnCutOrCopy(FALSE);
            return TRUE;
        }

        case CM_FIND_CLIPCOPYFULLNAME:
        {
            OnCopyNameToClipboard(cntcmFullName);
            return TRUE;
        }

        case CM_FIND_CLIPCOPYNAME:
        {
            OnCopyNameToClipboard(cntcmName);
            return TRUE;
        }

        case CM_FIND_CLIPCOPYFULLPATH:
        {
            OnCopyNameToClipboard(cntcmFullPath);
            return TRUE;
        }

        case CM_FIND_CLIPCOPYUNCNAME:
        {
            OnCopyNameToClipboard(cntcmUNCName);
            return TRUE;
        }

        case CM_FIND_SELECTALL:
        {
            OnSelectAll();
            return TRUE;
        }

        case CM_FIND_INVERTSEL:
        {
            OnInvertSelection();
            return TRUE;
        }

        case CM_FIND_SHOWERRORS:
        {
            Configuration.ShowGrepErrors = !Configuration.ShowGrepErrors;
            return TRUE;
        }

        case CM_FIND_FULLROWSEL:
        {
            SetFullRowSelect(!Configuration.FindFullRowSelect);
            return TRUE;
        }

        case CM_FIND_MESSAGES:
        {
            if (TBHeader != NULL)
                TBHeader->StopFlashIcon();
            OnShowLog();
            return TRUE;
        }

        case CM_HELP_CONTENTS:
        case CM_HELP_INDEX:
        case CM_HELP_SEARCH:
        {
            CHtmlHelpCommand command;
            DWORD_PTR dwData = 0;
            switch (LOWORD(wParam))
            {
            case CM_HELP_INDEX:
            {
                command = HHCDisplayIndex;
                break;
            }

            case CM_HELP_SEARCH:
            {
                command = HHCDisplaySearch;
                break;
            }

            case CM_HELP_CONTENTS:
            {
                OpenHtmlHelp(NULL, HWindow, HHCDisplayTOC, 0, TRUE); // avoid two message boxes in a row
                command = HHCDisplayContext;
                dwData = IDD_FIND;
                break;
            }
            }

            OpenHtmlHelp(NULL, HWindow, command, dwData, FALSE);

            return 0;
        }
        }
        break;
    }

    case WM_INITMENUPOPUP:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_MENUCHAR:
    {
        if (uMsg == WM_DRAWITEM && wParam == IDC_FIND_STATUS)
        {
            DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lParam;
            DarkModeColors colors;
            BOOL useDark = DarkMode_GetColors(&colors);
            COLORREF background = useDark ? colors.DialogBackground : GetSysColor(COLOR_3DFACE);
            COLORREF foreground = useDark ? colors.DialogText : GetSysColor(COLOR_BTNTEXT);
            FindFillRectSolid(di->hDC, &di->rcItem, background);
            COLORREF prevTextColor = SetTextColor(di->hDC, foreground);
            int prevBkMode = SetBkMode(di->hDC, TRANSPARENT);
            std::wstring text = SearchingText.GetWString();
            DrawTextW(di->hDC, text.c_str(), -1, &di->rcItem, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_PATH_ELLIPSIS);
            SetBkMode(di->hDC, prevBkMode);
            SetTextColor(di->hDC, prevTextColor);
            return TRUE;
        }

        if (ContextMenu != NULL)
        {
            IContextMenu3* contextMenu3 = NULL;
            LRESULT lResult = 0;
            if (uMsg == WM_MENUCHAR)
            {
                if (SUCCEEDED(ContextMenu->QueryInterface(IID_IContextMenu3, (void**)&contextMenu3)))
                {
                    contextMenu3->HandleMenuMsg2(uMsg, wParam, lParam, &lResult);
                    contextMenu3->Release();
                    return (BOOL)lResult;
                }
            }
            if (ContextMenu->HandleMenuMsg(uMsg, wParam, lParam) == NOERROR)
            {
                if (uMsg == WM_INITMENUPOPUP) // ensure the return value is correct
                    return 0;
                else
                    return TRUE;
            }
        }
        break;
    }

    case WM_SYSCOMMAND:
    {
        if (SkipCharacter) // suppress the beep on Alt+Enter
        {
            SkipCharacter = FALSE;
            return TRUE; // MSDN says we should return 0, but that beeps, so I am not sure
        }
        break;
    }

    case WM_NOTIFY:
    {
        if (wParam == IDC_FIND_STATUS && ((LPNMHDR)lParam)->code == NM_CUSTOMDRAW)
        {
            LPNMCUSTOMDRAW cd = (LPNMCUSTOMDRAW)lParam;
            DarkModeColors colors;
            if (cd->dwDrawStage == CDDS_PREPAINT && DarkMode_GetColors(&colors))
            {
                SetTextColor(cd->hdc, colors.DialogText);
                SetBkColor(cd->hdc, colors.DialogBackground);
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, CDRF_DODEFAULT);
                return TRUE;
            }
        }

        if (wParam == IDC_FIND_RESULTS)
        {
            switch (((LPNMHDR)lParam)->code)
            {
            case NM_DBLCLK:
            {
                if (((LPNMITEMACTIVATE)lParam)->iItem >= 0) // double-click outside items does nothing
                    OnOpen(TRUE);
                break;
            }

            case NM_RCLICK:
            {
                int clickedIndex = ((LPNMITEMACTIVATE)lParam)->iItem;
                if (clickedIndex >= 0) // right-click outside the item won't show the menu
                {
                    BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
                    BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

                    // when clicking outside the selection while holding Shift (Alt+Ctrl doesn't matter) or
                    // holding only Alt, the selection changes to the clicked item before the menu is opened
                    HWND hListView = FoundFilesListView->HWindow;
                    if ((shiftPressed || altPressed && !controlPressed) &&
                        (ListView_GetItemState(hListView, clickedIndex, LVIS_SELECTED) & LVIS_SELECTED) == 0)
                    {
                        ListView_SetItemState(hListView, -1, 0, LVIS_SELECTED | LVIS_FOCUSED); // -1: all items
                        ListView_SetItemState(hListView, clickedIndex, LVIS_SELECTED | LVIS_FOCUSED, 0x000F);
                    }

                    DWORD pos = GetMessagePos();
                    OnContextMenu(GET_X_LPARAM(pos), GET_Y_LPARAM(pos));
                }
                break;
            }

            case NM_CUSTOMDRAW:
            {
                LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lParam;

                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
                {
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                    return TRUE;
                }

                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    // Ask for subitem notifications: the Name/icon column stays
                    // native, while the Path column gets upstream-style compaction.
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, CDRF_NOTIFYSUBITEMDRAW);
                    return TRUE;
                }

                if (cd->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM))
                {
                    int itemIndex = (int)cd->nmcd.dwItemSpec;
                    CFoundFilesData* item = FoundFilesListView->At(itemIndex);
                    if (item == NULL)
                    {
                        SetWindowLongPtr(HWindow, DWLP_MSGRESULT, CDRF_DODEFAULT);
                        return TRUE;
                    }

                    BOOL selected = (ListView_GetItemState(FoundFilesListView->HWindow, itemIndex, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                    BOOL duplicate = GrepData.FindDuplicates && item->Different == 1;

                    // Draw only the Path column ourselves, just like upstream.
                    // This keeps the Name/icon column's focus and selection UX native.
                    if (cd->iSubItem == 1)
                    {
                        HDC hDC = cd->nmcd.hdc;

                        if (CacheBitmap == NULL)
                        {
                            CacheBitmap = new CBitmap();
                            if (CacheBitmap != NULL && !CacheBitmap->CreateBmp(hDC, 1, 1))
                            {
                                delete CacheBitmap;
                                CacheBitmap = NULL;
                            }
                        }
                        if (CacheBitmap == NULL)
                        {
                            SetWindowLongPtr(HWindow, DWLP_MSGRESULT, CDRF_DODEFAULT);
                            return TRUE;
                        }

                        RECT r;
                        if (!ListView_GetSubItemRect(FoundFilesListView->HWindow, itemIndex, cd->iSubItem, LVIR_BOUNDS, &r))
                        {
                            SetWindowLongPtr(HWindow, DWLP_MSGRESULT, CDRF_DODEFAULT);
                            return TRUE;
                        }

                        RECT r2;
                        r2.left = 0;
                        r2.top = 0;
                        r2.right = r.right - r.left;
                        r2.bottom = r.bottom - r.top;

                        if (CacheBitmap->NeedEnlarge(r2.right, r2.bottom) && !CacheBitmap->Enlarge(r2.right, r2.bottom))
                        {
                            SetWindowLongPtr(HWindow, DWLP_MSGRESULT, CDRF_DODEFAULT);
                            return TRUE;
                        }

                        DarkModeColors colors;
                        BOOL useDark = DarkMode_GetColors(&colors);
                        COLORREF bkColor = useDark ? (duplicate ? colors.InactiveSelection : colors.InputBackground)
                                                   : GetSysColor(duplicate ? COLOR_3DFACE : COLOR_WINDOW);
                        COLORREF textColor = useDark ? colors.InputText : GetSysColor(COLOR_WINDOWTEXT);

                        if (Configuration.FindFullRowSelect && selected)
                        {
                            if (GetFocus() == FoundFilesListView->HWindow)
                            {
                                bkColor = useDark ? colors.Highlight : GetSysColor(COLOR_HIGHLIGHT);
                                textColor = useDark ? colors.HighlightText : GetSysColor(COLOR_HIGHLIGHTTEXT);
                            }
                            else
                            {
                                if (useDark)
                                {
                                    if (colors.InactiveSelection != colors.InputBackground)
                                        bkColor = colors.InactiveSelection;
                                    else
                                    {
                                        bkColor = colors.Highlight;
                                        textColor = colors.HighlightText;
                                    }
                                }
                                else
                                {
                                    if (GetSysColor(COLOR_3DFACE) != GetSysColor(COLOR_WINDOW))
                                        bkColor = GetSysColor(COLOR_3DFACE);
                                    else
                                    {
                                        // high-contrast color schemes may not distinguish 3DFACE
                                        bkColor = GetSysColor(COLOR_HIGHLIGHT);
                                        textColor = GetSysColor(COLOR_HIGHLIGHTTEXT);
                                    }
                                }
                            }
                        }

                        COLORREF oldBkColor = SetBkColor(CacheBitmap->HMemDC, bkColor);
                        int oldBkMode = SetBkMode(CacheBitmap->HMemDC, TRANSPARENT);
                        ExtTextOutW(CacheBitmap->HMemDC, 0, 0, ETO_OPAQUE, &r2, L"", 0, NULL);

                        r2.left += 5;
                        r2.right -= 5;

                        HFONT font = (HFONT)SendMessage(FoundFilesListView->HWindow, WM_GETFONT, 0, 0);
                        HGDIOBJ oldFont = NULL;
                        if (font != NULL)
                            oldFont = SelectObject(CacheBitmap->HMemDC, font);
                        COLORREF oldTextColor = SetTextColor(CacheBitmap->HMemDC, textColor);

                        if (r2.right > r2.left)
                        {
                            std::vector<wchar_t> compactPath = MakeCompactPathBuffer(item->PathW);
                            PathCompactPathW(CacheBitmap->HMemDC, compactPath.data(), (UINT)(r2.right - r2.left));
                            DrawTextW(CacheBitmap->HMemDC, compactPath.data(), -1, &r2,
                                      DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_SINGLELINE);
                        }

                        SetTextColor(CacheBitmap->HMemDC, oldTextColor);
                        if (oldFont != NULL)
                            SelectObject(CacheBitmap->HMemDC, oldFont);
                        SetBkMode(CacheBitmap->HMemDC, oldBkMode);
                        SetBkColor(CacheBitmap->HMemDC, oldBkColor);

                        BitBlt(hDC, r.left, r.top, r.right - r.left, r.bottom - r.top,
                               CacheBitmap->HMemDC, 0, 0, SRCCOPY);

                        SetWindowLongPtr(HWindow, DWLP_MSGRESULT, CDRF_SKIPDEFAULT);
                        return TRUE;
                    }

                    if (duplicate && !selected)
                    {
                        DarkModeColors colors;
                        if (DarkMode_GetColors(&colors))
                        {
                            cd->clrTextBk = colors.InactiveSelection;
                            cd->clrText = colors.InputText;
                        }
                        else
                        {
                            cd->clrTextBk = GetSysColor(COLOR_3DFACE);
                            cd->clrText = GetSysColor(COLOR_WINDOWTEXT);
                        }
                        SetWindowLongPtr(HWindow, DWLP_MSGRESULT, CDRF_NEWFONT);
                        return TRUE;
                    }

                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, CDRF_DODEFAULT);
                    return TRUE;
                }

                break;
            }


            case LVN_ODFINDITEMW:
            {
                NMLVFINDITEMW* pFindInfo = (NMLVFINDITEMW*)lParam;
                int ret = FindListItemByNameW(FoundFilesListView, pFindInfo->iStart,
                                              pFindInfo->lvfi.flags, pFindInfo->lvfi.psz);

                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, ret);
                return TRUE;
            }

            case LVN_COLUMNCLICK:
            {
                int subItem = ((NM_LISTVIEW*)lParam)->iSubItem;
                if (subItem >= 0 && subItem < 5)
                    FoundFilesListView->SortItems(subItem);
                break;
            }


            case LVN_GETDISPINFOW:
            {
                NMLVDISPINFOW* info = (NMLVDISPINFOW*)lParam;
                CFoundFilesData* item = FoundFilesListView->At(info->item.iItem);
                if (info->item.mask & LVIF_IMAGE)
                    info->item.iImage = item->IsDir ? 0 : 1;
                if (info->item.mask & LVIF_TEXT)
                {
                    FoundFilesDataTextBufferW = item->GetTextW(info->item.iSubItem, FileNameFormat);
                    info->item.pszText = const_cast<LPWSTR>(FoundFilesDataTextBufferW.c_str());
                }
                break;
            }

            case LVN_ITEMCHANGED:
            {
                NMLISTVIEW* lv = (NMLISTVIEW*)lParam;
                if (lv->iItem >= 0 &&
                    (lv->uChanged & LVIF_STATE) != 0 &&
                    ((lv->uOldState ^ lv->uNewState) & (LVIS_SELECTED | LVIS_FOCUSED)) != 0)
                {
                    RedrawFindResultItem(FoundFilesListView->HWindow, lv->iItem);
                }

                EnableToolBar();
                if (!IsSearchInProgress())
                    UpdateStatusBar = TRUE; // the text will be set during Idle time
                break;
            }

            case LVN_BEGINDRAG:
            case LVN_BEGINRDRAG:
            {
                OnDrag(((LPNMHDR)lParam)->code == LVN_BEGINRDRAG);
                return 0;
            }

            case LVN_KEYDOWN:
            {
                NMLVKEYDOWN* kd = (NMLVKEYDOWN*)lParam;
                BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
                BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                DWORD cmd = 0;
                switch (kd->wVKey)
                {
                case VK_F3:
                {
                    if (!controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_VIEW;
                    if (!controlPressed && altPressed && !shiftPressed)
                        cmd = CM_FIND_ALTVIEW;
                    if (controlPressed && !altPressed && shiftPressed)
                        cmd = CM_FIND_VIEW_WITH;
                    if (controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_NAME;
                    break;
                }

                case VK_F4:
                {
                    if (!controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_EDIT;
                    if (controlPressed && !altPressed && shiftPressed)
                        cmd = CM_FIND_EDIT_WITH;
                    if (controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_PATH;
                    break;
                }

                case VK_F5:
                {
                    if (controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_TIME;
                    break;
                }

                case VK_F6:
                {
                    if (controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_SIZE;
                    break;
                }

                case VK_DELETE:
                {
                    if (!controlPressed && !altPressed && !shiftPressed ||
                        !controlPressed && !altPressed && shiftPressed)
                        cmd = CM_FIND_DELETE;
                    break;
                }

                case VK_F8:
                {
                    if (!controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_DELETE;
                    break;
                }

                case VK_F9:
                {
                    if (!controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_USERMENU;
                    break;
                }

                case VK_RETURN:
                {
                    if (!controlPressed && !altPressed)
                        OnOpen(!shiftPressed);
                    if (!controlPressed && altPressed && !shiftPressed)
                    {
                        cmd = CM_FIND_PROPERTIES;
                        SkipCharacter = TRUE;
                    }
                    break;
                }

                case VK_SPACE:
                {
                    if (!controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_FOCUS;
                    break;
                }

                case VK_INSERT:
                {
                    if (!controlPressed && altPressed && !shiftPressed)
                        cmd = CM_FIND_CLIPCOPYFULLNAME;
                    if (!controlPressed && altPressed && shiftPressed)
                        cmd = CM_FIND_CLIPCOPYNAME;
                    if (controlPressed && altPressed && !shiftPressed)
                        cmd = CM_FIND_CLIPCOPYFULLPATH;
                    if (controlPressed && !altPressed && shiftPressed)
                        cmd = CM_FIND_CLIPCOPYUNCNAME;
                    break;
                }

                case 'A':
                {
                    if (controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_SELECTALL;
                    break;
                }

                case 'C':
                {
                    if (controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_CLIPCOPY;
                    break;
                }

                case 'X':
                {
                    if (controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_CLIPCUT;
                    break;
                }

                case 'H':
                {
                    if (controlPressed && !altPressed && !shiftPressed)
                        cmd = CM_FIND_HIDESEL;
                    if (controlPressed && !altPressed && shiftPressed)
                        cmd = CM_FIND_HIDE_DUP;
                    break;
                }
                }
                if (cmd != 0)
                    PostMessage(HWindow, WM_COMMAND, cmd, 0);
                return 0;
            }
            }
        }
        break;
    }

    case WM_USER_ADDFILE:
    {
        UpdateListViewItems();
        return 0;
    }

    case WM_USER_ADDLOG:
    {
        // running in the find thread
        FIND_LOG_ITEM* item = (FIND_LOG_ITEM*)wParam;
        Log.Add(item->Flags, item->Text, item->Path);
        return 0;
    }

    case WM_USER_BUTTONS:
    {
        EnableControls(wParam != NULL);
        if (wParam != NULL)
            PostMessage((HWND)wParam, BM_SETSTYLE, BS_DEFPUSHBUTTON, MAKELPARAM(TRUE, 0));
        return 0;
    }

    case WM_USER_CFGCHANGED:
    {
        TBHeader->SetFont();
        return 0;
    }

    case WM_ACTIVATEAPP:
    {
        if (wParam == FALSE) // when deactivated we leave directories shown in panels
        {                    // so they can be deleted, dissconected, etc. by other software
            if (CanChangeDirectory())
                SetCurrentDirectoryToSystem();
        }
        else
        {
            SuppressToolTipOnCurrentMousePos(); // suppress unwanted tooltip when switching to the window
        }
        break;
    }

    case WM_ACTIVATE:
    {
        if (wParam != WA_INACTIVE)
        {
            if (FlashIconsOnActivation)
            {
                if (TBHeader != NULL)
                    TBHeader->StartFlashIcon();
                FlashIconsOnActivation = FALSE;
            }
        }
        break;
    }

    case WM_DESTROY:
    {
        if (SearchInProgress)
            StopSearch();

        if (LookInUnicodeFont != NULL)
        {
            DeleteObject(LookInUnicodeFont);
            LookInUnicodeFont = NULL;
        }

        if (!DlgFailed)
        {
            // store the width of the Name column
            Configuration.FindColNameWidth = ListView_GetColumnWidth(FoundFilesListView->HWindow, 0);
            // store the window placement
            Configuration.FindDialogWindowPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(HWindow, &Configuration.FindDialogWindowPlacement);
        }
        if (FoundFilesListView != NULL)
        {
            // release the handle, otherwise ListView would drag it to hell with itself
            ListView_SetImageList(FoundFilesListView->HWindow, NULL, LVSIL_SMALL);
        }
        if (MenuBar != NULL)
        {
            DestroyWindow(MenuBar->HWindow);
            delete MenuBar;
            MenuBar = NULL;
        }
        if (MainMenu != NULL)
        {
            delete MainMenu;
            MainMenu = NULL;
        }
        if (TBHeader != NULL)
        {
            DestroyWindow(TBHeader->HWindow);
            TBHeader = NULL;
        }
        FindDialogQueue.Remove(HWindow);

        // if the user copies the search results to the clipboard (Ctrl+C), switches to the main window
        // and invokes Paste Shortcut (Ctrl+S) while the shortcuts are being created, the Find window may close.
        // We must wait for Paste to finish in the main window; otherwise it could crash.
        //
        // If Paste Shortcut is used in Explorer (or elsewhere), we have no notification and a crash is still possible.
        //
        // If Ctrl+C is followed by closing the Find window before Paste,
        // we call OleFlushClipboard() within UninitializeOle(), which detaches the data from this thread and no problem occurs.
        // theoretically, we could call OleFlushClipboard() after every Ctrl+C directly here,
        // but we're not sure if something would stop working (not sure how robust the data rendering is),
        // plus OleFlushClipboard() can take a second with 2000 files.
        // Therefore we use this hack:
        while (PasteLinkIsRunning > 0)
        {
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (PasteLinkIsRunning > 0)
                Sleep(50); // active waiting; slow the thread down a bit
        }

        UninitializeOle();

        if (ZeroOnDestroy != NULL)
            *ZeroOnDestroy = NULL;
        PostQuitMessage(0);
        break;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
    {
        OnColorsChange();
        ApplyFindDialogTheme(HWindow, HStatusBar);
        break;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

//****************************************************************************
//
// CFindDialogQueue
//

void CFindDialogQueue::AddToArray(TDirectArray<HWND>& arr)
{
    CS.Enter();
    CWindowQueueItem* item = Head;
    while (item != NULL)
    {
        arr.Add(item->HWindow);
        item = item->Next;
    }
    CS.Leave();
}
