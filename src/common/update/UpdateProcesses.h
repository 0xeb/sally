// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The programs that run from a Sally folder (sally.exe, the bug reporter, the trace server)
// and must all have ended before the folder's files can be replaced.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace Sally::Update
{

struct RunningProcess
{
    DWORD Pid = 0;
    std::wstring Image; // full path of the executable
};

// True when path names something inside folder (or folder itself), compared without case.
bool IsPathInsideFolder(const std::wstring& path, const std::wstring& folder);

// Processes whose executable lies inside folder, except excludePid.
std::vector<RunningProcess> FindProcessesInFolder(const std::wstring& folder, DWORD excludePid);

// The full path of a loaded module (NULL: the program), empty on failure.
std::wstring ModuleFilePath(HMODULE module);

// Waits until every listed process has ended; false on timeout.
bool WaitForProcessesToExit(const std::vector<DWORD>& pids, DWORD timeoutMs);

} // namespace Sally::Update
