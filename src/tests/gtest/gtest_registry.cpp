// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../../common/IRegistry.h"

// For test build: provide gRegistry definition
IRegistry* gRegistry = nullptr;

// Mock implementation for testing
class MockRegistry : public IRegistry
{
public:
    MOCK_METHOD(RegistryResult, OpenKeyRead, (HKEY root, const wchar_t* subKey, HKEY& outKey), (override));
    MOCK_METHOD(RegistryResult, OpenKeyReadWrite, (HKEY root, const wchar_t* subKey, HKEY& outKey), (override));
    MOCK_METHOD(RegistryResult, CreateKey, (HKEY root, const wchar_t* subKey, HKEY& outKey), (override));
    MOCK_METHOD(void, CloseKey, (HKEY key), (override));
    MOCK_METHOD(RegistryResult, DeleteKey, (HKEY root, const wchar_t* subKey), (override));
    MOCK_METHOD(RegistryResult, DeleteKeyRecursive, (HKEY root, const wchar_t* subKey), (override));
    MOCK_METHOD(RegistryResult, GetString, (HKEY key, const wchar_t* valueName, std::wstring& value), (override));
    MOCK_METHOD(RegistryResult, GetDWord, (HKEY key, const wchar_t* valueName, DWORD& value), (override));
    MOCK_METHOD(RegistryResult, GetQWord, (HKEY key, const wchar_t* valueName, uint64_t& value), (override));
    MOCK_METHOD(RegistryResult, GetBinary, (HKEY key, const wchar_t* valueName, std::vector<uint8_t>& value), (override));
    MOCK_METHOD(RegistryResult, GetValue, (HKEY key, const wchar_t* valueName, RegValueType& type, std::vector<uint8_t>& data), (override));
    MOCK_METHOD(RegistryResult, SetString, (HKEY key, const wchar_t* valueName, const wchar_t* value), (override));
    MOCK_METHOD(RegistryResult, SetDWord, (HKEY key, const wchar_t* valueName, DWORD value), (override));
    MOCK_METHOD(RegistryResult, SetQWord, (HKEY key, const wchar_t* valueName, uint64_t value), (override));
    MOCK_METHOD(RegistryResult, SetBinary, (HKEY key, const wchar_t* valueName, const void* data, size_t size), (override));
    MOCK_METHOD(RegistryResult, DeleteValue, (HKEY key, const wchar_t* valueName), (override));
    MOCK_METHOD(RegistryResult, EnumSubKeys, (HKEY key, std::vector<std::wstring>& subKeys), (override));
    MOCK_METHOD(RegistryResult, EnumValues, (HKEY key, std::vector<std::wstring>& valueNames), (override));
    MOCK_METHOD(bool, KeyExists, (HKEY root, const wchar_t* subKey), (override));
    MOCK_METHOD(bool, ValueExists, (HKEY key, const wchar_t* valueName), (override));
};

class RegistryTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        oldRegistry = gRegistry;
        gRegistry = &mockRegistry;
    }

    void TearDown() override
    {
        gRegistry = oldRegistry;
    }

    MockRegistry mockRegistry;
    IRegistry* oldRegistry;
};

TEST_F(RegistryTest, OpenKeyRead_ReturnsKey)
{
    HKEY fakeKey = reinterpret_cast<HKEY>(0x1234);
    EXPECT_CALL(mockRegistry, OpenKeyRead(HKEY_CURRENT_USER, testing::_, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<2>(fakeKey),
            testing::Return(RegistryResult::Ok())));

    HKEY result;
    auto res = gRegistry->OpenKeyRead(HKEY_CURRENT_USER, L"Software\\Test", result);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(result, fakeKey);
}

TEST_F(RegistryTest, GetString_ReturnsValue)
{
    HKEY fakeKey = reinterpret_cast<HKEY>(0x1234);
    EXPECT_CALL(mockRegistry, GetString(fakeKey, testing::StrEq(L"TestValue"), testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<2>(L"Hello World"),
            testing::Return(RegistryResult::Ok())));

    std::wstring value;
    auto res = gRegistry->GetString(fakeKey, L"TestValue", value);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(value, L"Hello World");
}

TEST_F(RegistryTest, GetDWord_ReturnsValue)
{
    HKEY fakeKey = reinterpret_cast<HKEY>(0x1234);
    EXPECT_CALL(mockRegistry, GetDWord(fakeKey, testing::StrEq(L"Counter"), testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<2>(42),
            testing::Return(RegistryResult::Ok())));

    DWORD value;
    auto res = gRegistry->GetDWord(fakeKey, L"Counter", value);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(value, 42u);
}

TEST_F(RegistryTest, KeyNotFound_ReturnsError)
{
    EXPECT_CALL(mockRegistry, OpenKeyRead(testing::_, testing::_, testing::_))
        .WillOnce(testing::Return(RegistryResult::Error(ERROR_FILE_NOT_FOUND)));

    HKEY result;
    auto res = gRegistry->OpenKeyRead(HKEY_CURRENT_USER, L"NonExistent\\Key", result);
    EXPECT_FALSE(res.success);
    EXPECT_TRUE(res.notFound());
}

TEST_F(RegistryTest, SetString_Succeeds)
{
    HKEY fakeKey = reinterpret_cast<HKEY>(0x1234);
    EXPECT_CALL(mockRegistry, SetString(fakeKey, testing::StrEq(L"Name"), testing::StrEq(L"Value")))
        .WillOnce(testing::Return(RegistryResult::Ok()));

    auto res = gRegistry->SetString(fakeKey, L"Name", L"Value");
    EXPECT_TRUE(res.success);
}

TEST_F(RegistryTest, AnsiHelper_OpenKeyReadA)
{
    HKEY fakeKey = reinterpret_cast<HKEY>(0x5678);
    EXPECT_CALL(mockRegistry, OpenKeyRead(HKEY_LOCAL_MACHINE, testing::_, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<2>(fakeKey),
            testing::Return(RegistryResult::Ok())));

    HKEY result;
    auto res = OpenKeyReadA(gRegistry, HKEY_LOCAL_MACHINE, "Software\\Test", result);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(result, fakeKey);
}

TEST_F(RegistryTest, AnsiHelper_OpenKeyReadWriteA)
{
    HKEY fakeKey = reinterpret_cast<HKEY>(0x9ABC);
    EXPECT_CALL(mockRegistry, OpenKeyReadWrite(HKEY_CURRENT_USER, testing::_, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<2>(fakeKey),
            testing::Return(RegistryResult::Ok())));

    HKEY result;
    auto res = OpenKeyReadWriteA(gRegistry, HKEY_CURRENT_USER, "Software\\TestRW", result);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(result, fakeKey);
}

TEST_F(RegistryTest, AnsiHelper_DeleteValueA)
{
    HKEY fakeKey = reinterpret_cast<HKEY>(0x1111);
    EXPECT_CALL(mockRegistry, DeleteValue(fakeKey, testing::StrEq(L"AutoImportConfig")))
        .WillOnce(testing::Return(RegistryResult::Ok()));

    auto res = DeleteValueA(gRegistry, fakeKey, "AutoImportConfig");
    EXPECT_TRUE(res.success);
}

TEST(RegistryResultTest, NotFound_Works)
{
    auto res = RegistryResult::Error(ERROR_FILE_NOT_FOUND);
    EXPECT_FALSE(res.success);
    EXPECT_TRUE(res.notFound());

    auto res2 = RegistryResult::Error(ERROR_ACCESS_DENIED);
    EXPECT_FALSE(res2.notFound());
}
