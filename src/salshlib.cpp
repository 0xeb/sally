// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "common/clipboard/FakeRealPathPayload.h"

#include "dialogs.h"
#include "ui/IPrompter.h"
#include "common/IFileSystem.h"
#include "common/unicode/helpers.h"
#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "zip.h"
#include "pack.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"

// mutex for access to shared memory
HANDLE SalShExtSharedMemMutex = NULL;
// shared memory - see CSalShExtSharedMem structure
HANDLE SalShExtSharedMem = NULL;
// event for sending request to perform Paste in source Salamander (used only in Vista+)
HANDLE SalShExtDoPasteEvent = NULL;
// mapped shared memory - see CSalShExtSharedMem structure
CSalShExtSharedMem* SalShExtSharedMemView = NULL;

static HANDLE SalShExtRequestPayload = NULL;
static UINT64 SalShExtNextRequestId = 0;
static std::wstring SalShExtCapturedTarget;
static UINT64 SalShExtCapturedRequestId = 0;
static UINT64 SalShExtCapturedGeneration = 0;

// TRUE if SalShExt/SalamExt/SalExtX86/SalExtX64.DLL was successfully registered or was already
// registered (also checks file)
BOOL SalShExtRegistered = FALSE;

// ultimate hack: we need to find out which window the Drop will go to, we determine this
// in GetData based on mouse position, this variable holds the last test result
HWND LastWndFromGetData = NULL;

// ultimate hack: we need to find out which window the Paste will go to, we determine this
// in GetData based on foreground window, this variable holds the last test result
HWND LastWndFromPasteGetData = NULL;

BOOL OurDataOnClipboard = FALSE; // TRUE = our data-object is on clipboard (copy&paste from archive)

// data for Paste from clipboard stored inside "source" Salamander
CSalShExtPastedData SalShExtPastedData;

//*****************************************************************************

void InitSalShLib()
{
    CALL_STACK_MESSAGE1("InitSalShLib()");
    wchar_t sharedMemMutexName[256];
    wchar_t sharedMemName[256];
    const wchar_t* mutexName = SALSHEXT_GetSharedMemMutexName(sharedMemMutexName, _countof(sharedMemMutexName));
    const wchar_t* mappingName = SALSHEXT_GetSharedMemName(sharedMemName, _countof(sharedMemName));
    PSID psidEveryone;
    PACL paclNewDacl;
    SECURITY_ATTRIBUTES sa;
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES* saPtr = CreateAccessableSecurityAttributes(&sa, &sd, GENERIC_ALL, &psidEveryone, &paclNewDacl);

    // W-suffixed explicitly. mutexName/mappingName are wide (the IPC-name
    // helpers went wide in shexreg_ipc_names.h), and this build does not define UNICODE, so
    // the unsuffixed macros resolve to CreateMutexA/OpenMutexA and would read a wchar_t*
    // as char* - a garbage object name, with no compile error to say so.
    SalShExtSharedMemMutex = HANDLES_Q(CreateMutexW(saPtr, FALSE, mutexName));
    if (SalShExtSharedMemMutex == NULL)
        SalShExtSharedMemMutex = HANDLES_Q(OpenMutexW(SYNCHRONIZE, FALSE, mutexName));
    if (SalShExtSharedMemMutex != NULL)
    {
        WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
        SalShExtSharedMem = HANDLES_Q(CreateFileMappingW(INVALID_HANDLE_VALUE, saPtr, PAGE_READWRITE, // FIXME_X64 nepredavame x86/x64 nekompatibilni data?
                                                        0, sizeof(CSalShExtSharedMem),
                                                        mappingName));
        BOOL created;
        if (SalShExtSharedMem == NULL)
        {
            SalShExtSharedMem = HANDLES_Q(OpenFileMappingW(FILE_MAP_WRITE, FALSE, mappingName));
            created = FALSE;
        }
        else
        {
            created = (GetLastError() != ERROR_ALREADY_EXISTS);
        }

        if (SalShExtSharedMem != NULL)
        {
            SalShExtSharedMemView = (CSalShExtSharedMem*)HANDLES(MapViewOfFile(
                SalShExtSharedMem, FILE_MAP_WRITE, 0, 0, sizeof(CSalShExtSharedMem)));
            if (SalShExtSharedMemView != NULL)
            {
                if (created)
                {
                    memset(SalShExtSharedMemView, 0, sizeof(CSalShExtSharedMem));
                    SalShExtSharedMemView->Magic = SALSHEXT_CONTROL_MAGIC;
                    SalShExtSharedMemView->Version = SALSHEXT_CONTROL_VERSION;
                    SalShExtSharedMemView->Size = sizeof(CSalShExtSharedMem);
                }
                else if (!SALSHEXT_IsCompatibleControl(SalShExtSharedMemView))
                {
                    TRACE_E("InitSalShLib(): shell-extension IPC v7 control record is incompatible; handoff disabled");
                    HANDLES(UnmapViewOfFile(SalShExtSharedMemView));
                    SalShExtSharedMemView = NULL;
                }
            }
            else
                TRACE_E("InitSalShLib(): unable to map view of shared memory!");
        }
        else
            TRACE_E("InitSalShLib(): unable to create shared memory!");
        ReleaseMutex(SalShExtSharedMemMutex);
    }
    else
        TRACE_E("InitSalShLib(): unable to create mutex object for access to shared memory!");

    if (psidEveryone != NULL)
        FreeSid(psidEveryone);
    if (paclNewDacl != NULL)
        LocalFree(paclNewDacl);
}

void ReleaseSalShLib()
{
    CALL_STACK_MESSAGE1("ReleaseSalShLib()");
    if (OurDataOnClipboard)
    {
        OleSetClipboard(NULL);      // remove our data-object from clipboard
        OurDataOnClipboard = FALSE; // theoretically unnecessary (should be set in Release() of fakeDataObject)
    }
    if (SalShExtRequestPayload != NULL)
        HANDLES(CloseHandle(SalShExtRequestPayload));
    SalShExtRequestPayload = NULL;
    if (SalShExtSharedMemView != NULL)
        HANDLES(UnmapViewOfFile(SalShExtSharedMemView));
    SalShExtSharedMemView = NULL;
    if (SalShExtSharedMem != NULL)
        HANDLES(CloseHandle(SalShExtSharedMem));
    SalShExtSharedMem = NULL;
    if (SalShExtSharedMemMutex != NULL)
        HANDLES(CloseHandle(SalShExtSharedMemMutex));
    SalShExtSharedMemMutex = NULL;
}

