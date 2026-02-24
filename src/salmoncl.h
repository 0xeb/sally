// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

// SalmonClient
// SALMON.EXE module is used for out-of-process minidump generation, packaging, and upload to the server
// SALMON must run from Salamander startup to handle crashes. Crashes before SALMON starts
// happen silently and SALMON processes them "next time"
//
// this header is shared between SALMON and SALAMAND projects because of the memory they use to communicate
//
// out of process minidumps
// http://www.nynaeve.net/?p=128
// http://social.msdn.microsoft.com/Forums/en-US/windbg/thread/2dfd711f-e81e-466f-a566-4605e78075f6
//
// http://www.voyce.com/index.php/2008/06/11/creating-a-featherweight-debugger/
// http://social.msdn.microsoft.com/Forums/en-US/windbg/thread/2dfd711f-e81e-466f-a566-4605e78075f6
// http://social.msdn.microsoft.com/Forums/en-US/vsdebug/thread/b290b7bd-1ec8-4302-8e3a-8ee0dc134683/
// http://www.ms-news.info/f3682/minidumpwritedump-fails-after-writing-partial-dump-access-denied-1843614.html
//
// debugging handles
// http://www.codeproject.com/Articles/6988/Debug-Tutorial-Part-5-Handle-Leaks

#define SALMON_FILEMAPPIN_NAME_SIZE 20

// x64 and x86 versions of Salamander/Salmon are not compatible
#ifdef _WIN64
#define SALMON_SHARED_MEMORY_VERSION_PLATFORM 0x10000000
#else
#define SALMON_SHARED_MEMORY_VERSION_PLATFORM 0x00000000
#endif
#define SALMON_SHARED_MEMORY_VERSION (SALMON_SHARED_MEMORY_VERSION_PLATFORM | 4)

#pragma pack(push)
#pragma pack(4)
struct CSalmonSharedMemory
{
    DWORD Version;           // SALMON_SHARED_MEMORY_VERSION (if it does not match for SALAM/SALMON, fail and do not communicate...)
    HANDLE Process;          // handle of the parent process (so we can wait for its termination); we let this handle leak
    DWORD ProcessId;         // ID of the crashed parent process
    DWORD ThreadId;          // ID of the crashed thread
    HANDLE Fire;             // AS signals SALMON to send reports
    HANDLE Done;             // SALMON signals back to AS that it is done
    HANDLE SetSLG;           // AS signals SALMON to load SLG based on SLGName buffer, which it sets before signaling the event
    HANDLE CheckBugs;        // AS signals SALMON to check the bug report directory and if it finds any (from a previous crash), offer upload
    char SLGName[MAX_PATH];  // meaningful when AS signals SetSLG and says which SLG should be loaded
    char BugPath[MAX_PATH];  // set by Salamander, path where bug reports will be written (path may not exist, created only on crash)
    char BugName[MAX_PATH];  // set by Salamander, internal name of the minidump/bug report file
    char BaseName[MAX_PATH]; // set by Salmon, composed as "UID-BugName-DATE-TIME"; for a minidump it appends ".DMP"
    DWORD64 UID;             // unique machine ID, created by XORing GUIDs; stored in registry under Bug Reporter key; set by Salamander, Salmon only reads and inserts into bug report name

    // passing EXCEPTION_POINTERS by its parts; set before signaling the Fire event
    EXCEPTION_RECORD ExceptionRecord;
    CONTEXT ContextRecord;
};
#pragma pack(pop)

//-----------------------------------------------------------------------
#ifdef INSIDE_SALAMANDER

BOOL SalmonInit();
void SalmonSetSLG(const char* slgName); // sets language in salmon
void SalmonCheckBugs();

// store exception info in shared memory and ask Salmon to create a minidump; then wait for it to finish
// returns TRUE on success, FALSE if Salmon could not be called for some reason
BOOL SalmonFireAndWait(const EXCEPTION_POINTERS* e, char* bugReportPath);

#endif //INSIDE_SALAMANDER
