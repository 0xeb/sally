// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// What the parts of salupdate.exe share: the log, the messages in Sally's language, and the
// steps of an install.
//   salupdate.cpp      the log, the language and its texts, wWinMain
//   install.cpp        --prepare, --apply and an update started by hand with a downloaded zip
//   update_window.cpp  --check (the update window) and --report (how the last update went)

#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "update/UpdaterCommand.h"
#include "ApplicationAppearance.h"

namespace salupdate
{

// --- salupdate.cpp ------------------------------------------------------------------------------

Sally::Appearance::Snapshot CurrentApplicationAppearance();
extern std::wstring LogPath; // empty: no log
void Log(const std::wstring& text);

std::wstring ParentFolder(const std::wstring& path);
std::wstring FileNameOf(const std::wstring& path);
std::wstring ModulePath();
// Deletes a folder and everything in it; a link to a folder is removed, never followed.
bool RemoveTree(const std::wstring& folder);

extern LANGID Language; // Sally's language, chosen at start
std::wstring Text(UINT id);
template <typename... Args> std::wstring Text(UINT id, Args... args)
{
    const std::wstring format = Text(id);
    const int length = _scwprintf(format.c_str(), args...);
    if (length < 0)
        return format;
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    swprintf_s(&text[0], text.size(), format.c_str(), args...);
    text.resize(static_cast<size_t>(length));
    return text;
}
void Tell(const std::wstring& text, UINT icon, HWND owner = NULL);
bool Ask(const std::wstring& text, HWND owner = NULL);
std::wstring SystemErrorText(DWORD error);
// "1.0.36.188" -> "1.0.36", as Sally's About box and the release notes write it.
std::wstring ReleaseVersion(const std::wstring& fileVersion);

// --- install.cpp --------------------------------------------------------------------------------

// The work of --prepare: checks the package against command.Sha256, unpacks it into
// command.Staging, checks the unpacked release and puts a copy of this program beside it.
// result says what failed.
bool PrepareRelease(const Sally::Update::UpdaterCommand& command, Sally::Update::UpdateResult& result);
int Prepare(const Sally::Update::UpdaterCommand& command);
int Apply(const Sally::Update::UpdaterCommand& command);
int Manual(const Sally::Update::UpdaterCommand& command);

// The programs running from a Sally folder: every Sally, and any other program except the bug
// reporters (each ends with its Sally) and this updater.
struct ProgramsInFolder
{
    std::vector<DWORD> SallyPids;
    std::wstring Others; // one "\n<path>" per program
};
ProgramsInFolder FindProgramsInFolder(const std::wstring& installRoot);
// Asks every listed Sally to close (as its window's close button does).
void CloseSallyWindows(const std::vector<DWORD>& pids);
// Starts the updater copy in a staging folder with an --apply command.
bool StartApply(const Sally::Update::UpdaterCommand& apply, const std::wstring& updaterCopy, DWORD& error);

// --- update_window.cpp --------------------------------------------------------------------------

int CheckForUpdates(const Sally::Update::UpdaterCommand& command);
int ReportUpdate(const Sally::Update::UpdaterCommand& command);

} // namespace salupdate
