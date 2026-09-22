// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// CHeadlessWorkerObserver — production headless IWorkerObserver implementation.
//
// Auto-answers all Ask* dialogs with configurable response codes.
// Thread-safe: cancel/error/progress use atomics, event log uses a mutex.
// No HWND, no SendMessage, no message pump, no UI dependencies.
//
// Usage:
//   CHeadlessWorkerObserver obs;
//   obs.OverwriteResponse = IDYES;        // overwrite all
//   obs.FileErrorResponse = IDB_SKIPALL;  // skip errors
//   RunWorkerDirect(script, obs, NULL, NULL, /*headless=*/true);
//   obs.WaitForCompletion(30000);

#pragma once

#include "IWorkerObserver.h"
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

// Dialog response constants — use the real resource IDs from lang.rh
#include "lang/lang.rh"

class CHeadlessWorkerObserver : public IWorkerObserver
{
public:
    // --- Configuration (set before starting the worker) ---

    int FileErrorResponse = IDB_SKIP;      // AskFileError* default (skip/skipall/retry/cancel)
    int IgnoreErrorResponse = IDB_IGNORE;  // AskSetAttrsError, AskCopyPermError, AskCopyDirTimeError,
                                           // AskADSReadError, AskADSOpenError* (ignore/ignoreall/retry/cancel)
    int OverwriteResponse = IDB_SKIP;      // AskOverwrite / AskADSOverwrite default
    int HiddenSystemResponse = IDYES;      // AskHiddenOrSystem* default
    int CannotMoveResponse = IDB_SKIP;     // AskCannotMove* default
    int EncryptionLossResponse = IDYES;    // AskEncryptionLoss default

    CHeadlessWorkerObserver()
        : m_completionEvent(CreateEvent(NULL, TRUE, FALSE, NULL))
    {
    }

    ~CHeadlessWorkerObserver()
    {
        if (m_completionEvent)
            CloseHandle(m_completionEvent);
    }

    // Non-copyable
    CHeadlessWorkerObserver(const CHeadlessWorkerObserver&) = delete;
    CHeadlessWorkerObserver& operator=(const CHeadlessWorkerObserver&) = delete;

    // --- Control ---

    void Cancel() { m_cancelled.store(true, std::memory_order_release); }

    // --- Results ---

    bool WaitForCompletion(DWORD timeoutMs = 30000)
    {
        return WaitForSingleObject(m_completionEvent, timeoutMs) == WAIT_OBJECT_0;
    }

    HANDLE GetCompletionEvent() const { return m_completionEvent; }
    bool IsDone() const { return m_done.load(std::memory_order_acquire); }
    bool HasError() const { return m_error.load(std::memory_order_acquire); }
    int GetLastOperationPercent() const { return m_lastOpPercent.load(std::memory_order_relaxed); }
    int GetLastSummaryPercent() const { return m_lastSumPercent.load(std::memory_order_relaxed); }

    // Event log for diagnostics/assertions
    struct LogEntry
    {
        std::string method;      // stable ASCII method name (counting API)
        std::wstring detail;     // wide primary argument
    };

    std::vector<LogEntry> GetLog() const
    {
        std::lock_guard<std::mutex> lk(m_logMutex);
        return m_log;
    }

    int CountLogEntriesOfType(const char* method) const
    {
        std::lock_guard<std::mutex> lk(m_logMutex);
        int count = 0;
        for (const auto& e : m_log)
            if (e.method == method)
                count++;
        return count;
    }

    // --- IWorkerObserver implementation ---

    void SetOperationInfo(CProgressData* /*data*/) override
    {
        Log("SetOperationInfo", L"");
    }

    void SetProgress(int operationPercent, int summaryPercent) override
    {
        m_lastOpPercent.store(operationPercent, std::memory_order_relaxed);
        m_lastSumPercent.store(summaryPercent, std::memory_order_relaxed);
    }

    void SetProgressWithoutSuspend(int operationPercent, int summaryPercent) override
    {
        m_lastOpPercent.store(operationPercent, std::memory_order_relaxed);
        m_lastSumPercent.store(summaryPercent, std::memory_order_relaxed);
    }

    void WaitIfSuspended() override { /* no-op in headless mode */ }

    bool IsCancelled() const override
    {
        return m_cancelled.load(std::memory_order_acquire);
    }

    void SetError(bool error) override
    {
        m_error.store(error, std::memory_order_release);
    }

    void NotifyDone() override
    {
        m_done.store(true, std::memory_order_release);
        SetEvent(m_completionEvent);
    }

    HWND GetParentWindow() const override { return NULL; }

    // --- Error dialogs ---

    int AskFileError(const wchar_t* /*title*/, const wchar_t* fileName, const wchar_t* /*errorText*/) override
    {
        Log("AskFileError", fileName ? fileName : L"");
        return FileErrorResponse;
    }

