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
    HANDLE handleResult = INVALID_HANDLE_VALUE;
    HANDLE findHandleResult = INVALID_HANDLE_VALUE;

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

    HANDLE CreateFile(const wchar_t* path,
                      DWORD desiredAccess,
                      DWORD shareMode,
                      LPSECURITY_ATTRIBUTES securityAttributes,
                      DWORD creationDisposition,
                      DWORD flagsAndAttributes,
                      HANDLE templateFile) override
    {
        calls.push_back({L"CreateFile", path ? path : L"", L""});
        return handleResult;
    }

    HANDLE FindFirstFile(const wchar_t* path, WIN32_FIND_DATAW* findData) override
    {
        (void)findData;
        calls.push_back({L"FindFirstFile", path ? path : L"", L""});
        return findHandleResult;
    }

    BOOL FindNextFile(HANDLE findHandle, WIN32_FIND_DATAW* findData) override
    {
        (void)findHandle;
        (void)findData;
        calls.push_back({L"FindNextFile", L"", L""});
        return FALSE;
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

TEST(FileSystemMockTest, CreateFileDelegatesToImplementation)
{
    MockFileSystem mock;
    mock.handleResult = reinterpret_cast<HANDLE>(0x1234);

    HANDLE h = mock.CreateFile(L"C:\\test.bin", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    EXPECT_EQ(h, reinterpret_cast<HANDLE>(0x1234));

    ASSERT_EQ(mock.calls.size(), 1u);
    EXPECT_EQ(mock.calls[0].op, L"CreateFile");
    EXPECT_EQ(mock.calls[0].path, L"C:\\test.bin");
}

TEST(FileSystemMockTest, FindOperationsDelegatesToImplementation)
{
    MockFileSystem mock;
    mock.findHandleResult = reinterpret_cast<HANDLE>(0x5678);
    WIN32_FIND_DATAW fd = {};

    HANDLE h = mock.FindFirstFile(L"C:\\*.txt", &fd);
    EXPECT_EQ(h, reinterpret_cast<HANDLE>(0x5678));

    EXPECT_FALSE(mock.FindNextFile(h, &fd));

    ASSERT_EQ(mock.calls.size(), 2u);
    EXPECT_EQ(mock.calls[0].op, L"FindFirstFile");
    EXPECT_EQ(mock.calls[0].path, L"C:\\*.txt");
    EXPECT_EQ(mock.calls[1].op, L"FindNextFile");
}

TEST(FileSystemAnsiHelperTest, FindFirstFilePathAConvertsAndDelegates)
{
    MockFileSystem mock;
    mock.findHandleResult = reinterpret_cast<HANDLE>(0x9ABC);
    WIN32_FIND_DATAW fd = {};

    HANDLE h = FindFirstFilePathA(&mock, "C:\\Temp\\*.txt", &fd);

    EXPECT_EQ(h, reinterpret_cast<HANDLE>(0x9ABC));
    ASSERT_EQ(mock.calls.size(), 1u);
    EXPECT_EQ(mock.calls[0].op, L"FindFirstFile");
    EXPECT_EQ(mock.calls[0].path, L"C:\\Temp\\*.txt");
}

TEST(FileSystemAnsiHelperTest, FindFirstFilePathAPropagatesInvalidHandle)
{
    MockFileSystem mock;
    mock.findHandleResult = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW fd = {};

    HANDLE h = FindFirstFilePathA(&mock, "C:\\missing\\*.txt", &fd);

    EXPECT_EQ(h, INVALID_HANDLE_VALUE);
    ASSERT_EQ(mock.calls.size(), 1u);
    EXPECT_EQ(mock.calls[0].op, L"FindFirstFile");
    EXPECT_EQ(mock.calls[0].path, L"C:\\missing\\*.txt");
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
