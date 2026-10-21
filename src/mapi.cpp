// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "common/IThreadStartup.h"

#include "mapi.h"
#include "ui/IPrompter.h"
#include "common/Win32TextCodec.h"
#include "common/unicode/helpers.h"

static void ShowMailFailure(const std::wstring& detail)
{
    const std::wstring message = FormatStrW(LoadStrW(IDS_EMAILFILES_SENDERROR), detail.c_str());
    gPrompter->ShowError(NULL, LoadStrW(IDS_ERRORTITLE), message.c_str());
}

CSimpleMAPI::CSimpleMAPI(PFNMAPISENDMAIL sendMail)
    : FileNames(10, 50), TotalSize(0, 0)
{
    CALL_STACK_MESSAGE_NONE
    HLibrary = NULL;
    MAPISendMail = sendMail;
};

CSimpleMAPI::~CSimpleMAPI()
{
    CALL_STACK_MESSAGE_NONE
    Release();
}

BOOL CSimpleMAPI::Init(HWND hParent)
{
    CALL_STACK_MESSAGE_NONE
    if (HLibrary == NULL)
    {
        HLibrary = HANDLES_Q(LoadLibraryW(L"mapi32.dll"));
        if (HLibrary == NULL)
        {
            // under NT4.0 US + IE 4.01 US, mapi32.dll is not installed
            // but I found msoemapi.dll there which has the necessary export and more importantly, it works,
            // so why not try it...
            HLibrary = HANDLES_Q(LoadLibraryW(L"msoemapi.dll"));
            if (HLibrary == NULL)
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_EMAILFILES_MAPIERROR));
                return FALSE;
            }
        }

        MAPISendMail = (PFNMAPISENDMAIL)GetProcAddress(HLibrary, "MAPISendMail"); // no header available

        if (MAPISendMail == NULL)
        {
            Release();
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_EMAILFILES_MAPIERROR));
            return FALSE;
        }
    }
    return TRUE;
}

void CSimpleMAPI::Release()
{
    CALL_STACK_MESSAGE1("CSimpleMAPI::Release()");
    // free allocated strings
    int i;
    for (i = 0; i < FileNames.Count; i++)
        free(FileNames[i]);
    FileNames.DestroyMembers();
    // detach functions
    MAPISendMail = NULL;
    // release the library
    if (HLibrary != NULL)
    {
        HANDLES(FreeLibrary(HLibrary));
        HLibrary = NULL;
    }
    TotalSize.Set(0, 0);
}

