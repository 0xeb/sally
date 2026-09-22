// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// widefind — UI-free wide-text literal search for dbviewer's Find.
//
// Find's non-regex search against a genuinely-Unicode database (Database.GetIsUnicode())
// used to narrow the wide cell content through CP_ACP before comparing it through the
// encoded-byte Boyer-Moore interface. A CJK search term never matched real CJK content:
// both sides collapsed through lossy narrowing before any comparison happened.
//
// This module is the replacement comparison for that one case (bmSearchData != NULL and
// the database is Unicode): compare the exact wide cell content against the exact wide
// needle directly, no narrowing at all. Regex mode retains the SDK byte engine behind an
// explicit encoding adapter; for a Unicode database that adapter now encodes to UTF-8
// (see ChooseFindTextDomain), so every cell is searchable and nothing is substituted.
//
// Deliberately self-contained rather than reusing common/text/ContentSearcher.h's
// IsWordCharacterW/CaseFolding.h's ContainsFolded: pulling common/text/* into this plugin
// would need new CMake sources, and ContainsFolded's own documented offset-into-folded-text
// tradeoff is awkward here (Find needs the raw character offset to position the grid
// selection). Case-insensitive comparison is ASCII a-z/A-Z fold only - sufficient for the bug
// this fixes, since CJK and most other non-Latin scripts have no case distinction at all.

#pragma once

#include <cstddef>

namespace sally::dbviewer
{

// TRUE if 'c' counts as a word character for whole-word boundary purposes. '_' counts,
// matching this plugin's own IsAlphaNumeric[] convention for the byte-domain path.
bool IsWordCharacterWide(wchar_t c);

// Which text domain one Find must run in.
//
// The regex search goes through the SDK's encoded-byte engine, so a wide cell has to be
// encoded before the engine sees it. Encoding a Unicode database's cells to ANSI is not
// possible in general: the cell either loses characters or fails to encode at all, and a
// cell that cannot be encoded cannot be searched. UTF-8 encodes every cell, so choosing it
// for Unicode databases means no cell is ever skipped.
enum class FindTextDomain
{
    Wide,      // compare wide text directly, no encoding at all
    AnsiBytes, // the database is already ANSI bytes on disk
    Utf8Bytes, // wide cells encoded losslessly for the byte engine
};

// 'regularExpression' is Find's "Regular expression" option, 'databaseIsUnicode' is
// CDatabase::GetIsUnicode(). Only the regex path can reach Utf8Bytes, because only it is
// forced through the byte engine; a literal search over a Unicode database compares wide
// text directly and needs no encoding.
FindTextDomain ChooseFindTextDomain(bool regularExpression, bool databaseIsUnicode);

// TRUE if 'b' must be treated as part of a word when testing whole-word boundaries in
// UTF-8 text. Every byte of a multi-byte UTF-8 character is >= 0x80, so they all count;
// otherwise a whole-word match could be declared in the middle of a character. The ANSI
// IsAlphaNumeric[] table cannot answer this, because the same byte value means different
// things in the two encodings.
bool IsUtf8WordByte(unsigned char b);

// Case-insensitive (ASCII-only fold) or case-sensitive substring search over wide text,
// honoring whole-word boundaries when requested. Position-based, not fold-then-search, so the
// returned offset always refers directly to 'haystack'. Returns the 0-based character offset
// of the first match at or after 'startOffset', or -1 if none (including when 'needleLen' is 0
// or 'startOffset' is out of range).
int FindWideSubstring(const wchar_t* haystack, std::size_t haystackLen,
                      const wchar_t* needle, std::size_t needleLen,
                      bool caseSensitive, bool wholeWords, int startOffset);

} // namespace sally::dbviewer
