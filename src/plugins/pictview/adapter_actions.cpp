// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "adapter_actions.h"
#include "rename_utils.h"

#include <cwchar>
#include <string>

namespace pictview
{
namespace
{

std::wstring DecorateLongPathForFileOperation(const std::wstring& path)
{
    if (path.rfind(L"\\\\?\\", 0) == 0)
        return path;
    if (path.rfind(L"\\\\", 0) == 0)
        return L"\\\\?\\UNC\\" + path.substr(2);
    if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        return L"\\\\?\\" + path;
    return path;
}

std::wstring BuildSiblingPathForRename(const wchar_t* path, const wchar_t* newName)
{
    if (path == nullptr || path[0] == L'\0' || newName == nullptr || newName[0] == L'\0')
        return std::wstring();

    std::wstring source(path);
    const size_t slash = source.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return std::wstring(newName);

    std::wstring target = source.substr(0, slash + 1);
    target += newName;
    return target;
}

// The plugin registers its overwrite dialog; without one an overwrite is never confirmed.
RenameOverwritePrompt RegisteredOverwritePrompt = nullptr;
void* RegisteredOverwritePromptContext = nullptr;

RenameOverwriteDecision PromptRenameOverwrite(void* context, HWND parent, const wchar_t* sourcePath, const wchar_t* targetPath)
{
    (void)context;
    if (RegisteredOverwritePrompt == nullptr)
        return RenameOverwriteDecision::Cancel;
    return RegisteredOverwritePrompt(RegisteredOverwritePromptContext, parent, sourcePath, targetPath);
}

int RunShellFileOperation(void* context, SHFILEOPSTRUCTW& operation)
{
    (void)context;
    return SHFileOperationW(&operation);
}

} // namespace

bool DeleteFileForViewer(void* context,
                         HWND parent,
                         const wchar_t* path,
                         bool recycle,
                         bool& deleted,
                         bool& canceled)
{
    return DeleteFileForViewerWithShellOperation(context,
                                                parent,
                                                path,
                                                recycle,
                                                deleted,
                                                canceled,
                                                RunShellFileOperation,
                                                nullptr);
}

bool DeleteFileForViewerWithShellOperation(void* context,
                                           HWND parent,
                                           const wchar_t* path,
                                           bool recycle,
                                           bool& deleted,
                                           bool& canceled,
                                           ShellFileOperation shellOperation,
                                           void* shellOperationContext)
{
    (void)context;

    deleted = false;
    canceled = false;
    if (path == nullptr || path[0] == L'\0')
        return false;

    std::wstring sourceList(path);
    sourceList.push_back(L'\0');

    SHFILEOPSTRUCTW operation = {};
    operation.hwnd = parent;
    operation.wFunc = FO_DELETE;
    operation.pFrom = sourceList.c_str();
    operation.fFlags = recycle ? FOF_ALLOWUNDO : 0;
    operation.lpszProgressTitle = L"";

    if (shellOperation == nullptr)
        shellOperation = RunShellFileOperation;

    const int result = shellOperation(shellOperationContext, operation);
    canceled = operation.fAnyOperationsAborted != FALSE;
    if (result != 0)
        return canceled;

    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
    {
        const DWORD error = GetLastError();
        deleted = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }

    return true;
}

bool RenameFileForViewer(void* context,
                         HWND parent,
                         const wchar_t* path,
                         const wchar_t* newName,
                         std::wstring& newPath,
                         bool& renamed,
                         DWORD& error)
{
    return RenameFileForViewerWithOverwritePrompt(context,
                                                 parent,
                                                 path,
                                                 newName,
                                                 newPath,
                                                 renamed,
                                                 error,
                                                 PromptRenameOverwrite,
                                                 nullptr);
}

bool RenameFileForViewerWithOverwritePrompt(void* context,
                                            HWND parent,
                                            const wchar_t* path,
                                            const wchar_t* newName,
                                            std::wstring& newPath,
                                            bool& renamed,
                                            DWORD& error,
                                            RenameOverwritePrompt overwritePrompt,
                                            void* overwritePromptContext)
{
    (void)context;

    newPath.clear();
    renamed = false;
    error = ERROR_SUCCESS;

    if (newName == nullptr || newName[0] == L'\0' ||
        FileNameContainsInvalidRenameCharacter(newName))
    {
        error = ERROR_INVALID_NAME;
        return false;
    }

    std::wstring target = BuildSiblingPathForRename(path, newName);
    if (target.empty())
    {
        error = ERROR_INVALID_NAME;
        return false;
    }

    if (std::wstring(path) == target)
    {
        newPath = std::move(target);
        return true;
    }

    const std::wstring sourceForMove = DecorateLongPathForFileOperation(path);
    const std::wstring targetForMove = DecorateLongPathForFileOperation(target);
    if (!MoveFileExW(sourceForMove.c_str(), targetForMove.c_str(), 0))
    {
        error = GetLastError();
        if (error == ERROR_SUCCESS)
            error = ERROR_INVALID_NAME;

        if ((error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) &&
            _wcsicmp(path, target.c_str()) != 0)
        {
            const DWORD sourceAttributes = GetFileAttributesW(sourceForMove.c_str());
            const DWORD targetAttributes = GetFileAttributesW(targetForMove.c_str());
            if (sourceAttributes != INVALID_FILE_ATTRIBUTES &&
                targetAttributes != INVALID_FILE_ATTRIBUTES &&
                (sourceAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                (targetAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                if (overwritePrompt == nullptr)
                    overwritePrompt = PromptRenameOverwrite;

                const RenameOverwriteDecision answer = overwritePrompt(overwritePromptContext,
                                                                        parent,
                                                                        path,
                                                                        target.c_str());
                if (answer != RenameOverwriteDecision::Yes)
                {
                    error = ERROR_SUCCESS;
                    return true;
                }

                if ((targetAttributes & FILE_ATTRIBUTE_READONLY) != 0)
                {
                    const DWORD writableAttributes = targetAttributes & ~FILE_ATTRIBUTE_READONLY;
                    if (!SetFileAttributesW(targetForMove.c_str(), writableAttributes))
                    {
                        error = GetLastError();
                        if (error == ERROR_SUCCESS)
                            error = ERROR_ACCESS_DENIED;
                        return false;
                    }
                }

                if (!MoveFileExW(sourceForMove.c_str(), targetForMove.c_str(), MOVEFILE_REPLACE_EXISTING))
                {
                    error = GetLastError();
                    if (error == ERROR_SUCCESS)
                        error = ERROR_ACCESS_DENIED;
                    return false;
                }

                renamed = true;
                newPath = std::move(target);
                error = ERROR_SUCCESS;
                return true;
            }
        }
        return false;
    }

    renamed = true;
    newPath = std::move(target);
    return true;
}

bool CopyFileForViewer(void* context,
                       HWND parent,
                       const wchar_t* sourcePath,
                       const wchar_t* targetPath,
                       bool& copied,
                       bool& canceled)
{
    return CopyFileForViewerWithShellOperation(context,
                                              parent,
                                              sourcePath,
                                              targetPath,
                                              copied,
                                              canceled,
                                              RunShellFileOperation,
                                              nullptr);
}

bool CopyFileForViewerWithShellOperation(void* context,
                                         HWND parent,
                                         const wchar_t* sourcePath,
                                         const wchar_t* targetPath,
                                         bool& copied,
                                         bool& canceled,
                                         ShellFileOperation shellOperation,
                                         void* shellOperationContext)
{
    (void)context;

    copied = false;
    canceled = false;
    if (sourcePath == nullptr || sourcePath[0] == L'\0' ||
        targetPath == nullptr || targetPath[0] == L'\0')
    {
        return false;
    }

    std::wstring sourceList(sourcePath);
    sourceList.push_back(L'\0');

    std::wstring targetList(targetPath);
    targetList.push_back(L'\0');

    SHFILEOPSTRUCTW operation = {};
    operation.hwnd = parent;
    operation.wFunc = FO_COPY;
    operation.pFrom = sourceList.c_str();
    operation.pTo = targetList.c_str();
    operation.lpszProgressTitle = L"";

    if (shellOperation == nullptr)
        shellOperation = RunShellFileOperation;

    const int result = shellOperation(shellOperationContext, operation);
    canceled = operation.fAnyOperationsAborted != FALSE;
    if (result != 0)
        return canceled;

    copied = !canceled && GetFileAttributesW(targetPath) != INVALID_FILE_ATTRIBUTES;
    return true;
}

void SetRenameOverwritePrompt(RenameOverwritePrompt prompt, void* context)
{
    RegisteredOverwritePrompt = prompt;
    RegisteredOverwritePromptContext = context;
}

} // namespace pictview
