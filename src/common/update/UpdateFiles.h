// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Replaces the files of a Sally folder with those of a new release, all or nothing.
//
// Every file of the release is copied over the installed one. An installed file is not
// overwritten: it is renamed aside to "<name>.sally-old" first, which Windows allows even
// while the file is loaded (the shell extension stays loaded in Explorer, issue #128), and
// the new file takes its name. Files that the release does not contain (configuration,
// user files) are left alone.
//
// Each step is written to a journal in the folder before it is taken. When a step fails,
// the steps taken so far are undone in reverse order; a journal left behind by an
// interrupted update is undone the same way by the next run. Once every file is in place
// the renamed-aside files are deleted; one that is still loaded stays until a later
// CleanUpdateLeftovers can delete it.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace Sally::Update
{

extern const wchar_t* const kJournalFileName; // in the install folder
extern const wchar_t* const kLeftoverSuffix;  // ".sally-old"
// In the install folder: the renamed-aside files that were still in use when an update finished.
extern const wchar_t* const kLeftoverListFileName;

enum class PathKind
{
    Missing,
    File,
    Folder,
};

// The file system as the update sees it; paths are absolute. Win32UpdateFileOps
// (common/Win32UpdateFileOps.h) is the real one.
class IUpdateFileOps
{
public:
    virtual ~IUpdateFileOps() = default;
    virtual PathKind Query(const std::wstring& path) = 0;
    // Every file and folder below root, relative to it, parents before children.
    virtual bool List(const std::wstring& root, std::vector<std::wstring>& files,
                      std::vector<std::wstring>& folders, DWORD& error) = 0;
    virtual bool Rename(const std::wstring& from, const std::wstring& to, DWORD& error) = 0; // never replaces
    virtual bool Copy(const std::wstring& from, const std::wstring& to, DWORD& error) = 0;   // never replaces
    virtual bool RemoveFile(const std::wstring& path, DWORD& error) = 0;
    virtual bool CreateFolder(const std::wstring& path, DWORD& error) = 0;
    virtual bool RemoveFolder(const std::wstring& path, DWORD& error) = 0;
    // The journal: appended to and flushed line by line, read whole, removed.
    virtual bool AppendLine(const std::wstring& path, const std::string& utf8Line, DWORD& error) = 0;
    virtual bool ReadText(const std::wstring& path, std::string& utf8, DWORD& error) = 0;
};

struct UpdatePlan
{
    std::wstring SourceRoot;  // the extracted release
    std::wstring InstallRoot; // the Sally folder
    std::vector<std::wstring> Folders; // relative; only those the install lacks, parents first
    std::vector<std::wstring> Files;   // relative; every file of the release
    // relative; installed files the release retired (its obsolete-files manifest), renamed aside
    // like replaced files, so that a failed update brings them back
    std::vector<std::wstring> Removals;
    std::vector<std::wstring> EmptyFolders; // relative; removed after the update when empty, deepest first
};

struct UpdateFailure
{
    std::wstring Path;
    DWORD Code = 0;
    std::wstring What; // English, for the log and the result file
};

bool BuildUpdatePlan(const std::wstring& sourceRoot, const std::wstring& installRoot, IUpdateFileOps& ops,
                     UpdatePlan& plan, UpdateFailure& failure);

struct ApplyOutcome
{
    bool Ok = false;
    bool RolledBack = false;     // a failure was undone ...
    bool RollbackComplete = true; // ... completely
    UpdateFailure Failure;
    std::vector<std::wstring> Leftovers; // renamed-aside files that could not be deleted yet
};

ApplyOutcome ApplyUpdate(const UpdatePlan& plan, IUpdateFileOps& ops);

// Undoes the steps of a journal left in installRoot by an interrupted update. True when there
// was nothing to undo or everything was undone; the journal is then removed.
bool RecoverInterruptedUpdate(const std::wstring& installRoot, IUpdateFileOps& ops, UpdateFailure& failure);

// --- files a release retired ---------------------------------------------------------------------
//
// A release lists the files earlier releases installed and it no longer has (a retired plugin)
// in kObsoleteManifestName at its root: the line "SALLY-UPDATE-OBSOLETE 1", then one path per
// line relative to the Sally folder; a path ending with a backslash is a folder, removed once it
// is empty. The list only grows, so any release removes what every earlier one retired. Only
// paths below plugins\ and utils\ are accepted, and never one the release itself contains.

extern const wchar_t* const kObsoleteManifestName;

struct ObsoleteManifest
{
    std::vector<std::wstring> Files;
    std::vector<std::wstring> Folders; // without the trailing backslash
};

// A relative path inside plugins\ or utils\, with no "..", drive, wildcard or empty part.
bool IsRetirablePath(const std::wstring& relative);
// One bad line refuses the whole manifest.
bool ParseObsoleteManifest(const std::string& utf8, ObsoleteManifest& manifest, std::string& error);
// The manifest at root\kObsoleteManifestName; false when there is none or it is refused.
bool ReadObsoleteManifest(const std::wstring& root, IUpdateFileOps& ops, ObsoleteManifest& manifest);

struct ObsoleteSweep
{
    int Removed = 0;
    std::vector<std::wstring> StillInUse; // renamed aside and listed as leftovers
    std::vector<UpdateFailure> Failures;  // left in place (no access)
};

// Removes the retired files of an installed Sally, for an update installed by an updater that did
// not know the manifest yet. Nothing needs undoing, so there is no journal: each file is renamed
// aside and deleted, or listed as a leftover while it is still loaded; a later start finishes.
// Does nothing while a journal shows an update in progress. Cheap when everything is gone.
ObsoleteSweep RemoveObsoleteFiles(const std::wstring& installRoot, const ObsoleteManifest& manifest,
                                  IUpdateFileOps& ops);

// Deletes the renamed-aside files an update listed as still in use; returns those that still
// are. A file that is renamed aside while an earlier one of the same name is still held gets the
// next free name (<name>.1.sally-old, .2., ...), so repeated updates never collide. Cheap when there is nothing to do (one file check), so Sally calls it at every start.
// Does nothing while a journal shows an update in progress.
std::vector<std::wstring> CleanUpdateLeftovers(const std::wstring& installRoot, IUpdateFileOps& ops);

} // namespace Sally::Update
