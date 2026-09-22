// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// ****************************************************************************
// Boyer-Moore substring search algorithm
// ****************************************************************************

#pragma once

// search flags
#define sfCaseSensitive 0x01 // 0. bit = 1
#define sfForward 0x02       // 1. bit = 1

// ****************************************************************************

// THIS ENGINE IS BYTE-DOMAIN BY ALGORITHM, NOT BY CONVENIENCE.
// A sweep widened these DECLARATIONS on top of a consistently narrow
// implementation, and the "FLOOR" label on the .cpp meant nobody re-read the
// header. Four independent authorities say narrow is correct:
//   1. THE ALGORITHM - Boyer-Moore's bad-character tables (LowerCase, Fail1)
//      have 256 entries. A wchar_t index above U+00FF read off the end of both.
//   2. THE PLUGIN ABI - zip.cpp:3603-3607/3641/3642 and the FROZEN v107
//      compat/sdk107/spl_gen.h:576/612/616 both declare const char*.
//   3. EVERY CALLER - find.cpp and viewer_interaction_scrolling.cpp pass
//      memory-mapped file bytes, with explicit (char*) casts.
//   4. UPSTREAM - Open Salamander's regedt/utils.cpp declares
//      ConvertHexToString(LPWSTR, char* hex, int&); its caller names it patternA.
class CSearchData
{
public:
    CSearchData()
    {
        Fail1 = Fail2 = NULL;
        OriginalPattern = NULL;
        Length = 0;
        Pattern = NULL;
        Flags = 0;
    }

    ~CSearchData()
    {
        Clear();
    }

    int GetLength() const { return Length; }
    const char* GetPattern() const { return OriginalPattern; }

    BOOL IsGood() const { return OriginalPattern != NULL &&
                                 Pattern != NULL &&
                                 Fail1 != NULL && Fail2 != NULL; }
    void Clear() noexcept;
    void SetFlags(WORD flags);
    void Set(const char* pattern, WORD flags);
    // for patterns containing '\0'
    // buffer pattern must have length (length + 1) characters (compatibility with strings)
    void Set(const char* pattern, const int length, WORD flags);

    inline int SearchForward(const char* text, int length, int start);
    inline int SearchBackward(const char* text, int length);

protected:
    int Minimum(int a, int b) { return (a < b) ? a : b; }
    int Maximum(int a, int b) { return (a > b) ? a : b; }

    int* Fail1;            // fail array for current character
    int* Fail2;            // fail array for substring occurrence from right
    char* OriginalPattern; // original pattern to search for
    char* Pattern;         // pattern to search for in appropriate form (Flag)
    int Length;            // pattern length

private:
    BOOL Initialize(); // called only from SetFlags

    WORD Flags; // change through SetFlags
};

//
// ****************************************************************************
// SearchForward
// returns pattern position or -1
// text - text to search in
// length - length of text string
// start - first character numbered from 0
//

int CSearchData::SearchForward(const char* text, int length, int start)
{
    int l1 = Length - 1;
    int i, j = l1 + start;
    if (Flags & sfCaseSensitive)
    {
        while (j < length)
        {
            i = l1;
            while (i >= 0 && text[j] == Pattern[i])
            {
                i--;
                j--;
            }
            if (i == -1)
                return j + 1;
            j += Maximum(Fail1[text[j]], Fail2[i]);
        }
    }
    else
    {
        while (j < length)
        {
            i = l1;
            while (i >= 0 && LowerCase[text[j]] == Pattern[i])
            {
                i--;
                j--;
            }
            if (i == -1)
                return j + 1;
            j += Maximum(Fail1[LowerCase[text[j]]], Fail2[i]);
        }
    }
    return -1;
}

//
// ****************************************************************************
// SearchBackward
// returns pattern position or -1
// text - text to search in
// length - length of text string
//

int CSearchData::SearchBackward(const char* text, int length)
{
    int l1 = Length - 1;
    int l2 = length - 1;
    int i, j = l1;
    if (Flags & sfCaseSensitive)
    {
        while (j < length)
        {
            i = l1;
            while (i >= 0 && text[l2 - j] == Pattern[i])
            {
                i--;
                j--;
            }
            if (i == -1)
                return l2 - j - Length;
            j += Maximum(Fail1[text[l2 - j]], Fail2[i]);
        }
    }
    else
    {
        while (j < length)
        {
            i = l1;
            while (i >= 0 && LowerCase[text[l2 - j]] == Pattern[i])
            {
                i--;
                j--;
            }
            if (i == -1)
                return l2 - j - Length;
            j += Maximum(Fail1[LowerCase[text[l2 - j]]], Fail2[i]);
        }
    }
    return -1;
}
