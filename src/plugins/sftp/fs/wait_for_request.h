// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "panel_session.h"

enum class WaitResult
{
    Done,     // the request finished (successfully or not: look at the request)
    Cancelled // the user cancelled (ESC or the wait window's Close button); the request is Cancelled or, when the
              // session did not react within a short grace period, still running in the background
};

// Waits on the main thread for a request of the panel session while keeping Sally painted:
//  - the parent window is disabled (so the user cannot start another command) and shows the wait cursor,
//  - messages are pumped like the FTP plugin does,
//  - after about one second the Sally "safe wait window" appears (with a Close button),
//  - ESC (when one of our windows is in front) or the Close button calls session.Cancel() and the function waits
//    up to two more seconds for the session to acknowledge,
//  - a host key / password question from the session thread is shown through HandleInteraction (the wait window is
//    hidden meanwhile).
// 'text' is shown in the wait window. Everything is restored on every exit path. Never throws.
// 'textNow' (optional) is called about ten times a second on the main thread and returns the current text of the wait
// window (e.g. with a percentage); the window is changed only when the text differs from the last one.
// Not reentrant: IsWaiting() tells whether a wait is already running (a second one must not be started).
WaitResult WaitForRequest(HWND parent, CPanelSession& session, const std::shared_ptr<CPanelRequest>& request,
                          const std::wstring& text, const std::function<std::wstring()>& textNow = nullptr);

bool IsWaiting();
