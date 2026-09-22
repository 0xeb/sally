// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <windows.h>
#include <crtdbg.h>
#include <tchar.h>
#include <limits>
#include <string>

#include "DiagnosticTextEncoding.h"

#if defined(__TRACESERVER) || defined(TRACE_ENABLE)

// The order here is important.
// Section names must be 8 characters or less.
// The sections with the same name before the $
// are merged into one section. The order that
// they are merged is determined by sorting
// the characters after the $.
// i_trace and i_trace_end are used to set
// boundaries so we can find the real functions
// that we need to call for initialization.

#pragma warning(disable : 4075) // we want to define module initialization order

typedef void(__cdecl* _PVFV)(void);

#pragma section(".i_trc$a", read)
__declspec(allocate(".i_trc$a")) const _PVFV i_trace = (_PVFV)1; // at the beginning of section .i_trc we place variable i_trace

#pragma section(".i_trc$z", read)
__declspec(allocate(".i_trc$z")) const _PVFV i_trace_end = (_PVFV)1; // and at the end of section .i_trc we place variable i_trace_end

static wchar_t __TraceFileMappingName[64];
static wchar_t __TraceOpenConnectionMutexName[64];
static wchar_t __TraceConnectDataReadyEventName[64];
static wchar_t __TraceConnectDataAcceptedEventName[64];

static bool GetTraceModulePath(std::wstring& path) noexcept
{
    try
    {
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer(static_cast<size_t>(capacity), L'\0');
            SetLastError(ERROR_SUCCESS);
            const DWORD len = GetModuleFileNameW(NULL, buffer.data(), capacity);
            if (len == 0)
                return false;

            const DWORD error = GetLastError();
            if (len < capacity && !(len == capacity - 1 && error == ERROR_INSUFFICIENT_BUFFER))
            {
                buffer.resize(len);
                path.swap(buffer);
                return true;
            }
            if (capacity > (std::numeric_limits<DWORD>::max)() / 2)
                return false;
            capacity *= 2;
        }
    }
    catch (...)
    {
        return false;
    }
}

static bool GetTraceTempPath(std::wstring& path) noexcept
{
    try
    {
        DWORD capacity = 256;
        for (;;)
        {
            std::wstring buffer(static_cast<size_t>(capacity), L'\0');
            const DWORD len = GetTempPathW(capacity, buffer.data());
            if (len == 0)
                return false;
            if (len < capacity)
            {
                buffer.resize(len);
                path.swap(buffer);
                return true;
            }

            const DWORD required = len == (std::numeric_limits<DWORD>::max)() ? len : len + 1;
            if (required <= capacity)
                return false;
            capacity = required;
        }
    }
    catch (...)
    {
        return false;
    }
}

static DWORD GetTraceNamespaceHash()
{
    std::wstring modulePath;
    if (!GetTraceModulePath(modulePath))
        return 0;

    size_t len = modulePath.length();
    while (len > 0 && modulePath[len - 1] != L'\\' && modulePath[len - 1] != L'/')
        len--;

    DWORD hash = 2166136261U; // FNV-1a
    for (size_t i = 0; i < len; i++)
    {
        WCHAR ch = modulePath[i];
        if (ch == L'/')
            ch = L'\\';
        if (ch >= L'A' && ch <= L'Z')
            ch = ch - L'A' + L'a';
        hash ^= ch & 0xff;
        hash *= 16777619U;
        hash ^= ch >> 8;
        hash *= 16777619U;
    }
    return hash;
}

static void InitializeTraceObjectNames()
{
    DWORD namespaceHash = GetTraceNamespaceHash();

    _stprintf_s(__TraceFileMappingName, L"TraceServerMappingName.%08X", namespaceHash);
    _stprintf_s(__TraceOpenConnectionMutexName, L"TraceServerOpenConnectionMutex.%08X", namespaceHash);
    _stprintf_s(__TraceConnectDataReadyEventName, L"TraceServerConnectDataReadyEvent.%08X", namespaceHash);
    _stprintf_s(__TraceConnectDataAcceptedEventName, L"TraceServerConnectDataAcceptedEvent.%08X", namespaceHash);
}

void Initialize__Trace()
{
    InitializeTraceObjectNames();

    const _PVFV* x = &i_trace;
    for (++x; x < &i_trace_end; ++x)
        if (*x != NULL)
            (*x)();
}

#pragma init_seg(".i_trc$m")

const wchar_t* __FILE_MAPPING_NAME = __TraceFileMappingName;
const wchar_t* __OPEN_CONNECTION_MUTEX = __TraceOpenConnectionMutexName;
const wchar_t* __CONNECT_DATA_READY_EVENT_NAME = __TraceConnectDataReadyEventName;
const wchar_t* __CONNECT_DATA_ACCEPTED_EVENT_NAME = __TraceConnectDataAcceptedEventName;

#endif // defined(__TRACESERVER) || defined(TRACE_ENABLE)

#ifdef TRACE_ENABLE

#include <ostream>
#include <streambuf>
#include <stdio.h>
#if defined(__HEADER_TRACE_H) && !defined(_INC_PROCESS)
#error "Your precomp.h includes trace.h, so it must also earlier include process.h."
#endif // defined(__HEADER_TRACE_H) && !defined(_INC_PROCESS)
#include <process.h>
#ifdef _DEBUG
#include <sstream>
#endif // _DEBUG

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "trace.h"

#ifdef MULTITHREADED_TRACE_ENABLE

#undef _beginthreadex
#undef CreateThread

#endif // MULTITHREADED_TRACE_ENABLE

// See the comment on GetTrace()'s declaration in trace.h.
C__Trace& GetTrace()
{
    static C__Trace trace;
    return trace;
}

// Self-referencing counterparts of TRACE_I/TRACE_E for use ONLY inside C__Trace's
// own methods (Connect/Disconnect/CloseTraceFile), which run during GetTrace()'s own construction
// (C__Trace::C__Trace calls Connect(FALSE)). Calling the public TRACE_I/TRACE_E macros there would
// call GetTrace() again on the same thread while the Meyer's singleton is still being built -
// MSVC's magic-statics guard blocks that reentrant call waiting for a completion that can never
// happen, since it's the same thread doing the constructing. These operate on the already-existing
// *this instead, so they never touch the construction guard.
#define TRACE_I_SELF(str)                                                    \
    (::EnterCriticalSection(&CriticalSection), StoreLastError(),             \
     OStream() << str, *this)                                                \
        .SetInfo(__FILE__, __LINE__)                                         \
        .SendMessageToServer(__mtInformation)                                \
        .RestoreLastError()

