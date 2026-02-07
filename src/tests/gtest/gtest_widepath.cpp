// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Google Test suite for wide path helper functions and CPathBuffer classes
//
//****************************************************************************

#include <gtest/gtest.h>
#include <windows.h>
#include <string>

#include "../common/widepath.h"

// Declarations for functions in salpath_standalone.cpp
void SalPathAppendW(std::wstring& path, const wchar_t* name);
void SalPathAddBackslashW(std::wstring& path);
void SalPathRemoveBackslashW(std::wstring& path);
void SalPathStripPathW(std::wstring& path);
const wchar_t* SalPathFindFileNameW(const wchar_t* path);
void SalPathRemoveExtensionW(std::wstring& path);
bool SalPathAddExtensionW(std::wstring& path, const wchar_t* extension);
bool SalPathRenameExtensionW(std::wstring& path, const wchar_t* extension);
bool CutDirectoryW(std::wstring& path, std::wstring* cutDir = nullptr);

// ============================================================================
// SalPathAppendW tests
// ============================================================================

TEST(SalPathAppendW, NormalAppend)
{
    std::wstring path = L"C:\\foo";
    SalPathAppendW(path, L"bar");
    EXPECT_EQ(path, L"C:\\foo\\bar");
}

TEST(SalPathAppendW, PathWithTrailingBackslash)
{
    std::wstring path = L"C:\\foo\\";
    SalPathAppendW(path, L"bar");
    EXPECT_EQ(path, L"C:\\foo\\bar");
}

TEST(SalPathAppendW, NameWithLeadingBackslash)
{
    std::wstring path = L"C:\\foo";
    SalPathAppendW(path, L"\\bar");
    EXPECT_EQ(path, L"C:\\foo\\bar");
}

TEST(SalPathAppendW, BothBackslashes)
{
    std::wstring path = L"C:\\foo\\";
    SalPathAppendW(path, L"\\bar");
    EXPECT_EQ(path, L"C:\\foo\\bar");
}

TEST(SalPathAppendW, EmptyPath)
{
    std::wstring path;
    SalPathAppendW(path, L"bar");
    EXPECT_EQ(path, L"bar");
}

TEST(SalPathAppendW, EmptyName)
{
    std::wstring path = L"C:\\foo";
    SalPathAppendW(path, L"");
    EXPECT_EQ(path, L"C:\\foo");
}

TEST(SalPathAppendW, NullName)
{
    std::wstring path = L"C:\\foo";
    SalPathAppendW(path, nullptr);
    EXPECT_EQ(path, L"C:\\foo");
}

TEST(SalPathAppendW, RootPath)
{
    std::wstring path = L"C:\\";
    SalPathAppendW(path, L"dir");
    EXPECT_EQ(path, L"C:\\dir");
}

TEST(SalPathAppendW, UNCPath)
{
    std::wstring path = L"\\\\server\\share";
    SalPathAppendW(path, L"folder");
    EXPECT_EQ(path, L"\\\\server\\share\\folder");
}

TEST(SalPathAppendW, UnicodeChars)
{
    std::wstring path = L"C:\\\x6587\x4EF6"; // Chinese characters
    SalPathAppendW(path, L"\x30C6\x30B9\x30C8"); // Japanese katakana
    EXPECT_EQ(path, L"C:\\\x6587\x4EF6\\\x30C6\x30B9\x30C8");
}

// ============================================================================
// SalPathAddBackslashW tests
// ============================================================================

TEST(SalPathAddBackslashW, AddsBackslash)
{
    std::wstring path = L"C:\\foo";
    SalPathAddBackslashW(path);
    EXPECT_EQ(path, L"C:\\foo\\");
}

TEST(SalPathAddBackslashW, AlreadyHasBackslash)
{
    std::wstring path = L"C:\\foo\\";
    SalPathAddBackslashW(path);
    EXPECT_EQ(path, L"C:\\foo\\");
}

TEST(SalPathAddBackslashW, EmptyPath)
{
    std::wstring path;
    SalPathAddBackslashW(path);
    EXPECT_EQ(path, L"");
}

// ============================================================================
// SalPathRemoveBackslashW tests
// ============================================================================

TEST(SalPathRemoveBackslashW, RemovesBackslash)
{
    std::wstring path = L"C:\\foo\\";
    SalPathRemoveBackslashW(path);
    EXPECT_EQ(path, L"C:\\foo");
}

TEST(SalPathRemoveBackslashW, NoTrailingBackslash)
{
    std::wstring path = L"C:\\foo";
    SalPathRemoveBackslashW(path);
    EXPECT_EQ(path, L"C:\\foo");
}

