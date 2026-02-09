// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Google Test suite for IWorkerObserver / CTestWorkerObserver
//
// Validates the headless observer interface contract:
//   - Auto-answer policies produce correct return values
//   - Call tracking captures all interactions
//   - Completion signaling works for headless worker control
//   - Cancellation propagation
//
//****************************************************************************

#include <gtest/gtest.h>
#include <windows.h>

// Minimal CProgressData stub (the real one is in dialogs.h)
struct CProgressData
{
    const char* Operation;
    const char* Source;
    const char* Preposition;
    const char* Target;
};

#include "TestWorkerObserver.h"

// ============================================================================
// Basic observer behavior
// ============================================================================

TEST(TestWorkerObserver, InitialState)
{
    CTestWorkerObserver obs;
    EXPECT_FALSE(obs.IsCancelled());
    EXPECT_FALSE(obs.HasError());
    EXPECT_EQ(obs.GetLastOperationPercent(), 0);
    EXPECT_EQ(obs.GetLastSummaryPercent(), 0);
    EXPECT_TRUE(obs.GetCalls().empty());
}

TEST(TestWorkerObserver, ProgressTracking)
{
    CTestWorkerObserver obs;
    obs.SetProgress(500, 250);
    EXPECT_EQ(obs.GetLastOperationPercent(), 500);
    EXPECT_EQ(obs.GetLastSummaryPercent(), 250);

    obs.SetProgress(1000, 1000);
    EXPECT_EQ(obs.GetLastOperationPercent(), 1000);
    EXPECT_EQ(obs.GetLastSummaryPercent(), 1000);
}

TEST(TestWorkerObserver, ProgressWithoutSuspend)
{
    CTestWorkerObserver obs;
    obs.SetProgressWithoutSuspend(750, 500);
    EXPECT_EQ(obs.GetLastOperationPercent(), 750);
    EXPECT_EQ(obs.GetLastSummaryPercent(), 500);
}

TEST(TestWorkerObserver, CancellationPropagation)
{
    CTestWorkerObserver obs;
    EXPECT_FALSE(obs.IsCancelled());
    obs.Cancel();
    EXPECT_TRUE(obs.IsCancelled());
}

TEST(TestWorkerObserver, ErrorState)
{
    CTestWorkerObserver obs;
    EXPECT_FALSE(obs.HasError());
    obs.SetError(true);
    EXPECT_TRUE(obs.HasError());
    obs.SetError(false);
    EXPECT_FALSE(obs.HasError());
}

TEST(TestWorkerObserver, CompletionSignaling)
{
    CTestWorkerObserver obs;
    // Not yet signaled
    EXPECT_EQ(WaitForSingleObject(obs.GetCompletionEvent(), 0), WAIT_TIMEOUT);

    obs.NotifyDone();

    // Now signaled
    EXPECT_TRUE(obs.WaitForCompletion(0));
}

TEST(TestWorkerObserver, WaitIfSuspendedNeverBlocks)
{
    CTestWorkerObserver obs;
    // Should return immediately (no suspend in tests)
    obs.WaitIfSuspended();
    // If we get here, it didn't block
    SUCCEED();
}

// ============================================================================
// Dialog policy: file errors
// ============================================================================

TEST(TestWorkerObserver, FileErrorPolicySkip)
{
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkip);
    int ret = obs.AskFileError("Error", "C:\\test.txt", "Access denied");
    EXPECT_EQ(ret, IDB_SKIP);
}

TEST(TestWorkerObserver, FileErrorPolicyRetry)
{
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kRetry);
    int ret = obs.AskFileError("Error", "C:\\test.txt", "Sharing violation");
    EXPECT_EQ(ret, IDRETRY);
}

TEST(TestWorkerObserver, FileErrorPolicyCancel)
{
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kCancel);
    int ret = obs.AskFileError("Error", "C:\\test.txt", "Disk full");
    EXPECT_EQ(ret, IDCANCEL);
}

TEST(TestWorkerObserver, FileErrorPolicySkipAll)
{
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);
    int ret = obs.AskFileError("Error", "C:\\test.txt", "Whatever");
    EXPECT_EQ(ret, IDB_SKIPALL);
}

// ============================================================================
// Dialog policy: overwrite
// ============================================================================

TEST(TestWorkerObserver, OverwritePolicyYes)
{
    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kYes);
    int ret = obs.AskOverwrite("src.txt", "100 KB", "dst.txt", "50 KB");
    EXPECT_EQ(ret, IDYES);
}

TEST(TestWorkerObserver, OverwritePolicyYesAll)
{
    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kYesAll);
    int ret = obs.AskOverwrite("src.txt", "100 KB", "dst.txt", "50 KB");
    EXPECT_EQ(ret, IDB_ALL);
}

TEST(TestWorkerObserver, OverwritePolicySkip)
{
    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kSkip);
    int ret = obs.AskOverwrite("src.txt", "100 KB", "dst.txt", "50 KB");
    EXPECT_EQ(ret, IDB_SKIP);
}

// ============================================================================
// Dialog policy: hidden/system files
// ============================================================================

TEST(TestWorkerObserver, HiddenSystemPolicyYes)
{
    CTestWorkerObserver obs;
    obs.SetHiddenSystemPolicy(TestDialogPolicy::kYes);
    int ret = obs.AskHiddenOrSystem("Confirm", "C:\\hidden.sys", "Delete?");
    EXPECT_EQ(ret, IDYES);
}

