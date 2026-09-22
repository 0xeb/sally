// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// WideCollation — see WideCollation.h. UI-free by construction: this TU must
// keep compiling without precomp.h so tests can build it directly.

#ifdef SALLY_WIDE_COLLATION_STANDALONE
#define NOMINMAX
#include <windows.h>
#else
#include "precomp.h"
#endif

#include "common/text/WideCollation.h"
#include "common/unicode/helpers.h"


//
//*****************************************************************************

// Since Windows XP, there is StrCmpLogicalW in the system, which Explorer uses for this comparison

// Wide fallbacks for the non-locale ("binary-ish") comparison mode.
//
// Case folding goes through FoldCharW (CharLowerW), NOT towupper: nothing in the tree calls
// setlocale, so the CRT's towupper is __ascii_towupper and folds a-z and nothing else. That is a
// narrower fold than the byte tables this replaced - LowerCase[] was built with CharLower under
// the active code page, so "Ärger.txt" and "ärger.txt" did compare equal with "Sort uses locale"
// turned off, and with towupper they stopped. CharLowerW folds the whole of UTF-16 and keeps the
// DIRECTION the byte tables used, which matters for ordering: fold-to-upper would move '_'
// (U+005F) from before the letters to after them.
int WideICmpEx(const wchar_t* s1, int l1, const wchar_t* s2, int l2)
{
    int l = l1 < l2 ? l1 : l2;
    for (int i = 0; i < l; i++)
    {
        const wchar_t c1 = sally::unicode::FoldCharW(s1[i]);
        const wchar_t c2 = sally::unicode::FoldCharW(s2[i]);
        if (c1 != c2)
            return c1 < c2 ? -1 : 1;
    }
    if (l1 == l2)
        return 0;
    return l1 < l2 ? -1 : 1;
}

int WideCmpEx(const wchar_t* s1, int l1, const wchar_t* s2, int l2)
{
    int l = l1 < l2 ? l1 : l2;
    for (int i = 0; i < l; i++)
        if (s1[i] != s2[i])
            return s1[i] < s2[i] ? -1 : 1;
    if (l1 == l2)
        return 0;
    return l1 < l2 ? -1 : 1;
}


