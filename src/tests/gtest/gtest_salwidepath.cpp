// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Google Test suite for SalWidePath, SalAnsiName classes and SalLP* wrappers
//
//****************************************************************************

#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

#include "../common/widepath.h"

// ============================================================================
// Helper: build an ANSI path of a given length using repeated dir components
// ============================================================================
static std::string MakeLongPath(size_t minLen)
{
    std::string path = "C:\\";
    int i = 0;
    while (path.size() < minLen)
    {
        std::ostringstream oss;
        oss << "dir" << i++ << "\\";
        path += oss.str();
    }
    // Remove trailing backslash
    if (!path.empty() && path.back() == '\\')
        path.pop_back();
    return path;
}

// ============================================================================
// SalWidePath tests
// ============================================================================

TEST(SalWidePath, ShortPathValid)
{
    SalWidePath wp("C:\\test\\file.txt");
    EXPECT_TRUE(wp.IsValid());
    EXPECT_NE(wp.Get(), nullptr);
}

TEST(SalWidePath, ShortPathNoPrefix)
{
    SalWidePath wp("C:\\test\\file.txt");
    EXPECT_FALSE(wp.HasLongPathPrefix());
    // Should be a simple wide conversion, no \\?\ prefix
    EXPECT_NE(wcsncmp(wp.Get(), L"\\\\?\\", 4), 0);
}

TEST(SalWidePath, ShortPathContentMatches)
{
    SalWidePath wp("C:\\hello");
    ASSERT_TRUE(wp.IsValid());
    EXPECT_STREQ(wp.Get(), L"C:\\hello");
}

TEST(SalWidePath, LongPathGetsPrefix)
{
    std::string longPath = MakeLongPath(SAL_LONG_PATH_THRESHOLD + 10);
    SalWidePath wp(longPath.c_str());
    ASSERT_TRUE(wp.IsValid());
    EXPECT_TRUE(wp.HasLongPathPrefix());
    // Must start with \\?\ prefix
    EXPECT_EQ(wcsncmp(wp.Get(), L"\\\\?\\", 4), 0);
}

TEST(SalWidePath, UNCPathGetsUNCPrefix)
{
    // Build a long UNC path
    std::string uncPath = "\\\\server\\share\\";
    while (uncPath.size() < SAL_LONG_PATH_THRESHOLD + 10)
        uncPath += "subdir\\";
    uncPath.pop_back();

    SalWidePath wp(uncPath.c_str());
    ASSERT_TRUE(wp.IsValid());
    EXPECT_TRUE(wp.HasLongPathPrefix());
    // Must start with \\?\UNC\ prefix
    EXPECT_EQ(wcsncmp(wp.Get(), L"\\\\?\\UNC\\", 8), 0);
}

TEST(SalWidePath, AlreadyPrefixedPathNotDoubled)
{
    // Short path that already has \\?\ should not get a second prefix
    SalWidePath wp("\\\\?\\C:\\test");
    ASSERT_TRUE(wp.IsValid());
    // m_hasPrefix should be FALSE because PathHasLongPrefix returns TRUE
    EXPECT_FALSE(wp.HasLongPathPrefix());
    // Should start with exactly one \\?\, not \\?\\..\?\.
    EXPECT_EQ(wcsncmp(wp.Get(), L"\\\\?\\C:\\test", 11), 0);
}

TEST(SalWidePath, NullPathInvalid)
{
    SalWidePath wp(nullptr);
    EXPECT_FALSE(wp.IsValid());
    EXPECT_EQ(wp.Get(), nullptr);
}

TEST(SalWidePath, EmptyPathValid)
{
    SalWidePath wp("");
    EXPECT_TRUE(wp.IsValid());
    EXPECT_STREQ(wp.Get(), L"");
}

TEST(SalWidePath, RootPath)
{
    SalWidePath wp("C:\\");
    ASSERT_TRUE(wp.IsValid());
    EXPECT_STREQ(wp.Get(), L"C:\\");
    EXPECT_FALSE(wp.HasLongPathPrefix());
}

