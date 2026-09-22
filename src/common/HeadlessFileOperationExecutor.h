// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// HeadlessFileOperationExecutor — UI-free file execution over IWorkerObserver.
//
// This is a small execution seam for private integration tests and future
// worker extraction. It intentionally covers file operations first; directory
// script execution can compose these primitives after the worker dispatch loop
// becomes linkable outside the UI binary.

#pragma once

#include "IFileSystem.h"
#include "IWorkerObserver.h"
#include "SnapshotOperationPlanner.h"
#include "lang/lang.rh"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace sally::operation_executor
{

inline IFileSystem* OpExecFs()
{
    IFileSystem* fs = gFileSystem;
    if (fs == NULL)
        fs = GetWin32FileSystem();
    return fs;
}


struct CFileOperationExecutionState
{
    bool OverwriteAll = false;
    bool SkipAllOverwrites = false;
    bool SkipAllErrors = false;
    bool SkipCompressionChanges = false;
    bool SkipEncryptionChanges = false;
    bool IgnoreAllADSReadErrors = false;
    bool IgnoreAllADSOpenErrors = false;
    bool SkipAllADSOpenErrors = false;
    bool SkipAllADSCopyErrors = false;
    bool EncryptSystemAll = false;
    bool SkipAllEncryptSystem = false;
    bool IgnoreAllCopySecurityErrors = false;
};

struct CFileOperationResult
{
    bool success = false;
    bool skipped = false;
    DWORD lastError = ERROR_SUCCESS;
};

struct CDirectoryEntry
{
    std::wstring NameW;
    bool IsDir = false;
    unsigned __int64 Size = 0;
    DWORD Attr = 0;
    FILETIME LastWrite = {};
};

inline CFileOperationResult SuccessResult(bool skipped = false)
{
    CFileOperationResult result;
    result.success = true;
    result.skipped = skipped;
    return result;
}

inline CFileOperationResult ErrorResult(DWORD error)
{
    CFileOperationResult result;
    result.success = false;
    result.lastError = error;
    return result;
}

inline bool ConfirmOverwrite(IWorkerObserver& observer,
                             const std::wstring& sourcePath,
                             const std::wstring& targetPath,
                             CFileOperationExecutionState& state)
{
    if (state.OverwriteAll)
        return true;
    if (state.SkipAllOverwrites)
        return false;

    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return false;

    int response = observer.AskOverwrite(sourcePath.c_str(), L"",
                                         targetPath.c_str(), L"");
    switch (response)
    {
    case IDB_ALL:
        state.OverwriteAll = true;
        [[fallthrough]];
    case IDYES:
        return true;
    case IDB_SKIPALL:
        state.SkipAllOverwrites = true;
        [[fallthrough]];
    case IDB_SKIP:
        return false;
    case IDCANCEL:
    default:
        return false;
    }
}

inline int AskFileError(IWorkerObserver& observer,
                        const wchar_t* title,
                        const std::wstring& path,
                        DWORD error)
{
    wchar_t errorText[64] = {};
    swprintf_s(errorText, L"Error code %lu", error);
    return observer.AskFileError(title, path.c_str(), errorText);
}

class CScopedReadonlyAttributeClear
{
public:
    CScopedReadonlyAttributeClear(const std::wstring& path, DWORD attrs)
        : Path(path),
          OriginalAttrs(attrs),
          Restore(false)
    {
        if (OriginalAttrs != INVALID_FILE_ATTRIBUTES &&
            (OriginalAttrs & FILE_ATTRIBUTE_READONLY) != 0)
        {
            Restore = OpExecFs()->SetFileAttributes(
                Path.c_str(), OriginalAttrs & ~FILE_ATTRIBUTE_READONLY).success;
        }
    }

    ~CScopedReadonlyAttributeClear()
    {
        if (Restore)
            OpExecFs()->SetFileAttributes(Path.c_str(), OriginalAttrs);
    }

    void Commit()
    {
        Restore = false;
    }

private:
    std::wstring Path;
    DWORD OriginalAttrs;
    bool Restore;
};

inline bool IsDotDirectoryName(const wchar_t* name)
{
    return name != nullptr &&
           name[0] == L'.' &&
           (name[1] == L'\0' ||
            (name[1] == L'.' && name[2] == L'\0'));
}

inline bool EnumerateDirectoryEntriesW(const std::wstring& dir,
                                       std::vector<CDirectoryEntry>& entries)
{
    entries.clear();
    std::wstring search = dir;
    if (!search.empty() && search.back() != L'\\' && search.back() != L'/')
        search.push_back(L'\\');
    search += L"*";

    WIN32_FIND_DATAW data = {};
    HANDLE find = OpExecFs()->FindFirstFile(search.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return false;

    do
    {
        if (IsDotDirectoryName(data.cFileName))
            continue;

        CDirectoryEntry entry;
        entry.NameW = data.cFileName;
        entry.IsDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry.Attr = data.dwFileAttributes;
        entry.LastWrite = data.ftLastWriteTime;
        entry.Size = ((unsigned __int64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        entries.push_back(entry);
    } while (OpExecFs()->FindNextFile(find, &data));

    DWORD error = GetLastError();
    OpExecFs()->CloseFind(find);
    if (error != ERROR_NO_MORE_FILES)
        return false;

    std::sort(entries.begin(), entries.end(),
              [](const CDirectoryEntry& left, const CDirectoryEntry& right) {
                  return _wcsicmp(left.NameW.c_str(), right.NameW.c_str()) < 0;
              });
    return true;
}

inline CFileOperationResult ExecuteCreateDirectoryW(IWorkerObserver& observer,
                                                    const std::wstring& targetPath,
                                                    CFileOperationExecutionState& state)
{
    while (true)
    {
        const FileResult createResult = OpExecFs()->CreateDirectory(targetPath.c_str());
        if (createResult.success)
            return SuccessResult();

        DWORD error = createResult.errorCode;
        if (error == ERROR_ALREADY_EXISTS)
        {
            DWORD attrs = OpExecFs()->GetFileAttributes(targetPath.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES &&
                (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                return SuccessResult();
            }
        }

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(error);
        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, L"Error creating directory", targetPath, error);
        switch (response)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            state.SkipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return SuccessResult(true);
        case IDCANCEL:
        default:
            return ErrorResult(error);
        }
    }
}

inline CFileOperationResult ExecuteRemoveDirectoryW(IWorkerObserver& observer,
                                                    const std::wstring& sourcePath,
                                                    CFileOperationExecutionState& state)
{
    while (true)
    {
        const FileResult removeResult = OpExecFs()->RemoveDirectory(sourcePath.c_str());
        if (removeResult.success)
            return SuccessResult();

        DWORD error = removeResult.errorCode;
        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(error);
        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, L"Error removing directory", sourcePath, error);
        switch (response)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            state.SkipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return SuccessResult(true);
        case IDCANCEL:
        default:
            return ErrorResult(error);
        }
    }
}

inline CFileOperationResult ExecuteCopyFileW(IWorkerObserver& observer,
                                             const std::wstring& sourcePath,
                                             const std::wstring& targetPath,
                                             CFileOperationExecutionState& state)
{
    DWORD targetAttrs = OpExecFs()->GetFileAttributes(targetPath.c_str());
    if (targetAttrs != INVALID_FILE_ATTRIBUTES)
    {
        if (!ConfirmOverwrite(observer, sourcePath, targetPath, state))
            return observer.IsCancelled() ? ErrorResult(ERROR_CANCELLED) : SuccessResult(true);
    }
    CScopedReadonlyAttributeClear targetReadonly(targetPath, targetAttrs);

    while (true)
    {
        const FileResult copyResult = OpExecFs()->CopyFile(
            sourcePath.c_str(), targetPath.c_str(), false);
        if (copyResult.success)
        {
            targetReadonly.Commit();
            return SuccessResult();
        }

        DWORD error = copyResult.errorCode;
        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(ERROR_CANCELLED);
        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, L"Error copying file", sourcePath, error);
        switch (response)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            state.SkipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return SuccessResult(true);
        case IDCANCEL:
        default:
            return ErrorResult(error);
        }
    }
}

