// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <shlobj.h>
#include "../../common/IShell.h"

// For test build: provide gShell definition
IShell* gShell = nullptr;

// Mock implementation for testing
class MockShell : public IShell
{
public:
    MOCK_METHOD(ShellExecResult, Execute, (const ShellExecInfo& info), (override));
    MOCK_METHOD(ShellResult, FileOperation, (ShellFileOp operation,
                                             const wchar_t* sourcePaths,
                                             const wchar_t* destPath,
                                             DWORD flags,
                                             HWND hwnd), (override));
    MOCK_METHOD(ShellResult, GetFileInfo, (const wchar_t* path,
                                           DWORD attributes,
                                           SHFILEINFOW& info,
                                           UINT flags), (override));
    MOCK_METHOD(bool, BrowseForFolder, (HWND hwnd,
                                        const wchar_t* title,
                                        UINT flags,
                                        std::wstring& selectedPath), (override));
    MOCK_METHOD(ShellResult, GetSpecialFolderPath, (int csidl,
                                                    std::wstring& path,
                                                    bool create), (override));
};

class ShellTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        oldShell = gShell;
        gShell = &mockShell;
    }

    void TearDown() override
    {
        gShell = oldShell;
    }

    MockShell mockShell;
    IShell* oldShell;
};

TEST_F(ShellTest, Execute_ReturnsSuccess)
{
    EXPECT_CALL(mockShell, Execute(testing::_))
        .WillOnce(testing::Return(ShellExecResult::Ok(reinterpret_cast<HINSTANCE>(33))));

    ShellExecInfo info;
    info.file = L"notepad.exe";

    auto result = gShell->Execute(info);
    EXPECT_TRUE(result.success);
}

TEST_F(ShellTest, Execute_ReturnsError)
{
    EXPECT_CALL(mockShell, Execute(testing::_))
        .WillOnce(testing::Return(ShellExecResult::Error(ERROR_FILE_NOT_FOUND)));

    ShellExecInfo info;
    info.file = L"nonexistent.exe";

    auto result = gShell->Execute(info);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, (DWORD)ERROR_FILE_NOT_FOUND);
}

TEST_F(ShellTest, FileOperation_DeleteSucceeds)
{
    EXPECT_CALL(mockShell, FileOperation(ShellFileOp::Delete, testing::_, nullptr, testing::_, nullptr))
        .WillOnce(testing::Return(ShellResult::Ok()));

    auto result = gShell->FileOperation(ShellFileOp::Delete, L"C:\\test.txt\0", nullptr,
                                        OpNoConfirmation | OpSilent, nullptr);
    EXPECT_TRUE(result.success);
}

TEST_F(ShellTest, FileOperation_CopySucceeds)
{
    EXPECT_CALL(mockShell, FileOperation(ShellFileOp::Copy, testing::_, testing::_, testing::_, nullptr))
        .WillOnce(testing::Return(ShellResult::Ok()));

    auto result = gShell->FileOperation(ShellFileOp::Copy, L"C:\\src.txt\0", L"C:\\dst.txt\0",
                                        OpNoConfirmation, nullptr);
    EXPECT_TRUE(result.success);
}

TEST_F(ShellTest, GetFileInfo_ReturnsInfo)
{
    EXPECT_CALL(mockShell, GetFileInfo(testing::_, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(ShellResult::Ok()));

    SHFILEINFOW info;
    auto result = gShell->GetFileInfo(L"C:\\test.txt", 0, info, SHGFI_TYPENAME);
    EXPECT_TRUE(result.success);
}

TEST_F(ShellTest, BrowseForFolder_ReturnsPath)
{
    EXPECT_CALL(mockShell, BrowseForFolder(nullptr, testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<3>(L"C:\\Selected\\Folder"),
            testing::Return(true)));

    std::wstring path;
    bool result = gShell->BrowseForFolder(nullptr, L"Select Folder", BIF_RETURNONLYFSDIRS, path);
    EXPECT_TRUE(result);
    EXPECT_EQ(path, L"C:\\Selected\\Folder");
}

TEST_F(ShellTest, BrowseForFolder_Cancelled)
{
    EXPECT_CALL(mockShell, BrowseForFolder(nullptr, testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(false));

    std::wstring path;
    bool result = gShell->BrowseForFolder(nullptr, L"Select Folder", 0, path);
    EXPECT_FALSE(result);
}

TEST_F(ShellTest, GetSpecialFolderPath_ReturnsPath)
{
    EXPECT_CALL(mockShell, GetSpecialFolderPath(CSIDL_DESKTOP, testing::_, false))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<1>(L"C:\\Users\\Test\\Desktop"),
            testing::Return(ShellResult::Ok())));

    std::wstring path;
    auto result = gShell->GetSpecialFolderPath(CSIDL_DESKTOP, path, false);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(path, L"C:\\Users\\Test\\Desktop");
}

TEST(ShellExecInfoTest, DefaultValues)
{
    ShellExecInfo info;
    EXPECT_EQ(info.file, nullptr);
    EXPECT_EQ(info.parameters, nullptr);
    EXPECT_EQ(info.verb, nullptr);
    EXPECT_EQ(info.directory, nullptr);
    EXPECT_EQ(info.showCommand, SW_SHOWNORMAL);
    EXPECT_EQ(info.hwnd, nullptr);
}

TEST(ShellResultTest, OkAndError)
{
    auto ok = ShellResult::Ok();
    EXPECT_TRUE(ok.success);
    EXPECT_EQ(ok.errorCode, (DWORD)ERROR_SUCCESS);

    auto err = ShellResult::Error(ERROR_ACCESS_DENIED);
    EXPECT_FALSE(err.success);
    EXPECT_EQ(err.errorCode, (DWORD)ERROR_ACCESS_DENIED);
}

TEST(ShellExecResultTest, OkAndError)
{
    auto ok = ShellExecResult::Ok(reinterpret_cast<HINSTANCE>(42));
    EXPECT_TRUE(ok.success);
    EXPECT_EQ(ok.hInstance, reinterpret_cast<HINSTANCE>(42));

    auto err = ShellExecResult::Error(ERROR_FILE_NOT_FOUND);
    EXPECT_FALSE(err.success);
    EXPECT_EQ(err.errorCode, (DWORD)ERROR_FILE_NOT_FOUND);
}

TEST(ShellFileOpTest, EnumValues)
{
    EXPECT_EQ(static_cast<UINT>(ShellFileOp::Move), FO_MOVE);
    EXPECT_EQ(static_cast<UINT>(ShellFileOp::Copy), FO_COPY);
    EXPECT_EQ(static_cast<UINT>(ShellFileOp::Delete), FO_DELETE);
    EXPECT_EQ(static_cast<UINT>(ShellFileOp::Rename), FO_RENAME);
}
