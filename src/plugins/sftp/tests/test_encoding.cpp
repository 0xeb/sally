#include "test_support.h"

#include "encoding.h"

namespace
{
const wchar_t kReplacement = 0xFFFD;
}

TEST(Encoding, Utf8RoundTripAscii)
{
    const CNameCodec codec = CNameCodec::Utf8();
    EXPECT_EQ(codec.Decode("readme.txt"), L"readme.txt");
    EXPECT_EQ(codec.Encode(L"readme.txt"), "readme.txt");
}

TEST(Encoding, Utf8RoundTripBmpAndNonBmp)
{
    const CNameCodec codec = CNameCodec::Utf8();
    const std::wstring name = L"caf\u00E9 \u010D\u4E2D \U0001F600 end"; // includes a surrogate pair
    const std::string raw = codec.Encode(name);
    EXPECT_EQ(raw, "caf\xC3\xA9 \xC4\x8D\xE4\xB8\xAD \xF0\x9F\x98\x80 end");
    EXPECT_EQ(codec.Decode(raw), name);
}

TEST(Encoding, EmptyStrings)
{
    for (const CNameCodec& codec : {CNameCodec::Utf8(), CNameCodec::CodePage(1250), CNameCodec::Auto()})
    {
        EXPECT_EQ(codec.Decode(""), L"");
        EXPECT_EQ(codec.Encode(L""), "");
    }
    EXPECT_TRUE(IsValidUtf8(""));
}

TEST(Encoding, CodePage1250And1252)
{
    // bytes above 0x7F must work although char is unsigned (/J)
    EXPECT_EQ(CNameCodec::CodePage(1250).Decode("caf\xE8"), L"caf\u010D"); // c with caron
    EXPECT_EQ(CNameCodec::CodePage(1252).Decode("caf\xE8"), L"caf\u00E8"); // e with grave
    EXPECT_EQ(CNameCodec::CodePage(1252).Decode("caf\xE9"), L"caf\u00E9");
    EXPECT_EQ(CNameCodec::CodePage(1252).Encode(L"caf\u00E9"), "caf\xE9");
    EXPECT_EQ(CNameCodec::CodePage(1250).Encode(L"caf\u010D"), "caf\xE8");
}

TEST(Encoding, CodePageEncodeUnrepresentableBecomesQuestionMark)
{
    EXPECT_EQ(CNameCodec::CodePage(1252).Encode(L"a\u4E2D" L"b"), "a?b");
}

TEST(Encoding, InvalidCodePageDoesNotCrash)
{
    const CNameCodec codec = CNameCodec::CodePage(99999);
    codec.Decode("abc\xE9");
    codec.Encode(L"abc");
}

TEST(Encoding, IsValidUtf8)
{
    EXPECT_TRUE(IsValidUtf8("plain"));
    EXPECT_TRUE(IsValidUtf8("caf\xC3\xA9"));
    EXPECT_TRUE(IsValidUtf8("\xF0\x9F\x98\x80"));
    EXPECT_FALSE(IsValidUtf8("caf\xE9"));      // lone Latin-1 byte
    EXPECT_FALSE(IsValidUtf8("\xC3"));         // truncated
    EXPECT_FALSE(IsValidUtf8("\xC0\xAF"));     // overlong
    EXPECT_FALSE(IsValidUtf8("\xED\xA0\x80")); // surrogate
    EXPECT_FALSE(IsValidUtf8(std::string("\xFF\xFE", 2)));
}

TEST(Encoding, Utf8ModeInvalidBytesBecomeReplacementChar)
{
    const std::wstring text = CNameCodec::Utf8().Decode("caf\xE9.txt");
    EXPECT_NE(text.find(kReplacement), std::wstring::npos);
    EXPECT_EQ(text.substr(0, 3), L"caf");
    EXPECT_EQ(text.substr(text.size() - 4), L".txt");
}

TEST(Encoding, AutoPicksUtf8WhenValid)
{
    EXPECT_EQ(CNameCodec::Auto().Decode("caf\xC3\xA9"), L"caf\u00E9");
}

TEST(Encoding, AutoFallsBackToAnsiCodePage)
{
    const std::string raw = "caf\xE9";
    const std::wstring decoded = CNameCodec::Auto().Decode(raw);
    EXPECT_EQ(decoded, CNameCodec::CodePage(GetACP()).Decode(raw));
    EXPECT_EQ(decoded.find(kReplacement), std::wstring::npos);
}

TEST(Encoding, AutoEncodesAsUtf8)
{
    EXPECT_EQ(CNameCodec::Auto().Encode(L"caf\u00E9"), "caf\xC3\xA9");
}

TEST(Encoding, LongNames)
{
    const std::wstring name(5000, L'\u00E9');
    const CNameCodec utf8 = CNameCodec::Utf8();
    const std::string raw = utf8.Encode(name);
    EXPECT_EQ(raw.size(), 10000u);
    EXPECT_EQ(utf8.Decode(raw), name);

    const CNameCodec cp = CNameCodec::CodePage(1252);
    const std::string rawCp = cp.Encode(name);
    EXPECT_EQ(rawCp, std::string(5000, '\xE9'));
    EXPECT_EQ(cp.Decode(rawCp), name);
    EXPECT_EQ(CNameCodec::Auto().Decode(raw), name);
}

TEST(Encoding, AllByteValuesDoNotCrash)
{
    std::string raw;
    for (int i = 0; i < 256; ++i)
        raw.push_back(static_cast<char>(i));
    for (const CNameCodec& codec : {CNameCodec::Utf8(), CNameCodec::CodePage(1250), CNameCodec::Auto()})
        EXPECT_FALSE(codec.Decode(raw).empty());
}

TEST(Encoding, ReportsUnrepresentableCharacters)
{
    bool lossy = true;
    EXPECT_EQ(CNameCodec::CodePage(1252).Encode(L"caf\u00E9", &lossy), "caf\xE9");
    EXPECT_FALSE(lossy);
    EXPECT_EQ(CNameCodec::CodePage(1252).Encode(L"a\u4E2D" L"b", &lossy), "a?b");
    EXPECT_TRUE(lossy);
    EXPECT_EQ(CNameCodec::Utf8().Encode(L"a\u4E2D" L"b", &lossy), "a\xE4\xB8\xAD" "b");
    EXPECT_FALSE(lossy);
    CNameCodec::Utf8().Encode(L"", &lossy);
    EXPECT_FALSE(lossy);
}


namespace
{
// "café.txt" without relying on the source file encoding
std::wstring kCafeTxt()
{
    return std::wstring(L"caf") + static_cast<wchar_t>(0xE9) + L".txt";
}
} // namespace

TEST(Encoding, AutoDecidesPerName)
{
    const CNameCodec codec = CNameCodec::Auto();
    // one session can hold both kinds of names
    EXPECT_EQ(codec.Decode("caf\xC3\xA9.txt"), kCafeTxt());
    const std::wstring latin1 = codec.Decode("caf\xE9.txt");
    EXPECT_EQ(latin1.find(kReplacement), std::wstring::npos);
    if (GetACP() == 1252)
        EXPECT_EQ(latin1, kCafeTxt());
}
