// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstring>
#include <cwchar>
#include <string>

#include "../../common/Win32TextCodec.h"

// Ceiling for the engine's grow-and-retry buffer loops (CRenamer::RenameOwned,
// CVarString::ExecuteOwned).
//
// Those loops retry on a negative return, but Rename/Execute answer -1 for a genuine
// failure - an unencodable name, an invalid $(Date) format, a misconfigured renamer - just
// as readily as for "the buffer was too small". Retrying a genuine failure all the way to
// INT_MAX means ~23 doublings ending in a zeroed 2 GB allocation, for every file, before
// the caller is told no. This is not a path-length limit on the RESULT: it is the point
// past which growing the buffer cannot possibly help, because a rename target that long
// could never be handed to a Win32 rename API in the first place. That API's limit is
// 32767 UTF-16 units, and this buffer holds the UTF-8 form, which costs at most three
// bytes per unit.
inline constexpr size_t RenamerEngineBufferCeiling = 32767 * 3 + 1;

inline std::wstring RenamerTextToWide(const char* text, int length = -1)
{
    if (text == NULL)
        return std::wstring();
    const size_t inputLength = length < 0 ? strlen(text) : static_cast<size_t>(length);
    std::wstring result;
    if (!Win32DecodeText(CP_UTF8, text, inputLength, result) &&
        !Win32DecodeText(CP_ACP, text, inputLength, result))
        return std::wstring();
    return result;
}

inline bool TryRenamerTextToWide(const char* text, std::wstring& result,
                                 int length = -1)
{
    result.clear();
    if (text == nullptr)
        return false;
    const size_t inputLength = length < 0 ? strlen(text) : static_cast<size_t>(length);
    return Win32DecodeText(CP_UTF8, text, inputLength, result).Succeeded();
}

inline std::string WideToRenamerText(const wchar_t* text, int length = -1)
{
    if (text == NULL)
        return std::string();
    const size_t inputLength = length < 0 ? wcslen(text) : static_cast<size_t>(length);
    std::string result;
    if (!Win32EncodeText(CP_UTF8, text, inputLength, result))
        return std::string();
    return result;
}

inline bool TryWideToRenamerText(const wchar_t* text, std::string& result,
                                 int length = -1)
{
    result.clear();
    if (text == nullptr)
        return false;
    const size_t inputLength = length < 0 ? wcslen(text) : static_cast<size_t>(length);
    return Win32EncodeText(CP_UTF8, text, inputLength, result).Succeeded();
}

// Cut positions such as $(Name:2,4) count characters, but the engine holds names as UTF-8,
// where a letter like 'Ž' takes two bytes. Counting bytes would shift every later position
// and could split a letter into invalid UTF-8, so positions step over whole UTF-8 sequences
// (one character = one code point). Both helpers stop at the text's ends, so an
// out-of-range position simply selects up to the end or start.
inline bool IsRenamerTextContinuationByte(char c)
{
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

inline const char* AdvanceRenamerTextCharacters(const char* start, const char* end, int count)
{
    const char* p = start;
    for (; count > 0 && p < end; --count)
    {
        ++p;
        while (p < end && IsRenamerTextContinuationByte(*p))
            ++p;
    }
    return p;
}

inline const char* RetreatRenamerTextCharacters(const char* start, const char* end, int count)
{
    const char* p = end;
    for (; count > 0 && p > start; --count)
    {
        --p;
        while (p > start && IsRenamerTextContinuationByte(*p))
            --p;
    }
    return p;
}

// The text a cut variable selects: a start and an end position, each counted in characters
// from the start of the source or, when 'fromEnd', back from its end. An empty or inverted
// range has End <= Start.
struct CRenamerTextCut
{
    const char* Start;
    const char* End;
};

inline CRenamerTextCut SelectRenamerTextCut(const char* start, const char* end,
                                            int cutStart, bool startFromEnd,
                                            int cutEnd, bool endFromEnd)
{
    CRenamerTextCut cut;
    cut.Start = startFromEnd ? RetreatRenamerTextCharacters(start, end, cutStart)
                             : AdvanceRenamerTextCharacters(start, end, cutStart);
    cut.End = endFromEnd ? RetreatRenamerTextCharacters(start, end, cutEnd)
                         : AdvanceRenamerTextCharacters(start, end, cutEnd);
    return cut;
}

struct CRenamerNamePartOffsets
{
    size_t File;
    size_t Extension;
};

// Renamer expressions are UTF-8, but both path separators and dots are single-byte ASCII.
// Locate them with indices so empty and single-component results never form a pointer before
// the caller's buffer.
inline CRenamerNamePartOffsets FindRenamerNamePartOffsets(const char* text, size_t length,
                                                          bool isDirectory)
{
    CRenamerNamePartOffsets parts = {0, length};
    if (text == nullptr)
        return parts;

    size_t file = length;
    while (file > 0 && text[file - 1] != '\\')
        --file;
    parts.File = file;

    if (!isDirectory)
    {
        for (size_t i = length; i > file; --i)
        {
            if (text[i - 1] == '.')
            {
                parts.Extension = i - 1;
                break;
            }
        }
    }
    return parts;
}
