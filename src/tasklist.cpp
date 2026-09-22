// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "mainwnd.h"
#include "tasklist.h"
#include "plugins.h"
#include "common/InstanceNamespace.h"
#include "common/unicode/helpers.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"

#include <new>
#include <utility>
#include <vector>

#pragma warning(disable : 4074)
#pragma init_seg(compiler) // perform initialization as early as possible

#define NOHANDLES(function) function // defense against HANDLES macro contamination in source code via CheckHnd

CTaskList TaskList;

BOOL FirstInstance_3_or_later = FALSE;

// process list is shared across all Salamanders in the local session
// since AS 3.0 we changed the concept of the "Break" event - it raises an exception in the target, so we get a "full-fat" bug report, but at the same time the target ends
// therefore I'm changing the following constants "AltapSalamander*" -> "AltapSalamander3*", so we're separated from older versions

// WARNING: when changing, you need to update salbreak.exe, just send me the info please ... thanks, Petr

const char* FIRST_SALAMANDER_MUTEX_NAME = "SallyFirstInstance";     // introduced since AS 2.52 beta 1
const wchar_t* LOADSAVE_REGISTRY_MUTEX_NAME = SAL_REG_MUTEX_LOADSAVE_W; // introduced since AS 2.52 beta 1

static CRITICAL_SECTION CommandLineParamsCS;
static sally::cmdline::CommandLineRequest* PendingCommandLineRequest = NULL;
static HANDLE CommandLineParamsProcessed;

static BOOL ReadActivationRequest(const CActivationRequestRef& ref,
                                  sally::cmdline::CommandLineRequest& request,
                                  HANDLE* processedEvent)
{
    if (processedEvent == NULL)
        return FALSE;
    *processedEvent = NULL;
    if (ref.StructSize != sizeof(ref) ||
        ref.ProtocolVersion != SALLY_ACTIVATION_PROTOCOL_VERSION ||
        ref.RequestUID == 0 || ref.SenderPID == 0 || ref.Generation == 0 ||
        ref.PayloadBytes < sizeof(CActivationPayloadHeader) || ref.Reserved != 0)
    {
        return FALSE;
    }

    try
    {
        const std::string base =
            sally::instance::BuildSharedObjectNameForCurrentInstance(
                SALLY_ACTIVATION_PAYLOAD_BASE_NAME);
        const std::string name = BuildActivationPayloadName(
            base, ref.SenderPID, ref.RequestUID, ref.Generation);
        const std::string processedBase =
            sally::instance::BuildSharedObjectNameForCurrentInstance(
                SALLY_ACTIVATION_PROCESSED_EVENT_BASE_NAME);
        const std::string processedName = BuildActivationPayloadName(
            processedBase, ref.SenderPID, ref.RequestUID, ref.Generation);
        HANDLE mapping =
            NOHANDLES(OpenFileMappingA(FILE_MAP_READ, FALSE, name.c_str()));
        if (mapping == NULL)
            return FALSE;
        const void* view = NOHANDLES(MapViewOfFile(
            mapping, FILE_MAP_READ, 0, 0, ref.PayloadBytes));
        BOOL ok = FALSE;
        if (view != NULL)
        {
            ok = ParseActivationPayload(view, ref.PayloadBytes, ref.RequestUID,
                                        ref.Generation, ref.SenderPID, request)
                     ? TRUE
                     : FALSE;
            NOHANDLES(UnmapViewOfFile(view));
        }
        NOHANDLES(CloseHandle(mapping));
        if (ok)
        {
            *processedEvent = NOHANDLES(OpenEventA(
                EVENT_MODIFY_STATE, FALSE, processedName.c_str()));
            if (*processedEvent == NULL)
                return FALSE;
            request.requestTimestamp = ref.RequestTimestamp;
        }
        return ok;
    }
    catch (const std::bad_alloc&)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
}

// handle of the main window (it's not good to access MainWindow from control thread, which can be set to NULL under our hands)
HWND HSafeMainWindow = NULL;

void RaiseBreakException()
{
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif                                                   // CALLSTK_DISABLE
    RaiseException(OPENSAL_EXCEPTION_BREAK, 0, 0, NULL); // our own "break" exception
                                                         // code won't reach here anymore
}

//
// ****************************************************************************
// CTaskList
//

