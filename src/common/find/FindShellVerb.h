// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <shlobj.h>

#include <string>

namespace sally::find
{
// What happened when Find ran a shell verb (cut, copy, properties) on its selected results.
enum class ShellVerbOutcome
{
    NotRun,         // nothing selected, or OLE is unavailable
    NoCommonPrefix, // the selection spans drives or shares; no shell menu can hold it
    ItemNotFound,   // the shell could not build a menu for the selected items
    InvokeFailed,   // the verb ran and reported failure (or the handler crashed)
    Invoked,        // the verb ran and succeeded
};

inline ShellVerbOutcome ClassifyShellVerb(bool haveCommonPrefix, bool haveMenu,
                                          bool invokeSucceeded)
{
    if (!haveCommonPrefix)
        return ShellVerbOutcome::NoCommonPrefix;
    if (!haveMenu)
        return ShellVerbOutcome::ItemNotFound;
    return invokeSucceeded ? ShellVerbOutcome::Invoked : ShellVerbOutcome::InvokeFailed;
}

// Cut and Copy add Sally's "Preferred DropEffect" and ownership marker to whatever the
// clipboard holds; they never replace its contents. Unless the shell verb really put the
// selection there, those marks land on the user's earlier clipboard data, and marking it
// as a cut turns a later paste of files copied elsewhere into a move.
inline bool ShellVerbMayMarkClipboard(ShellVerbOutcome outcome)
{
    return outcome == ShellVerbOutcome::Invoked;
}

// The shell invocation Find makes for a canonical verb ("cut", "copy", "properties") on its
// selection, filled like the panel's working Cut, Copy and Properties.
//
// The verb goes in both forms. With CMIC_MASK_UNICODE the shell's default menu still reads
// the ANSI lpVerb, and a NULL there is taken as "command offset 0" (a menu command that was
// never queried), so the verb fails. Canonical verbs are ASCII. The directory is the
// selection's common folder, as the panel passes its own path. The object owns every
// string the structure points at, so it cannot be copied.
class ShellVerbInvocation
{
public:
    ShellVerbInvocation(const wchar_t* verb, const std::wstring& directory, HWND hwnd,
                        POINT invokePoint)
        : VerbW(verb != nullptr ? verb : L""), Directory(directory)
    {
        for (wchar_t c : VerbW)
            VerbA.push_back(c > 0 && c < 0x80 ? static_cast<char>(c) : '?');
        ZeroMemory(&Data, sizeof(Data));
        Data.cbSize = sizeof(Data);
        Data.fMask = CMIC_MASK_PTINVOKE | CMIC_MASK_UNICODE;
        Data.hwnd = hwnd;
        Data.lpVerb = VerbA.c_str();
        Data.lpVerbW = VerbW.c_str();
        Data.lpDirectoryW = Directory.empty() ? nullptr : Directory.c_str();
        Data.nShow = SW_SHOWNORMAL;
        Data.ptInvoke = invokePoint;
    }
    ShellVerbInvocation(const ShellVerbInvocation&) = delete;
    ShellVerbInvocation& operator=(const ShellVerbInvocation&) = delete;

    const CMINVOKECOMMANDINFOEX& Info() const { return Data; }
    CMINVOKECOMMANDINFO* Get() { return reinterpret_cast<CMINVOKECOMMANDINFO*>(&Data); }

private:
    std::wstring VerbW;
    std::string VerbA;
    std::wstring Directory;
    CMINVOKECOMMANDINFOEX Data;
};
} // namespace sally::find