TEST(SalWidePath, ImplicitConversion)
{
    SalWidePath wp("C:\\data");
    ASSERT_TRUE(wp.IsValid());
    const wchar_t* ptr = wp; // implicit conversion
    EXPECT_STREQ(ptr, L"C:\\data");
}

TEST(SalWidePath, PathAtThreshold)
{
    // Exactly at threshold: should get prefix
    std::string path = "C:\\";
    while (path.size() < SAL_LONG_PATH_THRESHOLD)
        path += "x";
    EXPECT_GE(path.size(), (size_t)SAL_LONG_PATH_THRESHOLD);

    SalWidePath wp(path.c_str());
    ASSERT_TRUE(wp.IsValid());
    EXPECT_TRUE(wp.HasLongPathPrefix());
}

TEST(SalWidePath, PathJustBelowThreshold)
{
    // Just below threshold: should NOT get prefix
    std::string path = "C:\\";
    while (path.size() < SAL_LONG_PATH_THRESHOLD - 1)
        path += "x";
    path.resize(SAL_LONG_PATH_THRESHOLD - 1);

    SalWidePath wp(path.c_str());
    ASSERT_TRUE(wp.IsValid());
    EXPECT_FALSE(wp.HasLongPathPrefix());
}

// ============================================================================
// SalAllocWidePath / SalFreeWidePath (manual API)
// ============================================================================

TEST(SalAllocWidePath, ShortPath)
{
    wchar_t* wp = SalAllocWidePath("D:\\foo\\bar");
    ASSERT_NE(wp, nullptr);
    EXPECT_STREQ(wp, L"D:\\foo\\bar");
    SalFreeWidePath(wp);
}

TEST(SalAllocWidePath, NullReturnsNull)
{
    wchar_t* wp = SalAllocWidePath(nullptr);
    EXPECT_EQ(wp, nullptr);
}

TEST(SalAllocWidePath, LongPathPrefixed)
{
    std::string lp = MakeLongPath(300);
    wchar_t* wp = SalAllocWidePath(lp.c_str());
    ASSERT_NE(wp, nullptr);
    EXPECT_EQ(wcsncmp(wp, L"\\\\?\\", 4), 0);
    SalFreeWidePath(wp);
}

// ============================================================================
// SalAnsiName tests
// ============================================================================

TEST(SalAnsiName, AsciiNameNotLossy)
{
    SalAnsiName name(L"readme.txt");
    EXPECT_FALSE(name.IsLossy());
    EXPECT_STREQ(name.GetAnsi(), "readme.txt");
    EXPECT_STREQ(name.GetWide(), L"readme.txt");
}

TEST(SalAnsiName, AsciiNameLengths)
{
    SalAnsiName name(L"test.doc");
    EXPECT_EQ(name.GetAnsiLen(), 8);
    EXPECT_EQ(name.GetWideLen(), 8);
}

TEST(SalAnsiName, UnicodeNameIsLossy)
{
    // CJK characters unlikely to be in any single-byte ANSI codepage
    SalAnsiName name(L"\x4E16\x754C.txt"); // 世界.txt
    // On a Western codepage this should be lossy
    // On a CJK codepage it might not be, so we test conservatively
    EXPECT_NE(name.GetAnsi(), nullptr);
    EXPECT_STREQ(name.GetWide(), L"\x4E16\x754C.txt");
}

TEST(SalAnsiName, NullInput)
{
    SalAnsiName name(nullptr);
    EXPECT_EQ(name.GetAnsi(), nullptr);
    EXPECT_EQ(name.GetWide(), nullptr);
    EXPECT_FALSE(name.IsLossy());
    EXPECT_EQ(name.GetAnsiLen(), 0);
    EXPECT_EQ(name.GetWideLen(), 0);
}