DWORD WINAPI FControlThread(void* param)
{
    // this thread is not called with our CCallStack - I encountered with a leaked handle that when trying to
    // dump it (during Salamander shutdown) Salam crashed

    CTaskList* tasklist = (CTaskList*)param;

    SetThreadNameInVC(L"ControlThread");

    HANDLE arr[3];
    arr[0] = tasklist->TerminateEvent;
    arr[1] = tasklist->Event;
    arr[2] = SalShExtDoPasteEvent;

    DWORD lastTodoUID = 0;

    DWORD ourPID = GetCurrentProcessId();

    BOOL loop = TRUE;
    while (loop)
    {
        DWORD waitRet = WaitForMultipleObjects(arr[2] == NULL ? 2 : 3, arr, FALSE, INFINITE);
        switch (waitRet)
        {
        case WAIT_OBJECT_0 + 0: // tasklist->TerminateEvent
        {
            loop = FALSE;
            break;
        }

        case WAIT_OBJECT_0 + 1: // tasklist->Event
        {
            // acquire ProcessList
            waitRet = WaitForSingleObject(tasklist->FMOMutex, TASKLIST_TODO_TIMEOUT);
            if (waitRet == WAIT_FAILED)
                Sleep(50); // so we don't eat CPU
            if (waitRet == WAIT_FAILED || waitRet == WAIT_TIMEOUT)
                break;

            // protection against cycling after command execution
            if (tasklist->ProcessList->TodoUID == lastTodoUID)
            {
                // release ProcessList
                ReleaseMutex(tasklist->FMOMutex);
                Sleep(50); // give opportunity to other processes
                break;
            }
            else
                lastTodoUID = tasklist->ProcessList->TodoUID;

            // we have ProcessList acquired
            DWORD pid = tasklist->ProcessList->PID;
            if (pid != ourPID) // if the event doesn't concern us
            {
                // release ProcessList
                ReleaseMutex(tasklist->FMOMutex);
                Sleep(50); // give opportunity to other processes
                break;
            }

            // now we're running in the process that should receive the message; at the same time we're in a secondary thread, so
            // any communication with the main thread needs to be solved with additional synchronization

            // reset Event, because now we know it belonged to us and it's unnecessary to let control threads of other processes run
            ResetEvent(tasklist->Event);

            // verify from timestamp if we haven't already missed the time we had available to handle the command
            DWORD tickCount = GetTickCount();
            if (tickCount - tasklist->ProcessList->TodoTimestamp >= TASKLIST_TODO_TIMEOUT)
            {
                // TIMEOUT
                // release ProcessList
                ReleaseMutex(tasklist->FMOMutex);
                break;
            }

            // make a copy of the acquired ProcessList
            CProcessList processList;
            memcpy(&processList, tasklist->ProcessList, sizeof(CProcessList));
            // and release shared memory
            ReleaseMutex(tasklist->FMOMutex);

            switch (processList.Todo)
            {
            case TASKLIST_TODO_HIGHLIGHT:
            {
                SetEvent(tasklist->EventProcessed); // message for requester process: we're done
                if (HSafeMainWindow != NULL)
                    PostMessage(HSafeMainWindow, WM_USER_FLASHWINDOW, 0, 0);
                break;
            }

            case TASKLIST_TODO_BREAK:
            {
                SetEvent(tasklist->EventProcessed); // message for requester process: we're done

                RaiseBreakException();
                // code won't reach here anymore

                break;
            }

            case TASKLIST_TODO_TERMINATE:
            {
                SetEvent(tasklist->EventProcessed); // message for requester process: we're done

                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                if (h != NULL)
                {
                    TerminateProcess(h, 666);
                    CloseHandle(h);
                }
                break;
            }

            case TASKLIST_TODO_ACTIVATE:
            {
                sally::cmdline::CommandLineRequest request;
                HANDLE activationProcessed = NULL;
                if (!ReadActivationRequest(processList.ActivationRequest, request,
                                           &activationProcessed))
                    break;

                BOOL published = FALSE;
                NOHANDLES(EnterCriticalSection(&CommandLineParamsCS));
                if (PendingCommandLineRequest != NULL)
                {
                    using std::swap;
                    swap(*PendingCommandLineRequest, request);
                    ResetEvent(CommandLineParamsProcessed);
                    published = TRUE;
                }
                NOHANDLES(LeaveCriticalSection(&CommandLineParamsCS));
                if (!published)
                {
                    NOHANDLES(CloseHandle(activationProcessed));
                    break;
                }

                // If the main thread is idle, poke it so it takes the dynamic request.
                // if it's not in IDLE, it's solving something right now and will handle the message at the moment it enters IDLE (if we wait for it)
                if (HSafeMainWindow != NULL)
                    PostMessage(HSafeMainWindow, WM_USER_WAKEUP_FROM_IDLE, 0, 0);

                // wait 5 seconds for the main thread to respond (don't enter critical section yet, so it can)
                WaitForSingleObject(CommandLineParamsProcessed, TASKLIST_TODO_TIMEOUT);

                // now we can enter the critical section
                NOHANDLES(EnterCriticalSection(&CommandLineParamsCS));
                if (PendingCommandLineRequest != NULL &&
                    PendingCommandLineRequest->requestUID ==
                        processList.ActivationRequest.RequestUID)
                    PendingCommandLineRequest->requestUID = 0;                // disable any further actions by the main thread
                waitRet = WaitForSingleObject(CommandLineParamsProcessed, 0); // ask what's the current state of the event
                if (waitRet == WAIT_OBJECT_0)
                    SetEvent(activationProcessed); // this exact request was accepted
                NOHANDLES(LeaveCriticalSection(&CommandLineParamsCS));
                NOHANDLES(CloseHandle(activationProcessed));
                break;
            }

            default:
            {
                TRACE_E("FControlThread: unknown todo=" << processList.Todo);
                break;
            }
            }
            break;
        }

        case WAIT_OBJECT_0 + 2: // SalShExtDoPasteEvent
        {
            BOOL sleep = TRUE;
            if (SalShExtSharedMemMutex != NULL)
            {
                WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                if (HSafeMainWindow != NULL && SalShExtSharedMemView != NULL &&
                    SalShExtSharedMemView->SalamanderMainWnd == (UINT64)(DWORD_PTR)HSafeMainWindow)
                {
                    ResetEvent(SalShExtDoPasteEvent); // "source" Salamander has been found, further searching is unnecessary
                    sleep = FALSE;
                    PostMessage(HSafeMainWindow, WM_USER_SALSHEXT_PASTE, SalShExtSharedMemView->PostMsgIndex, 0);
                }
                ReleaseMutex(SalShExtSharedMemMutex);
            }
            if (sleep)
                Sleep(50); // give chance to other Salamanders
            break;
        }

        default: // this shouldn't happen
        {
            Sleep(50); // so we don't eat CPU
            break;
        }
        }
    }

    return 0;
}

