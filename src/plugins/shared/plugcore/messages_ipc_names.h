// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

namespace sally
{
namespace plugcore
{
static const char kMessageCenterInstanceIdEnvA[] = "SALLY_INSTANCE_ID";

inline BOOL IsMessageCenterObjectNameChar(char ch)
{
    return ch >= 'A' && ch <= 'Z' ||
           ch >= 'a' && ch <= 'z' ||
           ch >= '0' && ch <= '9' ||
           ch == '_' || ch == '-';
}

inline void AppendBounded(char* dest, int destSize, const char* src)
{
    if (dest == NULL || src == NULL || destSize <= 0)
        return;

    int used = lstrlenA(dest);
    if (used >= destSize - 1)
        return;

    lstrcpynA(dest + used, src, destSize - used);
}

inline void SanitizeMessageCenterInstanceId(char* dest, int destSize, const char* instanceId)
{
    if (dest == NULL || destSize <= 0)
        return;

    dest[0] = 0;
    if (instanceId == NULL)
        return;

    int out = 0;
    for (const char* src = instanceId; *src != 0 && out < destSize - 1; ++src)
        dest[out++] = IsMessageCenterObjectNameChar(*src) ? *src : '_';

    while (out > 0 && dest[out - 1] == '_')
        --out;
    dest[out] = 0;
}

inline void GetMessageCenterInstanceIdForCurrentProcess(char* dest, int destSize)
{
    if (dest == NULL || destSize <= 0)
        return;

    dest[0] = 0;
    DWORD len = GetEnvironmentVariableA(kMessageCenterInstanceIdEnvA, dest, (DWORD)destSize);
    if (len == 0)
    {
#ifdef _DEBUG
        lstrcpynA(dest, "Debug", destSize);
#endif
        return;
    }

    if (len >= (DWORD)destSize)
        dest[0] = 0;
}

inline void BuildMessageCenterObjectName(char* dest, int destSize, const char* baseName, const char* instanceId)
{
    if (dest == NULL || destSize <= 0)
        return;

    lstrcpynA(dest, baseName != NULL ? baseName : "", destSize);

    char sanitized[128];
    SanitizeMessageCenterInstanceId(sanitized, (int)sizeof(sanitized), instanceId);
    if (sanitized[0] != 0)
    {
        AppendBounded(dest, destSize, "_");
        AppendBounded(dest, destSize, sanitized);
    }
}

inline void BuildMessageCenterObjectNameForCurrentProcess(char* dest, int destSize, const char* baseName)
{
    char instanceId[128];
    GetMessageCenterInstanceIdForCurrentProcess(instanceId, (int)sizeof(instanceId));
    BuildMessageCenterObjectName(dest, destSize, baseName, instanceId);
}
} // namespace plugcore
} // namespace sally
