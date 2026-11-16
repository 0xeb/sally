// The session log window: a modeless, resizable dialog on its OWN thread that shows the logs of CSessionLogStore (the
// panel connection and the workers). Workers never touch the window: the window thread polls the store's change counter
// with a timer and reloads the text when it moved.
//
// Threads: Show/Close/IsOpen are for the main thread (any thread except the window thread). The window thread is the only
// one that touches the window. mutex_ guards the window handle and the flags (a leaf lock: taken for a PostMessage, never
// held while calling the SDK or a dialog); lifecycleMutex_ serializes Show/Close and is held while joining the thread.
#pragma once

#include <mutex>
#include <string>
#include <thread>

#include "session_log.h"

class CSessionLogWindowHost
{
public:
    CSessionLogWindowHost() = default;
    ~CSessionLogWindowHost();
    CSessionLogWindowHost(const CSessionLogWindowHost&) = delete;
    CSessionLogWindowHost& operator=(const CSessionLogWindowHost&) = delete;

    // Opens the window (creating its thread when needed), brings it to the front and selects the log with this id (0: keep
    // the selection, or the newest log for a new window). False when the thread could not be started.
    bool Show(unsigned logId);
    // Destroys the window and joins its thread. Safe without a window.
    void Close();
    bool IsOpen() const;

private:
    friend class CSessionLogWindow;
    void WindowThreadMain();
    // window thread -> host
    bool OnWindowCreated(HWND window, unsigned& startLog); // false: Close was requested meanwhile
    void OnWindowDestroyed();

    std::mutex lifecycleMutex_; // Show/Close; held while joining the window thread
    std::thread thread_;        // guarded by lifecycleMutex_

    mutable std::mutex mutex_; // the leaf lock: guards the members below
    HWND hwnd_ = nullptr;      // valid only while set; the window thread clears it before the window is gone
    bool closing_ = false;
    bool running_ = false;     // the window thread has started and not ended
    unsigned startLog_ = 0;    // the log the window selects when it appears
};

// The plugin's window (one for all connections).
CSessionLogWindowHost& SessionLogWindow();

// "user@host:port - panel" and friends, in the user's language (string table).
std::wstring FormatSessionLogTitle(const CSessionLogInfo& info);
