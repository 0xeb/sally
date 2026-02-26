// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//*****************************************************************************
//
// CToolBarItem
//

class CToolBar;
class CTBCustomizeDialog;

class CToolBarItem
{
protected:
    DWORD Style;    // TLBI_STYLE_xxx
    DWORD State;    // TLBI_STATE_xxx
    DWORD ID;       // command id
    char* Text;     // allocated string
    int TextLen;    // length of string
    int ImageIndex; // Image index of the item. Set this member to -1 to
                    // indicate that the button does not have an image.
                    // The button layout will not include any space for
                    // a bitmap, only text.
    HICON HIcon;
    HICON HOverlay;
    DWORD CustomData; // FIXME_X64 - too small for a pointer, is it ever needed?
    int Width;        // width of item (computed if TLBI_STYLE_AUTOSIZE is set)

    char* Name; // name in customize dialog (valid during custimize session)

    // these values are used for optimized access to item states
    DWORD* Enabler; // points to variable that drives the item state.
                    // Non-zero means TLBI_STATE_GRAYED bit cleared.
                    // Zero means TLBI_STATE_GRAYED bit set.

    // internal data
    int Height; // height of item
    int Offset; // position of item in whole toolbar

    WORD IconX; // position of individual elements
    WORD TextX;
    WORD InnerX;
    WORD OutterX;

public:
    CToolBarItem();
    ~CToolBarItem();

    BOOL SetText(const char* text, int len = -1);

    friend class CToolBar;
    friend class CTBCustomizeDialog;
};

//*****************************************************************************
//
// CToolBar
//

class CBitmap;

class CToolBar : public CWindow, public CGUIToolBarAbstract
{
protected:
    TIndirectArray<CToolBarItem> Items;

    int Width; // overall window width
    int Height;
    HFONT HFont;
    int FontHeight;
    HWND HNotifyWindow; // where notifications are delivered
    HIMAGELIST HImageList;
    HIMAGELIST HHotImageList;
    int ImageWidth; // size of one image from the image list
    int ImageHeight;
    DWORD Style;          // TLB_STYLE_xxx
    BOOL DirtyItems;      // an operation affecting item layout occurred
                          // and a recompute is needed
    CBitmap* CacheBitmap; // back buffer used for drawing
    CBitmap* MonoBitmap;  // for grayed icons
    int CacheWidth;       // bitmap dimensions
    int CacheHeight;
    int HotIndex; // -1 = none
    int DownIndex;
    BOOL DropPressed;
    BOOL MonitorCapture;
    BOOL RelayToolTip;
    TOOLBAR_PADDING Padding;
    BOOL HasIcon;       // if there is an icon, GetNeededSpace() will include its height
    BOOL HasIconDirty;  // need to detect icon presence for GetNeededSpace()?
    BOOL Customizing;   // toolbar is currently being customized
    int InserMarkIndex; // -1 = none
    BOOL InserMarkAfter;
    BOOL MouseIsTracked;  // is the mouse tracked via TrackMouseEvent?
    DWORD DropDownUpTime; // time in [ms] when drop down was released, guarding against a re-press
    BOOL HelpMode;        // Salamander is in Shift+F1 (ctx help) mode and toolbar should highlight disabled items under cursor

public:
    //
    // Custom methods
    //
    CToolBar(HWND hNotifyWindow, CObjectOrigin origin = ooAllocated);
    ~CToolBar();

    //
    // Implementace metod CGUIToolBarAbstract
    //

    virtual BOOL WINAPI CreateWnd(HWND hParent);
    virtual HWND WINAPI GetHWND() { return HWindow; }

    virtual int WINAPI GetNeededWidth(); // returns width needed for the window
    virtual int WINAPI GetNeededHeight();

    virtual void WINAPI SetFont();
    virtual BOOL WINAPI GetItemRect(int index, RECT& r); // returns item position in screen coordinates

