// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Win32ChangeNotifier — IChangeNotifier over the snooper thread.
//
// The snooper owns the watch handles, the wait loop and the panel message posts;
// this class is the seam in front of it, so callers (and tests) stop reaching
// for free functions with A/W twins. It deliberately holds no state of its own:
// duplicating the snooper's bookkeeping here would be a second source of truth.

#include "precomp.h"

#include "plugins.h" // fileswnd.h needs the plugin encapsulations
#include "fileswnd.h"
#include "mainwnd.h"
#include "snooper.h"

#include "common/IChangeNotifier.h"

namespace
{
class CWin32ChangeNotifier : public IChangeNotifier
{
public:
    ChangeNotifierResult AddWatch(ChangeWatchOwner owner, const wchar_t* path,
                                  bool registerDeviceNotification) override
    {
        if (owner == nullptr || path == nullptr || path[0] == L'\0')
            return ChangeNotifierResult::Error(ERROR_INVALID_PARAMETER);
        AddDirectoryW(static_cast<CFilesWindow*>(owner), path,
                      registerDeviceNotification ? TRUE : FALSE);
        // The snooper reports capacity and open failures through its own trace
        // and simply leaves the panel unwatched (manual refresh); it returns no
        // status, so there is nothing honest to report here beyond acceptance.
        return ChangeNotifierResult::Ok();
    }

    ChangeNotifierResult ChangeWatch(ChangeWatchOwner owner, const wchar_t* path,
                                     bool registerDeviceNotification) override
    {
        if (owner == nullptr || path == nullptr || path[0] == L'\0')
            return ChangeNotifierResult::Error(ERROR_INVALID_PARAMETER);
        ChangeDirectoryW(static_cast<CFilesWindow*>(owner), path,
                         registerDeviceNotification ? TRUE : FALSE);
        return ChangeNotifierResult::Ok();
    }

    ChangeNotifierResult RemoveWatch(ChangeWatchOwner owner) override
    {
        if (owner == nullptr)
            return ChangeNotifierResult::Error(ERROR_INVALID_PARAMETER);
        DetachDirectory(static_cast<CFilesWindow*>(owner));
        return ChangeNotifierResult::Ok();
    }

    void SuspendNotifications() override { BeginSuspendMode(); }
    void ResumeNotifications() override { EndSuspendMode(); }
};

CWin32ChangeNotifier g_win32ChangeNotifier;
} // namespace

IChangeNotifier* GetWin32ChangeNotifier()
{
    return &g_win32ChangeNotifier;
}

IChangeNotifier* gChangeNotifier = &g_win32ChangeNotifier;
