// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "mainwnd.h"
#include "ui/IPrompter.h"
#include "common/IRegistry.h"
#include "common/reg_sz_safe_length.h"

CRegistryWorkerThread RegistryWorkerThread;

// ****************************************************************************

BOOL ClearKeyAux(HKEY key)
{
    std::vector<std::wstring> subKeys;
    if (!gRegistry->EnumSubKeys(key, subKeys).success)
        return FALSE;
    for (const std::wstring& name : subKeys)
    {
        HKEY subKey = NULL;
        if (gRegistry->OpenKeyReadWrite(key, name.c_str(), subKey).success)
        {
            BOOL ret = ClearKeyAux(subKey);
            gRegistry->CloseKey(subKey);
            if (!ret || !gRegistry->DeleteKey(key, name.c_str()).success)
                return FALSE;
        }
        else
            return FALSE;
    }

    std::vector<std::wstring> values;
    if (!gRegistry->EnumValues(key, values).success)
        return FALSE;
    for (const std::wstring& name : values)
        if (!gRegistry->DeleteValue(key, name.c_str()).success)
        {
            TRACE_E("Unable to delete values in specified key (in registry).");
            return FALSE;
        }

    return TRUE;
}

// ****************************************************************************

BOOL CreateKeyAux(HWND parent, HKEY hKey, const wchar_t* name, HKEY& createdKey, BOOL quiet)
{
    RegistryResult result = gRegistry->CreateKey(hKey, name, createdKey);
    if (result.success)
        return TRUE;
    else
    {
        if (!quiet)
        {
            if (HLanguage == NULL)
            {
                // Pre-langpack fallback (gPrompter/LoadStrW aren't up yet) - now
                // wide too via MessageBoxW/GetErrorTextOwned, so a non-ASCII system error message
                // (a non-English Windows install) doesn't get CP_ACP-mangled here.
                MessageBoxW(parent, GetErrorTextOwned(result.errorCode).c_str(), L"Error Saving Configuration",
                            MB_OK | MB_ICONEXCLAMATION);
            }
            else
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORSAVECONFIG), GetErrorTextOwned(result.errorCode).c_str());
            }
        }
        return FALSE;
    }
}

// ****************************************************************************

BOOL OpenKeyAux(HWND parent, HKEY hKey, const wchar_t* name, HKEY& openedKey, BOOL quiet)
{
    RegistryResult result = gRegistry->OpenKeyRead(hKey, name, openedKey);
    if (result.success)
        return TRUE;
    else
    {
        if (!quiet && !result.notFound())
        {
            if (HLanguage == NULL)
            {
                MessageBoxW(parent, GetErrorTextOwned(result.errorCode).c_str(),
                            L"Error Loading Configuration", MB_OK | MB_ICONEXCLAMATION);
            }
            else
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), GetErrorTextOwned(result.errorCode).c_str());
            }
        }
        return FALSE;
    }
}

// ****************************************************************************

void CloseKeyAux(HKEY hKey)
{
    gRegistry->CloseKey(hKey);
}

// ****************************************************************************

BOOL DeleteKeyAux(HKEY hKey, const wchar_t* name)
{
    return gRegistry->DeleteKey(hKey, name).success;
}

// ****************************************************************************

BOOL GetValueAux(HWND parent, HKEY hKey, const wchar_t* name, DWORD type, void* buffer, DWORD bufferSize, BOOL quiet)
{
    RegValueType returnedType = RegValueType::None;
    RegistryResult result = gRegistry->ReadValue(hKey, name, returnedType, buffer, bufferSize);
    if (result.success)
        if (static_cast<DWORD>(returnedType) == type)
            return TRUE;
        else
        {
            if (!quiet)
            {
                if (HLanguage == NULL)
                {
                    MessageBoxW(parent, L"Unexpected value type.",
                                L"Error Loading Configuration", MB_OK | MB_ICONEXCLAMATION);
                }
                else
                {
                    gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), LoadStrW(IDS_UNEXPECTEDVALUETYPE));
                }
            }
            return FALSE;
        }
    else
    {
        if (!result.notFound())
        {
            if (!quiet)
            {
                if (HLanguage == NULL)
                {
                    MessageBoxW(parent, GetErrorTextOwned(result.errorCode).c_str(),
                                L"Error Loading Configuration", MB_OK | MB_ICONEXCLAMATION);
                }
                else
                {
                    gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), GetErrorTextOwned(result.errorCode).c_str());
                }
            }
        }
        return FALSE;
    }
}

