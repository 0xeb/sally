// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// plugin_abi_routing — which ABI a loaded plugin gets.
//
// After the v108 break the loader has to answer one question per plugin, at
// construction time, before any interface pointer is handed over: does this
// binary speak the native wide ABI, does it need the legacy adapter, does it
// need user approval first, or must it be refused?
//
// That decision is pure arithmetic over a version number, so it lives here as a
// pure function rather than inside CPluginData::InitDLL's several hundred lines
// of DLL loading. Two reasons that matters:
//   * it is testable at every boundary (and the boundaries are exactly where a
//     mistake costs someone their plugin, or worse, loads a plugin that then
//     misreads every file name), and
//   * InitDLL keeps one call instead of growing a fourth nested version branch.
//
// THE LADDER, and why each rung exists:
//
//   < 102        Refused. Predates SalamanderPluginGetReqVer entirely (or
//                reports nonsense); there is no contract to honour.
//   == 102       Approval. Legacy commercial plugins. Historically allowed after
//                an explicit per-plugin user confirmation, and that behaviour is
//                deliberately unchanged by the break.
//   == 104       Quarantine. A known-bad vintage that stays refused; the break
//                does not rehabilitate it.
//   103, 105-107 Adapter. These speak the frozen v107-and-earlier ANSI ABI. The
//                legacy adapter marshals for them, which is the entire reason
//                sally/src/compat exists.
//   >= 108       Native. Wide ABI, no marshalling.
//   > LAST       Refused, and this rung is easy to forget: a plugin built for a
//                FUTURE Salamander expects services this build does not have.
//                Loading it optimistically means it calls through a vtable slot
//                that does not exist. Refusing is the safe answer.

#pragma once

namespace sally::compat
{

// The first Salamander version whose plugin ABI is wide-native.
constexpr int kFirstWideAbiVersion = 108;

// Oldest ABI the legacy adapter marshals for. Below this there is no adapter
// path at all.
constexpr int kOldestAdaptedVersion = 103;

// Legacy commercial vintage that requires explicit per-plugin approval.
constexpr int kLegacyApprovalVersion = 102;

// Known-bad vintage that stays refused regardless of everything else.
constexpr int kQuarantinedVersion = 104;

enum class AbiRoute
{
    // Speaks the current wide ABI: hand over core interfaces directly.
    Native,
    // Pre-108 ANSI ABI: wrap every interface in the legacy adapter.
    LegacyAdapter,
    // Legacy commercial vintage: needs user approval, then the adapter.
    NeedsUserApproval,
    // Refused. 'reason' says which rule refused it.
    Refused,
};

enum class RefusalReason
{
    None,
    // Older than any ABI this build can serve, or no version reported at all.
    TooOld,
    // A vintage deliberately kept out.
    Quarantined,
    // Built for a FUTURE Salamander: it would call services that do not exist.
    NewerThanHost,
};

struct RoutingDecision
{
    AbiRoute route = AbiRoute::Refused;
    RefusalReason reason = RefusalReason::TooOld;

    bool needsAdapter() const
    {
        return route == AbiRoute::LegacyAdapter || route == AbiRoute::NeedsUserApproval;
    }
    // Entry may be invoked only after approval has resolved to LegacyAdapter.
    bool loadable() const
    {
        return route == AbiRoute::Native || route == AbiRoute::LegacyAdapter;
    }
};

// Decide the route for a plugin reporting 'builtForVersion'.
//
// 'builtForVersion' is what the loader computed: SalamanderPluginGetReqVer,
// possibly raised by SalamanderPluginGetSDKVer, or -1 when the plugin exports
// neither (a pre-2.5-beta2 binary).
//
// 'hostLastVersion' is this build's LAST_VERSION_OF_SALAMANDER. It is a parameter
// rather than a compile-time constant so the routing can be tested across the
// break instead of only at whatever the build happens to be today — and because
// the frozen snapshot taught us that version macros are exactly the thing that
// silently disagrees between translation units.
//
// 'legacyApprovalAlreadyGiven' is the persisted per-plugin approval flag.
RoutingDecision DecideAbiRoute(int builtForVersion, int hostLastVersion,
                               bool legacyApprovalAlreadyGiven);

} // namespace sally::compat
