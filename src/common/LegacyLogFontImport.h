// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Import of a LOGFONT that a pre-Unicode release persisted as raw bytes.
//
// Several plugins store a LOGFONT straight into a REG_BINARY value. Every
// release before the global UNICODE flip compiled that as LOGFONTA - 60 bytes,
// with lfFaceName a char[32] in the process code page. LOGFONTW is 92 bytes
// with lfFaceName a wchar_t[32], so the two layouts are NOT interchangeable:
// reading the old bytes into the new struct leaves the numeric header intact
// and turns the face name into mojibake, which GDI then silently replaces with
// an arbitrary substitute font.
//
// The registry readers accept a value SHORTER than the buffer, so the size
// mismatch is not reported anywhere. Each plugin therefore has to know which
// layout its stored value is in, from its own config version, and call this to
// convert. This mirrors what core already does for its own font persistence
// (LoadLogFont in consts.h reads the historical narrow payload and writes the
// current one under a separate value name).
//
// Header-only: the plugins that need it do not link src/common/*.cpp.

#pragma once

#include <windows.h>
#include <cstring>
#include <string>

#include "common/Win32TextCodec.h"

// Converts the persisted narrow LOGFONT into the wide one Sally now uses.
// Every numeric field is copied verbatim; only the face name changes domain.
// Returns false when the stored face name cannot be decoded at all, in which
// case the caller should keep its default font rather than install a blank one.
inline bool ImportLegacyLogFont(const LOGFONTA& legacy, LOGFONTW& wide)
{
    wide.lfHeight = legacy.lfHeight;
    wide.lfWidth = legacy.lfWidth;
    wide.lfEscapement = legacy.lfEscapement;
    wide.lfOrientation = legacy.lfOrientation;
    wide.lfWeight = legacy.lfWeight;
    wide.lfItalic = legacy.lfItalic;
    wide.lfUnderline = legacy.lfUnderline;
    wide.lfStrikeOut = legacy.lfStrikeOut;
    wide.lfCharSet = legacy.lfCharSet;
    wide.lfOutPrecision = legacy.lfOutPrecision;
    wide.lfClipPrecision = legacy.lfClipPrecision;
    wide.lfQuality = legacy.lfQuality;
    wide.lfPitchAndFamily = legacy.lfPitchAndFamily;

    // lfFaceName is not required to be terminated when it fills the array.
    size_t length = 0;
    while (length < sizeof(legacy.lfFaceName) && legacy.lfFaceName[length] != '\0')
        ++length;

    std::wstring face;
    if (!Win32DecodeAcpPermissive(legacy.lfFaceName, length, face).Succeeded())
        return false;

    // A face name longer than the wide array cannot be stored, and a truncated
    // one names a different font; leave the caller's default in place instead.
    if (face.size() >= (sizeof(wide.lfFaceName) / sizeof(wchar_t)))
        return false;

    memset(wide.lfFaceName, 0, sizeof(wide.lfFaceName));
    if (!face.empty())
        memcpy(wide.lfFaceName, face.data(), face.size() * sizeof(wchar_t));
    return true;
}
