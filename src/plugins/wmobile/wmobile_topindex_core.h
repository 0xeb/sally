// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The panel top-index memory, extracted so it can be tested without a device.
//
// Second piece of wmobile's headless coverage, after wmobile_path_core. This one is pure logic -
// an in-memory stack of scroll positions, one per directory level - but it is fiddly logic:
// Push has to decide whether the incoming path is a CHILD of the remembered one, and FindAndPop
// has to strip exactly one component back off. Both hand-parse backslashes with a trailing
// separator that may or may not be present, which is the kind of code that looks obviously
// correct and is off by one.
//
// It is also the piece most likely to be quietly broken by the wide conversion: every offset here
// is a character count compared against a length, and none of it is checked by the compiler.
//
// THE COMPARATOR IS INJECTED on purpose. Production passes SalamanderGeneral->StrNICmp, whose
// case-folding is Salamander's own; tests pass the CRT equivalent. Extracting the structure
// without pinning the comparison means the tests exercise the same branching the plugin does
// while leaving its case-insensitivity semantics untouched.
//
// Deliberately still narrow: this mirrors the shipping code exactly so the tests describe what the
// plugin does TODAY. When the surrounding pipeline widens, the core and these tests widen together
// and the tests become the check on that step rather than a rewrite of it.

#pragma once

#include <cstddef>
#include <string>

namespace wmobile
{

// Case-insensitive comparison of the first 'count' characters; returns 0 when equal, matching
// StrNICmp and _strnicmp.
using PathCompareN = int (*)(const wchar_t* a, const wchar_t* b, size_t count);

// Number of levels remembered. Matches TOP_INDEX_MEM_SIZE in wmobile.h.
constexpr int kTopIndexMemSize = 50;

class TopIndexMemory
{
public:
    explicit TopIndexMemory(PathCompareN compare);

    void Clear();

    // Remembers 'topIndex' for 'path'. If 'path' is a direct child of the remembered path the
    // index is pushed onto the sequence; otherwise the sequence restarts. When the sequence is
    // full the OLDEST entry is discarded, so a deep descent keeps the most recent levels.
    void Push(const wchar_t* path, int topIndex);

    // If 'path' is the remembered path, pops its index and steps the memory up one level.
    // Any other path is treated as a jump: the memory is cleared and false is returned.
    bool FindAndPop(const wchar_t* path, int& topIndex);

    // For tests and diagnostics.
    const wchar_t* RememberedPath() const { return m_path.c_str(); }
    int Depth() const { return m_count; }

private:
    PathCompareN m_compare;
    std::wstring m_path;
    int m_indexes[kTopIndexMemSize];
    int m_count;
};

} // namespace wmobile
