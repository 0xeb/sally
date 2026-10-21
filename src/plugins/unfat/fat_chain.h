// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <unordered_set>

namespace sally::fat
{
    enum class Type
    {
        Fat12,
        Fat16,
        Fat32
    };
    enum class ChainResult
    {
        Complete,
        Corrupt,
        IoError,
        NoMemory
    };

    // Read receives an offset relative to the first FAT and an exact byte count.
    // Append publishes only validated data-cluster numbers. The caller discards
    // partial output on failure; corrupt chains must never be extracted as files.
    template <class Read, class Append>
    ChainResult ReadChain(Type type, uint32_t clusterCount, uint64_t tableBytes,
                          uint32_t first, Read&& read, Append&& append)
    {
        if (first == 0)
            return ChainResult::Complete; // empty file
        uint32_t mask, reserved;
        switch (type)
        {
        case Type::Fat12:
            mask = 0xfff;
            reserved = 0xff0;
            break;
        case Type::Fat16:
            mask = 0xffff;
            reserved = 0xfff0;
            break;
        case Type::Fat32:
            mask = 0x0fffffff;
            reserved = 0x0ffffff0;
            break;
        default:
            return ChainResult::Corrupt;
        }
        try
        {
            std::unordered_set<uint32_t> visited;
            uint32_t current = first;
            while (true)
            {
                if (current < 2 || current >= reserved ||
                    uint64_t(current) - 2 >= clusterCount ||
                    visited.size() >= clusterCount || !visited.insert(current).second)
                    return ChainResult::Corrupt;
                const uint64_t offset = type == Type::Fat12 ? uint64_t(current) + current / 2
                                                            : uint64_t(current) * (type == Type::Fat16 ? 2 : 4);
                const size_t bytes = type == Type::Fat32 ? 4 : 2;
                if (offset > tableBytes || bytes > tableBytes - offset)
                    return ChainResult::Corrupt;
                uint8_t raw[4]{};
                if (!read(offset, raw, bytes))
                    return ChainResult::IoError;
                uint32_t next = uint32_t(raw[0]) | uint32_t(raw[1]) << 8;
                if (type == Type::Fat32)
                    next |= uint32_t(raw[2]) << 16 | uint32_t(raw[3]) << 24;
                if (type == Type::Fat12 && (current & 1))
                    next >>= 4;
                next &= mask;
                if (!append(current))
                    return ChainResult::NoMemory;
                if (next >= reserved + 8)
                    return ChainResult::Complete;
                // Free, reserved and bad clusters are corruption, not successful EOF.
                if (next < 2 || next >= reserved)
                    return ChainResult::Corrupt;
                current = next;
            }
        }
        catch (const std::bad_alloc&)
        {
            return ChainResult::NoMemory;
        }
    }
} // namespace sally::fat
