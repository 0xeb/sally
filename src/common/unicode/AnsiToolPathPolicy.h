// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <string>

namespace sally::unicode
{

// Handing a path to a consumer that only speaks ANSI - an external archiver command
// line, a list file, a plugin whose ABI is char*-only - has three outcomes, and the
// long-standing bug is that the code only ever recognised one of them.
//
// WideToAnsi() answers every question with a string. For a path the code page cannot
// spell it answers with a *different* string: best-fit mapping turns 'e' into 'e' and
// anything hopeless into '?'. Passing that on is not a degraded operation, it is an
// operation on the wrong target - and '?' is a wildcard to most archivers, so the
// wrong target can be plural. Unpack (audit A18) hits exactly this: a mangled archive
// path that happens to match a real neighbouring archive unpacks that one instead,
// silently and successfully.
//
// The missing third answer is "no ANSI string names this file". Once a caller can see
// that case it can say so, which is the whole point.
enum class AnsiToolPathKind
{
    // CP_ACP spells the path exactly. The overwhelmingly common case.
    Direct,
    // CP_ACP cannot spell the path, but the volume's 8.3 alias can, and an alias names
    // the same file on disk. So the operation still runs, just under a different name.
    ShortAlias,
    // Neither. Nothing ANSI designates this file (8.3 generation is commonly disabled
    // on modern NTFS volumes via NtfsDisable8dot3NameCreation). The caller must refuse
    // and say why, not hand over a lossy guess.
    Unavailable,
};

struct AnsiToolPath
{
    AnsiToolPathKind Kind = AnsiToolPathKind::Unavailable;
    std::string Path; // meaningful only when Usable()
    std::wstring WidePath; // the same direct path or short alias, without a recovery conversion

    bool Usable() const { return Kind != AnsiToolPathKind::Unavailable; }
};

// Resolves the ANSI designator for 'widePath'.
//
// 'getShortPath' is the OS lookup, injected so this stays a pure decision that can be
// tested headlessly without a volume that has 8.3 names configured either way. It takes
// the wide path and returns the short form, or an empty string when the OS has none
// (file missing, 8.3 disabled, or the call failed). In production that is
// GetShortPathNameW; note it requires the file to already exist, so resolve target
// directories *after* creating them.
//
// The alias is re-checked for round-trip rather than assumed: GetShortPathNameW returns
// the long component unchanged for any component that has no 8.3 alias, so a "short"
// path can still be unspellable - and a half-shortened path is exactly the kind of thing
// that looks like it worked.
template <typename ShortPathProvider>
inline AnsiToolPath ResolveAnsiToolPath(const std::wstring& widePath, ShortPathProvider getShortPath)
{
    AnsiToolPath result;
    if (widePath.empty())
        return result;

    std::string direct;
    if (Win32EncodeAcpExact(widePath, direct))
    {
        result.Kind = AnsiToolPathKind::Direct;
        result.Path = direct;
        result.WidePath = widePath;
        return result;
    }

    const std::wstring shortPath = getShortPath(widePath);
    if (shortPath.empty())
        return result;

    std::string alias;
    if (!Win32EncodeAcpExact(shortPath, alias))
        return result;

    result.Kind = AnsiToolPathKind::ShortAlias;
    result.Path = alias;
    result.WidePath = shortPath;
    return result;
}

// The same question, asked where the wide original is no longer in reach.
//
// An external archiver's list file is a single-byte protocol - OEM, or ANSI when the
// packer is configured with NeedANSIListFile - and the enumerator that fills it
// (SalEnumSelection2, plugins/shared/spl_com.h:461) yields char* only. By the time a
// name reaches that loop the wide form is gone, so ResolveAnsiToolPath above cannot be
// used: there is nothing left to resolve from.
//
// What survives is evidence. '?' cannot occur in a Windows path component - the
// filesystem rejects it, and Sally's own validator lists it as an explicit reject
// character - so a '?' in an enumerated name did not come from the disk. It is the
// residue of WideToAnsi giving up. That makes it a sound detector without needing the
// original back.
//
// Worth being precise about why this matters more than a cosmetic glitch: '?' is a
// single-character wildcard to essentially every archiver Sally drives. A list-file
// entry that reads "??.txt" is not a name the archiver fails to find - it is a pattern
// that can match a *different* file sitting next to the intended one, which is then
// packed, or under Pack-with-Delete, packed and the original removed. Silence is the
// dangerous part; refusing and naming the file is the whole fix.
//
// Returns true when 'ansiName' cannot have come from a real name intact.
inline bool AnsiNameShowsNarrowingLoss(const char* ansiName)
{
    if (ansiName == nullptr)
        return false;
    for (const char* p = ansiName; *p != 0; ++p)
    {
        if (*p == '?')
            return true;
    }
    return false;
}

} // namespace sally::unicode
