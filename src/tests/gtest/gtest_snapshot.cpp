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
