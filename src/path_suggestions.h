// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// Folder suggestions while a path is typed: Windows' autocomplete drop-down on the edit control of
// combobox (or edit) 'controlId' in 'dialog', fed with the subfolders of the folder typed so far.
// A relative path is resolved against the active panel's folder, as F5/F6 and Shift+F7 resolve it.
// Does nothing when Configuration.SuggestPathNames is off. Call in WM_INITDIALOG before
// CreateKeyForwarder subclasses the edit, so the autocomplete's own subclass stays underneath.
void AttachPathSuggestions(HWND dialog, int controlId);
