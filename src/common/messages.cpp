// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <windows.h>
#include <crtdbg.h>

#define __MODUL_MESSAGES_CPP

#ifndef MESSAGES_DISABLE

// The order here is important.
// Section names must be 8 characters or less.
// The sections with the same name before the $
// are merged into one section. The order that
// they are merged is determined by sorting
// the characters after the $.
// i_messages and i_messages_end are used to set
// boundaries so we can find the real functions
// that we need to call for initialization.

#pragma warning(disable : 4075) // we want to define module initialization order

typedef void(__cdecl* _PVFV)(void);

#pragma section(".i_msg$a", read)
__declspec(allocate(".i_msg$a")) const _PVFV i_messages = (_PVFV)1; // at the beginning of section .i_msg we place the variable i_messages

#pragma section(".i_msg$z", read)
__declspec(allocate(".i_msg$z")) const _PVFV i_messages_end = (_PVFV)1; // and at the end of section .i_msg we place the variable i_messages_end

void Initialize__Messages()
{
    const _PVFV* x = &i_messages;
    for (++x; x < &i_messages_end; ++x)
        if (*x != NULL)
            (*x)();
}

#pragma init_seg(".i_msg$m")

#include <ostream>
#include <new>
#include <stdio.h>
#include <string>
#ifdef _DEBUG
#include <sstream>
#endif // _DEBUG

#ifndef TRACE_ENABLE
#define __MESSAGES_STR2(x) #x
#define __MESSAGES_STR(x) __MESSAGES_STR2(x)
//#pragma message(__FILE__ "(" __MESSAGES_STR(__LINE__) "): Warning: macro TRACE_ENABLE not defined - unable to show errors")
#endif // TRACE_ENABLE

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "trace.h"
#include "messages.h"
#include "DiagnosticTextEncoding.h"

const char* __MessagesTitle = "Message";
const WCHAR* __MessagesTitleW = L"Message";
HWND __MessagesParent = NULL;

#ifdef MULTITHREADED_MESSAGES_ENABLE

// critical section for the entire module - monitor
CRITICAL_SECTION __MessagesCriticalSection;
// handle of the current owning thread
DWORD __MessagesOwnerThreadID = 0;
// number of nested calls to EnterMessagesModul (within the current owning thread)
int __MessagesModulBlockCount = 0;

#ifdef MESSAGES_DEBUG

// call from a thread that has no access to the module's data and functions (did not allocate)
const char* __MessagesBadCall = "Incorrect call to function from modul MESSAGES.";

#endif // MESSAGES_DEBUG

#endif // MULTITHREADED_MESSAGES_ENABLE

C__Messages __Messages;
C__MessagesW __MessagesW;

#ifdef MULTITHREADED_MESSAGES_ENABLE

const char* __MessagesLowMemory = "Insufficient memory.";
const WCHAR* __MessagesLowMemoryW = L"Insufficient memory.";

//*****************************************************************************
//
// EnterMessagesModul
//

void EnterMessagesModul()
{
    EnterCriticalSection(&__MessagesCriticalSection);
    __MessagesOwnerThreadID = GetCurrentThreadId();
    __MessagesModulBlockCount++;
}

//*****************************************************************************
//
// LeaveMessagesModul
//

void LeaveMessagesModul()
{
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
        if (--__MessagesModulBlockCount == 0)
            __MessagesOwnerThreadID = 0;
        LeaveCriticalSection(&__MessagesCriticalSection);
    }
}

#endif // MULTITHREADED_MESSAGES_ENABLE

//*****************************************************************************
//
// C__Messages
//

C__Messages::C__Messages() : MessagesStrStream(&MessagesStringBuf)
{
#ifdef _DEBUG
    // new streams use internal locales which have implemented
    // individual "facets" using lazy creation - they are allocated on heap
    // when needed, i.e. when someone sends something to the stream that has
    // formatting dependent on locale rules, e.g. number, date,
    // or boolean. These "facets" are then deallocated on exit
    // of the program with compiler priority, i.e. after our memory leak check.
    // So if someone uses stream to output something localizable,
    // our debug heap will report memory leaks, even though there are none. To prevent
    // this, we force locales to create all "facets" now, while
    // we're not yet monitoring the heap.
    // For now we use only output stream and only with strings (without conversion)
    // and numbers. So sending a number to stringstream should suffice. If
    // in the future we start using streams more and debug heap starts reporting
    // leaks, we will need to add more input/output here.
    std::stringstream s;
    s << 1;
#endif // _DEBUG
#ifdef MULTITHREADED_MESSAGES_ENABLE
    InitializeCriticalSection(&__MessagesCriticalSection);
}

