// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "wmobile_treewalk_core.h"

#define COPY_BUFFER_SIZE (32 * 1024)

#define RAPI_FUNCTIONS \
    Func_Operator(CeRapiInitEx) \
        Func_Operator(CeRapiInit) \
            Func_Operator(CeRapiUninit) \
                Func_Operator(CeRapiGetError) \
                    Func_Operator(CeRapiFreeBuffer) \
                        Func_Operator(CeRapiInvoke) \
                            Func_Operator(CeCreateDatabase) \
                                Func_Operator(CeDeleteDatabase) \
                                    Func_Operator(CeDeleteRecord) \
                                        Func_Operator(CeFindFirstDatabase) \
                                            Func_Operator(CeFindNextDatabase) \
                                                Func_Operator(CeOidGetInfo) \
                                                    Func_Operator(CeOpenDatabase) \
                                                        Func_Operator(CeReadRecordProps) \
                                                            Func_Operator(CeSeekDatabase) \
                                                                Func_Operator(CeSetDatabaseInfo) \
                                                                    Func_Operator(CeWriteRecordProps) \
                                                                        Func_Operator(CeFindFirstFile) \
                                                                            Func_Operator(CeFindNextFile) \
                                                                                Func_Operator(CeFindClose) \
                                                                                    Func_Operator(CeGetFileAttributes) \
                                                                                        Func_Operator(CeSetFileAttributes) \
                                                                                            Func_Operator(CeCreateFile) \
                                                                                                Func_Operator(CeReadFile) \
                                                                                                    Func_Operator(CeWriteFile) \
                                                                                                        Func_Operator(CeCloseHandle) \
                                                                                                            Func_Operator(CeFindAllFiles) \
                                                                                                                Func_Operator(CeFindAllDatabases) \
                                                                                                                    Func_Operator(CeGetLastError) \
                                                                                                                        Func_Operator(CeSetFilePointer) \
                                                                                                                            Func_Operator(CeSetEndOfFile) \
                                                                                                                                Func_Operator(CeCreateDirectory) \
                                                                                                                                    Func_Operator(CeRemoveDirectory) \
                                                                                                                                        Func_Operator(CeCreateProcess) \
                                                                                                                                            Func_Operator(CeMoveFile) \
                                                                                                                                                Func_Operator(CeCopyFile) \
                                                                                                                                                    Func_Operator(CeDeleteFile) \
                                                                                                                                                        Func_Operator(CeGetFileSize) \
                                                                                                                                                            Func_Operator(CeRegOpenKeyEx) \
                                                                                                                                                                Func_Operator(CeRegEnumKeyEx) \
                                                                                                                                                                    Func_Operator(CeRegCreateKeyEx) \
                                                                                                                                                                        Func_Operator(CeRegCloseKey) \
                                                                                                                                                                            Func_Operator(CeRegDeleteKey) \
                                                                                                                                                                                Func_Operator(CeRegEnumValue) \
                                                                                                                                                                                    Func_Operator(CeRegDeleteValue) \
                                                                                                                                                                                        Func_Operator(CeRegQueryInfoKey) \
                                                                                                                                                                                            Func_Operator(CeRegQueryValueEx) \
                                                                                                                                                                                                Func_Operator(CeRegSetValueEx) \
                                                                                                                                                                                                    Func_Operator(CeGetStoreInformation) \
                                                                                                                                                                                                        Func_Operator(CeGetSystemMetrics) \
                                                                                                                                                                                                            Func_Operator(CeGetDesktopDeviceCaps) \
                                                                                                                                                                                                                Func_Operator(CeGetSystemInfo) \
                                                                                                                                                                                                                    Func_Operator(CeSHCreateShortcut) \
                                                                                                                                                                                                                        Func_Operator(CeSHGetShortcutTarget) \
                                                                                                                                                                                                                            Func_Operator(CeCheckPassword) \
                                                                                                                                                                                                                                Func_Operator(CeGetFileTime) \
                                                                                                                                                                                                                                    Func_Operator(CeSetFileTime) \
                                                                                                                                                                                                                                        Func_Operator(CeGetVersionEx) \
                                                                                                                                                                                                                                            Func_Operator(CeGetWindow) \
                                                                                                                                                                                                                                                Func_Operator(CeGetWindowLong) \
                                                                                                                                                                                                                                                    Func_Operator(CeGetWindowText) \
                                                                                                                                                                                                                                                        Func_Operator(CeGetClassName) \
                                                                                                                                                                                                                                                            Func_Operator(CeGlobalMemoryStatus) \
                                                                                                                                                                                                                                                                Func_Operator(CeGetSystemPowerStatusEx) \
                                                                                                                                                                                                                                                                    Func_Operator(CeGetTempPath) \
                                                                                                                                                                                                                                                                        Func_Operator(CeGetSpecialFolderPath) \
                                                                                                                                                                                                                                                                            Func_Operator(CeFindFirstDatabaseEx) \
                                                                                                                                                                                                                                                                                Func_Operator(CeFindNextDatabaseEx) \
                                                                                                                                                                                                                                                                                    Func_Operator(CeCreateDatabaseEx) \
                                                                                                                                                                                                                                                                                        Func_Operator(CeSetDatabaseInfoEx) \
                                                                                                                                                                                                                                                                                            Func_Operator(CeOpenDatabaseEx) \
                                                                                                                                                                                                                                                                                                Func_Operator(CeDeleteDatabaseEx) \
                                                                                                                                                                                                                                                                                                    Func_Operator(CeReadRecordPropsEx) \
                                                                                                                                                                                                                                                                                                        Func_Operator(CeMountDBVol) \
                                                                                                                                                                                                                                                                                                            Func_Operator(CeUnmountDBVol) \
                                                                                                                                                                                                                                                                                                                Func_Operator(CeFlushDBVol) \
                                                                                                                                                                                                                                                                                                                    Func_Operator(CeEnumDBVolumes) \
                                                                                                                                                                                                                                                                                                                        Func_Operator(CeOidGetInfoEx)

