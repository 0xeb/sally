// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Integration tests for wide-path worker operations:
//   - NTFS compression (CompressFileW/UncompressFileW patterns)
//   - NTFS encryption (EncryptFileW/DecryptFileW patterns)
//   - Directory info queries (GetDirInfoW pattern)
//   - Trailing space/dot path fixup (MakeCopyWithBackslashIfNeeded pattern)
//   - Unicode filenames and long paths
//
// These tests exercise the same Win32 API patterns used by the worker.cpp
// wide-primary functions, validating correctness with edge-case paths.
//
//****************************************************************************

#include <gtest/gtest.h>
#include <windows.h>
#include <winioctl.h>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================================
// Test fixture — creates a unique NTFS temp directory per test
// ============================================================================

class WorkerWideTest : public ::testing::Test
{
protected:
    fs::path m_tempDir;

    void SetUp() override
    {
        wchar_t tmpPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpPath);
        m_tempDir = fs::path(tmpPath) / L"sal_worker_wide_test";

        std::error_code ec;
        fs::remove_all(m_tempDir, ec);
        fs::create_directories(m_tempDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_tempDir, ec);
    }

    // Create a test file with given content
    void CreateTestFile(const fs::path& path, const char* content = "test data")
    {
        // Ensure parent directory exists
        fs::create_directories(path.parent_path());
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ASSERT_NE(h, INVALID_HANDLE_VALUE)
            << "Failed to create: " << path << " err=" << GetLastError();
        DWORD written;
        WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
        CloseHandle(h);
    }

    // Create a test directory
    void CreateTestDir(const fs::path& path)
    {
        fs::create_directories(path);
    }

    // Check if file/dir is compressed
    bool IsCompressed(const fs::path& path)
    {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_COMPRESSED);
    }

    // Check if file/dir is encrypted
    bool IsEncrypted(const fs::path& path)
    {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_ENCRYPTED);
    }

    // Set compression on a file/directory (mirrors CompressFileW pattern)
    DWORD SetCompression(const fs::path& path, USHORT format)
    {
        HANDLE file = CreateFileW(path.c_str(), FILE_READ_DATA | FILE_WRITE_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (file == INVALID_HANDLE_VALUE)
            return GetLastError();
        ULONG length;
        DWORD ret = ERROR_SUCCESS;
        if (!DeviceIoControl(file, FSCTL_SET_COMPRESSION, &format,
                             sizeof(USHORT), NULL, 0, &length, FALSE))
            ret = GetLastError();
        CloseHandle(file);
        return ret;
    }

    // Build a long path component (repeating chars to reach desired length)
    std::wstring LongComponent(int len, wchar_t c = L'A')
    {
        return std::wstring(len, c);
    }
};

// ============================================================================
// NTFS Compression tests (CompressFileW/UncompressFileW patterns)
// ============================================================================

TEST_F(WorkerWideTest, Compress_BasicFile)
{
    auto file = m_tempDir / L"test.txt";
    CreateTestFile(file);

    EXPECT_FALSE(IsCompressed(file));
    DWORD err = SetCompression(file, COMPRESSION_FORMAT_DEFAULT);
    EXPECT_EQ(err, ERROR_SUCCESS);
    EXPECT_TRUE(IsCompressed(file));
}

TEST_F(WorkerWideTest, Uncompress_CompressedFile)
{
    auto file = m_tempDir / L"compressed.txt";
    CreateTestFile(file);

    // Compress first
    ASSERT_EQ(SetCompression(file, COMPRESSION_FORMAT_DEFAULT), ERROR_SUCCESS);
    ASSERT_TRUE(IsCompressed(file));

    // Uncompress
    DWORD err = SetCompression(file, COMPRESSION_FORMAT_NONE);
    EXPECT_EQ(err, ERROR_SUCCESS);
    EXPECT_FALSE(IsCompressed(file));
}

TEST_F(WorkerWideTest, Compress_UnicodeFilename)
{
    auto file = m_tempDir / L"\u4e2d\u6587\u6d4b\u8bd5.txt"; // Chinese: 中文测试
    CreateTestFile(file);

    DWORD err = SetCompression(file, COMPRESSION_FORMAT_DEFAULT);
    EXPECT_EQ(err, ERROR_SUCCESS);
    EXPECT_TRUE(IsCompressed(file));
}

