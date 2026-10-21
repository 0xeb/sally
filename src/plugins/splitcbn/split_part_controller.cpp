// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "split_part_controller.h"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace sally::split
{
    PartResult RunParts(const Plan& plan, size_t bufferBytes, bool checksum, IPartIo& io)
    {
        PartResult result;
        bool open = false;
        auto fail = [&](DWORD error = ERROR_SUCCESS)
        {
            if (open)
            {
                open = false;
                io.ClosePart(false);
            }
            if (error != ERROR_SUCCESS)
                io.Error(error);
            return result;
        };
        if (!plan.Valid() || bufferBytes == 0 || bufferBytes > (std::numeric_limits<DWORD>::max)())
            return fail(ERROR_INVALID_PARAMETER);
        try
        {
            std::vector<BYTE> buffer(bufferBytes);
            uint64_t remaining = plan.Total;
            const bool automatic = plan.Kind == Mode::Automatic;
            while (remaining != 0)
            {
                if (result.Parts >= static_cast<unsigned>((std::numeric_limits<int>::max)() - 1))
                    return fail(ERROR_ARITHMETIC_OVERFLOW);
                // Poll even when the next part will be skipped, or no data has
                // been copied yet. Cancellation must precede output creation.
                if (!io.Progress(0, result.Bytes))
                    return fail();
                const uint64_t wanted = plan.Wanted(result.Parts, remaining);
                uint64_t bytes = 0;
                if (!io.Prepare(remaining, wanted, automatic, bytes))
                    return fail();
                if (bytes == 0)
                    return fail(ERROR_DISK_FULL);
                if (bytes > remaining || (!automatic && bytes != wanted))
                    return fail(ERROR_INVALID_DATA);
                io.SetPartTotal(bytes);
                if (!io.Progress(0, result.Bytes))
                    return fail();
                const auto disposition = io.OpenPart(result.Parts + 1, bytes);
                if (disposition == PartDisposition::Cancelled)
                    return fail();
                if (disposition != PartDisposition::Open && disposition != PartDisposition::Skipped)
                    return fail(ERROR_INVALID_DATA);
                open = disposition == PartDisposition::Open;
                uint64_t done = 0;
                if (!open && !checksum)
                {
                    if (!io.Skip(bytes))
                        return fail();
                    if (!io.Progress(bytes, result.Bytes + bytes))
                        return fail();
                }
                else
                {
                    while (done < bytes)
                    {
                        const size_t requested = static_cast<size_t>((std::min)(bytes - done, static_cast<uint64_t>(buffer.size())));
                        size_t read = 0;
                        if (!io.Read(buffer.data(), requested, read))
                            return fail();
                        if (read == 0)
                            return fail(ERROR_HANDLE_EOF);
                        if (read > requested)
                            return fail(ERROR_INVALID_DATA);
                        if (open)
                        {
                            size_t written = 0;
                            if (!io.Write(buffer.data(), read, written))
                                return fail();
                            if (written != read)
                                return fail(ERROR_WRITE_FAULT);
                        }
                        if (checksum)
                            io.Checksum(buffer.data(), read);
                        done += read;
                        if (!io.Progress(done, result.Bytes + done))
                            return fail();
                    }
                }
                if (open)
                {
                    open = false;
                    io.ClosePart(true);
                }
                remaining -= bytes;
                result.Bytes += bytes;
                ++result.Parts;
                if (!io.AfterPart(remaining, plan.Wanted(result.Parts, remaining), automatic))
                    return fail();
            }
            result.Success = true;
            return result;
        }
        catch (const std::bad_alloc&)
        {
            return fail(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return fail(ERROR_FILE_TOO_LARGE);
        }
    }
} // namespace sally::split
