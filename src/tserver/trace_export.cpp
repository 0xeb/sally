// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "trace_export.h"

#include <algorithm>

namespace
{
void AppendCell(std::wstring& line, const std::wstring& value,
                size_t width, bool leftAligned, bool delimiter)
{
    const size_t padding = width > value.size() ? width - value.size() : 0;
    if (!leftAligned)
        line.append(padding, L' ');
    line += value;
    if (leftAligned)
        line.append(padding, L' ');
    if (delimiter)
        line += L'|';
}
}

namespace TraceServerExport
{
Widths CalculateWidths(const Row& headers, const std::vector<Row>& rows)
{
    Widths widths = {};
    for (size_t column = 0; column < headers.size(); column++)
        widths[column] = headers[column].size();
    for (const Row& row : rows)
        for (size_t column = 0; column < row.size(); column++)
            widths[column] = (std::max)(widths[column], row[column].size());
    return widths;
}

std::wstring BuildLine(const Row& values, const Widths& widths, const wchar_t* prefix)
{
    static const bool leftAligned[12] = {
        false, false, true, false, false, true,
        false, false, false, true, false, true};
    std::wstring line(prefix != nullptr ? prefix : L"");
    for (size_t column = 0; column < values.size(); column++)
        AppendCell(line, values[column], widths[column], leftAligned[column],
                   column + 1 != values.size());
    line += L"\r\n";
    return line;
}

std::wstring BuildSeparator(const Widths& widths)
{
    std::wstring separator = L"-+";
    for (size_t column = 0; column < widths.size(); column++)
    {
        separator.append(widths[column], L'-');
        if (column + 1 != widths.size())
            separator += L'+';
    }
    separator += L"\r\n";
    return separator;
}
}