CTaskList::CTaskList()
{
    // we run in the 'compiler' group, i.e. before ms_init
    OK = FALSE;
    FMO = NULL;
    ProcessList = NULL;
    FMOMutex = NULL;
    Event = NULL;
    EventProcessed = NULL;
    ActivationDispatchMutex = NULL;
    TerminateEvent = NULL;
    ControlThread = NULL;
    // internal synchronization between ControlThread and main thread
    NOHANDLES(InitializeCriticalSection(&CommandLineParamsCS));
    CommandLineParamsProcessed = NULL;
}

BOOL CTaskList::Init()
{
    OK = FALSE;

    if (PendingCommandLineRequest == NULL)
    {
        try
        {
            PendingCommandLineRequest =
                new sally::cmdline::CommandLineRequest;
        }
        catch (const std::bad_alloc&)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
    }

    std::string processListName = sally::instance::BuildSharedObjectNameForCurrentInstance(SALLY_PROCESS_LIST_MAPPING_NAME);
    std::string processListMutexName = sally::instance::BuildSharedObjectNameForCurrentInstance(SALLY_PROCESS_LIST_MUTEX_NAME);
    std::string processListEventName = sally::instance::BuildSharedObjectNameForCurrentInstance(SALLY_PROCESS_LIST_EVENT_NAME);
    std::string processListEventProcessedName = sally::instance::BuildSharedObjectNameForCurrentInstance(SALLY_PROCESS_LIST_PROCESSED_EVENT_NAME);
    std::string activationDispatchMutexName = sally::instance::BuildSharedObjectNameForCurrentInstance(SALLY_ACTIVATION_DISPATCH_MUTEX_NAME);
    std::string firstInstanceMutexBaseName = sally::instance::BuildSharedObjectNameForCurrentInstance(FIRST_SALAMANDER_MUTEX_NAME);

    PSID psidEveryone;
    PACL paclNewDacl;
    SECURITY_ATTRIBUTES sa;
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES* saPtr = CreateAccessableSecurityAttributes(&sa, &sd, GENERIC_ALL, &psidEveryone, &paclNewDacl);

    //---  first a side note: under Vista+ we create an event for communication with copy-hook (it's waited for in control-thread)
    if (WindowsVistaAndLater)
    {
        wchar_t doPasteEventName[256];
        // wide, matching shexreg_ipc_names.h. CreateEventW/OpenEventW rather
        // than the TCHAR macros: this build does not define UNICODE, so the unsuffixed names
        // would resolve to the A forms and take a wchar_t* as if it were char*.
        const wchar_t* shellExtDoPasteEventName = SALSHEXT_GetDoPasteEventName(doPasteEventName, _countof(doPasteEventName));
        SalShExtDoPasteEvent = NOHANDLES(CreateEventW(saPtr, TRUE, FALSE, shellExtDoPasteEventName));
        if (SalShExtDoPasteEvent == NULL)
            SalShExtDoPasteEvent = NOHANDLES(OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, shellExtDoPasteEventName));
        if (SalShExtDoPasteEvent == NULL)
            TRACE_E("CTaskList::Init(): unable to create event object for communicating with copy-hook shell extension!");
    }

    //---  try to attach to FMO-mutex - at the same time test if some Salamander is already running
    // processList*Name are std::string (narrow) - explicit A forms rather than the TCHAR macros:
    // this build does not define UNICODE, so the unsuffixed names would resolve to the A forms
    // anyway, but under the msvc-unicode-canary probe they'd resolve to W forms taking wchar_t*.
    FMOMutex = NOHANDLES(OpenMutexA(SYNCHRONIZE, FALSE, processListMutexName.c_str()));
    if (FMOMutex == NULL) // we're the first Salamander 3.0 or newer in the local session
    {
        //---  creation of system objects for communication, acquire FMO
        FMOMutex = NOHANDLES(CreateMutexA(saPtr, TRUE, processListMutexName.c_str())); // task list is valid only for the given session, mutex belongs to local namespace
        if (FMOMutex == NULL)
            return FALSE; // fail
        FMO = NOHANDLES(CreateFileMappingA(INVALID_HANDLE_VALUE, saPtr, PAGE_READWRITE | SEC_COMMIT,
                                           0, sizeof(CProcessList), processListName.c_str()));
        if (FMO == NULL)
            return FALSE; // fail
        ProcessList = (CProcessList*)NOHANDLES(MapViewOfFile(FMO, FILE_MAP_WRITE, 0, 0, 0));
        if (ProcessList == NULL)
            return FALSE; // fail
        Event = NOHANDLES(CreateEventA(saPtr, TRUE, FALSE, processListEventName.c_str()));
        if (Event == NULL)
            return FALSE; // fail
        EventProcessed = NOHANDLES(CreateEventA(saPtr, TRUE, FALSE, processListEventProcessedName.c_str()));
        if (EventProcessed == NULL)
            return FALSE; // fail

        //---  initialization of shared memory
        ZeroMemory(ProcessList, sizeof(CProcessList));
        ProcessList->Magic = SALLY_PROCESS_LIST_PROTOCOL_MAGIC;
        ProcessList->Version = SALLY_PROCESS_LIST_PROTOCOL_VERSION;
        ProcessList->StructSize = sizeof(CProcessList);

        ProcessList->ItemsCount = 1;
        ProcessList->ItemsStateUID++;
        ProcessList->Items[0] = CProcessListItem();

        //---  release FMO
        ReleaseMutex(FMOMutex);
    }
    else // another instance, just attach ...
    {
        //---  acquire FMO
        DWORD waitRet = WaitForSingleObject(FMOMutex, TASKLIST_TODO_TIMEOUT);
        if (waitRet == WAIT_TIMEOUT)
            return FALSE; // fail

        //---  attach to other system objects for communication
        FMO = NOHANDLES(OpenFileMappingA(FILE_MAP_WRITE, FALSE, processListName.c_str()));
        if (FMO == NULL)
        {
            ReleaseMutex(FMOMutex);
            return FALSE; // fail
        }
        ProcessList = (CProcessList*)NOHANDLES(MapViewOfFile(FMO, FILE_MAP_WRITE, 0, 0, 0));
        if (ProcessList == NULL)
        {
            ReleaseMutex(FMOMutex);
            return FALSE; // fail
        }
        if (ProcessList->Magic != SALLY_PROCESS_LIST_PROTOCOL_MAGIC ||
            ProcessList->Version != SALLY_PROCESS_LIST_PROTOCOL_VERSION ||
            ProcessList->StructSize != sizeof(CProcessList) ||
            ProcessList->ItemsCount > MAX_TL_ITEMS)
        {
            ReleaseMutex(FMOMutex);
            return FALSE; // fail closed; this process continues as a separate instance
        }
        // to be able to call SetEvent() on event, it must have EVENT_MODIFY_STATE set, for Wait* it needs SYNCHRONIZE
        Event = NOHANDLES(OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, processListEventName.c_str()));
        if (Event == NULL)
        {
            ReleaseMutex(FMOMutex);
            return FALSE; // fail
        }
        EventProcessed = NOHANDLES(OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, processListEventProcessedName.c_str()));
        if (EventProcessed == NULL)
        {
            ReleaseMutex(FMOMutex);
            return FALSE; // fail
        }

        //---  add entry to shared memory
        BOOL attempt = 0;
    AGAIN:
        int c = ProcessList->ItemsCount;
        if (c < MAX_TL_ITEMS) // if there aren't too many, add this process
        {
            ProcessList->ItemsCount++;
            ProcessList->ItemsStateUID++;
            ProcessList->Items[c] = CProcessListItem();
        }
        else
        {
            if (attempt == 0)
            {
                // array is full, try to shake it (some process might have died and didn't let us know)
                RemoveKilledItems(NULL);
                attempt++;
                goto AGAIN;
            }
        }

        //---  release FMO
        ReleaseMutex(FMOMutex);
    }

    // detection of other Salamander instances
    LPWSTR sid = NULL;
    if (!GetStringSid(&sid))
        sid = NULL;
    std::wstring firstInstanceMutexBaseNameW;
    if (!sally::instance::WidenSharedObjectName(firstInstanceMutexBaseName,
                                                 firstInstanceMutexBaseNameW))
    {
        if (sid != NULL)
            LocalFree(sid);
        return FALSE;
    }
    std::wstring mutexName;
    try
    {
        if (sid == NULL)
        {
            // error getting SID -- local name space, without attached SID
            mutexName = firstInstanceMutexBaseNameW;
        }
        else
        {
            mutexName = L"Global\\";
            mutexName += firstInstanceMutexBaseNameW;
            mutexName += L'_';
            mutexName += sid;
            LocalFree(sid);
            sid = NULL;
        }
    }
    catch (const std::bad_alloc&)
    {
        if (sid != NULL)
            LocalFree(sid);
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    HANDLE hMutex = NOHANDLES(CreateMutexW(saPtr, FALSE, mutexName.c_str()));
    DWORD lastError = GetLastError();
    if (hMutex != NULL)
    {
        FirstInstance_3_or_later = (lastError != ERROR_ALREADY_EXISTS);
    }
    else
    {
        hMutex = NOHANDLES(OpenMutexW(SYNCHRONIZE, FALSE, mutexName.c_str()));
        lastError = GetLastError();
        if (hMutex != NULL)
            FirstInstance_3_or_later = FALSE;
    }

    ActivationDispatchMutex = NOHANDLES(CreateMutexA(
        saPtr, FALSE, activationDispatchMutexName.c_str()));
    if (ActivationDispatchMutex == NULL)
    {
        if (psidEveryone != NULL)
            FreeSid(psidEveryone);
        if (paclNewDacl != NULL)
            LocalFree(paclNewDacl);
        return FALSE;
    }

    if (psidEveryone != NULL)
        FreeSid(psidEveryone);
    if (paclNewDacl != NULL)
        LocalFree(paclNewDacl);

    TerminateEvent = NOHANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));
    if (TerminateEvent == NULL)
        return FALSE; // fail

    // internal synchronization between ControlThread and main thread
    CommandLineParamsProcessed = CreateEvent(NULL, TRUE, FALSE, NULL); // manual, nonsignaled
    if (CommandLineParamsProcessed == NULL)
        return FALSE; // failed

    // can't use _beginthreadex, because the library might not be initialized yet
    DWORD id;
    ControlThread = NOHANDLES(CreateThread(NULL, 0, FControlThread, this, 0, &id));
    if (ControlThread == NULL)
        return FALSE; // fail
    // this thread must get to run even if there's nothing left ...
    SetThreadPriority(ControlThread, THREAD_PRIORITY_TIME_CRITICAL);

    OK = TRUE;
    return TRUE;
}

