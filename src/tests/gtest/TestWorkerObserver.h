// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// CTestWorkerObserver — headless observer for automated testing.
//
// Auto-answers all Ask* dialogs with configurable policies (skip, retry,
// overwrite, cancel). Tracks all calls for assertion in tests.
// No HWND, no message pump, no UI dependencies.

#pragma once

#include "IWorkerObserver.h"
#include <vector>
#include <string>

// Policy for how the test observer auto-answers dialog prompts
enum class TestDialogPolicy
{
    kSkip,      // IDB_SKIP — skip this item
    kSkipAll,   // IDB_SKIPALL — skip all similar
    kRetry,     // IDRETRY — retry the operation
    kYes,       // IDYES — confirm (overwrite, delete hidden, etc.)
    kYesAll,    // IDB_ALL — confirm all
    kNo,        // IDNO — decline
    kCancel,    // IDCANCEL — cancel the entire operation
    kIgnore,    // IDB_IGNORE — ignore this error
    kIgnoreAll, // IDB_ALL (ignore variant) — ignore all similar
};

// Record of a single observer call (for test assertions)
struct TestObserverCall
{
    enum Type
    {
        kSetOperationInfo,
        kSetProgress,
        kWaitIfSuspended,
        kIsCancelled,
        kSetError,
        kNotifyDone,
        kAskFileError,
        kAskOverwrite,
        kAskHiddenOrSystem,
        kAskCannotMove,
        kNotifyError,
        kAskADSReadError,
        kAskADSOverwrite,
        kAskADSOpenError,
        kAskSetAttrsError,
        kAskCopyPermError,
        kAskCopyDirTimeError,
        kAskEncryptionLoss,
    };

    Type type;
    std::string arg1; // primary argument (title/fileName/etc.)
    std::string arg2; // secondary argument
    int returnValue;  // what we returned
};

// Standard dialog return values (match salamander resource IDs)
#ifndef IDB_SKIP
#define IDB_SKIP 200
#endif
#ifndef IDB_SKIPALL
#define IDB_SKIPALL 201
#endif
#ifndef IDB_ALL
#define IDB_ALL 202
#endif
#ifndef IDB_IGNORE
#define IDB_IGNORE 203
#endif

class CTestWorkerObserver : public IWorkerObserver
{
    // Completion signaling
    HANDLE m_completionEvent; // signaled when NotifyDone() is called
    bool m_cancelled;
    bool m_error;

    // Progress tracking
    int m_lastOperationPercent;
    int m_lastSummaryPercent;

    // Dialog policies
    TestDialogPolicy m_fileErrorPolicy;
    TestDialogPolicy m_overwritePolicy;
    TestDialogPolicy m_hiddenSystemPolicy;
    TestDialogPolicy m_cannotMovePolicy;
    TestDialogPolicy m_encryptionLossPolicy;

    // Call log
    std::vector<TestObserverCall> m_calls;

    int PolicyToReturnValue(TestDialogPolicy policy)
    {
        switch (policy)
        {
        case TestDialogPolicy::kSkip:
            return IDB_SKIP;
        case TestDialogPolicy::kSkipAll:
            return IDB_SKIPALL;
        case TestDialogPolicy::kRetry:
            return IDRETRY;
        case TestDialogPolicy::kYes:
            return IDYES;
        case TestDialogPolicy::kYesAll:
            return IDB_ALL;
        case TestDialogPolicy::kNo:
            return IDNO;
        case TestDialogPolicy::kCancel:
            return IDCANCEL;
        case TestDialogPolicy::kIgnore:
            return IDB_IGNORE;
        case TestDialogPolicy::kIgnoreAll:
            return IDB_ALL;
        default:
            return IDCANCEL;
        }
    }

public:
    CTestWorkerObserver()
        : m_completionEvent(CreateEvent(NULL, TRUE, FALSE, NULL))
        , m_cancelled(false)
        , m_error(false)
        , m_lastOperationPercent(0)
        , m_lastSummaryPercent(0)
        , m_fileErrorPolicy(TestDialogPolicy::kSkip)
        , m_overwritePolicy(TestDialogPolicy::kYes)
        , m_hiddenSystemPolicy(TestDialogPolicy::kYes)
        , m_cannotMovePolicy(TestDialogPolicy::kSkip)
        , m_encryptionLossPolicy(TestDialogPolicy::kYes)
    {
    }

