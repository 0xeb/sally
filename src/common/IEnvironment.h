// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <cstdint>
#include <windows.h>

// Result of environment operations
struct EnvResult
{
    bool success;
    DWORD errorCode;

    static EnvResult Ok() { return {true, ERROR_SUCCESS}; }
    static EnvResult Error(DWORD err) { return {false, err}; }

    // Convenience: check if variable not found
    bool notFound() const { return errorCode == ERROR_ENVVAR_NOT_FOUND; }
};

// Abstract interface for environment/system directory operations
// Enables mocking for tests and centralized environment access
class IEnvironment
{
public:
    virtual ~IEnvironment() = default;

    // Environment variables
    virtual EnvResult GetVariable(const wchar_t* name, std::wstring& value) = 0;
    virtual EnvResult SetVariable(const wchar_t* name, const wchar_t* value) = 0;

    // System paths
    virtual EnvResult GetTempPath(std::wstring& path) = 0;
    virtual EnvResult GetSystemDirectory(std::wstring& path) = 0;
    virtual EnvResult GetWindowsDirectory(std::wstring& path) = 0;

    // Current directory
    virtual EnvResult GetCurrentDirectory(std::wstring& path) = 0;
    virtual EnvResult SetCurrentDirectory(const wchar_t* path) = 0;

    // Expand environment strings (e.g., %USERPROFILE%\Documents)
    virtual EnvResult ExpandEnvironmentStrings(const wchar_t* source, std::wstring& expanded) = 0;

    // Computer/user names
    virtual EnvResult GetComputerName(std::wstring& name) = 0;
    virtual EnvResult GetUserName(std::wstring& name) = 0;
};

// Global environment interface - default is Win32 implementation
extern IEnvironment* gEnvironment;

// Returns the default Win32 implementation
IEnvironment* GetWin32Environment();