#define TRACE_E_SELF(str)                                                    \
    (::EnterCriticalSection(&CriticalSection), StoreLastError(),             \
     OStream() << str, *this)                                                \
        .SetInfo(__FILE__, __LINE__)                                         \
        .SendMessageToServer(__mtError)                                      \
        .RestoreLastError()

#ifdef MULTITHREADED_TRACE_ENABLE

//*****************************************************************************
//
// C__TraceThreadCache
//

#define __TRACE_CACHE_DELTA 16

C__TraceThreadCache::C__TraceThreadCache()
{
    UniqueThreadID = 0;
    Available = Count = 0;
    Data = (C__TraceCacheData*)GlobalAlloc(GMEM_FIXED, __TRACE_CACHE_DELTA *
                                                           sizeof(C__TraceCacheData));
    if (Data != NULL)
        Available = __TRACE_CACHE_DELTA;

    for (int i = 0; i < __TRACE_CACHE_SIZE; i++)
        CacheUID[i] = -1;
}

C__TraceThreadCache::~C__TraceThreadCache()
{
    if (Data != NULL)
    {
        for (int i = 0; i < Count; i++)
            CloseHandle(Data[i].Handle);
        GlobalFree((HGLOBAL)Data);
        Data = NULL;
        Count = 0;
        Available = 0;
    }
}

BOOL C__TraceThreadCache::EnlargeArray()
{
    if (Data == NULL)
        return FALSE;
    C__TraceCacheData* New = (C__TraceCacheData*)GlobalReAlloc((HGLOBAL)Data,
                                                               (Available + __TRACE_CACHE_DELTA) *
                                                                   sizeof(C__TraceCacheData),
                                                               GMEM_MOVEABLE);
    if (New == NULL)
        return FALSE;
    else
    {
        Data = New;
        Available += __TRACE_CACHE_DELTA;
        return TRUE;
    }
}

BOOL C__TraceThreadCache::Move(int direction, DWORD first, DWORD count)
{
    if (count == 0)
    {
        if (direction == 1 && Available == Count)
            return EnlargeArray();
        return TRUE;
    }
    if (direction == 1) // down
    {
        if (Available == Count && !EnlargeArray())
            return FALSE;
        memmove(Data + first + 1, Data + first, count * sizeof(C__TraceCacheData));
    }
    else // Up
        memmove(Data + first - 1, Data + first, count * sizeof(C__TraceCacheData));
    return TRUE;
}

BOOL C__TraceThreadCache::GetIndex(DWORD tid, int& index)
{
    if (Count == 0)
    {
        index = 0;
        return FALSE;
    }

    int l = 0, r = Count - 1, m;
    while (1)
    {
        m = (l + r) / 2;
        DWORD hw = Data[m].TID;
        if (hw == tid) // found
        {
            index = m;
            return TRUE;
        }
        else if (hw > tid)
        {
            if (l == r || l > m - 1) // not found
            {
                index = m; // should be at this position
                return FALSE;
            }
            r = m - 1;
        }
        else
        {
            if (l == r) // not found
            {
                index = m + 1; // should be after this position
                return FALSE;
            }
            l = m + 1;
        }
    }
}

BOOL C__TraceThreadCache::Add(HANDLE handle, DWORD tid)
{
    int index;
    BOOL found = GetIndex(tid, index);
    if (!found)
    {
        if (Available == Count) // full, discarding dead threads
        {
            DWORD code;
            for (int i = Count - 1; i >= 0; i--)
            {
                if (!GetExitCodeThread(Data[i].Handle, &code) || code != STILL_ACTIVE)
                {
                    DWORD id = Data[i].TID;                         // cache update:
                    if (CacheUID[__TraceCacheGetIndex(id)] != -1 && // valid record
                        CacheTID[__TraceCacheGetIndex(id)] == id)   // matching TID
                    {
                        CacheUID[__TraceCacheGetIndex(id)] = -1; // invalidation
                    }

                    CloseHandle(Data[i].Handle);
                    Move(0, i + 1, Count - i - 1);
                    Count--;

                    if (index > i)
                        index--;
                }
            }
        }
        if (Available == Count && !EnlargeArray())
            return FALSE;

        Move(1, index, Count - index); // insert new record
        Data[index].Handle = handle;
        Data[index].TID = tid;
        Data[index].UID = UniqueThreadID;
        Count++;
    }
    else
    {
        CloseHandle(Data[index].Handle);
        Data[index].Handle = handle;
        Data[index].UID = UniqueThreadID;
    }
    // cache update
    CacheTID[__TraceCacheGetIndex(tid)] = tid;
    CacheUID[__TraceCacheGetIndex(tid)] = UniqueThreadID++;

    return TRUE;
}

DWORD
C__TraceThreadCache::GetUniqueThreadId(DWORD tid)
{
    if (CacheUID[__TraceCacheGetIndex(tid)] != -1 && // if valid record
        CacheTID[__TraceCacheGetIndex(tid)] == tid)  // and if matches tid
    {
        return CacheUID[__TraceCacheGetIndex(tid)]; // uid is in cache
    }

    int index;
    if (GetIndex(tid, index))
    {
        CacheTID[__TraceCacheGetIndex(tid)] = Data[index].TID;
        CacheUID[__TraceCacheGetIndex(tid)] = Data[index].UID;
        return Data[index].UID;
    }
    else
        return -1; // not found -> this should not happen
}

//*****************************************************************************
//
// __TRACECreateThread
//

