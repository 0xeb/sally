// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "common/UserMenuConditions.h"

#include "masks.h"

namespace Sally::UserMenuConditions
{
namespace
{

bool IsSeparator(wchar_t c)
{
    return c == L'\\' || c == L'/';
}

bool IsBlank(const std::wstring& text)
{
    for (wchar_t c : text)
    {
        if (c != L' ' && c != L'\t')
            return false;
    }
    return true;
}

// Prepares one non-blank part of a rule; blank parts stay absent.
bool PreparePart(const std::wstring& text, RuleField field, std::unique_ptr<CMaskGroup>& out, RuleError* error)
{
    out.reset();
    if (IsBlank(text))
        return true;
    // Separators alone (";", " | ") prepare but name nothing: such a rule would hide its
    // command for good, so it is reported like any other malformed mask.
    if (text.find_first_not_of(L" \t;|") == std::wstring::npos)
    {
        if (error != nullptr)
        {
            error->Field = field;
            error->Position = 0;
        }
        return false;
    }
    auto group = std::make_unique<CMaskGroup>();
    group->SetMasksString(text.c_str());
    int position = 0;
    if (!group->PrepareMasks(position))
    {
        if (error != nullptr)
        {
            error->Field = field;
            error->Position = position;
        }
        return false;
    }
    out = std::move(group);
    return true;
}

struct Unit
{
    size_t Index;
    bool Separator;
    bool Hidden;
};

// Separators split a level into groups. A group that had entries and lost all of them to rules
// takes one separator with it: the one before it, or (first group) the one after it. A level
// where nothing is hidden, and so a menu without rules, is left exactly as it was; once
// something is hidden, separators left leading, trailing or next to each other also go.
std::vector<size_t> KeptAfterHiding(const std::vector<Unit>& units)
{
    bool anyHidden = false;
    for (const Unit& unit : units)
        anyHidden = anyHidden || unit.Hidden;
    std::vector<size_t> all;
    if (!anyHidden)
    {
        for (const Unit& unit : units)
            all.push_back(unit.Index);
        return all;
    }
    std::vector<std::vector<size_t>> groups(1);
    std::vector<size_t> separators;
    for (size_t u = 0; u < units.size(); u++)
    {
        if (units[u].Separator)
        {
            separators.push_back(u);
            groups.emplace_back();
        }
        else
            groups.back().push_back(u);
    }
    std::vector<bool> keepSeparator(separators.size(), true);
    for (size_t g = 0; g < groups.size(); g++)
    {
        const std::vector<size_t>& members = groups[g];
        bool vanished = !members.empty();
        for (size_t member : members)
        {
            if (!units[member].Hidden)
            {
                vanished = false;
                break;
            }
        }
        if (!vanished)
            continue;
        if (g > 0 && keepSeparator[g - 1])
            keepSeparator[g - 1] = false;
        else if (g < separators.size() && keepSeparator[g])
            keepSeparator[g] = false;
    }
    std::vector<size_t> kept;
    std::vector<bool> keptIsSeparator;
    size_t separator = 0;
    for (const Unit& unit : units)
    {
        if (unit.Separator)
        {
            if (keepSeparator[separator++] && !kept.empty() && !keptIsSeparator.back())
            {
                kept.push_back(unit.Index);
                keptIsSeparator.push_back(true);
            }
        }
        else if (!unit.Hidden)
        {
            kept.push_back(unit.Index);
            keptIsSeparator.push_back(false);
        }
    }
    while (!kept.empty() && keptIsSeparator.back())
    {
        kept.pop_back();
        keptIsSeparator.pop_back();
    }
    return kept;
}

} // namespace

bool IsActive(const Rule& rule)
{
    return rule.Enabled && (!IsBlank(rule.FileMask) || !IsBlank(rule.FolderMask));
}

bool Validate(const Rule& rule, RuleError& error)
{
    error = RuleError{};
    PreparedRule prepared;
    return prepared.Prepare(rule, &error);
}

std::vector<std::wstring> FolderNames(const std::wstring& path)
{
    size_t i = 0;
    if (path.size() >= 2 && IsSeparator(path[0]) && IsSeparator(path[1]))
    {
        i = 2; // UNC: the server is not a folder
        while (i < path.size() && !IsSeparator(path[i]))
            i++;
    }
    else if (path.size() >= 2 && path[1] == L':')
        i = 2; // the drive is not a folder
    std::vector<std::wstring> names;
    while (i < path.size())
    {
        while (i < path.size() && IsSeparator(path[i]))
            i++;
        const size_t start = i;
        while (i < path.size() && !IsSeparator(path[i]))
            i++;
        if (i > start)
        {
            std::wstring name = path.substr(start, i - start);
            if (name != L"." && name != L"..")
                names.push_back(std::move(name));
        }
    }
    return names;
}

PreparedRule::PreparedRule() = default;
PreparedRule::~PreparedRule() = default;

PreparedRule::PreparedRule(const PreparedRule& other)
{
    Prepare(other.SourceRule);
}

PreparedRule& PreparedRule::operator=(const PreparedRule& other)
{
    if (this != &other)
        Prepare(other.SourceRule);
    return *this;
}

bool PreparedRule::Prepare(const Rule& rule, RuleError* error)
{
    SourceRule = rule;
    Active = false;
    File.reset();
    Folder.reset();
    if (error != nullptr)
        *error = RuleError{};
    if (!IsActive(rule))
        return true;
    if (!PreparePart(rule.FileMask, RuleField::FileMask, File, error) ||
        !PreparePart(rule.FolderMask, RuleField::FolderMask, Folder, error))
    {
        File.reset();
        Folder.reset();
        return false;
    }
    Active = true;
    return true;
}

bool PreparedRule::IsVisible(const Context& context) const
{
    if (!Active || context.Panel == PanelKind::Unknown)
        return true;
    if (context.Panel != PanelKind::Disk)
        return false; // a rule describes disk folders and files
    if (File)
    {
        if (context.Focus != FocusKind::File || context.FocusName.empty() ||
            !File->AgreeMasks(context.FocusName.c_str(), NULL))
            return false;
    }
    if (Folder)
    {
        bool found = false;
        for (const std::wstring& name : FolderNames(context.Path))
        {
            if (Folder->AgreeMasks(name.c_str(), NULL))
            {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

bool RuleCache::IsVisible(const Rule& rule, const Context& context)
{
    if (!IsActive(rule))
    {
        Prepared.reset();
        return true;
    }
    if (!Prepared || Prepared->Source() != rule)
    {
        auto prepared = std::make_unique<PreparedRule>();
        prepared->Prepare(rule);
        Prepared = std::move(prepared);
    }
    return Prepared->IsVisible(context);
}

std::vector<bool> MenuEntriesShown(const std::vector<Entry>& entries)
{
    std::vector<bool> shown(entries.size(), true);
    std::vector<Unit> units;
    int level = 0;
    bool blockHidden = false;
    for (size_t i = 0; i < entries.size(); i++)
    {
        const Entry& entry = entries[i];
        if (level == 0)
        {
            switch (entry.Kind)
            {
            case EntryKind::Command:
                units.push_back({i, false, entry.RuleHides});
                shown[i] = !entry.RuleHides;
                break;
            case EntryKind::Separator:
                units.push_back({i, true, false});
                break;
            case EntryKind::SubmenuBegin:
                units.push_back({i, false, entry.RuleHides});
                shown[i] = !entry.RuleHides;
                blockHidden = entry.RuleHides;
                level = 1;
                break;
            case EntryKind::SubmenuEnd:
                break; // an orphaned closing marker shows nothing either way
            }
            continue;
        }
        if (blockHidden)
            shown[i] = false;
        if (entry.Kind == EntryKind::SubmenuBegin)
            level++;
        else if (entry.Kind == EntryKind::SubmenuEnd && --level == 0)
            blockHidden = false;
    }
    std::vector<bool> keep(entries.size(), false);
    for (size_t index : KeptAfterHiding(units))
        keep[index] = true;
    for (const Unit& unit : units)
    {
        if (unit.Separator)
            shown[unit.Index] = keep[unit.Index];
    }
    return shown;
}

std::vector<size_t> ToolbarEntries(const std::vector<Entry>& entries)
{
    std::vector<Unit> units;
    int level = 0;
    for (size_t i = 0; i < entries.size(); i++)
    {
        const Entry& entry = entries[i];
        switch (entry.Kind)
        {
        case EntryKind::SubmenuEnd:
            if (level > 0)
                level--;
            break;
        case EntryKind::Separator:
            if (level == 0 && entry.OnToolbar)
                units.push_back({i, true, false});
            break;
        case EntryKind::Command:
        case EntryKind::SubmenuBegin:
            if (level == 0 && entry.OnToolbar)
                units.push_back({i, false, entry.RuleHides});
            if (entry.Kind == EntryKind::SubmenuBegin)
                level++;
            break;
        }
    }
    return KeptAfterHiding(units);
}

} // namespace Sally::UserMenuConditions
