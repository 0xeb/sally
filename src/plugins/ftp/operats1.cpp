// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

static wchar_t* DupQueueWideText(const wchar_t* text)
{
    if (text == NULL)
        return NULL;
    const size_t length = wcslen(text) + 1;
    if (length > INT_MAX / sizeof(wchar_t))
        return NULL;
    wchar_t* copy = (wchar_t*)SalamanderGeneral->Alloc((int)(length * sizeof(wchar_t)));
    if (copy != NULL)
        memcpy(copy, text, length * sizeof(wchar_t));
    return copy;
}

static char* DupQueueByteText(const char* text, size_t length)
{
    if (text == NULL || length >= INT_MAX)
        return NULL;
    char* copy = (char*)SalamanderGeneral->Alloc((int)length + 1);
    if (copy != NULL)
    {
        memcpy(copy, text, length);
        copy[length] = 0;
    }
    return copy;
}

CFTPOperationsList FTPOperationsList; // all FTP operations

CRITICAL_SECTION CFTPQueueItem::NextItemUIDCritSect; // critical section for accessing CFTPQueueItem::NextItemUID
int CFTPQueueItem::NextItemUID = 0;                  // global counter for item UIDs

int CFTPOperation::NextOrdinalNumber = 0;            // global counter for an operation's OrdinalNumber (access only within the NextOrdinalNumberCS section!)
CRITICAL_SECTION CFTPOperation::NextOrdinalNumberCS; // critical section for NextOrdinalNumber

CUIDArray CanceledOperations(5, 5); // array of operation UIDs that should be canceled (after receiving the FTPCMD_CANCELOPERATION command)

HANDLE WorkerMayBeClosedEvent = NULL;      // generates a pulse when a worker socket is closed
int WorkerMayBeClosedState = 0;            // increases with every worker socket closure
CRITICAL_SECTION WorkerMayBeClosedStateCS; // critical section for accessing WorkerMayBeClosedState

CReturningConnections ReturningConnections(5, 5); // array with returned connections (from workers back to the panel)

CFTPDiskThread* FTPDiskThread = NULL; // thread handling disk operations (reason: non-blocking calls)

CUploadListingCache UploadListingCache; // cache of path listings on servers - used during uploads to check whether the target file/directory already exists

CFTPOpenedFiles FTPOpenedFiles; // list of files currently opened from this Salamander on FTP servers

//
// ****************************************************************************
// CFTPOperationsList
//

CFTPOperationsList::CFTPOperationsList() : Operations(10, 10)
{
    HANDLES(InitializeCriticalSection(&OpListCritSect));
    FirstFreeIndexInOperations = -1;
    CallOK = FALSE;
}

CFTPOperationsList::~CFTPOperationsList()
{
    HANDLES(DeleteCriticalSection(&OpListCritSect));
}

BOOL CFTPOperationsList::IsEmpty()
{
    CALL_STACK_MESSAGE1("CFTPOperationsList::IsEmpty()");

    HANDLES(EnterCriticalSection(&OpListCritSect));
    BOOL ret = Operations.Count == 0;
    HANDLES(LeaveCriticalSection(&OpListCritSect));
    return ret;
}

BOOL CFTPOperationsList::AddOperation(CFTPOperation* newOper, int* newuid)
{
    CALL_STACK_MESSAGE1("CFTPOperationsList::AddOperation(,)");

    BOOL ret = TRUE;
    HANDLES(EnterCriticalSection(&OpListCritSect));
    if (FirstFreeIndexInOperations != -1) // do we have a place to insert the new object?
    {
        // insert 'newOper' into the array
        Operations[FirstFreeIndexInOperations] = newOper;
        if (newuid != NULL)
            *newuid = FirstFreeIndexInOperations;
        newOper->SetUID(FirstFreeIndexInOperations);

        // searching for the next free slot in the array
        int i;
        for (i = FirstFreeIndexInOperations + 1; i < Operations.Count; i++)
        {
            if (Operations[i] == NULL)
            {
                FirstFreeIndexInOperations = i;
                break;
            }
        }
        if (i >= Operations.Count)
            FirstFreeIndexInOperations = -1;
    }
    else
    {
        int index = Operations.Add(newOper);
        if (Operations.IsGood())
        {
            if (newuid != NULL)
                *newuid = index;
            newOper->SetUID(index);
        }
        else
        {
            if (newuid != NULL)
                *newuid = -1;
            Operations.ResetState();
            ret = FALSE;
        }
    }
    HANDLES(LeaveCriticalSection(&OpListCritSect));
    return ret;
}

void CFTPOperationsList::DeleteOperation(int uid, BOOL doNotPostChangeOnPathNotifications)
{
    CALL_STACK_MESSAGE3("CFTPOperationsList::DeleteOperation(%d, %d)", uid,
                        doNotPostChangeOnPathNotifications);

    BOOL uploadOperDeleted = FALSE;
    std::wstring uploadUser;
    std::wstring uploadHost;
    const wchar_t* uploadUserView = NULL;
    const wchar_t* uploadHostView = NULL;
    unsigned short uploadPort;

    HANDLES(EnterCriticalSection(&OpListCritSect));
    if (uid >= 0 && uid < Operations.Count && Operations[uid] != NULL)
    {
        CFTPOperation* oper = Operations[uid];
        CFTPOperationType operType = oper->GetOperationType();
        if (operType == fotCopyUpload || operType == fotMoveUpload)
        { // if this is an upload operation, after discarding it try to clean up the upload listing cache
            uploadOperDeleted = TRUE;
            oper->GetUserHostPort(&uploadUserView, uploadHostView, &uploadPort);
            uploadOperDeleted = FtpStoreWideText(uploadUserView, uploadUser) &&
                                FtpStoreWideText(uploadHostView, uploadHost);
        }
        if (!doNotPostChangeOnPathNotifications)
        {
            COperationState state = oper->GetOperationState(FALSE);
            if (state != opstSuccessfullyFinished && state != opstFinishedWithSkips && state != opstFinishedWithErrors)
                oper->PostChangeOnPathNotifications(FALSE); // the change-on-path notification has not been sent yet, send it now (later it will no longer be possible)
        }
        delete oper;                     // deallocate the operation object
        Operations[uid] = NULL;          // free the slot in the array
        if (uid + 1 == Operations.Count) // last element, deallocate the empty end of the array
        {
            while (uid > 0 && Operations[uid - 1] == NULL)
                uid--;
            Operations.Delete(uid, Operations.Count - uid); // remove the empty end of the array (only NULL elements)
            if (!Operations.IsGood())
                Operations.ResetState();
            if (FirstFreeIndexInOperations >= Operations.Count)
                FirstFreeIndexInOperations = -1;
        }
        else // deleting inside the array, no array deallocation is possible
        {
            if (FirstFreeIndexInOperations == -1 || uid < FirstFreeIndexInOperations)
                FirstFreeIndexInOperations = uid;
        }
    }
    else
        TRACE_E("CFTPOperationsList::DeleteOperation(): Pokus o vymaz neplatneho prvku pole!");
    HANDLES(LeaveCriticalSection(&OpListCritSect));

    // if an upload operation was canceled and no other upload operation is working with
    // the server used by the canceled operation, we can release this server from the upload
    // listing cache
    if (uploadOperDeleted && !IsUploadingToServer(uploadUser.c_str(), uploadHost.c_str(), uploadPort))
        UploadListingCache.RemoveServer(uploadUser.c_str(), uploadHost.c_str(), uploadPort);
}

void CFTPOperationsList::CloseAllOperationDlgs()
{
    CALL_STACK_MESSAGE1("CFTPOperationsList::CloseAllOperationDlgs()");

    HANDLES(EnterCriticalSection(&OpListCritSect));
    int i;
    for (i = 0; i < Operations.Count; i++)
    {
        CFTPOperation* oper = Operations[i];
        if (oper != NULL)
        {
            HANDLE t = NULL;
            oper->CloseOperationDlg(&t);
            if (t != NULL)
            {
                HANDLES(LeaveCriticalSection(&OpListCritSect)); // we must leave the section; the dialog thread calls CFTPOperationsList::SetOperationDlg()
                CALL_STACK_MESSAGE1("AuxThreadQueue.WaitForExit()");
                AuxThreadQueue.WaitForExit(t, INFINITE);
                HANDLES(EnterCriticalSection(&OpListCritSect));
            }
        }
    }
    HANDLES(LeaveCriticalSection(&OpListCritSect));
}

void CFTPOperationsList::WaitForFinishOrESC(HWND parent, int milliseconds, CWaitWindow* waitWnd,
                                            CWorkerWaitSatisfiedReason& reason, BOOL& postWM_CLOSE,
                                            int& lastWorkerMayBeClosedState)
{
    CALL_STACK_MESSAGE2("CFTPOperationsList::WaitForFinishOrESC(, %d,)", milliseconds);

    const DWORD cycleTime = 200; // period for checking the ESC key in ms (200 = 5 times per second) - NOTE: also a safeguard against unintended races (PulseEvent(WorkerMayBeClosedEvent) may happen before entering the wait function)
    DWORD timeStart = GetTickCount();
    DWORD restOfWaitTime = milliseconds; // remaining waiting time

    GetAsyncKeyState(VK_ESCAPE); // init GetAsyncKeyState - see help
    while (1)
    {
        HANDLES(EnterCriticalSection(&WorkerMayBeClosedStateCS));
        BOOL socketClosure = lastWorkerMayBeClosedState != WorkerMayBeClosedState;
        lastWorkerMayBeClosedState = WorkerMayBeClosedState;
        HANDLES(LeaveCriticalSection(&WorkerMayBeClosedStateCS));
        if (socketClosure) // the worker socket was closed (or this is the first call to this method and 'lastWorkerMayBeClosedState' was -1)
        {
            reason = wwsrWorkerSocketClosure;
            break; // report the worker socket closure
        }

        DWORD waitTime;
        if (milliseconds != INFINITE)
            waitTime = min(cycleTime, restOfWaitTime);
        else
            waitTime = cycleTime;
        DWORD waitRes = MsgWaitForMultipleObjects(1, &WorkerMayBeClosedEvent, FALSE, waitTime, QS_ALLINPUT);

        // check the ESC key first (so we do not ignore it for the user)
        if (milliseconds != 0 &&                                                          // if the timeout is zero we are only pumping messages, do not handle ESC
            ((GetAsyncKeyState(VK_ESCAPE) & 0x8001) && GetForegroundWindow() == parent || // ESC pressed
             waitWnd != NULL && waitWnd->GetWindowClosePressed()))                        // close button in the wait window
        {
            MSG msg; // discard the buffered ESC
            while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
                ;
            reason = wwsrEsc;
            break; // report ESC
        }
        if (waitRes != WAIT_OBJECT_0) // no additional worker was closed (we ignore this event; it only "wakes" the waiting)
        {
            if (waitRes == WAIT_OBJECT_0 + 1) // process Windows messages
            {
                MSG msg;
                while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_CLOSE) // we cannot deliver WM_CLOSE; post it only after enabling the parent
                    {
                        postWM_CLOSE = TRUE;
                        SetForegroundWindow(parent); // for now bring the disabled window into view so the user knows what they are waiting for
                    }
                    else
                    {
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }
                }
            }
            else
            {
                if (waitRes == WAIT_TIMEOUT &&
                    restOfWaitTime == waitTime) // this is not just the ESC key test cycle timeout but the global timeout
                {
                    reason = wwsrTimeout;
                    break; // report the timeout
                }
            }
        }
        if (milliseconds != INFINITE) // recalculate the remaining wait time (according to real time)
        {
            DWORD t = GetTickCount() - timeStart; // works even when the tick counter overflows
            if (t < (DWORD)milliseconds)
                restOfWaitTime = (DWORD)milliseconds - t;
            else
                restOfWaitTime = 0; // let the timeout be reported (we must not do it ourselves - the worker socket closure has priority over the timeout)
        }
    }
}

#define WORKERS_VICTIMS_COUNT 50 // number of worker sockets processed in one cycle

void CFTPOperationsList::StopWorkers(HWND parent, int operUID, int workerInd)
{
    CALL_STACK_MESSAGE3("CFTPOperationsList::StopWorkers(, %d, %d)", operUID, workerInd);

    parent = FindPopupParent(parent);
    // remember the focus from 'parent' (store NULL if the focus is not within 'parent')
    HWND focusedWnd = GetFocus();
    HWND hwnd = focusedWnd;
    while (hwnd != NULL && hwnd != parent)
        hwnd = GetParent(hwnd);
    if (hwnd != parent)
        focusedWnd = NULL;
    // disable 'parent'; when enabling it restore the focus as well
    EnableWindow(parent, FALSE);

    // set the wait cursor over the parent; unfortunately we cannot do it differently
    CSetWaitCursorWindow* winParent = new CSetWaitCursorWindow;
    if (winParent != NULL)
        winParent->AttachToWindow(parent);

    // ensure the running workers learn that they should finish (if they have
    // a data connection, interrupt it)
    CFTPWorker* victims[WORKERS_VICTIMS_COUNT]; // necessary to avoid calling socket operations inside the OpListCritSect section
    int lastOpIndex = 0;                        // optimization (index of the last processed operation - start with it in the next round)
    while (1)
    {
        BOOL done = TRUE;
        int count = 0;
        HANDLES(EnterCriticalSection(&OpListCritSect));
        if (operUID != -1) // applies only to a single operation
        {
            if (operUID >= 0 && operUID < Operations.Count)
            {
                CFTPOperation* oper = Operations[operUID];
                if (oper != NULL)
                    done &= !oper->InformWorkersAboutStop(workerInd, victims, WORKERS_VICTIMS_COUNT, &count);
            }
        }
        else // applies to all operations and all workers
        {
            int i;
            for (i = lastOpIndex; count < WORKERS_VICTIMS_COUNT && i < Operations.Count; i++)
            {
                lastOpIndex = i;
                CFTPOperation* oper = Operations[i];
                if (oper != NULL)
                    done &= !oper->InformWorkersAboutStop(-1, victims, WORKERS_VICTIMS_COUNT, &count);
            }
            done &= (i >= Operations.Count);
        }
        HANDLES(LeaveCriticalSection(&OpListCritSect));

        int i;
        for (i = 0; i < count; i++)
            victims[i]->CloseDataConnectionOrPostShouldStop();
        if (done)
            break; // all operations reported they do not want another call
    }

    // ensure that the "elapsed time" stops in the operation dialogs of the halted operations
    HANDLES(EnterCriticalSection(&OpListCritSect));
    if (operUID != -1) // applies only to a single operation
    {
        if (operUID >= 0 && operUID < Operations.Count)
        {
            CFTPOperation* oper = Operations[operUID];
            if (oper != NULL)
                oper->OperationStatusMaybeChanged();
        }
    }
    else // applies to all operations and all workers
    {
        int i;
        for (i = 0; i < Operations.Count; i++)
        {
            CFTPOperation* oper = Operations[i];
            if (oper != NULL)
                oper->OperationStatusMaybeChanged();
        }
    }
    HANDLES(LeaveCriticalSection(&OpListCritSect));

    // open the wait window + wait for ESC + timeout + completion of worker shutdown + Windows messages
    // (process WM_CLOSE later + call SetForegroundWindow on 'parent' when received)
    int closConResID = operUID != -1 ? (workerInd != -1 ? IDS_CLOSINGOPERCONS1 : IDS_CLOSINGOPERCONS2) : IDS_CLOSINGOPERCONS3;
    int termConResID = operUID != -1 ? (workerInd != -1 ? IDS_TERMCONFOROPER1 : IDS_TERMCONFOROPER2) : IDS_TERMCONFOROPER3;
    BOOL postWM_CLOSE = FALSE;
    int lastWorkerMayBeClosedState = -1;
    CWaitWindow waitWnd(parent, TRUE);
    waitWnd.SetText(LangStr(closConResID).c_str());
    waitWnd.Create(WAITWND_CLWORKCON);
    int serverTimeout = Config.GetServerRepliesTimeout() * 1000;
    if (serverTimeout < 1000)
        serverTimeout = 1000; // at least one second

    DWORD start = GetTickCount();
    while (1)
    {
        // wait for a worker socket to close or for ESC
        DWORD now = GetTickCount();
        if (now - start > (DWORD)serverTimeout)
            now = start + (DWORD)serverTimeout;

        CWorkerWaitSatisfiedReason reason;
        WaitForFinishOrESC(parent, serverTimeout - (now - start), &waitWnd,
                           reason, postWM_CLOSE, lastWorkerMayBeClosedState);
        BOOL terminate = FALSE;
        switch (reason)
        {
        case wwsrEsc:
        {
            waitWnd.Show(FALSE);
            if (SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, termConResID).c_str(),
                                                 SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                 MB_YESNO | MSGBOXEX_ESCAPEENABLED |
                                                     MB_ICONQUESTION) == IDYES)
            { // cancel
                terminate = TRUE;
            }
            else
            {
                SalamanderGeneral->WaitForESCRelease(); // safeguard so that subsequent actions are not interrupted after every ESC in the previous message box
                waitWnd.Show(TRUE);
            }
            break;
        }

        case wwsrTimeout:
        {
            terminate = TRUE;
            break;
        }

            // case wwsrWorkerSocketClosure: break;  // only check whether all worker sockets are already closed
        }

        if (terminate) // cancel or timeout, force workers to finish quickly and continue waiting for them to end
        {
            lastOpIndex = 0; // optimization (index of the last processed operation - start with it in the next round)
            while (1)
            {
                BOOL done = TRUE;
                int count = 0;
                HANDLES(EnterCriticalSection(&OpListCritSect));
                if (operUID != -1) // applies only to a single operation
                {
                    if (operUID >= 0 && operUID < Operations.Count)
                    {
                        CFTPOperation* oper = Operations[operUID];
                        if (oper != NULL)
                            done &= !oper->ForceCloseWorkers(workerInd, victims, WORKERS_VICTIMS_COUNT, &count);
                    }
                }
                else // applies to all operations and all workers
                {
                    int i;
                    for (i = lastOpIndex; count < WORKERS_VICTIMS_COUNT && i < Operations.Count; i++)
                    {
                        lastOpIndex = i;
                        CFTPOperation* oper = Operations[i];
                        if (oper != NULL)
                            done &= !oper->ForceCloseWorkers(-1, victims, WORKERS_VICTIMS_COUNT, &count);
                    }
                    done &= (i >= Operations.Count);
                }
                HANDLES(LeaveCriticalSection(&OpListCritSect));

                int i;
                for (i = 0; i < count; i++)
                    victims[i]->ForceClose();
                if (done)
                    break; // all operations reported they do not want another call
            }
        }

        // check whether we can already close all workers
        BOOL finished = TRUE;
        HANDLES(EnterCriticalSection(&OpListCritSect));
        if (operUID != -1) // applies only to a single operation
        {
            if (operUID >= 0 && operUID < Operations.Count)
            {
                CFTPOperation* oper = Operations[operUID];
                if (oper != NULL)
                    finished &= oper->CanCloseWorkers(workerInd);
            }
        }
        else // applies to all operations and all workers
        {
            int i;
            for (i = 0; i < Operations.Count; i++)
            {
                CFTPOperation* oper = Operations[i];
                if (oper != NULL)
                    finished &= oper->CanCloseWorkers(-1);
            }
        }
        HANDLES(LeaveCriticalSection(&OpListCritSect));
        if (!finished)
            Sleep(100); // if the worker sockets were tested, wait a moment before the next possible test (prevents unnecessary machine load)
        else
            break; // worker sockets are closed, we can finish
    }
    waitWnd.Destroy();

    // remove finished workers (first release their data - return the operation items back to the queue)
    CUploadWaitingWorker* uploadFirstWaitingWorker = NULL; // upload operation: list of workers waiting for listings retrieved by some of the terminated workers (i.e. workers that need to be informed that the workers they waited on have finished)
    lastOpIndex = 0;                                       // optimization (index of the last processed operation - start with it in the next round)
    while (1)
    {
        BOOL done = TRUE;
        int count = 0;
        HANDLES(EnterCriticalSection(&OpListCritSect));
        if (operUID != -1) // applies only to a single operation
        {
            if (operUID >= 0 && operUID < Operations.Count)
            {
                CFTPOperation* oper = Operations[operUID];
                if (oper != NULL)
                    done &= !oper->DeleteWorkers(workerInd, victims, WORKERS_VICTIMS_COUNT, &count, &uploadFirstWaitingWorker);
            }
        }
        else // applies to all operations and all workers
        {
            int i;
            for (i = lastOpIndex; count < WORKERS_VICTIMS_COUNT && i < Operations.Count; i++)
            {
                lastOpIndex = i;
                CFTPOperation* oper = Operations[i];
                if (oper != NULL)
                    done &= !oper->DeleteWorkers(-1, victims, WORKERS_VICTIMS_COUNT, &count, &uploadFirstWaitingWorker);
            }
            done &= (i >= Operations.Count);
        }
        HANDLES(LeaveCriticalSection(&OpListCritSect));

        // upload operation: inform workers waiting for listings obtained by the terminating workers
        while (uploadFirstWaitingWorker != NULL)
        {
            SocketsThread->PostSocketMessage(uploadFirstWaitingWorker->WorkerMsg,
                                             uploadFirstWaitingWorker->WorkerUID,
                                             WORKER_TGTPATHLISTINGFINISHED, NULL);

            CUploadWaitingWorker* del = uploadFirstWaitingWorker;
            uploadFirstWaitingWorker = uploadFirstWaitingWorker->NextWorker;
            delete del;
        }

        int i;
        for (i = 0; i < count; i++)
            DeleteSocket(victims[i]);
        if (done)
            break; // all operations reported they do not want another call
    }

    Logs.RefreshListOfLogsInLogsDlg(); // refresh the Log - workers are closed

    // drop the wait cursor over the parent
    if (winParent != NULL)
    {
        winParent->DetachWindow();
        delete winParent;
    }

    // enable 'parent'
    EnableWindow(parent, TRUE);
    // if 'parent' is active, restore the focus as well
    if (GetForegroundWindow() == parent)
    {
        if (parent == SalamanderGeneral->GetMainWindowHWND())
            SalamanderGeneral->RestoreFocusInSourcePanel();
        else
        {
            if (focusedWnd != NULL)
                SetFocus(focusedWnd);
        }
    }

    // if WM_CLOSE arrived while waiting for the workers to finish, post it to the queue now,
    // it can be processed now
    if (postWM_CLOSE)
        PostMessage(parent, WM_CLOSE, 0, 0);
}

