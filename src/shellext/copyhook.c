// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>

#include "lstrfix.h"
#include "..\shexreg.h"
#include "shellext.h"

#undef INTERFACE
#define INTERFACE ImpICopyHook

STDMETHODIMP CH_QueryInterface(THIS_ REFIID riid, void** ppv)
{
    CopyHook* ch = (CopyHook*)This;
    return ch->m_pObj->lpVtbl->QueryInterface((IShellExt*)ch->m_pObj, riid, ppv);
}

STDMETHODIMP_(ULONG) CH_AddRef(THIS)
{
    CopyHook* ch = (CopyHook*)This;
    return ch->m_pObj->lpVtbl->AddRef((IShellExt*)ch->m_pObj);
}

STDMETHODIMP_(ULONG) CH_Release(THIS)
{
    CopyHook* ch = (CopyHook*)This;
    return ch->m_pObj->lpVtbl->Release((IShellExt*)ch->m_pObj);
}

#pragma optimize("", off)
void MyZeroMemory(void* ptr, DWORD size)
{
    char* c = (char*)ptr;
    while (size-- != 0)
        *c++ = 0;
}
#pragma optimize("", on)

PTOKEN_USER GetProcessUser(DWORD pid)
{
    PTOKEN_USER result = NULL;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (process != NULL)
    {
        HANDLE token = NULL;
        if (OpenProcessToken(process, TOKEN_QUERY, &token))
        {
            DWORD bytes = 0;
            if (!GetTokenInformation(token, TokenUser, NULL, 0, &bytes) &&
                GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            {
                PTOKEN_USER user = (PTOKEN_USER)GlobalAlloc(GMEM_FIXED, bytes);
                if (user != NULL)
                {
                    MyZeroMemory(user, bytes);
                    if (GetTokenInformation(token, TokenUser, user, bytes, &bytes) &&
                        IsValidSid(user->User.Sid))
                        result = user;
                    else
                        GlobalFree(user);
                }
            }
            CloseHandle(token);
        }
        CloseHandle(process);
    }
    return result;
}

BOOL AreNotProcessesOfTheSameUser(DWORD pid1, DWORD pid2)
{
    BOOL different = FALSE;
    PTOKEN_USER user1;
    PTOKEN_USER user2;
    if (pid1 == pid2)
        return FALSE;
    user1 = GetProcessUser(pid1);
    user2 = GetProcessUser(pid2);
    if ((user1 == NULL) != (user2 == NULL) ||
        (user1 != NULL && user2 != NULL && !EqualSid(user1->User.Sid, user2->User.Sid)))
        different = TRUE;
    if (user1 != NULL)
        GlobalFree(user1);
    if (user2 != NULL)
        GlobalFree(user2);
    return different;
}

typedef struct SALSHEXT_MAPPED_PAYLOAD
{
    HANDLE Mapping;
    const void* View;
    DWORD Bytes;
    UINT64 RequestId;
    UINT64 Generation;
} SALSHEXT_MAPPED_PAYLOAD;

static void CloseMappedPayload(SALSHEXT_MAPPED_PAYLOAD* payload)
{
    if (payload->View != NULL)
        UnmapViewOfFile(payload->View);
    if (payload->Mapping != NULL)
        CloseHandle(payload->Mapping);
    MyZeroMemory(payload, sizeof(*payload));
}

static BOOL OpenRequestPayload(const CSalShExtSharedMem* control,
                               SALSHEXT_MAPPED_PAYLOAD* payload)
{
    wchar_t name[256];
    const SALSHEXT_PAYLOAD_REF* ref = &control->RequestPayload;
    MyZeroMemory(payload, sizeof(*payload));
    if (ref->Key != SALSHEXT_PAYLOAD_KEY_REQUEST || ref->Reserved != 0 ||
        ref->ByteSize == 0 || ref->RequestId != control->RequestId ||
        ref->Generation != control->Generation ||
        SALSHEXT_GetPayloadName(ref->SenderProcessId, ref->RequestId, ref->Generation,
                                ref->Key, name, ARRAYSIZE(name)) == NULL)
        return FALSE;
    payload->Mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (payload->Mapping == NULL)
        return FALSE;
    payload->View = MapViewOfFile(payload->Mapping, FILE_MAP_READ, 0, 0, ref->ByteSize);
    payload->Bytes = ref->ByteSize;
    payload->RequestId = ref->RequestId;
    payload->Generation = ref->Generation;
    if (payload->View == NULL ||
        !SALSHEXT_ValidatePayload(payload->View, payload->Bytes, payload->RequestId,
                                  payload->Generation))
    {
        CloseMappedPayload(payload);
        return FALSE;
    }
    return TRUE;
}

static wchar_t* DuplicatePayloadField(const SALSHEXT_MAPPED_PAYLOAD* payload, DWORD fieldId)
{
    const wchar_t* text;
    UINT64 length;
    DWORD characterCount;
    DWORD bytes;
    wchar_t* copy;
    DWORD i;
    if (!SALSHEXT_GetPayloadField(payload->View, payload->Bytes, payload->RequestId,
                                  payload->Generation, fieldId, &text, &length) ||
        length > (MAXDWORD / sizeof(wchar_t)) - 1)
        return NULL;
    characterCount = (DWORD)length + 1;
    bytes = characterCount * sizeof(wchar_t);
    copy = (wchar_t*)GlobalAlloc(GMEM_FIXED, bytes);
    if (copy == NULL)
        return NULL;
    for (i = 0; i < (DWORD)length; i++)
        copy[i] = text[i];
    copy[(DWORD)length] = L'\0';
    return copy;
}

static wchar_t* WidenAnsiPath(const char* path)
{
    int count;
    wchar_t* result;
    if (path == NULL)
        return NULL;
    count = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
    if (count <= 0 || (SIZE_T)count > MAXDWORD / sizeof(wchar_t))
        return NULL;
    result = (wchar_t*)GlobalAlloc(GMEM_FIXED, (SIZE_T)count * sizeof(wchar_t));
    if (result == NULL || MultiByteToWideChar(CP_ACP, 0, path, -1, result, count) == 0)
    {
        if (result != NULL)
            GlobalFree(result);
        return NULL;
    }
    return result;
}

static wchar_t* GetShortPathOwned(const wchar_t* path)
{
    DWORD needed = GetShortPathNameW(path, NULL, 0);
    wchar_t* result;
    DWORD written;
    if (needed == 0 || needed >= MAXDWORD / sizeof(wchar_t))
        return NULL;
    result = (wchar_t*)GlobalAlloc(GMEM_FIXED, (SIZE_T)needed * sizeof(wchar_t));
    if (result == NULL)
        return NULL;
    written = GetShortPathNameW(path, result, needed);
    if (written == 0 || written >= needed)
    {
        GlobalFree(result);
        return NULL;
    }
    return result;
}

static BOOL PathsReferToSameDirectory(const wchar_t* left, const wchar_t* right)
{
    wchar_t* shortLeft;
    wchar_t* shortRight;
    BOOL equal;
    if (left == NULL || right == NULL)
        return FALSE;
    if (lstrcmpiW(left, right) == 0)
        return TRUE;
    shortLeft = GetShortPathOwned(left);
    shortRight = GetShortPathOwned(right);
    equal = shortLeft != NULL && shortRight != NULL && lstrcmpiW(shortLeft, shortRight) == 0;
    if (shortLeft != NULL)
        GlobalFree(shortLeft);
    if (shortRight != NULL)
        GlobalFree(shortRight);
    return equal;
}

static wchar_t* DestinationDirectoryOwned(const wchar_t* destination)
{
    const wchar_t* slash;
    SIZE_T length;
    wchar_t* result;
    if (destination == NULL)
        return NULL;
    slash = destination + lstrlenW(destination);
    while (slash > destination && slash[-1] != L'\\' && slash[-1] != L'/')
        slash--;
    length = (SIZE_T)(slash - destination);
    if (length == 0 || length >= MAXINT ||
        length > (MAXDWORD / sizeof(wchar_t)) - 1)
        return NULL;
    result = (wchar_t*)GlobalAlloc(GMEM_FIXED, (length + 1) * sizeof(wchar_t));
    if (result == NULL)
        return NULL;
    lstrcpynW(result, destination, (int)length + 1);
    return result;
}

static HANDLE PublishResponsePayload(CSalShExtSharedMem* control, const wchar_t* targetPath)
{
    SALSHEXT_PAYLOAD_INPUT input;
    DWORD bytes;
    wchar_t name[256];
    HANDLE mapping;
    void* view;
    input.FieldId = SALSHEXT_FIELD_TARGET_PATH;
    input.Text = targetPath;
    input.LengthCodeUnits = targetPath != NULL ? (UINT64)lstrlenW(targetPath) : 0;
    if (!SALSHEXT_CalculatePayloadBytes(&input, 1, &bytes) ||
        SALSHEXT_GetPayloadName(GetCurrentProcessId(), control->RequestId,
                                control->Generation, SALSHEXT_PAYLOAD_KEY_RESPONSE,
                                name, ARRAYSIZE(name)) == NULL)
        return NULL;
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, bytes, name);
    if (mapping == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mapping != NULL)
            CloseHandle(mapping);
        return NULL;
    }
    view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, bytes);
    if (view == NULL || !SALSHEXT_WritePayload(view, bytes, control->RequestId,
                                               control->Generation, &input, 1))
    {
        if (view != NULL)
            UnmapViewOfFile(view);
        CloseHandle(mapping);
        return NULL;
    }
    UnmapViewOfFile(view);
    control->ResponsePayload.SenderProcessId = GetCurrentProcessId();
    control->ResponsePayload.ByteSize = bytes;
    control->ResponsePayload.RequestId = control->RequestId;
    control->ResponsePayload.Generation = control->Generation;
    control->ResponsePayload.Key = SALSHEXT_PAYLOAD_KEY_RESPONSE;
    control->ResponsePayload.Reserved = 0;
    control->ResponseConsumedGeneration = 0;
    return mapping;
}

