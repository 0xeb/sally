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
#pragma pack(push, enter_include_spl_gui) // to make structures independent of alignment settings
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

////////////////////////////////////////////////////////
//                                                    //
// The range WM_APP + 200 to WM_APP + 399 is in       //
// const.h excluded from the space used for internal  //
// messages in Salamander.                            //
//                                                    //
////////////////////////////////////////////////////////

// menu messages
#define WM_USER_ENTERMENULOOP WM_APP + 200   // [0, 0] menu was entered
#define WM_USER_LEAVEMENULOOP WM_APP + 201   // [0, 0] menu loop mode was ended; this message is sent before posting the command
#define WM_USER_LEAVEMENULOOP2 WM_APP + 202  // [0, 0] menu loop mode was ended; this message is posted after the command
#define WM_USER_INITMENUPOPUP WM_APP + 204   // [(CGUIMenuPopupAbstract*)menuPopup, LOWORD(uPos), HIWORD(uID)]
#define WM_USER_UNINITMENUPOPUP WM_APP + 205 // [(CGUIMenuPopupAbstract*)menuPopup, LOWORD(uPos), HIWORD(uID)]
#define WM_USER_CONTEXTMENU WM_APP + 206     // [(CGUIMenuPopupAbstract*)menuPopup, (BOOL)fromMouse \
                                             //   (if triggered by mouse action, equals TRUE (use GetMessagePos); \
                                             //    if triggered by keyboard action VK_APPS or Shift+F10, equals FALSE)] \
                                             // p.s. if returns TRUE, the menu command is executed or submenu is opened \
                                             // To cast menuPopup to CMenuPopup in Salamander, \
                                             // use (CMenuPopup*)(CGUIMenuPopupAbstract*)menuPopup.

// toolbar messages
#define WM_USER_TBDROPDOWN WM_APP + 220    // [HWND hToolBar, int buttonIndex]
#define WM_USER_TBRESET WM_APP + 222       // [HWND hToolBar, TOOLBAR_TOOLTIP *tt]
#define WM_USER_TBBEGINADJUST WM_APP + 223 // [HWND hToolBar, 0]
#define WM_USER_TBENDADJUST WM_APP + 224   // [HWND hToolBar, 0]
#define WM_USER_TBGETTOOLTIP WM_APP + 225  // [HWND hToolBar, 0]
#define WM_USER_TBCHANGED WM_APP + 226     // [HWND hToolBar, 0]
#define WM_USER_TBENUMBUTTON2 WM_APP + 227 // [HWND hToolBar, TLBI_ITEM_INFO2 *tii]

// tooltip messages
#define TOOLTIP_TEXT_MAX 5000          // maximum length of tool tip string (WM_USER_TTGETTEXT message)
#define WM_USER_TTGETTEXT WM_APP + 240 // [ID passed in SetCurrentToolTip, buffer limited to TOOLTIP_TEXT_MAX]

// button pressed
#define WM_USER_BUTTON WM_APP + 244 // [(LO)WORD buttonID, (LO)WORD event was triggered from keyboard, if opening menu, select first item]
// drop down of button pressed
#define WM_USER_BUTTONDROPDOWN WM_APP + 245 // [(LO)WORD buttonID, (LO)WORD event was triggered from keyboard, if opening menu, select first item]

#define WM_USER_KEYDOWN WM_APP + 246 // [(LO)WORD ctrlID, DWORD virtual-key code]

//
// ****************************************************************************
// CGUIProgressBarAbstract
//

class CGUIProgressBarAbstract
{
public:
    // sets progress, optionally text in the center
    //
    // there is a safer variant SetProgress2(), check it out before using this method
    //
    // progress can work in two modes:
    //   1) for 'progress' >= 0 it is a classic thermometer 0% to 100%
    //      in this mode you can use the 'text' variable to set custom text displayed in the center
    //      if 'text' == NULL, standard percentage is displayed in the center
    //   2) for 'progress' == -1 it is an indeterminate state, where a small rectangle moves back and forth
    //      movement is controlled by methods SetSelfMoveTime(), SetSelfMoveSpeed() and Stop()
    //
    // redrawing is done immediately; for most operations it is recommended to cache data in the parent
    // dialog and start a 100ms timer, then call this method on timer tick
    //
    // can be called from any thread, thread with the control must be running, otherwise blocking occurs
    // (SendMessage is used to deliver 'progress' value to the control);
    virtual void WINAPI SetProgress(DWORD progress, const char* text) = 0;

    // has meaning in combination with calling SetProgress(-1)
    // specifies how many milliseconds after calling SetProgress(-1) the rectangle will continue moving on its own
    // if another SetProgress(-1) is called during this time, the timer resets from the beginning
    // if 'time'==0, the rectangle moves only once exactly when SetProgress(-1) is called
    // for value 'time'==0xFFFFFFFF the rectangle will move indefinitely (default value)
    virtual void WINAPI SetSelfMoveTime(DWORD time) = 0;

    // has meaning in combination with calling SetProgress(-1)
    // specifies time between rectangle movements in milliseconds
    // default value is 'moveTime'==50, which means 20 movements per second
    virtual void WINAPI SetSelfMoveSpeed(DWORD moveTime) = 0;

    // has meaning in combination with calling SetProgress(-1)
    // if the rectangle is currently moving (due to SetSelfMoveTime), it will be stopped
    virtual void WINAPI Stop() = 0;

    // sets progress, optionally text in the center
    //
    // compared to SetProgress() it has the advantage that if 'progressCurrent' >= 'progressTotal',
    // it sets progress directly: if 'progressTotal' is 0 it sets 0%, otherwise 100% and does not perform calculation
    // (it's meaningless + RTC complains due to type casting), this "disallowed" state occurs
    // e.g. when file size increases during operation or when working with file links - links have
    // zero size, but then contain data about the linked file size,
    // if you perform the calculation yourself, you must handle this "disallowed" state
    //
    // progress can work in two modes (see SetProgress()), with this method you can
    // only set mode 1):
    //   1) it is a classic thermometer 0% to 100%
    //      in this mode you can use the 'text' variable to set custom text displayed in the center
    //      if 'text' == NULL, standard percentage is displayed in the center
    //
    // redrawing is done immediately; for most operations it is recommended to cache data in the parent
    // dialog and start a 100ms timer, then call this method on timer tick
    //
    // can be called from any thread, thread with the control must be running, otherwise blocking occurs
    // (SendMessage is used to deliver 'progress' value to the control);
    virtual void WINAPI SetProgress2(const CQuadWord& progressCurrent, const CQuadWord& progressTotal,
                                     const char* text) = 0;

    // usage examples:
    //
    // 1. we want to move the rectangle manually, it doesn't move without our action
    //
    //   SetSelfMoveTime(0)           // disable automatic movement
    //   SetProgress(-1, NULL)        // move by one step
    //   ...
    //   SetProgress(-1, NULL)        // move by one step
    //
    // 2. rectangle should move automatically until Stop is called
    //
    //   SetSelfMoveTime(0xFFFFFFFF)  // infinite movement
    //   SetSelfMoveSpeed(50)         // 20 movements per second
    //   SetProgress(-1, NULL)        // start the rectangle
    //   ...                          // do some work
    //   Stop()                       // stop the rectangle
    //
    // 3. rectangle should move for a limited time, after which it stops
    //   if we "nudge" it during this time, the timer resets
    //
    //   SetSelfMoveTime(1000)        // moves automatically for one second, then stops
    //   SetSelfMoveSpeed(50)         // 20 movements per second
    //   SetProgress(-1, NULL)        // start the rectangle for one second
    //   ...
    //   SetProgress(-1, NULL)        // revive the rectangle for another second
    //
    // 4. during operation a pause occurred and we want to visualize it in progress bar
    //
    //   SetProgress(0, NULL)         // 0%
    //   SetProgress(100, NULL)       // 10%
    //   SetProgress(200, NULL)       // 20%
    //   SetProgress(300, "(paused)") // 30% -- instead of "30 %" the text "(paused)" is displayed
    //   ... (waiting for resume)
    //   SetProgress(300, NULL)       // 30% (text paused) turn off again and continue
    //   SetProgress(400, NULL)       // 40%
    //   ...
};

//
// ****************************************************************************
// CGUIStaticTextAbstract
//

#define STF_CACHED_PAINT 0x0000000001    // text display will go through cache (won't flicker) \
                                         // WARNING: display is significantly slower than without this flag. \
                                         // Do not use for text in dialogs that are displayed \
                                         // once and then remain unchanged. \
                                         // Use for frequently/rapidly changing text (ongoing operation).
#define STF_BOLD 0x0000000002            // bold font will be used for text
#define STF_UNDERLINE 0x0000000004       // underlined font will be used for text (for poor readability \
                                         // use only for HyperLink and special cases)
#define STF_DOTUNDERLINE 0x0000000008    // text will be dotted underlined (for poor readability \
                                         // use only for HyperLink and special cases)
#define STF_HYPERLINK_COLOR 0x0000000010 // text color will be determined by hyperlink color
#define STF_END_ELLIPSIS 0x0000000020    // if text is too long, it will be terminated with ellipsis "..."
#define STF_PATH_ELLIPSIS 0x0000000040   // if text is too long, it will be shortened and ellipsis "..." \
                                         // will be inserted so that the end is visible
#define STF_HANDLEPREFIX 0x0000000080    // characters after '&' will be underlined; cannot be used with STF_END_ELLIPSIS or STF_PATH_ELLIPSIS

class CGUIStaticTextAbstract
{
    // All methods can only be called from the parent window thread where
    // the object was attached to the windows control and the interface pointer was obtained.
    //
    // Control can be visited via keyboard in a dialog if we assign it the WS_TABSTOP style.
public:
    // sets the control text; calling this method is faster and less computationally expensive
    // than setting text via WM_SETTEXT; returns TRUE on success, otherwise FALSE
    virtual BOOL WINAPI SetText(const char* text) = 0;