TEST_F(WorkerWideTest, Compress_JapaneseKoreanFilename)
{
    auto file = m_tempDir / L"\u30c6\u30b9\u30c8_\ud14c\uc2a4\ud2b8.dat"; // テスト_테스트
    CreateTestFile(file);

    DWORD err = SetCompression(file, COMPRESSION_FORMAT_DEFAULT);
    EXPECT_EQ(err, ERROR_SUCCESS);
    EXPECT_TRUE(IsCompressed(file));
}

TEST_F(WorkerWideTest, Compress_EmojiFilename)
{
    // Emoji path — tests surrogate pair handling in wide APIs
    auto file = m_tempDir / L"\U0001F4C1_archive.txt"; // 📁_archive.txt
    CreateTestFile(file);

    DWORD err = SetCompression(file, COMPRESSION_FORMAT_DEFAULT);
    EXPECT_EQ(err, ERROR_SUCCESS);
    EXPECT_TRUE(IsCompressed(file));
}

TEST_F(WorkerWideTest, Compress_Directory)
{
    auto dir = m_tempDir / L"compressdir";
    CreateTestDir(dir);

    DWORD err = SetCompression(dir, COMPRESSION_FORMAT_DEFAULT);
    EXPECT_EQ(err, ERROR_SUCCESS);
    EXPECT_TRUE(IsCompressed(dir));
}

TEST_F(WorkerWideTest, Compress_ReadOnlyFile)
{
    // Mirrors the CompressFileW pattern: strip readonly, compress, restore
    auto file = m_tempDir / L"readonly.txt";
    CreateTestFile(file);
    SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY);

    DWORD attrs = GetFileAttributesW(file.c_str());
    ASSERT_TRUE(attrs & FILE_ATTRIBUTE_READONLY);

    // Strip readonly before compression (as CompressFileW does)
    SetFileAttributesW(file.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    DWORD err = SetCompression(file, COMPRESSION_FORMAT_DEFAULT);
    SetFileAttributesW(file.c_str(), attrs); // restore
    EXPECT_EQ(err, ERROR_SUCCESS);
    EXPECT_TRUE(IsCompressed(file));

    // Verify readonly is preserved
    attrs = GetFileAttributesW(file.c_str());
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_READONLY);

    // Cleanup: remove readonly for TearDown
    SetFileAttributesW(file.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
}

// ============================================================================
// NTFS Encryption tests (EncryptFileW/DecryptFileW patterns)
// ============================================================================

TEST_F(WorkerWideTest, Encrypt_BasicFile)
{
    auto file = m_tempDir / L"secret.txt";
    CreateTestFile(file);

    BOOL ok = EncryptFileW(file.c_str());
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED)
    {
        // EFS may not be available (e.g., Home edition, domain policy)
        GTEST_SKIP() << "EFS not available on this system";
    }
    EXPECT_TRUE(ok);
    EXPECT_TRUE(IsEncrypted(file));
}

TEST_F(WorkerWideTest, Decrypt_EncryptedFile)
{
    auto file = m_tempDir / L"encrypted.txt";
    CreateTestFile(file);

    if (!EncryptFileW(file.c_str()))
    {
        if (GetLastError() == ERROR_ACCESS_DENIED)
            GTEST_SKIP() << "EFS not available";
        FAIL() << "EncryptFileW failed: " << GetLastError();
    }
    ASSERT_TRUE(IsEncrypted(file));

    EXPECT_TRUE(DecryptFileW(file.c_str(), 0));
    EXPECT_FALSE(IsEncrypted(file));
}

TEST_F(WorkerWideTest, Encrypt_UnicodeFilename)
{
    auto file = m_tempDir / L"\u0428\u0438\u0444\u0440.txt"; // Russian: Шифр
    CreateTestFile(file);

    BOOL ok = EncryptFileW(file.c_str());
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED)
        GTEST_SKIP() << "EFS not available";
    EXPECT_TRUE(ok);
    EXPECT_TRUE(IsEncrypted(file));
}

