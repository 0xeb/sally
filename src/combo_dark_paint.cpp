// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Deliberately does NOT include precomp.h.
//
// This file is compiled into the core AND into every plugin. Plugins each have their own
// precomp.h, and from src/ the name resolves to Sally's core one instead, which redefines the
// trace classes and breaks every plugin build. Nothing here needs it: the module is plain Win32
// and stands on its own includes, which also lets the headless test compile it directly.
#include "combo_dark_paint.h"

#include <vector>

BOOL ComboDarkGetEditChildRect(HWND combo, RECT* editRectInParent)
{
    if (combo == NULL || editRectInParent == NULL || !IsWindow(combo))
        return FALSE;

    COMBOBOXINFO cbi = {0};
    cbi.cbSize = sizeof(cbi);
    if (!GetComboBoxInfo(combo, &cbi))
        return FALSE;

    HWND item = cbi.hwndItem;
    // NULL or self both mean "no edit child" - see the header for why self is fatal.
    if (item == NULL || item == combo || !IsWindow(item))
        return FALSE;

    if (!GetWindowRect(item, editRectInParent))
        return FALSE;
    MapWindowPoints(NULL, combo, (POINT*)editRectInParent, 2);
    return TRUE;
}

void ComboDarkDrawSelectedItem(HWND combo, HDC dc, const RECT& client, const RECT& dropButton,
                               const ComboDarkColors& colors)
{
    if (combo == NULL || dc == NULL || !IsWindow(combo))
        return;

    LRESULT sel = SendMessage(combo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR)
        return;
    // [merge:main->unicode] Explicitly the W forms, and a wchar_t buffer.
    //
    // This arrived from main as TCHAR / SendMessage / DrawText, which in the real (non-UNICODE)
    // build resolve to the A forms. Sally's dialogs are Unicode windows, so SendMessageA against
    // one makes USER32 down-convert the item text through CP_ACP - and this function exists
    // precisely to draw the selected item of a drop-list combo, i.e. the one place nothing else
    // will redraw it. The result was every non-ANSI entry rendering as "?????", in the core and
    // in all six plugins that compile this file. The length query must match the text query:
    // CB_GETLBTEXTLEN answers in BYTES under A and in CHARACTERS under W, so mixing them
    // under-allocates.
    LRESULT len = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)sel, 0);
    if (len == CB_ERR || len <= 0)
        return;

    std::vector<wchar_t> text((size_t)len + 1, 0);
    if (SendMessageW(combo, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)text.data()) == CB_ERR)
        return;

    RECT item = client;
    item.left += 3;
    item.right = (dropButton.right > dropButton.left) ? dropButton.left - 1 : client.right - 1;
    item.top += 1;
    item.bottom -= 1;
    if (item.right <= item.left || item.bottom <= item.top)
        return;

    const BOOL enabled = IsWindowEnabled(combo);
    const BOOL focused = (GetFocus() == combo);
    if (enabled && focused)
    {
        HBRUSH brush = CreateSolidBrush(colors.Highlight);
        if (brush != NULL)
        {
            FillRect(dc, &item, brush);
            DeleteObject(brush);
        }
    }

    HFONT font = (HFONT)SendMessage(combo, WM_GETFONT, 0, 0);
    HGDIOBJ oldFont = (font != NULL) ? SelectObject(dc, font) : NULL;
    int oldMode = SetBkMode(dc, TRANSPARENT);
    COLORREF oldText = SetTextColor(dc, !enabled  ? colors.DisabledText
                                        : focused ? colors.HighlightText
                                                  : colors.InputText);
    DrawTextW(dc, text.data(), -1, &item,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
    SetTextColor(dc, oldText);
    SetBkMode(dc, oldMode);
    if (oldFont != NULL)
        SelectObject(dc, oldFont);
}