C__Messages::~C__Messages()
{
    DeleteCriticalSection(&__MessagesCriticalSection);
#endif // MULTITHREADED_MESSAGES_ENABLE
}

// The ONE choke point for every message box this module raises.
//
// In the shipping app all windows are raised through ::MessageBoxW. Narrow
// diagnostic streams are decoded by __MessagesShowA immediately before this boundary.
//
// Under AUTOMATION they must NOT block: nothing is watching to click OK, so the
// run hangs FOREVER - no crash, no timeout, no output, just a process that never
// exits. That is not hypothetical; it cost two full diagnosis cycles on
// CFindDialog (a failed LoadIcon in WM_INITDIALOG, and the
// HANDLES() atexit leak report), each found only by attaching cdb to the stuck
// process. Any future test touching a HANDLES()-wrapped call could hit it again.
//
// Suppressed means printed to stderr - the diagnostic is still SEEN, which is the
// whole point of these boxes - and answered with the least-escalating option, i.e.
// the one that declines whatever extra action was offered. For the leak report's
// "list opened handles to Trace Server?" (MB_YESNO) that is IDNO.
//
// TWO triggers, because there are two kinds of automated process:
//
//  1. COMPILE-TIME (SALLY_NO_INTERACTIVE_DIALOGS, set tree-wide by
//     tests/CMakeLists.txt; or SALLY_E2E_HOST). Covers every test binary.
//
// Scope note: this whole file is inside `#ifndef MESSAGES_DISABLE`, and Release
// defines MESSAGES_DISABLE while HANDLES_ENABLE is Debug-only (cmake/sal_common.cmake).
// So a Release sally.exe raises none of these boxes in the first place - everything
// below concerns DEBUG binaries, which is exactly where the automation runs.
//
//  2. RUNTIME (the SALLY_NONINTERACTIVE env var). Covers what the define cannot:
//     a real sally.exe SPAWNED BY a test. That child is the shipping binary and
//     must keep its dialogs when a human runs it, so the define is wrong there -
//     but when an e2e test launches it, its exit-time leak box has no one to
//     dismiss it and blocks the parent. The env var is inherited by children, so
//     setting it once for a ctest run covers the whole process tree.
//
// Checked once and cached: this can be called from a static destructor, after
// other statics are gone, so it must not depend on any non-trivial global.
bool __MessagesIsNonInteractive()
{
#if defined(SALLY_E2E_HOST) || defined(SALLY_NO_INTERACTIVE_DIALOGS)
    return true;
#else
    static const bool suppressed = (::GetEnvironmentVariableA("SALLY_NONINTERACTIVE", NULL, 0) != 0);
    return suppressed;
#endif
}

#if defined(SALLY_E2E_HOST) || defined(SALLY_NO_INTERACTIVE_DIALOGS)
// A test binary propagates its own non-interactivity to every process it starts.
// Several tests CreateProcess() a real sally.exe (gtest_issue75_f2_escape_ui_e2e,
// gtest_external_tool_runner, ...); that child is the SHIPPING binary, so it has
// neither define and would raise a real modal box that nothing dismisses - the
// parent then waits on a child that is waiting on a human. Setting the env var
// here means children inherit the suppression automatically, with no per-test
// wiring to remember. Shipping sally.exe never runs this code.
static const struct __MessagesPropagateNonInteractive
{
    __MessagesPropagateNonInteractive()
    {
        ::SetEnvironmentVariableA("SALLY_NONINTERACTIVE", "1");
    }
} __messagesPropagateNonInteractive;
#endif

static int __MessagesNonInteractiveAnswer(UINT uType)
{
    switch (uType & MB_TYPEMASK)
    {
    case MB_OKCANCEL:
    case MB_YESNOCANCEL:
    case MB_RETRYCANCEL:
        return IDCANCEL;
    case MB_ABORTRETRYIGNORE:
        return IDIGNORE;
    case MB_YESNO:
        return IDNO;
    case MB_CANCELTRYCONTINUE:
        return IDCONTINUE;
    default: // MB_OK
        return IDOK;
    }
}

