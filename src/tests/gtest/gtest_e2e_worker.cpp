// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// End-to-end integration tests for the headless worker pipeline
//
// Exercises the full conceptual pipeline:
//   CSelectionSnapshot → operation list → execute → verify filesystem results
//
// Each test builds a CSelectionSnapshot describing the operation, then uses
// headless helper functions (mirroring the real worker) to execute operations
// and verify the filesystem state.
//
// Test groups:
//   p11c — Delete operations
//   p11d — Copy operations
//   p11e — Move operations
//   p11f — Unicode + long path operations
//
//****************************************************************************

#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>

// Minimal CProgressData stub
struct CProgressData
{
    const char* Operation;
    const char* Source;
    const char* Preposition;
    const char* Target;
};

#include "TestWorkerObserver.h"
#include "common/CSelectionSnapshot.h"

namespace fs = std::filesystem;

// ============================================================================
// Utility: narrow path for observer (lossy but fine for test logging)
// ============================================================================

static std::string NarrowPath(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    int len = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), (int)wide.size(), &result[0], len, NULL, NULL);
    return result;
}

// ============================================================================
// Headless operation helpers (same patterns as gtest_headless_worker/copy)
// ============================================================================

struct OpResult
{
    bool success;
    DWORD lastError;
};

static OpResult HeadlessDeleteFile(IWorkerObserver& observer,
                                   const std::wstring& filePath,
                                   DWORD fileAttrs,
                                   bool& skipAllErrors)
{
    // Clear read-only if needed
    if (fileAttrs & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesW(filePath.c_str(), fileAttrs & ~FILE_ATTRIBUTE_READONLY);

    while (true)
    {
        if (DeleteFileW(filePath.c_str()))
            return {true, 0};

        DWORD err = GetLastError();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return {false, err};

        if (skipAllErrors)
            return {true, 0};

        std::string nameA = NarrowPath(filePath);
        char errBuf[64];
        wsprintfA(errBuf, "Error code %lu", err);
        int ret = observer.AskFileError("Error deleting file", nameA.c_str(), errBuf);
        switch (ret)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            skipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return {true, 0};
        case IDCANCEL:
            return {false, err};
        }
    }
}

static OpResult HeadlessDeleteDir(IWorkerObserver& observer,
                                  const std::wstring& dirPath,
                                  bool& skipAllErrors)
{
    while (true)
    {
        if (RemoveDirectoryW(dirPath.c_str()))
            return {true, 0};

        DWORD err = GetLastError();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return {false, err};

        if (skipAllErrors)
            return {true, 0};

        std::string nameA = NarrowPath(dirPath);
        char errBuf[64];
        wsprintfA(errBuf, "Error code %lu", err);
        int ret = observer.AskFileError("Error removing directory", nameA.c_str(), errBuf);
        switch (ret)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            skipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return {true, 0};
        case IDCANCEL:
            return {false, err};
        }
    }
}

static OpResult HeadlessCopyFile(IWorkerObserver& observer,
                                 const std::wstring& srcPath,
                                 const std::wstring& dstPath,
                                 bool& skipAllErrors)
{
    // Ensure target directory exists
    fs::path dstDir = fs::path(dstPath).parent_path();
    std::error_code ec;
    fs::create_directories(dstDir, ec);

    while (true)
    {
        if (CopyFileW(srcPath.c_str(), dstPath.c_str(), FALSE))
            return {true, 0};

        DWORD err = GetLastError();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return {false, err};

        if (skipAllErrors)
            return {true, 0};

        std::string nameA = NarrowPath(srcPath);
        char errBuf[64];
        wsprintfA(errBuf, "Error code %lu", err);
        int ret = observer.AskFileError("Error copying file", nameA.c_str(), errBuf);
        switch (ret)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            skipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return {true, 0};
        case IDCANCEL:
            return {false, err};
        }
    }
}

static OpResult HeadlessMoveFile(IWorkerObserver& observer,
                                 const std::wstring& srcPath,
                                 const std::wstring& dstPath,
                                 bool& skipAllErrors)
{
    // Ensure target directory exists
    fs::path dstDir = fs::path(dstPath).parent_path();
    std::error_code ec;
    fs::create_directories(dstDir, ec);

    DWORD flags = MOVEFILE_COPY_ALLOWED;

    while (true)
    {
        if (MoveFileExW(srcPath.c_str(), dstPath.c_str(), flags))
            return {true, 0};

        DWORD err = GetLastError();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return {false, err};

        if (skipAllErrors)
            return {true, 0};

        std::string nameA = NarrowPath(srcPath);
        char errBuf[64];
        wsprintfA(errBuf, "Error code %lu", err);
        int ret = observer.AskFileError("Error moving file", nameA.c_str(), errBuf);
        switch (ret)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            skipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return {true, 0};
        case IDCANCEL:
            return {false, err};
        }
    }
}

// ============================================================================
// Pipeline executor — drives operations from a CSelectionSnapshot
// ============================================================================

