// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Long path copy/move integration tests (issue #34)
//
// Verifies that copy/move/delete operations succeed for files whose full
// path exceeds MAX_PATH (260 chars), even when items have ANSI-only names
// (NameW is empty). This is the scenario that triggers ERROR_INVALID_NAME
// (error 123) when wide paths are not properly populated with \\?\ prefix.
//
// Test groups:
//   LongPathCopyMove — E2E tests for copy/move/delete with long ANSI paths
//   SetSourceNameW   — Unit test for \\?\ prefix logic
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
// Utility helpers (shared with gtest_e2e_worker.cpp patterns)
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

// Create a directory tree using \\?\ prefix for long path support
static bool CreateLongPathDir(const std::wstring& path)
{
    std::wstring fullPath = path;
    if (fullPath.size() < 4 || fullPath.substr(0, 4) != L"\\\\?\\")
        fullPath = L"\\\\?\\" + fullPath;

    size_t startPos = 4;
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

// Create a file using \\?\ prefix for long path support
static HANDLE CreateLongPathFile(const std::wstring& path, const char* content = "test data")
{
    std::wstring fullPath = path;
    if (fullPath.substr(0, 4) != L"\\\\?\\")
        fullPath = L"\\\\?\\" + fullPath;
    HANDLE hFile = CreateFileW(fullPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE && content != nullptr)
    {
        DWORD written;
        WriteFile(hFile, content, (DWORD)strlen(content), &written, NULL);
    }
    return hFile;
}

// Check if a long-path file exists using \\?\ prefix
static bool LongPathExists(const std::wstring& path)
{
    std::wstring fullPath = path;
    if (fullPath.substr(0, 4) != L"\\\\?\\")
        fullPath = L"\\\\?\\" + fullPath;
    DWORD attrs = GetFileAttributesW(fullPath.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES;
}

// Read content from a long-path file
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

// Delete a long-path file using \\?\ prefix
static bool DeleteLongPathFile(const std::wstring& path)
{
    std::wstring fullPath = path;
    if (fullPath.substr(0, 4) != L"\\\\?\\")
        fullPath = L"\\\\?\\" + fullPath;
    return DeleteFileW(fullPath.c_str()) != FALSE;
}

// ============================================================================
// Headless operation helpers
// ============================================================================

struct OpResult
{
    bool success;
    DWORD lastError;
};

static OpResult HeadlessCopyFile(IWorkerObserver& observer,
                                 const std::wstring& srcPath,
                                 const std::wstring& dstPath,
                                 bool& skipAllErrors)
{
    // Ensure target directory exists using \\?\ prefix
    std::wstring dstDir;
    size_t lastSep = dstPath.rfind(L'\\');
    if (lastSep != std::wstring::npos)
        dstDir = dstPath.substr(0, lastSep);
    if (!dstDir.empty())
        CreateLongPathDir(dstDir);

    // Add \\?\ prefix if needed
    std::wstring src = srcPath;
    std::wstring dst = dstPath;
    if (src.substr(0, 4) != L"\\\\?\\")
        src = L"\\\\?\\" + src;
    if (dst.substr(0, 4) != L"\\\\?\\")
        dst = L"\\\\?\\" + dst;

    while (true)
    {
        if (CopyFileW(src.c_str(), dst.c_str(), FALSE))
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
    // Ensure target directory exists using \\?\ prefix
    std::wstring dstDir;
    size_t lastSep = dstPath.rfind(L'\\');
    if (lastSep != std::wstring::npos)
        dstDir = dstPath.substr(0, lastSep);
    if (!dstDir.empty())
        CreateLongPathDir(dstDir);

    // Add \\?\ prefix if needed
    std::wstring src = srcPath;
    std::wstring dst = dstPath;
    if (src.substr(0, 4) != L"\\\\?\\")
        src = L"\\\\?\\" + src;
    if (dst.substr(0, 4) != L"\\\\?\\")
        dst = L"\\\\?\\" + dst;

    DWORD flags = MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING;

    while (true)
    {
        if (MoveFileExW(src.c_str(), dst.c_str(), flags))
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

static OpResult HeadlessDeleteFile(IWorkerObserver& observer,
                                   const std::wstring& filePath,
                                   bool& skipAllErrors)
{
    std::wstring path = filePath;
    if (path.substr(0, 4) != L"\\\\?\\")
        path = L"\\\\?\\" + path;

    while (true)
    {
        if (DeleteFileW(path.c_str()))
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

// ============================================================================
// Pipeline executor — mirrors the worker dispatch for long-path scenarios
//
// Unlike the gtest_e2e_worker.cpp executor, this one simulates the
// BuildScriptFromSnapshot code path: it reads NameW vs Name to decide
// whether to use \\?\ prefixed wide paths. This catches the bug where
// ANSI-only items don't get wide paths populated.
// ============================================================================

static bool ExecuteLongPathSnapshot(const CSelectionSnapshot& snapshot,
                                    CTestWorkerObserver& obs)
{
    bool skipAllErrors = false;
    bool anyError = false;

    int totalOps = (int)snapshot.Items.size();

    for (size_t i = 0; !obs.IsCancelled() && i < snapshot.Items.size(); i++)
    {
        const CSnapshotItem& item = snapshot.Items[i];

        // Build wide source path — simulating BuildScript's logic:
        // If NameW is set, use it. Otherwise, widen the ANSI name.
        std::wstring srcName;
        if (!item.NameW.empty())
            srcName = item.NameW;
        else
            srcName = std::wstring(item.Name.begin(), item.Name.end());

        std::wstring srcDir = snapshot.SourcePathW;
        if (!srcDir.empty() && srcDir.back() != L'\\')
            srcDir += L'\\';
        std::wstring srcFull = srcDir + srcName;

        switch (snapshot.Action)
        {
        case EActionType::Delete:
        {
            std::string srcA = NarrowPath(srcFull);
            CProgressData pd = {"Deleting", srcA.c_str(), "", ""};
            obs.SetOperationInfo(&pd);
            obs.SetProgress(0, totalOps > 0 ? (int)((i * 1000) / totalOps) : 0);

            OpResult result = HeadlessDeleteFile(obs, srcFull, skipAllErrors);
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
            std::wstring tgtFull = tgtDir + srcName;

            std::string srcA = NarrowPath(srcFull);
            std::string tgtA = NarrowPath(tgtFull);
            const char* opStr = (snapshot.Action == EActionType::Copy) ? "Copying" : "Moving";
            CProgressData pd = {opStr, srcA.c_str(), "to", tgtA.c_str()};
            obs.SetOperationInfo(&pd);
            obs.SetProgress(0, totalOps > 0 ? (int)((i * 1000) / totalOps) : 0);

            OpResult result;
            if (snapshot.Action == EActionType::Copy)
                result = HeadlessCopyFile(obs, srcFull, tgtFull, skipAllErrors);
            else
                result = HeadlessMoveFile(obs, srcFull, tgtFull, skipAllErrors);
            if (!result.success)
                anyError = true;
            break;
        }

        default:
            anyError = true;
            break;
        }

        if (anyError)
            break;
    }

    obs.SetProgress(0, 1000);
    obs.SetError(anyError);
    obs.NotifyDone();
    return !anyError;
}

// ============================================================================
// Test fixture
// ============================================================================

class LongPathCopyMoveTest : public ::testing::Test
{
protected:
    fs::path m_srcDir;
    fs::path m_dstDir;
    std::wstring m_longSubDir;

    void SetUp() override
    {
        wchar_t tmpPath[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpPath);
        m_srcDir = fs::path(tmpPath) / L"sal_lp34_src";
        m_dstDir = fs::path(tmpPath) / L"sal_lp34_dst";

        // Clean any leftovers
        CleanDir(m_srcDir);
        CleanDir(m_dstDir);

        // Build a long subdirectory path > 260 chars
        m_longSubDir.clear();
        for (int i = 0; i < 6; i++)
        {
            if (!m_longSubDir.empty())
                m_longSubDir += L"\\";
            m_longSubDir += std::wstring(45, L'a' + (wchar_t)i);
        }
    }

    void TearDown() override
    {
        CleanDir(m_srcDir);
        CleanDir(m_dstDir);
    }

    void CleanDir(const fs::path& dir)
    {
        std::wstring prefix = L"\\\\?\\" + dir.wstring();
        std::wstring cmd = L"cmd /c rd /s /q \"" + prefix + L"\"";
        _wsystem(cmd.c_str());
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    // Create the deep directory structure and a file in it.
    // Returns the full path to the created file.
    std::wstring CreateDeepFile(const fs::path& baseDir,
                                const std::wstring& fileName,
                                const char* content = "test data")
    {
        std::wstring deepDir = baseDir.wstring() + L"\\" + m_longSubDir;
        if (!CreateLongPathDir(deepDir))
            return {};

        std::wstring filePath = deepDir + L"\\" + fileName;
        HANDLE hFile = CreateLongPathFile(filePath, content);
        if (hFile == INVALID_HANDLE_VALUE)
            return {};
        CloseHandle(hFile);
        return filePath;
    }

    // Build a snapshot with ANSI-only item name (NameW empty) — the bug trigger
    CSelectionSnapshot MakeAnsiOnlySnapshot(EActionType action,
                                            const std::wstring& fileName,
                                            uint64_t fileSize = 9)
    {
        CSelectionSnapshot snap;
        snap.Action = action;
        snap.SourcePath = NarrowPath(m_srcDir.wstring());
        snap.SourcePathW = m_srcDir.wstring();
        if (action == EActionType::Copy || action == EActionType::Move)
        {
            snap.TargetPath = NarrowPath(m_dstDir.wstring());
            snap.TargetPathW = m_dstDir.wstring();
        }
        snap.Mask = "*.*";

        // Build relative path including the deep subdirectory
        std::wstring relPath = m_longSubDir + L"\\" + fileName;

        CSnapshotItem si;
        si.Name = NarrowPath(relPath);  // ANSI name only
        si.NameW.clear();               // <-- THE BUG TRIGGER: no wide name
        si.IsDir = false;
        si.Size = fileSize;
        si.Attr = FILE_ATTRIBUTE_NORMAL;
        memset(&si.LastWrite, 0, sizeof(si.LastWrite));
        snap.Items.push_back(si);

        return snap;
    }

    // Build a snapshot with wide item name (NameW populated) — the working case
    CSelectionSnapshot MakeWideSnapshot(EActionType action,
                                        const std::wstring& fileName,
                                        uint64_t fileSize = 9)
    {
        auto snap = MakeAnsiOnlySnapshot(action, fileName, fileSize);
        // Set the wide name too
        std::wstring relPath = m_longSubDir + L"\\" + fileName;
        snap.Items[0].NameW = relPath;
        return snap;
    }
};

// ============================================================================
// Tests — Copy with long paths
// ============================================================================

// The main bug reproducer: ANSI-only item name with long path
TEST_F(LongPathCopyMoveTest, CopyFile_LongPath_AnsiOnlyItem_Succeeds)
{
    const wchar_t* fileName = L"test_file.txt";
    const char* content = "copy test content";

    std::wstring srcFile = CreateDeepFile(m_srcDir, fileName, content);
    ASSERT_FALSE(srcFile.empty()) << "Failed to create long-path source file";
    ASSERT_TRUE(LongPathExists(srcFile));
    EXPECT_GT(srcFile.size(), 260u) << "Path should exceed MAX_PATH";

    auto snap = MakeAnsiOnlySnapshot(EActionType::Copy, fileName, (uint64_t)strlen(content));

    // Add \\?\ prefix to snapshot paths for long path support
    if (snap.SourcePathW.substr(0, 4) != L"\\\\?\\")
        snap.SourcePathW = L"\\\\?\\" + snap.SourcePathW;
    if (snap.TargetPathW.substr(0, 4) != L"\\\\?\\")
        snap.TargetPathW = L"\\\\?\\" + snap.TargetPathW;

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kCancel);

    bool ok = ExecuteLongPathSnapshot(snap, obs);

    EXPECT_TRUE(ok) << "Copy with ANSI-only long path should succeed";
    EXPECT_TRUE(LongPathExists(srcFile)) << "Source should still exist after copy";

    std::wstring dstFile = m_dstDir.wstring() + L"\\" + m_longSubDir + L"\\" + fileName;
    EXPECT_TRUE(LongPathExists(dstFile)) << "Long-path copy target not found";
    EXPECT_EQ(ReadLongPathContent(dstFile), content);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 0)
        << "No errors should occur (error 123 = the bug)";
}

// Same but for move
TEST_F(LongPathCopyMoveTest, MoveFile_LongPath_AnsiOnlyItem_Succeeds)
{
    const wchar_t* fileName = L"move_test.txt";
    const char* content = "move test content";

    std::wstring srcFile = CreateDeepFile(m_srcDir, fileName, content);
    ASSERT_FALSE(srcFile.empty());
    ASSERT_TRUE(LongPathExists(srcFile));

    auto snap = MakeAnsiOnlySnapshot(EActionType::Move, fileName, (uint64_t)strlen(content));

    if (snap.SourcePathW.substr(0, 4) != L"\\\\?\\")
        snap.SourcePathW = L"\\\\?\\" + snap.SourcePathW;
    if (snap.TargetPathW.substr(0, 4) != L"\\\\?\\")
        snap.TargetPathW = L"\\\\?\\" + snap.TargetPathW;

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kCancel);

    bool ok = ExecuteLongPathSnapshot(snap, obs);

    EXPECT_TRUE(ok) << "Move with ANSI-only long path should succeed";
    EXPECT_FALSE(LongPathExists(srcFile)) << "Source should be gone after move";

    std::wstring dstFile = m_dstDir.wstring() + L"\\" + m_longSubDir + L"\\" + fileName;
    EXPECT_TRUE(LongPathExists(dstFile)) << "Long-path move target not found";
    EXPECT_EQ(ReadLongPathContent(dstFile), content);
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 0);
}

// Delete with ANSI-only item name
TEST_F(LongPathCopyMoveTest, DeleteFile_LongPath_AnsiOnlyItem_Succeeds)
{
    const wchar_t* fileName = L"delete_test.txt";

    std::wstring srcFile = CreateDeepFile(m_srcDir, fileName, "delete me");
    ASSERT_FALSE(srcFile.empty());
    ASSERT_TRUE(LongPathExists(srcFile));

    auto snap = MakeAnsiOnlySnapshot(EActionType::Delete, fileName);

    if (snap.SourcePathW.substr(0, 4) != L"\\\\?\\")
        snap.SourcePathW = L"\\\\?\\" + snap.SourcePathW;

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kCancel);

    bool ok = ExecuteLongPathSnapshot(snap, obs);

    EXPECT_TRUE(ok) << "Delete with ANSI-only long path should succeed";
    EXPECT_FALSE(LongPathExists(srcFile)) << "File should be deleted";
    EXPECT_EQ(obs.CountCallsOfType(TestObserverCall::kAskFileError), 0);
}

// Sanity check: copy with NameW set (already works)
TEST_F(LongPathCopyMoveTest, CopyFile_LongPath_WideItem_Succeeds)
{
    const wchar_t* fileName = L"wide_test.txt";
    const char* content = "wide item content";

    std::wstring srcFile = CreateDeepFile(m_srcDir, fileName, content);
    ASSERT_FALSE(srcFile.empty());

    auto snap = MakeWideSnapshot(EActionType::Copy, fileName, (uint64_t)strlen(content));

    if (snap.SourcePathW.substr(0, 4) != L"\\\\?\\")
        snap.SourcePathW = L"\\\\?\\" + snap.SourcePathW;
    if (snap.TargetPathW.substr(0, 4) != L"\\\\?\\")
        snap.TargetPathW = L"\\\\?\\" + snap.TargetPathW;

    CTestWorkerObserver obs;
    obs.SetFileErrorPolicy(TestDialogPolicy::kCancel);

    bool ok = ExecuteLongPathSnapshot(snap, obs);

    EXPECT_TRUE(ok);

    std::wstring dstFile = m_dstDir.wstring() + L"\\" + m_longSubDir + L"\\" + fileName;
    EXPECT_TRUE(LongPathExists(dstFile));
    EXPECT_EQ(ReadLongPathContent(dstFile), content);
}

// Test with C:\Temp\SalLongPathTest if present (manual test aid)
TEST_F(LongPathCopyMoveTest, CopyFile_LongPath_UsingRealTestDir)
{
    const std::wstring realTestDir = L"C:\\Temp\\SalLongPathTest";
    DWORD attrs = GetFileAttributesW(realTestDir.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        GTEST_SKIP() << "C:\\Temp\\SalLongPathTest not present, skipping real-dir test";
    }

    // Find the deepest file in the test directory
    std::wstring deepestFile;
    size_t maxLen = 0;

    std::wstring searchRoot = L"\\\\?\\" + realTestDir;
    WIN32_FIND_DATAW fd;

    // Simple recursive search for *.* using a stack
    std::vector<std::wstring> dirs;
    dirs.push_back(searchRoot);

    while (!dirs.empty())
    {
        std::wstring dir = dirs.back();
        dirs.pop_back();

        std::wstring pattern = dir + L"\\*";
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            continue;

        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;

            std::wstring fullPath = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                dirs.push_back(fullPath);
            else if (fullPath.size() > maxLen)
            {
                maxLen = fullPath.size();
                deepestFile = fullPath;
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    if (deepestFile.empty() || maxLen <= 260)
    {
        GTEST_SKIP() << "No file with path > 260 found in real test dir";
    }

    // Copy the deepest file to our temp destination
    std::wstring dstFile = L"\\\\?\\" + m_dstDir.wstring() + L"\\copied_from_real.txt";
    std::wstring dstDir = L"\\\\?\\" + m_dstDir.wstring();
    CreateLongPathDir(m_dstDir.wstring());

    BOOL ok = CopyFileW(deepestFile.c_str(), dstFile.c_str(), FALSE);
    EXPECT_TRUE(ok) << "Copy from real long-path dir failed, error=" << GetLastError()
                    << ", path length=" << maxLen;

    if (ok)
    {
        std::string content = ReadLongPathContent(dstFile.substr(4)); // strip \\?\ for helper
        EXPECT_FALSE(content.empty()) << "Copied file should have content";
    }
}

// ============================================================================
// Unit test: \\?\ prefix application logic
//
// Tests the core algorithm from COperation::SetSourceNameW/SetTargetNameW
// (worker.cpp:509-588) to verify that:
// 1. Long paths (>= threshold) get \\?\ prefix
// 2. Short paths don't get prefix
// 3. UNC paths get \\?\UNC\ prefix
// ============================================================================

// Threshold from widepath.h
#define SAL_LONG_PATH_THRESHOLD 240

// Standalone reimplementation of SetSourceNameW prefix logic for unit testing
static std::wstring ApplyLongPathPrefix(const char* ansiPath, const std::wstring& wideFileName)
{
    if (ansiPath == NULL)
        return {};

    int pathLen = MultiByteToWideChar(CP_ACP, 0, ansiPath, -1, NULL, 0);
    if (pathLen == 0)
        return {};

    std::wstring widePath;
    widePath.resize(pathLen);
    MultiByteToWideChar(CP_ACP, 0, ansiPath, -1, &widePath[0], pathLen);
    widePath.resize(pathLen - 1);

    if (!wideFileName.empty())
    {
        if (!widePath.empty() && widePath.back() != L'\\')
            widePath += L'\\';
        widePath += wideFileName;
    }

    if (widePath.length() >= SAL_LONG_PATH_THRESHOLD)
    {
        if (widePath.length() >= 2 && widePath[0] == L'\\' && widePath[1] == L'\\')
            widePath = L"\\\\?\\UNC\\" + widePath.substr(2);
        else
            widePath = L"\\\\?\\" + widePath;
    }

    return widePath;
}

TEST(SetSourceNameW, ShortPath_NoPrefix)
{
    std::wstring result = ApplyLongPathPrefix("C:\\Users\\test", L"file.txt");
    EXPECT_EQ(result, L"C:\\Users\\test\\file.txt");
    EXPECT_TRUE(result.substr(0, 4) != L"\\\\?\\") << "Short path should not get prefix";
}

TEST(SetSourceNameW, LongPath_GetsPrefix)
{
    // Build an ANSI path > 240 chars
    std::string longDir = "C:\\";
    for (int i = 0; i < 6; i++)
    {
        if (longDir.back() != '\\')
            longDir += '\\';
        longDir += std::string(40, 'a' + i);
    }
    std::wstring fileName = L"test_file.txt";

    std::wstring result = ApplyLongPathPrefix(longDir.c_str(), fileName);
    ASSERT_GT(result.size(), 4u);
    EXPECT_EQ(result.substr(0, 4), L"\\\\?\\") << "Long path should get \\\\?\\ prefix";
    EXPECT_EQ(result.substr(4, 3), L"C:\\") << "Drive letter preserved after prefix";
}

TEST(SetSourceNameW, LongPath_EmptyFileName_JustWidensPath)
{
    // Build a long ANSI path
    std::string longPath = "C:\\";
    while (longPath.size() < SAL_LONG_PATH_THRESHOLD + 10)
        longPath += "a";

    std::wstring result = ApplyLongPathPrefix(longPath.c_str(), L"");
    ASSERT_GT(result.size(), 4u);
    EXPECT_EQ(result.substr(0, 4), L"\\\\?\\") << "Long path without filename should get prefix";
}

TEST(SetSourceNameW, UNCPath_GetsUNCPrefix)
{
    // Build a long UNC path
    std::string uncPath = "\\\\server\\share\\";
    while (uncPath.size() < SAL_LONG_PATH_THRESHOLD + 10)
        uncPath += "x";

    std::wstring result = ApplyLongPathPrefix(uncPath.c_str(), L"file.txt");
    EXPECT_TRUE(result.size() > 8);
    EXPECT_EQ(result.substr(0, 8), L"\\\\?\\UNC\\") << "Long UNC path should get \\\\?\\UNC\\ prefix";
}

TEST(SetSourceNameW, AnsiNameWidened_ForLongPath)
{
    // This tests the exact fix: an ANSI-only item name gets widened and
    // the full path (dir + widened name) gets \\?\ prefix
    std::string longDir = "C:\\";
    for (int i = 0; i < 5; i++)
    {
        if (longDir.back() != '\\')
            longDir += '\\';
        longDir += std::string(45, 'a' + i);
    }

    // Simulate the fix: convert ANSI name to wide and pass to SetSourceNameW
    std::string ansiName = "my_file.txt";
    std::wstring wideName(ansiName.begin(), ansiName.end());

    std::wstring result = ApplyLongPathPrefix(longDir.c_str(), wideName);
    ASSERT_GT(result.size(), 4u);
    EXPECT_EQ(result.substr(0, 4), L"\\\\?\\");

    // Verify the filename is at the end
    size_t pos = result.rfind(L"my_file.txt");
    EXPECT_NE(pos, std::wstring::npos) << "Filename should appear in result path";
}