int __MessagesShowW(HWND hWnd, const WCHAR* text, const WCHAR* caption, UINT uType)
{
    if (!__MessagesIsNonInteractive())
        return ::MessageBoxW(hWnd, text, caption, uType);

    fwprintf(stderr, L"\n[sally messagebox suppressed - non-interactive] %s: %s\n",
             caption != NULL ? caption : L"(no caption)", text != NULL ? text : L"(no text)");
    fflush(stderr);
    return __MessagesNonInteractiveAnswer(uType);
}

static bool DecodeLegacyDiagnosticText(const char* bytes,
                                       std::wstring& text) noexcept
{
    if (bytes == NULL)
    {
        text.clear();
        return true;
    }
    return sally::diagnostic::DecodeAcp(bytes, text);
}

static bool EncodeLegacyDiagnosticText(const std::wstring& text,
                                       std::string& bytes) noexcept
{
    return sally::diagnostic::EncodeAcpExact(text, bytes);
}

// Compatibility adapter for narrow diagnostic streams. The UI boundary itself
// is UTF-16; legacy debug bytes are decoded once here and never become window
// ownership. If allocation fails, preserve the diagnostic path with static text.
int __MessagesShowA(HWND hWnd, const char* text, const char* caption, UINT uType)
{
    try
    {
        std::wstring textW;
        std::wstring captionW;
        const bool textDecoded = DecodeLegacyDiagnosticText(text, textW);
        const bool captionDecoded = DecodeLegacyDiagnosticText(caption, captionW);
        return __MessagesShowW(hWnd,
                               text == NULL ? NULL :
                                   (textDecoded ? textW.c_str() : L"Invalid legacy diagnostic text."),
                               caption == NULL ? NULL :
                                   (captionDecoded ? captionW.c_str() : L"Sally"),
                               uType);
    }
    catch (const std::bad_alloc&)
    {
        return __MessagesShowW(hWnd, L"Insufficient memory while preparing a diagnostic.",
                               L"Sally", uType);
    }
}

struct C__MessageBoxData
{
    const char* Text;
    const char* Caption;
    UINT Type;
    int Return;
};

int CALLBACK __MessagesMessageBoxThreadF(C__MessageBoxData* data)
{ // must not wait for a response from the calling thread because it will not respond
    // therefore parent==NULL -> no window disabling etc.
    data->Return = __MessagesShowA(NULL, data->Text, data->Caption, data->Type | MB_SETFOREGROUND);
    return 0;
}

int C__Messages::MessageBoxT(const char* lpCaption, UINT uType)
{
    C__MessageBoxData data;
    data.Caption = lpCaption;
    data.Type = uType;
    data.Return = 0;

    MessagesStrStream.flush(); // flushing to buffer (in lpText)

#ifndef MULTITHREADED_MESSAGES_ENABLE
    data.Text = MessagesStringBuf.c_str();
    MessagesStringBuf.erase(); // preparation for next message
#else                          // MULTITHREADED_MESSAGES_ENABLE
    std::string message;
    try
    {
        message = MessagesStringBuf.c_str();
        data.Text = message.c_str();
    }
    catch (const std::bad_alloc&)
    {
        data.Text = __MessagesLowMemory;
    }
    MessagesStringBuf.erase(); // preparation for next message
    LeaveMessagesModul();      // now other threads + message loops can start malfunctioning
#endif                         // MULTITHREADED_MESSAGES_ENABLE

    // we throw MessageBox in a new thread so it does not dispatch messages of this thread
    DWORD threadID;
    HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)__MessagesMessageBoxThreadF, &data, 0, &threadID);
    if (thread != NULL)
    {
        WaitForSingleObject(thread, INFINITE); // we wait until the user releases it
        CloseHandle(thread);
    }
    else
        TRACE_E("Unable to show MessageBox: " << data.Caption << ": " << data.Text);

    return data.Return;
}

