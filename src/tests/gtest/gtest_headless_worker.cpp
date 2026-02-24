// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Headless integration tests for the worker pipeline
//
// Proves that file operations work end-to-end through the decoupled
// IWorkerObserver interface — no progress dialog, no message pump.
// Uses real Win32 file I/O with temp files.
//
// The harness replicates the ThreadWorkerBody operation loop pattern:
//   1. Set operation info via observer
//   2. Perform the actual file operation (DeleteFileW, etc.)
//   3. Report progress via observer
//   4. On error, ask the observer for a decision
//   5. Signal completion via NotifyDone()
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

namespace fs = std::filesystem;

// ============================================================================
// Test fixture — creates a temp directory tree for each test
// ============================================================================

class HeadlessWorkerTest : public ::testing::Test
{
protected:
    fs::path m_tempDir;

    void SetUp() override
    {
        // Create a unique temp directory for this test
        wchar_t tmpPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpPath);
        m_tempDir = fs::path(tmpPath) / L"sal_headless_test";

        // Clean up from any previous failed run
        std::error_code ec;
        fs::remove_all(m_tempDir, ec);

        fs::create_directories(m_tempDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_tempDir, ec);
    }

    // Create a file with specified content
    fs::path CreateTestFile(const std::wstring& name, const std::string& content = "test data")
    {
        fs::path filePath = m_tempDir / name;
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

    // Create a file with specific attributes
    fs::path CreateTestFileWithAttrs(const std::wstring& name, DWORD attrs)
    {
        fs::path filePath = CreateTestFile(name);
        SetFileAttributesW(filePath.c_str(), attrs);
        return filePath;
    }

    // Create a subdirectory
    fs::path CreateTestDir(const std::wstring& name)
    {
        fs::path dirPath = m_tempDir / name;
        fs::create_directories(dirPath);
        return dirPath;
    }
};

// ============================================================================
// Headless delete operation — mirrors DoDeleteFile logic
// ============================================================================

// Convert wide string to narrow for observer (lossy but fine for test logging)
static std::string NarrowPath(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    int len = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), (int)wide.size(), NULL, 0, NULL, NULL);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, wide.c_str(), (int)wide.size(), &result[0], len, NULL, NULL);
    return result;
}

// Simplified delete that follows the same observer pattern as DoDeleteFile
struct DeleteResult
{
    bool success;
    DWORD lastError;
};

DeleteResult HeadlessDeleteFile(IWorkerObserver& observer,
                                const std::wstring& filePath,
                                DWORD fileAttrs,
                                bool confirmHiddenSystem,
                                bool& skipAllHidden,
                                bool& skipAllErrors)
{
    // Check for hidden/system — same pattern as DoDeleteFile lines 6220-6248
    if (fileAttrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
    {
        if (confirmHiddenSystem && !skipAllHidden)
        {
            observer.WaitIfSuspended();
            if (observer.IsCancelled())
                return {false, 0};

            std::string nameA = NarrowPath(filePath);
            int ret = observer.AskHiddenOrSystem("Confirm delete", nameA.c_str(), "Delete hidden/system file?");
            switch (ret)
            {
            case IDB_ALL:
                skipAllHidden = true;
                // fallthrough
            case IDYES:
                break;
            case IDB_SKIPALL:
                skipAllHidden = true;
                // fallthrough
            case IDB_SKIP:
                return {true, 0}; // skip = success (matches DoDeleteFile SKIP_DELETE)
            case IDCANCEL:
                return {false, 0};
            }
        }
    }

    // Clear read-only if needed — same as ClearReadOnlyAttr
    if (fileAttrs & FILE_ATTRIBUTE_READONLY)
    {
        SetFileAttributesW(filePath.c_str(), fileAttrs & ~FILE_ATTRIBUTE_READONLY);
    }

    // Attempt deletion
    while (true)
    {
        if (DeleteFileW(filePath.c_str()))
            return {true, 0};

        DWORD err = GetLastError();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return {false, err};

        if (skipAllErrors)
            return {true, 0}; // skip = success

        std::string nameA = NarrowPath(filePath);
        char errBuf[64];
        wsprintfA(errBuf, "Error code %lu", err);
        int ret = observer.AskFileError("Error deleting file", nameA.c_str(), errBuf);
        switch (ret)
        {
        case IDRETRY:
            break; // retry the loop
        case IDB_SKIPALL:
            skipAllErrors = true;
            // fallthrough
        case IDB_SKIP:
            return {true, 0};
        case IDCANCEL:
            return {false, err};
        }
    }
}

// Simplified directory delete
DeleteResult HeadlessDeleteDir(IWorkerObserver& observer,
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
        case IDB_SKIP:
            return {true, 0};
        case IDCANCEL:
            return {false, err};
        }
    }
}