HANDLE __TRACECreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes,
                           DWORD dwStackSize,
                           LPTHREAD_START_ROUTINE lpStartAddress,
                           LPVOID lpParameter, DWORD dwCreationFlags,
                           LPDWORD lpThreadId)
{
    DWORD tid;
    HANDLE ret = CreateThread(lpThreadAttributes, dwStackSize, lpStartAddress,
                              lpParameter, dwCreationFlags | CREATE_SUSPENDED, &tid);
    if (ret != NULL)
    {
        HANDLE handle;
        if (DuplicateHandle(GetCurrentProcess(), ret, GetCurrentProcess(),
                            &handle, 0, FALSE, DUPLICATE_SAME_ACCESS))
        {
            EnterCriticalSection(&GetTrace().CriticalSection);
            if (!GetTrace().ThreadCache.Add(handle, tid))
                CloseHandle(handle);
            LeaveCriticalSection(&GetTrace().CriticalSection);
        }
        if ((dwCreationFlags & CREATE_SUSPENDED) == 0)
            ResumeThread(ret);
    }
    if (lpThreadId != NULL)
        *lpThreadId = tid;
    return ret;
}

//*****************************************************************************
//
// __TRACE_beginthreadex
//

uintptr_t __TRACE_beginthreadex(void* security, unsigned stack_size,
                                unsigned(__stdcall* start_address)(void*),
                                void* arglist, unsigned initflag,
                                unsigned* thrdid)
{
    unsigned tid;
    uintptr_t ret = _beginthreadex(security, stack_size, start_address,
                                   arglist, initflag | CREATE_SUSPENDED, &tid);
    if (ret != NULL)
    {
        HANDLE handle;
        if (DuplicateHandle(GetCurrentProcess(), (HANDLE)ret, GetCurrentProcess(),
                            &handle, 0, FALSE, DUPLICATE_SAME_ACCESS))
        {
            EnterCriticalSection(&GetTrace().CriticalSection);
            if (!GetTrace().ThreadCache.Add(handle, tid))
                CloseHandle(handle);
            LeaveCriticalSection(&GetTrace().CriticalSection);
            if ((initflag & CREATE_SUSPENDED) == 0)
                ResumeThread((HANDLE)ret);
        }
    }
    if (thrdid != NULL)
        *thrdid = tid;
    return ret;
}

#endif // MULTITHREADED_TRACE_ENABLE

// ****************************************************************************
//
// CWStr
//

CWStr::CWStr(const char* s) : IsOK(TRUE), OwnsStr(s != NULL), Str(NULL)
{
    if (s != NULL)
        IsOK = sally::diagnostic::DecodeAcp(s, OwnedStr);
}

//*****************************************************************************
//
// C__Trace
//

C__Trace::C__Trace() : TraceStrStream(&TraceStringBuf), TraceStrStreamW(&TraceStringBufW)
{
#ifdef _DEBUG
    // new streams use internal locales, which have implemented
    // individual "facets" via lazy creation - are allocated on heap
    // when needed, i.e. when someone sends something to stream that has
    // formatting dependent on locale rules, like number, date,
    // or boolean. These "facets" are then deallocated on exit
    // of program with compiler priority, i.e. after our memory leak check.
    // So if someone uses stream to output anything localizable,
    // our debug heap will report memory leaks, even if there are none. To
    // prevent this, we force locales to create all "facets" now, while
    // we are not yet checking heap.
    // For now we use only output stream and only with strings (without conversion)
    // and numbers. So sending a number to stringstream should suffice. If
    // in the future we start using streams more and debug heap starts reporting
    // leaks, we will have to add more inputs/outputs here.
    std::stringstream s;
    s << 1;
    std::wstringstream s2;
    s2 << 1;
#endif // _DEBUG

    InitializeCriticalSection(&CriticalSection);
    HWritePipe = NULL;
    HPipeSemaphore = NULL;
    BytesAllocatedForWriteToPipe = 0;
#ifdef TRACE_TO_FILE
    HTraceFile = NULL;
#ifdef __TRACESERVER
    TraceFileName.clear();
#endif // __TRACESERVER
#endif // TRACE_TO_FILE
    ::QueryPerformanceFrequency(&PerformanceFrequency);
    SupportPerformanceFrequency = (PerformanceFrequency.QuadPart != 0);
    if (SupportPerformanceFrequency)
        ::QueryPerformanceCounter(&StartPerformanceCounter);
    else
        StartPerformanceCounter.QuadPart = 0;

#ifdef MULTITHREADED_TRACE_ENABLE
    HANDLE handle;
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                        GetCurrentProcess(), &handle,
                        0, FALSE, DUPLICATE_SAME_ACCESS) &&
        !ThreadCache.Add(handle, GetCurrentThreadId()))
    {
        CloseHandle(handle);
    }
#endif // MULTITHREADED_TRACE_ENABLE

    Connect(FALSE);
}

// See IsTraceAlive()'s declaration in trace.h - this is the only place that
// flips it, and it must happen before Disconnect()/DeleteCriticalSection() touch anything, so a
// concurrent or later caller checking IsTraceAlive() never observes a half-torn-down object.
static bool s_TraceAlive = true;

bool IsTraceAlive()
{
    return s_TraceAlive;
}

C__Trace::~C__Trace()
{
    s_TraceAlive = false;
    Disconnect();
    DeleteCriticalSection(&CriticalSection);
}

