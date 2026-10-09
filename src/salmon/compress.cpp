// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "zip/ZipWriter.h"

//------------------------------------------------------------------------------------------------
//
// CompresBugReports()
//
// Packs each report's files (<Name>.TXT, .DMP, .INF) into <Name>.zip, a format a GitHub
// issue accepts as an attachment.

namespace
{

std::wstring JoinPath(const std::wstring& directory, const std::wstring& name)
{
    std::wstring path = directory;
    if (!path.empty() && path.back() != L'\\')
        path += L'\\';
    path += name;
    return path;
}

bool IsReportArchive(const wchar_t* fileName)
{
    const wchar_t* ext = wcsrchr(fileName, L'.');
    return ext != NULL && (_wcsicmp(ext, L".zip") == 0 || _wcsicmp(ext, L".7z") == 0);
}

BOOL PackReport(const CBugReport& report, std::wstring& errorMessage)
{
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW find;
    HANDLE hFind = FindFirstFileW(JoinPath(BugReportPath, report.Name + L".*").c_str(), &find);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && !IsReportArchive(find.cFileName))
                files.push_back(find.cFileName);
        } while (FindNextFileW(hFind, &find));
        FindClose(hFind);
    }

    Sally::Zip::ZipWriter writer;
    BOOL ok = writer.Create(JoinPath(BugReportPath, report.Name + L".zip"));
    for (const std::wstring& file : files)
    {
        if (!ok)
            break;
        ok = writer.AddFile(JoinPath(BugReportPath, file), file);
    }
    if (ok)
        ok = writer.Finish();
    if (!ok)
        errorMessage = writer.ErrorText();
    return ok;
}

} // namespace

BOOL CompresBugReports(CCompressParams* compressParams)
{
    BOOL ret = TRUE;
    compressParams->ErrorMessage.clear();
    for (const CBugReport& report : BugReports)
    {
        std::wstring error;
        if (!PackReport(report, error))
        {
            if (compressParams->ErrorMessage.empty())
                compressParams->ErrorMessage = error;
            ret = FALSE;
        }
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
