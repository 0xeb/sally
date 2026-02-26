// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Google Test suite for wide fsutil helper functions (pure string operations)
//
//****************************************************************************

#include <gtest/gtest.h>
#include <windows.h>
#include <string>

#include "../../common/fsutil.h"

// ============================================================================
// BuildPathW (wide) tests
// ============================================================================

TEST(BuildPathW_Wide, NormalJoin)
{
    EXPECT_EQ(BuildPathW(L"C:\\Users", L"test.txt"), L"C:\\Users\\test.txt");
}

TEST(BuildPathW_Wide, DirWithTrailingBackslash)
{
    EXPECT_EQ(BuildPathW(L"C:\\Users\\", L"test.txt"), L"C:\\Users\\test.txt");
}

TEST(BuildPathW_Wide, EmptyFileName)
{
    EXPECT_EQ(BuildPathW(L"C:\\Users", L""), L"C:\\Users\\");
}

TEST(BuildPathW_Wide, EmptyDirectory)
{
    EXPECT_EQ(BuildPathW(L"", L"test.txt"), L"test.txt");
}

TEST(BuildPathW_Wide, BothEmpty)
{
    EXPECT_EQ(BuildPathW(L"", L""), L"");
}

TEST(BuildPathW_Wide, NullDirectory)
{
    EXPECT_EQ(BuildPathW((const wchar_t*)NULL, L"test.txt"), L"test.txt");
}

TEST(BuildPathW_Wide, NullFileName)
{
    EXPECT_EQ(BuildPathW(L"C:\\Users", (const wchar_t*)NULL), L"C:\\Users");
}

TEST(BuildPathW_Wide, BothNull)
{
    EXPECT_EQ(BuildPathW((const wchar_t*)NULL, (const wchar_t*)NULL), L"");
}

TEST(BuildPathW_Wide, DeepPath)
{
    EXPECT_EQ(BuildPathW(L"C:\\A\\B\\C\\D", L"file.txt"), L"C:\\A\\B\\C\\D\\file.txt");
}

TEST(BuildPathW_Wide, UNCPath)
{
    EXPECT_EQ(BuildPathW(L"\\\\server\\share", L"folder"), L"\\\\server\\share\\folder");
}

TEST(BuildPathW_Wide, UnicodeJapanese)
{
    EXPECT_EQ(BuildPathW(L"C:\\Users\\\x65E5\x672C\x8A9E", L"\x30D5\x30A1\x30A4\x30EB.txt"),
              L"C:\\Users\\\x65E5\x672C\x8A9E\\\x30D5\x30A1\x30A4\x30EB.txt");
}

// ============================================================================
// BuildPathW (ANSI) tests
// ============================================================================

TEST(BuildPathW_ANSI, NormalJoin)
{
    EXPECT_EQ(BuildPathW("C:\\Users", "test.txt"), L"C:\\Users\\test.txt");
}

TEST(BuildPathW_ANSI, DirWithTrailingBackslash)
{
    EXPECT_EQ(BuildPathW("C:\\Users\\", "test.txt"), L"C:\\Users\\test.txt");
}

TEST(BuildPathW_ANSI, EmptyParts)
{
    EXPECT_EQ(BuildPathW("", "test.txt"), L"test.txt");
}

TEST(BuildPathW_ANSI, NullDirectory)
{
    EXPECT_EQ(BuildPathW((const char*)NULL, "test.txt"), L"test.txt");
}

// ============================================================================
// GetFileNameW tests
// ============================================================================

TEST(GetFileNameW, NormalPath)
{
    EXPECT_EQ(GetFileNameW(L"C:\\Users\\test.txt"), L"test.txt");
}

TEST(GetFileNameW, DeepPath)
{
    EXPECT_EQ(GetFileNameW(L"C:\\Users\\Dir\\Sub\\file.doc"), L"file.doc");
}

TEST(GetFileNameW, NoBackslash)
{
    EXPECT_EQ(GetFileNameW(L"test.txt"), L"test.txt");
}

TEST(GetFileNameW, RootPath)
{
    EXPECT_EQ(GetFileNameW(L"C:\\"), L"");
}

