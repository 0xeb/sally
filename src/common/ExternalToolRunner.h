// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "IProcess.h"

#include <string>
#include <vector>
#include <windows.h>

struct ExternalToolRequest
{
    std::wstring applicationName;
    std::wstring commandLine;
    std::wstring workingDirectory;
    std::wstring windowTitle;
    std::vector<wchar_t> environmentBlock;
    bool useEnvironment = false;
    bool inheritHandles = false;
    bool createNewConsole = false;
    bool hideWindow = false;
    DWORD creationFlags = CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS;
    bool useShowWindow = false;
    WORD showWindow = SW_SHOWNORMAL;
    bool usePosition = false;
    DWORD x = 0;
    DWORD y = 0;
    bool useSize = false;
    DWORD width = 0;
    DWORD height = 0;
    HANDLE hStdInput = nullptr;
    HANDLE hStdOutput = nullptr;
    HANDLE hStdError = nullptr;
};

struct ExternalToolResult
{
    bool success;
    DWORD errorCode;
    HPROCESS process;
    DWORD processId;
    IProcess* processOwner;

    static ExternalToolResult Ok(HPROCESS process, DWORD processId, IProcess* processOwner)
    {
        return {true, ERROR_SUCCESS, process, processId, processOwner};
    }

    static ExternalToolResult Error(DWORD errorCode)
    {
        return {false, errorCode, INVALID_HPROCESS, 0, nullptr};
    }

    void CloseProcess()
    {
        if (processOwner != nullptr && process != INVALID_HPROCESS)
        {
            processOwner->CloseProcess(process);
            process = INVALID_HPROCESS;
        }
    }

    HANDLE DetachNativeProcessHandle()
    {
        if (processOwner == nullptr || process == INVALID_HPROCESS)
        {
            SetLastError(ERROR_INVALID_HANDLE);
            return NULL;
        }

        HANDLE nativeHandle = processOwner->DetachProcessHandle(process);
        if (nativeHandle != NULL)
            process = INVALID_HPROCESS;
        return nativeHandle;
    }
};

class IExternalToolRunner
{
public:
    virtual ~IExternalToolRunner() = default;

    virtual ExternalToolResult Launch(const ExternalToolRequest& request) = 0;
};

class CExternalToolRunner : public IExternalToolRunner
{
public:
    explicit CExternalToolRunner(IProcess* process = nullptr);

    ExternalToolResult Launch(const ExternalToolRequest& request) override;

private:
    IProcess* ResolveProcess() const;

    IProcess* Process;
};

extern IExternalToolRunner* gExternalToolRunner;