TEST(TestWorkerObserver, HiddenSystemPolicyCancel)
{
    CTestWorkerObserver obs;
    obs.SetHiddenSystemPolicy(TestDialogPolicy::kCancel);
    int ret = obs.AskHiddenOrSystem("Confirm", "C:\\hidden.sys", "Delete?");
    EXPECT_EQ(ret, IDCANCEL);
}

// ============================================================================
// Dialog policy: encryption loss
// ============================================================================

TEST(TestWorkerObserver, EncryptionLossYesAll)
{
    CTestWorkerObserver obs;
    obs.SetEncryptionLossPolicy(TestDialogPolicy::kYesAll);
    int ret = obs.AskEncryptionLoss(true, "C:\\encrypted.doc", false);
    EXPECT_EQ(ret, IDB_ALL);
}

// ============================================================================
// Call tracking
// ============================================================================

TEST(TestWorkerObserver, CallLogging)
{
    CTestWorkerObserver obs;

    CProgressData pd = {"Deleting", "C:\\file.txt", "", ""};
    obs.SetOperationInfo(&pd);
    obs.SetProgress(0, 0);
    obs.AskFileError("Error", "C:\\file.txt", "Access denied");
    obs.SetError(false);
    obs.NotifyDone();

    EXPECT_EQ(obs.GetCalls().size(), 5u);
    EXPECT_EQ(obs.GetCalls()[0].type, TestObserverCall::kSetOperationInfo);
    EXPECT_EQ(obs.GetCalls()[1].type, TestObserverCall::kSetProgress);
    EXPECT_EQ(obs.GetCalls()[2].type, TestObserverCall::kAskFileError);
    EXPECT_EQ(obs.GetCalls()[3].type, TestObserverCall::kSetError);
    EXPECT_EQ(obs.GetCalls()[4].type, TestObserverCall::kNotifyDone);
}

TEST(TestWorkerObserver, CountCallsOfType)
{
    CTestWorkerObserver obs;

    obs.SetProgress(100, 50);
    obs.SetProgress(200, 100);
    obs.SetProgress(300, 150);
    obs.AskFileError("E", "f", "e");
    obs.AskFileError("E", "f2", "e2");

    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kSetProgress), 3);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 2);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kNotifyDone), 0);
}

TEST(TestWorkerObserver, CallArgCapture)
{
    CTestWorkerObserver obs;
    obs.AskFileError("Delete Error", "C:\\important\\file.txt", "Permission denied");

    ASSERT_EQ(obs.GetCalls().size(), 1u);
    EXPECT_EQ(obs.GetCalls()[0].arg1, "C:\\important\\file.txt");
    EXPECT_EQ(obs.GetCalls()[0].arg2, "Permission denied");
}

// ============================================================================
// Simulated worker flow
// ============================================================================

TEST(TestWorkerObserver, SimulatedDeleteFlow)
{
    // Simulate what the worker does for a delete operation
    CTestWorkerObserver obs;
    obs.SetHiddenSystemPolicy(TestDialogPolicy::kYesAll);

    // Worker sets up operation info
    CProgressData pd = {"Deleting", "C:\\test\\file.txt", "", ""};
    obs.SetOperationInfo(&pd);
    obs.SetProgress(0, 0);

    // Worker encounters hidden file, asks for confirmation
    int ret = obs.AskHiddenOrSystem("Confirm", "C:\\test\\file.txt", "Delete hidden file?");
    EXPECT_EQ(ret, IDB_ALL); // YesAll → IDB_ALL

    // Worker completes
    obs.SetProgress(0, 1000);
    obs.SetError(false);
    obs.NotifyDone();

    // Verify the flow
    EXPECT_FALSE(obs.HasError());
    EXPECT_TRUE(obs.WaitForCompletion(0));
    EXPECT_EQ(obs.GetLastSummaryPercent(), 1000);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskHiddenOrSystem), 1);
}

TEST(TestWorkerObserver, SimulatedCancelledOperation)
{
    CTestWorkerObserver obs;

    // Simulate worker checking cancel at loop start
    EXPECT_FALSE(obs.IsCancelled());

    // UI thread cancels mid-operation
    obs.Cancel();

    // Worker checks again
    EXPECT_TRUE(obs.IsCancelled());

    // Worker exits with error
    obs.SetError(true);
    obs.NotifyDone();

    EXPECT_TRUE(obs.HasError());
    EXPECT_TRUE(obs.WaitForCompletion(0));
}

TEST(TestWorkerObserver, SimulatedMultiFileDelete)
{
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkip);

    // Process 3 files, second one fails
    for (int i = 0; i < 3; i++)
    {
        if (obs.IsCancelled())
            break;

        CProgressData pd = {"Deleting", "file", "", ""};
        obs.SetOperationInfo(&pd);

        if (i == 1)
        {
            // Simulate error on second file
            int ret = obs.AskFileError("Error", "file2.txt", "Locked");
            EXPECT_EQ(ret, IDB_SKIP);
        }

        obs.SetProgress(0, (i + 1) * 333);
    }

    obs.SetError(false);
    obs.NotifyDone();

    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 1);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kSetOperationInfo), 3);
    EXPECT_TRUE(obs.WaitForCompletion(0));
}