// Executes operations described by a CSelectionSnapshot through the headless
// helpers, mirroring the RunWorkerDirect dispatch loop.
static bool ExecuteSnapshot(const CSelectionSnapshot& snapshot,
                            CTestWorkerObserver& obs)
{
    bool skipAllErrors = false;
    bool anyError = false;

    obs.SetProgress(0, 0);

    int totalOps = (int)snapshot.Items.size();
    int completed = 0;

    for (size_t i = 0; !obs.IsCancelled() && i < snapshot.Items.size(); i++)
    {
        const CSnapshotItem& item = snapshot.Items[i];

        // Build full source path
        std::wstring srcDir = snapshot.SourcePathW;
        if (!srcDir.empty() && srcDir.back() != L'\\')
            srcDir += L'\\';
        std::wstring srcFull = srcDir + (item.NameW.empty()
                                             ? std::wstring(item.Name.begin(), item.Name.end())
                                             : item.NameW);

        switch (snapshot.Action)
        {
        case EActionType::Delete:
        {
            std::string srcA = NarrowPath(srcFull);
            CProgressData pd = {"Deleting", srcA.c_str(), "", ""};
            obs.SetOperationInfo(&pd);
            obs.SetProgress(0, totalOps > 0 ? (int)((i * 1000) / totalOps) : 0);

            OpResult result;
            if (item.IsDir)
            {
                // Delete directory contents first (recursive)
                std::error_code ec;
                for (auto& entry : fs::directory_iterator(srcFull, ec))
                {
                    if (entry.is_directory())
                    {
                        // Recursively delete subdirectory contents
                        for (auto& sub : fs::recursive_directory_iterator(
                                 entry.path(), fs::directory_options::none, ec))
                        {
                            if (!sub.is_directory())
                            {
                                DWORD attrs = GetFileAttributesW(sub.path().c_str());
                                result = HeadlessDeleteFile(obs, sub.path().wstring(), attrs, skipAllErrors);
                                if (!result.success)
                                {
                                    anyError = true;
                                    break;
                                }
                            }
                        }
                        if (anyError)
                            break;

                        // Remove subdirectories bottom-up
                        std::vector<fs::path> subDirs;
                        for (auto& sub : fs::recursive_directory_iterator(
                                 entry.path(), fs::directory_options::none, ec))
                        {
                            if (sub.is_directory())
                                subDirs.push_back(sub.path());
                        }
                        std::sort(subDirs.begin(), subDirs.end(), std::greater<>());
                        for (auto& d : subDirs)
                        {
                            result = HeadlessDeleteDir(obs, d.wstring(), skipAllErrors);
                            if (!result.success)
                            {
                                anyError = true;
                                break;
                            }
                        }
                        if (anyError)
                            break;

                        result = HeadlessDeleteDir(obs, entry.path().wstring(), skipAllErrors);
                        if (!result.success)
                        {
                            anyError = true;
                            break;
                        }
                    }
                    else
                    {
                        DWORD attrs = GetFileAttributesW(entry.path().c_str());
                        result = HeadlessDeleteFile(obs, entry.path().wstring(), attrs, skipAllErrors);
                        if (!result.success)
                        {
                            anyError = true;
                            break;
                        }
                    }
                }
                if (!anyError)
                    result = HeadlessDeleteDir(obs, srcFull, skipAllErrors);
            }
            else
            {
                DWORD attrs = GetFileAttributesW(srcFull.c_str());
                result = HeadlessDeleteFile(obs, srcFull, attrs, skipAllErrors);
            }

            if (!result.success)
                anyError = true;
            break;
        }

        case EActionType::Copy:
        case EActionType::Move:
        {
            std::wstring tgtDir = snapshot.TargetPathW;
            if (!tgtDir.empty() && tgtDir.back() != L'\\')
                tgtDir += L'\\';
            std::wstring tgtFull = tgtDir + (item.NameW.empty()
                                                 ? std::wstring(item.Name.begin(), item.Name.end())
                                                 : item.NameW);

            std::string srcA = NarrowPath(srcFull);
            std::string tgtA = NarrowPath(tgtFull);
            const char* opStr = (snapshot.Action == EActionType::Copy) ? "Copying" : "Moving";
            CProgressData pd = {opStr, srcA.c_str(), "to", tgtA.c_str()};
            obs.SetOperationInfo(&pd);
            obs.SetProgress(0, totalOps > 0 ? (int)((i * 1000) / totalOps) : 0);

            if (item.IsDir)
            {
                // Create target directory
                std::error_code ec;
                fs::create_directories(tgtFull, ec);

                // Copy/move directory contents recursively
                for (auto& entry : fs::recursive_directory_iterator(srcFull, ec))
                {
                    fs::path relPath = fs::relative(entry.path(), srcFull);
                    fs::path entryTarget = fs::path(tgtFull) / relPath;

                    if (entry.is_directory())
                    {
                        fs::create_directories(entryTarget, ec);
                    }
                    else
                    {
                        OpResult result;
                        if (snapshot.Action == EActionType::Copy)
                            result = HeadlessCopyFile(obs, entry.path().wstring(), entryTarget.wstring(), skipAllErrors);
                        else
                            result = HeadlessMoveFile(obs, entry.path().wstring(), entryTarget.wstring(), skipAllErrors);
                        if (!result.success)
                        {
                            anyError = true;
                            break;
                        }
                    }
                }

                // For Move, remove source directory after moving contents
                if (!anyError && snapshot.Action == EActionType::Move)
                {
                    // Remove subdirectories bottom-up
                    std::vector<fs::path> subDirs;
                    for (auto& entry : fs::recursive_directory_iterator(srcFull, ec))
                    {
                        if (entry.is_directory())
                            subDirs.push_back(entry.path());
                    }
                    std::sort(subDirs.begin(), subDirs.end(), std::greater<>());
                    for (auto& d : subDirs)
                    {
                        auto r = HeadlessDeleteDir(obs, d.wstring(), skipAllErrors);
                        if (!r.success)
                        {
                            anyError = true;
                            break;
                        }
                    }
                    if (!anyError)
                    {
                        auto r = HeadlessDeleteDir(obs, srcFull, skipAllErrors);
                        if (!r.success)
                            anyError = true;
                    }
                }
            }
            else
            {
                OpResult result;
                if (snapshot.Action == EActionType::Copy)
                    result = HeadlessCopyFile(obs, srcFull, tgtFull, skipAllErrors);
                else
                    result = HeadlessMoveFile(obs, srcFull, tgtFull, skipAllErrors);
                if (!result.success)
                    anyError = true;
            }
            break;
        }

        default:
            anyError = true;
            break;
        }

        if (anyError)
            break;
        completed++;
    }

    obs.SetProgress(0, 1000);
    obs.SetError(anyError);
    obs.NotifyDone();
    return !anyError;
}

