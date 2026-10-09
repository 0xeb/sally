// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "update/UpdateFiles.h"

#include "Win32TextCodec.h"

#include <algorithm>

namespace Sally::Update
{

const wchar_t* const kJournalFileName = L"sally-update.journal";
const wchar_t* const kLeftoverSuffix = L".sally-old";
const wchar_t* const kLeftoverListFileName = L"sally-update.leftovers";

namespace
{

const char* const kJournalHeader = "SALLY-UPDATE 1";
const char* const kJournalCommit = "COMMIT";
const char* const kLeftoverListHeader = "SALLY-UPDATE-LEFTOVERS 1";

std::wstring Join(const std::wstring& root, const std::wstring& relative)
{
    if (relative.empty())
        return root;
    if (!root.empty() && root.back() == L'\\')
        return root + relative;
    return root + L"\\" + relative;
}

std::string ToUtf8(const std::wstring& text)
{
    std::string utf8;
    Win32EncodeText(CP_UTF8, text, utf8);
    return utf8;
}

std::wstring FromUtf8(const std::string& text)
{
    std::wstring wide;
    Win32DecodeText(CP_UTF8, text, wide);
    return wide;
}

bool EndsWithNoCase(const std::wstring& text, const std::wstring& suffix)
{
    return text.size() >= suffix.size() &&
           CompareStringOrdinal(text.data() + text.size() - suffix.size(), static_cast<int>(suffix.size()),
                                suffix.data(), static_cast<int>(suffix.size()), TRUE) == CSTR_EQUAL;
}

// The renamed-aside files a finished update could not delete, relative to the install folder.
// Only names that end in the leftover suffix and stay inside the folder are taken.
std::vector<std::wstring> ReadLeftoverList(const std::wstring& list, IUpdateFileOps& ops)
{
    std::vector<std::wstring> leftovers;
    std::string text;
    DWORD error = 0;
    if (ops.Query(list) != PathKind::File || !ops.ReadText(list, text, error))
        return leftovers;
    size_t start = 0;
    bool first = true;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        if (first)
        {
            if (line != kLeftoverListHeader)
                return std::vector<std::wstring>();
            first = false;
            continue;
        }
        const std::wstring leftover = FromUtf8(line);
        if (EndsWithNoCase(leftover, kLeftoverSuffix) && leftover.find(L"..") == std::wstring::npos &&
            leftover.find(L':') == std::wstring::npos && leftover[0] != L'\\' && leftover[0] != L'/')
            leftovers.push_back(leftover);
    }
    return leftovers;
}

// One step of the journal: F (folder created), N (new file copied in), R (installed file
// renamed aside to Backup, new file copied in).
struct Step
{
    char Kind = 0;
    std::wstring Relative;
    std::wstring Backup; // relative, R only
};

std::string FormatStep(const Step& step)
{
    std::string line(1, step.Kind);
    line += '\t';
    line += ToUtf8(step.Relative);
    if (step.Kind == 'R')
    {
        line += '\t';
        line += ToUtf8(step.Backup);
    }
    return line;
}

bool ParseJournal(const std::string& text, std::vector<Step>& steps, bool& committed)
{
    steps.clear();
    committed = false;
    size_t start = 0;
    bool first = true;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        if (first)
        {
            if (line != kJournalHeader)
                return false;
            first = false;
            continue;
        }
        if (line == kJournalCommit)
        {
            committed = true;
            continue;
        }
        if (line.size() < 3 || line[1] != '\t' || (line[0] != 'F' && line[0] != 'N' && line[0] != 'R'))
            return false;
        Step step;
        step.Kind = line[0];
        const std::string rest = line.substr(2);
        if (step.Kind == 'R')
        {
            const size_t tab = rest.find('\t');
            if (tab == std::string::npos)
                return false;
            step.Relative = FromUtf8(rest.substr(0, tab));
            step.Backup = FromUtf8(rest.substr(tab + 1));
        }
        else
        {
            step.Relative = FromUtf8(rest);
        }
        if (step.Relative.empty() || step.Relative.find(L"..") != std::wstring::npos ||
            step.Relative.find(L':') != std::wstring::npos || step.Relative[0] == L'\\')
            return false;
        steps.push_back(step);
    }
    return !first;
}

bool UndoStep(const std::wstring& installRoot, const Step& step, IUpdateFileOps& ops, UpdateFailure& failure)
{
    const std::wstring target = Join(installRoot, step.Relative);
    DWORD error = 0;
    switch (step.Kind)
    {
    case 'F':
        if (ops.Query(target) == PathKind::Folder && !ops.RemoveFolder(target, error))
        {
            failure = {target, error, L"Cannot remove the new folder"};
            return false;
        }
        return true;
    case 'N':
        if (ops.Query(target) == PathKind::File && !ops.RemoveFile(target, error))
        {
            failure = {target, error, L"Cannot remove the new file"};
            return false;
        }
        return true;
    case 'R':
    {
        const std::wstring backup = Join(installRoot, step.Backup);
        if (ops.Query(backup) != PathKind::File)
            return true; // the installed file was never renamed aside
        if (ops.Query(target) == PathKind::File && !ops.RemoveFile(target, error))
        {
            failure = {target, error, L"Cannot remove the new file"};
            return false;
        }
        if (!ops.Rename(backup, target, error))
        {
            failure = {target, error, L"Cannot restore the previous file"};
            return false;
        }
        return true;
    }
    }
    return true;
}

