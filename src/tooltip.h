// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

//*****************************************************************************
//
// CToolTip
//
// This tooltip removes the main drawback of the original tooltip concept.
// Each window had its own tooltip object. Another drawback was
// that the object had to be given a list of regions over which
// tooltips should pop up.
//
// New concept: CMainWindow owns only one tooltip (object instance).
// The tooltip window is created only when needed, in the thread that
// requested display. Reason: we need the tooltip window to run in that thread;
// up to 2.6b6 inclusive the tooltip window ran in Salamander's main thread, and if
// it was blocked, tooltips were not shown.
// When moving the mouse over a control that uses this tooltip, the control
// calls SetCurrentID when entering a new area.
//
// The interface for working with the tooltip will be in const.h so it is available
// to all controls without needing to include mainwnd.h and tooltip.h.
//

// Used messages:
// WM_USER_TTGETTEXT - used to request text for a specific ID
//   wParam = ID passed to SetCurrentToolTip
//   lParam = buffer (points to tooltip buffer) maximum character count is TOOLTIP_TEXT_MAX
//            before calling this message, a terminator is placed at index zero
//            text may contain \n for a new line and \t for a tab
// if the window writes a null-terminated string into the buffer, it will be shown in the tooltip
// otherwise the tooltip will not be shown
//

class CToolTip : public CWindow
{
    enum TipTimerModeEnum
    {
        ttmNone,         // no timer running
        ttmWaitingOpen,  // waiting to open tooltip
        ttmWaitingClose, // waiting to close tooltip
        ttmWaitingKill,  // waiting to exit display mode
    };

protected:
    char Text[TOOLTIP_TEXT_MAX];
    int TextLen;
    HWND HNotifyWindow;
    DWORD LastID;
    TipTimerModeEnum WaitingMode;
    DWORD HideCounter;
    DWORD HideCounterMax;
    POINT LastCursorPos;
    BOOL IsModal;     // is our message loop currently running?
    BOOL ExitASAP;    // close as soon as possible and stop being modal
    UINT_PTR TimerID; // returned from SetTimer, needed for KillTimer

public:
    CToolTip(CObjectOrigin origin = ooStatic);
    ~CToolTip();

    BOOL RegisterClass();

    // hParent is required so that when it closes the tooltip closes too
    // without it we saw the parent thread end while the tooltip window stayed
    // open, but could not be closed (its thread no longer existed) -> crashes on
    // Salamander shutdown (fortunately before release 2.5b7)
    BOOL Create(HWND hParent);

    // This method starts a timer and if it is not called again before it expires
    // it asks window 'hNotifyWindow' for text via WM_USER_TTGETTEXT,
    // which it then shows under the cursor at its current coordinates.
    // Variable 'id' distinguishes areas when communicating with 'hNotifyWindow'.
    // If this method is called multiple times with the same 'id' parameter, the
    // subsequent calls are ignored.
    // Value 0 for parameter 'hNotifyWindow' is reserved for hiding the window and
    // interrupting a running timer.
    // parameter 'showDelay' has meaning if 'hNotifyWindow' != NULL
    // if it is >= 1, it specifies how long before tooltip is shown in [ms]
    // if it is 0, the default delay is used
    // if it is -1, the timer is not started at all
    void SetCurrentToolTip(HWND hNotifyWindow, DWORD id, int showDelay);

    // suppresses tooltip display at current mouse coordinates
    // useful to call when activating a window that uses tooltips
    // prevents unwanted tooltip display
    void SuppressToolTipOnCurrentMousePos();

    // returns TRUE if text is displayed; if no new text is provided, returns FALSE
    // if considerCursor==TRUE, measures cursor and moves tooltip below it
    // if modal==TRUE, starts a message loop that watches for tooltip close messages and returns after it is hidden
    BOOL Show(int x, int y, BOOL considerCursor, BOOL modal, HWND hParent);

    // hides tooltip
    void Hide();

    void OnTimer();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    BOOL GetText();
    void GetNeededWindowSize(SIZE* sz);

    void MessageLoop(); // for modal tooltip variant

    void MySetTimer(DWORD elapse);
    void MyKillTimer();

    DWORD GetTime(BOOL init);
};
