// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/IRegistry.h"
#include "common/IFileSystem.h"

// Pure decision helper for the issue #82 stale shell-extension cleanup.
//
// Every past Sally/Salamander version registered its shell extension under a sibling CLSID in
// the family {c78b61XX-f3ea-11d2-94a1-00e0292a01e3}, each pointing at its own per-install
// utils\salextx64.dll, salextx86.dll, or salextARM64.dll. After an upgrade those old DLLs stay loaded/locked
// by Explorer, so the previous install folder cannot be deleted. CleanupStaleShellExtensions()
// (sally_entry_lifecycle.cpp) walks the CLSID family and uses this predicate to decide which
// registered DLL paths are stale and safe to reclaim.
//
// Returns true when 'registeredPath' names a Sally shell-extension DLL (basename salextx64.dll,
// salextx86.dll, or salextARM64.dll, case-insensitive), is non-empty, and differs (case-insensitively) from the
// current install's DLL path. The caller additionally confirms the file still exists on disk
// before scheduling it for deletion on reboot. Pure / no I/O, so it is headless-testable.
bool IsStaleSalextRegistration(const wchar_t* currentSalextPath, const wchar_t* registeredPath);

// Outcome of one cleanup sweep. Counters rather than log calls keep this module free of
// Salamander's tracing (it also compiles standalone for tests) and make the walk assertable.
struct SalextCleanupStats
{
    int stale = 0;       // stale registrations whose DLL is still on disk
    int scheduled = 0;   // DLLs successfully scheduled for delete-on-reboot
    int keysRemoved = 0; // stale CLSID keys removed from the registry
};

// Walks the shell-extension CLSID family {c78b61XX-f3ea-11d2-94a1-00e0292a01e3} and reclaims
// every registration IsStaleSalextRegistration() accepts whose DLL still exists: schedules the
// locked DLL for delete-on-reboot and drops the stale CLSID key.
//
// Best-effort. Scheduling needs administrator rights and degrades to a no-op, which is why the
// caller gets counters back instead of a pass/fail. All registry and file access goes through
// the injected interfaces, so the whole sweep is headless-testable with fakes and no call site
// here touches Win32 directly. Passing null for either interface performs no work.
SalextCleanupStats ReclaimStaleSalextRegistrations(const wchar_t* currentSalextPath,
                                                   IRegistry* registry,
                                                   IFileSystem* fileSystem);
