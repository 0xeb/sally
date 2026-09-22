// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//*****************************************************************************
//*****************************************************************************
//
// Original regexp.h
//
//*****************************************************************************
//*****************************************************************************

/*
 * Definitions etc. for regexp(3) routines.
 *
 * Caveat:  this is V8 regexp(3) [actually, a reimplementation thereof],
 * not the System V one.
 */
#define NSUBEXP 10
// The Spencer core is byte-domain: regexp.cpp defines
// regerror(const char*) :94, regcomp(char*, const char*&) :655 and
// regexec(regexp*, char*, int) :1181. These declarations had been widened
// over them - and because a FREE function with a mismatched parameter type
// becomes an OVERLOAD rather than a redefinition, it raised no C2511 and
// never showed up in the orphan column that found the class members.
typedef struct regexp
{
    char* startp[NSUBEXP];
    char* endp[NSUBEXP];
    char regstart;   /* Internal use only. */
    char reganch;    /* Internal use only. */
    char* regmust;   /* Internal use only. */
    int regmlen;     /* Internal use only. */
    char program[1]; /* Unwarranted chumminess with compiler. */
} regexp;

regexp* regcomp(char* exp, const char*& lastErrorText);
int regexec(regexp* prog, char* string, int offset);
void regerror(const char* error);

//*****************************************************************************
//*****************************************************************************
//
// My part of regexp.h
//
//*****************************************************************************
//*****************************************************************************

// Errors that can occur during compilation and searching of regular expressions
enum CRegExpErrors
{
    reeNoError,
    reeLowMemory,
    reeEmpty,
    reeTooBig,
    reeTooManyParenthesises,
    reeUnmatchedParenthesis,
    reeOperandCouldBeEmpty,
    reeNested,
    reeInvalidRange,
    reeUnmatchedBracket,
    reeFollowsNothing,
    reeTrailingBackslash,
    reeInternalDisaster,
};

// Function that returns the text of the occurred error
const char* RegExpErrorText(CRegExpErrors err);

// search flags
#define sfCaseSensitive 0x01 // 0. bit = 1
#define sfForward 0x02       // 1. bit = 1

//*****************************************************************************
//
// CRegularExpression
//

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
class CRegularExpression
{
public:
    static const char* LastError; // Text of last error

    // How the engine should fold case when sfCaseSensitive is clear.
    //
    // Case insensitivity here is implemented by lowercasing BOTH the pattern and the
    // subject line and then matching exactly, so the fold has to agree with the encoding
    // of the bytes it is handed.
    enum class FoldEncoding
    {
        // Fold every byte through LowerCase[], the CP_ACP table. Correct when one byte is
        // one character: the viewer searching raw file bytes, zip's masks, and Find's
        // legacy-bytes arm. This is the default, so every pre-existing caller is unchanged.
        Acp,
        // The subject and pattern are UTF-8. Folding UTF-8 through the ACP table corrupts
        // lead bytes (CP-1252 maps 0xC3 to 0xE3), which both loses real matches and
        // invents false ones in unrelated scripts, so non-ASCII is folded by code point
        // instead - see common/text/Utf8CaseFold.h.
        Utf8,
    };

protected:
    const char* LastErrorText;
    char* OriginalPattern;
    regexp* Expression; // Compiled regular expression
    WORD Flags;

    char* Line;                // Buffer for line
    const char* OrigLineStart; // Pointer to the beginning of original text (passed to SetLine() as 'start')
    int Allocated;             // How many bytes are allocated
    int LineLength;            // Current length of line
    FoldEncoding Folding;      // Encoding the case fold assumes; see FoldEncoding

public:
    CRegularExpression()
    {
        Expression = NULL;
        OriginalPattern = NULL;
        Flags = sfCaseSensitive | sfForward;
        Line = NULL;
        OrigLineStart = NULL;
        Allocated = 0;
        LineLength = 0;
        LastErrorText = NULL;
        Folding = FoldEncoding::Acp;
    }

    ~CRegularExpression()
    {
        if (Expression != NULL)
            free(Expression);
        if (OriginalPattern != NULL)
            free(OriginalPattern);
        if (Line != NULL)
            free(Line);
    }

    BOOL IsGood() const { return OriginalPattern != NULL && Expression != NULL; }
    const char* GetPattern() const { return OriginalPattern; }

    // Must be called BEFORE Set()/SetFlags(): the fold is applied when the pattern is
    // compiled, so changing it afterwards would leave the pattern and the subject folded
    // by different rules.
    void SetFoldEncoding(FoldEncoding folding) { Folding = folding; }
    FoldEncoding GetFoldEncoding() const { return Folding; }

    const char* GetLastErrorText() const { return LastErrorText; }
    void Clear() noexcept;
    BOOL Set(const char* pattern, WORD flags); // Returns FALSE on error (call GetLastErrorText method)
    BOOL SetFlags(WORD flags);                 // Returns FALSE on error (call GetLastErrorText method)

    BOOL SetLine(const char* start, const char* end); // Line of text to search in, returns FALSE on error (call GetLastErrorText method)

    int SearchForward(int start, int& foundLen);
    int SearchBackward(int length, int& foundLen);

    // Replaces variables \1 ... \9 with text captured by corresponding parentheses
    // 'pattern' is the pattern used to replace found match, 'buffer' buffer
    // for output, 'bufSize' maximum size of text including terminating NULL
    // character, in variable 'count' returns the number of characters copied to buffer
    // Returns TRUE if the expression fits completely into the buffer
    BOOL ExpandVariables(char* pattern, char* buffer,
                         int bufSize, int* count);

    // Return values
    //
    // 0 Searched text was not found, nothing was copied to 'buffer'
    // 1 Text was successfully replaced
    // 2 'buffer' is too small
    int ReplaceForward(int start, char* pattern, BOOL global,
                       char* buffer, int bufSize);

protected:
    // Reverses regular expression - for backward searching
    // EXPRESSION MUST BE SYNTACTICALLY CORRECT! OTHERWISE IT DOES NOT WORK CORRECTLY!
    // e.g., "a)b(d)(" -> "((d)b)a" which is incorrect
    void ReverseRegExp(char*& dstExpEnd, char* srcExp, char* srcExpEnd);
};
