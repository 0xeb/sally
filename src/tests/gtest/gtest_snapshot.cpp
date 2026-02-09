// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Tests for CSelectionSnapshot — verifies the snapshot data structure
// can be constructed programmatically for headless use.

#include <gtest/gtest.h>
#include "common/CSelectionSnapshot.h"

TEST(CSelectionSnapshot, DefaultConstruction)
{
    CSelectionSnapshot snap;
    EXPECT_TRUE(snap.SourcePath.empty());
    EXPECT_TRUE(snap.SourcePathW.empty());
    EXPECT_TRUE(snap.Items.empty());
    EXPECT_EQ(snap.Action, EActionType::Copy);
    EXPECT_FALSE(snap.UseRecycleBin);
    EXPECT_FALSE(snap.OverwriteOlder);
    EXPECT_EQ(snap.SpeedLimit, 0u);
    EXPECT_EQ(snap.FileCount(), 0);
    EXPECT_EQ(snap.DirCount(), 0);
}

TEST(CSelectionSnapshot, AddFilesAndDirs)
{
    CSelectionSnapshot snap;
    snap.SourcePath = "C:\\Projects";
    snap.SourcePathW = L"C:\\Projects";
    snap.Action = EActionType::Delete;

    CSnapshotItem file1;
    file1.Name = "readme.txt";
    file1.NameW = L"readme.txt";
    file1.IsDir = false;
    file1.Size = 1024;
    file1.Attr = FILE_ATTRIBUTE_NORMAL;
    memset(&file1.LastWrite, 0, sizeof(file1.LastWrite));
    snap.Items.push_back(file1);

    CSnapshotItem dir1;
    dir1.Name = "subdir";
    dir1.NameW = L"subdir";
    dir1.IsDir = true;
    dir1.Size = 0;
    dir1.Attr = FILE_ATTRIBUTE_DIRECTORY;
    memset(&dir1.LastWrite, 0, sizeof(dir1.LastWrite));
    snap.Items.push_back(dir1);

    EXPECT_EQ(snap.Items.size(), 2u);
    EXPECT_EQ(snap.FileCount(), 1);
    EXPECT_EQ(snap.DirCount(), 1);
}

TEST(CSelectionSnapshot, CopyOperation)
{
    CSelectionSnapshot snap;
    snap.SourcePath = "C:\\Source";
    snap.TargetPath = "D:\\Target";
    snap.Action = EActionType::Copy;
    snap.Mask = "*.*";
    snap.OverwriteOlder = true;
    snap.CopySecurity = true;
    snap.PreserveDirTime = true;

    CSnapshotItem file;
    file.Name = "data.bin";
    file.IsDir = false;
    file.Size = 1048576; // 1 MB
    file.Attr = FILE_ATTRIBUTE_ARCHIVE;
    memset(&file.LastWrite, 0, sizeof(file.LastWrite));
    snap.Items.push_back(file);

    EXPECT_EQ(snap.FileCount(), 1);
    EXPECT_EQ(snap.DirCount(), 0);
    EXPECT_TRUE(snap.OverwriteOlder);
    EXPECT_EQ(snap.Mask, "*.*");
}

TEST(CSelectionSnapshot, UnicodeItems)
{
    CSelectionSnapshot snap;
    snap.SourcePathW = L"C:\\Проекты";
    snap.Action = EActionType::Move;

    CSnapshotItem item;
    item.Name = "???.txt"; // ANSI lossy
    item.NameW = L"\x6D4B\x8BD5.txt"; // Chinese characters
    item.IsDir = false;
    item.Size = 256;
    item.Attr = FILE_ATTRIBUTE_NORMAL;
    memset(&item.LastWrite, 0, sizeof(item.LastWrite));
    snap.Items.push_back(item);

    EXPECT_FALSE(snap.Items[0].NameW.empty());
    EXPECT_EQ(snap.Items[0].NameW, L"\x6D4B\x8BD5.txt");
}

