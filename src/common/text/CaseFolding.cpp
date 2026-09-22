// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// CaseFolding — see CaseFolding.h. UI-free by construction: this TU must keep
// compiling without precomp.h so tests can build it directly.

#ifdef SALLY_CASE_FOLDING_STANDALONE
#include <windows.h>
#else
#include "precomp.h"
#endif

#include "common/text/CaseFolding.h"

#include <vector>

namespace sally::text
{
namespace
{
LCID LocaleFor(FoldMode mode)
{
    // LOCALE_INVARIANT gives the same mapping everywhere, which is what a
    // file-name search needs; the user locale is opt-in.
    return mode == FoldMode::Invariant ? LOCALE_INVARIANT : LOCALE_USER_DEFAULT;
}
} // namespace

std::wstring Fold(const std::wstring& text, FoldMode mode)
{
    if (text.empty())
        return std::wstring();

    const LCID locale = LocaleFor(mode);
    // Uppercase folding, matching what the search domain expects. LCMapStringW
    // may produce a different length than the input, so ask for the size first
    // rather than assuming 1:1 (the byte tables' assumption, and the reason ß
    // and similar characters were mishandled).
    const int needed = LCMapStringW(locale, LCMAP_UPPERCASE,
                                    text.c_str(), (int)text.size(), nullptr, 0);
    if (needed <= 0)
        return text; // no mapping available: comparing the raw text is the honest fallback

    std::wstring folded((std::size_t)needed, L'\0');
    const int written = LCMapStringW(locale, LCMAP_UPPERCASE,
                                     text.c_str(), (int)text.size(),
                                     &folded[0], needed);
    if (written <= 0)
        return text;
    folded.resize((std::size_t)written);
    return folded;
}

int CompareFolded(const std::wstring& a, const std::wstring& b, FoldMode mode)
{
    const std::wstring fa = Fold(a, mode);
    const std::wstring fb = Fold(b, mode);
    if (fa == fb)
        return 0;
    return fa < fb ? -1 : 1;
}

bool ContainsFolded(const std::wstring& haystack, const std::wstring& needle,
                    FoldMode mode, std::size_t* matchOffset)
{
    if (needle.empty())
    {
        if (matchOffset != nullptr)
            *matchOffset = 0;
        return true;
    }
    const std::wstring h = Fold(haystack, mode);
    const std::wstring n = Fold(needle, mode);
    const std::size_t pos = h.find(n);
    if (pos == std::wstring::npos)
        return false;
    if (matchOffset != nullptr)
        *matchOffset = pos;
    return true;
}

} // namespace sally::text
