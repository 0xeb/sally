// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <new>
#include <stdexcept>
#include <string>
#include <vector>

// Plugin-owned local paths are live UTF-16 identities. Decorate them only at the
// Win32 file-I/O boundary, after dynamically resolving ordinary relative spellings.
// Archive member names and protocol payloads must not pass through this adapter.
inline bool PreparePluginLocalPathForIo(const wchar_t* fileName, std::wstring& ioPath)
{
    if (fileName == NULL || fileName[0] == L'\0')
    {
        SetLastError(ERROR_INVALID_NAME);
        return false;
    }

    try
    {
        const std::wstring source(fileName);
        std::wstring prepared;
        if (source.compare(0, 4, L"\\\\?\\") == 0 ||
            source.compare(0, 4, L"\\\\.\\") == 0)
        {
            prepared = source;
        }
        else
        {
            DWORD capacity = GetFullPathNameW(source.c_str(), 0, NULL, NULL);
            if (capacity == 0)
                return false;

            std::vector<wchar_t> buffer(capacity);
            std::wstring absolute;
            for (;;)
            {
                const DWORD written = GetFullPathNameW(
                    source.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), NULL);
                if (written == 0)
                    return false;
                if (written < buffer.size())
                {
                    absolute.assign(buffer.data(), written);
                    break;
                }
                buffer.resize(static_cast<size_t>(written) + 1);
            }

            for (wchar_t& ch : absolute)
                if (ch == L'/')
                    ch = L'\\';
            prepared = absolute.compare(0, 2, L"\\\\") == 0
                           ? L"\\\\?\\UNC\\" + absolute.substr(2)
                           : L"\\\\?\\" + absolute;
        }
        ioPath.swap(prepared);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}
