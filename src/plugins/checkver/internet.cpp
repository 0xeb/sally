// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <wininet.h>

#include "checkver.h"
#include "checkver.rh"
#include "checkver.rh2"
#include "lang\lang.rh"

const char* GITHUB_RELEASES_API_URL = "https://api.github.com/repos/0xeb/sally/releases/latest";
const char* GITHUB_API_HEADERS =
    "Accept: application/vnd.github+json\r\n"
    "X-GitHub-Api-Version: 2022-11-28\r\n";

const char* AGENT_NAME = "Sally CheckVer Plugin";

// limitation - may be called from only one thread; otherwise buffer overwrites are not handled
const char* GetInetErrorText(DWORD dError)
{
    static char tempErrorText[1024];
    tempErrorText[0] = 0;

    DWORD count = FormatMessage(FORMAT_MESSAGE_FROM_HMODULE, GetModuleHandle("wininet.dll"), dError, 0,
                                tempErrorText, 1024, NULL);

    if (count > 0)
    {
        // trim garbage on the right
        char* p = tempErrorText + count - 1;
        while (p > tempErrorText && (*p == '\n' || *p == '\r' || *p == ' '))
        {
            *p = 0;
            p--;
        }
    }
    else
        lstrcpy(tempErrorText, "Unable to get error message");
    return tempErrorText;
    /*
  // hopefully we will not need this (considering the trivial internet usage)
  sprintf(szTemp, "%s error code: %d\nMessage: %s\n", szCallFunc, dError, strName);
  int response;

  if (dError == ERROR_INTERNET_EXTENDED_ERROR)
  {
    InternetGetLastResponseInfo(&dwIntError, NULL, &dwLength);
    if (dwLength)
    {
      if (!(szBuffer = (char *) LocalAlloc(LPTR, dwLength)))
      {
        lstrcat(szTemp, "Unable to allocate memory to display Internet error code. Error code: ");
        lstrcat(szTemp, _itoa(GetLastError(), szBuffer, 10));
        lstrcat(szTemp, "\n");

        response = MessageBox(hErr, (LPSTR)szTemp,"Error", MB_OK);
        return FALSE;
      }

      if (!InternetGetLastResponseInfo (&dwIntError, (LPTSTR) szBuffer, &dwLength))
      {
        lstrcat(szTemp, "Unable to get Internet error. Error code: ");
        lstrcat(szTemp, _itoa(GetLastError(), szBuffer, 10));
        lstrcat(szTemp, "\n");
        response = MessageBox(hErr, (LPSTR)szTemp, "Error", MB_OK);
        return FALSE;
      }

      if (!(szBufferFinal = (char *) LocalAlloc(LPTR, (strlen(szBuffer) + strlen(szTemp) + 1))))
      {
        lstrcat(szTemp, "Unable to allocate memory. Error code: ");
        lstrcat(szTemp, _itoa (GetLastError(), szBuffer, 10));
        lstrcat(szTemp, "\n");
        response = MessageBox(hErr, (LPSTR)szTemp, "Error", MB_OK);
        return FALSE;
      }

      lstrcpy(szBufferFinal, szTemp);
      lstrcat(szBufferFinal, szBuffer);
      LocalFree(szBuffer);
      response = MessageBox(hErr, (LPSTR)szBufferFinal, "Error", MB_OK);
      LocalFree(szBufferFinal);
    }
  }
  else
  {
    response = MessageBox(hErr, (LPSTR)szTemp,"Error",MB_OK);
  }

  return response;
*/
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
    CPathBuffer buff; // Heap-allocated for long path support
    GetModuleFileName(DLLInstance, buff, buff.Size());
    HINSTANCE hLock = LoadLibrary(buff);

    BOOL exit = FALSE;

    DWORD errorCode = 0;
    HINTERNET hSession = NULL;
    HINTERNET hUrl = NULL;
    BOOL bResult = FALSE;
    DWORD dwBytesRead = 0;

    // is the main dialog still present and is it the one that opened us?
    if (dialogID == GetMainDialogID() && !exit)
    {
        AddLogLine(LoadStr(IDS_INET_PROTOCOL), FALSE);
        AddLogLine(LoadStr(IDS_INET_INIT), FALSE);
        errorCode = InternetAttemptConnect(0);
        if (errorCode != ERROR_SUCCESS)
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID)
            {
                char buff2[1024];
                sprintf(buff2, LoadStr(IDS_INET_INIT_FAILED), GetInetErrorText(errorCode));
                AddLogLine(buff2, TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (dialogID == GetMainDialogID() && !exit)
    {
        hSession = InternetOpen(AGENT_NAME, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (hSession == NULL)
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID)
            {
                DWORD err = GetLastError();
                char buff2[1024];
                sprintf(buff2, LoadStr(IDS_INET_INIT_FAILED), GetInetErrorText(err));
                AddLogLine(buff2, TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (dialogID == GetMainDialogID() && !exit)
    {
        AddLogLine(LoadStr(IDS_INET_CONNECT), FALSE);
        (void)firstLoadAfterInstall;
        hUrl = InternetOpenUrl(hSession, GITHUB_RELEASES_API_URL, GITHUB_API_HEADERS, -1,
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
                char buff2[1024];
                sprintf(buff2, LoadStr(IDS_INET_CONNECT_FAILED), GetInetErrorText(err));
                AddLogLine(buff2, TRUE);
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
                char statusText[128];
                _snprintf_s(statusText, _TRUNCATE, "HTTP %lu", statusCode);
                char buff2[1024];
                sprintf(buff2, LoadStr(IDS_INET_CONNECT_FAILED), statusText);
                AddLogLine(buff2, TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (dialogID == GetMainDialogID() && !exit)
    {
        AddLogLine(LoadStr(IDS_INET_READ), FALSE);
        LoadedScriptSize = 0;
        while (true)
        {
            DWORD bytesToRead = LOADED_SCRIPT_MAX - LoadedScriptSize;
            if (bytesToRead == 0)
            {
                EnterCriticalSection(&MainDialogIDSection);
                if (dialogID == MainDialogID)
                {
                    char buff2[1024];
                    sprintf(buff2, LoadStr(IDS_INET_READ_FAILED), "Response too large");
                    AddLogLine(buff2, TRUE);
                }
                LeaveCriticalSection(&MainDialogIDSection);
                exit = TRUE;
                break;
            }

            dwBytesRead = 0;
            bResult = InternetReadFile(hUrl, LoadedScript + LoadedScriptSize, bytesToRead, &dwBytesRead);
            if (!bResult)
            {
                EnterCriticalSection(&MainDialogIDSection);
                if (dialogID == MainDialogID)
                {
                    DWORD err = GetLastError();
                    char buff2[1024];
                    sprintf(buff2, LoadStr(IDS_INET_READ_FAILED), GetInetErrorText(err));
                    AddLogLine(buff2, TRUE);
                }
                LeaveCriticalSection(&MainDialogIDSection);
                exit = TRUE;
                break;
            }

            LoadedScriptSize += dwBytesRead;
            if (dwBytesRead == 0)
                break;
        }

        if (!exit && LoadedScriptSize == 0)
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID)
            {
                char buff2[1024];
                sprintf(buff2, LoadStr(IDS_INET_READ_FAILED), "GitHub returned an empty response");
                AddLogLine(buff2, TRUE);
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
            AddLogLine(LoadStr(IDS_INET_SUCCESS), FALSE);
        else
            LoadedScriptSize = 0;
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
