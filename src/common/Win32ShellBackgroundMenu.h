// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <shlobj.h>

namespace sally::shell
{
    struct BackgroundMenuDiagnostics
    {
        HRESULT CreateViewHr = E_PENDING;
        HRESULT InterfaceHr = E_PENDING;
        HRESULT QueryHr = E_PENDING;
        int RawItemCount = -1;
    };

    class BackgroundMenu
    {
    public:
        BackgroundMenu() = default;
        ~BackgroundMenu();
        BackgroundMenu(const BackgroundMenu&) = delete;
        BackgroundMenu& operator=(const BackgroundMenu&) = delete;
        void Reset();
        HMENU GetMenu() const { return Menu; }
        IContextMenu2* GetInterface() const { return Context; }
        HMENU DetachMenu();
        IContextMenu2* DetachInterface();

    private:
        HMENU Menu = nullptr;
        IContextMenu2* Context = nullptr;
        friend HRESULT BuildBackgroundMenu(HWND, IShellFolder*, UINT, UINT, UINT,
                                           BackgroundMenu&, BackgroundMenuDiagnostics&,
                                           HRESULT (*)(IContextMenu2*, HMENU, UINT, UINT, UINT));
    };

    using BackgroundMenuQuery = HRESULT (*)(IContextMenu2*, HMENU, UINT, UINT, UINT);
    HRESULT BuildBackgroundMenu(HWND owner, IShellFolder* folder, UINT first, UINT last, UINT flags,
                                BackgroundMenu& output, BackgroundMenuDiagnostics& diagnostics,
                                BackgroundMenuQuery query);
} // namespace sally::shell
