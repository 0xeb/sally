#include "test_support.h"

#include "ssh_version.h"

TEST(Core, LibraryVersions)
{
    const std::string text = SshLibraryVersions();
    EXPECT_NE(text.find("libssh2"), std::string::npos) << text;
    EXPECT_NE(text.find("OpenSSL"), std::string::npos) << text;
}

TEST(Server, Reachable)
{
    REQUIRE_SERVER();
    EXPECT_TRUE(sftptest::ServerReachable(sftptest::kPort));
}

TEST(Server, NoSftpPortReachable)
{
    REQUIRE_SERVER();
    EXPECT_TRUE(sftptest::ServerReachable(sftptest::kPortNoSftp));
}
