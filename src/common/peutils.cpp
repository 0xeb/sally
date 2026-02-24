// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "peutils.h"

BOOL DllExportsSalamanderEntry(const wchar_t* dllPath)
{
    HMODULE hMod = LoadLibraryExW(dllPath, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (hMod == NULL)
        return FALSE;
    BOOL found = (GetProcAddress(hMod, "SalamanderPluginEntry") != NULL);
    FreeLibrary(hMod);
    return found;
}
