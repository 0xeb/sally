// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <cstddef>
#include <string>

namespace sally::renamer
{
    // Documented CreateProcessW command-line limit, including the terminator.
    inline constexpr size_t WindowsCommandLineLimit = 32767;
    enum class FilterState
    {
        Completed,
        Cancelled,
        Failed
    };
    struct FilterResult
    {
        FilterState State = FilterState::Failed;
        DWORD Error = ERROR_SUCCESS;
        DWORD ExitCode = 0;
    };
    class IFilterProgress
    {
    public:
        virtual ~IFilterProgress() = default;
        virtual bool KeepRunning() = 0;
    };

    // Borrowed handles must be inheritable. Input is a rewound spool file,
    // removing any dependency between UI-thread writes and child stdin reads.
    // No child starts before ownership by a private kill-on-close job succeeds.
    FilterResult RunFilterProcess(const std::wstring& commandLine, const wchar_t* directory,
                                  HANDLE input, HANDLE output, HANDLE error, IFilterProgress& progress);
} // namespace sally::renamer
