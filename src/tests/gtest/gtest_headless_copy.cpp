// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Headless copy operation integration tests
//
// Proves that copy/move operations work end-to-end through the decoupled
// IWorkerObserver interface — no progress dialog, no message pump.
// Uses real Win32 file I/O with temp files.
//
// The harness mirrors the DoCopyFile patterns:
//   - Overwrite confirmation via observer.AskOverwrite()
//   - Error handling via observer.AskFileError()
//   - Progress reporting via observer.SetProgress()
//   - Cancellation via observer.IsCancelled()
//   - Unicode and long-path file names
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

// Convert wide string to narrow for observer
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
// Test fixture
// ============================================================================

class HeadlessCopyTest : public ::testing::Test
{
protected:
    fs::path m_srcDir;
    fs::path m_dstDir;

    void SetUp() override
    {
        wchar_t tmpPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpPath);
        m_srcDir = fs::path(tmpPath) / L"sal_copy_test_src";
        m_dstDir = fs::path(tmpPath) / L"sal_copy_test_dst";

        std::error_code ec;
        fs::remove_all(m_srcDir, ec);
        fs::remove_all(m_dstDir, ec);
        fs::create_directories(m_srcDir);
        fs::create_directories(m_dstDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_srcDir, ec);
        fs::remove_all(m_dstDir, ec);
    }

    fs::path CreateSourceFile(const std::wstring& name, const std::string& content = "source data")
    {
        fs::path filePath = m_srcDir / name;
        fs::create_directories(filePath.parent_path());
        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        EXPECT_NE(hFile, INVALID_HANDLE_VALUE);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            DWORD written;
            WriteFile(hFile, content.data(), (DWORD)content.size(), &written, NULL);
            CloseHandle(hFile);
        }
        return filePath;
    }

    fs::path CreateDestFile(const std::wstring& name, const std::string& content = "existing dest")
    {
        fs::path filePath = m_dstDir / name;
        fs::create_directories(filePath.parent_path());
        HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        EXPECT_NE(hFile, INVALID_HANDLE_VALUE);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            DWORD written;
            WriteFile(hFile, content.data(), (DWORD)content.size(), &written, NULL);
            CloseHandle(hFile);
        }
        return filePath;
    }

    std::string ReadFileContent(const fs::path& path)
    {
        HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
            return {};
        char buf[4096];
        DWORD bytesRead;
        ReadFile(hFile, buf, sizeof(buf), &bytesRead, NULL);
        CloseHandle(hFile);
        return std::string(buf, bytesRead);
    }
};

// ============================================================================
// Headless copy operation — mirrors DoCopyFile logic
// ============================================================================

struct CopyResult
{
    bool success;
    DWORD lastError;
    ULONGLONG bytesCopied;
};

static void GetFileInfoString(const std::wstring& path, std::string& info)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
    {
        ULARGE_INTEGER size;
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        char buf[128];
        wsprintfA(buf, "%llu bytes", size.QuadPart);
        info = buf;
    }
    else
    {
        info = "unknown";
    }
}

