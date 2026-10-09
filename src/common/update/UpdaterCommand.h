// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// How Sally talks to its updater, utils\salupdate.exe: the command line that starts it and
// the result file it leaves for the next Sally to report.
//
//   salupdate --prepare --package <zip> --sha256 <hex> --staging <folder> --version <x.y.z>
//             --arch <x64|x86|ARM64> --result <file>
//     Checks the downloaded package and extracts it to <folder>\files.
//   salupdate --apply --staging <folder> --install <Sally folder> --wait <pid,pid>
//             --from <x.y.z> --to <x.y.z> --result <file> [--no-relaunch] [--elevated] [--notify]
//     Waits for Sally to close, replaces the files and starts Sally again; --notify shows the
//     outcome in a message (an update started by hand rather than by CheckVer).
//   salupdate [<zip>] [--yes] [--install <Sally folder>]
//     By hand, or with a release package dropped onto it: checks the package, asks (unless
//     --yes), closes Sally and installs the package into the Sally folder the updater belongs
//     to. Without a package it undoes an interrupted update, if there is one.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace Sally::Update
{

enum class UpdaterMode
{
    None,
    Prepare,
    Apply,
    Manual,
};

struct UpdaterCommand
{
    UpdaterMode Mode = UpdaterMode::None;
    std::wstring Package;
    std::wstring Sha256;
    std::wstring Staging;
    std::wstring Version;
    std::wstring Arch;
    std::wstring InstallRoot;
    std::vector<DWORD> WaitPids;
    std::wstring FromVersion;
    std::wstring ToVersion;
    std::wstring ResultFile;
    bool Relaunch = true;
    bool Elevated = false;
    bool Notify = false;
    bool Yes = false; // manual: accept every question
};

bool ParseUpdaterCommand(const std::vector<std::wstring>& args, UpdaterCommand& command, std::wstring& error);
// The arguments, without the program name, quoted for CreateProcess.
std::wstring FormatUpdaterCommand(const UpdaterCommand& command);
// Quotes one argument the way CommandLineToArgvW reads it back.
std::wstring QuoteCommandLineArgument(const std::wstring& argument);

enum class UpdateStatus
{
    Prepared, // the package was checked and extracted
    Updated,  // the new version is in place
    Failed,   // nothing changed, or every change was undone (see RolledBack)
    Aborted,  // Sally did not close; nothing changed
};

struct UpdateResult
{
    UpdateStatus Status = UpdateStatus::Failed;
    std::wstring InstallRoot;
    std::wstring FromVersion;
    std::wstring ToVersion;
    std::wstring FailedPath;
    DWORD Code = 0;
    std::wstring Message; // English
    bool RolledBack = false;
    bool RollbackComplete = true;
    std::vector<std::wstring> Leftovers;
};

std::string FormatUpdateResult(const UpdateResult& result);
bool ParseUpdateResult(const std::string& text, UpdateResult& result);
bool WriteUpdateResult(const std::wstring& path, const UpdateResult& result);
bool ReadUpdateResult(const std::wstring& path, UpdateResult& result);

} // namespace Sally::Update
