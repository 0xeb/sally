// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "../folder_drop_menu.h"
#include <shlobj.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;

struct Handler
{
    ComPtr<IContextMenu> menu;
    ComPtr<IContextMenu2> menu2;
    ComPtr<IContextMenu3> menu3;
    UINT first = 0;
    UINT count = 0;
    std::vector<HMENU> submenus;
};

void CollectSubmenus(HMENU menu, std::vector<HMENU>& result)
{
    for (int i = 0; i < GetMenuItemCount(menu); ++i)
        if (HMENU child = GetSubMenu(menu, i))
        {
            if (std::find(result.begin(), result.end(), child) == result.end()) result.push_back(child);
            CollectSubmenus(child, result);
        }
}

class MenuSession
{
public:
    std::vector<Handler> handlers;
    std::vector<HKEY> associationKeys;
    HMENU root = nullptr;
    ~MenuSession()
    {
        handlers.clear();
        for (HKEY key : associationKeys) RegCloseKey(key);
    }
    static LRESULT CALLBACK Messages(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
    {
        auto& self = *reinterpret_cast<MenuSession*>(ref);
        if (message == WM_INITMENUPOPUP || message == WM_DRAWITEM || message == WM_MEASUREITEM || message == WM_MENUCHAR)
        {
            for (auto& handler : self.handlers)
            {
                if ((message == WM_INITMENUPOPUP || message == WM_MENUCHAR) &&
                    reinterpret_cast<HMENU>(message == WM_MENUCHAR ? lp : wp) != self.root &&
                    std::find(handler.submenus.begin(), handler.submenus.end(), reinterpret_cast<HMENU>(message == WM_MENUCHAR ? lp : wp)) == handler.submenus.end())
                    continue;
                // Draw/measure notifications identify their command owner explicitly.
                UINT item = UINT_MAX;
                if (message == WM_DRAWITEM && lp)
                {
                    auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
                    if (draw->CtlType != ODT_MENU) continue;
                    item = draw->itemID;
                }
                if (message == WM_MEASUREITEM && lp)
                {
                    auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
                    if (measure->CtlType != ODT_MENU) continue;
                    item = measure->itemID;
                }
                if (item != UINT_MAX && (item < handler.first || item - handler.first >= handler.count))
                    continue;
                LRESULT result = 0;
                if (handler.menu3)
                {
                    if (handler.menu3->HandleMenuMsg2(message, wp, lp, &result) == S_OK)
                    {
                        if (message != WM_INITMENUPOPUP) return result;
                    }
                }
                else if (handler.menu2 && message != WM_MENUCHAR && handler.menu2->HandleMenuMsg(message, wp, lp) == S_OK && message != WM_INITMENUPOPUP)
                    return message == WM_DRAWITEM || message == WM_MEASUREITEM ? TRUE : 0;
                if (message == WM_INITMENUPOPUP)
                {
                    const auto known = handler.submenus;
                    for (HMENU submenu : known) CollectSubmenus(submenu, handler.submenus);
                }
            }
        }
        return DefSubclassProc(window, message, wp, lp);
    }
};

std::wstring DefaultValue(HKEY key)
{
    DWORD bytes = 0;
    if (RegGetValueW(key, nullptr, nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS)
        return {};
    for (;;)
    {
        std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1);
        DWORD available = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        const LSTATUS status = RegGetValueW(key, nullptr, nullptr, RRF_RT_REG_SZ, nullptr, value.data(), &available);
        if (status == ERROR_MORE_DATA) { bytes = available; continue; }
        return status == ERROR_SUCCESS ? std::wstring(value.data()) : std::wstring();
    }
}

void AddHandlers(MenuSession& session, HMENU menu, PCIDLIST_ABSOLUTE target, IDataObject* data)
{
    std::vector<CLSID> seen;
    UINT next = 0x1000;
    for (const wchar_t* type : {L"Directory", L"Folder"})
    {
        HKEY association = nullptr, registrations = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, type, 0, KEY_READ, &association) != ERROR_SUCCESS)
            continue;
        session.associationKeys.push_back(association);
        if (RegOpenKeyExW(association, L"shellex\\DragDropHandlers", 0, KEY_READ, &registrations) == ERROR_SUCCESS)
        {
            DWORD longest = 0;
            RegQueryInfoKeyW(registrations, nullptr, nullptr, nullptr, nullptr, &longest, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
            std::vector<wchar_t> name(static_cast<size_t>(longest) + 1);
            for (DWORD index = 0; next < 0x7000; ++index)
            {
                DWORD length = static_cast<DWORD>(name.size());
                const LSTATUS status = RegEnumKeyExW(registrations, index, name.data(), &length, nullptr, nullptr, nullptr, nullptr);
                if (status == ERROR_NO_MORE_ITEMS) break;
                if (status == ERROR_MORE_DATA) { name.resize(name.size() * 2 + 1); --index; continue; }
                if (status != ERROR_SUCCESS) continue;
                HKEY registration = nullptr;
                if (RegOpenKeyExW(registrations, name.data(), 0, KEY_READ, &registration) != ERROR_SUCCESS)
                    continue;
                std::wstring className = DefaultValue(registration);
                RegCloseKey(registration);
                if (className.empty()) className.assign(name.data(), length);
                CLSID clsid;
                HRESULT classResult = CLSIDFromString(className.c_str(), &clsid);
                // A GUID-named registration may carry a descriptive default value.
                if (FAILED(classResult)) classResult = CLSIDFromString(name.data(), &clsid);
                if (FAILED(classResult) ||
                    std::any_of(seen.begin(), seen.end(), [&](const CLSID& value) { return IsEqualCLSID(value, clsid); }))
                    continue;
                seen.push_back(clsid);
                ComPtr<IShellExtInit> init;
                Handler handler;
                if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&init))) ||
                    FAILED(init->Initialize(target, data, association)) || FAILED(init.As(&handler.menu)))
                    continue;
                // Give each extension a disjoint range. Its offset is relative to first.
                handler.first = next;
                const UINT last = (std::min)(next + 0xfff, 0x6fffu);
                const int before = GetMenuItemCount(menu);
                // Handlers may retain the supplied root HMENU. Query the real
                // tracked menu, not a temporary menu whose handle would die.
                const HRESULT result = handler.menu->QueryContextMenu(menu, before, next, last, CMF_NORMAL);
                handler.count = HRESULT_CODE(result);
                if (FAILED(result) || !handler.count || handler.count > last - next + 1)
                {
                    while (GetMenuItemCount(menu) > before)
                        DeleteMenu(menu, GetMenuItemCount(menu) - 1, MF_BYPOSITION);
                    continue;
                }
                for (int item = before; item < GetMenuItemCount(menu); ++item)
                    if (HMENU submenu = GetSubMenu(menu, item))
                    {
                        handler.submenus.push_back(submenu);
                        CollectSubmenus(submenu, handler.submenus);
                    }
                handler.menu.As(&handler.menu2);
                handler.menu.As(&handler.menu3);
                session.handlers.push_back(std::move(handler));
                next += HRESULT_CODE(result);
            }
            RegCloseKey(registrations);
        }
    }
}
}

