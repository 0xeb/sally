#include "test_support.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

#include "ssh_init.h"
#include "ssh_session.h"

namespace
{
struct CNoCallbacks : ISshCallbacks
{
    HostKeyDecision VerifyHostKey(const std::string&, int, const std::string&, const std::string&, bool) override
    {
        return HostKeyDecision::AcceptOnce;
    }
    bool Prompt(PromptKind, const std::string&, const std::string&, const std::string&, bool, CSecret&) override { return false; }
    bool Progress(uint64_t, uint64_t) override { return true; }
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

const std::string kFix = "fixtures/";

class OpsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        REQUIRE_SERVER();
        ASSERT_TRUE(SshGlobalInit());
        initialised_ = true;
        session_.reset(new CSshSession(&cb_, nullptr));
        ASSERT_TRUE(session_->Connect(ServerSite())) << session_->LastError().message;
        dir_ = "upload/ops_" + std::to_string(GetCurrentProcessId()) + "_" +
               ::testing::UnitTest::GetInstance()->current_test_info()->name();
        RemoveTree(dir_); // leftovers of an aborted run
        ASSERT_TRUE(session_->Mkdir(dir_)) << session_->LastError().message;
    }
    void TearDown() override
    {
        if (session_ != nullptr)
        {
            if (!session_->IsConnected())
                session_->Connect(ServerSite());
            if (session_->HasSftp())
                RemoveTree(dir_);
            session_.reset();
        }
        if (initialised_)
            SshGlobalExit();
    }

    // Removes a directory tree (files, links and directories) on the server; ignores errors
    void RemoveTree(const std::string& path)
    {
        std::vector<CSftpEntry> list;
        if (session_->ListDir(path, list))
        {
            for (const CSftpEntry& e : list)
            {
                std::string full = path + "/" + e.rawName;
                if (e.isDir && !e.isLink)
                    RemoveTree(full);
                else
                    session_->Remove(full);
            }
        }
        session_->Rmdir(path);
    }

    std::string In(const std::string& name) const { return dir_ + "/" + name; }

