// Tests for SalSplitGeneralPathW, CutSpacesFromBothSidesW, and raw-buffer wide path overloads
#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <cstdlib>
#include <cwchar>

// ============================================================================
// CutSpacesFromBothSidesW tests
// ============================================================================

static BOOL CutSpacesFromBothSidesW_Standalone(wchar_t* path)
{
    BOOL ch = FALSE;
    wchar_t* n = path;
    while (*n != 0 && *n <= L' ')
        n++;
    if (n > path)
    {
        memmove(path, n, (wcslen(n) + 1) * sizeof(wchar_t));
        ch = TRUE;
    }
    n = path + wcslen(path);
    while (n > path && (*(n - 1) <= L' '))
        n--;
    if (*n != 0)
    {
        *n = 0;
        ch = TRUE;
    }
    return ch;
}

TEST(CutSpacesFromBothSidesWTest, NoSpaces)
{
    wchar_t buf[] = L"hello";
    EXPECT_FALSE(CutSpacesFromBothSidesW_Standalone(buf));
    EXPECT_STREQ(buf, L"hello");
}

TEST(CutSpacesFromBothSidesWTest, LeadingSpaces)
{
    wchar_t buf[] = L"   hello";
    EXPECT_TRUE(CutSpacesFromBothSidesW_Standalone(buf));
    EXPECT_STREQ(buf, L"hello");
}

TEST(CutSpacesFromBothSidesWTest, TrailingSpaces)
{
    wchar_t buf[] = L"hello   ";
    EXPECT_TRUE(CutSpacesFromBothSidesW_Standalone(buf));
    EXPECT_STREQ(buf, L"hello");
}

TEST(CutSpacesFromBothSidesWTest, BothSides)
{
    wchar_t buf[] = L"  hello  ";
    EXPECT_TRUE(CutSpacesFromBothSidesW_Standalone(buf));
    EXPECT_STREQ(buf, L"hello");
}

TEST(CutSpacesFromBothSidesWTest, AllSpaces)
{
    wchar_t buf[] = L"    ";
    EXPECT_TRUE(CutSpacesFromBothSidesW_Standalone(buf));
    EXPECT_STREQ(buf, L"");
}

TEST(CutSpacesFromBothSidesWTest, EmptyString)
{
    wchar_t buf[] = L"";
    EXPECT_FALSE(CutSpacesFromBothSidesW_Standalone(buf));
    EXPECT_STREQ(buf, L"");
}

TEST(CutSpacesFromBothSidesWTest, TabsAndControlChars)
{
    wchar_t buf[] = L"\t hello \r\n";
    EXPECT_TRUE(CutSpacesFromBothSidesW_Standalone(buf));
    EXPECT_STREQ(buf, L"hello");
}

// ============================================================================
// SalPathAppendW raw-buffer overload tests
// ============================================================================

static BOOL SalPathAppendW_Buf(wchar_t* path, const wchar_t* name, int pathSize)
{
    if (name == NULL)
        return TRUE;
    int l1 = (int)wcslen(path);
    int l2 = (int)wcslen(name);
    if (l1 > 0 && path[l1 - 1] != L'\\')
    {
        if (l1 + 1 + l2 + 1 > pathSize)
            return FALSE;
        path[l1++] = L'\\';
    }
    else
    {
        if (l1 + l2 + 1 > pathSize)
            return FALSE;
    }
    memmove(path + l1, name, (l2 + 1) * sizeof(wchar_t));
    return TRUE;
}

TEST(SalPathAppendWBufTest, BasicAppend)
{
    wchar_t buf[100] = L"C:\\Dir";
    EXPECT_TRUE(SalPathAppendW_Buf(buf, L"file.txt", 100));
    EXPECT_STREQ(buf, L"C:\\Dir\\file.txt");
}

TEST(SalPathAppendWBufTest, TrailingBackslash)
{
    wchar_t buf[100] = L"C:\\Dir\\";
    EXPECT_TRUE(SalPathAppendW_Buf(buf, L"file.txt", 100));
    EXPECT_STREQ(buf, L"C:\\Dir\\file.txt");
}

TEST(SalPathAppendWBufTest, BufferTooSmall)
{
    wchar_t buf[10] = L"C:\\Dir";
    EXPECT_FALSE(SalPathAppendW_Buf(buf, L"longname.txt", 10));
}

TEST(SalPathAppendWBufTest, NullName)
{
    wchar_t buf[100] = L"C:\\Dir";
    EXPECT_TRUE(SalPathAppendW_Buf(buf, NULL, 100));
    EXPECT_STREQ(buf, L"C:\\Dir");
}

// ============================================================================
// SalPathAddBackslashW raw-buffer overload tests
// ============================================================================

static BOOL SalPathAddBackslashW_Buf(wchar_t* path, int pathSize)
{
    int l = (int)wcslen(path);
    if (l > 0 && path[l - 1] != L'\\')
    {
        if (l + 2 > pathSize)
            return FALSE;
        path[l] = L'\\';
        path[l + 1] = 0;
    }
    return TRUE;
}

TEST(SalPathAddBackslashWBufTest, AddsBackslash)
{
    wchar_t buf[100] = L"C:\\Dir";
    EXPECT_TRUE(SalPathAddBackslashW_Buf(buf, 100));
    EXPECT_STREQ(buf, L"C:\\Dir\\");
}

TEST(SalPathAddBackslashWBufTest, AlreadyHasBackslash)
{
    wchar_t buf[100] = L"C:\\Dir\\";
    EXPECT_TRUE(SalPathAddBackslashW_Buf(buf, 100));
    EXPECT_STREQ(buf, L"C:\\Dir\\");
}

TEST(SalPathAddBackslashWBufTest, BufferTooSmall)
{
    wchar_t buf[7] = L"C:\\Dir";
    EXPECT_FALSE(SalPathAddBackslashW_Buf(buf, 7));
}

// ============================================================================
// SalPathRemoveBackslashW raw-buffer overload tests
// ============================================================================

static void SalPathRemoveBackslashW_Buf(wchar_t* path)
{
    int l = (int)wcslen(path);
    if (l > 0 && path[l - 1] == L'\\')
        path[l - 1] = 0;
}

TEST(SalPathRemoveBackslashWBufTest, RemovesBackslash)
{
    wchar_t buf[100] = L"C:\\Dir\\";
    SalPathRemoveBackslashW_Buf(buf);
    EXPECT_STREQ(buf, L"C:\\Dir");
}

TEST(SalPathRemoveBackslashWBufTest, NoBackslash)
{
    wchar_t buf[100] = L"C:\\Dir";
    SalPathRemoveBackslashW_Buf(buf);
    EXPECT_STREQ(buf, L"C:\\Dir");
}
