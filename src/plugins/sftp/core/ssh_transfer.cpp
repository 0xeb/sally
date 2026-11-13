// File transfers (CSshSession members). Every libssh2 call goes through Run/RunPtr.
#include <libssh2.h>
#include <libssh2_sftp.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "ssh_session.h"

namespace
{
constexpr size_t kMinBufferSize = 4 * 1024;
constexpr size_t kMaxBufferSize = 8 * 1024 * 1024;
constexpr auto kProgressInterval = std::chrono::milliseconds(100);

// Calls ISshCallbacks::Progress at most once per interval; Final always calls.
class CProgress
{
public:
    CProgress(ISshCallbacks* cb, uint64_t total) : cb_(cb), total_(total), last_(std::chrono::steady_clock::now()) {}

    // false: the callback asked to cancel
    bool Tick(uint64_t done)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_ < kProgressInterval)
            return true;
        last_ = now;
        return cb_ == nullptr || cb_->Progress(done, total_);
    }
    void Final(uint64_t done)
    {
        if (cb_ != nullptr)
            cb_->Progress(done, total_); // everything is transferred: a cancel answer no longer matters
    }

private:
    ISshCallbacks* cb_;
    uint64_t total_;
    std::chrono::steady_clock::time_point last_;
};

bool SetLocalPosition(HANDLE file, uint64_t offset)
{
    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(offset);
    return SetFilePointerEx(file, pos, nullptr, FILE_BEGIN) != FALSE;
}

// WriteFile until everything is written
bool WriteAll(HANDLE file, const char* data, size_t size, DWORD& winError)
{
    while (size > 0)
    {
        const DWORD chunk = static_cast<DWORD>((std::min<size_t>)(size, 1u << 30));
        DWORD written = 0;
        if (!WriteFile(file, data, chunk, &written, nullptr))
        {
            winError = GetLastError();
            return false;
        }
        if (written == 0)
        {
            winError = ERROR_WRITE_FAULT;
            return false;
        }
        data += written;
        size -= written;
    }
    return true;
}

int64_t UnixTimeOfFileTime(const FILETIME& ft)
{
    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    constexpr int64_t kEpochDiff100ns = 116444736000000000LL; // 1601 -> 1970
    return (static_cast<int64_t>(t.QuadPart) - kEpochDiff100ns) / 10000000;
}
} // namespace

void CSshSession::SetTransferBufferSize(size_t bytes)
{
    transferBufferSize_ = (std::min)((std::max)(bytes, kMinBufferSize), kMaxBufferSize);
}

// The English message ends in "(Windows error N)": the operations layer recognises disk full by it.
bool CSshSession::FailLocal(SshMsg msgId, const char* what, DWORD winError)
{
    return Fail(SshErrorKind::Io, msgId, {std::to_string(winError)},
                std::string(what) + " (Windows error " + std::to_string(winError) + ")");
}

