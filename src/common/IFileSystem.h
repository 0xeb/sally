// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <windows.h>

// File attributes for IFileSystem operations
struct FileInfo
{
    std::wstring name;
    uint64_t size;
    FILETIME creationTime;
    FILETIME lastWriteTime;
    DWORD attributes;
    bool isDirectory;
};

// One named stream attached to a file or directory. Names are semantic
// UTF-16 (for example L":metadata:$DATA"); stream contents remain bytes.
struct FileStreamEntry
{
    std::wstring name;
    uint64_t size;
    uint64_t allocationSize;
};

// Volume capability/geometry info. Portable subset the worker needs to
// decide ADS/compression/encryption/ACL support and cluster rounding, without
// callers touching GetVolumeInformation / DeviceIoControl directly.
struct VolumeCapabilities
{
    std::wstring fileSystemName;   // e.g. "NTFS", "FAT32", "exFAT"
    DWORD flags;                   // raw FILE_* volume flags (as reported by the OS)
    DWORD bytesPerCluster;         // 0 if unknown
    bool SupportsADS() const { return (flags & FILE_NAMED_STREAMS) != 0; }
    bool SupportsCompression() const { return (flags & FILE_FILE_COMPRESSION) != 0; }
    bool SupportsEncryption() const { return (flags & FILE_SUPPORTS_ENCRYPTION) != 0; }
    bool SupportsACLs() const { return (flags & FILE_PERSISTENT_ACLS) != 0; }
};

// Portable flags for MoveFileWithFlags — subset of MOVEFILE_*.
enum class MoveFlags : unsigned
{
    None = 0,
    ReplaceExisting = 1,
    CopyAllowed = 2,
    WriteThrough = 4,
    DelayUntilReboot = 8, // defer the operation to the next reboot (MOVEFILE_DELAY_UNTIL_REBOOT; needs admin)
};
inline MoveFlags operator|(MoveFlags a, MoveFlags b)
{
    return (MoveFlags)((unsigned)a | (unsigned)b);
}
inline bool HasFlag(MoveFlags v, MoveFlags f)
{
    return ((unsigned)v & (unsigned)f) != 0;
}

// Result of file operations
struct FileResult
{
    bool success;
    DWORD errorCode;  // Win32 error code on failure

    static FileResult Ok() { return {true, 0}; }
    static FileResult Error(DWORD err) { return {false, err}; }
};

// Abstract interface for file system operations
// Enables mocking for tests and potential future OS abstraction
class IFileSystem
{
public:
    virtual ~IFileSystem() {}

    // File existence and info
    virtual bool FileExists(const wchar_t* path) = 0;
    virtual bool DirectoryExists(const wchar_t* path) = 0;
    virtual FileResult GetFileInfo(const wchar_t* path, FileInfo& info) = 0;

    // File attributes
    virtual DWORD GetFileAttributes(const wchar_t* path) = 0;  // Returns INVALID_FILE_ATTRIBUTES on error
    virtual FileResult SetFileAttributes(const wchar_t* path, DWORD attributes) = 0;

    // File operations
    virtual FileResult DeleteFile(const wchar_t* path) = 0;
    virtual FileResult MoveFile(const wchar_t* source, const wchar_t* target) = 0;
    virtual FileResult CopyFile(const wchar_t* source, const wchar_t* target, bool failIfExists) = 0;