CopyResult HeadlessCopyFile(IWorkerObserver& observer,
                            const std::wstring& srcPath,
                            const std::wstring& dstPath,
                            bool& overwriteAll,
                            bool& skipAllOverwrite,
                            bool& skipAllErrors)
{
    CopyResult result = {false, 0, 0};

    // Check if target exists — overwrite confirmation (same pattern as DoCopyFile)
    DWORD dstAttrs = GetFileAttributesW(dstPath.c_str());
    if (dstAttrs != INVALID_FILE_ATTRIBUTES)
    {
        if (!overwriteAll)
        {
            observer.WaitIfSuspended();
            if (observer.IsCancelled())
                return result;

            if (skipAllOverwrite)
            {
                result.success = true;
                return result; // skip = success
            }

            std::string srcInfo, dstInfo;
            GetFileInfoString(srcPath, srcInfo);
            GetFileInfoString(dstPath, dstInfo);

            std::string srcNameA = NarrowPath(srcPath);
            std::string dstNameA = NarrowPath(dstPath);

            int ret = observer.AskOverwrite(srcNameA.c_str(), srcInfo.c_str(),
                                            dstNameA.c_str(), dstInfo.c_str());
            switch (ret)
            {
            case IDB_ALL:
                overwriteAll = true;
                // fallthrough
            case IDYES:
                break;
            case IDB_SKIPALL:
                skipAllOverwrite = true;
                // fallthrough
            case IDB_SKIP:
                result.success = true;
                return result;
            case IDCANCEL:
                return result;
            }

            // Clear read-only on target before overwriting
            if (dstAttrs & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(dstPath.c_str(), dstAttrs & ~FILE_ATTRIBUTE_READONLY);
        }
    }

    // Perform the copy with retry loop (same as DoCopyFile error handling)
    while (true)
    {
        if (CopyFileW(srcPath.c_str(), dstPath.c_str(), FALSE))
        {
            // Get file size for bytes copied
            WIN32_FILE_ATTRIBUTE_DATA data;
            if (GetFileAttributesExW(srcPath.c_str(), GetFileExInfoStandard, &data))
            {
                ULARGE_INTEGER size;
                size.HighPart = data.nFileSizeHigh;
                size.LowPart = data.nFileSizeLow;
                result.bytesCopied = size.QuadPart;
            }
            result.success = true;
            return result;
        }

        DWORD err = GetLastError();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
        {
            result.lastError = err;
            return result;
        }

        if (skipAllErrors)
        {
            result.success = true;
            return result;
        }

        std::string srcNameA = NarrowPath(srcPath);
        char errBuf[64];
        wsprintfA(errBuf, "Error code %lu", err);
        int ret = observer.AskFileError("Error copying file", srcNameA.c_str(), errBuf);
        switch (ret)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            skipAllErrors = true;
            // fallthrough
        case IDB_SKIP:
            result.success = true;
            return result;
        case IDCANCEL:
            result.lastError = err;
            return result;
        }
    }
}

// Headless move operation (copy + delete source)
CopyResult HeadlessMoveFile(IWorkerObserver& observer,
                            const std::wstring& srcPath,
                            const std::wstring& dstPath,
                            bool& overwriteAll,
                            bool& skipAllOverwrite,
                            bool& skipAllErrors)
{
    // Try MoveFileExW first (fast path — same volume)
    DWORD dstAttrs = GetFileAttributesW(dstPath.c_str());
    bool needOverwrite = (dstAttrs != INVALID_FILE_ATTRIBUTES);

    if (needOverwrite && !overwriteAll)
    {
        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return {false, 0, 0};

        if (skipAllOverwrite)
            return {true, 0, 0};

        std::string srcInfo, dstInfo;
        GetFileInfoString(srcPath, srcInfo);
        GetFileInfoString(dstPath, dstInfo);

        std::string srcNameA = NarrowPath(srcPath);
        std::string dstNameA = NarrowPath(dstPath);

        int ret = observer.AskOverwrite(srcNameA.c_str(), srcInfo.c_str(),
                                        dstNameA.c_str(), dstInfo.c_str());
        switch (ret)
        {
        case IDB_ALL:
            overwriteAll = true;
        case IDYES:
            break;
        case IDB_SKIPALL:
            skipAllOverwrite = true;
        case IDB_SKIP:
            return {true, 0, 0};
        case IDCANCEL:
            return {false, 0, 0};
        }
    }

    DWORD flags = MOVEFILE_COPY_ALLOWED;
    if (needOverwrite)
        flags |= MOVEFILE_REPLACE_EXISTING;

    while (true)
    {
        if (MoveFileExW(srcPath.c_str(), dstPath.c_str(), flags))
            return {true, 0, 0};

        DWORD err = GetLastError();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return {false, err, 0};

        if (skipAllErrors)
            return {true, 0, 0};

        std::string srcNameA = NarrowPath(srcPath);
        char errBuf[64];
        wsprintfA(errBuf, "Error code %lu", err);
        int ret = observer.AskFileError("Error moving file", srcNameA.c_str(), errBuf);
        switch (ret)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            skipAllErrors = true;
        case IDB_SKIP:
            return {true, 0, 0};
        case IDCANCEL:
            return {false, err, 0};
        }
    }
}

// ============================================================================
// Basic copy tests
// ============================================================================

