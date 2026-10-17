// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <string_view>
#include <vector>
#include <windows.h>

namespace FileCompWrap
{
struct Span
{
    int begin = 0, end = 0;
    int columnBegin = 0, columnEnd = 0;
};

inline bool ExtendsCluster(unsigned value)
{
    if ((value >= 0x300 && value <= 0x36f) || (value >= 0x1ab0 && value <= 0x1aff) ||
           (value >= 0x1dc0 && value <= 0x1dff) || (value >= 0x20d0 && value <= 0x20ff) ||
           (value >= 0xfe00 && value <= 0xfe0f) || (value >= 0xfe20 && value <= 0xfe2f) ||
           (value >= 0x1f3fb && value <= 0x1f3ff) || (value >= 0xe0100 && value <= 0xe01ef)) return true;
    if (value >= 0x300 && value <= 0xffff)
    {
        const wchar_t character = static_cast<wchar_t>(value);
        WORD type = 0;
        if (GetStringTypeW(CT_CTYPE3, &character, 1, &type))
            return (type & (C3_NONSPACING | C3_DIACRITIC | C3_VOWELMARK)) != 0;
    }
    return false;
}

template<class Char>
unsigned CodePoint(std::basic_string_view<Char> text, int& position)
{
    unsigned value = static_cast<unsigned>(text[position++]);
    if constexpr (sizeof(Char) == 2)
        if (value >= 0xd800 && value <= 0xdbff && position < static_cast<int>(text.size()))
        {
            const unsigned low = text[position];
            if (low >= 0xdc00 && low <= 0xdfff) { ++position; return 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00; }
        }
    return value;
}

template<class Char>
int ClusterEnd(std::basic_string_view<Char> text, int begin)
{
    int end = begin;
    const unsigned first = CodePoint(text, end);
    bool join = false;
    bool regional = first >= 0x1f1e6 && first <= 0x1f1ff;
    while (end < static_cast<int>(text.size()))
    {
        int next = end;
        const unsigned value = CodePoint(text, next);
        if (ExtendsCluster(value) || value == 0x200d || join || (regional && value >= 0x1f1e6 && value <= 0x1f1ff))
        {
            end = next;
            join = value == 0x200d;
            regional = false;
        }
        else break;
    }
    return end;
}

// Cell counts intentionally match the comparator's fixed-cell renderer, including
// tab stops measured from the original logical line, never from a wrapped fragment.
template<class Char>
std::vector<Span> Split(std::basic_string_view<Char> text, int width, int tabSize)
{
    width = (std::max)(width, 1);
    tabSize = (std::max)(tabSize, 1);
    std::vector<Span> result;
    int begin = 0, column = 0;
    while (begin < static_cast<int>(text.size()))
    {
        int end = begin, current = column, breakEnd = begin, breakColumn = column;
        while (end < static_cast<int>(text.size()))
        {
            const int next = ClusterEnd(text, end);
            const int cells = text[end] == '\t' ? tabSize - current % tabSize : next - end;
            if (end != begin && current + cells - column > width) break;
            current += cells;
            if (text[end] == ' ' || text[end] == '\t') { breakEnd = next; breakColumn = current; }
            end = next;
            if (current - column >= width) break;
        }
        if (end < static_cast<int>(text.size()) && breakEnd > begin)
        {
            end = breakEnd;
            current = breakColumn;
        }
        result.push_back({begin, end, column, current});
        begin = end;
        column = current;
    }
    if (result.empty()) result.push_back({});
    return result;
}

template<class Char>
int Snap(std::basic_string_view<Char> text, int offset, bool forward)
{
    for (int begin = 0; begin < static_cast<int>(text.size()); )
    {
        const int end = ClusterEnd(text, begin);
        if (offset > begin && offset < end) return forward ? end : begin;
        if (offset <= begin) break;
        begin = end;
    }
    return offset;
}

inline std::vector<int> Align(const std::vector<int>& left, const std::vector<int>& right)
{
    std::vector<int> starts(1, 0);
    for (size_t i = 0; i < (std::max)(left.size(), right.size()); ++i)
        starts.push_back(starts.back() + (std::max)(1, (std::max)(i < left.size() ? left[i] : 0, i < right.size() ? right[i] : 0)));
    return starts;
}

inline int LogicalRow(const std::vector<int>& starts, int visual)
{
    if (starts.size() < 2) return 0;
    visual = (std::max)(0, (std::min)(visual, starts.back() - 1));
    return static_cast<int>(std::upper_bound(starts.begin(), starts.end(), visual) - starts.begin()) - 1;
}
}
