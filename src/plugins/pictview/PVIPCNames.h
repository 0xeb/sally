// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdio.h>
#include <tchar.h>
#include <windows.h>

inline void BuildPictViewEnvelopeMutexName(TCHAR* dest, int destSize, DWORD processId)
{
    if (dest == NULL || destSize <= 0)
        return;

    _sntprintf_s(dest, destSize, _TRUNCATE, _T("PVEXE_%08X"), processId);
}

inline void BuildPictViewEnvelopeCommandLine(TCHAR* dest, int destSize, const TCHAR* mutexName)
{
    if (dest == NULL || destSize <= 0)
        return;

    _sntprintf_s(dest, destSize, _TRUNCATE, _T("SalPVEnv.exe %s"), mutexName != NULL ? mutexName : _T(""));
}

inline void BuildPictViewMessageMapName(TCHAR* dest, int destSize, const TCHAR* mutexName, DWORD messageId)
{
    if (dest == NULL || destSize <= 0)
        return;

    _sntprintf_s(dest, destSize, _TRUNCATE, _T("%s_%d"), mutexName != NULL ? mutexName : _T(""), messageId);
}

inline void BuildPictViewMessageEventName(TCHAR* dest, int destSize, const TCHAR* mutexName, DWORD messageId)
{
    if (dest == NULL || destSize <= 0)
        return;

    _sntprintf_s(dest, destSize, _TRUNCATE, _T("%s_ev%x"), mutexName != NULL ? mutexName : _T(""), messageId);
}

inline void BuildPictViewImageMapName(char* dest, int destSize, const char* mutexName, DWORD imageMapId)
{
    if (dest == NULL || destSize <= 0)
        return;

    _snprintf_s(dest, destSize, _TRUNCATE, "%s_img%d", mutexName != NULL ? mutexName : "", imageMapId);
}

#ifdef UNICODE
inline void BuildPictViewImageMapName(char* dest, int destSize, const wchar_t* mutexName, DWORD imageMapId)
{
    char narrowMutexName[64];
    narrowMutexName[0] = 0;
    if (mutexName != NULL)
        // [narrow-ok: ascii-by-construction] an IPC mutex name this plugin composes itself
        WideCharToMultiByte(CP_ACP, 0, mutexName, -1, narrowMutexName, (int)sizeof(narrowMutexName), NULL, NULL);

    BuildPictViewImageMapName(dest, destSize, narrowMutexName, imageMapId);
}
#endif
