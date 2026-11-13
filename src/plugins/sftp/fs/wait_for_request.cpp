// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "wait_for_request.h"

#include "../ui/interaction_ui.h"

namespace
{
const DWORD SliceMs = 100;            // how often ESC is polled
const DWORD WaitWindowDelayMs = 1000; // the wait window appears after this
const DWORD CancelGraceMs = 2000;     // how long we wait for the session to acknowledge a cancel

bool Waiting = false; // main thread only

// True when a window of our own process is in front. We use the process, not exactly 'parent': the disabled parent
// may not count as the foreground window, and ESC pressed in front of another program must not cancel.
bool OurProcessIsForeground()
{
    DWORD pid = 0;
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr)
        return false;
    GetWindowThreadProcessId(foreground, &pid);
    return pid == GetCurrentProcessId();
}

// Disables the parent for the whole wait (the user cannot start another command) and restores it, with the focus,
// at the end. Same idea as in the FTP plugin.
class CParentDisabler
{
public:
    explicit CParentDisabler(HWND parent) : parent_(parent), focus_(nullptr), wasEnabled_(true)
    {
        if (parent_ == nullptr)
            return;
        wasEnabled_ = IsWindowEnabled(parent_) != FALSE;
        // remember the focus if it is inside the parent
        HWND focus = GetFocus();
        for (HWND w = focus; w != nullptr; w = GetParent(w))
            if (w == parent_)
            {
                focus_ = focus;
                break;
            }
        EnableWindow(parent_, FALSE);
    }

    // The question dialogs are modal over the parent: they want it enabled to disable it themselves.
    void Suspend()
    {
        if (parent_ != nullptr)
            EnableWindow(parent_, TRUE);
    }
    void Resume()
    {
        if (parent_ != nullptr)
            EnableWindow(parent_, FALSE);
    }

    ~CParentDisabler()
    {
        if (parent_ == nullptr)
            return;
        if (wasEnabled_)
            EnableWindow(parent_, TRUE); // restore the state we found, not "enabled" unconditionally
        if (GetForegroundWindow() == parent_)
        {
            if (parent_ == SalamanderGeneral->GetMainWindowHWND())
                SalamanderGeneral->RestoreFocusInSourcePanel();
            else if (focus_ != nullptr)
                SetFocus(focus_);
        }
    }

private:
    HWND parent_;
    HWND focus_;
    bool wasEnabled_;
};

// The SDK wait window lives in its own thread; destroy it on every exit path.
class CSafeWaitWindow
{
public:
    CSafeWaitWindow(const std::wstring& text, HWND parent)
    {
        const std::wstring caption = LangStr(IDS_PLUGINNAME);
        // The wait window shows itself only while this exact window is the foreground one. Sally often hands us the
        // panel window (a child window, never the foreground window), so use the top-level window above it.
        const HWND foreground = parent != nullptr ? GetAncestor(parent, GA_ROOT) : nullptr;
        SalamanderGeneral->CreateSafeWaitWindow(text.c_str(), caption.c_str(), static_cast<int>(WaitWindowDelayMs),
                                                TRUE, foreground != nullptr ? foreground : parent);
    }
    ~CSafeWaitWindow() { SalamanderGeneral->DestroySafeWaitWindow(); }
    CSafeWaitWindow(const CSafeWaitWindow&) = delete;
    CSafeWaitWindow& operator=(const CSafeWaitWindow&) = delete;

    bool ClosePressed() { return SalamanderGeneral->GetSafeWaitWindowClosePressed() != FALSE; }
    void Show(bool show) { SalamanderGeneral->ShowSafeWaitWindow(show ? TRUE : FALSE); }
};

// Sets the wait cursor and restores the old one.
class CWaitCursor
{
public:
    CWaitCursor() : old_(SetCursor(LoadCursor(nullptr, IDC_WAIT))) {}
    ~CWaitCursor() { SetCursor(old_); }
    void Refresh() { SetCursor(LoadCursor(nullptr, IDC_WAIT)); } // Windows resets the cursor on mouse moves
private:
    HCURSOR old_;
};

// Re-posts WM_QUIT (seen while pumping) when the wait ends, so the application still quits.
struct CQuitState
{
    bool seen = false;
    int exitCode = 0;
    ~CQuitState()
    {
        if (seen)
            PostQuitMessage(exitCode);
    }
};

