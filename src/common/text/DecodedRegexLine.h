// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/Win32TextCodec.h"
#include "common/unicode/ViewerBomText.h"
#include <algorithm>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace sally::text
{
    // The retained regex engine consumes UTF-8 bytes. Its matches are projected back
    // to whole original decoded cells so neither UTF-16 units nor UTF-8 bytes become
    // partial selections. This adapter does not change the engine's byte-domain dialect.
    class DecodedRegexLine
    {
        struct Cell
        {
            size_t Byte;
            std::int64_t RawStart, RawEnd;
        };
        std::vector<Cell> Cells;

    public:
        struct Match
        {
            std::int64_t RawStart = 0, RawEnd = 0;
            size_t ByteStart = 0, ByteEnd = 0;
        };
        std::string Text;

        bool Build(const Sally::Unicode::DecodedRun& run, DWORD& error)
        {
            Text.clear();
            Cells.clear();
            const auto converted = Win32EncodeText(CP_UTF8, run.Text.data(), run.Text.size(), Text);
            if (!converted.Succeeded())
            {
                error = converted.Win32Error;
                return false;
            }
            if (run.Scalars.size() != run.CellCount() || run.RawEnd.size() != run.CellCount())
            {
                Text.clear();
                error = ERROR_INVALID_DATA;
                return false;
            }
            try
            {
                Cells.reserve(run.CellCount());
                size_t bytes = 0;
                for (size_t i = 0; i < run.CellCount(); ++i)
                {
                    Cells.push_back({bytes, run.RawStart[i], run.RawEnd[i]});
                    const auto scalar = run.Scalars[i] <= 0x10FFFF ? run.Scalars[i] : 0xFFFD;
                    bytes += scalar < 0x80 ? 1 : scalar < 0x800 ? 2
                                             : scalar < 0x10000 ? 3
                                                                : 4;
                }
                if (bytes == Text.size())
                {
                    error = ERROR_SUCCESS;
                    return true;
                }
                error = ERROR_INVALID_DATA;
            }
            catch (const std::bad_alloc&)
            {
                error = ERROR_NOT_ENOUGH_MEMORY;
            }
            catch (const std::length_error&)
            {
                error = ERROR_NOT_ENOUGH_MEMORY;
            }
            Cells.clear();
            Text.clear();
            return false;
        }

        size_t StartAt(std::int64_t rawOffset) const
        {
            const auto cell = std::lower_bound(Cells.begin(), Cells.end(), rawOffset,
                                               [](const Cell& value, std::int64_t offset)
                                               { return value.RawStart < offset; });
            return cell != Cells.end() ? cell->Byte : Text.size();
        }

        bool Map(size_t start, size_t length, Match& match) const
        {
            if (Cells.empty() || length == 0 || start >= Text.size() || length > Text.size() - start)
                return false;
            auto first = std::upper_bound(Cells.begin(), Cells.end(), start,
                                          [](size_t offset, const Cell& cell)
                                          { return offset < cell.Byte; });
            --first;
            const auto after = std::lower_bound(Cells.begin(), Cells.end(), start + length,
                                                [](const Cell& cell, size_t offset)
                                                { return cell.Byte < offset; });
            const auto last = after - 1;
            match.RawStart = first->RawStart;
            match.RawEnd = last->RawEnd;
            match.ByteStart = first->Byte;
            match.ByteEnd = after != Cells.end() ? after->Byte : Text.size();
            return true;
        }
    };
} // namespace sally::text
