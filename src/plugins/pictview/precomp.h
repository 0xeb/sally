// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Common header for the shared plugin sources compiled into PictView (WinLib, trace, dark
// mode). PictView's own sources include what they use directly.

#pragma once

#include <tchar.h>
#include <windows.h>
#include <crtdbg.h>
#include <ostream>
#include <commctrl.h>
#include <commdlg.h>
#include <limits.h>
#include <stdio.h>

#include "versinfo.rh2"

#include "spl_com.h"
#include "spl_base.h"
#include "spl_gen.h"
#include "spl_menu.h"
#include "spl_view.h"
#include "spl_gui.h"
#include "spl_vers.h"
#include "dbg.h"
#include "arraylt.h"
#include "winliblt.h"
#include "mhandles.h"
