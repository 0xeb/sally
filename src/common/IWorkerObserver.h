// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// IWorkerObserver — decouples the worker thread from the progress dialog UI.
//
// The worker thread calls these methods instead of SendMessage(hProgressDlg, ...).
// The default implementation (CDialogWorkerObserver) routes to the existing progress
// dialog via WM_USER_DIALOG / WM_USER_SETDIALOG. Future implementations can provide
// headless, mock, or alternative-UI observers.
//
// Each Ask* method blocks until the user responds. Return values match the existing
// dialog button IDs (IDRETRY, IDB_SKIP, IDB_SKIPALL, IDCANCEL, IDYES, etc.) so the
// worker logic doesn't change.

#pragma once

#include <windows.h>

// Forward declarations for data types used by overwrite dialogs
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
    virtual int AskFileError(const char* title, const char* fileName, const char* errorText) = 0;

    // ID-based variant — worker passes IDS_* constant + Win32 error code,
    // observer handles localization (LoadStr / GetErrorText).
    virtual int AskFileErrorById(int titleId, const char* fileName, DWORD win32Error) = 0;

    // Variant where both title and error text are string resource IDs.
    virtual int AskFileErrorByIds(int titleId, const char* fileName, int errorTextId) = 0;

    // --- Overwrite confirmation (message ID 1) ---
    // Ask whether to overwrite a file. Shows source and target info.
    // Returns IDYES, IDB_ALL (yes to all), IDB_SKIP, IDB_SKIPALL, IDCANCEL.
    virtual int AskOverwrite(const char* sourceName, const char* sourceInfo,
                             const char* targetName, const char* targetInfo) = 0;

    // --- Hidden/system file confirmation (message ID 2) ---
    // Returns IDYES, IDB_ALL, IDB_SKIP, IDB_SKIPALL, IDCANCEL.
    virtual int AskHiddenOrSystem(const char* title, const char* fileName,
                                  const char* actionText) = 0;

    // ID-based variant — worker passes IDS_* constants, observer handles localization.
    virtual int AskHiddenOrSystemById(int titleId, const char* fileName, int actionId) = 0;

    // --- Cannot move/rename (message IDs 3, 4) ---
    // Returns IDRETRY, IDB_SKIP, IDB_SKIPALL, IDCANCEL.
    virtual int AskCannotMove(const char* errorText, const char* fileName,
                              const char* destPath, bool isDirectory) = 0;

    // Variant with Win32 error code — observer formats error text.
    virtual int AskCannotMoveErr(const char* sourceName, const char* targetName,
                                 DWORD win32Error, bool isDirectory) = 0;

    // --- Simple error notification (message ID 5) ---
    // Informational only — no return value expected.
    virtual void NotifyError(const char* title, const char* fileName,
                             const char* errorText) = 0;

    // ID-based variant — worker passes IDS_* constants, observer handles localization.
    virtual void NotifyErrorById(int titleId, const char* fileName, int detailId) = 0;

    // --- ADS read error (message ID 6) ---
    // Returns IDB_SKIP, IDB_SKIPALL, IDB_IGNORE, IDB_ALL (ignore all), IDCANCEL.
    virtual int AskADSReadError(const char* fileName, const char* adsName) = 0;

    // --- ADS overwrite (message ID 7) ---
    // Same semantics as AskOverwrite but for alternate data streams.
    virtual int AskADSOverwrite(const char* sourceName, const char* sourceInfo,
                                const char* targetName, const char* targetInfo) = 0;

    // --- Cannot open ADS (message ID 8) ---
    // Returns IDRETRY, IDB_SKIP, IDB_SKIPALL, IDB_IGNORE, IDB_ALL (ignore all), IDCANCEL.
    virtual int AskADSOpenError(const char* fileName, const char* adsName,
                                const char* errorText) = 0;

    // ID-based variant — worker passes IDS_* constant + Win32 error code.
    virtual int AskADSOpenErrorById(int titleId, const char* fileName, DWORD win32Error) = 0;

    // --- Error setting attributes (message ID 9) ---
    // Returns IDRETRY, IDB_SKIP, IDB_SKIPALL, IDB_IGNORE, IDB_ALL (ignore all), IDCANCEL.
    virtual int AskSetAttrsError(const char* fileName, DWORD failedAttrs,
                                 DWORD currentAttrs) = 0;

    // --- Error copying permissions (message ID 10) ---
    // Returns IDRETRY, IDB_SKIP, IDB_SKIPALL, IDB_IGNORE, IDB_ALL (ignore all), IDCANCEL.
    virtual int AskCopyPermError(const char* sourceFile, const char* targetFile,
                                 const char* errorText) = 0;

    // --- Error copying directory time (message ID 11) ---
    // Returns IDRETRY, IDB_IGNORE, IDB_ALL (ignore all), IDCANCEL.
    virtual int AskCopyDirTimeError(const char* dirName, DWORD errorCode) = 0;

    // --- Confirm encryption loss (message ID 12) ---
    // Returns IDYES, IDB_ALL (yes to all), IDB_SKIP, IDB_SKIPALL, IDCANCEL.
    virtual int AskEncryptionLoss(bool isEncrypted, const char* fileName, bool isDir) = 0;
};
