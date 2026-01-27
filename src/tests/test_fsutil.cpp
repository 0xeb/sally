// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//
// Tests for fsutil.h - UI-decoupled file system utilities
// Uses real filesystem operations in a temp directory
//

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

// Minimal standalone test - doesn't need precomp.h
#define SAL_LONG_PATH_THRESHOLD 240
#define SAL_MAX_LONG_PATH 32767

#include "../common/fsutil.h"

// Test result tracking
static int g_passed = 0;
static int g_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            printf("  [PASS] %s\n", msg); \
            g_passed++; \
        } else { \
            printf("  [FAIL] %s\n", msg); \
            g_failed++; \
        } \
    } while (0)

// Get temp directory for tests
std::wstring GetTestDir()
{
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring result = temp;
    result += L"salamander_fsutil_test";
    return result;
}

// Create test directory structure
bool SetupTestDir()
{
    std::wstring testDir = GetTestDir();

    // Clean up if exists
    RemoveDirectoryW((testDir + L"\\subdir").c_str());
    DeleteFileW((testDir + L"\\test.txt").c_str());
    RemoveDirectoryW(testDir.c_str());

    // Create fresh
    if (!CreateDirectoryW(testDir.c_str(), NULL))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }

    // Create a test file
    std::wstring testFile = testDir + L"\\test.txt";
    HANDLE h = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    const char* content = "Test content for fsutil";
    DWORD written;
    WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
    CloseHandle(h);

    // Create a subdirectory
    CreateDirectoryW((testDir + L"\\subdir").c_str(), NULL);

    return true;
}

// Clean up test directory
void CleanupTestDir()
{
    std::wstring testDir = GetTestDir();
    DeleteFileW((testDir + L"\\test.txt").c_str());
    RemoveDirectoryW((testDir + L"\\subdir").c_str());
    RemoveDirectoryW(testDir.c_str());
}

//
// Test 1: GetFileInfoW on existing file
//
void TestGetFileInfoW_File()
{
    printf("\n=== Test: GetFileInfoW on file ===\n");

    std::wstring testFile = GetTestDir() + L"\\test.txt";
    SalFileInfo info = GetFileInfoW(testFile.c_str());

    TEST_ASSERT(info.IsValid, "GetFileInfoW returns valid for existing file");
    TEST_ASSERT(!(info.Attributes & FILE_ATTRIBUTE_DIRECTORY), "File is not a directory");
    TEST_ASSERT(info.FileSize > 0, "File has content");
    TEST_ASSERT(info.FileName == L"test.txt", "FileName is correct");
}

//
// Test 2: GetFileInfoW on existing directory
//
void TestGetFileInfoW_Directory()
{
    printf("\n=== Test: GetFileInfoW on directory ===\n");

    std::wstring testDir = GetTestDir() + L"\\subdir";
    SalFileInfo info = GetFileInfoW(testDir.c_str());

    TEST_ASSERT(info.IsValid, "GetFileInfoW returns valid for existing directory");
    TEST_ASSERT(info.Attributes & FILE_ATTRIBUTE_DIRECTORY, "Directory has directory attribute");
    TEST_ASSERT(info.FileName == L"subdir", "Directory name is correct");
}

//
// Test 3: GetFileInfoW on non-existent path
//
void TestGetFileInfoW_NotFound()
{
    printf("\n=== Test: GetFileInfoW on non-existent path ===\n");

    std::wstring noFile = GetTestDir() + L"\\does_not_exist.txt";
    SalFileInfo info = GetFileInfoW(noFile.c_str());

    TEST_ASSERT(!info.IsValid, "GetFileInfoW returns invalid for non-existent path");
    TEST_ASSERT(info.LastError == ERROR_FILE_NOT_FOUND ||
                info.LastError == ERROR_PATH_NOT_FOUND,
                "LastError is FILE_NOT_FOUND or PATH_NOT_FOUND");
}

