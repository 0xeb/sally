// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Google Test suite for CPathBuffer/CWidePathBuffer interaction with
// Windows APIs, sprintf, and other common usage patterns.
//
// These tests document and verify the workarounds needed when using
// CPathBuffer with template-deduced or variadic APIs.
//
//****************************************************************************

#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <cstdio>
#include <strsafe.h>
#include <shlwapi.h>

#include "../common/widepath.h"

// ============================================================================
// Basic Windows API compatibility
// ============================================================================

TEST(CPathBufferWinAPI, GetModuleFileNameA)
{
    CPathBuffer buf;
    DWORD len = GetModuleFileNameA(NULL, buf, buf.Size());
    EXPECT_GT(len, 0u);
    EXPECT_STRNE(buf, "");
}

TEST(CPathBufferWinAPI, GetCurrentDirectoryA)
{
    CPathBuffer buf;
    DWORD len = GetCurrentDirectoryA(buf.Size(), buf);
    EXPECT_GT(len, 0u);
}

TEST(CPathBufferWinAPI, GetFileAttributesA_WithCPathBuffer)
{
    CPathBuffer buf;
    ::GetCurrentDirectoryA((DWORD)buf.Size(), buf.Get());
    DWORD attrs = ::GetFileAttributesA(buf);
    EXPECT_NE(attrs, INVALID_FILE_ATTRIBUTES);
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// ============================================================================
// sprintf / _snprintf_s with CPathBuffer (variadic function pitfall)
// ============================================================================

TEST(CPathBufferWinAPI, SprintfDest)
{
    CPathBuffer buf;
    // CPathBuffer as destination - implicit char* conversion works
    sprintf(buf, "Hello %s", "World");
    EXPECT_STREQ(buf, "Hello World");
}

TEST(CPathBufferWinAPI, SprintfArg_NeedsCast)
{
    CPathBuffer src;
    strcpy(src, "test_value");

    char dest[100];
    // CPathBuffer as variadic arg REQUIRES explicit cast
    sprintf(dest, "Value=%s", (const char*)src);
    EXPECT_STREQ(dest, "Value=test_value");
}

TEST(CPathBufferWinAPI, SnprintfDest)
{
    CPathBuffer buf;
    _snprintf_s(buf, buf.Size(), _TRUNCATE, "Path: %s\\%s", "C:\\dir", "file.txt");
    EXPECT_STREQ(buf, "Path: C:\\dir\\file.txt");
}

// ============================================================================
// lstrcpyn / lstrcpy / lstrcat
// ============================================================================

TEST(CPathBufferWinAPI, LstrcpynDest)
{
    CPathBuffer buf;
    lstrcpynA(buf, "Hello World", buf.Size());
    EXPECT_STREQ(buf, "Hello World");
}

TEST(CPathBufferWinAPI, LstrcpyDest)
{
    CPathBuffer buf;
    lstrcpyA(buf, "Test");
    EXPECT_STREQ(buf, "Test");
}

TEST(CPathBufferWinAPI, LstrcatDest)
{
    CPathBuffer buf;
    lstrcpyA(buf, "Hello ");
    lstrcatA(buf, "World");
    EXPECT_STREQ(buf, "Hello World");
}

// ============================================================================
// LPARAM / WPARAM cast patterns
// ============================================================================

TEST(CPathBufferWinAPI, LparamCast)
{
    CPathBuffer buf;
    strcpy(buf, "test");
    // (LPARAM)buf doesn't work - need explicit char* cast
    LPARAM lp = (LPARAM)(char*)buf;
    const char* recovered = (const char*)lp;
    EXPECT_STREQ(recovered, "test");
}

TEST(CPathBufferWinAPI, WparamCast)
{
    CPathBuffer buf;
    strcpy(buf, "data");
    WPARAM wp = (WPARAM)(char*)buf;
    const char* recovered = (const char*)wp;
    EXPECT_STREQ(recovered, "data");
}

// ============================================================================
// Pointer arithmetic
// ============================================================================

TEST(CPathBufferWinAPI, PointerArithAddition)
{
    CPathBuffer buf;
    strcpy(buf, "C:\\test\\file.txt");
    char* afterRoot = (char*)buf + 3;
    EXPECT_STREQ(afterRoot, "test\\file.txt");
}

TEST(CPathBufferWinAPI, PointerDifference)
{
    CPathBuffer buf;
    strcpy(buf, "C:\\test");
    char* backslash = strchr(buf, '\\');
    ASSERT_NE(backslash, nullptr);
    ptrdiff_t offset = backslash - (char*)buf;
    EXPECT_EQ(offset, 2);
}

// ============================================================================
// reinterpret_cast patterns (e.g., registry LPBYTE)
// ============================================================================

TEST(CPathBufferWinAPI, ReinterpretCastToLPBYTE)
{
    CPathBuffer buf;
    strcpy(buf, "data");
    // Must use .Get() for reinterpret_cast
    LPBYTE lpb = reinterpret_cast<LPBYTE>(buf.Get());
    EXPECT_EQ(lpb[0], 'd');
    EXPECT_EQ(lpb[1], 'a');
}

// ============================================================================
// Ternary operator pitfall
// ============================================================================

TEST(CPathBufferWinAPI, TernaryWithNull)
{
    CPathBuffer buf;
    strcpy(buf, "present");
    bool condition = true;
    // condition ? cpathbuf : NULL doesn't compile
    // Must use: condition ? (const char*)cpathbuf : NULL
    const char* result = condition ? (const char*)buf : (const char*)NULL;
    EXPECT_STREQ(result, "present");

    condition = false;
    result = condition ? (const char*)buf : (const char*)NULL;
    EXPECT_EQ(result, nullptr);
}

// ============================================================================
// String comparison
// ============================================================================

TEST(CPathBufferWinAPI, StrcmpComparison)
{
    CPathBuffer buf;
    strcpy(buf, "hello");
    EXPECT_EQ(strcmp(buf, "hello"), 0);
    EXPECT_NE(strcmp(buf, "world"), 0);
}

TEST(CPathBufferWinAPI, StricmpComparison)
{
    CPathBuffer buf;
    strcpy(buf, "Hello");
    EXPECT_EQ(_stricmp(buf, "hello"), 0);
    EXPECT_EQ(_stricmp(buf, "HELLO"), 0);
}

TEST(CPathBufferWinAPI, LstrcmpiComparison)
{
    CPathBuffer buf;
    strcpy(buf, "C:\\Test");
    EXPECT_EQ(lstrcmpiA(buf, "c:\\test"), 0);
}

// ============================================================================
// PathFind* shell functions
// ============================================================================

TEST(CPathBufferWinAPI, PathFindExtensionA)
{
    CPathBuffer buf;
    strcpy(buf, "C:\\dir\\file.txt");
    LPSTR ext = PathFindExtensionA(buf);
    EXPECT_STREQ(ext, ".txt");
}

TEST(CPathBufferWinAPI, PathFindFileNameA)
{
    CPathBuffer buf;
    strcpy(buf, "C:\\dir\\file.txt");
    LPSTR name = PathFindFileNameA(buf);
    EXPECT_STREQ(name, "file.txt");
}

// ============================================================================
// Buffer size checks
// ============================================================================

TEST(CPathBufferWinAPI, SizeIsSAL_MAX_LONG_PATH)
{
    CPathBuffer buf;
    EXPECT_EQ(buf.Size(), SAL_MAX_LONG_PATH);
    EXPECT_EQ(buf.Size(), 32767);
}

TEST(CPathBufferWinAPI, CanStoreVeryLongPath)
{
    CPathBuffer buf;
    std::string longPath = "C:\\";
    for (int i = 0; i < 1000; i++)
        longPath += "verylongsegment\\";
    longPath.pop_back();

    ASSERT_LT(longPath.size(), (size_t)buf.Size());
    strcpy(buf, longPath.c_str());
    EXPECT_STREQ(buf, longPath.c_str());
}

// ============================================================================
// Zero-initialization pattern
// ============================================================================

TEST(CPathBufferWinAPI, ZeroInitWithBracket)
{
    CPathBuffer buf;
    buf[0] = 0;
    EXPECT_EQ(strlen(buf), 0u);
}

TEST(CPathBufferWinAPI, MemsetZero)
{
    CPathBuffer buf;
    memset(buf.Get(), 0, buf.Size());
    EXPECT_EQ(buf[0], '\0');
    EXPECT_EQ(buf[100], '\0');
}
