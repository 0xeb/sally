// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>

namespace filecomp
{
    inline bool CopyFocusedDifferenceText(HWND combo, HWND focus)
    {
        COMBOBOXINFO info{sizeof(info)};
        if (combo == nullptr || !GetComboBoxInfo(combo, &info) ||
            (focus != info.hwndItem && focus != combo))
            return false;

        // A caret without a selection is still owned by this edit. Native
        // WM_COPY leaves the clipboard alone; never fall back to file bytes.
        SendMessageW(info.hwndItem, WM_COPY, 0, 0);
        return true;
    }

    inline int TranslateCopyAwareAccelerator(HWND window, HACCEL accelerators, MSG* message, int differenceListId)
    {
        if (message->message == WM_KEYDOWN &&
            (message->wParam == 'C' || message->wParam == VK_INSERT) &&
            (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
            (GetKeyState(VK_SHIFT) & 0x8000) == 0 &&
            (GetKeyState(VK_MENU) & 0x8000) == 0)
        {
            HWND focus = GetFocus();
            HWND combo = GetParent(focus);
            if (message->hwnd == focus && GetDlgCtrlID(combo) == differenceListId &&
                IsChild(window, combo) && CopyFocusedDifferenceText(combo, focus))
                return TRUE;
        }
        // The file-view Copy menu can be disabled when it has no selection.
        // Handle edit shortcuts first so its state cannot swallow edit copying.
        return TranslateAcceleratorW(window, accelerators, message);
    }
} // namespace filecomp
