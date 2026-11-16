// Tests for ops/site_edit: form <-> site conversion, error placement, list strings, quick connect parsing.
#include <gtest/gtest.h>

#include "site_edit.h"

namespace
{
CSiteForm ValidForm()
{
    CSite s;
    s.name = L"Work";
    s.host = L"example.com";
    s.user = L"bob";
    return FormFromSite(s);
}
} // namespace

TEST(SiteEdit, RoundTrip)
{
    CSite in;
    in.name = L"Work";
    in.host = L"example.com";
    in.port = 2222;
    in.user = L"bob";
    in.keyFile = L"C:\\keys\\id";
    in.useAgent = true;
    in.compression = true;
    in.timeoutSeconds = 45;
    in.encoding = CSite::Encoding::CodePage;
    in.codePage = 1250;
    in.initialRemotePath = L"/srv";
    in.initialLocalPath = L"D:\\data";
    in.rememberPassword = true;
    in.workers = 6;
    in.safeUpload = false;
    in.preserveTime = false;
    in.setUploadPermissions = true;
    in.uploadPermissions = 0750;

    CSite out;
    ASSERT_EQ(SiteFromForm(FormFromSite(in), out), SiteError::None);
    EXPECT_EQ(out.workers, 6);
    EXPECT_FALSE(out.safeUpload);
    EXPECT_FALSE(out.preserveTime);
    EXPECT_TRUE(out.setUploadPermissions);
    EXPECT_EQ(out.uploadPermissions, 0750u);
    EXPECT_EQ(out.name, in.name);
    EXPECT_EQ(out.host, in.host);
    EXPECT_EQ(out.port, 2222);
    EXPECT_EQ(out.user, in.user);
    EXPECT_EQ(out.keyFile, in.keyFile);
    EXPECT_TRUE(out.useAgent);
    EXPECT_TRUE(out.compression);
    EXPECT_EQ(out.timeoutSeconds, 45);
    EXPECT_EQ(out.encoding, CSite::Encoding::CodePage);
    EXPECT_EQ(out.codePage, 1250u);
    EXPECT_EQ(out.initialRemotePath, L"/srv");
    EXPECT_EQ(out.initialLocalPath, L"D:\\data");
    EXPECT_TRUE(out.rememberPassword);
}

TEST(SiteEdit, TrimsTextFields)
{
    CSiteForm f = ValidForm();
    f.name = L"  Work ";
    f.host = L" example.com ";
    f.user = L" bob";
    f.port = L" 22 ";
    CSite out;
    ASSERT_EQ(SiteFromForm(f, out), SiteError::None);
    EXPECT_EQ(out.name, L"Work");
    EXPECT_EQ(out.host, L"example.com");
    EXPECT_EQ(out.user, L"bob");
}

TEST(SiteEdit, ErrorsPerField)
{
    CSite out;
    CSiteForm f = ValidForm();
    f.name = L"";
    EXPECT_EQ(SiteFromForm(f, out), SiteError::NameEmpty);
    f = ValidForm();
    f.name = L"a/b";
    EXPECT_EQ(SiteFromForm(f, out), SiteError::NameInvalid);
    f = ValidForm();
    f.host = L"";
    EXPECT_EQ(SiteFromForm(f, out), SiteError::HostEmpty);
    f = ValidForm();
    f.host = L"a b";
    EXPECT_EQ(SiteFromForm(f, out), SiteError::HostInvalid);
    f = ValidForm();
    f.user = L"";
    EXPECT_EQ(SiteFromForm(f, out), SiteError::UserEmpty);
    for (const wchar_t* bad : {L"", L"0", L"65536", L"abc", L"-1", L"22x", L"99999999999"})
    {
        f = ValidForm();
        f.port = bad;
        EXPECT_EQ(SiteFromForm(f, out), SiteError::PortRange) << bad;
    }
    for (const wchar_t* bad : {L"", L"0", L"3601", L"x"})
    {
        f = ValidForm();
        f.timeout = bad;
        EXPECT_EQ(SiteFromForm(f, out), SiteError::TimeoutRange) << bad;
    }
}

TEST(SiteEdit, CodePageOnlyCheckedWhenChosen)
{
    CSite out;
    CSiteForm f = ValidForm();
    f.codePage = L"junk";
    EXPECT_EQ(SiteFromForm(f, out), SiteError::None); // UTF-8: the code page text is ignored
    EXPECT_EQ(out.codePage, 1252u);

    f.encoding = CSite::Encoding::CodePage;
    EXPECT_EQ(SiteFromForm(f, out), SiteError::CodePageInvalid);
    f.codePage = L"0";
    EXPECT_EQ(SiteFromForm(f, out), SiteError::CodePageInvalid);
    f.codePage = L"1250";
    EXPECT_EQ(SiteFromForm(f, out), SiteError::None);
    EXPECT_EQ(out.codePage, 1250u);
}

TEST(SiteEdit, TransferSettingsDefaultsAndForm)
{
    const CSiteForm f = ValidForm();
    EXPECT_EQ(f.workers, L"2");
    EXPECT_TRUE(f.safeUpload);
    EXPECT_TRUE(f.preserveTime);
    EXPECT_FALSE(f.setUploadPermissions);
    EXPECT_EQ(f.uploadPermissions, L"644"); // shown in octal
}

TEST(SiteEdit, WorkersRange)
{
    CSite out;
    for (const wchar_t* bad : {L"", L"0", L"9", L"x", L"-1", L"2.5"})
    {
        CSiteForm f = ValidForm();
        f.workers = bad;
        EXPECT_EQ(SiteFromForm(f, out), SiteError::WorkersRange) << bad;
    }
    for (const wchar_t* good : {L"1", L" 8 "})
    {
        CSiteForm f = ValidForm();
        f.workers = good;
        EXPECT_EQ(SiteFromForm(f, out), SiteError::None) << good;
    }
}

