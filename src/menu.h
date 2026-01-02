// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

extern const char* WC_POPUPMENU;

#define UPDOWN_ARROW_WIDTH 9
#define UPDOWN_ARROW_HEIGHT 5
#define UPDOWN_ITEM_HEIGHT 12

class CMenuSharedResources;
class CMenuPopup;
class CMenuBar;
class CBitmap;

/*
Sent messages:
  WM_INITMENUPOPUP
    hmenuPopup = (HMENU) wParam;         // handle to submenu
    uPos = (UINT) LOWORD(lParam);        // submenu item position
    fSystemMenu = (BOOL) HIWORD(lParam); // window menu flag

    This message is sent only for Windows menu popup.

  WM_USER_INITMENUPOPUP
  WM_USER_UNINITMENUPOPUP
    menuPopup = (CGUIMenuPopupAbstract*) wParam; // pointer to submenu
    uPos =      LOWORD(lParam);                // submenu item position
    uID =       HIWORD(lParam);                // submenu ID

    These two messages are always sent - even for menu built on top of Windows
    menu popup.
*/

//*****************************************************************************
//
// CMenuWindowQueue
//

class CMenuWindowQueue
{
private:
    TDirectArray<HWND> Data;
    CRITICAL_SECTION DataCriticalSection; // critical section for data access
    BOOL UsingData;

public:
    CMenuWindowQueue();
    ~CMenuWindowQueue();

    BOOL Add(HWND hWindow);    // adds item to queue, returns success
    void Remove(HWND hWindow); // removes item from queue
    void DispatchCloseMenu();  // sends WM_USER_CLOSEMENU message to all open menu windows
};

extern CMenuWindowQueue MenuWindowQueue;

//*****************************************************************************
//
// COldMenuHookTlsAllocator
//

class COldMenuHookTlsAllocator
{
public:
    COldMenuHookTlsAllocator();
    ~COldMenuHookTlsAllocator();

    HHOOK HookThread();
    void UnhookThread(HHOOK hOldHookProc);
};

extern COldMenuHookTlsAllocator OldMenuHookTlsAllocator;

//*****************************************************************************
//
// CMenuSharedResources
//
// Only one instance of these resources exists for one submenu tree.
// They are created for example in the Track function.
// All submenus then only receive a pointer to these shared resources.
//

class CMenuSharedResources
{
public:
    // colors
    COLORREF NormalBkColor;
    COLORREF SelectedBkColor;
    COLORREF NormalTextColor;
    COLORREF SelectedTextColor;
    COLORREF HilightColor;
    COLORREF GrayTextColor;

    // cache DC
    CBitmap* CacheBitmap;
    CBitmap* MonoBitmap;

    // temp DC
    HDC HTempMemDC;  // memory dc for temporary transfers
    HDC HTemp2MemDC; // memory dc for temporary transfers

    // fonts
    HFONT HNormalFont; // continuously selected in HCacheMemoryDC
    HFONT HBoldFont;   // selected only temporarily

    // menu bitmaps
    HBITMAP HMenuBitmaps; // extracted from system: order according to CMenuBitmapEnum
    int MenuBitmapWidth;

    // other
    HWND HParent;          // window from which menu was invoked
    int TextItemHeight;    // height of text item
    BOOL BitmapsZoom;      // multiple of original bitmap size
    DWORD ChangeTickCount; // GetTickCount value from when selected item changed
    POINT LastMouseMove;
    CMenuBar* MenuBar; // window is activated from MenuBar; otherwise equals NULL
    DWORD SkillLevel;  // value for popup chain -- determines which items will be displayed
    BOOL HideAccel;    // should accelerators be hidden

    const RECT* ExcludeRect; // this rectangle must not be covered

    HANDLE HCloseEvent; // serves to start message queue

public:
    CMenuSharedResources();
    ~CMenuSharedResources();

    BOOL Create(HWND hParent, int width, int height);
};

//*****************************************************************************
//
// CMenuItem
//

