// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Google Test suite for filesystem operations:
//   SalLPCreateDirectory, SalLPRemoveDirectory, PathExistsW, IsDirectoryW
//
//****************************************************************************

#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <sstream>

#include "../common/widepath.h"
#include "../../common/fsutil.h"

// ============================================================================
// Test fixture: creates a unique temp directory per test
// ============================================================================

class FilesystemOpsTest : public ::testing::Test
{
protected:
    std::string m_tempDir;
    std::wstring m_tempDirW;

    void SetUp() override
    {
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        char tempFile[MAX_PATH];
        GetTempFileNameA(tempPath, "fso", 0, tempFile);
        // GetTempFileName creates a file; delete it and create as directory
        DeleteFileA(tempFile);
        CreateDirectoryA(tempFile, NULL);
        m_tempDir = tempFile;

        // Build wide version
        wchar_t wideBuf[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, m_tempDir.c_str(), -1, wideBuf, MAX_PATH);
        m_tempDirW = wideBuf;
    }

    void TearDown() override
    {
        // Recursively clean up the temp directory
        RecursiveDelete(m_tempDirW);
    }

    // Recursively delete a directory and its contents
    void RecursiveDelete(const std::wstring& dir)
    {
        WIN32_FIND_DATAW fd;
        std::wstring pattern = dir + L"\\*";
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                    continue;
                std::wstring full = dir + L"\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    RecursiveDelete(full);
                else
                    DeleteFileW(full.c_str());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryW(dir.c_str());
    }

    // Create a test file using wide API
    void CreateTestFileW(const std::wstring& path)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ASSERT_NE(h, INVALID_HANDLE_VALUE) << "Failed to create test file";
        const char* data = "test";
        DWORD written;
        WriteFile(h, data, 4, &written, NULL);
        CloseHandle(h);
    }

    // Create a test file from ANSI path (converts to wide for long-path support)
    void CreateTestFile(const std::string& path)
    {
        int wlen = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, NULL, 0);
        std::wstring wPath(wlen - 1, L'\0');
        MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &wPath[0], wlen);
        // Use \\?\ prefix for long path support
        if (wPath.length() >= MAX_PATH && wPath.substr(0, 4) != L"\\\\?\\")
            wPath = L"\\\\?\\" + wPath;
        CreateTestFileW(wPath);
    }
};

// ============================================================================
// SalLPCreateDirectory tests
// ============================================================================

