// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// SubstResolution — SUBST/DOS-device path resolution, UI-free and I/O-free.
//
// Extracted from sally_strings_waitwindow.cpp so production and the
// private tests compile the same translation unit. The `\??\` target grammar and
// the resolve loop are inherited Open Salamander logic; the wide form is a port
// of the same algorithm, so both holders stay on this file.
//
// WHY WIDE MATTERS HERE. `ResolveSubsts` bottoms out in `QueryDosDevice`, which
// under the narrow build is `QueryDosDeviceA` and hands back the SUBST target
// path narrowed through CP_ACP. So
//
//     subst Y: C:\Profiles\Example\文件
//
// resolves "Y:\report.txt" to "C:\Profiles\Example\??\report.txt" — a path that
// either does not exist or, worse, names a DIFFERENT real directory. The
// previous `ResolveSubstsW` was a wrapper that narrowed its own argument, called
// the ANSI implementation and widened the result back, so it inherited that
// defect AND added one at the boundary. Routing the query through
// `QueryDosDeviceW` removes the CP_ACP round trip outright rather than moving
// it.
//
// The chain that sits on top of this: ResolveLocalPathWithReparsePoints ->
// GetResolvedPathMountPointAndGUID / PathsAreOnTheSameVolume / IsPathOnSSD, and
// the SDK methods of the same names.

#pragma once

#include <functional>
#include <string>

namespace sally::paths
{

// What one QueryDosDeviceW target string turned out to be.
//
// Only the two mapped forms are resolvable. A physical device target
// (`\Device\HarddiskVolume1`) means the letter is a real volume, not a SUBST,
// and a network redirector target means the mapping is handled elsewhere.
enum class DosDeviceKind
{
    Unresolvable, // not a SUBST-style mapping (physical volume, redirector, junk)
    LocalPath,    // `\??\C:\Windows`  -> `C:\Windows`
    UncPath,      // `\??\UNC\srv\shr` -> `\\srv\shr`
};

// Parses one raw device target. Pure string work — no Win32 call, no I/O.
//
// The target forms this recognises, as reported by QueryDosDevice:
//
//   A (floppy)                 \Device\Floppy0
//   C (fixed disk)             \Device\HarddiskVolume1
//   U -> V:                    \??\V:
//   V (mapped \\drak\share)    \Device\LanmanRedirector\;V:00000000000fdf1\drak\share
//   W -> \\drak\share          \??\UNC\drak\share
//   X -> D:                    \??\D:
//   Y -> C:\Windows            \??\C:\Windows
//
// 'outPath' is assigned only when the result is not Unresolvable.
DosDeviceKind ParseDosDeviceTargetW(const wchar_t* deviceTarget, std::wstring& outPath);

// Answers "what is drive letter <driveLetter> mapped to", for
// ResolveSubstChainW. Returns false when the letter is not a resolvable
// mapping. Injected so the loop is testable without creating real SUBST drives.
//
// 'driveLetter' is upper-case ASCII 'A'..'Z'.
using SubstQueryW = std::function<bool(wchar_t driveLetter, std::wstring& outTarget)>;

// Upper-cases an ASCII drive letter, invariantly.
//
// Deliberately NOT towupper(): under a Turkish locale towupper(L'i') is U+0130,
// which is not a drive letter at all, and the mapping would then depend on the
// user's locale. Drive letters are ASCII by definition.
wchar_t NormalizeDriveLetterW(wchar_t driveLetter);

// How ResolveSubstChainW finished.
enum class SubstResolveResult
{
    Resolved,  // resolution completed (possibly a no-op — most paths are not SUBSTs)
    CycleGuard // gave up: `subst` mappings formed a cycle
};

// Walks 'path' through its SUBST mappings, in place.
//
// Terminates on the first letter that is not a resolvable mapping, so an
// ordinary path costs one query. A SUBST chain (`subst Y: X:\dir` where X is
// itself a SUBST) is followed to its end; a cycle is broken by a depth guard and
// reported rather than hung on, leaving 'path' at the last state reached — which
// is what the narrow original does.
//
// A target that is itself a UNC path is deliberately NOT followed: mapped
// network drives are resolved by a different mechanism, and substituting one
// here would produce a path the caller's volume logic cannot reason about.
SubstResolveResult ResolveSubstChainW(std::wstring& path, const SubstQueryW& query);

} // namespace sally::paths
