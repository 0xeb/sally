// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <string>

// Bounded, always-on ledger of Sally's last few shell context-menu interactions.
//
// Why this exists rather than TRACE_I: TRACE_ENABLE lives only in SAL_DEBUG_DEFINES
// (cmake/sal_common.cmake), so in the Release build a reporter runs, every TRACE_* in
// shellsup.cpp expands to __TraceEmptyFunction(). Issues #13/#15/#20 reproduce only on
// machines with particular shell extensions installed, so the facts we need have never
// been observable where they actually happen.
//
// Worse, the facts already exist and are thrown away: IContextMenu::InvokeCommand returns
// an HRESULT at three call sites in shellsup.cpp and all three discard it. That is why the
// 2026-07-03 handoff still lists "the HRESULT from InvokeCommand" as unknown after a full
// rewrite attempt. Recording it costs a local variable.
//
// This ledger is deliberately always on and unconditional: a command-line switch would be
// rejected wholesale by ParseCommandLineParameters() on older builds, and a registry flag
// costs a round trip with a reporter. At 8 records it is a few KB of BSS and five field
// writes per right-click, which is not worth gating.
//
// Contents are surfaced by the Bug Report facility (bugreprt.cpp), which does run in
// Release and already produces a file the user can attach to a GitHub issue.

// Which interface Sally decided owned the chosen command. Misattribution here is a live
// suspect for #13: an extension that allocates ids at or above SHELLMENU_NEW_ID_FIRST is
// routed to ContextSubmenuNew, which in the selection case is not assigned, so the command
// is silently dropped.
enum class ShellMenuOwner
{
    Unknown = 0,
    ItemMenu,    // panel->ContextMenu
    NewMenu,     // panel->ContextSubmenuNew
    SallyOwned,  // Sally's own pseudo-commands (paste / paste shortcuts) or a claimed verb
    Cancelled,   // tracking returned 0
};

// One right-click, from menu construction through invocation.
struct ShellMenuDiagRecord
{
    DWORD Tick = 0;                 // GetTickCount() when the menu was built
    std::wstring DirPathW;          // panel path, wide (NOT the lossy GetPathW() mirror)
    int SelCount = 0;               // selected items; 0 means background/directory menu
    bool Background = false;        // right-click on empty panel space
    bool AnyNameNeedsWide = false;  // at least one selected name does not round-trip CP_ACP

    HRESULT QueryContextMenuHr = 0; // discarded today (ShellActionAux5 returns void)
    int MenuItemCount = 0;          // GetMenuItemCount(h) after QueryContextMenu

    DWORD TrackedCmd = 0;           // what the tracker returned
    bool TopLevel = false;          // was that command a direct item of the root menu?
    std::wstring Verb;              // GCS_VERBW result, empty when the lookup failed
    HRESULT VerbHr = 0;

    ShellMenuOwner Owner = ShellMenuOwner::Unknown;
    HRESULT InvokeHr = 0;           // discarded today at shellsup.cpp:831, :852, :873

    bool ParentWasTransient = false;// ici.hwnd was a stack CShellExecuteWnd
    bool ParentAliveAfter = false;  // IsWindow(ici.hwnd) sampled after InvokeCommand returned
};

// Number of interactions retained. Small on purpose: a reporter reproduces a bug in a
// handful of clicks, and the bug report must stay short enough that people actually send it.
#define SHELLMENU_DIAG_CAPACITY 8

// Fixed-capacity ring. No allocation, no locking: the panel context menu is built and
// invoked on the main thread, and the report is best-effort by design.
class CShellMenuDiagLog
{
public:
    // Starts a new record and returns it for the caller to fill in as the interaction
    // proceeds. The returned pointer stays valid until the next Begin().
    ShellMenuDiagRecord* Begin(const wchar_t* dirPathW, int selCount, bool background);

    // The record Begin() handed out, or NULL if none is open. Call sites deep in
    // ShellAction() use this instead of threading a pointer through the Aux* wrappers.
    ShellMenuDiagRecord* Current() { return Open ? &Records[Head] : NULL; }

    void End() { Open = false; }

    // Iteration for the bug report, oldest first.
    int Count() const { return Filled; }
    const ShellMenuDiagRecord* At(int index) const;

    void Reset();

private:
    ShellMenuDiagRecord Records[SHELLMENU_DIAG_CAPACITY];
    int Head = -1;   // index of the most recent record
    int Filled = 0;  // how many slots hold real data
    bool Open = false;
};

// Process-wide instance. Written by the main thread during a right-click, read by the
// bug-report thread; races are acceptable because the report is best-effort.
extern CShellMenuDiagLog ShellMenuDiag;

// Renders 'record' into 'buf' as several ASCII lines separated by '\n'. Wide paths are
// emitted twice - once as the lossy ACP rendering and once \uXXXX-escaped - because the
// bug report file is ANSI (FPrintLine takes const char*) but every issue from the reporters
// who hit these bugs turns on non-ASCII names. Returns the number of characters written.
//
// Pure formatting, so the bug-report section can be exercised headlessly.
int FormatShellMenuDiagRecord(const ShellMenuDiagRecord& record, char* buf, int bufSize) noexcept;

// Appends 'text' to 'buf' with every non-ASCII code unit replaced by \uXXXX. Used by the
// formatter above and by the overlay ledger; exposed for its own unit tests.
std::string ReportAcpBytesW(const wchar_t* text);
std::string AsciiEscapedW(const wchar_t* text);
int AppendAsciiEscapedW(const wchar_t* text, char* buf, int bufSize) noexcept;
