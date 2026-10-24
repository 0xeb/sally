// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/Win32ShellBackgroundMenu.h"

namespace sally::shell
{
    namespace
    {
        struct SharingRow
        {
            UINT Position = 0;
            UINT Command = 0;
            HMENU Child = nullptr;
        };

        bool FindSharingRow(HMENU menu, IContextMenu2* context, UINT first, UINT last,
                            MenuVerbQuery queryVerb, SharingRow& row)
        {
            for (int position = 0; position < GetMenuItemCount(menu); ++position)
            {
                MENUITEMINFOW item = {sizeof(item)};
                item.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_FTYPE;
                if (!GetMenuItemInfoW(menu, position, TRUE, &item) || item.hSubMenu == nullptr ||
                    (item.fType & MFT_SEPARATOR) != 0 || item.wID < first || item.wID > last)
                    continue;
                std::wstring verb;
                if (queryVerb(context, item.wID - first, verb) == S_OK &&
                    _wcsicmp(verb.c_str(), L"Windows.Share") == 0)
                {
                    row = {static_cast<UINT>(position), item.wID, item.hSubMenu};
                    return true;
                }
            }
            return false;
        }

        class ContextReference
        {
        public:
            explicit ContextReference(IContextMenu2* context) : Context(context) { Context->AddRef(); }
            ~ContextReference() { Context->Release(); }
            ContextReference(const ContextReference&) = delete;
            ContextReference& operator=(const ContextReference&) = delete;
        private:
            IContextMenu2* Context;
        };

        bool RowStillMatches(HMENU menu, const SharingRow& row)
        {
            MENUITEMINFOW item = {sizeof(item)};
            item.fMask = MIIM_ID | MIIM_SUBMENU;
            return GetMenuItemInfoW(menu, row.Position, TRUE, &item) &&
                   item.wID == row.Command && item.hSubMenu == row.Child;
        }
    }

    bool PreferFolderSharingSubmenu(HMENU folderMenu, IContextMenu2* folderContext,
                                    HMENU backgroundMenu, IContextMenu2* backgroundContext,
                                    MenuVerbQuery queryVerb, MenuInitialize initialize,
                                    const SharingMenuOperations& operations)
    {
        if (folderContext == nullptr || backgroundContext == nullptr || queryVerb == nullptr ||
            initialize == nullptr || operations.Remove == nullptr || operations.Set == nullptr ||
            folderMenu == backgroundMenu || !IsMenu(folderMenu) || !IsMenu(backgroundMenu))
            return false;
        ContextReference folderReference(folderContext), backgroundReference(backgroundContext);
        SharingRow folder, background;
        if (!FindSharingRow(backgroundMenu, backgroundContext, 5000, 6000, queryVerb, background))
            return false;
        if (FAILED(initialize(backgroundContext, background.Child, background.Position)))
            return false;
        if (!FindSharingRow(backgroundMenu, backgroundContext, 5000, 6000, queryVerb, background) ||
            GetMenuItemCount(background.Child) != 0 ||
            !FindSharingRow(folderMenu, folderContext, 0, 4999, queryVerb, folder))
            return false;
        if (GetMenuItemCount(folder.Child) == 0 && FAILED(initialize(folderContext, folder.Child, folder.Position)))
            return false;

        // Initialization can change either tree. Re-read both identities before
        // transferring ownership, without further callbacks between mutations.
        if (!FindSharingRow(folderMenu, folderContext, 0, 4999, queryVerb, folder) ||
            !FindSharingRow(backgroundMenu, backgroundContext, 5000, 6000, queryVerb, background) ||
            !RowStillMatches(folderMenu, folder) || !RowStillMatches(backgroundMenu, background) ||
            folder.Child == background.Child || GetMenuItemCount(folder.Child) <= 0 ||
            GetMenuItemCount(background.Child) != 0)
            return false;
        if (!operations.Remove(folderMenu, folder.Position, MF_BYPOSITION))
            return false;
        MENUITEMINFOW replacement = {sizeof(replacement)};
        replacement.fMask = MIIM_SUBMENU;
        replacement.hSubMenu = folder.Child;
        if (!operations.Set(backgroundMenu, background.Position, TRUE, &replacement))
        {
            // This source row was about to be discarded with the folder root;
            // failure leaves the original background tree unchanged.
            DestroyMenu(folder.Child);
            return false;
        }
        DestroyMenu(background.Child);
        return true;
    }

    BackgroundMenu::~BackgroundMenu()
    {
        Reset();
    }

    void BackgroundMenu::Reset()
    {
        if (Menu != nullptr)
            DestroyMenu(Menu);
        if (Context != nullptr)
            Context->Release();
        Menu = nullptr;
        Context = nullptr;
    }

    HMENU BackgroundMenu::DetachMenu()
    {
        HMENU result = Menu;
        Menu = nullptr;
        return result;
    }

    IContextMenu2* BackgroundMenu::DetachInterface()
    {
        IContextMenu2* result = Context;
        Context = nullptr;
        return result;
    }

    HRESULT BuildBackgroundMenu(HWND owner, IShellFolder* folder, UINT first, UINT last, UINT flags,
                                BackgroundMenu& output, BackgroundMenuDiagnostics& diagnostics,
                                BackgroundMenuQuery query)
    {
        output.Reset();
        diagnostics = BackgroundMenuDiagnostics();
        if (folder == nullptr || query == nullptr || first > last)
            return E_INVALIDARG;
        IContextMenu* base = nullptr;
        diagnostics.CreateViewHr = folder->CreateViewObject(owner, IID_IContextMenu,
                                                            reinterpret_cast<void**>(&base));
        if (FAILED(diagnostics.CreateViewHr) || base == nullptr)
        {
            if (base != nullptr)
                base->Release();
            return FAILED(diagnostics.CreateViewHr) ? diagnostics.CreateViewHr : E_UNEXPECTED;
        }
        diagnostics.InterfaceHr = base->QueryInterface(IID_IContextMenu2,
                                                       reinterpret_cast<void**>(&output.Context));
        base->Release();
        if (FAILED(diagnostics.InterfaceHr) || output.Context == nullptr)
        {
            output.Reset();
            return FAILED(diagnostics.InterfaceHr) ? diagnostics.InterfaceHr : E_UNEXPECTED;
        }
        output.Menu = CreatePopupMenu();
        if (output.Menu == nullptr)
        {
            output.Reset();
            return E_OUTOFMEMORY;
        }
        diagnostics.QueryHr = query(output.Context, output.Menu, first, last, flags);
        diagnostics.RawItemCount = GetMenuItemCount(output.Menu);
        if (FAILED(diagnostics.QueryHr))
        {
            output.Reset();
            return diagnostics.QueryHr;
        }
        return S_OK;
    }
} // namespace sally::shell
