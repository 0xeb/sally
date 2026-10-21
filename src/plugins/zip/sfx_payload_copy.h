// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdlib>
#include <memory>

// The copy and flush callbacks return the existing ZIP error convention.
// A failed allocation must remain the result, without entering either callback.
template <class Allocate, class Copy, class Flush>
int CopySfxPayload(size_t bufferBytes, int lowMemory, Allocate&& allocate, Copy&& copy, Flush&& flush)
{
    std::unique_ptr<char, decltype(&std::free)> buffer(allocate(bufferBytes), &std::free);
    if (!buffer)
        return lowMemory;
    const int error = copy(buffer.get());
    return error != 0 ? error : flush();
}
