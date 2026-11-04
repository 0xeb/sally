// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/CommandShellService.h"
#include "common/IShell.h"

#include <new>

namespace
{
bool HasCommandLineWhitespace(const std::wstring& value)
{
    return value.find_first_of(L" \t") != std::wstring::npos;
}

std::wstring QuoteExecutableForCommandLine(const std::wstring& value)
{
    if (value.empty())
        return value;
    if (value.front() == L'"')
        return value;
    if (!HasCommandLineWhitespace(value))
        return value;

    std::wstring quoted;
    quoted.reserve(value.length() + 2);
    quoted.push_back(L'"');
    quoted.append(value);
    quoted.push_back(L'"');
    return quoted;
}

bool CommandLineTooLong(const std::wstring& commandLine)
{
    return commandLine.length() >= 32767;
}

std::wstring ExtractExecutableName(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return path;
    return path.substr(slash + 1);
}
} // namespace

CCommandShellService::CCommandShellService(IProcess* process, IEnvironment* environment, IShell* shell)
    : Process(process)
    , Environment(environment)
    , Shell(shell)
{
}

IProcess* CCommandShellService::ResolveProcess() const
{
    return Process != nullptr ? Process : gProcess;
}

IEnvironment* CCommandShellService::ResolveEnvironment() const
{
    return Environment != nullptr ? Environment : gEnvironment;
}

bool CCommandShellService::ResolveComspec(const CommandShellRequest& request,
                                          std::wstring& comspec, DWORD& errorCode) const
{
    if (!request.comspec.empty())
    {
        comspec = request.comspec;
        return true;
    }

    IEnvironment* environment = ResolveEnvironment();
    if (environment == nullptr)
    {
        errorCode = ERROR_INVALID_PARAMETER;
        return false;
    }

    EnvResult result = environment->GetVariable(L"COMSPEC", comspec);
    if (!result.success || comspec.empty())
    {
        errorCode = result.success ? ERROR_ENVVAR_NOT_FOUND : result.errorCode;
        return false;
    }

    return true;
}

CommandShellResult CCommandShellService::LaunchBuiltCommand(const CommandShellRequest& request,
                                                            const std::wstring& commandLine, const std::wstring& comspec)
{
    if (CommandLineTooLong(commandLine))
        return CommandShellResult::Error(ERROR_FILENAME_EXCED_RANGE);

    if (request.runAsAdministrator)
    {
        IShell* shell = Shell != nullptr ? Shell : gShell;
        if (shell == nullptr)
            return CommandShellResult::Error(ERROR_INVALID_PARAMETER);
        // COMSPEC may carry surrounding quotes. ShellExecute takes a filename,
        // while its separate parameter string must not contain that filename.
        const std::wstring executable = comspec.size() >= 2 && comspec.front() == L'"' && comspec.back() == L'"'
                                            ? comspec.substr(1, comspec.size() - 2) : comspec;
        if (executable.empty())
            return CommandShellResult::Error(ERROR_INVALID_PARAMETER);
        const std::wstring parameters = commandLine.substr(QuoteExecutableForCommandLine(comspec).size());
        ShellExecInfo info;
        info.file = executable.c_str();
        info.parameters = parameters.empty() ? nullptr : parameters.c_str();
        info.verb = L"runas";
        info.directory = request.workingDirectory.empty() ? nullptr : request.workingDirectory.c_str();
        info.hwnd = request.owner;
        const ShellExecResult result = shell->Execute(info);
        return result.success ? CommandShellResult::Ok(INVALID_HPROCESS, 0, nullptr)
                              : CommandShellResult::Error(result.errorCode);
    }

    IProcess* process = ResolveProcess();
    if (process == nullptr)
        return CommandShellResult::Error(ERROR_INVALID_PARAMETER);

    ProcessStartInfo info;
    info.commandLine = commandLine;
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

    HPROCESS launched = process->CreateProcess(info);
    if (launched == INVALID_HPROCESS)
    {
        DWORD errorCode = GetLastError();
        return CommandShellResult::Error(errorCode != ERROR_SUCCESS ? errorCode : ERROR_GEN_FAILURE);
    }

    return CommandShellResult::Ok(launched, process->GetProcessId(launched), process);
}

CommandShellResult CCommandShellService::LaunchShell(const CommandShellRequest& request)
try
{
    std::wstring comspec;
    DWORD errorCode = ERROR_SUCCESS;
    if (!ResolveComspec(request, comspec, errorCode))
        return CommandShellResult::Error(errorCode);

    return LaunchBuiltCommand(request, QuoteExecutableForCommandLine(comspec), comspec);
}
catch (const std::bad_alloc&)
{
    return CommandShellResult::Error(ERROR_NOT_ENOUGH_MEMORY);
}

CommandShellResult CCommandShellService::LaunchCommand(const CommandShellRequest& request)
try
{
    std::wstring comspec;
    DWORD errorCode = ERROR_SUCCESS;
    if (!ResolveComspec(request, comspec, errorCode))
        return CommandShellResult::Error(errorCode);

    std::wstring commandLine = QuoteExecutableForCommandLine(comspec);
    if (request.unicodeOutput)
        commandLine.append(L" /U");
    commandLine.append(request.keepOpen ? L" /K" : L" /C");

    if (!request.command.empty())
    {
        commandLine.push_back(L' ');
        commandLine.push_back(L'"');
        commandLine.append(request.command);
        commandLine.push_back(L'"');
    }

    return LaunchBuiltCommand(request, commandLine, comspec);
}
catch (const std::bad_alloc&)
{
    return CommandShellResult::Error(ERROR_NOT_ENOUGH_MEMORY);
}

CommandShellPolicyResult CCommandShellService::GetPolicyInfo(const CommandShellRequest& request)
try
{
    std::wstring comspec;
    DWORD errorCode = ERROR_SUCCESS;
    if (!ResolveComspec(request, comspec, errorCode))
        return CommandShellPolicyResult::Error(errorCode);

    CommandShellPolicyInfo info;
    info.quotedComspec = QuoteExecutableForCommandLine(comspec);

    info.executableNameForPolicy = ExtractExecutableName(comspec);

    return CommandShellPolicyResult::Ok(std::move(info));
}
catch (const std::bad_alloc&)
{
    return CommandShellPolicyResult::Error(ERROR_NOT_ENOUGH_MEMORY);
}

static CCommandShellService g_defaultCommandShellService;
ICommandShellService* gCommandShellService = &g_defaultCommandShellService;