    // Uploads a small file with this content (through a local temp file)
    bool PutFile(const std::string& remote, const std::string& content)
    {
        const std::string local = TempFile();
        {
            std::ofstream out(local, std::ios::binary);
            out << content;
        }
        HANDLE h = CreateFileA(local.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        bool ok = h != INVALID_HANDLE_VALUE && session_->Upload(h, remote, 0, false, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        DeleteFileA(local.c_str());
        return ok;
    }
    bool GetFile(const std::string& remote, std::string& content)
    {
        const std::string local = TempFile();
        HANDLE h = CreateFileA(local.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        bool ok = h != INVALID_HANDLE_VALUE && session_->Download(remote, h, 0, nullptr, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        std::ifstream in(local, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        content = ss.str();
        DeleteFileA(local.c_str());
        return ok;
    }
    static std::string TempFile()
    {
        char dir[MAX_PATH], name[MAX_PATH];
        GetTempPathA(MAX_PATH, dir);
        GetTempFileNameA(dir, "sfo", 0, name);
        return name;
    }
    std::vector<std::string> Names(const std::string& folder)
    {
        std::vector<CSftpEntry> list;
        std::vector<std::string> names;
        if (session_->ListDir(folder, list))
            for (const CSftpEntry& e : list)
                names.push_back(e.rawName);
        std::sort(names.begin(), names.end());
        return names;
    }

    CNoCallbacks cb_;
    std::unique_ptr<CSshSession> session_;
    std::string dir_;
    bool initialised_ = false;
};

const CSftpEntry* Find(const std::vector<CSftpEntry>& list, const std::string& name)
{
    for (const CSftpEntry& e : list)
        if (e.rawName == name)
            return &e;
    return nullptr;
}
} // namespace

TEST_F(OpsTest, ListFixturesFilesDirsAndLinks)
{
    std::vector<CSftpEntry> list;
    ASSERT_TRUE(session_->ListDir("fixtures", list)) << session_->LastError().message;

    for (const CSftpEntry& e : list)
        EXPECT_TRUE(e.rawName != "." && e.rawName != "..");

    const CSftpEntry* smallDir = Find(list, "small");
    ASSERT_NE(smallDir, nullptr);
    EXPECT_TRUE(smallDir->isDir);
    EXPECT_FALSE(smallDir->isLink);

    const CSftpEntry* dirlink = Find(list, "dirlink");
    ASSERT_NE(dirlink, nullptr);
    EXPECT_TRUE(dirlink->isLink);
    EXPECT_TRUE(dirlink->linkTargetIsDir);

    const CSftpEntry* filelink = Find(list, "filelink");
    ASSERT_NE(filelink, nullptr);
    EXPECT_TRUE(filelink->isLink);
    EXPECT_FALSE(filelink->linkTargetIsDir);

    const CSftpEntry* broken = Find(list, "brokenlink");
    ASSERT_NE(broken, nullptr);
    EXPECT_TRUE(broken->isLink);
    EXPECT_FALSE(broken->linkTargetIsDir);

    std::vector<CSftpEntry> files;
    ASSERT_TRUE(session_->ListDir("fixtures/small", files));
    const CSftpEntry* hello = Find(files, "hello.txt");
    ASSERT_NE(hello, nullptr);
    EXPECT_FALSE(hello->isDir);
    EXPECT_FALSE(hello->isLink);
    EXPECT_EQ(hello->size, 6u);
    EXPECT_EQ(Find(files, "random1m.bin")->size, 1048576u);
    EXPECT_EQ(hello->owner, Find(files, "random1k.bin")->owner);
    EXPECT_FALSE(hello->owner.empty());
}

TEST_F(OpsTest, ListEmptyDirectory)
{
    std::vector<CSftpEntry> list;
    list.resize(3);
    ASSERT_TRUE(session_->ListDir(dir_, list)) << session_->LastError().message;
    EXPECT_TRUE(list.empty());
}

TEST_F(OpsTest, ListMissingDirectoryAndFile)
{
    std::vector<CSftpEntry> list;
    EXPECT_FALSE(session_->ListDir(In("missing"), list));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound) << session_->LastError().message;
    EXPECT_TRUE(list.empty());
    EXPECT_FALSE(session_->ListDir(kFix + "small/hello.txt", list)); // a file is not a directory
    EXPECT_TRUE(session_->LastError().IsError());
}

TEST_F(OpsTest, ListFiveThousandFilesCompleteAndUnique)
{
    std::vector<CSftpEntry> list;
    ASSERT_TRUE(session_->ListDir("fixtures/many", list)) << session_->LastError().message;
    EXPECT_EQ(list.size(), 5000u);
    std::set<std::string> names;
    for (const CSftpEntry& e : list)
        names.insert(e.rawName);
    EXPECT_EQ(names.size(), 5000u);
    EXPECT_TRUE(names.count("file_0.txt") == 1 && names.count("file_4999.txt") == 1);
}

TEST_F(OpsTest, NonUtf8NamePreservedAndUsable)
{
    std::vector<CSftpEntry> list;
    ASSERT_TRUE(session_->ListDir("fixtures", list));
    const std::string raw = "caf\xE9.txt";
    const CSftpEntry* e = Find(list, raw);
    ASSERT_NE(e, nullptr) << "name with byte 0xE9 not found byte-exact";
    EXPECT_EQ(e->rawName.size(), 8u);
    EXPECT_FALSE(IsValidUtf8(e->rawName));

    CNameCodec codec = CNameCodec::Auto();
    std::wstring shown = codec.Decode(e->rawName);
    EXPECT_EQ(shown, std::wstring(L"caf\u00E9.txt"));

    CSftpEntry st;
    ASSERT_TRUE(session_->Stat(kFix + raw, st)) << session_->LastError().message;
    EXPECT_EQ(st.size, 7u); // "latin1\n"
    EXPECT_FALSE(st.isDir);

    // a directory with a raw byte in its name: create, stat, rename, list, remove, all by raw bytes
    const std::string d1 = In("d\xE9r"), d2 = In("e\xE8r\xFF");
    ASSERT_TRUE(session_->Mkdir(d1)) << session_->LastError().message;
    EXPECT_TRUE(session_->Stat(d1, st));
    EXPECT_TRUE(st.isDir);
    ASSERT_TRUE(session_->Rename(d1, d2, false)) << session_->LastError().message;
    EXPECT_FALSE(session_->Stat(d1, st));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
    ASSERT_TRUE(session_->ListDir(dir_, list));
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].rawName, std::string("e\xE8r\xFF"));
    EXPECT_TRUE(session_->Rmdir(d2)) << session_->LastError().message;
}

TEST_F(OpsTest, Utf8DirectoryNamesRoundTrip)
{
    CNameCodec codec = CNameCodec::Utf8();
    const std::string name = codec.Encode(L"p\u0159\u00EDli\u0161 \u017Elu\u0165ou\u010Dk\u00FD \u65E5\u672C");
    EXPECT_TRUE(IsValidUtf8(name));
    ASSERT_TRUE(session_->Mkdir(In(name))) << session_->LastError().message;
    std::vector<CSftpEntry> list;
    ASSERT_TRUE(session_->ListDir(dir_, list));
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].rawName, name);
    EXPECT_EQ(codec.Decode(list[0].rawName), L"p\u0159\u00EDli\u0161 \u017Elu\u0165ou\u010Dk\u00FD \u65E5\u672C");
    EXPECT_TRUE(session_->Rmdir(In(name)));
}

