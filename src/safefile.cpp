// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "usermenu.h"
#include "execute.h"
#include "cfgdlg.h"
#include "zip.h"
#include "spl_file.h"
#include "common/IFileSystem.h"
#include "common/fsutil.h"
#include "common/SafeFilePathContext.h"
#include "ui/IPrompter.h"
#include "common/widepath.h"

CSalamanderSafeFile SalSafeFile;

namespace
{
const wchar_t* SafeFileNameW(const SAFE_FILE* file)
{
    return file != NULL && file->FileName != NULL ? file->FileName : L"";
}

size_t GetSafeFileRootLengthW(const std::wstring& path)
{
    if (path.size() >= 8 && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\' &&
        (path[4] == L'U' || path[4] == L'u') &&
        (path[5] == L'N' || path[5] == L'n') &&
        (path[6] == L'C' || path[6] == L'c') &&
        path[7] == L'\\')
    {
        size_t serverEnd = path.find(L'\\', 8);
        if (serverEnd == std::wstring::npos)
            return path.size();
        size_t shareEnd = path.find(L'\\', serverEnd + 1);
        return shareEnd == std::wstring::npos ? path.size() : shareEnd + 1;
    }

    if (path.size() >= 7 && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\' &&
        path[5] == L':' && (path[6] == L'\\' || path[6] == L'/'))
    {
        return 7;
    }

    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        size_t serverEnd = path.find(L'\\', 2);
        if (serverEnd == std::wstring::npos)
            return path.size();
        size_t shareEnd = path.find(L'\\', serverEnd + 1);
        return shareEnd == std::wstring::npos ? path.size() : shareEnd + 1;
    }

    if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        return 3;

    if (!path.empty() && (path[0] == L'\\' || path[0] == L'/'))
        return 1;

    return 0;
}

std::wstring GetSafeFileParentPathW(const std::wstring& path)
{
    size_t rootLen = GetSafeFileRootLengthW(path);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash < rootLen)
        return std::wstring();
    if (slash + 1 == rootLen)
        return path.substr(0, rootLen);
    return path.substr(0, slash);
}

BOOL EnsureSafeFileDirectoryPathW(const sally::safe_file::PathContext& path,
                                  BOOL isDir,
                                  HWND hParent,
                                  DWORD* silentMask,
                                  BOOL allowSkip,
                                  BOOL* skipped,
                                  wchar_t* skipPath,
                                  int skipPathMax)
{
    std::wstring target = isDir ? path.WideNameRef() : GetSafeFileParentPathW(path.WideNameRef());
    if (target.empty())
        return TRUE;

    DWORD attrs = gFileSystem->GetFileAttributes(target.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

    size_t rootLen = GetSafeFileRootLengthW(target);
    if (target.size() <= rootLen)
    {
        int ret;
        if (silentMask != NULL && (*silentMask & SILENT_SKIP_DIR_CREATE) && allowSkip)
            ret = DIALOG_SKIP;
        else
            ret = DialogError(hParent, allowSkip ? BUTTONS_SKIPCANCEL : BUTTONS_OK, path.WideNameW(),
                              LoadStrW(IDS_ERRORCREATINGROOTDIR), LoadStrW(IDS_ERRORCREATINGDIR));
        switch (ret)
        {
        case DIALOG_SKIPALL:
            if (silentMask != NULL)
                *silentMask |= SILENT_SKIP_DIR_CREATE;
        case DIALOG_SKIP:
            if (skipped != NULL)
                *skipped = TRUE;
            if (skipPath != NULL)
                lstrcpynW(skipPath, path.WideNameW(), skipPathMax);
        }
        return FALSE;
    }

    std::wstring current = target.substr(0, rootLen);
    size_t pos = rootLen;
    while (pos < target.size())
    {
        while (pos < target.size() && (target[pos] == L'\\' || target[pos] == L'/'))
            ++pos;
        if (pos >= target.size())
            break;

        size_t next = target.find_first_of(L"\\/", pos);
        std::wstring component = next == std::wstring::npos ? target.substr(pos) : target.substr(pos, next - pos);
        if (!current.empty() && current[current.size() - 1] != L'\\' && current[current.size() - 1] != L'/')
            current += L'\\';
        current += component;

        BOOL invalidPath = !component.empty() && (component[component.size() - 1] <= L' ' || component[component.size() - 1] == L'.');
        while (TRUE)
        {
            DWORD existingAttrs = gFileSystem->GetFileAttributes(current.c_str());
            if (existingAttrs != INVALID_FILE_ATTRIBUTES)
            {
                if (existingAttrs & FILE_ATTRIBUTE_DIRECTORY)
                    break;

                int ret;
                if (silentMask != NULL && (*silentMask & SILENT_SKIP_DIR_NAMEUSED) && allowSkip)
                    ret = DIALOG_SKIP;
                else
                    ret = DialogError(hParent, allowSkip ? BUTTONS_RETRYSKIPCANCEL : BUTTONS_RETRYCANCEL,
                                      current.c_str(), LoadStrW(IDS_NAMEALREADYUSED), LoadStrW(IDS_ERRORCREATINGDIR));
                switch (ret)
                {
                case DIALOG_SKIPALL:
                    if (silentMask != NULL)
                        *silentMask |= SILENT_SKIP_DIR_NAMEUSED;
                case DIALOG_SKIP:
                    if (skipped != NULL)
                        *skipped = TRUE;
                    if (skipPath != NULL)
                        lstrcpynW(skipPath, current.c_str(), skipPathMax);
                    return FALSE;
                case DIALOG_CANCEL:
                case DIALOG_FAIL:
                    return FALSE;
                }
                continue;
            }

            FileResult createResult = invalidPath
                                          ? FileResult::Error(ERROR_INVALID_NAME)
                                          : gFileSystem->CreateDirectory(current.c_str());
            if (createResult.success)
                break;

            DWORD err = createResult.errorCode;
            int ret;
            if (silentMask != NULL && (*silentMask & SILENT_SKIP_DIR_CREATE) && allowSkip)
                ret = DIALOG_SKIP;
            else
                ret = DialogError(hParent, allowSkip ? BUTTONS_RETRYSKIPCANCEL : BUTTONS_RETRYCANCEL,
                                  current.c_str(), ::GetErrorTextOwned(err).c_str(), LoadStrW(IDS_ERRORCREATINGDIR));
            switch (ret)
            {
            case DIALOG_SKIPALL:
                if (silentMask != NULL)
                    *silentMask |= SILENT_SKIP_DIR_CREATE;
            case DIALOG_SKIP:
                if (skipped != NULL)
                    *skipped = TRUE;
                if (skipPath != NULL)
                    lstrcpynW(skipPath, current.c_str(), skipPathMax);
                return FALSE;
            case DIALOG_CANCEL:
            case DIALOG_FAIL:
                return FALSE;
            }
        }

        if (next == std::wstring::npos)
            break;
        pos = next + 1;
    }

    return TRUE;
}
} // namespace

