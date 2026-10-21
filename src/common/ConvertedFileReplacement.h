// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "IFileSystem.h"

namespace sally::conversion
{
    struct FileMetadata
    {
        FILETIME Creation = {}, Access = {}, Write = {};
        DWORD Attributes = FILE_ATTRIBUTE_NORMAL;
        std::vector<BYTE> Security;
    };

    inline FileResult CaptureMetadata(IFileSystem& files, HANDLE source, const std::wstring& path, FileMetadata& metadata)
    {
        FileInfo info;
        const auto attributes = files.GetFileInfo(path.c_str(), info);
        if (!attributes.success)
            return attributes;
        metadata.Attributes = info.attributes;
        const auto times = files.GetHandleFileTime(source, &metadata.Creation, &metadata.Access, &metadata.Write);
        if (!times.success)
            return times;
        VolumeCapabilities volume = {};
        const auto queried = files.QueryVolumeCapabilities(path.c_str(), volume);
        if (!queried.success)
            return queried;
        metadata.Security.clear();
        return volume.SupportsACLs() ? files.GetPathSecurity(path.c_str(), metadata.Security) : FileResult::Ok();
    }

    inline FileResult FinalizeContents(IFileSystem& files, HANDLE converted, const FileMetadata& metadata)
    {
        const auto flushed = files.FlushHandle(converted);
        if (!flushed.success)
            return flushed;
        return files.SetHandleFileTime(converted, &metadata.Creation, &metadata.Access, &metadata.Write);
    }

    inline FileResult RestoreMetadata(IFileSystem& files, const std::wstring& path, const FileMetadata& metadata)
    {
        const auto times = files.SetPathFileTime(path.c_str(), &metadata.Creation, &metadata.Access, &metadata.Write);
        const auto attributes = files.SetFileAttributes(path.c_str(), metadata.Attributes);
        return !times.success ? times : attributes;
    }

    inline DWORD WritableAttributes(DWORD attributes)
    {
        attributes &= ~FILE_ATTRIBUTE_READONLY;
        return attributes != 0 ? attributes : FILE_ATTRIBUTE_NORMAL;
    }

    // A newly created sibling directory reserves the backup namespace. Never
    // overwrite an unrelated existing backup, and never recursively clean it.
    class BackupReservation
    {
        IFileSystem& Files;
        std::wstring Directory, Backup;
        bool Owned = false;
        bool ContainsOriginal = false;

    public:
        explicit BackupReservation(IFileSystem& files) : Files(files) {}
        BackupReservation(const BackupReservation&) = delete;
        BackupReservation& operator=(const BackupReservation&) = delete;
        FileResult Reserve(const std::wstring& converted)
        {
            if (Owned)
                return FileResult::Ok();
            for (unsigned attempt = 0; attempt < 256; ++attempt)
            {
                Directory = converted + L".backup-" + std::to_wstring(attempt);
                const auto created = Files.CreateDirectory(Directory.c_str());
                if (created.success)
                {
                    Backup = Directory + L"\\original";
                    Owned = true;
                    return created;
                }
                if (created.errorCode != ERROR_ALREADY_EXISTS && created.errorCode != ERROR_FILE_EXISTS)
                    return created;
            }
            return FileResult::Error(ERROR_ALREADY_EXISTS);
        }
        const std::wstring& Path() const { return Backup; }
        const std::wstring& DirectoryPath() const { return Directory; }
        bool IsReserved() const { return Owned; }
        void RecordOriginal(bool present) { ContainsOriginal = present; }

        // The caller must have committed or restored the original first. In
        // recovery-required states the backup can be its only original copy.
        FileResult Cleanup(bool originalIsSafe)
        {
            if (!Owned)
                return FileResult::Ok();
            if (!originalIsSafe)
                return FileResult::Error(ERROR_INVALID_STATE);
            FileInfo info;
            const auto found = Files.GetFileInfo(Backup.c_str(), info);
            if (found.success)
            {
                if (!ContainsOriginal)
                    return FileResult::Error(ERROR_FILE_EXISTS);
                if ((info.attributes & FILE_ATTRIBUTE_READONLY) != 0)
                {
                    const auto writable = Files.SetFileAttributes(Backup.c_str(), WritableAttributes(info.attributes));
                    if (!writable.success)
                        return writable;
                }
                const auto removed = Files.DeleteFile(Backup.c_str());
                if (!removed.success)
                    return removed;
                ContainsOriginal = false;
            }
            else if (found.errorCode != ERROR_FILE_NOT_FOUND && found.errorCode != ERROR_PATH_NOT_FOUND)
                return found;
            const auto removed = Files.RemoveDirectory(Directory.c_str());
            if (removed.success)
                Owned = false;
            return removed;
        }
    };

