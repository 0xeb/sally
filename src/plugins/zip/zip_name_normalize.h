// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <type_traits>

// Pure, UI-free, precomp.h-free core shared by CZipCommon::ProcessName (narrow) and
// CZipCommon::ProcessNameW (wide) in common.cpp. Both callers must apply the exact same
// slash/space/illegal-char normalization to a zip entry name, whichever character width
// they decode it into - this header is the single source of truth for that algorithm so
// the two widths cannot silently drift apart. No dependency on the Salamander SDK: takes
// an explicit [sour, end) input range and an output buffer, has no side effects.

template <typename CharT>
inline bool ZipNameIsSlash(CharT c)
{
    return c == CharT('/') || c == CharT('\\');
}

template <typename CharT>
inline bool ZipNameIsIllegalChar(CharT c)
{
    return c == CharT('*') || c == CharT('?') || c == CharT('<') || c == CharT('>') ||
           c == CharT('|') || c == CharT('"') || c == CharT(':');
}

template <typename CharT>
inline bool ZipNameIsControlChar(CharT c)
{
    using UnsignedT = typename std::make_unsigned<CharT>::type;
    return static_cast<UnsignedT>(c) < 32;
}

// Normalizes [sour, end) into dest (caller-owned buffer, must be at least (end - sour) + 1
// CharT elements): strips leading slashes, collapses repeated slashes, turns leading/
// trailing spaces in each path component into underscores, replaces illegal Windows path
// characters and control characters with underscores, and strips one trailing directory
// slash. Returns the written length (not counting the terminating NUL, which IS written).
// If a trailing directory slash was stripped, sets *outWasDirBySlash to true (the caller
// decides what, if anything, to do with that - e.g. CZipCommon::ProcessName/ProcessNameW
// OR fileHeader->ExternAttr with FILE_ATTRIBUTE_DIRECTORY, matching legacy behavior).
template <typename CharT>
inline size_t NormalizeZipEntryName(const CharT* sour, const CharT* end, CharT* dest, bool* outWasDirBySlash)
{
    CharT* destStart = dest;
    if (outWasDirBySlash)
        *outWasDirBySlash = false;

    while (sour < end && ZipNameIsSlash(*sour))
        sour++;
    while (sour < end && *sour == CharT(' '))
    {
        *dest++ = CharT('_');
        sour++;
    }
    while (sour < end && *sour != CharT(0))
    {
        if (ZipNameIsSlash(*sour))
        {
            sour++;
            while (sour < end && ZipNameIsSlash(*sour))
                sour++;
            CharT* iter = dest - 1;
            while (iter >= destStart && *iter == CharT(' '))
                *iter-- = CharT('_');
            *dest++ = CharT('\\');
            while (sour < end && *sour == CharT(' '))
            {
                *dest++ = CharT('_');
                sour++;
            }
        }
        else if (ZipNameIsControlChar(*sour) || ZipNameIsIllegalChar(*sour))
        {
            *dest++ = CharT('_');
            sour++;
        }
        else
        {
            *dest++ = *sour++;
        }
    }
    if (dest > destStart && *(dest - 1) == CharT('\\'))
    {
        dest--;
        if (outWasDirBySlash)
            *outWasDirBySlash = true;
    }
    CharT* iter = dest - 1;
    while (iter >= destStart && *iter == CharT(' '))
        *iter-- = CharT('_');

    *dest = CharT(0);
    return static_cast<size_t>(dest - destStart);
}