static UINT64 SalShExtAllocateRequestId()
{
    if (SalShExtNextRequestId == 0)
        SalShExtNextRequestId = ((UINT64)GetCurrentProcessId() << 32) | GetTickCount();
    return ++SalShExtNextRequestId;
}

static BOOL SalShExtBeginRequestLocked(DWORD stateFlag, const SALSHEXT_PAYLOAD_INPUT* fields,
                                       DWORD fieldCount)
{
    DWORD byteSize;
    wchar_t payloadName[256];
    HANDLE mapping;
    void* view;
    UINT64 requestId;
    UINT64 generation;
    PSID everyoneSid;
    PACL payloadAcl;
    SECURITY_ATTRIBUTES securityAttributes;
    SECURITY_DESCRIPTOR securityDescriptor;
    SECURITY_ATTRIBUTES* security;

    if (SalShExtSharedMemView == NULL || !SALSHEXT_IsCompatibleControl(SalShExtSharedMemView) ||
        !SALSHEXT_CalculatePayloadBytes(fields, fieldCount, &byteSize))
        return FALSE;

    requestId = SalShExtAllocateRequestId();
    generation = SalShExtSharedMemView->Generation + 1;
    if (generation == 0)
        generation = 1;
    if (SALSHEXT_GetPayloadName(GetCurrentProcessId(), requestId, generation,
                                SALSHEXT_PAYLOAD_KEY_REQUEST, payloadName,
                                _countof(payloadName)) == NULL)
        return FALSE;

    security = CreateAccessableSecurityAttributes(&securityAttributes, &securityDescriptor,
                                                  GENERIC_ALL, &everyoneSid, &payloadAcl);
    mapping = HANDLES_Q(CreateFileMappingW(INVALID_HANDLE_VALUE, security, PAGE_READWRITE, 0,
                                           byteSize, payloadName));
    if (everyoneSid != NULL)
        FreeSid(everyoneSid);
    if (payloadAcl != NULL)
        LocalFree(payloadAcl);
    if (mapping == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (mapping != NULL)
            HANDLES(CloseHandle(mapping));
        TRACE_E("SalShExtBeginRequestLocked(): unable to create a unique v7 payload mapping");
        return FALSE;
    }
    view = HANDLES(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, byteSize));
    if (view == NULL || !SALSHEXT_WritePayload(view, byteSize, requestId, generation,
                                               fields, fieldCount))
    {
        if (view != NULL)
            HANDLES(UnmapViewOfFile(view));
        HANDLES(CloseHandle(mapping));
        return FALSE;
    }
    HANDLES(UnmapViewOfFile(view));

    if (SalShExtRequestPayload != NULL)
        HANDLES(CloseHandle(SalShExtRequestPayload));
    SalShExtRequestPayload = mapping;
    SalShExtCapturedTarget.clear();
    SalShExtCapturedRequestId = 0;
    SalShExtCapturedGeneration = 0;
    SalShExtSharedMemView->StateFlags = stateFlag;
    SalShExtSharedMemView->RequestId = requestId;
    SalShExtSharedMemView->Generation = generation;
    SalShExtSharedMemView->Operation = SALSHEXT_NONE;
    SalShExtSharedMemView->SalamanderMainWndPID = GetCurrentProcessId();
    SalShExtSharedMemView->SalamanderMainWndTID = GetCurrentThreadId();
    ProcessIdToSessionId(GetCurrentProcessId(), &SalShExtSharedMemView->SessionId);
    SalShExtSharedMemView->InitiatorIntegrityRid = 0;
    GetProcessIntegrityLevel(&SalShExtSharedMemView->InitiatorIntegrityRid);
    SalShExtSharedMemView->ReceiverProcessId = 0;
    SalShExtSharedMemView->ReservedIdentity = 0;
    SalShExtSharedMemView->SalamanderMainWnd =
        MainWindow != NULL ? (UINT64)(DWORD_PTR)MainWindow->HWindow : 0;
    SalShExtSharedMemView->ResponseConsumedGeneration = 0;
    memset(&SalShExtSharedMemView->ResponsePayload, 0,
           sizeof(SalShExtSharedMemView->ResponsePayload));
    SalShExtSharedMemView->RequestPayload.SenderProcessId = GetCurrentProcessId();
    SalShExtSharedMemView->RequestPayload.ByteSize = byteSize;
    SalShExtSharedMemView->RequestPayload.RequestId = requestId;
    SalShExtSharedMemView->RequestPayload.Generation = generation;
    SalShExtSharedMemView->RequestPayload.Key = SALSHEXT_PAYLOAD_KEY_REQUEST;
    SalShExtSharedMemView->RequestPayload.Reserved = 0;
    return TRUE;
}

BOOL SalShExtBeginDragRequestLocked(const std::wstring& fakeDirectory)
{
    SALSHEXT_PAYLOAD_INPUT field = {SALSHEXT_FIELD_DRAG_FAKE_DIR, fakeDirectory.c_str(),
                                    (UINT64)fakeDirectory.size()};
    return SalShExtBeginRequestLocked(SALSHEXT_STATE_DRAG_ACTIVE, &field, 1);
}

BOOL SalShExtBeginPasteRequestLocked(const std::wstring& fakeDirectory,
                                     const std::wstring& sameThreadMessage,
                                     const std::wstring& busyMessage)
{
    SALSHEXT_PAYLOAD_INPUT fields[3] = {
        {SALSHEXT_FIELD_PASTE_FAKE_DIR, fakeDirectory.c_str(), (UINT64)fakeDirectory.size()},
        {SALSHEXT_FIELD_UNABLE_TO_PASTE_SAME_THREAD, sameThreadMessage.c_str(),
         (UINT64)sameThreadMessage.size()},
        {SALSHEXT_FIELD_UNABLE_TO_PASTE_BUSY, busyMessage.c_str(),
         (UINT64)busyMessage.size()}};
    return SalShExtBeginRequestLocked(SALSHEXT_STATE_PASTE_ACTIVE, fields, 3);
}