BOOL GetValue2Aux(HWND parent, HKEY hKey, const wchar_t* name, DWORD type1, DWORD type2, DWORD* returnedType, void* buffer, DWORD bufferSize)
{
    RegValueType actualType = RegValueType::None;
    RegistryResult result = gRegistry->ReadValue(hKey, name, actualType, buffer, bufferSize);
    if (result.success)
        if (static_cast<DWORD>(actualType) == type1 || static_cast<DWORD>(actualType) == type2)
        {
            *returnedType = static_cast<DWORD>(actualType);
            return TRUE;
        }
        else
        {
            if (HLanguage == NULL)
            {
                MessageBoxW(parent, L"Unexpected value type.",
                            L"Error Loading Configuration", MB_OK | MB_ICONEXCLAMATION);
            }
            else
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), LoadStrW(IDS_UNEXPECTEDVALUETYPE));
            }
            return FALSE;
        }
    else
    {
        if (!result.notFound())
        {
            if (HLanguage == NULL)
            {
                MessageBoxW(parent, GetErrorTextOwned(result.errorCode).c_str(),
                            L"Error Loading Configuration", MB_OK | MB_ICONEXCLAMATION);
            }
            else
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), GetErrorTextOwned(result.errorCode).c_str());
            }
        }
        return FALSE;
    }
}

BOOL GetValueDontCheckTypeAux(HKEY hKey, const wchar_t* name, void* buffer, DWORD bufferSize)
{
    RegValueType type = RegValueType::None;
    return gRegistry->ReadValue(hKey, name, type, buffer, bufferSize).success;
}

// ****************************************************************************

BOOL SetValueAux(HWND parent, HKEY hKey, const wchar_t* name, DWORD type,
                 const void* data, DWORD dataSize, BOOL quiet)
{
    if (dataSize == -1)
    {
        // RegSetValueExW measures BYTES. The -1 convention is documented in
        // regwork.h as "for strings"; strlen() over wide data measured ONE CHARACTER and wrote
        // two bytes, so every REG_SZ written through this path was a single-character value.
        //
        // ...and the wcslen() that replaced it needs the SAME bound that
        // SetValueW's REG_SZ branch already applies (sally_strings_waitwindow.cpp).
        // This is the second, parallel wide registry-write path - SetValueW goes through
        // gRegistry->SetString, this one through CRegistryWorkerThread - and the earlier
        // mitigation was only ever applied to the first. A caller passing genuinely narrow data
        // here therefore still got an UNBOUNDED wcslen() over it: one trailing zero BYTE is not
        // the two consecutive zero bytes wcslen looks for, so the scan ran past the buffer into
        // whatever followed until a wide NUL happened to appear, and that garbage distance became
        // the write length. Observed live: the main window's "Split Position" / "Before Zoom
        // Split Position" stored as 515 chars of "50.0" + 0xCC debug fill, which then failed to
        // read back as ERROR_MORE_DATA - the user-visible "(234) More data is available" dialog
        // on every startup. Those two call sites are now genuinely wide, but bounding the scan
        // here is what stops the NEXT such caller from silently corrupting a value instead of
        // failing loudly. Same lesson as the ac00cbe9 handle-tracking mismatch: this codebase has
        // two parallel registry abstractions, and a fix applied to only one of them is not a fix.
        size_t len;
        if (!ComputeRegSzSafeLength((const wchar_t*)data, len))
        {
            // TRACE_EW, not TRACE_E: the value name is already wide, and narrowing it through
            // WideToAnsi purely to print it would mangle exactly the names most worth seeing
            // in this message - and add an ANSI conversion to a file that has none.
            TRACE_EW(L"SetValueAux: REG_SZ value '" << name << L"' has no wide NUL "
                     L"terminator within the safe scan bound - refusing to write (narrow data "
                     L"passed where a wide string was required?)");
            return FALSE;
        }
        dataSize = (DWORD)((len + 1) * sizeof(wchar_t));
    }
    RegistryResult result = gRegistry->WriteValue(hKey, name, static_cast<RegValueType>(type), data, dataSize);
    if (result.success)
        return TRUE;
    else
    {
        if (!quiet)
        {
            if (HLanguage == NULL)
            {
                MessageBoxW(parent, GetErrorTextOwned(result.errorCode).c_str(),
                            L"Error Saving Configuration", MB_OK | MB_ICONEXCLAMATION);
            }
            else
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORSAVECONFIG), GetErrorTextOwned(result.errorCode).c_str());
            }
        }
        return FALSE;
    }
}

