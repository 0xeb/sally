// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//
// Console test for long path support
//

#include <windows.h>
#include <stdio.h>
#include <string.h>

// Include widepath directly (we'll compile it into the test)
#include "../common/widepath.h"

// Test result tracking
static int g_passed = 0;
static int g_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do \
    { \
        if (cond) \
        { \
            printf("  [PASS] %s\n", msg); \
            g_passed++; \
        } \
        else \
        { \
            printf("  [FAIL] %s (error=%lu)\n", msg, GetLastError()); \
            g_failed++; \
        } \
    } while (0)

// Build a long path by repeating directory names
void BuildLongPath(char* buffer, size_t bufSize, const char* basePath, int targetLen)
{
    strcpy_s(buffer, bufSize, basePath);

    const char* segment = "\\VeryLongDirectoryNameForTesting";
    size_t segLen = strlen(segment);
    int idx = 0;

    while ((int)strlen(buffer) < targetLen && strlen(buffer) + segLen + 10 < bufSize)
    {
        char seg[100];
        sprintf_s(seg, "%s%d", segment, idx++);
        strcat_s(buffer, bufSize, seg);
    }
}

// Create directories recursively
BOOL CreateDirectoriesRecursive(const char* path)
{
    char temp[2048];
    strcpy_s(temp, path);

    // Use our long path wrapper
    for (char* p = temp + 3; *p; p++) // Skip "C:\"
    {
        if (*p == '\\')
        {
            *p = '\0';
            if (SalLPGetFileAttributes(temp) == INVALID_FILE_ATTRIBUTES)
            {
                if (!SalLPCreateDirectory(temp, NULL))
                {
                    DWORD err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS)
                    {
                        printf("    Failed to create: %s (len=%zu, err=%lu)\n", temp, strlen(temp), err);
                        return FALSE;
                    }
                }
            }
            *p = '\\';
        }
    }

    // Create final directory
    if (SalLPGetFileAttributes(temp) == INVALID_FILE_ATTRIBUTES)
    {
        if (!SalLPCreateDirectory(temp, NULL))
        {
            DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS)
            {
                printf("    Failed to create final: %s (len=%zu, err=%lu)\n", temp, strlen(temp), err);
                return FALSE;
            }
        }
    }
    return TRUE;
}

