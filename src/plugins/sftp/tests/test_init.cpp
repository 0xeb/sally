#include "test_support.h"

#include <libssh2.h>
#include <openssl/rand.h>

#include <atomic>
#include <thread>
#include <vector>

#include "ssh_init.h"

namespace
{
// Winsock is usable only between WSAStartup and WSACleanup; socket() fails with WSANOTINITIALISED otherwise.
// (test_support.h's EnsureWinsock keeps its own reference, so a failure here would mean our count is wrong
// only if that was never called - these tests do not call it.)
bool WinsockWorks()
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return false;
    closesocket(s);
    return true;
}
} // namespace

TEST(Init, InitAndExit)
{
    CSshError err;
    ASSERT_TRUE(SshGlobalInit(&err)) << err.message;
    EXPECT_FALSE(err.IsError());
    EXPECT_TRUE(WinsockWorks());
    SshGlobalExit();
}

TEST(Init, NestedReferenceCounting)
{
    ASSERT_TRUE(SshGlobalInit());
    ASSERT_TRUE(SshGlobalInit());
    SshGlobalExit(); // one reference left
    EXPECT_TRUE(WinsockWorks());
    SshGlobalExit();
}

TEST(Init, InitAfterExit)
{
    for (int i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(SshGlobalInit());
        EXPECT_TRUE(WinsockWorks());
        SshGlobalExit();
    }
}

TEST(Init, ExtraExitIsIgnored)
{
    SshGlobalExit(); // unbalanced, must not crash or go negative
    ASSERT_TRUE(SshGlobalInit());
    EXPECT_TRUE(WinsockWorks());
    SshGlobalExit();
}

TEST(Init, ConcurrentInit)
{
    constexpr int kThreads = 8;
    constexpr int kRounds = 20;
    std::atomic<int> succeeded{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back(
            [&]
            {
                while (!go.load())
                    std::this_thread::yield();
                for (int n = 0; n < kRounds; ++n)
                {
                    if (SshGlobalInit())
                    {
                        if (WinsockWorks() && libssh2_version(0) != nullptr)
                            ++succeeded;
                        SshGlobalExit();
                    }
                }
            });
    }
    go = true;
    for (std::thread& t : threads)
        t.join();
    EXPECT_EQ(succeeded.load(), kThreads * kRounds);
}

TEST(Init, ThreadScopeAfterOpenSslUse)
{
    ASSERT_TRUE(SshGlobalInit());
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
    {
        threads.emplace_back(
            [&]
            {
                CSshThreadScope scope;
                unsigned char bytes[32] = {};
                if (RAND_bytes(bytes, sizeof(bytes)) == 1)
                    ++ok;
            });
    }
    for (std::thread& t : threads)
        t.join();
    EXPECT_EQ(ok.load(), 4);
    SshGlobalExit();
}

TEST(Init, ThreadScopeOnThreadWithoutOpenSslUse)
{
    std::thread t([] { CSshThreadScope scope; });
    t.join();
}
