// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include <gtest/gtest.h>

#include "../../common/IFileSystem.h"
#include "../../common/widepath.h"

#include <string>

namespace
{
class MockBridgeFileSystem : public IFileSystem
{
public:
    std::wstring lastOp;
    std::wstring lastPath;
    std::wstring lastPath2;

    DWORD attrsResult = INVALID_FILE_ATTRIBUTES;
    FileResult deleteResult = FileResult::Error(ERROR_FILE_NOT_FOUND);
    FileResult moveResult = FileResult::Error(ERROR_FILE_NOT_FOUND);
    FileResult copyResult = FileResult::Error(ERROR_FILE_NOT_FOUND);
    FileResult createDirResult = FileResult::Error(ERROR_PATH_NOT_FOUND);
    FileResult removeDirResult = FileResult::Error(ERROR_PATH_NOT_FOUND);
    FileResult setAttrsResult = FileResult::Error(ERROR_FILE_NOT_FOUND);
    HANDLE createFileResult = INVALID_HANDLE_VALUE;
    HANDLE findHandleResult = INVALID_HANDLE_VALUE;
    bool findNextResult = false;
    WIN32_FIND_DATAW findDataResult = {};
    DWORD createFileDesiredAccess = 0;
    DWORD createFileShareMode = 0;
    DWORD createFileDisposition = 0;
    DWORD createFileFlags = 0;

    bool FileExists(const wchar_t* path) override
    {
        lastOp = L"FileExists";
        lastPath = path ? path : L"";
        return false;
    }

    bool DirectoryExists(const wchar_t* path) override
    {
        lastOp = L"DirectoryExists";
        lastPath = path ? path : L"";
        return false;
    }

    FileResult GetFileInfo(const wchar_t* path, FileInfo& info) override
    {
        lastOp = L"GetFileInfo";
        lastPath = path ? path : L"";
        info = FileInfo{};
        return FileResult::Error(ERROR_NOT_SUPPORTED);
    }

    DWORD GetFileAttributes(const wchar_t* path) override
    {
        lastOp = L"GetFileAttributes";
        lastPath = path ? path : L"";
        return attrsResult;
    }

    FileResult SetFileAttributes(const wchar_t* path, DWORD attributes) override
    {
        (void)attributes;
        lastOp = L"SetFileAttributes";
        lastPath = path ? path : L"";
        return setAttrsResult;
    }

    FileResult DeleteFile(const wchar_t* path) override
    {
        lastOp = L"DeleteFile";
        lastPath = path ? path : L"";
        return deleteResult;
    }

    FileResult MoveFile(const wchar_t* source, const wchar_t* target) override
    {
        lastOp = L"MoveFile";
        lastPath = source ? source : L"";
        lastPath2 = target ? target : L"";
        return moveResult;
    }

    FileResult CopyFile(const wchar_t* source, const wchar_t* target, bool failIfExists) override
    {
        (void)failIfExists;
        lastOp = L"CopyFile";
        lastPath = source ? source : L"";
        lastPath2 = target ? target : L"";
        return copyResult;
    }

    FileResult CreateDirectory(const wchar_t* path) override
    {
        lastOp = L"CreateDirectory";
        lastPath = path ? path : L"";
        return createDirResult;
    }

    FileResult RemoveDirectory(const wchar_t* path) override
    {
        lastOp = L"RemoveDirectory";
        lastPath = path ? path : L"";
        return removeDirResult;
    }

    HANDLE CreateFile(const wchar_t* path,
                      DWORD desiredAccess,
                      DWORD shareMode,
                      LPSECURITY_ATTRIBUTES securityAttributes,
                      DWORD creationDisposition,
                      DWORD flagsAndAttributes,
                      HANDLE templateFile) override
    {
        (void)securityAttributes;
        (void)templateFile;
        lastOp = L"CreateFile";
        lastPath = path ? path : L"";
        createFileDesiredAccess = desiredAccess;
        createFileShareMode = shareMode;
        createFileDisposition = creationDisposition;
        createFileFlags = flagsAndAttributes;
        return createFileResult;
    }

    HANDLE FindFirstFile(const wchar_t* path, WIN32_FIND_DATAW* findData) override
    {
        lastOp = L"FindFirstFile";
        lastPath = path ? path : L"";
        if (findData != nullptr)
            *findData = findDataResult;
        return findHandleResult;
    }

    BOOL FindNextFile(HANDLE findHandle, WIN32_FIND_DATAW* findData) override
    {
        (void)findHandle;
        lastOp = L"FindNextFile";
        if (findData != nullptr)
            *findData = findDataResult;
        return findNextResult ? TRUE : FALSE;
    }

    HANDLE OpenFileForRead(const wchar_t* path, DWORD shareMode) override
    {
        (void)shareMode;
        lastOp = L"OpenFileForRead";
        lastPath = path ? path : L"";
        return INVALID_HANDLE_VALUE;
    }

    HANDLE CreateFileForWrite(const wchar_t* path, bool failIfExists) override
    {
        (void)failIfExists;
        lastOp = L"CreateFileForWrite";
        lastPath = path ? path : L"";
        return INVALID_HANDLE_VALUE;
    }