class CDynRapi
{
    static HINSTANCE m_hLib;

public:
    static BOOL Load()
    {
        if (m_hLib != NULL)
            return TRUE;

        // Load the library
        UINT fuError;
        fuError = SetErrorMode(SEM_NOOPENFILEERRORBOX);
        m_hLib = LoadLibraryW(L"rapi.dll");
        SetErrorMode(fuError);

        if (m_hLib == NULL)
        {
            TRACE_E("CDynRapi::Load() Failed to load rapi.dll");
            return FALSE;
        }

// Hook pointers to its exports
#define Func_Operator(Name) Name = (RapiNS::Name*)GetProcAddress(m_hLib, #Name);
        RAPI_FUNCTIONS
#undef Func_Operator

        if (!IsGood()) // And verify everything
        {
            Unload();
            return FALSE;
        }

        return TRUE;
    }

    static void Unload()
    {
        if (m_hLib == NULL)
            return;

#define Func_Operator(Name) Name = NULL;
        RAPI_FUNCTIONS
#undef Func_Operator

        FreeLibrary(m_hLib);
        m_hLib = NULL;
    }

    static BOOL IsGood()
    {
        // The library must be loaded
        if (m_hLib == NULL)
            return FALSE;

// All exports must be linked
#define Func_Operator(Name) \
    if (Name == NULL) \
    { \
        TRACE_E("Export " << #Name << " was not found in the Rapi.dll"); \
        return FALSE; \
    }
        RAPI_FUNCTIONS
#undef Func_Operator

        return TRUE;
    }

#define Func_Operator(Name) static RapiNS::Name* Name;
    RAPI_FUNCTIONS
#undef Func_Operator
};

#define Func_Operator(Name) RapiNS::Name* CDynRapi::Name = NULL;
RAPI_FUNCTIONS
#undef Func_Operator

HINSTANCE CDynRapi::m_hLib = NULL;

CDynRapi dynRapi;

/////////////////////////////////////////////////////////////////////////////
// CRAPI class

BOOL CRAPI::initialized = FALSE;

BOOL CRAPI::Init()
{
    if (initialized)
        return TRUE;

    if (CDynRapi::Load())
        ReInit();

    if (!initialized)
    {
        TRACE_E("CRAPI::Init() Failed");

        CDynRapi::Unload();

        SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_CONNECTION).c_str(), TitleWMobileError, MSGBOX_ERROR);
    }

    return initialized;
}

void CRAPI::UnInit()
{
    if (initialized)
    {
        CDynRapi::CeRapiUninit();
        initialized = FALSE;
    }
    CDynRapi::Unload();
}

BOOL CRAPI::ReInit()
{
    if (initialized)
    {
        CDynRapi::CeRapiUninit();
        initialized = FALSE;

        CPluginFSInterface::EmptyCache(); // The device might have been swapped, so clear the cache
    }

    //JR REVIEW: Should we show a "Please wait" dialog with a Cancel button?
    SalamanderGeneral->CreateSafeWaitWindow(LangStr(IDS_CONNECTING).c_str(), TitleWMobile,
                                            500, FALSE, SalamanderGeneral->GetMainWindowHWND());

    //  Sleep(500); // JR REVIEW: Why was this needed?

    HANDLE hExit = CreateEvent(NULL, FALSE, FALSE, NULL);
    HRESULT hRapiResult = InitRapi(hExit, 3000);
    CloseHandle(hExit);

    if (SUCCEEDED(hRapiResult))
        initialized = TRUE;

    SalamanderGeneral->DestroySafeWaitWindow();

    return initialized;
}

/////////////////////////////////////////////////////////////////////////////
//RAPI

BOOL CRAPI::FindNextFile(HANDLE hFindFile, RapiNS::LPCE_FIND_DATA lpFindFileData)
{
    return CDynRapi::CeFindNextFile(hFindFile, lpFindFileData);
}

BOOL CRAPI::FindClose(HANDLE hFindFile)
{
    return CDynRapi::CeFindClose(hFindFile);
}

