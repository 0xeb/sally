// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//
// Tests for fsutil.h - Pure string operations (no filesystem access)
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

//
// BuildPathW tests
//
void TestBuildPathW()
{
    printf("\n=== Test: BuildPathW ===\n");

    std::wstring result = BuildPathW(L"C:\\Users", L"test.txt");
    TEST_ASSERT(result == L"C:\\Users\\test.txt", "Adds backslash");

    result = BuildPathW(L"C:\\Users\\", L"test.txt");
    TEST_ASSERT(result == L"C:\\Users\\test.txt", "No double backslash");

    result = BuildPathW(L"", L"test.txt");
    TEST_ASSERT(result == L"test.txt", "Empty directory");

    result = BuildPathW(L"C:\\Users", L"");
    TEST_ASSERT(result == L"C:\\Users\\", "Empty filename");

    result = BuildPathW(NULL, L"test.txt");
    TEST_ASSERT(result == L"test.txt", "NULL directory");

    // ANSI overload
    result = BuildPathW("C:\\Users", "test.txt");
    TEST_ASSERT(result == L"C:\\Users\\test.txt", "ANSI works");

    // Unicode paths
    result = BuildPathW(L"C:\\Users\\日本語", L"ファイル.txt");
    TEST_ASSERT(result == L"C:\\Users\\日本語\\ファイル.txt", "Japanese path");
}

//
// GetFileNameW tests
//
void TestGetFileNameW()
{
    printf("\n=== Test: GetFileNameW ===\n");

    TEST_ASSERT(GetFileNameW(L"C:\\Users\\test.txt") == L"test.txt", "Extracts filename");
    TEST_ASSERT(GetFileNameW(L"C:\\Users\\Dir\\file.doc") == L"file.doc", "Deep path");
    TEST_ASSERT(GetFileNameW(L"test.txt") == L"test.txt", "No directory");
    TEST_ASSERT(GetFileNameW(L"C:\\") == L"", "Root path");
    TEST_ASSERT(GetFileNameW(L"C:\\Users\\") == L"", "Trailing backslash");
    TEST_ASSERT(GetFileNameW(NULL) == L"", "NULL");
    TEST_ASSERT(GetFileNameW(L"") == L"", "Empty");
}

//
// GetDirectoryW tests
//
void TestGetDirectoryW()
{
    printf("\n=== Test: GetDirectoryW ===\n");

    TEST_ASSERT(GetDirectoryW(L"C:\\Users\\test.txt") == L"C:\\Users", "Extracts directory");
    TEST_ASSERT(GetDirectoryW(L"C:\\Users\\Dir\\file.doc") == L"C:\\Users\\Dir", "Deep path");
    TEST_ASSERT(GetDirectoryW(L"test.txt") == L"", "No directory");
    TEST_ASSERT(GetDirectoryW(L"C:\\file.txt") == L"C:", "Root file");
    TEST_ASSERT(GetDirectoryW(NULL) == L"", "NULL");
}

//
// GetExtensionW tests
//
void TestGetExtensionW()
{
    printf("\n=== Test: GetExtensionW ===\n");

    TEST_ASSERT(GetExtensionW(L"test.txt") == L"txt", "Basic extension");
    TEST_ASSERT(GetExtensionW(L"C:\\Users\\file.doc") == L"doc", "With path");
    TEST_ASSERT(GetExtensionW(L"archive.tar.gz") == L"gz", "Last extension");
    TEST_ASSERT(GetExtensionW(L".cvspass") == L"cvspass", "Dotfile");
    TEST_ASSERT(GetExtensionW(L"noextension") == L"", "No extension");
    TEST_ASSERT(GetExtensionW(L"C:\\folder.name\\file") == L"", "Dir dot ignored");
    TEST_ASSERT(GetExtensionW(NULL) == L"", "NULL");
}

//
// GetRootPathW tests
//
void TestGetRootPathW()
{
    printf("\n=== Test: GetRootPathW ===\n");

    TEST_ASSERT(GetRootPathW(L"C:\\Users\\test.txt") == L"C:\\", "Local path root");
    TEST_ASSERT(GetRootPathW(L"D:\\") == L"D:\\", "Root drive");
    TEST_ASSERT(GetRootPathW(L"E:\\Deep\\Nested\\Path") == L"E:\\", "Deep path");
    TEST_ASSERT(GetRootPathW(L"\\\\server\\share\\folder") == L"\\\\server\\share\\", "UNC root");
    TEST_ASSERT(GetRootPathW(L"\\\\server\\share") == L"\\\\server\\share\\", "UNC share");
    TEST_ASSERT(GetRootPathW(NULL) == L"", "NULL");
    TEST_ASSERT(GetRootPathW(L"") == L"", "Empty");
}

