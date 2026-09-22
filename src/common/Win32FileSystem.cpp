// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include "IFileSystem.h"
#include "IPathService.h"
#include "fsutil.h"
#include <aclapi.h>
#include <limits>
#include <new>
#include <ntddscsi.h>
#include <stdexcept>
#include <string>
#include <utility>

// RAII wrapper for canonical literal I/O path preparation.
class LongPath
{
public:
    explicit LongPath(const wchar_t* path) : m_valid(false)
    {
        if (gPathService == nullptr)
            gPathService = GetWin32PathService();
        if (gPathService == nullptr)
        {
            SetLastError(ERROR_INVALID_FUNCTION);
            return;
        }

        PathResult res = gPathService->PrepareForIo(path, m_path);
        if (!res.success)
        {
            SetLastError(res.errorCode);
            return;
        }
        m_valid = true;
    }

    const wchar_t* Get() const { return m_path.c_str(); }
    bool IsValid() const { return m_valid; }

private:
    std::wstring m_path;
    bool m_valid;
    LongPath(const LongPath&);
    LongPath& operator=(const LongPath&);
};

// Win32 implementation of IFileSystem with long path support
class Win32FileSystem : public IFileSystem
{
public:
    static DWORD LastErrorOr(DWORD fallback)
    {
        DWORD err = GetLastError();
        return err != ERROR_SUCCESS ? err : fallback;
    }

    bool FileExists(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return false;
        DWORD attrs = GetFileAttributesW(lp.Get());
        return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool DirectoryExists(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return false;
        DWORD attrs = GetFileAttributesW(lp.Get());
        return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    FileResult GetFileInfo(const wchar_t* path, FileInfo& info) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));

        WIN32_FILE_ATTRIBUTE_DATA data;
        if (!GetFileAttributesExW(lp.Get(), GetFileExInfoStandard, &data))
            return FileResult::Error(GetLastError());

