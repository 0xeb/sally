// Tests for ops/site_path (path parser/formatter) and ops/site (CSite -> CSshSite).
#include <gtest/gtest.h>

#include "site.h"
#include "site_path.h"

namespace
{
CSftpPath MustParse(const std::wstring& text)
{
    CSftpPath p;
    EXPECT_TRUE(ParseSftpPath(text, p)) ;
    return p;
}
} // namespace

TEST(SitePath, SplitFsPath)
{
    std::wstring userPart;
    EXPECT_TRUE(SplitFsPath(L"sftp:user@host/dir", L"sftp", userPart));
    EXPECT_EQ(L"user@host/dir", userPart);
    EXPECT_TRUE(SplitFsPath(L"SFTP:@Site/a", L"sftp", userPart)); // the name is not case sensitive
    EXPECT_EQ(L"@Site/a", userPart);
    EXPECT_FALSE(SplitFsPath(L"sftp:", L"sftp", userPart)); // nothing after the colon
    EXPECT_FALSE(SplitFsPath(L"c:\\dir", L"sftp", userPart));
    EXPECT_FALSE(SplitFsPath(L"sftpx:user@host/", L"sftp", userPart));
    EXPECT_FALSE(SplitFsPath(L"sftp", L"sftp", userPart));
    EXPECT_FALSE(SplitFsPath(L"sftp:a", L"", userPart));
}

TEST(SitePath, IsPathWithin)
{
    EXPECT_TRUE(IsPathWithin(L"/a/b", L"/a/b", false));
    EXPECT_FALSE(IsPathWithin(L"/a/b/c", L"/a/b", false));
    EXPECT_TRUE(IsPathWithin(L"/a/b/c", L"/a/b", true));
    EXPECT_FALSE(IsPathWithin(L"/a/bc", L"/a/b", true)); // a longer name is not a subdirectory
    EXPECT_FALSE(IsPathWithin(L"/a", L"/a/b", true));    // a parent is not within
    EXPECT_TRUE(IsPathWithin(L"/anything/deep", L"/", true));
    EXPECT_FALSE(IsPathWithin(L"/anything", L"/", false));
    EXPECT_TRUE(IsPathWithin(L"/", L"/", false));
}

TEST(SitePath, UserHostDefaults)
{
    CSftpPath p = MustParse(L"bob@example.com");
    EXPECT_FALSE(p.isSite);
    EXPECT_EQ(p.user, L"bob");
    EXPECT_EQ(p.host, L"example.com");
    EXPECT_EQ(p.port, 22);
    EXPECT_EQ(p.path, L"/");
}

TEST(SitePath, PortAndPath)
{
    CSftpPath p = MustParse(L"bob@example.com:2222/home/bob/x y");
    EXPECT_EQ(p.port, 2222);
    EXPECT_EQ(p.path, L"/home/bob/x y");
    p = MustParse(L"bob@example.com/var");
    EXPECT_EQ(p.port, 22);
    EXPECT_EQ(p.path, L"/var");
}

TEST(SitePath, Ipv6)
{
    CSftpPath p = MustParse(L"u@[::1]:2222/tmp");
    EXPECT_EQ(p.host, L"::1");
    EXPECT_EQ(p.port, 2222);
    EXPECT_EQ(p.path, L"/tmp");
    p = MustParse(L"u@[fe80::1]");
    EXPECT_EQ(p.host, L"fe80::1");
    EXPECT_EQ(p.port, 22);
    EXPECT_EQ(FormatSftpPath(p), L"u@[fe80::1]/");
    CSftpPath bad;
    EXPECT_FALSE(ParseSftpPath(L"u@::1/x", bad)); // unbracketed IPv6
    EXPECT_FALSE(ParseSftpPath(L"u@[::1/x", bad));
    EXPECT_FALSE(ParseSftpPath(L"u@[]/x", bad));
    EXPECT_FALSE(ParseSftpPath(L"u@[::1]x/x", bad));
}

TEST(SitePath, AtSignInUserName)
{
    CSftpPath p = MustParse(L"me@mail.org@host.net:2022/data@home");
    EXPECT_EQ(p.user, L"me@mail.org");
    EXPECT_EQ(p.host, L"host.net");
    EXPECT_EQ(p.port, 2022);
    EXPECT_EQ(p.path, L"/data@home"); // '@' in the path does not move the split
}