TEST(GetFileNameW, TrailingBackslash)
{
    EXPECT_EQ(GetFileNameW(L"C:\\Users\\"), L"");
}

TEST(GetFileNameW, UNCPath)
{
    EXPECT_EQ(GetFileNameW(L"\\\\server\\share\\file.txt"), L"file.txt");
}

TEST(GetFileNameW, Null)
{
    EXPECT_EQ(GetFileNameW(NULL), L"");
}

TEST(GetFileNameW, Empty)
{
    EXPECT_EQ(GetFileNameW(L""), L"");
}

// ============================================================================
// GetDirectoryW tests
// ============================================================================

TEST(GetDirectoryW, NormalPath)
{
    EXPECT_EQ(GetDirectoryW(L"C:\\Users\\test.txt"), L"C:\\Users");
}

TEST(GetDirectoryW, DeepPath)
{
    EXPECT_EQ(GetDirectoryW(L"C:\\Users\\Dir\\file.doc"), L"C:\\Users\\Dir");
}

TEST(GetDirectoryW, NoBackslash)
{
    EXPECT_EQ(GetDirectoryW(L"test.txt"), L"");
}

TEST(GetDirectoryW, RootFile)
{
    EXPECT_EQ(GetDirectoryW(L"C:\\file.txt"), L"C:");
}

TEST(GetDirectoryW, UNCPath)
{
    EXPECT_EQ(GetDirectoryW(L"\\\\server\\share\\file.txt"), L"\\\\server\\share");
}

TEST(GetDirectoryW, Null)
{
    EXPECT_EQ(GetDirectoryW(NULL), L"");
}

TEST(GetDirectoryW, Empty)
{
    EXPECT_EQ(GetDirectoryW(L""), L"");
}

// ============================================================================
// GetExtensionW tests
// ============================================================================

TEST(GetExtensionW, BasicExtension)
{
    EXPECT_EQ(GetExtensionW(L"test.txt"), L"txt");
}

TEST(GetExtensionW, WithPath)
{
    EXPECT_EQ(GetExtensionW(L"C:\\Users\\file.doc"), L"doc");
}

TEST(GetExtensionW, MultipleDots)
{
    EXPECT_EQ(GetExtensionW(L"archive.tar.gz"), L"gz");
}

TEST(GetExtensionW, DotFile)
{
    EXPECT_EQ(GetExtensionW(L".cvspass"), L"cvspass");
}

TEST(GetExtensionW, NoExtension)
{
    EXPECT_EQ(GetExtensionW(L"noextension"), L"");
}

TEST(GetExtensionW, DirDotIgnored)
{
    EXPECT_EQ(GetExtensionW(L"C:\\folder.name\\file"), L"");
}

TEST(GetExtensionW, DirDotWithFileExt)
{
    EXPECT_EQ(GetExtensionW(L"C:\\folder.name\\file.txt"), L"txt");
}

TEST(GetExtensionW, Null)
{
    EXPECT_EQ(GetExtensionW(NULL), L"");
}

TEST(GetExtensionW, Empty)
{
    EXPECT_EQ(GetExtensionW(L""), L"");
}

TEST(GetExtensionW, TrailingDot)
{
    EXPECT_EQ(GetExtensionW(L"file."), L"");
}

// ============================================================================
// RemoveDoubleBackslashesW tests
// ============================================================================

TEST(RemoveDoubleBackslashesW, DoubleBackslashes)
{
    std::wstring path = L"C:\\\\Users\\\\test.txt";
    RemoveDoubleBackslashesW(path);
    EXPECT_EQ(path, L"C:\\Users\\test.txt");
}

TEST(RemoveDoubleBackslashesW, TripleAndMore)
{
    std::wstring path = L"C:\\\\\\\\foo\\\\\\bar";
    RemoveDoubleBackslashesW(path);
    EXPECT_EQ(path, L"C:\\foo\\bar");
}

TEST(RemoveDoubleBackslashesW, PreservesUNCPrefix)
{
    std::wstring path = L"\\\\server\\\\share";
    RemoveDoubleBackslashesW(path);
    EXPECT_EQ(path, L"\\\\server\\share");
}

