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

// ============================================================================
// CPathBuffer struct member tests
// ============================================================================

struct TestStruct
{
    CPathBuffer path;
    CPathBuffer name;
    int value;
};

TEST(CPathBufferStructTest, ConstructionInStruct)
{
    TestStruct s;
    s.value = 42;
    lstrcpynA(s.path, "C:\\test\\path", s.path.Size());
    lstrcpynA(s.name, "testfile.txt", s.name.Size());
    EXPECT_STREQ(s.path, "C:\\test\\path");
    EXPECT_STREQ(s.name, "testfile.txt");
    EXPECT_EQ(s.value, 42);
}

TEST(CPathBufferStructTest, NullTerminationAtEnd)
{
    CPathBuffer buf;
    // Simulate the pattern: buf[buf.Size()-1] = 0
    memset(buf, 'A', 100);
    buf[buf.Size() - 1] = 0;
    // First 100 chars are 'A', rest is whatever, last char is null
    EXPECT_EQ(buf[0], 'A');
    EXPECT_EQ(buf[99], 'A');
    EXPECT_EQ(buf[buf.Size() - 1], '\0');
}

TEST(CPathBufferStructTest, MultipleCPathBuffersIndependent)
{
    TestStruct s;
    lstrcpynA(s.path, "C:\\alpha\\beta", s.path.Size());
    lstrcpynA(s.name, "gamma.txt", s.name.Size());
    // Modifying one must not affect the other
    lstrcpynA(s.path, "D:\\other", s.path.Size());
    EXPECT_STREQ(s.path, "D:\\other");
    EXPECT_STREQ(s.name, "gamma.txt");
}

TEST(CPathBufferStructTest, PointerArithmetic)
{
    CPathBuffer buf("C:\\dir\\file.txt");
    char* ptr = buf.Get();
    // buf + offset should point into the buffer
    char* offset = ptr + 3;
    EXPECT_EQ(*offset, 'd'); // "C:\dir..." -> index 3 is 'd'
    // Also works via implicit conversion
    char* base = buf;
    EXPECT_EQ(*(base + 0), 'C');
    EXPECT_EQ(*(base + 1), ':');
    EXPECT_EQ(*(base + 2), '\\');
}

TEST(CPathBufferStructTest, LstrcpynSafeCopy)
{
    CPathBuffer buf;
    const char* longStr = "C:\\very\\long\\path\\that\\is\\still\\fine";
    lstrcpynA(buf, longStr, buf.Size());
    EXPECT_STREQ(buf, longStr);

    // lstrcpynA truncates at count-1
    CPathBuffer buf2;
    lstrcpynA(buf2.Get(), "ABCDEFGHIJ", 5);
    EXPECT_STREQ(buf2.Get(), "ABCD");
}

TEST(CPathBufferStructTest, LongStringBeyondMaxPath)
{
    CPathBuffer buf;
    // Build a string longer than MAX_PATH (260)
    std::string longPath = "C:\\";
    for (int i = 0; i < 30; i++)
        longPath += "longdirname\\";
    longPath += "file.txt";
    EXPECT_GT(longPath.size(), 260u);

    lstrcpynA(buf, longPath.c_str(), buf.Size());
    EXPECT_STREQ(buf, longPath.c_str());
}

// ============================================================================
// CWidePathBuffer struct member tests
// ============================================================================

struct WideTestStruct
{
    CWidePathBuffer path;
    CWidePathBuffer name;
    int value;
};

TEST(CWidePathBufferStructTest, ConstructionInStruct)
{
    WideTestStruct s;
    s.value = 99;
    lstrcpynW(s.path, L"C:\\wide\\test", s.path.Size());
    lstrcpynW(s.name, L"widefile.txt", s.name.Size());
    EXPECT_STREQ(s.path, L"C:\\wide\\test");
    EXPECT_STREQ(s.name, L"widefile.txt");
    EXPECT_EQ(s.value, 99);
}

TEST(CWidePathBufferStructTest, NullTerminationAtEnd)
{
    CWidePathBuffer buf;
    // Simulate the pattern: buf[buf.Size()-1] = 0
    wmemset(buf, L'B', 100);
    buf[buf.Size() - 1] = L'\0';
    EXPECT_EQ(buf[0], L'B');
    EXPECT_EQ(buf[99], L'B');
    EXPECT_EQ(buf[buf.Size() - 1], L'\0');
}

TEST(CWidePathBufferStructTest, MultiByteToWideCharConversion)
{
    CWidePathBuffer wideBuf;
    const char* ansiPath = "C:\\convert\\this\\path";
    int len = MultiByteToWideChar(CP_ACP, 0, ansiPath, -1, wideBuf, wideBuf.Size());
    EXPECT_GT(len, 0);
    EXPECT_STREQ(wideBuf, L"C:\\convert\\this\\path");
}

TEST(CWidePathBufferStructTest, AppendMethod)
{
    CWidePathBuffer buf(L"C:\\root");
    EXPECT_TRUE(buf.Append(L"sub1"));
    EXPECT_STREQ(buf.Get(), L"C:\\root\\sub1");
    EXPECT_TRUE(buf.Append(L"sub2"));
    EXPECT_STREQ(buf.Get(), L"C:\\root\\sub1\\sub2");
    EXPECT_TRUE(buf.Append(L"file.txt"));
    EXPECT_STREQ(buf.Get(), L"C:\\root\\sub1\\sub2\\file.txt");
}
