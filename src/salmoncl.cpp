// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "salmoncl.h"
#include "salmon_registry_mutex_policy.h"
#include "common/Win32EarlyStartupRegistry.h"
#include "common/unicode/helpers.h"

CSalmonSharedMemory* SalmonSharedMemory = NULL;
HANDLE SalmonFileMapping = NULL;
HANDLE HSalmonProcess = NULL;

//****************************************************************************

// WARNING: we are running from entry point, before RTL initialization, global objects, etc.
// do not call TRACE, HANDLES, RTL, ...

HANDLE GetBugReporterRegistryMutex()
{
    // permissions fully open for all processes
    SECURITY_ATTRIBUTES secAttr;
    BYTE secDesc[SECURITY_DESCRIPTOR_MIN_LENGTH];
    secAttr.nLength = sizeof(secAttr);
    secAttr.bInheritHandle = FALSE;
    secAttr.lpSecurityDescriptor = &secDesc;
    InitializeSecurityDescriptor(secAttr.lpSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(secAttr.lpSecurityDescriptor, TRUE, 0, FALSE);
    // it would be convenient to add SID to the mutex name, because processes with different SID run with a different HKCU tree
    // but for simplicity we skip that and the mutex will be truly global
    const wchar_t* MUTEX_NAME = sally::salmon::BugReporterRegistryMutexName();
    HANDLE hMutex = NOHANDLES(CreateMutexW(&secAttr, FALSE, MUTEX_NAME));
    if (hMutex == NULL) // create can already open an existing mutex, but it can fail, so we try open afterwards
        hMutex = NOHANDLES(OpenMutexW(SYNCHRONIZE, FALSE, MUTEX_NAME));
    return hMutex;
}

BOOL SalmonGetBugReportUID(DWORD64* uid)
{
    const wchar_t* BUG_REPORTER_KEY = SAL_REG_KEY_BUG_REPORTER_W;
    const wchar_t* BUG_REPORTER_UID = SAL_REG_VALUE_BUG_REPORTER_UID_W;

    // this section runs at Salamander startup and theoretically concurrent registry read/write can occur
    // therefore we will guard access with a global mutex
    HANDLE hMutex = GetBugReporterRegistryMutex();
    if (hMutex != NULL)
        WaitForSingleObject(hMutex, INFINITE);
    *uid = 0;
    HKEY hKey;
    LONG res = NOHANDLES(RegOpenKeyExW(HKEY_CURRENT_USER, BUG_REPORTER_KEY, 0, KEY_READ, &hKey));
    if (res == ERROR_SUCCESS)
    {
        // try to load the old value if it exists
        DWORD gettedType;
        DWORD bufferSize = sizeof(*uid);
        res = RegQueryValueExW(hKey, BUG_REPORTER_UID, 0, &gettedType, (BYTE*)uid, &bufferSize);
        if (res != ERROR_SUCCESS || gettedType != REG_QWORD)
            *uid = 0;
        NOHANDLES(RegCloseKey(hKey));
    }
    // if UID does not exist yet, we create and save it
    if (*uid == 0)
    {
        GUID guid;
        if (CoCreateGuid(&guid) == S_OK)
        {
            // we won't store and send the entire GUID, half of it XORed with the other half is enough
            DWORD64* dw64 = (DWORD64*)&guid;
            *uid = dw64[0] ^ dw64[1];

            // try to save it
            DWORD createType;
            LONG res2 = NOHANDLES(RegCreateKeyExW(HKEY_CURRENT_USER, BUG_REPORTER_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                                                  KEY_READ | KEY_WRITE, NULL, &hKey, &createType));
            if (res2 == ERROR_SUCCESS)
            {
                res2 = RegSetValueExW(hKey, BUG_REPORTER_UID, 0, REG_QWORD, (BYTE*)uid, sizeof(*uid));
                if (res2 != ERROR_SUCCESS)
                    *uid = 0; // on failure we want zero
                NOHANDLES(RegCloseKey(hKey));
            }
        }
    }
    if (hMutex != NULL)
    {
        ReleaseMutex(hMutex);
        NOHANDLES(CloseHandle(hMutex));
    }

    return TRUE;
}

BOOL SalmonSharedMemInit(CSalmonSharedMemory* mem)
{
    SECURITY_ATTRIBUTES sa; // allow handle inheritance to child process (they can then work directly with our event)
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    RtlFillMemory(mem, sizeof(CSalmonSharedMemory), 0);

    mem->Version = SALMON_SHARED_MEMORY_VERSION;
    // salmon will be started as a child process with bInheritHandles==TRUE, so it can access these handles directly
    mem->ProcessId = GetCurrentProcessId();
    mem->Process = NOHANDLES(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, TRUE, mem->ProcessId));
    mem->Fire = NOHANDLES(CreateEvent(&sa, TRUE, FALSE, NULL));      // "nonsignaled" state, manual
    mem->Done = NOHANDLES(CreateEvent(&sa, TRUE, FALSE, NULL));      // "nonsignaled" state, manual
    mem->SetSLG = NOHANDLES(CreateEvent(&sa, TRUE, FALSE, NULL));    // "nonsignaled" state, manual
    mem->CheckBugs = NOHANDLES(CreateEvent(&sa, TRUE, FALSE, NULL)); // "nonsignaled" state, manual

    // we now put the path for bug reports into LOCAL_APPDATA, where Windows WER stores minidumps by default
    // we don't create the path immediately, we take care of that at the moment of the crash
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, mem->BugPath) == S_OK)
    {
        int len = lstrlenW(mem->BugPath);
        if (len > 0 && mem->BugPath[len - 1] == '\\') // better verify the trailing backslash at the end of the path
            mem->BugPath[len - 1] = 0;
        wcscat_s(mem->BugPath, L"\\Open Salamander");
    }

    // base name for bug reports
    wcscpy_s(mem->BugName, L"AS" VERSINFO_SAL_SHORT_VERSION);

    return (mem->Process != NULL && mem->Fire != NULL && mem->Done != NULL && mem->SetSLG != NULL &&
            mem->CheckBugs != NULL && mem->BugPath[0] != 0);
}