//
// Test 4: GetFileInfoW with NULL
//
void TestGetFileInfoW_Null()
{
    printf("\n=== Test: GetFileInfoW with NULL ===\n");

    SalFileInfo info = GetFileInfoW(NULL);

    TEST_ASSERT(!info.IsValid, "GetFileInfoW returns invalid for NULL");
    TEST_ASSERT(info.LastError == ERROR_INVALID_PARAMETER, "LastError is INVALID_PARAMETER");
}

//
// Test 5: BuildPathW wide strings
//
void TestBuildPathW_Wide()
{
    printf("\n=== Test: BuildPathW (wide) ===\n");

    std::wstring result = BuildPathW(L"C:\\Users", L"test.txt");
    TEST_ASSERT(result == L"C:\\Users\\test.txt", "BuildPathW adds backslash");

    result = BuildPathW(L"C:\\Users\\", L"test.txt");
    TEST_ASSERT(result == L"C:\\Users\\test.txt", "BuildPathW doesn't double backslash");

    result = BuildPathW(L"", L"test.txt");
    TEST_ASSERT(result == L"test.txt", "BuildPathW handles empty directory");

    result = BuildPathW(L"C:\\Users", L"");
    TEST_ASSERT(result == L"C:\\Users\\", "BuildPathW handles empty filename");

    result = BuildPathW(NULL, L"test.txt");
    TEST_ASSERT(result == L"test.txt", "BuildPathW handles NULL directory");
}

//
// Test 6: BuildPathW ANSI strings
//
void TestBuildPathW_Ansi()
{
    printf("\n=== Test: BuildPathW (ANSI) ===\n");

    std::wstring result = BuildPathW("C:\\Users", "test.txt");
    TEST_ASSERT(result == L"C:\\Users\\test.txt", "BuildPathW ANSI works");

    result = BuildPathW("C:\\Test", "file.doc");
    TEST_ASSERT(result == L"C:\\Test\\file.doc", "BuildPathW ANSI combines correctly");
}

//
// Test 7: PathExistsW
//
void TestPathExistsW()
{
    printf("\n=== Test: PathExistsW ===\n");

    std::wstring testDir = GetTestDir();
    std::wstring testFile = testDir + L"\\test.txt";

    TEST_ASSERT(PathExistsW(testDir.c_str()), "PathExistsW returns true for existing directory");
    TEST_ASSERT(PathExistsW(testFile.c_str()), "PathExistsW returns true for existing file");
    TEST_ASSERT(!PathExistsW((testDir + L"\\nope.txt").c_str()), "PathExistsW returns false for non-existent");
    TEST_ASSERT(!PathExistsW(NULL), "PathExistsW returns false for NULL");
}

//
// Test 8: IsDirectoryW
//
void TestIsDirectoryW()
{
    printf("\n=== Test: IsDirectoryW ===\n");

    std::wstring testDir = GetTestDir();
    std::wstring testFile = testDir + L"\\test.txt";
    std::wstring subDir = testDir + L"\\subdir";

    TEST_ASSERT(IsDirectoryW(testDir.c_str()), "IsDirectoryW returns true for directory");
    TEST_ASSERT(IsDirectoryW(subDir.c_str()), "IsDirectoryW returns true for subdirectory");
    TEST_ASSERT(!IsDirectoryW(testFile.c_str()), "IsDirectoryW returns false for file");
    TEST_ASSERT(!IsDirectoryW((testDir + L"\\nope").c_str()), "IsDirectoryW returns false for non-existent");
}

//
// Test 9: GetFileNameW - pure string operation
//
void TestGetFileNameW()
{
    printf("\n=== Test: GetFileNameW ===\n");

    TEST_ASSERT(GetFileNameW(L"C:\\Users\\test.txt") == L"test.txt", "GetFileNameW extracts filename");
    TEST_ASSERT(GetFileNameW(L"C:\\Users\\Dir\\file.doc") == L"file.doc", "GetFileNameW works with deep path");
    TEST_ASSERT(GetFileNameW(L"test.txt") == L"test.txt", "GetFileNameW handles no directory");
    TEST_ASSERT(GetFileNameW(L"C:\\") == L"", "GetFileNameW handles root path");
    TEST_ASSERT(GetFileNameW(L"C:\\Users\\") == L"", "GetFileNameW handles trailing backslash");
    TEST_ASSERT(GetFileNameW(NULL) == L"", "GetFileNameW handles NULL");
    TEST_ASSERT(GetFileNameW(L"") == L"", "GetFileNameW handles empty");
}