    ~CTestWorkerObserver()
    {
        if (m_completionEvent)
            CloseHandle(m_completionEvent);
    }

    // --- Configuration ---

    void SetFileErrorPolicy(TestDialogPolicy p) { m_fileErrorPolicy = p; }
    void SetOverwritePolicy(TestDialogPolicy p) { m_overwritePolicy = p; }
    void SetHiddenSystemPolicy(TestDialogPolicy p) { m_hiddenSystemPolicy = p; }
    void SetCannotMovePolicy(TestDialogPolicy p) { m_cannotMovePolicy = p; }
    void SetEncryptionLossPolicy(TestDialogPolicy p) { m_encryptionLossPolicy = p; }

    void Cancel() { m_cancelled = true; }

    // --- Results ---

    HANDLE GetCompletionEvent() const { return m_completionEvent; }
    bool WaitForCompletion(DWORD timeoutMs = 30000)
    {
        return WaitForSingleObject(m_completionEvent, timeoutMs) == WAIT_OBJECT_0;
    }

    bool HasError() const { return m_error; }
    int GetLastOperationPercent() const { return m_lastOperationPercent; }
    int GetLastSummaryPercent() const { return m_lastSummaryPercent; }

    const std::vector<TestObserverCall>& GetCalls() const { return m_calls; }

    int CountCallsOfType(TestObserverCall::Type type) const
    {
        int count = 0;
        for (const auto& c : m_calls)
            if (c.type == type)
                count++;
        return count;
    }

    // --- IWorkerObserver implementation ---

    void SetOperationInfo(CProgressData* /*data*/) override
    {
        m_calls.push_back({TestObserverCall::kSetOperationInfo, {}, {}, 0});
    }

    void SetProgress(int operationPercent, int summaryPercent) override
    {
        m_lastOperationPercent = operationPercent;
        m_lastSummaryPercent = summaryPercent;
        m_calls.push_back({TestObserverCall::kSetProgress, {}, {}, summaryPercent});
    }

    void SetProgressWithoutSuspend(int operationPercent, int summaryPercent) override
    {
        m_lastOperationPercent = operationPercent;
        m_lastSummaryPercent = summaryPercent;
    }

    void WaitIfSuspended() override
    {
        // Never suspend in tests
    }

    bool IsCancelled() const override
    {
        return m_cancelled;
    }

    void SetError(bool error) override
    {
        m_error = error;
        m_calls.push_back({TestObserverCall::kSetError, {}, {}, error ? 1 : 0});
    }

    void NotifyDone() override
    {
        m_calls.push_back({TestObserverCall::kNotifyDone, {}, {}, 0});
        SetEvent(m_completionEvent);
    }

    HWND GetParentWindow() const override
    {
        return NULL; // headless — no parent window
    }

    int AskFileError(const char* title, const char* fileName, const char* errorText) override
    {
        int ret = PolicyToReturnValue(m_fileErrorPolicy);
        m_calls.push_back({TestObserverCall::kAskFileError,
                           fileName ? fileName : "",
                           errorText ? errorText : "", ret});
        return ret;
    }

    int AskFileErrorById(int titleId, const char* fileName, DWORD win32Error) override
    {
        int ret = PolicyToReturnValue(m_fileErrorPolicy);
        char titleBuf[32];
        wsprintfA(titleBuf, "IDS_%d", titleId);
        char errBuf[32];
        wsprintfA(errBuf, "err_%lu", win32Error);
        m_calls.push_back({TestObserverCall::kAskFileError,
                           fileName ? fileName : "",
                           errBuf, ret});
        return ret;
    }

    int AskFileErrorByIds(int titleId, const char* fileName, int errorTextId) override
    {
        int ret = PolicyToReturnValue(m_fileErrorPolicy);
        char buf[64];
        wsprintfA(buf, "IDS_%d/IDS_%d", titleId, errorTextId);
        m_calls.push_back({TestObserverCall::kAskFileError,
                           fileName ? fileName : "",
                           buf, ret});
        return ret;
    }

    int AskOverwrite(const char* sourceName, const char* sourceInfo,
                     const char* targetName, const char* targetInfo) override
    {
        int ret = PolicyToReturnValue(m_overwritePolicy);
        m_calls.push_back({TestObserverCall::kAskOverwrite,
                           sourceName ? sourceName : "",
                           targetName ? targetName : "", ret});
        return ret;
    }

