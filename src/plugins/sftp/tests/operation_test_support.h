// Shared fixture of the operation tests (COperation against the Docker test server).
#pragma once

#include "test_support.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <thread>

#include "listing_format.h"
#include "operation.h"
#include "ssh_session.h"

namespace
{
namespace fs = std::filesystem;

// Accepts any host key once; remembers the fingerprint so the test can hand it to the workers.
class CAcceptAll : public ISshCallbacks
{
public:
    HostKeyDecision VerifyHostKey(const std::string&, int, const std::string&, const std::string&, bool) override
    {
        return HostKeyDecision::AcceptOnce;
    }
    bool Prompt(PromptKind, const std::string&, const std::string&, const std::string&, bool, CSecret&) override
    {
        return false;
    }
    bool Progress(uint64_t, uint64_t) override { return true; }
};

CSite TestSite(int port = sftptest::kPort, int timeoutSeconds = 20)
{
    CSite site;
    site.name = L"test";
    site.host = L"127.0.0.1";
    site.port = port;
    site.user = L"test";
    site.timeoutSeconds = timeoutSeconds;
    return site;
}

// Counts what the operation tells (all calls come from worker threads).
class CTestObserver : public IOperationObserver
{
public:
    std::atomic<int> items{0};
    std::atomic<int> progress{0};
    std::atomic<int> states{0};
    std::atomic<int> finished{0};
    void OnItemChanged(uint64_t) override { ++items; }
    void OnProgress() override { ++progress; }
    void OnStateChanged() override { ++states; }
    void OnFinished() override { ++finished; }
    std::atomic<int> interactions{0};
    void OnInteractionPending() override { ++interactions; }
};

// True when the condition became true within timeoutMs (polls every 5 ms).
bool WaitUntil(const std::function<bool()>& pred, DWORD timeoutMs)
{
    const DWORD start = GetTickCount();
    while (!pred())
    {
        if (GetTickCount() - start > timeoutMs)
            return false;
        Sleep(5);
    }
    return true;
}

DWORD HandleCount()
{
    DWORD n = 0;
    GetProcessHandleCount(GetCurrentProcess(), &n);
    return n;
}

int64_t UnixMtimeOfLocal(const fs::path& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return -1;
    ULARGE_INTEGER t;
    t.LowPart = data.ftLastWriteTime.dwLowDateTime;
    t.HighPart = data.ftLastWriteTime.dwHighDateTime;
    return (static_cast<int64_t>(t.QuadPart) - 116444736000000000LL) / 10000000;
}

std::string ReadAll(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteAll(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string RandomBytes(size_t size, unsigned seed)
{
    std::string bytes(size, '\0');
    unsigned state = seed * 2654435761u + 1;
    for (char& c : bytes)
    {
        state = state * 1664525u + 1013904223u;
        c = static_cast<char>(state >> 24);
    }
    return bytes;
}

class OperationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        REQUIRE_SERVER();
        static std::atomic<int> counter{0};
        const std::string unique = std::to_string(GetCurrentProcessId()) + "_" + std::to_string(counter++);
        remoteDir_ = "upload/op_" + unique;
        local_ = fs::temp_directory_path() / ("sally_op_" + unique);
        fs::remove_all(local_);
        fs::create_directories(local_);
        ASSERT_TRUE(admin_.Connect(Admin())) << admin_.LastError().message;
        ASSERT_TRUE(admin_.Mkdir(remoteDir_)) << admin_.LastError().message;
    }

    void TearDown() override
    {
        if (admin_.IsConnected())
            RemoveRemoteTree(remoteDir_);
        std::error_code ignored;
        fs::remove_all(local_, ignored);
    }

    CSshSite Admin() const
    {
        return ToSshSite(TestSite(), CSecret(sftptest::kPassword), CSecret());
    }

    // The connect parameters a panel session would hand over.
    CPanelConnectParams Params()
    {
        CPanelConnectParams params;
        params.site = TestSite(port_, timeoutSeconds_);
        params.password = CSecret(sftptest::kPassword);
        params.hostKeyFingerprint = admin_.HostKeyFingerprint();
        params.hostKeyType = admin_.HostKeyType();
        params.valid = true;
        return params;
    }

    COperationSettings Settings(TransferDirection direction, bool move = false, int workers = 2)
    {
        COperationSettings settings = SettingsFromSite(TestSite(), direction, move);
        settings.workers = workers;
        settings.policy = policy_;
        settings.safeUpload = safeUpload_;
        settings.reconnectDelaysMs = delays_;
        settings.localRoot = local_.wstring();
        settings.remoteRoot = remoteDir_;
        return settings;
    }

    static CRemoteSource RemoteSrc(const std::string& path, bool isDir, uint64_t size = 0, int64_t mtime = 0)
    {
        CRemoteSource s;
        s.path = path;
        s.isDir = isDir;
        s.size = size;
        s.mtime = mtime;
        return s;
    }

    static CLocalSource LocalSrc(const fs::path& path, bool isDir)
    {
        CLocalSource s;
        s.path = path.wstring();
        s.isDir = isDir;
        return s;
    }

    std::unique_ptr<COperation> MakeDownload(const std::vector<CRemoteSource>& sources, bool move = false, int workers = 2)
    {
        const COperationSettings settings = Settings(TransferDirection::Download, move, workers);
        return std::make_unique<COperation>(
            settings, Params(), COperation::BuildDownloadRoots(sources, settings, CNameCodec::Auto()));
    }

    // Delete on the server: the sources are full remote paths (isDir tells a directory from a file or link).
    std::unique_ptr<COperation> MakeDelete(const std::vector<CRemoteSource>& sources, int workers = 2)
    {
        const COperationSettings settings = Settings(TransferDirection::Delete, false, workers);
        return std::make_unique<COperation>(
            settings, Params(), COperation::BuildDeleteRoots(sources));
    }

    // Chmod on the server: the sources are full remote paths. 'recursive' explores directories; files/dirs choose what
    // gets the change then.
    std::unique_ptr<COperation> MakeChmod(const std::vector<CRemoteSource>& sources, const CModeChange& change,
                                          bool recursive, bool files = true, bool dirs = true, int workers = 2)
    {
        COperationSettings settings = Settings(TransferDirection::Chmod, false, workers);
        settings.modeChange = change;
        settings.chmodRecursive = recursive;
        settings.chmodFiles = files;
        settings.chmodDirs = dirs;
        return std::make_unique<COperation>(
            settings, Params(), COperation::BuildChmodRoots(sources, settings));
    }

    std::unique_ptr<COperation> MakeUpload(const std::vector<CLocalSource>& sources, const std::string& remoteRoot,
                                           bool move = false, int workers = 2)
    {
        COperationSettings settings = Settings(TransferDirection::Upload, move, workers);
        settings.remoteRoot = remoteRoot;
        return std::make_unique<COperation>(
            settings, Params(), COperation::BuildUploadRoots(sources, settings, CNameCodec::Auto()));
    }

    // Starts the operation and waits for the end. False on a timeout (the operation is cancelled then).
    static bool Run(COperation& op, CTestObserver& observer, DWORD timeoutMs = 120000)
    {
        op.SetObserver(&observer);
        EXPECT_TRUE(op.Start());
        if (op.WaitFinished(timeoutMs))
            return true;
        op.RequestCancel();
        op.WaitFinished(20000);
        return false;
    }

    // Bytes that differ from block to block (so a copy that starts at a wrong offset cannot match by accident).
    static std::string PatternBytes(size_t size, unsigned seed)
    {
        const std::string base = RandomBytes(64 * 1024, seed);
        std::string bytes;
        bytes.reserve(size);
        for (uint64_t block = 0; bytes.size() < size; ++block)
        {
            std::string chunk = base;
            memcpy(&chunk[0], &block, sizeof(block));
            bytes.append(chunk, 0, size - bytes.size());
        }
        return bytes;
    }

    static void SetLocalMtime(const fs::path& path, int64_t unixSeconds)
    {
        HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        ASSERT_NE(h, INVALID_HANDLE_VALUE);
        const FILETIME ft = UnixTimeToFileTime(unixSeconds);
        EXPECT_TRUE(SetFileTime(h, nullptr, nullptr, &ft));
        CloseHandle(h);
    }

    static std::string ToUtf8(const std::wstring& text)
    {
        std::string out;
        for (wchar_t c : text)
            out += c < 128 ? static_cast<char>(c) : '?';
        return out;
    }

    void PutRemoteFile(const std::string& remotePath, const std::string& content)
    {
        const fs::path temp = local_ / "put.tmp";
        WriteAll(temp, content);
        HANDLE h = CreateFileW(temp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        ASSERT_NE(h, INVALID_HANDLE_VALUE);
        uint64_t n = 0;
        EXPECT_TRUE(admin_.Upload(h, remotePath, 0, false, &n)) << admin_.LastError().message;
        CloseHandle(h);
        fs::remove(temp);
    }

    // Reads a remote file through a separate session (the reference for the compare).
    std::string GetRemoteFile(const std::string& remotePath)
    {
        const fs::path temp = local_ / "get.tmp";
        HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        EXPECT_NE(h, INVALID_HANDLE_VALUE);
        uint64_t n = 0;
        EXPECT_TRUE(admin_.Download(remotePath, h, 0, &n, nullptr)) << admin_.LastError().message;
        CloseHandle(h);
        const std::string content = ReadAll(temp);
        fs::remove(temp);
        return content;
    }

    bool RemoteExists(const std::string& path)
    {
        CSftpEntry e;
        return admin_.LStat(path, e);
    }

    void RemoveRemoteTree(const std::string& path)
    {
        CSftpEntry e;
        if (!admin_.LStat(path, e))
            return;
        if (e.isLink || !e.isDir)
        {
            admin_.Remove(path);
            return;
        }
        std::vector<CSftpEntry> entries;
        if (admin_.ListDir(path, entries))
            for (const CSftpEntry& child : entries)
                RemoveRemoteTree(AppendRemoteName(path, child.rawName));
        admin_.Rmdir(path);
    }

    // The items that need a look, for failure messages.
    static std::string Describe(COperation& op)
    {
        std::string text;
        for (const CQueueItem& item : op.Queue().Items(true))
        {
            text += "\n  item " + std::to_string(item.uid) + " state " + std::to_string(static_cast<int>(item.state)) +
                    " problem " + std::to_string(static_cast<int>(item.problem)) + " remote " + item.remotePath + " : ";
            for (wchar_t c : item.errorText)
                text += c < 128 ? static_cast<char>(c) : '?';
        }
        return text;
    }

    static std::vector<CQueueItem> ItemsWithProblem(COperation& op, Problem problem)
    {
        std::vector<CQueueItem> result;
        for (const CQueueItem& item : op.Queue().Items(true))
            if (item.problem == problem)
                result.push_back(item);
        return result;
    }

    // A directory junction (needs no privilege, unlike a symbolic link): link -> target.
    static bool MakeJunction(const fs::path& link, const fs::path& target)
    {
        const std::wstring command = L"cmd.exe /c mklink /J \"" + link.wstring() + L"\" \"" + target.wstring() +
                                     L"\" >nul 2>nul";
        std::vector<wchar_t> buffer(command.begin(), command.end());
        buffer.push_back(L'\0');
        STARTUPINFOW startup = {sizeof(startup)};
        PROCESS_INFORMATION process = {};
        if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                            &process))
            return false;
        WaitForSingleObject(process.hProcess, 20000);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
        return fs::exists(link);
    }

    CAcceptAll acceptAll_;
    CSshSession admin_{&acceptAll_, nullptr};
    std::string remoteDir_;
    fs::path local_;
    ConflictPolicy policy_ = ConflictPolicy::Ask;
    bool safeUpload_ = true;
    int port_ = sftptest::kPort;     // the workers connect here (a proxy port in some tests)
    int timeoutSeconds_ = 20;        // idle timeout of the workers' sessions
    std::vector<unsigned> delays_ = {10, 10, 10}; // reconnect backoff: short in tests
};
} // namespace