std::wstring GetStartupSLGName()
{
    // extract from registry the SLG name that will probably be used
    // later during Salamander runtime a different one may be selected, which will be changed afterwards
    // this serves only as a default; if the record is not found, we pass an empty string
    std::wstring slgName;
    const std::wstring keyName = std::wstring(SalamanderConfigurationRoots[0]) + L"\\" + SALAMANDER_CONFIG_REG;
    HKEY hKey;
    LONG res = NOHANDLES(RegOpenKeyExW(HKEY_CURRENT_USER, keyName.c_str(), 0, KEY_READ, &hKey));
    if (res == ERROR_SUCCESS)
    {
        DWORD valueType = 0;
        DWORD bufferSize = 0;
        res = sally::registry::QueryValueForEarlyStartupW(hKey, CONFIG_LANGUAGE_REG,
                                                          &valueType, NULL, &bufferSize);
        if ((res == ERROR_SUCCESS || res == ERROR_MORE_DATA) && valueType == REG_SZ && bufferSize > 0)
        {
            std::vector<wchar_t> buffer(bufferSize / sizeof(wchar_t) + 1, L'\0');
            res = sally::registry::QueryValueForEarlyStartupW(hKey, CONFIG_LANGUAGE_REG,
                                                              &valueType, reinterpret_cast<BYTE*>(buffer.data()), &bufferSize);
            if (res == ERROR_SUCCESS && valueType == REG_SZ)
                slgName.assign(buffer.data());
        }
        RegCloseKey(hKey);
    }
    return slgName;
}

