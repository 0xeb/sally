// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// Old-GNU sparse records describe logical extents; their concatenated bytes
// alone occupy the archive payload. Validate the entire map before publishing it.
struct CTarSparseExtent
{
    std::uint64_t Offset;
    std::uint64_t Length;
};

struct CTarSparseMap
{
    bool Active = false;
    std::uint64_t StoredSize = 0;
    std::uint64_t LogicalSize = 0;
    std::uint64_t MappedSize = 0;
    std::vector<CTarSparseExtent> Extents;

    void Reset()
    {
        Active = false;
        StoredSize = LogicalSize = MappedSize = 0;
        Extents.clear();
    }

    static bool ReadNumber(const char* field, size_t length, std::uint64_t& value)
    {
        value = 0;
        size_t index = 0;
        while (index < length && field[index] == ' ')
            ++index;
        while (index < length && field[index] >= '0' && field[index] <= '7')
        {
            if (value > ((std::numeric_limits<std::uint64_t>::max)() >> 3))
                return false;
            value = (value << 3) | (field[index++] - '0');
        }
        for (; index < length; ++index)
            if (field[index] != '\0' && field[index] != ' ')
                return false;
        return true;
    }

    bool Append(const char* offsetField, const char* lengthField)
    {
        std::uint64_t offset, length;
        if (!ReadNumber(offsetField, 12, offset) || !ReadNumber(lengthField, 12, length) ||
            offset > LogicalSize || length > LogicalSize - offset)
            return false;
        // Empty slots and GNU's final zero-length logical-end marker carry no data.
        if (length == 0)
            return true;
        if ((!Extents.empty() && offset < Extents.back().Offset + Extents.back().Length) ||
            MappedSize > StoredSize || length > StoredSize - MappedSize)
            return false;
        Extents.push_back({offset, length});
        MappedSize += length;
        return true;
    }

    bool Complete() const { return MappedSize == StoredSize; }
};
