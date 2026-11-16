// The unique name under which a viewed remote file is kept in Sally's disk cache. No Sally or UI headers.
#pragma once

#include <cstdint>
#include <string>

// "<fullFsPath>|<size>|<mtime>". fullFsPath is the whole FS path of the file ("sftp:user@host/dir/file", as FullFsPath
// builds it), size in bytes and mtime in Unix seconds as the listing shows them. Sally's cache only compares these names
// (case-sensitive, never turns them into a disk path), so any character is allowed. A file that changed on the server
// has another size or time, so it gets another name and the old copy is not served again. The two numbers are always the
// last two fields, so names of different files never collide.
std::wstring BuildCacheName(const std::wstring& fullFsPath, uint64_t size, int64_t mtime);
