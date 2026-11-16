#include <gtest/gtest.h>

#include <vector>

#include "hot_path.h"

namespace
{
// Walks the way Sally does and returns the shortened paths in the order found (the whole path is not included).
std::vector<std::wstring> Walk(const std::wstring& text)
{
    std::vector<std::wstring> cuts;
    const int pathLen = static_cast<int>(text.size());
    int offset = 0;
    while (NextHotPath(text.c_str(), pathLen, offset) && offset < pathLen)
        cuts.push_back(text.substr(0, offset));
    return cuts;
}
} // namespace

TEST(HotPath, WalkOfASitePath)
{
    const std::vector<std::wstring> expected = {L"sftp:@test/", L"sftp:@test/home", L"sftp:@test/home/test"};
    EXPECT_EQ(Walk(L"sftp:@test/home/test/fixtures"), expected);
}

TEST(HotPath, WalkWithUserHostAndPort)
{
    const std::vector<std::wstring> expected = {L"sftp:me@host:2222/", L"sftp:me@host:2222/a"};
    EXPECT_EQ(Walk(L"sftp:me@host:2222/a/b"), expected);
}

TEST(HotPath, Ipv6HostColonsDoNotEndTheRoot)
{
    const std::vector<std::wstring> expected = {L"sftp:me@[::1]:22/"};
    EXPECT_EQ(Walk(L"sftp:me@[::1]:22/x"), expected);
}

TEST(HotPath, RootOnlyHasNothingToCut)
{
    EXPECT_TRUE(Walk(L"sftp:@test/").empty());
    EXPECT_TRUE(Walk(L"sftp:@test").empty());
}

TEST(HotPath, TrailingSlashAndDoubleSlashes)
{
    EXPECT_EQ(Walk(L"sftp:@s/a/b/"), (std::vector<std::wstring>{L"sftp:@s/", L"sftp:@s/a", L"sftp:@s/a/b"}));
    EXPECT_EQ(Walk(L"sftp:@s/a/"), (std::vector<std::wstring>{L"sftp:@s/", L"sftp:@s/a"}));
    // a double slash gives an empty part: the walk still ends
    const std::vector<std::wstring> cuts = Walk(L"sftp:@s/a//b");
    ASSERT_FALSE(cuts.empty());
    EXPECT_EQ(cuts.front(), L"sftp:@s/");
}

TEST(HotPath, NonAsciiNames)
{
    const std::wstring text = L"sftp:@s/\x0159\x00ED\x017E/\x4E2D\x6587";
    const std::vector<std::wstring> expected = {L"sftp:@s/", L"sftp:@s/\x0159\x00ED\x017E"};
    EXPECT_EQ(Walk(text), expected);
}

TEST(HotPath, TheFilterAfterThePathIsIgnored)
{
    const std::wstring text = L"sftp:@s/a/b*.txt"; // pathLen covers only "sftp:@s/a"
    int offset = 0;
    const int pathLen = 9;
    ASSERT_TRUE(NextHotPath(text.c_str(), pathLen, offset));
    EXPECT_EQ(offset, 8);
    ASSERT_TRUE(NextHotPath(text.c_str(), pathLen, offset));
    EXPECT_EQ(offset, pathLen);
    EXPECT_FALSE(NextHotPath(text.c_str(), pathLen, offset));
}

TEST(HotPath, BadInput)
{
    int offset = 0;
    EXPECT_FALSE(NextHotPath(nullptr, 5, offset));
    EXPECT_FALSE(NextHotPath(L"", 0, offset));
    offset = -1;
    EXPECT_FALSE(NextHotPath(L"sftp:@s/a", 9, offset));
}

TEST(HotPath, CompleteAddsTheRootSlash)
{
    std::wstring a = L"sftp:@test";
    CompleteHotPath(a);
    EXPECT_EQ(a, L"sftp:@test/");
    std::wstring b = L"sftp:me@host:2222";
    CompleteHotPath(b);
    EXPECT_EQ(b, L"sftp:me@host:2222/");
}

TEST(HotPath, CompleteLeavesCompletePathsAlone)
{
    for (const wchar_t* text : {L"sftp:@test/", L"sftp:@test/home", L"sftp:", L"nothing"})
    {
        std::wstring path = text;
        CompleteHotPath(path);
        EXPECT_EQ(path, text);
    }
}
