// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#include "precomp.h"
#include "plugin_narrow_compat.h"
#include <new>
//#include <windows.h>
//#include <commctrl.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif // _MSC_VER

// suppress warning C4996: This function or variable may be unsafe. Consider using strcat_s instead.
// reason: lstrcat and other Windows routines simply aren't safe, so there's no point in addressing it here
#pragma warning(push)
#pragma warning(disable : 4996)

#ifdef __cplusplus
extern "C"
#endif
    LPSTR
    _sal_lstrcpyA(LPSTR lpString1, LPCSTR lpString2)
{
    return strcpy(lpString1, lpString2);
}

#ifdef __cplusplus
extern "C"
#endif
    LPWSTR
    _sal_lstrcpyW(LPWSTR lpString1, LPCWSTR lpString2)
{
    return wcscpy(lpString1, lpString2);
}

#ifdef __cplusplus
extern "C"
#endif
    LPSTR
    _sal_lstrcpynA(LPSTR lpString1, LPCSTR lpString2, int iMaxLength)
{
    if (iMaxLength <= 0)
        return lpString1;
    LPSTR ret = lpString1;
    LPSTR end = lpString1 + iMaxLength - 1;
    while (lpString1 < end && *lpString2 != 0)
        *lpString1++ = *lpString2++;
    *lpString1 = 0;
    return ret;
}

#ifdef __cplusplus
extern "C"
#endif
    LPWSTR
    _sal_lstrcpynW(LPWSTR lpString1, LPCWSTR lpString2, int iMaxLength)
{
    if (iMaxLength <= 0)
        return lpString1;
    LPWSTR ret = lpString1;
    LPWSTR end = lpString1 + iMaxLength - 1;
    while (lpString1 < end && *lpString2 != 0)
        *lpString1++ = *lpString2++;
    *lpString1 = 0;
    return ret;
}

#ifdef __cplusplus
extern "C"
#endif
    int
    _sal_lstrlenA(LPCSTR lpString)
{
    if (lpString == NULL)
        return 0;
    return (int)strlen(lpString);
}

#ifdef __cplusplus
extern "C"
#endif
    int
    _sal_lstrlenW(LPCWSTR lpString)
{
    if (lpString == NULL)
        return 0;
    return (int)wcslen(lpString);
}

#ifdef __cplusplus
extern "C"
#endif
    LPSTR
    _sal_lstrcatA(LPSTR lpString1, LPCSTR lpString2)
{
    return strcat(lpString1, lpString2);
}

#ifdef __cplusplus
extern "C"
#endif
    LPWSTR
    _sal_lstrcatW(LPWSTR lpString1, LPCWSTR lpString2)
{
    return wcscat(lpString1, lpString2);
}

#pragma warning(pop)

#if (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES) && !defined(INSIDE_SALAMANDER) && !defined(__BORLANDC__)

BOOL __CallStk_T = TRUE;

#endif // (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES) && !defined(INSIDE_SALAMANDER) && !defined(__BORLANDC__)

#if defined(TRACE_ENABLE) && !defined(INSIDE_SALAMANDER)

#include <ostream>
#include <streambuf>

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#include "spl_base.h"
#include "dbg.h"
#include "plugin_text_encoding.h"

#pragma warning(disable : 4074)
#pragma init_seg(compiler)

// See the comment on GetTrace()'s declaration in dbg.h. Function-local static, not
// a plain global - safer than relying on #pragma init_seg(compiler) above (which only orders
// this TU's own "compiler" segment against other segments, not against every other global
// object's constructor everywhere), and construction-on-first-use makes ordering moot entirely.
C__Trace& GetTrace()
{
    static C__Trace trace;
    return trace;
}

// See IsTraceAlive()'s declaration in dbg.h. Must flip before Disconnect-
// equivalent teardown (there isn't one here, just DeleteCriticalSection) so a caller checking
// IsTraceAlive() from another singleton's destructor never observes a half-torn-down object.
static bool s_TraceAlive = true;

bool IsTraceAlive()
{
    return s_TraceAlive;
}

// ****************************************************************************
//
// CWStr
//

CWStr::CWStr(const char* s)
{
    IsOK = TRUE;
    OwnsStr = FALSE;
    Str = NULL;
    if (s != NULL)
    {
        OwnsStr = TRUE;
        IsOK = sally::plugin_text::DecodeAcp(s, OwnedStr) ? TRUE : FALSE;
    }
}

//*****************************************************************************
//
// C__Trace
//

C__Trace::C__Trace() : TraceStrStream(&TraceStringBuf), TraceStrStreamW(&TraceStringBufW)
{
    InitializeCriticalSection(&CriticalSection);
}