TEST_F(OpsTest, SizeIs64Bit)
{
    CSftpEntry st;
    ASSERT_TRUE(session_->Stat(kFix + "sparse5g.bin", st)) << session_->LastError().message;
    EXPECT_EQ(st.size, 5ull * 1024 * 1024 * 1024);
    EXPECT_FALSE(st.isDir);
}

TEST_F(OpsTest, SetTimesRoundTrip)
{
    const int64_t t = 1700000000; // 2023-11-14
    ASSERT_TRUE(session_->SetTimes(dir_, t + 5, t)) << session_->LastError().message;
    CSftpEntry st;
    ASSERT_TRUE(session_->Stat(dir_, st));
    EXPECT_EQ(st.mtime, t);

    // beyond 2106 and before 1970 do not fit the 32-bit fields: refused, not truncated
    EXPECT_FALSE(session_->SetTimes(dir_, 0, 5000000000LL));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Unsupported);
    EXPECT_FALSE(session_->SetTimes(dir_, -1, 0));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Unsupported);
    ASSERT_TRUE(session_->Stat(dir_, st));
    EXPECT_EQ(st.mtime, t);
}

TEST_F(OpsTest, ChmodRoundTrip)
{
    const std::string d = In("modes");
    ASSERT_TRUE(session_->Mkdir(d, 0755));
    CSftpEntry st;
    ASSERT_TRUE(session_->Stat(d, st));
    EXPECT_EQ(st.permissions & 07777, 0755u);
    ASSERT_TRUE(session_->Chmod(d, 0700)) << session_->LastError().message;
    ASSERT_TRUE(session_->Stat(d, st));
    EXPECT_EQ(st.permissions & 07777, 0700u);
    EXPECT_TRUE(st.isDir); // type bits stay
    EXPECT_FALSE(session_->Chmod(In("missing"), 0700));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
}

TEST_F(OpsTest, RenameOntoExisting)
{
    const std::string a = In("a"), b = In("b");
    ASSERT_TRUE(session_->Mkdir(a));
    ASSERT_TRUE(session_->Mkdir(b));
    ASSERT_TRUE(session_->Mkdir(In("a/marker")));

    EXPECT_FALSE(session_->Rename(a, b, false));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Exists) << session_->LastError().message;
    CSftpEntry st;
    EXPECT_TRUE(session_->Stat(a, st)); // nothing moved

    ASSERT_TRUE(session_->Rename(a, b, true)) << session_->LastError().message;
    EXPECT_FALSE(session_->Stat(a, st));
    EXPECT_TRUE(session_->Stat(In("b/marker"), st));
    EXPECT_TRUE(session_->Rmdir(In("b/marker")));
    EXPECT_TRUE(session_->Rmdir(b));

    EXPECT_FALSE(session_->Rename(In("missing"), In("x"), false));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound) << session_->LastError().message;
}

TEST_F(OpsTest, MkdirExistingIsExists)
{
    EXPECT_FALSE(session_->Mkdir(dir_));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Exists) << session_->LastError().message;
    EXPECT_FALSE(session_->Mkdir(kFix + "small/hello.txt")); // an existing file counts too
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Exists);
}

