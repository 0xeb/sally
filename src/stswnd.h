// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include "common/CumulativeTextWidth.h"
#include "common/unicode/WideTextRange.h"

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
    int Offset;       // offset of the first character in characters
    int Chars;        // number of characters
    int PixelsOffset; // offset of the first character in pixels
    int Pixels;       // length in pixels
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
    // One representation. A `char* Text` mirror used to sit here, maintained by
    // the wide SetText path via WideToAnsi and selected by a UseWideText flag - but SetText set that flag
    // TRUE in the same breath as writing the mirror, so every narrow arm in this file was
    // unreachable and the mirror was written and never read.
    std::wstring TextW;
    // Whether SetText has ever been called. This is NOT `!TextW.empty()`: the old code tested
    // `Text != NULL`, and SetText(L"") allocated a 1-byte buffer, so a deliberately blank
    // status line still counted as assigned. Paint() relies on that - it draws the throbber,
    // zoom, hidden and security chrome inside the same guard.
    BOOL TextAssigned;
    int TextLen; // number of characters in 'TextW' without terminator
    std::wstring Size;
    BOOL SizeAssigned;
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
    std::wstring ThrobberTooltip;  // if empty, it will not be shown
    int ThrobberID;                // throbber identification number (-1 = invalid)

    CSecurityIconState Security;
    std::wstring SecurityTooltip; // if empty, it will not be shown

    std::vector<int> AlpDX; // cumulative pixel lengths for the UTF-16 text

    // Pixel width of the first 'chars' characters, i.e. the x offset at which character 'chars'
    // starts. 'chars' is legitimately 0 - empty status text, or a hot-track item with no
    // characters of its own (see the item.Pixels assignments in SetText) - and AlpDX[0 - 1] on a
    // std::vector is an out-of-range index, not the harmless read-before-the-buffer it was when
    // AlpDX was a raw int array. Every caller must go through here rather than index AlpDX.
    int TextWidthBefore(int chars) const
    {
        return sally::text::CumulativeWidthBefore(AlpDX, chars);
    }

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

    std::vector<sally::unicode::WideTextRange> SubTexts;

    IDropTarget* IDropTargetPtr;

public:
    CStatusWindow(CFilesWindow* filesWindow, int border, CObjectOrigin origin = ooAllocated);
    ~CStatusWindow();

    BOOL SetSubTexts(const sally::unicode::WideTextRange* subTexts,
                     size_t subTextsCount);
    // sets 'text' in the status line, 'pathLen' defines the path length (rest is filter),
    // if 'pathLen' is not used (path is the full 'text') it equals -1
    BOOL SetText(const wchar_t* text, int pathLen = -1);

    // builds HotTrackItems array: for disks and archivers based on backslashes
    // and for FS it asks the plugin
    void BuildHotTrackItems();

    void GetHotTextW(std::wstring& buffer);

    void DestroyWindow();

    int GetToolBarWidth() { return ToolBarWidth; }

    int GetNeededHeight();
    void SetSize(const CQuadWord& size);
    void SetHidden(int hiddenFiles, int hiddenDirs);
    void SetHistory(BOOL history);
    void SetThrobber(BOOL show, int delay = 0, BOOL calledFromDestroyWindow = FALSE); // call only from the main (GUI) thread, same as other methods
    // sets text shown as tooltip when hovering the throbber, the object makes a copy
    // if NULL, the tooltip will not be shown
    void SetThrobberTooltipW(const wchar_t* throbberTooltip);
    int ChangeThrobberID(); // changes ThrobberID and returns its new value
    BOOL IsThrobberVisible(int throbberID) { return ShowThrobber && ThrobberID == throbberID; }
    void HideThrobberAndSecurityIcon();

    void SetSecurity(CSecurityIconState iconState);
    void SetSecurityTooltipW(const wchar_t* tooltip);

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
    HIMAGELIST CreateDragImage(const wchar_t* text, int& dxHotspot, int& dyHotspot, int& imgWidth, int& imgHeight);

    void PaintThrobber(HDC hDC);
    //    void RepaintThrobber();

    void PaintSecurity(HDC hDC);
};
