// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// ContentSearcher — encoding-aware literal search in file content.
//
// Find's "Containing text" search was a raw byte grep with no encoding awareness
// at all, fed by a needle captured with GetDlgItemTextA FROM A UNICODE DIALOG —
// USER32 narrowed it invisibly, so a CJK needle arrived as "??" and matched
// nothing (or matched the wrong thing). This module is the replacement decision
// layer: it takes a WIDE needle and content bytes, asks EncodingDetector what
// the bytes are, and picks a strategy that can actually answer the question.
//
// THE THREE OUTCOMES, all deliberate:
//
//  1. Decoded content (UTF-8/UTF-16, BOM or heuristic) → decode a window and
//     match wide against wide. This is the case the old engine could never do.
//
//  2. Legacy bytes + a needle that round-trips the active code page EXACTLY →
//     byte search, identical semantics to before. This keeps the fast path and
//     the existing behaviour for the overwhelmingly common ANSI case.
//
//  3. Legacy bytes + a needle that CANNOT round-trip → NoMatchPossible. Not
//     "no match found": the question is unanswerable, because a byte file has
//     no representation of that needle. Reporting it distinctly lets Find say
//     something true instead of silently reporting zero hits, and it is the
//     same lossy-mirror-never-matches rule used for delete masks.
//
// Deliberately NOT here: regular expressions (the byte engine is kept for
// those and documents the limitation) and hex mode, which is the byte-domain
// escape hatch by design and must not be re-interpreted as text.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "common/text/CaseFolding.h"
#include "common/text/EncodingDetector.h"

namespace sally::text
{

enum class SearchOutcome
{
    Found,
    NotFound,
    // The needle cannot exist in this content's encoding (case 3 above).
    NoMatchPossible,
};

struct SearchOptions
{
    bool caseSensitive = false;
    // Match only when the hit is bounded by non-word characters on both sides.
    bool wholeWords = false;
    // Passed through to the detector; callers doing byte-oriented work turn the
    // heuristics off so their content is never re-interpreted.
    DetectionOptions detection;
};

struct SearchResult
{
    SearchOutcome outcome = SearchOutcome::NotFound;
    // Byte offset of the match within the content buffer (not the decoded
    // string): what Find reports and what the viewer needs to seek to.
    std::int64_t rawOffset = 0;
    // Which encoding the content was searched as — surfaced so callers can
    // explain themselves ("found in UTF-16 content") and so tests can assert
    // the strategy, not just the answer.
    Encoding encoding = Encoding::LegacyBytes;

    bool found() const { return outcome == SearchOutcome::Found; }
};

// Search 'content' for the wide 'needle'.
//
// An empty needle is Found at offset 0 (the conventional answer, stated so no
// caller has to guess). 'content' may be a window of a larger file; a match
// that would straddle the end of the window is not reported, so callers reading
// in blocks must overlap them by at least needle length minus one.
SearchResult SearchContent(const std::uint8_t* content, std::size_t contentSize,
                           const std::wstring& needle,
                           const SearchOptions& options = SearchOptions());

// TRUE when 'needle' can be represented exactly in the active code page, i.e.
// when a byte search for it is meaningful at all. Exposed because Find wants to
// tell the user WHY a legacy-byte file was skipped.
bool NeedleIsRepresentableInAnsi(const std::wstring& needle, std::string* ansiOut = nullptr);

// Wide word-boundary test, replacing the IsNotAlphaNorNum[256] byte table on the
// decoded path: that table calls every character outside the active code page a
// non-word character, which breaks whole-word matching for every non-Latin
// script.
bool IsWordCharacterW(wchar_t c);

} // namespace sally::text
