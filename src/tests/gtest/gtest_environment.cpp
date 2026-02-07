// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../../common/IEnvironment.h"

// For test build: provide gEnvironment definition
IEnvironment* gEnvironment = nullptr;

// Mock implementation for testing
class MockEnvironment : public IEnvironment
{
public:
    MOCK_METHOD(EnvResult, GetVariable, (const wchar_t* name, std::wstring& value), (override));
    MOCK_METHOD(EnvResult, SetVariable, (const wchar_t* name, const wchar_t* value), (override));
    MOCK_METHOD(EnvResult, GetTempPath, (std::wstring& path), (override));
    MOCK_METHOD(EnvResult, GetSystemDirectory, (std::wstring& path), (override));
    MOCK_METHOD(EnvResult, GetWindowsDirectory, (std::wstring& path), (override));
    MOCK_METHOD(EnvResult, GetCurrentDirectory, (std::wstring& path), (override));
    MOCK_METHOD(EnvResult, SetCurrentDirectory, (const wchar_t* path), (override));
    MOCK_METHOD(EnvResult, ExpandEnvironmentStrings, (const wchar_t* source, std::wstring& expanded), (override));
    MOCK_METHOD(EnvResult, GetComputerName, (std::wstring& name), (override));
    MOCK_METHOD(EnvResult, GetUserName, (std::wstring& name), (override));
};

class EnvironmentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        oldEnvironment = gEnvironment;
        gEnvironment = &mockEnvironment;
    }

    void TearDown() override
    {
        gEnvironment = oldEnvironment;
    }

    MockEnvironment mockEnvironment;
    IEnvironment* oldEnvironment;
};

TEST_F(EnvironmentTest, GetVariable_ReturnsValue)
{
    EXPECT_CALL(mockEnvironment, GetVariable(testing::StrEq(L"PATH"), testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<1>(L"C:\\Windows;C:\\Windows\\System32"),
            testing::Return(EnvResult::Ok())));

    std::wstring value;
    auto result = gEnvironment->GetVariable(L"PATH", value);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(value, L"C:\\Windows;C:\\Windows\\System32");
}

TEST_F(EnvironmentTest, GetVariable_NotFound)
{
    EXPECT_CALL(mockEnvironment, GetVariable(testing::_, testing::_))
        .WillOnce(testing::Return(EnvResult::Error(ERROR_ENVVAR_NOT_FOUND)));

    std::wstring value;
    auto result = gEnvironment->GetVariable(L"NONEXISTENT", value);
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.notFound());
}

TEST_F(EnvironmentTest, SetVariable_Succeeds)
{
    EXPECT_CALL(mockEnvironment, SetVariable(testing::StrEq(L"MY_VAR"), testing::StrEq(L"my_value")))
        .WillOnce(testing::Return(EnvResult::Ok()));

    auto result = gEnvironment->SetVariable(L"MY_VAR", L"my_value");
    EXPECT_TRUE(result.success);
}

TEST_F(EnvironmentTest, GetTempPath_ReturnsPath)
{
    EXPECT_CALL(mockEnvironment, GetTempPath(testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<0>(L"C:\\Users\\Test\\AppData\\Local\\Temp\\"),
            testing::Return(EnvResult::Ok())));

    std::wstring path;
    auto result = gEnvironment->GetTempPath(path);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(path, L"C:\\Users\\Test\\AppData\\Local\\Temp\\");
}

TEST_F(EnvironmentTest, GetSystemDirectory_ReturnsPath)
{
    EXPECT_CALL(mockEnvironment, GetSystemDirectory(testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<0>(L"C:\\Windows\\System32"),
            testing::Return(EnvResult::Ok())));

    std::wstring path;
    auto result = gEnvironment->GetSystemDirectory(path);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(path, L"C:\\Windows\\System32");
}

TEST_F(EnvironmentTest, GetCurrentDirectory_ReturnsPath)
{
    EXPECT_CALL(mockEnvironment, GetCurrentDirectory(testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<0>(L"C:\\Projects"),
            testing::Return(EnvResult::Ok())));

    std::wstring path;
    auto result = gEnvironment->GetCurrentDirectory(path);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(path, L"C:\\Projects");
}

TEST_F(EnvironmentTest, SetCurrentDirectory_Succeeds)
{
    EXPECT_CALL(mockEnvironment, SetCurrentDirectory(testing::StrEq(L"C:\\NewDir")))
        .WillOnce(testing::Return(EnvResult::Ok()));

    auto result = gEnvironment->SetCurrentDirectory(L"C:\\NewDir");
    EXPECT_TRUE(result.success);
}

TEST_F(EnvironmentTest, ExpandEnvironmentStrings_Expands)
{
    EXPECT_CALL(mockEnvironment, ExpandEnvironmentStrings(testing::StrEq(L"%USERPROFILE%\\Documents"), testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<1>(L"C:\\Users\\Test\\Documents"),
            testing::Return(EnvResult::Ok())));

    std::wstring expanded;
    auto result = gEnvironment->ExpandEnvironmentStrings(L"%USERPROFILE%\\Documents", expanded);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(expanded, L"C:\\Users\\Test\\Documents");
}

TEST_F(EnvironmentTest, GetComputerName_ReturnsName)
{
    EXPECT_CALL(mockEnvironment, GetComputerName(testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<0>(L"MYCOMPUTER"),
            testing::Return(EnvResult::Ok())));

    std::wstring name;
    auto result = gEnvironment->GetComputerName(name);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(name, L"MYCOMPUTER");
}

TEST_F(EnvironmentTest, GetUserName_ReturnsName)
{
    EXPECT_CALL(mockEnvironment, GetUserName(testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<0>(L"TestUser"),
            testing::Return(EnvResult::Ok())));

    std::wstring name;
    auto result = gEnvironment->GetUserName(name);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(name, L"TestUser");
}

TEST(EnvResultTest, OkAndError)
{
    auto ok = EnvResult::Ok();
    EXPECT_TRUE(ok.success);
    EXPECT_EQ(ok.errorCode, (DWORD)ERROR_SUCCESS);

    auto err = EnvResult::Error(ERROR_ACCESS_DENIED);
    EXPECT_FALSE(err.success);
    EXPECT_EQ(err.errorCode, (DWORD)ERROR_ACCESS_DENIED);
}

TEST(EnvResultTest, NotFound)
{
    auto notFound = EnvResult::Error(ERROR_ENVVAR_NOT_FOUND);
    EXPECT_FALSE(notFound.success);
    EXPECT_TRUE(notFound.notFound());

    auto other = EnvResult::Error(ERROR_ACCESS_DENIED);
    EXPECT_FALSE(other.notFound());
}