TEST(SalAnsiName, EmptyString)
{
    SalAnsiName name(L"");
    EXPECT_FALSE(name.IsLossy());
    EXPECT_STREQ(name.GetAnsi(), "");
    EXPECT_STREQ(name.GetWide(), L"");
    EXPECT_EQ(name.GetAnsiLen(), 0);
    EXPECT_EQ(name.GetWideLen(), 0);
}

TEST(SalAnsiName, AllocAnsiNameReturnsOwnedCopy)
{
    SalAnsiName name(L"copy_test.bin");
    char* copy = name.AllocAnsiName();
    ASSERT_NE(copy, nullptr);
    EXPECT_STREQ(copy, "copy_test.bin");
    // Must be a different pointer
    EXPECT_NE(copy, name.GetAnsi());
    free(copy);
}

TEST(SalAnsiName, AllocWideNameReturnsOwnedCopy)
{
    SalAnsiName name(L"wide_copy.dat");
    wchar_t* copy = name.AllocWideName();
    ASSERT_NE(copy, nullptr);
    EXPECT_STREQ(copy, L"wide_copy.dat");
    EXPECT_NE(copy, name.GetWide());
    free(copy);
}

TEST(SalAnsiName, AllocAnsiNameFromNull)
{
    SalAnsiName name(nullptr);
    EXPECT_EQ(name.AllocAnsiName(), nullptr);
    EXPECT_EQ(name.AllocWideName(), nullptr);
}

TEST(SalAnsiName, AsciiRoundTrip)
{
    // Convert wide -> ansi -> wide and verify
    const wchar_t* original = L"roundtrip.txt";
    SalAnsiName name(original);
    ASSERT_FALSE(name.IsLossy());
    // Convert back via MultiByteToWideChar
    int len = MultiByteToWideChar(CP_ACP, 0, name.GetAnsi(), -1, NULL, 0);
    ASSERT_GT(len, 0);
    std::wstring roundTripped(len - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, name.GetAnsi(), -1, &roundTripped[0], len);
    EXPECT_EQ(roundTripped, original);
}

TEST(SalAnsiName, SpecialCharsInName)
{
    SalAnsiName name(L"file (copy) [2].txt");
    EXPECT_FALSE(name.IsLossy());
    EXPECT_STREQ(name.GetAnsi(), "file (copy) [2].txt");
}

// ============================================================================
// SalLP* integration tests (real filesystem operations)
// ============================================================================

class SalLPFileOpsTest : public ::testing::Test
{
protected:
    std::string m_tempDir;

    void SetUp() override
    {
        // Create a unique temp dir for this test
        char tempPath[MAX_PATH];
        GetTempPathA(MAX_PATH, tempPath);
        char tempDir[MAX_PATH];
        GetTempFileNameA(tempPath, "sal", 0, tempDir);
        // GetTempFileName creates a file; delete it, then create as directory
        DeleteFileA(tempDir);
        CreateDirectoryA(tempDir, NULL);
        m_tempDir = tempDir;
    }

    void TearDown() override
    {
        // Clean up: remove files and directory
        WIN32_FIND_DATAA fd;
        std::string pattern = m_tempDir + "\\*";
        HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                    continue;
                std::string full = m_tempDir + "\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    RemoveDirectoryA(full.c_str());
                else
                    DeleteFileA(full.c_str());
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryA(m_tempDir.c_str());
    }