class CMenuItem
{
protected:
    DWORD Type;
    DWORD State;
    DWORD ID;
    CMenuPopup* SubMenu;
    HBITMAP HBmpChecked;
    HBITMAP HBmpUnchecked;
    HBITMAP HBmpItem;
    char* String;
    int ImageIndex;
    HICON HIcon;
    HICON HOverlay;
    ULONG_PTR CustomData;
    DWORD SkillLevel; // MENU_LEVEL_BEGINNER, MENU_LEVEL_INTERMEDIATE, MENU_LEVEL_ADVANCED
    // these values are used for optimized access to item states
    DWORD* Enabler; // Points to variable that controls item state.
                    // Non-zero value corresponds to cleared MENU_STATE_GRAYED bit.
                    // Zero corresponds to set MENU_STATE_GRAYED bit.
    DWORD Flags;    // MENU_FLAG_xxx
    DWORD Temp;     // helper variable for some methods

    // calculated values
    int Height;
    int MinWidth;
    int YOffset;

    const char* ColumnL1; // first column text
    int ColumnL1Len;      // character count
    int ColumnL1Width;
    int ColumnL1X;
    const char* ColumnL2; // second column text (can be NULL)
    int ColumnL2Len;      // character count
    int ColumnL2Width;
    int ColumnL2X;
    const char* ColumnR; // right column text (can be NULL)
    int ColumnRLen;      // character count
    int ColumnRWidth;
    int ColumnRX;

public:
    CMenuItem();
    ~CMenuItem();

    BOOL SetText(const char* text, int len = -1);

    // traverses TypeData string and according to separators and threeCol variable
    // sets variables ColumnL1 - ColumnR, ColumnL1Len - ColumnRLen,
    // ColumnL1Width - ColumnRWidth
    void DecodeSubTextLenghtsAndWidths(CMenuSharedResources* sharedRes, BOOL threeCol);

    friend class CMenuPopup;
    friend class CMenuBar;
};

//*****************************************************************************
//
// CMenuPopup
//

enum CMenuBitmapEnum
{
    menuBitmapArrowR,
    //  menuBitmapArrowL,
    //  menuBitmapArrowU,
    //  menuBitmapArrowD
};

enum CMenuPopupHittestEnum
{
    mphItem,            // on item, userData = item index
    mphUpArrow,         // on Up arrow
    mphDownArrow,       // on Down arrow
    mphBorderOrOutside, // on border or outside
    //  mphOutside, // outside window
};

/*
Items
  List of items contained in pop-up menu.

HParent
  Window to which notification messages will be delivered.

HImageList
  Icons displayed before items. Icon is determined by
  CMenuItem::ImageIndex variable.

HWindowsMenu
  Handle of Windows popup menu. Before opening this submenu
  enumeration of its items is performed. They are then transformed to temporary
  CMenuPopup object. After closing this submenu the temporary object is destroyed.
  For such menu notification messages WM_INITPOPUP, WM_DRAWITEM
  and WM_MEASUREITEM are sent.
*/

class CMenuPopup : public CWindow, public CGUIMenuPopupAbstract
{
protected:
    TIndirectArray<CMenuItem> Items;
    HMENU HWindowsMenu;

    RECT WindowRect;
    int TotalHeight; // total menu height; may not be fully displayed
    int Width;       // client area dimensions
    int Height;
    int TopItemY;           // y coordinate of first item
    BOOL UpArrowVisible;    // is up arrow displayed?
    BOOL UpDownTimerRunnig; // is timer running?
    BOOL DownArrowVisible;  // is down arrow displayed?
    DWORD Style;            // MENU_POPUP_xxxx
    DWORD TrackFlags;       // MENU_TRACK_xxxx
    CMenuSharedResources* SharedRes;
    CMenuPopup* OpenedSubMenu; // if any submenu is open, points to it
    CMenuPopup* FirstPopup;    // if not the first window, points to it; in case of first window points to itself
    int SelectedItemIndex;     // -1 == none
    BOOL SelectedByMouse;      // TRUE->ByMouse FALSE->ByKeyboard
    HIMAGELIST HImageList;
    HIMAGELIST HHotImageList;
    int ImageWidth; // dimensions of one image from HImageList
    int ImageHeight;
    DWORD ID;                  // copy of ID from CMenuItem
    BOOL Closing;              // HideAll was called and we're ending as soon as possible
    int MinWidth;              // when laying out width, width won't be less than this value
    BOOL ModifyMode;           // if menu is displayed, changes can't be made unless in ModifyMode
    DWORD SkillLevel;          // determines which items will be displayed in this popup
    int MouseWheelAccumulator; // vertical

public:
    //
    // Custom methods
    //

