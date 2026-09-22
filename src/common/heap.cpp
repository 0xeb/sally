// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <windows.h>
#include <crtdbg.h>
#include <new>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#if defined(_DEBUG) && !defined(HEAP_DISABLE)

#include "heap.h"
#include "lstrfix.h"
#include "messages.h"

// The order here is important.
// Section names must be 8 characters or less.
// The sections with the same name before the $
// are merged into one section. The order that
// they are merged is determined by sorting
// the characters after the $.
// i_heap and i_heap_end are used to set
// boundaries so we can find the real functions
// that we need to call for initialization.

#pragma warning(disable : 4075) // we want to define the module initialization order

typedef void(__cdecl* _PVFV)(void);

#pragma section(".i_hea$a", read)
__declspec(allocate(".i_hea$a")) const _PVFV i_heap = (_PVFV)1; // at the beginning of section .i_hea we place variable i_heap

#pragma section(".i_hea$z", read)
__declspec(allocate(".i_hea$z")) const _PVFV i_heap_end = (_PVFV)1; // and at the end of section .i_hea we place variable i_heap_end

void Initialize__Heap()
{
    const _PVFV* x = &i_heap;
    for (++x; x < &i_heap_end; ++x)
        if (*x != NULL)
            (*x)();
}

#pragma init_seg(".i_hea$m")

#include "trace.h"

int OurReportingFunction(int reportType, char* userMessage, int* retVal)
{
    const char* rType = "Unknown";

    if (reportType == _CRT_WARN)
        rType = "_CRT_WARN";
    else if (reportType == _CRT_ERROR)
        rType = "_CRT_ERROR";
    else if (reportType == _CRT_ASSERT)
        rType = "_CRT_ASSERT";

    TRACE_E(rType << ": " << userMessage);

    // By setting retVal to zero, we are instructing _CrtDbgReport
    // to continue with normal execution after generating the report.
    // If we wanted _CrtDbgReport to start the debugger, we would set
    // retVal to one.
    *retVal = 0;

    // we'll report some information, but we also
    // want _CrtDbgReport to get called - so we'll return FALSE
    return FALSE;
}

class C__GCHeapInit
{
public:
    C__GCHeapInit()
    {
        // The diagnostic layer's scratch/title owners are process-lifetime by design - they are
        // reachable from late static destructors and are therefore never freed. Construct them
        // now so they fall OUTSIDE the measured window; otherwise whichever one the session
        // happens to touch first lands after the checkpoint and is reported as a leak.
        PreallocateMessagesDiagnosticStorage();

        // save memory state at the beginning
        _CrtMemCheckpoint(&start_state);
        prev_reporting_hook = _CrtSetReportHook(OurReportingFunction);
        InitializeCriticalSection(&CriticalSection);
    }
    ~C__GCHeapInit()
    {
        // The tracker cannot leave its own std::wstring/vector allocations live while taking
        // the final CRT checkpoint or every registered module would look like a leak. Keep only
        // the loaded handles in untracked process-heap storage, then release all text ownership.
        const SIZE_T usedModuleCount = UsedModules.size();
        HMODULE* loadedModules = usedModuleCount <= SIZE_MAX / sizeof(HMODULE)
                                     ? static_cast<HMODULE*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                                       usedModuleCount * sizeof(HMODULE)))
                                     : NULL;
        if (loadedModules != NULL)
        {
            for (SIZE_T i = 0; i < usedModuleCount; ++i)
                loadedModules[i] = LoadLibraryExW(UsedModules[i].Name.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES);
        }
        std::vector<CUsedModule>().swap(UsedModules);

        // get current memory state
        _CrtMemState end_state;
        _CrtMemCheckpoint(&end_state);

        // check if there are any leaks
        _CrtMemState diff;
        if (_CrtMemDifference(&diff, &start_state, &end_state))
        {
            // print all unfreed blocks
            _CrtMemDumpAllObjectsSince(&start_state);

            // since we have the diff, let's also print it
            _CrtMemDumpStatistics(&diff);

            // show warning messagebox
            MSG msg; // remove possibly buffered ESC key (not to close msgbox immediately)
            while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
                ;
            // wide: English-only debug diagnostic, no localized resource involved -
            // MessageBoxA was simply the wrong call, not a LoadStr/resource gap.
            // and via the messages.h choke point, so a headless test reports the
            // leak on stderr instead of hanging forever at exit with nobody to click OK.
            extern int __MessagesShowW(HWND, const WCHAR*, const WCHAR*, UINT);
            __MessagesShowW(NULL, L"Detected memory leaks!", L"Heap Message",
                            MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL);
        }
        if (loadedModules != NULL)
        {
            for (SIZE_T i = 0; i < usedModuleCount; ++i)
            {
                if (loadedModules[i] != NULL)
                    FreeLibrary(loadedModules[i]);
            }
            HeapFree(GetProcessHeap(), 0, loadedModules);
        }
        _CrtSetReportHook(prev_reporting_hook);
        DeleteCriticalSection(&CriticalSection);
    }
    void AddUsedModule(const wchar_t* fileName)
    {
        EnterCriticalSection(&CriticalSection);
        for (const CUsedModule& module : UsedModules)
        {
            if (_wcsicmp(module.Name.c_str(), fileName) == 0)
            {
                LeaveCriticalSection(&CriticalSection);
                return;
            }
        }
        try
        {
            UsedModules.push_back({fileName});
        }
        catch (const std::bad_alloc&)
        {
            TRACE_E("Low memory while registering a module for CRT leak diagnostics.");
        }
        catch (const std::length_error&)
        {
            TRACE_E("Module path is too long for CRT leak diagnostics.");
        }
        LeaveCriticalSection(&CriticalSection);
    }

private:
    struct CUsedModule
    {
        std::wstring Name;
    };

    _CrtMemState start_state;
    _CRT_REPORT_HOOK prev_reporting_hook;
    CRITICAL_SECTION CriticalSection;
    std::vector<CUsedModule> UsedModules;
} __GCHeapCheckLeaks;

void AddModuleWithPossibleMemoryLeaks(const wchar_t* fileName)
{
    __GCHeapCheckLeaks.AddUsedModule(fileName);
}

#endif // defined(_DEBUG) && !defined(HEAP_DISABLE)