BOOL C__Trace::Connect(BOOL onUserRequest)
{
    EnterCriticalSection(&CriticalSection);
    DWORD storedLastError = GetLastError();

#ifdef TRACE_TO_FILE
    if (HTraceFile == NULL)
    {
        try
        {
            std::wstring tmpDir;
            if (GetTraceTempPath(tmpDir))
            {
                if (!tmpDir.empty() && tmpDir.back() != L'\\')
                    tmpDir += L'\\';
                const std::wstring stem = tmpDir + L"altap_traces";

                unsigned long long num = 1;
                while (1)
                {
                    const std::wstring candidate = stem + L"_" + std::to_wstring(num) + L".log";
                    HTraceFile = CreateFileW(candidate.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (HTraceFile != INVALID_HANDLE_VALUE)
                    {
#ifdef __TRACESERVER
                        TraceFileName = candidate;
#endif // __TRACESERVER
                        break;
                    }
                    DWORD err = GetLastError();
                    if ((err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) ||
                        num == (std::numeric_limits<unsigned long long>::max)())
                        break; // unexpected error (and nowhere to output it)
                    ++num;
                }
                if (HTraceFile == INVALID_HANDLE_VALUE)
                    HTraceFile = NULL;
                else // write header to file (column identification)
                {
                    DWORD wr;
                    const WCHAR* fileHeader = L"\xFEFF" /* BOM */ L"Type\tTID\t"
#ifdef MULTITHREADED_TRACE_ENABLE
                                              L"UTID\t"
#endif // MULTITHREADED_TRACE_ENABLE
                                              L"Date\tTime\tCounter [ms]\tModule\tLine\tMessage\r\n";
                    WriteFile(HTraceFile, fileHeader, (int)(sizeof(WCHAR) * wcslen(fileHeader)), &wr, NULL);
                    TRACE_I_SELF("Opening log file" << (onUserRequest ? " on user's request." : "."));
                }
            }
        }
        catch (...)
        {
            if (HTraceFile != NULL && HTraceFile != INVALID_HANDLE_VALUE)
                CloseHandle(HTraceFile);
            HTraceFile = NULL;
#ifdef __TRACESERVER
            TraceFileName.clear();
#endif // __TRACESERVER
        }
    }
#endif // TRACE_TO_FILE

    if (HWritePipe != NULL) // test server connection, if down, HWritePipe will close and we will try to reconnect
        TRACE_I_SELF("Connect request received when already connected to Trace Server.");

    BOOL ret = FALSE;
    if (HWritePipe != NULL)
        ret = TRUE; // if connection is already established
    else
    {
        // try to open Mutex for access to shared memory
        HANDLE hOpenConnectionMutex;
        hOpenConnectionMutex = OpenMutex(/*MUTEX_ALL_ACCESS*/ SYNCHRONIZE, FALSE, __OPEN_CONNECTION_MUTEX);
        if (hOpenConnectionMutex != NULL) // server found
        {
            // take over ConnectionMutex
            DWORD waitRet;
            while (1)
            {
                waitRet = WaitForSingleObject(hOpenConnectionMutex,
                                              __COMMUNICATION_WAIT_TIMEOUT);
                if (waitRet != WAIT_ABANDONED)
                    break;
            }
            if (waitRet == WAIT_OBJECT_0) // successfully acquired
            {
                // open FileMapping
                HANDLE hFileMapping;
                hFileMapping = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, __FILE_MAPPING_NAME);
                if (hFileMapping != NULL)
                {
                    // map shared memory
                    char* mapAddress;
                    mapAddress = (char*)MapViewOfFile(hFileMapping, FILE_MAP_ALL_ACCESS,
                                                      0, 0, __SIZEOF_CLIENTSERVERINITDATA);
                    if (mapAddress != NULL)
                    {
                        BytesAllocatedForWriteToPipe = 0;
                        HPipeSemaphore = CreateSemaphore(NULL, __PIPE_SIZE, __PIPE_SIZE, NULL);
                        if (HPipeSemaphore != NULL)
                        {
                            HANDLE HReadPipe;

                            // create anonymous pipe
                            SECURITY_ATTRIBUTES sa;
                            char secDesc[SECURITY_DESCRIPTOR_MIN_LENGTH];
                            sa.nLength = sizeof(sa);
                            sa.bInheritHandle = FALSE;
                            sa.lpSecurityDescriptor = &secDesc;
                            InitializeSecurityDescriptor(sa.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
                            // give the security descriptor a NULL DACL, done using the  "TRUE, (PACL)NULL" here
                            SetSecurityDescriptorDacl(sa.lpSecurityDescriptor, TRUE, 0, FALSE);
                            if (CreatePipe(&HReadPipe, &HWritePipe, &sa, __PIPE_SIZE * 1024))
                            {
                                // write handle for reading from pipe to shared memory
                                int expectedServerVer = TRACE_CLIENT_VERSION;
                                C__ClientServerInitData* initData = (C__ClientServerInitData*)mapAddress;
                                initData->Version = expectedServerVer - 1;               // try oldest connection method first
                                initData->ClientOrServerProcessId = GetCurrentProcessId();
                                initData->HReadOrWritePipe = HReadPipe;
                                initData->HPipeSemaphore = HPipeSemaphore;

                                // open hReadyEvent
                                HANDLE hReadyEvent;
                                hReadyEvent = OpenEvent(EVENT_ALL_ACCESS, TRUE, __CONNECT_DATA_READY_EVENT_NAME);
                                // open hAcceptedEvent
                                HANDLE hAcceptedEvent;
                                hAcceptedEvent = OpenEvent(EVENT_ALL_ACCESS, TRUE, __CONNECT_DATA_ACCEPTED_EVENT_NAME);
                                if (hReadyEvent != NULL && hAcceptedEvent != NULL)
                                {
                                    ResetEvent(hAcceptedEvent); // I want only a fresh server response

                                    while (1)
                                    {
                                        SetEvent(hReadyEvent); // tell server I have prepared data

                                        // wait for server to process data
                                        waitRet = WaitForSingleObject(hAcceptedEvent, __COMMUNICATION_WAIT_TIMEOUT);
                                        if (waitRet == WAIT_OBJECT_0)
                                        {
                                            // message displayed on old non-Unicode Trace Server (must be ANSI)
                                            const char* oldTraceServerA = "Disconnecting: this is not Unicode version of Trace Server.";

                                            // check the result from server
                                            if (initData->Version == TRUE)
                                            {
                                                if (expectedServerVer == TRACE_CLIENT_VERSION) // great, successfully connected to new Trace Server!
                                                {
#ifdef TRACE_IGNORE_AUTOCLEAR
                                                    ret = SendIgnoreAutoClear(TRUE); // ignore, on error we will disconnect
#else                                                                                // TRACE_IGNORE_AUTOCLEAR
                                                    ret = SendIgnoreAutoClear(FALSE); // do not ignore, on error we will disconnect
#endif                                                                               // TRACE_IGNORE_AUTOCLEAR
                                                }
                                                else
                                                    TRACE_E_SELF(oldTraceServerA);
                                            }
                                            else // failed: try to create pipe on server side
                                            {
                                                // write new version to shared memory, thus asking server to send handle for writing to pipe
                                                initData->Version = expectedServerVer; // Version

                                                SetEvent(hReadyEvent); // tell server I have prepared data

                                                // wait for server to process data
                                                waitRet = WaitForSingleObject(hAcceptedEvent, __COMMUNICATION_WAIT_TIMEOUT);
                                                if (waitRet == WAIT_OBJECT_0 && initData->Version == TRUE) // check the result from server
                                                {
                                                    HANDLE hWritePipeFromSrv = NULL;
                                                    HANDLE hPipeSemaphoreFromSrv = NULL;

                                                    // obtain server process handle
                                                    HANDLE hServerProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE,
                                                                                        initData->ClientOrServerProcessId /* here it is server PID */);
                                                    // obtain pipe and semaphore handles
                                                    if (hServerProcess != NULL &&
                                                        DuplicateHandle(hServerProcess, initData->HReadOrWritePipe /* here it is HWritePipe */, // server
                                                                        GetCurrentProcess(), &hWritePipeFromSrv,                                // client
                                                                        GENERIC_WRITE, FALSE, 0) &&
                                                        DuplicateHandle(hServerProcess, initData->HPipeSemaphore, // server
                                                                        GetCurrentProcess(), &hPipeSemaphoreFromSrv,                                        // client
                                                                        0, FALSE, DUPLICATE_SAME_ACCESS))
                                                    {
                                                        initData->Version = 3;                                   // write result -> 3 = succeeded, we have handles
                                                        initData->ClientOrServerProcessId = GetCurrentProcessId(); // here it is client PID
                                                    }
                                                    else
                                                    {
                                                        initData->Version = FALSE; // write result -> failed
                                                    }
                                                    if (hServerProcess != NULL)
                                                        CloseHandle(hServerProcess);

                                                    SetEvent(hReadyEvent); // tell server I have read data and written result

                                                    // on success: wait for server to start thread reading data from pipe and sends result
                                                    // on failure: tell server it failed, return failure again
                                                    waitRet = WaitForSingleObject(hAcceptedEvent, __COMMUNICATION_WAIT_TIMEOUT);
                                                    if (waitRet == WAIT_OBJECT_0 && // check the result from server
                                                        initData->Version == 2 /* 2 = reading thread successfully started in server */)
                                                    {
                                                        CloseHandle(HPipeSemaphore);
                                                        HPipeSemaphore = hPipeSemaphoreFromSrv; // use semaphore from server (close client one)

                                                        CloseHandle(HWritePipe);
                                                        HWritePipe = hWritePipeFromSrv; // use pipe from server (close client one)

                                                        if (expectedServerVer == TRACE_CLIENT_VERSION) // great, successfully connected to new Trace Server!
                                                        {
#ifdef TRACE_IGNORE_AUTOCLEAR
                                                            ret = SendIgnoreAutoClear(TRUE); // ignore, on error we will disconnect
#else                                                                                        // TRACE_IGNORE_AUTOCLEAR
                                                            ret = SendIgnoreAutoClear(FALSE); // do not ignore, on error we will disconnect
#endif                                                                                       // TRACE_IGNORE_AUTOCLEAR
                                                        }
                                                        else
                                                            TRACE_E_SELF(oldTraceServerA);
                                                    }
                                                    else
                                                    {
                                                        if (hWritePipeFromSrv != NULL)
                                                            CloseHandle(hWritePipeFromSrv);
                                                        if (hPipeSemaphoreFromSrv != NULL)
                                                            CloseHandle(hPipeSemaphoreFromSrv);
                                                    }
                                                }
                                                else // connection failed both ways (probably old Trace Server)
                                                {
                                                    if (expectedServerVer == TRACE_CLIENT_VERSION) // we tried new server version
                                                    {
                                                        expectedServerVer = TRACE_CLIENT_VERSION - 2; // now try older server version
                                                        // write version to shared memory that old server version supports
                                                        initData->Version = expectedServerVer - 1; // Version
                                                        continue;
                                                    }
                                                }
                                            }
                                        }
                                        break;
                                    }
                                }
                                if (hReadyEvent != NULL)
                                    CloseHandle(hReadyEvent);
                                if (hAcceptedEvent != NULL)
                                    CloseHandle(hAcceptedEvent);
                                if (!ret)
                                {
                                    CloseHandle(HWritePipe);
                                    HWritePipe = NULL;
                                }
                                CloseHandle(HReadPipe);
                            }
                            if (!ret)
                            {
                                CloseHandle(HPipeSemaphore);
                                HPipeSemaphore = NULL;
                            }
                        }
                        UnmapViewOfFile(mapAddress);
                    }
                    CloseHandle(hFileMapping);
                }
                ReleaseMutex(hOpenConnectionMutex); // other clients can connect
            }
            CloseHandle(hOpenConnectionMutex);
        }
        if (ret)
        {
            TRACE_I_SELF("Connected" << (onUserRequest ? " on user's request." : "."));
#ifdef TRACE_TO_FILE
            if (HTraceFile != NULL)
                TRACE_I_SELF("TRACE MESSAGES ARE ALSO WRITTEN TO FILE IN TEMP DIRECTORY.");
#endif // TRACE_TO_FILE
        }
    }
    SetLastError(storedLastError);
    LeaveCriticalSection(&CriticalSection);
    return ret;
}

