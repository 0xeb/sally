// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "IFileSystem.h"
#include <cstdint>
#include <limits>

namespace sally::filelist
{
    // The caller owns/closes the handle. Its exclusive write sharing prevents
    // another writer from appending between the saved length and rollback.
    class OutputTransaction
    {
        IFileSystem& Files;
        HANDLE File;
        bool Append;
        bool Existed;
        bool Prepared = false;
        std::uint64_t OriginalSize = 0;

    public:
        OutputTransaction(IFileSystem& files, HANDLE file, bool append, bool existed)
            : Files(files), File(file), Append(append), Existed(existed) {}
        OutputTransaction(const OutputTransaction&) = delete;
        OutputTransaction& operator=(const OutputTransaction&) = delete;

        FileResult Prepare()
        {
            if (Append)
            {
                const auto result = Files.GetHandleFileSize(File, &OriginalSize);
                if (!result.success)
                    return result;
                if (OriginalSize > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
                    return FileResult::Error(ERROR_ARITHMETIC_OVERFLOW);
            }
            const auto seek = Files.SeekHandle(File, Append ? static_cast<std::int64_t>(OriginalSize) : 0, FILE_BEGIN, nullptr);
            Prepared = seek.success;
            return seek;
        }

        FileResult Commit()
        {
            if (!Prepared)
                return FileResult::Error(ERROR_INVALID_STATE);
            const auto flushed = Files.FlushHandle(File);
            if (flushed.success)
                Prepared = false;
            return flushed;
        }

        FileResult Rollback()
        {
            if (!Prepared || !Append || !Existed)
                return FileResult::Ok();
            const auto seek = Files.SeekHandle(File, static_cast<std::int64_t>(OriginalSize), FILE_BEGIN, nullptr);
            if (!seek.success)
                return seek;
            const auto truncated = Files.SetHandleEnd(File);
            if (!truncated.success)
                return truncated;
            const auto flushed = Files.FlushHandle(File);
            if (flushed.success)
                Prepared = false;
            return flushed;
        }

        bool MayDeleteFailedOutput() const { return !Existed; }
    };
} // namespace sally::filelist
