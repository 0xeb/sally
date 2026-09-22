// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <string>

// Bounded, always-on ledger of what happened to every registered shell icon-overlay
// handler during InitShellIconOverlays().
//
// Why this exists: the overlay subsystem drops handlers at roughly twenty distinct
// points, and in a Release build not one of them is observable. Most use TRACE_I/TRACE_E,
// which compile to nothing without TRACE_ENABLE (a Debug-only define); several - including
// the disabled-list gate at shiconov.cpp, which is the single most likely cause of issue
// #90 - log nothing in any build at all.
//
// The Configuration > Icon Overlays page cannot substitute for this. It renders
// ListOfShellIconOverlays, which is populated for every registry key *before and
// independently of* the load attempt, with a checkbox bound only to the disabled list. A
// handler that failed CoCreateInstance, GetOverlayInfo, icon extraction or Add() therefore
// appears in that list checked and enabled. The array that actually decides whether an
// overlay renders, ShellIconOverlays.Overlays, is surfaced nowhere in the UI.
//
// Contents are printed by the Bug Report facility (bugreprt.cpp), which does run in
// Release and produces a file the user can attach to a GitHub issue.

// Every way a registered handler can fail to end up rendering. The comments give the
// shiconov.cpp branch each one corresponds to.
enum class OverlayOutcome
{
    NotAttempted = 0,
    Loaded,

    SkippedGloballyDisabled, // Configuration.EnableCustomIconOverlays is FALSE - silent in every build
    SkippedUserDisabled,     // name is in DisabledCustomIconOverlays - silent in every build

    RegKeyOpenFailed,        // RegOpenKeyEx on the handler subkey
    RegValueMissing,         // default value unreadable
    RegValueNotSz,           // default value is not REG_SZ
    InvalidClsid,            // CLSIDFromString rejected it

    CoCreateFailed,          // CoCreateInstance - HRESULT is discarded by the inline == S_OK test today
    GetOverlayInfoFailed,    // NOTE: the test is a strict == S_OK, so S_FALSE lands here too
    NoIconFileFlag,          // GetOverlayInfo succeeded but did not set ISIOI_ICONFILE
    IconExtractFailed,       // one or more of the 16/32/48 sizes did not extract
    ItemAllocFailed,         // new CShellIconOverlayItem returned NULL - silent
    CapReached,              // MAX_SHELL_ICON_OVERLAYS handlers already loaded
    ArrayGrowFailed,         // Overlays.Add() could not grow - silent
};

// One registered handler.
struct ShellOverlayDiagRecord
{
    std::wstring Name;           // registry key name verbatim - leading spaces are significant
    std::wstring Clsid;
    std::wstring IconFile;

    OverlayOutcome Outcome = OverlayOutcome::NotAttempted;
    HRESULT Hr = 0;              // the HRESULT that produced Outcome, when there was one
    DWORD InfoFlags = 0;         // ISIOI_* from GetOverlayInfo
    int IconIndex = 0;
    int Priority = 0;
    int LoadedIndex = -1;        // index in ShellIconOverlays.Overlays, or -1
    unsigned char IconsOk = 0;   // bit 0 = 16px, bit 1 = 32px, bit 2 = 48px

    // Per-panel icon-reader threads create their own instance of each handler (COM STA).
    // A failure here leaves ids[i] NULL so the handler silently matches nothing, which is
    // invisible both in the trace and in the configuration page.
    long ReaderFailures = 0;
    HRESULT ReaderLastHr = 0;
};

// Windows caps overlay handlers well below this; 48 covers any real machine with room to
// show that we are not the ones truncating.
#define SHICONOV_DIAG_CAPACITY 48

// Process-level context, captured once at startup. Answers "which configuration is Sally
// actually reading?" - the question behind the imported-root theory for #90, where a
// config imported from an older Altap Salamander install can carry a disabled-handler list
// or force EnableCustomIconOverlays off outright.
struct ShellOverlayDiagHeader
{
    std::wstring ConfigRoot;       // the resolved SALAMANDER_ROOT_REG
    DWORD ConfigVersion = 0;
    bool ValuePairPresent = false; // were both overlay values actually found in that root?
    bool EnableCustomIconOverlays = false;
    std::wstring DisabledList;
    UINT AnsiCodePage = 0;         // GetACP() - the overlay lookup is bounded by it
    int Registered = 0;
    int Loaded = 0;

    // The three icon sizes Sally demands from every handler. These are DPI-scaled
    // (GetIconSizeForSystemDPI), so at 125% they are 20/40/60 rather than 16/32/48 - and a
    // handler whose icon file cannot yield all three is dropped outright.
    int IconSizes[3] = {0, 0, 0};
    UINT SystemDpi = 0;
};

// Fixed-capacity table. Filled by the main thread during startup; ReaderFailures is bumped
// from the panel icon-reader threads with interlocked increments. Read by the bug-report
// thread. Best-effort by design, so no locking.
class CShellOverlayDiagLog
{
public:
    ShellOverlayDiagHeader Header;

    // Adds a handler as it is discovered in the registry and returns its record, or NULL
    // when the table is full. 'name' is stored verbatim.
    ShellOverlayDiagRecord* Add(const wchar_t* name, const wchar_t* clsid);

    // Finds a previously added record by exact (case-sensitive) name. Used by the icon
    // reader threads, which know only the name.
    ShellOverlayDiagRecord* Find(const wchar_t* name);

    int Count() const { return Filled; }
    const ShellOverlayDiagRecord* At(int index) const;

    void Reset();

private:
    ShellOverlayDiagRecord Records[SHICONOV_DIAG_CAPACITY];
    int Filled = 0;
};

extern CShellOverlayDiagLog ShellOverlayDiag;

// Short stable token for an outcome, e.g. "GetOverlayInfo". Deliberately untranslated: it
// names a Windows API, and it must survive being pasted into a GitHub issue.
const char* OverlayOutcomeText(OverlayOutcome outcome);

// Renders one record as a single ASCII line into 'buf'. Wide names are emitted both as the
// lossy ACP rendering and \uXXXX-escaped when the two differ. Returns characters written.
int FormatShellOverlayDiagRecord(const ShellOverlayDiagRecord& record, char* buf, int bufSize) noexcept;

// Records which configuration root this run resolved. SALAMANDER_ROOT_REG is legitimately
// NULL on a first run (no configuration exists in the registry yet); copying it raw faults,
// so render that state as a legible token instead. The field is only ever printed into the
// Bug Report, so the token also disambiguates "first run" from "the copy failed".
void SetOverlayDiagConfigRoot(ShellOverlayDiagHeader& header, const wchar_t* rootReg);