    // returns the control text; can be called from any thread;
    // returns NULL if SetText was not yet called and static control had no text
    virtual const char* WINAPI GetText() = 0;

    // sets the character for path separator; has meaning in case of STF_PATH_ELLIPSIS;
    // default is set to '\\';
    virtual void WINAPI SetPathSeparator(char separator) = 0;

    // assigns text that will be displayed as tooltip
    // returns TRUE if text copy was successfully allocated, otherwise returns FALSE
    virtual BOOL WINAPI SetToolTipText(const char* text) = 0;

    // assigns window and id, which will receive WM_USER_TTGETTEXT when tooltip is displayed
    virtual void WINAPI SetToolTip(HWND hNotifyWindow, DWORD id) = 0;
};

//
// ****************************************************************************
// CGUIHyperLinkAbstract
//

class CGUIHyperLinkAbstract
{
    // All methods can only be called from the parent window thread where
    // the object was attached to the windows control and the interface pointer was obtained.
    //
    // Control can be visited via keyboard in a dialog if we assign it the WS_TABSTOP style.
public:
    // sets the control text; calling this method is faster and less computationally expensive
    // than setting text via WM_SETTEXT; returns TRUE on success, otherwise FALSE
    virtual BOOL WINAPI SetText(const char* text) = 0;

    // returns the control text; can be called from any thread
    // returns NULL if SetText was not yet called and static control had no text
    virtual const char* WINAPI GetText() = 0;

    // assigns action to open URL address (file="https://www.altap.cz") or
    // launch program (file="C:\\TEST.EXE"); ShellExecute with 'open' command
    // is called on the parameter.
    virtual void WINAPI SetActionOpen(const char* file) = 0;

    // assigns action PostCommand(WM_COMMAND, command, 0) to parent window
    virtual void WINAPI SetActionPostCommand(WORD command) = 0;

    // assigns action to display hint and tooltip 'text'
    // if text is NULL, tooltip can be assigned by calling method
    // SetToolTipText or SetToolTip; method then always returns TRUE
    // if text is not NULL, method returns TRUE if text copy was successfully
    // allocated, otherwise returns FALSE
    // tooltip can be displayed with Space/Up/Down key (if focus is on
    // the control) and by mouse click; hint (tooltip) is then displayed directly
    // below the text and won't close until user clicks outside it with mouse or
    // presses a key
    virtual BOOL WINAPI SetActionShowHint(const char* text) = 0;

    // assigns text that will be displayed as tooltip
    // returns TRUE if text copy was successfully allocated, otherwise returns FALSE
    virtual BOOL WINAPI SetToolTipText(const char* text) = 0;

    // assigns window and id, which will receive WM_USER_TTGETTEXT when tooltip is displayed
    virtual void WINAPI SetToolTip(HWND hNotifyWindow, DWORD id) = 0;
};

//
// ****************************************************************************
// CGUIButtonAbstract
//

class CGUIButtonAbstract
{
    // All methods can only be called from the parent window thread where
    // the object was attached to the windows control and the interface pointer was obtained.
public:
    // assigns text that will be displayed as tooltip; returns TRUE on success, otherwise FALSE
    virtual BOOL WINAPI SetToolTipText(const char* text) = 0;

    // assigns window and id, which will receive WM_USER_TTGETTEXT when tooltip is displayed
    virtual void WINAPI SetToolTip(HWND hNotifyWindow, DWORD id) = 0;
};

//
// ****************************************************************************
// CGUIColorArrowButtonAbstract
//

class CGUIColorArrowButtonAbstract
{
    // All methods can only be called from the parent window thread where
    // the object was attached to the windows control and the interface pointer was obtained.
public:
    // sets text color 'textColor' and background color 'bkgndColor'
    virtual void WINAPI SetColor(COLORREF textColor, COLORREF bkgndColor) = 0;

    // sets text color 'textColor'
    virtual void WINAPI SetTextColor(COLORREF textColor) = 0;

    // sets background color 'bkgndColor'
    virtual void WINAPI SetBkgndColor(COLORREF bkgndColor) = 0;

    // returns text color
    virtual COLORREF WINAPI GetTextColor() = 0;

    // returns background color
    virtual COLORREF WINAPI GetBkgndColor() = 0;
};

//
// ****************************************************************************
// CGUIMenuPopupAbstract
//

#define MNTT_IT 1 // item
#define MNTT_PB 2 // popup begin
#define MNTT_PE 3 // popup end
#define MNTT_SP 4 // separator

#define MNTS_B 0x01 // skill level beginned
#define MNTS_I 0x02 // skill level intermediate
#define MNTS_A 0x04 // skill level advanced

struct MENU_TEMPLATE_ITEM
{
    int RowType;      // MNTT_*
    int TextResID;    // text resource
    BYTE SkillLevel;  // MNTS_*
    DWORD ID;         // generated command
    short ImageIndex; // -1 = no icon
    DWORD State;
    DWORD* Enabler; // control variable for enabling item
};

//
// constants
//

#define MENU_MASK_TYPE 0x00000001       // Retrieves or sets the 'Type' member.
#define MENU_MASK_STATE 0x00000002      // Retrieves or sets the 'State' member.
#define MENU_MASK_ID 0x00000004         // Retrieves or sets the 'ID' member.
#define MENU_MASK_SUBMENU 0x00000008    // Retrieves or sets the 'SubMenu' member.
#define MENU_MASK_CHECKMARKS 0x00000010 // Retrieves or sets the 'HBmpChecked' and 'HBmpUnchecked' members.
#define MENU_MASK_BITMAP 0x00000020     // Retrieves or sets the 'HBmpItem' member.
#define MENU_MASK_STRING 0x00000080     // Retrieves or sets the 'String' member.
#define MENU_MASK_IMAGEINDEX 0x00000100 // Retrieves or sets the 'ImageIndex' member.
#define MENU_MASK_ICON 0x00000200       // Retrieves or sets the 'HIcon' member.
#define MENU_MASK_OVERLAY 0x00000400    // Retrieves or sets the 'HOverlay' member.
#define MENU_MASK_CUSTOMDATA 0x00000800 // Retrieves or sets the 'CustomData' member.
#define MENU_MASK_ENABLER 0x00001000    // Retrieves or sets the 'Enabler' member.
#define MENU_MASK_SKILLLEVEL 0x00002000 // Retrieves or sets the 'SkillLevel' member.
#define MENU_MASK_FLAGS 0x00004000      // Retrieves or sets the 'Flags' member.

#define MENU_TYPE_STRING 0x00000001     // Displays the menu item using a text string.
#define MENU_TYPE_BITMAP 0x00000002     // Displays the menu item using a bitmap.
#define MENU_TYPE_SEPARATOR 0x00000004  // Specifies that the menu item is a separator.
#define MENU_TYPE_OWNERDRAW 0x00000100  // Assigns responsibility for drawing the menu item to the window that owns the menu.
#define MENU_TYPE_RADIOCHECK 0x00000200 // Displays selected menu items using a radio-button mark instead of a check mark if the HBmpChecked member is NULL.

#define MENU_FLAG_NOHOTKEY 0x00000001 // AssignHotKeys will skip this item

#define MENU_STATE_GRAYED 0x00000001  // Disables the menu item and grays it so that it cannot be selected.
#define MENU_STATE_CHECKED 0x00000002 // Checks the menu item.
#define MENU_STATE_DEFAULT 0x00000004 // Specifies that the menu item is the default. A menu can contain only one default menu item, which is displayed in bold.

#define MENU_LEVEL_BEGINNER 0x00000001
#define MENU_LEVEL_INTERMEDIATE 0x00000002
#define MENU_LEVEL_ADVANCED 0x00000004

#define MENU_POPUP_THREECOLUMNS 0x00000001
#define MENU_POPUP_UPDATESTATES 0x00000002 // UpdateStates is called before opening

// these flags are modified during the branch for individual popups
#define MENU_TRACK_SELECT 0x00000001 // If this flag is set, the function select item specified by SetSelectedItemIndex.
//#define MENU_TRACK_LEFTALIGN    0x00000000 // If this flag is set, the function positions the shortcut menu so that its left side is aligned with the coordinate specified by the x parameter.
//#define MENU_TRACK_TOPALIGN     0x00000000 // If this flag is set, the function positions the shortcut menu so that its top side is aligned with the coordinate specified by the y parameter.
//#define MENU_TRACK_HORIZONTAL   0x00000000 // If the menu cannot be shown at the specified location without overlapping the excluded rectangle, the system tries to accommodate the requested horizontal alignment before the requested vertical alignment.
#define MENU_TRACK_CENTERALIGN 0x00000002  // If this flag is set, the function centers the shortcut menu horizontally relative to the coordinate specified by the x parameter.
#define MENU_TRACK_RIGHTALIGN 0x00000004   // Positions the shortcut menu so that its right side is aligned with the coordinate specified by the x parameter.
#define MENU_TRACK_VCENTERALIGN 0x00000008 // If this flag is set, the function centers the shortcut menu vertically relative to the coordinate specified by the y parameter.
#define MENU_TRACK_BOTTOMALIGN 0x00000010  // If this flag is set, the function positions the shortcut menu so that its bottom side is aligned with the coordinate specified by the y parameter.
#define MENU_TRACK_VERTICAL 0x00000100     // If the menu cannot be shown at the specified location without overlapping the excluded rectangle, the system tries to accommodate the requested vertical alignment before the requested horizontal alignment.
// common flags for one Track branch
#define MENU_TRACK_NONOTIFY 0x00001000  // If this flag is set, the function does not send notification messages when the user clicks on a menu item.
#define MENU_TRACK_RETURNCMD 0x00002000 // If this flag is set, the function returns the menu item identifier of the user's selection in the return value.
//#define MENU_TRACK_LEFTBUTTON   0x00000000 // If this flag is set, the user can select menu items with only the left mouse button.
#define MENU_TRACK_RIGHTBUTTON 0x00010000 // If this flag is set, the user can select menu items with both the left and right mouse buttons.
#define MENU_TRACK_HIDEACCEL 0x00100000   // Salamander 2.51 or later: If this flag is set, the acceleration keys will not be underlined (specify when menu is opened by mouse event).

