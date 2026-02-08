// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//
// Google Test suite for filename/path validation helpers:
//   MakeCopyWithBackslashIfNeededW, PathContainsValidComponentsW, AlterFileNameW
//

#include <windows.h>
#include <string>
#include <gtest/gtest.h>

// Declarations matching the standalone implementations
std::wstring MakeCopyWithBackslashIfNeededW(const wchar_t* name);
BOOL NameEndsWithBackslashW(const wchar_t* name);
BOOL PathContainsValidComponentsW(const wchar_t* path);
std::wstring AlterFileNameW(const wchar_t* filename, int format, int change, bool isDir);

// ============================================================================
// MakeCopyWithBackslashIfNeededW
// ============================================================================

TEST(MakeCopyWithBackslashIfNeededW, NormalName_Unchanged)
{
    auto result = MakeCopyWithBackslashIfNeededW(L"hello.txt");
    EXPECT_EQ(result, L"hello.txt");
}

TEST(MakeCopyWithBackslashIfNeededW, TrailingSpace_BackslashAppended)
{
    auto result = MakeCopyWithBackslashIfNeededW(L"hello ");
    EXPECT_EQ(result, L"hello \\");
}

TEST(MakeCopyWithBackslashIfNeededW, TrailingDot_BackslashAppended)
{
    auto result = MakeCopyWithBackslashIfNeededW(L"hello.");
    EXPECT_EQ(result, L"hello.\\");
}

TEST(MakeCopyWithBackslashIfNeededW, TrailingControlChar_BackslashAppended)
{
    // Characters <= L' ' (space) trigger the fix; tab (0x09) qualifies
    auto result = MakeCopyWithBackslashIfNeededW(L"hello\t");
    EXPECT_EQ(result, L"hello\t\\");
}

TEST(MakeCopyWithBackslashIfNeededW, EmptyName_EmptyResult)
{
    auto result = MakeCopyWithBackslashIfNeededW(L"");
    EXPECT_TRUE(result.empty());
}

