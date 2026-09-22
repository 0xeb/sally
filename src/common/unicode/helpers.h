// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <windows.h>

namespace sally::unicode
{
// In-place case conversion for caller-owned wide buffers. CharLowerW/CharUpperW
// operate on the complete NUL-terminated string and retain its storage, which is
// the live analogue of General's historical byte-table mutation contract.
inline void LowerCaseInPlaceW(wchar_t* text)
{
    if (text != nullptr)
        CharLowerW(text);
}

inline void UpperCaseInPlaceW(wchar_t* text)
{
    if (text != nullptr)
        CharUpperW(text);
}

// Panel directory-read contract: a CFileData row keeps a wide NameW (rather
// than NULL) exactly when the ANSI Name cannot faithfully stand in for the wide
// name — i.e. when the CP_ACP conversion is lossy OR the name has any non-ASCII
// codepoint. The non-ASCII arm is a strict superset of the lossy set: it guards
// downstream decoding under a possibly-different CP_ACP (e.g. a Korean name
// that round-trips under CP_ACP=949 still needs the original wide form). Pure
// decision, no allocation — the single authority for when a panel row is wide.
// Single-character case fold for wide matching/comparison walks.
//
// The exact wide analogue of the narrow `LowerCase[]` table, which is itself
// built from CharLower under the active code page - so semantics carry over
// rather than being re-invented.
//
// NOT towlower(): under MSVC's default C locale that folds ASCII only, which
// would silently reproduce the very code-page limitation these ports remove.
// A wide matcher built on it compiles, passes every ASCII test, and still fails
// on the non-ANSI names it exists to support.
//
// For whole-STRING comparison prefer sally::text::CompareFolded; this exists for
// character-by-character walks (mask matching, common-prefix scanning) where a
// string-level fold does not fit.
inline wchar_t FoldCharW(wchar_t c)
{
    return (wchar_t)(UINT_PTR)CharLowerW((LPWSTR)(UINT_PTR)(WORD)c);
}

inline bool PanelNameNeedsWideName(const wchar_t* wideName, bool ansiConversionLossy)
{
    if (ansiConversionLossy)
        return true;
    if (wideName == nullptr)
        return false;
    for (const wchar_t* wp = wideName; *wp != L'\0'; ++wp)
        if (static_cast<unsigned>(*wp) > 0x7f)
            return true;
    return false;
}
} // namespace sally::unicode

// Trim spaces from the beginning and spaces/dots from the end of a filename
// component, matching Explorer's manual-create behavior.
inline BOOL MakeValidFileNameComponentW(wchar_t* path)
{
    if (path == NULL)
        return FALSE;

    BOOL changed = FALSE;
    wchar_t* n = path;
    while (*n != 0 && *n <= L' ')
        n++;
    if (n > path)
    {
        memmove(path, n, (wcslen(n) + 1) * sizeof(wchar_t));
        changed = TRUE;
    }

    n = path + wcslen(path);
    while (n > path && (*(n - 1) <= L' ' || *(n - 1) == L'.'))
        n--;
    if (*n != 0)
    {
        *n = 0;
        changed = TRUE;
    }

    return changed;
}

// Format a wide string using printf-style formatting, returns std::wstring
template <typename... Args>
inline std::wstring FormatStrW(const wchar_t* format, Args... args)
{
    int len = _scwprintf(format, args...);
    if (len <= 0)
        return std::wstring();
    std::wstring out(static_cast<size_t>(len) + 1, L'\0');
    if (swprintf_s(out.data(), out.size(), format, args...) != len)
        return std::wstring();
    out.resize(static_cast<size_t>(len));
    return out;
}

template <typename... Args>
inline std::string FormatStrA(const char* format, Args... args)
{
    int len = _scprintf(format, args...);
    if (len <= 0)
        return std::string();
    std::string out(static_cast<size_t>(len) + 1, '\0');
    if (sprintf_s(out.data(), out.size(), format, args...) != len)
        return std::string();
    out.resize(static_cast<size_t>(len));
    return out;
}