static BOOL GetModuleFileNameOwnedW(HMODULE module, std::wstring& path)
{
    DWORD capacity = 256;
    while (capacity <= MAXDWORD / 2)
    {
        std::vector<wchar_t> buffer(capacity, L'\0');
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(module, buffer.data(), capacity);
        if (length == 0)
            return FALSE;
        if (length < capacity - 1 || length == capacity - 1 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            path.assign(buffer.data(), length);
            return TRUE;
        }
        capacity *= 2;
    }
    return FALSE;
}

BOOL SalmonStartProcess(const wchar_t* fileMappingName)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HSalmonProcess = NULL;

    // issue #63: wide install paths so non-ASCII directories (Cyrillic, etc.)
    // don't get mangled by ANSI GetModuleFileName -> CreateProcess.
    std::wstring exePath;
    if (!GetModuleFileNameOwnedW(NULL, exePath))
        return FALSE;
    size_t slash = exePath.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return FALSE;
    std::wstring rtlDirW(exePath, 0, slash + 1);
    std::wstring salmonExeW = rtlDirW + L"utils\\salmon.exe";

    const std::wstring slgName = GetStartupSLGName();

    std::wstring cmdW;
    cmdW.reserve(salmonExeW.size() + wcslen(fileMappingName) + 64);
    cmdW.append(L"\"").append(salmonExeW).append(L"\"");
    cmdW.append(L" \"").append(fileMappingName).append(L"\"");
    cmdW.append(L" \"").append(slgName).append(L"\"");

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.wShowWindow = SW_SHOWNORMAL;

    std::wstring oldCurDirW;
    const DWORD currentDirectorySize = GetCurrentDirectoryW(0, NULL);
    if (currentDirectorySize > 0)
    {
        std::vector<wchar_t> currentDirectory(currentDirectorySize, L'\0');
        const DWORD length = GetCurrentDirectoryW(currentDirectorySize, currentDirectory.data());
        if (length > 0 && length < currentDirectorySize)
            oldCurDirW.assign(currentDirectory.data(), length);
    }

    // another attempt to solve the problem before we split SALMON.EXE into EXE+DLL:
    // extend PATH for the child so it can see the install-dir-local RTL.
    std::wstring envPATHW;
    const DWORD envSize = GetEnvironmentVariableW(L"PATH", NULL, 0);
    const BOOL hadEnvironmentPath = envSize > 0;
    if (hadEnvironmentPath)
    {
        std::vector<wchar_t> environmentPath(envSize, L'\0');
        const DWORD envLen = GetEnvironmentVariableW(L"PATH", environmentPath.data(), envSize);
        if (envLen > 0 && envLen < envSize)
            envPATHW.assign(environmentPath.data(), envLen);
    }
    BOOL envExtended = FALSE;
    if (!envPATHW.empty())
    {
        std::wstring newPATH = envPATHW;
        newPATH.append(L";").append(rtlDirW);
        envExtended = SetEnvironmentVariableW(L"PATH", newPATH.c_str());
    }

    SetCurrentDirectoryW(rtlDirW.c_str());

    // CreateProcessW takes a writable command-line buffer.
    std::vector<wchar_t> cmdBuf(cmdW.begin(), cmdW.end());
    cmdBuf.push_back(L'\0');

    BOOL ret = FALSE;
    if (NOHANDLES(CreateProcessW(NULL, cmdBuf.data(), NULL, NULL, TRUE,
                                 CREATE_DEFAULT_ERROR_MODE | HIGH_PRIORITY_CLASS, NULL,
                                 rtlDirW.c_str(), &si, &pi)))
    {
        HSalmonProcess = pi.hProcess;
        AllowSetForegroundWindow(SalGetProcessId(pi.hProcess));
        ret = TRUE;
    }

    if (!oldCurDirW.empty())
        SetCurrentDirectoryW(oldCurDirW.c_str());
    if (envExtended)
        SetEnvironmentVariableW(L"PATH", hadEnvironmentPath ? envPATHW.c_str() : NULL);
    return ret;
}

//BOOL IsSalmonRunning()
//{
//  if (HSalmonProcess != NULL)
//  {
//    DWORD waitRet = WaitForSingleObject(HSalmonProcess, 0);
//    return waitRet == WAIT_TIMEOUT;
//  }
//  return FALSE;
//}