HANDLE
CRAPI::FindFirstFileWide(const wchar_t* lpFileName, RapiNS::LPCE_FIND_DATA lpFindFileData, BOOL tryReinit)
{
    if (lpFileName == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return INVALID_HANDLE_VALUE;
    }

    HANDLE hFindFile = CDynRapi::CeFindFirstFile(lpFileName, lpFindFileData);
    if (hFindFile == INVALID_HANDLE_VALUE && tryReinit && GetLastError() != ERROR_NO_MORE_FILES && ReInit())
        hFindFile = CDynRapi::CeFindFirstFile(lpFileName, lpFindFileData);

    return hFindFile;
}

// Windows CE is Unicode-native; semantic device paths stay UTF-16 through CRAPI.
DWORD
CRAPI::GetFileAttributesWide(const wchar_t* lpFileName, BOOL tryReinit)
{
    if (lpFileName == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return 0xFFFFFFFF;
    }

    DWORD attr = CDynRapi::CeGetFileAttributes(lpFileName);
    if (attr == 0xFFFFFFFF && tryReinit && GetLastError() != ERROR_FILE_NOT_FOUND && ReInit())
        attr = CDynRapi::CeGetFileAttributes(lpFileName);

    return attr;
}

HANDLE
CRAPI::CreateFileWide(const wchar_t* lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    if (lpFileName == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return INVALID_HANDLE_VALUE;
    }

    return CDynRapi::CeCreateFile(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

BOOL CRAPI::GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime)
{
    return CDynRapi::CeGetFileTime(hFile, lpCreationTime, lpLastAccessTime, lpLastWriteTime);
}

BOOL CRAPI::SetFileTime(HANDLE hFile, FILETIME* lpCreationTime, FILETIME* lpLastAccessTime, FILETIME* lpLastWriteTime)
{
    return CDynRapi::CeSetFileTime(hFile, lpCreationTime, lpLastAccessTime, lpLastWriteTime);
}

DWORD
CRAPI::GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
    return CDynRapi::CeGetFileSize(hFile, lpFileSizeHigh);
}

BOOL CRAPI::GetStoreInformation(RapiNS::LPSTORE_INFORMATION lpsi)
{
    return CDynRapi::CeGetStoreInformation(lpsi);
}

BOOL CRAPI::CloseHandle(HANDLE hObject)
{
    return CDynRapi::CeCloseHandle(hObject);
}

BOOL CRAPI::ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
    return CDynRapi::CeReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

BOOL CRAPI::WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
    return CDynRapi::CeWriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);
}

