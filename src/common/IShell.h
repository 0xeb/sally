// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

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

struct FolderPickerOptions
{
    HWND owner = nullptr;
    const wchar_t* title = nullptr;
    const wchar_t* instruction = nullptr;
    const wchar_t* initialDirectory = nullptr;
    // Start the picker AT Network (the drive bar's Network Neighborhood button). It biases where
    // the dialog opens; it does not restrict what the user may pick. The BIF_RETURNONLYFSDIRS
    // browse tree it replaces was ROOTED at Network so navigating out was impossible, and an
    // IFileOpenDialog cannot be rooted that way - rejecting a non-UNC choice afterwards only made
    // the Select button behave like Cancel, with nothing shown. Every caller navigates the panel
    // to whatever comes back, and a local path is perfectly valid there.
    bool networkOnly = false;
};

// Abstract interface for shell operations
// Enables mocking for tests and centralized shell interaction
class IShell
{
public:
    virtual ~IShell() = default;

    // Execute a file/URL using shell
    virtual ShellExecResult Execute(const ShellExecInfo& info) = 0;

    // Perform file operations with typed shell items. Semantic paths stay dynamically owned;
    // the adapter creates the shell items and owns all COM lifetimes.
    virtual ShellResult FileOperation(ShellFileOp operation,
                                      const std::vector<std::wstring>& sourcePaths,
                                      const std::wstring& destPath,
                                      DWORD flags,
                                      HWND hwnd = nullptr) = 0;

    // Get file info (icon, type name, etc.)
    virtual ShellResult GetFileInfo(const wchar_t* path,
                                    DWORD attributes,
                                    SHFILEINFOW& info,
                                    UINT flags) = 0;

    // Pick a filesystem folder with IFileDialog/IShellItem. The selected value is published only
    // after the shell returns a complete filesystem path.
    virtual ShellResult PickFolder(const FolderPickerOptions& options,
                                   std::wstring& selectedPath) = 0;

    // Resolve a KNOWNFOLDERID into its dynamically allocated filesystem path. The default keeps
    // existing test/fake shells source-compatible; the Win32 adapter loads the Vista API at
    // runtime so this interface does not raise Sally's minimum-OS import floor.
    virtual ShellResult GetKnownFolderPath(const GUID& folderId, std::wstring& path)
    { (void)folderId; (void)path; return ShellResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Resolves a shell item identifier to its exact filesystem path. The owner is
    // dynamic because PIDLs can identify paths beyond the legacy MAX_PATH contract.
    virtual ShellResult GetFileSystemPathFromIdList(LPCITEMIDLIST itemId, std::wstring& path)
    { (void)itemId; (void)path; return ShellResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }

    // Move files/dirs to the Recycle Bin. Silent (no shell UI/confirm);
    // the caller owns confirmation and error display. parentWnd parents any
    // shell-internal window (the worker passes its CShellExecuteWnd).
    // On failure errorCode may be a shell DE_* code (0x71-0x88) rather than a
    // Win32 error - display-only; do not feed it to FormatMessage-style APIs.
    // A user abort maps to ERROR_CANCELLED.
    // Default fails with CALL_NOT_IMPLEMENTED so mocks need not override.
    virtual ShellResult MoveToRecycleBin(const std::vector<std::wstring>& paths,
                                         HWND parentWnd = NULL)
    { (void)paths; (void)parentWnd; return ShellResult::Error(ERROR_CALL_NOT_IMPLEMENTED); }
};

// Global shell interface - default is Win32 implementation
extern IShell* gShell;

// Returns the default Win32 implementation
IShell* GetWin32Shell();