// Wide collation core. The numeric-aware segmenter ports 1:1
// because digits and '.' are ASCII; the TEXT segments are the whole point: they
// now go through CompareStringW over the real UTF-16 names instead of
// CompareStringA over a CP_ACP mirror, where two names differing only outside
// the code page collapse to one key and sort nondeterministically.
int StrCmpLogicalExW(const wchar_t* s1, int l1, const wchar_t* s2, int l2,
                     BOOL* numericalyEqual, BOOL ignoreCase,
                     const CWideCollationOptions& options)
{
    const wchar_t* strEnd1 = s1 + l1; // end of string L's1'
    const wchar_t* beg1 = s1;         // beginning of segment (text or number)
    const wchar_t* end1 = s1;         // end of segment (text or number)
    const wchar_t* strEnd2 = s2 + l2; // end of string L's2'
    const wchar_t* beg2 = s2;         // beginning of segment (text or number)
    const wchar_t* end2 = s2;         // end of segment (text or number)
    int suggestion = 0;            // "suggestion" for result (0 / -1 / 1 = nothing / s1<s2 / s1>s2) - e.g. "001" < "01"

    BOOL findDots = options.BreakOnDots; // TRUE = names are split also by dots (not only by numbers)

    while (1)
    {
        const wchar_t* numBeg1 = NULL; // position of first non-zero digit
        BOOL isStr1 = (end1 >= strEnd1 || *end1 < L'0' || *end1 > L'9');
        if (isStr1) // text (even empty) or dot
        {
            if (findDots && end1 < strEnd1 && *end1 == L'.')
                end1++; // dot: if we are looking for them, take one at a time
            else        // text (even empty)
            {
                while (end1 < strEnd1 && (*end1 < L'0' || *end1 > L'9') && (!findDots || *end1 != L'.'))
                    end1++;
            }
        }
        else // number
        {
            while (end1 < strEnd1 && *end1 >= L'0' && *end1 <= L'9')
            {
                if (numBeg1 == NULL && *end1 != L'0')
                    numBeg1 = end1;
                end1++;
            }
        }
        const wchar_t* numBeg2 = NULL; // position of first non-zero digit
        BOOL isStr2 = (end2 >= strEnd2 || *end2 < L'0' || *end2 > L'9');
        if (isStr2) // text (even empty) or dot
        {
            if (findDots && end2 < strEnd2 && *end2 == L'.')
                end2++; // dot: if we are looking for them, take one at a time
            else        // text (even empty)
            {
                while (end2 < strEnd2 && (*end2 < L'0' || *end2 > L'9') && (!findDots || *end2 != L'.'))
                    end2++;
            }
        }
        else // number
        {
            while (end2 < strEnd2 && *end2 >= L'0' && *end2 <= L'9')
            {
                if (numBeg2 == NULL && *end2 != L'0')
                    numBeg2 = end2;
                end2++;
            }
        }

        if (isStr1 || isStr2) // comparison of text, dots or combined pairs of text, dots or numbers (everything except two numbers is compared as strings)
        {
            int ret;
            if (options.UsesLocale)
            {
                ret = CompareStringW(LOCALE_USER_DEFAULT, ignoreCase ? NORM_IGNORECASE : 0,
                                    beg1, (int)(end1 - beg1), beg2, (int)(end2 - beg2)) -
                      CSTR_EQUAL;
            }
            else
            {
                if (ignoreCase)
                    ret = WideICmpEx(beg1, (int)(end1 - beg1), beg2, (int)(end2 - beg2));
                else
                    ret = WideCmpEx(beg1, (int)(end1 - beg1), beg2, (int)(end2 - beg2));
            }
            if (ret != 0)
            {
                if (numericalyEqual != NULL)
                    *numericalyEqual = FALSE;
                return ret;
            }
        }
        else // comparison of two numbers
        {
            if (numBeg1 == NULL)
            {
                if (numBeg2 == NULL) // both numbers are zero
                {
                    if (suggestion == 0) // we are only interested in the first "suggestion" for result
                    {
                        if (end1 - beg1 > end2 - beg2)
                            suggestion = -1; // "000" < "00"
                        else if (end1 - beg1 < end2 - beg2)
                            suggestion = 1; // "00" > "000"
                    }
                }
                else // first number is zero, second number is not zero
                {
                    if (numericalyEqual != NULL)
                        *numericalyEqual = FALSE;
                    return -1; // "00" < "1"
                }
            }
            else
            {
                if (numBeg2 == NULL) // first number is not zero, second number is zero
                {
                    if (numericalyEqual != NULL)
                        *numericalyEqual = FALSE;
                    return 1; // "1" > "00"
                }
                else // both numbers are non-zero
                {
                    if (end1 - numBeg1 > end2 - numBeg2) // first number has more digits than second
                    {
                        if (numericalyEqual != NULL)
                            *numericalyEqual = FALSE;
                        return 1; // "100" > "99"
                    }
                    else
                    {
                        if (end1 - numBeg1 < end2 - numBeg2) // second number has more digits than first
                        {
                            if (numericalyEqual != NULL)
                                *numericalyEqual = FALSE;
                            return -1; // "99" < "100"
                        }
                        else // numbers have the same number of digits, compare them by value (equivalent to string comparison)
                        {
                            int ret = WideCmpEx(numBeg1, (int)(end1 - numBeg1), numBeg2, (int)(end2 - numBeg2));
                            if (ret != 0) // values are not equal
                            {
                                if (numericalyEqual != NULL)
                                    *numericalyEqual = FALSE;
                                return ret;
                            }
                            else // number values are the same, if they differ in the number of zeros in prefix, adopt this into "suggestion" for result
                            {
                                if (suggestion == 0) // we are only interested in the first "suggestion" for result
                                {
                                    if (end1 - beg1 > end2 - beg2)
                                        suggestion = -1; // "0001" < "001"
                                    else if (end1 - beg1 < end2 - beg2)
                                        suggestion = 1; // "001" > "0001"
                                }
                            }
                        }
                    }
                }
            }
        }

        if (end1 >= strEnd1 && end2 >= strEnd2)
            break; // end of comparison
        beg1 = end1;
        beg2 = end2;
    }

    if (numericalyEqual != NULL)
        *numericalyEqual = TRUE; // s1 and s2 are equal or numerically equal
    return suggestion;           // on equality or numerical equality we return the "suggested" result
}