class CGUIMenuPopupAbstract;

struct MENU_ITEM_INFO
{
    DWORD Mask;
    DWORD Type;
    DWORD State;
    DWORD ID;
    CGUIMenuPopupAbstract* SubMenu;
    HBITMAP HBmpChecked;
    HBITMAP HBmpUnchecked;
    HBITMAP HBmpItem;
    char* String;
    DWORD StringLen;
    int ImageIndex;
    HICON HIcon;
    HICON HOverlay;
    ULONG_PTR CustomData;
    DWORD SkillLevel;
    DWORD* Enabler;
    DWORD Flags;
};

/*
Mask
  Members to retrieve or set. This member can be one or more of these values.

Type
  Item type. This variable can have one or more values:

   MENU_TYPE_OWNERDRAW    Item drawing is handled by the window that owns the menu.
                          For each menu item, WM_MEASUREITEM and WM_DRAWITEM queries
                          are sent. TypeData variable contains a 32-bit application-
                          defined value.

   MENU_TYPE_RADIOCHECK   Checked items are displayed with a dot instead of a checkmark,
                          if HBmpChecked is NULL.

   MENU_TYPE_SEPARATOR    Horizontal separator line. TypeData has no meaning.

   MENU_TYPE_STRING       Item contains a text string. TypeData points to a null-
                          terminated string.

   MENU_TYPE_BITMAP       Item contains a bitmap.

  Values MENU_TYPE_BITMAP, MENU_TYPE_SEPARATOR and MENU_TYPE_STRING cannot be used together.

State
  Item state. This variable can have one or more values:

   MENU_STATE_CHECKED     Item is checked.

   MENU_STATE_DEFAULT     Menu can contain only one default item. It is
                          drawn in bold.

   MENU_STATE_GRAYED      Disables item - it will be grayed and cannot be selected.

SkillLevel
  User skill level of the item. This variable can have one of these values:

   MENU_LEVEL_BEGINNER       beginner - will always be displayed
   MENU_LEVEL_INTERMEDIATE   intermediate - will be displayed to guru and intermediate users
   MENU_LEVEL_ADVANCED       advanced - will be displayed only to guru users

ID
  Application-defined 16-bit value that identifies the menu item.

SubMenu
  Pointer to popup menu attached to this item. If this item does not
  open a submenu, SubMenu is NULL.

HBmpChecked
  Handle to bitmap that is displayed before the item when the item
  is checked. If this variable is NULL, the default bitmap is used.
  If MENU_TYPE_RADIOCHECK bit is set, the default bitmap is a dot,
  otherwise a checkmark. If ImageIndex is not -1, this bitmap
  will not be used.

HBmpUnchecked
  Handle to bitmap that is displayed before the item when the item
  is not checked. If this variable is NULL, no bitmap
  will be displayed. If ImageIndex is not -1, this bitmap
  will not be used.

ImageIndex
  Bitmap index in ImageList CMenuPopup::HImageList. Bitmap is drawn
  before the item. Depending on MENU_STATE_CHECKED and MENU_STATE_GRAYED.
  If variable is -1, nothing will be drawn.

Enabler
  Pointer to DWORD that determines item state: TRUE->enabled, FALSE->grayed.
  If NULL, item will be enabled.
*/

class CGUIMenuPopupAbstract
{
    // All methods can only be called from the parent window thread where
    // the object was attached to the windows control and the interface pointer was obtained.
public:
    //
    // LoadFromTemplate
    //   Builds menu contents based on 'menuTemplate',
    //
    // Parameters
    //   'hInstance'
    //      [in] Handle to the module containing the string resources (MENU_TEMPLATE_ITEM::TextResID).
    //
    //   'menuTemplate'
    //      [in] Pointer to a menu template.
    //
    //      A menu template consists of two or more MENU_TEMPLATE_ITEM structures.
    //      'MENU_TEMPLATE_ITEM::RowType' of first structure must be MNTT_PB (popup begin).
    //      'MENU_TEMPLATE_ITEM::RowType' of last structure must be MNTT_PE (popup end).
    //
    //   'enablersOffset'
    //      [in] Pointer to array of enablers.
    //
    //      If this parameter is NULL, 'MENU_ITEM_INFO::Enabler' value is pointer to enabler
    //      variable. Otherwise 'MENU_ITEM_INFO::Enabler' is index to the enablers array.
    //      Zero index is reserved for "always enabled" item.
    //
    //   'hImageList'
    //      [in] Handle of image list that the menu will use to display menu items images
    //      that are in their default state.
    //
    //      If this parameter is NULL, no images will be displayed in the menu items.
    //
    //   'hHotImageList'
    //      [in] Handle of image list that the menu will use to display menu items images
    //      that are in their selected or checked state.
    //
    //      If this parameter is NULL, normal images will be displayed instead of hot images.
    //
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    virtual BOOL WINAPI LoadFromTemplate(HINSTANCE hInstance,
                                         const MENU_TEMPLATE_ITEM* menuTemplate,
                                         DWORD* enablersOffset,
                                         HIMAGELIST hImageList,
                                         HIMAGELIST hHotImageList) = 0;

    //
    // SetSelectedItemIndex
    //   Sets the item which will be selected when menu popup is displayed.
    //
    // Parameters
    //   'index'
    //      [in] The index to select.
    //      If this value is -1, none of the items will be selected.
    //      This index is only applied when method Track with MENU_TRACK_SELECT flag is called.
    //
    // See Also
    //   GetSelectedItemIndex
    //
    virtual void WINAPI SetSelectedItemIndex(int index) = 0;

    //
    // GetSelectedItemIndex
    //   Retrieves the currently selected item in the menu.
    //
    // Return Values
    //   Returns the index of the selected item, or -1 if no item is selected.
    //
    // See Also
    //   SetSelectedItemIndex
    //
    virtual int WINAPI GetSelectedItemIndex() = 0;

    //
    // SetTemplateMenu
    //   Assigns the Windows menu handle which will be used as template when menu popup is displayed.
    //
    // Parameters
    //   'hWindowsMenu'
    //      [in] Handle to the Windows menu handle to be used as template.
    //      If this value is NULL, Windows menu handle will not be used.
    //
    // See Also
    //   GetTemplateMenu
    //
    virtual void WINAPI SetTemplateMenu(HMENU hWindowsMenu) = 0;

    //
    // GetTemplateMenu
    //   Retrieves a handle to the Windows menu assigned as template.
    //
    // Return Values
    //   The return value is a handle to the Windows menu.
    //   If the object has no Windows menu template assigned, the return value is NULL.
    //
    // See Also
    //   SetTemplateMenu
    //
    virtual HMENU WINAPI GetTemplateMenu() = 0;

    //
    // GetSubMenu
    //   Retrieves a pointer to the submenu activated by the specified menu item.
    //
    // Parameters
    //   'position'
    //      [in] Specifies the zero-based position of the menu item that activates the submenu.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a menu item identifier. Otherwise, it is a zero-based menu item position.
    //
    // Return Values
    //   If the function succeeds, the return value is a pointer to the submenu activated by the menu item.
    //   If the menu item does not activate submenu, the return value is NULL.
    //

    virtual CGUIMenuPopupAbstract* WINAPI GetSubMenu(DWORD position, BOOL byPosition) = 0;

    //
    // InsertItem
    //   Inserts a new menu item at the specified position in a menu.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the menu item before which to insert the new item.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a menu item identifier. Otherwise, it is a zero-based menu item position.
    //
    //   'mii'
    //      [in] Pointer to a MENU_ITEM_INFO structure that contains information about the new menu item.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   SetItemInfo, GetItemInfo
    //
    virtual BOOL WINAPI InsertItem(DWORD position,
                                   BOOL byPosition,
                                   const MENU_ITEM_INFO* mii) = 0;

    //
    // SetItemInfo
    //   Changes information about a menu item.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the menu item to change.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a menu item identifier. Otherwise, it is a zero-based menu item position.
    //
    //   'mii'
    //      [in] Pointer to a MENU_ITEM_INFO structure that contains information about the menu item
    //      and specifies which menu item attributes to change.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   InsertItem, GetItemInfo
    //
    virtual BOOL WINAPI SetItemInfo(DWORD position,
                                    BOOL byPosition,
                                    const MENU_ITEM_INFO* mii) = 0;

    //
    // GetItemInfo
    //   Retrieves information about a menu item.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the menu item to get information about.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a menu item identifier. Otherwise, it is a zero-based menu item position.
    //
    //   'mii'
    //      [in/out] Pointer to a MENU_ITEM_INFO structure that contains information to retrieve
    //      and receives information about the menu item.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   InsertItem, SetItemInfo
    //
    virtual BOOL WINAPI GetItemInfo(DWORD position,
                                    BOOL byPosition,
                                    MENU_ITEM_INFO* mii) = 0;

    //
    // SetStyle
    //   Sets the menu popup style.
    //
    // Parameters
    //   'style'
    //      [in] New menu style.
    //      This parameter can be a combination of menu popup styles MENU_POPUP_*.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    virtual BOOL WINAPI SetStyle(DWORD style) = 0;