// ****************************************************************************

BOOL DeleteValueAux(HKEY hKey, const wchar_t* name)
{
    return gRegistry->DeleteValue(hKey, name).success;
}

// ****************************************************************************

BOOL GetSizeAux(HWND parent, HKEY hKey, const wchar_t* name, DWORD type, DWORD& bufferSize)
{
    RegValueType actualType = RegValueType::None;
    RegistryResult result = gRegistry->ReadValue(hKey, name, actualType, NULL, bufferSize);
    if (result.success)
        if (static_cast<DWORD>(actualType) == type)
            return TRUE;
        else
        {
            if (HLanguage == NULL)
            {
                MessageBoxW(parent, L"Unexpected value type.",
                            L"Error Loading Configuration", MB_OK | MB_ICONEXCLAMATION);
            }
            else
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), LoadStrW(IDS_UNEXPECTEDVALUETYPE));
            }
            return FALSE;
        }
    else
    {
        if (!result.notFound())
        {
            if (HLanguage == NULL)
            {
                MessageBoxW(parent, GetErrorTextOwned(result.errorCode).c_str(),
                            L"Error Loading Configuration", MB_OK | MB_ICONEXCLAMATION);
            }
            else
            {
                gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), GetErrorTextOwned(result.errorCode).c_str());
            }
        }
        return FALSE;
    }
}

//
// ****************************************************************************
// CRegistryWorkerThread
//

CRegistryWorkerThread::CRegistryWorkerThread()
{
    Thread = NULL;
    OwnerTID = 0; // invalid TID
    StopWorkerSkipCount = 0;
    InUse = FALSE;

    WorkReady = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    WorkDone = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));

    WorkType = rwtNone;
    LastWorkSuccess = FALSE;
    Key = NULL;
    Name = NULL;
    OpenedKey = NULL;
    ValueType = 0;
    ValueType2 = 0;
    ReturnedValueType = NULL;
    Buffer = NULL;
    BufferSize = 0;
    Data = NULL;
    DataSize = 0;
}

CRegistryWorkerThread::~CRegistryWorkerThread()
{
    if (Thread != NULL)
        TRACE_E("CRegistryWorkerThread::~CRegistryWorkerThread(): you must call StopThread()!");
    if (WorkReady != NULL)
        HANDLES(CloseHandle(WorkReady));
    if (WorkDone != NULL)
        HANDLES(CloseHandle(WorkDone));
    if (WorkType != rwtNone)
        TRACE_E("CRegistryWorkerThread::~CRegistryWorkerThread(): unexpected situation WorkType != rwtNone");
}

