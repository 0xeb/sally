#include <gtest/gtest.h>

#include "file_mode.h"

TEST(FileMode, FormatOctalAlwaysHasFourDigits)
{
    EXPECT_EQ(FormatOctal(0755), L"0755");
    EXPECT_EQ(FormatOctal(0), L"0000");
    EXPECT_EQ(FormatOctal(04755), L"4755");
    EXPECT_EQ(FormatOctal(07777), L"7777");
    EXPECT_EQ(FormatOctal(0100644), L"0644"); // the file type bits are cut off
}

TEST(FileMode, ParseOctalAcceptsOneToFourOctalDigits)
{
    uint32_t mode = 99;
    ASSERT_TRUE(ParseOctal(L"755", mode));
    EXPECT_EQ(mode, 0755u);
    ASSERT_TRUE(ParseOctal(L"0755", mode));
    EXPECT_EQ(mode, 0755u);
    ASSERT_TRUE(ParseOctal(L"7", mode));
    EXPECT_EQ(mode, 7u);
    ASSERT_TRUE(ParseOctal(L"0", mode));
    EXPECT_EQ(mode, 0u);
    ASSERT_TRUE(ParseOctal(L"1777", mode));
    EXPECT_EQ(mode, 01777u);
}

TEST(FileMode, ParseOctalRefusesBadTextAndKeepsTheValue)
{
    uint32_t mode = 0123;
    for (const wchar_t* text : {L"", L"8", L"789", L"12345", L"-1", L"+7", L" 7", L"7 ", L"rwx", L"0x1F", L"\x0667"})
    {
        EXPECT_FALSE(ParseOctal(text, mode)) << "text of length " << wcslen(text);
        EXPECT_EQ(mode, 0123u);
    }
}

TEST(FileMode, OctalRoundTrip)
{
    for (uint32_t m : {0u, 0644u, 0755u, 0600u, 04755u, 02755u, 01777u, 07777u})
    {
        uint32_t back = 0;
        ASSERT_TRUE(ParseOctal(FormatOctal(m), back));
        EXPECT_EQ(back, m);
    }
}

TEST(FileMode, SymbolicText)
{
    EXPECT_EQ(FormatSymbolic(0755), L"rwxr-xr-x");
    EXPECT_EQ(FormatSymbolic(0644), L"rw-r--r--");
    EXPECT_EQ(FormatSymbolic(0), L"---------");
    EXPECT_EQ(FormatSymbolic(0777), L"rwxrwxrwx");
}

TEST(FileMode, SymbolicTextOfSpecialBits)
{
    EXPECT_EQ(FormatSymbolic(04755), L"rwsr-xr-x");
    EXPECT_EQ(FormatSymbolic(04644), L"rwSr--r--"); // setuid without x: capital S
    EXPECT_EQ(FormatSymbolic(02755), L"rwxr-sr-x");
    EXPECT_EQ(FormatSymbolic(02745), L"rwxr-Sr-x");
    EXPECT_EQ(FormatSymbolic(01777), L"rwxrwxrwt");
    EXPECT_EQ(FormatSymbolic(01776), L"rwxrwxrwT");
}

TEST(FileMode, SymbolicTextShowsMixedBitsAsQuestionMarks)
{
    EXPECT_EQ(FormatSymbolic(0600, 0044), L"rw-?--?--");
    EXPECT_EQ(FormatSymbolic(0, 04000), L"--?------"); // a mixed setuid hides the owner x
}

TEST(FileMode, StateOfSeveralModesSeparatesSharedAndMixedBits)
{
    const CModeState state = StateOfModes({0644, 0755, 0600});
    EXPECT_EQ(state.checked, 0600u);       // set in all three
    EXPECT_EQ(state.mixed, 0155u);         // set in some
    EXPECT_EQ(state.checked & state.mixed, 0u);
    EXPECT_EQ(ShownMode(state), 0600u);
}

TEST(FileMode, StateOfOneModeHasNothingMixed)
{
    const CModeState state = StateOfModes({04750});
    EXPECT_EQ(state.checked, 04750u);
    EXPECT_EQ(state.mixed, 0u);
    EXPECT_EQ(StateOfModes({}).checked, 0u);
}

TEST(FileMode, StateIgnoresTheFileTypeBits)
{
    const CModeState state = StateOfModes({040755, 0100644});
    EXPECT_EQ(state.checked, 0644u);
    EXPECT_EQ(state.mixed, 0111u);
}