bool UndoSteps(const std::wstring& installRoot, const std::vector<Step>& steps, IUpdateFileOps& ops,
               UpdateFailure& failure)
{
    bool complete = true;
    for (auto it = steps.rbegin(); it != steps.rend(); ++it)
    {
        UpdateFailure stepFailure;
        if (!UndoStep(installRoot, *it, ops, stepFailure))
        {
            if (complete)
                failure = stepFailure;
            complete = false;
        }
    }
    return complete;
}

std::wstring ChooseBackupName(const std::wstring& installRoot, const std::wstring& relative, IUpdateFileOps& ops)
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const std::wstring candidate =
            attempt == 0 ? relative + kLeftoverSuffix
                         : relative + L"." + std::to_wstring(attempt) + kLeftoverSuffix;
        const std::wstring path = Join(installRoot, candidate);
        const PathKind kind = ops.Query(path);
        if (kind == PathKind::Missing)
            return candidate;
        DWORD error = 0;
        if (kind == PathKind::File && ops.RemoveFile(path, error)) // a leftover that is no longer in use
            return candidate;
    }
    return std::wstring();
}

} // namespace

// ---------------------------------------------------------------------------------------------

bool BuildUpdatePlan(const std::wstring& sourceRoot, const std::wstring& installRoot, IUpdateFileOps& ops,
                     UpdatePlan& plan, UpdateFailure& failure)
{
    plan = UpdatePlan();
    plan.SourceRoot = sourceRoot;
    plan.InstallRoot = installRoot;
    if (ops.Query(installRoot) != PathKind::Folder)
    {
        failure = {installRoot, ERROR_PATH_NOT_FOUND, L"The Sally folder does not exist"};
        return false;
    }
    std::vector<std::wstring> files;
    std::vector<std::wstring> folders;
    DWORD error = 0;
    if (!ops.List(sourceRoot, files, folders, error))
    {
        failure = {sourceRoot, error, L"Cannot read the new version's files"};
        return false;
    }
    if (files.empty())
    {
        failure = {sourceRoot, ERROR_NO_MORE_FILES, L"The new version has no files"};
        return false;
    }
    for (const std::wstring& folder : folders)
    {
        const std::wstring target = Join(installRoot, folder);
        const PathKind kind = ops.Query(target);
        if (kind == PathKind::File)
        {
            failure = {target, ERROR_ALREADY_EXISTS, L"A file is in the way of a new folder"};
            return false;
        }
        if (kind == PathKind::Missing)
            plan.Folders.push_back(folder);
    }
    for (const std::wstring& file : files)
    {
        if (EndsWithNoCase(file, kLeftoverSuffix) ||
            CompareStringOrdinal(file.c_str(), -1, kJournalFileName, -1, TRUE) == CSTR_EQUAL ||
            CompareStringOrdinal(file.c_str(), -1, kLeftoverListFileName, -1, TRUE) == CSTR_EQUAL)
        {
            failure = {file, ERROR_INVALID_NAME, L"The new version contains a reserved file name"};
            return false;
        }
        const std::wstring target = Join(installRoot, file);
        if (ops.Query(target) == PathKind::Folder)
        {
            failure = {target, ERROR_ALREADY_EXISTS, L"A folder is in the way of a new file"};
            return false;
        }
        plan.Files.push_back(file);
    }
    return true;
}

