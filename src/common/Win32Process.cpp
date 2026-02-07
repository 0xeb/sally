// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include "IProcess.h"
#include <stdlib.h>
#include <string.h>

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

        // Set window visibility
        if (startInfo.hideWindow)
        {
            si.dwFlags |= STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
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

        // CreateProcessW modifies the command line buffer, so we need a copy
        wchar_t* cmdLineCopy = nullptr;
        if (startInfo.commandLine)
        {
            size_t len = wcslen(startInfo.commandLine) + 1;
            cmdLineCopy = (wchar_t*)malloc(len * sizeof(wchar_t));
            if (!cmdLineCopy)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return INVALID_HPROCESS;
            }
            wcscpy(cmdLineCopy, startInfo.commandLine);
        }

        BOOL result = ::CreateProcessW(
            startInfo.applicationName,
            cmdLineCopy,
            nullptr,  // process security attributes
            nullptr,  // thread security attributes
            startInfo.inheritHandles ? TRUE : FALSE,
            flags,
            nullptr,  // environment (inherit)
            startInfo.workingDirectory,
            &si,
            &pi);

        free(cmdLineCopy);

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
