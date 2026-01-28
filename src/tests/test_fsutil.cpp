// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//
// Tests for fsutil.h - Filesystem operations
// Uses real filesystem operations in a temp directory
//
// Note: Pure string operation tests are in test_fsutil_strings.cpp
//

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#define SAL_LONG_PATH_THRESHOLD 240
#define SAL_MAX_LONG_PATH 32767

#include "../common/fsutil.h"

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

std::wstring GetTestDir()
{
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    std::wstring result = temp;
    result += L"salamander_fsutil_test";
    return result;
}

bool SetupTestDir()
{
    std::wstring testDir = GetTestDir();

    RemoveDirectoryW((testDir + L"\\subdir").c_str());
    DeleteFileW((testDir + L"\\test.txt").c_str());
    RemoveDirectoryW(testDir.c_str());

    if (!CreateDirectoryW(testDir.c_str(), NULL))
    {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }

    std::wstring testFile = testDir + L"\\test.txt";
    HANDLE h = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    const char* content = "Test content for fsutil";
    DWORD written;
    WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
    CloseHandle(h);

    CreateDirectoryW((testDir + L"\\subdir").c_str(), NULL);
    return true;
}

void CleanupTestDir()
{
    std::wstring testDir = GetTestDir();
    DeleteFileW((testDir + L"\\test.txt").c_str());
    RemoveDirectoryW((testDir + L"\\subdir").c_str());
    RemoveDirectoryW(testDir.c_str());
}

//
// GetFileInfoW tests
//
void TestGetFileInfoW_File()
{
    printf("\n=== Test: GetFileInfoW on file ===\n");

    std::wstring testFile = GetTestDir() + L"\\test.txt";
    SalFileInfo info = GetFileInfoW(testFile.c_str());

    TEST_ASSERT(info.IsValid, "Returns valid for existing file");
    TEST_ASSERT(!(info.Attributes & FILE_ATTRIBUTE_DIRECTORY), "File is not directory");
    TEST_ASSERT(info.FileSize > 0, "File has content");
    TEST_ASSERT(info.FileName == L"test.txt", "FileName correct");
}

void TestGetFileInfoW_Directory()
{
    printf("\n=== Test: GetFileInfoW on directory ===\n");

    std::wstring testDir = GetTestDir() + L"\\subdir";
    SalFileInfo info = GetFileInfoW(testDir.c_str());

    TEST_ASSERT(info.IsValid, "Returns valid for directory");
    TEST_ASSERT(info.Attributes & FILE_ATTRIBUTE_DIRECTORY, "Has directory attribute");
    TEST_ASSERT(info.FileName == L"subdir", "Directory name correct");
}

void TestGetFileInfoW_NotFound()
{
    printf("\n=== Test: GetFileInfoW on non-existent ===\n");

    std::wstring noFile = GetTestDir() + L"\\does_not_exist.txt";
    SalFileInfo info = GetFileInfoW(noFile.c_str());

    TEST_ASSERT(!info.IsValid, "Returns invalid for non-existent");
    TEST_ASSERT(info.LastError == ERROR_FILE_NOT_FOUND ||
                info.LastError == ERROR_PATH_NOT_FOUND, "Correct error code");
}

void TestGetFileInfoW_Null()
{
    printf("\n=== Test: GetFileInfoW with NULL ===\n");

    SalFileInfo info = GetFileInfoW(NULL);
    TEST_ASSERT(!info.IsValid, "Returns invalid for NULL");
    TEST_ASSERT(info.LastError == ERROR_INVALID_PARAMETER, "INVALID_PARAMETER error");
}

//
// PathExistsW and IsDirectoryW tests
//
void TestPathExistsW()
{
    printf("\n=== Test: PathExistsW ===\n");

    std::wstring testDir = GetTestDir();
    std::wstring testFile = testDir + L"\\test.txt";

    TEST_ASSERT(PathExistsW(testDir.c_str()), "Directory exists");
    TEST_ASSERT(PathExistsW(testFile.c_str()), "File exists");
    TEST_ASSERT(!PathExistsW((testDir + L"\\nope.txt").c_str()), "Non-existent");
    TEST_ASSERT(!PathExistsW(NULL), "NULL");
}

void TestIsDirectoryW()
{
    printf("\n=== Test: IsDirectoryW ===\n");

    std::wstring testDir = GetTestDir();
    std::wstring testFile = testDir + L"\\test.txt";

    TEST_ASSERT(IsDirectoryW(testDir.c_str()), "Directory is directory");
    TEST_ASSERT(IsDirectoryW((testDir + L"\\subdir").c_str()), "Subdir is directory");
    TEST_ASSERT(!IsDirectoryW(testFile.c_str()), "File is not directory");
    TEST_ASSERT(!IsDirectoryW((testDir + L"\\nope").c_str()), "Non-existent");
}

