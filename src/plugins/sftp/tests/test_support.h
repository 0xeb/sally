// Shared helpers for the SFTP plugin tests: test server constants, fixture folder, skip-if-no-server.
#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace sftptest
{
constexpr const char* kHost = "127.0.0.1";
constexpr int kPort = 2222;       // sshd with the SFTP subsystem
constexpr int kPortNoSftp = 2223; // sshd without the SFTP subsystem
constexpr int kPortKbdInt = 2224; // sshd with keyboard-interactive (PAM) as the only method
constexpr int kPortPubKey = 2225; // sshd with public key as the only method
constexpr int kPortLegacy = 2226; // sshd with legacy algorithms only (aes128-cbc, hmac-sha1, ssh-rsa, dh-group14-sha1)
constexpr const char* kUser = "test";
constexpr const char* kPassword = "test";

#ifndef SFTP_TEST_DIR_DEFAULT
#define SFTP_TEST_DIR_DEFAULT "tests/docker/out"
#endif

// Folder with the generated keys (tests/docker/out); SFTP_TEST_DIR overrides the compile-time default.
inline std::string FixtureDir()
{
    char* env = nullptr;
    size_t len = 0;
    std::string dir = SFTP_TEST_DIR_DEFAULT;
    if (_dupenv_s(&env, &len, "SFTP_TEST_DIR") == 0 && env != nullptr && *env != '\0')
        dir = env;
    free(env);
    return dir;
}

inline void EnsureWinsock()
{
    static const bool started = []
    {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)started;
}

// Quick TCP connect to 127.0.0.1:port, gives up after about 500 ms.
inline bool ServerReachable(int port)
{
    EnsureWinsock();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return false;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, kHost, &addr.sin_addr);

    bool ok = false;
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
    {
        ok = true;
    }
    else if (WSAGetLastError() == WSAEWOULDBLOCK)
    {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(s, &writable);
        timeval timeout = {0, 500 * 1000};
        if (select(0, nullptr, &writable, nullptr, &timeout) == 1)
        {
            int err = 0;
            int len = sizeof(err);
            ok = getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) == 0 && err == 0;
        }
    }
    closesocket(s);
    return ok;
}

// A local TCP proxy to 127.0.0.1:targetPort that can be told to stop forwarding (both directions) while keeping the
// connections open: to the client that is a server that went silent. One thread, blocking connects to the target.
class CStallProxy
{
public:
    explicit CStallProxy(int targetPort) : targetPort_(targetPort)
    {
        EnsureWinsock();
        listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        inet_pton(AF_INET, kHost, &addr.sin_addr);
        bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(listen_, 8);
        int len = sizeof(addr);
        getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { Loop(); });
    }
    ~CStallProxy()
    {
        stop_ = true;
        thread_.join();
        for (const Pair& p : pairs_)
        {
            closesocket(p.client);
            closesocket(p.server);
        }
        closesocket(listen_);
    }
    CStallProxy(const CStallProxy&) = delete;
    CStallProxy& operator=(const CStallProxy&) = delete;

    int Port() const { return port_; }
    void Stall(bool on) { stalled_ = on; }
    // Stalls on its own once this many bytes (both directions, all connections) went through; 0 = no limit. A limit
    // that was reached stays reached: Stall(false) lets the traffic go again, and the counter keeps counting.
    void StallAfter(uint64_t totalBytes) { limit_ = totalBytes; }
    uint64_t Forwarded() const { return forwarded_; }
    int Accepted() const { return accepted_; } // connections that reached the target
    bool IsStalled() const { return stalled_; }
    // Closes every open connection (client and server side) but keeps listening: the client sees a server that closed
    // the connection, and may connect again. Returns when the proxy thread has done it.
    void DropConnections()
    {
        drop_ = true;
        for (int i = 0; i < 200 && drop_; ++i)
            Sleep(10);
    }

private:
    struct Pair
    {
        SOCKET client;
        SOCKET server;
    };

    void Accept()
    {
        SOCKET c = accept(listen_, nullptr, nullptr);
        if (c == INVALID_SOCKET)
            return;
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(targetPort_));
        inet_pton(AF_INET, kHost, &addr.sin_addr);
        if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            closesocket(s);
            closesocket(c);
            return;
        }
        pairs_.push_back({c, s});
        ++accepted_;
    }

    bool Forward(SOCKET from, SOCKET to)
    {
        char buf[16384];
        int n = recv(from, buf, sizeof(buf), 0);
        if (n <= 0)
            return false;
        forwarded_ += static_cast<uint64_t>(n);
        if (limit_ != 0 && forwarded_ >= limit_)
            stalled_ = true; // this chunk still goes through; the next round stops
        for (int sent = 0; sent < n;)
        {
            int m = send(to, buf + sent, n - sent, 0);
            if (m <= 0)
                return false;
            sent += m;
        }
        return true;
    }

    void Loop()
    {
        while (!stop_)
        {
            if (drop_)
            {
                for (const Pair& p : pairs_)
                {
                    closesocket(p.client);
                    closesocket(p.server);
                }
                pairs_.clear();
                drop_ = false;
            }
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listen_, &readSet);
            if (!stalled_)
                for (const Pair& p : pairs_)
                {
                    FD_SET(p.client, &readSet);
                    FD_SET(p.server, &readSet);
                }
            timeval slice = {0, 20 * 1000};
            if (select(0, &readSet, nullptr, nullptr, &slice) <= 0)
                continue;
            if (FD_ISSET(listen_, &readSet))
                Accept();
            if (stalled_)
                continue;
            for (size_t i = 0; i < pairs_.size();)
            {
                const Pair p = pairs_[i];
                bool alive = true;
                if (FD_ISSET(p.client, &readSet))
                    alive = Forward(p.client, p.server);
                if (alive && FD_ISSET(p.server, &readSet))
                    alive = Forward(p.server, p.client);
                if (!alive)
                {
                    closesocket(p.client);
                    closesocket(p.server);
                    pairs_.erase(pairs_.begin() + static_cast<std::ptrdiff_t>(i));
                }
                else
                    ++i;
            }
        }
    }

    int targetPort_;
    int port_ = 0;
    SOCKET listen_ = INVALID_SOCKET;
    std::atomic<bool> stop_{false};
    std::atomic<bool> stalled_{false};
    std::atomic<bool> drop_{false};
    std::atomic<uint64_t> limit_{0};
    std::atomic<uint64_t> forwarded_{0};
    std::atomic<int> accepted_{0};
    std::vector<Pair> pairs_;
    std::thread thread_;
};
} // namespace sftptest

// Skips the current test when the Docker test server is not running.
#define REQUIRE_SERVER()                                                                                       \
    do                                                                                                         \
    {                                                                                                          \
        if (!sftptest::ServerReachable(sftptest::kPort))                                                       \
            GTEST_SKIP() << "Docker test server not reachable on 127.0.0.1:2222 - run tests/docker/setup.ps1"; \
    } while (0)