ApplyOutcome ApplyUpdate(const UpdatePlan& plan, IUpdateFileOps& ops)
{
    ApplyOutcome outcome;
    const std::wstring journal = Join(plan.InstallRoot, kJournalFileName);
    if (ops.Query(journal) == PathKind::File && !RecoverInterruptedUpdate(plan.InstallRoot, ops, outcome.Failure))
        return outcome;

    std::vector<Step> steps;
    DWORD error = 0;
    auto fail = [&](const std::wstring& path, DWORD code, const wchar_t* what) {
        outcome.Failure = {path, code, what};
        outcome.RolledBack = true;
        UpdateFailure undoFailure;
        outcome.RollbackComplete = UndoSteps(plan.InstallRoot, steps, ops, undoFailure);
        DWORD ignored = 0;
        if (outcome.RollbackComplete)
            ops.RemoveFile(journal, ignored);
        return outcome;
    };
    auto record = [&](const Step& step) {
        if (!ops.AppendLine(journal, FormatStep(step), error))
            return false;
        steps.push_back(step);
        return true;
    };

    if (!ops.AppendLine(journal, kJournalHeader, error))
    {
        outcome.Failure = {journal, error, L"Cannot write to the Sally folder"};
        return outcome;
    }
    for (const std::wstring& folder : plan.Folders)
    {
        const std::wstring target = Join(plan.InstallRoot, folder);
        Step step;
        step.Kind = 'F';
        step.Relative = folder;
        if (!record(step))
            return fail(journal, error, L"Cannot write to the Sally folder");
        if (!ops.CreateFolder(target, error))
            return fail(target, error, L"Cannot create a folder");
    }
    for (const std::wstring& file : plan.Files)
    {
        const std::wstring target = Join(plan.InstallRoot, file);
        Step step;
        step.Relative = file;
        const PathKind kind = ops.Query(target);
        if (kind == PathKind::Folder)
            return fail(target, ERROR_ALREADY_EXISTS, L"A folder is in the way of a new file");
        if (kind == PathKind::File)
        {
            step.Kind = 'R';
            step.Backup = ChooseBackupName(plan.InstallRoot, file, ops);
            if (step.Backup.empty())
                return fail(target, ERROR_ALREADY_EXISTS, L"Cannot rename the installed file aside");
            if (!record(step))
                return fail(journal, error, L"Cannot write to the Sally folder");
            if (!ops.Rename(target, Join(plan.InstallRoot, step.Backup), error))
                return fail(target, error, L"Cannot rename the installed file aside");
        }
        else
        {
            step.Kind = 'N';
            if (!record(step))
                return fail(journal, error, L"Cannot write to the Sally folder");
        }
        if (!ops.Copy(Join(plan.SourceRoot, file), target, error))
            return fail(target, error, L"Cannot copy the new file");
    }
    if (!ops.AppendLine(journal, kJournalCommit, error))
        return fail(journal, error, L"Cannot write to the Sally folder");

    outcome.Ok = true;
    std::vector<std::wstring> leftovers;
    for (const Step& step : steps)
    {
        if (step.Kind != 'R')
            continue;
        const std::wstring backup = Join(plan.InstallRoot, step.Backup);
        DWORD ignored = 0;
        if (!ops.RemoveFile(backup, ignored))
        {
            outcome.Leftovers.push_back(backup);
            leftovers.push_back(step.Backup);
        }
    }
    // The files still in use are listed, so that a later start can delete just those.
    if (!leftovers.empty())
    {
        const std::wstring list = Join(plan.InstallRoot, kLeftoverListFileName);
        std::vector<std::wstring> listed = ReadLeftoverList(list, ops);
        if (listed.empty())
            ops.AppendLine(list, kLeftoverListHeader, error);
        for (const std::wstring& leftover : leftovers)
        {
            if (std::find(listed.begin(), listed.end(), leftover) == listed.end())
                ops.AppendLine(list, ToUtf8(leftover), error);
        }
    }
    DWORD ignored = 0;
    ops.RemoveFile(journal, ignored);
    return outcome;
}

bool RecoverInterruptedUpdate(const std::wstring& installRoot, IUpdateFileOps& ops, UpdateFailure& failure)
{
    const std::wstring journal = Join(installRoot, kJournalFileName);
    if (ops.Query(journal) != PathKind::File)
        return true;
    std::string text;
    DWORD error = 0;
    if (!ops.ReadText(journal, text, error))
    {
        failure = {journal, error, L"Cannot read the journal of an interrupted update"};
        return false;
    }
    std::vector<Step> steps;
    bool committed = false;
    if (!ParseJournal(text, steps, committed))
    {
        failure = {journal, ERROR_INVALID_DATA, L"The journal of an interrupted update is damaged"};
        return false;
    }
    if (committed)
    {
        // Every new file was in place; only the renamed-aside files may remain.
        for (const Step& step : steps)
        {
            if (step.Kind == 'R')
            {
                DWORD ignored = 0;
                ops.RemoveFile(Join(installRoot, step.Backup), ignored);
            }
        }
    }
    else if (!UndoSteps(installRoot, steps, ops, failure))
    {
        return false;
    }
    if (!ops.RemoveFile(journal, error))
    {
        failure = {journal, error, L"Cannot remove the journal of an interrupted update"};
        return false;
    }
    return true;
}

std::vector<std::wstring> CleanUpdateLeftovers(const std::wstring& installRoot, IUpdateFileOps& ops)
{
    std::vector<std::wstring> remaining;
    const std::wstring list = Join(installRoot, kLeftoverListFileName);
    if (ops.Query(list) != PathKind::File || ops.Query(Join(installRoot, kJournalFileName)) != PathKind::Missing)
        return remaining;
    std::vector<std::wstring> stillThere;
    for (const std::wstring& leftover : ReadLeftoverList(list, ops))
    {
        const std::wstring path = Join(installRoot, leftover);
        DWORD error = 0;
        if (ops.Query(path) == PathKind::File && !ops.RemoveFile(path, error))
        {
            remaining.push_back(path);
            stillThere.push_back(leftover);
        }
    }
    DWORD error = 0;
    ops.RemoveFile(list, error);
    if (!stillThere.empty())
    {
        ops.AppendLine(list, kLeftoverListHeader, error);
        for (const std::wstring& leftover : stillThere)
            ops.AppendLine(list, ToUtf8(leftover), error);
    }
    return remaining;
}

} // namespace Sally::Update
