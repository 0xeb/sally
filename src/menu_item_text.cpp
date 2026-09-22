// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_MENU_ITEM_TEXT_STANDALONE
#include "precomp.h"
#endif

#include "menu_item_text.h"

#include <cwchar>

BOOL ReadMenuItemTextW(HMENU hMenu, int item, std::wstring& out, BOOL byPosition)
{
    out.clear();

    // Ask for the length only. With dwTypeData NULL and MIIM_STRING set, cch comes back
    // as the character count excluding the terminator.
    MENUITEMINFOW mii;
    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING | MIIM_FTYPE;
    mii.dwTypeData = NULL;
    mii.cch = 0;
    if (!GetMenuItemInfoW(hMenu, item, byPosition, &mii))
        return FALSE;

    // Separators and bitmap items carry no string; asking for one would be meaningless
    // rather than merely empty, so say so instead of returning an empty success.
    if (mii.fType & (MFT_SEPARATOR | MFT_BITMAP))
        return FALSE;

    if (mii.cch == 0)
        return TRUE;

    // +1 for the terminator GetMenuItemInfoW writes.
    out.resize(mii.cch);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = &out[0];
    mii.cch = (UINT)out.size() + 1;
    if (!GetMenuItemInfoW(hMenu, item, byPosition, &mii))
    {
        out.clear();
        return FALSE;
    }

    // The second call reports what it actually wrote, which can be shorter than the
    // first call promised if the menu changed underneath us.
    out.resize(mii.cch);
    return TRUE;
}

std::wstring DecorateMenuActionTextW(const wchar_t* baseText, const wchar_t* actionText)
{
    std::wstring result = baseText != NULL ? baseText : L"";
    const size_t shortcut = result.rfind(L'\t');
    const size_t insertAt = shortcut == std::wstring::npos ? result.size() : shortcut;
    result.insert(insertAt, L" (");
    result.insert(insertAt + 2, actionText != NULL ? actionText : L"");
    result.insert(insertAt + 2 + (actionText != NULL ? wcslen(actionText) : 0), L")");
    return result;
}

// CharUpperBuffW is the wide sibling of the CharUpper call that built the byte-indexed
// UpperCase table this code used to consult, so case folding stays the one the rest of
// Sally applies rather than the CRT's locale-dependent notion.
static wchar_t UpperW(wchar_t ch)
{
    CharUpperBuffW(&ch, 1);
    return ch;
}

CMenuItemKeyMatch MatchMenuItemKeyW(const wchar_t* text, wchar_t key)
{
    if (text == NULL || *text == 0 || key == 0)
        return mikmNone;

    key = UpperW(key);

    const wchar_t* p = text;
    while (*p != 0)
    {
        if (*p == L'&')
        {
            if (*(p + 1) == L'&') // a literal ampersand, not a marker
            {
                p += 2;
                continue;
            }
            if (*(p + 1) == 0)
                break; // trailing '&' marks nothing
            // The item is marked. It answers to that character and to nothing else -
            // falling back to the first letter here would let one keystroke select two
            // items that look unrelated to the user.
            return UpperW(*(p + 1)) == key ? mikmPrefix : mikmNone;
        }
        p++;
    }

    return UpperW(*text) == key ? mikmFirstChar : mikmNone;
}

void SplitMenuItemColumnsW(const wchar_t* text, BOOL threeCol, CMenuItemColumnsW& out)
{
    out.L1 = NULL;
    out.L1Len = 0;
    out.L2 = NULL;
    out.L2Len = 0;
    out.R = NULL;
    out.RLen = 0;
    if (text == NULL)
        return;

    const wchar_t* iterator = text;
    const wchar_t* begin = text;
    int column = 0;
    while (TRUE)
    {
        if (*iterator == L'\t' || *iterator == 0)
        {
            if (column == 0)
            {
                out.L1 = begin;
                out.L1Len = (int)(iterator - begin);
            }
            else if (column == 1 && threeCol)
            {
                out.L2 = begin;
                out.L2Len = (int)(iterator - begin);
            }
            else
            {
                out.R = begin;
                out.RLen = (int)(iterator - begin);
            }
            if (*iterator == 0)
                break;
            begin = iterator + 1;
            column++;
        }
        iterator++;
    }
}