TEST_F(OpsTest, RmdirNonEmptyIsNotEmpty)
{
    ASSERT_TRUE(session_->Mkdir(In("full")));
    ASSERT_TRUE(session_->Mkdir(In("full/inner")));
    EXPECT_FALSE(session_->Rmdir(In("full")));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotEmpty) << session_->LastError().message;
    EXPECT_TRUE(session_->Rmdir(In("full/inner")));
    EXPECT_TRUE(session_->Rmdir(In("full")));
    EXPECT_FALSE(session_->Rmdir(In("full")));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
}

TEST_F(OpsTest, RemoveErrors)
{
    EXPECT_FALSE(session_->Remove(In("missing")));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
    EXPECT_FALSE(session_->Remove("/etc/passwd")); // not ours
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::PermissionDenied) << session_->LastError().message;
}

TEST_F(OpsTest, StatMissingIsNotFound)
{
    CSftpEntry st;
    EXPECT_FALSE(session_->Stat(In("nothing"), st));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
    EXPECT_EQ(session_->LastError().sftpStatus, 2ul);
    EXPECT_NE(session_->LastError().message.find("nothing"), std::string::npos);
    EXPECT_FALSE(session_->LStat(In("nothing"), st));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
}

TEST_F(OpsTest, PermissionDenied)
{
    EXPECT_FALSE(session_->Mkdir("/sally_not_allowed"));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::PermissionDenied) << session_->LastError().message;
    EXPECT_EQ(session_->LastError().sftpStatus, 3ul);
    std::vector<CSftpEntry> list;
    EXPECT_FALSE(session_->ListDir("/root", list));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::PermissionDenied) << session_->LastError().message;
}

TEST_F(OpsTest, StatVersusLStatOnLinks)
{
    CSftpEntry st;
    ASSERT_TRUE(session_->LStat(kFix + "dirlink", st)) << session_->LastError().message;
    EXPECT_TRUE(st.isLink);
    EXPECT_TRUE(st.linkTargetIsDir);
    ASSERT_TRUE(session_->Stat(kFix + "dirlink", st));
    EXPECT_FALSE(st.isLink);
    EXPECT_TRUE(st.isDir);
    ASSERT_TRUE(session_->LStat(kFix + "brokenlink", st));
    EXPECT_TRUE(st.isLink);
    EXPECT_FALSE(st.linkTargetIsDir);
    EXPECT_FALSE(session_->Stat(kFix + "brokenlink", st));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound);
}

TEST_F(OpsTest, ReadLinkAndRealPath)
{
    std::string target;
    ASSERT_TRUE(session_->ReadLink(kFix + "filelink", target)) << session_->LastError().message;
    EXPECT_EQ(target, "small/hello.txt");
    ASSERT_TRUE(session_->ReadLink(kFix + "brokenlink", target));
    EXPECT_EQ(target, "missing-target");
    EXPECT_FALSE(session_->ReadLink(kFix + "small/hello.txt", target)); // not a link
    EXPECT_TRUE(session_->LastError().IsError());

    std::string home, real;
    ASSERT_TRUE(session_->RealPath(".", home)) << session_->LastError().message;
    EXPECT_EQ(home, "/home/test");
    ASSERT_TRUE(session_->RealPath(kFix + "dirlink", real));
    EXPECT_EQ(real, "/home/test/fixtures/nested");
    ASSERT_TRUE(session_->RealPath(kFix + "small/../small/.", real));
    EXPECT_EQ(real, "/home/test/fixtures/small");
}

TEST_F(OpsTest, SymlinkCreatesALinkThatReadLinkAndStatSee)
{
    ASSERT_TRUE(session_->Mkdir(dir_ + "/real"));
    ASSERT_TRUE(session_->Symlink("real", dir_ + "/link")) << session_->LastError().message;
    std::string target;
    ASSERT_TRUE(session_->ReadLink(dir_ + "/link", target));
    EXPECT_EQ(target, "real");
    CSftpEntry entry;
    ASSERT_TRUE(session_->LStat(dir_ + "/link", entry));
    EXPECT_TRUE(entry.isLink);
    ASSERT_TRUE(session_->Stat(dir_ + "/link", entry));
    EXPECT_TRUE(entry.isDir);
    EXPECT_FALSE(session_->Symlink("real", dir_ + "/link")); // exists already
    ASSERT_TRUE(session_->Remove(dir_ + "/link"));
}

