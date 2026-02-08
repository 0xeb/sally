// Tests for BuildNameW — wide path construction from directory + name
#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <cstdlib>

// Standalone reimplementation of BuildNameW for isolated testing.
// No UI prompts — returns NULL on overflow without showing dialogs.
static wchar_t* BuildNameW_Standalone(const wchar_t* path, const wchar_t* name)
{
    int l1 = (int)wcslen(path);
    int l2, len = l1;
    if (name != NULL)
    {
        l2 = (int)wcslen(name);
        len += l2;
        if (path[l1 - 1] != L'\\')
            len++;
    }
    // SAL_MAX_LONG_PATH = 32767
    if (len >= 32767)
        return NULL;
    wchar_t* txt = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (txt == NULL)
        return NULL;
    if (name != NULL)
    {
        memmove(txt, path, l1 * sizeof(wchar_t));
        if (path[l1 - 1] != L'\\')
            txt[l1++] = L'\\';
        memmove(txt + l1, name, (l2 + 1) * sizeof(wchar_t));
    }
    else
        memmove(txt, path, (l1 + 1) * sizeof(wchar_t));
    return txt;
}

class BuildNameWTest : public ::testing::Test
{
protected:
    void ExpectBuildName(const wchar_t* path, const wchar_t* name, const wchar_t* expected)
    {
        wchar_t* result = BuildNameW_Standalone(path, name);
        ASSERT_NE(result, nullptr);
        EXPECT_STREQ(result, expected);
        free(result);
    }
};

// Basic path + name construction
TEST_F(BuildNameWTest, BasicPathPlusName)
{
    ExpectBuildName(L"C:\\Windows", L"System32", L"C:\\Windows\\System32");
}

TEST_F(BuildNameWTest, PathWithTrailingBackslash)
{
    ExpectBuildName(L"C:\\Windows\\", L"System32", L"C:\\Windows\\System32");
}

TEST_F(BuildNameWTest, NullNameCopiesPath)
{
    ExpectBuildName(L"C:\\Windows", nullptr, L"C:\\Windows");
}

TEST_F(BuildNameWTest, NullNameWithTrailingBackslash)
{
    ExpectBuildName(L"C:\\Windows\\", nullptr, L"C:\\Windows\\");
}

TEST_F(BuildNameWTest, RootPath)
{
    ExpectBuildName(L"C:\\", L"file.txt", L"C:\\file.txt");
}

TEST_F(BuildNameWTest, UNCPath)
{
    ExpectBuildName(L"\\\\server\\share", L"folder", L"\\\\server\\share\\folder");
}

TEST_F(BuildNameWTest, UNCPathWithTrailingBackslash)
{
    ExpectBuildName(L"\\\\server\\share\\", L"folder", L"\\\\server\\share\\folder");
}

TEST_F(BuildNameWTest, NestedPath)
{
    ExpectBuildName(L"C:\\a\\b\\c", L"d.txt", L"C:\\a\\b\\c\\d.txt");
}

TEST_F(BuildNameWTest, SingleCharName)
{
    ExpectBuildName(L"C:\\Dir", L"x", L"C:\\Dir\\x");
}

TEST_F(BuildNameWTest, EmptyName)
{
    ExpectBuildName(L"C:\\Dir", L"", L"C:\\Dir\\");
}

// Long path support — paths beyond MAX_PATH should work
TEST_F(BuildNameWTest, LongPathBeyondMAX_PATH)
{
    // Create a path longer than 260 chars
    std::wstring longDir = L"C:\\";
    while (longDir.size() < 300)
        longDir += L"LongDirectoryNameHere\\";
    std::wstring expected = longDir + L"file.txt";

    wchar_t* result = BuildNameW_Standalone(longDir.c_str(), L"file.txt");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(std::wstring(result), expected);
    free(result);
}

TEST_F(BuildNameWTest, VeryLongPathNearLimit)
{
    // Create a path near SAL_MAX_LONG_PATH (32767)
    std::wstring longDir = L"C:\\";
    while (longDir.size() < 32700)
        longDir += L"D\\";
    // Should succeed — under the limit
    wchar_t* result = BuildNameW_Standalone(longDir.c_str(), L"f.txt");
    ASSERT_NE(result, nullptr);
    free(result);
}

TEST_F(BuildNameWTest, PathExceedingLimitReturnsNull)
{
    // Create a path at or beyond SAL_MAX_LONG_PATH
    std::wstring longDir(32760, L'A');
    longDir = L"C:\\" + longDir;
    wchar_t* result = BuildNameW_Standalone(longDir.c_str(), L"extra.txt");
    EXPECT_EQ(result, nullptr);
}

// Unicode characters
TEST_F(BuildNameWTest, UnicodePathAndName)
{
    ExpectBuildName(L"C:\\Données", L"Ünïcödé.txt", L"C:\\Données\\Ünïcödé.txt");
}

TEST_F(BuildNameWTest, ChinesePath)
{
    ExpectBuildName(L"C:\\文件夹", L"文件.txt", L"C:\\文件夹\\文件.txt");
}

TEST_F(BuildNameWTest, EmojiInName)
{
    ExpectBuildName(L"C:\\Test", L"🎉.txt", L"C:\\Test\\🎉.txt");
}

// Extended-length prefix paths
TEST_F(BuildNameWTest, ExtendedLengthPrefix)
{
    ExpectBuildName(L"\\\\?\\C:\\Dir", L"file.txt", L"\\\\?\\C:\\Dir\\file.txt");
}

TEST_F(BuildNameWTest, ExtendedLengthPrefixUNC)
{
    ExpectBuildName(L"\\\\?\\UNC\\server\\share", L"file.txt", L"\\\\?\\UNC\\server\\share\\file.txt");
}
