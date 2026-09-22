// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "..\undelete.rh2"

#include "miscstr.h"
#include "os.h"

// ***************************************************************************
//
//  Global variables
//
static BOOL OSVersionDetected = FALSE;
BOOL IsWindowsNT = FALSE;
BOOL IsWindows2000AndLater = FALSE;
BOOL IsWindows95 = FALSE;
BOOL IsWindows95OSR2AndLater = FALSE;
BOOL IsWindowsVistaAndLater = FALSE;

static HMODULE KernelModule = NULL;
static HINSTANCE NTShell32DLLInstance = NULL;

// ***************************************************************************
//
//  Static members
//
OS<wchar_t>::TFindFirstVolumeMountPoint OS<wchar_t>::F_FindFirstVolumeMountPoint = NULL;
OS<wchar_t>::TFindNextVolumeMountPoint OS<wchar_t>::F_FindNextVolumeMountPoint = NULL;
OS<wchar_t>::TFindVolumeMountPointClose OS<wchar_t>::F_FindVolumeMountPointClose = NULL;
OS<wchar_t>::TGetVolumeNameForVolumeMountPoint OS<wchar_t>::F_GetVolumeNameForVolumeMountPoint = NULL;
OS<wchar_t>::TGetDiskFreeSpaceEx OS<wchar_t>::F_GetDiskFreeSpaceEx = NULL;
OS<wchar_t>::TFindFirstVolume OS<wchar_t>::F_FindFirstVolume = NULL;
OS<wchar_t>::TFindNextVolume OS<wchar_t>::F_FindNextVolume = NULL;
OS<wchar_t>::TFindVolumeClose OS<wchar_t>::F_FindVolumeClose = NULL;
OS<wchar_t>::TGetVolumePathNamesForVolumeName OS<wchar_t>::F_GetVolumePathNamesForVolumeName = NULL;
OS<wchar_t>::TGetLogicalDriveStrings OS<wchar_t>::F_GetLogicalDriveStrings = NULL;
OS<wchar_t>::TSHGetFileInfo OS<wchar_t>::F_SHGetFileInfo = NULL;
HMODULE OS<wchar_t>::ImageResDLL = NULL;

// ***************************************************************************
//
//  Functions
//
BOOL OS_InitOSVersion()
{
    if (!OSVersionDetected)
    {
        OSVersionDetected = TRUE;

        // To run under W9x we must use the A-version of GetVersionEx().
        OSVERSIONINFOW osvi;
        ZeroMemory(&osvi, sizeof(osvi));
        osvi.dwOSVersionInfoSize = sizeof(osvi);
        if (!GetVersionExW(&osvi))
        {
            DWORD err = GetLastError();
            TRACE_E("GetVersionEx() failed, GetLastError()=" << err);
            return FALSE;
        }
        IsWindowsNT = (osvi.dwPlatformId == VER_PLATFORM_WIN32_NT);
        IsWindows2000AndLater = (osvi.dwPlatformId == VER_PLATFORM_WIN32_NT && osvi.dwMajorVersion >= 5);
        IsWindows95 = (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS &&
                       osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 0);
        IsWindows95OSR2AndLater = (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS &&
                                       (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 0 && LOWORD(osvi.dwBuildNumber) > 1080) || // W95OSR2
                                   (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion >= 1));                                           // W98 a WinME
        IsWindowsVistaAndLater = (osvi.dwPlatformId == VER_PLATFORM_WIN32_NT && osvi.dwMajorVersion >= 6);
    }
    return TRUE;
}

// ***************************************************************************
//
//  Explicitly instantiated template methods
//

// ****************************************************************************
//
// Native-wide versions. FindVolumeClose and
// FindVolumeMountPointClose take no A/W suffix - Win32 has one implementation
// operating on a HANDLE rather than a string.
//

BOOL OS<wchar_t>::OS_GetVolumeNameForVolumeMountPointExists()
{
    static BOOL functionsDetected = FALSE;

    if (!functionsDetected)
    {
        if (!KernelModule)
            KernelModule = GetModuleHandleW(L"kernel32.dll");

        if (KernelModule)
            F_GetVolumeNameForVolumeMountPoint = (TGetVolumeNameForVolumeMountPoint)GetProcAddress(KernelModule, "GetVolumeNameForVolumeMountPointW");

        functionsDetected = TRUE;
    }

    return (F_GetVolumeNameForVolumeMountPoint != NULL);
}

BOOL OS<wchar_t>::OS_VolumeEnumExists()
{
    static BOOL functionsDetected = FALSE;

    if (!functionsDetected)
    {
        if (!KernelModule)
            KernelModule = GetModuleHandleW(L"kernel32.dll");

        if (KernelModule)
        {
            F_FindFirstVolume = (TFindFirstVolume)GetProcAddress(KernelModule, "FindFirstVolumeW");
            F_FindNextVolume = (TFindNextVolume)GetProcAddress(KernelModule, "FindNextVolumeW");
            F_FindVolumeClose = (TFindVolumeClose)GetProcAddress(KernelModule, "FindVolumeClose");
        }
        if (!F_FindFirstVolume || !F_FindNextVolume || !F_FindVolumeClose)
        {
            F_FindFirstVolume = NULL;
            F_FindNextVolume = NULL;
            F_FindVolumeClose = NULL;
        }
        functionsDetected = TRUE;
    }

    return (F_FindFirstVolume != NULL);
}

