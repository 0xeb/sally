// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//
// ****************************************************************************

// TRUE = first running instance of version 3.0 or newer
// determined based on mutex in the global namespace, so it is visible with mutexes
// from other sessions (remote desktop, fast user switching)
extern BOOL FirstInstance_3_or_later;

// shared memory contains:
//  DWORD                  - PID of the process to break
//  DWORD                  - number of items in the list
//  MAX_TL_ITEMS * CTLItem - list of items

#define MAX_TL_ITEMS 500 // maximum number of items in shared memory, cannot be changed!

#define TASKLIST_TODO_HIGHLIGHT 1 // the window of the process given in 'PID' should be highlighted
#define TASKLIST_TODO_BREAK 2     // the process given in 'PID' should be broken
#define TASKLIST_TODO_TERMINATE 3 // the process given in 'PID' should be terminated
#define TASKLIST_TODO_ACTIVATE 4  // the process given in 'PID' should be activated

#define TASKLIST_TODO_TIMEOUT 5000 // 5 seconds for processes to process todo

#define PROCESS_STATE_STARTING 1 // our process is starting, main window does not exist yet
#define PROCESS_STATE_RUNNING 2  // our process is running, we have a main window
#define PROCESS_STATE_ENDING 3   // our process is ending, we no longer have a main window

#pragma pack(push, enter_include_tasklist) // keep structures independent of configured alignment
#pragma pack(4)

extern HANDLE HSalmonProcess;

// WARNING, x64 and x86 processes communicate via this structure, beware of types (e.g. HANDLE) with different sizes
struct CProcessListItem
{
    DWORD PID;            // ProcessID, unique during process lifetime, then can be reused
    SYSTEMTIME StartTime; // when the process started
    DWORD IntegrityLevel; // process Integrity Level, used to distinguish processes running at different privilege levels
    BYTE SID_MD5[16];     // MD5 computed from process SID, used to distinguish processes running under different users; SID has unknown length, hence this workaround
    DWORD ProcessState;   // Salamander state, see PROCESS_STATE_xxx
    UINT64 HMainWindow;   // (x64 friendly) main window handle, if already/still exists (set on create/destroy)
    DWORD SalmonPID;      // ProcessID of Salmon, so the breaking process can grant it the right for SetForegroundWindow

    CProcessListItem()
    {
        PID = GetCurrentProcessId();
        GetLocalTime(&StartTime);
        GetProcessIntegrityLevel(&IntegrityLevel);
        GetSidMD5(SID_MD5);
        ProcessState = PROCESS_STATE_STARTING;
        HMainWindow = NULL;
        SalmonPID = 0;
        if (HSalmonProcess != NULL)
            SalmonPID = SalGetProcessId(HSalmonProcess); // Salmon already running at this time
    }
};

// WARNING, you can only add items to this structure because older versions of Salamander use it too
// WARNING, x64 and x86 processes communicate via this structure, beware of types (e.g. HANDLE) with different sizes
// WARNING, it probably makes no sense to bump the version and extend the structure, because added data
//          will not always be available (older Salamander version started first = new items will not
//          be present in shared memory) => the correct solution is probably changing AS_PROCESSLIST_NAME etc. +
//          reshaping data as needed (increase size, prune, reorder, etc.)
struct CCommandLineParams
{
    DWORD Version;               // newer Salamander versions may increase 'Version' and start using ReservedX variables
    DWORD RequestUID;            // unique (incrementing) ID of activation request
    DWORD RequestTimestamp;      // GetTickCount() value when activation request was created
    char LeftPath[2 * MAX_PATH]; // panel paths (left, right, or active); if empty, they should not be set
    char RightPath[2 * MAX_PATH];
    char ActivePath[2 * MAX_PATH];
    DWORD ActivatePanel;         // which panel to activate 0-none, 1-left, 2-right
    BOOL SetTitlePrefix;         // if TRUE, set title prefix according to TitlePrefix
    char TitlePrefix[MAX_PATH];  // title prefix, if empty do not change; keep length at MAX_PATH instead of TITLE_PREFIX_MAX which could change
    BOOL SetMainWindowIconIndex; // if TRUE, set main window icon according to MainWindowIconIndex
    DWORD MainWindowIconIndex;   // 0: first icon, 1: second icon, ...
    // WARNING, the structure can be extended only if it is still declared as the last member in CProcessList,
    // otherwise it is too late and must not be touched

