// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// Loads one shell icon-overlay's icon at every size Sally needs.
//
// Why this is its own function: a handler is dropped outright unless all three sizes
// load, so how we ask for them decides whether the overlay ever appears. The original
// code asked for two icons in a single ExtractIcons call, packing two sizes into the
// cx/cy arguments with MAKELONG and passing nIcons = 2. That form requests two
// consecutive icon *resources* starting at iconIndex and only works for a DLL or EXE
// holding several of them. A standalone .ico file contains exactly one icon, so the
// second size came back NULL and the handler was discarded - which is issue #90:
// TortoiseOverlays hands us ...\icons\<set>\NormalIcon.ico, every size is present in
// that file, and Sally threw it away anyway.
//
// Asking for one icon at a time also lets Windows scale from the nearest stored image,
// which matters because the sizes Sally requires are DPI-scaled: at 200% they are
// 32/64/96 while a typical overlay .ico stores only 16/32/48.
//
// 'sizes' and 'icons' are both ICONSIZE_COUNT long (16/32/48 slots, in that order).
// Every slot is written: a size that could not be loaded is set to NULL, so the caller
// can tell exactly which one failed rather than just that something did. Returns the
// number of sizes successfully loaded.
//
// This calls SHDefExtractIcon directly rather than going through Sally's own
// ExtractIcons() wrapper in geticon.cpp. That wrapper exists to emulate the old
// two-icons-in-one-call shape, which is precisely what we no longer want; it also
// silently discards its 'flags' argument, so nothing is lost by bypassing it.
//
// No COM and no Salamander state, so this is testable headlessly.
//
// 'extract' exists purely so the tests can observe HOW we call the shell rather than
// what the shell happens to return: whether a real .ico loses a size under the packed
// form depends on the icon's contents and the machine's DPI, so asserting on Windows'
// output cannot reliably guard the contract. A test passes a fake and asserts we make
// exactly one call per size, each with an unpacked size. Production passes NULL, which
// means SHDefExtractIconA.
typedef HRESULT(STDAPICALLTYPE* ShellIconExtractFn)(PCSTR pszIconFile, int iIndex, UINT uFlags,
                                                    HICON* phiconLarge, HICON* phiconSmall,
                                                    UINT nIconSize);

int LoadShellOverlayIcons(const char* iconFile, int iconIndex, const int* sizes, int sizeCount,
                          HICON* icons, ShellIconExtractFn extract = NULL);

// wide: IShellIconOverlayIdentifier::GetOverlayInfo hands back the icon file path
// wide (COM-only API, no ANSI form) - a handler installed under a path the active code page
// cannot spell used to get '?'-mangled by narrowing it just to call the narrow loader above.
// Same contract as the narrow form, calling SHDefExtractIconW by default.
typedef HRESULT(STDAPICALLTYPE* ShellIconExtractFnW)(PCWSTR pszIconFile, int iIndex, UINT uFlags,
                                                     HICON* phiconLarge, HICON* phiconSmall,
                                                     UINT nIconSize);

int LoadShellOverlayIconsW(const wchar_t* iconFile, int iconIndex, const int* sizes, int sizeCount,
                           HICON* icons, ShellIconExtractFnW extract = NULL);