TEST(SiteEdit, UploadPermissionsOnlyCheckedWhenOn)
{
    CSite out;
    CSiteForm f = ValidForm();
    f.uploadPermissions = L"junk";
    EXPECT_EQ(SiteFromForm(f, out), SiteError::None); // off: the text is ignored
    EXPECT_EQ(out.uploadPermissions, 0644u);

    f.setUploadPermissions = true;
    for (const wchar_t* bad : {L"", L"junk", L"8", L"648", L"10000", L"-1"})
    {
        f.uploadPermissions = bad;
        EXPECT_EQ(SiteFromForm(f, out), SiteError::UploadPermissionsInvalid) << bad;
    }
    f.uploadPermissions = L"7777";
    ASSERT_EQ(SiteFromForm(f, out), SiteError::None);
    EXPECT_EQ(out.uploadPermissions, 07777u);
    f.uploadPermissions = L" 0755 ";
    ASSERT_EQ(SiteFromForm(f, out), SiteError::None);
    EXPECT_EQ(out.uploadPermissions, 0755u);
    f.uploadPermissions = L"0";
    ASSERT_EQ(SiteFromForm(f, out), SiteError::None);
    EXPECT_EQ(out.uploadPermissions, 0u);
    EXPECT_TRUE(out.setUploadPermissions);
}

TEST(SiteEdit, FieldAndTabOfTransferErrors)
{
    EXPECT_EQ(FieldOfError(SiteError::WorkersRange), SiteField::Workers);
    EXPECT_EQ(FieldOfError(SiteError::UploadPermissionsInvalid), SiteField::UploadPermissions);
    EXPECT_EQ(TabOfField(SiteField::Workers), SiteTabConnection);
    EXPECT_EQ(TabOfField(SiteField::UploadPermissions), SiteTabConnection);
}

TEST(SiteEdit, FailureLeavesOutUnchanged)
{
    CSite out;
    out.name = L"Keep";
    CSiteForm f = ValidForm();
    f.host = L"";
    EXPECT_NE(SiteFromForm(f, out), SiteError::None);
    EXPECT_EQ(out.name, L"Keep");
}

TEST(SiteEdit, FieldAndTabOfError)
{
    EXPECT_EQ(FieldOfError(SiteError::NameInvalid), SiteField::Name);
    EXPECT_EQ(FieldOfError(SiteError::HostInvalid), SiteField::Host);
    EXPECT_EQ(FieldOfError(SiteError::PortRange), SiteField::Port);
    EXPECT_EQ(FieldOfError(SiteError::UserInvalid), SiteField::User);
    EXPECT_EQ(FieldOfError(SiteError::TimeoutRange), SiteField::Timeout);
    EXPECT_EQ(FieldOfError(SiteError::CodePageInvalid), SiteField::CodePage);
    EXPECT_EQ(TabOfField(SiteField::Port), SiteTabSession);
    EXPECT_EQ(TabOfField(SiteField::Timeout), SiteTabConnection);
    EXPECT_EQ(TabOfField(SiteField::CodePage), SiteTabConnection);
}

TEST(SiteEdit, HostColumn)
{
    CSite s;
    s.host = L"example.com";
    EXPECT_EQ(SiteHostColumn(s), L"example.com");
    s.port = 2222;
    EXPECT_EQ(SiteHostColumn(s), L"example.com:2222");
    s.host = L"::1";
    s.port = 22;
    EXPECT_EQ(SiteHostColumn(s), L"[::1]");
}

TEST(SiteEdit, QuickConnect)
{
    CSftpPath p;
    ASSERT_TRUE(ParseQuickConnect(L" bob@example.com:2222/srv/x ", p));
    EXPECT_EQ(p.user, L"bob");
    EXPECT_EQ(p.host, L"example.com");
    EXPECT_EQ(p.port, 2222);
    EXPECT_EQ(p.path, L"/srv/x");
    ASSERT_TRUE(ParseQuickConnect(L"SFTP:bob@example.com", p));
    EXPECT_EQ(p.port, 22);
    EXPECT_EQ(p.path, L"/");

    CSftpPath keep = p;
    EXPECT_FALSE(ParseQuickConnect(L"", p));
    EXPECT_FALSE(ParseQuickConnect(L"example.com", p));
    EXPECT_FALSE(ParseQuickConnect(L"@Work", p));
    EXPECT_FALSE(ParseQuickConnect(L"bob@host:99999", p));
    EXPECT_EQ(p.host, keep.host);
}

TEST(SiteEdit, ConnectPath)
{
    EXPECT_EQ(SiteConnectPath(L"Work"), L"@Work/");
    CSftpPath back;
    ASSERT_TRUE(ParseSftpPath(SiteConnectPath(L"My Site"), back));
    EXPECT_TRUE(back.isSite);
    EXPECT_EQ(back.siteName, L"My Site");
}

TEST(SiteEdit, ForgetSecretsRules)
{
    EXPECT_FALSE(ShouldClearSecrets(true, false)); // remember on, nothing ticked: keep (or store typed)
    EXPECT_TRUE(ShouldClearSecrets(true, true));   // forget wins even while remember stays ticked
    EXPECT_TRUE(ShouldClearSecrets(false, false)); // remember off drops them
    EXPECT_TRUE(ShouldClearSecrets(false, true));
}
