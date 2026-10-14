// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

class IFileEnumerator;

// Folder suggestions while a path is typed (F5/F6 targets, Shift+F7): the part typed up to the
// last '\' or '/' names a folder; its subfolders are offered as completions of what follows.
// A relative path is resolved against the source panel's folder, as F5/F6 resolve their target.
namespace Sally::PathCompletion
{

struct Target
{
    bool Complete = false;   // false: offer nothing (not a disk path, or nothing to list)
    std::wstring Directory;  // the folder to list
    std::wstring Head;       // the typed text up to and including the last separator
};

// 'typedHead' is what Windows' autocomplete passes as the part to expand: the typed text up to and
// including its last separator ("" when none was typed). 'baseDirectory' is the source panel's
// folder ("" when the panel is not a disk folder).
// Nothing is offered for plugin or archive paths ("ftp://", "C:\a.zip\..." simply lists nothing),
// a server root ("\\server\"), a drive-relative form ("C:dir\") or an invalid character.
Target Resolve(const std::wstring& typedHead, const std::wstring& baseDirectory);

// Head + name for each name, sorted without regard to case, "." and ".." left out, at most
// 'maxCount'.
std::vector<std::wstring> Candidates(const std::wstring& head, std::vector<std::wstring> names, size_t maxCount);

// The subfolders of target.Directory as completions (Head + name), through 'enumerator'.
std::vector<std::wstring> ListFolderCandidates(IFileEnumerator& enumerator, const Target& target, size_t maxCount);

constexpr size_t MaxSuggestions = 1000;

} // namespace Sally::PathCompletion