//
// Test 10: GetDirectoryW - pure string operation
//
void TestGetDirectoryW()
{
    printf("\n=== Test: GetDirectoryW ===\n");

    TEST_ASSERT(GetDirectoryW(L"C:\\Users\\test.txt") == L"C:\\Users", "GetDirectoryW extracts directory");
    TEST_ASSERT(GetDirectoryW(L"C:\\Users\\Dir\\file.doc") == L"C:\\Users\\Dir", "GetDirectoryW works with deep path");
    TEST_ASSERT(GetDirectoryW(L"test.txt") == L"", "GetDirectoryW handles no directory");
    TEST_ASSERT(GetDirectoryW(L"C:\\file.txt") == L"C:", "GetDirectoryW handles root file");
    TEST_ASSERT(GetDirectoryW(NULL) == L"", "GetDirectoryW handles NULL");
}

//
// Test 11: GetExtensionW - pure string operation
//
void TestGetExtensionW()
{
    printf("\n=== Test: GetExtensionW ===\n");

    TEST_ASSERT(GetExtensionW(L"test.txt") == L"txt", "GetExtensionW extracts extension");
    TEST_ASSERT(GetExtensionW(L"C:\\Users\\file.doc") == L"doc", "GetExtensionW works with full path");
    TEST_ASSERT(GetExtensionW(L"archive.tar.gz") == L"gz", "GetExtensionW returns last extension");
    TEST_ASSERT(GetExtensionW(L".cvspass") == L"cvspass", "GetExtensionW handles dotfile as extension");
    TEST_ASSERT(GetExtensionW(L"noextension") == L"", "GetExtensionW handles no extension");
    TEST_ASSERT(GetExtensionW(L"C:\\folder.name\\file") == L"", "GetExtensionW ignores dot in directory");
    TEST_ASSERT(GetExtensionW(NULL) == L"", "GetExtensionW handles NULL");
}

//
// Test 12: GetShortPathW - filesystem operation
//
void TestGetShortPathW()
{
    printf("\n=== Test: GetShortPathW ===\n");

    std::wstring testFile = GetTestDir() + L"\\test.txt";
    std::wstring shortPath = GetShortPathW(testFile.c_str());

    // Short path should exist and not be empty for existing file
    TEST_ASSERT(!shortPath.empty(), "GetShortPathW returns non-empty for existing file");
    // Short path should contain the file
    TEST_ASSERT(shortPath.find(L"TEST") != std::wstring::npos ||
                shortPath.find(L"test") != std::wstring::npos,
                "GetShortPathW result contains filename");

    // Non-existent file should return empty
    TEST_ASSERT(GetShortPathW(L"C:\\nonexistent12345.txt") == L"", "GetShortPathW returns empty for non-existent");
    TEST_ASSERT(GetShortPathW(NULL) == L"", "GetShortPathW handles NULL");
}

