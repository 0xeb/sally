// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "pack.h"
#include "codetbl.h"
#include "dialogs.h"
#include "common/widepath.h"
#include "ui/IPrompter.h"
#include "common/IRegistry.h"
#include "common/DiagnosticTextEncoding.h"
#include "common/unicode/helpers.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/IFileSystem.h"
#include "common/fsutil.h"

CSystemPolicies SystemPolicies;

static IRegistry* GetSystemPoliciesRegistry()
{
    return gRegistry != nullptr ? gRegistry : GetWin32Registry();
}

static BOOL GetValueDontCheckTypeViaRegistry(IRegistry* registry, HKEY hKey, const wchar_t* name, void* buffer, DWORD bufferSize)
{
    if (registry == nullptr || buffer == nullptr || bufferSize == 0)
        return FALSE;

    RegValueType type = RegValueType::None;
    std::vector<uint8_t> data;
    auto result = registry->GetValue(hKey, name, type, data);
    if (!result.success || data.size() > bufferSize)
        return FALSE;

    if (!data.empty())
        memcpy(buffer, data.data(), data.size());
    return TRUE;
}

CRITICAL_SECTION CheckPathCS; // critical section for check-path, necessary due to calls from multiple threads (not just main)

std::wstring CheckPathRootWithRetryMsgBox;
HWND LastDriveSelectErrDlgHWnd = NULL; // "drive not ready" dialog with Retry+Cancel buttons (used for automatic Retry after inserting media into drive)

struct CWideCheckPathData
{
    explicit CWideCheckPathData(const wchar_t* path)
        : Path(path != NULL ? path : L"")
    {
    }

    std::wstring Path;
    BOOL Valid = FALSE;
    DWORD LastError = ERROR_SUCCESS;
    LONG RefCount = 2; // caller + worker thread
};

static void ReleaseWideCheckPathData(CWideCheckPathData* data)
{
    if (data != NULL && InterlockedDecrement(&data->RefCount) == 0)
        delete data;
}

static void CheckWidePathAttributes(const std::wstring& path, BOOL& valid, DWORD& lastError)
{
    valid = (gFileSystem->GetFileAttributes(path.c_str()) != INVALID_FILE_ATTRIBUTES);
    lastError = valid ? ERROR_SUCCESS : GetLastError();
    if (!valid && lastError == ERROR_INVALID_PARAMETER)
        lastError = ERROR_NOT_READY;

    // Match the fixed-disk ACCESS_DENIED workaround from the ANSI check path.
    if (!valid && lastError == ERROR_ACCESS_DENIED &&
        path.length() >= 2 && path[1] == L':' &&
        ((path[0] >= L'a' && path[0] <= L'z') || (path[0] >= L'A' && path[0] <= L'Z')))
    {
        wchar_t root[4] = {path[0], L':', L'\\', L'\0'};
        if (GetDriveTypeW(root) == DRIVE_FIXED)
        {
            std::wstring probe = path;
            if (!probe.empty() && probe.back() != L'\\' && probe.back() != L'/')
                probe += L'\\';
            probe += L"*";

            WIN32_FIND_DATAW data;
            HANDLE find = gFileSystem->FindFirstFile(probe.c_str(), &data);
            if (find != INVALID_HANDLE_VALUE)
            {
                valid = TRUE;
                lastError = ERROR_SUCCESS;
                gFileSystem->CloseFind(find);
            }
        }
    }
}

static DWORD WINAPI WideCheckPathThreadF(void* param)
{
    CWideCheckPathData* data = (CWideCheckPathData*)param;
    SetThreadNameInVCAndTrace(L"CheckPathW");
    CheckWidePathAttributes(data->Path, data->Valid, data->LastError);
    ReleaseWideCheckPathData(data);
    return 0;
}

static DWORD RunWideCheckPathWorker(const wchar_t* path)
{
    CWideCheckPathData* data = new CWideCheckPathData(path);
    if (data == NULL)
    {
        BOOL valid;
        DWORD lastError;
        CheckWidePathAttributes(path != NULL ? std::wstring(path) : std::wstring(), valid, lastError);
        return valid ? ERROR_SUCCESS : lastError;
    }

    DWORD threadID;
    HANDLE thread = HANDLES(CreateThread(NULL, 0, WideCheckPathThreadF, data, 0, &threadID));
    if (thread == NULL)
    {
        DWORD lastError = ERROR_SUCCESS;
        BOOL valid = FALSE;
        CheckWidePathAttributes(data->Path, valid, lastError);
        ReleaseWideCheckPathData(data); // worker reference
        ReleaseWideCheckPathData(data); // caller reference
        return valid ? ERROR_SUCCESS : lastError;
    }

    DWORD exit = STILL_ACTIVE;
    GetAsyncKeyState(VK_ESCAPE);
    WaitForSingleObject(thread, 200);
    if (!GetExitCodeThread(thread, &exit))
        exit = STILL_ACTIVE;

    if (exit == STILL_ACTIVE)
    {
        std::wstring waitText = FormatStrW(LoadStrW(IDS_CHECKINGPATHESC), path != NULL ? path : L"");
        CreateSafeWaitWindow(waitText.c_str(), NULL, 4800 + 200, TRUE, NULL);

        while (exit == STILL_ACTIVE)
        {
            if (UserWantsToCancelSafeWaitWindow())
            {
                DestroySafeWaitWindow();
                HANDLES(CloseHandle(thread));
                ReleaseWideCheckPathData(data);

                MSG msg;
                while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
                    ;

                std::wstring infoMsg = FormatStrW(LoadStrW(IDS_TERMINATEDBYUSER), path != NULL ? path : L"");
                gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), infoMsg.c_str());
                return ERROR_USER_TERMINATED;
            }

            WaitForSingleObject(thread, 200);
            if (!GetExitCodeThread(thread, &exit))
                exit = STILL_ACTIVE;
        }

        DestroySafeWaitWindow();
    }

    DWORD result = data->Valid ? ERROR_SUCCESS : data->LastError;
    HANDLES(CloseHandle(thread));
    ReleaseWideCheckPathData(data);
    return result;
}

CRITICAL_SECTION OpenHtmlHelpCS; // critical section for OpenHtmlHelp()

// non-blocking reading of volume-name from CD drive:
CRITICAL_SECTION ReadCDVolNameCS;        // critical section for data access
UINT_PTR ReadCDVolNameReqUID = 0;        // UID of request (to recognize if anyone is still waiting for result)
std::wstring ReadCDVolNameBuffer; // IN/OUT value (root/volume name), protected by ReadCDVolNameCS

struct CInitOpenHtmlHelpCS
{
    CInitOpenHtmlHelpCS() { HANDLES(InitializeCriticalSection(&OpenHtmlHelpCS)); }
    ~CInitOpenHtmlHelpCS() { HANDLES(DeleteCriticalSection(&OpenHtmlHelpCS)); }
} __InitOpenHtmlHelpCS;

BOOL InitializeCheckThread()
{
    CALL_STACK_MESSAGE_NONE
    HANDLES(InitializeCriticalSection(&CheckPathCS));
    HANDLES(InitializeCriticalSection(&ReadCDVolNameCS));
    return TRUE;
}

void ReleaseCheckThreads()
{
    CALL_STACK_MESSAGE_NONE
    HANDLES(DeleteCriticalSection(&ReadCDVolNameCS));
    HANDLES(DeleteCriticalSection(&CheckPathCS));
}