//
// GetShortPathW tests
//
void TestGetShortPathW()
{
    printf("\n=== Test: GetShortPathW ===\n");

    std::wstring testFile = GetTestDir() + L"\\test.txt";
    std::wstring shortPath = GetShortPathW(testFile.c_str());

    TEST_ASSERT(!shortPath.empty(), "Returns non-empty for existing");
    TEST_ASSERT(shortPath.find(L"TEST") != std::wstring::npos ||
                shortPath.find(L"test") != std::wstring::npos, "Contains filename");
    TEST_ASSERT(GetShortPathW(L"C:\\nonexistent12345.txt") == L"", "Empty for non-existent");
    TEST_ASSERT(GetShortPathW(NULL) == L"", "NULL");
}

//
// Unicode integration tests - actual file operations
//
void TestUnicodeFilenames()
{
    printf("\n=== Test: Unicode filename operations ===\n");

    std::wstring testDir = GetTestDir();

    // Japanese filename
    std::wstring unicodeFile = testDir + L"\\テスト.txt";
    HANDLE h = CreateFileW(unicodeFile.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    TEST_ASSERT(h != INVALID_HANDLE_VALUE, "Create Japanese filename");
    if (h != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h);
        SalFileInfo info = GetFileInfoW(unicodeFile.c_str());
        TEST_ASSERT(info.IsValid, "GetFileInfoW finds Japanese file");
        TEST_ASSERT(info.FileName == L"テスト.txt", "Correct Japanese name");

        std::wstring renamedFile = testDir + L"\\テスト_renamed.txt";
        BOOL moved = MoveFileW(unicodeFile.c_str(), renamedFile.c_str());
        TEST_ASSERT(moved, "MoveFileW renames Japanese file");
        if (moved)
        {
            info = GetFileInfoW(renamedFile.c_str());
            TEST_ASSERT(info.IsValid, "Renamed file exists");
            DeleteFileW(renamedFile.c_str());
        }
        else
        {
            DeleteFileW(unicodeFile.c_str());
        }
    }

    // Chinese filename
    std::wstring chineseFile = testDir + L"\\中文文件.txt";
    h = CreateFileW(chineseFile.c_str(), GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    TEST_ASSERT(h != INVALID_HANDLE_VALUE, "Create Chinese filename");
    if (h != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h);
        TEST_ASSERT(PathExistsW(chineseFile.c_str()), "PathExistsW finds Chinese file");
        DeleteFileW(chineseFile.c_str());
    }

    // Emoji filename (if supported)
    std::wstring emojiFile = testDir + L"\\file_\U0001F600.txt";
    h = CreateFileW(emojiFile.c_str(), GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h);
        TEST_ASSERT(PathExistsW(emojiFile.c_str()), "Emoji filename works");
        DeleteFileW(emojiFile.c_str());
        printf("  [PASS] Emoji filename supported\n");
        g_passed++;
    }
    else
    {
        printf("  [SKIP] Emoji not supported on this system\n");
    }
}

void TestUnicodeDirectories()
{
    printf("\n=== Test: Unicode directory operations ===\n");

    std::wstring testDir = GetTestDir();
    std::wstring unicodeSubdir = testDir + L"\\サブフォルダ";

    BOOL created = CreateDirectoryW(unicodeSubdir.c_str(), NULL);
    TEST_ASSERT(created || GetLastError() == ERROR_ALREADY_EXISTS, "Create Unicode subdir");

    if (created || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        std::wstring filePath = BuildPathW(unicodeSubdir.c_str(), L"test.txt");
        HANDLE h = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        TEST_ASSERT(h != INVALID_HANDLE_VALUE, "Create file in Unicode dir");
        if (h != INVALID_HANDLE_VALUE)
        {
            CloseHandle(h);
            TEST_ASSERT(IsDirectoryW(unicodeSubdir.c_str()), "IsDirectoryW on Unicode path");
            DeleteFileW(filePath.c_str());
        }
        RemoveDirectoryW(unicodeSubdir.c_str());
    }
}

int main()
{
    printf("=====================================================\n");
    printf("  fsutil Filesystem Operations Test Suite\n");
    printf("=====================================================\n");

    printf("\nSetting up test directory...\n");
    if (!SetupTestDir())
    {
        printf("Failed to set up test directory!\n");
        return 1;
    }
    printf("Test directory: %ls\n", GetTestDir().c_str());

    TestGetFileInfoW_File();
    TestGetFileInfoW_Directory();
    TestGetFileInfoW_NotFound();
    TestGetFileInfoW_Null();
    TestPathExistsW();
    TestIsDirectoryW();
    TestGetShortPathW();
    TestUnicodeFilenames();
    TestUnicodeDirectories();

    printf("\nCleaning up...\n");
    CleanupTestDir();

    printf("\n=====================================================\n");
    printf("  Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("=====================================================\n");

    return g_failed > 0 ? 1 : 0;
}
