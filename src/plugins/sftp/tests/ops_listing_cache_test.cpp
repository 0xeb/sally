// Tests for ops/listing_cache (the panel's directory listing cache). A fake clock (plain time points) is used: no sleeping.
#include <gtest/gtest.h>

#include "listing_cache.h"

namespace
{
using Clock = CListingCache::Clock;

CListingCache::ListingPtr MakeListing(const std::string& firstName)
{
    std::vector<CSftpEntry> entries(1);
    entries[0].rawName = firstName;
    return std::make_shared<const std::vector<CSftpEntry>>(std::move(entries));
}

const Clock::time_point T0 = Clock::time_point() + std::chrono::hours(1);

Clock::time_point After(int seconds)
{
    return T0 + std::chrono::seconds(seconds);
}
} // namespace

TEST(ListingCache, MissOnEmptyCache)
{
    CListingCache cache;
    EXPECT_EQ(cache.Get("/a", 300, T0), nullptr);
}

TEST(ListingCache, HitWithinMaxAge)
{
    CListingCache cache;
    CListingCache::ListingPtr listing = MakeListing("x");
    cache.Put("/a", listing, T0);
    EXPECT_EQ(cache.Get("/a", 300, After(10)), listing);
}

TEST(ListingCache, MaxAgeEdgeIsStillFresh)
{
    CListingCache cache;
    cache.Put("/a", MakeListing("x"), T0);
    EXPECT_NE(cache.Get("/a", 300, After(300)), nullptr); // age == max age: fresh
    EXPECT_EQ(cache.Get("/a", 300, After(301)), nullptr); // one second older: expired
}

TEST(ListingCache, ExpiredEntryIsDropped)
{
    CListingCache cache;
    cache.Put("/a", MakeListing("x"), T0);
    EXPECT_EQ(cache.Get("/a", 5, After(6)), nullptr);
    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_EQ(cache.Get("/a", 1000, After(7)), nullptr); // gone for good, even with a larger limit
}

TEST(ListingCache, PutReplacesAndRestartsTheAge)
{
    CListingCache cache;
    cache.Put("/a", MakeListing("old"), T0);
    CListingCache::ListingPtr fresh = MakeListing("new");
    cache.Put("/a", fresh, After(200));
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_EQ(cache.Get("/a", 300, After(400)), fresh); // 200 s old, the first entry would be 400 s old
}

TEST(ListingCache, NullListingIsIgnored)
{
    CListingCache cache;
    cache.Put("/a", nullptr, T0);
    EXPECT_EQ(cache.Size(), 0u);
}

TEST(ListingCache, LruCapDropsTheLeastRecentlyUsed)
{
    CListingCache cache;
    for (size_t i = 0; i < CListingCache::MaxFolders; i++)
        cache.Put("/d" + std::to_string(i), MakeListing("x"), T0);
    EXPECT_EQ(cache.Size(), CListingCache::MaxFolders);

    EXPECT_NE(cache.Get("/d0", 300, T0), nullptr); // /d0 is now the most recently used, /d1 the oldest
    cache.Put("/extra", MakeListing("x"), T0);
    EXPECT_EQ(cache.Size(), CListingCache::MaxFolders);
    EXPECT_EQ(cache.Get("/d1", 300, T0), nullptr);
    EXPECT_NE(cache.Get("/d0", 300, T0), nullptr);
    EXPECT_NE(cache.Get("/extra", 300, T0), nullptr);
    EXPECT_NE(cache.Get("/d2", 300, T0), nullptr);
}

TEST(ListingCache, ReplacingDoesNotGrowTheCache)
{
    CListingCache cache;
    for (size_t i = 0; i < CListingCache::MaxFolders; i++)
        cache.Put("/d" + std::to_string(i), MakeListing("x"), T0);
    cache.Put("/d5", MakeListing("y"), T0);
    EXPECT_EQ(cache.Size(), CListingCache::MaxFolders);
    EXPECT_NE(cache.Get("/d0", 300, T0), nullptr); // nothing was pushed out
}

TEST(ListingCache, InvalidateDropsOnlyThatPath)
{
    CListingCache cache;
    cache.Put("/a", MakeListing("x"), T0);
    cache.Put("/a/b", MakeListing("x"), T0);
    cache.Invalidate("/a");
    EXPECT_EQ(cache.Get("/a", 300, T0), nullptr);
    EXPECT_NE(cache.Get("/a/b", 300, T0), nullptr);
    cache.Invalidate("/missing"); // harmless
}