TEST_F(OpsTest, OperationsAfterDisconnectFailCleanly)
{
    session_->Disconnect();
    std::vector<CSftpEntry> list;
    CSftpEntry st;
    std::string s;
    EXPECT_FALSE(session_->ListDir("fixtures", list));
    EXPECT_TRUE(session_->LastError().IsError());
    EXPECT_FALSE(session_->Stat("fixtures", st));
    EXPECT_FALSE(session_->LStat("fixtures", st));
    EXPECT_FALSE(session_->RealPath(".", s));
    EXPECT_FALSE(session_->ReadLink("x", s));
    EXPECT_FALSE(session_->Mkdir("x"));
    EXPECT_FALSE(session_->Rmdir("x"));
    EXPECT_FALSE(session_->Remove("x"));
    EXPECT_FALSE(session_->Rename("x", "y", true));
    EXPECT_FALSE(session_->Chmod("x", 0700));
    EXPECT_FALSE(session_->SetTimes("x", 1, 1));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Other);
}

TEST_F(OpsTest, CancelBeforeCallThenFreshSessionWorks)
{
    session_->Cancel();
    std::vector<CSftpEntry> list;
    EXPECT_FALSE(session_->ListDir("fixtures/many", list));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Cancelled);
    CSftpEntry st;
    EXPECT_FALSE(session_->Stat("fixtures", st));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Cancelled);

    session_->Disconnect();
    ASSERT_TRUE(session_->Connect(ServerSite())) << session_->LastError().message;
    ASSERT_TRUE(session_->ListDir("fixtures/many", list)) << session_->LastError().message;
    EXPECT_EQ(list.size(), 5000u);
}

TEST_F(OpsTest, CancelDuringListing)
{
    // Timing dependent: the listing may finish first. Either outcome is valid, the session must stay usable.
    std::thread canceller(
        [this]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            session_->Cancel();
        });
    std::vector<CSftpEntry> list;
    bool ok = session_->ListDir("fixtures/many", list);
    canceller.join();
    if (ok)
        EXPECT_EQ(list.size(), 5000u);
    else
    {
        EXPECT_EQ(session_->LastError().kind, SshErrorKind::Cancelled) << session_->LastError().message;
        EXPECT_TRUE(list.empty());
    }
    session_->Disconnect();
    ASSERT_TRUE(session_->Connect(ServerSite())) << session_->LastError().message;
    ASSERT_TRUE(session_->ListDir("fixtures/small", list));
    EXPECT_EQ(list.size(), 3u);
}

// ---- Rename with overwrite: the target is moved aside, never deleted first -------------------------------------

TEST_F(OpsTest, OverwriteFileOntoFile)
{
    ASSERT_TRUE(PutFile(In("src"), "new content"));
    ASSERT_TRUE(PutFile(In("dst"), "old content"));
    ASSERT_TRUE(session_->Rename(In("src"), In("dst"), true)) << session_->LastError().message;
    std::string text;
    ASSERT_TRUE(GetFile(In("dst"), text));
    EXPECT_EQ(text, "new content");
    EXPECT_EQ(Names(dir_), std::vector<std::string>{"dst"}); // no aside file left
}

TEST_F(OpsTest, OverwriteFileOntoEmptyDirectory)
{
    ASSERT_TRUE(PutFile(In("src"), "payload"));
    ASSERT_TRUE(session_->Mkdir(In("dst")));
    ASSERT_TRUE(session_->Rename(In("src"), In("dst"), true)) << session_->LastError().message;
    CSftpEntry st;
    ASSERT_TRUE(session_->Stat(In("dst"), st));
    EXPECT_FALSE(st.isDir);
    std::string text;
    ASSERT_TRUE(GetFile(In("dst"), text));
    EXPECT_EQ(text, "payload");
    EXPECT_EQ(Names(dir_), std::vector<std::string>{"dst"});
}

TEST_F(OpsTest, OverwriteDirectoryOntoFile)
{
    ASSERT_TRUE(session_->Mkdir(In("src")));
    ASSERT_TRUE(session_->Mkdir(In("src/inner")));
    ASSERT_TRUE(PutFile(In("dst"), "old"));
    ASSERT_TRUE(session_->Rename(In("src"), In("dst"), true)) << session_->LastError().message;
    CSftpEntry st;
    ASSERT_TRUE(session_->Stat(In("dst/inner"), st)) << session_->LastError().message;
    EXPECT_EQ(Names(dir_), std::vector<std::string>{"dst"});
}

