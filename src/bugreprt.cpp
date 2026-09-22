// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <tlhelp32.h>
#include "common/DiagnosticTextEncoding.h"
#include "common/IEnvironment.h"
#include "common/IRegistry.h"
#include "shiconov_diag.h"
#include "shellsup_diag.h"

// Cross-architecture register access macros for CONTEXT structure
#ifdef _WIN64
#ifdef _M_ARM64
// ARM64 registers
#define CONTEXT_IP(ctx) ((ctx).Pc)
#define CONTEXT_SP(ctx) ((ctx).Sp)
#define CONTEXT_FP(ctx) ((ctx).Fp)
#define CONTEXT_IP_NAME "PC"
#define CONTEXT_SP_NAME "SP"
#define CONTEXT_FP_NAME "FP"
#else
// x64 registers
#define CONTEXT_IP(ctx) ((ctx).Rip)
#define CONTEXT_SP(ctx) ((ctx).Rsp)
#define CONTEXT_FP(ctx) ((ctx).Rbp)
#define CONTEXT_IP_NAME "RIP"
#define CONTEXT_SP_NAME "RSP"
#define CONTEXT_FP_NAME "RBP"
#endif
#endif

#include "mainwnd.h"
#include "drivelst.h"
#include "plugins.h"
#include "stswnd.h"
#include "editwnd.h"
#include "fileswnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "worker.h"
#include "snooper.h"
#include "gui.h"

static std::wstring BugReportReasonBreak; // if not empty, displayed below the "Break" message

void SetBugReportReasonBreak(std::wstring reason)
{
    BugReportReasonBreak = std::move(reason);
}

const wchar_t* GetBugReportReasonBreak()
{
    return BugReportReasonBreak.c_str();
}

TIndirectArray<char> GlobalModulesStore(50, 20);
TDirectArray<DWORD> GlobalModulesListTimeStore(50, 20); // x64_OK

static char* DupAnsiString(const char* text)
{
    if (text == NULL)
        return NULL;

    const size_t length = strlen(text) + 1;
    char* copy = (char*)malloc(length);
    if (copy != NULL)
        memcpy(copy, text, length);
    return copy;
}

static CPluginData* FindPluginDataByDllName(const wchar_t* dllName)
{
    if (dllName == NULL)
        return NULL;

    return Plugins.GetPluginData(dllName);
}

static IRegistry* GetBugReportRegistry()
{
    return gRegistry != nullptr ? gRegistry : GetWin32Registry();
}

// Forward declaration - defined near AddNewlyLoadedModulesToGlobalModulesStore
// further down, used earlier by PrintBugReport. Both callers have their own __try, so the
// wide->narrow conversion lives in this ordinary (no __try) function instead of a local
// std::string in their bodies - see its definition for the full rationale.
static BOOL CopyModuleTextNarrow(char* dest, size_t destSize, const wchar_t* wideSrc) noexcept;

// Registry strings remain dynamically owned UTF-16 until this crash-report byte sink. Some BIOS
// values are REG_MULTI_SZ; the historical report printed only the first element, which is retained.
static BOOL CopyRegistryTextNarrow(IRegistry* registry, HKEY key, const wchar_t* valueName,
                                   char* dest, size_t destSize) noexcept
{
    if (registry == NULL || dest == NULL || destSize == 0)
        return FALSE;
    dest[0] = 0;
    try
    {
        std::wstring value;
        RegistryResult result = registry->GetString(key, valueName, value);
        if (!result.success && result.errorCode == ERROR_INVALID_DATATYPE)
        {
            RegValueType type = RegValueType::None;
            std::vector<uint8_t> data;
            result = registry->GetValue(key, valueName, type, data);
            if (!result.success || type != RegValueType::MultiString ||
                (data.size() % sizeof(wchar_t)) != 0)
            {
                return FALSE;
            }

            value.resize(data.size() / sizeof(wchar_t));
            if (!data.empty())
                memcpy(value.data(), data.data(), data.size());
            const size_t terminator = value.find(L'\0');
            if (terminator != std::wstring::npos)
                value.resize(terminator);
        }
        if (!result.success)
            return FALSE;
        return CopyModuleTextNarrow(dest, destSize, value.c_str());
    }
    catch (...)
    {
        dest[0] = 0;
        return FALSE;
    }
}

static BOOL GetEnvironmentPathReportText(BOOL systemDirectory, char* dest,
                                         size_t destSize) noexcept
{
    if (dest == NULL || destSize == 0 || gEnvironment == NULL)
        return FALSE;
    dest[0] = 0;
    try
    {
        std::wstring path;
        const EnvResult result = systemDirectory ? gEnvironment->GetSystemDirectory(path)
                                                 : gEnvironment->GetWindowsDirectory(path);
        return result.success && CopyModuleTextNarrow(dest, destSize, path.c_str());
    }
    catch (...)
    {
        dest[0] = 0;
        return FALSE;
    }
}

//
// ****************************************************************************
// GetProcessorSpeed
//

BOOL GetProcessorSpeed(DWORD* mhz)
{
#ifdef _M_ARM64
    // ARM64: __rdtsc is not available, processor speed detection not implemented
    *mhz = 0;
    return FALSE;
#else
    __try
    {
        LARGE_INTEGER t1, t2, t3, t4, f;
        unsigned __int64 c1, c2;
        int tp, pp;
        HANDLE t, p;

        if (!QueryPerformanceFrequency(&f))
            return FALSE; // installed hardware doesn't supports a high-resolution performance counter

        // temporarily raise priority
        t = GetCurrentThread();
        tp = GetThreadPriority(t);
        SetThreadPriority(t, THREAD_PRIORITY_TIME_CRITICAL);
        p = GetCurrentProcess();
        pp = GetPriorityClass(p);
        SetPriorityClass(p, REALTIME_PRIORITY_CLASS);

        // get the counters
        c1 = __rdtsc();
        QueryPerformanceCounter(&t1);

        //    Sleep(50);
        __int64 divider = 20;   // 1 / 20 s = 50 ms
        if (f.QuadPart == 1000) // GetTickCount are used, we measure the longer sample
            divider = 2;        // 0.5 s

        QueryPerformanceCounter(&t3);
        t4 = t3;
        while (t4.QuadPart < t3.QuadPart + f.QuadPart / divider)
            QueryPerformanceCounter(&t4);

        c2 = __rdtsc();
        QueryPerformanceCounter(&t2);

        // restore original priority
        SetThreadPriority(t, tp);
        SetPriorityClass(p, pp);

        // compute frequency
        *mhz = (DWORD)(f.QuadPart * (c2 - c1) / (t2.QuadPart - t1.QuadPart) / 1000000);

        return TRUE;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return FALSE;
    }
#endif // !_M_ARM64
}

HANDLE WINAPI GetThreadHandleFromID(DWORD dwThreadID, BOOL bInherit)
{
    return OpenThread(THREAD_ALL_ACCESS, bInherit, dwThreadID);
}

struct VS_VERSIONINFO_HEADER
{
    WORD wLength;
    WORD wValueLength;
    WORD wType;
};

// VERSIONINFO could be retrieved via GetFileVersionInfo, but we use a direct memory approach
// which might be more reliable after a crash
BOOL GetModuleVersion(HINSTANCE hModule, char* buffer, int bufferLen)
{
    // Validate module memory is committed before probing PE headers — modules
    // may be partially mapped during concurrent load/unload
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(hModule, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT)
    {
        lstrcpynA(buffer, "unknown", bufferLen);
        return FALSE;
    }

    HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
    if (hRes == NULL)
    {
        lstrcpynA(buffer, "unknown", bufferLen);
        return FALSE;
    }

    HGLOBAL hVer = LoadResource(hModule, hRes);
    if (hVer == NULL)
    {
        lstrcpynA(buffer, "unknown", bufferLen);
        return FALSE;
    }

    DWORD resSize = SizeofResource(hModule, hRes);
    const BYTE* first = (BYTE*)LockResource(hVer);
    if (resSize == 0 || first == 0)
    {
        lstrcpynA(buffer, "unknown", bufferLen);
        return FALSE;
    }
    const BYTE* iterator = first + sizeof(VS_VERSIONINFO_HEADER);

    DWORD signature = 0xFEEF04BD;

    while (memcmp(iterator, &signature, 4) != 0)
    {
        iterator++;
        if (iterator + 4 >= first + resSize)
        {
            lstrcpynA(buffer, "unknown", bufferLen);
            return FALSE;
        }
    }

    VS_FIXEDFILEINFO* ffi = (VS_FIXEDFILEINFO*)iterator;

    char buff[200];
    sprintf(buff, "%u.%u.%u.%u", HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
            HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    lstrcpynA(buffer, buff, bufferLen);
    return TRUE;
}

#define VER_NT_WORKSTATION 0x0000001
#define VER_NT_DOMAIN_CONTROLLER 0x0000002
#define VER_NT_SERVER 0x0000003

#define VER_SUITE_ENTERPRISE 0x00000002
#define VER_SUITE_DATACENTER 0x00000080
#define VER_SUITE_PERSONAL 0x00000200

struct MonitorEnumProcData
{
    int Index;
    FPrintLine PrintLine;
    void* Param;
};

BOOL CALLBACK
MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
    char buf[1024];
    MonitorEnumProcData* data = (MonitorEnumProcData*)dwData;

    MONITORINFOEX mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(hMonitor, &mi);
    RECT r = mi.rcMonitor;
    sprintf(buf, "Monitor %d: (%d,%d)-(%d,%d) %dx%d pixels", data->Index, r.left, r.top, r.right, r.bottom,
            r.right - r.left, r.bottom - r.top);

    // hdcMonitor is NULL, we must create a DC
    HDC hdc = NOHANDLES(CreateDC(mi.szDevice, mi.szDevice, NULL, NULL));
    int planes = GetDeviceCaps(hdc, PLANES);
    int bitsPixels = GetDeviceCaps(hdc, BITSPIXEL);
    NOHANDLES(DeleteDC(hdc));
    sprintf(buf + strlen(buf), ", planes: %d, bits per pixel: %d", planes, bitsPixels);

    if (mi.dwFlags & MONITORINFOF_PRIMARY)
        strcat(buf, ", primary display");
    data->PrintLine(data->Param, buf, TRUE);

    DISPLAY_DEVICE dd;
    dd.cb = sizeof(dd);
    int index = 0;
    while (EnumDisplayDevices(NULL, index, &dd, 0))
    {
        // DISPLAY_DEVICE/MONITORINFOEX are the SDK's UNICODE-macro-driven typedefs;
        // both DeviceName and szDevice are already WCHAR[32] under _UNICODE, so they compare
        // directly there - the (const char*) reinterpret cast was only a no-op in the narrow
        // build (both fields already char[32] there), never a real behavior difference.
        if (_wcsicmp(dd.DeviceName, mi.szDevice) == 0)
        {
            // DISPLAY_DEVICE's DeviceString is WCHAR[128] under _UNICODE; buf is
            // this function's own narrow-only text buffer.
            const int prefixLength = sprintf_s(buf, "Monitor %d Device Name: ", data->Index);
            if (prefixLength >= 0)
                CopyModuleTextNarrow(buf + prefixLength, sizeof(buf) - prefixLength,
                                     dd.DeviceString);
            data->PrintLine(data->Param, buf, TRUE);
            break;
        }
        index++;
    }

    data->Index++;
    return TRUE;
}

typedef BOOL(WINAPI* FEnumProcessModules)(
    HANDLE hProcess,
    HMODULE* lphModule,
    DWORD cb,
    LPDWORD lpcbNeeded);

typedef DWORD(WINAPI* FGetModuleFileNameExA)(
    HANDLE hProcess,
    HMODULE hModule,
    LPSTR lpFilename,
    DWORD nSize);

#pragma pack(4)
typedef struct _MODULEINFO
{
    LPVOID lpBaseOfDll;
    DWORD SizeOfImage;
    LPVOID EntryPoint;
} MODULEINFO, *LPMODULEINFO;
#pragma pack()

typedef BOOL(WINAPI* FGetModuleInformation)(
    HANDLE hProcess,
    HMODULE hModule,
    LPMODULEINFO lpmodinfo,
    DWORD cb);

BOOL FindInGlobalModulesStore(const char* str, int& index)
{
    if (GlobalModulesStore.Count == 0)
    {
        index = 0;
        return FALSE;
    }

    int l = 0, r = GlobalModulesStore.Count - 1, m;
    while (1)
    {
        m = (l + r) / 2;
        int res = strcmp(str, GlobalModulesStore[m]);
        if (res == 0) // found
        {
            index = m;
            return TRUE;
        }
        else if (res < 0)
        {
            if (l == r || l > m - 1) // not found
            {
                index = m;
                return FALSE;
            }
            r = m - 1;
        }
        else
        {
            if (l == r) // not found
            {
                index = m + 1;
                return FALSE;
            }
            l = m + 1;
        }
    }
}

