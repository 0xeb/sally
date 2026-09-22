// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SharedMemCompat — see SharedMemCompat.h. No Windows headers by design.

#include "common/SharedMemCompat.h"

namespace sally::ipc
{

SharedMemVerdict DecideSharedMemUse(const SharedMemObservation& observation)
{
    // Nothing this build could even describe.
    if (observation.expectedSize == 0)
        return SharedMemVerdict::UnusableDegrade;

    // The mapping must at least cover the struct we intend to touch, whoever
    // created it. A mapping smaller than that is unusable regardless of what any
    // size field claims.
    if (observation.mappedBytes < observation.expectedSize)
        return SharedMemVerdict::UnusableDegrade;

    if (observation.created)
    {
        // We are the writer: there is no peer layout to disagree with yet.
        return SharedMemVerdict::CreatedUseIt;
    }

    // Someone else created it. Their Size field is the only statement we have
    // about what they compiled.
    if (observation.observedSize == 0)
    {
        // Either a peer that never initialised it, or a block that predates the
        // Size convention. Both mean we cannot establish agreement.
        return SharedMemVerdict::UnusableDegrade;
    }

    // EXACT match only. 'observed < expected' is the stale-DLL case and would
    // have us write past their allocation; 'observed > expected' is a NEWER peer,
    // and reading their block with our older layout means reading fields at
    // offsets they may have moved. Neither is safe, and neither is worth a
    // heuristic - the section name is what gets bumped when the layout changes.
    if (observation.observedSize != observation.expectedSize)
        return SharedMemVerdict::IncompatibleDegrade;

    return SharedMemVerdict::CompatibleUseIt;
}

bool VerdictAllowsUse(SharedMemVerdict verdict)
{
    return verdict == SharedMemVerdict::CreatedUseIt ||
           verdict == SharedMemVerdict::CompatibleUseIt;
}

} // namespace sally::ipc
