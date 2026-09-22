// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The following functions do not crash when working with invalid memory (not even when working with NULL):
// lstrcpy, lstrcpyn, lstrlen and lstrcat (they are defined with suffix A or W, therefore
// we do not redefine them directly), for the sake of easier bug debugging we need them to crash,
// because otherwise the error is discovered later in a place where it may not be clear what caused it
// to happen
#define lstrcpyA _sal_lstrcpyA
#define lstrcpyW _sal_lstrcpyW
#define lstrcpynA _sal_lstrcpynA
#define lstrcpynW _sal_lstrcpynW
#define lstrlenA _sal_lstrlenA
#define lstrlenW _sal_lstrlenW
#define lstrcatA _sal_lstrcatA
#define lstrcatW _sal_lstrcatW
#ifdef __cplusplus
extern "C"
{
#endif
    // The four A forms are declared NARROW here because that is what they are.
    // Their bodies live in plugins/shared/dbg.cpp as _sal_lstrcpyA(LPSTR, LPCSTR) etc., and
    // plugins/shared/spl_base.h:54-61 already declares them that way. An earlier pass widened this
    // copy to LPWSTR, which left two conflicting extern "C" declarations of the same four
    // symbols in one tree -- and because extern "C" gives them identical mangled names, the
    // linker cannot object: a translation unit that included THIS header would hand a wchar_t*
    // to a function that walks bytes, and link clean. The A/W split here is real (the W bodies
    // are separate functions in the same file), so the A halves stay narrow until the _UNICODE
    // flip retires the A macros above along with them.
    LPSTR _sal_lstrcpyA(LPSTR lpString1, LPCSTR lpString2);
    LPWSTR _sal_lstrcpyW(LPWSTR lpString1, LPCWSTR lpString2);
    LPSTR _sal_lstrcpynA(LPSTR lpString1, LPCSTR lpString2, int iMaxLength);
    LPWSTR _sal_lstrcpynW(LPWSTR lpString1, LPCWSTR lpString2, int iMaxLength);
    int _sal_lstrlenA(LPCSTR lpString);
    int _sal_lstrlenW(LPCWSTR lpString);
    LPSTR _sal_lstrcatA(LPSTR lpString1, LPCSTR lpString2);
    LPWSTR _sal_lstrcatW(LPWSTR lpString1, LPCWSTR lpString2);
#ifdef __cplusplus
}
#endif
