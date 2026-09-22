// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <commctrl.h>

//
// Explicit wide common-control helpers.
//
// This build does not define UNICODE/_UNICODE, so <commctrl.h>'s ListView_*
// macros resolve to the A form. Handing one of them a wchar_t buffer is the
// quiet failure: LVITEM::pszText is LPSTR there, so either the compiler
// narrows the call for you or - worse, once a cast is in the way - the control
// reads UTF-16 bytes as ANSI and renders mojibake. Neither is reported.
//
// These spellings existed four times over as file-local macros
// (dialogs_tip_of_day, dialogs_viewer_editor_masks, and the dbviewer/pictview
// plugin dialogs). The core copies now share this header; the two plugin
// copies stay put because plugins do not include core headers.
//
// These become redundant when UNICODE is defined tree-wide - at that
// point the stock macros are already the W form and this header can go.
//

#define ListView_SetItemTextW(hwndLV, i, iSubItem_, pszText_)                    \
    {                                                                            \
        LV_ITEMW _ms_lvi;                                                        \
        _ms_lvi.iSubItem = iSubItem_;                                            \
        _ms_lvi.pszText = pszText_;                                              \
        SNDMSG((hwndLV), LVM_SETITEMTEXTW, (WPARAM)(i), (LPARAM)(LV_ITEM*)&_ms_lvi); \
    }

#define ListView_InsertItemW(hwndLV, pitem) \
    ((int)SNDMSG((hwndLV), LVM_INSERTITEMW, 0, (LPARAM)(const LV_ITEMW*)(pitem)))

#define ListView_GetItemW(hwndLV, pitem) \
    ((BOOL)SNDMSG((hwndLV), LVM_GETITEMW, 0, (LPARAM)(LV_ITEMW*)(pitem)))

#define ListView_InsertColumnW(hwndLV, iCol, pcolW) \
    ((int)SNDMSG((hwndLV), LVM_INSERTCOLUMNW, (WPARAM)(int)(iCol), (LPARAM)(const LVCOLUMNW*)(pcolW)))

#define ListView_GetItemTextW(hwndLV, i, iSubItem_, pszText_, cchTextMax_) \
    {                                                                      \
        LV_ITEMW _ms_lvi;                                                  \
        _ms_lvi.iSubItem = iSubItem_;                                      \
        _ms_lvi.cchTextMax = cchTextMax_;                                  \
        _ms_lvi.pszText = pszText_;                                        \
        SNDMSG((hwndLV), LVM_GETITEMTEXTW, (WPARAM)(i), (LPARAM)(LV_ITEM*)&_ms_lvi); \
    }