void CFTPOperationsList::PauseWorkers(HWND parent, int operUID, int workerInd, BOOL pause)
{
    CALL_STACK_MESSAGE4("CFTPOperationsList::PauseWorkers(, %d, %d, %d)", operUID, workerInd, pause);

    // ensure the relevant workers learn that they should perform pause/resume
    CFTPWorker* victims[WORKERS_VICTIMS_COUNT]; // necessary to avoid calling socket operations inside the OpListCritSect section
    int lastOpIndex = 0;                        // optimization (index of the last processed operation - start with it in the next round)
    while (1)
    {
        BOOL done = TRUE;
        int count = 0;
        HANDLES(EnterCriticalSection(&OpListCritSect));
        if (operUID != -1) // applies only to a single operation
        {
            if (operUID >= 0 && operUID < Operations.Count)
            {
                CFTPOperation* oper = Operations[operUID];
                if (oper != NULL)
                    done &= !oper->InformWorkersAboutPause(workerInd, victims, WORKERS_VICTIMS_COUNT, &count, pause);
            }
        }
        else // applies to all operations and all workers
        {
            int i;
            for (i = lastOpIndex; count < WORKERS_VICTIMS_COUNT && i < Operations.Count; i++)
            {
                lastOpIndex = i;
                CFTPOperation* oper = Operations[i];
                if (oper != NULL)
                    done &= !oper->InformWorkersAboutPause(-1, victims, WORKERS_VICTIMS_COUNT, &count, pause);
            }
            done &= (i >= Operations.Count);
        }
        HANDLES(LeaveCriticalSection(&OpListCritSect));

        int i;
        for (i = 0; i < count; i++)
            victims[i]->PostShouldPauseOrResume();
        if (done)
            break; // all operations reported they do not want another call
    }

    // let every operation check whether a full pause/resume happened
    // (stop/start the timer + reset meters immediately after resuming)
    HANDLES(EnterCriticalSection(&OpListCritSect));
    if (operUID != -1) // applies only to a single operation
    {
        if (operUID >= 0 && operUID < Operations.Count)
        {
            CFTPOperation* oper = Operations[operUID];
            if (oper != NULL)
                oper->OperationStatusMaybeChanged();
        }
    }
    else // applies to all operations and all workers
    {
        int i;
        for (i = 0; i < Operations.Count; i++)
        {
            CFTPOperation* oper = Operations[i];
            if (oper != NULL)
                oper->OperationStatusMaybeChanged();
        }
    }
    HANDLES(LeaveCriticalSection(&OpListCritSect));
}

BOOL CFTPOperationsList::StartCall(int operUID)
{
    HANDLES(EnterCriticalSection(&OpListCritSect));
    CallOK = TRUE;
    if (operUID >= 0 && operUID < Operations.Count && Operations[operUID] != NULL)
        return TRUE;
    else
    {
        TRACE_E("Unexpected situation in CFTPOperationsList::StartCall(): operUID is invalid!");
        return CallOK = FALSE;
    }
}

BOOL CFTPOperationsList::EndCall()
{
    HANDLES(LeaveCriticalSection(&OpListCritSect));
    return CallOK;
}

BOOL CFTPOperationsList::ActivateOperationDlg(int operUID, BOOL& success, HWND dropTargetWnd)
{
    CALL_STACK_MESSAGE2("CFTPOperationsList::ActivateOperationDlg(%d, ,)", operUID);
    if (StartCall(operUID))
        success = Operations[operUID]->ActivateOperationDlg(dropTargetWnd);
    else
        success = FALSE;
    return EndCall();
}

BOOL CFTPOperationsList::CloseOperationDlg(int operUID, HANDLE* dlgThread)
{
    CALL_STACK_MESSAGE2("CFTPOperationsList::CloseOperationDlg(%d,)", operUID);
    if (StartCall(operUID))
        Operations[operUID]->CloseOperationDlg(dlgThread);
    return EndCall();
}

BOOL CFTPOperationsList::CanMakeChangesOnPath(const wchar_t* user, const wchar_t* host, unsigned short port,
                                              const char* path, CFTPServerPathType pathType,
                                              int ignoreOperUID)
{
    CALL_STACK_MESSAGE1("CFTPOperationsList::CanMakeChangesOnPath()");

    HANDLES(EnterCriticalSection(&OpListCritSect));
    BOOL ret = FALSE;
    int i;
    for (i = 0; i < Operations.Count; i++)
    {
        CFTPOperation* oper = Operations[i];
        if (oper != NULL && oper->GetUID() != ignoreOperUID &&
            oper->CanMakeChangesOnPath(user, host, port, path, pathType))
        {
            ret = TRUE;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&OpListCritSect));
    return ret;
}

BOOL CFTPOperationsList::IsUploadingToServer(const wchar_t* user, const wchar_t* host, unsigned short port)
{
    CALL_STACK_MESSAGE1("CFTPOperationsList::IsUploadingToServer()");

    HANDLES(EnterCriticalSection(&OpListCritSect));
    BOOL ret = FALSE;
    int i;
    for (i = 0; i < Operations.Count; i++)
    {
        CFTPOperation* oper = Operations[i];
        if (oper != NULL && oper->IsUploadingToServer(user, host, port))
        {
            ret = TRUE;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&OpListCritSect));
    return ret;
}

//
// ****************************************************************************
// CFTPQueue
//

CFTPQueue::CFTPQueue(const CFtpTextCodec& textCodec) : Items(100, 500), TextCodec(textCodec)
{
    HANDLES(InitializeCriticalSection(&QueueCritSect));
    LastFoundUID = -1;
    LastFoundIndex = 0;
    FirstWaitingItemIndex = 0;
    GetOnlyExploreAndResolveItems = TRUE;
    ExploreAndResolveItemsCount = 0;
    DoneOrSkippedItemsCount = 0;
    WaitingOrProcessingOrDelayedItemsCount = 0;
    DoneOrSkippedByteSize.Set(0, 0);
    DoneOrSkippedBlockSize.Set(0, 0);
    WaitingOrProcessingOrDelayedByteSize.Set(0, 0);
    WaitingOrProcessingOrDelayedBlockSize.Set(0, 0);
    DoneOrSkippedUploadSize.Set(0, 0);
    WaitingOrProcessingOrDelayedUploadSize.Set(0, 0);
    CopyUnknownSizeCount = 0;
    CopyUnknownSizeCountIfUnknownBlockSize = 0;
    LastErrorOccurenceTime = -1;
    LastFoundErrorOccurenceTime = -1;
}