TEST(CSelectionSnapshot, ChangeAttrsData)
{
    CSelectionSnapshot snap;
    snap.Action = EActionType::ChangeAttrs;
    snap.AttrsData.AttrAnd = ~(DWORD)FILE_ATTRIBUTE_READONLY;
    snap.AttrsData.AttrOr = FILE_ATTRIBUTE_ARCHIVE;
    snap.AttrsData.SubDirs = TRUE;
    snap.AttrsData.ChangeCompression = FALSE;
    snap.AttrsData.ChangeEncryption = FALSE;

    CSnapshotItem file;
    file.Name = "test.doc";
    file.IsDir = false;
    file.Size = 4096;
    file.Attr = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE;
    memset(&file.LastWrite, 0, sizeof(file.LastWrite));
    snap.Items.push_back(file);

    DWORD newAttrs = (file.Attr & snap.AttrsData.AttrAnd) | snap.AttrsData.AttrOr;
    EXPECT_EQ(newAttrs, (DWORD)FILE_ATTRIBUTE_ARCHIVE);
}

TEST(CSelectionSnapshot, MoveToVector)
{
    // Verify snapshot is moveable (no raw pointers to manage)
    CSelectionSnapshot snap;
    snap.SourcePath = "C:\\Test";
    snap.Action = EActionType::Delete;

    CSnapshotItem item;
    item.Name = "file.txt";
    item.IsDir = false;
    item.Size = 100;
    item.Attr = 0;
    memset(&item.LastWrite, 0, sizeof(item.LastWrite));
    snap.Items.push_back(item);

    std::vector<CSelectionSnapshot> queue;
    queue.push_back(std::move(snap));

    EXPECT_EQ(queue[0].SourcePath, "C:\\Test");
    EXPECT_EQ(queue[0].Items.size(), 1u);
}

TEST(CSelectionSnapshot, LargeSelection)
{
    CSelectionSnapshot snap;
    snap.SourcePath = "C:\\BigDir";
    snap.Action = EActionType::Copy;
    snap.TargetPath = "D:\\Backup";

    for (int i = 0; i < 10000; i++)
    {
        CSnapshotItem item;
        item.Name = "file_" + std::to_string(i) + ".dat";
        item.IsDir = (i % 10 == 0);
        item.Size = i * 1024ULL;
        item.Attr = item.IsDir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        memset(&item.LastWrite, 0, sizeof(item.LastWrite));
        snap.Items.push_back(item);
    }

    EXPECT_EQ(snap.Items.size(), 10000u);
    EXPECT_EQ(snap.FileCount(), 9000);
    EXPECT_EQ(snap.DirCount(), 1000);
}

// Test that snapshot can produce a delete operation list
TEST(CSelectionSnapshot, DeleteOperationList)
{
    CSelectionSnapshot snap;
    snap.SourcePath = "C:\\TestDir";
    snap.Action = EActionType::Delete;
    snap.UseRecycleBin = false;

    // Add 3 files and 1 directory
    for (int i = 0; i < 3; i++)
    {
        CSnapshotItem item;
        item.Name = "file" + std::to_string(i) + ".txt";
        item.IsDir = false;
        item.Size = 1000 * (i + 1);
        item.Attr = FILE_ATTRIBUTE_NORMAL;
        memset(&item.LastWrite, 0, sizeof(item.LastWrite));
        snap.Items.push_back(item);
    }
    CSnapshotItem dir;
    dir.Name = "subdir";
    dir.IsDir = true;
    dir.Size = 0;
    dir.Attr = FILE_ATTRIBUTE_DIRECTORY;
    memset(&dir.LastWrite, 0, sizeof(dir.LastWrite));
    snap.Items.push_back(dir);

    // Verify we can iterate and build paths
    for (const auto& item : snap.Items)
    {
        std::string fullPath = snap.SourcePath + "\\" + item.Name;
        EXPECT_FALSE(fullPath.empty());
        if (item.IsDir)
            EXPECT_NE(item.Attr & FILE_ATTRIBUTE_DIRECTORY, 0u);
    }
    EXPECT_EQ(snap.FileCount(), 3);
    EXPECT_EQ(snap.DirCount(), 1);
}

