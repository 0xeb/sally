// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SurrogateAssembler — see SurrogateAssembler.h. No Windows headers: this is a
// state machine over code units and it stays compilable anywhere.

#include "common/SurrogateAssembler.h"

namespace sally::input
{
namespace
{
bool IsHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }
} // namespace

bool SurrogateAssembler::IsUsableInput(wchar_t unit)
{
    // Control range and DEL are not search input. Note what is deliberately NOT
    // excluded: anything >= 0x100. The old gate stopped at 256, which is exactly
    // how quick-search lost every non-Latin script.
    if (unit < 0x20 || unit == 0x7F)
        return false;
    return true;
}

void SurrogateAssembler::Reset()
{
    pendingHigh_ = 0;
    character_.clear();
}

FeedResult SurrogateAssembler::Feed(wchar_t unit)
{
    character_.clear();

    if (pendingHigh_ != 0)
    {
        const wchar_t high = pendingHigh_;
        pendingHigh_ = 0;

        if (IsLowSurrogate(unit))
        {
            // The expected completion: emit the pair intact.
            character_.push_back(high);
            character_.push_back(unit);
            return FeedResult::Character;
        }

        // The pair was broken. The pending high surrogate is unusable on its own
        // and is DISCARDED rather than emitted as a lone half - a lone surrogate
        // in a search buffer can never match anything and would corrupt any
        // string it is appended to.
        //
        // The new unit is then judged on its own merits, so a broken pair costs
        // one character rather than two.
        if (IsHighSurrogate(unit))
        {
            pendingHigh_ = unit; // two highs in a row: keep the newer one
            return FeedResult::Pending;
        }
        if (!IsUsableInput(unit))
            return FeedResult::Rejected;
        character_.push_back(unit);
        return FeedResult::Character;
    }

    if (IsHighSurrogate(unit))
    {
        pendingHigh_ = unit;
        return FeedResult::Pending;
    }

    if (IsLowSurrogate(unit))
    {
        // A low surrogate with no high before it is malformed input.
        return FeedResult::Rejected;
    }

    if (!IsUsableInput(unit))
        return FeedResult::Rejected;

    character_.push_back(unit);
    return FeedResult::Character;
}

} // namespace sally::input
