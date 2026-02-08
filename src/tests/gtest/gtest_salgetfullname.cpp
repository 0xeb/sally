// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//
// Google Test suite for SalGetFullNameW — wide path resolution.
//

#include <windows.h>
#include <string>
#include <gtest/gtest.h>

// Error IDs (must match salgetfullname_standalone.cpp)
#define IDS_PATHISINVALID 5501
#define IDS_SERVERNAMEMISSING 5502
#define IDS_SHARENAMEMISSING 5503
#define IDS_INVALIDDRIVE 5504
#define IDS_INCOMLETEFILENAME 5505
#define IDS_TOOLONGPATH 5506
#define IDS_EMPTYNAMENOTALLOWED 5507

#define SAL_MAX_LONG_PATH 32767

// DefaultDir stub — set up per test as needed
extern char DefaultDir['z' - 'a' + 1][SAL_MAX_LONG_PATH];

// Declaration of the function under test
BOOL SalGetFullNameW(std::wstring& name, int* errTextID = NULL, const wchar_t* curDir = NULL,
                     std::wstring* nextFocus = NULL, BOOL* callNethood = NULL,
                     BOOL allowRelPathWithSpaces = FALSE);

// ============================================================================
// Absolute paths — should pass through unchanged
// ============================================================================

TEST(SalGetFullNameW, AbsolutePath_Unchanged)
{
    std::wstring name = L"C:\\Windows\\System32";
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"C:\\Windows\\System32");
}

TEST(SalGetFullNameW, AbsolutePath_RootDrive)
{
    std::wstring name = L"C:\\";
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"C:\\");
}

TEST(SalGetFullNameW, AbsolutePath_TrailingBackslashRemoved)
{
    std::wstring name = L"C:\\Windows\\";
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"C:\\Windows");
}

TEST(SalGetFullNameW, AbsolutePath_LeadingSpacesTrimmed)
{
    std::wstring name = L"  C:\\Windows";
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"C:\\Windows");
}

// ============================================================================
// Relative paths with curDir
// ============================================================================

TEST(SalGetFullNameW, RelativePath_WithCurDir)
{
    std::wstring name = L"subdir\\file.txt";
    int err = 0;
    EXPECT_TRUE(SalGetFullNameW(name, &err, L"C:\\Projects"));
    EXPECT_EQ(name, L"C:\\Projects\\subdir\\file.txt");
}

TEST(SalGetFullNameW, RelativePath_CurDirWithTrailingBackslash)
{
    std::wstring name = L"file.txt";
    EXPECT_TRUE(SalGetFullNameW(name, NULL, L"C:\\Projects\\"));
    EXPECT_EQ(name, L"C:\\Projects\\file.txt");
}

TEST(SalGetFullNameW, RelativePath_NoCurDir_Fails)
{
    std::wstring name = L"file.txt";
    int err = 0;
    EXPECT_FALSE(SalGetFullNameW(name, &err));
    EXPECT_EQ(err, IDS_INCOMLETEFILENAME);
}

TEST(SalGetFullNameW, RelativePath_NextFocusSet)
{
    std::wstring name = L"myfile.txt";
    std::wstring nextFocus;
    EXPECT_TRUE(SalGetFullNameW(name, NULL, L"C:\\Dir", &nextFocus));
    EXPECT_EQ(nextFocus, L"myfile.txt");
}

TEST(SalGetFullNameW, RelativePath_WithBackslash_NoNextFocus)
{
    std::wstring name = L"sub\\file.txt";
    std::wstring nextFocus;
    EXPECT_TRUE(SalGetFullNameW(name, NULL, L"C:\\Dir", &nextFocus));
    // nextFocus should not be set when name contains backslash
    EXPECT_TRUE(nextFocus.empty());
}

// ============================================================================
// Backslash-rooted paths (\path from current drive)
// ============================================================================

TEST(SalGetFullNameW, BackslashRooted_FromDrive)
{
    std::wstring name = L"\\Windows\\System32";
    EXPECT_TRUE(SalGetFullNameW(name, NULL, L"C:\\SomeDir"));
    EXPECT_EQ(name, L"C:\\Windows\\System32");
}

TEST(SalGetFullNameW, BackslashRooted_FromUNC)
{
    std::wstring name = L"\\share2\\dir";
    EXPECT_TRUE(SalGetFullNameW(name, NULL, L"\\\\server\\share1\\subdir"));
    EXPECT_EQ(name, L"\\\\server\\share1\\share2\\dir");
}

// ============================================================================
// Drive-relative paths (c:path)
// ============================================================================

TEST(SalGetFullNameW, DriveRelative_UseCurDir)
{
    std::wstring name = L"C:subdir";
    EXPECT_TRUE(SalGetFullNameW(name, NULL, L"C:\\Projects"));
    EXPECT_EQ(name, L"C:\\Projects\\subdir");
}