// Test copy snapshot with target path and mask
TEST(CSelectionSnapshot, CopyWithMask)
{
    CSelectionSnapshot snap;
    snap.SourcePath = "C:\\Source";
    snap.TargetPath = "D:\\Target";
    snap.Action = EActionType::Copy;
    snap.Mask = "*.txt";
    snap.OverwriteOlder = true;
    snap.CopyAttrs = true;
    snap.PreserveDirTime = true;
    snap.CopySecurity = false;

    CSnapshotItem f1;
    f1.Name = "readme.txt";
    f1.IsDir = false;
    f1.Size = 256;
    f1.Attr = FILE_ATTRIBUTE_ARCHIVE;
    memset(&f1.LastWrite, 0, sizeof(f1.LastWrite));
    snap.Items.push_back(f1);

    CSnapshotItem f2;
    f2.Name = "data.bin";
    f2.IsDir = false;
    f2.Size = 1048576;
    f2.Attr = FILE_ATTRIBUTE_NORMAL;
    memset(&f2.LastWrite, 0, sizeof(f2.LastWrite));
    snap.Items.push_back(f2);

    // Build target paths
    for (const auto& item : snap.Items)
    {
        std::string targetFile = snap.TargetPath + "\\" + item.Name;
        EXPECT_FALSE(targetFile.empty());
    }

    EXPECT_TRUE(snap.OverwriteOlder);
    EXPECT_TRUE(snap.CopyAttrs);
    EXPECT_FALSE(snap.CopySecurity);
}

// Test snapshot with wide paths for Unicode support
TEST(CSelectionSnapshot, WidePathConstruction)
{
    CSelectionSnapshot snap;
    snap.SourcePath = "C:\\Projects";
    snap.SourcePathW = L"C:\\Projects\\\x957F\x8DEF\x5F84\x6D4B\x8BD5";
    snap.TargetPath = "D:\\Backup";
    snap.TargetPathW = L"D:\\Backup\\\x957F\x8DEF\x5F84\x6D4B\x8BD5";
    snap.Action = EActionType::Move;

    CSnapshotItem item;
    item.Name = "???.txt"; // lossy ANSI
    item.NameW = L"\x6587\x4EF6.txt"; // Unicode original
    item.IsDir = false;
    item.Size = 100;
    item.Attr = FILE_ATTRIBUTE_NORMAL;
    memset(&item.LastWrite, 0, sizeof(item.LastWrite));
    snap.Items.push_back(item);

    // Wide path should be preferred when available
    for (const auto& it : snap.Items)
    {
        if (!it.NameW.empty())
        {
            std::wstring fullW = snap.SourcePathW + L"\\" + it.NameW;
            EXPECT_FALSE(fullW.empty());
        }
        else
        {
            std::string fullA = snap.SourcePath + "\\" + it.Name;
            EXPECT_FALSE(fullA.empty());
        }
    }
}

// Test snapshot with ConvertData
TEST(CSelectionSnapshot, ConvertDataPreservation)
{
    CSelectionSnapshot snap;
    snap.Action = EActionType::Convert;

    // Set up identity code table
    for (int i = 0; i < 256; i++)
        snap.ConvertData.CodeTable[i] = (char)i;
    snap.ConvertData.EOFType = 1;

    CSnapshotItem item;
    item.Name = "text.txt";
    item.IsDir = false;
    item.Size = 500;
    item.Attr = FILE_ATTRIBUTE_NORMAL;
    memset(&item.LastWrite, 0, sizeof(item.LastWrite));
    snap.Items.push_back(item);

    EXPECT_EQ(snap.ConvertData.EOFType, 1);
    EXPECT_EQ(snap.ConvertData.CodeTable[65], 'A');
}

// Test SpeedLimit options
TEST(CSelectionSnapshot, SpeedLimitOptions)
{
    CSelectionSnapshot snap;
    snap.Action = EActionType::Copy;
    snap.UseSpeedLimit = true;
    snap.SpeedLimit = 1048576; // 1 MB/s
    snap.StartOnIdle = true;

    EXPECT_TRUE(snap.UseSpeedLimit);
    EXPECT_EQ(snap.SpeedLimit, 1048576u);
    EXPECT_TRUE(snap.StartOnIdle);
}