//*****************************************************************************
//
// CSalamanderSafeFile
//

static BOOL SafeFileOpenWithContext(SAFE_FILE* file,
                                    const sally::safe_file::PathContext& path,
                                    DWORD dwDesiredAccess,
                                    DWORD dwShareMode,
                                    DWORD dwCreationDisposition,
                                    DWORD dwFlagsAndAttributes,
                                    HWND hParent,
                                    DWORD flags,
                                    DWORD* pressedButton,
                                    DWORD* silentMask)
{
    CALL_STACK_MESSAGE7("CSalamanderSafeFile::SafeFileOpen(, %ls, %u, %u, %u, %u, , %u, ,)",
                        path.DisplayNameW(), dwDesiredAccess, dwShareMode, dwCreationDisposition,
                        dwFlagsAndAttributes, flags);

    // for errors such as LOW_MEMORY we want the operation to abort entirely
    if (pressedButton != NULL)
        *pressedButton = DIALOG_CANCEL;

    HANDLE hFile;
    do
    {
        hFile = gFileSystem->CreateFile(path.WideNameW(), dwDesiredAccess, dwShareMode, NULL,
                                        dwCreationDisposition, dwFlagsAndAttributes, NULL);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            DWORD dlgRet;
            if (silentMask != NULL && (*silentMask & SILENT_SKIP_FILE_OPEN) && ButtonsContainsSkip(flags))
                dlgRet = DIALOG_SKIP;
            else
            {
                DWORD lastError = GetLastError();
                dlgRet = DialogError(hParent, (flags & BUTTONS_MASK), path.WideNameW(),
                                     GetErrorTextOwned(lastError).c_str(), LoadStrW(IDS_ERROROPENINGFILE));
            }
            switch (dlgRet)
            {
            case DIALOG_RETRY:
                break;
            case DIALOG_SKIPALL:
                if (silentMask != NULL)
                    *silentMask |= SILENT_SKIP_FILE_OPEN;
            case DIALOG_SKIP:
            default:
            {
                if (pressedButton != NULL)
                    *pressedButton = dlgRet;
                return FALSE;
            }
            }
        }
    } while (hFile == INVALID_HANDLE_VALUE);

    // everything is OK - populate the context structure
    file->FileName = DupStr(path.WideNameW());
    if (file->FileName == NULL)
    {
        TRACE_E(LOW_MEMORY);
        (void)gFileSystem->CloseFileHandle(hFile);
        return FALSE;
    }
    file->HFile = hFile;
    file->HParentWnd = hParent;
    file->dwDesiredAccess = dwDesiredAccess;
    file->dwShareMode = dwShareMode;
    file->dwCreationDisposition = dwCreationDisposition;
    file->dwFlagsAndAttributes = dwFlagsAndAttributes;
    file->WholeFileAllocated = FALSE;
    return TRUE;
}

BOOL CSalamanderSafeFile::SafeFileOpen(SAFE_FILE* file,
                                       const wchar_t* fileName,
                                       DWORD dwDesiredAccess,
                                       DWORD dwShareMode,
                                       DWORD dwCreationDisposition,
                                       DWORD dwFlagsAndAttributes,
                                       HWND hParent,
                                       DWORD flags,
                                       DWORD* pressedButton,
                                       DWORD* silentMask)
{
    return SafeFileOpenWithContext(file,
                                   sally::safe_file::PathContext(fileName),
                                   dwDesiredAccess,
                                   dwShareMode,
                                   dwCreationDisposition,
                                   dwFlagsAndAttributes,
                                   hParent,
                                   flags,
                                   pressedButton,
                                   silentMask);
}

BOOL CSalamanderSafeFile::SafeFileOpenW(SAFE_FILE* file,
                                        const wchar_t* fileName,
                                        const wchar_t* displayFileName,
                                        DWORD dwDesiredAccess,
                                        DWORD dwShareMode,
                                        DWORD dwCreationDisposition,
                                        DWORD dwFlagsAndAttributes,
                                        HWND hParent,
                                        DWORD flags,
                                        DWORD* pressedButton,
                                        DWORD* silentMask)
{
    return SafeFileOpenWithContext(file,
                                   sally::safe_file::PathContext(fileName, displayFileName),
                                   dwDesiredAccess,
                                   dwShareMode,
                                   dwCreationDisposition,
                                   dwFlagsAndAttributes,
                                   hParent,
                                   flags,
                                   pressedButton,
                                   silentMask);
}

