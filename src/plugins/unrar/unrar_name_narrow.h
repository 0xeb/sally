// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// CPluginInterfaceForArchiver::ReadHeader (unrar.cpp) fills CFileHeader's
// legacy narrow FileName mirror from the archive entry's genuine wide name via
// WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, ...) - WC_COMPOSITECHECK does not prevent
// best-fit substitution (unlike WC_NO_BEST_FIT_CHARS), so an entry name outside CP_ACP (any
// non-Latin script, e.g. Cyrillic or Japanese) is silently mapped to a different, plausible-
// looking Latin character instead of failing. FileNameW (the genuine wide field) is populated
// correctly and is what the real extraction path prefers; FileName only reaches user-facing
// dialog text (skip/overwrite/password/too-long-name prompts) and one defensive re-widening
// fallback for the case FileNameW is empty. Even for display-only text, a silently-substituted
// character claims false precision about what the archive entry is actually named - this helper
// refuses instead, leaving the mirror an honest "?" so a viewer knows a character was lost
// rather than trusting a look-alike.
//
// Zero dependency on precomp.h/the Salamander SDK, so unlike the plugin code itself this is
// directly unit-testable (same shape as renamer_name_narrow.h/net_resource_narrow.h).

#include <windows.h>

#include <cstring>
#include <cwchar>
#include <string>

#include "../../common/Win32TextCodec.h"

inline constexpr UINT RAR_LEGACY_CODE_PAGE = CP_ACP;

inline bool DecodeRarLegacyName(const char* bytes, std::wstring& name)
{
    if (bytes == NULL)
    {
        std::wstring empty;
        name.swap(empty);
        return true;
    }
    Win32DecodeTextLenient(RAR_LEGACY_CODE_PAGE, bytes, strlen(bytes), name);
    return true;
}

inline bool CopyRarLegacyNameToWideExact(const char* bytes, wchar_t* target, size_t targetCount)
{
    if (target == NULL || targetCount == 0)
        return false;

    std::wstring decoded;
    if (!DecodeRarLegacyName(bytes, decoded) || decoded.size() >= targetCount)
    {
        target[0] = L'\0';
        return false;
    }

    memcpy(target, decoded.c_str(), (decoded.size() + 1) * sizeof(wchar_t));
    return true;
}

inline bool EncodeRarPasswordExact(const wchar_t* password, std::string& bytes)
{
    if (password == NULL)
    {
        std::string empty;
        bytes.swap(empty);
        return true;
    }
    std::string encoded;
    if (!Win32EncodeText(RAR_LEGACY_CODE_PAGE, password, wcslen(password), encoded).Succeeded())
    {
        bytes.clear();
        return false;
    }
    bytes.swap(encoded);
    return true;
}

inline std::string ProjectRarFileNameToAnsiExactOrPlaceholder(const wchar_t* wideName)
{
    if (wideName == NULL)
        return std::string();

    std::string out;
    if (!Win32EncodeText(RAR_LEGACY_CODE_PAGE, wideName, wcslen(wideName), out))
        return "?";
    return out;
}