    enum class ReplacementState
    {
        Unchanged,
        Committed,
        RecoveryRequired
    };
    struct ReplacementResult
    {
        ReplacementState State = ReplacementState::Unchanged;
        DWORD Error = ERROR_SUCCESS;
        DWORD MetadataError = ERROR_SUCCESS;
    };

    inline ReplacementResult AttemptReplacement(IFileSystem& files, const std::wstring& original,
                                                const std::wstring& converted, BackupReservation& backup,
                                                const FileMetadata& metadata)
    {
        if (!backup.IsReserved())
            return {ReplacementState::Unchanged, ERROR_INVALID_STATE};
        FileInfo existingBackup;
        const auto found = files.GetFileInfo(backup.Path().c_str(), existingBackup);
        if (found.success)
            return {ReplacementState::Unchanged, ERROR_FILE_EXISTS};
        if (found.errorCode != ERROR_FILE_NOT_FOUND && found.errorCode != ERROR_PATH_NOT_FOUND)
            return {ReplacementState::Unchanged, found.errorCode};
        if (!metadata.Security.empty())
        {
            // ReplaceFile preserves the DACL, but does not promise the original
            // owner/group. Prepare the complete descriptor before committing.
            const auto secured = files.SetPathSecurity(converted.c_str(), metadata.Security.data(), metadata.Security.size());
            if (!secured.success)
                return {ReplacementState::Unchanged, secured.errorCode};
        }
        const auto attributes = files.SetFileAttributes(converted.c_str(), WritableAttributes(metadata.Attributes));
        if (!attributes.success)
            return {ReplacementState::Unchanged, attributes.errorCode};
        const bool readOnly = (metadata.Attributes & FILE_ATTRIBUTE_READONLY) != 0;
        if (readOnly)
        {
            const auto writable = files.SetFileAttributes(original.c_str(), WritableAttributes(metadata.Attributes));
            if (!writable.success)
                return {ReplacementState::Unchanged, writable.errorCode};
        }
        const auto replaced = files.ReplaceFileWithBackup(original.c_str(), converted.c_str(), backup.Path().c_str());
        if (replaced.success)
        {
            backup.RecordOriginal(true);
            const auto restored = RestoreMetadata(files, original, metadata);
            return {ReplacementState::Committed, ERROR_SUCCESS, restored.errorCode};
        }
        const bool recovery = replaced.errorCode == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;
        if (recovery)
            backup.RecordOriginal(true);
        DWORD metadataError = ERROR_SUCCESS;
        if (readOnly)
        {
            const auto restored = files.SetFileAttributes(recovery ? backup.Path().c_str() : original.c_str(), metadata.Attributes);
            if (!restored.success)
                metadataError = restored.errorCode;
        }
        return {recovery ? ReplacementState::RecoveryRequired : ReplacementState::Unchanged,
                replaced.errorCode, metadataError};
    }

    inline ReplacementResult RecoverOriginal(IFileSystem& files, const std::wstring& original,
                                             BackupReservation& backup, const FileMetadata& metadata)
    {
        // No overwrite flag: an unexpected file at the original name must never
        // be destroyed to recover the retained original.
        const auto restored = files.MoveFile(backup.Path().c_str(), original.c_str());
        if (!restored.success)
            return {ReplacementState::RecoveryRequired, restored.errorCode};
        backup.RecordOriginal(false);
        const auto metadataResult = RestoreMetadata(files, original, metadata);
        return {ReplacementState::Unchanged, ERROR_SUCCESS, metadataResult.errorCode};
    }

    enum class Decision
    {
        Retry,
        Skip,
        Cancel
    };
    struct CompletionResult
    {
        bool Converted = false;
        bool Continue = false;
        DWORD Error = ERROR_SUCCESS;
    };