TEST(SalPathRemoveBackslashW, EmptyPath)
{
    std::wstring path;
    SalPathRemoveBackslashW(path);
    EXPECT_EQ(path, L"");
}

// ============================================================================
// SalPathStripPathW tests
// ============================================================================

TEST(SalPathStripPathW, NormalPath)
{
    std::wstring path = L"C:\\foo\\bar.txt";
    SalPathStripPathW(path);
    EXPECT_EQ(path, L"bar.txt");
}

TEST(SalPathStripPathW, FileNameOnly)
{
    std::wstring path = L"bar.txt";
    SalPathStripPathW(path);
    EXPECT_EQ(path, L"bar.txt");
}

TEST(SalPathStripPathW, EmptyPath)
{
    std::wstring path;
    SalPathStripPathW(path);
    EXPECT_EQ(path, L"");
}

TEST(SalPathStripPathW, UNCPath)
{
    std::wstring path = L"\\\\server\\share\\dir\\file.txt";
    SalPathStripPathW(path);
    EXPECT_EQ(path, L"file.txt");
}

// ============================================================================
// SalPathFindFileNameW tests
// ============================================================================

TEST(SalPathFindFileNameW, NormalPath)
{
    const wchar_t* result = SalPathFindFileNameW(L"C:\\foo\\bar.txt");
    EXPECT_STREQ(result, L"bar.txt");
}

TEST(SalPathFindFileNameW, FileNameOnly)
{
    const wchar_t* result = SalPathFindFileNameW(L"bar.txt");
    EXPECT_STREQ(result, L"bar.txt");
}

TEST(SalPathFindFileNameW, NullPtr)
{
    const wchar_t* result = SalPathFindFileNameW(nullptr);
    EXPECT_EQ(result, nullptr);
}

TEST(SalPathFindFileNameW, RootPath)
{
    const wchar_t* result = SalPathFindFileNameW(L"C:\\");
    EXPECT_STREQ(result, L"");
}

TEST(SalPathFindFileNameW, UnicodeFileName)
{
    const wchar_t* result = SalPathFindFileNameW(L"C:\\dir\\\x6587\x4EF6.txt");
    EXPECT_STREQ(result, L"\x6587\x4EF6.txt");
}

// ============================================================================
// SalPathRemoveExtensionW tests
// ============================================================================

TEST(SalPathRemoveExtensionW, NormalExtension)
{
    std::wstring path = L"C:\\foo\\bar.txt";
    SalPathRemoveExtensionW(path);
    EXPECT_EQ(path, L"C:\\foo\\bar");
}

TEST(SalPathRemoveExtensionW, NoExtension)
{
    std::wstring path = L"C:\\foo\\bar";
    SalPathRemoveExtensionW(path);
    EXPECT_EQ(path, L"C:\\foo\\bar");
}

TEST(SalPathRemoveExtensionW, MultipleDotsRemovesLast)
{
    std::wstring path = L"C:\\foo\\bar.tar.gz";
    SalPathRemoveExtensionW(path);
    EXPECT_EQ(path, L"C:\\foo\\bar.tar");
}

TEST(SalPathRemoveExtensionW, DotInDirectory)
{
    std::wstring path = L"C:\\foo.bar\\baz";
    SalPathRemoveExtensionW(path);
    // No extension in filename, so dot in dir doesn't count
    EXPECT_EQ(path, L"C:\\foo.bar\\baz");
}

TEST(SalPathRemoveExtensionW, EmptyPath)
{
    std::wstring path;
    SalPathRemoveExtensionW(path);
    EXPECT_EQ(path, L"");
}

// ============================================================================
// SalPathAddExtensionW tests
// ============================================================================

TEST(SalPathAddExtensionW, AddsExtension)
{
    std::wstring path = L"C:\\foo\\bar";
    bool result = SalPathAddExtensionW(path, L".txt");
    EXPECT_TRUE(result);
    EXPECT_EQ(path, L"C:\\foo\\bar.txt");
}

TEST(SalPathAddExtensionW, AlreadyHasExtension)
{
    std::wstring path = L"C:\\foo\\bar.txt";
    bool result = SalPathAddExtensionW(path, L".bak");
    EXPECT_TRUE(result);
    // Should not add second extension - existing one preserved
    EXPECT_EQ(path, L"C:\\foo\\bar.txt");
}

TEST(SalPathAddExtensionW, NullExtension)
{
    std::wstring path = L"C:\\foo\\bar";
    bool result = SalPathAddExtensionW(path, nullptr);
    EXPECT_FALSE(result);
    EXPECT_EQ(path, L"C:\\foo\\bar");
}