//
// Test 13: Unicode filename operations - integration test
//
void TestUnicodeFilenames()
{
    printf("\n=== Test: Unicode filename operations ===\n");

    std::wstring testDir = GetTestDir();

    // Create file with Unicode name (Japanese characters)
    std::wstring unicodeFile = testDir + L"\\テスト.txt";
    HANDLE h = CreateFileW(unicodeFile.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    TEST_ASSERT(h != INVALID_HANDLE_VALUE, "Create file with Unicode name");
    if (h != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h);

        // Verify we can find it with GetFileInfoW
        SalFileInfo info = GetFileInfoW(unicodeFile.c_str());
        TEST_ASSERT(info.IsValid, "GetFileInfoW finds Unicode-named file");
        TEST_ASSERT(info.FileName == L"テスト.txt", "GetFileInfoW returns correct Unicode name");

        // Test rename with MoveFileW (what RenameFileInternalW uses)
        std::wstring renamedFile = testDir + L"\\テスト_renamed.txt";
        BOOL moved = MoveFileW(unicodeFile.c_str(), renamedFile.c_str());
        TEST_ASSERT(moved, "MoveFileW renames Unicode file");

        if (moved)
        {
            // Verify renamed file exists
            info = GetFileInfoW(renamedFile.c_str());
            TEST_ASSERT(info.IsValid, "Renamed Unicode file exists");
            DeleteFileW(renamedFile.c_str());
        }
        else
        {
            DeleteFileW(unicodeFile.c_str());
        }
    }

    // Create file with Chinese characters
    std::wstring chineseFile = testDir + L"\\中文文件.txt";
    h = CreateFileW(chineseFile.c_str(), GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    TEST_ASSERT(h != INVALID_HANDLE_VALUE, "Create file with Chinese name");
    if (h != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h);
        TEST_ASSERT(PathExistsW(chineseFile.c_str()), "PathExistsW finds Chinese-named file");
        DeleteFileW(chineseFile.c_str());
    }

    // Create file with emoji (if supported)
    std::wstring emojiFile = testDir + L"\\file_\U0001F600.txt";  // grinning face emoji
    h = CreateFileW(emojiFile.c_str(), GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h);
        TEST_ASSERT(PathExistsW(emojiFile.c_str()), "PathExistsW finds emoji-named file");
        DeleteFileW(emojiFile.c_str());
        printf("  [PASS] Emoji filename supported on this system\n");
        g_passed++;
    }
    else
    {
        printf("  [SKIP] Emoji filename not supported (expected on older Windows)\n");
    }
}

//
// Test 14: Path building with Unicode - integration test
//
void TestBuildPathW_Unicode()
{
    printf("\n=== Test: BuildPathW with Unicode ===\n");

    std::wstring result = BuildPathW(L"C:\\Users\\日本語", L"ファイル.txt");
    TEST_ASSERT(result == L"C:\\Users\\日本語\\ファイル.txt", "BuildPathW with Japanese path");

    result = BuildPathW(L"C:\\数据", L"文件.doc");
    TEST_ASSERT(result == L"C:\\数据\\文件.doc", "BuildPathW with Chinese path");

    // Test actual directory creation and file access
    std::wstring testDir = GetTestDir();
    std::wstring unicodeSubdir = testDir + L"\\サブフォルダ";

    BOOL created = CreateDirectoryW(unicodeSubdir.c_str(), NULL);
    TEST_ASSERT(created || GetLastError() == ERROR_ALREADY_EXISTS, "Create Unicode subdirectory");

    if (created || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        std::wstring filePath = BuildPathW(unicodeSubdir.c_str(), L"test.txt");
        HANDLE h = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        TEST_ASSERT(h != INVALID_HANDLE_VALUE, "Create file in Unicode subdirectory");
        if (h != INVALID_HANDLE_VALUE)
        {
            CloseHandle(h);
            TEST_ASSERT(IsDirectoryW(unicodeSubdir.c_str()), "IsDirectoryW on Unicode path");
            DeleteFileW(filePath.c_str());
        }
        RemoveDirectoryW(unicodeSubdir.c_str());
    }
}