BOOL OS<wchar_t>::OS_VolumeMountPointEnumExists()
{
    static BOOL functionsDetected = FALSE;

    if (!functionsDetected)
    {
        if (!KernelModule)
            KernelModule = GetModuleHandleW(L"kernel32.dll");

        if (KernelModule)
        {
            F_FindFirstVolumeMountPoint = (TFindFirstVolumeMountPoint)GetProcAddress(KernelModule, "FindFirstVolumeMountPointW");
            F_FindNextVolumeMountPoint = (TFindNextVolumeMountPoint)GetProcAddress(KernelModule, "FindNextVolumeMountPointW");
            F_FindVolumeMountPointClose = (TFindVolumeMountPointClose)GetProcAddress(KernelModule, "FindVolumeMountPointClose");
        }
        if (!F_FindFirstVolumeMountPoint || !F_FindNextVolumeMountPoint || !F_FindVolumeMountPointClose)
        {
            F_FindFirstVolumeMountPoint = NULL;
            F_FindNextVolumeMountPoint = NULL;
            F_FindVolumeMountPointClose = NULL;
        }
        functionsDetected = TRUE;
    }

    return (F_FindFirstVolumeMountPoint != NULL);
}

BOOL OS<wchar_t>::OS_GetLogicalDriveStringsExists()
{
    static BOOL functionsDetected = FALSE;

    if (!functionsDetected)
    {
        if (!KernelModule)
            KernelModule = GetModuleHandleW(L"kernel32.dll");

        if (KernelModule)
            F_GetLogicalDriveStrings = (TGetLogicalDriveStrings)GetProcAddress(KernelModule, "GetLogicalDriveStringsW");

        functionsDetected = TRUE;
    }

    return (F_GetLogicalDriveStrings != NULL);
}

BOOL OS<wchar_t>::OS_GetDiskFreeSpaceExExists()
{
    static BOOL functionsDetected = FALSE;

    if (!functionsDetected)
    {
        if (!KernelModule)
            KernelModule = GetModuleHandleW(L"kernel32.dll");

        if (KernelModule)
            F_GetDiskFreeSpaceEx = (TGetDiskFreeSpaceEx)GetProcAddress(KernelModule, "GetDiskFreeSpaceExW");

        functionsDetected = TRUE;
    }

    return (F_GetDiskFreeSpaceEx != NULL);
}

BOOL OS<wchar_t>::OS_GetVolumePathNamesForVolumeNameExists()
{
    static BOOL functionsDetected = FALSE;

    if (!functionsDetected)
    {
        if (!KernelModule)
            KernelModule = GetModuleHandleW(L"kernel32.dll");

        if (KernelModule)
            F_GetVolumePathNamesForVolumeName = (TGetVolumePathNamesForVolumeName)GetProcAddress(KernelModule, "GetVolumePathNamesForVolumeNameW");

        functionsDetected = TRUE;
    }

    return (F_GetVolumePathNamesForVolumeName != NULL);
}

BOOL OS<wchar_t>::OS_InitShell32Bindings()
{
    return TRUE;
}

void OS<wchar_t>::OS_ReleaseShell32Bindings()
{
}

template <>
VolumeType OS<wchar_t>::OS_GetVolumeType(const wchar_t* root)
{
    return static_cast<VolumeType>(::GetDriveTypeW(root));
}

template <>
void OS<wchar_t>::OS_GetDisplayNameFromSystem(const wchar_t* root, wchar_t* volumeName, int volumeNameBufSize)
{
    CALL_STACK_MESSAGE2("GetDisplayNameFromSystem(%ls)", root);

    SHFILEINFOW fi = {0};
    if (SHGetFileInfoW(root, 0, &fi, sizeof(fi), SHGFI_DISPLAYNAME))
    {
        lstrcpynW(volumeName, fi.szDisplayName, volumeNameBufSize);
        wchar_t* s = wcsrchr(volumeName, L'(');
        if (s != NULL)
        {
            while (s > volumeName && *(s - 1) == L' ')
                s--;
            *s = 0;
        }
    }
    else
        volumeName[0] = 0;
}

template <>
BOOL OS<wchar_t>::OS_GetVolumeInfo(const wchar_t* rootPathName, wchar_t* volumeNameBuffer, DWORD volumeNameSize,
                                   DWORD* volumeSerialNumber, DWORD* maximumComponentLength,
                                   DWORD* fileSystemFlags, wchar_t* fileSystemNameBuffer, DWORD fileSystemNameSize)
{
    return ::GetVolumeInformationW(rootPathName, volumeNameBuffer, volumeNameSize, volumeSerialNumber,
                                   maximumComponentLength, fileSystemFlags, fileSystemNameBuffer, fileSystemNameSize);
}

template <>
HANDLE OS<wchar_t>::OS_CreateFile(const wchar_t* fileName, DWORD desiredAccess, DWORD shareMode,
                                  SECURITY_ATTRIBUTES* securityAttributes, DWORD creationDisposition,
                                  DWORD flagsAndAttributes, HANDLE templateFile)
{
    return HANDLES_Q(CreateFileW(fileName, desiredAccess, shareMode, securityAttributes,
                                 creationDisposition, flagsAndAttributes, templateFile));
}

template <>
HICON OS<wchar_t>::OS_GetFileOrPathIconAux(const wchar_t* path, BOOL large)
{
    __try
    {
        SHFILEINFOW shi;
        shi.hIcon = NULL;
        SHGetFileInfoW(path, 0, &shi, sizeof(shi),
                       SHGFI_ICON | SHGFI_SHELLICONSIZE | (large ? 0 : SHGFI_SMALLICON));
        return shi.hIcon;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return NULL;
    }
}