// We want to learn about SEH Exceptions also on x64 Windows 7 SP1 and later
// http://blog.paulbetts.org/index.php/2010/07/20/the-case-of-the-disappearing-onload-exception-user-mode-callback-exceptions-in-x64/
// http://connect.microsoft.com/VisualStudio/feedback/details/550944/hardware-exceptions-on-x64-machines-are-silently-caught-in-wndproc-messages
// http://support.microsoft.com/kb/976038
void EnableExceptionsOn64()
{
    typedef BOOL(WINAPI * FSetProcessUserModeExceptionPolicy)(DWORD dwFlags);
    typedef BOOL(WINAPI * FGetProcessUserModeExceptionPolicy)(LPDWORD dwFlags);
    typedef BOOL(WINAPI * FIsWow64Process)(HANDLE, PBOOL);
#define PROCESS_CALLBACK_FILTER_ENABLED 0x1

    HINSTANCE hDLL = LoadLibraryW(L"KERNEL32.DLL");
    if (hDLL != NULL)
    {
        FIsWow64Process isWow64 = (FIsWow64Process)GetProcAddress(hDLL, "IsWow64Process");                                                      // Min: XP SP2
        FSetProcessUserModeExceptionPolicy set = (FSetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "SetProcessUserModeExceptionPolicy"); // Min: Vista with hotfix
        FGetProcessUserModeExceptionPolicy get = (FGetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "GetProcessUserModeExceptionPolicy"); // Min: Vista with hotfix
        if (isWow64 != NULL && set != NULL && get != NULL)
        {
            BOOL bIsWow64;
            if (isWow64(GetCurrentProcess(), &bIsWow64) && bIsWow64)
            {
                DWORD dwFlags;
                if (get(&dwFlags))
                    set(dwFlags & ~PROCESS_CALLBACK_FILTER_ENABLED);
            }
        }
        FreeLibrary(hDLL);
    }
}

BOOL SalmonInit()
{
    EnableExceptionsOn64();

    SalmonSharedMemory = NULL;
    std::wstring salmonFileMappingName;
    // allocation of shared space in pagefile.sys
    DWORD ti = (GetTickCount() >> 3) & 0xFFF;
    while (TRUE) // looking for a unique name for file-mapping
    {
        salmonFileMappingName = FormatStrW(L"Salmon%X", ti++);
        SalmonFileMapping = NOHANDLES(CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, // FIXME_X64 aren't we passing x86/x64 incompatible data?
                                                         sizeof(CSalmonSharedMemory), salmonFileMappingName.c_str()));
        if (SalmonFileMapping == NULL || GetLastError() != ERROR_ALREADY_EXISTS)
            break;
        NOHANDLES(CloseHandle(SalmonFileMapping));
    }
    if (SalmonFileMapping != NULL)
    {
        SalmonSharedMemory = (CSalmonSharedMemory*)NOHANDLES(MapViewOfFile(SalmonFileMapping, FILE_MAP_WRITE, 0, 0, 0)); // FIXME_X64 aren't we passing x86/x64 incompatible data?
        if (SalmonSharedMemory != NULL)
        {
            ZeroMemory(SalmonSharedMemory, sizeof(CSalmonSharedMemory));
            if (SalmonSharedMemInit(SalmonSharedMemory))
            {
                SalmonGetBugReportUID(&SalmonSharedMemory->UID);

                // if salmon fails to start, we still return TRUE - problem will be reported later after SLG is loaded
                SalmonStartProcess(salmonFileMappingName.c_str());
                return TRUE;
            }
        }
    }
    // a serious (and unexpected) error occurred, we block Salamander startup, message will be in English (we don't have slg)
    return FALSE;
}

// info that salmon is not running needs to be displayed only once
static BOOL SalmonNotRunningReported = FALSE;

