// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <algorithm>
#include <new>
#include <wininet.h>

#include "checkver.h"
#include "checkver.rh"
#include "checkver.rh2"
#include "lang\lang.rh"

const wchar_t* GITHUB_RELEASES_API_URL = L"https://api.github.com/repos/0xeb/sally/releases/latest";
const wchar_t* GITHUB_API_HEADERS =
    L"Accept: application/vnd.github+json\r\n"
    L"X-GitHub-Api-Version: 2022-11-28\r\n";

const wchar_t* AGENT_NAME = L"Sally CheckVer Plugin";

std::wstring GetInetErrorText(DWORD error)
{
    wchar_t* allocated = NULL;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS,
        GetModuleHandleW(L"wininet.dll"), error, 0,
        reinterpret_cast<wchar_t*>(&allocated), 0, NULL);
    if (count == 0 || allocated == NULL)
        return L"Unable to get error message";

    std::wstring text;
    try
    {
        text.assign(allocated, count);
    }
    catch (const std::bad_alloc&)
    {
        LocalFree(allocated);
        return L"Unable to allocate error message";
    }
    LocalFree(allocated);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
        text.pop_back();
    return text;
}

void IncMainDialogID()
{
    // neither callbacks nor trace must end up here - called from a thread which may run
    // when Salamander has long since exited
    EnterCriticalSection(&MainDialogIDSection);
    MainDialogID++;
    LeaveCriticalSection(&MainDialogIDSection);
}

DWORD
GetMainDialogID()
{
    // neither callbacks nor trace must end up here - called from a thread which may run
    // when Salamander has long since exited
    EnterCriticalSection(&MainDialogIDSection);
    DWORD id = MainDialogID;
    LeaveCriticalSection(&MainDialogIDSection);
    return id;
}

struct CTDData
{
    DWORD MainDialogID;
    BOOL FirstLoadAfterInstall;
    HANDLE Continue;
};