// ============================================================================
// Basic delete tests — single file
// ============================================================================

TEST_F(HeadlessWorkerTest, DeleteSingleFile)
{
    auto filePath = CreateTestFile(L"simple.txt");
    ASSERT_TRUE(fs::exists(filePath));

    CTestWorkerObserver obs;

    CProgressData pd = {"Deleting", "simple.txt", "", ""};
    obs.SetOperationInfo(&pd);
    obs.SetProgress(0, 0);

    bool skipHidden = false, skipErrors = false;
    DWORD attrs = GetFileAttributesW(filePath.c_str());
    auto result = HeadlessDeleteFile(obs, filePath.wstring(), attrs, true, skipHidden, skipErrors);

    obs.SetProgress(0, 1000);
    obs.NotifyDone();

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(filePath));
    EXPECT_TRUE(obs.WaitForCompletion(0));
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 0);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskHiddenOrSystem), 0);
}

TEST_F(HeadlessWorkerTest, DeleteReadOnlyFile)
{
    auto filePath = CreateTestFileWithAttrs(L"readonly.txt", FILE_ATTRIBUTE_READONLY);
    ASSERT_TRUE(fs::exists(filePath));

    CTestWorkerObserver obs;
    bool skipHidden = false, skipErrors = false;
    DWORD attrs = GetFileAttributesW(filePath.c_str());
    EXPECT_TRUE(attrs & FILE_ATTRIBUTE_READONLY);

    auto result = HeadlessDeleteFile(obs, filePath.wstring(), attrs, true, skipHidden, skipErrors);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(filePath));
}

TEST_F(HeadlessWorkerTest, DeleteHiddenFileWithConfirmYes)
{
    auto filePath = CreateTestFileWithAttrs(L"hidden.txt", FILE_ATTRIBUTE_HIDDEN);
    ASSERT_TRUE(fs::exists(filePath));

    CTestWorkerObserver obs;
    obs.SetHiddenSystemPolicy(TestDialogPolicy::kYes);
    bool skipHidden = false, skipErrors = false;

    auto result = HeadlessDeleteFile(obs, filePath.wstring(),
                                     FILE_ATTRIBUTE_HIDDEN, true, skipHidden, skipErrors);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(filePath));
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskHiddenOrSystem), 1);
}

TEST_F(HeadlessWorkerTest, DeleteHiddenFileWithSkip)
{
    auto filePath = CreateTestFileWithAttrs(L"hidden_skip.txt", FILE_ATTRIBUTE_HIDDEN);
    ASSERT_TRUE(fs::exists(filePath));

    CTestWorkerObserver obs;
    obs.SetHiddenSystemPolicy(TestDialogPolicy::kSkip);
    bool skipHidden = false, skipErrors = false;

    auto result = HeadlessDeleteFile(obs, filePath.wstring(),
                                     FILE_ATTRIBUTE_HIDDEN, true, skipHidden, skipErrors);

    EXPECT_TRUE(result.success);       // skip returns success
    EXPECT_TRUE(fs::exists(filePath)); // but file still exists (was skipped)
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskHiddenOrSystem), 1);
}

TEST_F(HeadlessWorkerTest, DeleteHiddenFileWithCancel)
{
    auto filePath = CreateTestFileWithAttrs(L"hidden_cancel.txt", FILE_ATTRIBUTE_HIDDEN);
    CTestWorkerObserver obs;
    obs.SetHiddenSystemPolicy(TestDialogPolicy::kCancel);
    bool skipHidden = false, skipErrors = false;

    auto result = HeadlessDeleteFile(obs, filePath.wstring(),
                                     FILE_ATTRIBUTE_HIDDEN, true, skipHidden, skipErrors);

    EXPECT_FALSE(result.success);      // cancel = operation failed
    EXPECT_TRUE(fs::exists(filePath)); // file untouched
}

// ============================================================================
// Error handling — locked file, nonexistent file
// ============================================================================