DWORD SalCheckPathW(BOOL echo, const wchar_t* path, DWORD err, BOOL postRefresh, HWND parent)
{
    CALL_STACK_MESSAGE5("SalCheckPathW(%d, %ls, 0x%X, %d, )", echo, path, err, postRefresh);

    HANDLES(EnterCriticalSection(&CheckPathCS));

    static BOOL called = FALSE;
    if (called)
    {
        HANDLES(LeaveCriticalSection(&CheckPathCS));
        TRACE_I("SalCheckPathW: recursive call (in one thread) is not allowed!");
        return 666;
    }
    called = TRUE;

    BeginStopRefresh();

    BOOL valid = FALSE;
    DWORD lastError = ERROR_SUCCESS;

RETRY:

    if (err == ERROR_SUCCESS)
    {
        if (path == NULL || *path == L'\0')
        {
            lastError = ERROR_PATH_NOT_FOUND;
        }
        else
        {
            lastError = RunWideCheckPathWorker(path);
            valid = lastError == ERROR_SUCCESS;
        }
    }
    else
    {
        lastError = err;
        err = ERROR_SUCCESS;
    }

    if ((err == ERROR_USER_TERMINATED || echo) && !valid)
    {
        switch (lastError)
        {
        case (DWORD)ERROR_USER_TERMINATED:
            break;

        case ERROR_NOT_READY:
        {
            // "There is no disk in drive E:" with Retry/Cancel, and the automatic Retry that fires
            // when the user inserts media - main_window_commands_help.cpp watches
            // CheckPathRootWithRetryMsgBox and posts IDRETRY to LastDriveSelectErrDlgHWnd. Without
            // this case the drive-not-ready error fell through to the OK-only box below, so an
            // empty CD/DVD or card-reader slot abandoned the operation outright and the two
            // globals above were never set by anyone. Same shape as the reparse-point recovery in
            // CFilesWindow::ReadDirectory.
            const std::wstring checked = path != NULL ? path : L"";
            std::wstring drive;
            UINT drvType;
            if (checked.length() >= 2 && checked[0] == L'\\' && checked[1] == L'\\')
            {
                drvType = DRIVE_REMOTE;
                drive = GetRootPath(checked.c_str());
                SalPathRemoveBackslashW(drive); // we don't want the last '\'
            }
            else
            {
                drive.assign(1, checked.empty() ? L' ' : checked[0]);
                drvType = MyGetDriveTypeW(checked.c_str());
            }
            if (drvType != DRIVE_REMOTE)
            {
                std::wstring currentReparsePoint;
                GetCurrentLocalReparsePointW(checked.c_str(), currentReparsePoint);
                CheckPathRootWithRetryMsgBox = currentReparsePoint;
                if (currentReparsePoint.length() > 3)
                {
                    drive = currentReparsePoint;
                    SalPathRemoveBackslashW(drive);
                }
            }
            else
                CheckPathRootWithRetryMsgBox = GetRootPath(checked.c_str());
            const std::wstring driveError = FormatStrW(LoadStrW(IDS_NODISKINDRIVE), drive.c_str());
            int msgboxRes = (int)CDriveSelectErrDlg(parent, driveError.c_str(), checked.c_str()).Execute();
            CheckPathRootWithRetryMsgBox.clear();
            UpdateWindow(MainWindow->HWindow);
            if (msgboxRes == IDRETRY)
                goto RETRY;
            break;
        }

        case ERROR_DIRECTORY:
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_BAD_PATHNAME:
        {
            std::wstring msg = FormatStrW(LoadStrW(IDS_DIRNAMEINVALID), path != NULL ? path : L"");
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
            break;
        }

        default:
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), GetErrorTextOwned(lastError).c_str());
            break;
        }
    }

    EndStopRefresh(postRefresh);
    called = FALSE;

    HANDLES(LeaveCriticalSection(&CheckPathCS));
    return valid ? ERROR_SUCCESS : lastError;
}

BOOL SalCheckAndRestorePathW(HWND parent, const wchar_t* path, BOOL tryNet)
{
    CALL_STACK_MESSAGE3("SalCheckAndRestorePathW(, %ls, %d)", path, tryNet);
    DWORD err;
    if ((err = SalCheckPathW(FALSE, path, ERROR_SUCCESS, TRUE, parent)) != ERROR_SUCCESS)
    {
        BOOL ok = FALSE;
        BOOL pathInvalid = FALSE;
        if (tryNet && err != ERROR_USER_TERMINATED && path != NULL)
        {
            tryNet = FALSE;
            if (path[0] != L'\0' && path[1] == L':' &&
                ((path[0] >= L'a' && path[0] <= L'z') || (path[0] >= L'A' && path[0] <= L'Z')))
            {
                if (CheckAndRestoreNetworkConnection(parent, (char)path[0], pathInvalid))
                {
                    if ((err = SalCheckPathW(FALSE, path, ERROR_SUCCESS, TRUE, parent)) == ERROR_SUCCESS)
                        ok = TRUE;
                }
            }
            else
            {
                if (CheckAndConnectUNCNetworkPathW(parent, path, pathInvalid, FALSE))
                {
                    if ((err = SalCheckPathW(FALSE, path, ERROR_SUCCESS, TRUE, parent)) == ERROR_SUCCESS)
                        ok = TRUE;
                }
            }
        }
        if (!ok)
        {
            if (pathInvalid ||
                err == ERROR_USER_TERMINATED ||
                SalCheckPathW(TRUE, path, err, TRUE, parent) != ERROR_SUCCESS)
            {
                return FALSE;
            }
        }
    }

    if (tryNet && path != NULL)
    {
        BOOL pathInvalid = FALSE;
        if (CheckAndConnectUNCNetworkPathW(parent, path, pathInvalid, FALSE))
        {
            if (SalCheckPathW(TRUE, path, ERROR_SUCCESS, TRUE, parent) != ERROR_SUCCESS)
                return FALSE;
        }
        else if (pathInvalid)
        {
            return FALSE;
        }
    }

    return TRUE;
}

BOOL SalCheckAndRestorePathWithCutW(HWND parent, std::wstring& path, BOOL& tryNet, DWORD& err, DWORD& lastErr,
                                    BOOL& pathInvalid, BOOL& cut, BOOL donotReconnect)
{
    CALL_STACK_MESSAGE4("SalCheckAndRestorePathWithCutW(, %ls, %d, , , , , %d)", path.c_str(), tryNet,
                        donotReconnect);

    pathInvalid = FALSE;
    cut = FALSE;
    lastErr = ERROR_SUCCESS;
    BOOL semTimeoutOccured = FALSE;

_CHECK_AGAIN:

    while ((err = SalCheckPathW(FALSE, path.c_str(), ERROR_SUCCESS, TRUE, parent)) != ERROR_SUCCESS)
    {
        if (err == ERROR_SEM_TIMEOUT && !semTimeoutOccured)
        {
            semTimeoutOccured = TRUE;
            Sleep(300);
            continue;
        }
        if (err == ERROR_USER_TERMINATED)
            break;
        if (tryNet)
        {
            tryNet = FALSE;
            if (path.length() >= 2 && path[1] == L':' &&
                ((path[0] >= L'a' && path[0] <= L'z') || (path[0] >= L'A' && path[0] <= L'Z')))
            {
                if (!donotReconnect && CheckAndRestoreNetworkConnection(parent, (char)path[0], pathInvalid))
                    continue;
            }
            else
            {
                if (CheckAndConnectUNCNetworkPathW(parent, path.c_str(), pathInvalid,
                                                    donotReconnect))
                {
                    continue;
                }
            }
            if (pathInvalid)
                break;
        }
        lastErr = err;
        if (!IsDirError(err))
            break;
        if (!CutDirectoryW(path))
            break;
        cut = TRUE;
    }

    if (tryNet && err != ERROR_USER_TERMINATED)
    {
        tryNet = FALSE;
        if (CheckAndConnectUNCNetworkPathW(parent, path.c_str(), pathInvalid,
                                            donotReconnect))
        {
            goto _CHECK_AGAIN;
        }
    }

    return !pathInvalid && err == ERROR_SUCCESS;
}