DWORD WINAPI ThreadDownload(void* param)
{
    CTDData* data = (CTDData*)param;
    DWORD dialogID = data->MainDialogID;
    BOOL firstLoadAfterInstall = data->FirstLoadAfterInstall;
    SetEvent(data->Continue); // let the main thread continue; from this point on the data are invalid (=NULL)
    data = NULL;

    // lock the DLL to prevent it from being unloaded while this function runs
    std::wstring modulePath;
    HINSTANCE hLock = SPLGetModuleFileNameOwned(DLLInstance, modulePath)
                          ? LoadLibraryW(modulePath.c_str())
                          : NULL;

    BOOL exit = FALSE;

    DWORD errorCode = 0;
    HINTERNET hSession = NULL;
    HINTERNET hUrl = NULL;
    BOOL bResult = FALSE;
    DWORD dwBytesRead = 0;

    // is the main dialog still present and is it the one that opened us?
    if (dialogID == GetMainDialogID() && !exit)
    {
        AddLogLine(LangStr(IDS_INET_PROTOCOL).c_str(), FALSE);
        AddLogLine(LangStr(IDS_INET_INIT).c_str(), FALSE);
        errorCode = InternetAttemptConnect(0);
        if (errorCode != ERROR_SUCCESS)
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID)
            {
                const std::wstring message = SPLFormatStringOwned(
                    LangStr(IDS_INET_INIT_FAILED).c_str(), GetInetErrorText(errorCode).c_str());
                AddLogLine(message.c_str(), TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (dialogID == GetMainDialogID() && !exit)
    {
        hSession = InternetOpenW(AGENT_NAME, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (hSession == NULL)
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID)
            {
                DWORD err = GetLastError();
                const std::wstring message = SPLFormatStringOwned(
                    LangStr(IDS_INET_INIT_FAILED).c_str(), GetInetErrorText(err).c_str());
                AddLogLine(message.c_str(), TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (dialogID == GetMainDialogID() && !exit)
    {
        AddLogLine(LangStr(IDS_INET_CONNECT).c_str(), FALSE);
        (void)firstLoadAfterInstall;
        hUrl = InternetOpenUrlW(hSession, GITHUB_RELEASES_API_URL, GITHUB_API_HEADERS, -1,
                               INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_RELOAD |
                                   INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE |
                                   INTERNET_FLAG_SECURE,
                               0);

        if (hUrl == NULL)
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID)
            {
                DWORD err = GetLastError();
                const std::wstring message = SPLFormatStringOwned(
                    LangStr(IDS_INET_CONNECT_FAILED).c_str(), GetInetErrorText(err).c_str());
                AddLogLine(message.c_str(), TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (dialogID == GetMainDialogID() && !exit)
    {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (!HttpQueryInfo(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode,
                           &statusCodeSize, NULL) ||
            statusCode < 200 || statusCode >= 300)
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID)
            {
                const std::wstring statusText = SPLFormatStringOwned(L"HTTP %lu", statusCode);
                const std::wstring message = SPLFormatStringOwned(
                    LangStr(IDS_INET_CONNECT_FAILED).c_str(), statusText.c_str());
                AddLogLine(message.c_str(), TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (dialogID == GetMainDialogID() && !exit)
    {
        AddLogLine(LangStr(IDS_INET_READ).c_str(), FALSE);
        LoadedScript.clear();
        std::array<BYTE, 16 * 1024> chunk;
        while (true)
        {
            dwBytesRead = 0;
            const size_t remaining = CHECKVER_MAX_RELEASE_RESPONSE_BYTES - LoadedScript.size();
            const DWORD bytesToRead = static_cast<DWORD>((std::min)(chunk.size(), remaining + 1));
            bResult = InternetReadFile(hUrl, chunk.data(), bytesToRead, &dwBytesRead);
            if (!bResult)
            {
                EnterCriticalSection(&MainDialogIDSection);
                if (dialogID == MainDialogID)
                {
                    DWORD err = GetLastError();
                    const std::wstring message = SPLFormatStringOwned(
                        LangStr(IDS_INET_READ_FAILED).c_str(), GetInetErrorText(err).c_str());
                    AddLogLine(message.c_str(), TRUE);
                }
                LeaveCriticalSection(&MainDialogIDSection);
                exit = TRUE;
                break;
            }

            if (dwBytesRead == 0)
                break;
            if (dwBytesRead > remaining)
            {
                EnterCriticalSection(&MainDialogIDSection);
                if (dialogID == MainDialogID)
                {
                    const std::wstring message = SPLFormatStringOwned(
                        LangStr(IDS_INET_READ_FAILED).c_str(), L"Response too large");
                    AddLogLine(message.c_str(), TRUE);
                }
                LeaveCriticalSection(&MainDialogIDSection);
                exit = TRUE;
                break;
            }
            try
            {
                LoadedScript.insert(LoadedScript.end(), chunk.begin(), chunk.begin() + dwBytesRead);
            }
            catch (const std::bad_alloc&)
            {
                EnterCriticalSection(&MainDialogIDSection);
                if (dialogID == MainDialogID)
                {
                    const std::wstring message = SPLFormatStringOwned(
                        LangStr(IDS_INET_READ_FAILED).c_str(), L"Unable to allocate response buffer");
                    AddLogLine(message.c_str(), TRUE);
                }
                LeaveCriticalSection(&MainDialogIDSection);
                exit = TRUE;
                break;
            }
        }

        if (!exit && LoadedScript.empty())
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID)
            {
                const std::wstring message = SPLFormatStringOwned(
                    LangStr(IDS_INET_READ_FAILED).c_str(), L"GitHub returned an empty response");
                AddLogLine(message.c_str(), TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (hUrl != NULL)
        InternetCloseHandle(hUrl);
    if (hSession != NULL)
        InternetCloseHandle(hSession);

    EnterCriticalSection(&MainDialogIDSection);
    DWORD id = MainDialogID;
    if (dialogID == GetMainDialogID())
    {
        if (!exit)
            AddLogLine(LangStr(IDS_INET_SUCCESS).c_str(), FALSE);
        else
            LoadedScript.clear();
        PostMessage(HMainDialog, WM_USER_DOWNLOADTHREAD_EXIT, !exit, 0); // thread ends; data are loaded
        FreeLibrary(hLock);                                              // release the lock
        LeaveCriticalSection(&MainDialogIDSection);
        return 0; // let the thread die naturally
    }
    else
    {
        LeaveCriticalSection(&MainDialogIDSection);
        // we were killed from the outside - after FreeLibrary the last lock on the SPL may be removed
        // (Salamander may no longer be running) and there would be nowhere to return to,
        // therefore call this function:
        FreeLibraryAndExitThread(hLock, 3666);
        return 0; // we never return here, but the compiler cannot know that :-)
    }
}

HANDLE
StartDownloadThread(BOOL firstLoadAfterInstall)
{
    CTDData data;
    data.MainDialogID = GetMainDialogID();
    data.FirstLoadAfterInstall = firstLoadAfterInstall;
    data.Continue = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (data.Continue == NULL)
    {
        TRACE_E("Unable to create Continue event.");
        return NULL;
    }

    DWORD threadID;
    HANDLE hThread = CreateThread(NULL, 0, ThreadDownload, &data, 0, &threadID);
    if (hThread == NULL)
        TRACE_E("Unable to create Check Version Download thread.");
    else // wait until the thread takes the data
        WaitForSingleObject(data.Continue, INFINITE);

    CloseHandle(data.Continue);

    return hThread;
}
