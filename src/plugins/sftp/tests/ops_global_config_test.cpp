// Tests for ops/global_config: defaults, clamping, the thread-safe snapshot and the defaults of a new site.
#include <gtest/gtest.h>

#include <thread>

#include "global_config.h"
#include "site_list.h"

TEST(GlobalConfig, DefaultsMatchThePlan)
{
    const CGlobalConfig c;
    EXPECT_EQ(c.defaultWorkers, 2);
    EXPECT_EQ(c.defaultTimeoutSeconds, 30);
    EXPECT_EQ(c.keepaliveSeconds, 60);
    EXPECT_TRUE(c.listingCacheEnabled);
    EXPECT_EQ(c.listingCacheMaxAgeSeconds, 300);
    EXPECT_EQ(c.detachedIdleMinutes, 30);
    EXPECT_EQ(c.logLevel, SessionLogLevel::Normal);
    EXPECT_EQ(c.logMaxKiB, 512);
    EXPECT_EQ(c.closedLogsKept, 10);
    EXPECT_EQ(c.maxWorkersTotal, 6);
}

TEST(GlobalConfig, DefaultsAreInsideTheirRanges)
{
    CGlobalConfig c;
    const CGlobalConfig before = c;
    c.Clamp();
    EXPECT_EQ(c.defaultWorkers, before.defaultWorkers);
    EXPECT_EQ(c.defaultTimeoutSeconds, before.defaultTimeoutSeconds);
    EXPECT_EQ(c.keepaliveSeconds, before.keepaliveSeconds);
    EXPECT_EQ(c.listingCacheMaxAgeSeconds, before.listingCacheMaxAgeSeconds);
    EXPECT_EQ(c.detachedIdleMinutes, before.detachedIdleMinutes);
    EXPECT_EQ(c.logMaxKiB, before.logMaxKiB);
    EXPECT_EQ(c.closedLogsKept, before.closedLogsKept);
    EXPECT_EQ(c.maxWorkersTotal, before.maxWorkersTotal);
}

TEST(GlobalConfig, ClampsTooLowValues)
{
    CGlobalConfig c;
    c.defaultWorkers = 0;
    c.defaultTimeoutSeconds = -5;
    c.keepaliveSeconds = 3;
    c.listingCacheMaxAgeSeconds = 0;
    c.detachedIdleMinutes = -1;
    c.logLevel = static_cast<SessionLogLevel>(-2);
    c.logMaxKiB = 1;
    c.closedLogsKept = -1;
    c.maxWorkersTotal = 0;
    c.Clamp();
    EXPECT_EQ(c.defaultWorkers, MinDefaultWorkers);
    EXPECT_EQ(c.defaultTimeoutSeconds, MinDefaultTimeout);
    EXPECT_EQ(c.keepaliveSeconds, MinKeepaliveSeconds); // 1..9 is not "off": the shortest interval
    EXPECT_EQ(c.listingCacheMaxAgeSeconds, MinCacheAgeSeconds);
    EXPECT_EQ(c.detachedIdleMinutes, 0);
    EXPECT_EQ(c.logLevel, SessionLogLevel::Off);
    EXPECT_EQ(c.logMaxKiB, MinLogKiB);
    EXPECT_EQ(c.closedLogsKept, 0);
    EXPECT_EQ(c.maxWorkersTotal, MinWorkersTotal);
}

TEST(GlobalConfig, ClampsTooHighValues)
{
    CGlobalConfig c;
    c.defaultWorkers = 100;
    c.defaultTimeoutSeconds = 1000000;
    c.keepaliveSeconds = 1000000;
    c.listingCacheMaxAgeSeconds = 1000000000;
    c.detachedIdleMinutes = 99999;
    c.logLevel = static_cast<SessionLogLevel>(77);
    c.logMaxKiB = 1 << 30;
    c.closedLogsKept = 5000;
    c.maxWorkersTotal = 500;
    c.Clamp();
    EXPECT_EQ(c.defaultWorkers, MaxDefaultWorkers);
    EXPECT_EQ(c.defaultTimeoutSeconds, MaxDefaultTimeout);
    EXPECT_EQ(c.keepaliveSeconds, MaxKeepaliveSeconds);
    EXPECT_EQ(c.listingCacheMaxAgeSeconds, MaxCacheAgeSeconds);
    EXPECT_EQ(c.detachedIdleMinutes, MaxDetachedIdleMinutes);
    EXPECT_EQ(c.logLevel, SessionLogLevel::Verbose);
    EXPECT_EQ(c.logMaxKiB, MaxLogKiB);
    EXPECT_EQ(c.closedLogsKept, MaxClosedLogsKept);
    EXPECT_EQ(c.maxWorkersTotal, MaxWorkersTotal);
}