    CMenuPopup(DWORD id = 0);
    BOOL LoadFromTemplate2(HINSTANCE hInstance, const MENU_TEMPLATE_ITEM* menuTemplate, DWORD* enablersOffset, HIMAGELIST hImageList, HIMAGELIST hHotImageList, int* addedRows);

    //
    // Implementation of CGUIMenuPopupAbstract methods
    //

    virtual BOOL WINAPI LoadFromTemplate(HINSTANCE hInstance, const MENU_TEMPLATE_ITEM* menuTemplate, DWORD* enablersOffset, HIMAGELIST hImageList = NULL, HIMAGELIST hHotImageList = NULL);

    virtual void WINAPI SetSelectedItemIndex(int index); // serves to preset selected item (MENU_TRACK_SELECT flag must be set, otherwise not used)
    virtual int WINAPI GetSelectedItemIndex() { return SelectedItemIndex; }

    virtual void WINAPI SetTemplateMenu(HMENU hWindowsMenu) { HWindowsMenu = hWindowsMenu; }
    virtual HMENU WINAPI GetTemplateMenu() { return HWindowsMenu; }

    virtual CGUIMenuPopupAbstract* WINAPI GetSubMenu(DWORD position, BOOL byPosition);

    // The InsertItem method inserts a new menu item into a menu, moving other items
    // down the menu.
    //
    // Parameters:
    //
    // 'position'     [in] Identifier or position of the menu item before which to insert
    //                the new item. The meaning of this parameter depends on the
    //                value of 'byPosition'.
    //
    // 'byPosition'   [in] Value specifying the meaning of 'position'. If this parameter is FALSE,
    //                'position' is a menu item identifier. Otherwise, it is a menu item position.
    //                If 'byPosition' is TRUE and 'position' is -1, the new menu item is appended
    //                to the end of the menu.
    //
    // 'mii'          [in] Pointer to a MENU_ITEM_INFO structure that contains information about
    //                the new menu item.
    virtual BOOL WINAPI InsertItem(DWORD position, BOOL byPosition, const MENU_ITEM_INFO* mii);

    virtual BOOL WINAPI SetItemInfo(DWORD position, BOOL byPosition, const MENU_ITEM_INFO* mii);
    virtual BOOL WINAPI GetItemInfo(DWORD position, BOOL byPosition, MENU_ITEM_INFO* mii);
    virtual BOOL WINAPI SetStyle(DWORD style); // MENU_POPUP_xxxxx family
    virtual BOOL WINAPI CheckItem(DWORD position, BOOL byPosition, BOOL checked);
    virtual BOOL WINAPI CheckRadioItem(DWORD positionFirst, DWORD positionLast, DWORD positionCheck, BOOL byPosition);
    virtual BOOL WINAPI SetDefaultItem(DWORD position, BOOL byPosition);
    virtual BOOL WINAPI EnableItem(DWORD position, BOOL byPosition, BOOL enabled);
    virtual int WINAPI GetItemCount() { return Items.Count; }

    virtual void WINAPI RemoveAllItems();
    virtual BOOL WINAPI RemoveItemsRange(int firstIndex, int lastIndex);

    // allows making changes over open menu popup
    virtual BOOL WINAPI BeginModifyMode(); // start edit mode
    virtual BOOL WINAPI EndModifyMode();   // end mode - menu is redrawn

