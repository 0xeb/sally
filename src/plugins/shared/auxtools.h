// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

//
// ****************************************************************************
// CThreadQueue
//

struct CThreadQueueItem
{
    HANDLE Thread;
    DWORD ThreadID; // only for debugging purposes (finding the thread in the thread list in the debugger)
    int Locks;      // lock count; if > 0 we must not close 'Thread'
    CThreadQueueItem* Next;

    CThreadQueueItem(HANDLE thread, DWORD tid)
    {
        Thread = thread;
        ThreadID = tid;
        Next = NULL;
        Locks = 0;
    }
};

class CThreadQueue
{
protected:
    const char* QueueName; // queue name (for debugging purposes only)
    CThreadQueueItem* Head;
    HANDLE Continue; // we must wait for data handoff to the started thread

    struct CCS // access from multiple threads -> synchronization required
    {
        CRITICAL_SECTION cs;

        CCS() { InitializeCriticalSection(&cs); }
        ~CCS() { DeleteCriticalSection(&cs); }

        void Enter() { EnterCriticalSection(&cs); }
        void Leave() { LeaveCriticalSection(&cs); }
    } CS;

public:
    CThreadQueue(const char* queueName /* napr. "DemoPlug Viewers" */);
    ~CThreadQueue();

    // starts function 'body' with parameter 'param' in a newly created thread with a stack
    // of size 'stack_size' (0 = default); returns thread handle or NULL on error,
    // also writes the result before starting the thread (resume) into 'threadHandle'
    // (if not NULL); use the returned thread handle only for NULL tests and for calling
    // CThreadQueue methods WaitForExit() and KillThread(); the thread handle is closed by
    // this queue object
    // WARNING: -the thread may start with a delay until after StartThread() returns
    //         (if 'param' is a pointer to a structure stored on the stack, it is necessary
    //          to synchronize handing off the data from 'param' - the main thread must wait
    //          for the new thread to take the data)
    //        -the returned thread handle may already be closed if the thread finishes before
    //         returning from StartThread() and StartThread() or KillAll() is called from another thread
    // can be called from any thread
    HANDLE StartThread(unsigned(WINAPI* body)(void*), void* param, unsigned stack_size = 0,
                       HANDLE* threadHandle = NULL, DWORD* threadID = NULL);

    // waits for a thread from this queue to finish; 'thread' is a thread handle that may already
    // be closed (this object closes it when StartThread and KillAll are called); if it
    // waits until the thread finishes, it removes the thread from the queue and closes its handle
    BOOL WaitForExit(HANDLE thread, int milliseconds = INFINITE);

    // kills a thread from this queue (via TerminateThread()); 'thread' is a thread handle
    // that may already be closed (this object closes it when StartThread and KillAll are called);
    // if it finds the thread, it kills it, removes it from the queue, and closes its handle (the thread object
    // is not deallocated because its state is unknown, possibly inconsistent)
    void KillThread(HANDLE thread, DWORD exitCode = 666);

    // verifies that all threads finished; if 'force' is TRUE and a thread is still running,
    // waits 'forceWaitTime' (ms) for all threads to finish, then kills the running threads
    // (their objects are not deallocated because their state is unknown, possibly inconsistent);
    // returns TRUE if all threads have finished; with 'force' TRUE it always returns TRUE;
    // if 'force' is FALSE and a thread is still running, waits 'waitTime' (ms) for all threads to finish,
    // if something is still running afterwards, returns FALSE; INFINITE means unlimited
    // wait time
    // can be called from any thread
    BOOL KillAll(BOOL force, int waitTime = 1000, int forceWaitTime = 200, DWORD exitCode = 666);

protected:                                                 // internal unsynchronized methods
    BOOL Add(CThreadQueueItem* item);                      // adds an item to the queue, returns success
    BOOL FindAndLockItem(HANDLE thread);                   // finds the item for 'thread' in the queue and locks it
    void UnlockItem(HANDLE thread, BOOL deleteIfUnlocked); // unlocks the item for 'thread' in the queue, optionally deletes it
    void ClearFinishedThreads();                           // removes threads that already finished from the queue
    static DWORD WINAPI ThreadBase(void* param);           // universal thread body
};

//
// ****************************************************************************
// CThread
//
// WARNING: must be allocated (CThread cannot be only on the stack); it deallocates itself
//         only in case of successful thread creation via Create()

class CThread
{
public:
    // thread handle (NULL = thread not running/has not run), WARNING: after the thread ends it
    // closes itself (is invalid); in addition this object is already deallocated
    HANDLE Thread;

protected:
    char Name[101]; // buffer for the thread name (used in TRACE and CALL-STACK to identify the thread)
                    // WARNING: if thread data contains references to the stack or other temporary objects,
                    //          it is necessary to ensure those references are used only while they are valid

public:
    CThread(const char* name = NULL);
    virtual ~CThread() {} // aby se spravne volaly destruktory potomku

    // creation (start) of a thread in the 'queue' thread queue; 'stack_size' is the stack size
    // of the new thread in bytes (0 = default); returns the new thread handle or NULL on error;
    // closing the handle is handled by the 'queue' object; if the thread is created successfully, this object
    // is deallocated when the thread ends; if creation fails, deallocation is on the caller
    // WARNING: without additional synchronization the thread may finish before Create() returns ->
    //          therefore the "this" pointer must be considered invalid after a successful Create() call,
    //          the same applies to the returned thread handle (use only for NULL tests and for calling
    //          CThreadQueue methods WaitForExit() and KillThread())
    // can be called from any thread
    HANDLE Create(CThreadQueue& queue, unsigned stack_size = 0, DWORD* threadID = NULL);

    // returns 'Thread' see above
    HANDLE GetHandle() { return Thread; }

    // returns thread name
    const char* GetName() { return Name; }

    // this method contains the thread body
    virtual unsigned Body() = 0;

protected:
    static unsigned WINAPI UniversalBody(void* param); // helper method for starting the thread
};