// structure used for digests from individual thread stacks
// generated while enumerating module a
struct CModuleInfo
{
    char* Name;        // module name (without path)
    void* BaseAddress; // module base address
    DWORD Size;        // module size in memory
};

class CModulesInfo
{
public:
    TDirectArray<CModuleInfo> Modules;

public:
    CModulesInfo() : Modules(1, 10)
    {
    }

    ~CModulesInfo()
    {
        Clean();
    }

    void Clean()
    {
        int i;
        for (i = 0; i < Modules.Count; i++)
        {
            CModuleInfo* module = &Modules[i];
            if (module->Name != NULL)
            {
                free(module->Name);
                module->Name = NULL;
            }
        }
        Modules.DetachMembers();
    }

    BOOL Add(const char* name, void* baseAdderess, DWORD size)
    {
        CModuleInfo module;

        const char* myName = strrchr(name, '\\');
        if (myName != NULL)
            myName++;
        else
            myName = name;
        module.Name = DupAnsiString(myName);
        if (module.Name == NULL)
            return FALSE;
        module.BaseAddress = baseAdderess;
        module.Size = size;
        Modules.Add(module);
        if (!Modules.IsGood())
        {
            Modules.ResetState();
            free(module.Name);
            return FALSE;
        }
        return TRUE;
    }

    CModuleInfo* Find(void* address)
    {
        int i;
        for (i = 0; i < Modules.Count; i++)
        {
            CModuleInfo* module = &Modules[i];
            if (address >= module->BaseAddress && address < (BYTE*)module->BaseAddress + module->Size)
                return module;
        }
        return NULL;
    }
};

#define PRODUCT_ULTIMATE 0x00000001
#define PRODUCT_HOME_BASIC 0x00000002
#define PRODUCT_HOME_PREMIUM 0x00000003
#define PRODUCT_ENTERPRISE 0x00000004
#define PRODUCT_HOME_BASIC_N 0x00000005
#define PRODUCT_BUSINESS 0x00000006
#define PRODUCT_STANDARD_SERVER 0x00000007
#define PRODUCT_DATACENTER_SERVER 0x00000008
#define PRODUCT_SMALLBUSINESS_SERVER 0x00000009
#define PRODUCT_ENTERPRISE_SERVER 0x0000000A
#define PRODUCT_STARTER 0x0000000B
#define PRODUCT_DATACENTER_SERVER_CORE 0x0000000C
#define PRODUCT_STANDARD_SERVER_CORE 0x0000000D
#define PRODUCT_ENTERPRISE_SERVER_CORE 0x0000000E
#define PRODUCT_ENTERPRISE_SERVER_IA64 0x0000000F
#define PRODUCT_BUSINESS_N 0x00000010
#define PRODUCT_WEB_SERVER 0x00000011
#define PRODUCT_CLUSTER_SERVER 0x00000012
#define PRODUCT_HOME_SERVER 0x00000013
#define PRODUCT_STORAGE_EXPRESS_SERVER 0x00000014
#define PRODUCT_STORAGE_STANDARD_SERVER 0x00000015
#define PRODUCT_STORAGE_WORKGROUP_SERVER 0x00000016
#define PRODUCT_STORAGE_ENTERPRISE_SERVER 0x00000017
#define PRODUCT_SERVER_FOR_SMALLBUSINESS 0x00000018
#define PRODUCT_SMALLBUSINESS_SERVER_PREMIUM 0x00000019

#define VER_SUITE_COMPUTE_SERVER 0x00004000
#define VER_SUITE_STORAGE_SERVER 0x00002000
#define VER_SUITE_WH_SERVER 0x00008000

typedef void(WINAPI* PGNSI)(LPSYSTEM_INFO);
#define BUFSIZE 80
#define SM_SERVERR2 89

BOOL PrintSystemVersion(FPrintLine PrintLine, void* param, char* buf, char* avbuf,
                        size_t avbufCapacity)
{
    // Explicitly OSVERSIONINFOEXA - the code below deliberately resolves and calls
    // "GetVersionExA" by name, so the struct it fills must always be the A layout. This is a
    // SEPARATE variable from the OSVERSIONINFOEX further down, which SalGetVersionEx (a
    // project-owned, wchar_t-generic-by-design function) fills instead - the two calls need
    // different struct widths and must not share storage.
    static OSVERSIONINFOEXA osvi;
    static OSVERSIONINFOEX osviSal;
    static SYSTEM_INFO si;
    static BOOL bOsVersionInfoEx;
    static PGNSI pGNSI;
    pGNSI = NULL;

    PrintLine(param, "System Version:", FALSE);

    // avoid deprecated warning (the function may be removed from the SDK in the future)
    typedef BOOL(WINAPI * FDynGetVersionExA)(LPOSVERSIONINFOA lpVersionInformation);
    FDynGetVersionExA DynGetVersionExA = (FDynGetVersionExA)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetVersionExA");
    if (DynGetVersionExA != NULL)
    {
        ZeroMemory(&osvi, sizeof(OSVERSIONINFOEXA));
        osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXA);

        // Try calling GetVersionEx using the OSVERSIONINFOEX structure.
        // If that fails, try using the OSVERSIONINFO structure.
        if ((bOsVersionInfoEx = DynGetVersionExA((LPOSVERSIONINFOA)&osvi)) == 0)
        {
            // If OSVERSIONINFOEX doesn't work, try OSVERSIONINFO.
            osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
            if (!DynGetVersionExA((LPOSVERSIONINFOA)&osvi))
                return FALSE;
        }

        sprintf(buf, "GetVersionEx Version %u.%u (Build %u)", osvi.dwMajorVersion,
                osvi.dwMinorVersion, osvi.dwBuildNumber & 0xFFFF);
        if (bOsVersionInfoEx)
        {
            sprintf(buf + strlen(buf), " SP %u.%u, %s", osvi.wServicePackMajor,
                    osvi.wServicePackMinor, osvi.szCSDVersion);
        }
        PrintLine(param, buf, TRUE);
    }

    ZeroMemory(&osviSal, sizeof(OSVERSIONINFOEX));
    SalGetVersionEx(&osviSal, FALSE); // !!! SLOW, original GetVersionEx() is deprecated !!!
    sprintf(buf, "SalGetVersionEx Version %u.%u (Build %u)", osviSal.dwMajorVersion,
            osviSal.dwMinorVersion, osviSal.dwBuildNumber & 0xFFFF);
    sprintf(buf + strlen(buf), " SP %u.%u, SMask %u, PType %u, PlatId %u", osviSal.wServicePackMajor,
            osviSal.wServicePackMinor, osviSal.wSuiteMask, osviSal.wProductType, osviSal.dwPlatformId);
    PrintLine(param, buf, TRUE);

    // Call GetNativeSystemInfo if supported or GetSystemInfo otherwise.
    pGNSI = (PGNSI)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetNativeSystemInfo"); // Min: XP
    if (pGNSI != NULL)
    {
        pGNSI(&si);
        sprintf(buf, "GetNativeSystemInfo architecture ");
        if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
            sprintf(buf + strlen(buf), "64-bit (x64)");
        else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
            sprintf(buf + strlen(buf), "32-bit (x86)");
        else
            sprintf(buf + strlen(buf), "%d (unknown)", si.wProcessorArchitecture);
        PrintLine(param, buf, TRUE);
    }

    if (!GetEnvironmentPathReportText(TRUE, avbuf, avbufCapacity))
        lstrcpynA(avbuf, "(unavailable)", static_cast<int>(avbufCapacity));
    sprintf(buf, "System directory: %s", avbuf);
    PrintLine(param, buf, TRUE);
    if (!GetEnvironmentPathReportText(FALSE, avbuf, avbufCapacity))
        lstrcpynA(avbuf, "(unavailable)", static_cast<int>(avbufCapacity));
    sprintf(buf, "Windows directory: %s", avbuf);
    PrintLine(param, buf, TRUE);

    return TRUE;
}

const char* FindModuleName(char* buf, size_t bufCapacity, void* address,
                           BOOL unloadedName = FALSE)
{
    if (buf == NULL || bufCapacity == 0)
        return "";
    buf[0] = 0;
    int i;
    for (i = 0; i < GlobalModulesStore.Count; i++)
    {
        if (GlobalModulesStore[i] != NULL)
        {
            char* s = GlobalModulesStore[i];
            if (*s == '0' && *(s + 1) == 'x')
            { // line format: sprintf(buf, "0x%p (size: 0x%X) (ver: %s): %s", hInstance, iterator->SizeOfImage, ver, szModName);
                char* addr;
                DWORD size;
                sscanf(s, "0x%p (size: 0x%X)", &addr, &size);
                if (addr <= address && address < addr + size)
                {
                    s = strstr(s, "): ");
                    if (s != NULL)
                    {
                        const char* moduleText = s + 3;
                        const char* moduleDetail = strstr(moduleText, " (");
                        const size_t moduleLength = moduleDetail != NULL
                                                            ? static_cast<size_t>(moduleDetail - moduleText)
                                                            : strlen(moduleText);
                        const int moduleChars = static_cast<int>(
                            moduleLength > 0x7FFFFFFF ? 0x7FFFFFFF : moduleLength);
                        if (unloadedName)
                            _snprintf_s(buf, bufCapacity, _TRUNCATE, "(UNLOADED: %.*s)",
                                        moduleChars, moduleText);
                        else
                            _snprintf_s(buf, bufCapacity, _TRUNCATE, " (%.*s: 0x%X)",
                                        moduleChars, moduleText,
                                        (DWORD)((char*)address - addr));
                        return buf;
                    }
                }
            }
        }
    }
    return "";
}

static CModulesInfo ModulesInfo;

// The bug report is a narrow text file by design and this code runs while the process is
// crashing. The formatted size remains dynamically owned UTF-16 until this named diagnostic
// byte sink.
static void AppendDiskSizeA(char* dst, int dstSizeInChars, const CQuadWord& size)
{
    const std::wstring wide = PrintDiskSize(size, 0);
    int used = (int)strlen(dst);
    if (used < dstSizeInChars - 1)
        CopyModuleTextNarrow(dst + used, static_cast<size_t>(dstSizeInChars - used),
                             wide.c_str());
}

// The bug report is an ACP byte stream. Keep its encoding at this named sink while the
// numeric value and locale separator stay native-wide everywhere else.
static void AppendNumberA(char* dst, int dstSizeInChars, const CQuadWord& number)
{
    const std::wstring wide = NumberToStr(number);
    const int used = static_cast<int>(strlen(dst));
    if (used < dstSizeInChars - 1)
        CopyModuleTextNarrow(dst + used, static_cast<size_t>(dstSizeInChars - used), wide.c_str());
}

static BOOL GetDosDeviceReportText(BYTE driveNum, char* target, size_t targetSize)
{
    std::wstring deviceName;
    if (!MyQueryDosDeviceW(driveNum, deviceName))
        return FALSE;
    CopyModuleTextNarrow(target, targetSize, deviceName.c_str());
    return TRUE;
}

static BOOL GetVolumeReportText(const wchar_t* root, char* volumeName,
                                size_t volumeNameSize, char* fileSystemName,
                                size_t fileSystemNameSize,
                                DWORD* volumeSerialNumber,
                                DWORD* maximumComponentLength,
                                DWORD* fileSystemFlags)
{
    std::wstring volumeNameW;
    std::wstring fileSystemNameW;
    if (!MyGetVolumeInformationW(root, NULL, NULL, NULL, &volumeNameW,
                                 volumeSerialNumber, maximumComponentLength,
                                 fileSystemFlags, &fileSystemNameW))
        return FALSE;
    CopyModuleTextNarrow(volumeName, volumeNameSize, volumeNameW.c_str());
    CopyModuleTextNarrow(fileSystemName, fileSystemNameSize,
                         fileSystemNameW.c_str());
    return TRUE;
}

// Keep C++ ownership outside PrintBugReport: that function contains SEH
// __try blocks and therefore cannot contain objects that require unwinding.
static BOOL AppendPluginFSPathA(CPluginFSInterfaceEncapsulation* pluginFS,
                                char* target, size_t targetSize)
{
    if (pluginFS == NULL || target == NULL || targetSize == 0)
        return FALSE;
    std::wstring path;
    if (!pluginFS->GetCurrentPathW(path))
        return FALSE;
    const size_t used = strlen(target);
    if (used >= targetSize)
        return FALSE;
    CopyModuleTextNarrow(target + used, targetSize - used, path.c_str());
    return TRUE;
}