static HANDLE SafeFileCreateWithContext(const sally::safe_file::PathContext& path,
                                        DWORD dwDesiredAccess,
                                        DWORD dwShareMode,
                                        DWORD dwFlagsAndAttributes,
                                        BOOL isDir,
                                        HWND hParent,
                                        const wchar_t* srcFileName,
                                        const wchar_t* srcFileInfo,
                                        DWORD* silentMask,
                                        BOOL allowSkip,
                                        BOOL* skipped,
                                        wchar_t* skipPath,
                                        int skipPathMax,
                                        CQuadWord* allocateWholeFile,
                                        SAFE_FILE* file)
{
    CALL_STACK_MESSAGE7("CSalamanderGeneral::SafeFileCreate(%ls, %u, %u, %u, %d, , , , %d)",
                        path.DisplayNameW(), dwDesiredAccess, dwShareMode, dwFlagsAndAttributes, isDir, allowSkip);
    dwFlagsAndAttributes &= 0xFFFF0000 | FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
                            FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY |
                            FILE_ATTRIBUTE_ARCHIVE;
    if (skipped != NULL)
        *skipped = FALSE;
    if (skipPath != NULL && skipPathMax > 0)
        *skipPath = 0;
    BOOL wholeFileAllocated = FALSE;
    BOOL needWholeAllocTest = FALSE; // we must verify that the pointer can be set and the data are not appended to the end of the file
    if (allocateWholeFile != NULL &&
        *allocateWholeFile >= CQuadWord(0, 0x80000000))
    {
        *allocateWholeFile -= CQuadWord(0, 0x80000000);
        needWholeAllocTest = TRUE;
    }

    // check whether the target already exists
    DWORD attrs;
    HANDLE hFile;
    while (1)
    {
        attrs = sally::safe_file::GetFileAttributesExact(path);
        if (attrs == 0xFFFFFFFF)
            break;

        // it already exists; we'll check whether it's just a collision with a DOS-style name (the full name of the existing file/directory is different)
        if (!isDir)
        {
            WIN32_FIND_DATAW data;
            HANDLE find = SalFindFirstFileHW(path.WideNameW(), &data);
            if (find != INVALID_HANDLE_VALUE)
            {
                SalLPFindClose(find);
                const wchar_t* tgtNameW = SalPathFindFileNameW(path.WideNameW());
                if (_wcsicmp(tgtNameW, data.cAlternateFileName) == 0 && // match only for the DOS name
                    _wcsicmp(tgtNameW, data.cFileName) != 0)   // (the full name is different)
                {
                    // rename ("clean up") the file/directory with the conflicting DOS name to a temporary 8.3 name (which doesn't require an extra DOS name)
                    std::wstring temporaryDirectory(path.WideNameW());
                    const size_t separator = temporaryDirectory.find_last_of(L'\\');
                    temporaryDirectory.resize(separator == std::wstring::npos ? 0 : separator + 1);
                    // data.cFileName is the genuine wide name the collision was
                    // detected against; use it directly rather than round-tripping through the
                    // lossy narrow cFileNameA mirror.
                    std::wstring origFullName = temporaryDirectory + data.cFileName;
                    if (!origFullName.empty())
                    {
                        DWORD num = (GetTickCount() / 10) % 0xFFF;
                        std::wstring tmpName;
                        while (1)
                        {
                            tmpName = temporaryDirectory + FormatStrW(L"sal%03X", num++);
                            if (::SalMoveFile(origFullName.c_str(), tmpName.c_str()))
                                break;
                            DWORD e = GetLastError();
                            if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS)
                            {
                                tmpName.clear();
                                break;
                            }
                        }
                        if (!tmpName.empty()) // if we managed to "clean up" the conflicting file/directory, try creating the target
                        {                    // file/directory and then restore the original name to the "cleaned" file/directory
                            hFile = INVALID_HANDLE_VALUE;
                            //              if (!isDir)   // file
                            //              {       // add the handle to HANDLES at the end only if the SAFE_FILE structure is being filled
                            hFile = NOHANDLES(::sally::safe_file::CreateFileExact(path, dwDesiredAccess, dwShareMode, NULL,
                                                                                  CREATE_NEW, dwFlagsAndAttributes, NULL));
                            //              }
                            //              else   // directory
                            //              {
                            //                if (CreateDirectory(fileName, NULL)) out = (void *)1;  // on success we must return something other than INVALID_HANDLE_VALUE
                            //              }
                            if (!::SalMoveFile(tmpName.c_str(), origFullName.c_str()))
                            { // this can apparently happen; inexplicably, Windows creates a file named origFullName instead of 'fileName' (the DOS name)
                                TRACE_IW(L"Unexpected situation in CSalamanderGeneral::SafeCreateFile(): unable to rename file from tmp-name to original long file name! " << origFullName.c_str());

                                if (hFile != INVALID_HANDLE_VALUE)
                                {
                                    //                  if (!isDir)
                                    (void)gFileSystem->CloseFileHandle(hFile);
                                    hFile = INVALID_HANDLE_VALUE;
                                    //                  if (!isDir)
                                    gFileSystem->DeleteFile(path.WideNameW());
                                    //                  else RemoveDirectory(fileName);
                                    if (!::SalMoveFile(tmpName.c_str(), origFullName.c_str()))
                                        TRACE_EW(L"Fatal unexpected situation in CSalamanderGeneral::SafeCreateFile(): unable to rename file from tmp-name to original long file name! " << origFullName.c_str());
                                }
                            }
                            if (hFile != INVALID_HANDLE_VALUE)
                                goto SUCCESS; // return only on success; errors are handled later (ignore the DOS-name conflict)
                        }
                    }
                }
            }
        }

        // it already exists, but what is it?
        if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        {
            int ret;
            // it is a directory
            if (isDir)
            {
                // if we wanted a directory, that is fine
                // and return anything other than INVALID_HANDLE_VALUE
                return (void*)1;
            }
            // otherwise report an error
            if (silentMask != NULL && (*silentMask & SILENT_SKIP_FILE_NAMEUSED) && allowSkip)
                ret = DIALOG_SKIP;
            else
            {
                // ERROR: filename+error, buttons retry/skip/skip all/cancel
                ret = DialogError(hParent, allowSkip ? BUTTONS_RETRYSKIPCANCEL : BUTTONS_RETRYCANCEL,
                                  path.WideNameW(), LoadStrW(IDS_NAMEALREADYUSEDFORDIR), LoadStrW(IDS_ERRORCREATINGFILE));
            }
            switch (ret)
            {
            case DIALOG_SKIPALL:
                if (silentMask != NULL)
                    *silentMask |= SILENT_SKIP_FILE_NAMEUSED;
                // no break here
            case DIALOG_SKIP:
                if (skipped != NULL)
                    *skipped = TRUE;
                return INVALID_HANDLE_VALUE;
            case DIALOG_CANCEL:
            case DIALOG_FAIL:
                return INVALID_HANDLE_VALUE;
            }
        }
        else
        {
            int ret;
            // it is a file, check whether it can be overwritten
            if (isDir)
            {
                // we are trying to create a directory, but there is already a file with the same name in the place -- report an error
                if (silentMask != NULL && (*silentMask & SILENT_SKIP_DIR_NAMEUSED) && allowSkip)
                    ret = DIALOG_SKIP;
                else
                {
                    // ERROR: filename+error, buttons retry/skip/skip all/cancel
                    ret = DialogError(hParent, allowSkip ? BUTTONS_RETRYSKIPCANCEL : BUTTONS_RETRYCANCEL,
                                      path.WideNameW(), LoadStrW(IDS_NAMEALREADYUSED), LoadStrW(IDS_ERRORCREATINGDIR));
                }
                switch (ret)
                {
                case DIALOG_SKIPALL:
                    if (silentMask != NULL)
                        *silentMask |= SILENT_SKIP_DIR_NAMEUSED;
                    // no break here
                case DIALOG_SKIP:
                    if (skipped != NULL)
                        *skipped = TRUE;
                    if (skipPath != NULL)
                        lstrcpynW(skipPath, path.WideNameW(), skipPathMax); // the user wants to return the skipped path
                    return INVALID_HANDLE_VALUE;
                case DIALOG_CANCEL:
                case DIALOG_FAIL:
                    return INVALID_HANDLE_VALUE;
                    // else retry
                }
            }
            else
            {
                // ask whether to overwrite
                if ((srcFileName != NULL && !Configuration.CnfrmFileOver) || (silentMask != NULL && (*silentMask & SILENT_OVERWRITE_FILE_EXIST)))
                    ret = DIALOG_YES;
                else if (silentMask != NULL && (*silentMask & SILENT_SKIP_FILE_EXIST) && allowSkip)
                    ret = DIALOG_SKIP;
                else
                {
                    wchar_t fibuffer[500];
                    HANDLE file2 = gFileSystem->CreateFile(path.WideNameW(), 0,
                                                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (file2 != INVALID_HANDLE_VALUE)
                    {
                        GetFileOverwriteInfoW(fibuffer, _countof(fibuffer), file2, path.WideNameW());
                        gFileSystem->CloseFileHandle(file2);
                    }
                    else
                        wcscpy_s(fibuffer, LoadStrW(IDS_ERR_FILEOPEN));
                    if (srcFileName != NULL)
                    {
                        // CONFIRM FILE OVERWRITE: filename1+filedata1+filename2+filedata2, buttons yes/all/skip/skip all/cancel
                        // srcFileName/srcFileInfo are now wide at both
                        // SafeFileCreate entry points (the earlier comment describing them as
                        // "narrow BY CONSTRUCTION" is stale - both the ANSI and wide entry points
                        // now supply wide directly).
                        ret = DialogOverwrite(hParent, allowSkip ? BUTTONS_YESALLSKIPCANCEL : BUTTONS_YESALLCANCEL,
                                              path.WideNameW(), fibuffer,
                                              srcFileName, srcFileInfo);
                    }
                    else
                    {
                        // CONFIRM FILE OVERWRITE: filename1+filedata1+a newly created file, buttons yes/all/skip/skip all/cancel
                        ret = DialogQuestion(hParent, allowSkip ? BUTTONS_YESALLSKIPCANCEL : BUTTONS_YESNOCANCEL,
                                             path.WideNameW(), LoadStrW(IDS_NEWLYCREATEDFILE), LoadStrW(IDS_CONFIRMFILEOVERWRITING));
                    }
                }
                switch (ret)
                {
                case DIALOG_SKIPALL:
                    if (silentMask != NULL)
                        *silentMask |= SILENT_SKIP_FILE_EXIST;
                    // no break here
                case DIALOG_SKIP:
                    if (skipped != NULL)
                        *skipped = TRUE;
                    return INVALID_HANDLE_VALUE;
                case DIALOG_CANCEL:
                case DIALOG_NO:
                case DIALOG_FAIL:
                    return INVALID_HANDLE_VALUE;
                case DIALOG_ALL:
                    ret = DIALOG_YES;
                    if (silentMask != NULL)
                        *silentMask |= SILENT_OVERWRITE_FILE_EXIST;
                    break;
                }
                if (ret == DIALOG_YES)
                {
                    // we will overwrite - clear the attributes
                    if (attrs & FILE_ATTRIBUTE_HIDDEN ||
                        attrs & FILE_ATTRIBUTE_SYSTEM ||
                        attrs & FILE_ATTRIBUTE_READONLY)
                    {
                        // for files without hidden and system attributes, the second (hidden+system) confirmation is not shown
                        if (srcFileName == NULL || !Configuration.CnfrmSHFileOver ||
                            (silentMask != NULL && (*silentMask & SILENT_OVERWRITE_FILE_SYSHID)) ||
                            ((attrs & FILE_ATTRIBUTE_HIDDEN) == 0 && (attrs & FILE_ATTRIBUTE_SYSTEM) == 0))
                            ret = DIALOG_YES;
                        else if (silentMask != NULL && (*silentMask & SILENT_SKIP_FILE_SYSHID) && allowSkip)
                            ret = DIALOG_SKIP;
                        else
                            // QUESTION: filename+question, buttons yes/all/skip/skip all/cancel
                            ret = DialogQuestion(hParent, allowSkip ? BUTTONS_YESALLSKIPCANCEL : BUTTONS_YESALLCANCEL,
                                                 path.WideNameW(), LoadStrW(IDS_WANTOVERWRITESHFILE), LoadStrW(IDS_CONFIRMFILEOVERWRITING));
                        switch (ret)
                        {
                        case DIALOG_SKIPALL:
                            if (silentMask != NULL)
                                *silentMask |= SILENT_SKIP_FILE_SYSHID;
                            // no break here
                        case DIALOG_SKIP:
                            if (skipped != NULL)
                                *skipped = TRUE;
                            return INVALID_HANDLE_VALUE;
                        case DIALOG_CANCEL:
                        case DIALOG_FAIL:
                            return INVALID_HANDLE_VALUE;
                        case DIALOG_ALL:
                            ret = DIALOG_YES;
                            if (silentMask != NULL)
                                *silentMask |= SILENT_OVERWRITE_FILE_SYSHID;
                            break;
                        }
                        if (ret == DIALOG_YES)
                        {
                            sally::safe_file::SetFileAttributesExact(path, FILE_ATTRIBUTE_NORMAL);
                            break;
                        }
                    }
                    else
                        break;
                }
            }
        }
    }

    if (attrs == 0xFFFFFFFF &&
        !EnsureSafeFileDirectoryPathW(path, isDir, hParent, silentMask, allowSkip,
                                      skipped, skipPath, skipPathMax))
    {
        return INVALID_HANDLE_VALUE;
    }

CREATE_FILE:
    // if it is a file, create it
    if (!isDir)
    { // add the handle to HANDLES at the end only if the SAFE_FILE structure is being filled
        while ((hFile = NOHANDLES(::sally::safe_file::CreateFileExact(path, dwDesiredAccess, dwShareMode, NULL,
                                                                      CREATE_ALWAYS, dwFlagsAndAttributes, NULL))) == INVALID_HANDLE_VALUE)
        {
            DWORD err = GetLastError();
            // handles the situation when a file needs to be overwritten on Samba:
            // the file has permissions 440+different_owner and is in a directory where the current user can write to
            // (it can be deleted, but not overwritten directly (cannot be opened for writing) - we work around it:
            //  delete and create the file again)
            // (on Samba it is possible to allow deleting read-only files, which allows deleting a read-only file,
            //  otherwise it cannot be deleted because Windows cannot delete a read-only file and at the same time
            //  the "read-only" attribute cannot be cleared on that file because the current user is not the owner)
            if (gFileSystem->DeleteFile(path.WideNameW()).success) // if it is read-only, it can be deleted only on Samba with "delete readonly" allowed
            {                         // add the handle to HANDLES at the end only if the SAFE_FILE structure is being filled
                hFile = NOHANDLES(::sally::safe_file::CreateFileExact(path, dwDesiredAccess, dwShareMode, NULL,
                                                                      CREATE_ALWAYS, dwFlagsAndAttributes, NULL));
                if (hFile != INVALID_HANDLE_VALUE)
                    break;
                err = GetLastError();
            }

            int ret;
            if (silentMask != NULL && (*silentMask & SILENT_SKIP_FILE_CREATE) && allowSkip)
                ret = DIALOG_SKIP;
            else
            {
                // ERROR: filename+error, buttons retry/skip/skip all/cancel
                ret = DialogError(hParent, allowSkip ? BUTTONS_RETRYSKIPCANCEL : BUTTONS_RETRYCANCEL, path.WideNameW(),
                                  ::GetErrorTextOwned(err).c_str(), LoadStrW(IDS_ERRORCREATINGFILE));
            }
            switch (ret)
            {
            case DIALOG_SKIPALL:
                if (silentMask != NULL)
                    *silentMask |= SILENT_SKIP_FILE_CREATE;
                // no break here
            case DIALOG_SKIP:
                if (skipped != NULL)
                    *skipped = TRUE;
                return INVALID_HANDLE_VALUE;
            case DIALOG_CANCEL:
            case DIALOG_FAIL:
                return INVALID_HANDLE_VALUE;
                // else retry
            }
        }

    SUCCESS:
        // *************** Anti-fragmentation code begins here

        // if possible, allocate the necessary space for the file (prevents disk fragmentation + smoother writes to floppies)
        if (allocateWholeFile != NULL)
        {
            BOOL fatal = TRUE;
            BOOL ignoreErr = FALSE;
            DWORD allocateError = ERROR_SUCCESS;
            if (*allocateWholeFile < CQuadWord(2, 0))
                TRACE_E("SafeFileCreate: (WARNING) allocateWholeFile less than 2");

        SET_SIZE_AGAIN:
            uint64_t newPosition = 0;
            const FileResult allocateSeekResult = gFileSystem->SeekHandle(
                hFile, static_cast<int64_t>(allocateWholeFile->Value), FILE_BEGIN,
                &newPosition);
            if (!allocateSeekResult.success)
                allocateError = allocateSeekResult.errorCode;
            else if (newPosition != allocateWholeFile->Value)
                allocateError = ERROR_INVALID_FUNCTION;
            if (allocateSeekResult.success && newPosition == allocateWholeFile->Value)
            {
                const FileResult allocateResult = gFileSystem->SetHandleEnd(hFile);
                if (allocateResult.success)
                {
                    const FileResult rewindResult =
                        gFileSystem->SeekHandle(hFile, 0, FILE_BEGIN, &newPosition);
                    if (rewindResult.success && newPosition == 0)
                    {
                        if (needWholeAllocTest)
                        {
                            DWORD wr;
                            if (gFileSystem->WriteToHandle(hFile, "x", 1, &wr).success && wr == 1)
                            {
                                if (gFileSystem->SetHandleEnd(hFile).success) // try truncating the file to one byte
                                {
                                    uint64_t size = 0;
                                    if (gFileSystem->GetHandleFileSize(hFile, &size).success && size == 1)
                                    { // check whether the written byte was appended to the end of the file and whether we can truncate the file
                                        needWholeAllocTest = FALSE;
                                        goto SET_SIZE_AGAIN; // we have to set the full file size again
                                    }
                                }
                            }
                        }
                        else
                        {
                            fatal = FALSE;
                            wholeFileAllocated = TRUE; // everything is OK, the file is stretched
                        }
                    }
                }
                else
                {
                    allocateError = allocateResult.errorCode;
                    if (allocateError == ERROR_DISK_FULL)
                        ignoreErr = TRUE; // low disk space
                }
            }
            if (fatal)
            {
                if (!ignoreErr)
                {
                    DWORD err = allocateError != ERROR_SUCCESS ? allocateError : GetLastError();
                    TRACE_EW(L"SafeFileCreate(): unable to allocate whole file size before copy operation, please report under what conditions this occurs! GetLastError(): " << GetErrorTextOwned(err).c_str());
                    *allocateWholeFile = CQuadWord(-1, 0); // skip further attempts on this target disk
                }
                else
                    *allocateWholeFile = CQuadWord(0, 0); // the file could not be prepared, but we will try again next time

                // also try truncating the file to zero to avoid unnecessary writing when closing the file
                (void)gFileSystem->SeekHandle(hFile, 0, FILE_BEGIN, NULL);
                (void)gFileSystem->SetHandleEnd(hFile);

                (void)gFileSystem->CloseFileHandle(hFile);
                ClearReadOnlyAttr(path.WideNameW()); // in case it ended up read-only so we can handle it
                gFileSystem->DeleteFile(path.WideNameW());

                allocateWholeFile = NULL; // next time we will no longer try to preallocate
                goto CREATE_FILE;
            }
        }
        // *************** Anti-fragmentation code ends here
    }
    // return the result - if we got this far, we return success
    if (isDir)
        return (void*)1; // for a directory, just return anything other than INVALID_HANDLE_VALUE
    if (file != NULL)    // our task is to initialize the SAFE_FILE structure
    {
        file->FileName = DupStr(path.WideNameW());
        if (file->FileName == NULL)
        {
            TRACE_E(LOW_MEMORY);
            (void)gFileSystem->CloseFileHandle(hFile);
            return FALSE;
        }
        file->HFile = hFile;
        file->HParentWnd = hParent;
        file->dwDesiredAccess = dwDesiredAccess;
        file->dwShareMode = dwShareMode;
        file->dwCreationDisposition = CREATE_ALWAYS;
        file->dwFlagsAndAttributes = dwFlagsAndAttributes;
        file->WholeFileAllocated = wholeFileAllocated;
        HANDLES_ADD(__htFile, __hoCreateFile, hFile); // add handle hFile to HANDLES
    }
    return hFile;
}

