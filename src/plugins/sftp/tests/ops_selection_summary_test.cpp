#include <gtest/gtest.h>

#include "selection_summary.h"

namespace
{
CSummaryItem File(uint64_t size, uint32_t mode)
{
    CSummaryItem i;
    i.size = size;
    i.mode = 0100000 | mode;
    return i;
}
CSummaryItem Dir(uint32_t mode)
{
    CSummaryItem i;
    i.isDir = true;
    i.mode = 040000 | mode;
    return i;
}
} // namespace

TEST(SelectionSummary, EmptyHasNoMode)
{
    const CSelectionSummary s = SummarizeSelection({});
    EXPECT_EQ(s.files, 0);
    EXPECT_EQ(s.dirs, 0);
    EXPECT_FALSE(s.modeKnown);
}

TEST(SelectionSummary, CountsAndSumsFilesOnly)
{
    const CSelectionSummary s = SummarizeSelection({File(100, 0644), File(50, 0644), Dir(0755)});
    EXPECT_EQ(s.files, 2);
    EXPECT_EQ(s.dirs, 1);
    EXPECT_EQ(s.totalSize, 150u);
    EXPECT_EQ(s.links, 0);
}

TEST(SelectionSummary, LinkIsCountedAsFileOrDirAndAsLink)
{
    CSummaryItem link = File(7, 0777);
    link.isLink = true;
    CSummaryItem dirLink = Dir(0777);
    dirLink.isLink = true;
    const CSelectionSummary s = SummarizeSelection({link, dirLink});
    EXPECT_EQ(s.files, 1);
    EXPECT_EQ(s.dirs, 1);
    EXPECT_EQ(s.links, 2);
    EXPECT_FALSE(s.modeKnown); // only links: their own mode says nothing
}

TEST(SelectionSummary, LinksDoNotTakePartInThePermissions)
{
    CSummaryItem link = File(7, 0777);
    link.isLink = true;
    const CSelectionSummary s = SummarizeSelection({File(1, 0644), link});
    ASSERT_TRUE(s.modeKnown);
    EXPECT_EQ(s.modeState.mixed, 0u);
    EXPECT_EQ(s.modeState.checked, 0644u);
}

TEST(SelectionSummary, CommonAndMixedPermissions)
{
    const CSelectionSummary s = SummarizeSelection({File(1, 0644), File(1, 0664)});
    ASSERT_TRUE(s.modeKnown);
    EXPECT_EQ(s.modeState.mixed, 0020u);
    EXPECT_EQ(s.modeState.checked & ~s.modeState.mixed, 0644u);
}

TEST(SelectionSummary, MissingModeHidesThePermissions)
{
    CSummaryItem noMode;
    const CSelectionSummary s = SummarizeSelection({File(1, 0644), noMode});
    EXPECT_FALSE(s.modeKnown);
}

TEST(SelectionSummary, SizeSumDoesNotWrap)
{
    const CSelectionSummary s = SummarizeSelection({File(UINT64_MAX - 1, 0644), File(10, 0644)});
    EXPECT_EQ(s.totalSize, UINT64_MAX);
}
