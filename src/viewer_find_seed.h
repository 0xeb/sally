// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Sizing rules for the Find dialog's auto-fill from the viewer selection.
//
// THESE ARE NOT LIMITS ON WHAT CAN BE SEARCHED. The pattern the user types or
// pastes into the Find combo is not capped, and the retired FIND_TEXT_LEN ceiling
// of 201 characters is not coming back. What is bounded here is only the
// convenience that pre-fills that combo from whatever is currently selected.
//
// It has to be bounded because it runs synchronously on the UI thread. Ctrl+A on
// a large file followed by Ctrl+F read the entire selection - Prepare() in
// 1000-byte steps over the whole range - and in hex mode every source byte then
// became three more characters on the way into the combo box. On a 500 MB file
// that is a multi-minute freeze ending in bad_alloc, with nothing shown to the
// user and no way to cancel. The selection itself is untouched; only the seed is
// clipped, exactly as it was before the widening.
//
// The two budgets differ because the paths cost differently: the byte path feeds
// a 3x hex expansion, while the decoded path yields at most one UTF-16 code unit
// per source byte.

#include <string>

namespace Sally
{
namespace Viewer
{

// Raw bytes the seed may read from the file, in either mode.
const long long kFindSeedMaxBytes = 4096;

// UTF-16 code units the decoded seed may end up with.
const long long kFindSeedMaxChars = 1024;

// Clip a selection range to the seed's byte budget. Returns the new end offset.
// A degenerate or inverted range is returned unchanged so the caller's own
// validation still sees what it was given.
inline long long ClampFindSeedRange(long long startSel, long long endSel)
{
    if (endSel <= startSel)
        return endSel;
    if (endSel - startSel > kFindSeedMaxBytes)
        return startSel + kFindSeedMaxBytes;
    return endSel;
}

// Clip decoded seed text to the character budget without ending on half of a
// surrogate pair - a lone high surrogate in the Find box would render as a
// replacement glyph and search for something the file does not contain.
inline void ClampFindSeedText(std::wstring& text)
{
    if (text.size() <= static_cast<std::size_t>(kFindSeedMaxChars))
        return;
    text.resize(static_cast<std::size_t>(kFindSeedMaxChars));
    if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF)
        text.pop_back();
}

} // namespace Viewer
} // namespace Sally