    // determines items that will be displayed in menu
    // 'skillLevel' can be one of MENU_LEVEL_BEGINNER, MENU_LEVEL_INTERMEDIATE and MENU_LEVEL_ADVANCED values
    virtual void WINAPI SetSkillLevel(DWORD skillLevel);

    // The FindItemPosition method finds a menu item position.
    //
    // Parameters:
    //
    // 'id'           [in] Identifier of the menu item
    //
    // Return Values:
    //
    // If the method succeeds, the return value is zero base index of the menu item.
    //
    // If menu item is not found, return value is -1.
    virtual int WINAPI FindItemPosition(DWORD id);

    virtual BOOL WINAPI FillMenuHandle(HMENU hMenu);
    virtual BOOL WINAPI GetStatesFromHWindowsMenu(HMENU hMenu);
    virtual void WINAPI SetImageList(HIMAGELIST hImageList, BOOL subMenu = FALSE); // if subMenu==TRUE, handle is set to submenu as well
    virtual HIMAGELIST WINAPI GetImageList();
    virtual void WINAPI SetHotImageList(HIMAGELIST hHotImageList, BOOL subMenu = FALSE);
    virtual HIMAGELIST WINAPI GetHotImageList();

    // The TrackPopupMenuEx function displays a shortcut menu at the specified location
    // and tracks the selection of items on the shortcut menu. The shortcut menu can
    // appear anywhere on the screen.
    //
    // Parameters:
    //
    // 'trackFlags'   [in] Use one of the following flags to specify how the function
    //                positions the shortcut menu horizontally: MENU_TRACK_xxxx
    //
    // 'x'            [in] Horizontal location of the shortcut menu, in screen coordinates.
    //
    // 'y'            [in] Vertical location of the shortcut menu, in screen coordinates.
    //
    // 'hwnd'         [in] Handle to the window that owns the shortcut menu. This window
    //                receives all messages from the menu. The window does not receive a
    //                WM_COMMAND message from the menu until the function returns.
    //                If you specify TPM_NONOTIFY in the fuFlags parameter, the function
    //                does not send messages to the window identified by hwnd. However,
    //                you still have to pass a window handle in hwnd. It can be any window
    //                handle from your application.
    //
    // 'exclude'      [in] Rectangle to exclude when positioning the menu, in screen
    //                coordinates. This parameter can be NULL.
    //
    // Return Values:
    //   If you specify TPM_RETURNCMD in the 'flags' parameter, the return value is the
    //   menu-item identifier of the item that the user selected. If the user cancels
    //   the menu without making a selection, or if an error occurs, then the return
    //   value is zero.
    //
    //   If you do not specify TPM_RETURNCMD in the 'flags' parameter, the return value
    //   is nonzero if the function succeeds and zero if it fails.
    virtual DWORD WINAPI Track(DWORD trackFlags, int x, int y, HWND hwnd, const RECT* exclude);

    virtual BOOL WINAPI GetItemRect(int index, RECT* rect); // returns bounding rectangle around item in screen coordinates

    // traverses all items and if they have 'EnablerData' pointer set
    // compares value (pointed to) with actual item state.
    // If state differs, changes it.
    virtual void WINAPI UpdateItemsState();

    virtual void WINAPI SetMinWidth(int minWidth);

    virtual void WINAPI SetPopupID(DWORD id);
    virtual DWORD WINAPI GetPopupID();
    virtual void WINAPI AssignHotKeys();

protected:
    void Cleanup(); // initializes object
    BOOL LoadFromHandle();
    void LayoutColumns(); // traverses items and sets values according to their dimensions
    DWORD GetOwnerDrawItemState(const CMenuItem* item, BOOL selected);
    void DrawCheckBitmapVista(HDC hDC, CMenuItem* item, int yOffset, BOOL selected); // draws with alpha blend
    void DrawCheckBitmap(HDC hDC, CMenuItem* item, int yOffset, BOOL selected);      // check marks provided by user (HBmpChecked and HBmpUnchecked)
    void DrawCheckImage(HDC hDC, CMenuItem* item, int yOffset, BOOL selected);       // standard checkmarks, ImageIndex, HIcon
    void DrawCheckMark(HDC hDC, CMenuItem* item, int yOffset, BOOL selected);        // calls corresponding function
    void DrawItem(HDC hDC, CMenuItem* item, int yOffset, BOOL selected);             // draws one item
    void DrawUpDownItem(HDC hDC, BOOL up);                                           // draws item containing up or down arrow
    CMenuPopupHittestEnum HitTest(const POINT* point, int* userData);

