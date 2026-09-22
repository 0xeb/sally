// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// error codes
#define AE_SUCCESS 0
#define AE_OPEN 1
#define AE_ACCESS 2
#define AE_EOF 3
#define AE_BADARC 4
#define AE_BADDATA 5
#define AE_BADVERSION 6
#define AE_ENCRYPT 7
#define AE_METHOD 8
#define AE_UNKNTYPE 9
#define AE_CRC 10
#define AE_BADVOL 11

// operations for ProcessFile()
#define PFO_SKIP 0
#define PFO_EXTRACT 1

// modes for ARJChangeVolProc()
#define CVM_NOTIFY 0
#define CVM_ASK 1

// flags for ARJErrorProc()
#define EF_RETRY 0x01

// callbacks from the DLL
typedef BOOL(WINAPI* FARJChangeVolProc)(std::wstring& volName, const wchar_t* prevName, int mode);
typedef BOOL(WINAPI* FARJProcessDataProc)(const void* buffer, DWORD size);
typedef BOOL(WINAPI* FARJErrorProc)(int error, BOOL flags);
typedef void(WINAPI* FARJArchiveVolumeProc)(const wchar_t* volumeName);

struct CARJOpenData
{
    //input fields
    const wchar_t* ArcName;
    FARJChangeVolProc ARJChangeVolProc;
    FARJProcessDataProc ARJProcessDataProc;
    FARJErrorProc ARJErrorProc;
    FARJArchiveVolumeProc ARJArchiveVolumeProc;
};

// flags
#define FF_ENCRYPTED 0x01 // =garbled
#define FF_VOLUME 0x04
#define FF_EXTFILE 0x08
#define FF_PATHSYM 0x10
#define FF_BACKUP 0x20

// file type
#define FT_BINARY 0
#define FT_TEXT 1
#define FT_DIRECTORY 3
#define FT_VOLUMELABEL 4

struct CARJHeaderData
{
    BYTE ArcVer;
    BYTE ArcVerMin;
    BYTE HostOS;
    BYTE Flags;
    BYTE Method;
    BYTE FileType;
    FILETIME Time;
    DWORD Size;
    DWORD CompSize;
    DWORD Attr;
    std::wstring FileName;
};

BOOL WINAPI ARJOpenArchive(CARJOpenData* openData);
BOOL WINAPI ARJCloseArchive();
BOOL WINAPI ARJReadHeader(CARJHeaderData* headerData);
BOOL WINAPI ARJProcessFile(int operation, LPDWORD size = NULL);