void SalShExtEndRequestLocked(DWORD stateFlag)
{
    if (SalShExtSharedMemView != NULL)
    {
        SalShExtSharedMemView->StateFlags &= ~stateFlag;
        if ((SalShExtSharedMemView->StateFlags &
             (SALSHEXT_STATE_DRAG_ACTIVE | SALSHEXT_STATE_PASTE_ACTIVE)) == 0)
        {
            memset(&SalShExtSharedMemView->RequestPayload, 0,
                   sizeof(SalShExtSharedMemView->RequestPayload));
            memset(&SalShExtSharedMemView->ResponsePayload, 0,
                   sizeof(SalShExtSharedMemView->ResponsePayload));
            if (SalShExtRequestPayload != NULL)
                HANDLES(CloseHandle(SalShExtRequestPayload));
            SalShExtRequestPayload = NULL;
        }
    }
}

BOOL SalShExtReadResponseLocked(std::wstring& targetPath)
{
    if (SalShExtSharedMemView != NULL &&
        SalShExtCapturedRequestId == SalShExtSharedMemView->RequestId &&
        SalShExtCapturedGeneration == SalShExtSharedMemView->Generation)
    {
        targetPath = SalShExtCapturedTarget;
        return TRUE;
    }
    const SALSHEXT_PAYLOAD_REF ref = SalShExtSharedMemView != NULL
                                        ? SalShExtSharedMemView->ResponsePayload
                                        : SALSHEXT_PAYLOAD_REF{};
    wchar_t payloadName[256];
    HANDLE mapping;
    const void* view;
    const wchar_t* text;
    UINT64 length;
    BOOL ok = FALSE;

    targetPath.clear();
    if (SalShExtSharedMemView == NULL || ref.Key != SALSHEXT_PAYLOAD_KEY_RESPONSE ||
        ref.Reserved != 0 || ref.ByteSize == 0 || ref.RequestId != SalShExtSharedMemView->RequestId ||
        ref.Generation != SalShExtSharedMemView->Generation ||
        SALSHEXT_GetPayloadName(ref.SenderProcessId, ref.RequestId, ref.Generation, ref.Key,
                                payloadName, _countof(payloadName)) == NULL)
        return FALSE;
    mapping = HANDLES_Q(OpenFileMappingW(FILE_MAP_READ, FALSE, payloadName));
    if (mapping == NULL)
        return FALSE;
    view = HANDLES(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, ref.ByteSize));
    if (view != NULL)
    {
        if (SALSHEXT_GetPayloadField(view, ref.ByteSize, ref.RequestId, ref.Generation,
                                     SALSHEXT_FIELD_TARGET_PATH, &text, &length) &&
            length <= (UINT64)targetPath.max_size())
        {
            targetPath.assign(text, (size_t)length);
            SalShExtSharedMemView->ResponseConsumedGeneration = ref.Generation;
            ok = TRUE;
        }
        HANDLES(UnmapViewOfFile(view));
    }
    HANDLES(CloseHandle(mapping));
    return ok;
}

BOOL SalShExtPublishLocalResponseLocked(const std::wstring& targetPath)
{
    if (SalShExtSharedMemView == NULL ||
        (SalShExtSharedMemView->StateFlags & SALSHEXT_STATE_DRAG_ACTIVE) == 0)
        return FALSE;
    SalShExtCapturedTarget = targetPath;
    SalShExtCapturedRequestId = SalShExtSharedMemView->RequestId;
    SalShExtCapturedGeneration = SalShExtSharedMemView->Generation;
    SalShExtSharedMemView->ResponseConsumedGeneration = SalShExtSharedMemView->Generation;
    return TRUE;
}

void SalShExtCaptureResponse()
{
    if (SalShExtSharedMemMutex == NULL || SalShExtSharedMemView == NULL)
        return;
    WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
    std::wstring target;
    if (SalShExtReadResponseLocked(target))
    {
        SalShExtCapturedTarget = std::move(target);
        SalShExtCapturedRequestId = SalShExtSharedMemView->RequestId;
        SalShExtCapturedGeneration = SalShExtSharedMemView->Generation;
    }
    ReleaseMutex(SalShExtSharedMemMutex);
}

