// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>
#include <vector>

namespace TraceServerExport
{
using Row = std::array<std::wstring, 12>;
using Widths = std::array<size_t, 12>;

Widths CalculateWidths(const Row& headers, const std::vector<Row>& rows);
std::wstring BuildLine(const Row& values, const Widths& widths, const wchar_t* prefix);
std::wstring BuildSeparator(const Widths& widths);
}