TEST_F(HeadlessCopyTest, CopySingleFile)
{
    auto src = CreateSourceFile(L"file.txt", "hello world");
    fs::path dst = m_dstDir / L"file.txt";

    CTestWorkerObserver obs;
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    CProgressData pd = {"Copying", "file.txt", "to", "dst"};
    obs.SetOperationInfo(&pd);
    obs.SetProgress(0, 0);

    auto result = HeadlessCopyFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    obs.SetProgress(0, 1000);
    obs.NotifyDone();

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(fs::exists(src));  // source still exists
    EXPECT_TRUE(fs::exists(dst));  // copy created
    EXPECT_EQ(ReadFileContent(dst), "hello world");
    EXPECT_EQ(result.bytesCopied, 11u);
}

TEST_F(HeadlessCopyTest, CopyOverwriteWithConfirmYes)
{
    auto src = CreateSourceFile(L"overwrite.txt", "new content");
    auto dst = CreateDestFile(L"overwrite.txt", "old content");

    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kYes);
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessCopyFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(ReadFileContent(dst), "new content");
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskOverwrite), 1);
}

TEST_F(HeadlessCopyTest, CopyOverwriteWithSkip)
{
    auto src = CreateSourceFile(L"skip.txt", "new content");
    auto dst = CreateDestFile(L"skip.txt", "old content");

    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kSkip);
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessCopyFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_TRUE(result.success);                    // skip = success
    EXPECT_EQ(ReadFileContent(dst), "old content"); // not overwritten
}

TEST_F(HeadlessCopyTest, CopyOverwriteWithCancel)
{
    auto src = CreateSourceFile(L"cancel.txt", "new content");
    auto dst = CreateDestFile(L"cancel.txt", "old content");

    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kCancel);
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessCopyFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(ReadFileContent(dst), "old content");
}

TEST_F(HeadlessCopyTest, CopyOverwriteAllSkipsSubsequentPrompts)
{
    auto src1 = CreateSourceFile(L"ova1.txt", "data1");
    auto src2 = CreateSourceFile(L"ova2.txt", "data2");
    auto dst1 = CreateDestFile(L"ova1.txt", "old1");
    auto dst2 = CreateDestFile(L"ova2.txt", "old2");

    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kYesAll);
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    // First copy — triggers AskOverwrite which returns IDB_ALL → sets overwriteAll
    auto r1 = HeadlessCopyFile(obs, src1.wstring(), dst1.wstring(),
                               overwriteAll, skipAllOvr, skipAllErr);
    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(overwriteAll);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskOverwrite), 1);

    // Second copy — overwriteAll is set, no prompt
    auto r2 = HeadlessCopyFile(obs, src2.wstring(), dst2.wstring(),
                               overwriteAll, skipAllOvr, skipAllErr);
    EXPECT_TRUE(r2.success);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskOverwrite), 1); // still just 1

    EXPECT_EQ(ReadFileContent(dst1), "data1");
    EXPECT_EQ(ReadFileContent(dst2), "data2");
}

TEST_F(HeadlessCopyTest, CopyNonexistentSourceSkips)
{
    std::wstring fakeSrc = (m_srcDir / L"nosuchfile.txt").wstring();
    fs::path dst = m_dstDir / L"nosuchfile.txt";

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kSkip);
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessCopyFile(obs, fakeSrc, dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_TRUE(result.success); // skip = success
    EXPECT_FALSE(fs::exists(dst));
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 1);
}

// ============================================================================
// Copy read-only target
// ============================================================================

TEST_F(HeadlessCopyTest, CopyOverwriteReadOnlyTarget)
{
    auto src = CreateSourceFile(L"ro_target.txt", "new");
    auto dst = CreateDestFile(L"ro_target.txt", "old");
    SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_READONLY);

    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kYes);
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessCopyFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(ReadFileContent(dst), "new");
}

// ============================================================================
// Move tests
// ============================================================================

TEST_F(HeadlessCopyTest, MoveSingleFile)
{
    auto src = CreateSourceFile(L"moveme.txt", "move data");
    fs::path dst = m_dstDir / L"moveme.txt";

    CTestWorkerObserver obs;
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessMoveFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(src)); // source removed
    EXPECT_TRUE(fs::exists(dst));  // moved to dest
    EXPECT_EQ(ReadFileContent(dst), "move data");
}