CTaskList::~CTaskList()
{
    if (ControlThread != NULL)
    {
        SetEvent(TerminateEvent);                     // terminate!
        WaitForSingleObject(ControlThread, INFINITE); // wait for thread to finish
        NOHANDLES(CloseHandle(ControlThread));
    }
    if (TerminateEvent != NULL)
        NOHANDLES(CloseHandle(TerminateEvent));

    // remove ourselves from the list
    if (OK)
    {
        //---  acquire FMO
        if (WaitForSingleObject(FMOMutex, TASKLIST_TODO_TIMEOUT) != WAIT_TIMEOUT)
        {
            CProcessListItem* ptr = ProcessList->Items;
            int c = ProcessList->ItemsCount;

            //---  throw out current process, it's terminating ...
            DWORD PID = GetCurrentProcessId();
            int i;
            for (i = 0; i < c; i++)
            {
                if (PID == ptr[i].PID)
                {
                    //---  kick process out of the list
                    memmove(ptr + i, ptr + i + 1, (c - i - 1) * sizeof(CProcessListItem));
                    c--;
                    i--;
                }
            }
            ProcessList->ItemsCount = c;
            ProcessList->ItemsStateUID++;

            //---  release FMO
            ReleaseMutex(FMOMutex);
        }
    }

    if (ProcessList != NULL)
        NOHANDLES(UnmapViewOfFile(ProcessList));
    if (FMO != NULL)
        NOHANDLES(CloseHandle(FMO));
    if (FMOMutex != NULL)
        NOHANDLES(CloseHandle(FMOMutex));
    if (Event != NULL)
        NOHANDLES(CloseHandle(Event));
    if (EventProcessed != NULL)
        NOHANDLES(CloseHandle(EventProcessed));
    if (ActivationDispatchMutex != NULL)
        NOHANDLES(CloseHandle(ActivationDispatchMutex));
    if (CommandLineParamsProcessed != NULL)
        NOHANDLES(CloseHandle(CommandLineParamsProcessed));
    delete PendingCommandLineRequest;
    PendingCommandLineRequest = NULL;
    NOHANDLES(DeleteCriticalSection(&CommandLineParamsCS));

    if (SalShExtDoPasteEvent != NULL)
        NOHANDLES(CloseHandle(SalShExtDoPasteEvent));
    SalShExtDoPasteEvent = NULL;
}