void C__Trace::Disconnect()
{
    EnterCriticalSection(&CriticalSection);
    DWORD storedLastError = GetLastError();
    if (HWritePipe != NULL)
    {
        TRACE_I_SELF("Disconnected.");
        CloseWritePipeAndSemaphore();
    }
#ifdef TRACE_TO_FILE
    if (HTraceFile != NULL)
    {
        TRACE_I_SELF("Closing log file.");
        CloseHandle(HTraceFile);
        HTraceFile = NULL;
#ifdef __TRACESERVER
        TraceFileName.clear();
#endif // __TRACESERVER
    }
#endif // TRACE_TO_FILE
    SetLastError(storedLastError);
    LeaveCriticalSection(&CriticalSection);
}

#ifdef TRACE_TO_FILE
void C__Trace::CloseTraceFile()
{
    EnterCriticalSection(&CriticalSection);
    DWORD storedLastError = GetLastError();
    if (HTraceFile != NULL)
    {
        TRACE_I_SELF("Closing log file on user's request.");
        CloseHandle(HTraceFile);
        HTraceFile = NULL;
#ifdef __TRACESERVER
        TraceFileName.clear();
#endif // __TRACESERVER
    }
    SetLastError(storedLastError);
    LeaveCriticalSection(&CriticalSection);
}
#endif // TRACE_TO_FILE

