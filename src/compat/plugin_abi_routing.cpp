// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// plugin_abi_routing — see plugin_abi_routing.h.
//
// No Windows headers, no SDK headers, no precomp.h: this is arithmetic over a
// version number and it stays that way, so it can be compiled into anything
// (including a test that needs no ABI at all).

#include "compat/plugin_abi_routing.h"

namespace sally::compat
{

RoutingDecision DecideAbiRoute(int builtForVersion, int hostLastVersion,
                               bool legacyApprovalAlreadyGiven)
{
    RoutingDecision decision;

    // Ordered most-restrictive first, because several rules can match one
    // version and the refusals must win.

    // A plugin built for a FUTURE host expects services this build does not
    // export. It would call through vtable slots that do not exist here, so
    // optimism is a crash. This rung is checked BEFORE the native range, which
    // is the whole point: >= 108 is not sufficient, it must also be <= what this
    // build actually implements.
    if (builtForVersion > hostLastVersion)
    {
        decision.route = AbiRoute::Refused;
        decision.reason = RefusalReason::NewerThanHost;
        return decision;
    }

    // Deliberately kept out, regardless of falling inside the adapted range.
    if (builtForVersion == kQuarantinedVersion)
    {
        decision.route = AbiRoute::Refused;
        decision.reason = RefusalReason::Quarantined;
        return decision;
    }

    // Legacy commercial vintage: allowed, but only with explicit per-plugin
    // approval. Once approved it still needs the adapter - approval is about
    // trust, not about the ABI it speaks.
    if (builtForVersion == kLegacyApprovalVersion)
    {
        decision.route = legacyApprovalAlreadyGiven ? AbiRoute::LegacyAdapter
                                                    : AbiRoute::NeedsUserApproval;
        decision.reason = RefusalReason::None;
        return decision;
    }

    // Anything below the oldest adapted version has no path at all. This also
    // catches the -1 the loader uses for a binary that exports no version
    // function.
    if (builtForVersion < kOldestAdaptedVersion)
    {
        decision.route = AbiRoute::Refused;
        decision.reason = RefusalReason::TooOld;
        return decision;
    }

    if (builtForVersion >= kFirstWideAbiVersion)
    {
        decision.route = AbiRoute::Native;
        decision.reason = RefusalReason::None;
        return decision;
    }

    // 103 and 105-107: the adapter's reason for existing.
    decision.route = AbiRoute::LegacyAdapter;
    decision.reason = RefusalReason::None;
    return decision;
}

} // namespace sally::compat