// ============================================================================
// Test fixture
// ============================================================================

class E2EWorkerTest : public ::testing::Test
{
protected:
    fs::path m_srcDir;
    fs::path m_dstDir;

    void SetUp() override
    {
        wchar_t tmpPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpPath);
        m_srcDir = fs::path(tmpPath) / L"sal_e2e_src";
        m_dstDir = fs::path(tmpPath) / L"sal_e2e_dst";

        std::error_code ec;
        fs::remove_all(m_srcDir, ec);
        fs::remove_all(m_dstDir, ec);
        fs::create_directories(m_srcDir);
        fs::create_directories(m_dstDir);
    }

    void TearDown() override
    {
        // Use \\?\ prefix for long path cleanup
        auto cleanDir = [](const fs::path& dir)
        {
            std::wstring prefix = L"\\\\?\\" + dir.wstring();
            // Use shell command for recursive deletion to handle long paths
            std::wstring cmd = L"cmd /c rd /s /q \"" + prefix + L"\"";
            _wsystem(cmd.c_str());
            // Fallback with std::filesystem
            std::error_code ec;
            fs::remove_all(dir, ec);
        };
        cleanDir(m_srcDir);
        cleanDir(m_dstDir);
    }

    // Create a file in the source directory with given content
    fs::path CreateSourceFile(const std::wstring& name, const std::string& content = "test data")
    {
        fs::path filePath = m_srcDir / name;
        fs::create_directories(filePath.parent_path());
        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        EXPECT_NE(hFile, INVALID_HANDLE_VALUE) << "Failed to create: " << filePath.string();
        if (hFile != INVALID_HANDLE_VALUE)
        {
            DWORD written;
            WriteFile(hFile, content.data(), (DWORD)content.size(), &written, NULL);
            CloseHandle(hFile);
        }
        return filePath;
    }

    // Create a subdirectory in the source directory
    fs::path CreateSourceDir(const std::wstring& name)
    {
        fs::path dirPath = m_srcDir / name;
        fs::create_directories(dirPath);
        return dirPath;
    }

    // Read file content
    std::string ReadFileContent(const fs::path& path)
    {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            return {};
        char buf[8192];
        DWORD bytesRead;
        ReadFile(hFile, buf, sizeof(buf), &bytesRead, NULL);
        CloseHandle(hFile);
        return std::string(buf, bytesRead);
    }

    // Build a CSelectionSnapshot for delete
    CSelectionSnapshot MakeDeleteSnapshot(const std::vector<std::pair<std::wstring, bool>>& items)
    {
        CSelectionSnapshot snap;
        snap.Action = EActionType::Delete;
        snap.SourcePath = NarrowPath(m_srcDir.wstring());
        snap.SourcePathW = m_srcDir.wstring();

        for (auto& [name, isDir] : items)
        {
            CSnapshotItem si;
            si.Name = NarrowPath(name);
            si.NameW = name;
            si.IsDir = isDir;
            si.Size = 0;
            fs::path fullPath = m_srcDir / name;
            si.Attr = GetFileAttributesW(fullPath.c_str());
            if (si.Attr == INVALID_FILE_ATTRIBUTES)
                si.Attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            memset(&si.LastWrite, 0, sizeof(si.LastWrite));
            snap.Items.push_back(si);
        }
        return snap;
    }

    // Build a CSelectionSnapshot for copy
    CSelectionSnapshot MakeCopySnapshot(const std::vector<std::pair<std::wstring, bool>>& items)
    {
        CSelectionSnapshot snap;
        snap.Action = EActionType::Copy;
        snap.SourcePath = NarrowPath(m_srcDir.wstring());
        snap.SourcePathW = m_srcDir.wstring();
        snap.TargetPath = NarrowPath(m_dstDir.wstring());
        snap.TargetPathW = m_dstDir.wstring();
        snap.Mask = "*.*";

        for (auto& [name, isDir] : items)
        {
            CSnapshotItem si;
            si.Name = NarrowPath(name);
            si.NameW = name;
            si.IsDir = isDir;
            fs::path fullPath = m_srcDir / name;
            if (!isDir)
            {
                std::error_code ec;
                si.Size = fs::file_size(fullPath, ec);
            }
            else
            {
                si.Size = 0;
            }
            si.Attr = GetFileAttributesW(fullPath.c_str());
            if (si.Attr == INVALID_FILE_ATTRIBUTES)
                si.Attr = isDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            memset(&si.LastWrite, 0, sizeof(si.LastWrite));
            snap.Items.push_back(si);
        }
        return snap;
    }

    // Build a CSelectionSnapshot for move
    CSelectionSnapshot MakeMoveSnapshot(const std::vector<std::pair<std::wstring, bool>>& items)
    {
        auto snap = MakeCopySnapshot(items);
        snap.Action = EActionType::Move;
        return snap;
    }
};

