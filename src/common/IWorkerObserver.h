// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// IWorkerObserver — decouples the worker thread from the progress dialog UI.
//
// The worker thread calls these methods instead of SendMessage(hProgressDlg, ...).
// The default implementation (CDialogWorkerObserver) routes to the existing progress
// dialog via WM_USER_DIALOG / WM_USER_SETDIALOG. Future implementations can provide
// headless, mock, or alternative-UI observers.
//
// Wide-only surface: every string crossing this interface is
// UTF-16. The former ANSI virtuals and their silently-discarding W-sibling
// default bodies are gone — each method is a single pure virtual, so the
// compiler enumerates every implementor on any change.
//
// Each Ask* method blocks until the user responds. Return values match the existing
// dialog button IDs (IDRETRY, IDB_SKIP, IDB_SKIPALL, IDCANCEL, IDYES, etc.) so the
// worker logic doesn't change.

#pragma once

#include <windows.h>

struct CProgressData;

// Standard dialog return values (matching resource IDs from worker.cpp)
// IDRETRY, IDYES, IDNO, IDCANCEL are from windows.h
// IDB_SKIP, IDB_SKIPALL, IDB_ALL, IDB_IGNORE come from the app's resource.h

class IWorkerObserver
{
public:
    virtual ~IWorkerObserver() = default;

    // --- Progress updates ---

    // Set the current operation description (source, target, preposition)
    virtual void SetOperationInfo(CProgressData* data) = 0;

    // Update progress bars (0-1000 scale)
    virtual void SetProgress(int operationPercent, int summaryPercent) = 0;

    // Update progress without waiting for suspend (used inside copy loops
    // where the worker must not block mid-transfer)
    virtual void SetProgressWithoutSuspend(int operationPercent, int summaryPercent) = 0;

    // --- Suspend / Cancel ---

    // Block if the UI has suspended the worker (pause button).
    // Returns immediately if not suspended.
    virtual void WaitIfSuspended() = 0;

    // Check if the user has requested cancellation.
    virtual bool IsCancelled() const = 0;

    // Signal that the worker is done (error or success).
    virtual void SetError(bool error) = 0;

    // Signal that the worker has finished — dialog can close.
    virtual void NotifyDone() = 0;

    // Get a parent HWND for shell operations (e.g. SHFileOperation for Recycle Bin).
    // Returns NULL in headless/test mode. The shell API handles NULL gracefully.
    virtual HWND GetParentWindow() const = 0;

    // --- Error dialogs (WM_USER_DIALOG message ID 0) ---
    // Generic file error with retry/skip/cancel options.
    // Returns IDRETRY, IDB_SKIP, IDB_SKIPALL, IDCANCEL, or IDB_IGNORE.
    virtual int AskFileError(const wchar_t* title, const wchar_t* fileName,
                             const wchar_t* errorText) = 0;

    // ID-based variant — worker passes IDS_* constant + Win32 error code,
    // observer handles localization (LoadStrW / GetErrorTextOwned).
    virtual int AskFileErrorById(int titleId, const wchar_t* fileName, DWORD win32Error) = 0;

    // Variant where both title and error text are string resource IDs.
    virtual int AskFileErrorByIds(int titleId, const wchar_t* fileName, int errorTextId) = 0;

    // --- Overwrite confirmation (message ID 1) ---
    // Ask whether to overwrite a file (or, with dirOverwrite, join a directory).
    // Returns IDYES, IDB_ALL (yes to all), IDB_SKIP, IDB_SKIPALL, IDCANCEL.
    virtual int AskOverwrite(const wchar_t* sourceName, const wchar_t* sourceInfo,
                             const wchar_t* targetName, const wchar_t* targetInfo,
                             bool dirOverwrite = false) = 0;