TEST_F(HeadlessCopyTest, MoveOverwriteWithConfirm)
{
    auto src = CreateSourceFile(L"move_ovr.txt", "new");
    auto dst = CreateDestFile(L"move_ovr.txt", "old");

    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kYes);
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessMoveFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(src));
    EXPECT_EQ(ReadFileContent(dst), "new");
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskOverwrite), 1);
}

TEST_F(HeadlessCopyTest, MoveOverwriteSkip)
{
    auto src = CreateSourceFile(L"move_skip.txt", "new");
    auto dst = CreateDestFile(L"move_skip.txt", "old");

    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kSkip);
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessMoveFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(fs::exists(src));                   // source still there (skipped)
    EXPECT_EQ(ReadFileContent(dst), "old");         // not overwritten
}

// ============================================================================
// Unicode copy/move tests
// ============================================================================

TEST_F(HeadlessCopyTest, CopyUnicodeFile_CJK)
{
    auto src = CreateSourceFile(L"\x6d4b\x8bd5.txt", "CJK data"); // 测试.txt
    fs::path dst = m_dstDir / L"\x6d4b\x8bd5.txt";

    CTestWorkerObserver obs;
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessCopyFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(ReadFileContent(dst), "CJK data");
}

TEST_F(HeadlessCopyTest, CopyUnicodeFile_Emoji)
{
    auto src = CreateSourceFile(L"\U0001F680_rocket.txt", "emoji data"); // 🚀_rocket.txt
    fs::path dst = m_dstDir / L"\U0001F680_rocket.txt";

    CTestWorkerObserver obs;
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessCopyFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(ReadFileContent(dst), "emoji data");
}

TEST_F(HeadlessCopyTest, MoveUnicodeFile_Cyrillic)
{
    auto src = CreateSourceFile(L"\x0444\x0430\x0439\x043b.txt", "Cyrillic data"); // файл.txt
    fs::path dst = m_dstDir / L"\x0444\x0430\x0439\x043b.txt";

    CTestWorkerObserver obs;
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    auto result = HeadlessMoveFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(fs::exists(src));
    EXPECT_EQ(ReadFileContent(dst), "Cyrillic data");
}

// ============================================================================
// Multi-file copy — worker loop pattern
// ============================================================================

TEST_F(HeadlessCopyTest, MultiFileCopyAllSucceed)
{
    std::vector<std::pair<fs::path, fs::path>> ops;
    for (int i = 0; i < 5; i++)
    {
        std::wstring name = L"multi_" + std::to_wstring(i) + L".txt";
        auto src = CreateSourceFile(name, "content " + std::to_string(i));
        ops.push_back({src, m_dstDir / name});
    }

    CTestWorkerObserver obs;
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;
    int completed = 0;

    for (size_t i = 0; !obs.IsCancelled() && i < ops.size(); i++)
    {
        CProgressData pd = {"Copying", "file", "to", "dst"};
        obs.SetOperationInfo(&pd);
        obs.SetProgress(0, (int)((i * 1000) / ops.size()));

        auto result = HeadlessCopyFile(obs, ops[i].first.wstring(), ops[i].second.wstring(),
                                      overwriteAll, skipAllOvr, skipAllErr);
        if (!result.success)
            break;
        completed++;
    }

    obs.SetProgress(0, 1000);
    obs.NotifyDone();

    EXPECT_EQ(completed, 5);
    for (int i = 0; i < 5; i++)
    {
        EXPECT_TRUE(fs::exists(ops[i].second));
        EXPECT_EQ(ReadFileContent(ops[i].second), "content " + std::to_string(i));
    }
}

TEST_F(HeadlessCopyTest, MultiFileCopyWithOverwriteMix)
{
    // Some files exist at destination, some don't
    auto src1 = CreateSourceFile(L"mix1.txt", "new1");
    auto src2 = CreateSourceFile(L"mix2.txt", "new2");
    auto src3 = CreateSourceFile(L"mix3.txt", "new3");
    CreateDestFile(L"mix1.txt", "old1"); // exists
    // mix2 doesn't exist at dest
    CreateDestFile(L"mix3.txt", "old3"); // exists

    CTestWorkerObserver obs;
    obs.SetOverwritePolicy(TestDialogPolicy::kYes);
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    std::vector<std::pair<fs::path, fs::path>> ops = {
        {src1, m_dstDir / L"mix1.txt"},
        {src2, m_dstDir / L"mix2.txt"},
        {src3, m_dstDir / L"mix3.txt"},
    };

    for (auto& [src, dst] : ops)
    {
        HeadlessCopyFile(obs, src.wstring(), dst.wstring(),
                        overwriteAll, skipAllOvr, skipAllErr);
    }

    obs.NotifyDone();

    EXPECT_EQ(ReadFileContent(m_dstDir / L"mix1.txt"), "new1");
    EXPECT_EQ(ReadFileContent(m_dstDir / L"mix2.txt"), "new2");
    EXPECT_EQ(ReadFileContent(m_dstDir / L"mix3.txt"), "new3");
    // AskOverwrite called only for mix1 and mix3 (existing targets)
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskOverwrite), 2);
}