// ============================================================================
// p11c — Delete tests
// ============================================================================

TEST_F(E2EWorkerTest, E2EDelete_SingleFile)
{
    auto filePath = CreateSourceFile(L"single.txt", "hello");
    ASSERT_TRUE(fs::exists(filePath));

    auto snap = MakeDeleteSnapshot({{L"single.txt", false}});
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(fs::exists(filePath));
    EXPECT_TRUE(obs.WaitForCompletion(0));
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kNotifyDone), 1);
}

TEST_F(E2EWorkerTest, E2EDelete_MultipleFiles)
{
    auto f1 = CreateSourceFile(L"file_a.txt", "aaa");
    auto f2 = CreateSourceFile(L"file_b.txt", "bbb");
    auto f3 = CreateSourceFile(L"file_c.txt", "ccc");
    ASSERT_TRUE(fs::exists(f1));
    ASSERT_TRUE(fs::exists(f2));
    ASSERT_TRUE(fs::exists(f3));

    auto snap = MakeDeleteSnapshot({
        {L"file_a.txt", false},
        {L"file_b.txt", false},
        {L"file_c.txt", false},
    });
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(fs::exists(f1));
    EXPECT_FALSE(fs::exists(f2));
    EXPECT_FALSE(fs::exists(f3));
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kSetOperationInfo), 3);
}

TEST_F(E2EWorkerTest, E2EDelete_Directory)
{
    auto dir = CreateSourceDir(L"mydir");
    CreateSourceFile(L"mydir\\child1.txt", "c1");
    CreateSourceFile(L"mydir\\child2.txt", "c2");
    CreateSourceFile(L"mydir\\sub\\deep.txt", "deep");
    ASSERT_TRUE(fs::exists(dir));
    ASSERT_TRUE(fs::exists(m_srcDir / L"mydir\\child1.txt"));

    auto snap = MakeDeleteSnapshot({{L"mydir", true}});
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(fs::exists(dir));
    EXPECT_FALSE(fs::exists(m_srcDir / L"mydir\\child1.txt"));
    EXPECT_FALSE(fs::exists(m_srcDir / L"mydir\\sub\\deep.txt"));
}

// ============================================================================
// p11d — Copy tests
// ============================================================================

TEST_F(E2EWorkerTest, E2ECopy_SingleFile)
{
    auto src = CreateSourceFile(L"copy_me.txt", "copy content");
    ASSERT_TRUE(fs::exists(src));

    auto snap = MakeCopySnapshot({{L"copy_me.txt", false}});
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(fs::exists(src));                         // source still exists
    EXPECT_TRUE(fs::exists(m_dstDir / L"copy_me.txt"));  // copy created
    EXPECT_EQ(ReadFileContent(m_dstDir / L"copy_me.txt"), "copy content");
}

TEST_F(E2EWorkerTest, E2ECopy_MultipleFiles)
{
    auto f1 = CreateSourceFile(L"multi_a.txt", "aaa");
    auto f2 = CreateSourceFile(L"multi_b.txt", "bbb");
    auto f3 = CreateSourceFile(L"multi_c.txt", "ccc");

    auto snap = MakeCopySnapshot({
        {L"multi_a.txt", false},
        {L"multi_b.txt", false},
        {L"multi_c.txt", false},
    });
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    // All 3 sources still exist
    EXPECT_TRUE(fs::exists(f1));
    EXPECT_TRUE(fs::exists(f2));
    EXPECT_TRUE(fs::exists(f3));
    // All 3 copies exist
    EXPECT_TRUE(fs::exists(m_dstDir / L"multi_a.txt"));
    EXPECT_TRUE(fs::exists(m_dstDir / L"multi_b.txt"));
    EXPECT_TRUE(fs::exists(m_dstDir / L"multi_c.txt"));
    // Verify content
    EXPECT_EQ(ReadFileContent(m_dstDir / L"multi_a.txt"), "aaa");
    EXPECT_EQ(ReadFileContent(m_dstDir / L"multi_b.txt"), "bbb");
    EXPECT_EQ(ReadFileContent(m_dstDir / L"multi_c.txt"), "ccc");
}

