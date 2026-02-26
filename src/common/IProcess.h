// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
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
    const wchar_t* applicationName;  // Optional, can be nullptr
    const wchar_t* commandLine;      // Command line (required if appName is null)
    const wchar_t* workingDirectory; // Optional, nullptr = inherit
    bool inheritHandles;             // Whether to inherit handles
    bool createNewConsole;           // CREATE_NEW_CONSOLE flag
    bool hideWindow;                 // SW_HIDE in STARTUPINFO
    DWORD creationFlags;             // Additional creation flags (0 for default)

    // Standard handles for redirection (optional)
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;

    ProcessStartInfo()
        : applicationName(nullptr)
        , commandLine(nullptr)
        , workingDirectory(nullptr)
        , inheritHandles(false)
        , createNewConsole(false)
        , hideWindow(false)
        , creationFlags(0)
        , hStdInput(nullptr)
        , hStdOutput(nullptr)
        , hStdError(nullptr)
    {
    }
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

    // Get process ID from handle
    virtual DWORD GetProcessId(HPROCESS process) = 0;

    // Open existing process by ID
    virtual HPROCESS OpenProcess(DWORD processId, DWORD desiredAccess) = 0;
};

// Global process interface - default is Win32 implementation
extern IProcess* gProcess;

// Returns the default Win32 implementation
IProcess* GetWin32Process();

// ANSI helpers for migration
inline std::wstring AnsiCmdToWide(const char* cmd)
{
    if (!cmd || !*cmd) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, cmd, -1, nullptr, 0);
    if (len == 0) return L"";
    std::wstring wide;
    wide.resize(len);
    MultiByteToWideChar(CP_ACP, 0, cmd, -1, &wide[0], len);
    wide.resize(len - 1);
    return wide;
}

// ANSI helper: Create process with ANSI strings
inline HPROCESS CreateProcessA(IProcess* proc, const char* appName, const char* cmdLine,
                               const char* workDir, bool inheritHandles = false,
                               bool createNewConsole = false, bool hideWindow = false)
{
    ProcessStartInfo info;
    std::wstring wideApp = appName ? AnsiCmdToWide(appName) : L"";
    std::wstring wideCmd = cmdLine ? AnsiCmdToWide(cmdLine) : L"";
    std::wstring wideDir = workDir ? AnsiCmdToWide(workDir) : L"";

    info.applicationName = wideApp.empty() ? nullptr : wideApp.c_str();
    info.commandLine = wideCmd.empty() ? nullptr : wideCmd.c_str();
    info.workingDirectory = wideDir.empty() ? nullptr : wideDir.c_str();
    info.inheritHandles = inheritHandles;
    info.createNewConsole = createNewConsole;
    info.hideWindow = hideWindow;

    return proc->CreateProcess(info);
}
