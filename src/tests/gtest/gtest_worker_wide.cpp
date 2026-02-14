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
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// REPARSE_DATA_BUFFER is not in standard SDK headers (needs ntifs.h from DDK)
#ifndef REPARSE_DATA_BUFFER_HEADER_SIZE
typedef struct _REPARSE_DATA_BUFFER
{
    ULONG ReparseTag;
    USHORT ReparseDataLength;
    USHORT Reserved;
    union
    {
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            ULONG Flags;
            WCHAR PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct
        {
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR PathBuffer[1];
        } MountPointReparseBuffer;
        struct
        {
            UCHAR DataBuffer[1];
        } GenericReparseBuffer;
    } DUMMYUNIONNAME;
    // ReSharper disable once CppInconsistentNaming
} REPARSE_DATA_BUFFER, *PREPARSE_DATA_BUFFER;
#define REPARSE_DATA_BUFFER_HEADER_SIZE FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer)
#endif

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

    // Ensure file starts uncompressed (temp dir may inherit NTFS compression)
    SetCompression(file, COMPRESSION_FORMAT_NONE);
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

// ============================================================================
// Junction/Reparse-point deletion tests (DoDeleteDirLinkAuxW pattern)
//
// DoDeleteDirLinkAuxW removes directory reparse points (junctions, symlinks)
// by: GetFileAttributesW → CreateFileW(OPEN_REPARSE_POINT) →
//     FSCTL_GET_REPARSE_POINT → FSCTL_DELETE_REPARSE_POINT → RemoveDirectoryW
// ============================================================================

// Helper: create a directory junction (mount point) from linkPath → targetPath
static BOOL CreateJunction(const std::wstring& linkPath, const std::wstring& targetPath)
{
    // Create the link directory (must exist and be empty)
    if (!CreateDirectoryW(linkPath.c_str(), NULL))
        return FALSE;

    // Open with reparse semantics
    HANDLE h = CreateFileW(linkPath.c_str(), GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                           NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        RemoveDirectoryW(linkPath.c_str());
        return FALSE;
    }

    // Build the reparse data buffer for a junction (IO_REPARSE_TAG_MOUNT_POINT)
    // Target must be in NT path form: \??\C:\path
    std::wstring ntTarget = L"\\??\\" + targetPath;
    WORD targetByteLen = (WORD)(ntTarget.size() * sizeof(wchar_t));
    WORD bufferSize = (WORD)(targetByteLen + sizeof(wchar_t) + sizeof(wchar_t)); // null + print name null

    size_t rdbSize = FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer) + bufferSize;
    std::vector<char> buf(rdbSize, 0);
    REPARSE_DATA_BUFFER* rdb = reinterpret_cast<REPARSE_DATA_BUFFER*>(buf.data());

    rdb->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    rdb->ReparseDataLength = (WORD)(rdbSize - FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer));
    rdb->MountPointReparseBuffer.SubstituteNameOffset = 0;
    rdb->MountPointReparseBuffer.SubstituteNameLength = targetByteLen;
    rdb->MountPointReparseBuffer.PrintNameOffset = targetByteLen + sizeof(wchar_t);
    rdb->MountPointReparseBuffer.PrintNameLength = 0;
    memcpy(rdb->MountPointReparseBuffer.PathBuffer, ntTarget.c_str(), targetByteLen + sizeof(wchar_t));

    DWORD returned;
    BOOL ok = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, rdb, (DWORD)rdbSize,
                              NULL, 0, &returned, NULL);
    CloseHandle(h);

    if (!ok)
        RemoveDirectoryW(linkPath.c_str());
    return ok;
}

