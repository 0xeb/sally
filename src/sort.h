// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CFilesArray;
struct CFileData;

enum CSortType
{
    stName,
    stExtension,
    stTime,
    stSize,
    stAttr
};

void SortFilesAndDirectories(CFilesArray* files, CFilesArray* dirs,
                             CSortType sortType, BOOL reverseSort, BOOL sortDirsByName);

void SortNameExt(CFilesArray& files, int left, int right, BOOL reverse);
void SortExtName(CFilesArray& files, int left, int right, BOOL reverse);
void SortTimeNameExt(CFilesArray& files, int left, int right, BOOL reverse);
void SortSizeNameExt(CFilesArray& files, int left, int right, BOOL reverse);
void SortAttrNameExt(CFilesArray& files, int left, int right, BOOL reverse);

typedef BOOL (*CLessFunction)(const CFileData&, const CFileData&, BOOL);

// comparison for two files, primary key name, secondary key extension, returns -1, 0, 1 like strcmp
int CmpNameExt(const CFileData& f1, const CFileData& f2);
int CmpNameExtIgnCase(const CFileData& f1, const CFileData& f2); // ignore-case variant

// WARNING: sort codes in RefreshDirectory, ChangeSortType and CompareDirectories must match!!!

BOOL LessNameExt(const CFileData& f1, const CFileData& f2, BOOL reverse);
BOOL LessNameExtIgnCase(const CFileData& f1, const CFileData& f2, BOOL reverse);
BOOL LessExtName(const CFileData& f1, const CFileData& f2, BOOL reverse);
BOOL LessTimeNameExt(const CFileData& f1, const CFileData& f2, BOOL reverse);
BOOL LessSizeNameExt(const CFileData& f1, const CFileData& f2, BOOL reverse);
BOOL LessAttrNameExt(const CFileData& f1, const CFileData& f2, BOOL reverse);

void IntSort(int array[], int left, int right);

// StrICmp for regional-settings-sort and detect-numbers-sort; in 'numericalyEqual' (if not
// NULL) returns TRUE when strings are numerically equal (e.g. "a01" and "a1")
// Each plain name below used to be declared with a signature IDENTICAL
// to its own ...W twin on the next line - lesson 42, four times over - while sort.cpp
// defined the plain names NARROW and the W twins wide. Every external caller passed wide
// and was binding to a declaration with no body; those were retargeted to the W twins.
// The plain narrow declarations themselves (and StrCmpLogicalEx, which only
// they called) are now DELETED rather than fixed - a full tree-wide caller trace (core and
// every plugin) found zero remaining callers of any of them after that retargeting, making
// them dead code, not a lagging definition. See sort.cpp for the full removal note.
int RegSetStrICmpW(const wchar_t* s1, const wchar_t* s2); // wide (defined in sort.cpp)
int RegSetStrICmpExW(const wchar_t* s1, int l1, const wchar_t* s2, int l2, BOOL* numericalyEqual); // wide; -1 = wcslen

// strcmp for regional-settings-sort and detect-numbers-sort; in 'numericalyEqual' (if not
// NULL) returns TRUE when strings are numerically equal (e.g. "a01" and "a1")
int RegSetStrCmpW(const wchar_t* s1, const wchar_t* s2); // wide (defined in sort.cpp)
int RegSetStrCmpExW(const wchar_t* s1, int l1, const wchar_t* s2, int l2, BOOL* numericalyEqual); // wide; -1 = wcslen