    BOOL FindNextItemIndex(int fromIndex, BOOL topToDown, int* index);
    inline CMenuPopup* FindActivePopup();       // finds last open popup; returns pointer to object
    inline CMenuPopup* FindPopup(HWND hWindow); // searches from us to last child; returns pointer to object or NULL
    inline void DoDispatchMessage(MSG* msg, BOOL* leaveMenu, DWORD* retValue, BOOL* dispatchLater);
    void OnTimerTimeout();
    void CheckSelectedPath(CMenuPopup* terminator); // traverses entire branch and sets SelectedItems to lead to last popup

    // adds to Track [in] menuBar
    //               [in] delayedMsg
    //               [in] dispatchDelayedMsg: Should delayedMsg be delivered after returning from this method?
    //
    DWORD TrackInternal(DWORD trackFlags, int x, int y, HWND hwnd, const RECT* exclude,
                        CMenuBar* menuBar, MSG& delayedMsg, BOOL& dispatchDelayedMsg);

    void CloseOpenedSubmenu();
    void HideAll();

    void PaintAllItems(HRGN hUpdateRgn);

    void OnKeyRight(BOOL* leaveMenu);
    void OnKeyReturn(BOOL* leaveMenu, DWORD* retValue);
    void OnChar(char key, BOOL* leaveMenu, DWORD* retValue);
    int FindNextItemIndex(int firstIndex, char key);

    // for navigation using PgDn/PgUp, searches for index of first item
    // after separator; if 'down' is TRUE, searches downward
    // from item 'firstIndex', otherwise searches upward
    int FindGroupIndex(int firstIndex, BOOL down);

    // if 'byMouse' is TRUE, change is made by mouse, otherwise by keyboard
    // select set from keyboard "holds" while user moves mouse outside popups
    void SelectNewItemIndex(int newItemIndex, BOOL byMouse);

    void EnsureItemVisible(int index); // if item lies outside displayed area, ensures
                                       // scrolling and drawing of items so it is fully
                                       // visible

    void OnMouseWheel(WPARAM wParam, LPARAM lParam);

    // x, y are coordinates of top left corner of window
    // submenuItemPos serves to send notification to application
    BOOL CreatePopupWindow(CMenuPopup* firstPopup, int x, int y, int submenuItemPos, const RECT* exclude);

    // Returns handle of popup window under cursor; if child window is under cursor,
    // its parent will be found.
    //
    // Introduced due to PicaView, which inserts child window into context menu,
    // into which it renders image with delay. In Salam 2.0 when hovering over such
    // image, menu item was deselected because WindowFromPoint returned
    // different window than popup.
    HWND PopupWindowFromPoint(POINT point);

    void ResetMouseWheelAccumulator() { MouseWheelAccumulator = 0; }

    LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    friend class CMenuItem;
    friend class CMenuBar;
};

//*****************************************************************************
//
// CMenuBar
//

class CMenuBar : public CWindow, public CGUIMenuBarAbstract
{
protected:
    CMenuPopup* Menu;
    int Width; // dimensions of entire window
    int Height;
    HFONT HFont;
    int FontHeight;
    int HotIndex;       // item that is either raised or pressed (none = -1)
    HWND HNotifyWindow; // where we will deliver notifications
    BOOL MenuLoop;      // are submenus being expanded
    DWORD RetValue;     // which command to send to application window
    MSG DelayedMsg;
    BOOL DispatchDelayedMsg;
    BOOL HotIndexIsTracked; // is popup open under HotIndex?
    BOOL HandlingVK_MENU;
    BOOL WheelDuringMenu;
    POINT LastMouseMove;
    BOOL Closing;        // WM_USER_CLOSEMENU was called, we're ending as soon as possible
    HANDLE HCloseEvent;  // serves to start message queue
    BOOL MouseIsTracked; // is mouse tracked using TrackMouseEvent?
    BOOL HelpMode;       // are we in Context Help mode (Shift+F1)?