TEST(SalPathAddExtensionW, DotInDirNoExtInFile)
{
    std::wstring path = L"C:\\foo.bar\\baz";
    bool result = SalPathAddExtensionW(path, L".txt");
    EXPECT_TRUE(result);
    EXPECT_EQ(path, L"C:\\foo.bar\\baz.txt");
}

// ============================================================================
// SalPathRenameExtensionW tests
// ============================================================================

TEST(SalPathRenameExtensionW, RenamesExtension)
{
    std::wstring path = L"C:\\foo\\bar.txt";
    bool result = SalPathRenameExtensionW(path, L".bak");
    EXPECT_TRUE(result);
    EXPECT_EQ(path, L"C:\\foo\\bar.bak");
}

TEST(SalPathRenameExtensionW, NoExistingExtension)
{
    std::wstring path = L"C:\\foo\\bar";
    bool result = SalPathRenameExtensionW(path, L".txt");
    EXPECT_TRUE(result);
    EXPECT_EQ(path, L"C:\\foo\\bar.txt");
}

TEST(SalPathRenameExtensionW, NullExtension)
{
    std::wstring path = L"C:\\foo\\bar.txt";
    bool result = SalPathRenameExtensionW(path, nullptr);
    EXPECT_FALSE(result);
    EXPECT_EQ(path, L"C:\\foo\\bar.txt");
}

TEST(SalPathRenameExtensionW, MultipleDotsReplacesLast)
{
    std::wstring path = L"C:\\foo\\bar.tar.gz";
    bool result = SalPathRenameExtensionW(path, L".xz");
    EXPECT_TRUE(result);
    EXPECT_EQ(path, L"C:\\foo\\bar.tar.xz");
}

// ============================================================================
// CutDirectoryW tests
// ============================================================================

TEST(CutDirectoryW, NormalPath)
{
    std::wstring path = L"C:\\dir1\\dir2";
    std::wstring cutDir;
    EXPECT_TRUE(CutDirectoryW(path, &cutDir));
    EXPECT_EQ(path, L"C:\\dir1");
    EXPECT_EQ(cutDir, L"dir2");
}

TEST(CutDirectoryW, TrailingBackslash)
{
    std::wstring path = L"C:\\dir1\\dir2\\";
    std::wstring cutDir;
    EXPECT_TRUE(CutDirectoryW(path, &cutDir));
    EXPECT_EQ(path, L"C:\\dir1");
    EXPECT_EQ(cutDir, L"dir2");
}

TEST(CutDirectoryW, CutToRoot)
{
    std::wstring path = L"C:\\somedir";
    std::wstring cutDir;
    EXPECT_TRUE(CutDirectoryW(path, &cutDir));
    EXPECT_EQ(path, L"C:\\");
    EXPECT_EQ(cutDir, L"somedir");
}

TEST(CutDirectoryW, RootCannotShorten)
{
    std::wstring path = L"C:\\";
    EXPECT_FALSE(CutDirectoryW(path));
}

TEST(CutDirectoryW, EmptyPath)
{
    std::wstring path;
    EXPECT_FALSE(CutDirectoryW(path));
}

TEST(CutDirectoryW, NoCutDirParam)
{
    std::wstring path = L"C:\\dir1\\dir2";
    EXPECT_TRUE(CutDirectoryW(path));
    EXPECT_EQ(path, L"C:\\dir1");
}

TEST(CutDirectoryW, UNCRootCannotShorten)
{
    std::wstring path = L"\\\\server\\share";
    EXPECT_FALSE(CutDirectoryW(path));
}

TEST(CutDirectoryW, UNCSubdir)
{
    std::wstring path = L"\\\\server\\share\\subdir";
    std::wstring cutDir;
    EXPECT_TRUE(CutDirectoryW(path, &cutDir));
    EXPECT_EQ(path, L"\\\\server\\share");
    EXPECT_EQ(cutDir, L"subdir");
}

TEST(CutDirectoryW, NoBackslash)
{
    std::wstring path = L"filename";
    EXPECT_FALSE(CutDirectoryW(path));
}

// ============================================================================
// Long path tests
// ============================================================================

TEST(SalPathAppendW, LongPath)
{
    // Build a path >260 chars
    std::wstring path = L"C:\\";
    for (int i = 0; i < 30; i++)
        path += L"longdirname\\";
    path.pop_back(); // remove trailing backslash
    EXPECT_GT(path.length(), 260u);

    SalPathAppendW(path, L"file.txt");
    EXPECT_GT(path.length(), 260u);
    // Verify it ends with the appended name
    EXPECT_TRUE(path.length() > 8);
    EXPECT_EQ(path.substr(path.length() - 8), L"file.txt");
}

