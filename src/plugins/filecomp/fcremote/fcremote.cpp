// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "../remote_ipc_names.h"

HINSTANCE DLLInstance = NULL;

void my_memcpy(void* dst, const void* src, int len)
{
    char* d = (char*)dst;
    const char* s = (char*)src;
    while (len--)
        *d++ = *s++;
}

#pragma optimize("", off)
void my_zeromem(void* dst, int len)
{
    char* d = (char*)dst;
    while (len--)
        *d++ = 0;
}
#pragma optimize("", on)

const char*
Concatenate(const char* string1, const char* string2)
{
    static char buffer[5120];
    static int iterator = 0;

    int len1 = lstrlenA(string1);
    int len2 = lstrlenA(string2);

    if (len1 + len2 >= 5120)
        return "STRING TOO LONG";
    if (iterator + len1 + len2 >= 5120)
        iterator = 0;

    const char* ret = buffer + iterator;
    my_memcpy(buffer + iterator, string1, len1);
    iterator += len1;
    my_memcpy(buffer + iterator, string2, len2);
    iterator += len2;

    buffer[iterator++] = 0;
    return ret;
}

const wchar_t*
LangStr(int resID)
{
    const wchar_t* ret;
    switch (resID)
    {
    case IDS_SPLERROR:
        ret = L"File Comparator - Error";
        break;
    case IDS_INVALIDARGS:
        ret = L"Invalid arguments. Usage:\n\tfcremote.exe [options] first second\nOptions are:\n\t-w\tWait until File Comparator closes.\n\t--wait\tWait until File Comparator closes.";
        break;
    case IDS_MSGERR:
        ret = L"Cannot send message to File Comparator plugin.";
        break;
    case IDS_LAUNCHSAL:
        ret = L"Unable to launch Open Salamander.";
        break;
    case IDS_MSGERR2:
        ret = L"Cannot send message to File Comparator plugin. Ensure 'Load plugin on Open Salamander start' option is set in File Comparator configuration.";
        break;
    default:
        ret = L"ERROR LOADING STRING";
    }
    return ret;
}

inline int IsSpace(wchar_t c) { return c == L' ' || c == L'\t'; }

int RemoveQuotes(wchar_t* dest, const wchar_t* source, int len)
{
    int d = 0, s = 0;
    while (s < len)
    {
        if (source[s] == L'"')
            s++;
        else
            dest[d++] = source[s++];
    }
    return d;
}

BOOL MakeArgv(const wchar_t* commandLine, wchar_t argv[4][REMOTE_PATH_CAPACITY], int& argc,
              int maxlen, int maxarg)
{
    argc = 0;
    const wchar_t* start = commandLine;
    while (*start)
    {
        // trim the whitespace at the beginning
        while (*start && IsSpace(*start))
            start++;
        if (!*start || argc >= maxarg)
            break;
        const wchar_t* end = start;
        // find the end of the token
        while (*end && !IsSpace(*end))
        {
            if (*end++ == L'"')
            {
                while (*end && *end != L'"')
                    end++;
                if (end)
                    end++;
                else
                    end = start + lstrlenW(start);
            }
        }
        // add the token to the array
        int len = RemoveQuotes(argv[argc], start, min((int)(end - start), maxlen - 1));
        argv[argc++][len] = 0;
        start = end;
    }
    return *start == 0;
}

BOOL PathAppend(wchar_t* pPath, int pathCapacity, const wchar_t* pMore)
{
    if (pPath == NULL || pMore == NULL)
    {
        TRACE_E("pPath == NULL || pMore == NULL");
        return FALSE;
    }
    if (pMore[0] == 0)
    {
        TRACE_E("pMore[0] == 0");
        return TRUE;
    }
    int len = lstrlenW(pPath);
    const int moreLength = lstrlenW(pMore);
    if (len + moreLength + 2 > pathCapacity)
        return FALSE;
    // trim the trailing backslash before appending
    if (len > 1 && pPath[len - 1] != L'\\' && pMore[0] != L'\\')
    {
        pPath[len] = L'\\';
        len++;
    }
    lstrcpyW(pPath + len, pMore);
    return TRUE;
}

