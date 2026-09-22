// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

namespace sally
{
namespace text
{

// GetTextExtentExPointW fills a CUMULATIVE width array: element i is the pixel width of the first
// i + 1 characters. Callers overwhelmingly want the inverse question - "where does character N
// start?" - which is element N - 1, and N is legitimately 0: an empty string, or a path component
// with no characters of its own.
//
// Spelling that as `widths[n - 1]` at the call site is the defect this exists to prevent. On a raw
// int array `widths[-1]` silently read four bytes before the buffer and usually produced a
// plausible-looking offset; on a std::vector the index converts to a huge size_t and the checked
// operator[] terminates the process. A real crash dump (CStatusWindow::Paint, empty status text)
// was exactly this.
//
// Returns the pixel width of the first 'characters' characters, i.e. the x offset at which
// character 'characters' begins. Zero for a non-positive count, and clamped to the last known
// width if the count runs past what was measured.
inline int CumulativeWidthBefore(const std::vector<int>& widths, int characters)
{
    if (characters <= 0)
        return 0;
    if (static_cast<size_t>(characters) > widths.size())
        return widths.empty() ? 0 : widths.back();
    return widths[static_cast<size_t>(characters) - 1];
}

} // namespace text
} // namespace sally
