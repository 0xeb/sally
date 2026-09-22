// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include "worker.h"
#include "common/HeadlessScriptExecutor.h"
#include "common/IFileSystem.h"
#include "common/IFileEnumerator.h" // reparse-blob ops
#include "common/unicode/helpers.h"

#include <winioctl.h>

#include <algorithm>
#include <vector>

namespace sally::operation_executor
{

static IFileSystem* ExecutorFs()
{
    IFileSystem* fs = gFileSystem;
    if (fs == NULL)
        fs = GetWin32FileSystem();
    return fs;
}

static std::wstring OperationSourcePathW(const COperation& op)
{
    return op.SourceNameW; // the wide name IS the name
}

static std::wstring OperationTargetPathW(const COperation& op)
{
    return op.TargetNameW;
}

static bool ScriptOperationCountsForProgress(COperationCode opcode)
{
    return opcode != ocLabelForSkipOfCreateDir;
}

static CFileOperationResult ExecuteMoveDirectoryW(IWorkerObserver& observer,
                                                  const std::wstring& sourcePath,
                                                  const std::wstring& targetPath,
                                                  CFileOperationExecutionState& state)
{
    while (true)
    {
        const FileResult moveResult = ExecutorFs()->MoveFileWithFlags(
            sourcePath.c_str(), targetPath.c_str(), MoveFlags::CopyAllowed);
        if (moveResult.success)
            return SuccessResult();

        DWORD error = moveResult.errorCode;
        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(ERROR_CANCELLED);
        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, L"Error moving directory", sourcePath, error);
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

static CFileOperationResult ExecuteCopyDirectoryTimeW(IWorkerObserver& observer,
                                                      const std::wstring& targetPath,
                                                      FILETIME lastWrite,
                                                      CFileOperationExecutionState& state)
{
    while (true)
    {
        HANDLE directory = ExecutorFs()->CreateFile(targetPath.c_str(), FILE_WRITE_ATTRIBUTES,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (directory != INVALID_HANDLE_VALUE)
        {
            const FileResult timeResult = ExecutorFs()->SetHandleFileTime(
                directory, NULL, NULL, &lastWrite);
            const DWORD error = timeResult.errorCode;
            ExecutorFs()->CloseFileHandle(directory);
            if (timeResult.success)
                return SuccessResult();

            if (state.SkipAllErrors)
                return SuccessResult(true);
            int response = AskFileError(observer, L"Error setting directory time", targetPath, error);
            switch (response)
            {
            case IDRETRY:
                break;
            case IDB_SKIPALL:
                state.SkipAllErrors = true;
                [[fallthrough]];
            case IDB_SKIP:
            case IDB_IGNORE:
            case IDB_ALL:
                return SuccessResult(true);
            case IDCANCEL:
            default:
                return ErrorResult(error);
            }
        }
        else
        {
            DWORD error = GetLastError();
            observer.WaitIfSuspended();
            if (observer.IsCancelled())
                return ErrorResult(ERROR_CANCELLED);
            if (state.SkipAllErrors)
                return SuccessResult(true);

            int response = AskFileError(observer, L"Error opening directory", targetPath, error);
            switch (response)
            {
            case IDRETRY:
                break;
            case IDB_SKIPALL:
                state.SkipAllErrors = true;
                [[fallthrough]];
            case IDB_SKIP:
            case IDB_IGNORE:
            case IDB_ALL:
                return SuccessResult(true);
            case IDCANCEL:
            default:
                return ErrorResult(error);
            }
        }
    }
}

static std::wstring AttributeWritePathW(std::wstring path)
{
    if (!path.empty() && (path.back() <= L' ' || path.back() == L'.'))
        path.push_back(L'\\');
    return path;
}

static std::wstring ADSBasePathW(std::wstring path)
{
    if (path.length() > 3 && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    return path;
}

static std::string ADSErrorTextA(DWORD error)
{
    char buffer[64] = {};
    wsprintfA(buffer, "Error code %lu", error);
    return std::string(buffer);
}

static bool EnumerateADSStreamsW(const std::wstring& path,
                                 std::vector<std::wstring>& streamNames,
                                 DWORD& error)
{
    streamNames.clear();
    error = ERROR_SUCCESS;

    // through gFileEnumerator (mockable; long-path decorated).
    IFileEnumerator* enumerator = gFileEnumerator;
    HENUM find = enumerator->StartStreamEnum(ADSBasePathW(path).c_str());
    if (find == INVALID_HENUM)
    {
        error = GetLastError();
        if (error == ERROR_HANDLE_EOF || error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED)
        {
            error = ERROR_SUCCESS; // no streams / FAT-family: no ADS support
            return true;
        }
        return false;
    }

    StreamEnumEntry entry;
    EnumResult r;
    // Done() is {success=true, noMoreFiles=true} - testing .success alone
    // loops forever on EOF (and re-appends the last stream each round).
    while ((r = enumerator->NextStream(find, entry)).success && !r.noMoreFiles)
    {
        if (entry.name != L"::$DATA")
            streamNames.push_back(entry.name);
    }
    error = r.success ? ERROR_SUCCESS : r.errorCode;
    enumerator->EndStreamEnum(find);
    if (error == ERROR_HANDLE_EOF || error == ERROR_NO_MORE_FILES || error == ERROR_SUCCESS)
    {
        error = ERROR_SUCCESS;
        return true;
    }
    return false;
}

static CFileOperationResult HandleADSReadError(IWorkerObserver& observer,
                                               const std::wstring& sourcePath,
                                               const std::wstring& streamName,
                                               CFileOperationExecutionState& state)
{
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.IgnoreAllADSReadErrors)
        return SuccessResult();

    int response = observer.AskADSReadError(sourcePath.c_str(), streamName.c_str());
    switch (response)
    {
    case IDB_ALL:
    case IDB_IGNOREALL:
        state.IgnoreAllADSReadErrors = true;
        [[fallthrough]];
    case IDB_IGNORE:
        return SuccessResult();
    case IDB_SKIPALL:
        state.SkipAllADSCopyErrors = true;
        [[fallthrough]];
    case IDB_SKIP:
        return SuccessResult(true);
    case IDCANCEL:
    default:
        return ErrorResult(ERROR_CANCELLED);
    }
}

static CFileOperationResult HandleADSOpenError(IWorkerObserver& observer,
                                               const std::wstring& filePath,
                                               const std::wstring& streamName,
                                               DWORD error,
                                               CFileOperationExecutionState& state)
{
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.IgnoreAllADSOpenErrors)
        return SuccessResult();
    if (state.SkipAllADSOpenErrors)
        return SuccessResult(true);

    wchar_t errorText[64] = {};
    swprintf_s(errorText, L"Error code %lu", error);
    int response = observer.AskADSOpenError(filePath.c_str(), streamName.c_str(), errorText);
    switch (response)
    {
    case IDRETRY:
        return ErrorResult(ERROR_RETRY);
    case IDB_ALL:
    case IDB_IGNOREALL:
        state.IgnoreAllADSOpenErrors = true;
        [[fallthrough]];
    case IDB_IGNORE:
        return SuccessResult();
    case IDB_SKIPALL:
        state.SkipAllADSOpenErrors = true;
        [[fallthrough]];
    case IDB_SKIP:
        return SuccessResult(true);
    case IDCANCEL:
    default:
        return ErrorResult(error != ERROR_SUCCESS ? error : ERROR_CANCELLED);
    }
}

static CFileOperationResult HandleADSWriteError(IWorkerObserver& observer,
                                                const std::wstring& targetPath,
                                                DWORD error,
                                                CFileOperationExecutionState& state)
{
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.SkipAllADSCopyErrors)
        return SuccessResult(true);

    int response = AskFileError(observer, L"Error writing ADS", targetPath, error);
    switch (response)
    {
    case IDRETRY:
        return ErrorResult(ERROR_RETRY);
    case IDB_SKIPALL:
        state.SkipAllADSCopyErrors = true;
        [[fallthrough]];
    case IDB_SKIP:
        return SuccessResult(true);
    case IDCANCEL:
    default:
        return ErrorResult(error != ERROR_SUCCESS ? error : ERROR_CANCELLED);
    }
}

static void CloseADSHandle(HANDLE& handle)
{
    if (handle != INVALID_HANDLE_VALUE)
    {
        ExecutorFs()->CloseFileHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
}

static CFileOperationResult CopyOneADSStreamW(IWorkerObserver& observer,
                                              const std::wstring& sourcePath,
                                              const std::wstring& targetPath,
                                              const std::wstring& streamName,
                                              CFileOperationExecutionState& state)
{
    if (state.SkipAllADSCopyErrors)
        return SuccessResult(true);

    const std::wstring sourceStream = ADSBasePathW(sourcePath) + streamName;
    const std::wstring targetStream = ADSBasePathW(targetPath) + streamName;
    const DWORD streamFlags = FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_BACKUP_SEMANTICS;

    while (true)
    {
        HANDLE input = ExecutorFs()->CreateFile(sourceStream.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   NULL, OPEN_EXISTING, streamFlags, NULL);
        if (input == INVALID_HANDLE_VALUE)
        {
            CFileOperationResult openResult =
                HandleADSOpenError(observer, sourcePath, streamName, GetLastError(), state);
            if (!openResult.success && openResult.lastError == ERROR_RETRY)
                continue;
            return openResult;
        }

        HANDLE output = ExecutorFs()->CreateFile(targetStream.c_str(), GENERIC_WRITE, 0, NULL,
                                    CREATE_ALWAYS, streamFlags, NULL);
        if (output == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            CloseADSHandle(input);
            CFileOperationResult openResult =
                HandleADSOpenError(observer, targetPath, streamName, error, state);
            if (!openResult.success && openResult.lastError == ERROR_RETRY)
                continue;
            return openResult;
        }

        bool retryStream = false;
        std::vector<char> buffer(64 * 1024);
        while (true)
        {
            DWORD read = 0;
            const FileResult readIoResult = ExecutorFs()->ReadFromHandle(
                input, buffer.data(), (DWORD)buffer.size(), &read);
            if (!readIoResult.success)
            {
                DWORD error = readIoResult.errorCode;
                CloseADSHandle(output);
                CloseADSHandle(input);
                CFileOperationResult readResult = HandleADSReadError(observer, sourcePath, streamName, state);
                if (!readResult.success || readResult.skipped)
                    ExecutorFs()->DeleteFile(targetStream.c_str());
                return readResult.success ? readResult : ErrorResult(error);
            }
            if (read == 0)
                break;

            observer.WaitIfSuspended();
            if (observer.IsCancelled())
            {
                CloseADSHandle(output);
                CloseADSHandle(input);
                ExecutorFs()->DeleteFile(targetStream.c_str());
                return ErrorResult(ERROR_CANCELLED);
            }

            DWORD written = 0;
            const FileResult writeIoResult = ExecutorFs()->WriteToHandle(
                output, buffer.data(), read, &written);
            if (!writeIoResult.success || written != read)
            {
                DWORD error = writeIoResult.errorCode;
                if (error == ERROR_SUCCESS)
                    error = ERROR_WRITE_FAULT;
                CloseADSHandle(output);
                CloseADSHandle(input);
                ExecutorFs()->DeleteFile(targetStream.c_str());

                CFileOperationResult writeResult = HandleADSWriteError(observer, targetPath, error, state);
                if (!writeResult.success && writeResult.lastError == ERROR_RETRY)
                {
                    retryStream = true;
                    break;
                }
                return writeResult;
            }
        }

        if (retryStream)
            continue;

        CloseADSHandle(output);
        CloseADSHandle(input);
        return SuccessResult();
    }
}

static CFileOperationResult ExecuteCopyADSW(IWorkerObserver& observer,
                                            const std::wstring& sourcePath,
                                            const std::wstring& targetPath,
                                            CFileOperationExecutionState& state)
{
    DWORD error = ERROR_SUCCESS;
    std::vector<std::wstring> streams;
    if (!EnumerateADSStreamsW(sourcePath, streams, error))
    {
        CFileOperationResult readResult = HandleADSReadError(observer, sourcePath, L"", state);
        if (!readResult.success || readResult.skipped)
            return readResult;
        return SuccessResult();
    }

    for (const std::wstring& streamName : streams)
    {
        CFileOperationResult result = CopyOneADSStreamW(observer, sourcePath, targetPath, streamName, state);
        if (!result.success || result.skipped)
            return result;
    }
    return SuccessResult();
}

static bool HasUnsafeTrailingFileNameCharW(const std::wstring& path)
{
    return !path.empty() && (path.back() <= L' ' || path.back() == L'.');
}

static std::wstring ParentDirectoryW(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return std::wstring();
    return path.substr(0, slash + 1);
}

static void TranslateConvertChunk(const char* source,
                                  DWORD sourceSize,
                                  const CConvertData& convertData,
                                  bool& crlfBreak,
                                  std::vector<char>& target)
{
    target.clear();
    target.reserve(sourceSize * 2);

    for (DWORD i = 0; i < sourceSize; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(source[i]);
        const bool lastChar = i == sourceSize - 1;

        if (convertData.EOFType != 0)
        {
            if (crlfBreak && i == 0 && ch == '\n')
            {
                crlfBreak = false;
                continue;
            }

            if (ch == '\r' || ch == '\n')
            {
                switch (convertData.EOFType)
                {
                case 2:
                    target.push_back(convertData.CodeTable[static_cast<unsigned char>('\n')]);
                    break;
                case 3:
                    target.push_back(convertData.CodeTable[static_cast<unsigned char>('\r')]);
                    break;
                default:
                    target.push_back(convertData.CodeTable[static_cast<unsigned char>('\r')]);
                    target.push_back(convertData.CodeTable[static_cast<unsigned char>('\n')]);
                    break;
                }

                if (lastChar && ch == '\r')
                    crlfBreak = true;
                if (!lastChar && ch == '\r' && source[i + 1] == '\n')
                    ++i;
                continue;
            }
        }

        target.push_back(convertData.CodeTable[ch]);
    }
}

static bool WriteAllW(HANDLE file, const std::vector<char>& bytes, DWORD& error)
{
    error = ERROR_SUCCESS;
    size_t offset = 0;
    while (offset < bytes.size())
    {
        DWORD chunkSize = (DWORD)std::min<size_t>(bytes.size() - offset, MAXDWORD);
        DWORD written = 0;
        const FileResult writeResult = ExecutorFs()->WriteToHandle(
            file, bytes.data() + offset, chunkSize, &written);
        if (!writeResult.success || written == 0)
        {
            error = writeResult.errorCode;
            if (error == ERROR_SUCCESS)
                error = ERROR_WRITE_FAULT;
            return false;
        }
        offset += written;
    }
    return true;
}

static HANDLE CreateConvertTempFileW(const std::wstring& directory,
                                     std::wstring& tempPath,
                                     DWORD& error)
{
    error = ERROR_SUCCESS;
    tempPath.clear();
    if (directory.empty())
    {
        error = ERROR_INVALID_NAME;
        return INVALID_HANDLE_VALUE;
    }

    std::wstring base = directory;
    if (base.back() != L'\\' && base.back() != L'/')
        base.push_back(L'\\');

    const DWORD pid = GetCurrentProcessId();
    const DWORD tick = GetTickCount();
    for (DWORD attempt = 0; attempt < 256; ++attempt)
    {
        tempPath = base + L"cnv-" + std::to_wstring(pid) + L"-" +
                   std::to_wstring(tick) + L"-" + std::to_wstring(attempt) + L".tmp";
        HANDLE file = ExecutorFs()->CreateFile(tempPath.c_str(), GENERIC_WRITE, 0, NULL,
                                  CREATE_NEW,
                                  FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_SEQUENTIAL_SCAN,
                                  NULL);
        if (file != INVALID_HANDLE_VALUE)
            return file;

        error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            return INVALID_HANDLE_VALUE;
    }

    error = ERROR_ALREADY_EXISTS;
    return INVALID_HANDLE_VALUE;
}

static CFileOperationResult AskConvertFileError(IWorkerObserver& observer,
                                                const wchar_t* title,
                                                const std::wstring& path,
                                                DWORD error,
                                                CFileOperationExecutionState& state,
                                                bool& retry)
{
    retry = false;
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.SkipAllErrors)
        return SuccessResult(true);

    int response = AskFileError(observer, title, path, error);
    switch (response)
    {
    case IDRETRY:
        retry = true;
        return SuccessResult();
    case IDB_SKIPALL:
        state.SkipAllErrors = true;
        [[fallthrough]];
    case IDB_SKIP:
    case IDB_IGNORE:
    case IDB_ALL:
        return SuccessResult(true);
    case IDCANCEL:
    default:
        return ErrorResult(error != ERROR_SUCCESS ? error : ERROR_CANCELLED);
    }
}

static CFileOperationResult AskConvertMoveError(IWorkerObserver& observer,
                                                const std::wstring& tempPath,
                                                const std::wstring& sourcePath,
                                                DWORD error,
                                                CFileOperationExecutionState& state,
                                                bool& retry)
{
    retry = false;
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.SkipAllErrors)
        return SuccessResult(true);