TEST_F(OpsTest, OverwriteOntoNonEmptyDirectoryIsExistsAndTouchesNothing)
{
    ASSERT_TRUE(PutFile(In("src"), "payload"));
    ASSERT_TRUE(session_->Mkdir(In("dst")));
    ASSERT_TRUE(PutFile(In("dst/keep.txt"), "keep me"));
    EXPECT_FALSE(session_->Rename(In("src"), In("dst"), true));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Exists) << session_->LastError().message;
    std::string text;
    ASSERT_TRUE(GetFile(In("dst/keep.txt"), text));
    EXPECT_EQ(text, "keep me");
    ASSERT_TRUE(GetFile(In("src"), text));
    EXPECT_EQ(text, "payload");
    EXPECT_EQ(Names(dir_), (std::vector<std::string>{"dst", "src"}));
}

TEST_F(OpsTest, OverwriteWithMissingSourceKeepsTargetAndLeavesNoAsideFile)
{
    ASSERT_TRUE(PutFile(In("dst"), "precious"));
    EXPECT_FALSE(session_->Rename(In("missing"), In("dst"), true));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotFound) << session_->LastError().message;
    std::string text;
    ASSERT_TRUE(GetFile(In("dst"), text));
    EXPECT_EQ(text, "precious");
    EXPECT_EQ(Names(dir_), std::vector<std::string>{"dst"});
}

// The second rename fails after the target was moved aside (a directory cannot move into itself): the target must
// be put back and no aside file may stay.
TEST_F(OpsTest, OverwriteFailingAfterMoveAsideRestoresTheTarget)
{
    ASSERT_TRUE(session_->Mkdir(In("d")));
    ASSERT_TRUE(session_->Mkdir(In("d/sub")));
    EXPECT_FALSE(session_->Rename(In("d"), In("d/sub"), true));
    EXPECT_TRUE(session_->LastError().IsError());
    EXPECT_EQ(session_->LastError().message.find("left as"), std::string::npos) << session_->LastError().message;
    CSftpEntry st;
    ASSERT_TRUE(session_->Stat(In("d/sub"), st)) << session_->LastError().message;
    EXPECT_TRUE(st.isDir);
    EXPECT_EQ(Names(In("d")), std::vector<std::string>{"sub"});
    EXPECT_TRUE(session_->IsConnected()); // a refused operation is not a broken connection
}

TEST_F(OpsTest, CaseOnlyRenameIsJustARename)
{
    ASSERT_TRUE(PutFile(In("report.txt"), "data"));
    ASSERT_TRUE(session_->Rename(In("report.txt"), In("REPORT.TXT"), true)) << session_->LastError().message;
    EXPECT_EQ(Names(dir_), std::vector<std::string>{"REPORT.TXT"});
    std::string text;
    ASSERT_TRUE(GetFile(In("REPORT.TXT"), text));
    EXPECT_EQ(text, "data");
    // the same path twice is a no-op
    EXPECT_TRUE(session_->Rename(In("REPORT.TXT"), In("REPORT.TXT"), true));
    EXPECT_EQ(Names(dir_), std::vector<std::string>{"REPORT.TXT"});
}

TEST_F(OpsTest, OverwriteWithoutExistingTargetIsAPlainRename)
{
    ASSERT_TRUE(PutFile(In("a"), "x"));
    ASSERT_TRUE(session_->Rename(In("a"), In("b"), true)) << session_->LastError().message;
    EXPECT_EQ(Names(dir_), std::vector<std::string>{"b"});
}

// ---- Information: statvfs, banner, negotiated methods --------------------------------------------------------------

TEST_F(OpsTest, StatvfsOfTheHomeDirectory)
{
    CSftpVfs vfs;
    ASSERT_TRUE(session_->Statvfs("/home/test", vfs)) << session_->LastError().message;
    EXPECT_GT(vfs.bytesTotal, 0u);
    EXPECT_GT(vfs.bytesAvailableToUser, 0u);
    EXPECT_GE(vfs.bytesTotal, vfs.bytesFree);
    EXPECT_GE(vfs.bytesFree, vfs.bytesAvailableToUser);
    EXPECT_GT(vfs.files, 0u);
}