void SalmonSetSLG(const wchar_t* slgName)
{
    ResetEvent(SalmonSharedMemory->Done);

    wcscpy_s(SalmonSharedMemory->SLGName, slgName);
    SetEvent(SalmonSharedMemory->SetSLG);

    // wait for signal from Salmon that it processed the task (event Done) or for the case when someone killed Salmon
    HANDLE arr[2];
    arr[0] = HSalmonProcess;
    arr[1] = SalmonSharedMemory->Done;
    DWORD waitRet = WaitForMultipleObjects(2, arr, FALSE, INFINITE);
    if (waitRet != WAIT_OBJECT_0 + 1) // someone killed salmon or something went wrong in communication
    {
        if (!SalmonNotRunningReported && HLanguage != NULL)
        {
#ifdef _DEBUG
            TRACE_E("Salmon is not running (debug build, suppressing dialog)");
#else
            // wide: this fires whenever a .slg language pack is loaded
            // (HLanguage != NULL), so IDS_SALMON_NOT_RUNNING's translation can contain
            // characters outside the process ANSI code page - MessageBoxA/LoadStr silently
            // mangled it. SALAMANDER_TEXT_VERSIONW()/LoadStrW/MessageBoxW already proven
            // together elsewhere (sally_entry_lifecycle.cpp).
            MessageBoxW(NULL, LoadStrW(IDS_SALMON_NOT_RUNNING), SALAMANDER_TEXT_VERSIONW(), MB_OK | MB_ICONERROR);
#endif
            SalmonNotRunningReported = TRUE;
        }
    }
    ResetEvent(SalmonSharedMemory->Done);
}

void SalmonCheckBugs()
{
    ResetEvent(SalmonSharedMemory->Done);
    SetEvent(SalmonSharedMemory->CheckBugs);

    // wait for signal from Salmon that it processed the task (event Done) or for the case when someone killed Salmon
    HANDLE arr[2];
    arr[0] = HSalmonProcess;
    arr[1] = SalmonSharedMemory->Done;
    DWORD waitRet = WaitForMultipleObjects(2, arr, FALSE, INFINITE);
    if (waitRet != WAIT_OBJECT_0 + 1) // someone killed salmon or something went wrong in communication
    {
        if (!SalmonNotRunningReported && HLanguage != NULL)
        {
#ifdef _DEBUG
            TRACE_E("Salmon is not running (debug build, suppressing dialog)");
#else
            // wide - see SalmonSetSLG's identical call above.
            MessageBoxW(NULL, LoadStrW(IDS_SALMON_NOT_RUNNING), SALAMANDER_TEXT_VERSIONW(), MB_OK | MB_ICONERROR);
#endif
            SalmonNotRunningReported = TRUE;
        }
    }
    ResetEvent(SalmonSharedMemory->Done);
}

const wchar_t* SalmonFireAndWait(const EXCEPTION_POINTERS* e)
{
    SalmonSharedMemory->ThreadId = GetCurrentThreadId();
    SalmonSharedMemory->ExceptionRecord = *e->ExceptionRecord;
    SalmonSharedMemory->ContextRecord = *e->ContextRecord;
    SetEvent(SalmonSharedMemory->Fire);

    // wait for signal from Salmon that it processed the task (event Done) or for the case when someone killed Salmon
    HANDLE arr[2];
    arr[0] = HSalmonProcess;
    arr[1] = SalmonSharedMemory->Done;
    WaitForMultipleObjects(2, arr, FALSE, INFINITE);
    ResetEvent(SalmonSharedMemory->Done);

    // Packed Salmon IPC v5 fixes these fields at MAX_PATH. Keep its matching output
    // buffer inside this adapter so the capacity does not become core path ownership.
    static wchar_t bugReportPath[MAX_PATH];
    wcscpy_s(bugReportPath, SalmonSharedMemory->BugPath);
    SalPathAppendW(bugReportPath, SalmonSharedMemory->BaseName, _countof(bugReportPath));
    wcscat_s(bugReportPath, L".TXT");

    return bugReportPath;
}