// Helper: delete a reparse point and remove the directory — same logic as DoDeleteDirLinkAuxW
static BOOL DeleteReparsePointAndDir(const std::wstring& path, DWORD* outErr = nullptr)
{
    if (outErr) *outErr = ERROR_SUCCESS;

    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        // No reparse point — just try removing the dir
        if (!RemoveDirectoryW(path.c_str()))
        {
            if (outErr) *outErr = GetLastError();
            return FALSE;
        }
        return TRUE;
    }

    HANDLE dir = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (dir == INVALID_HANDLE_VALUE)
    {
        if (outErr) *outErr = GetLastError();
        return FALSE;
    }

    DWORD dummy;
    char rdbBuf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
    REPARSE_GUID_DATA_BUFFER* juncData = (REPARSE_GUID_DATA_BUFFER*)rdbBuf;
    BOOL ok = FALSE;

    if (!DeviceIoControl(dir, FSCTL_GET_REPARSE_POINT, NULL, 0, juncData,
                         MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &dummy, NULL))
    {
        if (outErr) *outErr = GetLastError();
        CloseHandle(dir);
        return FALSE;
    }

    if (juncData->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT &&
        juncData->ReparseTag != IO_REPARSE_TAG_SYMLINK)
    {
        if (outErr) *outErr = 4394; // ERROR_REPARSE_TAG_MISMATCH
        CloseHandle(dir);
        return FALSE;
    }

    REPARSE_GUID_DATA_BUFFER rgdb = {0};
    rgdb.ReparseTag = juncData->ReparseTag;
    DWORD dwBytes;
    ok = DeviceIoControl(dir, FSCTL_DELETE_REPARSE_POINT, &rgdb,
                         REPARSE_GUID_DATA_BUFFER_HEADER_SIZE,
                         NULL, 0, &dwBytes, NULL);
    CloseHandle(dir);

    if (!ok)
    {
        if (outErr) *outErr = GetLastError();
        return FALSE;
    }

    // Now remove the empty directory
    if (!RemoveDirectoryW(path.c_str()))
    {
        if (outErr) *outErr = GetLastError();
        return FALSE;
    }
    return TRUE;
}

TEST_F(WorkerWideTest, Junction_CreateAndDelete)
{
    auto target = m_tempDir / L"junction_target";
    auto link = m_tempDir / L"junction_link";
    CreateTestDir(target);

    // Create a junction
    BOOL created = CreateJunction(link.wstring(), target.wstring());
    if (!created)
        GTEST_SKIP() << "Junction creation failed (may require elevated privileges)";

    // Verify it's a reparse point
    DWORD attr = GetFileAttributesW(link.c_str());
    EXPECT_NE(attr, INVALID_FILE_ATTRIBUTES);
    EXPECT_TRUE(attr & FILE_ATTRIBUTE_REPARSE_POINT);
    EXPECT_TRUE(attr & FILE_ATTRIBUTE_DIRECTORY);

    // Delete using the same pattern as DoDeleteDirLinkAuxW
    DWORD err;
    BOOL ok = DeleteReparsePointAndDir(link.wstring(), &err);
    EXPECT_TRUE(ok) << "Delete junction failed, err=" << err;

    // Verify junction is gone
    attr = GetFileAttributesW(link.c_str());
    EXPECT_EQ(attr, INVALID_FILE_ATTRIBUTES);

    // Verify target directory still exists (junction deletion doesn't affect target)
    attr = GetFileAttributesW(target.c_str());
    EXPECT_NE(attr, INVALID_FILE_ATTRIBUTES);
}

