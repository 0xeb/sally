// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

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
