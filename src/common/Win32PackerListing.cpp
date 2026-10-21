// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/Win32PackerListing.h"
#include "common/PackerListingOutput.h"
#include <array>
#include <new>
#include <stdexcept>

DWORD Win32ReadPackerListingLines(HANDLE pipe, const std::function<bool(std::string_view)>& acceptLine)
{
    try
    {
        // This bounds each pipe read, not the accumulated listing line.
        std::array<char, 4096> chunk;
        sally::pack::PackerListingLines lines;
        for (;;)
        {
            DWORD read = 0;
            if (!ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr))
            {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE)
                    break;
                return error;
            }
            if (read == 0)
                break;
            if (!lines.Append(std::string_view(chunk.data(), read), acceptLine))
                return ERROR_NOT_ENOUGH_MEMORY;
        }
        return lines.Finish(acceptLine) ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
    }
    catch (const std::bad_alloc&)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    catch (const std::length_error&)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}