FolderDropResult ShowFolderDropMenu(HWND owner, const wchar_t* target, IDataObject* data,
                                  DWORD effects, DWORD defaultEffect, const FolderDropLabels& labels)
{
    FolderDropResult result;
    HMENU menu = CreatePopupMenu();
    if (!menu) return result;
    MenuSession session;
    session.root = menu;
    if (effects & DROPEFFECT_MOVE) AppendMenuW(menu, MF_STRING, 1, labels.move);
    if (effects & DROPEFFECT_COPY) AppendMenuW(menu, MF_STRING, 2, labels.copy);
    if (effects & DROPEFFECT_LINK) AppendMenuW(menu, MF_STRING, 3, labels.link);
    const UINT defaultId = defaultEffect == DROPEFFECT_MOVE ? 1 : defaultEffect == DROPEFFECT_COPY ? 2 : defaultEffect == DROPEFFECT_LINK ? 3 : 0;
    if (defaultId) SetMenuDefaultItem(menu, defaultId, FALSE);
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(SHParseDisplayName(target, nullptr, &pidl, 0, nullptr)))
    {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AddHandlers(session, menu, pidl, data);
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, labels.cancel);
    POINT point;
    GetCursorPos(&point);
    // The session and COM owners outlive tracking, message forwarding and invocation.
    const UINT_PTR subclassId = reinterpret_cast<UINT_PTR>(&session);
    const BOOL subclassed = SetWindowSubclass(owner, MenuSession::Messages, subclassId, reinterpret_cast<DWORD_PTR>(&session));
    const UINT command = subclassed ? TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, owner, nullptr) : 0;
    if (command >= 1 && command <= 3)
        result.choice = command == 1 ? FolderDropChoice::Move : command == 2 ? FolderDropChoice::Copy : FolderDropChoice::Link;
    else
    {
        for (auto& handler : session.handlers)
            if (FolderDropCommandInRange(command, handler.first, handler.count))
            {
                result.choice = FolderDropChoice::Extension;
                CMINVOKECOMMANDINFOEX info = {};
                info.cbSize = sizeof(info);
                info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
                info.hwnd = owner;
                info.lpDirectoryW = target;
                info.lpVerb = MAKEINTRESOURCEA(command - handler.first);
                info.lpVerbW = MAKEINTRESOURCEW(command - handler.first);
                info.nShow = SW_SHOWNORMAL;
                info.ptInvoke = point;
                result.result = handler.menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
                break;
            }
    }
    if (subclassed) RemoveWindowSubclass(owner, MenuSession::Messages, subclassId);
    DestroyMenu(menu);
    session.handlers.clear(); // release extensions before their borrowed initialization data
    CoTaskMemFree(pidl);
    return result;
}
