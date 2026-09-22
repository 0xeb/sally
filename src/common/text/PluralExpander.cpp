// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// PluralExpander — see PluralExpander.h. UI-free by construction: this TU must
// keep compiling without precomp.h so tests can build it directly.

#ifdef SALLY_PLURAL_EXPANDER_STANDALONE
#define SALLY_PLURAL_EXPANDER_STANDALONE_TEST 1
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#ifndef TRACE_E
#define TRACE_E(x) ((void)0)
#endif
#ifndef TRACE_EW
#define TRACE_EW(x) ((void)0)
#endif
extern std::wstring ThousandsSeparator;
#else
#include "precomp.h"
#endif

#include "common/text/PluralExpander.h"

//

// Converts a number (it may begin with a '+' character) to unsigned __int64.
// The len variable specifies the maximum count of processed characters.
// If 'isNum' is not NULL, it returns TRUE when the entire string
// 'str' represents a number.

unsigned __int64
StrToUInt64W(const wchar_t* str, int len, BOOL* isNum)
{
    const wchar_t* end = str + len;
    const wchar_t* s = str;
    while (s < end && *s <= L' ')
        s++;
    if (s < end && *s == L'+')
        s++;

    unsigned __int64 total = 0;
    const wchar_t* begNum = s;
    while (s < end && *s >= L'0' && *s <= L'9')
    {
        unsigned __int64 new_total = total * 10 + (*s - L'0');
        if (new_total >= total)
        {
            total = total * 10 + (*s - L'0');
            s++;
        }
        else
        {
            total = 0xffffffffffffffff;
            while (s < end && *s >= L'0' && *s <= L'9')
                s++;
            break;
        }
    }
    BOOL hasDigits = begNum != s;
    while (s < end && *s <= L' ')
        s++;
    if (isNum != NULL)
        *isNum = (hasDigits && s == end);
    return total;
}

std::wstring NumberToStr(const CQuadWord& number)
{
    const std::wstring digits = std::to_wstring(number.Value);
    if (ThousandsSeparator.empty() || digits.size() <= 3)
        return digits;

    std::wstring result;
    const size_t separatorCount = (digits.size() - 1) / 3;
    result.reserve(digits.size() + separatorCount * ThousandsSeparator.size());
    const size_t firstGroup = digits.size() - separatorCount * 3;
    result.append(digits, 0, firstGroup);
    for (size_t offset = firstGroup; offset < digits.size(); offset += 3)
    {
        result += ThousandsSeparator;
        result.append(digits, offset, 3);
    }
    return result;
}

//

// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// WARNING: whenever ExpandPluralStringW is modified it is also necessary to update
//          ValidatePluralStrings in the TRANSLATOR project
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

//
// ExpandPluralString

// Wide number formatting with the configured thousands separator
// (promoted out of worker.cpp - the plural helpers need it too).