int C__Messages::MessageBox(HWND hWnd, const char* lpCaption, UINT uType)
{
    int ret;
    MessagesStrStream.flush(); // flushing to buffer (in lpText)

#ifndef MULTITHREADED_MESSAGES_ENABLE
    if (!IsWindow(hWnd))
        hWnd = NULL;
    ret = __MessagesShowA(hWnd, MessagesStringBuf.c_str(), lpCaption, uType);
    MessagesStringBuf.erase(); // preparation for next message
#else                          // MULTITHREADED_MESSAGES_ENABLE
    std::string message;
    const char* txt = __MessagesLowMemory;
    try
    {
        message = MessagesStringBuf.c_str();
        txt = message.c_str();
    }
    catch (const std::bad_alloc&)
    {
    }
    MessagesStringBuf.erase(); // preparation for next message
    LeaveMessagesModul();      // now other threads + message loops can start malfunctioning

    if (!IsWindow(hWnd))
        hWnd = NULL;
    ret = __MessagesShowA(hWnd, txt, lpCaption, uType);

#endif                         // MULTITHREADED_MESSAGES_ENABLE

    return ret;
}

//*****************************************************************************
//
// C__MessagesW
//

C__MessagesW::C__MessagesW() : MessagesStrStream(&MessagesStringBuf)
{
#ifdef _DEBUG
    // new streams use internal locales which have implemented
    // individual "facets" using lazy creation - they are allocated on heap
    // when needed, i.e. when someone sends something to the stream that has
    // formatting dependent on locale rules, e.g. number, date,
    // or boolean. These "facets" are then deallocated on exit
    // of the program with compiler priority, i.e. after our memory leak check.
    // So if someone uses stream to output something localizable,
    // our debug heap will report memory leaks, even though there are none. To prevent
    // this, we force locales to create all "facets" now, while
    // we're not yet monitoring the heap.
    // For now we use only output stream and only with strings (without conversion)
    // and numbers. So sending a number to stringstream should suffice. If
    // in the future we start using streams more and debug heap starts reporting
    // leaks, we will need to add more input/output here.
    std::wstringstream s;
    s << 1;
#endif // _DEBUG
}

struct C__MessageBoxDataW
{
    const WCHAR* Text;
    const WCHAR* Caption;
    UINT Type;
    int Return;
};

int CALLBACK __MessagesWMessageBoxThreadF(C__MessageBoxDataW* data)
{ // must not wait for a response from the calling thread because it will not respond
    // therefore parent==NULL -> no window disabling etc.
    data->Return = __MessagesShowW(NULL, data->Text, data->Caption, data->Type | MB_SETFOREGROUND);
    return 0;
}

int C__MessagesW::MessageBoxT(const WCHAR* lpCaption, UINT uType)
{
    C__MessageBoxDataW data;
    data.Caption = lpCaption;
    data.Type = uType;
    data.Return = 0;

    MessagesStrStream.flush(); // flushing to buffer (in lpText)

#ifndef MULTITHREADED_MESSAGES_ENABLE
    data.Text = MessagesStringBuf.c_str();
    MessagesStringBuf.erase(); // preparation for next message
#else                          // MULTITHREADED_MESSAGES_ENABLE
    std::wstring message;
    try
    {
        message = MessagesStringBuf.c_str();
        data.Text = message.c_str();
    }
    catch (const std::bad_alloc&)
    {
        data.Text = __MessagesLowMemoryW;
    }
    MessagesStringBuf.erase(); // preparation for next message
    LeaveMessagesModul();      // now other threads + message loops can start malfunctioning
#endif                         // MULTITHREADED_MESSAGES_ENABLE

    // we throw MessageBox in a new thread so it does not dispatch messages of this thread
    DWORD threadID;
    HANDLE thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)__MessagesWMessageBoxThreadF, &data, 0, &threadID);
    if (thread != NULL)
    {
        WaitForSingleObject(thread, INFINITE); // we wait until the user releases it
        CloseHandle(thread);
    }
    else
        TRACE_EW(L"Unable to show MessageBox: " << data.Caption << L": " << data.Text);

    return data.Return;
}

int C__MessagesW::MessageBox(HWND hWnd, const WCHAR* lpCaption, UINT uType)
{
    int ret;
    MessagesStrStream.flush(); // flushing to buffer (in lpText)

#ifndef MULTITHREADED_MESSAGES_ENABLE
    if (!IsWindow(hWnd))
        hWnd = NULL;
    ret = __MessagesShowW(hWnd, MessagesStringBuf.c_str(), lpCaption, uType);
    MessagesStringBuf.erase(); // preparation for next message
#else                          // MULTITHREADED_MESSAGES_ENABLE
    std::wstring message;
    const WCHAR* txt = __MessagesLowMemoryW;
    try
    {
        message = MessagesStringBuf.c_str();
        txt = message.c_str();
    }
    catch (const std::bad_alloc&)
    {
    }
    MessagesStringBuf.erase(); // preparation for next message
    LeaveMessagesModul();      // now other threads + message loops can start malfunctioning

    if (!IsWindow(hWnd))
        hWnd = NULL;
    ret = __MessagesShowW(hWnd, txt, lpCaption, uType);

#endif                         // MULTITHREADED_MESSAGES_ENABLE

    return ret;
}

