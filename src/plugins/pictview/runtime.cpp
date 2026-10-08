// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Globals the shared plugin sources (trace, WinLib) expect every plugin to define. The plugin
// entry sets them from the host.

#include "precomp.h"

CSalamanderDebugAbstract* SalamanderDebug = nullptr;
int SalamanderVersion = 0;