    // these two variables serve for cooperation between MenuBar and MenuPopup
    // they are set in CMenuPopup::TrackInternal and determine further behavior
    // of MenuBar after closing Popup
    int IndexToOpen;     // if set to -1, no Popup should be opened,
                         // otherwise contains index of popup to open
    BOOL OpenWithSelect; // should first item be selected in opened menu?
    BOOL OpenByMouse;    // opened by mouse or keyboard?
    BOOL ExitMenuLoop;   // if TRUE, we end MenuLoop
    BOOL HelpMode2;      // did we receive WM_USER_HELP_MOUSEMOVE and are waiting for WM_USER_HELP_MOUSELEAVE? (we must highlight item under cursor)
    WORD UIState;        // displaying accelerators
    BOOL ForceAccelVisible;

public:
    //
    // Custom methods
    //
    CMenuBar(CMenuPopup* menu, HWND hNotifyWindow, CObjectOrigin origin = ooStatic);
    ~CMenuBar();

    //
    // Implementation of CGUIMenuBarAbstract methods
    //

    virtual BOOL WINAPI CreateWnd(HWND hParent);
    virtual HWND WINAPI GetHWND() { return HWindow; }

    virtual int WINAPI GetNeededWidth();                 // returns width that will be needed for window
    virtual int WINAPI GetNeededHeight();                // returns height that will be needed for window
    virtual void WINAPI SetFont();                       // gets font for menu bar from system
    virtual BOOL WINAPI GetItemRect(int index, RECT& r); // returns item position in screen coordinates
    virtual void WINAPI EnterMenu();                     // user pressed VK_MENU
    virtual BOOL WINAPI IsInMenuLoop() { return MenuLoop; }
    virtual void WINAPI SetHelpMode(BOOL helpMode) { HelpMode = helpMode; }

    // If the message is translated, the return value is TRUE.
    virtual BOOL WINAPI IsMenuBarMessage(CONST MSG* lpMsg);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void DrawItem(int index);
    void DrawItem(HDC hDC, int index, int x);
    void DrawAllItems(HDC hDC);
    void RefreshMinWidths(); // traverses all items and calculates their 'MinWidth'

    void TrackHotIndex();                                                  // presses HotIndex and calls TrackPopup; returns after its closing
    void EnterMenuInternal(int index, BOOL openWidthSelect, BOOL byMouse); // byMouse tells whether opening is via mouse or keyboard

    // returns TRUE if item is at position; then also sets 'index'
    // otherwise returns FALSE
    BOOL HitTest(int xPos, int yPos, int& index);

    // searches inserted submenus and returns TRUE if it finds any with hot
    // key 'hotKey'; also returns its index
    BOOL HotKeyIndexLookup(char hotKey, int& itemIndex);

    friend class CMenuPopup;
};

BOOL InitializeMenu();
void ReleaseMenu();

extern CMenuPopup MainMenu;
extern CMenuPopup ArchiveMenu;
extern CMenuPopup ArchivePanelMenu;

BOOL BuildSalamanderMenus();           // builds global menu for Salamander
BOOL BuildFindMenu(CMenuPopup* popup); // builds menu instance for find

// Adds items to 'popup' created based on 'buttonsID' array.
// 'hWindow' is parent of buttons referenced by constants in 'buttonsID' array.
// 'buttonsID' array can contain any number of values terminated by 0.
// Value -1 is reserved for separator and value -2 for default item (following
// item will have default state set). Other values are considered
// button ids. Their text is extracted and added to menu. Also
// their Enabled state is extracted and reflected in the menu item.
void FillContextMenuFromButtons(CMenuPopup* popup, HWND hWindow, int* buttonsID);
