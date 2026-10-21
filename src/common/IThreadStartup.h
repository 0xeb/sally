// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <cstddef>
#include <string>

struct StartupHandleResult
{
    HANDLE Handle;
    DWORD Error;
};

// Internal startup boundary. Returned handles belong to the caller; buffers
// must be compatible with free(), including on worker-thread completion.
class IThreadStartup
{
public:
    virtual ~IThreadStartup() = default;
    virtual StartupHandleResult CreateSignalEvent(bool manualReset, bool initialState) = 0;
    virtual StartupHandleResult StartThread(LPTHREAD_START_ROUTINE routine, void* parameter, DWORD* threadId) = 0;
    virtual void* AllocateBuffer(size_t bytes) = 0;
};

extern IThreadStartup* gThreadStartup;
IThreadStartup* GetWin32ThreadStartup();
std::wstring FormatThreadStartupErrorW(DWORD error);
