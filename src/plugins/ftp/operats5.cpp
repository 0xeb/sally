// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

//
// ****************************************************************************
// CFTPWorkersList
//

CFTPWorkersList::CFTPWorkersList() : Workers(5, 10)
{
    HANDLES(InitializeCriticalSection(&WorkersListCritSect));
    NextWorkerID = 1;
    LastFoundErrorOccurenceTime = -1;
}

CFTPWorkersList::~CFTPWorkersList()
{
    if (Workers.Count > 0)
        TRACE_E("Unexpected situation in CFTPWorkersList::~CFTPWorkersList(): operation is destructed, but its workers still exist! count=" << Workers.Count);
    HANDLES(DeleteCriticalSection(&WorkersListCritSect));
}

BOOL CFTPWorkersList::AddWorker(CFTPWorker* newWorker)
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::AddWorker()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = TRUE;
    newWorker->SetID(NextWorkerID++);
    Workers.Add(newWorker);
    if (!Workers.IsGood())
    {
        Workers.ResetState();
        ret = FALSE;
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

BOOL CFTPWorkersList::InformWorkersAboutStop(int workerInd, CFTPWorker** victims,
                                             int maxVictims, int* foundVictims)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::InformWorkersAboutStop(%d, , ,)", workerInd);

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = FALSE;
    if (workerInd != -1)
    {
        if (workerInd >= 0 && workerInd < Workers.Count)
        {
            if (*foundVictims < maxVictims)
            {
                CFTPWorker* worker = Workers[workerInd];
                if (worker->InformAboutStop()) // add them among the victims (CloseDataConnectionOrPostShouldStop() will be called on them later)
                    *(victims + (*foundVictims)++) = worker;
            }
            else
                ret = TRUE;
        }
    }
    else
    {
        int i = 0;
        while (*foundVictims < maxVictims && i < Workers.Count)
        {
            CFTPWorker* worker = Workers[i++];
            if (worker->InformAboutStop()) // add them among the victims (CloseDataConnectionOrPostShouldStop() will be called on them later)
                *(victims + (*foundVictims)++) = worker;
        }
        ret = i < Workers.Count;
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

BOOL CFTPWorkersList::InformWorkersAboutPause(int workerInd, CFTPWorker** victims,
                                              int maxVictims, int* foundVictims, BOOL pause)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::InformWorkersAboutPause(%d, , ,)", workerInd);

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = FALSE;
    if (workerInd != -1)
    {
        if (workerInd >= 0 && workerInd < Workers.Count)
        {
            if (*foundVictims < maxVictims)
            {
                CFTPWorker* worker = Workers[workerInd];
                if (worker->InformAboutPause(pause)) // add them among the victims (PostShouldPauseOrResume() will be called on them later)
                    *(victims + (*foundVictims)++) = worker;
            }
            else
                ret = TRUE;
        }
    }
    else
    {
        int i = 0;
        while (*foundVictims < maxVictims && i < Workers.Count)
        {
            CFTPWorker* worker = Workers[i++];
            if (worker->InformAboutPause(pause)) // add them among the victims (PostShouldPauseOrResume() will be called on them later)
                *(victims + (*foundVictims)++) = worker;
        }
        ret = i < Workers.Count;
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

BOOL CFTPWorkersList::CanCloseWorkers(int workerInd)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::CanCloseWorkers(%d)", workerInd);

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = TRUE;
    if (workerInd != -1)
    {
        if (workerInd >= 0 && workerInd < Workers.Count)
        {
            CFTPWorker* worker = Workers[workerInd];
            if (!worker->SocketClosedAndDataConDoesntExist() || worker->HaveWorkInDiskThread())
            {
                ret = FALSE;
            }
        }
    }
    else
    {
        int i;
        for (i = 0; i < Workers.Count; i++)
        {
            CFTPWorker* worker = Workers[i];
            if (!worker->SocketClosedAndDataConDoesntExist() || worker->HaveWorkInDiskThread())
            {
                ret = FALSE;
                break;
            }
        }
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

BOOL CFTPWorkersList::ForceCloseWorkers(int workerInd, CFTPWorker** victims,
                                        int maxVictims, int* foundVictims)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::ForceCloseWorkers(%d, , ,)", workerInd);

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = FALSE;
    if (workerInd != -1)
    {
        if (workerInd >= 0 && workerInd < Workers.Count)
        {
            CFTPWorker* worker = Workers[workerInd];
            worker->ForceCloseDiskWork();
            if (*foundVictims < maxVictims)
            {
                if (!worker->SocketClosedAndDataConDoesntExist()) // add them among the victims (ForceClose() will be called on them later)
                    *(victims + (*foundVictims)++) = worker;
            }
            else
                ret = TRUE;
        }
    }
    else
    {
        int i = 0;
        while (*foundVictims < maxVictims && i < Workers.Count)
        {
            CFTPWorker* worker = Workers[i++];
            worker->ForceCloseDiskWork();
            if (!worker->SocketClosedAndDataConDoesntExist()) // add them among the victims (ForceClose() will be called on them later)
                *(victims + (*foundVictims)++) = worker;
        }
        ret = i < Workers.Count;
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

BOOL CFTPWorkersList::DeleteWorkers(int workerInd, CFTPWorker** victims,
                                    int maxVictims, int* foundVictims,
                                    CUploadWaitingWorker** uploadFirstWaitingWorker)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::DeleteWorkers(%d, , , ,)", workerInd);

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = FALSE;
    if (workerInd != -1)
    {
        if (workerInd >= 0 && workerInd < Workers.Count)
        {
            if (*foundVictims < maxVictims)
            {
                CFTPWorker* worker = Workers[workerInd];
                worker->ReleaseData(uploadFirstWaitingWorker);
                Workers.Detach(workerInd);
                if (!Workers.IsGood())
                    Workers.ResetState(); // disconnection must always succeed (error = cannot shrink the array)
                if (worker->CanDeleteFromDelWorkers())
                    *(victims + (*foundVictims)++) = worker; // add them among the victims (DeleteSocket() will be called on them later)
            }
            else
                ret = TRUE;
        }
    }
    else
    {
        while (*foundVictims < maxVictims && Workers.Count > 0)
        {
            CFTPWorker* worker = Workers[Workers.Count - 1];
            worker->ReleaseData(uploadFirstWaitingWorker);
            Workers.Detach(Workers.Count - 1);
            if (!Workers.IsGood())
                Workers.ResetState(); // disconnection must always succeed (error = cannot shrink the array)
            if (worker->CanDeleteFromDelWorkers())
                *(victims + (*foundVictims)++) = worker; // add them among the victims (DeleteSocket() will be called on them later)
        }
        ret = Workers.Count > 0;
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

int CFTPWorkersList::GetCount()
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::GetCount()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    int ret = Workers.Count;
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

int CFTPWorkersList::GetFirstErrorIndex()
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::GetFirstErrorIndex()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    int ret = -1;
    int i;
    for (i = 0; i < Workers.Count; i++)
    {
        if (Workers[i]->HaveError())
        { // found
            ret = i;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

int CFTPWorkersList::GetWorkerIndex(int workerID)
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::GetWorkerIndex()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    int ret = -1;
    int i;
    for (i = 0; i < Workers.Count; i++)
    {
        if (Workers[i]->GetID() == workerID)
        { // found
            ret = i;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

void CFTPWorkersList::GetListViewDataFor(int index, NMLVDISPINFO* lvdi, std::wstring& text) noexcept
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::GetListViewDataFor()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    LVITEM* itemData = &(lvdi->item);
    if (index >= 0 && index < Workers.Count) // index is valid
    {
        Workers[index]->GetListViewData(itemData, text);
    }
    else // for an invalid index (listview has not refreshed yet) we must return at least an empty item
    {
        if (itemData->mask & LVIF_IMAGE)
            itemData->iImage = 0; // we have only one icon so far
        if (itemData->mask & LVIF_TEXT)
        {
            text.clear();
            itemData->pszText = const_cast<LPWSTR>(text.c_str());
        }
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
}

int CFTPWorkersList::GetWorkerID(int index)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::GetWorkerID(%d)", index);

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    int id = -1;
    if (index >= 0 && index < Workers.Count) // index is valid
    {
        id = Workers[index]->GetID();
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return id;
}

int CFTPWorkersList::GetLogUID(int index)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::GetLogUID(%d)", index);

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    int uid = -1;
    if (index >= 0 && index < Workers.Count) // index is valid
    {
        uid = Workers[index]->GetLogUID();
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return uid;
}

BOOL CFTPWorkersList::HaveError(int index)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::HaveError(%d)", index);

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = FALSE;
    if (index >= 0 && index < Workers.Count) // index is valid
    {
        ret = Workers[index]->HaveError();
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

BOOL CFTPWorkersList::IsPaused(int index, BOOL* isWorking)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::IsPaused(%d,)", index);

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = FALSE;
    if (index >= 0 && index < Workers.Count) // index is valid
    {
        ret = Workers[index]->IsPaused(isWorking);
    }
    else
        *isWorking = FALSE;
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

BOOL CFTPWorkersList::SomeWorkerIsWorking(BOOL* someIsWorkingAndNotPaused)
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::SomeWorkerIsWorking()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = FALSE;
    *someIsWorkingAndNotPaused = FALSE;
    BOOL isPaused, isWorking;
    int i;
    for (i = 0; i < Workers.Count; i++)
    {
        isPaused = Workers[i]->IsPaused(&isWorking);
        if (isWorking)
        {
            ret = TRUE;
            if (!isPaused)
            {
                *someIsWorkingAndNotPaused = TRUE;
                break; // nothing else to find out
            }
        }
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

BOOL CFTPWorkersList::GetErrorDescr(int index, std::wstring& errorText, CCertificate** unverifiedCertificate)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::GetErrorDescr(%d, ,)", index);

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    if (unverifiedCertificate != NULL)
        *unverifiedCertificate = NULL;
    BOOL ret = FALSE;
    CFTPWorker* worker = NULL;
    BOOL postActivate = FALSE;
    int msg = -1;
    int uid = -1;
    if (index >= 0 && index < Workers.Count) // index is valid
    {
        worker = Workers[index];
        ret = worker->GetErrorDescr(errorText, &postActivate, unverifiedCertificate);
        if (postActivate)
            postActivate = worker->GetPostTarget(&msg, &uid);
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    if (postActivate)
        SocketsThread->PostSocketMessage(msg, uid, WORKER_ACTIVATE, NULL);
    return ret;
}

void CFTPWorkersList::ActivateWorkers()
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::ActivateWorkers()");

    int i = 0;
    while (1)
    {
        HANDLES(EnterCriticalSection(&WorkersListCritSect));
        CFTPWorker* worker = (i < Workers.Count) ? Workers[i] : NULL;
        int msg = -1;
        int uid = -1;
        BOOL postActivate = FALSE;
        if (worker != NULL)
        {
            postActivate = worker->GetPostTarget(&msg, &uid);
        }
        HANDLES(LeaveCriticalSection(&WorkersListCritSect));
        if (postActivate)
            SocketsThread->PostSocketMessage(msg, uid, WORKER_ACTIVATE, NULL);
        if (worker == NULL)
            break; // end of loop
        i++;
    }
}

void CFTPWorkersList::PostLoginChanged(int workerID)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::PostLoginChanged(%d)", workerID);

    int i = 0;
    while (1)
    {
        HANDLES(EnterCriticalSection(&WorkersListCritSect));
        CFTPWorker* worker = (i < Workers.Count) ? Workers[i] : NULL;
        int uid = -1;
        int msg = -1;
        BOOL send = FALSE;
        if (worker != NULL)
        {
            if ((workerID == -1 || worker->GetID() == workerID) && // all or the one with ID 'workerID'
                worker->GetState() == fwsConnectionError)          // only if it is in state fwsConnectionError
            {
                send = worker->GetPostTarget(&msg, &uid);
            }
        }
        HANDLES(LeaveCriticalSection(&WorkersListCritSect));
        if (send)
            SocketsThread->PostSocketMessage(msg, uid, WORKER_NEWLOGINPARAMS, NULL);
        if (worker == NULL)
            break; // end of loop
        i++;
    }
}

BOOL CFTPWorkersList::GiveWorkToSleepingConWorker(CFTPWorker* sourceWorker)
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::GiveWorkToSleepingConWorker()");

    // ATTENTION: we may already be inside CSocketsThread::CritSect (and CSocket::SocketCritSect) !!!

    BOOL ret = FALSE;
    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL postActivate = FALSE;
    CFTPWorker* worker;
    int sourceWorkerUID = -1;
    int sourceWorkerMsg = -1;
    int workerUID = -1;
    int workerMsg = -1;
    int i;
    for (i = 0; i < Workers.Count; i++)
    {
        worker = Workers[i];
        if (worker != sourceWorker)
        {
            BOOL openCon, receivingWakeup;
            if (worker->IsSleeping(&openCon, &receivingWakeup) && openCon && !receivingWakeup) // "sleeping" worker with an open connection
            {
                worker->GiveWorkToSleepingConWorker(sourceWorker);
                postActivate = TRUE;
                if (!sourceWorker->GetPostTarget(&sourceWorkerMsg, &sourceWorkerUID))
                    sourceWorkerMsg = -1;
                if (!worker->GetPostTarget(&workerMsg, &workerUID))
                    workerMsg = -1;
                ret = TRUE;
                break;
            }
        }
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    if (postActivate)
    {
        if (sourceWorkerMsg != -1)
            SocketsThread->PostSocketMessage(sourceWorkerMsg, sourceWorkerUID, WORKER_ACTIVATE, NULL);
        if (workerMsg != -1)
            SocketsThread->PostSocketMessage(workerMsg, workerUID, WORKER_ACTIVATE, NULL);
    }
    return ret;
}

void CFTPWorkersList::AddCurrentDownloadSize(CQuadWord* downloaded)
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::AddCurrentDownloadSize()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    int i;
    for (i = 0; i < Workers.Count; i++)
        Workers[i]->AddCurrentDownloadSize(downloaded);
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
}

void CFTPWorkersList::AddCurrentUploadSize(CQuadWord* uploaded)
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::AddCurrentUploadSize()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    int i;
    for (i = 0; i < Workers.Count; i++)
        Workers[i]->AddCurrentUploadSize(uploaded);
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
}

BOOL CFTPWorkersList::SearchWorkerWithNewError(int* index, DWORD lastErrorOccurenceTime)
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::SearchWorkerWithNewError()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL res = FALSE;
    if (LastFoundErrorOccurenceTime + 1 < lastErrorOccurenceTime + 1) // +1 is here because -1 is used as initialization values
    {                                                                 // it makes sense to search
        int foundIndex = -1;
        DWORD foundErrorOccurenceTime = -1;
        int i;
        for (i = 0; i < Workers.Count; i++)
        {
            CFTPWorker* worker = Workers[i];
            DWORD workerErrorOccurenceTime = worker->GetErrorOccurenceTime();
            if (workerErrorOccurenceTime != -1 &&                                         // the worker contains an error (except for an error forced by the user while resolving login/password during reconnect wait)
                workerErrorOccurenceTime >= LastFoundErrorOccurenceTime + 1 &&            // it's a "new" error
                (foundIndex == -1 || foundErrorOccurenceTime > workerErrorOccurenceTime)) // the first one found so far or the "oldest" (we handle errors in the order they occurred)
            {
                foundErrorOccurenceTime = workerErrorOccurenceTime;
                foundIndex = i;
            }
        }
        if (foundIndex == -1)
            LastFoundErrorOccurenceTime = lastErrorOccurenceTime; // not found -> adjust LastFoundErrorOccurenceTime so that next time only "newer" errors are searched
        else
        {
            *index = foundIndex;
            LastFoundErrorOccurenceTime = foundErrorOccurenceTime;
            res = TRUE;
        }
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return res;
}

void CFTPWorkersList::PostNewWorkAvailable(BOOL onlyOneItem)
{
    CALL_STACK_MESSAGE2("CFTPWorkersList::PostNewWorkAvailable(%d)", onlyOneItem);

    // ATTENTION: we may already be inside CSocketsThread::CritSect (and CSocket::SocketCritSect) !!!

    if (onlyOneItem)
    {
        // first try to find a "sleeping" worker, preferably with an open connection
        HANDLES(EnterCriticalSection(&WorkersListCritSect));
        CFTPWorker* worker = NULL;
        int i, found = -1;
        for (i = 0; i < Workers.Count; i++)
        {
            worker = Workers[i];
            BOOL openCon, receivingWakeup;
            if (worker->IsSleeping(&openCon, &receivingWakeup) && !receivingWakeup)
            {
                if (openCon)
                    break; // found one with a connection, no point searching further
                else
                {
                    if (found == -1)
                        found = i; // store the first "sleeping" worker without a connection and keep searching
                }
            }
        }
        if (i == Workers.Count)
        {
            if (found != -1)
                worker = Workers[found];
            else
                worker = NULL;
        }
        int msg = -1;
        int uid = -1;
        BOOL postWakeup = FALSE;
        if (worker != NULL)
        {
            postWakeup = worker->GetPostTarget(&msg, &uid);
            if (postWakeup)
                worker->SetReceivingWakeup(TRUE);
        }
        HANDLES(LeaveCriticalSection(&WorkersListCritSect));

        if (postWakeup) // if there is at least one "sleeping" worker, post a "wake-up" to them
            SocketsThread->PostSocketMessage(msg, uid, WORKER_WAKEUP, NULL);
    }
    else
    {
        BOOL doSecRound = FALSE;
        int r;
        for (r = 0; r < 2; r++)
        {
            int i = 0;
            while (1)
            {
                BOOL openCon, receivingWakeup;
                CFTPWorker* worker = NULL;
                HANDLES(EnterCriticalSection(&WorkersListCritSect));
                while (i < Workers.Count)
                {
                    worker = Workers[i];
                    if (worker->IsSleeping(&openCon, &receivingWakeup) && !receivingWakeup)
                    {
                        if (r == 1 || openCon)
                            break; // first search for those with an open connection, then all of them
                        else
                            doSecRound = TRUE;
                    }
                    i++;
                }
                if (i == Workers.Count)
                    worker = NULL;
                int msg = -1;
                int uid = -1;
                BOOL postWakeup = FALSE;
                if (worker != NULL)
                {
                    postWakeup = worker->GetPostTarget(&msg, &uid);
                    if (postWakeup)
                        worker->SetReceivingWakeup(TRUE);
                }
                HANDLES(LeaveCriticalSection(&WorkersListCritSect));

                if (worker == NULL)
                    break; // there is no worker left in the array
                if (postWakeup)
                    SocketsThread->PostSocketMessage(msg, uid, WORKER_WAKEUP, NULL);
                i++;
            }
            if (!doSecRound)
                break;
        }
    }
}

BOOL CFTPWorkersList::EmptyOrAllShouldStop()
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::EmptyOrAllShouldStop()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = TRUE;
    int i;
    for (i = 0; i < Workers.Count; i++)
    {
        if (!Workers[i]->GetShouldStop())
        {
            ret = FALSE;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

BOOL CFTPWorkersList::AtLeastOneWorkerIsWaitingForUser()
{
    CALL_STACK_MESSAGE1("CFTPWorkersList::AtLeastOneWorkerIsWaitingForUser()");

    HANDLES(EnterCriticalSection(&WorkersListCritSect));
    BOOL ret = FALSE;
    int i;
    for (i = 0; i < Workers.Count; i++)
    {
        if (Workers[i]->GetState() == fwsConnectionError)
        {
            ret = TRUE;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&WorkersListCritSect));
    return ret;
}

//
// ****************************************************************************
// CReturningConnections
//

CReturningConnections::CReturningConnections(int base, int delta) : Data(base, delta)
{
    HANDLES(InitializeCriticalSection(&RetConsCritSect));
}

CReturningConnections::~CReturningConnections()
{
    if (Data.Count > 0)
        TRACE_E("Unexpected situation in CReturningConnections::~CReturningConnections(): array is not empty!");
    HANDLES(DeleteCriticalSection(&RetConsCritSect));
}

BOOL CReturningConnections::Add(int controlConUID, CFTPWorker* workerWithCon)
{
    HANDLES(EnterCriticalSection(&RetConsCritSect));
    BOOL ok = TRUE;
    CReturningConnectionData* newItem = new CReturningConnectionData(controlConUID, workerWithCon);
    if (newItem != NULL)
    {
        Data.Add(newItem);
        if (!Data.IsGood())
        {
            delete newItem;
            Data.ResetState();
            ok = FALSE;
        }
    }
    else
    {
        TRACE_E(LOW_MEMORY);
        ok = FALSE;
    }
    HANDLES(LeaveCriticalSection(&RetConsCritSect));
    return ok;
}

BOOL CReturningConnections::GetFirstCon(int* controlConUID, CFTPWorker** workerWithCon)
{
    HANDLES(EnterCriticalSection(&RetConsCritSect));
    BOOL ok = TRUE;
    if (Data.Count > 0)
    {
        *controlConUID = Data[0]->ControlConUID;
        *workerWithCon = Data[0]->WorkerWithCon;
        Data.Delete(0);
        if (!Data.IsGood())
            Data.ResetState();
    }
    else
    {
        *controlConUID = -1;
        *workerWithCon = NULL;
        ok = FALSE;
    }
    HANDLES(LeaveCriticalSection(&RetConsCritSect));
    return ok;
}

void CReturningConnections::CloseData()
{
    HANDLES(EnterCriticalSection(&RetConsCritSect));
    while (Data.Count > 0)
    {
        CFTPWorker* worker = Data[Data.Count - 1]->WorkerWithCon;
        Data.Delete(Data.Count - 1);
        if (!Data.IsGood())
            Data.ResetState();
        HANDLES(LeaveCriticalSection(&RetConsCritSect));

        worker->ForceClose(); // forcefully close the socket (nothing should be waiting on close socket (calling CloseSocket() would suffice), but we play it safe - SocketClosed is set to TRUE immediately after adding to ReturningConnections)
        if (worker->CanDeleteFromRetCons())
            DeleteSocket(worker);

        HANDLES(EnterCriticalSection(&RetConsCritSect));
    }
    HANDLES(LeaveCriticalSection(&RetConsCritSect));
}

//
// ****************************************************************************
// CFTPFileToClose
//

CFTPFileToClose::CFTPFileToClose(const wchar_t* path, const wchar_t* name, HANDLE file, BOOL deleteIfEmpty,
                                 BOOL setDateAndTime, const CFTPDate* date, const CFTPTime* time,
                                 BOOL deleteFile, CQuadWord* setEndOfFile)
{
    File = file;
    DeleteIfEmpty = deleteIfEmpty;
    FileName = path != NULL ? path : L"";
    SPLSalPathAppendOwned(FileName, name);
    SetDateAndTime = setDateAndTime;
    if (SetDateAndTime && date != NULL)
        Date = *date;
    else
        memset(&Date, 0, sizeof(Date));
    if (SetDateAndTime && time != NULL)
        Time = *time;
    else
    {
        memset(&Time, 0, sizeof(Time));
        Time.Hour = 24;
    }
    AlwaysDeleteFile = deleteFile;
    if (setEndOfFile != NULL)
        EndOfFile = *setEndOfFile;
    else
        EndOfFile.Set(-1, -1);
}

//
// ****************************************************************************
// CDiskListingItem
//

CDiskListingItem::CDiskListingItem(const wchar_t* name, BOOL isDir, const CQuadWord& size)
{
    Name = name != NULL ? _wcsdup(name) : NULL;
    IsDir = isDir;
    Size = size;
}

//
// ****************************************************************************
// CFTPDiskThread
//

CFTPDiskThread::CFTPDiskThread() : CThread(L"FTP Disk Thread"), Work(20, 50, dtNoDelete), FilesToClose(20, 50)
{
    HANDLES(InitializeCriticalSection(&DiskCritSect));
    ContEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL)); // manual, nonsignaled
    if (ContEvent == NULL)
        TRACE_E("CFTPDiskThread::CFTPDiskThread(): Unable to create synchronization event object.");
    ShouldTerminate = FALSE;
    WorkIsInProgress = FALSE;
    NextFileCloseIndex = 0;
    DoneFileCloseIndex = -1;
    FileClosedEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL)); // manual, nonsignaled
    if (FileClosedEvent == NULL)
        TRACE_E("CFTPDiskThread::CFTPDiskThread(): Unable to create FileClosedEvent object.");
}

CFTPDiskThread::~CFTPDiskThread()
{
    while (Work.Count > 0 && Work[0] == NULL)
    {
        Work.Detach(0);
        if (!Work.IsGood())
            Work.ResetState();
    }
    if (Work.Count > 0)
        TRACE_E("Unexpected situation in CFTPDiskThread::~CFTPDiskThread(): array with work is not empty!");
    if (FilesToClose.Count > 0)
        TRACE_I("CFTPDiskThread::~CFTPDiskThread(): array with files to close is not empty!");
    if (ContEvent != NULL)
        HANDLES(CloseHandle(ContEvent));
    if (FileClosedEvent != NULL)
        HANDLES(CloseHandle(FileClosedEvent));
    FileClosedEvent = NULL;
    HANDLES(DeleteCriticalSection(&DiskCritSect));
}

void CFTPDiskThread::Terminate()
{
    HANDLES(EnterCriticalSection(&DiskCritSect));
    ShouldTerminate = TRUE;
    SetEvent(ContEvent);
    HANDLES(LeaveCriticalSection(&DiskCritSect));
}

BOOL CFTPDiskThread::AddWork(CFTPDiskWork* work)
{
    CALL_STACK_MESSAGE1("CFTPDiskThread::AddWork()");
    HANDLES(EnterCriticalSection(&DiskCritSect));
    BOOL ret = TRUE;
    Work.Add(work);
    if (!Work.IsGood())
    {
        Work.ResetState();
        ret = FALSE;
    }
    if (Work.Count == 1)
        SetEvent(ContEvent);
    HANDLES(LeaveCriticalSection(&DiskCritSect));
    return ret;
}

BOOL CFTPDiskThread::CancelWork(const CFTPDiskWork* work, BOOL* workIsInProgress)
{
    CALL_STACK_MESSAGE1("CFTPDiskThread::CancelWork()");
    HANDLES(EnterCriticalSection(&DiskCritSect));
    BOOL ret = FALSE; // not found = the work is already finished and removed from the Work array
    if (workIsInProgress != NULL)
        *workIsInProgress = FALSE;
    int i;
    for (i = 0; i < Work.Count; i++)
    {
        if (Work[i] == work)
        {
            ret = TRUE;
            if (i == 0)
            {
                Work[0] = NULL; // the first item may currently be processed, cannot remove it from the array (it is rewritten to NULL to detect its cancellation)
                if (workIsInProgress != NULL)
                    *workIsInProgress = WorkIsInProgress;
            }
            else // the work has certainly not started processing yet, so we can simply drop it
            {
                Work.Detach(i);
                if (!Work.IsGood())
                    Work.ResetState();
            }
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&DiskCritSect));
    return ret;
}

BOOL CFTPDiskThread::AddFileToClose(const wchar_t* path, const wchar_t* name, HANDLE file, BOOL deleteIfEmpty,
                                    BOOL setDateAndTime, const CFTPDate* date, const CFTPTime* time,
                                    BOOL deleteFile, CQuadWord* setEndOfFile, int* fileCloseIndex)
{
    CALL_STACK_MESSAGE1("CFTPDiskThread::AddFileToClose()");
    HANDLES(EnterCriticalSection(&DiskCritSect));
    BOOL ret = FALSE;
    if (fileCloseIndex != NULL)
        *fileCloseIndex = -1;
    CFTPFileToClose* n = new CFTPFileToClose(path, name, file, deleteIfEmpty, setDateAndTime,
                                             date, time, deleteFile, setEndOfFile);
    if (n != NULL)
    {
        FilesToClose.Add(n);
        if (!FilesToClose.IsGood())
        {
            FilesToClose.ResetState();
            delete n;
        }
        else
        {
            ret = TRUE;
            if (fileCloseIndex != NULL)
                *fileCloseIndex = NextFileCloseIndex;
            NextFileCloseIndex++;
        }
        if (FilesToClose.Count == 1)
            SetEvent(ContEvent);
    }
    else
        TRACE_E(LOW_MEMORY);
    HANDLES(LeaveCriticalSection(&DiskCritSect));
    return ret;
}

BOOL CFTPDiskThread::WaitForFileClose(int fileCloseIndex, DWORD timeout)
{
    CALL_STACK_MESSAGE3("CFTPDiskThread::WaitForFileClose(%d, %u)", fileCloseIndex, timeout);
    DWORD ti = GetTickCount();
    BOOL closed = FALSE;
    while (1)
    {
        HANDLES(EnterCriticalSection(&DiskCritSect));
        if (DoneFileCloseIndex != -1 && fileCloseIndex <= DoneFileCloseIndex)
            closed = TRUE;
        HANDLES(LeaveCriticalSection(&DiskCritSect));
        if (closed)
            break;
        DWORD elapsed = GetTickCount() - ti;
        if (FileClosedEvent != NULL)
        {
            DWORD res = WaitForSingleObject(FileClosedEvent, timeout == INFINITE ? INFINITE : (elapsed < timeout ? timeout - elapsed : 0));
            if (res != WAIT_OBJECT_0)
            {
                TRACE_I("CFTPDiskThread::WaitForFileClose(): waiting for file closure has timed out!");
                break; // timeout or abandoned (should not be needed)
            }
        }
        else // always false (only if creating FileClosedEvent failed) -> use "active" waiting
        {
            if (timeout != INFINITE && elapsed >= timeout)
                break; // timeout
            Sleep(100);
        }
    }
    return closed;
}

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES (-1)
#endif // INVALID_FILE_ATTRIBUTES

static BOOL BuildWideDiskChildPath(std::wstring& out, std::wstring_view path,
                                   std::wstring_view name) noexcept
{
    try
    {
        std::wstring staged(path);
        const std::wstring stagedName(name);
        SPLSalPathAppendOwned(staged, stagedName.c_str());
        out.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

static BOOL MakeValidDiskName(std::wstring& name) noexcept
{
    std::wstring validName;
    try
    {
        validName = name;
    }
    catch (...)
    {
        return FALSE;
    }
    if (!SPLSalMakeValidFileNameComponentOwned(SalamanderGeneral, validName))
        return FALSE;
    name.swap(validName);
    return TRUE;
}

static BOOL MakeAutorenameName(const std::wstring& baseName, int suffixNumber,
                               BOOL preserveExtension, std::wstring& name) noexcept
{
    try
    {
        const std::wstring suffix = L" (" + std::to_wstring(suffixNumber) + L")";
        const size_t extension = preserveExtension ? baseName.find_last_of(L'.') : std::wstring::npos;
        std::wstring staged;
        if (extension == std::wstring::npos)
        {
            staged = baseName;
            staged += suffix;
        }
        else
        {
            staged.assign(baseName, 0, extension);
            staged += suffix;
            staged.append(baseName, extension, std::wstring::npos);
        }
        name.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

static void StripAutorenameSuffix(std::wstring& name, int& suffixNumber) noexcept
{
    if (name.size() < 4)
        return;
    const size_t extension = name.find_last_of(L'.');
    if (extension == 0)
        return;
    const size_t close = extension == std::wstring::npos ? name.size() - 1 : extension - 1;
    if (name[close] != L')')
        return;
    const size_t open = name.rfind(L" (", close);
    if (open == std::wstring::npos || open + 2 >= close)
        return;
    int value = 0;
    for (size_t i = open + 2; i < close; ++i)
    {
        if (name[i] < L'0' || name[i] > L'9' || value > (INT_MAX - 9) / 10)
            return;
        value = value * 10 + (name[i] - L'0');
    }
    if (value < 2)
        return;
    name.erase(open, close - open + 1);
    suffixNumber = value;
}

void DoCreateDir(CFTPDiskWork& localWork, BOOL& workDone, BOOL& needCopyBack)
{
    BOOL isValid = SalamanderGeneral->SalIsValidFileNameComponent(localWork.Name.c_str());
    DWORD winErr = NO_ERROR;
    BOOL alreadyExists = FALSE;
    std::wstring fullName;
    if (isValid)
    {
        if (!BuildWideDiskChildPath(fullName, localWork.Path, localWork.Name))
            winErr = ERROR_NOT_ENOUGH_MEMORY;
        else
        {
            DWORD e;
            if (!SalamanderGeneral->SalCreateDirectoryEx(fullName.c_str(), &e))
            {
                winErr = e;
                if (winErr != ERROR_ALREADY_EXISTS && winErr != ERROR_FILE_EXISTS)
                { // check whether the error was caused by an existing file/directory, and continue searching for a name via autorename if needed
                    DWORD attr = SalamanderGeneral->SalGetFileAttributes(fullName.c_str());
                    if (attr != INVALID_FILE_ATTRIBUTES)
                        winErr = ERROR_ALREADY_EXISTS;
                }
                if (winErr == ERROR_ALREADY_EXISTS || winErr == ERROR_FILE_EXISTS)
                {
                    DWORD attr = SalamanderGeneral->SalGetFileAttributes(fullName.c_str());
                    if (attr == INVALID_FILE_ATTRIBUTES)
                        winErr = GetLastError();
                    else
                    {
                        if (attr & FILE_ATTRIBUTE_DIRECTORY)
                            alreadyExists = TRUE;
                    }
                }
            }
            else
                workDone = TRUE;
        }
    }
    else
        winErr = ERROR_INVALID_NAME; // incorrect syntax of dir name

    if (winErr != NO_ERROR)
    {
        int action = 0;                                                   // 0 - nothing, 1 - autorename
        if (localWork.ForceAction == fqiaUseAutorename ||                 // try autorename
            localWork.ForceAction == fqiaUseExistingDir && alreadyExists) // the directory already exists and we force "use existing dir" -> return success
        {
            if (localWork.ForceAction == fqiaUseAutorename)
                action = 1;
        }
        else
        {
            if (alreadyExists)
            { // dir already exists
                switch (localWork.DirAlreadyExists)
                {
                case DIRALREADYEXISTS_USERPROMPT:
                {
                    localWork.ProblemID = ITEMPR_TGTDIRALREADYEXISTS;
                    localWork.State = sqisUserInputNeeded;
                    needCopyBack = TRUE;
                    break;
                }

                case DIRALREADYEXISTS_AUTORENAME:
                    action = 1;
                    break;
                case DIRALREADYEXISTS_JOIN:
                    break; // return success

                case DIRALREADYEXISTS_SKIP:
                {
                    localWork.ProblemID = ITEMPR_TGTDIRALREADYEXISTS;
                    localWork.State = sqisSkipped;
                    needCopyBack = TRUE;
                    break;
                }
                }
            }
            else // cannot create dir
            {
                switch (localWork.CannotCreateDir)
                {
                case CANNOTCREATENAME_USERPROMPT:
                {
                    localWork.ProblemID = ITEMPR_CANNOTCREATETGTDIR;
                    localWork.WinError = winErr;
                    localWork.State = sqisUserInputNeeded;
                    needCopyBack = TRUE;
                    break;
                }

                case CANNOTCREATENAME_AUTORENAME:
                    action = 1;
                    break;

                case CANNOTCREATENAME_SKIP:
                {
                    localWork.ProblemID = ITEMPR_CANNOTCREATETGTDIR;
                    localWork.WinError = winErr;
                    localWork.State = sqisSkipped;
                    needCopyBack = TRUE;
                    break;
                }
                }
            }
        }

        if (action == 1) // autorename (for already exists, cannot create, and force action)
        {
            BOOL ok = FALSE;
            if (!isValid)
            {
                if (!MakeValidDiskName(localWork.Name))
                {
                    localWork.ProblemID = ITEMPR_LOWMEM;
                    localWork.State = sqisFailed;
                    needCopyBack = TRUE;
                    return;
                }
                needCopyBack = TRUE;
            }
            const std::wstring baseName = localWork.Name;
            BOOL tryCurrentName = !isValid;
            BOOL firstAttempt = TRUE;
            int itemProblem = ITEMPR_CANNOTCREATETGTDIR;
            int suffixCounter = 1;
            while (1)
            {
                if (!tryCurrentName)
                {
                    if (!firstAttempt && winErr != ERROR_FILE_EXISTS && winErr != ERROR_ALREADY_EXISTS)
                        break;
                    if (suffixCounter == INT_MAX ||
                        !MakeAutorenameName(baseName, ++suffixCounter, FALSE, localWork.Name))
                    {
                        itemProblem = ITEMPR_LOWMEM;
                        winErr = NO_ERROR;
                        break;
                    }
                    needCopyBack = TRUE;
                }
                tryCurrentName = FALSE;
                if (!BuildWideDiskChildPath(fullName, localWork.Path, localWork.Name))
                {
                    itemProblem = ITEMPR_LOWMEM;
                    winErr = NO_ERROR;
                    break;
                }
                // allocate the new name
                if (localWork.NewTgtName != NULL)
                    free(localWork.NewTgtName);
                localWork.NewTgtName = _wcsdup(localWork.Name.c_str());
                if (localWork.NewTgtName == NULL)
                {
                    itemProblem = ITEMPR_LOWMEM;
                    winErr = NO_ERROR;
                    break;
                }
                // try another directory name
                if (!CreateDirectoryW(fullName.c_str(), NULL))
                {
                    winErr = GetLastError();
                    if (winErr != ERROR_ALREADY_EXISTS && winErr != ERROR_FILE_EXISTS)
                    { // check whether the error was caused by an existing file/directory, and continue searching for a name via autorename if needed
                        DWORD attr = SalamanderGeneral->SalGetFileAttributes(fullName.c_str());
                        if (attr != INVALID_FILE_ATTRIBUTES)
                            winErr = ERROR_ALREADY_EXISTS;
                    }
                }
                else
                {
                    workDone = TRUE;
                    ok = TRUE;
                    break; // success, return OK + the new name
                }
                firstAttempt = FALSE;
            }

            if (!ok)
            {
                if (localWork.NewTgtName != NULL)
                    free(localWork.NewTgtName);
                localWork.NewTgtName = NULL;
                localWork.ProblemID = itemProblem;
                localWork.WinError = winErr;
                localWork.State = sqisFailed;                 // may still be adjusted in the code a few lines below
                if (itemProblem == ITEMPR_CANNOTCREATETGTDIR) // reported error is "unable to create or open file"
                {
                    switch (localWork.CannotCreateDir)
                    {
                    case CANNOTCREATENAME_USERPROMPT:
                        localWork.State = sqisUserInputNeeded;
                        break;
                    // case CANNOTCREATENAME_AUTORENAME: localWork.State = sqisFailed; break;
                    case CANNOTCREATENAME_SKIP:
                        localWork.State = sqisSkipped;
                        break;
                    }
                }
                needCopyBack = TRUE;
            }
        }
    }
}

CFTPQueueItemState DoCreateFileGetWantedErrorState(CFTPDiskWork& localWork)
{                                          // helper function for DoCreateFile()
    CFTPQueueItemState state = sqisFailed; // may still be adjusted in the code a few lines below
    switch (localWork.CannotCreateFile)    // reported error is "unable to create or open file"
    {
    case CANNOTCREATENAME_USERPROMPT:
        state = sqisUserInputNeeded;
        break;
    // case CANNOTCREATENAME_AUTORENAME: state = sqisFailed; break;
    case CANNOTCREATENAME_SKIP:
        state = sqisSkipped;
        break;
    }
    return state;
}

void DoCreateFile(CFTPDiskWork& localWork, BOOL& workDone, BOOL& needCopyBack)
{
    BOOL isValid = SalamanderGeneral->SalIsValidFileNameComponent(localWork.Name.c_str());
    DWORD winErr = NO_ERROR;
    BOOL alreadyExists = FALSE;
    HANDLE file = NULL;
    std::wstring fullName;
    if (isValid)
    {
        if (!BuildWideDiskChildPath(fullName, localWork.Path, localWork.Name))
            winErr = ERROR_NOT_ENOUGH_MEMORY;
        else
        {
            DWORD salCrErr;
            file = SalamanderGeneral->SalCreateFileEx(fullName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, FILE_FLAG_SEQUENTIAL_SCAN, &salCrErr);
            SetLastError(salCrErr);
            HANDLES_ADD_EX(__otQuiet, file != INVALID_HANDLE_VALUE, __htFile,
                           __hoCreateFile, file, salCrErr, TRUE);
            if (file == INVALID_HANDLE_VALUE) // cannot create a new file
            {
                winErr = GetLastError();
                if (winErr != ERROR_ALREADY_EXISTS && winErr != ERROR_FILE_EXISTS)
                { // check whether the error was caused by an existing file/directory, and continue searching for a name via autorename if needed
                    DWORD attr = SalamanderGeneral->SalGetFileAttributes(fullName.c_str());
                    if (attr != INVALID_FILE_ATTRIBUTES)
                        winErr = ERROR_ALREADY_EXISTS;
                }
                if (winErr == ERROR_ALREADY_EXISTS || winErr == ERROR_FILE_EXISTS)
                {
                    DWORD attr = SalamanderGeneral->SalGetFileAttributes(fullName.c_str());
                    if (attr == INVALID_FILE_ATTRIBUTES)
                        winErr = GetLastError();
                    else
                    {
                        if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
                            alreadyExists = TRUE;
                    }
                }
            }
            else
            {
                workDone = TRUE; // if the operation is cancelled, delete the newly created file
                localWork.OpenedFile = file;
                localWork.FileSize.Set(0, 0);
                localWork.CanOverwrite = TRUE;       // the file was newly created (not resumed)
                localWork.CanDeleteEmptyFile = TRUE; // the file was newly created (not resumed)
                needCopyBack = TRUE;
            }
        }
    }
    else
        winErr = ERROR_INVALID_NAME; // incorrect syntax of file name

    if (winErr != NO_ERROR)
    {
        int action = 0; // 0 - nothing, 1 - autorename, 2 - resume, 3 - resume or overwrite, 4 - overwrite
        BOOL reduceFileSize = FALSE;
        if (localWork.ForceAction == fqiaUseAutorename)
            action = 1;
        else
        {
            if (alreadyExists)
            {
                switch (localWork.ForceAction)
                {
                case fqiaResume:
                    action = 2;
                    break;
                case fqiaResumeOrOverwrite:
                    action = 3;
                    break;
                case fqiaOverwrite:
                    action = 4;
                    break;
                case fqiaReduceFileSizeAndResume:
                    action = 2;
                    reduceFileSize = TRUE;
                    break;
                }
            }
        }
        if (action == 0)
        {
            if (alreadyExists)
            { // file already exists
                switch (localWork.Type)
                {
                case fdwtCreateFile:
                {
                    switch (localWork.FileAlreadyExists)
                    {
                    case FILEALREADYEXISTS_USERPROMPT:
                    {
                        localWork.ProblemID = ITEMPR_TGTFILEALREADYEXISTS;
                        localWork.State = sqisUserInputNeeded;
                        needCopyBack = TRUE;
                        break;
                    }

                    case FILEALREADYEXISTS_AUTORENAME:
                        action = 1;
                        break;
                    case FILEALREADYEXISTS_RESUME:
                        action = 2;
                        break;
                    case FILEALREADYEXISTS_RES_OVRWR:
                        action = 3;
                        break;
                    case FILEALREADYEXISTS_OVERWRITE:
                        action = 4;
                        break;

                    case FILEALREADYEXISTS_SKIP:
                    {
                        localWork.ProblemID = ITEMPR_TGTFILEALREADYEXISTS;
                        localWork.State = sqisSkipped;
                        needCopyBack = TRUE;
                        break;
                    }
                    }
                    break;
                }

                case fdwtRetryCreatedFile:
                {
                    switch (localWork.RetryOnCreatedFile)
                    {
                    case RETRYONCREATFILE_USERPROMPT:
                    {
                        localWork.ProblemID = ITEMPR_RETRYONCREATFILE;
                        localWork.State = sqisUserInputNeeded;
                        needCopyBack = TRUE;
                        break;
                    }

                    case RETRYONCREATFILE_AUTORENAME:
                        action = 1;
                        break;
                    case RETRYONCREATFILE_RESUME:
                        action = 2;
                        break;
                    case RETRYONCREATFILE_RES_OVRWR:
                        action = 3;
                        break;
                    case RETRYONCREATFILE_OVERWRITE:
                        action = 4;
                        break;

                    case RETRYONCREATFILE_SKIP:
                    {
                        localWork.ProblemID = ITEMPR_RETRYONCREATFILE;
                        localWork.State = sqisSkipped;
                        needCopyBack = TRUE;
                        break;
                    }
                    }
                    break;
                }

                case fdwtRetryResumedFile:
                {
                    switch (localWork.RetryOnResumedFile)
                    {
                    case RETRYONRESUMFILE_USERPROMPT:
                    {
                        localWork.ProblemID = ITEMPR_RETRYONRESUMFILE;
                        localWork.State = sqisUserInputNeeded;
                        needCopyBack = TRUE;
                        break;
                    }

                    case RETRYONRESUMFILE_AUTORENAME:
                        action = 1;
                        break;
                    case RETRYONRESUMFILE_RESUME:
                        action = 2;
                        break;
                    case RETRYONRESUMFILE_RES_OVRWR:
                        action = 3;
                        break;
                    case RETRYONRESUMFILE_OVERWRITE:
                        action = 4;
                        break;

                    case RETRYONRESUMFILE_SKIP:
                    {
                        localWork.ProblemID = ITEMPR_RETRYONRESUMFILE;
                        localWork.State = sqisSkipped;
                        needCopyBack = TRUE;
                        break;
                    }
                    }
                    break;
                }
                }
            }
            else // cannot create file
            {
                switch (localWork.CannotCreateFile)
                {
                case CANNOTCREATENAME_USERPROMPT:
                {
                    localWork.ProblemID = ITEMPR_CANNOTCREATETGTFILE;
                    localWork.WinError = winErr;
                    localWork.State = sqisUserInputNeeded;
                    needCopyBack = TRUE;
                    break;
                }

                case CANNOTCREATENAME_AUTORENAME:
                    action = 1;
                    break;

                case CANNOTCREATENAME_SKIP:
                {
                    localWork.ProblemID = ITEMPR_CANNOTCREATETGTFILE;
                    localWork.WinError = winErr;
                    localWork.State = sqisSkipped;
                    needCopyBack = TRUE;
                    break;
                }
                }
            }
        }

        DWORD attr = 0;
        BOOL readonly = FALSE;
        switch (action)
        {
        case 1: // autorename (for already exists, transfer failed, cannot create, and force action)
        {
            BOOL ok = FALSE;
            if (!isValid)
            {
                if (!MakeValidDiskName(localWork.Name))
                {
                    localWork.ProblemID = ITEMPR_LOWMEM;
                    localWork.State = sqisFailed;
                    needCopyBack = TRUE;
                    break;
                }
                needCopyBack = TRUE;
            }
            int suffixCounter = 1;
            BOOL tryCurrentName = !isValid;
            BOOL firstAttempt = TRUE;
            std::wstring baseName = localWork.Name;
            if (isValid && localWork.AlreadyRenamedName) // second round of renaming: ensure "name (2)" -> "name (3)" instead of -> "name (2) (2)"
            {
                StripAutorenameSuffix(baseName, suffixCounter);
                tryCurrentName = FALSE; // start trying suffixes immediately
            }
            int itemProblem = ITEMPR_CANNOTCREATETGTFILE;
            while (1)
            {
                if (!tryCurrentName)
                {
                    if (!firstAttempt && winErr != ERROR_FILE_EXISTS && winErr != ERROR_ALREADY_EXISTS)
                        break;
                    if (suffixCounter == INT_MAX ||
                        !MakeAutorenameName(baseName, ++suffixCounter, TRUE, localWork.Name))
                    {
                        itemProblem = ITEMPR_LOWMEM;
                        winErr = NO_ERROR;
                        break;
                    }
                    needCopyBack = TRUE;
                }
                tryCurrentName = FALSE;
                if (!BuildWideDiskChildPath(fullName, localWork.Path, localWork.Name))
                {
                    itemProblem = ITEMPR_LOWMEM;
                    winErr = NO_ERROR;
                    break;
                }
                // allocate the new name
                if (localWork.NewTgtName != NULL)
                    free(localWork.NewTgtName);
                localWork.NewTgtName = _wcsdup(localWork.Name.c_str());
                if (localWork.NewTgtName == NULL)
                {
                    itemProblem = ITEMPR_LOWMEM;
                    winErr = NO_ERROR;
                    break;
                }
                // try another file name
                file = HANDLES_Q(CreateFileW(fullName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                            CREATE_NEW, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                if (file == INVALID_HANDLE_VALUE)
                {
                    winErr = GetLastError();
                    if (winErr != ERROR_ALREADY_EXISTS && winErr != ERROR_FILE_EXISTS)
                    { // check whether the error was caused by an existing file/directory, and continue searching for a name via autorename if needed
                        DWORD attr2 = SalamanderGeneral->SalGetFileAttributes(fullName.c_str());
                        if (attr2 != INVALID_FILE_ATTRIBUTES)
                            winErr = ERROR_ALREADY_EXISTS;
                    }
                }
                else
                {
                    workDone = TRUE; // if the operation is cancelled, delete the newly created file
                    ok = TRUE;
                    localWork.OpenedFile = file;
                    localWork.FileSize.Set(0, 0);
                    localWork.CanOverwrite = TRUE;       // the file was newly created (not resumed)
                    localWork.CanDeleteEmptyFile = TRUE; // the file was newly created (not resumed)
                    needCopyBack = TRUE;
                    break; // success, return OK + the new name
                }
                firstAttempt = FALSE;
            }

            if (!ok)
            {
                if (localWork.NewTgtName != NULL)
                    free(localWork.NewTgtName);
                localWork.NewTgtName = NULL;
                localWork.ProblemID = itemProblem;
                localWork.WinError = winErr;
                localWork.State = (itemProblem == ITEMPR_CANNOTCREATETGTFILE ? DoCreateFileGetWantedErrorState(localWork) : sqisFailed);
                needCopyBack = TRUE;
            }
            break;
        }

        case 2: // resume (for already exists, transfer failed, and force action) + if reduceFileSize==TRUE we also need to shrink the file
        case 3: // resume or overwrite (for already exists, transfer failed, and force action)
        {
            file = HANDLES_Q(CreateFileW(fullName.c_str(),
                                        GENERIC_READ /* we will read and check the overlap */ |
                                            GENERIC_WRITE,
                                        FILE_SHARE_READ, NULL,
                                        OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
            if (file == INVALID_HANDLE_VALUE) // cannot open the file
            {
                winErr = GetLastError();
                // check whether it happens to be read-only (only via the attribute)
                attr = SalamanderGeneral->SalGetFileAttributes(fullName.c_str());
                if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
                { // try to clear the read-only attribute and open the file again
                    readonly = TRUE;
                    SetFileAttributesW(fullName.c_str(), attr & (~FILE_ATTRIBUTE_READONLY));
                    file = HANDLES_Q(CreateFileW(fullName.c_str(),
                                                GENERIC_READ /* we will read and check the overlap */ |
                                                    GENERIC_WRITE,
                                                FILE_SHARE_READ, NULL,
                                                OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                    winErr = GetLastError();
                }
            }
            BOOL denyOverwrite = FALSE;
            if (file != INVALID_HANDLE_VALUE) // the file is open
            {
                denyOverwrite = TRUE; // the file was opened successfully; no point overwriting it if determining its size fails
                CQuadWord size;
                size.LoDWord = GetFileSize(file, &size.HiDWord);
                if (size.LoDWord != INVALID_FILE_SIZE || (winErr = GetLastError()) == NO_ERROR)
                {
                    BOOL ok = TRUE;
                    if (reduceFileSize) // we still need to shorten the file, so let's do it
                    {
                        DWORD resumeOverlap = Config.GetResumeOverlap();
                        if (resumeOverlap == 0)
                            resumeOverlap = 1; // at least by one byte (this threatens only if the user just changed it in configuration)
                        if (size < CQuadWord(resumeOverlap, 0))
                            resumeOverlap = (DWORD)size.Value;
                        size -= CQuadWord(resumeOverlap, 0);

                        CQuadWord curSeek = size;
                        curSeek.LoDWord = SetFilePointer(file, curSeek.LoDWord, (LONG*)&curSeek.HiDWord, FILE_BEGIN);
                        if (curSeek.LoDWord == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR ||
                            curSeek != size)
                        { // error: cannot set seek in the file
                            ok = FALSE;
                            winErr = GetLastError();
                            HANDLES(CloseHandle(file)); // no need to delete the file because it existed a moment ago (it likely hasn't disappeared => we did not create it)
                        }
                        else
                        {
                            if (!SetEndOfFile(file))
                            { // error: the file cannot be truncated
                                ok = FALSE;
                                winErr = GetLastError();
                                HANDLES(CloseHandle(file)); // no need to delete the file because it existed a moment ago (it likely hasn't disappeared => we did not create it)
                            }
                            else
                            {
                                curSeek.Set(0, 0);
                                curSeek.LoDWord = SetFilePointer(file, curSeek.LoDWord, (LONG*)&curSeek.HiDWord, FILE_BEGIN);
                                if (curSeek.LoDWord == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR ||
                                    curSeek != CQuadWord(0, 0))
                                { // error: cannot seek back to the start of the file
                                    ok = FALSE;
                                    winErr = GetLastError();
                                    HANDLES(CloseHandle(file)); // no need to delete the file because it existed a moment ago (it likely hasn't disappeared => we did not create it)
                                }
                            }
                        }
                    }

                    if (ok)
                    {
                        // workDone = TRUE;  // when cancelling the operation we will not delete the file, it's a resume
                        localWork.OpenedFile = file;
                        localWork.FileSize = size;
                        localWork.CanOverwrite = (action == 3 /* resume or overwrite */); // FALSE = the file was resumed
                        localWork.CanDeleteEmptyFile = FALSE;                             // the file was resumed (do not delete it even if it has zero size)
                        needCopyBack = TRUE;
                        break; // success, we are done
                    }
                }
                else // error when determining the file size, close the file and finish with an error
                {
                    HANDLES(CloseHandle(file)); // no need to delete the file because it existed a moment ago (it likely hasn't disappeared => we did not create it)
                }
            }

            if (action == 2 /* resume */ || denyOverwrite)
            {
                if (readonly)
                    SetFileAttributesW(fullName.c_str(), attr); // restore the read-only attribute (failed to open the file)

                localWork.ProblemID = ITEMPR_CANNOTCREATETGTFILE;
                localWork.WinError = winErr;
                localWork.State = DoCreateFileGetWantedErrorState(localWork);
                needCopyBack = TRUE;
                break; // resume failed, abort
            }
            // break;  // resume or overwrite - if resume fails we try overwrite as well
        }
        // case 3:  // resume or overwrite - if resume fails we try overwrite as well
        case 4: // overwrite (pri already exists, transfer failed i force action)
        {
            if (action == 4) // check whether it happens to be read-only (only via the attribute), but only if we have not done so already
            {
                attr = SalamanderGeneral->SalGetFileAttributes(fullName.c_str());
                if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
                { // try to clear the read-only attribute
                    readonly = TRUE;
                    SetFileAttributesW(fullName.c_str(), attr & (~FILE_ATTRIBUTE_READONLY));
                }
            }
            file = HANDLES_Q(CreateFileW(fullName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                        CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
            if (file == INVALID_HANDLE_VALUE) // cannot open the file
            {
                winErr = GetLastError();

                // handles the situation where a file needs to be overwritten on Samba:
                // the file has 440+different_owner and is in a directory where the current user has write access
                // (it can be deleted, but not overwritten directly (cannot be opened for writing) - we work around it:
                //  delete + create the file again)
                // (on Samba it is possible to allow deleting read-only files, which makes deleting a read-only file possible,
                //  otherwise it cannot be deleted because Windows cannot remove a read-only file and at the same time
                //  the "read-only" attribute cannot be cleared on that file because the current user is not the owner)
                if (DeleteFileW(fullName.c_str())) // if it is read-only, it can be deleted only on Samba with "delete readonly" enabled
                {
                    file = HANDLES_Q(CreateFileW(fullName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                                CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                    winErr = GetLastError();
                }
            }

            if (file != INVALID_HANDLE_VALUE) // the file is open
            {
                workDone = TRUE; // if the operation is cancelled, delete the newly created file
                localWork.OpenedFile = file;
                localWork.FileSize.Set(0, 0);
                localWork.CanOverwrite = TRUE;
                localWork.CanDeleteEmptyFile = TRUE; // the file was newly created (not resumed)
                needCopyBack = TRUE;
            }
            else
            {
                if (readonly)
                    SetFileAttributesW(fullName.c_str(), attr); // restore the read-only attribute (failed to delete the file)

                localWork.ProblemID = ITEMPR_CANNOTCREATETGTFILE;
                localWork.WinError = winErr;
                localWork.State = DoCreateFileGetWantedErrorState(localWork);
                needCopyBack = TRUE;
            }
            break;
        }
        }
    }
}

void DoCheckOrWriteToFile(CFTPDiskWork& localWork, BOOL& needCopyBack)
{
    if (localWork.WorkFile != NULL)
    {
        CQuadWord fileSize;
        fileSize.LoDWord = GetFileSize(localWork.WorkFile, &fileSize.HiDWord);
        if (fileSize.LoDWord == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
        { // error: cannot determine the file size
            localWork.State = sqisFailed;
            localWork.ProblemID = ITEMPR_TGTFILEREADERROR;
            localWork.WinError = GetLastError();
            needCopyBack = TRUE;
        }
        else
        {
            BOOL writeFile = TRUE;
            BOOL skipSetSeekForWrite = FALSE;
            int flushBufOffset = 0;
            if (localWork.CheckFromOffset < localWork.WriteOrReadFromOffset) // we are going to verify the file contents
            {
                writeFile = FALSE;
                if (localWork.CheckFromOffset >= fileSize)
                { // unexpected error: we are supposed to check file contents past the end of the file - the file probably changed recently (should not happen, it is opened "share-read-only")
                    localWork.State = sqisFailed;
                    localWork.ProblemID = ITEMPR_RESUMETESTFAILED;
                    needCopyBack = TRUE;
                }
                else // set the seek position in the file
                {
                    CQuadWord curSeek = localWork.CheckFromOffset;
                    curSeek.LoDWord = SetFilePointer(localWork.WorkFile, curSeek.LoDWord, (LONG*)&curSeek.HiDWord, FILE_BEGIN);
                    if (curSeek.LoDWord == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR ||
                        curSeek != localWork.CheckFromOffset)
                    { // error: cannot set seek in the file
                        localWork.State = sqisFailed;
                        localWork.ProblemID = ITEMPR_TGTFILEREADERROR;
                        localWork.WinError = GetLastError();
                        needCopyBack = TRUE;
                    }
                    else // compare the requested part of the file with the buffer contents
                    {
                        char buf[4096];                                                                           // buffer for reading from disk
                        int bytesToCheck = (localWork.WriteOrReadFromOffset - localWork.CheckFromOffset).LoDWord; // the size of the verified tail of the file is under 1GB, so this truncation is possible
                        if (bytesToCheck > localWork.ValidBytesInFlushDataBuffer)                                 // optionally trim by the flush buffer size (we do not need to verify the entire segment at once)
                            bytesToCheck = localWork.ValidBytesInFlushDataBuffer;
                        while (bytesToCheck > 0)
                        {
                            DWORD check = min(bytesToCheck, 4096);
                            DWORD readBytes;
                            if (!ReadFile(localWork.WorkFile, buf, check, &readBytes, NULL) ||
                                check != readBytes)
                            { // file read error
                                localWork.State = sqisFailed;
                                localWork.ProblemID = ITEMPR_TGTFILEREADERROR;
                                localWork.WinError = GetLastError();
                                needCopyBack = TRUE;
                                break;
                            }
                            else
                            {
                                if (memcmp(buf, localWork.FlushDataBuffer + flushBufOffset, check) == 0)
                                { // file and flush buffer match, everything is OK
                                    bytesToCheck -= check;
                                    flushBufOffset += check;
                                }
                                else // the file contains something different than the flush buffer (resume: the file changed since last time)
                                {
                                    localWork.State = sqisFailed;
                                    localWork.ProblemID = ITEMPR_RESUMETESTFAILED;
                                    needCopyBack = TRUE;
                                    break;
                                }
                            }
                        }
                        writeFile = (bytesToCheck == 0); // if the end-of-file check failed, there is no point in writing
                        skipSetSeekForWrite = TRUE;
                    }
                }
            }

            if (writeFile && flushBufOffset < localWork.ValidBytesInFlushDataBuffer) // we are going to write the flushed data to the file
            {
                if (localWork.WriteOrReadFromOffset > fileSize)
                { // unexpected error: we should write a chunk past the end of the file (it would be filled with random values, unacceptable) - the file probably changed recently (should not happen, it is opened "share-read-only")
                    localWork.State = sqisFailed;
                    localWork.ProblemID = ITEMPR_RESUMETESTFAILED;
                    needCopyBack = TRUE;
                }
                else // set the seek position in the file
                {
                    if (!skipSetSeekForWrite)
                    {
                        CQuadWord curSeek = localWork.WriteOrReadFromOffset;
                        curSeek.LoDWord = SetFilePointer(localWork.WorkFile, curSeek.LoDWord, (LONG*)&curSeek.HiDWord, FILE_BEGIN);
                        if (curSeek.LoDWord == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR ||
                            curSeek != localWork.WriteOrReadFromOffset)
                        { // error: cannot set seek in the file
                            localWork.State = sqisFailed;
                            localWork.ProblemID = ITEMPR_TGTFILEWRITEERROR;
                            localWork.WinError = GetLastError();
                            needCopyBack = TRUE;
                            writeFile = FALSE;
                        }
                    }

                    if (writeFile)
                    {
                        DWORD writtenBytes;
                        if (!WriteFile(localWork.WorkFile, localWork.FlushDataBuffer + flushBufOffset,
                                       localWork.ValidBytesInFlushDataBuffer - flushBufOffset, &writtenBytes, NULL) ||
                            writtenBytes != (DWORD)(localWork.ValidBytesInFlushDataBuffer - flushBufOffset))
                        {
                            localWork.State = sqisFailed;
                            localWork.ProblemID = ITEMPR_TGTFILEWRITEERROR;
                            localWork.WinError = GetLastError();
                            needCopyBack = TRUE;
                        }
                        else // successfully written, we are successfully done
                        {
                            if (localWork.WriteOrReadFromOffset + CQuadWord(writtenBytes, 0) < fileSize)
                            {                                     // if the write finished before the end of the file, call SetEndOfFile (trim unwanted old data that should be overwritten)
                                SetEndOfFile(localWork.WorkFile); // we do not test success; it does not really matter
                            }
                        }
                    }
                }
            }
        }
    }
    else // report an error
    {
        TRACE_E("DoCheckOrWriteToFile(): localWork.WorkFile may not be NULL!");
        localWork.State = sqisFailed;
        localWork.ProblemID = ITEMPR_LOWMEM;
        needCopyBack = TRUE;
    }
}

void DoListDirectory(CFTPDiskWork& localWork, BOOL& needCopyBack)
{
    std::wstring srcPath;
    if (BuildWideDiskChildPath(srcPath, localWork.Path, localWork.Name))
    {
        localWork.DiskListing = new TIndirectArray<CDiskListingItem>(100, 500);
        if (localWork.DiskListing != NULL && localWork.DiskListing->IsGood())
        {
            SPLSalPathAppendOwned(srcPath, L"*.*");
            const std::size_t srcPathEnd = srcPath.find_last_of(L'\\'); // cannot fail
            WIN32_FIND_DATAW fileData;
            HANDLE search = HANDLES_Q(FindFirstFileW(srcPath.c_str(), &fileData));
            if (search == INVALID_HANDLE_VALUE)
            {
                DWORD err = GetLastError();
                if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES)
                {
                    localWork.State = sqisFailed;
                    localWork.ProblemID = ITEMPR_UPLOADCANNOTLISTSRCPATH;
                    localWork.WinError = err;
                }
            }
            else
            {
                do
                {
                    const wchar_t* s = fileData.cFileName;
                    if (*s == L'.' && (*(s + 1) == 0 || *(s + 1) == L'.' && *(s + 2) == 0))
                        continue;
                    BOOL isDir = (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    CQuadWord size(fileData.nFileSizeLow, fileData.nFileSizeHigh);
                    if (!isDir && (fileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
                        srcPathEnd != std::wstring::npos)
                    {
                        const std::wstring linkPath =
                            srcPath.substr(0, srcPathEnd + 1) + fileData.cFileName;
                        DWORD err;
                        if (!SalamanderGeneral->SalGetFileSize2(linkPath.c_str(), size, &err))
                        {
                            TRACE_E("DoListDirectory(): unable to get link target file size, error: " << err);
                            size.Set(fileData.nFileSizeLow, fileData.nFileSizeHigh);
                        }
                    }
                    CDiskListingItem* item = new CDiskListingItem(fileData.cFileName, isDir, size);
                    if (item != NULL && item->IsGood())
                    {
                        localWork.DiskListing->Add(item);
                        if (!localWork.DiskListing->IsGood())
                        {
                            delete item;
                            localWork.DiskListing->ResetState();
                            localWork.State = sqisFailed;
                            localWork.ProblemID = ITEMPR_LOWMEM;
                            break;
                        }
                    }
                    else
                    {
                        if (item != NULL)
                            delete item;
                        else
                            TRACE_E(LOW_MEMORY);
                        localWork.State = sqisFailed;
                        localWork.ProblemID = ITEMPR_LOWMEM;
                        break;
                    }
                } while (FindNextFileW(search, &fileData));
                DWORD err = GetLastError();
                HANDLES(FindClose(search));
                if (localWork.State != sqisFailed && err != ERROR_NO_MORE_FILES)
                {
                    localWork.State = sqisFailed;
                    localWork.ProblemID = ITEMPR_UPLOADCANNOTLISTSRCPATH;
                    localWork.WinError = err;
                }
            }
        }
        else
        {
            TRACE_E(LOW_MEMORY);
            localWork.State = sqisFailed;
            localWork.ProblemID = ITEMPR_LOWMEM;
        }
    }
    else
    {
        localWork.State = sqisFailed;
        localWork.ProblemID = ITEMPR_INVALIDPATHTODIR;
    }
    if (localWork.State == sqisFailed && localWork.DiskListing != NULL)
    {
        delete localWork.DiskListing;
        localWork.DiskListing = NULL;
    }
    needCopyBack = TRUE;
}

void DoDeleteDir(CFTPDiskWork& localWork, BOOL& needCopyBack)
{
    std::wstring delPath;
    if (BuildWideDiskChildPath(delPath, localWork.Path, localWork.Name))
    {
        DWORD attr = SalamanderGeneral->SalGetFileAttributes(delPath.c_str());
        BOOL chAttrs = SalamanderGeneral->ClearReadOnlyAttr(delPath.c_str(), attr); // so it can be deleted ...
        if (!RemoveDirectoryW(delPath.c_str()))
        {
            DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
            { // if the directory no longer exists, everything is OK, otherwise print an error:
                if (chAttrs)
                    SetFileAttributesW(delPath.c_str(), attr); // deletion failed, so at least try to restore its attributes
                localWork.State = sqisFailed;
                localWork.ProblemID = ITEMPR_UNABLETODELETEDISKDIR;
                localWork.WinError = err;
                needCopyBack = TRUE; // return the error
            }
        }
    }
    else // the path on disk is too long
    {
        localWork.State = sqisFailed;
        localWork.ProblemID = ITEMPR_INVALIDPATHTODIR;
        needCopyBack = TRUE; // return the error
    }
}

void DoDeleteFile(CFTPDiskWork& localWork, BOOL& needCopyBack)
{
    std::wstring delPath;
    if (BuildWideDiskChildPath(delPath, localWork.Path, localWork.Name))
    {
        DWORD attr = SalamanderGeneral->SalGetFileAttributes(delPath.c_str());
        BOOL chAttrs = SalamanderGeneral->ClearReadOnlyAttr(delPath.c_str(), attr); // so it can be deleted ...
        if (!DeleteFileW(delPath.c_str()))
        {
            DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND)
            { // if the file no longer exists, everything is OK, otherwise print an error:
                if (chAttrs)
                    SetFileAttributesW(delPath.c_str(), attr); // deletion failed, so at least try to restore its attributes
                localWork.State = sqisFailed;
                localWork.ProblemID = ITEMPR_UNABLETODELETEDISKFILE;
                localWork.WinError = err;
                needCopyBack = TRUE; // return the error
            }
        }
    }
    else // the path on disk is too long (should never happen - the file has just been opened and read)
    {
        localWork.State = sqisFailed;
        localWork.ProblemID = ITEMPR_UNABLETODELETEDISKFILE;
        localWork.WinError = ERROR_FILENAME_EXCED_RANGE; // "file name is too long"
        needCopyBack = TRUE;                             // return the error
    }
}

void DoOpenFileForReading(CFTPDiskWork& localWork, BOOL& needCopyBack)
{
    std::wstring fileName;
    DWORD winError = NO_ERROR;
    BOOL ok = FALSE;
    if (BuildWideDiskChildPath(fileName, localWork.Path, localWork.Name))
    {
        HANDLE in = HANDLES_Q(CreateFileW(fileName.c_str(), GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                         OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
        if (in != INVALID_HANDLE_VALUE)
        {
            CQuadWord size;
            size.LoDWord = GetFileSize(in, &size.HiDWord);
            if (size.LoDWord == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
            {
                winError = GetLastError();
                HANDLES(CloseHandle(in));
            }
            else
            {
                ok = TRUE;
                localWork.OpenedFile = in;
                localWork.FileSize = size;
            }
        }
        else
            winError = GetLastError();
    }
    else
        winError = ERROR_FILENAME_EXCED_RANGE; // "file name is too long"
    if (!ok)
    {
        localWork.State = sqisFailed;
        localWork.ProblemID = ITEMPR_UPLOADCANNOTOPENSRCFILE;
        localWork.WinError = winError;
    }
    needCopyBack = TRUE; // return the error or the handle + file info
}

void DoReadFile(CFTPDiskWork& localWork, BOOL& needCopyBack, BOOL isASCIITrMode)
{
    localWork.ValidBytesInFlushDataBuffer = 0;
    localWork.EOLsInFlushDataBuffer = 0;
    if (localWork.WorkFile != NULL)
    {
        CQuadWord curSeek = localWork.WriteOrReadFromOffset;
        curSeek.LoDWord = SetFilePointer(localWork.WorkFile, curSeek.LoDWord, (LONG*)&curSeek.HiDWord, FILE_BEGIN);
        if (curSeek.LoDWord == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR ||
            curSeek != localWork.WriteOrReadFromOffset)
        { // error: cannot set seek in the file
            localWork.State = sqisFailed;
            localWork.ProblemID = ITEMPR_SRCFILEREADERROR;
            localWork.WinError = GetLastError();
        }
        else // compare the requested part of the file with the buffer contents
        {
            DWORD read;
            if (isASCIITrMode) // text transfer mode = CRLF for all EOLs, we will convert
            {
                char buf[DATACON_UPLOADFLUSHBUFFERSIZE]; // buffer for reading from disk
                if (ReadFile(localWork.WorkFile, buf, DATACON_UPLOADFLUSHBUFFERSIZE, &read, NULL))
                {
                    char* textBuf = localWork.FlushDataBuffer;
                    char* textBufEndMinOne = textBuf + DATACON_UPLOADFLUSHBUFFERSIZE - 1;
                    char* s = buf;
                    char* end = s + read;
                    while (s < end && textBuf < textBufEndMinOne)
                    {
                        if (*s == '\n') // LF
                        {
                            *textBuf++ = '\r';
                            *textBuf++ = '\n';
                            localWork.EOLsInFlushDataBuffer++;
                        }
                        else
                        {
                            if (*s == '\r')
                            {
                                if (s + 1 < end)
                                {
                                    if (*(s + 1) == '\n') // CRLF
                                    {
                                        s++;
                                        *textBuf++ = '\r';
                                        *textBuf++ = '\n';
                                        localWork.EOLsInFlushDataBuffer++;
                                    }
                                    else // CR
                                    {
                                        *textBuf++ = '\r';
                                        *textBuf++ = '\n';
                                        localWork.EOLsInFlushDataBuffer++;
                                    }
                                }
                                else // CR + EOF
                                {
                                    *textBuf++ = '\r';
                                    *textBuf++ = '\n';
                                    localWork.EOLsInFlushDataBuffer++;
                                }
                            }
                            else
                                *textBuf++ = *s; // normal character, just copy it
                        }
                        s++;
                    }
                    localWork.ValidBytesInFlushDataBuffer = (int)(textBuf - localWork.FlushDataBuffer);
                    localWork.WriteOrReadFromOffset += CQuadWord((DWORD)(s - buf), 0);
                }
                else // read error
                {
                    localWork.State = sqisFailed;
                    localWork.ProblemID = ITEMPR_SRCFILEREADERROR;
                    localWork.WinError = GetLastError();
                }
            }
            else
            {
                if (ReadFile(localWork.WorkFile, localWork.FlushDataBuffer, DATACON_UPLOADFLUSHBUFFERSIZE, &read, NULL))
                {
                    localWork.ValidBytesInFlushDataBuffer = read;
                    localWork.WriteOrReadFromOffset += CQuadWord(read, 0);
                }
                else // read error
                {
                    localWork.State = sqisFailed;
                    localWork.ProblemID = ITEMPR_SRCFILEREADERROR;
                    localWork.WinError = GetLastError();
                }
            }
        }
    }
    else // report an error
    {
        TRACE_E("DoReadFile(): localWork.WorkFile may not be NULL!");
        localWork.State = sqisFailed;
        localWork.ProblemID = ITEMPR_LOWMEM;
    }
    needCopyBack = TRUE;
}

unsigned
CFTPDiskThread::Body()
{
    CALL_STACK_MESSAGE1("CFTPDiskThread::Body()");

    CFTPDiskWork localWork;
    localWork.NewTgtName = NULL;
    localWork.OpenedFile = NULL;
    localWork.DiskListing = NULL;
    std::string errorText;
    while (1)
    {
        // check if there is any work or if the thread should terminate
        HANDLES(EnterCriticalSection(&DiskCritSect));
        BOOL wait = !ShouldTerminate && Work.Count == 0 && FilesToClose.Count == 0;
        if (wait)
            ResetEvent(ContEvent);
        HANDLES(LeaveCriticalSection(&DiskCritSect));

        if (wait) // wait if there is no work and the thread is not supposed to terminate
        {
            CALL_STACK_MESSAGE1("CFTPDiskThread::Body(): waiting...");
            WaitForSingleObject(ContEvent, INFINITE);
        }

        // pick up work or detect the thread termination request
        HANDLES(EnterCriticalSection(&DiskCritSect));
        BOOL endThread = ShouldTerminate;
        CFTPFileToClose* fileToClose = NULL;
        CFTPDiskWork* work = NULL;
        BOOL snapshotFailed = FALSE;
        if (FilesToClose.Count > 0) // closing files has the highest priority, then thread termination, and finally regular work
        {
            fileToClose = FilesToClose[0];
            FilesToClose.Detach(0);
            endThread = FALSE;
        }
        else
        {
            if (!endThread && Work.Count > 0)
            {
                work = Work[0];
                if (work != NULL)
                {
                    if (!localWork.CopyFrom(*work))
                    {
                        localWork.Reset();
                        localWork.SocketMsg = work->SocketMsg;
                        localWork.SocketUID = work->SocketUID;
                        localWork.MsgID = work->MsgID;
                        localWork.ProblemID = ITEMPR_LOWMEM;
                        localWork.State = sqisFailed;
                        snapshotFailed = TRUE;
                    }
                    WorkIsInProgress = TRUE;
                }
            }
        }
        HANDLES(LeaveCriticalSection(&DiskCritSect));
        if (endThread)
            break; // terminate the thread

        if (fileToClose != NULL) // close the file and optionally delete an empty file
        {
            CQuadWord size;
            BOOL delFile = FALSE;
            if (fileToClose->AlwaysDeleteFile)
                delFile = TRUE;
            else
            {
                if (fileToClose->DeleteIfEmpty)
                {
                    size.LoDWord = GetFileSize(fileToClose->File, &size.HiDWord);
                    delFile = (size == CQuadWord(0, 0)); // if GetFileSize fails the file is not deleted
                }
            }
            if (!delFile && fileToClose->SetDateAndTime &&
                (fileToClose->Date.Day != 0 || fileToClose->Time.Hour != 24)) // only if at least something will be set (otherwise the following block makes no sense)
            {
                SYSTEMTIME st;
                FILETIME ft, ft2;
                if ((fileToClose->Date.Day == 0 || fileToClose->Time.Hour == 24) && // the date or time are "empty values" (we must obtain them from the file)
                    (!GetFileTime(fileToClose->File, NULL, NULL, &ft) ||
                     !FileTimeToLocalFileTime(&ft, &ft2) ||
                     !FileTimeToSystemTime(&ft2, &st)))
                {
                    GetLocalTime(&st); // cannot read date&time from the file, so take the current time at least (we have to fill the "empty values" somehow)
                }
                if (fileToClose->Date.Day != 0) // if the date is not an "empty value"
                {
                    st.wYear = fileToClose->Date.Year;
                    st.wMonth = fileToClose->Date.Month;
                    st.wDayOfWeek = 0;
                    st.wDay = fileToClose->Date.Day;
                }
                if (fileToClose->Time.Hour != 24) // if the time is not an "empty value"
                {
                    st.wHour = fileToClose->Time.Hour;
                    st.wMinute = fileToClose->Time.Minute;
                    st.wSecond = fileToClose->Time.Second;
                    st.wMilliseconds = fileToClose->Time.Millisecond;
                }
                if (!SystemTimeToFileTime(&st, &ft2) ||
                    !LocalFileTimeToFileTime(&ft2, &ft))
                {
                    DWORD err = GetLastError();
                    FTPGetErrorText(err, errorText);
                    TRACE_E("CFTPDiskThread::Body(): SystemTimeToFileTime() or LocalFileTimeToFileTime() failed: " << errorText.c_str());
                }
                else
                {
                    if (!SetFileTime(fileToClose->File, NULL, NULL, &ft))
                    {
                        DWORD err = GetLastError();
                        FTPGetErrorText(err, errorText);
                        TRACE_E("CFTPDiskThread::Body(): SetFileTime() failed: " << errorText.c_str());
                    }
                }
            }
            if (!delFile && fileToClose->EndOfFile != CQuadWord(-1, -1))
            {
                size.LoDWord = GetFileSize(fileToClose->File, &size.HiDWord);
                if (size.LoDWord != INVALID_FILE_SIZE || GetLastError() == NO_ERROR)
                {
                    if (fileToClose->EndOfFile <= size)
                    {
                        CQuadWord curSeek = fileToClose->EndOfFile;
                        curSeek.LoDWord = SetFilePointer(fileToClose->File, curSeek.LoDWord, (LONG*)&curSeek.HiDWord, FILE_BEGIN);
                        if ((curSeek.LoDWord != INVALID_SET_FILE_POINTER || GetLastError() == NO_ERROR) &&
                            curSeek == fileToClose->EndOfFile)
                        {
                            if (SetEndOfFile(fileToClose->File) == 0)
                            {
                                DWORD err = GetLastError();
                                FTPGetErrorText(err, errorText);
                                TRACE_E("CFTPDiskThread::Body(): SetEndOfFile failed: " << errorText.c_str());
                            }
                        }
                        else
                        {
                            DWORD err = GetLastError();
                            FTPGetErrorText(err, errorText);
                            TRACE_E("CFTPDiskThread::Body(): SetFilePointer failed: " << errorText.c_str());
                        }
                    }
                    else
                        TRACE_E("CFTPDiskThread::Body(): fileToClose->EndOfFile > size!");
                }
                else
                {
                    DWORD err = GetLastError();
                    FTPGetErrorText(err, errorText);
                    TRACE_E("CFTPDiskThread::Body(): GetFileSize failed: " << errorText.c_str());
                }
            }
            HANDLES(CloseHandle(fileToClose->File));
            if (delFile)
                DeleteFileW(fileToClose->FileName.c_str());
            delete fileToClose;

            HANDLES(EnterCriticalSection(&DiskCritSect));
            DoneFileCloseIndex++; // from -1 (none closed yet) go to zero, then increment by one
            HANDLES(LeaveCriticalSection(&DiskCritSect));
            if (FileClosedEvent != NULL)
                PulseEvent(FileClosedEvent);
        }
        else
        {
            // perform the requested work
            BOOL needCopyBack = snapshotFailed;
            BOOL workDone = FALSE;
            if (work != NULL && !snapshotFailed) // ATTENTION: the 'work' object must not be accessed; it may no longer exist (only the pointer value may be tested, not the memory it points to)
            {
                try
                {
                    switch (localWork.Type)
                    {
                    case fdwtCreateDir:
                    {
                        DoCreateDir(localWork, workDone, needCopyBack);
                        break;
                    }

                    case fdwtCreateFile:
                    case fdwtRetryCreatedFile:
                    case fdwtRetryResumedFile:
                    {
                        DoCreateFile(localWork, workDone, needCopyBack);
                        break;
                    }

                    case fdwtCheckOrWriteFile:
                    {
                        DoCheckOrWriteToFile(localWork, needCopyBack);
                        break;
                    }

                    case fdwtCreateAndWriteFile:
                    {
                        FTPExecuteCreateAndWriteFileDiskWork(localWork, needCopyBack, workDone);
                        break;
                    }

                    case fdwtListDir:
                    {
                        DoListDirectory(localWork, needCopyBack);
                        break;
                    }

                    case fdwtDeleteDir:
                    {
                        DoDeleteDir(localWork, needCopyBack);
                        break;
                    }

                    case fdwtOpenFileForReading:
                    {
                        DoOpenFileForReading(localWork, needCopyBack);
                        break;
                    }

                    case fdwtReadFile:
                    case fdwtReadFileInASCII:
                    {
                        DoReadFile(localWork, needCopyBack, localWork.Type == fdwtReadFileInASCII);
                        break;
                    }

                    case fdwtDeleteFile:
                    {
                        DoDeleteFile(localWork, needCopyBack);
                        break;
                    }

                    default:
                        TRACE_E("CFTPDiskThread::Body(): unknown type of work: " << localWork.Type);
                        break;
                    }
                }
                catch (...)
                {
                    localWork.ProblemID = ITEMPR_LOWMEM;
                    localWork.WinError = NO_ERROR;
                    localWork.State = sqisFailed;
                    needCopyBack = TRUE;
                }
            }

            // determine whether the work needs to be cancelled
            HANDLES(EnterCriticalSection(&DiskCritSect));
            BOOL doCancel = FALSE;
            if (work != NULL && Work.Count > 0 && work == Work[0]) // the work being processed was not cancelled
            {
                if (needCopyBack)
                {
                    if (snapshotFailed)
                    {
                        work->ProblemID = ITEMPR_LOWMEM;
                        work->State = sqisFailed;
                    }
                    else
                        work->MoveFrom(localWork); // take over the work results without allocating
                }
            }
            else
                doCancel = work != NULL;
            if (Work.Count > 0) // remove the processed item or NULL (if it was cancelled)
            {
                Work.Detach(0);
                if (!Work.IsGood())
                    Work.ResetState();
            }
            WorkIsInProgress = FALSE;
            HANDLES(LeaveCriticalSection(&DiskCritSect));

            if (work != NULL)
            {
                if (localWork.NewTgtName != NULL) // on cancel deallocate the allocated new directory name
                {
                    free(localWork.NewTgtName);
                    localWork.NewTgtName = NULL;
                }
                if (localWork.OpenedFile != NULL) // on cancel close the opened file handle
                {
                    HANDLES(CloseHandle(localWork.OpenedFile));
                    localWork.OpenedFile = NULL;
                }
                if (localWork.DiskListing != NULL) // on cancel deallocate the allocated listing
                {
                    delete localWork.DiskListing;
                    localWork.DiskListing = NULL;
                }
                if (!doCancel) // inform the worker about the work results
                {
                    SocketsThread->PostSocketMessage(localWork.SocketMsg, localWork.SocketUID, localWork.MsgID, work);
                }
                else // the work was cancelled, perform cleanup (as if the work never happened)
                {
                    switch (localWork.Type)
                    {
                    case fdwtCreateDir:
                    {
                        if (workDone)
                        {
                            std::wstring createdPath;
                            if (!BuildWideDiskChildPath(createdPath, localWork.Path, localWork.Name) ||
                                !RemoveDirectoryW(createdPath.c_str()))
                                TRACE_E("CFTPDiskThread::Body(): cancelling disk operation: unable to remove directory");
                        }
                        break;
                    }

                    case fdwtCreateFile:
                    case fdwtRetryCreatedFile:
                    case fdwtRetryResumedFile:
                    {
                        if (workDone)
                        {
                            std::wstring createdPath;
                            if (!BuildWideDiskChildPath(createdPath, localWork.Path, localWork.Name) ||
                                !DeleteFileW(createdPath.c_str())) // the created file cannot have the read-only attribute; otherwise it could not be opened for writing
                                TRACE_E("CFTPDiskThread::Body(): cancelling disk operation: unable to remove file");
                        }
                        break;
                    }

                    case fdwtCreateAndWriteFile:
                    {
                        if (workDone)
                        {
                            if (!DeleteFileW(localWork.DirectFileName.c_str())) // the created file cannot have the read-only attribute
                                TRACE_E("CFTPDiskThread::Body(): cancelling disk operation: unable to remove target file");
                        }
                        // break; // intentionally no break here!
                    }
                    case fdwtCheckOrWriteFile:
                    {
                        if (localWork.FlushDataBuffer != NULL) // the buffer could not be released from the worker because we were using it, so release it now
                        {
                            free(localWork.FlushDataBuffer);
                            localWork.FlushDataBuffer = NULL;
                        }
                        break;
                    }

                    case fdwtListDir:
                        break; // nothing to do when cancelling directory listing
                    case fdwtDeleteDir:
                        break; // nothing to do when cancelling directory deletion
                    case fdwtOpenFileForReading:
                        break; // nothing to do when cancelling opening a file for reading

                    case fdwtReadFile:
                    case fdwtReadFileInASCII:
                    {
                        if (localWork.FlushDataBuffer != NULL) // the buffer could not be released from the worker because we were using it, so release it now
                        {
                            free(localWork.FlushDataBuffer);
                            localWork.FlushDataBuffer = NULL;
                        }
                        break;
                    }

                    case fdwtDeleteFile:
                        break; // nothing to do when cancelling file deletion

                    default:
                        TRACE_E("CFTPDiskThread::Body(), cancel: unknown type of work: " << localWork.Type);
                        break;
                    }
                }
            }
        }
    }
    return 0;
}
