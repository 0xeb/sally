// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_connect_to_core — frozen v107 Connect callback over the live wide API.

// Never include precomp.h here. Standard-library headers must be visible at
// global scope before sdk107.h enters its frozen namespace wrapper.

#define NOMINMAX
#include <windows.h>

#include <string>

#include "compat/legacy_to_core.h"

#include "compat/legacy_convert.h"

namespace sally::compat
{
    namespace
    {

        bool WidenConnectOptional(const char* value, std::wstring& storage,
                                  const wchar_t*& result)
        {
            if (value == nullptr)
            {
                result = nullptr;
                return true;
            }
            if (!WidenPluginText(value, storage))
            {
                result = nullptr;
                return false;
            }
            result = storage.c_str();
            return true;
        }

    } // namespace

    CLegacySalamanderConnect::CLegacySalamanderConnect(
        ::CSalamanderConnectAbstract& wideConnect,
        CLegacyGUIIconListTransfer& iconListTransfer)
        : WideConnect(wideConnect), IconListTransfer(iconListTransfer)
    {
    }

    void WINAPI CLegacySalamanderConnect::AddCustomPacker(
        const char* title, const char* defaultExtension, BOOL update)
    {
        std::wstring titleW;
        std::wstring extensionW;
        const wchar_t* liveTitle = nullptr;
        const wchar_t* liveExtension = nullptr;
        if (!WidenConnectOptional(title, titleW, liveTitle) ||
            !WidenConnectOptional(defaultExtension, extensionW, liveExtension))
            return;
        WideConnect.AddCustomPacker(liveTitle, liveExtension, update);
    }

    void WINAPI CLegacySalamanderConnect::AddCustomUnpacker(
        const char* title, const char* masks, BOOL update)
    {
        std::wstring titleW;
        std::wstring masksW;
        const wchar_t* liveTitle = nullptr;
        const wchar_t* liveMasks = nullptr;
        if (!WidenConnectOptional(title, titleW, liveTitle) ||
            !WidenConnectOptional(masks, masksW, liveMasks))
            return;
        WideConnect.AddCustomUnpacker(liveTitle, liveMasks, update);
    }

    void WINAPI CLegacySalamanderConnect::AddPanelArchiver(
        const char* extensions, BOOL edit, BOOL updateExts)
    {
        std::wstring extensionsW;
        const wchar_t* liveExtensions = nullptr;
        if (!WidenConnectOptional(extensions, extensionsW, liveExtensions))
            return;
        WideConnect.AddPanelArchiver(liveExtensions, edit, updateExts);
    }

    void WINAPI CLegacySalamanderConnect::ForceRemovePanelArchiver(
        const char* extension)
    {
        std::wstring extensionW;
        const wchar_t* liveExtension = nullptr;
        if (!WidenConnectOptional(extension, extensionW, liveExtension))
            return;
        WideConnect.ForceRemovePanelArchiver(liveExtension);
    }

    void WINAPI CLegacySalamanderConnect::AddViewer(const char* masks, BOOL force)
    {
        std::wstring masksW;
        const wchar_t* liveMasks = nullptr;
        if (!WidenConnectOptional(masks, masksW, liveMasks))
            return;
        WideConnect.AddViewer(liveMasks, force);
    }

    void WINAPI CLegacySalamanderConnect::ForceRemoveViewer(const char* mask)
    {
        std::wstring maskW;
        const wchar_t* liveMask = nullptr;
        if (!WidenConnectOptional(mask, maskW, liveMask))
            return;
        WideConnect.ForceRemoveViewer(liveMask);
    }

    void WINAPI CLegacySalamanderConnect::AddMenuItem(
        int iconIndex, const char* name, DWORD hotKey, int id, BOOL callGetState,
        DWORD stateOr, DWORD stateAnd, DWORD skillLevel)
    {
        std::wstring nameW;
        const wchar_t* liveName = nullptr;
        if (!WidenConnectOptional(name, nameW, liveName))
            return;
        WideConnect.AddMenuItem(iconIndex, liveName, hotKey, id, callGetState,
                                stateOr, stateAnd, skillLevel);
    }

    void WINAPI CLegacySalamanderConnect::AddSubmenuStart(
        int iconIndex, const char* name, int id, BOOL callGetState, DWORD stateOr,
        DWORD stateAnd, DWORD skillLevel)
    {
        std::wstring nameW;
        const wchar_t* liveName = nullptr;
        if (!WidenConnectOptional(name, nameW, liveName))
            return;
        WideConnect.AddSubmenuStart(iconIndex, liveName, id, callGetState,
                                    stateOr, stateAnd, skillLevel);
    }

    void WINAPI CLegacySalamanderConnect::AddSubmenuEnd()
    {
        WideConnect.AddSubmenuEnd();
    }

    void WINAPI CLegacySalamanderConnect::SetChangeDriveMenuItem(
        const char* title, int iconIndex)
    {
        std::wstring titleW;
        const wchar_t* liveTitle = nullptr;
        if (!WidenConnectOptional(title, titleW, liveTitle))
            return;
        WideConnect.SetChangeDriveMenuItem(liveTitle, iconIndex);
    }

    void WINAPI CLegacySalamanderConnect::SetThumbnailLoader(const char* masks)
    {
        std::wstring masksW;
        const wchar_t* liveMasks = nullptr;
        if (!WidenConnectOptional(masks, masksW, liveMasks))
            return;
        WideConnect.SetThumbnailLoader(liveMasks);
    }

    void WINAPI CLegacySalamanderConnect::SetBitmapWithIcons(HBITMAP bitmap)
    {
        WideConnect.SetBitmapWithIcons(bitmap);
    }

    void WINAPI CLegacySalamanderConnect::SetPluginIcon(int iconIndex)
    {
        WideConnect.SetPluginIcon(iconIndex);
    }

    void WINAPI CLegacySalamanderConnect::SetPluginMenuAndToolbarIcon(int iconIndex)
    {
        WideConnect.SetPluginMenuAndToolbarIcon(iconIndex);
    }

    void WINAPI CLegacySalamanderConnect::SetIconListForGUI(
        sdk107::CGUIIconListAbstract* iconList)
    {
        if (iconList == nullptr)
        {
            WideConnect.SetIconListForGUI(nullptr);
            return;
        }

        ::CGUIIconListAbstract* wide = IconListTransfer.TakeIconList(iconList);
        if (wide != nullptr)
            WideConnect.SetIconListForGUI(wide);
    }

} // namespace sally::compat