BOOL IsFakeDataObject(IDataObject* pDataObject, int* fakeType, std::wstring* srcFSPath)
{
    CALL_STACK_MESSAGE1("IsFakeDataObject()");
    if (fakeType != NULL)
        *fakeType = 0;
    if (srcFSPath != NULL)
        srcFSPath->clear();

    FORMATETC formatEtc;
    formatEtc.cfFormat = RegisterClipboardFormatA(SALCF_FAKE_REALPATH);
    formatEtc.ptd = NULL;
    formatEtc.dwAspect = DVASPECT_CONTENT;
    formatEtc.lindex = -1;
    formatEtc.tymed = TYMED_HGLOBAL;

    STGMEDIUM stgMedium;
    stgMedium.tymed = TYMED_HGLOBAL;
    stgMedium.hGlobal = NULL;
    stgMedium.pUnkForRelease = NULL;

    if (pDataObject != NULL && pDataObject->GetData(&formatEtc, &stgMedium) == S_OK)
    {
        if (stgMedium.tymed != TYMED_HGLOBAL || stgMedium.hGlobal != NULL)
            ReleaseStgMedium(&stgMedium);

        if (fakeType != NULL || srcFSPath != NULL)
        {
            formatEtc.cfFormat = RegisterClipboardFormatA(SALCF_FAKE_SRCTYPE);
            formatEtc.ptd = NULL;
            formatEtc.dwAspect = DVASPECT_CONTENT;
            formatEtc.lindex = -1;
            formatEtc.tymed = TYMED_HGLOBAL;

            stgMedium.tymed = TYMED_HGLOBAL;
            stgMedium.hGlobal = NULL;
            stgMedium.pUnkForRelease = NULL;

            BOOL isFS = FALSE;
            if (pDataObject->GetData(&formatEtc, &stgMedium) == S_OK)
            {
                if (stgMedium.tymed == TYMED_HGLOBAL && stgMedium.hGlobal != NULL)
                {
                    int* data = (int*)HANDLES(GlobalLock(stgMedium.hGlobal));
                    if (data != NULL)
                    {
                        isFS = *data == 2;
                        if (fakeType != NULL)
                            *fakeType = *data;
                        HANDLES(GlobalUnlock(stgMedium.hGlobal));
                    }
                }
                if (stgMedium.tymed != TYMED_HGLOBAL || stgMedium.hGlobal != NULL)
                    ReleaseStgMedium(&stgMedium);
            }

            if (isFS && srcFSPath != NULL)
            {
                formatEtc.cfFormat = RegisterClipboardFormatA(SALCF_FAKE_SRCFSPATH);
                formatEtc.ptd = NULL;
                formatEtc.dwAspect = DVASPECT_CONTENT;
                formatEtc.lindex = -1;
                formatEtc.tymed = TYMED_HGLOBAL;

                stgMedium.tymed = TYMED_HGLOBAL;
                stgMedium.hGlobal = NULL;
                stgMedium.pUnkForRelease = NULL;
                if (pDataObject->GetData(&formatEtc, &stgMedium) == S_OK)
                {
                    if (stgMedium.tymed == TYMED_HGLOBAL && stgMedium.hGlobal != NULL)
                    {
                        wchar_t* data = (wchar_t*)HANDLES(GlobalLock(stgMedium.hGlobal));
                        if (data != NULL)
                        {
                            const size_t maxChars = GlobalSize(stgMedium.hGlobal) / sizeof(wchar_t);
                            size_t length = 0;
                            while (length < maxChars && data[length] != 0)
                                ++length;
                            srcFSPath->assign(data, length);
                            HANDLES(GlobalUnlock(stgMedium.hGlobal));
                        }
                    }
                    if (stgMedium.tymed != TYMED_HGLOBAL || stgMedium.hGlobal != NULL)
                        ReleaseStgMedium(&stgMedium);
                }
            }
        }
        return TRUE;
    }
    return FALSE;
}

BOOL GetFakeDataObjectRealPath(IDataObject* pDataObject, std::wstring& realPath, wchar_t* itemKind,
                               BOOL* formatPresent)
{
    realPath.clear();
    if (itemKind != NULL)
        *itemKind = L'\0';
    if (formatPresent != NULL)
        *formatPresent = FALSE;
    if (pDataObject == NULL)
        return FALSE;

    FORMATETC formatEtc = {};
    formatEtc.cfFormat = RegisterClipboardFormatA(SALCF_FAKE_REALPATH);
    formatEtc.dwAspect = DVASPECT_CONTENT;
    formatEtc.lindex = -1;
    formatEtc.tymed = TYMED_HGLOBAL;

    STGMEDIUM medium = {};
    if (pDataObject->GetData(&formatEtc, &medium) != S_OK)
        return FALSE;

    if (formatPresent != NULL)
        *formatPresent = TRUE;

    BOOL result = FALSE;
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal != NULL)
    {
        const SIZE_T bytes = GlobalSize(medium.hGlobal);
        const wchar_t* payload = static_cast<const wchar_t*>(HANDLES(GlobalLock(medium.hGlobal)));
        if (payload != NULL)
        {
            const size_t capacity = bytes / sizeof(wchar_t);
            result = sally::clipboard::TryParseFakeRealPath(payload, capacity,
                                                             realPath, itemKind);
            HANDLES(GlobalUnlock(medium.hGlobal));
        }
    }
    ReleaseStgMedium(&medium);
    return result;
}

//
//*****************************************************************************
// CFakeDragDropDataObject
//

STDMETHODIMP CFakeDragDropDataObject::QueryInterface(REFIID iid, void** ppv)
{
    if (iid == IID_IUnknown || iid == IID_IDataObject)
    {
        *ppv = this;
        AddRef();
        return NOERROR;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
}

STDMETHODIMP CFakeDragDropDataObject::GetData(FORMATETC* formatEtc, STGMEDIUM* medium)
{
    CALL_STACK_MESSAGE1("CFakeDragDropDataObject::GetData()");
    // TRACE_I("CFakeDragDropDataObject::GetData():" << formatEtc->cfFormat);
    if (formatEtc == NULL || medium == NULL)
        return E_INVALIDARG;

    POINT pt;
    GetCursorPos(&pt);
    LastWndFromGetData = WindowFromPoint(pt);

    if (formatEtc->cfFormat == CFSalFakeRealPath && (formatEtc->tymed & TYMED_HGLOBAL))
    {
        HGLOBAL dataDup = NULL; // create copy of RealPath
        const SIZE_T size = (RealPath.length() + 1) * sizeof(wchar_t);
        dataDup = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size));
        if (dataDup != NULL)
        {
            void* ptr1 = HANDLES(GlobalLock(dataDup));
            if (ptr1 != NULL)
            {
                memcpy(ptr1, RealPath.c_str(), size);
                HANDLES(GlobalUnlock(dataDup));
            }
            else
            {
                NOHANDLES(GlobalFree(dataDup));
                dataDup = NULL;
            }
        }
        if (dataDup != NULL) // we have data, store it to medium and return
        {
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = dataDup;
            medium->pUnkForRelease = NULL;
            return S_OK;
        }
        else
            return E_UNEXPECTED;
    }
    else
    {
        if (formatEtc->cfFormat == CFSalFakeSrcType && (formatEtc->tymed & TYMED_HGLOBAL))
        {
            HGLOBAL dataDup = NULL;
            dataDup = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(int)));
            if (dataDup != NULL)
            {
                BOOL ok = FALSE;
                int* ptr1 = (int*)HANDLES(GlobalLock(dataDup));
                if (ptr1 != NULL)
                {
                    *ptr1 = SrcType;
                    ok = TRUE;
                }
                if (ptr1 != NULL)
                    HANDLES(GlobalUnlock(dataDup));
                if (!ok)
                {
                    NOHANDLES(GlobalFree(dataDup));
                    dataDup = NULL;
                }
            }
            if (dataDup != NULL) // we have data, store it to medium and return
            {
                medium->tymed = TYMED_HGLOBAL;
                medium->hGlobal = dataDup;
                medium->pUnkForRelease = NULL;
                return S_OK;
            }
            else
                return E_UNEXPECTED;
        }
        else
        {
            if (formatEtc->cfFormat == CFSalFakeSrcFSPath && (formatEtc->tymed & TYMED_HGLOBAL))
            {
                HGLOBAL dataDup = NULL; // create copy of SrcFSPath
                const SIZE_T size = (SrcFSPath.length() + 1) * sizeof(wchar_t);
                dataDup = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size));
                if (dataDup != NULL)
                {
                    void* ptr1 = HANDLES(GlobalLock(dataDup));
                    if (ptr1 != NULL)
                    {
                        memcpy(ptr1, SrcFSPath.c_str(), size);
                        HANDLES(GlobalUnlock(dataDup));
                    }
                    else
                    {
                        NOHANDLES(GlobalFree(dataDup));
                        dataDup = NULL;
                    }
                }
                if (dataDup != NULL) // we have data, store it to medium and return
                {
                    medium->tymed = TYMED_HGLOBAL;
                    medium->hGlobal = dataDup;
                    medium->pUnkForRelease = NULL;
                    return S_OK;
                }
                else
                    return E_UNEXPECTED;
            }
            else
                return WinDataObject->GetData(formatEtc, medium);
        }
    }
}

