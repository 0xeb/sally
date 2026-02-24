// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// Tiny DLL that exports SalamanderPluginEntry (stub) for testing.

#include <windows.h>

extern "C" __declspec(dllexport) void* __stdcall SalamanderPluginEntry(void* s)
{
    return NULL;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
