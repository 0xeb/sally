// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Wide combo-box helpers, extracted from
// CUnicodeNameInputController when that class was retired.
//
// They live in their own translation unit so a unit test can
// compile them without dragging in the whole viewer-enumeration subsystem - the
// same reason UnicodeHistoryUtils.cpp sits next door. Declarations are in consts.h,
// with the narrow sibling.

#include <windows.h>

void LoadComboFromStdHistoryValues(HWND combo, wchar_t** historyArr, int historyItemsCount)
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    if (historyArr != NULL)
    {
        for (int i = 0; i < historyItemsCount; i++)
        {
            if (historyArr[i] != NULL && historyArr[i][0] != L'\0')
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)historyArr[i]);
        }
    }
}

// Also extracted from CUnicodeNameInputController, and also real
// behaviour rather than workaround: a dialog font carrying a specific lfCharSet
// cannot render CJK, so non-ASCII text comes out as boxes even on a genuinely
// Unicode control. Re-selects the same face with DEFAULT_CHARSET when the text
// needs it. Call AFTER the dialog font has been applied. The returned HFONT, when
// non-NULL, is owned by the caller and must be DeleteObject'd on WM_DESTROY.
HFONT EnsureComboFontCanRenderW(HWND combo, const wchar_t* text)
{
    if (combo == NULL || text == NULL)
        return NULL;

    bool nonAscii = false;
    for (const wchar_t* p = text; *p != L'\0'; p++)
    {
        if ((unsigned int)*p > 0x7F)
        {
            nonAscii = true;
            break;
        }
    }
    if (!nonAscii)
        return NULL;

    HFONT current = (HFONT)SendMessage(combo, WM_GETFONT, 0, 0);
    if (current == NULL)
        return NULL;

    LOGFONTW lf = {};
    if (GetObjectW(current, sizeof(lf), &lf) != sizeof(lf) || lf.lfCharSet == DEFAULT_CHARSET)
        return NULL; // already able to fall back across scripts

    lf.lfCharSet = DEFAULT_CHARSET;
    HFONT replacement = CreateFontIndirectW(&lf);
    if (replacement == NULL)
        return NULL;

    SendMessage(combo, WM_SETFONT, (WPARAM)replacement, TRUE);
    COMBOBOXINFO cbi = {sizeof(COMBOBOXINFO)};
    if (GetComboBoxInfo(combo, &cbi) && cbi.hwndItem != NULL)
        SendMessage(cbi.hwndItem, WM_SETFONT, (WPARAM)replacement, TRUE);
    return replacement;
}