    //
    // CheckItem
    //   Sets the state of the specified menu item's check-mark attribute to either checked or clear.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the menu item to change.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a menu item identifier. Otherwise, it is a zero-based menu item position.
    //
    //   'checked'
    //      [in] Indicates whether the menu item will be checked or cleared.
    //      If this parameter is TRUE, sets the check-mark attribute to the selected state.
    //      If this parameter is FALSE, sets the check-mark attribute to the clear state.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   EnableItem, CheckRadioItem, InsertItem, SetItemInfo
    //
    virtual BOOL WINAPI CheckItem(DWORD position,
                                  BOOL byPosition,
                                  BOOL checked) = 0;

    //
    // CheckRadioItem
    //   Checks a specified menu item and makes it a radio item. At the same time, the method
    //   clears all other menu items in the associated group and clears the radio-item type
    //   flag for those items.
    //
    // Parameters
    //   'positionFirst'
    //      [in] Identifier or position of the first menu item in the group.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'positionLast'
    //      [in] Identifier or position of the last menu item in the group.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'positionCheck'
    //      [in] Identifier or position of the menu item to check.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'positionFirst', 'positionLast', and
    //      'positionCheck'. If this parameter is FALSE, the other parameters specify
    //      menu item identifiers. Otherwise, the other parameters specify the menu
    //      item positions.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   EnableItem, CheckItem, InsertItem, SetItemInfo
    //
    virtual BOOL WINAPI CheckRadioItem(DWORD positionFirst,
                                       DWORD positionLast,
                                       DWORD positionCheck,
                                       BOOL byPosition) = 0;

    //
    // SetDefaultItem
    //   Sets the default menu item for the specified menu.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the new default menu item or –1 for no default item.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a menu item identifier. Otherwise, it is a zero-based menu item position.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   EnableItem, CheckItem, CheckRadioItem, InsertItem, SetItemInfo
    //
    virtual BOOL WINAPI SetDefaultItem(DWORD position,
                                       BOOL byPosition) = 0;

    //
    // EnableItem
    //   Enables or disables the specified menu item.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the menu item to be enabled or disabled.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a menu item identifier. Otherwise, it is a zero-based menu item position.
    //
    //   'enabled'
    //      [in] Indicates whether the menu item will be enabled or disabled.
    //      If this parameter is TRUE, enables the menu item.
    //      If this parameter is FALSE, disables the menu item.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   CheckItem, InsertItem, SetItemInfo
    //
    virtual BOOL WINAPI EnableItem(DWORD position,
                                   BOOL byPosition,
                                   BOOL enabled) = 0;

    //
    // EnableItem
    //   Determines the number of items in the specified menu.
    //
    // Return Values
    //   The return value specifies the number of items in the menu.
    //
    virtual int WINAPI GetItemCount() = 0;

    //
    // RemoveAllItems
    //   Deletes all items from the menu popup.
    //   If the removed menu item opens submenu, this method frees the memory used by the submenu.
    //
    // See Also
    //   RemoveItemsRange
    //
    virtual void WINAPI RemoveAllItems() = 0;

    //
    // RemoveItemsRange
    //   Deletes items range from the menu popup.
    //
    // Parameters
    //   'firstIndex'
    //      [in] Specifies the first menu item to be deleted.
    //
    //   'lastIndex'
    //      [in] Specifies the last menu item to be deleted.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   RemoveAllItems
    //
    virtual BOOL WINAPI RemoveItemsRange(int firstIndex,
                                         int lastIndex) = 0;

    //
    // BeginModifyMode
    //   Allows changes of the opened menu popup.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   EndModifyMode
    //
    virtual BOOL WINAPI BeginModifyMode() = 0;

    //
    // EndModifyMode
    //   Ends modify mode.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   BeginModifyMode
    //
    virtual BOOL WINAPI EndModifyMode() = 0;

    //
    // SetSkillLevel
    //   Sets user skill level for this menu popup.
    //
    // Parameters
    //   'skillLevel'
    //      [in] Specifies the user skill level.
    //      This parameter must be one or a combination of MENU_LEVEL_*.
    //
    virtual void WINAPI SetSkillLevel(DWORD skillLevel) = 0;

    //
    // FindItemPosition
    //   Retrieves the menu item position in the menu popup.
    //
    // Parameters
    //   'id'
    //      [in] Specifies the identifier of the menu item whose position is to be retrieved.
    //
    // Return Values
    //   Zero-based position of the specified menu item.
    //   If menu item is not found, return value is -1.
    //
    virtual int WINAPI FindItemPosition(DWORD id) = 0;

    //
    // FillMenuHandle
    //   Inserts the menu items to the Windows menu popup.
    //
    // Parameters
    //   'hMenu'
    //      [in] Handle to the menu to be filled.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    virtual BOOL WINAPI FillMenuHandle(HMENU hMenu) = 0;

    //
    // GetStatesFromHWindowsMenu
    //   Applies Windows menu popup item states to the contained items.
    //
    // Parameters
    //   'hMenu'
    //      [in] Handle to the menu whose item states are to be retrieved.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    virtual BOOL WINAPI GetStatesFromHWindowsMenu(HMENU hMenu) = 0;

    //
    // SetImageList
    //   Sets the image list that the menu will use to display images in the items that
    //   are in their default state.
    //
    // Parameters
    //   'hImageList'
    //      [in] Handle to the image list that will be set.
    //      If this parameter is NULL, no images will be displayed in the items.
    //
    //   'subMenu'
    //      [in] Specifies whether to set SubMenus image list to.
    //      If this parameter is TRUE, image list will be set also in all submenu items,
    //      otherwise image list will be set only in this menu popup.
    //
    // See Also
    //   SetHotImageList
    //
    virtual void WINAPI SetImageList(HIMAGELIST hImageList,
                                     BOOL subMenu) = 0;

    //
    // SetHotImageList
    //   Sets the image list that the menu will use to display images in the items that
    //   are in their hot or checked state.
    //
    // Parameters
    //   'hImageList'
    //      [in] Handle to the image list that will be set.
    //      If this parameter is NULL, no images will be displayed in the items.
    //
    //   'subMenu'
    //      [in] Specifies whether to set SubMenus image list to.
    //      If this parameter is TRUE, image list will be set also in all submenu items,
    //      otherwise image list will be set only in this menu popup.
    //
    // See Also
    //   SetImageList
    //
    virtual void WINAPI SetHotImageList(HIMAGELIST hHotImageList,
                                        BOOL subMenu) = 0;

    //
    // Track
    //   Displays a shortcut menu at the specified location and tracks the selection of
    //   items on the shortcut menu.
    //
    // Parameters
    //   'trackFlags'
    //      [in] Specifies function options.
    //      This parameter can be a combination of MENU_TRACK_* flags.
    //
    //   'x'
    //      [in] Horizontal location of the shortcut menu, in screen coordinates.
    //
    //   'y'
    //      [in] Vertical location of the shortcut menu, in screen coordinates.
    //
    //   'hwnd'
    //      [in] Handle to the window that owns the shortcut menu. This window
    //      receives all messages from the menu. The window does not receive
    //      a WM_COMMAND message from the menu until the function returns.
    //
    //      If you specify MENU_TRACK_NONOTIFY in the 'trackFlags' parameter,
    //      the function does not send messages to the window identified by hwnd.
    //      However, you still have to pass a window handle in 'hwnd'. It can be
    //      any window handle from your application.
    //   'exclude'
    //      [in] Rectangle to exclude when positioning the window, in screen coordinates.
    //
    // Return Values
    //   If you specify MENU_TRACK_RETURNCMD in the 'trackFlags' parameter, the return
    //   value is the menu-item identifier of the item that the user selected. If the
    //   user cancels the menu without making a selection, or if an error occurs, then
    //   the return value is zero.
    //
    //   If you do not specify MENU_TRACK_RETURNCMD in the 'trackFlags' parameter, the
    //   return value is nonzero if the function succeeds and zero if it fails.
    //
    virtual DWORD WINAPI Track(DWORD trackFlags,
                               int x,
                               int y,
                               HWND hwnd,
                               const RECT* exclude) = 0;

    //
    // GetItemRect
    //   Retrieves the bounding rectangle of a item in the menu.
    //
    // Parameters
    //   'index'
    //      [in] Zero-based index of the item for which to retrieve information.
    //
    //   'rect'
    //      [out] Address of a RECT structure that receives the screen coordinates
    //      of the bounding rectangle.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    virtual BOOL WINAPI GetItemRect(int index,
                                    RECT* rect) = 0;

    //
    // UpdateItemsState
    //   Updates state for all items with specified 'Enabler'.
    //   Call this method when enabler variables altered.
    //
    virtual void WINAPI UpdateItemsState() = 0;

    //
    // SetMinWidth
    //   Sets the minimum width to be used for menu popup.
    //
    // Parameters
    //   'minWidth'
    //      [in] Specifies the minimum width of the menu popup.
    //
    virtual void WINAPI SetMinWidth(int minWidth) = 0;

    //
    // SetPopupID
    //   Sets the ID for menu popup.
    //
    // Parameters
    //   'id'
    //      [in] Specifies the ID of the menu popup.
    //
    virtual void WINAPI SetPopupID(DWORD id) = 0;

    //
    // GetPopupID
    //   Retrieves the ID for menu popup.
    //
    // Return Values
    //   Returns the ID for menu popup.
    //
    virtual DWORD WINAPI GetPopupID() = 0;

    //
    // AssignHotKeys
    //   Automatically assigns hot keys to the menu items that
    //   has not hot key already assigned.
    //
    virtual void WINAPI AssignHotKeys() = 0;
};

//
// ****************************************************************************
// CGUIMenuBarAbstract
//