CFTPQueue::~CFTPQueue()
{
    HANDLES(DeleteCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateCounters(CFTPQueueItem* item, BOOL add)
{
    if (item->IsExploreOrResolveItem())
    {
        if (add)
            ExploreAndResolveItemsCount++;
        else
            ExploreAndResolveItemsCount--;
    }

    if (item->Type != fqitCopyFileOrFileLink && item->Type != fqitMoveFileOrFileLink &&
        item->Type != fqitUploadCopyFile && item->Type != fqitUploadMoveFile &&
        item->GetItemState() != sqisDone && item->GetItemState() != sqisSkipped)
    {
        if (add)
            CopyUnknownSizeCount++;
        else
            CopyUnknownSizeCount--;
    }

    switch (item->GetItemState())
    {
    case sqisDone:
    case sqisSkipped:
    {
        if (add)
            DoneOrSkippedItemsCount++;
        else
            DoneOrSkippedItemsCount--;

        if (item->Type == fqitCopyFileOrFileLink || item->Type == fqitMoveFileOrFileLink)
        {
            CFTPQueueItemCopyOrMove* copyItem = (CFTPQueueItemCopyOrMove*)item;
            if (copyItem->Size != CQuadWord(-1, -1))
            {
                if (copyItem->SizeInBytes)
                {
                    if (add)
                        DoneOrSkippedByteSize += copyItem->Size;
                    else
                        DoneOrSkippedByteSize -= copyItem->Size;
                }
                else
                {
                    if (add)
                        DoneOrSkippedBlockSize += copyItem->Size;
                    else
                        DoneOrSkippedBlockSize -= copyItem->Size;
                }
            }
        }
        else
        {
            if (item->Type == fqitUploadCopyFile || item->Type == fqitUploadMoveFile)
            {
                if (add)
                    DoneOrSkippedUploadSize += ((CFTPQueueItemCopyOrMoveUpload*)item)->Size;
                else
                    DoneOrSkippedUploadSize -= ((CFTPQueueItemCopyOrMoveUpload*)item)->Size;
            }
        }
        break;
    }

    case sqisWaiting:
    case sqisProcessing:
    case sqisDelayed:
    {
        if (add)
            WaitingOrProcessingOrDelayedItemsCount++;
        else
            WaitingOrProcessingOrDelayedItemsCount--;

        if (item->Type == fqitCopyFileOrFileLink || item->Type == fqitMoveFileOrFileLink)
        {
            CFTPQueueItemCopyOrMove* copyItem = (CFTPQueueItemCopyOrMove*)item;
            if (copyItem->Size != CQuadWord(-1, -1))
            {
                if (copyItem->SizeInBytes)
                {
                    if (add)
                        WaitingOrProcessingOrDelayedByteSize += copyItem->Size;
                    else
                        WaitingOrProcessingOrDelayedByteSize -= copyItem->Size;
                }
                else
                {
                    if (add)
                    {
                        WaitingOrProcessingOrDelayedBlockSize += copyItem->Size;
                        CopyUnknownSizeCountIfUnknownBlockSize++;
                    }
                    else
                    {
                        WaitingOrProcessingOrDelayedBlockSize -= copyItem->Size;
                        CopyUnknownSizeCountIfUnknownBlockSize--;
                    }
                }
            }
            else
            {
                if (add)
                    CopyUnknownSizeCount++;
                else
                    CopyUnknownSizeCount--;
            }
        }
        else
        {
            if (item->Type == fqitUploadCopyFile || item->Type == fqitUploadMoveFile)
            {
                if (add)
                    WaitingOrProcessingOrDelayedUploadSize += ((CFTPQueueItemCopyOrMoveUpload*)item)->Size;
                else
                    WaitingOrProcessingOrDelayedUploadSize -= ((CFTPQueueItemCopyOrMoveUpload*)item)->Size;
            }
        }
        break;
    }

    default: // sqisFailed, sqisUserInputNeeded, sqisForcedToFail
    {
        if (item->Type == fqitCopyFileOrFileLink || item->Type == fqitMoveFileOrFileLink)
        {
            CFTPQueueItemCopyOrMove* copyItem = (CFTPQueueItemCopyOrMove*)item;
            if (copyItem->Size == CQuadWord(-1, -1))
            {
                if (add)
                    CopyUnknownSizeCount++;
                else
                    CopyUnknownSizeCount--;
            }
            else
            {
                if (!copyItem->SizeInBytes)
                {
                    if (add)
                        CopyUnknownSizeCountIfUnknownBlockSize++;
                    else
                        CopyUnknownSizeCountIfUnknownBlockSize--;
                }
            }
        }
        break;
    }
    }
}

BOOL CFTPQueue::AddItem(CFTPQueueItem* newItem)
{
    CALL_STACK_MESSAGE_NONE
    // CALL_STACK_MESSAGE1("CFTPQueue::AddItem()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    BOOL ret = TRUE;
    Items.Add(newItem);
    if (Items.IsGood())
    {
        UpdateCounters(newItem, TRUE);
        if (newItem->IsItemInSimpleErrorState())
            newItem->ErrorOccurenceTime = GiveLastErrorOccurenceTime();
    }
    else
    {
        Items.ResetState();
        ret = FALSE;
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return ret;
}

BOOL CFTPQueue::ReplaceItemWithListOfItems(int itemUID, CFTPQueueItem** items, int itemsCount)
{
    CALL_STACK_MESSAGE3("CFTPQueue::ReplaceItemWithListOfItems(%d, , %d)", itemUID, itemsCount);

    HANDLES(EnterCriticalSection(&QueueCritSect));
    BOOL ret = FALSE;
    if (FindItemWithUID(itemUID) != NULL) // item found
    {
        if (itemsCount > 1)
        {
            Items.Insert(LastFoundIndex + 1, items + 1, itemsCount - 1);
            if (Items.IsGood())
            {
                UpdateCounters(Items[LastFoundIndex], FALSE);
                delete Items[LastFoundIndex];
                Items[LastFoundIndex] = *items;
                LastFoundUID = (*items)->UID;
                int i;
                for (i = LastFoundIndex; i < LastFoundIndex + itemsCount; i++)
                {
                    CFTPQueueItem* item = Items[i];
                    UpdateCounters(item, TRUE);
                    if (item->IsItemInSimpleErrorState())
                        item->ErrorOccurenceTime = GiveLastErrorOccurenceTime();
                }
                ret = TRUE;
                HandleFirstWaitingItemIndex(TRUE, // instead of searching the array we expect the worst case - an explore item at the first position in the array
                                            LastFoundIndex);
            }
            else
                Items.ResetState();
        }
        else
        {
            if (itemsCount == 1)
            {
                UpdateCounters(Items[LastFoundIndex], FALSE);
                delete Items[LastFoundIndex];
                Items[LastFoundIndex] = *items;
                LastFoundUID = (*items)->UID;
                UpdateCounters(*items, TRUE);
                if ((*items)->IsItemInSimpleErrorState())
                    (*items)->ErrorOccurenceTime = GiveLastErrorOccurenceTime();
                HandleFirstWaitingItemIndex((*items)->IsExploreOrResolveItem(), LastFoundIndex);
            }
            else // itemsCount == 0
            {
                if (FirstWaitingItemIndex > LastFoundIndex)
                    FirstWaitingItemIndex--; // the array will shift, adjust FirstWaitingItemIndex
                UpdateCounters(Items[LastFoundIndex], FALSE);
                Items.Delete(LastFoundIndex);
                if (!Items.IsGood())
                    Items.ResetState(); // maximum error when shrinking the array, but the deletion was surely executed
                LastFoundIndex = 0;
                LastFoundUID = -1;
            }
            ret = TRUE;
        }
    }
    else
        TRACE_E("Unexpected situation in CFTPQueue::ReplaceItemWithListOfItems(): item not found: UID=" << itemUID);
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return ret;
}

void CFTPQueue::AddToNotDoneSkippedFailed(int itemDirUID, int notDone, int skipped, int failed,
                                          int uiNeeded, CFTPOperation* oper)
{
    CALL_STACK_MESSAGE6("CFTPQueue::AddToNotDoneSkippedFailed(%d, %d, %d, %d, %d)",
                        itemDirUID, notDone, skipped, uiNeeded, failed);

    HANDLES(EnterCriticalSection(&QueueCritSect));
    CFTPQueueItem* found = FindItemWithUID(itemDirUID);
    if (found != NULL)
    {
        if (found->Type == fqitDeleteDir || found->Type == fqitMoveDeleteDir ||
            found->Type == fqitUploadMoveDeleteDir || found->Type == fqitMoveDeleteDirLink ||
            found->Type == fqitChAttrsDir)
        {
            CFTPQueueItemDir* itemDir = (CFTPQueueItemDir*)found;
            itemDir->ChildItemsNotDone += notDone;
            itemDir->ChildItemsSkipped += skipped;
            itemDir->ChildItemsFailed += failed;
            itemDir->ChildItemsUINeeded += uiNeeded;
            if (itemDir->ChildItemsNotDone < 0 || itemDir->ChildItemsSkipped < 0 ||
                itemDir->ChildItemsFailed < 0 || itemDir->ChildItemsUINeeded < 0)
            {
                TRACE_E("Unexpected situation in CFTPQueue::AddToNotDoneSkippedFailed(): some counter is negative! "
                        "NotDone="
                        << itemDir->ChildItemsNotDone << ", Skipped=" << itemDir->ChildItemsSkipped << ", Failed=" << itemDir->ChildItemsFailed << ", UINeeded=" << itemDir->ChildItemsUINeeded);
            }
            // if an automatic state change is possible, perform it
            if (itemDir->GetItemState() == sqisDelayed || itemDir->GetItemState() == sqisForcedToFail)
            {
                CFTPQueueItemState newState = itemDir->GetStateFromCounters();
                BOOL change = itemDir->GetItemState() != newState;
                if (change)
                {
                    if (newState == sqisWaiting)
                        HandleFirstWaitingItemIndex(FALSE, LastFoundIndex);
                    itemDir->ChangeStateAndCounters(newState, oper, this);
                    oper->ReportItemChange(itemDir->UID);
                }
            }
        }
        else
            TRACE_E("Unexpected situation in CFTPQueue::AddToNotDoneSkippedFailed(): parent item is type=" << found->Type);
    }
    else
        TRACE_E("Unexpected situation in CFTPQueue::AddToNotDoneSkippedFailed(): unknown parent item, UID=" << itemDirUID);
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

int CFTPQueue::GetCount()
{
    CALL_STACK_MESSAGE1("CFTPQueue::GetCount()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    int ret = Items.Count;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return ret;
}

void CFTPQueue::LockForMoreOperations()
{
    CALL_STACK_MESSAGE1("CFTPQueue::LockForMoreOperations()");
    HANDLES(EnterCriticalSection(&QueueCritSect));
}

void CFTPQueue::UnlockForMoreOperations()
{
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

int CFTPQueue::GetUserInputNeededCount(BOOL onlyUINeeded, TDirectArray<DWORD>* UINeededArr,
                                       int focusedItemUID, int* indexInAll, int* indexInUIN)
{
    CALL_STACK_MESSAGE1("CFTPQueue::GetUserInputNeededCount()");

    *indexInAll = -1;
    *indexInUIN = -1;

    HANDLES(EnterCriticalSection(&QueueCritSect));
    int c = 0;
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        CFTPQueueItem* item = Items[i];
        if (item->UID == focusedItemUID)
        {
            *indexInAll = i;
            if (item->GetItemState() >= sqisSkipped /* sqisSkipped, sqisFailed, sqisForcedToFail or sqisUserInputNeeded */)
                *indexInUIN = c;
        }
        if (item->GetItemState() >= sqisSkipped /* sqisSkipped, sqisFailed, sqisForcedToFail or sqisUserInputNeeded */)
        {
            if (onlyUINeeded)
                UINeededArr->Add(i);
            c++;
        }
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    if (!UINeededArr->IsGood())
        UINeededArr->ResetState();
    return c;
}

int CFTPQueue::GetItemUID(int index)
{
    CALL_STACK_MESSAGE1("CFTPQueue::GetItemUID()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    int uid;
    if (index >= 0 && index < Items.Count)
        uid = Items[index]->UID;
    else
        uid = -1;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return uid;
}

int CFTPQueue::GetItemIndex(int itemUID)
{
    CALL_STACK_MESSAGE1("CFTPQueue::GetItemIndex()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    int index;
    if (FindItemWithUID(itemUID) != NULL)
        index = LastFoundIndex;
    else
        index = -1;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return index;
}

void CFTPQueue::GetListViewDataForW(int index, NMLVDISPINFO* lvdi, std::wstring& text) noexcept
{
    LVITEM* itemData = &lvdi->item;
    HANDLES(EnterCriticalSection(&QueueCritSect));
    try
    {
        text.clear();
        if (index < 0 || index >= Items.Count)
        {
            if (itemData->mask & LVIF_IMAGE)
                itemData->iImage = 1;
        }
        else
        {
            CFTPQueueItem* item = Items[index];
            if (itemData->mask & LVIF_IMAGE)
            {
                switch (item->Type)
                {
                case fqitDeleteLink:
                case fqitDeleteFile:
                case fqitCopyResolveLink:
                case fqitMoveResolveLink:
                case fqitCopyFileOrFileLink:
                case fqitMoveFileOrFileLink:
                case fqitChAttrsFile:
                case fqitChAttrsResolveLink:
                case fqitUploadCopyFile:
                case fqitUploadMoveFile:
                    itemData->iImage = 1;
                    break;
                default:
                    itemData->iImage = 0;
                    break;
                }
            }

            if (itemData->mask & LVIF_TEXT)
            {
                if (itemData->iSubItem == 1)
                {
                    std::wstring reason;
                    switch (item->GetItemState())
                    {
                    case sqisDone:
                        text = LangStr(IDS_OPERDLGOPSTS_FINISHED);
                        break;
                    case sqisSkipped:
                        if (!item->GetProblemDescr(TextCodec, reason))
                            FtpStoreWideText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str(), reason);
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDLGOPSTS_SKIPPED).c_str(), reason.c_str());
                        break;
                    case sqisFailed:
                        if (!item->GetProblemDescr(TextCodec, reason))
                            FtpStoreWideText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str(), reason);
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDLGOPSTS_FAILED).c_str(), reason.c_str());
                        break;
                    case sqisForcedToFail:
                        text = LangStr(IDS_OPERDLGOPSTS_FORCEDTOFAIL);
                        break;
                    case sqisUserInputNeeded:
                        if (!item->GetProblemDescr(TextCodec, reason))
                            FtpStoreWideText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str(), reason);
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDLGOPSTS_WAITUSER).c_str(), reason.c_str());
                        break;
                    case sqisWaiting:
                        text = LangStr(IDS_OPERDLGOPSTS_WAITING);
                        break;
                    case sqisProcessing:
                        text = LangStr(IDS_OPERDLGOPSTS_PROCESSING);
                        break;
                    case sqisDelayed:
                        text = LangStr(IDS_OPERDLGOPSTS_DELAYED);
                        break;
                    default:
                        TRACE_E("Unexpected situation in CFTPQueue::GetListViewDataForW(): unknown status!");
                        break;
                    }
                }
                else if (itemData->iSubItem == 0)
                {
                    const std::wstring sourcePath = item->LocalPath != NULL ? item->LocalPath : DecodeRemoteText(item->Path);
                    const std::wstring sourceName = item->LocalName != NULL ? item->LocalName : DecodeRemoteText(item->Name);
                    auto formatSize = [](const CQuadWord& value, BOOL inBytes) {
                        const std::wstring number = inBytes
                                                        ? SPLPrintDiskSizeOwned(SalamanderGeneral, value, 0)
                                                        : SPLNumberToStrOwned(SalamanderGeneral, value);
                        std::wstring size = L" (";
                        size += number;
                        if (!inBytes)
                        {
                            size += L' ';
                            size += LangStr(IDS_OPERDOPDS_SIZEINBLOCKS);
                        }
                        size += L')';
                        return size;
                    };

                    switch (item->Type)
                    {
                    case fqitDeleteLink:
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDOPDS_DELLINK).c_str(), sourceName.c_str(), sourcePath.c_str());
                        break;
                    case fqitDeleteFile:
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDOPDS_DELFILE).c_str(), sourceName.c_str(), sourcePath.c_str());
                        break;
                    case fqitDeleteDir:
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDOPDS_DELDIR).c_str(), sourceName.c_str(), sourcePath.c_str());
                        break;
                    case fqitDeleteExploreDir:
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDOPDS_DELEXPLDIR).c_str(), sourceName.c_str(), sourcePath.c_str());
                        break;
                    case fqitChAttrsExploreDir:
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDOPDS_CHATTREXPLDIR).c_str(), sourceName.c_str(), sourcePath.c_str());
                        break;
                    case fqitCopyResolveLink:
                    case fqitMoveResolveLink:
                    case fqitChAttrsResolveLink:
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDOPDS_RESLINK).c_str(), sourceName.c_str(), sourcePath.c_str());
                        break;
                    case fqitCopyFileOrFileLink:
                    case fqitMoveFileOrFileLink:
                    {
                        CFTPQueueItemCopyOrMove* download = (CFTPQueueItemCopyOrMove*)item;
                        const std::wstring targetPath = download->LocalTgtPath;
                        const std::wstring targetName = download->LocalTgtName;
                        const std::wstring size = download->Size == CQuadWord(-1, -1) ? L"" : formatSize(download->Size, download->SizeInBytes);
                        const std::wstring mode = LangStr(
                            download->AsciiTransferMode ? IDS_OPERDOPDS_ASCIITRMODE : IDS_OPERDOPDS_BINARYTRMODE);
                        if (targetName == sourceName)
                            text = SPLFormatStringOwned(LangStr(item->Type == fqitCopyFileOrFileLink ? IDS_OPERDOPDS_COPY1 : IDS_OPERDOPDS_MOVE1).c_str(), sourceName.c_str(), size.c_str(), sourcePath.c_str(), targetPath.c_str(), mode.c_str());
                        else
                            text = SPLFormatStringOwned(LangStr(item->Type == fqitCopyFileOrFileLink ? IDS_OPERDOPDS_COPY2 : IDS_OPERDOPDS_MOVE2).c_str(), sourceName.c_str(), size.c_str(), sourcePath.c_str(), targetPath.c_str(), targetName.c_str(), mode.c_str());
                        break;
                    }
                    case fqitUploadCopyFile:
                    case fqitUploadMoveFile:
                    {
                        CFTPQueueItemCopyOrMoveUpload* upload = (CFTPQueueItemCopyOrMoveUpload*)item;
                        const std::wstring targetPath = DecodeRemoteText(upload->TgtPath);
                        const std::wstring targetName = DecodeRemoteText(upload->RenamedName != NULL ? upload->RenamedName : upload->TgtName);
                        const std::wstring size = formatSize(upload->Size, TRUE);
                        const std::wstring mode = LangStr(
                            upload->AsciiTransferMode ? IDS_OPERDOPDS_ASCIITRMODE : IDS_OPERDOPDS_BINARYTRMODE);
                        if (targetName == sourceName)
                            text = SPLFormatStringOwned(LangStr(item->Type == fqitUploadCopyFile ? IDS_OPERDOPDS_COPY1 : IDS_OPERDOPDS_MOVE1).c_str(), sourceName.c_str(), size.c_str(), sourcePath.c_str(), targetPath.c_str(), mode.c_str());
                        else
                            text = SPLFormatStringOwned(LangStr(item->Type == fqitUploadCopyFile ? IDS_OPERDOPDS_COPY2 : IDS_OPERDOPDS_MOVE2).c_str(), sourceName.c_str(), size.c_str(), sourcePath.c_str(), targetPath.c_str(), targetName.c_str(), mode.c_str());
                        break;
                    }
                    case fqitMoveDeleteDir:
                    case fqitUploadMoveDeleteDir:
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDOPDS_MOVEDELDIR).c_str(), sourceName.c_str(), sourcePath.c_str());
                        break;
                    case fqitMoveDeleteDirLink:
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDOPDS_MOVEDELDIRLNK).c_str(), sourceName.c_str(), sourcePath.c_str());
                        break;
                    case fqitCopyExploreDir:
                    case fqitMoveExploreDir:
                    case fqitMoveExploreDirLink:
                    {
                        CFTPQueueItemCopyMoveExplore* download = (CFTPQueueItemCopyMoveExplore*)item;
                        const int textID = item->Type == fqitCopyExploreDir ? IDS_OPERDOPDS_COPYEXPLDIR :
                                           item->Type == fqitMoveExploreDir ? IDS_OPERDOPDS_MOVEEXPLDIR : IDS_OPERDOPDS_MOVEEXPLDIRLNK;
                        text = SPLFormatStringOwned(LangStr(textID).c_str(), sourceName.c_str(), sourcePath.c_str(), download->LocalTgtPath, download->LocalTgtName);
                        break;
                    }
                    case fqitUploadCopyExploreDir:
                    case fqitUploadMoveExploreDir:
                    {
                        CFTPQueueItemCopyMoveUploadExplore* upload = (CFTPQueueItemCopyMoveUploadExplore*)item;
                        const std::wstring targetPath = DecodeRemoteText(upload->TgtPath);
                        const std::wstring targetName = DecodeRemoteText(upload->TgtName);
                        text = SPLFormatStringOwned(LangStr(item->Type == fqitUploadCopyExploreDir ? IDS_OPERDOPDS_COPYEXPLDIR : IDS_OPERDOPDS_MOVEEXPLDIR).c_str(), sourceName.c_str(), sourcePath.c_str(), targetPath.c_str(), targetName.c_str());
                        break;
                    }
                    case fqitChAttrsFile:
                    case fqitChAttrsDir:
                    {
                        const WORD attr = item->Type == fqitChAttrsFile ? ((CFTPQueueItemChAttr*)item)->Attr : ((CFTPQueueItemChAttrDir*)item)->Attr;
                        std::string rights;
                        std::wstring rightsText;
                        if (!FTPFormatUNIXRights(rights, attr) ||
                            !FtpDecodeLocalText(rights, rightsText))
                            break;
                        text = SPLFormatStringOwned(LangStr(item->Type == fqitChAttrsFile ? IDS_OPERDOPDS_CHATTRFILE : IDS_OPERDOPDS_CHATTRDIR).c_str(), sourceName.c_str(), sourcePath.c_str(), rightsText.c_str());
                        break;
                    }
                    case fqitChAttrsExploreDirLink:
                        text = SPLFormatStringOwned(LangStr(IDS_OPERDOPDS_CHATTREXPLDIRLNK).c_str(), sourceName.c_str(), sourcePath.c_str());
                        break;
                    }
                }
            }
        }

        if (itemData->mask & LVIF_TEXT)
            itemData->pszText = const_cast<LPWSTR>(text.c_str());
    }
    catch (...)
    {
        text.clear();
        if (itemData->mask & LVIF_TEXT)
            itemData->pszText = const_cast<LPWSTR>(text.c_str());
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

std::wstring CFTPQueue::DecodeRemoteText(const char* bytes) const
{
    std::wstring text;
    const char* value = bytes != NULL ? bytes : "";
    FtpDecodeServerTextForPresentation(TextCodec,
                                       std::string_view(value, strlen(value)), text);
    return text;
}

BOOL CFTPQueue::IsItemWithErrorToSolve(int index, BOOL* canSkip, BOOL* canRetry)
{
    CALL_STACK_MESSAGE1("CFTPQueue::IsItemWithErrorToSolve(,,)");

    BOOL ret = FALSE;
    if (canSkip != NULL)
        *canSkip = FALSE;
    if (canRetry != NULL)
        *canRetry = FALSE;
    HANDLES(EnterCriticalSection(&QueueCritSect));
    if (index >= 0 && index < Items.Count) // index is valid
    {
        CFTPQueueItem* item = Items[index];
        ret = item->HasErrorToSolve(canSkip, canRetry);
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return ret;
}

CFTPQueueItem*
CFTPQueue::FindItemWithUID(int UID)
{
    if (LastFoundUID == UID && LastFoundIndex < Items.Count &&
        Items[LastFoundIndex]->UID == LastFoundUID)
    {
        return Items[LastFoundIndex]; // found in the cache; no need to traverse the entire array
    }
    else
    {
        int i;
        for (i = 0; i < Items.Count; i++)
        {
            CFTPQueueItem* item = Items[i];
            if (item->UID == UID)
            {
                LastFoundUID = UID;
                LastFoundIndex = i;
                return item;
            }
        }
    }
    return NULL;
}

void CFTPQueue::HandleFirstWaitingItemIndex(BOOL exploreOrResolveItem, int itemIndex)
{
    if (!GetOnlyExploreAndResolveItems && exploreOrResolveItem)
    {
        GetOnlyExploreAndResolveItems = TRUE;
        FirstWaitingItemIndex = itemIndex;
    }
    else
    {
        if ((!GetOnlyExploreAndResolveItems || exploreOrResolveItem) &&
            FirstWaitingItemIndex > itemIndex) // FirstWaitingItemIndex needs to be updated
        {
            FirstWaitingItemIndex = itemIndex;
        }
    }
}

int CFTPQueue::SkipItem(int UID, CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::SkipItem()");

    int ret = -1;
    HANDLES(EnterCriticalSection(&QueueCritSect));
    CFTPQueueItem* found = FindItemWithUID(UID);
    if (found != NULL)
    {
        BOOL canSkip;
        found->HasErrorToSolve(&canSkip, NULL);
        if (canSkip)
        {
            ret = LastFoundIndex;
            found->ChangeStateAndCounters(sqisSkipped, oper, this);
            if (found->ProblemID == ITEMPR_OK)
                found->ProblemID = ITEMPR_SKIPPEDBYUSER;
            found->ForceAction = fqiaNone; // just to be safe (probably not needed)
        }
        else
            TRACE_I("CFTPQueue::SkipItem(): cannot skip item because it's not in any of skip-possible states!");
    }
    else
        TRACE_I("CFTPQueue::SkipItem(): cannot skip item because it's not found!");
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return ret;
}

int CFTPQueue::RetryItem(int UID, CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::RetryItem()");

    int ret = -1;
    HANDLES(EnterCriticalSection(&QueueCritSect));
    CFTPQueueItem* found = FindItemWithUID(UID);
    if (found != NULL)
    {
        BOOL canRetry;
        found->HasErrorToSolve(NULL, &canRetry);
        if (canRetry)
        {
            if (found->Type == fqitUploadCopyExploreDir || found->Type == fqitUploadMoveExploreDir ||
                found->Type == fqitUploadCopyFile || found->Type == fqitUploadMoveFile)
            {
                const wchar_t* hostBuf = NULL;
                const wchar_t* userBuf = NULL;
                unsigned short portBuf;
                oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                UploadListingCache.RemoveNotAccessibleListings(userBuf, hostBuf, portBuf);
            }
            CFTPQueueItemState newState = sqisWaiting;
            if (found->Type == fqitDeleteDir || found->Type == fqitMoveDeleteDir ||
                found->Type == fqitUploadMoveDeleteDir || found->Type == fqitMoveDeleteDirLink ||
                found->Type == fqitChAttrsDir)
            {
                newState = ((CFTPQueueItemDir*)found)->GetStateFromCounters();
            }
            if (newState == sqisWaiting)
                HandleFirstWaitingItemIndex(found->IsExploreOrResolveItem(), LastFoundIndex);
            ret = LastFoundIndex;
            found->ChangeStateAndCounters(newState, oper, this);
            if (found->ProblemID == ITEMPR_UNABLETORESUME)
                oper->SetResumeIsNotSupported(FALSE); // so Retry makes any sense at all
            oper->SetSizeCmdIsSupported(TRUE);        // so that the SIZE command is tried again if needed
            found->ProblemID = ITEMPR_OK;
            found->WinError = NO_ERROR;
            if (found->ErrAllocDescr != NULL)
            {
                SalamanderGeneral->Free(found->ErrAllocDescr);
                found->ErrAllocDescr = NULL;
            }
            found->ForceAction = fqiaNone; // to ensure Autorename is not performed instead of Retry
        }
        else
            TRACE_I("CFTPQueue::RetryItem(): cannot retry item because it's not in any of error states!");
    }
    else
        TRACE_I("CFTPQueue::RetryItem(): cannot retry item because it's not found!");
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return ret;
}

int CFTPQueue::SolveErrorOnItem(HWND parent, int UID, CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::SolveErrorOnItem()");

    int openDlgWithID = 0;
    std::string ftpPath;
    std::string ftpName;
    std::wstring diskPathW;
    std::wstring diskNameW;
    char* newName = NULL;
    wchar_t* newNameW = NULL;
    wchar_t* localNewName = NULL;
    std::string origRightsStorage;
    const char* origRights = NULL;
    WORD newAttr = 0;
    DWORD winError = NO_ERROR;
    BOOL applyToAll = FALSE;
    std::wstring errorDescription;
    auto storeLocalError = [&](std::string_view bytes)
    {
        if (!FtpDecodeLocalText(bytes, errorDescription))
            FtpStoreWideText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str(), errorDescription);
    };
    auto storeServerError = [&](std::string_view bytes)
    {
        if (!FtpDecodeServerTextForPresentation(TextCodec, bytes, errorDescription))
            FtpStoreWideText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str(), errorDescription);
    };
    auto storeWinError = [&](DWORD error)
    {
        std::string bytes;
        if (!FTPGetErrorText(error, bytes) || !FtpDecodeLocalText(bytes, errorDescription))
            FtpStoreWideText(LangStr(IDS_OPERDOPPR_LOWMEM).c_str(), errorDescription);
    };
    BOOL isUploadItem = FALSE;
    const wchar_t* hostBuf = NULL;
    const wchar_t* userBuf = NULL;
    unsigned short portBuf;

    HANDLES(EnterCriticalSection(&QueueCritSect));
    CFTPQueueItem* found = FindItemWithUID(UID);
    if (found != NULL)
    {
        if (found->LocalPath != NULL)
            FtpStoreWideText(found->LocalPath, diskPathW);
        if (found->LocalName != NULL)
            FtpStoreWideText(found->LocalName, diskNameW);
        if (found->Type == fqitCopyResolveLink || found->Type == fqitMoveResolveLink ||
            found->Type == fqitCopyFileOrFileLink || found->Type == fqitMoveFileOrFileLink)
        {
            CFTPQueueItemCopyOrMove* download = (CFTPQueueItemCopyOrMove*)found;
            FtpStoreWideText(download->LocalTgtPath != NULL ? download->LocalTgtPath : L"", diskPathW);
            FtpStoreWideText(download->LocalTgtName != NULL ? download->LocalTgtName : L"", diskNameW);
        }
        else if (found->Type == fqitCopyExploreDir || found->Type == fqitMoveExploreDir ||
                 found->Type == fqitMoveExploreDirLink)
        {
            CFTPQueueItemCopyMoveExplore* download = (CFTPQueueItemCopyMoveExplore*)found;
            FtpStoreWideText(download->LocalTgtPath != NULL ? download->LocalTgtPath : L"", diskPathW);
            FtpStoreWideText(download->LocalTgtName != NULL ? download->LocalTgtName : L"", diskNameW);
        }
        if (found->HasErrorToSolve(NULL, NULL))
        {
            if (found->Type == fqitUploadCopyExploreDir || found->Type == fqitUploadMoveExploreDir)
            {
                isUploadItem = TRUE;
                const CFTPQueueItemCopyMoveUploadExplore* upload = (CFTPQueueItemCopyMoveUploadExplore*)found;
                FtpStoreProtocolBytes(upload->TgtPath != NULL ? upload->TgtPath : "", ftpPath);
                FtpStoreProtocolBytes(upload->TgtName != NULL ? upload->TgtName : "", ftpName);
            }
            else
            {
                if (found->Type == fqitUploadCopyFile || found->Type == fqitUploadMoveFile)
                {
                    isUploadItem = TRUE;
                    const CFTPQueueItemCopyOrMoveUpload* upload = (CFTPQueueItemCopyOrMoveUpload*)found;
                    FtpStoreProtocolBytes(upload->TgtPath != NULL ? upload->TgtPath : "", ftpPath);
                    FtpStoreProtocolBytes(upload->TgtName != NULL ? upload->TgtName : "", ftpName);
                }
                else
                {
                    FtpStoreProtocolBytes(found->Path != NULL ? found->Path : "", ftpPath);
                    FtpStoreProtocolBytes(found->Name != NULL ? found->Name : "", ftpName);
                }
            }
            switch (found->ProblemID)
            {
            case ITEMPR_LOWMEM:
            {
                if (isUploadItem)
                {
                }
                openDlgWithID = 1;
                break;
            }

            case ITEMPR_UNABLETOCWD:
            case ITEMPR_UNABLETOCWDONLYPATH:
            case ITEMPR_UNABLETOPWD:
            case ITEMPR_UNABLETORESOLVELNK:
            case ITEMPR_UNABLETODELETEFILE:
            case ITEMPR_UNABLETODELETEDIR:
            case ITEMPR_UNABLETOCHATTRS:
            case ITEMPR_UPLOADCANNOTLISTTGTPATH:
            case ITEMPR_UPLOADCANNOTLISTSRCPATH:
            case ITEMPR_UNABLETODELETEDISKDIR:
            case ITEMPR_UPLOADCANNOTOPENSRCFILE:
            case ITEMPR_SRCFILEREADERROR:
            case ITEMPR_UNABLETODELETEDISKFILE:
            {
                if (found->ProblemID == ITEMPR_UNABLETOCWDONLYPATH ||
                    found->ProblemID == ITEMPR_UPLOADCANNOTLISTTGTPATH)
                {
                    ftpName.clear();
                }
                if (found->ProblemID == ITEMPR_UPLOADCANNOTLISTSRCPATH ||
                    found->ProblemID == ITEMPR_UNABLETODELETEDISKDIR ||
                    found->ProblemID == ITEMPR_UPLOADCANNOTOPENSRCFILE ||
                    found->ProblemID == ITEMPR_SRCFILEREADERROR ||
                    found->ProblemID == ITEMPR_UNABLETODELETEDISKFILE)
                {
                    ftpPath.clear();
                    ftpName.clear();
                }
                openDlgWithID = found->ProblemID == ITEMPR_UNABLETOPWD ? 13 : found->ProblemID == ITEMPR_UNABLETOCWD || found->ProblemID == ITEMPR_UNABLETOCWDONLYPATH ? 12
                                                                          : found->ProblemID == ITEMPR_UNABLETORESOLVELNK                                              ? 17
                                                                          : found->ProblemID == ITEMPR_UNABLETODELETEFILE                                              ? 18
                                                                          : found->ProblemID == ITEMPR_UNABLETODELETEDIR                                               ? 19
                                                                          : found->ProblemID == ITEMPR_UNABLETOCHATTRS                                                 ? 20
                                                                          : found->ProblemID == ITEMPR_UPLOADCANNOTLISTTGTPATH                                         ? 30
                                                                          : found->ProblemID == ITEMPR_UPLOADCANNOTLISTSRCPATH                                         ? 31
                                                                          : found->ProblemID == ITEMPR_UNABLETODELETEDISKDIR                                           ? 33
                                                                          : found->ProblemID == ITEMPR_UPLOADCANNOTOPENSRCFILE                                         ? 35
                                                                          : found->ProblemID == ITEMPR_SRCFILEREADERROR                                                ? 39
                                                                                                                                                                       : 41;
                if (found->ErrAllocDescr != NULL)
                    storeServerError(found->ErrAllocDescr);
                else
                {
                    if (found->WinError != NO_ERROR)
                        storeWinError(found->WinError);
                    else
                    {
                        if (found->ProblemID == ITEMPR_UPLOADCANNOTLISTTGTPATH)
                            storeLocalError(LoadStr(IDS_OPERDOPPR_UPLCANTLISTTGTPATH));
                        else
                            storeLocalError(LoadStr(IDS_UNKNOWNERROR));
                    }
                }
                break;
            }

            case ITEMPR_INCOMPLETELISTING:
            {
                openDlgWithID = 14;
                if (found->ErrAllocDescr != NULL)
                    storeServerError(found->ErrAllocDescr);
                else
                {
                    if (found->WinError != NO_ERROR)
                        storeWinError(found->WinError);
                    else
                        storeLocalError(LoadStr(IDS_UNKNOWNERROR));
                }
                break;
            }

            case ITEMPR_LISTENFAILURE:
            {
                if (found->Type == fqitUploadCopyFile || found->Type == fqitUploadMoveFile ||
                    found->Type == fqitUploadCopyExploreDir || found->Type == fqitUploadMoveExploreDir)
                {
                    openDlgWithID = 49;
                }
                else
                    openDlgWithID = 15;
                if (found->WinError != NO_ERROR)
                    storeWinError(found->WinError);
                else
                    storeLocalError(LoadStr(IDS_UNKNOWNERROR));
                break;
            }

            case ITEMPR_UNABLETOPARSELISTING:
                openDlgWithID = 16;
                break;
            case ITEMPR_TGTFILEINUSE:
                openDlgWithID = 37;
                break;
            case ITEMPR_SRCFILEINUSE:
                openDlgWithID = 50;
                break;

            case ITEMPR_UNABLETORESUME:
            case ITEMPR_RESUMETESTFAILED:
            {
                if (found->Type == fqitCopyFileOrFileLink || found->Type == fqitMoveFileOrFileLink)
                {
                }
                if (found->Type == fqitUploadCopyFile || found->Type == fqitUploadMoveFile)
                {
                }
                storeLocalError(LoadStr(found->ProblemID == ITEMPR_UNABLETORESUME ? IDS_SSCD2_UNABLETORESUME : IDS_SSCD2_RESUMETESTFAILED));
                openDlgWithID = found->ProblemID == ITEMPR_UNABLETORESUME ? (found->Type == fqitUploadCopyFile ||
                                                                                     found->Type == fqitUploadMoveFile
                                                                                 ? 45
                                                                                 : 21)
                                                                          : 22;
                break;
            }

            case ITEMPR_TGTFILEREADERROR:
            case ITEMPR_TGTFILEWRITEERROR:
            {
                if (found->Type == fqitCopyFileOrFileLink || found->Type == fqitMoveFileOrFileLink)
                {
                }
                if (found->WinError != NO_ERROR)
                    storeWinError(found->WinError);
                else
                    storeLocalError(LoadStr(IDS_UNKNOWNERROR));
                openDlgWithID = found->ProblemID == ITEMPR_TGTFILEREADERROR ? 23 : 24;
                break;
            }

            case ITEMPR_INCOMPLETEDOWNLOAD:
            case ITEMPR_UNABLETODELSRCFILE:
            case ITEMPR_INCOMPLETEUPLOAD:
            case ITEMPR_UPLOADASCIIRESUMENOTSUP:
            case ITEMPR_UPLOADUNABLETORESUMEUNKSIZ:
            case ITEMPR_UPLOADUNABLETORESUMEBIGTGT:
            case ITEMPR_UPLOADTESTIFFINISHEDNOTSUP:
            {
                if (found->Type == fqitCopyFileOrFileLink || found->Type == fqitMoveFileOrFileLink)
                {
                }
                if (found->Type == fqitUploadCopyFile || found->Type == fqitUploadMoveFile)
                {
                }
                switch (found->ProblemID)
                {
                case ITEMPR_UPLOADASCIIRESUMENOTSUP:
                    storeLocalError(LoadStr(IDS_UPLERR_UPLCANTRESUMINASC));
                    break;
                case ITEMPR_UPLOADUNABLETORESUMEUNKSIZ:
                    storeLocalError(LoadStr(IDS_OPERDOPPR_UPUNABLERESUNKSIZ));
                    break;
                case ITEMPR_UPLOADUNABLETORESUMEBIGTGT:
                    storeLocalError(LoadStr(IDS_UPLERR_UPLCANTRESUMBIGTGT));
                    break;
                case ITEMPR_UPLOADTESTIFFINISHEDNOTSUP:
                    storeLocalError(LoadStr(IDS_UPLERR_UPLTESTIFFINNOTSUP));
                    break;

                default:
                {
                    if (found->ErrAllocDescr != NULL)
                        storeServerError(found->ErrAllocDescr);
                    else
                    {
                        if (found->WinError != NO_ERROR)
                            storeWinError(found->WinError);
                        else
                            storeLocalError(LoadStr(IDS_UNKNOWNERROR));
                    }
                    break;
                }
                }
                openDlgWithID = found->ProblemID == ITEMPR_INCOMPLETEDOWNLOAD ? 25 : found->ProblemID == ITEMPR_UNABLETODELSRCFILE       ? 26
                                                                                 : found->ProblemID == ITEMPR_INCOMPLETEUPLOAD           ? 40
                                                                                 : found->ProblemID == ITEMPR_UPLOADASCIIRESUMENOTSUP    ? 44
                                                                                 : found->ProblemID == ITEMPR_UPLOADUNABLETORESUMEUNKSIZ ? 46
                                                                                 : found->ProblemID == ITEMPR_UPLOADUNABLETORESUMEBIGTGT ? 47
                                                                                                                                         : 51;
                break;
            }

            default:
            {
                switch (found->Type)
                {
                case fqitDeleteExploreDir: // explore a directory for delete (note: directory links are removed as a whole, the operation objective is met and nothing "extra" is deleted) (object of class CFTPQueueItemDelExplore)
                {
                    if (found->ProblemID == ITEMPR_DIRISHIDDEN)
                        openDlgWithID = 9;
                    else if (found->ProblemID == ITEMPR_DIRISNOTEMPTY)
                        openDlgWithID = 11;
                    break;
                }

                case fqitDeleteLink: // delete for a link (object of class CFTPQueueItemDel)
                case fqitDeleteFile: // delete for a file (object of class CFTPQueueItemDel)
                {
                    if (found->ProblemID == ITEMPR_FILEISHIDDEN)
                        openDlgWithID = 10;
                    break;
                }
                    /*
            case fqitCopyResolveLink:        // copying: determine whether it is a link to a file or a directory (object of class CFTPQueueItemCopyOrMove)
            case fqitMoveResolveLink:        // move: determine whether it is a link to a file or a directory (object of class CFTPQueueItemCopyOrMove)
            case fqitDeleteDir:              // delete for a directory (object of class CFTPQueueItemDir)
            case fqitMoveDeleteDir:          // delete a directory after moving its contents (object of class CFTPQueueItemDir)
            case fqitMoveDeleteDirLink:      // delete a directory link after moving its contents (object of class CFTPQueueItemDir)
            case fqitChAttrsExploreDir:      // explore a directory to change attributes (also adds an item to change the directory attributes) (object of class CFTPQueueItemChAttrExplore)
            case fqitChAttrsResolveLink:     // change attributes: determine whether it is a link to a directory (object of class CFTPQueueItem)
            case fqitChAttrsExploreDirLink:  // explore a link to a directory to change attributes (object of class CFTPQueueItem)
              break;
*/
                case fqitChAttrsFile: // change file attributes (note: attributes cannot be changed on links) (object of class CFTPQueueItemChAttr)
                case fqitChAttrsDir:  // change directory attributes (object of class CFTPQueueItemChAttrDir)
                {
                    if (found->ProblemID == ITEMPR_UNKNOWNATTRS)
                    {
                        if (found->Type == fqitChAttrsFile)
                        {
                            if (((CFTPQueueItemChAttr*)found)->OrigRights != NULL)
                            {
                                if (FtpStoreProtocolBytes(((CFTPQueueItemChAttr*)found)->OrigRights, origRightsStorage))
                                    origRights = origRightsStorage.c_str();
                            }
                            newAttr = ((CFTPQueueItemChAttr*)found)->Attr;
                        }
                        else // fqitChAttrsDir
                        {
                            if (((CFTPQueueItemChAttrDir*)found)->OrigRights != NULL)
                            {
                                if (FtpStoreProtocolBytes(((CFTPQueueItemChAttrDir*)found)->OrigRights, origRightsStorage))
                                    origRights = origRightsStorage.c_str();
                            }
                            newAttr = ((CFTPQueueItemChAttrDir*)found)->Attr;
                        }
                        openDlgWithID = 8;
                    }
                    break;
                }

                case fqitCopyExploreDir:     // explore a directory or a link to a directory for copying (object of class CFTPQueueItemCopyMoveExplore)
                case fqitMoveExploreDir:     // explore a directory for moving (deletes the directory after completion) (object of class CFTPQueueItemCopyMoveExplore)
                case fqitMoveExploreDirLink: // explore a link to a directory for moving (deletes the link to the directory after completion) (object of class CFTPQueueItemCopyMoveExplore)
                {
                    if (((CFTPQueueItemCopyMoveExplore*)found)->TgtDirState == TGTDIRSTATE_UNKNOWN)
                    {
                        winError = found->WinError;

                        switch (found->ProblemID)
                        {
                        case ITEMPR_CANNOTCREATETGTDIR:
                            openDlgWithID = 2;
                            break;
                        case ITEMPR_TGTDIRALREADYEXISTS:
                            openDlgWithID = 3;
                            break;
                        }
                    }
                    break;
                }

                case fqitUploadCopyExploreDir: // upload: explore a directory for copying (object of class CFTPQueueItemCopyMoveUploadExplore)
                case fqitUploadMoveExploreDir: // upload: explore a directory for moving (deletes the directory after completion) (object of class CFTPQueueItemCopyMoveUploadExplore)
                {
                    if (((CFTPQueueItemCopyMoveUploadExplore*)found)->TgtDirState == UPLOADTGTDIRSTATE_UNKNOWN)
                    {

                        switch (found->ProblemID)
                        {
                        case ITEMPR_UPLOADCANNOTCREATETGTDIR:
                        {
                            if (found->ErrAllocDescr != NULL)
                                storeServerError(found->ErrAllocDescr);
                            else
                            {
                                if (found->WinError == ERROR_ALREADY_EXISTS)
                                    storeLocalError(LoadStr(IDS_UPLERR_CANTCRTGTDIRFILEEX));
                                else
                                    storeLocalError(LoadStr(IDS_UPLERR_CANTCRTGTDIRINV));
                            }
                            openDlgWithID = 28;
                            break;
                        }

                        case ITEMPR_UPLOADTGTDIRALREADYEXISTS:
                            openDlgWithID = 29;
                            break;

                        case ITEMPR_UPLOADCRDIRAUTORENFAILED:
                        {
                            if (found->ErrAllocDescr != NULL)
                                storeServerError(found->ErrAllocDescr);
                            openDlgWithID = 32;
                            break;
                        }
                        }
                    }
                    break;
                }

                case fqitCopyFileOrFileLink: // copying a file or a link to a file (object of class CFTPQueueItemCopyOrMove)
                case fqitMoveFileOrFileLink: // moving a file or a link to a file (object of class CFTPQueueItemCopyOrMove)
                {
                    if (((CFTPQueueItemCopyOrMove*)found)->TgtFileState != TGTFILESTATE_TRANSFERRED)
                    {
                        winError = found->WinError;

                        switch (found->ProblemID)
                        {
                        case ITEMPR_CANNOTCREATETGTFILE:
                            openDlgWithID = 4;
                            break;
                        case ITEMPR_TGTFILEALREADYEXISTS:
                            openDlgWithID = 5;
                            break;
                        case ITEMPR_RETRYONCREATFILE:
                            openDlgWithID = 6;
                            break;
                        case ITEMPR_RETRYONRESUMFILE:
                            openDlgWithID = 7;
                            break;
                        case ITEMPR_ASCIITRFORBINFILE:
                            openDlgWithID = 27;
                            break;
                        }
                    }
                    break;
                }

                case fqitUploadCopyFile: // upload: copying a file (object of class CFTPQueueItemCopyOrMoveUpload)
                case fqitUploadMoveFile: // upload: moving a file (object of class CFTPQueueItemCopyOrMoveUpload)
                {
                    if (((CFTPQueueItemCopyOrMoveUpload*)found)->TgtFileState != UPLOADTGTFILESTATE_TRANSFERRED)
                    {

                        switch (found->ProblemID)
                        {
                        case ITEMPR_UPLOADCANNOTCREATETGTFILE:
                        {
                            if (found->ErrAllocDescr == NULL)
                            {
                                storeLocalError(LoadStr(found->WinError == ERROR_ALREADY_EXISTS ? IDS_UPLERR_CANTCRTGTFILEDIREX : IDS_UPLERR_CANTCRTGTFILEINV));
                            }
                            else
                                storeServerError(found->ErrAllocDescr);
                            openDlgWithID = 34;
                            break;
                        }

                        case ITEMPR_UPLOADTGTFILEALREADYEXISTS:
                            openDlgWithID = 36;
                            break;
                        case ITEMPR_ASCIITRFORBINFILE:
                            openDlgWithID = 38;
                            break;
                        case ITEMPR_RETRYONCREATFILE:
                            openDlgWithID = 42;
                            break;
                        case ITEMPR_RETRYONRESUMFILE:
                            openDlgWithID = 43;
                            break;

                        case ITEMPR_UPLOADFILEAUTORENFAILED:
                        {
                            if (found->ErrAllocDescr != NULL)
                                storeServerError(found->ErrAllocDescr);
                            openDlgWithID = 48;
                            break;
                        }
                        }
                    }
                    break;
                }
                }
                break;
            }
            }
        }
        else
        {
            TRACE_I("CFTPQueue::SolveErrorOnItem(): cannot solve error on item because it's not in any of error states!");
            openDlgWithID = -1;
        }
    }
    else
    {
        TRACE_I("CFTPQueue::SolveErrorOnItem(): cannot solve error on item because it's not found!");
        openDlgWithID = -1;
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));

    std::wstring ftpPathW;
    std::wstring ftpNameW;
    FtpDecodeServerTextForPresentation(TextCodec, ftpPath, ftpPathW);
    FtpDecodeServerTextForPresentation(TextCodec, ftpName, ftpNameW);
    int ret = -2; // no change
    if (openDlgWithID > 0)
    {
        // open the Solve Error dialog
        INT_PTR dlgResult = IDCANCEL;
        switch (openDlgWithID)
        {
        case 1: // insufficient memory
        {
            CSolveLowMemoryErr dlg(parent, isUploadItem ? diskPathW.c_str() : ftpPathW.c_str(), isUploadItem ? diskNameW.c_str() : ftpNameW.c_str(), &applyToAll);
            dlgResult = dlg.Execute();
            break;
        }

        case 2: // error creating the target directory
        {
            CSolveItemErrorDlg dlg(parent, oper, winError, NULL, ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW, sidtCannotCreateTgtDir);
            dlgResult = dlg.Execute();
            break;
        }

        case 3: // target directory already exists
        {
            CSolveItemErrorDlg dlg(parent, oper, winError, NULL, ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW, sidtTgtDirAlreadyExists);
            dlgResult = dlg.Execute();
            break;
        }

        case 4: // error creating the target file
        {
            CSolveItemErrorDlg dlg(parent, oper, winError, NULL, ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW, sidtCannotCreateTgtFile);
            dlgResult = dlg.Execute();
            break;
        }

        case 5: // target file already exists
        {
            CSolveItemErrorDlg dlg(parent, oper, winError, NULL, ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW, sidtTgtFileAlreadyExists);
            dlgResult = dlg.Execute();
            break;
        }

        case 6:  // file transfer error, the file was created or overwritten or resumed with overwrite allowed
        case 42: // upload: file transfer error, the file was created or overwritten or resumed with overwrite allowed
        {
            CSolveItemErrorDlg dlg(parent, oper, winError, NULL, ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW,
                                   openDlgWithID == 6 ? sidtTransferFailedOnCreatedFile : sidtUploadTransferFailedOnCreatedFile);
            dlgResult = dlg.Execute();
            break;
        }

        case 7:  // file transfer error, the file was resumed without the option to overwrite
        case 43: // upload: file transfer error, the file was resumed without the option to overwrite
        {
            CSolveItemErrorDlg dlg(parent, oper, winError, NULL, ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW,
                                   openDlgWithID == 7 ? sidtTransferFailedOnResumedFile : sidtUploadTransferFailedOnResumedFile);
            dlgResult = dlg.Execute();
            break;
        }

        case 8: // error: the file/directory has unknown attributes we cannot preserve (permissions other than 'r'+'w'+'x')
        {
            CSolveItemErrUnkAttrDlg dlg(parent, oper, ftpPathW.c_str(), ftpNameW.c_str(), origRights, newAttr, &applyToAll);
            dlgResult = dlg.Execute();
            if (dlgResult == CM_SIEA_SETNEWATTRS)
            {
                applyToAll = FALSE; // normally attributes are set only for one file/directory (not one set of attributes for all items)
                CSolveItemSetNewAttrDlg dlg2(parent, oper, ftpPathW.c_str(), ftpNameW.c_str(), origRights, &newAttr, &applyToAll);
                if (dlg2.Execute() == IDCANCEL)
                    dlgResult = IDCANCEL;
            }
            break;
        }

        case 9: // delete directory error: the directory is hidden (more a confirmation than an error)
        {
            CSolveItemErrorSimpleDlg dlg(parent, oper, ftpPathW.c_str(), ftpNameW.c_str(),
                                         &applyToAll, sisdtDelHiddenDir);
            dlgResult = dlg.Execute();
            break;
        }

        case 10: // delete file or link error: the file is hidden (more a confirmation than an error)
        {
            CSolveItemErrorSimpleDlg dlg(parent, oper, ftpPathW.c_str(), ftpNameW.c_str(),
                                         &applyToAll, sisdtDelHiddenFile);
            dlgResult = dlg.Execute();
            break;
        }

        case 11: // delete directory error: the directory is not empty (more a confirmation than an error)
        {
            CSolveItemErrorSimpleDlg dlg(parent, oper, ftpPathW.c_str(), ftpNameW.c_str(),
                                         &applyToAll, sisdtDelNonEmptyDir);
            dlgResult = dlg.Execute();
            break;
        }

        case 12: // error changing the working path on the server
        case 13: // error retrieving the working path on the server
        case 14: // unable to load the full list of files and directories from the server
        case 15: // error preparing to open the active data connection
        case 17: // error checking whether the link is a directory or a file
        case 18: // error while deleting a file or link
        case 19: // error while deleting a directory
        case 20: // error changing attributes (chmod)
        case 30: // upload: error listing the target path (cannot detect name collisions in the target directory)
        case 31: // upload: error listing the source path
        case 33: // upload: error deleting the emptied source directory (during Move)
        case 35: // upload: unable to open the source file
        case 39: // upload: error reading the source file
        case 41: // upload: unable to delete the source file on disk (Move)
        case 49: // upload: error preparing to open the active data connection
        {
            BOOL useDiskPathAndName = openDlgWithID == 31 || openDlgWithID == 33 || openDlgWithID == 35 ||
                                      openDlgWithID == 39 || openDlgWithID == 41 || openDlgWithID == 49;
            CSolveServerCmdErr dlg(parent,
                                   openDlgWithID == 12 ? IDS_SSCD_TITLE1 : openDlgWithID == 13                      ? IDS_SSCD_TITLE2
                                                                       : openDlgWithID == 14                        ? IDS_SSCD_TITLE3
                                                                       : openDlgWithID == 15 || openDlgWithID == 49 ? IDS_SSCD_TITLE4
                                                                       : openDlgWithID == 17                        ? IDS_SSCD_TITLE5
                                                                       : openDlgWithID == 18                        ? IDS_SSCD_TITLE7
                                                                       : openDlgWithID == 19                        ? IDS_SSCD_TITLE8
                                                                       : openDlgWithID == 30                        ? IDS_SSCD_TITLE9
                                                                       : openDlgWithID == 31                        ? IDS_SSCD_TITLE10
                                                                       : openDlgWithID == 33                        ? IDS_SSCD_TITLE8
                                                                       : openDlgWithID == 35                        ? IDS_SSCD_TITLE11
                                                                       : openDlgWithID == 39                        ? IDS_SSCD_TITLE12
                                                                       : openDlgWithID == 41                        ? IDS_SSCD2_TITLE5
                                                                                                                    : IDS_SSCD_TITLE6 /* openDlgWithID == 20 */,
                                   useDiskPathAndName ? diskPathW.c_str() : ftpPathW.c_str(),
                                   useDiskPathAndName ? diskNameW.c_str() : ftpNameW.c_str(),
                                   errorDescription.c_str(), &applyToAll,
                                   openDlgWithID == 18 ? siscdtDeleteFile : openDlgWithID == 19 ? siscdtDeleteDir
                                                                                                : siscdtSimple);
            dlgResult = dlg.Execute();
            break;
        }

        case 16: // unknown format of the file and directory list (listing) from the server
        case 37: // target file or link is locked by another operation
        case 50: // source file or link is locked by another operation
        {
            CSolveLowMemoryErr dlg(parent, ftpPathW.c_str(), ftpNameW.c_str(), &applyToAll, openDlgWithID == 16 ? IDS_SCRD_TITLE1 : openDlgWithID == 37 ? IDS_SCRD_TITLE2
                                                                                                                                      : IDS_SCRD_TITLE3);
            dlgResult = dlg.Execute();
            break;
        }

        case 21: // unable to resume (Copy/Move) - server does not support resuming
        case 22: // unable to resume (Copy/Move) - unexpected tail of file (file has changed)
        case 23: // error reading the target file
        case 24: // error writing the target file
        case 25: // Copy/Move: unable to retrieve file from server: %s
        case 26: // Move: unable to delete the source file
        case 44: // upload: resume in ASCII transfer mode is not supported (try resuming in binary mode)
        case 45: // upload: unable to resume (Copy/Move) - server does not support resuming
        case 46: // upload: unable to resume the file because the target file size is unknown
        case 47: // upload: unable to resume the file because the target file is larger than the source file
        case 51: // issue "unable to verify whether the file was uploaded successfully" (we sent the entire file and the server simply did not respond; the file is most likely OK but we cannot test it - reasons: ASCII transfer mode or we do not have the size in bytes (neither listing nor the SIZE command))
        {
            CSolveServerCmdErr2 dlg(parent,
                                    openDlgWithID == 21 || openDlgWithID == 22 || openDlgWithID == 44 ||
                                            openDlgWithID == 45 || openDlgWithID == 46 || openDlgWithID == 47
                                        ? IDS_SSCD2_TITLE1
                                    : openDlgWithID == 23 ? IDS_SSCD2_TITLE2
                                    : openDlgWithID == 24 ? IDS_SSCD2_TITLE3
                                    : openDlgWithID == 25 ? IDS_SSCD2_TITLE4
                                    : openDlgWithID == 26 ? IDS_SSCD2_TITLE5
                                                          : IDS_SSCD2_TITLE7,
                                    ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                    diskNameW.c_str(), errorDescription.c_str(), &applyToAll,
                                    openDlgWithID == 22 ? siscdt2ResumeTestFailed : openDlgWithID == 26                                                                    ? siscdt2Simple
                                                                                : openDlgWithID == 44 || openDlgWithID == 45 || openDlgWithID == 46 || openDlgWithID == 47 ? siscdt2UploadUnableToStore
                                                                                : openDlgWithID == 51                                                                      ? siscdt2UploadTestIfFinished
                                                                                                                                                                           : siscdt2ResumeFile);
            dlgResult = dlg.Execute();
            break;
        }

        case 27: // ASCII transfer mode for a binary file
        case 38: // upload: ASCII transfer mode for a binary file
        {
            CSolveItemErrorDlg dlg(parent, oper, NO_ERROR, NULL, ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, NULL,
                                   openDlgWithID == 27 ? sidtASCIITrModeForBinFile : sidtUploadASCIITrModeForBinFile);
            dlgResult = dlg.Execute();
            break;
        }

        case 28: // upload: error creating the target directory
        {
            CSolveItemErrorDlg dlg(parent, oper, NO_ERROR, errorDescription.c_str(), ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW, sidtUploadCannotCreateTgtDir);
            dlgResult = dlg.Execute();
            break;
        }

        case 29: // upload: target directory already exists
        {
            CSolveItemErrorDlg dlg(parent, oper, NO_ERROR, NULL, ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW, sidtUploadTgtDirAlreadyExists);
            dlgResult = dlg.Execute();
            break;
        }

        case 32: // upload: error creating the target directory with auto-rename (under a different name)
        case 48: // upload: error creating the target file with auto-rename (under a different name)
        case 40: // upload: unable to store file to server
        {
            CSolveItemErrorDlg dlg(parent, oper, NO_ERROR, errorDescription.c_str(), ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW,
                                   openDlgWithID == 32 ? sidtUploadCrDirAutoRenFailed : openDlgWithID == 48 ? sidtUploadFileAutoRenFailed
                                                                                                            : sidtUploadStoreFileFailed);
            dlgResult = dlg.Execute();
            break;
        }

        case 34: // upload: error creating the target file
        {
            CSolveItemErrorDlg dlg(parent, oper, NO_ERROR, errorDescription.c_str(), ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW, sidtUploadCannotCreateTgtFile);
            dlgResult = dlg.Execute();
            break;
        }

        case 36: // upload: target file already exists
        {
            CSolveItemErrorDlg dlg(parent, oper, NO_ERROR, NULL, ftpPathW.c_str(), ftpNameW.c_str(), diskPathW.c_str(),
                                   diskNameW.c_str(), &applyToAll, &newNameW, sidtUploadTgtFileAlreadyExists);
            dlgResult = dlg.Execute();
            break;
        }
        }

        if (dlgResult != IDCANCEL && newNameW != NULL)
        {
            std::string encodedName;
            if (isUploadItem)
            {
                if (!TextCodec.Encode(newNameW, wcslen(newNameW), encodedName))
                {
                    // The name NAMES a file on the server, so exact-or-refuse is right - a
                    // best-fit substitution would create a DIFFERENT file than the user asked for.
                    // What was missing is any way to find that out. Turning OK into IDCANCEL on its
                    // own made the dialog close exactly as if the user had pressed Cancel: the
                    // rename discarded, the item still in its error state, and nothing said. The
                    // edit control is Unicode now, so typing an unencodable character is ordinary
                    // rather than impossible, and this is the one arm where the refusal is the
                    // user's own input being rejected.
                    SalamanderGeneral->SalMessageBox(parent,
                                                     LangStr(IDS_NEWNAME_CANNOTENCODE).c_str(),
                                                     LangStr(IDS_FTPERRORTITLE).c_str(),
                                                     MB_OK | MB_ICONEXCLAMATION);
                    dlgResult = IDCANCEL;
                }
                else
                {
                    newName = DupQueueByteText(encodedName.data(), encodedName.size());
                    if (newName == NULL)
                        dlgResult = IDCANCEL;
                }
            }
            else
            {
                localNewName = DupQueueWideText(newNameW);
                if (localNewName == NULL)
                    dlgResult = IDCANCEL;
            }
        }

        if (dlgResult != IDCANCEL) // store the values entered by the user in the item
        {
            HANDLES(EnterCriticalSection(&QueueCritSect));
            found = FindItemWithUID(UID);
            if (found != NULL)
            {
                if (found->HasErrorToSolve(NULL, NULL))
                {
                    // save the dialog result to the items
                    CFTPQueueItem* item = found;
                    int itemIndex = LastFoundIndex;
                    int enumIndex = 0;
                    DWORD wantedProblemID = found->ProblemID;
                    BOOL notAccessibleListingsRemoved = FALSE;
                    oper->SetSizeCmdIsSupported(TRUE); // so that the SIZE command is tried again if needed
                    while (1)
                    {
                        if (!notAccessibleListingsRemoved &&
                            (item->Type == fqitUploadCopyExploreDir || item->Type == fqitUploadMoveExploreDir ||
                             item->Type == fqitUploadCopyFile || item->Type == fqitUploadMoveFile))
                        {
                            notAccessibleListingsRemoved = TRUE;
                            oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                            UploadListingCache.RemoveNotAccessibleListings(userBuf, hostBuf, portBuf);
                        }

                        if (openDlgWithID == 1 || openDlgWithID >= 12 && openDlgWithID <= 27 ||
                            openDlgWithID >= 30 && openDlgWithID <= 31 || openDlgWithID == 33 ||
                            openDlgWithID == 35 || openDlgWithID >= 37 && openDlgWithID <= 39 || openDlgWithID == 41 ||
                            openDlgWithID >= 44 && openDlgWithID <= 47 || openDlgWithID >= 49 && openDlgWithID <= 51)
                        {
                            switch (dlgResult)
                            {
                            case IDOK:                   // Retry or Use Binary Mode (only for openDlgWithID==27, 38)
                            case CM_SATR_RETRY:          // Retry (only for openDlgWithID==27, 38)
                            case CM_SATR_IGNORE:         // Ignore (only for openDlgWithID==27, 38)
                            case CM_SSCD_USEALTNAME:     // use alternate name (openDlgWithID: 21, 22, 23, 24, 25, 44, 45, 46, 47)
                            case CM_SSCD_RESUME:         // resume (openDlgWithID: 21, 22, 23, 24, 25, 44, 45, 46, 47)
                            case CM_SSCD_RESUMEOROVR:    // resume or overwrite (openDlgWithID: 21, 22, 23, 24, 25, 44, 45, 46, 47)
                            case CM_SSCD_OVERWRITE:      // overwrite (openDlgWithID: 21, 22, 23, 24, 25, 44, 45, 46, 47)
                            case CM_SSCD_REDUCEFILESIZE: // reduce file size and try to resume again (openDlgWithID: 22)
                            {
                                CFTPQueueItemState newState = sqisWaiting;
                                if (item->Type == fqitDeleteDir || item->Type == fqitMoveDeleteDir ||
                                    item->Type == fqitUploadMoveDeleteDir || item->Type == fqitMoveDeleteDirLink ||
                                    item->Type == fqitChAttrsDir)
                                {
                                    newState = ((CFTPQueueItemDir*)item)->GetStateFromCounters();
                                }
                                if (newState == sqisWaiting)
                                    HandleFirstWaitingItemIndex(item->IsExploreOrResolveItem(), itemIndex);

                                if (ret == -2)
                                    ret = itemIndex;
                                else
                                    ret = -1;

                                item->ChangeStateAndCounters(newState, oper, this);
                                if (openDlgWithID == 27 &&
                                    (item->Type == fqitCopyFileOrFileLink || item->Type == fqitMoveFileOrFileLink))
                                {
                                    if (dlgResult == IDOK) // Use Binary Mode (ASCII transfer mode for a binary file)
                                        UpdateAsciiTransferMode((CFTPQueueItemCopyOrMove*)item, FALSE);
                                    else
                                    {
                                        if (dlgResult == CM_SATR_IGNORE) // Ignore (ASCII transfer mode for a binary file)
                                            UpdateIgnoreAsciiTrModeForBinFile((CFTPQueueItemCopyOrMove*)item, TRUE);
                                    }
                                }
                                if (openDlgWithID == 38 &&
                                    (item->Type == fqitUploadCopyFile || item->Type == fqitUploadMoveFile))
                                {
                                    if (dlgResult == IDOK) // Use Binary Mode (ASCII transfer mode for a binary file)
                                        UpdateAsciiTransferMode((CFTPQueueItemCopyOrMoveUpload*)item, FALSE);
                                    else
                                    {
                                        if (dlgResult == CM_SATR_IGNORE) // Ignore (ASCII transfer mode for a binary file)
                                            UpdateIgnoreAsciiTrModeForBinFile((CFTPQueueItemCopyOrMoveUpload*)item, TRUE);
                                    }
                                }
                                if (openDlgWithID == 12 &&
                                    (item->Type == fqitUploadCopyExploreDir || item->Type == fqitUploadMoveExploreDir))
                                { // invalidate the target path listing (if an outdated listing containing the target directory is used, CWD into this directory reports "path not found" - with an up-to-date listing the directory is created via MKD)
                                    CFTPQueueItemCopyMoveUploadExplore* curItem = (CFTPQueueItemCopyMoveUploadExplore*)item;
                                    UpdateUploadTgtDirState(curItem, UPLOADTGTDIRSTATE_UNKNOWN);
                                    oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                                    CFTPServerPathType pathType = oper->GetFTPServerPathType(curItem->TgtPath);
                                    UploadListingCache.InvalidatePathListing(userBuf, hostBuf, portBuf, curItem->TgtPath, pathType);
                                }
                                if (found->ProblemID == ITEMPR_UNABLETORESUME)
                                    oper->SetResumeIsNotSupported(FALSE); // so Retry makes any sense at all
                                item->ProblemID = ITEMPR_OK;
                                item->WinError = NO_ERROR;
                                if (item->ErrAllocDescr != NULL)
                                {
                                    SalamanderGeneral->Free(item->ErrAllocDescr);
                                    item->ErrAllocDescr = NULL;
                                }
                                item->ForceAction = dlgResult == CM_SSCD_USEALTNAME ? fqiaUseAutorename : dlgResult == CM_SSCD_RESUME       ? fqiaResume
                                                                                                      : dlgResult == CM_SSCD_REDUCEFILESIZE ? fqiaReduceFileSizeAndResume
                                                                                                      : dlgResult == CM_SSCD_RESUMEOROVR    ? fqiaResumeOrOverwrite
                                                                                                      : dlgResult == CM_SSCD_OVERWRITE      ? fqiaOverwrite
                                                                                                                                            : fqiaNone;
                                break;
                            }

                            case IDB_SCRD_SKIP: // skip (openDlgWithID: 1, 16, 27, 37, 38, 50)
                            case IDB_SSCD_SKIP: // skip (openDlgWithID: 12, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 30, 31, 33, 44, 45, 46, 47)
                            {
                                if (ret == -2)
                                    ret = itemIndex;
                                else
                                    ret = -1;

                                item->ChangeStateAndCounters(sqisSkipped, oper, this);
                                item->ForceAction = fqiaNone; // just to be safe (probably not needed)
                                break;
                            }

                            case CM_SSCD_MARKASDELETED: // mark as deleted (openDlgWithID: 18, 19)
                            {
                                if (ret == -2)
                                    ret = itemIndex;
                                else
                                    ret = -1;

                                item->ChangeStateAndCounters(sqisDone, oper, this);
                                item->ProblemID = ITEMPR_OK;
                                item->WinError = NO_ERROR;
                                if (item->ErrAllocDescr != NULL)
                                {
                                    SalamanderGeneral->Free(item->ErrAllocDescr);
                                    item->ErrAllocDescr = NULL;
                                }
                                item->ForceAction = fqiaNone; // just to be safe (probably not needed)
                                break;
                            }

                            case CM_SSCD_MARKASUPLOADED: // mark file as successfully uploaded (openDlgWithID: 51)
                            {
                                if (ret == -2)
                                    ret = itemIndex;
                                else
                                    ret = -1;

                                ((CFTPQueueItemCopyOrMoveUpload*)item)->TgtFileState = UPLOADTGTFILESTATE_TRANSFERRED;
                                if (item->Type == fqitUploadMoveFile) // we still need to delete the source file
                                {
                                    HandleFirstWaitingItemIndex(item->IsExploreOrResolveItem(), itemIndex);
                                    item->ChangeStateAndCounters(sqisWaiting, oper, this);
                                }
                                else
                                    item->ChangeStateAndCounters(sqisDone, oper, this);
                                item->ProblemID = ITEMPR_OK;
                                item->ForceAction = fqiaNone; // just to be safe (probably not needed)
                                break;
                            }
                            }
                        }
                        else
                        {
                            switch (item->Type)
                            {
                            case fqitChAttrsFile: // change file attributes (note: attributes cannot be changed on links) (object of class CFTPQueueItemChAttr)
                            case fqitChAttrsDir:  // change directory attributes (object of class CFTPQueueItemChAttrDir)
                            {
                                if (openDlgWithID == 8) // error: the file/directory has unknown attributes we cannot preserve (permissions other than 'r'+'w'+'x')
                                {
                                    switch (dlgResult)
                                    {
                                    case IDOK:                // ignore
                                    case CM_SIEA_RETRY:       // retry
                                    case CM_SIEA_SETNEWATTRS: // wants to set a new attribute value
                                    {
                                        CFTPQueueItemState newState = sqisWaiting;
                                        if (item->Type == fqitChAttrsDir)
                                            newState = ((CFTPQueueItemDir*)item)->GetStateFromCounters();
                                        if (newState == sqisWaiting)
                                            HandleFirstWaitingItemIndex(item->IsExploreOrResolveItem(), itemIndex);

                                        if (ret == -2)
                                            ret = itemIndex;
                                        else
                                            ret = -1;

                                        if (dlgResult == IDOK) // ignore
                                        {
                                            if (item->Type == fqitChAttrsFile)
                                                ((CFTPQueueItemChAttr*)item)->AttrErr = FALSE;
                                            else
                                                ((CFTPQueueItemChAttrDir*)item)->AttrErr = FALSE; // fqitChAttrsDir
                                        }

                                        if (dlgResult == CM_SIEA_SETNEWATTRS) // wants to set a new attribute value
                                        {
                                            if (item->Type == fqitChAttrsFile)
                                            {
                                                ((CFTPQueueItemChAttr*)item)->Attr = newAttr;
                                                ((CFTPQueueItemChAttr*)item)->AttrErr = FALSE;
                                            }
                                            else // fqitChAttrsDir
                                            {
                                                ((CFTPQueueItemChAttrDir*)item)->Attr = newAttr;
                                                ((CFTPQueueItemChAttrDir*)item)->AttrErr = FALSE;
                                            }
                                        }
                                        item->ChangeStateAndCounters(newState, oper, this);
                                        item->ProblemID = ITEMPR_OK;
                                        item->WinError = NO_ERROR;
                                        if (item->ErrAllocDescr != NULL)
                                        {
                                            SalamanderGeneral->Free(item->ErrAllocDescr);
                                            item->ErrAllocDescr = NULL;
                                        }
                                        item->ForceAction = fqiaNone;
                                        break;
                                    }

                                    case IDB_SCRD_SKIP: // skip
                                    {
                                        if (ret == -2)
                                            ret = itemIndex;
                                        else
                                            ret = -1;

                                        item->ChangeStateAndCounters(sqisSkipped, oper, this);
                                        item->ForceAction = fqiaNone; // just to be safe (probably not needed)
                                        break;
                                    }
                                    }
                                    break;
                                }
                                break;
                            }

                            case fqitDeleteExploreDir: // explore a directory for delete (note: directory links are removed as a whole, the operation objective is met and nothing "extra" is deleted) (object of class CFTPQueueItemDelExplore)
                            case fqitDeleteLink:       // delete for a link (object of class CFTPQueueItemDel)
                            case fqitDeleteFile:       // delete for a file (object of class CFTPQueueItemDel)
                            {
                                if (openDlgWithID == 9 ||  // delete directory error: the directory is hidden (more a confirmation than an error)
                                    openDlgWithID == 10 || // delete file or link error: the file is hidden (more a confirmation than an error)
                                    openDlgWithID == 11)   // delete directory error: the directory is not empty (more a confirmation than an error)
                                {
                                    switch (dlgResult)
                                    {
                                    case IDOK:          // delete
                                    case CM_SISE_RETRY: // retry
                                    {
                                        HandleFirstWaitingItemIndex(item->IsExploreOrResolveItem(), itemIndex);

                                        if (ret == -2)
                                            ret = itemIndex;
                                        else
                                            ret = -1;

                                        if (dlgResult == IDOK) // delete
                                        {
                                            switch (item->Type)
                                            {
                                            case fqitDeleteExploreDir:
                                            {
                                                if (openDlgWithID == 9) // error: hidden directory
                                                    ((CFTPQueueItemDelExplore*)item)->IsHiddenDir = FALSE;
                                                else // error: directory not empty
                                                    ((CFTPQueueItemDelExplore*)item)->IsTopLevelDir = FALSE;
                                                break;
                                            }

                                            case fqitDeleteLink:
                                            case fqitDeleteFile:
                                                ((CFTPQueueItemDel*)item)->IsHiddenFile = FALSE;
                                                break;
                                            }
                                        }

                                        item->ChangeStateAndCounters(sqisWaiting, oper, this);
                                        item->ProblemID = ITEMPR_OK;
                                        item->WinError = NO_ERROR;
                                        if (item->ErrAllocDescr != NULL) // just to be safe (probably not needed)
                                        {
                                            SalamanderGeneral->Free(item->ErrAllocDescr);
                                            item->ErrAllocDescr = NULL;
                                        }
                                        item->ForceAction = fqiaNone;
                                        break;
                                    }

                                    case IDB_SISE_SKIP: // skip
                                    {
                                        if (ret == -2)
                                            ret = itemIndex;
                                        else
                                            ret = -1;

                                        item->ChangeStateAndCounters(sqisSkipped, oper, this);
                                        item->ForceAction = fqiaNone; // just to be safe (probably not needed)
                                        break;
                                    }
                                    }
                                    break;
                                }
                                break;
                            }

                            case fqitCopyExploreDir:       // explore a directory or a link to a directory for copying (object of class CFTPQueueItemCopyMoveExplore)
                            case fqitMoveExploreDir:       // explore a directory for moving (deletes the directory after completion) (object of class CFTPQueueItemCopyMoveExplore)
                            case fqitMoveExploreDirLink:   // explore a link to a directory for moving (deletes the link to the directory after completion) (object of class CFTPQueueItemCopyMoveExplore)
                            case fqitUploadCopyExploreDir: // upload: explore a directory for copying (object of class CFTPQueueItemCopyMoveUploadExplore)
                            case fqitUploadMoveExploreDir: // upload: explore a directory for moving (deletes the directory after completion) (object of class CFTPQueueItemCopyMoveUploadExplore)
                            {
                                switch (openDlgWithID)
                                {
                                case 2:  // error creating the target directory
                                case 3:  // target directory already exists
                                case 28: // upload: error creating the target directory
                                case 29: // upload: target directory already exists
                                case 32: // upload: error creating the target directory with auto-rename (under a different name)
                                {
                                    switch (dlgResult)
                                    {
                                    case IDOK:                   // retry
                                    case CM_SCRD_USEALTNAME:     // autorename
                                    case CM_SDEX_USEEXISTINGDIR: // use existing directory
                                    {
                                        if (newName != NULL || localNewName != NULL)
                                        {
                                            if (item->Type == fqitUploadCopyExploreDir || item->Type == fqitUploadMoveExploreDir)
                                            {
                                                if (((CFTPQueueItemCopyMoveUploadExplore*)item)->TgtName != NULL)
                                                    SalamanderGeneral->Free(((CFTPQueueItemCopyMoveUploadExplore*)item)->TgtName);
                                                ((CFTPQueueItemCopyMoveUploadExplore*)item)->TgtName = newName;
                                            }
                                            else
                                            {
                                                CFTPQueueItemCopyMoveExplore* download = (CFTPQueueItemCopyMoveExplore*)item;
                                                if (localNewName != NULL)
                                                {
                                                    if (download->LocalTgtName != NULL)
                                                        SalamanderGeneral->Free(download->LocalTgtName);
                                                    download->LocalTgtName = localNewName;
                                                    localNewName = NULL;
                                                }
                                            }
                                            newName = NULL;
                                            if (newNameW != NULL)
                                            {
                                                SalamanderGeneral->Free(newNameW);
                                                newNameW = NULL;
                                            }
                                        }

                                        HandleFirstWaitingItemIndex(item->IsExploreOrResolveItem(), itemIndex);
                                        if (ret == -2)
                                            ret = itemIndex;
                                        else
                                            ret = -1;

                                        if (openDlgWithID == 28 &&
                                            (item->Type == fqitUploadCopyExploreDir || item->Type == fqitUploadMoveExploreDir))
                                        { // invalidate the target path listing (if an outdated listing lacking the target directory is used, MKD for this directory reports "already exists" - with an up-to-date listing the change goes directly into this directory via CWD)
                                            CFTPQueueItemCopyMoveUploadExplore* curItem = (CFTPQueueItemCopyMoveUploadExplore*)item;
                                            UpdateUploadTgtDirState(curItem, UPLOADTGTDIRSTATE_UNKNOWN);
                                            oper->GetUserHostPort(&userBuf, hostBuf, &portBuf);
                                            CFTPServerPathType pathType = oper->GetFTPServerPathType(curItem->TgtPath);
                                            UploadListingCache.InvalidatePathListing(userBuf, hostBuf, portBuf, curItem->TgtPath, pathType);
                                        }

                                        item->ChangeStateAndCounters(sqisWaiting, oper, this);
                                        item->ProblemID = ITEMPR_OK;
                                        item->WinError = NO_ERROR;
                                        if (item->ErrAllocDescr != NULL)
                                        {
                                            SalamanderGeneral->Free(item->ErrAllocDescr);
                                            item->ErrAllocDescr = NULL;
                                        }
                                        if (dlgResult == IDOK)
                                            item->ForceAction = fqiaNone; // just to be safe (probably not needed)
                                        else
                                        {
                                            if (dlgResult == CM_SCRD_USEALTNAME)
                                                item->ForceAction = fqiaUseAutorename; // force Autorename on the next attempt
                                            else
                                                item->ForceAction = fqiaUseExistingDir; // force Use Existing Directory on the next attempt
                                        }
                                        break;
                                    }

                                    case IDB_SCRD_SKIP: // skip
                                    {
                                        if (ret == -2)
                                            ret = itemIndex;
                                        else
                                            ret = -1;

                                        item->ChangeStateAndCounters(sqisSkipped, oper, this);
                                        item->ForceAction = fqiaNone; // just to be safe (probably not needed)
                                        break;
                                    }
                                    }
                                    break;
                                }
                                }
                                break;
                            }

                            case fqitCopyFileOrFileLink: // copying a file or a link to a file (object of class CFTPQueueItemCopyOrMove)
                            case fqitMoveFileOrFileLink: // moving a file or a link to a file (object of class CFTPQueueItemCopyOrMove)
                            case fqitUploadCopyFile:     // upload: copying a file (object of class CFTPQueueItemCopyOrMoveUpload)
                            case fqitUploadMoveFile:     // upload: moving a file (object of class CFTPQueueItemCopyOrMoveUpload)
                            {
                                switch (openDlgWithID)
                                {
                                case 4:  // error creating the target file
                                case 5:  // target file already exists
                                case 6:  // file transfer error, the file was created or overwritten or resumed with overwrite allowed
                                case 7:  // file transfer error, the file was resumed without the option to overwrite
                                case 34: // upload: error creating the target file
                                case 36: // upload: target file already exists
                                case 40: // upload: unable to store file to server
                                case 42: // upload: file transfer error, the file was created or overwritten or resumed with overwrite allowed
                                case 43: // upload: file transfer error, the file was resumed without the option to overwrite
                                case 48: // upload: unable to create the target file under any name
                                {
                                    switch (dlgResult)
                                    {
                                    case IDOK:                // retry
                                    case CM_SCRD_USEALTNAME:  // autorename
                                    case CM_SIED_RESUME:      // resume
                                    case CM_SIED_RESUMEOROVR: // resume or overwrite
                                    case CM_SIED_OVERWRITE:   // overwrite
                                        // case CM_SIED_OVERWRITEALL: // overwrite-all translates to overwrite + applyToAll==TRUE
                                        {
                                            if (newName != NULL || localNewName != NULL)
                                            {
                                                if (item->Type == fqitCopyFileOrFileLink || item->Type == fqitMoveFileOrFileLink)
                                                {
                                                }
                                                else
                                                {
                                                    if (((CFTPQueueItemCopyOrMoveUpload*)item)->TgtName != NULL)
                                                        SalamanderGeneral->Free(((CFTPQueueItemCopyOrMoveUpload*)item)->TgtName);
                                                    ((CFTPQueueItemCopyOrMoveUpload*)item)->TgtName = newName;
                                                }
                                                if (item->Type == fqitCopyFileOrFileLink || item->Type == fqitMoveFileOrFileLink)
                                                {
                                                    CFTPQueueItemCopyOrMove* download = (CFTPQueueItemCopyOrMove*)item;
                                                    if (localNewName != NULL)
                                                    {
                                                        if (download->LocalTgtName != NULL)
                                                            SalamanderGeneral->Free(download->LocalTgtName);
                                                        download->LocalTgtName = localNewName;
                                                        localNewName = NULL;
                                                    }
                                                }
                                                newName = NULL;
                                                if (newNameW != NULL)
                                                {
                                                    SalamanderGeneral->Free(newNameW);
                                                    newNameW = NULL;
                                                }
                                            }

                                            HandleFirstWaitingItemIndex(item->IsExploreOrResolveItem(), itemIndex);
                                            if (ret == -2)
                                                ret = itemIndex;
                                            else
                                                ret = -1;

                                            item->ChangeStateAndCounters(sqisWaiting, oper, this);
                                            item->ProblemID = ITEMPR_OK;
                                            item->WinError = NO_ERROR;
                                            if (item->ErrAllocDescr != NULL)
                                            {
                                                SalamanderGeneral->Free(item->ErrAllocDescr);
                                                item->ErrAllocDescr = NULL;
                                            }
                                            switch (dlgResult)
                                            {
                                            case IDOK:
                                                item->ForceAction = fqiaNone;
                                                break; // to make sure Retry is performed and not, for example, Autorename
                                            case CM_SCRD_USEALTNAME:
                                                item->ForceAction = fqiaUseAutorename;
                                                break; // force Autorename on the next attempt
                                            case CM_SIED_RESUME:
                                                item->ForceAction = fqiaResume;
                                                break; // force Resume on the next attempt
                                            case CM_SIED_RESUMEOROVR:
                                                item->ForceAction = fqiaResumeOrOverwrite;
                                                break; // force Resume or Overwrite on the next attempt
                                            case CM_SIED_OVERWRITE:
                                                item->ForceAction = fqiaOverwrite;
                                                break; // force Overwrite on the next attempt
                                                       // case CM_SIED_OVERWRITEALL: // overwrite-all translates to overwrite + applyToAll==TRUE
                                            }
                                            break;
                                        }

                                    case IDB_SCRD_SKIP: // skip
                                    {
                                        if (ret == -2)
                                            ret = itemIndex;
                                        else
                                            ret = -1;

                                        item->ChangeStateAndCounters(sqisSkipped, oper, this);
                                        item->ForceAction = fqiaNone; // just to be safe (probably not needed)
                                        break;
                                    }
                                    }
                                    break;
                                }
                                }
                                break;
                            }
                            }
                        }
                        if (!applyToAll)
                            break;
                        else
                        {
                            if (newName != NULL)
                            {
                                SalamanderGeneral->Free(newName);
                                newName = NULL;
                            }
                            if (newNameW != NULL)
                            {
                                SalamanderGeneral->Free(newNameW);
                                newNameW = NULL;
                            }
                            if (localNewName != NULL)
                            {
                                SalamanderGeneral->Free(localNewName);
                                localNewName = NULL;
                            }
                            // try to find another item with the same error
                            BOOL nextItemFound = FALSE;
                            for (; enumIndex < Items.Count; enumIndex++)
                            {
                                item = Items[enumIndex];
                                if (item->HasErrorToSolve(NULL, NULL) &&
                                    item->ProblemID == wantedProblemID &&
                                    enumIndex != LastFoundIndex)
                                {
                                    itemIndex = enumIndex++;
                                    nextItemFound = TRUE;
                                    break;
                                }
                            }
                            if (!nextItemFound)
                                break; // no further item was found
                        }
                    }
                }
            }
            HANDLES(LeaveCriticalSection(&QueueCritSect));
        }
    }
    else
    {
        if (openDlgWithID == 0)
            TRACE_E("CFTPQueue::SolveErrorOnItem(): unknown type of error!");
    }
    if (newName != NULL)
        SalamanderGeneral->Free(newName);
    if (newNameW != NULL)
        SalamanderGeneral->Free(newNameW);
    if (localNewName != NULL)
        SalamanderGeneral->Free(localNewName);
    return ret;
}