BOOL SalParsePathW(HWND parent, std::wstring& path, int& type, BOOL& isDir, wchar_t*& secondPart,
                   const wchar_t* errorTitle, std::wstring* nextFocus, BOOL curPathIsDiskOrArchive,
                   const wchar_t* curPath, const wchar_t* curArchivePath, int* error)
{
    CALL_STACK_MESSAGE_NONE
    type = -1;
    secondPart = NULL;
    isDir = FALSE;
    if (nextFocus != NULL)
        nextFocus->clear();
    if (error != NULL)
        *error = 0;

PARSE_AGAIN_W:
    std::wstring fsName;
    wchar_t* fsUserPart = NULL;
    if (IsPluginFSPath(path.data(), &fsName, &fsUserPart))
    {
        int index;
        int fsNameIndex;
        if (!Plugins.IsPluginFS(fsName.c_str(), index, fsNameIndex))
        {
            const std::wstring msg = FormatStrW(
                LoadStrW(IDS_PATHERRORFORMAT), path.c_str(),
                LoadStrW(IDS_NOTPLUGINFS));
            gPrompter->ShowError(errorTitle, msg.c_str());
            if (error != NULL)
                *error = SPP_NOTPLUGINFS;
            return FALSE;
        }

        type = PATH_TYPE_FS;
        secondPart = fsUserPart;
        return TRUE;
    }

    int len = (int)path.length();
    BOOL backslashAtEnd = (len > 0 && path[len - 1] == L'\\');
    BOOL mustBePath =
        len == 2 &&
        ((path[0] >= L'a' && path[0] <= L'z') ||
         (path[0] >= L'A' && path[0] <= L'Z')) &&
        path[1] == L':';

    if (nextFocus != NULL && !mustBePath)
    {
        size_t pos = path.find(L'\\');
        if (pos == std::wstring::npos || pos + 1 == path.length())
        {
            size_t focusLen = (pos == std::wstring::npos) ? path.length() : pos;
            *nextFocus = path.substr(0, focusLen);
        }
    }

    int errTextID;
    if (!SalGetFullNameW(path, &errTextID, curPathIsDiskOrArchive ? curPath : NULL, nextFocus, NULL, curPathIsDiskOrArchive))
    {
        if (errTextID == IDS_EMPTYNAMENOTALLOWED)
        {
            if (curPath == NULL)
            {
                if (error != NULL)
                    *error = SPP_EMPTYPATHNOTALLOWED;
            }
            else
            {
                path = curPath;
                goto PARSE_AGAIN_W;
            }
        }
        else
        {
            if (errTextID == IDS_INCOMLETEFILENAME)
            {
                if (error != NULL)
                    *error = SPP_INCOMLETEPATH;
                if (!curPathIsDiskOrArchive)
                    return FALSE;
            }
            else if (error != NULL)
                *error = SPP_WINDOWSPATHERROR;
        }
        std::wstring msg = FormatStrW(LoadStrW(IDS_PATHERRORFORMAT), path.c_str(), LoadStrW(errTextID));
        gPrompter->ShowError(errorTitle, msg.c_str());
        if (backslashAtEnd || mustBePath)
            SalPathAddBackslashW(path);
        return FALSE;
    }

    if (curArchivePath != NULL && _wcsicmp(path.c_str(), curArchivePath) == 0)
    {
        SalPathAddBackslashW(path);
        backslashAtEnd = TRUE;
    }

    std::wstring root = GetRootPath(path.c_str());
    BOOL tryNet = !curPathIsDiskOrArchive || curPath == NULL || !HasTheSameRootPath(root.c_str(), curPath);
    if (!SalCheckAndRestorePathW(parent, root.c_str(), tryNet))
    {
        if (backslashAtEnd || mustBePath)
            SalPathAddBackslashW(path);
        if (error != NULL)
            *error = SPP_WINDOWSPATHERROR;
        return FALSE;
    }

FIND_AGAIN_W:
    wchar_t* buffer = path.data();
    wchar_t* end = buffer + path.length();
    wchar_t* afterRoot = buffer + root.length();
    if (afterRoot > buffer && *(afterRoot - 1) == L'\\')
        ;
    else if (*afterRoot == L'\\')
        afterRoot++;
    wchar_t lastChar = 0;
    BOOL hasMask = FALSE;
    if (end > afterRoot)
    {
        wchar_t* end2 = end;
        while (*--end2 != L'\\')
        {
            if (*end2 == L'*' || *end2 == L'?')
                hasMask = TRUE;
        }
        if (hasMask)
        {
            CutSpacesFromBothSidesW(end2 + 1);
            end = end2;
            lastChar = *end;
            *end = 0;
            path.resize((size_t)(end - buffer));
            buffer = path.data();
            end = buffer + path.length();
            afterRoot = buffer + root.length();
        }
    }

    HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
    isDir = TRUE;
    std::wstring text;
    while (end > afterRoot)
    {
        if (*(end - 1) != L'\\')
        {
            DWORD attrs = gFileSystem->GetFileAttributes(path.c_str());
            if (attrs != 0xFFFFFFFF)
            {
                if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    if (lastChar != 0 || backslashAtEnd || mustBePath)
                    {
                        if (PackerFormatConfig.PackIsArchive(path.c_str()))
                        {
                            type = PATH_TYPE_ARCHIVE;
                            isDir = FALSE;
                            break;
                        }
                        text = LoadStrW(IDS_NOTARCHIVEPATH);
                        if (error != NULL)
                            *error = SPP_NOTARCHIVEFILE;
                        break;
                    }
                    isDir = FALSE;
                    while (*--end != L'\\')
                        ;
                    lastChar = *end;
                    break;
                }
                break;
            }
            else
            {
                DWORD err = GetLastError();
                if (err != ERROR_FILE_NOT_FOUND && err != ERROR_INVALID_NAME &&
                    err != ERROR_PATH_NOT_FOUND && err != ERROR_BAD_PATHNAME &&
                    err != ERROR_DIRECTORY)
                {
                    text = GetErrorTextOwned(err).c_str();
                    if (error != NULL)
                        *error = SPP_WINDOWSPATHERROR;
                    break;
                }
            }
        }
        *end = lastChar;
        while (*--end != L'\\')
            ;
        lastChar = *end;
        *end = 0;
        path.resize((size_t)(end - buffer));
        buffer = path.data();
        end = buffer + path.length();
        afterRoot = buffer + root.length();
    }
    // Capture the boundary between the existing-path prefix and the
    // non-existent leaf/mask before the buffer-trick resize below grows
    // path back to its original length. Use `end - buffer` rather than
    // `path.length()`: the new-dir/mask loop path always resizes path to
    // match `end`, so the two agree; but the existing-file break at
    // lines 1264-1268 walks `end` back to the previous '\\' WITHOUT
    // resizing path, leaving path.length() at the full original length.
    // Taking path.length() there would lose the boundary the same way the
    // pre-fix code did and secondPart would point at the terminator.
    const size_t boundaryOff = (size_t)(end - buffer);
    if (end <= buffer + path.length())
        *end = lastChar;
    path.resize(wcslen(buffer));
    SetCursor(oldCur);

    if (text.empty())
    {
        buffer = path.data();
        // Restore end to the boundary captured above, not to the new
        // terminator. Without this, secondPart would be returned empty for
        // copy/move targets with a non-existent leaf or mask, and
        // SalSplitWindowsPathW would treat the whole input as an existing
        // directory.
        end = buffer + boundaryOff;
        if (*end == L'\\')
            end++;
        if (isDir && *end != 0 && !hasMask && wcschr(end, L'\\') == NULL)
        {
            BOOL changeNextFocus = nextFocus != NULL && !nextFocus->empty() && _wcsicmp(nextFocus->c_str(), end) == 0;
            if (MakeValidFileNameComponentW(end))
            {
                path.resize(wcslen(buffer));
                if (changeNextFocus)
                    *nextFocus = end;
                goto FIND_AGAIN_W;
            }
        }
        secondPart = end;
        type = PATH_TYPE_WINDOWS;
        return TRUE;
    }

    std::wstring msg = FormatStrW(LoadStrW(IDS_PATHERRORFORMAT), path.c_str(), text.c_str());
    gPrompter->ShowError(errorTitle, msg.c_str());
    if (backslashAtEnd || mustBePath)
        SalPathAddBackslashW(path);
    return FALSE;
}

static BOOL SalSplitGeneralPathBufferW(HWND parent, const wchar_t* title, const wchar_t* errorTitle, int selCount,
                                      wchar_t* path, size_t pathCapacity, wchar_t* afterRoot, wchar_t* secondPart,
                                      BOOL pathIsDir, BOOL backslashAtEnd, const wchar_t* dirName,
                                      const wchar_t* curPath, wchar_t*& mask, wchar_t* newDirs,
                                      size_t newDirsCapacity, SGP_IsTheSamePathF isTheSamePathF);

