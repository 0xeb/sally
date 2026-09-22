// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// PluralExpander — the plural-grammar template engine, UI-free.
//
// Extracted from sally_text_templates.cpp so the engine can be
// tested against the real production source instead of a copy: it is pure text
// processing with no dependency on resources, panels or dialogs. The production
// grammar is UTF-16-only; frozen byte-facing adapters project through their wide contract.
//
// GRAMMAR CONTRACT: whenever the expander changes, ValidatePluralStrings in the
// TRANSLATOR project must change with it. The grammar is
//   {!}text{[index:]form|form|...}text
// with '\\' escaping '|', '\\', ':', '{' and '}', and each form optionally
// prefixed by "<limit>:" selecting it when the argument is <= limit.

#pragma once

#include <windows.h>
#include <string>

#ifdef SALLY_PLURAL_EXPANDER_STANDALONE_TEST
// Standalone test builds do not pull the plugin SDK; mirror its value type.
struct CQuadWord
{
    union
    {
        struct
        {
            DWORD LoDWord;
            DWORD HiDWord;
        };
        unsigned __int64 Value;
    };
    CQuadWord() {}
    CQuadWord(DWORD lo, DWORD hi) { LoDWord = lo; HiDWord = hi; }
    CQuadWord& SetUI64(unsigned __int64 v) { Value = v; return *this; }
};
#else
struct CQuadWord;
#endif

// Numeric parsing over a bounded, not necessarily NUL-terminated, range.
// (Default arguments live in consts.h, which every core TU already sees.)
unsigned __int64 StrToUInt64W(const wchar_t* str, int len, BOOL* isNum);

// Dynamically owned thousands-separated number rendering.
std::wstring NumberToStr(const CQuadWord& number);

// Expands a plural template. Returns the written length (excluding the NUL).
int ExpandPluralStringW(wchar_t* lpOut, int nOutMax, const wchar_t* lpFmt, int nParCount,
                        const CQuadWord* lpParArray);

// Dynamic wide result for core text owners. The buffer form above remains for frozen SDK and
// compatibility projections whose caller supplies a fixed destination.
std::wstring ExpandPluralStringOwnedW(const wchar_t* format, int parameterCount,
                                      const CQuadWord* parameters);
