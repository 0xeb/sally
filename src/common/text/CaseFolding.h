// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// CaseFolding — wide case folding for SEARCH.
//
// The search domain folded case through 256-entry byte tables (LowerCase[] /
// UpperCase[]), which by construction cannot fold anything outside the active
// code page: searching for "ФАЙЛ" would not match "файл", and on a Western code
// page neither string survives the trip at all.
//
// This is new original work: LCMapStringW does the folding, and the API is built
// around the two properties a search needs and the byte tables cannot give —
// a fold that is stable for comparison, and an explicit choice between
// locale-sensitive and locale-invariant behaviour.
//
// WHY THE INVARIANT OPTION MATTERS: Turkish locales fold 'I' to the dotless
// 'ı', so a locale-sensitive fold makes "FILE" and "file" DIFFERENT strings on a
// Turkish machine. A file-name search must not change results with the user's
// locale, so search folding is invariant by default; UI-facing comparisons that
// should follow the user's expectations can ask for the locale fold explicitly.

#pragma once

#include <string>

namespace sally::text
{

enum class FoldMode
{
    // Locale-independent: same answer on every machine. The default for search.
    Invariant,
    // Follows the user's locale, including its special cases (Turkish dotted/
    // dotless i, etc.).
    UserLocale,
};

// Fold a string for case-insensitive comparison. The result is only meaningful
// as a comparison key: it is not a display form and its length may differ from
// the input's.
std::wstring Fold(const std::wstring& text, FoldMode mode = FoldMode::Invariant);

// Case-insensitive comparison via folding. Returns <0, 0, >0.
int CompareFolded(const std::wstring& a, const std::wstring& b,
                  FoldMode mode = FoldMode::Invariant);

// TRUE when 'needle' occurs in 'haystack' under folding. 'matchOffset' (optional)
// receives the offset IN THE FOLDED HAYSTACK — callers needing raw offsets must
// map them back themselves, because folding is not length-preserving (ß folds to
// two characters under some modes).
bool ContainsFolded(const std::wstring& haystack, const std::wstring& needle,
                    FoldMode mode = FoldMode::Invariant,
                    std::size_t* matchOffset = nullptr);

} // namespace sally::text