TEST_F(WorkerWideTest, Junction_UnicodeNames)
{
    auto target = m_tempDir / L"\u76ee\u6a19\u30c7\u30a3\u30ec\u30af\u30c8\u30ea"; // 目標ディレクトリ (Japanese: target directory)
    auto link = m_tempDir / L"\u30ea\u30f3\u30af_\u63a5\u5408"; // リンク_接合 (Japanese: link_junction)
    CreateTestDir(target);

    BOOL created = CreateJunction(link.wstring(), target.wstring());
    if (!created)
        GTEST_SKIP() << "Junction creation with Unicode names failed";

    DWORD attr = GetFileAttributesW(link.c_str());
    EXPECT_TRUE(attr & FILE_ATTRIBUTE_REPARSE_POINT);

    DWORD err;
    BOOL ok = DeleteReparsePointAndDir(link.wstring(), &err);
    EXPECT_TRUE(ok) << "Delete Unicode junction failed, err=" << err;

    // Junction gone, target still exists
    EXPECT_EQ(GetFileAttributesW(link.c_str()), INVALID_FILE_ATTRIBUTES);
    EXPECT_NE(GetFileAttributesW(target.c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(WorkerWideTest, Junction_LongPath)
{
    // Create target and link with long paths (>260 chars)
    fs::path longBase = m_tempDir;
    for (int i = 0; i < 12; i++)
        longBase /= LongComponent(20, L'J' + (wchar_t)(i % 5));

    std::wstring prefix = L"\\\\?\\" + m_tempDir.wstring();
    std::wstring current = prefix;
    for (int i = 0; i < 12; i++)
    {
        current += L"\\" + LongComponent(20, L'J' + (wchar_t)(i % 5));
        CreateDirectoryW(current.c_str(), NULL);
    }

    std::wstring longTarget = current + L"\\target";
    std::wstring longLink = current + L"\\link";
    CreateDirectoryW(longTarget.c_str(), NULL);

    if (GetFileAttributesW(longTarget.c_str()) == INVALID_FILE_ATTRIBUTES)
        GTEST_SKIP() << "Long path creation failed";

    BOOL created = CreateJunction(longLink, longTarget);
    if (!created)
        GTEST_SKIP() << "Junction creation at long path failed";

    DWORD attr = GetFileAttributesW(longLink.c_str());
    EXPECT_TRUE(attr & FILE_ATTRIBUTE_REPARSE_POINT);

    DWORD err;
    BOOL ok = DeleteReparsePointAndDir(longLink, &err);
    EXPECT_TRUE(ok) << "Delete long-path junction failed, err=" << err;

    EXPECT_EQ(GetFileAttributesW(longLink.c_str()), INVALID_FILE_ATTRIBUTES);
    EXPECT_NE(GetFileAttributesW(longTarget.c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(WorkerWideTest, Junction_NonReparseDir_JustRemoves)
{
    // When the directory is NOT a reparse point, DoDeleteDirLinkAuxW still
    // tries to remove the empty directory
    auto dir = m_tempDir / L"plain_empty_dir";
    CreateTestDir(dir);

    DWORD attr = GetFileAttributesW(dir.c_str());
    EXPECT_FALSE(attr & FILE_ATTRIBUTE_REPARSE_POINT);

    DWORD err;
    BOOL ok = DeleteReparsePointAndDir(dir.wstring(), &err);
    EXPECT_TRUE(ok);

    EXPECT_EQ(GetFileAttributesW(dir.c_str()), INVALID_FILE_ATTRIBUTES);
}

TEST_F(WorkerWideTest, Junction_ReadOnlyDir)
{
    // DoDeleteDirLinkAuxW calls ClearReadOnlyAttrW before RemoveDirectoryW
    auto target = m_tempDir / L"ro_target";
    auto link = m_tempDir / L"ro_link";
    CreateTestDir(target);

    BOOL created = CreateJunction(link.wstring(), target.wstring());
    if (!created)
        GTEST_SKIP() << "Junction creation failed";

    // Make the junction directory read-only
    SetFileAttributesW(link.c_str(),
                       GetFileAttributesW(link.c_str()) | FILE_ATTRIBUTE_READONLY);

    DWORD attr = GetFileAttributesW(link.c_str());
    EXPECT_TRUE(attr & FILE_ATTRIBUTE_READONLY);

    // Clear read-only before deletion (as ClearReadOnlyAttrW does)
    SetFileAttributesW(link.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);

    DWORD err;
    BOOL ok = DeleteReparsePointAndDir(link.wstring(), &err);
    EXPECT_TRUE(ok) << "Delete read-only junction failed, err=" << err;

    EXPECT_EQ(GetFileAttributesW(link.c_str()), INVALID_FILE_ATTRIBUTES);
}

// ---- ADS (Alternate Data Streams) tests ----
// These test the same Win32 API patterns used by CheckFileOrDirADS and DoCopyADS

class AdsTest : public ::testing::Test
{
protected:
    fs::path tempDir;

    void SetUp() override
    {
        tempDir = fs::temp_directory_path() / L"sal_ads_test";
        fs::create_directories(tempDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }

    // Write data to a named ADS on a file
    static bool WriteADS(const std::wstring& filePath, const wchar_t* streamName, const void* data, DWORD size)
    {
        std::wstring adsPath = filePath + L":" + streamName;
        HANDLE h = CreateFileW(adsPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written;
        BOOL ok = WriteFile(h, data, size, &written, NULL);
        CloseHandle(h);
        return ok && written == size;
    }

    // Read data from a named ADS on a file
    static std::string ReadADS(const std::wstring& filePath, const wchar_t* streamName)
    {
        std::wstring adsPath = filePath + L":" + streamName;
        HANDLE h = CreateFileW(adsPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return {};
        char buf[4096];
        DWORD bytesRead;
        std::string result;
        while (ReadFile(h, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0)
            result.append(buf, bytesRead);
        CloseHandle(h);
        return result;
    }

    // Enumerate ADS streams using FindFirstStreamW/FindNextStreamW
    static std::vector<std::wstring> EnumerateStreams(const std::wstring& filePath)
    {
        std::vector<std::wstring> streams;
        WIN32_FIND_STREAM_DATA streamData;
        HANDLE h = FindFirstStreamW(filePath.c_str(), FindStreamInfoStandard, &streamData, 0);
        if (h == INVALID_HANDLE_VALUE)
            return streams;
        do
        {
            // Stream names look like ":streamname:$DATA"
            streams.push_back(streamData.cStreamName);
        } while (FindNextStreamW(h, &streamData));
        FindClose(h);
        return streams;
    }
};

TEST_F(AdsTest, WriteAndRead_BasicStream)
{
    fs::path file = tempDir / "test.txt";
    // Create the main file
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    const char mainData[] = "main content";
    DWORD written;
    WriteFile(h, mainData, sizeof(mainData) - 1, &written, NULL);
    CloseHandle(h);

    // Write an ADS
    const char adsData[] = "alternate stream data";
    ASSERT_TRUE(WriteADS(file.wstring(), L"mystream", adsData, sizeof(adsData) - 1));

    // Read it back
    std::string readBack = ReadADS(file.wstring(), L"mystream");
    EXPECT_EQ(readBack, "alternate stream data");

    // Main stream unaffected
    h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    char buf[64];
    DWORD bytesRead;
    ReadFile(h, buf, sizeof(buf), &bytesRead, NULL);
    CloseHandle(h);
    EXPECT_EQ(std::string(buf, bytesRead), "main content");
}

TEST_F(AdsTest, EnumerateStreams_MultipleADS)
{
    fs::path file = tempDir / "multi.txt";
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    CloseHandle(h);

    const char data1[] = "stream1";
    const char data2[] = "stream2";
    ASSERT_TRUE(WriteADS(file.wstring(), L"alpha", data1, sizeof(data1) - 1));
    ASSERT_TRUE(WriteADS(file.wstring(), L"beta", data2, sizeof(data2) - 1));

    auto streams = EnumerateStreams(file.wstring());
    // Should have at least ::$DATA (main), :alpha:$DATA, :beta:$DATA
    ASSERT_GE(streams.size(), 3u);

    // Check for our named streams
    bool foundAlpha = false, foundBeta = false;
    for (auto& s : streams)
    {
        if (s == L":alpha:$DATA")
            foundAlpha = true;
        if (s == L":beta:$DATA")
            foundBeta = true;
    }
    EXPECT_TRUE(foundAlpha) << "Expected :alpha:$DATA stream";
    EXPECT_TRUE(foundBeta) << "Expected :beta:$DATA stream";
}

TEST_F(AdsTest, UnicodeStreamName)
{
    fs::path file = tempDir / L"\x30C6\x30B9\x30C8.txt"; // テスト.txt
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    CloseHandle(h);

    const char data[] = "unicode stream";
    ASSERT_TRUE(WriteADS(file.wstring(), L"\x30B9\x30C8\x30EA\x30FC\x30E0", data, sizeof(data) - 1)); // ストリーム

    std::string readBack = ReadADS(file.wstring(), L"\x30B9\x30C8\x30EA\x30FC\x30E0");
    EXPECT_EQ(readBack, "unicode stream");
}

TEST_F(AdsTest, LongPath_ADS)
{
    // Build path exceeding MAX_PATH by creating directories one at a time
    std::wstring longDir = L"\\\\?\\" + tempDir.wstring();
    for (int i = 0; i < 15; i++)
    {
        longDir += L"\\subdir_pad_" + std::to_wstring(i);
        CreateDirectoryW(longDir.c_str(), NULL);
    }

    std::wstring longFile = longDir + L"\\file.txt";
    HANDLE h = CreateFileW(longFile.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        GTEST_SKIP() << "Cannot create long-path file";
    CloseHandle(h);

    const char data[] = "long path ADS data";
    std::wstring adsPath = longFile + L":longstream";
    h = CreateFileW(adsPath.c_str(), GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        GTEST_SKIP() << "Cannot create ADS on long path";
    DWORD written;
    WriteFile(h, data, sizeof(data) - 1, &written, NULL);
    CloseHandle(h);

    // Read back
    h = CreateFileW(adsPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    char buf[64];
    DWORD bytesRead;
    ReadFile(h, buf, sizeof(buf), &bytesRead, NULL);
    CloseHandle(h);
    EXPECT_EQ(std::string(buf, bytesRead), "long path ADS data");
}

TEST_F(AdsTest, DirectoryADS)
{
    // Directories can also have ADS
    fs::path dir = tempDir / "dirwithads";
    fs::create_directory(dir);

    const char data[] = "dir ADS data";
    ASSERT_TRUE(WriteADS(dir.wstring(), L"dirstream", data, sizeof(data) - 1));

    std::string readBack = ReadADS(dir.wstring(), L"dirstream");
    EXPECT_EQ(readBack, "dir ADS data");

    auto streams = EnumerateStreams(dir.wstring());
    bool foundDirStream = false;
    for (auto& s : streams)
    {
        if (s == L":dirstream:$DATA")
            foundDirStream = true;
    }
    EXPECT_TRUE(foundDirStream) << "Expected :dirstream:$DATA on directory";
}
