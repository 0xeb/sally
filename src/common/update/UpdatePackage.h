// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Where an update is downloaded and unpacked, and the checks that the unpacked release is
// the Sally version and architecture that was asked for.

#pragma once

#include <windows.h>

#include <string>

namespace Sally::Update
{

// %LOCALAPPDATA%\Sally\Update, created when missing; empty on failure.
std::wstring UpdateWorkFolder();
// <work folder>\v<version>-<arch>
std::wstring StagingFolderFor(const std::wstring& workFolder, const std::wstring& version, const std::wstring& arch);
// The extracted release inside a staging folder.
std::wstring StagedFilesFolder(const std::wstring& staging);
// The result file the updater leaves for the next Sally.
std::wstring UpdateResultPath(const std::wstring& workFolder);

// IMAGE_FILE_MACHINE_* of an architecture name used in release package names (x64, x86,
// ARM64; any case), 0 when unknown.
WORD MachineForArch(const std::wstring& arch);

bool ReadPeMachine(const std::wstring& path, WORD& machine);
// "a.b.c.d" from the file's version resource.
bool ReadFileVersionText(const std::wstring& path, std::wstring& version);
// True when every number of expected ("1.0.36") equals the same number of actual
// ("1.0.36.190"); a leading 'v' on expected is ignored.
bool VersionMatches(const std::wstring& actual, const std::wstring& expected);

enum class VersionOrder
{
    Older,
    Same,
    Newer,
    Unknown,
};

// How a package's "a.b.c.d" version relates to the installed one, number by number.
VersionOrder CompareVersionText(const std::wstring& installed, const std::wstring& package);

// The Sally folder of an updater at <folder>\utils\salupdate.exe; empty for any other place.
std::wstring InstallRootOfUpdater(const std::wstring& updaterPath);

// The SHA-256 a release publishes for a package, from a Sally-v*-SHA256SUMS.txt (or any
// *SHA256SUMS*.txt) beside it. False when no such file lists the package.
bool FindChecksumNextTo(const std::wstring& packagePath, std::string& sha256, std::wstring& sumsPath);

// The unpacked release must hold a sally.exe for this architecture and version.
bool CheckStagedRelease(const std::wstring& filesFolder, const std::wstring& version, const std::wstring& arch,
                        std::wstring& error);

} // namespace Sally::Update