static BOOL SalSplitWindowsPathBufferW(HWND parent, const wchar_t* title, const wchar_t* errorTitle, int selCount,
                                       wchar_t* path, size_t pathCapacity, wchar_t* secondPart, BOOL pathIsDir,
                                       BOOL backslashAtEnd, const wchar_t* dirName, const wchar_t* curDiskPath,
                                       wchar_t*& mask)
{
    std::wstring root = GetRootPath(path);
    wchar_t* afterRoot = path + root.length() - 1;
    if (*afterRoot == L'\\')
        afterRoot++;

    std::vector<wchar_t> newDirs(wcslen(path) + 1, L'\0');
    if (SalSplitGeneralPathBufferW(parent, title, errorTitle, selCount, path, pathCapacity,
                                   afterRoot, secondPart, pathIsDir, backslashAtEnd, dirName,
                                   curDiskPath, mask, newDirs.data(), newDirs.size(), NULL))
    {
        if (mask - 1 > path && *(mask - 2) == L'\\' &&
            (mask - 1 > afterRoot || *path == L'\\'))
        {
            memmove(mask - 2, mask - 1, (wcslen(mask) + 2) * sizeof(wchar_t));
            mask--;
        }

        if (newDirs[0] != 0)
        {
            size_t prefixLen = (size_t)(secondPart - path);
            memmove(newDirs.data() + prefixLen, newDirs.data(), (wcslen(newDirs.data()) + 1) * sizeof(wchar_t));
            memmove(newDirs.data(), path, prefixLen * sizeof(wchar_t));
            newDirs[prefixLen + wcslen(newDirs.data() + prefixLen)] = 0;
            SalPathRemoveBackslashW(newDirs.data());

            BOOL ok = TRUE;
            wchar_t* st = newDirs.data() + prefixLen;
            while (1)
            {
                BOOL invalidPath = *st != 0 && *st <= L' ';
                wchar_t* slash = wcschr(st, L'\\');
                if (slash != NULL)
                {
                    if (slash > st && (*(slash - 1) <= L' ' || *(slash - 1) == L'.'))
                        invalidPath = TRUE;
                    *slash = 0;
                }
                else if (*st != 0)
                {
                    wchar_t* end = st + wcslen(st) - 1;
                    if (*end <= L' ' || *end == L'.')
                        invalidPath = TRUE;
                }

                const FileResult createResult = invalidPath ? FileResult::Error(ERROR_INVALID_NAME) :
                                                              gFileSystem->CreateDirectory(newDirs.data());
                if (!createResult.success)
                {
                    DWORD lastErr = createResult.errorCode;
                    if (lastErr != ERROR_ALREADY_EXISTS)
                    {
                        std::wstring msg = FormatStrW(LoadStrW(IDS_CREATEDIRFAILED), newDirs.data());
                        gPrompter->ShowError(errorTitle, msg.c_str());
                        ok = FALSE;
                        break;
                    }
                }

                if (slash != NULL)
                    *slash = L'\\';
                else
                    break;
                st = slash + 1;
            }

            std::wstring changesRoot(path, prefixLen);
            MainWindow->PostChangeOnPathNotificationW(changesRoot.c_str(), FALSE);
            if (!ok)
            {
                wchar_t* e = path + wcslen(path);
                if (e > path && *(e - 1) != L'\\')
                    *e++ = L'\\';
                if (e != mask)
                    memmove(e, mask, (wcslen(mask) + 1) * sizeof(wchar_t));
                return FALSE;
            }
        }
        return TRUE;
    }
    return FALSE;
}

BOOL SalSplitWindowsPathOwnedW(HWND parent, const wchar_t* title, const wchar_t* errorTitle, int selCount,
                               std::wstring& path, size_t secondPartOffset, BOOL pathIsDir,
                               BOOL backslashAtEnd, const wchar_t* dirName, const wchar_t* curDiskPath,
                               std::wstring& mask)
{
    if (secondPartOffset > path.size() ||
        path.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) - 6)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    std::vector<wchar_t> buffer(path.length() + 6, L'\0');
    wmemcpy(buffer.data(), path.c_str(), path.length() + 1);
    wchar_t* splitMask = NULL;
    const BOOL result = SalSplitWindowsPathBufferW(parent, title, errorTitle, selCount,
                                                    buffer.data(), buffer.size(),
                                                    buffer.data() + secondPartOffset, pathIsDir,
                                                    backslashAtEnd, dirName, curDiskPath, splitMask);
    path.assign(buffer.data());
    mask = splitMask != NULL ? splitMask : L"";
    return result;
}

