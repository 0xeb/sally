// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "viewer_text_file.h"

#include <algorithm>

namespace Sally::Viewer
{

Win32HandleReader::~Win32HandleReader()
{
    Close();
}

void Win32HandleReader::Attach(Opener opener, Closer closer, std::int64_t expectedSize)
{
    Close();
    m_opener = std::move(opener);
    m_closer = std::move(closer);
    m_expectedSize = expectedSize;
}

bool Win32HandleReader::Open()
{
    if (m_handle != INVALID_HANDLE_VALUE)
        return true;
    if (!m_opener)
        return false;
    HANDLE handle = m_opener();
    if (handle == INVALID_HANDLE_VALUE || handle == NULL)
        return false;
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart != m_expectedSize)
    {
        if (m_closer)
            m_closer(handle);
        return false;
    }
    m_handle = handle;
    return true;
}

void Win32HandleReader::Close()
{
    if (m_handle == INVALID_HANDLE_VALUE)
        return;
    if (m_closer)
        m_closer(m_handle);
    m_handle = INVALID_HANDLE_VALUE;
}

bool Win32HandleReader::ReadAt(std::int64_t offset, void* buffer, std::size_t size, std::size_t& read)
{
    read = 0;
    if (m_handle == INVALID_HANDLE_VALUE || offset < 0)
        return false;
    std::uint8_t* out = static_cast<std::uint8_t*>(buffer);
    while (read < size)
    {
        const DWORD want = (DWORD)std::min<std::size_t>(size - read, 0x40000000u);
        OVERLAPPED at = {};
        const std::uint64_t position = (std::uint64_t)offset + read;
        at.Offset = (DWORD)position;
        at.OffsetHigh = (DWORD)(position >> 32);
        DWORD got = 0;
        if (!ReadFile(m_handle, out + read, want, &got, &at))
            return GetLastError() == ERROR_HANDLE_EOF;
        if (got == 0)
            break;
        read += got;
    }
    return true;
}

} // namespace Sally::Viewer