        info.name = path;  // Store original path, not the prefixed one
        info.size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        info.creationTime = data.ftCreationTime;
        info.lastWriteTime = data.ftLastWriteTime;
        info.attributes = data.dwFileAttributes;
        info.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return FileResult::Ok();
    }

    DWORD GetFileAttributes(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(LastErrorOr(ERROR_INVALID_PARAMETER));
            return INVALID_FILE_ATTRIBUTES;
        }
        return ::GetFileAttributesW(lp.Get());
    }

    FileResult SetFileAttributes(const wchar_t* path, DWORD attributes) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::SetFileAttributesW(lp.Get(), attributes))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult DeleteFile(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::DeleteFileW(lp.Get()))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult MoveFile(const wchar_t* source, const wchar_t* target) override
    {
        LongPath lpSrc(source);
        LongPath lpDst(target);
        if (!lpSrc.IsValid() || !lpDst.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::MoveFileW(lpSrc.Get(), lpDst.Get()))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult CopyFile(const wchar_t* source, const wchar_t* target, bool failIfExists) override
    {
        LongPath lpSrc(source);
        LongPath lpDst(target);
        if (!lpSrc.IsValid() || !lpDst.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::CopyFileW(lpSrc.Get(), lpDst.Get(), failIfExists ? TRUE : FALSE))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    FileResult CreateDirectory(const wchar_t* path) override
    {
        return CreateDirectoryWithSecurity(path, NULL);
    }

    FileResult CreateDirectoryWithSecurity(const wchar_t* path,
                                           LPSECURITY_ATTRIBUTES securityAttributes) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::CreateDirectoryW(lp.Get(), securityAttributes))
            return FileResult::Ok();
        // Keep raw Win32 semantics: an existing directory is reported as
        // Error(ERROR_ALREADY_EXISTS). SalLPCreateDirectory callers distinguish
        // it via GetLastError(), and CreateDirectoryFlow must not record an
        // existing directory as "first created" (rollback would delete it).
        return FileResult::Error(GetLastError());
    }

    FileResult RemoveDirectory(const wchar_t* path) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (::RemoveDirectoryW(lp.Get()))
            return FileResult::Ok();
        return FileResult::Error(GetLastError());
    }

    HANDLE CreateFile(const wchar_t* path,
                      DWORD desiredAccess,
                      DWORD shareMode,
                      LPSECURITY_ATTRIBUTES securityAttributes,
                      DWORD creationDisposition,
                      DWORD flagsAndAttributes,
                      HANDLE templateFile) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(LastErrorOr(ERROR_INVALID_PARAMETER));
            return INVALID_HANDLE_VALUE;
        }
        return ::CreateFileW(lp.Get(), desiredAccess, shareMode, securityAttributes,
                             creationDisposition, flagsAndAttributes, templateFile);
    }

    HANDLE FindFirstFile(const wchar_t* path, WIN32_FIND_DATAW* findData) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(LastErrorOr(ERROR_INVALID_PARAMETER));
            return INVALID_HANDLE_VALUE;
        }
        return HANDLES_Q(FindFirstFileW(lp.Get(), findData));
    }

    BOOL FindNextFile(HANDLE findHandle, WIN32_FIND_DATAW* findData) override
    {
        return ::FindNextFileW(findHandle, findData);
    }

    FileResult CloseFind(HANDLE findHandle) override
    {
        if (findHandle == INVALID_HANDLE_VALUE || findHandle == NULL)
            return FileResult::Error(ERROR_INVALID_HANDLE);
        if (!HANDLES(FindClose(findHandle)))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    HANDLE OpenFileForRead(const wchar_t* path, DWORD shareMode) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(LastErrorOr(ERROR_INVALID_PARAMETER));
            return INVALID_HANDLE_VALUE;
        }
        return ::CreateFileW(lp.Get(), GENERIC_READ, shareMode, NULL,
                             OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    }

    HANDLE CreateFileForWrite(const wchar_t* path, bool failIfExists) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
        {
            SetLastError(LastErrorOr(ERROR_INVALID_PARAMETER));
            return INVALID_HANDLE_VALUE;
        }
        DWORD disposition = failIfExists ? CREATE_NEW : CREATE_ALWAYS;
        return ::CreateFileW(lp.Get(), GENERIC_WRITE, 0, NULL,
                             disposition, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    }

    FileResult CloseFileHandle(HANDLE h) override
    {
        if (h == INVALID_HANDLE_VALUE || h == NULL)
            return FileResult::Error(ERROR_INVALID_HANDLE);
        if (!::CloseHandle(h))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    // --- P2-a handle I/O ops ---------------------------------------------

    FileResult ReadFromHandle(HANDLE h, void* buffer, DWORD toRead, DWORD* read) override
    {
        DWORD got = 0;
        if (!::ReadFile(h, buffer, toRead, &got, NULL))
            return FileResult::Error(::GetLastError());
        if (read)
            *read = got;
        return FileResult::Ok();
    }

    FileResult WriteToHandle(HANDLE h, const void* buffer, DWORD toWrite, DWORD* written) override
    {
        DWORD put = 0;
        if (!::WriteFile(h, buffer, toWrite, &put, NULL))
            return FileResult::Error(::GetLastError());
        if (written)
            *written = put;
        return FileResult::Ok();
    }

    FileResult ReadFromHandleOverlapped(HANDLE h, void* buffer, DWORD toRead,
                                         OVERLAPPED* overlapped) override
    {
        if (!::ReadFile(h, buffer, toRead, NULL, overlapped))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult WriteToHandleOverlapped(HANDLE h, const void* buffer, DWORD toWrite,
                                        OVERLAPPED* overlapped) override
    {
        if (!::WriteFile(h, buffer, toWrite, NULL, overlapped))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult CompleteHandleIo(HANDLE h, OVERLAPPED* overlapped,
                                 DWORD* transferred, bool wait) override
    {
        DWORD bytes = 0;
        if (!::GetOverlappedResult(h, overlapped, &bytes, wait ? TRUE : FALSE))
            return FileResult::Error(::GetLastError());
        if (transferred)
            *transferred = bytes;
        return FileResult::Ok();
    }

    FileResult CancelHandleIo(HANDLE h) override
    {
        if (!::CancelIo(h))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult SeekHandle(HANDLE h, int64_t distance, DWORD moveMethod, uint64_t* newPos) override
    {
        LARGE_INTEGER li;
        li.QuadPart = distance;
        LARGE_INTEGER out;
        if (!::SetFilePointerEx(h, li, &out, moveMethod))
            return FileResult::Error(::GetLastError());
        if (newPos)
            *newPos = (uint64_t)out.QuadPart;
        return FileResult::Ok();
    }

    FileResult SetHandleFileTime(HANDLE h, const FILETIME* creation,
                                 const FILETIME* lastAccess, const FILETIME* lastWrite) override
    {
        if (!::SetFileTime(h, creation, lastAccess, lastWrite))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult GetHandleFileTime(HANDLE h, FILETIME* creation,
                                 FILETIME* lastAccess, FILETIME* lastWrite) override
    {
        if (!::GetFileTime(h, creation, lastAccess, lastWrite))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult GetHandleFileSize(HANDLE h, uint64_t* size) override
    {
        LARGE_INTEGER li;
        if (!::GetFileSizeEx(h, &li))
            return FileResult::Error(::GetLastError());
        if (size)
            *size = (uint64_t)li.QuadPart;
        return FileResult::Ok();
    }

    FileResult GetHandleStreams(
        HANDLE h, std::vector<FileStreamEntry>& streams) override
    {
        streams.clear();
        size_t capacity = sizeof(FILE_STREAM_INFO) + 256 * sizeof(wchar_t);
        std::vector<BYTE> storage;
        for (;;)
        {
            if (capacity > MAXDWORD)
                return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
            storage.assign(capacity, 0);
            if (::GetFileInformationByHandleEx(
                    h, FileStreamInfo, storage.data(), (DWORD)storage.size()))
                break;

            const DWORD error = ::GetLastError();
            // Directories without named streams can report EOF instead of
            // returning an empty FILE_STREAM_INFO list.  That is a successful
            // enumeration with zero entries, not an ADS probe failure.
            if (error == ERROR_HANDLE_EOF)
                return FileResult::Ok();
            if (error != ERROR_MORE_DATA && error != ERROR_INSUFFICIENT_BUFFER)
                return FileResult::Error(error);
            if (capacity > MAXDWORD / 2)
                return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
            capacity *= 2;
        }

        const BYTE* current = storage.data();
        const BYTE* const end = storage.data() + storage.size();
        for (;;)
        {
            if ((size_t)(end - current) < offsetof(FILE_STREAM_INFO, StreamName))
            {
                streams.clear();
                return FileResult::Error(ERROR_INVALID_DATA);
            }
            const FILE_STREAM_INFO* info = (const FILE_STREAM_INFO*)current;
            const size_t nameBytes = info->StreamNameLength;
            // A zero-length name ends the list; it is not a stream.
            //
            // GetFileInformationByHandleEx reports no byte count, and 'storage' is
            // zero-filled, so a call that succeeds having written nothing leaves a
            // record that parses cleanly as one entry with an empty name and a
            // NextEntryOffset of 0. Pre-unicode guarded this with its
            // "if (ioStatus.Information > 0) // check whether we obtained any data at
            // all"; the Win32 form of the same check is that no real FILE_STREAM_INFO
            // has an empty name - even the default stream is called "::$DATA". Without
            // it, the phantom entry is not "::$DATA", so every such file looked like it
            // carried an alternate data stream: the copy warns about ADS loss it is not
            // about to cause, and reports the streams as non-discardable.
            if (nameBytes == 0)
                return FileResult::Ok();
            if (nameBytes % sizeof(wchar_t) != 0 ||
                nameBytes > (size_t)(end - current) -
                                offsetof(FILE_STREAM_INFO, StreamName))
            {
                streams.clear();
                return FileResult::Error(ERROR_INVALID_DATA);
            }

            FileStreamEntry entry;
            entry.name.assign(info->StreamName,
                              nameBytes / sizeof(wchar_t));
            entry.size = info->StreamSize.QuadPart < 0
                             ? 0
                             : (uint64_t)info->StreamSize.QuadPart;
            entry.allocationSize = info->StreamAllocationSize.QuadPart < 0
                                       ? 0
                                       : (uint64_t)info->StreamAllocationSize.QuadPart;
            streams.push_back(std::move(entry));

            if (info->NextEntryOffset == 0)
                return FileResult::Ok();
            if (info->NextEntryOffset < offsetof(FILE_STREAM_INFO, StreamName) ||
                info->NextEntryOffset > (size_t)(end - current))
            {
                streams.clear();
                return FileResult::Error(ERROR_INVALID_DATA);
            }
            current += info->NextEntryOffset;
        }
    }

    FileResult FlushHandle(HANDLE h) override
    {
        if (!::FlushFileBuffers(h))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult SetHandleEnd(HANDLE h) override
    {
        if (!::SetEndOfFile(h))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    // --- P2-a semantic attributes ----------------------------------------

    FileResult SetHandleCompression(HANDLE h, bool compress) override
    {
        USHORT state = compress ? COMPRESSION_FORMAT_DEFAULT : COMPRESSION_FORMAT_NONE;
        DWORD bytes = 0;
        if (!::DeviceIoControl(h, FSCTL_SET_COMPRESSION, &state, sizeof(state),
                               NULL, 0, &bytes, NULL))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult SetHandleCompressionOverlapped(HANDLE h, bool compress,
                                               OVERLAPPED* overlapped) override
    {
        USHORT state = compress ? COMPRESSION_FORMAT_DEFAULT : COMPRESSION_FORMAT_NONE;
        if (!::DeviceIoControl(h, FSCTL_SET_COMPRESSION, &state, sizeof(state),
                               NULL, 0, NULL, overlapped))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult SetHandleSparse(HANDLE h, bool sparse) override
    {
        FILE_SET_SPARSE_BUFFER buf;
        buf.SetSparse = sparse ? TRUE : FALSE;
        DWORD bytes = 0;
        if (!::DeviceIoControl(h, FSCTL_SET_SPARSE, &buf, sizeof(buf),
                               NULL, 0, &bytes, NULL))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult EncryptPath(const wchar_t* path) override
    {
        LongPath lp(path);
        // EncryptFileW does not accept the \\?\ prefix reliably; use raw path.
        if (!::EncryptFileW(path))
            return FileResult::Error(::GetLastError());
        (void)lp;
        return FileResult::Ok();
    }

    FileResult DecryptPath(const wchar_t* path) override
    {
        if (!::DecryptFileW(path, 0))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    // --- P2-a security blob ----------------------------------------------

    FileResult GetPathSecurity(const wchar_t* path, std::vector<BYTE>& sd) override
    {
        sd.clear();
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        const SECURITY_INFORMATION si = OWNER_SECURITY_INFORMATION |
                                        GROUP_SECURITY_INFORMATION |
                                        DACL_SECURITY_INFORMATION;
        DWORD needed = 0;
        for (;;)
        {
            const DWORD capacity = (DWORD)sd.size();
            if (::GetFileSecurityW(
                    lp.Get(), si,
                    sd.empty() ? NULL : (PSECURITY_DESCRIPTOR)sd.data(),
                    capacity, &needed))
            {
                if (needed != 0 && needed < sd.size())
                    sd.resize(needed);
                return FileResult::Ok();
            }

            const DWORD error = ::GetLastError();
            if (error != ERROR_INSUFFICIENT_BUFFER || needed == 0 ||
                needed <= capacity)
            {
                sd.clear();
                return FileResult::Error(
                    error != ERROR_SUCCESS ? error : ERROR_ACCESS_DENIED);
            }
            sd.assign(needed, 0);
        }
    }

    FileResult SetPathSecurity(const wchar_t* path, const BYTE* sd, size_t len) override
    {
        if (sd == NULL || len < SECURITY_DESCRIPTOR_MIN_LENGTH)
            return FileResult::Error(ERROR_INVALID_PARAMETER);
        PSECURITY_DESCRIPTOR descriptor =
            (PSECURITY_DESCRIPTOR)const_cast<BYTE*>(sd);
        if (!::IsValidSecurityDescriptor(descriptor) ||
            ::GetSecurityDescriptorLength(descriptor) > len)
            return FileResult::Error(ERROR_INVALID_SECURITY_DESCR);

        PSID owner = NULL;
        PSID group = NULL;
        PACL dacl = NULL;
        BOOL ownerDefaulted = FALSE;
        BOOL groupDefaulted = FALSE;
        BOOL daclPresent = FALSE;
        BOOL daclDefaulted = FALSE;
        SECURITY_DESCRIPTOR_CONTROL control = 0;
        DWORD revision = 0;
        if (!::GetSecurityDescriptorOwner(descriptor, &owner, &ownerDefaulted) ||
            !::GetSecurityDescriptorGroup(descriptor, &group, &groupDefaulted) ||
            !::GetSecurityDescriptorDacl(descriptor, &daclPresent, &dacl, &daclDefaulted) ||
            !::GetSecurityDescriptorControl(descriptor, &control, &revision))
        {
            return FileResult::Error(::GetLastError());
        }

        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        const bool inheritedDacl = (control & SE_DACL_PROTECTED) == 0;
        const SECURITY_INFORMATION securityInfo =
            OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
            DACL_SECURITY_INFORMATION |
            (inheritedDacl ? UNPROTECTED_DACL_SECURITY_INFORMATION
                           : PROTECTED_DACL_SECURITY_INFORMATION);
        auto setParts = [&](SECURITY_INFORMATION info, PSID partOwner,
                            PSID partGroup, PACL partDacl) -> DWORD {
            return ::SetNamedSecurityInfoW(const_cast<LPWSTR>(lp.Get()),
                                           SE_FILE_OBJECT, info, partOwner,
                                           partGroup, partDacl, NULL);
        };

        const DWORD initialError = setParts(
            securityInfo, owner, group, daclPresent ? dacl : NULL);
        if (initialError == ERROR_SUCCESS)
            return FileResult::Ok();

        // Preserve the worker's historical recovery behavior without leaking
        // LocalFree-owned security pointers into core. If setting all three
        // components at once fails, accept already-equal owner/group values,
        // and otherwise apply each component separately with the DACL last.
        std::vector<BYTE> targetDescriptor;
        PSID targetOwner = NULL;
        PSID targetGroup = NULL;
        PACL targetDacl = NULL;
        auto readTarget = [&]() -> bool {
            if (!GetPathSecurity(path, targetDescriptor).success)
                return false;
            BOOL targetOwnerDefaulted = FALSE;
            BOOL targetGroupDefaulted = FALSE;
            BOOL targetDaclPresent = FALSE;
            BOOL targetDaclDefaulted = FALSE;
            return ::GetSecurityDescriptorOwner(targetDescriptor.data(), &targetOwner,
                                                &targetOwnerDefaulted) &&
                   ::GetSecurityDescriptorGroup(targetDescriptor.data(), &targetGroup,
                                                &targetGroupDefaulted) &&
                   ::GetSecurityDescriptorDacl(targetDescriptor.data(), &targetDaclPresent,
                                               &targetDacl, &targetDaclDefaulted);
        };
        bool targetRead = readTarget();

        std::vector<BYTE> tokenUserStorage;
        PSID currentUser = NULL;
        HANDLE token = NULL;
        if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            DWORD needed = 0;
            ::GetTokenInformation(token, TokenUser, NULL, 0, &needed);
            if (needed != 0)
            {
                tokenUserStorage.assign(needed, 0);
                if (::GetTokenInformation(token, TokenUser,
                                          tokenUserStorage.data(), needed, &needed))
                {
                    currentUser =
                        ((TOKEN_USER*)tokenUserStorage.data())->User.Sid;
                }
            }
            ::CloseHandle(token);
        }

        bool ownerOfFile = targetRead && targetOwner != NULL &&
                           currentUser != NULL &&
                           ::EqualSid(targetOwner, currentUser);
        if (!ownerOfFile && currentUser != NULL &&
            setParts(OWNER_SECURITY_INFORMATION, currentUser, NULL, NULL) ==
                ERROR_SUCCESS)
        {
            ownerOfFile = true;
            targetRead = readTarget();
        }

        const SECURITY_INFORMATION daclInfo =
            DACL_SECURITY_INFORMATION |
            (inheritedDacl ? UNPROTECTED_DACL_SECURITY_INFORMATION
                           : PROTECTED_DACL_SECURITY_INFORMATION);
        bool ownerOk = false;
        bool groupOk = false;
        bool daclOk = false;

        if (ownerOfFile && currentUser != NULL)
        {
            const DWORD aclSize = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) -
                                  sizeof(DWORD) + ::GetLengthSid(currentUser);
            std::vector<BYTE> aclStorage(aclSize, 0);
            PACL temporaryDacl = (PACL)aclStorage.data();
            if (::InitializeAcl(temporaryDacl, aclSize, ACL_REVISION) &&
                ::AddAccessAllowedAce(temporaryDacl, ACL_REVISION,
                                      READ_CONTROL | WRITE_DAC | WRITE_OWNER,
                                      currentUser) &&
                setParts(DACL_SECURITY_INFORMATION |
                             PROTECTED_DACL_SECURITY_INFORMATION,
                         NULL, NULL, temporaryDacl) == ERROR_SUCCESS)
            {
                ownerOk = setParts(OWNER_SECURITY_INFORMATION, owner, NULL,
                                   NULL) == ERROR_SUCCESS;
                groupOk = setParts(GROUP_SECURITY_INFORMATION, NULL, group,
                                   NULL) == ERROR_SUCCESS;
                daclOk = setParts(daclInfo, NULL, NULL,
                                  daclPresent ? dacl : NULL) == ERROR_SUCCESS;
            }
        }

        if (!ownerOk)
        {
            ownerOk = setParts(OWNER_SECURITY_INFORMATION, owner, NULL, NULL) ==
                          ERROR_SUCCESS ||
                      targetRead &&
                          ((owner == NULL && targetOwner == NULL) ||
                           (owner != NULL && targetOwner != NULL &&
                            ::EqualSid(owner, targetOwner)));
        }
        if (!groupOk)
        {
            groupOk = setParts(GROUP_SECURITY_INFORMATION, NULL, group, NULL) ==
                          ERROR_SUCCESS ||
                      targetRead &&
                          ((group == NULL && targetGroup == NULL) ||
                           (group != NULL && targetGroup != NULL &&
                            ::EqualSid(group, targetGroup)));
        }
        if (!daclOk)
        {
            daclOk = setParts(daclInfo, NULL, NULL,
                              daclPresent ? dacl : NULL) == ERROR_SUCCESS;
        }

        return ownerOk && groupOk && daclOk
                   ? FileResult::Ok()
                   : FileResult::Error(initialError);
    }

    // --- P2-a move + volume ----------------------------------------------

    FileResult MoveFileWithFlags(const wchar_t* source, const wchar_t* target, MoveFlags flags) override
    {
        LongPath s(source);
        LongPath t(target);
        if (!::MoveFileExW(s.Get(), t.Get(), MoveFlagsToWin32(flags)))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult ScheduleDeleteOnReboot(const wchar_t* path) override
    {
        // Do NOT wrap with LongPath / \\?\ here: this value is handed to the Session Manager
        // (HKLM ...\PendingFileRenameOperations) and MoveFileEx performs its own DOS->NT
        // conversion; a \\?\ prefix can leave a non-canonical pending entry. Requires admin.
        if (!::MoveFileExW(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult GetDiskFree(const wchar_t* path, uint64_t* freeForCaller, uint64_t* totalBytes) override
    {
        LongPath lp(path);
        ULARGE_INTEGER freeAvail = {}, total = {}, freeTotal = {};
        if (!::GetDiskFreeSpaceExW(lp.Get(), &freeAvail, &total, &freeTotal))
            return FileResult::Error(::GetLastError());
        if (freeForCaller) *freeForCaller = freeAvail.QuadPart;
        if (totalBytes)    *totalBytes = total.QuadPart;
        return FileResult::Ok();
    }

    FileResult QueryVolumeCapabilities(const wchar_t* path, VolumeCapabilities& caps) override
    {
        LongPath lp(path);
        if (!lp.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));

        try
        {
            std::wstring root;
            DWORD rootCapacity = 256;
            for (;;)
            {
                root.assign(rootCapacity, L'\0');
                if (::GetVolumePathNameW(lp.Get(), root.data(), rootCapacity))
                {
                    root.resize(wcslen(root.c_str()));
                    break;
                }
                const DWORD error = ::GetLastError();
                if (error != ERROR_FILENAME_EXCED_RANGE && error != ERROR_INSUFFICIENT_BUFFER &&
                    error != ERROR_MORE_DATA)
                    return FileResult::Error(error);
                if (rootCapacity > (std::numeric_limits<DWORD>::max)() / 2)
                    return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
                rootCapacity *= 2;
            }

            std::wstring fsName;
            DWORD fsCapacity = 64;
            DWORD flags = 0, maxComp = 0, serial = 0;
            for (;;)
            {
                fsName.assign(fsCapacity, L'\0');
                if (::GetVolumeInformationW(root.c_str(), NULL, 0, &serial, &maxComp, &flags,
                                            fsName.data(), fsCapacity))
                {
                    fsName.resize(wcslen(fsName.c_str()));
                    break;
                }
                const DWORD error = ::GetLastError();
                if (error != ERROR_FILENAME_EXCED_RANGE && error != ERROR_INSUFFICIENT_BUFFER &&
                    error != ERROR_MORE_DATA)
                    return FileResult::Error(error);
                if (fsCapacity > (std::numeric_limits<DWORD>::max)() / 2)
                    return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
                fsCapacity *= 2;
            }
            caps.fileSystemName.swap(fsName);
            caps.flags = flags;

            DWORD sectorsPerCluster = 0, bytesPerSector = 0, freeClusters = 0, totalClusters = 0;
            caps.bytesPerCluster = 0;
            if (::GetDiskFreeSpaceW(root.c_str(), &sectorsPerCluster, &bytesPerSector,
                                    &freeClusters, &totalClusters))
                caps.bytesPerCluster = sectorsPerCluster * bytesPerSector;
            return FileResult::Ok();
        }
        catch (const std::bad_alloc&)
        {
            return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return FileResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
    }

    FileResult SetVolumeLabel(const wchar_t* rootPath, const wchar_t* label) override
    {
        LongPath root(rootPath);
        if (!root.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        if (!::SetVolumeLabelW(root.Get(), label))
            return FileResult::Error(::GetLastError());
        return FileResult::Ok();
    }

    FileResult QueryVolumeTrim(const wchar_t* volume, bool* enabled) override
    {
        if (enabled == NULL)
            return FileResult::Error(ERROR_INVALID_PARAMETER);
        LongPath path(volume);
        if (!path.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        HANDLE handle = ::CreateFileW(path.Get(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE)
            return FileResult::Error(::GetLastError());

        STORAGE_PROPERTY_QUERY query = {};
        query.PropertyId = (STORAGE_PROPERTY_ID)StorageDeviceTrimProperty;
        query.QueryType = PropertyStandardQuery;
        DEVICE_TRIM_DESCRIPTOR descriptor = {};
        DWORD returned = 0;
        const BOOL ok = ::DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY,
                                          &query, sizeof(query), &descriptor, sizeof(descriptor),
                                          &returned, NULL);
        const DWORD error = ok && returned == sizeof(descriptor) ? ERROR_SUCCESS :
                            ok ? ERROR_INVALID_DATA : ::GetLastError();
        ::CloseHandle(handle);
        if (error != ERROR_SUCCESS)
            return FileResult::Error(error);
        *enabled = descriptor.TrimEnabled != 0;
        return FileResult::Ok();
    }

    FileResult QueryVolumeSeekPenalty(const wchar_t* volume, bool* incursPenalty) override
    {
        if (incursPenalty == NULL)
            return FileResult::Error(ERROR_INVALID_PARAMETER);
        LongPath path(volume);
        if (!path.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        HANDLE handle = ::CreateFileW(path.Get(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE)
            return FileResult::Error(::GetLastError());

        STORAGE_PROPERTY_QUERY query = {};
        query.PropertyId = (STORAGE_PROPERTY_ID)StorageDeviceSeekPenaltyProperty;
        query.QueryType = PropertyStandardQuery;
        DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor = {};
        DWORD returned = 0;
        const BOOL ok = ::DeviceIoControl(handle, IOCTL_STORAGE_QUERY_PROPERTY,
                                          &query, sizeof(query), &descriptor, sizeof(descriptor),
                                          &returned, NULL);
        const DWORD error = ok && returned == sizeof(descriptor) ? ERROR_SUCCESS :
                            ok ? ERROR_INVALID_DATA : ::GetLastError();
        ::CloseHandle(handle);
        if (error != ERROR_SUCCESS)
            return FileResult::Error(error);
        *incursPenalty = descriptor.IncursSeekPenalty != 0;
        return FileResult::Ok();
    }

    FileResult QueryVolumeRotationRate(const wchar_t* volume, WORD* rpm) override
    {
        if (rpm == NULL)
            return FileResult::Error(ERROR_INVALID_PARAMETER);
        LongPath path(volume);
        if (!path.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        HANDLE handle = ::CreateFileW(path.Get(), GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                      OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE)
            return FileResult::Error(::GetLastError());

        struct ATAIdentifyDeviceQuery
        {
            ATA_PASS_THROUGH_EX header;
            WORD data[256];
        } query = {};
        query.header.Length = sizeof(query.header);
        query.header.AtaFlags = ATA_FLAGS_DATA_IN;
        query.header.DataTransferLength = sizeof(query.data);
        query.header.TimeOutValue = 3;
        query.header.DataBufferOffset = (DWORD)((BYTE*)&query.data - (BYTE*)&query);
        query.header.CurrentTaskFile[6] = 0xec;
        DWORD returned = 0;
        const BOOL ok = ::DeviceIoControl(handle, IOCTL_ATA_PASS_THROUGH,
                                          &query, sizeof(query), &query, sizeof(query),
                                          &returned, NULL);
        const DWORD error = ok && returned == sizeof(query) ? ERROR_SUCCESS :
                            ok ? ERROR_INVALID_DATA : ::GetLastError();
        ::CloseHandle(handle);
        if (error != ERROR_SUCCESS)
            return FileResult::Error(error);
        *rpm = query.data[217];
        return FileResult::Ok();
    }

    FileResult QueryDriveMediaType(wchar_t driveLetter, DWORD* mediaType) override
    {
        if (mediaType == NULL || !((driveLetter >= L'A' && driveLetter <= L'Z') ||
                                   (driveLetter >= L'a' && driveLetter <= L'z')))
            return FileResult::Error(ERROR_INVALID_PARAMETER);
        wchar_t volume[] = L"\\\\.\\A:";
        volume[4] = driveLetter;
        HANDLE handle = ::CreateFileW(volume, 0, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (handle == INVALID_HANDLE_VALUE)
            return FileResult::Error(::GetLastError());
        DISK_GEOMETRY geometry[20] = {};
        DWORD returned = 0;
        const BOOL ok = ::DeviceIoControl(handle, IOCTL_STORAGE_GET_MEDIA_TYPES, NULL, 0,
                                          geometry, sizeof(geometry), &returned, NULL);
        const DWORD error = ok && returned >= sizeof(DISK_GEOMETRY) ? ERROR_SUCCESS :
                            ok ? ERROR_INVALID_DATA : ::GetLastError();
        ::CloseHandle(handle);
        if (error != ERROR_SUCCESS)
            return FileResult::Error(error);
        *mediaType = (DWORD)geometry[0].MediaType;
        return FileResult::Ok();
    }

    FileResult GetCompressedSize(const wchar_t* path, uint64_t* size) override
    {
        if (size == NULL)
            return FileResult::Error(ERROR_INVALID_PARAMETER);
        LongPath decorated(path);
        if (!decorated.IsValid())
            return FileResult::Error(::GetLastError());
        ULARGE_INTEGER s;
        s.LowPart = ::GetCompressedFileSizeW(decorated.Get(), &s.HighPart);
        if (s.LowPart == INVALID_FILE_SIZE)
        {
            DWORD err = ::GetLastError();
            if (err != NO_ERROR)
                return FileResult::Error(err);
        }
        *size = s.QuadPart;
        return FileResult::Ok();
    }

    // --- reparse points as opaque blobs ----------------------

    FileResult GetReparseData(const wchar_t* path, std::vector<BYTE>& data) override
    {
        data.clear();
        LongPath decorated(path);
        if (!decorated.IsValid())
            return FileResult::Error(::GetLastError());
        HANDLE h = ::CreateFileW(decorated.Get(), FILE_READ_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING,
                                 FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                 NULL);
        if (h == INVALID_HANDLE_VALUE)
            return FileResult::Error(::GetLastError());
        data.resize(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
        DWORD bytes = 0;
        BOOL ok = ::DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0,
                                    data.data(), (DWORD)data.size(), &bytes, NULL);
        DWORD err = ok ? NO_ERROR : ::GetLastError();
        ::CloseHandle(h);
        if (!ok)
        {
            data.clear();
            return FileResult::Error(err);
        }
        data.resize(bytes);
        return FileResult::Ok();
    }

    FileResult SetReparseData(const wchar_t* path, const BYTE* data, size_t len) override
    {
        if (data == NULL || len == 0 ||
            len > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
            return FileResult::Error(ERROR_INVALID_PARAMETER);
        LongPath decorated(path);
        if (!decorated.IsValid())
            return FileResult::Error(::GetLastError());
        HANDLE h = ::CreateFileW(decorated.Get(), GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                 FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                 NULL);
        if (h == INVALID_HANDLE_VALUE)
            return FileResult::Error(::GetLastError());
        DWORD bytes = 0;
        BOOL ok = ::DeviceIoControl(h, FSCTL_SET_REPARSE_POINT,
                                    const_cast<BYTE*>(data), (DWORD)len,
                                    NULL, 0, &bytes, NULL);
        DWORD err = ok ? NO_ERROR : ::GetLastError();
        ::CloseHandle(h);
        return ok ? FileResult::Ok() : FileResult::Error(err);
    }

    FileResult DeleteDirectoryReparseData(const wchar_t* path) override
    {
        std::vector<BYTE> data;
        const FileResult readResult = GetReparseData(path, data);
        if (!readResult.success)
            return readResult;
        if (data.size() < sizeof(DWORD))
            return FileResult::Error(ERROR_INVALID_REPARSE_DATA);

        const DWORD tag = *(const DWORD*)data.data();
        if (tag != IO_REPARSE_TAG_MOUNT_POINT && tag != IO_REPARSE_TAG_SYMLINK)
            return FileResult::Error(ERROR_REPARSE_TAG_MISMATCH);

        LongPath decorated(path);
        if (!decorated.IsValid())
            return FileResult::Error(LastErrorOr(ERROR_INVALID_PARAMETER));
        HANDLE h = ::CreateFileW(decorated.Get(), GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE |
                                     FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING,
                                 FILE_FLAG_OPEN_REPARSE_POINT |
                                     FILE_FLAG_BACKUP_SEMANTICS,
                                 NULL);
        if (h == INVALID_HANDLE_VALUE)
            return FileResult::Error(::GetLastError());

        REPARSE_GUID_DATA_BUFFER header = {};
        header.ReparseTag = tag;
        DWORD bytes = 0;
        const BOOL ok = ::DeviceIoControl(
            h, FSCTL_DELETE_REPARSE_POINT, &header,
            REPARSE_GUID_DATA_BUFFER_HEADER_SIZE, NULL, 0, &bytes, NULL);
        const DWORD error = ok ? ERROR_SUCCESS : ::GetLastError();
        ::CloseHandle(h);
        return ok ? FileResult::Ok() : FileResult::Error(error);
    }
};

// Singleton instance
static Win32FileSystem g_win32FileSystem;
IFileSystem* gFileSystem = &g_win32FileSystem;

IFileSystem* GetWin32FileSystem()
{
    return &g_win32FileSystem;
}

DWORD MoveFlagsToWin32(MoveFlags flags)
{
    DWORD win = 0;
    if (HasFlag(flags, MoveFlags::ReplaceExisting)) win |= MOVEFILE_REPLACE_EXISTING;
    if (HasFlag(flags, MoveFlags::CopyAllowed)) win |= MOVEFILE_COPY_ALLOWED;
    if (HasFlag(flags, MoveFlags::WriteThrough)) win |= MOVEFILE_WRITE_THROUGH;
    if (HasFlag(flags, MoveFlags::DelayUntilReboot)) win |= MOVEFILE_DELAY_UNTIL_REBOOT;
    return win;
}