TEST(GlobalConfig, ZeroMeansOffOrNever)
{
    CGlobalConfig c;
    c.keepaliveSeconds = 0;
    c.detachedIdleMinutes = 0;
    c.closedLogsKept = 0;
    c.Clamp();
    EXPECT_EQ(c.keepaliveSeconds, 0);
    EXPECT_EQ(c.detachedIdleMinutes, 0);
    EXPECT_EQ(c.closedLogsKept, 0);
}

TEST(GlobalConfig, RangeEdgesAreKept)
{
    CGlobalConfig c;
    c.keepaliveSeconds = MinKeepaliveSeconds;
    c.maxWorkersTotal = MaxWorkersTotal;
    c.defaultWorkers = MaxDefaultWorkers;
    c.Clamp();
    EXPECT_EQ(c.keepaliveSeconds, MinKeepaliveSeconds);
    EXPECT_EQ(c.maxWorkersTotal, MaxWorkersTotal);
    EXPECT_EQ(c.defaultWorkers, MaxDefaultWorkers);
}

TEST(GlobalConfig, StoreGivesASnapshot)
{
    CGlobalConfigStore store;
    EXPECT_EQ(store.Get().defaultWorkers, 2); // starts with the defaults

    CGlobalConfig c;
    c.defaultWorkers = 5;
    store.Set(c);
    const CGlobalConfig snapshot = store.Get();
    c.defaultWorkers = 7;
    store.Set(c);
    EXPECT_EQ(snapshot.defaultWorkers, 5); // the copy does not follow later changes
    EXPECT_EQ(store.Get().defaultWorkers, 7);
}

TEST(GlobalConfig, StoreClampsOnSet)
{
    CGlobalConfigStore store;
    CGlobalConfig c;
    c.maxWorkersTotal = 999;
    c.keepaliveSeconds = 4;
    store.Set(c);
    EXPECT_EQ(store.Get().maxWorkersTotal, MaxWorkersTotal);
    EXPECT_EQ(store.Get().keepaliveSeconds, MinKeepaliveSeconds);
}

TEST(GlobalConfig, StoreSurvivesConcurrentUse)
{
    CGlobalConfigStore store;
    std::thread writer([&store] {
        for (int i = 1; i <= 2000; i++)
        {
            CGlobalConfig c;
            c.defaultWorkers = 1 + i % MaxDefaultWorkers;
            c.defaultTimeoutSeconds = c.defaultWorkers * 10; // both fields always change together
            store.Set(c);
        }
    });
    bool consistent = true;
    for (int i = 0; i < 2000; i++)
    {
        const CGlobalConfig c = store.Get();
        if (c.defaultTimeoutSeconds != c.defaultWorkers * 10 && c.defaultTimeoutSeconds != 30)
            consistent = false; // 30 = the initial default
    }
    writer.join();
    EXPECT_TRUE(consistent);
}

TEST(GlobalConfig, NewSiteTakesTheDefaults)
{
    CGlobalConfig c;
    c.defaultWorkers = 4;
    c.defaultTimeoutSeconds = 90;
    const CSite site = MakeNewSite(c);
    EXPECT_EQ(site.workers, 4);
    EXPECT_EQ(site.timeoutSeconds, 90);
    // everything else is the built-in site default
    const CSite plain;
    EXPECT_EQ(site.port, plain.port);
    EXPECT_EQ(site.safeUpload, plain.safeUpload);
    EXPECT_EQ(site.encoding, plain.encoding);
}

TEST(GlobalConfig, NewSiteIsValidEvenWithUnclampedConfig)
{
    CGlobalConfig c;
    c.defaultWorkers = 50;
    c.defaultTimeoutSeconds = 0;
    const CSite site = MakeNewSite(c);
    EXPECT_GE(site.workers, 1);
    EXPECT_LE(site.workers, MaxSiteWorkers);
    EXPECT_GE(site.timeoutSeconds, 1);
}

TEST(GlobalConfig, GlobalInstanceIsOneObject)
{
    const CGlobalConfig original = GlobalConfigStore().Get();
    CGlobalConfig c = original;
    c.defaultWorkers = original.defaultWorkers == 3 ? 4 : 3;
    GlobalConfigStore().Set(c);
    EXPECT_EQ(GlobalConfigStore().Get().defaultWorkers, c.defaultWorkers);
    GlobalConfigStore().Set(original);
}