// Delete directories recursively (for cleanup)
void DeleteDirectoriesRecursive(const char* basePath)
{
    char searchPath[2048];
    sprintf_s(searchPath, "%s\\*", basePath);

    WIN32_FIND_DATAA findData;
    HANDLE hFind = SalLPFindFirstFileA(searchPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
                continue;

            char fullPath[2048];
            sprintf_s(fullPath, "%s\\%s", basePath, findData.cFileName);

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                DeleteDirectoriesRecursive(fullPath);
                SalLPRemoveDirectory(fullPath);
            }
            else
            {
                SalLPDeleteFile(fullPath);
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    SalLPRemoveDirectory(basePath);
}

void TestShortPath()
{
    printf("\n=== Test 1: Short Path (baseline) ===\n");

    const char* testDir = "C:\\Temp\\SalTest_Short";
    const char* testFile = "C:\\Temp\\SalTest_Short\\test.txt";

    // Cleanup
    SalLPDeleteFile(testFile);
    SalLPRemoveDirectory(testDir);

    // Create directory
    BOOL created = SalLPCreateDirectory(testDir, NULL);
    TEST_ASSERT(created || GetLastError() == ERROR_ALREADY_EXISTS, "Create short directory");

    // Create file
    HANDLE h = SalLPCreateFile(testFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    TEST_ASSERT(h != INVALID_HANDLE_VALUE, "Create short file");
    if (h != INVALID_HANDLE_VALUE)
    {
        const char* content = "Test content";
        DWORD written;
        WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
        CloseHandle(h);
    }

    // Get attributes
    DWORD attrs = SalLPGetFileAttributes(testFile);
    TEST_ASSERT(attrs != INVALID_FILE_ATTRIBUTES, "Get short file attributes");

    // Find file
    WIN32_FIND_DATAA findData;
    HANDLE hFind = SalLPFindFirstFileA(testFile, &findData);
    TEST_ASSERT(hFind != INVALID_HANDLE_VALUE, "FindFirstFile short path");
    if (hFind != INVALID_HANDLE_VALUE)
        FindClose(hFind);

    // Cleanup
    SalLPDeleteFile(testFile);
    SalLPRemoveDirectory(testDir);
}

void TestLongPath()
{
    printf("\n=== Test 2: Long Path (>260 chars) ===\n");

    char longDir[2048];
    char longFile[2048];

    BuildLongPath(longDir, sizeof(longDir), "C:\\Temp\\SalTest_Long", 300);
    sprintf_s(longFile, "%s\\test.txt", longDir);

    printf("  Directory path length: %zu chars\n", strlen(longDir));
    printf("  File path length: %zu chars\n", strlen(longFile));

    // Cleanup first
    DeleteDirectoriesRecursive("C:\\Temp\\SalTest_Long");

    // Create directory structure
    BOOL created = CreateDirectoriesRecursive(longDir);
    TEST_ASSERT(created, "Create long directory structure");

    // Get attributes of long directory
    DWORD attrs = SalLPGetFileAttributes(longDir);
    TEST_ASSERT(attrs != INVALID_FILE_ATTRIBUTES, "Get long directory attributes");
    TEST_ASSERT(attrs & FILE_ATTRIBUTE_DIRECTORY, "Long path is directory");

    // Create file in long path
    HANDLE h = SalLPCreateFile(longFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    TEST_ASSERT(h != INVALID_HANDLE_VALUE, "Create file in long path");
    if (h != INVALID_HANDLE_VALUE)
    {
        const char* content = "Long path test content";
        DWORD written;
        WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
        CloseHandle(h);
    }

    // Get file attributes
    attrs = SalLPGetFileAttributes(longFile);
    TEST_ASSERT(attrs != INVALID_FILE_ATTRIBUTES, "Get long file attributes");

    // FindFirstFile on long path
    WIN32_FIND_DATAA findData;
    HANDLE hFind = SalLPFindFirstFileA(longFile, &findData);
    TEST_ASSERT(hFind != INVALID_HANDLE_VALUE, "FindFirstFile long path");
    if (hFind != INVALID_HANDLE_VALUE)
    {
        printf("    Found file: %s\n", findData.cFileName);
        FindClose(hFind);
    }

    // Read file back
    h = SalLPCreateFile(longFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    TEST_ASSERT(h != INVALID_HANDLE_VALUE, "Open file in long path for reading");
    if (h != INVALID_HANDLE_VALUE)
    {
        char buffer[100];
        DWORD read;
        ReadFile(h, buffer, sizeof(buffer) - 1, &read, NULL);
        buffer[read] = '\0';
        printf("    File content: %s\n", buffer);
        CloseHandle(h);
    }

    // Delete file
    BOOL deleted = SalLPDeleteFile(longFile);
    TEST_ASSERT(deleted, "Delete file in long path");

    // Cleanup
    DeleteDirectoriesRecursive("C:\\Temp\\SalTest_Long");
}

void TestVeryLongPath()
{
    printf("\n=== Test 3: Very Long Path (>500 chars) ===\n");

    char longDir[2048];
    char longFile[2048];

    BuildLongPath(longDir, sizeof(longDir), "C:\\Temp\\SalTest_VeryLong", 500);
    sprintf_s(longFile, "%s\\test.txt", longDir);

    printf("  Directory path length: %zu chars\n", strlen(longDir));
    printf("  File path length: %zu chars\n", strlen(longFile));

    // Cleanup first
    DeleteDirectoriesRecursive("C:\\Temp\\SalTest_VeryLong");

    // Create directory structure
    BOOL created = CreateDirectoriesRecursive(longDir);
    TEST_ASSERT(created, "Create very long directory structure");

    // Create file
    HANDLE h = SalLPCreateFile(longFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    TEST_ASSERT(h != INVALID_HANDLE_VALUE, "Create file in very long path");
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);

    // FindFirstFile with wildcard in long path
    char searchPath[2048];
    sprintf_s(searchPath, "%s\\*", longDir);
    WIN32_FIND_DATAA findData;
    HANDLE hFind = SalLPFindFirstFileA(searchPath, &findData);
    TEST_ASSERT(hFind != INVALID_HANDLE_VALUE, "FindFirstFile wildcard in very long path");
    if (hFind != INVALID_HANDLE_VALUE)
    {
        int fileCount = 0;
        do
        {
            if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0)
            {
                printf("    Found: %s\n", findData.cFileName);
                fileCount++;
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
        TEST_ASSERT(fileCount == 1, "Found expected file count");
    }

    // Cleanup
    DeleteDirectoriesRecursive("C:\\Temp\\SalTest_VeryLong");
}

void TestWidePath()
{
    printf("\n=== Test 4: SalWidePath class ===\n");

    // Test short path (no prefix needed)
    {
        SalWidePath wp("C:\\Temp\\test.txt");
        TEST_ASSERT(wp.IsValid(), "SalWidePath valid for short path");
        TEST_ASSERT(!wp.HasLongPathPrefix(), "No prefix for short path");
    }

    // Test long path (prefix needed)
    {
        char longPath[500];
        BuildLongPath(longPath, sizeof(longPath), "C:\\Temp\\SalTest", 300);

        SalWidePath wp(longPath);
        TEST_ASSERT(wp.IsValid(), "SalWidePath valid for long path");
        TEST_ASSERT(wp.HasLongPathPrefix(), "Prefix added for long path");

        // Check the prefix was added
        const wchar_t* widePath = wp.Get();
        TEST_ASSERT(widePath != NULL && wcsncmp(widePath, L"\\\\?\\", 4) == 0, "Wide path has \\\\?\\ prefix");
    }
}

int main()
{
    printf("===========================================\n");
    printf("  Salamander Long Path Support Test Suite\n");
    printf("===========================================\n");

    TestShortPath();
    TestLongPath();
    TestVeryLongPath();
    TestWidePath();

    printf("\n===========================================\n");
    printf("  Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("===========================================\n");

    return g_failed > 0 ? 1 : 0;
}