HANDLE
CSalamanderSafeFile::SafeFileCreate(const wchar_t* fileName,
                                    DWORD dwDesiredAccess,
                                    DWORD dwShareMode,
                                    DWORD dwFlagsAndAttributes,
                                    BOOL isDir,
                                    HWND hParent,
                                    const wchar_t* srcFileName,
                                    const wchar_t* srcFileInfo,
                                    DWORD* silentMask,
                                    BOOL allowSkip,
                                    BOOL* skipped,
                                    wchar_t* skipPath,
                                    int skipPathMax,
                                    CQuadWord* allocateWholeFile,
                                    SAFE_FILE* file)
{
    return SafeFileCreateWithContext(sally::safe_file::PathContext(fileName),
                                     dwDesiredAccess,
                                     dwShareMode,
                                     dwFlagsAndAttributes,
                                     isDir,
                                     hParent,
                                     srcFileName,
                                     srcFileInfo,
                                     silentMask,
                                     allowSkip,
                                     skipped,
                                     skipPath,
                                     skipPathMax,
                                     allocateWholeFile,
                                     file);
}

HANDLE
CSalamanderSafeFile::SafeFileCreateW(const wchar_t* fileName,
                                     const wchar_t* displayFileName,
                                     DWORD dwDesiredAccess,
                                     DWORD dwShareMode,
                                     DWORD dwFlagsAndAttributes,
                                     BOOL isDir,
                                     HWND hParent,
                                     const wchar_t* srcFileName,
                                     const wchar_t* srcFileInfo,
                                     DWORD* silentMask,
                                     BOOL allowSkip,
                                     BOOL* skipped,
                                     wchar_t* skipPath,
                                     int skipPathMax,
                                     CQuadWord* allocateWholeFile,
                                     SAFE_FILE* file)
{
    return SafeFileCreateWithContext(sally::safe_file::PathContext(fileName, displayFileName),
                                     dwDesiredAccess,
                                     dwShareMode,
                                     dwFlagsAndAttributes,
                                     isDir,
                                     hParent,
                                     srcFileName,
                                     srcFileInfo,
                                     silentMask,
                                     allowSkip,
                                     skipped,
                                     skipPath,
                                     skipPathMax,
                                     allocateWholeFile,
                                     file);
}