//
// Test 15: ExpandEnvironmentW - environment variable expansion
//
void TestExpandEnvironmentW()
{
    printf("\n=== Test: ExpandEnvironmentW ===\n");

    // Test basic expansion - WINDIR should always exist
    std::wstring result = ExpandEnvironmentW(L"%WINDIR%");
    TEST_ASSERT(!result.empty() && result != L"%WINDIR%", "ExpandEnvironmentW expands WINDIR");
    TEST_ASSERT(result.find(L"Windows") != std::wstring::npos ||
                result.find(L"windows") != std::wstring::npos,
                "WINDIR contains 'Windows'");

    // Test expansion with surrounding text
    result = ExpandEnvironmentW(L"Path is: %WINDIR%\\System32");
    TEST_ASSERT(result.find(L"Path is:") != std::wstring::npos, "Prefix preserved");
    TEST_ASSERT(result.find(L"\\System32") != std::wstring::npos, "Suffix preserved");

    // Test multiple variables
    result = ExpandEnvironmentW(L"%SystemDrive%%HOMEPATH%");
    TEST_ASSERT(result.find(L'%') == std::wstring::npos, "All variables expanded");

    // Test non-existent variable (should remain as-is)
    result = ExpandEnvironmentW(L"%NONEXISTENT_VAR_12345%");
    TEST_ASSERT(result == L"%NONEXISTENT_VAR_12345%", "Non-existent var unchanged");

    // Test empty and NULL
    TEST_ASSERT(ExpandEnvironmentW(L"") == L"", "Empty string returns empty");
    TEST_ASSERT(ExpandEnvironmentW(NULL) == L"", "NULL returns empty");

    // Test plain string (no variables)
    result = ExpandEnvironmentW(L"C:\\Users\\Test");
    TEST_ASSERT(result == L"C:\\Users\\Test", "Plain string unchanged");
}

//
// Test 16: RemoveDoubleBackslashesW - backslash cleanup
//
void TestRemoveDoubleBackslashesW()
{
    printf("\n=== Test: RemoveDoubleBackslashesW ===\n");

    // Test basic double backslash removal
    std::wstring path = L"C:\\\\Users\\\\test.txt";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"C:\\Users\\test.txt", "Removes double backslashes");

    // Test triple+ backslashes
    path = L"C:\\\\\\\\foo\\\\\\bar";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"C:\\foo\\bar", "Removes triple+ backslashes");

    // Test UNC path preservation (\\server\share)
    path = L"\\\\server\\\\share\\\\file.txt";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"\\\\server\\share\\file.txt", "Preserves UNC prefix");

    // Test long path prefix preservation (\\?\)
    path = L"\\\\?\\C:\\\\Users\\\\test.txt";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"\\\\?\\C:\\Users\\test.txt", "Preserves long path prefix");

    // Test UNC long path preservation (\\?\UNC\)
    path = L"\\\\?\\UNC\\\\server\\\\share";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"\\\\?\\UNC\\server\\share", "Cleans after \\\\?\\UNC");

    // Test path without doubles (no change)
    path = L"C:\\Users\\test.txt";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"C:\\Users\\test.txt", "Normal path unchanged");

    // Test empty string
    path = L"";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"", "Empty string stays empty");

    // Test single backslash
    path = L"\\";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"\\", "Single backslash unchanged");
}

int main()
{
    printf("=====================================================\n");
    printf("  fsutil.h Test Suite\n");
    printf("  Testing UI-decoupled file system utilities\n");
    printf("=====================================================\n");

    // Setup
    printf("\nSetting up test directory...\n");
    if (!SetupTestDir())
    {
        printf("Failed to set up test directory!\n");
        return 1;
    }
    printf("Test directory: %ls\n", GetTestDir().c_str());

    // Run tests
    TestGetFileInfoW_File();
    TestGetFileInfoW_Directory();
    TestGetFileInfoW_NotFound();
    TestGetFileInfoW_Null();
    TestBuildPathW_Wide();
    TestBuildPathW_Ansi();
    TestPathExistsW();
    TestIsDirectoryW();
    TestGetFileNameW();
    TestGetDirectoryW();
    TestGetExtensionW();
    TestGetShortPathW();
    TestUnicodeFilenames();
    TestBuildPathW_Unicode();
    TestExpandEnvironmentW();
    TestRemoveDoubleBackslashesW();

    // Cleanup
    printf("\nCleaning up...\n");
    CleanupTestDir();

    // Results
    printf("\n=====================================================\n");
    printf("  Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("=====================================================\n");

    return g_failed > 0 ? 1 : 0;
}
