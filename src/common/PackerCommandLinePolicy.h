// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <climits>
#include <cstddef>
#include <cwchar>
#include <string>
#include <windows.h>

namespace sally::pack
{

constexpr std::size_t kLegacyDosCommandLineLimit = 128;

// External DOS archivers write listing names in the active OEM code page. Keep the
// listing grammar byte-oriented, then cross into Sally's UTF-16 name/path contracts once,
// after the parser has identified an exact byte span.
inline bool DecodeOemListingText(const char* bytes, std::size_t length, std::wstring& wide)
{
    if (bytes == nullptr)
    {
        if (length != 0)
            return false;
        wide.clear();
        return true;
    }
    Win32DecodeTextLenient(CP_OEMCP, bytes, length, wide);
    return true;
}

// Returns masks from right to left while preserving the legacy pack-dialog grammar:
// an odd semicolon run separates masks, an even run escapes to half as many literal
// semicolons, and surrounding control/space characters are ignored. The caller owns
// the mutable buffer; 'maskList' remains at its prefix or becomes null after the last mask.
inline const wchar_t* NextMaskFromEnd(wchar_t*& maskList)
{
    if (maskList == nullptr)
        return nullptr;

    wchar_t* const begin = maskList;
    const std::size_t length = std::wcslen(begin);
    if (length == 0)
    {
        maskList = nullptr;
        return nullptr;
    }

    std::ptrdiff_t ptr = static_cast<std::ptrdiff_t>(length) - 1;
    std::ptrdiff_t end = ptr;
    while (true)
    {
        while (ptr >= 0 && begin[ptr] == L';')
            --ptr;
        if (((end - ptr) & 1) == 1)
            begin[end--] = L'\0';

        if (end >= 0 && begin[end] <= L' ')
        {
            do
            {
                --end;
            } while (end >= 0 && begin[end] <= L' ');
            begin[end + 1] = L'\0';
            ptr = end;
        }
        else
        {
            break;
        }
    }

    if (end < 0)
    {
        maskList = nullptr;
        return nullptr;
    }

    while (ptr >= 0)
    {
        if (begin[ptr] == L';')
        {
            std::ptrdiff_t preceding = ptr - 1;
            while (preceding >= 0 && begin[preceding] == L';')
                --preceding;
            if (((ptr - preceding) & 1) == 1)
                break;
            ptr = preceding;
        }
        else
        {
            --ptr;
        }
    }

    wchar_t* result;
    if (ptr < 0)
    {
        maskList = nullptr;
        result = begin;
    }
    else
    {
        begin[ptr] = L'\0';
        result = begin + ptr + 1;
    }

    while (*result != L'\0' && *result <= L' ')
        ++result;
    for (wchar_t* text = result; *text != L'\0'; ++text)
    {
        if (*text == L';' && *(text + 1) == L';')
            std::wmemmove(text, text + 1, std::wcslen(text + 1) + 1);
    }
    return result;
}

inline bool ShouldRejectLegacyCommandLine(bool supportsLongNames, std::size_t commandLineLength, bool usesListFile)
{
    return !supportsLongNames &&
           !usesListFile &&
           commandLineLength >= kLegacyDosCommandLineLimit;
}

// How the file-list Sally writes for an external archiver must be encoded.
enum class EListFileEncoding
{
    Oem,   // the default; what CharToOem has always produced
    Ansi,  // CP_ACP, selected per packer by NeedANSIListFile
    Utf8,  // the archiver was told to read the list as UTF-8
    Utf16, // ...or as UTF-16LE
};

// Reads the encoding out of the packer's own command line.
//
// This is deliberately NOT a separate setting. The list file is a protocol with the
// archiver, and the archiver only reads it as UTF-8 because the command line told it to.
// A checkbox next to the command line would be a second place to state the same fact, and
// the two would drift - a user pasting a new command line would silently get a list file
// in the wrong encoding, which is the failure this whole area already suffers from.
// Deriving it means Sally cannot disagree with the tool it is driving.
//
// The grammar is RAR's -sc<charset><objects>, which Sally's own bundled RAR configuration
// has always used: the shipped default carried -scol, meaning OEM for list files. So the
// charset letter is read from the same switch that was already there. 'l' among the
// objects is what makes the switch apply to list files at all; -scfg (UTF-8 for messages)
// says nothing about them.
//
//   -scol  OEM list files      -scfl  UTF-8 list files
//   -scal  ANSI list files     -scul  UTF-16 list files
//
// 'fallback' is returned when the command line says nothing, so the caller's existing
// OEM/ANSI choice keeps deciding for every packer that has no such switch - which is all
// of the DOS-era ones, none of which could read a Unicode list anyway.
template <typename CharT>
inline EListFileEncoding ListFileEncodingFromCommandLineImpl(const CharT* commandLine,
                                                             EListFileEncoding fallback)
{
    if (commandLine == nullptr)
        return fallback;

    for (const CharT* p = commandLine; *p != 0; ++p)
    {
        // A switch, not a substring of a path: it must follow whitespace or a quote and
        // start with '-' or '/'. "C:\-scfl\a.rar" must not be read as one.
        const bool atSwitchStart = (p == commandLine) || *(p - 1) == ' ' || *(p - 1) == '\t' ||
                                   *(p - 1) == '"';
        if (!atSwitchStart || (*p != '-' && *p != '/'))
            continue;
        if (*(p + 1) == 0 || *(p + 2) == 0 ||
            !(*(p + 1) == 's' && *(p + 2) == 'c'))
            continue;

        const CharT charset = *(p + 3);
        if (charset == 0)
            continue;

        // Objects run until the end of the switch. Require 'l' explicitly.
        bool appliesToListFiles = false;
        for (const CharT* o = p + 4; *o != 0 && *o != ' ' && *o != '\t' && *o != '"'; ++o)
        {
            if (*o == 'l')
            {
                appliesToListFiles = true;
                break;
            }
        }
        if (!appliesToListFiles)
            continue;

        switch (charset)
        {
        case 'f':
            return EListFileEncoding::Utf8;
        case 'u':
            return EListFileEncoding::Utf16;
        case 'a':
            return EListFileEncoding::Ansi;
        case 'o':
            return EListFileEncoding::Oem;
        default:
            break; // an unknown charset letter is not a guess we should make
        }
    }
    return fallback;
}

inline EListFileEncoding ListFileEncodingFromCommandLine(const char* commandLine,
                                                         EListFileEncoding fallback)
{
    return ListFileEncodingFromCommandLineImpl(commandLine, fallback);
}

inline EListFileEncoding ListFileEncodingFromCommandLine(const wchar_t* commandLine,
                                                         EListFileEncoding fallback)
{
    return ListFileEncodingFromCommandLineImpl(commandLine, fallback);
}

inline EListFileEncoding ListFileEncodingFromCommandLine(std::nullptr_t,
                                                         EListFileEncoding fallback)
{
    return fallback;
}

// Whether an entry written in 'encoding' can carry any name at all, or only names the
// system code page happens to spell. The single-byte encodings cannot, which is why the
// narrowing guard still applies to them.
inline bool ListFileEncodingIsUnicode(EListFileEncoding encoding)
{
    return encoding == EListFileEncoding::Utf8 || encoding == EListFileEncoding::Utf16;
}

// The byte order mark a Unicode list file opens with. Empty for the single-byte
// encodings, which have none and must keep their existing byte-for-byte output.
//
// RAR identifies a UTF-16 list by its BOM and writes one itself. UTF-8 does not strictly
// need one, but a list that states what it is cannot be misread by a tool that guesses.
inline std::string ListFileBom(EListFileEncoding encoding)
{
    if (encoding == EListFileEncoding::Utf16)
        return std::string("\xFF\xFE", 2);
    if (encoding == EListFileEncoding::Utf8)
        return std::string("\xEF\xBB\xBF", 3);
    return std::string();
}

// Encodes one entry, terminator included, for a Unicode list file.
//
// Returns the exact bytes to write. Kept pure and separate from the file handle because
// this is the step the whole wide path exists to reach - 'nameW' has never been through
// the code page, so a name CP_ACP cannot spell survives here intact rather than arriving
// as the question marks the single-byte branches have to refuse.
//
// CRLF, not LF: these lists are read by DOS-descended archivers, and the file is opened
// in binary mode precisely so that nothing rewrites a 0x0A that is half of a UTF-16 code
// unit rather than a line ending.
inline std::string EncodeListFileEntry(EListFileEncoding encoding, const std::wstring& nameW)
{
    if (encoding == EListFileEncoding::Utf16)
    {
        const std::wstring line = nameW + L"\r\n";
        return std::string(reinterpret_cast<const char*>(line.c_str()), line.length() * sizeof(wchar_t));
    }

    std::string utf8;
    if (!Win32EncodeText(CP_UTF8, nameW, utf8))
        return {};
    utf8 += "\r\n";
    return utf8;
}

// Encodes an entry for the complete list-file protocol. UTF-16 is byte-preserving,
// while UTF-8 refuses malformed UTF-16. The OEM/ANSI branches refuse whenever the
// selected code page cannot represent the name exactly; emitting a replacement '?'
// would turn the intended archive name into a wildcard.
inline bool TryEncodeListFileEntry(EListFileEncoding encoding, const std::wstring& nameW,
                                   std::string& bytes)
{
    std::string encoded;
    if (encoding == EListFileEncoding::Utf16)
    {
        encoded = EncodeListFileEntry(encoding, nameW);
        bytes.swap(encoded);
        return true;
    }

    const UINT codePage = encoding == EListFileEncoding::Utf8
                              ? CP_UTF8
                              : (encoding == EListFileEncoding::Ansi ? ::GetACP() : ::GetOEMCP());
    if (!Win32EncodeTextExact(codePage, nameW, encoded))
        return false;

    encoded += "\r\n";
    bytes.swap(encoded);
    return true;
}

} // namespace sally::pack