BOOL CTaskList::SetProcessState(DWORD processState, HWND hMainWindow, BOOL* timeouted)
{
    if (timeouted != NULL)
        *timeouted = FALSE;

    HSafeMainWindow = hMainWindow;

    if (OK)
    {
        DWORD ret = WaitForSingleObject(FMOMutex, TASKLIST_TODO_TIMEOUT);
        if (ret != WAIT_FAILED && ret != WAIT_TIMEOUT)
        {
            // find ourselves in the process list and set processState and hMainWindow
            CProcessListItem* ptr = ProcessList->Items;
            int c = ProcessList->ItemsCount;
            DWORD PID = GetCurrentProcessId();
            int i;
            for (i = 0; i < c; i++)
            {
                if (PID == ptr[i].PID)
                {
                    ptr[i].ProcessState = processState;
                    ptr[i].HMainWindow = (UINT64)(DWORD_PTR)hMainWindow; // 64-bit for x64/x86 compatibility
                    break;
                }
            }
            ReleaseMutex(FMOMutex);
            return TRUE;
        }
        else
        {
            if (timeouted != NULL)
                *timeouted = TRUE;
            TRACE_E("SetProcessState(): WaitForSingleObject failed!");
        }
    }
    return FALSE;
}

int CTaskList::GetItems(CProcessListItem* items, DWORD* itemsStateUID, BOOL* timeouted)
{
    if (timeouted != NULL)
        *timeouted = FALSE;
    if (OK)
    {
        BOOL changed = FALSE;
        //---  acquire FMO
        if (WaitForSingleObject(FMOMutex, TASKLIST_TODO_TIMEOUT) == WAIT_TIMEOUT)
        {
            if (timeouted != NULL)
                *timeouted = TRUE;
            return 0; // fail
        }

        CProcessListItem* ptr = ProcessList->Items;

        //---  throw out killed processes
        RemoveKilledItems(&changed);

        //---  return values
        if (items != NULL)
            memcpy(items, ptr, ProcessList->ItemsCount * sizeof(CProcessListItem));
        if (changed)
            ProcessList->ItemsStateUID++;
        if (itemsStateUID != NULL)
            *itemsStateUID = ProcessList->ItemsStateUID;

        int count = ProcessList->ItemsCount;
        //---  release FMO
        ReleaseMutex(FMOMutex);
        return count;
    }
    else
        return 0;
}