TEST(RemoveDoubleBackslashesW, PreservesLongPathPrefix)
{
    std::wstring path = L"\\\\?\\C:\\\\Users";
    RemoveDoubleBackslashesW(path);
    EXPECT_EQ(path, L"\\\\?\\C:\\Users");
}

TEST(RemoveDoubleBackslashesW, NoDoubles)
{
    std::wstring path = L"C:\\Users\\test.txt";
    RemoveDoubleBackslashesW(path);
    EXPECT_EQ(path, L"C:\\Users\\test.txt");
}

TEST(RemoveDoubleBackslashesW, Empty)
{
    std::wstring path;
    RemoveDoubleBackslashesW(path);
    EXPECT_EQ(path, L"");
}

TEST(RemoveDoubleBackslashesW, SingleBackslash)
{
    std::wstring path = L"\\";
    RemoveDoubleBackslashesW(path);
    EXPECT_EQ(path, L"\\");
}

// ============================================================================
// GetRootPathW tests
// ============================================================================

TEST(GetRootPathW, LocalPath)
{
    EXPECT_EQ(GetRootPathW(L"C:\\Users\\test.txt"), L"C:\\");
}

TEST(GetRootPathW, RootDrive)
{
    EXPECT_EQ(GetRootPathW(L"D:\\"), L"D:\\");
}

TEST(GetRootPathW, DeepPath)
{
    EXPECT_EQ(GetRootPathW(L"E:\\Deep\\Nested\\Path"), L"E:\\");
}

TEST(GetRootPathW, UNCWithFolder)
{
    EXPECT_EQ(GetRootPathW(L"\\\\server\\share\\folder"), L"\\\\server\\share\\");
}

TEST(GetRootPathW, UNCShareOnly)
{
    EXPECT_EQ(GetRootPathW(L"\\\\server\\share"), L"\\\\server\\share\\");
}

TEST(GetRootPathW, Null)
{
    EXPECT_EQ(GetRootPathW(NULL), L"");
}

TEST(GetRootPathW, Empty)
{
    EXPECT_EQ(GetRootPathW(L""), L"");
}

TEST(GetRootPathW, UNCDeepNested)
{
    EXPECT_EQ(GetRootPathW(L"\\\\server\\share\\a\\b\\c"), L"\\\\server\\share\\");
}

// ============================================================================
// IsUNCRootPathW tests
// ============================================================================

TEST(IsUNCRootPathW, ShareIsRoot)
{
    EXPECT_TRUE(IsUNCRootPathW(L"\\\\server\\share"));
}

TEST(IsUNCRootPathW, ShareWithTrailingSlash)
{
    EXPECT_TRUE(IsUNCRootPathW(L"\\\\server\\share\\"));
}

TEST(IsUNCRootPathW, ServerOnly)
{
    EXPECT_TRUE(IsUNCRootPathW(L"\\\\server"));
}

TEST(IsUNCRootPathW, WithFolder)
{
    EXPECT_FALSE(IsUNCRootPathW(L"\\\\server\\share\\folder"));
}

TEST(IsUNCRootPathW, LocalPath)
{
    EXPECT_FALSE(IsUNCRootPathW(L"C:\\"));
}

TEST(IsUNCRootPathW, Null)
{
    EXPECT_FALSE(IsUNCRootPathW(NULL));
}

TEST(IsUNCRootPathW, Empty)
{
    EXPECT_FALSE(IsUNCRootPathW(L""));
}

TEST(IsUNCRootPathW, DeepSubdirectory)
{
    EXPECT_FALSE(IsUNCRootPathW(L"\\\\server\\share\\a\\b"));
}

// ============================================================================
// IsUNCPathW tests
// ============================================================================

TEST(IsUNCPathW, UNCShare)
{
    EXPECT_TRUE(IsUNCPathW(L"\\\\server\\share"));
}

TEST(IsUNCPathW, ServerOnly)
{
    EXPECT_TRUE(IsUNCPathW(L"\\\\server"));
}

TEST(IsUNCPathW, LocalPath)
{
    EXPECT_FALSE(IsUNCPathW(L"C:\\Users"));
}