    // --- Hidden/system file confirmation (message ID 2) ---
    // Returns IDYES, IDB_ALL, IDB_SKIP, IDB_SKIPALL, IDCANCEL.
    virtual int AskHiddenOrSystem(const wchar_t* title, const wchar_t* fileName,
                                  const wchar_t* actionText) = 0;

    // ID-based variant — worker passes IDS_* constants, observer handles localization.
    virtual int AskHiddenOrSystemById(int titleId, const wchar_t* fileName, int actionId) = 0;

    // --- Cannot move/rename (message IDs 3, 4) ---
    // Returns IDRETRY, IDB_SKIP, IDB_SKIPALL, IDCANCEL.
    virtual int AskCannotMove(const wchar_t* errorText, const wchar_t* fileName,
                              const wchar_t* destPath, bool isDirectory) = 0;

    // Variant with Win32 error code — observer formats error text.
    virtual int AskCannotMoveErr(const wchar_t* sourceName, const wchar_t* targetName,
                                 DWORD win32Error, bool isDirectory) = 0;

    // --- Simple error notification (message ID 5) ---
    // Informational only — no return value expected.
    virtual void NotifyError(const wchar_t* title, const wchar_t* fileName,
                             const wchar_t* errorText) = 0;

    // ID-based variant — worker passes IDS_* constants, observer handles localization.
    virtual void NotifyErrorById(int titleId, const wchar_t* fileName, int detailId) = 0;

    // --- ADS read error (message ID 6) ---
    // Returns IDB_SKIP, IDB_SKIPALL, IDB_IGNORE, IDB_ALL (ignore all), IDCANCEL.
    virtual int AskADSReadError(const wchar_t* fileName, const wchar_t* adsName) = 0;

    // --- ADS overwrite (message ID 7) ---
    // Same semantics as AskOverwrite but for alternate data streams.
    virtual int AskADSOverwrite(const wchar_t* sourceName, const wchar_t* sourceInfo,
                                const wchar_t* targetName, const wchar_t* targetInfo) = 0;

    // --- Cannot open ADS (message ID 8) ---
    // Returns IDRETRY, IDB_SKIP, IDB_SKIPALL, IDB_IGNORE, IDB_ALL (ignore all), IDCANCEL.
    virtual int AskADSOpenError(const wchar_t* fileName, const wchar_t* adsName,
                                const wchar_t* errorText) = 0;

    // ID-based variant — worker passes IDS_* constant + Win32 error code.
    virtual int AskADSOpenErrorById(int titleId, const wchar_t* fileName, DWORD win32Error) = 0;

    // --- Error setting attributes (message ID 9) ---
    // Returns IDRETRY, IDB_SKIP, IDB_SKIPALL, IDB_IGNORE, IDB_ALL (ignore all), IDCANCEL.
    virtual int AskSetAttrsError(const wchar_t* fileName, DWORD failedAttrs,
                                 DWORD currentAttrs) = 0;

    // --- Error copying permissions (message ID 10) ---
    // Returns IDRETRY, IDB_SKIP, IDB_SKIPALL, IDB_IGNORE, IDB_ALL (ignore all), IDCANCEL.
    // Takes the Win32 error code directly — the legacy path laundered it
    // through a char* "errorText" parameter and truncated it back at the
    // marshaling boundary.
    virtual int AskCopyPermError(const wchar_t* sourceFile, const wchar_t* targetFile,
                                 DWORD win32Error) = 0;

    // --- Error copying directory time (message ID 11) ---
    // Returns IDRETRY, IDB_IGNORE, IDB_ALL (ignore all), IDCANCEL.
    virtual int AskCopyDirTimeError(const wchar_t* dirName, DWORD errorCode) = 0;

    // --- Confirm encryption loss (message ID 12) ---
    // Returns IDYES, IDB_ALL (yes to all), IDB_SKIP, IDB_SKIPALL, IDCANCEL.
    virtual int AskEncryptionLoss(bool isEncrypted, const wchar_t* fileName, bool isDir) = 0;
};