class CGUIMenuBarAbstract
{
    // All methods can only be called from the parent window thread where
    // the object was attached to the windows control and the interface pointer was obtained.
public:
    //
    // CreateWnd
    //   Creates child toolbar window.
    //
    // Parameters
    //   'hParent'
    //      [in] Handle to the parent or owner window of the toolbar being created.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    virtual BOOL WINAPI CreateWnd(HWND hParent) = 0;

    //
    // GetHWND
    //   Retrieves Windows HWND value of the toolbar.
    //
    // Return Values
    //   The Windows HWND handle of the toolbar.
    //
    virtual HWND WINAPI GetHWND() = 0;

    //
    // GetNeededWidth
    //   Retrieves the total width of all of the visible buttons and separators
    //   in the toolbar.
    //
    // Return Values
    //   Returns the needed width for the toolbar.
    //
    // See Also
    //   GetNeededHeight
    //
    virtual int WINAPI GetNeededWidth() = 0;

    //
    // GetNeededHeight
    //   Retrieves the total height of all of the visible buttons and separators
    //   in the toolbar.
    //
    // Return Values
    //   Returns the needed height for the toolbar.
    //
    // See Also
    //   GetNeededWidth
    //
    virtual int WINAPI GetNeededHeight() = 0;

    //
    // SetFont
    //   Updates the font that a menubar is to use when drawing text.
    //   Call this method after receiving PLUGINEVENT_SETTINGCHANGE through CPluginInterface::Event().
    //
    virtual void WINAPI SetFont() = 0;

    //
    // GetItemRect
    //   Retrieves the bounding rectangle of a button in the toolbar.
    //
    // Parameters
    //   'index'
    //      [in] Zero-based index of the button for which to retrieve information.
    //
    //   'r'
    //      [out] Address of a RECT structure that receives the screen coordinates
    //      of the bounding rectangle.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    virtual BOOL WINAPI GetItemRect(int index, RECT& r) = 0;

    // switches menu to Menu mode (as if user pressed and released Alt)
    virtual void WINAPI EnterMenu() = 0;
    // returns TRUE if menu is in Menu mode
    virtual BOOL WINAPI IsInMenuLoop() = 0;
    // switches menu to Help mode (Shift + F1)
    virtual void WINAPI SetHelpMode(BOOL helpMode) = 0;

    //
    // IsMenuBarMessage
    //   The IsMenuBarMessage method determines whether a message is intended for
    //   the menubar or menu and, if it is, processes the message.
    //
    // Parameters
    //   'lpMsg'
    //      [in] Pointer to an MSG structure that contains the message to be checked.
    //
    // Return Values
    //   If the message has been processed, the return value is nonzero.
    //   If the message has not been processed, the return value is zero.
    //
    virtual BOOL WINAPI IsMenuBarMessage(CONST MSG* lpMsg) = 0;
};

//
// ****************************************************************************
// CGUIToolBarAbstract
//

// Toolbar Styles

#define TLB_STYLE_IMAGE 0x00000001      // icons with ImageIndex != -1 will be displayed \
                                        // also GetNeededSpace will account for icon height
#define TLB_STYLE_TEXT 0x00000002       // text will be displayed for items with TLBI_STYLE_SHOWTEXT set \
                                        // also GetNeededSpace will account for font size
#define TLB_STYLE_ADJUSTABLE 0x00000004 // can toolbar be customized?
#define TLB_STYLE_VERTICAL 0x00000008   // buttons are stacked vertically, separators horizontal, mutually exclusive with TLB_STYLE_TEXT, \
                                        // because vertical text is not supported

// Toolbar Item Masks
#define TLBI_MASK_ID 0x00000001         // Retrieves or sets the 'ID' member.
#define TLBI_MASK_CUSTOMDATA 0x00000002 // Retrieves or sets the 'CustomData' member.
#define TLBI_MASK_IMAGEINDEX 0x00000004 // Retrieves or sets the 'ImageIndex' member.
#define TLBI_MASK_ICON 0x00000008       // Retrieves or sets the 'HIcon' member.
#define TLBI_MASK_STATE 0x00000010      // Retrieves or sets the 'State' member.
#define TLBI_MASK_TEXT 0x00000020       // Retrieves or sets the 'Text' member.
#define TLBI_MASK_TEXTLEN 0x00000040    // Retrieves the 'TextLen' member.
#define TLBI_MASK_STYLE 0x00000080      // Retrieves or sets the 'Style' member.
#define TLBI_MASK_WIDTH 0x00000100      // Retrieves or sets the 'Width' member.
#define TLBI_MASK_ENABLER 0x00000200    // Retrieves or sets the 'Enabler' member.
#define TLBI_MASK_OVERLAY 0x00000800    // Retrieves or sets the 'HOverlay' member.

// Toolbar Item Styles
#define TLBI_STYLE_CHECK 0x00000001 // Creates a dual-state push button that toggles between \
                                    // the pressed and nonpressed states each time the user \
                                    // clicks it. The button has a different background color \
                                    // when it is in the pressed state.

#define TLBI_STYLE_RADIO 0x00000002 // If TLBI_STATE_CHECKED is not set when clicked, it switches \
                                    // to that state. If already there, it stays there.

#define TLBI_STYLE_DROPDOWN 0x00000004 // Creates a drop-down style button that can display a \
                                       // list when the button is clicked. Instead of the \
                                       // WM_COMMAND message used for normal buttons, \
                                       // drop-down buttons send a WM_USER_TBDROPDOWN notification. \
                                       // An application can then have the notification handler \
                                       // display a list of options.

#define TLBI_STYLE_NOPREFIX 0x00000008 // Specifies that the button text will not have an \
                                       // accelerator prefix associated with it.

#define TLBI_STYLE_SEPARATOR 0x00000010 // Creates a separator, providing a small gap between \
                                        // button groups. A button that has this style does not \
                                        // receive user input.

#define TLBI_STYLE_SHOWTEXT 0x00000020 // Specifies that button text should be displayed. \
                                       // All buttons can have text, but only those buttons \
                                       // with the BTNS_SHOWTEXT button style will display it. \
                                       // This style must be used with the TLB_STYLE_TEXT style.

#define TLBI_STYLE_WHOLEDROPDOWN 0x00000040 // Specifies that the button will have a drop-down arrow, \
                                            // but not as a separate section.

#define TLBI_STYLE_SEPARATEDROPDOWN 0x00000080 // Specifies that the button will have a drop-down arrow, \
                                               // in separated section.

#define TLBI_STYLE_FIXEDWIDTH 0x00000100 // Width of this item is not automatically calculated.

// Toolbar Item States
#define TLBI_STATE_CHECKED 0x00000001         // The button has the TLBI_STYLE_CHECK style and is being clicked.
#define TLBI_STATE_GRAYED 0x00000002          // The button is grayed and cannot receive user input.
#define TLBI_STATE_PRESSED 0x00000004         // The button is being clicked.
#define TLBI_STATE_DROPDOWNPRESSED 0x00000008 // The drop down is being clicked.

struct TLBI_ITEM_INFO2
{
    DWORD Mask;
    DWORD Style;
    DWORD State;
    DWORD ID;
    char* Text;
    int TextLen;
    int Width;
    int ImageIndex;
    HICON HIcon;
    HICON HOverlay;
    DWORD CustomData; // FIXME_X64 - male pro ukazatel, neni nekdy potreba?
    DWORD* Enabler;

    DWORD Index;
    char* Name;
    int NameLen;
};

/*
Mask
  TLBI_MASK_*

Style
  TLBI_STYLE_*

State
  TLBI_STATE_*

ID
  Command identifier associated with the button.
  This identifier is used in a WM_COMMAND message when the button is chosen.

Text
  Text string displayed in the toolbar item.

TextLen
  Length of the toolbar item text, when information is received.

Width
  Width of the toolbar item text.

ImageIndex
  Zero-based index of the button image in the image list.

HIcon
  Handle to the icon to display instead of image list image.
  Icon will not be destroyet.

CustomData
  Application-defined value associated with the toolbar item.

Enabler
  Pointer to the item enabler. Used in the UpdateItemsState.
  0 -> item is TLBI_STATE_GRAYED; else item is enabled

Index
  For enumeration items in customize dialog.

Name
  Name in customize dialog.

NameLen
  Name len in customize dialog.
*/

struct TOOLBAR_PADDING // The padding values are used to create a blank area
{
    WORD ToolBarVertical; // blank area above and below the button
    WORD ButtonIconText;  // blank area between icon and text
    WORD IconLeft;        // blank area before icon
    WORD IconRight;       // blank area behind icon
    WORD TextLeft;        // blank area before text
    WORD TextRight;       // blank area behind text
};

struct TOOLBAR_TOOLTIP
{
    HWND HToolBar;    // window that queries for tooltip
    DWORD ID;         // ID of button for which tooltip is requested
    DWORD Index;      // index of button for which tooltip is requested
    DWORD CustomData; // custom data of button, if defined // FIXME_X64 - too small for pointer, is it ever needed?
    char* Buffer;     // this buffer needs to be filled, maximum character count is TOOLTIP_TEXT_MAX
                      // by default the message inserts terminator at the first character
};

class CGUIToolBarAbstract
{
    // All methods can only be called from the parent window thread where
    // the object was attached to the windows control and the interface pointer was obtained.
public:
    //
    // CreateWnd
    //   Creates child toolbar window.
    //
    // Parameters
    //   'hParent'
    //      [in] Handle to the parent or owner window of the toolbar being created.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    virtual BOOL WINAPI CreateWnd(HWND hParent) = 0;

    //
    // GetHWND
    //   Retrieves Windows HWND value of the toolbar.
    //
    // Return Values
    //   The Windows HWND handle of the toolbar.
    //
    virtual HWND WINAPI GetHWND() = 0;

