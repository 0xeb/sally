// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class CMaskGroup;

// A top-level User Menu command (or submenu) can be shown only when the active panel
// matches its rule. The rule has two optional parts, both Sally masks; when both are given, both
// must match:
//   - the focused-file mask, against the name of the focused ordinary file (a folder, ".." or
//     an empty panel never matches it);
//   - the folder mask, against each folder name of the active panel's path ("src" matches
//     D:\Projects\app\src).
// A command whose rule does not match is left out of the User Menu bar and the F9 User Menu.
// Each command decides only for itself, so rules cannot conflict. This module has no UI: the
// main window captures a Context, the bar and the menu ask which entries to show.
namespace Sally::UserMenuConditions
{

enum class PanelKind
{
    Unknown, // no panel to describe (start-up, a host without panels): nothing is filtered
    Disk,
    Archive,
    PluginFS,
};

enum class FocusKind
{
    None,   // empty panel, nothing focused
    File,
    Folder,
    Parent, // ".."
};

struct Context
{
    PanelKind Panel = PanelKind::Unknown;
    std::wstring Path;      // the active panel's path (disk panels)
    FocusKind Focus = FocusKind::None;
    std::wstring FocusName; // the focused item's name, no path

    bool operator==(const Context& other) const
    {
        return Panel == other.Panel && Focus == other.Focus && Path == other.Path && FocusName == other.FocusName;
    }
    bool operator!=(const Context& other) const { return !(*this == other); }
};

struct Rule
{
    bool Enabled = false;    // "Show only when" ticked; unticked keeps the text but always shows
    std::wstring FileMask;   // "Focused file matches:"
    std::wstring FolderMask; // "In folder named:"

    bool operator==(const Rule& other) const
    {
        return Enabled == other.Enabled && FileMask == other.FileMask && FolderMask == other.FolderMask;
    }
    bool operator!=(const Rule& other) const { return !(*this == other); }
};

enum class RuleField
{
    None,
    FileMask,
    FolderMask,
};

struct RuleError
{
    RuleField Field = RuleField::None;
    int Position = 0; // index of the offending character in that field
};

// True when the rule restricts its command: ticked, and at least one mask is not blank.
// Ticked with both masks blank means "always", like an unticked rule.
bool IsActive(const Rule& rule);

// False for a malformed mask, with the field and character position. The Configuration page
// checks with this before it stores a rule; the bar and the menu prepare with the same code.
bool Validate(const Rule& rule, RuleError& error);

// The folder names of a path as the folder rule sees them: no drive ("C:"), no UNC server.
std::vector<std::wstring> FolderNames(const std::wstring& path);

// A rule with its masks prepared. Copies prepare their own masks; prepared masks are never
// shared, so a copy cannot release another object's masks.
class PreparedRule
{
public:
    PreparedRule();
    ~PreparedRule();
    PreparedRule(const PreparedRule& other);
    PreparedRule& operator=(const PreparedRule& other);

    bool Prepare(const Rule& rule, RuleError* error = nullptr);
    const Rule& Source() const { return SourceRule; }

    // An inactive rule, or one whose masks do not prepare (a hand-edited profile), shows its
    // command: a malformed rule never hides a command silently. An active rule hides it on
    // archive and plugin panels, and otherwise shows it only when every given part matches.
    bool IsVisible(const Context& context) const;

private:
    Rule SourceRule;
    bool Active = false;
    std::unique_ptr<CMaskGroup> File;
    std::unique_ptr<CMaskGroup> Folder;
};

// Each command keeps one of these: the masks are prepared again only when the rule's text
// changes. A copied command starts with an empty cache.
class RuleCache
{
public:
    RuleCache() = default;
    RuleCache(const RuleCache&) {}
    RuleCache& operator=(const RuleCache&)
    {
        Prepared.reset();
        return *this;
    }

    bool IsVisible(const Rule& rule, const Context& context);

private:
    std::unique_ptr<PreparedRule> Prepared;
};

enum class EntryKind
{
    Command,
    SubmenuBegin,
    SubmenuEnd,
    Separator,
};

struct Entry
{
    EntryKind Kind = EntryKind::Command;
    bool RuleHides = false; // this entry's own rule does not match (only top-level rules count)
    bool OnToolbar = false; // "Show item in User Menu bar"
};

// F9: whether each entry appears. A hidden submenu takes its whole block (to its closing marker,
// or to the end when it has none). A top-level separator goes with the commands it separated when
// they were all hidden; once anything at the top level is hidden, no separator is left leading,
// trailing or next to another. Nothing hidden: the menu comes out exactly as before.
std::vector<bool> MenuEntriesShown(const std::vector<Entry>& entries);

// The User Menu bar: the indexes of the top-level entries it shows, in order (separators too),
// with the same separator rule among the entries placed on the bar.
std::vector<size_t> ToolbarEntries(const std::vector<Entry>& entries);

} // namespace Sally::UserMenuConditions