TEST(SitePath, PortRange)
{
    CSftpPath p;
    EXPECT_FALSE(ParseSftpPath(L"u@h:0/x", p));
    EXPECT_FALSE(ParseSftpPath(L"u@h:65536/x", p));
    EXPECT_FALSE(ParseSftpPath(L"u@h:abc/x", p));
    EXPECT_FALSE(ParseSftpPath(L"u@h:/x", p));
    EXPECT_FALSE(ParseSftpPath(L"u@h:-1/x", p));
    EXPECT_FALSE(ParseSftpPath(L"u@h:123456/x", p));
    EXPECT_EQ(MustParse(L"u@h:1/x").port, 1);
    EXPECT_EQ(MustParse(L"u@h:65535/x").port, 65535);
}

TEST(SitePath, Invalid)
{
    CSftpPath p;
    p.user = L"keep";
    EXPECT_FALSE(ParseSftpPath(L"", p));
    EXPECT_FALSE(ParseSftpPath(L"/", p));
    EXPECT_FALSE(ParseSftpPath(L"host/path", p)); // no user
    EXPECT_FALSE(ParseSftpPath(L"u@/path", p));   // no host
    EXPECT_FALSE(ParseSftpPath(L"@/path", p));    // no site name
    CSftpPath before;
    before.user = L"keep";
    CSftpPath q = before;
    EXPECT_FALSE(ParseSftpPath(L"u@h:0", q));
    EXPECT_EQ(q.user, L"keep"); // unchanged on failure
}

TEST(SitePath, SiteReference)
{
    CSftpPath p = MustParse(L"@My Site/a/b");
    EXPECT_TRUE(p.isSite);
    EXPECT_EQ(p.siteName, L"My Site");
    EXPECT_EQ(p.path, L"/a/b");
    p = MustParse(L"@Site");
    EXPECT_EQ(p.path, L"/");
    EXPECT_EQ(FormatSftpPath(p), L"@Site/");
}

TEST(SitePath, TrailingSlashes)
{
    EXPECT_EQ(MustParse(L"u@h/a/b/").path, L"/a/b");
    EXPECT_EQ(MustParse(L"u@h/a/b///").path, L"/a/b");
    EXPECT_EQ(MustParse(L"u@h///").path, L"/");
    EXPECT_EQ(MustParse(L"@S/x/").path, L"/x");
}

TEST(SitePath, FormatOmitsDefaultPort)
{
    EXPECT_EQ(FormatSftpPath(MustParse(L"u@h:22/x")), L"u@h/x");
    EXPECT_EQ(FormatSftpPath(MustParse(L"u@h:2222/x")), L"u@h:2222/x");
    EXPECT_EQ(FormatSftpPath(MustParse(L"u@[::1]:2222")), L"u@[::1]:2222/");
}

TEST(SitePath, RoundTrip)
{
    const wchar_t* texts[] = {L"u@h/",          L"u@h:2222/a/b",  L"a@b@c.d:1/x", L"u@[::1]:65535/p q",
                              L"@Site/",        L"@My Site/a/b",  L"u@h/dir"};
    for (const wchar_t* t : texts)
    {
        CSftpPath a = MustParse(t);
        std::wstring f = FormatSftpPath(a);
        EXPECT_EQ(f, t);
        CSftpPath b = MustParse(f);
        EXPECT_TRUE(SameServer(a, b));
        EXPECT_EQ(a.path, b.path);
    }
}

TEST(SitePath, RootOf)
{
    CSftpPath r = RootOf(MustParse(L"u@h:2222/a/b"));
    EXPECT_EQ(FormatSftpPath(r), L"u@h:2222/");
    EXPECT_EQ(FormatSftpPath(RootOf(MustParse(L"@S/a"))), L"@S/");
}

TEST(SitePath, SameServer)
{
    EXPECT_TRUE(SameServer(MustParse(L"u@Host.COM/a"), MustParse(L"u@host.com:22/b")));
    EXPECT_FALSE(SameServer(MustParse(L"u@h/a"), MustParse(L"U@h/a"))); // user names are case sensitive
    EXPECT_FALSE(SameServer(MustParse(L"u@h/a"), MustParse(L"u@h:2222/a")));
    EXPECT_FALSE(SameServer(MustParse(L"u@h/a"), MustParse(L"u@h2/a")));
    EXPECT_TRUE(SameServer(MustParse(L"@Site/a"), MustParse(L"@SITE/b")));
    EXPECT_FALSE(SameServer(MustParse(L"@Site/a"), MustParse(L"@Other/a")));
    EXPECT_FALSE(SameServer(MustParse(L"@h/a"), MustParse(L"u@h/a")));
}

