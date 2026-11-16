#include <gtest/gtest.h>

#include <windows.h>

#include "local_name.h"

TEST(LocalName, ValidNames)
{
    const wchar_t* names[] = {L"a", L"file.txt", L".bashrc", L"a b", L"a.tar.gz", L"caf\u00e9", L"console", L"com10",
                              L"COM0", L"lpt", L"nul_x", L"a-b_c (2).txt"};
    for (const wchar_t* n : names)
        EXPECT_TRUE(IsValidLocalName(n)) << n;
}

TEST(LocalName, InvalidNames)
{
    const wchar_t* names[] = {L"", L".", L"..", L"a:b", L"a<b", L"a>b", L"a\"b", L"a/b", L"a\\b", L"a|b", L"a?b",
                              L"a*b", L"x.", L"x ", L"x. ", L"a\tb", L"a\x01", L"CON", L"con", L"con.txt",
                              L"Con.tar.gz", L"PRN", L"AUX.x", L"NUL", L"nul.txt", L"COM1", L"com9.log", L"LPT1",
                              L"lpt9.txt", L"CON .txt"};
    for (const wchar_t* n : names)
        EXPECT_FALSE(IsValidLocalName(n)) << n;
}

TEST(LocalName, LengthLimit)
{
    EXPECT_TRUE(IsValidLocalName(std::wstring(255, L'a')));
    EXPECT_FALSE(IsValidLocalName(std::wstring(256, L'a')));
}

TEST(LocalPath, JoinsWithBackslash)
{
    EXPECT_EQ(MakeLocalPath(L"C:\\dir", L"f.txt"), L"C:\\dir\\f.txt");
    EXPECT_EQ(MakeLocalPath(L"C:\\dir\\", L"f.txt"), L"C:\\dir\\f.txt");
    EXPECT_EQ(MakeLocalPath(L"\\\\srv\\share", L"f.txt"), L"\\\\srv\\share\\f.txt");
    EXPECT_EQ(MakeLocalPath(L"", L"f.txt"), L"f.txt");
}

TEST(LocalPath, ShortPathHasNoPrefix)
{
    const std::wstring dir = L"C:\\" + std::wstring(150, L'd');
    const std::wstring path = MakeLocalPath(dir, std::wstring(50, L'f')); // 3 + 150 + 1 + 50 = 204
    EXPECT_EQ(path.find(L"\\\\?\\"), std::wstring::npos);
}

TEST(LocalPath, BoundaryIs248)
{
    // CreateDirectory refuses 248 characters or more without the prefix, so the prefix starts there
    const std::wstring dir = L"C:\\" + std::wstring(200, L'd');
    EXPECT_EQ(MakeLocalPath(dir, std::wstring(43, L'f')).size(), 247u);
    EXPECT_EQ(MakeLocalPath(dir, std::wstring(43, L'f')).find(L"\\\\?\\"), std::wstring::npos);
    const std::wstring p248 = MakeLocalPath(dir, std::wstring(44, L'f'));
    EXPECT_EQ(p248.compare(0, 4, L"\\\\?\\"), 0);
}

TEST(LocalPath, LongDrivePathGetsPrefix)
{
    const std::wstring dir = L"C:\\" + std::wstring(260, L'd');
    EXPECT_EQ(MakeLocalPath(dir, L"f.txt"), L"\\\\?\\" + dir + L"\\f.txt");
}

TEST(LocalPath, LongUncPathGetsUncPrefix)
{
    const std::wstring dir = L"\\\\srv\\share\\" + std::wstring(260, L'd');
    EXPECT_EQ(MakeLocalPath(dir, L"f.txt"), L"\\\\?\\UNC\\srv\\share\\" + std::wstring(260, L'd') + L"\\f.txt");
}

TEST(LocalPath, AlreadyPrefixedIsKept)
{
    const std::wstring dir = L"\\\\?\\C:\\" + std::wstring(260, L'd');
    EXPECT_EQ(MakeLocalPath(dir, L"f.txt"), dir + L"\\f.txt");
    const std::wstring unc = L"\\\\?\\UNC\\srv\\share\\" + std::wstring(260, L'd');
    EXPECT_EQ(MakeLocalPath(unc, L"f.txt"), unc + L"\\f.txt");
    // a short prefixed path is also left alone
    EXPECT_EQ(MakeLocalPath(L"\\\\?\\C:\\a", L"b"), L"\\\\?\\C:\\a\\b");
}

TEST(LocalPath, LongRelativePathIsUnchanged)
{
    const std::wstring dir = std::wstring(300, L'd');
    EXPECT_EQ(MakeLocalPath(dir, L"f"), dir + L"\\f");
}

TEST(DecodeRemoteName, ValidUtf8IsNotLossy)
{
    bool lossy = true;
    EXPECT_EQ(DecodeRemoteNameToLocal(CNameCodec::Utf8(), "caf\xC3\xA9", &lossy), L"caf\u00e9");
    EXPECT_FALSE(lossy);
}

TEST(DecodeRemoteName, InvalidUtf8IsLossy)
{
    bool lossy = false;
    std::wstring result = DecodeRemoteNameToLocal(CNameCodec::Utf8(), "a\xFF" "b", &lossy);
    EXPECT_EQ(result, L"a\ufffd" L"b");
    EXPECT_TRUE(lossy);
}

TEST(DecodeRemoteName, LiteralReplacementCharacterIsNotLossy)
{
    bool lossy = true;
    DecodeRemoteNameToLocal(CNameCodec::Utf8(), "a\xEF\xBF\xBD", &lossy);
    EXPECT_FALSE(lossy);
}

TEST(DecodeRemoteName, AutoFallsBackToCodePageWithoutLoss)
{
    bool lossy = true;
    DecodeRemoteNameToLocal(CNameCodec::CodePage(1252), "caf\xE9", &lossy);
    EXPECT_FALSE(lossy);
    DecodeRemoteNameToLocal(CNameCodec::Utf8(), "x", nullptr); // null `lossy` is allowed
}

TEST(ExtendedLocalPath, PrefixesOnlyLongAbsolutePaths)
{
    EXPECT_EQ(ExtendedLocalPath(L"C:\\short\\file.txt"), L"C:\\short\\file.txt");
    const std::wstring longDrive = L"C:\\" + std::wstring(300, L'a');
    EXPECT_EQ(ExtendedLocalPath(longDrive), L"\\\\?\\" + longDrive);
    const std::wstring longUnc = L"\\\\server\\share\\" + std::wstring(300, L'a');
    EXPECT_EQ(ExtendedLocalPath(longUnc), L"\\\\?\\UNC\\" + longUnc.substr(2));
    const std::wstring already = L"\\\\?\\C:\\" + std::wstring(300, L'a');
    EXPECT_EQ(ExtendedLocalPath(already), already);
    const std::wstring relative(300, L'a');
    EXPECT_EQ(ExtendedLocalPath(relative), relative); // the prefix needs an absolute path
}
