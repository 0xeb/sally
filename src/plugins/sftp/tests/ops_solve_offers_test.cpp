// Tests for ops/solve_offers: which answers the solve-error dialog offers.
#include <gtest/gtest.h>

#include "solve_offers.h"

TEST(SolveOffers, TargetExistsOffersEverythingForTransfers)
{
    const CSolveOffers o = SolveOffersFor(Problem::TargetExists, ItemType::Download);
    EXPECT_TRUE(o.retry && o.skip && o.overwrite && o.resume && o.rename && o.applyToAll);
}

TEST(SolveOffers, SkipIsAlwaysOffered)
{
    for (int p = static_cast<int>(Problem::None); p <= static_cast<int>(Problem::Other); p++)
        for (ItemType t : {ItemType::Download, ItemType::Upload, ItemType::MakeDirLocal, ItemType::DeleteRemoteDir})
            EXPECT_TRUE(SolveOffersFor(static_cast<Problem>(p), t).skip);
}

TEST(SolveOffers, InvalidNameWantsRenameNotOverwrite)
{
    const CSolveOffers o = SolveOffersFor(Problem::InvalidLocalName, ItemType::Download);
    EXPECT_TRUE(o.rename);
    EXPECT_FALSE(o.overwrite);
    EXPECT_FALSE(o.resume);
}

TEST(SolveOffers, InvalidNameOfADirectoryOffersRename)
{
    EXPECT_TRUE(SolveOffersFor(Problem::InvalidLocalName, ItemType::ExploreRemote).rename);
    EXPECT_TRUE(SolveOffersFor(Problem::InvalidRemoteName, ItemType::ExploreLocal).rename);
    EXPECT_FALSE(SolveOffersFor(Problem::InvalidLocalName, ItemType::DeleteRemoteDir).rename);
}

TEST(SolveOffers, ResumeOnlyForTransfersAfterInterruption)
{
    EXPECT_TRUE(SolveOffersFor(Problem::TransferFailed, ItemType::Upload).resume);
    EXPECT_TRUE(SolveOffersFor(Problem::ConnectionLost, ItemType::Download).resume);
    EXPECT_FALSE(SolveOffersFor(Problem::ConnectionLost, ItemType::MakeDirRemote).resume);
    EXPECT_FALSE(SolveOffersFor(Problem::DiskFull, ItemType::Download).resume);
}

TEST(SolveOffers, NonTransfersGetNoFileActions)
{
    const CSolveOffers o = SolveOffersFor(Problem::TargetExists, ItemType::MakeDirLocal);
    EXPECT_TRUE(o.retry && o.skip);
    EXPECT_FALSE(o.overwrite || o.resume || o.rename);
}

TEST(SolveOffers, SymlinkLoopOnlySkip)
{
    const CSolveOffers o = SolveOffersFor(Problem::SymlinkLoop, ItemType::ExploreRemote);
    EXPECT_TRUE(o.skip);
    EXPECT_FALSE(o.retry);
    EXPECT_TRUE(IsOffered(o, SolveAction::Skip));
    EXPECT_FALSE(IsOffered(o, SolveAction::Retry));
}

TEST(SolveOffers, ChmodItemsOfferOnlyRetryAndSkip)
{
    for (Problem problem : {Problem::ChmodFailed, Problem::PermissionDenied, Problem::NotFound, Problem::Other})
        for (ItemType type : {ItemType::SetAttrs, ItemType::ExploreRemote})
        {
            const CSolveOffers o = SolveOffersFor(problem, type);
            EXPECT_TRUE(o.retry && o.skip);
            EXPECT_FALSE(o.overwrite || o.resume || o.rename);
        }
}

TEST(SolveOffers, DeleteItemsOfferOnlyRetryAndSkip)
{
    for (Problem problem : {Problem::DeleteFailed, Problem::PermissionDenied, Problem::NotFound, Problem::Other})
        for (ItemType type : {ItemType::DeleteRemoteFile, ItemType::DeleteRemoteDir, ItemType::ExploreRemote})
        {
            const CSolveOffers o = SolveOffersFor(problem, type);
            EXPECT_TRUE(o.retry && o.skip);
            EXPECT_FALSE(o.overwrite || o.resume || o.rename);
        }
}