void CSalamanderSafeFile::SafeFileClose(SAFE_FILE* file)
{
    if (file->HFile != NULL && file->HFile != INVALID_HANDLE_VALUE)
    {
        if (file->WholeFileAllocated)
            (void)gFileSystem->SetHandleEnd(file->HFile); // otherwise the rest of the file would be written
        (void)gFileSystem->CloseFileHandle(file->HFile);
    }
    if (file->FileName != NULL)
        free(file->FileName);
    ZeroMemory(file, sizeof(SAFE_FILE));
}

BOOL CSalamanderSafeFile::SafeFileSeek(SAFE_FILE* file, CQuadWord* distance, DWORD moveMethod, DWORD* error)
{
    if (error != NULL)
        *error = NO_ERROR;
    if (file->HFile == NULL)
    {
        TRACE_E("CSalamanderSafeFile::SafeFileSeek() HFile==NULL");
        return FALSE;
    }

    uint64_t newPosition = 0;
    const FileResult seekResult = gFileSystem->SeekHandle(
        file->HFile, static_cast<int64_t>(distance->Value), moveMethod,
        &newPosition);
    if (!seekResult.success)
    {
        if (error != NULL)
            *error = seekResult.errorCode;
        return FALSE;
    }

    distance->Value = newPosition;
    return TRUE;
}