    int AskHiddenOrSystem(const char* title, const char* fileName,
                          const char* actionText) override
    {
        int ret = PolicyToReturnValue(m_hiddenSystemPolicy);
        m_calls.push_back({TestObserverCall::kAskHiddenOrSystem,
                           fileName ? fileName : "", {}, ret});
        return ret;
    }

    int AskHiddenOrSystemById(int titleId, const char* fileName, int actionId) override
    {
        int ret = PolicyToReturnValue(m_hiddenSystemPolicy);
        m_calls.push_back({TestObserverCall::kAskHiddenOrSystem,
                           fileName ? fileName : "", {}, ret});
        return ret;
    }

    int AskCannotMove(const char* errorText, const char* fileName,
                      const char* destPath, bool isDirectory) override
    {
        int ret = PolicyToReturnValue(m_cannotMovePolicy);
        m_calls.push_back({TestObserverCall::kAskCannotMove,
                           fileName ? fileName : "", {}, ret});
        return ret;
    }

    int AskCannotMoveErr(const char* sourceName, const char* targetName,
                         DWORD win32Error, bool isDirectory) override
    {
        int ret = PolicyToReturnValue(m_cannotMovePolicy);
        m_calls.push_back({TestObserverCall::kAskCannotMove,
                           sourceName ? sourceName : "", {}, ret});
        return ret;
    }

    void NotifyError(const char* title, const char* fileName,
                     const char* errorText) override
    {
        m_calls.push_back({TestObserverCall::kNotifyError,
                           fileName ? fileName : "",
                           errorText ? errorText : "", 0});
    }

    void NotifyErrorById(int titleId, const char* fileName, int detailId) override
    {
        char buf[64];
        wsprintfA(buf, "IDS_%d/IDS_%d", titleId, detailId);
        m_calls.push_back({TestObserverCall::kNotifyError,
                           fileName ? fileName : "",
                           buf, 0});
    }

    int AskADSReadError(const char* fileName, const char* adsName) override
    {
        int ret = PolicyToReturnValue(m_fileErrorPolicy);
        m_calls.push_back({TestObserverCall::kAskADSReadError,
                           fileName ? fileName : "", {}, ret});
        return ret;
    }

    int AskADSOverwrite(const char* sourceName, const char* sourceInfo,
                        const char* targetName, const char* targetInfo) override
    {
        int ret = PolicyToReturnValue(m_overwritePolicy);
        m_calls.push_back({TestObserverCall::kAskADSOverwrite,
                           sourceName ? sourceName : "", {}, ret});
        return ret;
    }

    int AskADSOpenError(const char* fileName, const char* adsName,
                        const char* errorText) override
    {
        int ret = PolicyToReturnValue(m_fileErrorPolicy);
        m_calls.push_back({TestObserverCall::kAskADSOpenError,
                           fileName ? fileName : "", {}, ret});
        return ret;
    }

    int AskADSOpenErrorById(int titleId, const char* fileName, DWORD win32Error) override
    {
        int ret = PolicyToReturnValue(m_fileErrorPolicy);
        m_calls.push_back({TestObserverCall::kAskADSOpenError,
                           fileName ? fileName : "", {}, ret});
        return ret;
    }

    int AskSetAttrsError(const char* fileName, DWORD failedAttrs,
                         DWORD currentAttrs) override
    {
        int ret = PolicyToReturnValue(m_fileErrorPolicy);
        m_calls.push_back({TestObserverCall::kAskSetAttrsError,
                           fileName ? fileName : "", {}, ret});
        return ret;
    }

    int AskCopyPermError(const char* sourceFile, const char* targetFile,
                         const char* errorText) override
    {
        int ret = PolicyToReturnValue(m_fileErrorPolicy);
        m_calls.push_back({TestObserverCall::kAskCopyPermError,
                           sourceFile ? sourceFile : "", {}, ret});
        return ret;
    }

    int AskCopyDirTimeError(const char* dirName, DWORD errorCode) override
    {
        int ret = PolicyToReturnValue(m_fileErrorPolicy);
        m_calls.push_back({TestObserverCall::kAskCopyDirTimeError,
                           dirName ? dirName : "", {}, ret});
        return ret;
    }

    int AskEncryptionLoss(bool isEncrypted, const char* fileName, bool isDir) override
    {
        int ret = PolicyToReturnValue(m_encryptionLossPolicy);
        m_calls.push_back({TestObserverCall::kAskEncryptionLoss,
                           fileName ? fileName : "", {}, ret});
        return ret;
    }
};