void CCallStack::PrintBugReport(EXCEPTION_POINTERS* Exception, DWORD ThreadID, DWORD ShellExtCrashID,
                                FPrintLine PrintLine, void* param)
{
    //  static TDirectArray<DWORD> knownThreads(20, 10);
    if (Exception != NULL)
    {
        ModulesInfo.Clean();
        //    knownThreads.DetachMembers();
    }

    static char buf[1024];
    static char num[50];
    // Final crash-report byte scratch. Its capacity is derived from the external
    // MODULEENTRY32W component contract and worst-case UTF-8 expansion, not a path limit.
    static char avbuf[sizeof(((MODULEENTRY32*)0)->szExePath) * 2 + 1];
    static char nameBuf[sizeof(((MODULEENTRY32*)0)->szExePath) * 2 + 1];
    static char volumeNameA[2000];
    static char fileSystemNameA[200];
    static char deviceNameA[4096];
    // Scratch for the shell-integration sections. Static like the rest: this runs inside a
    // process that may already be crashing, so we do not want it on the stack. Sized for a
    // \uXXXX-escaped long path plus the surrounding fields.
    static char BugReportDiagBuf[4096];
    // Fixed crash-path byte scratch is intentional. The semantic reason is dynamically
    // owned UTF-16; this diagnostic sink may truncate but never feeds runtime behavior.
    static char BugReportReasonBreakA[6001];

    strcpy(buf, SALAMANDER_TEXT_VERSION);
#ifdef _DEBUG
    strcat(buf, " (DEBUG)");
#endif //_DEBUG
    PrintLine(param, buf, FALSE);

    __try
    {
        PrintLine(param, "", FALSE);
        if (Exception != NULL)
        {
            PrintLine(param, "Information About Exception:", FALSE);

            const char* message = NULL;
            switch (Exception->ExceptionRecord->ExceptionCode)
            {
            case EXCEPTION_BREAKPOINT:
                message = "debug breakpoint";
                break;

            case EXCEPTION_ACCESS_VIOLATION:
            {
                lstrcpyA(avbuf, "access violation");
                if (Exception->ExceptionRecord->NumberParameters >= 2)
                {
                    sprintf(avbuf + strlen(avbuf), ": %s on 0x%p",
                            (Exception->ExceptionRecord->ExceptionInformation[0] == 0) ? "read" : "write",
                            (void*)Exception->ExceptionRecord->ExceptionInformation[1]);
                }
                message = avbuf;
                break;
            }

            case EXCEPTION_STACK_OVERFLOW:
                message = "stack overflow";
                break;
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                message = "divide by zero";
                break;
            case 0xE06D7363:
                message = "microsoft C++ exception";
                break;
            case OPENSAL_EXCEPTION_RTC:
                message = "open salamander rtc exception";
                break;
            case OPENSAL_EXCEPTION_BREAK:
                message = "open salamander break exception";
                break;
            }
            sprintf(num, "code 0x%X", Exception->ExceptionRecord->ExceptionCode);
            if (message == NULL)
                message = num;

            sprintf(buf, "Exception: %s", message);
            PrintLine(param, buf, TRUE);
            sprintf(buf, "Exception origin: thread ID = 0x%X, execution address = 0x%p%s", ThreadID,
                    Exception->ExceptionRecord->ExceptionAddress,
                    FindModuleName(nameBuf, _countof(nameBuf),
                                   Exception->ExceptionRecord->ExceptionAddress));
            PrintLine(param, buf, TRUE);
            if (IsDebuggerPresent())
            {
                // the x64/Debug build on W7 started from the MSVC debugger did not generate a valid minidump (function failed, dump incomplete)
                // outside MSVC everything worked correctly - note that we are running from the debugger
                sprintf(buf, "Debugger is present!");
                PrintLine(param, buf, TRUE);
            }
            PrintLine(param, "", FALSE);
        }
        else
        {
#ifndef CALLSTK_DISABLE
            CCallStack* stack = CCallStack::GetThis();
            if (stack != NULL)
            {
                stack->DontSuspend = TRUE;
            }
            CCallStack::ExceptionExists = TRUE;
#endif // CALLSTK_DISABLE
        }

        const wchar_t* bugReportReasonBreak = GetBugReportReasonBreak();
        if (bugReportReasonBreak[0] != 0)
        {
            PrintLine(param, "Break was used.", FALSE);
            // The report writer is intentionally ANSI. Escaping here preserves every
            // UTF-16 code unit without a locale-dependent CP_ACP conversion while the
            // crash-path producers and retained reason remain wide.
            AppendAsciiEscapedW(bugReportReasonBreak, BugReportReasonBreakA,
                                (int)sizeof(BugReportReasonBreakA));
            PrintLine(param, BugReportReasonBreakA, FALSE);
            PrintLine(param, "", FALSE);
        }

        if (ShellExtCrashID != -1)
        {
            sprintf(buf, "Some faulty shell extension throwed exception.\r\nLook at HandleException(GetExceptionInformation(), %u)", ShellExtCrashID);
            PrintLine(param, buf, FALSE);
            PrintLine(param, "", FALSE);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    //#ifdef MSVC_RUNTIME_CHECKS
    __try
    {
        if (RTCErrorDescription[0] != 0)
        {
            PrintLine(param, "RTC Error", FALSE);
            PrintLine(param, RTCErrorDescription, FALSE); // indentation is embedded in the string, it is multi-line
            PrintLine(param, "", FALSE);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }
    //#endif // MSVC_RUNTIME_CHECKS

#ifndef CALLSTK_DISABLE
    BOOL threadNotFound = FALSE;
    __try
    {
        // output the call stack of the thread where the exception occurred
        PrintLine(param, "Call Stacks:", FALSE);
        int excepThreadIndex = -1;
        if (Exception != NULL)
        {
            int i;
            for (i = 0; i < CCallStack::CallStacks.Count; i++)
            {
                CCallStack* stack = CCallStack::CallStacks[i];
                if (ThreadID == stack->ThreadID)
                {
                    sprintf(buf, "Thread with Exception (ID: 0x%X)", stack->ThreadID);
                    char* term = buf + strlen(buf);
                    const wchar_t* dll = stack->GetPluginDLLName();
                    if (dll != NULL)
                    {
                        __try
                        {
                            CPluginData* data = FindPluginDataByDllName(dll);
                            if (data != NULL)
                                sprintf(term, ": in %ls", data->Name.c_str());
                        }
                        __except (EXCEPTION_EXECUTE_HANDLER)
                        {
                            strcpy(term, ": in (exception)");
                        }
                    }
                    PrintLine(param, buf, TRUE);

                    stack->Reset();
                    const char* s;
                    while ((s = stack->GetNextLine()) != NULL)
                    {
                        PrintLine(param, s, TRUE);
                    }
                    if (stack->Skipped > 0)
                    {
                        sprintf(buf, "Number of skipped records: %d", stack->Skipped);
                        PrintLine(param, buf, TRUE);
                    }
                    if (CCallStack::CallStacks.Count > 1)
                        PrintLine(param, "----", TRUE);
                    excepThreadIndex = i;
                    break;
                }
            }
            if (i == CCallStack::CallStacks.Count)
            {
                PrintLine(param, "Thread with Exception was NOT FOUND (it is not registered for call-stacks)!", TRUE);
                PrintLine(param, "It can also be Find thread and Exception was generated during exiting of thread.", TRUE);
                PrintLine(param, "", FALSE);
                threadNotFound = TRUE;
            }
        }
        // output the remaining call stacks
        int i;
        for (i = 0; i < CCallStack::CallStacks.Count; i++)
        {
            if (i != excepThreadIndex)
            {
                CCallStack* stack = CCallStack::CallStacks[i];
                sprintf(buf, "Thread ID: 0x%X", stack->ThreadID);
                PrintLine(param, buf, TRUE);
                stack->Reset();
                const char* s;
                while ((s = stack->GetNextLine()) != NULL)
                {
                    PrintLine(param, s, TRUE);
                }
                if (stack->Skipped > 0)
                {
                    sprintf(buf, "Number of skipped records: %d", stack->Skipped);
                    PrintLine(param, buf, TRUE);
                }
                if (i + 1 < CCallStack::CallStacks.Count &&
                    (i + 1 != CCallStack::CallStacks.Count - 1 || excepThreadIndex != CCallStack::CallStacks.Count - 1))
                {
                    PrintLine(param, "----", TRUE);
                }
            }
        }
        PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }
#endif // CALLSTK_DISABLE

#ifdef _WIN64
    __try
    {
        // output available registers and, if that doesn't throw an exception, also the stack
        if (Exception != NULL && Exception->ContextRecord != NULL)
        {
            static DWORD64 pointersForDump[32 + 64 + 8 + 8]; // stack, eax, ebx, ecx, edx, esi, edi, ebp, eip ;;; esp-32, esp-28, ... esp-4
            ZeroMemory(&pointersForDump, sizeof(pointersForDump));
            PrintLine(param, "Registers:", FALSE);
            PCONTEXT ctxtRec = Exception->ContextRecord;
            DWORD idx = 32 + 64;
#ifdef _M_ARM64
            if (ctxtRec->ContextFlags & CONTEXT_INTEGER)
            {
                sprintf(buf, "X0  = 0x%p  X1  = 0x%p", (void*)ctxtRec->X0, (void*)ctxtRec->X1);
                PrintLine(param, buf, TRUE);
                sprintf(buf, "X2  = 0x%p  X3  = 0x%p", (void*)ctxtRec->X2, (void*)ctxtRec->X3);
                PrintLine(param, buf, TRUE);
                pointersForDump[idx++] = ctxtRec->X0;
                pointersForDump[idx++] = ctxtRec->X1;
                pointersForDump[idx++] = ctxtRec->X2;
                pointersForDump[idx++] = ctxtRec->X3;
                pointersForDump[idx++] = ctxtRec->Fp;
                pointersForDump[idx++] = ctxtRec->Lr;
                pointersForDump[idx++] = ctxtRec->Pc;
            }
            if (ctxtRec->ContextFlags & CONTEXT_CONTROL)
            {
                int jj;
                for (jj = 8; jj > 0; jj--)
                    pointersForDump[idx++] = ctxtRec->Sp - jj * 4;
                sprintf(buf, "PC  = 0x%p  SP  = 0x%p", (void*)ctxtRec->Pc, (void*)ctxtRec->Sp);
                PrintLine(param, buf, TRUE);
                sprintf(buf, "FP  = 0x%p  LR  = 0x%p", (void*)ctxtRec->Fp, (void*)ctxtRec->Lr);
                PrintLine(param, buf, TRUE);
            }
#else  // x64
            if (ctxtRec->ContextFlags & CONTEXT_INTEGER)
            {
                sprintf(buf, "RAX = 0x%p  RBX = 0x%p", (void*)ctxtRec->Rax, (void*)ctxtRec->Rbx);
                PrintLine(param, buf, TRUE);
                sprintf(buf, "RCX = 0x%p  RDX = 0x%p", (void*)ctxtRec->Rcx, (void*)ctxtRec->Rdx);
                PrintLine(param, buf, TRUE);
                sprintf(buf, "RSI = 0x%p  RDI = 0x%p", (void*)ctxtRec->Rsi, (void*)ctxtRec->Rdi);
                PrintLine(param, buf, TRUE);
                pointersForDump[idx++] = ctxtRec->Rax;
                pointersForDump[idx++] = ctxtRec->Rbx;
                pointersForDump[idx++] = ctxtRec->Rcx;
                pointersForDump[idx++] = ctxtRec->Rdx;
                pointersForDump[idx++] = ctxtRec->Rsi;
                pointersForDump[idx++] = ctxtRec->Rdi;
                pointersForDump[idx++] = ctxtRec->Rbp;
                pointersForDump[idx++] = ctxtRec->Rip;
            }
            if (ctxtRec->ContextFlags & CONTEXT_CONTROL)
            {
                int jj;
                for (jj = 8; jj > 0; jj--)
                    pointersForDump[idx++] = ctxtRec->Rsp - jj * 4;
                sprintf(buf, "RIP = 0x%p  RSP = 0x%p", (void*)ctxtRec->Rip, (void*)ctxtRec->Rsp);
                PrintLine(param, buf, TRUE);
                sprintf(buf, "RBP = 0x%p  RFL = 0x%X", (void*)ctxtRec->Rbp, ctxtRec->EFlags);
                PrintLine(param, buf, TRUE);
            }
#endif // _M_ARM64
            PrintLine(param, "", FALSE);
#ifndef _DEBUG // to prevent stopping the debugger in debug builds
            if (ctxtRec->ContextFlags & CONTEXT_CONTROL)
            {
                DWORD64* rsp = (DWORD64*)CONTEXT_SP(*ctxtRec);
                int i = 0;
                int validStackCount = 0;
                while (i < 32 + 64)
                {
                    // esp may point somewhere invalid - prepare for it
                    __try
                    {
                        pointersForDump[i] = *(rsp + i);
                        i++;
                        validStackCount++;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        i = 32 + 64; // exit
                    }
                }

                i = 32 + 64 + 8;
                int jj = 0;
                int validStackCount2 = 32 + 64 + 8;
                while (i < 32 + 64 + 8 + 8)
                {
                    // esp may point somewhere invalid - prepare for it
                    __try
                    {
                        pointersForDump[i] = *(rsp - 8 + jj);
                        i++;
                        jj++;
                        validStackCount2++;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        i = 32 + 64 + 8 + 8; // exit
                    }
                }

                PrintLine(param, "Stack & Memory Dump:", FALSE);
                i = 0;
                while (i < 32 + 64 + 8 + 8)
                {
                    if (i < 32 + 64 && i < validStackCount || i >= 32 + 64 && i < validStackCount2)
                    {
                        if (i == 32 + 64)
                            PrintLine(param, "---- RAX, RBX, RCX, RDX, RSI, RDI, RBP, RIP:", TRUE);
                        if (i == 32 + 64 + 8)
                            PrintLine(param, "---- RSP-32, RSP-28, RSP-24, RSP-20, RSP-16, RSP-12, RSP-8, RSP-4:", TRUE);
                        __try
                        {
                            if (i < 32 || i >= 32 + 64)
                            {
                                buf[0] = 0;
                                sprintf(buf, "%p: ", (void*)pointersForDump[i]);
                                BYTE* iterator = (BYTE*)(pointersForDump[i]);
                                int j = 0;
                                while (j < 28)
                                {
                                    // spaces waste space; disable them and rather append the values
                                    //                if (j > 0)
                                    //                  lstrcatA(buf, " ");
                                    __try
                                    {
                                        sprintf(buf + lstrlenA(buf), "%02X", *(iterator + j));
                                        j++;
                                    }
                                    __except (EXCEPTION_EXECUTE_HANDLER)
                                    {
                                        lstrcatA(buf, "(exception)");
                                        j = 28;
                                    }
                                }
                                PrintLine(param, buf, TRUE);
                                i++;
                            }
                            else
                            {
                                if (i + 4 < validStackCount)
                                {
                                    sprintf(buf, "%p  %p  %p  %p", (void*)pointersForDump[i], (void*)pointersForDump[i + 1],
                                            (void*)pointersForDump[i + 2], (void*)pointersForDump[3 + i]);
                                    PrintLine(param, buf, TRUE);
                                }
                                i += 4;
                            }
                        }
                        __except (EXCEPTION_EXECUTE_HANDLER)
                        {
                            PrintLine(param, "(exception)", TRUE);
                            i = 32 + 64 + 8 + 8; // exit
                        }
                    }
                    else
                        i++;
                }
                PrintLine(param, "", FALSE);
            }
#endif //_DEBUG  // to prevent stopping the debugger in debug builds
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }
#else          // _WIN64
    __try
    {
        // output available registers and, if that doesn't throw an exception, also the stack
        if (Exception != NULL && Exception->ContextRecord != NULL)
        {
            static DWORD pointersForDump[32 + 64 + 8 + 8]; // stack, eax, ebx, ecx, edx, esi, edi, ebp, eip ;;; esp-32, esp-28, ... esp-4
            ZeroMemory(&pointersForDump, sizeof(pointersForDump));
            PrintLine(param, "Registers:", FALSE);
            PCONTEXT ctxtRec = Exception->ContextRecord;
            DWORD idx = 32 + 64;
            if (ctxtRec->ContextFlags & CONTEXT_INTEGER)
            {
                sprintf(buf, "EAX = 0x%p  EBX = 0x%p", (void*)ctxtRec->Eax, (void*)ctxtRec->Ebx);
                PrintLine(param, buf, TRUE);
                sprintf(buf, "ECX = 0x%p  EDX = 0x%p", (void*)ctxtRec->Ecx, (void*)ctxtRec->Edx);
                PrintLine(param, buf, TRUE);
                sprintf(buf, "ESI = 0x%p  EDI = 0x%p", (void*)ctxtRec->Esi, (void*)ctxtRec->Edi);
                PrintLine(param, buf, TRUE);
                pointersForDump[idx++] = ctxtRec->Eax;
                pointersForDump[idx++] = ctxtRec->Ebx;
                pointersForDump[idx++] = ctxtRec->Ecx;
                pointersForDump[idx++] = ctxtRec->Edx;
                pointersForDump[idx++] = ctxtRec->Esi;
                pointersForDump[idx++] = ctxtRec->Edi;
                pointersForDump[idx++] = ctxtRec->Ebp;
                pointersForDump[idx++] = ctxtRec->Eip;
            }
            if (ctxtRec->ContextFlags & CONTEXT_CONTROL)
            {
                int jj;
                for (jj = 8; jj > 0; jj--)
                    pointersForDump[idx++] = ctxtRec->Esp - jj * 4;
                sprintf(buf, "EIP = 0x%p  ESP = 0x%p", (void*)ctxtRec->Eip, (void*)ctxtRec->Esp);
                PrintLine(param, buf, TRUE);
                sprintf(buf, "EBP = 0x%p  EFL = 0x%X", (void*)ctxtRec->Ebp, ctxtRec->EFlags);
                PrintLine(param, buf, TRUE);
            }
            PrintLine(param, "", FALSE);
#ifndef _DEBUG // to prevent stopping the debugger in debug builds
            if (ctxtRec->ContextFlags & CONTEXT_CONTROL)
            {
                DWORD* esp = (DWORD*)ctxtRec->Esp;
                int i = 0;
                int validStackCount = 0;
                while (i < 32 + 64)
                {
                    // esp may point somewhere invalid - prepare for it
                    __try
                    {
                        pointersForDump[i] = *(esp + i);
                        i++;
                        validStackCount++;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        i = 32 + 64; // exit
                    }
                }

                i = 32 + 64 + 8;
                int jj = 0;
                int validStackCount2 = 32 + 64 + 8;
                while (i < 32 + 64 + 8 + 8)
                {
                    // esp may point somewhere invalid - prepare for it
                    __try
                    {
                        pointersForDump[i] = *(esp - 8 + jj);
                        i++;
                        jj++;
                        validStackCount2++;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        i = 32 + 64 + 8 + 8; // exit
                    }
                }

                PrintLine(param, "Stack & Memory Dump:", FALSE);
                i = 0;
                while (i < 32 + 64 + 8 + 8)
                {
                    if (i < 32 + 64 && i < validStackCount || i >= 32 + 64 && i < validStackCount2)
                    {
                        if (i == 32 + 64)
                            PrintLine(param, "---- EAX, EBX, ECX, EDX, ESI, EDI, EBP, EIP:", TRUE);
                        if (i == 32 + 64 + 8)
                            PrintLine(param, "---- ESP-32, ESP-28, ESP-24, ESP-20, ESP-16, ESP-12, ESP-8, ESP-4:", TRUE);
                        __try
                        {
                            if (i < 32 || i >= 32 + 64)
                            {
                                buf[0] = 0;
                                sprintf(buf, "%p: ", (void*)pointersForDump[i]);
                                BYTE* iterator = (BYTE*)(pointersForDump[i]);
                                int j = 0;
                                while (j < 28)
                                {
                                    // spaces waste space; disable them and rather append the values
                                    //                if (j > 0)
                                    //                  lstrcatA(buf, " ");
                                    __try
                                    {
                                        sprintf(buf + lstrlenA(buf), "%02X", *(iterator + j));
                                        j++;
                                    }
                                    __except (EXCEPTION_EXECUTE_HANDLER)
                                    {
                                        lstrcatA(buf, "(exception)");
                                        j = 28;
                                    }
                                }
                                PrintLine(param, buf, TRUE);
                                i++;
                            }
                            else
                            {
                                if (i + 4 < validStackCount)
                                {
                                    sprintf(buf, "%p  %p  %p  %p", (void*)pointersForDump[i], (void*)pointersForDump[i + 1],
                                            (void*)pointersForDump[i + 2], (void*)pointersForDump[3 + i]);
                                    PrintLine(param, buf, TRUE);
                                }
                                i += 4;
                            }
                        }
                        __except (EXCEPTION_EXECUTE_HANDLER)
                        {
                            PrintLine(param, "(exception)", TRUE);
                            i = 32 + 64 + 8 + 8; // exit
                        }
                    }
                    else
                        i++;
                }
                PrintLine(param, "", FALSE);
            }
#endif         //_DEBUG  // to prevent stopping the debugger in debug builds
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }
#endif         // _WIN64

    __try
    {
        if (MainThreadID == ThreadID && MessagesKeeper.GetCount() > 0)
        {
            PrintLine(param, "Main Thread Messages:", FALSE);
            int count = MessagesKeeper.GetCount();
            int i;
            for (i = 0; i < count; i++)
            {
                MessagesKeeper.Print(buf, 500, i);
                PrintLine(param, buf, TRUE);
            }
            PrintLine(param, "", FALSE);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        if (Exception != NULL)
        {
            PrintLine(param, "Window Handles:", FALSE);

            lstrcpyA(buf, "MainWindow=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "LeftPanel=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->LeftPanel);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "LeftFilesBox=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->LeftPanel->ListBox);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "LeftDirectoryLine=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->LeftPanel->DirectoryLine);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "LeftToolBar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->LeftPanel->DirectoryLine->ToolBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "LeftStatusLine=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->LeftPanel->StatusLine);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "RightPanel=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->RightPanel);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "RightFilesBox=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->RightPanel->ListBox);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "RightDirectoryLine=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->RightPanel->DirectoryLine);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "RightToolBar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->RightPanel->DirectoryLine->ToolBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "RightStatusLine=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->RightPanel->StatusLine);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "TopRebar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->HTopRebar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "MenuBar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->MenuBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "TopToolBar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->TopToolBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "MiddleToolBar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->MiddleToolBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "UMToolBar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->UMToolBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "HPToolBar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->HPToolBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "PluginsBar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->PluginsBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "DriveBar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->DriveBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "DriveBar2=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->DriveBar2);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "BottomToolBar=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->BottomToolBar);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "EditWindow=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->EditWindow);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            lstrcpyA(buf, "EditLine=");
            __try
            {
                sprintf(buf + lstrlenA(buf), "0x%p", MainWindow->EditWindow->GetEditLine());
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                lstrcatA(buf, "(exception)");
            }
            PrintLine(param, buf, TRUE);

            PrintLine(param, "", FALSE);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        if (Exception == NULL)
        {
#ifndef CALLSTK_DISABLE
            CCallStack::ExceptionExists = FALSE;
            CCallStack* stack = CCallStack::GetThis();
            if (stack != NULL)
            {
                stack->DontSuspend = FALSE;
            }
#endif // CALLSTK_DISABLE
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        PrintLine(param, "Global Variables:", FALSE);
        sprintf(buf, "StopRefresh = %d", StopRefresh);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "SnooperSuspended = %d", SnooperSuspended);
        PrintLine(param, buf, TRUE);
        if (MainWindow != NULL)
        {
            sprintf(buf, "ActivateSuspMode = %d", MainWindow->ActivateSuspMode);
            PrintLine(param, buf, TRUE);
        }
        sprintf(buf, "FullRowSelect = %d", Configuration.FullRowSelect);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "OnlyOneInstance = %d", Configuration.OnlyOneInstance);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "StatusArea = %d", Configuration.StatusArea);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "UseRecycleBin = %d", Configuration.UseRecycleBin);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "CnfrmDragDrop = %d", Configuration.CnfrmDragDrop);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "AlwaysOnTop = %d", Configuration.AlwaysOnTop);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "SortUsesLocale = %d", Configuration.SortUsesLocale);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "SortDetectNumbers = %d", Configuration.SortDetectNumbers);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "SortNewerOnTop = %d", Configuration.SortNewerOnTop);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "SortDirsByName = %d", Configuration.SortDirsByName);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "SortDirsByExt = %d", Configuration.SortDirsByExt);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "EHasOccured = %d, %d, %d, %d, %d, %d, %d, %d", MenuNewExceptionHasOccured,
                FGIExceptionHasOccured, ICExceptionHasOccured, QCMExceptionHasOccured,
                OCUExceptionHasOccured, GTDExceptionHasOccured, SHLExceptionHasOccured,
                RelExceptionHasOccured);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "ConfigWasImported = %d", Configuration.ConfigWasImported);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "NetwareFastDirMove = %d", Configuration.NetwareFastDirMove);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "UseAsyncCopyAlg = %d", Configuration.UseAsyncCopyAlg);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "ReloadEnvVariables = %d", Configuration.ReloadEnvVariables);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "AutoSave = %d", Configuration.AutoSave);
        PrintLine(param, buf, TRUE);
        AppendAsciiEscapedW(Configuration.IfPathIsInaccessibleGoTo.c_str(), BugReportDiagBuf,
                            (int)sizeof(BugReportDiagBuf));
        _snprintf_s(buf, _TRUNCATE, "IfPathIsInaccessibleGoTo (isMyDocs = %d) = %s",
                    Configuration.IfPathIsInaccessibleGoToIsMyDocs, BugReportDiagBuf);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "NoDrives = 0x%08X", SystemPolicies.GetNoDrives());
        PrintLine(param, buf, TRUE);
        sprintf(buf, "NoFind = %u", SystemPolicies.GetNoFind());
        PrintLine(param, buf, TRUE);
        sprintf(buf, "VisibleDrives = 0x%08X", Configuration.VisibleDrives);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "EnableCustomIconOverlays = %d", Configuration.EnableCustomIconOverlays);
        PrintLine(param, buf, TRUE);
        AppendAsciiEscapedW(Configuration.DisabledCustomIconOverlays, BugReportDiagBuf,
                            (int)sizeof(BugReportDiagBuf));
        _snprintf_s(buf, _TRUNCATE, "DisabledCustomIconOverlays = %s", BugReportDiagBuf);
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    // Shell integration: what actually happened to each registered icon-overlay handler,
    // and to the last few context-menu interactions. These two sections exist because the
    // TRACE_* calls in shiconov.cpp and shellsup.cpp are compiled out of Release, so this
    // report is the only channel that reaches a reporter running a shipped build.
    __try
    {
        PrintLine(param, "Icon Overlays:", FALSE);

        AppendAsciiEscapedW(ShellOverlayDiag.Header.ConfigRoot.c_str(), BugReportDiagBuf,
                            (int)sizeof(BugReportDiagBuf));
        _snprintf_s(buf, _TRUNCATE, "Config root in use: %s", BugReportDiagBuf);
        PrintLine(param, buf, TRUE);
        _snprintf_s(buf, _TRUNCATE, "ANSI code page: %u   Registered: %d   Loaded: %d",
                    ShellOverlayDiag.Header.AnsiCodePage,
                    ShellOverlayDiag.Header.Registered, ShellOverlayDiag.Header.Loaded);
        PrintLine(param, buf, TRUE);
        _snprintf_s(buf, _TRUNCATE, "System DPI: %u   Required icon sizes: %d/%d/%d",
                    ShellOverlayDiag.Header.SystemDpi, ShellOverlayDiag.Header.IconSizes[0],
                    ShellOverlayDiag.Header.IconSizes[1], ShellOverlayDiag.Header.IconSizes[2]);
        PrintLine(param, buf, TRUE);

        for (int i = 0; i < ShellOverlayDiag.Count(); i++)
        {
            const ShellOverlayDiagRecord* rec = ShellOverlayDiag.At(i);
            if (rec == NULL)
                continue;
            BugReportDiagBuf[0] = 0;
            FormatShellOverlayDiagRecord(*rec, BugReportDiagBuf, (int)sizeof(BugReportDiagBuf));
            _snprintf_s(buf, _TRUNCATE, "[%2d] %s", i, BugReportDiagBuf);
            PrintLine(param, buf, TRUE);
        }
        PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        PrintLine(param, "Shell Context Menu (oldest first):", FALSE);
        for (int i = 0; i < ShellMenuDiag.Count(); i++)
        {
            const ShellMenuDiagRecord* rec = ShellMenuDiag.At(i);
            if (rec == NULL)
                continue;
            BugReportDiagBuf[0] = 0;
            FormatShellMenuDiagRecord(*rec, BugReportDiagBuf, (int)sizeof(BugReportDiagBuf));

            // The formatter emits several lines; PrintLine takes one at a time.
            char* line = BugReportDiagBuf;
            while (*line != 0)
            {
                char* eol = strchr(line, '\n');
                if (eol != NULL)
                    *eol = 0;
                PrintLine(param, line, TRUE);
                if (eol == NULL)
                    break;
                line = eol + 1;
            }
        }
        PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        PrintLine(param, "Panels:", FALSE);
        BOOL printEOL = TRUE;
        int panelNum;
        for (panelNum = 0; MainWindow != NULL && panelNum < 2; panelNum++)
        {
            CFilesWindow* panel = panelNum == 0 ? MainWindow->LeftPanel : MainWindow->RightPanel;
            if (panel != NULL)
            {
                sprintf(buf, "%s panel (%s):", panelNum == 0 ? "Left" : "Right",
                        panel == MainWindow->GetActivePanel() ? "source" : "target");
                PrintLine(param, buf, TRUE);
                switch (panel->GetPanelType())
                {
                case ptDisk:
                {
                    // GetPathW is wide-only; %ls (not %s) is required here.
                    sprintf(buf, "Path = %ls", panel->GetPathW());
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "PanelType = %d", panel->GetPanelType());
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "Dirs = %d", panel->Dirs != NULL ? panel->Dirs->Count : -1);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "Files = %d", panel->Files != NULL ? panel->Files->Count : -1);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "SelectedCount = %d", panel->SelectedCount);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "FileBasedCompression = %d", panel->FileBasedCompression);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "FileBasedEncryption = %d", panel->FileBasedEncryption);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "SupportACLS = %d", panel->SupportACLS);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "Network Drive = %d", panel->GetNetworkDrive());
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "DeviceNotification = 0x%p", panel->DeviceNotification);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "UseSystemIcons = %d", panel->UseSystemIcons);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "UseThumbnails = %d", panel->UseThumbnails);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "SuppressAutoRefresh = %d", panel->GetSuppressAutoRefresh());
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "MonitorChanges = %d", panel->GetMonitorChanges());
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "IconCacheValid = %d", panel->IconCacheValid);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "AutomaticRefresh = %d", panel->AutomaticRefresh);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "ViewMode = %d", panel->GetViewMode());
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "SortType = %d", panel->SortType);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "ReverseSort = %d", panel->ReverseSort);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "SortedWithRegSet = %d", panel->SortedWithRegSet);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "SortedWithDetectNum = %d", panel->SortedWithDetectNum);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "NextFocusName = %ls", panel->NextFocusNameW.c_str());
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "FocusFirstNewItem = %d", panel->FocusFirstNewItem);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "HiddenDirsFilesReason = %u", panel->HiddenDirsFilesReason);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "HiddenDirsCount = %d", panel->HiddenDirsCount);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "HiddenFilesCount = %d", panel->HiddenFilesCount);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "FilterEnabled = %d", panel->FilterEnabled);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "QuickSearchMode = %d", panel->QuickSearchMode);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "FocusedIndex = %d", panel->FocusedIndex);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "FocusVisible = %d", panel->FocusVisible);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "TrackingSingleClick = %d", panel->TrackingSingleClick);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "UserWorkedOnThisPath = %d", panel->UserWorkedOnThisPath);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "StopThumbnailLoading = %d", panel->StopThumbnailLoading);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "EnumFileNamesSourceUID = %d", panel->EnumFileNamesSourceUID);
                    PrintLine(param, buf, TRUE);
                    break;
                }

                case ptZIPArchive:
                {
                    // GetZIPArchive/GetZIPPath are wide-only; %ls is required here.
                    sprintf(buf, "Archive = %ls", panel->GetZIPArchive());
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "ArcPath = %ls", panel->GetZIPPath());
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "Dirs = %d", panel->Dirs != NULL ? panel->Dirs->Count : -1);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "Files = %d", panel->Files != NULL ? panel->Files->Count : -1);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "SelectedCount = %d", panel->SelectedCount);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "AssocUsed = %d", panel->AssocUsed);
                    PrintLine(param, buf, TRUE);
                    break;
                }

                case ptPluginFS:
                {
                    sprintf(buf, "FSPath = %ls:", panel->GetPluginFS()->GetPluginFSName());
                    __try
                    {
                        if (!panel->GetPluginFS()->NotEmpty() ||
                            !AppendPluginFSPathA(panel->GetPluginFS(), buf,
                                                 _countof(buf)))
                        {
                            lstrcatA(buf, !panel->GetPluginFS()->NotEmpty() ? "(empty)" : "(error)");
                        }
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        lstrcatA(buf, "(exception)");
                    }
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "Plugin DLL is ");
                    __try
                    {
                        if (panel->GetPluginFS()->NotEmpty())
                        {
                            CPluginData* data = Plugins.GetPluginData(panel->GetPluginFS()->GetPluginInterfaceForFS()->GetInterface());
                            if (data != NULL)
                                sprintf(buf + lstrlenA(buf), "%ls v. %ls", data->DLLName.c_str(), data->Version.c_str());
                            else
                                lstrcatA(buf, "(error)");
                        }
                        else
                            lstrcatA(buf, "(empty)");
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        lstrcatA(buf, "(exception)");
                    }
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "Dirs = %d", panel->Dirs != NULL ? panel->Dirs->Count : -1);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "Files = %d", panel->Files != NULL ? panel->Files->Count : -1);
                    PrintLine(param, buf, TRUE);
                    sprintf(buf, "SelectedCount = %d", panel->SelectedCount);
                    PrintLine(param, buf, TRUE);
                    break;
                }

                default:
                {
                    sprintf(buf, "Unknown panel type: %d", panel->GetPanelType());
                    PrintLine(param, buf, TRUE);
                    break;
                }
                }
                PrintLine(param, "", FALSE);
                printEOL = FALSE;
            }
        }
        if (printEOL)
            PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    BOOL* isModuleLoaded = NULL;
    int globalModulesStoreCount = 0;
    __try
    {
        PrintLine(param, "Modules:", FALSE);
        globalModulesStoreCount = GlobalModulesStore.Count; // make a copy in case GlobalModulesStore.Count changes
        if (globalModulesStoreCount > 0)
            isModuleLoaded = (BOOL*)malloc(sizeof(BOOL) * globalModulesStoreCount);
        if (isModuleLoaded != NULL)
            memset(isModuleLoaded, 0, sizeof(BOOL) * globalModulesStoreCount);
        int foundIndex;

        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
        if (snap != (HANDLE)-1)
        {
            // tlhelp32.h has no explicit-A form for this API: MODULEENTRY32/
            // Module32First/Module32Next ARE the narrow names (Module32FirstW/MODULEENTRY32W
            // are the only suffixed forms; UNICODE #defines the bare names away to those). No
            // safe shortcut exists here - genuine P4/Task-30 scope, same shape as
            // SE_RESTORE_NAME's lack of a wide form in reverse.
            static MODULEENTRY32 module;
            module.dwSize = sizeof(module);
            if (Module32First(snap, &module))
            {
                do
                {
                    if ((int)module.dwSize >= ((char*)(&(module.szExePath)) - (char*)(&(module.dwSize))))
                    {
                        static char modulePath[sizeof(module.szExePath) * 2 + 1];
                        if (module.dwSize == sizeof(module))
                        {
                            // module.szExePath/szModule are genuinely wchar_t-generic
                            // (no explicit-A form exists on MODULEENTRY32 - see the comment
                            // above); modulePath/nameBuf feed this function's narrow-only crash-
                            // report text (buf, sprintf below). PrintBugReport has its own __try,
                            // so this goes through CopyModuleTextNarrow (an ordinary function)
                            // rather than a local std::string here - see its definition and
                            // comment near AddNewlyLoadedModulesToGlobalModulesStore.
                            CopyModuleTextNarrow(modulePath, _countof(modulePath), module.szExePath);
                        }
                        else
                            lstrcpyA(modulePath, "(unknown)");
                        CopyModuleTextNarrow(nameBuf, _countof(nameBuf), module.szModule);
                        static char ver[100];
                        GetModuleVersion((HINSTANCE)module.modBaseAddr, ver, 100);
                        _snprintf_s(buf, _countof(buf), _TRUNCATE,
                                    "0x%p (size: 0x%X) (ver: %s): %s (%s)",
                                    module.modBaseAddr, module.modBaseSize, ver, nameBuf,
                                    modulePath);
                        if (isModuleLoaded != NULL && FindInGlobalModulesStore(buf, foundIndex) &&
                            foundIndex < globalModulesStoreCount)
                        {
                            isModuleLoaded[foundIndex] = TRUE;
                        }
                        if (Exception != NULL)
                            ModulesInfo.Add(modulePath, module.modBaseAddr, module.modBaseSize);
                    }
                    else
                        sprintf(buf, "unknown module (dwSize = %u)", module.dwSize);
                    PrintLine(param, buf, TRUE);
                    module.dwSize = sizeof(module);
                } while (Module32Next(snap, &module));
            }
            NOHANDLES(CloseHandle(snap));
        }
        PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        PrintLine(param, "Unloaded Modules:", FALSE);
        BOOL unloadedModuleExist = FALSE;
        int i;
        for (i = 0; i < GlobalModulesStore.Count; i++)
        {
            if (isModuleLoaded == NULL || i >= globalModulesStoreCount || !isModuleLoaded[i])
            {
                if (GlobalModulesStore[i] != NULL)
                {
                    sprintf(buf, "(%u ms) %s", GlobalModulesListTimeStore[i] - SalamanderStartTime, GlobalModulesStore[i]);
                    PrintLine(param, buf, TRUE);
                    unloadedModuleExist = TRUE;
                }
                else
                    PrintLine(param, "(null)", TRUE);
            }
        }
        if (isModuleLoaded != NULL)
            free(isModuleLoaded);
        if (!unloadedModuleExist)
        {
            sprintf(buf, "NONE");
            PrintLine(param, buf, TRUE);
        }
        PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        PrintLine(param, "Plugins:", FALSE);
        int pluginIndex = 0;
        CPluginData* plugin;
        while ((plugin = Plugins.Get(pluginIndex++)) != NULL)
        {
            // CPluginData::Name/DLLName/Version are all std::wstring; %ls is required here.
            sprintf(buf, "%ls: %ls v. %ls", plugin->Name.c_str(), plugin->DLLName.c_str(), plugin->Version.c_str());
            if (plugin->DLL != NULL)
                sprintf(buf + strlen(buf), ", loaded (0x%p)", plugin->DLL);
            else
                sprintf(buf + strlen(buf), ", not loaded");
            PrintLine(param, buf, TRUE);
        }
        PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        // output current times
        static SYSTEMTIME st;
        GetLocalTime(&st);
        sprintf(buf, "Bug Report Time: %u.%u.%u - %02u:%02u:%02u",
                st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);
        PrintLine(param, buf, FALSE);
        DWORD ti = SalamanderExceptionTime - SalamanderStartTime;
        sprintf(buf, "Salamander Started Before: %u:%02u:%02u:%02u.%03u",
                ti / (24 * 60 * 60 * 1000), (ti / (60 * 60 * 1000)) % 24,
                (ti / (60 * 1000)) % 60, (ti / (1000)) % 60, ti % 1000);
        PrintLine(param, buf, FALSE);
        ti = GetTickCount();
        sprintf(buf, "System Started Before: %u:%02u:%02u:%02u",
                ti / (24 * 60 * 60 * 1000), (ti / (60 * 60 * 1000)) % 24,
                (ti / (60 * 1000)) % 60, (ti / (1000)) % 60);
        PrintLine(param, buf, FALSE);
        PrintLine(param, "", FALSE);

        lstrcpyA(buf, "Module Name: ");
        GetModuleFileNameA(HInstance, buf + strlen(buf), 1000);
        PrintLine(param, buf, FALSE);

        const char* cmdline = GetCommandLineA();
        _snprintf_s(buf, _TRUNCATE, "Command Line: %s", cmdline);
        PrintLine(param, buf, FALSE);

        sprintf(buf, "Built: %s", __DATE__ ", " __TIME__);
        PrintLine(param, buf, FALSE);

        LCID lcid = GetThreadLocale();

        lstrcpyA(buf, "Country: ");
        GetLocaleInfoA(lcid, LOCALE_ICOUNTRY, buf + strlen(buf), 100);
        lstrcatA(buf, " (");
        GetLocaleInfoA(lcid, LOCALE_SENGCOUNTRY, buf + strlen(buf), 100);
        lstrcatA(buf, ")");
        PrintLine(param, buf, FALSE);

        lstrcpyA(buf, "Language: ");
        GetLocaleInfoA(lcid, LOCALE_ILANGUAGE, buf + strlen(buf), 100);
        lstrcatA(buf, " (");
        GetLocaleInfoA(lcid, LOCALE_SENGLANGUAGE, buf + strlen(buf), 100);
        lstrcatA(buf, ")");
        PrintLine(param, buf, FALSE);

        lstrcpyA(buf, "Code Page: ");
        GetLocaleInfoA(lcid, LOCALE_IDEFAULTANSICODEPAGE, buf + strlen(buf), 100);
        PrintLine(param, buf, FALSE);

        PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        PrintSystemVersion(PrintLine, param, buf, avbuf, _countof(avbuf));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    HKEY hKey;
    IRegistry* registry = GetBugReportRegistry();
    __try
    {
        if (registry->OpenKeyRead(HKEY_LOCAL_MACHINE, SAL_REG_KEY_MICROSOFT_IE_W, hKey).success)
        {
            static char iver[50];
            static char build[50];
            static char version[50];
            iver[0] = 0;
            build[0] = 0;
            version[0] = 0;
            CopyRegistryTextNarrow(registry, hKey, SAL_REG_VALUE_IE_IVER_W, iver, _countof(iver));
            CopyRegistryTextNarrow(registry, hKey, SAL_REG_VALUE_BUILD_W, build, _countof(build));
            CopyRegistryTextNarrow(registry, hKey, SAL_REG_VALUE_VERSION_W, version, _countof(version));

            if (iver[0] != 0 || build[0] != 0 || version[0] != 0)
            {
                lstrcpyA(buf, "IE ");
                if (version[0] != 0)
                {
                    lstrcatA(buf, "Version: ");
                    lstrcatA(buf, version);
                    lstrcatA(buf, " ");
                }
                if (build[0] != 0)
                {
                    lstrcatA(buf, "Build: ");
                    lstrcatA(buf, build);
                    lstrcatA(buf, " ");
                }
                if (iver[0] != 0)
                {
                    lstrcatA(buf, "IVer: ");
                    lstrcatA(buf, iver);
                }
                PrintLine(param, buf, TRUE);
            }

            registry->CloseKey(hKey);
        }

        sprintf(buf, "COMCTL32.DLL Version: %u.%u", CCVerMajor, CCVerMinor);
        PrintLine(param, buf, TRUE);

        if (registry->OpenKeyRead(HKEY_LOCAL_MACHINE,
                                  SAL_REG_KEY_WINDOWS_NT_CURRENT_VERSION_W, hKey).success)
        {
            char myBuff[100];

            if (CopyRegistryTextNarrow(registry, hKey, SAL_REG_VALUE_WINDOWS_PRODUCT_NAME_W, myBuff, _countof(myBuff)))
            {
                sprintf(buf, "ProductName (from registry): %s", myBuff);
                PrintLine(param, buf, TRUE);
            }
            if (CopyRegistryTextNarrow(registry, hKey, SAL_REG_VALUE_WINDOWS_CURRENT_VERSION_W, myBuff, _countof(myBuff)))
            {
                sprintf(buf, "CurrentVersion (from registry): %s", myBuff);
                PrintLine(param, buf, TRUE);
            }
            registry->CloseKey(hKey);
        }

        // try to locate the main LiteStep window
#define LM_GETREVID 9265
#define LS_BUFSIZE 2048
        // narrow literals - explicit FindWindowA.
        HWND hLiteStepWnd = FindWindowA("TApplication", "LiteStep");
        if (hLiteStepWnd != NULL)
        {
            static char buffer[LS_BUFSIZE];
            buffer[0] = 0;

            lstrcpyA(buf, "LiteStep");

            int msgflags = 0;
            msgflags |= (LS_BUFSIZE << 4);

            // attempt to retrieve its version
            if (SendMessage(hLiteStepWnd, LM_GETREVID, msgflags, (LPARAM)buffer))
            {
                // LiteStep prints many lines here - keep only the first one
                char* p = buffer;
                while (*p != 0 && *p != '\n')
                    p++;
                *p = 0;
                lstrcatA(buf, " ");
                lstrcatA(buf, buffer);
                // Do whatever
            }
            else
            {
                // it failed - at least report it is running
                lstrcatA(buf, " is present");
            }
            PrintLine(param, buf, TRUE);
        }

        PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        // OTHER INFORMATION
        PrintLine(param, "Other Information:", FALSE);

        strcpy(buf, "User is Admin: ");
        strcat(buf, IsUserAdmin() ? "yes" : "no");
        PrintLine(param, buf, TRUE);

        DWORD integrityLevel;
        if (GetProcessIntegrityLevel(&integrityLevel))
        {
            sprintf(buf, "Integrity Level: 0x%X", integrityLevel);
            PrintLine(param, buf, TRUE);
        }

        strcpy(buf, "Terminal Services: ");
        strcat(buf, IsRemoteSession() ? "running in a remote session" : "running on the console");
        PrintLine(param, buf, TRUE);

        PrintLine(param, "", FALSE);

        // HARDWARE INFORMATION
        static SYSTEM_INFO si;
        GetSystemInfo(&si);
        PrintLine(param, "Hardware Information:", FALSE);
        sprintf(buf, "OEM ID: 0x%X", si.dwOemId);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "Number of Processors: %u", si.dwNumberOfProcessors);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "Processor Type: %u", si.dwProcessorType);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "Processor Architecture: %u", si.wProcessorArchitecture);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "Processor Level: %u", si.wProcessorLevel);
        PrintLine(param, buf, TRUE);
        if (registry->OpenKeyRead(HKEY_LOCAL_MACHINE,
                                  SAL_REG_KEY_HARDWARE_CPU0_W, hKey).success)
        {
            static char processorName[200];
            static char vendorName[200];
            DWORD mhz;

            if (!CopyRegistryTextNarrow(registry, hKey, SAL_REG_VALUE_PROCESSOR_NAME_STRING_W, processorName, _countof(processorName)))
                if (!CopyRegistryTextNarrow(registry, hKey, SAL_REG_VALUE_IDENTIFIER_W, processorName, _countof(processorName))) // probably unnecessary on W2K+
                    processorName[0] = 0;
            if (!CopyRegistryTextNarrow(registry, hKey, SAL_REG_VALUE_VENDOR_IDENTIFIER_W, vendorName, _countof(vendorName)))
                vendorName[0] = 0;
            if (!registry->GetDWord(hKey, SAL_REG_VALUE_PROCESSOR_SPEED_MHZ_W, mhz).success)
            {
                if (!GetProcessorSpeed(&mhz))
                    mhz = 0;
            }
            if (vendorName[0] != 0)
                sprintf(buf, "Processor Vendor Name: %s", vendorName);
            PrintLine(param, buf, TRUE);
            if (processorName[0] != 0)
            {
                char* ss = processorName;
                while (*ss == ' ')
                    ss++; // Intel adds spaces before the processor name so it looks nicer in the Control Panel System window
                sprintf(buf, "Processor Name: %s", ss);
                PrintLine(param, buf, TRUE);
            }
            if (mhz != 0)
                sprintf(buf, "Processor Speed: ~%u MHz", mhz);
            PrintLine(param, buf, TRUE);

            registry->CloseKey(hKey);
        }

        sprintf(buf, "Page size: %u", si.dwPageSize);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "Minimum app address: 0x%p", si.lpMinimumApplicationAddress);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "Maximum app address: 0x%p", si.lpMaximumApplicationAddress);
        PrintLine(param, buf, TRUE);
        sprintf(buf, "Active processor mask: 0x%IX", si.dwActiveProcessorMask);
        PrintLine(param, buf, TRUE);
        BOOL fResult = GetSystemMetrics(SM_MOUSEPRESENT);
        if (fResult)
            sprintf(buf, "Mouse is installed");
        else
            sprintf(buf, "No mouse installed");
        PrintLine(param, buf, TRUE);

        static MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms) != 0)
        {
            sprintf(buf, "Total Physical Memory: %I64u KB", ms.ullTotalPhys / 1024);
            PrintLine(param, buf, TRUE);
            sprintf(buf, "Free Physical Memory: %I64u KB", ms.ullAvailPhys / 1024);
            PrintLine(param, buf, TRUE);
            sprintf(buf, "Total Virtual Memory: %I64u KB", ms.ullTotalVirtual / 1024);
            PrintLine(param, buf, TRUE);
            sprintf(buf, "Free Virtual Memory: %I64u KB", ms.ullAvailVirtual / 1024);
            PrintLine(param, buf, TRUE);
        }

        if (registry->OpenKeyRead(HKEY_LOCAL_MACHINE,
                                  SAL_REG_KEY_HARDWARE_DESCRIPTION_SYSTEM_W, hKey).success)
        {
            static char bios[200];

            bios[0] = 0;
            CopyRegistryTextNarrow(registry, hKey, SAL_REG_VALUE_SYSTEM_BIOS_VERSION_W, bios, _countof(bios));
            if (bios[0] != 0)
            {
                sprintf(buf, "BIOS Version: %s", bios);
                PrintLine(param, buf, TRUE);
            }

            bios[0] = 0;
            CopyRegistryTextNarrow(registry, hKey, SAL_REG_VALUE_SYSTEM_BIOS_DATE_W, bios, _countof(bios));
            if (bios[0] != 0)
            {
                sprintf(buf, "BIOS Date: %s", bios);
                PrintLine(param, buf, TRUE);
            }
            registry->CloseKey(hKey);
        }

        MonitorEnumProcData enumData;
        enumData.Index = 1;
        enumData.PrintLine = PrintLine;
        enumData.Param = param;
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&enumData);
        PrintLine(param, "", FALSE);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        if (Exception != NULL)
        {
            // retrieve each thread's CONTEXT
            // and display EIP + the call stack
            PrintLine(param, "Stack Back Trace:", FALSE);
            BOOL firstTime = TRUE;
            int i;
            for (i = 0; i < CCallStack::CallStacks.Count; i++)
            {
                // !!! accesses to CCallStack::CallStacks array should be protected by a critical section
                // otherwise we risk touching non-existing memory
                CCallStack* stack;
                stack = CCallStack::CallStacks[i];

                HANDLE hThread = NULL;
                if (stack != NULL)
                {
                    __try // a rather dumb safeguard against changes to CCallStack::CallStacks; also guard against GetThreadHandleFromID function
                    {
                        hThread = stack->ThreadHandle;
                        if (hThread == NULL)
                        {
                            // if hThread is NULL try to assign it using GetThreadHandleFromID
                            hThread = GetThreadHandleFromID(stack->ThreadID, FALSE);
                        }
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        PrintLine(param, "(GetThreadHandleFromID exception)", TRUE);
                        hThread = NULL;
                    }
                }
                if (stack != NULL && hThread != NULL)
                {
                    BOOL threadWithException = (Exception != NULL && Exception->ContextRecord != NULL && stack->ThreadID == ThreadID);
                    if (stack->ThreadID != GetCurrentThreadId() || threadWithException)
                    {
                        // if this isn't our thread, suspend it so we can retrieve context
                        if (threadWithException || SuspendThread(hThread) != -1)
                        {
                            if (!firstTime)
                                PrintLine(param, "----", TRUE);
                            else
                                firstTime = FALSE;
                            sprintf(buf, "Thread ID: 0x%X", stack->ThreadID);
                            if (ThreadID == stack->ThreadID)
                                strcat(buf, " (Thread with Exception)");
                            PrintLine(param, buf, TRUE);

                            //            if (knownThreads.IsGood())
                            //              knownThreads.Add(stack->ThreadID);

                            // ===============================================
                            // === Argument Passing and Naming Conventions ===
                            // ===============================================
                            //
                            // name        Stack cleanup  Parameter passing                             name decoration                        int func(int a, double b)
                            // -----------------------------------------------------------------------------------------------------------------------------------------
                            // __cdecl     Caller         Pushes parameters on the stack, in reverse    prefix: "_"                            _func
                            //                            order (right to left)
                            //                            (ebp+0x8 is param0, ebp+0xC is param1 ...)
                            //
                            // __stdcall   Callee         Pushes parameters on the stack, in reverse    prefix: "_"; suffix: "@" followed      _func@12
                            //                            order (right to left)                         by the number of bytes (in decimal)
                            //                            (ebp+0x8 is param0, ebp+0xC is param1 ...)    in the argument list.
                            //
                            // __fastcall  Callee         The first two DWORD or smaller arguments are  prefix: "@"; suffix: "@" followed by   @func@12
                            //                            passed in ecx and edx registers; all other    the number of bytes (in decimal) in
                            //                            arguments are passed on stack,                the argument list.
                            //                            reverse order / right to left
                            //                            (ebp+0x8 is param2, ebp+0xC is param3 ...)
                            //
                            // thiscall    Callee         Pushed on stack; this pointer stored in ECX   (C++ mangling)
                            //
                            //
                            // ==========================================================
                            // === Stack frame layout (no frame pointer optimization) ===
                            // ==========================================================
                            //
                            // this function's ebp
                            // points here ------------\
              //     ebp-0x8   ebp-0x4   ebp+0x0   ebp+0x4   ebp+0x8   ebp+0xC
                            //                         caller's  return
                            //     local1    local0    ebp       addr      param0    param1
                            // ... 00000000  00000000  00000000  00000000  00000000  00000000 ...

                            // retrieve the thread context
                            CONTEXT ctx;
                            ZeroMemory(&ctx, sizeof(ctx));
                            if (threadWithException)
                                memcpy(&ctx, Exception->ContextRecord, sizeof(ctx));
                            else
#ifdef _M_ARM64
                                ctx.ContextFlags = CONTEXT_INTEGER | CONTEXT_CONTROL;
#else
                                ctx.ContextFlags = CONTEXT_SEGMENTS | CONTEXT_INTEGER | CONTEXT_CONTROL;
#endif
                            if (threadWithException || GetThreadContext(hThread, &ctx))
                            {
#ifdef _WIN64
                                BOOL firstPc = TRUE;
                                // Unwind until IP is 0, sp is at the stack top, and callee IP is in kernel32.
                                while (TRUE)
                                {
                                    DWORD64 controlPc = CONTEXT_IP(ctx);

                                    CModuleInfo* modInfo = ModulesInfo.Find((void*)controlPc);
                                    const char* name;
                                    if (modInfo == NULL)
                                    {
                                        name = FindModuleName(nameBuf, _countof(nameBuf),
                                                              (void*)controlPc, TRUE);
                                        if (*name == 0)
                                            name = "(unknown module)";
                                    }
                                    else
                                        name = modInfo->Name;

                                    if (firstPc)
                                    {
                                        sprintf(buf, CONTEXT_IP_NAME " = 0x%p %s", (void*)controlPc, name);
                                        firstPc = FALSE;
                                    }
                                    else
                                        sprintf(buf, "0x%p %s", (void*)controlPc, name);
                                    PrintLine(param, buf, TRUE);

                                    DWORD64 ImageBase;
                                    RUNTIME_FUNCTION* pFunctionEntry = RtlLookupFunctionEntry(
                                        controlPc,
                                        &ImageBase,
                                        NULL);
                                    if (pFunctionEntry)
                                    {
                                        PVOID HandlerData;
                                        DWORD64 EstablisherFrame;

                                        RtlVirtualUnwind(
                                            0,
                                            ImageBase,
                                            controlPc,
                                            pFunctionEntry,
                                            &ctx,
                                            &HandlerData,
                                            &EstablisherFrame,
                                            NULL);

                                        DWORD64 mewControlPc = CONTEXT_IP(ctx);
                                        if (mewControlPc == 0)
                                            break;
                                    }
                                    else
                                    {
                                        // Nested functions that do not use any stack space or nonvolatile
                                        // registers are not required to have unwind info (ex.
                                        // USER32!ZwUserCreateWindowEx).
                                        CONTEXT_IP(ctx) = *(DWORD64*)(CONTEXT_SP(ctx));
                                        CONTEXT_SP(ctx) += sizeof(DWORD64);
                                    }
                                }
#else
                                CModuleInfo* modInfo = ModulesInfo.Find((void*)ctx.Eip);
                                const char* name;
                                if (modInfo == NULL)
                                {
                                    name = FindModuleName(nameBuf, _countof(nameBuf),
                                                          (void*)ctx.Eip, TRUE);
                                    if (*name == 0)
                                        name = "(unknown module)";
                                }
                                else
                                    name = modInfo->Name;
                                sprintf(buf, "EIP = 0x%p %s", (void*)ctx.Eip, name);
                                PrintLine(param, buf, TRUE);
                                // esp may point somewhere invalid - prepare for it
                                __try
                                {
                                    // *(ebp) is the frame pointer of the calling function
                                    // *(ebp + 4) is the return adress (the place in the calling function where
                                    //            execution will resume after we return from this function)
                                    DWORD* ebp = (DWORD*)ctx.Ebp;

                                    int j;
                                    for (j = 0; j < 50; j++)
                                    {
                                        DWORD retAddr = ebp[1]; // return address *(ebp + 4)
                                        if (retAddr == 0)
                                            break;
                                        modInfo = ModulesInfo.Find((void*)retAddr);
                                        if (modInfo == NULL)
                                        {
                                            name = FindModuleName(nameBuf, _countof(nameBuf),
                                                                  (void*)retAddr, TRUE);
                                            if (*name == 0)
                                                name = "(unknown module)";
                                        }
                                        else
                                            name = modInfo->Name;
                                        sprintf(buf, "0x%p %s", (void*)retAddr, name);

                                        PrintLine(param, buf, TRUE);

                                        // move to the stack frame of the caller function (*ebp)
                                        ebp = (DWORD*)ebp[0];
                                    }
                                }
                                __except (EXCEPTION_EXECUTE_HANDLER)
                                {
                                    PrintLine(param, "(exception)", TRUE);
                                }
#endif // _WIN64
                            }
                            if (stack->ThreadID != GetCurrentThreadId())
                                ResumeThread(hThread);
                        }
                    }
                }
            }

            if (threadNotFound)
            {
                HANDLE hThread = NULL;
                __try
                {
                    hThread = GetThreadHandleFromID(ThreadID, FALSE);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    PrintLine(param, "(GetThreadHandleFromID exception)", TRUE);
                    hThread = NULL;
                }
                if (hThread != NULL)
                {
                    if (!firstTime)
                        PrintLine(param, "----", TRUE);
                    else
                        firstTime = FALSE;
                    sprintf(buf, "Thread ID: 0x%X", ThreadID);
                    strcat(buf, " (Thread with Exception)");
                    PrintLine(param, buf, TRUE);

                    // retrieve the thread context
                    CONTEXT ctx;
                    ZeroMemory(&ctx, sizeof(ctx));
                    memcpy(&ctx, Exception->ContextRecord, sizeof(ctx));
                    if (TRUE)
                    {
#ifdef _WIN64
                        BOOL firstPc = TRUE;
                        // Unwind until IP is 0, sp is at the stack top, and callee IP is in kernel32.
                        while (TRUE)
                        {
                            DWORD64 controlPc = CONTEXT_IP(ctx);

                            CModuleInfo* modInfo = ModulesInfo.Find((void*)controlPc);
                            const char* name;
                            if (modInfo == NULL)
                            {
                                name = FindModuleName(nameBuf, _countof(nameBuf),
                                                      (void*)controlPc, TRUE);
                                if (*name == 0)
                                    name = "(unknown module)";
                            }
                            else
                                name = modInfo->Name;

                            if (firstPc)
                            {
                                sprintf(buf, CONTEXT_IP_NAME " = 0x%p %s", (void*)controlPc, name);
                                firstPc = FALSE;
                            }
                            else
                                sprintf(buf, "0x%p %s", (void*)controlPc, name);
                            PrintLine(param, buf, TRUE);

                            DWORD64 ImageBase;
                            RUNTIME_FUNCTION* pFunctionEntry = RtlLookupFunctionEntry(
                                controlPc,
                                &ImageBase,
                                NULL);
                            if (pFunctionEntry)
                            {
                                PVOID HandlerData;
                                DWORD64 EstablisherFrame;

                                RtlVirtualUnwind(
                                    0,
                                    ImageBase,
                                    controlPc,
                                    pFunctionEntry,
                                    &ctx,
                                    &HandlerData,
                                    &EstablisherFrame,
                                    NULL);

                                DWORD64 mewControlPc = CONTEXT_IP(ctx);
                                if (mewControlPc == 0)
                                    break;
                            }
                            else
                            {
                                // Nested functions that do not use any stack space or nonvolatile
                                // registers are not required to have unwind info (ex.
                                // USER32!ZwUserCreateWindowEx).
                                CONTEXT_IP(ctx) = *(DWORD64*)(CONTEXT_SP(ctx));
                                CONTEXT_SP(ctx) += sizeof(DWORD64);
                            }
                        }
#else
                        CModuleInfo* modInfo = ModulesInfo.Find((void*)ctx.Eip);
                        const char* name;
                        if (modInfo == NULL)
                        {
                            name = FindModuleName(nameBuf, _countof(nameBuf),
                                                  (void*)ctx.Eip, TRUE);
                            if (*name == 0)
                                name = "(unknown module)";
                        }
                        else
                            name = modInfo->Name;
                        sprintf(buf, "EIP = 0x%p %s", (void*)ctx.Eip, name);
                        PrintLine(param, buf, TRUE);
                        // esp may point somewhere invalid - prepare for it
                        __try
                        {
                            // *(ebp) is the frame pointer of the calling function
                            // *(ebp + 4) is the return adress (the place in the calling function where
                            //            execution will resume after we return from this function)
                            DWORD* ebp = (DWORD*)ctx.Ebp;

                            int j;
                            for (j = 0; j < 50; j++)
                            {
                                DWORD retAddr = ebp[1]; // return address *(ebp + 4)
                                if (retAddr == 0)
                                    break;
                                modInfo = ModulesInfo.Find((void*)retAddr);
                                if (modInfo == NULL)
                                {
                                    name = FindModuleName(nameBuf, _countof(nameBuf),
                                                          (void*)retAddr, TRUE);
                                    if (*name == 0)
                                        name = "(unknown module)";
                                }
                                else
                                    name = modInfo->Name;
                                sprintf(buf, "0x%p %s", (void*)retAddr, name);

                                PrintLine(param, buf, TRUE);

                                // move to the stack frame of the caller function (*ebp)
                                ebp = (DWORD*)ebp[0];
                            }
                        }
                        __except (EXCEPTION_EXECUTE_HANDLER)
                        {
                            PrintLine(param, "(exception)", TRUE);
                        }
#endif // _WIN64
                    }
                }
            }
            PrintLine(param, "", FALSE);

            /*
  // leaving thread enumeration unfinished for now because Petr suggested showing
  // the back trace for the thread where the crash occurred (we know its ID)

      // the knownThreads array contains a list of displayed threads;
      // now we attempt to find those not yet shown
      if (knownThreads.IsGood())
      {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != (HANDLE)-1)
        {
          static THREADENTRY32 thread;
          thread.dwSize = sizeof(thread);
          if (Thread32First(snap, &thread))
          {
            do
            {
              if (thread.th32OwnerProcessID ????)
              int a= 5;
            } while (Thread32Next(snap, &thread));
          }
          else
            DWORD err = GetLastError();
          NOHANDLES(CloseHandle(snap));
        }
      }
  */
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    __try
    {
        // DRIVES information
        if (TRUE)
        {
            PrintLine(param, "Drives:", FALSE);
            // available drives
            // lowest bit corresponds to 'A', the second bit to 'B', ...
            DWORD netDrives; // bitfield of network drives
            static BYTE bufForGetNetworkDrives[10000];
            __try
            {
                GetNetworkDrivesBody(netDrives, NULL, bufForGetNetworkDrives,
                                     sizeof(bufForGetNetworkDrives));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                PrintLine(param, "(exception in GetNetworkDrives)", TRUE);
                netDrives = 0;
            }
            DWORD mask = GetLogicalDrives();
            int i = 1;
            char drive = 'A';
            wchar_t rootW[4] = L" :\\";
            while (i != 0)
            {
                if ((mask & i) || (netDrives & i)) // drive is accessible
                {
                    BOOL accessible = (mask & i) != 0;
                    rootW[0] = static_cast<wchar_t>(drive);
                    UINT driveType = MyGetDriveTypeW(rootW);
                    sprintf(buf, "%c:\\", drive);
                    if (driveType <= 6)
                    {
                        const char* drvTypeStrp[7] = {"unknown", "no root dir", "removable", "fixed", "remote", "cdrom", "ramdisk"};
                        sprintf(buf + strlen(buf), "  [%s]", drvTypeStrp[driveType]);
                    }
                    PrintLine(param, buf, TRUE);

                    BOOL getMoreInfo = TRUE;
                    if (driveType == 2)
                    {
                        // removable
                        int drv = drive - 'A' + 1;
                        DWORD medium = GetDriveFormFactor(drv);
                        if (medium == 350 || medium == 525 || medium == 800 || medium == 1)
                            getMoreInfo = FALSE; // avoid unnecesary floppy disk operations during MyGetVolumeInformation()
                    }

                    if (getMoreInfo)
                    {
                        //---  GetVolumeInformation
                        DWORD volumeSerialNumber;
                        DWORD maximumComponentLength;
                        DWORD fileSystemFlags;
                        BOOL err = !GetVolumeReportText(
                            rootW, volumeNameA, _countof(volumeNameA),
                            fileSystemNameA, _countof(fileSystemNameA),
                            &volumeSerialNumber, &maximumComponentLength,
                            &fileSystemFlags);
                        if (!err)
                        {
                            buf[0] = 0;
                            if (volumeNameA[0] != 0)
                                sprintf(buf + strlen(buf), "  LB: %s", volumeNameA);
                            sprintf(buf + strlen(buf), "  SN: %04X-%04X", HIWORD(volumeSerialNumber), LOWORD(volumeSerialNumber));
                            sprintf(buf + strlen(buf), "  FL: 0x%08X", fileSystemFlags);
                            sprintf(buf + strlen(buf), "  FS: %s", fileSystemNameA);
                            sprintf(buf + strlen(buf), "  LN: %s", maximumComponentLength > 100 ? "yes" : "no");

                            PrintLine(param, buf, TRUE);
                        }

                        //---  GetDiskFreeSpace
                        DWORD sectorsPerCluster;
                        DWORD bytesPerSector;
                        DWORD numberOfFreeClusters;
                        DWORD totalNumberOfClusters;
                        err = (MyGetDiskFreeSpaceW(rootW, &sectorsPerCluster, &bytesPerSector,
                                                   &numberOfFreeClusters, &totalNumberOfClusters) == 0);

                        CQuadWord diskTotalBytes = CQuadWord(-1, -1), diskFreeBytes;
                        ULARGE_INTEGER availBytes, totalBytes, freeBytes;
                        if (GetDiskFreeSpaceExW(rootW, &availBytes, &totalBytes, &freeBytes))
                        {
                            diskTotalBytes.Value = (unsigned __int64)totalBytes.QuadPart;
                            diskFreeBytes.Value = (unsigned __int64)freeBytes.QuadPart;
                        }
                        if (diskTotalBytes == CQuadWord(-1, -1) && !err)
                        {
                            diskTotalBytes = CQuadWord(bytesPerSector, 0) * CQuadWord(sectorsPerCluster, 0) *
                                             CQuadWord(totalNumberOfClusters, 0);
                            diskFreeBytes = CQuadWord(bytesPerSector, 0) * CQuadWord(sectorsPerCluster, 0) *
                                            CQuadWord(numberOfFreeClusters, 0);
                        }
                        //---  GetDiskFreeSpace - display
                        if (!err)
                        {
                            strcpy(buf, "  BytePerSec: ");
                            AppendNumberA(buf, _countof(buf), CQuadWord(bytesPerSector, 0));
                            strcat(buf, "  SecPerClus: ");
                            AppendNumberA(buf, _countof(buf), CQuadWord(sectorsPerCluster, 0));
                            if (CQuadWord(bytesPerSector, 0) * CQuadWord(sectorsPerCluster, 0) != CQuadWord(0, 0))
                                strcat(buf, "  Clusters: ");
                            AppendNumberA(buf, _countof(buf), diskTotalBytes / (CQuadWord(bytesPerSector, 0) * CQuadWord(sectorsPerCluster, 0)));
                            PrintLine(param, buf, TRUE);
                        }
                        if (diskTotalBytes != CQuadWord(-1, -1))
                        {
                            // PrintDiskSize is wchar_t* now. The crash
                            // report is narrow by design and this runs on the crash path,
                            // so append through a fixed buffer - no allocation.
                            strcpy(buf, "  Capacity: ");
                            AppendDiskSizeA(buf, _countof(buf), diskTotalBytes);
                            strcat(buf, "  Free: ");
                            AppendDiskSizeA(buf, _countof(buf), diskFreeBytes);
                            diskTotalBytes -= diskFreeBytes;
                            strcat(buf, "  Used: ");
                            AppendDiskSizeA(buf, _countof(buf), diskTotalBytes);
                            PrintLine(param, buf, TRUE);
                        }
                    }
                    /*
          int driveType;
          if (mask & i)
            driveType = GetDriveType(root);
          else
            driveType = DRIVE_REMOTE;
          */
                    if (GetDosDeviceReportText(drive - 'A', deviceNameA, _countof(deviceNameA)))
                    {
                        sprintf(buf, "  Device: %s", deviceNameA);
                        PrintLine(param, buf, TRUE);
                    }
                }
                drive++;
                i <<= 1;
            }
            PrintLine(param, "", FALSE);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        PrintLine(param, buf, TRUE);
        PrintLine(param, "", FALSE);
    }

    PrintLine(param, "End.", FALSE); // for detecting a complete bug report

    if (Exception != NULL)
        ModulesInfo.Clean(); // not required, but free memory
}

void AddUniqueToGlobalModulesStore(const char* str)
{
    if (GlobalModulesStore.Count == 0)
    {
        char* s = DupAnsiString(str);
        if (s != NULL)
        {
            GlobalModulesListTimeStore.Add(GetTickCount());
            if (!GlobalModulesListTimeStore.IsGood())
                GlobalModulesListTimeStore.ResetState();
            else
            {
                GlobalModulesStore.Add(s);
                if (!GlobalModulesStore.IsGood())
                {
                    GlobalModulesListTimeStore.Delete(0);
                    if (!GlobalModulesListTimeStore.IsGood())
                        GlobalModulesListTimeStore.ResetState();
                    GlobalModulesStore.ResetState();
                }
            }
        }
        return;
    }

    int l = 0, r = GlobalModulesStore.Count - 1, m;
    while (1)
    {
        m = (l + r) / 2;
        int res = strcmp(str, GlobalModulesStore[m]);
        if (res == 0) // found
        {
            return;
        }
        else if (res < 0)
        {
            if (l == r || l > m - 1) // not found
            {
                char* s = DupAnsiString(str);
                if (s != NULL)
                {
                    GlobalModulesListTimeStore.Insert(m, GetTickCount());
                    if (!GlobalModulesListTimeStore.IsGood())
                        GlobalModulesListTimeStore.ResetState();
                    else
                    {
                        GlobalModulesStore.Insert(m, s);
                        if (!GlobalModulesStore.IsGood())
                        {
                            GlobalModulesListTimeStore.Delete(m);
                            if (!GlobalModulesListTimeStore.IsGood())
                                GlobalModulesListTimeStore.ResetState();
                            GlobalModulesStore.ResetState();
                        }
                    }
                }
                return;
            }
            r = m - 1;
        }
        else
        {
            if (l == r) // not found
            {
                char* s = DupAnsiString(str);
                if (s != NULL)
                {
                    GlobalModulesListTimeStore.Insert(m + 1, GetTickCount());
                    if (!GlobalModulesListTimeStore.IsGood())
                        GlobalModulesListTimeStore.ResetState();
                    else
                    {
                        GlobalModulesStore.Insert(m + 1, s);
                        if (!GlobalModulesStore.IsGood())
                        {
                            GlobalModulesListTimeStore.Delete(m + 1);
                            if (!GlobalModulesListTimeStore.IsGood())
                                GlobalModulesListTimeStore.ResetState();
                            GlobalModulesStore.ResetState();
                        }
                    }
                }
                return;
            }
            l = m + 1;
        }
    }
}

static BOOL CopyModuleTextNarrow(char* dest, size_t destSize, const wchar_t* wideSrc) noexcept
{
    return sally::diagnostic::CopyAcpLossy(wideSrc, dest, destSize) ? TRUE : FALSE;
}

void AddNewlyLoadedModulesToGlobalModulesStore()
{
    char buf[500];
    __try
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
        if (snap != (HANDLE)-1)
        {
            // no explicit-A form exists for this API - see the comment above the
            // other MODULEENTRY32 site; genuine P4/Task-30 scope.
            MODULEENTRY32 module;
            module.dwSize = sizeof(module);
            if (Module32First(snap, &module))
            {
                do
                {
                    if ((int)module.dwSize >= ((char*)(&(module.szExePath)) - (char*)(&(module.dwSize))))
                    {
                        // Raw final diagnostic scratch is required by the surrounding SEH block.
                        // Capacity derives from MODULEENTRY32W plus worst-case UTF-8 expansion.
                        char moduleName[sizeof(module.szModule) * 2 + 1];
                        char modulePath[sizeof(module.szExePath) * 2 + 1];
                        if (module.dwSize == sizeof(module))
                        {
                            // module.szExePath is genuinely wchar_t-generic (no
                            // explicit-A form exists); this function has its own __try (below),
                            // so the wide->narrow conversion goes through CopyModuleTextNarrow
                            // (an ordinary function) rather than a local std::string here.
                            CopyModuleTextNarrow(modulePath, _countof(modulePath), module.szExePath);
                        }
                        else
                            lstrcpyA(modulePath, "(unknown)");
                        CopyModuleTextNarrow(moduleName, _countof(moduleName), module.szModule);
                        char ver[100];
                        GetModuleVersion((HINSTANCE)module.modBaseAddr, ver, 100);
                        _snprintf_s(buf, _countof(buf), _TRUNCATE,
                                    "0x%p (size: 0x%X) (ver: %s): %s (%s)",
                                    module.modBaseAddr, module.modBaseSize, ver, moduleName,
                                    modulePath);
                    }
                    else
                        sprintf(buf, "unknown module (dwSize = %u)", module.dwSize);
                    AddUniqueToGlobalModulesStore(buf);
                    module.dwSize = sizeof(module);
                } while (Module32Next(snap, &module));
            }
            NOHANDLES(CloseHandle(snap));
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sprintf(buf, "some exception has occured...");
        AddUniqueToGlobalModulesStore(buf);
    }
}