TEST_F(E2EWorkerTest, E2ECopy_PreservesAttributes)
{
    auto src = CreateSourceFile(L"readonly.txt", "ro data");
    SetFileAttributesW(src.c_str(), FILE_ATTRIBUTE_READONLY);

    DWORD srcAttrs = GetFileAttributesW(src.c_str());
    ASSERT_TRUE(srcAttrs & FILE_ATTRIBUTE_READONLY);

    auto snap = MakeCopySnapshot({{L"readonly.txt", false}});
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    fs::path dst = m_dstDir / L"readonly.txt";
    EXPECT_TRUE(fs::exists(dst));
    EXPECT_EQ(ReadFileContent(dst), "ro data");

    // CopyFileW preserves attributes by default
    DWORD dstAttrs = GetFileAttributesW(dst.c_str());
    EXPECT_NE(dstAttrs, (DWORD)INVALID_FILE_ATTRIBUTES);
    EXPECT_TRUE(dstAttrs & FILE_ATTRIBUTE_READONLY) << "READONLY attribute not preserved";
}

// ============================================================================
// p11e — Move tests
// ============================================================================

TEST_F(E2EWorkerTest, E2EMove_SingleFile)
{
    auto src = CreateSourceFile(L"move_me.txt", "move content");
    ASSERT_TRUE(fs::exists(src));

    auto snap = MakeMoveSnapshot({{L"move_me.txt", false}});
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(fs::exists(src));                         // source removed
    EXPECT_TRUE(fs::exists(m_dstDir / L"move_me.txt"));   // moved to dest
    EXPECT_EQ(ReadFileContent(m_dstDir / L"move_me.txt"), "move content");
}

TEST_F(E2EWorkerTest, E2EMove_MultipleFiles)
{
    auto f1 = CreateSourceFile(L"mv_a.txt", "aaa");
    auto f2 = CreateSourceFile(L"mv_b.txt", "bbb");
    auto f3 = CreateSourceFile(L"mv_c.txt", "ccc");

    auto snap = MakeMoveSnapshot({
        {L"mv_a.txt", false},
        {L"mv_b.txt", false},
        {L"mv_c.txt", false},
    });
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    // Sources gone
    EXPECT_FALSE(fs::exists(f1));
    EXPECT_FALSE(fs::exists(f2));
    EXPECT_FALSE(fs::exists(f3));
    // Targets exist
    EXPECT_TRUE(fs::exists(m_dstDir / L"mv_a.txt"));
    EXPECT_TRUE(fs::exists(m_dstDir / L"mv_b.txt"));
    EXPECT_TRUE(fs::exists(m_dstDir / L"mv_c.txt"));
    // Verify content
    EXPECT_EQ(ReadFileContent(m_dstDir / L"mv_a.txt"), "aaa");
    EXPECT_EQ(ReadFileContent(m_dstDir / L"mv_b.txt"), "bbb");
    EXPECT_EQ(ReadFileContent(m_dstDir / L"mv_c.txt"), "ccc");
}

// ============================================================================
// p11f — Unicode + Long path tests
// ============================================================================

TEST_F(E2EWorkerTest, E2EDelete_UnicodeFilenames)
{
    // Japanese: 日本語.txt
    auto f1 = CreateSourceFile(L"\x65E5\x672C\x8A9E.txt", "japanese");
    // French accented: données.txt
    auto f2 = CreateSourceFile(L"donn\x00E9es.txt", "french");
    ASSERT_TRUE(fs::exists(f1));
    ASSERT_TRUE(fs::exists(f2));

    auto snap = MakeDeleteSnapshot({
        {L"\x65E5\x672C\x8A9E.txt", false},
        {L"donn\x00E9es.txt", false},
    });
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    EXPECT_FALSE(fs::exists(f1));
    EXPECT_FALSE(fs::exists(f2));
}

// Helper: create a directory tree using \\?\ prefix for long path support
static bool CreateLongPathDir(const std::wstring& path)
{
    // Use \\?\ prefix if not already present
    std::wstring fullPath = path;
    if (fullPath.size() < 4 || fullPath.substr(0, 4) != L"\\\\?\\")
        fullPath = L"\\\\?\\" + fullPath;

    // Start scanning after the \\?\ prefix and drive letter (e.g., \\?\C:\)
    size_t startPos = 4;
    // Skip drive root like "C:\"
    size_t driveEnd = fullPath.find(L'\\', startPos);
    if (driveEnd != std::wstring::npos)
        startPos = driveEnd + 1;

    for (size_t pos = startPos; pos <= fullPath.size(); pos++)
    {
        if (pos == fullPath.size() || fullPath[pos] == L'\\')
        {
            std::wstring sub = fullPath.substr(0, pos);
            if (!CreateDirectoryW(sub.c_str(), NULL))
            {
                DWORD err = GetLastError();
                if (err != ERROR_ALREADY_EXISTS)
                    return false;
            }
        }
    }
    return true;
}

