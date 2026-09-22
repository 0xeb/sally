// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The device directory walk, with the filesystem injected so it can run headlessly.
//
// Third and last of the prerequisites the KB set out for making wmobile's wide conversion safe.
// The first two covered path composition and the top-index cache; this one covers the algorithm
// that produces the FILE LIST which delete and copy then act on. That is the piece where a bug
// stops being cosmetic: a walk that returns the wrong relative path deletes the wrong file.
//
// WHY INJECT RATHER THAN FAKE rapi.dll. CDynRapi is declared inside rapi.cpp, so reaching its
// function pointers from a test means either moving the class out or compiling rapi.cpp against
// precomp.h, the whole Salamander SDK and its globals. Injecting at the ENUMERATOR level gets the
// same coverage of the logic that matters, with no SDK in the test and no production restructuring
// beyond this extraction. The plugin supplies a RAPI-backed enumerator; tests supply an in-memory
// one.
//
// WHAT THIS DELIBERATELY LEAVES BEHIND. Error reporting and the low-memory bail-out stay in
// rapi.cpp - they are SDK calls and UI, not algorithm. The walk reports failure and the caller
// decides how to say so.
//
// THERE IS NO CANCEL PROMPT, HERE OR IN rapi.cpp, AND THERE WAS NONE BEFORE. The predecessor
// recursion polled GetSafeWaitWindowClosePressed() and offered IDS_YESNO_CANCEL, under its own
// author's comment "JR TODO: This does not work!" - the wait window is not up in that call path,
// so the poll never fired. The extraction dropped a guard that had never run; a long device scan
// was uncancellable then and is uncancellable now. Wiring real cancellation means an abort
// callback threaded through WalkDeviceTree, which is a feature, not a port.
//
// THE LIMITATION THIS EXISTS TO REMOVE. Today's recursion carries its directory path in a NARROW
// buffer, so a directory whose name is not representable in the machine's ANSI code page is listed
// but never descended into - its contents are invisible to listing, copy and delete alike. The
// walk below is wide throughout, and DescendsIntoNamesOutsideTheAnsiCodePage in the test pins that.

#pragma once

#include <string>
#include <vector>

namespace wmobile
{

// One entry as the device reports it. Mirrors the fields the plugin reads out of CE_FIND_DATA.
struct DeviceEntry
{
    std::wstring name;      // the leaf name, exactly as the device holds it (CE is Unicode-native)
    bool isDirectory = false;
    unsigned long sizeLow = 0;
    // The device's REAL attributes. The caller needs them to clear read-only /
    // hidden / system before removing a source item on a Move, and to raise the
    // hidden/system delete confirmations; substituting a synthetic
    // DIRECTORY/NORMAL pair silently disabled both.
    unsigned long attributes = 0;
};

// The device, reduced to the one operation the walk needs.
class DeviceEnumerator
{
public:
    virtual ~DeviceEnumerator() = default;

    // Lists 'searchPath' (a full path ending in a wildcard, as FindFirstFile takes). Returns false
    // only on a REAL error; an empty directory is success with no entries, which is how the RAPI
    // implementation must also report ERROR_NO_MORE_FILES and ERROR_FILE_NOT_FOUND.
    virtual bool Enumerate(const wchar_t* searchPath, std::vector<DeviceEntry>& out) = 0;
};

// One item the walk found, named RELATIVE to the root it was given.
struct FoundItem
{
    std::wstring relativePath;
    bool isDirectory = false;
    unsigned long sizeLow = 0;
    unsigned long attributes = 0;
    // True for the copy of a directory recorded BEFORE its contents. A directory
    // is listed twice under directoriesFirst: the pre-order entry tells the
    // caller to CREATE the target, the post-order one tells it to REMOVE the
    // source once the contents are done. Emitting only the first left every
    // source directory behind on a Move.
    bool isPreOrderMarker = false;
};

struct WalkOptions
{
    // When true a directory is recorded BEFORE its contents; when false, after. The plugin uses
    // the first for copy (create the directory, then fill it) and the second for delete (empty it,
    // then remove it). Getting this backwards deletes a directory that still has files in it.
    bool directoriesFirst = false;

    // Guards against a device reporting a cycle. A nonzero maxPathChars is available to callers
    // that must project into a genuine external fixed record; ordinary walks are uncapped.
    int maxDepth = 64;
    size_t maxPathChars = 0;
};

// Walks 'root' and appends everything matching 'pattern' at the top level - and everything beneath
// any directory it meets - to 'found'. Returns false if the enumerator reported an error or a
// limit in 'options' was hit; 'found' may already hold entries in that case, and the caller is
// expected to discard them.
bool WalkDeviceTree(DeviceEnumerator& device,
                    const wchar_t* root,
                    const wchar_t* pattern,
                    const WalkOptions& options,
                    std::vector<FoundItem>& found);

} // namespace wmobile