BOOL CTaskList::FireEvent(DWORD todo, DWORD pid, BOOL* timeouted)
{
    if (timeouted != NULL)
        *timeouted = FALSE;
    if (OK)
    {
        // acquire ProcessList
        DWORD waitRet = WaitForSingleObject(FMOMutex, 2000);
        if (waitRet == WAIT_FAILED)
            return FALSE;
        if (waitRet == WAIT_TIMEOUT)
        {
            if (timeouted != NULL)
                *timeouted = TRUE;
            return FALSE; // fail
        }

        // set parameters to be passed
        ProcessList->Todo = todo;
        ProcessList->TodoUID++;
        ProcessList->TodoTimestamp = GetTickCount();
        ProcessList->PID = pid;

        // when breaking another Salamander instance, allow its Salmon to go on top of us
        if (todo == TASKLIST_TODO_BREAK)
        {
            for (DWORD i = 0; i < ProcessList->ItemsCount; i++)
            {
                if (ProcessList->Items[i].PID == pid)
                {
                    AllowSetForegroundWindow(ProcessList->Items[i].PID);       // better allow even own Salamander, even though it's probably unnecessary...
                    AllowSetForegroundWindow(ProcessList->Items[i].SalmonPID); // definitely must allow its Salmon to go on top of us
                    break;
                }
            }
        }

        // release ProcessList
        ReleaseMutex(FMOMutex);

        // start check in all Salamanders
        ResetEvent(EventProcessed);
        SetEvent(Event);

        //---  give a moment to react (during this time someone should "catch" it and fulfill the task)
        BOOL ret = (WaitForSingleObject(EventProcessed, 1000) == WAIT_OBJECT_0);

        //---  tell all Salamanders to prepare for next command
        ResetEvent(Event);

        //---  set back break-PID
        //    ProcessList->Todo = 0;
        //    ProcessList->PID = 0;

        //---  release FMO

        return ret;
    }
    return FALSE;
}

