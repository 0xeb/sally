// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "CreateDirectoryFlow.h"

#include "../consts.h"
#include "IFileSystem.h"
#include "fsutil.h"
#include "unicode/helpers.h"

namespace
{
IFileSystem* GetActiveFileSystem()
{
    return gFileSystem != NULL ? gFileSystem : GetWin32FileSystem();
}

bool IsManualCreateLeafInvalid(const wchar_t* path)
{
    if (path == NULL)
        return false;

    const wchar_t* name = wcsrchr(path, L'\\');
    name = name != NULL ? name + 1 : path;
    int nameLen = (int)wcslen(name);
    return nameLen > 0 && (*name <= L' ' || name[nameLen - 1] <= L' ' || name[nameLen - 1] == L'.');
}
} // namespace

namespace sally::filesystem
{
void NormalizeManualCreateInputW(std::wstring& inputPath)
{
    for (wchar_t& ch : inputPath)
    {
        if (ch == L'/')
            ch = L'\\';
    }
    RemoveDoubleBackslashesW(inputPath);

    if (!inputPath.empty())
    {
        wchar_t* writable = inputPath.data();
        wchar_t* leaf = wcsrchr(writable, L'\\');
        MakeValidFileNameComponentW(leaf != NULL ? leaf + 1 : writable);
    }
}

bool PrepareCreateDirectoryTargetW(const std::wstring& inputPath,
                                   const wchar_t* currentDir,
                                   CreateDirectoryPlan& plan,
                                   CreateDirectoryFailure* failure)
{
    std::wstring normalized = inputPath;
    NormalizeManualCreateInputW(normalized);

    int errTextId = 0;
    std::wstring nextFocus;
    if (!SalGetFullNameW(normalized, &errTextId, currentDir, &nextFocus, NULL, FALSE))
    {
        if (failure != NULL)
        {
            failure->stage = CreateDirectoryFailure::kResolve;
            failure->path = inputPath;
            failure->errorTextId = errTextId;
            failure->errorCode = 0;
        }
        return false;
    }

    std::wstring parentPath = normalized;
    if (!CutDirectoryW(parentPath))
    {
        if (failure != NULL)
        {
            failure->stage = CreateDirectoryFailure::kResolve;
            failure->path = normalized;
            failure->errorTextId = IDS_PATHISINVALID;
            failure->errorCode = ERROR_INVALID_NAME;
        }
        return false;
    }

    plan.fullPath = normalized;
    plan.parentPath = parentPath;
    plan.nextFocus = !nextFocus.empty() ? nextFocus : GetFileNameW(normalized.c_str());
    return true;
}

bool DirectoryExistsW(const std::wstring& path)
{
    if (path.empty())
        return false;

    DWORD attrs = GetActiveFileSystem()->GetFileAttributes(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsManualCreateLeafInvalidW(const std::wstring& path)
{
    return IsManualCreateLeafInvalid(path.c_str());
}

bool EnsureDirectoryTreeExistsW(const std::wstring& dirPath,
                                bool manualCreate,
                                std::wstring* firstCreatedDir,
                                CreateDirectoryFailure* failure)
{
    if (firstCreatedDir != NULL)
        firstCreatedDir->clear();

    if (dirPath.empty())
    {
        if (failure != NULL)
        {
            failure->stage = CreateDirectoryFailure::kParent;
            failure->path.clear();
            failure->errorCode = ERROR_INVALID_NAME;
            failure->errorTextId = IDS_PATHISINVALID;
        }
        return false;
    }

    IFileSystem* fileSystem = GetActiveFileSystem();
    DWORD attrs = fileSystem->GetFileAttributes(dirPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return true;

        if (failure != NULL)
        {
            failure->stage = CreateDirectoryFailure::kParent;
            failure->path = dirPath;
            failure->errorCode = ERROR_ALREADY_EXISTS;
            failure->errorTextId = 0;
        }
        return false;
    }

    std::wstring rootPath = GetRootPath(dirPath.c_str());
    if (rootPath.empty() || dirPath.size() <= rootPath.size())
    {
        if (failure != NULL)
        {
            failure->stage = CreateDirectoryFailure::kParent;
            failure->path = dirPath;
            failure->errorCode = ERROR_INVALID_NAME;
            failure->errorTextId = IDS_PATHISINVALID;
        }
        return false;
    }

    std::wstring existingPath = dirPath;
    while (!existingPath.empty())
    {
        attrs = fileSystem->GetFileAttributes(existingPath.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES)
            break;
        if (!CutDirectoryW(existingPath))
            break;
    }

    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        if (failure != NULL)
        {
            failure->stage = CreateDirectoryFailure::kParent;
            failure->path = dirPath;
            failure->errorCode = GetLastError();
            failure->errorTextId = IDS_CREATEDIRFAILED;
        }
        return false;
    }

    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        if (failure != NULL)
        {
            failure->stage = CreateDirectoryFailure::kParent;
            failure->path = existingPath;
            failure->errorCode = ERROR_ALREADY_EXISTS;
            failure->errorTextId = 0;
        }
        return false;
    }

    std::wstring current = existingPath;
    SalPathAddBackslashW(current);
    size_t currentLen = current.size();
    const wchar_t* tail = dirPath.c_str() + existingPath.size();
    if (*tail == L'\\')
        tail++;

    bool firstCreatedCaptured = false;
    while (*tail != 0)
    {
        bool invalidName = manualCreate && *tail <= L' ';
        const wchar_t* slash = wcschr(tail, L'\\');
        if (slash == NULL)
            slash = tail + wcslen(tail);

        current.append(tail, slash - tail);
        if (!current.empty() && (current.back() <= L' ' || current.back() == L'.'))
            invalidName = true;

        if (invalidName)
        {
            if (failure != NULL)
            {
                failure->stage = CreateDirectoryFailure::kParent;
                failure->path = current;
                failure->errorCode = ERROR_INVALID_NAME;
                failure->errorTextId = 0;
            }
            return false;
        }

        FileResult createResult = fileSystem->CreateDirectory(current.c_str());
        if (!createResult.success && createResult.errorCode != ERROR_ALREADY_EXISTS)
        {
            if (failure != NULL)
            {
                failure->stage = CreateDirectoryFailure::kParent;
                failure->path = current;
                failure->errorCode = createResult.errorCode;
                failure->errorTextId = 0;
            }
            return false;
        }

        if (createResult.success && firstCreatedDir != NULL && !firstCreatedCaptured)
        {
            *firstCreatedDir = current;
            firstCreatedCaptured = true;
        }

        current.push_back(L'\\');
        currentLen = current.size();
        tail = *slash == L'\\' ? slash + 1 : slash;
    }

    return true;
}
} // namespace sally::filesystem

