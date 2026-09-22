// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SurrogateAssembler — assemble WM_CHAR code units into characters.
//
// Quick-search dropped every character it could not fit in a byte. The gate was
// literal: `wParam > 31 && wParam < 256`, followed by ToAscii and LOBYTE(ch), so
// typing a CJK or Cyrillic character to jump to a file simply did nothing.
//
// Removing that gate is not enough on its own, because a wide window delivers
// characters above U+FFFF as TWO WM_CHAR messages — a high surrogate then a low
// one. Code that treats each message as a whole character sees two meaningless
// half-characters instead of one emoji or rare ideograph, and a quick-search
// buffer built that way can never match the file name it is compared against.
//
// This is that assembly, as a small explicit state machine rather than an ad-hoc
// pair of statics in a window procedure — so the awkward cases (a lone surrogate,
// two highs in a row, a BMP character arriving mid-pair) have defined answers and
// tests instead of whatever the message order happened to produce.

#pragma once

#include <string>

namespace sally::input
{

enum class FeedResult
{
    // A complete character is available via TakeCharacter().
    Character,
    // A high surrogate was consumed; the assembler is waiting for its low half.
    // Callers must not treat this as input yet.
    Pending,
    // The code unit is not usable as search input (a control character, or a
    // malformed surrogate sequence). Nothing is pending afterwards.
    Rejected,
};

class SurrogateAssembler
{
public:
    // Feed one WM_CHAR code unit.
    FeedResult Feed(wchar_t unit);

    // The most recently completed character, as 1 or 2 code units (a surrogate
    // pair is returned intact, because that is what a wide string needs).
    // Valid only immediately after Feed returned Character.
    const std::wstring& TakeCharacter() const { return character_; }

    // TRUE while a high surrogate is waiting for its partner.
    bool pending() const { return pendingHigh_ != 0; }

    // Forget any half-typed character. Call when the search is cancelled or the
    // focus moves, so a stale high surrogate cannot pair with the next keystroke
    // minutes later.
    void Reset();

    // Would this code unit be accepted as search input at all? Control
    // characters are not; everything else printable is, including code units
    // outside the active code page — which is the whole point.
    static bool IsUsableInput(wchar_t unit);

private:
    wchar_t pendingHigh_ = 0;
    std::wstring character_;
};

} // namespace sally::input
