// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../../common/IProcess.h"

// For test build: provide gProcess definition
IProcess* gProcess = nullptr;

// Mock implementation for testing
class MockProcess : public IProcess
{
public:
    MOCK_METHOD(HPROCESS, CreateProcess, (const ProcessStartInfo& startInfo), (override));
    MOCK_METHOD(WaitResult, WaitForProcess, (HPROCESS process, DWORD timeoutMs), (override));
    MOCK_METHOD(ProcessResult, GetExitCode, (HPROCESS process, DWORD& exitCode), (override));
    MOCK_METHOD(ProcessResult, TerminateProcess, (HPROCESS process, UINT exitCode), (override));
    MOCK_METHOD(bool, IsProcessRunning, (HPROCESS process), (override));
    MOCK_METHOD(void, CloseProcess, (HPROCESS process), (override));
    MOCK_METHOD(DWORD, GetProcessId, (HPROCESS process), (override));
    MOCK_METHOD(HPROCESS, OpenProcess, (DWORD processId, DWORD desiredAccess), (override));
};

class ProcessTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        oldProcess = gProcess;
        gProcess = &mockProcess;
    }

    void TearDown() override
    {
        gProcess = oldProcess;
    }

    MockProcess mockProcess;
    IProcess* oldProcess;
};

TEST_F(ProcessTest, CreateProcess_ReturnsHandle)
{
    HPROCESS fakeHandle = reinterpret_cast<HPROCESS>(0x1234);

    EXPECT_CALL(mockProcess, CreateProcess(testing::_))
        .WillOnce(testing::Return(fakeHandle));

    ProcessStartInfo info;
    info.commandLine = L"notepad.exe";

    HPROCESS handle = gProcess->CreateProcess(info);
    EXPECT_EQ(handle, fakeHandle);
}

TEST_F(ProcessTest, WaitForProcess_ReturnsSignaled)
{
    HPROCESS fakeHandle = reinterpret_cast<HPROCESS>(0x1234);

    EXPECT_CALL(mockProcess, WaitForProcess(fakeHandle, INFINITE))
        .WillOnce(testing::Return(WaitResult::Signaled));

    auto result = gProcess->WaitForProcess(fakeHandle, INFINITE);
    EXPECT_EQ(result, WaitResult::Signaled);
}

TEST_F(ProcessTest, WaitForProcess_ReturnsTimeout)
{
    HPROCESS fakeHandle = reinterpret_cast<HPROCESS>(0x1234);

    EXPECT_CALL(mockProcess, WaitForProcess(fakeHandle, 1000))
        .WillOnce(testing::Return(WaitResult::Timeout));

    auto result = gProcess->WaitForProcess(fakeHandle, 1000);
    EXPECT_EQ(result, WaitResult::Timeout);
}

TEST_F(ProcessTest, GetExitCode_ReturnsCode)
{
    HPROCESS fakeHandle = reinterpret_cast<HPROCESS>(0x1234);
    DWORD expectedCode = 42;

    EXPECT_CALL(mockProcess, GetExitCode(fakeHandle, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<1>(expectedCode),
            testing::Return(ProcessResult::Ok())));

    DWORD exitCode;
    auto result = gProcess->GetExitCode(fakeHandle, exitCode);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(exitCode, 42u);
}

TEST_F(ProcessTest, IsProcessRunning_ReturnsTrue)
{
    HPROCESS fakeHandle = reinterpret_cast<HPROCESS>(0x1234);

    EXPECT_CALL(mockProcess, IsProcessRunning(fakeHandle))
        .WillOnce(testing::Return(true));

    EXPECT_TRUE(gProcess->IsProcessRunning(fakeHandle));
}

TEST_F(ProcessTest, TerminateProcess_Succeeds)
{
    HPROCESS fakeHandle = reinterpret_cast<HPROCESS>(0x1234);

    EXPECT_CALL(mockProcess, TerminateProcess(fakeHandle, 1))
        .WillOnce(testing::Return(ProcessResult::Ok()));

    auto result = gProcess->TerminateProcess(fakeHandle, 1);
    EXPECT_TRUE(result.success);
}

TEST_F(ProcessTest, CloseProcess_Called)
{
    HPROCESS fakeHandle = reinterpret_cast<HPROCESS>(0x1234);

    EXPECT_CALL(mockProcess, CloseProcess(fakeHandle))
        .Times(1);

    gProcess->CloseProcess(fakeHandle);
}

TEST_F(ProcessTest, GetProcessId_ReturnsId)
{
    HPROCESS fakeHandle = reinterpret_cast<HPROCESS>(0x1234);

    EXPECT_CALL(mockProcess, GetProcessId(fakeHandle))
        .WillOnce(testing::Return(12345));

    DWORD pid = gProcess->GetProcessId(fakeHandle);
    EXPECT_EQ(pid, 12345u);
}

TEST_F(ProcessTest, OpenProcess_ReturnsHandle)
{
    HPROCESS fakeHandle = reinterpret_cast<HPROCESS>(0x5678);

    EXPECT_CALL(mockProcess, OpenProcess(12345, PROCESS_QUERY_INFORMATION))
        .WillOnce(testing::Return(fakeHandle));

    HPROCESS handle = gProcess->OpenProcess(12345, PROCESS_QUERY_INFORMATION);
    EXPECT_EQ(handle, fakeHandle);
}

TEST(ProcessStartInfoTest, DefaultValues)
{
    ProcessStartInfo info;
    EXPECT_EQ(info.applicationName, nullptr);
    EXPECT_EQ(info.commandLine, nullptr);
    EXPECT_EQ(info.workingDirectory, nullptr);
    EXPECT_FALSE(info.inheritHandles);
    EXPECT_FALSE(info.createNewConsole);
    EXPECT_FALSE(info.hideWindow);
    EXPECT_EQ(info.creationFlags, 0u);
    EXPECT_EQ(info.hStdInput, nullptr);
    EXPECT_EQ(info.hStdOutput, nullptr);
    EXPECT_EQ(info.hStdError, nullptr);
}

TEST(WaitResultTest, EnumValues)
{
    EXPECT_NE(WaitResult::Signaled, WaitResult::Timeout);
    EXPECT_NE(WaitResult::Signaled, WaitResult::Failed);
    EXPECT_NE(WaitResult::Timeout, WaitResult::Failed);
}

TEST(ProcessResultTest, OkAndError)
{
    auto ok = ProcessResult::Ok();
    EXPECT_TRUE(ok.success);
    EXPECT_EQ(ok.errorCode, (DWORD)ERROR_SUCCESS);

    auto err = ProcessResult::Error(ERROR_ACCESS_DENIED);
    EXPECT_FALSE(err.success);
    EXPECT_EQ(err.errorCode, (DWORD)ERROR_ACCESS_DENIED);
}