TEST_F(OpsTest, StatvfsOfAMissingPathFailsWithoutBreakingTheSession)
{
    CSftpVfs vfs;
    EXPECT_FALSE(session_->Statvfs("/no/such/path", vfs));
    EXPECT_NE(session_->LastError().kind, SshErrorKind::None);
    EXPECT_TRUE(session_->IsConnected());
    EXPECT_TRUE(session_->Statvfs(dir_, vfs)); // relative to nothing: dir_ is a login-relative path
}

TEST_F(OpsTest, ServerInfoAndNegotiatedMethods)
{
    const CSshServerInfo info = session_->ServerInfo();
    EXPECT_EQ(info.banner.substr(0, 4), "SSH-");
    EXPECT_EQ(info.sftpVersionSupported, 3);

    const CSshMethods m = session_->NegotiatedMethods();
    EXPECT_FALSE(m.kex.empty());
    EXPECT_FALSE(m.hostKey.empty());
    EXPECT_FALSE(m.cipherClientToServer.empty());
    EXPECT_FALSE(m.cipherServerToClient.empty());
    EXPECT_FALSE(m.compressionClientToServer.empty()); // "none" when compression is off
    EXPECT_FALSE(session_->HostKeyType().empty());
    EXPECT_EQ(session_->HostKeyFingerprint().substr(0, 7), "SHA256:");
}

TEST_F(OpsTest, StatReportsTheAccessTime)
{
    ASSERT_TRUE(PutFile(In("a"), "x"));
    CSftpEntry e;
    ASSERT_TRUE(session_->LStat(In("a"), e));
    EXPECT_GT(e.atime, 0);
    EXPECT_GT(e.mtime, 0);
}

TEST(SshInfo, NotConnectedGivesEmptyInfo)
{
    CNoCallbacks cb;
    CSshSession session(&cb, nullptr);
    EXPECT_TRUE(session.ServerInfo().banner.empty());
    EXPECT_TRUE(session.NegotiatedMethods().kex.empty());
    CSftpVfs vfs;
    EXPECT_FALSE(session.Statvfs("/", vfs));
}

// ---- Entry name validation ---------------------------------------------------------------------------------------

TEST(RemoteEntryName, Validator)
{
    EXPECT_TRUE(IsValidRemoteEntryName("file.txt"));
    EXPECT_TRUE(IsValidRemoteEntryName("caf\xE9.txt")); // not UTF-8: still a valid raw name
    EXPECT_TRUE(IsValidRemoteEntryName("..."));
    EXPECT_TRUE(IsValidRemoteEntryName(".hidden"));
    EXPECT_TRUE(IsValidRemoteEntryName("a b\\c")); // a backslash is a local-path matter, not a remote one
    EXPECT_FALSE(IsValidRemoteEntryName(""));
    EXPECT_FALSE(IsValidRemoteEntryName("."));
    EXPECT_FALSE(IsValidRemoteEntryName(".."));
    EXPECT_FALSE(IsValidRemoteEntryName("a/b"));
    EXPECT_FALSE(IsValidRemoteEntryName("/etc"));
    EXPECT_FALSE(IsValidRemoteEntryName("../x"));
    EXPECT_FALSE(IsValidRemoteEntryName(std::string("a\0b", 3)));
    EXPECT_FALSE(IsValidRemoteEntryName(std::string(1, '\0')));
}

// ---- Error ids (SshMsg) of failed calls; the English text stays as it was --------------------------------------------

TEST_F(OpsTest, ListMissingDirectoryIsSftpListWithPathAndStatus)
{
    std::vector<CSftpEntry> list;
    const std::string path = In("no-such-dir");
    EXPECT_FALSE(session_->ListDir(path, list));
    const CSshError& error = session_->LastError();
    EXPECT_EQ(error.msgId, SshMsg::SftpList);
    ASSERT_EQ(error.args.size(), 1u); // a server status: the reason comes from sftpStatus
    EXPECT_EQ(error.args[0], path);
    EXPECT_EQ(error.sftpStatus, static_cast<unsigned long>(LIBSSH2_FX_NO_SUCH_FILE));
    EXPECT_EQ(error.message.find("Cannot list '" + path + "': no such file"), 0u) << error.message;
}