BOOL CRegistryWorkerThread::StartThread()
{
    BOOL ret = FALSE;
    if (Thread == NULL)
    {
        if (WorkReady != NULL && WorkDone != NULL)
        {
            // clean-up
            if (InUse)
                TRACE_E("CRegistryWorkerThread::StartThread(): thread is not running and InUse is TRUE!");
            InUse = FALSE; // just to be safe, at this point, it cannot be TRUE
            ResetEvent(WorkReady);
            ResetEvent(WorkDone);
            WorkType = rwtNone;

            DWORD threadID;
            Thread = HANDLES(CreateThread(NULL, 0, CRegistryWorkerThread::ThreadBody, (void*)this, 0, &threadID));
            if (Thread != NULL)
            {
                OwnerTID = GetCurrentThreadId(); // enable usage for this thread
                StopWorkerSkipCount = 0;
                int level = GetThreadPriority(GetCurrentThread());
                SetThreadPriority(Thread, level);
                ret = TRUE; // success!
            }
            else
                TRACE_E("CRegistryWorkerThread::StartThread(): unable to start registry-worker thread!");
        }
        else
            TRACE_E("CRegistryWorkerThread::StartThread(): unable to start thread, neccessary event-objects are not OK!");
    }
    else
    {
        TRACE_E("CRegistryWorkerThread::StartThread(): thread is already running!");
        if (OwnerTID == GetCurrentThreadId())
            StopWorkerSkipCount++;
    }
    return ret;
}

void CRegistryWorkerThread::StopThread()
{
    if (Thread != NULL)
    {
        if (OwnerTID == GetCurrentThreadId())
        {
            if (StopWorkerSkipCount > 0)
            {
                StopWorkerSkipCount--;
                TRACE_E("CRegistryWorkerThread::StopThread(): ignoring call!");
            }
            else
            {
                CInUseHandler i;
                if (i.CanUseThread(this))
                {
                    WorkType = rwtStopWorker;

                    WaitForWorkDoneWithMessageLoop();
                    WaitForSingleObject(Thread, INFINITE);
                    HANDLES(CloseHandle(Thread));
                    Thread = NULL;
                    OwnerTID = 0; // invalid TID
                }
                else // should never happen
                {
                    // prevent a dead lock: if this thread already has work in the registry worker
                    // thread, we cannot wait for it to finish here
                    TRACE_E("CRegistryWorkerThread::StopThread(): preventing dead lock, skipping stop signal!");
                }
            }
        }
        else
            TRACE_E("CRegistryWorkerThread::StopThread(): you can stop thread only from the same thread you have started it!");
    }
    else
        TRACE_E("CRegistryWorkerThread::StopThread(): thread is not running!");
}

