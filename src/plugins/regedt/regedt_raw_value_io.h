// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <cstdlib>
#include <memory>

namespace sally::regedt
{
    inline bool WriteRawValue(HANDLE file, const BYTE* data, DWORD size, DWORD& error,
                              decltype(&::WriteFile) write = ::WriteFile)
    {
        DWORD written = 0;
        if (!write(file, data, size, &written, nullptr))
        {
            error = GetLastError();
            return false;
        }
        error = written == size ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
        return error == ERROR_SUCCESS;
    }

    // Commit the replacement only after an exact successful read. Failure must
    // leave the previous registry payload, size and allocation authoritative.
    inline bool ReadRawValue(HANDLE file, DWORD size, BYTE*& data, DWORD& dataSize, DWORD& allocated, DWORD& error,
                             decltype(&::ReadFile) read = ::ReadFile)
    {
        std::unique_ptr<BYTE, decltype(&std::free)> replacement(
            static_cast<BYTE*>(std::malloc(size ? size : 1)), &std::free);
        if (!replacement)
        {
            error = ERROR_NOT_ENOUGH_MEMORY;
            return false;
        }
        DWORD received = 0;
        if (!read(file, replacement.get(), size, &received, nullptr))
        {
            error = GetLastError();
            return false;
        }
        if (received != size)
        {
            error = ERROR_HANDLE_EOF;
            return false;
        }
        std::free(data);
        data = replacement.release();
        dataSize = allocated = size;
        error = ERROR_SUCCESS;
        return true;
    }
} // namespace sally::regedt
