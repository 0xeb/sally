// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/Win32ShellBackgroundMenu.h"

namespace sally::shell
{
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