void CRegistryWorkerThread::WaitForWorkDoneWithMessageLoop()
{
    SLOW_CALL_STACK_MESSAGE1("WaitForWorkDoneWithMessageLoop()");

    SetEvent(WorkReady);
    while (1)
    {
        DWORD waitRes = MsgWaitForMultipleObjects(1, &WorkDone, FALSE, INFINITE, QS_ALLINPUT);
        if (waitRes == WAIT_OBJECT_0)
            break; // work is finished, continue...
        else
        {
            if (waitRes == WAIT_OBJECT_0 + 1) // new input
            {
                MSG msg;
                while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
        }
    }
}

BOOL CRegistryWorkerThread::ClearKey(HKEY key)
{
    CInUseHandler i;
    if (i.CanUseThread(this))
    {
        WorkType = rwtClearKey;
        LastWorkSuccess = FALSE;
        Key = key;

        WaitForWorkDoneWithMessageLoop();
        return LastWorkSuccess;
    }
    else
        return ClearKeyAux(key);
}

BOOL CRegistryWorkerThread::CreateKey(HKEY key, const wchar_t* name, HKEY& createdKey)
{
    CInUseHandler i;
    if (i.CanUseThread(this))
    {
        WorkType = rwtCreateKey;
        LastWorkSuccess = FALSE;
        Key = key;
        Name = name;
        OpenedKey = NULL;

        WaitForWorkDoneWithMessageLoop();
        createdKey = OpenedKey;
        return LastWorkSuccess;
    }
    else
        return CreateKeyAux(MainWindow != NULL ? MainWindow->HWindow : NULL, key, name, createdKey, FALSE);
}

BOOL CRegistryWorkerThread::OpenKey(HKEY key, const wchar_t* name, HKEY& openedKey)
{
    CInUseHandler i;
    if (i.CanUseThread(this))
    {
        WorkType = rwtOpenKey;
        LastWorkSuccess = FALSE;
        Key = key;
        Name = name;
        OpenedKey = NULL;

        WaitForWorkDoneWithMessageLoop();
        openedKey = OpenedKey;
        return LastWorkSuccess;
    }
    else
    {
        return OpenKeyAux(MainWindow != NULL ? MainWindow->HWindow : NULL,
                          key, name, openedKey, FALSE);
    }
}

void CRegistryWorkerThread::CloseKey(HKEY key)
{
    CInUseHandler i;
    if (i.CanUseThread(this))
    {
        WorkType = rwtCloseKey;
        Key = key;

        WaitForWorkDoneWithMessageLoop();
    }
    else
        CloseKeyAux(key);
}

BOOL CRegistryWorkerThread::DeleteKey(HKEY key, const wchar_t* name)
{
    CInUseHandler i;
    if (i.CanUseThread(this))
    {
        WorkType = rwtDeleteKey;
        LastWorkSuccess = FALSE;
        Key = key;
        Name = name;

        WaitForWorkDoneWithMessageLoop();
        return LastWorkSuccess;
    }
    else
        return DeleteKeyAux(key, name);
}

BOOL CRegistryWorkerThread::GetValue(HKEY key, const wchar_t* name, DWORD type, void* buffer, DWORD bufferSize)
{
    CInUseHandler i;
    if (i.CanUseThread(this))
    {
        WorkType = rwtGetValue;
        LastWorkSuccess = FALSE;
        Key = key;
        Name = name;
        ValueType = type;
        Buffer = buffer;
        BufferSize = bufferSize;

        WaitForWorkDoneWithMessageLoop();
        return LastWorkSuccess;
    }
    else
    {
        return GetValueAux(MainWindow != NULL ? MainWindow->HWindow : NULL,
                           key, name, type, buffer, bufferSize, FALSE);
    }
}

BOOL CRegistryWorkerThread::GetValue2(HKEY key, const wchar_t* name, DWORD type1, DWORD type2, DWORD* returnedType, void* buffer, DWORD bufferSize)
{
    CInUseHandler i;
    if (i.CanUseThread(this))
    {
        WorkType = rwtGetValue2;
        LastWorkSuccess = FALSE;
        Key = key;
        Name = name;
        ValueType = type1;
        ValueType2 = type2;
        ReturnedValueType = returnedType;
        Buffer = buffer;
        BufferSize = bufferSize;

        WaitForWorkDoneWithMessageLoop();
        return LastWorkSuccess;
    }
    else
    {
        return GetValue2Aux(MainWindow != NULL ? MainWindow->HWindow : NULL,
                            key, name, type1, type2, returnedType, buffer, bufferSize);
    }
}

BOOL CRegistryWorkerThread::SetValue(HKEY key, const wchar_t* name, DWORD type, const void* data, DWORD dataSize)
{
    CInUseHandler i;
    if (i.CanUseThread(this))
    {
        WorkType = rwtSetValue;
        LastWorkSuccess = FALSE;
        Key = key;
        Name = name;
        ValueType = type;
        Data = data;
        DataSize = dataSize;

        WaitForWorkDoneWithMessageLoop();
        return LastWorkSuccess;
    }
    else
        return SetValueAux(MainWindow != NULL ? MainWindow->HWindow : NULL, key, name, type, data, dataSize, FALSE);
}

BOOL CRegistryWorkerThread::DeleteValue(HKEY key, const wchar_t* name)
{
    CInUseHandler i;
    if (i.CanUseThread(this))
    {
        WorkType = rwtDeleteValue;
        LastWorkSuccess = FALSE;
        Key = key;
        Name = name;

        WaitForWorkDoneWithMessageLoop();
        return LastWorkSuccess;
    }
    else
        return DeleteValueAux(key, name);
}

BOOL CRegistryWorkerThread::GetSize(HKEY key, const wchar_t* name, DWORD type, DWORD& bufferSize)
{
    CInUseHandler i;
    if (i.CanUseThread(this))
    {
        WorkType = rwtGetSize;
        LastWorkSuccess = FALSE;
        Key = key;
        Name = name;
        ValueType = type;
        BufferSize = 0;

        WaitForWorkDoneWithMessageLoop();
        bufferSize = BufferSize;
        return LastWorkSuccess;
    }
    else
        return GetSizeAux(MainWindow != NULL ? MainWindow->HWindow : NULL, key, name, type, bufferSize);
}

unsigned
CRegistryWorkerThread::Body()
{
    CALL_STACK_MESSAGE1("CRegistryWorkerThread::Body()");
    SetThreadNameInVCAndTrace(L"RegistryWorker");
    TRACE_I("Begin");

    int loops = 0;
    while (1)
    {
        DWORD res = WaitForMultipleObjects(1, &WorkReady, FALSE, INFINITE);
        switch (res)
        {
        case WAIT_OBJECT_0 + 0: // WorkReady
        {
            loops++;
            switch (WorkType)
            {
            case rwtStopWorker:
            {
                TRACE_I("CRegistryWorkerThread::Body(): loops: " << loops);
                TRACE_I("End");
                WorkType = rwtNone;
                SetEvent(WorkDone);
                return 0; // terminate the thread
            }

            case rwtClearKey:
                LastWorkSuccess = ClearKeyAux(Key);
                break;
            case rwtCreateKey:
                LastWorkSuccess = CreateKeyAux(NULL, Key, Name, OpenedKey, FALSE);
                break;
            case rwtOpenKey:
                LastWorkSuccess = OpenKeyAux(NULL, Key, Name, OpenedKey, FALSE);
                break;
            case rwtCloseKey:
                CloseKeyAux(Key);
                break;
            case rwtDeleteKey:
                LastWorkSuccess = DeleteKeyAux(Key, Name);
                break;
            case rwtGetValue:
                LastWorkSuccess = GetValueAux(NULL, Key, Name, ValueType, Buffer, BufferSize, FALSE);
                break;
            case rwtGetValue2:
                LastWorkSuccess = GetValue2Aux(NULL, Key, Name, ValueType, ValueType2, ReturnedValueType, Buffer, BufferSize);
                break;
            case rwtSetValue:
                LastWorkSuccess = SetValueAux(NULL, Key, Name, ValueType, Data, DataSize, FALSE);
                break;
            case rwtDeleteValue:
                LastWorkSuccess = DeleteValueAux(Key, Name);
                break;
            case rwtGetSize:
                LastWorkSuccess = GetSizeAux(NULL, Key, Name, ValueType, BufferSize);
                break;
            }
            WorkType = rwtNone;
            SetEvent(WorkDone);
            break;
        }
        }
    }
}

unsigned
CRegistryWorkerThread::ThreadBodyFEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return ((CRegistryWorkerThread*)param)->Body();
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("CRegistryWorkerThread::ThreadBodyFEH: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // a more forceful exit (this one still performs some actions)
    }
    return 0;
#endif // CALLSTK_DISABLE
}

DWORD WINAPI
CRegistryWorkerThread::ThreadBody(void* param)
{
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    ThreadBodyFEH(param);
    return 0;
}

CRegistryWorkerThread::CInUseHandler::~CInUseHandler()
{
    if (T != NULL)
        T->InUse = FALSE;
}

BOOL CRegistryWorkerThread::CInUseHandler::CanUseThread(CRegistryWorkerThread* t)
{
    if (t->Thread != NULL && t->OwnerTID == GetCurrentThreadId())
    { // work in the worker concerns only the thread that started it
        BOOL ret = !t->InUse;
        if (ret) // work can run in the registry worker thread
        {
            t->InUse = TRUE;
            T = t; // destructor will set T->InUse = FALSE
        }
        // otherwise // this is a recursive call (due to the message loop and thus message distribution) = we reject running the work there
        return ret;
    }
    return FALSE;
}