//
//*****************************************************************************
// CFakeCopyPasteDataObject
//

STDMETHODIMP CFakeCopyPasteDataObject::QueryInterface(REFIID iid, void** ppv)
{
    //  TRACE_I("QueryInterface");
    if (iid == IID_IUnknown || iid == IID_IDataObject)
    {
        *ppv = this;
        AddRef();
        return NOERROR;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
}

STDMETHODIMP_(ULONG)
CFakeCopyPasteDataObject::Release(void)
{
    CALL_STACK_MESSAGE1("CFakeCopyPasteDataObject::Release()");
    //  TRACE_I("CFakeCopyPasteDataObject::Release(): " << RefCount - 1);
    if (--RefCount == 0)
    {
        OurDataOnClipboard = FALSE;

        if (CutOrCopyDone) // if error occurred during cut/copy, waiting is pointless and cleanup will be done elsewhere
        {
            //      TRACE_I("CFakeCopyPasteDataObject::Release(): deleting clipfake directory!");

            // now we can cancel "paste" in shared memory, clean up fake-dir and release data-object
            if (SalShExtSharedMemView != NULL) // store time to shared memory (to distinguish between paste and other copy/move of fake-dir)
            {
                //        TRACE_I("CFakeCopyPasteDataObject::Release(): DoPasteFromSalamander = FALSE");
                WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                SalShExtEndRequestLocked(SALSHEXT_STATE_PASTE_ACTIVE);
                ReleaseMutex(SalShExtSharedMemMutex);
            }
            //      TRACE_I("CFakeCopyPasteDataObject::Release(): removedir");
            const size_t slash = FakeDir.find_last_of(L'\\');
            if (slash != std::wstring::npos && FakeDir.compare(slash + 1, std::wstring::npos, L"CLIPFAKE") == 0)
            { // just to be safe, check that we really delete only fake-dir
                const std::wstring dir = FakeDir.substr(0, slash);
                RemoveTemporaryDirW(dir.c_str());
            }
            //      TRACE_I("CFakeCopyPasteDataObject::Release(): posting WM_USER_SALSHEXT_TRYRELDATA");
            if (MainWindow != NULL)
                PostMessage(MainWindow->HWindow, WM_USER_SALSHEXT_TRYRELDATA, 0, 0); // try to release data (if not locked or blocked)
        }

        delete this;
        return 0; // must not touch the object, it no longer exists
    }
    return RefCount;
}

STDMETHODIMP CFakeCopyPasteDataObject::GetData(FORMATETC* formatEtc, STGMEDIUM* medium)
{
    CALL_STACK_MESSAGE1("CFakeCopyPasteDataObject::GetData()");
    //  char buf[300];
    //  if (!GetClipboardFormatName(formatEtc->cfFormat, buf, 300)) buf[0] = 0;
    //  TRACE_I("CFakeCopyPasteDataObject::GetData():" << formatEtc->cfFormat << " (" << buf << ")");
    if (formatEtc == NULL || medium == NULL)
        return E_INVALIDARG;
    if (formatEtc->cfFormat == CFSalFakeRealPath && (formatEtc->tymed & TYMED_HGLOBAL))
    {
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = NULL;
        medium->pUnkForRelease = NULL;
        return S_OK; // return S_OK to pass the test in IsFakeDataObject() function
    }
    else
    {
        if (formatEtc->cfFormat == CFIdList)
        { // Paste to Explorer uses this format, we don't care about others (they won't use copy-hook anyway)
            // fixes Win98 problem: when Copy to clipboard from Explorer, GetData is called on existing
            // clipboard object, only then it's released and replaced with new object from Explorer (problem is
            // 2 second timeout due to waiting for copy-hook call - we always expect it after GetData)
            DWORD ti = GetTickCount();
            if (ti - LastGetDataCallTime >= 100) // optimization: store new time only when changed by at least 100ms
            {
                LastGetDataCallTime = ti;
                if (SalShExtSharedMemView != NULL) // store time to shared memory (to distinguish between paste and other copy/move of fake-dir)
                {
                    WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                    SalShExtSharedMemView->ClipDataObjLastGetDataTime = ti;
                    ReleaseMutex(SalShExtSharedMemMutex);
                }
            }

            LastWndFromPasteGetData = GetForegroundWindow();
        }
        return WinDataObject->GetData(formatEtc, medium);
    }
}

//
//*****************************************************************************
// CSalShExtPastedData
//

CSalShExtPastedData::CSalShExtPastedData()
{
    DataID = -1;
    Lock = FALSE;
    ArchiveFileNameW.clear();
    PathInArchive.clear();
    StoredArchiveDir = NULL;
    memset(&StoredArchiveDate, 0, sizeof(StoredArchiveDate));
    StoredArchiveSize.Set(0, 0);
}

CSalShExtPastedData::~CSalShExtPastedData()
{
    if (StoredArchiveDir != NULL)
        TRACE_E("CSalShExtPastedData::~CSalShExtPastedData(): unexpected situation: StoredArchiveDir is not empty!");
    Clear();
}

BOOL CSalShExtPastedData::SetData(const wchar_t* archiveFileName, const wchar_t* pathInArchive, CFilesArray* files,
                                  CFilesArray* dirs, BOOL namesAreCaseSensitive, int* selIndexes,
                                  int selIndexesCount)
{
    CALL_STACK_MESSAGE1("CSalShExtPastedData::SetData()");

    Clear();

    LastWndFromPasteGetData = NULL; // for first Paste we null it here

    ArchiveFileNameW = archiveFileName != NULL ? archiveFileName : L"";
    PathInArchive = pathInArchive != NULL ? pathInArchive : L"";
    SelFilesAndDirs.SetCaseSensitive(namesAreCaseSensitive);
    int i;
    for (i = 0; i < selIndexesCount; i++)
    {
        int index = selIndexes[i];
        if (index < dirs->Count) // it's a directory
        {
            if (!SelFilesAndDirs.Add(TRUE, dirs->At(index).Name))
                break;
        }
        else // it's a file
        {
            if (!SelFilesAndDirs.Add(FALSE, files->At(index - dirs->Count).Name))
                break;
        }
    }
    if (i < selIndexesCount) // out of memory error
    {
        Clear();
        return FALSE;
    }
    else
        return TRUE;
}

void CSalShExtPastedData::Clear()
{
    CALL_STACK_MESSAGE1("CSalShExtPastedData::Clear()");
    //  TRACE_I("CSalShExtPastedData::Clear()");
    DataID = -1;
    ArchiveFileNameW.clear();
    PathInArchive.clear();
    SelFilesAndDirs.Clear();
    ReleaseStoredArchiveData();
}

void CSalShExtPastedData::ReleaseStoredArchiveData()
{
    CALL_STACK_MESSAGE1("CSalShExtPastedData::ReleaseStoredArchiveData()");

    if (StoredArchiveDir != NULL)
    {
        if (StoredPluginData.NotEmpty())
        {
            // release plugin data for individual files and directories
            BOOL releaseFiles = StoredPluginData.CallReleaseForFiles();
            BOOL releaseDirs = StoredPluginData.CallReleaseForDirs();
            if (releaseFiles || releaseDirs)
                StoredArchiveDir->ReleasePluginData(StoredPluginData, releaseFiles, releaseDirs);

            // release StoredPluginData interface
            CPluginInterfaceEncapsulation plugin(StoredPluginData.GetPluginInterface(), StoredPluginData.GetBuiltForVersion());
            plugin.ReleasePluginDataInterface(StoredPluginData.GetInterface());
        }
        StoredArchiveDir->Clear(NULL); // release "standard" (Salamander's) listing data
        delete StoredArchiveDir;
    }
    StoredArchiveDir = NULL;
    StoredPluginData.Init(NULL, NULL, NULL, NULL, 0);
}

BOOL CSalShExtPastedData::WantData(const wchar_t* archiveFileName, CSalamanderDirectory* archiveDir,
                                   CPluginDataInterfaceEncapsulation pluginData,
                                   FILETIME archiveDate, CQuadWord archiveSize)
{
    CALL_STACK_MESSAGE1("CSalShExtPastedData::WantData()");

    if (!Lock /* shouldn't happen, but we protect ourselves */ &&
        archiveFileName != NULL && _wcsicmp(ArchiveFileNameW.c_str(), archiveFileName) == 0 &&
        archiveSize != CQuadWord(-1, -1) && // corrupted date&time stamp indicates archive that needs to be reloaded
        (!pluginData.NotEmpty() || pluginData.CanBeCopiedToClipboard()))
    {
        ReleaseStoredArchiveData();
        StoredArchiveDir = archiveDir;
        StoredPluginData = pluginData;
        StoredArchiveDate = archiveDate;
        StoredArchiveSize = archiveSize;
        return TRUE;
    }
    return FALSE;
}

BOOL CSalShExtPastedData::CanUnloadPlugin(HWND parent, CPluginInterfaceAbstract* plugin)
{
    CALL_STACK_MESSAGE1("CSalShExtPastedData::CanUnloadPlugin()");

    BOOL used = FALSE;
    if (StoredPluginData.NotEmpty() && StoredPluginData.GetPluginInterface() == plugin)
        used = TRUE;
    else
    {
        if (!ArchiveFileNameW.empty())
        {
            // check if the unloaded plugin has anything to do with our archive,
            // plugin could easily unload during archiver usage (every archiver function
            // loads the plugin), but let's not overdo it, so we'll discard any archive listing
            int format = PackerFormatConfig.PackIsArchive(ArchiveFileNameW.c_str());
            if (format != 0) // we found a supported archive
            {
                format--;
                CPluginData* data;
                int index = PackerFormatConfig.GetUnpackerIndex(format);
                if (index < 0) // view: is it internal processing (plugin)?
                {
                    data = Plugins.Get(-index - 1);
                    if (data != NULL && data->GetPluginInterface()->GetInterface() == plugin)
                        used = TRUE;
                }
                if (PackerFormatConfig.GetUsePacker(format)) // has edit?
                {
                    index = PackerFormatConfig.GetPackerIndex(format);
                    if (index < 0) // is it internal processing (plugin)?
                    {
                        data = Plugins.Get(-index - 1);
                        if (data != NULL && data->GetPluginInterface()->GetInterface() == plugin)
                            used = TRUE;
                    }
                }
            }
        }
    }

    if (used)
        ReleaseStoredArchiveData(); // we're using plugin data, we must (or it's better to) release them
    return TRUE;                    // plugin unload is possible
}

static BOOL GetArchiveFileState(const wchar_t* path, FILETIME& writeTime, CQuadWord& size, DWORD& error)
{
    HANDLE file = gFileSystem->CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    const DWORD openError = GetLastError();
    HANDLES_ADD_EX(__otQuiet, file != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, file, openError, TRUE);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = openError;
        size.Set(0, 0);
        return FALSE;
    }

    const FileResult timeResult = gFileSystem->GetHandleFileTime(file, NULL, NULL, &writeTime);
    uint64_t sizeValue = 0;
    const FileResult sizeResult = gFileSystem->GetHandleFileSize(file, &sizeValue);
    HANDLES_REMOVE(file, __htFile, "IFileSystem::CloseHandle");
    gFileSystem->CloseFileHandle(file);

    if (!timeResult.success || !sizeResult.success)
    {
        error = !timeResult.success ? timeResult.errorCode : sizeResult.errorCode;
        size.Set(0, 0);
        return FALSE;
    }
    size.Set((DWORD)sizeValue, (DWORD)(sizeValue >> 32));
    error = ERROR_SUCCESS;
    return TRUE;
}

