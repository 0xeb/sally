#include "test_support.h"

#include <windows.h>
#include <winioctl.h>

#include <chrono>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

#include "ssh_init.h"
#include "ssh_session.h"

namespace
{
constexpr uint64_t kKiB = 1024;
constexpr uint64_t kMiB = 1024 * kKiB;
constexpr uint64_t kGiB = 1024 * kMiB;

// Records the progress; cancels (returns false) once done >= cancelAt.
struct CTestCallbacks : ISshCallbacks
{
    HostKeyDecision VerifyHostKey(const std::string&, int, const std::string&, const std::string&, bool) override
    {
        return HostKeyDecision::AcceptOnce;
    }
    bool Prompt(PromptKind, const std::string&, const std::string&, const std::string&, bool, CSecret&) override { return false; }
    bool Progress(uint64_t done, uint64_t total) override
    {
        values.push_back(done);
        lastTotal = total;
        return done < cancelAt;
    }
    std::vector<uint64_t> values;
    uint64_t lastTotal = 0;
    uint64_t cancelAt = UINT64_MAX;
};

CSshSite ServerSite()
{
    CSshSite site;
    site.host = sftptest::kHost;
    site.port = sftptest::kPort;
    site.user = sftptest::kUser;
    site.password.assign(sftptest::kPassword);
    site.timeoutSeconds = 20;
    site.nameCodec = CNameCodec::Auto();
    return site;
}

// Local file handle that closes itself
class CLocal
{
public:
    CLocal() = default;
    CLocal(const CLocal&) = delete;
    CLocal& operator=(const CLocal&) = delete;
    ~CLocal() { Close(); }
    bool Open(const std::string& path, DWORD access, DWORD disposition)
    {
        Close();
        h_ = CreateFileA(path.c_str(), access, FILE_SHARE_READ, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
        return h_ != INVALID_HANDLE_VALUE;
    }
    void Close()
    {
        if (h_ != INVALID_HANDLE_VALUE)
            CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
    }
    HANDLE get() const { return h_; }
    uint64_t Size() const
    {
        LARGE_INTEGER s = {};
        GetFileSizeEx(h_, &s);
        return static_cast<uint64_t>(s.QuadPart);
    }
    void MakeSparse() const
    {
        DWORD ret = 0;
        DeviceIoControl(h_, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ret, nullptr);
    }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};

std::vector<char> RandomData(size_t size, unsigned seed)
{
    std::mt19937 gen(seed);
    std::vector<char> data(size);
    for (size_t i = 0; i < size; ++i)
        data[i] = static_cast<char>(gen());
    return data;
}

bool WriteLocalFile(const std::string& path, const char* data, size_t size)
{
    CLocal f;
    if (!f.Open(path, GENERIC_WRITE, CREATE_ALWAYS))
        return false;
    DWORD written = 0;
    return size == 0 || (WriteFile(f.get(), data, static_cast<DWORD>(size), &written, nullptr) && written == size);
}

bool ReadLocalFile(const std::string& path, std::vector<char>& out)
{
    out.clear();
    CLocal f;
    if (!f.Open(path, GENERIC_READ, OPEN_EXISTING))
        return false;
    out.resize(static_cast<size_t>(f.Size()));
    DWORD got = 0;
    return out.empty() || (ReadFile(f.get(), out.data(), static_cast<DWORD>(out.size()), &got, nullptr) &&
                           got == out.size());
}

class TransferTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        REQUIRE_SERVER();
        ASSERT_TRUE(SshGlobalInit());
        initialised_ = true;
        Connect();
        ASSERT_NE(session_, nullptr);

        const std::string name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
        const std::string unique = std::to_string(GetCurrentProcessId()) + "_" + name;
        remoteDir_ = "upload/xfer_" + unique;
        session_->Remove(remoteDir_ + "/a"); // nothing to do when the directory is fresh
        session_->Rmdir(remoteDir_);
        ASSERT_TRUE(session_->Mkdir(remoteDir_)) << session_->LastError().message;