static BOOL SalSplitGeneralPathBufferW(HWND parent, const wchar_t* title, const wchar_t* errorTitle, int selCount,
                                      wchar_t* path, size_t pathCapacity, wchar_t* afterRoot, wchar_t* secondPart,
                                      BOOL pathIsDir, BOOL backslashAtEnd, const wchar_t* dirName,
                                      const wchar_t* curPath, wchar_t*& mask, wchar_t* newDirs,
                                      size_t newDirsCapacity, SGP_IsTheSamePathF isTheSamePathF)
{
    mask = NULL;
    std::vector<wchar_t> tmpNewDirsStorage(wcslen(path) + 1, L'\0');
    wchar_t* tmpNewDirs = tmpNewDirsStorage.data();
    tmpNewDirs[0] = 0;
    if (newDirs != NULL)
        newDirs[0] = 0;

    BOOL result = FALSE;

    if (pathIsDir) // existing part of path is a directory
    {
        if (*secondPart != 0) // there's also non-existent part of path
        {
            // analyze non-existent part of path - file/directory + mask?
            wchar_t* s = secondPart;
            BOOL hasMask = FALSE;
            wchar_t* maskFrom = secondPart;
            while (1)
            {
                while (*s != 0 && *s != L'?' && *s != L'*' && *s != L'\\')
                    s++;
                if (*s == L'\\')
                    maskFrom = ++s;
                else
                {
                    hasMask = (*s != 0);
                    break;
                }
            }

            if (maskFrom != secondPart) // there's some path before the mask
            {
                memcpy(tmpNewDirs, secondPart, (maskFrom - secondPart) * sizeof(wchar_t));
                tmpNewDirs[maskFrom - secondPart] = 0;
            }

            if (hasMask)
            {
                // ensure splitting into path (ending with backslash) and mask
                memmove(maskFrom + 1, maskFrom, (wcslen(maskFrom) + 1) * sizeof(wchar_t));
                *maskFrom++ = 0;

                mask = maskFrom;
            }
            else
            {
                if (!backslashAtEnd) // just name (mask without '*' and '?')
                {
                    if (selCount > 1 &&
                        gPrompter->AskYesNo(title, LoadStrW(IDS_MOVECOPY_NONSENSE)).type != PromptResult::kYes)
                    {
                        return FALSE; // back to copy/move dialog
                    }

                    // ensure splitting into path (ending with backslash) and mask
                    memmove(maskFrom + 1, maskFrom, (wcslen(maskFrom) + 1) * sizeof(wchar_t));
                    *maskFrom++ = 0;

                    mask = maskFrom;
                }
                else // name with slash at end -> directory
                {
                    SalPathAppendW(tmpNewDirs, maskFrom, static_cast<int>(tmpNewDirsStorage.size()));
                    SalPathAddBackslashW(path, static_cast<int>(pathCapacity));
                    mask = path + wcslen(path) + 1;
                    wcscpy(mask, L"*.*");
                }
            }
            CutSpacesFromBothSidesW(mask);

            if (tmpNewDirs[0] != 0) // still need to create those new directories
            {
                if (newDirs != NULL) // creation is supported
                {
                    lstrcpynW(newDirs, tmpNewDirs, static_cast<int>(newDirsCapacity));
                    memmove(tmpNewDirs, path, (secondPart - path) * sizeof(wchar_t));
                    wcscpy(tmpNewDirs + (secondPart - path), newDirs);
                    SalPathRemoveBackslashW(tmpNewDirs);

                    if (Configuration.CnfrmCreatePath) // ask if path should be created
                    {
                        std::wstring msg = FormatStrW(LoadStrW(IDS_MOVECOPY_CREATEPATH), tmpNewDirs);
                        bool dontShow = false;
                        PromptResult res = gPrompter->AskYesNoWithCheckbox(title, msg.c_str(),
                                                                           LoadStrW(IDS_MOVECOPY_CREATEPATH_CNFRM), &dontShow);
                        Configuration.CnfrmCreatePath = !dontShow;
                        if (res.type != PromptResult::kYes)
                        {
                            wchar_t* e = path + wcslen(path); // fix 'path' (join 'path' and 'mask')
                            if (e > path && *(e - 1) != L'\\')
                                *e++ = L'\\';
                            if (e != mask)
                                memmove(e, mask, (wcslen(mask) + 1) * sizeof(wchar_t));
                            return FALSE; // back to copy/move dialog
                        }
                    }
                }
                else
                {
                    gPrompter->ShowError(errorTitle, LoadStrW(IDS_TARGETPATHMUSTEXIST));
                    wchar_t* e = path + wcslen(path); // fix 'path' (join 'path' and 'mask')
                    if (e > path && *(e - 1) != L'\\')
                        *e++ = L'\\';
                    if (e != mask)
                        memmove(e, mask, (wcslen(mask) + 1) * sizeof(wchar_t));
                    return FALSE; // back to copy/move dialog
                }
            }
            result = TRUE; // exit Copy/Move dialog loop and go perform the operation
        }
        else // no non-existent part of path (specified path completely exists)
        {
            if (dirName != NULL && curPath != NULL &&
                !backslashAtEnd && selCount <= 1) // no '\\' at end of path (force directory) + single source
            {
                wchar_t* name = path + wcslen(path);
                while (name >= afterRoot && *(name - 1) != L'\\')
                    name--;
                if (name >= afterRoot && *name != 0)
                {
                    *(name - 1) = 0;
                    if (_wcsicmp(dirName, name) == 0 &&
                        (isTheSamePathF != NULL && isTheSamePathF(path, curPath) ||
                         isTheSamePathF == NULL && IsTheSamePath(path, curPath)))
                    { // renaming directory to same name (except letter case, identity possible)
                        // ensure splitting into path (ending with backslash) and mask
                        memmove(name + 1, name, (wcslen(name) + 1) * sizeof(wchar_t));
                        *(name - 1) = L'\\';
                        *name++ = 0;

                        mask = name;
                        return TRUE; // exit Copy/Move dialog loop and go perform the operation
                    }
                    *(name - 1) = L'\\';
                }
            }

            // simple path target with universal mask
            SalPathAddBackslashW(path, static_cast<int>(pathCapacity));
            mask = path + wcslen(path) + 1;
            wcscpy(mask, L"*.*");
            result = TRUE; // exit Copy/Move dialog loop and go perform the operation
        }
    }
    else // file overwrite - 'secondPart' points to filename in path 'path'
    {
        wchar_t* nameEnd = secondPart;
        while (*nameEnd != 0 && *nameEnd != L'\\')
            nameEnd++;
        if (*nameEnd == 0 && !backslashAtEnd) // renaming/overwriting existing file
        {
            if (selCount > 1 &&
                gPrompter->AskYesNo(title, LoadStrW(IDS_MOVECOPY_NONSENSE)).type != PromptResult::kYes)
            {
                return FALSE; // back to copy/move dialog
            }

            // ensure splitting into path (ending with backslash) and mask
            memmove(secondPart + 1, secondPart, (wcslen(secondPart) + 1) * sizeof(wchar_t));
            *secondPart++ = 0;

            mask = secondPart;
            result = TRUE; // exit Copy/Move dialog loop and go perform the operation
        }
        else // path into archive? not possible here...
        {
            gPrompter->ShowError(errorTitle, LoadStrW(IDS_ARCPATHNOTSUPPORTED));
            if (backslashAtEnd)
                SalPathAddBackslashW(path, static_cast<int>(pathCapacity));
            return FALSE; // back to copy/move dialog
        }
    }

    return result;
}

BOOL SalSplitGeneralPathOwnedW(HWND parent, const wchar_t* title, const wchar_t* errorTitle, int selCount,
                              std::wstring& path, size_t afterRootOffset, size_t secondPartOffset,
                              BOOL pathIsDir, BOOL backslashAtEnd, const wchar_t* dirName,
                              const wchar_t* curPath, std::wstring& mask, std::wstring* newDirs,
                              SGP_IsTheSamePathF isTheSamePathF)
{
    mask.clear();
    if (newDirs != NULL)
        newDirs->clear();
    if (afterRootOffset > path.size() || secondPartOffset > path.size() ||
        path.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) - 6)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    std::vector<wchar_t> buffer(path.size() + 6, L'\0');
    std::wmemcpy(buffer.data(), path.c_str(), path.size() + 1);
    std::vector<wchar_t> newDirsBuffer;
    if (newDirs != NULL)
        newDirsBuffer.assign(path.size() + 1, L'\0');

    wchar_t* splitMask = NULL;
    const BOOL result = SalSplitGeneralPathBufferW(
        parent, title, errorTitle, selCount, buffer.data(), buffer.size(),
        buffer.data() + afterRootOffset, buffer.data() + secondPartOffset,
        pathIsDir, backslashAtEnd, dirName, curPath, splitMask,
        newDirs != NULL ? newDirsBuffer.data() : NULL, newDirsBuffer.size(),
        isTheSamePathF);
    path.assign(buffer.data());
    mask.assign(splitMask != NULL ? splitMask : L"");
    if (newDirs != NULL)
        newDirs->assign(newDirsBuffer.data());
    return result;
}

// 2026-08-26: the narrow NameEndsWithBackslash(char*) and FileNameIsInvalid(char*,
// ...) were deleted - confirmed-dead (zero callers anywhere: core, plugins, tests). Their wide
// siblings, NameEndsWithBackslashW (common/PathDisplayUtils.cpp) and FileNameIsInvalidW (below),
// are the ones actually used - FileNameIsInvalidW alone has 6 real callers (shellib.cpp,
// worker.cpp).

// MakeCopyWithBackslashIfNeededW + NameEndsWithBackslashW moved to common/PathDisplayUtils.cpp (shared with private tests).

BOOL FileNameIsInvalidW(const wchar_t* name, BOOL isFullName, BOOL ignInvalidName)
{
    const wchar_t* s = name;
    if (isFullName && (*s >= L'a' && *s <= L'z' || *s >= L'A' && *s <= L'Z') && *(s + 1) == L':')
        s += 2;
    while (*s != 0 && *s != L':')
        s++;
    if (*s == L':')
        return TRUE;
    if (ignInvalidName)
        return FALSE;
    int nameLen = (int)(s - name);
    return nameLen > 0 && (name[nameLen - 1] <= L' ' || name[nameLen - 1] == L'.');
}

BOOL SalMoveFile(const wchar_t* srcName, const wchar_t* destName)
{
    if (!gFileSystem->MoveFile(srcName, destName).success)
    {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED)
        { // could be a Novell problem (MoveFile returns error for files with read-only attribute)
            DWORD attr = gFileSystem->GetFileAttributes(srcName);
            if (attr != 0xFFFFFFFF && (attr & FILE_ATTRIBUTE_READONLY))
            {
                gFileSystem->SetFileAttributes(srcName, FILE_ATTRIBUTE_ARCHIVE);
                if (gFileSystem->MoveFile(srcName, destName).success)
                {
                    gFileSystem->SetFileAttributes(destName, attr);
                    return TRUE;
                }
                else
                {
                    err = GetLastError();
                    gFileSystem->SetFileAttributes(srcName, attr);
                }
            }
            SetLastError(err);
        }
        return FALSE;
    }
    return TRUE;
}

void RecognizeFileType(HWND parent, const char* pattern, int patternLen, BOOL forceText,
                       BOOL* isText, std::wstring* codePage)
{
    CodeTables.Init(parent);
    CodeTables.RecognizeFileType(pattern, patternLen, forceText, isText, codePage);
}