    void CloseHandle(HANDLE h) override
    {
        (void)h;
        lastOp = L"CloseHandle";
    }
};

}

class WidePathFileSystemBridgeTest : public ::testing::Test
{
protected:
    IFileSystem* oldFileSystem = nullptr;
    MockBridgeFileSystem mockFileSystem;

    void SetUp() override
    {
        oldFileSystem = gFileSystem;
        gFileSystem = &mockFileSystem;
        SetLastError(ERROR_SUCCESS);
    }

    void TearDown() override
    {
        gFileSystem = oldFileSystem;
    }
};

TEST_F(WidePathFileSystemBridgeTest, DeleteFileUsesIFileSystem)
{
    mockFileSystem.deleteResult = FileResult::Error(ERROR_ACCESS_DENIED);

    EXPECT_FALSE(SalLPDeleteFile("C:\\temp\\forbidden.txt"));
    EXPECT_EQ(GetLastError(), (DWORD)ERROR_ACCESS_DENIED);
    EXPECT_EQ(mockFileSystem.lastOp, L"DeleteFile");
    EXPECT_EQ(mockFileSystem.lastPath, L"C:\\temp\\forbidden.txt");
}

TEST_F(WidePathFileSystemBridgeTest, MoveFileUsesIFileSystem)
{
    mockFileSystem.moveResult = FileResult::Ok();

    EXPECT_TRUE(SalLPMoveFile("C:\\temp\\a.txt", "C:\\temp\\b.txt"));
    EXPECT_EQ(mockFileSystem.lastOp, L"MoveFile");
    EXPECT_EQ(mockFileSystem.lastPath, L"C:\\temp\\a.txt");
    EXPECT_EQ(mockFileSystem.lastPath2, L"C:\\temp\\b.txt");
}

TEST_F(WidePathFileSystemBridgeTest, GetFileAttributesUsesIFileSystem)
{
    mockFileSystem.attrsResult = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE;

    DWORD attrs = SalLPGetFileAttributes("C:\\temp\\x.txt");
    EXPECT_EQ(attrs, (DWORD)(FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE));
    EXPECT_EQ(mockFileSystem.lastOp, L"GetFileAttributes");
    EXPECT_EQ(mockFileSystem.lastPath, L"C:\\temp\\x.txt");
}

TEST_F(WidePathFileSystemBridgeTest, CreateFileUsesIFileSystem)
{
    HANDLE expected = reinterpret_cast<HANDLE>(0x1234);
    mockFileSystem.createFileResult = expected;

    HANDLE h = SalLPCreateFile("C:\\temp\\in.bin", GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);

    EXPECT_EQ(h, expected);
    EXPECT_EQ(mockFileSystem.lastOp, L"CreateFile");
    EXPECT_EQ(mockFileSystem.lastPath, L"C:\\temp\\in.bin");
    EXPECT_EQ(mockFileSystem.createFileDesiredAccess, (DWORD)GENERIC_READ);
    EXPECT_EQ(mockFileSystem.createFileShareMode, (DWORD)FILE_SHARE_READ);
    EXPECT_EQ(mockFileSystem.createFileDisposition, (DWORD)OPEN_EXISTING);
    EXPECT_EQ(mockFileSystem.createFileFlags, (DWORD)FILE_FLAG_SEQUENTIAL_SCAN);
}

TEST_F(WidePathFileSystemBridgeTest, CreateDirectoryUsesIFileSystemWhenSecurityNull)
{
    mockFileSystem.createDirResult = FileResult::Ok();

    EXPECT_TRUE(SalLPCreateDirectory("C:\\temp\\newdir", NULL));
    EXPECT_EQ(mockFileSystem.lastOp, L"CreateDirectory");
    EXPECT_EQ(mockFileSystem.lastPath, L"C:\\temp\\newdir");
}

TEST_F(WidePathFileSystemBridgeTest, FindFirstFileUsesIFileSystem)
{
    mockFileSystem.findHandleResult = reinterpret_cast<HANDLE>(0x8888);
    wcscpy_s(mockFileSystem.findDataResult.cFileName, L"alpha.txt");
    WIN32_FIND_DATAW fd = {};

    HANDLE h = SalLPFindFirstFile("C:\\temp\\*.txt", &fd);

    EXPECT_EQ(h, reinterpret_cast<HANDLE>(0x8888));
    EXPECT_EQ(mockFileSystem.lastOp, L"FindFirstFile");
    EXPECT_EQ(mockFileSystem.lastPath, L"C:\\temp\\*.txt");
    EXPECT_STREQ(fd.cFileName, L"alpha.txt");
}

TEST_F(WidePathFileSystemBridgeTest, FindNextFileAUsesIFileSystem)
{
    mockFileSystem.findNextResult = true;
    wcscpy_s(mockFileSystem.findDataResult.cFileName, L"beta.txt");
    WIN32_FIND_DATAA fd = {};

    EXPECT_TRUE(SalLPFindNextFileA(reinterpret_cast<HANDLE>(0x1111), &fd));
    EXPECT_EQ(mockFileSystem.lastOp, L"FindNextFile");
    EXPECT_STREQ(fd.cFileName, "beta.txt");
}
