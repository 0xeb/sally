// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

int CTextFileViewWindowBase::VisualRowCount() const
{
    return WordWrap && !WrapStarts.empty() ? WrapStarts.back() : static_cast<int>(Script[ViewMode].size());
}

int CTextFileViewWindowBase::LogicalRow(int visual) const
{
    return WordWrap && !WrapStarts.empty() ? FileCompWrap::LogicalRow(WrapStarts, visual) : visual;
}

int CTextFileViewWindowBase::VisualRow(int logical) const
{
    if (!WordWrap || WrapStarts.empty()) return logical;
    return WrapStarts[(std::max)(0, (std::min)(logical, static_cast<int>(WrapStarts.size()) - 1))];
}

const FileCompWrap::Span* CTextFileViewWindowBase::VisualSpan(int visual) const
{
    if (!WordWrap || WrapStarts.size() < 2 || visual < 0 || visual >= WrapStarts.back()) return nullptr;
    const int logical = LogicalRow(visual);
    const int subrow = visual - WrapStarts[logical];
    return subrow < static_cast<int>(WrapLines[logical].size()) ? &WrapLines[logical][subrow] : nullptr;
}

int CTextFileViewWindowBase::CaretVisualRow() const
{
    if (!WordWrap || WrapStarts.size() < 2 || CaretYPos >= static_cast<int>(WrapLines.size())) return CaretYPos;
    const auto* hint = VisualSpan(CaretVisualHint);
    if (hint && LogicalRow(CaretVisualHint) == CaretYPos && CaretXPos >= hint->begin && CaretXPos <= hint->end)
        return CaretVisualHint;
    const auto& spans = WrapLines[CaretYPos];
    if (!hint && CaretVisualHint >= 0 && CaretVisualHint < VisualRowCount() &&
        LogicalRow(CaretVisualHint) == CaretYPos && !spans.empty() && CaretXPos == spans.back().end)
        return CaretVisualHint; // visual alignment padding still belongs to this source line
    for (int i = 0; i < static_cast<int>(spans.size()); ++i)
        if (CaretXPos < spans[i].end || i + 1 == static_cast<int>(spans.size())) return WrapStarts[CaretYPos] + i;
    return WrapStarts[CaretYPos];
}

void CTextFileViewWindowBase::RebuildWrapLayout()
{
    if (!WordWrap || !DataValid || !Siblink || !Siblink->Is(fvtText)) return;
    auto& other = *static_cast<CTextFileViewWindowBase*>(Siblink);
    const int anchor = LogicalRow(FirstVisibleLine);
    const int subrow = FirstVisibleLine - VisualRow(anchor);
    const int columns = (std::max)(1L, (Width - LineNumWidth) / (std::max)(1L, FontWidth));
    const int otherColumns = (std::max)(1L, (other.Width - other.LineNumWidth) / (std::max)(1L, other.FontWidth));
    std::vector<int> ownCounts, otherCounts;
    WrapLines.clear();
    WrapLines.reserve(Script[ViewMode].size());
    for (int row = 0; row < static_cast<int>(Script[ViewMode].size()); ++row)
    {
        WrapLines.push_back(WrapRow(row, columns));
        ownCounts.push_back(static_cast<int>(WrapLines.back().size()));
        otherCounts.push_back(static_cast<int>(other.WrapRow(row, otherColumns).size()));
    }
    WrapStarts = FileCompWrap::Align(ownCounts, otherCounts);
    if (WrapLines.empty()) FirstVisibleLine = 0;
    else
    {
        const int row = (std::min)(anchor, static_cast<int>(WrapLines.size()) - 1);
        FirstVisibleLine = WrapStarts[row] + (std::min)(subrow, WrapStarts[row + 1] - WrapStarts[row] - 1);
    }
    CaretVisualHint = -1;
}

void CTextFileViewWindowBase::SetWordWrap(BOOL value)
{
    const int anchor = LogicalRow(FirstVisibleLine);
    WordWrap = value;
    WrapLines.clear();
    WrapStarts.clear();
    FirstVisibleLine = anchor;
    CaretVisualHint = -1;
    FirstVisibleChar = 0;
    UpdateScrollBars();
    InvalidateRect(HWindow, nullptr, FALSE);
    UpdateCaretPos(FALSE);
}

int CTextFileViewWindowBase::HitTestLine(int y)
{
    HitVisualRow = (std::max)(0, (std::min)(FirstVisibleLine + y / static_cast<int>(FontHeight), VisualRowCount() - 1));
    return LogicalRow(HitVisualRow);
}

int CTextFileViewWindowBase::HitTestChar(int x, int logical)
{
    if (Script[ViewMode].empty() || Script[ViewMode][logical].IsBlank()) return 0;
    if (!WordWrap) return TransformXOrg(x, logical);
    const auto* span = VisualSpan(HitVisualRow);
    CaretVisualHint = HitVisualRow;
    if (!span) return LineLength(Script[ViewMode][logical].GetLine()); // alignment padding: end of source line
    const int saved = FirstVisibleChar;
    FirstVisibleChar = span->columnBegin;
    int source = TransformXOrg(x, logical);
    FirstVisibleChar = saved;
    CaretVisualHint = HitVisualRow;
    return SnapSource(logical, (std::max)(span->begin, (std::min)(source, span->end)), false);
}

void CTextFileViewWindowBase::MoveWrappedCaret(int delta, BOOL constrainDifference)
{
    if (!DataValid || VisualRowCount() == 0) return;
    const int current = CaretVisualRow();
    int target = (std::max)(0, (std::min)(current + delta, VisualRowCount() - 1));
    if (constrainDifference)
    {
        const int direction = target < current ? -1 : 1;
        for (int row = current; row != target; row += direction)
            if (Script[ViewMode][LogicalRow(row + direction)].GetClass() == CLineSpec::lcSeparator)
            { target = row; break; }
    }
    int column = 0;
    const auto* span = VisualSpan(current);
    if (span && !Script[ViewMode][CaretYPos].IsBlank())
        column = MeasureLine(Script[ViewMode][CaretYPos].GetLine(), CaretXPos) - span->columnBegin;
    HitVisualRow = target;
    const int logical = LogicalRow(target);
    const int source = HitTestChar(LineNumWidth + column * FontWidth, logical);
    CaretVisualHint = target;
    SetCaretPos(source, logical);
}