TEST_F(FilesystemOpsTest, CreateDirectory_NewDir)
{
    std::string subdir = m_tempDir + "\\newdir";
    EXPECT_TRUE(SalLPCreateDirectory(subdir.c_str(), NULL));

    // Verify it exists using GetFileAttributes
    DWORD attrs = SalLPGetFileAttributes(subdir.c_str());
    EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

TEST_F(FilesystemOpsTest, CreateDirectory_AlreadyExists)
{
    std::string subdir = m_tempDir + "\\existing";
    EXPECT_TRUE(SalLPCreateDirectory(subdir.c_str(), NULL));
    // Second call should fail (directory already exists)
    EXPECT_FALSE(SalLPCreateDirectory(subdir.c_str(), NULL));
}

// ============================================================================
// SalLPRemoveDirectory tests
// ============================================================================

TEST_F(FilesystemOpsTest, RemoveDirectory_EmptyDir)
{
    std::string subdir = m_tempDir + "\\toremove";
    ASSERT_TRUE(SalLPCreateDirectory(subdir.c_str(), NULL));
    EXPECT_TRUE(SalLPRemoveDirectory(subdir.c_str()));

    // Verify it no longer exists
    DWORD attrs = SalLPGetFileAttributes(subdir.c_str());
    EXPECT_EQ(attrs, INVALID_FILE_ATTRIBUTES);
}

TEST_F(FilesystemOpsTest, RemoveDirectory_NonExistent)
{
    std::string subdir = m_tempDir + "\\nonexistent";
    EXPECT_FALSE(SalLPRemoveDirectory(subdir.c_str()));
}

// ============================================================================
// PathExistsW tests
// ============================================================================

TEST_F(FilesystemOpsTest, PathExistsW_ExistingFile)
{
    std::wstring filePath = m_tempDirW + L"\\testfile.txt";
    CreateTestFileW(filePath);
    EXPECT_TRUE(PathExistsW(filePath.c_str()));
}

TEST_F(FilesystemOpsTest, PathExistsW_ExistingDirectory)
{
    std::wstring subdir = m_tempDirW + L"\\existdir";
    CreateDirectoryW(subdir.c_str(), NULL);
    EXPECT_TRUE(PathExistsW(subdir.c_str()));
}

TEST_F(FilesystemOpsTest, PathExistsW_NonExistent)
{
    std::wstring bogus = m_tempDirW + L"\\does_not_exist.xyz";
    EXPECT_FALSE(PathExistsW(bogus.c_str()));
}

// ============================================================================
// IsDirectoryW tests
// ============================================================================

TEST_F(FilesystemOpsTest, IsDirectoryW_Directory)
{
    std::wstring subdir = m_tempDirW + L"\\adir";
    CreateDirectoryW(subdir.c_str(), NULL);
    EXPECT_TRUE(IsDirectoryW(subdir.c_str()));
}

TEST_F(FilesystemOpsTest, IsDirectoryW_File)
{
    std::wstring filePath = m_tempDirW + L"\\afile.txt";
    CreateTestFileW(filePath);
    EXPECT_FALSE(IsDirectoryW(filePath.c_str()));
}

TEST_F(FilesystemOpsTest, IsDirectoryW_NonExistent)
{
    std::wstring bogus = m_tempDirW + L"\\nope";
    EXPECT_FALSE(IsDirectoryW(bogus.c_str()));
}

// ============================================================================
// Long path tests (>260 chars)
// ============================================================================

TEST_F(FilesystemOpsTest, LongPath_CreateAndRemoveDirectory)
{
    // Build a deeply nested path exceeding MAX_PATH (260 chars)
    std::string longDir = m_tempDir;
    int i = 0;
    while (longDir.size() < 300)
    {
        std::ostringstream oss;
        oss << "\\d" << i++;
        longDir += oss.str();
    }

    // Create directories along the path using SalLPCreateDirectory
    // We must create each intermediate directory
    std::string current = m_tempDir;
    size_t pos = m_tempDir.size();
    while (pos < longDir.size())
    {
        pos = longDir.find('\\', pos + 1);
        if (pos == std::string::npos)
            pos = longDir.size();
        current = longDir.substr(0, pos);
        ASSERT_TRUE(SalLPCreateDirectory(current.c_str(), NULL))
            << "Failed to create: " << current << " (len=" << current.size() << ")";
    }

    // Verify leaf exists
    DWORD attrs = SalLPGetFileAttributes(longDir.c_str());
    EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_DIRECTORY);

    // Remove the leaf
    EXPECT_TRUE(SalLPRemoveDirectory(longDir.c_str()));
    EXPECT_EQ(SalLPGetFileAttributes(longDir.c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(FilesystemOpsTest, LongPath_PathExistsW_And_IsDirectoryW)
{
    // Build a deeply nested wide path exceeding MAX_PATH
    std::wstring longDir = m_tempDirW;
    int i = 0;
    while (longDir.size() < 300)
    {
        std::wostringstream oss;
        oss << L"\\d" << i++;
        longDir += oss.str();
    }

    // Create intermediate directories using wide API with \\?\ prefix
    std::wstring current = m_tempDirW;
    size_t pos = m_tempDirW.size();
    while (pos < longDir.size())
    {
        pos = longDir.find(L'\\', pos + 1);
        if (pos == std::wstring::npos)
            pos = longDir.size();
        current = longDir.substr(0, pos);

        // Use \\?\ prefix for wide API to support long paths
        std::wstring prefixed = L"\\\\?\\" + current;
        ASSERT_TRUE(CreateDirectoryW(prefixed.c_str(), NULL))
            << "Failed to create long dir (len=" << current.size() << ")";
    }

    // Test PathExistsW and IsDirectoryW on the long path
    // These use GetFileInfoW which adds long path prefix internally
    EXPECT_TRUE(PathExistsW(longDir.c_str()));
    EXPECT_TRUE(IsDirectoryW(longDir.c_str()));

    // Create a file inside the long path
    std::wstring filePath = longDir + L"\\test.txt";
    std::wstring prefixedFile = L"\\\\?\\" + filePath;
    HANDLE h = CreateFileW(prefixedFile.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    CloseHandle(h);

    EXPECT_TRUE(PathExistsW(filePath.c_str()));
    EXPECT_FALSE(IsDirectoryW(filePath.c_str()));

    // Clean up the file (TearDown handles directories)
    DeleteFileW(prefixedFile.c_str());
}

// ============================================================================
// SalLPGetFileAttributes / SalLPSetFileAttributes tests
// ============================================================================

TEST_F(FilesystemOpsTest, GetFileAttributes_Directory)
{
    std::string subdir = m_tempDir + "\\attrdir";
    ASSERT_TRUE(SalLPCreateDirectory(subdir.c_str(), NULL));
    DWORD attrs = SalLPGetFileAttributes(subdir.c_str());
    EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

TEST_F(FilesystemOpsTest, GetFileAttributes_File)
{
    std::wstring filePath = m_tempDirW + L"\\attrfile.txt";
    CreateTestFileW(filePath);
    // Convert to ANSI for SalLP call
    char ansiPath[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, filePath.c_str(), -1, ansiPath, MAX_PATH, NULL, NULL);
    DWORD attrs = SalLPGetFileAttributes(ansiPath);
    EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
    EXPECT_FALSE(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

TEST_F(FilesystemOpsTest, GetFileAttributes_NonExistent)
{
    std::string bogus = m_tempDir + "\\nonexistent_file.xyz";
    EXPECT_EQ(SalLPGetFileAttributes(bogus.c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(FilesystemOpsTest, SetFileAttributes_ReadOnly)
{
    std::wstring filePath = m_tempDirW + L"\\readonly.txt";
    CreateTestFileW(filePath);
    char ansiPath[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, filePath.c_str(), -1, ansiPath, MAX_PATH, NULL, NULL);

    // Set read-only attribute
    EXPECT_TRUE(SalLPSetFileAttributes(ansiPath, FILE_ATTRIBUTE_READONLY));
    DWORD attrs = SalLPGetFileAttributes(ansiPath);
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_READONLY);

    // Clear read-only so TearDown can delete
    EXPECT_TRUE(SalLPSetFileAttributes(ansiPath, FILE_ATTRIBUTE_NORMAL));
    attrs = SalLPGetFileAttributes(ansiPath);
    EXPECT_FALSE(attrs & FILE_ATTRIBUTE_READONLY);
}

// ============================================================================
// SalLPDeleteFile tests
// ============================================================================

TEST_F(FilesystemOpsTest, DeleteFile_ExistingFile)
{
    std::wstring filePath = m_tempDirW + L"\\todelete.txt";
    CreateTestFileW(filePath);
    char ansiPath[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, filePath.c_str(), -1, ansiPath, MAX_PATH, NULL, NULL);

    EXPECT_TRUE(SalLPDeleteFile(ansiPath));
    EXPECT_EQ(SalLPGetFileAttributes(ansiPath), INVALID_FILE_ATTRIBUTES);
}

TEST_F(FilesystemOpsTest, DeleteFile_NonExistent)
{
    std::string bogus = m_tempDir + "\\no_such_file.txt";
    EXPECT_FALSE(SalLPDeleteFile(bogus.c_str()));
}

// ============================================================================
// SalLPCopyFile tests
// ============================================================================

TEST_F(FilesystemOpsTest, CopyFile_Basic)
{
    std::wstring srcPath = m_tempDirW + L"\\source.txt";
    CreateTestFileW(srcPath);
    char ansiSrc[MAX_PATH], ansiDst[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, srcPath.c_str(), -1, ansiSrc, MAX_PATH, NULL, NULL);

    std::string dstPath = m_tempDir + "\\copy.txt";
    strcpy(ansiDst, dstPath.c_str());

    EXPECT_TRUE(SalLPCopyFile(ansiSrc, ansiDst, TRUE));
    EXPECT_NE(SalLPGetFileAttributes(ansiDst), INVALID_FILE_ATTRIBUTES);
}

TEST_F(FilesystemOpsTest, CopyFile_FailIfExists)
{
    std::wstring srcPath = m_tempDirW + L"\\src2.txt";
    std::wstring dstPath = m_tempDirW + L"\\dst2.txt";
    CreateTestFileW(srcPath);
    CreateTestFileW(dstPath);
    char ansiSrc[MAX_PATH], ansiDst[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, srcPath.c_str(), -1, ansiSrc, MAX_PATH, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, dstPath.c_str(), -1, ansiDst, MAX_PATH, NULL, NULL);

    // Should fail because destination exists and failIfExists=TRUE
    EXPECT_FALSE(SalLPCopyFile(ansiSrc, ansiDst, TRUE));
}

// ============================================================================
// SalLPMoveFile tests
// ============================================================================

TEST_F(FilesystemOpsTest, MoveFile_Basic)
{
    std::wstring srcPath = m_tempDirW + L"\\movesrc.txt";
    CreateTestFileW(srcPath);
    char ansiSrc[MAX_PATH], ansiDst[MAX_PATH];
    WideCharToMultiByte(CP_ACP, 0, srcPath.c_str(), -1, ansiSrc, MAX_PATH, NULL, NULL);

    std::string dstPath = m_tempDir + "\\movedst.txt";
    strcpy(ansiDst, dstPath.c_str());

    EXPECT_TRUE(SalLPMoveFile(ansiSrc, ansiDst));
    // Source should be gone
    EXPECT_EQ(SalLPGetFileAttributes(ansiSrc), INVALID_FILE_ATTRIBUTES);
    // Destination should exist
    EXPECT_NE(SalLPGetFileAttributes(ansiDst), INVALID_FILE_ATTRIBUTES);
}

// ============================================================================
// SalLPCreateFile tests
// ============================================================================

TEST_F(FilesystemOpsTest, CreateFile_NewFile)
{
    std::string filePath = m_tempDir + "\\created.txt";
    HANDLE h = SalLPCreateFile(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    EXPECT_NE(h, INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    EXPECT_NE(SalLPGetFileAttributes(filePath.c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(FilesystemOpsTest, CreateFile_OpenExisting)
{
    std::wstring wPath = m_tempDirW + L"\\existing.txt";
    CreateTestFileW(wPath);
    std::string filePath = m_tempDir + "\\existing.txt";
    HANDLE h = SalLPCreateFile(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, 0, NULL);
    EXPECT_NE(h, INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
}

// ============================================================================
// SalFindFirstFileH / SalLPFindFirstFile tests
// ============================================================================

TEST_F(FilesystemOpsTest, SalFindFirstFileH_FindsExistingFile)
{
    CreateTestFile(m_tempDir + "\\testfile.txt");
    std::string pattern = m_tempDir + "\\testfile.txt";
    WIN32_FIND_DATAA fd;
    HANDLE h = SalLPFindFirstFileA(pattern.c_str(), &fd);
    EXPECT_NE(h, INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE)
    {
        EXPECT_STREQ(fd.cFileName, "testfile.txt");
        FindClose(h);
    }
}

TEST_F(FilesystemOpsTest, SalFindFirstFileH_WildcardEnumeration)
{
    CreateTestFile(m_tempDir + "\\alpha.txt");
    CreateTestFile(m_tempDir + "\\beta.txt");
    std::string pattern = m_tempDir + "\\*.txt";
    WIN32_FIND_DATAA fd;
    HANDLE h = SalLPFindFirstFileA(pattern.c_str(), &fd);
    EXPECT_NE(h, INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE)
    {
        int count = 0;
        do
        {
            count++;
        } while (FindNextFileA(h, &fd));
        EXPECT_EQ(count, 2);
        FindClose(h);
    }
}

TEST_F(FilesystemOpsTest, SalFindFirstFileH_NonexistentReturnsInvalid)
{
    std::string pattern = m_tempDir + "\\nonexistent_file_xyz.dat";
    WIN32_FIND_DATAA fd;
    HANDLE h = SalLPFindFirstFileA(pattern.c_str(), &fd);
    EXPECT_EQ(h, INVALID_HANDLE_VALUE);
}

TEST_F(FilesystemOpsTest, SalLPFindFirstFile_WideData)
{
    CreateTestFile(m_tempDir + "\\widefile.txt");
    std::string pattern = m_tempDir + "\\widefile.txt";
    WIN32_FIND_DATAW fd;
    HANDLE h = SalLPFindFirstFile(pattern.c_str(), &fd);
    EXPECT_NE(h, INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE)
    {
        EXPECT_STREQ(fd.cFileName, L"widefile.txt");
        FindClose(h);
    }
}

TEST_F(FilesystemOpsTest, SalFindFirstFileH_LongPath)
{
    // Build a path longer than MAX_PATH
    std::string longDir = m_tempDir;
    for (int i = 0; i < 15; i++)
    {
        longDir += "\\abcdefghijklmno";
        SalLPCreateDirectory(longDir.c_str(), NULL);
    }
    EXPECT_GT(longDir.length(), (size_t)MAX_PATH);

    // Create a file inside the deeply nested directory
    std::string longFile = longDir + "\\deepfile.txt";
    CreateTestFile(longFile);

    // FindFirstFile via SalLPFindFirstFileA
    WIN32_FIND_DATAA fd;
    HANDLE h = SalLPFindFirstFileA(longFile.c_str(), &fd);
    EXPECT_NE(h, INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE)
    {
        EXPECT_STREQ(fd.cFileName, "deepfile.txt");
        FindClose(h);
    }

    // Also test wide version
    WIN32_FIND_DATAW fdw;
    HANDLE hw = SalLPFindFirstFile(longFile.c_str(), &fdw);
    EXPECT_NE(hw, INVALID_HANDLE_VALUE);
    if (hw != INVALID_HANDLE_VALUE)
    {
        EXPECT_STREQ(fdw.cFileName, L"deepfile.txt");
        FindClose(hw);
    }
}

// ============================================================================
// Long path tests for CreateFile and file attributes
// ============================================================================

TEST_F(FilesystemOpsTest, LongPath_CreateFileAndGetAttributes)
{
    // Build a path longer than MAX_PATH
    std::string longDir = m_tempDir;
    for (int i = 0; i < 15; i++)
    {
        longDir += "\\longsegment_test";
        SalLPCreateDirectory(longDir.c_str(), NULL);
    }
    EXPECT_GT(longDir.length(), (size_t)MAX_PATH);

    // Create a file via SalLPCreateFile
    std::string longFile = longDir + "\\testcreate.dat";
    HANDLE h = SalLPCreateFile(longFile.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    EXPECT_NE(h, INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE)
    {
        const char* data = "longpath";
        DWORD written;
        WriteFile(h, data, 8, &written, NULL);
        CloseHandle(h);
    }

    // Verify attributes
    DWORD attrs = SalLPGetFileAttributes(longFile.c_str());
    EXPECT_NE(attrs, (DWORD)INVALID_FILE_ATTRIBUTES);
    EXPECT_FALSE(attrs & FILE_ATTRIBUTE_DIRECTORY);

    // Set read-only and verify
    EXPECT_TRUE(SalLPSetFileAttributes(longFile.c_str(), FILE_ATTRIBUTE_READONLY));
    attrs = SalLPGetFileAttributes(longFile.c_str());
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_READONLY);

    // Clear read-only for cleanup
    SalLPSetFileAttributes(longFile.c_str(), FILE_ATTRIBUTE_NORMAL);
}

TEST_F(FilesystemOpsTest, LongPath_CopyAndMoveFile)
{
    // Build a path longer than MAX_PATH
    std::string longDir = m_tempDir;
    for (int i = 0; i < 15; i++)
    {
        longDir += "\\copymove_segment";
        SalLPCreateDirectory(longDir.c_str(), NULL);
    }
    EXPECT_GT(longDir.length(), (size_t)MAX_PATH);

    // Create source file
    std::string srcFile = longDir + "\\source.txt";
    CreateTestFile(srcFile);

    // Copy
    std::string copyFile = longDir + "\\copied.txt";
    EXPECT_TRUE(SalLPCopyFile(srcFile.c_str(), copyFile.c_str(), TRUE));
    EXPECT_NE(SalLPGetFileAttributes(copyFile.c_str()), (DWORD)INVALID_FILE_ATTRIBUTES);

    // Move
    std::string movedFile = longDir + "\\moved.txt";
    EXPECT_TRUE(SalLPMoveFile(copyFile.c_str(), movedFile.c_str()));
    EXPECT_EQ(SalLPGetFileAttributes(copyFile.c_str()), (DWORD)INVALID_FILE_ATTRIBUTES); // source gone
    EXPECT_NE(SalLPGetFileAttributes(movedFile.c_str()), (DWORD)INVALID_FILE_ATTRIBUTES); // target exists

    // Delete
    EXPECT_TRUE(SalLPDeleteFile(movedFile.c_str()));
    EXPECT_EQ(SalLPGetFileAttributes(movedFile.c_str()), (DWORD)INVALID_FILE_ATTRIBUTES);
}