TEST(IsUNCPathW, SingleBackslash)
{
    EXPECT_FALSE(IsUNCPathW(L"\\single"));
}

TEST(IsUNCPathW, Null)
{
    EXPECT_FALSE(IsUNCPathW(NULL));
}

TEST(IsUNCPathW, Empty)
{
    EXPECT_FALSE(IsUNCPathW(L""));
}

// ============================================================================
// HasTrailingBackslashW tests
// ============================================================================

TEST(HasTrailingBackslashW, HasTrailing)
{
    EXPECT_TRUE(HasTrailingBackslashW(L"C:\\Users\\"));
}

TEST(HasTrailingBackslashW, NoTrailing)
{
    EXPECT_FALSE(HasTrailingBackslashW(L"C:\\Users"));
}

TEST(HasTrailingBackslashW, RootDrive)
{
    EXPECT_TRUE(HasTrailingBackslashW(L"C:\\"));
}

TEST(HasTrailingBackslashW, JustBackslash)
{
    EXPECT_TRUE(HasTrailingBackslashW(L"\\"));
}

TEST(HasTrailingBackslashW, Null)
{
    EXPECT_FALSE(HasTrailingBackslashW(NULL));
}

TEST(HasTrailingBackslashW, Empty)
{
    EXPECT_FALSE(HasTrailingBackslashW(L""));
}

// ============================================================================
// RemoveTrailingBackslashW tests
// ============================================================================

TEST(RemoveTrailingBackslashW, RemovesTrailing)
{
    std::wstring path = L"C:\\Users\\";
    RemoveTrailingBackslashW(path);
    EXPECT_EQ(path, L"C:\\Users");
}

TEST(RemoveTrailingBackslashW, NoTrailingNoChange)
{
    std::wstring path = L"C:\\Users";
    RemoveTrailingBackslashW(path);
    EXPECT_EQ(path, L"C:\\Users");
}

TEST(RemoveTrailingBackslashW, Empty)
{
    std::wstring path;
    RemoveTrailingBackslashW(path);
    EXPECT_EQ(path, L"");
}

TEST(RemoveTrailingBackslashW, SingleBackslash)
{
    std::wstring path = L"\\";
    RemoveTrailingBackslashW(path);
    EXPECT_EQ(path, L"");
}

// ============================================================================
// AddTrailingBackslashW tests
// ============================================================================

TEST(AddTrailingBackslashW, AddsTrailing)
{
    std::wstring path = L"C:\\Users";
    AddTrailingBackslashW(path);
    EXPECT_EQ(path, L"C:\\Users\\");
}

TEST(AddTrailingBackslashW, AlreadyHasNoDouble)
{
    std::wstring path = L"C:\\Users\\";
    AddTrailingBackslashW(path);
    EXPECT_EQ(path, L"C:\\Users\\");
}

TEST(AddTrailingBackslashW, Empty)
{
    std::wstring path;
    AddTrailingBackslashW(path);
    EXPECT_EQ(path, L"");
}

TEST(AddTrailingBackslashW, UNCPath)
{
    std::wstring path = L"\\\\server\\share";
    AddTrailingBackslashW(path);
    EXPECT_EQ(path, L"\\\\server\\share\\");
}

// ============================================================================
// RemoveExtensionW tests
// ============================================================================

TEST(RemoveExtensionW, BasicExtension)
{
    std::wstring path = L"test.txt";
    RemoveExtensionW(path);
    EXPECT_EQ(path, L"test");
}

TEST(RemoveExtensionW, WithPath)
{
    std::wstring path = L"C:\\Users\\file.doc";
    RemoveExtensionW(path);
    EXPECT_EQ(path, L"C:\\Users\\file");
}

TEST(RemoveExtensionW, DoubleExtension)
{
    std::wstring path = L"archive.tar.gz";
    RemoveExtensionW(path);
    EXPECT_EQ(path, L"archive.tar");
}

TEST(RemoveExtensionW, NoExtension)
{
    std::wstring path = L"noext";
    RemoveExtensionW(path);
    EXPECT_EQ(path, L"noext");
}

TEST(RemoveExtensionW, DotFile)
{
    std::wstring path = L".hidden";
    RemoveExtensionW(path);
    EXPECT_EQ(path, L"");
}

