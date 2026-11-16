// Tests for the global connection slots (D58): CWorkerSlots itself with threads (no server needed).
#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "global_config.h"
#include "worker_slots.h"

namespace
{
// A slot gate whose limit the test changes while threads wait.
class SlotsFixture : public ::testing::Test
{
protected:
    SlotsFixture() : slots_([this] { return limit_.load(); }) {}

    static bool WaitUntil(const std::function<bool()>& pred, int timeoutMs = 5000)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!pred())
        {
            if (std::chrono::steady_clock::now() > end)
                return false;
            Sleep(2);
        }
        return true;
    }

    std::atomic<int> limit_{2};
    CWorkerSlots slots_;
};
} // namespace

TEST_F(SlotsFixture, TryAcquireStopsAtTheLimitAndReleaseFreesOne)
{
    EXPECT_TRUE(slots_.TryAcquire());
    EXPECT_TRUE(slots_.TryAcquire());
    EXPECT_FALSE(slots_.TryAcquire());
    EXPECT_EQ(slots_.Held(), 2);
    slots_.Release();
    EXPECT_TRUE(slots_.TryAcquire());
    slots_.Release();
    slots_.Release();
    slots_.Release(); // one too many: ignored
    EXPECT_EQ(slots_.Held(), 0);
}

TEST_F(SlotsFixture, AcquireWaitsForARelease)
{
    ASSERT_TRUE(slots_.TryAcquire());
    ASSERT_TRUE(slots_.TryAcquire());
    std::atomic<bool> got{false};
    std::thread waiter([&] { got = slots_.Acquire([] { return false; }); });
    ASSERT_TRUE(WaitUntil([&] { return slots_.Waiting() == 1; }));
    Sleep(100);
    EXPECT_FALSE(got.load());
    slots_.Release();
    waiter.join();
    EXPECT_TRUE(got.load());
    EXPECT_EQ(slots_.Held(), 2);
    EXPECT_EQ(slots_.Waiting(), 0);
}

TEST_F(SlotsFixture, CancelWhileWaitingReturnsFalseAndLeaksNothing)
{
    ASSERT_TRUE(slots_.TryAcquire());
    ASSERT_TRUE(slots_.TryAcquire());
    std::atomic<bool> cancel{false};
    std::atomic<int> result{-1};
    std::thread waiter([&] { result = slots_.Acquire([&] { return cancel.load(); }) ? 1 : 0; });
    ASSERT_TRUE(WaitUntil([&] { return slots_.Waiting() == 1; }));
    const auto start = std::chrono::steady_clock::now();
    cancel = true;
    waiter.join();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    EXPECT_EQ(result.load(), 0);
    EXPECT_LT(ms, 1000); // prompt: a few time slices
    EXPECT_EQ(slots_.Held(), 2);
    EXPECT_EQ(slots_.Waiting(), 0);
    slots_.Release();
    EXPECT_TRUE(slots_.TryAcquire()); // the slot of the cancelled waiter is not lost
}

TEST_F(SlotsFixture, StopBeforeTheFirstTryTakesNothingEvenWhenASlotIsFree)
{
    EXPECT_FALSE(slots_.Acquire([] { return true; }));
    EXPECT_EQ(slots_.Held(), 0);
}

TEST_F(SlotsFixture, PauseWhileWaitingReturnsFalseAndAcquireWorksAgainAfterResume)
{
    ASSERT_TRUE(slots_.TryAcquire());
    ASSERT_TRUE(slots_.TryAcquire());
    std::atomic<bool> paused{false};
    std::atomic<int> result{-1};
    std::thread waiter([&] { result = slots_.Acquire([&] { return paused.load(); }) ? 1 : 0; });
    ASSERT_TRUE(WaitUntil([&] { return slots_.Waiting() == 1; }));
    paused = true;
    waiter.join();
    EXPECT_EQ(result.load(), 0);

    paused = false; // resumed: the worker asks again and gets the slot freed meanwhile
    slots_.Release();
    EXPECT_TRUE(slots_.Acquire([&] { return paused.load(); }));
    EXPECT_EQ(slots_.Held(), 2);
}