//
// IsUNCRootPathW tests
//
void TestIsUNCRootPathW()
{
    printf("\n=== Test: IsUNCRootPathW ===\n");

    TEST_ASSERT(IsUNCRootPathW(L"\\\\server\\share"), "UNC share is root");
    TEST_ASSERT(IsUNCRootPathW(L"\\\\server\\share\\"), "UNC with slash is root");
    TEST_ASSERT(IsUNCRootPathW(L"\\\\server"), "Server-only is root");
    TEST_ASSERT(!IsUNCRootPathW(L"\\\\server\\share\\folder"), "With folder not root");
    TEST_ASSERT(!IsUNCRootPathW(L"C:\\"), "Local not UNC");
    TEST_ASSERT(!IsUNCRootPathW(NULL), "NULL");
}

//
// IsUNCPathW tests
//
void TestIsUNCPathW()
{
    printf("\n=== Test: IsUNCPathW ===\n");

    TEST_ASSERT(IsUNCPathW(L"\\\\server\\share"), "UNC detected");
    TEST_ASSERT(IsUNCPathW(L"\\\\server"), "Server-only detected");
    TEST_ASSERT(!IsUNCPathW(L"C:\\Users"), "Local not UNC");
    TEST_ASSERT(!IsUNCPathW(L"\\single"), "Single backslash not UNC");
    TEST_ASSERT(!IsUNCPathW(NULL), "NULL");
}

//
// Trailing backslash tests
//
void TestTrailingBackslash()
{
    printf("\n=== Test: Trailing backslash ===\n");

    TEST_ASSERT(HasTrailingBackslashW(L"C:\\Users\\"), "Has trailing");
    TEST_ASSERT(!HasTrailingBackslashW(L"C:\\Users"), "No trailing");
    TEST_ASSERT(!HasTrailingBackslashW(NULL), "NULL");

    std::wstring path = L"C:\\Users\\";
    RemoveTrailingBackslashW(path);
    TEST_ASSERT(path == L"C:\\Users", "Removes trailing");

    path = L"C:\\Users";
    RemoveTrailingBackslashW(path);
    TEST_ASSERT(path == L"C:\\Users", "No change if none");

    path = L"C:\\Users";
    AddTrailingBackslashW(path);
    TEST_ASSERT(path == L"C:\\Users\\", "Adds trailing");

    path = L"C:\\Users\\";
    AddTrailingBackslashW(path);
    TEST_ASSERT(path == L"C:\\Users\\", "No double");
}

//
// RemoveDoubleBackslashesW tests
//
void TestRemoveDoubleBackslashesW()
{
    printf("\n=== Test: RemoveDoubleBackslashesW ===\n");

    std::wstring path = L"C:\\\\Users\\\\test.txt";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"C:\\Users\\test.txt", "Removes doubles");

    path = L"C:\\\\\\\\foo\\\\\\bar";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"C:\\foo\\bar", "Removes triple+");

    path = L"\\\\server\\\\share";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"\\\\server\\share", "Preserves UNC prefix");

    path = L"\\\\?\\C:\\\\Users";
    RemoveDoubleBackslashesW(path);
    TEST_ASSERT(path == L"\\\\?\\C:\\Users", "Preserves long path prefix");
}

//
// ExpandEnvironmentW tests
//
void TestExpandEnvironmentW()
{
    printf("\n=== Test: ExpandEnvironmentW ===\n");

    std::wstring result = ExpandEnvironmentW(L"%WINDIR%");
    TEST_ASSERT(!result.empty() && result != L"%WINDIR%", "Expands WINDIR");

    result = ExpandEnvironmentW(L"%NONEXISTENT_VAR_12345%");
    TEST_ASSERT(result == L"%NONEXISTENT_VAR_12345%", "Non-existent unchanged");

    TEST_ASSERT(ExpandEnvironmentW(NULL) == L"", "NULL");
    TEST_ASSERT(ExpandEnvironmentW(L"plain") == L"plain", "Plain unchanged");
}

