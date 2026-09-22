// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

// Result of process operations
struct ProcessResult
{
    bool success;
    DWORD errorCode;

    static ProcessResult Ok() { return {true, ERROR_SUCCESS}; }
    static ProcessResult Error(DWORD err) { return {false, err}; }
};

// Options for process creation
struct ProcessStartInfo
{
    std::wstring applicationName;  // Empty lets Windows parse the executable from commandLine.
    std::wstring commandLine;
    std::wstring workingDirectory; // Empty inherits the parent's current directory.
    std::wstring windowTitle;
    std::vector<wchar_t> environmentBlock; // Explicit double-NUL UTF-16 block.
    bool useEnvironment = false;
    bool inheritHandles = false;
    bool createNewConsole = false;
    bool hideWindow = false;
    DWORD creationFlags = 0;
    bool useShowWindow = false;
    WORD showWindow = SW_SHOWNORMAL;
    bool usePosition = false;
    DWORD x = 0;
    DWORD y = 0;
    bool useSize = false;
    DWORD width = 0;
    DWORD height = 0;

    // Standard handles for redirection (optional)
    HANDLE hStdInput = nullptr;
    HANDLE hStdOutput = nullptr;
    HANDLE hStdError = nullptr;
};

// Wait result enum
enum class WaitResult
{
    Signaled,    // Object signaled (process exited, etc.)
    Timeout,     // Timeout expired
    Failed       // Wait failed (call GetLastError)
};

// Opaque process handle
typedef void* HPROCESS;
#define INVALID_HPROCESS nullptr

// Abstract interface for process operations
// Enables mocking for tests and centralized process management
class IProcess
{
public:
    virtual ~IProcess() = default;

    // Create a new process
    // Returns process handle on success, INVALID_HPROCESS on failure
    virtual HPROCESS CreateProcess(const ProcessStartInfo& startInfo) = 0;

    // Wait for process to exit
    // timeoutMs: INFINITE for infinite wait, or timeout in milliseconds
    virtual WaitResult WaitForProcess(HPROCESS process, DWORD timeoutMs = INFINITE) = 0;

    // Get process exit code (only valid after process exits)
    virtual ProcessResult GetExitCode(HPROCESS process, DWORD& exitCode) = 0;

    // Terminate a process forcefully
    virtual ProcessResult TerminateProcess(HPROCESS process, UINT exitCode) = 0;

    // Check if process is still running
    virtual bool IsProcessRunning(HPROCESS process) = 0;

    // Close process handle (must be called when done)
    virtual void CloseProcess(HPROCESS process) = 0;

    // Transfer ownership of the native process HANDLE to a caller that must
    // pass it to legacy APIs. The HPROCESS wrapper is invalid after success.
    virtual HANDLE DetachProcessHandle(HPROCESS process) = 0;

    // Get process ID from handle
    virtual DWORD GetProcessId(HPROCESS process) = 0;

    // Open existing process by ID
    virtual HPROCESS OpenProcess(DWORD processId, DWORD desiredAccess) = 0;
};

// Global process interface - default is Win32 implementation
extern IProcess* gProcess;

// Returns the default Win32 implementation
IProcess* GetWin32Process();