BOOL CSalamanderSafeFile::SafeFileSeekMsg(SAFE_FILE* file, CQuadWord* distance, DWORD moveMethod,
                                          HWND hParent, DWORD flags, DWORD* pressedButton,
                                          DWORD* silentMask, BOOL seekForRead)
{
    if (file->HFile == NULL)
    {
        TRACE_E("CSalamanderSafeFile::SafeFileSeekMsg() HFile==NULL");
        return FALSE;
    }
SEEK_AGAIN:
    DWORD lastError;
    BOOL ret = SafeFileSeek(file, distance, moveMethod, &lastError);
    if (!ret)
    {
        DWORD dlgRet;
        DWORD skip = seekForRead ? SILENT_SKIP_FILE_READ : SILENT_SKIP_FILE_WRITE;
        if (silentMask != NULL && (*silentMask & skip) && ButtonsContainsSkip(flags)) // if we are not supposed to ignore the message, show it
            dlgRet = DIALOG_SKIP;
        else
        {
            dlgRet = DialogError((hParent == HWND_STORED) ? file->HParentWnd : hParent, (flags & BUTTONS_MASK),
                                 SafeFileNameW(file), GetErrorTextOwned(lastError).c_str(),
                                 LoadStrW(seekForRead ? IDS_ERRORREADINGFILE : IDS_ERRORWRITINGFILE));
        }
        switch (dlgRet)
        {
        case DIALOG_RETRY:
            goto SEEK_AGAIN; // try again
        case DIALOG_SKIPALL:
            if (silentMask != NULL)
                *silentMask |= skip;
        default:
        {
            if (pressedButton != NULL)
                *pressedButton = dlgRet; // return the button the user clicked
            return FALSE;
        }
        }
    }
    return ret;
}

