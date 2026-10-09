// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>

#include "viewer_text_core.h"

namespace Sally::Viewer
{

// Reads a file through a handle that the owner opens and closes. Every open checks the size
// the file had when the viewer read it, so a file that changed underneath reads as an error
// instead of shifting the text.
class Win32HandleReader : public IRandomAccessReader
{
public:
    using Opener = std::function<HANDLE()>;
    using Closer = std::function<void(HANDLE)>;

    Win32HandleReader() = default;
    ~Win32HandleReader() override;
    Win32HandleReader(const Win32HandleReader&) = delete;
    Win32HandleReader& operator=(const Win32HandleReader&) = delete;

    // The file to read and the size it must have. Closes a handle still open.
    void Attach(Opener opener, Closer closer, std::int64_t expectedSize);

    bool Open() override;
    void Close() override;
    bool ReadAt(std::int64_t offset, void* buffer, std::size_t size, std::size_t& read) override;

private:
    Opener m_opener;
    Closer m_closer;
    std::int64_t m_expectedSize = 0;
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

} // namespace Sally::Viewer