    virtual BOOL WINAPI CheckItem(DWORD position, BOOL byPosition, BOOL checked);
    virtual BOOL WINAPI EnableItem(DWORD position, BOOL byPosition, BOOL enabled);

    // if an image list is assigned, inserts the icon at the corresponding position
    // normal and hot variables specify which image lists are affected
    virtual BOOL WINAPI ReplaceImage(DWORD position, BOOL byPosition, HICON hIcon, BOOL normal = TRUE, BOOL hot = FALSE);

    virtual int WINAPI FindItemPosition(DWORD id);

    virtual void WINAPI SetImageList(HIMAGELIST hImageList);
    virtual HIMAGELIST WINAPI GetImageList();

    virtual void WINAPI SetHotImageList(HIMAGELIST hImageList);
    virtual HIMAGELIST WINAPI GetHotImageList();

    // toolbar style
    virtual void WINAPI SetStyle(DWORD style);
    virtual DWORD WINAPI GetStyle();

    virtual BOOL WINAPI RemoveItem(DWORD position, BOOL byPosition);
    virtual void WINAPI RemoveAllItems();

    virtual int WINAPI GetItemCount() { return Items.Count; }

    // invokes configuration dialog
    virtual void WINAPI Customize();

    virtual void WINAPI SetPadding(const TOOLBAR_PADDING* padding);
    virtual void WINAPI GetPadding(TOOLBAR_PADDING* padding);

    // walks all items and if they have the 'EnablerData' pointer set
    // compares the value (it points to) with the actual item state.
    // If the state differs, it updates it.
    virtual void WINAPI UpdateItemsState();

    // if the point is above an item (not a separator), returns its index.
    // otherwise returns a negative number
    virtual int WINAPI HitTest(int xPos, int yPos);

    // returns TRUE if the position is on an item boundary; then also sets 'index'
    // to that item and the 'after' variable, which indicates whether it is the left or
    // right side of the item. If the point is above an item, returns FALSE.
    // If the point is not above any item, returns TRUE and sets 'index' to -1.
    virtual BOOL WINAPI InsertMarkHitTest(int xPos, int yPos, int& index, BOOL& after);

    // sets InsertMark to index position (before or after)
    // if index == -1, removes InsertMark
    virtual void WINAPI SetInsertMark(int index, BOOL after);

    // Sets the hot item in a toolbar. Returns the index of the previous hot item, or -1 if there was no hot item.
    virtual int WINAPI SetHotItem(int index);

    // screen color depth may have changed; CacheBitmap must be rebuilt
    virtual void WINAPI OnColorsChanged();

    virtual BOOL WINAPI InsertItem2(DWORD position, BOOL byPosition, const TLBI_ITEM_INFO2* tii);
    virtual BOOL WINAPI SetItemInfo2(DWORD position, BOOL byPosition, const TLBI_ITEM_INFO2* tii);
    virtual BOOL WINAPI GetItemInfo2(DWORD position, BOOL byPosition, TLBI_ITEM_INFO2* tii);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void DrawDropDown(HDC hDC, int x, int y, BOOL grayed);
    void DrawItem(int index);
    void DrawItem(HDC hDC, int index);
    void DrawAllItems(HDC hDC);

    void DrawInsertMark(HDC hDC);

    // returns TRUE if there is an item at the position; then also sets 'index'
    // otherwise returns FALSE
    // if the user clicked drop down, sets 'dropDown' to TRUE
    BOOL HitTest(int xPos, int yPos, int& index, BOOL& dropDown);

    // walks all items and computes their 'MinWidth' and 'XOffset'
    // follows (and sets) DirtyItems
    // returns TRUE if all items were redrawn
    BOOL Refresh();

    friend class CTBCustomizeDialog;
};

//*****************************************************************************
//
// CTBCustomizeDialog
//

