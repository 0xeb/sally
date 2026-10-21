// SPDX-FileCopyrightText: 2009-2023 Milan Kase <manison@manison.cz>
// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <limits>
#include <string>
#include <vector>

namespace sally::automation
{
    inline std::wstring GetListViewItemTextOwned(HWND list, int item)
    {
        size_t capacity = 2;
        const size_t limit = static_cast<size_t>((std::numeric_limits<int>::max)());
        for (;;)
        {
            std::vector<wchar_t> buffer(capacity, L'\0');
            LVITEMW entry = {};
            entry.iSubItem = 0;
            entry.pszText = buffer.data();
            entry.cchTextMax = static_cast<int>(buffer.size());
            const LRESULT copied = SendMessageW(
                list, LVM_GETITEMTEXTW, item, reinterpret_cast<LPARAM>(&entry));
            if (copied <= 0)
                return std::wstring();
            if (copied < static_cast<LRESULT>(buffer.size() - 1))
                return std::wstring(buffer.data(), static_cast<size_t>(copied));
            if (capacity > limit / 2)
                return std::wstring();
            capacity *= 2;
        }
    }

    inline BOOL MoveDirectoryListItem(HWND hwndList, int iDelta)
    {
        const int selected = ListView_GetNextItem(hwndList, -1, LVNI_SELECTED);
        if (selected < 0 || (iDelta != -1 && iDelta != 1))
            return FALSE;
        const int target = selected + iDelta;
        if (target < 0 || target >= ListView_GetItemCount(hwndList))
            return FALSE;

        // Acquire both complete paths before changing either row. State belongs to
        // the moved path too, so the selected/focused item follows the move.
        const std::wstring selectedText = GetListViewItemTextOwned(hwndList, selected);
        const std::wstring targetText = GetListViewItemTextOwned(hwndList, target);
        LVITEMW first = {};
        first.mask = LVIF_STATE;
        first.stateMask = ~0u;
        first.iItem = selected;
        LVITEMW second = first;
        second.iItem = target;
        if (!SendMessageW(hwndList, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&first)) ||
            !SendMessageW(hwndList, LVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&second)))
            return FALSE;

        first.iItem = target;
        second.iItem = selected;
        first.mask = second.mask = LVIF_TEXT | LVIF_STATE;
        first.pszText = const_cast<wchar_t*>(selectedText.c_str());
        second.pszText = const_cast<wchar_t*>(targetText.c_str());
        if (!SendMessageW(hwndList, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&first)))
            return FALSE;
        if (!SendMessageW(hwndList, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&second)))
        {
            // Restore the destination if the second write fails. The original
            // source text is still intact at this point.
            second.iItem = target;
            SendMessageW(hwndList, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&second));
            first.mask = LVIF_STATE;
            first.iItem = selected;
            SendMessageW(hwndList, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&first));
            return FALSE;
        }
        return TRUE;
    }
} // namespace sally::automation