    //
    // GetNeededWidth
    //   Retrieves the total width of all of the visible buttons and separators
    //   in the toolbar.
    //
    // Return Values
    //   Returns the needed width for the toolbar.
    //
    // See Also
    //   GetNeededHeight
    //
    virtual int WINAPI GetNeededWidth() = 0;

    //
    // GetNeededHeight
    //   Retrieves the total height of all of the visible buttons and separators
    //   in the toolbar.
    //
    // Return Values
    //   Returns the needed height for the toolbar.
    //
    // See Also
    //   GetNeededWidth
    //
    virtual int WINAPI GetNeededHeight() = 0;

    //
    // SetFont
    //   Updates the font that a menubar is to use when drawing text.
    //   Call this method after receiving PLUGINEVENT_SETTINGCHANGE through CPluginInterface::Event().
    //
    virtual void WINAPI SetFont() = 0;

    //
    // GetItemRect
    //   Retrieves the bounding rectangle of a button in the toolbar.
    //
    // Parameters
    //   'index'
    //      [in] Zero-based index of the button for which to retrieve information.
    //
    //   'r'
    //      [out] Address of a RECT structure that receives the screen coordinates
    //      of the bounding rectangle.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    virtual BOOL WINAPI GetItemRect(int index, RECT& r) = 0;

    // CheckItem
    //   Sets the state of the specified button's attribute to either checked or normal.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the button to change.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a button identifier. Otherwise, it is a zero-based button position.
    //
    //   'checked'
    //      [in] Indicates whether the button will be checked or cleared.
    //      If this parameter is TRUE, sets the button to the checked state.
    //      If this parameter is FALSE, sets the button to the normal state.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   EnableItem, InsertItem, SetItemInfo
    //
    virtual BOOL WINAPI CheckItem(DWORD position,
                                  BOOL byPosition,
                                  BOOL checked) = 0;

    //
    // EnableItem
    //   Enables or disables the specified button.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the button to be enabled or disabled.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a button identifier. Otherwise, it is a zero-based button position.
    //
    //   'enabled'
    //      [in] Indicates whether the button will be enabled or disabled.
    //      If this parameter is TRUE, enables the button.
    //      If this parameter is FALSE, disables the button.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   CheckItem, InsertItem, SetItemInfo
    //
    virtual BOOL WINAPI EnableItem(DWORD position,
                                   BOOL byPosition,
                                   BOOL enabled) = 0;

    //
    // ReplaceImage
    //   Replaces an existing bitmap with a new bitmap.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the button to change.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a button identifier. Otherwise, it is a zero-based button position.
    //
    //   'hIcon'
    //      [in] Handle to the icon that contains the bitmap and mask for the new image.
    //
    //   'normal'
    //      [in] Specifies whether to replace normal image list icon.
    //
    //   'hot'
    //      [in] Specifies whether to replace hot image list icon.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    virtual BOOL WINAPI ReplaceImage(DWORD position,
                                     BOOL byPosition,
                                     HICON hIcon,
                                     BOOL normal,
                                     BOOL hot) = 0;

    //
    // FindItemPosition
    //   Retrieves the button position in the toolbar.
    //
    // Parameters
    //   'id'
    //      [in] Specifies the identifier of the button whose position is to be retrieved.
    //
    // Return Values
    //   Zero-based position of the specified button.
    //   If button is not found, return value is -1.
    //
    virtual int WINAPI FindItemPosition(DWORD id) = 0;

    //
    // SetImageList
    //   Sets the image list that the toolbar will use to display images in the button
    //   that are in their default state.
    //
    // Parameters
    //   'hImageList'
    //      [in] Handle to the image list that will be set.
    //
    // See Also
    //   GetImageList, GetHotImageList, SetHotImageList
    //
    virtual void WINAPI SetImageList(HIMAGELIST hImageList) = 0;

    //
    // GetImageList
    //   Retrieves the image list that a toolbar uses to display buttons
    //   in their default state.
    //
    // Return Values
    //   Returns the handle to the image list, or NULL if no image list is set.
    //
    // See Also
    //   SetImageList, GetHotImageList, SetHotImageList
    //
    virtual HIMAGELIST WINAPI GetImageList() = 0;

    //
    // SetHotImageList
    //   Sets the image list that the toolbar will use to display images in the button
    //   that are in their hot state.
    //
    // Parameters
    //   'hImageList'
    //      [in] Handle to the image list that will be set.
    //
    // See Also
    //   SetImageList, GetImageList, SetHotImageList
    //
    virtual void WINAPI SetHotImageList(HIMAGELIST hImageList) = 0;

    //
    // GetHotImageList
    //   Retrieves the image list that a toolbar uses to display hot buttons.
    //
    // Return Values
    //   Returns the handle to the image list that the toolbar uses to display hot
    //   buttons, or NULL if no hot image list is set.
    //
    // See Also
    //   SetImageList, GetImageList, SetHotImageList
    //
    virtual HIMAGELIST WINAPI GetHotImageList() = 0;

    //
    // SetStyle
    //   Sets the styles for the toolbar.
    //
    // Parameters
    //   'style'
    //      [in] Value specifying the styles to be set for the toolbar.
    //      This parameter can be a combination of TLB_STYLE_* styles.
    //
    // See Also
    //   GetStyle
    //
    virtual void WINAPI SetStyle(DWORD style) = 0;

    //
    // GetStyle
    //   Retrieves the styles currently in use for the toolbar.
    //
    // Return Values
    //   Returns a DWORD value that is a combination of TLB_STYLE_* styles.
    //
    // See Also
    //   SetStyle
    //
    virtual DWORD WINAPI GetStyle() = 0;

    //
    // RemoveItem
    //   Deletes a button from the toolbar.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the button to delete.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a button identifier. Otherwise, it is a zero-based button position.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   RemoveAllItems
    //
    virtual BOOL WINAPI RemoveItem(DWORD position,
                                   BOOL byPosition) = 0;

    //
    // RemoveAllItems
    //   Deletes all buttons from the toolbar.
    //
    // See Also
    //   RemoveItem
    //
    virtual void WINAPI RemoveAllItems() = 0;

    //
    // GetItemCount
    //   Retrieves a count of the buttons currently in the toolbar.
    //
    // Return Values
    //   Returns the count of the buttons.
    //
    virtual int WINAPI GetItemCount() = 0;

    //
    // Customize
    //   Displays the Customize Toolbar dialog box.
    //
    virtual void WINAPI Customize() = 0;

    //
    // SetPadding
    //   Sets the padding for a toolbar control.
    //
    // Parameters
    //   'padding'
    //      [in] Address of a TOOLBAR_PADDING structure that contains
    //      the new padding information.
    //
    // See Also
    //   GetPadding
    //

    virtual void WINAPI SetPadding(const TOOLBAR_PADDING* padding) = 0;

    //
    // GetPadding
    //   Retrieves the padding for the toolbar.
    //
    // Parameters
    //   'padding'
    //      [out] Address of a TOOLBAR_PADDING structure that will receive
    //      the padding information.
    //
    // See Also
    //   SetPadding
    //
    virtual void WINAPI GetPadding(TOOLBAR_PADDING* padding) = 0;

    //
    // UpdateItemsState
    //   Updates state for all items with specified 'Enabler'.
    //   Call this method when enabler variables altered.
    //
    virtual void WINAPI UpdateItemsState() = 0;

    //
    // HitTest
    //   Determines where a point lies in the toolbar.
    //
    // Parameters
    //   'xPos'
    //      [in] The x-coordinate of the hit test.
    //
    //   'yPos'
    //      [in] The y-coordinate of the hit test.
    //
    // Return Values
    //   Returns an integer value. If the return value is zero or a positive value,
    //   it is the zero-based index of the nonseparator item in which the point lies.
    //   If the return value is negative, the point does not lie within a button.
    //
    // Remarks
    //   The coordinates are relative to the toolbar's client area.
    //
    virtual int WINAPI HitTest(int xPos,
                               int yPos) = 0;

    //
    // InsertMarkHitTest
    //   Retrieves the insertion mark information for a point in the toolbar.
    //
    // Parameters
    //   'xPos'
    //      [in] The x-coordinate of the hit test.
    //
    //   'yPos'
    //      [in] The y-coordinate of the hit test.
    //
    //   'index'
    //      [out] Zero-based index of the button with insertion mark.
    //      If this member is -1, there is no insertion mark.
    //
    //   'after'
    //      [out] Defines where the insertion mark is in relation to 'index'.
    //      If the value is FALSE, the insertion mark is to the left of the specified button.
    //      If the value is TRUE, the insertion mark is to the right of the specified button.
    //
    // Return Values
    //   Returns TRUE if the point is an insertion mark, or FALSE otherwise.
    //
    // Remarks
    //   The coordinates are relative to the toolbar's client area.
    //
    // See Also
    //   SetInsertMark
    //
    virtual BOOL WINAPI InsertMarkHitTest(int xPos,
                                          int yPos,
                                          int& index,
                                          BOOL& after) = 0;

    //
    // SetInsertMark
    //   Sets the current insertion mark for the toolbar.
    //
    // Parameters
    //   'index'
    //      [out] Zero-based index of the button with insertion mark.
    //      If this member is -1, there is no insertion mark.
    //
    //   'after'
    //      [out] Defines where the insertion mark is in relation to 'index'.
    //      If the value is FALSE, the insertion mark is to the left of the specified button.
    //      If the value is TRUE, the insertion mark is to the right of the specified button.
    //
    // See Also
    //   InsertMarkHitTest
    //
    virtual void WINAPI SetInsertMark(int index,
                                      BOOL after) = 0;

    //
    // SetHotItem
    //   Sets the hot item in the toolbar.
    //
    // Parameters
    //   'index'
    //      [out] Zero-based index of the item that will be made hot.
    //      If this value is -1, none of the items will be hot.
    //
    // Return Values
    //   Returns the index of the previous hot item, or -1 if there was no hot item.
    //
    virtual int WINAPI SetHotItem(int index) = 0;

    //
    // SetHotItem
    //   Updates the toolbar graphic handles.
    //
    virtual void WINAPI OnColorsChanged() = 0;

    //
    // InsertItem2
    //   Inserts a new button at the specified position in a toolbar.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the button before which to insert the new button.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a button identifier. Otherwise, it is a zero-based button position.
    //
    //   'tii'
    //      [in] Pointer to a TLBI_ITEM_INFO2 structure that contains information about the
    //      new button.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   SetItemInfo2, GetItemInfo2
    virtual BOOL WINAPI InsertItem2(DWORD position,
                                    BOOL byPosition,
                                    const TLBI_ITEM_INFO2* tii) = 0;

    //
    // SetItemInfo2
    //   Changes information about a button.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the button to change.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a button identifier. Otherwise, it is a zero-based button position.
    //
    //   'mii'
    //      [in] Pointer to a TLBI_ITEM_INFO2 structure that contains information about the button
    //      and specifies which button attributes to change.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   InsertItem2, GetItemInfo2
    virtual BOOL WINAPI SetItemInfo2(DWORD position,
                                     BOOL byPosition,
                                     const TLBI_ITEM_INFO2* tii) = 0;

    //
    // GetItemInfo2
    //   Retrieves information about a button.
    //
    // Parameters
    //   'position'
    //      [in] Identifier or position of the button to get information about.
    //      The meaning of this parameter depends on the value of 'byPosition'.
    //
    //   'byPosition'
    //      [in] Value specifying the meaning of 'position'. If this parameter is FALSE, 'position'
    //      is a button identifier. Otherwise, it is a zero-based button position.
    //
    //   'mii'
    //      [in/out] Pointer to a TLBI_ITEM_INFO2 structure that contains information to retrieve
    //      and receives information about the button.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // See Also
    //   InsertItem2, SetItemInfo2
    virtual BOOL WINAPI GetItemInfo2(DWORD position,
                                     BOOL byPosition,
                                     TLBI_ITEM_INFO2* tii) = 0;
};

