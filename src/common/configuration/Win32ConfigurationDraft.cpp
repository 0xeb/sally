// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "Win32ConfigurationDraft.h"
#include "../ColumnWidthInput.h"
#include <commctrl.h>

namespace sally::configuration
{
    namespace
    {
        std::wstring WindowText(HWND window)
        {
            // Controls belong to this UI thread; still retry if a subclass grows
            // its text between WM_GETTEXTLENGTH and WM_GETTEXT.
            std::wstring text;
            for (;;)
            {
                const int length = GetWindowTextLengthW(window);
                text.resize(static_cast<std::size_t>(length) + 1);
                const int copied = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
                if (copied < length || GetWindowTextLengthW(window) <= copied)
                {
                    text.resize(copied);
                    return text;
                }
            }
        }

        struct CaptureContext
        {
            DraftState& Output;
            bool ModelPage;
        };
    } // namespace

    void CaptureInvalidColumnWidthControls(HWND page, DraftState& output, int leftControl, int rightControl)
    {
        for (int id : {leftControl, rightControl})
        {
            HWND control = GetDlgItem(page, id);
            if (control == NULL || !IsWindowEnabled(control))
                continue;
            const std::wstring text = WindowText(control);
            unsigned width;
            if (!TryParseColumnWidth(text, width))
                output.Append(id, text);
        }
    }

    void CaptureDraftFont(const LOGFONTW& font, DraftState& output)
    {
        output.Add(font.lfHeight);
        output.Add(font.lfWidth);
        output.Add(font.lfEscapement);
        output.Add(font.lfOrientation);
        output.Add(font.lfWeight);
        output.Add(font.lfItalic);
        output.Add(font.lfUnderline);
        output.Add(font.lfStrikeOut);
        output.Add(font.lfCharSet);
        output.Add(font.lfOutPrecision);
        output.Add(font.lfClipPrecision);
        output.Add(font.lfQuality);
        output.Add(font.lfPitchAndFamily);
        output.Add(std::wstring(font.lfFaceName, wcsnlen_s(font.lfFaceName, LF_FACESIZE)));
    }

    void CaptureDraftControls(HWND page, DraftState& output, bool modelPage)
    {
        CaptureContext context{output, modelPage};
        EnumChildWindows(page, [](HWND window, LPARAM parameter) -> BOOL
                         {
            auto& context = *reinterpret_cast<CaptureContext*>(parameter);
            wchar_t name[32] = {};
            GetClassNameW(window, name, ARRAYSIZE(name));
            auto& state = context.Output;
            const int id = GetDlgCtrlID(window);
            if (_wcsicmp(name, L"Edit") == 0)
            {
                wchar_t parentClass[32] = {};
                GetClassNameW(GetParent(window), parentClass, ARRAYSIZE(parentClass));
                const bool labelEdit = _wcsicmp(parentClass, L"ListBox") == 0 || _wcsicmp(parentClass, WC_LISTVIEWW) == 0;
                if (!context.ModelPage || labelEdit)
                {
                    state.Add(id);
                    state.Add(WindowText(window));
                }
            }
            else if (!context.ModelPage && _wcsicmp(name, L"Button") == 0)
            {
                const auto type = GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK;
                if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE ||
                    type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON)
                {
                    state.Add(id);
                    state.Add(SendMessageW(window, BM_GETCHECK, 0, 0));
                }
            }
            else if (!context.ModelPage && _wcsicmp(name, L"ComboBox") == 0)
            {
                state.Add(id);
                state.Add(SendMessageW(window, CB_GETCURSEL, 0, 0));
                state.Add(WindowText(window));
            }
            else if (!context.ModelPage && _wcsicmp(name, WC_LISTVIEWW) == 0)
            {
                state.Add(id);
                const int count = ListView_GetItemCount(window);
                state.Add(count);
                for (int row = 0; row < count; ++row)
                    state.Add(ListView_GetItemState(window, row, LVIS_STATEIMAGEMASK));
            }
            else if (!context.ModelPage && _wcsicmp(name, WC_TREEVIEWW) == 0)
            {
                state.Add(id);
                std::vector<HTREEITEM> pending;
                HTREEITEM item = TreeView_GetRoot(window);
                while (item != nullptr)
                {
                    TVITEMW value = {};
                    value.mask = TVIF_IMAGE | TVIF_STATE;
                    value.hItem = item;
                    value.stateMask = TVIS_STATEIMAGEMASK;
                    SendMessageW(window, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&value));
                    state.Add(value.iImage);
                    state.Add(value.state);
                    const HTREEITEM next = TreeView_GetNextSibling(window, item);
                    const HTREEITEM child = TreeView_GetChild(window, item);
                    if (child != nullptr)
                    {
                        if (next != nullptr) pending.push_back(next);
                        item = child;
                    }
                    else if (next != nullptr)
                        item = next;
                    else if (!pending.empty())
                    {
                        item = pending.back();
                        pending.pop_back();
                    }
                    else
                        item = nullptr;
                }
            }
            return TRUE; }, reinterpret_cast<LPARAM>(&context));
    }
} // namespace sally::configuration