BOOL PathRemoveFileSpec(wchar_t* pszPath)
{
    if (pszPath == NULL)
    {
        TRACE_E("pszPath == NULL");
        return FALSE;
    }
    int len = lstrlenW(pszPath);
    wchar_t* iterator = pszPath + len - 1;
    while (iterator >= pszPath)
    {
        if (*iterator == L'\\')
        {
            if (iterator - 1 < pszPath || *(iterator - 1) == L':')
                iterator++;
            *iterator = 0;
            break;
        }
        iterator--;
    }
    return TRUE;
}

#ifndef ASFW_ANY
#define ASFW_ANY ((DWORD) - 1)
#endif

int RemoteCompareFiles(HINSTANCE hInstance, const wchar_t* commandLine)
{
    /*
  char spl[MAX_PATH];
  GetModuleFileNameA(hInstance, spl, MAX_PATH);
  PathRemoveFileSpec(spl); // remove fcremote.exe
  PathAppend(spl, "filecomp.spl");
  DLLInstance = LoadLibraryEx(spl, NULL, LOAD_LIBRARY_AS_DATAFILE);
*/
    typedef wchar_t TRemoteArg[REMOTE_PATH_CAPACITY];
    TRemoteArg* argv = new TRemoteArg[4];
    int argc;
    int first, second;
    BOOL wait = FALSE;

    // prepare argv
    BOOL argOK = MakeArgv(commandLine, argv, argc, REMOTE_PATH_CAPACITY, 4) &&
                 3 <= argc && argc <= 4;
    if (argOK)
    {
        if (argc == 3)
        {
            first = 1;
            second = 2;
        }
        else
        {
            if (lstrcmpW(argv[1], L"-w") == 0 || lstrcmpW(argv[1], L"--wait") == 0)
                wait = TRUE;
            else
                argOK = FALSE;
            first = 2;
            second = 3;
        }
    }

    if (!argOK)
    {
        MessageBoxW(NULL, LangStr(IDS_INVALIDARGS), LangStr(IDS_SPLERROR), MB_OK | MB_ICONERROR);
        if (DLLInstance)
            FreeLibrary(DLLInstance);
        delete[] argv;
        return -1;
    }

    HANDLE releaseEvent = NULL;
    BOOL firstTry = TRUE;
    BOOL ret = -1;
    while (1)
    {
        CMessageCenter mc(MessageCenterName, TRUE);
        if (!mc.IsGood())
        {
            if (firstTry)
            {
                // try to launch Salamander
                wchar_t* sal = new wchar_t[REMOTE_PATH_CAPACITY];
                if (GetModuleFileNameW(hInstance, sal, REMOTE_PATH_CAPACITY) == 0)
                {
                    delete[] sal;
                    break;
                }
                PathRemoveFileSpec(sal); // fcremote.exe
                PathRemoveFileSpec(sal); // filecomp
                PathRemoveFileSpec(sal); // plugins
                if (!PathAppend(sal, REMOTE_PATH_CAPACITY, L"sally.exe"))
                {
                    delete[] sal;
                    break;
                }

                STARTUPINFOW si;
                PROCESS_INFORMATION pi;
                my_zeromem(&si, sizeof(STARTUPINFO));
                si.cb = sizeof(STARTUPINFO);
                si.lpTitle = NULL;
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_SHOWNORMAL;
                const BOOL launched = CreateProcessW(sal, NULL, NULL, NULL, FALSE,
                                                     CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                                                     NULL, NULL, &si, &pi);
                delete[] sal;
                if (!launched)
                {
                    MessageBoxW(NULL, LangStr(IDS_LAUNCHSAL), LangStr(IDS_SPLERROR), MB_OK | MB_ICONERROR);
                    break;
                }
                char startedEventName[128];
                sally::filecomp::BuildFileCompStartedEventNameForCurrentProcess(startedEventName, (int)sizeof(startedEventName));
                HANDLE started =
                    CreateEventA(NULL, TRUE, FALSE, startedEventName);
                WaitForSingleObject(started, 5000);
                CloseHandle(started);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                firstTry = FALSE;
                continue; // try again with Salamander already running
            }
            MessageBoxW(NULL, LangStr(IDS_MSGERR2), LangStr(IDS_SPLERROR), MB_OK | MB_ICONERROR);
            break;
        }

        CRCMessage* msg = new CRCMessage;
        msg->Size = sizeof(*msg);
        lstrcpynW(msg->Path1, argv[first], REMOTE_PATH_CAPACITY);
        lstrcpynW(msg->Path2, argv[second], REMOTE_PATH_CAPACITY);
        msg->Path1[REMOTE_PATH_CAPACITY - 1] = L'\0';
        msg->Path2[REMOTE_PATH_CAPACITY - 1] = L'\0';
        GetCurrentDirectoryW(REMOTE_PATH_CAPACITY, msg->CurrentDirectory);

        if (wait)
        {
            sally::filecomp::BuildFileCompReleaseEventNameForCurrentProcess(
                msg->ReleaseEvent, (int)sizeof(msg->ReleaseEvent), GetCurrentProcessId());
            releaseEvent = CreateEventA(NULL, TRUE, FALSE, msg->ReleaseEvent);
        }
        else
            *msg->ReleaseEvent = 0;

        AllowSetForegroundWindow(ASFW_ANY);
        if (!mc.SendMessage(msg, 5000))
        {
            delete msg;
            MessageBoxW(NULL, LangStr(IDS_MSGERR), LangStr(IDS_SPLERROR), MB_OK | MB_ICONERROR);
            break;
        }
        delete msg;
        ret = 0;
        break;
    }
    if (DLLInstance)
        FreeLibrary(DLLInstance);
    if (releaseEvent)
    {
        WaitForSingleObject(releaseEvent, INFINITE);
        CloseHandle(releaseEvent);
    }
    delete[] argv;
    return ret;
}

