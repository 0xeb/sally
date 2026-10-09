// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Installing a newer Sally release from the Check Version window: download the package and the
// release's checksums, verify, unpack with utils\salupdate.exe, then close Sally and let the
// updater replace the files and start Sally again. The next Sally reports how it went.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "release_check.h"

// The install worker posts these to the main dialog.
#define WM_USER_INSTALL_LOG WM_APP + 668   // lParam: std::wstring* line (the dialog deletes it)
#define WM_USER_INSTALL_READY WM_APP + 669 // wParam: TRUE when the release is unpacked and ready

// Starts downloading and unpacking a release; FALSE when it cannot start.
BOOL StartReleaseInstall(HWND dialog, const checkver::ReleaseCheckResult& release);
BOOL IsReleaseInstallRunning();
// Stops waiting for a running install (closing the window); the worker ends on its own.
void AbandonReleaseInstall();
// After WM_USER_INSTALL_READY: confirms with the user, closes Sally and starts the updater.
void FinishReleaseInstall(HWND dialog, BOOL ready);

// The result an earlier update left for this Sally folder; consumed once.
BOOL TakeUpdateResult(std::vector<std::wstring>& reportLines, BOOL& failed);
// Whether an earlier update left a result for this Sally folder.
BOOL HasPendingUpdateResult();

// The release-feed address (the GitHub "latest release" API unless configured otherwise).
const wchar_t* GetReleaseFeedUrl();
void SetReleaseFeedUrl(const std::wstring& url);
