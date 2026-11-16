#include "test_support.h"

#include <string>
#include <type_traits>
#include <utility>

#include "secret.h"

namespace
{
// The inspected bytes are always inside the buffer the CSecret still owns (it keeps the allocation after clear()
// and resize() to a smaller size), and the buffer is zero-initialised, so this reads no freed or uninitialised memory.
bool AllZero(const char* data, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
        if (data[i] != '\0')
            return false;
    return true;
}
} // namespace

TEST(Secret, StoresAndTerminates)
{
    CSecret secret("hunter2");
    EXPECT_EQ(secret.size(), 7u);
    EXPECT_STREQ(secret.c_str(), "hunter2");
    EXPECT_FALSE(secret.empty());
    CSecret empty;
    EXPECT_TRUE(empty.empty());
    EXPECT_STREQ(empty.c_str(), "");
}

TEST(Secret, ClearWipesBuffer)
{
    CSecret secret("hunter2");
    const char* buffer = secret.data();
    const size_t capacity = secret.capacity();
    secret.clear();
    EXPECT_EQ(secret.size(), 0u);
    EXPECT_EQ(secret.data(), buffer);
    EXPECT_TRUE(AllZero(buffer, capacity + 1));
}

TEST(Secret, ShrinkWipesRemovedBytes)
{
    CSecret secret("abcdef");
    const char* buffer = secret.data();
    secret.resize(2);
    EXPECT_STREQ(secret.c_str(), "ab");
    EXPECT_EQ(secret.data(), buffer);
    EXPECT_TRUE(AllZero(buffer + 2, 5)); // 4 removed bytes + terminator
}

TEST(Secret, ResizeGrowKeepsContentAndZeroFills)
{
    CSecret secret("ab");
    secret.resize(10);
    EXPECT_EQ(secret.size(), 10u);
    EXPECT_EQ(secret.data()[0], 'a');
    EXPECT_EQ(secret.data()[1], 'b');
    EXPECT_TRUE(AllZero(secret.data() + 2, 8));
}

TEST(Secret, UsableAsFillBuffer)
{
    CSecret secret;
    secret.resize(16);
    memcpy(secret.data(), "typed", 5);
    secret.resize(5);
    EXPECT_STREQ(secret.c_str(), "typed");
}

TEST(Secret, AssignReplacesContent)
{
    CSecret secret("first-secret");
    secret.assign("x", 1);
    EXPECT_STREQ(secret.c_str(), "x");
    secret.assign(std::string("second"));
    EXPECT_EQ(secret.ToStdString(), "second");
}

TEST(Secret, MoveTransfersAndEmptiesSource)
{
    CSecret a("moved");
    const char* buffer = a.data();
    CSecret b(std::move(a));
    EXPECT_EQ(b.data(), buffer);
    EXPECT_STREQ(b.c_str(), "moved");
    EXPECT_TRUE(a.empty());

    CSecret c("other");
    c = std::move(b);
    EXPECT_STREQ(c.c_str(), "moved");
    EXPECT_TRUE(b.empty());
}

TEST(Secret, MoveAssignWipesOldContent)
{
    CSecret target("old-secret");
    const char* oldBuffer = target.data();
    const size_t oldCapacity = target.capacity();
    CSecret source("new");
    target = std::move(source);
    EXPECT_STREQ(target.c_str(), "new");
    // the old buffer now belongs to 'source' (swapped), already wiped
    EXPECT_EQ(source.data(), oldBuffer);
    EXPECT_TRUE(AllZero(oldBuffer, oldCapacity + 1));
}

TEST(Secret, CloneIsIndependent)
{
    CSecret a("shared");
    CSecret b = a.Clone();
    EXPECT_NE(a.data(), b.data());
    a.clear();
    EXPECT_STREQ(b.c_str(), "shared");
}

TEST(Secret, NotCopyable)
{
    EXPECT_FALSE(std::is_copy_constructible<CSecret>::value);
    EXPECT_FALSE(std::is_copy_assignable<CSecret>::value);
}
