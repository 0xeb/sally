// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shexreg_ipc_protocol.h"

static BOOL SALSHEXT_AddU64(UINT64 left, UINT64 right, UINT64* result)
{
    UINT64 value = left + right;
    if (value < left)
        return FALSE;
    *result = value;
    return TRUE;
}

static BOOL SALSHEXT_TerminatedUtf16Bytes(UINT64 codeUnits, UINT64* result)
{
    DWORD units;
    if (codeUnits > (MAXDWORD / sizeof(wchar_t)) - 1)
        return FALSE;
    units = (DWORD)codeUnits;
    *result = (DWORD)((units + 1) * sizeof(wchar_t));
    return TRUE;
}

BOOL SALSHEXT_CalculatePayloadBytes(const SALSHEXT_PAYLOAD_INPUT* fields, DWORD fieldCount,
                                    DWORD* byteSize)
{
    UINT64 total;
    UINT64 tableBytes;
    DWORD i;

    if (byteSize == NULL || fields == NULL || fieldCount == 0 ||
        fieldCount > SALSHEXT_MAX_PAYLOAD_FIELDS)
        return FALSE;

    tableBytes = (UINT64)fieldCount * sizeof(SALSHEXT_PAYLOAD_FIELD);
    if (!SALSHEXT_AddU64(sizeof(SALSHEXT_PAYLOAD_HEADER), tableBytes, &total))
        return FALSE;

    for (i = 0; i < fieldCount; i++)
    {
        UINT64 textBytes;
        if (fields[i].FieldId == 0 || fields[i].Text == NULL ||
            !SALSHEXT_TerminatedUtf16Bytes(fields[i].LengthCodeUnits, &textBytes) ||
            !SALSHEXT_AddU64(total, textBytes, &total))
            return FALSE;
    }
    if (total > MAXDWORD)
        return FALSE;
    *byteSize = (DWORD)total;
    return TRUE;
}

BOOL SALSHEXT_WritePayload(void* destination, DWORD destinationBytes, UINT64 requestId,
                           UINT64 generation, const SALSHEXT_PAYLOAD_INPUT* fields,
                           DWORD fieldCount)
{
    SALSHEXT_PAYLOAD_HEADER* header;
    SALSHEXT_PAYLOAD_FIELD* table;
    BYTE* bytes;
    UINT64 offset;
    DWORD required;
    DWORD i;

    if (!SALSHEXT_CalculatePayloadBytes(fields, fieldCount, &required) ||
        destination == NULL || destinationBytes != required)
        return FALSE;

    bytes = (BYTE*)destination;
    header = (SALSHEXT_PAYLOAD_HEADER*)destination;
    table = (SALSHEXT_PAYLOAD_FIELD*)(bytes + sizeof(*header));
    header->Magic = SALSHEXT_PAYLOAD_MAGIC;
    header->Version = SALSHEXT_PAYLOAD_VERSION;
    header->TotalBytes = required;
    header->RequestId = requestId;
    header->Generation = generation;
    header->FieldCount = fieldCount;
    header->Reserved = 0;
    offset = sizeof(*header) + ((UINT64)fieldCount * sizeof(*table));

    for (i = 0; i < fieldCount; i++)
    {
        DWORD j;
        DWORD length = (DWORD)fields[i].LengthCodeUnits;
        wchar_t* output;
        table[i].FieldId = fields[i].FieldId;
        table[i].Reserved = 0;
        table[i].OffsetBytes = offset;
        table[i].LengthCodeUnits = fields[i].LengthCodeUnits;
        output = (wchar_t*)(bytes + (DWORD)offset);
        for (j = 0; j < length; j++)
            output[j] = fields[i].Text[j];
        output[length] = L'\0';
        {
            UINT64 textBytes;
            if (!SALSHEXT_TerminatedUtf16Bytes(fields[i].LengthCodeUnits, &textBytes))
                return FALSE;
            offset += textBytes;
        }
    }
    return offset == required;
}

