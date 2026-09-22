// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

// #101 diagnostic. Alt+drag is reported as still showing the "blocked" cursor even though the
// drag source advertises DROPEFFECT_LINK (shellsup.cpp, saLeftDragFiles) and the modifier
// mapping in ComputeOwnFolderDropEffect() is correct. Reading alone cannot say WHICH of the
// three branches in CImpDropTarget::DragOver() runs, nor whether OLE actually delivers MK_ALT
// in grfKeyState - so this records it instead of guessing again.
//
// Entirely opt-in and off by default: nothing is opened, created or written unless the
// environment variable SALLY_DRAGDROP_LOG is set. Set it to a file path, or to "1" to use
// %TEMP%\sally-dragdrop.log.
//
// Lines are emitted only when the observed state CHANGES, because DragOver fires on every
// mouse move and an unfiltered log is unreadable.

bool DragDropDiagEnabled();

// Records one drag-over decision. 'branch' names the branch actually taken.
void DragDropDiagRecord(const char* branch, DWORD keyState, DWORD effectIn, DWORD effectOut,
                        int tgtType, bool haveShellTarget, bool ownFolderDrop, bool tgtFile);

// Records, once per drag, the target path and every clipboard format the dragged data object
// actually offers.
//
// Why this matters: the log showed the shell drop target returning DROPEFFECT_NONE for Alt over
// a FOLDER while returning COPY|LINK for the same key state over a FILE. Same data object, same
// allowed effects - so the difference is what the folder target needs in order to offer LINK.
// Issue #87 already taught this codebase that the shell shortcut path wants richer formats than
// CF_HDROP (it was fixed there with a PIDL-backed data object); this checks whether the DRAG
// data object has the same gap.
void DragDropDiagDataObject(IDataObject* dataObject, const wchar_t* curDir);
