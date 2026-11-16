// Tests for ops/site_list (site list, name rules, validation) and the known_hosts path helpers.
#include <gtest/gtest.h>

#include <map>

#include "site_list.h"
#include "site_path.h"
#include "text_util.h"

namespace
{
CSite MakeSite(const std::wstring& name)
{
    CSite s;
    s.name = name;
    s.host = L"example.com";
    s.user = L"bob";
    return s;
}
} // namespace

TEST(SiteList, AddFindCaseInsensitive)
{
    CSiteList list;
    EXPECT_TRUE(list.Add(MakeSite(L"Work")));
    ASSERT_NE(list.Find(L"WORK"), nullptr);
    EXPECT_EQ(list.Find(L"work")->name, L"Work");
    EXPECT_EQ(list.Find(L"Other"), nullptr);
    EXPECT_EQ(list.IndexOf(L"wOrK"), 0);
}

TEST(SiteList, AddRejectsEmptyDuplicateAndInvalid)
{
    CSiteList list;
    EXPECT_TRUE(list.Add(MakeSite(L"Work")));
    EXPECT_FALSE(list.Add(MakeSite(L"")));
    EXPECT_FALSE(list.Add(MakeSite(L"work")));
    EXPECT_FALSE(list.Add(MakeSite(L"a/b")));
    EXPECT_EQ(list.Count(), 1u);
}

TEST(SiteList, RemoveKeepsOrder)
{
    CSiteList list;
    list.Add(MakeSite(L"A"));
    list.Add(MakeSite(L"B"));
    list.Add(MakeSite(L"C"));
    EXPECT_TRUE(list.Remove(L"b"));
    EXPECT_FALSE(list.Remove(L"B"));
    ASSERT_EQ(list.Count(), 2u);
    EXPECT_EQ(list.At(0).name, L"A");
    EXPECT_EQ(list.At(1).name, L"C");
}

TEST(SiteList, RenameRules)
{
    CSiteList list;
    list.Add(MakeSite(L"A"));
    list.Add(MakeSite(L"B"));
    EXPECT_FALSE(list.Rename(L"A", L"b"));  // taken by another site
    EXPECT_FALSE(list.Rename(L"A", L"x/y")); // invalid
    EXPECT_FALSE(list.Rename(L"Z", L"Q"));   // unknown
    EXPECT_TRUE(list.Rename(L"A", L"a"));    // only the letter case
    EXPECT_EQ(list.At(0).name, L"a");
    EXPECT_TRUE(list.Rename(L"a", L"Alpha"));
    EXPECT_EQ(list.At(0).name, L"Alpha");
}

TEST(SiteList, UpdateReplacesAndMayRename)
{
    CSiteList list;
    list.Add(MakeSite(L"A"));
    list.Add(MakeSite(L"B"));
    CSite changed = MakeSite(L"A");
    changed.host = L"other.org";
    EXPECT_TRUE(list.Update(L"a", changed));
    EXPECT_EQ(list.At(0).host, L"other.org");
    changed.name = L"B";
    EXPECT_FALSE(list.Update(L"A", changed)); // would clash with B
    EXPECT_EQ(list.At(0).name, L"A");
    changed.name = L"C";
    EXPECT_TRUE(list.Update(L"A", changed));
    EXPECT_EQ(list.At(0).name, L"C");
    EXPECT_FALSE(list.Update(L"missing", changed));
}

TEST(SiteList, MakeUniqueNameAndDuplicate)
{
    CSiteList list;
    EXPECT_EQ(list.MakeUniqueName(L"Name"), L"Name");
    EXPECT_EQ(list.MakeUniqueName(L""), L"Site");
    list.Add(MakeSite(L"Name"));
    EXPECT_EQ(list.MakeUniqueName(L"name"), L"name (2)");
    EXPECT_EQ(list.Duplicate(L"Name"), L"Name (2)");
    EXPECT_EQ(list.Duplicate(L"Name"), L"Name (3)");
    EXPECT_EQ(list.Duplicate(L"Missing"), L"");
    ASSERT_EQ(list.Count(), 3u);
    EXPECT_EQ(list.At(1).host, L"example.com");
}

TEST(SiteList, NameRulesMatchSitePath)
{
    EXPECT_EQ(ValidateSiteName(L""), SiteError::NameEmpty);
    EXPECT_EQ(ValidateSiteName(L"a/b"), SiteError::NameInvalid);
    EXPECT_EQ(ValidateSiteName(L"a\\b"), SiteError::NameInvalid);
    EXPECT_EQ(ValidateSiteName(L"@x"), SiteError::NameInvalid);
    EXPECT_EQ(ValidateSiteName(L" x"), SiteError::NameInvalid);
    EXPECT_EQ(ValidateSiteName(L"x "), SiteError::NameInvalid);
    EXPECT_EQ(ValidateSiteName(L"a\tb"), SiteError::NameInvalid);

    // every accepted name survives "@Name/path" through the path parser
    const wchar_t* good[] = {L"Work", L"my server (2)", L"host:22", L"a@b", L"café 中", L"x.y-z_1"};
    for (const wchar_t* name : good)
    {
        ASSERT_EQ(ValidateSiteName(name), SiteError::None) << name;
        CSftpPath parsed;
        ASSERT_TRUE(ParseSftpPath(std::wstring(L"@") + name + L"/home/x", parsed)) << name;
        EXPECT_TRUE(parsed.isSite);
        EXPECT_EQ(parsed.siteName, name);
        EXPECT_EQ(parsed.path, L"/home/x");
    }
}

