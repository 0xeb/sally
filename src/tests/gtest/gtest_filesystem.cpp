// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include <gtest/gtest.h>
#include "../../common/IFileSystem.h"
#include <string>
#include <vector>

// Define the global pointer (normally defined in Win32FileSystem.cpp)
IFileSystem* gFileSystem = nullptr;

// Mock implementation for testing
class MockFileSystem : public IFileSystem
{
public:
    struct Call
    {
        std::wstring op;
        std::wstring path;
        std::wstring path2;
    };

    std::vector<Call> calls;
    bool existsResult = true;
    FileResult opResult = FileResult::Ok();
    DWORD attributesResult = FILE_ATTRIBUTE_NORMAL;

    bool FileExists(const wchar_t* path) override
    {
        calls.push_back({L"FileExists", path ? path : L"", L""});
        return existsResult;
    }

    bool DirectoryExists(const wchar_t* path) override
    {
        calls.push_back({L"DirectoryExists", path ? path : L"", L""});
        return existsResult;
    }

    FileResult GetFileInfo(const wchar_t* path, FileInfo& info) override
    {
        calls.push_back({L"GetFileInfo", path ? path : L"", L""});
        info.name = path ? path : L"";
        info.size = 1234;
        info.attributes = FILE_ATTRIBUTE_NORMAL;
        info.isDirectory = false;
        return opResult;
    }

    DWORD GetFileAttributes(const wchar_t* path) override
    {
        calls.push_back({L"GetFileAttributes", path ? path : L"", L""});
        return attributesResult;
    }

    FileResult SetFileAttributes(const wchar_t* path, DWORD attributes) override
    {
        calls.push_back({L"SetFileAttributes", path ? path : L"", L""});
        return opResult;
    }

    FileResult DeleteFile(const wchar_t* path) override
    {
        calls.push_back({L"DeleteFile", path ? path : L"", L""});
        return opResult;
    }

    FileResult MoveFile(const wchar_t* source, const wchar_t* target) override
    {
        calls.push_back({L"MoveFile", source ? source : L"", target ? target : L""});
        return opResult;
    }

    FileResult CopyFile(const wchar_t* source, const wchar_t* target, bool failIfExists) override
    {
        calls.push_back({L"CopyFile", source ? source : L"", target ? target : L""});
        return opResult;
    }

    FileResult CreateDirectory(const wchar_t* path) override
    {
        calls.push_back({L"CreateDirectory", path ? path : L"", L""});
        return opResult;
    }

    FileResult RemoveDirectory(const wchar_t* path) override
    {
        calls.push_back({L"RemoveDirectory", path ? path : L"", L""});
        return opResult;
    }

    HANDLE OpenFileForRead(const wchar_t* path, DWORD shareMode) override
    {
        calls.push_back({L"OpenFileForRead", path ? path : L"", L""});
        return INVALID_HANDLE_VALUE; // Mock returns invalid
    }

    HANDLE CreateFileForWrite(const wchar_t* path, bool failIfExists) override
    {
        calls.push_back({L"CreateFileForWrite", path ? path : L"", L""});
        return INVALID_HANDLE_VALUE; // Mock returns invalid
    }

    void CloseHandle(HANDLE h) override
    {
        calls.push_back({L"CloseHandle", L"", L""});
    }
};

TEST(FileSystemMockTest, RecordsOperations)
{
    MockFileSystem mock;
    gFileSystem = &mock;

    gFileSystem->FileExists(L"C:\\test.txt");
    gFileSystem->DirectoryExists(L"C:\\dir");
    gFileSystem->DeleteFile(L"C:\\delete.txt");
    gFileSystem->MoveFile(L"C:\\src.txt", L"C:\\dst.txt");
    gFileSystem->CopyFile(L"C:\\a.txt", L"C:\\b.txt", false);
    gFileSystem->CreateDirectory(L"C:\\newdir");
    gFileSystem->RemoveDirectory(L"C:\\olddir");

    ASSERT_EQ(mock.calls.size(), 7u);
    EXPECT_EQ(mock.calls[0].op, L"FileExists");
    EXPECT_EQ(mock.calls[0].path, L"C:\\test.txt");
    EXPECT_EQ(mock.calls[1].op, L"DirectoryExists");
    EXPECT_EQ(mock.calls[2].op, L"DeleteFile");
    EXPECT_EQ(mock.calls[3].op, L"MoveFile");
    EXPECT_EQ(mock.calls[3].path2, L"C:\\dst.txt");
    EXPECT_EQ(mock.calls[4].op, L"CopyFile");
    EXPECT_EQ(mock.calls[5].op, L"CreateDirectory");
    EXPECT_EQ(mock.calls[6].op, L"RemoveDirectory");
}

TEST(FileSystemMockTest, ReturnsConfiguredResults)
{
    MockFileSystem mock;
    mock.existsResult = false;
    mock.opResult = FileResult::Error(ERROR_ACCESS_DENIED);

    EXPECT_FALSE(mock.FileExists(L"C:\\file.txt"));
    EXPECT_FALSE(mock.DirectoryExists(L"C:\\dir"));

    auto result = mock.DeleteFile(L"C:\\file.txt");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, (DWORD)ERROR_ACCESS_DENIED);
}

TEST(FileSystemMockTest, GetFileInfoPopulatesStruct)
{
    MockFileSystem mock;
    FileInfo info;

    auto result = mock.GetFileInfo(L"C:\\test.txt", info);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(info.name, L"C:\\test.txt");
    EXPECT_EQ(info.size, 1234u);
    EXPECT_FALSE(info.isDirectory);
}

TEST(FileSystemMockTest, FileAttributes)
{
    MockFileSystem mock;
    mock.attributesResult = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN;

    DWORD attrs = mock.GetFileAttributes(L"C:\\test.txt");
    EXPECT_EQ(attrs, (DWORD)(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN));

    auto result = mock.SetFileAttributes(L"C:\\test.txt", FILE_ATTRIBUTE_NORMAL);
    EXPECT_TRUE(result.success);

    ASSERT_EQ(mock.calls.size(), 2u);
    EXPECT_EQ(mock.calls[0].op, L"GetFileAttributes");
    EXPECT_EQ(mock.calls[1].op, L"SetFileAttributes");
}

// Test that we can swap implementations at runtime
TEST(FileSystemMockTest, RuntimeSwap)
{
    MockFileSystem mock1;
    MockFileSystem mock2;

    gFileSystem = &mock1;
    gFileSystem->FileExists(L"test1");

    gFileSystem = &mock2;
    gFileSystem->FileExists(L"test2");

    EXPECT_EQ(mock1.calls.size(), 1u);
    EXPECT_EQ(mock2.calls.size(), 1u);
    EXPECT_EQ(mock1.calls[0].path, L"test1");
    EXPECT_EQ(mock2.calls[0].path, L"test2");
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