BOOL C__Trace::WritePipe(LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite)
{
    DWORD numberOfBytesWritten = 0;
    while (BytesAllocatedForWriteToPipe < nNumberOfBytesToWrite)
    {
        DWORD res = WaitForSingleObject(HPipeSemaphore, 500);
        if (res == WAIT_OBJECT_0)
            BytesAllocatedForWriteToPipe += 1024;
        else
        {
            if (res == WAIT_TIMEOUT) // timeout, check if server pipe is still alive
            {
                if (!WriteFile(HWritePipe, lpBuffer, 0, &numberOfBytesWritten, NULL))
                    return FALSE;
            }
            else // other error, better quit to avoid infinite loop
                return FALSE;
        }
    }
    if (WriteFile(HWritePipe, lpBuffer, nNumberOfBytesToWrite, &numberOfBytesWritten, NULL) &&
        numberOfBytesWritten == nNumberOfBytesToWrite)
    {
        BytesAllocatedForWriteToPipe -= nNumberOfBytesToWrite;
        return TRUE;
    }
    return FALSE;
}

void C__Trace::CloseWritePipeAndSemaphore()
{
    if (HWritePipe != NULL)
        CloseHandle(HWritePipe);
    HWritePipe = NULL;
    if (HPipeSemaphore != NULL)
        CloseHandle(HPipeSemaphore);
    HPipeSemaphore = NULL;
}

void C__Trace::SendSetNameMessageToServer(const char* name, const WCHAR* nameW, C__MessageType type)
{
    if (HWritePipe != NULL)
    {
        BOOL unicode = (type == __mtSetProcessNameW || type == __mtSetThreadNameW);
        char data[__SIZEOF_PIPEDATAHEADER];
        *(int*)&data[0] = type;                   // Type
        *(DWORD*)&data[4] = GetCurrentThreadId(); // ThreadID
#ifdef MULTITHREADED_TRACE_ENABLE                 // UniqueThreadID
        *(DWORD*)&data[8] = ThreadCache.GetUniqueThreadId(*(DWORD*)&data[4]);
#else                                                                                      // MULTITHREADED_TRACE_ENABLE
        *(DWORD*)&data[8] = *(DWORD*)&data[4];
#endif                                                                                     // MULTITHREADED_TRACE_ENABLE
        memset(data + 12, 0, 16);                                                          // Time
        *(DWORD*)&data[28] = unicode ? (DWORD)wcslen(nameW) + 1 : (DWORD)strlen(name) + 1; // MessageSize
        *(DWORD*)&data[32] = 0;                                                            // MessageTextOffset
        *(DWORD*)&data[36] = 0;                                                            // Line
        *(double*)&data[40] = 0.0;                                                         // Counter

        if (!WritePipe(data, __SIZEOF_PIPEDATAHEADER) ||
            !WritePipe(unicode ? (void*)nameW : (void*)name, (unicode ? sizeof(WCHAR) : 1) * (*(DWORD*)&data[28])))
        {
            CloseWritePipeAndSemaphore();
        }
    }
}

BOOL C__Trace::SendIgnoreAutoClear(BOOL ignore)
{
    char data[__SIZEOF_PIPEDATAHEADER];
    *(int*)&data[0] = __mtIgnoreAutoClear; // Type
    *(DWORD*)&data[4] = ignore ? 1 : 0;    // ThreadID: 0 = do not ignore, 1 = ignore auto-clear on Trace Server
    return WritePipe(data, __SIZEOF_PIPEDATAHEADER);
}

void C__Trace::SetProcessName(const char* name)
{
    EnterCriticalSection(&CriticalSection);
    DWORD storedLastError = GetLastError();
    SendSetNameMessageToServer(name, NULL, __mtSetProcessName);
    SetLastError(storedLastError);
    LeaveCriticalSection(&CriticalSection);
}

void C__Trace::SetProcessNameW(const WCHAR* name)
{
    EnterCriticalSection(&CriticalSection);
    DWORD storedLastError = GetLastError();
    SendSetNameMessageToServer(NULL, name, __mtSetProcessNameW);
    SetLastError(storedLastError);
    LeaveCriticalSection(&CriticalSection);
}

void C__Trace::SetThreadName(const char* name)
{
    EnterCriticalSection(&CriticalSection);
    DWORD storedLastError = GetLastError();
#ifdef MULTITHREADED_TRACE_ENABLE
    if (ThreadCache.GetUniqueThreadId(GetCurrentThreadId()) != -1) // only with assigned UID, otherwise all "unknown" would be named with this name at once
        SendSetNameMessageToServer(name, NULL, __mtSetThreadName);
#else  // MULTITHREADED_TRACE_ENABLE
    SendSetNameMessageToServer(name, NULL, __mtSetThreadName);
#endif // MULTITHREADED_TRACE_ENABLE
    SetLastError(storedLastError);
    LeaveCriticalSection(&CriticalSection);
}

void C__Trace::SetThreadNameW(const WCHAR* name)
{
    EnterCriticalSection(&CriticalSection);
    DWORD storedLastError = GetLastError();
#ifdef MULTITHREADED_TRACE_ENABLE
    if (ThreadCache.GetUniqueThreadId(GetCurrentThreadId()) != -1) // only with assigned UID, otherwise all "unknown" would be named with this name at once
        SendSetNameMessageToServer(NULL, name, __mtSetThreadNameW);
#else  // MULTITHREADED_TRACE_ENABLE
    SendSetNameMessageToServer(NULL, name, __mtSetThreadNameW);
#endif // MULTITHREADED_TRACE_ENABLE
    SetLastError(storedLastError);
    LeaveCriticalSection(&CriticalSection);
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
    const char* File; // just a reference to static string
    int Line;
};