// Restricted pump (like it is needed here, unlike the FTP plugin's "dispatch everything"): sent messages are processed
// inside PeekMessage, paint messages are dispatched so Sally repaints. Posted, timer and input messages stay in the
// queue: dispatching them could make Sally close or re-enter this FS while it is in the middle of a call.
// Returns true when WM_QUIT was seen (it is re-posted by 'quit' at the end of the wait).
bool PumpMessages(CQuitState& quit)
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE | PM_QS_PAINT | PM_QS_SENDMESSAGE))
    {
        if (msg.message == WM_QUIT)
        {
            quit.seen = true;
            quit.exitCode = static_cast<int>(msg.wParam);
            return true;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return false;
}

// ESC or the Close button pressed? (A pressed ESC is consumed: pending key messages are dropped and we wait for the
// key to be released, so the ESC does not leak into the next thing that polls it.)
// 'escArmed': ESC counts only after it was seen released once (a key held from a dialog that just closed must not
// cancel at once).
bool CancelRequested(CSafeWaitWindow& waitWindow, bool escArmed)
{
    bool pressed = false;
    if (escArmed && (GetAsyncKeyState(VK_ESCAPE) & 0x8001) != 0 && OurProcessIsForeground())
        pressed = true;
    if (waitWindow.ClosePressed())
        pressed = true;
    if (pressed)
    {
        MSG msg; // discard the buffered ESC
        while (PeekMessageW(&msg, nullptr, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
            ;
        SalamanderGeneral->WaitForESCRelease();
    }
    return pressed;
}

// Every wait can be cancelled with ESC: the hint is added to the end of the first line of the text.
std::wstring WithEscHint(const std::wstring& text)
{
    const std::wstring hint = L" " + LangStr(IDS_WAIT_ESC_HINT);
    const size_t lineEnd = text.find(L'\n');
    if (lineEnd == std::wstring::npos)
        return text + hint;
    return text.substr(0, lineEnd) + hint + text.substr(lineEnd);
}

WaitResult WaitImpl(HWND parent, CPanelSession& session, const std::shared_ptr<CPanelRequest>& request,
                    const std::wstring& plainText, const std::function<std::wstring()>& textNow)
{
    const std::wstring text = WithEscHint(plainText);
    CParentDisabler disabler(parent);
    CWaitCursor cursor;
    CSafeWaitWindow waitWindow(text, parent);

    CQuitState quit; // WM_QUIT seen while pumping is re-posted when this function returns
    HANDLE handles[2] = {request->DoneEvent(), session.InteractionEvent()};
    bool cancelled = false;
    bool escArmed = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) == 0;
    DWORD cancelTick = 0;
    std::wstring shownText = text;

    GetAsyncKeyState(VK_ESCAPE); // clear the "pressed since the last call" bit, see the Win32 help
    for (;;)
    {
        const DWORD r = MsgWaitForMultipleObjects(2, handles, FALSE, SliceMs, QS_PAINT | QS_SENDMESSAGE);
        if (r == WAIT_OBJECT_0)
            break; // done
        if (r == WAIT_OBJECT_0 + 1)
        {
            // The session thread asks a question and blocks until it is answered. Hide the wait window, let the
            // dialog be modal over the (re-enabled) parent, then go on waiting.
            waitWindow.Show(false);
            disabler.Suspend();
            HandleInteraction(parent, session); // never throws, always answers
            disabler.Resume();
            waitWindow.Show(true);
            GetAsyncKeyState(VK_ESCAPE); // an ESC typed into the dialog must not cancel the request
            escArmed = false;            // ...and neither must one that is still held
            continue;
        }
        bool quitSeen = false;
        if (r == WAIT_OBJECT_0 + 2)
            quitSeen = PumpMessages(quit);
        else if (r == WAIT_FAILED)
        {
            TRACE_E("WaitForRequest: MsgWaitForMultipleObjects failed.");
            Sleep(SliceMs); // never spin
        }
        cursor.Refresh();
        if (textNow && !cancelled)
        {
            const std::wstring now = WithEscHint(textNow());
            if (now != shownText)
            {
                shownText = now;
                SalamanderGeneral->SetSafeWaitWindowText(shownText.c_str());
            }
        }
        if (!escArmed && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) == 0)
            escArmed = true;

        if (!cancelled)
        {
            if (quitSeen || CancelRequested(waitWindow, escArmed))
            {
                cancelled = true;
                cancelTick = GetTickCount();
                session.Cancel();
            }
        }
        else if (GetTickCount() - cancelTick > CancelGraceMs)
        {
            TRACE_E("WaitForRequest: the session did not acknowledge the cancel within the grace period.");
            return WaitResult::Cancelled; // the request goes on in the background
        }
    }
    return request->Cancelled() ? WaitResult::Cancelled : WaitResult::Done;
}
} // namespace

bool IsWaiting()
{
    return Waiting;
}

WaitResult WaitForRequest(HWND parent, CPanelSession& session, const std::shared_ptr<CPanelRequest>& request,
                          const std::wstring& text, const std::function<std::wstring()>& textNow)
{
    CALL_STACK_MESSAGE1("WaitForRequest()");
    if (request->IsDone())
        return request->Cancelled() ? WaitResult::Cancelled : WaitResult::Done;
    if (Waiting)
    {
        // Do not cancel anything here: the running wait belongs to another request.
        TRACE_E("WaitForRequest: nested wait (a second request started while the first one is waited for).");
        return WaitResult::Cancelled;
    }

    Waiting = true;
    WaitResult result = WaitResult::Cancelled;
    try
    {
        result = WaitImpl(parent, session, request, text, textNow);
    }
    catch (...)
    {
        TRACE_E("WaitForRequest: unexpected exception.");
        session.Cancel();
    }
    Waiting = false;
    return result;
}