bool CSshSession::Download(const std::string& remotePath, HANDLE localFile, uint64_t startOffset,
                           uint64_t* bytesTransferred, uint64_t* totalSize)
{
    uint64_t done = 0;
    if (bytesTransferred != nullptr)
        *bytesTransferred = 0;
    if (totalSize != nullptr)
        *totalSize = 0;
    if (!BeginSftp("download", remotePath))
        return false;

    int code = 0;
    LIBSSH2_SFTP_HANDLE* h = RunPtr(
        [&]
        {
            return libssh2_sftp_open_ex(sftp_, remotePath.data(), static_cast<unsigned int>(remotePath.size()),
                                        LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
        },
        timeoutMs_, code);
    if (h == nullptr)
        return FailSftp(SshMsg::SftpReadFile, "Cannot open", remotePath, code);

    const bool ok = DownloadLoop(h, remotePath, localFile, startOffset, &done, totalSize);
    const CSshError error = error_;
    CloseSftpHandle(h); // after a cancel without waiting: the socket is down and libssh2 frees the handle
    if (bytesTransferred != nullptr)
        *bytesTransferred = done;
    if (!ok)
        error_ = error; // closing must not replace the reason of the failure
    return ok;
}

bool CSshSession::DownloadLoop(LIBSSH2_SFTP_HANDLE* h, const std::string& remotePath, HANDLE localFile,
                               uint64_t startOffset, uint64_t* done, uint64_t* totalSize)
{
    LIBSSH2_SFTP_ATTRIBUTES attrs = {};
    int rc = Run([&] { return libssh2_sftp_fstat_ex(h, &attrs, 0); }, timeoutMs_);
    if (rc != 0)
        return FailSftp(SshMsg::SftpReadInfo, "Cannot get the size of", remotePath, rc);
    const uint64_t total = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) != 0 ? attrs.filesize : 0;
    if (totalSize != nullptr)
        *totalSize = total;
    // A resume needs a known size: a server that reports none (or a file of 0 bytes) cannot be resumed at an offset
    if (startOffset > 0 && ((attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) == 0 || startOffset > total))
        return Fail(SshErrorKind::Io, SshMsg::ResumeFailed, {Display(remotePath)},
                    "Cannot resume '" + Display(remotePath) +
                        "': the start offset is beyond the end of the remote file or its size is unknown");

    if (!SetLocalPosition(localFile, startOffset))
        return FailLocal(SshMsg::LocalSetPosition, "Cannot set the position in the local file", GetLastError());
    libssh2_sftp_seek64(h, startOffset);

    std::vector<char> buf(transferBufferSize_);
    CProgress progress(cb_, total);
    uint64_t position = startOffset;
    for (;;)
    {
        if (IsCancelled())
            return Fail(SshErrorKind::Cancelled, std::string());
        const int got =
            Run([&] { return static_cast<int>(libssh2_sftp_read(h, buf.data(), buf.size())); }, timeoutMs_);
        if (got == 0)
            break; // end of file
        if (got < 0)
            return FailSftp(SshMsg::SftpReadFile, "Cannot read", remotePath, got);

        DWORD winError = 0;
        if (!WriteAll(localFile, buf.data(), static_cast<size_t>(got), winError))
            return FailLocal(SshMsg::LocalWrite, "Cannot write the local file", winError);
        position += static_cast<uint64_t>(got);
        *done = position - startOffset;
        if (!progress.Tick(position))
            return Fail(SshErrorKind::Cancelled, "The transfer was cancelled");
    }
    progress.Final(position);
    return true;
}

bool CSshSession::Upload(HANDLE localFile, const std::string& remotePath, uint64_t startOffset, bool resume,
                         uint64_t* bytesTransferred, bool keepMtime)
{
    uint64_t done = 0;
    if (bytesTransferred != nullptr)
        *bytesTransferred = 0;
    if (!BeginSftp("upload", remotePath))
        return false;

    // The local file is checked before the remote one is opened: opening with TRUNC destroys the remote data.
    LARGE_INTEGER localSize;
    if (!GetFileSizeEx(localFile, &localSize))
        return FailLocal(SshMsg::LocalGetSize, "Cannot get the size of the local file", GetLastError());
    if (startOffset > static_cast<uint64_t>(localSize.QuadPart))
        return Fail(SshErrorKind::Io, "The start offset is beyond the end of the local file");

    // Offset 0 starts the file anew; a nonzero offset keeps the existing data, so the file must exist and is never
    // created (resume only documents the intent: without the file there is nothing to continue, and an empty file
    // must not be left behind by a call that then fails)
    (void)resume;
    unsigned long flags = LIBSSH2_FXF_WRITE;
    if (startOffset == 0)
        flags |= LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC;

    int code = 0;
    LIBSSH2_SFTP_HANDLE* h = RunPtr(
        [&]
        {
            return libssh2_sftp_open_ex(sftp_, remotePath.data(), static_cast<unsigned int>(remotePath.size()), flags,
                                        0644, LIBSSH2_SFTP_OPENFILE);
        },
        timeoutMs_, code);
    if (h == nullptr)
        return FailSftp(SshMsg::SftpWriteFile, "Cannot open", remotePath, code);

    bool ok = UploadLoop(h, remotePath, localFile, startOffset, &done);
    if (bytesTransferred != nullptr)
        *bytesTransferred = done;

    // Closing flushes the server's write; its failure (e.g. disk full) is a transfer failure, also when a Cancel
    // arrived after the last block (then the close cannot complete and the call reports the cancel: the data is not
    // confirmed). A failed transfer only closes the handle and keeps its own error.
    if (ok)
    {
        const int rc = Run([h] { return libssh2_sftp_close_handle(h); }, timeoutMs_);
        if (rc != 0)
        {
            if (rc == kBrokenCode)
                CloseSftpHandle(h); // Run did not call libssh2
            ok = FailSftp(SshMsg::SftpWriteFile, "Cannot finish writing", remotePath, rc);
        }
    }
    else
    {
        const CSshError error = error_;
        CloseSftpHandle(h);
        error_ = error;
    }

    if (ok && keepMtime)
    {
        FILETIME written;
        if (!GetFileTime(localFile, nullptr, nullptr, &written))
            return FailLocal(SshMsg::LocalReadTime, "Cannot read the time of the local file", GetLastError());
        const int64_t mtime = UnixTimeOfFileTime(written);
        ok = SetTimes(remotePath, mtime, mtime);
    }
    return ok;
}

