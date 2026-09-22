// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "ExternalToolRunner.h"
#include "IShell.h"

#include <string>
#include <windows.h>

struct ViewerEditorProcessLaunchRequest
{
    std::wstring commandLine;
    std::wstring workingDirectory;
    DWORD creationFlags = NORMAL_PRIORITY_CLASS;
    bool useShowWindow = false;
    WORD showWindow = SW_SHOWNORMAL;
    bool usePosition = false;
    DWORD x = 0;
    DWORD y = 0;
    bool useSize = false;
    DWORD width = 0;
    DWORD height = 0;
};

class IViewerEditorLauncher
{
public:
    virtual ~IViewerEditorLauncher() = default;

    virtual ExternalToolResult LaunchProcess(const ViewerEditorProcessLaunchRequest& request) = 0;
    virtual ShellExecResult OpenFileWithShell(HWND hwnd, const wchar_t* path, int showCommand) = 0;
};

class CViewerEditorLauncher : public IViewerEditorLauncher
{
public:
    CViewerEditorLauncher(IExternalToolRunner* runner = nullptr, IShell* shell = nullptr);

    ExternalToolResult LaunchProcess(const ViewerEditorProcessLaunchRequest& request) override;
    ShellExecResult OpenFileWithShell(HWND hwnd, const wchar_t* path, int showCommand) override;

private:
    IExternalToolRunner* ResolveRunner() const;
    IShell* ResolveShell() const;

    IExternalToolRunner* Runner;
    IShell* Shell;
};

extern IViewerEditorLauncher* gViewerEditorLauncher;
