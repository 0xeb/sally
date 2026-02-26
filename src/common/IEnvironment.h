// SPDX-FileCopyrightText: 2026 Open Salamander Authors
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

// ANSI helpers for migration
inline std::wstring AnsiEnvToWide(const char* str)
{
    if (!str || !*str) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, str, -1, nullptr, 0);
    if (len == 0) return L"";
    std::wstring wide;
    wide.resize(len);
    MultiByteToWideChar(CP_ACP, 0, str, -1, &wide[0], len);
    wide.resize(len - 1);
    return wide;
}

inline void WideEnvToAnsi(const std::wstring& wide, char* buffer, int bufferSize)
{
    if (buffer && bufferSize > 0)
    {
        WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, buffer, bufferSize, nullptr, nullptr);
        buffer[bufferSize - 1] = '\0';  // Ensure null termination
    }
}

// ANSI helper: Get environment variable
inline EnvResult EnvGetVariableA(IEnvironment* env, const char* name,
                                 char* buffer, DWORD bufferSize)
{
    std::wstring value;
    auto result = env->GetVariable(AnsiEnvToWide(name).c_str(), value);
    if (result.success && buffer && bufferSize > 0)
        WideEnvToAnsi(value, buffer, bufferSize);
    return result;
}

// ANSI helper: Set environment variable
inline EnvResult EnvSetVariableA(IEnvironment* env, const char* name, const char* value)
{
    return env->SetVariable(AnsiEnvToWide(name).c_str(),
                            value ? AnsiEnvToWide(value).c_str() : nullptr);
}

// ANSI helper: Get temp path
inline EnvResult EnvGetTempPathA(IEnvironment* env, char* buffer, DWORD bufferSize)
{
    std::wstring path;
    auto result = env->GetTempPath(path);
    if (result.success && buffer && bufferSize > 0)
        WideEnvToAnsi(path, buffer, bufferSize);
    return result;
}

// ANSI helper: Get current directory
inline EnvResult EnvGetCurrentDirectoryA(IEnvironment* env, char* buffer, DWORD bufferSize)
{
    std::wstring path;
    auto result = env->GetCurrentDirectory(path);
    if (result.success && buffer && bufferSize > 0)
        WideEnvToAnsi(path, buffer, bufferSize);
    return result;
}

// ANSI helper: Set current directory
inline EnvResult EnvSetCurrentDirectoryA(IEnvironment* env, const char* path)
{
    return env->SetCurrentDirectory(AnsiEnvToWide(path).c_str());
}

// ANSI helper: Get system directory
inline EnvResult EnvGetSystemDirectoryA(IEnvironment* env, char* buffer, DWORD bufferSize)
{
    std::wstring path;
    auto result = env->GetSystemDirectory(path);
    if (result.success && buffer && bufferSize > 0)
        WideEnvToAnsi(path, buffer, bufferSize);
    return result;
}

// ANSI helper: Get Windows directory
inline EnvResult EnvGetWindowsDirectoryA(IEnvironment* env, char* buffer, DWORD bufferSize)
{
    std::wstring path;
    auto result = env->GetWindowsDirectory(path);
    if (result.success && buffer && bufferSize > 0)
        WideEnvToAnsi(path, buffer, bufferSize);
    return result;
}