BOOL SALSHEXT_ValidatePayload(const void* payload, DWORD mappedBytes, UINT64 requestId,
                              UINT64 generation)
{
    const SALSHEXT_PAYLOAD_HEADER* header;
    const SALSHEXT_PAYLOAD_FIELD* table;
    UINT64 dataBegin;
    DWORD i;
    DWORD j;
    UINT64 expectedOffset;

    if (payload == NULL || mappedBytes < sizeof(SALSHEXT_PAYLOAD_HEADER))
        return FALSE;
    header = (const SALSHEXT_PAYLOAD_HEADER*)payload;
    if (header->Magic != SALSHEXT_PAYLOAD_MAGIC || header->Version != SALSHEXT_PAYLOAD_VERSION ||
        header->TotalBytes != mappedBytes || header->RequestId != requestId ||
        header->Generation != generation || header->FieldCount == 0 ||
        header->FieldCount > SALSHEXT_MAX_PAYLOAD_FIELDS || header->Reserved != 0)
        return FALSE;
    dataBegin = sizeof(*header) + ((UINT64)header->FieldCount * sizeof(SALSHEXT_PAYLOAD_FIELD));
    if (dataBegin > mappedBytes || (mappedBytes & (sizeof(wchar_t) - 1)) != 0)
        return FALSE;
    table = (const SALSHEXT_PAYLOAD_FIELD*)((const BYTE*)payload + sizeof(*header));
    expectedOffset = dataBegin;
    for (i = 0; i < header->FieldCount; i++)
    {
        UINT64 textBytes;
        UINT64 end;
        const wchar_t* text;
        DWORD length;
        if (table[i].FieldId == 0 || table[i].Reserved != 0 ||
            (table[i].OffsetBytes & (sizeof(wchar_t) - 1)) != 0 ||
            table[i].OffsetBytes != expectedOffset ||
            !SALSHEXT_TerminatedUtf16Bytes(table[i].LengthCodeUnits, &textBytes) ||
            !SALSHEXT_AddU64(table[i].OffsetBytes, textBytes, &end) || end > mappedBytes)
            return FALSE;
        length = (DWORD)table[i].LengthCodeUnits;
        expectedOffset = end;
        text = (const wchar_t*)((const BYTE*)payload + (DWORD)table[i].OffsetBytes);
        if (text[length] != L'\0')
            return FALSE;
        for (j = 0; j < i; j++)
        {
            UINT64 otherBytes;
            UINT64 otherEnd;
            if (table[j].FieldId == table[i].FieldId ||
                !SALSHEXT_TerminatedUtf16Bytes(table[j].LengthCodeUnits, &otherBytes) ||
                !SALSHEXT_AddU64(table[j].OffsetBytes, otherBytes, &otherEnd) ||
                !(end <= table[j].OffsetBytes || otherEnd <= table[i].OffsetBytes))
                return FALSE;
        }
    }
    return expectedOffset == mappedBytes;
}

BOOL SALSHEXT_GetPayloadField(const void* payload, DWORD mappedBytes, UINT64 requestId,
                              UINT64 generation, DWORD fieldId, const wchar_t** text,
                              UINT64* lengthCodeUnits)
{
    const SALSHEXT_PAYLOAD_HEADER* header;
    const SALSHEXT_PAYLOAD_FIELD* table;
    DWORD i;
    if (text == NULL || lengthCodeUnits == NULL ||
        !SALSHEXT_ValidatePayload(payload, mappedBytes, requestId, generation))
        return FALSE;
    header = (const SALSHEXT_PAYLOAD_HEADER*)payload;
    table = (const SALSHEXT_PAYLOAD_FIELD*)((const BYTE*)payload + sizeof(*header));
    for (i = 0; i < header->FieldCount; i++)
    {
        if (table[i].FieldId == fieldId)
        {
            *text = (const wchar_t*)((const BYTE*)payload + (DWORD)table[i].OffsetBytes);
            *lengthCodeUnits = table[i].LengthCodeUnits;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL SALSHEXT_IsCompatibleControl(const CSalShExtSharedMem* control)
{
    return control != NULL && control->Magic == SALSHEXT_CONTROL_MAGIC &&
           control->Version == SALSHEXT_CONTROL_VERSION && control->Size == sizeof(*control);
}