        char temp[MAX_PATH];
        GetTempPathA(MAX_PATH, temp);
        localDir_ = std::string(temp) + "sally_sftp_xfer_" + unique;
        CreateDirectoryA(localDir_.c_str(), nullptr);
    }
    void TearDown() override
    {
        if (session_ != nullptr)
        {
            if (!session_->IsConnected() || session_->LastError().kind == SshErrorKind::Cancelled)
            {
                session_->Disconnect();
                session_->Connect(ServerSite());
            }
            if (session_->HasSftp())
            {
                std::vector<CSftpEntry> list;
                if (session_->ListDir(remoteDir_, list))
                    for (const CSftpEntry& e : list)
                        session_->Remove(remoteDir_ + "/" + e.rawName);
                session_->Rmdir(remoteDir_);
            }
            session_.reset();
        }
        for (const std::string& f : localFiles_)
            DeleteFileA(f.c_str());
        RemoveDirectoryA(localDir_.c_str());
        if (initialised_)
            SshGlobalExit();
    }

    void Connect()
    {
        session_.reset(new CSshSession(&cb_, nullptr));
        ASSERT_TRUE(session_->Connect(ServerSite())) << session_->LastError().message;
    }
    std::string Local(const std::string& name)
    {
        localFiles_.push_back(localDir_ + "\\" + name);
        return localFiles_.back();
    }
    std::string Remote(const std::string& name) const { return remoteDir_ + "/" + name; }

    // Uploads data completely as remote name
    void PutRemote(const std::string& name, const std::vector<char>& data)
    {
        const std::string local = Local("put_" + name);
        ASSERT_TRUE(WriteLocalFile(local, data.data(), data.size()));
        CLocal f;
        ASSERT_TRUE(f.Open(local, GENERIC_READ, OPEN_EXISTING));
        uint64_t n = 0;
        ASSERT_TRUE(session_->Upload(f.get(), Remote(name), 0, false, &n)) << session_->LastError().message;
        ASSERT_EQ(n, data.size());
    }
    // Downloads a remote path completely and returns its content
    void GetRemote(const std::string& remote, std::vector<char>& out)
    {
        const std::string local = Local("get_" + std::to_string(localFiles_.size()));
        CLocal f;
        ASSERT_TRUE(f.Open(local, GENERIC_WRITE, CREATE_ALWAYS));
        uint64_t n = 0, total = 0;
        ASSERT_TRUE(session_->Download(remote, f.get(), 0, &n, &total)) << session_->LastError().message;
        f.Close();
        ASSERT_TRUE(ReadLocalFile(local, out));
        ASSERT_EQ(n, out.size());
    }

    CTestCallbacks cb_;
    std::unique_ptr<CSshSession> session_;
    std::string remoteDir_, localDir_;
    std::vector<std::string> localFiles_;
    bool initialised_ = false;
};
} // namespace

TEST_F(TransferTest, RoundTripVariousSizes)
{
    const size_t sizes[] = {0, 1, 100 * kKiB, 5 * kMiB + 123};
    for (size_t size : sizes)
    {
        std::vector<char> data = RandomData(size, static_cast<unsigned>(size));
        PutRemote("rt.bin", data);
        CSftpEntry e;
        ASSERT_TRUE(session_->Stat(Remote("rt.bin"), e));
        EXPECT_EQ(e.size, size);
        std::vector<char> back;
        GetRemote(Remote("rt.bin"), back);
        EXPECT_EQ(back, data) << "size " << size;
    }
}

TEST_F(TransferTest, UploadOverwriteShorterTruncates)
{
    PutRemote("o.bin", RandomData(10000, 1));
    std::vector<char> shorter = RandomData(100, 2);
    PutRemote("o.bin", shorter);
    CSftpEntry e;
    ASSERT_TRUE(session_->Stat(Remote("o.bin"), e));
    EXPECT_EQ(e.size, 100u);
    std::vector<char> back;
    GetRemote(Remote("o.bin"), back);
    EXPECT_EQ(back, shorter);
}