TEST(MakeCopyWithBackslashIfNeededW, NullPointer_EmptyResult)
{
    auto result = MakeCopyWithBackslashIfNeededW(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST(MakeCopyWithBackslashIfNeededW, TrailingBackslash_Unchanged)
{
    // Already ends with backslash — no additional backslash needed
    auto result = MakeCopyWithBackslashIfNeededW(L"C:\\dir\\");
    EXPECT_EQ(result, L"C:\\dir\\");
}

TEST(MakeCopyWithBackslashIfNeededW, MultipleDots_BackslashAppended)
{
    auto result = MakeCopyWithBackslashIfNeededW(L"name...");
    EXPECT_EQ(result, L"name...\\");
}

// ============================================================================
// NameEndsWithBackslashW
// ============================================================================

TEST(NameEndsWithBackslashW, EndsWithBackslash_ReturnsTrue)
{
    EXPECT_TRUE(NameEndsWithBackslashW(L"C:\\dir\\"));
}

TEST(NameEndsWithBackslashW, NoBackslash_ReturnsFalse)
{
    EXPECT_FALSE(NameEndsWithBackslashW(L"C:\\dir"));
}

TEST(NameEndsWithBackslashW, NullPointer_ReturnsFalse)
{
    EXPECT_FALSE(NameEndsWithBackslashW(nullptr));
}

TEST(NameEndsWithBackslashW, EmptyString_ReturnsFalse)
{
    EXPECT_FALSE(NameEndsWithBackslashW(L""));
}

// ============================================================================
// PathContainsValidComponentsW
// ============================================================================

TEST(PathContainsValidComponentsW, ValidPath_ReturnsTrue)
{
    EXPECT_TRUE(PathContainsValidComponentsW(L"C:\\foo\\bar"));
}

TEST(PathContainsValidComponentsW, ComponentEndingWithDot_ReturnsFalse)
{
    // "foo." is a component ending with dot → invalid
    EXPECT_FALSE(PathContainsValidComponentsW(L"C:\\foo.\\bar"));
}

TEST(PathContainsValidComponentsW, ComponentEndingWithSpace_ReturnsFalse)
{
    EXPECT_FALSE(PathContainsValidComponentsW(L"C:\\foo \\bar"));
}

TEST(PathContainsValidComponentsW, DoubleDotComponent_ReturnsFalse)
{
    // ".." ends with dot → invalid
    EXPECT_FALSE(PathContainsValidComponentsW(L"C:\\foo\\..\\bar"));
}

TEST(PathContainsValidComponentsW, SingleDotComponent_ReturnsFalse)
{
    // "." ends with dot → invalid
    EXPECT_FALSE(PathContainsValidComponentsW(L"C:\\.\\bar"));
}

TEST(PathContainsValidComponentsW, RootPathOnly_ReturnsTrue)
{
    EXPECT_TRUE(PathContainsValidComponentsW(L"C:\\"));
}

TEST(PathContainsValidComponentsW, EmptyPath_ReturnsTrue)
{
    // Empty string — no components to fail → TRUE
    EXPECT_TRUE(PathContainsValidComponentsW(L""));
}

TEST(PathContainsValidComponentsW, LastComponentEndingWithDot_ReturnsFalse)
{
    // Last component (no trailing backslash) ending with dot
    EXPECT_FALSE(PathContainsValidComponentsW(L"C:\\foo\\bar."));
}

TEST(PathContainsValidComponentsW, LastComponentEndingWithSpace_ReturnsFalse)
{
    EXPECT_FALSE(PathContainsValidComponentsW(L"C:\\foo\\bar "));
}

// ============================================================================
// AlterFileNameW — format=2 lowercase, change=0 (name+ext)
// ============================================================================

TEST(AlterFileNameW, Lowercase_NameAndExt)
{
    auto result = AlterFileNameW(L"HELLO.TXT", 2, 0, false);
    EXPECT_EQ(result, L"hello.txt");
}

TEST(AlterFileNameW, Lowercase_AlreadyLower)
{
    auto result = AlterFileNameW(L"hello.txt", 2, 0, false);
    EXPECT_EQ(result, L"hello.txt");
}

// ============================================================================
// AlterFileNameW — format=3 uppercase, change=0
// ============================================================================

TEST(AlterFileNameW, Uppercase_NameAndExt)
{
    auto result = AlterFileNameW(L"hello.txt", 3, 0, false);
    EXPECT_EQ(result, L"HELLO.TXT");
}

// ============================================================================
// AlterFileNameW — format=1 capitalize, change=0
// ============================================================================

TEST(AlterFileNameW, Capitalize_SingleWord)
{
    auto result = AlterFileNameW(L"hello.txt", 1, 0, false);
    EXPECT_EQ(result, L"Hello.Txt");
}

TEST(AlterFileNameW, Capitalize_MultipleWords)
{
    // Spaces and dots reset the capital flag
    auto result = AlterFileNameW(L"hello world.txt", 1, 0, false);
    EXPECT_EQ(result, L"Hello World.Txt");
}

// ============================================================================
// AlterFileNameW — change=1 (name only), extension preserved
// ============================================================================

TEST(AlterFileNameW, Uppercase_NameOnly_ExtPreserved)
{
    auto result = AlterFileNameW(L"hello.txt", 3, 1, false);
    EXPECT_EQ(result, L"HELLO.txt");
}

TEST(AlterFileNameW, Lowercase_NameOnly_ExtPreserved)
{
    auto result = AlterFileNameW(L"HELLO.TXT", 2, 1, false);
    EXPECT_EQ(result, L"hello.TXT");
}

TEST(AlterFileNameW, Capitalize_NameOnly_ExtPreserved)
{
    auto result = AlterFileNameW(L"hello world.TXT", 1, 1, false);
    EXPECT_EQ(result, L"Hello World.TXT");
}

// ============================================================================
// AlterFileNameW — change=2 (ext only), name preserved
// ============================================================================

TEST(AlterFileNameW, Uppercase_ExtOnly_NamePreserved)
{
    auto result = AlterFileNameW(L"hello.txt", 3, 2, false);
    EXPECT_EQ(result, L"hello.TXT");
}

TEST(AlterFileNameW, Lowercase_ExtOnly_NamePreserved)
{
    auto result = AlterFileNameW(L"HELLO.TXT", 2, 2, false);
    EXPECT_EQ(result, L"HELLO.txt");
}

// ============================================================================
// AlterFileNameW — change=2 with no extension
// ============================================================================

TEST(AlterFileNameW, ExtOnly_NoExtension_Unchanged)
{
    // No dot → no extension → return as-is
    auto result = AlterFileNameW(L"README", 3, 2, false);
    EXPECT_EQ(result, L"README");
}

// ============================================================================
// AlterFileNameW — format=7 (mixed name, lowercase ext), change=0
// ============================================================================

TEST(AlterFileNameW, Format7_MixedNameLowercaseExt)
{
    auto result = AlterFileNameW(L"HELLO WORLD.TXT", 7, 0, false);
    EXPECT_EQ(result, L"Hello World.txt");
}

// ============================================================================
// AlterFileNameW — Unicode support
// ============================================================================

TEST(AlterFileNameW, Unicode_Lowercase)
{
    // towlower/towupper behavior for non-ASCII depends on C locale;
    // verify at least that ASCII chars are lowercased and the result length is correct
    auto result = AlterFileNameW(L"\x00C9TUDE.TXT", 2, 0, false);
    EXPECT_EQ(result.length(), 9u);
    // ASCII portion should be lowered regardless of locale
    EXPECT_EQ(result.substr(1), L"tude.txt");
}

TEST(AlterFileNameW, Unicode_Uppercase)
{
    auto result = AlterFileNameW(L"\x00E9tude.txt", 3, 0, false);
    EXPECT_EQ(result.length(), 9u);
    EXPECT_EQ(result.substr(1), L"TUDE.TXT");
}

// ============================================================================
// Wide path utilities from salamdr3.cpp
// ============================================================================

// Declarations
void SalPathAppendW(std::wstring& path, const wchar_t* name);
void SalPathAddBackslashW(std::wstring& path);
void SalPathRemoveBackslashW(std::wstring& path);
void SalPathStripPathW(std::wstring& path);
const wchar_t* SalPathFindFileNameW(const wchar_t* path);
void SalPathRemoveExtensionW(std::wstring& path);
bool SalPathAddExtensionW(std::wstring& path, const wchar_t* extension);
bool SalPathRenameExtensionW(std::wstring& path, const wchar_t* extension);

// --- SalPathAppendW ---

TEST(SalPathAppendW, BasicAppend)
{
    std::wstring path = L"C:\\Dir";
    SalPathAppendW(path, L"file.txt");
    EXPECT_EQ(path, L"C:\\Dir\\file.txt");
}

TEST(SalPathAppendW, PathWithTrailingBackslash)
{
    std::wstring path = L"C:\\Dir\\";
    SalPathAppendW(path, L"file.txt");
    EXPECT_EQ(path, L"C:\\Dir\\file.txt");
}

TEST(SalPathAppendW, NameWithLeadingBackslash)
{
    std::wstring path = L"C:\\Dir";
    SalPathAppendW(path, L"\\file.txt");
    EXPECT_EQ(path, L"C:\\Dir\\file.txt");
}

TEST(SalPathAppendW, EmptyPath)
{
    std::wstring path;
    SalPathAppendW(path, L"file.txt");
    EXPECT_EQ(path, L"file.txt");
}

TEST(SalPathAppendW, NullName)
{
    std::wstring path = L"C:\\Dir";
    SalPathAppendW(path, nullptr);
    EXPECT_EQ(path, L"C:\\Dir");
}

TEST(SalPathAppendW, LongPath)
{
    std::wstring path = L"C:\\" + std::wstring(200, L'a');
    std::wstring name(100, L'b');
    SalPathAppendW(path, name.c_str());
    EXPECT_GT(path.length(), 260u);
    EXPECT_EQ(path.back(), L'b');
}

// --- SalPathAddBackslashW ---

TEST(SalPathAddBackslashW, AddsBackslash)
{
    std::wstring path = L"C:\\Dir";
    SalPathAddBackslashW(path);
    EXPECT_EQ(path, L"C:\\Dir\\");
}

TEST(SalPathAddBackslashW, AlreadyHasBackslash)
{
    std::wstring path = L"C:\\Dir\\";
    SalPathAddBackslashW(path);
    EXPECT_EQ(path, L"C:\\Dir\\");
}

TEST(SalPathAddBackslashW, EmptyPath)
{
    std::wstring path;
    SalPathAddBackslashW(path);
    EXPECT_TRUE(path.empty());
}

// --- SalPathRemoveBackslashW ---

TEST(SalPathRemoveBackslashW, RemovesBackslash)
{
    std::wstring path = L"C:\\Dir\\";
    SalPathRemoveBackslashW(path);
    EXPECT_EQ(path, L"C:\\Dir");
}

TEST(SalPathRemoveBackslashW, NoBackslash)
{
    std::wstring path = L"C:\\Dir";
    SalPathRemoveBackslashW(path);
    EXPECT_EQ(path, L"C:\\Dir");
}

// --- SalPathStripPathW ---

TEST(SalPathStripPathW, StripsPath)
{
    std::wstring path = L"C:\\Dir\\file.txt";
    SalPathStripPathW(path);
    EXPECT_EQ(path, L"file.txt");
}

TEST(SalPathStripPathW, NoBackslash)
{
    std::wstring path = L"file.txt";
    SalPathStripPathW(path);
    EXPECT_EQ(path, L"file.txt");
}

// --- SalPathFindFileNameW ---

TEST(SalPathFindFileNameW, FindsFileName)
{
    EXPECT_STREQ(SalPathFindFileNameW(L"C:\\Dir\\file.txt"), L"file.txt");
}

TEST(SalPathFindFileNameW, NoPath)
{
    EXPECT_STREQ(SalPathFindFileNameW(L"file.txt"), L"file.txt");
}

TEST(SalPathFindFileNameW, NullReturnsNull)
{
    EXPECT_EQ(SalPathFindFileNameW(nullptr), nullptr);
}

// --- SalPathRemoveExtensionW ---

TEST(SalPathRemoveExtensionW, RemovesExtension)
{
    std::wstring path = L"C:\\Dir\\file.txt";
    SalPathRemoveExtensionW(path);
    EXPECT_EQ(path, L"C:\\Dir\\file");
}

TEST(SalPathRemoveExtensionW, NoExtension)
{
    std::wstring path = L"C:\\Dir\\file";
    SalPathRemoveExtensionW(path);
    EXPECT_EQ(path, L"C:\\Dir\\file");
}

TEST(SalPathRemoveExtensionW, DotInDirectory)
{
    std::wstring path = L"C:\\Dir.old\\file";
    SalPathRemoveExtensionW(path);
    EXPECT_EQ(path, L"C:\\Dir.old\\file");
}

// --- SalPathAddExtensionW ---

TEST(SalPathAddExtensionW, AddsExtension)
{
    std::wstring path = L"C:\\Dir\\file";
    EXPECT_TRUE(SalPathAddExtensionW(path, L".txt"));
    EXPECT_EQ(path, L"C:\\Dir\\file.txt");
}

TEST(SalPathAddExtensionW, ExistingExtension_NotAdded)
{
    std::wstring path = L"C:\\Dir\\file.txt";
    EXPECT_TRUE(SalPathAddExtensionW(path, L".bak"));
    EXPECT_EQ(path, L"C:\\Dir\\file.txt"); // unchanged
}

// --- SalPathRenameExtensionW ---

TEST(SalPathRenameExtensionW, ReplacesExtension)
{
    std::wstring path = L"C:\\Dir\\file.txt";
    EXPECT_TRUE(SalPathRenameExtensionW(path, L".bak"));
    EXPECT_EQ(path, L"C:\\Dir\\file.bak");
}

TEST(SalPathRenameExtensionW, AddsExtensionWhenNone)
{
    std::wstring path = L"C:\\Dir\\file";
    EXPECT_TRUE(SalPathRenameExtensionW(path, L".txt"));
    EXPECT_EQ(path, L"C:\\Dir\\file.txt");
}