TEST(ListingCache, InvalidateTreeKeepsTheNameBoundary)
{
    CListingCache cache;
    cache.Put("/a", MakeListing("x"), T0);
    cache.Put("/a/b", MakeListing("x"), T0);
    cache.Put("/a/b/c", MakeListing("x"), T0);
    cache.Put("/ab", MakeListing("x"), T0);
    cache.Put("/ab/c", MakeListing("x"), T0);
    cache.Put("/b", MakeListing("x"), T0);
    cache.InvalidateTree("/a");
    EXPECT_EQ(cache.Get("/a", 300, T0), nullptr);
    EXPECT_EQ(cache.Get("/a/b", 300, T0), nullptr);
    EXPECT_EQ(cache.Get("/a/b/c", 300, T0), nullptr);
    EXPECT_NE(cache.Get("/ab", 300, T0), nullptr);
    EXPECT_NE(cache.Get("/ab/c", 300, T0), nullptr);
    EXPECT_NE(cache.Get("/b", 300, T0), nullptr);
}

TEST(ListingCache, InvalidateTreeTolerantOfTrailingSlash)
{
    CListingCache cache;
    cache.Put("/a", MakeListing("x"), T0);
    cache.Put("/a/b", MakeListing("x"), T0);
    cache.Put("/ab", MakeListing("x"), T0);
    cache.InvalidateTree("/a/");
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_NE(cache.Get("/ab", 300, T0), nullptr);
}

TEST(ListingCache, InvalidateTreeOfTheRootDropsEverything)
{
    CListingCache cache;
    cache.Put("/", MakeListing("x"), T0);
    cache.Put("/a", MakeListing("x"), T0);
    cache.Put("/a/b", MakeListing("x"), T0);
    cache.InvalidateTree("/");
    EXPECT_EQ(cache.Size(), 0u);
}

TEST(ListingCache, RawNonUtf8KeysAreExactBytes)
{
    CListingCache cache;
    const std::string latin1 = "/d\xE9";   // not valid UTF-8
    const std::string other = "/d\xE8";    // differs in one byte
    const std::string utf8 = "/d\xC3\xA9"; // the same letter as UTF-8
    cache.Put(latin1, MakeListing("l"), T0);
    cache.Put(utf8, MakeListing("u"), T0);
    EXPECT_EQ(cache.Get(other, 300, T0), nullptr);
    ASSERT_NE(cache.Get(latin1, 300, T0), nullptr);
    EXPECT_EQ((*cache.Get(latin1, 300, T0))[0].rawName, "l");
    EXPECT_EQ((*cache.Get(utf8, 300, T0))[0].rawName, "u");
    cache.InvalidateTree("/d\xE9");
    EXPECT_EQ(cache.Get(latin1, 300, T0), nullptr);
    EXPECT_NE(cache.Get(utf8, 300, T0), nullptr);
}

TEST(ListingCache, KeysWithEmbeddedZeroByteStayDistinct)
{
    CListingCache cache;
    cache.Put(std::string("/a\0b", 4), MakeListing("x"), T0);
    EXPECT_EQ(cache.Get("/a", 300, T0), nullptr);
}

TEST(ListingCache, ClearDropsEverything)
{
    CListingCache cache;
    cache.Put("/a", MakeListing("x"), T0);
    cache.Put("/b", MakeListing("x"), T0);
    cache.Clear();
    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_EQ(cache.Get("/a", 300, T0), nullptr);
}

TEST(ListingCache, HeldListingSurvivesInvalidation)
{
    // the panel keeps its own pointer to the entries it shows: dropping the cache entry must not free them
    CListingCache cache;
    CListingCache::ListingPtr held = MakeListing("x");
    cache.Put("/a", held, T0);
    cache.Clear();
    EXPECT_EQ((*held)[0].rawName, "x");
}

TEST(ListingCache, MayUseListingCacheTable)
{
    EXPECT_TRUE(MayUseListingCache(true, false));
    EXPECT_FALSE(MayUseListingCache(true, true));  // Ctrl+R
    EXPECT_FALSE(MayUseListingCache(false, false)); // switched off
    EXPECT_FALSE(MayUseListingCache(false, true));
}