BOOL CTaskList::ActivateRunningInstance(
    const sally::cmdline::CommandLineRequest* request, BOOL* timeouted)
{
    if (timeouted != NULL)
        *timeouted = FALSE;

    if (!OK)
        return FALSE;

    DWORD dispatchWait = WaitForSingleObject(ActivationDispatchMutex,
                                             TASKLIST_TODO_TIMEOUT);
    if (dispatchWait == WAIT_TIMEOUT || dispatchWait == WAIT_FAILED)
    {
        if (timeouted != NULL)
            *timeouted = dispatchWait == WAIT_TIMEOUT;
        return FALSE;
    }
    struct CReleaseActivationMutex
    {
        HANDLE Mutex;
        ~CReleaseActivationMutex() { ReleaseMutex(Mutex); }
    } releaseActivationMutex{ActivationDispatchMutex};

    CProcessListItem ourProcessInfo;

    // find running process in our class, or starting one (which we wait for a moment to see if it starts running)
    int firstStarting = -1; // index of process that's from our class (same Integrity Level and SID) but doesn't have main window yet
    int firstRunnig = -1;   // index of process that's from our class (same Integrity Level and SID) and is already running (has main window)
    DWORD timeStamp = GetTickCount();
    do
    {
        firstStarting = -1;
        firstRunnig = -1;
        DWORD ret = WaitForSingleObject(FMOMutex, 200);
        if (ret == WAIT_FAILED)
            return FALSE;
        if (ret != WAIT_TIMEOUT) // we obtained mutex
        {
            int i;
            for (i = 0; i < (int)ProcessList->ItemsCount; i++)
            {
                CProcessListItem* item = &ProcessList->Items[i];
                // looking for processes only in our class (same IntegrityLevel and SID)
                if (item->PID != ourProcessInfo.PID &&
                    item->IntegrityLevel == ourProcessInfo.IntegrityLevel &&
                    memcmp(item->SID_MD5, ourProcessInfo.SID_MD5, 16) == 0)
                {
                    if (item->ProcessState == PROCESS_STATE_RUNNING)
                    {
                        firstRunnig = i;
                        break; // if we found running instance, we don't need to search for starting one anymore
                    }
                    if (item->ProcessState == PROCESS_STATE_STARTING && firstStarting == -1)
                        firstStarting = i;
                }
            }

            if (firstRunnig == -1) // no process from our class has main window yet
            {
                ReleaseMutex(FMOMutex); // so release memory to others
                if (firstStarting == -1)
                    return FALSE; // we didn't find any starting candidate, exit
                else
                    Sleep(200); // we found starting candidate, sleep for 200ms to give it chance to call SetProcessState()
            }
        }
    } while (firstRunnig == -1 && (GetTickCount() - timeStamp < TASKLIST_TODO_TIMEOUT)); // we wait for running instance maximum 5s

    // if we didn't find any instance from our class that should have main window, or if waiting took 5s, we quit
    if (firstRunnig == -1)
        return FALSE;

    CProcessListItem* item = &ProcessList->Items[firstRunnig];

    DWORD requestUID = ProcessList->TodoUID + 1;
    if (requestUID == 0)
        requestUID = 1;
    DWORD generation = requestUID ^ GetCurrentProcessId() ^ GetTickCount();
    if (generation == 0)
        generation = 1;
    std::vector<BYTE> payload;
    if (request == NULL ||
        !BuildActivationPayload(*request, requestUID, generation,
                                GetCurrentProcessId(), payload))
    {
        ReleaseMutex(FMOMutex);
        return FALSE;
    }
    std::string payloadName;
    std::string processedName;
    try
    {
        const std::string payloadBase =
            sally::instance::BuildSharedObjectNameForCurrentInstance(
                SALLY_ACTIVATION_PAYLOAD_BASE_NAME);
        payloadName = BuildActivationPayloadName(
            payloadBase, GetCurrentProcessId(), requestUID, generation);
        const std::string processedBase =
            sally::instance::BuildSharedObjectNameForCurrentInstance(
                SALLY_ACTIVATION_PROCESSED_EVENT_BASE_NAME);
        processedName = BuildActivationPayloadName(
            processedBase, GetCurrentProcessId(), requestUID, generation);
    }
    catch (const std::bad_alloc&)
    {
        TRACE_E(LOW_MEMORY);
        ReleaseMutex(FMOMutex);
        return FALSE;
    }
    HANDLE activationProcessed = NOHANDLES(CreateEventA(
        NULL, TRUE, FALSE, processedName.c_str()));
    if (activationProcessed == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (activationProcessed != NULL)
            NOHANDLES(CloseHandle(activationProcessed));
        ReleaseMutex(FMOMutex);
        return FALSE;
    }
    HANDLE payloadMapping = NOHANDLES(CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, 0,
        static_cast<DWORD>(payload.size()), payloadName.c_str()));
    if (payloadMapping == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (payloadMapping != NULL)
            NOHANDLES(CloseHandle(payloadMapping));
        NOHANDLES(CloseHandle(activationProcessed));
        ReleaseMutex(FMOMutex);
        return FALSE;
    }
    void* payloadView = NOHANDLES(MapViewOfFile(
        payloadMapping, FILE_MAP_WRITE, 0, 0, payload.size()));
    if (payloadView == NULL)
    {
        NOHANDLES(CloseHandle(payloadMapping));
        NOHANDLES(CloseHandle(activationProcessed));
        ReleaseMutex(FMOMutex);
        return FALSE;
    }
    memcpy(payloadView, payload.data(), payload.size());
    NOHANDLES(UnmapViewOfFile(payloadView));

    // Set fixed control metadata only. The producer keeps the payload mapping
    // alive until acknowledgement or timeout.
    ProcessList->Todo = TASKLIST_TODO_ACTIVATE;
    ProcessList->TodoUID = requestUID; // tell processes that new command will be processed
    ProcessList->TodoTimestamp = GetTickCount();
    ProcessList->PID = item->PID;
    CActivationRequestRef& ref = ProcessList->ActivationRequest;
    ZeroMemory(&ref, sizeof(ref));
    ref.StructSize = sizeof(ref);
    ref.ProtocolVersion = SALLY_ACTIVATION_PROTOCOL_VERSION;
    ref.RequestUID = requestUID;
    ref.RequestTimestamp = ProcessList->TodoTimestamp;
    ref.SenderPID = GetCurrentProcessId();
    ref.Generation = generation;
    ref.PayloadBytes = static_cast<DWORD>(payload.size());

    // allow activated process to call SetForegroundWindow, otherwise it won't be able to pull itself up
    AllowSetForegroundWindow(item->PID);

    // Start the check in all Sally processes and release shared memory.
    ReleaseMutex(FMOMutex);

    SetEvent(Event);

    // give a moment to react (during this time someone should "catch" it and fulfill the task)
    // 500ms is our reserve, to safely cover subordinate threads
    BOOL ret = (WaitForSingleObject(activationProcessed,
                                    TASKLIST_TODO_TIMEOUT + 500) == WAIT_OBJECT_0);

    NOHANDLES(CloseHandle(payloadMapping));
    NOHANDLES(CloseHandle(activationProcessed));

    // tell all Salamanders to prepare for next command (also reset in control thread, if some process is doing todo)
    ResetEvent(Event);

    // zero out todo
    // ProcessList->Todo = 0; // we should first acquire FMOMutex, but in this case there's nothing to break and we can zero values
    // ProcessList->PID = 0;

    return ret;
}

