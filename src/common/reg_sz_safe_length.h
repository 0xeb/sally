// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cwchar>
#include <cstddef>

// SetValueW's REG_SZ branch (sally_strings_waitwindow.cpp) has no way
// to know whether the caller's 'data' pointer genuinely points at a wchar_t string -
// the API is type-erased (const void* + a dataSize that this branch ignores, since
// the underlying registry write derives its own length from the string itself).
// A caller that mistakenly passes a narrow char* buffer gets it scanned for a
// two-byte-aligned zero code unit, which can run past the buffer's real end - an
// out-of-bounds read, not just wrong data. This function bounds that scan: a
// genuinely wide string's terminator is always found far below 'cap' in any
// realistic config value, so hitting the cap means 'data' was never valid wide text.
// Dependency-free (no precomp.h) so it is directly unit-testable without a fake-host
// harness - the same shape as zip_name_normalize.h in the zip plugin.
inline bool ComputeRegSzSafeLength(const wchar_t* data, size_t& outLen, size_t cap = 65536)
{
    size_t len = wcsnlen(data, cap);
    if (len >= cap)
    {
        outLen = 0;
        return false;
    }
    outLen = len;
    return true;
}
