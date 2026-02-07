// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>
#include <shellapi.h>

// Result of shell operations
struct ShellResult
{
    bool success;
    DWORD errorCode;

    static ShellResult Ok() { return {true, ERROR_SUCCESS}; }
    static ShellResult Error(DWORD err) { return {false, err}; }
};

// File operation type for SHFileOperation
enum class ShellFileOp
{
    Move = FO_MOVE,
    Copy = FO_COPY,
    Delete = FO_DELETE,
    Rename = FO_RENAME
};

// Flags for file operations
enum ShellFileOpFlags : DWORD
{
    OpNoConfirmation = FOF_NOCONFIRMATION,
    OpSilent = FOF_SILENT,
    OpNoErrorUI = FOF_NOERRORUI,
    OpAllowUndo = FOF_ALLOWUNDO,
    OpFilesOnly = FOF_FILESONLY,
    OpNoRecursion = FOF_NORECURSION,
    OpNoConfirmMkDir = FOF_NOCONFIRMMKDIR
};

// Result of ShellExecute
struct ShellExecResult
{
    bool success;
    HINSTANCE hInstance;  // > 32 on success
    DWORD errorCode;

    static ShellExecResult Ok(HINSTANCE h) { return {true, h, ERROR_SUCCESS}; }
    static ShellExecResult Error(DWORD err) { return {false, nullptr, err}; }
};

// Options for ShellExecute
struct ShellExecInfo
{
    const wchar_t* file;         // File to execute
    const wchar_t* parameters;   // Command line parameters (optional)
    const wchar_t* verb;         // Operation: "open", "edit", "print", etc. (optional)
    const wchar_t* directory;    // Working directory (optional)
    int showCommand;             // SW_SHOW, SW_HIDE, etc.
    HWND hwnd;                   // Parent window for error dialogs

    ShellExecInfo()
        : file(nullptr)
        , parameters(nullptr)
        , verb(nullptr)
        , directory(nullptr)
        , showCommand(SW_SHOWNORMAL)
        , hwnd(nullptr)
    {
    }
};

// Abstract interface for shell operations
// Enables mocking for tests and centralized shell interaction
class IShell
{
public:
    virtual ~IShell() = default;

    // Execute a file/URL using shell
    virtual ShellExecResult Execute(const ShellExecInfo& info) = 0;

    // Perform file operations (copy, move, delete, rename) with shell UI
    // sourcePaths: Double-null terminated list of source paths
    // destPath: Destination path (for copy/move/rename)
    // Returns true if operation completed without errors
    virtual ShellResult FileOperation(ShellFileOp operation,
                                      const wchar_t* sourcePaths,
                                      const wchar_t* destPath,
                                      DWORD flags,
                                      HWND hwnd = nullptr) = 0;

    // Get file info (icon, type name, etc.)
    virtual ShellResult GetFileInfo(const wchar_t* path,
                                    DWORD attributes,
                                    SHFILEINFOW& info,
                                    UINT flags) = 0;

    // Browse for folder dialog
    virtual bool BrowseForFolder(HWND hwnd,
                                 const wchar_t* title,
                                 UINT flags,
                                 std::wstring& selectedPath) = 0;

    // Get special folder path (CSIDL_* constants)
    virtual ShellResult GetSpecialFolderPath(int csidl,
                                             std::wstring& path,
                                             bool create = false) = 0;
};

// Global shell interface - default is Win32 implementation
extern IShell* gShell;

// Returns the default Win32 implementation
IShell* GetWin32Shell();

// ANSI helpers for migration
inline std::wstring AnsiShellToWide(const char* str)
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

// ANSI helper: Execute file
inline ShellExecResult ShellExecuteA(IShell* shell, HWND hwnd, const char* verb,
                                     const char* file, const char* params,
                                     const char* dir, int showCmd)
{
    ShellExecInfo info;
    std::wstring wideVerb = verb ? AnsiShellToWide(verb) : L"";
    std::wstring wideFile = file ? AnsiShellToWide(file) : L"";
    std::wstring wideParams = params ? AnsiShellToWide(params) : L"";
    std::wstring wideDir = dir ? AnsiShellToWide(dir) : L"";

    info.verb = wideVerb.empty() ? nullptr : wideVerb.c_str();
    info.file = wideFile.empty() ? nullptr : wideFile.c_str();
    info.parameters = wideParams.empty() ? nullptr : wideParams.c_str();
    info.directory = wideDir.empty() ? nullptr : wideDir.c_str();
    info.showCommand = showCmd;
    info.hwnd = hwnd;

    return shell->Execute(info);
}
