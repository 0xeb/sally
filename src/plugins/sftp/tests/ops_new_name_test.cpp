#include <gtest/gtest.h>

#include "new_name.h"

TEST(NewName, PlainNamesAreOk)
{
    EXPECT_EQ(CheckNewName(L"file.txt"), NewNameCheck::Ok);
    EXPECT_EQ(CheckNewName(L".hidden"), NewNameCheck::Ok);
    EXPECT_EQ(CheckNewName(L"...") , NewNameCheck::Ok);
    EXPECT_EQ(CheckNewName(L"with space"), NewNameCheck::Ok);
    EXPECT_EQ(CheckNewName(L"back\\slash"), NewNameCheck::Ok); // a valid Unix name
    EXPECT_EQ(CheckNewName(L"řížť中"), NewNameCheck::Ok);
}

TEST(NewName, EmptyIsRejected)
{
    EXPECT_EQ(CheckNewName(L""), NewNameCheck::Empty);
}

TEST(NewName, DotsSlashAndNulAreInvalid)
{
    EXPECT_EQ(CheckNewName(L"."), NewNameCheck::Invalid);
    EXPECT_EQ(CheckNewName(L".."), NewNameCheck::Invalid);
    EXPECT_EQ(CheckNewName(L"a/b"), NewNameCheck::Invalid);
    EXPECT_EQ(CheckNewName(L"/abs"), NewNameCheck::Invalid);
    EXPECT_EQ(CheckNewName(L"dir/"), NewNameCheck::Invalid);
    EXPECT_EQ(CheckNewName(std::wstring(L"a\0b", 3)), NewNameCheck::Invalid);
}