inline CFileOperationResult ExecuteMoveFileW(IWorkerObserver& observer,
                                             const std::wstring& sourcePath,
                                             const std::wstring& targetPath,
                                             CFileOperationExecutionState& state)
{
    DWORD targetAttrs = OpExecFs()->GetFileAttributes(targetPath.c_str());
    const bool targetExists = targetAttrs != INVALID_FILE_ATTRIBUTES;
    if (targetExists)
    {
        if (!ConfirmOverwrite(observer, sourcePath, targetPath, state))
            return observer.IsCancelled() ? ErrorResult(ERROR_CANCELLED) : SuccessResult(true);
    }
    CScopedReadonlyAttributeClear targetReadonly(targetPath, targetAttrs);

    MoveFlags flags = MoveFlags::CopyAllowed;
    if (targetExists)
        flags = flags | MoveFlags::ReplaceExisting;

    while (true)
    {
        const FileResult moveResult = OpExecFs()->MoveFileWithFlags(
            sourcePath.c_str(), targetPath.c_str(), flags);
        if (moveResult.success)
        {
            targetReadonly.Commit();
            return SuccessResult();
        }

        DWORD error = moveResult.errorCode;
        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(ERROR_CANCELLED);
        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, L"Error moving file", sourcePath, error);
        switch (response)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            state.SkipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return SuccessResult(true);
        case IDCANCEL:
        default:
            return ErrorResult(error);
        }
    }
}