class CTBCustomizeDialog : public CCommonDialog
{
    enum TBCDDragMode
    {
        tbcdDragNone,
        tbcdDragAvailable,
        tbcdDragCurrent,
    };

protected:
    TDirectArray<TLBI_ITEM_INFO2> AllItems; // all available items
    CToolBar* ToolBar;
    HWND HAvailableLB;
    HWND HCurrentLB;
    DWORD DragNotify;
    TBCDDragMode DragMode;
    int DragIndex;

public:
    CTBCustomizeDialog(CToolBar* toolBar);
    ~CTBCustomizeDialog();
    BOOL Execute();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void DestroyItems();
    BOOL EnumButtons(); // via WM_USER_TBENUMBUTTON2 notification fills Items array with all buttons the toolbar can hold

    BOOL PresentInToolBar(DWORD id);      // is this command in the toolbar?
    BOOL FindIndex(DWORD id, int* index); // finds command in AllItems
    void FillLists();                     // fills both list boxes

    void EnableControls();
    void MoveItem(int srcIndex, int tgtIndex);
    void OnAdd();
    void OnRemove();
    void OnUp();
    void OnDown();
    void OnReset();
};

//*****************************************************************************
//
// CMainToolBar
//
// Toolbar that can be customized, carries command buttons. It sits on top
// of Salamander and above each panel.
//

enum CMainToolBarType
{
    mtbtTop,
    mtbtMiddle,
    mtbtLeft,
    mtbtRight,
};

class CMainToolBar : public CToolBar
{
protected:
    CMainToolBarType Type;

public:
    CMainToolBar(HWND hNotifyWindow, CMainToolBarType type, CObjectOrigin origin = ooStatic);

    BOOL Load(const char* data);
    BOOL Save(char* data);

    // needs to return tooltip
    void OnGetToolTip(LPARAM lParam);
    // during configuration fills the dialog with items
    BOOL OnEnumButton(LPARAM lParam);
    // user pressed reset in the configuration dialog - load the default layout
    void OnReset();

    void SetType(CMainToolBarType type);

protected:
    // fills 'tii' with data for item 'tbbeIndex' and returns TRUE
    // if the item is incomplete (command removed), returns FALSE
    BOOL FillTII(int tbbeIndex, TLBI_ITEM_INFO2* tii, BOOL fillName); // 'buttonIndex' is from TBBE_xxxx family; -1 = separator
};

//*****************************************************************************
//
// CBottomToolBar
//
// toolbar at the bottom of Salamander - contains hints for F1-F12 in
// combination with Ctrl, Alt and Shift
//

enum CBottomTBStateEnum
{
    btbsNormal,
    btbsAlt,
    btbsCtrl,
    btbsShift,
    btbsCtrlShift,
    //  btbsCtrlAlt,
    btbsAltShift,
    //  btbsCtrlAltShift,
    btbsMenu,
    btbsCount
};

class CBottomToolBar : public CToolBar
{
public:
    CBottomToolBar(HWND hNotifyWindow, CObjectOrigin origin = ooStatic);

    virtual BOOL WINAPI CreateWnd(HWND hParent);

    // called on every modifier change (Ctrl, Alt, Shift) - walks the filled
    // toolbar and sets its texts and IDs
    BOOL SetState(CBottomTBStateEnum state);

    // initializes the static array from which we feed the toolbar
    static BOOL InitDataFromResources();

    void OnGetToolTip(LPARAM lParam);

    virtual void WINAPI SetFont();

protected:
    CBottomTBStateEnum State;

    // internal function called from InitDataFromResources
    static BOOL InitDataResRow(CBottomTBStateEnum state, int textResID);

    // for each button finds the longest text and sets button width accordingly
    BOOL SetMaxItemWidths();
};

//*****************************************************************************
//
// CUserMenuBar
//

class CUserMenuBar : public CToolBar
{
public:
    CUserMenuBar(HWND hNotifyWindow, CObjectOrigin origin = ooStatic);

    // pulls items from UserMenu and loads buttons into the toolbar
    BOOL CreateButtons();

    void ToggleLabels();
    virtual int WINAPI GetNeededHeight();