// The wide delete path. No conversion at all: the caller's UTF-16 path goes
// straight to CE, which is what it has always wanted. A device file whose name is outside the
// machine's code page can now be deleted - previously the narrow wrapper refused it, so such a
// file was visible but not removable.
BOOL CRAPI::CreateDirectoryWide(const wchar_t* lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
    if (lpPathName == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return CDynRapi::CeCreateDirectory(lpPathName, lpSecurityAttributes);
}

BOOL CRAPI::MoveFileWide(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName)
{
    if (lpExistingFileName == NULL || lpNewFileName == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return CDynRapi::CeMoveFile(lpExistingFileName, lpNewFileName);
}

BOOL CRAPI::DeleteFileWide(const wchar_t* lpFileName)
{
    if (lpFileName == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return CDynRapi::CeDeleteFile(lpFileName);
}

BOOL CRAPI::RemoveDirectoryWide(const wchar_t* lpPathName)
{
    if (lpPathName == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return CDynRapi::CeRemoveDirectory(lpPathName);
}

BOOL CRAPI::SetFileAttributesWide(const wchar_t* lpFileName, DWORD dwFileAttributes)
{
    if (lpFileName == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return CDynRapi::CeSetFileAttributes(lpFileName, dwFileAttributes);
}
BOOL CRAPI::FindAllFilesWide(const wchar_t* path, DWORD flags, LPDWORD foundCount,
                             RapiNS::LPLPCE_FIND_DATA findData, BOOL tryReinit)
{
    if (path == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    BOOL ret = CDynRapi::CeFindAllFiles(path, flags, foundCount, findData);
    if (!ret && tryReinit && GetLastError() != ERROR_NO_MORE_FILES && ReInit())
        ret = CDynRapi::CeFindAllFiles(path, flags, foundCount, findData);
    return ret;
}

BOOL CRAPI::CreateProcessWide(const wchar_t* lpApplicationName, const wchar_t* lpCommandLine)
{
    if (lpApplicationName == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return CDynRapi::CeCreateProcess(lpApplicationName, lpCommandLine, NULL, NULL, FALSE, 0,
                                     NULL, NULL, NULL, NULL);
}

BOOL CRAPI::SHGetShortcutTargetWide(const wchar_t* shortcut, std::wstring& target)
{
    // CeSHGetShortcutTarget has no sizing query. Its Windows CE API contract writes at most
    // 260 WCHARs; keep that fixed storage entirely inside this adapter.
    static constexpr size_t WindowsCeShortcutTargetCapacity = 260;
    target.clear();
    if (shortcut == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    std::vector<wchar_t> shortcutBuffer(shortcut, shortcut + wcslen(shortcut) + 1);
    wchar_t targetBuffer[WindowsCeShortcutTargetCapacity] = {};
    if (!CDynRapi::CeSHGetShortcutTarget(shortcutBuffer.data(), targetBuffer, _countof(targetBuffer)))
        return FALSE;
    target.assign(targetBuffer);
    return TRUE;
}

HRESULT
CRAPI::FreeBuffer(LPVOID Buffer)
{
    return CDynRapi::CeRapiFreeBuffer(Buffer);
}

DWORD
CRAPI::GetLastError(void)
{
    DWORD err = CDynRapi::CeGetLastError();
    if (err == 0)
        err = CDynRapi::CeRapiGetError();

    return err;
}

HRESULT
CRAPI::RapiGetError(void)
{
    return CDynRapi::CeRapiGetError();
}

/////////////////////////////////////////////////////////////////////////////
// Helpers

// The device enumerator behind the tested walk (wmobile_treewalk_core).
//
// It calls CDynRapi's Ce* functions DIRECTLY rather than going through CRAPI's narrow
// wrappers. Those functions have always been wide - Windows CE has no ANSI code page at all -
// so the wrapper's narrow signatures were a Win32-shaped convenience, and skipping them here
// makes the walk Unicode-clean without widening the rest of CRAPI first.
//
// What that fixes: the previous recursion carried its directory path in a narrow buffer and
// said so in its own comment - a directory whose name could not survive a CP_ACP round trip
// was listed but never entered, making its entire contents invisible to listing, copy and
// delete. See DescendsIntoNamesOutsideTheAnsiCodePage in gtest_wmobile_treewalk_core.
class CRapiDeviceEnumerator : public wmobile::DeviceEnumerator
{
public:
    bool Enumerate(const wchar_t* searchPath, std::vector<wmobile::DeviceEntry>& out) override
    {
        RapiNS::CE_FIND_DATA data;
        HANDLE find = CDynRapi::CeFindFirstFile(searchPath, &data);
        if (find == INVALID_HANDLE_VALUE)
        {
            // An empty directory is not a failure. Some storage implementations report
            // ERROR_FILE_NOT_FOUND where others report ERROR_NO_MORE_FILES; both mean 'nothing
            // here', and the original code already had to accept the pair.
            const int err = CDynRapi::CeGetLastError();
            return err == ERROR_NO_MORE_FILES || err == ERROR_FILE_NOT_FOUND;
        }

        for (;;)
        {
            wmobile::DeviceEntry entry;
            entry.name = data.cFileName; // CE is Unicode-native; no conversion, no refusal
            entry.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            entry.sizeLow = data.nFileSizeLow;
            entry.attributes = data.dwFileAttributes; // the device's real attributes, not a synthetic pair
            out.push_back(entry);

            if (!CDynRapi::CeFindNextFile(find, &data))
            {
                const int err = CDynRapi::CeGetLastError();
                CDynRapi::CeFindClose(find);
                return err == ERROR_NO_MORE_FILES;
            }
        }
    }
};

BOOL CRAPI::FindAllFilesInTreeWide(const wchar_t* rootPath, const wchar_t* fileName,
                                   CFileInfoArray& array, int block, BOOL dirFirst)
{
    if (rootPath == NULL || fileName == NULL)
        return FALSE;

    wmobile::WalkOptions options;
    options.directoriesFirst = dirFirst ? true : false;
    CRapiDeviceEnumerator device;
    std::vector<wmobile::FoundItem> found;
    if (!wmobile::WalkDeviceTree(device, rootPath, fileName, options, found))
    {
        const DWORD err = GetLastError();
        SalamanderGeneral->ShowMessageBox(SPLGetErrorTextOwned(SalamanderGeneral, err).c_str(),
                                          TitleWMobileError, MSGBOX_ERROR);
        return FALSE;
    }

    for (const wmobile::FoundItem& item : found)
    {
        // A directory recorded BEFORE its contents is the 'create it first' marker the copy
        // path looks for; the post-order copy of that same directory carries the caller's
        // block, which is what drives source-directory removal on a Move
        // (fs_operations.cpp's `fi.block != -1` arm). Pass the device's REAL attributes:
        // that arm also clears read-only/hidden/system before removing, and the delete
        // confirmations key off them, so a synthetic DIRECTORY/NORMAL pair disabled both.
        if (!array.AddOwned(item.relativePath.c_str(), item.attributes,
                            item.sizeLow, item.isPreOrderMarker ? -1 : block))
        {
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERR_MEMORYLOW).c_str(),
                                              TitleWMobileError, MSGBOX_ERROR);
            TRACE_E("Low memory");
            return FALSE;
        }
    }
    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
//Implementation

DWORD
CRAPI::WaitAndDispatch(DWORD nCount, HANDLE* phWait, DWORD dwTimeout, BOOL bOnlySendMessage)
{
    DWORD dwObj;
    DWORD dwStart = GetTickCount();
    DWORD dwTimeLeft = dwTimeout;

    for (;;)
    {
        dwObj = MsgWaitForMultipleObjects(nCount, phWait, FALSE, dwTimeLeft,
                                          (bOnlySendMessage) ? QS_SENDMESSAGE : QS_ALLINPUT);

        if (dwObj == (DWORD)-1)
        {
            dwObj = WaitForMultipleObjects(nCount, phWait, FALSE, 100);

            if (dwObj == (DWORD)-1)
                break;
        }
        else
        {
            if (dwObj == WAIT_TIMEOUT)
                break;
        }

        if ((UINT)(dwObj - WAIT_OBJECT_0) < nCount)
            break;

        MSG msg;

        if (bOnlySendMessage)
            PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);
        else
        {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                DispatchMessageW(&msg);
        }

        if (INFINITE != dwTimeout)
        {
            dwTimeLeft = dwTimeout - (GetTickCount() - dwStart);
            if ((int)dwTimeLeft < 0)
                break;
        }
    }

    return dwObj;
}

HRESULT
CRAPI::InitRapi(HANDLE hExit, DWORD dwTimeout)
{
    RapiNS::RAPIINIT rapiinit = {sizeof(rapiinit)};
    HRESULT hResult = CDynRapi::CeRapiInitEx(&rapiinit);

    if (FAILED(hResult))
        return hResult;

    HANDLE hWait[2] = {hExit, rapiinit.heRapiInit};
    enum
    {
        WAIT_EXIT = WAIT_OBJECT_0,
        WAIT_INIT
    };

    DWORD dwObj = WaitAndDispatch(2, hWait, dwTimeout, TRUE);

    // Event signaled by RAPI
    if (WAIT_INIT == dwObj)
    {
        // If the connection failed, uninitialize the
        // Windows CE RAPI.
        if (FAILED(rapiinit.hrRapiInit))
            CDynRapi::CeRapiUninit();

        return rapiinit.hrRapiInit;
    }

    // Either event signaled by user or a time-out occurred.
    CDynRapi::CeRapiUninit();

    if (WAIT_EXIT == dwObj)
        return HRESULT_FROM_WIN32(ERROR_CANCELLED);

    return E_FAIL;
}

// The three copy functions below are wide. They had only four call sites, all in
// the two copy loops being converted with them, so they were widened outright rather than given
// siblings - there is no narrow caller left to keep a wrapper for.
//
// READ THESE CAREFULLY: each one straddles the phone and the PC, and in the narrow originals the
// only thing distinguishing the two was a leading "::". An unqualified GetFileAttributes/
// CreateFile/DeleteFile inside CRAPI:: is the DEVICE (a CRAPI static); a "::"-qualified one is the
// PC (Win32). Getting that backwards would compile and would silently operate on the wrong
// machine. The wide forms make it visible: device calls now end in "Wide", PC calls in "W".
//
//   CopyFileToPC   source = device, target = PC
//   CopyFileToCE   source = PC,     target = device
//   CopyFile       both = device
//
// The three are deliberately NOT unified. They differ in more than which side is which - notably
// CopyFileToCE reads its source attributes through the Salamander SDK rather than Win32, and both
// it and CopyFile set the target attributes twice while CopyFileToPC sets them once. Folding
// those into one parameterised function would mean deciding which asymmetries are bugs, which is
// a behaviour change wearing a widening's clothes.
DWORD
CRAPI::CopyFileToPC(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName, BOOL bFailIfExists, CProgressDlg* dlg, INT64 totalCopied, INT64 totalSize, const wchar_t** errorFileName)
{
    DWORD err = 0;

    HANDLE srcHandle = INVALID_HANDLE_VALUE, dstHandle = INVALID_HANDLE_VALUE;
    FILETIME creationTime, accessedTime, writeTime;
    DWORD size, copied = 0;

    DWORD attr = GetFileAttributesWide(lpExistingFileName); // device
    if (attr == 0xFFFFFFFF)
        goto ONERROR_SRC;

    srcHandle = CreateFileWide(lpExistingFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); // device
    if (srcHandle == INVALID_HANDLE_VALUE)
        goto ONERROR_SRC;

    size = GetFileSize(srcHandle, NULL); // JR REVIEW: Files larger than 4 GB likely won't exist on Windows Mobile
    if (size == 0xFFFFFFFF)
        goto ONERROR_SRC;

    if (totalSize < totalCopied + size)
        totalSize = totalCopied + size;

    if (!GetFileTime(srcHandle, &creationTime, &accessedTime, &writeTime))
        goto ONERROR_SRC;

    dstHandle = ::CreateFileW(lpNewFileName, GENERIC_WRITE, 0, NULL, bFailIfExists ? CREATE_NEW : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); // PC
    if (dstHandle == INVALID_HANDLE_VALUE)
        goto ONERROR_DST;

    DWORD read, written;

    char buffer[COPY_BUFFER_SIZE];

    do
    {
        if (!CDynRapi::CeReadFile(srcHandle, buffer, sizeof(buffer), &read, NULL))
            goto ONERROR_SRC;

        if (!::WriteFile(dstHandle, &buffer, read, &written, NULL))
            goto ONERROR_DST;

        copied += read;
        totalCopied += read;

        if (dlg != NULL)
        {
            float progress = (size ? ((float)copied / (float)size) : 1) * 1000;
            float progressTotal = (totalSize ? ((float)totalCopied / (float)totalSize) : 1) * 1000;
            dlg->SetProgress((DWORD)progressTotal, (DWORD)progress, TRUE);

            if (dlg->GetWantCancel())
            {
                err = -1;
                ::CloseHandle(dstHandle);
                dstHandle = INVALID_HANDLE_VALUE;
                ::DeleteFileW(lpNewFileName); // PC
                goto RETURN;
            }
        }
    } while (read >= sizeof(buffer));

    ::SetFileTime(dstHandle, &creationTime, &accessedTime, &writeTime); // JR REVIEW: should we ignore potential errors?
    ::SetFileAttributesW(lpNewFileName, attr); // PC

RETURN:
    if (srcHandle != INVALID_HANDLE_VALUE)
        CloseHandle(srcHandle);
    if (dstHandle != INVALID_HANDLE_VALUE)
        ::CloseHandle(dstHandle);

    return err;

ONERROR_SRC:
    err = GetLastError();
    if (!err)
        err = E_FAIL; // just in case
    if (errorFileName)
        *errorFileName = lpExistingFileName;
    goto RETURN;

ONERROR_DST:
    err = ::GetLastError();
    if (!err)
        err = E_FAIL; // just in case
    if (errorFileName)
        *errorFileName = lpNewFileName;
    goto RETURN;
}

DWORD
CRAPI::CopyFileToCE(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName, BOOL bFailIfExists, CProgressDlg* dlg, INT64 totalCopied, INT64 totalSize, const wchar_t** errorFileName)
{
    DWORD err = 0;

    HANDLE dstHandle = INVALID_HANDLE_VALUE, srcHandle = INVALID_HANDLE_VALUE;
    FILETIME creationTime, accessedTime, writeTime;
    DWORD size, copied = 0;

    DWORD attr = SalamanderGeneral->SalGetFileAttributes(lpExistingFileName); // PC, via the SDK
    if (attr == 0xFFFFFFFF)
        goto ONERROR_SRC;

    srcHandle = ::CreateFileW(lpExistingFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); // PC
    if (srcHandle == INVALID_HANDLE_VALUE)
        goto ONERROR_SRC;

    size = ::GetFileSize(srcHandle, NULL); // JR REVIEW: Files larger than 4 GB likely won't exist on Windows Mobile
    if (size == 0xFFFFFFFF)
        goto ONERROR_SRC;

    if (totalSize < totalCopied + size)
        totalSize = totalCopied + size;

    if (!::GetFileTime(srcHandle, &creationTime, &accessedTime, &writeTime))
        goto ONERROR_SRC;

    dstHandle = CreateFileWide(lpNewFileName, GENERIC_WRITE, 0, NULL, bFailIfExists ? CREATE_NEW : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); // device
    if (dstHandle == INVALID_HANDLE_VALUE)
        goto ONERROR_DST;

    DWORD read, written;

    char buffer[COPY_BUFFER_SIZE];

    do
    {
        if (!::ReadFile(srcHandle, buffer, sizeof(buffer), &read, NULL))
            goto ONERROR_SRC;

        if (!WriteFile(dstHandle, &buffer, read, &written, NULL))
            goto ONERROR_DST;

        copied += read;
        totalCopied += read;

        if (dlg != NULL)
        {
            float progress = (size ? ((float)copied / (float)size) : 1) * 1000;
            float progressTotal = (totalSize ? ((float)totalCopied / (float)totalSize) : 1) * 1000;
            dlg->SetProgress((DWORD)progressTotal, (DWORD)progress, TRUE);

            if (dlg->GetWantCancel())
            {
                err = -1;
                CloseHandle(dstHandle);
                dstHandle = INVALID_HANDLE_VALUE;
                DeleteFileWide(lpNewFileName); // device
                goto RETURN;
            }
        }
    } while (read >= sizeof(buffer));

    SetFileTime(dstHandle, &creationTime, &accessedTime, &writeTime); // JR REVIEW: should we ignore potential errors?
    SetFileAttributesWide(lpNewFileName, attr); // device

RETURN:
    if (dstHandle != INVALID_HANDLE_VALUE)
        CloseHandle(dstHandle);
    if (srcHandle != INVALID_HANDLE_VALUE)
        ::CloseHandle(srcHandle);

    if (err == 0)
        SetFileAttributesWide(lpNewFileName, attr); // device

    return err;

ONERROR_SRC:
    err = ::GetLastError();
    if (!err)
        err = E_FAIL; // just in case
    if (errorFileName)
        *errorFileName = lpExistingFileName;
    goto RETURN;

ONERROR_DST:
    err = GetLastError();
    if (!err)
        err = E_FAIL; // just in case
    if (errorFileName)
        *errorFileName = lpNewFileName;
    goto RETURN;
}

DWORD
CRAPI::CopyFileWide(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName, BOOL bFailIfExists, CProgressDlg* dlg, INT64 totalCopied, INT64 totalSize, const wchar_t** errorFileName)
{
    DWORD err = 0;

    HANDLE dstHandle = INVALID_HANDLE_VALUE, srcHandle = INVALID_HANDLE_VALUE;
    FILETIME creationTime, accessedTime, writeTime;
    DWORD size, copied = 0;

    DWORD attr = GetFileAttributesWide(lpExistingFileName); // device
    if (attr == 0xFFFFFFFF)
        goto ONERROR_SRC;

    srcHandle = CreateFileWide(lpExistingFileName, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); // device
    if (srcHandle == INVALID_HANDLE_VALUE)
        goto ONERROR_SRC;

    size = GetFileSize(srcHandle, NULL); // JR REVIEW: Files larger than 4 GB likely won't exist on Windows Mobile
    if (size == 0xFFFFFFFF)
        goto ONERROR_SRC;

    if (totalSize < totalCopied + size)
        totalSize = totalCopied + size;

    if (!GetFileTime(srcHandle, &creationTime, &accessedTime, &writeTime))
        goto ONERROR_SRC;

    dstHandle = CreateFileWide(lpNewFileName, GENERIC_WRITE, 0, NULL, bFailIfExists ? CREATE_NEW : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); // device
    if (dstHandle == INVALID_HANDLE_VALUE)
        goto ONERROR_DST;

    DWORD read, written;

    char buffer[COPY_BUFFER_SIZE];

    do
    {
        if (!ReadFile(srcHandle, buffer, sizeof(buffer), &read, NULL))
            goto ONERROR_SRC;

        if (!WriteFile(dstHandle, &buffer, read, &written, NULL))
            goto ONERROR_DST;

        copied += read;
        totalCopied += read;

        if (dlg != NULL)
        {
            float progress = (size ? ((float)copied / (float)size) : 1) * 1000;
            float progressTotal = (totalSize ? ((float)totalCopied / (float)totalSize) : 1) * 1000;
            dlg->SetProgress((DWORD)progressTotal, (DWORD)progress, TRUE);

            if (dlg->GetWantCancel())
            {
                err = -1;
                CloseHandle(dstHandle);
                dstHandle = INVALID_HANDLE_VALUE;
                DeleteFileWide(lpNewFileName); // device
                goto RETURN;
            }
        }
    } while (read >= sizeof(buffer));

    SetFileTime(dstHandle, &creationTime, &accessedTime, &writeTime); // JR REVIEW: should we ignore potential errors?
    SetFileAttributesWide(lpNewFileName, attr); // device

RETURN:
    if (dstHandle != INVALID_HANDLE_VALUE)
        CloseHandle(dstHandle);
    if (srcHandle != INVALID_HANDLE_VALUE)
        CloseHandle(srcHandle);

    if (err == 0)
        SetFileAttributesWide(lpNewFileName, attr); // device

    return err;

ONERROR_SRC:
    err = GetLastError();
    if (!err)
        err = E_FAIL; // just in case
    if (errorFileName)
        *errorFileName = lpExistingFileName;
    goto RETURN;

ONERROR_DST:
    err = GetLastError();
    if (!err)
        err = E_FAIL; // just in case
    if (errorFileName)
        *errorFileName = lpNewFileName;
    goto RETURN;
}

DWORD CRAPI::SetFileTimeWide(const wchar_t* fileName, const SYSTEMTIME* creationTime,
                             const SYSTEMTIME* lastAccessTime, const SYSTEMTIME* lastWriteTime)
{
    if (creationTime == NULL && lastAccessTime == NULL && lastWriteTime == NULL)
        return 0;

    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD attr = GetFileAttributesWide(fileName);
    if (attr == 0xFFFFFFFF)
        goto ONERROR_CE_WIDE;
    if ((attr & FILE_ATTRIBUTE_READONLY) &&
        !SetFileAttributesWide(fileName, attr & ~FILE_ATTRIBUTE_READONLY))
        goto ONERROR_CE_WIDE;

    handle = CreateFileWide(fileName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        goto ONERROR_CE_WIDE;

    FILETIME time, created, accessed, modified;
    if (creationTime)
    {
        SystemTimeToFileTime(creationTime, &time);
        LocalFileTimeToFileTime(&time, &created);
    }
    if (lastAccessTime)
    {
        SystemTimeToFileTime(lastAccessTime, &time);
        LocalFileTimeToFileTime(&time, &accessed);
    }
    if (lastWriteTime)
    {
        SystemTimeToFileTime(lastWriteTime, &time);
        LocalFileTimeToFileTime(&time, &modified);
    }
    if (!SetFileTime(handle, creationTime ? &created : NULL,
                     lastAccessTime ? &accessed : NULL, lastWriteTime ? &modified : NULL))
        goto ONERROR_CE_WIDE;

    CloseHandle(handle);
    if (attr & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesWide(fileName, attr);
    return 0;

ONERROR_CE_WIDE:
    DWORD err = GetLastError();
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    if (attr != 0xFFFFFFFF && (attr & FILE_ATTRIBUTE_READONLY))
        SetFileAttributesWide(fileName, attr);
    return err;
}

BOOL CRAPI::CheckAndCreateDirectory(const wchar_t* dir, HWND parent, BOOL quiet)
{
    CALL_STACK_MESSAGE2("CheckAndCreateDirectory(%ls)", dir);

    DWORD attrs = GetFileAttributesWide(dir);
    std::wstring name;
    const std::wstring root = L"\\";
    const auto reportError = [&](const wchar_t* format, const wchar_t* subject)
    {
        const std::wstring message = SPLFormatStringOwned(format, subject);
        SalamanderGeneral->SalMessageBox(parent, message.c_str(), TitleWMobileError,
                                         MB_OK | MB_ICONEXCLAMATION);
    };

    if (attrs == 0xFFFFFFFF) // probably does not exist; allow creation
    {
        if (wcslen(dir) <= root.size()) // dir is the root directory
        {
            reportError(LangStr(IDS_ERR_CREATEDIR).c_str(), dir);
            return FALSE;
        }
        const std::wstring question = SPLFormatStringOwned(LangStr(IDS_YESNO_CREATEDIR).c_str(), dir);
        if (quiet || SalamanderGeneral->SalMessageBox(parent, question.c_str(), TitleWMobileQuestion,
                                                       MB_YESNOCANCEL | MB_ICONQUESTION) == IDYES)
        {
            name = dir;
            while (1) // find the first existing directory
            {
                const size_t slash = name.find_last_of(L'\\');
                if (slash == std::wstring::npos)
                {
                    reportError(LangStr(IDS_ERR_CREATEDIR).c_str(), dir);
                    return FALSE;
                }
                if (slash > root.size())
                    name.resize(slash);
                else
                {
                    name = root;
                    break; // already at the root directory
                }
                attrs = GetFileAttributesWide(name.c_str());
                if (attrs != 0xFFFFFFFF) // the name exists
                {
                    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
                        break; // we will build from this directory
                    else       // it is a file, that would not work...
                    {
                        reportError(LangStr(IDS_ERR_DIRNAMEISFILE).c_str(), name.c_str());
                        return FALSE;
                    }
                }
            }
            if (name.back() != L'\\')
                name.push_back(L'\\');
            size_t start = name.size();
            const std::wstring requested(dir);
            if (start < requested.size() && requested[start] == L'\\')
                ++start;
            while (start < requested.size())
            {
                size_t slash = requested.find(L'\\', start);
                if (slash == std::wstring::npos)
                    slash = requested.size();
                name.append(requested, start, slash - start);
                if (!CreateDirectoryWide(name.c_str(), NULL)) // device
                {
                    reportError(LangStr(IDS_ERR_CREATEDIR).c_str(), name.c_str());
                    return FALSE;
                }
                if (slash == requested.size())
                    break;
                name.push_back(L'\\');
                start = slash + 1;
            }
            return TRUE;
        }
        return FALSE;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        return TRUE;
    else // file, that would not work...
    {
        reportError(LangStr(IDS_ERR_DIRNAMEISFILE).c_str(), dir);
        return FALSE;
    }
}

std::wstring CRAPI::GetFileDataWide(const wchar_t* name)
{
    RapiNS::CE_FIND_DATA data;

    HANDLE find = FindFirstFileWide(name, &data);
    if (find == INVALID_HANDLE_VALUE)
        return L"?";

    CQuadWord size(data.nFileSizeLow, data.nFileSizeHigh);
    std::wstring result = SPLNumberToStrOwned(SalamanderGeneral, size);

    FILETIME time;
    SYSTEMTIME st;
    FileTimeToLocalFileTime(&data.ftLastWriteTime, &time);
    FileTimeToSystemTime(&time, &st);

    int dateLength = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, NULL, 0);
    std::wstring date;
    if (dateLength > 0)
    {
        std::vector<wchar_t> value(static_cast<size_t>(dateLength));
        if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, value.data(), dateLength))
            date.assign(value.data());
    }
    if (date.empty())
        date = SPLFormatStringOwned(L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);

    int timeLength = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, NULL, 0);
    std::wstring timeText;
    if (timeLength > 0)
    {
        std::vector<wchar_t> value(static_cast<size_t>(timeLength));
        if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, value.data(), timeLength))
            timeText.assign(value.data());
    }
    if (timeText.empty())
        timeText = SPLFormatStringOwned(L"%u:%u:%u", st.wHour, st.wMinute, st.wSecond);

    result += L", " + date + L", " + timeText;

    FindClose(find);
    return result;
}

BOOL CRAPI::CheckConnection()
{
    RapiNS::CEOSVERSIONINFO vi;
    ZeroMemory(&vi, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);

    if (CDynRapi::CeGetVersionEx(&vi))
        return TRUE;

    return FALSE;
}