BOOL CSimpleMAPI::AddFile(const wchar_t* fileName, const CQuadWord* size)
{
    CALL_STACK_MESSAGE_NONE
    size_t len = wcslen(fileName);

    wchar_t* text = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (text == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    FileNames.Add(text);
    if (!FileNames.IsGood())
    {
        free(text); // was `delete` on a malloc'd block - UB. Release() at
                    // :62 frees these with free(), which is what the allocator requires.
        FileNames.ResetState();
        return FALSE;
    }

    memcpy(text, fileName, (len + 1) * sizeof(wchar_t));
    TotalSize += *size;

    return TRUE;
}

BOOL CSimpleMAPI::SendMail()
{
    CALL_STACK_MESSAGE1("CSimpleMAPI::SendMail()");
    if (MAPISendMail == NULL)
    {
        gPrompter->ShowError(NULL, LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_EMAILFILES_MAPIERROR));
        return FALSE;
    }
    if (FileNames.Count == 0)
    {
        TRACE_E("FileNames.Count == 0");
        ShowMailFailure(FormatThreadStartupErrorW(ERROR_INVALID_PARAMETER));
        return FALSE;
    }

    MapiFileDesc* fileDesc = static_cast<MapiFileDesc*>(gThreadStartup->AllocateBuffer(FileNames.Count * sizeof(MapiFileDesc)));
    if (fileDesc == NULL)
    {
        TRACE_E(LOW_MEMORY);
        ShowMailFailure(FormatThreadStartupErrorW(ERROR_NOT_ENOUGH_MEMORY));
        return FALSE;
    }

    ZeroMemory(fileDesc, FileNames.Count * sizeof(MapiFileDesc));

    // mapi32.dll's ANSI MAPISendMail export requires
    // narrow (LPSTR) strings in MapiFileDesc - an external ABI Sally does not own (see mapi.h).
    // FileNames is stored wide (AddFile's callers are wide); convert to ANSI here at the
    // boundary, exact-or-refuse rather than best-fit - a silently substituted attachment path
    // could point MAPISendMail at a different file than the one the user selected. These ANSI
    // copies must outlive the MAPISendMail call below, so they are freed alongside
    // fileDesc/subject after it returns, not before.
    char** fileNamesA = static_cast<char**>(gThreadStartup->AllocateBuffer(FileNames.Count * sizeof(char*)));
    if (fileNamesA == NULL)
    {
        TRACE_E(LOW_MEMORY);
        free(fileDesc);
        ShowMailFailure(FormatThreadStartupErrorW(ERROR_NOT_ENOUGH_MEMORY));
        return FALSE;
    }
    ZeroMemory(fileNamesA, FileNames.Count * sizeof(char*));

    MapiFileDesc* iterator = fileDesc;
    int subjectSize = 0;
    int i;
    BOOL ok = TRUE;
    for (i = 0; i < FileNames.Count; i++)
    {
        std::string fileNameA;
        if (!Win32EncodeAcpExact(FileNames[i], fileNameA))
        {
            TRACE_E("CSimpleMAPI::SendMail(): an attachment path cannot be represented in the system code page");
            const std::wstring message = FormatStrW(LoadStrW(IDS_EMAILFILES_PATHERROR), FileNames[i]);
            gPrompter->ShowError(NULL, LoadStrW(IDS_ERRORTITLE), message.c_str());
            ok = FALSE;
            break;
        }

        fileNamesA[i] = static_cast<char*>(gThreadStartup->AllocateBuffer(fileNameA.size() + 1));
        if (fileNamesA[i] == NULL)
        {
            TRACE_E(LOW_MEMORY);
            ShowMailFailure(FormatThreadStartupErrorW(ERROR_NOT_ENOUGH_MEMORY));
            ok = FALSE;
            break;
        }
        memcpy(fileNamesA[i], fileNameA.c_str(), fileNameA.size() + 1); // includes the NUL

        iterator->nPosition = (ULONG)-1;        // position not specified
        iterator->lpszPathName = fileNamesA[i]; // pathname
        char* p = strrchr(fileNamesA[i], '\\');
        if (p == NULL)
            p = fileNamesA[i];
        else
            p++;
        iterator->lpszFileName = p;        // filename visible for user
        subjectSize += (int)strlen(p) + 2; // file_name1, file_name2, ....
        iterator++;
    }

    if (!ok)
    {
        for (int j = 0; j < FileNames.Count; j++)
            free(fileNamesA[j]);
        free(fileNamesA);
        free(fileDesc);
        return FALSE;
    }

    MapiMessage message;
    ZeroMemory(&message, sizeof(message));
    message.nFileCount = FileNames.Count;
    message.lpFiles = fileDesc;

    // Simple MAPI is an ANSI ABI. The attachment PATHS above are identity and must be exact or
    // refused; the subject and note body are text the mail client merely displays, so they take the
    // best-effort projection instead. Refusing them abandoned the whole Email Files command - with
    // no dialog, because SendMail() runs on a worker thread whose return value nobody reads -
    // whenever the language pack's strings were outside the active ACP, which is an ordinary
    // configuration (the Russian pack on an ACP-1252 machine; Select Language permits it). The
    // narrow build these strings came from substituted '?' and sent the mail, attachments intact.
    std::string subjectPrefix;
    std::string body;
    if (!Win32EncodeAcpLossy(LoadStrOwned(IDS_EMAILFILES_SUBJECT), subjectPrefix) ||
        !Win32EncodeAcpLossy(LoadStrOwned(IDS_EMAILFILES_BODY), body))
    {
        // Only a genuine conversion failure (out of memory, absurd length) reaches here now.
        TRACE_E("CSimpleMAPI::SendMail(): localized message text could not be converted");
        for (i = 0; i < FileNames.Count; i++)
            free(fileNamesA[i]);
        free(fileNamesA);
        free(fileDesc);
        ShowMailFailure(FormatThreadStartupErrorW(ERROR_NOT_ENOUGH_MEMORY));
        return FALSE;
    }

    std::string subject;
    subject.reserve(subjectPrefix.size() + static_cast<size_t>(subjectSize) + 1);
    subject = subjectPrefix;
    subject.push_back(' ');
    iterator = fileDesc;
    for (i = 0; i < FileNames.Count; i++)
    {
        subject += iterator->lpszFileName;
        if (i < FileNames.Count - 1)
            subject += ", ";
        iterator++;
    }
    message.lpszSubject = const_cast<char*>(subject.c_str());
    message.lpszNoteText = const_cast<char*>(body.c_str());

    ULONG ret = MAPISendMail(0,
                             /*(ULONG)hParent*/ 0, // we will be non-modal
                             &message,
                             MAPI_LOGON_UI | MAPI_DIALOG,
                             0L);

    for (i = 0; i < FileNames.Count; i++)
        free(fileNamesA[i]);
    free(fileNamesA);
    free(fileDesc);
    if (ret != SUCCESS_SUCCESS && ret != MAPI_USER_ABORT)
        ShowMailFailure(FormatStrW(LoadStrW(IDS_EMAILFILES_PROVIDERERROR), static_cast<unsigned>(ret)));
    return ret == SUCCESS_SUCCESS;
}

unsigned SimpleMAPISendMailThreadBody(void* param)
{
    CALL_STACK_MESSAGE1("SimpleMAPISendMailThreadBody()");
    SetThreadNameInVCAndTrace(L"MapiSendMail");
    TRACE_I("Begin");
    // lower the thread priority to "normal" so the operation does not burden the machine too much
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    CSimpleMAPI* mapi = (CSimpleMAPI*)param;
    BOOL ret = mapi->SendMail();
    delete mapi;

    TRACE_I("End");
    return ret ? 1 : 0;
}

unsigned SimpleMAPISendMailThreadEH(void* param)
{
    CALL_STACK_MESSAGE_NONE
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return SimpleMAPISendMailThreadBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread SimpleMAPISendMailThreadBody: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this call still performs some operations)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI SimpleMAPISendMailThread(void* param)
{
    CALL_STACK_MESSAGE_NONE
    CCallStack stack;
    return SimpleMAPISendMailThreadEH(param);
}

BOOL SimpleMAPISendMail(CSimpleMAPI* mapi)
{
    CALL_STACK_MESSAGE_NONE
    HCURSOR hOldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

    DWORD threadID;
    const StartupHandleResult started = gThreadStartup->StartThread(SimpleMAPISendMailThread, mapi, &threadID);
    HANDLE thread = started.Handle;
    HANDLES_ADD_EX(__otQuiet, thread != NULL, __htThread, __hoCreateThread, thread, started.Error, TRUE);
    if (thread == NULL)
    {
        TRACE_E("Unable to start SimpleMAPISendMailThread thread.");
        delete mapi;
        SetCursor(hOldCur);
        ShowMailFailure(FormatThreadStartupErrorW(started.Error));
        return FALSE;
    }

    AddAuxThread(thread); // add the thread among existing viewers (killed on exit)
    SetCursor(hOldCur);
    return TRUE;
}
