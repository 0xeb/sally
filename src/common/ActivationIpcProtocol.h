// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

#include "common/CommandLineParser.h"

#define SALLY_ACTIVATION_PROTOCOL_MAGIC 0x32544341u /* ACT2 */
#define SALLY_ACTIVATION_PROTOCOL_VERSION 2u
#define SALLY_ACTIVATION_FLAG_SET_TITLE 0x00000001u
#define SALLY_ACTIVATION_FLAG_SET_ICON 0x00000002u

// Versioned, ASCII-only Win32 object identifiers. V2 intentionally cannot
// attach to the fixed-text Sally1 task-list layout; incompatible peers start a
// separate instance instead of interpreting each other's shared memory.
#define SALLY_PROCESS_LIST_MAPPING_NAME "Sally2ProcessList"
#define SALLY_PROCESS_LIST_MUTEX_NAME "Sally2ProcessListMutex"
#define SALLY_PROCESS_LIST_EVENT_NAME "Sally2ProcessListEvent"
#define SALLY_PROCESS_LIST_PROCESSED_EVENT_NAME "Sally2ProcessListEventProcessed"
#define SALLY_ACTIVATION_PAYLOAD_BASE_NAME "Sally2ActivationPayload"
#define SALLY_ACTIVATION_PROCESSED_EVENT_BASE_NAME "Sally2ActivationProcessed"
#define SALLY_ACTIVATION_DISPATCH_MUTEX_NAME "Sally2ActivationDispatchMutex"
#define SALLY_PROCESS_LIST_PROTOCOL_MAGIC 0x32534C50u /* PLS2 */
#define SALLY_PROCESS_LIST_PROTOCOL_VERSION 2u

#pragma pack(push, 4)

// Fixed control metadata embedded in the cross-process task list. It contains
// no pointers or text and therefore has the same layout in x86 and x64.
struct CActivationRequestRef
{
    DWORD StructSize;
    DWORD ProtocolVersion;
    DWORD RequestUID;
    DWORD RequestTimestamp;
    DWORD SenderPID;
    DWORD Generation;
    DWORD PayloadBytes;
    DWORD Reserved;
};

struct CActivationPayloadField
{
    DWORD Offset;
    DWORD LengthChars;
};

// Request-sized mapping header. Offsets are byte offsets from this header and
// lengths exclude the checked trailing UTF-16 NUL.
struct CActivationPayloadHeader
{
    DWORD Magic;
    DWORD Version;
    DWORD HeaderSize;
    DWORD TotalSize;
    DWORD RequestUID;
    DWORD Generation;
    DWORD SenderPID;
    DWORD ActivatePanel;
    DWORD Flags;
    DWORD MainWindowIconIndex;
    CActivationPayloadField LeftPath;
    CActivationPayloadField RightPath;
    CActivationPayloadField ActivePath;
    CActivationPayloadField TitlePrefix;
};

#pragma pack(pop)

static_assert(sizeof(CActivationRequestRef) == 32, "activation control ABI drift");
static_assert(sizeof(CActivationPayloadField) == 8, "activation field ABI drift");
static_assert(sizeof(CActivationPayloadHeader) == 72, "activation payload ABI drift");

bool BuildActivationPayload(const sally::cmdline::CommandLineRequest& request,
                            DWORD requestUID, DWORD generation, DWORD senderPID,
                            std::vector<BYTE>& payload);

bool ParseActivationPayload(const void* payload, std::size_t payloadBytes,
                            DWORD expectedRequestUID, DWORD expectedGeneration,
                            DWORD expectedSenderPID,
                            sally::cmdline::CommandLineRequest& request);

std::string BuildActivationPayloadName(const std::string& namespacedBase,
                                       DWORD senderPID, DWORD requestUID,
                                       DWORD generation);
