// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// CSelectionSnapshot — immutable capture of panel selection state.
//
// Replaces direct CFilesWindow member access during script building.
// Can be constructed from a panel (CFilesWindow::TakeSnapshot) or
// programmatically for headless/test use. Once created, the snapshot
// is independent of the panel — the panel can refresh without
// affecting queued operations.

#pragma once

#include <windows.h>
#include <string>
#include <vector>

// Action types for file operations.
// Matches CActionType in fileswnd.h — duplicated here so common/ code
// and tests can use it without pulling in the full panel header.
enum class EActionType
{
    Copy,
    Move,
    Delete,
    CountSize,
    ChangeAttrs,
    ChangeCase,
    RecursiveConvert,
    Convert,
};

// Per-item data captured from the panel's Files/Dirs arrays.
struct CSnapshotItem
{
    std::string Name;       // filename (ANSI)
    std::wstring NameW;     // filename (Unicode, empty if same as ANSI)
    std::string DosName;    // DOS 8.3 name (empty if none)
    bool IsDir;             // true for directories
    unsigned __int64 Size;  // file size in bytes (0 for dirs unless counted)
    DWORD Attr;             // FILE_ATTRIBUTE_* flags
    FILETIME LastWrite;     // last write time
};

// Attribute change parameters (matches CAttrsData in fileswnd.h).
struct CSnapshotAttrsData
{
    DWORD AttrAnd;          // bits to clear (AND mask)
    DWORD AttrOr;           // bits to set (OR mask)
    BOOL SubDirs;           // include subdirectories
    BOOL ChangeCompression; // change NTFS compression
    BOOL ChangeEncryption;  // change NTFS encryption
};

// Case change parameters (matches CChangeCaseData in fileswnd.h).
struct CSnapshotChangeCaseData
{
    int FileNameFormat;     // format code for AlterFileName
    int Change;             // which part of name to change
    BOOL SubDirs;           // include subdirectories
};

// Convert parameters (matches CConvertData in worker.h).
struct CSnapshotConvertData
{
    char CodeTable[256];    // character mapping table
    int EOFType;            // end-of-file type
};

// Immutable selection snapshot — everything needed to build a COperations script.
struct CSelectionSnapshot
{
    // --- Source ---
    std::string SourcePath;     // current panel directory (ANSI)
    std::wstring SourcePathW;   // current panel directory (Unicode)

    // --- Selected items ---
    std::vector<CSnapshotItem> Items; // selected files and directories

    // --- Action ---
    EActionType Action;

    // --- Target (copy/move only) ---
    std::string TargetPath;     // destination directory
    std::wstring TargetPathW;   // destination directory (Unicode)
    std::string Mask;           // file mask for target name mapping (e.g. "*.*")

    // --- Options ---
    bool UseRecycleBin;         // delete to recycle bin
    bool InvertRecycleBin;      // Shift held — invert recycle bin setting
    bool OverwriteOlder;        // overwrite only older files
    bool CopySecurity;          // preserve NTFS permissions
    bool CopyAttrs;             // preserve Archive/Encrypt/Compress
    bool PreserveDirTime;       // preserve directory timestamps
    bool IgnoreADS;             // skip alternate data streams
    bool SkipEmptyDirs;         // skip empty directories during copy
    bool StartOnIdle;           // start only when system is idle
    bool UseSpeedLimit;         // enable speed limiting
    DWORD SpeedLimit;           // speed limit in bytes/sec

    // --- Operation-specific data ---
    CSnapshotAttrsData AttrsData;           // for ChangeAttrs
    CSnapshotChangeCaseData ChangeCaseData; // for ChangeCase
    CSnapshotConvertData ConvertData;       // for Convert

    CSelectionSnapshot()
        : Action(EActionType::Copy)
        , UseRecycleBin(false)
        , InvertRecycleBin(false)
        , OverwriteOlder(false)
        , CopySecurity(false)
        , CopyAttrs(false)
        , PreserveDirTime(false)
        , IgnoreADS(false)
        , SkipEmptyDirs(false)
        , StartOnIdle(false)
        , UseSpeedLimit(false)
        , SpeedLimit(0)
        , AttrsData{}
        , ChangeCaseData{}
        , ConvertData{}
    {
    }

    // --- Convenience ---
    int FileCount() const
    {
        int n = 0;
        for (const auto& item : Items)
            if (!item.IsDir)
                n++;
        return n;
    }

    int DirCount() const
    {
        int n = 0;
        for (const auto& item : Items)
            if (item.IsDir)
                n++;
        return n;
    }
};