TEST_F(TransferTest, RemoveAfterUpload)
{
    PutRemote("gone.bin", RandomData(10, 3));
    ASSERT_TRUE(session_->Remove(Remote("gone.bin"))) << session_->LastError().message;
    CSftpEntry e;
    EXPECT_FALSE(session_->Stat(Remote("gone.bin"), e));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
}

TEST_F(TransferTest, ResumeDownload)
{
    const std::vector<char> data = RandomData(300 * kKiB + 7, 4);
    PutRemote("r.bin", data);
    const size_t half = data.size() / 2;

    const std::string local = Local("resume_dl.bin");
    ASSERT_TRUE(WriteLocalFile(local, data.data(), half));
    CLocal f;
    ASSERT_TRUE(f.Open(local, GENERIC_WRITE, OPEN_EXISTING));
    uint64_t n = 0, total = 0;
    ASSERT_TRUE(session_->Download(Remote("r.bin"), f.get(), half, &n, &total)) << session_->LastError().message;
    EXPECT_EQ(n, data.size() - half);
    EXPECT_EQ(total, data.size());
    f.Close();

    std::vector<char> back;
    ASSERT_TRUE(ReadLocalFile(local, back));
    EXPECT_EQ(back, data);
}

TEST_F(TransferTest, ResumeUpload)
{
    const std::vector<char> data = RandomData(300 * kKiB + 7, 5);
    const size_t half = data.size() / 2;
    PutRemote("ru.bin", std::vector<char>(data.begin(), data.begin() + half)); // first half only

    const std::string local = Local("full.bin");
    ASSERT_TRUE(WriteLocalFile(local, data.data(), data.size()));
    CLocal f;
    ASSERT_TRUE(f.Open(local, GENERIC_READ, OPEN_EXISTING));
    uint64_t n = 0;
    ASSERT_TRUE(session_->Upload(f.get(), Remote("ru.bin"), half, true, &n)) << session_->LastError().message;
    EXPECT_EQ(n, data.size() - half);

    std::vector<char> back;
    GetRemote(Remote("ru.bin"), back);
    EXPECT_EQ(back, data);
}

TEST_F(TransferTest, ResumeWithBadOffsets)
{
    const std::vector<char> data = RandomData(1000, 6);
    PutRemote("b.bin", data);

    // download offset beyond the remote size
    CLocal out;
    ASSERT_TRUE(out.Open(Local("bad_dl.bin"), GENERIC_WRITE, CREATE_ALWAYS));
    uint64_t n = 99;
    EXPECT_FALSE(session_->Download(Remote("b.bin"), out.get(), 5000, &n, nullptr));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Io);
    EXPECT_NE(session_->LastError().message.find("beyond"), std::string::npos) << session_->LastError().message;
    EXPECT_EQ(n, 0u);

    // upload offset beyond the remote size (local file is long enough)
    const std::string local = Local("long.bin");
    const std::vector<char> big = RandomData(5000, 7);
    ASSERT_TRUE(WriteLocalFile(local, big.data(), big.size()));
    CLocal in;
    ASSERT_TRUE(in.Open(local, GENERIC_READ, OPEN_EXISTING));
    EXPECT_FALSE(session_->Upload(in.get(), Remote("b.bin"), 3000, true, &n));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Io);
    EXPECT_NE(session_->LastError().message.find("shorter than the resume offset"), std::string::npos)
        << session_->LastError().message;

    // the remote file is untouched, the session still works
    std::vector<char> back;
    GetRemote(Remote("b.bin"), back);
    EXPECT_EQ(back, data);

    // resume of a file that does not exist
    EXPECT_FALSE(session_->Upload(in.get(), Remote("missing.bin"), 10, true, &n));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
}