static void WaitForResponseConsumption(HANDLE mutex, CSalShExtSharedMem* control,
                                       UINT64 generation)
{
    DWORD count = 0;
    while (control->ResponseConsumedGeneration != generation && count++ < 50)
    {
        ReleaseMutex(mutex);
        Sleep(100);
        WaitForSingleObject(mutex, INFINITE);
        if (!SALSHEXT_IsCompatibleControl(control) || control->Generation != generation)
            break;
    }
}

static UINT CopyCallbackWide(HWND hwnd, UINT wFunc, const wchar_t* source,
                             const wchar_t* destination)
{
    wchar_t mutexName[256];
    wchar_t mappingName[256];
    wchar_t eventName[256];
    HANDLE mutex = NULL;
    HANDLE controlMapping = NULL;
    HANDLE pasteEvent = NULL;
    HANDLE responseMapping = NULL;
    CSalShExtSharedMem* control = NULL;
    SALSHEXT_MAPPED_PAYLOAD requestPayload;
    wchar_t* dragFake = NULL;
    wchar_t* pasteFake = NULL;
    wchar_t* message = NULL;
    wchar_t* target = NULL;
    HWND captureWindow = NULL;
    UINT64 responseGeneration = 0;
    UINT result = IDYES;
    BOOL showMessage = FALSE;
    MyZeroMemory(&requestPayload, sizeof(requestPayload));

    if ((wFunc != FO_MOVE && wFunc != FO_COPY) || source == NULL || destination == NULL)
        return result;
    mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE,
                       SALSHEXT_GetSharedMemMutexName(mutexName, ARRAYSIZE(mutexName)));
    if (mutex == NULL)
    {
        HANDLE legacy = OpenMutexW(SYNCHRONIZE, FALSE, SALSHEXT_V6_SHAREDMEMMUTEXNAME);
        if (legacy != NULL)
        {
            WriteToLog("CH_CopyCallback: incompatible shell-extension IPC v6 peer; v7 handoff disabled");
            CloseHandle(legacy);
        }
        return result;
    }
    WaitForSingleObject(mutex, INFINITE);
    controlMapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE,
                                      SALSHEXT_GetSharedMemName(mappingName, ARRAYSIZE(mappingName)));
    if (controlMapping == NULL)
        goto Unlock;
    control = (CSalShExtSharedMem*)MapViewOfFile(controlMapping, FILE_MAP_WRITE, 0, 0,
                                                sizeof(CSalShExtSharedMem));
    if (!SALSHEXT_IsCompatibleControl(control) || !OpenRequestPayload(control, &requestPayload))
        goto Unlock;
    {
        DWORD sessionId = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) ||
            sessionId != control->SessionId || control->ReservedIdentity != 0)
            goto Unlock;
        control->ReceiverProcessId = GetCurrentProcessId();
    }

    if ((control->StateFlags & SALSHEXT_STATE_DRAG_ACTIVE) != 0)
        dragFake = DuplicatePayloadField(&requestPayload, SALSHEXT_FIELD_DRAG_FAKE_DIR);
    if (dragFake != NULL && PathsReferToSameDirectory(dragFake, source))
    {
        result = IDNO;
        target = DestinationDirectoryOwned(destination);
        if (target != NULL)
        {
            responseMapping = PublishResponsePayload(control, target);
            if (responseMapping != NULL)
            {
                control->StateFlags |= SALSHEXT_STATE_DROP_DONE;
                control->StateFlags &= ~SALSHEXT_STATE_PASTE_DONE;
                control->Operation = wFunc == FO_COPY ? SALSHEXT_COPY : SALSHEXT_MOVE;
                captureWindow = (HWND)(DWORD_PTR)control->SalamanderMainWnd;
                responseGeneration = control->Generation;
            }
        }
        goto Unlock;
    }

    if ((control->StateFlags & SALSHEXT_STATE_PASTE_ACTIVE) != 0)
        pasteFake = DuplicatePayloadField(&requestPayload, SALSHEXT_FIELD_PASTE_FAKE_DIR);
    if (pasteFake != NULL && PathsReferToSameDirectory(pasteFake, source))
    {
        BOOL recent = GetTickCount() - control->ClipDataObjLastGetDataTime < 3000;
        if (!recent && hwnd != NULL)
        {
            DWORD targetPid = 0;
            GetWindowThreadProcessId(hwnd, &targetPid);
            recent = targetPid != 0 &&
                     AreNotProcessesOfTheSameUser(control->SalamanderMainWndPID, targetPid);
        }
        if (recent)
        {
            result = IDNO;
            if (control->SalamanderMainWndPID == GetCurrentProcessId() &&
                control->SalamanderMainWndTID == GetCurrentThreadId())
            {
                message = DuplicatePayloadField(&requestPayload,
                                                SALSHEXT_FIELD_UNABLE_TO_PASTE_SAME_THREAD);
                showMessage = TRUE;
            }
            else
            {
                DWORD count = 0;
                control->SalBusyState = 0;
                pasteEvent = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE,
                                        SALSHEXT_GetDoPasteEventName(eventName, ARRAYSIZE(eventName)));
                if (pasteEvent != NULL)
                    SetEvent(pasteEvent);
                else
                {
                    control->BlockPasteDataRelease = TRUE;
                    control->ClipDataObjLastGetDataTime = GetTickCount() - 60000;
                    PostMessageW((HWND)(DWORD_PTR)control->SalamanderMainWnd,
                                 WM_USER_SALSHEXT_PASTE, control->PostMsgIndex, 0);
                }
                while (control->SalBusyState == 0 && count++ < 50)
                {
                    ReleaseMutex(mutex);
                    Sleep(100);
                    WaitForSingleObject(mutex, INFINITE);
                    if (!SALSHEXT_IsCompatibleControl(control))
                        break;
                }
                control->PostMsgIndex++;
                if (pasteEvent == NULL)
                {
                    control->BlockPasteDataRelease = FALSE;
                    PostMessageW((HWND)(DWORD_PTR)control->SalamanderMainWnd,
                                 WM_USER_SALSHEXT_TRYRELDATA, 0, 0);
                }
                else
                {
                    ResetEvent(pasteEvent);
                    CloseHandle(pasteEvent);
                    pasteEvent = NULL;
                }
                if (control->SalBusyState == 1)
                {
                    target = DestinationDirectoryOwned(destination);
                    if (target != NULL)
                        responseMapping = PublishResponsePayload(control, target);
                    if (responseMapping != NULL)
                    {
                        responseGeneration = control->Generation;
                        control->StateFlags |= SALSHEXT_STATE_PASTE_DONE;
                        control->StateFlags &= ~SALSHEXT_STATE_DROP_DONE;
                        control->Operation = wFunc == FO_COPY ? SALSHEXT_COPY : SALSHEXT_MOVE;
                        WaitForResponseConsumption(mutex, control, responseGeneration);
                    }
                }
                else
                {
                    message = DuplicatePayloadField(&requestPayload,
                                                    SALSHEXT_FIELD_UNABLE_TO_PASTE_BUSY);
                    showMessage = TRUE;
                    result = IDCANCEL;
                }
            }
        }
    }