//
// ****************************************************************************
// CGUIIconListAbstract
//
// Our internal 32-bit image list. 8 bits for each RGB channel and 8 bits for alpha transparency.

class CGUIIconListAbstract
{
public:
    // creates image list with icon dimensions 'imageWidth' x 'imageHeight' and icon count
    // 'imageCount'; the image list then needs to be filled using ReplaceIcon() method calls;
    // returns TRUE on success, otherwise FALSE
    virtual BOOL WINAPI Create(int imageWidth, int imageHeight, int imageCount) = 0;

    // creates based on the provided windows image list ('hIL'); 'requiredImageSize' specifies
    // icon dimensions, if -1, dimensions from 'hIL' are used; returns TRUE on success,
    // otherwise FALSE
    virtual BOOL WINAPI CreateFromImageList(HIMAGELIST hIL, int requiredImageSize) = 0;

    // creates based on the provided PNG resource; 'hInstance' and 'lpBitmapName' specify the resource,
    // 'imageWidth' specifies single icon width in pixels; returns TRUE on success, otherwise FALSE
    // note: PNG must be a strip of icons one row high
    // note: PNG should be compressed using PNGSlim, see https://forum.altap.cz/viewtopic.php?f=15&t=3278
    virtual BOOL WINAPI CreateFromPNG(HINSTANCE hInstance, LPCTSTR lpBitmapName, int imageWidth) = 0;

    // replaces icon at given index with icon 'hIcon'; returns TRUE on success, otherwise FALSE
    virtual BOOL WINAPI ReplaceIcon(int index, HICON hIcon) = 0;

    // creates icon from given index and returns its handle; caller is responsible for its destruction
    // (calling DestroyIcon(hIcon)); returns NULL on failure
    virtual HICON WINAPI GetIcon(int index) = 0;

    // creates based on PNG provided in memory; 'rawPNG' is pointer to memory containing PNG
    // (for example loaded from file) and 'rawPNGSize' specifies PNG memory size in bytes,
    // 'imageWidth' specifies single icon width in pixels; returns TRUE on success, otherwise FALSE
    // note: PNG must be a strip of icons one row high
    // note: PNG should be compressed using PNGSlim, see https://forum.altap.cz/viewtopic.php?f=15&t=3278
    virtual BOOL WINAPI CreateFromRawPNG(const void* rawPNG, DWORD rawPNGSize, int imageWidth) = 0;

    // creates as a copy of another (created) icon list; if 'grayscale' is TRUE,
    // conversion to grayscale version is also performed; returns TRUE on success, otherwise FALSE
    virtual BOOL WINAPI CreateAsCopy(const CGUIIconListAbstract* iconList, BOOL grayscale) = 0;

    // creates HIMAGELIST, returns its handle or NULL on failure
    // returned imagelist needs to be destroyed after use via API ImageList_Destroy()
    virtual HIMAGELIST WINAPI GetImageList() = 0;
};

//
// ****************************************************************************
// CGUIToolbarHeaderAbstract
//
// Helper bar placed above a list (e.g. HotPaths configuration, UserMenu),
// which can contain toolbar with buttons for list control on the right.
//
// All methods can only be called from the thread of the window where
// the object was attached to the windows control.
//

// Bit masks for EnableToolbar() and CheckToolbar()
#define TLBHDRMASK_MODIFY 0x01
#define TLBHDRMASK_NEW 0x02
#define TLBHDRMASK_DELETE 0x04
#define TLBHDRMASK_SORT 0x08
#define TLBHDRMASK_UP 0x10
#define TLBHDRMASK_DOWN 0x20

// Button identification for WM_COMMAND, see SetNotifyWindow()
#define TLBHDR_MODIFY 1
#define TLBHDR_NEW 2
#define TLBHDR_DELETE 3
#define TLBHDR_SORT 4
#define TLBHDR_UP 5
#define TLBHDR_DOWN 6
// Item count
#define TLBHDR_COUNT 6

class CGUIToolbarHeaderAbstract
{
public:
    // by default all buttons are enabled; after calling this method only buttons
    // corresponding to 'enableMask' will be enabled, which consists of one or more
    // (summed) TLBHDRMASK_xxx values
    virtual void WINAPI EnableToolbar(DWORD enableMask) = 0;

    // by default all buttons are unpressed; after calling this method buttons
    // corresponding to 'checkMask' will be pressed, which consists of one or more
    // (summed) TLBHDRMASK_xxx values
    virtual void WINAPI CheckToolbar(DWORD checkMask) = 0;

    // by calling this method the caller specifies window 'hWnd', to which WM_COMMAND
    // from ToolbarHeader will be delivered; LOWORD(wParam) will contain 'ctrlID' from
    // AttachToolbarHeader() call and HIWORD(wParam) is one of TLBHDR_xxx values (according
    // to the button that user clicked)
    // note: this method needs to be called only in special situations when messages
    // should be delivered to a window other than parent, where messages are delivered by default
    virtual void WINAPI SetNotifyWindow(HWND hWnd) = 0;
};

//
// ****************************************************************************
// CSalamanderGUIAbstract
//

#define BTF_CHECKBOX 0x00000001    // button behaves like a checkbox
#define BTF_DROPDOWN 0x00000002    // button contains drop down section at the back, sends WM_USER_BUTTONDROPDOWN message to parent
#define BTF_LBUTTONDOWN 0x00000004 // button reacts to LBUTTONDOWN and sends WM_USER_BUTTON
#define BTF_RIGHTARROW 0x00000008  // button has an arrow pointing right at the end
#define BTF_MORE 0x00000010        // button has a symbol for expanding the dialog at the end

