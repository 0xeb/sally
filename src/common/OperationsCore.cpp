// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// OperationsCore - linkable COperations/COperation pieces used by the
// standalone script builder and headless integration executors.

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include "worker.h"
#include "common/unicode/PathIdentityPolicy.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/unicode/helpers.h"

//
// ****************************************************************************
// CTransferSpeedMeter
//

CTransferSpeedMeter::CTransferSpeedMeter()
{
    Clear();
}

void CTransferSpeedMeter::Clear()
{
    ActIndexInTrBytes = 0;
    ActIndexInTrBytesTimeLim = 0;
    CountOfTrBytesItems = 0;
    ActIndexInLastPackets = 0;
    CountOfLastPackets = 0;
    ResetSpeed = TRUE;
    MaxPacketSize = 0;
}

//
// ****************************************************************************
// CProgressSpeedMeter
//

CProgressSpeedMeter::CProgressSpeedMeter()
{
    Clear();
}

void CProgressSpeedMeter::Clear()
{
    ActIndexInTrBytes = 0;
    ActIndexInTrBytesTimeLim = 0;
    CountOfTrBytesItems = 0;
    ActIndexInLastPackets = 0;
    CountOfLastPackets = 0;
    MaxPacketSize = 0;
}

static std::wstring BuildOperationNameW(std::wstring widePath, const std::wstring& wideFileName)
{
    if (!wideFileName.empty())
    {
        if (!widePath.empty() && widePath.back() != L'\\')
            widePath += L'\\';
        widePath += wideFileName;
    }

    // Operation state owns logical paths. Win32FileSystem prepares the literal
    // kernel form at the I/O boundary.
    return widePath;
}

void COperation::SetSourceNameW(const std::wstring& widePath, const std::wstring& wideFileName)
{
    SourceNameW = BuildOperationNameW(widePath, wideFileName);
}

void COperation::SetTargetNameW(const std::wstring& widePath, const std::wstring& wideFileName)
{
    TargetNameW = BuildOperationNameW(widePath, wideFileName);
}

BOOL COperation::AreSourceAndTargetExactlySamePath() const
{
    return SourceNameW == TargetNameW && !SourceNameW.empty();
}

// Case-insensitive comparison of source and target paths - uses wide paths if both available
BOOL COperation::AreSourceAndTargetSamePath() const
{
    return _wcsicmp(SourceNameW.c_str(), TargetNameW.c_str()) == 0 && !SourceNameW.empty();
}

#ifdef SALLY_WORKER_CORE_STANDALONE
static bool IsPathSlashW(wchar_t ch)
{
    return ch == L'\\' || ch == L'/';
}

static std::wstring StandaloneRootPathW(std::wstring path)
{
    if (path.length() >= 2 && path[1] == L':')
        return path.substr(0, 2);

    if (path.length() >= 2 && IsPathSlashW(path[0]) && IsPathSlashW(path[1]))
    {
        size_t serverEnd = path.find_first_of(L"\\/", 2);
        if (serverEnd == std::wstring::npos)
            return path;
        size_t shareEnd = path.find_first_of(L"\\/", serverEnd + 1);
        if (shareEnd == std::wstring::npos)
            return path;
        return path.substr(0, shareEnd);
    }

    size_t firstSlash = path.find_first_of(L"\\/");
    if (firstSlash == std::wstring::npos)
        return path;
    return path.substr(0, firstSlash);
}
#endif

BOOL COperation::HasSameRootPath() const
{
#ifdef SALLY_WORKER_CORE_STANDALONE
    if (SourceNameW.empty() || TargetNameW.empty())
        return FALSE;
    return _wcsicmp(StandaloneRootPathW(SourceNameW).c_str(),
                    StandaloneRootPathW(TargetNameW).c_str()) == 0;
#else
    return HasTheSameRootPath(SourceNameW.c_str(), TargetNameW.c_str());
#endif
}

//
// ****************************************************************************
// COperations
//