TEST(Site, ToSshSiteConvertsFields)
{
    CSite site;
    site.name = L"Work";
    site.host = L"host.\u00e9xample";
    site.port = 2200;
    site.user = L"j\u00fcrgen";
    site.keyFile = L"C:/keys/id_ed25519";
    site.useAgent = true;
    site.compression = true;
    site.timeoutSeconds = 7;
    site.encoding = CSite::Encoding::CodePage;
    site.codePage = 1250;

    CSshSite out = ToSshSite(site, CSecret("pw"), CSecret("phrase"));
    EXPECT_EQ(out.host, "host.\xc3\xa9xample");
    EXPECT_EQ(out.user, "j\xc3\xbcrgen");
    EXPECT_EQ(out.port, 2200);
    EXPECT_EQ(out.keyFile, "C:/keys/id_ed25519");
    EXPECT_TRUE(out.useAgent);
    EXPECT_TRUE(out.compression);
    EXPECT_EQ(out.timeoutSeconds, 7);
    EXPECT_EQ(out.password.ToStdString(), "pw");
    EXPECT_EQ(out.passphrase.ToStdString(), "phrase");
    EXPECT_EQ(out.nameCodec.GetMode(), CNameCodec::Mode::CodePage);
}

TEST(Site, Defaults)
{
    CSite site;
    EXPECT_EQ(site.port, 22);
    EXPECT_EQ(site.timeoutSeconds, 30);
    EXPECT_FALSE(site.rememberPassword);
    EXPECT_EQ(site.workers, 2);
    EXPECT_TRUE(site.safeUpload);
    EXPECT_TRUE(site.preserveTime);
    EXPECT_FALSE(site.setUploadPermissions);
    EXPECT_EQ(site.uploadPermissions, 0644u);
    // new and quick-connect sites decode per name: valid UTF-8 as UTF-8, a Latin-1 name through the ANSI code page
    EXPECT_EQ(site.encoding, CSite::Encoding::Auto);
    EXPECT_EQ(ToSshSite(site, CSecret(), CSecret()).nameCodec.GetMode(), CNameCodec::Mode::Auto);
    site.encoding = CSite::Encoding::Utf8;
    EXPECT_EQ(site.MakeCodec().GetMode(), CNameCodec::Mode::Utf8);
}


namespace
{
// "café.txt" without relying on the source file encoding
std::wstring kCafeTxt()
{
    return std::wstring(L"caf") + static_cast<wchar_t>(0xE9) + L".txt";
}
} // namespace

TEST(Site, AutoDecodesLatin1AndUtf8NamesOfTheDefaultSite)
{
    const CNameCodec codec = CSite().MakeCodec();
    EXPECT_EQ(codec.Decode("caf\xC3\xA9.txt"), kCafeTxt());
    const std::wstring latin1 = codec.Decode("caf\xE9.txt");
    EXPECT_EQ(latin1.find(static_cast<wchar_t>(0xFFFD)), std::wstring::npos);
    EXPECT_EQ(latin1, CNameCodec::CodePage(GetACP()).Decode("caf\xE9.txt"));
    if (GetACP() == 1252)
        EXPECT_EQ(latin1, kCafeTxt());
}

TEST(Site, StoredEncodingValues)
{
    // saved sites keep what they stored; only a missing or unknown value falls back to the default (Auto)
    CSite::Encoding e = CSite().encoding;
    EXPECT_TRUE(EncodingFromStored(0, e));
    EXPECT_EQ(e, CSite::Encoding::Utf8);
    EXPECT_TRUE(EncodingFromStored(1, e));
    EXPECT_EQ(e, CSite::Encoding::Auto);
    EXPECT_TRUE(EncodingFromStored(2, e));
    EXPECT_EQ(e, CSite::Encoding::CodePage);
    e = CSite::Encoding::Utf8;
    EXPECT_FALSE(EncodingFromStored(3, e));
    EXPECT_EQ(e, CSite::Encoding::Utf8); // untouched
    // a stored value written by SaveSite (static_cast of the enum) always reads back
    for (CSite::Encoding saved : {CSite::Encoding::Utf8, CSite::Encoding::Auto, CSite::Encoding::CodePage})
    {
        CSite::Encoding back = CSite::Encoding::Auto;
        EXPECT_TRUE(EncodingFromStored(static_cast<unsigned long>(saved), back));
        EXPECT_EQ(back, saved);
    }
}
