// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define STRING(code, string) code,

enum
{
#include "texts.h"
    STR_LAST_STRING
};

#undef STRING

extern const char* const StringTable[];

// exports from zip2sfx.cpp
extern std::wstring ZipName; // archive path
extern HANDLE ZipFile;
extern DWORD ArcSize;
extern DWORD EOCentrDirOffs;
extern BOOL Encrypt;

extern std::wstring ExeName;
extern std::wstring SfxPackageName;
extern HANDLE ExeFile;

extern HANDLE SfxPackage; // sfx package

extern CSfxSettings Settings; // sfx options
extern char About[SE_MAX_ABOUT];
extern CIcon* Icons;
extern int IconsCount;

extern char* IOBuffer;
extern __UINT32* CrcTab;

extern BOOL InflatingTexts;

BOOL Error(int error, ...);
BOOL ErrorPath(int error, const wchar_t* path);
void PrintWideText(const std::wstring& text);
BOOL Read(HANDLE file, void* buffer, DWORD size);
BOOL Write(HANDLE file, const void* buffer, DWORD size);

// exports from zip2sfx2.cpp
BOOL WriteSfxExecutable();
BOOL AppendArchive();
DWORD SalGetFileAttributes(const wchar_t* fileName);