// Wide twin of ExpandPluralString. Same grammar, same escape rules,
// same {index:form|form|form} syntax - the Translator's ValidatePluralStrings
// contract covers BOTH (any change here changes there).
int ExpandPluralStringW(wchar_t* lpOut, int nOutMax, const wchar_t* lpFmt, int nParCount,
                       const CQuadWord* lpParArray)
{
    const wchar_t* input = lpFmt;
    wchar_t* output = lpOut;
    wchar_t* outputNullTerm = lpOut + nOutMax - 1;
    int actParIndex = 0;

    struct CAuxParUsed
    {
        BOOL StackArr[20];
        BOOL* Arr;
        CAuxParUsed(int nParCount)
        {
            Arr = nParCount <= sizeof(StackArr) / sizeof(StackArr[0]) ? StackArr : new BOOL[nParCount];
            memset(Arr, 0, nParCount * sizeof(BOOL));
        }
        ~CAuxParUsed()
        {
            if (Arr != StackArr)
                delete[] (Arr);
        }
    } parUsedArr(max(0, nParCount));

    if (nOutMax > 0 && lpOut != NULL)
        *lpOut = 0;

    // check and skip the {!} signature
    if (input != NULL && *input++ == L'{' && *input++ == L'!' && *input++ == L'}' && nOutMax > 0)
    {
        while (*input != 0)
        {
            if (*input == L'\\' &&
                (*(input + 1) == L'|' || *(input + 1) == L'\\' || *(input + 1) == L':' ||
                 *(input + 1) == L'{' || *(input + 1) == L'}')) // escape sequence
            {
                input++;
                if (output >= outputNullTerm) // the buffer must also fit the terminating zero
                {
                    lpOut[nOutMax - 1] = 0;
                    TRACE_E("ExpandPluralStringW: truncated output string.");
                    return nOutMax - 1;
                }
                *output++ = *input++;
            }
            else
            {
                if (*input == L'{') // perform expansion of the curly brace
                {
                    input++;

                    // fetch the corresponding parameter value from the array
                    unsigned __int64 arg;
                    const wchar_t* parInd = input;
                    int parIndVal = 0;
                    while (*parInd >= L'0' && *parInd <= L'9')
                        parIndVal = 10 * parIndVal + *parInd++ - L'0';
                    if (*parInd == L':' && parInd > input) // an index was assigned, use it
                    {
                        if (parIndVal >= 1 && parIndVal <= nParCount)
                        {
                            input = parInd + 1;
                            parUsedArr.Arr[parIndVal - 1] = TRUE;
                            arg = lpParArray[parIndVal - 1].Value;
                        }
                        else
                        {
                            TRACE_E("ExpandPluralStringW: specified index of parameter is out of range: " << parIndVal);
                            *output = 0;
                            return (int)(output - lpOut);
                        }
                    }
                    else // use the next parameter in order
                    {
                        if (actParIndex < nParCount)
                        {
                            parUsedArr.Arr[actParIndex] = TRUE;
                            arg = lpParArray[actParIndex++].Value;
                        }
                        else
                        {
                            TRACE_E("ExpandPluralStringW: few parameters in array.");
                            *output = 0;
                            return (int)(output - lpOut);
                        }
                    }

                    while (*input != L'}' && *input != 0)
                    {
                        const wchar_t* subStr = input;
                        int subStrLen = 0;

                        while (*input != L'}' && *input != 0 && *input != L'|')
                        {
                            if (*input == L'\\' &&
                                (*(input + 1) == L'|' || *(input + 1) == L'\\' || *(input + 1) == L':' ||
                                 *(input + 1) == L'{' || *(input + 1) == L'}')) // escape sequence
                                input++;
                            subStrLen++;
                            input++;
                        }

                        if (*input == L'|')
                            input++;

                        const wchar_t* numStr = input;
                        int numStrLen = 0;

                        while (*input != L'}' && *input != 0 && *input != L'|')
                        {
                            if (*input == L'\\' &&
                                (*(input + 1) == L'|' || *(input + 1) == L'\\' || *(input + 1) == L':' ||
                                 *(input + 1) == L'{' || *(input + 1) == L'}')) // escape sequence
                                input++;
                            numStrLen++;
                            input++;
                        }

                        if (*input == L'|')
                            input++;

                        if (numStrLen == 0 && *input != L'}')
                        {
                            TRACE_EW(L"ExpandPluralStringW: syntax error: " << lpFmt);
                        }

                        unsigned __int64 num = 0;
                        if (numStrLen > 0)
                        {
                            BOOL isNum;
                            num = StrToUInt64W(numStr, numStrLen, &isNum);
                            if (!isNum)
                                TRACE_EW(L"ExpandPluralStringW: contains limit that is not a number: " << lpFmt);
                        }

                        // if this is the last string without interval limitation,
                        // or the value of arg is less than or equal to the interval boundary
                        if (numStrLen == 0 || arg <= num)
                        {
                            // insert the relevant substring into the output string
                            int i;
                            for (i = 0; i < subStrLen; i++)
                            {
                                if (*subStr == L'\\' &&
                                    (*(subStr + 1) == L'|' || *(subStr + 1) == L'\\' || *(subStr + 1) == L':' ||
                                     *(subStr + 1) == L'{' || *(subStr + 1) == L'}')) // escape sequence
                                    subStr++;
                                if (output >= outputNullTerm) // the buffer must also fit the terminating zero
                                {
                                    lpOut[nOutMax - 1] = 0;
                                    TRACE_E("ExpandPluralStringW: truncated output string.");
                                    return nOutMax - 1;
                                }
                                *output++ = *subStr++;
                            }

                            // and stop searching
                            while (*input != L'}' && *input != 0)
                            {
                                if (*input == L'\\' &&
                                    (*(input + 1) == L'|' || *(input + 1) == L'\\' || *(input + 1) == L':' ||
                                     *(input + 1) == L'{' || *(input + 1) == L'}')) // escape sequence
                                    input++;
                                input++;
                            }
                        }
                    }
                    if (*input == L'}')
                        input++;
                }
                else
                {
                    if (output >= outputNullTerm) // the buffer must also fit the terminating zero
                    {
                        lpOut[nOutMax - 1] = 0;
                        TRACE_E("ExpandPluralStringW: truncated output string.");
                        return nOutMax - 1;
                    }
                    *output++ = *input++;
                }
            }
        }
        *output = 0; // insert the terminator
    }
    else
        TRACE_E("ExpandPluralStringW: format string does not contain {!} signature or output buffer is too short.");

    int i;
    for (i = 0; i < nParCount; i++)
        if (!parUsedArr.Arr[i])
        {
            TRACE_E("ExpandPluralStringW: warning: some parameters from array were not used, zero-based index "
                    "of first unused parameter is "
                    << i);
            break;
        }

    return (int)(output - lpOut);
}

std::wstring ExpandPluralStringOwnedW(const wchar_t* format, int parameterCount,
                                      const CQuadWord* parameters)
{
    size_t capacity = format != NULL ? wcslen(format) + 32 : 64;
    capacity = max(capacity, (size_t)64);
    for (;;)
    {
        std::vector<wchar_t> output(capacity, L'\0');
        const int written = ExpandPluralStringW(output.data(), (int)output.size(), format,
                                                parameterCount, parameters);
        if (written >= 0 && (size_t)written < output.size() - 1)
            return std::wstring(output.data(), written);
        capacity *= 2;
    }
}
