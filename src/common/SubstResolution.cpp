// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// SUBST/DOS-device path resolution. Extracted from sally_strings_waitwindow.cpp
// so production and the private tests compile the same translation
// unit. Rationale for the wide port is in the header.

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include <string>

#include "common/SubstResolution.h"
#include "common/SalPathWide.h"

namespace sally::paths
{

namespace
{

// `\??\` — the object-manager prefix every SUBST-style mapping target carries.
constexpr wchar_t DosDevicePrefix[] = L"\\??\\";
constexpr size_t DosDevicePrefixLen = 4;

// `UNC\` — the marker that the remainder is a share path rather than a local one.
constexpr wchar_t UncMarker[] = L"UNC\\";
constexpr size_t UncMarkerLen = 4;

inline bool IsAsciiDriveLetter(wchar_t c)
{
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

} // namespace

DosDeviceKind ParseDosDeviceTargetW(const wchar_t* deviceTarget, std::wstring& outPath)
{
    if (deviceTarget == nullptr)
        return DosDeviceKind::Unresolvable;

    const size_t len = wcslen(deviceTarget);
    if (len < DosDevicePrefixLen || wcsncmp(deviceTarget, DosDevicePrefix, DosDevicePrefixLen) != 0)
        return DosDeviceKind::Unresolvable; // a physical device or a redirector target

    const wchar_t* rest = deviceTarget + DosDevicePrefixLen;
    const size_t restLen = len - DosDevicePrefixLen;

    // `\??\C:\Windows` — the substituted local path, kept verbatim from the drive letter on.
    if (restLen >= 2 && IsAsciiDriveLetter(rest[0]) && rest[1] == L':')
    {
        outPath.assign(rest, restLen);
        return DosDeviceKind::LocalPath;
    }

    // `\??\UNC\drak\share` -> `\\drak\share`. The marker is followed by the
    // separator that becomes the second backslash of the UNC prefix, so the
    // remainder is taken from that separator and one backslash is prepended.
    if (restLen >= UncMarkerLen && wcsncmp(rest, UncMarker, UncMarkerLen) == 0)
    {
        outPath.assign(1, L'\\');
        outPath.append(rest + UncMarkerLen - 1, restLen - (UncMarkerLen - 1));
        return DosDeviceKind::UncPath;
    }

    return DosDeviceKind::Unresolvable;
}

wchar_t NormalizeDriveLetterW(wchar_t driveLetter)
{
    // Deliberately arithmetic rather than towupper(): under a Turkish locale
    // towupper(L'i') is U+0130, so a locale-aware fold would map drive letter
    // 'i' to something that is not a drive letter at all. Drive letters are
    // ASCII by definition, so the invariant mapping is the correct one — the
    // same call made for CaseFolding.
    if (driveLetter >= L'a' && driveLetter <= L'z')
        return static_cast<wchar_t>(driveLetter - L'a' + L'A');
    return driveLetter;
}

SubstResolveResult ResolveSubstChainW(std::wstring& path, const SubstQueryW& query)
{
    // Matches the narrow original: 50 substitutions are followed, the 51st is
    // treated as a cycle. Deep-but-finite chains are vanishingly rare and a real
    // cycle would otherwise hang the caller.
    int cycle = 0;

    while (path.size() >= 2 && IsAsciiDriveLetter(path[0]) && path[1] == L':')
    {
        if (cycle++ == 50)
            return SubstResolveResult::CycleGuard;

        std::wstring target;
        if (!query || !query(NormalizeDriveLetterW(path[0]), target))
            break; // not a SUBST — an ordinary path stops here, after one query

        // A mapped network drive resolves to a UNC path. Those are handled by a
        // different mechanism, so the chain stops rather than substituting one.
        if (target.empty() || target[0] == L'\\')
            break;

        // Carry everything after the drive letter onto the target.
        SalPathAppendW(target, path.c_str() + 2);

        // A bare `C:` means the drive's root, which every consumer spells `C:\`.
        if (target.size() == 2 && target[1] == L':')
            target += L'\\';

        path = target;
    }

    return SubstResolveResult::Resolved;
}

} // namespace sally::paths