void CSalShExtPastedData::DoPasteOperation(BOOL copy, const wchar_t* tgtPath)
{
    CALL_STACK_MESSAGE1("CSalShExtPastedData::DoPasteOperation()");
    if (ArchiveFileNameW.empty() || SelFilesAndDirs.GetCount() == 0)
    {
        TRACE_E("CSalShExtPastedData::DoPasteOperation(): empty data, nothing to do!");
        return;
    }
    if (MainWindow == NULL || MainWindow->LeftPanel == NULL || MainWindow->RightPanel == NULL)
    {
        TRACE_E("CSalShExtPastedData::DoPasteOperation(): unexpected situation!");
        return;
    }

    BeginStopRefresh(); // auto-refresh takes a break

    CSalamanderDirectory* archiveDir = NULL;
    CPluginDataInterfaceAbstract* pluginData = NULL;
    for (int j = 0; j < 2; j++)
    {
        CFilesWindow* panel = j == 0 ? MainWindow->GetActivePanel() : MainWindow->GetNonActivePanel();
        if (panel->Is(ptZIPArchive) &&
            _wcsicmp(ArchiveFileNameW.c_str(), panel->GetZIPArchive()) == 0)
        { // panel contains our archive
            BOOL archMaybeUpdated;
            panel->OfferArchiveUpdateIfNeeded(MainWindow->HWindow, IDS_ARCHIVECLOSEEDIT2, &archMaybeUpdated);
            if (archMaybeUpdated)
            {
                EndStopRefresh(); // now auto-refresh resumes
                return;
            }
            // we'll use data from panel (we're in main thread, panel cannot change during operation)
            archiveDir = panel->GetArchiveDir();
            pluginData = panel->PluginData.GetInterface();
            break;
        }
    }

    if (StoredArchiveDir != NULL) // if we have any archive data stored
    {
        if (archiveDir != NULL)
            ReleaseStoredArchiveData(); // archive is open in panel, discard stored data
        else                            // try to use stored data, check archive file size&date
        {
            BOOL canUseData = FALSE;
            FILETIME archiveDate;  // archive file date&time
            CQuadWord archiveSize; // archive file size
            DWORD err = NO_ERROR;
            if (GetArchiveFileState(ArchiveFileNameW.c_str(), archiveDate, archiveSize, err) &&
                CompareFileTime(&archiveDate, &StoredArchiveDate) == 0 && // date doesn't differ and
                archiveSize == StoredArchiveSize)                         // size doesn't differ either
            {
                canUseData = TRUE;
            }
            if (canUseData)
            {
                archiveDir = StoredArchiveDir;
                pluginData = StoredPluginData.GetInterface();
            }
            else
                ReleaseStoredArchiveData(); // archive file has changed, discard stored data
        }
    }

    if (archiveDir == NULL) // no data, must list the archive again
    {
        CSalamanderDirectory* newArchiveDir = new CSalamanderDirectory(FALSE);
        if (newArchiveDir == NULL)
            TRACE_E(LOW_MEMORY);
        else
        {
            // get file information (exists?, size, date&time)
            DWORD err = NO_ERROR;
            FILETIME archiveDate;  // archive file date&time
            CQuadWord archiveSize; // archive file size
            GetArchiveFileState(ArchiveFileNameW.c_str(), archiveDate, archiveSize, err);

            if (err != NO_ERROR)
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_FILEERRORFORMAT), ArchiveFileNameW.c_str(), GetErrorTextOwned(err).c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORUNPACK), msg.c_str());
            }
            else
            {
                // apply optimized adding to 'newArchiveDir'
                newArchiveDir->AllocAddCache();

                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                CPluginDataInterfaceAbstract* pluginDataAbs = NULL;
                CPluginData* plugin = NULL;
                CreateSafeWaitWindow(LoadStrW(IDS_LISTINGARCHIVE), NULL, 2000, FALSE, MainWindow->HWindow);
                BOOL haveList = PackList(MainWindow->GetActivePanel(), ArchiveFileNameW.c_str(), *newArchiveDir, pluginDataAbs, plugin);
                DestroySafeWaitWindow();
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

                if (haveList)
                {
                    // release cache so it doesn't clutter the object unnecessarily
                    newArchiveDir->FreeAddCache();

                    StoredArchiveDir = newArchiveDir;
                    newArchiveDir = NULL; // so that newArchiveDir doesn't get released
                    if (plugin != NULL)
                    {
                        StoredPluginData.Init(pluginDataAbs, plugin->DLLName.c_str(), plugin->Version.c_str(),
                                              plugin->GetPluginInterface()->GetInterface(), plugin->BuiltForVersion);
                    }
                    else
                        StoredPluginData.Init(NULL, NULL, NULL, NULL, 0); // used only by plugins, not by Salamander
                    StoredArchiveDate = archiveDate;
                    StoredArchiveSize = archiveSize;

                    archiveDir = StoredArchiveDir; // for Paste operation we'll use the new listing
                    pluginData = StoredPluginData.GetInterface();
                }
            }

            if (newArchiveDir != NULL)
                delete newArchiveDir;
        }
    }

    if (archiveDir != NULL) // if we have archive data, perform Paste
    {
        CPanelTmpEnumData data;
        SelFilesAndDirs.Sort();
        data.IndexesCount = SelFilesAndDirs.GetCount();
        data.Indexes = (int*)malloc(sizeof(int) * data.IndexesCount);
        BOOL* foundDirs = NULL;
        if (SelFilesAndDirs.GetDirsCount() > 0)
            foundDirs = (BOOL*)malloc(sizeof(BOOL) * SelFilesAndDirs.GetDirsCount());
        BOOL* foundFiles = NULL;
        if (SelFilesAndDirs.GetFilesCount() > 0)
            foundFiles = (BOOL*)malloc(sizeof(BOOL) * SelFilesAndDirs.GetFilesCount());
        if (data.Indexes == NULL ||
            SelFilesAndDirs.GetDirsCount() > 0 && foundDirs == NULL ||
            SelFilesAndDirs.GetFilesCount() > 0 && foundFiles == NULL)
        {
            TRACE_E(LOW_MEMORY);
        }
        else
        {
            CFilesArray* files = archiveDir->GetFiles(PathInArchive.c_str());
            CFilesArray* dirs = archiveDir->GetDirs(PathInArchive.c_str());
            int actIndex = 0;
            int foundOnIndex;
            if (dirs != NULL && SelFilesAndDirs.GetDirsCount() > 0)
            {
                memset(foundDirs, 0, SelFilesAndDirs.GetDirsCount() * sizeof(BOOL));
                int i;
                for (i = 0; i < dirs->Count; i++)
                {
                    if (SelFilesAndDirs.Contains(TRUE, dirs->At(i).Name, &foundOnIndex) &&
                        foundOnIndex >= 0 && foundOnIndex < SelFilesAndDirs.GetDirsCount() &&
                        !foundDirs[foundOnIndex]) // mark only the first instance of name (if there are multiple identical names in SelFilesAndDirs, it doesn't work, binary search (in Contains) always finds the same one)
                    {
                        foundDirs[foundOnIndex] = TRUE; // this name was just found
                        data.Indexes[actIndex++] = i;
                    }
                }
            }
            if (files != NULL && SelFilesAndDirs.GetFilesCount() > 0)
            {
                memset(foundFiles, 0, SelFilesAndDirs.GetFilesCount() * sizeof(BOOL));
                int i;
                for (i = 0; i < files->Count; i++)
                {
                    if (SelFilesAndDirs.Contains(FALSE, files->At(i).Name, &foundOnIndex) &&
                        foundOnIndex >= 0 && foundOnIndex < SelFilesAndDirs.GetFilesCount() &&
                        !foundFiles[foundOnIndex]) // mark only the first instance of name (if there are multiple identical names in SelFilesAndDirs, it doesn't work, binary search (in Contains) always finds the same one)
                    {
                        foundFiles[foundOnIndex] = TRUE;            // this name was just found
                        data.Indexes[actIndex++] = dirs->Count + i; // all files have index shifted past directories, panel convention
                    }
                }
            }
            data.IndexesCount = actIndex;
            if (data.IndexesCount == 0) // our zip-root entirely went to the eternal hunting grounds
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORUNPACK), LoadStrW(IDS_ARCFILESNOTFOUND));
            }
            else
            {
                BOOL unpack = TRUE;
                if (data.IndexesCount != SelFilesAndDirs.GetCount()) // didn't find all selected items from clipboard (name duplicates or deleted files from archive)
                {
                    unpack = gPrompter->AskYesNo(LoadStrW(IDS_ERRORUNPACK), LoadStrW(IDS_ARCFILESNOTFOUND2)).type == PromptResult::kYes;
                }
                if (unpack)
                {
                    data.CurrentIndex = 0;
                    data.ZIPPath = PathInArchive.c_str();
                    data.Dirs = dirs;
                    data.Files = files;
                    data.ArchiveDir = archiveDir;
                    data.EnumLastDir = NULL;
                    data.EnumLastIndex = -1;

                    std::wstring targetPath = tgtPath;
                    if (targetPath.length() > 3 && targetPath.back() == L'\\')
                        targetPath.pop_back(); // except "c:\" we remove trailing backslash

                    // actual unpacking
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                    PackUncompress(MainWindow->HWindow, MainWindow->GetActivePanel(), ArchiveFileNameW.c_str(),
                                   pluginData, targetPath.c_str(), PathInArchive.c_str(), PanelSalEnumSelection, &data);
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

                    //if (GetForegroundWindow() == MainWindow->HWindow)  // for incomprehensible reasons focus disappears from panel during drag&drop to Explorer, restore it there
                    //  RestoreFocusInSourcePanel();

                    // refresh non-auto-refreshed directories
                    // change on target path and its subdirectories (creating new directories and unpacking
                    // files/directories)
                    MainWindow->PostChangeOnPathNotificationW(targetPath.c_str(), TRUE);
                    // change in directory where archive is located (shouldn't happen during unpack, but better refresh)
                    // Notify the archive's containing directory from the sole UTF-16 owner.
                    std::wstring archiveDirW = ArchiveFileNameW;
                    CutDirectoryW(archiveDirW);
                    MainWindow->PostChangeOnPathNotificationW(archiveDirW.c_str(), FALSE);

                    UpdateWindow(MainWindow->HWindow);
                }
            }
        }
        if (data.Indexes != NULL)
            free(data.Indexes);
        if (foundDirs != NULL)
            free(foundDirs);
        if (foundFiles != NULL)
            free(foundFiles);
    }

    EndStopRefresh(); // now auto-refresh resumes
}
