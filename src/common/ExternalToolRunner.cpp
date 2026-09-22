// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/ExternalToolRunner.h"

#include <cwchar>
#include <new>

namespace
{
bool CommandLineTooLong(const std::wstring& commandLine)
{
    return commandLine.length() >= 32767;
}
} // namespace

CExternalToolRunner::CExternalToolRunner(IProcess* process)
    : Process(process)
{
}

IProcess* CExternalToolRunner::ResolveProcess() const
{
    return Process != nullptr ? Process : gProcess;
}

ExternalToolResult CExternalToolRunner::Launch(const ExternalToolRequest& request)
try
{
    if (request.commandLine.empty())
        return ExternalToolResult::Error(ERROR_INVALID_PARAMETER);
    if (CommandLineTooLong(request.commandLine))
        return ExternalToolResult::Error(ERROR_FILENAME_EXCED_RANGE);

    IProcess* process = ResolveProcess();
    if (process == nullptr)
        return ExternalToolResult::Error(ERROR_INVALID_PARAMETER);

    ProcessStartInfo info;
    info.applicationName = request.applicationName;
    info.commandLine = request.commandLine;
    info.workingDirectory = request.workingDirectory;
    info.environmentBlock = request.environmentBlock;
    info.useEnvironment = request.useEnvironment;
    info.inheritHandles = request.inheritHandles;
    info.createNewConsole = request.createNewConsole;
    info.hideWindow = request.hideWindow;
    info.creationFlags = request.creationFlags;
    info.windowTitle = request.windowTitle;
    info.useShowWindow = request.useShowWindow;
    info.showWindow = request.showWindow;
    info.usePosition = request.usePosition;
    info.x = request.x;
    info.y = request.y;
    info.useSize = request.useSize;
    info.width = request.width;
    info.height = request.height;
    info.hStdInput = request.hStdInput;
    info.hStdOutput = request.hStdOutput;
    info.hStdError = request.hStdError;

    HPROCESS launched = process->CreateProcess(info);
    if (launched == INVALID_HPROCESS)
    {
        DWORD errorCode = GetLastError();
        return ExternalToolResult::Error(errorCode != ERROR_SUCCESS ? errorCode : ERROR_GEN_FAILURE);
    }

    return ExternalToolResult::Ok(launched, process->GetProcessId(launched), process);
}
catch (const std::bad_alloc&)
{
    return ExternalToolResult::Error(ERROR_NOT_ENOUGH_MEMORY);
}

static CExternalToolRunner g_defaultExternalToolRunner;
IExternalToolRunner* gExternalToolRunner = &g_defaultExternalToolRunner;