//*****************************************************************************
//
// CSystemPolicies
//

CSystemPolicies::CSystemPolicies()
    : RestrictRunList(10, 50), DisallowRunList(10, 50)
{
    // enable everything
    EnableAll();
}

CSystemPolicies::~CSystemPolicies()
{
    // release lists
    EnableAll();
}

void CSystemPolicies::EnableAll()
{
    NoRun = 0;
    NoDrives = 0;
    //NoViewOnDrive = 0;
    NoFind = 0;
    NoShellSearchButton = 0;
    NoNetHood = 0;
    //NoEntireNetwork = 0;
    //NoComputersNearMe = 0;
    NoNetConnectDisconnect = 0;
    RestrictRun = 0;
    DisallowRun = 0;
    NoDotBreakInLogicalCompare = 0;

    // uvolnim seznamy alokovanych string

    int i;
    for (i = 0; i < RestrictRunList.Count; i++)
        if (RestrictRunList[i] != NULL)
            free(RestrictRunList[i]);
    RestrictRunList.DetachMembers();

    for (i = 0; i < DisallowRunList.Count; i++)
        if (DisallowRunList[i] != NULL)
            free(DisallowRunList[i]);
    DisallowRunList.DetachMembers();
}

BOOL CSystemPolicies::LoadList(TDirectArray<wchar_t*>* list, HKEY hRootKey, const wchar_t* keyName)
{
    HKEY hKey;
    IRegistry* registry = GetSystemPoliciesRegistry();
    if (registry->OpenKeyRead(hRootKey, keyName, hKey).success)
    {
        std::vector<std::wstring> valueNames;
        if (registry->EnumValues(hKey, valueNames).success)
        {
            for (const auto& valueName : valueNames)
            {
                std::wstring appNameWide;
                if (registry->GetString(hKey, valueName.c_str(), appNameWide).success)
                {
                    // GetString already returns the registry's own wide
                    // value and the list is TDirectArray<wchar_t*>; the WideToAnsi that
                    // used to stand here best-fitted a policy entry so that it no longer
                    // equalled the name being checked. Size in BYTES, length in CHARACTERS.
                    const size_t bytes = (appNameWide.size() + 1) * sizeof(wchar_t);
                    wchar_t* appName = (wchar_t*)malloc(bytes);
                    if (appName == NULL)
                    {
                        registry->CloseKey(hKey);
                        return FALSE;
                    }
                    list->Add(appName);
                    if (!list->IsGood())
                    {
                        list->ResetState();
                        free(appName);
                        registry->CloseKey(hKey);
                        return FALSE;
                    }
                    memcpy(appName, appNameWide.c_str(), bytes);
                }
            }
        }
        registry->CloseKey(hKey);
    }
    return TRUE;
}

BOOL CSystemPolicies::FindNameInList(TDirectArray<wchar_t*>* list, const wchar_t* name)
{
    int i;
    for (i = 0; i < list->Count; i++)
        if (StrICmpW(list->At(i), name) == 0)
            return TRUE;
    return FALSE;
}

BOOL CSystemPolicies::GetMyCanRun(const wchar_t* fileName)
{
    const wchar_t* p = wcsrchr(fileName, L'\\');
    if (p == NULL)
        p = fileName;
    else
        p++;
    // skip spaces from left
    while (*p != 0 && *p == L' ')
        p++;
    std::wstring name(p);
    while (!name.empty() && name.back() == L' ')
        name.pop_back();
    if (DisallowRun != 0)
    {
        if (FindNameInList(&DisallowRunList, name.c_str()))
            return FALSE;
    }
    if (RestrictRun != 0)
    {
        if (!FindNameInList(&RestrictRunList, name.c_str()))
            return FALSE;
    }
    return TRUE;
}

void CSystemPolicies::LoadFromRegistry()
{
    // enable everything
    EnableAll();

    // pull restrictions
    IRegistry* registry = GetSystemPoliciesRegistry();
    HKEY hKey;
    if (registry->OpenKeyRead(HKEY_CURRENT_USER, SAL_REG_KEY_POLICIES_EXPLORER_CURRENT_USER_W, hKey).success)
    {
        // according to MSDN values can be DWORD and BINARY:
        // It is a REG_DWORD or 4-byte REG_BINARY data value, found under the same key.
        GetValueDontCheckTypeViaRegistry(registry, hKey, SAL_REG_VALUE_NO_RUN_W, /*REG_DWORD,*/ &NoRun, sizeof(DWORD));
        GetValueDontCheckTypeViaRegistry(registry, hKey, SAL_REG_VALUE_NO_DRIVES_W, /*REG_DWORD,*/ &NoDrives, sizeof(DWORD));
        //GetValueDontCheckTypeViaRegistry(registry, hKey, "NoViewOnDrive", /*REG_DWORD,*/ &NoViewOnDrive, sizeof(DWORD));
        GetValueDontCheckTypeViaRegistry(registry, hKey, SAL_REG_VALUE_NO_FIND_W, /*REG_DWORD,*/ &NoFind, sizeof(DWORD));
        GetValueDontCheckTypeViaRegistry(registry, hKey, SAL_REG_VALUE_NO_SHELL_SEARCH_BUTTON_W, /*REG_DWORD,*/ &NoShellSearchButton, sizeof(DWORD));
        GetValueDontCheckTypeViaRegistry(registry, hKey, SAL_REG_VALUE_NO_NET_HOOD_W, /*REG_DWORD,*/ &NoNetHood, sizeof(DWORD));
        //GetValueDontCheckTypeViaRegistry(registry, hKey, "NoComputersNearMe", /*REG_DWORD,*/ &NoComputersNearMe, sizeof(DWORD));
        GetValueDontCheckTypeViaRegistry(registry, hKey, SAL_REG_VALUE_NO_NET_CONNECT_DISCONNECT_W, /*REG_DWORD,*/ &NoNetConnectDisconnect, sizeof(DWORD));
        GetValueDontCheckTypeViaRegistry(registry, hKey, SAL_REG_VALUE_RESTRICT_RUN_W, /*REG_DWORD,*/ &RestrictRun, sizeof(DWORD));
        if (RestrictRun && !LoadList(&RestrictRunList, HKEY_CURRENT_USER, SAL_REG_KEY_POLICIES_EXPLORER_RESTRICT_RUN_CURRENT_USER_W))
            RestrictRun = 0; // low memory; disable this option
        GetValueDontCheckTypeViaRegistry(registry, hKey, SAL_REG_VALUE_DISALLOW_RUN_W, /*REG_DWORD,*/ &DisallowRun, sizeof(DWORD));
        if (DisallowRun && !LoadList(&DisallowRunList, HKEY_CURRENT_USER, SAL_REG_KEY_POLICIES_EXPLORER_DISALLOW_RUN_CURRENT_USER_W))
            DisallowRun = 0; // low memory; disable this option
        registry->CloseKey(hKey);
    }

    if (registry->OpenKeyRead(HKEY_CURRENT_USER, SAL_REG_KEY_POLICIES_EXPLORER_MACHINE_W, hKey).success)
    {
        GetValueDontCheckTypeViaRegistry(registry, hKey, SAL_REG_VALUE_NO_DOT_BREAK_IN_LOGICAL_COMPARE_W, /*REG_DWORD,*/ &NoDotBreakInLogicalCompare, sizeof(DWORD));
        registry->CloseKey(hKey);
    }
    if (registry->OpenKeyRead(HKEY_LOCAL_MACHINE, SAL_REG_KEY_POLICIES_EXPLORER_MACHINE_W, hKey).success)
    {
        GetValueDontCheckTypeViaRegistry(registry, hKey, SAL_REG_VALUE_NO_DOT_BREAK_IN_LOGICAL_COMPARE_W, /*REG_DWORD,*/ &NoDotBreakInLogicalCompare, sizeof(DWORD));
        registry->CloseKey(hKey);
    }
}

