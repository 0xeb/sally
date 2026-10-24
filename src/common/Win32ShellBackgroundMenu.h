// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <shlobj.h>
#include <string>

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

    using MenuVerbQuery = HRESULT (*)(IContextMenu2*, UINT, std::wstring&);
    using MenuInitialize = HRESULT (*)(IContextMenu2*, HMENU, UINT);
    struct SharingMenuOperations
    {
        decltype(&RemoveMenu) Remove = &RemoveMenu;
        decltype(&SetMenuItemInfoW) Set = &SetMenuItemInfoW;
    };
    // The folder-item provider owns sharing commands that the background
    // provider can advertise with an empty child. Keep its handles and IDs.
    // The caller must discard folderMenu after copying any other needed rows:
    // the donor row is consumed even if attaching its child subsequently fails.
    bool PreferFolderSharingSubmenu(HMENU folderMenu, IContextMenu2* folderContext,
                                    HMENU backgroundMenu, IContextMenu2* backgroundContext,
                                    MenuVerbQuery queryVerb, MenuInitialize initialize,
                                    const SharingMenuOperations& operations = SharingMenuOperations());
} // namespace sally::shell