C__Trace::~C__Trace()
{
    s_TraceAlive = false;
    DeleteCriticalSection(&CriticalSection);
}

C__Trace&
C__Trace::SetInfo(const char* file, int line)
{
    File = file;
    FileW = NULL;
    Line = line;
    return *this;
}

C__Trace&
C__Trace::SetInfoW(const WCHAR* file, int line)
{
    File = NULL;
    FileW = file;
    Line = line;
    return *this;
}

struct C__TraceMsgBoxThreadData
{
    const char* Msg;  // owned by SendMessageToServer until the thread joins
    const char* File; // just a reference to a static string
    int Line;
};

#ifdef __BORLANDC__
#define _countof(_Array) (sizeof(_Array) / sizeof(_Array[0]))
#endif // __BORLANDC__

DWORD WINAPI __TraceMsgBoxThread(void* param)
{
    C__TraceMsgBoxThreadData* data = (C__TraceMsgBoxThreadData*)param;
    try
    {
        std::wstring msg = L"TRACE_C message received!\n\nFile: ";
        msg += ToWideArg(data->File);
        msg += L"\nLine: ";
        msg += std::to_wstring(data->Line);
        msg += L"\n\nMessage: ";
        msg += ToWideArg(data->Msg);
        msg += L"\n\nTRACE_C message means that fatal error has occured. "
               L"Application will be crashed by \"access violation\" exception after "
               L"clicking OK. Please send us bug report to help us fix this problem. "
               L"If you want to copy this message to clipboard, use Ctrl+C key.";
        MessageBoxW(NULL, msg.c_str(), L"Debug Message",
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    catch (...)
    {
        MessageBoxW(NULL, L"Unable to allocate the fatal trace diagnostic.",
                    L"Debug Message", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    return 0;
}

struct C__TraceMsgBoxThreadDataW
{
    const WCHAR* Msg;  // owned by SendMessageToServer until the thread joins
    const WCHAR* File; // just a reference to a static string
    int Line;
};

DWORD WINAPI __TraceMsgBoxThreadW(void* param)
{
    C__TraceMsgBoxThreadDataW* data = (C__TraceMsgBoxThreadDataW*)param;
    try
    {
        std::wstring msg = L"TRACE_C message received!\n\nFile: ";
        msg += data->File != NULL ? data->File : L"";
        msg += L"\nLine: ";
        msg += std::to_wstring(data->Line);
        msg += L"\n\nMessage: ";
        if (data->Msg != NULL)
            msg += data->Msg;
        msg += L"\n\nTRACE_C message means that fatal error has occured. "
               L"Application will be crashed by \"access violation\" exception after "
               L"clicking OK. Please send us bug report to help us fix this problem. "
               L"If you want to copy this message to clipboard, use Ctrl+C key.";
        MessageBoxW(NULL, msg.c_str(), L"Debug Message",
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    catch (...)
    {
        MessageBoxW(NULL, L"Unable to allocate the fatal trace diagnostic.",
                    L"Debug Message", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    return 0;
}

void C__Trace::SendMessageToServer(BOOL information, BOOL unicode, BOOL crash)
{
    // flush to buffer
    if (unicode)
        TraceStrStreamW.flush();
    else
        TraceStrStream.flush();
    if (SalamanderDebug != NULL)
    {
        if (unicode)
        {
            if (information)
                SalamanderDebug->TraceI(FileW, Line, TraceStringBufW.c_str());
            else
                SalamanderDebug->TraceE(FileW, Line, TraceStringBufW.c_str());
        }
        else
        {
            const std::wstring fileW = ToWideArg(File);
            const std::wstring textW = ToWideArg(TraceStringBuf.c_str());
            if (information)
                SalamanderDebug->TraceI(File != NULL ? fileW.c_str() : NULL,
                                        Line, textW.c_str());
            else
                SalamanderDebug->TraceE(File != NULL ? fileW.c_str() : NULL,
                                        Line, textW.c_str());
        }
    }
    // only if crash==TRUE:
    // we make a copy of data, starting a thread for msgbox may trigger additional TRACE
    // messages (e.g., in DllMain reaction to DLL_THREAD_ATTACH), if we didn't leave
    // CriticalSection, a deadlock would occur;
    // TRACE_C must not be used in DllMain, otherwise a deadlock occurs:
    //   - if placed in DLL_THREAD_ATTACH: it tries to open a new thread for msgbox
    //     and that is blocked from DllMain
    //   - if placed in DLL_THREAD_DETACH: while waiting for the thread with msgbox
    //     of previous TRACE_C to close, we catch TRACE_C from DLL_THREAD_DETACH and leave it
    //     waiting in an infinite loop, see below
    // additionally, we introduce protection against multiplying msgboxes when multiple TRACE_C
    // occur simultaneously, it would only cause confusion, now msgbox opens only for the first
    // one and after closing it triggers a crash, other TRACE_C remain caught in infinite wait loop
    static BOOL msgBoxOpened = FALSE;
    C__TraceMsgBoxThreadData threadData;
    C__TraceMsgBoxThreadDataW threadDataW;
    std::string threadMessage;
    std::wstring threadMessageW;
    if (unicode)
        memset(&threadDataW, 0, sizeof(threadDataW));
    else
        memset(&threadData, 0, sizeof(threadData));
    if (crash) // break/crash after printing TRACE error message (TRACE_C and TRACE_MC)
    {
        if (!msgBoxOpened)
        {
            if (unicode)
            {
                try
                {
                    threadMessageW = TraceStringBufW.c_str();
                    threadDataW.Msg = threadMessageW.c_str();
                    threadDataW.File = FileW;
                    threadDataW.Line = Line;
                    msgBoxOpened = TRUE;
                }
                catch (const std::bad_alloc&)
                {
                    threadDataW.Msg = NULL;
                }
            }
            else
            {
                try
                {
                    threadMessage = TraceStringBuf.c_str();
                    threadData.Msg = threadMessage.c_str();
                    threadData.File = File;
                    threadData.Line = Line;
                    msgBoxOpened = TRUE;
                }
                catch (const std::bad_alloc&)
                {
                    threadData.Msg = NULL;
                }
            }
        }
        else
        {
            if (unicode)
                threadDataW.Msg = NULL;
            else
                threadData.Msg = NULL;
        }
    }
    if (unicode)
        TraceStringBufW.erase(); // preparation for next trace
    else
        TraceStringBuf.erase();
    LeaveCriticalSection(&CriticalSection);
    if (crash)
    {
        if (unicode && threadDataW.Msg != NULL || // break/crash after printing TRACE error message (TRACE_C and TRACE_MC)
            !unicode && threadData.Msg != NULL)
        {
            // we output the message in another thread so it doesn't pump messages of the current thread
            DWORD id;
            HANDLE msgBoxThread = CreateThread(NULL, 0, unicode ? __TraceMsgBoxThreadW : __TraceMsgBoxThread,
                                               unicode ? (void*)&threadDataW : (void*)&threadData, 0, &id);
            if (msgBoxThread != NULL)
            {
                WaitForSingleObject(msgBoxThread, INFINITE); // if TRACE_C is placed in DllMain in DLL_THREAD_ATTACH, a deadlock occurs - highly unlikely, we don't handle this
                CloseHandle(msgBoxThread);
            }
            msgBoxOpened = FALSE;
            // software crash is triggered directly in the code where TRACE_C/TRACE_MC is placed, so
            // it's visible in the bug report exactly where the macros are located; the crash therefore
            // follows after this method completes
        }
        else // we block other threads with TRACE_C, once the msgbox opened for
        {    // the first TRACE_C closes, it will crash there too, to avoid chaos
            if (msgBoxOpened)
            {
                while (1)
                    Sleep(1000); // blocking leads to deadlock e.g. when TRACE_C is (and shouldn't be) in DLL_THREAD_DETACH
            }
        }
    }
}

//*****************************************************************************

#ifdef _DEBUG
#undef memcpy
void* _sal_safe_memcpy(void* dest, const void* src, size_t count)
{
    if ((char*)dest + count > src && (char*)src + count > dest)
    {
        TRACE_C("_sal_safe_memcpy: source and destination of memcpy overlap!");
    }
    return memcpy(dest, src, count);
}
#endif // _DEBUG

#endif // defined(TRACE_ENABLE) && !defined(INSIDE_SALAMANDER)

// trap for custom definitions of these "forbidden" operators (for the check of
// forbidden combinations of WCHAR / char strings in TRACE macros to work, the following
// operators must not be defined in other modules - otherwise the linker wouldn't report an error - idea:
// in DEBUG version we catch linker errors, in RELEASE version we catch errors from custom operator
// definitions)
#if !defined(_DEBUG) && !defined(INSIDE_SALAMANDER) && !defined(__BORLANDC__)

#include <ostream>

std::ostream& operator<<(std::ostream& out, const wchar_t* str)
{
    return out << (void*)str;
}
std::wostream& operator<<(std::wostream& out, const char* str) { return out << (void*)str; }

#endif // _DEBUG
