// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
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
// Long path construction — verifies paths > MAX_PATH (260) are not truncated
// These test the patterns fixed during MAX_PATH barrier removal.
// ============================================================================

TEST(CPathBufferLongPath, CanHoldPathLongerThanMAX_PATH)
{
    CPathBuffer buf;
    // Build a path of ~300 chars: C:\<long_dir>\file.txt
    std::string longDir(280, 'a');
    std::string path = "C:\\" + longDir + "\\file.txt";
    ASSERT_GT(path.length(), (size_t)MAX_PATH);

    ASSERT_TRUE(buf.EnsureCapacity((int)path.length() + 1));
    strcpy(buf, path.c_str());
    EXPECT_STREQ(buf, path.c_str());
    EXPECT_EQ(strlen(buf), path.length());
}

TEST(CPathBufferLongPath, PathConcatExceedingMAX_PATH)
{
    // Simulates the pattern in SalGetFullName where curDir + name are joined
    CPathBuffer buf;
    std::string dir(200, 'd');
    std::string name(100, 'n');
    std::string fullPath = "C:\\" + dir + "\\" + name;
    ASSERT_GT(fullPath.length(), (size_t)MAX_PATH);

    ASSERT_TRUE(buf.EnsureCapacity((int)fullPath.length() + 1));
    strcpy(buf, "C:\\");
    strcat(buf, dir.c_str());
    strcat(buf, "\\");
    strcat(buf, name.c_str());
    EXPECT_STREQ(buf, fullPath.c_str());
}

TEST(CPathBufferLongPath, SizeStartsAtInitialCapacity)
{
    CPathBuffer buf;
    EXPECT_EQ(buf.Size(), SAL_PATH_BUFFER_INITIAL_CAPACITY);
    EXPECT_EQ(buf.MaxCapacity(), SAL_MAX_LONG_PATH);
    EXPECT_GT(buf.MaxCapacity(), MAX_PATH);
}

TEST(CPathBufferLongPath, LstrcpynWithLongPath)
{
    CPathBuffer buf;
    std::string longPath(500, 'x');
    ASSERT_TRUE(buf.EnsureCapacity((int)longPath.size() + 1));
    lstrcpynA(buf, longPath.c_str(), buf.Size());
    EXPECT_EQ(strlen(buf), 500u);
}

TEST(CPathBufferLongPath, MemmoveWithLongPath)
{
    // Simulates the pattern in SalGetFullName: memmove(name + offset, s, len)
    CPathBuffer buf;
    std::string prefix = "C:\\";
    std::string suffix(300, 's');
    ASSERT_TRUE(buf.EnsureCapacity((int)(prefix.length() + suffix.length() + 1)));
    strcpy(buf, suffix.c_str());
    // Insert prefix at start
    memmove(buf.Get() + prefix.length(), buf.Get(), strlen(buf) + 1);
    memcpy(buf.Get(), prefix.c_str(), prefix.length());

    std::string expected = prefix + suffix;
    ASSERT_GT(expected.length(), (size_t)MAX_PATH);
    EXPECT_STREQ(buf, expected.c_str());
}

// ============================================================================
// Buffer size checks
// ============================================================================

TEST(CPathBufferWinAPI, SizeStartsAtInitialCapacity)
{
    CPathBuffer buf;
    EXPECT_EQ(buf.Size(), SAL_PATH_BUFFER_INITIAL_CAPACITY);
    EXPECT_EQ(buf.MaxCapacity(), SAL_MAX_LONG_PATH);
    EXPECT_EQ(buf.MaxCapacity(), 32767);
}

TEST(CPathBufferWinAPI, CanStoreVeryLongPath)
{
    CPathBuffer buf;
    std::string longPath = "C:\\";
    for (int i = 0; i < 1000; i++)
        longPath += "verylongsegment\\";
    longPath.pop_back();

    ASSERT_LT(longPath.size(), (size_t)buf.MaxCapacity());
    ASSERT_TRUE(buf.EnsureCapacity((int)longPath.size() + 1));
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

// ============================================================================
// GetVolumeInformationA 16-bit overflow regression test
//
// Windows 10 KERNELBASE!GetVolumeInformationA has a bug: it computes the
// internal wide buffer size as (nVolumeNameSize + 1) * 2 using 16-bit
// arithmetic. When nVolumeNameSize >= 32767 (SAL_MAX_LONG_PATH), the result
// overflows to 0, causing a 0-byte allocation and subsequent heap corruption.
//
// The same overflow applies to nFileSystemNameSize (parameter 8).
//
// ALWAYS pass MAX_PATH (not CPathBuffer::MaxCapacity()) as the size parameter
// to GetVolumeInformationA / GetVolumeInformation.
// ============================================================================

TEST(CPathBufferWinAPI, GetVolumeInformationA_WithMAX_PATH)
{
    // GetVolumeInformationA works correctly with MAX_PATH size
    CPathBuffer volumeName;
    char root[] = "C:\\";
    DWORD dummy, flags;

    BOOL ok = GetVolumeInformationA(root, volumeName, MAX_PATH,
                                    NULL, &dummy, &flags, NULL, 0);
    // Should succeed on C: drive (always present)
    EXPECT_TRUE(ok);
}

TEST(CPathBufferWinAPI, GetVolumeInformationA_WithBoundarySize)
{
    // Size 32766 is the maximum safe value: (32766+1)*2 = 65534, fits in 16-bit.
    // Size 32767 (SAL_MAX_LONG_PATH) would overflow: (32767+1)*2 = 0 in 16-bit!
    CPathBuffer volumeName;
    char root[] = "C:\\";
    DWORD dummy, flags;

    // 32766 should work — this is the boundary
    BOOL ok = GetVolumeInformationA(root, volumeName, 32766,
                                    NULL, &dummy, &flags, NULL, 0);
    EXPECT_TRUE(ok);

    // DO NOT test with 32767 (SAL_MAX_LONG_PATH) — it causes heap corruption!
    // With page heap enabled, it crashes immediately. Without page heap, it
    // silently corrupts adjacent heap memory.
}

TEST(CPathBufferWinAPI, GetVolumeInformationA_FileSystemName_WithMAX_PATH)
{
    // The same 16-bit overflow affects the file system name size parameter.
    CPathBuffer fsName;
    char root[] = "C:\\";
    DWORD dummy, flags;

    BOOL ok = GetVolumeInformationA(root, NULL, 0, NULL, &dummy, &flags,
                                    fsName, MAX_PATH);
    EXPECT_TRUE(ok);
    // Should return something like "NTFS", "FAT32", "exFAT"
    EXPECT_GT(strlen(fsName), 0u);
}