TEST_F(TransferTest, TailReadOfSparseFileBeyond4GiB)
{
    const uint64_t offset = 5 * kGiB - kMiB;
    const std::string local = Local("tail.bin");
    CLocal f;
    ASSERT_TRUE(f.Open(local, GENERIC_READ | GENERIC_WRITE, CREATE_ALWAYS));
    f.MakeSparse(); // keep the 5 GiB of hole out of the disk

    uint64_t n = 0, total = 0;
    ASSERT_TRUE(session_->Download("fixtures/sparse5g.bin", f.get(), offset, &n, &total))
        << session_->LastError().message;
    EXPECT_EQ(total, 5 * kGiB);
    EXPECT_EQ(n, kMiB);
    EXPECT_EQ(f.Size(), 5 * kGiB);

    std::vector<char> tail(static_cast<size_t>(kMiB));
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(offset);
    ASSERT_TRUE(SetFilePointerEx(f.get(), pos, nullptr, FILE_BEGIN));
    DWORD got = 0;
    ASSERT_TRUE(ReadFile(f.get(), tail.data(), static_cast<DWORD>(tail.size()), &got, nullptr));
    ASSERT_EQ(got, tail.size());
    EXPECT_EQ(tail, std::vector<char>(tail.size(), 0));
}

TEST_F(TransferTest, ProgressIsMonotonicAndEndsAtTotal)
{
    const std::vector<char> data = RandomData(5 * kMiB, 8);
    cb_.values.clear();
    PutRemote("p.bin", data);
    ASSERT_FALSE(cb_.values.empty());
    EXPECT_EQ(cb_.values.back(), data.size());
    EXPECT_EQ(cb_.lastTotal, data.size());
    for (size_t i = 1; i < cb_.values.size(); ++i)
        EXPECT_GE(cb_.values[i], cb_.values[i - 1]);

    cb_.values.clear();
    std::vector<char> back;
    GetRemote(Remote("p.bin"), back);
    ASSERT_FALSE(cb_.values.empty());
    EXPECT_EQ(cb_.values.back(), data.size());
    EXPECT_EQ(cb_.lastTotal, data.size());
    for (size_t i = 1; i < cb_.values.size(); ++i)
        EXPECT_GE(cb_.values[i], cb_.values[i - 1]);
}

TEST_F(TransferTest, ProgressCallbackCancelsDownload)
{
    // The callback is called at most every 100 ms, so use the big file and cancel at the first call
    cb_.cancelAt = 1;
    const std::string local = Local("cancel_cb.bin");
    CLocal f;
    ASSERT_TRUE(f.Open(local, GENERIC_WRITE, CREATE_ALWAYS));
    uint64_t n = 0;
    EXPECT_FALSE(session_->Download("fixtures/sparse5g.bin", f.get(), 0, &n, nullptr));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Cancelled);
    EXPECT_GT(n, 0u);
    EXPECT_LT(n, 5 * kGiB);
    EXPECT_EQ(f.Size(), n); // the partial data stays and matches the count
    f.Close();

    // not a connection problem: the same session still works
    CSftpEntry e;
    EXPECT_TRUE(session_->Stat("fixtures/small/hello.txt", e)) << session_->LastError().message;
}

TEST_F(TransferTest, CancelFromSecondThreadThenNewSessionWorks)
{
    const std::string local = Local("cancel_thread.bin");
    CLocal f;
    ASSERT_TRUE(f.Open(local, GENERIC_WRITE, CREATE_ALWAYS));

    std::thread canceller(
        [this]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            session_->Cancel();
        });
    const auto start = std::chrono::steady_clock::now();
    uint64_t n = 0;
    const bool ok = session_->Download("fixtures/sparse5g.bin", f.get(), 0, &n, nullptr);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    canceller.join();

    EXPECT_FALSE(ok);
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Cancelled);
    EXPECT_LT(elapsed, std::chrono::seconds(5));
    EXPECT_EQ(f.Size(), n);
    f.Close();

    session_->Disconnect();
    CSshSession fresh(&cb_, nullptr);
    ASSERT_TRUE(fresh.Connect(ServerSite())) << fresh.LastError().message;
    CSftpEntry e;
    EXPECT_TRUE(fresh.Stat("fixtures/small/hello.txt", e)) << fresh.LastError().message;
    // the old object works again after Disconnect
    ASSERT_TRUE(session_->Connect(ServerSite())) << session_->LastError().message;
}