COperations::COperations(int base, int delta, const wchar_t* waitInQueueSubject,
                         const wchar_t* waitInQueueFrom, const wchar_t* waitInQueueTo) : Sizes(1, 400), Count(0)
{
    TotalSize = CQuadWord(0, 0);
    CompressedSize = CQuadWord(0, 0);
    OccupiedSpace = CQuadWord(0, 0);
    TotalFileSize = CQuadWord(0, 0);
    FreeSpace = CQuadWord(0, 0);
    BytesPerCluster = 0;
    ClearReadonlyMask = 0xFFFFFFFF;
    InvertRecycleBin = FALSE;
    CanUseRecycleBin = TRUE;
    SameRootButDiffVolume = FALSE;
    TargetPathSupADS = FALSE;
    //  TargetPathSupEFS = FALSE;
    IsCopyOrMoveOperation = FALSE;
    OverwriteOlder = FALSE;
    CopySecurity = FALSE;
    PreserveDirTime = FALSE;
    SourcePathIsNetwork = FALSE;
    CopyAttrs = FALSE;
    StartOnIdle = FALSE;
    ShowStatus = FALSE;
    IsCopyOperation = FALSE;
    FastMoveUsed = FALSE;
    ChangeSpeedLimit = FALSE;
    FilesCount = 0;
    DirsCount = 0;
    RemapNameFrom = NULL;
    RemapNameFromLen = 0;
    RemapNameTo = NULL;
    RemapNameToLen = 0;
    RemovableTgtDisk = FALSE;
    RemovableSrcDisk = FALSE;
    SkipAllCountSizeErrors = FALSE;
    WorkPath1InclSubDirs = FALSE;
    WorkPath2InclSubDirs = FALSE;
    WaitInQueueSubject = waitInQueueSubject ? waitInQueueSubject : L"";
    WaitInQueueFrom = waitInQueueFrom ? waitInQueueFrom : L"";
    WaitInQueueTo = waitInQueueTo ? waitInQueueTo : L"";
    HANDLES(InitializeCriticalSection(&StatusCS));
    TransferredFileSize = CQuadWord(0, 0);
    ProgressSize = CQuadWord(0, 0);
    UseSpeedLimit = FALSE;
    SpeedLimit = 1;
    SleepAfterWrite = -1;
    LastBufferLimit = 1;
    LastSetupTime = GetTickCount();
    BytesTrFromLastSetup = CQuadWord(0, 0);
    UseProgressBufferLimit = FALSE;
    ProgressBufferLimit = ASYNC_SLOW_COPY_BUF_SIZE;
    LastProgBufLimTestTime = GetTickCount() - 1000;
    LastFileBlockCount = 0;
    LastFileStartTime = GetTickCount();
}

void COperations::SetSpeedLimit(BOOL useSpeedLimit, DWORD speedLimit)
{
    HANDLES(EnterCriticalSection(&StatusCS));
    UseSpeedLimit = useSpeedLimit;
    SpeedLimit = speedLimit;
    HANDLES(LeaveCriticalSection(&StatusCS));
}

BOOL ShouldWarnNotEnoughSpaceForCopyMove(const COperations* script,
                                         const wchar_t* targetPath,
                                         CQuadWord* requiredSpace)
{
    if (requiredSpace != NULL)
        *requiredSpace = CQuadWord(0, 0);
    if (script == NULL || script->BytesPerCluster == 0)
        return FALSE;

#ifdef SALLY_WORKER_CORE_STANDALONE
    const BOOL targetIsSamba = FALSE;
#else
    const BOOL targetIsSamba = targetPath != NULL && IsSambaDrivePathW(targetPath);
#endif

    const BOOL occupiedSpTooBig =
        script->OccupiedSpace != CQuadWord(0, 0) &&
        script->OccupiedSpace > script->FreeSpace &&
        !targetIsSamba;
    const BOOL fileSizeTooBig = script->TotalFileSize > script->FreeSpace;
    if (!occupiedSpTooBig && !fileSizeTooBig)
        return FALSE;

    if (requiredSpace != NULL)
        *requiredSpace = occupiedSpTooBig ? script->OccupiedSpace : script->TotalFileSize;
    return TRUE;
}