BOOL SalCreateDirectoryExW(const wchar_t* name, DWORD* err)
{
    if (err != NULL)
        *err = 0;
    if (name == NULL || *name == 0)
    {
        if (err != NULL)
            *err = ERROR_INVALID_NAME;
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }

    IFileSystem* fileSystem = GetActiveFileSystem();
    std::wstring nameW(name);
    std::wstring createNameW = MakeCopyWithBackslashIfNeededW(name);
    bool nameUnmodified = (createNameW == nameW);

    FileResult initialCreate = fileSystem->CreateDirectory(createNameW.c_str());
    if (initialCreate.success)
        return TRUE;

    DWORD errLoc = initialCreate.errorCode;
    if (nameUnmodified &&
        (errLoc == ERROR_FILE_EXISTS || errLoc == ERROR_ALREADY_EXISTS))
    {
        WIN32_FIND_DATAW data;
        HANDLE find = fileSystem != NULL ? fileSystem->FindFirstFile(nameW.c_str(), &data)
                                         : INVALID_HANDLE_VALUE;
        if (find != INVALID_HANDLE_VALUE)
        {
            fileSystem->CloseFind(find);

            const wchar_t* targetNameW = SalPathFindFileNameW(name);

            // "The name we asked for matches an existing file's 8.3 ALIAS but
            // not its real name" - the collision this whole block works around. All three
            // operands were narrowed to CP_ACP first, which breaks the second test in the
            // direction that matters: two different long names that mangle to the same
            // '?'-string compare EQUAL, so the condition reads "the alias IS the real name",
            // the workaround is skipped, and creating the directory just fails. All three
            // have a wide form right here, so nothing needs the mirror.
            if (StrICmpW(targetNameW, data.cAlternateFileName) == 0 &&
                StrICmpW(targetNameW, data.cFileName) != 0)
            {
                std::wstring tmpNameW = nameW;
                CutDirectoryW(tmpNameW);
                SalPathAddBackslashW(tmpNameW);
                size_t tmpNamePartPos = tmpNameW.size();
                std::wstring origFullNameW = tmpNameW;
                SalPathAppendW(origFullNameW, data.cFileName);
                tmpNameW = origFullNameW;

                DWORD num = (GetTickCount() / 10) % 0xFFF;
                DWORD origFullNameAttr = fileSystem->GetFileAttributes(origFullNameW.c_str());
                while (1)
                {
                    wchar_t tmpSuffix[8];
                    swprintf(tmpSuffix, _countof(tmpSuffix), L"sal%03X", num++);
                    tmpNameW.resize(tmpNamePartPos);
                    tmpNameW += tmpSuffix;
                    FileResult tempMove = fileSystem->MoveFile(origFullNameW.c_str(), tmpNameW.c_str());
                    if (tempMove.success)
                        break;

                    DWORD moveErr = tempMove.errorCode;
                    if (moveErr != ERROR_FILE_EXISTS && moveErr != ERROR_ALREADY_EXISTS)
                    {
                        tmpNameW.clear();
                        break;
                    }
                }

                if (!tmpNameW.empty())
                {
                    BOOL createDirDone = fileSystem->CreateDirectory(nameW.c_str()).success;
                    if (!fileSystem->MoveFile(tmpNameW.c_str(), origFullNameW.c_str()).success)
                    {
                        TRACE_I("Unexpected situation: unable to rename file from tmp-name to original long file name!");
                        if (createDirDone)
                        {
                            if (fileSystem->RemoveDirectory(nameW.c_str()).success)
                                createDirDone = FALSE;
                            if (!fileSystem->MoveFile(tmpNameW.c_str(), origFullNameW.c_str()).success)
                                TRACE_E("Fatal unexpected situation: unable to rename file from tmp-name to original long file name!");
                        }
                    }
                    else if ((origFullNameAttr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                    {
                        fileSystem->SetFileAttributes(origFullNameW.c_str(), origFullNameAttr);
                    }

                    if (createDirDone)
                        return TRUE;
                }
            }
        }
    }

    if (err != NULL)
        *err = errLoc;
    return FALSE;
}