TEST(RemoveExtensionW, DirDotNoFileExt)
{
    std::wstring path = L"C:\\folder.name\\file";
    RemoveExtensionW(path);
    EXPECT_EQ(path, L"C:\\folder.name\\file");
}

TEST(RemoveExtensionW, Empty)
{
    std::wstring path;
    RemoveExtensionW(path);
    EXPECT_EQ(path, L"");
}

// ============================================================================
// SetExtensionW tests
// ============================================================================

TEST(SetExtensionW, ReplacesExtension)
{
    std::wstring path = L"test.txt";
    SetExtensionW(path, L".doc");
    EXPECT_EQ(path, L"test.doc");
}

TEST(SetExtensionW, AddsExtension)
{
    std::wstring path = L"test";
    SetExtensionW(path, L".doc");
    EXPECT_EQ(path, L"test.doc");
}

TEST(SetExtensionW, WithPath)
{
    std::wstring path = L"C:\\Users\\file.txt";
    SetExtensionW(path, L".bak");
    EXPECT_EQ(path, L"C:\\Users\\file.bak");
}

TEST(SetExtensionW, NullExtension)
{
    std::wstring path = L"test.txt";
    SetExtensionW(path, NULL);
    EXPECT_EQ(path, L"test");
}

TEST(SetExtensionW, EmptyExtension)
{
    std::wstring path = L"test.txt";
    SetExtensionW(path, L"");
    EXPECT_EQ(path, L"test");
}

TEST(SetExtensionW, EmptyPath)
{
    std::wstring path;
    SetExtensionW(path, L".txt");
    EXPECT_EQ(path, L"");
}

// ============================================================================
// GetFileNameWithoutExtensionW tests
// ============================================================================

TEST(GetFileNameWithoutExtensionW, NormalPath)
{
    EXPECT_EQ(GetFileNameWithoutExtensionW(L"C:\\Users\\test.txt"), L"test");
}

TEST(GetFileNameWithoutExtensionW, NoExtension)
{
    EXPECT_EQ(GetFileNameWithoutExtensionW(L"noext"), L"noext");
}

TEST(GetFileNameWithoutExtensionW, MultipleDots)
{
    EXPECT_EQ(GetFileNameWithoutExtensionW(L"C:\\archive.tar.gz"), L"archive.tar");
}

TEST(GetFileNameWithoutExtensionW, Null)
{
    EXPECT_EQ(GetFileNameWithoutExtensionW(NULL), L"");
}

TEST(GetFileNameWithoutExtensionW, Empty)
{
    EXPECT_EQ(GetFileNameWithoutExtensionW(L""), L"");
}

TEST(GetFileNameWithoutExtensionW, JustFilename)
{
    EXPECT_EQ(GetFileNameWithoutExtensionW(L"document.pdf"), L"document");
}

// ============================================================================
// GetParentPathW tests
// ============================================================================

TEST(GetParentPathW, BasicParent)
{
    EXPECT_EQ(GetParentPathW(L"C:\\Users\\Test"), L"C:\\Users");
}

TEST(GetParentPathW, WithTrailingBackslash)
{
    EXPECT_EQ(GetParentPathW(L"C:\\Users\\Test\\"), L"C:\\Users");
}

TEST(GetParentPathW, ParentIsRoot)
{
    EXPECT_EQ(GetParentPathW(L"C:\\Users"), L"C:\\");
}

TEST(GetParentPathW, RootNoParent)
{
    EXPECT_EQ(GetParentPathW(L"C:\\"), L"");
}

TEST(GetParentPathW, UNCParent)
{
    EXPECT_EQ(GetParentPathW(L"\\\\server\\share\\folder"), L"\\\\server\\share");
}

TEST(GetParentPathW, UNCRootNoParent)
{
    EXPECT_EQ(GetParentPathW(L"\\\\server\\share"), L"");
}

TEST(GetParentPathW, Null)
{
    EXPECT_EQ(GetParentPathW(NULL), L"");
}

TEST(GetParentPathW, Empty)
{
    EXPECT_EQ(GetParentPathW(L""), L"");
}