void CTaskList::ReleasePendingRequest()
{
    // The control thread may still be running and publishing into this owner, so take the same
    // lock it does. Every reader already re-checks the pointer inside the section, so dropping it
    // here only makes further activation requests decline to publish - which is correct once the
    // main thread has stopped consuming them.
    NOHANDLES(EnterCriticalSection(&CommandLineParamsCS));
    sally::cmdline::CommandLineRequest* released = PendingCommandLineRequest;
    PendingCommandLineRequest = NULL;
    NOHANDLES(LeaveCriticalSection(&CommandLineParamsCS));
    delete released;
}

BOOL CTaskList::TakePendingActivationRequest(
    DWORD lastRequestUID, sally::cmdline::CommandLineRequest& request)
{
    BOOL accepted = FALSE;
    NOHANDLES(EnterCriticalSection(&CommandLineParamsCS));
    if (PendingCommandLineRequest != NULL &&
        PendingCommandLineRequest->requestUID != lastRequestUID &&
        PendingCommandLineRequest->requestUID != 0 &&
        GetTickCount() - PendingCommandLineRequest->requestTimestamp <
            TASKLIST_TODO_TIMEOUT)
    {
        try
        {
            sally::cmdline::CommandLineRequest copied =
                *PendingCommandLineRequest;
            request = std::move(copied);
            accepted = TRUE;
            SetEvent(CommandLineParamsProcessed);
        }
        catch (const std::bad_alloc&)
        {
            TRACE_E(LOW_MEMORY);
        }
    }
    NOHANDLES(LeaveCriticalSection(&CommandLineParamsCS));
    return accepted;
}

BOOL CTaskList::RemoveKilledItems(BOOL* changed)
{
    if (!OK)
        return FALSE;

    if (changed != NULL)
        *changed = FALSE;
    CProcessListItem* ptr = ProcessList->Items;
    int c = ProcessList->ItemsCount;

    int i;
    for (i = 0; i < c; i++)
    {
        HANDLE h = NOHANDLES(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, ptr[i].PID));
        if (h != NULL)
        {
            // on older Windows we get handle even for terminated process
            // therefore it's necessary to still ask for exitcode; from W2K probably unnecessary
            BOOL cont = FALSE;
            DWORD exitcode;
            if (!GetExitCodeProcess(h, &exitcode) || exitcode == STILL_ACTIVE)
                cont = TRUE;
            NOHANDLES(CloseHandle(h));
            if (cont)
                continue; // leave process in the list
        }
        else
        {
            DWORD lastError = GetLastError();
            if (lastError == ERROR_ACCESS_DENIED)
            {
                continue; // leave process in the list
            }
        }
        memmove(ptr + i, ptr + i + 1, (c - i - 1) * sizeof(CProcessListItem));
        c--;
        i--;
        if (changed != NULL)
            *changed = TRUE;
    }
    ProcessList->ItemsCount = c;

    /*
// doesn't work under XP if processes within one session are run under different users
// we don't have the right to open handle of another process
//---  throw out killed processes
int i;
    for (i = 0; i < c; i++)
    {
      HANDLE h = NOHANDLES(OpenProcess(PROCESS_TERMINATE, FALSE, ptr[i].PID));
      if (h != NULL)
      {
        BOOL cont = FALSE;
        DWORD exitcode;
        if (!GetExitCodeProcess(h, &exitcode) || exitcode == STILL_ACTIVE) cont = TRUE;
        NOHANDLES(CloseHandle(h));
        if (cont) continue;  // leave process in the list
      }
//---  kick process out of the list
      memmove(ptr + i, ptr + i + 1, (c - i - 1) * sizeof(CTLItem));
      c--;
      i--;
    }
    ((DWORD *)SharedMem)[0] = c;   // items-count
    memcpy(items, ptr, c * sizeof(CTLItem));
*/

    return TRUE;
}
