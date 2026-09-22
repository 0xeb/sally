// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <windows.h>

#include "lstrfix.h"

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

/*
SALSPAWN errorcodes:
  err -> External prg err
  retBase -> bad options
  retBase + 1 -> no executable
  retBase * 2 + err -> CreateProcess err
  retBase * 3 + err -> WaitForSingleObject err
  retBase * 4 + err -> GetExitCode err
*/

BOOL CtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
    // ignore CTRL+C, Ctrl+Break, and other good reasons to terminate... because we must
    // first terminate the externally started archiver (otherwise it keeps running
    // even when Salamander says compression/decompression has finished)
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        return TRUE;

    default:
        return FALSE;
    }
}

// ****************************************************************************
// EnableExceptionsOn64
//

// We want to learn about SEH Exceptions even on x64 Windows 7 SP1 and later
// http://blog.paulbetts.org/index.php/2010/07/20/the-case-of-the-disappearing-onload-exception-user-mode-callback-exceptions-in-x64/
// http://connect.microsoft.com/VisualStudio/feedback/details/550944/hardware-exceptions-on-x64-machines-are-silently-caught-in-wndproc-messages
// http://support.microsoft.com/kb/976038
void EnableExceptionsOn64()
{
    typedef BOOL(WINAPI * FSetProcessUserModeExceptionPolicy)(DWORD dwFlags);
    typedef BOOL(WINAPI * FGetProcessUserModeExceptionPolicy)(LPDWORD dwFlags);
    typedef BOOL(WINAPI * FIsWow64Process)(HANDLE, PBOOL);
#define PROCESS_CALLBACK_FILTER_ENABLED 0x1

    HINSTANCE hDLL = LoadLibraryA("KERNEL32.DLL");
    if (hDLL != NULL)
    {
        FIsWow64Process isWow64 = (FIsWow64Process)GetProcAddress(hDLL, "IsWow64Process");                                                      // Min: XP SP2
        FSetProcessUserModeExceptionPolicy set = (FSetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "SetProcessUserModeExceptionPolicy"); // Min: Vista with hotfix
        FGetProcessUserModeExceptionPolicy get = (FGetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "GetProcessUserModeExceptionPolicy"); // Min: Vista with hotfix
        if (isWow64 != NULL && set != NULL && get != NULL)
        {
            BOOL bIsWow64;
            if (isWow64(GetCurrentProcess(), &bIsWow64) && bIsWow64)
            {
                DWORD dwFlags;
                if (get(&dwFlags))
                    set(dwFlags & ~PROCESS_CALLBACK_FILTER_ENABLED);
            }
        }
        FreeLibrary(hDLL);
    }
}

void mainCRTStartup()
{
    EnableExceptionsOn64();
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE);
    BOOL help = FALSE;
    BOOL error = FALSE;
    int retBase = 10000;
    // Wide. salspawn launches whatever command line Salamander hands it, so a
    // non-ANSI path in that line was being flattened through CP_ACP before CreateProcess ever
    // saw it - the one satellite still doing that (salmon went wide already, and salopen was retired).
    //
    // This binary is built with NO CRT (see CMakeLists.txt "minimal, no CRT" and the
    // mainCRTStartup entry point above), so everything below stays raw wchar_t pointer walking
    // and Win32 - no wcslen, no wcscpy.
    wchar_t* exeName = NULL;
    const wchar_t* cmdline;
    DWORD exitCode;

    // we don't want any critical errors like "no disk in drive A:"
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS);

    cmdline = GetCommandLineW();
    // skip leading spaces
    while (*cmdline == L' ' || *cmdline == L'\t')
        cmdline++;
    // skip exe name
    if (*cmdline == L'"')
    {
        cmdline++;
        while (*cmdline != L'\0' && *cmdline != L'"')
            cmdline++;
        if (*cmdline == L'"')
            cmdline++;
    }
    else
        while (*cmdline != L'\0' && *cmdline != L' ' && *cmdline != L'\t')
            cmdline++;
    // get params
    while (1)
    {
        // skip spaces
        while (*cmdline == L' ' || *cmdline == L'\t')
            cmdline++;
        if (*cmdline == L'\0')
            break;
        // is it a switch ?
        if (*cmdline == L'-' || *cmdline == L'/')
        {
            cmdline += 2;
            switch (*(cmdline - 1))
            {
            case L'?':
            case L'h':
            case L'H':
                help = TRUE;
                break;
            case L'c':
                if (*cmdline > L'9' || *cmdline < L'0')
                {
                    help = TRUE;
                    break;
                }
                retBase = 0;
                while (*cmdline <= L'9' && *cmdline >= L'0')
                    retBase = retBase * 10 + *cmdline++ - L'0';
                if (*cmdline != L' ' && *cmdline != L'\t' && *cmdline != L'\0')
                {
                    help = TRUE;
                    break;
                }
                break;
            default:
                ExitProcess(retBase);
            }
        }
        // if not, it must be a line to execute
        else
        {
            const wchar_t* commandStart = cmdline;
            SIZE_T commandLength = 0;
            while (*cmdline != L'\0')
            {
                commandLength++;
                cmdline++;
            }
            if (commandLength >= MAXINT ||
                commandLength > (((SIZE_T)-1) / sizeof(wchar_t)) - 1)
                ExitProcess(ERROR_NOT_ENOUGH_MEMORY + retBase * 2);
            exeName = (wchar_t*)HeapAlloc(GetProcessHeap(), 0,
                                          (commandLength + 1) * sizeof(wchar_t));
            if (exeName == NULL)
                ExitProcess(ERROR_NOT_ENOUGH_MEMORY + retBase * 2);
            lstrcpynW(exeName, commandStart, (int)commandLength + 1);
        }
    }

    if (exeName == NULL || exeName[0] == L'\0' || help)
    {
        // The banner stays a narrow WriteFile on purpose: this is raw BYTE output to a console
        // handle, not text handed to a Win32 text API, and every character in it is ASCII by
        // construction. Widening it would mean emitting UTF-16 bytes to a byte stream.
        DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
                  "SALSPAWN: Spawn for Open Salamander, Copyright (C) 1998-2023 Open Salamander Authors\n\nUsage: salspawn [-|/<switch>] <executable> [exe params]\n\nAvailable switches:\n  ?,h,H - this help screen\n  c<num> - sets base of SALSPAWN error level to <num>\n\n",
                  221, &written, NULL);
        ExitProcess(retBase + 1);
    }

    PROCESS_INFORMATION pi;
    STARTUPINFOW si;
    si.cb = sizeof(si);
    si.lpReserved = NULL;
    si.lpTitle = NULL;
    si.lpDesktop = NULL;
    si.cbReserved2 = 0;
    si.lpReserved2 = 0;
    si.dwFlags = 0;
    // CreateProcessW's second parameter is in/out and must be writable, which is why the command
    // line was copied into dynamically allocated storage above.
    if (!CreateProcessW(NULL, exeName, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP,
                        NULL, NULL, &si, &pi))
    {
        DWORD createError = GetLastError();
        HeapFree(GetProcessHeap(), 0, exeName);
        ExitProcess(createError + retBase * 2);
    }
    HeapFree(GetProcessHeap(), 0, exeName);

    if (WaitForSingleObject(pi.hProcess, INFINITE) == WAIT_FAILED)
    {
        exitCode = GetLastError() + retBase * 3;
        goto EXIT1;
    }

    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        exitCode = GetLastError() + retBase * 4;
    }
EXIT1:
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    ExitProcess(exitCode);
}