TEST_F(TransferTest, LocalWriteErrorIsIo)
{
    const std::string local = Local("readonly_handle.bin");
    ASSERT_TRUE(WriteLocalFile(local, "x", 1));
    CLocal f;
    ASSERT_TRUE(f.Open(local, GENERIC_READ, OPEN_EXISTING)); // no write access
    uint64_t n = 0;
    EXPECT_FALSE(session_->Download("fixtures/small/hello.txt", f.get(), 0, &n, nullptr));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Io);
    EXPECT_NE(session_->LastError().message.find("Windows error"), std::string::npos)
        << session_->LastError().message;
    EXPECT_EQ(n, 0u);

    // the remote handle was closed cleanly: the session is fine
    CSftpEntry e;
    EXPECT_TRUE(session_->Stat("fixtures/small/hello.txt", e)) << session_->LastError().message;
}

TEST_F(TransferTest, UploadToRootOwnedDirIsPermissionDenied)
{
    const std::string local = Local("denied.bin");
    ASSERT_TRUE(WriteLocalFile(local, "abc", 3));
    CLocal f;
    ASSERT_TRUE(f.Open(local, GENERIC_READ, OPEN_EXISTING));
    uint64_t n = 0;
    EXPECT_FALSE(session_->Upload(f.get(), "/sally_denied_" + std::to_string(GetCurrentProcessId()), 0, false, &n));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::PermissionDenied) << session_->LastError().message;
}

TEST_F(TransferTest, DownloadOfMissingFileIsNotFound)
{
    CLocal f;
    ASSERT_TRUE(f.Open(Local("missing.bin"), GENERIC_WRITE, CREATE_ALWAYS));
    uint64_t n = 0;
    EXPECT_FALSE(session_->Download("fixtures/does-not-exist", f.get(), 0, &n, nullptr));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
}

TEST_F(TransferTest, DownloadFollowsFileSymlink)
{
    std::vector<char> viaLink, direct;
    GetRemote("fixtures/filelink", viaLink);
    GetRemote("fixtures/small/hello.txt", direct);
    EXPECT_FALSE(direct.empty());
    EXPECT_EQ(viaLink, direct);
}

TEST_F(TransferTest, UploadKeepsLocalMtime)
{
    const std::string local = Local("mtime.bin");
    ASSERT_TRUE(WriteLocalFile(local, "abc", 3));
    CLocal f;
    ASSERT_TRUE(f.Open(local, GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING));
    // 2020-01-02 03:04:05 UTC as FILETIME
    const ULONGLONG ft = (1577934245ULL + 11644473600ULL) * 10000000ULL;
    FILETIME t;
    t.dwLowDateTime = static_cast<DWORD>(ft & 0xFFFFFFFF);
    t.dwHighDateTime = static_cast<DWORD>(ft >> 32);
    ASSERT_TRUE(SetFileTime(f.get(), nullptr, nullptr, &t));
    uint64_t n = 0;
    ASSERT_TRUE(session_->Upload(f.get(), Remote("mtime.bin"), 0, false, &n, true)) << session_->LastError().message;
    CSftpEntry e;
    ASSERT_TRUE(session_->Stat(Remote("mtime.bin"), e));
    EXPECT_EQ(e.mtime, 1577934245);
}

