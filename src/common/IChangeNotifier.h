// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// IChangeNotifier — directory change notification, decoupled and wide-only.
//
// The snooper was the last raw-Win32 subsystem with no interface in front of it.
// This is that interface: the panel asks for a watch on a UTF-16
// path and never learns whether a thread, FindFirstChangeNotificationW, or a
// test double is doing the work.
//
// WIDE-ONLY BY CONTRACT. The previous entry points came in A/W pairs and the
// panel called the ANSI one with its lossy mirror, so a directory whose name the
// active code page cannot spell was watched at the WRONG path — or not at all —
// and the panel silently stopped auto-refreshing. There is deliberately no
// narrow overload here for a caller to reach for by accident.
//
// THREADING: implementations may service watches on their own thread. Callers
// must treat every method as "ask now, effect shortly after"; the panel's
// existing message protocol (WM_USER_* refresh posts) remains the delivery path.
//
// CAPACITY: the Win32 implementation waits on the watch handles with
// WaitForMultipleObjects, so the number of simultaneous watches is bounded by
// MAXIMUM_WAIT_OBJECTS minus the implementation's own control handles. Callers
// must tolerate AddWatch failing for capacity reasons — the panel degrades to
// manual refresh, it does not error out.

#pragma once

#include <windows.h>

struct ChangeNotifierResult
{
    bool success;
    DWORD errorCode;

    static ChangeNotifierResult Ok() { return {true, ERROR_SUCCESS}; }
    static ChangeNotifierResult Error(DWORD err) { return {false, err}; }
    // TRUE when the request failed because no watch slot was available.
    bool atCapacity() const { return !success && errorCode == ERROR_NO_MORE_ITEMS; }
};

// The object a watch belongs to. Opaque here on purpose: the notifier only needs
// identity for ChangeWatch/RemoveWatch, not the panel's structure.
using ChangeWatchOwner = void*;

class IChangeNotifier
{
public:
    virtual ~IChangeNotifier() = default;

    // Start watching 'path' (a directory) on behalf of 'owner'.
    // registerDeviceNotification asks for media-arrival/removal notices too,
    // which only makes sense for removable and fixed volumes.
    virtual ChangeNotifierResult AddWatch(ChangeWatchOwner owner,
                                          const wchar_t* path,
                                          bool registerDeviceNotification) = 0;

    // Repoint an existing watch. Equivalent to RemoveWatch + AddWatch but
    // implementations may keep the slot, which matters at capacity.
    virtual ChangeNotifierResult ChangeWatch(ChangeWatchOwner owner,
                                            const wchar_t* path,
                                            bool registerDeviceNotification) = 0;

    // Stop watching for 'owner'. Idempotent: removing an unknown owner succeeds.
    virtual ChangeNotifierResult RemoveWatch(ChangeWatchOwner owner) = 0;

    // Pause/resume delivery without giving up the watch slots. Used around
    // operations that would otherwise produce a notification storm.
    virtual void SuspendNotifications() = 0;
    virtual void ResumeNotifications() = 0;
};

// Global notifier - default is the Win32 snooper-backed implementation.
extern IChangeNotifier* gChangeNotifier;

// Returns the default Win32 implementation.
IChangeNotifier* GetWin32ChangeNotifier();