//
// Extension manipulation tests
//
void TestExtensionHelpers()
{
    printf("\n=== Test: Extension helpers ===\n");

    std::wstring path = L"test.txt";
    RemoveExtensionW(path);
    TEST_ASSERT(path == L"test", "RemoveExtensionW basic");

    path = L"C:\\Users\\file.doc";
    RemoveExtensionW(path);
    TEST_ASSERT(path == L"C:\\Users\\file", "RemoveExtensionW with path");

    path = L"archive.tar.gz";
    RemoveExtensionW(path);
    TEST_ASSERT(path == L"archive.tar", "RemoveExtensionW double ext");

    path = L"test.txt";
    SetExtensionW(path, L".doc");
    TEST_ASSERT(path == L"test.doc", "SetExtensionW replaces");

    path = L"test";
    SetExtensionW(path, L".doc");
    TEST_ASSERT(path == L"test.doc", "SetExtensionW adds");

    TEST_ASSERT(GetFileNameWithoutExtensionW(L"C:\\Users\\test.txt") == L"test", "GetFileNameWithoutExtW");
    TEST_ASSERT(GetFileNameWithoutExtensionW(L"noext") == L"noext", "No ext");
}

//
// GetParentPathW tests
//
void TestGetParentPathW()
{
    printf("\n=== Test: GetParentPathW ===\n");

    TEST_ASSERT(GetParentPathW(L"C:\\Users\\Test") == L"C:\\Users", "Basic parent");
    TEST_ASSERT(GetParentPathW(L"C:\\Users\\Test\\") == L"C:\\Users", "With trailing");
    TEST_ASSERT(GetParentPathW(L"C:\\Users") == L"C:\\", "Parent is root");
    TEST_ASSERT(GetParentPathW(L"C:\\") == L"", "Root no parent");
    TEST_ASSERT(GetParentPathW(L"\\\\server\\share\\folder") == L"\\\\server\\share", "UNC parent");
    TEST_ASSERT(GetParentPathW(L"\\\\server\\share") == L"", "UNC root no parent");
    TEST_ASSERT(GetParentPathW(NULL) == L"", "NULL");
}

//
// IsTheSamePathW tests
//
void TestIsTheSamePathW()
{
    printf("\n=== Test: IsTheSamePathW ===\n");

    TEST_ASSERT(IsTheSamePathW(L"C:\\Users", L"C:\\Users"), "Exact match");
    TEST_ASSERT(IsTheSamePathW(L"C:\\Users", L"c:\\users"), "Case insensitive");
    TEST_ASSERT(IsTheSamePathW(L"C:\\Users", L"C:\\Users\\"), "Trailing backslash");
    TEST_ASSERT(IsTheSamePathW(L"C:\\Users\\", L"C:\\Users"), "Reverse trailing");
    TEST_ASSERT(!IsTheSamePathW(L"C:\\Users", L"C:\\Temp"), "Different paths");
    TEST_ASSERT(!IsTheSamePathW(L"C:\\Users", L"C:\\Users\\Test"), "Prefix only");
    TEST_ASSERT(IsTheSamePathW(NULL, NULL), "Both NULL");
    TEST_ASSERT(!IsTheSamePathW(L"C:\\", NULL), "One NULL");
}

//
// PathStartsWithW tests
//
void TestPathStartsWithW()
{
    printf("\n=== Test: PathStartsWithW ===\n");

    TEST_ASSERT(PathStartsWithW(L"C:\\Users\\Test", L"C:\\Users"), "Basic prefix");
    TEST_ASSERT(PathStartsWithW(L"C:\\Users\\Test", L"c:\\users"), "Case insensitive");
    TEST_ASSERT(PathStartsWithW(L"C:\\Users\\Test", L"C:\\Users\\"), "Prefix with slash");
    TEST_ASSERT(PathStartsWithW(L"C:\\Users", L"C:\\Users"), "Exact match");
    TEST_ASSERT(!PathStartsWithW(L"C:\\Users", L"C:\\Users\\Test"), "Longer prefix");
    TEST_ASSERT(!PathStartsWithW(L"C:\\Usernames", L"C:\\Users"), "Partial match");
    TEST_ASSERT(PathStartsWithW(L"C:\\Users", L""), "Empty prefix");
    TEST_ASSERT(!PathStartsWithW(NULL, L"C:\\"), "NULL path");
}

int main()
{
    printf("=====================================================\n");
    printf("  fsutil String Operations Test Suite\n");
    printf("=====================================================\n");

    TestBuildPathW();
    TestGetFileNameW();
    TestGetDirectoryW();
    TestGetExtensionW();
    TestGetRootPathW();
    TestIsUNCRootPathW();
    TestIsUNCPathW();
    TestTrailingBackslash();
    TestRemoveDoubleBackslashesW();
    TestExpandEnvironmentW();
    TestExtensionHelpers();
    TestGetParentPathW();
    TestIsTheSamePathW();
    TestPathStartsWithW();

    printf("\n=====================================================\n");
    printf("  Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("=====================================================\n");

    return g_failed > 0 ? 1 : 0;
}
