// Tests for ops/cancel_policy (what ESC during a listing does).
#include <gtest/gtest.h>

#include "cancel_policy.h"

TEST(CancelPolicy, DecideOnCancelTable)
{
    struct Row
    {
        CancelPoint where;
        bool opening;
        bool haveCache;
        CancelAction expected;
    };
    const Row rows[] = {
        {CancelPoint::InChangePath, true, false, CancelAction::FailOpen},
        {CancelPoint::InChangePath, true, true, CancelAction::FailOpen},
        {CancelPoint::InListCurrentPath, true, false, CancelAction::FailOpen},
        {CancelPoint::InChangePath, false, false, CancelAction::LegacyShorten},
        {CancelPoint::InListCurrentPath, false, false, CancelAction::LegacyShorten},
        {CancelPoint::InChangePath, false, true, CancelAction::ReturnCachedNow},
        {CancelPoint::InListCurrentPath, false, true, CancelAction::ReturnFalseThenCache},
    };
    for (const Row& r : rows)
        EXPECT_EQ(DecideOnCancel(r.where, r.opening, r.haveCache), r.expected);
}

TEST(CancelPolicy, ChangePathEntryTable)
{
    EXPECT_EQ(DecideChangePathEntry(true, true), ChangePathEntry::ServeCachedPath);
    EXPECT_EQ(DecideChangePathEntry(true, false), ChangePathEntry::Normal);
    EXPECT_EQ(DecideChangePathEntry(false, true), ChangePathEntry::Normal); // a later request really reads the server
    EXPECT_EQ(DecideChangePathEntry(false, false), ChangePathEntry::Normal);
}
