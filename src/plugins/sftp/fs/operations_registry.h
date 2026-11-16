// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../ui/operation_window.h"
#include "operations_list.h"

// What the owner of a new transfer tells the registry besides the operation itself.
struct COperationLaunch
{
    std::wstring source;   // shown after "From:" in the window and in the operations list
    std::wstring target;   // shown after "To:"
    std::wstring hostName; // "host:port", named in the questions the workers ask
    // Full paths (Windows or "sftp:...") whose listings changed. When a run of the operation ends, Sally is told with
    // PostChangeOnPathNotification so every panel showing one of them refreshes.
    std::vector<std::wstring> changedPaths;
};

// All transfers of the plugin: the operations list (which owns the operations) plus, for each operation, its window
// and its description. Everything except the operation callbacks runs on Sally's main thread.
// (C# analogy: a static "TransferManager" with a list of jobs and one progress window per job.)
class COperationsRegistry
{
public:
    // Creates the operation and its window, starts the workers and shows the window. False (an error was shown) when
    // the workers could not be started. Throws std::bad_alloc only.
    bool Launch(COperationSettings settings, CPanelConnectParams params, std::vector<CQueueItem> roots,
                const COperationLaunch& launch);

    // Menu "Operations...": the modal list of operations (Show, Cancel).
    void ShowDialog(HWND parent);

    // Plugin Release, first step: when transfers are running, asks "Cancel them and unload?" (not with 'force' or at a
    // critical shutdown). False: the user said No, nothing was changed.
    bool ConfirmRelease(HWND parent, BOOL force);
    // Plugin Release, second step: cancels everything, closes the windows, joins all threads, empties the list.
    void Shutdown();

private:
    struct CEntry
    {
        std::shared_ptr<COperationWindowHost> window;
        std::wstring description; // "source -> target"
    };

    // Drops the operations that are over (nothing left for the user) and whose window is closed. Running, paused and
    // parked operations stay, and so does one whose window is only hidden. Called when a new operation starts and when the
    // operations list opens, so a window that was closed since then is gone from the list.
    void DropClosedFinished();

    std::wstring DescriptionOf(uint64_t uid) const;
    bool ShowWindowOf(uint64_t uid);

    COperationsList list_;
    std::map<uint64_t, CEntry> entries_; // by operation uid; main thread only
};

// The one instance of the plugin.
extern COperationsRegistry OperationsRegistry;
