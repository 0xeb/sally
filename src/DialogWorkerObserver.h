// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// CDialogWorkerObserver — routes IWorkerObserver calls to the existing
// progress dialog via SendMessage (WM_USER_DIALOG / WM_USER_SETDIALOG).
// This is the bridge that lets us decouple without changing behavior.
//
// Wide-only marshaling: one message-id space (0-12); every
// payload slot carries UTF-16. The parallel 1xx ANSI+wide id space is gone.
// The handler side (dialogs.cpp WM_USER_DIALOG) narrows display strings for
// dialog ctor args that are still char* until the winlib flip.

#pragma once

#include "common/IWorkerObserver.h"

class CDialogWorkerObserver : public IWorkerObserver
{
    // Non-owning bridge state. The progress dialog and worker-owned flags outlive
    // the observer while a file operation is active.
    HWND m_hProgressDlg;
    HANDLE m_workerNotSuspended;
    BOOL* m_cancelWorker;
    int* m_operationProgress;
    int* m_summaryProgress;

public:
    CDialogWorkerObserver(HWND hDlg, HANDLE workerNotSuspended, BOOL* cancelWorker,
                          int* operationProgress, int* summaryProgress)
        : m_hProgressDlg(hDlg)
        , m_workerNotSuspended(workerNotSuspended)
        , m_cancelWorker(cancelWorker)
        , m_operationProgress(operationProgress)
        , m_summaryProgress(summaryProgress)
    {
    }

    void SetOperationInfo(CProgressData* data) override
    {
        WaitIfSuspended();
        if (!IsCancelled())
            SendMessage(m_hProgressDlg, WM_USER_SETDIALOG, (WPARAM)data, 0);
    }

    void SetProgress(int operationPercent, int summaryPercent) override
    {
        WaitIfSuspended();
        if (!IsCancelled() &&
            (*m_operationProgress != operationPercent || *m_summaryProgress != summaryPercent))
        {
            *m_operationProgress = operationPercent;
            *m_summaryProgress = summaryPercent;
            SendMessage(m_hProgressDlg, WM_USER_SETDIALOG, 0, 0);
        }
    }

    void SetProgressWithoutSuspend(int operationPercent, int summaryPercent) override
    {
        if (!IsCancelled() &&
            (*m_operationProgress != operationPercent || *m_summaryProgress != summaryPercent))
        {
            *m_operationProgress = operationPercent;
            *m_summaryProgress = summaryPercent;
            SendMessage(m_hProgressDlg, WM_USER_SETDIALOG, 0, 0);
        }
    }

    void WaitIfSuspended() override
    {
        WaitForSingleObject(m_workerNotSuspended, INFINITE);
    }

    bool IsCancelled() const override
    {
        return *m_cancelWorker != FALSE;
    }

    void SetError(bool error) override
    {
        *m_cancelWorker = error ? TRUE : FALSE;
    }

    void NotifyDone() override
    {
        SendMessage(m_hProgressDlg, WM_COMMAND, IDOK, 0);
    }

    HWND GetParentWindow() const override
    {
        return m_hProgressDlg;
    }

    int AskFileError(const wchar_t* title, const wchar_t* fileName,
                     const wchar_t* errorText) override
    {
        int ret = IDCANCEL;
        void* data[4];
        data[0] = &ret;
        data[1] = const_cast<wchar_t*>(title);
        data[2] = const_cast<wchar_t*>(fileName);
        data[3] = const_cast<wchar_t*>(errorText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
        return ret;
    }

    int AskFileErrorById(int titleId, const wchar_t* fileName, DWORD win32Error) override
    {
        return AskFileError(LoadStrW(titleId), fileName, GetErrorTextOwned(win32Error).c_str());
    }

    int AskFileErrorByIds(int titleId, const wchar_t* fileName, int errorTextId) override
    {
        return AskFileError(LoadStrW(titleId), fileName, LoadStrW(errorTextId));
    }

    int AskOverwrite(const wchar_t* sourceName, const wchar_t* sourceInfo,
                     const wchar_t* targetName, const wchar_t* targetInfo,
                     bool dirOverwrite = false) override
    {
        int ret = IDCANCEL;
        void* data[6];
        data[0] = &ret;
        data[1] = const_cast<wchar_t*>(sourceName);
        data[2] = const_cast<wchar_t*>(sourceInfo);
        data[3] = const_cast<wchar_t*>(targetName);
        data[4] = const_cast<wchar_t*>(targetInfo);
        data[5] = (void*)(DWORD_PTR)(dirOverwrite ? 1 : 0);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 1, (LPARAM)data);
        return ret;
    }

    int AskHiddenOrSystem(const wchar_t* title, const wchar_t* fileName,
                          const wchar_t* actionText) override
    {
        int ret = IDCANCEL;
        void* data[4];
        data[0] = &ret;
        data[1] = const_cast<wchar_t*>(title);
        data[2] = const_cast<wchar_t*>(fileName);
        data[3] = const_cast<wchar_t*>(actionText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 2, (LPARAM)data);
        return ret;
    }

