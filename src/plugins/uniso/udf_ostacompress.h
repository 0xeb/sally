// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// OSTA CS0 "compressed unicode" decoder, extracted from udf.cpp so it can be
// unit-tested directly (same shape as renamer_name_narrow.h/unrar_name_narrow.h/
// net_resource_narrow.h - zero dependency on precomp.h/the Salamander SDK).
//
// UDF file identifiers (CUDF::ReadFileIdentifier) are stored on-disk in this OSTA-compressed
// form and decode straight to UTF-16 - there is no ANSI/OEM step in the UDF spec itself. The
// owned decoder is what the real file-identifier pipeline
// (ReadFileIdentifier -> ScanDir -> AddFileDir -> CFileData::Name) uses; a narrow
// WideCharToMultiByte(CP_ACP, 0, ...) step used to sit in front of every caller, including this
// one, silently best-fit-substituting any UDF file name outside the machine's code page (DVD/
// Blu-ray images routinely carry them) before it ever reached CFileData::Name - the same bug
// shape this codebase has already fixed in zip/wmobile/nethood. Diagnostic output is encoded only
// at the named report boundary.

#include <windows.h>
#include <string>

inline bool DecodeOSTACompressedOwned(const BYTE* id, int len, std::wstring& result) noexcept
{
    if (id == nullptr || len <= 0 || (id[0] != 8 && id[0] != 16))
        return false;

    try
    {
        const int bytesPerCharacter = id[0] == 16 ? 2 : 1;
        const int usableBytes = id[0] == 16 ? (len - 1) & ~1 : len - 1;
        std::wstring staged;
        staged.reserve((size_t)usableBytes / bytesPerCharacter);
        for (int offset = 1; offset < 1 + usableBytes; offset += bytesPerCharacter)
        {
            wchar_t ch = id[0] == 16 ? (wchar_t)((id[offset] << 8) | id[offset + 1]) : (wchar_t)id[offset];
            if (ch == L'\0')
                break;
            staged.push_back(ch);
        }
        result.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