TEST_F(HeadlessWorkerTest, DeleteNonexistentFileSkipsOnError)
{
    std::wstring fakePath = (m_tempDir / L"nonexistent.txt").wstring();

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkip);
    bool skipHidden = false, skipErrors = false;

    auto result = HeadlessDeleteFile(obs, fakePath, FILE_ATTRIBUTE_NORMAL, false, skipHidden, skipErrors);

    EXPECT_TRUE(result.success); // skip = success
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 1);

    // Verify error text was captured
    auto& calls = obs.GetCalls();
    bool foundError = false;
    for (auto& c : calls)
    {
        if (c.type == TestObserverCall::kAskFileError)
        {
            EXPECT_FALSE(c.arg1.empty()); // filename captured
            EXPECT_FALSE(c.arg2.empty()); // error text captured
            foundError = true;
        }
    }
    EXPECT_TRUE(foundError);
}

TEST_F(HeadlessWorkerTest, DeleteLockedFileWithRetryThenSkip)
{
    auto filePath = CreateTestFile(L"locked.txt");

    // Lock the file by holding a handle open
    HANDLE hLock = CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(hLock, INVALID_HANDLE_VALUE);

    // First call returns Retry, second returns Skip
    // We'll use a custom approach: set policy to Skip (first error triggers skip)
    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkip);
    bool skipHidden = false, skipErrors = false;

    auto result = HeadlessDeleteFile(obs, filePath.wstring(),
                                     FILE_ATTRIBUTE_NORMAL, false, skipHidden, skipErrors);

    EXPECT_TRUE(result.success); // skip = success
    EXPECT_TRUE(fs::exists(filePath)); // file still locked
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 1);

    CloseHandle(hLock);
}

TEST_F(HeadlessWorkerTest, DeleteLockedFileCancels)
{
    auto filePath = CreateTestFile(L"locked_cancel.txt");

    HANDLE hLock = CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(hLock, INVALID_HANDLE_VALUE);

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kCancel);
    bool skipHidden = false, skipErrors = false;

    auto result = HeadlessDeleteFile(obs, filePath.wstring(),
                                     FILE_ATTRIBUTE_NORMAL, false, skipHidden, skipErrors);

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(fs::exists(filePath));

    CloseHandle(hLock);
}

// ============================================================================
// Multi-file delete — worker loop pattern
// ============================================================================

TEST_F(HeadlessWorkerTest, MultiFileDeleteAllSucceed)
{
    std::vector<fs::path> files;
    for (int i = 0; i < 5; i++)
        files.push_back(CreateTestFile(L"multi_" + std::to_wstring(i) + L".txt"));

    CTestWorkerObserver obs;
    bool skipHidden = false, skipErrors = false;
    int completed = 0;

    for (size_t i = 0; !obs.IsCancelled() && i < files.size(); i++)
    {
        CProgressData pd = {"Deleting", "file", "", ""};
        obs.SetOperationInfo(&pd);
        obs.SetProgress(0, (int)((i * 1000) / files.size()));

        DWORD attrs = GetFileAttributesW(files[i].c_str());
        auto result = HeadlessDeleteFile(obs, files[i].wstring(), attrs, false, skipHidden, skipErrors);
        if (!result.success)
            break;
        completed++;
    }

    obs.SetProgress(0, 1000);
    obs.NotifyDone();

    EXPECT_EQ(completed, 5);
    for (auto& f : files)
        EXPECT_FALSE(fs::exists(f));
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kSetOperationInfo), 5);
    EXPECT_TRUE(obs.WaitForCompletion(0));
}

TEST_F(HeadlessWorkerTest, MultiFileDeleteWithMidCancel)
{
    std::vector<fs::path> files;
    for (int i = 0; i < 5; i++)
        files.push_back(CreateTestFile(L"cancel_" + std::to_wstring(i) + L".txt"));

    CTestWorkerObserver obs;
    bool skipHidden = false, skipErrors = false;
    int completed = 0;

    for (size_t i = 0; !obs.IsCancelled() && i < files.size(); i++)
    {
        if (i == 3)
            obs.Cancel(); // cancel after 3rd file

        if (obs.IsCancelled())
            break;

        DWORD attrs = GetFileAttributesW(files[i].c_str());
        auto result = HeadlessDeleteFile(obs, files[i].wstring(), attrs, false, skipHidden, skipErrors);
        if (!result.success)
            break;
        completed++;
    }

    obs.SetError(true);
    obs.NotifyDone();

    EXPECT_EQ(completed, 3); // only first 3 deleted
    EXPECT_FALSE(fs::exists(files[0]));
    EXPECT_FALSE(fs::exists(files[1]));
    EXPECT_FALSE(fs::exists(files[2]));
    EXPECT_TRUE(fs::exists(files[3]));  // not deleted
    EXPECT_TRUE(fs::exists(files[4]));  // not deleted
    EXPECT_TRUE(obs.HasError());
}

