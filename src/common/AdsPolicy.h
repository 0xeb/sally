// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// ADS prompt/probe policy — pure decision functions shared by production
// (files_window_copy_move.cpp) and the private tests. Promoted from the
// former SALLY_PRIVATE_TESTS *ForTest forwarders.

#pragma once

#include <windows.h>
#include <string.h>

#include "lang/lang.rh" // IDB_IGNORE / IDB_IGNOREALL button ids

// Whether an ADS probe error is worth reporting to the user. Preserves the
// legacy exceptions: \\tsclient\* returns ERROR_INVALID_FUNCTION (no ADS on
// RDP redirects), and network paths yield false positives with
// ERROR_INVALID_PARAMETER / ERROR_NO_MORE_ITEMS.
inline BOOL ShouldReportADSProbeError(const wchar_t* sourcePath, DWORD adsWinError, BOOL sourcePathIsNet)
{
    if (adsWinError == NO_ERROR)
        return FALSE;

    if (adsWinError == ERROR_INVALID_FUNCTION &&
        sourcePath != NULL && _wcsnicmp(sourcePath, L"\\\\tsclient\\", 11) == 0)
    {
        return FALSE;
    }

    if ((adsWinError == ERROR_INVALID_PARAMETER || adsWinError == ERROR_NO_MORE_ITEMS) &&
        sourcePathIsNet)
    {
        return FALSE;
    }

    return TRUE;
}

// Normalizes a modal ADS read-error dialog response to the worker contract:
// retry/cancel/ignore pass through; ignore-all sets the flag and degrades to
// ignore; anything unknown degrades to ignore.
inline int NormalizeADSReadErrorResponse(int res, BOOL* ignoreAll)
{
    switch (res)
    {
    case IDRETRY:
    case IDCANCEL:
    case IDB_IGNORE:
        return res;

    case IDB_IGNOREALL:
        if (ignoreAll != NULL)
            *ignoreAll = TRUE;
        return IDB_IGNORE;

    default:
        return IDB_IGNORE;
    }
}