// Helper: create a file using \\?\ prefix for long path support
static HANDLE CreateLongPathFile(const std::wstring& path)
{
    std::wstring fullPath = path;
    if (fullPath.substr(0, 4) != L"\\\\?\\")
        fullPath = L"\\\\?\\" + fullPath;
    return CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

// Helper: check if a long-path file exists using \\?\ prefix
static bool LongPathExists(const std::wstring& path)
{
    std::wstring fullPath = path;
    if (fullPath.substr(0, 4) != L"\\\\?\\")
        fullPath = L"\\\\?\\" + fullPath;
    DWORD attrs = GetFileAttributesW(fullPath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
}

// Helper: read content from a long-path file
static std::string ReadLongPathContent(const std::wstring& path)
{
    std::wstring fullPath = path;
    if (fullPath.substr(0, 4) != L"\\\\?\\")
        fullPath = L"\\\\?\\" + fullPath;
    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return {};
    char buf[8192];
    DWORD bytesRead;
    ReadFile(hFile, buf, sizeof(buf), &bytesRead, NULL);
    CloseHandle(hFile);
    return std::string(buf, bytesRead);
}

TEST_F(E2EWorkerTest, E2ECopy_LongPath)
{
    // Build a long path > 260 chars using nested directories
    std::wstring longSubDir;
    for (int i = 0; i < 6; i++)
    {
        if (!longSubDir.empty())
            longSubDir += L"\\";
        longSubDir += std::wstring(45, L'a' + (wchar_t)i);
    }

    // Create the deep directory in source using \\?\ prefix
    std::wstring deepSrcDirStr = m_srcDir.wstring() + L"\\" + longSubDir;
    if (!CreateLongPathDir(deepSrcDirStr))
    {
        GTEST_SKIP() << "Cannot create long path directory";
    }

    // Create a file in the deep directory
    std::wstring fileName = L"longpath_file.txt";
    std::wstring srcFileStr = deepSrcDirStr + L"\\" + fileName;
    HANDLE hFile = CreateLongPathFile(srcFileStr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        GTEST_SKIP() << "Cannot create long-path file";
    }
    const char* content = "long path content";
    DWORD written;
    WriteFile(hFile, content, (DWORD)strlen(content), &written, NULL);
    CloseHandle(hFile);

    ASSERT_TRUE(LongPathExists(srcFileStr));
    // Verify path length exceeds MAX_PATH
    EXPECT_GT(srcFileStr.size(), 260u);

    // Build snapshot — copy the file using relative path
    std::wstring relPath = longSubDir + L"\\" + fileName;
    auto snap = MakeCopySnapshot({{relPath, false}});

    // Update snapshot paths to use \\?\ prefix for long path support
    if (snap.SourcePathW.substr(0, 4) != L"\\\\?\\")
        snap.SourcePathW = L"\\\\?\\" + snap.SourcePathW;
    if (snap.TargetPathW.substr(0, 4) != L"\\\\?\\")
        snap.TargetPathW = L"\\\\?\\" + snap.TargetPathW;

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    // Source still exists
    EXPECT_TRUE(LongPathExists(srcFileStr));
    // Target should exist
    std::wstring dstFileStr = m_dstDir.wstring() + L"\\" + longSubDir + L"\\" + fileName;
    EXPECT_TRUE(LongPathExists(dstFileStr)) << "Long-path copy target not found";
    EXPECT_EQ(ReadLongPathContent(dstFileStr), "long path content");
}

TEST_F(E2EWorkerTest, E2EMove_UnicodeAndLongPath)
{
    // Build a moderately long path with Unicode directory names
    // Using Cyrillic: каталог (katalog)
    std::wstring unicodeDir = L"\x043A\x0430\x0442\x0430\x043B\x043E\x0433";
    // Pad to exceed MAX_PATH
    std::wstring longDir;
    for (int i = 0; i < 5; i++)
    {
        if (!longDir.empty())
            longDir += L"\\";
        longDir += unicodeDir + L"_" + std::to_wstring(i) + L"_" + std::wstring(35, L'x');
    }

    // Create using \\?\ prefix for long path support
    std::wstring deepSrcDirStr = m_srcDir.wstring() + L"\\" + longDir;
    if (!CreateLongPathDir(deepSrcDirStr))
    {
        GTEST_SKIP() << "Cannot create Unicode long path directory";
    }

    // Create a Unicode-named file in the deep directory (Chinese: 文件.txt)
    std::wstring fileName = L"\x6587\x4EF6.txt";
    std::wstring srcFileStr = deepSrcDirStr + L"\\" + fileName;
    HANDLE hFile = CreateLongPathFile(srcFileStr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        GTEST_SKIP() << "Cannot create Unicode long-path file";
    }
    const char* content = "unicode long path data";
    DWORD written;
    WriteFile(hFile, content, (DWORD)strlen(content), &written, NULL);
    CloseHandle(hFile);

    ASSERT_TRUE(LongPathExists(srcFileStr));

    // Build snapshot for moving the file
    std::wstring relPath = longDir + L"\\" + fileName;
    auto snap = MakeMoveSnapshot({{relPath, false}});

    // Use \\?\ prefix for long path support
    if (snap.SourcePathW.substr(0, 4) != L"\\\\?\\")
        snap.SourcePathW = L"\\\\?\\" + snap.SourcePathW;
    if (snap.TargetPathW.substr(0, 4) != L"\\\\?\\")
        snap.TargetPathW = L"\\\\?\\" + snap.TargetPathW;

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    // Source should be gone
    EXPECT_FALSE(LongPathExists(srcFileStr));
    // Target should exist
    std::wstring dstFileStr = m_dstDir.wstring() + L"\\" + longDir + L"\\" + fileName;
    EXPECT_TRUE(LongPathExists(dstFileStr)) << "Unicode long-path move target not found";
    EXPECT_EQ(ReadLongPathContent(dstFileStr), "unicode long path data");
}

// ============================================================================
// p11g — Cancellation tests
// ============================================================================

TEST_F(E2EWorkerTest, E2EDelete_CancelStopsProcessing)
{
    auto f1 = CreateSourceFile(L"file1.txt", "aaa");
    auto f2 = CreateSourceFile(L"file2.txt", "bbb");
    auto f3 = CreateSourceFile(L"file3.txt", "ccc");

    auto snap = MakeDeleteSnapshot({
        {L"file1.txt", false},
        {L"file2.txt", false},
        {L"file3.txt", false},
    });

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    // Cancel after first SetOperationInfo call
    // We modify ExecuteSnapshot behavior by cancelling from within the observer.
    // Since we can't hook mid-loop, cancel before starting and verify nothing happens.
    obs.Cancel();

    bool ok = ExecuteSnapshot(snap, obs);

    // Operation should report no error (cancelled, not failed)
    // All files should still exist since cancel was set before processing
    EXPECT_TRUE(fs::exists(f1));
    EXPECT_TRUE(fs::exists(f2));
    EXPECT_TRUE(fs::exists(f3));
    // No SetOperationInfo calls since loop body is skipped when cancelled
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kSetOperationInfo), 0);
}