BOOL SalGetFileSize(HANDLE file, CQuadWord& size, DWORD& err)
{
    CALL_STACK_MESSAGE1("SalGetFileSize(, ,)");
    if (file == NULL || file == INVALID_HANDLE_VALUE)
    {
        TRACE_E("SalGetFileSize(): file handle is invalid!");
        err = ERROR_INVALID_HANDLE;
        size.Set(0, 0);
        return FALSE;
    }

    uint64_t value = 0;
    const FileResult result = gFileSystem->GetHandleFileSize(file, &value);
    if (result.success)
    {
        size.Set((DWORD)value, (DWORD)(value >> 32));
        err = NO_ERROR;
        return TRUE;
    }
    err = result.errorCode;
    size.Set(0, 0);
    return FALSE;
}

BOOL SalGetFileSize2(const wchar_t* fileName, CQuadWord& size, DWORD* err)
{
    HANDLE hFile = gFileSystem->CreateFile(fileName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                           NULL, OPEN_EXISTING, 0, NULL);
    HANDLES_ADD_EX(__otQuiet, hFile != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, hFile, GetLastError(), TRUE);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD dummyErr;
        BOOL ret = SalGetFileSize(hFile, size, err != NULL ? *err : dummyErr);
        HANDLES_REMOVE(hFile, __htFile, "IFileSystem::CloseHandle");
        gFileSystem->CloseFileHandle(hFile);
        return ret;
    }
    if (err != NULL)
        *err = GetLastError();
    size.Set(0, 0);
    return FALSE;
}

// The trailing space/dot workaround below is a PATH-SYNTAX rule, not a
// text rule - Win32 trims trailing spaces and dots, so appending '\\' is what makes the query hit
// the file the caller named. It is reproduced here exactly; widening must not quietly change which
// file gets queried.
DWORD SalGetFileAttributes(const wchar_t* fileName)
{
    CALL_STACK_MESSAGE1("SalGetFileAttributes()");
    if (fileName == NULL)
        return INVALID_FILE_ATTRIBUTES;

    size_t nameLen = wcslen(fileName);
    if (nameLen > 0 && (fileName[nameLen - 1] <= L' ' || fileName[nameLen - 1] == L'.'))
    {
        std::wstring fileNameCopy(fileName);
        fileNameCopy += L'\\';
        return gFileSystem->GetFileAttributes(fileNameCopy.c_str());
    }

    return gFileSystem->GetFileAttributes(fileName);
}

BOOL ClearReadOnlyAttr(const wchar_t* name, DWORD attr)
{
    if (attr == (DWORD)-1)
        attr = gFileSystem->GetFileAttributes(name);
    if (attr != INVALID_FILE_ATTRIBUTES)
    {
        // only drop RO (for hardlinks it also changes attributes of other hardlinks to the same file, so keep it minimal)
        if ((attr & FILE_ATTRIBUTE_READONLY) != 0)
        {
            if (!gFileSystem->SetFileAttributes(name, attr & ~FILE_ATTRIBUTE_READONLY).success)
                TRACE_EW(L"ClearReadOnlyAttr(): error setting attrs (0x" << std::hex << (attr & ~FILE_ATTRIBUTE_READONLY) << std::dec << L"): " << name);
            return TRUE;
        }
    }
    else
    {
        TRACE_EW(L"ClearReadOnlyAttr(): error getting attrs: " << name);
        if (!gFileSystem->SetFileAttributes(name, FILE_ATTRIBUTE_ARCHIVE).success) // cannot read attributes, try at least writing (don't care if it's needed)
            TRACE_EW(L"ClearReadOnlyAttr(): error setting attrs (FILE_ATTRIBUTE_ARCHIVE): " << name);
        return TRUE;
    }
    return FALSE;
}

// Wide throughout. The narrow original matched the caller's path against
// the ANSI network enumeration - mirror against mirror - so two different servers the code
// page cannot spell compared EQUAL and the WRONG provider was returned. That answer decides
// whether fast-directory-move is disabled (Novell) and which write path the worker takes
// (Lantastic), so the failure is a wrong operation strategy rather than an error.
//
// WNetEnumResourceW is not a courtesy sibling here: the enumeration is the authority being
// compared against, so narrowing it corrupts both sides of the comparison at once.
BOOL IsNetworkProviderDriveW(const wchar_t* path, DWORD providerType)
{
    HANDLE hEnumNet;
    DWORD err = WNetOpenEnumW(RESOURCE_CONNECTED, RESOURCETYPE_DISK,
                              RESOURCEUSAGE_CONNECTABLE, NULL, &hEnumNet);
    if (err == NO_ERROR)
    {
        std::wstring provider;
        BOOL haveProvider = FALSE;
        DWORD bufSize;
        BYTE buf[1000];
        NETRESOURCEW* netSource = (NETRESOURCEW*)buf;
        while (1)
        {
            DWORD e = 1;
            bufSize = sizeof(buf);
            err = WNetEnumResourceW(hEnumNet, &e, netSource, &bufSize);
            if (err == NO_ERROR && e == 1)
            {
                const BOOL matched =
                    path[0] == L'\\'
                        ? (netSource->lpRemoteName != NULL &&
                           HasTheSameRootPath(path, netSource->lpRemoteName))
                        : (netSource->lpLocalName != NULL &&
                           towlower(path[0]) == towlower(netSource->lpLocalName[0]));
                if (matched)
                {
                    // As before, a match ends the search whether or not it names a
                    // provider; a NULL lpProvider simply leaves nothing to identify.
                    if (netSource->lpProvider != NULL)
                    {
                        provider = netSource->lpProvider;
                        haveProvider = TRUE;
                    }
                    break;
                }
            }
            else
                break;
        }
        WNetCloseEnum(hEnumNet);

        if (haveProvider)
        {
            NETINFOSTRUCT ni; // no string fields, so it has no A/W split
            memset(&ni, 0, sizeof(ni));
            ni.cbStructure = sizeof(ni);
            if (WNetGetNetworkInformationW(provider.c_str(), &ni) == NO_ERROR)
            {
                return ni.wNetType == HIWORD(providerType);
            }
        }
    }
    return FALSE;
}

// No ANSI IsNetworkProviderDrive: its only two callers were IsNOVELLDrive and
// IsLantasticDrive, and both now go through the wide form. Not wrapped, deleted.

BOOL IsNOVELLDriveW(const wchar_t* path)
{
    return IsNetworkProviderDriveW(path, WNNC_NET_NETWARE);
}

BOOL IsLantasticDriveW(const wchar_t* path, std::wstring& lastLantasticCheckRoot,
                       BOOL& lastIsLantasticPath)
{
    if (!lastLantasticCheckRoot.empty() &&
        HasTheSameRootPath(lastLantasticCheckRoot.c_str(), path))
    {
        return lastIsLantasticPath;
    }

    lastLantasticCheckRoot = GetRootPath(path);
    lastIsLantasticPath = FALSE;
    if (path[0] != L'\\') // not UNC - may not be a network path (which cannot be LANTASTIC)
    {
        if (GetDriveTypeW(lastLantasticCheckRoot.c_str()) != DRIVE_REMOTE)
            return FALSE; // not a network path
    }

    return lastIsLantasticPath =
               IsNetworkProviderDriveW(lastLantasticCheckRoot.c_str(), WNNC_NET_LANTASTIC);
}

HCURSOR SetHandCursor()
{
    // pouzijeme systemovy kurzor -- zamezime zbytecnemu
    // poblikavani pri zmene kurzoru
    return SetCursor(LoadCursor(NULL, IDC_HAND));
}

void WaitForESCRelease()
{
    int c = 20; // wait up to 1/5 second for ESC release (so ESC in dialog doesn't immediately interrupt directory reading)
    while (c--)
    {
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8001) == 0)
            break;
        Sleep(10);
    }
}

void GetListViewContextMenuPos(HWND hListView, POINT* p)
{
    if (ListView_GetItemCount(hListView) == 0)
    {
        p->x = 0;
        p->y = 0;
        ClientToScreen(hListView, p);
        return;
    }
    int focIndex = ListView_GetNextItem(hListView, -1, LVNI_FOCUSED);
    if (focIndex != -1)
    {
        if ((ListView_GetItemState(hListView, focIndex, LVNI_SELECTED) & LVNI_SELECTED) == 0)
            focIndex = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
    }
    RECT cr;
    GetClientRect(hListView, &cr);
    RECT r;
    ListView_GetItemRect(hListView, 0, &r, LVIR_LABEL);
    p->x = r.left;
    if (p->x < 0)
        p->x = 0;
    if (focIndex != -1)
        ListView_GetItemRect(hListView, focIndex, &r, LVIR_BOUNDS);
    if (focIndex == -1 || r.bottom < 0 || r.bottom > cr.bottom)
        r.bottom = 0;
    p->y = r.bottom;
    ClientToScreen(hListView, p);
}

