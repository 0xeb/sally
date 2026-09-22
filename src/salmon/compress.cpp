// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

//------------------------------------------------------------------------------------------------
//
// CompresBugReports()
//

BOOL CompresBugReports(CCompressParams* compressParams)
{
    BOOL ret = FALSE;
    compressParams->ErrorMessage.clear();
    const wchar_t* WRAPPER_DLL = L"..\\plugins\\7zip\\7zwrapper.dll";
    HINSTANCE h7zwrapper = LoadLibraryW(WRAPPER_DLL);
    if (h7zwrapper != NULL)
    {
        typedef BOOL(WINAPI * CompressFilesW_t)(const wchar_t* archiveName7z, const wchar_t* sourceDir,
                                                const wchar_t* filter, wchar_t** errorMessage);
        CompressFilesW_t CompressFilesW;
        CompressFilesW = (CompressFilesW_t)GetProcAddress(h7zwrapper, "CompressFilesW");
        if (CompressFilesW != NULL)
        {
            ret = TRUE;
            for (const CBugReport& report : BugReports)
            {
                // Fully qualified on purpose. The 7-Zip wrapper globs this mask
                // with a bare FindFirstFileW(filter, ...), which resolves against
                // the PROCESS CURRENT DIRECTORY - its 'sourceDir' argument is only
                // used afterwards to rebuild full paths from cFileName, and does
                // not scope the search. pre-unicode made that work by calling
                // SetCurrentDirectory(BugReportPath) first; the wide port dropped
                // that call but kept the bare mask, so the glob matched nothing
                // wherever salmon happened to be running and every bug-report
                // archive came out empty, silently. Qualifying the mask fixes it
                // without reintroducing a process-global directory change.
                std::wstring mask = BugReportPath;
                if (!mask.empty() && mask.back() != L'\\')
                    mask += L'\\';
                mask += report.Name + L".*";
                std::wstring archive = BugReportPath;
                if (!archive.empty() && archive.back() != L'\\')
                    archive += L'\\';
                archive += report.Name + L".7Z";
                DeleteFileW(archive.c_str()); // so the subsequent compression does not fail

                wchar_t* error = NULL;
                BOOL res = CompressFilesW(archive.c_str(), BugReportPath.c_str(), mask.c_str(), &error);
                if (!res)
                    compressParams->ErrorMessage = error != NULL ? error : L"The 7-Zip wrapper could not create the archive.";
                CoTaskMemFree(error);
                ret &= res;
                if (!ReportOldBugs)
                    break;
            }
        }
        else
        {
            compressParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_LOAD_FAILED, HLanguage).c_str(), WRAPPER_DLL);
        }
        FreeLibrary(h7zwrapper);
    }
    else
    {
        compressParams->ErrorMessage = FormatText(LoadStr(IDS_SALMON_LOAD_FAILED, HLanguage).c_str(), WRAPPER_DLL);
    }
    return ret;
}

DWORD WINAPI CompressThreadF(void* param)
{
    CCompressParams* compressParams = (CCompressParams*)param;
    try
    {
        compressParams->Result = CompresBugReports(compressParams);
    }
    catch (const std::bad_alloc&)
    {
        compressParams->Result = FALSE;
        try { compressParams->ErrorMessage = L"Not enough memory to prepare the bug report archive."; }
        catch (...) {}
    }
    catch (...)
    {
        compressParams->Result = FALSE;
        try { compressParams->ErrorMessage = L"Unexpected failure while preparing the bug report archive."; }
        catch (...) {}
    }
    return EXIT_SUCCESS;
}

HANDLE HCompressThread = NULL;

BOOL StartCompressThread(CCompressParams* params)
{
    if (HCompressThread != NULL)
        return FALSE;
    DWORD id;
    HCompressThread = CreateThread(NULL, 0, CompressThreadF, params, 0, &id);
    return HCompressThread != NULL;
}

BOOL IsCompressThreadRunning()
{
    if (HCompressThread == NULL)
        return FALSE;
    DWORD res = WaitForSingleObject(HCompressThread, 0);
    if (res != WAIT_TIMEOUT)
    {
        CloseHandle(HCompressThread);
        HCompressThread = NULL;
        return FALSE;
    }
    return TRUE;
}