DWORD WINAPI __TraceMsgBoxThread(void* param)
{
    C__TraceMsgBoxThreadData* data = (C__TraceMsgBoxThreadData*)param;
    try
    {
        std::string msg = "TRACE_C message received!\n\nFile: ";
        msg += data->File != NULL ? data->File : "";
        msg += "\nLine: ";
        msg += std::to_string(data->Line);
        msg += "\n\nMessage: ";
        if (data->Msg != NULL)
            msg += data->Msg;
        msg += "\n\nTRACE_C message means that fatal error has occured. "
               "Application will be crashed by \"access violation\" exception after "
               "clicking OK. Please send us bug report to help us fix this problem. "
               "If you want to copy this message to clipboard, use Ctrl+C key.";
        // The narrow trace stream is decoded by the single diagnostic adapter;
        // no ANSI window is created.
        extern int __MessagesShowA(HWND, const char*, const char*, UINT);
        __MessagesShowA(NULL, msg.c_str(), "Debug Message", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    catch (...)
    {
        extern int __MessagesShowW(HWND, const WCHAR*, const WCHAR*, UINT);
        __MessagesShowW(NULL, L"Unable to allocate the fatal trace diagnostic.",
                        L"Debug Message", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    return 0;
}

struct C__TraceMsgBoxThreadDataW
{
    const WCHAR* Msg;  // owned by SendMessageToServer until the thread joins
    const WCHAR* File; // just a reference to static string
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
        extern int __MessagesShowW(HWND, const WCHAR*, const WCHAR*, UINT);
        __MessagesShowW(NULL, msg.c_str(), L"Debug Message", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    catch (...)
    {
        extern int __MessagesShowW(HWND, const WCHAR*, const WCHAR*, UINT);
        __MessagesShowW(NULL, L"Unable to allocate the fatal trace diagnostic.",
                        L"Debug Message", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    return 0;
}

#if defined(TRACE_TO_FILE) && defined(__TRACESERVER)

DWORD WINAPI __TraceMsgBoxThreadErrInTS(void* param)
{
    // choke point, not ::MessageBoxW.
    extern int __MessagesShowW(HWND, const WCHAR*, const WCHAR*, UINT);
    __MessagesShowW(NULL, (WCHAR*)param, L"Trace Server", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    return 0;
}

#endif // defined(TRACE_TO_FILE) && defined(__TRACESERVER)

C__Trace&
C__Trace::SendMessageToServer(C__MessageType type, BOOL crash)
{
    BOOL unicode = type == __mtInformationW || type == __mtErrorW;
    // flush to buffer
    if (unicode)
        TraceStrStreamW.flush();
    else
        TraceStrStream.flush();

    SYSTEMTIME st;
    GetLocalTime(&st);

    BOOL writePCWarning = FALSE;
    const char* pcWarning = "[Performance Counter BUG detected! Using last good PC value]: ";
    const WCHAR* pcWarningW = L"[Performance Counter BUG detected! Using last good PC value]: ";
    int pcWarningLen;
    double performanceCounterValue;
    DWORD addToMessageSize = 0;
    if (SupportPerformanceFrequency)
    {
        LARGE_INTEGER perfCounter;
        ::QueryPerformanceCounter(&perfCounter);

        static LONGLONG lastPC = 0;
        if (lastPC != 0 && lastPC > perfCounter.QuadPart) // counter must always grow, decrease is an error (on multicore processors this error appears, solution is to set affinity to single core for debugged process in Task Manager)
        {
            perfCounter.QuadPart = lastPC + 1; // artificially increase counter value to last value plus one (just so it does not decrease and does not cause completely wrong ordering in Trace Server)
            pcWarningLen = unicode ? (int)wcslen(pcWarningW) : (int)strlen(pcWarning);
            addToMessageSize = pcWarningLen;
            writePCWarning = TRUE;
        }
        lastPC = perfCounter.QuadPart;

        performanceCounterValue = (double)perfCounter.QuadPart / PerformanceFrequency.QuadPart * 1000.0;
    }
    else
        performanceCounterValue = 0.0;

#ifdef TRACE_TO_FILE
    if (HTraceFile != NULL)
    {
        DWORD wr;
        WCHAR bufW[5000];
        swprintf_s(bufW, unicode ? L"%s\t%d\t" // file name in FileW (unicode)
#ifdef MULTITHREADED_TRACE_ENABLE
                                   L"%d\t"
#endif // MULTITHREADED_TRACE_ENABLE
                                   L"%d.%d.%d\t%d:%02d:%02d.%03d\t%.3lf\t%s\t%d\t"
                                 : L"%s\t%d\t" // file name in File (ANSI)
#ifdef MULTITHREADED_TRACE_ENABLE
                                   L"%d\t"
#endif // MULTITHREADED_TRACE_ENABLE
                                   L"%d.%d.%d\t%d:%02d:%02d.%03d\t%.3lf\t%S\t%d\t",
                   type == __mtInformation || type == __mtInformationW ? L"Info" : L"Error", GetCurrentThreadId(),
#ifdef MULTITHREADED_TRACE_ENABLE
                   ThreadCache.GetUniqueThreadId(GetCurrentThreadId()),
#endif // MULTITHREADED_TRACE_ENABLE
                   st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                   performanceCounterValue, unicode ? (void*)FileW : (void*)File, Line);
        WriteFile(HTraceFile, bufW, sizeof(WCHAR) * (int)wcslen(bufW), &wr, NULL);
        if (writePCWarning)
            WriteFile(HTraceFile, pcWarningW, sizeof(WCHAR) * (int)wcslen(pcWarningW), &wr, NULL);
        if (unicode)
            WriteFile(HTraceFile, TraceStringBufW.c_str(), sizeof(WCHAR) * (int)TraceStringBufW.length(), &wr, NULL);
        else
        {
            if (TraceStringBuf.length() > 0)
            {
                std::wstring traceText;
                if (sally::diagnostic::DecodeAcpLossy(TraceStringBuf.c_str(),
                                                      TraceStringBuf.length(), traceText))
                {
                    WriteFile(HTraceFile, traceText.data(),
                              static_cast<DWORD>(sizeof(WCHAR) * traceText.length()), &wr, NULL);
                }
            }
        }
        WriteFile(HTraceFile, L"\r\n", sizeof(WCHAR) * 2, &wr, NULL);
        FlushFileBuffers(HTraceFile); // flush data to disk

#ifdef __TRACESERVER
        // for Trace Server debugging: TRACE messages go only to file, when TRACE_E arrives, notify with msgbox
        if (!crash && (type == __mtError || type == __mtErrorW))
        {
            const std::wstring message =
                L"Error message from Trace Server has been written to file with traces:\n" +
                TraceFileName;

            // print message in another thread to avoid pumping current thread messages
            DWORD id;
            HANDLE msgBoxThread = CreateThread(NULL, 0, __TraceMsgBoxThreadErrInTS,
                                               const_cast<wchar_t*>(message.c_str()), 0, &id);
            if (msgBoxThread != NULL)
            {
                WaitForSingleObject(msgBoxThread, INFINITE);
                CloseHandle(msgBoxThread);
            }
        }
#endif // __TRACESERVER
    }
#endif // TRACE_TO_FILE

    if (HWritePipe != NULL)
    {
        DWORD fileSize = (DWORD)((unicode ? wcslen(FileW) : strlen(File)) + 1);
        DWORD textSize = (DWORD)((unicode ? TraceStringBufW.length() : TraceStringBuf.length()) + 1);

        char data[__SIZEOF_PIPEDATAHEADER];
        *(int*)&data[0] = type;                   // Type
        *(DWORD*)&data[4] = GetCurrentThreadId(); // ThreadID, UniqueThreadID
#ifdef MULTITHREADED_TRACE_ENABLE
        *(DWORD*)&data[8] = ThreadCache.GetUniqueThreadId(*(DWORD*)&data[4]);
#else                                                                         // MULTITHREADED_TRACE_ENABLE
        *(DWORD*)&data[8] = *(DWORD*)&data[4];
#endif                                                                        // MULTITHREADED_TRACE_ENABLE
        *(SYSTEMTIME*)(data + 12) = st;                                       // Time
        *(DWORD*)&data[28] = (DWORD)(fileSize + textSize + addToMessageSize); // MessageSize
        *(DWORD*)&data[32] = fileSize;                                        // MessageTextOffset
        *(DWORD*)&data[36] = Line;                                            // Line
        *(double*)&data[40] = performanceCounterValue;

        if (!WritePipe(data, __SIZEOF_PIPEDATAHEADER) ||
            !WritePipe(unicode ? (void*)FileW : (void*)File, (DWORD)((unicode ? sizeof(WCHAR) : 1) * fileSize)) ||
            writePCWarning && !WritePipe(unicode ? (void*)pcWarningW : (void*)pcWarning,
                                         (unicode ? sizeof(WCHAR) : 1) * pcWarningLen) || // output PC error at the beginning of the message, during debugging this is quite important (messages are out of real order)
            !WritePipe(unicode ? (void*)TraceStringBufW.c_str() : (void*)TraceStringBuf.c_str(),
                       (unicode ? sizeof(WCHAR) : 1) * textSize))
        {
            CloseWritePipeAndSemaphore();
        }
    }
    // only if crash==TRUE:
    // we create a copy of data, starting thread for msgbox can trigger another TRACE
    // messages (e.g. in DllMain response to DLL_THREAD_ATTACH), if we did not leave
    // CriticalSection, deadlock would occur;
    // TRACE_C must not be used in DllMain, otherwise deadlock will occur:
    //   - if placed in DLL_THREAD_ATTACH: wants to open new thread for msgbox
    //     and that is blocked from DllMain
    //   - if placed in DLL_THREAD_DETACH: while waiting for thread with msgbox to close
    //     we catch TRACE_C from DLL_THREAD_DETACH from previous TRACE_C and let it
    //     wait in infinite loop, see below
    // also we introduce defense against multiplication of msgboxes when multiple TRACE_C at once
    // would just confuse, now only first msgbox opens and after closing it triggers
    // crash, other TRACE_C remain stuck in infinite wait loop, see below
    static BOOL msgBoxOpened = FALSE;
    C__TraceMsgBoxThreadData threadData;
    C__TraceMsgBoxThreadDataW threadDataW;
    std::string threadMessage;
    std::wstring threadMessageW;
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
            // print message in another thread to avoid pumping current thread messages
            DWORD id;
            HANDLE msgBoxThread = CreateThread(NULL, 0, unicode ? __TraceMsgBoxThreadW : __TraceMsgBoxThread,
                                               unicode ? (void*)&threadDataW : (void*)&threadData, 0, &id);
            if (msgBoxThread != NULL)
            {
                WaitForSingleObject(msgBoxThread, INFINITE); // if TRACE_C is placed in DllMain in DLL_THREAD_ATTACH, deadlock will occur - highly unlikely, not handling
                CloseHandle(msgBoxThread);
            }
            msgBoxOpened = FALSE;
            // we trigger the crash directly in the code where TRACE_C/TRACE_MC is placed, so
            // it is visible in bug report exactly where macros are; crash thus follows
            // after this method completes
        }
        else // other threads with TRACE_C we block, until msgbox opened for
        {    // first TRACE_C closes, so it will fall there too, so there is no mess
            if (msgBoxOpened)
            {
                while (1)
                    Sleep(1000); // blocking leads to deadlock e.g. when TRACE_C is (and should not be) in DLL_THREAD_DETACH
            }
        }
    }
    return *this;
}

#endif // TRACE_ENABLE

// trick for our own definition of these "forbidden" operators (so that control
// of forbidden combinations of WCHAR / char strings in one TRACE or MESSAGE macro
// works) - the following operators must not be defined in other modules - otherwise linker
// would not report error - idea: in DEBUG version we catch linker errors, in RELEASE version we catch
// errors of operator definition; to test both things, TRACE_ENABLE must be allowed
// in both DEBUG and RELEASE version, which e.g. Salamander SDK build satisfies;
// the most common model is to have TRACE_ENABLE enabled in DEBUG version and disabled in RELEASE, in this
// case only the first test is performed, which is more important (forbidden combinations of strings
// WCHAR / char))
#ifndef _DEBUG

#include <ostream>

std::ostream& operator<<(std::ostream& out, const wchar_t* str)
{
    return out << (void*)str;
}
std::wostream& operator<<(std::wostream& out, const char* str) { return out << (void*)str; }

#endif // _DEBUG