inline CFileOperationResult ExecuteDeleteFileW(IWorkerObserver& observer,
                                               const std::wstring& sourcePath,
                                               DWORD attrs,
                                               CFileOperationExecutionState& state)
{
    CScopedReadonlyAttributeClear sourceReadonly(sourcePath, attrs);

    while (true)
    {
        const FileResult deleteResult = OpExecFs()->DeleteFile(sourcePath.c_str());
        if (deleteResult.success)
        {
            sourceReadonly.Commit();
            return SuccessResult();
        }

        DWORD error = deleteResult.errorCode;
        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(ERROR_CANCELLED);
        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, L"Error deleting file", sourcePath, error);
        switch (response)
        {
        case IDRETRY:
            break;
        case IDB_SKIPALL:
            state.SkipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return SuccessResult(true);
        case IDCANCEL:
        default:
            return ErrorResult(error);
        }
    }
}

inline CFileOperationResult ExecutePlannedFileOperation(
    IWorkerObserver& observer,
    const sally::operation_planner::CPlannedSnapshotItem& plan,
    CFileOperationExecutionState& state)
{
    if (plan.IsDir)
        return ErrorResult(ERROR_INVALID_PARAMETER);

    switch (plan.Kind)
    {
    case sally::operation_planner::PlannedOperationKind::CopyFile:
        return ExecuteCopyFileW(observer, plan.SourcePathW, plan.TargetPathW, state);
    case sally::operation_planner::PlannedOperationKind::MoveFile:
        return ExecuteMoveFileW(observer, plan.SourcePathW, plan.TargetPathW, state);
    case sally::operation_planner::PlannedOperationKind::DeleteFile:
        return ExecuteDeleteFileW(observer, plan.SourcePathW, plan.Attr, state);
    default:
        return ErrorResult(ERROR_INVALID_PARAMETER);
    }
}

inline CFileOperationResult ExecutePlannedDirectoryOperation(
    IWorkerObserver& observer,
    const sally::operation_planner::CPlannedSnapshotItem& plan,
    CFileOperationExecutionState& state)
{
    using sally::operation_planner::PlannedOperationKind;
    if (!plan.IsDir)
        return ErrorResult(ERROR_INVALID_PARAMETER);

    const bool isCopy = plan.Kind == PlannedOperationKind::CopyDirectory;
    const bool isMove = plan.Kind == PlannedOperationKind::MoveDirectory;
    const bool isDelete = plan.Kind == PlannedOperationKind::DeleteDirectory;
    if (!isCopy && !isMove && !isDelete)
        return ErrorResult(ERROR_INVALID_PARAMETER);

    if (isCopy || isMove)
    {
        CFileOperationResult created = ExecuteCreateDirectoryW(observer, plan.TargetPathW, state);
        if (!created.success)
            return created;
        if (created.skipped)
            return created;
    }

    std::vector<CDirectoryEntry> entries;
    if (!EnumerateDirectoryEntriesW(plan.SourcePathW, entries))
    {
        DWORD error = GetLastError();
        if (error == ERROR_SUCCESS)
            error = ERROR_PATH_NOT_FOUND;
        if (state.SkipAllErrors)
            return SuccessResult(true);
        int response = AskFileError(observer, L"Error listing directory", plan.SourcePathW, error);
        switch (response)
        {
        case IDB_SKIPALL:
            state.SkipAllErrors = true;
            [[fallthrough]];
        case IDB_SKIP:
            return SuccessResult(true);
        case IDCANCEL:
        default:
            return ErrorResult(error);
        }
    }

    for (const CDirectoryEntry& entry : entries)
    {
        if (observer.IsCancelled())
            return ErrorResult(ERROR_CANCELLED);

        sally::operation_planner::CPlannedSnapshotItem child;
        if (!sally::operation_planner::TryPlanChildItem(
                plan.Action,
                plan.SourcePathW,
                plan.HasTarget() ? plan.TargetPathW : std::wstring(),
                entry.NameW, entry.NameW,
                entry.IsDir, entry.Size, entry.Attr, entry.LastWrite,
                child))
        {
            return ErrorResult(ERROR_INVALID_PARAMETER);
        }

        CFileOperationResult result = child.IsDir
                                          ? ExecutePlannedDirectoryOperation(observer, child, state)
                                          : ExecutePlannedFileOperation(observer, child, state);
        if (!result.success)
            return result;
    }

    if (isMove || isDelete)
        return ExecuteRemoveDirectoryW(observer, plan.SourcePathW, state);

    return SuccessResult();
}

inline CFileOperationResult ExecutePlannedOperation(
    IWorkerObserver& observer,
    const sally::operation_planner::CPlannedSnapshotItem& plan,
    CFileOperationExecutionState& state)
{
    return plan.IsDir
               ? ExecutePlannedDirectoryOperation(observer, plan, state)
               : ExecutePlannedFileOperation(observer, plan, state);
}

} // namespace sally::operation_executor
