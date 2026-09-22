// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SharedMemCompat — deciding whether a shared-memory block is safe to use
//.
//
// Sally and the registered shell extension share a named memory mapping. Either
// process may create it, so either may find one that the OTHER created — and the
// other may be a DIFFERENT BUILD: a stale `salextx64.dll` left registered from a
// previous install is the normal case, not an exotic one.
//
// THE LATENT BUG THIS EXISTS TO FIX: `InitSalShLib` writes
// `SalShExtSharedMemView->Size = sizeof(CSalShExtSharedMem)` when it CREATES the
// mapping, and then never reads that field again. When it opens a mapping someone
// else created, it maps the view and starts using the struct without checking
// whether the writer agreed about the layout. If a stale DLL created a smaller
// block, Sally reads (and writes) past the end of what that DLL allocated —
// cross-process memory corruption whose symptom appears in the other process.
//
// Widening the struct to WCHAR (this task's actual goal) makes that worse, not
// better: the size changes, so a stale DLL and a new Sally are guaranteed to
// disagree. Hence the never-mutate-in-place convention the section-name ladder
// records — `SalExten_SharedMem` → `_2` → `_3` → `_5` (current) → `_6` next.
//
// This module is the decision, as a pure function: it takes what was observed
// about the block and returns what the caller may do with it. No Windows calls,
// so every branch is testable without a second process.

#pragma once

#include <cstddef>

namespace sally::ipc
{

enum class SharedMemVerdict
{
    // We created it: initialise it and use it.
    CreatedUseIt,
    // Someone else created it and agrees about the layout: use it.
    CompatibleUseIt,
    // Someone else created it and does NOT agree. Do not touch the contents.
    // The caller degrades: features that need the channel are unavailable, and
    // everything else keeps working. Never a crash, never a silent write.
    IncompatibleDegrade,
    // The block is not usable at all (zero size, or smaller than the header we
    // must read to learn anything). Also a degrade, kept separate so the trace
    // says something true.
    UnusableDegrade,
};

struct SharedMemObservation
{
    // TRUE when this process created the mapping (GetLastError() was not
    // ERROR_ALREADY_EXISTS after CreateFileMapping).
    bool created = false;
    // The Size field found in the block. Meaningless when 'created' is true.
    std::size_t observedSize = 0;
    // sizeof() of the struct THIS build expects.
    std::size_t expectedSize = 0;
    // Bytes actually mapped. A mapping can legitimately be larger (page
    // granularity) but never smaller than what the writer claimed.
    std::size_t mappedBytes = 0;
};

// Decide what may be done with the block.
//
// The rule is deliberately strict — EXACT size agreement — rather than
// "observed >= expected". A larger block from a NEWER peer is not safe either:
// this build would read fields at offsets the newer layout may have moved. Only
// an exact match means both sides compiled the same struct.
SharedMemVerdict DecideSharedMemUse(const SharedMemObservation& observation);

// TRUE when the verdict permits touching the block's contents. Provided so
// callers cannot get the polarity wrong at each site.
bool VerdictAllowsUse(SharedMemVerdict verdict);

} // namespace sally::ipc