void CFTPQueue::UpdateItemState(CFTPQueueItem* item, CFTPQueueItemState state, DWORD problemID,
                                DWORD winError, char* errAllocDescr, CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateItemState()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    if (item != NULL)
    {
        if (state == sqisWaiting && FindItemWithUID(item->UID) != NULL) // item found ("always true")
            HandleFirstWaitingItemIndex(item->IsExploreOrResolveItem(), LastFoundIndex);

        item->ChangeStateAndCounters(state, oper, this); // item state change
        item->ProblemID = problemID;
        item->WinError = winError;
        if (item->ErrAllocDescr != NULL)
            SalamanderGeneral->Free(item->ErrAllocDescr); // free any previous value
        item->ErrAllocDescr = errAllocDescr;
    }
    else
    {
        if (errAllocDescr != NULL)
            free(errAllocDescr); // there is nowhere to store the value, so at least free it
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateRenamedName(CFTPQueueItemCopyOrMoveUpload* item, char* renamedName)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateRenamedName()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    if (item->RenamedName != NULL)
        free(item->RenamedName);
    item->RenamedName = renamedName;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateAutorenamePhase(CFTPQueueItemCopyOrMoveUpload* item, int autorenamePhase)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateAutorenamePhase()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->AutorenamePhase = autorenamePhase;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::ChangeTgtNameToRenamedName(CFTPQueueItemCopyOrMoveUpload* item)
{
    CALL_STACK_MESSAGE1("CFTPQueue::ChangeTgtNameToRenamedName()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    if (item->RenamedName != NULL)
    {
        SalamanderGeneral->Free(item->TgtName);
        item->TgtName = item->RenamedName;
        item->RenamedName = NULL;
    }
    else
        TRACE_E("Unexpected situation in CFTPQueue::ChangeTgtNameToRenamedName(): item->RenamedName == NULL");
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

BOOL CFTPQueue::UpdateLocalTgtName(CFTPQueueItemCopyMoveExplore* item, const wchar_t* tgtName)
{
    wchar_t* copy = DupQueueWideText(tgtName);
    if (copy == NULL)
        return FALSE;
    HANDLES(EnterCriticalSection(&QueueCritSect));
    if (item->LocalTgtName != NULL)
        SalamanderGeneral->Free(item->LocalTgtName);
    item->LocalTgtName = copy;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return TRUE;
}

void CFTPQueue::UpdateTgtName(CFTPQueueItemCopyMoveUploadExplore* item, char* tgtName)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateTgtName2()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    if (item->TgtName != NULL)
        SalamanderGeneral->Free(item->TgtName);
    item->TgtName = tgtName;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

BOOL CFTPQueue::UpdateLocalTgtName(CFTPQueueItemCopyOrMove* item, const wchar_t* tgtName)
{
    wchar_t* copy = DupQueueWideText(tgtName);
    if (copy == NULL)
        return FALSE;
    HANDLES(EnterCriticalSection(&QueueCritSect));
    if (item->LocalTgtName != NULL)
        SalamanderGeneral->Free(item->LocalTgtName);
    item->LocalTgtName = copy;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return TRUE;
}

void CFTPQueue::UpdateTgtDirState(CFTPQueueItemCopyMoveExplore* item, unsigned tgtDirState)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateTgtDirState()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->TgtDirState = tgtDirState;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateUploadTgtDirState(CFTPQueueItemCopyMoveUploadExplore* item, unsigned tgtDirState)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateUploadTgtDirState()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->TgtDirState = tgtDirState;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateForceAction(CFTPQueueItem* item, CFTPQueueItemAction forceAction)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateForceAction()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->ForceAction = forceAction;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateTgtFileState(CFTPQueueItemCopyOrMove* item, unsigned tgtFileState)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateTgtFileState()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->TgtFileState = tgtFileState;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateFileSize(CFTPQueueItemCopyOrMove* item, CQuadWord const& size,
                               BOOL sizeInBytes, CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateFileSize()");

    if (size == CQuadWord(-1, -1))
        TRACE_E("CFTPQueue::UpdateFileSize(): unexpected value of 'size' (-1)!");
    HANDLES(EnterCriticalSection(&QueueCritSect));
    if (item->Size != CQuadWord(-1, -1))
    {
        oper->SubFromTotalSize(item->Size, item->SizeInBytes);
        if (item->GetItemState() == sqisDone || item->GetItemState() == sqisSkipped)
        {
            if (item->SizeInBytes)
                DoneOrSkippedByteSize -= item->Size;
            else
                DoneOrSkippedBlockSize -= item->Size;
        }
        else
        {
            if (item->GetItemState() == sqisWaiting || item->GetItemState() == sqisProcessing ||
                item->GetItemState() == sqisDelayed)
            {
                if (item->SizeInBytes)
                    WaitingOrProcessingOrDelayedByteSize -= item->Size;
                else
                {
                    WaitingOrProcessingOrDelayedBlockSize -= item->Size;
                    CopyUnknownSizeCountIfUnknownBlockSize--;
                }
            }
            else
            {
                if (!item->SizeInBytes)
                    CopyUnknownSizeCountIfUnknownBlockSize--;
            }
        }
    }
    else
    {
        if (item->GetItemState() != sqisDone && item->GetItemState() != sqisSkipped)
            CopyUnknownSizeCount--;
    }
    item->Size = size;
    item->SizeInBytes = sizeInBytes;
    if (item->Size != CQuadWord(-1, -1))
    {
        oper->AddToTotalSize(item->Size, item->SizeInBytes);
        if (item->GetItemState() == sqisDone || item->GetItemState() == sqisSkipped)
        {
            if (item->SizeInBytes)
                DoneOrSkippedByteSize += item->Size;
            else
                DoneOrSkippedBlockSize += item->Size;
        }
        else
        {
            if (item->GetItemState() == sqisWaiting || item->GetItemState() == sqisProcessing ||
                item->GetItemState() == sqisDelayed)
            {
                if (item->SizeInBytes)
                    WaitingOrProcessingOrDelayedByteSize += item->Size;
                else
                {
                    WaitingOrProcessingOrDelayedBlockSize += item->Size;
                    CopyUnknownSizeCountIfUnknownBlockSize++;
                }
            }
            else
            {
                if (!item->SizeInBytes)
                    CopyUnknownSizeCountIfUnknownBlockSize++;
            }
        }
    }
    else
    {
        if (item->GetItemState() != sqisDone && item->GetItemState() != sqisSkipped)
            CopyUnknownSizeCount++;
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateTextFileSizes(CFTPQueueItemCopyOrMoveUpload* item, CQuadWord const& sizeWithCRLF_EOLs,
                                    CQuadWord const& numberOfEOLs)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateTextFileSizes()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->SizeWithCRLF_EOLs = sizeWithCRLF_EOLs;
    item->NumberOfEOLs = numberOfEOLs;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateAsciiTransferMode(CFTPQueueItemCopyOrMove* item, BOOL asciiTransferMode)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateAsciiTransferMode()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->AsciiTransferMode = asciiTransferMode;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateIgnoreAsciiTrModeForBinFile(CFTPQueueItemCopyOrMove* item,
                                                  BOOL ignoreAsciiTrModeForBinFile)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateIgnoreAsciiTrModeForBinFile()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->IgnoreAsciiTrModeForBinFile = ignoreAsciiTrModeForBinFile;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateAsciiTransferMode(CFTPQueueItemCopyOrMoveUpload* item, BOOL asciiTransferMode)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateAsciiTransferMode2()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->AsciiTransferMode = asciiTransferMode;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateIgnoreAsciiTrModeForBinFile(CFTPQueueItemCopyOrMoveUpload* item,
                                                  BOOL ignoreAsciiTrModeForBinFile)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateIgnoreAsciiTrModeForBinFile2()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->IgnoreAsciiTrModeForBinFile = ignoreAsciiTrModeForBinFile;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateTgtFileState(CFTPQueueItemCopyOrMoveUpload* item, unsigned tgtFileState)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateTgtFileState2()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->TgtFileState = tgtFileState;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateFileSize(CFTPQueueItemCopyOrMoveUpload* item, CQuadWord const& size,
                               CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateFileSize2()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    oper->SubFromTotalSize(item->Size, TRUE);
    if (item->GetItemState() == sqisDone || item->GetItemState() == sqisSkipped)
        DoneOrSkippedUploadSize -= item->Size;
    else
    {
        if (item->GetItemState() == sqisWaiting || item->GetItemState() == sqisProcessing ||
            item->GetItemState() == sqisDelayed)
        {
            WaitingOrProcessingOrDelayedUploadSize -= item->Size;
        }
    }
    item->Size = size;
    oper->AddToTotalSize(item->Size, TRUE);
    if (item->GetItemState() == sqisDone || item->GetItemState() == sqisSkipped)
        DoneOrSkippedUploadSize += item->Size;
    else
    {
        if (item->GetItemState() == sqisWaiting || item->GetItemState() == sqisProcessing ||
            item->GetItemState() == sqisDelayed)
        {
            WaitingOrProcessingOrDelayedUploadSize += item->Size;
        }
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateAttrErr(CFTPQueueItemChAttrDir* item, BYTE attrErr)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateAttrErr()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->AttrErr = attrErr;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateAttrErr(CFTPQueueItemChAttr* item, BYTE attrErr)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateAttrErr()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->AttrErr = attrErr;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateIsHiddenDir(CFTPQueueItemDelExplore* item, BOOL isHiddenDir)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateIsHiddenDir()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->IsHiddenDir = isHiddenDir;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::UpdateIsHiddenFile(CFTPQueueItemDel* item, BOOL isHiddenFile)
{
    CALL_STACK_MESSAGE1("CFTPQueue::UpdateIsHiddenFile()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    item->IsHiddenFile = isHiddenFile;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

int CFTPQueue::GetSimpleProgress(int* doneOrSkippedCount, int* totalCount, int* unknownSizeCount,
                                 int* waitingCount)
{
    CALL_STACK_MESSAGE1("CFTPQueue::GetSimpleProgress()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    int progress = 0;
    if (Items.Count > 0)
        progress = (1000 * DoneOrSkippedItemsCount) / Items.Count;
    if (doneOrSkippedCount != NULL)
        *doneOrSkippedCount = DoneOrSkippedItemsCount;
    if (totalCount != NULL)
        *totalCount = Items.Count;
    if (unknownSizeCount != NULL)
        *unknownSizeCount = ExploreAndResolveItemsCount;
    if (waitingCount != NULL)
        *waitingCount = WaitingOrProcessingOrDelayedItemsCount;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return progress;
}

void CFTPQueue::GetCopyProgressInfo(CQuadWord* downloaded, int* unknownSizeCount,
                                    CQuadWord* totalWithoutErrors, int* errorsCount,
                                    int* doneOrSkippedCount, int* totalCount,
                                    CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::GetCopyProgressInfo()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    *downloaded = DoneOrSkippedByteSize;
    *unknownSizeCount = CopyUnknownSizeCount;
    CQuadWord size;
    if (oper->GetApproxByteSize(&size, DoneOrSkippedBlockSize))
        *downloaded += size;
    else
        *unknownSizeCount += CopyUnknownSizeCountIfUnknownBlockSize; // block size is unknown, so add those with size in blocks to the unknown ones
    *totalWithoutErrors = WaitingOrProcessingOrDelayedByteSize;
    if (oper->GetApproxByteSize(&size, WaitingOrProcessingOrDelayedBlockSize))
        *totalWithoutErrors += size;
    *totalWithoutErrors += *downloaded;
    *errorsCount = Items.Count - DoneOrSkippedItemsCount - WaitingOrProcessingOrDelayedItemsCount;
    *doneOrSkippedCount = DoneOrSkippedItemsCount;
    *totalCount = Items.Count;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void CFTPQueue::GetCopyUploadProgressInfo(CQuadWord* uploaded, int* unknownSizeCount,
                                          CQuadWord* totalWithoutErrors, int* errorsCount,
                                          int* doneOrSkippedCount, int* totalCount,
                                          CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::GetCopyUploadProgressInfo()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    *uploaded = DoneOrSkippedUploadSize;
    *unknownSizeCount = CopyUnknownSizeCount;
    *totalWithoutErrors = WaitingOrProcessingOrDelayedUploadSize;
    *totalWithoutErrors += *uploaded;
    *errorsCount = Items.Count - DoneOrSkippedItemsCount - WaitingOrProcessingOrDelayedItemsCount;
    *doneOrSkippedCount = DoneOrSkippedItemsCount;
    *totalCount = Items.Count;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

BOOL CFTPQueue::AllItemsDone()
{
    CALL_STACK_MESSAGE1("CFTPQueue::AllItemsDone()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        if (Items[i]->GetItemState() != sqisDone)
            break;
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return i == Items.Count;
}

BOOL CFTPQueue::SearchItemWithNewError(int* itemUID, int* itemIndex)
{
    CALL_STACK_MESSAGE1("CFTPQueue::SearchItemWithNewError()");
    HANDLES(EnterCriticalSection(&QueueCritSect));
    BOOL res = FALSE;
    if (LastFoundErrorOccurenceTime + 1 < LastErrorOccurenceTime + 1) // +1 is here because -1 is used as the initial value
    {                                                                 // it makes sense to search
        int foundUID = -1;
        int foundIndex = -1;
        DWORD foundErrorOccurenceTime = -1;
        int i;
        for (i = 0; i < Items.Count; i++)
        {
            CFTPQueueItem* item = Items[i];
            DWORD itemErrorOccurenceTime = -1;
            if (item->IsItemInSimpleErrorState() && item->HasErrorToSolve(NULL, NULL))
                itemErrorOccurenceTime = item->ErrorOccurenceTime;
            if (itemErrorOccurenceTime != -1 &&                                       // the item contains an error
                itemErrorOccurenceTime >= LastFoundErrorOccurenceTime + 1 &&          // it is a "new" error
                (foundUID == -1 || foundErrorOccurenceTime > itemErrorOccurenceTime)) // so far the first found or the "oldest" (we resolve errors in the order they occurred)
            {
                foundErrorOccurenceTime = itemErrorOccurenceTime;
                foundUID = item->UID;
                foundIndex = i;
            }
        }
        if (foundUID == -1)
            LastFoundErrorOccurenceTime = LastErrorOccurenceTime; // not found -> adjust LastFoundErrorOccurenceTime so that only "newer" errors are searched for next time
        else
        {
            *itemUID = foundUID;
            *itemIndex = foundIndex;
            LastFoundErrorOccurenceTime = foundErrorOccurenceTime;
            res = TRUE;
        }
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return res;
}

#ifdef _DEBUG
void CFTPQueue::DebugCheckCounters(CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::DebugCheckCounters()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    int dirItems = 0;
    int exploreAndResolveItemsCount = 0;
    int doneOrSkippedItemsCount = 0;
    int waitingOrProcessingOrDelayedItemsCount = 0;
    CQuadWord doneOrSkippedByteSize(0, 0);
    CQuadWord doneOrSkippedBlockSize(0, 0);
    CQuadWord waitingOrProcessingOrDelayedByteSize(0, 0);
    CQuadWord waitingOrProcessingOrDelayedBlockSize(0, 0);
    CQuadWord doneOrSkippedUploadSize(0, 0);
    CQuadWord waitingOrProcessingOrDelayedUploadSize(0, 0);
    int unknownSizeCount = 0;
    BOOL blockSizeUnknown = !oper->IsBlkSizeKnown();
    int i;
    for (i = 0; i < Items.Count + 1; i++)
    {
        CFTPQueueItem* item = i < Items.Count ? Items[i] : NULL;

        if (item != NULL)
        {
            if (item->IsExploreOrResolveItem())
                exploreAndResolveItemsCount++;

            if (item->Type != fqitCopyFileOrFileLink && item->Type != fqitMoveFileOrFileLink &&
                item->Type != fqitUploadCopyFile && item->Type != fqitUploadMoveFile &&
                item->GetItemState() != sqisDone && item->GetItemState() != sqisSkipped)
            {
                unknownSizeCount++;
            }

            switch (item->GetItemState())
            {
            case sqisDone:
            case sqisSkipped:
            {
                doneOrSkippedItemsCount++;
                if (item->Type == fqitCopyFileOrFileLink || item->Type == fqitMoveFileOrFileLink)
                {
                    CFTPQueueItemCopyOrMove* copyItem = (CFTPQueueItemCopyOrMove*)item;
                    if (copyItem->Size != CQuadWord(-1, -1))
                    {
                        if (copyItem->SizeInBytes)
                            doneOrSkippedByteSize += copyItem->Size;
                        else
                            doneOrSkippedBlockSize += copyItem->Size;
                    }
                }
                else
                {
                    if (item->Type == fqitUploadCopyFile || item->Type == fqitUploadMoveFile)
                        doneOrSkippedUploadSize += ((CFTPQueueItemCopyOrMoveUpload*)item)->Size;
                }
                break;
            }

            case sqisWaiting:
            case sqisProcessing:
            case sqisDelayed:
            {
                waitingOrProcessingOrDelayedItemsCount++;
                if (item->Type == fqitCopyFileOrFileLink || item->Type == fqitMoveFileOrFileLink)
                {
                    CFTPQueueItemCopyOrMove* copyItem = (CFTPQueueItemCopyOrMove*)item;
                    if (copyItem->Size != CQuadWord(-1, -1))
                    {
                        if (copyItem->SizeInBytes)
                            waitingOrProcessingOrDelayedByteSize += copyItem->Size;
                        else
                        {
                            waitingOrProcessingOrDelayedBlockSize += copyItem->Size;
                            if (blockSizeUnknown)
                                unknownSizeCount++;
                        }
                    }
                    else
                        unknownSizeCount++;
                }
                else
                {
                    if (item->Type == fqitUploadCopyFile || item->Type == fqitUploadMoveFile)
                        waitingOrProcessingOrDelayedUploadSize += ((CFTPQueueItemCopyOrMoveUpload*)item)->Size;
                }
                break;
            }

            default:
            {
                if (item->Type == fqitCopyFileOrFileLink || item->Type == fqitMoveFileOrFileLink)
                {
                    CFTPQueueItemCopyOrMove* copyItem = (CFTPQueueItemCopyOrMove*)item;
                    if (copyItem->Size == CQuadWord(-1, -1))
                        unknownSizeCount++;
                    else
                    {
                        if (!copyItem->SizeInBytes && blockSizeUnknown)
                            unknownSizeCount++;
                    }
                }
                break;
            }
            }
        }

        if (item == NULL ||
            item->Type == fqitDeleteDir || item->Type == fqitMoveDeleteDir ||
            item->Type == fqitUploadMoveDeleteDir || item->Type == fqitMoveDeleteDirLink ||
            item->Type == fqitChAttrsDir)
        { // we examine only operations and directory items
            if (item != NULL)
                dirItems++;
            CFTPQueueItemDir* itemDir = item != NULL ? (CFTPQueueItemDir*)item : NULL;
            int parentUID = itemDir != NULL ? itemDir->UID : -1;

            int childItemsNotDone = 0;
            int childItemsSkipped = 0;
            int childItemsFailed = 0;
            int childItemsUINeeded = 0;
            int x;
            for (x = 0; x < Items.Count; x++)
            {
                CFTPQueueItem* actItem = Items[x];
                if (actItem->ParentUID == parentUID)
                {
                    switch (actItem->GetItemState())
                    {
                    case sqisDone:
                        break;
                    case sqisSkipped:
                        childItemsSkipped++;
                        break;
                    case sqisUserInputNeeded:
                        childItemsUINeeded++;
                        break;

                    case sqisFailed:
                    case sqisForcedToFail:
                        childItemsFailed++;
                        break;

                    default:
                        childItemsNotDone++;
                        break;
                    }
                }
            }
            childItemsNotDone += childItemsSkipped + childItemsFailed + childItemsUINeeded;

            if (itemDir != NULL)
            {
                if (itemDir->ChildItemsNotDone != childItemsNotDone ||
                    itemDir->ChildItemsSkipped != childItemsSkipped ||
                    itemDir->ChildItemsFailed != childItemsFailed ||
                    itemDir->ChildItemsUINeeded != childItemsUINeeded)
                {
                    TRACE_E("CFTPQueue::DebugCheckCounters(): problem found in item (UID=" << itemDir->UID << "): " << itemDir->ChildItemsNotDone << " : " << childItemsNotDone << ", " << itemDir->ChildItemsSkipped << " : " << childItemsSkipped << ", " << itemDir->ChildItemsFailed << " : " << childItemsFailed << ", " << itemDir->ChildItemsUINeeded << " : " << childItemsUINeeded);
                }
            }
            else
            {
                int operChildItemsNotDone;
                int operChildItemsSkipped;
                int operChildItemsFailed;
                int operChildItemsUINeeded;
                oper->DebugGetCounters(&operChildItemsNotDone, &operChildItemsSkipped,
                                       &operChildItemsFailed, &operChildItemsUINeeded);
                if (operChildItemsNotDone != childItemsNotDone ||
                    operChildItemsSkipped != childItemsSkipped ||
                    operChildItemsFailed != childItemsFailed ||
                    operChildItemsUINeeded != childItemsUINeeded)
                {
                    TRACE_E("CFTPQueue::DebugCheckCounters(): problem found in operation: " << operChildItemsNotDone << " : " << childItemsNotDone << ", " << operChildItemsSkipped << " : " << childItemsSkipped << ", " << operChildItemsFailed << " : " << childItemsFailed << ", " << operChildItemsUINeeded << " : " << childItemsUINeeded);
                }
            }
        }
    }
    if (exploreAndResolveItemsCount != ExploreAndResolveItemsCount)
        TRACE_E("ExploreAndResolveItemsCount has incorrect value: " << exploreAndResolveItemsCount << " != " << ExploreAndResolveItemsCount);
    if (doneOrSkippedItemsCount != DoneOrSkippedItemsCount)
        TRACE_E("DoneOrSkippedItemsCount has incorrect value: " << doneOrSkippedItemsCount << " != " << DoneOrSkippedItemsCount);
    if (waitingOrProcessingOrDelayedItemsCount != WaitingOrProcessingOrDelayedItemsCount)
        TRACE_E("WaitingOrProcessingOrDelayedItemsCount has incorrect value: " << waitingOrProcessingOrDelayedItemsCount << " != " << WaitingOrProcessingOrDelayedItemsCount);
    if (doneOrSkippedByteSize != DoneOrSkippedByteSize)
        TRACE_E("DoneOrSkippedByteSize has incorrect value: " << doneOrSkippedByteSize.GetDouble() << " != " << DoneOrSkippedByteSize.GetDouble());
    if (doneOrSkippedBlockSize != DoneOrSkippedBlockSize)
        TRACE_E("DoneOrSkippedBlockSize has incorrect value: " << doneOrSkippedBlockSize.GetDouble() << " != " << DoneOrSkippedBlockSize.GetDouble());
    if (waitingOrProcessingOrDelayedByteSize != WaitingOrProcessingOrDelayedByteSize)
        TRACE_E("WaitingOrProcessingOrDelayedByteSize has incorrect value: " << waitingOrProcessingOrDelayedByteSize.GetDouble() << " != " << WaitingOrProcessingOrDelayedByteSize.GetDouble());
    if (waitingOrProcessingOrDelayedBlockSize != WaitingOrProcessingOrDelayedBlockSize)
        TRACE_E("WaitingOrProcessingOrDelayedBlockSize has incorrect value: " << waitingOrProcessingOrDelayedBlockSize.GetDouble() << " != " << WaitingOrProcessingOrDelayedBlockSize.GetDouble());
    if (doneOrSkippedUploadSize != DoneOrSkippedUploadSize)
        TRACE_E("DoneOrSkippedUploadSize has incorrect value: " << doneOrSkippedUploadSize.GetDouble() << " != " << DoneOrSkippedUploadSize.GetDouble());
    if (waitingOrProcessingOrDelayedUploadSize != WaitingOrProcessingOrDelayedUploadSize)
        TRACE_E("WaitingOrProcessingOrDelayedUploadSize has incorrect value: " << waitingOrProcessingOrDelayedUploadSize.GetDouble() << " != " << WaitingOrProcessingOrDelayedUploadSize.GetDouble());
    if (unknownSizeCount != CopyUnknownSizeCount + (blockSizeUnknown ? CopyUnknownSizeCountIfUnknownBlockSize : 0))
    {
        TRACE_E("CopyUnknownSizeCount* has incorrect value: " << unknownSizeCount << " != " << CopyUnknownSizeCount + (blockSizeUnknown ? CopyUnknownSizeCountIfUnknownBlockSize : 0) << " (" << CopyUnknownSizeCount << ", " << CopyUnknownSizeCountIfUnknownBlockSize << ")");
    }

    //  TRACE_I("CFTPQueue::DebugCheckCounters(): items=" << Items.Count << ", dir-items=" << dirItems);
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}
#endif

CFTPQueueItem*
CFTPQueue::GetNextWaitingItem(CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::GetNextWaitingItem()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    BOOL getOnlyExploreAndResolveItems = GetOnlyExploreAndResolveItems;
    CFTPQueueItem* ret = NULL;
    while (1)
    {
        int i;
        for (i = FirstWaitingItemIndex; i < Items.Count; i++)
        {
            CFTPQueueItem* item = Items[i];
            if (item->GetItemState() == sqisWaiting &&
                (!getOnlyExploreAndResolveItems || item->IsExploreOrResolveItem())) // item found
            {
                FirstWaitingItemIndex = i + 1;
                item->ChangeStateAndCounters(sqisProcessing, oper, this); // item state change
                ret = item;
                break;
            }
        }
        if (ret != NULL || !getOnlyExploreAndResolveItems)
            break;
        else
        {
            // there are no more "explore" or "resolve" items, so in the second round we take all items
            getOnlyExploreAndResolveItems = GetOnlyExploreAndResolveItems = FALSE;
            FirstWaitingItemIndex = 0;
        }
    }
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return ret;
}

void CFTPQueue::ReturnToWaitingItems(CFTPQueueItem* item, CFTPOperation* oper)
{
    CALL_STACK_MESSAGE1("CFTPQueue::ReturnToWaitingItems()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    if (item != NULL)
    {
        CFTPQueueItemState newState = sqisWaiting;
        if (item->Type == fqitDeleteDir || item->Type == fqitMoveDeleteDir ||
            item->Type == fqitUploadMoveDeleteDir || item->Type == fqitMoveDeleteDirLink ||
            item->Type == fqitChAttrsDir)
        {
            newState = ((CFTPQueueItemDir*)item)->GetStateFromCounters();
        }
        item->ChangeStateAndCounters(newState, oper, this); // item state change
        if (newState == sqisWaiting)
        {
            BOOL exploreOrResolve = item->IsExploreOrResolveItem();
            if (exploreOrResolve || !GetOnlyExploreAndResolveItems)
            {
                FirstWaitingItemIndex = 0; // we will not determine the index of the returned item (computationally as difficult as finding the first "waiting" item - which is done only once, unlike calling this function)
                if (exploreOrResolve)
                    GetOnlyExploreAndResolveItems = TRUE;
            }
        }
    }
    else
        TRACE_E("Invalid call to CFTPQueue::ReturnToWaitingItems(): 'item' may not be NULL!");
    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

//
// ****************************************************************************
// CFTPOperation
//

CFTPOperation::CFTPOperation() : IdentityCodec(FtpLocalTextCodec())
{
    HANDLES(InitializeCriticalSection(&OperCritSect));
    UID = -1;
    HANDLES(EnterCriticalSection(&NextOrdinalNumberCS));
    OrdinalNumber = NextOrdinalNumber++;
    HANDLES(LeaveCriticalSection(&NextOrdinalNumberCS));
    Queue = NULL;
    OperationDlg = NULL;
    OperationDlgThread = NULL;

    ProxyServer = NULL;
    ProxyScriptText.clear();
    ProxyScriptStartExecPoint = NULL;
    ConnectToHost.clear();
    ConnectToPort = -1;
    HostIP = INADDR_NONE;
    Host.clear();
    Port = -1;
    User.clear();
    Password.clear();
    Account.clear();
    RetryLoginWithoutAsking = FALSE;
    InitFTPCommands.clear();
    UsePassiveMode = FALSE;
    SizeCmdIsSupported = TRUE; // we will try it on the server at least once
    ListCommand.clear();
    ServerIP = INADDR_NONE;
    ServerSystem.clear();
    ServerFirstReply.clear();
    UseListingsCache = FALSE;
    ListingServerType.clear();

    ReportChangeInWorkerID = -2;
    ReportProgressChange = FALSE;
    ReportChangeInItemUID = -3;
    ReportChangeInItemUID2 = -1; // just for form's sake (pointless)
    OperStateChangedPosted = FALSE;
    LastReportedOperState = opstNone;

    Type = fotNone;
    OperationSubject.clear();

    ChildItemsNotDone = 0;
    ChildItemsSkipped = 0;
    ChildItemsFailed = 0;
    ChildItemsUINeeded = 0;

    RemoteSourcePath = NULL;
    SourcePath = NULL;
    SrcPathSeparator = '/';
    SrcPathCanChange = FALSE;
    SrcPathCanChangeInclSubdirs = FALSE;

    LastErrorOccurenceTime = -1;

    AttrAnd = -1;
    AttrOr = 0;

    RemoteTargetPath = NULL;
    TargetPath = NULL;
    TgtPathSeparator = '\\';
    TgtPathCanChange = FALSE;
    TgtPathCanChangeInclSubdirs = FALSE;
    ASCIIFileMasks = NULL;

    TotalSizeInBytes.SetUI64(0);
    TotalSizeInBlocks.SetUI64(0);

    BlkSizeTotalInBytes.SetUI64(0);
    BlkSizeTotalInBlocks.SetUI64(0);
    BlkSizeActualValue = -1;

    ResumeIsNotSupported = FALSE;
    DataConWasOpenedForAppendCmd = FALSE;

    AutodetectTrMode = 0;
    UseAsciiTransferMode = 0;
    CannotCreateFile = 0;
    CannotCreateDir = 0;
    FileAlreadyExists = 0;
    DirAlreadyExists = 0;
    RetryOnCreatedFile = 0;
    RetryOnResumedFile = 0;
    AsciiTrModeButBinFile = 0;
    UploadCannotCreateFile = 0;
    UploadCannotCreateDir = 0;
    UploadFileAlreadyExists = 0;
    UploadDirAlreadyExists = 0;
    UploadRetryOnCreatedFile = 0;
    UploadRetryOnResumedFile = 0;
    UploadAsciiTrModeButBinFile = 0;
    ConfirmDelOnNonEmptyDir = 0;
    ConfirmDelOnHiddenFile = 0;
    ConfirmDelOnHiddenDir = 0;
    ChAttrOfFiles = 0;
    ChAttrOfDirs = 0;
    UnknownAttrs = 0;
    EncryptDataConnection = EncryptControlConnection = FALSE;
    pCertificate = NULL;
    CompressData = 0;

    GlobalTransferSpeedMeter.JustConnected();   // measure the global transfer speed from the start of the operation
    GlobalLastActivityTime.Set(GetTickCount()); // the first activity is the start of the operation

    OperationEnd = OperationStart = GetTickCount();
    if (OperationEnd == -1)
        OperationEnd++;
}

CFTPOperation::~CFTPOperation()
{
    if (OperationDlg != NULL)
        TRACE_E("Unexpected situation in CFTPOperation::~CFTPOperation(): operation is destructed, but its dialog still exists!");
    if (Queue != NULL)
        delete Queue;
    if (ProxyServer != NULL)
        delete ProxyServer;
    FTPSecureWipe(Password);
    FTPSecureWipe(Account);
    // std::string members (InitFTPCommands, ListCommand, ServerSystem, ServerFirstReply,
    // ListingServerType)
    // are automatically freed
    if (SourcePath != NULL)
        SalamanderGeneral->Free(SourcePath);
    if (RemoteSourcePath != NULL)
        SalamanderGeneral->Free(RemoteSourcePath);
    if (TargetPath != NULL)
        SalamanderGeneral->Free(TargetPath);
    if (RemoteTargetPath != NULL)
        SalamanderGeneral->Free(RemoteTargetPath);
    if (ASCIIFileMasks != NULL)
        SalamanderGeneral->FreeSalamanderMaskGroup(ASCIIFileMasks);
    if (pCertificate != NULL)
        pCertificate->Release();

    HANDLES(DeleteCriticalSection(&OperCritSect));
}

int CFTPOperation::GetUID()
{
    CALL_STACK_MESSAGE1("CFTPOperation::GetUID()");

    HANDLES(EnterCriticalSection(&OperCritSect));
    int uid = UID;
    HANDLES(LeaveCriticalSection(&OperCritSect));
    return uid;
}

void CFTPOperation::SetUID(int uid)
{
    CALL_STACK_MESSAGE2("CFTPOperation::SetUID(%d)", uid);

    HANDLES(EnterCriticalSection(&OperCritSect));
    UID = uid;
    HANDLES(LeaveCriticalSection(&OperCritSect));
}
