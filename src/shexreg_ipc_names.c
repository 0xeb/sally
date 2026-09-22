// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shexreg_ipc_names.h"

//const char *SALSHEXT_SHAREDMEMMUTEXNAME = "SalShExt_SharedMemMutex"; // salshext.dll (Sal 2.5 beta 1)
//const char *SALSHEXT_SHAREDMEMNAME = "SalShExt_SharedMem";           // salshext.dll (Sal 2.5 beta 1)
//const char *SALSHEXT_SHAREDMEMMUTEXNAME = "SalExten_SharedMemMutex"; // salexten.dll - pracovni verze, pred 2.5 beta 2
//const char *SALSHEXT_SHAREDMEMNAME = "SalExten_SharedMem";           // salexten.dll - pracovni verze, pred 2.5 beta 2
//const char *SALSHEXT_SHAREDMEMMUTEXNAME = "SalExten_SharedMemMutex2";// salexten.dll (od verze 2.5 beta 2) + salamext.dll
//const char *SALSHEXT_SHAREDMEMNAME = "SalExten_SharedMem2";          // salexten.dll (od verze 2.5 beta 2) + salamext.dll
//const char *SALSHEXT_DOPASTEEVENTNAME = "SalExten_DoPasteEvent2";    // salamext.dll - pracovni verze pro 2.52 beta 1, pouzivala se jen pod Vista+
//const char *SALSHEXT_SHAREDMEMMUTEXNAME = "SalExten_SharedMemMutex3";// salamext.dll (od verze 2.52 beta 1)
//const char *SALSHEXT_SHAREDMEMNAME = "SalExten_SharedMem3";          // salamext.dll (od verze 2.52 beta 1)
//const char *SALSHEXT_DOPASTEEVENTNAME = "SalExten_DoPasteEvent3";    // salamext.dll (od verze 2.52 beta 1, pouziva se jen pod Vista+)
// Wide, to match the declarations in shexreg_ipc_names.h. The header was
// widened by an earlier sweep while this .c was not - the sweep glob only ever covered
// *.cpp and *.h - and because the header is extern "C" there is no mangling, so the
// linker matched the mismatched signatures without a word. The NAME STRINGS are
// unchanged: CreateMutexA("X") and CreateMutexW(L"X") address the same kernel object, so
// widening here does not alter which objects Explorer and Sally meet on.
//const wchar_t* SALSHEXT_SHAREDMEMMUTEXNAME = L"SalExten_SharedMemMutex5"; // salextx64.dll (Sally 1.0)
//const wchar_t* SALSHEXT_SHAREDMEMNAME = L"SalExten_SharedMem5";        // salextx64.dll (Sally 1.0)
//const wchar_t* SALSHEXT_DOPASTEEVENTNAME = L"SalExten_DoPasteEvent5";  // salextx64.dll (Sally 1.0)
// `CSalShExtSharedMem` (shexreg.h) was widened to wchar_t fields under THIS
// still-live "5" name, never bumped - violating the never-mutate-in-place convention this
// very ladder exists to enforce (see SharedMemCompat.h's own header comment, which already
// documented "_5 (current) -> _6 next" as the plan). A stale salextx64.dll built before that
// widening (the normal case for a shell extension, not an exotic one - Explorer can keep an
// old copy loaded indefinitely) would `CreateFileMapping`/`OpenFileMapping` the SAME name
// with the SMALLER, narrow-struct size; today's sally.exe would then read/write the wide
// struct's later fields past the end of that smaller mapping - cross-process memory
// corruption with no error, no crash where you'd look for one. Bumping the name is the
// documented fix: two different struct layouts now simply create two different, independent
// mappings and never interoperate, instead of interoperating incorrectly.
const wchar_t* SALSHEXT_V6_SHAREDMEMMUTEXNAME = L"SalExten_SharedMemMutex6";
const wchar_t* SALSHEXT_V6_SHAREDMEMNAME = L"SalExten_SharedMem6";
const wchar_t* SALSHEXT_SHAREDMEMMUTEXNAME = L"SalExten_SharedMemMutex7";
const wchar_t* SALSHEXT_SHAREDMEMNAME = L"SalExten_SharedMem7";
const wchar_t* SALSHEXT_DOPASTEEVENTNAME = L"SalExten_DoPasteEvent7";
static const wchar_t* SALSHEXT_PAYLOADNAME = L"SalExten_Payload7";

static int SALSHEXT_IsTruthyEnvValue(const wchar_t* value)
{
    return value != NULL &&
           (lstrcmpiW(value, L"1") == 0 ||
            lstrcmpiW(value, L"true") == 0 ||
            lstrcmpiW(value, L"yes") == 0 ||
            lstrcmpiW(value, L"on") == 0);
}

BOOL SALSHEXT_IsIpcIsolationEnabled()
{
    wchar_t enabled[16];
    wchar_t instanceId[128];
    // _countof, not sizeof: GetEnvironmentVariableW counts CHARACTERS.
    DWORD enabledLen = GetEnvironmentVariableW(SALSHEXT_IPC_ISOLATION_ENV_W, enabled, ARRAYSIZE(enabled));
    DWORD instanceLen = GetEnvironmentVariableW(SALSHEXT_INSTANCE_ID_ENV_W, instanceId, ARRAYSIZE(instanceId));

    if (enabledLen == 0 || enabledLen >= ARRAYSIZE(enabled) ||
        instanceLen == 0 || instanceLen >= ARRAYSIZE(instanceId))
    {
        return FALSE;
    }
    return SALSHEXT_IsTruthyEnvValue(enabled);
}