TEST(SalPathFindFileNameW, LongPath)
{
    std::wstring path = L"C:\\";
    for (int i = 0; i < 30; i++)
        path += L"longdirname\\";
    path += L"myfile.dat";
    EXPECT_GT(path.length(), 260u);
    const wchar_t* result = SalPathFindFileNameW(path.c_str());
    EXPECT_STREQ(result, L"myfile.dat");
}

// ============================================================================
// CPathBuffer tests
// ============================================================================

TEST(CPathBuffer, DefaultConstruction)
{
    CPathBuffer buf;
    EXPECT_TRUE(buf.IsValid());
    EXPECT_EQ(buf.Size(), SAL_MAX_LONG_PATH);
    EXPECT_STREQ(buf.Get(), "");
}

TEST(CPathBuffer, ConstructWithPath)
{
    CPathBuffer buf("C:\\test\\path");
    EXPECT_TRUE(buf.IsValid());
    EXPECT_STREQ(buf.Get(), "C:\\test\\path");
}

TEST(CPathBuffer, ImplicitConversion)
{
    CPathBuffer buf("hello");
    const char* ptr = buf; // implicit conversion
    EXPECT_STREQ(ptr, "hello");
}

TEST(CPathBuffer, SubscriptOperator)
{
    CPathBuffer buf("ABCD");
    char* raw = buf.Get();
    EXPECT_EQ(raw[0], 'A');
    EXPECT_EQ(raw[1], 'B');
    EXPECT_EQ(raw[3], 'D');
}

TEST(CPathBuffer, StrcpyInto)
{
    CPathBuffer buf;
    strcpy(buf.Get(), "C:\\some\\path");
    EXPECT_STREQ(buf.Get(), "C:\\some\\path");
}

// ============================================================================
// CWidePathBuffer tests
// ============================================================================

TEST(CWidePathBuffer, DefaultConstruction)
{
    CWidePathBuffer buf;
    EXPECT_TRUE(buf.IsValid());
    EXPECT_EQ(buf.Size(), SAL_MAX_LONG_PATH);
    EXPECT_STREQ(buf.Get(), L"");
}

TEST(CWidePathBuffer, ConstructWithPath)
{
    CWidePathBuffer buf(L"C:\\test\\path");
    EXPECT_TRUE(buf.IsValid());
    EXPECT_STREQ(buf.Get(), L"C:\\test\\path");
}

TEST(CWidePathBuffer, ImplicitConversion)
{
    CWidePathBuffer buf(L"hello");
    const wchar_t* ptr = buf; // implicit conversion
    EXPECT_STREQ(ptr, L"hello");
}

TEST(CWidePathBuffer, WcscpyInto)
{
    CWidePathBuffer buf;
    wcscpy(buf.Get(), L"C:\\wide\\path");
    EXPECT_STREQ(buf.Get(), L"C:\\wide\\path");
}

TEST(CWidePathBuffer, AppendWide)
{
    CWidePathBuffer buf(L"C:\\dir");
    EXPECT_TRUE(buf.Append(L"subdir"));
    EXPECT_STREQ(buf.Get(), L"C:\\dir\\subdir");
}

TEST(CWidePathBuffer, AppendToEmpty)
{
    CWidePathBuffer buf;
    EXPECT_TRUE(buf.Append(L"first"));
    EXPECT_STREQ(buf.Get(), L"first");
}

TEST(CWidePathBuffer, AppendWithTrailingBackslash)
{
    CWidePathBuffer buf(L"C:\\dir\\");
    EXPECT_TRUE(buf.Append(L"subdir"));
    // Append skips adding backslash when path already ends with one
    EXPECT_STREQ(buf.Get(), L"C:\\dir\\subdir");
}

TEST(CWidePathBuffer, AppendAnsi)
{
    CWidePathBuffer buf(L"C:\\dir");
    EXPECT_TRUE(buf.Append("subdir"));
    EXPECT_STREQ(buf.Get(), L"C:\\dir\\subdir");
}

TEST(CWidePathBuffer, AppendNull)
{
    CWidePathBuffer buf(L"C:\\dir");
    EXPECT_FALSE(buf.Append((const wchar_t*)nullptr));
    EXPECT_STREQ(buf.Get(), L"C:\\dir");
}

TEST(CWidePathBuffer, UnicodeContent)
{
    CWidePathBuffer buf(L"C:\\\x6587\x4EF6\\\x30C6\x30B9\x30C8");
    EXPECT_TRUE(buf.IsValid());
    EXPECT_STREQ(buf.Get(), L"C:\\\x6587\x4EF6\\\x30C6\x30B9\x30C8");
}
