// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Google Test suite for long path support in viewer operations.
//
// Tests the SalCreateFileH + SalLPGetFileAttributes APIs used by the
// internal viewer (viewer2.cpp) and file panel view/edit (fileswn5.cpp)
// to open files with paths exceeding MAX_PATH (260 chars).
//
// Issue: https://github.com/0xeb/sally/issues/24
//
//****************************************************************************

#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <sstream>

#include "../common/widepath.h"

// ============================================================================
// Test fixture: creates a deeply nested temp directory structure (>260 chars)
// ============================================================================

class LongPathViewerTest : public ::testing::Test
{
protected:
    std::string m_tempDir;   // Short base temp dir
    std::string m_longDir;   // Deep nested dir (>260 chars total)
    std::wstring m_tempDirW;

    void SetUp() override
    {
        // Create a unique short temp dir
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        char tempFile[MAX_PATH];
        GetTempFileNameA(tempPath, "lpv", 0, tempFile);
        DeleteFileA(tempFile);
        CreateDirectoryA(tempFile, NULL);
        m_tempDir = tempFile;

        wchar_t wideBuf[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, m_tempDir.c_str(), -1, wideBuf, MAX_PATH);
        m_tempDirW = wideBuf;

        // Build a deeply nested path exceeding MAX_PATH
        m_longDir = m_tempDir;
        int i = 0;
        while (m_longDir.size() < 300)
        {
            std::ostringstream oss;
            oss << "\\deep" << i++;
            m_longDir += oss.str();
        }

        // Create all intermediate directories using SalLPCreateDirectory
        std::string current = m_tempDir;
        size_t pos = m_tempDir.size();
        while (pos < m_longDir.size())
        {
            pos = m_longDir.find('\\', pos + 1);
            if (pos == std::string::npos)
                pos = m_longDir.size();
            current = m_longDir.substr(0, pos);
            ASSERT_TRUE(SalLPCreateDirectory(current.c_str(), NULL))
                << "Failed to create dir: " << current << " (len=" << current.size() << ")";
        }
        ASSERT_GT(m_longDir.size(), (size_t)MAX_PATH);
    }

    void TearDown() override
    {
        RecursiveDeleteW(m_tempDirW);
    }