    virtual void WINAPI Customize();

    virtual void WINAPI SetInsertMark(int index, BOOL after);
    virtual int WINAPI SetHotItem(int index);

    void OnGetToolTip(LPARAM lParam);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//*****************************************************************************
//
// CHotPathsBar
//

class CHotPathsBar : public CToolBar
{
public:
    CHotPathsBar(HWND hNotifyWindow, CObjectOrigin origin = ooStatic);

    // pulls items from HotPaths and loads buttons into the toolbar
    BOOL CreateButtons();

    void ToggleLabels();
    virtual int WINAPI GetNeededHeight();

    virtual void WINAPI Customize();

    //    void SetInsertMark(int index, BOOL after);
    //    int SetHotItem(int index);

    void OnGetToolTip(LPARAM lParam);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//*****************************************************************************
//
// CDriveBar
//

class CDrivesList;

class CDriveBar : public CToolBar
{
protected:
    // return values for List
    DWORD DriveType;
    DWORD_PTR DriveTypeParam;
    int PostCmd;
    void* PostCmdParam;
    BOOL FromContextMenu;
    CDrivesList* List;

    // cache: contains ?: or \\ for UNC or empty string
    char CheckedDrive[3];

public:
    // we want to display plugin icons in monochrome, so we keep them in image lists
    HIMAGELIST HDrivesIcons;
    HIMAGELIST HDrivesIconsGray;

public:
    CDriveBar(HWND hNotifyWindow, CObjectOrigin origin = ooStatic);
    ~CDriveBar();

    void DestroyImageLists();

    // clears existing and loads new buttons;
    // if 'copyDrivesListFrom' is not NULL, copy drive data instead of re-fetching
    // 'copyDrivesListFrom' can also refer to the called object
    BOOL CreateDriveButtons(CDriveBar* copyDrivesListFrom);

    virtual int WINAPI GetNeededHeight();

    void OnGetToolTip(LPARAM lParam);

    // user clicked the button with command id
    void Execute(DWORD id);

    // presses the icon corresponding to the path; if none is found, none is pressed;
    // the force variable invalidates the cache
    void SetCheckedDrive(CFilesWindow* panel, BOOL force = FALSE);

    // if a notification about drive add/remove arrives, the list must be rebuilt;
    // if 'copyDrivesListFrom' is not NULL, copy drive data instead of re-fetching
    // 'copyDrivesListFrom' can also refer to the called object
    void RebuildDrives(CDriveBar* copyDrivesListFrom = NULL);

    // need to show context menu; item is determined from GetMessagePos; returns TRUE
    // if a button was hit and the menu opened; otherwise returns FALSE
    BOOL OnContextMenu();

    // returns drive bitmask as obtained during last List->BuildData()
    // if BuildData() has not run yet, returns 0
    // can be used for quick detection of drive changes
    DWORD GetCachedDrivesMask();

    // returns available cloud storages bitmask as obtained during last List->BuildData()
    // if BuildData() has not run yet, returns 0
    // can be used for quick detection of cloud storages availability changes
    DWORD GetCachedCloudStoragesMask();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//*****************************************************************************
//
// CPluginsBar
//

class CPluginsBar : public CToolBar
{
protected:
    // icons representing plugins, created using CPlugins::CreateIconsList
    HIMAGELIST HPluginsIcons;
    HIMAGELIST HPluginsIconsGray;

public:
    CPluginsBar(HWND hNotifyWindow, CObjectOrigin origin = ooStatic);
    ~CPluginsBar();

    void DestroyImageLists();

    // clears existing and loads new buttons
    BOOL CreatePluginButtons();

    virtual int WINAPI GetNeededHeight();

    virtual void WINAPI Customize();

    void OnGetToolTip(LPARAM lParam);

    //  protected:
    //    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

extern void PrepareToolTipText(char* buff, BOOL stripHotKey);

extern void GetSVGIconsMainToolbar(CSVGIcon** svgIcons, int* svgIconsCount);
