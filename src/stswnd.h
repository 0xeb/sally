// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

#include <string>

//
// ****************************************************************************

class CMainToolBar;

enum CBorderLines
{
    blNone = 0x00,
    blTop = 0x01,
    blBottom = 0x02
};

enum CSecurityIconState
{
    sisNone = 0x00,      // icon not shown
    sisUnsecured = 0x01, // unlocked lock icon shown
    sisSecured = 0x02    // locked lock icon shown
};

/*
enum
{
  otStatusWindow = otLastWinLibObject
};
*/

//
// CHotTrackItem
//
// item contains the index of the first character, number of characters, offset of the first
// character in pixels, and their length in pixels; for the displayed path a list of these
// items is created and kept in an array
//
// for path "\\john\c\winnt
//
// these items are created:
//
// (0, 9,  0, length of first nine characters)   = \\john\c\
// (0, 14, 0, length of 14 characters)          = \\john\c\winnt
//
// for "DIR: 12"
//
// (0, 3, 0, length of three characters DIR)
// (5, 2, pixel offset "12", length of two characters "12")

struct CHotTrackItem
{
    WORD Offset;       // offset of the first character in characters
    WORD Chars;        // number of characters
    WORD PixelsOffset; // offset of the first character in pixels
    WORD Pixels;       // length in pixels
};

class CStatusWindow : public CWindow
{
public:
    CMainToolBar* ToolBar;
    CFilesWindow* FilesWindow;

protected:
    TDirectArray<CHotTrackItem> HotTrackItems;
    BOOL HotTrackItemsMeasured;

    int Border; // separator line at top/bottom
    char* Text;
    int TextLen; // number of characters in 'Text' without terminator
    char* Size;
    int PathLen;          // -1 (path is the whole Text), otherwise path length in Text (rest is filter)
    BOOL History;         // show arrow between text and size?
    BOOL Hidden;          // show filter symbol?
    int HiddenFilesCount; // number of filtered files
    int HiddenDirsCount;  // and directories
    BOOL WholeTextVisible;

    BOOL ShowThrobber;             // TRUE if the 'progress' throbber should be shown after text/hidden filter (independent of window existence)
    BOOL DelayedThrobber;          // TRUE if timer for showing throbber is already running
    DWORD DelayedThrobberShowTime; // GetTickCount() value when delayed throbber should be shown (0 = not delayed)
    BOOL Throbber;                 // show 'progress' throbber after text/hidden filter? (TRUE only if window exists)
    int ThrobberFrame;             // index aktualniho policka animace
    std::string ThrobberTooltip;   // if empty, it will not be shown
    int ThrobberID;                // throbber identification number (-1 = invalid)

    CSecurityIconState Security;
    std::string SecurityTooltip; // if empty, it will not be shown

    int Allocated;
    int* AlpDX; // array of lengths (from 0th to Xth character in the string)
    BOOL Left;

    int ToolBarWidth; // current toolbar width

    int EllipsedChars; // number of omitted characters after root; otherwise -1
    int EllipsedWidth; // length of omitted string after root; otherwise -1

    CHotTrackItem* HotItem;     // highlighted item
    CHotTrackItem* LastHotItem; // last highlighted item
    BOOL HotSize;               // size item is highlighted
    BOOL HotHistory;            // history item is highlighted
    BOOL HotZoom;               // zoom item is highlighted
    BOOL HotHidden;             // filter symbol is highlighted
    BOOL HotSecurity;           // lock symbol is highlighted

    RECT TextRect;     // where we drew the text
    RECT HiddenRect;   // where we drew the filter symbol
    RECT SizeRect;     // where we drew the size text
    RECT HistoryRect;  // where we drew the history dropdown
    RECT ZoomRect;     // where we drew the zoom dropdown
    RECT ThrobberRect; // where we drew the throbber
    RECT SecurityRect; // where we drew the lock
    int MaxTextRight;
    BOOL MouseCaptured;
    BOOL RButtonDown;
    BOOL LButtonDown;
    POINT LButtonDownPoint; // where the user pressed LButton

    int Height;
    int Width; // dimensions

    BOOL NeedToInvalidate; // for SetAutomatic() - change occurred, need to repaint?

    DWORD* SubTexts;     // DWORD array: LOWORD position, HIWORD length
    DWORD SubTextsCount; // number of items in SubTexts array

    IDropTarget* IDropTargetPtr;

public:
    CStatusWindow(CFilesWindow* filesWindow, int border, CObjectOrigin origin = ooAllocated);
    ~CStatusWindow();

    BOOL SetSubTexts(DWORD* subTexts, DWORD subTextsCount);
    // sets 'text' in the status line, 'pathLen' defines the path length (rest is filter),
    // if 'pathLen' is not used (path is the full 'text') it equals -1
    BOOL SetText(const char* text, int pathLen = -1);

    // builds HotTrackItems array: for disks and archivers based on backslashes
    // and for FS it asks the plugin
    void BuildHotTrackItems();

    void GetHotText(char* buffer, int bufSize);

    void DestroyWindow();

    int GetToolBarWidth() { return ToolBarWidth; }

    int GetNeededHeight();
    void SetSize(const CQuadWord& size);
    void SetHidden(int hiddenFiles, int hiddenDirs);
    void SetHistory(BOOL history);
    void SetThrobber(BOOL show, int delay = 0, BOOL calledFromDestroyWindow = FALSE); // call only from the main (GUI) thread, same as other methods
    // sets text shown as tooltip when hovering the throbber, the object makes a copy
    // if NULL, the tooltip will not be shown
    void SetThrobberTooltip(const char* throbberTooltip);
    int ChangeThrobberID(); // changes ThrobberID and returns its new value
    BOOL IsThrobberVisible(int throbberID) { return ShowThrobber && ThrobberID == throbberID; }
    void HideThrobberAndSecurityIcon();

    void SetSecurity(CSecurityIconState iconState);
    void SetSecurityTooltip(const char* tooltip);

    void InvalidateIfNeeded();

    void LayoutWindow();
    void Paint(HDC hdc, BOOL highlightText = FALSE, BOOL highlightHotTrackOnly = FALSE);
    void Repaint(BOOL flashText = FALSE, BOOL hotTrackOnly = FALSE);
    void InvalidateAndUpdate(BOOL update); // can be called even for HWindow == NULL
    void FlashText(BOOL hotTrackOnly = FALSE);

    BOOL FindHotTrackItem(int xPos, int& index);

    void SetLeftPanel(BOOL left);
    BOOL ToggleToolBar();

    BOOL IsLeft() { return Left; }

    BOOL SetDriveIcon(HICON hIcon);     // icon is copied into imagelist - destruction must be handled by caller
    void SetDrivePressed(BOOL pressed); // zamackne drive ikonku

    BOOL GetTextFrameRect(RECT* r);   // returns rectangle around text in screen coordinates
    BOOL GetFilterFrameRect(RECT* r); // returns rectangle around filter symbol in screen coordinates

    // display color depth may have changed; need to rebuild CacheBitmap
    void OnColorsChanged();

    void SetFont();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void RegisterDragDrop();
    void RevokeDragDrop();

    // creates imagelist with one item, used for displaying drag progress
    // after drag ends this imagelist must be released
    // input is a point for which dxHotspot and dyHotspot offsets are computed
    HIMAGELIST CreateDragImage(const char* text, int& dxHotspot, int& dyHotspot, int& imgWidth, int& imgHeight);

    void PaintThrobber(HDC hDC);
    //    void RepaintThrobber();

    void PaintSecurity(HDC hDC);
};