BOOL CSalamanderSafeFile::SafeFileGetSize(SAFE_FILE* file, CQuadWord* fileSize, DWORD* error)
{
    if (error != NULL)
        *error = NO_ERROR;
    uint64_t size = 0;
    const FileResult sizeResult = gFileSystem->GetHandleFileSize(file->HFile, &size);
    if (!sizeResult.success)
    {
        if (error != NULL)
            *error = sizeResult.errorCode;
        return FALSE;
    }
    fileSize->Value = size;
    return TRUE;
}

BOOL CSalamanderSafeFile::SafeFileRead(SAFE_FILE* file, LPVOID lpBuffer,
                                       DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead,
                                       HWND hParent, DWORD flags, DWORD* pressedButton,
                                       DWORD* silentMask)
{
    if (file->HFile == NULL)
    {
        TRACE_E("CSalamanderSafeFile::SafeFileRead() HFile==NULL");
        return FALSE;
    }
    std::wstring reopenName;
    // obtain the current seek position in the file
    uint64_t currentSeek = 0;
    DWORD pendingIoError = ERROR_SUCCESS;
    FileResult ioResult = gFileSystem->SeekHandle(
        file->HFile, 0, FILE_CURRENT, &currentSeek);
    if (!ioResult.success)
    {
        pendingIoError = ioResult.errorCode;
        goto READ_ERROR; // cannot set the offset, try again
    }

    while (TRUE)
    {
        ioResult = gFileSystem->ReadFromHandle(
            file->HFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead);
        if (ioResult.success)
        {
            if ((flags & SAFE_FILE_CHECK_SIZE) && nNumberOfBytesToRead != *lpNumberOfBytesRead)
            {
                // the caller requires reading exactly as many bytes as requested
                DWORD dlgRet;
                if (silentMask != NULL && (*silentMask & SILENT_SKIP_FILE_READ) && ButtonsContainsSkip(flags))
                    dlgRet = DIALOG_SKIP;
                else
                {
                    dlgRet = DialogError((hParent == HWND_STORED) ? file->HParentWnd : hParent, (flags & BUTTONS_MASK),
                                         SafeFileNameW(file), GetErrorTextOwned(ERROR_HANDLE_EOF).c_str(), LoadStrW(IDS_ERRORREADINGFILE));
                }
                switch (dlgRet)
                {
                case DIALOG_RETRY:
                    goto SEEK;
                case DIALOG_SKIPALL:
                    if (silentMask != NULL)
                        *silentMask |= SILENT_SKIP_FILE_READ;
                default:
                {
                    if (pressedButton != NULL)
                        *pressedButton = dlgRet; // return the button the user clicked
                    return FALSE;
                }
                }
            }
            return TRUE;
        }
        else
        {
        READ_ERROR:
            DWORD lastError = pendingIoError != ERROR_SUCCESS ?
                                  pendingIoError : ioResult.errorCode;
            pendingIoError = ERROR_SUCCESS;
            DWORD dlgRet;
            if (silentMask != NULL && (*silentMask & SILENT_SKIP_FILE_READ) && ButtonsContainsSkip(flags))
                dlgRet = DIALOG_SKIP;
            else
            {
                dlgRet = DialogError((hParent == HWND_STORED) ? file->HParentWnd : hParent, (flags & BUTTONS_MASK),
                                     SafeFileNameW(file), GetErrorTextOwned(lastError).c_str(), LoadStrW(IDS_ERRORREADINGFILE));
            }
            switch (dlgRet)
            {
            case DIALOG_RETRY:
            {
                if (file->HFile != NULL)
                {
                    if (file->WholeFileAllocated)
                        (void)gFileSystem->SetHandleEnd(file->HFile);     // otherwise the rest of the file would be written
                    (void)gFileSystem->CloseFileHandle(file->HFile); // close the invalid handle because we could not read from it anyway
                }

                reopenName = SafeFileNameW(file);
                file->HFile = gFileSystem->CreateFile(reopenName.c_str(), file->dwDesiredAccess,
                                                      file->dwShareMode, NULL,
                                                      file->dwCreationDisposition,
                                                      file->dwFlagsAndAttributes, NULL);
                if (file->HFile != INVALID_HANDLE_VALUE) // opened; now set the offset
                {
                SEEK:
                    uint64_t restoredSeek = 0;
                    ioResult = gFileSystem->SeekHandle(
                        file->HFile, static_cast<int64_t>(currentSeek), FILE_BEGIN,
                        &restoredSeek);
                    if (!ioResult.success)
                    {
                        pendingIoError = ioResult.errorCode;
                        goto READ_ERROR; // cannot set the offset, try again
                    }
                    if (restoredSeek != currentSeek)
                    {
                        pendingIoError = ERROR_SEEK_ON_DEVICE;
                        goto READ_ERROR; // cannot set the offset (the file may already be smaller), try again
                    }
                }
                else // cannot open it, the problem persists...
                {
                    pendingIoError = GetLastError();
                    file->HFile = NULL;
                    goto READ_ERROR;
                }
                break;
            }

            case DIALOG_SKIPALL:
                if (silentMask != NULL)
                    *silentMask |= SILENT_SKIP_FILE_READ;
            default:
            {
                if (pressedButton != NULL)
                    *pressedButton = dlgRet; // return the button the user clicked
                return FALSE;
            }
            }
        }
    }
}

