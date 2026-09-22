// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Registry-corruption bug family: the shared registry facade's
// REG_SZ path (SetValueW/GetValueW, sally_strings_waitwindow.cpp) is
// wide-only - on write it derives the byte count from wcslen() over the
// caller's data (bounded/refused rather than OOB-reading, see
// reg_sz_safe_length.h), and on read it copies the stored UTF-16LE bytes into
// the caller's buffer with zero conversion. A plugin field that stays narrow
// (char*) by design - byte-owned protocol/bookmark/UI text, not something a
// widening tick converts - therefore cannot round-trip through this facade
// at all without an explicit bridge at the Load()/Save() boundary: writes are
// refused outright and reads of an existing wide value corrupt a narrow
// destination.
//
// These two functions are that bridge, factored out as pure conversions
// (no registry access) so the conversion itself is directly unit-testable
// without a fake-host registry harness - same shape as reg_sz_safe_length.h
// and zip_name_normalize.h. A caller wires them to registry->SetValue/
// GetValue; see plugins/ftp/ftp.cpp's SetValueSZ/GetValueSZ for the wiring.

#include <windows.h>

#include <new>
#include <stdexcept>
#include <string>

#include "plugin_text_encoding.h"

// Narrow -> wide for a REG_SZ write. Lossless: the narrow bytes came from
// CP_ACP in the first place (same contract as ToWideArg in
// plugin_narrow_compat.h), so converting back to UTF-16 recovers exactly
// what they meant.
inline BOOL EncodeRegSzFromNarrowOwned(const char* narrowValue,
                                       std::wstring& wideValue) noexcept
{
    wideValue.clear();
    if (narrowValue == NULL)
        return FALSE;
    return sally::plugin_text::DecodeAcp(narrowValue, wideValue) ? TRUE : FALSE;
}

inline std::wstring EncodeRegSzFromNarrow(const char* narrowValue)
{
    std::wstring wideValue;
    EncodeRegSzFromNarrowOwned(narrowValue, wideValue);
    return wideValue;
}

// Wide -> narrow for a REG_SZ read, lossy for characters outside CP_ACP (same
// contract as WideToAnsi elsewhere in this codebase). Returns FALSE (buffer
// left untouched) rather than truncate if the converted value would not fit
// in 'narrowBufSize' bytes/chars.
inline BOOL DecodeRegSzToNarrow(const wchar_t* wideValue, char* narrowBuf, int narrowBufSize)
{
    if (wideValue == NULL || narrowBuf == NULL || narrowBufSize <= 0)
        return FALSE;
    // EXACT, and it matters more here than almost anywhere else: this decodes
    // REG_SZ values that include regedt's Command/Arguments/InitDir (program paths) and every
    // plugin's MRU history. A best-fit substitution does not degrade a display - it stores a
    // history entry the user can later re-select, believing it names the file it did before.
    // Every caller already checks this BOOL, so refusing is the cheap part.
    std::string staged;
    if (!sally::plugin_text::EncodeAcpExact(wideValue, staged) ||
        staged.size() + 1 > static_cast<size_t>(narrowBufSize))
        return FALSE;
    std::memcpy(narrowBuf, staged.c_str(), staged.size() + 1);
    return TRUE;
}

inline BOOL DecodeRegSzToNarrowOwned(const wchar_t* wideValue,
                                     std::string& narrowValue) noexcept
{
    narrowValue.clear();
    if (wideValue == NULL)
        return FALSE;
    return sally::plugin_text::EncodeAcpExact(wideValue, narrowValue) ? TRUE : FALSE;
}