    int response = observer.AskCannotMoveErr(tempPath.c_str(), sourcePath.c_str(), error, false);
    switch (response)
    {
    case IDRETRY:
        retry = true;
        return SuccessResult();
    case IDB_SKIPALL:
        state.SkipAllErrors = true;
        [[fallthrough]];
    case IDB_SKIP:
        return SuccessResult(true);
    case IDCANCEL:
    default:
        return ErrorResult(error != ERROR_SUCCESS ? error : ERROR_CANCELLED);
    }
}

static CFileOperationResult ExecuteConvertFileW(IWorkerObserver& observer,
                                                const std::wstring& sourcePath,
                                                const CConvertData* convertData,
                                                CFileOperationExecutionState& state)
{
    if (convertData == NULL)
        return ErrorResult(ERROR_INVALID_PARAMETER);

    if (HasUnsafeTrailingFileNameCharW(sourcePath))
    {
        bool retry = false;
        return AskConvertFileError(observer, L"Error opening file", sourcePath,
                                   ERROR_INVALID_NAME, state, retry);
    }

    while (true)
    {
        HANDLE source = ExecutorFs()->CreateFile(sourcePath.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (source == INVALID_HANDLE_VALUE)
        {
            bool retry = false;
            CFileOperationResult openResult =
                AskConvertFileError(observer, L"Error opening file", sourcePath, GetLastError(), state, retry);
            if (retry)
                continue;
            return openResult;
        }

        std::wstring tempPath;
        DWORD tempError = ERROR_SUCCESS;
        HANDLE target = CreateConvertTempFileW(ParentDirectoryW(sourcePath), tempPath, tempError);
        if (target == INVALID_HANDLE_VALUE)
        {
            ExecutorFs()->CloseFileHandle(source);
            bool retry = false;
            CFileOperationResult tempResult =
                AskConvertFileError(observer, L"Error creating temp file",
                                    tempPath.empty() ? sourcePath : tempPath,
                                    tempError, state, retry);
            if (retry)
                continue;
            return tempResult;
        }

        bool restart = false;
        bool crlfBreak = false;
        std::vector<char> sourceBuffer(OPERATION_BUFFER);
        std::vector<char> targetBuffer;
        while (true)
        {
            DWORD read = 0;
            const FileResult readIoResult = ExecutorFs()->ReadFromHandle(
                source, sourceBuffer.data(), (DWORD)sourceBuffer.size(), &read);
            if (!readIoResult.success)
            {
                DWORD error = readIoResult.errorCode;
                ExecutorFs()->CloseFileHandle(target);
                ExecutorFs()->CloseFileHandle(source);
                ExecutorFs()->DeleteFile(tempPath.c_str());

                bool retry = false;
                CFileOperationResult readResult =
                    AskConvertFileError(observer, L"Error reading file", sourcePath, error, state, retry);
                if (retry)
                {
                    restart = true;
                    break;
                }
                return readResult;
            }
            if (read == 0)
                break;

            observer.WaitIfSuspended();
            if (observer.IsCancelled())
            {
                ExecutorFs()->CloseFileHandle(target);
                ExecutorFs()->CloseFileHandle(source);
                ExecutorFs()->DeleteFile(tempPath.c_str());
                return ErrorResult(ERROR_CANCELLED);
            }

            TranslateConvertChunk(sourceBuffer.data(), read, *convertData, crlfBreak, targetBuffer);
            DWORD writeError = ERROR_SUCCESS;
            if (!WriteAllW(target, targetBuffer, writeError))
            {
                ExecutorFs()->CloseFileHandle(target);
                ExecutorFs()->CloseFileHandle(source);
                ExecutorFs()->DeleteFile(tempPath.c_str());

                bool retry = false;
                CFileOperationResult writeResult =
                    AskConvertFileError(observer, L"Error writing file", tempPath, writeError, state, retry);
                if (retry)
                {
                    restart = true;
                    break;
                }
                return writeResult;
            }
        }

        if (restart)
            continue;

        ExecutorFs()->CloseFileHandle(source);
        const FileResult closeFileResult = ExecutorFs()->CloseFileHandle(target);
        if (!closeFileResult.success)
        {
            DWORD error = closeFileResult.errorCode;
            ExecutorFs()->DeleteFile(tempPath.c_str());

            bool retry = false;
            CFileOperationResult closeDecision =
                AskConvertFileError(observer, L"Error writing file", tempPath, error, state, retry);
            if (retry)
                continue;
            return closeDecision;
        }

        const DWORD sourceAttrs = ExecutorFs()->GetFileAttributes(sourcePath.c_str());
        if (sourceAttrs != INVALID_FILE_ATTRIBUTES &&
            (sourceAttrs & FILE_ATTRIBUTE_READONLY) != 0)
        {
            ExecutorFs()->SetFileAttributes(sourcePath.c_str(), sourceAttrs & ~FILE_ATTRIBUTE_READONLY);
        }

        while (true)
        {
            const FileResult moveResult = ExecutorFs()->MoveFileWithFlags(
                tempPath.c_str(), sourcePath.c_str(), MoveFlags::ReplaceExisting);
            if (moveResult.success)
            {
                if (sourceAttrs != INVALID_FILE_ATTRIBUTES)
                    ExecutorFs()->SetFileAttributes(sourcePath.c_str(), sourceAttrs);
                return SuccessResult();
            }

            DWORD error = moveResult.errorCode;
            bool retry = false;
            CFileOperationResult moveErrorResult =
                AskConvertMoveError(observer, tempPath, sourcePath, error, state, retry);
            if (retry)
                continue;

            ExecutorFs()->DeleteFile(tempPath.c_str());
            if (sourceAttrs != INVALID_FILE_ATTRIBUTES)
                ExecutorFs()->SetFileAttributes(sourcePath.c_str(), sourceAttrs);
            return moveErrorResult;
        }
    }
}

class CSecurityDescriptorHolder
{
public:
    CSecurityDescriptorHolder(const CSecurityDescriptorHolder&) = delete;
    CSecurityDescriptorHolder& operator=(const CSecurityDescriptorHolder&) = delete;

    CSecurityDescriptorHolder() = default;

    bool Read(const std::wstring& path)
    {
        Reset();
        Path = AttributeWritePathW(path);
        const FileResult result = ExecutorFs()->GetPathSecurity(Path.c_str(), Descriptor);
        Error = result.success ? ERROR_SUCCESS : result.errorCode;
        return Error == ERROR_SUCCESS;
    }

    DWORD ApplyTo(const std::wstring& targetPath) const
    {
        if (Error != ERROR_SUCCESS)
            return Error;
        if (Descriptor.empty())
            return ERROR_INVALID_SECURITY_DESCR;
        const std::wstring target = AttributeWritePathW(targetPath);
        const DWORD targetAttrs = ExecutorFs()->GetFileAttributes(target.c_str());
        const FileResult result = ExecutorFs()->SetPathSecurity(
            target.c_str(), Descriptor.data(), Descriptor.size());

        if (targetAttrs != INVALID_FILE_ATTRIBUTES)
            ExecutorFs()->SetFileAttributes(target.c_str(), targetAttrs);
        return result.success ? ERROR_SUCCESS : result.errorCode;
    }

    DWORD LastError() const { return Error; }

private:
    std::wstring Path;
    std::vector<BYTE> Descriptor;
    DWORD Error = ERROR_SUCCESS;

    void Reset()
    {
        Descriptor.clear();
        Error = ERROR_SUCCESS;
        Path.clear();
    }
};

static CFileOperationResult HandleCopySecurityError(IWorkerObserver& observer,
                                                    const std::wstring& sourcePath,
                                                    const std::wstring& targetPath,
                                                    DWORD error,
                                                    CFileOperationExecutionState& state)
{
    observer.WaitIfSuspended();
    if (observer.IsCancelled())
        return ErrorResult(ERROR_CANCELLED);
    if (state.IgnoreAllCopySecurityErrors)
        return SuccessResult();

    int response = observer.AskCopyPermError(sourcePath.c_str(), targetPath.c_str(), error);
    switch (response)
    {
    case IDB_IGNOREALL:
        state.IgnoreAllCopySecurityErrors = true;
        [[fallthrough]];
    case IDB_IGNORE:
        return SuccessResult();
    case IDCANCEL:
    default:
        return ErrorResult(error != ERROR_SUCCESS ? error : ERROR_ACCESS_DENIED);
    }
}

static CFileOperationResult ExecuteCopySecurityW(IWorkerObserver& observer,
                                                 const std::wstring& sourcePath,
                                                 const std::wstring& targetPath,
                                                 CFileOperationExecutionState& state,
                                                 const CSecurityDescriptorHolder* preReadSecurity = NULL)
{
    if (state.IgnoreAllCopySecurityErrors)
        return SuccessResult();

    CSecurityDescriptorHolder security;
    const CSecurityDescriptorHolder* effectiveSecurity = preReadSecurity;
    if (effectiveSecurity == NULL)
    {
        security.Read(sourcePath);
        effectiveSecurity = &security;
    }

    if (effectiveSecurity->LastError() != ERROR_SUCCESS)
    {
        return HandleCopySecurityError(observer, sourcePath, targetPath,
                                       effectiveSecurity->LastError(), state);
    }

    DWORD error = effectiveSecurity->ApplyTo(targetPath);
    if (error != ERROR_SUCCESS)
        return HandleCopySecurityError(observer, sourcePath, targetPath, error, state);
    return SuccessResult();
}

static bool IsCompressionUnsupportedError(DWORD error)
{
    return error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED;
}

static bool IsEncryptionUnsupportedError(DWORD error)
{
    return error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED ||
           error == ERROR_CALL_NOT_IMPLEMENTED;
}

static DWORD SetCompressionFormatW(const std::wstring& path, USHORT compressionFormat)
{
    if (compressionFormat != COMPRESSION_FORMAT_NONE &&
        compressionFormat != COMPRESSION_FORMAT_DEFAULT)
    {
        return ERROR_INVALID_PARAMETER;
    }
    HANDLE file = ExecutorFs()->CreateFile(path.c_str(), FILE_READ_DATA | FILE_WRITE_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();

    const FileResult result = ExecutorFs()->SetHandleCompression(
        file, compressionFormat != COMPRESSION_FORMAT_NONE);
    ExecutorFs()->CloseFileHandle(file);
    return result.success ? ERROR_SUCCESS : result.errorCode;
}

static DWORD EncryptPathW(const wchar_t* path)
{
    const FileResult result = ExecutorFs()->EncryptPath(path);
    return result.success ? ERROR_SUCCESS : result.errorCode;
}

static DWORD DecryptPathW(const wchar_t* path)
{
    const FileResult result = ExecutorFs()->DecryptPath(path);
    return result.success ? ERROR_SUCCESS : result.errorCode;
}

static DWORD InvokeWithPreservedFileTimeW(const std::wstring& path,
                                          DWORD attrs,
                                          DWORD (*operationFn)(const wchar_t* path))
{
    DWORD flags = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0;

    HANDLE file = ExecutorFs()->CreateFile(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, flags, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();

    FILETIME created = {};
    FILETIME modified = {};
    const FileResult readTimeResult = ExecutorFs()->GetHandleFileTime(
        file, &created, NULL, &modified);
    ExecutorFs()->CloseFileHandle(file);
    if (!readTimeResult.success)
        return readTimeResult.errorCode;

    DWORD result = operationFn(path.c_str());

    file = ExecutorFs()->CreateFile(path.c_str(), FILE_WRITE_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, flags, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        const FileResult restoreTimeResult = ExecutorFs()->SetHandleFileTime(
            file, &created, NULL, &modified);
        ExecutorFs()->CloseFileHandle(file);
        if (result == ERROR_SUCCESS && !restoreTimeResult.success)
            result = restoreTimeResult.errorCode;
    }
    return result;
}

static CFileOperationResult ExecuteCompressionChangeW(IWorkerObserver& observer,
                                                      const std::wstring& path,
                                                      DWORD currentAttrs,
                                                      DWORD targetAttrs,
                                                      CFileOperationExecutionState& state)
{
    if (state.SkipCompressionChanges)
        return SuccessResult();

    const std::wstring attrPath = AttributeWritePathW(path);
    const bool targetCompressed = (targetAttrs & FILE_ATTRIBUTE_COMPRESSED) != 0;

    while (true)
    {
        DWORD effectiveCurrentAttrs = ExecutorFs()->GetFileAttributes(attrPath.c_str());
        if (effectiveCurrentAttrs == INVALID_FILE_ATTRIBUTES)
            effectiveCurrentAttrs = currentAttrs;

        if (effectiveCurrentAttrs != INVALID_FILE_ATTRIBUTES &&
            (((effectiveCurrentAttrs & FILE_ATTRIBUTE_COMPRESSED) != 0) == targetCompressed))
        {
            return SuccessResult();
        }

        const bool clearReadOnly = effectiveCurrentAttrs != INVALID_FILE_ATTRIBUTES &&
                                   (effectiveCurrentAttrs & FILE_ATTRIBUTE_READONLY) != 0;
        if (clearReadOnly)
            ExecutorFs()->SetFileAttributes(attrPath.c_str(), effectiveCurrentAttrs & ~FILE_ATTRIBUTE_READONLY);

        DWORD error = SetCompressionFormatW(attrPath, targetCompressed ? COMPRESSION_FORMAT_DEFAULT : COMPRESSION_FORMAT_NONE);

        if (clearReadOnly)
            ExecutorFs()->SetFileAttributes(attrPath.c_str(), effectiveCurrentAttrs);

        if (error == ERROR_SUCCESS)
            return SuccessResult();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(ERROR_CANCELLED);

        if (IsCompressionUnsupportedError(error))
        {
            state.SkipCompressionChanges = true;
            observer.NotifyError(L"Error changing compression", path.c_str(), L"Compression is not supported");
            return SuccessResult();
        }

        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, L"Error changing compression", path, error);
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

static CFileOperationResult ExecuteEncryptionChangeW(IWorkerObserver& observer,
                                                     const std::wstring& path,
                                                     DWORD currentAttrs,
                                                     DWORD targetAttrs,
                                                     CFileOperationExecutionState& state)
{
    if (state.SkipEncryptionChanges)
        return SuccessResult();

    const std::wstring attrPath = AttributeWritePathW(path);
    const bool targetEncrypted = (targetAttrs & FILE_ATTRIBUTE_ENCRYPTED) != 0;

    while (true)
    {
        DWORD effectiveCurrentAttrs = ExecutorFs()->GetFileAttributes(attrPath.c_str());
        if (effectiveCurrentAttrs == INVALID_FILE_ATTRIBUTES)
            effectiveCurrentAttrs = currentAttrs;

        if (effectiveCurrentAttrs != INVALID_FILE_ATTRIBUTES &&
            (((effectiveCurrentAttrs & FILE_ATTRIBUTE_ENCRYPTED) != 0) == targetEncrypted))
        {
            return SuccessResult();
        }

        if (targetEncrypted &&
            effectiveCurrentAttrs != INVALID_FILE_ATTRIBUTES &&
            (effectiveCurrentAttrs & FILE_ATTRIBUTE_SYSTEM) != 0 &&
            (targetAttrs & FILE_ATTRIBUTE_SYSTEM) != 0 &&
            !state.EncryptSystemAll)
        {
            observer.WaitIfSuspended();
            if (observer.IsCancelled())
                return ErrorResult(ERROR_CANCELLED);
            if (state.SkipAllEncryptSystem)
                return SuccessResult();

            int response = observer.AskHiddenOrSystem(L"Confirm system file encryption", path.c_str(), L"Encrypt system file");
            switch (response)
            {
            case IDB_ALL:
                state.EncryptSystemAll = true;
                [[fallthrough]];
            case IDYES:
                break;
            case IDB_SKIPALL:
                state.SkipAllEncryptSystem = true;
                [[fallthrough]];
            case IDB_SKIP:
                return SuccessResult();
            case IDCANCEL:
            default:
                return ErrorResult(ERROR_CANCELLED);
            }
        }

        DWORD attrsToRestore = INVALID_FILE_ATTRIBUTES;
        if (effectiveCurrentAttrs != INVALID_FILE_ATTRIBUTES)
        {
            DWORD attrsForOperation = effectiveCurrentAttrs;
            if (targetEncrypted)
                attrsForOperation &= ~(FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY);
            else
                attrsForOperation &= ~FILE_ATTRIBUTE_READONLY;

            if (attrsForOperation != effectiveCurrentAttrs)
            {
                attrsToRestore = effectiveCurrentAttrs;
                ExecutorFs()->SetFileAttributes(attrPath.c_str(), attrsForOperation);
            }
        }

        DWORD error = InvokeWithPreservedFileTimeW(attrPath, effectiveCurrentAttrs,
                                                   targetEncrypted ? EncryptPathW : DecryptPathW);

        if (attrsToRestore != INVALID_FILE_ATTRIBUTES)
            ExecutorFs()->SetFileAttributes(attrPath.c_str(), attrsToRestore);

        if (error == ERROR_SUCCESS)
            return SuccessResult();

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return ErrorResult(ERROR_CANCELLED);

        if (IsEncryptionUnsupportedError(error))
        {
            state.SkipEncryptionChanges = true;
            observer.NotifyError(L"Error changing encryption", path.c_str(), L"Encryption is not supported");
            return SuccessResult();
        }

        if (state.SkipAllErrors)
            return SuccessResult(true);

        int response = AskFileError(observer, L"Error changing encryption", path, error);
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

static CFileOperationResult ExecuteChangeAttributesW(IWorkerObserver& observer,
                                                     const std::wstring& path,
                                                     DWORD attrs,
                                                     DWORD currentAttrs,
                                                     const CChangeAttrsData* attrsData,
                                                     CFileOperationExecutionState& state)
{
    const std::wstring attrPath = AttributeWritePathW(path);
    while (true)
    {
        if (attrsData != NULL && attrsData->ChangeCompression &&
            (attrs & FILE_ATTRIBUTE_COMPRESSED) == 0)
        {
            CFileOperationResult compression = ExecuteCompressionChangeW(observer, path, currentAttrs, attrs, state);
            if (!compression.success || compression.skipped)
                return compression;
        }
        if (attrsData != NULL && attrsData->ChangeEncryption &&
            (attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0)
        {
            CFileOperationResult encryption = ExecuteEncryptionChangeW(observer, path, currentAttrs, attrs, state);
            if (!encryption.success || encryption.skipped)
                return encryption;
        }
        if (attrsData != NULL && attrsData->ChangeCompression &&
            (attrs & FILE_ATTRIBUTE_COMPRESSED) != 0)
        {
            CFileOperationResult compression = ExecuteCompressionChangeW(observer, path, currentAttrs, attrs, state);
            if (!compression.success || compression.skipped)
                return compression;
        }
        if (attrsData != NULL && attrsData->ChangeEncryption &&
            (attrs & FILE_ATTRIBUTE_ENCRYPTED) != 0)
        {
            CFileOperationResult encryption = ExecuteEncryptionChangeW(observer, path, currentAttrs, attrs, state);
            if (!encryption.success || encryption.skipped)
                return encryption;
        }

        const FileResult setAttrsResult = ExecutorFs()->SetFileAttributes(attrPath.c_str(), attrs);
        if (setAttrsResult.success)
        {
            if (attrsData == NULL ||
                (!attrsData->ChangeTimeModified &&
                 !attrsData->ChangeTimeCreated &&
                 !attrsData->ChangeTimeAccessed))
            {
                return SuccessResult();
            }

            const bool isReadOnly = (attrs & FILE_ATTRIBUTE_READONLY) != 0;
            if (isReadOnly)
                ExecutorFs()->SetFileAttributes(attrPath.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);

            DWORD flags = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0;
            HANDLE file = ExecutorFs()->CreateFile(attrPath.c_str(), FILE_WRITE_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL, OPEN_EXISTING, flags, NULL);
            if (file != INVALID_HANDLE_VALUE)
            {
                const FILETIME* created = attrsData->ChangeTimeCreated ? &attrsData->TimeCreated : NULL;
                const FILETIME* accessed = attrsData->ChangeTimeAccessed ? &attrsData->TimeAccessed : NULL;
                const FILETIME* modified = attrsData->ChangeTimeModified ? &attrsData->TimeModified : NULL;
                const FileResult timeResult = ExecutorFs()->SetHandleFileTime(
                    file, created, accessed, modified);
                const DWORD error = timeResult.errorCode;
                ExecutorFs()->CloseFileHandle(file);
                if (isReadOnly)
                    ExecutorFs()->SetFileAttributes(attrPath.c_str(), attrs);
                if (timeResult.success)
                    return SuccessResult();

                if (state.SkipAllErrors)
                    return SuccessResult(true);
                int response = AskFileError(observer, L"Error changing attributes", path, error);
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
            else
            {
                DWORD error = GetLastError();
                if (isReadOnly)
                    ExecutorFs()->SetFileAttributes(attrPath.c_str(), attrs);
                if (state.SkipAllErrors)
                    return SuccessResult(true);
                int response = AskFileError(observer, L"Error changing attributes", path, error);
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
        else
        {
            DWORD error = setAttrsResult.errorCode;
            observer.WaitIfSuspended();
            if (observer.IsCancelled())
                return ErrorResult(ERROR_CANCELLED);
            if (state.SkipAllErrors)
                return SuccessResult(true);

            int response = AskFileError(observer, L"Error changing attributes", path, error);
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
}

static CFileOperationResult ExecuteScriptOperation(IWorkerObserver& observer,
                                                   const COperation& op,
                                                   CFileOperationExecutionState& state,
                                                   const CScriptExecutionOptions& options)
{
    if (op.Opcode == ocLabelForSkipOfCreateDir)
        return SuccessResult(true);

    if (op.Opcode == ocCountSize)
        return SuccessResult();

    if (op.Opcode == ocChangeAttrs)
    {
        const std::wstring sourcePath = OperationSourcePathW(op);
        CProgressData progressData = {};
        progressData.Source = sourcePath.empty() ? NULL : sourcePath.c_str();
        observer.SetOperationInfo(&progressData);

        return ExecuteChangeAttributesW(observer, sourcePath, op.NewAttrs, op.Attr,
                                        options.AttrsData, state);
    }

    if (op.Opcode == ocConvert)
    {
        if ((op.Attr & (FILE_ATTRIBUTE_DIRECTORY |
                        FILE_ATTRIBUTE_REPARSE_POINT |
                        FILE_ATTRIBUTE_COMPRESSED |
                        FILE_ATTRIBUTE_ENCRYPTED |
                        FILE_ATTRIBUTE_SPARSE_FILE)) != 0)
        {
            return ErrorResult(ERROR_NOT_SUPPORTED);
        }

        const std::wstring sourcePath = OperationSourcePathW(op);
        CProgressData progressData = {};
        progressData.Source = sourcePath.empty() ? NULL : sourcePath.c_str();
        observer.SetOperationInfo(&progressData);

        return ExecuteConvertFileW(observer, sourcePath, options.ConvertData, state);
    }

    if (op.Opcode == ocCopyDirTime)
    {
        const std::wstring targetPath = OperationTargetPathW(op);
        CProgressData progressData = {};
        progressData.Target = targetPath.empty() ? NULL : targetPath.c_str();
        observer.SetOperationInfo(&progressData);

        return ExecuteCopyDirectoryTimeW(observer, targetPath, op.DirTime, state);
    }

    const std::wstring sourcePath = OperationSourcePathW(op);
    const std::wstring targetPath = OperationTargetPathW(op);

    CProgressData progressData = {};
    progressData.Source = sourcePath.empty() ? NULL : sourcePath.c_str();
    progressData.Target = targetPath.empty() ? NULL : targetPath.c_str();
    observer.SetOperationInfo(&progressData);

    CSecurityDescriptorHolder moveSecurity;
    bool hasMoveSecurity = false;
    bool isMoveWithSecurity = options.CopySecurity &&
                              (op.Opcode == ocMoveFile || op.Opcode == ocMoveDir);
    if (isMoveWithSecurity && !state.IgnoreAllCopySecurityErrors)
    {
        if (moveSecurity.Read(sourcePath))
        {
            hasMoveSecurity = true;
        }
        else
        {
            CFileOperationResult securityResult =
                HandleCopySecurityError(observer, sourcePath, targetPath, moveSecurity.LastError(), state);
            if (!securityResult.success)
                return securityResult;
        }
    }

    CFileOperationResult result;
    switch (op.Opcode)
    {
    case ocCopyFile:
        result = ExecuteCopyFileW(observer, sourcePath, targetPath, state);
        break;
    case ocMoveFile:
        result = ExecuteMoveFileW(observer, sourcePath, targetPath, state);
        break;
    case ocDeleteFile:
        return ExecuteDeleteFileW(observer, sourcePath, op.Attr, state);
    case ocCreateDir:
        result = ExecuteCreateDirectoryW(observer, targetPath, state);
        break;
    case ocMoveDir:
        result = ExecuteMoveDirectoryW(observer, sourcePath, targetPath, state);
        break;
    case ocDeleteDir:
    case ocDeleteDirLink:
        return ExecuteRemoveDirectoryW(observer, sourcePath, state);
    case ocCreateDirLink: // clone the link (never its target)
    {
        IFileSystem* linkFs = gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();
        std::vector<BYTE> blob;
        FileResult r = linkFs->GetReparseData(sourcePath.c_str(), blob);
        if (r.success)
        {
            r = linkFs->CreateDirectory(targetPath.c_str());
            if (r.success || r.errorCode == ERROR_ALREADY_EXISTS)
                r = linkFs->SetReparseData(targetPath.c_str(), blob.data(), blob.size());
        }
        if (!r.success)
            return ErrorResult(r.errorCode);
        return SuccessResult();
    }
    default:
        return ErrorResult(ERROR_NOT_SUPPORTED);
    }

    if (!result.success || result.skipped)
        return result;

    if ((op.OpFlags & OPFL_COPY_ADS) != 0)
    {
        switch (op.Opcode)
        {
        case ocCopyFile:
        case ocCreateDir:
        {
            CFileOperationResult adsResult = ExecuteCopyADSW(observer, sourcePath, targetPath, state);
            if (!adsResult.success || adsResult.skipped)
                return adsResult;
            break;
        }
        default:
            break;
        }
    }

    if (!options.CopySecurity)
        return result;

    switch (op.Opcode)
    {
    case ocCopyFile:
    case ocCreateDir:
        return ExecuteCopySecurityW(observer, sourcePath, targetPath, state);
    case ocMoveFile:
    case ocMoveDir:
        if (hasMoveSecurity)
            return ExecuteCopySecurityW(observer, sourcePath, targetPath, state, &moveSecurity);
        return result;
    default:
        return result;
    }
}

CScriptExecutionResult ExecuteOperationsHeadless(IWorkerObserver& observer,
                                                 COperations& script,
                                                 CFileOperationExecutionState& state,
                                                 bool signalDone)
{
    CScriptExecutionOptions options;
    return ExecuteOperationsHeadlessWithOptions(observer, script, state, options, signalDone);
}

CScriptExecutionResult ExecuteOperationsHeadlessWithOptions(IWorkerObserver& observer,
                                                            COperations& script,
                                                            CFileOperationExecutionState& state,
                                                            const CScriptExecutionOptions& options,
                                                            bool signalDone)
{
    CScriptExecutionResult result;
    CScriptExecutionOptions effectiveOptions = options;
    if (script.CopySecurity)
        effectiveOptions.CopySecurity = true;

    int totalProgressOps = 0;
    for (int i = 0; i < script.Count; ++i)
    {
        if (ScriptOperationCountsForProgress(script.At(i).Opcode))
            ++totalProgressOps;
    }

    observer.SetProgress(0, 0);
    int processedProgressOps = 0;
    for (int i = 0; i < script.Count; ++i)
    {
        if (observer.IsCancelled())
        {
            result.lastError = ERROR_CANCELLED;
            observer.SetError(true);
            if (signalDone)
                observer.NotifyDone();
            return result;
        }

        const COperation& op = script.At(i);
        CFileOperationResult opResult = ExecuteScriptOperation(observer, op, state, effectiveOptions);
        if (!opResult.success)
        {
            result.lastError = opResult.lastError != ERROR_SUCCESS ? opResult.lastError : ERROR_INVALID_PARAMETER;
            observer.SetError(true);
            if (signalDone)
                observer.NotifyDone();
            return result;
        }

        if (ScriptOperationCountsForProgress(op.Opcode))
        {
            ++processedProgressOps;
            ++result.completedOperations;
            const int progress = totalProgressOps == 0
                                     ? 1000
                                     : (processedProgressOps * 1000) / totalProgressOps;
            observer.SetProgress(0, progress);
        }
    }

    result.success = true;
    observer.SetProgress(0, 1000);
    observer.SetError(false);
    if (signalDone)
        observer.NotifyDone();
    return result;
}

} // namespace sally::operation_executor