TEST_F(OpsTest, StatDeleteRenameMkdirErrorsCarryTheirActionIds)
{
    const std::string missing = In("missing");
    CSftpEntry entry;
    EXPECT_FALSE(session_->Stat(missing, entry));
    EXPECT_EQ(session_->LastError().msgId, SshMsg::SftpReadInfo);
    ASSERT_EQ(session_->LastError().args.size(), 1u);
    EXPECT_EQ(session_->LastError().args[0], missing);

    EXPECT_FALSE(session_->Remove(missing));
    EXPECT_EQ(session_->LastError().msgId, SshMsg::SftpDelete);
    EXPECT_EQ(session_->LastError().args[0], missing);
    EXPECT_EQ(session_->LastError().sftpStatus, static_cast<unsigned long>(LIBSSH2_FX_NO_SUCH_FILE));

    EXPECT_FALSE(session_->Rename(missing, In("other"), false));
    EXPECT_EQ(session_->LastError().msgId, SshMsg::SftpRename);
    EXPECT_EQ(session_->LastError().args[0], missing); // the source path

    EXPECT_FALSE(session_->Mkdir(In("no-parent/child")));
    EXPECT_EQ(session_->LastError().msgId, SshMsg::SftpCreate);
    EXPECT_EQ(session_->LastError().args[0], In("no-parent/child"));

    EXPECT_FALSE(session_->Chmod(missing, 0644));
    EXPECT_EQ(session_->LastError().msgId, SshMsg::SftpChangeAttrs);
}

TEST_F(OpsTest, MkdirExistingIsAlreadyExistsWithPath)
{
    EXPECT_FALSE(session_->Mkdir(dir_));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Exists);
    EXPECT_EQ(session_->LastError().msgId, SshMsg::AlreadyExists);
    ASSERT_EQ(session_->LastError().args.size(), 1u);
    EXPECT_EQ(session_->LastError().args[0], dir_);
    EXPECT_EQ(session_->LastError().message, "'" + dir_ + "' already exists");
}

TEST_F(OpsTest, SymlinkOntoExistingNameIsAlreadyExistsWithPath)
{
    ASSERT_TRUE(session_->Mkdir(dir_ + "/real"));
    ASSERT_TRUE(session_->Symlink("real", dir_ + "/link")) << session_->LastError().message;
    EXPECT_FALSE(session_->Symlink("real", dir_ + "/link"));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::Exists) << session_->LastError().message;
    EXPECT_EQ(session_->LastError().msgId, SshMsg::AlreadyExists);
    ASSERT_EQ(session_->LastError().args.size(), 1u);
    EXPECT_EQ(session_->LastError().args[0], dir_ + "/link");
    ASSERT_TRUE(session_->Remove(dir_ + "/link"));
}

TEST_F(OpsTest, RmdirNonEmptyKeepsTheDeleteIdAndTheNotEmptyKind)
{
    ASSERT_TRUE(session_->Mkdir(In("full")));
    ASSERT_TRUE(session_->Mkdir(In("full/inner")));
    EXPECT_FALSE(session_->Rmdir(In("full")));
    EXPECT_EQ(session_->LastError().kind, SshErrorKind::NotEmpty); // fs/ derives the reason "directory not empty"
    EXPECT_EQ(session_->LastError().msgId, SshMsg::SftpDelete);
    EXPECT_EQ(session_->LastError().args[0], In("full"));
}

TEST_F(OpsTest, UploadWithAnInvalidLocalHandleIsLocalGetSizeWithTheWindowsError)
{
    EXPECT_FALSE(session_->Upload(INVALID_HANDLE_VALUE, In("x.bin"), 0, false, nullptr));
    const CSshError& error = session_->LastError();
    EXPECT_EQ(error.msgId, SshMsg::LocalGetSize);
    ASSERT_EQ(error.args.size(), 1u);
    EXPECT_EQ(error.args[0], std::to_string(ERROR_INVALID_HANDLE));
    EXPECT_NE(error.message.find("(Windows error " + error.args[0] + ")"), std::string::npos); // the disk-full check reads this
}

TEST_F(OpsTest, DownloadingAMissingFileIsSftpReadFile)
{
    HANDLE local = CreateFileW(L"nul", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(local, INVALID_HANDLE_VALUE);
    EXPECT_FALSE(session_->Download(In("missing.bin"), local, 0, nullptr, nullptr));
    CloseHandle(local);
    EXPECT_EQ(session_->LastError().msgId, SshMsg::SftpReadFile);
    EXPECT_EQ(session_->LastError().args[0], In("missing.bin"));
}