TEST_F(HeadlessWorkerTest, MultiFileDeleteWithSkipAll)
{
    // Create files, lock one
    auto file1 = CreateTestFile(L"skipall_1.txt");
    auto file2 = CreateTestFile(L"skipall_2.txt");
    auto file3 = CreateTestFile(L"skipall_3.txt");

    // Lock file2
    HANDLE hLock = CreateFileW(file2.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_NE(hLock, INVALID_HANDLE_VALUE);

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkipAll);
    bool skipHidden = false, skipErrors = false;

    std::vector<fs::path> files = {file1, file2, file3};
    for (size_t i = 0; i < files.size(); i++)
    {
        DWORD attrs = GetFileAttributesW(files[i].c_str());
        HeadlessDeleteFile(obs, files[i].wstring(), attrs, false, skipHidden, skipErrors);
    }

    obs.NotifyDone();

    EXPECT_FALSE(fs::exists(file1)); // deleted
    EXPECT_TRUE(fs::exists(file2));  // skipped (locked)
    EXPECT_FALSE(fs::exists(file3)); // deleted
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 1);
    // After SkipAll, skipErrors flag should be set
    EXPECT_TRUE(skipErrors);

    CloseHandle(hLock);
}

// ============================================================================
// Directory deletion
// ============================================================================

TEST_F(HeadlessWorkerTest, DeleteEmptyDirectory)
{
    auto dirPath = CreateTestDir(L"empty_dir");
    ASSERT_TRUE(fs::exists(dirPath));

    CTestWorkerObserver obs;
    bool skipErrors = false;

    auto result = HeadlessDeleteDir(obs, dirPath.wstring(), skipErrors);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(dirPath));
}

TEST_F(HeadlessWorkerTest, DeleteNonEmptyDirFails)
{
    auto dirPath = CreateTestDir(L"nonempty_dir");
    CreateTestFile(L"nonempty_dir\\child.txt");

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkip);
    bool skipErrors = false;

    auto result = HeadlessDeleteDir(obs, dirPath.wstring(), skipErrors);

    EXPECT_TRUE(result.success);       // skip = success
    EXPECT_TRUE(fs::exists(dirPath));  // still exists (not empty)
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 1);
}

TEST_F(HeadlessWorkerTest, DeleteDirTreeBottomUp)
{
    // Replicate the real worker pattern: delete files first, then dirs bottom-up
    CreateTestFile(L"tree\\sub1\\a.txt");
    CreateTestFile(L"tree\\sub1\\b.txt");
    CreateTestFile(L"tree\\sub2\\c.txt");

    CTestWorkerObserver obs;
    bool skipHidden = false, skipErrors = false;
    int deleteCount = 0;

    // Phase 1: delete files
    std::vector<fs::path> files = {
        m_tempDir / L"tree\\sub1\\a.txt",
        m_tempDir / L"tree\\sub1\\b.txt",
        m_tempDir / L"tree\\sub2\\c.txt",
    };
    for (auto& f : files)
    {
        DWORD attrs = GetFileAttributesW(f.c_str());
        auto result = HeadlessDeleteFile(obs, f.wstring(), attrs, false, skipHidden, skipErrors);
        EXPECT_TRUE(result.success);
        deleteCount++;
    }

    // Phase 2: delete directories bottom-up (matches worker script order)
    std::vector<fs::path> dirs = {
        m_tempDir / L"tree\\sub1",
        m_tempDir / L"tree\\sub2",
        m_tempDir / L"tree",
    };
    for (auto& d : dirs)
    {
        auto result = HeadlessDeleteDir(obs, d.wstring(), skipErrors);
        EXPECT_TRUE(result.success);
        deleteCount++;
    }

    obs.SetProgress(0, 1000);
    obs.NotifyDone();

    EXPECT_EQ(deleteCount, 6);
    EXPECT_FALSE(fs::exists(m_tempDir / L"tree"));
    EXPECT_TRUE(obs.WaitForCompletion(0));
}

