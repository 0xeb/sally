// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace sally::dbviewer
{

constexpr std::size_t FindHistorySize = 10;
using FindHistoryEntries = std::array<std::wstring, FindHistorySize>;

// Moves an existing entry to the front or inserts a new entry, discarding the oldest.
// Publication is transactional: allocation failure leaves history unchanged.
bool RememberFindText(FindHistoryEntries& history, const std::wstring& text) noexcept;

} // namespace sally::dbviewer
