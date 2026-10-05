// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Refills a history combo box (New name, Search for, Replace with) and leaves 'text' in its
// edit field. CB_RESETCONTENT also clears the edit text, so the text must be written back
// after the list is rebuilt - never read from the control at this point (#117: reading it
// returned "" and Batch Rename computed an empty name for every file).
inline void RefillRenamerHistoryCombo(HWND combo, const std::vector<std::wstring>& items, const std::wstring& text)
{
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& item : items)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    SetWindowTextW(combo, text.c_str());
}