    // ask(source, destination, error, recovery) is presentation-only. Recovery
    // errors must not be hidden by a previous Skip All or cancellation request.
    // notify(path, error) reports files retained after a cleanup/metadata error.
    template <class Ask, class Notify>
    CompletionResult CompleteReplacement(IFileSystem& files, const std::wstring& original,
                                         const std::wstring& converted, const FileMetadata& metadata, Ask ask, Notify notify)
    {
        BackupReservation backup(files);
        ReplacementState state = ReplacementState::Unchanged;
        bool restoreOriginalMetadata = false;
        for (;;)
        {
            DWORD error = ERROR_SUCCESS;
            std::wstring errorPath = converted;
            if (state == ReplacementState::RecoveryRequired)
            {
                const auto recovery = RecoverOriginal(files, original, backup, metadata);
                state = recovery.State;
                error = recovery.Error;
                restoreOriginalMetadata = recovery.MetadataError != ERROR_SUCCESS;
                if (error == ERROR_SUCCESS)
                    error = recovery.MetadataError;
                errorPath = state == ReplacementState::RecoveryRequired ? backup.Path() : original;
            }
            if (error == ERROR_SUCCESS && restoreOriginalMetadata)
            {
                error = RestoreMetadata(files, original, metadata).errorCode;
                restoreOriginalMetadata = error != ERROR_SUCCESS;
                errorPath = original;
            }
            if (error == ERROR_SUCCESS && state == ReplacementState::Unchanged)
            {
                const auto reserved = backup.Reserve(converted);
                if (!reserved.success)
                    error = reserved.errorCode;
                else
                {
                    auto replacement = AttemptReplacement(files, original, converted, backup, metadata);
                    state = replacement.State;
                    error = replacement.Error;
                    restoreOriginalMetadata = state == ReplacementState::Unchanged && replacement.MetadataError != ERROR_SUCCESS;
                    if (state == ReplacementState::RecoveryRequired)
                    {
                        // Restore the old name immediately, before offering Skip
                        // or Cancel. A retry after failed recovery only recovers.
                        const auto recovered = RecoverOriginal(files, original, backup, metadata);
                        state = recovered.State;
                        restoreOriginalMetadata = recovered.MetadataError != ERROR_SUCCESS;
                        if (state == ReplacementState::RecoveryRequired)
                        {
                            error = recovered.Error;
                            errorPath = backup.Path();
                        }
                    }
                    if (replacement.MetadataError != ERROR_SUCCESS && error == ERROR_SUCCESS)
                    {
                        error = replacement.MetadataError;
                        errorPath = original;
                    }
                }
            }
            if (error == ERROR_SUCCESS && state == ReplacementState::Committed)
            {
                // This branch is also the retry path after a committed result:
                // never convert or replace the new content a second time.
                error = RestoreMetadata(files, original, metadata).errorCode;
                errorPath = original;
                if (error == ERROR_SUCCESS)
                {
                    const auto cleaned = backup.Cleanup(true);
                    if (cleaned.success)
                        return {true, true, ERROR_SUCCESS};
                    error = cleaned.errorCode;
                    errorPath = backup.Path();
                }
            }
            const bool recoveryRequired = state == ReplacementState::RecoveryRequired;
            const Decision decision = ask(errorPath, original, error, recoveryRequired);
            if (decision == Decision::Retry)
                continue;
            if (recoveryRequired || state == ReplacementState::Committed)
            {
                // A retained backup is preferable to deleting the only original
                // or silently discarding it after an incomplete metadata restore.
                notify(backup.Path(), error);
                return {false, !recoveryRequired && decision == Decision::Skip, error};
            }
            const auto cleanup = backup.Cleanup(true);
            if (!cleanup.success)
                notify(backup.Path(), cleanup.errorCode);
            const auto writable = files.SetFileAttributes(converted.c_str(), FILE_ATTRIBUTE_NORMAL);
            const auto removed = writable.success ? files.DeleteFile(converted.c_str()) : writable;
            if (!removed.success)
                notify(converted, removed.errorCode);
            if (restoreOriginalMetadata)
                notify(original, error);
            return {false, decision == Decision::Skip, error};
        }
    }
} // namespace sally::conversion