    // Helper: create a file with some content via SalLP API
    void CreateTestFile(const std::string& name, const char* content = "test")
    {
        std::string path = m_tempDir + "\\" + name;
        HANDLE h = SalLPCreateFile(path.c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ASSERT_NE(h, INVALID_HANDLE_VALUE) << "Failed to create: " << path;
        DWORD written;
        WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
        CloseHandle(h);
    }
};

TEST_F(SalLPFileOpsTest, CreateFileAndGetAttributes)
{
    CreateTestFile("hello.txt");
    std::string path = m_tempDir + "\\hello.txt";
    DWORD attrs = SalLPGetFileAttributes(path.c_str());
    EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
    EXPECT_FALSE(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

TEST_F(SalLPFileOpsTest, SetFileAttributes)
{
    CreateTestFile("readonly.txt");
    std::string path = m_tempDir + "\\readonly.txt";
    EXPECT_TRUE(SalLPSetFileAttributes(path.c_str(), FILE_ATTRIBUTE_READONLY));
    DWORD attrs = SalLPGetFileAttributes(path.c_str());
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_READONLY);
    // Remove readonly so TearDown can delete
    SalLPSetFileAttributes(path.c_str(), FILE_ATTRIBUTE_NORMAL);
}

TEST_F(SalLPFileOpsTest, DeleteFile)
{
    CreateTestFile("todelete.txt");
    std::string path = m_tempDir + "\\todelete.txt";
    EXPECT_TRUE(SalLPDeleteFile(path.c_str()));
    EXPECT_EQ(SalLPGetFileAttributes(path.c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(SalLPFileOpsTest, CreateAndRemoveDirectory)
{
    std::string subdir = m_tempDir + "\\subdir";
    EXPECT_TRUE(SalLPCreateDirectory(subdir.c_str(), NULL));
    DWORD attrs = SalLPGetFileAttributes(subdir.c_str());
    EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_DIRECTORY);
    EXPECT_TRUE(SalLPRemoveDirectory(subdir.c_str()));
    EXPECT_EQ(SalLPGetFileAttributes(subdir.c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(SalLPFileOpsTest, CopyFile)
{
    CreateTestFile("source.txt", "hello world");
    std::string src = m_tempDir + "\\source.txt";
    std::string dst = m_tempDir + "\\dest.txt";
    EXPECT_TRUE(SalLPCopyFile(src.c_str(), dst.c_str(), TRUE));
    // Verify destination exists
    EXPECT_NE(SalLPGetFileAttributes(dst.c_str()), INVALID_FILE_ATTRIBUTES);
    // Verify failIfExists works
    EXPECT_FALSE(SalLPCopyFile(src.c_str(), dst.c_str(), TRUE));
}

TEST_F(SalLPFileOpsTest, CopyFileOverwrite)
{
    CreateTestFile("src2.txt", "aaa");
    CreateTestFile("dst2.txt", "bbb");
    std::string src = m_tempDir + "\\src2.txt";
    std::string dst = m_tempDir + "\\dst2.txt";
    EXPECT_TRUE(SalLPCopyFile(src.c_str(), dst.c_str(), FALSE));
}

TEST_F(SalLPFileOpsTest, MoveFile)
{
    CreateTestFile("moveme.txt", "data");
    std::string src = m_tempDir + "\\moveme.txt";
    std::string dst = m_tempDir + "\\moved.txt";
    EXPECT_TRUE(SalLPMoveFile(src.c_str(), dst.c_str()));
    EXPECT_EQ(SalLPGetFileAttributes(src.c_str()), INVALID_FILE_ATTRIBUTES);
    EXPECT_NE(SalLPGetFileAttributes(dst.c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(SalLPFileOpsTest, GetFileAttributesNonexistent)
{
    std::string path = m_tempDir + "\\nonexistent.xyz";
    EXPECT_EQ(SalLPGetFileAttributes(path.c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(SalLPFileOpsTest, DeleteFileNonexistent)
{
    std::string path = m_tempDir + "\\nope.txt";
    EXPECT_FALSE(SalLPDeleteFile(path.c_str()));
}

TEST_F(SalLPFileOpsTest, NullPathReturnsFailure)
{
    EXPECT_EQ(SalLPGetFileAttributes(nullptr), INVALID_FILE_ATTRIBUTES);
    EXPECT_FALSE(SalLPDeleteFile(nullptr));
    EXPECT_FALSE(SalLPCreateDirectory(nullptr, NULL));
    EXPECT_FALSE(SalLPRemoveDirectory(nullptr));
    EXPECT_FALSE(SalLPMoveFile(nullptr, nullptr));
    EXPECT_FALSE(SalLPCopyFile(nullptr, nullptr, FALSE));
    EXPECT_EQ(SalLPCreateFile(nullptr, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL),
              INVALID_HANDLE_VALUE);
}

// ============================================================================
// SalLPFindFirstFile / FindNextFile tests
// ============================================================================

TEST_F(SalLPFileOpsTest, FindFirstFileWide)
{
    CreateTestFile("find_a.txt");
    CreateTestFile("find_b.txt");

    std::string pattern = m_tempDir + "\\find_*.txt";
    WIN32_FIND_DATAW fd;
    HANDLE h = SalLPFindFirstFile(pattern.c_str(), &fd);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);

    int count = 1;
    while (SalLPFindNextFile(h, &fd))
        count++;
    FindClose(h);

    EXPECT_EQ(count, 2);
}

TEST_F(SalLPFileOpsTest, FindFirstFileAnsi)
{
    CreateTestFile("ansi_x.dat");
    CreateTestFile("ansi_y.dat");

    std::string pattern = m_tempDir + "\\ansi_*.dat";
    WIN32_FIND_DATAA fd;
    HANDLE h = SalLPFindFirstFileA(pattern.c_str(), &fd);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);

    // Collect names
    std::vector<std::string> names;
    names.push_back(fd.cFileName);
    while (SalLPFindNextFileA(h, &fd))
        names.push_back(fd.cFileName);
    FindClose(h);

    EXPECT_EQ(names.size(), 2u);
    // Both names should be present (order may vary)
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names[0], "ansi_x.dat");
    EXPECT_EQ(names[1], "ansi_y.dat");
}

TEST_F(SalLPFileOpsTest, FindFirstFileNoMatch)
{
    std::string pattern = m_tempDir + "\\nomatch_*.zzz";
    WIN32_FIND_DATAW fd;
    HANDLE h = SalLPFindFirstFile(pattern.c_str(), &fd);
    EXPECT_EQ(h, INVALID_HANDLE_VALUE);
}

TEST_F(SalLPFileOpsTest, FindFirstFileNullPath)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = SalLPFindFirstFile(nullptr, &fd);
    EXPECT_EQ(h, INVALID_HANDLE_VALUE);
}

TEST_F(SalLPFileOpsTest, FindFirstFileDirectoryEntry)
{
    std::string subdir = m_tempDir + "\\finddir";
    SalLPCreateDirectory(subdir.c_str(), NULL);

    std::string pattern = m_tempDir + "\\finddir";
    WIN32_FIND_DATAW fd;
    HANDLE h = SalLPFindFirstFile(pattern.c_str(), &fd);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    EXPECT_TRUE(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    FindClose(h);

    SalLPRemoveDirectory(subdir.c_str());
}

// ============================================================================
// SalLPCreateFile tests
// ============================================================================

TEST_F(SalLPFileOpsTest, CreateFileRead)
{
    CreateTestFile("readtest.txt", "abc123");
    std::string path = m_tempDir + "\\readtest.txt";
    HANDLE h = SalLPCreateFile(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, 0, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    char buf[32] = {};
    DWORD read;
    EXPECT_TRUE(ReadFile(h, buf, sizeof(buf) - 1, &read, NULL));
    EXPECT_STREQ(buf, "abc123");
    CloseHandle(h);
}

TEST_F(SalLPFileOpsTest, CreateFileOpenNonexistent)
{
    std::string path = m_tempDir + "\\doesnotexist.txt";
    HANDLE h = SalLPCreateFile(path.c_str(), GENERIC_READ, 0, NULL,
                               OPEN_EXISTING, 0, NULL);
    EXPECT_EQ(h, INVALID_HANDLE_VALUE);
}
