// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <string>

// Menu item text: reading it back out of an HMENU, and splitting it into the columns
// CMenuPopup lays out.
//
// Why this is its own file: CMenuPopup::LoadFromHandle() is how a menu built by someone
// else - the shell's context menu (shellsup.cpp), the shell's New menu, the drive bar,
// the Find results list - becomes something Sally owns and draws. That readback used the
// ANSI GetMenuItemInfo into a char[2048], so every character outside the active code page
// became '?' before Sally ever saw it. Extract to "D:\<CJK>\" arrived as Extract to
// "D:\???\", and a Send To list of Unicode targets collapsed into rows of identical
// question marks. Nothing downstream could recover it, because the loss happened at the
// point of entry.
//
// Neither function touches Salamander state or a window, so both are testable headlessly:
// a test builds an HMENU with InsertMenuItemW and asserts the text survives the round
// trip. That is the whole contract, and it is exactly what the ANSI readback failed.

// Reads item 'position' (by position, not by command id) out of 'hMenu' as wide text.
//
// Asks the OS for the length first and sizes the buffer to match, rather than reading
// into a fixed buffer and silently truncating; shell extensions are free to produce
// arbitrarily long entries and some Extract-to verbs embed the full target path.
//
// Returns FALSE if the item has no string (a separator or a bitmap item), leaving 'out'
// empty. A string item whose text is empty returns TRUE with an empty 'out'.
BOOL ReadMenuItemTextW(HMENU hMenu, int item, std::wstring& out, BOOL byPosition = TRUE);

// Adds a parenthesized action before an optional tab-separated shortcut column.
std::wstring DecorateMenuActionTextW(const wchar_t* baseText, const wchar_t* actionText);

// Spans of 'text' that CMenuPopup draws as separate columns, tab-separated in the source
// string. Each pointer aims into the caller's buffer and is NOT null-terminated - the
// length is authoritative. A column that is not present is NULL with length 0.
struct CMenuItemColumnsW
{
    const wchar_t* L1;
    int L1Len;
    const wchar_t* L2;
    int L2Len;
    const wchar_t* R;
    int RLen;
};

// Splits 'text' on tabs into up to three columns.
//
// With threeCol, the layout is  left \t middle \t right ; without it,  left \t right .
// Past that the right column is reassigned by each further tab-separated run, so the
// last one wins - the ANSI original behaved the same way and menus in practice do not
// carry the extra tabs that would make the difference observable.
void SplitMenuItemColumnsW(const wchar_t* text, BOOL threeCol, CMenuItemColumnsW& out);

// How a menu item answers to a keystroke during first-letter navigation.
enum CMenuItemKeyMatch
{
    mikmNone,      // does not answer to this key
    mikmPrefix,    // '&' marks exactly this key - the strong match
    mikmFirstChar, // no '&' marker anywhere, and the text begins with this key
};

// Decides whether 'text' answers to 'key'.
//
// An item carrying a '&' marker answers only to the character it marks: a marked item
// that does not match is out, rather than falling back to its first letter. That is what
// keeps Alt+F from also selecting "Find" when "&File" exists. Doubled '&&' is a literal
// ampersand and is skipped. Both sides are uppercased, so matching is case-insensitive.
//
// Comparing wide characters is the point. The narrow form indexed a CP_ACP table, so
// every item whose text did not fit the code page had already become a run of '?' -
// they all answered to the same keystroke, and typing a letter jumped to whichever one
// happened to come first. It also meant a menu entry in a script the code page does not
// cover could not be reached by typing at all.
CMenuItemKeyMatch MatchMenuItemKeyW(const wchar_t* text, wchar_t key);