// ****************************************************************************
// EnableExceptionsOn64
//

// We want to be notified about SEH exceptions even on x64 Windows 7 SP1 and newer
// http://blog.paulbetts.org/index.php/2010/07/20/the-case-of-the-disappearing-onload-exception-user-mode-callback-exceptions-in-x64/
// http://connect.microsoft.com/VisualStudio/feedback/details/550944/hardware-exceptions-on-x64-machines-are-silently-caught-in-wndproc-messages
// http://support.microsoft.com/kb/976038
void EnableExceptionsOn64()
{
    typedef BOOL(WINAPI * FSetProcessUserModeExceptionPolicy)(DWORD dwFlags);
    typedef BOOL(WINAPI * FGetProcessUserModeExceptionPolicy)(LPDWORD dwFlags);
    typedef BOOL(WINAPI * FIsWow64Process)(HANDLE, PBOOL);
#define PROCESS_CALLBACK_FILTER_ENABLED 0x1

    HINSTANCE hDLL = LoadLibraryW(L"KERNEL32.DLL");
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

// requires VC2008
void* __cdecl operator new(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

void __cdecl operator delete(void* ptr)
{
    HeapFree(GetProcessHeap(), 0, ptr);
}

void __cdecl operator delete(void* ptr, size_t)
{
    HeapFree(GetProcessHeap(), 0, ptr);
}

// requires VC2015
void* __cdecl operator new[](size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

void __cdecl operator delete[](void* ptr)
{
    HeapFree(GetProcessHeap(), 0, ptr);
}

void __cdecl operator delete[](void* ptr, size_t)
{
    HeapFree(GetProcessHeap(), 0, ptr);
}

void WinMainCRTStartup()
{
    EnableExceptionsOn64();
    // avoid critical errors such as "no disk in drive A:"
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS);

    int ret = RemoteCompareFiles(GetModuleHandle(NULL), GetCommandLineW());
    ExitProcess(ret);
}
