// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class IFileEnumerator;

// Path suggestions while a path is typed (F5/F6 targets, Shift+F7, F7): the part typed up to the
// last '\' or '/' names a folder; its entries are offered as completions of what follows.
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

enum class CandidateKinds
{
    Folders,         // destinations of several items, folders, Shift+F7, F7
    FoldersAndFiles, // Copy/Move of one disk file: an existing file name may be the new name
};

struct CandidateOptions
{
    CandidateKinds Kinds = CandidateKinds::Folders;
    // New-directory dialogs offer existing folders as parents: accepting one leaves a trailing
    // separator so the user can type a new leaf. Files never receive a separator.
    bool AppendFolderSeparator = false;
    // Files for which this returns true are not offered (the name alone, without the head).
    // Called on the caller's thread; it must not touch state the UI thread can change.
    std::function<bool(const std::wstring& fileName)> ExcludeFile;
};

// The entries of target.Directory as completions (Head + name), through 'enumerator': folders
// first, then files when the options ask for them, each group sorted without regard to case.
// Each group has its own cap of 'maxCount', so a folder holding many files still offers its
// subfolders, and a huge folder is never read to the end.
std::vector<std::wstring> ListCandidates(IFileEnumerator& enumerator, const Target& target, size_t maxCount,
                                         const CandidateOptions& options = {});

// Which names a Copy/Move target offers: files as well only when exactly one item is copied or
// moved, that item is a file, and the source is a disk folder.
CandidateKinds SuggestionKindsFor(size_t operationItemCount, bool singleItemIsFile, bool sourceIsDisk);

constexpr size_t MaxSuggestions = 1000;

} // namespace Sally::PathCompletion