    CCommandLineParams()
    {
        ZeroMemory(this, sizeof(CCommandLineParams));
    }
};

// Open Salamander Process List
// !!! WARNING: only add items to the structure, because older versions of Salamander also use it
struct CProcessList
{
    DWORD Version; // newer Salamander versions may increase 'Version' and start using ReservedX variables

    DWORD ItemsCount;    // number of valid items in Items array
    DWORD ItemsStateUID; // "version" of Items list; increases with each change; used by Tasks dialog as a refresh signal
    CProcessListItem Items[MAX_TL_ITEMS];

    DWORD Todo;                           // determines what to do after firing event via FireEvent, contains one of TASKLIST_TODO_* values
    DWORD TodoUID;                        // order of sent request, increases for each next request
    DWORD TodoTimestamp;                  // GetTickCount() value when Todo request was created
    DWORD PID;                            // PID for which to perform Todo action
    CCommandLineParams CommandLineParams; // panel paths and other activation parameters
                                          // WARNING, if this structure needs expanding, it would be reasonable to extend CCommandLineParams first, e.g.
                                          // reserve some MAX_PATH buffers and a few DWORDs if we want to pass new command line parameters
};

#pragma pack(pop, enter_include_tasklist)

class CTaskList
{
protected:
    HANDLE FMO;                // file-mapping-object, shared memory
    CProcessList* ProcessList; // pointer into shared memory
    HANDLE FMOMutex;           // mutex for access to FMO
    HANDLE Event;              // event; when signaled, other processes should check
                               // whether they should perform the Todo action
    HANDLE EventProcessed;     // if one process performs the Todo action, it sets this
                               // event to signaled to tell the controlling process it's done
    HANDLE TerminateEvent;     // event to terminate break-thread
    HANDLE ControlThread;      // control-thread (waits for events and handles them immediately)
    BOOL OK;                   // did construction succeed?

public:
    CTaskList();
    ~CTaskList();

    BOOL Init();

    // fills task-list items; items - array of at least MAX_TL_ITEMS CTLItem structs, returns item count
    // 'items' may be NULL if we only care about 'itemsStateUID'
    // returns "version" of process list; version increases with every list change (item added/removed)
    // used by dialog as info to refresh the list; 'itemsStateUID' can be NULL
    // if 'timeouted' is not NULL, sets whether failure was caused by timeout waiting for shared memory
    int GetItems(CProcessListItem* items, DWORD* itemsStateUID, BOOL* timeouted = NULL);

    // asks process 'pid' to perform action per 'todo' (except TASKLIST_TODO_ACTIVATE)
    // if 'timeouted' is not NULL, sets whether failure was caused by timeout waiting for shared memory
    BOOL FireEvent(DWORD todo, DWORD pid, BOOL* timeouted = NULL);

    // if 'timeouted' is not NULL, sets whether failure was caused by timeout waiting for shared memory
    BOOL ActivateRunningInstance(const CCommandLineParams* cmdLineParams, BOOL* timeouted = NULL);

    // finds us in process list and sets 'ProcessState' and 'HMainWindow'; returns TRUE on success, otherwise FALSE
    // if 'timeouted' is not NULL, sets whether failure was caused by timeout waiting for shared memory
    BOOL SetProcessState(DWORD processState, HWND hMainWindow, BOOL* timeouted = NULL);

protected:
    // walks process list and removes non-existing items
    // must be called after successful entry into 'FMOMutex' critical section!
    // sets 'changed' to TRUE if any item was discarded, otherwise FALSE
    BOOL RemoveKilledItems(BOOL* changed);

    friend DWORD WINAPI FControlThread(void* param);
};

extern CTaskList TaskList;

// protection for access to CommandLineParams
extern CRITICAL_SECTION CommandLineParamsCS;
// used to pass activation parameters from Control thread to main thread
extern CCommandLineParams CommandLineParams;
// event is signaled as soon as the main thread takes the parameters
extern HANDLE CommandLineParamsProcessed;
