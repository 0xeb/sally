// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#pragma warning(push)
#pragma warning(disable : 4091) // disable typedef warning without variable declaration
#include <dbghelp.h>
#pragma warning(pop)

BOOL GenerateMiniDump(CMinidumpParams* minidumpParams, CSalmonSharedMemory* mem,
                      const std::wstring& reportBaseName, BOOL smallMinidump, BOOL* overSize)
{
    BOOL ret = FALSE;
    *overSize = FALSE;

    // Build path to dbghelp.dll next to our executable (utils\dbghelp.dll)
    std::wstring dllPath;
    static HMODULE hDbgHelp;
    if (GetCurrentModulePath(dllPath))
    {
        const size_t pos = dllPath.rfind(L'\\');
        if (pos != std::wstring::npos)
            dllPath.resize(pos);
        dllPath += L"\\dbghelp.dll";
        hDbgHelp = LoadLibraryW(dllPath.c_str());
    }
    if (hDbgHelp == NULL)
        hDbgHelp = LoadLibraryW(L"dbghelp.dll"); // fall back to system copy (sufficient on Win10+)

    if (hDbgHelp != NULL)
    {
        typedef BOOL(WINAPI * MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, CONST PMINIDUMP_EXCEPTION_INFORMATION,
                                                   CONST PMINIDUMP_USER_STREAM_INFORMATION, CONST PMINIDUMP_CALLBACK_INFORMATION);
        static MiniDumpWriteDump_t funcMiniDumpWriteDump;
        funcMiniDumpWriteDump = (MiniDumpWriteDump_t)GetProcAddress(hDbgHelp, "MiniDumpWriteDump");
        if (funcMiniDumpWriteDump != NULL)
        {
            std::wstring fileName = BugReportPath;
            if (!fileName.empty() && fileName.back() != L'\\')
                fileName += L'\\';
            fileName += reportBaseName;
            fileName += L".DMP";

            // the path may not exist yet - create it
            SHCreateDirectoryExW(NULL, BugReportPath.c_str(), NULL);

            HANDLE hDumpFile;
            hDumpFile = CreateFileW(fileName.c_str(), GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
            if (hDumpFile != INVALID_HANDLE_VALUE)
            {
                EXCEPTION_POINTERS ePtrs;
                MINIDUMP_EXCEPTION_INFORMATION expParam;
                ePtrs.ContextRecord = &mem->ContextRecord;
                ePtrs.ExceptionRecord = &mem->ExceptionRecord;
                expParam.ThreadId = mem->ThreadId;
                expParam.ExceptionPointers = &ePtrs;
                expParam.ClientPointers = FALSE;

                // great explanation of the flags (better than on MSDN): http://www.debuginfo.com/articles/effminidumps.html#minidumptypes
                static MINIDUMP_TYPE dumpType;
                // some of the flags require dbghelp.dll 6.1 - that is why Salamander ships its own copy
                if (smallMinidump)
                {
                    dumpType = (MINIDUMP_TYPE)(MiniDumpWithProcessThreadData |
                                               MiniDumpWithDataSegs |
                                               MiniDumpWithFullMemoryInfo |
                                               MiniDumpWithThreadInfo |
                                               MiniDumpWithUnloadedModules |
                                               MiniDumpIgnoreInaccessibleMemory); // under no circumstances do we want the function to fail
                }
                else
                {
                    dumpType = (MINIDUMP_TYPE)(MiniDumpWithPrivateReadWriteMemory |
                                               MiniDumpWithDataSegs |
                                               MiniDumpWithHandleData |
                                               MiniDumpWithFullMemoryInfo |
                                               MiniDumpWithThreadInfo |
                                               MiniDumpWithUnloadedModules |
                                               MiniDumpIgnoreInaccessibleMemory); // under no circumstances do we want the function to fail
                }

                BOOL bMiniDumpSuccessful;
                bMiniDumpSuccessful = funcMiniDumpWriteDump(mem->Process, mem->ProcessId,
                                                            hDumpFile, dumpType,
                                                            &expParam,
                                                            NULL, NULL);
                if (bMiniDumpSuccessful)
                {
                    ret = TRUE;
                }
                else
                {
                    // generation fails on W7 with the x64/Debug build launched from MSVC; if I run it outside MSVC, everything works fine
                    DWORD err = GetLastError();
                    minidumpParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_MINIDUMP_CALL, HLanguage).c_str(), err);
                }
                // regardless of whether minidump generation returned TRUE or FALSE, check the size of the produced dump
                DWORD sizeHigh = 0;
                DWORD sizeLow = GetFileSize(hDumpFile, &sizeHigh);
                if (sizeLow != INVALID_FILE_SIZE && (sizeHigh > 0 || sizeLow > 50 * 1000 * 1024))
                    *overSize = TRUE; // if the result exceeds 50 MB, report it so a smaller version can be tried
                CloseHandle(hDumpFile);
            }
            else
            {
                DWORD err = GetLastError();
                minidumpParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_MINIDUMP_CREATE, HLanguage).c_str(), fileName.c_str(), err);
            }
        }
        else
        {
            minidumpParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_LOAD_FAILED, HLanguage).c_str(), dllPath.c_str());
        }
    }
    else
    {
        minidumpParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_LOAD_FAILED, HLanguage).c_str(), dllPath.c_str());
    }

    return ret;
}

