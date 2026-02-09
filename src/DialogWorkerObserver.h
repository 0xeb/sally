// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// CDialogWorkerObserver — routes IWorkerObserver calls to the existing
// progress dialog via SendMessage (WM_USER_DIALOG / WM_USER_SETDIALOG).
// This is the bridge that lets us decouple without changing behavior.

#pragma once

#include "common/IWorkerObserver.h"

class CDialogWorkerObserver : public IWorkerObserver
{
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

    int AskFileError(const char* title, const char* fileName, const char* errorText) override
    {
        int ret = IDCANCEL;
        char* data[4];
        data[0] = (char*)&ret;
        data[1] = const_cast<char*>(title);
        data[2] = const_cast<char*>(fileName);
        data[3] = const_cast<char*>(errorText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
        return ret;
    }

    int AskOverwrite(const char* sourceName, const char* sourceInfo,
                     const char* targetName, const char* targetInfo) override
    {
        int ret = IDCANCEL;
        char* data[5];
        data[0] = (char*)&ret;
        data[1] = const_cast<char*>(sourceName);
        data[2] = const_cast<char*>(sourceInfo);
        data[3] = const_cast<char*>(targetName);
        data[4] = const_cast<char*>(targetInfo);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 1, (LPARAM)data);
        return ret;
    }

    int AskHiddenOrSystem(const char* title, const char* fileName,
                          const char* actionText) override
    {
        int ret = IDCANCEL;
        char* data[4];
        data[0] = (char*)&ret;
        data[1] = const_cast<char*>(title);
        data[2] = const_cast<char*>(fileName);
        data[3] = const_cast<char*>(actionText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 2, (LPARAM)data);
        return ret;
    }

    int AskCannotMove(const char* errorText, const char* fileName,
                      const char* destPath, bool isDirectory) override
    {
        int ret = IDCANCEL;
        char* data[4];
        data[0] = (char*)&ret;
        data[1] = const_cast<char*>(errorText);
        data[2] = const_cast<char*>(fileName);
        data[3] = const_cast<char*>(destPath);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, isDirectory ? 4 : 3, (LPARAM)data);
        return ret;
    }

    void NotifyError(const char* title, const char* fileName, const char* errorText) override
    {
        char* data[3];
        data[0] = const_cast<char*>(title);
        data[1] = const_cast<char*>(fileName);
        data[2] = const_cast<char*>(errorText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 5, (LPARAM)data);
    }

    int AskADSReadError(const char* fileName, const char* adsName) override
    {
        int ret = IDCANCEL;
        char* data[3];
        data[0] = (char*)&ret;
        data[1] = const_cast<char*>(fileName);
        data[2] = const_cast<char*>(adsName);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 6, (LPARAM)data);
        return ret;
    }

    int AskADSOverwrite(const char* sourceName, const char* sourceInfo,
                        const char* targetName, const char* targetInfo) override
    {
        int ret = IDCANCEL;
        char* data[5];
        data[0] = (char*)&ret;
        data[1] = const_cast<char*>(sourceName);
        data[2] = const_cast<char*>(sourceInfo);
        data[3] = const_cast<char*>(targetName);
        data[4] = const_cast<char*>(targetInfo);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 7, (LPARAM)data);
        return ret;
    }

    int AskADSOpenError(const char* fileName, const char* adsName,
                        const char* errorText) override
    {
        int ret = IDCANCEL;
        char* data[4];
        data[0] = (char*)&ret;
        data[1] = const_cast<char*>(fileName);
        data[2] = const_cast<char*>(adsName);
        data[3] = const_cast<char*>(errorText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 8, (LPARAM)data);
        return ret;
    }

    int AskSetAttrsError(const char* fileName, DWORD failedAttrs, DWORD currentAttrs) override
    {
        int ret = IDCANCEL;
        char* data[4];
        data[0] = (char*)&ret;
        data[1] = const_cast<char*>(fileName);
        data[2] = (char*)(DWORD_PTR)failedAttrs;
        data[3] = (char*)(DWORD_PTR)currentAttrs;
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 9, (LPARAM)data);
        return ret;
    }

    int AskCopyPermError(const char* sourceFile, const char* targetFile,
                         const char* errorText) override
    {
        int ret = IDCANCEL;
        char* data[4];
        data[0] = (char*)&ret;
        data[1] = const_cast<char*>(sourceFile);
        data[2] = const_cast<char*>(targetFile);
        data[3] = const_cast<char*>(errorText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 10, (LPARAM)data);
        return ret;
    }

    int AskCopyDirTimeError(const char* dirName, const char* errorText) override
    {
        int ret = IDCANCEL;
        char* data[3];
        data[0] = (char*)&ret;
        data[1] = const_cast<char*>(dirName);
        data[2] = const_cast<char*>(errorText);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 11, (LPARAM)data);
        return ret;
    }

    int AskEncryptionLoss(bool isEncrypted, const char* fileName, bool isDir) override
    {
        int ret = IDCANCEL;
        char* data[4];
        data[0] = (char*)&ret;
        data[1] = (char*)(DWORD_PTR)(isEncrypted ? 1 : 0);
        data[2] = const_cast<char*>(fileName);
        data[3] = (char*)(DWORD_PTR)(isDir ? 1 : 0);
        SendMessage(m_hProgressDlg, WM_USER_DIALOG, 12, (LPARAM)data);
        return ret;
    }
};