//*****************************************************************************
//
// rsc
//

namespace
{
// The diagnostic layer is used from late static destructors. Keep its dynamic
// scratch owners alive for the process lifetime so destruction order across
// translation units cannot invalidate them before a final handle/trace report.
std::string& ResourceStringBufferA()
{
    static std::string* value = new std::string;
    return *value;
}

std::wstring& ResourceStringBufferW()
{
    static std::wstring* value = new std::wstring;
    return *value;
}

std::string& PrintfBufferA()
{
    static std::string* value = new std::string;
    return *value;
}

std::wstring& PrintfBufferW()
{
    static std::wstring* value = new std::wstring;
    return *value;
}

std::string& ErrorBufferA()
{
    static std::string* value = new std::string;
    return *value;
}

std::wstring& ErrorBufferW()
{
    static std::wstring* value = new std::wstring;
    return *value;
}

bool LoadResourceStringOwned(int resID, std::wstring& value)
{
    const WCHAR* resource = NULL;
    const int length = LoadStringW(HInstance, resID,
                                   reinterpret_cast<LPWSTR>(&resource), 0);
    if (length <= 0 || resource == NULL)
    {
        value.clear();
        return false;
    }
    value.assign(resource, static_cast<size_t>(length));
    return true;
}

bool VFormatOwned(const char* format, va_list params, std::string& value)
{
    if (format == NULL)
    {
        value.clear();
        return false;
    }
    try
    {
        va_list countParams;
        va_copy(countParams, params);
        const int length = _vscprintf(format, countParams);
        va_end(countParams);
        if (length < 0)
        {
            value.clear();
            return false;
        }
        std::string staged(static_cast<size_t>(length) + 1, '\0');
        if (vsprintf_s(staged.data(), staged.size(), format, params) < 0)
        {
            value.clear();
            return false;
        }
        staged.resize(static_cast<size_t>(length));
        value.swap(staged);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        value.clear();
        return false;
    }
}

bool VFormatOwned(const WCHAR* format, va_list params, std::wstring& value)
{
    if (format == NULL)
    {
        value.clear();
        return false;
    }
    try
    {
        va_list countParams;
        va_copy(countParams, params);
        const int length = _vscwprintf(format, countParams);
        va_end(countParams);
        if (length < 0)
        {
            value.clear();
            return false;
        }
        std::wstring staged(static_cast<size_t>(length) + 1, L'\0');
        if (vswprintf_s(staged.data(), staged.size(), format, params) < 0)
        {
            value.clear();
            return false;
        }
        staged.resize(static_cast<size_t>(length));
        value.swap(staged);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        value.clear();
        return false;
    }
}

bool FormatSystemErrorOwned(DWORD error, std::wstring& value)
{
    std::wstring staged = L"(" + std::to_wstring(error) + L") ";
    WCHAR* systemText = NULL;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
        NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&systemText), 0, NULL);

    try
    {
        if (length != 0 && systemText != NULL)
            staged.append(systemText, static_cast<size_t>(length));
    }
    catch (...)
    {
        if (systemText != NULL)
            LocalFree(systemText);
        throw;
    }
    if (systemText != NULL)
        LocalFree(systemText);
    value.swap(staged);
    return length != 0;
}

std::string& MessagesTitleStorageA()
{
    static std::string* value = new std::string;
    return *value;
}

std::wstring& MessagesTitleStorageW()
{
    static std::wstring* value = new std::wstring;
    return *value;
}
} // namespace

void PreallocateMessagesDiagnosticStorage()
{
    ResourceStringBufferA();
    ResourceStringBufferW();
    PrintfBufferA();
    PrintfBufferW();
    ErrorBufferA();
    ErrorBufferW();
    MessagesTitleStorageA();
    MessagesTitleStorageW();
}

