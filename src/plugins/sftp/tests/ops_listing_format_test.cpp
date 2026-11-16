// Tests for ops/listing_format (permission text, times, extension split, remote path helpers).
#include <gtest/gtest.h>

#include "listing_format.h"
#include "site.h"

TEST(ListingFormat, PermissionsOfCommonFiles)
{
    EXPECT_EQ(L"-rw-r--r--", FormatPermissions(0100644, false, false));
    EXPECT_EQ(L"drwxr-xr-x", FormatPermissions(0040755, true, false));
    EXPECT_EQ(L"lrwxrwxrwx", FormatPermissions(0120777, false, true));
    EXPECT_EQ(L"-rwx------", FormatPermissions(0100700, false, false));
    EXPECT_EQ(L"----------", FormatPermissions(0100000, false, false));
}

TEST(ListingFormat, PermissionsSpecialBits)
{
    EXPECT_EQ(L"-rwsr-xr-x", FormatPermissions(0104755, false, false));
    EXPECT_EQ(L"-rwSr--r--", FormatPermissions(0104644, false, false));
    EXPECT_EQ(L"-rwxr-sr-x", FormatPermissions(0102755, false, false));
    EXPECT_EQ(L"drwxrwxrwt", FormatPermissions(0041777, true, false));
    EXPECT_EQ(L"drwxrwxrwT", FormatPermissions(0041776, true, false));
}

TEST(ListingFormat, PermissionsOtherTypesAndMissingTypeBits)
{
    EXPECT_EQ(L"crw-rw-rw-", FormatPermissions(0020666, false, false));
    EXPECT_EQ(L"brw-rw----", FormatPermissions(0060660, false, false));
    EXPECT_EQ(L"prw-r--r--", FormatPermissions(0010644, false, false));
    EXPECT_EQ(L"srwxr-xr-x", FormatPermissions(0140755, false, false));
    // no type bits: the flags decide
    EXPECT_EQ(L"drwxr-xr-x", FormatPermissions(0755, true, false));
    EXPECT_EQ(L"lrwxr-xr-x", FormatPermissions(0755, false, true));
    EXPECT_EQ(L"-rw-r--r--", FormatPermissions(0644, false, false));
}

TEST(ListingFormat, UnixTimeToFileTime)
{
    const FILETIME epoch = UnixTimeToFileTime(0);
    const uint64_t epochTicks = (static_cast<uint64_t>(epoch.dwHighDateTime) << 32) | epoch.dwLowDateTime;
    EXPECT_EQ(116444736000000000ULL, epochTicks);

    const FILETIME t = UnixTimeToFileTime(1700000000);
    const uint64_t ticks = (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    EXPECT_EQ((1700000000ULL + 11644473600ULL) * 10000000ULL, ticks);

    const FILETIME before1601 = UnixTimeToFileTime(-20000000000LL);
    EXPECT_EQ(0u, before1601.dwLowDateTime);
    EXPECT_EQ(0u, before1601.dwHighDateTime);

    // dates before 1970 are fine
    const FILETIME old = UnixTimeToFileTime(-86400);
    const uint64_t oldTicks = (static_cast<uint64_t>(old.dwHighDateTime) << 32) | old.dwLowDateTime;
    EXPECT_EQ(116444736000000000ULL - 864000000000ULL, oldTicks);
}

TEST(ListingFormat, FileTimeToUnixTime)
{
    EXPECT_EQ(0, FileTimeToUnixTime(UnixTimeToFileTime(0)));
    EXPECT_EQ(1700000000, FileTimeToUnixTime(UnixTimeToFileTime(1700000000)));
    EXPECT_EQ(-86400, FileTimeToUnixTime(UnixTimeToFileTime(-86400)));
    const FILETIME year1601 = {0, 0};
    EXPECT_EQ(-11644473600LL, FileTimeToUnixTime(year1601));
}

TEST(ListingFormat, ExtensionOffset)
{
    EXPECT_EQ(2u, ExtensionOffset(L"a.txt"));
    EXPECT_EQ(5u, ExtensionOffset(L"noext"));
    EXPECT_EQ(8u, ExtensionOffset(L"a.b.tar.gz"));
    EXPECT_EQ(1u, ExtensionOffset(L".bashrc"));
    EXPECT_EQ(2u, ExtensionOffset(L"x."));
    EXPECT_EQ(0u, ExtensionOffset(L""));
}

TEST(ListingFormat, HiddenAndAttributes)
{
    CSftpEntry e;
    e.rawName = ".profile";
    EXPECT_TRUE(IsHiddenName(e.rawName));
    EXPECT_EQ(static_cast<DWORD>(FILE_ATTRIBUTE_HIDDEN), EntryAttributes(e));

    e.rawName = "docs";
    e.isDir = true;
    EXPECT_EQ(static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY), EntryAttributes(e));
    EXPECT_FALSE(IsHiddenName(e.rawName));

    CSftpEntry link;
    link.rawName = "l";
    link.isLink = true;
    link.linkTargetIsDir = true;
    EXPECT_TRUE(EntryIsDirectory(link));
    EXPECT_EQ(static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT), EntryAttributes(link));

    link.linkTargetIsDir = false;
    EXPECT_FALSE(EntryIsDirectory(link));
    EXPECT_EQ(static_cast<DWORD>(FILE_ATTRIBUTE_REPARSE_POINT), EntryAttributes(link));

    CSftpEntry plain;
    plain.rawName = "f";
    EXPECT_EQ(static_cast<DWORD>(FILE_ATTRIBUTE_NORMAL), EntryAttributes(plain));
}

