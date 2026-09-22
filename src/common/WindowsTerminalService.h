// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "CommandShellService.h"
#include "IEnvironment.h"
#include "IExecutableLocator.h"
#include "IFileSystem.h"
#include "IProcess.h"

#include <string>
#include <vector>

enum class ShellTargetKind : DWORD
{
    ComSpec = 0,
    WindowsTerminalDefault = 1,
    WindowsTerminalProfile = 2,
};

struct ShellTarget
{
    ShellTargetKind kind = ShellTargetKind::ComSpec;
    std::wstring profileGuid;
    std::wstring profileName;
};

struct WindowsTerminalProfile
{
    std::wstring guid;
    std::wstring name;
    bool isDefault = false;
};

struct WindowsTerminalCatalog
{
    bool installed = false;
    bool settingsAvailable = false;
    std::wstring executablePath;
    std::wstring settingsPath;
    std::wstring defaultProfileGuid;
    std::vector<WindowsTerminalProfile> profiles;
    std::wstring parseError;
};

struct WindowsTerminalLaunchRequest
{
    std::wstring workingDirectory;
    bool usePosition = false;
    int x = 0;
    int y = 0;
};

bool ResolveWindowsTerminalTarget(const WindowsTerminalCatalog& catalog,
                                  const ShellTarget& requested,
                                  ShellTarget& resolved);

class CWindowsTerminalService
{
public:
    CWindowsTerminalService(IExecutableLocator* executableLocator = nullptr,
                            IEnvironment* environment = nullptr,
                            IFileSystem* fileSystem = nullptr,
                            IProcess* process = nullptr);

    const WindowsTerminalCatalog& Refresh(bool force);
    bool ResolveTarget(const ShellTarget& requested, ShellTarget& resolved) const;
    CommandShellResult Launch(const ShellTarget& target,
                              const WindowsTerminalLaunchRequest& request);

    static bool ParseSettingsJson(const char* json, size_t size,
                                  WindowsTerminalCatalog& catalog,
                                  std::wstring& error);
    static std::wstring BuildCommandLine(const std::wstring& executablePath,
                                         const ShellTarget& target,
                                         const WindowsTerminalLaunchRequest& request);

private:
    IExecutableLocator* ResolveExecutableLocator() const;
    IEnvironment* ResolveEnvironment() const;
    IFileSystem* ResolveFileSystem() const;
    IProcess* ResolveProcess() const;
    bool FindSettingsFile(const ExecutableLocation& executable,
                          std::wstring& path, FileInfo& info) const;
    bool ReadSettingsFile(const std::wstring& path, std::vector<char>& bytes,
                          DWORD& errorCode) const;

    IExecutableLocator* ExecutableLocator;
    IEnvironment* Environment;
    IFileSystem* FileSystem;
    IProcess* Process;
    WindowsTerminalCatalog Catalog;
    FILETIME CachedSettingsWriteTime{};
    bool CachedSettingsFilePresent = false;
    bool CacheInitialized = false;
};

extern CWindowsTerminalService* gWindowsTerminalService;
