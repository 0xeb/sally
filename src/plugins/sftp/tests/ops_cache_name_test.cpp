#include <gtest/gtest.h>

#include "cache_name.h"

TEST(CacheName, IsDeterministicAndKeepsThePath)
{
    const std::wstring path = L"sftp:user@host/dir/File.TXT";
    EXPECT_EQ(BuildCacheName(path, 10, 1700000000), BuildCacheName(path, 10, 1700000000));
    EXPECT_EQ(BuildCacheName(path, 10, 1700000000), L"sftp:user@host/dir/File.TXT|10|1700000000");
}

TEST(CacheName, ChangesWithSizeOrTime)
{
    const std::wstring path = L"sftp:@Site/a";
    const std::wstring base = BuildCacheName(path, 10, 100);
    EXPECT_NE(base, BuildCacheName(path, 11, 100));
    EXPECT_NE(base, BuildCacheName(path, 10, 101));
}

TEST(CacheName, CaseIsPreserved)
{
    EXPECT_NE(BuildCacheName(L"sftp:h/A", 1, 1), BuildCacheName(L"sftp:h/a", 1, 1));
}

TEST(CacheName, NonAsciiAndSpecialCharacters)
{
    const std::wstring name = BuildCacheName(L"sftp:h/říž|x 中", 0, -5);
    EXPECT_EQ(name, L"sftp:h/říž|x 中|0|-5");
}

TEST(CacheName, LargeValues)
{
    EXPECT_EQ(BuildCacheName(L"p", 5368709120ull, 4102444800ll), L"p|5368709120|4102444800");
}