// ============================================================================
// p11h — Error handling and skip policy tests
// ============================================================================

TEST_F(E2EWorkerTest, E2EDelete_SkipLockedFile)
{
    auto f1 = CreateSourceFile(L"normal.txt", "aaa");
    auto f2 = CreateSourceFile(L"locked.txt", "bbb");
    auto f3 = CreateSourceFile(L"also_normal.txt", "ccc");

    // Lock f2 by opening it exclusively
    HANDLE hLock = CreateFileW(f2.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, // no sharing
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(hLock, INVALID_HANDLE_VALUE) << "Failed to lock file";

    auto snap = MakeDeleteSnapshot({
        {L"normal.txt", false},
        {L"locked.txt", false},
        {L"also_normal.txt", false},
    });
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    CloseHandle(hLock); // release lock

    EXPECT_TRUE(ok); // skip policy means no overall error
    EXPECT_FALSE(fs::exists(f1));        // deleted
    EXPECT_TRUE(fs::exists(f2));         // skipped (was locked)
    EXPECT_FALSE(fs::exists(f3));        // deleted
    EXPECT_GE(obs.CountCallsOfType(TestObserverCall::kAskFileError), 1);
}

TEST_F(E2EWorkerTest, E2EDelete_CancelOnError)
{
    auto f1 = CreateSourceFile(L"first.txt", "aaa");
    auto f2 = CreateSourceFile(L"locked.txt", "bbb");
    auto f3 = CreateSourceFile(L"third.txt", "ccc");

    // Lock f2 by opening it exclusively
    HANDLE hLock = CreateFileW(f2.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(hLock, INVALID_HANDLE_VALUE);

    auto snap = MakeDeleteSnapshot({
        {L"first.txt", false},
        {L"locked.txt", false},
        {L"third.txt", false},
    });
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kCancel); // cancel on first error

    bool ok = ExecuteSnapshot(snap, obs);

    CloseHandle(hLock);

    EXPECT_FALSE(ok);                     // cancelled
    EXPECT_FALSE(fs::exists(f1));         // deleted (before error)
    EXPECT_TRUE(fs::exists(f2));          // locked, triggered cancel
    EXPECT_TRUE(fs::exists(f3));          // not reached due to cancel
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 1);
}

// ============================================================================
// p11i — Copy directory and overwrite tests
// ============================================================================

TEST_F(E2EWorkerTest, E2ECopy_DirectoryRecursive)
{
    auto dir = CreateSourceDir(L"topdir");
    CreateSourceFile(L"topdir\\a.txt", "file_a");
    CreateSourceFile(L"topdir\\b.txt", "file_b");
    CreateSourceDir(L"topdir\\sub1");
    CreateSourceFile(L"topdir\\sub1\\c.txt", "file_c");
    CreateSourceDir(L"topdir\\sub1\\deep");
    CreateSourceFile(L"topdir\\sub1\\deep\\d.txt", "file_d");

    auto snap = MakeCopySnapshot({{L"topdir", true}});
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    // Source still intact
    EXPECT_TRUE(fs::exists(m_srcDir / L"topdir\\a.txt"));
    EXPECT_TRUE(fs::exists(m_srcDir / L"topdir\\sub1\\deep\\d.txt"));
    // Copies created
    EXPECT_TRUE(fs::exists(m_dstDir / L"topdir\\a.txt"));
    EXPECT_TRUE(fs::exists(m_dstDir / L"topdir\\b.txt"));
    EXPECT_TRUE(fs::exists(m_dstDir / L"topdir\\sub1\\c.txt"));
    EXPECT_TRUE(fs::exists(m_dstDir / L"topdir\\sub1\\deep\\d.txt"));
    // Content matches
    EXPECT_EQ(ReadFileContent(m_dstDir / L"topdir\\a.txt"), "file_a");
    EXPECT_EQ(ReadFileContent(m_dstDir / L"topdir\\sub1\\deep\\d.txt"), "file_d");
}