TEST(SiteList, ValidateSiteFields)
{
    CSite s = MakeSite(L"ok");
    EXPECT_EQ(ValidateSite(s), SiteError::None);

    CSite t = s;
    t.host = L"";
    EXPECT_EQ(ValidateSite(t), SiteError::HostEmpty);
    for (const wchar_t* bad : {L"a b", L"a\tb", L"a/b", L"u@h", L"[::1]", L"a\\b"})
    {
        t = s;
        t.host = bad;
        EXPECT_EQ(ValidateSite(t), SiteError::HostInvalid) << bad;
    }
    t = s;
    t.host = L"::1"; // bare IPv6 is stored without brackets
    EXPECT_EQ(ValidateSite(t), SiteError::None);

    for (int port : {0, -1, 65536})
    {
        t = s;
        t.port = port;
        EXPECT_EQ(ValidateSite(t), SiteError::PortRange) << port;
    }
    t = s;
    t.port = 65535;
    EXPECT_EQ(ValidateSite(t), SiteError::None);

    t = s;
    t.user = L"";
    EXPECT_EQ(ValidateSite(t), SiteError::UserEmpty);
    t.user = L"a\nb";
    EXPECT_EQ(ValidateSite(t), SiteError::UserInvalid);
    t.user = L"domain@user"; // '@' is allowed in the user
    EXPECT_EQ(ValidateSite(t), SiteError::None);

    t = s;
    t.timeoutSeconds = 0;
    EXPECT_EQ(ValidateSite(t), SiteError::TimeoutRange);

    t = s;
    t.encoding = CSite::Encoding::CodePage;
    t.codePage = 0;
    EXPECT_EQ(ValidateSite(t), SiteError::CodePageInvalid);
    t.codePage = 1250;
    EXPECT_EQ(ValidateSite(t), SiteError::None);

    t = s;
    t.name = L"";
    EXPECT_EQ(ValidateSite(t), SiteError::NameEmpty);
}

TEST(SiteList, ValidateTransferSettings)
{
    CSite t = MakeSite(L"ok");
    EXPECT_EQ(ValidateSite(t), SiteError::None); // the defaults are valid
    for (int workers : {0, -1, MaxSiteWorkers + 1})
    {
        t.workers = workers;
        EXPECT_EQ(ValidateSite(t), SiteError::WorkersRange) << workers;
    }
    for (int workers : {1, MaxSiteWorkers})
    {
        t.workers = workers;
        EXPECT_EQ(ValidateSite(t), SiteError::None) << workers;
    }

    t = MakeSite(L"ok");
    t.uploadPermissions = MaxUploadPermissions + 1;
    EXPECT_EQ(ValidateSite(t), SiteError::None); // ignored while switched off
    t.setUploadPermissions = true;
    EXPECT_EQ(ValidateSite(t), SiteError::UploadPermissionsInvalid);
    t.uploadPermissions = MaxUploadPermissions;
    EXPECT_EQ(ValidateSite(t), SiteError::None);
    t.uploadPermissions = 0;
    EXPECT_EQ(ValidateSite(t), SiteError::None);
}

TEST(SiteList, TransferSettingsSurviveAddUpdateDuplicate)
{
    CSiteList list;
    CSite s = MakeSite(L"a");
    s.workers = 5;
    s.safeUpload = false;
    s.preserveTime = false;
    s.setUploadPermissions = true;
    s.uploadPermissions = 0600;
    ASSERT_TRUE(list.Add(s));
    const std::wstring copy = list.Duplicate(L"a");
    const CSite* c = list.Find(copy);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->workers, 5);
    EXPECT_FALSE(c->safeUpload);
    EXPECT_FALSE(c->preserveTime);
    EXPECT_TRUE(c->setUploadPermissions);
    EXPECT_EQ(c->uploadPermissions, 0600u);
}

TEST(KnownHostsPaths, OwnAndUserPaths)
{
    const std::wstring own = KnownHostsOwnPath();
    ASSERT_GT(own.size(), 20u);
    EXPECT_EQ(own[1], L':');
    EXPECT_EQ(own.substr(own.size() - 22), L"Sally\\SFTP\\known_hosts");
    EXPECT_EQ(own.substr(0, own.size() - 12), AppDataSallyDir());

    const std::wstring user = KnownHostsUserPath();
    ASSERT_GT(user.size(), 20u);
    EXPECT_EQ(user[1], L':');
    EXPECT_EQ(user.substr(user.size() - 17), L"\\.ssh\\known_hosts");
}

// The secrets map is keyed with SiteNameLess; it must agree with the list about which names are the same.
TEST(SiteList, NameKeyAgreesWithListEquality)
{
    const std::wstring names[] = {L"k",       L"K",        std::wstring(1, 0x212A) /* KELVIN SIGN */,
                                  L"i",       L"I",        std::wstring(1, 0x0130) /* I WITH DOT ABOVE */,
                                  std::wstring(1, 0x0131) /* DOTLESS I */, L"ß" /* sharp s */, L"SS", L"Work", L"work", L""};
    for (const std::wstring& a : names)
    {
        for (const std::wstring& b : names)
        {
            const bool sameKey = !SiteNameLess()(a, b) && !SiteNameLess()(b, a);
            EXPECT_EQ(SiteNamesEqual(a, b), sameKey);

            CSiteList list;
            if (list.Add(MakeSite(a)))
                EXPECT_EQ(list.Find(b) != nullptr, sameKey);
        }
    }
    std::map<std::wstring, int, SiteNameLess> secrets;
    secrets[L"Work"] = 1;
    EXPECT_EQ(secrets.count(L"WORK"), 1u);
    EXPECT_EQ(secrets.count(L"Other"), 0u);
}