TEST_F(SlotsFixture, LoweringTheLimitKeepsHoldersAndBlocksNewAcquires)
{
    limit_ = 3;
    ASSERT_TRUE(slots_.TryAcquire());
    ASSERT_TRUE(slots_.TryAcquire());
    ASSERT_TRUE(slots_.TryAcquire());
    limit_ = 1;
    EXPECT_EQ(slots_.Held(), 3); // nobody is thrown out
    EXPECT_FALSE(slots_.TryAcquire());
    slots_.Release();
    slots_.Release();
    EXPECT_FALSE(slots_.TryAcquire()); // one holder left, limit 1
    slots_.Release();
    EXPECT_TRUE(slots_.TryAcquire());
}

TEST_F(SlotsFixture, RaisingTheLimitWakesTheWaiters)
{
    limit_ = 1;
    ASSERT_TRUE(slots_.TryAcquire());
    std::atomic<int> got{0};
    std::vector<std::thread> waiters;
    for (int i = 0; i < 2; ++i)
        waiters.emplace_back([&] { if (slots_.Acquire([] { return false; })) ++got; });
    ASSERT_TRUE(WaitUntil([&] { return slots_.Waiting() == 2; }));
    Sleep(100);
    EXPECT_EQ(got.load(), 0);
    limit_ = 3; // nobody signals: the waiters look again on their own
    ASSERT_TRUE(WaitUntil([&] { return got.load() == 2; }));
    for (std::thread& waiter : waiters)
        waiter.join();
    EXPECT_EQ(slots_.Held(), 3);
}

TEST_F(SlotsFixture, ManyThreadsNeverHoldMoreThanTheLimit)
{
    limit_ = 3;
    std::atomic<int> inside{0};
    std::atomic<int> maxInside{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back(
            [&]
            {
                for (int round = 0; round < 20; ++round)
                {
                    ASSERT_TRUE(slots_.Acquire([] { return false; }));
                    const int now = ++inside;
                    int seen = maxInside.load();
                    while (now > seen && !maxInside.compare_exchange_weak(seen, now))
                    {
                    }
                    Sleep(1);
                    --inside;
                    slots_.Release();
                }
            });
    }
    for (std::thread& thread : threads)
        thread.join();
    EXPECT_LE(maxInside.load(), 3);
    EXPECT_LE(slots_.Peak(), 3);
    EXPECT_EQ(slots_.Held(), 0);
    EXPECT_EQ(slots_.Waiting(), 0);
}

TEST_F(SlotsFixture, LeaseGivesTheSlotBackOnceOnEveryWayOut)
{
    {
        CSlotLease lease(slots_);
        EXPECT_FALSE(lease.Held());
        EXPECT_TRUE(lease.TryAcquire());
        EXPECT_TRUE(lease.Acquire([] { return false; })); // already held: no second slot
        EXPECT_EQ(slots_.Held(), 1);
        lease.Release();
        lease.Release(); // twice: still one release
        EXPECT_EQ(slots_.Held(), 0);
        EXPECT_TRUE(lease.TryAcquire());
    } // the destructor releases
    EXPECT_EQ(slots_.Held(), 0);

    try
    {
        CSlotLease lease(slots_);
        ASSERT_TRUE(lease.TryAcquire());
        throw 1; // a worker that ends with an exception
    }
    catch (int)
    {
    }
    EXPECT_EQ(slots_.Held(), 0);
}

TEST_F(SlotsFixture, ALeaseThatWasCancelledWhileWaitingHoldsNothing)
{
    ASSERT_TRUE(slots_.TryAcquire());
    ASSERT_TRUE(slots_.TryAcquire());
    CSlotLease lease(slots_);
    EXPECT_FALSE(lease.Acquire([] { return true; }));
    EXPECT_FALSE(lease.Held());
    lease.Release();
    EXPECT_EQ(slots_.Held(), 2);
}

// The shared instance takes the limit from the global configuration at every decision.
TEST(WorkerSlotsGlobal, TheLimitComesFromTheConfigurationAndChangesAtRunTime)
{
    const CGlobalConfig original = GlobalConfigStore().Get();
    CGlobalConfig config = original;
    config.maxWorkersTotal = 1;
    GlobalConfigStore().Set(config);

    CWorkerSlots& slots = WorkerSlots();
    ASSERT_EQ(slots.Held(), 0) << "another test left a slot behind";
    EXPECT_TRUE(slots.TryAcquire());
    EXPECT_FALSE(slots.TryAcquire());
    config.maxWorkersTotal = 2;
    GlobalConfigStore().Set(config);
    EXPECT_TRUE(slots.TryAcquire());
    slots.Release();
    slots.Release();

    GlobalConfigStore().Set(original);
    EXPECT_EQ(slots.Held(), 0);
}