TEST_F(E2EWorkerTest, E2EMove_DirectoryRecursive)
{
    CreateSourceDir(L"movedir");
    CreateSourceFile(L"movedir\\x.txt", "data_x");
    CreateSourceDir(L"movedir\\inner");
    CreateSourceFile(L"movedir\\inner\\y.txt", "data_y");

    auto snap = MakeMoveSnapshot({{L"movedir", true}});
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    // Source directory should be gone
    EXPECT_FALSE(fs::exists(m_srcDir / L"movedir"));
    // Targets exist
    EXPECT_TRUE(fs::exists(m_dstDir / L"movedir\\x.txt"));
    EXPECT_TRUE(fs::exists(m_dstDir / L"movedir\\inner\\y.txt"));
    EXPECT_EQ(ReadFileContent(m_dstDir / L"movedir\\x.txt"), "data_x");
    EXPECT_EQ(ReadFileContent(m_dstDir / L"movedir\\inner\\y.txt"), "data_y");
}

TEST_F(E2EWorkerTest, E2ECopy_OverwriteExistingFile)
{
    // Create source and pre-existing target with different content
    CreateSourceFile(L"overwrite_me.txt", "new content");

    // Pre-create target
    fs::path dstFile = m_dstDir / L"overwrite_me.txt";
    {
        HANDLE hFile = CreateFileW(dstFile.c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ASSERT_NE(hFile, INVALID_HANDLE_VALUE);
        const char* old = "old content";
        DWORD written;
        WriteFile(hFile, old, (DWORD)strlen(old), &written, NULL);
        CloseHandle(hFile);
    }
    ASSERT_EQ(ReadFileContent(dstFile), "old content");

    auto snap = MakeCopySnapshot({{L"overwrite_me.txt", false}});
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);
    obs.SetOverwritePolicy(TestDialogPolicy::kYes);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    // CopyFileW with failIfExists=FALSE overwrites automatically,
    // so the custom helper doesn't trigger AskOverwrite.
    // The target should have the new content.
    EXPECT_EQ(ReadFileContent(dstFile), "new content");
}

TEST_F(E2EWorkerTest, E2ECopy_SkipOnOverwrite)
{
    // Create source file
    CreateSourceFile(L"keep_old.txt", "new data");

    // Pre-create target with old content
    fs::path dstFile = m_dstDir / L"keep_old.txt";
    {
        HANDLE hFile = CreateFileW(dstFile.c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ASSERT_NE(hFile, INVALID_HANDLE_VALUE);
        const char* old = "old data";
        DWORD written;
        WriteFile(hFile, old, (DWORD)strlen(old), &written, NULL);
        CloseHandle(hFile);
    }

    // Make target read-only to trigger error on overwrite
    SetFileAttributesW(dstFile.c_str(), FILE_ATTRIBUTE_READONLY);

    auto snap = MakeCopySnapshot({{L"keep_old.txt", false}});
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkip);

    bool ok = ExecuteSnapshot(snap, obs);

    // Restore attributes for cleanup
    SetFileAttributesW(dstFile.c_str(), FILE_ATTRIBUTE_NORMAL);

    EXPECT_TRUE(ok);
    // Target should still have old content since copy was skipped
    EXPECT_EQ(ReadFileContent(dstFile), "old data");
    // Error should have been reported
    EXPECT_GE(obs.CountCallsOfType(TestObserverCall::kAskFileError), 1);
}

// ============================================================================
// p11j — Mixed Unicode scripts (CJK, RTL, combining marks)
// ============================================================================

TEST_F(E2EWorkerTest, E2ECopy_MixedScripts)
{
    // Chinese: 复制.txt
    auto f1 = CreateSourceFile(L"\x590D\x5236.txt", "chinese");
    // Arabic: ملف.txt
    auto f2 = CreateSourceFile(L"\x0645\x0644\x0641.txt", "arabic");
    // Korean: 파일.txt
    auto f3 = CreateSourceFile(L"\xD30C\xC77C.txt", "korean");

    auto snap = MakeCopySnapshot({
        {L"\x590D\x5236.txt", false},
        {L"\x0645\x0644\x0641.txt", false},
        {L"\xD30C\xC77C.txt", false},
    });
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);

    bool ok = ExecuteSnapshot(snap, obs);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(fs::exists(m_dstDir / L"\x590D\x5236.txt"));
    EXPECT_TRUE(fs::exists(m_dstDir / L"\x0645\x0644\x0641.txt"));
    EXPECT_TRUE(fs::exists(m_dstDir / L"\xD30C\xC77C.txt"));
    EXPECT_EQ(ReadFileContent(m_dstDir / L"\x590D\x5236.txt"), "chinese");
    EXPECT_EQ(ReadFileContent(m_dstDir / L"\x0645\x0644\x0641.txt"), "arabic");
    EXPECT_EQ(ReadFileContent(m_dstDir / L"\xD30C\xC77C.txt"), "korean");
}
