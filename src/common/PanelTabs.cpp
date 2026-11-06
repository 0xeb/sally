// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "PanelTabs.h"

#include <algorithm>

namespace sally::tabs
{

PanelTabSet::PanelTabSet()
{
    TabRecord first;
    first.Id = TakeId();
    Tabs.push_back(first);
}

std::size_t PanelTabSet::IndexOf(std::uint32_t id) const
{
    for (std::size_t i = 0; i < Tabs.size(); ++i)
    {
        if (Tabs[i].Id == id)
            return i;
    }
    return npos;
}

std::size_t PanelTabSet::InsertAfterActive(TabRecord record, bool atEnd)
{
    if (IsFull())
        return npos;
    record.Id = TakeId();
    record.Locked = false;
    const std::size_t at = atEnd ? Tabs.size() : ActiveIndex + 1;
    Tabs.insert(Tabs.begin() + static_cast<std::ptrdiff_t>(at), std::move(record));
    return at;
}

PanelTabSet::CloseResult PanelTabSet::Close(std::size_t index)
{
    return Erase(index, true);
}

PanelTabSet::CloseResult PanelTabSet::Discard(std::size_t index)
{
    return Erase(index, false);
}

PanelTabSet::CloseResult PanelTabSet::Erase(std::size_t index, bool remember)
{
    if (index >= Tabs.size())
        return CloseResult::NotFound;
    if (Tabs.size() == 1)
        return CloseResult::LastTab;
    if (remember && Tabs[index].Locked)
        return CloseResult::Locked;
    if (remember)
    {
        Closed.push_back(Tabs[index]);
        if (Closed.size() > kMaxClosed)
            Closed.erase(Closed.begin());
    }
    Tabs.erase(Tabs.begin() + static_cast<std::ptrdiff_t>(index));
    if (index < ActiveIndex)
        --ActiveIndex;
    else if (index == ActiveIndex && ActiveIndex >= Tabs.size())
        ActiveIndex = Tabs.size() - 1; // the closed tab was the last: its left neighbour
    return CloseResult::Closed;
}

std::size_t PanelTabSet::CloseOthers(std::size_t index)
{
    if (index >= Tabs.size())
        return 0;
    std::size_t closed = 0;
    const std::uint32_t keep = Tabs[index].Id;
    // Close from the right so indexes on the left stay valid; locked tabs stay.
    for (std::size_t i = Tabs.size(); i-- > 0;)
    {
        if (i == index || Tabs[i].Locked)
            continue;
        Closed.push_back(Tabs[i]);
        if (Closed.size() > kMaxClosed)
            Closed.erase(Closed.begin());
        Tabs.erase(Tabs.begin() + static_cast<std::ptrdiff_t>(i));
        ++closed;
    }
    ActiveIndex = IndexOf(keep);
    return closed;
}

bool PanelTabSet::SetLocked(std::size_t index, bool locked)
{
    if (index >= Tabs.size())
        return false;
    Tabs[index].Locked = locked;
    return true;
}

bool PanelTabSet::Rename(std::size_t index, const std::wstring& title)
{
    if (index >= Tabs.size())
        return false;
    // surrounding blanks are not part of a name
    const std::size_t first = title.find_first_not_of(L" \t");
    const std::size_t last = title.find_last_not_of(L" \t");
    Tabs[index].Title = first == std::wstring::npos ? std::wstring() : title.substr(first, last - first + 1);
    return true;
}

std::size_t PanelTabSet::ReopenClosed()
{
    if (Closed.empty() || IsFull())
        return npos;
    TabRecord record = Closed.back();
    Closed.pop_back();
    const std::uint32_t id = record.Id; // a reopened tab keeps its id (it never was reused)
    const std::size_t at = ActiveIndex + 1;
    Tabs.insert(Tabs.begin() + static_cast<std::ptrdiff_t>(at), std::move(record));
    Tabs[at].Id = id;
    return at;
}

bool PanelTabSet::Move(std::size_t from, std::size_t to)
{
    if (from >= Tabs.size() || to >= Tabs.size())
        return false;
    if (from == to)
        return true;
    const std::uint32_t activeId = Tabs[ActiveIndex].Id;
    TabRecord moving = std::move(Tabs[from]);
    Tabs.erase(Tabs.begin() + static_cast<std::ptrdiff_t>(from));
    Tabs.insert(Tabs.begin() + static_cast<std::ptrdiff_t>(to), std::move(moving));
    ActiveIndex = IndexOf(activeId);
    return true;
}

std::size_t PanelTabSet::MoveToGap(std::size_t from, std::size_t gap)
{
    if (from >= Tabs.size() || gap > Tabs.size())
        return npos;
    const std::size_t to = gap > from ? gap - 1 : gap;
    return Move(from, to) ? to : npos;
}

bool PanelTabSet::Activate(std::size_t index)
{
    if (index >= Tabs.size())
        return false;
    ActiveIndex = index;
    return true;
}

void PanelTabSet::Replace(std::vector<TabRecord> records, std::size_t active)
{
    if (records.size() > kMaxTabs)
        records.resize(kMaxTabs);
    Tabs.clear();
    Closed.clear();
    for (TabRecord& record : records)
    {
        record.Id = TakeId();
        Tabs.push_back(std::move(record));
    }
    if (Tabs.empty())
    {
        TabRecord first;
        first.Id = TakeId();
        Tabs.push_back(first);
    }
    ActiveIndex = active < Tabs.size() ? active : 0;
}

namespace
{

std::wstring TrimSeparators(std::wstring path)
{
    while (path.size() > 1 && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    return path;
}

std::wstring LastPart(const std::wstring& text)
{
    const std::wstring path = TrimSeparators(text);
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool IsDriveRoot(const std::wstring& path)
{
    return (path.size() == 2 || path.size() == 3) && path[1] == L':' &&
           (path.size() == 2 || path[2] == L'\\' || path[2] == L'/');
}

} // namespace

std::wstring DisplayTitle(const TabRecord& record)
{
    if (!record.Title.empty())
        return record.Title;
    switch (record.Kind)
    {
    case LocationKind::Disk:
    {
        if (record.Primary.empty())
            return std::wstring();
        if (IsDriveRoot(record.Primary))
            return record.Primary.substr(0, 2) + L"\\";
        const std::wstring title = LastPart(record.Primary);
        return title.empty() ? record.Primary : title;
    }
    case LocationKind::Archive:
    {
        std::wstring title = LastPart(record.Primary);
        const std::wstring inner = LastPart(record.Secondary);
        if (!inner.empty())
            // "pack.zip > docs" with U+203A; escaped, as the sally target is not compiled with /utf-8
            title += L" \u203A " + inner;
        return title;
    }
    case LocationKind::PluginFS:
    {
        const std::wstring inner = LastPart(record.Secondary);
        return inner.empty() ? record.Primary + L":" : record.Primary + L": " + inner;
    }
    }
    return std::wstring();
}

std::wstring FullLocation(const TabRecord& record)
{
    switch (record.Kind)
    {
    case LocationKind::Disk:
        return record.Primary;
    case LocationKind::Archive:
    {
        if (record.Secondary.empty())
            return record.Primary;
        std::wstring inner = record.Secondary;
        if (!inner.empty() && inner[0] != L'\\')
            inner = L"\\" + inner;
        return TrimSeparators(record.Primary) + inner;
    }
    case LocationKind::PluginFS:
        return record.Primary + L":" + record.Secondary;
    }
    return std::wstring();
}

TabValues Encode(const TabRecord& record)
{
    TabValues values;
    values.Numbers[kTabValueKind] = static_cast<DWORD>(record.Kind);
    values.Strings[kTabValuePath] = record.Primary;
    if (!record.Secondary.empty())
        values.Strings[kTabValueInner] = record.Secondary;
    values.Numbers[kTabValueView] = static_cast<DWORD>(record.ViewTemplate);
    values.Numbers[kTabValueSort] = static_cast<DWORD>(record.SortType);
    values.Numbers[kTabValueReverse] = record.ReverseSort ? 1 : 0;
    values.Numbers[kTabValueFilterEnabled] = record.FilterEnabled ? 1 : 0;
    if (!record.Filter.empty())
        values.Strings[kTabValueFilter] = record.Filter;
    if (!record.FocusedName.empty())
        values.Strings[kTabValueFocus] = record.FocusedName;
    values.Numbers[kTabValueTop] = static_cast<DWORD>(record.TopIndex < 0 ? 0 : record.TopIndex);
    if (!record.Title.empty())
        values.Strings[kTabValueTitle] = record.Title;
    if (record.Locked)
        values.Numbers[kTabValueLocked] = 1;
    return values;
}

bool Decode(const TabValues& values, int viewTemplateCount, int maxSortType, TabRecord& record)
{
    record = TabRecord();
    auto number = [&](const wchar_t* name, DWORD fallback) {
        const auto found = values.Numbers.find(name);
        return found == values.Numbers.end() ? fallback : found->second;
    };
    auto text = [&](const wchar_t* name) {
        const auto found = values.Strings.find(name);
        return found == values.Strings.end() ? std::wstring() : found->second;
    };
    const DWORD kind = number(kTabValueKind, 0);
    if (kind > static_cast<DWORD>(LocationKind::PluginFS))
        return false;
    record.Kind = static_cast<LocationKind>(kind);
    record.Primary = text(kTabValuePath);
    if (record.Primary.empty())
        return false;
    record.Secondary = text(kTabValueInner);
    const DWORD view = number(kTabValueView, 2);
    record.ViewTemplate = viewTemplateCount > 0 && view < static_cast<DWORD>(viewTemplateCount) ? static_cast<int>(view) : 2;
    if (viewTemplateCount > 0 && record.ViewTemplate >= viewTemplateCount)
        record.ViewTemplate = 0;
    const DWORD sort = number(kTabValueSort, 0);
    record.SortType = sort <= static_cast<DWORD>(maxSortType) ? static_cast<int>(sort) : 0;
    record.ReverseSort = number(kTabValueReverse, 0) != 0;
    record.FilterEnabled = number(kTabValueFilterEnabled, 0) != 0;
    record.Filter = text(kTabValueFilter);
    record.FocusedName = text(kTabValueFocus);
    const DWORD top = number(kTabValueTop, 0);
    record.TopIndex = top > 0x7fffffffUL ? 0 : static_cast<int>(top);
    record.Title = text(kTabValueTitle);
    record.Locked = number(kTabValueLocked, 0) != 0;
    record.Dormant = true;
    return true;
}

StripLayout LayOutStrip(const std::vector<int>& preferred, std::size_t active, const StripMetrics& metrics)
{
    StripLayout layout;
    const std::size_t count = preferred.size();
    layout.Slots.resize(count);
    if (count == 0 || metrics.Available <= 0)
        return layout;
    if (active >= count)
        active = 0;
    const int minWidth = (std::max)(1, metrics.MinWidth);
    const int maxWidth = (std::max)(minWidth, metrics.MaxWidth);

    std::vector<int> widths(count);
    long long total = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        widths[i] = (std::min)(maxWidth, (std::max)(minWidth, preferred[i]));
        total += widths[i];
    }

    if (total > metrics.Available)
    {
        // Shrink every tab evenly first; they keep at least MinWidth.
        const int even = static_cast<int>(metrics.Available / static_cast<long long>(count));
        if (even >= minWidth)
        {
            for (int& width : widths)
                width = (std::min)(width, even);
            total = 0;
            for (int width : widths)
                total += width;
        }
        else
        {
            // Not even the narrowest tabs fit: show a window of tabs around the active one and an
            // overflow button that lists all of them.
            layout.Overflow = true;
            // A strip too narrow for even one tab keeps just the overflow button, inside the strip.
            const int room = (std::max)(0, metrics.Available - metrics.OverflowWidth);
            std::size_t visible = static_cast<std::size_t>(room / minWidth);
            if (visible > count)
                visible = count;
            std::size_t first = active + 1 > visible ? active + 1 - visible : 0;
            if (first + visible > count)
                first = count - visible;
            int x = 0;
            for (std::size_t i = first; i < first + visible; ++i)
            {
                layout.Slots[i].Visible = true;
                layout.Slots[i].Left = x;
                x += minWidth;
                layout.Slots[i].Right = x;
            }
            layout.OverflowLeft = (std::max)(x, room);
            return layout;
        }
    }

    int x = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        layout.Slots[i].Visible = true;
        layout.Slots[i].Left = x;
        x += widths[i];
        layout.Slots[i].Right = x;
    }
    return layout;
}

std::size_t DropGapAt(const StripLayout& layout, int x)
{
    std::size_t gap = 0;
    for (std::size_t i = 0; i < layout.Slots.size(); ++i)
    {
        const auto& slot = layout.Slots[i];
        if (!slot.Visible)
            continue;
        if (x < (slot.Left + slot.Right) / 2)
            return i;
        gap = i + 1;
    }
    return gap;
}

StripHit HitTestStrip(const StripLayout& layout, int x)
{
    StripHit hit;
    for (std::size_t i = 0; i < layout.Slots.size(); ++i)
    {
        const auto& slot = layout.Slots[i];
        if (slot.Visible && x >= slot.Left && x < slot.Right)
        {
            hit.Kind = StripHitKind::Tab;
            hit.Index = i;
            return hit;
        }
    }
    if (layout.Overflow && x >= layout.OverflowLeft)
        hit.Kind = StripHitKind::Overflow;
    return hit;
}

} // namespace sally::tabs