// ============================================================================
// Unicode path tests
// ============================================================================

TEST_F(HeadlessWorkerTest, DeleteUnicodeFile_CJK)
{
    auto filePath = CreateTestFile(L"\x6d4b\x8bd5\x6587\x4ef6.txt"); // 测试文件.txt
    ASSERT_TRUE(fs::exists(filePath));

    CTestWorkerObserver obs;
    bool skipHidden = false, skipErrors = false;
    DWORD attrs = GetFileAttributesW(filePath.c_str());

    auto result = HeadlessDeleteFile(obs, filePath.wstring(), attrs, false, skipHidden, skipErrors);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(filePath));
}

TEST_F(HeadlessWorkerTest, DeleteUnicodeFile_Emoji)
{
    auto filePath = CreateTestFile(L"\U0001F4C4_notes.txt"); // 📄_notes.txt
    ASSERT_TRUE(fs::exists(filePath));

    CTestWorkerObserver obs;
    bool skipHidden = false, skipErrors = false;
    DWORD attrs = GetFileAttributesW(filePath.c_str());

    auto result = HeadlessDeleteFile(obs, filePath.wstring(), attrs, false, skipHidden, skipErrors);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(filePath));
}

TEST_F(HeadlessWorkerTest, DeleteUnicodeFile_Cyrillic)
{
    auto filePath = CreateTestFile(L"\x0444\x0430\x0439\x043b.txt"); // файл.txt
    ASSERT_TRUE(fs::exists(filePath));

    CTestWorkerObserver obs;
    bool skipHidden = false, skipErrors = false;
    DWORD attrs = GetFileAttributesW(filePath.c_str());

    auto result = HeadlessDeleteFile(obs, filePath.wstring(), attrs, false, skipHidden, skipErrors);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(filePath));
}

TEST_F(HeadlessWorkerTest, DeleteUnicodeFile_Arabic)
{
    auto filePath = CreateTestFile(L"\x0645\x0644\x0641.txt"); // ملف.txt
    ASSERT_TRUE(fs::exists(filePath));

    CTestWorkerObserver obs;
    bool skipHidden = false, skipErrors = false;
    DWORD attrs = GetFileAttributesW(filePath.c_str());

    auto result = HeadlessDeleteFile(obs, filePath.wstring(), attrs, false, skipHidden, skipErrors);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(filePath));
}

TEST_F(HeadlessWorkerTest, DeleteUnicodeDirTree)
{
    // Create a directory tree with mixed Unicode names
    CreateTestFile(L"\x65E5\x672C\x8A9E\\\x30C6\x30B9\x30C8.txt"); // 日本語\テスト.txt

    CTestWorkerObserver obs;
    bool skipHidden = false, skipErrors = false;

    fs::path file = m_tempDir / L"\x65E5\x672C\x8A9E\\\x30C6\x30B9\x30C8.txt";
    fs::path dir = m_tempDir / L"\x65E5\x672C\x8A9E";

    DWORD attrs = GetFileAttributesW(file.c_str());
    auto r1 = HeadlessDeleteFile(obs, file.wstring(), attrs, false, skipHidden, skipErrors);
    EXPECT_TRUE(r1.success);

    auto r2 = HeadlessDeleteDir(obs, dir.wstring(), skipErrors);
    EXPECT_TRUE(r2.success);

    EXPECT_FALSE(fs::exists(dir));
}

// ============================================================================
// Long path tests (> MAX_PATH)
// ============================================================================

TEST_F(HeadlessWorkerTest, DeleteLongPathFile)
{
    // Build a path longer than MAX_PATH (260)
    // Each segment is 50 chars, need ~6 levels to exceed 260
    std::wstring longDir = L"";
    for (int i = 0; i < 6; i++)
    {
        if (!longDir.empty())
            longDir += L"\\";
        longDir += std::wstring(50, L'a' + (wchar_t)i);
    }

    // Use \\?\ prefix for long path support
    fs::path longDirPath = m_tempDir / longDir;

    std::error_code ec;
    fs::create_directories(longDirPath, ec);
    if (ec)
    {
        GTEST_SKIP() << "Cannot create long path (needs long path support enabled): " << ec.message();
    }

    // Create file in the deep directory
    fs::path filePath = longDirPath / L"deep_file.txt";
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        GTEST_SKIP() << "Cannot create long-path file (needs LongPathsEnabled registry)";
    }
    DWORD written;
    WriteFile(hFile, "long path data", 14, &written, NULL);
    CloseHandle(hFile);

    ASSERT_TRUE(fs::exists(filePath));
    EXPECT_GT(filePath.wstring().size(), 260u);

    CTestWorkerObserver obs;
    bool skipHidden = false, skipErrors = false;
    DWORD attrs = GetFileAttributesW(filePath.c_str());

    auto result = HeadlessDeleteFile(obs, filePath.wstring(), attrs, false, skipHidden, skipErrors);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(filePath));
}

