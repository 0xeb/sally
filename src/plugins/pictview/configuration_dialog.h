// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include "viewer.h"

namespace pictview
{

// PictView Configuration: the Appearance, Colors, Keyboard & Mouse, Tools and Advanced pages
// (DLG_CFGPAGE_*) as a Sally property sheet. Returns true when the user chose OK; 'preferences'
// then holds the edited values.
bool PromptViewerPreferences(HWND parent, HINSTANCE instance, ViewerPreferences& preferences);

} // namespace pictview
