// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#pragma once

#include <windows.h>
#include <string>

// Result of path-related operations.
struct PathResult
{
    bool success;
    DWORD errorCode;

    static PathResult Ok() { return {true, ERROR_SUCCESS}; }
    static PathResult Error(DWORD err) { return {false, err}; }
};

// Abstract interface for Win32 path operations.
// Centralizes dynamic buffer growth/retry and long-path normalization.
class IPathService
{
public:
    virtual ~IPathService() {}

    // Converts path to a Win32-long-path-safe variant (adds \\?\ or \\?\UNC\ when needed).
    virtual PathResult ToLongPath(const wchar_t* path, std::wstring& outPath) = 0;

    // Retrieves process current directory.
    virtual PathResult GetCurrentDirectory(std::wstring& outPath) = 0;

    // Retrieves module file name.
    virtual PathResult GetModuleFileName(HMODULE module, std::wstring& outPath) = 0;

    // Retrieves temp directory path.
    virtual PathResult GetTempPath(std::wstring& outPath) = 0;

    // Expands a possibly relative path to full absolute path.
    virtual PathResult GetFullPathName(const wchar_t* inputPath, std::wstring& outPath) = 0;
};

// Global path service instance - default is Win32 implementation.
extern IPathService* gPathService;

// Returns the default Win32 implementation.
IPathService* GetWin32PathService();