Unlock:
    if (control != NULL)
        UnmapViewOfFile(control);
    if (controlMapping != NULL)
        CloseHandle(controlMapping);
    ReleaseMutex(mutex);
    if (captureWindow != NULL && responseMapping != NULL)
    {
        DWORD_PTR ignored;
        SendMessageTimeoutW(captureWindow, WM_USER_SALSHEXT_CAPTUREPAYLOAD, 0, 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 5000, &ignored);
    }
    if (responseMapping != NULL)
        CloseHandle(responseMapping);
    CloseHandle(mutex);
    CloseMappedPayload(&requestPayload);
    if (showMessage && message != NULL)
        MessageBoxW(hwnd, message, L"Sally", MB_OK | MB_ICONEXCLAMATION);
    if (dragFake != NULL)
        GlobalFree(dragFake);
    if (pasteFake != NULL)
        GlobalFree(pasteFake);
    if (message != NULL)
        GlobalFree(message);
    if (target != NULL)
        GlobalFree(target);
    return result;
}

STDMETHODIMP_(UINT)
CH_CopyCallback(THIS_ HWND hwnd, UINT wFunc, UINT wFlags,
                LPCSTR pszSrcFile, DWORD dwSrcAttribs,
                LPCSTR pszDestFile, DWORD dwDestAttribs)
{
    wchar_t* source = WidenAnsiPath(pszSrcFile);
    wchar_t* destination = WidenAnsiPath(pszDestFile);
    UINT result = IDYES;
    if (source != NULL && destination != NULL)
        result = CopyCallbackWide(hwnd, wFunc, source, destination);
    if (source != NULL)
        GlobalFree(source);
    if (destination != NULL)
        GlobalFree(destination);
    return result;
}

#undef INTERFACE
#define INTERFACE ImpICopyHookW

STDMETHODIMP CHW_QueryInterface(THIS_ REFIID riid, void** ppv)
{
    CopyHookW* ch = (CopyHookW*)This;
    return ch->m_pObj->lpVtbl->QueryInterface((IShellExt*)ch->m_pObj, riid, ppv);
}

STDMETHODIMP_(ULONG) CHW_AddRef(THIS)
{
    CopyHookW* ch = (CopyHookW*)This;
    return ch->m_pObj->lpVtbl->AddRef((IShellExt*)ch->m_pObj);
}

STDMETHODIMP_(ULONG) CHW_Release(THIS)
{
    CopyHookW* ch = (CopyHookW*)This;
    return ch->m_pObj->lpVtbl->Release((IShellExt*)ch->m_pObj);
}

STDMETHODIMP_(UINT)
CHW_CopyCallback(THIS_ HWND hwnd, UINT wFunc, UINT wFlags,
                 LPCWSTR pszSrcFile, DWORD dwSrcAttribs,
                 LPCWSTR pszDestFile, DWORD dwDestAttribs)
{
    return CopyCallbackWide(hwnd, wFunc, pszSrcFile, pszDestFile);
}