TEST(ListingFormat, CompareRawNames)
{
    EXPECT_EQ(0, CompareRawNames("a", "a"));
    EXPECT_LT(CompareRawNames("a", "b"), 0);
    EXPECT_GT(CompareRawNames("b", "a"), 0);
    EXPECT_NE(0, CompareRawNames("caf\xE9", "caf\xC3\xA9")); // different bytes never compare equal
}

TEST(ListingFormat, NormalizeRemotePath)
{
    EXPECT_EQ("/", NormalizeRemotePath(""));
    EXPECT_EQ("/", NormalizeRemotePath("/"));
    EXPECT_EQ("/", NormalizeRemotePath("///"));
    EXPECT_EQ("/a/b", NormalizeRemotePath("/a//b/"));
    EXPECT_EQ("/b", NormalizeRemotePath("/a/../b"));
    EXPECT_EQ("/", NormalizeRemotePath("/.."));
    EXPECT_EQ("/a", NormalizeRemotePath("/a/./."));
    EXPECT_EQ("/a/b", NormalizeRemotePath("a/b"));
}

TEST(ListingFormat, ParentAndLast)
{
    EXPECT_EQ("/", ParentRemotePath("/"));
    EXPECT_EQ("/", ParentRemotePath("/a"));
    EXPECT_EQ("/a", ParentRemotePath("/a/b"));
    EXPECT_EQ("/a", ParentRemotePath("/a/b/"));
    EXPECT_EQ("", LastRemoteComponent("/"));
    EXPECT_EQ("b", LastRemoteComponent("/a/b"));
    EXPECT_TRUE(IsRootRemotePath("/"));
    EXPECT_TRUE(IsRootRemotePath("/a/.."));
    EXPECT_FALSE(IsRootRemotePath("/a"));
}

TEST(ListingFormat, JoinAndResolve)
{
    EXPECT_EQ("/a/b", JoinRemotePath("/a", "b"));
    EXPECT_EQ("/b", JoinRemotePath("/", "b"));
    EXPECT_EQ("/a/caf\xC3\xA9", JoinRemotePath("/a/", "caf\xC3\xA9"));

    EXPECT_EQ("/a", ResolveRemotePath("/a", ""));
    EXPECT_EQ("/x/y", ResolveRemotePath("/a", "/x/y/"));
    EXPECT_EQ("/a/x", ResolveRemotePath("/a", "x"));
    EXPECT_EQ("/", ResolveRemotePath("/a", ".."));
    EXPECT_EQ("/c", ResolveRemotePath("/a/b", "../../c"));
}

TEST(ListingFormat, PathLevelErrors)
{
    EXPECT_TRUE(IsPathLevelError(SshErrorKind::NotFound));
    EXPECT_TRUE(IsPathLevelError(SshErrorKind::PermissionDenied));
    EXPECT_TRUE(IsPathLevelError(SshErrorKind::Other));
    EXPECT_FALSE(IsPathLevelError(SshErrorKind::Cancelled));
    EXPECT_FALSE(IsPathLevelError(SshErrorKind::ConnectFailed));
    EXPECT_FALSE(IsPathLevelError(SshErrorKind::Timeout));
    EXPECT_FALSE(IsPathLevelError(SshErrorKind::AuthFailed));
    EXPECT_FALSE(IsPathLevelError(SshErrorKind::HostKeyRejected));
    EXPECT_FALSE(IsPathLevelError(SshErrorKind::None));
}

TEST(ListingFormat, WindowTitle)
{
    const std::wstring prefix = L"sftp:u@h";
    EXPECT_EQ(L"sftp:u@h/", FormatWindowTitlePath(prefix, L"/", 1));
    EXPECT_EQ(L"sftp:u@h/", FormatWindowTitlePath(prefix, L"/", 2));
    EXPECT_EQ(L"c", FormatWindowTitlePath(prefix, L"/a/b/c", 1));
    EXPECT_EQ(L"sftp:u@h/.../c", FormatWindowTitlePath(prefix, L"/a/b/c", 2));
    EXPECT_EQ(L"sftp:u@h/a", FormatWindowTitlePath(prefix, L"/a", 2));
    EXPECT_EQ(L"sftp:u@h/a/b", FormatWindowTitlePath(prefix, L"/a/b", 0));
    EXPECT_EQ(L"b", FormatWindowTitlePath(prefix, L"/a/b/", 1));
}

TEST(SiteHelpers, PasswordPromptNeeded)
{
    CSite site;
    EXPECT_TRUE(SiteNeedsPasswordPrompt(site, false));
    EXPECT_FALSE(SiteNeedsPasswordPrompt(site, true));
    site.useAgent = true;
    EXPECT_FALSE(SiteNeedsPasswordPrompt(site, false));
    site.useAgent = false;
    site.keyFile = L"C:/keys/id";
    EXPECT_FALSE(SiteNeedsPasswordPrompt(site, false));
}
