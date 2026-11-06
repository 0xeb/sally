// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/PanelTabs.h"

#include <string>
#include <vector>

class CMainWindow;

//
// ****************************************************************************
// CTabWindow
//
// A panel's tab strip, above its directory line: one tab per saved panel state, the active
// one in the directory line's colours. Click activates a tab, middle-click or the hover cross
// closes it, double-click on empty space opens a new one, the wheel cycles, right-click opens the
// tab menu, and when the tabs do not fit an overflow button lists all of them.
//

class CTabWindow : public CWindow
{
public:
    CFilesWindow* FilesWindow;

public:
    CTabWindow(CFilesWindow* filesWindow);
    ~CTabWindow();

    void DestroyWindow();
    int GetNeededHeight();
    void Repaint();

    // The strip's own window class, so tools and scripts that look for the directory line or
    // status line (CWINDOW_CLASSNAME2 children of a panel) do not mistake the strip for one.
    static const wchar_t* WindowClassName();

    // A tab dragged along the strip (reorder); npos when none. DragId names it, as the tabs can
    // change while the mouse is held.
    std::size_t DragIndex;
    std::uint32_t DragId;
    void CancelDrag();
    // Folders dropped on the strip, opened after the drag-and-drop loop returns.
    std::vector<std::wstring> PendingDrops;

protected:
    sally::tabs::StripLayout Layout;
    std::size_t HotIndex;  // the tab under the mouse, npos for none
    BOOL HotClose;         // the mouse is on the hot tab's close cross
    BOOL TrackingLeave;    // TrackMouseEvent(TME_LEAVE) is pending
    DWORD LastEmptyClick;  // time of the last click on empty strip space (double-click detection)
    POINT LastEmptyPoint;
    int WheelDelta;        // wheel movement not yet turned into a tab switch
    POINT DragStart;       // where the drag of DragIndex began
    BOOL Dragging;         // DragIndex moved far enough: the drop gap is shown
    std::size_t DropGap;   // the gap the dragged tab would go to
    IDropTarget* DropTarget; // folders dropped on the strip open in new tabs

    void EndDrag();
    void OpenPendingDrops();

    void ResetPointerState();

    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void Paint(HDC hDC);
    void Measure(HDC hDC, int width);
    RECT CloseRect(std::size_t index);
    void ShowTabMenu(std::size_t index, POINT screen);
    void ShowOverflowMenu();
};

// The Total Commander key profile (Configuration.TabKeysTotalCommander): Ctrl+T, Ctrl+W, Ctrl+Tab
// and Ctrl+Shift+Tab work on the active panel's tabs. Returns TRUE when it handled 'msg'.
BOOL TranslateTabKeyProfile(CMainWindow* mainWindow, const MSG& msg);
