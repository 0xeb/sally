#include <gtest/gtest.h>

#include "sftp_url.h"

TEST(SftpUrl, PlainUrlWithDefaultPort)
{
    EXPECT_EQ(BuildSftpUrl(L"test", L"example.com", 22, L"/home/test/a.txt"), L"sftp://test@example.com/home/test/a.txt");
}

TEST(SftpUrl, PortOtherThan22IsShown)
{
    EXPECT_EQ(BuildSftpUrl(L"test", L"localhost", 2222, L"/"), L"sftp://test@localhost:2222/");
}

TEST(SftpUrl, EmptyUserHasNoAt)
{
    EXPECT_EQ(BuildSftpUrl(L"", L"host", 22, L"/x"), L"sftp://host/x");
}

TEST(SftpUrl, Ipv6HostGetsBrackets)
{
    EXPECT_EQ(BuildSftpUrl(L"me", L"::1", 2222, L"/x"), L"sftp://me@[::1]:2222/x");
    EXPECT_EQ(BuildSftpUrl(L"me", L"fe80::1", 22, L"/"), L"sftp://me@[fe80::1]/");
}

TEST(SftpUrl, SpecialCharactersAreEncoded)
{
    EXPECT_EQ(BuildSftpUrl(L"a@b c", L"h", 22, L"/with space/100%/a#b?c"),
              L"sftp://a%40b%20c@h/with%20space/100%25/a%23b%3Fc");
}

TEST(SftpUrl, NonAsciiIsUtf8PercentEncoded)
{
    // U+0159 (r with caron) is C5 99 in UTF-8; U+4E2D is E4 B8 AD
    EXPECT_EQ(BuildSftpUrl(L"u", L"h", 22, L"/\x0159/\x4E2D"), L"sftp://u@h/%C5%99/%E4%B8%AD");
}

TEST(SftpUrl, RelativePathGetsASlash)
{
    EXPECT_EQ(BuildSftpUrl(L"u", L"h", 22, L""), L"sftp://u@h/");
    EXPECT_EQ(BuildSftpUrl(L"u", L"h", 22, L"x"), L"sftp://u@h/x");
}
