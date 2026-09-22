// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // mutex name used to access the shared memory (opened via OpenMutex after it was created with CreateMutex)
    extern const wchar_t* SALSHEXT_SHAREDMEMMUTEXNAME;
    // shared-memory name (opened via OpenFileMapping after being created with CreateFileMapping)
    extern const wchar_t* SALSHEXT_SHAREDMEMNAME;
    // event name used to request Paste in the source Salamander; used only on Vista+
    // (older OS versions can post WM_USER_SALSHEXT_PASTE directly from the copy hook; on Vista+
    // that post fails when Salamander runs "as admin")
    extern const wchar_t* SALSHEXT_DOPASTEEVENTNAME;
    extern const wchar_t* SALSHEXT_V6_SHAREDMEMMUTEXNAME;
    extern const wchar_t* SALSHEXT_V6_SHAREDMEMNAME;

#define SALSHEXT_IPC_ISOLATION_ENV_A "SALLY_SHELL_EXTENSION_IPC_ISOLATION"
#define SALSHEXT_INSTANCE_ID_ENV_A "SALLY_INSTANCE_ID"
// Wide siblings, used by the .c now that it reads the environment with
// GetEnvironmentVariableW. The _A forms stay: tests set these via SetEnvironmentVariableA,
// and Windows converts, so a value written narrow reads back wide unchanged.
#define SALSHEXT_IPC_ISOLATION_ENV_W L"SALLY_SHELL_EXTENSION_IPC_ISOLATION"
#define SALSHEXT_INSTANCE_ID_ENV_W L"SALLY_INSTANCE_ID"

    // Compatibility names are the default. Isolation is opt-in so Explorer-side
    // shell-extension IPC is not silently separated from production Sally.
    BOOL SALSHEXT_IsIpcIsolationEnabled();
    const wchar_t* SALSHEXT_GetSharedMemMutexName(wchar_t* buffer, DWORD bufferSize);
    const wchar_t* SALSHEXT_GetSharedMemName(wchar_t* buffer, DWORD bufferSize);
    const wchar_t* SALSHEXT_GetDoPasteEventName(wchar_t* buffer, DWORD bufferSize);
    const wchar_t* SALSHEXT_GetPayloadName(DWORD senderProcessId, UINT64 requestId,
                                           UINT64 generation, DWORD payloadKey,
                                           wchar_t* buffer, DWORD bufferSize);

#ifdef __cplusplus
} // extern "C"
#endif
