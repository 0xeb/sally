// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// WideCollation — the panel's name-ordering core, UI-free.
//
// Extracted from sort.cpp so the comparator can be tested against
// the real production source. The numeric-aware segmenter is inherited Open
// Salamander logic; the wide implementation is a port of the same algorithm, so
// both holders stay on this file.
//
// WHY WIDE MATTERS HERE: the ANSI comparator collated the CP_ACP mirror, so two
// names differing only outside the active code page narrowed to the same key,
// compared "equal", and ordered nondeterministically. Text segments now go
// through CompareStringW over the real UTF-16 names.
//
// The two configuration switches are PARAMETERS rather than globals: the core
// has no business reading Configuration, and it makes the behaviour testable.

#pragma once

#include <windows.h>

struct CWideCollationOptions
{
    BOOL UsesLocale = TRUE;    // CompareStringW vs. code-unit comparison
    BOOL DetectNumbers = TRUE; // numeric-aware segmenting
    // TRUE = names are split on dots as well as on digit runs. Production
    // derives this from WindowsVistaAndLater && !NoDotBreakInLogicalCompare;
    // it is a parameter here so the core stays free of app globals.
    BOOL BreakOnDots = TRUE;
};

// Logical (numeric-aware) comparison of two wide names.
// 'numericalyEqual' (optional) reports that the compared numeric segments had
// the same VALUE while the strings still differ (e.g. "007" vs "7").
int StrCmpLogicalExW(const wchar_t* s1, int l1, const wchar_t* s2, int l2,
                     BOOL* numericalyEqual, BOOL ignoreCase,
                     const CWideCollationOptions& options);

// Case-folding comparisons that work beyond the active code page.
int WideICmpEx(const wchar_t* s1, int l1, const wchar_t* s2, int l2);
int WideCmpEx(const wchar_t* s1, int l1, const wchar_t* s2, int l2);