    // Directory operations
    virtual FileResult CreateDirectory(const wchar_t* path) = 0;
    virtual FileResult CreateDirectoryWithSecurity(const wchar_t* path,
                                                   LPSECURITY_ATTRIBUTES securityAttributes)
    {
        if (securityAttributes == NULL)
            return this->CreateDirectory(path);
        (void)path;
        return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED);
    }
    virtual FileResult RemoveDirectory(const wchar_t* path) = 0;

    // Generic file open/create operation
    virtual HANDLE CreateFile(const wchar_t* path,
                              DWORD desiredAccess,
                              DWORD shareMode,
                              LPSECURITY_ATTRIBUTES securityAttributes,
                              DWORD creationDisposition,
                              DWORD flagsAndAttributes,
                              HANDLE templateFile) = 0;

    // File enumeration handle operations
    virtual HANDLE FindFirstFile(const wchar_t* path, WIN32_FIND_DATAW* findData) = 0;
    virtual BOOL FindNextFile(HANDLE findHandle, WIN32_FIND_DATAW* findData) = 0;
    virtual FileResult CloseFind(HANDLE findHandle)
    { (void)findHandle; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // File handle operations (for copy loops, etc.)
    virtual HANDLE OpenFileForRead(const wchar_t* path, DWORD shareMode = FILE_SHARE_READ) = 0;
    virtual HANDLE CreateFileForWrite(const wchar_t* path, bool failIfExists) = 0;
    virtual FileResult CloseFileHandle(HANDLE h) = 0;

    // --- handle I/O ops --------------------------
    // Named to avoid the Win32 A/W macro trick used above; new interface
    // surface prefers distinct names. Default impls fail with
    // ERROR_CALL_NOT_IMPLEMENTED so mocks need only override what they inject.

    // Read up to 'toRead' bytes; '*read' receives the count (0 at EOF).
    virtual FileResult ReadFromHandle(HANDLE h, void* buffer, DWORD toRead, DWORD* read)
    { (void)h; (void)buffer; (void)toRead; if (read) *read = 0; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Write 'toWrite' bytes; '*written' receives the count.
    virtual FileResult WriteToHandle(HANDLE h, const void* buffer, DWORD toWrite, DWORD* written)
    { (void)h; (void)buffer; (void)toWrite; if (written) *written = 0; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Start overlapped I/O. ERROR_IO_PENDING is returned explicitly when the
    // request was queued; callers complete it through CompleteHandleIo.
    virtual FileResult ReadFromHandleOverlapped(HANDLE h, void* buffer, DWORD toRead,
                                                 OVERLAPPED* overlapped)
    { (void)h; (void)buffer; (void)toRead; (void)overlapped; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult WriteToHandleOverlapped(HANDLE h, const void* buffer, DWORD toWrite,
                                                OVERLAPPED* overlapped)
    { (void)h; (void)buffer; (void)toWrite; (void)overlapped; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult CompleteHandleIo(HANDLE h, OVERLAPPED* overlapped,
                                         DWORD* transferred, bool wait)
    { (void)h; (void)overlapped; if (transferred) *transferred = 0; (void)wait; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult CancelHandleIo(HANDLE h)
    { (void)h; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Move the file pointer. 'moveMethod' is FILE_BEGIN/CURRENT/END.
    // '*newPos' (optional) receives the resulting absolute position.
    virtual FileResult SeekHandle(HANDLE h, int64_t distance, DWORD moveMethod, uint64_t* newPos)
    { (void)h; (void)distance; (void)moveMethod; (void)newPos; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Set creation/access/write times (any pointer may be NULL to leave as-is).
    virtual FileResult SetHandleFileTime(HANDLE h, const FILETIME* creation,
                                         const FILETIME* lastAccess, const FILETIME* lastWrite)
    { (void)h; (void)creation; (void)lastAccess; (void)lastWrite; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    virtual FileResult GetHandleFileTime(HANDLE h, FILETIME* creation,
                                         FILETIME* lastAccess, FILETIME* lastWrite)
    { (void)h; (void)creation; (void)lastAccess; (void)lastWrite; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // '*size' receives the file size in bytes.
    virtual FileResult GetHandleFileSize(HANDLE h, uint64_t* size)
    { (void)h; if (size) *size = 0; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Enumerate named streams on an already-open file/directory handle.
    // Mutable OS storage and growth retries remain adapter-owned.
    virtual FileResult GetHandleStreams(HANDLE h,
                                        std::vector<FileStreamEntry>& streams)
    { (void)h; streams.clear(); return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Flush buffered writes to disk.
    virtual FileResult FlushHandle(HANDLE h)
    { (void)h; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Truncate/extend the file to the current pointer position (SetEndOfFile).
    virtual FileResult SetHandleEnd(HANDLE h)
    { (void)h; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // --- semantic file attributes (replace raw DeviceIoControl) ------

    // NTFS transparent compression on an open handle.
    virtual FileResult SetHandleCompression(HANDLE h, bool compress)
    { (void)h; (void)compress; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult SetHandleCompressionOverlapped(HANDLE h, bool compress,
                                                       OVERLAPPED* overlapped)
    { (void)h; (void)compress; (void)overlapped; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Mark an open handle's file as sparse.
    virtual FileResult SetHandleSparse(HANDLE h, bool sparse)
    { (void)h; (void)sparse; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // EFS encrypt/decrypt by path.
    virtual FileResult EncryptPath(const wchar_t* path)
    { (void)path; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult DecryptPath(const wchar_t* path)
    { (void)path; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // --- security as opaque self-relative SD blob --------------------
    // Core never parses the descriptor — it just copies it source→target.
    virtual FileResult GetPathSecurity(const wchar_t* path, std::vector<BYTE>& sd)
    { (void)path; sd.clear(); return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult SetPathSecurity(const wchar_t* path, const BYTE* sd, size_t len)
    { (void)path; (void)sd; (void)len; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // --- move with flags + volume queries ----------------------------
    virtual FileResult MoveFileWithFlags(const wchar_t* source, const wchar_t* target, MoveFlags flags)
    { (void)source; (void)target; (void)flags; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Schedule 'path' for deletion on the next reboot — for files a running process still holds
    // (e.g. a shell-extension DLL loaded by Explorer, see issue #82). Requires administrator rights;
    // returns the Win32 error (typically ERROR_ACCESS_DENIED) when unprivileged so callers degrade.
    virtual FileResult ScheduleDeleteOnReboot(const wchar_t* path)
    { (void)path; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // '*freeForCaller' / '*totalBytes' (either may be NULL) in bytes.
    virtual FileResult GetDiskFree(const wchar_t* path, uint64_t* freeForCaller, uint64_t* totalBytes)
    { (void)path; (void)freeForCaller; (void)totalBytes; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    virtual FileResult QueryVolumeCapabilities(const wchar_t* path, VolumeCapabilities& caps)
    { (void)path; (void)caps; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    virtual FileResult SetVolumeLabel(const wchar_t* rootPath, const wchar_t* label)
    { (void)rootPath; (void)label; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Storage-media hints used by the SSD heuristic. Raw storage IOCTLs stay in
    // the Win32 adapter; callers receive only the semantic value.
    virtual FileResult QueryVolumeTrim(const wchar_t* volume, bool* enabled)
    { (void)volume; if (enabled) *enabled = false; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult QueryVolumeSeekPenalty(const wchar_t* volume, bool* incursPenalty)
    { (void)volume; if (incursPenalty) *incursPenalty = true; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult QueryVolumeRotationRate(const wchar_t* volume, WORD* rpm)
    { (void)volume; if (rpm) *rpm = 0; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
    virtual FileResult QueryDriveMediaType(wchar_t driveLetter, DWORD* mediaType)
    { (void)driveLetter; if (mediaType) *mediaType = 0; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Compressed/sparse on-disk size (GetCompressedFileSizeW).
    // Returns the LOGICAL size for uncompressed files; INVALID means error.
    virtual FileResult GetCompressedSize(const wchar_t* path, uint64_t* size)
    { (void)path; if (size) *size = 0; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // --- reparse points as opaque blobs ----------------------
    // Core never parses reparse data — copying a link means cloning the raw
    // buffer (works identically for junctions, mount points and directory
    // symlinks, preserving exact semantics). Reading and cloning a JUNCTION
    // needs no privilege; only symlink CREATION is privilege-gated by Windows
    // (the Set call then fails with ERROR_PRIVILEGE_NOT_HELD and callers
    // degrade/refuse).
    virtual FileResult GetReparseData(const wchar_t* path, std::vector<BYTE>& data)
    { (void)path; data.clear(); return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // 'path' must be an existing EMPTY directory (create it first); on success
    // it becomes a link carrying exactly the given reparse buffer.
    virtual FileResult SetReparseData(const wchar_t* path, const BYTE* data, size_t len)
    { (void)path; (void)data; (void)len; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Remove a directory junction or symbolic-link reparse point without
    // following its target. Other reparse tags are rejected explicitly.
    virtual FileResult DeleteDirectoryReparseData(const wchar_t* path)
    { (void)path; return FileResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
};

// Global file system instance - default is Win32 implementation
extern IFileSystem* gFileSystem;

// Returns the default Win32 implementation
IFileSystem* GetWin32FileSystem();

// Maps portable MoveFlags to the Win32 MOVEFILE_* bitmask (exposed so the mapping is unit-testable).
DWORD MoveFlagsToWin32(MoveFlags flags);