bool CSshSession::UploadLoop(LIBSSH2_SFTP_HANDLE* h, const std::string& remotePath, HANDLE localFile,
                             uint64_t startOffset, uint64_t* done)
{
    LARGE_INTEGER size;
    if (!GetFileSizeEx(localFile, &size))
        return FailLocal(SshMsg::LocalGetSize, "Cannot get the size of the local file", GetLastError());
    const uint64_t total = static_cast<uint64_t>(size.QuadPart);
    if (startOffset > total)
        return Fail(SshErrorKind::Io, "The start offset is beyond the end of the local file");

    if (startOffset > 0)
    {
        LIBSSH2_SFTP_ATTRIBUTES attrs = {};
        int rc = Run([&] { return libssh2_sftp_fstat_ex(h, &attrs, 0); }, timeoutMs_);
        if (rc != 0)
            return FailSftp(SshMsg::SftpReadInfo, "Cannot get the size of", remotePath, rc);
        if ((attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) == 0 || attrs.filesize < startOffset)
            return Fail(SshErrorKind::Io, SshMsg::ResumeFailed, {Display(remotePath)},
                        "Cannot resume '" + Display(remotePath) + "': remote file is shorter than the resume offset");
    }

    if (!SetLocalPosition(localFile, startOffset))
        return FailLocal(SshMsg::LocalSetPosition, "Cannot set the position in the local file", GetLastError());
    libssh2_sftp_seek64(h, startOffset);

    std::vector<char> buf(transferBufferSize_);
    CProgress progress(cb_, total);
    uint64_t position = startOffset;
    for (;;)
    {
        if (IsCancelled())
            return Fail(SshErrorKind::Cancelled, std::string());
        DWORD got = 0;
        if (!ReadFile(localFile, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr))
            return FailLocal(SshMsg::LocalRead, "Cannot read the local file", GetLastError());
        if (got == 0)
            break; // end of file

        size_t sent = 0;
        while (sent < got)
        {
            if (IsCancelled())
                return Fail(SshErrorKind::Cancelled, std::string());
            const int rc = Run(
                [&] { return static_cast<int>(libssh2_sftp_write(h, buf.data() + sent, got - sent)); }, timeoutMs_);
            if (rc < 0)
                return FailSftp(SshMsg::SftpWriteFile, "Cannot write", remotePath, rc);
            if (rc == 0)
                return Fail(SshErrorKind::Io, SshMsg::ServerAcceptedNoData, {Display(remotePath)},
                            "The server accepted no data for '" + Display(remotePath) + "'");
            sent += static_cast<size_t>(rc);
            position += static_cast<uint64_t>(rc);
            *done = position - startOffset;
        }
        if (!progress.Tick(position))
            return Fail(SshErrorKind::Cancelled, "The transfer was cancelled");
    }
    progress.Final(position);
    return true;
}