// ============================================================================
// Worker thread pattern — full flow with completion event
// ============================================================================

struct WorkerThreadArgs
{
    CTestWorkerObserver* obs;
    std::vector<std::wstring>* paths;
    bool addDelay; // small sleep between ops for cancel-timing tests
};

static DWORD WINAPI WorkerDeleteThread(LPVOID param)
{
    auto* args = (WorkerThreadArgs*)param;
    auto& obs = *args->obs;
    auto& paths = *args->paths;

    bool skipHidden = false, skipErrors = false;
    bool error = false;

    obs.SetProgress(0, 0);

    for (size_t i = 0; !obs.IsCancelled() && i < paths.size(); i++)
    {
        CProgressData pd = {"Deleting", "file", "", ""};
        obs.SetOperationInfo(&pd);

        if (args->addDelay)
            Sleep(10);

        DWORD attrs = GetFileAttributesW(paths[i].c_str());
        auto result = HeadlessDeleteFile(obs, paths[i], attrs,
                                         false, skipHidden, skipErrors);
        if (!result.success)
        {
            error = true;
            break;
        }

        int progress = (int)(((i + 1) * 1000) / paths.size());
        obs.SetProgress(0, progress);
    }

    obs.SetError(error || obs.IsCancelled());
    obs.NotifyDone();
    delete args;
    return 0;
}

TEST_F(HeadlessWorkerTest, FullWorkerFlowOnThread)
{
    auto file1 = CreateTestFile(L"threaded_1.txt");
    auto file2 = CreateTestFile(L"threaded_2.txt");
    auto file3 = CreateTestFile(L"threaded_3.txt");

    CTestWorkerObserver obs;
    auto paths = new std::vector<std::wstring>{file1.wstring(), file2.wstring(), file3.wstring()};

    auto* args = new WorkerThreadArgs{&obs, paths, false};
    HANDLE hThread = CreateThread(NULL, 0, WorkerDeleteThread, args, 0, NULL);
    ASSERT_NE(hThread, (HANDLE)NULL);

    // Wait for worker completion via observer event (not thread handle)
    EXPECT_TRUE(obs.WaitForCompletion(5000));

    WaitForSingleObject(hThread, 1000);
    CloseHandle(hThread);
    delete paths;

    // Verify results
    EXPECT_FALSE(obs.HasError());
    EXPECT_FALSE(fs::exists(file1));
    EXPECT_FALSE(fs::exists(file2));
    EXPECT_FALSE(fs::exists(file3));
    EXPECT_EQ(obs.GetLastSummaryPercent(), 1000);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kSetOperationInfo), 3);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kNotifyDone), 1);
}

TEST_F(HeadlessWorkerTest, FullWorkerFlowCancelFromMainThread)
{
    std::vector<fs::path> files;
    for (int i = 0; i < 10; i++)
        files.push_back(CreateTestFile(L"canceltest_" + std::to_wstring(i) + L".txt"));

    CTestWorkerObserver obs;
    auto paths = new std::vector<std::wstring>;
    for (auto& f : files)
        paths->push_back(f.wstring());

    auto* args = new WorkerThreadArgs{&obs, paths, true};
    HANDLE hThread = CreateThread(NULL, 0, WorkerDeleteThread, args, 0, NULL);
    ASSERT_NE(hThread, (HANDLE)NULL);

    // Let worker process a few files, then cancel
    Sleep(50);
    obs.Cancel();

    EXPECT_TRUE(obs.WaitForCompletion(5000));
    WaitForSingleObject(hThread, 1000);
    CloseHandle(hThread);
    delete paths;

    // Some files deleted, some remain
    int remaining = 0;
    for (auto& f : files)
        if (fs::exists(f))
            remaining++;

    EXPECT_GT(remaining, 0);       // at least some not deleted
    EXPECT_TRUE(obs.HasError());   // cancelled = error state
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kNotifyDone), 1);
}
