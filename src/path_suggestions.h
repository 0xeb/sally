// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <optional>
#include <string>

#include "common/PathCompletion.h"

struct PathSuggestionOptions
{
    // Folders only, or folders and files (Copy/Move of one disk file). Windows' autocomplete
    // shows the entries sorted by name whatever order they are listed in (owner accepted,
    // 2026-11-03). Files that are archives Sally recognizes are never offered: a typed archive
    // name packs into it, which a suggestion must not trigger by surprise.
    Sally::PathCompletion::CandidateKinds Kinds = Sally::PathCompletion::CandidateKinds::Folders;
    // Existing folders are offered as parents for a new directory name (F7).
    bool AppendFolderSeparator = false;
    // While the drop-down is visible, Enter/Escape go to the autocomplete, so the first Enter
    // only accepts a suggestion (caret moved to the end) and the dialog stays open.
    bool AcceptOnlyWhileVisible = false;
    // The folder a relative path is resolved against; when absent, the active disk panel's path.
    std::optional<std::wstring> BaseDirectory;
    // A path inside an archive that is open in a panel offers that archive's folders
    // (only where the target can be inside an archive: F5/F6 from a disk folder, Shift+F7).
    bool ArchiveFolders = false;
};

// Path suggestions while a path is typed: Windows' autocomplete drop-down on the edit control of
// combobox (or edit) 'controlId' in 'dialog', fed with the entries of the folder typed so far.
// Does nothing when Configuration.SuggestPathNames is off. Call in WM_INITDIALOG before
// CreateKeyForwarder subclasses the edit, so the autocomplete's own subclass stays underneath.
void AttachPathSuggestions(HWND dialog, int controlId, const PathSuggestionOptions& options);

// Folder suggestions resolved against the active panel. appendSeparator offers existing folders
// as parents for a new directory name (F7) and keeps Enter/Escape in the edit while visible.
void AttachPathSuggestions(HWND dialog, int controlId, bool appendSeparator = false);

// The Pack (Alt+F5) and Unpack (Alt+F9) target fields: folders of the active disk panel's
// path, offered as parents (the archive name or the target folder is typed after them), and the
// first Enter only accepts a visible suggestion so a half-typed target never packs or unpacks.
inline PathSuggestionOptions PackTargetSuggestions()
{
    PathSuggestionOptions options;
    options.AppendFolderSeparator = true;
    options.AcceptOnlyWhileVisible = true;
    return options;
}
