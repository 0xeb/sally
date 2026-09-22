// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Utf8CaseFold - LENGTH-PRESERVING case folding for UTF-8 BYTE engines.
//
// Sally has two byte-oriented search engines that predate the Unicode migration:
// the Spencer regexp core (common/regexp.cpp) and BMSearch. Both fold case with a
// 256-entry table, which was correct while every subject byte was a single ACP
// character and is actively wrong once the subject is UTF-8: the table rewrites
// lead bytes (on CP-1252 it maps 0xC3 to 0xE3), so case-insensitive matching both
// MISSES real matches and INVENTS false ones across unrelated scripts.
//
// CaseFolding.h is the right tool for wide text, but its fold is explicitly NOT
// length-preserving, so it cannot be used where an engine reports offsets into the
// caller's buffer. This header provides the other half: a fold that is guaranteed
// to preserve byte length, which is what lets an engine run over a folded copy while
// every offset it reports still indexes the original.
//
// Promoted from src/plugins/renamer/renamer_case_fold.h, which now aliases it, so
// the renamer and the core engines share one implementation.

#pragma once

#include <string>
#include <string_view>

#include <windows.h>

namespace sally::text
{

// These helpers restore case-insensitive matching for non-ASCII letters by folding the
// text up front, so a byte engine can keep running over bytes. The fold is
// LENGTH-PRESERVING: a code point whose lowercase form would not occupy the same number
// of UTF-8 bytes is left exactly as it was. That guarantee is what makes the technique
// safe - an engine can run over the folded copy while every offset it reports (match
// starts, ends, captured sub-expressions) still indexes the original buffer, so
// replacement output is never lowercased.
enum class FoldScope
{
    // Fold every code point. For BMSearch, which then searches case-sensitively.
    All,
    // Fold only code points >= 0x80. For the regexp engine, whose syntax is entirely
    // ASCII: folding ASCII here would rewrite \W into \w and \S into \s. ASCII case
    // insensitivity stays with the engine (the renamer's RE_CASELES table, or the
    // plain A-Z fold common/regexp.cpp applies alongside this call).
    NonAsciiOnly,
};

namespace detail
{

// Decodes one UTF-8 sequence. Returns the number of bytes consumed, or 0 for anything
// malformed - the caller then copies the single byte through untouched.
inline std::size_t DecodeUtf8(std::string_view text, std::size_t at, char32_t& code)
{
    const auto byteAt = [&](std::size_t i) -> unsigned
    { return static_cast<unsigned char>(text[i]); };

    const unsigned lead = byteAt(at);
    std::size_t length;
    char32_t value;
    if (lead < 0x80)
    {
        code = static_cast<char32_t>(lead);
        return 1;
    }
    else if ((lead & 0xE0) == 0xC0)
    {
        length = 2;
        value = lead & 0x1F;
    }
    else if ((lead & 0xF0) == 0xE0)
    {
        length = 3;
        value = lead & 0x0F;
    }
    else if ((lead & 0xF8) == 0xF0)
    {
        length = 4;
        value = lead & 0x07;
    }
    else
        return 0;

    if (at + length > text.size())
        return 0;
    for (std::size_t i = 1; i < length; i++)
    {
        const unsigned continuation = byteAt(at + i);
        if ((continuation & 0xC0) != 0x80)
            return 0;
        value = (value << 6) | (continuation & 0x3F);
    }

    // Reject overlong forms and surrogates so a folded copy can never be longer or
    // shorter than what we consumed.
    if ((length == 2 && value < 0x80) || (length == 3 && value < 0x800) ||
        (length == 4 && value < 0x10000) || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF))
        return 0;

    code = value;
    return length;
}

inline std::size_t EncodeUtf8(char32_t code, char* output)
{
    if (code < 0x80)
    {
        output[0] = static_cast<char>(code);
        return 1;
    }
    if (code < 0x800)
    {
        output[0] = static_cast<char>(0xC0 | (code >> 6));
        output[1] = static_cast<char>(0x80 | (code & 0x3F));
        return 2;
    }
    if (code < 0x10000)
    {
        output[0] = static_cast<char>(0xE0 | (code >> 12));
        output[1] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        output[2] = static_cast<char>(0x80 | (code & 0x3F));
        return 3;
    }
    output[0] = static_cast<char>(0xF0 | (code >> 18));
    output[1] = static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    output[2] = static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    output[3] = static_cast<char>(0x80 | (code & 0x3F));
    return 4;
}

inline char32_t LowerCodePoint(char32_t code)
{
    wchar_t units[2];
    int count;
    if (code < 0x10000)
    {
        units[0] = static_cast<wchar_t>(code);
        count = 1;
    }
    else
    {
        const char32_t offset = code - 0x10000;
        units[0] = static_cast<wchar_t>(0xD800 + (offset >> 10));
        units[1] = static_cast<wchar_t>(0xDC00 + (offset & 0x3FF));
        count = 2;
    }

    ::CharLowerBuffW(units, static_cast<DWORD>(count));

    if (count == 1)
        return static_cast<char32_t>(units[0]);
    return 0x10000 + ((static_cast<char32_t>(units[0]) - 0xD800) << 10) +
           (static_cast<char32_t>(units[1]) - 0xDC00);
}

} // namespace detail

inline std::string FoldUtf8Preserving(std::string_view text, FoldScope scope)
{
    std::string folded;
    folded.reserve(text.size());

    std::size_t at = 0;
    while (at < text.size())
    {
        char32_t code = 0;
        const std::size_t consumed = detail::DecodeUtf8(text, at, code);
        if (consumed == 0)
        {
            folded.push_back(text[at]);
            at++;
            continue;
        }

        if (scope == FoldScope::NonAsciiOnly && code < 0x80)
        {
            folded.append(text, at, consumed);
            at += consumed;
            continue;
        }

        const char32_t lowered = detail::LowerCodePoint(code);
        char encoded[4];
        const std::size_t produced = detail::EncodeUtf8(lowered, encoded);
        if (produced == consumed)
            folded.append(encoded, produced);
        else
            folded.append(text, at, consumed); // keep offsets aligned - see FoldScope
        at += consumed;
    }

    return folded;
}

} // namespace sally::text