class CSalamanderGUIAbstract
{
public:
    ///////////////////////////////////////////////////////////////////////////
    //
    // ProgressBar
    //
    // Used to display operation status as percentage of work already done.
    // Has meaning for operations that can take a significant amount of time. There
    // progress is better than just a WAIT cursor.
    //
    // attaches Salamander progress bar to Windows control (this control determines position
    // of progress bar); 'hParent' is handle of parent window (dialog or window); ctrlID is ID
    // of Windows control; on successful attachment returns progress bar interface,
    // otherwise returns NULL; interface is valid until destruction (delivery of WM_DESTROY)
    // of Windows control; after attachment progress bar is set to 0%;
    // frame is drawn in its own mode, so do not assign flags SS_WHITEFRAME | WS_BORDER to control
    virtual CGUIProgressBarAbstract* WINAPI AttachProgressBar(HWND hParent, int ctrlID) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // StaticText
    //
    // Used to display non-standard text in dialogs (bold, underlined),
    // text that changes rapidly and would flicker or text with unpredictable length,
    // which needs to be intelligently shortened.
    //
    // attaches Salamander StaticText to Windows control (this control determines position
    // of StaticText); 'hParent' is handle of parent window (dialog or window); ctrlID is ID;
    // 'flags' is from STF_* family, can be 0 or any combination of values;
    // on successful attachment returns StaticText interface,
    // otherwise returns NULL; interface is valid until destruction (delivery of WM_DESTROY)
    // of Windows control; after attachment text and alignment are extracted from Windows control.
    // tested on Windows control "STATIC"
    virtual CGUIStaticTextAbstract* WINAPI AttachStaticText(HWND hParent, int ctrlID, DWORD flags) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // HyperLink
    //
    // Used to display a hyperlink. After clicking it is possible to open URL address
    // or launch file (SetActionOpen), or have a command posted back
    // to the dialog (SetActionPostCommand).
    // Control is accessible via TAB key (can have focus), but WS_TABSTOP needs to be
    // set for it. Action is then triggered by Space key. Right mouse button
    // or Shift+F10 can invoke a menu with option to copy control text
    // to clipboard.
    //
    // attaches Salamander HyperLink to Windows control (this control determines position
    // of HyperLink); 'hParent' is handle of parent window (dialog or window); ctrlID is ID of Windows control;
    // 'flags' is from STF_* family, can be 0 or any combination of values;
    // recommended combination for HyperLink is STF_UNDERLINE | STF_HYPERLINK_COLOR
    // on successful attachment returns HyperLink interface, otherwise returns NULL; interface is
    // valid until destruction (delivery of WM_DESTROY) of Windows control; after attachment
    // text and alignment are extracted from Windows control.
    // tested on Windows control "STATIC"
    virtual CGUIHyperLinkAbstract* WINAPI AttachHyperLink(HWND hParent, int ctrlID, DWORD flags) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // Button
    //
    // Used to create a button with text or icon. Button can contain an arrow
    // on the right or drop-down arrow. See BTF_xxx flags.
    //
    // attaches Salamander TextArrowButton to Windows control (this control determines position,
    // text or icon and generated command); 'hParent' is handle of parent window (dialog or window);
    // ctrlID is ID of Windows control;
    // on successful attachment returns CGUIButtonAbstract interface, otherwise returns NULL; interface is
    // valid until destruction (delivery of WM_DESTROY) of Windows control;
    // Tested on Windows control "BUTTON".
    virtual CGUIButtonAbstract* WINAPI AttachButton(HWND hParent, int ctrlID, DWORD flags) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // ColorArrowButton
    //
    // Used to create a button with a colored rectangle, followed by an arrow pointing right.
    // (if showArrow==TRUE)
    // Text is displayed in the rectangle, which can have a different color than the rectangle background.
    // Used in color configurations, where it can display one or two colors.
    // After pressing, a popup menu is expanded with option to choose which color to configure.
    //
    // attaches Salamander ColorArrowButton to Windows control (this control determines position,
    // text and command of ColorArrowButton); 'hParent' is handle of parent window (dialog or window);
    // ctrlID is ID of Windows control;
    // on successful attachment returns ColorArrowButton interface, otherwise returns NULL; interface is
    // valid until destruction (delivery of WM_DESTROY) of Windows control;
    // Tested on Windows control "BUTTON".
    virtual CGUIColorArrowButtonAbstract* WINAPI AttachColorArrowButton(HWND hParent, int ctrlID, BOOL showArrow) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // ChangeToArrowButton
    //
    // Used to create a button with an arrow pointing right placed in the center
    // of the button. Inserted after input field and after pressing, a popup menu
    // is expanded next to the button containing items that can be inserted into the input field (form of hint).
    //
    // Changes button style so it can hold an icon with arrow and then assigns
    // this icon. Does not attach any Salamander object to the control, because
    // everything is handled by the operating system. Returns TRUE on success, otherwise FALSE.
    // Button text is ignored.
    // Tested on Windows control "BUTTON".
    virtual BOOL WINAPI ChangeToArrowButton(HWND hParent, int ctrlID) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // MenuPopup
    //
    // Used to create an empty popup menu. Returns pointer to interface or
    // NULL on error.
    virtual CGUIMenuPopupAbstract* WINAPI CreateMenuPopup() = 0;
    // Used to free allocated menu.
    virtual BOOL WINAPI DestroyMenuPopup(CGUIMenuPopupAbstract* popup) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // MenuBar
    //
    // Used to create menu bar; 'menu' items will be displayed in menu bar,
    // their children will be submenus; 'hNotifyWindow' identifies window to which
    // commands and notifications will be sent. Returns pointer to interface or NULL
    // on error.
    virtual CGUIMenuBarAbstract* WINAPI CreateMenuBar(CGUIMenuPopupAbstract* menu, HWND hNotifyWindow) = 0;
    // Used to free allocated menu bar. Also destroys the window.
    virtual BOOL WINAPI DestroyMenuBar(CGUIMenuBarAbstract* menuBar) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // ToolBar support
    //
    // Used to create inactive (gray) version of bitmaps for menu or toolbar.
    // From source bitmap 'hSource' creates grayscale bitmap 'hGrayscale'
    // and black-and-white mask 'hMask'. Color 'transparent' is considered transparent.
    // On success returns TRUE and 'hGrayscale' and 'hMask'; on error returns FALSE.
    virtual BOOL WINAPI CreateGrayscaleAndMaskBitmaps(HBITMAP hSource, COLORREF transparent,
                                                      HBITMAP& hGrayscale, HBITMAP& hMask) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // ToolBar
    //
    // Used to create tool bar; 'hNotifyWindow' identifies window to which
    // commands and notifications will be sent. Returns pointer to interface or NULL
    // on error.
    virtual CGUIToolBarAbstract* WINAPI CreateToolBar(HWND hNotifyWindow) = 0;
    // Used to free allocated tool bar. Also destroys the window.
    virtual BOOL WINAPI DestroyToolBar(CGUIToolBarAbstract* toolBar) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // ToolTip
    //
    // This method starts a timer and if it is not called again before expiration
    // requests text from window 'hNotifyWindow' via WM_USER_TTGETTEXT message,
    // which is then displayed below the cursor at its current coordinates.
    // Variable 'id' serves to distinguish areas when communicating with window 'hNotifyWindow'.
    // If this method is called multiple times with the same 'id' parameter, these
    // subsequent calls will be ignored.
    // Value 0 for 'hNotifyWindow' parameter is reserved for hiding window and interrupting
    // running timer.
    virtual void WINAPI SetCurrentToolTip(HWND hNotifyWindow, DWORD id) = 0;
    // suppresses tooltip display at current mouse coordinates
    // useful to call when activating window where tooltips are used
    // this prevents unwanted tooltip display
    virtual void WINAPI SuppressToolTipOnCurrentMousePos() = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // XP Visual Styles
    //
    // If called under operating system supporting visual styles,
    // calls SetWindowTheme(hWindow, L" ", L" ") to disable visual styles
    // for window 'hWindow'
    // returns TRUE if operating system supports visual styles, otherwise returns FALSE
    virtual BOOL WINAPI DisableWindowVisualStyles(HWND hWindow) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // IconList
    //
    // Two methods for allocation and destruction of IconList object serving to hold
    // 32bpp icons (3 x 8 bits for color and 8 bits for alpha transparency)
    // For other operations on IconList see CGUIIconListAbstract description
    virtual CGUIIconListAbstract* WINAPI CreateIconList() = 0;
    virtual BOOL WINAPI DestroyIconList(CGUIIconListAbstract* iconList) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // ToolTip support
    //
    // Searches 'buf' for first occurrence of '\t' character. If 'stripHotKey' is TRUE, the
    // string is terminated at this character. Otherwise a space is inserted in its place and
    // the rest of the text is parenthesized. Buffer 'buf' must be large enough when 'stripHotKey'
    // is FALSE to allow the text in buffer to be extended by two characters (parenthesizing).
    virtual void WINAPI PrepareToolTipText(char* buf, BOOL stripHotKey) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // Subject with file/dir name truncated if needed
    //
    // Sets text created as sprintf(, subjectFormatString, fileName) to static 'subjectWnd'.
    // Format string 'subjectFormatString' must contain exactly one '%s' (at the place of
    // 'fileName' insertion). If text would exceed static length, it gets shortened by truncating
    // 'fileName'. Additionally performs conversion of 'fileName' according to SALCFG_FILENAMEFORMAT
    // (to match how 'fileName' is displayed in panel) via CSalamanderGeneralAbstract::AlterFileName.
    // If it's a file, 'isDir' will be FALSE, otherwise TRUE. If static 'subjectWnd' has SS_NOPREFIX,
    // 'duplicateAmpersands' will be FALSE, otherwise TRUE (duplicates second and subsequent ampersands
    // ('&'), first ampersand marks hotkey in subject and must be contained in 'subjectFormatString' before '%s').
    // Usage example: SetSubjectTruncatedText(GetDlgItem(HWindow, IDS_SUBJECT), "&Rename %s to",
    //                                          file->Name, fileIsDir, TRUE)
    // can be called from any thread
    virtual void WINAPI SetSubjectTruncatedText(HWND subjectWnd, const char* subjectFormatString, const char* fileName,
                                                BOOL isDir, BOOL duplicateAmpersands) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // ToolbarHeader
    //
    // Used to create a header above a list (whether listview or listbox), which contains
    // text description and a group of buttons on the right side. Example can be seen in Salamander
    // configuration, see Hot Paths or User Menu. 'hParent' is dialog handle, 'ctrlID' is ID of static text,
    // around which ToolbarHeader will be created, 'hAlignWindow' is handle of list to which
    // the header will be aligned, 'buttonMask' is one or (sum of) more TLBHDRMASK_xxx values
    // and specifies which buttons will be displayed in the header.
    virtual CGUIToolbarHeaderAbstract* WINAPI AttachToolbarHeader(HWND hParent, int ctrlID, HWND hAlignWindow, DWORD buttonMask) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // ArrangeHorizontalLines
    //
    // Finds horizontal lines in dialog 'hWindow' and aligns them from the right to the static text
    // or checkbox or radiobox they connect to. Additionally finds checkboxes and
    // radioboxes that form labels for groupboxes and shortens them according to their text and
    // current font in the dialog. Eliminates unnecessary spaces created due to different
    // screen DPI.
    virtual void WINAPI ArrangeHorizontalLines(HWND hWindow) = 0;

    ///////////////////////////////////////////////////////////////////////////
    //
    // GetWindowFontHeight
    //
    // for 'hWindow' gets current font via WM_GETFONT and returns its height
    // via GetObject()
    virtual int WINAPI GetWindowFontHeight(HWND hWindow) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_gui)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