TEST(SalGetFullNameW, DriveRelative_DifferentDrive_UsesDefaultDir)
{
    // Set up DefaultDir for drive D:
    strcpy(DefaultDir['d' - 'a'], "D:\\Work");
    std::wstring name = L"D:file.txt";
    EXPECT_TRUE(SalGetFullNameW(name, NULL, L"C:\\Projects"));
    EXPECT_EQ(name, L"D:\\Work\\file.txt");
}

TEST(SalGetFullNameW, DriveRelative_InvalidDrive)
{
    std::wstring name = L"1:path";
    int err = 0;
    EXPECT_FALSE(SalGetFullNameW(name, &err, L"C:\\Dir"));
    EXPECT_EQ(err, IDS_INVALIDDRIVE);
}

// ============================================================================
// Dot and double-dot elimination
// ============================================================================

TEST(SalGetFullNameW, DotElimination)
{
    std::wstring name = L"C:\\Windows\\.\\System32";
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"C:\\Windows\\System32");
}

TEST(SalGetFullNameW, DoubleDotElimination)
{
    std::wstring name = L"C:\\Windows\\System32\\..\\Fonts";
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"C:\\Windows\\Fonts");
}

TEST(SalGetFullNameW, MultipleDotDot)
{
    std::wstring name = L"C:\\a\\b\\c\\..\\..\\d";
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"C:\\a\\d");
}

// ============================================================================
// UNC paths
// ============================================================================

TEST(SalGetFullNameW, UNCPath_Valid)
{
    std::wstring name = L"\\\\server\\share\\dir\\file.txt";
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"\\\\server\\share\\dir\\file.txt");
}

TEST(SalGetFullNameW, UNCPath_TrailingBackslashRemoved)
{
    std::wstring name = L"\\\\server\\share\\dir\\";
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"\\\\server\\share\\dir");
}

TEST(SalGetFullNameW, UNCPath_MissingServer)
{
    std::wstring name = L"\\\\";
    int err = 0;
    BOOL callNethood = FALSE;
    EXPECT_FALSE(SalGetFullNameW(name, &err, NULL, NULL, &callNethood));
    EXPECT_EQ(err, IDS_SERVERNAMEMISSING);
    EXPECT_TRUE(callNethood);
}

TEST(SalGetFullNameW, UNCPath_MissingShare)
{
    std::wstring name = L"\\\\server";
    int err = 0;
    BOOL callNethood = FALSE;
    EXPECT_FALSE(SalGetFullNameW(name, &err, NULL, NULL, &callNethood));
    EXPECT_EQ(err, IDS_SHARENAMEMISSING);
    EXPECT_TRUE(callNethood);
}

TEST(SalGetFullNameW, UNCPath_InvalidPrefix)
{
    std::wstring name = L"\\\\?\\Volume{...}";
    int err = 0;
    EXPECT_FALSE(SalGetFullNameW(name, &err));
    EXPECT_EQ(err, IDS_PATHISINVALID);
}

// ============================================================================
// Empty / error cases
// ============================================================================

TEST(SalGetFullNameW, EmptyName_Fails)
{
    std::wstring name = L"";
    int err = 0;
    EXPECT_FALSE(SalGetFullNameW(name, &err));
    EXPECT_EQ(err, IDS_EMPTYNAMENOTALLOWED);
}

TEST(SalGetFullNameW, WhitespaceOnly_Fails)
{
    std::wstring name = L"   ";
    int err = 0;
    EXPECT_FALSE(SalGetFullNameW(name, &err));
    EXPECT_EQ(err, IDS_EMPTYNAMENOTALLOWED);
}

// ============================================================================
// Long paths (> MAX_PATH)
// ============================================================================

TEST(SalGetFullNameW, LongAbsolutePath_Preserved)
{
    std::wstring longDir(300, L'a');
    std::wstring name = L"C:\\" + longDir + L"\\file.txt";
    EXPECT_GT(name.length(), 260u);
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"C:\\" + longDir + L"\\file.txt");
}

TEST(SalGetFullNameW, LongRelativePath_Resolved)
{
    std::wstring longDir(300, L'b');
    std::wstring name = longDir;
    std::wstring curDir = L"C:\\Base";
    EXPECT_TRUE(SalGetFullNameW(name, NULL, curDir.c_str()));
    EXPECT_EQ(name, L"C:\\Base\\" + longDir);
    EXPECT_GT(name.length(), 260u);
}

TEST(SalGetFullNameW, LongUNCPath_Preserved)
{
    std::wstring longDir(300, L'c');
    std::wstring name = L"\\\\server\\share\\" + longDir;
    EXPECT_GT(name.length(), 260u);
    EXPECT_TRUE(SalGetFullNameW(name));
    EXPECT_EQ(name, L"\\\\server\\share\\" + longDir);
}
