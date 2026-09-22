// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <string>

// Pasting a path that names a FILE must list the file's directory and focus the file -
// not try to list the file itself.
//
// CFilesWindow::ChangeDir() has always done this for ANSI paths
// (files_window_directory_read.cpp: `if (*end == 0 && CutDirectory(shortenedPath, &name))`
// -> ChangePathToDisk(..., name) + CHPPFR_FILENAMEFOCUSED). ClipboardPastePath() routes a
// non-ANSI path to ChangePathToDisk() instead, which has no such split, so the full file
// path reached the directory-listing code and produced
//   "(267) The directory name is invalid."
// Confirmed from a live dump: ClipboardPastePath -> ChangePathToDisk -> ShowError.
//
// These are the pure decisions behind that split, kept UI-free and FS-free so the headless
// test can drive them directly. The caller supplies the attributes it already queried.

namespace sally
{

// The panel's ANSI spelling of a wide filename.
//
// MUST stay byte-identical to how CFilesWindow::ReadDirectory builds CFileData::Name
// (files_window_directory_read.cpp: WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, ...)),
// because CommonRefresh() focuses a row by comparing suggestedFocusName against that very
// Name with StrICmp/strcmp. Same conversion in, same bytes out, so focus matches without
// needing a wide focus channel.
//
// A lossy name keeps its '?' substitutions, exactly as the panel stores them. Two different
// wide names can therefore share one ANSI spelling; focus then lands on the first match,
// which is a cosmetic ambiguity rather than an error. Returns "" when conversion fails
// entirely (the panel stores an empty Name in that case too).


// TRUE when 'fullPath' (whose attributes are 'attrs') names an existing non-directory and can
// be split, i.e. the paste should list 'directoryOut' and focus 'focusNameOut'.
//
// FALSE - caller keeps its current behaviour - when the path does not exist, is a directory,
// or has no parent to fall back to ("C:\", "\server\share").
bool ResolvePastedFilePathW(const wchar_t* fullPath, DWORD attrs,
                            std::wstring& directoryOut, std::wstring& focusNameOut);

} // namespace sally