TEST(GetParentPathW, DeepPath)
{
    EXPECT_EQ(GetParentPathW(L"C:\\A\\B\\C\\D"), L"C:\\A\\B\\C");
}

TEST(GetParentPathW, UNCDeepPath)
{
    EXPECT_EQ(GetParentPathW(L"\\\\server\\share\\a\\b\\c"), L"\\\\server\\share\\a\\b");
}

// ============================================================================
// IsTheSamePathW tests
// ============================================================================

TEST(IsTheSamePathW, ExactMatch)
{
    EXPECT_TRUE(IsTheSamePathW(L"C:\\Users", L"C:\\Users"));
}

TEST(IsTheSamePathW, CaseInsensitive)
{
    EXPECT_TRUE(IsTheSamePathW(L"C:\\Users", L"c:\\users"));
}

TEST(IsTheSamePathW, TrailingBackslash)
{
    EXPECT_TRUE(IsTheSamePathW(L"C:\\Users", L"C:\\Users\\"));
}

TEST(IsTheSamePathW, ReverseTrailingBackslash)
{
    EXPECT_TRUE(IsTheSamePathW(L"C:\\Users\\", L"C:\\Users"));
}

TEST(IsTheSamePathW, DifferentPaths)
{
    EXPECT_FALSE(IsTheSamePathW(L"C:\\Users", L"C:\\Temp"));
}

TEST(IsTheSamePathW, PrefixOnly)
{
    EXPECT_FALSE(IsTheSamePathW(L"C:\\Users", L"C:\\Users\\Test"));
}

TEST(IsTheSamePathW, BothNull)
{
    EXPECT_TRUE(IsTheSamePathW(NULL, NULL));
}

TEST(IsTheSamePathW, OneNull)
{
    EXPECT_FALSE(IsTheSamePathW(L"C:\\", NULL));
}

TEST(IsTheSamePathW, UNCPaths)
{
    EXPECT_TRUE(IsTheSamePathW(L"\\\\server\\share", L"\\\\SERVER\\SHARE"));
}

TEST(IsTheSamePathW, BothTrailingBackslash)
{
    EXPECT_TRUE(IsTheSamePathW(L"C:\\Users\\", L"c:\\users\\"));
}

// ============================================================================
// PathStartsWithW tests
// ============================================================================

TEST(PathStartsWithW, BasicPrefix)
{
    EXPECT_TRUE(PathStartsWithW(L"C:\\Users\\Test", L"C:\\Users"));
}

TEST(PathStartsWithW, CaseInsensitive)
{
    EXPECT_TRUE(PathStartsWithW(L"C:\\Users\\Test", L"c:\\users"));
}

TEST(PathStartsWithW, PrefixWithBackslash)
{
    EXPECT_TRUE(PathStartsWithW(L"C:\\Users\\Test", L"C:\\Users\\"));
}

TEST(PathStartsWithW, ExactMatch)
{
    EXPECT_TRUE(PathStartsWithW(L"C:\\Users", L"C:\\Users"));
}

TEST(PathStartsWithW, LongerPrefix)
{
    EXPECT_FALSE(PathStartsWithW(L"C:\\Users", L"C:\\Users\\Test"));
}

TEST(PathStartsWithW, PartialComponentMatch)
{
    EXPECT_FALSE(PathStartsWithW(L"C:\\Usernames", L"C:\\Users"));
}

TEST(PathStartsWithW, EmptyPrefix)
{
    EXPECT_TRUE(PathStartsWithW(L"C:\\Users", L""));
}

TEST(PathStartsWithW, NullPath)
{
    EXPECT_FALSE(PathStartsWithW(NULL, L"C:\\"));
}

TEST(PathStartsWithW, NullPrefix)
{
    EXPECT_FALSE(PathStartsWithW(L"C:\\Users", NULL));
}

TEST(PathStartsWithW, BothNull)
{
    EXPECT_FALSE(PathStartsWithW(NULL, NULL));
}

TEST(PathStartsWithW, UNCPrefix)
{
    EXPECT_TRUE(PathStartsWithW(L"\\\\server\\share\\folder", L"\\\\server\\share"));
}