    // Create a file at the given ANSI path with content, using wide API + \\?\ prefix
    void CreateFileAtLongPath(const std::string& ansiPath, const char* content, size_t contentLen)
    {
        int wlen = MultiByteToWideChar(CP_ACP, 0, ansiPath.c_str(), -1, NULL, 0);
        std::wstring wPath(wlen - 1, L'\0');
        MultiByteToWideChar(CP_ACP, 0, ansiPath.c_str(), -1, &wPath[0], wlen);
        if (wPath.length() >= MAX_PATH && wPath.substr(0, 4) != L"\\\\?\\")
            wPath = L"\\\\?\\" + wPath;
        HANDLE h = CreateFileW(wPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ASSERT_NE(h, INVALID_HANDLE_VALUE) << "Failed to create: " << ansiPath;
        DWORD written;
        WriteFile(h, content, (DWORD)contentLen, &written, NULL);
        CloseHandle(h);
    }

    void RecursiveDeleteW(const std::wstring& dir)
    {
        // Use \\?\ prefix for long path support during cleanup
        std::wstring prefixed = dir;
        if (dir.length() >= MAX_PATH && dir.substr(0, 4) != L"\\\\?\\")
            prefixed = L"\\\\?\\" + dir;

        WIN32_FIND_DATAW fd;
        std::wstring pattern = prefixed + L"\\*";
        HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                    continue;
                std::wstring full = dir + L"\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    RecursiveDeleteW(full);
                else
                {
                    std::wstring prefFull = full;
                    if (full.length() >= MAX_PATH && full.substr(0, 4) != L"\\\\?\\")
                        prefFull = L"\\\\?\\" + full;
                    DeleteFileW(prefFull.c_str());
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryW(prefixed.c_str());
    }
};

// ============================================================================
// Test: SalCreateFileH opens a file at a long path (>260 chars)
//
// This mirrors viewer2.cpp::FileChanged() which uses SalCreateFileH to open
// the file for reading. Previously it used CreateFileW(AnsiToWide(...)) which
// fails for paths >260 chars because it lacks the \\?\ prefix.
// ============================================================================

TEST_F(LongPathViewerTest, SalCreateFileH_OpenReadAtLongPath)
{
    std::string longFile = m_longDir + "\\testfile.txt";
    ASSERT_GT(longFile.size(), (size_t)MAX_PATH);

    const char* content = "Hello from a long path!";
    CreateFileAtLongPath(longFile, content, strlen(content));

    // Open using SalLPCreateFile (what SalCreateFileH resolves to in non-debug builds)
    HANDLE h = SalLPCreateFile(longFile.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE)
        << "SalLPCreateFile failed for path of length " << longFile.size();

    // Read content back — this is what the viewer does after opening
    char buf[256] = {};
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &bytesRead, NULL);
    CloseHandle(h);

    EXPECT_TRUE(ok);
    EXPECT_EQ(bytesRead, (DWORD)strlen(content));
    EXPECT_STREQ(buf, content);
}

// ============================================================================
// Test: SalLPGetFileAttributes works for files at long paths
//
// This mirrors fileswn5.cpp::ViewFile() / EditFile() which use
// SalLPGetFileAttributes to validate the file exists before opening it.
// Previously used GetFileAttributesW(AnsiToWide(...)) which lacks \\?\ prefix.
// ============================================================================

TEST_F(LongPathViewerTest, SalLPGetFileAttributes_LongPath)
{
    std::string longFile = m_longDir + "\\attrtest.txt";
    ASSERT_GT(longFile.size(), (size_t)MAX_PATH);

    CreateFileAtLongPath(longFile, "x", 1);

    DWORD attrs = SalLPGetFileAttributes(longFile.c_str());
    EXPECT_NE(attrs, (DWORD)INVALID_FILE_ATTRIBUTES)
        << "SalLPGetFileAttributes failed for path of length " << longFile.size();
    EXPECT_FALSE(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// ============================================================================
// Test: SalLPGetFileAttributes returns INVALID_FILE_ATTRIBUTES for nonexistent
//       files at long paths (error path validation)
// ============================================================================

TEST_F(LongPathViewerTest, SalLPGetFileAttributes_LongPath_Nonexistent)
{
    std::string longFile = m_longDir + "\\nonexistent.txt";
    ASSERT_GT(longFile.size(), (size_t)MAX_PATH);

    DWORD attrs = SalLPGetFileAttributes(longFile.c_str());
    EXPECT_EQ(attrs, (DWORD)INVALID_FILE_ATTRIBUTES);
}

// ============================================================================
// Test: Viewer-like file open + sequential read on long path
//
// Simulates what the viewer actually does: open with FILE_FLAG_SEQUENTIAL_SCAN,
// read file in chunks, verify content integrity.
// ============================================================================

TEST_F(LongPathViewerTest, ViewerOpenFile_SequentialRead)
{
    std::string longFile = m_longDir + "\\viewer_seq.txt";
    ASSERT_GT(longFile.size(), (size_t)MAX_PATH);

    // Create a larger test file (4KB) to simulate a real viewer scenario
    std::string content(4096, 'A');
    for (size_t i = 0; i < content.size(); i++)
        content[i] = 'A' + (char)(i % 26);
    CreateFileAtLongPath(longFile, content.c_str(), content.size());

    // Open like the viewer does
    HANDLE h = SalLPCreateFile(longFile.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);

    // Read in chunks like the viewer
    std::string readBack;
    char buf[1024];
    DWORD bytesRead;
    while (ReadFile(h, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0)
        readBack.append(buf, bytesRead);
    CloseHandle(h);

    EXPECT_EQ(readBack.size(), content.size());
    EXPECT_EQ(readBack, content);
}

// ============================================================================
// Test: FindFirstFileW with \\?\ prefix finds file at long path
//
// Validates that the \\?\ prefix approach works with FindFirstFileW — used
// by directory enumeration that feeds the viewer's file list.
// ============================================================================

TEST_F(LongPathViewerTest, FindFirstFileW_LongPath)
{
    std::string longFile = m_longDir + "\\findme.dat";
    ASSERT_GT(longFile.size(), (size_t)MAX_PATH);

    CreateFileAtLongPath(longFile, "data", 4);

    // Use SalLPFindFirstFile (which adds \\?\ prefix internally)
    WIN32_FIND_DATAW fd;
    HANDLE h = SalLPFindFirstFile(longFile.c_str(), &fd);
    ASSERT_NE(h, INVALID_HANDLE_VALUE)
        << "SalLPFindFirstFile failed for path of length " << longFile.size();
    EXPECT_STREQ(fd.cFileName, L"findme.dat");
    FindClose(h);
}

// ============================================================================
// Test: DOS name fallback at long path
//
// Simulates the pattern in fileswn5.cpp ViewFile/EditFile: first try the
// long name with SalLPGetFileAttributes, and if it fails, try the DOS name.
// At long paths, the important thing is that SalLPGetFileAttributes succeeds
// for existing files so the DOS fallback is never needed.
// ============================================================================

TEST_F(LongPathViewerTest, ViewerDosNameFallback_LongPath)
{
    std::string longFile = m_longDir + "\\longname.txt";
    ASSERT_GT(longFile.size(), (size_t)MAX_PATH);

    CreateFileAtLongPath(longFile, "test", 4);

    // Simulate the ViewFile pattern:
    // 1. Try GetFileAttributes on the full long path
    DWORD attrs = SalLPGetFileAttributes(longFile.c_str());
    EXPECT_NE(attrs, (DWORD)INVALID_FILE_ATTRIBUTES)
        << "Long path should be accessible - no DOS name fallback needed";

    // 2. If it succeeded, the viewer uses this path directly (no DOS fallback)
    // This is the expected path for long path files
    HANDLE h = SalLPCreateFile(longFile.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    CloseHandle(h);
}
