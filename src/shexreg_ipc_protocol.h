// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SALSHEXT_CONTROL_MAGIC 0x374C4153UL /* "SAL7" */
#define SALSHEXT_CONTROL_VERSION 7UL
#define SALSHEXT_PAYLOAD_MAGIC 0x37594150UL /* "PAY7" */
#define SALSHEXT_PAYLOAD_VERSION 7UL

#define SALSHEXT_STATE_DRAG_ACTIVE 0x00000001UL
#define SALSHEXT_STATE_PASTE_ACTIVE 0x00000002UL
#define SALSHEXT_STATE_DROP_DONE 0x00000004UL
#define SALSHEXT_STATE_PASTE_DONE 0x00000008UL

#define SALSHEXT_PAYLOAD_KEY_REQUEST 1UL
#define SALSHEXT_PAYLOAD_KEY_RESPONSE 2UL

#define SALSHEXT_FIELD_DRAG_FAKE_DIR 1UL
#define SALSHEXT_FIELD_PASTE_FAKE_DIR 2UL
#define SALSHEXT_FIELD_UNABLE_TO_PASTE_SAME_THREAD 3UL
#define SALSHEXT_FIELD_UNABLE_TO_PASTE_BUSY 4UL
#define SALSHEXT_FIELD_TARGET_PATH 5UL

#define SALSHEXT_MAX_PAYLOAD_FIELDS 4UL

#pragma pack(push, 4)
    typedef struct SALSHEXT_PAYLOAD_REF
    {
        DWORD SenderProcessId;
        DWORD ByteSize;
        UINT64 RequestId;
        UINT64 Generation;
        DWORD Key;
        DWORD Reserved;
    } SALSHEXT_PAYLOAD_REF;

    typedef struct CSalShExtSharedMem
    {
        DWORD Magic;
        DWORD Version;
        DWORD Size;
        DWORD StateFlags;

        DWORD ClipDataObjLastGetDataTime;
        DWORD SalamanderMainWndPID;
        DWORD SalamanderMainWndTID;
        DWORD PostMsgIndex;
        DWORD SessionId;
        DWORD InitiatorIntegrityRid;
        DWORD ReceiverProcessId;
        DWORD ReservedIdentity;
        UINT64 SalamanderMainWnd;

        DWORD BlockPasteDataRelease;
        DWORD SalBusyState;
        DWORD PastedDataID;
        DWORD Operation;

        UINT64 RequestId;
        UINT64 Generation;
        SALSHEXT_PAYLOAD_REF RequestPayload;
        SALSHEXT_PAYLOAD_REF ResponsePayload;
        UINT64 ResponseConsumedGeneration;
    } CSalShExtSharedMem;

    typedef struct SALSHEXT_PAYLOAD_HEADER
    {
        DWORD Magic;
        DWORD Version;
        UINT64 TotalBytes;
        UINT64 RequestId;
        UINT64 Generation;
        DWORD FieldCount;
        DWORD Reserved;
    } SALSHEXT_PAYLOAD_HEADER;

    typedef struct SALSHEXT_PAYLOAD_FIELD
    {
        DWORD FieldId;
        DWORD Reserved;
        UINT64 OffsetBytes;
        UINT64 LengthCodeUnits;
    } SALSHEXT_PAYLOAD_FIELD;

    typedef struct SALSHEXT_PAYLOAD_INPUT
    {
        DWORD FieldId;
        const wchar_t* Text;
        UINT64 LengthCodeUnits;
    } SALSHEXT_PAYLOAD_INPUT;
#pragma pack(pop)

    typedef char SALSHEXT_ASSERT_REF_SIZE[(sizeof(SALSHEXT_PAYLOAD_REF) == 32) ? 1 : -1];
    typedef char SALSHEXT_ASSERT_CONTROL_SIZE[(sizeof(CSalShExtSharedMem) == 160) ? 1 : -1];
    typedef char SALSHEXT_ASSERT_HEADER_SIZE[(sizeof(SALSHEXT_PAYLOAD_HEADER) == 40) ? 1 : -1];
    typedef char SALSHEXT_ASSERT_FIELD_SIZE[(sizeof(SALSHEXT_PAYLOAD_FIELD) == 24) ? 1 : -1];

    BOOL SALSHEXT_CalculatePayloadBytes(const SALSHEXT_PAYLOAD_INPUT* fields, DWORD fieldCount,
                                        DWORD* byteSize);
    BOOL SALSHEXT_WritePayload(void* destination, DWORD destinationBytes, UINT64 requestId,
                               UINT64 generation, const SALSHEXT_PAYLOAD_INPUT* fields,
                               DWORD fieldCount);
    BOOL SALSHEXT_ValidatePayload(const void* payload, DWORD mappedBytes, UINT64 requestId,
                                  UINT64 generation);
    BOOL SALSHEXT_GetPayloadField(const void* payload, DWORD mappedBytes, UINT64 requestId,
                                  UINT64 generation, DWORD fieldId, const wchar_t** text,
                                  UINT64* lengthCodeUnits);
    BOOL SALSHEXT_IsCompatibleControl(const CSalShExtSharedMem* control);

#ifdef __cplusplus
} // extern "C"
#endif
