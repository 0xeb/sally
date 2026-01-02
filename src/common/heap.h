// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

// Add macro _CRTDBG_MAP_ALLOC to DEBUG version of the project, otherwise the source of the leak is not shown.

#if defined(_DEBUG) && !defined(HEAP_DISABLE)

#define GCHEAP_MAX_USED_MODULES 100 // how many modules at most should be remembered for loading before leak output

// Called for modules in which memory leaks can be reported. If memory leaks are detected,
// modules registered this way are loaded "as image" (without module initialization) and then
// memory leak output occurs (at the time of memory leak check, these modules are already unloaded).
// This way, names of .cpp modules are visible instead of "#File Error#" messages, and at the same time
// MSVC does not get bothered with a bunch of generated exceptions (module names are available).
// Can be called from any thread.
void AddModuleWithPossibleMemoryLeaks(const TCHAR* fileName);

#endif // defined(_DEBUG) && !defined(HEAP_DISABLE)
