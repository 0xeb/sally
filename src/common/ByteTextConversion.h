// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>

namespace sally::conversion
{
    // Encoded-byte conversion, deliberately independent of path/text ownership.
    // The caller supplies space for at most twice sourceSize bytes. A CR emits
    // its line ending immediately; only the very next byte may complete CRLF.
    inline std::size_t ConvertByteChunk(const char* source, std::size_t sourceSize,
                                        const char* codeTable, int lineEndings,
                                        bool& previousWasCR, char* target)
    {
        std::size_t written = 0;
        for (std::size_t index = 0; index < sourceSize; ++index)
        {
            const auto byte = static_cast<unsigned char>(source[index]);
            const bool followsCR = previousWasCR;
            previousWasCR = false;
            if (lineEndings != 0)
            {
                if (followsCR && byte == '\n')
                    continue;
                if (byte == '\r' || byte == '\n')
                {
                    previousWasCR = byte == '\r';
                    if (lineEndings != 2)
                        target[written++] = codeTable['\r'];
                    if (lineEndings != 3)
                        target[written++] = codeTable['\n'];
                    continue;
                }
            }
            target[written++] = codeTable[byte];
        }
        return written;
    }
} // namespace sally::conversion
