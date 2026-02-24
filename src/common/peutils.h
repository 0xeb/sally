// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <windows.h>

// Returns TRUE if the DLL at dllPath exports "SalamanderPluginEntry".
// Uses DONT_RESOLVE_DLL_REFERENCES: maps the PE image without calling
// DllMain or loading dependencies.
BOOL DllExportsSalamanderEntry(const wchar_t* dllPath);
