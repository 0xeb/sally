// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_build_menu_to_core — frozen v107 BuildMenu callback over the live wide API.

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

        bool WidenBuildMenuOptional(const char* value, std::wstring& storage,
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

    CLegacySalamanderBuildMenu::CLegacySalamanderBuildMenu(
        ::CSalamanderBuildMenuAbstract& wideBuildMenu,
        CLegacyGUIIconListTransfer& iconListTransfer)
        : WideBuildMenu(wideBuildMenu), IconListTransfer(iconListTransfer)
    {
    }

    void WINAPI CLegacySalamanderBuildMenu::AddMenuItem(
        int iconIndex, const char* name, DWORD hotKey, int id,
        BOOL callGetState, DWORD stateOr, DWORD stateAnd, DWORD skillLevel)
    {
        std::wstring nameW;
        const wchar_t* liveName = nullptr;
        if (!WidenBuildMenuOptional(name, nameW, liveName))
            return;
        WideBuildMenu.AddMenuItem(iconIndex, liveName, hotKey, id,
                                  callGetState, stateOr, stateAnd, skillLevel);
    }

    void WINAPI CLegacySalamanderBuildMenu::AddSubmenuStart(
        int iconIndex, const char* name, int id, BOOL callGetState,
        DWORD stateOr, DWORD stateAnd, DWORD skillLevel)
    {
        std::wstring nameW;
        const wchar_t* liveName = nullptr;
        if (!WidenBuildMenuOptional(name, nameW, liveName))
            return;
        WideBuildMenu.AddSubmenuStart(iconIndex, liveName, id, callGetState,
                                      stateOr, stateAnd, skillLevel);
    }

    void WINAPI CLegacySalamanderBuildMenu::AddSubmenuEnd()
    {
        WideBuildMenu.AddSubmenuEnd();
    }

    void WINAPI CLegacySalamanderBuildMenu::SetIconListForMenu(
        sdk107::CGUIIconListAbstract* iconList)
    {
        if (iconList == nullptr)
        {
            WideBuildMenu.SetIconListForMenu(nullptr);
            return;
        }

        ::CGUIIconListAbstract* wide = IconListTransfer.TakeIconList(iconList);
        if (wide != nullptr)
            WideBuildMenu.SetIconListForMenu(wide);
    }

} // namespace sally::compat