extern BOOL DirExists(const wchar_t* dirName);

// based on the current time and the short Salamander version, generate a name (without an extension)
// from which the names for the text bug report and for the minidump are derived
std::wstring GetReportBaseName(const wchar_t* targetPath, const wchar_t* shortName, DWORD64 uid, SYSTEMTIME lt)
{
    const std::wstring year = lt.wYear >= 2000 && lt.wYear < 2100
                                  ? FormatText(L"%02u", (BYTE)(lt.wYear - 2000))
                                  : FormatText(L"%04u", lt.wYear);
    std::wstring name = FormatText(L"%I64X-%s-%s%02u%02u-%02u%02u%02u",
                                   uid, shortName, year.c_str(), lt.wMonth, lt.wDay,
                                   lt.wHour, lt.wMinute, lt.wSecond);
    CharUpperBuffW(name.data(), static_cast<DWORD>(name.size()));

    // if the target path exists, there could be a collision (unlikely thanks to the timestamp in the name)
    if (targetPath != NULL && DirExists(targetPath))
    {
        int i;
        for (i = 0; i < 100; i++) // cover 1 - 99, then give up
        {
            std::wstring findMask = name;
            if (i > 0)
                findMask += FormatText(L"-%d", i);
            findMask += L'*';
            std::wstring findPath = targetPath;
            if (!findPath.empty() && findPath.back() != L'\\')
                findPath += L'\\';
            findPath += findMask;
            WIN32_FIND_DATAW find;
            HANDLE hFind = NOHANDLES(FindFirstFileW(findPath.c_str(), &find));
            if (hFind != INVALID_HANDLE_VALUE)
                NOHANDLES(FindClose(hFind));
            else
                break; // no conflict found
        }
        if (i > 0)
            name += FormatText(L"-%d", i);
    }
    return name;
}

static BOOL PublishReportBaseName(CMinidumpParams* params, const std::wstring& name)
{
    if (name.size() >= _countof(SalmonSharedMemory->BaseName))
    {
        params->ErrorMessage = L"The crash report name exceeds the frozen Salmon IPC field.";
        return FALSE;
    }
    wcscpy_s(SalmonSharedMemory->BaseName, name.c_str());
    return TRUE;
}

DWORD WINAPI MinidumpThreadF(void* param)
{
    CMinidumpParams* minidumpParams = (CMinidumpParams*)param;
    try
    {
    SYSTEMTIME lt;
    GetLocalTime(&lt);

    std::wstring reportBaseName = GetReportBaseName(BugReportPath.c_str(), CrashReportName.c_str(),
                                                    SalmonSharedMemory->UID, lt);
    if (!PublishReportBaseName(minidumpParams, reportBaseName))
    {
        minidumpParams->Result = FALSE;
        SetEvent(SalmonSharedMemory->Done);
        return EXIT_FAILURE;
    }

    // generate the minidump
    BOOL overSize;
    BOOL ret = GenerateMiniDump(minidumpParams, SalmonSharedMemory, reportBaseName, FALSE, &overSize);

    if (!ret || overSize)
    {
        reportBaseName = GetReportBaseName(BugReportPath.c_str(), CrashReportName.c_str(),
                                           SalmonSharedMemory->UID, lt);
        if (!PublishReportBaseName(minidumpParams, reportBaseName))
        {
            minidumpParams->Result = FALSE;
            SetEvent(SalmonSharedMemory->Done);
            return EXIT_FAILURE;
        }

        // generate the minidump
        ret = GenerateMiniDump(minidumpParams, SalmonSharedMemory, reportBaseName, TRUE, &overSize);
    }

    // let Salamander know that the minidump has been created
    // at this moment Salamander attempts to write the text bug report to disk and then exits
    SetEvent(SalmonSharedMemory->Done);

    // wait until Salamander saves the report or terminates; it may be in a bad state, so wait only for a limited time
    DWORD res = WaitForSingleObject(SalmonSharedMemory->Process, 10000);

    minidumpParams->Result = ret;
    }
    catch (const std::bad_alloc&)
    {
        minidumpParams->Result = FALSE;
        try { minidumpParams->ErrorMessage = L"Not enough memory to create the crash report."; }
        catch (...) {}
        SetEvent(SalmonSharedMemory->Done);
    }
    catch (...)
    {
        minidumpParams->Result = FALSE;
        try { minidumpParams->ErrorMessage = L"Unexpected failure while creating the crash report."; }
        catch (...) {}
        SetEvent(SalmonSharedMemory->Done);
    }
    return EXIT_SUCCESS;
}

HANDLE HMinidumpThread = NULL;

BOOL StartMinidumpThread(CMinidumpParams* params)
{
    if (HMinidumpThread != NULL)
        return FALSE;
    DWORD id;
    HMinidumpThread = CreateThread(NULL, 0, MinidumpThreadF, params, 0, &id);
    return HMinidumpThread != NULL;
}

BOOL IsMinidumpThreadRunning()
{
    if (HMinidumpThread == NULL)
        return FALSE;
    DWORD res = WaitForSingleObject(HMinidumpThread, 0);
    if (res != WAIT_TIMEOUT)
    {
        CloseHandle(HMinidumpThread);
        HMinidumpThread = NULL;
        return FALSE;
    }
    return TRUE;
}
