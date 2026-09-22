// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "IFileSystem.h"

#include <string>
#include <windows.h>

namespace sally
{
namespace safe_file
{
inline IFileSystem* ActiveFileSystem()
{
    return gFileSystem != nullptr ? gFileSystem : GetWin32FileSystem();
}

class PathContext
{
public:
    explicit PathContext(const wchar_t* fileName, const wchar_t* displayName = nullptr)
        : Name(fileName != nullptr ? fileName : L""),
          DisplayName(displayName != nullptr && displayName[0] != L'\0' ? displayName : Name)
    {
    }

    const wchar_t* DisplayNameW() const { return DisplayName.c_str(); }
    const wchar_t* WideNameW() const { return Name.c_str(); }
    const std::wstring& WideNameRef() const { return Name; }

private:
    std::wstring Name;
    std::wstring DisplayName;
};

inline HANDLE CreateFileExact(const PathContext& path,
                              DWORD desiredAccess,
                              DWORD shareMode,
                              LPSECURITY_ATTRIBUTES securityAttributes,
                              DWORD creationDisposition,
                              DWORD flagsAndAttributes,
                              HANDLE templateFile)
{
    return ActiveFileSystem()->CreateFile(path.WideNameW(), desiredAccess, shareMode,
                                          securityAttributes, creationDisposition,
                                          flagsAndAttributes, templateFile);
}

inline DWORD GetFileAttributesExact(const PathContext& path)
{
    return ActiveFileSystem()->GetFileAttributes(path.WideNameW());
}

inline BOOL SetFileAttributesExact(const PathContext& path, DWORD fileAttributes)
{
    FileResult result = ActiveFileSystem()->SetFileAttributes(path.WideNameW(), fileAttributes);
    if (!result.success)
        SetLastError(result.errorCode);
    return result.success ? TRUE : FALSE;
}

inline BOOL DeleteFileExact(const PathContext& path)
{
    FileResult result = ActiveFileSystem()->DeleteFile(path.WideNameW());
    if (!result.success)
        SetLastError(result.errorCode);
    return result.success ? TRUE : FALSE;
}
} // namespace safe_file
} // namespace sally