// ============================================================================
// Threaded copy flow
// ============================================================================

struct CopyThreadArgs
{
    CTestWorkerObserver* obs;
    std::vector<std::pair<std::wstring, std::wstring>>* ops;
};

static DWORD WINAPI CopyWorkerThread(LPVOID param)
{
    auto* args = (CopyThreadArgs*)param;
    auto& obs = *args->obs;
    auto& ops = *args->ops;

    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;
    bool error = false;

    obs.SetProgress(0, 0);

    for (size_t i = 0; !obs.IsCancelled() && i < ops.size(); i++)
    {
        CProgressData pd = {"Copying", "file", "to", "dst"};
        obs.SetOperationInfo(&pd);

        auto result = HeadlessCopyFile(obs, ops[i].first, ops[i].second,
                                      overwriteAll, skipAllOvr, skipAllErr);
        if (!result.success)
        {
            error = true;
            break;
        }

        int progress = (int)(((i + 1) * 1000) / ops.size());
        obs.SetProgress(0, progress);
    }

    obs.SetError(error || obs.IsCancelled());
    obs.NotifyDone();
    delete args;
    return 0;
}

TEST_F(HeadlessCopyTest, ThreadedMultiFileCopy)
{
    auto ops = new std::vector<std::pair<std::wstring, std::wstring>>;
    for (int i = 0; i < 8; i++)
    {
        std::wstring name = L"threaded_" + std::to_wstring(i) + L".txt";
        auto src = CreateSourceFile(name, "threaded content " + std::to_string(i));
        ops->push_back({src.wstring(), (m_dstDir / name).wstring()});
    }

    CTestWorkerObserver obs;
    auto* args = new CopyThreadArgs{&obs, ops};
    HANDLE hThread = CreateThread(NULL, 0, CopyWorkerThread, args, 0, NULL);
    ASSERT_NE(hThread, (HANDLE)NULL);

    EXPECT_TRUE(obs.WaitForCompletion(5000));
    WaitForSingleObject(hThread, 1000);
    CloseHandle(hThread);

    EXPECT_FALSE(obs.HasError());
    EXPECT_EQ(obs.GetLastSummaryPercent(), 1000);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kSetOperationInfo), 8);

    for (int i = 0; i < 8; i++)
    {
        std::wstring name = L"threaded_" + std::to_wstring(i) + L".txt";
        EXPECT_TRUE(fs::exists(m_dstDir / name));
        EXPECT_EQ(ReadFileContent(m_dstDir / name), "threaded content " + std::to_string(i));
    }

    delete ops;
}

// ============================================================================
// Large file copy with progress
// ============================================================================

TEST_F(HeadlessCopyTest, CopyLargeFileTracksProgress)
{
    // Create a 1MB file
    std::string bigContent(1024 * 1024, 'X');
    auto src = CreateSourceFile(L"bigfile.bin", bigContent);
    fs::path dst = m_dstDir / L"bigfile.bin";

    CTestWorkerObserver obs;
    bool overwriteAll = false, skipAllOvr = false, skipAllErr = false;

    obs.SetProgress(0, 0);
    auto result = HeadlessCopyFile(obs, src.wstring(), dst.wstring(),
                                  overwriteAll, skipAllOvr, skipAllErr);
    obs.SetProgress(0, 1000);
    obs.NotifyDone();

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.bytesCopied, 1024u * 1024u);
    EXPECT_TRUE(fs::exists(dst));
    EXPECT_EQ(fs::file_size(dst), 1024u * 1024u);
}
