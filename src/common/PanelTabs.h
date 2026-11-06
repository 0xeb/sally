// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Panel tabs: each panel keeps a set of tabs, and a tab is the saved state of the panel at a
// location (where it is, how it is viewed, what is focused). The one file panel per side shows the
// active tab; switching saves the panel into the outgoing tab and loads the incoming one through
// the normal path change. This module is the UI-free model: the records, the operations on the set,
// the tab titles and the registry form. The panel, its history and selection stay in the core.

#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sally::tabs
{

// The same numbering as CPathHistoryItem's type.
enum class LocationKind : int
{
    Disk = 0,
    Archive = 1,
    PluginFS = 2,
};

struct TabRecord
{
    std::uint32_t Id = 0; // unique within the session, never reused; assigned by the set
    LocationKind Kind = LocationKind::Disk;
    std::wstring Primary;   // disk path, archive file or plugin file system name
    std::wstring Secondary; // path inside the archive, or the file system's user part
    int ViewTemplate = 2;   // index of the panel's view template (2 = Detailed)
    int SortType = 0;
    bool ReverseSort = false;
    bool FilterEnabled = false;
    std::wstring Filter;
    std::wstring FocusedName;
    int TopIndex = 0;
    // Restored from the configuration and not yet shown in this session: its folder is read on
    // first activation (the strip shows such a tab dimmed).
    bool Dormant = false;
    std::wstring Title; // a name the user gave the tab; empty shows the location's name
    bool Locked = false; // a locked tab cannot be closed (it can still change folders)
};

class PanelTabSet
{
public:
    static constexpr std::size_t kMaxTabs = 32;
    static constexpr std::size_t kMaxClosed = 10;
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    // A set always holds at least one tab; the first one starts empty (the panel fills it).
    PanelTabSet();

    std::size_t Count() const { return Tabs.size(); }
    std::size_t Active() const { return ActiveIndex; }
    const TabRecord& At(std::size_t index) const { return Tabs[index]; }
    TabRecord& At(std::size_t index) { return Tabs[index]; }
    const TabRecord& ActiveTab() const { return Tabs[ActiveIndex]; }
    TabRecord& ActiveTab() { return Tabs[ActiveIndex]; }
    std::size_t IndexOf(std::uint32_t id) const;

    bool IsFull() const { return Tabs.size() >= kMaxTabs; }
    // Inserts a copy of 'record' (with a fresh id) right after the active tab, or after the last
    // tab with 'atEnd'; returns its index, or npos when the set is full. The active tab does not
    // change. A new tab is never locked.
    std::size_t InsertAfterActive(TabRecord record, bool atEnd = false);

    enum class CloseResult
    {
        Closed,
        LastTab,  // the only tab cannot be closed
        NotFound, // index out of range
        Locked,   // a locked tab stays open
    };
    // Closes the tab; it goes onto the reopen stack. When the active tab closes, the tab to its
    // right becomes active (or the one to its left when it was the last).
    CloseResult Close(std::size_t index);
    // Removes a tab that never opened (its folder could not be listed); it is not reopenable.
    CloseResult Discard(std::size_t index);
    // Closes every unlocked tab but 'index', which becomes active; returns how many were closed.
    std::size_t CloseOthers(std::size_t index);
    // Locks or unlocks a tab; returns false for a bad index.
    bool SetLocked(std::size_t index, bool locked);
    // Gives a tab its own name; an empty name goes back to the location's name.
    bool Rename(std::size_t index, const std::wstring& title);
    bool CanReopen() const { return !Closed.empty(); }
    // Reopens the most recently closed tab after the active one; returns its index or npos.
    std::size_t ReopenClosed();

    // Moves a tab; the active tab keeps being the same tab.
    bool Move(std::size_t from, std::size_t to);
    // Moves a tab to the gap before tab 'gap' (0..Count(); Count() is after the last tab), as a
    // drag on the strip drops it; returns the tab's new index or npos.
    std::size_t MoveToGap(std::size_t from, std::size_t gap);
    bool Activate(std::size_t index);
    std::size_t NextIndex() const { return (ActiveIndex + 1) % Tabs.size(); }
    std::size_t PrevIndex() const { return (ActiveIndex + Tabs.size() - 1) % Tabs.size(); }

    // Replaces the whole set (loading the configuration); an empty list keeps one empty tab.
    void Replace(std::vector<TabRecord> records, std::size_t active);
    const std::vector<TabRecord>& Records() const { return Tabs; }
    std::size_t ClosedCount() const { return Closed.size(); }

private:
    std::vector<TabRecord> Tabs;
    std::vector<TabRecord> Closed; // the most recent at the back
    std::size_t ActiveIndex = 0;
    std::uint32_t NextId = 1;

    std::uint32_t TakeId() { return NextId++; }
    CloseResult Erase(std::size_t index, bool remember);
};

// The text on a tab: the folder's name ("C:\" for a drive root, the share for "\\server\share"),
// "pack.zip" or "pack.zip > docs" inside an archive, and the last part of a plugin file system's
// path ("ftp: pub").
std::wstring DisplayTitle(const TabRecord& record);
// The full location for the tooltip and Copy Path.
std::wstring FullLocation(const TabRecord& record);

// The registry form of one tab: named DWORD and string values, written into its own numbered subkey
// ("1", "2", ...). Value names are the TabValue* constants.
struct TabValues
{
    std::map<std::wstring, DWORD> Numbers;
    std::map<std::wstring, std::wstring> Strings;
};

inline constexpr wchar_t kTabValueKind[] = L"Kind";
inline constexpr wchar_t kTabValuePath[] = L"Path W";
inline constexpr wchar_t kTabValueInner[] = L"Inner W";
inline constexpr wchar_t kTabValueView[] = L"View Type";
inline constexpr wchar_t kTabValueSort[] = L"Sort Type";
inline constexpr wchar_t kTabValueReverse[] = L"Reverse Sort";
inline constexpr wchar_t kTabValueFilterEnabled[] = L"Enable Filter";
inline constexpr wchar_t kTabValueFilter[] = L"Filter W";
inline constexpr wchar_t kTabValueFocus[] = L"Focus W";
inline constexpr wchar_t kTabValueTop[] = L"Top Index";
inline constexpr wchar_t kTabValueTitle[] = L"Title W";
inline constexpr wchar_t kTabValueLocked[] = L"Locked";
// The names Decode reads, for a loader that asks for each value by name.
inline constexpr const wchar_t* kTabNumberValues[] = {kTabValueKind, kTabValueView, kTabValueSort, kTabValueReverse,
                                                      kTabValueFilterEnabled, kTabValueTop, kTabValueLocked};
inline constexpr const wchar_t* kTabStringValues[] = {kTabValuePath, kTabValueInner, kTabValueFilter, kTabValueFocus,
                                                      kTabValueTitle};

TabValues Encode(const TabRecord& record);
// False for a record that cannot be restored (no path, unknown kind). The view index is clamped to
// [0, viewTemplateCount) and the sort type to [0, maxSortType]; the result is dormant.
bool Decode(const TabValues& values, int viewTemplateCount, int maxSortType, TabRecord& record);

// The tab strip's geometry: tab widths from their titles, shrinking to a minimum, then an overflow
// button listing every tab while only a window of tabs around the active one stays visible.
struct StripMetrics
{
    int Available = 0;    // width of the strip
    int MinWidth = 60;    // narrowest tab
    int MaxWidth = 220;   // widest tab
    int OverflowWidth = 20;
};

struct StripLayout
{
    struct Slot
    {
        bool Visible = false;
        int Left = 0;
        int Right = 0;
    };
    std::vector<Slot> Slots; // one per tab
    bool Overflow = false;   // some tabs are hidden; the overflow button lists all
    int OverflowLeft = 0;    // position of the overflow button when Overflow
};

// preferred: each tab's preferred width (title plus padding). The active tab is always visible.
StripLayout LayOutStrip(const std::vector<int>& preferred, std::size_t active, const StripMetrics& metrics);

// Where a tab dragged to x would be dropped: the gap before the visible tab whose middle is right
// of x, or after the last visible tab (a value for PanelTabSet::MoveToGap).
std::size_t DropGapAt(const StripLayout& layout, int x);

enum class StripHitKind
{
    None,
    Tab,
    Overflow,
};
struct StripHit
{
    StripHitKind Kind = StripHitKind::None;
    std::size_t Index = PanelTabSet::npos;
};
StripHit HitTestStrip(const StripLayout& layout, int x);

} // namespace sally::tabs
