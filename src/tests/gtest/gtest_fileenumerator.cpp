// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "../../common/IFileEnumerator.h"
#include <vector>

// For test build: provide gFileEnumerator definition
IFileEnumerator* gFileEnumerator = nullptr;

// Mock implementation for testing
class MockFileEnumerator : public IFileEnumerator
{
public:
    MOCK_METHOD(HENUM, StartEnum, (const wchar_t* path, const wchar_t* pattern), (override));
    MOCK_METHOD(EnumResult, NextFile, (HENUM handle, FileEnumEntry& entry), (override));
    MOCK_METHOD(void, EndEnum, (HENUM handle), (override));
};

class FileEnumeratorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        oldEnumerator = gFileEnumerator;
        gFileEnumerator = &mockEnumerator;
    }

    void TearDown() override
    {
        gFileEnumerator = oldEnumerator;
    }

    MockFileEnumerator mockEnumerator;
    IFileEnumerator* oldEnumerator;
};

TEST_F(FileEnumeratorTest, StartEnum_ReturnsHandle)
{
    HENUM fakeHandle = reinterpret_cast<HENUM>(0x1234);
    EXPECT_CALL(mockEnumerator, StartEnum(testing::StrEq(L"C:\\test"), nullptr))
        .WillOnce(testing::Return(fakeHandle));

    HENUM handle = gFileEnumerator->StartEnum(L"C:\\test", nullptr);
    EXPECT_EQ(handle, fakeHandle);
}

TEST_F(FileEnumeratorTest, NextFile_ReturnsEntry)
{
    HENUM fakeHandle = reinterpret_cast<HENUM>(0x1234);
    FileEnumEntry expectedEntry;
    expectedEntry.name = L"test.txt";
    expectedEntry.size = 1024;
    expectedEntry.attributes = FILE_ATTRIBUTE_NORMAL;

    EXPECT_CALL(mockEnumerator, NextFile(fakeHandle, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<1>(expectedEntry),
            testing::Return(EnumResult::Ok())));

    FileEnumEntry entry;
    auto result = gFileEnumerator->NextFile(fakeHandle, entry);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.noMoreFiles);
    EXPECT_EQ(entry.name, L"test.txt");
    EXPECT_EQ(entry.size, 1024u);
}

TEST_F(FileEnumeratorTest, NextFile_ReturnsDoneWhenComplete)
{
    HENUM fakeHandle = reinterpret_cast<HENUM>(0x1234);

    EXPECT_CALL(mockEnumerator, NextFile(fakeHandle, testing::_))
        .WillOnce(testing::Return(EnumResult::Done()));

    FileEnumEntry entry;
    auto result = gFileEnumerator->NextFile(fakeHandle, entry);
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.noMoreFiles);
}

TEST_F(FileEnumeratorTest, EndEnum_ClosesHandle)
{
    HENUM fakeHandle = reinterpret_cast<HENUM>(0x1234);

    EXPECT_CALL(mockEnumerator, EndEnum(fakeHandle))
        .Times(1);

    gFileEnumerator->EndEnum(fakeHandle);
}

TEST_F(FileEnumeratorTest, AnsiHelper_StartEnumA)
{
    HENUM fakeHandle = reinterpret_cast<HENUM>(0x5678);

    EXPECT_CALL(mockEnumerator, StartEnum(testing::_, testing::_))
        .WillOnce(testing::Return(fakeHandle));

    HENUM handle = StartEnumA(gFileEnumerator, "C:\\test", "*.txt");
    EXPECT_EQ(handle, fakeHandle);
}

TEST(FileEnumEntryTest, IsDirectory_Works)
{
    FileEnumEntry entry;
    entry.attributes = FILE_ATTRIBUTE_DIRECTORY;
    EXPECT_TRUE(entry.IsDirectory());

    entry.attributes = FILE_ATTRIBUTE_NORMAL;
    EXPECT_FALSE(entry.IsDirectory());
}

TEST(FileEnumEntryTest, IsHidden_Works)
{
    FileEnumEntry entry;
    entry.attributes = FILE_ATTRIBUTE_HIDDEN;
    EXPECT_TRUE(entry.IsHidden());

    entry.attributes = FILE_ATTRIBUTE_NORMAL;
    EXPECT_FALSE(entry.IsHidden());
}

TEST(EnumResultTest, States_Work)
{
    auto ok = EnumResult::Ok();
    EXPECT_TRUE(ok.success);
    EXPECT_FALSE(ok.noMoreFiles);

    auto done = EnumResult::Done();
    EXPECT_TRUE(done.success);
    EXPECT_TRUE(done.noMoreFiles);

    auto error = EnumResult::Error(ERROR_ACCESS_DENIED);
    EXPECT_FALSE(error.success);
    EXPECT_FALSE(error.noMoreFiles);
    EXPECT_EQ(error.errorCode, (DWORD)ERROR_ACCESS_DENIED);
}

TEST(HasPatternTest, DetectsWildcards)
{
    EXPECT_TRUE(IFileEnumerator::HasPattern(L"*.txt"));
    EXPECT_TRUE(IFileEnumerator::HasPattern(L"test?.doc"));
    EXPECT_TRUE(IFileEnumerator::HasPattern(L"C:\\dir\\*"));
    EXPECT_FALSE(IFileEnumerator::HasPattern(L"C:\\test.txt"));
    EXPECT_FALSE(IFileEnumerator::HasPattern(L""));
    EXPECT_FALSE(IFileEnumerator::HasPattern(nullptr));
}
