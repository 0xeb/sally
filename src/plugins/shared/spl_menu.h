// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_menu) // so that structures are independent of alignment settings
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

class CSalamanderForOperationsAbstract;

//
// ****************************************************************************
// CSalamanderBuildMenuAbstract
//
// set of Salamander methods for building plugin menu
//
// this is a subset of CSalamanderConnectAbstract methods, methods behave the same way,
// the same constants are used, see CSalamanderConnectAbstract for description

class CSalamanderBuildMenuAbstract
{
public:
    // icons are specified using the CSalamanderBuildMenuAbstract::SetIconListForMenu method, for the rest
    // of the description see CSalamanderConnectAbstract::AddMenuItem
    virtual void WINAPI AddMenuItem(int iconIndex, const char* name, DWORD hotKey, int id, BOOL callGetState,
                                    DWORD state_or, DWORD state_and, DWORD skillLevel) = 0;

    // icons are specified using the CSalamanderBuildMenuAbstract::SetIconListForMenu method, for the rest
    // of the description see CSalamanderConnectAbstract::AddSubmenuStart
    virtual void WINAPI AddSubmenuStart(int iconIndex, const char* name, int id, BOOL callGetState,
                                        DWORD state_or, DWORD state_and, DWORD skillLevel) = 0;

    // for description see CSalamanderConnectAbstract::AddSubmenuEnd
    virtual void WINAPI AddSubmenuEnd() = 0;

    // sets the bitmap with plugin icons for the menu; the bitmap must be allocated using
    // CSalamanderGUIAbstract::CreateIconList() call and then created and filled using
    // CGUIIconListAbstract interface methods; icon dimensions must be 16x16 pixels;
    // Salamander takes ownership of the bitmap object, plugin must not destroy it after calling
    // this function; Salamander keeps it only in memory, it is not saved anywhere
    virtual void WINAPI SetIconListForMenu(CGUIIconListAbstract* iconList) = 0;
};

//
// ****************************************************************************
// CPluginInterfaceForMenuExtAbstract
//

// menu item state flags (for menu extension plugins)
#define MENU_ITEM_STATE_ENABLED 0x01 // enabled, without this flag the item is disabled
#define MENU_ITEM_STATE_CHECKED 0x02 // there is a "check" or "radio" mark before the item
#define MENU_ITEM_STATE_RADIO 0x04   // ignored without MENU_ITEM_STATE_CHECKED, \
                                     // "radio" mark, without this flag it's a "check" mark
#define MENU_ITEM_STATE_HIDDEN 0x08  // item should not appear in the menu at all

class CPluginInterfaceForMenuExtAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceForMenuExtEncapsulation)
    friend class CPluginInterfaceForMenuExtEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // returns state of the menu item with identification number 'id'; return value is a combination of
    // flags (see MENU_ITEM_STATE_XXX); 'eventMask' see CSalamanderConnectAbstract::AddMenuItem
    virtual DWORD WINAPI GetMenuItemState(int id, DWORD eventMask) = 0;

    // executes menu command with identification number 'id', 'eventMask' see
    // CSalamanderConnectAbstract::AddMenuItem, 'salamander' is a set of usable Salamander
    // methods for performing operations (WARNING: can be NULL, see description of method
    // CSalamanderGeneralAbstract::PostMenuExtCommand), 'parent' is the parent of messageboxes,
    // returns TRUE if selection in the panel should be cleared (Cancel was not used, Skip
    // could have been used), otherwise returns FALSE (deselection is not performed);
    // WARNING: If the command causes changes on some path (disk/FS), it should use
    //          CSalamanderGeneralAbstract::PostChangeOnPathNotification to inform
    //          panels without automatic refresh and open FS (active and disconnected)
    // NOTE: if the command works with files/directories from the path in the current panel or
    //       directly with this path, you need to call
    //       CSalamanderGeneralAbstract::SetUserWorkedOnPanelPath for the current panel,
    //       otherwise the path in this panel will not be added to the list of working
    //       directories - List of Working Directories (Alt+F12)
    virtual BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                        int id, DWORD eventMask) = 0;

    // displays help for menu command with identification number 'id' (user presses Shift+F1,
    // finds this plugin's menu in the Plugins menu and selects a command from it), 'parent' is
    // the parent of messageboxes, returns TRUE if some help was displayed, otherwise the
    // "Using Plugins" chapter from Salamander help is displayed
    virtual BOOL WINAPI HelpForMenuItem(HWND parent, int id) = 0;

    // function for "dynamic menu extension", called only if you specify FUNCTION_DYNAMICMENUEXT in
    // SetBasicPluginData; builds the plugin menu on its load, and then again just before
    // opening it in the Plugins menu or on the Plugin bar (also before opening the Keyboard
    // Shortcuts window from Plugins Manager); commands in the new menu should have the same IDs as in the old one,
    // so that user-assigned hotkeys remain and so they can work as the last
    // used command (see Plugins / Last Command); 'parent' is the parent of messageboxes, 'salamander'
    // is a set of methods for building the menu
    virtual void WINAPI BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_menu)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
