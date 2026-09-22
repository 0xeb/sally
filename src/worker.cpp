// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "worker.h"
#include "common/CreateDirectoryFlow.h"
#include "common/fsutil.h"
#include "common/CopyStrategy.h"
#include "common/widepath.h"
#include "common/IFileSystem.h"
#include "common/IShell.h"
#include "common/IWorkerObserver.h"
#include "common/WorkerDirectHeadless.h"
#include "DialogWorkerObserver.h"
#include "common/unicode/helpers.h"
#include "common/unicode/PathIdentityPolicy.h"
#include "common/unicode/PanelPathPolicy.h"

#include <aclapi.h>
#include <ntsecapi.h>

// these functions have no header, we must load them dynamically
NTFSCONTROLFILE DynNtFsControlFile = NULL;

COperationsQueue OperationsQueue; // queue of disk operations

// The local shim that used to live here narrowed its own wide argument and
// called the ANSI IsLantasticDrive - a *W function that threw away the W. Its comment
// claimed "LANTASTIC detection needs the ANSI network APIs"; it does not. WNetOpenEnumW /
// WNetEnumResourceW / WNetGetNetworkInformationW all exist, and the enumeration is the
// authority the path is compared against, so narrowing it corrupted both sides at once.
// The refusal it fell back to (TryWideToAnsiRoundTripExact) was safe but wrong: it answered
// "not LANTASTIC" for every share the code page cannot spell. IsLantasticDriveW in
// sally_path_validation.cpp is now the real implementation.

static IFileSystem* GetWorkerFileSystem()
{
    IFileSystem* fileSystem = gFileSystem;
    if (fileSystem == NULL)
        fileSystem = GetWin32FileSystem();
    return fileSystem;
}

// if defined, various debug messages are written to TRACE
//#define WORKER_COPY_DEBUG_MSG

// comment out when we no longer want to monitor all the messages from the asynchronous copy algorithm
//#define ASYNC_COPY_DEBUG_MSG

//
// ****************************************************************************
// CTransferSpeedMeter
//

void CTransferSpeedMeter::GetSpeed(CQuadWord* speed)
{
    CALL_STACK_MESSAGE1("CTransferSpeedMeter::GetSpeed()");

    DWORD time = GetTickCount();

    if (CountOfLastPackets >= 2)
    { // test whether this is a low speed (calculated from LastPacketsSize and LastPacketsTime)
        int firstPacket = ((TRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - CountOfLastPackets) % (TRSPMETER_NUMOFSTOREDPACKETS + 1);
        int lastPacket = ((TRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - 1) % (TRSPMETER_NUMOFSTOREDPACKETS + 1);
        DWORD lastPacketTime = LastPacketsTime[lastPacket];
        DWORD totalTime = lastPacketTime - LastPacketsTime[firstPacket]; // time between receiving the first and last packet
        if (totalTime >= ((DWORD)(CountOfLastPackets - 1) * TRSPMETER_STPCKTSMININTERVAL) / TRSPMETER_NUMOFSTOREDPACKETS)
        {                                     // this is a low speed (up to TRSPMETER_NUMOFSTOREDPACKETS packets per TRSPMETER_STPCKTSMININTERVAL ms)
            if (time - lastPacketTime > 2000) // two-second "protection" period for the last computed slow speed
            {                                 // check whether the speed has dropped by more than double compared to the speed of the last packet; if so, display
                                              // zero speed (so that when a slow transfer stops we do not keep showing the last recorded speed value)
                int preLastPacket = ((TRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - 2) % (TRSPMETER_NUMOFSTOREDPACKETS + 1);
                if ((UINT64)2 * MaxPacketSize * (lastPacketTime - LastPacketsTime[preLastPacket]) < (UINT64)LastPacketsSize[lastPacket] * (time - lastPacketTime))
                {
                    speed->SetUI64(0);
                    ResetSpeed = TRUE;
                    return; // speed dropped at least two times, better show zero
                }
            }
            if (totalTime > TRSPMETER_ACTSPEEDSTEP * TRSPMETER_ACTSPEEDNUMOFSTEPS)
            { // compute the speed only from data closest to TRSPMETER_ACTSPEEDSTEP * TRSPMETER_ACTSPEEDNUMOFSTEPS
                // (if packets arrive slowly, the queue may contain packets from the last five minutes - but here we
                // compute the "instant" speed, not the average over the last five minutes)
                int i = firstPacket;
                while (1)
                {
                    if (++i >= TRSPMETER_NUMOFSTOREDPACKETS + 1)
                        i = 0;
                    if (i == lastPacket || lastPacketTime - LastPacketsTime[i] < TRSPMETER_ACTSPEEDSTEP * TRSPMETER_ACTSPEEDNUMOFSTEPS)
                        break;
                    firstPacket = i;
                }
                totalTime = lastPacketTime - LastPacketsTime[firstPacket];
            }
            UINT64 totalSize = 0; // sum of all packet sizes except the first one (from whitch we use only the time)
            do
            {
                if (++firstPacket >= TRSPMETER_NUMOFSTOREDPACKETS + 1)
                    firstPacket = 0;
                totalSize += LastPacketsSize[firstPacket];
            } while (firstPacket != lastPacket);
            speed->SetUI64((1000 * totalSize) / totalTime);
            return; // low speed computed, we are done
        }
        else // this is a high speed (more than TRSPMETER_NUMOFSTOREDPACKETS packets per TRSPMETER_STPCKTSMININTERVAL ms),
        {    // perform a sudden speed drop test (especially when copying zero-sized files or creating empty directories begins)
            if (time - lastPacketTime > 800)
            { // if no packet has arrived for 800 ms, report zero speed
                speed->SetUI64(0);
                ResetSpeed = TRUE;
                return;
            }
        }
    }
    else // nothing to calculate from yet, report "0 B/s"
    {
        speed->SetUI64(0);
        return;
    }
    // high speed (more than TRSPMETER_NUMOFSTOREDPACKETS packets per TRSPMETER_STPCKTSMININTERVAL ms)
    if (CountOfTrBytesItems > 0) // after the connection is established this is "always true"
    {
        int actIndexAdded = 0;                           // 0 = current index not included, 1 = current index included
        int emptyTrBytes = 0;                            // number of counted empty steps
        UINT64 total = 0;                                // total number of bytes over the last at most TRSPMETER_ACTSPEEDNUMOFSTEPS steps
        int addFromTrBytes = CountOfTrBytesItems - 1;    // number of closed steps to add from the queue
        DWORD restTime = 0;                              // time from the last counted step to now
        if ((int)(time - ActIndexInTrBytesTimeLim) >= 0) // current index already closed + empty steps may be needed
        {
            emptyTrBytes = (time - ActIndexInTrBytesTimeLim) / TRSPMETER_ACTSPEEDSTEP;
            restTime = (time - ActIndexInTrBytesTimeLim) % TRSPMETER_ACTSPEEDSTEP;
            emptyTrBytes = min(emptyTrBytes, TRSPMETER_ACTSPEEDNUMOFSTEPS);
            if (emptyTrBytes < TRSPMETER_ACTSPEEDNUMOFSTEPS) // empty steps are not enough; include the current index as well
            {
                total = TransferedBytes[ActIndexInTrBytes];
                actIndexAdded = 1;
            }
            addFromTrBytes = TRSPMETER_ACTSPEEDNUMOFSTEPS - actIndexAdded - emptyTrBytes;
            addFromTrBytes = min(addFromTrBytes, CountOfTrBytesItems - 1); // how many closed steps from the queue to include
        }
        else
        {
            restTime = time + TRSPMETER_ACTSPEEDSTEP - ActIndexInTrBytesTimeLim;
            total = TransferedBytes[ActIndexInTrBytes];
        }

        int actIndex = ActIndexInTrBytes;
        int i;
        for (i = 0; i < addFromTrBytes; i++)
        {
            if (--actIndex < 0)
                actIndex = TRSPMETER_ACTSPEEDNUMOFSTEPS; // moving along the circular queue
            total += TransferedBytes[actIndex];
        }
        DWORD t = (addFromTrBytes + actIndexAdded + emptyTrBytes) * TRSPMETER_ACTSPEEDSTEP + restTime;
        if (t > 0)
            speed->SetUI64((total * 1000) / t);
        else
            speed->SetUI64(0); // nothing to calculate from yet, report "0 B/s"
    }
    else
        speed->SetUI64(0); // nothing to calculate from yet, report "0 B/s"
}

void CTransferSpeedMeter::JustConnected()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CTransferSpeedMeter::JustConnected()");

    TransferedBytes[0] = 0;
    ActIndexInTrBytes = 0;
    ActIndexInTrBytesTimeLim = (LastPacketsTime[0] = GetTickCount()) + TRSPMETER_ACTSPEEDSTEP;
    CountOfTrBytesItems = 1;
    LastPacketsSize[0] = 0;
    ActIndexInLastPackets = 1;
    CountOfLastPackets = 1;
    ResetSpeed = TRUE;
    MaxPacketSize = 0;
}

void CTransferSpeedMeter::BytesReceived(DWORD count, DWORD time, DWORD maxPacketSize)
{
    DEBUG_SLOW_CALL_STACK_MESSAGE1("CTransferSpeedMeter::BytesReceived(, ,)"); // ignore parameters for performance reasons (the call stack already slows us down)

    MaxPacketSize = maxPacketSize;

    if (count > 0)
    {
        //    if (count > MaxPacketSize)  // happens when the speed changes (due to SpeedLimit or ProgressBufferLimit); packets arrive that were read using the old buffer size
        //      TRACE_E("CTransferSpeedMeter::BytesReceived(): count > MaxPacketSize (" << count << " > " << MaxPacketSize << ")");

        if (ResetSpeed)
            ResetSpeed = FALSE;

        LastPacketsSize[ActIndexInLastPackets] = count;
        LastPacketsTime[ActIndexInLastPackets] = time;
        if (++ActIndexInLastPackets >= TRSPMETER_NUMOFSTOREDPACKETS + 1)
            ActIndexInLastPackets = 0;
        if (CountOfLastPackets < TRSPMETER_NUMOFSTOREDPACKETS + 1)
            CountOfLastPackets++;
    }
    if ((int)(time - ActIndexInTrBytesTimeLim) < 0) // within the current time interval, just add the byte count to the interval
    {
        TransferedBytes[ActIndexInTrBytes] += count;
    }
    else // outside the current time interval, we must create a new interval
    {
        int emptyTrBytes = (time - ActIndexInTrBytesTimeLim) / TRSPMETER_ACTSPEEDSTEP;
        int i = min(emptyTrBytes, TRSPMETER_ACTSPEEDNUMOFSTEPS); // more has no effect (the entire queue would be reset)
        if (i > 0 && CountOfTrBytesItems <= TRSPMETER_ACTSPEEDNUMOFSTEPS)
            CountOfTrBytesItems = min(TRSPMETER_ACTSPEEDNUMOFSTEPS + 1, CountOfTrBytesItems + i);
        while (i--)
        {
            if (++ActIndexInTrBytes > TRSPMETER_ACTSPEEDNUMOFSTEPS)
                ActIndexInTrBytes = 0; // moving along the circular queue
            TransferedBytes[ActIndexInTrBytes] = 0;
        }
        ActIndexInTrBytesTimeLim += (emptyTrBytes + 1) * TRSPMETER_ACTSPEEDSTEP;
        if (++ActIndexInTrBytes > TRSPMETER_ACTSPEEDNUMOFSTEPS)
            ActIndexInTrBytes = 0; // moving along the circular queue
        if (CountOfTrBytesItems <= TRSPMETER_ACTSPEEDNUMOFSTEPS)
            CountOfTrBytesItems++;
        TransferedBytes[ActIndexInTrBytes] = count;
    }
}

void CTransferSpeedMeter::AdjustProgressBufferLimit(DWORD* progressBufferLimit, DWORD lastFileBlockCount,
                                                    DWORD lastFileStartTime)
{
    if (CountOfLastPackets > 1 && lastFileBlockCount > 0) // "always true": at the start of the file CountOfLastPackets is 1 (2 = we already have one packet)
    {
        unsigned __int64 size = 0; // total size of stored packets of the last file
        int i = ((TRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - 1) % (TRSPMETER_NUMOFSTOREDPACKETS + 1);
        int c = min((DWORD)(CountOfLastPackets - 1), lastFileBlockCount);
        int packets = c;
        DWORD ti = GetTickCount();
        while (c--)
        {
            size += LastPacketsSize[i];
            if (i-- == 0)
                i = TRSPMETER_NUMOFSTOREDPACKETS;
            if (ti - LastPacketsTime[i] > 2000)
            {
                packets -= c;
                break; // take packets at most 2 seconds old (trying to compute the "current" speed)
            }
        }
        DWORD totalTime = min(ti - LastPacketsTime[i], ti - lastFileStartTime); // LastPacketsTime[i] may be older than lastFileStartTime (it is the last packet of the previous file); we care only about the time spent on this file
        if (totalTime == 0)
            totalTime = 10; // treat 0 ms as 10 ms (approx. the GetTickCount() step)
        unsigned __int64 speed = (size * 1000) / totalTime;
        DWORD bufLimit = ASYNC_SLOW_COPY_BUF_SIZE;
        while (bufLimit < ASYNC_COPY_BUF_SIZE)
        {
            // determined experimentally that Windows 7 loves a 32 KB buffer size; with it the utilization curve
            // of the network link is usually nicely smooth, whereas with 64 KB it jumps like crazy
            // and the overall achieved speed is about 5% lower... so a dirty bloody hack: we will also
            // prefer 32 KB... up to 8 * 128 (1024 KB/s)... that skips 64 KB and 128 KB, the next
            // buffer limit is as high as 256 KB
            // +
            // introduce a measure against oscillation between two buffer limit sizes when the speed is on the boundary
            // between two buffer limit sizes; raising it by one level will be harder (to choose the same buffer
            // the speed may be up to 9 * bufLimit instead of the standard 8 * bufLimit)
            if (bufLimit == 32 * 1024) // for the 32 KB buffer limit we use the values for the 128 KB buffer limit (instead of choosing 64 KB and 128 KB we pick 32 KB)
            {
                if (speed <= (bufLimit == *progressBufferLimit ? 9 * 128 * 1024 : 8 * 128 * 1024))
                    break;
                bufLimit = 256 * 1024; // 32 KB did not work, try up to 256 KB (64 KB and 128 KB cannot happen, 32 KB would have been chosen)
            }
            else
            {
                if (speed <= (bufLimit == *progressBufferLimit ? 9 * bufLimit : 8 * bufLimit))
                    break;
                bufLimit *= 2;
            }
        }
        if (bufLimit > ASYNC_COPY_BUF_SIZE)
            bufLimit = ASYNC_COPY_BUF_SIZE;
        *progressBufferLimit = bufLimit;
#ifdef WORKER_COPY_DEBUG_MSG
        TRACE_I("AdjustProgressBufferLimit(): speed=" << speed / 1024.0 << " KB/s, size=" << size << " B, packets=" << packets << ", new buffer limit=" << bufLimit);
#endif // WORKER_COPY_DEBUG_MSG
    }
    else
        TRACE_E("Unexpected situation in CTransferSpeedMeter::AdjustProgressBufferLimit()!");
}

//
// ****************************************************************************
// CProgressSpeedMeter
//

void CProgressSpeedMeter::GetSpeed(CQuadWord* speed)
{
    CALL_STACK_MESSAGE1("CProgressSpeedMeter::GetSpeed()");

    DWORD time = GetTickCount();

    if (CountOfLastPackets >= 2)
    { // test whether this is a low speed (calculated from LastPacketsSize and LastPacketsTime)
        int firstPacket = ((PRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - CountOfLastPackets) % (PRSPMETER_NUMOFSTOREDPACKETS + 1);
        int lastPacket = ((PRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - 1) % (PRSPMETER_NUMOFSTOREDPACKETS + 1);
        DWORD lastPacketTime = LastPacketsTime[lastPacket];
        DWORD totalTime = lastPacketTime - LastPacketsTime[firstPacket]; // time between receiving the first and last packet
        if (totalTime >= ((DWORD)(CountOfLastPackets - 1) * PRSPMETER_STPCKTSMININTERVAL) / PRSPMETER_NUMOFSTOREDPACKETS)
        {                                     // this is a low speed (up to PRSPMETER_NUMOFSTOREDPACKETS packets per PRSPMETER_STPCKTSMININTERVAL ms)
            if (time - lastPacketTime > 5000) // five-second "protection" period for the last computed slow speed
            {                                 // check whether the speed has dropped by more than four times compared to the speed of the last packet; if so, display
                                              // zero speed (so that when a slow transfer stops we do not keep showing the last recorded time-left value)
                int preLastPacket = ((PRSPMETER_NUMOFSTOREDPACKETS + 1) + ActIndexInLastPackets - 2) % (PRSPMETER_NUMOFSTOREDPACKETS + 1);
                if ((UINT64)4 * MaxPacketSize * (lastPacketTime - LastPacketsTime[preLastPacket]) < (UINT64)LastPacketsSize[lastPacket] * (time - lastPacketTime))
                {
                    speed->SetUI64(0);
                    return; // speed dropped at least two times, better show zero
                }
            }
            if (totalTime > PRSPMETER_ACTSPEEDSTEP * PRSPMETER_ACTSPEEDNUMOFSTEPS)
            { // compute the speed only from data closest to PRSPMETER_ACTSPEEDSTEP * PRSPMETER_ACTSPEEDNUMOFSTEPS
                // (if packets arrive slowly, the queue may contain packets from the last five minutes, but here we
                // compute the speed over the last X seconds, not the average over the last five minutes)
                int i = firstPacket;
                while (1)
                {
                    if (++i >= PRSPMETER_NUMOFSTOREDPACKETS + 1)
                        i = 0;
                    if (i == lastPacket || lastPacketTime - LastPacketsTime[i] < PRSPMETER_ACTSPEEDSTEP * PRSPMETER_ACTSPEEDNUMOFSTEPS)
                        break;
                    firstPacket = i;
                }
                totalTime = lastPacketTime - LastPacketsTime[firstPacket];
            }
            UINT64 totalSize = 0; // sum of all packet sizes except the first one (from whitch we use only the time)
            do
            {
                if (++firstPacket >= PRSPMETER_NUMOFSTOREDPACKETS + 1)
                    firstPacket = 0;
                totalSize += LastPacketsSize[firstPacket];
            } while (firstPacket != lastPacket);
            speed->SetUI64((1000 * totalSize) / totalTime);
            return; // low speed computed, we are done
        }
        else // this is a high speed (more than PRSPMETER_NUMOFSTOREDPACKETS packets per PRSPMETER_STPCKTSMININTERVAL ms),
        {    // perform a sudden speed drop test (especially when copying zero-sized files or creating empty directories begins)
            if (time - lastPacketTime > 5000)
            { // if no packet has arrived for 5000 ms, report zero speed
                speed->SetUI64(0);
                return;
            }
        }
    }
    else // nothing to calculate from yet, report "0 B/s"
    {
        speed->SetUI64(0);
        return;
    }
    // high speed (more than PRSPMETER_NUMOFSTOREDPACKETS packets per PRSPMETER_STPCKTSMININTERVAL ms)
    if (CountOfTrBytesItems > 0) // after the connection is established this is "always true"
    {
        int actIndexAdded = 0;                           // 0 = current index not included, 1 = current index included
        int emptyTrBytes = 0;                            // number of counted empty steps
        UINT64 total = 0;                                // total number of bytes over the last at most PRSPMETER_ACTSPEEDNUMOFSTEPS steps
        int addFromTrBytes = CountOfTrBytesItems - 1;    // number of closed steps to add from the queue
        DWORD restTime = 0;                              // time from the last counted step to now
        if ((int)(time - ActIndexInTrBytesTimeLim) >= 0) // current index already closed + empty steps may be needed
        {
            emptyTrBytes = (time - ActIndexInTrBytesTimeLim) / PRSPMETER_ACTSPEEDSTEP;
            restTime = (time - ActIndexInTrBytesTimeLim) % PRSPMETER_ACTSPEEDSTEP;
            emptyTrBytes = min(emptyTrBytes, PRSPMETER_ACTSPEEDNUMOFSTEPS);
            if (emptyTrBytes < PRSPMETER_ACTSPEEDNUMOFSTEPS) // empty steps are not enough; include the current index as well
            {
                total = TransferedBytes[ActIndexInTrBytes];
                actIndexAdded = 1;
            }
            addFromTrBytes = PRSPMETER_ACTSPEEDNUMOFSTEPS - actIndexAdded - emptyTrBytes;
            addFromTrBytes = min(addFromTrBytes, CountOfTrBytesItems - 1); // how many closed steps from the queue to include
        }
        else
        {
            restTime = time + PRSPMETER_ACTSPEEDSTEP - ActIndexInTrBytesTimeLim;
            total = TransferedBytes[ActIndexInTrBytes];
        }

        int actIndex = ActIndexInTrBytes;
        int i;
        for (i = 0; i < addFromTrBytes; i++)
        {
            if (--actIndex < 0)
                actIndex = PRSPMETER_ACTSPEEDNUMOFSTEPS; // moving along the circular queue
            total += TransferedBytes[actIndex];
        }
        DWORD t = (addFromTrBytes + actIndexAdded + emptyTrBytes) * PRSPMETER_ACTSPEEDSTEP + restTime;
        if (t > 0)
            speed->SetUI64((total * 1000) / t);
        else
            speed->SetUI64(0); // nothing to calculate from yet, report "0 B/s"
    }
    else
        speed->SetUI64(0); // nothing to calculate from yet, report "0 B/s"
}

void CProgressSpeedMeter::JustConnected()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CProgressSpeedMeter::JustConnected()");

    TransferedBytes[0] = 0;
    ActIndexInTrBytes = 0;
    ActIndexInTrBytesTimeLim = (LastPacketsTime[0] = GetTickCount()) + PRSPMETER_ACTSPEEDSTEP;
    CountOfTrBytesItems = 1;
    LastPacketsSize[0] = 0;
    ActIndexInLastPackets = 1;
    CountOfLastPackets = 1;
    MaxPacketSize = 0;
}

void CProgressSpeedMeter::BytesReceived(DWORD count, DWORD time, DWORD maxPacketSize)
{
    DEBUG_SLOW_CALL_STACK_MESSAGE1("CProgressSpeedMeter::BytesReceived(, ,)"); // ignore parameters for performance reasons (the call stack already slows us down)

    MaxPacketSize = maxPacketSize;

    if (count > 0)
    {
        //    if (count > MaxPacketSize)  // happens when the speed changes (due to SpeedLimit or ProgressBufferLimit); packets arrive that were read using the old buffer size
        //      TRACE_E("CProgressSpeedMeter::BytesReceived(): count > MaxPacketSize (" << count << " > " << MaxPacketSize << ")");

        LastPacketsSize[ActIndexInLastPackets] = count;
        LastPacketsTime[ActIndexInLastPackets] = time;
        if (++ActIndexInLastPackets >= PRSPMETER_NUMOFSTOREDPACKETS + 1)
            ActIndexInLastPackets = 0;
        if (CountOfLastPackets < PRSPMETER_NUMOFSTOREDPACKETS + 1)
            CountOfLastPackets++;
    }
    if ((int)(time - ActIndexInTrBytesTimeLim) < 0) // within the current time interval, just add the byte count to the interval
    {
        TransferedBytes[ActIndexInTrBytes] += count;
    }
    else // outside the current time interval, we must create a new interval
    {
        int emptyTrBytes = (time - ActIndexInTrBytesTimeLim) / PRSPMETER_ACTSPEEDSTEP;
        int i = min(emptyTrBytes, PRSPMETER_ACTSPEEDNUMOFSTEPS); // more has no effect (the entire queue would be reset)
        if (i > 0 && CountOfTrBytesItems <= PRSPMETER_ACTSPEEDNUMOFSTEPS)
            CountOfTrBytesItems = min(PRSPMETER_ACTSPEEDNUMOFSTEPS + 1, CountOfTrBytesItems + i);
        while (i--)
        {
            if (++ActIndexInTrBytes > PRSPMETER_ACTSPEEDNUMOFSTEPS)
                ActIndexInTrBytes = 0; // moving along the circular queue
            TransferedBytes[ActIndexInTrBytes] = 0;
        }
        ActIndexInTrBytesTimeLim += (emptyTrBytes + 1) * PRSPMETER_ACTSPEEDSTEP;
        if (++ActIndexInTrBytes > PRSPMETER_ACTSPEEDNUMOFSTEPS)
            ActIndexInTrBytes = 0; // moving along the circular queue
        if (CountOfTrBytesItems <= PRSPMETER_ACTSPEEDNUMOFSTEPS)
            CountOfTrBytesItems++;
        TransferedBytes[ActIndexInTrBytes] = count;
    }
}

//
// ****************************************************************************
// COperation helper methods
//

//
// ****************************************************************************
// Helper functions for file operations that use wide paths when available
// These ensure proper handling of long paths and Unicode filenames
//

// Opens source file for reading - uses SourceNameW if available

static FileResult CloseWorkerTrackedFile(HANDLE file)
{
    if (file == NULL || file == INVALID_HANDLE_VALUE)
        return FileResult::Error(ERROR_INVALID_HANDLE);
    HANDLES_REMOVE(file, __htFile, "IFileSystem::CloseFileHandle");
    return GetWorkerFileSystem()->CloseFileHandle(file);
}

HANDLE COperation::OpenSourceFile(DWORD flags) const
{
    const wchar_t* nameW = SourceNameW.c_str();
    HANDLE h = GetWorkerFileSystem()->CreateFile(
        nameW, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, flags, NULL);
    DWORD err = GetLastError();
    HANDLES_ADD_EX(__otQuiet, h != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, h, err, TRUE);
    return h;
}

// Opens target file for writing.
HANDLE COperation::OpenTargetFile(DWORD access, DWORD shareMode, DWORD disposition, DWORD flags) const
{
    const wchar_t* nameW = TargetNameW.c_str();
    HANDLE h = GetWorkerFileSystem()->CreateFile(
        nameW, access, shareMode, NULL, disposition, flags, NULL);
    DWORD err = GetLastError();
    HANDLES_ADD_EX(__otQuiet, h != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, h, err, TRUE);
    return h;
}

// Creates target file with encryption handling.
// NOTE: Does NOT add handle tracking - caller is responsible for HANDLES_ADD_EX
HANDLE COperation::CreateTargetFileEx(DWORD desiredAccess, DWORD shareMode, DWORD flagsAndAttributes, BOOL* encryptionNotSupported) const
{
    const wchar_t* nameW = TargetNameW.c_str();
    IFileSystem* fileSystem = GetWorkerFileSystem();
    HANDLE out = fileSystem->CreateFile(nameW, desiredAccess, shareMode, NULL,
                                        CREATE_NEW, flagsAndAttributes, NULL);
    if (out == INVALID_HANDLE_VALUE && encryptionNotSupported != NULL &&
        (flagsAndAttributes & FILE_ATTRIBUTE_ENCRYPTED))
    {
        // Test whether encryption is what the volume refused.
        HANDLE testOut = fileSystem->CreateFile(nameW, desiredAccess, shareMode, NULL,
                                                CREATE_NEW, (flagsAndAttributes & ~(FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_READONLY)), NULL);
        if (testOut != INVALID_HANDLE_VALUE)
        {
            *encryptionNotSupported = TRUE;
            fileSystem->CloseFileHandle(testOut);
            fileSystem->DeleteFile(nameW);
        }
    }
    return out;
}

// Deletes target file.
BOOL COperation::DeleteTargetFile() const
{
    return GetWorkerFileSystem()->DeleteFile(
        TargetNameW.c_str()).success;
}

// Sets target file attributes.
BOOL COperation::SetTargetAttributes(DWORD attrs) const
{
    return GetWorkerFileSystem()->SetFileAttributes(
        TargetNameW.c_str(), attrs).success;
}

// Gets target file attributes.
DWORD COperation::GetTargetAttributes() const
{
    return GetWorkerFileSystem()->GetFileAttributes(
        TargetNameW.c_str());
}

// Deletes source file.
BOOL COperation::DeleteSourceFile() const
{
    return GetWorkerFileSystem()->DeleteFile(
        SourceNameW.c_str()).success;
}

// Sets source file attributes.
BOOL COperation::SetSourceAttributes(DWORD attrs) const
{
    return GetWorkerFileSystem()->SetFileAttributes(
        SourceNameW.c_str(), attrs).success;
}

// Gets source file attributes.
DWORD COperation::GetSourceAttributes() const
{
    return GetWorkerFileSystem()->GetFileAttributes(
        SourceNameW.c_str());
}

// Clears read-only attribute on target file. ClearReadOnlyAttr is a pure wide
// helper (get+clear attrs); no interface method needed.
BOOL COperation::ClearTargetReadOnly(DWORD attr) const
{
    return ClearReadOnlyAttr(TargetNameW.c_str(), attr);
}

// Clears read-only attribute on source file.
BOOL COperation::ClearSourceReadOnly(DWORD attr) const
{
    return ClearReadOnlyAttr(SourceNameW.c_str(), attr);
}

// Checks if source file name is invalid (ends with space/dot).
BOOL COperation::IsSourceNameInvalid(BOOL ignInvalidName) const
{
    return FileNameIsInvalidW(SourceNameW.c_str(),
                              TRUE, ignInvalidName);
}

// Checks if target file name is invalid (ends with space/dot).
BOOL COperation::IsTargetNameInvalid(BOOL ignInvalidName) const
{
    return FileNameIsInvalidW(TargetNameW.c_str(),
                              TRUE, ignInvalidName);
}

// FindFirstFile for target path - always returns wide find data.
HANDLE COperation::FindFirstTarget(WIN32_FIND_DATAW* findData) const
{
    IFileSystem* fileSystem = GetWorkerFileSystem();
    if (fileSystem == NULL)
        return INVALID_HANDLE_VALUE;
    return fileSystem->FindFirstFile(
        TargetNameW.c_str(), findData);
}

// FindFirstFile for source path - always returns wide find data.
HANDLE COperation::FindFirstSource(WIN32_FIND_DATAW* findData) const
{
    IFileSystem* fileSystem = GetWorkerFileSystem();
    if (fileSystem == NULL)
        return INVALID_HANDLE_VALUE;
    return fileSystem->FindFirstFile(
        SourceNameW.c_str(), findData);
}

DWORD COperation::SetCompressionW(const wchar_t* path, USHORT compressionFormat)
{
    HANDLE file = GetWorkerFileSystem()->CreateFile(path, FILE_READ_DATA | FILE_WRITE_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();

    const FileResult compressionResult = GetWorkerFileSystem()->SetHandleCompression(
        file, compressionFormat != COMPRESSION_FORMAT_NONE);
    DWORD ret = compressionResult.success ? ERROR_SUCCESS : compressionResult.errorCode;
    (void)GetWorkerFileSystem()->CloseFileHandle(file);
    return ret;
}

// Saves file times, invokes operationFn, then restores times.
DWORD COperation::WithPreservedFileTimeW(const wchar_t* path, DWORD attrs,
                                         DWORD (*operationFn)(const wchar_t* path))
{
    DWORD flags = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0;

    HANDLE file = GetWorkerFileSystem()->CreateFile(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, flags, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();

    FILETIME ftCreated = {};
    FILETIME ftModified = {};
    const FileResult savedTimeResult = GetWorkerFileSystem()->GetHandleFileTime(
        file, &ftCreated, NULL, &ftModified);
    (void)GetWorkerFileSystem()->CloseFileHandle(file);

    DWORD ret = operationFn(path);

    // Restore file times regardless of operationFn result
    file = GetWorkerFileSystem()->CreateFile(path, GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, flags, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        if (savedTimeResult.success)
            (void)GetWorkerFileSystem()->SetHandleFileTime(
                file, &ftCreated, NULL, &ftModified);
        (void)GetWorkerFileSystem()->CloseFileHandle(file);
    }
    return ret;
}

//
// ****************************************************************************
// COperations
//

void COperations::SetTFS(const CQuadWord& TFS)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        TransferredFileSize = TFS;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::CalcLimitBufferSize(int* limitBufferSize, int bufferSize)
{
    if (limitBufferSize != NULL)
    {
        *limitBufferSize = UseSpeedLimit && SpeedLimit < (DWORD)bufferSize ? (UseProgressBufferLimit && ProgressBufferLimit < SpeedLimit ? ProgressBufferLimit : SpeedLimit) : (UseProgressBufferLimit && ProgressBufferLimit < (DWORD)bufferSize ? ProgressBufferLimit : bufferSize);
    }
}

void COperations::EnableProgressBufferLimit(BOOL useProgressBufferLimit)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        UseProgressBufferLimit = useProgressBufferLimit;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::SetFileStartParams()
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        LastFileBlockCount = 0;
        LastFileStartTime = GetTickCount();
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::SetTFSandProgressSize(const CQuadWord& TFS, const CQuadWord& pSize,
                                        int* limitBufferSize, int bufferSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        TransferredFileSize = TFS;
        ProgressSize = pSize;
        CalcLimitBufferSize(limitBufferSize, bufferSize);
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::GetNewBufSize(int* limitBufferSize, int bufferSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        CalcLimitBufferSize(limitBufferSize, bufferSize);
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::AddBytesToSpeedMetersAndTFSandPS(DWORD bytesCount, BOOL onlyToProgressSpeedMeter,
                                                   int bufferSize, int* limitBufferSize, DWORD maxPacketSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        DWORD ti = GetTickCount();

        if (maxPacketSize == 0)
            CalcLimitBufferSize((int*)&maxPacketSize, bufferSize);

        DWORD bytesCountForSpeedMeters = bytesCount;
        if (!onlyToProgressSpeedMeter)
        {
            if (limitBufferSize != NULL && bytesCount > 0)
            {
                if (UseSpeedLimit)
                {
                    DWORD sleepNow = 0;
                    if (SleepAfterWrite == -1) // this is the first packet, set up the speed limit parameters
                    {
                        CalcLimitBufferSize(limitBufferSize, bufferSize);
                        LastBufferLimit = *limitBufferSize;
                        if (SpeedLimit >= HIGH_SPEED_LIMIT)
                        { // here there is no risk of receiving more data than the speed limit allows (HIGH_SPEED_LIMIT must be >= the largest buffer)
                            SleepAfterWrite = 0;
                            BytesTrFromLastSetup.SetUI64(bytesCount);
                        }
                        else
                        {
                            SleepAfterWrite = (1000 * *limitBufferSize) / SpeedLimit; // for the first second of the transfer assume the transfer itself is "infinitely fast" (determining the actual speed from the first packet is unrealistic)
                            if (bytesCount > SpeedLimit)                              // a slowdown occurred during the operation (for example, a 32 KB buffer read & write happened and the speed limit is 1 B/s, so theoretically we should now wait 32768 seconds, which is naturally unrealistic)
                                sleepNow = 1000;                                      // wait one second + add to the speed meter only the bytes allowed by the speed limit (e.g., just 1 B)
                            else
                                sleepNow = (SleepAfterWrite * bytesCount) / *limitBufferSize;
                            BytesTrFromLastSetup.SetUI64(0);
                            LastSetupTime = ti + sleepNow;
                        }
                    }
                    else
                    {
                        if ((int)(ti - LastSetupTime) >= 1000 || BytesTrFromLastSetup.Value + bytesCount >= SpeedLimit ||
                            SpeedLimit >= HIGH_SPEED_LIMIT && BytesTrFromLastSetup.Value + bytesCount >= SpeedLimit / HIGH_SPEED_LIMIT_BRAKE_DIV)
                        { // time to recalculate the speed limit parameters + possibly "brake"
                            __int64 sleepFromLastSetup64 = (SleepAfterWrite * BytesTrFromLastSetup.Value) / LastBufferLimit;
                            DWORD sleepFromLastSetup = sleepFromLastSetup64 < 1000 ? (DWORD)sleepFromLastSetup64 : 1000;
                            BytesTrFromLastSetup += CQuadWord(bytesCount, 0);
                            __int64 idealTotalTime64 = (1000 * BytesTrFromLastSetup.Value + SpeedLimit - 1) / SpeedLimit; // "+ SpeedLimit - 1" is for rounding
                            int idealTotalTime = idealTotalTime64 < 10000 ? (int)idealTotalTime64 : 10000;
                            if (idealTotalTime > (int)(ti - LastSetupTime))
                            {
                                sleepNow = idealTotalTime - (ti - LastSetupTime); // need to brake (we are faster or only slightly slower than the speed limit)
                                if (sleepNow > 1000)                              // waiting longer than a second makes no sense (the meter will accept at most *limitBufferSize)
                                    sleepNow = 1000;
                            }
                            // else sleepNow = 0;  // we are slower than the speed limit (at ideal speed we would wait the proportional part of SleepAfterWrite)

                            CalcLimitBufferSize(limitBufferSize, bufferSize);
                            LastBufferLimit = *limitBufferSize;

                            if (SpeedLimit >= HIGH_SPEED_LIMIT)
                                SleepAfterWrite = 0;
                            else
                            {
                                int idealTotalSleep = (int)(sleepFromLastSetup + (idealTotalTime - (ti - LastSetupTime)));
                                if (idealTotalSleep > 0) // speed limit is lower than the copy speed, we will brake after each packet
                                    SleepAfterWrite = (DWORD)(((unsigned __int64)idealTotalSleep * LastBufferLimit) / BytesTrFromLastSetup.Value);
                                else
                                    SleepAfterWrite = 0; // speed limit is higher than the copy speed (no need to brake)
                            }
                            LastSetupTime = ti + sleepNow;
                            BytesTrFromLastSetup.SetUI64(0);
                        }
                        else // for intermediate packets use the precomputed parameters
                        {
                            BytesTrFromLastSetup += CQuadWord(bytesCount, 0);
                            *limitBufferSize = LastBufferLimit < bufferSize ? LastBufferLimit : bufferSize;
                            if (SleepAfterWrite > 0)
                            {
                                sleepNow = (SleepAfterWrite * bytesCount) / LastBufferLimit;
                                if (sleepNow > 1000) // waiting longer than a second makes no sense (the meter will accept at most the speed limit)
                                    sleepNow = 1000;
                            }
                        }
                    }
                    if (bytesCount > SpeedLimit)               // a slowdown occurred during the operation (for example, a 32 KB buffer read & write happened and the speed limit is 1 B/s, so theoretically we should now wait 32768 seconds, which is naturally unrealistic)
                        bytesCountForSpeedMeters = SpeedLimit; // add to the speed meter only the bytes allowed by the speed limit (e.g., just 1 B)
                    if (sleepNow > 0)                          // braking because of the speed limit
                    {
                        HANDLES(LeaveCriticalSection(&StatusCS));
                        Sleep(sleepNow);
                        HANDLES(EnterCriticalSection(&StatusCS));
                        ti = GetTickCount();
                    }
                }
                else
                    CalcLimitBufferSize(limitBufferSize, bufferSize); // without limit - full speed (except for ProgressBufferLimit)
            }
            TransferSpeedMeter.BytesReceived(bytesCountForSpeedMeters, ti, maxPacketSize);
            TransferredFileSize.Value += bytesCount;

            if (UseProgressBufferLimit &&
                (++LastFileBlockCount >= ASYNC_SLOW_COPY_BUF_MINBLOCKS || // provided there is enough data for the test
                 ProgressBufferLimit * LastFileBlockCount >= ASYNC_SLOW_COPY_BUF_MINBLOCKS * ASYNC_SLOW_COPY_BUF_SIZE) &&
                ti - LastProgBufLimTestTime >= 1000) // and it is time for another test
            {                                        // compute ProgressBufferLimit for the next round (the next read still uses the current value)
                TransferSpeedMeter.AdjustProgressBufferLimit(&ProgressBufferLimit, LastFileBlockCount, LastFileStartTime);
                LastProgBufLimTestTime = GetTickCount();
                if (LastFileBlockCount > 1000000000)
                    LastFileBlockCount = 1000000; // overflow protection (just a ton of blocks, the exact count is not that important)
            }
        }
        ProgressSpeedMeter.BytesReceived(bytesCountForSpeedMeters, ti, maxPacketSize);
        ProgressSize.Value += bytesCount;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::AddBytesToTFSandSetProgressSize(const CQuadWord& bytesCount, const CQuadWord& pSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        TransferredFileSize += bytesCount;
        ProgressSize = pSize;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::AddBytesToTFS(const CQuadWord& bytesCount)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        TransferredFileSize += bytesCount;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::GetTFS(CQuadWord* TFS)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        *TFS = TransferredFileSize;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::GetTFSandResetTrSpeedIfNeeded(CQuadWord* TFS)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        *TFS = TransferredFileSize;
        if (TransferSpeedMeter.ResetSpeed)
        {
            TransferSpeedMeter.JustConnected();
            if (UseSpeedLimit)
            {
                SleepAfterWrite = -1; // compute when the first packet arrives
                LastSetupTime = GetTickCount();
                BytesTrFromLastSetup.SetUI64(0);
            }
        }
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::SetProgressSize(const CQuadWord& pSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        ProgressSize = pSize;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::GetStatus(CQuadWord* transferredFileSize, CQuadWord* transferSpeed,
                            CQuadWord* progressSize, CQuadWord* progressSpeed,
                            BOOL* useSpeedLimit, DWORD* speedLimit)
{
    HANDLES(EnterCriticalSection(&StatusCS));
    *transferredFileSize = TransferredFileSize;
    *progressSize = ProgressSize;
    TransferSpeedMeter.GetSpeed(transferSpeed);
    ProgressSpeedMeter.GetSpeed(progressSpeed);
    *useSpeedLimit = UseSpeedLimit;
    *speedLimit = SpeedLimit;
    HANDLES(LeaveCriticalSection(&StatusCS));
}

void COperations::InitSpeedMeters(BOOL operInProgress)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        TransferSpeedMeter.JustConnected();
        ProgressSpeedMeter.JustConnected();
        if (UseSpeedLimit)
        {
            SleepAfterWrite = -1; // compute when the first packet arrives
            LastSetupTime = GetTickCount();
            BytesTrFromLastSetup.SetUI64(0);
        }
        // after a pause, a speed limit change, or an error dialog discard the old data
        if (operInProgress)
        {
            LastFileBlockCount = 0;
            LastFileStartTime = GetTickCount();
            LastProgBufLimTestTime = GetTickCount(); // postpone the next test by a second so that we have relevant data
        }
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

BOOL COperations::GetTFSandProgressSize(CQuadWord* transferredFileSize, CQuadWord* progressSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        *transferredFileSize = TransferredFileSize;
        *progressSize = ProgressSize;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
    return ShowStatus;
}

void COperations::GetSpeedLimit(BOOL* useSpeedLimit, DWORD* speedLimit)
{
    HANDLES(EnterCriticalSection(&StatusCS));
    *useSpeedLimit = UseSpeedLimit;
    *speedLimit = SpeedLimit;
    HANDLES(LeaveCriticalSection(&StatusCS));
}

//
// ****************************************************************************
// CAsyncCopyParams
//

struct CAsyncCopyParams
{
    void* Buffers[8];         // allocated buffers of size ASYNC_COPY_BUF_SIZE bytes
    OVERLAPPED Overlapped[8]; // structures for asynchronous operations

    BOOL UseAsyncAlg; // TRUE = use the asynchronous algorithm (data must be allocated), FALSE = old synchronous algorithm (allocate nothing)

    BOOL HasFailed; // TRUE = failed to create an event for the Overlapped array, the structure is unusable

    CAsyncCopyParams();
    ~CAsyncCopyParams();

    void Init(BOOL useAsyncAlg);

    BOOL Failed() { return HasFailed; }

    DWORD GetOverlappedFlag() { return UseAsyncAlg ? FILE_FLAG_OVERLAPPED : 0; }

    OVERLAPPED* InitOverlapped(int i);                                    // zeroes and returns Overlapped[i]
    OVERLAPPED* InitOverlappedWithOffset(int i, const CQuadWord& offset); // zeroes it, sets 'offset', and returns Overlapped[i]
    OVERLAPPED* GetOverlapped(int i) { return &Overlapped[i]; }
    void SetOverlappedToEOF(int i, const CQuadWord& offset); // sets Overlapped[i] to the state after finishing asynchronous reading that detected EOF
};

CAsyncCopyParams::CAsyncCopyParams()
{
    memset(Buffers, 0, sizeof(Buffers));
    memset(Overlapped, 0, sizeof(Overlapped));
    UseAsyncAlg = FALSE;
    HasFailed = FALSE;
}

void CAsyncCopyParams::Init(BOOL useAsyncAlg)
{
    UseAsyncAlg = useAsyncAlg;
    if (UseAsyncAlg && Buffers[0] == NULL)
    {
        for (int i = 0; i < 8; i++)
        {
            Buffers[i] = malloc(ASYNC_COPY_BUF_SIZE);
            Overlapped[i].hEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));
            if (Overlapped[i].hEvent == NULL)
            {
                DWORD err = GetLastError();
                TRACE_EW(L"Unable to create synchronization object for Copy rutine: " << GetErrorTextOwned(err).c_str());
                HasFailed = TRUE;
            }
        }
    }
}

CAsyncCopyParams::~CAsyncCopyParams()
{
    for (int i = 0; i < 8; i++)
    {
        if (Buffers[i] != NULL)
            free(Buffers[i]);
        if (Overlapped[i].hEvent != NULL)
            HANDLES(CloseHandle(Overlapped[i].hEvent));
    }
}

OVERLAPPED*
CAsyncCopyParams::InitOverlapped(int i)
{
    if (!UseAsyncAlg)
        TRACE_C("CAsyncCopyParams::InitOverlapped(): unexpected call, UseAsyncAlg is FALSE!");
    Overlapped[i].Internal = 0;
    Overlapped[i].InternalHigh = 0;
    Overlapped[i].Offset = 0;
    Overlapped[i].OffsetHigh = 0;
    // Overlapped[i].Pointer = 0;  // this is a union, Pointer overlaps with Offset and OffsetHigh
    return &Overlapped[i];
}

OVERLAPPED*
CAsyncCopyParams::InitOverlappedWithOffset(int i, const CQuadWord& offset)
{
    if (!UseAsyncAlg)
        TRACE_C("CAsyncCopyParams::InitOverlappedWithOffset(): unexpected call, UseAsyncAlg is FALSE!");
    Overlapped[i].Internal = 0;
    Overlapped[i].InternalHigh = 0;
    Overlapped[i].Offset = offset.LoDWord;
    Overlapped[i].OffsetHigh = offset.HiDWord;
    // Overlapped[i].Pointer = 0;  // this is a union, Pointer overlaps with Offset and OffsetHigh
    return &Overlapped[i];
}

void CAsyncCopyParams::SetOverlappedToEOF(int i, const CQuadWord& offset)
{
    if (!UseAsyncAlg)
        TRACE_C("CAsyncCopyParams::SetOverlappedToEOF(): unexpected call, UseAsyncAlg is FALSE!");
    Overlapped[i].Internal = 0xC0000011 /* STATUS_END_OF_FILE */; // NTSTATUS code equivalent to system error code ERROR_HANDLE_EOF
    Overlapped[i].InternalHigh = 0;
    Overlapped[i].Offset = offset.LoDWord;
    Overlapped[i].OffsetHigh = offset.HiDWord;
    // Overlapped[i].Pointer = 0;  // this is a union, Pointer overlaps with Offset and OffsetHigh
    SetEvent(Overlapped[i].hEvent);
}

// **********************************************************************************

void InitWorker()
{
    if (NtDLL != NULL) // "always true"
    {
        DynNtFsControlFile = (NTFSCONTROLFILE)GetProcAddress(NtDLL, "NtFsControlFile");                      // has no header
    }
}

void ReleaseWorker()
{
    DynNtFsControlFile = NULL;
}

struct CWorkerData
{
    COperations* Script;
    HWND HProgressDlg;
    void* Buffer;
    BOOL BufferIsAllocated;
    DWORD ClearReadonlyMask;
    CConvertData* ConvertData;
    HANDLE WContinue;

    HANDLE WorkerNotSuspended;
    BOOL* CancelWorker;
    int* OperationProgress;
    int* SummaryProgress;
};

struct CWorkerState
{
    BOOL OverwriteAll; // keeps the state of automatic overwriting of the target with the source
    BOOL OverwriteHiddenAll;
    BOOL DeleteHiddenAll;
    BOOL EncryptSystemAll;
    BOOL DirOverwriteAll;
    BOOL FileOutLossEncrAll;
    BOOL DirCrLossEncrAll;

    BOOL SkipAllFileWrite; // has the Skip All button already been used?
    BOOL SkipAllFileRead;
    BOOL SkipAllOverwrite;
    BOOL SkipAllSystemOrHidden;
    BOOL SkipAllFileOpenIn;
    BOOL SkipAllFileOpenOut;
    BOOL SkipAllOverwriteErr;
    BOOL SkipAllMoveErrors;
    BOOL SkipAllDeleteErr;
    BOOL SkipAllDirCreate;
    BOOL SkipAllDirCreateErr;
    BOOL SkipAllChangeAttrs;
    BOOL SkipAllEncryptSystem;
    BOOL SkipAllFileADSOpenIn;
    BOOL SkipAllFileADSOpenOut;
    BOOL SkipAllGetFileTime;
    BOOL SkipAllSetFileTime;
    BOOL SkipAllFileADSRead;
    BOOL SkipAllFileADSWrite;
    BOOL SkipAllDirOver;
    BOOL SkipAllFileOutLossEncr;
    BOOL SkipAllDirCrLossEncr;

    BOOL IgnoreAllADSReadErr;
    BOOL IgnoreAllADSOpenOutErr;
    BOOL IgnoreAllGetFileTimeErr;
    BOOL IgnoreAllSetFileTimeErr;
    BOOL IgnoreAllSetAttrsErr;
    BOOL IgnoreAllCopyPermErr;
    BOOL IgnoreAllCopyDirTimeErr;

    wchar_t OpStrCopying[50];
    wchar_t OpStrCopyingPrep[50];
    wchar_t OpStrMoving[50];
    wchar_t OpStrMovingPrep[50];
    wchar_t OpStrCreatingDir[50];
    wchar_t OpStrDeleting[50];
    wchar_t OpStrConverting[50];
    wchar_t OpStrChangingAttrs[50];

    int CnfrmFileOver; // local copy of the Salamander configuration
    int CnfrmDirOver;
    int CnfrmSHFileOver;
    int CnfrmSHFileDel;
    int UseRecycleBin;
    BOOL UseAsyncCopyAlg;
    CMaskGroup RecycleMasks;

    // Source of the most recent ocCreateDirLink the user skipped.
    //
    // A MOVE of a directory link is two operations: clone the reparse buffer at the
    // target, then delete the source link. Skipping the clone is not a refusal to do
    // anything - the delete still follows it in the script - so without this the link
    // vanished from the source and was never made at the target. Records the source
    // name so DoDeleteDirLink can recognise its own half of the pair and skip too.
    std::wstring SkippedDirLinkSourceW;

    // Initialize all skip/confirm flags to FALSE and copy config values
    void Init()
    {
        OverwriteAll = OverwriteHiddenAll = DeleteHiddenAll =
            SkipAllFileWrite = SkipAllFileRead =
                SkipAllOverwrite = SkipAllSystemOrHidden =
                    SkipAllFileOpenIn = SkipAllFileOpenOut =
                        SkipAllOverwriteErr = SkipAllMoveErrors =
                            SkipAllDeleteErr = SkipAllDirCreate =
                                SkipAllDirCreateErr = SkipAllChangeAttrs =
                                    EncryptSystemAll = SkipAllEncryptSystem =
                                        IgnoreAllADSReadErr = SkipAllFileADSOpenIn =
                                            SkipAllFileADSOpenOut = SkipAllFileADSRead =
                                                SkipAllFileADSWrite = DirOverwriteAll =
                                                    SkipAllDirOver = IgnoreAllADSOpenOutErr =
                                                        IgnoreAllSetAttrsErr = IgnoreAllCopyPermErr =
                                                            IgnoreAllCopyDirTimeErr = SkipAllFileOutLossEncr =
                                                                FileOutLossEncrAll = SkipAllDirCrLossEncr =
                                                                    DirCrLossEncrAll = IgnoreAllGetFileTimeErr =
                                                                        IgnoreAllSetFileTimeErr = SkipAllGetFileTime =
                                                                            SkipAllSetFileTime = FALSE;
        SkippedDirLinkSourceW.clear();
        CnfrmFileOver = Configuration.CnfrmFileOver;
        CnfrmDirOver = Configuration.CnfrmDirOver;
        CnfrmSHFileOver = Configuration.CnfrmSHFileOver;
        CnfrmSHFileDel = Configuration.CnfrmSHFileDel;
        UseRecycleBin = Configuration.UseRecycleBin;
        UseAsyncCopyAlg = Windows7AndLater && Configuration.UseAsyncCopyAlg;
        // wide - Configuration.RecycleMasks is a real CMaskGroup with wide
        // storage; the narrow round trip mangled a non-ASCII recycle-bin mask for the
        // duration of the whole copy/move/delete operation.
        RecycleMasks.SetMasksString(Configuration.RecycleMasks.GetMasksString(),
                                     Configuration.RecycleMasks.GetExtendedMode());
        int errorPos;
        if (UseRecycleBin == 2 && !PrepareRecycleMasks(errorPos))
            TRACE_E("Error in recycle-bin group mask.");

        lstrcpynW(OpStrCopying, LoadStrW(IDS_COPYING), 50);
        lstrcpynW(OpStrCopyingPrep, LoadStrW(IDS_COPYINGPREP), 50);
        lstrcpynW(OpStrMoving, LoadStrW(IDS_MOVING), 50);
        lstrcpynW(OpStrMovingPrep, LoadStrW(IDS_MOVINGPREP), 50);
        lstrcpynW(OpStrCreatingDir, LoadStrW(IDS_CREATINGDIR), 50);
        lstrcpynW(OpStrDeleting, LoadStrW(IDS_DELETING), 50);
        lstrcpynW(OpStrConverting, LoadStrW(IDS_CONVERTING), 50);
        lstrcpynW(OpStrChangingAttrs, LoadStrW(IDS_CHANGINGATTRS), 50);
    }

    // Headless initialization — no LoadStr(), no Configuration global.
    // Uses hardcoded English strings and safe defaults.
    // For use by RunWorkerDirect in test/CLI/headless contexts.
    void InitHeadless()
    {
        OverwriteAll = OverwriteHiddenAll = DeleteHiddenAll =
            SkipAllFileWrite = SkipAllFileRead =
                SkipAllOverwrite = SkipAllSystemOrHidden =
                    SkipAllFileOpenIn = SkipAllFileOpenOut =
                        SkipAllOverwriteErr = SkipAllMoveErrors =
                            SkipAllDeleteErr = SkipAllDirCreate =
                                SkipAllDirCreateErr = SkipAllChangeAttrs =
                                    EncryptSystemAll = SkipAllEncryptSystem =
                                        IgnoreAllADSReadErr = SkipAllFileADSOpenIn =
                                            SkipAllFileADSOpenOut = SkipAllFileADSRead =
                                                SkipAllFileADSWrite = DirOverwriteAll =
                                                    SkipAllDirOver = IgnoreAllADSOpenOutErr =
                                                        IgnoreAllSetAttrsErr = IgnoreAllCopyPermErr =
                                                            IgnoreAllCopyDirTimeErr = SkipAllFileOutLossEncr =
                                                                FileOutLossEncrAll = SkipAllDirCrLossEncr =
                                                                    DirCrLossEncrAll = IgnoreAllGetFileTimeErr =
                                                                        IgnoreAllSetFileTimeErr = SkipAllGetFileTime =
                                                                            SkipAllSetFileTime = FALSE;
        SkippedDirLinkSourceW.clear();

        // Always consult observer for confirmation decisions (1 = ask)
        CnfrmFileOver = 1;
        CnfrmDirOver = 1;
        CnfrmSHFileOver = 1;
        CnfrmSHFileDel = 1;
        UseRecycleBin = 0;
        UseAsyncCopyAlg = FALSE;

        // Hardcoded English operation strings (no resource DLL needed)
        lstrcpynW(OpStrCopying, L"Copying", 50);
        lstrcpynW(OpStrCopyingPrep, L"to", 50);
        lstrcpynW(OpStrMoving, L"Moving", 50);
        lstrcpynW(OpStrMovingPrep, L"to", 50);
        lstrcpynW(OpStrCreatingDir, L"Creating directory", 50);
        lstrcpynW(OpStrDeleting, L"Deleting", 50);
        lstrcpynW(OpStrConverting, L"Converting", 50);
        lstrcpynW(OpStrChangingAttrs, L"Changing attributes", 50);
    }

    BOOL PrepareRecycleMasks(int& errorPos)
    {
        return RecycleMasks.PrepareMasks(errorPos);
    }

    // The narrow AgreeRecycleMasks wrapper was removed - its sole caller
    // already uses AgreeRecycleMasksW directly, and RecycleMasks.AgreeMasks itself now
    // takes wchar_t* (see masks.cpp), so the narrow wrapper had no valid body
    // left to give it.
    BOOL AgreeRecycleMasksW(const wchar_t* fileName, const wchar_t* fileExt)
    {
        return RecycleMasks.AgreeMasks(fileName, fileExt);
    }
};

int CaclProg(const CQuadWord& progressCurrent, const CQuadWord& progressTotal)
{
    return progressCurrent >= progressTotal ? (progressTotal.Value == 0 ? 0 : 1000) : (int)((progressCurrent * CQuadWord(1000, 0)) / progressTotal).Value;
}

BOOL GetDirTimeW(const wchar_t* dirName, FILETIME* ftModified);
BOOL DoCopyDirTime(IWorkerObserver& observer, FILETIME* modified, CWorkerState& workerState, BOOL quiet,
                   const std::wstring& targetNameW);

// Wide info line for the overwrite prompt: "size, date, time[, attrs]".

void GetFileOverwriteInfoW(wchar_t* buff, int buffLen, HANDLE file, const wchar_t* fileName, FILETIME* fileTime, BOOL* getTimeFailed)
{
    FILETIME lastWrite;
    SYSTEMTIME st;
    FILETIME ft;
    wchar_t date[50], time[50];
    if (!GetWorkerFileSystem()->GetHandleFileTime(
            file, NULL, NULL, &lastWrite).success ||
        !FileTimeToLocalFileTime(&lastWrite, &ft) ||
        !FileTimeToSystemTime(&ft, &st))
    {
        if (getTimeFailed != NULL)
            *getTimeFailed = TRUE;
        date[0] = 0;
        time[0] = 0;
    }
    else
    {
        if (fileTime != NULL)
            *fileTime = ft;
        if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, time, 50) == 0)
            swprintf_s(time, L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
        if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, date, 50) == 0)
            swprintf_s(date, L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);
    }

    wchar_t attr[30];
    wcscpy_s(attr, L", ");
    DWORD attrs = GetWorkerFileSystem()->GetFileAttributes(fileName);
    if (attrs != 0xFFFFFFFF)
    {
        GetAttrsStringW(attr + 2, attrs);
    }
    if (wcslen(attr) == 2)
        attr[0] = 0;

    std::wstring number;
    CQuadWord size;
    DWORD err;
    if (SalGetFileSize(file, size, err))
        number = NumberToStr(size);

    _snwprintf_s(buff, buffLen, _TRUNCATE, L"%s, %s, %s%s", number.c_str(), date, time, attr);
}

// Wide version of GetDirInfo — uses native wide APIs
void GetDirInfoW(wchar_t* buffer, const wchar_t* dir)
{
    std::wstring dirPath = MakeCopyWithBackslashIfNeededW(dir);

    BOOL ok = FALSE;
    FILETIME lastWrite;
    if (NameEndsWithBackslashW(dirPath.c_str()))
    {
        HANDLE file = GetWorkerFileSystem()->CreateFile(dirPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (file != INVALID_HANDLE_VALUE)
        {
            if (GetWorkerFileSystem()->GetHandleFileTime(
                    file, NULL, NULL, &lastWrite).success)
                ok = TRUE;
            (void)CloseWorkerTrackedFile(file);
        }
    }
    else
    {
        WIN32_FIND_DATAW data;
        IFileSystem* fileSystem = GetWorkerFileSystem();
        HANDLE find = fileSystem != NULL ? fileSystem->FindFirstFile(dirPath.c_str(), &data)
                                         : INVALID_HANDLE_VALUE;
        if (find != INVALID_HANDLE_VALUE)
        {
            SalLPFindClose(find);
            lastWrite = data.ftLastWriteTime;
            ok = TRUE;
        }
    }
    if (ok)
    {
        SYSTEMTIME st;
        FILETIME ft;
        if (FileTimeToLocalFileTime(&lastWrite, &ft) &&
            FileTimeToSystemTime(&ft, &st))
        {
            wchar_t date[50], time[50];
            if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, time, 50) == 0)
                swprintf_s(time, L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
            if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, date, 50) == 0)
                swprintf_s(date, L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);

            swprintf(buffer, 101, L"%s, %s", date, time);
        }
        else
            swprintf(buffer, 101, L"%s, %s", LoadStrW(IDS_INVALID_DATEORTIME), LoadStrW(IDS_INVALID_DATEORTIME));
    }
    else
        buffer[0] = 0;
}


BOOL IsDirectoryEmptyW(const wchar_t* name) // directories/subdirectories contain no files
{
    std::wstring dir(name);
    if (!dir.empty() && dir.back() != L'\\')
        dir += L'\\';
    std::wstring pattern = dir + L"*";

    IFileSystem* fileSystem = GetWorkerFileSystem();
    WIN32_FIND_DATAW fileData;
    HANDLE search = fileSystem != NULL ? fileSystem->FindFirstFile(pattern.c_str(), &fileData)
                                       : INVALID_HANDLE_VALUE;
    if (search != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fileData.cFileName[0] == 0 ||
                fileData.cFileName[0] == L'.' && (fileData.cFileName[1] == 0 ||
                                                   fileData.cFileName[1] == L'.' && fileData.cFileName[2] == 0))
                continue;

            if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                std::wstring subDir = dir + fileData.cFileName;
                if (!IsDirectoryEmptyW(subDir.c_str())) // the subdirectory is not empty
                {
                    SalLPFindClose(search);
                    return FALSE;
                }
            }
            else
            {
                SalLPFindClose(search); // a file exists here
                return FALSE;
            }
        } while (fileSystem != NULL && fileSystem->FindNextFile(search, &fileData));
        SalLPFindClose(search);
    }
    return TRUE;
}

// 2026-08-25: the narrow IsDirectoryEmpty(char*) thin adapter was deleted -
// confirmed-dead (zero callers anywhere; a pure core-internal helper, no ABI entry at all).

void GainWriteOwnerAccess()
{
    static BOOL firstCall = TRUE;
    if (firstCall)
    {
        firstCall = FALSE;

        HANDLE tokenHandle;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tokenHandle))
        {
            TRACE_E("GainWriteOwnerAccess(): OpenProcessToken failed!");
            return;
        }

        int i;
        for (i = 0; i < 3; i++)
        {
            // Privilege names are permanently-ASCII Windows constants (never localized/widened);
            // the SDK only exposes them via the TCHAR-generic SE_*_NAME macros, so this uses the
            // exact literal each expands to instead, staying explicitly narrow rather than
            // picking up LPCTSTR (this codebase's own TcharVocabulary ratchet tracks and rejects
            // new LPCTSTR usage).
            const wchar_t* privName = NULL;
            switch (i)
            {
            case 0:
                privName = L"SeRestorePrivilege"; // SE_RESTORE_NAME
                break;
            case 1:
                privName = L"SeTakeOwnershipPrivilege"; // SE_TAKE_OWNERSHIP_NAME
                break;
            case 2:
                privName = L"SeSecurityPrivilege"; // SE_SECURITY_NAME
                break;
            }

            LUID value;
            if (privName != NULL && LookupPrivilegeValueW(NULL, privName, &value))
            {
                TOKEN_PRIVILEGES tokenPrivileges;
                tokenPrivileges.PrivilegeCount = 1;
                tokenPrivileges.Privileges[0].Luid = value;
                tokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

                AdjustTokenPrivileges(tokenHandle, FALSE, &tokenPrivileges, sizeof(tokenPrivileges), NULL, NULL);
                if (GetLastError() != NO_ERROR)
                {
                    DWORD err = GetLastError();
                    TRACE_EW(L"GainWriteOwnerAccess(): AdjustTokenPrivileges(" << privName << L") failed! error: " << GetErrorTextOwned(err).c_str());
                }
                // The filesystem security adapter attempts ownership changes
                // only when needed; enabling the privilege here is sufficient.
            }
            else
            {
                DWORD err = GetLastError();
                TRACE_EW(L"GainWriteOwnerAccess(): LookupPrivilegeValue(" << (privName != NULL ? privName : L"null") << L") failed! error: " << GetErrorTextOwned(err).c_str());
            }
        }
        CloseHandle(tokenHandle);
    }
}
/*
//  Purpose:    Determines if the user is a member of the administrators group.
//  Return:     TRUE if user is a admin
//              FALSE if not
#define STATUS_SUCCESS          ((NTSTATUS)0x00000000L) // ntsubauth
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
typedef NTSTATUS (WINAPI *FNtQueryInformationToken)(
    HANDLE TokenHandle,                             // IN
    TOKEN_INFORMATION_CLASS TokenInformationClass,  // IN
    PVOID TokenInformation,                         // OUT
    ULONG TokenInformationLength,                   // IN
    PULONG ReturnLength                             // OUT
    );


BOOL IsUserAdmin()
{
  if (NtDLL == NULL)
    return TRUE;

  GainWriteOwnerAccess();

  FNtQueryInformationToken DynNTNtQueryInformationToken = (FNtQueryInformationToken)GetProcAddress(NtDLL, "NtQueryInformationToken"); // has no header
  if (DynNTNtQueryInformationToken == NULL)
  {
    TRACE_E("Getting NtQueryInformationToken export failed!");
    return FALSE;
  }

  static int fIsUserAnAdmin = -1;  // cache

  if (-1 == fIsUserAnAdmin)
  {
    SID_IDENTIFIER_AUTHORITY authNT = SECURITY_NT_AUTHORITY;
    NTSTATUS                 Status;
    ULONG                    InfoLength;
    PTOKEN_GROUPS            TokenGroupList;
    ULONG                    GroupIndex;
    BOOL                     FoundAdmins;
    PSID                     AdminsDomainSid;
    HANDLE                   hUserToken;

    // Open the user's token
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hUserToken))
        return FALSE;

    // Create Admins domain sid.
    Status = AllocateAndInitializeSid(
               &authNT,
               2,
               SECURITY_BUILTIN_DOMAIN_RID,
               DOMAIN_ALIAS_RID_ADMINS,
               0, 0, 0, 0, 0, 0,
               &AdminsDomainSid
               );

    // Test if user is in the Admins domain

    // Get a list of groups in the token
    Status = DynNTNtQueryInformationToken(
                 hUserToken,               // Handle
                 TokenGroups,              // TokenInformationClass
                 NULL,                     // TokenInformation
                 0,                        // TokenInformationLength
                 &InfoLength               // ReturnLength
                 );

    if ((Status != STATUS_SUCCESS) && (Status != STATUS_BUFFER_TOO_SMALL))
    {
      FreeSid(AdminsDomainSid);
      CloseHandle(hUserToken);
      return FALSE;
    }

    TokenGroupList = (PTOKEN_GROUPS)GlobalAlloc(GPTR, InfoLength);

    if (TokenGroupList == NULL)
    {
      FreeSid(AdminsDomainSid);
      CloseHandle(hUserToken);
      return FALSE;
    }

    Status = DynNTNtQueryInformationToken(
                 hUserToken,        // Handle
                 TokenGroups,              // TokenInformationClass
                 TokenGroupList,           // TokenInformation
                 InfoLength,               // TokenInformationLength
                 &InfoLength               // ReturnLength
                 );

    if (!NT_SUCCESS(Status))
    {
      GlobalFree(TokenGroupList);
      FreeSid(AdminsDomainSid);
      CloseHandle(hUserToken);
      return FALSE;
    }


    // Search group list for Admins alias
    FoundAdmins = FALSE;

    for (GroupIndex=0; GroupIndex < TokenGroupList->GroupCount; GroupIndex++ )
    {
      if (EqualSid(TokenGroupList->Groups[GroupIndex].Sid, AdminsDomainSid))
      {
        FoundAdmins = TRUE;
        break;
      }
    }

    // Tidy up
    GlobalFree(TokenGroupList);
    FreeSid(AdminsDomainSid);
    CloseHandle(hUserToken);

    fIsUserAnAdmin = FoundAdmins ? 1 : 0;
  }

  return (BOOL)fIsUserAnAdmin;
}

*/

/* according to http://vcfaq.mvps.org/sdk/21.htm */
BOOL IsUserAdmin()
{
    HANDLE hToken = NULL;
    PSID pAdminSid = NULL;
    std::vector<BYTE> tokenGroups;
    PTOKEN_GROUPS pGroups = NULL;
    DWORD dwSize = 0;
    DWORD i;
    BOOL bSuccess;
    SID_IDENTIFIER_AUTHORITY siaNtAuth = SECURITY_NT_AUTHORITY;

    // get token handle
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return FALSE;

    bSuccess = FALSE;
    while (1)
    {
        const DWORD capacity = (DWORD)tokenGroups.size();
        pGroups = tokenGroups.empty() ? NULL :
                                         (PTOKEN_GROUPS)tokenGroups.data();
        if (GetTokenInformation(hToken, TokenGroups, pGroups, capacity,
                                &dwSize))
        {
            bSuccess = TRUE;
            break;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || dwSize == 0 ||
            dwSize <= capacity)
            break;
        tokenGroups.assign(dwSize, 0);
    }
    CloseHandle(hToken);
    if (!bSuccess)
        return FALSE;

    if (!AllocateAndInitializeSid(&siaNtAuth, 2,
                                  SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &pAdminSid))
        return FALSE;

    bSuccess = FALSE;
    for (i = 0; (i < pGroups->GroupCount) && !bSuccess; i++)
    {
        if (EqualSid(pAdminSid, pGroups->Groups[i].Sid))
            bSuccess = TRUE;
    }
    FreeSid(pAdminSid);

    return bSuccess;
}

struct CSrcSecurity // helper structure for keeping security info for MoveFile (the source disappears after the operation, its security info must be stored beforehand)
{
    std::vector<BYTE> Descriptor;
    DWORD SrcError;

    CSrcSecurity() { Clear(); }
    void Clear()
    {
        Descriptor.clear();
        SrcError = NO_ERROR;
    }
};

BOOL DoCopySecurity(DWORD* err, CSrcSecurity* srcSecurity,
                    const std::wstring& sourceNameW,
                    const std::wstring& targetNameW)
{
    // if the path ends with a space or dot, we must append '\\', otherwise
    // GetNamedSecurityInfo (and others) trim the spaces/dots and then work
    // with a different path
    std::wstring sourceNameSecW = MakeCopyWithBackslashIfNeededW(sourceNameW.c_str());
    std::wstring targetNameSecW = MakeCopyWithBackslashIfNeededW(targetNameW.c_str());

    std::vector<BYTE> descriptor;
    if (srcSecurity != NULL) // MoveFile: simply take over the security info
    {
        descriptor.swap(srcSecurity->Descriptor);
        *err = srcSecurity->SrcError;
        srcSecurity->Clear();
    }
    else // obtain the security info from the source
    {
        const FileResult readResult = GetWorkerFileSystem()->GetPathSecurity(
            sourceNameSecW.c_str(), descriptor);
        *err = readResult.success ? ERROR_SUCCESS : readResult.errorCode;
    }
    if (*err != ERROR_SUCCESS)
        return FALSE;

    const DWORD attr = GetWorkerFileSystem()->GetFileAttributes(targetNameSecW.c_str());
    const FileResult writeResult = GetWorkerFileSystem()->SetPathSecurity(
        targetNameSecW.c_str(), descriptor.data(), descriptor.size());
    *err = writeResult.success ? ERROR_SUCCESS : writeResult.errorCode;
    if (attr != INVALID_FILE_ATTRIBUTES)
        GetWorkerFileSystem()->SetFileAttributes(targetNameSecW.c_str(), attr);
    return writeResult.success;
}

// Forward declarations for wide versions (defined below)
DWORD CompressFileW(const wchar_t* fileName, DWORD attrs);
DWORD UncompressFileW(const wchar_t* fileName, DWORD attrs);
DWORD MyEncryptFileW(IWorkerObserver& observer, const wchar_t* fileName,
                     DWORD attrs, DWORD finalAttrs, CWorkerState& workerState, BOOL& cancelOper, BOOL preserveDate);
DWORD MyDecryptFileW(const wchar_t* fileName, DWORD attrs, BOOL preserveDate);
BOOL DoDeleteDirLinkAuxW(const wchar_t* nameDelLink, DWORD* err);

// 2026-08-25: the narrow CompressFile/UncompressFile/MyEncryptFile/
// MyDecryptFile(char*, ...) thin adapters were deleted - confirmed-dead (zero callers anywhere;
// pure core-internal helpers, no ABI entry at all). Their wide siblings below are the sole
// surviving implementations.

// --- Wide versions of compression/encryption helpers ---
// These are the real implementations; the ANSI versions above are thin wrappers.

DWORD CompressFileW(const wchar_t* fileName, DWORD attrs)
{
    DWORD ret = ERROR_SUCCESS;
    if (attrs & FILE_ATTRIBUTE_COMPRESSED)
        return ret;

    std::wstring fileNameCrFile = MakeCopyWithBackslashIfNeededW(fileName);

    BOOL attrsChange = FALSE;
    if (attrs & FILE_ATTRIBUTE_READONLY)
    {
        attrsChange = TRUE;
        GetWorkerFileSystem()->SetFileAttributes(fileNameCrFile.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    }
    ret = COperation::SetCompressionW(fileNameCrFile.c_str(), COMPRESSION_FORMAT_DEFAULT);
    if (attrsChange)
        GetWorkerFileSystem()->SetFileAttributes(fileNameCrFile.c_str(), attrs);
    return ret;
}

DWORD UncompressFileW(const wchar_t* fileName, DWORD attrs)
{
    DWORD ret = ERROR_SUCCESS;
    if ((attrs & FILE_ATTRIBUTE_COMPRESSED) == 0)
        return ret;

    std::wstring fileNameCrFile = MakeCopyWithBackslashIfNeededW(fileName);

    BOOL attrsChange = FALSE;
    if (attrs & FILE_ATTRIBUTE_READONLY)
    {
        attrsChange = TRUE;
        GetWorkerFileSystem()->SetFileAttributes(fileNameCrFile.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    }
    ret = COperation::SetCompressionW(fileNameCrFile.c_str(), COMPRESSION_FORMAT_NONE);
    if (attrsChange)
        GetWorkerFileSystem()->SetFileAttributes(fileNameCrFile.c_str(), attrs);
    return ret;
}

// Wrappers for use with COperation::WithPreservedFileTimeW
static DWORD DecryptFileOp(const wchar_t* path)
{
    const FileResult result = GetWorkerFileSystem()->DecryptPath(path);
    return result.success ? ERROR_SUCCESS : result.errorCode;
}

static DWORD EncryptFileOp(const wchar_t* path)
{
    const FileResult result = GetWorkerFileSystem()->EncryptPath(path);
    return result.success ? ERROR_SUCCESS : result.errorCode;
}

DWORD MyDecryptFileW(const wchar_t* fileName, DWORD attrs, BOOL preserveDate)
{
    DWORD ret = ERROR_SUCCESS;
    if ((attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0)
        return ret;

    std::wstring fileNameCrFile = MakeCopyWithBackslashIfNeededW(fileName);

    BOOL attrsChange = FALSE;
    if (attrs & FILE_ATTRIBUTE_READONLY)
    {
        attrsChange = TRUE;
        GetWorkerFileSystem()->SetFileAttributes(fileNameCrFile.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    }
    if (preserveDate)
    {
        ret = COperation::WithPreservedFileTimeW(fileNameCrFile.c_str(), attrs, DecryptFileOp);
    }
    else
    {
        const FileResult result = GetWorkerFileSystem()->DecryptPath(fileNameCrFile.c_str());
        if (!result.success)
            ret = result.errorCode;
    }
    if (attrsChange)
        GetWorkerFileSystem()->SetFileAttributes(fileNameCrFile.c_str(), attrs);
    return ret;
}

DWORD MyEncryptFileW(IWorkerObserver& observer, const wchar_t* fileName,
                     DWORD attrs, DWORD finalAttrs,
                     CWorkerState& workerState, BOOL& cancelOper, BOOL preserveDate)
{
    DWORD retEnc = ERROR_SUCCESS;
    cancelOper = FALSE;
    if (attrs & FILE_ATTRIBUTE_ENCRYPTED)
        return retEnc;

    std::wstring fileNameCrFile = MakeCopyWithBackslashIfNeededW(fileName);

    // SYSTEM attribute check — EncryptFile reports "access denied" for SYSTEM files
    if ((attrs & FILE_ATTRIBUTE_SYSTEM) && (finalAttrs & FILE_ATTRIBUTE_SYSTEM))
    {
        if (!workerState.EncryptSystemAll)
        {
            observer.WaitIfSuspended();
            if (observer.IsCancelled())
                return retEnc;

            if (workerState.SkipAllEncryptSystem)
                return retEnc;

            int ret = IDCANCEL;
            ret = observer.AskHiddenOrSystemById(IDS_CONFIRMSFILEENCRYPT, fileName, IDS_ENCRYPTSFILE);
            switch (ret)
            {
            case IDB_ALL:
                workerState.EncryptSystemAll = TRUE;
            case IDYES:
                break;

            case IDB_SKIPALL:
                workerState.SkipAllEncryptSystem = TRUE;
            case IDB_SKIP:
                return retEnc;

            case IDCANCEL:
            {
                cancelOper = TRUE;
                return retEnc;
            }
            }
        }
    }

    BOOL attrsChange = FALSE;
    if (attrs & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY))
    {
        attrsChange = TRUE;
        GetWorkerFileSystem()->SetFileAttributes(fileNameCrFile.c_str(), attrs & ~(FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY));
    }
    if (preserveDate)
    {
        retEnc = COperation::WithPreservedFileTimeW(fileNameCrFile.c_str(), attrs, EncryptFileOp);
    }
    else
    {
        const FileResult result = GetWorkerFileSystem()->EncryptPath(fileNameCrFile.c_str());
        if (!result.success)
            retEnc = result.errorCode;
    }
    if (attrsChange)
        GetWorkerFileSystem()->SetFileAttributes(fileNameCrFile.c_str(), attrs);
    return retEnc;
}

BOOL CheckFileOrDirADS(const std::wstring& fileNameW, BOOL isDir,
                       CQuadWord* adsSize,
                       std::vector<std::wstring>* streamNames,
                       BOOL* lowMemory, DWORD* winError,
                       DWORD bytesPerCluster, CQuadWord* adsOccupiedSpace,
                       BOOL* onlyDiscardableStreams)
{
    if (adsSize != NULL)
        adsSize->SetUI64(0);
    if (adsOccupiedSpace != NULL)
        adsOccupiedSpace->SetUI64(0);
    if (streamNames != NULL)
        streamNames->clear();
    if (lowMemory != NULL)
        *lowMemory = FALSE;
    if (winError != NULL)
        *winError = NO_ERROR;
    if (onlyDiscardableStreams != NULL)
        *onlyDiscardableStreams = TRUE;

    // If the path ends with a space or dot, append '\\'; otherwise the
    // filesystem API would trim it and inspect a different object.
    const std::wstring path = MakeCopyWithBackslashIfNeededW(fileNameW.c_str());
    HANDLE file = GetWorkerFileSystem()->CreateFile(
        path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, isDir ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL);
    const DWORD openError = GetLastError();
    HANDLES_ADD_EX(__otQuiet, file != INVALID_HANDLE_VALUE, __htFile,
                   __hoCreateFile, file, openError, TRUE);
    if (file == INVALID_HANDLE_VALUE)
    {
        if (winError != NULL)
            *winError = openError;
        return FALSE;
    }

    std::vector<FileStreamEntry> entries;
    const FileResult streamResult =
        GetWorkerFileSystem()->GetHandleStreams(file, entries);
    (void)CloseWorkerTrackedFile(file);
    if (!streamResult.success)
    {
        if (winError != NULL)
            *winError = streamResult.errorCode;
        return FALSE;
    }

    BOOL ret = FALSE;
    try
    {
        for (const FileStreamEntry& entry : entries)
        {
            if (entry.name != L"::$DATA") // ignore the default stream
            {
                ret = TRUE;
                if (adsSize != NULL)
                    adsSize->Value += entry.size;
                if (adsOccupiedSpace != NULL && bytesPerCluster != 0 &&
                    entry.size != 0)
                {
                    const uint64_t rounded =
                        ((entry.size - 1) / bytesPerCluster + 1) *
                        bytesPerCluster;
                    adsOccupiedSpace->Value += rounded;
                }

                if (onlyDiscardableStreams != NULL &&
                    (entry.name.size() < 29 ||
                     _wcsnicmp(entry.name.c_str(),
                               L":\x05Q30lsldxJoudresxAaaqpcawXc:", 29) != 0) &&
                    (entry.name.size() < 40 ||
                     _wcsnicmp(entry.name.c_str(),
                               L":{4c8cc155-6c1e-11d1-8e41-00c04fb9386d}:", 40) != 0) &&
                    (entry.name.size() < 9 ||
                     _wcsnicmp(entry.name.c_str(), L":KAVICHS:", 9) != 0))
                {
                    *onlyDiscardableStreams = FALSE;
                }

                if (streamNames != NULL)
                    streamNames->push_back(entry.name);
                else if (adsSize == NULL && adsOccupiedSpace == NULL &&
                         onlyDiscardableStreams == NULL)
                    break;
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        if (lowMemory != NULL)
            *lowMemory = TRUE;
        if (streamNames != NULL)
            streamNames->clear();
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    return ret;
}

BOOL DeleteAllADS(HANDLE file, const std::wstring& fileNameW)
{
    std::vector<FileStreamEntry> entries;
    const FileResult streamResult =
        GetWorkerFileSystem()->GetHandleStreams(file, entries);
    if (!streamResult.success)
    {
        TRACE_IW(L"DeleteAllADS(" << fileNameW.c_str()
                                   << L"): stream enumeration failed: "
                                   << GetErrorTextOwned(streamResult.errorCode).c_str());
        return FALSE;
    }

    for (const FileStreamEntry& entry : entries)
    {
        if (entry.name == L"::$DATA")
            continue;
        size_t start = !entry.name.empty() && entry.name[0] == L':' ? 1 : 0;
        const size_t end = entry.name.find(L':', start);
        if (end != std::wstring::npos && end > start)
        {
            std::wstring adsFullName = fileNameW;
            adsFullName.push_back(L':');
            adsFullName.append(entry.name, start, end - start);
            const FileResult deleteResult =
                GetWorkerFileSystem()->DeleteFile(adsFullName.c_str());
            if (!deleteResult.success)
            {
                TRACE_IW(L"DeleteAllADS(" << adsFullName.c_str()
                                           << L"): DeleteFile has failed: "
                                           << GetErrorTextOwned(deleteResult.errorCode).c_str());
                return FALSE;
            }
        }
    }
    return TRUE;
}

// 2026-08-25: the narrow CutADSNameSuffix(char*) was deleted - confirmed-dead
// (zero callers anywhere). CutADSNameSuffixW below is the sole surviving implementation.
void CutADSNameSuffixW(std::wstring& s)
{
    size_t pos = s.rfind(L':');
    if (pos != std::wstring::npos && _wcsicmp(s.c_str() + pos, L":$DATA") == 0)
        s.resize(pos);
}

// 2026-08-25: DoLongName (conversion to the extended-path \\?\ variant) was
// deleted - confirmed-dead (zero callers anywhere, not even a forward declaration elsewhere).
// Long-path support is handled by dynamically owned UTF-16 paths; no manual \\?\ prefixing is needed.

static FileResult SeekWorkerHandleExact(HANDLE file, const CQuadWord& offset)
{
    uint64_t position = 0;
    const FileResult result = GetWorkerFileSystem()->SeekHandle(
        file, static_cast<int64_t>(offset.Value), FILE_BEGIN, &position);
    if (!result.success)
        return result;
    return position == offset.Value ? FileResult::Ok() :
                                      FileResult::Error(ERROR_INVALID_FUNCTION);
}

static FileResult GetWorkerHandleSize(HANDLE file, CQuadWord& size)
{
    uint64_t value = 0;
    const FileResult result = GetWorkerFileSystem()->GetHandleFileSize(file, &value);
    if (result.success)
        size.Value = value;
    return result;
}

#define RETRYCOPY_TAIL_MINSIZE (32 * 1024) // at least two blocks of this size are verified at the end of the file tested in CheckTailOfOutFile(); afterwards the block size grows up to ASYNC_COPY_BUF_SIZE (if reading is fast enough); NOTE: must be <= ASYNC_COPY_BUF_SIZE
#define RETRYCOPY_TESTINGTIME 3000         // duration of the CheckTailOfOutFile() test in [ms]

void CheckTailOfOutFileShowErr(const wchar_t* txt, DWORD err = GetLastError())
{
    TRACE_IW(L"CheckTailOfOutFile(): " << txt << L" Error: " << GetErrorTextOwned(err).c_str());
}

BOOL CheckTailOfOutFile(CAsyncCopyParams* asyncPar, HANDLE in, HANDLE out, const CQuadWord& offset,
                        const CQuadWord& curInOffset, BOOL ignoreReadErrOnOut)
{
    // Direct Win32: all calls here (ReadFile, SetFilePointer, GetOverlappedResult) operate on
    // open HANDLEs for data verification — performance-sensitive, wrapping not needed
    char* bufIn = (char*)malloc(ASYNC_COPY_BUF_SIZE);
    char* bufOut = (char*)malloc(ASYNC_COPY_BUF_SIZE);

    DWORD startTime = GetTickCount();
    DWORD rutineStartTime = startTime;
    CQuadWord lastOffset = offset;
    int roundNum = 1;
    DWORD curBufSize = RETRYCOPY_TAIL_MINSIZE;
    DWORD lastRoundStartTime = 0;
    DWORD lastRoundBufSize = 0;
    BOOL searchLongLastingBlock = TRUE;
    BOOL ok;
    while (1)
    {
        DWORD roundStartTime = GetTickCount();
        ok = FALSE;
        CQuadWord start;
        start.Value = lastOffset.Value > curBufSize ? lastOffset.Value - curBufSize : 0;
        DWORD size = (DWORD)(lastOffset.Value - start.Value);
        if (size == 0)
        {
            ok = TRUE;
            break; // nothing to verify
        }
#ifdef WORKER_COPY_DEBUG_MSG
        TRACE_I("CheckTailOfOutFile(): check: " << start.Value << " - " << lastOffset.Value << ", size: " << size);
#endif // WORKER_COPY_DEBUG_MSG
        if (asyncPar == NULL)
        {
            const FileResult seekInResult = SeekWorkerHandleExact(in, start);
            if (seekInResult.success)
            { // set the 'start' offset in the input
                const FileResult seekOutResult = SeekWorkerHandleExact(out, start);
                if (seekOutResult.success)
                { // set the 'start' offset in the output
                    DWORD read;
                    const FileResult readOutResult = GetWorkerFileSystem()->ReadFromHandle(
                        out, bufOut, size, &read);
                    if (readOutResult.success && read == size)
                    { // read 'size' bytes into the output buffer (fails if opened without read access)
                        const FileResult readInResult = GetWorkerFileSystem()->ReadFromHandle(
                            in, bufIn, size, &read);
                        if (readInResult.success && read == size)
                        {                                         // read 'size' bytes into the input buffer
                            if (memcmp(bufIn, bufOut, size) == 0) // compare whether the input/output buffers match
                                ok = TRUE;
                            else
                                TRACE_I("CheckTailOfOutFile(): tail of target file is different from source file, tail without differences: " << (offset.Value - lastOffset.Value));
                        }
                        else
                            CheckTailOfOutFileShowErr(
                                L"Unable to read IN file.",
                                readInResult.success ? ERROR_HANDLE_EOF : readInResult.errorCode);
                    }
                    else
                    {
                        if (ignoreReadErrOnOut) // if the input file failed earlier, ignore that we cannot read the output (input was reopened, output has remained open)
                        {
                            CheckTailOfOutFileShowErr(
                                L"Unable to read OUT file, but it was not broken, so it's no problem.",
                                readOutResult.success ? ERROR_HANDLE_EOF : readOutResult.errorCode);
                            ok = TRUE;
                            break;
                        }
                        else
                            CheckTailOfOutFileShowErr(
                                L"Unable to read OUT file.",
                                readOutResult.success ? ERROR_HANDLE_EOF : readOutResult.errorCode);
                    }
                }
                else
                {
                    CheckTailOfOutFileShowErr(
                        L"Unable to set file pointer to start offset in OUT file.",
                        seekOutResult.errorCode);
                }
            }
            else
            {
                CheckTailOfOutFileShowErr(
                    L"Unable to set file pointer to start offset in IN file.",
                    seekInResult.errorCode);
            }
        }
        else
        {
            // asynchronously read the block starting at 'start' of length 'size' bytes from in and out, then compare
            DWORD readOut;
            FileResult readOutResult = GetWorkerFileSystem()->ReadFromHandleOverlapped(
                out, bufOut, size, asyncPar->InitOverlappedWithOffset(0, start));
            if (readOutResult.success || readOutResult.errorCode == ERROR_IO_PENDING)
                readOutResult = GetWorkerFileSystem()->CompleteHandleIo(
                    out, asyncPar->GetOverlapped(0), &readOut, true);
            if (readOutResult.success)
            {
                DWORD readIn;
                FileResult readInResult = GetWorkerFileSystem()->ReadFromHandleOverlapped(
                    in, bufIn, size, asyncPar->InitOverlappedWithOffset(1, start));
                if (readInResult.success || readInResult.errorCode == ERROR_IO_PENDING)
                    readInResult = GetWorkerFileSystem()->CompleteHandleIo(
                        in, asyncPar->GetOverlapped(1), &readIn, true);
                if (readInResult.success)
                {
                    if (readOut != size || readIn != size ||
                        memcmp(bufIn, bufOut, size) != 0) // compare whether the input/output buffers match
                    {
                        TRACE_I("CheckTailOfOutFile(): tail of target file is different from source file (async), tail without differences: " << (offset.Value - lastOffset.Value));
                    }
                    else
                        ok = TRUE;
                }
                else
                    CheckTailOfOutFileShowErr(
                        L"Unable to read IN file (async).", readInResult.errorCode);
            }
            else
            {
                if (ignoreReadErrOnOut) // if the input file failed earlier, ignore that we cannot read the output (input was reopened, output has remained open)
                {
                    CheckTailOfOutFileShowErr(
                        L"Unable to read OUT file (async), but it was not broken, so it's no problem.",
                        readOutResult.errorCode);
                    ok = TRUE;
                    break;
                }
                else
                    CheckTailOfOutFileShowErr(
                        L"Unable to read OUT file (async).", readOutResult.errorCode);
            }
        }
        if (!ok)
            break;
        lastOffset = start;
        DWORD curBufSizeBackup = curBufSize;
        if (roundNum > 1)
        {
            DWORD ti = GetTickCount();
            if (searchLongLastingBlock)
            {
                DWORD t1 = roundStartTime - lastRoundStartTime;
                DWORD t2 = ti - roundStartTime;
                if (roundNum == 2 && t1 > 300 && 10 * t2 < t1) // first iteration waits for the disk/network to be ready, shift the start time (so we spend the configured time reading instead of just waiting)
                {
#ifdef WORKER_COPY_DEBUG_MSG
                    TRACE_I("CheckTailOfOutFile(): detected long lasting first block, start time shifted by " << ((roundStartTime - startTime) / 1000.0) << " secs.");
#endif // WORKER_COPY_DEBUG_MSG
                    startTime = roundStartTime;
                }
                else
                {
                    if (t2 > 1000 && ((curBufSize * 10) / lastRoundBufSize) * t1 < t2)
                    { // unexpectedly long block read, likely waiting for disk "verification" or similar; ignore this block once so the overall check still behaves normally
                        searchLongLastingBlock = FALSE;
                        DWORD sh = t2 - ((unsigned __int64)curBufSize * t1) / lastRoundBufSize;
#ifdef WORKER_COPY_DEBUG_MSG
                        TRACE_I("CheckTailOfOutFile(): detected long lasting block, start time shifted by " << (sh / 1000.0) << " secs.");
#endif // WORKER_COPY_DEBUG_MSG
                        startTime += sh;
                    }
                }
            }
            if (ti - startTime > RETRYCOPY_TESTINGTIME)
                break; // we have been reading long enough; stop after the mandatory two rounds
            if (ti - roundStartTime < 300 && curBufSize < ASYNC_COPY_BUF_SIZE)
            { // when reading is fast enough, enlarge the buffer to avoid excessive reverse seeking (toward the beginning of the file)
                curBufSize *= 2;
                if (curBufSize > ASYNC_COPY_BUF_SIZE)
                    curBufSize = ASYNC_COPY_BUF_SIZE;
            }
        }
        roundNum++;
        lastRoundStartTime = roundStartTime;
        lastRoundBufSize = curBufSizeBackup;
    }

    if (ok && asyncPar == NULL) // reposition input/output to required offsets
    {
        const FileResult seekInResult = SeekWorkerHandleExact(in, curInOffset);
        if (!seekInResult.success)
        {
            CheckTailOfOutFileShowErr(
                L"Unable to set file pointer back to current offset in IN file.",
                seekInResult.errorCode);
            ok = FALSE;
        }
        const FileResult seekOutResult = ok ? SeekWorkerHandleExact(out, offset) :
                                              FileResult::Error(ERROR_SUCCESS);
        if (ok && !seekOutResult.success)
        {
            CheckTailOfOutFileShowErr(
                L"Unable to set file pointer back to current offset in OUT file.",
                seekOutResult.errorCode);
            ok = FALSE;
        }
    }
#ifdef WORKER_COPY_DEBUG_MSG
    if (!ok)
        TRACE_I("CheckTailOfOutFile(): aborting Retry...");
    else
    {
        TRACE_I("CheckTailOfOutFile(): " << (offset.Value - lastOffset.Value) / 1024.0 << " KB tested in " << (GetTickCount() - rutineStartTime) / 1000.0 << " secs (clear read time: " << (GetTickCount() - startTime) / 1000.0 << " secs).");
    }
#endif // WORKER_COPY_DEBUG_MSG
    free(bufIn);
    free(bufOut);
    return ok;
}

// copies ADS into the newly created file/directory
// returns FALSE only when cancelled; success + Skip both return TRUE; Skip sets 'skip'
// (when not NULL) to TRUE
// Direct Win32: all CreateFileW calls open ADS streams (path:streamname), not main files;
// all ReadFile/WriteFile/GetFileSize/SetFilePointer/SetEndOfFile/CloseHandle calls operate on
// open HANDLEs — COperation wrapping not applicable here
BOOL DoCopyADS(IWorkerObserver& observer, BOOL isDir,
               CQuadWord const& totalDone, CQuadWord& operDone, CQuadWord const& operTotal,
               CWorkerState& workerState, COperations* script, BOOL* skip, void* buffer,
               const std::wstring& sourceNameW,
               const std::wstring& targetNameW)
{
    BOOL doCopyADSRet = TRUE;
    BOOL lowMemory;
    DWORD adsWinError;
    std::vector<std::wstring> streamNames;
    BOOL skipped = FALSE;
    DWORD pendingWriteError = ERROR_SUCCESS;
    FileResult readIoResult = FileResult::Ok();
    CQuadWord lastTransferredFileSize, finalTransferredFileSize;
    script->GetTFSandResetTrSpeedIfNeeded(&lastTransferredFileSize);
    finalTransferredFileSize = lastTransferredFileSize;
    if (operTotal > operDone) // it should always be at least equal, but we play it safe...
        finalTransferredFileSize += (operTotal - operDone);

COPY_ADS_AGAIN:

    if (CheckFileOrDirADS(sourceNameW, isDir, NULL, &streamNames,
                          &lowMemory, &adsWinError, 0, NULL, NULL) &&
        !lowMemory && !streamNames.empty())
    {                                  // we have the list of ADS, let's try to copy them to the target file/directory
        // Always compute wide paths — supports Unicode and long paths
        const std::wstring& effectiveSourceW = sourceNameW;
        const std::wstring& effectiveTargetW = targetNameW;

        std::wstring sourceBase = effectiveSourceW;
        std::wstring targetBase = effectiveTargetW;
        SalPathRemoveBackslashW(sourceBase);
        SalPathRemoveBackslashW(targetBase);

        int bufferSize = script->RemovableSrcDisk || script->RemovableTgtDisk ? REMOVABLE_DISK_COPY_BUFFER : OPERATION_BUFFER;

        BOOL endProcessing = FALSE;
        CQuadWord operationDone;
        for (size_t i = 0; i < streamNames.size(); i++)
        {
            const std::wstring srcName = sourceBase + streamNames[i];
            const std::wstring tgtName = targetBase + streamNames[i];

        COPY_AGAIN_ADS:

            operationDone = CQuadWord(0, 0);
            int limitBufferSize = bufferSize;
            script->SetTFSandProgressSize(lastTransferredFileSize, totalDone + operDone, &limitBufferSize, bufferSize);

            BOOL doNextFile = FALSE;
            while (1)
            {
                // Direct Win32: opens ADS stream (path:streamname), not the main file — COperation wrapping N/A
                HANDLE in = GetWorkerFileSystem()->CreateFile(srcName.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                HANDLES_ADD_EX(__otQuiet, in != INVALID_HANDLE_VALUE, __htFile,
                               __hoCreateFile, in, GetLastError(), TRUE);
                if (in != INVALID_HANDLE_VALUE)
                {
                    CQuadWord fileSize;
                    const FileResult sizeResult = GetWorkerHandleSize(in, fileSize);
                    if (!sizeResult.success)
                    {
                        DWORD err = sizeResult.errorCode;
                        TRACE_EW(L"GetFileSize(some ADS of " << srcName.c_str() << L"): unexpected error: " << GetErrorTextOwned(err).c_str());
                        fileSize.SetUI64(0);
                    }

                    while (1)
                    {
                        // Direct Win32: opens ADS stream for writing — COperation wrapping N/A
                        HANDLE out = GetWorkerFileSystem()->CreateFile(tgtName.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                        DWORD createError = out == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
                        HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                       __hoCreateFile, out, createError, TRUE);

                        BOOL canOverwriteMACADSs = TRUE;

                    COPY_OVERWRITE:

                        if (out != INVALID_HANDLE_VALUE)
                        {
                            canOverwriteMACADSs = FALSE;

                            // if possible, pre-allocate the required space (avoids disk fragmentation and smooths writes to floppies)
                            BOOL wholeFileAllocated = FALSE;
                            if (fileSize > CQuadWord(limitBufferSize, 0) && // pointless to pre-allocate below the copy buffer size
                                fileSize < CQuadWord(0, 0x80000000))        // file size must be positive (otherwise seeking fails – values above 8 EB, so practically never)
                            {
                                BOOL fatal = TRUE;
                                BOOL ignoreErr = FALSE;
                                DWORD allocateError = ERROR_SUCCESS;
                                const FileResult allocateSeekResult =
                                    SeekWorkerHandleExact(out, fileSize);
                                if (allocateSeekResult.success)
                                {
                                    const FileResult allocateResult =
                                        GetWorkerFileSystem()->SetHandleEnd(out);
                                    if (allocateResult.success)
                                    {
                                        uint64_t rewindPosition = 0;
                                        const FileResult rewindResult =
                                            GetWorkerFileSystem()->SeekHandle(
                                                out, 0, FILE_BEGIN, &rewindPosition);
                                        if (rewindResult.success && rewindPosition == 0)
                                        {
                                            fatal = FALSE;
                                            wholeFileAllocated = TRUE;
                                        }
                                    }
                                    else
                                    {
                                        allocateError = allocateResult.errorCode;
                                        if (allocateError == ERROR_DISK_FULL)
                                            ignoreErr = TRUE; // low disk space
                                    }
                                }
                                else
                                    allocateError = allocateSeekResult.errorCode;
                                if (fatal)
                                {
                                    if (!ignoreErr)
                                    {
                                        DWORD err = allocateError != ERROR_SUCCESS ?
                                                        allocateError : GetLastError();
                                        TRACE_EW(L"DoCopyADS(): unable to allocate whole file size before copy operation, please report under what conditions this occurs! GetLastError(): " << GetErrorTextOwned(err).c_str());
                                    }

                                    // try truncating the file to zero so closing it does not trigger unnecessary writes
                                    (void)GetWorkerFileSystem()->SeekHandle(
                                        out, 0, FILE_BEGIN, NULL);
                                    (void)GetWorkerFileSystem()->SetHandleEnd(out);

                                    (void)CloseWorkerTrackedFile(out);
                                    out = INVALID_HANDLE_VALUE;
                                    const FileResult deleteResult = GetWorkerFileSystem()->DeleteFile(tgtName.c_str());
                                    if (deleteResult.success)
                                    {
                                        out = GetWorkerFileSystem()->CreateFile(tgtName.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                        createError = out == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
                                        HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                                       __hoCreateFile, out, createError, TRUE);
                                        if (out == INVALID_HANDLE_VALUE)
                                            goto CREATE_ERROR_ADS;
                                    }
                                    else
                                    {
                                        createError = deleteResult.errorCode;
                                        goto CREATE_ERROR_ADS;
                                    }
                                }
                            }

                            DWORD read;
                            DWORD written;
                            // Direct Win32: performance-critical ADS copy loop (ReadFile/WriteFile on open HANDLEs)
                            while (1)
                            {
                                readIoResult = GetWorkerFileSystem()->ReadFromHandle(
                                    in, buffer, limitBufferSize, &read);
                                if (readIoResult.success)
                                {
                                    if (read == 0)
                                        break;                                                     // EOF
                                    if (!script->ChangeSpeedLimit)                                 // if the speed limit can change, this is not a "suitable" place to wait
                                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                    if (observer.IsCancelled())
                                    {
                                    COPY_ERROR_ADS:

                                        if (in != NULL)
                                            (void)CloseWorkerTrackedFile(in);
                                        if (out != NULL)
                                        {
                                            if (wholeFileAllocated)
                                                (void)GetWorkerFileSystem()->SetHandleEnd(out); // otherwise on a floppy the remaining bytes would be written
                                            (void)CloseWorkerTrackedFile(out);
                                        }
                                        GetWorkerFileSystem()->DeleteFile(tgtName.c_str());
                                        doCopyADSRet = FALSE;
                                        endProcessing = TRUE;
                                        break;
                                    }

                                    while (1)
                                    {
                                        const FileResult writeIoResult =
                                            GetWorkerFileSystem()->WriteToHandle(
                                                out, buffer, read, &written);
                                        if (writeIoResult.success && read == written)
                                            break;
                                        pendingWriteError = writeIoResult.success ?
                                                                ERROR_DISK_FULL : writeIoResult.errorCode;

                                    WRITE_ERROR_ADS:

                                        DWORD err = pendingWriteError != ERROR_SUCCESS ?
                                                        pendingWriteError : GetLastError();
                                        pendingWriteError = ERROR_SUCCESS;

                                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                        if (observer.IsCancelled())
                                            goto COPY_ERROR_ADS;

                                        if (workerState.SkipAllFileADSWrite)
                                            goto SKIP_COPY_ADS;

                                        int ret;
                                        ret = IDCANCEL;
                                        if (err == NO_ERROR && read != written)
                                            err = ERROR_DISK_FULL;
                                        {
                                            std::wstring nameBufW = tgtName;
                                            CutADSNameSuffixW(nameBufW);
                                            ret = observer.AskFileErrorById(IDS_ERRORWRITINGADS, nameBufW.c_str(), err);
                                        }
                                        switch (ret)
                                        {
                                        case IDRETRY: // on a network we must reopen the handle; local access would not allow sharing
                                        {
                                            if (in == NULL && out == NULL)
                                            {
                                                GetWorkerFileSystem()->DeleteFile(tgtName.c_str());
                                                goto COPY_AGAIN_ADS;
                                            }
                                            if (out != NULL)
                                            {
                                                if (wholeFileAllocated)
                                                    (void)GetWorkerFileSystem()->SetHandleEnd(out);     // otherwise on a floppy the remaining bytes would be written
                                                (void)CloseWorkerTrackedFile(out); // close the invalid handle
                                            }
                                            out = GetWorkerFileSystem()->CreateFile(tgtName.c_str(), GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_ALWAYS,
                                                              FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                            HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                                           __hoCreateFile, out, GetLastError(), TRUE);
                                            if (out != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                                            {
                                                CQuadWord currentSize;
                                                const FileResult sizeResult =
                                                    GetWorkerHandleSize(out, currentSize);
                                                if (!sizeResult.success ||
                                                    currentSize < operationDone ||
                                                    !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone + CQuadWord(read, 0), FALSE))
                                                { // cannot determine the size or the file is too small; restart the entire copy
                                                    (void)CloseWorkerTrackedFile(in);
                                                    (void)CloseWorkerTrackedFile(out);
                                                    GetWorkerFileSystem()->DeleteFile(tgtName.c_str());
                                                    goto COPY_AGAIN_ADS;
                                                }
                                            }
                                            else // still cannot open; problem persists
                                            {
                                                out = NULL;
                                                goto WRITE_ERROR_ADS;
                                            }
                                            break;
                                        }

                                        case IDB_SKIPALL:
                                            workerState.SkipAllFileADSWrite = TRUE;
                                        case IDB_SKIP:
                                        {
                                        SKIP_COPY_ADS:

                                            if (in != NULL)
                                                (void)CloseWorkerTrackedFile(in);
                                            if (out != NULL)
                                            {
                                                if (wholeFileAllocated)
                                                    (void)GetWorkerFileSystem()->SetHandleEnd(out); // otherwise on a floppy the remaining bytes would be written
                                                (void)CloseWorkerTrackedFile(out);
                                            }
                                            GetWorkerFileSystem()->DeleteFile(tgtName.c_str());
                                            if (skip != NULL)
                                                *skip = TRUE;
                                            skipped = TRUE;
                                            endProcessing = TRUE;
                                            break;
                                        }

                                        case IDCANCEL:
                                            goto COPY_ERROR_ADS;
                                        }
                                        if (endProcessing)
                                            break;
                                    }
                                    if (endProcessing)
                                        break;
                                    if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                    if (observer.IsCancelled())
                                        goto COPY_ERROR_ADS;

                                    script->AddBytesToSpeedMetersAndTFSandPS(read, FALSE, bufferSize, &limitBufferSize);

                                    if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                    operationDone += CQuadWord(read, 0);
                                    observer.SetProgressWithoutSuspend(CaclProg(operDone + operationDone, operTotal),
                                                              CaclProg(totalDone + operDone + operationDone, script->TotalSize));

                                    if (script->ChangeSpeedLimit)                                  // speed limit may change; this is the right place to wait until the
                                    {                                                              // worker resumes and fetch a fresh copy buffer size
                                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                        script->GetNewBufSize(&limitBufferSize, bufferSize);
                                    }
                                }
                                else
                                {
                                READ_ERROR_ADS:

                                    DWORD err = readIoResult.errorCode;
                                    observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                    if (observer.IsCancelled())
                                        goto COPY_ERROR_ADS;

                                    if (workerState.SkipAllFileADSRead)
                                        goto SKIP_COPY_ADS;

                                    int ret = IDCANCEL;
                                    {
                                        std::wstring nameBufW = srcName;
                                        CutADSNameSuffixW(nameBufW);
                                        ret = observer.AskFileErrorById(IDS_ERRORREADINGADS, nameBufW.c_str(), err);
                                    }
                                    switch (ret)
                                    {
                                    case IDRETRY:
                                    {
                                        if (in != NULL)
                                            (void)CloseWorkerTrackedFile(in); // close the invalid handle

                                        in = GetWorkerFileSystem()->CreateFile(srcName.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                         OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                        HANDLES_ADD_EX(__otQuiet, in != INVALID_HANDLE_VALUE, __htFile,
                                                       __hoCreateFile, in, GetLastError(), TRUE);
                                        if (in != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                                        {
                                            CQuadWord currentSize;
                                            const FileResult sizeResult =
                                                GetWorkerHandleSize(in, currentSize);
                                            if (!sizeResult.success ||
                                                currentSize < operationDone ||
                                                !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone, TRUE))
                                            { // cannot obtain size or the file is too small; restart the entire operation
                                                (void)CloseWorkerTrackedFile(in);
                                                if (wholeFileAllocated)
                                                    (void)GetWorkerFileSystem()->SetHandleEnd(out); // otherwise on a floppy the remaining bytes would be written
                                                (void)CloseWorkerTrackedFile(out);
                                                GetWorkerFileSystem()->DeleteFile(tgtName.c_str());
                                                goto COPY_AGAIN_ADS;
                                            }
                                        }
                                        else // still cannot open; problem persists
                                        {
                                            readIoResult = FileResult::Error(GetLastError());
                                            in = NULL;
                                            goto READ_ERROR_ADS;
                                        }
                                        break;
                                    }
                                    case IDB_SKIPALL:
                                        workerState.SkipAllFileADSRead = TRUE;
                                    case IDB_SKIP:
                                        goto SKIP_COPY_ADS;
                                    case IDCANCEL:
                                        goto COPY_ERROR_ADS;
                                    }
                                }
                            }
                            if (endProcessing)
                                break;

                            if (wholeFileAllocated &&     // the entire target layout was pre-allocated
                                operationDone < fileSize) // and the source file shrank
                            {
                                const FileResult truncateResult =
                                    GetWorkerFileSystem()->SetHandleEnd(out);
                                if (!truncateResult.success) // trim it here
                                {
                                    written = read = 0;
                                    pendingWriteError = truncateResult.errorCode;
                                    goto WRITE_ERROR_ADS;
                                }
                            }

                            // commented out because it sets the time of the file/directory that owns the ADS instead of the ADS timestamps
                            //              FILETIME creation, lastAccess, lastWrite;
                            //              GetFileTime(in, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite);
                            //              SetFileTime(out, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite);

                            (void)CloseWorkerTrackedFile(in);
                            const FileResult closeOutResult =
                                CloseWorkerTrackedFile(out);
                            if (!closeOutResult.success) // even after a failed call we assume the handle is closed,
                            {                               // see https://forum.altap.cz/viewtopic.php?f=6&t=8455
                                in = out = NULL;            // (reports that the target file can be deleted, so its handle was not left open)
                                written = read = 0;
                                pendingWriteError = closeOutResult.errorCode;
                                goto WRITE_ERROR_ADS;
                            }

                            // commented out because it sets the attributes of the file/directory that owns the ADS instead of the ADS attributes
                            //              DWORD attr = DynGetFileAttributesW(srcName);
                            //              if (attr != INVALID_FILE_ATTRIBUTES) DynSetFileAttributesW(tgtName, attr);

                            operDone += operationDone;
                            lastTransferredFileSize += operationDone;
                            doNextFile = TRUE;
                        }
                        else
                        {
                        CREATE_ERROR_ADS:

                            DWORD err = createError;

                            // Macintosh compatibility: NTFS automatically creates ADS entries myFile:Afp_Resource and myFile:Afp_AfpInfo,
                            // overwrite them silently with the versions from the source file
                            if (canOverwriteMACADSs &&
                                (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) &&
                                (_wcsnicmp(streamNames[i].c_str(), L":Afp_Resource", 13) == 0 &&
                                     (streamNames[i][13] == 0 || streamNames[i][13] == L':') ||
                                 _wcsnicmp(streamNames[i].c_str(), L":Afp_AfpInfo", 12) == 0 &&
                                     (streamNames[i][12] == 0 || streamNames[i][12] == L':')))
                            {
                                out = GetWorkerFileSystem()->CreateFile(tgtName.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                                  FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                createError = out == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
                                HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                               __hoCreateFile, out, createError, TRUE);

                                canOverwriteMACADSs = FALSE;
                                goto COPY_OVERWRITE;
                            }

                            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                            if (observer.IsCancelled())
                                goto CANCEL_OPEN2_ADS;

                            if (workerState.SkipAllFileADSOpenOut)
                                goto SKIP_OPEN_OUT_ADS;

                            if (workerState.IgnoreAllADSOpenOutErr)
                                goto IGNORE_OPENOUTADS;

                            int ret;
                            ret = IDCANCEL;
                            {
                                std::wstring nameBufW = tgtName;
                                CutADSNameSuffixW(nameBufW);
                                ret = observer.AskADSOpenErrorById(IDS_ERROROPENINGADS, nameBufW.c_str(), err);
                            }
                            switch (ret)
                            {
                            case IDRETRY:
                                break;

                            case IDB_IGNOREALL:
                                workerState.IgnoreAllADSOpenOutErr = TRUE; // break is intentionally omitted here
                            case IDB_IGNORE:
                            {
                            IGNORE_OPENOUTADS:

                                (void)CloseWorkerTrackedFile(in);
                                operDone += fileSize;
                                lastTransferredFileSize += fileSize;
                                script->SetTFSandProgressSize(lastTransferredFileSize, totalDone + operDone);
                                doNextFile = TRUE;
                                break;
                            }

                            case IDB_SKIPALL:
                                workerState.SkipAllFileADSOpenOut = TRUE;
                            case IDB_SKIP:
                            {
                            SKIP_OPEN_OUT_ADS:

                                (void)CloseWorkerTrackedFile(in);
                                if (skip != NULL)
                                    *skip = TRUE;
                                skipped = TRUE;
                                endProcessing = TRUE;
                                break;
                            }

                            case IDCANCEL:
                            {
                            CANCEL_OPEN2_ADS:

                                (void)CloseWorkerTrackedFile(in);
                                doCopyADSRet = FALSE;
                                endProcessing = TRUE;
                                break;
                            }
                            }
                        }
                        if (doNextFile || endProcessing)
                            break;
                    }
                }
                else
                {
                    DWORD err = GetLastError();
                    observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                    if (observer.IsCancelled())
                    {
                        doCopyADSRet = FALSE;
                        endProcessing = TRUE;
                        break;
                    }

                    if (workerState.SkipAllFileADSOpenIn)
                        goto SKIP_OPEN_IN_ADS;

                    int ret;
                    ret = IDCANCEL;
                    {
                        std::wstring nameBufW = srcName;
                        CutADSNameSuffixW(nameBufW);
                        ret = observer.AskFileErrorById(IDS_ERROROPENINGADS, nameBufW.c_str(), err);
                    }
                    switch (ret)
                    {
                    case IDRETRY:
                        break;

                    case IDB_SKIPALL:
                        workerState.SkipAllFileADSOpenIn = TRUE;
                    case IDB_SKIP:
                    {
                    SKIP_OPEN_IN_ADS:

                        if (skip != NULL)
                            *skip = TRUE;
                        skipped = TRUE;
                        endProcessing = TRUE;
                        break;
                    }

                    case IDCANCEL:
                    {
                        doCopyADSRet = FALSE;
                        endProcessing = TRUE;
                        break;
                    }
                    }
                }
                if (doNextFile || endProcessing)
                    break;
            }
            if (endProcessing)
                break;
        }

    }
    else
    {
        if (adsWinError != NO_ERROR) // display the Windows error (low-memory warning goes only to TRACE_E)
        {
            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
                return FALSE;

            if (workerState.IgnoreAllADSReadErr)
                goto IGNORE_ADS;

            int ret;
            ret = IDCANCEL;
            ret = observer.AskADSReadError(sourceNameW.c_str(), GetErrorTextOwned(adsWinError).c_str());
            switch (ret)
            {
            case IDRETRY:
                goto COPY_ADS_AGAIN;

            case IDB_IGNOREALL:
                workerState.IgnoreAllADSReadErr = TRUE; // break is intentionally omitted here
            case IDB_IGNORE:
            {
            IGNORE_ADS:

                script->SetTFSandProgressSize(finalTransferredFileSize, totalDone + operTotal);

                observer.SetProgress(0, CaclProg(totalDone + operTotal, script->TotalSize));
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
        if (lowMemory)
            doCopyADSRet = FALSE; // lack of memory -> cancel the operation
    }
    if (doCopyADSRet && skipped)
    {
        script->SetTFSandProgressSize(finalTransferredFileSize, totalDone + operTotal);

        observer.SetProgress(0, CaclProg(totalDone + operTotal, script->TotalSize));
    }
    return doCopyADSRet;
}

// Wide. Its only caller was the SDK forwarder, so this widened in
// place rather than growing a narrow twin.
HANDLE SalCreateFileEx(const wchar_t* fileName, DWORD desiredAccess,
                       DWORD shareMode, DWORD flagsAndAttributes, BOOL* encryptionNotSupported)
{
    HANDLE out = SalLPCreateFile(fileName, desiredAccess, shareMode, NULL,
                                     CREATE_NEW, flagsAndAttributes, NULL);
    if (out == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (encryptionNotSupported != NULL && (flagsAndAttributes & FILE_ATTRIBUTE_ENCRYPTED))
        { // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
            out = SalLPCreateFile(fileName, desiredAccess, shareMode, NULL,
                                      CREATE_NEW, (flagsAndAttributes & ~(FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_READONLY)), NULL);
            if (out != INVALID_HANDLE_VALUE)
            {
                *encryptionNotSupported = TRUE;
                (void)GetWorkerFileSystem()->CloseFileHandle(out);
                out = INVALID_HANDLE_VALUE;
                if (!gFileSystem->DeleteFile(fileName).success) // XP and Vista ignore this scenario, so do the same (at worst warn user that a zero-length file was added on disk and cannot be deleted)
                    TRACE_IW(L"Unable to delete testing target file: " << fileName);
            }
        }
        if (err == ERROR_FILE_EXISTS || // check whether this is merely overwriting the DOS name
            err == ERROR_ALREADY_EXISTS ||
            err == ERROR_ACCESS_DENIED)
        {
            WIN32_FIND_DATAW data;
            HANDLE find = SalFindFirstFileHW(fileName, &data);
            if (find != INVALID_HANDLE_VALUE)
            {
                SalLPFindClose(find);
                if (err != ERROR_ACCESS_DENIED || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    // The find data was ALREADY wide; the two
                    // WideCharToMultiByte calls that stood here existed only so the
                    // names could be compared against a narrow 'fileName'. With the
                    // parameter wide they are gone, and so are the two
                    // AnsiToWide(origFullName) calls further down - four conversions
                    // deleted, not relocated.
                    const wchar_t* tgtName = SalPathFindFileNameW(fileName);
                    if (StrICmpW(tgtName, data.cAlternateFileName) == 0 && // match only for DOS name
                        StrICmpW(tgtName, data.cFileName) != 0)            // (full name differs)
                    {
                        // rename ("tidy up") the file/directory with the conflicting DOS name to a temporary 8.3 name (no extra DOS name needed)
                        std::wstring dir(fileName);
                        CutDirectoryW(dir);
                        SalPathAddBackslashW(dir);

                        std::wstring origFullName = dir;
                        SalPathAppendW(origFullName, data.cFileName);

                        DWORD num = (GetTickCount() / 10) % 0xFFF;
                        DWORD origFullNameAttr = GetWorkerFileSystem()->GetFileAttributes(origFullName.c_str());

                        // The narrow original kept a raw pointer INTO its buffer and
                        // sprintf'd through it each round. Rebuilding from 'dir' is
                        // equivalent and cannot dangle if the string reallocates.
                        std::wstring tmpName;
                        while (1)
                        {
                            wchar_t suffix[16];
                            swprintf(suffix, _countof(suffix), L"sal%03X", num++);
                            tmpName = dir + suffix;
                            if (SalMoveFile(origFullName.c_str(), tmpName.c_str()))
                                break;
                            DWORD e = GetLastError();
                            if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS)
                            {
                                tmpName.clear();
                                break;
                            }
                        }
                        if (!tmpName.empty()) // if we successfully "tidied" the conflicting file, try creating
                        {                     // the target file again, then restore the original name
                            out = SalLPCreateFile(fileName, desiredAccess, shareMode, NULL,
                                                      CREATE_NEW, flagsAndAttributes, NULL);
                            if (out == INVALID_HANDLE_VALUE && encryptionNotSupported != NULL &&
                                (flagsAndAttributes & FILE_ATTRIBUTE_ENCRYPTED))
                            { // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
                                out = SalLPCreateFile(fileName, desiredAccess, shareMode, NULL,
                                                          CREATE_NEW, (flagsAndAttributes & ~(FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_READONLY)), NULL);
                                if (out != INVALID_HANDLE_VALUE)
                                {
                                    *encryptionNotSupported = TRUE;
                                    (void)GetWorkerFileSystem()->CloseFileHandle(out);
                                    out = INVALID_HANDLE_VALUE;
                                    if (!gFileSystem->DeleteFile(fileName).success) // XP and Vista ignore this scenario, so do the same (at worst warn user that a zero-length file was added on disk and cannot be deleted)
                                        TRACE_EW(L"Unable to delete testing target file: " << fileName);
                                }
                            }
                            if (!SalMoveFile(tmpName.c_str(), origFullName.c_str()))
                            { // this apparently can happen; inexplicably, Windows creates a file named origFullName instead of fileName (the DOS name)
                                TRACE_IW(L"Unexpected situation in SalCreateFileEx(): unable to rename file from tmp-name to original long file name! " << origFullName);

                                if (out != INVALID_HANDLE_VALUE)
                                {
                                    (void)GetWorkerFileSystem()->CloseFileHandle(out);
                                    out = INVALID_HANDLE_VALUE;
                                    gFileSystem->DeleteFile(fileName);
                                    if (!SalMoveFile(tmpName.c_str(), origFullName.c_str()))
                                        TRACE_EW(L"Fatal unexpected situation in SalCreateFileEx(): unable to rename file from tmp-name to original long file name! " << origFullName);
                                }
                            }
                            else
                            {
                                if ((origFullNameAttr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                                    GetWorkerFileSystem()->SetFileAttributes(origFullName.c_str(), origFullNameAttr); // leave without extra handling or retry; not critical (normally toggles unpredictably)
                            }
                        }
                    }
                }
            }
        }
        if (out == INVALID_HANDLE_VALUE)
            SetLastError(err);
    }
    return out;
}

static FileResult SetWorkerHandleCompression(CAsyncCopyParams* asyncPar,
                                              HANDLE file, bool compress)
{
    IFileSystem* fileSystem = GetWorkerFileSystem();
    if (!asyncPar->UseAsyncAlg)
        return fileSystem->SetHandleCompression(file, compress);

    FileResult result = fileSystem->SetHandleCompressionOverlapped(
        file, compress, asyncPar->InitOverlapped(0));
    if (!result.success && result.errorCode != ERROR_IO_PENDING)
        return result;

    DWORD transferred = 0;
    return fileSystem->CompleteHandleIo(
        file, asyncPar->GetOverlapped(0), &transferred, true);
}

// Sets compression/encryption attributes on the target file.
// DeviceIoControl (FSCTL_SET_COMPRESSION) operates on open HANDLE — handle-based, wrapping not needed.
// Uses wide APIs (GetFileAttributesW, EncryptFileW, DecryptFileW, CreateFileW) when nameW is available.
void SetCompressAndEncryptedAttrs(DWORD attr, HANDLE* out, BOOL openAlsoForRead,
                                  BOOL* encryptionNotSupported, CAsyncCopyParams* asyncPar,
                                  const std::wstring& nameW)
{
    if (*out != INVALID_HANDLE_VALUE)
    {
        const std::wstring& effectiveNameW = nameW;

        DWORD err = NO_ERROR;
        DWORD curAttr = GetWorkerFileSystem()->GetFileAttributes(effectiveNameW.c_str());
        if ((curAttr == INVALID_FILE_ATTRIBUTES ||
             (attr & FILE_ATTRIBUTE_COMPRESSED) != (curAttr & FILE_ATTRIBUTE_COMPRESSED)) &&
            (attr & FILE_ATTRIBUTE_COMPRESSED) == 0)
        {
            const FileResult compressionResult =
                SetWorkerHandleCompression(asyncPar, *out, false);
            if (!compressionResult.success)
            {
                err = compressionResult.errorCode;
                TRACE_IW(L"SetCompressAndEncryptedAttrs(): Unable to set Compressed attribute for " << effectiveNameW.c_str() << L"! error=" << GetErrorTextOwned(err).c_str());
            }
        }
        if (curAttr == INVALID_FILE_ATTRIBUTES ||
            (attr & FILE_ATTRIBUTE_ENCRYPTED) != (curAttr & FILE_ATTRIBUTE_ENCRYPTED))
        { // SalCreateFileEx above likely failed
            err = NO_ERROR;
            (void)GetWorkerFileSystem()->CloseFileHandle(*out); // close the file; otherwise we cannot change its encrypted attribute
            if (attr & FILE_ATTRIBUTE_ENCRYPTED)
            {
                const FileResult encryptResult =
                    GetWorkerFileSystem()->EncryptPath(effectiveNameW.c_str());
                if (!encryptResult.success)
                {
                    err = encryptResult.errorCode;
                    if (encryptionNotSupported != NULL)
                        *encryptionNotSupported = TRUE;
                }
            }
            else
            {
                const FileResult decryptResult =
                    GetWorkerFileSystem()->DecryptPath(effectiveNameW.c_str());
                if (!decryptResult.success)
                    err = decryptResult.errorCode;
            }
            if (err != NO_ERROR)
                TRACE_IW(L"SetCompressAndEncryptedAttrs(): Unable to set Encrypted attribute for " << effectiveNameW.c_str() << L"! error=" << GetErrorTextOwned(err).c_str());
            // reopen the existing file to continue writing
            *out = GetWorkerFileSystem()->CreateFile(effectiveNameW.c_str(), GENERIC_WRITE | (openAlsoForRead ? GENERIC_READ : 0), 0, NULL, OPEN_ALWAYS,
                               asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            if (openAlsoForRead && *out == INVALID_HANDLE_VALUE)
                *out = GetWorkerFileSystem()->CreateFile(effectiveNameW.c_str(), GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                                   asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            if (*out == INVALID_HANDLE_VALUE) // still a problem: cannot reopen; delete it + report an error
            {
                err = GetLastError();
                GetWorkerFileSystem()->DeleteFile(effectiveNameW.c_str());
                SetLastError(err);
            }
        }
        if (*out != INVALID_HANDLE_VALUE && // only when reopening succeeded (and we did not delete the file)
            (curAttr == INVALID_FILE_ATTRIBUTES ||
             (attr & FILE_ATTRIBUTE_COMPRESSED) != (curAttr & FILE_ATTRIBUTE_COMPRESSED)) &&
            (attr & FILE_ATTRIBUTE_COMPRESSED) != 0)
        {
            const FileResult compressionResult =
                SetWorkerHandleCompression(asyncPar, *out, true);
            if (!compressionResult.success)
            {
                err = compressionResult.errorCode;
                TRACE_IW(L"SetCompressAndEncryptedAttrs(): Unable to set Compressed attribute for " << effectiveNameW.c_str() << L"! error=" << GetErrorTextOwned(err).c_str());
            }
        }
    }
}

void CorrectCaseOfTgtNameW(std::wstring& tgtName, BOOL dataRead, WIN32_FIND_DATAW* data)
{
    if (!dataRead)
    {
        HANDLE find = GetWorkerFileSystem()->FindFirstFile(tgtName.c_str(), data);
        if (find != INVALID_HANDLE_VALUE)
            SalLPFindClose(find);
        else
            return; // failed to read data for the target file; abort
    }
    const size_t len = wcslen(data->cFileName);
    if (tgtName.size() >= len &&
        _wcsicmp(tgtName.c_str() + tgtName.size() - len, data->cFileName) == 0)
        tgtName.replace(tgtName.size() - len, len, data->cFileName);
}

void SetTFSandPSforSkippedFile(COperation* op, CQuadWord& lastTransferredFileSize,
                               COperations* script, const CQuadWord& pSize)
{
    if (op->FileSize < COPY_MIN_FILE_SIZE)
    {
        lastTransferredFileSize += op->FileSize;                      // file size
        if (op->Size > COPY_MIN_FILE_SIZE)                            // should always be at least COPY_MIN_FILE_SIZE, but be safe...
            lastTransferredFileSize += op->Size - COPY_MIN_FILE_SIZE; // add the ADS size
    }
    else
        lastTransferredFileSize += op->Size; // file size + ADS
    script->SetTFSandProgressSize(lastTransferredFileSize, pSize);
}

// Synchronous copy loop. Handle I/O is routed through IFileSystem so injected
// failures carry their error value with the operation.
void DoCopyFileLoopOrig(HANDLE& in, HANDLE& out, void* buffer, int& limitBufferSize,
                        COperations* script, CWorkerState& workerState, BOOL wholeFileAllocated,
                        COperation* op, const CQuadWord& totalDone, BOOL& copyError, BOOL& skipCopy,
                        IWorkerObserver& observer, CQuadWord& operationDone, CQuadWord& fileSize,
                        int bufferSize, int& allocWholeFileOnStart, BOOL& copyAgain)
{
    int autoRetryAttemptsSNAP = 0;
    DWORD read;
    DWORD written;
    DWORD pendingWriteError = ERROR_SUCCESS;
    FileResult readIoResult = FileResult::Ok();
    while (1)
    {
        readIoResult = GetWorkerFileSystem()->ReadFromHandle(
            in, buffer, limitBufferSize, &read);
        if (readIoResult.success)
        {
            autoRetryAttemptsSNAP = 0;
            if (read == 0)
                break;                                                     // EOF
            if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }

            while (1)
            {
                const FileResult writeIoResult = GetWorkerFileSystem()->WriteToHandle(
                    out, buffer, read, &written);
                if (writeIoResult.success && read == written)
                {
                    break;
                }
                pendingWriteError = writeIoResult.success ?
                                        ERROR_DISK_FULL : writeIoResult.errorCode;

            WRITE_ERROR:

                DWORD err = pendingWriteError != ERROR_SUCCESS ?
                                pendingWriteError : GetLastError();
                pendingWriteError = ERROR_SUCCESS;

                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                if (observer.IsCancelled())
                {
                    copyError = TRUE; // goto COPY_ERROR
                    return;
                }

                if (workerState.SkipAllFileWrite)
                {
                    skipCopy = TRUE; // goto SKIP_COPY
                    return;
                }

                int ret;
                ret = IDCANCEL;
                if (err == NO_ERROR && read != written)
                    err = ERROR_DISK_FULL;
                ret = observer.AskFileErrorById(IDS_ERRORWRITINGFILE, op->TargetNameW.c_str(), err);
                switch (ret)
                {
                case IDRETRY: // on a network we must reopen the handle; local access forbids it due to sharing
                {
                    if (out != NULL)
                    {
                        if (wholeFileAllocated)
                            (void)GetWorkerFileSystem()->SetHandleEnd(out);     // otherwise on a floppy the remaining bytes would be written
                        (void)CloseWorkerTrackedFile(out); // close the invalid handle
                    }
                    out = op->OpenTargetFile(GENERIC_WRITE | GENERIC_READ, 0, OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN);
                    if (out != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                    {
                        CQuadWord currentSize;
                        const FileResult sizeResult =
                            GetWorkerHandleSize(out, currentSize);
                        if (!sizeResult.success ||                   // cannot obtain the size
                            currentSize < operationDone ||           // file is too small
                            wholeFileAllocated && currentSize > fileSize &&
                                currentSize > operationDone + CQuadWord(read, 0) || // pre-allocated file is too large (beyond the reserved size and beyond the written portion including the current block) = extra bytes were appended (allocWholeFileOnStart should be 0 /* need-test */)
                            !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone + CQuadWord(read, 0), FALSE))
                        { // restart the whole operation
                            (void)CloseWorkerTrackedFile(in);
                            (void)CloseWorkerTrackedFile(out);
                            op->DeleteTargetFile();
                            copyAgain = TRUE; // goto COPY_AGAIN;
                            return;
                        }
                    }
                    else // still cannot open; problem persists
                    {
                        out = NULL;
                        goto WRITE_ERROR;
                    }
                    break;
                }

                case IDB_SKIPALL:
                    workerState.SkipAllFileWrite = TRUE;
                case IDB_SKIP:
                {
                    skipCopy = TRUE; // goto SKIP_COPY
                    return;
                }

                case IDCANCEL:
                {
                    copyError = TRUE; // goto COPY_ERROR
                    return;
                }
                }
            }
            if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }

            script->AddBytesToSpeedMetersAndTFSandPS(read, FALSE, bufferSize, &limitBufferSize);

            if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            operationDone += CQuadWord(read, 0);
            observer.SetProgressWithoutSuspend(CaclProg(operationDone, op->Size),
                                                 CaclProg(totalDone + operationDone, script->TotalSize));

            if (script->ChangeSpeedLimit)                                  // speed limit may change; this is the right place to wait until the
            {                                                              // worker resumes and fetches a fresh copy buffer size
                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                script->GetNewBufSize(&limitBufferSize, bufferSize);
            }
        }
        else
        {
        READ_ERROR:

            DWORD err = readIoResult.errorCode;
            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }

            if (workerState.SkipAllFileRead)
            {
                skipCopy = TRUE; // goto SKIP_COPY
                return;
            }

            if (err == ERROR_NETNAME_DELETED && ++autoRetryAttemptsSNAP <= 3)
            { // on SNAP server reading sometimes randomly fails with ERROR_NETNAME_DELETED; Retry button reportedly helps, so trigger it automatically
                Sleep(100);
                goto RETRY_COPY;
            }

            int ret;
            ret = IDCANCEL;
            ret = observer.AskFileErrorById(IDS_ERRORREADINGFILE, op->SourceNameW.c_str(), err);
            switch (ret)
            {
            case IDRETRY:
            {
            RETRY_COPY:

                if (in != NULL)
                    (void)CloseWorkerTrackedFile(in); // close the invalid handle
                in = op->OpenSourceFile(FILE_FLAG_SEQUENTIAL_SCAN);
                if (in != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                {
                    CQuadWord currentSize;
                    const FileResult sizeResult = GetWorkerHandleSize(in, currentSize);
                    if (!sizeResult.success ||
                        currentSize < operationDone ||
                        !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone, TRUE))
                    { // cannot obtain the size or the file is too small; restart the whole operation
                        (void)CloseWorkerTrackedFile(in);
                        if (wholeFileAllocated)
                            (void)GetWorkerFileSystem()->SetHandleEnd(out); // otherwise on a floppy the remaining bytes would be written
                        (void)CloseWorkerTrackedFile(out);
                        op->DeleteTargetFile();
                        copyAgain = TRUE; // goto COPY_AGAIN;
                        return;
                    }
                }
                else // still cannot open; problem persists
                {
                    readIoResult = FileResult::Error(GetLastError());
                    in = NULL;
                    goto READ_ERROR;
                }
                break;
            }

            case IDB_SKIPALL:
                workerState.SkipAllFileRead = TRUE;
            case IDB_SKIP:
            {
                skipCopy = TRUE; // goto SKIP_COPY
                return;
            }

            case IDCANCEL:
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }
            }
        }
    }

    if (wholeFileAllocated) // we pre-allocated the complete file layout (meaning the allocation was useful; for example, the file cannot be empty)
    {
        if (operationDone < fileSize) // and the source file shrank
        {
            const FileResult truncateResult =
                GetWorkerFileSystem()->SetHandleEnd(out);
            if (!truncateResult.success) // trim it here
            {
                written = read = 0;
                pendingWriteError = truncateResult.errorCode;
                goto WRITE_ERROR;
            }
        }

        if (allocWholeFileOnStart == 0 /* need-test */)
        {
            CQuadWord curFileSize;
            const FileResult sizeResult = GetWorkerHandleSize(out, curFileSize);
            BOOL getFileSizeSuccess = sizeResult.success;
            if (getFileSizeSuccess && curFileSize == operationDone)
            { // verify that no extra bytes were appended at the end and that truncation works
                allocWholeFileOnStart = 1 /* yes */;
            }
            else
            {
#ifdef _DEBUG
                if (getFileSizeSuccess)
                {
                    TRACE_EW(L"DoCopyFileLoopOrig(): unable to allocate whole file size before copy operation, please report "
                             L"under what conditions this occurs! Error: different file sizes: target="
                             << NumberToStr(curFileSize) << L" bytes, source=" << NumberToStr(operationDone) << L" bytes");
                }
                else
                {
                    DWORD err = sizeResult.errorCode;
                    TRACE_EW(L"DoCopyFileLoopOrig(): unable to test result of allocation of whole file size before copy operation, please report "
                            L"under what conditions this occurs! GetFileSize("
                            << op->TargetNameW.c_str() << L") error: " << GetErrorTextOwned(err).c_str());
                }
#endif
                allocWholeFileOnStart = 2 /* no */; // skip further attempts on this target disk

                (void)CloseWorkerTrackedFile(out);
                out = NULL;
                op->ClearTargetReadOnly(); // if it somehow became read-only (should never happen), so we know how to handle it
                if (op->DeleteTargetFile())
                {
                    (void)CloseWorkerTrackedFile(in);
                    copyAgain = TRUE; // goto COPY_AGAIN;
                    return;
                }
                else
                {
                    written = read = 0;
                    goto WRITE_ERROR;
                }
            }
        }
    }
}

enum CCopy_BlkState
{
    cbsFree,       // block not in use
    cbsRead,       // reading source file blocks - completed (waiting to be written)
    cbsInProgress, // --- states below mean "waiting for the operation to finish" (completed states are above)
    cbsReading,    // reading source file blocks - requested (in progress)
    cbsTestingEOF, // checking the end of the source file
    cbsWriting,    // writing the block to the target file
    cbsDiscarded,  // attempted to read beyond the end of the source file (should only return error: EOF)
};

enum CCopy_ForceOp
{
    fopNotUsed, // free to read or write as needed
    fopReading, // forced to read
    fopWriting  // forced to write
};

struct CCopy_Context
{
    CAsyncCopyParams* AsyncPar;

    CCopy_ForceOp ForceOp;        // TRUE = must read now, FALSE = must write now
    BOOL ReadingDone;             // TRUE = the source file has been fully read
    CCopy_BlkState BlockState[8]; // block state
    DWORD BlockDataLen[8];        // for each block: expected data (cbsReading + cbsTestingEOF), valid data (cbsWriting)
    CQuadWord BlockOffset[8];     // for each block: block offset in the source/target file (also stored in the 'AsyncPar' OVERLAPPED)
    DWORD BlockTime[8];           // for each block: "time" when the last async operation in this block started
    DWORD CurTime;                // "time" counter for 'BlockTime', handles wrap-around (though unlikely)
    int FreeBlocks;               // current number of free blocks (cbsFree)
    int FreeBlockIndex;           // candidate index of a free block (cbsFree); must be verified
    int ReadingBlocks;            // current number of blocks being read(cbsReading and cbsTestingEOF)
    int WritingBlocks;            // current number of blocks being written (cbsWriting)
    CQuadWord ReadOffset;         // offset for reading the next block from the source file (previous one is already in progress)
    CQuadWord WriteOffset;        // offset for writing the next block to the target file (previous one is already in progress)
    int AutoRetryAttemptsSNAP;    // number of automatic Retry attempts (max 3): SNAP servers sporadically return ERROR_NETNAME_DELETED while reading, Retry button reportedly helps, so trigger it automatically

    // selected DoCopyFileLoopAsync parameters to avoid passing a long argument list everywhere
    CWorkerState* DlgData;
    COperation* Op;
    IWorkerObserver* Observer;
    HANDLE* In;
    HANDLE* Out;
    BOOL WholeFileAllocated;
    COperations* Script;
    CQuadWord* OperationDone;
    const CQuadWord* TotalDone;
    const CQuadWord* LastTransferredFileSize;
    BOOL DisableNetworkLocalBuffering;

    CCopy_Context(CAsyncCopyParams* asyncPar, int numOfBlocks, CWorkerState* workerState, COperation* op,
                  IWorkerObserver& observer, HANDLE* in, HANDLE* out, BOOL wholeFileAllocated, COperations* script,
                  CQuadWord* operationDone, const CQuadWord* totalDone, const CQuadWord* lastTransferredFileSize,
                  BOOL disableNetworkLocalBuffering)
    {
        AsyncPar = asyncPar;
        ForceOp = fopNotUsed;
        ReadingDone = FALSE;
        CurTime = 0;
        for (int i = 0; i < _countof(BlockState); i++)
            BlockState[i] = cbsFree;
        memset(BlockDataLen, 0, sizeof(BlockDataLen));
        memset(BlockOffset, 0, sizeof(BlockOffset));
        memset(BlockTime, 0, sizeof(BlockTime));
        FreeBlocks = numOfBlocks;
        FreeBlockIndex = 0;
        ReadingBlocks = 0;
        WritingBlocks = 0;
        ReadOffset.SetUI64(0);
        WriteOffset.SetUI64(0);
        AutoRetryAttemptsSNAP = 0;

        DlgData = workerState;
        Op = op;
        Observer = &observer;
        In = in;
        Out = out;
        WholeFileAllocated = wholeFileAllocated;
        Script = script;
        OperationDone = operationDone;
        TotalDone = totalDone;
        LastTransferredFileSize = lastTransferredFileSize;
        DisableNetworkLocalBuffering = disableNetworkLocalBuffering;
    }

    BOOL IsOperationDone(int numOfBlocks)
    {
        return ReadingDone && FreeBlocks == numOfBlocks;
    }

    BOOL StartReading(int blkIndex, DWORD readSize, DWORD* err, BOOL testEOF);
    BOOL StartWriting(int blkIndex, DWORD* err);
    int FindBlock(CCopy_BlkState state);
    void FreeBlock(int blkIndex);
    void DiscardBlocksBehindEOF(const CQuadWord& fileSize, int excludeIndex);
    void GetNewFileSize(const wchar_t* fileName, HANDLE file, CQuadWord* fileSize, const CQuadWord& minFileSize);

    BOOL HandleReadingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain);
    BOOL HandleWritingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain,
                          const CQuadWord& allocFileSize, const CQuadWord& maxWriteOffset);

    // interrupts any pending asynchronous operations
    void CancelOpPhase1();
    // ensures that all asynchronous operations have really finished + positions the pointer at the end of the contiguous
    // portion of the target file so the file is truncated correctly (before a possible closing and deletion)
    // WARNING: frees unnecessary blocks; only those with data read from the input file remain, and they still
    //          follow WriteOffset (usable for retry)
    void CancelOpPhase2(int errBlkIndex);
    BOOL RetryCopyReadErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain);
    BOOL RetryCopyWriteErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain, const CQuadWord& allocFileSize,
                           const CQuadWord& maxWriteOffset);
    BOOL HandleSuspModeAndCancel(BOOL* copyError);
};

BOOL DisableLocalBuffering(CAsyncCopyParams* asyncPar, HANDLE file, DWORD* err)
{
    // Direct Win32: handle-based NT IOCTL for network buffering optimization — wrapping not needed
    CALL_STACK_MESSAGE1("DisableLocalBuffering()");
    if (DynNtFsControlFile != NULL) // "always true"
    {
        IO_STATUS_BLOCK ioStatus;
        ResetEvent(asyncPar->Overlapped[0].hEvent);
        ULONG status = DynNtFsControlFile(file, asyncPar->Overlapped[0].hEvent, NULL,
                                          0, &ioStatus, 0x00140390 /* IOCTL_LMR_DISABLE_LOCAL_BUFFERING */,
                                          NULL, 0, NULL, 0);
        if (status == STATUS_PENDING) // must wait for the operation to finish; it runs asynchronously
        {
            CALL_STACK_MESSAGE1("DisableLocalBuffering(): STATUS_PENDING");
            WaitForSingleObject(asyncPar->Overlapped[0].hEvent, INFINITE);
            status = ioStatus.Status;
        }
        if (status == 0 /* STATUS_SUCCESS */)
            return TRUE;
        *err = LsaNtStatusToWinError(status);
    }
    else
        *err = ERROR_INVALID_FUNCTION;
    return FALSE;
}

BOOL CCopy_Context::StartReading(int blkIndex, DWORD readSize, DWORD* err, BOOL testEOF)
{
#ifdef ASYNC_COPY_DEBUG_MSG
    char sss[1000];
    sprintf(sss, "ReadFile: %d 0x%08X 0x%08X", blkIndex, ReadOffset.LoDWord, readSize);
    TRACE_I(sss);
#endif // ASYNC_COPY_DEBUG_MSG

    const FileResult readResult = GetWorkerFileSystem()->ReadFromHandleOverlapped(
        *In, AsyncPar->Buffers[blkIndex], readSize,
        AsyncPar->InitOverlappedWithOffset(blkIndex, ReadOffset));
    if (!readResult.success && readResult.errorCode != ERROR_IO_PENDING)
    { // a read error occurred; handle it
        *err = readResult.errorCode;
        if (*err == ERROR_HANDLE_EOF) // synchronously reported EOF; convert it to an asynchronously reported EOF
            AsyncPar->SetOverlappedToEOF(blkIndex, ReadOffset);
        else
            return FALSE;
    }
    // if the read was completed synchronously (or via cache, which we cannot detect),
    // we must write something now; otherwise writing may idle and slow down the whole operation
    BOOL opCompleted = HasOverlappedIoCompleted(AsyncPar->GetOverlapped(blkIndex));
    ForceOp = opCompleted ? fopWriting : fopNotUsed;

#ifdef ASYNC_COPY_DEBUG_MSG
    TRACE_I("ReadFile result: " << (opCompleted ? "DONE" : "ASYNC"));
#endif // ASYNC_COPY_DEBUG_MSG

    if (opCompleted && !Script->ChangeSpeedLimit)                   // when the speed limit can change, this is not a suitable wait point
        Observer->WaitIfSuspended(); // if we should be in suspend mode, wait ...
    if (Observer->IsCancelled())
    {
        *err = ERROR_CANCELLED;
        return FALSE; // cancellation will be handled in the error-handling
    }

    BlockOffset[blkIndex] = ReadOffset;
    BlockDataLen[blkIndex] = readSize;
    if (!testEOF) // block was cbsFree before calling this method
    {
        ReadOffset.Value += readSize;
        BlockState[blkIndex] = cbsReading;
    }
    else
        BlockState[blkIndex] = cbsTestingEOF;
    BlockTime[blkIndex] = CurTime++;
    FreeBlocks--;
    ReadingBlocks++;
    return TRUE;
}

BOOL CCopy_Context::StartWriting(int blkIndex, DWORD* err)
{
#ifdef ASYNC_COPY_DEBUG_MSG
    char sss[1000];
    sprintf(sss, "WriteFile: %d 0x%08X 0x%08X", blkIndex, WriteOffset.LoDWord, BlockDataLen[blkIndex]);
    TRACE_I(sss);
#endif // ASYNC_COPY_DEBUG_MSG

    const FileResult writeResult = GetWorkerFileSystem()->WriteToHandleOverlapped(
        *Out, AsyncPar->Buffers[blkIndex], BlockDataLen[blkIndex],
        AsyncPar->InitOverlappedWithOffset(blkIndex, WriteOffset));
    if (!writeResult.success && writeResult.errorCode != ERROR_IO_PENDING)
    { // a write error occurred; handle it
        *err = writeResult.errorCode;
        return FALSE;
    }
    // if the write was completed synchronously (or via cache, which we cannot detect),
    // we must read something now; otherwise reading may idle and slow down the whole operation
    BOOL opCompleted = HasOverlappedIoCompleted(AsyncPar->GetOverlapped(blkIndex));
    ForceOp = !ReadingDone && opCompleted ? fopReading : fopNotUsed;

#ifdef ASYNC_COPY_DEBUG_MSG
    TRACE_I("WriteFile result: " << (opCompleted ? "DONE" : "ASYNC"));
#endif // ASYNC_COPY_DEBUG_MSG

    if (opCompleted && !Script->ChangeSpeedLimit)                   // when the speed limit can change, this is not a suitable wait point
        Observer->WaitIfSuspended(); // if we should be in suspend mode, wait ...
    if (Observer->IsCancelled())
    {
        *err = ERROR_CANCELLED;
        return FALSE; // cancellation will be handled in the error-handling
    }

    WriteOffset.Value += BlockDataLen[blkIndex];
    BlockState[blkIndex] = cbsWriting; // block was cbsRead before calling this method
    BlockTime[blkIndex] = CurTime++;
    WritingBlocks++;
    return TRUE;
}

int CCopy_Context::FindBlock(CCopy_BlkState state)
{
    for (int i = 0; i < _countof(BlockState); i++)
        if (BlockState[i] == state)
            return i;
    TRACE_C("CCopy_Context::FindBlock(): unable to find block with required state (" << (int)state << ").");
    return -1; // dead code, only for the compiler
}

void CCopy_Context::FreeBlock(int blkIndex)
{
    if (BlockState[blkIndex] == cbsReading || BlockState[blkIndex] == cbsTestingEOF)
        ReadingBlocks--;
    if (BlockState[blkIndex] == cbsWriting)
        WritingBlocks--;
    BlockState[blkIndex] = cbsFree;
    FreeBlockIndex = blkIndex;
    FreeBlocks++;
}

void CCopy_Context::DiscardBlocksBehindEOF(const CQuadWord& fileSize, int excludeIndex)
{
    for (int i = 0; i < _countof(BlockState); i++)
    {
        if (i == excludeIndex)
            continue;
        CCopy_BlkState st = BlockState[i];
        if ((st == cbsRead || st == cbsReading) && BlockOffset[i] >= fileSize)
        {
            if (st == cbsRead) // discard data read beyond the end of the file; they are useless
                FreeBlock(i);
            else
            {
                BlockState[i] = cbsDiscarded; // reading past the end of the file is pointless; no reason to adjust BlockTime
                ReadingBlocks--;
            }
        }
    }
}

void CCopy_Context::GetNewFileSize(const wchar_t* fileName, HANDLE file, CQuadWord* fileSize, const CQuadWord& minFileSize)
{
    const FileResult sizeResult = GetWorkerHandleSize(file, *fileSize);
    if (!sizeResult.success)
    {
        DWORD err = sizeResult.errorCode;
        TRACE_EW(L"CCopy_Context::GetNewFileSize(): GetFileSize(" << fileName << L"): unexpected error: " << GetErrorTextOwned(err).c_str());
        *fileSize = minFileSize;
    }
    else
    {
        if (*fileSize < minFileSize) // if GetFileSize happened to return a shorter length than already read
            *fileSize = minFileSize;
    }
}

void CCopy_Context::CancelOpPhase1()
{
    const FileResult cancelInResult = GetWorkerFileSystem()->CancelHandleIo(*In);
    if (!cancelInResult.success)
    {
        DWORD err = cancelInResult.errorCode;
        TRACE_EW(L"CCopy_Context::CancelOpPhase1(): CancelIo(IN) failed, error: " << GetErrorTextOwned(err).c_str());
    }
    const FileResult cancelOutResult = *Out != NULL ?
        GetWorkerFileSystem()->CancelHandleIo(*Out) : FileResult::Ok();
    if (!cancelOutResult.success)
    {
        DWORD err = cancelOutResult.errorCode;
        TRACE_EW(L"CCopy_Context::CancelOpPhase1(): CancelIo(OUT) failed, error: " << GetErrorTextOwned(err).c_str());
    }
}

void CCopy_Context::CancelOpPhase2(int errBlkIndex)
{
    // NOTE: errBlkIndex == -1 for errors when issuing an async reading (no block assigned),
    //       for errors while truncating the file after the main copy loop finished (no block assigned),
    //       or for Cancel in the progress dialog (no block assigned)

    DWORD bytes;
    for (int i = 0; i < _countof(BlockState); i++)
    {
        if (BlockState[i] > cbsInProgress)
        { // GetOverlappedResult should return results immediately because CancelIo() was called for both files
            const FileResult completionResult = GetWorkerFileSystem()->CompleteHandleIo(
                BlockState[i] == cbsWriting ? *Out : *In,
                AsyncPar->GetOverlapped(i), &bytes, true);
            if (completionResult.success)
            {
                if (BlockState[i] == cbsReading && BlockDataLen[i] == bytes) // fully read -> convert to cbsRead block
                {
                    BlockState[i] = cbsRead;
                    ReadingBlocks--;
                }
                else
                {
                    if (BlockState[i] == cbsWriting && BlockDataLen[i] == bytes) // fully written -> convert to cbsRead block (might write again, so keep it)
                    {
                        BlockState[i] = cbsRead;
                        WritingBlocks--;
                    }
                }
            }
            else
            {
                DWORD err = completionResult.errorCode;
                if (i != errBlkIndex &&             // already reporting the error for this block; no need to repeat it in TRACE
                    err != ERROR_OPERATION_ABORTED) // not an error, merely reports cancellation (CancelIo() call)
                {                                   // log issues in other blocks, usually harmless and best ignored
                    TRACE_IW(L"CCopy_Context::CancelOpPhase2(): GetOverlappedResult(" << (BlockState[i] == cbsWriting ? L"OUT" : L"IN") << L", " << i << L") returned error: " << GetErrorTextOwned(err).c_str());
                }
            }
            switch (BlockState[i])
            {
            case cbsReading:    // not fully read
            case cbsTestingEOF: // EOF test not finished
            case cbsDiscarded:
                FreeBlock(i);
                break;

            case cbsWriting:                      // unwritten block
                if (WriteOffset > BlockOffset[i]) // lower WriteOffset if needed
                    WriteOffset = BlockOffset[i];
                BlockState[i] = cbsRead; // not fully written but already read -> convert to cbsRead block (might write again, so keep it)
                WritingBlocks--;
                break;
            }
        }
    }

    ReadOffset = WriteOffset; // determine how far we have contiguous data from the offset where writing should resume
    for (int i = 0; i < _countof(BlockState); i++)
    {
        if (BlockState[i] == cbsRead && BlockOffset[i] == ReadOffset) // block read directly after ReadOffset
        {
            ReadOffset.Value += BlockDataLen[i];
            i = -1; // start the search from the beginning again (with 8 blocks this is affordable, max 36 loop iterations)
        }
    }

    // drop blocks that are already written or too far ahead (not contiguous)
    // so they can be read again later
    for (int i = 0; i < _countof(BlockState); i++)
        if (BlockState[i] == cbsRead && (BlockOffset[i] < WriteOffset || BlockOffset[i] > ReadOffset))
            FreeBlock(i);

    // when deleting the target file, set the file pointer to the end of the written portion;
    // the caller will truncate it with SetEndOfFile before deletion (otherwise zeroes might be written
    // from the end of the written part to the end of the pre-allocated file - pre-allocation is
    // used to prevent fragmentation)
    if (*Out != NULL) // only if the target file was not closed meanwhile
    {
        const FileResult seekResult = SeekWorkerHandleExact(*Out, WriteOffset);
        if (!seekResult.success)
        {
            DWORD err = seekResult.errorCode;
            TRACE_EW(L"CCopy_Context::CancelOpPhase2(): unable to set file pointer in OUT file, error: " << GetErrorTextOwned(err).c_str());
        }
    }
}

BOOL CCopy_Context::RetryCopyReadErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain)
{
    if (*In != NULL)
        (void)CloseWorkerTrackedFile(*In); // close the invalid handle
    *In = Op->OpenSourceFile(AsyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN);
    if (*In != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
    {
        CQuadWord size;
        const FileResult sizeResult = GetWorkerHandleSize(*In, size);
        if (sizeResult.success && size >= ReadOffset)
        { // size obtained and the file is large enough
            // if the source is on a network: disable local client-side in-memory caching
            // http://msdn.microsoft.com/en-us/library/ee210753%28v=vs.85%29.aspx
            //
            // using Overlapped[0].hEvent from AsyncPar is OK; nothing is "in-progress" now, the event is unused
            // (but WARNING: for example Buffers[0] from AsyncPar may still be in use)
            if (DisableNetworkLocalBuffering && (Op->OpFlags & OPFL_SRCPATH_IS_NET) && !DisableLocalBuffering(AsyncPar, *In, err))
                TRACE_EW(L"CCopy_Context::RetryCopyReadErr(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network source file: " << Op->SourceNameW.c_str() << L", error: " << GetErrorTextOwned(*err).c_str());
            // using Overlapped[0 and 1].hEvent and Overlapped[0 and 1] from AsyncPar is OK; nothing is
            // "in-progress", the event nor the overlapped structures are used (but WARNING: for example Buffers[0]
            // from AsyncPar may still be in use)
            if (CheckTailOfOutFile(AsyncPar, *In, *Out, WriteOffset, WriteOffset, TRUE))
            {
                ForceOp = ReadOffset > WriteOffset ? fopWriting : fopNotUsed; // if the read side is ahead, resume with writing
                *OperationDone = WriteOffset;
                Script->SetTFSandProgressSize(*LastTransferredFileSize + *OperationDone, *TotalDone + *OperationDone);
                Observer->SetProgressWithoutSuspend(CaclProg(*OperationDone, Op->Size),
                                                 CaclProg(*TotalDone + *OperationDone, Script->TotalSize));
                return TRUE; // success: proceed with retry
            }
        }
        // cannot obtain the size, the file is too small, or the last written part differs from the source -> restart from scratch
        (void)CloseWorkerTrackedFile(*In);
        if (WholeFileAllocated)
            (void)GetWorkerFileSystem()->SetHandleEnd(*Out); // otherwise on a floppy the remaining bytes would be written
        (void)CloseWorkerTrackedFile(*Out);
        Op->DeleteTargetFile();
        *copyAgain = TRUE; // goto COPY_AGAIN;
        return FALSE;
    }
    else // still cannot open; problem persists
    {
        *err = GetLastError();
        *In = NULL;
        *errAgain = TRUE; // goto READ_ERROR;
        return FALSE;
    }
}

BOOL CCopy_Context::HandleReadingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain)
{
    // NOTE: blkIndex == -1 when the async read request failed (no block assigned)

    CancelOpPhase1();

    while (1)
    {
        Observer->WaitIfSuspended(); // if we should be in suspend mode, wait ...
        if (Observer->IsCancelled())
        {
            CancelOpPhase2(blkIndex);
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }

        if (DlgData->SkipAllFileRead)
        {
            CancelOpPhase2(blkIndex);
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        int ret = IDCANCEL;
        if (err == ERROR_NETNAME_DELETED && ++AutoRetryAttemptsSNAP <= 3)
        { // SNAP servers occasionally return ERROR_NETNAME_DELETED while reading; Retry button reportedly helps, so trigger it automatically
            Sleep(100);
            ret = IDRETRY;
        }
        else
        {
            ret = Observer->AskFileErrorById(IDS_ERRORREADINGFILE, Op->SourceNameW.c_str(), err);
        }
        CancelOpPhase2(blkIndex);
        BOOL errAgain = FALSE;
        switch (ret)
        {
        case IDRETRY:
        {
            if (RetryCopyReadErr(&err, copyAgain, &errAgain))
                return TRUE; // retry
            else
            {
                if (errAgain)
                    break;    // same problem again; repeat the message
                return FALSE; // copyAgain==TRUE, goto COPY_AGAIN;
            }
        }

        case IDB_SKIPALL:
            DlgData->SkipAllFileRead = TRUE;
        case IDB_SKIP:
        {
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        case IDCANCEL:
        {
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }
        }
        if (errAgain)
            continue; // IDRETRY: same problem again; repeat the message
        TRACE_C("CCopy_Context::HandleReadingErr(): unexpected result of WM_USER_DIALOG(0).");
        return TRUE;
    }
}

BOOL CCopy_Context::RetryCopyWriteErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain,
                                      const CQuadWord& allocFileSize, const CQuadWord& maxWriteOffset)
{
    if (*Out != NULL)
    {
        if (WholeFileAllocated)
            (void)GetWorkerFileSystem()->SetHandleEnd(*Out);     // otherwise on a floppy the remaining bytes would be written
        (void)CloseWorkerTrackedFile(*Out); // close the invalid handle
    }
    *Out = Op->OpenTargetFile(GENERIC_WRITE | GENERIC_READ, 0, OPEN_ALWAYS,
                              AsyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN);
    if (*Out != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
    {
        BOOL ok = TRUE;
        CQuadWord size;
        const FileResult sizeResult = GetWorkerHandleSize(*Out, size);
        if (!sizeResult.success ||                                                // cannot obtain the size
            size < WriteOffset ||                                                // file is too small
            WholeFileAllocated && size > allocFileSize && size > maxWriteOffset) // pre-allocated file is too large (greater than the pre-allocated size and the written portion including the current block) = extra bytes appended (allocWholeFileOnStart should be 0 /* need-test */)
        {                                                                        // restart the entire thing
            ok = FALSE;
        }
        // success (file size matches what we need)
        // if the target is on a network: disable local client-side in-memory caching
        // http://msdn.microsoft.com/en-us/library/ee210753%28v=vs.85%29.aspx
        //
        // using Overlapped[0].hEvent from AsyncPar is OK; nothing is "in-progress" now, the event is unused
        // (but WARNING: for example Buffers[0] from AsyncPar may still be in use)
        if (ok && DisableNetworkLocalBuffering && (Op->OpFlags & OPFL_TGTPATH_IS_NET) && !DisableLocalBuffering(AsyncPar, *Out, err))
            TRACE_EW(L"CCopy_Context::RetryCopyWriteErr(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network target file: " << Op->TargetNameW.c_str() << L", error: " << GetErrorTextOwned(*err).c_str());
        // using Overlapped[0 and 1].hEvent and Overlapped[0 and 1] from AsyncPar is OK; nothing is
        // "in-progress", the event nor the overlapped structures are used (but WARNING: for example Buffers[0]
        // from AsyncPar may still be in use)
        if (!ok || !CheckTailOfOutFile(AsyncPar, *In, *Out, WriteOffset, WriteOffset, FALSE))
        {
            (void)CloseWorkerTrackedFile(*In);
            (void)CloseWorkerTrackedFile(*Out);
            Op->DeleteTargetFile();
            *copyAgain = TRUE; // goto COPY_AGAIN;
            return FALSE;
        }
        ForceOp = ReadOffset > WriteOffset ? fopWriting : fopNotUsed; // if the read side is ahead, resume with writing
        *OperationDone = WriteOffset;
        Script->SetTFSandProgressSize(*LastTransferredFileSize + *OperationDone, *TotalDone + *OperationDone);
        Observer->SetProgressWithoutSuspend(CaclProg(*OperationDone, Op->Size),
                                                 CaclProg(*TotalDone + *OperationDone, Script->TotalSize));
        return TRUE; // success: proceed with retry
    }
    else // still cannot open; problem persists
    {
        *err = GetLastError();
        *Out = NULL;
        *errAgain = TRUE; // goto WRITE_ERROR;
        return FALSE;
    }
}

BOOL CCopy_Context::HandleWritingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain,
                                     const CQuadWord& allocFileSize, const CQuadWord& maxWriteOffset)
{
    // NOTE: blkIndex == -1 for an error while truncating the file after the main copy loop finishes (it has no assigned block)

    CancelOpPhase1();

    while (1)
    {
        Observer->WaitIfSuspended(); // if we are supposed to be in suspend mode, wait ...
        if (Observer->IsCancelled())
        {
            CancelOpPhase2(blkIndex);
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }

        if (DlgData->SkipAllFileWrite)
        {
            CancelOpPhase2(blkIndex);
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        int ret = IDCANCEL;
        ret = Observer->AskFileErrorById(IDS_ERRORWRITINGFILE, Op->TargetNameW.c_str(), err);
        CancelOpPhase2(blkIndex);
        BOOL errAgain = FALSE;
        switch (ret)
        {
        case IDRETRY:
        {
            if (RetryCopyWriteErr(&err, copyAgain, &errAgain, allocFileSize, maxWriteOffset))
                return TRUE; // retry
            else
            {
                if (errAgain)
                    break;    // same problem again, repeat the message
                return FALSE; // copyAgain==TRUE, goto COPY_AGAIN;
            }
        }

        case IDB_SKIPALL:
            DlgData->SkipAllFileWrite = TRUE;
        case IDB_SKIP:
        {
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        case IDCANCEL:
        {
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }
        }
        if (errAgain)
            continue; // IDRETRY: same problem again, repeat the message
        TRACE_C("CCopy_Context::HandleWritingErr(): unexpected result of WM_USER_DIALOG(0).");
        return TRUE;
    }
}

BOOL CCopy_Context::HandleSuspModeAndCancel(BOOL* copyError)
{
    if (!Script->ChangeSpeedLimit)                                  // if the speed limit cannot change (otherwise this is not a "suitable" place to wait)
        Observer->WaitIfSuspended(); // if we are supposed to be in suspend mode, wait ...
    if (Observer->IsCancelled())
    {
        CancelOpPhase1();
        CancelOpPhase2(-1);
        *copyError = TRUE; // goto COPY_ERROR
        return TRUE;
    }
    return FALSE;
}

// Asynchronous copy loop. All Win32 calls (ReadFile, WriteFile, GetOverlappedResult, GetFileSize,
// SetFilePointer, SetEndOfFile, CloseHandle) operate on open HANDLEs — handle-based, no path wrapping needed.
void DoCopyFileLoopAsync(CAsyncCopyParams* asyncPar, HANDLE& in, HANDLE& out, void* buffer, int& limitBufferSize,
                         COperations* script, CWorkerState& workerState, BOOL wholeFileAllocated, COperation* op,
                         const CQuadWord& totalDone, BOOL& copyError, BOOL& skipCopy, IWorkerObserver& observer,
                         CQuadWord& operationDone, CQuadWord& fileSize, int bufferSize,
                         int& allocWholeFileOnStart, BOOL& copyAgain, const CQuadWord& lastTransferredFileSize,
                         BOOL disableNetworkLocalBuffering)
{
    CQuadWord allocFileSize = fileSize;
    DWORD err = NO_ERROR;
    DWORD bytes = 0; // helper DWORD - how many bytes were read/written in the block

    // if the source/target is on the network: disable local client-side in-memory caching
    // http://msdn.microsoft.com/en-us/library/ee210753%28v=vs.85%29.aspx
    if (disableNetworkLocalBuffering && (op->OpFlags & OPFL_SRCPATH_IS_NET) && !DisableLocalBuffering(asyncPar, in, &err))
        TRACE_EW(L"DoCopyFileLoopAsync(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network source file: " << op->SourceNameW.c_str() << L", error: " << GetErrorTextOwned(err).c_str());
    if (disableNetworkLocalBuffering && (op->OpFlags & OPFL_TGTPATH_IS_NET) && !DisableLocalBuffering(asyncPar, out, &err))
        TRACE_EW(L"DoCopyFileLoopAsync(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network target file: " << op->TargetNameW.c_str() << L", error: " << GetErrorTextOwned(err).c_str());

    // copy loop parameters
    int numOfBlocks = 8;

    // Copy operation context (prevents passing heaps of parameters to helper functions, now context methods)
    CCopy_Context ctx(asyncPar, numOfBlocks, &workerState, op, observer, &in, &out, wholeFileAllocated, script,
                      &operationDone, &totalDone, &lastTransferredFileSize, disableNetworkLocalBuffering);
    BOOL doCopy = TRUE;
    while (doCopy)
    {
        if (ctx.ForceOp != fopWriting && ctx.FreeBlocks > 0 && !ctx.ReadingDone && ctx.ReadingBlocks < (numOfBlocks + 1) / 2) // read in parallel at most up to half of the blocks
        {
            DWORD toRead = ctx.ReadOffset + CQuadWord(limitBufferSize, 0) <= fileSize ? limitBufferSize : (fileSize - ctx.ReadOffset).LoDWord;
            BOOL testEOF = toRead == 0;
            if (!testEOF || ctx.ReadingBlocks == 0) // data read or EOF test (the EOF test runs only when all reads are finished)
            {
                if (ctx.BlockState[ctx.FreeBlockIndex] != cbsFree)
                    ctx.FreeBlockIndex = ctx.FindBlock(cbsFree);
                // EOF test = read the entire block, otherwise read the usual 'toRead'
                if (ctx.StartReading(ctx.FreeBlockIndex, testEOF ? limitBufferSize : toRead, &err, testEOF))
                    continue; // success (asynchronous read started), try to start another read
                else
                { // error (starting asynchronous read)
                    if (!ctx.HandleReadingErr(-1, err, &copyError, &skipCopy, &copyAgain))
                        return; // cancel/skip(skip-all)/retry-complete
                    continue;   // retry-resume
                }
            }
        }
        // reading has already been issued or is unnecessary, check whether something is completed
        BOOL shouldWait = TRUE; // TRUE = nothing else can be queued asynchronously, we must wait for some pending operation to finish
        BOOL retryCopy = FALSE; // TRUE = after an error we should run Retry = start over from the beginning of the "doCopy" loop
        // two passes are needed only for synchronous writes (we want to mark it
        // completed immediately and not after another read, mainly for progress reporting)
        for (int afterWriting = 0; afterWriting < 2; afterWriting++)
        {
            for (int i = 0; i < _countof(ctx.BlockState); i++)
            {
                if (ctx.BlockState[i] > cbsInProgress && HasOverlappedIoCompleted(asyncPar->GetOverlapped(i)))
                {
                    shouldWait = FALSE; // in the spirit of "keep it simple" (there are situations where it could remain TRUE, but we ignore them)
                    switch (ctx.BlockState[i])
                    {
                    case cbsReading:    // reading the source file into a block - requested (in progress)
                    case cbsTestingEOF: // testing for the end of the source file
                    {
                        BOOL testingEOF = ctx.BlockState[i] == cbsTestingEOF;

#ifdef ASYNC_COPY_DEBUG_MSG
                        TRACE_I("READ done: " << i);
#endif // ASYNC_COPY_DEBUG_MSG

                        FileResult completionResult = GetWorkerFileSystem()->CompleteHandleIo(
                            in, asyncPar->GetOverlapped(i), &bytes, true);
                        BOOL res = completionResult.success;
                        DWORD completionError = completionResult.errorCode;
                        if (testingEOF && res && bytes == 0)
                        {
                            res = FALSE; // MSDN says it should return FALSE and ERROR_HANDLE_EOF at EOF, so enforce that (Novell Netware 6.5 disk returns TRUE)
                            completionError = ERROR_HANDLE_EOF;
                        }
                        if (res || completionError == ERROR_HANDLE_EOF)
                        {
                            ctx.AutoRetryAttemptsSNAP = 0;
                            if (!res) // EOF at the beginning of the block (for cbsReading only: EOF can also be before this block and will be handled later in a block with a lower offset)
                            {
                                // when GetOverlappedResult() returns FALSE it does not have to return bytes==0
                                // (TRACE_C existed for that and crashes happened), so zero the bytes explicitly
                                bytes = 0;
                                if (testingEOF)
                                    ctx.ReadingDone = TRUE; // confirmed end of the source file, stop reading further
                                // we must not force fopWriting (we have not read anything, there is nothing to write), unless this is an EOF test,
                                // let the other asynchronous reads finish, then perform the EOF test, and only then continue with writing
                                ctx.ForceOp = fopNotUsed;
                            }
                            if (bytes < ctx.BlockDataLen[i]) // the file is shorter than expected -> set the new file size
                            {
                                if (!testingEOF || bytes != 0)
                                    ctx.ReadOffset = fileSize = ctx.BlockOffset[i] + CQuadWord(bytes, 0);
                                if (!testingEOF)
                                    ctx.DiscardBlocksBehindEOF(fileSize, i);
                                if (bytes == 0) // EOF = no data, free the block
                                {
                                    ctx.FreeBlock(i);
                                    if (testingEOF)
                                        doCopy = !ctx.IsOperationDone(numOfBlocks); // verify whether this finished the copy
                                }
                                else
                                    ctx.BlockDataLen[i] = bytes; // pretend we intended to read exactly this much
                            }
                            else
                            {
                                if (testingEOF) // we were looking for EOF and read a full block; the file probably grew significantly, determine the new size
                                {
                                    ctx.ReadOffset = ctx.BlockOffset[i] + CQuadWord(bytes, 0);
                                    ctx.GetNewFileSize(op->SourceNameW.c_str(), in, &fileSize, ctx.ReadOffset);
                                }
                            }
                            if (ctx.BlockState[i] == cbsReading || ctx.BlockState[i] == cbsTestingEOF)
                            {
                                ctx.ReadingBlocks--;
                                ctx.BlockState[i] = cbsRead;
                            }
                        }
                        else // error
                        {
                            if (!ctx.HandleReadingErr(i, completionError, &copyError, &skipCopy, &copyAgain))
                                return;       // cancel/skip(skip-all)/retry-complete
                            retryCopy = TRUE; // retry-resume
                        }
                        break;
                    }

                    case cbsWriting: // writing a block to the target file
                    {
#ifdef ASYNC_COPY_DEBUG_MSG
                        TRACE_I("WRITE done: " << i);
#endif // ASYNC_COPY_DEBUG_MSG

                        const FileResult completionResult = GetWorkerFileSystem()->CompleteHandleIo(
                            out, asyncPar->GetOverlapped(i), &bytes, true);
                        BOOL res = completionResult.success;
                        if (!res || bytes != ctx.BlockDataLen[i]) // error
                        {
                            err = completionResult.errorCode;
                            if (err == NO_ERROR && bytes != ctx.BlockDataLen[i])
                                err = ERROR_DISK_FULL;
                            CQuadWord maxWriteOffset = ctx.WriteOffset;
                            if (!ctx.HandleWritingErr(i, err, &copyError, &skipCopy, &copyAgain, allocFileSize, maxWriteOffset))
                                return;       // cancel/skip(skip-all)/retry-complete
                            retryCopy = TRUE; // retry-resume
                            break;
                        }

                        if (ctx.HandleSuspModeAndCancel(&copyError))
                            return; // cancel

                        script->AddBytesToSpeedMetersAndTFSandPS(bytes, FALSE, bufferSize, &limitBufferSize);

                        if (!script->ChangeSpeedLimit)                                 // if the speed limit can change, this is not a "suitable" place to wait
                            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                        operationDone += CQuadWord(bytes, 0);
                        observer.SetProgressWithoutSuspend(CaclProg(operationDone, op->Size),
                                                 CaclProg(totalDone + operationDone, script->TotalSize));

                        if (script->ChangeSpeedLimit)                                  // the speed limit is likely to change, this is a "suitable" place to wait until the
                        {                                                              // worker resumes so we can get the buffer size for copying again
                            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                            script->GetNewBufSize(&limitBufferSize, bufferSize);
                        }

                        // break; // the break is intentionally missing here...
                    }
                    case cbsDiscarded: // reading the source file beyond its end (should only return the EOF error)
                    {
                        ctx.FreeBlock(i);
                        doCopy = !ctx.IsOperationDone(numOfBlocks);
                        break;
                    }
                    }
                }
                if (!doCopy || retryCopy)
                    break;
            }
            if (!doCopy || retryCopy)
                break;

            // we have read data into blocks, check whether they can be written to the target file;
            // written/discarded blocks were freed (we will read into them again at the top of the loop)
            CQuadWord nextReadBlkOffset; // lowest offset of a skipped cbsRead block
            do
            {
                nextReadBlkOffset.SetUI64(0);
                // write in parallel at most up to half of the blocks
                for (int i = 0; ctx.ForceOp != fopReading && i < _countof(ctx.BlockState) && ctx.WritingBlocks < (numOfBlocks + 1) / 2; i++)
                {
                    if (ctx.BlockState[i] == cbsRead)
                    {
                        if (ctx.WriteOffset == ctx.BlockOffset[i])
                        {
                            if (!ctx.StartWriting(i, &err))
                            { // error (asynchronous write)
                                CQuadWord maxWriteOffset = ctx.WriteOffset + CQuadWord(ctx.BlockDataLen[i], 0);
                                if (!ctx.HandleWritingErr(i, err, &copyError, &skipCopy, &copyAgain, allocFileSize, maxWriteOffset))
                                    return;       // cancel/skip(skip-all)/retry-complete
                                retryCopy = TRUE; // retry-resume
                                break;
                            }
                        }
                        else
                        {
                            if (nextReadBlkOffset.Value == 0 || ctx.BlockOffset[i] < nextReadBlkOffset)
                                nextReadBlkOffset = ctx.BlockOffset[i];
                        }
                    }
                } // we have another cbsRead block adjoining the written portion of the target file -> keep writing
            } while (!retryCopy && ctx.ForceOp != fopReading && nextReadBlkOffset.Value != 0 && nextReadBlkOffset == ctx.WriteOffset &&
                     ctx.WritingBlocks < (numOfBlocks + 1) / 2); // write in parallel at most up to half of the blocks
            if (retryCopy || ctx.ForceOp != fopReading)
                break; // we are going to Retry or the write was not synchronous (finished in about 0 ms) or we only write now, so two passes are pointless
        }
        if (!doCopy || retryCopy)
            continue;

        if (shouldWait) // another pass through the loop is pointless, no chance to start a new read or write, wait
        {               // for the oldest asynchronous operation to finish
            DWORD oldestBlockTime = 0;
            int oldestBlockIndex = -1;
            for (int i = 0; i < _countof(ctx.BlockState); i++)
            {
                if (ctx.BlockState[i] > cbsInProgress)
                {
                    DWORD ti = ctx.CurTime - ctx.BlockTime[i];
                    if (oldestBlockTime < ti)
                    {
                        oldestBlockTime = ti;
                        oldestBlockIndex = i;
                    }
                }
            }
            if (oldestBlockIndex == -1)
                TRACE_C("DoCopyFileLoopAsync(): unexpected situation: unable to find any block with operation in progress!");

#ifdef ASYNC_COPY_DEBUG_MSG
            TRACE_I("wait: GetOverlappedResult: " << oldestBlockIndex << (ctx.BlockState[oldestBlockIndex] == cbsWriting ? " WRITE" : " READ"));
#endif // ASYNC_COPY_DEBUG_MSG

            // wait for the oldest pending asynchronous operation to complete here
            // for the source file ('in') this covers: cbsReading, cbsTestingEOF, and cbsDiscarded
            // for the target file ('out') this covers only cbsWriting
            (void)GetWorkerFileSystem()->CompleteHandleIo(
                ctx.BlockState[oldestBlockIndex] == cbsWriting ? out : in,
                asyncPar->GetOverlapped(oldestBlockIndex), &bytes, true);

#ifdef ASYNC_COPY_DEBUG_MSG
            char sss[1000];
            sprintf(sss, "wait done: 0x%08X 0x%08X", ctx.BlockOffset[oldestBlockIndex].LoDWord, bytes);
            TRACE_I(sss);
#endif // ASYNC_COPY_DEBUG_MSG

            if (ctx.HandleSuspModeAndCancel(&copyError))
                return; // cancel
        }
    }
    if (ctx.ReadOffset != ctx.WriteOffset || operationDone != ctx.WriteOffset)
        TRACE_C("DoCopyFileLoopAsync(): unexpected situation after copy: ReadOffset != WriteOffset || operationDone != ctx.WriteOffset");

    if (wholeFileAllocated) // we allocated the full size of the file (meaning the allocation made sense, e.g. the file cannot be empty)
    {
        if (operationDone < allocFileSize) // and the source file shrank, trim it here
        {
            while (1)
            {
                uint64_t position = 0;
                const FileResult seekResult = GetWorkerFileSystem()->SeekHandle(
                    out, static_cast<int64_t>(ctx.WriteOffset.Value), FILE_BEGIN,
                    &position);
                DWORD err2 = ERROR_SUCCESS;
                bool failed = false;
                if (!seekResult.success)
                {
                    failed = true;
                    err2 = seekResult.errorCode;
                }
                else if (position != ctx.WriteOffset.Value)
                {
                    failed = true;
                    err2 = ERROR_INVALID_FUNCTION;
                }
                else
                {
                    const FileResult truncateResult =
                        GetWorkerFileSystem()->SetHandleEnd(out);
                    if (!truncateResult.success)
                    {
                        failed = true;
                        err2 = truncateResult.errorCode;
                    }
                }
                if (failed)
                {
                    if (!ctx.HandleWritingErr(-1, err2, &copyError, &skipCopy, &copyAgain, allocFileSize, CQuadWord(0, 0)))
                        return; // cancel/skip(skip-all)/retry-complete
                                // retry-resume
                }
                else
                    break; // success
            }
        }

        if (allocWholeFileOnStart == 0 /* need-test */)
        {
            CQuadWord curFileSize;
            const FileResult sizeResult = GetWorkerHandleSize(out, curFileSize);
            BOOL getFileSizeSuccess = sizeResult.success;
            if (getFileSizeSuccess && curFileSize == operationDone)
            { // verify that no extra bytes were appended to the end of the file + that we can truncate the file
                allocWholeFileOnStart = 1 /* yes */;
            }
            else
            {
#ifdef _DEBUG
                if (getFileSizeSuccess)
                {
                    TRACE_EW(L"DoCopyFileLoopAsync(): unable to allocate whole file size before copy operation, please report "
                             L"under what conditions this occurs! Error: different file sizes: target="
                             << NumberToStr(curFileSize) << L" bytes, source=" << NumberToStr(operationDone) << L" bytes");
                }
                else
                {
                    DWORD err2 = sizeResult.errorCode;
                    TRACE_EW(L"DoCopyFileLoopAsync(): unable to test result of allocation of whole file size before copy operation, please report "
                            L"under what conditions this occurs! GetFileSize("
                            << op->TargetNameW.c_str() << L") error: " << GetErrorTextOwned(err2).c_str());
                }
#endif
                allocWholeFileOnStart = 2 /* no */; // skip further attempts on this target disk

                while (1)
                {
                    (void)CloseWorkerTrackedFile(out);
                    out = NULL;
                    op->ClearTargetReadOnly(); // in case it was created as read-only (should never happen) so we can handle it
                    if (op->DeleteTargetFile())
                    {
                        (void)CloseWorkerTrackedFile(in);
                        copyAgain = TRUE; // goto COPY_AGAIN;
                        return;
                    }
                    else
                    {
                        if (!ctx.HandleWritingErr(-1, GetLastError(), &copyError, &skipCopy, &copyAgain, allocFileSize, CQuadWord(0, 0)))
                            return; // cancel/skip(skip-all)/retry-complete
                                    // retry-resume
                    }
                }
            }
        }
    }
}

// Copy I/O wrapping assessment:
// - Path-based calls (open, delete, set attrs): wrapped via COperation methods (OpenSourceFile,
//   OpenTargetFile, CreateTargetFileEx, DeleteTargetFile, SetTargetAttributes, GetTargetAttributes)
// - Handle-based calls (GetFileSize, SetFilePointer, SetEndOfFile, GetFileTime, SetFileTime):
//   operate on open HANDLEs, no path involved — wrapping provides no Unicode/long-path benefit
// - Inner loop I/O (ReadFile, WriteFile, GetOverlappedResult): performance-critical hot path,
//   must stay as direct Win32 calls to avoid virtual dispatch overhead per block
// - ADS stream I/O (DoCopyADS): uses wide paths with ":streamname" suffix, not main-file paths —
//   COperation wrapping not applicable
// - DeviceIoControl (compression, network buffering): handle-based, wrapping not needed
BOOL DoCopyFile(COperation* op, IWorkerObserver& observer, void* buffer,
                COperations* script, CQuadWord& totalDone,
                DWORD clearReadonlyMask, BOOL* skip, BOOL lantasticCheck,
                int& mustDeleteFileBeforeOverwrite, int& allocWholeFileOnStart,
                CWorkerState& workerState, BOOL copyADS, BOOL copyAsEncrypted,
                BOOL isMove, CAsyncCopyParams*& asyncPar)
{
    if (script->CopyAttrs && copyAsEncrypted)
        TRACE_E("DoCopyFile(): unexpected parameter value: copyAsEncrypted is TRUE when script->CopyAttrs is TRUE!");

    // if the path ends with a space/dot, it is invalid and we must not copy it,
    // CreateFile would trim the spaces/dots and copy a different file or under a different name
    BOOL invalidSrcName = op->IsSourceNameInvalid();
    BOOL invalidTgtName = op->IsTargetNameInvalid();

    // optimization: skipping all "older and identical" files is about 4x faster,
    // slowing down when the file is newer is 5%, so it should be well worth it
    // (it is safe to assume the user enables "Overwrite Older" when the skips occur)
    BOOL tgtNameCaseCorrected = FALSE; // TRUE = the letter case in the target name was already adjusted to match the existing target file (so overwriting does not change it)
    WIN32_FIND_DATAW dataIn, dataOut;
    if ((op->OpFlags & OPFL_OVERWROLDERALRTESTED) == 0 &&
        !invalidSrcName && !invalidTgtName && script->OverwriteOlder)
    {
        HANDLE find;
        find = op->FindFirstTarget(&dataOut);
        if (find != INVALID_HANDLE_VALUE)
        {
            SalLPFindClose(find);

            CorrectCaseOfTgtNameW(op->TargetNameW, TRUE, &dataOut);
            tgtNameCaseCorrected = TRUE;

            const wchar_t* tgtLeaf = wcsrchr(op->TargetNameW.c_str(), L'\\');
            tgtLeaf = tgtLeaf != NULL ? tgtLeaf + 1 : op->TargetNameW.c_str();
            if (_wcsicmp(tgtLeaf, dataOut.cFileName) == 0 &&                // ensure it is not just a DOS-name match (that would change the DOS-name instead of overwriting)
                (dataOut.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) // ensure it is not a directory (overwrite-older cannot help there)
            {
                find = op->FindFirstSource(&dataIn);
                if (find != INVALID_HANDLE_VALUE)
                {
                    SalLPFindClose(find);

                    // truncate times to seconds (different file systems store timestamps with different precision, leading to "differences" even between "identical" times)
                    *(unsigned __int64*)&dataIn.ftLastWriteTime = *(unsigned __int64*)&dataIn.ftLastWriteTime - (*(unsigned __int64*)&dataIn.ftLastWriteTime % 10000000);
                    *(unsigned __int64*)&dataOut.ftLastWriteTime = *(unsigned __int64*)&dataOut.ftLastWriteTime - (*(unsigned __int64*)&dataOut.ftLastWriteTime % 10000000);

                    if ((dataIn.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&             // verify the source is still a file
                        CompareFileTime(&dataIn.ftLastWriteTime, &dataOut.ftLastWriteTime) <= 0) // source file is not newer than the target file - skip the copy operation
                    {
                        CQuadWord fileSize(op->FileSize);
                        if (fileSize < COPY_MIN_FILE_SIZE)
                        {
                            if (op->Size > COPY_MIN_FILE_SIZE)             // should always be at least COPY_MIN_FILE_SIZE, but play it safe...
                                fileSize += op->Size - COPY_MIN_FILE_SIZE; // add the size of ADS streams
                        }
                        else
                            fileSize = op->Size;
                        totalDone += op->Size;
                        script->AddBytesToTFSandSetProgressSize(fileSize, totalDone);

                        observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                        if (skip != NULL)
                            *skip = TRUE;
                        return TRUE;
                    }
                }
            }
        }
    }

    // decide which algorithm to use for copying: the ancient synchronous one or
    // the asynchronous one inspired by the Windows 7 CopyFileEx version:
    // - under Vista it misbehaved badly; forget Vista, it is almost dead anyway, and
    //   when using the old algorithm against Win7 over the network I saw no speed difference
    //   for uploads, and downloads were only 15% slower (acceptable)
    // - historically the asynchronous algorithm was used for network copies only; issue #52 also
    //   allows large plain copies between fast local volumes to use the same loop for measurement
    // - with the old algorithm, copying on Win7 over the network is easily 2x-3x slower for downloads,
    //   almost 2x slower for uploads, and about 30% slower for network-to-network copies
    sally::copy::StrategyInput copyStrategyInput;
    copyStrategyInput.FileSize = op->FileSize.Value;
    copyStrategyInput.SourceIsNetwork = (op->OpFlags & OPFL_SRCPATH_IS_NET) != 0;
    copyStrategyInput.SourceIsFast = (op->OpFlags & OPFL_SRCPATH_IS_FAST) != 0;
    copyStrategyInput.SourceIsRemovable = script->RemovableSrcDisk != FALSE;
    copyStrategyInput.TargetIsNetwork = (op->OpFlags & OPFL_TGTPATH_IS_NET) != 0;
    copyStrategyInput.TargetIsFast = (op->OpFlags & OPFL_TGTPATH_IS_FAST) != 0;
    copyStrategyInput.TargetIsRemovable = script->RemovableTgtDisk != FALSE;
    copyStrategyInput.AsyncEnabled = workerState.UseAsyncCopyAlg != FALSE;
    copyStrategyInput.SimpleCopyEligible = !copyADS && !copyAsEncrypted && !script->CopyAttrs &&
                                           !script->CopySecurity && !lantasticCheck;
    // Keep production behavior unchanged until large local async has merge-quality measurements.
    copyStrategyInput.FastLocalAsyncEnabled = false;
    // Keep network async behavior unchanged until a measured production rule replaces it.
    copyStrategyInput.DisableNetworkLocalBuffering = true;
    copyStrategyInput.CopyFileExCandidateEnabled = false;

    sally::copy::Strategy copyStrategy = sally::copy::SelectStrategy(copyStrategyInput);
    BOOL useAsyncAlg = sally::copy::UsesAsyncLoop(copyStrategy);
    BOOL disableNetworkLocalBuffering = sally::copy::DisablesNetworkLocalBuffering(copyStrategy);

    if (asyncPar == NULL)
        asyncPar = new CAsyncCopyParams;

    asyncPar->Init(useAsyncAlg);
    script->EnableProgressBufferLimit(useAsyncAlg);
    struct CDisableProgressBufferLimit // ensure Script->EnableProgressBufferLimit(FALSE) is called on every exit from this function
    {
        COperations* Script;
        CDisableProgressBufferLimit(COperations* script) { Script = script; }
        ~CDisableProgressBufferLimit() { Script->EnableProgressBufferLimit(FALSE); }
    } DisableProgressBufferLimit(script);

    CQuadWord operationDone;
    CQuadWord lastTransferredFileSize;
    script->GetTFSandResetTrSpeedIfNeeded(&lastTransferredFileSize);

COPY_AGAIN:

    operationDone = CQuadWord(0, 0);
    HANDLE in;

    if (skip != NULL)
        *skip = FALSE;

    int bufferSize;
    if (useAsyncAlg)
    {
        if (op->FileSize.Value <= 512 * 1024)
            bufferSize = ASYNC_COPY_BUF_SIZE_512KB;
        else if (op->FileSize.Value <= 2 * 1024 * 1024)
            bufferSize = ASYNC_COPY_BUF_SIZE_2MB;
        else if (op->FileSize.Value <= 8 * 1024 * 1024)
            bufferSize = ASYNC_COPY_BUF_SIZE_8MB;
        else
            bufferSize = ASYNC_COPY_BUF_SIZE;
    }
    else
        bufferSize = script->RemovableSrcDisk || script->RemovableTgtDisk ? REMOVABLE_DISK_COPY_BUFFER : OPERATION_BUFFER;

    int limitBufferSize = bufferSize;
    script->SetTFSandProgressSize(lastTransferredFileSize, totalDone, &limitBufferSize, bufferSize);

    while (1)
    {
        if (!invalidSrcName && !asyncPar->Failed())
        {
            in = op->OpenSourceFile(asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN);
        }
        else
        {
            in = INVALID_HANDLE_VALUE;
        }
        if (in != INVALID_HANDLE_VALUE)
        {
            CQuadWord fileSize = op->FileSize;

            HANDLE out;
            BOOL lossEncryptionAttr = FALSE;
            BOOL skipAllocWholeFileOnStart = FALSE;
            while (1)
            {
            OPEN_TGT_FILE:

                BOOL encryptionNotSupported = FALSE;
                DWORD fileAttrs = asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN |
                                  (!lossEncryptionAttr && copyAsEncrypted ? FILE_ATTRIBUTE_ENCRYPTED : 0) |
                                  (script->CopyAttrs ? (op->Attr & (FILE_ATTRIBUTE_COMPRESSED | (lossEncryptionAttr ? 0 : FILE_ATTRIBUTE_ENCRYPTED))) : 0);
                if (!invalidTgtName)
                {
                    // GENERIC_READ for 'out' slows asynchronous copying from disk to network (measured 95 MB/s instead of 111 MB/s on Win7 x64 GLAN)
                    // Use CreateTargetFileEx for Unicode filename support (uses wide path when available)
                    out = op->CreateTargetFileEx(GENERIC_WRITE | (script->CopyAttrs ? GENERIC_READ : 0), 0, fileAttrs, &encryptionNotSupported);
                    if (!encryptionNotSupported && script->CopyAttrs && out == INVALID_HANDLE_VALUE) // in case read access to the directory is not allowed (we added it only for setting the Compressed attribute), try creating a write-only file
                        out = op->CreateTargetFileEx(GENERIC_WRITE, 0, fileAttrs, &encryptionNotSupported);

                    if (out == INVALID_HANDLE_VALUE && encryptionNotSupported && workerState.FileOutLossEncrAll && !lossEncryptionAttr)
                    { // the user agreed to lose the Encrypted attribute for all problematic files, so make that happen here
                        lossEncryptionAttr = TRUE;
                        continue;
                    }
                    HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                   __hoCreateFile, out, GetLastError(), TRUE);
                    if (script->CopyAttrs)
                    {
                        fileAttrs = lossEncryptionAttr ? (op->Attr & ~FILE_ATTRIBUTE_ENCRYPTED) : op->Attr;
                        SetCompressAndEncryptedAttrs(fileAttrs, &out, TRUE, NULL, asyncPar, op->TargetNameW);
                    }

                    if (out != INVALID_HANDLE_VALUE && (fileAttrs & FILE_ATTRIBUTE_ENCRYPTED))
                    { // verify that the Encrypted attribute is really set (on FAT it is simply ignored, the system does not return an error (for CreateFile specifically))
                        DWORD attrs;
                        attrs = op->GetTargetAttributes();
                        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0)
                        { // unable to apply the Encrypted attribute, ask the user what to do...
                            if (workerState.FileOutLossEncrAll)
                                lossEncryptionAttr = TRUE;
                            else
                            {
                                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                if (observer.IsCancelled())
                                    goto CANCEL_ENCNOTSUP;

                                if (workerState.SkipAllFileOutLossEncr)
                                    goto SKIP_ENCNOTSUP;

                                int ret;
                                ret = IDCANCEL;
                                ret = observer.AskEncryptionLoss(true, op->TargetNameW.c_str(), isMove != 0);
                                switch (ret)
                                {
                                case IDB_ALL:
                                    workerState.FileOutLossEncrAll = TRUE; // the break; is intentionally missing here
                                case IDYES:
                                    lossEncryptionAttr = TRUE;
                                    break;

                                case IDB_SKIPALL:
                                    workerState.SkipAllFileOutLossEncr = TRUE;
                                case IDB_SKIP:
                                {
                                SKIP_ENCNOTSUP:

                                    (void)CloseWorkerTrackedFile(out);
                                    op->DeleteTargetFile();
                                    goto SKIP_OPEN_OUT;
                                }

                                case IDCANCEL:
                                {
                                CANCEL_ENCNOTSUP:

                                    (void)CloseWorkerTrackedFile(out);
                                    op->DeleteTargetFile();
                                    goto CANCEL_OPEN2;
                                }
                                }
                            }
                        }
                    }
                }
                else
                {
                    out = INVALID_HANDLE_VALUE;
                }

                if (out != INVALID_HANDLE_VALUE)
                {

                COPY:

                    // if possible, allocate the required space for the file (prevents disk fragmentation + smoother writes to floppies)
                    BOOL wholeFileAllocated = FALSE;
                    if (!skipAllocWholeFileOnStart &&               // last time failed, so the same would probably happen now
                        allocWholeFileOnStart != 2 /* no */ &&      // allocating the whole file is not forbidden
                        fileSize > CQuadWord(limitBufferSize, 0) && // allocation is pointless below the copy buffer size
                        fileSize < CQuadWord(0, 0x80000000))        // file size is positive number (otherwise seeking is impossible - numbers above 8EB, so likely never happens)
                    {
                        BOOL fatal = TRUE;
                        BOOL ignoreErr = FALSE;
                        DWORD allocateError = ERROR_SUCCESS;
                        const FileResult allocateSeekResult =
                            SeekWorkerHandleExact(out, fileSize);
                        if (allocateSeekResult.success)
                        {
                            const FileResult allocateResult =
                                GetWorkerFileSystem()->SetHandleEnd(out);
                            if (allocateResult.success)
                            {
                                uint64_t rewindPosition = 0;
                                const FileResult rewindResult =
                                    GetWorkerFileSystem()->SeekHandle(
                                        out, 0, FILE_BEGIN, &rewindPosition);
                                if (rewindResult.success && rewindPosition == 0)
                                {
                                    fatal = FALSE;
                                    wholeFileAllocated = TRUE;
                                }
                            }
                            else
                            {
                                allocateError = allocateResult.errorCode;
                                if (allocateError == ERROR_DISK_FULL)
                                    ignoreErr = TRUE; // not enough space on the disk
                            }
                        }
                        else
                            allocateError = allocateSeekResult.errorCode;
                        if (fatal)
                        {
                            if (!ignoreErr)
                            {
                                DWORD err = allocateError != ERROR_SUCCESS ?
                                                allocateError : GetLastError();
                                TRACE_EW(L"DoCopyFile(): unable to allocate whole file size before copy operation, please report under what conditions this occurs! GetLastError(): " << GetErrorTextOwned(err).c_str());
                                allocWholeFileOnStart = 2 /* no */; // we will forego further attempts on this target disk
                            }

                            // try truncating the file to zero so closing it does not trigger any unnecessary writes
                            (void)GetWorkerFileSystem()->SeekHandle(
                                out, 0, FILE_BEGIN, NULL);
                            (void)GetWorkerFileSystem()->SetHandleEnd(out);

                            (void)CloseWorkerTrackedFile(out);
                            out = INVALID_HANDLE_VALUE;
                            op->ClearTargetReadOnly(); // in case it was created as read-only (should never happen) so we can handle it
                            if (op->DeleteTargetFile())
                            {
                                skipAllocWholeFileOnStart = TRUE;
                                goto OPEN_TGT_FILE;
                            }
                            else
                                goto CREATE_ERROR;
                        }
                    }

                    script->SetFileStartParams();

                    BOOL copyError = FALSE;
                    BOOL skipCopy = FALSE;
                    BOOL copyAgain = FALSE;
                    if (useAsyncAlg)
                    {
                        DoCopyFileLoopAsync(asyncPar, in, out, buffer, limitBufferSize, script, workerState, wholeFileAllocated, op,
                                            totalDone, copyError, skipCopy, observer, operationDone, fileSize,
                                            bufferSize, allocWholeFileOnStart, copyAgain, lastTransferredFileSize,
                                            disableNetworkLocalBuffering);
                        // NOTE: neither 'in' nor 'out' has the file pointer (SetFilePointer) positioned at the end of the file,
                        //       'out' has it set only when (copyError || skipCopy)
                    }
                    else
                    {
                        DoCopyFileLoopOrig(in, out, buffer, limitBufferSize, script, workerState, wholeFileAllocated, op,
                                           totalDone, copyError, skipCopy, observer, operationDone, fileSize,
                                           bufferSize, allocWholeFileOnStart, copyAgain);
                    }

                    if (copyError)
                    {
                    COPY_ERROR:

                        if (in != NULL)
                            (void)CloseWorkerTrackedFile(in);
                        if (out != NULL)
                        {
                            if (wholeFileAllocated)
                                (void)GetWorkerFileSystem()->SetHandleEnd(out); // otherwise on a floppy the remaining part of the file would be written
                            (void)CloseWorkerTrackedFile(out);
                        }
                        op->DeleteTargetFile();
                        return FALSE;
                    }
                    if (skipCopy)
                    {
                    SKIP_COPY:

                        totalDone += op->Size;
                        SetTFSandPSforSkippedFile(op, lastTransferredFileSize, script, totalDone);

                        if (in != NULL)
                            (void)CloseWorkerTrackedFile(in);
                        if (out != NULL)
                        {
                            if (wholeFileAllocated)
                                (void)GetWorkerFileSystem()->SetHandleEnd(out); // otherwise on a floppy the remaining part of the file would be written
                            (void)CloseWorkerTrackedFile(out);
                        }
                        op->DeleteTargetFile();
                        observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                        if (skip != NULL)
                            *skip = TRUE;
                        return TRUE;
                    }
                    if (copyAgain)
                        goto COPY_AGAIN;

                    if (lantasticCheck)
                    {
                        CQuadWord inSize, outSize;
                        const FileResult inSizeResult = GetWorkerHandleSize(in, inSize);
                        const FileResult outSizeResult = GetWorkerHandleSize(out, outSize);
                        if (!inSizeResult.success || !outSizeResult.success ||
                            inSize != outSize)
                        {                                                              // Lantastic 7.0: everything seems fine, but the result is wrong
                            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                            if (observer.IsCancelled())
                                goto COPY_ERROR;

                            if (workerState.SkipAllFileWrite)
                                goto SKIP_COPY;

                            int ret = IDCANCEL;
                            ret = observer.AskFileErrorById(IDS_ERRORWRITINGFILE, op->TargetNameW.c_str(), ERROR_DISK_FULL);
                            switch (ret)
                            {
                            case IDRETRY:
                            {
                                operationDone = CQuadWord(0, 0);
                                script->SetTFSandProgressSize(lastTransferredFileSize, totalDone);
                                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                                (void)GetWorkerFileSystem()->SeekHandle(
                                    in, 0, FILE_BEGIN, NULL); // read again
                                (void)GetWorkerFileSystem()->SeekHandle(
                                    out, 0, FILE_BEGIN, NULL); // write again
                                (void)GetWorkerFileSystem()->SetHandleEnd(out);                        // truncate the output file
                                goto COPY;
                            }

                            case IDB_SKIPALL:
                                workerState.SkipAllFileWrite = TRUE;
                            case IDB_SKIP:
                                goto SKIP_COPY;

                            case IDCANCEL:
                                goto COPY_ERROR;
                            }
                        }
                    }

                    FILETIME /*creation, lastAccess,*/ lastWrite;
                    BOOL ignoreGetFileTimeErr = FALSE;
                    while (!ignoreGetFileTimeErr)
                    {
                        const FileResult timeResult = GetWorkerFileSystem()->GetHandleFileTime(
                            in, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite);
                        if (timeResult.success)
                            break;
                        DWORD err = timeResult.errorCode;

                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                        if (observer.IsCancelled())
                            goto COPY_ERROR;

                        if (workerState.SkipAllGetFileTime)
                            goto SKIP_COPY;

                        if (workerState.IgnoreAllGetFileTimeErr)
                            goto IGNORE_GETFILETIME;

                        int ret;
                        ret = IDCANCEL;
                        ret = observer.AskADSOpenErrorById(IDS_ERRORGETTINGFILETIME, op->SourceNameW.c_str(), err);
                        switch (ret)
                        {
                        case IDRETRY:
                            break;

                        case IDB_IGNOREALL:
                            workerState.IgnoreAllGetFileTimeErr = TRUE; // the break; is intentionally missing here
                        case IDB_IGNORE:
                        {
                        IGNORE_GETFILETIME:

                            ignoreGetFileTimeErr = TRUE;
                            break;
                        }

                        case IDB_SKIPALL:
                            workerState.SkipAllGetFileTime = TRUE;
                        case IDB_SKIP:
                            goto SKIP_COPY;

                        case IDCANCEL:
                            goto COPY_ERROR;
                        }
                    }

                    (void)CloseWorkerTrackedFile(in);
                    in = NULL;

                    if (operationDone < COPY_MIN_FILE_SIZE) // zero/small files take at least as long as files of size COPY_MIN_FILE_SIZE
                        script->AddBytesToSpeedMetersAndTFSandPS((DWORD)(COPY_MIN_FILE_SIZE - operationDone).Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

                    DWORD attr = op->Attr & clearReadonlyMask;
                    if (copyADS) // copy ADS streams if needed
                    {
                        op->SetTargetAttributes(FILE_ATTRIBUTE_ARCHIVE); // probably unnecessary, it hardly slows copying; reason: the file must not be read-only to work with it
                        CQuadWord operDone = operationDone;                        // the file is already copied
                        if (operDone < COPY_MIN_FILE_SIZE)
                            operDone = COPY_MIN_FILE_SIZE; // zero/small files take at least as long as files of size COPY_MIN_FILE_SIZE
                        BOOL adsSkip = FALSE;
                        if (!DoCopyADS(observer, FALSE, totalDone,
                                       operDone, op->Size, workerState, script, &adsSkip, buffer,
                                       op->SourceNameW, op->TargetNameW) ||
                            adsSkip) // user hit cancel or skipped at least one ADS
                        {
                            if (out != NULL)
                                (void)CloseWorkerTrackedFile(out);
                            out = NULL;
                            if (op->DeleteTargetFile() == 0)
                            {
                                DWORD err = GetLastError();
                                TRACE_EW(L"DoCopyFile(): Unable to remove newly created file: " << op->TargetNameW.c_str() << L", error: " << GetErrorTextOwned(err).c_str());
                            }
                            if (!adsSkip)
                                return FALSE; // cancel the entire operation
                            if (skip != NULL)
                                *skip = TRUE; // it is a Skip, must report higher up (Move must not delete the source file)
                        }
                    }

                    if (out != NULL)
                    {
                        if (!ignoreGetFileTimeErr) // only if we did not ignore the error while reading the file time (nothing to set otherwise)
                        {
                            BOOL ignoreSetFileTimeErr = FALSE;
                            while (!ignoreSetFileTimeErr)
                            {
                                const FileResult timeResult = GetWorkerFileSystem()->SetHandleFileTime(
                                    out, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite);
                                if (timeResult.success)
                                    break;
                                DWORD err = timeResult.errorCode;

                                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                if (observer.IsCancelled())
                                    goto COPY_ERROR;

                                if (workerState.SkipAllSetFileTime)
                                    goto SKIP_COPY;

                                if (workerState.IgnoreAllSetFileTimeErr)
                                    goto IGNORE_SETFILETIME;

                                int ret;
                                ret = IDCANCEL;
                                ret = observer.AskADSOpenErrorById(IDS_ERRORSETTINGFILETIME, op->TargetNameW.c_str(), err);
                                switch (ret)
                                {
                                case IDRETRY:
                                    break;

                                case IDB_IGNOREALL:
                                    workerState.IgnoreAllSetFileTimeErr = TRUE; // the break; is intentionally missing here
                                case IDB_IGNORE:
                                {
                                IGNORE_SETFILETIME:

                                    ignoreSetFileTimeErr = TRUE;
                                    break;
                                }

                                case IDB_SKIPALL:
                                    workerState.SkipAllSetFileTime = TRUE;
                                case IDB_SKIP:
                                    goto SKIP_COPY;

                                case IDCANCEL:
                                    goto COPY_ERROR;
                                }
                            }
                        }
                        const FileResult closeOutResult =
                            CloseWorkerTrackedFile(out);
                        if (!closeOutResult.success)
                        {
                            out = NULL;
                            DWORD err = closeOutResult.errorCode;
                            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                            if (observer.IsCancelled())
                                goto COPY_ERROR;

                            if (workerState.SkipAllFileWrite)
                                goto SKIP_COPY;

                            int ret = IDCANCEL;
                            ret = observer.AskFileErrorById(IDS_ERRORWRITINGFILE, op->TargetNameW.c_str(), err);
                            switch (ret)
                            {
                            case IDRETRY:
                            {
                                if (op->DeleteTargetFile() == 0)
                                {
                                    DWORD err2 = GetLastError();
                                    TRACE_EW(L"DoCopyFile(): Unable to remove newly created file: " << op->TargetNameW.c_str() << L", error: " << GetErrorTextOwned(err2).c_str());
                                }
                                goto COPY_AGAIN;
                            }

                            case IDB_SKIPALL:
                                workerState.SkipAllFileWrite = TRUE;
                            case IDB_SKIP:
                                goto SKIP_COPY;

                            case IDCANCEL:
                                goto COPY_ERROR;
                            }
                        }

                        op->SetTargetAttributes(script->CopyAttrs ? attr : (attr | FILE_ATTRIBUTE_ARCHIVE));
                    }

                    if (script->CopyAttrs) // verify whether the source file attributes were preserved
                    {
                        DWORD curAttrs;
                        curAttrs = op->GetTargetAttributes();
                        if (curAttrs == INVALID_FILE_ATTRIBUTES || (curAttrs & DISPLAYED_ATTRIBUTES) != (attr & DISPLAYED_ATTRIBUTES))
                        {                                                              // attributes probably were not preserved, warn the user
                            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                            if (observer.IsCancelled())
                                goto COPY_ERROR_2;

                            int ret;
                            ret = IDCANCEL;
                            if (workerState.IgnoreAllSetAttrsErr)
                                ret = IDB_IGNORE;
                            else
                            {
                                ret = observer.AskSetAttrsError(op->TargetNameW.c_str(), (attr & DISPLAYED_ATTRIBUTES), (curAttrs == INVALID_FILE_ATTRIBUTES ? 0 : (curAttrs & DISPLAYED_ATTRIBUTES)));
                            }
                            switch (ret)
                            {
                            case IDB_IGNOREALL:
                                workerState.IgnoreAllSetAttrsErr = TRUE; // break is intentional here; nothing is missing
                            case IDB_IGNORE:
                                break;

                            case IDCANCEL:
                            {
                            COPY_ERROR_2:

                                op->ClearTargetReadOnly(); // the file must not be read-only if it is to be deleted
                                op->DeleteTargetFile();
                                return FALSE;
                            }
                            }
                        }
                    }

                    if (script->CopySecurity) // should we copy NTFS security permissions?
                    {
                        DWORD err;
                        if (!DoCopySecurity(&err, NULL,
                                           op->SourceNameW, op->TargetNameW))
                        {
                            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                            if (observer.IsCancelled())
                                goto COPY_ERROR_2;

                            int ret;
                            ret = IDCANCEL;
                            if (workerState.IgnoreAllCopyPermErr)
                                ret = IDB_IGNORE;
                            else
                            {
                                ret = observer.AskCopyPermError(op->SourceNameW.c_str(), op->TargetNameW.c_str(), err);
                            }
                            switch (ret)
                            {
                            case IDB_IGNOREALL:
                                workerState.IgnoreAllCopyPermErr = TRUE; // the break; is intentionally missing here
                            case IDB_IGNORE:
                                break;

                            case IDCANCEL:
                                goto COPY_ERROR_2;
                            }
                        }
                    }

                    totalDone += op->Size;
                    script->SetProgressSize(totalDone);
                    return TRUE;
                }
                else
                {
                    if (!invalidTgtName && encryptionNotSupported)
                    {
                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                        if (observer.IsCancelled())
                            goto CANCEL_OPEN2;

                        if (workerState.SkipAllFileOutLossEncr)
                            goto SKIP_OPEN_OUT;

                        int ret;
                        ret = IDCANCEL;
                        ret = observer.AskEncryptionLoss(true, op->TargetNameW.c_str(), isMove != 0);
                        switch (ret)
                        {
                        case IDB_ALL:
                            workerState.FileOutLossEncrAll = TRUE; // the break; is intentionally missing here
                        case IDYES:
                            lossEncryptionAttr = TRUE;
                            break;

                        case IDB_SKIPALL:
                            workerState.SkipAllFileOutLossEncr = TRUE;
                        case IDB_SKIP:
                        {
                        SKIP_OPEN_OUT:

                            totalDone += op->Size;
                            SetTFSandPSforSkippedFile(op, lastTransferredFileSize, script, totalDone);

                            (void)CloseWorkerTrackedFile(in);
                            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                            if (skip != NULL)
                                *skip = TRUE;
                            return TRUE;
                        }

                        case IDCANCEL:
                        {
                        CANCEL_OPEN2:

                            (void)CloseWorkerTrackedFile(in);
                            return FALSE;
                        }
                        }
                    }
                    else
                    {
                    CREATE_ERROR:

                        DWORD err = GetLastError();
                        if (invalidTgtName)
                            err = ERROR_INVALID_NAME;
                        BOOL errDeletingFile = FALSE;
                        if (err == ERROR_FILE_EXISTS || // overwrite the file?
                            err == ERROR_ALREADY_EXISTS)
                        {
                            if (!workerState.OverwriteAll && (workerState.CnfrmFileOver || script->OverwriteOlder))
                            {
                                wchar_t sAttr[101], tAttr[101];
                                BOOL getTimeFailed;
                                getTimeFailed = FALSE;
                                FILETIME sFileTime, tFileTime;
                                GetFileOverwriteInfoW(sAttr, _countof(sAttr), in, op->SourceNameW.c_str(), &sFileTime, &getTimeFailed);
                                (void)CloseWorkerTrackedFile(in);
                                in = NULL;
                                out = op->OpenTargetFile(0, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, 0);
                                if (out != INVALID_HANDLE_VALUE)
                                {
                                    GetFileOverwriteInfoW(tAttr, _countof(tAttr), out, op->TargetNameW.c_str(), &tFileTime, &getTimeFailed);
                                    (void)CloseWorkerTrackedFile(out);
                                }
                                else
                                {
                                    getTimeFailed = TRUE;
                                    lstrcpynW(tAttr, LoadStrW(IDS_ERR_FILEOPEN), _countof(tAttr));
                                }
                                out = NULL;

                                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                if (observer.IsCancelled())
                                    goto CANCEL_OPEN;

                                if (workerState.SkipAllOverwrite)
                                    goto SKIP_OPEN;

                                int ret;
                                ret = IDCANCEL;

                                if (!getTimeFailed && script->OverwriteOlder) // option from the Copy/Move dialog
                                {
                                    // trim times to seconds (different file systems store times with different precision, so "differences" occurred even between "identical" times)
                                    *(unsigned __int64*)&sFileTime = *(unsigned __int64*)&sFileTime - (*(unsigned __int64*)&sFileTime % 10000000);
                                    *(unsigned __int64*)&tFileTime = *(unsigned __int64*)&tFileTime - (*(unsigned __int64*)&tFileTime % 10000000);

                                    if (CompareFileTime(&sFileTime, &tFileTime) > 0)
                                        ret = IDYES; // overwrite older files without asking
                                    else
                                        ret = IDB_SKIP; // skip other existing files
                                }
                                else
                                {
                                    // show the prompt
                                    ret = observer.AskOverwrite(op->TargetNameW.c_str(), tAttr, op->SourceNameW.c_str(), sAttr);
                                }
                                switch (ret)
                                {
                                case IDB_ALL:
                                    workerState.OverwriteAll = TRUE;
                                case IDYES:
                                default: // for safety (to prevent exiting this block with the 'in' handle closed)
                                {
                                    in = op->OpenSourceFile(asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN);
                                    if (in == INVALID_HANDLE_VALUE)
                                        goto OPEN_IN_ERROR;
                                    break;
                                }

                                case IDB_SKIPALL:
                                    workerState.SkipAllOverwrite = TRUE;
                                case IDB_SKIP:
                                {
                                SKIP_OPEN:

                                    totalDone += op->Size;
                                    SetTFSandPSforSkippedFile(op, lastTransferredFileSize, script, totalDone);

                                    observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                                    if (skip != NULL)
                                        *skip = TRUE;
                                    return TRUE;
                                }

                                case IDCANCEL:
                                {
                                CANCEL_OPEN:

                                    return FALSE;
                                }
                                }
                            }

                            DWORD attr = op->GetTargetAttributes();
                            if (attr != INVALID_FILE_ATTRIBUTES && (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                            {
                                if (!workerState.OverwriteHiddenAll && workerState.CnfrmSHFileOver) // ignore script->OverwriteOlder here; user wants to see that this is a SYSTEM or HIDDEN file even with the option enabled
                                {
                                    (void)CloseWorkerTrackedFile(in);
                                    in = NULL;

                                    observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                    if (observer.IsCancelled())
                                        goto CANCEL_OPEN;

                                    if (workerState.SkipAllSystemOrHidden)
                                        goto SKIP_OPEN;

                                    int ret = IDCANCEL;
                                    ret = observer.AskHiddenOrSystemById(IDS_CONFIRMFILEOVERWRITING, op->TargetNameW.c_str(), IDS_WANTOVERWRITESHFILE);
                                    switch (ret)
                                    {
                                    case IDB_ALL:
                                        workerState.OverwriteHiddenAll = TRUE;
                                    case IDYES:
                                    default: // for safety (to prevent exiting this block with the 'in' handle closed)
                                    {
                                        in = op->OpenSourceFile(asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN);
                                        if (in == INVALID_HANDLE_VALUE)
                                            goto OPEN_IN_ERROR;
                                        attr = op->GetTargetAttributes(); // refresh attributes in case the user changed them
                                        break;
                                    }

                                    case IDB_SKIPALL:
                                        workerState.SkipAllSystemOrHidden = TRUE;
                                    case IDB_SKIP:
                                        goto SKIP_OPEN;

                                    case IDCANCEL:
                                        goto CANCEL_OPEN;
                                    }
                                }
                            }

                            BOOL targetCannotOpenForWrite = FALSE;
                            while (1)
                            {
                                if (targetCannotOpenForWrite || mustDeleteFileBeforeOverwrite == 1 /* yes */)
                                { // the file must be deleted first
                                    BOOL chAttr = op->ClearTargetReadOnly(attr);

                                    if (!tgtNameCaseCorrected)
                                    {
                                        CorrectCaseOfTgtNameW(op->TargetNameW, FALSE, &dataOut);
                                        tgtNameCaseCorrected = TRUE;
                                    }

                                    if (op->DeleteTargetFile())
                                        goto OPEN_TGT_FILE; // if it is read-only (clearing the attribute may have failed), it can be deleted only on Samba with "delete readonly" enabled
                                    else                    // cannot delete either, end with an error...
                                    {
                                        err = GetLastError();
                                        if (chAttr)
                                            op->SetTargetAttributes(attr);
                                        errDeletingFile = TRUE;
                                        goto NORMAL_ERROR;
                                    }
                                }
                                else // overwrite the file in place
                                {
                                    // if we have not yet tested truncating the file to zero, obtain the current file size
                                    CQuadWord origFileSize(0, 0); // file size before truncation
                                    if (mustDeleteFileBeforeOverwrite == 0 /* need test */)
                                    {
                                        out = op->OpenTargetFile(0, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, 0);
                                        if (out != INVALID_HANDLE_VALUE)
                                        {
                                            if (!GetWorkerHandleSize(out, origFileSize).success)
                                                origFileSize.Set(0, 0); // error => set the size to zero and test it on another file
                                            (void)CloseWorkerTrackedFile(out);
                                        }
                                    }

                                    // open the file with ADS removal and truncation to zero
                                    BOOL chAttr = FALSE;
                                    if (attr != INVALID_FILE_ATTRIBUTES &&
                                        (attr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                                    { // CREATE_ALWAYS does not play well with read-only, hidden, or system attributes, so drop them if needed
                                        chAttr = TRUE;
                                        op->SetTargetAttributes(0);
                                    }
                                    // GENERIC_READ for 'out' slows asynchronous copying from disk to network (measured 95 MB/s instead of 111 MB/s on Win7 x64 GLAN)
                                    DWORD access = GENERIC_WRITE | (script->CopyAttrs ? GENERIC_READ : 0);
                                    fileAttrs = asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN |
                                                (!lossEncryptionAttr && copyAsEncrypted ? FILE_ATTRIBUTE_ENCRYPTED : 0) | // setting attributes during CREATE_ALWAYS works since XP and is the only way to apply Encrypted attribute when the file denies read access
                                                (script->CopyAttrs ? (op->Attr & (FILE_ATTRIBUTE_COMPRESSED | (lossEncryptionAttr ? 0 : FILE_ATTRIBUTE_ENCRYPTED))) : 0);
                                    out = op->OpenTargetFile(access, 0, CREATE_ALWAYS, fileAttrs);
                                    if (out == INVALID_HANDLE_VALUE && fileAttrs != (asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN)) // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
                                        out = op->OpenTargetFile(access, 0, CREATE_ALWAYS, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN);
                                    if (script->CopyAttrs && out == INVALID_HANDLE_VALUE)
                                    { // if read access to the directory is denied (we added it only for setting the Compressed attribute), try opening the file for write only
                                        access = GENERIC_WRITE;
                                        out = op->OpenTargetFile(access, 0, CREATE_ALWAYS, fileAttrs);
                                        if (out == INVALID_HANDLE_VALUE && fileAttrs != (asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN)) // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
                                            out = op->OpenTargetFile(access, 0, CREATE_ALWAYS, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN);
                                    }
                                    if (out == INVALID_HANDLE_VALUE) // target file cannot be opened for writing, so delete it and create it again
                                    {
                                        // handles the situation when a Samba file must be overwritten:
                                        // the file has mode 440+different_owner and sits in a directory where the current user has write access
                                        // (deletion works, but direct overwrite does not (cannot open for writing) - workaround:
                                        //  delete and recreate the file)
                                        // (Samba can allow deleting read-only files, which enables deleting them,
                                        //  otherwise Windows cannot delete a read-only file and we cannot drop
                                        //  the "read-only" attribute because the current user is not the owner)
                                        if (chAttr)
                                            op->SetTargetAttributes(attr);
                                        targetCannotOpenForWrite = TRUE;
                                        continue;
                                    }

                                    // on target paths that support ADS also delete ADS on the target file (CREATE_ALWAYS should remove them, but on home W2K and XP they simply stay; no idea why, W2K and XP in VMWare delete ADS normally)
                                    if (script->TargetPathSupADS && !DeleteAllADS(out, op->TargetNameW))
                                    {
                                        (void)CloseWorkerTrackedFile(out);
                                        out = INVALID_HANDLE_VALUE;
                                        if (chAttr)
                                            op->SetTargetAttributes(attr);
                                        targetCannotOpenForWrite = TRUE;
                                        continue;
                                    }

                                    // if we have not yet tested truncating the file to zero, obtain the new file size
                                    if (mustDeleteFileBeforeOverwrite == 0 /* need test */)
                                    {
                                        (void)CloseWorkerTrackedFile(out);
                                        out = op->OpenTargetFile(access, 0, OPEN_ALWAYS, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN);
                                        if (out == INVALID_HANDLE_VALUE) // cannot reopen the target file we just opened, unlikely, try deleting and recreating it
                                        {
                                            targetCannotOpenForWrite = TRUE;
                                            continue;
                                        }
                                        CQuadWord newFileSize(0, 0); // file size after truncation
                                        const FileResult sizeResult = GetWorkerHandleSize(out, newFileSize);
                                        if (sizeResult.success && // we have the new size
                                            newFileSize == CQuadWord(0, 0))                                             // file really has 0 bytes
                                        {
                                            if (origFileSize != CQuadWord(0, 0))            // truncation can only be tested on a non-zero file
                                                mustDeleteFileBeforeOverwrite = 2; /* no */ // success (not a SNAP server - NSA drive, truncation does not work there)
                                        }
                                        else
                                        {
                                            (void)CloseWorkerTrackedFile(out);
                                            out = INVALID_HANDLE_VALUE;
                                            mustDeleteFileBeforeOverwrite = 1 /* yes */; // on error or when the size is non-zero, play it safe...
                                            continue;
                                        }
                                    }

                                    if (script->CopyAttrs || !lossEncryptionAttr && copyAsEncrypted)
                                    {
                                        encryptionNotSupported = FALSE;
                                        SetCompressAndEncryptedAttrs((!lossEncryptionAttr && copyAsEncrypted ? FILE_ATTRIBUTE_ENCRYPTED : 0) | (script->CopyAttrs ? (op->Attr & (FILE_ATTRIBUTE_COMPRESSED | (lossEncryptionAttr ? 0 : FILE_ATTRIBUTE_ENCRYPTED))) : 0),
                                                                     &out, script->CopyAttrs, &encryptionNotSupported, asyncPar, op->TargetNameW);
                                        if (encryptionNotSupported) // unable to apply the Encrypted attribute, ask the user what to do...
                                        {
                                            if (workerState.FileOutLossEncrAll)
                                                lossEncryptionAttr = TRUE;
                                            else
                                            {
                                                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                                if (observer.IsCancelled())
                                                    goto CANCEL_ENCNOTSUP;

                                                if (workerState.SkipAllFileOutLossEncr)
                                                    goto SKIP_ENCNOTSUP;

                                                int ret;
                                                ret = IDCANCEL;
                                                ret = observer.AskEncryptionLoss(true, op->TargetNameW.c_str(), isMove != 0);
                                                switch (ret)
                                                {
                                                case IDB_ALL:
                                                    workerState.FileOutLossEncrAll = TRUE; // the break; is intentionally missing here
                                                case IDYES:
                                                    lossEncryptionAttr = TRUE;
                                                    break;

                                                case IDB_SKIPALL:
                                                    workerState.SkipAllFileOutLossEncr = TRUE;
                                                case IDB_SKIP:
                                                    goto SKIP_ENCNOTSUP;

                                                case IDCANCEL:
                                                    goto CANCEL_ENCNOTSUP;
                                                }
                                            }
                                        }
                                    }
                                }
                                break;
                            }

                            goto COPY;
                        }
                        else // regular error
                        {
                        NORMAL_ERROR:

                            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                            if (observer.IsCancelled())
                                goto CANCEL_OPEN2;

                            if (workerState.SkipAllFileOpenOut)
                                goto SKIP_OPEN_OUT;

                            int ret;
                            ret = IDCANCEL;
                            ret = observer.AskFileErrorById(errDeletingFile ? IDS_ERRORDELETINGFILE : IDS_ERROROPENINGFILE, op->TargetNameW.c_str(), err);
                            switch (ret)
                            {
                            case IDRETRY:
                                break;

                            case IDB_SKIPALL:
                                workerState.SkipAllFileOpenOut = TRUE;
                            case IDB_SKIP:
                                goto SKIP_OPEN_OUT;

                            case IDCANCEL:
                                goto CANCEL_OPEN2;
                            }
                        }
                    }
                }
            }
        }
        else
        {
        OPEN_IN_ERROR:

            DWORD err = GetLastError();
            if (invalidSrcName)
                err = ERROR_INVALID_NAME;
            if (asyncPar->Failed())
                err = ERROR_NOT_ENOUGH_MEMORY;                         // cannot create the synchronization event = lack of resources (will probably never happens, so we do not bother)
            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
                return FALSE;

            if (workerState.SkipAllFileOpenIn)
                goto SKIP_OPEN_IN;

            int ret;
            ret = IDCANCEL;
            ret = observer.AskFileErrorById(IDS_ERROROPENINGFILE, op->SourceNameW.c_str(), err);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                workerState.SkipAllFileOpenIn = TRUE;
            case IDB_SKIP:
            {
            SKIP_OPEN_IN:

                totalDone += op->Size;
                SetTFSandPSforSkippedFile(op, lastTransferredFileSize, script, totalDone);

                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                if (skip != NULL)
                    *skip = TRUE;
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

BOOL DoMoveFile(COperation* op, IWorkerObserver& observer, void* buffer,
                COperations* script, CQuadWord& totalDone, BOOL dir,
                DWORD clearReadonlyMask, BOOL* novellRenamePatch, BOOL lantasticCheck,
                int& mustDeleteFileBeforeOverwrite, int& allocWholeFileOnStart,
                CWorkerState& workerState, BOOL copyADS, BOOL copyAsEncrypted,
                BOOL* setDirTimeAfterMove, CAsyncCopyParams*& asyncPar,
                BOOL ignInvalidName)
{
    if (script->CopyAttrs && copyAsEncrypted)
        TRACE_E("DoMoveFile(): unexpected parameter value: copyAsEncrypted is TRUE when script->CopyAttrs is TRUE!");

    // if the path ends with a space/dot, it is invalid and we must not move it,
    // MoveFile would trim the spaces/dots and move a different file or under a different name,
    // directories fare better: appending a backslash helps there, we block the move
    // only when a new directory name would be invalid (when moving under the old
    // name, 'ignInvalidName' is TRUE)
    BOOL invalidName = op->IsSourceNameInvalid(dir) ||
                       op->IsTargetNameInvalid(dir && ignInvalidName);

    if (!copyAsEncrypted && !script->SameRootButDiffVolume && op->HasSameRootPath())
    {
        // if the path ends with a space or dot, we must append '\\', otherwise GetNamedSecurityInfo,
        // GetDirTime, SetFileAttributes, and MoveFile trim the spaces/dots and operate on a different path
        std::wstring effectiveSourceW = op->SourceNameW;
        std::wstring effectiveTargetW = op->TargetNameW;
        std::wstring sourceNameMvDirW = MakeCopyWithBackslashIfNeededW(effectiveSourceW.c_str());
        std::wstring targetNameMvDirW = MakeCopyWithBackslashIfNeededW(effectiveTargetW.c_str());
        // true when path was NOT modified (no trailing space/dot) — replaces old pointer-equality check
        bool sourceNameUnmodified = (sourceNameMvDirW == effectiveSourceW);
        bool targetNameUnmodified = (targetNameMvDirW == effectiveTargetW);

        int autoRetryAttempts = 0;
        CSrcSecurity srcSecurity;
        BOOL srcSecurityErr = FALSE;
        if (!invalidName && script->CopySecurity) // should we copy NTFS security permissions?
        {
            const FileResult securityResult = GetWorkerFileSystem()->GetPathSecurity(
                sourceNameMvDirW.c_str(), srcSecurity.Descriptor);
            srcSecurity.SrcError = securityResult.success ? ERROR_SUCCESS : securityResult.errorCode;
            if (srcSecurity.SrcError != ERROR_SUCCESS) // failed to read security info from the source file -> nothing to apply on the target
            {
                srcSecurityErr = TRUE;
                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                if (observer.IsCancelled())
                    return FALSE;

                int ret;
                ret = IDCANCEL;
                if (workerState.IgnoreAllCopyPermErr)
                    ret = IDB_IGNORE;
                else
                {
                    ret = observer.AskCopyPermError(op->SourceNameW.c_str(), op->TargetNameW.c_str(), srcSecurity.SrcError);
                }
                switch (ret)
                {
                case IDB_IGNOREALL:
                    workerState.IgnoreAllCopyPermErr = TRUE; // the break; is intentionally missing here
                case IDB_IGNORE:
                    break;

                case IDCANCEL:
                    return FALSE;
                }
            }
        }
        FILETIME dirTimeModified;
        BOOL dirTimeModifiedIsValid = FALSE;
        if (!invalidName && dir && !*novellRenamePatch && *setDirTimeAfterMove != 2 /* no */) // the issue apparently does not apply to Novell Netware, so ignore it there (affects e.g. Samba)
            dirTimeModifiedIsValid = GetDirTimeW(sourceNameMvDirW.c_str(), &dirTimeModified);
        while (1)
        {
            if (!invalidName && !*novellRenamePatch && gFileSystem->MoveFile(sourceNameMvDirW.c_str(), targetNameMvDirW.c_str()).success)
            {
                if (script->CopyAttrs && (op->Attr & FILE_ATTRIBUTE_ARCHIVE) == 0) // Archive attribute was not set, MoveFile turned it on, clear it again
                    GetWorkerFileSystem()->SetFileAttributes(targetNameMvDirW.c_str(), op->Attr); // leave without handling or retry, not important (it normally toggles chaotically)

            OPERATION_DONE:

                DWORD curAttrs = INVALID_FILE_ATTRIBUTES;
                if (script->CopyAttrs) // check whether the source file attributes were preserved
                {
                    curAttrs = GetWorkerFileSystem()->GetFileAttributes(targetNameMvDirW.c_str());
                    if (curAttrs == INVALID_FILE_ATTRIBUTES || (curAttrs & DISPLAYED_ATTRIBUTES) != (op->Attr & DISPLAYED_ATTRIBUTES))
                    {                                                              // attributes probably were not preserved, warn the user
                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                        if (observer.IsCancelled())
                            goto MOVE_ERROR_2;

                        int ret;
                        ret = IDCANCEL;
                        if (workerState.IgnoreAllSetAttrsErr)
                            ret = IDB_IGNORE;
                        else
                        {
                            ret = observer.AskSetAttrsError(op->TargetNameW.c_str(), (op->Attr & DISPLAYED_ATTRIBUTES), (curAttrs == INVALID_FILE_ATTRIBUTES ? 0 : (curAttrs & DISPLAYED_ATTRIBUTES)));
                        }
                        switch (ret)
                        {
                        case IDB_IGNOREALL:
                            workerState.IgnoreAllSetAttrsErr = TRUE; // the break; is intentionally missing here
                        case IDB_IGNORE:
                            break;

                        case IDCANCEL:
                        {
                        MOVE_ERROR_2:

                            return FALSE; // the file was moved to the target + cancel occurred; but we would rather not move it back, nobody should mind much
                        }
                        }
                    }
                }

                if (script->CopySecurity && !srcSecurityErr) // should we copy NTFS security permissions?
                {
                    DWORD err;
                    if (!DoCopySecurity(&err, &srcSecurity,
                                       sourceNameMvDirW, targetNameMvDirW))
                    {
                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                        if (observer.IsCancelled())
                            goto MOVE_ERROR_2;

                        int ret;
                        ret = IDCANCEL;
                        if (workerState.IgnoreAllCopyPermErr)
                            ret = IDB_IGNORE;
                        else
                        {
                            ret = observer.AskCopyPermError(op->SourceNameW.c_str(), op->TargetNameW.c_str(), err);
                        }
                        switch (ret)
                        {
                        case IDB_IGNOREALL:
                            workerState.IgnoreAllCopyPermErr = TRUE; // the break; is intentionally missing here
                        case IDB_IGNORE:
                            break;

                        case IDCANCEL:
                            goto MOVE_ERROR_2;
                        }
                    }
                }

                if (dir && dirTimeModifiedIsValid && *setDirTimeAfterMove != 2 /* no */)
                {
                    FILETIME movedDirTimeModified;
                    BOOL gotTime = GetDirTimeW(targetNameMvDirW.c_str(), &movedDirTimeModified);
                    if (gotTime)
                    {
                        if (CompareFileTime(&dirTimeModified, &movedDirTimeModified) == 0)
                        {
                            if (*setDirTimeAfterMove == 0 /* need test */)
                                *setDirTimeAfterMove = 2 /* no */;
                        }
                        else
                        {
                            if (*setDirTimeAfterMove == 0 /* need test */)
                                *setDirTimeAfterMove = 1 /* yes */;
                            DoCopyDirTime(observer, &dirTimeModified, workerState, TRUE, targetNameMvDirW); // ignore any failure, this is just a hack (we already ignore time read errors from the directory); MoveFile should not change times
                        }
                    }
                }

                script->AddBytesToSpeedMetersAndTFSandPS((DWORD)op->Size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

                totalDone += op->Size;
                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                return TRUE;
            }
            else
            {
                DWORD err = GetLastError();
                if (invalidName)
                    err = ERROR_INVALID_NAME;
                // Novell patch - before calling MoveFile we need to drop the read-only attribute
                if (!invalidName && *novellRenamePatch || err == ERROR_ACCESS_DENIED)
                {
                    DWORD attr = GetWorkerFileSystem()->GetFileAttributes(sourceNameMvDirW.c_str());
                    BOOL setAttr = ClearReadOnlyAttr(sourceNameMvDirW.c_str(), attr);
                    if (gFileSystem->MoveFile(sourceNameMvDirW.c_str(), targetNameMvDirW.c_str()).success)
                    {
                        if (!*novellRenamePatch)
                            *novellRenamePatch = TRUE; // the next operations will go straight through here
                        if (setAttr || script->CopyAttrs && (attr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                            GetWorkerFileSystem()->SetFileAttributes(targetNameMvDirW.c_str(), attr);

                        goto OPERATION_DONE;
                    }
                    err = GetLastError();
                    if (setAttr)
                        GetWorkerFileSystem()->SetFileAttributes(sourceNameMvDirW.c_str(), attr);
                }

                if (!op->AreSourceAndTargetSamePath() &&            // provided this is not just a change of case
                    (err == ERROR_FILE_EXISTS ||                    // verify whether this is only overwriting the DOS name of the file/directory
                     err == ERROR_ALREADY_EXISTS) &&
                    targetNameUnmodified) // no invalid names are allowed here (path was not modified by MakeCopy)
                {
                    WIN32_FIND_DATAW findData;
                    HANDLE find = op->FindFirstTarget(&findData);
                    if (find != INVALID_HANDLE_VALUE)
                    {
                        SalLPFindClose(find);
                        const wchar_t* tgtLeaf = wcsrchr(op->TargetNameW.c_str(), L'\\');
                        tgtLeaf = tgtLeaf != NULL ? tgtLeaf + 1 : op->TargetNameW.c_str();
                        if (_wcsicmp(tgtLeaf, findData.cAlternateFileName) == 0 && // match only on the DOS name
                            _wcsicmp(tgtLeaf, findData.cFileName) != 0)     // (the full name is different)
                        {
                            // rename ("tidy up") the file/directory with the conflicting DOS name to a temporary 8.3 name (does not need an extra DOS name)
                            std::wstring tmpNameW = effectiveTargetW;
                            CutDirectoryW(tmpNameW);
                            SalPathAddBackslashW(tmpNameW);
                            size_t tmpNamePartPos = tmpNameW.size();
                            std::wstring origFullNameW = tmpNameW;
                            SalPathAppendW(origFullNameW, findData.cFileName); // use wide cFileName directly — no ANSI lossy conversion
                            tmpNameW = origFullNameW; // start with the original full name path as base for the temp name
                            {
                                DWORD num = (GetTickCount() / 10) % 0xFFF;
                                DWORD origFullNameAttr = GetWorkerFileSystem()->GetFileAttributes(origFullNameW.c_str());
                                while (1)
                                {
                                    wchar_t tmpSuffix[8];
                                    swprintf(tmpSuffix, _countof(tmpSuffix), L"sal%03X", num++);
                                    tmpNameW.resize(tmpNamePartPos);
                                    tmpNameW += tmpSuffix;
                                    const FileResult tidyResult = GetWorkerFileSystem()->MoveFile(
                                        origFullNameW.c_str(), tmpNameW.c_str());
                                    if (tidyResult.success)
                                        break;
                                    DWORD e = tidyResult.errorCode;
                                    if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS)
                                    {
                                        tmpNameW.clear();
                                        break;
                                    }
                                }
                                if (!tmpNameW.empty()) // if we managed to "tidy up" the conflicting file/directory, try moving it again
                                {                      // then restore the original name of the "tidied" file/directory
                                    BOOL moveDone = gFileSystem->MoveFile(sourceNameMvDirW.c_str(), targetNameMvDirW.c_str()).success;
                                    if (script->CopyAttrs && (op->Attr & FILE_ATTRIBUTE_ARCHIVE) == 0) // the Archive attribute was not set; MoveFile turned it on, clear it again
                                        op->SetTargetAttributes(op->Attr);                   // leave without handling or retry, not important (it normally toggles chaotically)
                                    if (!GetWorkerFileSystem()->MoveFile(
                                             tmpNameW.c_str(), origFullNameW.c_str()).success)
                                    { // this apparently can happen; inexplicably, Windows creates a file named origFullName instead of op->TargetName (the DOS name)
                                        TRACE_I("DoMoveFile(): Unexpected situation: unable to rename file/dir from tmp-name to original long file name!");
                                        if (moveDone)
                                        {
                                            if (gFileSystem->MoveFile(targetNameMvDirW.c_str(), sourceNameMvDirW.c_str()).success)
                                                moveDone = FALSE;
                                            if (!GetWorkerFileSystem()->MoveFile(
                                                     tmpNameW.c_str(), origFullNameW.c_str()).success)
                                                TRACE_E("DoMoveFile(): Fatal unexpected situation: unable to rename file/dir from tmp-name to original long file name!");
                                        }
                                    }
                                    else
                                    {
                                        if ((origFullNameAttr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                                            GetWorkerFileSystem()->SetFileAttributes(origFullNameW.c_str(), origFullNameAttr); // leave without handling or retry, not important (it normally toggles chaotically)
                                    }

                                    if (moveDone)
                                        goto OPERATION_DONE;
                                }
                            }
                        }
                    }
                }

                if ((err == ERROR_ALREADY_EXISTS || // theoretically can happen for directories; prevent that (overwrite prompt is only for files)
                     err == ERROR_FILE_EXISTS) &&
                    !dir && !op->AreSourceAndTargetSamePath() &&
                    sourceNameUnmodified && targetNameUnmodified) // no invalid names allowed here (paths were not modified by MakeCopy)
                {
                    HANDLE in, out;
                    in = op->OpenSourceFile(FILE_ATTRIBUTE_NORMAL);
                    if (in == INVALID_HANDLE_VALUE)
                    {
                        err = GetLastError();
                        goto NORMAL_ERROR;
                    }
                    out = op->OpenTargetFile(0, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
                    if (out == INVALID_HANDLE_VALUE)
                    {
                        err = GetLastError();
                        (void)CloseWorkerTrackedFile(in);
                        goto NORMAL_ERROR;
                    }

                    if (!workerState.OverwriteAll && (workerState.CnfrmFileOver || script->OverwriteOlder))
                    {
                        wchar_t sAttr[101], tAttr[101];
                        BOOL getTimeFailed;
                        getTimeFailed = FALSE;
                        FILETIME sFileTime, tFileTime;
                        GetFileOverwriteInfoW(sAttr, _countof(sAttr), in, op->SourceNameW.c_str(), &sFileTime, &getTimeFailed);
                        GetFileOverwriteInfoW(tAttr, _countof(tAttr), out, op->TargetNameW.c_str(), &tFileTime, &getTimeFailed);
                        (void)CloseWorkerTrackedFile(in);
                        (void)CloseWorkerTrackedFile(out);

                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                        if (observer.IsCancelled())
                            goto CANCEL_OPEN;

                        if (dir)
                            TRACE_E("Error in script.");

                        if (workerState.SkipAllOverwrite)
                            goto SKIP_OPEN;

                        int ret;
                        ret = IDCANCEL;

                        if (!getTimeFailed && script->OverwriteOlder) // option from the Copy/Move dialog
                        {
                            // trim timestamps to seconds (different file systems store times with different precision, leading to "differences" even between "matching" times)
                            *(unsigned __int64*)&sFileTime = *(unsigned __int64*)&sFileTime - (*(unsigned __int64*)&sFileTime % 10000000);
                            *(unsigned __int64*)&tFileTime = *(unsigned __int64*)&tFileTime - (*(unsigned __int64*)&tFileTime % 10000000);

                            if (CompareFileTime(&sFileTime, &tFileTime) > 0)
                                ret = IDYES; // older ones should be overwritten without asking
                            else
                                ret = IDB_SKIP; // skip the other existing ones
                        }
                        else
                        {
                            // display the prompt
                            ret = observer.AskOverwrite(op->TargetNameW.c_str(), tAttr, op->SourceNameW.c_str(), sAttr);
                        }
                        switch (ret)
                        {
                        case IDB_ALL:
                            workerState.OverwriteAll = TRUE;
                        case IDYES:
                            break;

                        case IDB_SKIPALL:
                            workerState.SkipAllOverwrite = TRUE;
                        case IDB_SKIP:
                        {
                        SKIP_OPEN:

                            totalDone += op->Size;
                            script->SetProgressSize(totalDone);
                            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                            return TRUE;
                        }

                        case IDCANCEL:
                        {
                        CANCEL_OPEN:

                            return FALSE;
                        }
                        }
                    }
                    else
                    {
                        (void)CloseWorkerTrackedFile(in);
                        (void)CloseWorkerTrackedFile(out);
                    }

                    DWORD attr = op->GetTargetAttributes();
                    if (attr != INVALID_FILE_ATTRIBUTES && (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                    {
                        if (!workerState.OverwriteHiddenAll && workerState.CnfrmSHFileOver) // ignore script->OverwriteOlder here; user wants to see that this is a SYSTEM or HIDDEN file even with the option enabled
                        {
                            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                            if (observer.IsCancelled())
                                goto CANCEL_OPEN;

                            if (dir)
                                TRACE_E("Error in script.");

                            if (workerState.SkipAllSystemOrHidden)
                                goto SKIP_OPEN;

                            int ret = IDCANCEL;
                            ret = observer.AskHiddenOrSystemById(IDS_CONFIRMFILEOVERWRITING, op->TargetNameW.c_str(), IDS_WANTOVERWRITESHFILE);
                            switch (ret)
                            {
                            case IDB_ALL:
                                workerState.OverwriteHiddenAll = TRUE;
                            case IDYES:
                                break;

                            case IDB_SKIPALL:
                                workerState.SkipAllSystemOrHidden = TRUE;
                            case IDB_SKIP:
                                goto SKIP_OPEN;

                            case IDCANCEL:
                                goto CANCEL_OPEN;
                            }
                            attr = op->GetTargetAttributes(); // may also fail (returns INVALID_FILE_ATTRIBUTES)
                        }
                    }

                    op->ClearTargetReadOnly(attr); // make sure it can be deleted ...
                    while (1)
                    {
                        if (op->DeleteTargetFile())
                            break;
                        else
                        {
                            DWORD err2 = GetLastError();
                            if (err2 == ERROR_FILE_NOT_FOUND)
                                break; // if the user already deleted the file manually, everything is fine

                            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                            if (observer.IsCancelled())
                                return FALSE;

                            if (dir)
                                TRACE_E("Error in script.");

                            if (workerState.SkipAllOverwriteErr)
                                goto SKIP_OVERWRITE_ERROR;

                            int ret;
                            ret = IDCANCEL;
                            ret = observer.AskFileErrorById(IDS_ERROROVERWRITINGFILE, op->TargetNameW.c_str(), err2);
                            switch (ret)
                            {
                            case IDRETRY:
                                break;

                            case IDB_SKIPALL:
                                workerState.SkipAllOverwriteErr = TRUE;
                            case IDB_SKIP:
                            {
                            SKIP_OVERWRITE_ERROR:

                                totalDone += op->Size;
                                script->SetProgressSize(totalDone);
                                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                                return TRUE;
                            }

                            case IDCANCEL:
                                return FALSE;
                            }
                        }
                    }
                }
                else
                {
                NORMAL_ERROR:

                    observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                    if (observer.IsCancelled())
                        return FALSE;

                    if (workerState.SkipAllMoveErrors)
                        goto SKIP_MOVE_ERROR;

                    if (err == ERROR_SHARING_VIOLATION && ++autoRetryAttempts <= 2)
                    {               // auto-retry added to handle move errors while directory icons are being read (SHGetFileInfo running in parallel with MoveFile)
                        Sleep(100); // wait a moment before the next attempt
                    }
                    else
                    {
                        int ret;
                        ret = IDCANCEL;
                        ret = observer.AskCannotMoveErr(op->SourceNameW.c_str(), op->TargetNameW.c_str(), err, dir != FALSE);
                        switch (ret)
                        {
                        case IDRETRY:
                            break;

                        case IDB_SKIPALL:
                            workerState.SkipAllMoveErrors = TRUE;
                        case IDB_SKIP:
                        {
                        SKIP_MOVE_ERROR:

                            totalDone += op->Size;
                            script->SetProgressSize(totalDone);
                            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                            return TRUE;
                        }

                        case IDCANCEL:
                            return FALSE;
                        }
                    }
                }
            }
        }
    }
    else
    {
        if (dir)
        {
            TRACE_E("Error in script.");
            return FALSE;
        }

        BOOL skip;
        BOOL notError = DoCopyFile(op, observer, buffer, script, totalDone,
                                   clearReadonlyMask, &skip, lantasticCheck,
                                   mustDeleteFileBeforeOverwrite, allocWholeFileOnStart,
                                   workerState, copyADS, copyAsEncrypted, TRUE, asyncPar);
        if (notError && !skip) // still need to clean up the file from the source
        {
            op->ClearSourceReadOnly(); // ensure it can be deleted
            while (1)
            {
                if (op->DeleteSourceFile())
                    break;
                {
                    DWORD err = GetLastError();

                    observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                    if (observer.IsCancelled())
                        return FALSE;

                    if (workerState.SkipAllDeleteErr)
                        return TRUE;

                    int ret = IDCANCEL;
                    ret = observer.AskFileErrorById(IDS_ERRORDELETINGFILE, op->SourceNameW.c_str(), err);
                    switch (ret)
                    {
                    case IDRETRY:
                        break;
                    case IDB_SKIPALL:
                        workerState.SkipAllDeleteErr = TRUE;
                    case IDB_SKIP:
                        return TRUE;
                    case IDCANCEL:
                        return FALSE;
                    }
                }
            }
        }
        return notError;
    }
}

BOOL DoDeleteFile(IWorkerObserver& observer, const CQuadWord& size, COperations* script,
                  CQuadWord& totalDone, DWORD attr, CWorkerState& workerState,
                  const std::wstring& nameW)
{
    // if the path ends with a space/dot it is invalid and we must not delete it,
    // DeleteFile would trim the spaces/dots and remove a different file
    std::wstring effectiveDeleteNameW = nameW;
    BOOL deleteNameIsNul = ShouldBypassRecycleBinForDeleteW(effectiveDeleteNameW.c_str());
    BOOL invalidName = FileNameIsInvalidW(effectiveDeleteNameW.c_str(), TRUE) && !deleteNameIsNul;

    DWORD err;
    while (1)
    {
        if (!invalidName)
        {
            if (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
            {
                if (!workerState.DeleteHiddenAll && workerState.CnfrmSHFileDel)
                {
                    observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                    if (observer.IsCancelled())
                        return FALSE;

                    if (workerState.SkipAllSystemOrHidden)
                        goto SKIP_DELETE;

                    int ret = IDCANCEL;
                    ret = observer.AskHiddenOrSystemById(IDS_CONFIRMSHFILEDELETE, nameW.c_str(), IDS_DELETESHFILE);
                    switch (ret)
                    {
                    case IDB_ALL:
                        workerState.DeleteHiddenAll = TRUE;
                    case IDYES:
                        break;

                    case IDB_SKIPALL:
                        workerState.SkipAllSystemOrHidden = TRUE;
                    case IDB_SKIP:
                        goto SKIP_DELETE;

                    case IDCANCEL:
                        return FALSE;
                    }
                }
            }
            {
                std::wstring effectiveNameW = nameW;
                ClearReadOnlyAttr(effectiveNameW.c_str(), attr); // ensure it can be deleted
            }

            err = ERROR_SUCCESS;
            BOOL useRecycleBin;
            switch (workerState.UseRecycleBin)
            {
            case 0:
                useRecycleBin = script->CanUseRecycleBin && script->InvertRecycleBin;
                break;
            case 1:
                useRecycleBin = script->CanUseRecycleBin && !script->InvertRecycleBin;
                break;
            case 2:
            {
                if (!script->CanUseRecycleBin || script->InvertRecycleBin)
                    useRecycleBin = FALSE;
                else
                {
                    // leafW is already the exact wide leaf name - use it
                    // directly against AgreeRecycleMasksW instead of round-tripping to
                    // ANSI (only to feed a narrow API that itself re-widens internally)
                    // and, for names that don't survive that round-trip exactly,
                    // unconditionally recycling instead of actually running the mask
                    // check. AgreeMasks self-computes the extension when passed NULL,
                    // the same algorithm the removed manual scan implemented by hand.
                    const wchar_t* leafW = wcsrchr(nameW.c_str(), L'\\');
                    leafW = leafW != NULL ? leafW + 1 : nameW.c_str();
                    useRecycleBin = workerState.AgreeRecycleMasksW(leafW, NULL);
                }
                break;
            }
            }
            if (deleteNameIsNul)
                useRecycleBin = FALSE;

            if (useRecycleBin)
            {
                std::wstring effectiveNameW = nameW;
                // SHFileOperationW needs double-null terminated wide path
                std::wstring nameList = effectiveNameW;
                nameList.push_back(L'\0'); // double-null termination
                if (!PathContainsValidComponents(nameList.c_str()))
                {
                    err = ERROR_INVALID_NAME;
                }
                else
                {
                    // via IShell; the interface adds FOF_NOERRORUI
                    // (the worker owns error display - no double dialog) and maps
                    // a user abort to ERROR_CANCELLED. err may be a shell DE_*
                    // code, as before.
                    CShellExecuteWnd shellExecuteWnd;
                    HWND parentWnd = shellExecuteWnd.Create(observer.GetParentWindow(), L"SEW: DoDeleteFile");
                    ShellResult recycleRes = gShell->MoveToRecycleBin({effectiveNameW}, parentWnd);
                    err = recycleRes.success ? ERROR_SUCCESS : recycleRes.errorCode;
                }
            }
            else
            {
                std::wstring effectiveNameW = nameW;
                if (deleteNameIsNul)
                {
                    IFileSystem* fileSystem = GetWorkerFileSystem();
                    if (fileSystem == NULL)
                        err = ERROR_INVALID_FUNCTION;
                    else
                    {
                        FileResult deleteRes = fileSystem->DeleteFile(effectiveNameW.c_str());
                        if (!deleteRes.success)
                            err = deleteRes.errorCode;
                    }
                }
                else
                {
                    const FileResult deleteResult = GetWorkerFileSystem()->DeleteFile(effectiveNameW.c_str());
                    if (!deleteResult.success)
                        err = deleteResult.errorCode;
                }
            }
        }
        else
        {
            err = ERROR_INVALID_NAME;
        }
        if (err == ERROR_SUCCESS)
        {
            totalDone += size;
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
            return TRUE;
        }
        else
        {
            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
                return FALSE;

            if (workerState.SkipAllDeleteErr)
                goto SKIP_DELETE;

            int ret;
            ret = IDCANCEL;
            ret = observer.AskFileErrorById(IDS_ERRORDELETINGFILE, effectiveDeleteNameW.c_str(), err);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                workerState.SkipAllDeleteErr = TRUE;
            case IDB_SKIP:
            {
            SKIP_DELETE:

                totalDone += size;
                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
        if (!invalidName)
        {
            std::wstring effectiveNameW = nameW;
            DWORD attr2 = GetWorkerFileSystem()->GetFileAttributes(effectiveNameW.c_str()); // get the current attribute state
            if (attr2 != INVALID_FILE_ATTRIBUTES)
                attr = attr2;
        }
    }
}

// 2026-08-25: the narrow SalCreateDirectoryEx(char*, ...) and GetDirTime(char*,
// ...) thin adapters were deleted - confirmed-dead (zero callers anywhere; SalCreateDirectoryEx's
// legacy v107 ABI shim forwards to WideGeneral.SalCreateDirectoryEx, never to this free function;
// GetDirTime has no ABI entry at all, a pure core-internal orphan). Their wide siblings below are
// the sole surviving implementations.
BOOL GetDirTimeW(const wchar_t* dirName, FILETIME* ftModified)
{
    HANDLE dir;
    dir = GetWorkerFileSystem()->CreateFile(dirName, GENERIC_READ,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL, OPEN_EXISTING,
                      FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (dir != INVALID_HANDLE_VALUE)
    {
        BOOL ret = GetWorkerFileSystem()->GetHandleFileTime(
                       dir, NULL /*ftCreated*/, NULL /*ftAccessed*/, ftModified).success;
        (void)CloseWorkerTrackedFile(dir);
        return ret;
    }
    return FALSE;
}

BOOL DoCopyDirTime(IWorkerObserver& observer, FILETIME* modified, CWorkerState& workerState, BOOL quiet,
                   const std::wstring& targetNameW)
{
    // if the path ends with a space/dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    std::wstring targetNameCrFileW = MakeCopyWithBackslashIfNeededW(targetNameW.c_str());

    BOOL showError = !quiet;
    DWORD error = NO_ERROR;
    DWORD attr = GetWorkerFileSystem()->GetFileAttributes(targetNameCrFileW.c_str());
    BOOL setAttr = FALSE;
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
    {
        GetWorkerFileSystem()->SetFileAttributes(targetNameCrFileW.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
        setAttr = TRUE;
    }
    HANDLE file = GetWorkerFileSystem()->CreateFile(targetNameCrFileW.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        const FileResult timeResult = GetWorkerFileSystem()->SetHandleFileTime(
            file, NULL /*&ftCreated*/, NULL /*&ftAccessed*/, modified);
        if (timeResult.success)
            showError = FALSE; // success!
        else
            error = timeResult.errorCode;
        (void)CloseWorkerTrackedFile(file);
    }
    else
        error = GetLastError();
    if (setAttr)
        GetWorkerFileSystem()->SetFileAttributes(targetNameCrFileW.c_str(), attr);

    if (showError)
    {
        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
        if (observer.IsCancelled())
            return FALSE;

        int ret;
        ret = IDCANCEL;
        if (workerState.IgnoreAllCopyDirTimeErr)
            ret = IDB_IGNORE;
        else
        {
            ret = observer.AskCopyDirTimeError(targetNameW.empty() ? NULL : targetNameW.c_str(), error);
        }
        switch (ret)
        {
        case IDB_IGNOREALL:
            workerState.IgnoreAllCopyDirTimeErr = TRUE; // break intentionally omitted here
        case IDB_IGNORE:
            break;

        case IDCANCEL:
            return FALSE;
        }
    }
    return TRUE;
}

BOOL DoCreateDir(IWorkerObserver& observer, DWORD attr,
                 DWORD clearReadonlyMask, CWorkerState& workerState,
                 CQuadWord& totalDone, CQuadWord& operTotal,
                 BOOL adsCopy, COperations* script,
                 void* buffer, BOOL& skip, BOOL& alreadyExisted,
                 BOOL createAsEncrypted, BOOL ignInvalidName,
                 const std::wstring& nameW,
                 const std::wstring& sourceDirW)
{
    if (script->CopyAttrs && createAsEncrypted)
        TRACE_E("DoCreateDir(): unexpected parameter value: createAsEncrypted is TRUE when script->CopyAttrs is TRUE!");

    skip = FALSE;
    alreadyExisted = FALSE;
    // Wide name for user-facing error reporting: real wide when supplied,
    // else the ANSI mirror widened.
    const std::wstring effectiveCreateNameW = nameW;
    CQuadWord lastTransferredFileSize;
    script->GetTFS(&lastTransferredFileSize);

    std::wstring effectiveNameW = nameW;
    BOOL invalidName = FileNameIsInvalidW(effectiveNameW.c_str(), TRUE, ignInvalidName);

    // if the path ends with a space/dot, we must append '\\'; otherwise SetFileAttributes
    // and RemoveDirectory trim the spaces/dots and operate on a different path
    std::wstring nameCrDirW = MakeCopyWithBackslashIfNeededW(effectiveNameW.c_str());
    std::wstring sourceDirCrDirW;
    if (!sourceDirW.empty())
        sourceDirCrDirW = MakeCopyWithBackslashIfNeededW(sourceDirW.c_str());

    while (1)
    {
        DWORD err;
        BOOL createOk;
        if (!invalidName)
        {
            const FileResult createResult = GetWorkerFileSystem()->CreateDirectory(nameCrDirW.c_str());
            createOk = createResult.success ? TRUE : FALSE;
            if (!createOk)
                err = createResult.errorCode;
        }
        else
            createOk = FALSE;
        if (!invalidName && createOk)
        {
            script->AddBytesToSpeedMetersAndTFSandPS((DWORD)CREATE_DIR_SIZE.Value, TRUE, 0, NULL, MAX_OP_FILESIZE); // directory already created

            DWORD newAttr = attr & clearReadonlyMask;
            if (!sourceDirW.empty() && adsCopy) // copy ADS when required
            {
                CQuadWord operDone = CREATE_DIR_SIZE; // directory already created
                BOOL adsSkip = FALSE;
                if (!DoCopyADS(observer, TRUE, totalDone,
                               operDone, operTotal, workerState, script, &adsSkip, buffer,
                               sourceDirW, nameW) ||
                    adsSkip) // user cancelled or skipped at least one ADS
                {
                    const FileResult removeResult = GetWorkerFileSystem()->RemoveDirectory(nameCrDirW.c_str());
                    if (!removeResult.success)
                    {
                        DWORD err2 = removeResult.errorCode;
                        TRACE_EW(L"Unable to remove newly created directory: " << nameW.c_str() << L", error: " << GetErrorTextOwned(err2).c_str());
                    }
                    if (!adsSkip)
                        return FALSE; // cancel the entire operation (Skip must return TRUE)
                    skip = TRUE;
                    newAttr = -1; // the directory should no longer exist, so do not apply attributes
                }
            }
            if (newAttr != -1)
            {
                DWORD curAttrs = INVALID_FILE_ATTRIBUTES;
                if (script->CopyAttrs || createAsEncrypted) // set Compressed & Encrypted attributes based on the source directory
                {
                    if (createAsEncrypted)
                    {
                        newAttr &= ~FILE_ATTRIBUTE_COMPRESSED;
                        newAttr |= FILE_ATTRIBUTE_ENCRYPTED;
                    }
                    DWORD changeAttrErr = NO_ERROR;
                    DWORD currentAttrs = GetWorkerFileSystem()->GetFileAttributes(nameCrDirW.c_str());
                    if (currentAttrs != INVALID_FILE_ATTRIBUTES)
                    {
                        if ((newAttr & FILE_ATTRIBUTE_COMPRESSED) != (currentAttrs & FILE_ATTRIBUTE_COMPRESSED) &&
                            (newAttr & FILE_ATTRIBUTE_COMPRESSED) == 0)
                        {
                            changeAttrErr = UncompressFileW(nameCrDirW.c_str(), currentAttrs);
                        }
                        if (changeAttrErr == NO_ERROR &&
                            (newAttr & FILE_ATTRIBUTE_ENCRYPTED) != (currentAttrs & FILE_ATTRIBUTE_ENCRYPTED))
                        {
                            BOOL dummyCancelOper = FALSE;
                            if (newAttr & FILE_ATTRIBUTE_ENCRYPTED)
                            {
                                changeAttrErr = MyEncryptFileW(observer, nameCrDirW.c_str(), currentAttrs, 0 /* allow encrypting directories with the SYSTEM attribute */,
                                                                                     workerState, dummyCancelOper, FALSE);

                                if ( //(WindowsVistaAndLater || script->TargetPathSupEFS) &&  // complain regardless of OS version and EFS support; originally directories on FAT could not be encrypted before Vista, we behave the same (to match Explorer, the Encrypted attribute is not that important)
                                    !workerState.DirCrLossEncrAll && changeAttrErr != ERROR_SUCCESS)
                                {                                                              // failed to set the Encrypted attribute on the directory, ask the user what to do
                                    observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                    if (observer.IsCancelled())
                                        goto CANCEL_CRDIR;

                                    int ret;
                                    if (workerState.SkipAllDirCrLossEncr)
                                        ret = IDB_SKIP;
                                    else
                                    {
                                        ret = IDCANCEL;
                                        ret = observer.AskEncryptionLoss(false, nameW.c_str(), !script->IsCopyOperation);
                                    }
                                    switch (ret)
                                    {
                                    case IDB_ALL:
                                        workerState.DirCrLossEncrAll = TRUE; // break intentionally omitted here
                                    case IDYES:
                                        break;

                                    case IDB_SKIPALL:
                                        workerState.SkipAllDirCrLossEncr = TRUE;
                                    case IDB_SKIP:
                                    {
                                        ClearReadOnlyAttr(nameCrDirW.c_str());
                                        GetWorkerFileSystem()->RemoveDirectory(nameCrDirW.c_str());
                                        script->SetTFS(lastTransferredFileSize); // add TFS only after the directory is fully outside; ProgressSize will be synced outside (no point in adjusting it here)
                                        skip = TRUE;
                                        return TRUE;
                                    }

                                    case IDCANCEL:
                                        goto CANCEL_CRDIR;
                                    }
                                }
                            }
                            else
                                changeAttrErr = MyDecryptFileW(nameCrDirW.c_str(), currentAttrs, FALSE);
                        }
                        if (changeAttrErr == NO_ERROR &&
                            (newAttr & FILE_ATTRIBUTE_COMPRESSED) != (currentAttrs & FILE_ATTRIBUTE_COMPRESSED) &&
                            (newAttr & FILE_ATTRIBUTE_COMPRESSED) != 0)
                        {
                            changeAttrErr = CompressFileW(nameCrDirW.c_str(), currentAttrs);
                        }
                    }
                    else
                        changeAttrErr = GetLastError();
                    if (changeAttrErr != NO_ERROR)
                    {
                        TRACE_IW(L"DoCreateDir(): Unable to set Encrypted or Compressed attributes for " << nameW.c_str() << L"! error=" << GetErrorTextOwned(changeAttrErr).c_str());
                    }
                }
                GetWorkerFileSystem()->SetFileAttributes(nameCrDirW.c_str(), newAttr);

                if (script->CopyAttrs) // verify whether the source file attributes were preserved
                {
                    curAttrs = GetWorkerFileSystem()->GetFileAttributes(nameCrDirW.c_str());
                    if (curAttrs == INVALID_FILE_ATTRIBUTES || (curAttrs & DISPLAYED_ATTRIBUTES) != (newAttr & DISPLAYED_ATTRIBUTES))
                    {                                                              // attributes probably did not transfer; warn the user
                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                        if (observer.IsCancelled())
                            goto CANCEL_CRDIR;

                        int ret;
                        ret = IDCANCEL;
                        if (workerState.IgnoreAllSetAttrsErr)
                            ret = IDB_IGNORE;
                        else
                        {
                            ret = observer.AskSetAttrsError(nameW.c_str(), (newAttr & DISPLAYED_ATTRIBUTES), (curAttrs == INVALID_FILE_ATTRIBUTES ? 0 : (curAttrs & DISPLAYED_ATTRIBUTES)));
                        }
                        switch (ret)
                        {
                        case IDB_IGNOREALL:
                            workerState.IgnoreAllSetAttrsErr = TRUE; // break intentionally omitted here
                        case IDB_IGNORE:
                            break;

                        case IDCANCEL:
                        {
                        CANCEL_CRDIR:

                            ClearReadOnlyAttr(nameCrDirW.c_str());
                            GetWorkerFileSystem()->RemoveDirectory(nameCrDirW.c_str());
                            return FALSE;
                        }
                        }
                    }
                }

                if (!sourceDirW.empty() && script->CopySecurity) // should NTFS security permissions be copied?
                {
                    DWORD err2;
                    if (!DoCopySecurity(&err2, NULL, sourceDirW, nameW))
                    {
                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                        if (observer.IsCancelled())
                            goto CANCEL_CRDIR;

                        int ret;
                        ret = IDCANCEL;
                        if (workerState.IgnoreAllCopyPermErr)
                            ret = IDB_IGNORE;
                        else
                        {
                            ret = observer.AskCopyPermError(sourceDirW.c_str(), nameW.c_str(), err2);
                        }
                        switch (ret)
                        {
                        case IDB_IGNOREALL:
                            workerState.IgnoreAllCopyPermErr = TRUE; // break intentionally omitted here
                        case IDB_IGNORE:
                            break;

                        case IDCANCEL:
                            goto CANCEL_CRDIR;
                        }
                    }
                }
            }
            return TRUE;
        }
        else
        {
            if (invalidName)
                err = ERROR_INVALID_NAME;
            if (err == ERROR_ALREADY_EXISTS ||
                err == ERROR_FILE_EXISTS)
            {
                DWORD attr2 = GetWorkerFileSystem()->GetFileAttributes(nameCrDirW.c_str());
                if (attr2 & FILE_ATTRIBUTE_DIRECTORY) // "directory overwrite"
                {
                    if (workerState.CnfrmDirOver && !workerState.DirOverwriteAll) // should we ask the user about overwriting the directory?
                    {
                        wchar_t sAttr[101], tAttr[101];
                        GetDirInfoW(sAttr, sourceDirW.c_str());
                        GetDirInfoW(tAttr, effectiveNameW.c_str());

                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                        if (observer.IsCancelled())
                            return FALSE;

                        if (workerState.SkipAllDirOver)
                            goto SKIP_CREATE_ERROR;

                        int ret = IDCANCEL;
                        ret = observer.AskADSOverwrite(nameW.c_str(), tAttr, sourceDirW.c_str(), sAttr);
                        switch (ret)
                        {
                        case IDB_ALL:
                            workerState.DirOverwriteAll = TRUE;
                        case IDYES:
                            break;

                        case IDB_SKIPALL:
                            workerState.SkipAllDirOver = TRUE;
                        case IDB_SKIP:
                            goto SKIP_CREATE_ERROR;

                        case IDCANCEL:
                            return FALSE;
                        }
                    }
                    alreadyExisted = TRUE;
                    return TRUE; // o.k.
                }

                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                if (observer.IsCancelled())
                    return FALSE;

                if (workerState.SkipAllDirCreate)
                    goto SKIP_CREATE_ERROR;

                int ret = IDCANCEL;
                ret = observer.AskFileErrorByIds(IDS_ERRORCREATINGDIR, effectiveCreateNameW.c_str(), IDS_NAMEALREADYUSED);
                switch (ret)
                {
                case IDRETRY:
                    break;

                case IDB_SKIPALL:
                    workerState.SkipAllDirCreate = TRUE;
                case IDB_SKIP:
                    goto SKIP_CREATE_ERROR;

                case IDCANCEL:
                    return FALSE;
                }
                continue;
            }

            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
                return FALSE;

            if (workerState.SkipAllDirCreateErr)
                goto SKIP_CREATE_ERROR;

            int ret;
            ret = IDCANCEL;
            ret = observer.AskFileErrorById(IDS_ERRORCREATINGDIR, effectiveCreateNameW.c_str(), err);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                workerState.SkipAllDirCreateErr = TRUE;
            case IDB_SKIP:
            {
            SKIP_CREATE_ERROR:

                skip = TRUE; // this is a skip (all operations within the directory must be skipped)
                return TRUE;
            }
            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

BOOL DoDeleteDir(IWorkerObserver& observer, const CQuadWord& size, COperations* script,
                 CQuadWord& totalDone, DWORD attr, BOOL dontUseRecycleBin, CWorkerState& workerState,
                 const std::wstring& nameW)
{
    DWORD err;
    int AutoRetryCounter = 0;
    DWORD startTime = GetTickCount();

    // if the path ends with a space/dot, we must append '\\'; otherwise SetFileAttributes
    // and RemoveDirectory trim the spaces/dots and operate on a different path
    std::wstring nameRmDirW = MakeCopyWithBackslashIfNeededW(
        nameW.c_str());

    // Wide name for user-facing error reporting.
    const std::wstring effectiveDeleteDirW = nameW;

    while (1)
    {
        ClearReadOnlyAttr(nameRmDirW.c_str(), attr); // ensure it can be deleted

        err = ERROR_SUCCESS;
        if (script->CanUseRecycleBin && !dontUseRecycleBin &&
            (script->InvertRecycleBin && workerState.UseRecycleBin == 0 ||
             !script->InvertRecycleBin && workerState.UseRecycleBin == 1) &&
            IsDirectoryEmptyW(nameRmDirW.c_str())) // subdirectory must not contain any files!!!
        {
            // SHFileOperation needs double-null terminated wide path
            std::wstring nameList = nameRmDirW;
            nameList.push_back(L'\0'); // double-null termination
            if (!PathContainsValidComponents(nameList.c_str()))
            {
                err = ERROR_INVALID_NAME;
            }
            else
            {
                // via IShell (see DoDeleteFile note).
                CShellExecuteWnd shellExecuteWnd;
                HWND parentWnd = shellExecuteWnd.Create(observer.GetParentWindow(), L"SEW: DoDeleteDir");
                ShellResult recycleRes = gShell->MoveToRecycleBin({nameRmDirW}, parentWnd);
                err = recycleRes.success ? ERROR_SUCCESS : recycleRes.errorCode;
            }
        }
        else
        {
            const FileResult removeResult = GetWorkerFileSystem()->RemoveDirectory(nameRmDirW.c_str());
            if (!removeResult.success)
                err = removeResult.errorCode;
        }

        if (err == ERROR_SUCCESS)
        {
            script->AddBytesToSpeedMetersAndTFSandPS((DWORD)size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

            totalDone += size;
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
            return TRUE;
        }
        else
        {
            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
                return FALSE;

            if (workerState.SkipAllDeleteErr)
                goto SKIP_DELETE;

            if (AutoRetryCounter < 4 && GetTickCount() - startTime + (AutoRetryCounter + 1) * 100 <= 2000 &&
                (err == ERROR_DIR_NOT_EMPTY || err == ERROR_SHARING_VIOLATION))
            { // add auto-retry to handle this case: I have directories 1\2\3, deleting 1 including subdirectories while 3 is shown in a panel (watching for changes) -> removing 2 reports "directory not empty" because 3 stays in a transitional state due to change notifications (it is deleted, so it cannot be listed, but it still exists on disk briefly; quite a mess)
                //        TRACE_IW(L"DoDeleteDir(): err: " << GetErrorTextOwned(err).c_str());
                AutoRetryCounter++;
                Sleep(AutoRetryCounter * 100);
                //        TRACE_I("DoDeleteDir(): " << AutoRetryCounter << ". retry, delay is " << AutoRetryCounter * 100 << "ms");
            }
            else
            {
                int ret;
                ret = IDCANCEL;
                ret = observer.AskFileErrorById(IDS_ERRORDELETINGDIR, effectiveDeleteDirW.c_str(), err);
                switch (ret)
                {
                case IDRETRY:
                    break;

                case IDB_SKIPALL:
                    workerState.SkipAllDeleteErr = TRUE;
                case IDB_SKIP:
                {
                SKIP_DELETE:

                    totalDone += size;
                    script->SetProgressSize(totalDone);
                    observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                    return TRUE;
                }

                case IDCANCEL:
                    return FALSE;
                }
            }
        }

        DWORD attr2 = GetWorkerFileSystem()->GetFileAttributes(nameRmDirW.c_str()); // get the current attribute state
        if (attr2 != INVALID_FILE_ATTRIBUTES)
            attr = attr2;
    }
}

BOOL DoDeleteDirLinkAuxW(const wchar_t* nameDelLink, DWORD* err)
{
    // remove the reparse point from directory 'nameDelLink' (wide version)
    if (err != NULL)
        *err = ERROR_SUCCESS;
    BOOL ok = FALSE;
    DWORD attr = GetWorkerFileSystem()->GetFileAttributes(nameDelLink);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        const FileResult deleteResult =
            GetWorkerFileSystem()->DeleteDirectoryReparseData(nameDelLink);
        ok = deleteResult.success;
        if (!ok && err != NULL)
            *err = deleteResult.errorCode;
    }
    else
        ok = TRUE; // the reparse point is apparently gone; all that remains is to delete the empty directory...
    // remove the empty directory (that remained after deleting the reparse point)
    if (ok)
        ClearReadOnlyAttr(nameDelLink, attr); // ensure it can be deleted even with the read-only attribute
    FileResult removeResult = ok ? GetWorkerFileSystem()->RemoveDirectory(nameDelLink)
                                 : FileResult::Error(ERROR_SUCCESS);
    if (ok && !removeResult.success)
    {
        ok = FALSE;
        if (err != NULL)
            *err = removeResult.errorCode;
    }
    return ok;
}

BOOL DeleteDirLink(const wchar_t* name, DWORD* err)
{
    // Go through wide path — handles trailing space/dot fixup natively
    std::wstring nameW = MakeCopyWithBackslashIfNeededW(name);
    return DoDeleteDirLinkAuxW(nameW.c_str(), err);
}

BOOL DoDeleteDirLink(IWorkerObserver& observer, const CQuadWord& size, COperations* script,
                     CQuadWord& totalDone, CWorkerState& workerState,
                     const std::wstring& nameW)
{
    // The clone half of a move that the user skipped: the link was never made at the
    // target, so deleting it here would simply lose it. Consume the marker and count
    // the operation as done so the progress bar still reaches the end.
    if (!workerState.SkippedDirLinkSourceW.empty() &&
        CompareStringOrdinal(workerState.SkippedDirLinkSourceW.c_str(), -1, nameW.c_str(), -1,
                             TRUE) == CSTR_EQUAL)
    {
        workerState.SkippedDirLinkSourceW.clear();
        totalDone += size;
        script->SetProgressSize(totalDone);
        observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
        return TRUE;
    }
    workerState.SkippedDirLinkSourceW.clear();

    // if the path ends with a space/dot, we must append '\\'; otherwise CreateFile
    // and RemoveDirectory trim the spaces/dots and operate on a different path
    std::wstring nameDelLinkW = MakeCopyWithBackslashIfNeededW(
        nameW.c_str());

    // Wide name for user-facing error reporting.
    const std::wstring effectiveDeleteDirW = nameW;

    while (1)
    {
        DWORD err;
        BOOL ok = DoDeleteDirLinkAuxW(nameDelLinkW.c_str(), &err);

        if (ok)
        {
            script->AddBytesToSpeedMetersAndTFSandPS((DWORD)size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

            totalDone += size;
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
            return TRUE;
        }
        else
        {
            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
                return FALSE;

            if (workerState.SkipAllDeleteErr)
                goto SKIP_DELETE_LINK;

            int ret;
            ret = IDCANCEL;
            ret = observer.AskFileErrorById(IDS_ERRORDELETINGDIRLINK, effectiveDeleteDirW.c_str(), err);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                workerState.SkipAllDeleteErr = TRUE;
            case IDB_SKIP:
            {
            SKIP_DELETE_LINK:

                totalDone += size;
                script->SetProgressSize(totalDone);
                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

// Copy-the-link: create the target directory and clone the raw
// reparse buffer from the source link. Never opens or enumerates the link
// TARGET - the operation is on the link object itself. Same retry/skip
// protocol as the other link op.
BOOL DoCreateDirLink(IWorkerObserver& observer, COperation* op, COperations* script,
                     CQuadWord& totalDone, CWorkerState& workerState)
{
    // Link ops come only from the snapshot builder, which always sets explicit
    // wide names - no ANSI fallback here (the ratchet holds the line).
    const std::wstring& sourceW = op->SourceNameW;
    const std::wstring& targetW = op->TargetNameW;
    IFileSystem* fs = GetWorkerFileSystem();

    // Start clean: the marker means "the clone that was supposed to precede the NEXT
    // delete did not happen", so a stale one from an earlier link must not survive
    // into this pair.
    workerState.SkippedDirLinkSourceW.clear();

    while (1)
    {
        DWORD err = NO_ERROR;
        BOOL createdTargetDir = FALSE;
        std::vector<BYTE> blob;
        FileResult r = fs->GetReparseData(sourceW.c_str(), blob);
        if (!r.success)
            err = r.errorCode;
        if (err == NO_ERROR)
        {
            r = fs->CreateDirectory(targetW.c_str());
            // Remember whether the directory is ours. If it was already there we did
            // not make it, and must not unmake it below.
            createdTargetDir = r.success;
            if (!r.success && r.errorCode != ERROR_ALREADY_EXISTS)
                err = r.errorCode;
        }
        if (err == NO_ERROR)
        {
            r = fs->SetReparseData(targetW.c_str(), blob.data(), blob.size());
            if (!r.success)
            {
                err = r.errorCode;
                // Clean up only what this attempt created. An ERROR_ALREADY_EXISTS
                // target is a directory the user already had; removing it on our way
                // out of a failed clone would destroy it on their behalf.
                if (createdTargetDir)
                    fs->RemoveDirectory(targetW.c_str()); // do not leave a plain dir behind
            }
        }

        if (err == NO_ERROR)
        {
            script->AddBytesToSpeedMetersAndTFSandPS((DWORD)op->Size.Value, TRUE, 0, NULL,
                                                     MAX_OP_FILESIZE);
            totalDone += op->Size;
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
            return TRUE;
        }

        observer.WaitIfSuspended();
        if (observer.IsCancelled())
            return FALSE;

        // This is a directory-CREATE failure, so it answers to the directory-create
        // Skip All, not the delete one. Sharing SkipAllDeleteErr meant a Skip All
        // given to an unrelated delete error silently suppressed every later link
        // clone - and, on a move, silently deleted those source links.
        if (workerState.SkipAllDirCreateErr)
            goto SKIP_CREATE_LINK;

        switch (observer.AskFileErrorById(IDS_ERRORCREATINGDIR, sourceW.c_str(), err))
        {
        case IDRETRY:
            break;

        case IDB_SKIPALL:
            workerState.SkipAllDirCreateErr = TRUE;
        case IDB_SKIP:
        {
        SKIP_CREATE_LINK:
            // Tell the paired ocDeleteDirLink that this half of a move did not happen.
            workerState.SkippedDirLinkSourceW = sourceW;
            totalDone += op->Size;
            script->SetProgressSize(totalDone);
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
            return TRUE;
        }

        case IDCANCEL:
            return FALSE;
        }
    }
}

// a) create a temporary file in the same directory as file 'name'
// b) transfer the contents of 'name' into the temporary file while applying the
//    conversions specified by convertData.CodeType and convertData.EOFType
// c) overwrite file 'name' with the temporary file
//
// convertData.EOFType  - determines how line endings are replaced
//            CR, LF, and CRLF are all considered end-of-line markers
//            0: leave line endings unchanged
//            1: replace line endings with CRLF (DOS, Windows, OS/2)
//            2: replace line endings with LF (UNIX)
//            3: replace line endings with CR (MAC)
BOOL DoConvert(IWorkerObserver& observer, char* sourceBuffer, char* targetBuffer,
               const CQuadWord& size, COperations* script, CQuadWord& totalDone,
               CConvertData& convertData, CWorkerState& workerState,
               const std::wstring& nameW)
{
    // Always have a wide path available
    std::wstring effectiveNameW = nameW;

    // if the path ends with a space/dot it is invalid and we must not run the conversion,
    // CreateFile would trim the spaces/dots and convert a different file
    BOOL invalidName = FileNameIsInvalidW(effectiveNameW.c_str(), TRUE);

CONVERT_AGAIN:

    CQuadWord operationDone;
    operationDone = CQuadWord(0, 0);
    while (1)
    {
        // attempt to open the source file
        HANDLE hSource;
        if (!invalidName)
        {
            hSource = GetWorkerFileSystem()->CreateFile(effectiveNameW.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                  OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            DWORD openErr = GetLastError();
            HANDLES_ADD_EX(__otQuiet, hSource != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, hSource, openErr, TRUE);
        }
        else
        {
            hSource = INVALID_HANDLE_VALUE;
        }
        if (hSource != INVALID_HANDLE_VALUE)
        {
            // derive the path for the temporary file (using wide path)
            std::wstring tmpPathW;
            size_t lastSlash = effectiveNameW.rfind(L'\\');
            if (lastSlash == std::wstring::npos)
            {
                TRACE_E("Parameter 'name' must be full path to file (including path)");
                (void)CloseWorkerTrackedFile(hSource);
                return FALSE;
            }
            tmpPathW = effectiveNameW.substr(0, lastSlash + 1);


            // find a name for the temporary file and let the system create it
            std::wstring tmpFileNameW;
            BOOL tmpFileExists = FALSE;
            while (1)
            {
                tmpFileNameW = SalGetTempFileNameW(tmpPathW.c_str(), L"cnv", true);
                BOOL tmpOk = !tmpFileNameW.empty();
                if (tmpOk)
                {
                    // keep ANSI buffer in sync for error messages
                }
                if (tmpOk)
                {
                    tmpFileExists = TRUE;

                    // align the temp file attributes with the source file
                    DWORD srcAttrs = GetWorkerFileSystem()->GetFileAttributes(effectiveNameW.c_str());
                    DWORD tgtAttrs = GetWorkerFileSystem()->GetFileAttributes(tmpFileNameW.c_str());
                    BOOL changeAttrs = FALSE;
                    if (srcAttrs != INVALID_FILE_ATTRIBUTES && tgtAttrs != INVALID_FILE_ATTRIBUTES && srcAttrs != tgtAttrs)
                    {
                        changeAttrs = TRUE; // SetFileAttributes will be called later...
                        // does the NTFS compression flag differ?
                        if ((srcAttrs & FILE_ATTRIBUTE_COMPRESSED) != (tgtAttrs & FILE_ATTRIBUTE_COMPRESSED) &&
                            (srcAttrs & FILE_ATTRIBUTE_COMPRESSED) == 0)
                        {
                            UncompressFileW(tmpFileNameW.c_str(), tgtAttrs);
                        }
                        if ((srcAttrs & FILE_ATTRIBUTE_ENCRYPTED) != (tgtAttrs & FILE_ATTRIBUTE_ENCRYPTED))
                        {
                            BOOL cancelOper = FALSE;
                            if (srcAttrs & FILE_ATTRIBUTE_ENCRYPTED)
                                MyEncryptFileW(observer, tmpFileNameW.c_str(), tgtAttrs, 0, workerState, cancelOper, FALSE);
                            else
                                MyDecryptFileW(tmpFileNameW.c_str(), tgtAttrs, FALSE);
                            if (observer.IsCancelled() || cancelOper)
                            {
                                (void)CloseWorkerTrackedFile(hSource);
                                ClearReadOnlyAttr(tmpFileNameW.c_str());
                                gFileSystem->DeleteFile(tmpFileNameW.c_str());
                                return FALSE;
                            }
                        }
                        if ((srcAttrs & FILE_ATTRIBUTE_COMPRESSED) != (tgtAttrs & FILE_ATTRIBUTE_COMPRESSED) &&
                            (srcAttrs & FILE_ATTRIBUTE_COMPRESSED) != 0)
                        {
                            CompressFileW(tmpFileNameW.c_str(), tgtAttrs);
                        }
                    }

                    // open the empty temporary file
                    HANDLE hTarget = GetWorkerFileSystem()->CreateFile(tmpFileNameW.c_str(), GENERIC_WRITE, 0, NULL,
                                                 OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                    {
                        DWORD openErr2 = GetLastError();
                        HANDLES_ADD_EX(__otQuiet, hTarget != INVALID_HANDLE_VALUE, __htFile, __hoCreateFile, hTarget, openErr2, TRUE);
                    }
                    if (hTarget != INVALID_HANDLE_VALUE)
                    {
                        DWORD read;
                        BOOL crlfBreak = FALSE;
                        FileResult readIoResult = FileResult::Ok();
                        DWORD pendingWriteError = ERROR_SUCCESS;
                        while (1)
                        {
                            readIoResult = GetWorkerFileSystem()->ReadFromHandle(
                                hSource, sourceBuffer, OPERATION_BUFFER, &read);
                            if (readIoResult.success)
                            {
                                DWORD written;
                                if (read == 0)
                                    break;                                                 // EOF
                                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                if (observer.IsCancelled())
                                {
                                CONVERT_ERROR:

                                    if (hSource != NULL)
                                        (void)GetWorkerFileSystem()->CloseFileHandle(hSource);
                                    if (hTarget != NULL)
                                        (void)GetWorkerFileSystem()->CloseFileHandle(hTarget);
                                    ClearReadOnlyAttr(tmpFileNameW.c_str());
                                    gFileSystem->DeleteFile(tmpFileNameW.c_str());
                                    return FALSE;
                                }

                                // translate sourceBuffer -> targetBuffer
                                char* sourceIterator;
                                char* targetIterator;
                                sourceIterator = sourceBuffer;
                                targetIterator = targetBuffer;
                                while (sourceIterator - sourceBuffer < (int)read)
                                {
                                    // lastChar is TRUE when sourceIterator points to the final character in the buffer
                                    BOOL lastChar = (sourceIterator - sourceBuffer == (int)read - 1);

                                    if (convertData.EOFType != 0)
                                    {
                                        if (crlfBreak && sourceIterator == sourceBuffer && *sourceIterator == '\n')
                                        {
                                            // we already processed this CRLF, leave the LF as is now
                                            crlfBreak = FALSE;
                                        }
                                        else
                                        {
                                            if (*sourceIterator == '\r' || *sourceIterator == '\n')
                                            {
                                                switch (convertData.EOFType)
                                                {
                                                case 2:
                                                    *targetIterator++ = convertData.CodeTable['\n'];
                                                    break;
                                                case 3:
                                                    *targetIterator++ = convertData.CodeTable['\r'];
                                                    break;
                                                default:
                                                {
                                                    *targetIterator++ = convertData.CodeTable['\r'];
                                                    *targetIterator++ = convertData.CodeTable['\n'];
                                                    break;
                                                }
                                                }
                                                // capture CRLF which splits across the buffer boundary
                                                if (lastChar && *sourceIterator == '\r')
                                                    crlfBreak = TRUE;
                                                // capture CRLF that is contiguous – skip the LF
                                                if (!lastChar &&
                                                    *sourceIterator == '\r' && *(sourceIterator + 1) == '\n')
                                                    sourceIterator++;
                                            }
                                            else
                                            {
                                                *targetIterator = convertData.CodeTable[static_cast<BYTE>(*sourceIterator)];
                                                targetIterator++;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        *targetIterator = convertData.CodeTable[static_cast<BYTE>(*sourceIterator)];
                                        targetIterator++;
                                    }
                                    sourceIterator++;
                                }

                                // write the data to the temp file
                                while (1)
                                {
                                    const FileResult writeIoResult =
                                        GetWorkerFileSystem()->WriteToHandle(
                                            hTarget, targetBuffer,
                                            (DWORD)(targetIterator - targetBuffer), &written);
                                    if (writeIoResult.success &&
                                        targetIterator - targetBuffer == (int)written)
                                        break;
                                    pendingWriteError = writeIoResult.success ?
                                                            ERROR_DISK_FULL : writeIoResult.errorCode;

                                WRITE_ERROR_CONVERT:

                                    DWORD err = pendingWriteError != ERROR_SUCCESS ?
                                                    pendingWriteError : GetLastError();
                                    pendingWriteError = ERROR_SUCCESS;

                                    observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                    if (observer.IsCancelled())
                                        goto CONVERT_ERROR;

                                    if (workerState.SkipAllFileWrite)
                                        goto SKIP_CONVERT;

                                    int ret;
                                    ret = IDCANCEL;
                                    if (hTarget != NULL && err == NO_ERROR && targetIterator - targetBuffer != (int)written)
                                        err = ERROR_DISK_FULL;
                                    ret = observer.AskFileErrorById(IDS_ERRORWRITINGFILE, tmpFileNameW.c_str(), err);
                                    switch (ret)
                                    {
                                    case IDRETRY:
                                    {
                                        if (hSource == NULL && hTarget == NULL)
                                        {
                                            ClearReadOnlyAttr(tmpFileNameW.c_str());
                                            gFileSystem->DeleteFile(tmpFileNameW.c_str());
                                            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                                            goto CONVERT_AGAIN;
                                        }
                                        break;
                                    }

                                    case IDB_SKIPALL:
                                        workerState.SkipAllFileWrite = TRUE;
                                    case IDB_SKIP:
                                    {
                                    SKIP_CONVERT:

                                        totalDone += size;
                                        if (hSource != NULL)
                                            (void)GetWorkerFileSystem()->CloseFileHandle(hSource);
                                        if (hTarget != NULL)
                                            (void)GetWorkerFileSystem()->CloseFileHandle(hTarget);
                                        ClearReadOnlyAttr(tmpFileNameW.c_str());
                                        gFileSystem->DeleteFile(tmpFileNameW.c_str());
                                        observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                                        return TRUE;
                                    }

                                    case IDCANCEL:
                                        goto CONVERT_ERROR;
                                    }
                                }
                                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                if (observer.IsCancelled())
                                    goto CONVERT_ERROR;

                                operationDone += CQuadWord(read, 0);
                                observer.SetProgress(
                                            CaclProg(operationDone, size),
                                            CaclProg(totalDone + operationDone, script->TotalSize));
                            }
                            else
                            {
                                DWORD err = readIoResult.errorCode;
                                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                if (observer.IsCancelled())
                                    goto CONVERT_ERROR;

                                if (workerState.SkipAllFileRead)
                                    goto SKIP_CONVERT;

                                int ret = IDCANCEL;
                                ret = observer.AskFileErrorById(IDS_ERRORREADINGFILE, effectiveNameW.c_str(), err);
                                switch (ret)
                                {
                                case IDRETRY:
                                    break;
                                case IDB_SKIPALL:
                                    workerState.SkipAllFileRead = TRUE;
                                case IDB_SKIP:
                                    goto SKIP_CONVERT;
                                case IDCANCEL:
                                    goto CONVERT_ERROR;
                                }
                            }
                        }
                        // close the files and update the global progress
                        // do not reuse operationDone so the progress stays correct even if the file changes "under our feet"
                        (void)GetWorkerFileSystem()->CloseFileHandle(hSource);
                        const FileResult closeTargetResult =
                            GetWorkerFileSystem()->CloseFileHandle(hTarget);
                        if (!closeTargetResult.success) // even after a failed call we assume the handle is closed,
                        {                                   // see https://forum.altap.cz/viewtopic.php?f=6&t=8455
                            pendingWriteError = closeTargetResult.errorCode;
                            hSource = hTarget = NULL;       // (it states that the target file can be deleted, so the handle was not left open)
                            goto WRITE_ERROR_CONVERT;
                        }
                        totalDone += size;
                        // restore attributes (write operations have trouble with read-only)
                        if (changeAttrs)
                            GetWorkerFileSystem()->SetFileAttributes(tmpFileNameW.c_str(), srcAttrs);
                        // overwrite the original file with the temp file
                        while (1)
                        {
                            ClearReadOnlyAttr(effectiveNameW.c_str());
                            BOOL deleteOk = gFileSystem->DeleteFile(effectiveNameW.c_str()).success;
                            if (deleteOk)
                            {
                                while (1)
                                {
                                    const FileResult moveResult = GetWorkerFileSystem()->MoveFile(
                                        tmpFileNameW.c_str(), effectiveNameW.c_str());
                                    if (moveResult.success)
                                        return TRUE; // success
                                    else
                                    {
                                        DWORD err = moveResult.errorCode;

                                        observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                        if (observer.IsCancelled())
                                            return FALSE;

                                        if (workerState.SkipAllMoveErrors)
                                            return TRUE;

                                        int ret = IDCANCEL;
                                        ret = observer.AskCannotMoveErr(tmpFileNameW.c_str(), nameW.c_str(), err, false);
                                        switch (ret)
                                        {
                                        case IDRETRY:
                                            break;

                                        case IDB_SKIPALL:
                                            workerState.SkipAllMoveErrors = TRUE;
                                        case IDB_SKIP:
                                            return TRUE;

                                        case IDCANCEL:
                                            return FALSE;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                DWORD err = GetLastError();

                                observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                                if (observer.IsCancelled())
                                {
                                CANCEL_CONVERT:

                                    ClearReadOnlyAttr(tmpFileNameW.c_str());
                                    gFileSystem->DeleteFile(tmpFileNameW.c_str());
                                    return FALSE;
                                }

                                if (workerState.SkipAllOverwriteErr)
                                    goto SKIP_OVERWRITE_ERROR;

                                int ret;
                                ret = IDCANCEL;
                                ret = observer.AskFileErrorById(IDS_ERROROVERWRITINGFILE, effectiveNameW.c_str(), err);
                                switch (ret)
                                {
                                case IDRETRY:
                                    break;

                                case IDB_SKIPALL:
                                    workerState.SkipAllOverwriteErr = TRUE;
                                case IDB_SKIP:
                                {
                                SKIP_OVERWRITE_ERROR:

                                    ClearReadOnlyAttr(tmpFileNameW.c_str());
                                    gFileSystem->DeleteFile(tmpFileNameW.c_str());
                                    return TRUE;
                                }

                                case IDCANCEL:
                                    goto CANCEL_CONVERT;
                                }
                            }
                        }
                    }
                    else
                        goto TMP_OPEN_ERROR;
                }
                else
                {
                TMP_OPEN_ERROR:

                    DWORD err = GetLastError();

                    std::wstring fakeNameW; // name of the temp file that cannot be created/opened
                    if (tmpFileExists)
                    {
                        fakeNameW = tmpFileNameW;
                        ClearReadOnlyAttr(tmpFileNameW.c_str());
                        gFileSystem->DeleteFile(tmpFileNameW.c_str());
                        tmpFileExists = FALSE;
                    }
                    else
                    {
                        // assemble a fictitious temp-file name for the failed creation attempt
                        fakeNameW = tmpPathW;
                        if (!fakeNameW.empty() && fakeNameW.back() == L'\\')
                            fakeNameW.pop_back();
                        fakeNameW += L"\\cnv0000.tmp";
                    }

                    observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
                    if (observer.IsCancelled())
                        goto CANCEL_OPEN2;

                    if (workerState.SkipAllFileOpenOut)
                        goto SKIP_OPEN_OUT;

                    int ret;
                    ret = IDCANCEL;
                    ret = observer.AskFileErrorById(IDS_ERRORCREATINGTMPFILE, fakeNameW.c_str(), err);
                    switch (ret)
                    {
                    case IDRETRY:
                        break;

                    case IDB_SKIPALL:
                        workerState.SkipAllFileOpenOut = TRUE;
                    case IDB_SKIP:
                    {
                    SKIP_OPEN_OUT:

                        (void)CloseWorkerTrackedFile(hSource);
                        totalDone += size;
                        observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                        return TRUE;
                    }

                    case IDCANCEL:
                    {
                    CANCEL_OPEN2:

                        (void)CloseWorkerTrackedFile(hSource);
                        return FALSE;
                    }
                    }
                }
            }
        }
        else
        {
            DWORD err = GetLastError();
            if (invalidName)
                err = ERROR_INVALID_NAME;
            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
                return FALSE;

            if (workerState.SkipAllFileOpenIn)
                goto SKIP_OPEN_IN;

            int ret;
            ret = IDCANCEL;
            ret = observer.AskFileErrorById(IDS_ERROROPENINGFILE, effectiveNameW.c_str(), err);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                workerState.SkipAllFileOpenIn = TRUE;
            case IDB_SKIP:
            {
            SKIP_OPEN_IN:

                totalDone += size;
                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

BOOL DoChangeAttrs(IWorkerObserver& observer, const CQuadWord& size, DWORD attrs,
                   COperations* script, CQuadWord& totalDone,
                   FILETIME* timeModified, FILETIME* timeCreated, FILETIME* timeAccessed,
                   BOOL& changeCompression, BOOL& changeEncryption, DWORD fileAttr,
                   CWorkerState& workerState,
                   const std::wstring& nameW)
{
    // if the path ends with a space/dot, we must append '\\'; otherwise
    // SetFileAttributes (and others) trims the spaces/dots and operates
    // on a different path
    std::wstring nameSetAttrsW = MakeCopyWithBackslashIfNeededW(
        nameW.c_str());
    // Wide name for user-facing error reporting.
    const std::wstring effectiveAttrsNameW = nameW;
    // Compute wide path for compress/encrypt/decrypt operations (use original nameW,
    // not the backslash-fixed version, since those functions do their own fixup)
    std::wstring effectiveNameW = nameW;

    while (1)
    {
        DWORD error = ERROR_SUCCESS;
        BOOL showCompressErr = FALSE;
        BOOL showEncryptErr = FALSE;
        int errTitleId = 0;
        if (changeCompression && (attrs & FILE_ATTRIBUTE_COMPRESSED) == 0)
        {
            error = UncompressFileW(effectiveNameW.c_str(), fileAttr);
            if (error != ERROR_SUCCESS)
            {
                errTitleId = IDS_ERRORCOMPRESSING;
                if (error == ERROR_INVALID_FUNCTION)
                    showCompressErr = TRUE; // not supported
            }
        }
        if (error == ERROR_SUCCESS && changeEncryption && (attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0)
        {
            error = MyDecryptFileW(effectiveNameW.c_str(), fileAttr, TRUE);
            if (error != ERROR_SUCCESS)
            {
                errTitleId = IDS_ERRORENCRYPTING;
                if (error == ERROR_INVALID_FUNCTION)
                    showEncryptErr = TRUE; // not supported
            }
        }
        if (error == ERROR_SUCCESS && changeCompression && (attrs & FILE_ATTRIBUTE_COMPRESSED))
        {
            error = CompressFileW(effectiveNameW.c_str(), fileAttr);
            if (error != ERROR_SUCCESS)
            {
                errTitleId = IDS_ERRORCOMPRESSING;
                if (error == ERROR_INVALID_FUNCTION)
                    showCompressErr = TRUE; // not supported
            }
        }
        if (error == ERROR_SUCCESS && changeEncryption && (attrs & FILE_ATTRIBUTE_ENCRYPTED))
        {
            BOOL cancelOper = FALSE;
            error = MyEncryptFileW(observer, effectiveNameW.c_str(), fileAttr, attrs, workerState, cancelOper, TRUE);
            if (observer.IsCancelled() || cancelOper)
                return FALSE;
            if (error != ERROR_SUCCESS)
            {
                errTitleId = IDS_ERRORENCRYPTING;
                if (error == ERROR_INVALID_FUNCTION)
                    showEncryptErr = TRUE; // not supported
            }
        }
        if (showCompressErr || showEncryptErr)
        {
            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
                return FALSE;

            if (showCompressErr)
                changeCompression = FALSE;
            if (showEncryptErr)
                changeEncryption = FALSE;
            int notifyTitleId = (showCompressErr && (attrs & FILE_ATTRIBUTE_COMPRESSED) || !showEncryptErr) ? IDS_ERRORCOMPRESSING : IDS_ERRORENCRYPTING;
            int notifyDetailId = (showCompressErr && (attrs & FILE_ATTRIBUTE_COMPRESSED) || !showEncryptErr) ? IDS_COMPRNOTSUPPORTED : IDS_ENCRYPNOTSUPPORTED;
            observer.NotifyErrorById(notifyTitleId, nameW.c_str(), notifyDetailId);
            error = ERROR_SUCCESS;
        }
        FileResult setAttrsResult = FileResult::Error(error);
        if (error == ERROR_SUCCESS)
            setAttrsResult = GetWorkerFileSystem()->SetFileAttributes(nameSetAttrsW.c_str(), attrs);
        if (error == ERROR_SUCCESS && setAttrsResult.success)
        {
            BOOL isDir = ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
            // if any of the timestamps need to be set
            if (timeModified != NULL || timeCreated != NULL || timeAccessed != NULL)
            {
                HANDLE file;
                if (attrs & FILE_ATTRIBUTE_READONLY)
                    GetWorkerFileSystem()->SetFileAttributes(nameSetAttrsW.c_str(), attrs & (~FILE_ATTRIBUTE_READONLY));
                file = GetWorkerFileSystem()->CreateFile(nameSetAttrsW.c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, isDir ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL);
                if (file != INVALID_HANDLE_VALUE)
                {
                    FILETIME ftCreated, ftAccessed, ftModified;
                    (void)GetWorkerFileSystem()->GetHandleFileTime(
                        file, &ftCreated, &ftAccessed, &ftModified);
                    if (timeCreated != NULL)
                        ftCreated = *timeCreated;
                    if (timeAccessed != NULL)
                        ftAccessed = *timeAccessed;
                    if (timeModified != NULL)
                        ftModified = *timeModified;
                    (void)GetWorkerFileSystem()->SetHandleFileTime(
                        file, &ftCreated, &ftAccessed, &ftModified);
                    (void)CloseWorkerTrackedFile(file);
                    if (attrs & FILE_ATTRIBUTE_READONLY)
                        GetWorkerFileSystem()->SetFileAttributes(nameSetAttrsW.c_str(), attrs);
                }
                else
                {
                    error = GetLastError();
                    if (attrs & FILE_ATTRIBUTE_READONLY)
                        GetWorkerFileSystem()->SetFileAttributes(nameSetAttrsW.c_str(), attrs);
                    goto SHOW_ERROR;
                }
            }
            totalDone += size;
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
            return TRUE;
        }
        else
        {
        SHOW_ERROR:

            if (error == ERROR_SUCCESS)
                error = setAttrsResult.errorCode;
            if (errTitleId == 0)
                errTitleId = IDS_ERRORCHANGINGATTRS;

            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
            if (observer.IsCancelled())
                return FALSE;

            if (workerState.SkipAllChangeAttrs)
                goto SKIP_ATTRS_ERROR;

            int ret;
            ret = IDCANCEL;
            ret = observer.AskFileErrorById(errTitleId, effectiveAttrsNameW.c_str(), error);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                workerState.SkipAllChangeAttrs = TRUE;
            case IDB_SKIP:
            {
            SKIP_ATTRS_ERROR:

                totalDone += size;
                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

unsigned ThreadWorkerBody(void* parameter)
{
    CALL_STACK_MESSAGE1("ThreadWorkerBody()");
    SetThreadNameInVCAndTrace(L"Worker");
    TRACE_I("Begin");

    CWorkerData* data = (CWorkerData*)parameter;
    //--- create a local copy of the data
    HANDLE wContinue = data->WContinue;
    CWorkerState workerState;
    workerState.Init();
    COperations* script = data->Script;
    if (script->TotalSize == CQuadWord(0, 0))
    {
        script->TotalSize = CQuadWord(1, 0); // guard against division by zero
                                             // TRACE_E("ThreadWorkerBody(): script->TotalSize may not be zero!");  // when building the script we do not set the "synchronizing one", which caused issues in Calculate Occupied Space
    }

    if (script->CopySecurity)
        GainWriteOwnerAccess();

    CDialogWorkerObserver dialogObserver(data->HProgressDlg, data->WorkerNotSuspended,
                                         data->CancelWorker, data->OperationProgress,
                                         data->SummaryProgress);
    IWorkerObserver& observer = dialogObserver;
    void* buffer = data->Buffer;
    BOOL bufferIsAllocated = data->BufferIsAllocated;
    CChangeAttrsData* attrsData = (CChangeAttrsData*)data->Buffer;
    DWORD clearReadonlyMask = data->ClearReadonlyMask;
    CConvertData convertData;
    if (data->ConvertData != NULL) // make a copy of the data for Convert
    {
        convertData = *data->ConvertData;
    }
    SetEvent(wContinue); // data ready; resume the main thread or the progress-dialog thread
                         //---
    observer.SetProgress(0, 0);
    script->InitSpeedMeters(FALSE);

    std::wstring lastLantasticCheckRoot; // last path root checked for Lantastic ("" = nothing checked yet)
    BOOL lastIsLantasticPath = FALSE;                                                                                  // result of checking root lastLantasticCheckRoot
    int mustDeleteFileBeforeOverwrite = 0; /* need test */                                                             // (added for SNAP server - NSA drive - SetEndOfFile fails - 0/1/2 = need-test/yes/no
    int allocWholeFileOnStart = 0; /* need test */                                                                     // safety measure (e.g. SNAP servers - NSA drives - may fail); cannot risk a broken Copy - 0/1/2 = need-test/yes/no
    int setDirTimeAfterMove = script->PreserveDirTime && script->SourcePathIsNetwork ? 0 /* need test */ : 2 /* no */; // e.g. on Samba, moving/renaming a directory changes its date and time - 0/1/2 = need-test/yes/no

    BOOL Error = FALSE;
    CQuadWord totalDone;
    totalDone = CQuadWord(0, 0);
    CProgressData pd;
    BOOL novellRenamePatch = FALSE; // TRUE when the read-only attribute must be cleared before MoveFile (required on Novell)
    char* tgtBuffer = NULL;         // conversion buffer for ocConvert
    CAsyncCopyParams* asyncPar = NULL;
    if (buffer != NULL)
    {
        // operation label strings are loaded in workerState.Init()

        TRACE_I("Worker: script->Count=" << script->Count << ", IsCancelled=" << observer.IsCancelled());
        int i;
        for (i = 0; !observer.IsCancelled() && i < script->Count; i++)
        {
            COperation* op = &script->At(i);

            switch (op->Opcode)
            {
            case ocCopyFile:
            {
                pd.Operation = workerState.OpStrCopying;
                pd.Source = op->SourceNameW.c_str();
                pd.Preposition = workerState.OpStrCopyingPrep;
                pd.Target = op->TargetNameW.c_str();
                observer.SetOperationInfo(&pd);

                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

                BOOL lantasticCheck = IsLantasticDriveW(op->TargetNameW.c_str(), lastLantasticCheckRoot, lastIsLantasticPath);

                Error = !DoCopyFile(op, observer, buffer, script, totalDone,
                                    clearReadonlyMask, NULL, lantasticCheck, mustDeleteFileBeforeOverwrite,
                                    allocWholeFileOnStart, workerState,
                                    (op->OpFlags & OPFL_COPY_ADS) != 0,
                                    (op->OpFlags & OPFL_AS_ENCRYPTED) != 0,
                                    FALSE, asyncPar);
                break;
            }

            case ocMoveDir:
            case ocMoveFile:
            {
                pd.Operation = workerState.OpStrMoving;
                pd.Source = op->SourceNameW.c_str();
                pd.Preposition = workerState.OpStrMovingPrep;
                pd.Target = op->TargetNameW.c_str();
                observer.SetOperationInfo(&pd);

                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

                BOOL lantasticCheck = IsLantasticDriveW(op->TargetNameW.c_str(), lastLantasticCheckRoot, lastIsLantasticPath);
                BOOL ignInvalidName = op->Opcode == ocMoveDir && (op->OpFlags & OPFL_IGNORE_INVALID_NAME) != 0;

                Error = !DoMoveFile(op, observer, buffer, script, totalDone,
                                    op->Opcode == ocMoveDir, clearReadonlyMask, &novellRenamePatch,
                                    lantasticCheck, mustDeleteFileBeforeOverwrite,
                                    allocWholeFileOnStart, workerState,
                                    (op->OpFlags & OPFL_COPY_ADS) != 0,
                                    (op->OpFlags & OPFL_AS_ENCRYPTED) != 0,
                                    &setDirTimeAfterMove, asyncPar, ignInvalidName);
                break;
            }

            case ocCreateDir:
            {
                BOOL copyADS = (op->OpFlags & OPFL_COPY_ADS) != 0;
                BOOL crAsEncrypted = (op->OpFlags & OPFL_AS_ENCRYPTED) != 0;
                BOOL ignInvalidName = (op->OpFlags & OPFL_IGNORE_INVALID_NAME) != 0;
                pd.Operation = workerState.OpStrCreatingDir;
                pd.Source = op->TargetNameW.c_str();
                pd.Preposition = L"";
                pd.Target = L"";
                observer.SetOperationInfo(&pd);

                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

                BOOL skip, alreadyExisted;
                Error = !DoCreateDir(observer, op->Attr, clearReadonlyMask, workerState,
                                     totalDone, op->Size, copyADS, script, buffer, skip,
                                     alreadyExisted, crAsEncrypted, ignInvalidName,
                                     op->TargetNameW,
                                 op->SourceNameW);
                if (!Error)
                {
                    if (skip) // skip directory creation
                    {
                        // skip all script operations up to the label that closes this directory
                        CQuadWord skipTotal(0, 0);
                        int createDirIndex = i;
                        while (++i < script->Count)
                        {
                            COperation* oper = &script->At(i);
                            if (oper->Opcode == ocLabelForSkipOfCreateDir && (int)oper->Attr == createDirIndex)
                            {
                                script->AddBytesToTFS(oper->SkippedDirSize);
                                break;
                            }
                            skipTotal += oper->Size;
                        }
                        if (i == script->Count)
                        {
                            i = createDirIndex;
                            TRACE_E("ThreadWorkerBody(): unable to find end-label for dir-create operation: opcode=" << op->Opcode << ", index=" << i);
                        }
                        else
                            totalDone += skipTotal;
                    }
                    else
                    {
                        if (alreadyExisted)
                            op->Attr = 0x10000000 /* dir already existed */;
                        else
                            op->Attr = 0x01000000 /* dir was created */;
                    }
                    totalDone += op->Size;
                    script->SetProgressSize(totalDone);
                    observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                }
                break;
            }

            case ocCopyDirTime:
            {
                BOOL skipSetDirTime = FALSE;
                // locate the skip-label; it stores the index of the create-dir operation along with
                // whether the target directory already existed or was created (date/time are copied
                // only when we created the directory)
                COperation* skipLabel = NULL;
                if (i + 1 < script->Count && script->At(i + 1).Opcode == ocLabelForSkipOfCreateDir)
                    skipLabel = &script->At(i + 1);
                else
                {
                    if (i + 2 < script->Count && script->At(i + 2).Opcode == ocLabelForSkipOfCreateDir)
                        skipLabel = &script->At(i + 2);
                }
                if (skipLabel != NULL)
                {
                    if (skipLabel->Attr < (DWORD)script->Count)
                    {
                        COperation* crDir = &script->At(skipLabel->Attr);
                        if (crDir->Opcode == ocCreateDir && (crDir->OpFlags & OPFL_AS_ENCRYPTED) == 0)
                        {
                            if (crDir->Attr == 0x10000000 /* dir already existed */)
                                skipSetDirTime = TRUE;
                            else
                            {
                                if (crDir->Attr != 0x01000000 /* dir was created */)
                                    TRACE_E("ThreadWorkerBody(): unexpected value of Attr in create-dir operation (not 'existed' nor 'created')!");
                            }
                        }
                        else
                            TRACE_E("ThreadWorkerBody(): unexpected opcode or flags of create-dir operation! Opcode=" << crDir->Opcode << ", OpFlags=" << crDir->OpFlags);
                    }
                    else
                        TRACE_E("ThreadWorkerBody(): unexpected index of create-dir operation! index=" << skipLabel->Attr);
                }
                else
                    TRACE_E("ThreadWorkerBody(): unable to find end-label for dir-create operation (not in first following item nor in second following item)!");

                if (!skipSetDirTime)
                {
                    pd.Operation = workerState.OpStrChangingAttrs;
                    pd.Source = op->TargetNameW.c_str();
                    pd.Preposition = L"";
                    pd.Target = L"";
                    observer.SetOperationInfo(&pd);

                    observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

                    FILETIME modified = op->DirTime;
                    Error = !DoCopyDirTime(observer, &modified, workerState, FALSE,
                                           op->TargetNameW);
                }
                if (!Error)
                {
                    script->AddBytesToSpeedMetersAndTFSandPS((DWORD)op->Size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

                    totalDone += op->Size;
                    observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                }
                break;
            }

            case ocDeleteFile:
            case ocDeleteDir:
            case ocDeleteDirLink:
            {
                TRACE_IW(L"Worker: delete op=" << op->Opcode << L" src=" << (op->SourceNameW.empty() ? L"(null)" : op->SourceNameW.c_str()));
                pd.Operation = workerState.OpStrDeleting;
                pd.Source = op->SourceNameW.c_str();
                pd.Preposition = L"";
                pd.Target = L"";
                observer.SetOperationInfo(&pd);

                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

                if (op->Opcode == ocDeleteFile)
                {
                    Error = !DoDeleteFile(observer, op->Size,
                                          script, totalDone, op->Attr, workerState,
                                          op->SourceNameW);
                }
                else
                {
                    if (op->Opcode == ocDeleteDir)
                    {
                        Error = !DoDeleteDir(observer, op->Size,
                                             script, totalDone, op->Attr, !op->DeleteDirToRecycleBin,
                                             workerState,
                                             op->SourceNameW);
                    }
                    else
                    {
                        Error = !DoDeleteDirLink(observer, op->Size,
                                                 script, totalDone, workerState,
                                                 op->SourceNameW);
                    }
                }
                break;
            }

            case ocCreateDirLink:
            {
                pd.Operation = workerState.OpStrCopying;
                pd.Source = op->SourceNameW.c_str();
                pd.Preposition = workerState.OpStrCopyingPrep;
                pd.Target = op->TargetNameW.c_str();
                observer.SetOperationInfo(&pd);
                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                Error = !DoCreateDirLink(observer, op, script, totalDone, workerState);
                break;
            }

            case ocConvert:
            {
                // output buffer - the conversion will be performed in it (in the worst case,
                // when the input file contains only CR or LF and we translate them to CRLF,
                // this buffer is twice the size of sourceBuffer) and afterwards we will write from it
                // to the temporary file
                if (tgtBuffer == NULL) // first pass?
                {
                    tgtBuffer = (char*)malloc(OPERATION_BUFFER * 2);
                    if (tgtBuffer == NULL)
                    {
                        TRACE_E(LOW_MEMORY);
                        Error = TRUE;
                        break; // error ...
                    }
                }
                pd.Operation = workerState.OpStrConverting;
                pd.Source = op->SourceNameW.c_str();
                pd.Preposition = L"";
                pd.Target = L"";
                observer.SetOperationInfo(&pd);

                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

                Error = !DoConvert(observer, (char*)buffer, tgtBuffer, op->Size, script,
                                   totalDone, convertData, workerState,
                                   op->SourceNameW);
                break;
            }

            case ocChangeAttrs:
            {
                pd.Operation = workerState.OpStrChangingAttrs;
                pd.Source = op->SourceNameW.c_str();
                pd.Preposition = L"";
                pd.Target = L"";
                observer.SetOperationInfo(&pd);

                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

                Error = !DoChangeAttrs(observer, op->Size, op->NewAttrs,
                                       script, totalDone,
                                       attrsData->ChangeTimeModified ? &attrsData->TimeModified : NULL,
                                       attrsData->ChangeTimeCreated ? &attrsData->TimeCreated : NULL,
                                       attrsData->ChangeTimeAccessed ? &attrsData->TimeAccessed : NULL,
                                       attrsData->ChangeCompression, attrsData->ChangeEncryption,
                                       op->Attr, workerState,
                                       op->SourceNameW);
                break;
            }

            case ocLabelForSkipOfCreateDir:
                break; // no action
            }
            if (Error)
                break;
            observer.WaitIfSuspended(); // if we should be in suspend mode, wait ...
        }
        if (!Error && !observer.IsCancelled() && i == script->Count && totalDone != script->TotalSize &&
            (totalDone != CQuadWord(0, 0) || script->TotalSize != CQuadWord(1, 0))) // intentional change of script->TotalSize to one (prevents division by zero)
        {
            TRACE_E("ThreadWorkerBody(): operation done: totalDone != script->TotalSize (" << totalDone.Value << " != " << script->TotalSize.Value << ")");
        }
        CQuadWord transferredFileSize, progressSize;
        if (!Error && !observer.IsCancelled() && i == script->Count &&
            script->GetTFSandProgressSize(&transferredFileSize, &progressSize) &&
            (transferredFileSize != script->TotalFileSize ||
             progressSize != script->TotalSize &&
                 (progressSize != CQuadWord(0, 0) || script->TotalSize != CQuadWord(1, 0)))) // intentional change of script->TotalSize to one (prevents division by zero)
        {
            if (transferredFileSize != script->TotalFileSize)
            {
                TRACE_E("ThreadWorkerBody(): operation done: transferredFileSize != script->TotalFileSize (" << transferredFileSize.Value << " != " << script->TotalFileSize.Value << ")");
            }
            if (progressSize != script->TotalSize &&
                (progressSize != CQuadWord(0, 0) || script->TotalSize != CQuadWord(1, 0)))
            {
                TRACE_E("ThreadWorkerBody(): operation done: progressSize != script->TotalSize (" << progressSize.Value << " != " << script->TotalSize.Value << ")");
            }
        }
    }
    if (asyncPar != NULL)
        delete asyncPar;
    if (tgtBuffer != NULL)
        free(tgtBuffer);
    if (bufferIsAllocated)
        free(buffer);
    observer.SetError(Error != FALSE);  // if this was triggered by Cancel, make that obvious ...
    observer.NotifyDone();              // we're done ...
    WaitForSingleObject(wContinue, INFINITE);       // we need to stop the main thread

    FreeScript(script); // calls delete, so the main thread cannot be running

    TRACE_I("End");
    return 0;
}

BOOL RunWorkerDirect(COperations* script, IWorkerObserver& observer,
                     CChangeAttrsData* attrsData, CConvertData* convertData,
                     bool headless)
{
    if (headless && script != NULL &&
        sally::worker::CanRunWorkerDirectHeadless(*script, attrsData, convertData))
    {
        return sally::worker::RunWorkerDirectHeadless(script, observer, attrsData, convertData);
    }

    CWorkerState workerState;
    if (headless)
        workerState.InitHeadless();
    else
        workerState.Init();

    if (script->TotalSize == CQuadWord(0, 0))
        script->TotalSize = CQuadWord(1, 0);

    if (script->CopySecurity)
        GainWriteOwnerAccess();

    void* buffer;
    BOOL bufferIsAllocated;
    if (attrsData != NULL)
    {
        buffer = attrsData;
        bufferIsAllocated = FALSE;
    }
    else
    {
        bufferIsAllocated = TRUE;
        buffer = malloc(max(REMOVABLE_DISK_COPY_BUFFER, OPERATION_BUFFER));
        if (buffer == NULL)
        {
            observer.SetError(true);
            observer.NotifyDone();
            return FALSE;
        }
    }

    observer.SetProgress(0, 0);
    script->InitSpeedMeters(FALSE);

    std::wstring lastLantasticCheckRoot;
    BOOL lastIsLantasticPath = FALSE;
    int mustDeleteFileBeforeOverwrite = 0;
    int allocWholeFileOnStart = 0;
    int setDirTimeAfterMove = script->PreserveDirTime && script->SourcePathIsNetwork ? 0 : 2;

    BOOL Error = FALSE;
    CQuadWord totalDone(0, 0);
    CProgressData pd;
    BOOL novellRenamePatch = FALSE;
    char* tgtBuffer = NULL;
    CAsyncCopyParams* asyncPar = NULL;
    DWORD clearReadonlyMask = script->ClearReadonlyMask;
    CConvertData convertDataLocal;
    if (convertData != NULL)
        convertDataLocal = *convertData;

    int i;
    for (i = 0; !observer.IsCancelled() && i < script->Count; i++)
    {
        COperation* op = &script->At(i);

        switch (op->Opcode)
        {
        case ocCopyFile:
        {
            pd.Operation = workerState.OpStrCopying;
            pd.Source = op->SourceNameW.c_str();
            pd.Preposition = workerState.OpStrCopyingPrep;
            pd.Target = op->TargetNameW.c_str();
            observer.SetOperationInfo(&pd);
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

            BOOL lantasticCheck = IsLantasticDriveW(op->TargetNameW.c_str(), lastLantasticCheckRoot, lastIsLantasticPath);
            Error = !DoCopyFile(op, observer, buffer, script, totalDone,
                                clearReadonlyMask, NULL, lantasticCheck, mustDeleteFileBeforeOverwrite,
                                allocWholeFileOnStart, workerState,
                                (op->OpFlags & OPFL_COPY_ADS) != 0,
                                (op->OpFlags & OPFL_AS_ENCRYPTED) != 0,
                                FALSE, asyncPar);
            break;
        }
        case ocMoveFile:
        case ocMoveDir:
        {
            pd.Operation = workerState.OpStrMoving;
            pd.Source = op->SourceNameW.c_str();
            pd.Preposition = workerState.OpStrMovingPrep;
            pd.Target = op->TargetNameW.c_str();
            observer.SetOperationInfo(&pd);
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

            BOOL lantasticCheck2 = IsLantasticDriveW(op->TargetNameW.c_str(), lastLantasticCheckRoot, lastIsLantasticPath);
            BOOL ignInvalidName = op->Opcode == ocMoveDir && (op->OpFlags & OPFL_IGNORE_INVALID_NAME) != 0;
            Error = !DoMoveFile(op, observer, buffer, script, totalDone,
                                op->Opcode == ocMoveDir, clearReadonlyMask, &novellRenamePatch,
                                lantasticCheck2, mustDeleteFileBeforeOverwrite,
                                allocWholeFileOnStart, workerState,
                                (op->OpFlags & OPFL_COPY_ADS) != 0,
                                (op->OpFlags & OPFL_AS_ENCRYPTED) != 0,
                                &setDirTimeAfterMove, asyncPar, ignInvalidName);
            break;
        }
        case ocDeleteFile:
        {
            pd.Operation = workerState.OpStrDeleting;
            pd.Source = op->SourceNameW.c_str();
            pd.Preposition = L"";
            pd.Target = L"";
            observer.SetOperationInfo(&pd);
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

            Error = !DoDeleteFile(observer, op->Size,
                                  script, totalDone, op->Attr, workerState,
                                  op->SourceNameW);
            break;
        }
        case ocCreateDir:
        {
            BOOL copyADS = (op->OpFlags & OPFL_COPY_ADS) != 0;
            BOOL crAsEncrypted = (op->OpFlags & OPFL_AS_ENCRYPTED) != 0;
            BOOL ignInvalidName2 = (op->OpFlags & OPFL_IGNORE_INVALID_NAME) != 0;
            pd.Operation = workerState.OpStrCreatingDir;
            pd.Source = op->TargetNameW.c_str();
            pd.Preposition = L"";
            pd.Target = L"";
            observer.SetOperationInfo(&pd);
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

            BOOL skip, alreadyExisted;
            Error = !DoCreateDir(observer, op->Attr, clearReadonlyMask, workerState,
                                 totalDone, op->Size, copyADS, script, buffer, skip,
                                 alreadyExisted, crAsEncrypted, ignInvalidName2,
                                 op->TargetNameW,
                                 op->SourceNameW);
            if (!Error)
            {
                if (skip)
                {
                    CQuadWord skipTotal(0, 0);
                    int createDirIndex = i;
                    while (++i < script->Count)
                    {
                        COperation* oper = &script->At(i);
                        if (oper->Opcode == ocLabelForSkipOfCreateDir && (int)oper->Attr == createDirIndex)
                        {
                            script->AddBytesToTFS(oper->SkippedDirSize);
                            break;
                        }
                        skipTotal += oper->Size;
                    }
                    if (i == script->Count)
                        i = createDirIndex;
                    else
                        totalDone += skipTotal;
                }
                else
                {
                    if (alreadyExisted)
                        op->Attr = 0x10000000;
                    else
                        op->Attr = 0x01000000;
                    totalDone += op->Size;
                    script->SetProgressSize(totalDone);
                    observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
                }
            }
            break;
        }
        case ocDeleteDir:
        case ocDeleteDirLink:
        {
            pd.Operation = workerState.OpStrDeleting;
            pd.Source = op->SourceNameW.c_str();
            pd.Preposition = L"";
            pd.Target = L"";
            observer.SetOperationInfo(&pd);
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

            if (op->Opcode == ocDeleteDir)
            {
                Error = !DoDeleteDir(observer, op->Size,
                                     script, totalDone, op->Attr, !op->DeleteDirToRecycleBin,
                                     workerState,
                                     op->SourceNameW);
            }
            else
            {
                Error = !DoDeleteDirLink(observer, op->Size,
                                         script, totalDone, workerState,
                                         op->SourceNameW);
            }
            break;
        }

        case ocCreateDirLink:
        {
            pd.Operation = workerState.OpStrCopying;
            pd.Source = op->SourceNameW.c_str();
            pd.Preposition = workerState.OpStrCopyingPrep;
            pd.Target = op->TargetNameW.c_str();
            observer.SetOperationInfo(&pd);
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
            Error = !DoCreateDirLink(observer, op, script, totalDone, workerState);
            break;
        }
        case ocCopyDirTime:
        {
            FILETIME modified = op->DirTime;
            Error = !DoCopyDirTime(observer, &modified, workerState, FALSE,
                                   op->TargetNameW);
            if (!Error)
            {
                script->AddBytesToSpeedMetersAndTFSandPS((DWORD)op->Size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);
                totalDone += op->Size;
                observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));
            }
            break;
        }
        case ocCountSize:
            totalDone += op->Size;
            break;
        case ocConvert:
        {
            if (tgtBuffer == NULL)
            {
                tgtBuffer = (char*)malloc(OPERATION_BUFFER * 2);
                if (tgtBuffer == NULL)
                {
                    Error = TRUE;
                    break;
                }
            }
            pd.Operation = workerState.OpStrConverting;
            pd.Source = op->SourceNameW.c_str();
            pd.Preposition = L"";
            pd.Target = L"";
            observer.SetOperationInfo(&pd);
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

            Error = !DoConvert(observer, (char*)buffer, tgtBuffer, op->Size, script,
                               totalDone, convertDataLocal, workerState,
                               op->SourceNameW);
            break;
        }
        case ocChangeAttrs:
        {
            pd.Operation = workerState.OpStrChangingAttrs;
            pd.Source = op->SourceNameW.c_str();
            pd.Preposition = L"";
            pd.Target = L"";
            observer.SetOperationInfo(&pd);
            observer.SetProgress(0, CaclProg(totalDone, script->TotalSize));

            BOOL changeCompr = attrsData != NULL ? attrsData->ChangeCompression : FALSE;
            BOOL changeEncr = attrsData != NULL ? attrsData->ChangeEncryption : FALSE;
            Error = !DoChangeAttrs(observer, op->Size, op->NewAttrs,
                                   script, totalDone,
                                   attrsData != NULL && attrsData->ChangeTimeModified ? &attrsData->TimeModified : NULL,
                                   attrsData != NULL && attrsData->ChangeTimeCreated ? &attrsData->TimeCreated : NULL,
                                   attrsData != NULL && attrsData->ChangeTimeAccessed ? &attrsData->TimeAccessed : NULL,
                                   changeCompr, changeEncr,
                                   op->Attr, workerState,
                                   op->SourceNameW);
            break;
        }
        case ocLabelForSkipOfCreateDir:
            break;
        }
        if (Error)
            break;
        observer.WaitIfSuspended();
    }

    if (asyncPar != NULL)
        delete asyncPar;
    if (tgtBuffer != NULL)
        free(tgtBuffer);
    if (bufferIsAllocated)
        free(buffer);

    BOOL success = !Error && !observer.IsCancelled();
    observer.SetError(Error != FALSE);
    observer.NotifyDone();
    return success;
}

unsigned ThreadWorkerEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return ThreadWorkerBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread Worker: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harsher exit (this one still invokes something)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI ThreadWorker(void* param)
{
    CCallStack stack;
    return ThreadWorkerEH(param);
}

HANDLE StartWorker(COperations* script, HWND hDlg, CChangeAttrsData* attrsData,
                   CConvertData* convertData, HANDLE wContinue, HANDLE workerNotSuspended,
                   BOOL* cancelWorker, int* operationProgress, int* summaryProgress)
{
    CWorkerData data;
    data.WorkerNotSuspended = workerNotSuspended;
    data.CancelWorker = cancelWorker;
    data.OperationProgress = operationProgress;
    data.SummaryProgress = summaryProgress;
    data.WContinue = wContinue;
    data.ConvertData = convertData;
    data.Script = script;
    data.HProgressDlg = hDlg;
    data.ClearReadonlyMask = script->ClearReadonlyMask;
    if (attrsData != NULL)
    {
        data.Buffer = attrsData;
        data.BufferIsAllocated = FALSE;
    }
    else
    {
        data.BufferIsAllocated = TRUE;
        data.Buffer = malloc(max(REMOVABLE_DISK_COPY_BUFFER, OPERATION_BUFFER));
        if (data.Buffer == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return NULL;
        }
    }
    DWORD threadID;
    ResetEvent(wContinue);
    *cancelWorker = FALSE;

    // if (Worker != NULL) HANDLES(CloseHandle(Worker));  // was probably unnecessary
    HANDLE worker = HANDLES(CreateThread(NULL, 0, ThreadWorker, &data, 0, &threadID));
    if (worker == NULL)
    {
        if (data.BufferIsAllocated)
            free(data.Buffer);
        TRACE_E("Unable to start Worker thread.");
        return NULL;
    }
    //  SetThreadPriority(Worker, THREAD_PRIORITY_HIGHEST);
    WaitForSingleObject(wContinue, INFINITE); // wait until it copies the data (they are on the stack)
    return worker;
}

void FreeScript(COperations* script)
{
    if (script == NULL)
        return;
    // Note: COperation destructors now handle freeing SourceName/TargetName
    // based on ownership flags. The manual free loop is no longer needed.
    // delete script -> ~COperations -> ~vector -> ~COperation for each element
    // WaitInQueue strings are now std::string members, auto-destroyed with the object.
    delete script;
}

BOOL COperationsQueue::AddOperation(HWND dlg, BOOL startOnIdle, BOOL* startPaused)
{
    CALL_STACK_MESSAGE1("COperationsQueue::AddOperation()");

    HANDLES(EnterCriticalSection(&QueueCritSect));

    int i;
    for (i = 0; i < OperDlgs.Count; i++) // ensure uniqueness (an operation can be added only once)
        if (OperDlgs[i] == dlg)
            break;

    BOOL ret = FALSE;
    if (i == OperDlgs.Count) // the operation can be added
    {
        OperDlgs.Add(dlg);
        if (OperDlgs.IsGood())
        {
            if (startOnIdle)
            {
                int j;
                for (j = 0; j < OperPaused.Count && OperPaused[j] == 1 /* auto-paused */; j++)
                    ; // if another operation is already running or was paused manually, start this one as "auto-paused"
                *startPaused = j < OperPaused.Count;
            }
            else
                *startPaused = FALSE;
            OperPaused.Add(*startPaused ? 1 /* auto-paused */ : 0 /* running */);
            if (!OperPaused.IsGood())
            {
                OperPaused.ResetState();
                OperDlgs.Delete(OperDlgs.Count - 1);
                if (!OperDlgs.IsGood())
                    OperDlgs.ResetState();
            }
            else
                ret = TRUE;
        }
        else
            OperDlgs.ResetState();
    }
    else
        TRACE_E("COperationsQueue::AddOperation(): this operation has already been added!");

    HANDLES(LeaveCriticalSection(&QueueCritSect));

    return ret;
}

void COperationsQueue::OperationEnded(HWND dlg, BOOL doNotResume, HWND* foregroundWnd)
{
    CALL_STACK_MESSAGE1("COperationsQueue::OperationEnded()");

    HANDLES(EnterCriticalSection(&QueueCritSect));

    BOOL found = FALSE;
    int i;
    for (i = 0; i < OperDlgs.Count; i++)
    {
        if (OperDlgs[i] == dlg)
        {
            found = TRUE;
            OperDlgs.Delete(i);
            if (!OperDlgs.IsGood())
                OperDlgs.ResetState();
            OperPaused.Delete(i);
            if (!OperPaused.IsGood())
                OperPaused.ResetState();
            break;
        }
    }
    if (!found)
        TRACE_E("COperationsQueue::OperationEnded(): unexpected situation: operation was not found!");
    else
    {
        if (!doNotResume)
        {
            int j;
            for (j = 0; j < OperPaused.Count && OperPaused[j] == 1 /* auto-paused */; j++)
                ; // if no operation is running and none was paused manually, resume the first one in the queue
            if (j == OperPaused.Count && OperDlgs.Count > 0)
            {
                PostMessage(OperDlgs[0], WM_COMMAND, CM_RESUMEOPER, 0);
                if (foregroundWnd != NULL && GetForegroundWindow() == dlg)
                    *foregroundWnd = OperDlgs[0];
            }
        }
    }

    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void COperationsQueue::SetPaused(HWND dlg, int paused)
{
    CALL_STACK_MESSAGE1("COperationsQueue::SetPaused()");

    HANDLES(EnterCriticalSection(&QueueCritSect));

    int i;
    for (i = 0; i < OperDlgs.Count; i++)
    {
        if (OperDlgs[i] == dlg)
        {
            OperPaused[i] = paused;
            break;
        }
    }
    if (i == OperDlgs.Count)
        TRACE_E("COperationsQueue::SetPaused(): operation was not found!");

    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

BOOL COperationsQueue::IsEmpty()
{
    CALL_STACK_MESSAGE1("COperationsQueue::IsEmpty()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    BOOL ret = OperDlgs.Count == 0;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return ret;
}

void COperationsQueue::AutoPauseOperation(HWND dlg, HWND* foregroundWnd)
{
    CALL_STACK_MESSAGE1("COperationsQueue::AutoPauseOperation()");

    HANDLES(EnterCriticalSection(&QueueCritSect));

    int i;
    for (i = 0; i < OperDlgs.Count; i++)
    {
        if (OperDlgs[i] == dlg)
        {
            int j;
            for (j = i; j + 1 < OperDlgs.Count; j++)
                OperDlgs[j] = OperDlgs[j + 1];
            for (j = i; j + 1 < OperPaused.Count; j++)
                OperPaused[j] = OperPaused[j + 1];
            OperDlgs[j] = dlg;
            OperPaused[j] = 1 /* auto-paused */;
            break;
        }
    }
    if (i == OperDlgs.Count)
        TRACE_E("COperationsQueue::AutoPauseOperation(): operation was not found!");

    // if no operation is running and none was paused manually, resume the first one in the queue
    int j;
    for (j = 0; j < OperPaused.Count && OperPaused[j] == 1 /* auto-paused */; j++)
        ;
    if (j == OperPaused.Count && OperDlgs.Count > 0)
    {
        PostMessage(OperDlgs[0], WM_COMMAND, CM_RESUMEOPER, 0);
        if (foregroundWnd != NULL && GetForegroundWindow() == dlg)
            *foregroundWnd = OperDlgs[0];
    }

    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

int COperationsQueue::GetNumOfOperations()
{
    CALL_STACK_MESSAGE1("COperationsQueue::GetNumOfOperations()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    int c = OperDlgs.Count;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return c;
}
