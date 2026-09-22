// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Pure, UI-free, precomp.h-free core shared by CompareExtInfos/BSearchName (common.cpp).
// Both compare selected-file entries (CExtInfo::Name, wide) against a
// dir/file-partitioned, flag-driven case-sensitivity rule: directories fold case according
// to the archive's own Unix-ness (the `unix` flag), files are always compared case-sensitive
// regardless of archive origin. common.cpp's MatchFiles relies on this partition being
// self-consistent between the sort (QuickSortNames -> CompareExtInfos) and the search
// (BSearchName) - see the 2026-08-24
// "comparison-chain audit" entry for the trace proving today's behavior is a deliberate
// dir/file split, not a latent bug. This header is the single source of truth for that
// split so sort and search cannot silently drift apart again.
//
// The case-insensitive primitive is injected (icmp) rather than hardcoded: production calls
// SalamanderGeneral->StrICmp (which folds via sally::text::CompareFolded - already covered by
// its own test suite); tests plug a plain wide comparator. This header pins the BRANCHING
// (which comparator applies to which entries, and in what order), not the folding rules.

template <typename Icmp>
inline int CompareExtInfoNames(const wchar_t* leftName, bool leftIsDir, int leftItemNumber,
                               const wchar_t* rightName, bool rightIsDir, int rightItemNumber,
                               bool unix, Icmp icmp)
{
    if (leftIsDir)
    {
        if (rightIsDir)
        {
            int ret = unix ? wcscmp(leftName, rightName) : icmp(leftName, rightName);
            return ret ? ret : (leftItemNumber < rightItemNumber ? -1 : (leftItemNumber == rightItemNumber ? 0 : 1));
        }
        return -1;
    }
    if (rightIsDir)
        return 1;

    int ret = wcscmp(leftName, rightName);
    return ret ? ret : (leftItemNumber < rightItemNumber ? -1 : (leftItemNumber == rightItemNumber ? 0 : 1));
}

// BSearchName's own comparison: the caller already selects which range (directory range vs
// file range) to search, so this side of the split needs no dir/file branching of its own -
// just the respectCase choice between exact and folded comparison.
template <typename Icmp>
inline int CompareSearchName(const wchar_t* name, const wchar_t* candidateName, bool respectCase, Icmp icmp)
{
    return respectCase ? wcscmp(name, candidateName) : icmp(name, candidateName);
}
