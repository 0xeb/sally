// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "ftp_diskwork_core.h"

CFTPDiskWork::CFTPDiskWork()
{
    Reset();
}

void CFTPDiskWork::Reset() noexcept
{
    SocketMsg = 0;
    SocketUID = 0;
    MsgID = 0;

    Type = fdwtNone;

    Path.clear();
    Name.clear();
    DirectFileName.clear();

    ForceAction = fqiaNone;
    AlreadyRenamedName = FALSE;

    CannotCreateDir = 0;
    DirAlreadyExists = 0;
    CannotCreateFile = 0;
    FileAlreadyExists = 0;
    RetryOnCreatedFile = 0;
    RetryOnResumedFile = 0;

    CheckFromOffset.Set(0, 0);
    WriteOrReadFromOffset.Set(0, 0);
    FlushDataBuffer = NULL;
    ValidBytesInFlushDataBuffer = 0;
    EOLsInFlushDataBuffer = 0;
    WorkFile = NULL;

    ProblemID = ITEMPR_OK;
    WinError = NO_ERROR;
    State = sqisNone;
    NewTgtName = NULL;
    OpenedFile = NULL;
    FileSize.Set(0, 0);
    CanOverwrite = FALSE;
    CanDeleteEmptyFile = FALSE;
    DiskListing = NULL;
}

void CFTPDiskWork::CopyScalarStateFrom(const CFTPDiskWork& work) noexcept
{
    SocketMsg = work.SocketMsg;
    SocketUID = work.SocketUID;
    MsgID = work.MsgID;
    Type = work.Type;
    ForceAction = work.ForceAction;
    AlreadyRenamedName = work.AlreadyRenamedName;
    CannotCreateDir = work.CannotCreateDir;
    DirAlreadyExists = work.DirAlreadyExists;
    CannotCreateFile = work.CannotCreateFile;
    FileAlreadyExists = work.FileAlreadyExists;
    RetryOnCreatedFile = work.RetryOnCreatedFile;
    RetryOnResumedFile = work.RetryOnResumedFile;
    CheckFromOffset = work.CheckFromOffset;
    WriteOrReadFromOffset = work.WriteOrReadFromOffset;
    FlushDataBuffer = work.FlushDataBuffer;
    ValidBytesInFlushDataBuffer = work.ValidBytesInFlushDataBuffer;
    EOLsInFlushDataBuffer = work.EOLsInFlushDataBuffer;
    WorkFile = work.WorkFile;
    ProblemID = work.ProblemID;
    WinError = work.WinError;
    State = work.State;
    NewTgtName = work.NewTgtName;
    OpenedFile = work.OpenedFile;
    FileSize = work.FileSize;
    CanOverwrite = work.CanOverwrite;
    CanDeleteEmptyFile = work.CanDeleteEmptyFile;
    DiskListing = work.DiskListing;
}

BOOL CFTPDiskWork::CopyFrom(const CFTPDiskWork& work) noexcept
{
    if (this == &work)
        return TRUE;
    try
    {
        std::wstring stagedPath(work.Path);
        std::wstring stagedName(work.Name);
        std::wstring stagedDirectFileName(work.DirectFileName);
        CopyScalarStateFrom(work);
        Path.swap(stagedPath);
        Name.swap(stagedName);
        DirectFileName.swap(stagedDirectFileName);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

void CFTPDiskWork::MoveFrom(CFTPDiskWork& work) noexcept
{
    if (this == &work)
        return;
    CopyScalarStateFrom(work);
    Path.swap(work.Path);
    Name.swap(work.Name);
    DirectFileName.swap(work.DirectFileName);
    work.NewTgtName = NULL;
    work.OpenedFile = NULL;
    work.DiskListing = NULL;
}

BOOL FTPPrepareCreateAndWriteFileDiskWork(CFTPDiskWork& work, int socketMsg, int socketUID,
                                          DWORD msgID, const wchar_t* targetFileName,
                                          HANDLE workFile, char* flushDataBuffer,
                                          int validBytesInFlushDataBuffer) noexcept
{
    std::wstring stagedFileName;
    try
    {
        stagedFileName = targetFileName != NULL ? targetFileName : L"";
    }
    catch (...)
    {
        // The caller has already detached this buffer from the data connection.
        // Publish it even when path staging fails so the existing failure cleanup
        // remains its sole owner.
        work.FlushDataBuffer = flushDataBuffer;
        return FALSE;
    }
    work.SocketMsg = socketMsg;
    work.SocketUID = socketUID;
    work.MsgID = msgID;
    work.Type = fdwtCreateAndWriteFile;

    work.Path.clear();
    work.Name.clear();
    work.DirectFileName.swap(stagedFileName);

    work.ForceAction = fqiaNone;
    work.AlreadyRenamedName = FALSE;
    work.CannotCreateDir = 0;
    work.DirAlreadyExists = 0;
    work.CannotCreateFile = 0;
    work.FileAlreadyExists = 0;
    work.RetryOnCreatedFile = 0;
    work.RetryOnResumedFile = 0;

    work.CheckFromOffset.Set(0, 0);
    work.WriteOrReadFromOffset.Set(0, 0);
    work.FlushDataBuffer = flushDataBuffer;
    work.ValidBytesInFlushDataBuffer = validBytesInFlushDataBuffer;
    work.EOLsInFlushDataBuffer = 0;
    work.WorkFile = workFile;

    work.ProblemID = ITEMPR_OK;
    work.WinError = NO_ERROR;
    work.State = sqisNone;
    work.NewTgtName = NULL;
    work.OpenedFile = NULL;
    work.FileSize.Set(0, 0);
    work.CanOverwrite = FALSE;
    work.CanDeleteEmptyFile = FALSE;
    work.DiskListing = NULL;
    return TRUE;
}

void FTPExecuteCreateAndWriteFileDiskWork(CFTPDiskWork& localWork, BOOL& needCopyBack,
                                          BOOL& workDone)
{
    HANDLE file = NULL;
    if (localWork.WorkFile == NULL) // the file has not been created yet
    {
        SetFileAttributesW(localWork.DirectFileName.c_str(), FILE_ATTRIBUTE_NORMAL); // to allow overwriting a read-only file as well
        HANDLE f = CreateFileW(localWork.DirectFileName.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS,
                              FILE_FLAG_SEQUENTIAL_SCAN,
                              NULL);
        if (f != INVALID_HANDLE_VALUE)
        {
            file = f;
            localWork.OpenedFile = f;
            workDone = TRUE;     // if cancelled, close the file handle and delete the file
            needCopyBack = TRUE; // return the handle of the created file
        }
        else // error while creating the file
        {
            localWork.State = sqisFailed;
            localWork.WinError = GetLastError();
            needCopyBack = TRUE; // return the error
        }
    }
    else
        file = localWork.WorkFile; // write only

    if (file != NULL && localWork.ValidBytesInFlushDataBuffer > 0) // write to the file
    {
        DWORD writtenBytes;
        if (!WriteFile(file, localWork.FlushDataBuffer, localWork.ValidBytesInFlushDataBuffer,
                       &writtenBytes, NULL) ||
            writtenBytes != (DWORD)localWork.ValidBytesInFlushDataBuffer)
        {
            localWork.State = sqisFailed;
            localWork.WinError = GetLastError();
            needCopyBack = TRUE; // return the error
        }
        // else;  // successfully written, we are successfully done
    }
}
