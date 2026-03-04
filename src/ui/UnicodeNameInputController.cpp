// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UnicodeNameInputController.h"

#include <vector>

namespace
{
HWND GetComboEditControl(HWND hCombo)
{
    if (hCombo == NULL)
        return NULL;

    COMBOBOXINFO cbi = {};
    cbi.cbSize = sizeof(cbi);
    if (GetComboBoxInfo(hCombo, &cbi))
        return cbi.hwndItem;
    return NULL;
}

bool ContainsNonAscii(const std::wstring& text)
{
    for (size_t i = 0; i < text.size(); i++)
    {
        if ((unsigned int)text[i] > 0x7F)
            return true;
    }
    return false;
}

bool WideHistoryHasNonAscii(wchar_t* historyW[], int historyWCount)
{
    if (historyW == NULL || historyWCount <= 0)
        return false;

    for (int i = 0; i < historyWCount; i++)
    {
        if (historyW[i] != NULL && historyW[i][0] != L'\0' && ContainsNonAscii(historyW[i]))
            return true;
    }
    return false;
}

int AddWideHistoryItems(HWND combo, wchar_t* historyW[], int historyWCount)
{
    if (combo == NULL || historyW == NULL || historyWCount <= 0)
        return 0;

    int inserted = 0;
    for (int i = 0; i < historyWCount; i++)
    {
        if (historyW[i] != NULL && historyW[i][0] != L'\0')
        {
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)historyW[i]);
            inserted++;
        }
    }
    return inserted;
}

void AddAnsiHistoryFallback(HWND legacyCombo, HWND unicodeCombo)
{
    if (legacyCombo == NULL || unicodeCombo == NULL)
        return;

    int count = (int)SendMessage(legacyCombo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++)
    {
        int lenA = (int)SendMessageA(legacyCombo, CB_GETLBTEXTLEN, (WPARAM)i, 0);
        if (lenA <= 0)
            continue;

        std::vector<char> ansi((size_t)lenA + 1, 0);
        if (SendMessageA(legacyCombo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)ansi.data()) == CB_ERR)
            continue;

        int lenW = MultiByteToWideChar(CP_ACP, 0, ansi.data(), -1, NULL, 0);
        if (lenW <= 0)
            continue;

        std::vector<wchar_t> wide((size_t)lenW);
        if (MultiByteToWideChar(CP_ACP, 0, ansi.data(), -1, wide.data(), lenW) <= 0)
            continue;

        SendMessageW(unicodeCombo, CB_ADDSTRING, 0, (LPARAM)wide.data());
    }
}
} // namespace

CUnicodeNameInputController::CUnicodeNameInputController()
{
    HParent = NULL;
    HLegacyCombo = NULL;
    HUnicodeCombo = NULL;
    HOwnedFont = NULL;
    LegacyID = 0;
    ControlID = 0;
}

CUnicodeNameInputController::~CUnicodeNameInputController()
{
    Reset();
}

