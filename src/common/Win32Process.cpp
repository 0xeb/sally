// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "IProcess.h"
#include <cstdlib>
#include <string.h>
#include <new>
#include <vector>

// Internal state for process handles
struct ProcessState
{
    HANDLE hProcess;
    HANDLE hThread;  // Thread handle from CreateProcess (closed immediately)
    DWORD processId;
};

class Win32Process : public IProcess
{
public:
    HPROCESS CreateProcess(const ProcessStartInfo& startInfo) override
    {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;

        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        si.lpTitle = startInfo.windowTitle.empty()
                         ? nullptr
                         : const_cast<LPWSTR>(startInfo.windowTitle.c_str());

        // Set window visibility
        if (startInfo.hideWindow)
        {
            si.dwFlags |= STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
        }
        else if (startInfo.useShowWindow)
        {
            si.dwFlags |= STARTF_USESHOWWINDOW;
            si.wShowWindow = startInfo.showWindow;
        }

        if (startInfo.usePosition)
        {
            si.dwFlags |= STARTF_USEPOSITION;
            si.dwX = startInfo.x;
            si.dwY = startInfo.y;
        }

        if (startInfo.useSize)
        {
            si.dwFlags |= STARTF_USESIZE;
            si.dwXSize = startInfo.width;
            si.dwYSize = startInfo.height;
        }

        // Set standard handles if provided
        if (startInfo.hStdInput || startInfo.hStdOutput || startInfo.hStdError)
        {
            si.dwFlags |= STARTF_USESTDHANDLES;
            si.hStdInput = startInfo.hStdInput;
            si.hStdOutput = startInfo.hStdOutput;
            si.hStdError = startInfo.hStdError;
        }

        // Build creation flags
        DWORD flags = startInfo.creationFlags;
        if (startInfo.createNewConsole)
            flags |= CREATE_NEW_CONSOLE;

        LPVOID environment = nullptr;
        if (startInfo.useEnvironment)
        {
            if (startInfo.environmentBlock.size() < 2 ||
                startInfo.environmentBlock[startInfo.environmentBlock.size() - 1] != L'\0' ||
                startInfo.environmentBlock[startInfo.environmentBlock.size() - 2] != L'\0')
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return INVALID_HPROCESS;
            }
            environment = const_cast<wchar_t*>(startInfo.environmentBlock.data());
            flags |= CREATE_UNICODE_ENVIRONMENT;
        }

        // CreateProcessW modifies the command line buffer, so we need a copy
        std::vector<wchar_t> commandLine;
        try
        {
            if (!startInfo.commandLine.empty())
            {
                commandLine.assign(startInfo.commandLine.begin(),
                                   startInfo.commandLine.end());
                commandLine.push_back(L'\0');
            }
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HPROCESS;
        }

        BOOL result = ::CreateProcessW(
            startInfo.applicationName.empty() ? nullptr : startInfo.applicationName.c_str(),
            commandLine.empty() ? nullptr : commandLine.data(),
            nullptr,  // process security attributes
            nullptr,  // thread security attributes
            startInfo.inheritHandles ? TRUE : FALSE,
            flags,
            environment,
            startInfo.workingDirectory.empty() ? nullptr : startInfo.workingDirectory.c_str(),
            &si,
            &pi);

        if (!result)
            return INVALID_HPROCESS;

        // Close thread handle immediately - we don't need it
        ::CloseHandle(pi.hThread);

        // Allocate state
        ProcessState* state = (ProcessState*)malloc(sizeof(ProcessState));
        if (!state)
        {
            ::CloseHandle(pi.hProcess);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HPROCESS;
        }

        state->hProcess = pi.hProcess;
        state->hThread = nullptr;  // Already closed
        state->processId = pi.dwProcessId;

        return static_cast<HPROCESS>(state);
    }

    WaitResult WaitForProcess(HPROCESS process, DWORD timeoutMs) override
    {
        if (!process)
            return WaitResult::Failed;

        ProcessState* state = static_cast<ProcessState*>(process);
        DWORD result = ::WaitForSingleObject(state->hProcess, timeoutMs);

        switch (result)
        {
            case WAIT_OBJECT_0:
                return WaitResult::Signaled;
            case WAIT_TIMEOUT:
                return WaitResult::Timeout;
            default:
                return WaitResult::Failed;
        }
    }

    ProcessResult GetExitCode(HPROCESS process, DWORD& exitCode) override
    {
        if (!process)
            return ProcessResult::Error(ERROR_INVALID_HANDLE);

        ProcessState* state = static_cast<ProcessState*>(process);
        if (!::GetExitCodeProcess(state->hProcess, &exitCode))
            return ProcessResult::Error(GetLastError());

        return ProcessResult::Ok();
    }

    ProcessResult TerminateProcess(HPROCESS process, UINT exitCode) override
    {
        if (!process)
            return ProcessResult::Error(ERROR_INVALID_HANDLE);

        ProcessState* state = static_cast<ProcessState*>(process);
        if (!::TerminateProcess(state->hProcess, exitCode))
            return ProcessResult::Error(GetLastError());

        return ProcessResult::Ok();
    }

    bool IsProcessRunning(HPROCESS process) override
    {
        if (!process)
            return false;

        ProcessState* state = static_cast<ProcessState*>(process);
        DWORD result = ::WaitForSingleObject(state->hProcess, 0);
        return result == WAIT_TIMEOUT;
    }

    void CloseProcess(HPROCESS process) override
    {
        if (!process)
            return;

        ProcessState* state = static_cast<ProcessState*>(process);
        if (state->hProcess)
            ::CloseHandle(state->hProcess);
        free(state);
    }

    HANDLE DetachProcessHandle(HPROCESS process) override
    {
        if (!process)
        {
            SetLastError(ERROR_INVALID_HANDLE);
            return NULL;
        }

        ProcessState* state = static_cast<ProcessState*>(process);
        HANDLE hProcess = state->hProcess;
        state->hProcess = NULL;
        free(state);
        if (hProcess == NULL)
            SetLastError(ERROR_INVALID_HANDLE);
        return hProcess;
    }

    DWORD GetProcessId(HPROCESS process) override
    {
        if (!process)
            return 0;

        ProcessState* state = static_cast<ProcessState*>(process);
        return state->processId;
    }

    HPROCESS OpenProcess(DWORD processId, DWORD desiredAccess) override
    {
        HANDLE hProcess = ::OpenProcess(desiredAccess, FALSE, processId);
        if (!hProcess)
            return INVALID_HPROCESS;

        ProcessState* state = (ProcessState*)malloc(sizeof(ProcessState));
        if (!state)
        {
            ::CloseHandle(hProcess);
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return INVALID_HPROCESS;
        }

        state->hProcess = hProcess;
        state->hThread = nullptr;
        state->processId = processId;

        return static_cast<HPROCESS>(state);
    }
};

// Global instance
static Win32Process g_win32Process;
IProcess* gProcess = &g_win32Process;

IProcess* GetWin32Process()
{
    return &g_win32Process;
}