TEST(FileMode, ChangeOfStateLeavesMixedBitsOut)
{
    const CModeState state = StateOfModes({0644, 0755});
    const CModeChange change = ChangeOfState(state);
    EXPECT_EQ(change.setBits, 0644u);
    EXPECT_EQ(change.clearBits, 07000u | 0022u); // the special bits and group/others write are cleared everywhere
    EXPECT_EQ(change.setBits & change.clearBits, 0u);
    EXPECT_EQ(ApplyModeChange(0644, change), 0644u);
    EXPECT_EQ(ApplyModeChange(0755, change), 0755u); // untouched: the mixed bits keep what each item had
}

TEST(FileMode, UserChangeOnMixedSelection)
{
    // the user decides the "others read" bit only; the other mixed bits stay mixed
    CModeState state = StateOfModes({0640, 0644, 0600});
    EXPECT_NE(state.mixed & 0004, 0u);
    state.mixed &= ~0004u;
    state.checked &= ~0004u; // unchecked
    const CModeChange change = ChangeOfState(state);
    EXPECT_EQ(ApplyModeChange(0644, change), 0640u);
    EXPECT_EQ(ApplyModeChange(0600, change), 0600u);
    EXPECT_EQ(ApplyModeChange(0646, change) & 0004, 0u);
}

TEST(FileMode, TypedOctalDecidesEveryBit)
{
    const CModeChange change = ChangeOfState(StateOfMode(0640));
    EXPECT_EQ(change.setBits, 0640u);
    EXPECT_EQ(change.clearBits, 07137u);
    EXPECT_EQ(ApplyModeChange(07777, change), 0640u);
    EXPECT_EQ(ApplyModeChange(0, change), 0640u);
}

TEST(FileMode, ApplyKeepsOnlyPermissionBits)
{
    CModeChange change;
    change.setBits = 0100;
    EXPECT_EQ(ApplyModeChange(0100644, change), 0744u); // the file type is not part of the result
    change.clearBits = 04000;
    EXPECT_EQ(ApplyModeChange(04755, change), 0755u);
}

TEST(FileMode, EmptyChangeChangesNothing)
{
    const CModeChange none;
    EXPECT_TRUE(none.IsEmpty());
    EXPECT_EQ(ApplyModeChange(02751, none), 02751u);
}

TEST(FileMode, UntouchedSpecialBitsArePreservedByTheGridOnly)
{
    // the grid decides the nine rwx bits; the special bits stay as set in the state (shown in the octal field)
    CModeState state = StateOfMode(04755);
    state.mixed = 04000; // several items disagree about setuid and the user did not touch it
    const CModeChange change = ChangeOfState(state);
    EXPECT_EQ(ApplyModeChange(04644, change), 04755u);
    EXPECT_EQ(ApplyModeChange(0644, change), 0755u);
}

TEST(FileMode, AnUnknownItemModeMakesEveryBitMixed)
{
    CItemMode known;
    known.mode = 0644;
    known.known = true;
    const CItemMode unknown; // known == false
    const CModeState one = StateOfItemModes({known});
    EXPECT_EQ(one.checked, 0644u);
    EXPECT_EQ(one.mixed, 0u);

    const CModeState state = StateOfItemModes({known, unknown});
    EXPECT_EQ(state.checked, 0u);
    EXPECT_EQ(state.mixed, kModeAll);
    EXPECT_EQ(StateOfItemModes({unknown}).mixed, kModeAll);
}

TEST(FileMode, AChangeOfSomeBitsIsRefusedForAnUnknownMode)
{
    CModeChange change;
    change.setBits = 0004;
    const CItemMode unknown;
    uint32_t result = 0xABCD;
    EXPECT_FALSE(ApplyToItemMode(unknown, change, result));
    EXPECT_EQ(result, 0xABCDu); // not touched

    CItemMode known;
    known.mode = 0640;
    known.known = true;
    ASSERT_TRUE(ApplyToItemMode(known, change, result));
    EXPECT_EQ(result, 0644u);
}

TEST(FileMode, AChangeOfEveryBitIsAppliedToAnUnknownMode)
{
    const CItemMode unknown;
    uint32_t result = 0;
    ASSERT_TRUE(ApplyToItemMode(unknown, ChangeOfState(StateOfMode(0600)), result));
    EXPECT_EQ(result, 0600u);
    // a state with one mixed bit does not decide everything
    CModeState partial = StateOfMode(0600);
    partial.mixed = 04000;
    EXPECT_FALSE(ApplyToItemMode(unknown, ChangeOfState(partial), result));
}