static void SALSHEXT_SanitizeInstanceId(const wchar_t* instanceId, wchar_t* buffer, DWORD bufferSize)
{
    DWORD out = 0;
    if (bufferSize == 0)
        return;

    for (; instanceId != NULL && *instanceId != 0 && out + 1 < bufferSize; instanceId++)
    {
        wchar_t ch = *instanceId;
        if ((ch >= L'a' && ch <= L'z') ||
            (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9') ||
            ch == L'-' || ch == L'_')
        {
            buffer[out++] = ch;
        }
        else if (out == 0 || buffer[out - 1] != L'_')
        {
            buffer[out++] = L'_';
        }
    }

    while (out > 0 && buffer[out - 1] == L'_')
        out--;
    buffer[out] = 0;
}

static BOOL SALSHEXT_AppendNamePart(wchar_t* buffer, DWORD bufferSize, DWORD* length, const wchar_t* text)
{
    if (buffer == NULL || bufferSize == 0 || length == NULL || *length >= bufferSize)
        return FALSE;

    while (text != NULL && *text != 0)
    {
        if (*length + 1 >= bufferSize)
        {
            buffer[0] = 0;
            return FALSE;
        }
        buffer[*length] = *text;
        (*length)++;
        text++;
    }
    buffer[*length] = 0;
    return TRUE;
}

static const wchar_t* SALSHEXT_GetSharedObjectName(const wchar_t* baseName, wchar_t* buffer, DWORD bufferSize)
{
    wchar_t instanceId[128];
    wchar_t sanitized[128];
    DWORD instanceLen;
    DWORD length;

    if (!SALSHEXT_IsIpcIsolationEnabled())
        return baseName;

    instanceLen = GetEnvironmentVariableW(SALSHEXT_INSTANCE_ID_ENV_W, instanceId, ARRAYSIZE(instanceId));
    if (instanceLen == 0 || instanceLen >= ARRAYSIZE(instanceId))
        return baseName;

    SALSHEXT_SanitizeInstanceId(instanceId, sanitized, ARRAYSIZE(sanitized));
    if (sanitized[0] == 0 || buffer == NULL || bufferSize == 0)
        return baseName;

    length = 0;
    buffer[0] = 0;
    if (!SALSHEXT_AppendNamePart(buffer, bufferSize, &length, baseName) ||
        !SALSHEXT_AppendNamePart(buffer, bufferSize, &length, L"_") ||
        !SALSHEXT_AppendNamePart(buffer, bufferSize, &length, sanitized))
    {
        return baseName;
    }
    return buffer;
}

const wchar_t* SALSHEXT_GetSharedMemMutexName(wchar_t* buffer, DWORD bufferSize)
{
    return SALSHEXT_GetSharedObjectName(SALSHEXT_SHAREDMEMMUTEXNAME, buffer, bufferSize);
}

const wchar_t* SALSHEXT_GetSharedMemName(wchar_t* buffer, DWORD bufferSize)
{
    return SALSHEXT_GetSharedObjectName(SALSHEXT_SHAREDMEMNAME, buffer, bufferSize);
}

const wchar_t* SALSHEXT_GetDoPasteEventName(wchar_t* buffer, DWORD bufferSize)
{
    return SALSHEXT_GetSharedObjectName(SALSHEXT_DOPASTEEVENTNAME, buffer, bufferSize);
}

static BOOL SALSHEXT_AppendHex(wchar_t* buffer, DWORD bufferSize, DWORD* length,
                              UINT64 value, DWORD digits)
{
    static const wchar_t hex[] = L"0123456789ABCDEF";
    ULARGE_INTEGER parts;
    DWORD index;
    if (digits == 0 || digits > 16 || *length + digits >= bufferSize)
        return FALSE;
    parts.QuadPart = value;
    for (index = digits; index > 0; index--)
    {
        DWORD nibbleIndex = index - 1;
        DWORD word = nibbleIndex >= 8 ? parts.HighPart : parts.LowPart;
        DWORD shift = (nibbleIndex & 7) * 4;
        buffer[(*length)++] = hex[(word >> shift) & 0xF];
    }
    buffer[*length] = 0;
    return TRUE;
}

const wchar_t* SALSHEXT_GetPayloadName(DWORD senderProcessId, UINT64 requestId,
                                       UINT64 generation, DWORD payloadKey,
                                       wchar_t* buffer, DWORD bufferSize)
{
    wchar_t base[256];
    const wchar_t* namespaceName = SALSHEXT_GetSharedObjectName(SALSHEXT_PAYLOADNAME,
                                                                base, ARRAYSIZE(base));
    DWORD length = 0;
    if (buffer == NULL || bufferSize == 0 ||
        !SALSHEXT_AppendNamePart(buffer, bufferSize, &length, namespaceName) ||
        !SALSHEXT_AppendNamePart(buffer, bufferSize, &length, L"_") ||
        !SALSHEXT_AppendHex(buffer, bufferSize, &length, senderProcessId, 8) ||
        !SALSHEXT_AppendNamePart(buffer, bufferSize, &length, L"_") ||
        !SALSHEXT_AppendHex(buffer, bufferSize, &length, requestId, 16) ||
        !SALSHEXT_AppendNamePart(buffer, bufferSize, &length, L"_") ||
        !SALSHEXT_AppendHex(buffer, bufferSize, &length, generation, 16) ||
        !SALSHEXT_AppendNamePart(buffer, bufferSize, &length, L"_") ||
        !SALSHEXT_AppendHex(buffer, bufferSize, &length, payloadKey, 8))
    {
        if (buffer != NULL && bufferSize != 0)
            buffer[0] = 0;
        return NULL;
    }
    return buffer;
}