    int AskFileErrorById(int /*titleId*/, const wchar_t* fileName, DWORD /*win32Error*/) override
    {
        Log("AskFileErrorById", fileName ? fileName : L"");
        return FileErrorResponse;
    }

    int AskFileErrorByIds(int /*titleId*/, const wchar_t* fileName, int /*errorTextId*/) override
    {
        Log("AskFileErrorByIds", fileName ? fileName : L"");
        return FileErrorResponse;
    }

    // --- Overwrite ---

    int AskOverwrite(const wchar_t* sourceName, const wchar_t* /*sourceInfo*/,
                     const wchar_t* /*targetName*/, const wchar_t* /*targetInfo*/,
                     bool /*dirOverwrite*/ = false) override
    {
        Log("AskOverwrite", sourceName ? sourceName : L"");
        return OverwriteResponse;
    }

    // --- Hidden/system ---

    int AskHiddenOrSystem(const wchar_t* /*title*/, const wchar_t* fileName,
                          const wchar_t* /*actionText*/) override
    {
        Log("AskHiddenOrSystem", fileName ? fileName : L"");
        return HiddenSystemResponse;
    }

    int AskHiddenOrSystemById(int /*titleId*/, const wchar_t* fileName, int /*actionId*/) override
    {
        Log("AskHiddenOrSystemById", fileName ? fileName : L"");
        return HiddenSystemResponse;
    }

    // --- Cannot move ---

    int AskCannotMove(const wchar_t* /*errorText*/, const wchar_t* fileName,
                      const wchar_t* /*destPath*/, bool /*isDirectory*/) override
    {
        Log("AskCannotMove", fileName ? fileName : L"");
        return CannotMoveResponse;
    }

    int AskCannotMoveErr(const wchar_t* sourceName, const wchar_t* /*targetName*/,
                         DWORD /*win32Error*/, bool /*isDirectory*/) override
    {
        Log("AskCannotMoveErr", sourceName ? sourceName : L"");
        return CannotMoveResponse;
    }

    // --- Notifications ---

    void NotifyError(const wchar_t* /*title*/, const wchar_t* fileName,
                     const wchar_t* /*errorText*/) override
    {
        Log("NotifyError", fileName ? fileName : L"");
    }

    void NotifyErrorById(int /*titleId*/, const wchar_t* fileName, int /*detailId*/) override
    {
        Log("NotifyErrorById", fileName ? fileName : L"");
    }

    // --- ADS ---

    int AskADSReadError(const wchar_t* fileName, const wchar_t* /*adsName*/) override
    {
        Log("AskADSReadError", fileName ? fileName : L"");
        return IgnoreErrorResponse;
    }

    int AskADSOverwrite(const wchar_t* sourceName, const wchar_t* /*sourceInfo*/,
                        const wchar_t* /*targetName*/, const wchar_t* /*targetInfo*/) override
    {
        Log("AskADSOverwrite", sourceName ? sourceName : L"");
        return OverwriteResponse;
    }

    int AskADSOpenError(const wchar_t* fileName, const wchar_t* /*adsName*/,
                        const wchar_t* /*errorText*/) override
    {
        Log("AskADSOpenError", fileName ? fileName : L"");
        return IgnoreErrorResponse;
    }

    int AskADSOpenErrorById(int /*titleId*/, const wchar_t* fileName, DWORD /*win32Error*/) override
    {
        Log("AskADSOpenErrorById", fileName ? fileName : L"");
        return IgnoreErrorResponse;
    }

    // --- Attributes / permissions / time ---

    int AskSetAttrsError(const wchar_t* fileName, DWORD /*failedAttrs*/,
                         DWORD /*currentAttrs*/) override
    {
        Log("AskSetAttrsError", fileName ? fileName : L"");
        return IgnoreErrorResponse;
    }

    int AskCopyPermError(const wchar_t* sourceFile, const wchar_t* /*targetFile*/,
                         DWORD /*win32Error*/) override
    {
        Log("AskCopyPermError", sourceFile ? sourceFile : L"");
        return IgnoreErrorResponse;
    }

    int AskCopyDirTimeError(const wchar_t* dirName, DWORD /*errorCode*/) override
    {
        Log("AskCopyDirTimeError", dirName ? dirName : L"");
        return IgnoreErrorResponse;
    }

    // --- Encryption ---

    int AskEncryptionLoss(bool /*isEncrypted*/, const wchar_t* fileName, bool /*isDir*/) override
    {
        Log("AskEncryptionLoss", fileName ? fileName : L"");
        return EncryptionLossResponse;
    }

private:
    HANDLE m_completionEvent;
    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_error{false};
    std::atomic<bool> m_done{false};
    std::atomic<int> m_lastOpPercent{0};
    std::atomic<int> m_lastSumPercent{0};

    mutable std::mutex m_logMutex;
    std::vector<LogEntry> m_log;

    void Log(const char* method, const wchar_t* detail)
    {
        std::lock_guard<std::mutex> lk(m_logMutex);
        m_log.push_back({method, detail});
    }
};