BOOL CSalamanderSafeFile::SafeFileWrite(SAFE_FILE* file, LPVOID lpBuffer,
                                        DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten,
                                        HWND hParent, DWORD flags, DWORD* pressedButton,
                                        DWORD* silentMask)
{
    if (file->HFile == NULL)
    {
        TRACE_E("CSalamanderSafeFile::SafeFileWrite() HFile==NULL");
        return FALSE;
    }
    // obtain the current seek position in the file
    uint64_t currentSeek = 0;
    DWORD pendingIoError = ERROR_SUCCESS;
    FileResult ioResult = gFileSystem->SeekHandle(
        file->HFile, 0, FILE_CURRENT, &currentSeek);
    if (!ioResult.success)
    {
        pendingIoError = ioResult.errorCode;
        goto WRITE_ERROR; // cannot set the offset, try again
    }

    while (TRUE)
    {
        ioResult = gFileSystem->WriteToHandle(
            file->HFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten);
        if (ioResult.success &&
            nNumberOfBytesToWrite == *lpNumberOfBytesWritten)
        {
            return TRUE;
        }
        else
        {
        WRITE_ERROR:
            DWORD lastError = pendingIoError != ERROR_SUCCESS ?
                                  pendingIoError : ioResult.errorCode;
            pendingIoError = ERROR_SUCCESS;
            DWORD dlgRet;
            if (silentMask != NULL && (*silentMask & SILENT_SKIP_FILE_WRITE) && ButtonsContainsSkip(flags))
                dlgRet = DIALOG_SKIP;
            else
            {
                dlgRet = DialogError((hParent == HWND_STORED) ? file->HParentWnd : hParent, (flags & BUTTONS_MASK),
                                     SafeFileNameW(file), GetErrorTextOwned(lastError).c_str(), LoadStrW(IDS_ERRORWRITINGFILE));
            }
            switch (dlgRet)
            {
            case DIALOG_RETRY:
            {
                if (file->HFile != NULL)
                {
                    if (file->WholeFileAllocated)
                        (void)gFileSystem->SetHandleEnd(file->HFile);     // otherwise the rest of the file would be written
                    (void)gFileSystem->CloseFileHandle(file->HFile); // close the invalid handle because we could not read from it anyway
                }

                std::wstring reopenName = SafeFileNameW(file);
                file->HFile = gFileSystem->CreateFile(reopenName.c_str(), file->dwDesiredAccess,
                                                      file->dwShareMode, NULL,
                                                      file->dwCreationDisposition,
                                                      file->dwFlagsAndAttributes, NULL);
                if (file->HFile != INVALID_HANDLE_VALUE) // opened; now set the offset
                {
                    //SEEK:
                    uint64_t restoredSeek = 0;
                    ioResult = gFileSystem->SeekHandle(
                        file->HFile, static_cast<int64_t>(currentSeek), FILE_BEGIN,
                        &restoredSeek);
                    if (!ioResult.success)
                    {
                        pendingIoError = ioResult.errorCode;
                        goto WRITE_ERROR; // cannot set the offset, try again
                    }
                    if (restoredSeek != currentSeek)
                    {
                        pendingIoError = ERROR_SEEK_ON_DEVICE;
                        goto WRITE_ERROR; // cannot set the offset (the file may already be smaller), try again
                    }
                }
                else // cannot open it, the problem persists...
                {
                    pendingIoError = GetLastError();
                    file->HFile = NULL;
                    goto WRITE_ERROR;
                }
                break;
            }

            case DIALOG_SKIPALL:
                if (silentMask != NULL)
                    *silentMask |= SILENT_SKIP_FILE_WRITE;
            default:
            {
                if (pressedButton != NULL)
                    *pressedButton = dlgRet; // return the button the user clicked
                return FALSE;
            }
            }
        }
    }
}