    int AskHiddenOrSystemById(int titleId, const wchar_t* fileName, int actionId) override
    {
        return AskHiddenOrSystem(LoadStrW(titleId), fileName, LoadStrW(actionId));
    }

    int AskCannotMove(const wchar_t* errorText, const wchar_t* fileName,
                      const wchar_t* destPath, bool isDirectory) override
    {
        // Slot order matches the CCannotMoveDlg ctor (fileName, targetPath,
        // errorText) — see the handler cases 3/4 in dialogs.cpp.
        int ret = IDCANCEL;
        void* data[4];
        data[0] = &ret;
        data[1] = const_cast<wchar_t*>(fileName);
        data[2] = const_cast<wchar_t*>(destPath);
        data[3] = const_cast<wchar_t*>(errorText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, isDirectory ? 4 : 3, (LPARAM)data);
        return ret;
    }

    int AskCannotMoveErr(const wchar_t* sourceName, const wchar_t* targetName,
                         DWORD win32Error, bool isDirectory) override
    {
        return AskCannotMove(GetErrorTextOwned(win32Error).c_str(), sourceName, targetName, isDirectory);
    }

    void NotifyError(const wchar_t* title, const wchar_t* fileName,
                     const wchar_t* errorText) override
    {
        void* data[3];
        data[0] = const_cast<wchar_t*>(title);
        data[1] = const_cast<wchar_t*>(fileName);
        data[2] = const_cast<wchar_t*>(errorText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 5, (LPARAM)data);
    }

    void NotifyErrorById(int titleId, const wchar_t* fileName, int detailId) override
    {
        NotifyError(LoadStrW(titleId), fileName, LoadStrW(detailId));
    }

    int AskADSReadError(const wchar_t* fileName, const wchar_t* adsName) override
    {
        int ret = IDCANCEL;
        void* data[3];
        data[0] = &ret;
        data[1] = const_cast<wchar_t*>(fileName);
        data[2] = const_cast<wchar_t*>(adsName);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 6, (LPARAM)data);
        return ret;
    }

    int AskADSOverwrite(const wchar_t* sourceName, const wchar_t* sourceInfo,
                        const wchar_t* targetName, const wchar_t* targetInfo) override
    {
        int ret = IDCANCEL;
        void* data[5];
        data[0] = &ret;
        data[1] = const_cast<wchar_t*>(sourceName);
        data[2] = const_cast<wchar_t*>(sourceInfo);
        data[3] = const_cast<wchar_t*>(targetName);
        data[4] = const_cast<wchar_t*>(targetInfo);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 7, (LPARAM)data);
        return ret;
    }

    int AskADSOpenError(const wchar_t* fileName, const wchar_t* adsName,
                        const wchar_t* errorText) override
    {
        int ret = IDCANCEL;
        void* data[4];
        data[0] = &ret;
        data[1] = const_cast<wchar_t*>(fileName);
        data[2] = const_cast<wchar_t*>(adsName);
        data[3] = const_cast<wchar_t*>(errorText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 8, (LPARAM)data);
        return ret;
    }

    int AskADSOpenErrorById(int titleId, const wchar_t* fileName, DWORD win32Error) override
    {
        return AskADSOpenError(LoadStrW(titleId), fileName, GetErrorTextOwned(win32Error).c_str());
    }

    int AskSetAttrsError(const wchar_t* fileName, DWORD failedAttrs, DWORD currentAttrs) override
    {
        int ret = IDCANCEL;
        void* data[4];
        data[0] = &ret;
        data[1] = const_cast<wchar_t*>(fileName);
        data[2] = (void*)(DWORD_PTR)failedAttrs;
        data[3] = (void*)(DWORD_PTR)currentAttrs;
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 9, (LPARAM)data);
        return ret;
    }

    int AskCopyPermError(const wchar_t* sourceFile, const wchar_t* targetFile,
                         DWORD win32Error) override
    {
        int ret = IDCANCEL;
        void* data[4];
        data[0] = &ret;
        data[1] = const_cast<wchar_t*>(sourceFile);
        data[2] = const_cast<wchar_t*>(targetFile);
        data[3] = (void*)(DWORD_PTR)win32Error;
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 10, (LPARAM)data);
        return ret;
    }

    int AskCopyDirTimeError(const wchar_t* dirName, DWORD errorCode) override
    {
        int ret = IDCANCEL;
        void* data[3];
        data[0] = &ret;
        data[1] = const_cast<wchar_t*>(dirName);
        data[2] = (void*)(DWORD_PTR)errorCode;
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 11, (LPARAM)data);
        return ret;
    }

    int AskEncryptionLoss(bool isEncrypted, const wchar_t* fileName, bool isDir) override
    {
        int ret = IDCANCEL;
        void* data[4];
        data[0] = &ret;
        data[1] = (void*)(DWORD_PTR)(isEncrypted ? 1 : 0);
        data[2] = const_cast<wchar_t*>(fileName);
        data[3] = (void*)(DWORD_PTR)(isDir ? 1 : 0);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 12, (LPARAM)data);
        return ret;
    }
};
