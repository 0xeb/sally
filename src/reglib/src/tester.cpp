// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <crtdbg.h>
#include <string>

#include "regparse.h"

// We want to learn about SEH Exceptions even on x64 Windows 7 SP1 and later
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
        FIsWow64Process isWow64 = (FIsWow64Process)GetProcAddress(hDLL, "IsWow64Process");
        FSetProcessUserModeExceptionPolicy set = (FSetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "SetProcessUserModeExceptionPolicy");
        FGetProcessUserModeExceptionPolicy get = (FGetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "GetProcessUserModeExceptionPolicy");
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

static BOOL DumpRegistryToFile(CSalamanderRegistryExAbstractW* registry,
                               const wchar_t* fileName, const wchar_t* clearKeyName)
{
    HANDLE file = CreateFileW(fileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    const BOOL result = registry->Dump(file, clearKeyName);
    CloseHandle(file);
    return result;
}

int wmain(int argc, wchar_t* argv[])
{
    EnableExceptionsOn64();

    HANDLE hFile;
    wchar_t* buf;
    DWORD size, nBytesRead;
    CSalamanderRegistryExAbstractW* pRegistry;
    int ret = 0;

    _CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF | _CRTDBG_ALLOC_MEM_DF);

    if ((argc != 2) && (argc != 3))
    {
        printf("RegParser reads in MBCS Reg4.0 and UTF16 Reg5.0 file and stores its content in Registry or second file.\n\n"
               "Usage:\n  RegParser.exe {InFile.reg [OutFile.reg]} | {branch OutFile.reg}\n\n"
               "1 argument - InFile.reg copied to system registry\n"
               "2 file arguments - InFile.reg parsed and resaved to OutFile.reg\n"
               "branch + 1 file argument - branch in system registry copied to OutFile.reg\n"
               "                           The branch must be enclosed in []\n\n");
        return 0;
    }

    if ((argc == 3) && (argv[1][0] == '['))
    {
        CSalamanderRegistryExAbstractW* pSysRegistry = REG_SysRegistryFactoryW();
        pRegistry = REG_MemRegistryFactoryW();

        if (pSysRegistry && pRegistry)
        {
            // Copy system registry to file
            std::wstring branch(argv[1] + 1);
            if (!branch.empty() && branch.back() == L']')
            {
                branch.pop_back();
                eRPE_ERROR regerr = CopyRegistryBranchW(branch.c_str(), pSysRegistry, pRegistry);
                if (RPE_OK == regerr)
                {
                    if (!DumpRegistryToFile(pRegistry, argv[2], NULL))
                    {
                        wprintf(L"Dumping branch %s to file %s failed\n", branch.c_str(), argv[2]);
                        ret = 11;
                    }
                }
                else
                {
                    wprintf(L"Error %d: Could not copy branch %s from system registry\n", regerr, branch.c_str());
                    ret = 12;
                }
            }
            else
            {
                wprintf(L"Invalid branch name %s\n", argv[2]);
                ret = 13;
            }
        }
        else
        {
            printf("Error: Could not instantiate registry classes\n");
            ret = 14;
        }
        if (pRegistry)
            pRegistry->Release();
        if (pSysRegistry)
            pSysRegistry->Release();
        return ret;
    }

    hFile = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0);
    if (INVALID_HANDLE_VALUE == hFile)
    {
        wprintf(L"Error %d: Could not open %s\n", errno, argv[1]);
        return 1;
    }
    size = GetFileSize(hFile, NULL);
    if (0xFFFFFFFF == size)
    {
        printf("Error %d: Could not obtain input file size\n", GetLastError());
        CloseHandle(hFile);
        return 2;
    }
    buf = (wchar_t*)malloc(size + sizeof(wchar_t));
    if (!buf)
    {
        printf("Error: Could not allocate %d bytes\n", size);
        CloseHandle(hFile);
        return 3;
    }
    if (!ReadFile(hFile, buf, size, &nBytesRead, NULL) || (size != nBytesRead))
    {
        printf("Error: Could not read %d bytes (oonly %d bytes was read)\n", size, nBytesRead);
        free(buf);
        CloseHandle(hFile);
        return 4;
    }

    *(WCHAR*)((LPBYTE)buf + size) = 0; // safety net for too short file
    DWORD utf16ByteSize = 0;
    eRPE_ERROR conversionError = ConvertRegistryFileToUtf16(&buf, size, utf16ByteSize);
    if (conversionError != RPE_OK)
    {
        printf("Error: Could not decode registry file: %d\n", conversionError);
        free(buf);
        CloseHandle(hFile);
        return 5;
    }
    size = utf16ByteSize;

    pRegistry = (argc == 2) ? REG_SysRegistryFactoryW() : REG_MemRegistryFactoryW();
    if (pRegistry)
    {
        eRPE_ERROR regerr = ParseRegistryFileW(buf, pRegistry, FALSE);
        if (RPE_OK == regerr)
        {
            if (!DumpRegistryToFile(pRegistry, argv[2], NULL))
            {
                wprintf(L"Dumping to file %s failed\n", argv[2]);
                ret = 6;
            }
        }
        else
        {
            wprintf(L"Loading file %s failed: error %d\n", argv[1], regerr);
            ret = 7;
        }
        pRegistry->Release();
    }
    else
    {
        printf("Error: Could not instantiate registry class\n");
        ret = 8;
    }
    free(buf);
    CloseHandle(hFile);
#if defined(_DEBUG)
    _CrtDumpMemoryLeaks();
#endif
    return ret;
}