const char* rsc(int resID)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        try
        {
            std::wstring resource;
            if (!LoadResourceStringOwned(resID, resource))
            {
                TRACE_E("Unable to load string from resource (resource ID is " << resID << ")");
                ResourceStringBufferA().clear();
            }
            else if (!EncodeLegacyDiagnosticText(resource, ResourceStringBufferA()))
                ResourceStringBufferA() =
                    "Unicode resource is unavailable through the legacy diagnostic API.";
            return ResourceStringBufferA().c_str();
        }
        catch (const std::bad_alloc&)
        {
            return "Insufficient memory.";
        }
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

const WCHAR* rscW(int resID)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        try
        {
            if (!LoadResourceStringOwned(resID, ResourceStringBufferW()))
            {
                TRACE_E("Unable to load string from resource (resource ID is " << resID << ")");
                ResourceStringBufferW().clear();
            }
            return ResourceStringBufferW().c_str();
        }
        catch (const std::bad_alloc&)
        {
            return L"Insufficient memory.";
        }
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

//*****************************************************************************
//
// spf
//

const char* spf(const char* formatString, ...)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        try
        {
            std::string& buffer = PrintfBufferA();
            va_list params;
            va_start(params, formatString);
            VFormatOwned(formatString, params, buffer);
            va_end(params);
            return buffer.c_str();
        }
        catch (const std::bad_alloc&)
        {
            return "Insufficient memory.";
        }
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

const WCHAR* spfW(const WCHAR* formatString, ...)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        try
        {
            std::wstring& buffer = PrintfBufferW();
            va_list params;
            va_start(params, formatString);
            VFormatOwned(formatString, params, buffer);
            va_end(params);
            return buffer.c_str();
        }
        catch (const std::bad_alloc&)
        {
            return L"Insufficient memory.";
        }
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

//*****************************************************************************
//
// spf
//

const char* spf(int formatStringResID, ...)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        try
        {
            const char* format = rsc(formatStringResID);
            std::string& buffer = PrintfBufferA();
            va_list params;
            va_start(params, formatStringResID);
            VFormatOwned(format, params, buffer);
            va_end(params);
            return buffer.c_str();
        }
        catch (const std::bad_alloc&)
        {
            return "Insufficient memory.";
        }
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

const WCHAR* spfW(int formatStringResID, ...)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        try
        {
            const WCHAR* format = rscW(formatStringResID);
            std::wstring& buffer = PrintfBufferW();
            va_list params;
            va_start(params, formatStringResID);
            VFormatOwned(format, params, buffer);
            va_end(params);
            return buffer.c_str();
        }
        catch (const std::bad_alloc&)
        {
            return L"Insufficient memory.";
        }
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

//*****************************************************************************
//
// err
//

const char* err(DWORD error)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        try
        {
            std::wstring errorW;
            FormatSystemErrorOwned(error, errorW);
            if (!EncodeLegacyDiagnosticText(errorW, ErrorBufferA()))
                ErrorBufferA() =
                    "Unicode system error is unavailable through the legacy diagnostic API.";
            return ErrorBufferA().c_str();
        }
        catch (const std::bad_alloc&)
        {
            return "Insufficient memory.";
        }
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

const WCHAR* errW(DWORD error)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        try
        {
            FormatSystemErrorOwned(error, ErrorBufferW());
            return ErrorBufferW().c_str();
        }
        catch (const std::bad_alloc&)
        {
            return L"Insufficient memory.";
        }
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

//*****************************************************************************
//
// SetMessagesTitleW
//

void SetMessagesTitleW(const WCHAR* title)
{
    try
    {
        std::wstring wide = title != NULL ? title : L"";
        std::string narrow;
        if (!EncodeLegacyDiagnosticText(wide, narrow))
            narrow = "Sally";
        std::wstring& wideStorage = MessagesTitleStorageW();
        std::string& narrowStorage = MessagesTitleStorageA();
        wideStorage.swap(wide);
        narrowStorage.swap(narrow);
        __MessagesTitleW = wideStorage.c_str();
        __MessagesTitle = narrowStorage.c_str();
    }
    catch (const std::bad_alloc&)
    {
        // Keep the previous process-lifetime title. Diagnostics must remain usable
        // when the title itself cannot be allocated.
    }
}

//*****************************************************************************
//
// SetMessagesParent
//

void SetMessagesParent(HWND parent)
{
    __MessagesParent = parent;
}

#endif // MESSAGES_DISABLE