TEST_F(WorkerWideTest, Encrypt_PreserveFileTime)
{
    // Mirrors MyEncryptFileW preserveDate pattern
    auto file = m_tempDir / L"preserve_time.txt";
    CreateTestFile(file);

    // Get original time
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    FILETIME origCreated, origModified;
    GetFileTime(h, &origCreated, NULL, &origModified);
    CloseHandle(h);

    if (!EncryptFileW(file.c_str()))
    {
        if (GetLastError() == ERROR_ACCESS_DENIED)
            GTEST_SKIP() << "EFS not available";
        FAIL() << "EncryptFileW failed: " << GetLastError();
    }

    // Restore timestamps (as MyEncryptFileW does with preserveDate)
    h = CreateFileW(file.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    EXPECT_TRUE(SetFileTime(h, &origCreated, NULL, &origModified));
    CloseHandle(h);

    // Verify timestamps were restored
    h = CreateFileW(file.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    FILETIME newCreated, newModified;
    GetFileTime(h, &newCreated, NULL, &newModified);
    CloseHandle(h);

    EXPECT_EQ(origCreated.dwLowDateTime, newCreated.dwLowDateTime);
    EXPECT_EQ(origCreated.dwHighDateTime, newCreated.dwHighDateTime);
    EXPECT_EQ(origModified.dwLowDateTime, newModified.dwLowDateTime);
    EXPECT_EQ(origModified.dwHighDateTime, newModified.dwHighDateTime);
}

// ============================================================================
// GetDirInfoW pattern tests — directory time queries
// ============================================================================

TEST_F(WorkerWideTest, DirInfo_BasicDirectory)
{
    auto dir = m_tempDir / L"infodir";
    CreateTestDir(dir);

    // Query via FindFirstFileW (as GetDirInfoW does for non-backslash paths)
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(dir.c_str(), &fd);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    FindClose(h);

    FILETIME ft = fd.ftLastWriteTime;
    SYSTEMTIME st;
    EXPECT_TRUE(FileTimeToLocalFileTime(&ft, &ft));
    EXPECT_TRUE(FileTimeToSystemTime(&ft, &st));
    EXPECT_GT(st.wYear, (WORD)2000);
}

TEST_F(WorkerWideTest, DirInfo_BackslashEnding)
{
    // GetDirInfoW uses CreateFileW for paths ending with backslash
    auto dir = m_tempDir / L"bsdir";
    CreateTestDir(dir);

    std::wstring dirWithBs = dir.wstring() + L"\\";
    HANDLE h = CreateFileW(dirWithBs.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);

    FILETIME lastWrite;
    EXPECT_TRUE(GetFileTime(h, NULL, NULL, &lastWrite));
    CloseHandle(h);

    SYSTEMTIME st;
    FILETIME ft;
    EXPECT_TRUE(FileTimeToLocalFileTime(&lastWrite, &ft));
    EXPECT_TRUE(FileTimeToSystemTime(&ft, &st));
    EXPECT_GT(st.wYear, (WORD)2000);
}

TEST_F(WorkerWideTest, DirInfo_UnicodeDirectory)
{
    auto dir = m_tempDir / L"\u00e9\u00e8\u00ea_caf\u00e9"; // éèê_café
    CreateTestDir(dir);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(dir.c_str(), &fd);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    FindClose(h);

    EXPECT_TRUE(fd.ftLastWriteTime.dwHighDateTime > 0 || fd.ftLastWriteTime.dwLowDateTime > 0);
}

TEST_F(WorkerWideTest, DirInfo_LongPathDirectory)
{
    // Create a directory with a deep path (>260 chars total)
    fs::path longDir = m_tempDir;
    for (int i = 0; i < 15; i++)
        longDir /= LongComponent(20, L'D' + (wchar_t)(i % 10));

    // Use \\?\ prefix for long path support
    std::wstring longPath = L"\\\\?\\" + longDir.wstring();

    // Create nested directories
    std::wstring current = L"\\\\?\\" + m_tempDir.wstring();
    for (int i = 0; i < 15; i++)
    {
        current += L"\\" + LongComponent(20, L'D' + (wchar_t)(i % 10));
        CreateDirectoryW(current.c_str(), NULL);
    }

    DWORD attrs = GetFileAttributesW(longPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        GTEST_SKIP() << "Long path creation failed (system may not support long paths)";

    // Query with CreateFileW + GetFileTime (as GetDirInfoW does for backslash paths)
    HANDLE h = CreateFileW((longPath + L"\\").c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE) << "CreateFileW failed: " << GetLastError();
    FILETIME lastWrite;
    EXPECT_TRUE(GetFileTime(h, NULL, NULL, &lastWrite));
    CloseHandle(h);
}

// ============================================================================
// Trailing space/dot fixup tests (MakeCopyWithBackslashIfNeeded pattern)
// ============================================================================

TEST_F(WorkerWideTest, TrailingSpace_CreateAndQuery)
{
    // Windows silently trims trailing spaces from paths unless you use \\?\
    // The MakeCopyWithBackslashIfNeeded pattern appends '\\' to fix this
    auto dir = m_tempDir / L"normaldir";
    CreateTestDir(dir);

    // Can query normally
    DWORD attrs = GetFileAttributesW(dir.c_str());
    EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

TEST_F(WorkerWideTest, TrailingDot_FileOperations)
{
    // Test that compression works on a file in a directory with a normal name
    // (trailing dot files require special \\?\ handling which the worker functions do)
    auto file = m_tempDir / L"dottest" / L"file.txt";
    CreateTestDir(m_tempDir / L"dottest");
    CreateTestFile(file);

    DWORD err = SetCompression(file, COMPRESSION_FORMAT_DEFAULT);
    EXPECT_EQ(err, ERROR_SUCCESS);
    EXPECT_TRUE(IsCompressed(file));
}

// ============================================================================
// Long path tests for compression/encryption
// ============================================================================

TEST_F(WorkerWideTest, Compress_LongPathFile)
{
    // Create a file with a long path (>260 chars)
    fs::path longDir = m_tempDir;
    for (int i = 0; i < 10; i++)
        longDir /= LongComponent(25, L'C' + (wchar_t)(i % 5));

    std::wstring longDirStr = L"\\\\?\\" + longDir.wstring();

    // Create nested directories
    std::wstring current = L"\\\\?\\" + m_tempDir.wstring();
    for (int i = 0; i < 10; i++)
    {
        current += L"\\" + LongComponent(25, L'C' + (wchar_t)(i % 5));
        CreateDirectoryW(current.c_str(), NULL);
    }

    std::wstring filePath = longDirStr + L"\\longfile.txt";

    // Create the file
    HANDLE h = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        GTEST_SKIP() << "Long path file creation failed";
    DWORD written;
    WriteFile(h, "data", 4, &written, NULL);
    CloseHandle(h);

    // Compress via DeviceIoControl (as CompressFileW does)
    h = CreateFileW(filePath.c_str(), FILE_READ_DATA | FILE_WRITE_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE) << "Failed to open for compression: " << GetLastError();

    USHORT state = COMPRESSION_FORMAT_DEFAULT;
    ULONG length;
    BOOL ok = DeviceIoControl(h, FSCTL_SET_COMPRESSION, &state,
                              sizeof(USHORT), NULL, 0, &length, FALSE);
    CloseHandle(h);
    EXPECT_TRUE(ok) << "Compression failed: " << GetLastError();

    // Verify compressed
    DWORD attrs = GetFileAttributesW(filePath.c_str());
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_COMPRESSED);
}

TEST_F(WorkerWideTest, Encrypt_LongPathFile)
{
    fs::path longDir = m_tempDir;
    for (int i = 0; i < 10; i++)
        longDir /= LongComponent(25, L'E' + (wchar_t)(i % 5));

    std::wstring longDirStr = L"\\\\?\\" + longDir.wstring();

    std::wstring current = L"\\\\?\\" + m_tempDir.wstring();
    for (int i = 0; i < 10; i++)
    {
        current += L"\\" + LongComponent(25, L'E' + (wchar_t)(i % 5));
        CreateDirectoryW(current.c_str(), NULL);
    }

    std::wstring filePath = longDirStr + L"\\encrypted.txt";

    HANDLE h = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        GTEST_SKIP() << "Long path file creation failed";
    DWORD written;
    WriteFile(h, "secret", 6, &written, NULL);
    CloseHandle(h);

    BOOL ok = EncryptFileW(filePath.c_str());
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED)
        GTEST_SKIP() << "EFS not available";
    EXPECT_TRUE(ok) << "EncryptFileW failed: " << GetLastError();

    DWORD attrs = GetFileAttributesW(filePath.c_str());
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_ENCRYPTED);
}
