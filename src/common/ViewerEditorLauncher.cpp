// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/ViewerEditorLauncher.h"

#include <new>

CViewerEditorLauncher::CViewerEditorLauncher(IExternalToolRunner* runner, IShell* shell)
    : Runner(runner)
    , Shell(shell)
{
}

IExternalToolRunner* CViewerEditorLauncher::ResolveRunner() const
{
    return Runner != nullptr ? Runner : gExternalToolRunner;
}

IShell* CViewerEditorLauncher::ResolveShell() const
{
    return Shell != nullptr ? Shell : gShell;
}

ExternalToolResult CViewerEditorLauncher::LaunchProcess(const ViewerEditorProcessLaunchRequest& request)
try
{
    IExternalToolRunner* runner = ResolveRunner();
    if (runner == nullptr)
        return ExternalToolResult::Error(ERROR_INVALID_PARAMETER);

    ExternalToolRequest toolRequest;
    toolRequest.commandLine = request.commandLine;
    toolRequest.workingDirectory = request.workingDirectory;
    toolRequest.creationFlags = request.creationFlags;
    toolRequest.useShowWindow = request.useShowWindow;
    toolRequest.showWindow = request.showWindow;
    toolRequest.usePosition = request.usePosition;
    toolRequest.x = request.x;
    toolRequest.y = request.y;
    toolRequest.useSize = request.useSize;
    toolRequest.width = request.width;
    toolRequest.height = request.height;

    return runner->Launch(toolRequest);
}
catch (const std::bad_alloc&)
{
    return ExternalToolResult::Error(ERROR_NOT_ENOUGH_MEMORY);
}

ShellExecResult CViewerEditorLauncher::OpenFileWithShell(HWND hwnd, const wchar_t* path, int showCommand)
{
    if (path == nullptr || path[0] == L'\0')
        return ShellExecResult::Error(ERROR_INVALID_PARAMETER);

    IShell* shell = ResolveShell();
    if (shell == nullptr)
        return ShellExecResult::Error(ERROR_INVALID_PARAMETER);

    ShellExecInfo info;
    info.hwnd = hwnd;
    info.verb = L"open";
    info.file = path;
    info.showCommand = showCommand;

    return shell->Execute(info);
}

static CViewerEditorLauncher g_defaultViewerEditorLauncher;
IViewerEditorLauncher* gViewerEditorLauncher = &g_defaultViewerEditorLauncher;