// Run on demand: sftp_tests --gtest_also_run_disabled_tests --gtest_filter=Benchmark.*
// Docker loopback numbers are only good for comparing buffer sizes with each other, not as absolute speeds.
TEST(Benchmark, DISABLED_DownloadUpload64MiB)
{
    REQUIRE_SERVER();
    ASSERT_TRUE(SshGlobalInit());
    {
        CTestCallbacks cb;
        CSshSession session(&cb, nullptr);
        ASSERT_TRUE(session.Connect(ServerSite())) << session.LastError().message;

        char temp[MAX_PATH];
        GetTempPathA(MAX_PATH, temp);
        const std::string pid = std::to_string(GetCurrentProcessId());
        const std::string src = std::string(temp) + "sally_sftp_bench_src_" + pid;
        const std::string dst = std::string(temp) + "sally_sftp_bench_dst_" + pid;
        const std::string remote = "upload/bench_" + pid + ".bin";
        const std::vector<char> block = RandomData(static_cast<size_t>(kMiB), 42);
        {
            CLocal f;
            ASSERT_TRUE(f.Open(src, GENERIC_WRITE, CREATE_ALWAYS));
            for (int i = 0; i < 64; ++i)
            {
                DWORD w = 0;
                WriteFile(f.get(), block.data(), static_cast<DWORD>(block.size()), &w, nullptr);
            }
        }

        const size_t sizes[] = {32 * kKiB, 64 * kKiB, 128 * kKiB, 256 * kKiB, 1 * kMiB, 4 * kMiB};
        for (size_t bufSize : sizes)
        {
            session.SetTransferBufferSize(bufSize);
            double upSec = 0, downSec = 0;
            {
                CLocal f;
                ASSERT_TRUE(f.Open(src, GENERIC_READ, OPEN_EXISTING));
                uint64_t n = 0;
                const auto t0 = std::chrono::steady_clock::now();
                ASSERT_TRUE(session.Upload(f.get(), remote, 0, false, &n)) << session.LastError().message;
                upSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            }
            {
                CLocal f;
                ASSERT_TRUE(f.Open(dst, GENERIC_WRITE, CREATE_ALWAYS));
                uint64_t n = 0;
                const auto t0 = std::chrono::steady_clock::now();
                ASSERT_TRUE(session.Download(remote, f.get(), 0, &n, nullptr)) << session.LastError().message;
                downSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            }
            std::printf("buffer %7zu KiB: upload %7.1f MB/s, download %7.1f MB/s\n", bufSize / 1024,
                        67.108864 / upSec, 67.108864 / downSec);
        }
        session.Remove(remote);
        DeleteFileA(src.c_str());
        DeleteFileA(dst.c_str());
    }
    SshGlobalExit();
}

TEST_F(TransferTest, NonZeroOffsetWithoutResumeDoesNotCreateTheRemoteFile)
{
    const std::string local = Local("offset_nocreate.bin");
    const std::vector<char> big = RandomData(100, 9);
    ASSERT_TRUE(WriteLocalFile(local, big.data(), big.size()));
    CLocal in;
    ASSERT_TRUE(in.Open(local, GENERIC_READ, OPEN_EXISTING));
    uint64_t n = 0;
    EXPECT_FALSE(session_->Upload(in.get(), Remote("never_created.bin"), 10, false, &n));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound) << session_->LastError().message;
    CSftpEntry e;
    EXPECT_FALSE(session_->Stat(Remote("never_created.bin"), e)); // no empty file left behind
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
}

TEST_F(TransferTest, DownloadOffsetOnEmptyRemoteFileIsAnError)
{
    PutRemote("zero.bin", std::vector<char>());
    CLocal out;
    ASSERT_TRUE(out.Open(Local("zero_dl.bin"), GENERIC_WRITE, CREATE_ALWAYS));
    uint64_t n = 0;
    EXPECT_FALSE(session_->Download(Remote("zero.bin"), out.get(), 5, &n, nullptr));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Io) << session_->LastError().message;
    EXPECT_TRUE(session_->Download(Remote("zero.bin"), out.get(), 0, &n, nullptr)); // offset 0 is fine
    EXPECT_EQ(n, 0u);
}