BOOL CUnicodeNameInputController::EnableForCombo(HWND parent, int controlID, const std::wstring& initialText,
                                                 wchar_t* historyW[], int historyWCount, int maxChars, int selectionEnd)
{
    Reset();

    if (parent == NULL)
        return FALSE;

    HWND hCombo = GetDlgItem(parent, controlID);
    if (hCombo == NULL)
        return FALSE;

    RECT comboRect = {};
    GetWindowRect(hCombo, &comboRect);
    MapWindowPoints(NULL, parent, (LPPOINT)&comboRect, 2);
    RECT droppedRect = {};
    BOOL haveDroppedRect = (BOOL)SendMessage(hCombo, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)&droppedRect);
    if (haveDroppedRect)
        MapWindowPoints(NULL, parent, (LPPOINT)&droppedRect, 2);

    DWORD style = (DWORD)GetWindowLongPtr(hCombo, GWL_STYLE);
    DWORD exStyle = (DWORD)GetWindowLongPtr(hCombo, GWL_EXSTYLE);
    HFONT hFont = (HFONT)SendMessage(hCombo, WM_GETFONT, 0, 0);
    HINSTANCE hInstance = (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE);
    int droppedWidth = (int)SendMessage(hCombo, CB_GETDROPPEDWIDTH, 0, 0);
    int editHeight = (int)SendMessage(hCombo, CB_GETITEMHEIGHT, (WPARAM)-1, 0);
    int listHeight = (int)SendMessage(hCombo, CB_GETITEMHEIGHT, 0, 0);
    BOOL extendedUI = (BOOL)SendMessage(hCombo, CB_GETEXTENDEDUI, 0, 0);

    HParent = parent;
    HLegacyCombo = hCombo;
    ControlID = controlID;
    LegacyID = GetWindowLongPtr(hCombo, GWLP_ID);

    // Keep the original combobox around for legacy dialog transfer flow, but hide it.
    SetWindowLongPtr(hCombo, GWLP_ID, (LONG_PTR)(controlID + 20000));
    ShowWindow(hCombo, SW_HIDE);

    int comboWidth = comboRect.right - comboRect.left;
    int comboHeight = comboRect.bottom - comboRect.top;
    if (haveDroppedRect)
    {
        int droppedHeight = droppedRect.bottom - droppedRect.top;
        if (droppedHeight > comboHeight)
            comboHeight = droppedHeight;
    }

    HUnicodeCombo = CreateWindowExW(
        exStyle, L"COMBOBOX", L"",
        style,
        comboRect.left, comboRect.top,
        comboWidth, comboHeight,
        parent, (HMENU)(INT_PTR)controlID, hInstance, NULL);
    if (HUnicodeCombo == NULL)
    {
        Reset();
        return FALSE;
    }

    HFONT comboFont = hFont;
    if (ContainsNonAscii(initialText) || WideHistoryHasNonAscii(historyW, historyWCount))
    {
        HFONT uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        if (uiFont != NULL)
            comboFont = uiFont;

        // If dialog uses a non-Unicode capable custom font, create a clone with DEFAULT_CHARSET.
        if (hFont != NULL)
        {
            LOGFONTW lf = {};
            if (GetObjectW(hFont, sizeof(lf), &lf) == sizeof(lf))
            {
                lf.lfCharSet = DEFAULT_CHARSET;
                HOwnedFont = CreateFontIndirectW(&lf);
                if (HOwnedFont != NULL)
                    comboFont = HOwnedFont;
            }
        }
    }
    if (comboFont != NULL)
    {
        SendMessage(HUnicodeCombo, WM_SETFONT, (WPARAM)comboFont, TRUE);
        HWND hEdit = GetComboEditControl(HUnicodeCombo);
        if (hEdit != NULL)
            SendMessage(hEdit, WM_SETFONT, (WPARAM)comboFont, TRUE);
    }
    if (droppedWidth > 0)
        SendMessage(HUnicodeCombo, CB_SETDROPPEDWIDTH, (WPARAM)droppedWidth, 0);
    if (editHeight > 0)
        SendMessage(HUnicodeCombo, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)editHeight);
    if (listHeight > 0)
        SendMessage(HUnicodeCombo, CB_SETITEMHEIGHT, 0, (LPARAM)listHeight);
    if (extendedUI)
        SendMessage(HUnicodeCombo, CB_SETEXTENDEDUI, TRUE, 0);
    if (maxChars > 0)
        SendMessage(HUnicodeCombo, CB_LIMITTEXT, (WPARAM)(maxChars - 1), 0);

    SendMessageW(HUnicodeCombo, CB_RESETCONTENT, 0, 0);
    int wideItems = AddWideHistoryItems(HUnicodeCombo, historyW, historyWCount);
    if (wideItems == 0)
        AddAnsiHistoryFallback(hCombo, HUnicodeCombo);

    SetText(initialText);
    if (selectionEnd < 0)
        selectionEnd = (int)initialText.length();
    PostMessage(HUnicodeCombo, CB_SETEDITSEL, 0, MAKELPARAM(0, selectionEnd));
    SetFocus(HUnicodeCombo);

    return TRUE;
}

void CUnicodeNameInputController::Reset()
{
    if (HUnicodeCombo != NULL)
    {
        DestroyWindow(HUnicodeCombo);
        HUnicodeCombo = NULL;
    }
    if (HOwnedFont != NULL)
    {
        DeleteObject(HOwnedFont);
        HOwnedFont = NULL;
    }
    if (HLegacyCombo != NULL)
    {
        SetWindowLongPtr(HLegacyCombo, GWLP_ID, LegacyID);
        ShowWindow(HLegacyCombo, SW_SHOW);
        HLegacyCombo = NULL;
    }
    HParent = NULL;
    LegacyID = 0;
    ControlID = 0;
}

std::wstring CUnicodeNameInputController::GetText() const
{
    if (HUnicodeCombo == NULL)
        return std::wstring();

    int len = GetWindowTextLengthW(HUnicodeCombo);
    if (len <= 0)
        return std::wstring();

    std::vector<wchar_t> buffer(len + 1);
    GetWindowTextW(HUnicodeCombo, buffer.data(), len + 1);
    return std::wstring(buffer.data());
}

void CUnicodeNameInputController::SetText(const std::wstring& text) const
{
    if (HUnicodeCombo != NULL)
        SetWindowTextW(HUnicodeCombo, text.c_str());
}

void CUnicodeNameInputController::SyncSelectionToEdit() const
{
    if (HUnicodeCombo == NULL)
        return;

    int sel = (int)SendMessage(HUnicodeCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR)
        return;

    int len = (int)SendMessageW(HUnicodeCombo, CB_GETLBTEXTLEN, (WPARAM)sel, 0);
    if (len < 0)
        return;

    std::vector<wchar_t> text((size_t)len + 1);
    SendMessageW(HUnicodeCombo, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)text.data());
    SetText(text.data());
    PostMessage(HUnicodeCombo, CB_SETEDITSEL, 0, MAKELPARAM(len, len));
    SetFocus(HUnicodeCombo);
}
