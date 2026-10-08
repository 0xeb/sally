// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include <windows.h>
#include <shellapi.h>

namespace pictview
{

enum class RenameOverwriteDecision
{
    Yes,
    No,
    Cancel,
};

using RenameOverwritePrompt = RenameOverwriteDecision (*)(void* context,
                                                          HWND parent,
                                                          const wchar_t* sourcePath,
                                                          const wchar_t* targetPath);

using ShellFileOperation = int (*)(void* context, SHFILEOPSTRUCTW& operation);

// The confirmation RenameFileForViewer asks for when the new name already exists.
void SetRenameOverwritePrompt(RenameOverwritePrompt prompt, void* context);

bool DeleteFileForViewer(void* context,
                         HWND parent,
                         const wchar_t* path,
                         bool recycle,
                         bool& deleted,
                         bool& canceled);

bool DeleteFileForViewerWithShellOperation(void* context,
                                           HWND parent,
                                           const wchar_t* path,
                                           bool recycle,
                                           bool& deleted,
                                           bool& canceled,
                                           ShellFileOperation shellOperation,
                                           void* shellOperationContext);

bool RenameFileForViewer(void* context,
                         HWND parent,
                         const wchar_t* path,
                         const wchar_t* newName,
                         std::wstring& newPath,
                         bool& renamed,
                         DWORD& error);

bool RenameFileForViewerWithOverwritePrompt(void* context,
                                            HWND parent,
                                            const wchar_t* path,
                                            const wchar_t* newName,
                                            std::wstring& newPath,
                                            bool& renamed,
                                            DWORD& error,
                                            RenameOverwritePrompt overwritePrompt,
                                            void* overwritePromptContext);

bool CopyFileForViewer(void* context,
                       HWND parent,
                       const wchar_t* sourcePath,
                       const wchar_t* targetPath,
                       bool& copied,
                       bool& canceled);

bool CopyFileForViewerWithShellOperation(void* context,
                                         HWND parent,
                                         const wchar_t* sourcePath,
                                         const wchar_t* targetPath,
                                         bool& copied,
                                         bool& canceled,
                                         ShellFileOperation shellOperation,
                                         void* shellOperationContext);

} // namespace pictview
