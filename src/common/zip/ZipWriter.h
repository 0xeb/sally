// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Writes a ZIP archive of whole files, deflated, with UTF-8 names. Classic ZIP only:
// a file or an archive that would need ZIP64 (4 GB) is refused.

#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace Sally::Zip
{

class ZipWriter
{
public:
    ZipWriter() = default;
    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;
    // An archive that was created but not finished is deleted.
    ~ZipWriter();

    bool Create(const std::wstring& archivePath);
    // entryName is the path inside the archive; '\' and '/' both separate folders.
    bool AddFile(const std::wstring& sourcePath, const std::wstring& entryName);
    bool Finish();

    // English diagnostics of the first failure, and its Win32 error code (0 if none).
    const std::wstring& ErrorText() const { return m_errorText; }
    DWORD ErrorCode() const { return m_errorCode; }

private:
    bool Fail(const std::wstring& text, DWORD code);
    bool WriteBytes(const void* data, size_t size);

    HANDLE m_file = INVALID_HANDLE_VALUE;
    std::wstring m_path;
    std::string m_centralDirectory;
    uint64_t m_offset = 0;
    uint32_t m_entryCount = 0;
    bool m_finished = false;
    std::wstring m_errorText;
    DWORD m_errorCode = 0;
};

} // namespace Sally::Zip
