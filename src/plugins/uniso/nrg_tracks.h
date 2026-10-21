// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

namespace sally::nrg
{
    struct DaoTrack
    {
        unsigned Index;
        uint16_t SectorSize;
        uint8_t Type;
        uint64_t Start, End;
    };
    inline uint64_t BigEndian(const uint8_t* bytes, size_t count)
    {
        uint64_t value = 0;
        for (size_t i = 0; i < count; ++i)
            value = (value << 8) | bytes[i];
        return value;
    }
    // Read must return true only after reading exactly the requested bytes.
    // Publish no track metadata until the entire DAOX chunk has been validated.
    template <class Read>
    bool ReadDaoTracks(uint32_t chunkBytes, unsigned existingTracks, Read&& read,
                       std::vector<DaoTrack>& output)
    {
        if (chunkBytes < 0x16)
            return false;
        uint8_t header[0x16]{};
        if (!read(header, sizeof(header)))
            return false;
        const unsigned first = header[0x14], last = header[0x15];
        if (first == 0 || last < first || last > existingTracks ||
            uint64_t(last - first + 1) * 0x2a > chunkBytes - 0x16)
            return false;
        try
        {
            std::vector<DaoTrack> staged;
            staged.reserve(last - first + 1);
            for (unsigned number = first; number <= last; ++number)
            {
                uint8_t bytes[0x2a]{};
                if (!read(bytes, sizeof(bytes)))
                    return false;
                const uint64_t start = BigEndian(bytes + 0x1a, 8);
                const uint64_t end = BigEndian(bytes + 0x22, 8);
                if (end < start || end > uint64_t((std::numeric_limits<int64_t>::max)()))
                    return false;
                staged.push_back({number - 1, static_cast<uint16_t>(BigEndian(bytes + 0x0c, 2)),
                                  bytes[0x0e], start, end});
            }
            output.swap(staged);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
    }
} // namespace sally::nrg