// Wide form of the reserved-name test. Device names are pure ASCII, so this compares
// against ASCII literals directly rather than narrowing a name that may not survive it.
static BOOL IsDeviceNameAuxW(const wchar_t* s, const wchar_t* end)
{
    while (end > s && *(end - 1) <= L' ')
        end--;
    static const wchar_t* dev1_arr[] = {L"CON", L"PRN", L"AUX", L"NUL", NULL};
    if (end - s == 3)
    {
        const wchar_t** dev1 = dev1_arr;
        while (*dev1 != NULL)
            if (_wcsnicmp(s, *dev1++, 3) == 0)
                return TRUE;
    }
    static const wchar_t* dev2_arr[] = {L"COM", L"LPT", NULL};
    if (end - s == 4 && *(end - 1) >= L'1' && *(end - 1) <= L'9')
    {
        const wchar_t** dev2 = dev2_arr;
        while (*dev2 != NULL)
            if (_wcsnicmp(s, *dev2++, 3) == 0)
                return TRUE;
    }
    return FALSE;
}

// Validates the native-wide name directly: no trailing space or dot, no control characters
// or shell metacharacters, and no reserved device name.
BOOL SalIsValidFileNameComponentW(const wchar_t* fileNameComponent)
{
    if (fileNameComponent == NULL)
        return FALSE;

    const wchar_t* start = fileNameComponent;
    const size_t len = wcslen(fileNameComponent);
    // test white-spaces and '.' at end of name (file-system would trim them)
    if (len > 0 && (start[len - 1] <= L' ' || start[len - 1] == L'.'))
        return FALSE;

    BOOL testSimple = TRUE;
    BOOL simple = TRUE; // TRUE = risk of "lpt1", "prn" and other critical names
    BOOL wasSpace = FALSE;
    BOOL allAscii = TRUE;

    for (const wchar_t* p = start; *p != 0; p++)
    {
        if (*p > 0x7f)
            allAscii = FALSE;

        if (testSimple && *p > L' ' &&
            (*p < L'a' || *p > L'z') &&
            (*p < L'A' || *p > L'Z') &&
            (*p < L'0' || *p > L'9'))
        {
            simple = FALSE; // "prn.txt" and "prn  .txt" are reserved names
            testSimple = FALSE;
            if (*p == L'.' && p > start && IsDeviceNameAuxW(start, p))
                return FALSE;
        }
        if (*p <= L' ')
        {
            wasSpace = TRUE;
            if (*p != L' ')
                return FALSE; // disallowed white-space
        }
        else
        {
            if (testSimple && wasSpace)
            {
                simple = FALSE; // "prn bla.txt" is not a reserved name
                testSimple = FALSE;
            }
        }
        switch (*p)
        {
        case L'*':
        case L'?':
        case L'\\':
        case L'/':
        case L'<':
        case L'>':
        case L'|':
        case L'"':
        case L':':
            return FALSE; // disallowed character
        }
    }
    if (simple && allAscii && IsDeviceNameAuxW(start, start + len))
        return FALSE; // simple name + device
    return TRUE;
}

// Returns the valid native-wide form without an ACP mirror or path-sized ceiling.
std::wstring SalMakeValidFileNameComponentW(const wchar_t* fileNameComponent)
{
    std::wstring result = fileNameComponent != NULL ? fileNameComponent : L"";

    // trim white-spaces and '.' at end of name (file-system would do it anyway, at least it's clear immediately)
    size_t end = result.length();
    while (end > 0 && (result[end - 1] <= L' ' || result[end - 1] == L'.'))
        end--;
    if (end < result.length())
        result.resize(end);
    if (result.empty()) // empty string or sequence of '.' and white-spaces -> replace with name "_"
        result = L"_";

    BOOL testSimple = TRUE;
    BOOL simple = TRUE; // TRUE = risk of "lpt1", "prn" and other critical names, better add '_'
    BOOL wasSpace = FALSE;

    for (size_t i = 0; i < result.length(); i++)
    {
        if (testSimple && result[i] > L' ' &&
            (result[i] < L'a' || result[i] > L'z') &&
            (result[i] < L'A' || result[i] > L'Z') &&
            (result[i] < L'0' || result[i] > L'9'))
        {
            simple = FALSE; // "prn.txt" and "prn  .txt" are reserved names
            testSimple = FALSE;
            if (result[i] == L'.' && i > 0 && IsDeviceNameAuxW(result.c_str(), result.c_str() + i))
            {
                // insert '_' right before this dot, same as the narrow version
                result.insert(result.begin() + i, L'_');
                i++; // the dot moved one position to the right - continue from it
                if (i >= result.length())
                    break;
            }
        }
        if (result[i] <= L' ')
        {
            wasSpace = TRUE;
            result[i] = L' '; // replace all white-spaces with ' '
        }
        else
        {
            if (testSimple && wasSpace)
            {
                simple = FALSE; // "prn bla.txt" is not a reserved name
                testSimple = FALSE;
            }
        }
        switch (result[i])
        {
        case L'*':
        case L'?':
        case L'\\':
        case L'/':
        case L'<':
        case L'>':
        case L'|':
        case L'"':
        case L':':
            result[i] = L'_';
            break;
        }
    }
    if (simple && IsDeviceNameAuxW(result.c_str(), result.c_str() + result.length())) // for simple names add '_'
        result += L'_';

    return result;
}

typedef struct tagTHREADNAME_INFO
{
    DWORD dwType;     // must be 0x1000
    LPCSTR szName;    // pointer to name (in user addr space)
    DWORD dwThreadID; // thread ID (-1=caller thread)
    DWORD dwFlags;    // reserved for future use, must be zero
} THREADNAME_INFO;

static void RaiseLegacyThreadNameInVC(const char* threadName)
{
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = threadName;
    info.dwThreadID = -1 /* caller thread */;
    info.dwFlags = 0;

    __try
    {
        RaiseException(0x406D1388, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR*)&info);
    }
    __except (EXCEPTION_CONTINUE_EXECUTION)
    {
    }
}

static void SetLegacyThreadNameInVC(const wchar_t* threadName) noexcept
{
    const std::string legacyThreadName =
        sally::diagnostic::EncodeAcpLossy(threadName != nullptr ? threadName : L"");
    RaiseLegacyThreadNameInVC(legacyThreadName.c_str());
}

void SetThreadNameInVC(const wchar_t* threadName)
{
    SetLegacyThreadNameInVC(threadName);
}

void SetThreadNameInVCAndTrace(const wchar_t* name)
{
    SetTraceThreadNameW(name);
    SetThreadNameInVC(name);
}

// SHGetFolderPathW has a fixed MAX_PATH output contract. Keep that storage local to this
// adapter, then move the semantic path into dynamically-owned UTF-16 before appending.
BOOL CreateOurPathInRoamingAPPDATAW(std::wstring& path)
{
    static wchar_t appData[MAX_PATH]; // may run from an exception path with little stack available
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0 /* SHGFP_TYPE_CURRENT */, appData) != S_OK)
        return FALSE;

    path.assign(appData);
    SalPathAddBackslashW(path);
    path += L"Sally";
    gFileSystem->CreateDirectory(path.c_str()); // if it fails (e.g. already exists), we don't care...
    return TRUE;
}

void SlashesToBackslashesAndRemoveDups(std::wstring& path)
{
    size_t write = 0;
    for (wchar_t ch : path)
    {
        if (ch == L'/')
            ch = L'\\';
        if (ch == L'\\' && write > 1 && path[write - 1] == L'\\')
            continue;
        path[write++] = ch;
    }
    path.resize(write);
}
